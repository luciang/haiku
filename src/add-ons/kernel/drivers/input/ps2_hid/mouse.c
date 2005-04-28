/*
 * Copyright 2001-2005 Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * PS/2 mouse device driver
 *
 * Authors (in chronological order):
 * 		Elad Lahav (elad@eldarshany.com)
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 */

/*
 * A PS/2 mouse is connected to the IBM 8042 controller, and gets its
 * name from the IBM PS/2 personal computer, which was the first to
 * use this device. All resources are shared between the keyboard, and
 * the mouse, referred to as the "Auxiliary Device".
 *
 * I/O:
 * ~~~
 * The controller has 3 I/O registers:
 * 1. Status (input), mapped to port 64h
 * 2. Control (output), mapped to port 64h
 * 3. Data (input/output), mapped to port 60h
 *
 * Data:
 * ~~~~
 * A packet read from the mouse data port is composed of
 * three bytes:
 * byte 0: status byte, where
 * - bit 0: Y overflow (1 = true)
 * - bit 1: X overflow (1 = true)
 * - bit 2: MSB of Y offset
 * - bit 3: MSB of X offset
 * - bit 4: Syncronization bit (always 1)
 * - bit 5: Middle button (1 = down)
 * - bit 6: Right button (1 = down)
 * - bit 7: Left button (1 = down)
 * byte 1: X position change, since last probed (-127 to +127)
 * byte 2: Y position change, since last probed (-127 to +127)
 * 
 * Intellimouse mice send a four byte packet, where the first three
 * bytes are the same as standard mice, and the last one reports the
 * Z position, which is, usually, the wheel movement.
 *
 * Interrupts:
 * ~~~~~~~~~~
 * The PS/2 mouse device is connected to interrupt 12, which means that
 * it uses the second interrupt controller (handles INT8 to INT15). In
 * order for this interrupt to be enabled, both the 5th interrupt of
 * the second controller AND the 3rd interrupt of the first controller
 * (cascade mode) should be unmasked.
 * This is all done inside install_io_interrupt_handler(), no need to
 * worry about it anymore
 * The controller uses 3 consecutive interrupts to inform the computer
 * that it has new data. On the first the data register holds the status
 * byte, on the second the X offset, and on the 3rd the Y offset.
 */


#include <Drivers.h>
#include <string.h>

#include "kb_mouse_driver.h"
#include "common.h"
#include "packet_buffer.h"


#define MOUSE_HISTORY_SIZE	256
	// we record that many mouse packets before we start to drop them

static sem_id sMouseSem;
static int32 sSync;
static struct packet_buffer *sMouseBuffer;
static int32 sOpenMask;

static bigtime_t sLastClickTime;
static bigtime_t sClickSpeed;
static int32 sClickCount;
static int sButtonsState;

static int32 sPacketSize;
 

/** Writes a byte to the mouse device. Uses the control port to indicate
 *	that the byte is sent to the auxiliary device (mouse), instead of the
 *	keyboard.
 */

status_t
ps2_write_aux_byte(uint8 data)
{
	TRACE(("ps2_write_aux_byte(data = %u)\n", data));

	if (ps2_write_ctrl(PS2_CTRL_WRITE_AUX) == B_OK
		&& ps2_write_data(data) == B_OK
		&& ps2_read_data(&data) == B_OK)
		return data == PS2_REPLY_ACK ? B_OK : B_TIMED_OUT;

	return B_ERROR;
}


/*
static status_t
ps2_reset_mouse()
{
	int8 read;
	
	TRACE(("ps2_reset_mouse()\n"));
	
	write_aux_byte(PS2_CMD_RESET_MOUSE);
	read = read_data_byte();
	
	TRACE(("reset mouse: %2x\n", read));
	return B_OK;	
}
*/


/** Set sampling rate of the ps2 port.
 */
 
static status_t
ps2_set_sample_rate(uint32 rate)
{
	int32 tries = 5;

	while (--tries > 0) {
		status_t status = ps2_write_aux_byte(PS2_CMD_SET_SAMPLE_RATE);
		if (status == B_OK)
			status = ps2_write_aux_byte(rate);

		if (status == B_OK)
			return B_OK;
	}

	return B_ERROR;
}


/** Converts a packet received by the mouse to a "movement".
 */  
 
static void
ps2_packet_to_movement(uint8 packet[], mouse_movement *pos)
{
	int buttons = packet[0] & 7;
	int xDelta = ((packet[0] & 0x10) ? 0xFFFFFF00 : 0) | packet[1];
	int yDelta = ((packet[0] & 0x20) ? 0xFFFFFF00 : 0) | packet[2];
	bigtime_t currentTime = system_time();
	int8 wheel_ydelta = 0;
	int8 wheel_xdelta = 0;
	
	if (buttons != 0 && sButtonsState == 0) {
		if (sLastClickTime + sClickSpeed > currentTime)
			sClickCount++;
		else
			sClickCount = 1;
	}

	sLastClickTime = currentTime;
	sButtonsState = buttons;

	if (sPacketSize == PS2_PACKET_INTELLIMOUSE) { 
		switch (packet[3] & 0x0F) {
			case 0x01: wheel_ydelta = +1; break; // wheel 1 down
			case 0x0F: wheel_ydelta = -1; break; // wheel 1 up
			case 0x02: wheel_xdelta = +1; break; // wheel 2 down
			case 0x0E: wheel_xdelta = -1; break; // wheel 2 up
		}
	}

	if (pos) {
		pos->xdelta = xDelta;
		pos->ydelta = yDelta;
		pos->buttons = buttons;
		pos->clicks = sClickCount;
		pos->modifiers = 0;
		pos->timestamp = currentTime;
		pos->wheel_ydelta = (int)wheel_ydelta;
		pos->wheel_xdelta = (int)wheel_xdelta;

		TRACE(("xdelta: %d, ydelta: %d, buttons %x, clicks: %ld, timestamp %Ld\n",
			xDelta, yDelta, buttons, sClickCount, currentTime));
	}
}


/** Read a mouse event from the mouse events chain buffer.
 */

static status_t
ps2_mouse_read(mouse_movement *userMovement)
{
	uint8 packet[PS2_MAX_PACKET_SIZE];
	mouse_movement movement;
	status_t status;

	TRACE(("ps2_mouse_read()\n"));
	status = acquire_sem_etc(sMouseSem, 1, B_CAN_INTERRUPT, 0);
	if (status < B_OK)
		return status;

	if (packet_buffer_read(sMouseBuffer, packet, sPacketSize) < (size_t)sPacketSize) {
		TRACE(("error copying buffer\n"));
		return status;
	}

	ps2_packet_to_movement(packet, &movement);

	return user_memcpy(userMovement, &movement, sizeof(mouse_movement));
}


/** Enables or disables mouse reporting for the PS/2 port.
 */

static status_t
set_mouse_enabled(bool enable)
{
	int32 tries = 5;

	while (true) {
		if (ps2_write_aux_byte(enable ?
				PS2_CMD_ENABLE_MOUSE : PS2_CMD_DISABLE_MOUSE) == B_OK)
			return B_OK;

		if (--tries <= 0)
			break;
	}

	return B_ERROR;
}


/** Interrupt handler for the mouse device. Called whenever the I/O
 *	controller generates an interrupt for the PS/2 mouse. Reads mouse
 *	information from the data port, and stores it, so it can be accessed
 *	by read() operations. The full data is obtained using 3 consecutive
 *	calls to the handler, each holds a different byte on the data port.
 */

static int32 
handle_mouse_interrupt(void* data)
{
	int8 read = gIsa->read_io_8(PS2_PORT_CTRL);
	if (read < 0) {
		TRACE(("Interrupt was not generated by the ps2 mouse\n"));
		return B_UNHANDLED_INTERRUPT;
	}

	read = gIsa->read_io_8(PS2_PORT_DATA);

	TRACE(("mouse interrupt : byte %x\n", read));
	while (packet_buffer_write(sMouseBuffer, &read, sizeof(read)) == 0) {
		// we start dropping packets when the buffer is full
		packet_buffer_flush(sMouseBuffer, sPacketSize);
	}

	if (sSync == 0 && !(read & 8)) {
		TRACE(("mouse resynched, bad data\n"));
		return B_HANDLED_INTERRUPT;
	}

	if (++sSync == sPacketSize) {
		TRACE(("mouse synched\n"));
		sSync = 0;
		release_sem_etc(sMouseSem, 1, B_DO_NOT_RESCHEDULE);

		return B_INVOKE_SCHEDULER;
	}

	return B_HANDLED_INTERRUPT;
}


//	#pragma mark -


status_t
probe_mouse(void)
{
	int8 deviceId = -1;

	// get device id
	if (ps2_write_aux_byte(PS2_CMD_GET_DEVICE_ID) == B_OK)
		ps2_read_data(&deviceId);

	TRACE(("probe_mouse(): device id: %2x\n", deviceId));		

	if (deviceId == 0) {
		int32 tries = 5;

		while (--tries > 0) {
			// try to switch to intellimouse mode
			if (ps2_set_sample_rate(200) == B_OK
				&& ps2_set_sample_rate(100) == B_OK
				&& ps2_set_sample_rate(80) == B_OK) {
				// get device id, again
				if (ps2_write_aux_byte(PS2_CMD_GET_DEVICE_ID) == B_OK)
					ps2_read_data(&deviceId);
				break;
			}
		}
	}

	if (deviceId == PS2_DEV_ID_STANDARD) {
		sPacketSize = PS2_PACKET_STANDARD;
		TRACE(("Standard PS/2 mouse found\n"));
	} else if (deviceId == PS2_DEV_ID_INTELLIMOUSE) {
		sPacketSize = PS2_PACKET_INTELLIMOUSE;
		TRACE(("Extended PS/2 mouse found\n"));
	} else {
		// Something's wrong. Better quit
		dprintf("ps2_hid: No mouse found\n");
		return B_ERROR;
	}

	return B_OK;
}


//	#pragma mark -
//	Device functions


status_t 
mouse_open(const char *name, uint32 flags, void **_cookie)
{
	status_t status;	
	int8 commandByte;

	TRACE(("mouse_open()\n"));	

	if (atomic_or(&sOpenMask, 1) != 0)
		return B_BUSY;

	acquire_sem(gDeviceOpenSemaphore);

	status = probe_mouse();
	if (status != B_OK)
		goto err1;

	status = ps2_common_initialize();
	if (status != B_OK)
		goto err1;

	sMouseBuffer = create_packet_buffer(MOUSE_HISTORY_SIZE * sPacketSize);
	if (sMouseBuffer == NULL) {
		TRACE(("can't allocate mouse actions buffer\n"));
		status = B_NO_MEMORY;
		goto err2;
	}

	// create the mouse semaphore, used for synchronization between
	// the interrupt handler and the read operation
	sMouseSem = create_sem(0, "ps2_mouse_sem");
	if (sMouseSem < 0) {
		TRACE(("failed creating PS/2 mouse semaphore!\n"));
		status = sMouseSem;
		goto err3;
	}

	*_cookie = NULL;

	commandByte = ps2_get_command_byte() | PS2_BITS_AUX_INTERRUPT;
	commandByte &= ~PS2_BITS_MOUSE_DISABLED;

	status = ps2_set_command_byte(commandByte);
	if (status < B_OK) {
		TRACE(("mouse_open(): sending command byte failed\n"));
		goto err4;
	}

	status = set_mouse_enabled(true);
	if (status < B_OK) {
		TRACE(("mouse_open(): cannot enable PS/2 mouse\n"));	
		goto err4;
	}

	status = install_io_interrupt_handler(INT_PS2_MOUSE,
		handle_mouse_interrupt, NULL, 0);
	if (status < B_OK) {
		TRACE(("mouse_open(): cannot install interrupt handler\n"));
		goto err4;
	}

	release_sem(gDeviceOpenSemaphore);

	TRACE(("mouse_open(): mouse succesfully enabled\n"));
	return B_OK;

err4:
	delete_sem(sMouseSem);
err3:
	delete_packet_buffer(sMouseBuffer);
err2:
	ps2_common_uninitialize();
err1:
	atomic_and(&sOpenMask, 0);
	release_sem(gDeviceOpenSemaphore);

	return status;
}


status_t
mouse_close(void *cookie)
{
	TRACE(("mouse_close()\n"));

	set_mouse_enabled(false);

	delete_packet_buffer(sMouseBuffer);
	delete_sem(sMouseSem);

	remove_io_interrupt_handler(INT_PS2_MOUSE, handle_mouse_interrupt, NULL);

	ps2_common_uninitialize();
	atomic_and(&sOpenMask, 0);

	return B_OK;
}


status_t
mouse_freecookie(void *cookie)
{
	return B_OK;
}


status_t
mouse_read(void *cookie, off_t pos, void *buffer, size_t *_length)
{
	*_length = 0;
	return B_NOT_ALLOWED;
}


status_t
mouse_write(void *cookie, off_t pos, const void *buffer, size_t *_length)
{
	*_length = 0;
	return B_NOT_ALLOWED;
}


status_t
mouse_ioctl(void *cookie, uint32 op, void *buffer, size_t length)
{
	switch (op) {
		case MS_NUM_EVENTS:
		{
			int32 count;
			TRACE(("MS_NUM_EVENTS\n"));
			get_sem_count(sMouseSem, &count);
			return count;
		}

		case MS_READ:
			TRACE(("MS_READ\n"));	
			return ps2_mouse_read((mouse_movement *)buffer);

		case MS_SET_TYPE:
			TRACE(("MS_SET_TYPE not implemented\n"));
			return B_BAD_VALUE;

		case MS_SET_MAP:
			TRACE(("MS_SET_MAP (set mouse mapping) not implemented\n"));
			return B_BAD_VALUE;

		case MS_GET_ACCEL:
			TRACE(("MS_GET_ACCEL (get mouse acceleration) not implemented\n"));
			return B_BAD_VALUE;

		case MS_SET_ACCEL:
			TRACE(("MS_SET_ACCEL (set mouse acceleration) not implemented\n"));
			return B_BAD_VALUE;

		case MS_SET_CLICKSPEED:
			TRACE(("MS_SETCLICK (set click speed)\n"));
			return user_memcpy(&sClickSpeed, buffer, sizeof(bigtime_t));

		default:
			TRACE(("unknown opcode: %ld\n", op));
			return B_BAD_VALUE;
	}
}


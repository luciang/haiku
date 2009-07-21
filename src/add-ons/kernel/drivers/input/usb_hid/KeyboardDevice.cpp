/*
 * Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */

/*!	Driver for USB Human Interface Devices.
 	Interpretation code based on the previous usb_hid driver which was written
	by Jérôme Duval.
*/


#include <string.h>

#include <usb/USB_hid.h>

#include <debug.h>

#include "Driver.h"
#include "KeyboardDevice.h"

// input server private for raw_key_info, KB_READ, etc...
#include "kb_mouse_driver.h"


static usb_id sDebugKeyboardPipe = 0;
static size_t sDebugKeyboardReportSize = 0;
static int32 sDebuggerCommandAdded = 0;


static int
debug_get_keyboard_config(int argc, char **argv)
{
	set_debug_variable("_usbPipeID", (uint64)sDebugKeyboardPipe);
	set_debug_variable("_usbReportSize", (uint64)sDebugKeyboardReportSize);
	return 0;
}


//	#pragma mark -


KeyboardDevice::KeyboardDevice(usb_device device, usb_pipe interruptPipe,
	size_t interfaceIndex, report_insn *instructions, size_t instructionCount,
	size_t totalReportSize)
	:
	HIDDevice(device, interruptPipe, interfaceIndex, instructions,
		instructionCount, totalReportSize, 512),
	fRepeatDelay(300000),
	fRepeatRate(35000),
	fLastTransferBuffer(NULL)
{
	fCurrentRepeatDelay = B_INFINITE_TIMEOUT;
	fCurrentRepeatKey = 0;

	fLastTransferBuffer = (uint8 *)malloc(totalReportSize);
	if (fLastTransferBuffer == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	SetBaseName("input/keyboard/usb/");

	if (atomic_add(&sDebuggerCommandAdded, 1) == 0) {
		add_debugger_command("get_usb_keyboard_config",
			&debug_get_keyboard_config,
			"Gets the required config of the USB keyboard");
	}
}


KeyboardDevice::~KeyboardDevice()
{
	free(fLastTransferBuffer);

	if (atomic_add(&sDebuggerCommandAdded, -1) == 1) {
		remove_debugger_command("get_usb_keyboard_config",
			&debug_get_keyboard_config);
	}
}


status_t
KeyboardDevice::Open(uint32 flags)
{
	status_t status = HIDDevice::Open(flags);
	if (status != B_OK) {
		TRACE_ALWAYS("keyboard device failed to open: %s\n",
			strerror(status));
		return status;
	}

	fCurrentRepeatDelay = B_INFINITE_TIMEOUT;
	fCurrentRepeatKey = 0;

	return B_OK;
}


status_t
KeyboardDevice::Control(uint32 op, void *buffer, size_t length)
{
	switch (op) {
		case KB_READ:
		{
			bigtime_t enterTime = system_time();
			while (_RingBufferReadable() == 0) {
				if (!_IsTransferUnprocessed()) {
					status_t result = _ScheduleTransfer();
					if (result != B_OK)
						return result;
				}

				// NOTE: this thread is now blocking until the semaphore is
				// released from the callback function or the repeat timeout
				// expires
				status_t result = acquire_sem_etc(fTransferNotifySem, 1,
					B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT, fCurrentRepeatDelay);
				if (result == B_OK) {
					result = _InterpretBuffer();
					_SetTransferProcessed();
					if (result != B_OK)
						return result;
				} else if (result != B_TIMED_OUT)
					return result;

				if (!IsOpen())
					return B_ERROR;

				if (_RingBufferReadable() == 0 && fCurrentRepeatKey != 0
					&& system_time() - enterTime > fCurrentRepeatDelay) {
					// this case is for handling key repeats, it means no
					// interrupt transfer has happened or it didn't produce any
					// new key events, but a repeated key down is due
					_WriteKey(fCurrentRepeatKey, true);

					// the next timeout is reduced to the repeat_rate
					fCurrentRepeatDelay = fRepeatRate;
					break;
				}
			}

			// process what is in the ring_buffer, it could be written
			// there because we handled an interrupt transfer or because
			// we wrote the current repeat key
			return _RingBufferRead(buffer, sizeof(raw_key_info));
		}

		case KB_SET_LEDS:
		{
			uint8 ledData[4];
			if (user_memcpy(ledData, buffer, sizeof(ledData)) != B_OK)
				return B_BAD_ADDRESS;
			return _SetLEDs(ledData);
		}

		case KB_SET_KEY_REPEAT_RATE:
		{
			int32 repeatRate;
			if (user_memcpy(&repeatRate, buffer, sizeof(repeatRate)) != B_OK)
				return B_BAD_ADDRESS;

			if (repeatRate == 0 || repeatRate > 1000000)
				return B_BAD_VALUE;

			fRepeatRate = 10000000 / repeatRate;
			return B_OK;
		}

		case KB_GET_KEY_REPEAT_RATE:
		{
			int32 repeatRate = 10000000 / fRepeatRate;
			if (user_memcpy(buffer, &repeatRate, sizeof(repeatRate)) != B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
		}

		case KB_SET_KEY_REPEAT_DELAY:
			if (user_memcpy(&fRepeatDelay, buffer, sizeof(fRepeatDelay))
					!= B_OK)
				return B_BAD_ADDRESS;
			return B_OK;

		case KB_GET_KEY_REPEAT_DELAY:
			if (user_memcpy(buffer, &fRepeatDelay, sizeof(fRepeatDelay))
					!= B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
	}

	TRACE_ALWAYS("keyboard device unhandled control 0x%08lx\n", op);
	return B_ERROR;
}


void
KeyboardDevice::_WriteKey(uint32 key, bool down)
{
	raw_key_info info;
	info.be_keycode = key;
	info.is_keydown = down;
	info.timestamp = system_time();

	_RingBufferWrite(&info, sizeof(raw_key_info));
}


status_t
KeyboardDevice::_SetLEDs(uint8 *data)
{
	if (IsRemoved())
		return B_ERROR;

	uint8 leds = 0;
	if (data[0] == 1)
		leds |= (1 << 0);
	if (data[1] == 1)
		leds |= (1 << 1);
	if (data[2] == 1)
		leds |= (1 << 2);

	size_t actualLength;
	return gUSBModule->send_request(fDevice,
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		USB_REQUEST_HID_SET_REPORT,
		0x200 | 0 /* TODO: report id */, fInterfaceIndex,
		sizeof(leds), &leds, &actualLength);
}


status_t
KeyboardDevice::_InterpretBuffer()
{
	if (fTransferStatus != B_OK) {
		if (IsRemoved())
			return B_ERROR;

		if (fTransferStatus == B_DEV_STALLED
			&& gUSBModule->clear_feature(fInterruptPipe,
				USB_FEATURE_ENDPOINT_HALT) != B_OK)
			return B_ERROR;

		return B_OK;
	}

	const static uint32 sModifierTable[] = {
		KEY_ControlL,
		KEY_ShiftL,
		KEY_AltL,
		KEY_WinL,
		KEY_ControlR,
		KEY_ShiftR,
		KEY_AltR,
		KEY_WinR
	};

	const static uint32 sKeyTable[] = {
		0x00,	// ERROR
		0x00,	// ERROR
		0x00,	// ERROR
		0x00,	// ERROR
		0x3c,	// A
		0x50,	// B
		0x4e,	// C
		0x3e,	// D
		0x29,	// E
		0x3f,	// F
		0x40,	// G
		0x41,	// H
		0x2e,	// I
		0x42,	// J
		0x43,	// K
		0x44,	// L
		0x52,	// M
		0x51,	// N
		0x2f,	// O
		0x30,	// P
		0x27,	// Q
		0x2a,	// R
		0x3d,	// S
		0x2b,	// T
		0x2d,	// U
		0x4f,	// V
		0x28,	// W
		0x4d,	// X
		0x2c,	// Y
		0x4c,	// Z
		0x12,	// 1
		0x13,	// 2
		0x14,	// 3
		0x15,	// 4
		0x16,	// 5
		0x17,	// 6
		0x18,	// 7
		0x19,	// 8
		0x1a,	// 9
		0x1b,	// 0
		0x47,	// enter
		0x01,	// Esc
		0x1e,	// Backspace
		0x26,	// Tab
		0x5e,	// Space
		0x1c,	// -
		0x1d,	// =
		0x31,	// [
		0x32,	// ]
		0x33,	// backslash
		0x33,	// backslash
		0x45,	// ;
		0x46,	// '
		0x11,	// `
		0x53,	// ,
		0x54,	// .
		0x55,	// /
		KEY_CapsLock,	// Caps
		0x02,	// F1
		0x03,	// F2
		0x04,	// F3
		0x05,	// F4
		0x06,	// F5
		0x07,	// F6
		0x08,	// F7
		0x09,	// F8
		0x0a,	// F9
		0x0b,	// F10
		0x0c,	// F11
		0x0d,	// F12
		0x0e,	// PrintScreen
		KEY_Scroll,	// Scroll Lock
		KEY_Pause,	// Pause (0x7f with Ctrl)
		0x1f,	// Insert
		0x20,	// Home
		0x21,	// Page up
		0x34,	// Delete
		0x35,	// End
		0x36,	// Page down
		0x63,	// Right arrow
		0x61,	// Left arrow
		0x62,	// Down arrow
		0x57,	// Up arrow
		0x22,	// Num Lock
		0x23,	// Pad /
		0x24,	// Pad *
		0x25,	// Pad -
		0x3a,	// Pad +
		0x5b,	// Pad Enter
		0x58,	// Pad 1
		0x59,	// Pad 2
		0x5a,	// Pad 3
		0x48,	// Pad 4
		0x49,	// Pad 5
		0x4a,	// Pad 6
		0x37,	// Pad 7
		0x38,	// Pad 8
		0x39,	// Pad 9
		0x64,	// Pad 0
		0x65,	// Pad .
		0x69,	// <
		KEY_Menu,	// Menu
		KEY_Power,	// Power
		KEY_NumEqual,	// Pad =
		0x00,	// F13 unmapped
		0x00,	// F14 unmapped
		0x00,	// F15 unmapped
		0x00,	// F16 unmapped
		0x00,	// F17 unmapped
		0x00,	// F18 unmapped
		0x00,	// F19 unmapped
		0x00,	// F20 unmapped
		0x00,	// F21 unmapped
		0x00,	// F22 unmapped
		0x00,	// F23 unmapped
		0x00,	// F24 unmapped
		0x00,	// Execute unmapped
		0x00,	// Help unmapped
		0x00,	// Menu unmapped
		0x00,	// Select unmapped
		0x00,	// Stop unmapped
		0x00,	// Again unmapped
		0x00,	// Undo unmapped
		0x00,	// Cut unmapped
		0x00,	// Copy unmapped
		0x00,	// Paste unmapped
		0x00,	// Find unmapped
		0x00,	// Mute unmapped
		0x00,	// Volume up unmapped
		0x00,	// Volume down unmapped
		0x00,	// CapsLock unmapped
		0x00,	// NumLock unmapped
		0x00,	// Scroll lock unmapped
		0x70,	// Keypad . on Brazilian ABNT2
		0x00,	// = sign
		0x6b,	// Ro (\\ key, japanese)
		0x6e,	// Katakana/Hiragana, second key right to spacebar, japanese
		0x6a,	// Yen (macron key, japanese)
		0x6d,	// Henkan, first key right to spacebar, japanese
		0x6c,	// Muhenkan, key left to spacebar, japanese
	};

	static size_t sKeyTableSize = sizeof(sKeyTable) / sizeof(sKeyTable[0]);

	uint8 modifierChange = fLastTransferBuffer[0] ^ fTransferBuffer[0];
	for (uint8 i = 0; modifierChange; i++, modifierChange >>= 1) {
		if (modifierChange & 1)
			_WriteKey(sModifierTable[i], (fTransferBuffer[0] >> i) & 1);
	}

	bool phantomState = true;
	for (size_t i = 2; i < fTotalReportSize; i++) {
		if (fTransferBuffer[i] != 0x01) {
			phantomState = false;
			break;
		}
	}

	if (phantomState) {
		// no valid key information is present in this state and we don't
		// want to overwrite our last buffer as otherwise we generate
		// spurious key ups now and spurious key downs when leaving the
		// phantom state again
		return B_OK;
	}

	static bool sysReqPressed = false;

	bool keyDown = false;
	uint8 *current = fLastTransferBuffer;
	uint8 *compare = fTransferBuffer;
	for (int32 twice = 0; twice < 2; twice++) {
		for (size_t i = 2; i < fTotalReportSize; i++) {
			if (current[i] == 0x00 || current[i] == 0x01)
				continue;

			bool found = false;
			for (size_t j = 2; j < fTotalReportSize; j++) {
				if (compare[j] == current[i]) {
					found = true;
					break;
				}
			}

			if (found)
				continue;

			// a change occured
			uint32 key = 0;
			if (current[i] < sKeyTableSize)
				key = sKeyTable[current[i]];

			if (key == KEY_Pause && (current[0] & 1))
				key = KEY_Break;
			else if (key == 0xe && (current[0] & 1)) {
				key = KEY_SysRq;
				sysReqPressed = keyDown;
			} else if (sysReqPressed && keyDown && current[i] >= 4
				&& current[i] <= 30 && (fLastTransferBuffer[0] & 0x44) != 0) {
				// Alt-SysReq+letter was pressed
				sDebugKeyboardPipe = fInterruptPipe;
				sDebugKeyboardReportSize = fTotalReportSize;

				char letter = current[i] - 4 + 'a';

				if (debug_emergency_key_pressed(letter))
					continue;
			} else if (key == 0) {
				// unmapped key
				key = 0x200000 + current[i];
			}

			_WriteKey(key, keyDown);

			if (keyDown) {
				// repeat handling
				fCurrentRepeatKey = key;
				fCurrentRepeatDelay = fRepeatDelay;
			} else {
				// cancel the repeats if they are for this key
				if (fCurrentRepeatKey == key) {
					fCurrentRepeatDelay = B_INFINITE_TIMEOUT;
					fCurrentRepeatKey = 0;
				}
			}
		}

		current = fTransferBuffer;
		compare = fLastTransferBuffer;
		keyDown = true;
	}

	memcpy(fLastTransferBuffer, fTransferBuffer, fTotalReportSize);
	return B_OK;
}

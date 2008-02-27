/*
 * Copyright 2005-2007 Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * PS/2 bus manager
 *
 * Authors (in chronological order):
 *		Marcus Overhagen (marcus@overhagen.de)
 */


#include "ps2_dev.h"
#include "ps2_service.h"

#include <fs/devfs.h>

#include <string.h>


ps2_dev ps2_device[PS2_DEVICE_COUNT] = {
	{ .name = "input/mouse/ps2/0",   .active = false, .idx = 0, .result_sem = -1 },
	{ .name = "input/mouse/ps2/1",   .active = false, .idx = 1, .result_sem = -1 },
	{ .name = "input/mouse/ps2/2",   .active = false, .idx = 2, .result_sem = -1 },
	{ .name = "input/mouse/ps2/3",   .active = false, .idx = 3, .result_sem = -1 },
	{ .name = "input/keyboard/at/0", .active = false, .result_sem = -1, .flags = PS2_FLAG_KEYB }
};


status_t
ps2_dev_init(void)
{
	int i;
	for (i = 0; i < PS2_DEVICE_COUNT; i++) {
		ps2_dev *dev = &ps2_device[i];
		dev->result_sem = create_sem(0, "ps2 result");
		if (dev->result_sem < 0)
			goto err;
	}
	return B_OK;
err:
	ps2_dev_exit();
	return B_ERROR;
}


void
ps2_dev_exit(void)
{
	int i;
	for (i = 0; i < PS2_DEVICE_COUNT; i++) {
		ps2_dev *dev = &ps2_device[i];
		if (dev->result_sem >= 0) {
			delete_sem(dev->result_sem);
			dev->result_sem = -1;
		}
	}
}


void
ps2_dev_publish(ps2_dev *dev)
{
	status_t status;
	TRACE("ps2: ps2_dev_publish %s\n", dev->name);
	
	if (dev->active)
		return;

	dev->active = true;
	
	status = devfs_publish_device(dev->name, (atomic_get(&dev->flags)
			& PS2_FLAG_KEYB) ? &gKeyboardDeviceHooks : &gMouseDeviceHooks);

	INFO("ps2: devfs_publish_device %s, status = 0x%08lx\n", dev->name, status);
}


void
ps2_dev_unpublish(ps2_dev *dev)
{
	status_t status;
	TRACE("ps2: ps2_dev_unpublish %s\n", dev->name);

	if (!dev->active)
		return;
		
	dev->active = false;
	
	status = devfs_unpublish_device(dev->name, true);

	if ((dev->flags & PS2_FLAG_ENABLED) && dev->disconnect)
		dev->disconnect(dev);

	INFO("ps2: devfs_unpublish_device %s, status = 0x%08lx\n", dev->name, status);
}


int32
ps2_dev_handle_int(ps2_dev *dev)
{
	const uint8 data = dev->history[0].data;
	uint32 flags;
	
	flags = atomic_get(&dev->flags);
	
	if (flags & PS2_FLAG_CMD) {
		if ((flags & (PS2_FLAG_ACK | PS2_FLAG_NACK)) == 0) {
			int cnt = 1;
			if (data == PS2_REPLY_ACK) {
				atomic_or(&dev->flags, PS2_FLAG_ACK);
			} else if (data == PS2_REPLY_RESEND || data == PS2_REPLY_ERROR) {
				atomic_or(&dev->flags, PS2_FLAG_NACK);
			} else if ((flags & PS2_FLAG_GETID) && (data == 0 || data == 3 || data == 4)) {
				// workaround for broken mice that don't ack the "get id" command
				TRACE("ps2: ps2_dev_handle_int: mouse didn't ack the 'get id' command\n");
				atomic_or(&dev->flags, PS2_FLAG_ACK);
				if (dev->result_buf_cnt) {
					dev->result_buf[dev->result_buf_idx] = data;
					dev->result_buf_idx++;
					dev->result_buf_cnt--;
					if (dev->result_buf_cnt == 0) {
						atomic_and(&dev->flags, ~PS2_FLAG_CMD);
						cnt++;
					}
				}
			} else {
//				TRACE("ps2: ps2_dev_handle_int unexpected data 0x%02x while waiting for ack\n", data);
				TRACE("ps2: int1 %02x\n", data);
				goto pass_to_handler;
			}
			release_sem_etc(dev->result_sem, cnt, B_DO_NOT_RESCHEDULE);
			return B_INVOKE_SCHEDULER;
		} else if (dev->result_buf_cnt) {
			dev->result_buf[dev->result_buf_idx] = data;
			dev->result_buf_idx++;
			dev->result_buf_cnt--;
			if (dev->result_buf_cnt == 0) {
				atomic_and(&dev->flags, ~PS2_FLAG_CMD);
				release_sem_etc(dev->result_sem, 1, B_DO_NOT_RESCHEDULE);
				return B_INVOKE_SCHEDULER;
			}
		} else {
//			TRACE("ps2: ps2_dev_handle_int unexpected data 0x%02x during command processing\n", data);
			TRACE("ps2: int2 %02x\n", data);
			goto pass_to_handler;
		}
		return B_HANDLED_INTERRUPT;
	}
	
pass_to_handler:

	if ((flags & PS2_FLAG_KEYB) == 0) {
		if (dev->history[0].error && data == 0xfd) {
			INFO("ps2: hot removal of %s\n", dev->name);
			ps2_service_notify_device_removed(dev);
			return B_INVOKE_SCHEDULER;
		}
		if (data == 0x00 && dev->history[1].data == 0xaa && (dev->history[0].time - dev->history[1].time) < 50000) {
			INFO("ps2: hot plugin of %s\n", dev->name);
			if (dev->active) {
				INFO("ps2: device %s still active, republishing...\n", dev->name);
				ps2_service_notify_device_republish(dev);
			} else {
				ps2_service_notify_device_added(dev);
			}
			return B_INVOKE_SCHEDULER;
		}
	}

	if (!dev->active) {
		TRACE("ps2: %s not active, data 0x%02x dropped\n", dev->name, data);
		if (data != 0x00 && data != 0xaa) {
			INFO("ps2: possibly a hot plugin of %s\n", dev->name);
			ps2_service_notify_device_added(dev);
			return B_INVOKE_SCHEDULER;
		}
		return B_HANDLED_INTERRUPT;
	}
	
	if ((flags & PS2_FLAG_ENABLED) == 0) {
		TRACE("ps2: %s not enabled, data 0x%02x dropped\n", dev->name, data);
		return B_HANDLED_INTERRUPT;
	}

	return dev->handle_int(dev);
}


status_t
ps2_dev_command(ps2_dev *dev, uint8 cmd, const uint8 *out, int out_count, uint8 *in, int in_count)
{
	return ps2_dev_command_timeout(dev, cmd, out, out_count, in, in_count, 4000000);
}


status_t
ps2_dev_command_timeout(ps2_dev *dev, uint8 cmd, const uint8 *out, int out_count, uint8 *in, int in_count, bigtime_t timeout)
{
	status_t res;
	bigtime_t start;
	int32 sem_count;
	int i;

	TRACE("ps2: ps2_dev_command cmd 0x%02x, out-count %d, in-count %d, dev %s\n", cmd, out_count, in_count, dev->name);
	for (i = 0; i < out_count; i++)
		TRACE("ps2: ps2_dev_command tx: 0x%02x\n", out[i]);

	res = get_sem_count(dev->result_sem, &sem_count);
	if (res == B_OK && sem_count != 0) {
		TRACE("ps2: ps2_dev_command: sem_count %ld, fixing!\n", sem_count);
		if (sem_count > 0)
			acquire_sem_etc(dev->result_sem, sem_count, 0, 0);
		else
			release_sem_etc(dev->result_sem, -sem_count, 0);
	}

	dev->result_buf_cnt = in_count;
	dev->result_buf_idx = 0;
	dev->result_buf = in;
	
	res = B_OK;
	for (i = -1; res == B_OK && i < out_count; i++) {

		atomic_and(&dev->flags, ~(PS2_FLAG_ACK | PS2_FLAG_NACK | PS2_FLAG_GETID));

		acquire_sem(gControllerSem);

		if (!(atomic_get(&dev->flags) & PS2_FLAG_KEYB)) {
			uint8 prefix_cmd;
			if (gActiveMultiplexingEnabled)
				prefix_cmd = 0x90 + dev->idx;
			else
				prefix_cmd = 0xd4;
			res = ps2_wait_write();
			if (res == B_OK)
				ps2_write_ctrl(prefix_cmd);
		}

		res = ps2_wait_write();
		if (res == B_OK) {
			if (i == -1) {
				if (cmd == PS2_CMD_GET_DEVICE_ID)
					atomic_or(&dev->flags, PS2_FLAG_CMD | PS2_FLAG_GETID);
				else
					atomic_or(&dev->flags, PS2_FLAG_CMD);
				ps2_write_data(cmd);
			} else {
				ps2_write_data(out[i]);
			}
		}

		release_sem(gControllerSem);

		start = system_time();
		res = acquire_sem_etc(dev->result_sem, 1, B_RELATIVE_TIMEOUT, timeout);

		if (res != B_OK)
			atomic_and(&dev->flags, ~PS2_FLAG_CMD);

		TRACE("ps2: ps2_dev_command wait for ack res 0x%08lx, wait-time %Ld\n", res, system_time() - start);

		if (atomic_get(&dev->flags) & PS2_FLAG_ACK) {
			TRACE("ps2: ps2_dev_command got ACK\n");
		}

		if (atomic_get(&dev->flags) & PS2_FLAG_NACK) {
			atomic_and(&dev->flags, ~PS2_FLAG_CMD);
			res = B_IO_ERROR;
			TRACE("ps2: ps2_dev_command got NACK\n");
		}

		if (res != B_OK)
			break;
	}	

	if (res == B_OK) {
		if (in_count == 0) {
			atomic_and(&dev->flags, ~PS2_FLAG_CMD);
		} else {
			start = system_time();
			res = acquire_sem_etc(dev->result_sem, 1, B_RELATIVE_TIMEOUT, timeout);

			atomic_and(&dev->flags, ~PS2_FLAG_CMD);

			if (dev->result_buf_cnt != 0) {
				TRACE("ps2: ps2_dev_command error: %d rx bytes not received\n", dev->result_buf_cnt);
				in_count -= dev->result_buf_cnt;
				dev->result_buf_cnt = 0;
				res = B_IO_ERROR;
			}

			TRACE("ps2: ps2_dev_command wait for input res 0x%08lx, wait-time %Ld\n", res, system_time() - start);
		
			for (i = 0; i < in_count; i++)
				TRACE("ps2: ps2_dev_command rx: 0x%02x\n", in[i]);
		}
	}

	TRACE("ps2: ps2_dev_command result 0x%08lx\n", res);

	return res;
}


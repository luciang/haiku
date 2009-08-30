/*
 * Copyright 2004-2008, Haiku, Inc. All Rights Reserved.
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */

//!	Everything doing the real input/output stuff.


#include "scsi_periph_int.h"
#include <scsi.h>

#include <string.h>
#include <stdlib.h>


static status_t
inquiry(scsi_periph_device_info *device, scsi_inquiry *inquiry)
{
	const scsi_res_inquiry *device_inquiry = NULL;
	size_t inquiryLength;

	if (gDeviceManager->get_attr_raw(device->node, SCSI_DEVICE_INQUIRY_ITEM,
			(const void **)&device_inquiry, &inquiryLength, true) != B_OK)
		return B_ERROR;

	memcpy(inquiry, device_inquiry, min_c(inquiryLength, sizeof(scsi_inquiry)));
	return B_OK;
}


static status_t
prevent_allow(scsi_periph_device_info *device, bool prevent)
{
	scsi_cmd_prevent_allow cmd;

	SHOW_FLOW0(0, "");

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = SCSI_OP_PREVENT_ALLOW;
	cmd.prevent = prevent;

	return periph_simple_exec(device, (uint8 *)&cmd, sizeof(cmd), NULL, 0,
		SCSI_DIR_NONE);
}


/** !!!keep this in sync with scsi_raw driver!!! */

static status_t
raw_command(scsi_periph_device_info *device, raw_device_command *cmd)
{
	scsi_ccb *request;

	SHOW_FLOW0(0, "");

	request = device->scsi->alloc_ccb(device->scsi_device);
	if (request == NULL)
		return B_NO_MEMORY;

	request->flags = 0;

	if (cmd->flags & B_RAW_DEVICE_DATA_IN)
		request->flags |= SCSI_DIR_IN;
	else if (cmd->data_length)
		request->flags |= SCSI_DIR_OUT;
	else
		request->flags |= SCSI_DIR_NONE;

	request->data = (uint8*)cmd->data;
	request->sg_list = NULL;
	request->data_length = cmd->data_length;
	request->sort = -1;
	request->timeout = cmd->timeout;

	memcpy(request->cdb, cmd->command, SCSI_MAX_CDB_SIZE);
	request->cdb_length = cmd->command_length;

	device->scsi->sync_io(request);

	// TBD: should we call standard error handler here, or may the
	// actions done there (like starting the unit) confuse the application?

	cmd->cam_status = request->subsys_status;
	cmd->scsi_status = request->device_status;

	if ((request->subsys_status & SCSI_AUTOSNS_VALID) != 0 && cmd->sense_data) {
		memcpy(cmd->sense_data, request->sense, min_c(cmd->sense_data_length,
			(size_t)SCSI_MAX_SENSE_SIZE - request->sense_resid));
	}

	if ((cmd->flags & B_RAW_DEVICE_REPORT_RESIDUAL) != 0) {
		// this is a bit strange, see Be's sample code where I pinched this from;
		// normally, residual means "number of unused bytes left"
		// but here, we have to return "number of used bytes", which is the opposite
		cmd->data_length = cmd->data_length - request->data_resid;
		cmd->sense_data_length = SCSI_MAX_SENSE_SIZE - request->sense_resid;
	}

	device->scsi->free_ccb(request);

	return B_OK;
}


status_t
periph_ioctl(scsi_periph_handle_info *handle, int op, void *buffer,
	size_t length)
{
	switch (op) {
		case B_GET_MEDIA_STATUS: {
			status_t res = B_OK;

			if (handle->device->removable)
				res = periph_get_media_status(handle);

			SHOW_FLOW(2, "%s", strerror(res));

			*(status_t *)buffer = res;
			return B_OK;
		}

		case B_SCSI_INQUIRY:
			return inquiry(handle->device, (scsi_inquiry *)buffer);

		case B_SCSI_PREVENT_ALLOW:
			return prevent_allow(handle->device, *(bool *)buffer);

		case B_RAW_DEVICE_COMMAND:
			return raw_command(handle->device, (raw_device_command*)buffer);

		default:
			if (handle->device->scsi->ioctl != NULL) {
				return handle->device->scsi->ioctl(handle->device->scsi_device,
					op, buffer, length);
			}

			SHOW_ERROR(4, "Unknown ioctl: %x", op);
			return B_BAD_VALUE;
	}
}


/*!	kernel daemon
	once in a minute, it sets a flag so that the next command is executed
	ordered; this way, we avoid starvation of SCSI commands inside the
	SCSI queuing system - the ordered command waits for all previous
	commands and thus no command can starve longer then a minute
*/
void
periph_sync_queue_daemon(void *arg, int iteration)
{
	scsi_periph_device_info *device = (scsi_periph_device_info *)arg;

	SHOW_FLOW0(3, "Setting ordered flag for next R/W access");
	atomic_or(&device->next_tag_action, SCSI_ORDERED_QTAG);
}


/*! Universal read/write function */
status_t
periph_io(scsi_periph_device_info *device, io_operation *operation,
	size_t* _bytesTransferred)
{
	uint32 blockSize = device->block_size;
	size_t numBlocks = operation->Length() / blockSize;
	uint32 pos = operation->Offset() / blockSize;
	scsi_ccb *request;
	err_res res;
	int retries = 0;
	int err;

	// don't test rw10_enabled restrictions - this flag may get changed
	request = device->scsi->alloc_ccb(device->scsi_device);

	if (request == NULL)
		return B_NO_MEMORY;

	do {
		size_t numBytes;
		bool is_rw10;

		request->flags = operation->IsWrite() ? SCSI_DIR_OUT : SCSI_DIR_IN;

		// make sure we avoid 10 byte commands if they aren't supported
		if( !device->rw10_enabled || device->preferred_ccb_size == 6) {
			// restricting transfer is OK - the block manager will
			// take care of transferring the rest
			if (numBlocks > 0x100)
				numBlocks = 0x100;

			// no way to break the 21 bit address limit
			if (pos > 0x200000) {
				err = B_BAD_VALUE;
				goto abort;
			}

			// don't allow transfer cross the 24 bit address limit
			// (I'm not sure whether this is allowed, but this way we
			// are sure to not ask for trouble)
			if (pos < 0x100000)
				numBlocks = min_c(numBlocks, 0x100000 - pos);
		}

		numBytes = numBlocks * blockSize;
		if (numBytes != operation->Length())
			panic("I/O operation would need to be cut.");

		request->data = NULL;
		request->sg_list = (physical_entry*)operation->Vecs();
		request->data_length = numBytes;
		request->sg_count = operation->VecCount();
		request->io_operation = operation;
		request->sort = pos;
		request->timeout = device->std_timeout;
		// see whether daemon instructed us to post an ordered command;
		// reset flag after read
		SHOW_FLOW( 3, "flag=%x, next_tag=%x, ordered: %s",
			(int)request->flags, (int)device->next_tag_action,
			(request->flags & SCSI_ORDERED_QTAG) != 0 ? "yes" : "no" );

		// use shortest commands whenever possible
		if (pos + numBlocks < 0x200000 && numBlocks <= 0x100) {
			scsi_cmd_rw_6 *cmd = (scsi_cmd_rw_6 *)request->cdb;

			is_rw10 = false;

			memset(cmd, 0, sizeof(*cmd));
			cmd->opcode = operation->IsWrite()
				? SCSI_OP_WRITE_6 : SCSI_OP_READ_6;
			cmd->high_lba = (pos >> 16) & 0x1f;
			cmd->mid_lba = (pos >> 8) & 0xff;
			cmd->low_lba = pos & 0xff;
			cmd->length = numBlocks;

			request->cdb_length = sizeof(*cmd);
		} else {
			scsi_cmd_rw_10 *cmd = (scsi_cmd_rw_10 *)request->cdb;

			is_rw10 = true;

			memset(cmd, 0, sizeof(*cmd));
			cmd->opcode = operation->IsWrite()
				? SCSI_OP_WRITE_10 : SCSI_OP_READ_10;
			cmd->relative_address = 0;
			cmd->force_unit_access = 0;
			cmd->disable_page_out = 0;
			cmd->lba = B_HOST_TO_BENDIAN_INT32(pos);
			cmd->length = B_HOST_TO_BENDIAN_INT16(numBlocks);

			request->cdb_length = sizeof(*cmd);
		}

		// last chance to detect errors that occured during concurrent accesses
		err = 0; // TODO: handle->pending_error;

		if (err)
			goto abort;

		device->scsi->async_io(request);

		acquire_sem(request->completion_sem);

		// ask generic peripheral layer what to do now
		res = periph_check_error(device, request);

		switch (res.action) {
			case err_act_ok:
				*_bytesTransferred = numBytes - request->data_resid;
				break;

			case err_act_start:
				res = periph_send_start_stop(device, request, 1,
					device->removable);
				if (res.action == err_act_ok)
					res.action = err_act_retry;
				break;

			case err_act_invalid_req:
				// if this was a 10 byte command, the device probably doesn't
				// support them, so disable them and retry
				if (is_rw10) {
					atomic_and(&device->rw10_enabled, 0);
					res.action = err_act_retry;
				} else
					res.action = err_act_fail;
				break;
		}
	} while((res.action == err_act_retry && retries++ < 3)
		|| (res.action == err_act_many_retries && retries++ < 30));

	device->scsi->free_ccb(request);

	// peripheral layer only created "read" error, so we have to
	// map them to "write" errors if this was a write request
	if (res.error_code == B_DEV_READ_ERROR && operation->IsWrite())
		return B_DEV_WRITE_ERROR;

	return res.error_code;

abort:
	device->scsi->free_ccb(request);
	return err;
}


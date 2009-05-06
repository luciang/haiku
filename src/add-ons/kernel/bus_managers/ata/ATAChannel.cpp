/*
 * Copyright 2009, Michael Lotz, mmlr@mlotz.ch.
 * Copyright 2008, Marcus Overhagen.
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2002-2003, Thomas Kurschel.
 *
 * Distributed under the terms of the MIT License.
 */

#include "ATAPrivate.h"


ATAChannel::ATAChannel(device_node *node)
	:
	fNode(node),
	fChannelID(0),
	fController(NULL),
	fCookie(NULL),
	fExpectsInterrupt(false),
	fStatus(B_NO_INIT),
	fSCSIBus(NULL),
	fDeviceCount(0),
	fDevices(NULL),
	fUseDMA(true),
	fRequest(NULL)
{
	B_INITIALIZE_SPINLOCK(&fInterruptLock);
	fInterruptCondition.Init(this, "ata dma transfer");

	gDeviceManager->get_attr_uint32(node, ATA_CHANNEL_ID_ITEM, &fChannelID,
		true);
	snprintf(fDebugContext, sizeof(fDebugContext), " %lu", fChannelID);

	if (fUseDMA) {
		void *settings = load_driver_settings(B_SAFEMODE_DRIVER_SETTINGS);
		if (settings != NULL) {
			if (get_driver_boolean_parameter(settings,
				B_SAFEMODE_DISABLE_IDE_DMA, false, false)) {
				TRACE_ALWAYS("disabling DMA because of safemode setting\n");
				fUseDMA = false;
			}

			unload_driver_settings(settings);
		}
	}

	if (fUseDMA) {
		uint8 canDMA;
		if (gDeviceManager->get_attr_uint8(node, ATA_CONTROLLER_CAN_DMA_ITEM,
			&canDMA, true) != B_OK) {
			TRACE_ERROR("unknown if controller supports DMA, not using it\n");
			fUseDMA = false;
		}

		if (canDMA == 0) {
			TRACE_ALWAYS("controller doesn't support DMA, disabling\n");
			fUseDMA = false;
		}
	}

	fRequest = new(std::nothrow) ATARequest(true);
	if (fRequest == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	uint8 maxDevices = 2;
	if (gDeviceManager->get_attr_uint8(node, ATA_CONTROLLER_MAX_DEVICES_ITEM,
			&maxDevices, true) != B_OK) {
		maxDevices = 2;
	}

	fDeviceCount = MIN(maxDevices, 2);
	fDevices = new(std::nothrow) ATADevice *[fDeviceCount];
	if (fDevices == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	for (uint8 i = 0; i < fDeviceCount; i++)
		fDevices[i] = NULL;

	device_node *parent = gDeviceManager->get_parent_node(node);
	fStatus = gDeviceManager->get_driver(parent,
		(driver_module_info **)&fController, &fCookie);
	gDeviceManager->put_node(parent);

	fController->set_channel(fCookie, this);
}


ATAChannel::~ATAChannel()
{
	if (fDevices) {
		for (uint8 i = 0; i < fDeviceCount; i++)
			delete fDevices[i];
		delete [] fDevices;
	}

	delete fRequest;
}


status_t
ATAChannel::InitCheck()
{
	return fStatus;
}


void
ATAChannel::SetBus(scsi_bus bus)
{
	fSCSIBus = bus;
}


status_t
ATAChannel::ScanBus()
{
	// check if there is anything at all
	if (AltStatus() == 0xff) {
		TRACE_ALWAYS("illegal status value, assuming no devices connected\n");
		return B_OK;
	}

	bool devicePresent[fDeviceCount];
	uint16 deviceSignature[fDeviceCount];
	status_t result = Reset(devicePresent, deviceSignature);
	if (result != B_OK) {
		TRACE_ERROR("resetting the channel failed\n");
		return result;
	}

	for (uint8 i = 0; i < fDeviceCount; i++) {
		if (!devicePresent[i])
			continue;

		ATADevice *device = NULL;
		if (deviceSignature[i] == ATA_SIGNATURE_ATAPI)
			device = new(std::nothrow) ATAPIDevice(this, i);
		else
			device = new(std::nothrow) ATADevice(this, i);

		if (device == NULL) {
			TRACE_ERROR("out of memory allocating device\n");
			return B_NO_MEMORY;
		}

		TRACE("trying ATA%s device %u\n", device->IsATAPI() ? "PI" : "", i);

		if (device->Identify() != B_OK) {
			delete device;
			continue;
		}

		if (device->Configure() != B_OK) {
			TRACE_ERROR("failed to configure device\n");
			delete device;
			continue;
		}

		TRACE_ALWAYS("identified ATA%s device %u\n", device->IsATAPI()
			? "PI" : "", i);

		fDevices[i] = device;
	}

	return B_OK;
}


void
ATAChannel::PathInquiry(scsi_path_inquiry *info)
{
	info->hba_inquiry = SCSI_PI_TAG_ABLE | SCSI_PI_WIDE_16;
	info->hba_misc = 0;
	info->sim_priv = 0;
	info->initiator_id = 2;
	info->hba_queue_size = 1;
	memset(info->vuhba_flags, 0, sizeof(info->vuhba_flags));

	strlcpy(info->sim_vid, "Haiku", SCSI_SIM_ID);

	const char *controllerName = NULL;
	if (gDeviceManager->get_attr_string(fNode,
			SCSI_DESCRIPTION_CONTROLLER_NAME, &controllerName, true) == B_OK)
		strlcpy(info->hba_vid, controllerName, SCSI_HBA_ID);
	else
		strlcpy(info->hba_vid, "unknown", SCSI_HBA_ID);

	strlcpy(info->sim_version, "1.0", SCSI_VERS);
	strlcpy(info->hba_version, "1.0", SCSI_VERS);
	strlcpy(info->controller_family, "ATA", SCSI_FAM_ID);
	strlcpy(info->controller_type, "ATA", SCSI_TYPE_ID);
}


void
ATAChannel::GetRestrictions(uint8 targetID, bool *isATAPI, bool *noAutoSense,
	uint32 *maxBlocks)
{
	// we always indicate ATAPI so we have to emulate fewer commands
	*isATAPI = true;
	*noAutoSense = false;
	*maxBlocks = 0x100;

	if (targetID < fDeviceCount && fDevices[targetID] != NULL)
		fDevices[targetID]->GetRestrictions(noAutoSense, maxBlocks);
}


status_t
ATAChannel::ExecuteIO(scsi_ccb *ccb)
{
	TRACE_FUNCTION("%p\n", ccb);
	status_t result = fRequest->Start(ccb);
	if (result != B_OK)
		return result;

	if (ccb->cdb[0] == SCSI_OP_REQUEST_SENSE && fRequest->HasSense()) {
		TRACE("request sense\n");
		fRequest->RequestSense();
		fRequest->Finish(false);
		return B_OK;
	}

	// we aren't a check sense request, clear sense data for new request
	fRequest->ClearSense();

	if (ccb->target_id >= fDeviceCount) {
		TRACE_ERROR("invalid target device\n");
		fRequest->SetStatus(SCSI_SEL_TIMEOUT);
		fRequest->Finish(false);
		return B_BAD_INDEX;
	}

	ATADevice *device = fDevices[ccb->target_id];
	if (device == NULL) {
		TRACE_ERROR("target device not present\n");
		fRequest->SetStatus(SCSI_SEL_TIMEOUT);
		fRequest->Finish(false);
		return B_BAD_INDEX;
	}

	fRequest->SetTimeout(ccb->timeout > 0 ? ccb->timeout * 1000 * 1000
		: ATA_STANDARD_TIMEOUT);

	result = device->ExecuteIO(fRequest);
	fRequest->Finish(false);
	return result;
}


status_t
ATAChannel::SelectDevice(uint8 device)
{
	TRACE_FUNCTION("device: %u\n", device);

	if (device > 1)
		return B_BAD_INDEX;

	ata_task_file taskFile;
	taskFile.lba.mode = ATA_MODE_LBA;
	taskFile.lba.device = device;

	status_t result = _WriteRegs(&taskFile, ATA_MASK_DEVICE_HEAD);
	if (result != B_OK) {
		TRACE_ERROR("writing register failed when trying to select device %d\n",
			device);
		return result;
	}

	_FlushAndWait(1);

#if 0
	// for debugging only
	_ReadRegs(&taskFile, ATA_MASK_DEVICE_HEAD);
	if (taskFile.chs.device != device) {
		TRACE_ERROR("device %d not selected! head 0x%x, mode 0x%x, device %d\n",
			device, taskFile.chs.head, taskFile.chs.mode, taskFile.chs.device);
		return B_ERROR;
	}
#endif

	return B_OK;
}


uint8
ATAChannel::SelectedDevice()
{
	ata_task_file taskFile;
	if (_ReadRegs(&taskFile, ATA_MASK_DEVICE_HEAD) != B_OK) {
		TRACE_ERROR("reading register failed when detecting selected device\n");
		return 2;
	}

	return taskFile.lba.device;
}


status_t
ATAChannel::Reset(bool *presence, uint16 *signatures)
{
	TRACE_FUNCTION("\n");

	SelectDevice(0);

	// disable interrupts and assert SRST for at least 5 usec
	if (_WriteControl(ATA_DEVICE_CONTROL_DISABLE_INTS
		| ATA_DEVICE_CONTROL_SOFT_RESET) != B_OK) {
		TRACE_ERROR("failed to set reset signaling\n");
		return B_ERROR;
	}

	_FlushAndWait(20);

	// clear reset and wait for at least 2 ms (wait 150ms like everyone else)
	if (_WriteControl(ATA_DEVICE_CONTROL_DISABLE_INTS) != B_OK) {
		TRACE_ERROR("failed to clear reset signaling\n");
		return B_ERROR;
	}

	_FlushAndWait(150 * 1000);

	if (presence != NULL) {
		for (uint8 i = 0; i < fDeviceCount; i++)
			presence[i] = false;
	}

	uint8 deviceCount = fDeviceCount;
	for (uint8 i = 0; i < deviceCount; i++) {
		if (SelectDevice(i) != B_OK || SelectedDevice() != i) {
			TRACE_ALWAYS("cannot select device %d, assuming not present\n", i);
			continue;
		}

		// ensure interrupts are disabled for this device
		_WriteControl(ATA_DEVICE_CONTROL_DISABLE_INTS);

		if (AltStatus() == 0xff) {
			TRACE_ALWAYS("illegal status value for device %d,"
				" assuming not present\n", i);
			continue;
		}

		// wait up to 31 seconds for busy to clear
		if (Wait(0, ATA_STATUS_BUSY, 0, 31 * 1000 * 1000) != B_OK) {
			TRACE_ERROR("device %d reset timeout\n", i);
			continue;
		}

		ata_task_file taskFile;
		if (_ReadRegs(&taskFile, ATA_MASK_LBA_MID | ATA_MASK_LBA_HIGH
				| ATA_MASK_ERROR) != B_OK) {
			TRACE_ERROR("reading status failed\n");
			return B_ERROR;
		}

		if (taskFile.read.error != 0x01
			&& (i > 0 || taskFile.read.error != 0x81)) {
			TRACE_ERROR("device %d failed, error code is 0x%02x\n", i,
				taskFile.read.error);
			// Workaround for Gigabyte i-RAM, which always reports 0x00 
			// TODO: find something nicer
			if (i == 1 && taskFile.read.error == 0x00) {
				TRACE_ERROR("continuing anyway...\n");
			} else
				continue;
		}

		if (i == 0 && taskFile.read.error >= 0x80) {
			TRACE_ERROR("device %d indicates that other device failed"
				" with code 0x%02x\n", i, taskFile.read.error);
			deviceCount = 1;
		}

		if (presence != NULL)
			presence[i] = true;

		uint16 signature = taskFile.lba.lba_8_15
			| (((uint16)taskFile.lba.lba_16_23) << 8);
		TRACE_ALWAYS("signature of device %d: 0x%04x\n", i, signature);

		if (signatures != NULL)
			signatures[i] = signature;
	}

	return B_OK;
}


status_t
ATAChannel::Wait(uint8 setBits, uint8 clearedBits, uint32 flags,
	bigtime_t timeout)
{
	bigtime_t startTime = system_time();
	_FlushAndWait(1);

	TRACE("waiting for set bits 0x%02x and cleared bits 0x%02x\n",
		setBits, clearedBits);

#if ATA_TRACING
	unsigned lastStatus = 0x100;
#endif
	while (true) {
		uint8 status = AltStatus();
		if ((flags & ATA_CHECK_ERROR_BIT) != 0
			&& (status & ATA_STATUS_BUSY) == 0
			&& (status & ATA_STATUS_ERROR) != 0) {
			TRACE("error bit set while waiting\n");
			return B_ERROR;
		}

		if ((flags & ATA_CHECK_DEVICE_FAULT) != 0
			&& (status & ATA_STATUS_BUSY) == 0
			&& (status & ATA_STATUS_DEVICE_FAULT) != 0) {
			TRACE("device fault bit set while waiting\n");
			return B_ERROR;
		}

		if ((status & clearedBits) == 0) {
			if ((flags & ATA_WAIT_ANY_BIT) != 0 && (status & setBits) != 0)
				return B_OK;
			if ((status & setBits) == setBits)
				return B_OK;
		}

		bigtime_t elapsedTime = system_time() - startTime;
#if ATA_TRACING
		if (lastStatus != status) {
			TRACE("wait status after %lld: 0x%02x\n", elapsedTime, status);
			lastStatus = status;
		}
#endif

		if (elapsedTime > timeout) {
			TRACE("timeout, wait status after %lld: 0x%02x\n",
				elapsedTime, status);
			return B_TIMED_OUT;
		}

		// The device may be ready almost immediatelly. If it isn't,
		// poll often during the first 20ms, otherwise poll lazyly.
		if (elapsedTime < 1000)
			spin(1);
		else if (elapsedTime < 20000)
			snooze(1000);
		else
			snooze(50000);
	}

	return B_ERROR;
}


status_t
ATAChannel::WaitDataRequest(bool high)
{
	return Wait(high ? ATA_STATUS_DATA_REQUEST : 0,
		high ? 0 : ATA_STATUS_DATA_REQUEST, 0, (high ? 10 : 1) * 1000 * 1000);
}


status_t
ATAChannel::WaitDeviceReady()
{
	return Wait(ATA_STATUS_DEVICE_READY, 0, 0, 5 * 1000 * 1000);
}


status_t
ATAChannel::WaitForIdle()
{
	return Wait(0, ATA_STATUS_BUSY | ATA_STATUS_DATA_REQUEST, 0, 50 * 1000);
}


void
ATAChannel::PrepareWaitingForInterrupt()
{
	TRACE_FUNCTION("\n");
	InterruptsSpinLocker locker(fInterruptLock);
	fExpectsInterrupt = true;
	fInterruptCondition.Add(&fInterruptConditionEntry);

	// enable interrupts
	_WriteControl(0);
}


status_t
ATAChannel::WaitForInterrupt(bigtime_t timeout)
{
	TRACE_FUNCTION("timeout: %lld\n", timeout);
	status_t result = fInterruptConditionEntry.Wait(B_RELATIVE_TIMEOUT,
		timeout);

	InterruptsSpinLocker locker(fInterruptLock);
	fExpectsInterrupt = false;
	locker.Unlock();

	// disable interrupts
	_WriteControl(ATA_DEVICE_CONTROL_DISABLE_INTS);

	if (result != B_OK) {
		TRACE_ERROR("timeout waiting for interrupt\n");
		return B_TIMED_OUT;
	}

	return B_OK;
}


status_t
ATAChannel::SendRequest(ATARequest *request, uint32 flags)
{
	ATADevice *device = request->Device();
	if (device->Select() != B_OK || WaitForIdle() != B_OK) {
		// resetting the device here will discard current configuration,
		// it's better when the SCSI bus manager requests an external reset.
		TRACE_ERROR("device selection timeout\n");
		request->SetStatus(SCSI_SEL_TIMEOUT);
		return B_TIMED_OUT;
	}

	if ((flags & ATA_DEVICE_READY_REQUIRED) != 0
		&& (AltStatus() & ATA_STATUS_DEVICE_READY) == 0) {
		TRACE_ERROR("device ready not set\n");
		request->SetStatus(SCSI_SEQUENCE_FAIL);
		return B_ERROR;
	}

	if (_WriteRegs(device->TaskFile(), device->RegisterMask()
			| ATA_MASK_COMMAND) != B_OK) {
		TRACE_ERROR("can't write command\n");
		request->SetStatus(SCSI_HBA_ERR);
		return B_ERROR;
	}

	return B_OK;
}


status_t
ATAChannel::FinishRequest(ATARequest *request, uint32 flags, uint8 errorMask)
{
	if (flags & ATA_WAIT_FINISH) {
		// wait for the device to finish current command (device no longer busy)
		status_t result = Wait(0, ATA_STATUS_BUSY, flags, request->Timeout());
		if (result != B_OK) {
			TRACE_ERROR("timeout waiting for request finish\n");
			request->SetStatus(SCSI_CMD_TIMEOUT);
			return result;
		}
	}

	ata_task_file *taskFile = request->Device()->TaskFile();

	// read status, this also acknowledges pending interrupts
	status_t result = _ReadRegs(taskFile, ATA_MASK_STATUS | ATA_MASK_ERROR);
	if (result != B_OK) {
		TRACE("reading status failed\n");
		request->SetStatus(SCSI_SEQUENCE_FAIL);
		return result;
	}

	if (taskFile->read.status & ATA_STATUS_BUSY) {
		TRACE("command failed, device still busy\n");
		request->SetStatus(SCSI_SEQUENCE_FAIL);
		return B_ERROR;
	}

	if ((flags & ATA_DEVICE_READY_REQUIRED)
		&& (taskFile->read.status & ATA_STATUS_DEVICE_READY) == 0) {
		TRACE("command failed, device ready required but not set\n");
		request->SetStatus(SCSI_SEQUENCE_FAIL);
		return B_ERROR;
	}

	uint8 checkFlags = ATA_STATUS_ERROR;
	if (flags & ATA_CHECK_DEVICE_FAULT)
		checkFlags |= ATA_STATUS_DEVICE_FAULT;

	if ((taskFile->read.status & checkFlags) == 0)
		return B_OK;

	if ((taskFile->read.error & ATA_ERROR_MEDIUM_CHANGED)
			!= ATA_ERROR_MEDIUM_CHANGED) {
		TRACE_ERROR("command failed, error bit is set: 0x%02x\n",
			taskFile->read.error);
	}

	uint8 error = taskFile->read.error & errorMask;
	if (error & ATA_ERROR_INTERFACE_CRC) {
		TRACE_ERROR("interface crc error\n");
		request->SetSense(SCSIS_KEY_HARDWARE_ERROR, SCSIS_ASC_LUN_COM_CRC);
		return B_ERROR;
	}

	if (request->IsWrite()) {
		if (error & ATA_ERROR_WRITE_PROTECTED) {
			request->SetSense(SCSIS_KEY_DATA_PROTECT, SCSIS_ASC_WRITE_PROTECTED);
			return B_ERROR;
		}
	} else {
		if (error & ATA_ERROR_UNCORRECTABLE) {
			request->SetSense(SCSIS_KEY_MEDIUM_ERROR, SCSIS_ASC_UNREC_READ_ERR);
			return B_ERROR;
		}
	}

	if (error & ATA_ERROR_MEDIUM_CHANGED) {
		request->SetSense(SCSIS_KEY_UNIT_ATTENTION, SCSIS_ASC_MEDIUM_CHANGED);
		return B_ERROR;
	}

	if (error & ATA_ERROR_INVALID_ADDRESS) {
		// XXX strange error code, don't really know what it means
		request->SetSense(SCSIS_KEY_MEDIUM_ERROR, SCSIS_ASC_RANDOM_POS_ERROR);
		return B_ERROR;
	}

	if (error & ATA_ERROR_MEDIA_CHANGE_REQUESTED) {
		request->SetSense(SCSIS_KEY_UNIT_ATTENTION, SCSIS_ASC_REMOVAL_REQUESTED);
		return B_ERROR;
	}

	if (error & ATA_ERROR_NO_MEDIA) {
		request->SetSense(SCSIS_KEY_MEDIUM_ERROR, SCSIS_ASC_NO_MEDIUM);
		return B_ERROR;
	}

	if (error & ATA_ERROR_ABORTED) {
		request->SetSense(SCSIS_KEY_ABORTED_COMMAND, SCSIS_ASC_NO_SENSE);
		return B_ERROR;
	}

	// either there was no error bit set or it was masked out
	request->SetSense(SCSIS_KEY_HARDWARE_ERROR, SCSIS_ASC_INTERNAL_FAILURE);
	return B_ERROR;
}


status_t
ATAChannel::PrepareDMA(ATARequest *request)
{
	scsi_ccb *ccb = request->CCB();
	return fController->prepare_dma(fCookie, ccb->sg_list, ccb->sg_count,
		request->IsWrite());
}


status_t
ATAChannel::StartDMA()
{
	return fController->start_dma(fCookie);
}


status_t
ATAChannel::FinishDMA()
{
	return fController->finish_dma(fCookie);
}


status_t
ATAChannel::ExecutePIOTransfer(ATARequest *request)
{
	bigtime_t timeout = request->Timeout();
	status_t result = B_OK;
	size_t *bytesLeft = request->BytesLeft();
	while (*bytesLeft > 0) {
		size_t currentLength = MIN(*bytesLeft, ATA_BLOCK_SIZE);
		if (request->IsWrite()) {
			result = _WritePIOBlock(request, currentLength);
			if (result != B_OK) {
				TRACE_ERROR("failed to write pio block\n");
				break;
			}
		} else {
			result = _ReadPIOBlock(request, currentLength);
			if (result != B_OK) {
				TRACE_ERROR("failed to read pio block\n");
				break;
			}
		}

		*bytesLeft -= currentLength;

		if (*bytesLeft > 0) {
			// wait for next block to be ready
			if (Wait(ATA_STATUS_DATA_REQUEST, ATA_STATUS_BUSY,
				ATA_CHECK_ERROR_BIT | ATA_CHECK_DEVICE_FAULT,
				timeout) != B_OK) {
				TRACE_ERROR("timeout waiting for device to request data\n");
				result = B_TIMED_OUT;
				break;
			}
		}
	}

	if (result == B_OK && WaitDataRequest(false) != B_OK) {
		TRACE_ERROR("device still expects data transfer\n");
		result = B_ERROR;
	}

	return result;
}


status_t
ATAChannel::ReadRegs(ATADevice *device)
{
	return _ReadRegs(device->TaskFile(), device->RegisterMask());
}


uint8
ATAChannel::AltStatus()
{
	return fController->get_altstatus(fCookie);
}


status_t
ATAChannel::ReadPIO(uint8 *buffer, size_t length)
{
	return fController->read_pio(fCookie, (uint16 *)buffer,
		length / sizeof(uint16), false);
}


status_t
ATAChannel::WritePIO(uint8 *buffer, size_t length)
{
	return fController->write_pio(fCookie, (uint16 *)buffer,
		length / sizeof(uint16), true);
}


status_t
ATAChannel::Interrupt(uint8 status)
{
	SpinLocker locker(fInterruptLock);
	if (!fExpectsInterrupt) {
		TRACE("interrupt when not expecting transfer\n");
		return B_UNHANDLED_INTERRUPT;
	}

	if ((status & ATA_STATUS_BUSY) != 0) {
		TRACE(("interrupt while device is busy\n"));
		return B_UNHANDLED_INTERRUPT;
	}

	fInterruptCondition.NotifyAll();
	return B_INVOKE_SCHEDULER;
}


status_t
ATAChannel::_ReadRegs(ata_task_file *taskFile, ata_reg_mask mask)
{
	return fController->read_command_block_regs(fCookie, taskFile, mask);
}


status_t
ATAChannel::_WriteRegs(ata_task_file *taskFile, ata_reg_mask mask)
{
	return fController->write_command_block_regs(fCookie, taskFile, mask);
}


status_t
ATAChannel::_WriteControl(uint8 value)
{
	return fController->write_device_control(fCookie, ATA_DEVICE_CONTROL_BIT3
		| value);
}


void
ATAChannel::_FlushAndWait(bigtime_t waitTime)
{
	AltStatus();
	if (waitTime > 100)
		snooze(waitTime);
	else
		spin(waitTime);
}


status_t
ATAChannel::_ReadPIOBlock(ATARequest *request, size_t length)
{
	uint32 transferred = 0;
	status_t result = _TransferPIOBlock(request, length, &transferred);
	request->CCB()->data_resid -= transferred;

	// if length was odd, there's an extra byte waiting in request->OddByte()
	if (request->GetOddByte(NULL)) {
		// discard byte and adjust res_id as the extra byte didn't reach the
		// buffer
		request->CCB()->data_resid++;
	}

	if (result != B_BUFFER_OVERFLOW)
		return result;

	// the device returns more data then the buffer can store;
	// for ATAPI this is OK - we just discard remaining bytes (there
	// is no way to tell ATAPI about that, but we "only" waste time)

	// perhaps discarding the extra odd-byte was sufficient
	if (transferred >= length)
		return B_OK;

	TRACE_ERROR("pio read: discarding after %lu bytes\n", transferred);

	uint8 buffer[32];
	length -= transferred;
	// discard 32 bytes at once	(see _WritePIOBlock())
	while (length > 0) {
		// read extra byte if length is odd (that's the "length + 1")
		size_t currentLength = MIN(length + 1, (uint32)sizeof(buffer))
			/ sizeof(uint16);
		fController->read_pio(fCookie, (uint16 *)buffer, currentLength, false);
		length -= currentLength * 2;
	}

	return B_OK;
}


status_t
ATAChannel::_WritePIOBlock(ATARequest *request, size_t length)
{
	size_t transferred = 0;
	status_t result = _TransferPIOBlock(request, length, &transferred);
	request->CCB()->data_resid -= transferred;

	if (result != B_BUFFER_OVERFLOW)
		return result;

	// there may be a pending odd byte - transmit that now
	uint8 byte;
	if (request->GetOddByte(&byte)) {
		uint8 buffer[2];
		buffer[0] = byte;
		buffer[1] = 0;

		fController->write_pio(fCookie, (uint16 *)buffer, 1, false);
		request->CCB()->data_resid--;
		transferred += 2;
	}

	// "transferred" may actually be larger then length because the last odd-byte
	// is sent together with an extra zero-byte
	if (transferred >= length)
		return B_OK;

	// Ouch! the device asks for data but we haven't got any left.
	// Sadly, this behaviour is OK for ATAPI packets, but there is no
	// way to tell the device that we don't have any data left;
	// only solution is to send zero bytes, though it's BAD
	static const uint8 buffer[32] = {};

	TRACE_ERROR("pio write: discarding after %lu bytes\n", transferred);

	length -= transferred;
	while (length > 0) {
		// if device asks for odd number of bytes, append an extra byte to
		// make length even (this is the "length + 1" term)
		size_t currentLength = MIN(length + 1, (int)(sizeof(buffer)))
			/ sizeof(uint16);
		fController->write_pio(fCookie, (uint16 *)buffer, currentLength, false);
		length -= currentLength * 2;
	}

	return B_BUFFER_OVERFLOW;
}


status_t
ATAChannel::_TransferPIOBlock(ATARequest *request, size_t length,
	size_t *transferred)
{
	// data is usually split up into multiple scatter/gather blocks
	while (length > 0) {
		if (request->SGElementsLeft() == 0) {
			// ups - buffer too small (for ATAPI data, this is OK)
			return B_BUFFER_OVERFLOW;
		}

		// we might have transmitted part of a scatter/entry already
		const physical_entry *entry = request->CurrentSGElement();
		uint32 offset = request->CurrentSGOffset();
		uint32 currentLength = MIN(entry->size - offset, length);

		status_t result = _TransferPIOPhysical(request,
			(addr_t)entry->address + offset, currentLength, transferred);
		if (result != B_OK) {
			request->SetSense(SCSIS_KEY_HARDWARE_ERROR,
				SCSIS_ASC_INTERNAL_FAILURE);
			return result;
		}

		request->AdvanceSG(currentLength);
		length -= currentLength;
	}

	return B_OK;
}


// TODO: this should not be necessary, we could directly use virtual addresses
#include <vm.h>
#include <thread.h>

status_t
ATAChannel::_TransferPIOPhysical(ATARequest *request, addr_t physicalAddress,
	size_t length, size_t *transferred)
{
	// we must split up chunk into B_PAGE_SIZE blocks as we can map only
	// one page into address space at once
	while (length > 0) {
		struct thread *thread = thread_get_current_thread();
		thread_pin_to_current_cpu(thread);

		void *handle;
		addr_t virtualAddress;
		if (vm_get_physical_page_current_cpu(physicalAddress, &virtualAddress,
				&handle) != B_OK) {
			thread_unpin_from_current_cpu(thread);
			// ouch: this should never ever happen
			return B_ERROR;
		}

		ASSERT(physicalAddress % B_PAGE_SIZE == virtualAddress % B_PAGE_SIZE);

		// if chunk starts in the middle of a page, we have even less then
		// a page left
		size_t pageLeft = B_PAGE_SIZE - physicalAddress % B_PAGE_SIZE;
		size_t currentLength = MIN(pageLeft, length);

		status_t result = _TransferPIOVirtual(request, (uint8 *)virtualAddress,
			currentLength, transferred);

		vm_put_physical_page_current_cpu(virtualAddress, handle);
		thread_unpin_from_current_cpu(thread);

		if (result != B_OK)
			return result;

		length -= currentLength;
		physicalAddress += currentLength;
	}

	return B_OK;
}


status_t
ATAChannel::_TransferPIOVirtual(ATARequest *request, uint8 *virtualAddress,
	size_t length, size_t *transferred)
{
	if (request->IsWrite()) {
		// if there is a byte left from last chunk, transmit it together
		// with the first byte of the current chunk (IDE requires 16 bits
		// to be transmitted at once)
		uint8 byte;
		if (request->GetOddByte(&byte)) {
			uint8 buffer[2];

			buffer[0] = byte;
			buffer[1] = *virtualAddress++;

			fController->write_pio(fCookie, (uint16 *)buffer, 1, false);

			length--;
			*transferred += 2;
		}

		fController->write_pio(fCookie, (uint16 *)virtualAddress, length / 2,
			false);

		// take care if chunk size was odd, which means that 1 byte remains
		virtualAddress += length & ~1;
		*transferred += length & ~1;

		if ((length & 1) != 0)
			request->SetOddByte(*virtualAddress);
	} else {
		// if we read one byte too much last time, push it into current chunk
		uint8 byte;
		if (request->GetOddByte(&byte)) {
			*virtualAddress++ = byte;
			length--;
		}

		fController->read_pio(fCookie, (uint16 *)virtualAddress, length / 2,
			false);

		// take care of odd chunk size;
		// in this case we read 1 byte to few!
		virtualAddress += length & ~1;
		*transferred += length & ~1;

		if ((length & 1) != 0) {
			uint8 buffer[2];

			// now read the missing byte; as we have to read 2 bytes at once,
			// we'll read one byte too much
			fController->read_pio(fCookie, (uint16 *)buffer, 1, false);

			*virtualAddress = buffer[0];
			request->SetOddByte(buffer[1]);

			*transferred += 2;
		}
	}

	return B_OK;
}

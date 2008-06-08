/*
 * Copyright 2004-2006, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Niels S. Reedijk
 */

#include "usb_p.h"


Pipe::Pipe(Object *parent)
	:	Object(parent),
		fDataToggle(false),
		fControllerCookie(NULL)
{
	// all other init is to be done in InitCommon()
}


Pipe::~Pipe()
{
	CancelQueuedTransfers(true);
	GetBusManager()->NotifyPipeChange(this, USB_CHANGE_DESTROYED);
}


void
Pipe::InitCommon(int8 deviceAddress, uint8 endpointAddress, usb_speed speed,
	pipeDirection direction, size_t maxPacketSize, uint8 interval,
	int8 hubAddress, uint8 hubPort)
{
	fDeviceAddress = deviceAddress;
	fEndpointAddress = endpointAddress;
	fSpeed = speed;
	fDirection = direction;
	fMaxPacketSize = maxPacketSize;
	fHubAddress = hubAddress;
	fHubPort = hubPort;

	GetBusManager()->NotifyPipeChange(this, USB_CHANGE_CREATED);
}


void
Pipe::SetHubInfo(int8 address, uint8 port)
{
	fHubAddress = address;
	fHubPort = port;
}


status_t
Pipe::SubmitTransfer(Transfer *transfer)
{
	// ToDo: keep track of all submited transfers to be able to cancel them
	return GetBusManager()->SubmitTransfer(transfer);
}


status_t
Pipe::CancelQueuedTransfers(bool force)
{
	return GetBusManager()->CancelQueuedTransfers(this, force);
}


status_t
Pipe::SetFeature(uint16 selector)
{
	return ((Device *)Parent())->DefaultPipe()->SendRequest(
		USB_REQTYPE_STANDARD | USB_REQTYPE_ENDPOINT_OUT,
		USB_REQUEST_SET_FEATURE,
		selector,
		fEndpointAddress,
		0,
		NULL,
		0,
		NULL);
}


status_t
Pipe::ClearFeature(uint16 selector)
{
	// clearing a stalled condition resets the data toggle
	if (selector == USB_FEATURE_ENDPOINT_HALT)
		SetDataToggle(false);

	return ((Device *)Parent())->DefaultPipe()->SendRequest(
		USB_REQTYPE_STANDARD | USB_REQTYPE_ENDPOINT_OUT,
		USB_REQUEST_CLEAR_FEATURE,
		selector,
		fEndpointAddress,
		0,
		NULL,
		0,
		NULL);
}


status_t
Pipe::GetStatus(uint16 *status)
{
	return ((Device *)Parent())->DefaultPipe()->SendRequest(
		USB_REQTYPE_STANDARD | USB_REQTYPE_ENDPOINT_IN,
		USB_REQUEST_GET_STATUS,
		0,
		fEndpointAddress,
		2,
		(void *)status,
		2,
		NULL);
}


//
// #pragma mark -
//


InterruptPipe::InterruptPipe(Object *parent)
	:	Pipe(parent)
{
}


status_t
InterruptPipe::QueueInterrupt(void *data, size_t dataLength,
	usb_callback_func callback, void *callbackCookie)
{
	if (dataLength > 0 && data == NULL)
		return B_BAD_VALUE;

	Transfer *transfer = new(std::nothrow) Transfer(this);
	if (!transfer)
		return B_NO_MEMORY;

	transfer->SetData((uint8 *)data, dataLength);
	transfer->SetCallback(callback, callbackCookie);

	status_t result = GetBusManager()->SubmitTransfer(transfer);
	if (result < B_OK)
		delete transfer;
	return result;
}


//
// #pragma mark -
//


BulkPipe::BulkPipe(Object *parent)
	:	Pipe(parent)
{
}


status_t
BulkPipe::QueueBulk(void *data, size_t dataLength, usb_callback_func callback,
	void *callbackCookie)
{
	if (dataLength > 0 && data == NULL)
		return B_BAD_VALUE;

	Transfer *transfer = new(std::nothrow) Transfer(this);
	if (!transfer)
		return B_NO_MEMORY;

	transfer->SetData((uint8 *)data, dataLength);
	transfer->SetCallback(callback, callbackCookie);

	status_t result = GetBusManager()->SubmitTransfer(transfer);
	if (result < B_OK)
		delete transfer;
	return result;
}


status_t
BulkPipe::QueueBulkV(iovec *vector, size_t vectorCount,
	usb_callback_func callback, void *callbackCookie)
{
	if (vectorCount > 0 && vector == NULL)
		return B_BAD_VALUE;

	Transfer *transfer = new(std::nothrow) Transfer(this);
	if (!transfer)
		return B_NO_MEMORY;

	transfer->SetVector(vector, vectorCount);
	transfer->SetCallback(callback, callbackCookie);

	status_t result = GetBusManager()->SubmitTransfer(transfer);
	if (result < B_OK)
		delete transfer;
	return result;
}


//
// #pragma mark -
//


IsochronousPipe::IsochronousPipe(Object *parent)
	:	Pipe(parent),
		fMaxQueuedPackets(0),
		fMaxBufferDuration(0),
		fSampleSize(0)
{
}


status_t
IsochronousPipe::QueueIsochronous(void *data, size_t dataLength,
	usb_iso_packet_descriptor *packetDesc, uint32 packetCount,
	uint32 *startingFrameNumber, uint32 flags, usb_callback_func callback,
	void *callbackCookie)
{
	if ((dataLength > 0 && data == NULL)
		|| (packetCount > 0 && packetDesc == NULL))
		return B_BAD_VALUE;

	usb_isochronous_data *isochronousData
		= new(std::nothrow) usb_isochronous_data;

	if (!isochronousData)
		return B_NO_MEMORY;

	isochronousData->packet_descriptors = packetDesc;
	isochronousData->packet_count = packetCount;
	isochronousData->starting_frame_number = startingFrameNumber;
	isochronousData->flags = flags;

	Transfer *transfer = new(std::nothrow) Transfer(this);
	if (!transfer) {
		delete isochronousData;
		return B_NO_MEMORY;
	}

	transfer->SetData((uint8 *)data, dataLength);
	transfer->SetCallback(callback, callbackCookie);
	transfer->SetIsochronousData(isochronousData);

	status_t result = GetBusManager()->SubmitTransfer(transfer);
	if (result < B_OK)
		delete transfer;
	return result;
}


status_t
IsochronousPipe::SetPipePolicy(uint8 maxQueuedPackets,
	uint16 maxBufferDurationMS, uint16 sampleSize)
{
	if (maxQueuedPackets == fMaxQueuedPackets
		|| maxBufferDurationMS == fMaxBufferDuration
		|| sampleSize == fSampleSize)
		return B_OK;

	fMaxQueuedPackets = maxQueuedPackets;
	fMaxBufferDuration = maxBufferDurationMS;
	fSampleSize = sampleSize;

	GetBusManager()->NotifyPipeChange(this, USB_CHANGE_PIPE_POLICY_CHANGED);
	return B_OK;
}


status_t
IsochronousPipe::GetPipePolicy(uint8 *maxQueuedPackets,
	uint16 *maxBufferDurationMS, uint16 *sampleSize)
{
	if (maxQueuedPackets)
		*maxQueuedPackets = fMaxQueuedPackets;
	if (maxBufferDurationMS)
		*maxBufferDurationMS = fMaxBufferDuration;
	if (sampleSize)
		*sampleSize = fSampleSize;
	return B_OK;
}


//
// #pragma mark -
//


ControlPipe::ControlPipe(Object *parent)
	:	Pipe(parent),
		fNotifySem(-1)
{
	mutex_init(&fSendRequestLock, "control pipe send request");
}


ControlPipe::~ControlPipe()
{
	if (fNotifySem >= 0)
		delete_sem(fNotifySem);
	mutex_lock(&fSendRequestLock);
	mutex_destroy(&fSendRequestLock);
}


status_t
ControlPipe::SendRequest(uint8 requestType, uint8 request, uint16 value,
	uint16 index, uint16 length, void *data, size_t dataLength,
	size_t *actualLength)
{
	status_t result = mutex_lock(&fSendRequestLock);
	if (result != B_OK)
		return result;

	if (fNotifySem < 0) {
		fNotifySem = create_sem(0, "usb send request notify");
		if (fNotifySem < 0) {
			mutex_unlock(&fSendRequestLock);
			return B_NO_MORE_SEMS;
		}
	}

	result = QueueRequest(requestType, request, value, index, length, data,
		dataLength, SendRequestCallback, this);
	if (result < B_OK) {
		mutex_unlock(&fSendRequestLock);
		return result;
	}

	// The sem will be released unconditionally in the callback after the
	// result data was filled in. Use a 1 second timeout for control transfers.
	if (acquire_sem_etc(fNotifySem, 1, B_RELATIVE_TIMEOUT, 1000000) < B_OK) {
		TRACE_ERROR(("USB ControlPipe: timeout waiting for queued request to complete\n"));

		CancelQueuedTransfers(false);

		// After the above cancel returns it is guaranteed that the callback
		// has been invoked. Therefore we can simply grab that released
		// semaphore again to clean up.
		acquire_sem_etc(fNotifySem, 1, B_RELATIVE_TIMEOUT, 0);

		if (actualLength)
			*actualLength = 0;

		mutex_unlock(&fSendRequestLock);
		return B_TIMED_OUT;
	}

	if (actualLength)
		*actualLength = fActualLength;

	mutex_unlock(&fSendRequestLock);
	return fTransferStatus;
}


void
ControlPipe::SendRequestCallback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	ControlPipe *pipe = (ControlPipe *)cookie;
	pipe->fTransferStatus = status;
	pipe->fActualLength = actualLength;
	release_sem(pipe->fNotifySem);
}


status_t
ControlPipe::QueueRequest(uint8 requestType, uint8 request, uint16 value,
	uint16 index, uint16 length, void *data, size_t dataLength,
	usb_callback_func callback, void *callbackCookie)
{
	if (dataLength > 0 && data == NULL)
		return B_BAD_VALUE;

	usb_request_data *requestData = new(std::nothrow) usb_request_data;
	if (!requestData)
		return B_NO_MEMORY;

	requestData->RequestType = requestType;
	requestData->Request = request;
	requestData->Value = value;
	requestData->Index = index;
	requestData->Length = length;

	Transfer *transfer = new(std::nothrow) Transfer(this);
	if (!transfer) {
		delete requestData;
		return B_NO_MEMORY;
	}

	transfer->SetRequestData(requestData);
	transfer->SetData((uint8 *)data, dataLength);
	transfer->SetCallback(callback, callbackCookie);

	status_t result = GetBusManager()->SubmitTransfer(transfer);
	if (result < B_OK)
		delete transfer;
	return result;
}

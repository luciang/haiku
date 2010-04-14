/*
 * Copyright 2003-2006, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Niels S. Reedijk
 */

#include <util/kernel_cpp.h>
#include "usb_private.h"
#include <USB_rle.h>

#define USB_MODULE_NAME "module"

Stack *gUSBStack = NULL;


#ifdef HAIKU_TARGET_PLATFORM_HAIKU
static int
debug_get_pipe_for_id(int argc, char **argv)
{
	if (gUSBStack == NULL)
		return 1;

	if (!is_debug_variable_defined("_usbPipeID"))
		return 2;

	uint64 id = get_debug_variable("_usbPipeID", 0);
	Object *object = gUSBStack->GetObjectNoLock((usb_id)id);
	if (!object || (object->Type() & USB_OBJECT_PIPE) == 0)
		return 3;

	// check if we support debug transfers for this pipe (only on UHCI for now)
	if (object->GetBusManager()->TypeName()[0] != 'u')
		return 4;

	set_debug_variable("_usbPipe", (uint64)object);
	return 0;
}
#endif


static int32
bus_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT: {
			TRACE_MODULE("init\n");
			if (gUSBStack)
				return B_OK;

#ifdef HAIKU_TARGET_PLATFORM_BEOS
			// This code is to handle plain R5 (non-BONE) where the same module
			// gets loaded multiple times (once for each exported module
			// interface, the USB v2 and v3 API in our case). We don't want to
			// ever create multiple stacks however, so we "share" the same stack
			// for both modules by storing it's address in a shared area.
			void *address = NULL;
			area_id shared = find_area("shared usb stack");
			if (shared >= B_OK && clone_area("usb stack clone", &address,
				B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA, shared) >= B_OK) {
				gUSBStack = *((Stack **)address);
				TRACE_MODULE("found shared stack at %p\n", gUSBStack);
				return B_OK;
			}
#endif

#ifdef TRACE_USB
			set_dprintf_enabled(true);
#ifndef HAIKU_TARGET_PLATFORM_HAIKU
			load_driver_symbols("usb");
#endif
#endif
			Stack *stack = new(std::nothrow) Stack();
			TRACE_MODULE("usb_module: stack created %p\n", stack);
			if (!stack)
				return B_NO_MEMORY;

			if (stack->InitCheck() != B_OK) {
				delete stack;
				return ENODEV;
			}

			gUSBStack = stack;

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
			add_debugger_command("get_usb_pipe_for_id",
				&debug_get_pipe_for_id,
				"Gets the config for a USB pipe");
#elif HAIKU_TARGET_PLATFORM_BEOS
			// Plain R5 workaround, see comment above.
			shared = create_area("shared usb stack", &address,
				B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_NO_LOCK,
				B_KERNEL_WRITE_AREA);
			if (shared >= B_OK)
				*((Stack **)address) = gUSBStack;
#endif
			break;
		}

		case B_MODULE_UNINIT:
			TRACE_MODULE("uninit\n");
			delete gUSBStack;
			gUSBStack = NULL;

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
			remove_debugger_command("get_usb_pipe_for_id",
				&debug_get_pipe_for_id);
#endif
			break;

		default:
			return EINVAL;
	}

	return B_OK;
}


status_t
register_driver(const char *driverName,
	const usb_support_descriptor *descriptors,
	size_t count, const char *optionalRepublishDriverName)
{
	return gUSBStack->RegisterDriver(driverName, descriptors, count,
		optionalRepublishDriverName);
}


status_t
install_notify(const char *driverName, const usb_notify_hooks *hooks)
{
	return gUSBStack->InstallNotify(driverName, hooks);
}


status_t
uninstall_notify(const char *driverName)
{
	return gUSBStack->UninstallNotify(driverName);
}


const usb_device_descriptor *
get_device_descriptor(usb_device device)
{
	TRACE_MODULE("get_device_descriptor(%ld)\n", device);
	Object *object = gUSBStack->GetObject(device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return NULL;

	return ((Device *)object)->DeviceDescriptor();
}


const usb_configuration_info *
get_nth_configuration(usb_device device, uint32 index)
{
	TRACE_MODULE("get_nth_configuration(%ld, %lu)\n", device, index);
	Object *object = gUSBStack->GetObject(device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return NULL;

	return ((Device *)object)->ConfigurationAt((int32)index);
}


const usb_configuration_info *
get_configuration(usb_device device)
{
	TRACE_MODULE("get_configuration(%ld)\n", device);
	Object *object = gUSBStack->GetObject(device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return NULL;

	return ((Device *)object)->Configuration();
}


status_t
set_configuration(usb_device device,
	const usb_configuration_info *configuration)
{
	TRACE_MODULE("set_configuration(%ld, %p)\n", device, configuration);
	Object *object = gUSBStack->GetObject(device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((Device *)object)->SetConfiguration(configuration);
}


status_t
set_alt_interface(usb_device device, const usb_interface_info *interface)
{
	TRACE_MODULE("set_alt_interface(%ld, %p)\n", device, interface);
	Object *object = gUSBStack->GetObject(device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((Device *)object)->SetAltInterface(interface);
}


status_t
set_feature(usb_id handle, uint16 selector)
{
	TRACE_MODULE("set_feature(%ld, %d)\n", handle, selector);
	Object *object = gUSBStack->GetObject(handle);
	if (!object)
		return B_DEV_INVALID_PIPE;

	return object->SetFeature(selector);
}


status_t
clear_feature(usb_id handle, uint16 selector)
{
	TRACE_MODULE("clear_feature(%ld, %d)\n", handle, selector);
	Object *object = gUSBStack->GetObject(handle);
	if (!object)
		return B_DEV_INVALID_PIPE;

	return object->ClearFeature(selector);
}


status_t
get_status(usb_id handle, uint16 *status)
{
	TRACE_MODULE("get_status(%ld, %p)\n", handle, status);
	if (!status)
		return B_BAD_VALUE;

	Object *object = gUSBStack->GetObject(handle);
	if (!object)
		return B_DEV_INVALID_PIPE;

	return object->GetStatus(status);
}


status_t
get_descriptor(usb_device device, uint8 type, uint8 index, uint16 languageID,
	void *data, size_t dataLength, size_t *actualLength)
{
	TRACE_MODULE("get_descriptor(%ld, 0x%02x, 0x%02x, 0x%04x, %p, %ld, %p)\n",
		device, type, index, languageID, data, dataLength, actualLength);
	Object *object = gUSBStack->GetObject(device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((Device *)object)->GetDescriptor(type, index, languageID,
		data, dataLength, actualLength);
}


status_t
send_request(usb_device device, uint8 requestType, uint8 request,
	uint16 value, uint16 index, uint16 length, void *data, size_t *actualLength)
{
	TRACE_MODULE("send_request(%ld, 0x%02x, 0x%02x, 0x%04x, 0x%04x, %d, %p, %p)\n",
		device, requestType, request, value, index, length, data, actualLength);
	Object *object = gUSBStack->GetObject(device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((Device *)object)->DefaultPipe()->SendRequest(requestType, request,
		value, index, length, data, length, actualLength);
}


status_t
queue_request(usb_device device, uint8 requestType, uint8 request,
	uint16 value, uint16 index, uint16 length, void *data,
	usb_callback_func callback, void *callbackCookie)
{
	TRACE_MODULE("queue_request(%ld, 0x%02x, 0x%02x, 0x%04x, 0x%04x, %u, %p, %p, %p)\n",
		device, requestType, request, value, index, length, data, callback,
		callbackCookie);
	Object *object = gUSBStack->GetObject(device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((Device *)object)->DefaultPipe()->QueueRequest(requestType,
		request, value, index, length, data, length, callback, callbackCookie);
}


status_t
queue_interrupt(usb_pipe pipe, void *data, size_t dataLength,
	usb_callback_func callback, void *callbackCookie)
{
	TRACE_MODULE("queue_interrupt(%ld, %p, %ld, %p, %p)\n",
		pipe, data, dataLength, callback, callbackCookie);
	Object *object = gUSBStack->GetObject(pipe);
	if (!object || (object->Type() & USB_OBJECT_INTERRUPT_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((InterruptPipe *)object)->QueueInterrupt(data, dataLength, callback,
		callbackCookie);
}


status_t
queue_bulk(usb_pipe pipe, void *data, size_t dataLength,
	usb_callback_func callback, void *callbackCookie)
{
	TRACE_MODULE("queue_bulk(%ld, %p, %ld, %p, %p)\n",
		pipe, data, dataLength, callback, callbackCookie);
	Object *object = gUSBStack->GetObject(pipe);
	if (!object || (object->Type() & USB_OBJECT_BULK_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((BulkPipe *)object)->QueueBulk(data, dataLength, callback,
		callbackCookie);
}


status_t
queue_bulk_v(usb_pipe pipe, iovec *vector, size_t vectorCount,
	usb_callback_func callback, void *callbackCookie)
{
	TRACE_MODULE("queue_bulk_v(%ld, %p, %ld, %p, %p)\n",
		pipe, vector, vectorCount, callback, callbackCookie);
	Object *object = gUSBStack->GetObject(pipe);
	if (!object || (object->Type() & USB_OBJECT_BULK_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((BulkPipe *)object)->QueueBulkV(vector, vectorCount, callback,
		callbackCookie, false);
}


status_t
queue_bulk_v_physical(usb_pipe pipe, iovec *vector, size_t vectorCount,
	usb_callback_func callback, void *callbackCookie)
{
	TRACE_MODULE("queue_bulk_v_physical(%ld, %p, %ld, %p, %p)\n",
		pipe, vector, vectorCount, callback, callbackCookie);
	Object *object = gUSBStack->GetObject(pipe);
	if (!object || (object->Type() & USB_OBJECT_BULK_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((BulkPipe *)object)->QueueBulkV(vector, vectorCount, callback,
		callbackCookie, true);
}


status_t
queue_isochronous(usb_pipe pipe, void *data, size_t dataLength,
	usb_iso_packet_descriptor *packetDesc, uint32 packetCount,
	uint32 *startingFrameNumber, uint32 flags, usb_callback_func callback,
	void *callbackCookie)
{
	TRACE_MODULE("queue_isochronous(%ld, %p, %ld, %p, %ld, %p, 0x%08lx, %p, %p)\n",
		pipe, data, dataLength, packetDesc, packetCount, startingFrameNumber,
		flags, callback, callbackCookie);
	Object *object = gUSBStack->GetObject(pipe);
	if (!object || (object->Type() & USB_OBJECT_ISO_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((IsochronousPipe *)object)->QueueIsochronous(data, dataLength,
		packetDesc, packetCount, startingFrameNumber, flags, callback,
		callbackCookie);
}


status_t
set_pipe_policy(usb_pipe pipe, uint8 maxQueuedPackets,
	uint16 maxBufferDurationMS, uint16 sampleSize)
{
	TRACE_MODULE("set_pipe_policy(%ld, %d, %d, %d)\n", pipe, maxQueuedPackets,
		maxBufferDurationMS, sampleSize);
	Object *object = gUSBStack->GetObject(pipe);
	if (!object || (object->Type() & USB_OBJECT_ISO_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((IsochronousPipe *)object)->SetPipePolicy(maxQueuedPackets,
		maxBufferDurationMS, sampleSize);
}


status_t
cancel_queued_transfers(usb_pipe pipe)
{
	TRACE_MODULE("cancel_queued_transfers(%ld)\n", pipe);
	Object *object = gUSBStack->GetObject(pipe);
	if (!object || (object->Type() & USB_OBJECT_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return ((Pipe *)object)->CancelQueuedTransfers(false);
}


status_t
usb_ioctl(uint32 opcode, void *buffer, size_t bufferSize)
{
	TRACE_MODULE("usb_ioctl(%lu, %p, %ld)\n", opcode, buffer, bufferSize);

	switch (opcode) {
		case 'DNAM': {
			Object *object = gUSBStack->GetObject(*(usb_id *)buffer);
			if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
				return B_BAD_VALUE;

			uint32 index = 0;
			return ((Device *)object)->BuildDeviceName((char *)buffer, &index,
				bufferSize, NULL);
		}
	}

	return B_DEV_INVALID_IOCTL;
}


status_t
get_nth_roothub(uint32 index, usb_device *rootHub)
{
	if (!rootHub)
		return B_BAD_VALUE;

	BusManager *busManager = gUSBStack->BusManagerAt(index);
	if (!busManager)
		return B_ENTRY_NOT_FOUND;

	Hub *hub = busManager->GetRootHub();
	if (!hub)
		return B_NO_INIT;

	*rootHub = hub->USBID();
	return B_OK;
}


status_t
get_nth_child(usb_device _hub, uint8 index, usb_device *childDevice)
{
	if (!childDevice)
		return B_BAD_VALUE;

	Object *object = gUSBStack->GetObject(_hub);
	if (!object || (object->Type() & USB_OBJECT_HUB) == 0)
		return B_DEV_INVALID_PIPE;

	Hub *hub = (Hub *)object;
	for (uint8 i = 0; i < 8; i++) {
		if (hub->ChildAt(i) == NULL)
			continue;

		if (index-- > 0)
			continue;

		*childDevice = hub->ChildAt(i)->USBID();
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
get_device_parent(usb_device _device, usb_device *parentHub, uint8 *portIndex)
{
	if (!parentHub || !portIndex)
		return B_BAD_VALUE;

	Object *object = gUSBStack->GetObject(_device);
	if (!object || (object->Type() & USB_OBJECT_DEVICE) == 0)
		return B_DEV_INVALID_PIPE;

	Object *parent = object->Parent();
	if (!parent || (parent->Type() & USB_OBJECT_HUB) == 0)
		return B_ENTRY_NOT_FOUND;

	Hub *hub = (Hub *)parent;
	for (uint8 i = 0; i < 8; i++) {
		if (hub->ChildAt(i) == object) {
			*portIndex = i;
			*parentHub = hub->USBID();
			return B_OK;
		}
	}

	return B_ERROR;
}


status_t
reset_port(usb_device _hub, uint8 portIndex)
{
	Object *object = gUSBStack->GetObject(_hub);
	if (!object || (object->Type() & USB_OBJECT_HUB) == 0)
		return B_DEV_INVALID_PIPE;

	Hub *hub = (Hub *)object;
	return hub->ResetPort(portIndex);
}


status_t
disable_port(usb_device _hub, uint8 portIndex)
{
	Object *object = gUSBStack->GetObject(_hub);
	if (!object || (object->Type() & USB_OBJECT_HUB) == 0)
		return B_DEV_INVALID_PIPE;

	Hub *hub = (Hub *)object;
	return hub->DisablePort(portIndex);
}


/*
	This module exports the USB API v3
*/
struct usb_module_info gModuleInfoV3 = {
	// First the bus_manager_info:
	{
		{
			"bus_managers/usb/v3",
			B_KEEP_LOADED,				// Keep loaded, even if no driver requires it
			bus_std_ops
		},
		NULL							// the rescan function
	},

	register_driver,					// register_driver
	install_notify,						// install_notify
	uninstall_notify,					// uninstall_notify
	get_device_descriptor,				// get_device_descriptor
	get_nth_configuration,				// get_nth_configuration
	get_configuration,					// get_configuration
	set_configuration,					// set_configuration
	set_alt_interface,					// set_alt_interface
	set_feature,						// set_feature
	clear_feature, 						// clear_feature
	get_status, 						// get_status
	get_descriptor,						// get_descriptor
	send_request,						// send_request
	queue_interrupt,					// queue_interrupt
	queue_bulk,							// queue_bulk
	queue_bulk_v,						// queue_bulk_v
	queue_isochronous,					// queue_isochronous
	queue_request,						// queue_request
	set_pipe_policy,					// set_pipe_policy
	cancel_queued_transfers,			// cancel_queued_transfers
	usb_ioctl,							// usb_ioctl
	get_nth_roothub,					// get_nth_roothub
	get_nth_child,						// get_nth_child
	get_device_parent,					// get_device_parent
	reset_port,							// reset_port
	disable_port						// disable_port
	//queue_bulk_v_physical				// queue_bulk_v_physical
};


//
// #pragma mark -
//


const usb_device_descriptor *
get_device_descriptor_v2(const void *device)
{
	return get_device_descriptor((usb_id)device);
}


const usb_configuration_info *
get_nth_configuration_v2(const void *device, uint index)
{
	return get_nth_configuration((usb_id)device, index);
}


const usb_configuration_info *
get_configuration_v2(const void *device)
{
	return get_configuration((usb_id)device);
}


status_t
set_configuration_v2(const void *device,
	const usb_configuration_info *configuration)
{
	return set_configuration((usb_id)device, configuration);
}


status_t
set_alt_interface_v2(const void *device, const usb_interface_info *interface)
{
	return set_alt_interface((usb_id)device, interface);
}


status_t
set_feature_v2(const void *object, uint16 selector)
{
	return set_feature((usb_id)object, selector);
}


status_t
clear_feature_v2(const void *object, uint16 selector)
{
	return clear_feature((usb_id)object, selector);
}


status_t
get_status_v2(const void *object, uint16 *status)
{
	return get_status((usb_id)object, status);
}


status_t
get_descriptor_v2(const void *device, uint8 type, uint8 index,
	uint16 languageID, void *data, size_t dataLength, size_t *actualLength)
{
	return get_descriptor((usb_id)device, type, index, languageID, data,
		dataLength, actualLength);
}


status_t
send_request_v2(const void *device, uint8 requestType, uint8 request,
	uint16 value, uint16 index, uint16 length, void *data,
	size_t /*dataLength*/, size_t *actualLength)
{
	return send_request((usb_id)device, requestType, request, value, index,
		length, data, actualLength);
}


status_t
queue_request_v2(const void *device, uint8 requestType, uint8 request,
	uint16 value, uint16 index, uint16 length, void *data,
	size_t /*dataLength*/, usb_callback_func callback, void *callbackCookie)
{
	return queue_request((usb_id)device, requestType, request, value, index,
		length, data, callback, callbackCookie);
}


status_t
queue_interrupt_v2(const void *pipe, void *data, size_t dataLength,
	usb_callback_func callback, void *callbackCookie)
{
	return queue_interrupt((usb_id)pipe, data, dataLength, callback,
		callbackCookie);
}


status_t
queue_bulk_v2(const void *pipe, void *data, size_t dataLength,
	usb_callback_func callback, void *callbackCookie)
{
	return queue_bulk((usb_id)pipe, data, dataLength, callback,
		callbackCookie);
}


status_t
queue_isochronous_v2(const void *pipe, void *data, size_t dataLength,
	rlea *rleArray, uint16 bufferDurationMS, usb_callback_func callback,
	void *callbackCookie)
{
	// ToDo: convert rlea to usb_iso_packet_descriptor
	// ToDo: use a flag to indicate that the callback shall produce a rlea
	usb_iso_packet_descriptor *packetDesc = NULL;
	return queue_isochronous((usb_id)pipe, data, dataLength, packetDesc, 0,
		NULL, 0, callback, callbackCookie);
}


status_t
set_pipe_policy_v2(const void *pipe, uint8 maxQueuedPackets,
	uint16 maxBufferDurationMS, uint16 sampleSize)
{
	return set_pipe_policy((usb_id)pipe, maxQueuedPackets, maxBufferDurationMS,
		sampleSize);
}


status_t
cancel_queued_transfers_v2(const void *pipe)
{
	return cancel_queued_transfers((usb_id)pipe);
}


struct usb_module_info_v2 {
	bus_manager_info				binfo;
	status_t						(*register_driver)(const char *, const usb_support_descriptor *, size_t, const char *);
	status_t						(*install_notify)(const char *, const usb_notify_hooks *);
	status_t						(*uninstall_notify)(const char *);
	const usb_device_descriptor		*(*get_device_descriptor)(const void *);
	const usb_configuration_info	*(*get_nth_configuration)(const void *, uint);
	const usb_configuration_info	*(*get_configuration)(const void *);
	status_t						(*set_configuration)(const void *, const usb_configuration_info *);
	status_t						(*set_alt_interface)(const void *, const usb_interface_info *);
	status_t						(*set_feature)(const void *, uint16);
	status_t						(*clear_feature)(const void *, uint16);
	status_t						(*get_status)(const void *, uint16 *);
	status_t						(*get_descriptor)(const void *, uint8, uint8, uint16, void *, size_t, size_t *);
	status_t						(*send_request)(const void *, uint8, uint8, uint16, uint16, uint16, void *, size_t, size_t *);
	status_t						(*queue_interrupt)(const void *, void *, size_t, usb_callback_func, void *);
	status_t						(*queue_bulk)(const void *, void *, size_t, usb_callback_func, void *);
	status_t						(*queue_isochronous)(const void *, void *, size_t, rlea *, uint16, usb_callback_func, void *);
	status_t						(*queue_request)(const void *, uint8, uint8, uint16, uint16, uint16, void *, size_t, usb_callback_func, void *);
	status_t						(*set_pipe_policy)(const void *, uint8, uint16, uint16);
	status_t						(*cancel_queued_transfers)(const void *);
	status_t						(*usb_ioctl)(uint32 opcode, void *,size_t);
};


/*
	This module exports the USB API v2
*/
struct usb_module_info_v2 gModuleInfoV2 = {
	// First the bus_manager_info:
	{
		{
			"bus_managers/usb/v2",
			B_KEEP_LOADED,				// Keep loaded, even if no driver requires it
			bus_std_ops
		},
		NULL							// the rescan function
	},

	register_driver,					// register_driver
	install_notify,						// install_notify
	uninstall_notify,					// uninstall_notify
	get_device_descriptor_v2,			// get_device_descriptor
	get_nth_configuration_v2,			// get_nth_configuration
	get_configuration_v2,				// get_configuration
	set_configuration_v2,				// set_configuration
	set_alt_interface_v2,				// set_alt_interface
	set_feature_v2,						// set_feature
	clear_feature_v2,					// clear_feature
	get_status_v2, 						// get_status
	get_descriptor_v2,					// get_descriptor
	send_request_v2,					// send_request
	queue_interrupt_v2,					// queue_interrupt
	queue_bulk_v2,						// queue_bulk
	queue_isochronous_v2,				// queue_isochronous
	queue_request_v2,					// queue_request
	set_pipe_policy_v2,					// set_pipe_policy
	cancel_queued_transfers_v2,			// cancel_queued_transfers
	usb_ioctl							// usb_ioctl
};


//
// #pragma mark -
//


module_info *modules[] = {
	(module_info *)&gModuleInfoV2,
	(module_info *)&gModuleInfoV3,
	NULL
};

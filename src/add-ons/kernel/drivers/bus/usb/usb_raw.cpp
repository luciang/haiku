/*
 * Copyright 2006-2008, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 */

#include <KernelExport.h>
#include <Drivers.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "usb_raw.h"
#include "usb_raw_private.h"
#include "BeOSCompatibility.h"


//#define TRACE_USB_RAW
#ifdef TRACE_USB_RAW
#define TRACE(x)	dprintf x
#else
#define TRACE(x)	/* nothing */
#endif


#define DRIVER_NAME		"usb_raw"
#define DEVICE_NAME		"bus/usb/raw"
#define DRIVER_VERSION	0x0015

int32 api_version = B_CUR_DRIVER_API_VERSION;
static usb_module_info *gUSBModule = NULL;
static raw_device *gDeviceList = NULL;
static uint32 gDeviceCount = 0;
static benaphore gDeviceListLock;
static char **gDeviceNames = NULL;

static status_t
usb_raw_device_added(usb_device newDevice, void **cookie)
{
	TRACE((DRIVER_NAME": device_added(0x%08lx)\n", newDevice));
	raw_device *device = (raw_device *)malloc(sizeof(raw_device));

	status_t result = benaphore_init(&device->lock, "usb_raw device lock");
	if (result < B_OK) {
		free(device);
		return result;
	}

	device->notify = create_sem(0, "usb_raw callback notify");
	if (device->notify < B_OK) {
		benaphore_destroy(&device->lock);
		free(device);
		return B_NO_MORE_SEMS;
	}

	char deviceName[32];
	memcpy(deviceName, &newDevice, sizeof(usb_device));
	if (gUSBModule->usb_ioctl('DNAM', deviceName, sizeof(deviceName)) >= B_OK) {
		sprintf(device->name, "bus/usb/%s", deviceName);
	} else {
		sprintf(device->name, "bus/usb/%08lx", newDevice);
	}

	device->device = newDevice;
	device->reference_count = 0;

	benaphore_lock(&gDeviceListLock);
	device->link = (void *)gDeviceList;
	gDeviceList = device;
	gDeviceCount++;
	benaphore_unlock(&gDeviceListLock);

	TRACE((DRIVER_NAME": new device: 0x%08lx\n", (uint32)device));
	*cookie = (void *)device;
	return B_OK;
}


static status_t
usb_raw_device_removed(void *cookie)
{
	TRACE((DRIVER_NAME": device_removed(0x%08lx)\n", (uint32)cookie));
	raw_device *device = (raw_device *)cookie;

	benaphore_lock(&gDeviceListLock);
	if (gDeviceList == device) {
		gDeviceList = (raw_device *)device->link;
	} else {
		raw_device *element = gDeviceList;
		while (element) {
			if (element->link == device) {
				element->link = device->link;
				break;
			}

			element = (raw_device *)element->link;
		}
	}
	gDeviceCount--;
	benaphore_unlock(&gDeviceListLock);

	device->device = 0;
	if (device->reference_count == 0) {
		benaphore_lock(&device->lock);
		benaphore_destroy(&device->lock);
		delete_sem(device->notify);
		free(device);
	}

	return B_OK;
}


//
//#pragma mark -
//


static status_t
usb_raw_open(const char *name, uint32 flags, void **cookie)
{
	TRACE((DRIVER_NAME": open()\n"));
	benaphore_lock(&gDeviceListLock);
	raw_device *element = gDeviceList;
	while (element) {
		if (strcmp(name, element->name) == 0) {
			element->reference_count++;
			*cookie = element;
			benaphore_unlock(&gDeviceListLock);
			return B_OK;
		}

		element = (raw_device *)element->link;
	}

	benaphore_unlock(&gDeviceListLock);
	return B_NAME_NOT_FOUND;
}


static status_t
usb_raw_close(void *cookie)
{
	TRACE((DRIVER_NAME": close()\n"));
	return B_OK;
}


static status_t
usb_raw_free(void *cookie)
{
	TRACE((DRIVER_NAME": free()\n"));
	benaphore_lock(&gDeviceListLock);

	raw_device *device = (raw_device *)cookie;
	device->reference_count--;
	if (device->device == 0) {
		benaphore_lock(&device->lock);
		benaphore_destroy(&device->lock);
		delete_sem(device->notify);
		free(device);
	}

	benaphore_unlock(&gDeviceListLock);
	return B_OK;
}


static void
usb_raw_callback(void *cookie, status_t status, void *data, size_t actualLength)
{
	TRACE((DRIVER_NAME": callback()\n"));
	raw_device *device = (raw_device *)cookie;

	switch (status) {
		case B_OK:
			device->status = RAW_STATUS_SUCCESS;
			break;
		case B_TIMED_OUT:
			device->status = RAW_STATUS_TIMEOUT;
			break;
		case B_CANCELED:
			device->status = RAW_STATUS_ABORTED;
			break;
		case B_DEV_CRC_ERROR:
			device->status = RAW_STATUS_CRC_ERROR;
			break;
		case B_DEV_STALLED:
			device->status = RAW_STATUS_STALLED;
			break;
		default:
			device->status = RAW_STATUS_FAILED;
			break;
	}

	device->actual_length = actualLength;
	release_sem(device->notify);
}


static status_t
usb_raw_ioctl(void *cookie, uint32 op, void *buffer, size_t length)
{
	TRACE((DRIVER_NAME": ioctl\n"));
	raw_device *device = (raw_device *)cookie;
	raw_command *command = (raw_command *)buffer;

	if (device->device == 0)
		return B_DEV_NOT_READY;

	switch (op) {
		case RAW_COMMAND_GET_VERSION: {
			command->version.status = DRIVER_VERSION;
			return B_OK;
		}

		case RAW_COMMAND_GET_DEVICE_DESCRIPTOR: {
			const usb_device_descriptor *deviceDescriptor =
				gUSBModule->get_device_descriptor(device->device);
			if (!deviceDescriptor) {
				command->device.status = RAW_STATUS_ABORTED;
				return B_OK;
			}

			memcpy(command->device.descriptor, deviceDescriptor,
				sizeof(usb_device_descriptor));
			command->device.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_nth_configuration(device->device,
				command->config.config_index);
			if (!configurationInfo) {
				command->config.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			memcpy(command->config.descriptor, configurationInfo->descr,
				sizeof(usb_configuration_descriptor));
			command->config.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_GET_INTERFACE_DESCRIPTOR: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_nth_configuration(device->device,
				command->interface.config_index);
			if (!configurationInfo) {
				command->interface.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			if (command->interface.interface_index >= configurationInfo->interface_count) {
				command->interface.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			const usb_interface_info *interfaceInfo =
				configurationInfo->interface[command->interface.interface_index].active;
			if (!interfaceInfo) {
				command->interface.status = RAW_STATUS_ABORTED;
				return B_OK;
			}

			memcpy(command->interface.descriptor, interfaceInfo->descr,
				sizeof(usb_interface_descriptor));
			command->interface.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_GET_ALT_INTERFACE_COUNT: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_nth_configuration(device->device,
				command->alternate.config_index);
			if (!configurationInfo) {
				command->alternate.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			if (command->alternate.interface_index >= configurationInfo->interface_count) {
				command->alternate.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			*command->alternate.alternate_count
				= configurationInfo->interface[command->alternate.interface_index].alt_count;
			command->alternate.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_GET_ALT_INTERFACE_DESCRIPTOR: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_nth_configuration(device->device,
				command->alternate.config_index);
			if (!configurationInfo) {
				command->alternate.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			if (command->alternate.interface_index >= configurationInfo->interface_count) {
				command->alternate.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			const usb_interface_list *interfaceList =
				&configurationInfo->interface[command->alternate.interface_index];
			if (command->alternate.alternate_index >= interfaceList->alt_count) {
				command->alternate.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			memcpy(command->alternate.descriptor,
				&interfaceList->alt[command->alternate.alternate_index],
				sizeof(usb_interface_descriptor));
			command->alternate.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_nth_configuration(device->device,
				command->endpoint.config_index);
			if (!configurationInfo) {
				command->endpoint.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			if (command->endpoint.interface_index >= configurationInfo->interface_count) {
				command->endpoint.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			const usb_interface_info *interfaceInfo =
				configurationInfo->interface[command->endpoint.interface_index].active;
			if (!interfaceInfo) {
				command->endpoint.status = RAW_STATUS_ABORTED;
				return B_OK;
			}

			if (command->endpoint.endpoint_index >= interfaceInfo->endpoint_count) {
				command->endpoint.status = RAW_STATUS_INVALID_ENDPOINT;
				return B_OK;
			}

			memcpy(command->endpoint.descriptor,
				interfaceInfo->endpoint[command->endpoint.endpoint_index].descr,
				sizeof(usb_endpoint_descriptor));
			command->endpoint.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_GET_GENERIC_DESCRIPTOR: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_nth_configuration(device->device,
				command->generic.config_index);
			if (!configurationInfo) {
				command->generic.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			if (command->generic.interface_index >= configurationInfo->interface_count) {
				command->generic.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			const usb_interface_info *interfaceInfo =
				configurationInfo->interface[command->generic.interface_index].active;
			if (!interfaceInfo) {
				command->generic.status = RAW_STATUS_ABORTED;
				return B_OK;
			}

			if (command->generic.generic_index >= interfaceInfo->generic_count) {
				// ToDo: add RAW_STATUS_INVALID_GENERIC
				command->generic.status = RAW_STATUS_INVALID_ENDPOINT;
				return B_OK;
			}

			usb_descriptor *descriptor = interfaceInfo->generic[command->generic.generic_index];
			if (!descriptor) {
				command->generic.status = RAW_STATUS_ABORTED;
				return B_OK;
			}

			if (descriptor->generic.length > command->generic.length) {
				command->generic.status = RAW_STATUS_NO_MEMORY;
				return B_OK;
			}

			memcpy(command->generic.descriptor, descriptor,
				descriptor->generic.length);
			command->generic.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_GET_STRING_DESCRIPTOR: {
			size_t actualLength = 0;
			uint8 firstTwoBytes[2];

			if (gUSBModule->get_descriptor(device->device,
				USB_DESCRIPTOR_STRING, command->string.string_index, 0,
				firstTwoBytes, 2, &actualLength) < B_OK
				|| actualLength != 2
				|| firstTwoBytes[1] != USB_DESCRIPTOR_STRING) {
				command->string.status = RAW_STATUS_ABORTED;
				command->string.length = 0;
				return B_OK;
			}

			uint8 stringLength = MIN(firstTwoBytes[0], command->string.length);
			char *string = (char *)malloc(stringLength);
			if (!string) {
				command->string.status = RAW_STATUS_ABORTED;
				command->string.length = 0;
				return B_NO_MEMORY;
			}

			if (gUSBModule->get_descriptor(device->device,
				USB_DESCRIPTOR_STRING, command->string.string_index, 0,
				string, stringLength, &actualLength) < B_OK
				|| actualLength != stringLength) {
				command->string.status = RAW_STATUS_ABORTED;
				command->string.length = 0;
				free(string);
				return B_OK;
			}

			memcpy(command->string.descriptor, string, stringLength);
			command->string.status = RAW_STATUS_SUCCESS;
			command->string.length = stringLength;
			free(string);
			return B_OK;
		}

		case RAW_COMMAND_GET_DESCRIPTOR: {
			size_t actualLength = 0;
			uint8 firstTwoBytes[2];

			if (gUSBModule->get_descriptor(device->device,
				command->descriptor.type, command->descriptor.index,
				command->descriptor.language_id, firstTwoBytes, 2,
				&actualLength) < B_OK
				|| actualLength != 2
				|| firstTwoBytes[1] != command->descriptor.type) {
				command->descriptor.status = RAW_STATUS_ABORTED;
				command->descriptor.length = 0;
				return B_OK;
			}

			uint8 length = MIN(firstTwoBytes[0], command->descriptor.length);
			uint8 *buffer = (uint8 *)malloc(length);
			if (!buffer) {
				command->descriptor.status = RAW_STATUS_ABORTED;
				command->descriptor.length = 0;
				return B_NO_MEMORY;
			}

			if (gUSBModule->get_descriptor(device->device,
				command->descriptor.type, command->descriptor.index,
				command->descriptor.language_id, buffer, length,
				&actualLength) < B_OK
				|| actualLength != length) {
				command->descriptor.status = RAW_STATUS_ABORTED;
				command->descriptor.length = 0;
				free(buffer);
				return B_OK;
			}

			memcpy(command->descriptor.data, buffer, length);
			command->descriptor.status = RAW_STATUS_SUCCESS;
			command->descriptor.length = length;
			free(buffer);
			return B_OK;
		}

		case RAW_COMMAND_SET_CONFIGURATION: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_nth_configuration(device->device,
				command->config.config_index);
			if (!configurationInfo) {
				command->config.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			if (gUSBModule->set_configuration(device->device,
				configurationInfo) < B_OK) {
				command->config.status = RAW_STATUS_FAILED;
				return B_OK;
			}

			command->config.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_SET_ALT_INTERFACE: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_nth_configuration(device->device,
				command->alternate.config_index);
			if (!configurationInfo) {
				command->alternate.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			if (command->alternate.interface_index >= configurationInfo->interface_count) {
				command->alternate.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			const usb_interface_list *interfaceList =
				&configurationInfo->interface[command->alternate.interface_index];
			if (command->alternate.alternate_index >= interfaceList->alt_count) {
				command->alternate.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			if (gUSBModule->set_alt_interface(device->device,
				&interfaceList->alt[command->alternate.alternate_index]) < B_OK) {
				command->alternate.status = RAW_STATUS_FAILED;
				return B_OK;
			}

			command->alternate.status = RAW_STATUS_SUCCESS;
			return B_OK;
		}

		case RAW_COMMAND_CONTROL_TRANSFER: {
			benaphore_lock(&device->lock);
			if (gUSBModule->queue_request(device->device,
				command->control.request_type, command->control.request,
				command->control.value, command->control.index,
				command->control.length, command->control.data,
				usb_raw_callback, device) < B_OK) {
				command->control.status = RAW_STATUS_FAILED;
				command->control.length = 0;
				benaphore_unlock(&device->lock);
				return B_OK;
			}

			acquire_sem(device->notify);
			command->control.status = device->status;
			command->control.length = device->actual_length;
			benaphore_unlock(&device->lock);
			return B_OK;
		}

		case RAW_COMMAND_INTERRUPT_TRANSFER:
		case RAW_COMMAND_BULK_TRANSFER:
		case RAW_COMMAND_ISOCHRONOUS_TRANSFER: {
			const usb_configuration_info *configurationInfo =
				gUSBModule->get_configuration(device->device);
			if (!configurationInfo) {
				command->transfer.status = RAW_STATUS_INVALID_CONFIGURATION;
				return B_OK;
			}

			if (command->transfer.interface >= configurationInfo->interface_count) {
				command->transfer.status = RAW_STATUS_INVALID_INTERFACE;
				return B_OK;
			}

			const usb_interface_info *interfaceInfo =
				configurationInfo->interface[command->transfer.interface].active;
			if (!interfaceInfo) {
				command->transfer.status = RAW_STATUS_ABORTED;
				return B_OK;
			}

			if (command->transfer.endpoint >= interfaceInfo->endpoint_count) {
				command->transfer.status = RAW_STATUS_INVALID_ENDPOINT;
				return B_OK;
			}

			const usb_endpoint_info *endpointInfo =
				&interfaceInfo->endpoint[command->transfer.endpoint];
			if (!endpointInfo->handle) {
				command->transfer.status = RAW_STATUS_INVALID_ENDPOINT;
				return B_OK;
			}

			benaphore_lock(&device->lock);

			status_t status;
			if (op == RAW_COMMAND_INTERRUPT_TRANSFER) {
				status = gUSBModule->queue_interrupt(endpointInfo->handle,
					command->transfer.data, command->transfer.length,
					usb_raw_callback, device);
			} else if (op == RAW_COMMAND_BULK_TRANSFER) {
				status = gUSBModule->queue_bulk(endpointInfo->handle,
					command->transfer.data, command->transfer.length,
					usb_raw_callback, device);
			} else {
				status = gUSBModule->queue_isochronous(endpointInfo->handle,
					command->isochronous.data, command->isochronous.length,
					command->isochronous.packet_descriptors,
					command->isochronous.packet_count, NULL, 0,
					usb_raw_callback, device);
			}

			if (status < B_OK) {
				command->transfer.status = RAW_STATUS_FAILED;
				command->transfer.length = 0;
				benaphore_unlock(&device->lock);
				return B_OK;
			}

			acquire_sem(device->notify);
			command->transfer.status = device->status;
			command->transfer.length = device->actual_length;
			benaphore_unlock(&device->lock);
			return B_OK;
		}
	}

	return B_DEV_INVALID_IOCTL;
}


static status_t
usb_raw_read(void *cookie, off_t position, void *buffer, size_t *length)
{
	TRACE((DRIVER_NAME": read()\n"));
	*length = 0;
	return B_OK;
}


static status_t
usb_raw_write(void *cookie, off_t position, const void *buffer, size_t *length)
{
	TRACE((DRIVER_NAME": write()\n"));
	*length = 0;
	return B_OK;
}


//
//#pragma mark -
//


status_t 
init_hardware()
{
	TRACE((DRIVER_NAME": init_hardware()\n"));
	return B_OK;
}


status_t
init_driver()
{
	TRACE((DRIVER_NAME": init_driver()\n"));
	static usb_notify_hooks notifyHooks = {
		&usb_raw_device_added,
		&usb_raw_device_removed
	};

	gDeviceList = NULL;
	gDeviceCount = 0;
	status_t result = benaphore_init(&gDeviceListLock, "usb_raw device list lock");
	if (result < B_OK) {
		TRACE((DRIVER_NAME": failed to create device list lock\n"));
		return result;
	}

	TRACE((DRIVER_NAME": trying module %s\n", B_USB_MODULE_NAME));
	result = get_module(B_USB_MODULE_NAME, (module_info **)&gUSBModule);
	if (result < B_OK) {
		TRACE((DRIVER_NAME": getting module failed 0x%08lx\n", result));
		benaphore_destroy(&gDeviceListLock);
		return result;
	}

	gUSBModule->register_driver(DRIVER_NAME, NULL, 0, NULL);
	gUSBModule->install_notify(DRIVER_NAME, &notifyHooks);
	return B_OK;
}


void
uninit_driver()
{
	TRACE((DRIVER_NAME": uninit_driver()\n"));
	gUSBModule->uninstall_notify(DRIVER_NAME);
	benaphore_lock(&gDeviceListLock);

	if (gDeviceNames) {
		for (int32 i = 1; gDeviceNames[i]; i++)
			free(gDeviceNames[i]);
		free(gDeviceNames);
		gDeviceNames = NULL;
	}

	benaphore_destroy(&gDeviceListLock);
	put_module(B_USB_MODULE_NAME);
}


const char **
publish_devices()
{
	TRACE((DRIVER_NAME": publish_devices()\n"));
	if (gDeviceNames) {
		for (int32 i = 1; gDeviceNames[i]; i++)
			free(gDeviceNames[i]);
		free(gDeviceNames);
		gDeviceNames = NULL;
	}

	int32 index = 0;
	gDeviceNames = (char **)malloc(sizeof(char *) * (gDeviceCount + 2));
	if (!gDeviceNames)
		return NULL;

	gDeviceNames[index++] = DEVICE_NAME;

	benaphore_lock(&gDeviceListLock);
	raw_device *element = gDeviceList;
	while (element) {
		gDeviceNames[index++] = strdup(element->name);
		element = (raw_device *)element->link;
	}

	gDeviceNames[index++] = NULL;
	benaphore_unlock(&gDeviceListLock);
	return (const char **)gDeviceNames;
}


device_hooks *
find_device(const char *name)
{
	TRACE((DRIVER_NAME": find_device()\n"));
	static device_hooks hooks = {
		&usb_raw_open,
		&usb_raw_close,
		&usb_raw_free,
		&usb_raw_ioctl,
		&usb_raw_read,
		&usb_raw_write,
		NULL,
		NULL,
		NULL,
		NULL
	};

	return &hooks;
}

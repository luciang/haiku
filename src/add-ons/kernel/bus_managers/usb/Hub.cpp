/*
 * Copyright 2003-2006, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Niels S. Reedijk
 */


#include "usb_p.h"

#include <stdio.h>

#include <algorithm>


Hub::Hub(Object *parent, int8 hubAddress, uint8 hubPort,
	usb_device_descriptor &desc, int8 deviceAddress, usb_speed speed,
	bool isRootHub)
	:	Device(parent, hubAddress, hubPort, desc, deviceAddress, speed,
			isRootHub),
		fInterruptPipe(NULL)
{
	TRACE("creating hub\n");

	memset(&fHubDescriptor, 0, sizeof(fHubDescriptor));
	for (int32 i = 0; i < USB_MAX_PORT_COUNT; i++)
		fChildren[i] = NULL;

	if (!fInitOK) {
		TRACE_ERROR("device failed to initialize\n");
		return;
	}

	// Set to false again for the hub init.
	fInitOK = false;

	if (fDeviceDescriptor.device_class != 9) {
		TRACE_ERROR("wrong class! bailing out\n");
		return;
	}

	TRACE("getting hub descriptor...\n");
	size_t actualLength;
	status_t status = GetDescriptor(USB_DESCRIPTOR_HUB, 0, 0,
		(void *)&fHubDescriptor, sizeof(usb_hub_descriptor), &actualLength);

	// we need at least 8 bytes
	if (status < B_OK || actualLength < 8) {
		TRACE_ERROR("error getting hub descriptor\n");
		return;
	}

	TRACE("hub descriptor (%ld bytes):\n", actualLength);
	TRACE("\tlength:..............%d\n", fHubDescriptor.length);
	TRACE("\tdescriptor_type:.....0x%02x\n", fHubDescriptor.descriptor_type);
	TRACE("\tnum_ports:...........%d\n", fHubDescriptor.num_ports);
	TRACE("\tcharacteristics:.....0x%04x\n", fHubDescriptor.characteristics);
	TRACE("\tpower_on_to_power_g:.%d\n", fHubDescriptor.power_on_to_power_good);
	TRACE("\tdevice_removeable:...0x%02x\n", fHubDescriptor.device_removeable);
	TRACE("\tpower_control_mask:..0x%02x\n", fHubDescriptor.power_control_mask);

	if (fHubDescriptor.num_ports > USB_MAX_PORT_COUNT) {
		TRACE_ALWAYS("hub supports more ports than we do (%d vs. %d)\n",
			fHubDescriptor.num_ports, USB_MAX_PORT_COUNT);
		fHubDescriptor.num_ports = USB_MAX_PORT_COUNT;
	}

	usb_interface_list *list = Configuration()->interface;
	Object *object = GetStack()->GetObject(list->active->endpoint[0].handle);
	if (object && (object->Type() & USB_OBJECT_INTERRUPT_PIPE) != 0) {
		fInterruptPipe = (InterruptPipe *)object;
		fInterruptPipe->QueueInterrupt(fInterruptStatus,
			sizeof(fInterruptStatus), InterruptCallback, this);
	} else {
		TRACE_ALWAYS("no interrupt pipe found\n");
	}

	// Wait some time before powering up the ports
	if (!isRootHub)
		snooze(USB_DELAY_HUB_POWER_UP);

	// Enable port power on all ports
	for (int32 i = 0; i < fHubDescriptor.num_ports; i++) {
		status = DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_SET_FEATURE, PORT_POWER, i + 1, 0, NULL, 0, NULL);

		if (status < B_OK)
			TRACE_ERROR("power up failed on port %ld\n", i);
	}

	// Wait for power to stabilize
	snooze(fHubDescriptor.power_on_to_power_good * 2000);

	fInitOK = true;
	TRACE("initialised ok\n");
}


Hub::~Hub()
{
	delete fInterruptPipe;
}


status_t
Hub::Changed(change_item **changeList, bool added)
{
	status_t result = Device::Changed(changeList, added);
	if (added || result < B_OK)
		return result;

	for (int32 i = 0; i < fHubDescriptor.num_ports; i++) {
		if (fChildren[i] == NULL)
			continue;

		fChildren[i]->Changed(changeList, false);
		fChildren[i] = NULL;
	}

	return B_OK;
}


status_t
Hub::UpdatePortStatus(uint8 index)
{
	// get the current port status
	size_t actualLength = 0;
	status_t result = DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_IN,
		USB_REQUEST_GET_STATUS, 0, index + 1, 4, (void *)&fPortStatus[index],
		4, &actualLength);

	if (result < B_OK || actualLength < 4) {
		TRACE_ERROR("error updating port status\n");
		return B_ERROR;
	}

	return B_OK;
}


status_t
Hub::ResetPort(uint8 index)
{
	status_t result = DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
		USB_REQUEST_SET_FEATURE, PORT_RESET, index + 1, 0, NULL, 0, NULL);

	if (result < B_OK)
		return result;

	for (int32 i = 0; i < 10; i++) {
		snooze(USB_DELAY_PORT_RESET);

		result = UpdatePortStatus(index);
		if (result < B_OK)
			return result;

		if (fPortStatus[index].change & PORT_STATUS_RESET) {
			// reset is done
			break;
		}
	}

	if ((fPortStatus[index].change & PORT_STATUS_RESET) == 0) {
		TRACE_ERROR("port %d won't reset\n", index);
		return B_ERROR;
	}

	// clear the reset change
	result = DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
		USB_REQUEST_CLEAR_FEATURE, C_PORT_RESET, index + 1, 0, NULL, 0, NULL);
	if (result < B_OK)
		return result;

	// wait for reset recovery
	snooze(USB_DELAY_PORT_RESET_RECOVERY);
	TRACE("port %d was reset successfully\n", index);
	return B_OK;
}


status_t
Hub::DisablePort(uint8 index)
{
	return DefaultPipe()->SendRequest(USB_REQTYPE_CLASS
		| USB_REQTYPE_OTHER_OUT, USB_REQUEST_CLEAR_FEATURE, PORT_ENABLE,
		index + 1, 0, NULL, 0, NULL);
}



void
Hub::Explore(change_item **changeList)
{
	for (int32 i = 0; i < fHubDescriptor.num_ports; i++) {
		status_t result = UpdatePortStatus(i);
		if (result < B_OK)
			continue;

#ifdef TRACE_USB
		if (fPortStatus[i].change) {
			TRACE("port %ld: status: 0x%04x; change: 0x%04x\n", i,
				fPortStatus[i].status, fPortStatus[i].change);
			TRACE("device at port %ld: %p (%ld)\n", i, fChildren[i],
				fChildren[i] != NULL ? fChildren[i]->USBID() : 0);
		}
#endif

		if (fPortStatus[i].change & PORT_STATUS_CONNECTION) {
			// clear status change
			DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
				USB_REQUEST_CLEAR_FEATURE, C_PORT_CONNECTION, i + 1,
				0, NULL, 0, NULL);

			if (fPortStatus[i].status & PORT_STATUS_CONNECTION) {
				// new device attached!
				TRACE_ALWAYS("port %ld: new device connected\n", i);

				int32 retry = 2;
				while (retry--) {
					// wait some time for the device to power up
					snooze(USB_DELAY_DEVICE_POWER_UP);

					// reset the port, this will also enable it
					result = ResetPort(i);
					if (result < B_OK) {
						TRACE_ERROR("resetting port %ld failed\n", i);
						break;
					}

					result = UpdatePortStatus(i);
					if (result < B_OK)
						break;

					if ((fPortStatus[i].status & PORT_STATUS_CONNECTION) == 0) {
						// device has vanished after reset, ignore
						TRACE("device disappeared on reset\n");
						break;
					}

					if (fChildren[i] != NULL) {
						TRACE_ERROR("new device on a port that is already in "
							"use\n");
						fChildren[i]->Changed(changeList, false);
						fChildren[i] = NULL;
					}

					usb_speed speed = USB_SPEED_FULLSPEED;
					if (fPortStatus[i].status & PORT_STATUS_LOW_SPEED)
						speed = USB_SPEED_LOWSPEED;
					if (fPortStatus[i].status & PORT_STATUS_HIGH_SPEED)
						speed = USB_SPEED_HIGHSPEED;

					// either let the device inherit our addresses (if we are
					// already potentially using a transaction translator) or
					// set ourselfs as the hub when we might become the
					// transaction translator for the device.
					int8 hubAddress = HubAddress();
					uint8 hubPort = HubPort();
					if (Speed() == USB_SPEED_HIGHSPEED) {
						hubAddress = DeviceAddress();
						hubPort = i + 1;
					}

					Device *newDevice = GetBusManager()->AllocateDevice(this,
						hubAddress, hubPort, speed);

					if (newDevice) {
						newDevice->Changed(changeList, true);
						fChildren[i] = newDevice;
						break;
					} else {
						// the device failed to setup correctly, disable the
						// port so that the device doesn't get in the way of
						// future addressing.
						DisablePort(i);
					}
				}
			} else {
				// Device removed...
				TRACE_ALWAYS("port %ld: device removed\n", i);
				if (fChildren[i] != NULL) {
					TRACE("removing device %p\n", fChildren[i]);
					fChildren[i]->Changed(changeList, false);
					fChildren[i] = NULL;
				}
			}
		}

		// other port changes we do not really handle, report and clear them
		if (fPortStatus[i].change & PORT_STATUS_ENABLE) {
			TRACE_ALWAYS("port %ld %sabled\n", i, (fPortStatus[i].status & PORT_STATUS_ENABLE) ? "en" : "dis");
			DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
				USB_REQUEST_CLEAR_FEATURE, C_PORT_ENABLE, i + 1,
				0, NULL, 0, NULL);
		}

		if (fPortStatus[i].change & PORT_STATUS_SUSPEND) {
			TRACE_ALWAYS("port %ld is %ssuspended\n", i, (fPortStatus[i].status & PORT_STATUS_SUSPEND) ? "" : "not ");
			DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
				USB_REQUEST_CLEAR_FEATURE, C_PORT_SUSPEND, i + 1,
				0, NULL, 0, NULL);
		}

		if (fPortStatus[i].change & PORT_STATUS_OVER_CURRENT) {
			TRACE_ALWAYS("port %ld is %sin an over current state\n", i, (fPortStatus[i].status & PORT_STATUS_OVER_CURRENT) ? "" : "not ");
			DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
				USB_REQUEST_CLEAR_FEATURE, C_PORT_OVER_CURRENT, i + 1,
				0, NULL, 0, NULL);
		}

		if (fPortStatus[i].change & PORT_STATUS_RESET) {
			TRACE_ALWAYS("port %ld was reset\n", i);
			DefaultPipe()->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
				USB_REQUEST_CLEAR_FEATURE, C_PORT_RESET, i + 1,
				0, NULL, 0, NULL);
		}
	}

	// explore down the tree if we have hubs connected
	for (int32 i = 0; i < fHubDescriptor.num_ports; i++) {
		if (!fChildren[i] || (fChildren[i]->Type() & USB_OBJECT_HUB) == 0)
			continue;

		((Hub *)fChildren[i])->Explore(changeList);
	}
}


void
Hub::InterruptCallback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	TRACE_STATIC((Hub *)cookie, "interrupt callback!\n");
}


status_t
Hub::GetDescriptor(uint8 descriptorType, uint8 index, uint16 languageID,
	void *data, size_t dataLength, size_t *actualLength)
{
	return DefaultPipe()->SendRequest(
		USB_REQTYPE_DEVICE_IN | USB_REQTYPE_CLASS,			// type
		USB_REQUEST_GET_DESCRIPTOR,							// request
		(descriptorType << 8) | index,						// value
		languageID,											// index
		dataLength,											// length
		data,												// buffer
		dataLength,											// buffer length
		actualLength);										// actual length
}


status_t
Hub::ReportDevice(usb_support_descriptor *supportDescriptors,
	uint32 supportDescriptorCount, const usb_notify_hooks *hooks,
	usb_driver_cookie **cookies, bool added, bool recursive)
{
	status_t result = B_UNSUPPORTED;

	if (added) {
		// Report hub before children when adding devices
		TRACE("reporting hub before children\n");
		result = Device::ReportDevice(supportDescriptors,
			supportDescriptorCount, hooks, cookies, added, recursive);
	}

	for (int32 i = 0; recursive && i < fHubDescriptor.num_ports; i++) {
		if (!fChildren[i])
			continue;

		if (fChildren[i]->ReportDevice(supportDescriptors,
				supportDescriptorCount, hooks, cookies, added, true) == B_OK)
			result = B_OK;
	}

	if (!added) {
		// Report hub after children when removing devices
		TRACE("reporting hub after children\n");
		if (Device::ReportDevice(supportDescriptors, supportDescriptorCount,
				hooks, cookies, added, recursive) == B_OK)
			result = B_OK;
	}

	return result;
}


status_t
Hub::BuildDeviceName(char *string, uint32 *index, size_t bufferSize,
	Device *device)
{
	status_t result = Device::BuildDeviceName(string, index, bufferSize, device);
	if (result < B_OK) {
		// recursion to parent failed, we're at the root(hub)
		if (*index < bufferSize) {
			int32 managerIndex = GetStack()->IndexOfBusManager(GetBusManager());
			size_t totalBytes = snprintf(string + *index, bufferSize - *index,
				"%ld", managerIndex);
			*index += std::min(totalBytes, bufferSize - *index - 1);
		}
	}

	if (!device) {
		// no device was specified - report the hub
		if (*index < bufferSize) {
			size_t totalBytes = snprintf(string + *index, bufferSize - *index,
				"/hub");
			*index += std::min(totalBytes, bufferSize - *index - 1);
		}
	} else {
		// find out where the requested device sitts
		for (int32 i = 0; i < fHubDescriptor.num_ports; i++) {
			if (fChildren[i] == device) {
				if (*index < bufferSize) {
					size_t totalBytes = snprintf(string + *index,
						bufferSize - *index, "/%ld", i);
					*index += std::min(totalBytes, bufferSize - *index - 1);
				}
				break;
			}
		}
	}

	return B_OK;
}

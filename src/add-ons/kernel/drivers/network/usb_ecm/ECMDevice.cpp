/*
	Driver for USB Ethernet Control Model devices
	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
	Distributed under the terms of the MIT license.
*/
#include <ether_driver.h>
#include <net/if_media.h>
#include <string.h>
#include <stdlib.h>

#include "BeOSCompatibility.h"
#include "ECMDevice.h"
#include "Driver.h"

ECMDevice::ECMDevice(usb_device device)
	:	fStatus(B_ERROR),
		fOpen(false),
		fRemoved(false),
		fDevice(device),
		fControlInterfaceIndex(0),
		fDataInterfaceIndex(0),
		fMACAddressIndex(0),
		fMaxSegmentSize(0),
		fNotifyEndpoint(0),
		fReadEndpoint(0),
		fWriteEndpoint(0),
		fNotifyReadSem(-1),
		fNotifyWriteSem(-1),
		fNotifyBuffer(NULL),
		fNotifyBufferLength(0),
		fLinkStateChangeSem(-1),
		fHasConnection(false),
		fDownstreamSpeed(0),
		fUpstreamSpeed(0)
{
	const usb_device_descriptor *deviceDescriptor
		= gUSBModule->get_device_descriptor(device);
	const usb_configuration_info *config
		= gUSBModule->get_nth_configuration(device, 0);

	if (deviceDescriptor == NULL || config == NULL) {
		TRACE_ALWAYS("failed to get basic device info\n");
		return;
	}

	TRACE_ALWAYS("creating device: vendor: 0x%04x; device: 0x%04x\n",
		deviceDescriptor->vendor_id, deviceDescriptor->product_id);

	uint8 controlIndex = 0;
	uint8 dataIndex = 0;
	bool foundUnionDescriptor = false;
	bool foundEthernetDescriptor = false;
	for (size_t i = 0; i < config->interface_count
		&& (!foundUnionDescriptor || !foundEthernetDescriptor); i++) {
		usb_interface_info *interface = config->interface[i].active;
		usb_interface_descriptor *descriptor = interface->descr;
		if (descriptor->interface_class == USB_INTERFACE_CLASS_CDC
			&& descriptor->interface_subclass == USB_INTERFACE_SUBCLASS_ECM
			&& interface->generic_count > 0) {
			// try to find and interpret the union and ethernet functional
			// descriptors
			foundUnionDescriptor = foundEthernetDescriptor = false;
			for (size_t j = 0; j < interface->generic_count; j++) {
				usb_generic_descriptor *generic = &interface->generic[j]->generic;
				if (generic->length >= 5
					&& generic->data[0] == FUNCTIONAL_SUBTYPE_UNION) {
					controlIndex = generic->data[1];
					dataIndex = generic->data[2];
					foundUnionDescriptor = true;
				} else if (generic->length >= sizeof(ethernet_functional_descriptor)
					&& generic->data[0] == FUNCTIONAL_SUBTYPE_ETHERNET) {
					ethernet_functional_descriptor *ethernet
						= (ethernet_functional_descriptor *)generic->data;
					fMACAddressIndex = ethernet->mac_address_index;
					fMaxSegmentSize = ethernet->max_segment_size;
					foundEthernetDescriptor = true;
				}

				if (foundUnionDescriptor && foundEthernetDescriptor)
					break;
			}
		}
	}

	if (!foundUnionDescriptor) {
		TRACE_ALWAYS("did not find a union descriptor\n");
		return;
	}

	if (!foundEthernetDescriptor) {
		TRACE_ALWAYS("did not find an ethernet descriptor\n");
		return;
	}

	if (_ReadMACAddress() != B_OK) {
		TRACE_ALWAYS("failed to read mac address\n");
		return;
	}

	if (controlIndex >= config->interface_count) {
		TRACE_ALWAYS("control interface index invalid\n");
		return;
	}

	// check that the indicated control interface fits our needs
	usb_interface_info *interface = config->interface[controlIndex].active;
	usb_interface_descriptor *descriptor = interface->descr;
	if ((descriptor->interface_class != USB_INTERFACE_CLASS_CDC
		|| descriptor->interface_subclass != USB_INTERFACE_SUBCLASS_ECM)
		|| interface->endpoint_count == 0) {
		TRACE_ALWAYS("control interface invalid\n");
		return;
	}

	fControlInterfaceIndex = controlIndex;
	fNotifyEndpoint = interface->endpoint[0].handle;

	// setup notify buffer and try to schedule a notification transfer
	fNotifyBufferLength = 64;
	fNotifyBuffer = (uint8 *)malloc(fNotifyBufferLength);
	if (fNotifyBuffer == NULL) {
		TRACE_ALWAYS("out of memory for notify buffer allocation\n");
		return;
	}

	if (gUSBModule->queue_interrupt(fNotifyEndpoint, fNotifyBuffer,
		fNotifyBufferLength, _NotifyCallback, this) != B_OK) {
		// we cannot use notifications - hardcode to active connection
		fHasConnection = true;
		fDownstreamSpeed = 1000 * 1000 * 10; // 10Mbps
		fUpstreamSpeed = 1000 * 1000 * 10; // 10Mbps
	}

	if (dataIndex >= config->interface_count) {
		TRACE_ALWAYS("data interface index invalid\n");
		return;
	}

	// check that the indicated data interface fits our needs
	if (config->interface[dataIndex].alt_count < 2) {
		TRACE_ALWAYS("data interface does not provide two alternate interfaces\n");
		return;
	}

	// alternate 0 is the disabled, endpoint-less default interface
	interface = &config->interface[dataIndex].alt[1];
	descriptor = interface->descr;
	if (descriptor->interface_class != USB_INTERFACE_CLASS_CDC_DATA
		|| interface->endpoint_count < 2) {
		TRACE_ALWAYS("data interface invalid\n");
		return;
	}

	fDataInterfaceIndex = dataIndex;
	fNotifyReadSem = create_sem(0, DRIVER_NAME"_notify_read");
	if (fNotifyReadSem < B_OK) {
		TRACE_ALWAYS("failed to create read notify sem\n");
		return;
	}

	fNotifyWriteSem = create_sem(0, DRIVER_NAME"_notify_write");
	if (fNotifyWriteSem < B_OK) {
		TRACE_ALWAYS("failed to create write notify sem\n");
		return;
	}

	fStatus = B_OK;
}


ECMDevice::~ECMDevice()
{
	if (fNotifyReadSem >= B_OK)
		delete_sem(fNotifyReadSem);
	if (fNotifyWriteSem >= B_OK)
		delete_sem(fNotifyWriteSem);

	gUSBModule->cancel_queued_transfers(fNotifyEndpoint);
	free(fNotifyBuffer);
}


status_t
ECMDevice::Open(uint32 flags)
{
	if (fOpen)
		return B_BUSY;

	// reset the device by switching the data interface to the disabled first
	// interface and then enable it by setting the second actual data interface
	const usb_configuration_info *config
		= gUSBModule->get_nth_configuration(fDevice, 0);

	gUSBModule->set_alt_interface(fDevice,
		&config->interface[fDataInterfaceIndex].alt[0]);

	// update to the changed config
	config = gUSBModule->get_nth_configuration(fDevice, 0);
	gUSBModule->set_alt_interface(fDevice,
		&config->interface[fDataInterfaceIndex].alt[1]);

	// update again
	config = gUSBModule->get_nth_configuration(fDevice, 0);
	usb_interface_info *interface = config->interface[fDataInterfaceIndex].active;
	if (interface->endpoint_count < 2) {
		TRACE_ALWAYS("setting the data alternate interface failed\n");
		return B_ERROR;
	}

	if (!(interface->endpoint[0].descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN))
		fWriteEndpoint = interface->endpoint[0].handle;
	else
		fReadEndpoint = interface->endpoint[0].handle;

	if (interface->endpoint[1].descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
		fReadEndpoint = interface->endpoint[1].handle;
	else
		fWriteEndpoint = interface->endpoint[1].handle;

	if (fReadEndpoint == 0 || fWriteEndpoint == 0) {
		TRACE_ALWAYS("no read and write endpoints found\n");
		return B_ERROR;
	}

	// the device should now be ready
	fOpen = true;
	return B_OK;
}


status_t
ECMDevice::Close()
{
	if (fRemoved) {
		fOpen = false;
		return B_OK;
	}

	gUSBModule->cancel_queued_transfers(fReadEndpoint);
	gUSBModule->cancel_queued_transfers(fWriteEndpoint);

	// put the device into non-connected mode again by switching the data
	// interface to the disabled alternate
	const usb_configuration_info *config
		= gUSBModule->get_nth_configuration(fDevice, 0);

	gUSBModule->set_alt_interface(fDevice,
		&config->interface[fDataInterfaceIndex].alt[0]);

	fOpen = false;
	return B_OK;
}


status_t
ECMDevice::Free()
{
	return B_OK;
}


status_t
ECMDevice::Read(uint8 *buffer, size_t *numBytes)
{
	if (fRemoved) {
		*numBytes = 0;
		return B_ERROR;
	}

	status_t result = gUSBModule->queue_bulk(fReadEndpoint, buffer, *numBytes,
		_ReadCallback, this);
	if (result != B_OK) {
		*numBytes = 0;
		return result;
	}

	result = acquire_sem_etc(fNotifyReadSem, 1, B_CAN_INTERRUPT, 0);
	if (result < B_OK) {
		*numBytes = 0;
		return result;
	}

	if (fStatusRead != B_OK && fStatusRead != B_CANCELED) {
		TRACE_ALWAYS("device status error 0x%08lx\n", fStatusRead);
		result = gUSBModule->clear_feature(fReadEndpoint,
			USB_FEATURE_ENDPOINT_HALT);
		if (result != B_OK) {
			TRACE_ALWAYS("failed to clear halt state\n");
			*numBytes = 0;
			return result;
		}
	}

	*numBytes = fActualLengthRead;
	return B_OK;
}


status_t
ECMDevice::Write(const uint8 *buffer, size_t *numBytes)
{
	if (fRemoved) {
		*numBytes = 0;
		return B_ERROR;
	}

	status_t result = gUSBModule->queue_bulk(fWriteEndpoint, (uint8 *)buffer,
		*numBytes, _WriteCallback, this);
	if (result != B_OK) {
		*numBytes = 0;
		return result;
	}

	result = acquire_sem_etc(fNotifyWriteSem, 1, B_CAN_INTERRUPT, 0);
	if (result < B_OK) {
		*numBytes = 0;
		return result;
	}

	if (fStatusWrite != B_OK && fStatusWrite != B_CANCELED) {
		TRACE_ALWAYS("device status error 0x%08lx\n", fStatusWrite);
		result = gUSBModule->clear_feature(fWriteEndpoint,
			USB_FEATURE_ENDPOINT_HALT);
		if (result != B_OK) {
			TRACE_ALWAYS("failed to clear halt state\n");
			*numBytes = 0;
			return result;
		}
	}

	*numBytes = fActualLengthWrite;
	return B_OK;
}


status_t
ECMDevice::Control(uint32 op, void *buffer, size_t length)
{
	switch (op) {
		case ETHER_INIT:
			return B_OK;

		case ETHER_GETADDR:
			memcpy(buffer, &fMACAddress, sizeof(fMACAddress));
			return B_OK;

		case ETHER_GETFRAMESIZE:
			*(uint32 *)buffer = fMaxSegmentSize;
			return B_OK;

#if HAIKU_TARGET_PLATFORM_HAIKU
		case ETHER_SET_LINK_STATE_SEM:
			fLinkStateChangeSem = *(sem_id *)buffer;
			return B_OK;

		case ETHER_GET_LINK_STATE:
		{
			ether_link_state *state = (ether_link_state *)buffer;
			state->media = IFM_ETHER | IFM_FULL_DUPLEX
				| (fHasConnection ? IFM_ACTIVE : 0);
			state->quality = 1000;
			state->speed = fDownstreamSpeed / 1000;
			return B_OK;
		}
#endif

		default:
			TRACE_ALWAYS("unsupported ioctl %lu\n", op);
	}

	return B_DEV_INVALID_IOCTL;
}


void
ECMDevice::Removed()
{
	fRemoved = true;
	fHasConnection = false;
	fDownstreamSpeed = fUpstreamSpeed = 0;

	if (fLinkStateChangeSem >= B_OK)
		release_sem_etc(fLinkStateChangeSem, 1, B_DO_NOT_RESCHEDULE);
}


status_t
ECMDevice::_ReadMACAddress()
{
	if (fMACAddressIndex == 0)
		return B_BAD_VALUE;

	size_t actualLength = 0;
	size_t macStringLength = 26;
	uint8 macString[macStringLength];
	status_t result = gUSBModule->get_descriptor(fDevice, USB_DESCRIPTOR_STRING,
		fMACAddressIndex, 0, macString, macStringLength, &actualLength);
	if (result != B_OK)
		return result;

	if (actualLength != macStringLength) {
		TRACE_ALWAYS("did not retrieve full mac address\n");
		return B_ERROR;
	}

	char macPart[3];
	macPart[2] = 0;
	for (int32 i = 0; i < 6; i++) {
		macPart[0] = macString[2 + i * 4 + 0];
		macPart[1] = macString[2 + i * 4 + 2];
		fMACAddress[i] = strtol(macPart, NULL, 16);
	}

	TRACE_ALWAYS("read mac address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		fMACAddress[0], fMACAddress[1], fMACAddress[2], fMACAddress[3],
		fMACAddress[4], fMACAddress[5]);
	return B_OK;
}


void
ECMDevice::_ReadCallback(void *cookie, int32 status, void *data,
	uint32 actualLength)
{
	ECMDevice *device = (ECMDevice *)cookie;
	device->fActualLengthRead = actualLength;
	device->fStatusRead = status;
	release_sem_etc(device->fNotifyReadSem, 1, B_DO_NOT_RESCHEDULE);
}


void
ECMDevice::_WriteCallback(void *cookie, int32 status, void *data,
	uint32 actualLength)
{
	ECMDevice *device = (ECMDevice *)cookie;
	device->fActualLengthWrite = actualLength;
	device->fStatusWrite = status;
	release_sem_etc(device->fNotifyWriteSem, 1, B_DO_NOT_RESCHEDULE);
}


void
ECMDevice::_NotifyCallback(void *cookie, int32 status, void *data,
	uint32 actualLength)
{
	ECMDevice *device = (ECMDevice *)cookie;
	if (status == B_CANCELED || device->fRemoved)
		return;

	if (status == B_OK && actualLength >= sizeof(cdc_notification)) {
		bool linkStateChange = false;
		cdc_notification *notification
			= (cdc_notification *)device->fNotifyBuffer;

		switch (notification->notification_code) {
			case CDC_NOTIFY_NETWORK_CONNECTION:
				TRACE("connection state change to %d\n", notification->value);
				device->fHasConnection = notification->value > 0;
				linkStateChange = true;
				break;

			case CDC_NOTIFY_CONNECTION_SPEED_CHANGE:
			{
				if (notification->data_length < sizeof(cdc_connection_speed)
					|| actualLength < sizeof(cdc_notification)
					+ sizeof(cdc_connection_speed)) {
					TRACE_ALWAYS("not enough data in connection speed change\n");
					break;
				}

				cdc_connection_speed *speed;
				speed = (cdc_connection_speed *)&notification->data[0];
				device->fUpstreamSpeed = speed->upstream_speed;
				device->fDownstreamSpeed = speed->downstream_speed;
				device->fHasConnection = true;
				TRACE("connection speed change to %ld/%ld\n",
					speed->downstream_speed, speed->upstream_speed);
				linkStateChange = true;
				break;
			}

			default:
				TRACE_ALWAYS("unsupported notification 0x%02x\n",
					notification->notification_code);
				break;
		}

		if (linkStateChange && device->fLinkStateChangeSem >= B_OK)
			release_sem_etc(device->fLinkStateChangeSem, 1, B_DO_NOT_RESCHEDULE);
	}

	if (status != B_OK) {
		TRACE_ALWAYS("device status error 0x%08lx\n", status);
		if (gUSBModule->clear_feature(device->fNotifyEndpoint,
			USB_FEATURE_ENDPOINT_HALT) != B_OK)
			TRACE_ALWAYS("failed to clear halt state\n");
	}

	// schedule next notification buffer
	gUSBModule->queue_interrupt(device->fNotifyEndpoint, device->fNotifyBuffer,
		device->fNotifyBufferLength, _NotifyCallback, device);
}

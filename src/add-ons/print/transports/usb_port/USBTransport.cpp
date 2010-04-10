/*
 * Copyright 2001-2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Originally written based on Parallel port addon by Michael Pfeiffer,
 * changes by Andreas Benzler, Philippe Houdoin
 * Rewritten to use the USBKit by Ithamar R. Adema.
 *   (Using code from usb_printer.cpp by Michael Lotz)
 *
 * Authors:
 *		Ithamar R. Adema, <ithamar.adema@team-embedded.nl>
 *		Michael Pfeiffer,
 *		Andreas Benzler,
 *		Philippe Houdoin,
 */

#include "PrintTransportAddOn.h"

#include <USBKit.h>
#include <String.h>

#include <HashString.h>
#include <HashMap.h>

#define PRINTER_INTERFACE_CLASS		0x07
#define PRINTER_INTERFACE_SUBCLASS	0x01

// printer interface types
#define PIT_UNIDIRECTIONAL		0x01
#define PIT_BIDIRECTIONAL		0x02
#define PIT_1284_4_COMPATIBLE		0x03
#define PIT_VENDOR_SPECIFIC		0xff

class USBPrinter
{
public:
	USBPrinter(const BString& id, const BString& name,
		const BUSBInterface *intf, const BUSBEndpoint* in, const BUSBEndpoint* out);

	ssize_t Write(const void *buf, size_t size);
	ssize_t Read(void *buf, size_t size);

	const BUSBInterface	*fInterface;
	const BUSBEndpoint	*fOut;
	const BUSBEndpoint	*fIn;
	BString				fName;
	BString				fID;
};


class USBPrinterRoster : public BUSBRoster {
public:
	USBPrinterRoster();

	status_t DeviceAdded(BUSBDevice* dev);
	void DeviceRemoved(BUSBDevice* dev);

	USBPrinter *Printer(const BString& key);

	status_t ListPrinters(BMessage *msg);
private:
	typedef HashMap<HashString,USBPrinter*> PrinterMap;
	PrinterMap fPrinters;
};


class USBTransport : public BDataIO 
{	
public:
	USBTransport(BDirectory *printer, BMessage *msg);
	~USBTransport();

	status_t InitCheck() { return fPrinter ? B_OK : B_ERROR; };

	ssize_t Read(void *buffer, size_t size);
	ssize_t Write(const void *buffer, size_t size);

private:
	USBPrinter *fPrinter;
};


// Set transport_features so we stay loaded
uint32 transport_features = B_TRANSPORT_IS_HOTPLUG;

USBPrinterRoster gUSBPrinterRoster;

USBPrinterRoster::USBPrinterRoster()
{
	Start();
}

USBPrinter *USBPrinterRoster::Printer(const BString& key)
{
	if (fPrinters.ContainsKey(key.String()))
		return fPrinters.Get(key.String());

	return NULL;
}

status_t USBPrinterRoster::DeviceAdded(BUSBDevice* dev)
{
	const BUSBConfiguration *cfg = dev->ActiveConfiguration();
	const BUSBEndpoint *in = NULL, *out = NULL;
	const BUSBInterface *printer = NULL;

	// Try to find a working printer interface in this device
	if (cfg) {
		for (uint32 idx=0; printer == NULL && idx < cfg->CountInterfaces(); idx++) {
			const BUSBInterface *intf=cfg->InterfaceAt(idx);
			if (intf->Class() == PRINTER_INTERFACE_CLASS &&
				intf->Subclass() == PRINTER_INTERFACE_SUBCLASS &&
				(intf->Protocol() == PIT_UNIDIRECTIONAL ||
				 intf->Protocol() == PIT_BIDIRECTIONAL ||
				 intf->Protocol() == PIT_1284_4_COMPATIBLE)) {
				// Found a usable Printer interface!
				for (uint32 eptidx=0; eptidx < intf->CountEndpoints(); eptidx++) {
					const BUSBEndpoint *ept = intf->EndpointAt(eptidx);
					if (!ept->IsBulk())
						continue;

					if (ept->IsInput())
						in = ept;
					else if (ept->IsOutput())
						out = ept;

					if (!in || !out)
						continue;

					printer = intf;
					break;
				}
			}
		}
	}

	if (printer != NULL) {
		// We found a working printer interface, lets determine a unique ID
		//  for it now, and a user identification for display in the Printers
		//  preference.
		BString port_id = dev->SerialNumberString();
		if (!port_id.Length()) {
			// No persistent unique ID available, use the vendor/product
			//   ID for now. This will be unique as long as no two similar
			//   devices are attached.
			port_id << dev->VendorID() << "/" << dev->ProductID();
		}

		BString port_name = dev->ManufacturerString();
		if (port_name.Length())
			port_name << " ";
		port_name << dev->ProductString();

		//TODO: Do we want to use usb.ids to find proper name if strings
		//        are not supplied by USB?

		fPrinters.Put(port_id.String(), new USBPrinter(port_id, port_name,
			printer, in, out));
	}

	return B_OK;
}

void USBPrinterRoster::DeviceRemoved(BUSBDevice* dev)
{
	PrinterMap::Iterator iterator = fPrinters.GetIterator();
	while (iterator.HasNext()) {
		const PrinterMap::Entry& entry = iterator.Next();
		// If the device is in the list, remove it
		if (entry.value->fInterface->Device() == dev) {
			fPrinters.Remove(entry.key);
			delete entry.value;
			break;
		}
	}
}


status_t USBPrinterRoster::ListPrinters(BMessage* msg)
{
	PrinterMap::Iterator iterator = fPrinters.GetIterator();
	while (iterator.HasNext()) {
		const PrinterMap::Entry& entry = iterator.Next();
		msg->AddString("port_id", entry.value->fID);
		msg->AddString("port_name", entry.value->fName);
	}

	return B_OK;
}


USBPrinter::USBPrinter(const BString& id, const BString& name,
	const BUSBInterface *intf, const BUSBEndpoint* in, const BUSBEndpoint* out)
	: fInterface(intf), fOut(out), fIn(in), fName(name), fID(id)
{
}


//TODO: see usb_printer.cpp for error handling during read/write!
ssize_t USBPrinter::Write(const void *buf, size_t size)
{
	if (!buf || size <= 0)
		return B_BAD_VALUE;

	// NOTE: we can safely cast below as we're sending data _out_
	return fOut->BulkTransfer((void*)buf, size);
}


ssize_t USBPrinter::Read(void *buf, size_t size)
{
	if (!buf || size <= 0)
		return B_BAD_VALUE;

	return fIn->BulkTransfer(buf, size);
}


// Implementation of transport add-on interface

BDataIO * 
instantiate_transport(BDirectory *printer, BMessage *msg) 
{
	USBTransport * transport = new USBTransport(printer, msg);
	if (transport->InitCheck() == B_OK)
		return transport;
	
	delete transport; 
	return NULL;
}

// List detected printers
status_t list_transport_ports(BMessage* msg)
{
	return gUSBPrinterRoster.ListPrinters(msg);
}

// Implementation of USBTransport
USBTransport::USBTransport(BDirectory *printer, BMessage *msg) 
	: fPrinter(NULL)
{
	BString key;

	if (printer->ReadAttrString("transport_address", &key) < 0)
		return;

	fPrinter = gUSBPrinterRoster.Printer(key.String());
	if (!fPrinter)
		return;

	// If caller doesn't care...
	if (!msg)
		return;

	// Fill up the message
	msg->what = 'okok';
}


USBTransport::~USBTransport()
{
}


ssize_t
USBTransport::Read(void *buffer, size_t size)
{
	return fPrinter ? fPrinter->Read(buffer, size) : B_ERROR;
}


ssize_t
USBTransport::Write(const void *buffer, size_t size)
{
	return fPrinter ? fPrinter->Write(buffer, size) : B_ERROR;
}

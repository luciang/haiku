/*
 * Originally released under the Be Sample Code License.
 * Copyright 2000, Be Incorporated. All rights reserved.
 *
 * Modified for Haiku by François Revol and Michael Lotz.
 * Copyright 2007-2008, Haiku Inc. All rights reserved.
 */

#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <String.h>
#include <USBKit.h>
#include <stdio.h>


static void
DumpInterface(const BUSBInterface *interface)
{
	if (!interface)
		return;

	printf("                Class .............. 0x%02x\n",
		interface->Class());
	printf("                Subclass ........... 0x%02x\n",
		interface->Subclass());
	printf("                Protocol ........... 0x%02x\n",
		interface->Protocol());
	printf("                Interface String ... \"%s\"\n",
		interface->InterfaceString());

	for (uint32 i = 0; i < interface->CountEndpoints(); i++) {
		const BUSBEndpoint *endpoint = interface->EndpointAt(i);
		if (!endpoint)
			continue;

		printf("                [Endpoint %lu]\n", i);
		printf("                    MaxPacketSize .... %d\n",
			endpoint->MaxPacketSize());
		printf("                    Interval ......... %d\n",
			endpoint->Interval());

		if (endpoint->IsControl())
			printf("                    Type ............. Control\n");
		else if (endpoint->IsBulk())
			printf("                    Type ............. Bulk\n");
		else if (endpoint->IsIsochronous())
			printf("                    Type ............. Isochronous\n");
		else if (endpoint->IsInterrupt())
			printf("                    Type ............. Interrupt\n");

		if(endpoint->IsInput())
			printf("                    Direction ........ Input\n");
		else
			printf("                    Direction ........ Output\n");
	}

	char buffer[256];
	usb_descriptor *generic = (usb_descriptor *)buffer;
	for (uint32 i = 0; interface->OtherDescriptorAt(i, generic, 256) == B_OK; i++) {
		printf("                [Descriptor %lu]\n", i);
		printf("                    Type ............. 0x%02x\n",
			generic->generic.descriptor_type);
		printf("                    Data ............. ");
		// the length includes the length and descriptor_type field
		for(int32 j = 0; j < generic->generic.length - 2; j++)
			printf("%02x ", generic->generic.data[j]);
		printf("\n");
	}
}


static void
DumpConfiguration(const BUSBConfiguration *configuration)
{
	if (!configuration)
		return;

	printf("        Configuration String . \"%s\"\n",
		configuration->ConfigurationString());
	for (uint32 i = 0; i < configuration->CountInterfaces(); i++) {
		printf("        [Interface %lu]\n", i);
		const BUSBInterface *interface = configuration->InterfaceAt(i);

		for (uint32 j = 0; j < interface->CountAlternates(); j++) {
			const BUSBInterface *alternate = interface->AlternateAt(j);
			printf("            [Alternate %lu%s]\n", j,
				j == interface->AlternateIndex() ? " active" : "");
			DumpInterface(alternate);
		}
	}
}


static void
DumpInfo(BUSBDevice &device, bool verbose)
{
	if (!verbose) {
		printf("%04x:%04x /dev/bus/usb%s \"%s\" \"%s\" ver. %04x\n",
			device.VendorID(), device.ProductID(), device.Location(),
			device.ManufacturerString(), device.ProductString(),
			device.Version());
		return;
	}

	printf("[Device /dev/bus/usb%s]\n", device.Location());
	printf("    Class .................. 0x%02x\n", device.Class());
	printf("    Subclass ............... 0x%02x\n", device.Subclass());
	printf("    Protocol ............... 0x%02x\n", device.Protocol());
	printf("    Max Endpoint 0 Packet .. %d\n", device.MaxEndpoint0PacketSize());
	printf("    USB Version ............ 0x%04x\n", device.USBVersion());
	printf("    Vendor ID .............. 0x%04x\n", device.VendorID());
	printf("    Product ID ............. 0x%04x\n", device.ProductID());
	printf("    Product Version ........ 0x%04x\n", device.Version());
	printf("    Manufacturer String .... \"%s\"\n", device.ManufacturerString());
	printf("    Product String ......... \"%s\"\n", device.ProductString());
	printf("    Serial Number .......... \"%s\"\n", device.SerialNumberString());

	for (uint32 i = 0; i < device.CountConfigurations(); i++) {
		printf("    [Configuration %lu]\n", i);
		DumpConfiguration(device.ConfigurationAt(i));
	}
}


class DumpRoster : public BUSBRoster {
public:
					DumpRoster(bool verbose)
						:	fVerbose(verbose)
					{
					}

virtual	status_t	DeviceAdded(BUSBDevice *device)
					{
						DumpInfo(*device, fVerbose);
						return B_OK;
					}

virtual	void		DeviceRemoved(BUSBDevice *device)
					{
					}

private:
		bool		fVerbose;
};


int
main(int argc, char *argv[])
{
	bool verbose = false;
	BString devname = "";
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (argv[i][1] == 'v')
				verbose = true;
			else {
				printf("Usage: listusb [-v] [device]\n\n");
				printf("-v: Show more detailed information including interfaces, configurations, etc.\n\n");
				printf("If a device is not specified, all devices found on the bus will be listed\n");
				return 1;
			}
		} else {
			devname = argv[i];
		}
	}

	if (devname.Length() > 0) {
		BUSBDevice device(devname.String());
		if (device.InitCheck() < B_OK) {
			printf("Cannot open USB device: %s\n", devname.String());
			return 1;
		} else {
				DumpInfo(device, verbose);
				return 0;
		}
	} else {
		DumpRoster roster(verbose);
		roster.Start();
		roster.Stop();
	} 

	return 0;
}

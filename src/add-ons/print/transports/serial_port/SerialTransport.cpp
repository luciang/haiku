/*****************************************************************************/
// Serial port transport add-on.
//
// Author
//   Michael Pfeiffer
//
// This application and all source files used in its construction, except 
// where noted, are licensed under the MIT License, and have been written 
// and are:
//
// Copyright (c) 2001-2003 OpenBeOS Project
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to whom the 
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included 
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
/*****************************************************************************/

#include <unistd.h>
#include <stdio.h>

#include <StorageKit.h>
#include <SupportKit.h>

#include "PrintTransportAddOn.h"

class SerialTransport : public BDataIO {
public:
	SerialTransport(BDirectory* printer, BMessage* msg);
	~SerialTransport();

	status_t InitCheck() { return fFile > -1 ? B_OK : B_ERROR; }

	ssize_t Read(void* buffer, size_t size);
	ssize_t Write(const void* buffer, size_t size);

private:
	int fFile;
};

// Impelmentation of SerialTransport
SerialTransport::SerialTransport(BDirectory* printer, BMessage* msg) 
	: fFile(-1)
{
	char address[80];
	char device[B_PATH_NAME_LENGTH];
	bool bidirectional = true;

	unsigned int size = printer->ReadAttr("transport_address", B_STRING_TYPE, 0, address, sizeof(address));
	if (size <= 0 || size >= sizeof(address)) return;
	address[size] = 0; // make sure string is 0-terminated
		
	strcat(strcpy(device, "/dev/ports/"), address);
	fFile = open(device, O_RDWR | O_EXCL | O_BINARY, 0);
	if (fFile < 0) {
		// Try unidirectional access mode
		bidirectional = false;
		fFile = open(device, O_WRONLY | O_EXCL | O_BINARY, 0);
	}

	if (fFile < 0)
		return;

	if (! msg)
		// Caller don't care about transport init message output content...
		return;

	msg->what = 'okok';
	msg->AddBool("bidirectional", bidirectional);
	msg->AddString("_serial/DeviceName", device);

}

SerialTransport::~SerialTransport()
{
	if (InitCheck() == B_OK)
		close(fFile);
}

ssize_t SerialTransport::Read(void* buffer, size_t size)
{
	return read(fFile, buffer, size);
}

ssize_t SerialTransport::Write(const void* buffer, size_t size)
{
	return write(fFile, buffer, size);
}

BDataIO* instantiate_transport(BDirectory* printer, BMessage* msg)
{
	SerialTransport* transport = new SerialTransport(printer, msg);
	if (transport->InitCheck() == B_OK)
		return transport;

	delete transport;
	return NULL;
}

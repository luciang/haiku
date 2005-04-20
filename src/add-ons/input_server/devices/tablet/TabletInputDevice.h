/*****************************************************************************/
// Tablet input server device addon 
// Adapted by Jerome Duval, written by Stefano Ceccherini 
//
// TabletInputDevice.h
//
// Copyright (c) 2005 Haiku Project
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
#ifndef __TABLETINPUTDEVICE_H
#define __TABLETINPUTDEVICE_H

#include <InputServerDevice.h>
#include <InterfaceDefs.h>
#include <List.h>
#include <stdio.h>

#define DEBUG 1

class TabletInputDevice : public BInputServerDevice {
public:
	TabletInputDevice();
	~TabletInputDevice();
	
	virtual status_t InitCheck();
	
	virtual status_t Start(const char *name, void *cookie);
	virtual status_t Stop(const char *name, void *cookie);
	
	virtual status_t Control(const char *name, void *cookie,
							 uint32 command, BMessage *message);
private:
	status_t HandleMonitor(BMessage *message);
	status_t InitFromSettings(void *cookie, uint32 opcode = 0);
	void RecursiveScan(const char *directory);
	
	status_t AddDevice(const char *path);
	status_t RemoveDevice(const char *path);
	
	static int32 DeviceWatcher(void *arg);
			
	BList fDevices;
#ifdef DEBUG
public:
	static FILE *sLogFile;
#endif
};

extern "C" BInputServerDevice *instantiate_input_device();

#endif


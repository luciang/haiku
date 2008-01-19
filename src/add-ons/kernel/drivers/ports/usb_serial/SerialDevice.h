/*
 * Copyright (c) 2007-2008 by Michael Lotz
 * Heavily based on the original usb_serial driver which is:
 *
 * Copyright (c) 2003 by Siarzhuk Zharski <imker@gmx.li>
 * Distributed under the terms of the MIT License.
 */
#ifndef _USB_DEVICE_H_
#define _USB_DEVICE_H_

#include "Driver.h"

class SerialDevice {
public:
								SerialDevice(usb_device device,
									uint16 vendorID, uint16 productID,
									const char *description);
virtual							~SerialDevice();

static	SerialDevice *			MakeDevice(usb_device device, uint16 vendorID,
									uint16 productID);

		status_t				Init();

		usb_device				Device() { return fDevice; };
		uint16					ProductID() { return fProductID; };
		uint16					VendorID() { return fVendorID; };
		const char *			Description() { return fDescription; };

		void					SetControlPipe(usb_pipe handle);
		usb_pipe				ControlPipe() { return fControlPipe; };

		void					SetReadPipe(usb_pipe handle);
		usb_pipe				ReadPipe() { return fReadPipe; };

		void					SetWritePipe(usb_pipe handle);
		usb_pipe				WritePipe() { return fWritePipe; }

		char *					ReadBuffer() { return fReadBuffer; };
		size_t					ReadBufferSize() { return fReadBufferSize; };

		char *					WriteBuffer() { return fWriteBuffer; };
		size_t					WriteBufferSize() { return fWriteBufferSize; };

		void					SetModes();
		bool					Service(struct tty *ptty, struct ddrover *ddr,
									uint flags);

		status_t				Open(uint32 flags);
		status_t				Read(char *buffer, size_t *numBytes);
		status_t				Write(const char *buffer, size_t *numBytes);
		status_t				Control(uint32 op, void *arg, size_t length);
		status_t				Select(uint8 event, uint32 ref, selectsync *sync);
		status_t				DeSelect(uint8 event, selectsync *sync);
		status_t				Close();
		status_t				Free();

		/* virtual interface to be overriden as necessary */
virtual	status_t				AddDevice(const usb_configuration_info *config);

virtual	status_t				ResetDevice();

virtual	status_t				SetLineCoding(usb_serial_line_coding *coding);
virtual	status_t				SetControlLineState(uint16 state);

virtual	void					OnRead(char **buffer, size_t *numBytes);
virtual	void					OnWrite(const char *buffer, size_t *numBytes);
virtual	void					OnClose();

private:
static	int32					DeviceThread(void *data);

static	void					ReadCallbackFunction(void *cookie,
									int32 status, void *data,
									uint32 actualLength);
static	void					WriteCallbackFunction(void *cookie,
									int32 status, void *data,
									uint32 actualLength);
static	void					InterruptCallbackFunction(void *cookie,
									int32 status, void *data,
									uint32 actualLength);

		usb_device				fDevice;		// USB device handle
		uint16					fVendorID;
		uint16					fProductID;
		const char *			fDescription;	// informational description

		/* communication pipes */
		usb_pipe				fControlPipe;
		usb_pipe				fReadPipe;
		usb_pipe				fWritePipe;

		/* line coding */
		usb_serial_line_coding	fLineCoding;

		/* data buffers */
		area_id					fBufferArea;
		char *					fReadBuffer;
		size_t					fReadBufferSize;
		char *					fWriteBuffer;
		size_t					fWriteBufferSize;
		char *					fInterruptBuffer;
		size_t					fInterruptBufferSize;

		/* variables used in callback functionality */
		size_t					fActualLengthRead;
		uint32					fStatusRead;
		size_t					fActualLengthWrite;
		uint32					fStatusWrite;
		size_t					fActualLengthInterrupt;
		uint32					fStatusInterrupt;

		/* semaphores used in callbacks */
		sem_id					fDoneRead;
		sem_id					fDoneWrite;

		uint16					fControlOut;
		bool					fInputStopped;
		struct ttyfile			fTTYFile;
		struct tty				fTTY;

		/* device thread management */
		thread_id				fDeviceThread;
		bool					fStopDeviceThread;

		/* device locks to ensure no concurent reads/writes */
		benaphore				fReadLock;
		benaphore				fWriteLock;
};

#endif // _USB_DEVICE_H_

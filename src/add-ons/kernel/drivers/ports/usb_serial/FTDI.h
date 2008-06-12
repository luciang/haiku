/*
 * Copyright (c) 2007-2008 by Michael Lotz
 * Heavily based on the original usb_serial driver which is:
 *
 * Copyright (c) 2003 by Siarzhuk Zharski <imker@gmx.li>
 * Distributed under the terms of the MIT License.
 */
#ifndef _USB_FTDI_H_
#define _USB_FTDI_H_

#include "SerialDevice.h"

/* supported vendor and product ids */
#define VENDOR_FTDI				0x0403
#define PRODUCT_FTDI_8U100AX	0x8372
#define PRODUCT_FTDI_8U232AM	0x6001

#define FTDI_BUFFER_SIZE		64


class FTDIDevice : public SerialDevice {
public:
								FTDIDevice(usb_device device, uint16 vendorID,
									uint16 productID, const char *description);

virtual	status_t				AddDevice(const usb_configuration_info *config);

virtual	status_t				ResetDevice();

virtual	status_t				SetLineCoding(usb_serial_line_coding *coding);
virtual	status_t				SetControlLineState(uint16 state);

virtual	void					OnRead(char **buffer, size_t *numBytes);
virtual	void					OnWrite(const char *buffer, size_t *numBytes, 
									size_t *packetBytes);

private:
		size_t					fHeaderLength;
		uint8					fStatusMSR;
		uint8					fStatusLSR;
};

#endif //_USB_FTDI_H_

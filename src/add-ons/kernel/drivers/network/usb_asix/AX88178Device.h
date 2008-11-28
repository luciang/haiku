/*
 *	ASIX AX88172/AX88772/AX88178 USB 2.0 Ethernet Driver.
 *	Copyright (c) 2008 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *	
 *	Heavily based on code of the 
 *	Driver for USB Ethernet Control Model devices
 *	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
 *	Distributed under the terms of the MIT license.
 *
 */

#ifndef _USB_AX88178_DEVICE_H_
#define _USB_AX88178_DEVICE_H_

#include "ASIXDevice.h"

class AX88178Device : public ASIXDevice {
public:
							AX88178Device(usb_device device, const char *description);
protected:
		status_t			InitDevice();
virtual	status_t			SetupDevice(bool deviceReplugged);
virtual	status_t			StartDevice();
virtual	status_t			OnNotify(uint32 actualLength);
virtual	status_t			GetLinkState(ether_link_state *state);		
};

#endif //_USB_AX88178_DEVICE_H_

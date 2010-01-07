/*
 *  Davicom 9601 USB 1.1 Ethernet Driver.
 *  Copyright 2009 Adrien Destugues <pulkomandy@gmail.com>
 *	Distributed under the terms of the MIT license.
 *
 *	Heavily based on code of : 
 *	ASIX AX88172/AX88772/AX88178 USB 2.0 Ethernet Driver.
 *	Copyright (c) 2008 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *	
 *	Driver for USB Ethernet Control Model devices
 *	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
 *	Distributed under the terms of the MIT license.
 *
 */

#ifndef _USB_DAVICOM_DRIVER_H_
#define _USB_DAVICOM_DRIVER_H_

#include <OS.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <USB3.h>
#include <ether_driver.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <util/kernel_cpp.h>

#define DRIVER_NAME	"usb_davicom"
#define MAX_DEVICES	8

const uint8 kInvalidRequest = 0xff;

const char* const kVersion = "ver.0.8.3";

extern usb_module_info *gUSBModule;

extern "C" {
status_t	usb_davicom_device_added(usb_device device, void **cookie);
status_t	usb_davicom_device_removed(void *cookie);

status_t	init_hardware();
void		uninit_driver();

const char **publish_devices();
device_hooks *find_device(const char *name);
}


#endif //_USB_DAVICOM_DRIVER_H_


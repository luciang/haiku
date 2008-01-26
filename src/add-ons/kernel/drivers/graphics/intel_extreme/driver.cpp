/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "driver.h"
#include "device.h"
#include "lock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <AGP.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <SupportDefs.h>

#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define MAX_CARDS 4

// list of supported devices
const struct supported_device {
	uint32		device_id;
	int32		type;
	const char	*name;
} kSupportedDevices[] = {
	{0x3577, INTEL_TYPE_83x, "i830GM"},
	{0x2562, INTEL_TYPE_83x, "i845G"},

	{0x2572, INTEL_TYPE_85x, "i865G"},
	{0x3582, INTEL_TYPE_85x, "i855G"},

	{0x2792, INTEL_TYPE_91x, "i910"},
	{0x258a, INTEL_TYPE_91x, "i915"},
	{0x2582, INTEL_TYPE_91x, "i915G"},
	{0x2592, INTEL_TYPE_91x, "i915GM"},
	{0x2772, INTEL_TYPE_945, "i945G"},
	{0x27a2, INTEL_TYPE_945, "i945GM"},
	{0x29a2, INTEL_TYPE_965, "i965G"},
	{0x29b2, INTEL_TYPE_G33, "G33G"},
	{0x29c2, INTEL_TYPE_G33, "Q35G"},
	{0x29d2, INTEL_TYPE_G33, "Q33G"},
};

int32 api_version = B_CUR_DRIVER_API_VERSION;

char *gDeviceNames[MAX_CARDS + 1];
intel_info *gDeviceInfo[MAX_CARDS];
pci_module_info *gPCI;
agp_module_info *gGART;
lock gLock;


static status_t
get_next_intel_extreme(int32 *_cookie, pci_info &info, uint32 &type)
{
	int32 index = *_cookie;

	// find devices

	for (; gPCI->get_nth_pci_info(index, &info) == B_OK; index++) {
		// check vendor
		if (info.vendor_id != VENDOR_ID_INTEL
			|| info.class_base != PCI_display
			|| info.class_sub != 0)
			continue;

		// check device
		for (uint32 i = 0; i < sizeof(kSupportedDevices)
				/ sizeof(kSupportedDevices[0]); i++) {
			if (info.device_id == kSupportedDevices[i].device_id) {
				type = i;
				*_cookie = index + 1;
				return B_OK;
			}
		}
	}

	return B_ENTRY_NOT_FOUND;
}


extern "C" const char **
publish_devices(void)
{
	TRACE((DEVICE_NAME ": publish_devices()\n"));
	return (const char **)gDeviceNames;
}


extern "C" status_t
init_hardware(void)
{
	TRACE((DEVICE_NAME ": init_hardware()\n"));

	status_t status = get_module(B_PCI_MODULE_NAME,(module_info **)&gPCI);
	if (status != B_OK) {
		TRACE((DEVICE_NAME ": pci module unavailable\n"));
		return status;
	}

	int32 cookie = 0;
	uint32 type;
	pci_info info;
	status = get_next_intel_extreme(&cookie, info, type);

	put_module(B_PCI_MODULE_NAME);
	return status;
}


extern "C" status_t
init_driver(void)
{
	TRACE((DEVICE_NAME ": init_driver()\n"));

	status_t status = get_module(B_PCI_MODULE_NAME, (module_info **)&gPCI);
	if (status != B_OK) {
		TRACE((DEVICE_NAME ": pci module unavailable\n"));
		return status;
	}

	status = init_lock(&gLock, "intel extreme ksync");
	if (status < B_OK) {
		put_module(B_PCI_MODULE_NAME);
		return status;
	}

	get_module(B_AGP_MODULE_NAME, (module_info **)&gGART);

	// find devices

	int32 found = 0;

	for (int32 cookie = 0; found < MAX_CARDS;) {
		pci_info *info = (pci_info *)malloc(sizeof(pci_info));
		if (info == NULL)
			break;

		uint32 type;
		status = get_next_intel_extreme(&cookie, *info, type);
		if (status < B_OK) {
			free(info);
			break;
		}

		// create device names & allocate device info structure

		char name[64];
		sprintf(name, "graphics/intel_extreme_%02x%02x%02x",
			 info->bus, info->device,
			 info->function);

		gDeviceNames[found] = strdup(name);
		if (gDeviceNames[found] == NULL)
			break;

		gDeviceInfo[found] = (intel_info *)malloc(sizeof(intel_info));
		if (gDeviceInfo[found] == NULL) {
			free(gDeviceNames[found]);
			break;
		}

		// initialize the structure for later use

		memset(gDeviceInfo[found], 0, sizeof(intel_info));
		gDeviceInfo[found]->init_status = B_NO_INIT;
		gDeviceInfo[found]->id = found;
		gDeviceInfo[found]->pci = info;
		gDeviceInfo[found]->registers = (uint8 *)info->u.h0.base_registers[0];
		gDeviceInfo[found]->device_identifier = kSupportedDevices[type].name;
		gDeviceInfo[found]->device_type = kSupportedDevices[type].type;

		dprintf(DEVICE_NAME ": (%ld) %s, revision = 0x%x\n", found,
			kSupportedDevices[type].name, info->revision);
		found++;
	}

	gDeviceNames[found] = NULL;

	if (found == 0) {
		uninit_lock(&gLock);
		if (gGART != NULL)
			put_module(B_AGP_MODULE_NAME);
		put_module(B_PCI_MODULE_NAME);
		return ENODEV;
	}

	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	TRACE((DEVICE_NAME ": uninit_driver()\n"));

	uninit_lock(&gLock);

	// free device related structures
	char *name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		free(gDeviceInfo[index]);
		free(name);
	}

	if (gGART != NULL)
		put_module(B_AGP_MODULE_NAME);
	put_module(B_PCI_MODULE_NAME);
}


extern "C" device_hooks *
find_device(const char *name)
{
	int index;

	TRACE((DEVICE_NAME ": find_device()\n"));

	for (index = 0; gDeviceNames[index] != NULL; index++) {
		if (!strcmp(name, gDeviceNames[index]))
			return &gDeviceHooks;
	}

	return NULL;
}

/*
extern "C" void
wake_driver(void)
{
	// for compatibility with Dano, only
}


extern "C" void
suspend_driver(void)
{
	// for compatibility with Dano, only
}
*/

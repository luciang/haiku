/*
 * Copyright 2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "driver.h"
#include "device.h"
#include "intel_extreme.h"
#include "utility.h"

#include <OS.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <graphic_driver.h>
#include <image.h>

#include <stdlib.h>
#include <string.h>


#define TRACE_DEVICE
#ifdef TRACE_DEVICE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


/* device hooks prototypes */

static status_t device_open(const char *name, uint32 flags, void **_cookie);
static status_t device_close(void *data);
static status_t device_free(void *data);
static status_t device_ioctl(void *data, uint32 opcode, void *buffer, size_t length);
static status_t device_read(void *data, off_t offset, void *buffer, size_t *length);
static status_t device_write(void *data, off_t offset, const void *buffer, size_t *length);


device_hooks gDeviceHooks = {
	device_open,
	device_close,
	device_free,
	device_ioctl,
	device_read,
	device_write,
	NULL,
	NULL,
	NULL,
	NULL
};


static status_t
check_device_info(struct intel_info *info)
{
	if (!info || info->cookie_magic != INTEL_COOKIE_MAGIC)
		return B_BAD_VALUE;

	return B_OK;
}


static int
getset_register(int argc, char **argv)
{
	if (argc < 2 || argc > 3) {
		kprintf("usage: %s <register> [set-to-value]\n", argv[0]);
		return 0;
	}

	uint32 reg = parse_expression(argv[1]);
	uint32 value = 0;
	bool set = argc == 3;
	if (set)
		value = parse_expression(argv[2]);

	kprintf("intel_extreme register %#lx\n", reg);

	intel_info &info = *gDeviceInfo[0];
	uint32 oldValue = read32(info.registers + reg);

	kprintf("  %svalue: %#lx (%lu)\n", set ? "old " : "", oldValue, oldValue);

	if (set) {
		write32(info.registers + reg, value);

		value = read32(info.registers + reg);
		kprintf("  new value: %#lx (%lu)\n", value, value);
	}

	return 0;
}


//	#pragma mark - Device Hooks


static status_t
device_open(const char *name, uint32 /*flags*/, void **_cookie)
{
	TRACE((DEVICE_NAME ": open(name = %s)\n", name));
	int32 id;

	// find accessed device
	{
		char *thisName;

		// search for device name
		for (id = 0; (thisName = gDeviceNames[id]) != NULL; id++) {
			if (!strcmp(name, thisName))
				break;
		}
		if (!thisName)
			return B_BAD_VALUE;
	}
	intel_info *info = gDeviceInfo[id];
	*_cookie = info;

	acquire_lock(&gLock);

	status_t status = B_OK;

	// TODO: the second open will succeed even though the first one failed!
	if (info->open_count++ == 0) {
		// this device has been opened for the first time, so
		// we allocate needed resources and initialize the structure
		status = intel_extreme_init(*info);
		if (status == B_OK) {
			add_debugger_command("ie_reg", getset_register,
				"dumps or sets the specified intel_extreme register");
		}
	}

	release_lock(&gLock);

	return status;
}


static status_t
device_close(void *data)
{
	TRACE((DEVICE_NAME ": close\n"));
	struct intel_info *info;

	if (check_device_info(info = (intel_info *)data) != B_OK)
		return B_BAD_VALUE;

	info->cookie_magic = INTEL_FREE_COOKIE_MAGIC;
	return B_OK;
}


static status_t
device_free(void *data)
{
	struct intel_info *info = (intel_info *)data;
	status_t retval = B_NO_ERROR;

	if (info == NULL || info->cookie_magic != INTEL_FREE_COOKIE_MAGIC)
		retval = B_BAD_VALUE;

	acquire_lock(&gLock);

	if (info->open_count-- == 1) {
		// release info structure
		info->cookie_magic = 0;
		remove_debugger_command("ie_reg", getset_register);
		intel_extreme_uninit(*info);
	}

	release_lock(&gLock);

	return retval;
}


static status_t
device_ioctl(void *data, uint32 op, void *buffer, size_t bufferLength)
{
	struct intel_info *info;

	if (check_device_info(info = (intel_info *)data) != B_OK)
		return B_BAD_VALUE;

	switch (op) {
		case B_GET_ACCELERANT_SIGNATURE:
			strcpy((char *)buffer, INTEL_ACCELERANT_NAME);
			TRACE((DEVICE_NAME ": accelerant: %s\n", INTEL_ACCELERANT_NAME));
			return B_OK;

		// needed to share data between kernel and accelerant		
		case INTEL_GET_PRIVATE_DATA:
		{
			intel_get_private_data *data = (intel_get_private_data *)buffer;

			if (data->magic == INTEL_PRIVATE_DATA_MAGIC) {
				data->shared_info_area = info->shared_area;
				return B_OK;
			}
			break;
		}

		// needed for cloning
		case INTEL_GET_DEVICE_NAME:
#ifdef __HAIKU__
			if (user_strlcpy((char *)buffer, gDeviceNames[info->id], B_PATH_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;
#else
			strncpy((char *)buffer, gDeviceNames[info->id], B_PATH_NAME_LENGTH);
			((char *)buffer)[B_PATH_NAME_LENGTH - 1] = '\0';
#endif
			return B_OK;

		// graphics mem manager
		case INTEL_ALLOCATE_GRAPHICS_MEMORY:
		{
			intel_allocate_graphics_memory allocMemory;
#ifdef __HAIKU__
			if (user_memcpy(&allocMemory, buffer, sizeof(intel_allocate_graphics_memory)) < B_OK)
				return B_BAD_ADDRESS;
#else
			memcpy(&allocMemory, buffer, sizeof(intel_allocate_graphics_memory));
#endif

			if (allocMemory.magic != INTEL_PRIVATE_DATA_MAGIC)
				return B_BAD_VALUE;

			status_t status = mem_alloc(info->memory_manager, allocMemory.size, info,
				&allocMemory.handle, &allocMemory.buffer_offset);
			if (status == B_OK) {
				// copy result
#ifdef __HAIKU__
				if (user_memcpy(buffer, &allocMemory, sizeof(intel_allocate_graphics_memory)) < B_OK)
					return B_BAD_ADDRESS;
#else
				memcpy(buffer, &allocMemory, sizeof(intel_allocate_graphics_memory));
#endif
			}
			return status;
		}

		case INTEL_FREE_GRAPHICS_MEMORY:
		{
			intel_free_graphics_memory freeMemory;
#ifdef __HAIKU__
			if (user_memcpy(&freeMemory, buffer, sizeof(intel_free_graphics_memory)) < B_OK)
				return B_BAD_ADDRESS;
#else
			memcpy(&freeMemory, buffer, sizeof(intel_free_graphics_memory));
#endif

			if (freeMemory.magic == INTEL_PRIVATE_DATA_MAGIC)
				return mem_free(info->memory_manager, freeMemory.handle, info);
			break;
		}

		default:
			TRACE((DEVICE_NAME ": ioctl() unknown message %ld (length = %ld)\n",
				op, bufferLength));
			break;
	}

	return B_DEV_INVALID_IOCTL;
}


static status_t
device_read(void */*data*/, off_t /*pos*/, void */*buffer*/, size_t *_length)
{
	*_length = 0;	
	return B_NOT_ALLOWED;
}


static status_t
device_write(void */*data*/, off_t /*pos*/, const void */*buffer*/, size_t *_length)
{
	*_length = 0;	
	return B_NOT_ALLOWED;
}


/*
 * Copyright 2009, Clemens Zeidler. All rights reserved.
 * Copyright 2006, Jérôme Duval. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "acpi_priv.h"
#include "acpi.h"


static status_t
acpi_install_notify_handler(acpi_device device,	uint32 handlerType,
	acpi_notify_handler handler, void *context)
{
	return install_notify_handler(device->handle, handlerType, handler,
		context);
}

static status_t
acpi_remove_notify_handler(acpi_device device, uint32 handlerType,
	acpi_notify_handler handler)
{
	return remove_notify_handler(device->handle, handlerType, handler);
}


static status_t
acpi_install_address_space_handler(acpi_device device, uint32 spaceId,
	acpi_adr_space_handler handler,	acpi_adr_space_setup setup,	void *data)
{
	return install_address_space_handler(device->handle, spaceId, handler,
		setup, data);
}

static status_t
acpi_remove_address_space_handler(acpi_device device, uint32 spaceId,
	acpi_adr_space_handler handler)
{
	return remove_address_space_handler(device->handle, spaceId, handler);
}
					

static uint32 
acpi_get_object_type(acpi_device device)
{
	return device->type;
}


static status_t 
acpi_get_object(acpi_device device, const char *path, acpi_object_type **return_value) 
{
	if (path) {
		char objname[255];
		snprintf(objname, sizeof(objname), "%s.%s", device->path, path);
		return get_object(objname, return_value);
	}
	return get_object(device->path, return_value);
}


static status_t 
acpi_evaluate_method(acpi_device device, const char *method,
	acpi_objects *args, acpi_data *returnValue) 
{
	return evaluate_method(device->handle, method, args, returnValue);
}


static status_t
acpi_device_init_driver(device_node *node, void **cookie)
{
	ACPI_HANDLE handle;
	const char *path;
	acpi_device_cookie *device;
	status_t status = B_OK;
	uint32 type;
	
	if (gDeviceManager->get_attr_uint32(node, ACPI_DEVICE_TYPE_ITEM, &type, false) != B_OK)
		return B_ERROR;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_PATH_ITEM, &path, false) != B_OK)
		return B_ERROR;
	
	device = malloc(sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;
	
	memset(device, 0, sizeof(*device));

	if (AcpiGetHandle(NULL, (ACPI_STRING)path, &handle) != AE_OK) {
		free(device);
		return B_ENTRY_NOT_FOUND;
	}

	device->handle = handle;
	device->path = strdup(path);
	device->type = type;
	device->node = node;

	snprintf(device->name, sizeof(device->name), "acpi_device %s", 
		path);
	
	*cookie = device;

	return status;
}


static void
acpi_device_uninit_driver(void *cookie)
{
	acpi_device_cookie *device = cookie;
	
	free(device->path);
	free(device);
}


static status_t
acpi_device_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
	}

	return B_BAD_VALUE;
}

					
acpi_device_module_info gACPIDeviceModule = {
	{
		{
			ACPI_DEVICE_MODULE_NAME,
			0,
			acpi_device_std_ops
		},

		NULL,		// supports device
		NULL,		// register device (our parent registered us)
		acpi_device_init_driver,
		acpi_device_uninit_driver,
		NULL,	// register child devices
		NULL,	// rescan devices
		NULL,	// device removed
	},
	acpi_install_notify_handler,
	acpi_remove_notify_handler,
	acpi_install_address_space_handler,
	acpi_remove_address_space_handler,
	acpi_get_object_type,
	acpi_get_object,
	acpi_evaluate_method,
};

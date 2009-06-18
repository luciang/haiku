/*-
 * Copyright (c) 2009 Clemens Zeidler
 * Copyright (c) 2003-2007 Nate Lawson
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "acpi_embedded_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ACPI.h>
#include <condition_variable.h>
#include <Errors.h>
#include <KernelExport.h>
#include <drivers/PCI.h>

#include "SmallResourceData.h"

#define ACPI_EC_DRIVER_NAME "drivers/power/acpi_embedded_controller/driver_v1"

#define ACPI_EC_DEVICE_NAME "drivers/power/acpi_embedded_controller/device_v1"

/* Base Namespace devices are published to */
#define ACPI_EC_BASENAME "power/embedded_controller/%d"

// name of pnp generator of path ids
#define ACPI_EC_PATHID_GENERATOR "embedded_controller/path_id"

device_manager_info *gDeviceManager = NULL;

pci_module_info *gPCIManager = NULL;


uint8
bus_space_read_1(int address)
{
	return gPCIManager->read_io_8(address);
}


void
bus_space_write_1(int address, uint8 v)
{
	gPCIManager->write_io_8(address, v);
}


status_t
acpi_GetInteger(acpi_device_module_info* acpi, acpi_device& acpiCookie,
	char* path, int* number)
{
	status_t status;
 	acpi_data buf;
	acpi_object_type object;
	buf.pointer = &object;
	buf.length = sizeof(acpi_object_type);
    /*
    * Assume that what we've been pointed at is an Integer object, or
    * a method that will return an Integer.
      */
	status = acpi->evaluate_method(acpiCookie, path, NULL, &buf);
    
    if (status == B_OK) {
         if (object.object_type == ACPI_TYPE_INTEGER)
             *number = object.data.integer;
         else
             status = B_ERROR;
     }
	return status;	
}


acpi_handle
acpi_GetReference(acpi_module_info* acpi, acpi_handle scope,
	acpi_object_type *obj)
{
    acpi_handle h;

    if (obj == NULL)
        return (NULL);

    switch (obj->object_type) {
    case ACPI_TYPE_LOCAL_REFERENCE:
    case ACPI_TYPE_ANY:
        h = obj->data.reference.handle;
        break;
    case ACPI_TYPE_STRING:
		/*
		* The String object usually contains a fully-qualified path, so
		* scope can be NULL.
		*
		* XXX This may not always be the case.
		*/
		if (acpi->get_handle(scope, obj->data.string.string, &h) != B_OK)
			h = NULL;
		break;
	default:
		h = NULL;
		break;
	}
 
    return (h);
}


int
acpi_PkgInt(acpi_object_type *res, int idx, int *dst)
{
    acpi_object_type         *obj;

    obj = &res->data.package.objects[idx];
    if (obj == NULL || obj->object_type != ACPI_TYPE_INTEGER)
        return (EINVAL);
    *dst = obj->data.integer;
 
    return (0);
}
   

int
acpi_PkgInt32(acpi_object_type *res, int idx, uint32 *dst)
{
    int			        tmp;
    int                 error;

    error = acpi_PkgInt(res, idx, &tmp);
    if (error == 0)
         *dst = (uint32)tmp;
 
     return (error);
}
    

static status_t
embedded_controller_open(void *initCookie, const char *path, int flags, void** cookie)
{
	acpi_ec_softc *device = (acpi_ec_softc*)initCookie;
	*cookie = device;

	return B_OK;
}


static status_t
embedded_controller_close(void* cookie)
{
	return B_OK;
}


static status_t
embedded_controller_read(void* _cookie, off_t position, void *buffer, size_t* numBytes)
{
	return B_IO_ERROR;
}


static status_t
embedded_controller_write(void* cookie, off_t position, const void* buffer, size_t* numBytes)
{
	return B_IO_ERROR;
}


status_t
embedded_controller_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	return B_ERROR;
}


static status_t
embedded_controller_free(void* cookie)
{
	return B_OK;
}


//	#pragma mark - driver module API

int32
acpi_get_type(device_node* dev)
{
	const char *bus;
	if (gDeviceManager->get_attr_string(dev, B_DEVICE_BUS, &bus, false))
		return -1;
	
	if (strcmp(bus, "acpi"))
		return -1;

	uint32 deviceType;
	if (gDeviceManager->get_attr_uint32(dev, ACPI_DEVICE_TYPE_ITEM,
			&deviceType, false) != B_OK)
		return -1;

	return deviceType;
}


static float
embedded_controller_support(device_node *dev)
{
    static char *ec_ids[] = { "PNP0C09", NULL };

	/* Check that this is a device. */
    if (acpi_get_type(dev) != ACPI_TYPE_DEVICE)
        return 0.;
	
    const char *name;
    if (gDeviceManager->get_attr_string(dev, ACPI_DEVICE_HID_ITEM, &name,
    	false) != B_OK || strcmp(name, ec_ids[0]))
    	return 0.0;		
   
	TRACE("supported device found %s\n", name);
    return 0.6;
}


static status_t
embedded_controller_register_device(device_node *node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ string: "ACPI embedded controller" }},
		{ NULL }
	};
	
	return gDeviceManager->register_node(node, ACPI_EC_DRIVER_NAME, attrs,
		NULL, NULL);
}


static status_t
embedded_controller_init_driver(device_node *dev, void **_driverCookie)
{
	TRACE("init driver\n");
	acpi_ec_softc  *sc;
	sc = (acpi_ec_softc*)malloc(sizeof(acpi_ec_softc));
    memset(sc, 0, sizeof(acpi_ec_softc));
	
	*_driverCookie = sc;
	sc->ec_dev = dev;
		
	sc->ec_condition_var.Init(NULL, "ec condition variable");
    
	device_node *parent;
	parent = gDeviceManager->get_parent_node(dev);
	gDeviceManager->get_driver(parent, (driver_module_info **)&sc->ec_acpi,
		(void **)&sc->ec_handle);
	gDeviceManager->put_node(parent);
	
	SmallResourceData resourceData(sc->ec_acpi, sc->ec_handle, "_CRS");
	if (resourceData.InitCheck() != B_OK) {
		TRACE("failed to read _CRS resource\n")	;
		return B_ERROR;
	}
	io_port portData;

	if (get_module(B_ACPI_MODULE_NAME, (module_info**)&sc->ec_acpi_module) != B_OK)
		return B_ERROR;
	
	// DPC module
	if (gDPC == NULL && get_module(B_DPC_MODULE_NAME,
			(module_info **)&gDPC) != B_OK) {
		dprintf("failed to get dpc module for os execution\n");
		return B_ERROR;
	}

	if (gDPCHandle == NULL) {
		if (gDPC->new_dpc_queue(&gDPCHandle, "acpi_task",
				B_NORMAL_PRIORITY) != B_OK) {
			dprintf("failed to create os execution queue\n");
			return B_ERROR;
		}
	}
	
    acpi_data buf;
	buf.pointer = NULL;
    buf.length = ACPI_ALLOCATE_BUFFER;
    
	
    /*
     * Read the unit ID to check for duplicate attach and the
     * global lock value to see if we should acquire it when
     * accessing the EC.
     */
     
    status_t status;
    status = acpi_GetInteger(sc->ec_acpi, sc->ec_handle, "_UID", &sc->ec_uid);
    if (status != B_OK)
        sc->ec_uid = 0;
    status = acpi_GetInteger(sc->ec_acpi, sc->ec_handle, "_GLK", &sc->ec_glk);
    if (status != B_OK)
        sc->ec_glk = 0;

    /*
     * Evaluate the _GPE method to find the GPE bit used by the EC to
     * signal status (SCI).  If it's a package, it contains a reference
     * and GPE bit, similar to _PRW.
     */
    status = sc->ec_acpi->evaluate_method(sc->ec_handle, "_GPE", NULL, &buf);
    if (status != B_OK) {
        TRACE("can't evaluate _GPE\n");
        goto error;
    }
    
    acpi_object_type* obj;
    obj = (acpi_object_type*)buf.pointer;
    if (obj == NULL)
        goto error;
	
	switch (obj->object_type) {
    case ACPI_TYPE_INTEGER:
        sc->ec_gpehandle = NULL;
        sc->ec_gpebit = obj->data.integer;
        break;
    case ACPI_TYPE_PACKAGE:
        if (!ACPI_PKG_VALID(obj, 2))
            goto error;
        sc->ec_gpehandle =
            acpi_GetReference(sc->ec_acpi_module, NULL,
            	&obj->data.package.objects[0]);
        if (sc->ec_gpehandle == NULL ||
            acpi_PkgInt32(obj, 1, (uint32*)&sc->ec_gpebit) != 0)
            goto error;
        break;
    default:
        TRACE("_GPE has invalid type %i\n", int(obj->object_type));
        goto error;
    }

	sc->ec_suspending = FALSE;
	
    /* Attach bus resources for data and command/status ports. */
    sc->ec_data_rid = 0;
    if (resourceData.ReadIOPort(&portData) != B_OK)
		goto error;
		
    sc->ec_data_pci_address = portData.minimumBase;

    sc->ec_csr_rid = 1; 
    if (resourceData.ReadIOPort(&portData) != B_OK)
		goto error;
	
	sc->ec_csr_pci_address = portData.minimumBase;
	
    /*
     * Install a handler for this EC's GPE bit.  We want edge-triggered
     * behavior.
     */
    TRACE("attaching GPE handler\n");
    status = sc->ec_acpi_module->install_gpe_handler(sc->ec_gpehandle,
    	sc->ec_gpebit, ACPI_GPE_EDGE_TRIGGERED, &EcGpeHandler, sc);
    if (status != B_OK) {
        TRACE("can't install ec GPE handler\n");
        goto error;
    }

    /*
     * Install address space handler
     */
    TRACE("attaching address space handler\n");
    status = sc->ec_acpi->install_address_space_handler(sc->ec_handle,
    	ACPI_ADR_SPACE_EC, &EcSpaceHandler, &EcSpaceSetup, sc);
    if (status != B_OK) {
        TRACE("can't install address space handler\n");
        goto error;
    }

    /* Enable runtime GPEs for the handler. */
    status = sc->ec_acpi_module->set_gpe_type(sc->ec_gpehandle, sc->ec_gpebit,
                            ACPI_GPE_TYPE_RUNTIME);
    if (status != B_OK) {
        TRACE("AcpiSetGpeType failed.\n");
        goto error;
    }
    status = sc->ec_acpi_module->enable_gpe(sc->ec_gpehandle, sc->ec_gpebit,
    	ACPI_NOT_ISR);
    if (status != B_OK) {
        TRACE("AcpiEnableGpe failed.\n");
        goto error;
    }

    return (0);

error:
	if (buf.pointer)
        free(buf.pointer);
    
    sc->ec_acpi_module->remove_gpe_handler(sc->ec_gpehandle, sc->ec_gpebit,
    	&EcGpeHandler);
    sc->ec_acpi->remove_address_space_handler(sc->ec_handle, ACPI_ADR_SPACE_EC,
        EcSpaceHandler);

    return (ENXIO);
}


static void
embedded_controller_uninit_driver(void *driverCookie)
{
	acpi_ec_softc* sc = (struct acpi_ec_softc *)driverCookie;
	free(sc);
	put_module(B_ACPI_MODULE_NAME);
	put_module(B_DPC_MODULE_NAME);
}


static status_t
embedded_controller_register_child_devices(void *_cookie)
{
	device_node *node = ((acpi_ec_softc*)_cookie)->ec_dev;

	int pathID = gDeviceManager->create_id(ACPI_EC_PATHID_GENERATOR);
	if (pathID < 0) {
		TRACE("register_child_device couldn't create a path_id\n");
		return B_ERROR;
	}

	char name[128];
	snprintf(name, sizeof(name), ACPI_EC_BASENAME, pathID);

	return gDeviceManager->publish_device(node, name, ACPI_EC_DEVICE_NAME);
}


static status_t
embedded_controller_init_device(void *driverCookie, void **cookie)
{
	return B_ERROR;
}


static void
embedded_controller_uninit_device(void *_cookie)
{
	acpi_ec_softc *device = (acpi_ec_softc*)_cookie;
	free(device);
}


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&gDeviceManager },
	{ B_PCI_MODULE_NAME, (module_info **)&gPCIManager},
	{}
};


driver_module_info embedded_controller_driver_module = {
	{
		ACPI_EC_DRIVER_NAME,
		0,
		NULL
	},

	embedded_controller_support,
	embedded_controller_register_device,
	embedded_controller_init_driver,
	embedded_controller_uninit_driver,
	embedded_controller_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};


struct device_module_info embedded_controller_device_module = {
	{
		ACPI_EC_DEVICE_NAME,
		0,
		NULL
	},

	embedded_controller_init_device,
	embedded_controller_uninit_device,
	NULL,

	embedded_controller_open,
	embedded_controller_close,
	embedded_controller_free,
	embedded_controller_read,
	embedded_controller_write,
	NULL,
	embedded_controller_control,

	NULL,
	NULL
};


module_info *modules[] = {
	(module_info *)&embedded_controller_driver_module,
	(module_info *)&embedded_controller_device_module,
	NULL
};


static void
EcGpeQueryHandler(void *context)
{
    struct acpi_ec_softc *sc = (struct acpi_ec_softc *)context;
    uint8 data;
    status_t status;
    char qxx[5];

    ASSERT(context != NULL);//, ("EcGpeQueryHandler called with NULL"));

    /* Serialize user access with EcSpaceHandler(). */
    status = EcLock(sc);
    if (status != B_OK) {
        TRACE("GpeQuery lock error.\n");
        return;
    }

    /*
     * Send a query command to the EC to find out which _Qxx call it
     * wants to make.  This command clears the SCI bit and also the
     * interrupt source since we are edge-triggered.  To prevent the GPE
     * that may arise from running the query from causing another query
     * to be queued, we clear the pending flag only after running it.
     */
    status = EcCommand(sc, EC_COMMAND_QUERY);
    sc->ec_sci_pend = FALSE;
    if (status != B_OK) {
        EcUnlock(sc);
        TRACE("GPE query failed.\n");
        return;
    }
    data = EC_GET_DATA(sc);

    /*
     * We have to unlock before running the _Qxx method below since that
     * method may attempt to read/write from EC address space, causing
     * recursive acquisition of the lock.
     */
    EcUnlock(sc);

    /* Ignore the value for "no outstanding event". (13.3.5) */
    TRACE("ec query ok,%s running _Q%02X\n", Data ? "" : " not", data);
    if (data == 0)
        return;

    /* Evaluate _Qxx to respond to the controller. */
    snprintf(qxx, sizeof(qxx), "_Q%02X", data);
    AcpiUtStrupr(qxx);
    status = sc->ec_acpi->evaluate_method(sc->ec_handle, qxx, NULL, NULL);
    if (status != B_OK) {
        TRACE("evaluation of query method %s failed\n", qxx);
    }
}

/*
 * The GPE handler is called when IBE/OBF or SCI events occur.  We are
 * called from an unknown lock context.
 */
static uint32
EcGpeHandler(void *context)
{
	struct acpi_ec_softc *sc = (acpi_ec_softc*)context;
    status_t status;
    EC_STATUS EcStatus;

    ASSERT(context != NULL);//, ("EcGpeHandler called with NULL"));
    TRACE("ec gpe handler start\n");

    /*
     * Notify EcWaitEvent() that the status register is now fresh.  If we
     * didn't do this, it wouldn't be possible to distinguish an old IBE
     * from a new one, for example when doing a write transaction (writing
     * address and then data values.)
     */
    atomic_add(&sc->ec_gencount, 1);
    sc->ec_condition_var.NotifyAll();
    /*
     * If the EC_SCI bit of the status register is set, queue a query handler.
     * It will run the query and _Qxx method later, under the lock.
     */
    EcStatus = EC_GET_CSR(sc);
    if ((EcStatus & EC_EVENT_SCI) && !sc->ec_sci_pend) {
        TRACE("ec gpe queueing query handler\n");
        status = AcpiOsExecute(OSL_GPE_HANDLER, EcGpeQueryHandler, context);
        if (status == B_OK)
            sc->ec_sci_pend = TRUE;
        else
            dprintf("EcGpeHandler: queuing GPE query handler failed\n");
    }
    return (0);
}


static status_t
EcSpaceSetup(acpi_handle region, uint32 function, void *context,
             void **regionContext)
{
    /*
     * If deactivating a region, always set the output to NULL.  Otherwise,
     * just pass the context through.
     */
    if (function == ACPI_REGION_DEACTIVATE)
        *regionContext = NULL;
    else
        *regionContext = context;

    return B_OK;
}


static status_t
EcSpaceHandler(uint32 function, acpi_physical_address address, uint32 width,
	int *value, void *context, void *regionContext)
{
	TRACE("enter EcSpaceHandler\n");
    struct acpi_ec_softc *sc = (struct acpi_ec_softc *)context;
    status_t status;
    uint8 ecAddr, ecData;
    uint32 i;

    if (width % 8 != 0 || value == NULL || context == NULL)
		return B_BAD_VALUE;
    if (address + (width / 8) - 1 > 0xFF)
		return B_BAD_ADDRESS;

    if (function == ACPI_READ)
        *value = 0;
    ecAddr = address;
    status = B_ERROR;

    /*
     * If booting, check if we need to run the query handler.  If so, we
     * we call it directly here since our thread taskq is not active yet.
     */
	/*if (cold || rebooting || sc->ec_suspending) {
        if ((EC_GET_CSR(sc) & EC_EVENT_SCI)) {
            //CTR0(KTR_ACPI, "ec running gpe handler directly");
            EcGpeQueryHandler(sc);
        }
    }*/

    /* Serialize with EcGpeQueryHandler() at transaction granularity. */
    status = EcLock(sc);
    if (status != B_OK)
        return (status);

    /* Perform the transaction(s), based on width. */
    for (i = 0; i < width; i += 8, ecAddr++) {
        switch (function) {
        case ACPI_READ:
            status = EcRead(sc, ecAddr, &ecData);
            if (status == B_OK)
                *value |= ((int)ecData) << i;
            break;
        case ACPI_WRITE:
            ecData = (uint8)((*value) >> i);
            status = EcWrite(sc, ecAddr, &ecData);
            break;
        default:
            TRACE("invalid EcSpaceHandler function\n");
            status = B_BAD_VALUE;
            break;
        }
        if (status != B_OK)
            break;
    }

	EcUnlock(sc);
    return (status);
}

static status_t
EcCheckStatus(struct acpi_ec_softc *sc, const char *msg, EC_EVENT event)
{
    status_t status = B_ERROR;
    EC_STATUS ec_status;

    ec_status = EC_GET_CSR(sc);
    if (sc->ec_burstactive && !(ec_status & EC_FLAG_BURST_MODE)) {
        TRACE("ec burst disabled in waitevent (%s)\n", msg);
        sc->ec_burstactive = false;
    }
    if (EVENT_READY(event, ec_status)) {
        TRACE("ec %s wait ready, status %#x\n", msg, ec_status);
        status = B_OK;
    }
    return (status);
}

static status_t
EcWaitEvent(struct acpi_ec_softc *sc, EC_EVENT event, int32 gen_count)
{
    status_t status = B_ERROR;
    int32 count, i;
	
	// int need_poll = cold || rebooting || ec_polled_mode || sc->ec_suspending;
    int need_poll = ec_polled_mode || sc->ec_suspending;
    
    /*
     * The main CPU should be much faster than the EC.  So the status should
     * be "not ready" when we start waiting.  But if the main CPU is really
     * slow, it's possible we see the current "ready" response.  Since that
     * can't be distinguished from the previous response in polled mode,
     * this is a potential issue.  We really should have interrupts enabled
     * during boot so there is no ambiguity in polled mode.
     *
     * If this occurs, we add an additional delay before actually entering
     * the status checking loop, hopefully to allow the EC to go to work
     * and produce a non-stale status.
     */
    if (need_poll) {
        static int      once;

        if (EcCheckStatus(sc, "pre-check", event) == B_OK) {
            if (!once) {
                TRACE("warning: EC done before starting event wait\n");
                once = 1;
            }
            spin(10);
        }
    }

    /* Wait for event by polling or GPE (interrupt). */
    if (need_poll) {
        count = (ec_timeout * 1000) / EC_POLL_DELAY;
        if (count == 0)
            count = 1;
        for (i = 0; i < count; i++) {
            status = EcCheckStatus(sc, "poll", event);
            if (status == B_OK)
                break;
            spin(EC_POLL_DELAY);
        }
    } else {
        // ToDo: scale timeout for slow cpu see BSD code...
        count = ec_timeout;
		
		/*
         * Wait for the GPE to signal the status changed, checking the
         * status register each time we get one.  It's possible to get a
         * GPE for an event we're not interested in here (i.e., SCI for
         * EC query).
         */
        for (i = 0; i < count; i++) {
            if (gen_count != sc->ec_gencount) {
                /*
                 * Record new generation count.  It's possible the GPE was
                 * just to notify us that a query is needed and we need to
                 * wait for a second GPE to signal the completion of the
                 * event we are actually waiting for.
                 */
                gen_count = sc->ec_gencount;
                status = EcCheckStatus(sc, "sleep", event);
                if (status == B_OK)
                    break;
            }
            sc->ec_condition_var.Wait();
        }

        /*
         * We finished waiting for the GPE and it never arrived.  Try to
         * read the register once and trust whatever value we got.  This is
         * the best we can do at this point.  Then, force polled mode on
         * since this system doesn't appear to generate GPEs.
         */
        if (status != B_OK) {
            status = EcCheckStatus(sc, "sleep_end", event);
            TRACE("wait timed out (%sresponse), forcing polled mode\n",
                Status == B_OK ? "" : "no ");
            ec_polled_mode = TRUE;
        }
    }
	if (status != B_OK)
		TRACE("error: ec wait timed out\n");

    return (status);
}

static status_t
EcCommand(struct acpi_ec_softc *sc, EC_COMMAND cmd)
{
    status_t status;
    EC_EVENT event;
    EC_STATUS ec_status;
    u_int gen_count;

	/* Don't use burst mode if user disabled it. */
    if (!ec_burst_mode && cmd == EC_COMMAND_BURST_ENABLE)
        return (B_ERROR);

    /* Decide what to wait for based on command type. */
    switch (cmd) {
    case EC_COMMAND_READ:
    case EC_COMMAND_WRITE:
    case EC_COMMAND_BURST_DISABLE:
        event = EC_EVENT_INPUT_BUFFER_EMPTY;
        break;
    case EC_COMMAND_QUERY:
    case EC_COMMAND_BURST_ENABLE:
        event = EC_EVENT_OUTPUT_BUFFER_FULL;
        break;
    default:
        TRACE("EcCommand: invalid command %#x\n", cmd);
        return (B_BAD_VALUE);
    }

    /* Run the command and wait for the chosen event. */
    TRACE("ec running command %#x\n", cmd);
    gen_count = sc->ec_gencount;
    EC_SET_CSR(sc, cmd);
    status = EcWaitEvent(sc, event, gen_count);
    if (status == B_OK) {
        /* If we succeeded, burst flag should now be present. */
        if (cmd == EC_COMMAND_BURST_ENABLE) {
            ec_status = EC_GET_CSR(sc);
            if ((ec_status & EC_FLAG_BURST_MODE) == 0)
                status = B_ERROR;
        }
    } else
        TRACE("EcCommand: no response to %#x\n", cmd);

    return (status);
}

static status_t
EcRead(struct acpi_ec_softc *sc, uint8 address, uint8 *readData)
{
    status_t status;
    uint8 data;
    u_int gen_count;

	TRACE("ec read from %#x\n", Address);

    /* If we can't start burst mode, continue anyway. */
    status = EcCommand(sc, EC_COMMAND_BURST_ENABLE);
    if (status == B_OK) {
        data = EC_GET_DATA(sc);
        if (data == EC_BURST_ACK) {
			TRACE("ec burst enabled\n");
            sc->ec_burstactive = TRUE;
        }
    }

    status = EcCommand(sc, EC_COMMAND_READ);
    if (status != B_OK)
        return (status);

    gen_count = sc->ec_gencount;
	
	EC_SET_DATA(sc, address);
    status = EcWaitEvent(sc, EC_EVENT_OUTPUT_BUFFER_FULL, gen_count);
    if (status != B_OK) {
        TRACE("EcRead: failed waiting to get data\n");
        return (status);
    }
	*readData = EC_GET_DATA(sc);
	
	if (sc->ec_burstactive) {
        sc->ec_burstactive = FALSE;
        status = EcCommand(sc, EC_COMMAND_BURST_DISABLE);
        if (status != B_OK)
            return (status);
        TRACE("ec disabled burst ok\n");
    }

    return (B_OK);
}

static status_t
EcWrite(struct acpi_ec_softc *sc, uint8 address, uint8 *writeData)
{
    status_t status;
    uint8 data;
    u_int gen_count;

    /* If we can't start burst mode, continue anyway. */
    status = EcCommand(sc, EC_COMMAND_BURST_ENABLE);
    if (status == B_OK) {
        data = EC_GET_DATA(sc);
        if (data == EC_BURST_ACK) {
            TRACE("ec burst enabled\n");
            sc->ec_burstactive = TRUE;
        }
    }

    status = EcCommand(sc, EC_COMMAND_WRITE);
    if (status != B_OK)
        return (status);

    gen_count = sc->ec_gencount;
    EC_SET_DATA(sc, address);
    status = EcWaitEvent(sc, EC_EVENT_INPUT_BUFFER_EMPTY, gen_count);
    if (status != B_OK) {
        TRACE("EcRead: failed waiting for sent address\n");
        return (status);
    }

    gen_count = sc->ec_gencount;
    EC_SET_DATA(sc, *writeData);
    status = EcWaitEvent(sc, EC_EVENT_INPUT_BUFFER_EMPTY, gen_count);
    if (status != B_OK) {
        TRACE("EcWrite: failed waiting for sent data\n");
        return (status);
    }

    if (sc->ec_burstactive) {
        sc->ec_burstactive = FALSE;
        status = EcCommand(sc, EC_COMMAND_BURST_DISABLE);
        if (status != B_OK)
            return (status);
        TRACE("ec disabled burst ok");
    }

    return (B_OK);
}


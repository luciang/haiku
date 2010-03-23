/*
 * Copyright (c) 2007-2008 by Michael Lotz
 * Heavily based on the original usb_serial driver which is:
 *
 * Copyright (c) 2003 by Siarzhuk Zharski <imker@gmx.li>
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <Drivers.h>
#include <image.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

#include "Driver.h"
#include "SerialDevice.h"

int32 api_version = B_CUR_DRIVER_API_VERSION;
static const char *sDeviceBaseName = DEVFS_BASE;
SerialDevice *gSerialDevices[DEVICES_COUNT];
char *gDeviceNames[DEVICES_COUNT + 1];
config_manager_for_driver_module_info *gConfigManagerModule = NULL;
isa_module_info *gISAModule = NULL;
pci_module_info *gPCIModule = NULL;
//XXX: ifndef __HAIKU__
tty_module_info_v1_bone *gTTYModule = NULL;
struct ddomain gSerialDomain;
sem_id gDriverLock = -1;
bool gHandleISA = false;

// 24 MHz clock
static const uint32 sDefaultRates[] = {
		0,		//B0
		2304,	//B50
		1536,	//B75
		1047,	//B110
		857,	//B134
		768,	//B150
		512,	//B200
		384,	//B300
		192,	//B600
		0,		//B1200
		0,		//B1800
		48,		//B2400
		24,		//B4800
		12,		//B9600
		6,		//B19200
		3,		//B38400
		2,		//B57600
		1,		//B115200
		0,		//B230400
		4, //460800 !? B31250!
		0, //921600 !?
};

// 8MHz clock on serial3 and 4 on the BeBox
#if 0
static const uint32 sBeBoxRates[] = {
		0,		//B0
		//... TODO
};
#endif

static const struct serial_support_descriptor sSupportedDevices[] = {

#ifdef HANDLE_ISA_COM
	// ISA devices
	{ B_ISA_BUS, "Generic 16550 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16550 } },
#endif
	// PCI devices

	// vendor/device matches first

/*
	{ B_PCI_BUS, "OxfordSemi 16950 Serial Port", sDefaultRates, NULL, { 32, 32, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16950,
		0x1415, 0x9501 } },
*/
	// generic fallback matches
	/*
	{ B_PCI_BUS, "Generic XT Serial Port", NULL },
	  { PCI_INVAL, PCI_INVAL, PCI_simple_communications,
		PCI_serial, PCI_serial_xt } },
		
	{ B_PCI_BUS, "Generic 16450 Serial Port", NULL },
	  { PCI_INVAL, PCI_INVAL, PCI_simple_communications,
		PCI_serial, PCI_serial_16450 } },
		
	*/
	{ B_PCI_BUS, "Generic 16550 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16550,
		PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, "Generic 16650 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16650,
		PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, "Generic 16750 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16750,
		PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, "Generic 16850 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16850,
		PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, "Generic 16950 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16950,
		PCI_INVAL, PCI_INVAL } },

	// non PCI_serial devices

	// beos zz driver supported that one
	{ B_PCI_BUS, "Lucent Modem", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_simple_communications_other, 0x00, 
		0x11C1, 0x0480 } }, 

	{ B_PCI_BUS, NULL, NULL, NULL, {0}, {0} }
};


#if 0
status_t
pc_serial_device_added(pc_device device, void **cookie)
{
	TRACE_FUNCALLS("> pc_serial_device_added(0x%08x, 0x%08x)\n", device, cookie);

	status_t status = B_OK;
	const pc_device_descriptor *descriptor
		= gUSBModule->get_device_descriptor(device);

	TRACE_ALWAYS("probing device: 0x%04x/0x%04x\n", descriptor->vendor_id,
		descriptor->product_id);

	*cookie = NULL;
	SerialDevice *serialDevice = SerialDevice::MakeDevice(device,
		descriptor->vendor_id, descriptor->product_id);

	const pc_configuration_info *configuration
		= gUSBModule->get_nth_configuration(device, 0);

	if (!configuration)
		return B_ERROR;

	status = serialDevice->AddDevice(configuration);
	if (status < B_OK) {
		delete serialDevice;
		return status;
	}

	acquire_sem(gDriverLock);
	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i] != NULL)
			continue;

		status = serialDevice->Init();
		if (status < B_OK) {
			delete serialDevice;
			return status;
		}

		gSerialDevices[i] = serialDevice;
		*cookie = serialDevice;

		release_sem(gDriverLock);
		TRACE_ALWAYS("%s (0x%04x/0x%04x) added\n", serialDevice->Description(),
			descriptor->vendor_id, descriptor->product_id);
		return B_OK;
	}

	release_sem(gDriverLock);
	return B_ERROR;
}


status_t
pc_serial_device_removed(void *cookie)
{
	TRACE_FUNCALLS("> pc_serial_device_removed(0x%08x)\n", cookie);

	acquire_sem(gDriverLock);

	SerialDevice *device = (SerialDevice *)cookie;
	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i] == device) {
			if (device->IsOpen()) {
				// the device will be deleted upon being freed
				device->Removed();
			} else {
				delete device;
				gSerialDevices[i] = NULL;
			}
			break;
		}
	}

	release_sem(gDriverLock);
	TRACE_FUNCRET("< pc_serial_device_removed() returns\n");
	return B_OK;
}
#endif

//#pragma mark -

status_t
pc_serial_insert_device(SerialDevice *device)
{
	status_t status = B_OK;

	//XXX fix leaks!
	acquire_sem(gDriverLock);
	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i] != NULL)
			continue;

		status = device->Init();
		if (status < B_OK) {
			delete device;
			//return status;
			break;
		}

		gSerialDevices[i] = device;

		release_sem(gDriverLock);
		TRACE_ALWAYS("%s added\n", device->Description());
		return B_OK;
	}

	release_sem(gDriverLock);
	return B_ERROR;
}


// probe devices with config_manager
static status_t
scan_bus(bus_type bus)
{
	const char *bus_name = "Unknown";
	uint64 cookie = 0;
	//status_t status;
	struct {
		device_info di;
		pci_info pi;
	} big_info;
	struct device_info &dinfo = big_info.di;
	
	switch (bus) {
	case B_ISA_BUS:
		bus_name = "ISA";
		break;
	case B_PCI_BUS:
		bus_name = "PCI";
		break;
	case B_PCMCIA_BUS:
	default:
		return EINVAL;
	}
	TRACE_ALWAYS("scanning %s bus...\n", bus_name);

//XXX: clean up this mess

	while ((gConfigManagerModule->get_next_device_info(bus, 
		&cookie, &big_info.di, sizeof(big_info)) == B_OK)) {
		// skip disabled devices
		if (dinfo.flags & B_DEVICE_INFO_ENABLED == 0)
			continue;
		// skip non configured devices
		if (dinfo.flags & B_DEVICE_INFO_CONFIGURED == 0)
			continue;
		// and devices in error
		if (dinfo.config_status < B_OK)
			continue;

		
		/*
		TRACE_ALWAYS("device: 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n",
		dinfo.id[0], dinfo.id[1], dinfo.id[2], dinfo.id[3]);
		*/

		/*
		if (bus == B_PCI_BUS) {
			pci_info *pcii = (pci_info *)(((char *)&dinfo) + 
				dinfo.bus_dependent_info_offset);
			TRACE_ALWAYS("pci: %04x:%04x\n",
				pcii->vendor_id, pcii->device_id);
			if ((pcii->header_type & PCI_header_type_mask) == 
				PCI_header_type_generic) {
				TRACE_ALWAYS("subsys: %04x:%04x\n",
					pcii->u.h0.subsystem_vendor_id, pcii->u.h0.subsystem_id);
			}
		}
		*/
		
		const struct serial_support_descriptor *supported = NULL;
		for (int i = 0; sSupportedDevices[i].name; i++) {
			if (sSupportedDevices[i].bus != bus)
				continue;
			if (sSupportedDevices[i].match.class_base != PCI_undefined &&
				sSupportedDevices[i].match.class_base != dinfo.devtype.base)
				continue;
			if (sSupportedDevices[i].match.class_sub != PCI_undefined &&
				sSupportedDevices[i].match.class_sub != dinfo.devtype.subtype)
				continue;
			if (sSupportedDevices[i].match.class_api != PCI_undefined &&
				sSupportedDevices[i].match.class_api != dinfo.devtype.interface)
				continue;

#if 0
			// either this way
			if (bus == B_PCI_BUS) {
				pci_info *pcii = (pci_info *)(((char *)&dinfo) + 
					dinfo.bus_dependent_info_offset);
				if (sSupportedDevices[i].match.vendor_id != PCI_INVAL &&
					sSupportedDevices[i].match.vendor_id != pcii->vendor_id)
					continue;
				if (sSupportedDevices[i].match.device_id != PCI_INVAL &&
					sSupportedDevices[i].match.device_id != pcii->device_id)
					continue;
			}
#endif
			// or this one:
			// .id[0] = vendor_id and .id[1] = device_id
			// .id[3?] = subsys_vendor_id and .id[2?] = subsys_device_id
			if (bus == B_PCI_BUS &&
				sSupportedDevices[i].match.vendor_id != PCI_INVAL &&
				sSupportedDevices[i].match.vendor_id != dinfo.id[0])
				continue;

			if (bus == B_PCI_BUS &&
				sSupportedDevices[i].match.device_id != PCI_INVAL &&
				sSupportedDevices[i].match.device_id != dinfo.id[1])
				continue;


			supported = &sSupportedDevices[i];
			break;
		}
		if (supported == NULL)
			continue;

		struct {
			struct device_configuration c;
			resource_descriptor res[16];
		} config;
		if (gConfigManagerModule->get_size_of_current_configuration_for(
			cookie) > (int)sizeof(config)) {
			TRACE_ALWAYS("config size too big for device\n");
			continue;
		}
			
		if (gConfigManagerModule->get_current_configuration_for(cookie,
			&config.c, sizeof(config)) < B_OK) {
			TRACE_ALWAYS("can't get config for device\n");
			continue;
			
		}

		TRACE_ALWAYS("device %Ld resources: %d irq %d dma %d io %d mem\n",
			cookie,
			gConfigManagerModule->count_resource_descriptors_of_type(
				&config.c, B_IRQ_RESOURCE),
			gConfigManagerModule->count_resource_descriptors_of_type(
				&config.c, B_DMA_RESOURCE),
			gConfigManagerModule->count_resource_descriptors_of_type(
				&config.c, B_IO_PORT_RESOURCE),
			gConfigManagerModule->count_resource_descriptors_of_type(
				&config.c, B_MEMORY_RESOURCE));


		// we first need the IRQ
		resource_descriptor irqdesc;
		if (gConfigManagerModule->get_nth_resource_descriptor_of_type(
			&config.c, 0, B_IRQ_RESOURCE, &irqdesc, sizeof(irqdesc)) < B_OK) {
			TRACE_ALWAYS("can't find IRQ for device\n");
			continue;
		}
		int irq;
		// XXX: what about APIC lines ?
		for (irq = 0; irq < 32; irq++) {
			if (irqdesc.d.m.mask & (1 << irq))
				break;
		}
		//TRACE_ALWAYS("irq %d\n", irq);
		//TRACE_ALWAYS("irq: %lx,%lx,%lx\n", irqdesc.d.m.mask, irqdesc.d.m.flags, irqdesc.d.m.cookie);

		TRACE_ALWAYS("found %s device %Ld [%x|%x|%x] "
			/*"ID: '%16.16s'"*/" irq: %d flags: %08lx status: %s\n",
			bus_name, cookie, dinfo.devtype.base, dinfo.devtype.subtype,
			dinfo.devtype.interface, /*dinfo.id,*/ irq, dinfo.flags,
			strerror(dinfo.config_status));

		// force enable I/O ports on PCI devices
#if 0
		if (bus == B_PCI_BUS) {
			pci_info *pcii = (pci_info *)(((char *)&dinfo) + 
				dinfo.bus_dependent_info_offset);
			
			uint32 cmd = gPCIModule->read_pci_config(pcii->bus, pcii->device,
				pcii->function, PCI_command, 2);
			TRACE_ALWAYS("PCI_command: 0x%04lx\n", cmd);
			cmd |= PCI_command_io;
			gPCIModule->write_pci_config(pcii->bus, pcii->device, 
				pcii->function, PCI_command, 2, cmd);
		}
#endif

		// instanciate devices on IO ports
		resource_descriptor iodesc;
		SerialDevice *master = NULL;
		for (int i = 0;
			gConfigManagerModule->get_nth_resource_descriptor_of_type(
			&config.c, i, B_IO_PORT_RESOURCE, &iodesc, sizeof(iodesc)) == B_OK;
			i++) {
			TRACE_ALWAYS("io at 0x%04lx len 0x%04lx\n", iodesc.d.r.minbase, 
				iodesc.d.r.len);

			if (iodesc.d.r.len < supported->constraints.minsize)
				continue;
			if (iodesc.d.r.len > supported->constraints.maxsize)
				continue;
			SerialDevice *device;
			uint32 ioport = iodesc.d.r.minbase;
next_split:
			// no more to split
			if ((ioport - iodesc.d.r.minbase) >= iodesc.d.r.len)
				continue;
		
			TRACE_ALWAYS("inserting device at io 0x%04lx as %s\n", ioport, 
				supported->name);

			
			device = new SerialDevice(supported, ioport, irq, master);
			if (pc_serial_insert_device(device) < B_OK) {
				TRACE_ALWAYS("can't insert device\n");
				continue;
			}
			if (master == NULL)
				master = device;
			
			ioport += supported->constraints.split;
			goto next_split;
			// try next part of the I/O range now
		}
		// we have at least one device
		if (master) {
			// hook up the irq
#if 0
			status = install_io_interrupt_handler(irq, pc_serial_interrupt, 
				master, 0);
			TRACE_ALWAYS("installing irq %d handler: %s\n", irq, strerror(status));
#endif
		}
	}
	return B_OK;
}

// this version doesn't use config_manager, but can't probe the IRQ yet
status_t
scan_pci_alt()
{
	pci_info info;
	int ix;
	// probe PCI devices
	for (ix = 0; (*gPCIModule->get_nth_pci_info)(ix, &info) == B_OK; ix++) {
		// sanity check
		if (info.header_type & PCI_header_type_mask != PCI_header_type_generic)
			continue;
		/*
		TRACE_ALWAYS("probing PCI device %2d [%x|%x|%x] %04x:%04x\n",
			ix, info.class_base, info.class_sub, info.class_api,
			info.vendor_id, info.device_id);
		*/

		const struct serial_support_descriptor *supported = NULL;
		for (int i = 0; sSupportedDevices[i].name; i++) {
			if (sSupportedDevices[i].bus != B_PCI_BUS)
				continue;
			if (info.class_base != sSupportedDevices[i].match.class_base)
				continue;
			if (info.class_sub != sSupportedDevices[i].match.class_sub)
				continue;
			if (info.class_api != sSupportedDevices[i].match.class_api)
				continue;
			if (sSupportedDevices[i].match.vendor_id != PCI_INVAL
				&& info.vendor_id != sSupportedDevices[i].match.vendor_id)
				continue;
			if (sSupportedDevices[i].match.device_id != PCI_INVAL
				&& info.device_id != sSupportedDevices[i].match.device_id)
				continue;
			supported = &sSupportedDevices[i];
		}
		if (supported == NULL)
			continue;

		TRACE_ALWAYS("found PCI device %2d [%x|%x|%x] %04x:%04x as %s\n",
			ix, info.class_base, info.class_sub, info.class_api,
			info.vendor_id, info.device_id, supported->name);

		// find I/O ports
		for (int r = 0; r < 6; r++) {
			TRACE_ALWAYS("range at 0x%08lx len 0x%lx flags 0x%02x\n",
				info.u.h0.base_registers[r], info.u.h0.base_register_sizes[r],
				info.u.h0.base_register_flags[r]);
			
			// not I/O
			if (info.u.h0.base_register_flags[r] & PCI_address_space == 0)
				continue;
			//
			TRACE_ALWAYS("regs at 0x%08lx len 0x%lx\n",
				info.u.h0.base_registers[r], info.u.h0.base_register_sizes[r]);
			//&PCI_address_io_mask
		}
		// XXX: interrupt_line doesn't seem to 
		TRACE_ALWAYS("irq line %d, pin %d\n",
			info.u.h0.interrupt_line, info.u.h0.interrupt_pin);
	}

	return B_OK;
}


//#pragma mark -


/* init_hardware - called once the first time the driver is loaded */
status_t
init_hardware()
{
	TRACE("init_hardware\n");
	return B_OK;
}


/* init_driver - called every time the driver is loaded. */
status_t
init_driver()
{
	status_t status;
	load_settings();
	create_log_file();

	TRACE_FUNCALLS("> init_driver()\n");

	status = get_module(B_TTY_MODULE_NAME, (module_info **)&gTTYModule);
	if (status < B_OK)
		goto err_tty;

#ifndef __HAIKU__
	// due to BONE having a different function count and ordering,
	// but the same version, to avoid crashing we detect it at runtime.
	{
		static tty_module_info_v1_bone sTTYModuleBONE;
		static tty_module_info_v1_r5 *ttyModuleR5 =
			(tty_module_info_v1_r5 *)gTTYModule;
		image_info info;
		int32 cookie = 0;
		while (get_next_image_info(/*B_KERNEL_TEAM*/1, &cookie, &info) == B_OK) {
			//dprintf(DRIVER_NAME ": checking image %32s\n", info.name);
			if ((char *)(gTTYModule->ttyopen) >= (char *)info.text
				&& (char *)(gTTYModule->ttyopen) < ((char *)info.text + info.text_size)) {
				void *symbol;
				dprintf(DRIVER_NAME ": detected tty module %32s\n", info.name);
				if (get_image_symbol(info.id, "ttydeselect",
					B_SYMBOL_TYPE_ANY, &symbol) != B_OK) {
					dprintf(DRIVER_NAME ": no ttydeselect() in tty module, assuming R5\n");
					// let's fake a BONE module with NULL select hooks
					memcpy(&sTTYModuleBONE.mi, &ttyModuleR5->mi, sizeof(ttyModuleR5->mi));
					sTTYModuleBONE.ttyopen = ttyModuleR5->ttyopen;
					sTTYModuleBONE.ttyclose = ttyModuleR5->ttyclose;
					sTTYModuleBONE.ttyfree = ttyModuleR5->ttyfree;
					sTTYModuleBONE.ttyread = ttyModuleR5->ttyread;
					sTTYModuleBONE.ttywrite = ttyModuleR5->ttywrite;
					sTTYModuleBONE.ttycontrol = ttyModuleR5->ttycontrol;
					sTTYModuleBONE.ttyinit = ttyModuleR5->ttyinit;
					sTTYModuleBONE.ttyilock = ttyModuleR5->ttyilock;
					sTTYModuleBONE.ttyhwsignal = ttyModuleR5->ttyhwsignal;
					sTTYModuleBONE.ttyin = ttyModuleR5->ttyin;
					sTTYModuleBONE.ttyout = ttyModuleR5->ttyout;
					sTTYModuleBONE.ddrstart = ttyModuleR5->ddrstart;
					sTTYModuleBONE.ddrdone = ttyModuleR5->ddrdone;
					sTTYModuleBONE.ddacquire = ttyModuleR5->ddacquire;
					// no select hooks
					sTTYModuleBONE.ttyselect = NULL;
					sTTYModuleBONE.ttydeselect = NULL;

					gTTYModule = &sTTYModuleBONE;
				}
				break;
			}
		}
	}
#endif

	status = get_module(B_PCI_MODULE_NAME, (module_info **)&gPCIModule);
	if (status < B_OK)
		goto err_pci;

	status = get_module(B_ISA_MODULE_NAME, (module_info **)&gISAModule);
	if (status < B_OK)
		goto err_isa;

	status = get_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME, 
		(module_info **)&gConfigManagerModule);
	if (status < B_OK)
		goto err_cm;

	for (int32 i = 0; i < DEVICES_COUNT; i++)
		gSerialDevices[i] = NULL;

	gDeviceNames[0] = NULL;

	gDriverLock = create_sem(1, DRIVER_NAME"_devices_table_lock");
	if (gDriverLock < B_OK) {
		status = gDriverLock;
		goto err_sem;
	}

	status = ENOENT;

	memset(&gSerialDomain, 0, sizeof(gSerialDomain));
	ddbackground(&gSerialDomain);


	scan_bus(B_ISA_BUS);
	scan_bus(B_PCI_BUS);


	// XXX: ISA cards
	// XXX: pcmcia
	
	
	TRACE_FUNCRET("< init_driver() returns\n");
	return B_OK;

//err_none:
	delete_sem(gDriverLock);
err_sem:
	put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
err_cm:
	put_module(B_ISA_MODULE_NAME);
err_isa:
	put_module(B_PCI_MODULE_NAME);
err_pci:
	put_module(B_TTY_MODULE_NAME);
err_tty:
	TRACE_FUNCRET("< init_driver() returns %s\n", strerror(status));
	return status;
}


/* uninit_driver - called every time the driver is unloaded */
void
uninit_driver()
{
	TRACE_FUNCALLS("> uninit_driver()\n");

	//gUSBModule->uninstall_notify(DRIVER_NAME);
	acquire_sem(gDriverLock);

	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i]) {
			/*
			if (gSerialDevices[i]->Master() == gSerialDevices[i])
				remove_io_interrupt_handler(gSerialDevices[i]->IRQ(), 
					pc_serial_interrupt, gSerialDevices[i]);
			*/
			delete gSerialDevices[i];
			gSerialDevices[i] = NULL;
		}
	}

	for (int32 i = 0; gDeviceNames[i]; i++)
		free(gDeviceNames[i]);

	delete_sem(gDriverLock);
	put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
	put_module(B_ISA_MODULE_NAME);
	put_module(B_PCI_MODULE_NAME);
	put_module(B_TTY_MODULE_NAME);

	TRACE_FUNCRET("< uninit_driver() returns\n");
}


bool
pc_serial_service(struct tty *ptty, struct ddrover *ddr, uint flags)
{
	TRACE_FUNCALLS("> pc_serial_service(0x%08x, 0x%08x, 0x%08x)\n", ptty, ddr, flags);

	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i] && gSerialDevices[i]->Service(ptty, ddr, flags)) {
			TRACE_FUNCRET("< pc_serial_service() returns: true\n");
			return true;
		}
	}

	TRACE_FUNCRET("< pc_serial_service() returns: false\n");
	return false;
}


int32
pc_serial_interrupt(void *arg)
{
	int32 ret;
	SerialDevice *master = (SerialDevice *)arg;
	TRACE_FUNCALLS("> pc_serial_interrupt(%p)\n", arg);

	if (!master)
		return B_UNHANDLED_INTERRUPT;

	ret = master->InterruptHandler();
	return ret;


	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i] && gSerialDevices[i]->Master() == master) {
			ret = gSerialDevices[i]->InterruptHandler();
			// XXX: handle more than 1 ?
			if (ret != B_UNHANDLED_INTERRUPT) {
				TRACE_FUNCRET("< pc_serial_interrupt() returns: true\n");
				return ret;
			}
		}
	}

	TRACE_FUNCRET("< pc_serial_interrupt() returns: unhandled\n");
	return B_UNHANDLED_INTERRUPT;
}


/* pc_serial_open - handle open() calls */
static status_t
pc_serial_open(const char *name, uint32 flags, void **cookie)
{
	TRACE_FUNCALLS("> pc_serial_open(%s, 0x%08x, 0x%08x)\n", name, flags, cookie);
	acquire_sem(gDriverLock);
	status_t status = ENODEV;

	*cookie = NULL;
	int i = strtol(name + strlen(sDeviceBaseName), NULL, 10);
	if (i >= 0 && i < DEVICES_COUNT && gSerialDevices[i]) {
		status = gSerialDevices[i]->Open(flags);
		*cookie = gSerialDevices[i];
	}

	release_sem(gDriverLock);
	TRACE_FUNCRET("< pc_serial_open() returns: 0x%08x\n", status);
	return status;
}


/* pc_serial_read - handle read() calls */
static status_t
pc_serial_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	TRACE_FUNCALLS("> pc_serial_read(0x%08x, %Ld, 0x%08x, %d)\n", cookie,
		position, buffer, *numBytes);
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Read((char *)buffer, numBytes);
}


/* pc_serial_write - handle write() calls */
static status_t
pc_serial_write(void *cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	TRACE_FUNCALLS("> pc_serial_write(0x%08x, %Ld, 0x%08x, %d)\n", cookie,
		position, buffer, *numBytes);
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Write((const char *)buffer, numBytes);
}


/* pc_serial_control - handle ioctl calls */
static status_t
pc_serial_control(void *cookie, uint32 op, void *arg, size_t length)
{
	TRACE_FUNCALLS("> pc_serial_control(0x%08x, 0x%08x, 0x%08x, %d)\n",
		cookie, op, arg, length);
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Control(op, arg, length);
}


#if defined(B_BEOS_VERSION_DANO) || defined(__HAIKU__)
/* pc_serial_select - handle select start */
static status_t
pc_serial_select(void *cookie, uint8 event, uint32 ref, selectsync *sync)
{
	TRACE_FUNCALLS("> pc_serial_select(0x%08x, 0x%08x, 0x%08x, %p)\n",
		cookie, event, ref, sync);
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Select(event, ref, sync);
}


/* pc_serial_deselect - handle select exit */
static status_t
pc_serial_deselect(void *cookie, uint8 event, selectsync *sync)
{
	TRACE_FUNCALLS("> pc_serial_deselect(0x%08x, 0x%08x, %p)\n",
		cookie, event, sync);
	SerialDevice *device = (SerialDevice *)cookie;
	return device->DeSelect(event, sync);
}
#endif // DANO, HAIKU


/* pc_serial_close - handle close() calls */
static status_t
pc_serial_close(void *cookie)
{
	TRACE_FUNCALLS("> pc_serial_close(0x%08x)\n", cookie);
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Close();
}


/* pc_serial_free - called after last device is closed, and all i/o complete. */
static status_t
pc_serial_free(void *cookie)
{
	TRACE_FUNCALLS("> pc_serial_free(0x%08x)\n", cookie);
	SerialDevice *device = (SerialDevice *)cookie;
	acquire_sem(gDriverLock);
	status_t status = device->Free();
	if (device->IsRemoved()) {
		for (int32 i = 0; i < DEVICES_COUNT; i++) {
			if (gSerialDevices[i] == device) {
				// the device is removed already but as it was open the
				// removed hook has not deleted the object
				delete device;
				gSerialDevices[i] = NULL;
				break;
			}
		}
	}

	release_sem(gDriverLock);
	return status;
}


/* publish_devices - null-terminated array of devices supported by this driver. */
const char **
publish_devices()
{
	TRACE_FUNCALLS("> publish_devices()\n");
	for (int32 i = 0; gDeviceNames[i]; i++)
		free(gDeviceNames[i]);

	int32 j = 0;
	acquire_sem(gDriverLock);
	for(int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i]) {
			gDeviceNames[j] = (char *)malloc(strlen(sDeviceBaseName) + 4);
			if (gDeviceNames[j]) {
				sprintf(gDeviceNames[j], "%s%ld", sDeviceBaseName, i);
				j++;
			} else
				TRACE_ALWAYS("publish_devices - no memory to allocate device names\n");
		}
	}

	gDeviceNames[j] = NULL;
	release_sem(gDriverLock);
	return (const char **)&gDeviceNames[0];
}


/* find_device - return poiter to device hooks structure for a given device */
device_hooks *
find_device(const char *name)
{
	static device_hooks deviceHooks = {
		pc_serial_open,			/* -> open entry point */
		pc_serial_close,			/* -> close entry point */
		pc_serial_free,			/* -> free cookie */
		pc_serial_control,			/* -> control entry point */
		pc_serial_read,			/* -> read entry point */
		pc_serial_write,			/* -> write entry point */
#if defined(B_BEOS_VERSION_DANO) || defined(__HAIKU__)
		pc_serial_select,			/* -> select entry point */
		pc_serial_deselect			/* -> deselect entry point */
#endif
	};

	TRACE_FUNCALLS("> find_device(%s)\n", name);
	return &deviceHooks;
}

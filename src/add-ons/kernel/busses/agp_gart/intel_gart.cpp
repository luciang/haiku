/*
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <AreaKeeper.h>
#include <intel_extreme.h>

#include <stdlib.h>

#include <AGP.h>
#include <KernelExport.h>
#include <PCI.h>


#define TRACE_INTEL
#ifdef TRACE_INTEL
#	define TRACE(x...) dprintf("\33[33magp-intel:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif

#ifndef __HAIKU__
#	define B_KERNEL_READ_AREA	0
#	define B_KERNEL_WRITE_AREA	0
#endif

/* read and write to PCI config space */
#define get_pci_config(info, offset, size) \
	(sPCI->read_pci_config((info).bus, (info).device, (info).function, \
		(offset), (size)))
#define set_pci_config(info, offset, size, value) \
	(sPCI->write_pci_config((info).bus, (info).device, (info).function, \
		(offset), (size), (value)))
#define write32(address, data) \
	(*((volatile uint32 *)(address)) = (data))
#define read32(address) \
	(*((volatile uint32 *)(address)))


const struct supported_device {
	uint32		bridge_id;
	uint32		display_id;
	uint32		type;
	const char	*name;
} kSupportedDevices[] = {
	{0x29b0, 0x29b2, INTEL_TYPE_G33, "G33"},
	{0x29c0, 0x29c2, INTEL_TYPE_G33, "Q35"},
	{0x29d0, 0x29d2, INTEL_TYPE_G33, "Q33"},
};

struct intel_info {
	pci_info	bridge;
	pci_info	display;
	uint32		type;

	uint32		*gtt_base;
	addr_t		gtt_physical_base;
	area_id		gtt_area;
	size_t		gtt_entries;
	size_t		gtt_stolen_entries;

	uint32		*registers;
	area_id		registers_area;

	addr_t		aperture_base;
	addr_t		aperture_physical_base;
	area_id		aperture_area;
	size_t		aperture_size;
	size_t		aperture_stolen_size;

	addr_t		scratch_page;
	area_id		scratch_area;
};

static intel_info sInfo;
static pci_module_info *sPCI;


static bool
has_display_device(pci_info &info, uint32 deviceID)
{
	for (uint32 index = 0; sPCI->get_nth_pci_info(index, &info) == B_OK;
			index++) {
		if (info.vendor_id != VENDOR_ID_INTEL
			|| info.device_id != deviceID
			|| info.class_base != PCI_display)
			continue;

		return true;
	}

	return false;
}


static void
determine_memory_sizes(intel_info &info, size_t &gttSize, size_t &stolenSize)
{
	// read stolen memory from the PCI configuration of the PCI bridge
	uint16 memoryConfig = get_pci_config(info.bridge,
		INTEL_GRAPHICS_MEMORY_CONTROL, 2);
	size_t memorySize = 1 << 20; // 1 MB
	gttSize = 0;
	stolenSize = 0;

	if (info.type == INTEL_TYPE_965) {
		switch (memoryConfig & i965_GTT_MASK) {
			case i965_GTT_128K:
				gttSize = 128 << 10;
				break;
			case i965_GTT_256K:
				gttSize = 256 << 10;
				break;
			case i965_GTT_512K:
				gttSize = 512 << 10;
				break;
		}
	} else if (info.type == INTEL_TYPE_G33) {
		switch (memoryConfig & G33_GTT_MASK) {
			case G33_GTT_1M:
				gttSize = 1 << 20;
				break;
			case G33_GTT_2M:
				gttSize = 2 << 20;
				break;
		}
	} else {
#if 0
		// older models have the GTT as large as their frame buffer mapping
		// TODO: check if the i9xx version works with the i8xx chips as well
		size_t frameBufferSize = 0;
		if ((info.device_type & INTEL_TYPE_FAMILY_MASK) == INTEL_TYPE_8xx) {
			if ((info.device_type & INTEL_TYPE_83x) != 0
				&& (memoryConfig & MEMORY_MASK) == i830_FRAME_BUFFER_64M)
				frameBufferSize = 64 << 20;
			else
				frameBufferSize = 128 << 20;
		} else if ((info.device_type & INTEL_TYPE_FAMILY_MASK) == INTEL_TYPE_9xx)
			frameBufferSize = info.pci->u.h0.base_register_sizes[2];

		gttSize = frameBufferSize / 1024;
#endif
	}

#if 0
	// TODO: test with different models!

	if (info.device_type == (INTEL_TYPE_8xx | INTEL_TYPE_83x)) {
		switch (memoryConfig & STOLEN_MEMORY_MASK) {
			case i830_LOCAL_MEMORY_ONLY:
				// TODO: determine its size!
				break;
			case i830_STOLEN_512K:
				memorySize >>= 1;
				break;
			case i830_STOLEN_8M:
				memorySize *= 8;
				break;
		}
	} else if (info.device_type == (INTEL_TYPE_8xx | INTEL_TYPE_85x)
		|| (info.device_type & INTEL_TYPE_FAMILY_MASK) == INTEL_TYPE_9xx)
#endif
	{
		switch (memoryConfig & STOLEN_MEMORY_MASK) {
			case i855_STOLEN_MEMORY_4M:
				memorySize *= 4;
				break;
			case i855_STOLEN_MEMORY_8M:
				memorySize *= 8;
				break;
			case i855_STOLEN_MEMORY_16M:
				memorySize *= 16;
				break;
			case i855_STOLEN_MEMORY_32M:
				memorySize *= 32;
				break;
			case i855_STOLEN_MEMORY_48M:
				memorySize *= 48;
				break;
			case i855_STOLEN_MEMORY_64M:
				memorySize *= 64;
				break;
			case i855_STOLEN_MEMORY_128M:
				memorySize *= 128;
				break;
			case i855_STOLEN_MEMORY_256M:
				memorySize *= 256;
				break;
		}
	}

	stolenSize = memorySize - gttSize - 4096;
}


static void
set_gtt_entry(intel_info &info, uint32 offset, addr_t physicalAddress)
{
	write32(info.gtt_base + (offset >> GTT_PAGE_SHIFT),
		(uint32)physicalAddress | GTT_ENTRY_VALID);
}


static void
intel_unmap(intel_info &info)
{
	delete_area(info.registers_area);
	delete_area(info.gtt_area);
	delete_area(info.scratch_area);
	delete_area(info.aperture_area);
	info.aperture_size = 0;
}


static status_t
intel_map(intel_info &info)
{
	info.gtt_physical_base = get_pci_config(info.display, i915_GTT_BASE, 4);

	size_t gttSize, stolenSize;
	determine_memory_sizes(info, gttSize, stolenSize);

	info.gtt_entries = gttSize / 4096;
	info.gtt_stolen_entries = stolenSize / 4096;
dprintf("GTT base %lx, size %lu, entries %lu, stolen %lu\n", info.gtt_physical_base,
	gttSize, info.gtt_entries, stolenSize);

	int fbIndex = 0;
	int mmioIndex = 1;
	if ((info.type & INTEL_TYPE_FAMILY_MASK) == INTEL_TYPE_9xx) {
		// for some reason Intel saw the need to change the order of the mappings
		// with the introduction of the i9xx family
		mmioIndex = 0;
		fbIndex = 2;
	}

	AreaKeeper gttMapper;
	info.gtt_area = gttMapper.Map("intel GMCH gtt",
		(void *)info.gtt_physical_base, gttSize, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void **)&info.gtt_base);
	if (gttMapper.InitCheck() < B_OK) {
		dprintf("agp_intel: could not map GTT!\n");
		return info.gtt_area;
	}

	AreaKeeper mmioMapper;
	info.registers_area = mmioMapper.Map("intel GMCH mmio",
		(void *)info.display.u.h0.base_registers[mmioIndex],
		info.display.u.h0.base_register_sizes[mmioIndex], B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void **)&info.registers);
	if (mmioMapper.InitCheck() < B_OK) {
		dprintf("agp_intel: could not map memory I/O!\n");
		return info.registers_area;
	}

	void *scratchAddress;
	AreaKeeper scratchCreator;
	info.scratch_area = scratchCreator.Create("intel GMCH scratch",
		&scratchAddress, B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (scratchCreator.InitCheck() < B_OK) {
		dprintf("agp_intel: could not create scratch page!\n");
		return info.scratch_area;
	}

	physical_entry entry;
	if (get_memory_map(scratchAddress, B_PAGE_SIZE, &entry, 1) != B_OK)
		return B_ERROR;

	info.aperture_physical_base = info.display.u.h0.base_registers[fbIndex];
	info.aperture_stolen_size = stolenSize;
	if (info.aperture_size == 0)
		info.aperture_size = info.display.u.h0.base_register_sizes[0];

	AreaKeeper apertureMapper;
	info.aperture_area = apertureMapper.Map("intel graphics aperture",
		(void *)info.aperture_physical_base, info.aperture_size,
		B_ANY_KERNEL_BLOCK_ADDRESS | B_MTR_WC,
		B_READ_AREA | B_WRITE_AREA, (void **)&info.aperture_base);
	if (apertureMapper.InitCheck() < B_OK) {
		// try again without write combining
		dprintf(DEVICE_NAME ": enabling write combined mode failed.\n");

		info.aperture_area = apertureMapper.Map("intel graphics aperture",
			(void *)info.aperture_physical_base, info.aperture_size,
			B_ANY_KERNEL_BLOCK_ADDRESS, B_READ_AREA | B_WRITE_AREA,
			(void **)&info.aperture_base);
	}
	if (apertureMapper.InitCheck() < B_OK) {
		dprintf(DEVICE_NAME ": could not map graphics aperture!\n");
		return info.aperture_area;
	}

	info.scratch_page = (addr_t)entry.address;

	gttMapper.Detach();
	mmioMapper.Detach();
	scratchCreator.Detach();
	apertureMapper.Detach();

	return B_OK;
}


//	#pragma mark - module interface


status_t
intel_create_aperture(uint8 bus, uint8 device, uint8 function, size_t size,
	void **_aperture)
{
	// TODO: we currently only support a single AGP bridge!
	if ((bus != sInfo.bridge.bus || device != sInfo.bridge.device
			|| function != sInfo.bridge.function)
		&& (bus != sInfo.display.bus || device != sInfo.display.device
			|| function != sInfo.display.function))
		return B_BAD_VALUE;

	sInfo.aperture_size = size;

	if (intel_map(sInfo) < B_OK)
		return B_ERROR;

	uint16 gmchControl = get_pci_config(sInfo.bridge,
		INTEL_GRAPHICS_MEMORY_CONTROL, 2) | MEMORY_CONTROL_ENABLED;
	set_pci_config(sInfo.bridge, INTEL_GRAPHICS_MEMORY_CONTROL, 2, gmchControl);

	write32(sInfo.registers + INTEL_PAGE_TABLE_CONTROL, 
		sInfo.gtt_physical_base | PAGE_TABLE_ENABLED);
	read32(sInfo.registers + INTEL_PAGE_TABLE_CONTROL);

	if (sInfo.scratch_page != 0) {
		for (size_t i = sInfo.gtt_stolen_entries; i < sInfo.gtt_entries; i++) {
			set_gtt_entry(sInfo, i << GTT_PAGE_SHIFT, sInfo.scratch_page);
		}
		read32(sInfo.gtt_base + sInfo.gtt_entries - 1);
	}

	asm("wbinvd;");

	*_aperture = NULL;
	return B_OK;
}


void
intel_delete_aperture(void *aperture)
{
	intel_unmap(sInfo);
}


static status_t
intel_get_aperture_info(void *aperture, aperture_info *info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	info->base = sInfo.aperture_base;
	info->physical_base = sInfo.aperture_physical_base;
	info->size = sInfo.aperture_size;
	info->reserved_size = sInfo.aperture_stolen_size;

	return B_OK;
}


status_t
intel_set_aperture_size(void *aperture, size_t size)
{
	return B_ERROR;
}


static status_t
intel_bind_page(void *aperture, uint32 offset, addr_t physicalAddress)
{
	set_gtt_entry(sInfo, offset << GTT_PAGE_SHIFT, physicalAddress);
	return B_OK;
}


static status_t
intel_unbind_page(void *aperture, uint32 offset)
{
	if (sInfo.scratch_page != 0)
		set_gtt_entry(sInfo, offset << GTT_PAGE_SHIFT, sInfo.scratch_page);

	return B_OK;
}


void
intel_flush_tlbs(void *aperture)
{
	read32(sInfo.gtt_base + sInfo.gtt_entries - 1);
	asm("wbinvd;");
}


//	#pragma mark -


static status_t
intel_init()
{
	TRACE("bus manager init\n");

	if (get_module(B_PCI_MODULE_NAME, (module_info **)&sPCI) != B_OK)
		return B_ERROR;

	bool found = false;

	for (uint32 index = 0; sPCI->get_nth_pci_info(index, &sInfo.bridge) == B_OK;
			index++) {
		if (sInfo.bridge.vendor_id != VENDOR_ID_INTEL
			|| sInfo.bridge.class_base != PCI_bridge)
			continue;

		// check device
		for (uint32 i = 0; i < sizeof(kSupportedDevices)
				/ sizeof(kSupportedDevices[0]); i++) {
			if (sInfo.bridge.device_id == kSupportedDevices[i].bridge_id) {
				sInfo.type = kSupportedDevices[i].type;
				found = has_display_device(sInfo.display,
					kSupportedDevices[i].display_id);
				break;
			}
		}

		if (found)
			break;
	}

	if (!found)
		return ENODEV;

	TRACE("found intel bridge\n");
	return B_OK;
}


static void
intel_uninit()
{
}


static int32
intel_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return intel_init();
		case B_MODULE_UNINIT:
			intel_uninit();
			return B_OK;
	}

	return B_BAD_VALUE;
}


static struct agp_gart_bus_module_info sIntelModuleInfo = {
	{
		"busses/agp_gart/intel/v0",
		0,
		intel_std_ops
	},

	intel_create_aperture,
	intel_delete_aperture,

	intel_get_aperture_info,
	intel_set_aperture_size,
	intel_bind_page,
	intel_unbind_page,
	intel_flush_tlbs
};

module_info *modules[] = {
	(module_info *)&sIntelModuleInfo,
	NULL
};

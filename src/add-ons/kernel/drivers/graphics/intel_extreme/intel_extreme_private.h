/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef INTEL_EXTREME_PRIVATE_H
#define INTEL_EXTREME_PRIVATE_H


#include <AGP.h>
#include <KernelExport.h>
#include <PCI.h>

#include "intel_extreme.h"
#include "lock.h"


struct intel_info {
	int32			open_count;
	status_t		init_status;
	int32			id;
	pci_info*		pci;
	addr_t			aperture_base;
	aperture_id		aperture;
	uint8*			registers;
	area_id			registers_area;
	struct intel_shared_info* shared_info;
	area_id			shared_area;

	struct overlay_registers* overlay_registers;

	bool			fake_interrupts;

	const char*		device_identifier;
	DeviceType		device_type;
};

extern status_t intel_free_memory(intel_info& info, addr_t offset);
extern status_t intel_allocate_memory(intel_info& info, size_t size,
	size_t alignment, uint32 flags, addr_t* _offset,
	phys_addr_t* _physicalBase = NULL);
extern status_t intel_extreme_init(intel_info& info);
extern void intel_extreme_uninit(intel_info& info);

#endif  /* INTEL_EXTREME_PRIVATE_H */

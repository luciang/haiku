/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef RADEON_RD_PRIVATE_H
#define RADEON_RD_PRIVATE_H


#include <AGP.h>
#include <KernelExport.h>
#include <PCI.h>


#include "radeon_hd.h"
#include "lock.h"


struct radeon_info {
	int32			open_count;
	status_t		init_status;
	int32			id;
	pci_info*		pci;
	uint8*			registers;
	area_id			registers_area;
	area_id			framebuffer_area;

	struct radeon_shared_info* shared_info;
	area_id			shared_area;

	const char*		device_identifier;
	DeviceType		device_type;
};


extern status_t radeon_hd_init(radeon_info& info);
extern void radeon_hd_uninit(radeon_info& info);

#endif  /* RADEON_RD_PRIVATE_H */

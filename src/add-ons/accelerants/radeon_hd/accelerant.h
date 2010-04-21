/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef RADEON_HD_ACCELERANT_H
#define RADEON_HD_ACCELERANT_H


#include "radeon_hd.h"


#include <edid.h>


struct accelerant_info {
	vuint8			*regs;
	area_id			regs_area;

	radeon_shared_info *shared_info;
	area_id			shared_info_area;

	display_mode	*mode_list;		// cloned list of standard display modes
	area_id			mode_list_area;

	edid1_info		edid_info;
	bool			has_edid;

	int				device;
	bool			is_clone;

	// LVDS panel mode passed from the bios/startup.
	display_mode	lvds_panel_mode;
};

#define HEAD_MODE_A_ANALOG		0x01
#define HEAD_MODE_B_DIGITAL		0x02
#define HEAD_MODE_CLONE			0x03
#define HEAD_MODE_LVDS_PANEL	0x08

extern accelerant_info *gInfo;

// register access

inline uint32
read32(uint32 offset)
{
	return *(volatile uint32 *)(gInfo->regs + offset);
}

inline void
write32(uint32 offset, uint32 value)
{
	*(volatile uint32 *)(gInfo->regs + offset) = value;
}

// modes.cpp 
extern status_t create_mode_list(void);

#endif	/* RADEON_HD_ACCELERANT_H */

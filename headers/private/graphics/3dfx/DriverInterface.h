/*
 * Copyright 2007-2010 Haiku, Inc.  All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Gerald Zajac
 */

#ifndef DRIVERINTERFACE_H
#define DRIVERINTERFACE_H


#include <Accelerant.h>
#include <GraphicsDefs.h>
#include <Drivers.h>
#include <edid.h>
#include <video_overlay.h>


// This file contains info that is shared between the kernel driver and the
// accelerant, and info that is shared among the source files of the accelerant.


#define ENABLE_DEBUG_TRACE		// if defined, turns on debug output to syslog


#define ARRAY_SIZE(a) (int(sizeof(a) / sizeof(a[0]))) 	// get number of elements in an array



struct Benaphore {
	sem_id	sem;
	int32	count;

	status_t Init(const char* name)
	{
		count = 0;
		sem = create_sem(0, name);
		return sem < 0 ? sem : B_OK;
	}

	status_t Acquire()
	{
		if (atomic_add(&count, 1) > 0)
			return acquire_sem(sem);
		return B_OK;
	}

	status_t Release()
	{
		if (atomic_add(&count, -1) > 1)
			return release_sem(sem);
		return B_OK;
	}

	void Delete()	{ delete_sem(sem); }
};


#define TDFX_PRIVATE_DATA_MAGIC	 0x5042


enum {
	TDFX_GET_SHARED_DATA = B_DEVICE_OP_CODES_END + 123,
	TDFX_DEVICE_NAME,
	TDFX_GET_PIO_REG,
	TDFX_SET_PIO_REG
};


// Chip type numbers.  These are used to group the chips into related
// groups.	See table chipTable in driver.c

enum ChipType {
	TDFX_NONE = 0,

	BANSHEE,
	VOODOO_3,
	VOODOO_5,
};


struct PIORegInfo {
	uint32	magic;	// magic number
	uint32	offset;	// offset of register in PIO register area
	int16	index;	// index of value to read/write; < 0 if not indexed reg
	uint8	value;	// value to write or value that was read
};


struct DisplayModeEx : display_mode {
	uint8	bitsPerPixel;
	uint8	bytesPerPixel;
	uint16	bytesPerRow;		// number of bytes in one line/row
};


struct OverlayBuffer : overlay_buffer {
	OverlayBuffer*	nextBuffer;	// pointer to next buffer in chain, NULL = none
	uint32			size;		// size of overlay buffer
};


struct SharedInfo {
	// Device ID info.
	uint16	vendorID;			// PCI vendor ID, from pci_info
	uint16	deviceID;			// PCI device ID, from pci_info
	uint8	revision;			// PCI device revsion, from pci_info
	ChipType chipType;			// indicates group in which chip belongs (a group has similar functionality)
	char	chipName[32];		// user recognizable name of chip

	bool	bAccelerantInUse;	// true = accelerant has been initialized

	// Memory mappings.
	area_id regsArea;			// area_id for the memory mapped registers. It will
								// be cloned into accelerant's address space.
	area_id videoMemArea;		// video memory area_id.  The addresses are shared with all teams.
	addr_t	videoMemAddr;		// video memory addr as viewed from virtual memory
	phys_addr_t	videoMemPCI;	// video memory addr as viewed from the PCI bus (for DMA)
	uint32	videoMemSize; 		// video memory size in bytes.

	uint32	cursorOffset;		// offset of cursor in video memory
	uint32	frameBufferOffset;	// offset of frame buffer in video memory
	uint32	maxFrameBufferSize;	// max available video memory for frame buffer

	// Color spaces supported by current video chip/driver.
	color_space	colorSpaces[6];
	uint32	colorSpaceCount;	// number of color spaces in array colorSpaces

	uint32 maxPixelClock;		// max pixel clock of current chip in KHz

	// List of screen modes.
	area_id modeArea;			// area containing list of display modes the driver supports
	uint32	modeCount;			// number of display modes in the list

	DisplayModeEx displayMode;	// current display mode configuration

	uint16		cursorHotX;		// Cursor hot spot. Top left corner of the cursor
	uint16		cursorHotY;		// is 0,0

	edid1_info	edidInfo;
	bool		bHaveEDID;		// true = EDID info from device is in edidInfo

	Benaphore	engineLock;		// for access to the acceleration engine
	Benaphore	overlayLock;	// for overlay operations

	int32		overlayAllocated;	// non-zero if overlay is allocated
	uint32		overlayToken;
	OverlayBuffer* overlayBuffer;	// pointer to linked list of buffers; NULL = none
};


#endif	// DRIVERINTERFACE_H

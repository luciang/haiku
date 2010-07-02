/*
 * Copyright 2009, François Revol, revol@free.fr.
 * Distributed under the terms of the MIT License.
 */


#include "arch_video.h"

#include <arch/cpu.h>
#include <boot/stage2.h>
#include <boot/platform.h>
#include <boot/menu.h>
#include <boot/kernel_args.h>
#include <boot/platform/generic/video.h>
#include <board_config.h>
#include <util/list.h>
#include <drivers/driver_settings.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//XXX
extern "C" addr_t mmu_map_physical_memory(addr_t physicalAddress, size_t size, uint32 flags);


#define TRACE_VIDEO
#ifdef TRACE_VIDEO
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define write_io_32(a, v) ((*(vuint32 *)a) = v)
#define read_io_32(a) (*(vuint32 *)a)

#define dumpr(a) dprintf("LCC:%s:0x%lx\n", #a, read_io_32(a))



#if BOARD_CPU_PXA270
//	#pragma mark -


extern void *gFrameBufferBase;
static struct pxa27x_lcd_dma_descriptor sVideoDMADesc;
static uint32 scratch[128] __attribute__((aligned(16)));


status_t
arch_probe_video_mode(void)
{
	dprintf("%s()\n", __FUNCTION__);
	uint32 bppCode, pixelFormat;
	struct pxa27x_lcd_dma_descriptor *dma;

	// check if LCD controller is enabled
	if (!(read_io_32(LCCR0) | 0x00000001))
		return B_NO_INIT;

	pixelFormat = bppCode = read_io_32(LCCR3);
	bppCode = (bppCode >> 26) & 0x08 | (bppCode >> 24) & 0x07;
	pixelFormat >>= 30;

	dma = (struct pxa27x_lcd_dma_descriptor *)(read_io_32(FDADR0) & ~0x0f);
	if (!dma)
		return B_ERROR;

	switch (bppCode) {
		case 2:
			gKernelArgs.frame_buffer.depth = 4;
			break;
		case 3:
			gKernelArgs.frame_buffer.depth = 8;
			break;
		case 4:
			gKernelArgs.frame_buffer.depth = 16;
			break;
		case 9:
		case 10:
			gKernelArgs.frame_buffer.depth = 32; // RGB888
			break;
		defaut:
			return B_ERROR;
	}

	gKernelArgs.frame_buffer.physical_buffer.start = (dma->fdadr & ~0x0f);
	gKernelArgs.frame_buffer.width = (read_io_32(LCCR1) & ((1 << 10) - 1)) + 1;
	gKernelArgs.frame_buffer.height = (read_io_32(LCCR2) & ((1 << 10) - 1)) + 1;
	gKernelArgs.frame_buffer.bytes_per_row = gKernelArgs.frame_buffer.width
		* sizeof(uint32);
	gKernelArgs.frame_buffer.physical_buffer.size = gKernelArgs.frame_buffer.width
		* gKernelArgs.frame_buffer.height
		* gKernelArgs.frame_buffer.depth / 8;

	dprintf("video mode: %ux%ux%u\n", gKernelArgs.frame_buffer.width,
		gKernelArgs.frame_buffer.height, gKernelArgs.frame_buffer.depth);

	gKernelArgs.frame_buffer.enabled = true;

	

	return B_OK;
}

status_t
arch_set_video_mode(int width, int height, int depth)
{
	dprintf("%s(%d, %d, %d)\n", __FUNCTION__, width, height, depth);
	status_t err;
	void *fb;
	uint32 fbSize = width * height * depth / 8;
	//fb = malloc(800*600*4 + 16 - 1);
	//fb = (void *)(((uint32)fb) & ~(0x0f));
	//fb = scratch - 800;
	//fb = (void *)0xa0000000;

//	gFrameBufferBase = scratch - 800;

#if 1
	gFrameBufferBase = (void *)0xa4000000;
	gKernelArgs.frame_buffer.physical_buffer.start = (addr_t)gFrameBufferBase;
#endif
#if 0
	if (!gFrameBufferBase) {
		//XXX: realloc if larger !!!
		err = platform_allocate_region(&gFrameBufferBase, fbSize, 0, false);
dprintf("error %08x\n", err);
		if (err < B_OK)
			return err;
		gKernelArgs.frame_buffer.physical_buffer.start = (addr_t)gFrameBufferBase;
/*
		gFrameBufferBase = (void *)mmu_map_physical_memory(
			0xa8000000, fbSize, 0);
		if (gFrameBufferBase == NULL)
			return B_NO_MEMORY;
		gKernelArgs.frame_buffer.physical_buffer.start = (addr_t)gFrameBufferBase;//0xa8000000;
*/
	}
#endif
	fb = gFrameBufferBase;

	dprintf("fb @ %p\n", fb);

	
	sVideoDMADesc.fdadr = ((uint32)&sVideoDMADesc & ~0x0f) | 0x01;
	sVideoDMADesc.fsadr = (uint32)(fb) & ~0x0f;
	sVideoDMADesc.fidr = 0;
	sVideoDMADesc.ldcmd = fbSize;

	// if not already enabled, set a default mode
	if (!(read_io_32(LCCR0) & 0x00000001)) {
		int bpp = 0x09; // 24 bpp
		int pdfor = 0x3; // Format 4: RGB888 (no alpha bit)
		dprintf("Setting video mode\n");
		switch (depth) {
			case 4:
				bpp = 2;
				break;
			case 8:
				bpp = 3;
				break;
			case 16:
				bpp = 3;
				break;
			case 32:
				bpp = 9;
				pdfor = 0x3;
				break;
			default:
				return EINVAL;
		}
		write_io_32(LCCR1, (0 << 0) | (width - 1));
		write_io_32(LCCR2, (0 << 0) | (height - 1));
		write_io_32(LCCR3, (pdfor << 30) | ((bpp >> 3) << 29) | ((bpp & 0x07) << 24));
		write_io_32(FDADR0, sVideoDMADesc.fdadr);
		write_io_32(LCCR0, read_io_32(LCCR0) | 0x01800001);     // no ints +ENB
		write_io_32(FBR0, sVideoDMADesc.fdadr);
		dumpr(LCCR0);
		dumpr(LCCR1);
		dumpr(LCCR2);
		dumpr(LCCR3);
		dumpr(LCCR4);
	} else
		return EALREADY; // for now
	
	// clear the video memory
	memset((void *)fb, 0, fbSize);

	// XXX test pattern
	for (int i = 0; i < 128; i++) {
		((uint32 *)fb)[i+16] = 0x000000ff << ((i%4) * 8);
		scratch[i] = 0x000000ff << ((i%4) * 8);
	}

	// update framebuffer descriptor
	return arch_probe_video_mode();
}


status_t
arch_set_default_video_mode()
{
	dprintf("%s()\n", __FUNCTION__);
	return arch_set_video_mode(800, 600, 32);
}


#endif



/*
 * Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2008, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2008, Philippe Saint-Pierre <stpere@gmail.com>
 * Distributed under the terms of the MIT License.
 */


#include "video.h"

#include <arch/cpu.h>
#include <boot/stage2.h>
#include <boot/platform.h>
#include <boot/menu.h>
#include <boot/platform/generic/video.h>
#include <boot/kernel_args.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define TRACE_VIDEO
#ifdef TRACE_VIDEO
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


static void
blit32(addr_t frameBuffer, uint32 bytesPerRow, const uint8 *data, uint16 width,
	uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	uint32 *start = (uint32 *)(frameBuffer + bytesPerRow * top + 4 * left);

	for (int32 y = 0; y < height; y++) {
		const uint8* src = data;
		uint32* dst = start;
		for (int32 x = 0; x < width; x++) {
			dst[0] = (src[2] << 16) | (src[1] << 8) | (src[0]);
			dst++;
			src += 3;
		}

		data += imageWidth * 3;
		start = (uint32 *)((addr_t)start
			+ bytesPerRow);
	}
}


static void
blit24(addr_t frameBuffer, uint32 bytesPerRow, const uint8 *data, uint16 width,
	uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	uint8 *start = (uint8 *)frameBuffer + bytesPerRow * top + 3 * left;

	for (int32 y = 0; y < height; y++) {
		const uint8* src = data;
		uint8* dst = start;
		for (int32 x = 0; x < width; x++) {
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst += 3;
			src += 3;
		}

		data += imageWidth * 3;
		start = start + bytesPerRow;
	}
}


static void
blit16(addr_t frameBuffer, uint32 bytesPerRow, const uint8 *data, uint16 width,
	uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	uint16 *start = (uint16 *)(frameBuffer + bytesPerRow * top + 2 * left);

	for (int32 y = 0; y < height; y++) {
		const uint8* src = data;
		uint16* dst = start;
		for (int32 x = 0; x < width; x++) {
			dst[0] = ((src[2] >> 3) << 11)
				| ((src[1] >> 2) << 5)
				| ((src[0] >> 3));
			dst++;
			src += 3;
		}

		data += imageWidth * 3;
		start = (uint16 *)((addr_t)start
			+ bytesPerRow);
	}
}


static void
blit15(addr_t frameBuffer, uint32 bytesPerRow, const uint8 *data, uint16 width,
	uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	uint16 *start = (uint16 *)(frameBuffer + bytesPerRow * top + 2 * left);

	for (int32 y = 0; y < height; y++) {
		const uint8* src = data;
		uint16* dst = start;
		for (int32 x = 0; x < width; x++) {
			dst[0] = ((src[2] >> 3) << 10)
				| ((src[1] >> 3) << 5)
				| ((src[0] >> 3));
			dst++;
			src += 3;
		}

		data += imageWidth * 3;
		start = (uint16 *)((addr_t)start
			+ bytesPerRow);
	}
}


static void
blit8(addr_t frameBuffer, uint32 bytesPerRow, const uint8 *data, uint16 width,
	uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	if (!data)
		return;

	addr_t start = frameBuffer + bytesPerRow * top
		+ left;

	for (int32 i = 0; i < height; i++) {
		memcpy((void *)(start + bytesPerRow * i),
			&data[i * imageWidth], width);
	}
}


static void
blit4(addr_t frameBuffer, uint32 bytesPerRow, const uint8 *data, uint16 width,
	uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	if (!data)
		return;
	// call back platform specific code since it's really platform-specific.
	platform_blit4(frameBuffer, bytesPerRow, data, width, height,
		imageWidth, left, top);
}


void
video_blit_image(addr_t frameBuffer, uint32 bytesPerRow, const uint8 *data,
	uint16 width, uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	switch (gKernelArgs.frame_buffer.depth) {
		case 4:
			return blit4(frameBuffer, bytesPerRow, data,
				width, height, imageWidth, left, top);
		case 8:
			return blit8(frameBuffer, bytesPerRow, data,
				width, height, imageWidth, left, top);
		case 15:
			return blit15(frameBuffer, bytesPerRow, data,
				width, height, imageWidth, left, top);
		case 16:
			return blit16(frameBuffer, bytesPerRow, data,
				width, height, imageWidth, left, top);
		case 24:
			return blit24(frameBuffer, bytesPerRow, data,
				width, height, imageWidth, left, top);
		case 32:
			return blit32(frameBuffer, bytesPerRow, data,
				width, height, imageWidth, left, top);
	}
}


/*
** Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include <OS.h>

#include <boot/stage2.h>
#include <boot/platform.h>


static const size_t kChunkSize = 16384;

kernel_args gKernelArgs;

static void *sFirstFree;
static void *sLast;
static size_t sFree = kChunkSize;


/** This function can be used to allocate memory that is going
 *	to be passed over to the kernel. For example, the preloaded_image
 *	structures are allocated this way.
 *	The boot loader heap doesn't make it into the kernel!
 */

extern "C" void *
kernel_args_malloc(size_t size)
{
	if (sFirstFree != NULL && size <= sFree) {
		// there is enough space in the current buffer
		void *address = sFirstFree;
		sFirstFree = (void *)((addr_t)sFirstFree + size);
		sLast = address;
		sFree -= size;

		return address;
	}

	if (size > kChunkSize / 2 && sFree < size) {
		// the block is so large, we'll allocate a new block for it
		void *block = NULL;
		if (platform_allocate_region(&block, size, B_READ_AREA | B_WRITE_AREA) != B_OK)
			return NULL;

		return block;
	}

	// just allocate a new block and "close" the old one
	void *block = NULL;
	if (platform_allocate_region(&block, kChunkSize, B_READ_AREA | B_WRITE_AREA) != B_OK)
		return NULL;

	sFirstFree = (void *)((addr_t)block + size);
	sLast = block;
	sFree = kChunkSize - size;

	return block;
}


/** This function frees a block allocated via kernel_args_malloc().
 *	It's very simple; it can only free the last allocation. It's
 *	enough for its current usage in the boot loader, though.
 */

extern "C" void
kernel_args_free(void *block)
{
	if (sLast != block) {
		// sorry, we're dumb
		return;
	}

	sFree = (addr_t)sFirstFree - (addr_t)sLast;
	sFirstFree = block;
	sLast = NULL;
}


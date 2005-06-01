/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "block_cache_private.h"

#include <KernelExport.h>
#include <fs_cache.h>

#include <lock.h>
#include <util/kernel_cpp.h>
#include <util/DoublyLinkedList.h>
#include <util/AutoLock.h>
#include <util/khash.h>
#include <vm.h>
#include <vm_page.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


static class BlockAddressPool sBlockAddressPool;


BlockAddressPool::BlockAddressPool()
{
	benaphore_init(&fLock, "block address pool");

	fBase = 0xa0000000;
		// directly after the I/O space area
	fArea = vm_create_null_area(vm_get_kernel_aspace_id(), "block cache", (void **)&fBase,
		B_BASE_ADDRESS, kBlockAddressSize);

	fFirstFree = fBase;
	fNextFree = -1;
}


BlockAddressPool::~BlockAddressPool()
{
	delete_area(fArea);
}


addr_t
BlockAddressPool::Get()
{
	BenaphoreLocker locker(&fLock);

	// ToDo: we must make sure that a single volume will never eat
	//	all available ranges! Every mounted volume should have a
	//	guaranteed minimum available for its own use.

	addr_t address = fFirstFree;
	if (address != NULL) {
		// Blocks are first taken from the initial free chunk, and
		// when that is empty, from the free list.
		// This saves us the initialization of the free list array,
		// dunno if it's such a good idea, though.
		if ((fFirstFree += RangeSize()) >= fBase + kBlockAddressSize)
			fFirstFree = NULL;

		return address;
	}

	if (fNextFree == -1)
		return NULL;

	address = (fNextFree << RangeShift()) + fBase;
	fNextFree = fFreeList[fNextFree];

	return address;
}


void
BlockAddressPool::Put(addr_t address)
{
	BenaphoreLocker locker(&fLock);

	int32 index = (address - fBase) >> RangeShift();
	fFreeList[index] = fNextFree;
	fNextFree = index;
}


//	#pragma mark -


/* static */
int
block_range::Compare(void *_blockRange, const void *_address)
{
	block_range *range = (block_range *)_blockRange;
	addr_t address = (addr_t)_address;

	return ((range->base - sBlockAddressPool.BaseAddress()) >> sBlockAddressPool.RangeShift())
		== ((address - sBlockAddressPool.BaseAddress()) >> sBlockAddressPool.RangeShift());
}


/* static */
uint32
block_range::Hash(void *_blockRange, const void *_address, uint32 range)
{
	block_range *blockRange = (block_range *)_blockRange;
	addr_t address = (addr_t)_address;

	if (blockRange != NULL)
		return ((blockRange->base - sBlockAddressPool.BaseAddress())
			>> sBlockAddressPool.RangeShift()) % range;

	return ((address - sBlockAddressPool.BaseAddress())
		>> sBlockAddressPool.RangeShift()) % range;
}


/* static */
status_t
block_range::NewBlockRange(block_cache *cache, block_range **_range)
{
	addr_t address = sBlockAddressPool.Get();
	if (address == NULL)
		return B_ENTRY_NOT_FOUND;

	block_range *range = (block_range *)malloc(sizeof(block_range)
		+ cache->chunks_per_range * sizeof(block_chunk));
	if (range == NULL)
		return B_NO_MEMORY;

	memset(range, 0, sizeof(block_range) + cache->chunks_per_range * sizeof(block_chunk));
	range->base = address;

	*_range = range;
	return B_OK;
}


uint32
block_range::BlockIndex(block_cache *cache, void *address)
{
	return (((addr_t)address - base) % cache->chunk_size) / cache->block_size;
}


uint32
block_range::ChunkIndex(block_cache *cache, void *address)
{
	return ((addr_t)address - base) / cache->chunk_size;
}


block_chunk *
block_range::Chunk(block_cache *cache, void *address)
{
	return &chunks[ChunkIndex(cache, address)];
}


status_t
block_range::Allocate(block_cache *cache, cached_block *block)
{
	block_chunk *chunk;

	void *address = Allocate(cache, &chunk);
	if (address == NULL)
		return B_NO_MEMORY;

	block->data = address;

	// add the block to the chunk
	block->chunk_next = chunk->blocks;
	chunk->blocks = block;

	return B_OK;
}


void
block_range::Free(block_cache *cache, cached_block *block)
{
	Free(cache, block->data);
	block_chunk *chunk = Chunk(cache, block->data);

	// remove block from list

	cached_block *last = NULL, *next = chunk->blocks;
	while (next != NULL && next != block) {
		last = next;
		next = next->chunk_next;
	}
	if (next == NULL) {
		panic("cached_block %p was not in chunk %p\n", block, chunk);
	} else {
		if (last)
			last->chunk_next = block->chunk_next;
		else
			chunk->blocks = block->chunk_next;
	}

	block->data = NULL;
}


void *
block_range::Allocate(block_cache *cache, block_chunk **_chunk)
{
	// get free chunk
	
	uint32 chunk;
	for (chunk = 0; chunk < cache->chunks_per_range; chunk++) {
		if ((used_mask & (1UL << chunk)) == 0)
			break;
	}
	if (chunk == cache->chunks_per_range)
		return NULL;

	// get free block in chunk

	uint32 numBlocks = cache->chunk_size / cache->block_size;
	uint32 i;
	for (i = 0; i < numBlocks; i++) {
		if ((chunks[chunk].used_mask & (1UL << i)) == 0)
			break;
	}
	if (i == numBlocks) {
		panic("block_chunk %lu in range %p pretended to be free but isn't\n", i, this);
		return NULL;
	}

	if (!chunks[chunk].mapped) {
		// allocate pages if needed
		uint32 numPages = cache->chunk_size / B_PAGE_SIZE;
		uint32 pageBaseIndex = numPages * chunk;
		if (pages[pageBaseIndex] == NULL) {
			// there are no pages for us yet
			for (uint32 i = 0; i < numPages; i++) {
				vm_page *page = vm_page_allocate_page(PAGE_STATE_FREE);
				if (page == NULL) {
					// ToDo: handle this gracefully
					panic("no memory for block!!\n");
					return NULL;
				}

				pages[pageBaseIndex + i] = page;
			}
		}

		// map the memory

		vm_address_space *addressSpace = vm_get_kernel_aspace();
		vm_translation_map *map = &addressSpace->translation_map;
		map->ops->lock(map);

		for (uint32 i = 0; i < numPages; i++) {
			map->ops->map(map, base + chunk * cache->chunk_size + i * B_PAGE_SIZE,
				pages[pageBaseIndex + i]->ppn * B_PAGE_SIZE,
				B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		}

		map->ops->unlock(map);
		vm_put_aspace(addressSpace);

		chunks[chunk].mapped = true;
	}

	chunks[chunk].used_mask |= 1UL << i;
	if (chunks[chunk].used_mask == (1UL << numBlocks) - 1) {
		// all blocks are used in this chunk, propagate usage bit
		used_mask |= 1UL << chunk;

		if (used_mask == cache->range_mask) {
			// range is full, remove it from the free list

			// usually, the first entry will be us, but we don't count on it
			block_range *last = NULL, *range = cache->free_ranges;
			while (range != NULL && range != this) {
				last = range;
				range = range->free_next;
			}
			if (range == NULL) {
				panic("block_range %p was free but not in the free list\n", this);
			} else {
				if (last)
					last->free_next = free_next;
				else
					cache->free_ranges = free_next;
			}
		}
	}

	if (_chunk)
		*_chunk = &chunks[chunk];
	return (void *)(base + cache->chunk_size * chunk + cache->block_size * i);
}


void
block_range::Free(block_cache *cache, void *address)
{
	uint32 chunk = ChunkIndex(cache, address);

	if (chunks[chunk].used_mask == cache->chunk_mask) {
		if (used_mask == cache->range_mask) {
			// range was full before, add it to the free list
			free_next = cache->free_ranges;
			cache->free_ranges = this;
		}
		// chunk was full before, propagate usage bit to range
		used_mask &= ~(1UL << chunk);
	}
	chunks[chunk].used_mask |= BlockIndex(cache, address);
}


//	#pragma mark -


extern "C" status_t
init_block_allocator(void)
{
	new(&sBlockAddressPool) BlockAddressPool;
		// static initializers do not work in the kernel,
		// so we have to do it here, manually

	return sBlockAddressPool.InitCheck();
}

/*
 * Copyright 2002-2005, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <OS.h>
#include <KernelExport.h>

#include <kerrors.h>
#include <vm.h>
#include <vm_priv.h>
#include <vm_page.h>
#include <vm_cache.h>
#include <vm_store_anonymous_noswap.h>
#include <vm_store_device.h>
#include <vm_store_null.h>
#include <file_cache.h>
#include <memheap.h>
#include <debug.h>
#include <console.h>
#include <int.h>
#include <smp.h>
#include <lock.h>
#include <thread.h>
#include <team.h>

#include <boot/stage2.h>
#include <boot/elf.h>

#include <arch/cpu.h>
#include <arch/vm.h>

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

//#define TRACE_VM
//#define TRACE_FAULTS
#ifdef TRACE_VM
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif
#ifdef TRACE_FAULTS
#	define FTRACE(x) dprintf x
#else
#	define FTRACE(x) ;
#endif

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define ROUNDOWN(a, b) (((a) / (b)) * (b))

extern vm_address_space *kernel_aspace;

#define REGION_HASH_TABLE_SIZE 1024
static area_id sNextAreaID;
static void *sAreaHash;
static sem_id sAreaHashLock;

static off_t sAvailableMemory;
static benaphore sAvailableMemoryLock;

// function declarations
static vm_area *_vm_create_region_struct(vm_address_space *aspace, const char *name, int wiring, int lock);
static status_t map_backing_store(vm_address_space *aspace, vm_store *store, void **vaddr,
	off_t offset, addr_t size, uint32 addressSpec, int wiring, int lock, int mapping, vm_area **_area, const char *area_name);
static status_t vm_soft_fault(addr_t address, bool is_write, bool is_user);
static vm_area *vm_virtual_map_lookup(vm_virtual_map *map, addr_t address);
static void vm_put_area(vm_area *area);


static int
area_compare(void *_r, const void *key)
{
	vm_area *r = _r;
	const area_id *id = key;

	if (r->id == *id)
		return 0;

	return -1;
}


static uint32
area_hash(void *_r, const void *key, uint32 range)
{
	vm_area *r = _r;
	const area_id *id = key;

	if (r != NULL)
		return r->id % range;

	return *id % range;
}


static vm_area *
vm_get_area(area_id id)
{
	vm_area *area;

	acquire_sem_etc(sAreaHashLock, READ_COUNT, 0, 0);

	area = hash_lookup(sAreaHash, &id);
	if (area)
		atomic_add(&area->ref_count, 1);

	release_sem_etc(sAreaHashLock, READ_COUNT, 0);

	return area;
}


static vm_area *
_vm_create_reserved_region_struct(vm_virtual_map *map)
{
	vm_area *reserved = malloc(sizeof(vm_area));
	if (reserved == NULL)
		return NULL;

	memset(reserved, 0, sizeof(vm_area));
	reserved->id = RESERVED_REGION_ID;
		// this marks it as reserved space
	reserved->map = map;

	return reserved;
}


static vm_area *
_vm_create_area_struct(vm_address_space *aspace, const char *name, uint32 wiring, uint32 protection)
{
	vm_area *area = NULL;

	// restrict the area name to B_OS_NAME_LENGTH
	size_t length = strlen(name) + 1;
	if (length > B_OS_NAME_LENGTH)
		length = B_OS_NAME_LENGTH;

	area = (vm_area *)malloc(sizeof(vm_area));
	if (area == NULL)
		return NULL;

	area->name = (char *)malloc(length);
	if (area->name == NULL) {
		free(area);
		return NULL;
	}
	strlcpy(area->name, name, length);

	area->id = atomic_add(&sNextAreaID, 1);
	area->base = 0;
	area->size = 0;
	area->protection = protection;
	area->wiring = wiring;
	area->ref_count = 1;

	area->cache_ref = NULL;
	area->cache_offset = 0;

	area->aspace = aspace;
	area->aspace_next = NULL;
	area->map = &aspace->virtual_map;
	area->cache_next = area->cache_prev = NULL;
	area->hash_next = NULL;

	return area;
}


/**	Finds a reserved area that covers the region spanned by \a start and
 *	\a size, inserts the \a area into that region and makes sure that
 *	there are reserved regions for the remaining parts.
 */

static status_t
find_reserved_area(vm_virtual_map *map, addr_t start, addr_t size, vm_area *area)
{
	vm_area *next, *last = NULL;

	next = map->areas;
	while (next) {
		if (next->base <= start && next->base + next->size >= start + size) {
			// this area covers the requested range
			if (next->id != RESERVED_REGION_ID) {
				// but it's not reserved space, it's a real area
				return B_BAD_VALUE;
			}

			break;
		}
		last = next;
		next = next->aspace_next;
	}
	if (next == NULL)
		return B_ENTRY_NOT_FOUND;

	// now we have to transfer the requested part of the reserved
	// range to the new area - and remove, resize or split the old
	// reserved area.
	
	if (start == next->base) {
		// the area starts at the beginning of the reserved range
		if (last)
			last->aspace_next = area;
		else
			map->areas = area;

		if (size == next->size) {
			// the new area fully covers the reversed range
			area->aspace_next = next->aspace_next;
			free(next);
		} else {
			// resize the reserved range behind the area
			area->aspace_next = next;
			next->base += size;
			next->size -= size;
		}
	} else if (start + size == next->base + next->size) {
		// the area is at the end of the reserved range
		area->aspace_next = next->aspace_next;
		next->aspace_next = area;

		// resize the reserved range before the area
		next->size = start - next->base;
	} else {
		// the area splits the reserved range into two separate ones
		// we need a new reserved area to cover this space
		vm_area *reserved = _vm_create_reserved_region_struct(map);
		if (reserved == NULL)
			return B_NO_MEMORY;

		reserved->aspace_next = next->aspace_next;
		area->aspace_next = reserved;
		next->aspace_next = area;

		// resize regions
		reserved->size = next->base + next->size - start - size;
		next->size = start - next->base;
		reserved->base = start + size;
	}

	area->base = start;
	area->size = size;
	map->change_count++;

	return B_OK;
}


// must be called with this address space's virtual_map.sem held

static status_t
find_and_insert_area_slot(vm_virtual_map *map, addr_t start, addr_t size, addr_t end,
	uint32 addressSpec, vm_area *area)
{
	vm_area *last = NULL;
	vm_area *next;
	bool foundspot = false;

	TRACE(("find_and_insert_region_slot: map %p, start 0x%lx, size %ld, end 0x%lx, addressSpec %ld, area %p\n",
		map, start, size, end, addressSpec, area));

	// do some sanity checking
	if (start < map->base || size == 0
		|| (end - 1) > (map->base + (map->size - 1))
		|| start + size > end)
		return B_BAD_ADDRESS;

	if (addressSpec == B_EXACT_ADDRESS) {
		// search for a reserved area
		status_t status = find_reserved_area(map, start, size, area);
		if (status == B_OK || status == B_BAD_VALUE)
			return status;

		// there was no reserved area, and the slot doesn't seem to be used already
		// ToDo: this could be further optimized.
	}

	// walk up to the spot where we should start searching
second_chance:
	next = map->areas;
	while (next) {
		if (next->base >= start + size) {
			// we have a winner
			break;
		}
		last = next;
		next = next->aspace_next;
	}

	switch (addressSpec) {
		case B_ANY_ADDRESS:
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
			// find a hole big enough for a new area
			if (!last) {
				// see if we can build it at the beginning of the virtual map
				if (!next || (next->base >= map->base + size)) {
					foundspot = true;
					area->base = map->base;
					break;
				}
				last = next;
				next = next->aspace_next;
			}
			// keep walking
			while (next) {
				if (next->base >= last->base + last->size + size) {
					// we found a spot (it'll be filled up below)
					break;
				}
				last = next;
				next = next->aspace_next;
			}

			if ((map->base + (map->size - 1)) >= (last->base + last->size + (size - 1))) {
				// got a spot
				foundspot = true;
				area->base = last->base + last->size;
				break;
			}
			break;

		case B_BASE_ADDRESS:
			// find a hole big enough for a new area beginning with "start"
			if (!last) {
				// see if we can build it at the beginning of the specified start
				if (!next || (next->base >= start + size)) {
					foundspot = true;
					area->base = start;
					break;
				}
				last = next;
				next = next->aspace_next;
			}
			// keep walking
			while (next) {
				if (next->base >= last->base + last->size + size) {
					// we found a spot (it'll be filled up below)
					break;
				}
				last = next;
				next = next->aspace_next;
			}

			if ((map->base + (map->size - 1)) >= (last->base + last->size + (size - 1))) {
				// got a spot
				foundspot = true;
				if (last->base + last->size <= start)
					area->base = start;
				else
					area->base = last->base + last->size;
				break;
			}
			// we didn't find a free spot in the requested range, so we'll
			// try again without any restrictions
			start = map->base;
			addressSpec = B_ANY_ADDRESS;
			last = NULL;
			goto second_chance;

		case B_EXACT_ADDRESS:
			// see if we can create it exactly here
			if (!last) {
				if (!next || (next->base >= start + size)) {
					foundspot = true;
					area->base = start;
					break;
				}
			} else {
				if (next) {
					if (last->base + last->size <= start && next->base >= start + size) {
						foundspot = true;
						area->base = start;
						break;
					}
				} else {
					if ((last->base + (last->size - 1)) <= start - 1) {
						foundspot = true;
						area->base = start;
					}
				}
			}
			break;
		default:
			return B_BAD_VALUE;
	}

	if (!foundspot)
		return addressSpec == B_EXACT_ADDRESS ? B_BAD_VALUE : B_NO_MEMORY;

	area->size = size;
	if (last) {
		area->aspace_next = last->aspace_next;
		last->aspace_next = area;
	} else {
		area->aspace_next = map->areas;
		map->areas = area;
	}
	map->change_count++;
	return B_OK;
}


/**	This inserts the area you pass into the virtual_map of the
 *	specified address space.
 *	It will also set the "_address" argument to its base address when
 *	the call succeeds.
 *	You need to hold the virtual_map semaphore.
 */

static status_t
insert_area(vm_address_space *addressSpace, void **_address,
	uint32 addressSpec, addr_t size, vm_area *area)
{
	addr_t searchBase, searchEnd;
	status_t status;

	switch (addressSpec) {
		case B_EXACT_ADDRESS:
			searchBase = (addr_t)*_address;
			searchEnd = (addr_t)*_address + size;
			break;

		case B_BASE_ADDRESS:
			searchBase = (addr_t)*_address;
			searchEnd = addressSpace->virtual_map.base + (addressSpace->virtual_map.size - 1);
			break;

		case B_ANY_ADDRESS:
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
			searchBase = addressSpace->virtual_map.base;
			searchEnd = addressSpace->virtual_map.base + (addressSpace->virtual_map.size - 1);
			break;

		default:
			return B_BAD_VALUE;
	}

	status = find_and_insert_area_slot(&addressSpace->virtual_map, searchBase, size,
				searchEnd, addressSpec, area);
	if (status == B_OK)
		// ToDo: do we have to do anything about B_ANY_KERNEL_ADDRESS
		//		vs. B_ANY_KERNEL_BLOCK_ADDRESS here?
		*_address = (void *)area->base;

	return status;
}


// a ref to the cache holding this store must be held before entering here
static status_t
map_backing_store(vm_address_space *aspace, vm_store *store, void **_virtualAddress,
	off_t offset, addr_t size, uint32 addressSpec, int wiring, int protection,
	int mapping, vm_area **_area, const char *areaName)
{
	vm_cache *cache;
	vm_cache_ref *cache_ref;
	vm_area *area;
	vm_cache *nu_cache;
	vm_cache_ref *nu_cache_ref = NULL;
	vm_store *nu_store;

	int err;

	TRACE(("map_backing_store: aspace %p, store %p, *vaddr %p, offset 0x%Lx, size %lu, addressSpec %ld, wiring %d, protection %d, _area %p, area_name '%s'\n",
		aspace, store, *_virtualAddress, offset, size, addressSpec, wiring, protection, _area, areaName));

	area = _vm_create_area_struct(aspace, areaName, wiring, protection);
	if (area == NULL)
		return B_NO_MEMORY;

	cache = store->cache;
	cache_ref = cache->ref;

	// if this is a private map, we need to create a new cache & store object
	// pair to handle the private copies of pages as they are written to
	if (mapping == REGION_PRIVATE_MAP) {
		// create an anonymous store object
		nu_store = vm_store_create_anonymous_noswap((protection & B_STACK_AREA) != 0, USER_STACK_GUARD_PAGES);
		if (nu_store == NULL)
			panic("map_backing_store: vm_create_store_anonymous_noswap returned NULL");
		nu_cache = vm_cache_create(nu_store);
		if (nu_cache == NULL)
			panic("map_backing_store: vm_cache_create returned NULL");
		nu_cache_ref = vm_cache_ref_create(nu_cache);
		if (nu_cache_ref == NULL)
			panic("map_backing_store: vm_cache_ref_create returned NULL");
		nu_cache->temporary = 1;
		nu_cache->scan_skip = cache->scan_skip;

		nu_cache->source = cache;

		// grab a ref to the cache object we're now linked to as a source
		vm_cache_acquire_ref(cache_ref, true);

		cache = nu_cache;
		cache_ref = cache->ref;
		store = nu_store;
		cache->virtual_size = offset + size;
	}

	err = vm_cache_set_minimal_commitment(cache_ref, offset + size);
	if (err != B_OK)
		goto err1a;

	vm_cache_acquire_ref(cache_ref, true);

	acquire_sem_etc(aspace->virtual_map.sem, WRITE_COUNT, 0, 0);

	// check to see if this aspace has entered DELETE state
	if (aspace->state == VM_ASPACE_STATE_DELETION) {
		// okay, someone is trying to delete this aspace now, so we can't
		// insert the area, so back out
		err = ERR_VM_INVALID_ASPACE;
		goto err1b;
	}

	err = insert_area(aspace, _virtualAddress, addressSpec, size, area);
	if (err < B_OK)
		goto err1b;

	// attach the cache to the area
	area->cache_ref = cache_ref;
	area->cache_offset = offset;
	// point the cache back to the area
	vm_cache_insert_area(cache_ref, area);

	// insert the area in the global area hash table
	acquire_sem_etc(sAreaHashLock, WRITE_COUNT, 0 ,0);
	hash_insert(sAreaHash, area);
	release_sem_etc(sAreaHashLock, WRITE_COUNT, 0);

	// grab a ref to the aspace (the area holds this)
	atomic_add(&aspace->ref_count, 1);

	release_sem_etc(aspace->virtual_map.sem, WRITE_COUNT, 0);

	*_area = area;
	return B_OK;

err1b:
	release_sem_etc(aspace->virtual_map.sem, WRITE_COUNT, 0);
	vm_cache_release_ref(cache_ref);
	goto err;
err1a:
	if (nu_cache_ref) {
		// had never acquired it's initial ref, so acquire and then release it
		// this should clean up all the objects it references
		vm_cache_acquire_ref(cache_ref, true);
		vm_cache_release_ref(cache_ref);
	}
err:
	free(area->name);
	free(area);
	return err;
}


status_t
vm_unreserve_address_range(aspace_id aid, void *address, addr_t size)
{
	vm_address_space *addressSpace;
	vm_area *area, *last = NULL;
	status_t status = B_OK;

	addressSpace = vm_get_aspace_by_id(aid);
	if (addressSpace == NULL)
		return ERR_VM_INVALID_ASPACE;

	acquire_sem_etc(addressSpace->virtual_map.sem, WRITE_COUNT, 0, 0);

	// check to see if this aspace has entered DELETE state
	if (addressSpace->state == VM_ASPACE_STATE_DELETION) {
		// okay, someone is trying to delete this aspace now, so we can't
		// insert the area, so back out
		status = ERR_VM_INVALID_ASPACE;
		goto out;
	}

	// search area list and remove any matching reserved ranges

	area = addressSpace->virtual_map.areas;
	while (area) {
		// the area must be completely part of the reserved range
		if (area->id == RESERVED_REGION_ID && area->base >= (addr_t)address
			&& area->base + area->size <= (addr_t)address + size) {
			// remove reserved range
			vm_area *reserved = area;
			if (last)
				last->aspace_next = reserved->aspace_next;
			else
				addressSpace->virtual_map.areas = reserved->aspace_next;

			area = reserved->aspace_next;
			free(reserved);
			continue;
		}

		last = area;
		area = area->aspace_next;
	}
	
out:
	release_sem_etc(addressSpace->virtual_map.sem, WRITE_COUNT, 0);
	vm_put_aspace(addressSpace);
	return status;
}


status_t
vm_reserve_address_range(aspace_id aid, void **_address, uint32 addressSpec, addr_t size)
{
	vm_address_space *addressSpace;
	vm_area *area;
	status_t status = B_OK;

	if (size == 0)
		return B_BAD_VALUE;

	addressSpace = vm_get_aspace_by_id(aid);
	if (addressSpace == NULL)
		return ERR_VM_INVALID_ASPACE;

	area = _vm_create_reserved_region_struct(&addressSpace->virtual_map);
	if (area == NULL) {
		status = B_NO_MEMORY;
		goto err1;
	}

	acquire_sem_etc(addressSpace->virtual_map.sem, WRITE_COUNT, 0, 0);

	// check to see if this aspace has entered DELETE state
	if (addressSpace->state == VM_ASPACE_STATE_DELETION) {
		// okay, someone is trying to delete this aspace now, so we can't
		// insert the area, so back out
		status = ERR_VM_INVALID_ASPACE;
		goto err2;
	}

	status = insert_area(addressSpace, _address, addressSpec, size, area);
	if (status < B_OK)
		goto err2;

	// the area is now reserved!

	release_sem_etc(addressSpace->virtual_map.sem, WRITE_COUNT, 0);
	return B_OK;

err2:
	release_sem_etc(addressSpace->virtual_map.sem, WRITE_COUNT, 0);
	free(area);
err1:
	vm_put_aspace(addressSpace);
	return status;
}


area_id
vm_create_anonymous_area(aspace_id aid, const char *name, void **address, 
	uint32 addressSpec, addr_t size, uint32 wiring, uint32 protection)
{
	vm_area *area;
	vm_cache *cache;
	vm_store *store;
	vm_address_space *aspace;
	vm_cache_ref *cache_ref;
	vm_page *page = NULL;
	bool isStack = (protection & B_STACK_AREA) != 0;
	status_t err;

	TRACE(("create_anonymous_area %s: size 0x%lx\n", name, size));

#ifdef DEBUG_KERNEL_STACKS
	if ((protection & B_KERNEL_STACK_AREA) != 0)
		isStack = true;
#endif

	/* check parameters */
	switch (addressSpec) {
		case B_ANY_ADDRESS:
		case B_EXACT_ADDRESS:
		case B_BASE_ADDRESS:
		case B_ANY_KERNEL_ADDRESS:
			break;

		default:
			return B_BAD_VALUE;
	}

	switch (wiring) {
		case B_NO_LOCK:
		case B_FULL_LOCK:
		case B_LAZY_LOCK:
		case B_CONTIGUOUS:
		case B_ALREADY_WIRED:
			break;
		case B_LOMEM:
		//case B_SLOWMEM:
			dprintf("B_LOMEM/SLOWMEM is not yet supported!\n");
			wiring = B_FULL_LOCK;
			break;
		default:
			return B_BAD_VALUE;
	}

	aspace = vm_get_aspace_by_id(aid);
	if (aspace == NULL)
		return ERR_VM_INVALID_ASPACE;

	size = PAGE_ALIGN(size);

	if (wiring == B_CONTIGUOUS) {
		// we try to allocate the page run here upfront as this may easily fail for obvious reasons
		page = vm_page_allocate_page_run(PAGE_STATE_CLEAR, size / B_PAGE_SIZE);
		if (page == NULL) {
			vm_put_aspace(aspace);
			return B_NO_MEMORY;
		}
	}

	// create an anonymous store object
	store = vm_store_create_anonymous_noswap(isStack, (protection & B_USER_PROTECTION) != 0 ? 
		USER_STACK_GUARD_PAGES : KERNEL_STACK_GUARD_PAGES);
	if (store == NULL)
		panic("vm_create_anonymous_area: vm_create_store_anonymous_noswap returned NULL");
	cache = vm_cache_create(store);
	if (cache == NULL)
		panic("vm_create_anonymous_area: vm_cache_create returned NULL");
	cache_ref = vm_cache_ref_create(cache);
	if (cache_ref == NULL)
		panic("vm_create_anonymous_area: vm_cache_ref_create returned NULL");
	cache->temporary = 1;

	switch (wiring) {
		case B_LAZY_LOCK:	// for now
		case B_FULL_LOCK:
		case B_CONTIGUOUS:
		case B_ALREADY_WIRED:
			cache->scan_skip = 1;
			break;
		case B_NO_LOCK:
		//case B_LAZY_LOCK:
			cache->scan_skip = 0;
			break;
	}

//	dprintf("create_anonymous_area: calling map_backing store\n");

	vm_cache_acquire_ref(cache_ref, true);
	err = map_backing_store(aspace, store, address, 0, size, addressSpec, wiring, protection, REGION_NO_PRIVATE_MAP, &area, name);
	vm_cache_release_ref(cache_ref);
	if (err < 0) {
		vm_put_aspace(aspace);

		if (wiring == B_CONTIGUOUS) {
			// we had reserved the area space upfront...
			addr_t pageNumber = page->ppn;
			int32 i;
			for (i = size / B_PAGE_SIZE; i-- > 0; pageNumber++) {
				page = vm_lookup_page(pageNumber);
				if (page == NULL)
					panic("couldn't lookup physical page just allocated\n");

				vm_page_set_state(page, PAGE_STATE_FREE);
			}
		}	
		return err;
	}

//	dprintf("create_anonymous_area: done calling map_backing store\n");

	cache_ref = store->cache->ref;
	switch (wiring) {
		case B_NO_LOCK:
		case B_LAZY_LOCK:
			break; // do nothing

		case B_FULL_LOCK:
		{
			// pages aren't mapped at this point, but we just simulate a fault on
			// every page, which should allocate them
			addr_t va;
			// XXX remove
			for (va = area->base; va < area->base + area->size; va += B_PAGE_SIZE) {
#ifdef DEBUG_KERNEL_STACKS
#	ifdef STACK_GROWS_DOWNWARDS
				if (isStack && va < area->base + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE)
#	else
				if (isStack && va >= area->base + area->size - KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE)
#	endif
					continue;
#endif
				vm_soft_fault(va, false, false);
			}
			break;
		}

		case B_ALREADY_WIRED:
		{
			// the pages should already be mapped. This is only really useful during
			// boot time. Find the appropriate vm_page objects and stick them in
			// the cache object.
			addr_t va;
			addr_t pa;
			uint32 flags;
			int err;
			off_t offset = 0;

			if (!kernel_startup)
				panic("ALREADY_WIRED flag used outside kernel startup\n");

			mutex_lock(&cache_ref->lock);
			(*aspace->translation_map.ops->lock)(&aspace->translation_map);
			for (va = area->base; va < area->base + area->size; va += B_PAGE_SIZE, offset += B_PAGE_SIZE) {
				err = (*aspace->translation_map.ops->query)(&aspace->translation_map, va, &pa, &flags);
				if (err < 0) {
//					dprintf("vm_create_anonymous_area: error looking up mapping for va 0x%x\n", va);
					continue;
				}
				page = vm_lookup_page(pa / B_PAGE_SIZE);
				if (page == NULL) {
//					dprintf("vm_create_anonymous_area: error looking up vm_page structure for pa 0x%x\n", pa);
					continue;
				}
				atomic_add(&page->ref_count, 1);
				vm_page_set_state(page, PAGE_STATE_WIRED);
				vm_cache_insert_page(cache_ref, page, offset);
			}
			(*aspace->translation_map.ops->unlock)(&aspace->translation_map);
			mutex_unlock(&cache_ref->lock);
			break;
		}

		case B_CONTIGUOUS:
		{
			addr_t physicalAddress = page->ppn * B_PAGE_SIZE;
			addr_t virtualAddress;
			off_t offset = 0;

			mutex_lock(&cache_ref->lock);
			(*aspace->translation_map.ops->lock)(&aspace->translation_map);

			for (virtualAddress = area->base; virtualAddress < area->base + area->size;
					virtualAddress += B_PAGE_SIZE, offset += B_PAGE_SIZE, physicalAddress += B_PAGE_SIZE) {
				page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
				if (page == NULL)
					panic("couldn't lookup physical page just allocated\n");

				atomic_add(&page->ref_count, 1);
				err = (*aspace->translation_map.ops->map)(&aspace->translation_map,
							virtualAddress, physicalAddress, protection);
				if (err < 0)
					panic("couldn't map physical page in page run\n");

				vm_page_set_state(page, PAGE_STATE_WIRED);
				vm_cache_insert_page(cache_ref, page, offset);
			}

			(*aspace->translation_map.ops->unlock)(&aspace->translation_map);
			mutex_unlock(&cache_ref->lock);
			break;
		}

		default:
			break;
	}
	vm_put_aspace(aspace);

//	dprintf("create_anonymous_area: done\n");

	if (area == NULL)
		return B_NO_MEMORY;

	return area->id;
}


area_id
vm_map_physical_memory(aspace_id aid, const char *name, void **_address,
	uint32 addressSpec, addr_t size, uint32 protection, addr_t phys_addr)
{
	vm_area *area;
	vm_cache *cache;
	vm_cache_ref *cache_ref;
	vm_store *store;
	addr_t map_offset;
	int err;
	vm_address_space *aspace = vm_get_aspace_by_id(aid);

	TRACE(("vm_map_physical_memory(aspace = %ld, \"%s\", virtual = %p, spec = %ld,"
		" size = %lu, protection = %ld, phys = %p)\n",
		aid, name, _address, addressSpec, size, protection, (void *)phys_addr));

	if (aspace == NULL)
		return ERR_VM_INVALID_ASPACE;

	// if the physical address is somewhat inside a page,
	// move the actual area down to align on a page boundary
	map_offset = phys_addr % B_PAGE_SIZE;
	size += map_offset;
	phys_addr -= map_offset;

	size = PAGE_ALIGN(size);

	// create an device store object
	store = vm_store_create_device(phys_addr);
	if (store == NULL)
		panic("vm_map_physical_memory: vm_store_create_device returned NULL");
	cache = vm_cache_create(store);
	if (cache == NULL)
		panic("vm_map_physical_memory: vm_cache_create returned NULL");
	cache_ref = vm_cache_ref_create(cache);
	if (cache_ref == NULL)
		panic("vm_map_physical_memory: vm_cache_ref_create returned NULL");
	// tell the page scanner to skip over this area, it's pages are special
	cache->scan_skip = 1;

	vm_cache_acquire_ref(cache_ref, true);
	err = map_backing_store(aspace, store, _address, 0, size, addressSpec, 0, protection, REGION_NO_PRIVATE_MAP, &area, name);
	vm_cache_release_ref(cache_ref);
	vm_put_aspace(aspace);
	if (err < 0)
		return err;

	// modify the pointer returned to be offset back into the new area
	// the same way the physical address in was offset
	*_address = (void *)((addr_t)*_address + map_offset);

	return area->id;
}


area_id
vm_create_null_area(aspace_id aid, const char *name, void **address, uint32 addressSpec, addr_t size)
{
	vm_area *area;
	vm_cache *cache;
	vm_cache_ref *cache_ref;
	vm_store *store;
//	addr_t map_offset;
	int err;

	vm_address_space *aspace = vm_get_aspace_by_id(aid);
	if (aspace == NULL)
		return ERR_VM_INVALID_ASPACE;

	size = PAGE_ALIGN(size);

	// create an null store object
	store = vm_store_create_null();
	if (store == NULL)
		panic("vm_map_physical_memory: vm_store_create_null returned NULL");
	cache = vm_cache_create(store);
	if (cache == NULL)
		panic("vm_map_physical_memory: vm_cache_create returned NULL");
	cache_ref = vm_cache_ref_create(cache);
	if (cache_ref == NULL)
		panic("vm_map_physical_memory: vm_cache_ref_create returned NULL");
	// tell the page scanner to skip over this area, no pages will be mapped here
	cache->scan_skip = 1;

	vm_cache_acquire_ref(cache_ref, true);
	err = map_backing_store(aspace, store, address, 0, size, addressSpec, 0, B_KERNEL_READ_AREA, REGION_NO_PRIVATE_MAP, &area, name);
	vm_cache_release_ref(cache_ref);
	vm_put_aspace(aspace);
	if (err < 0)
		return err;

	return area->id;
}


status_t
vm_create_vnode_cache(void *vnode, struct vm_cache_ref **_cacheRef)
{
	vm_cache_ref *cacheRef;
	vm_cache *cache;
	vm_store *store;

	// create a vnode store object
	store = vm_create_vnode_store(vnode);
	if (store == NULL) {
		dprintf("vm_create_vnode_cache: couldn't create vnode store\n");
		return B_NO_MEMORY;
	}

	cache = vm_cache_create(store);
	if (cache == NULL) {
		dprintf("vm_create_vnode_cache: vm_cache_create returned NULL\n");
		return B_NO_MEMORY;
	}

	cacheRef = vm_cache_ref_create(cache);
	if (cacheRef == NULL) {
		dprintf("vm_create_vnode_cache: vm_cache_ref_create returned NULL\n");
		return B_NO_MEMORY;
	}

	// acquire the cache ref once to represent the ref that the vnode will have
	// this is one of the only places where we dont want to ref to ripple down to the store
	vm_cache_acquire_ref(cacheRef, false);

	*_cacheRef = cacheRef;
	return B_OK;
}


/** Will map the file at the path specified by \a name to an area in memory.
 *	The file will be mirrored beginning at the specified \a offset. The \a offset
 *	and \a size arguments have to be page aligned.
 */

static area_id
_vm_map_file(aspace_id aid, const char *name, void **_address, uint32 addressSpec,
	size_t size, uint32 protection, uint32 mapping, const char *path, off_t offset, bool kernel)
{
	vm_cache_ref *cacheRef;
	vm_area *area;
	void *vnode;
	status_t status;

	// ToDo: maybe attach to an FD, not a path (or both, like VFS calls)
	// ToDo: check file access permissions (would be already done if the above were true)
	// ToDo: for binary files, we want to make sure that they get the
	//	copy of a file at a given time, ie. later changes should not
	//	make it into the mapped copy -- this will need quite some changes
	//	to be done in a nice way

	vm_address_space *aspace = vm_get_aspace_by_id(aid);
	if (aspace == NULL)
		return ERR_VM_INVALID_ASPACE;

	TRACE(("_vm_map_file(\"%s\", offset = %Ld, size = %lu, mapping %ld)\n", path, offset, size, mapping));

	offset = ROUNDOWN(offset, B_PAGE_SIZE);
	size = PAGE_ALIGN(size);

	// get the vnode for the object, this also grabs a ref to it
	status = vfs_get_vnode_from_path(path, kernel, &vnode);
	if (status < B_OK)
		goto err1;

	status = vfs_get_vnode_cache(vnode, &cacheRef);
	if (status < B_OK)
		goto err2;

	// acquire a ref to the cache before we do work on it. Dont ripple the ref acquision to the vnode
	// below because we'll have to release it later anyway, since we grabbed a ref to the vnode at
	// vfs_get_vnode_from_path(). This puts the ref counts in sync.
	vm_cache_acquire_ref(cacheRef, false);
	status = map_backing_store(aspace, cacheRef->cache->store, _address, offset, size,
					addressSpec, 0, protection, mapping, &area, name);
	vm_cache_release_ref(cacheRef);
	vm_put_aspace(aspace);

	if (status < B_OK)
		return status;

	return area->id;

err2:
	vfs_vnode_release_ref(vnode);
err1:
	vm_put_aspace(aspace);
	return status;
}


area_id
vm_map_file(aspace_id aid, const char *name, void **address, uint32 addressSpec,
	addr_t size, uint32 protection, uint32 mapping, const char *path, off_t offset)
{
	return _vm_map_file(aid, name, address, addressSpec, size, protection, mapping, path, offset, true);
}


// ToDo: create a BeOS style call for this!

area_id
_user_vm_map_file(const char *uname, void **uaddress, int addressSpec,
	addr_t size, int protection, int mapping, const char *upath, off_t offset)
{
	char name[B_OS_NAME_LENGTH];
	char path[B_PATH_NAME_LENGTH];
	void *address;
	int rc;

	if (!IS_USER_ADDRESS(uname) || !IS_USER_ADDRESS(uaddress) || !IS_USER_ADDRESS(upath)
		|| user_strlcpy(name, uname, B_OS_NAME_LENGTH) < B_OK
		|| user_strlcpy(path, upath, B_PATH_NAME_LENGTH) < B_OK
		|| user_memcpy(&address, uaddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	// userland created areas can always be accessed by the kernel
	protection |= B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA;

	rc = _vm_map_file(vm_get_current_user_aspace_id(), name, &address, addressSpec, size,
			protection, mapping, path, offset, false);
	if (rc < 0)
		return rc;

	if (user_memcpy(uaddress, &address, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	return rc;
}


area_id
vm_clone_area(aspace_id aid, const char *name, void **address, uint32 addressSpec,
	uint32 protection, uint32 mapping, area_id sourceID)
{
	vm_area *newArea = NULL;
	vm_area *sourceArea;
	status_t status;

	vm_address_space *aspace = vm_get_aspace_by_id(aid);
	if (aspace == NULL)
		return ERR_VM_INVALID_ASPACE;

	sourceArea = vm_get_area(sourceID);
	if (sourceArea == NULL) {
		vm_put_aspace(aspace);
		return ERR_VM_INVALID_REGION;
	}

	if (sourceArea->aspace == kernel_aspace && aspace != kernel_aspace
		&& !(sourceArea->protection & B_USER_CLONEABLE_AREA)) {
		// kernel areas must not be cloned in userland, unless explicitly
		// declared user-cloneable upon construction
		status = B_NOT_ALLOWED;
	} else {
		vm_cache_acquire_ref(sourceArea->cache_ref, true);
		status = map_backing_store(aspace, sourceArea->cache_ref->cache->store, address,
					sourceArea->cache_offset, sourceArea->size, addressSpec, sourceArea->wiring,
					protection, mapping, &newArea, name);
		vm_cache_release_ref(sourceArea->cache_ref);
	}

	vm_put_area(sourceArea);
	vm_put_aspace(aspace);

	if (status < B_OK)
		return status;

	return newArea->id;
}


static status_t
_vm_delete_area(vm_address_space *aspace, area_id id)
{
	status_t status = B_OK;
	vm_area *area;

	TRACE(("vm_delete_area: aspace id 0x%lx, area id 0x%lx\n", aspace->id, id));

	area = vm_get_area(id);
	if (area == NULL)
		return B_BAD_VALUE;

	if (area->aspace == aspace) {
		vm_put_area(area);
			// next put below will actually delete it
	} else
		status = B_NOT_ALLOWED;

	vm_put_area(area);
	return status;
}


status_t
vm_delete_area(aspace_id aid, area_id rid)
{
	vm_address_space *aspace;
	status_t err;

	aspace = vm_get_aspace_by_id(aid);
	if (aspace == NULL)
		return ERR_VM_INVALID_ASPACE;

	err = _vm_delete_area(aspace, rid);
	vm_put_aspace(aspace);
	return err;
}


static void
remove_area_from_virtual_map(vm_address_space *addressSpace, vm_area *area, bool locked)
{
	vm_area *temp, *last = NULL;

	if (!locked)
		acquire_sem_etc(addressSpace->virtual_map.sem, WRITE_COUNT, 0, 0);

	temp = addressSpace->virtual_map.areas;
	while (temp != NULL) {
		if (area == temp) {
			if (last != NULL) {
				last->aspace_next = temp->aspace_next;
			} else {
				addressSpace->virtual_map.areas = temp->aspace_next;
			}
			addressSpace->virtual_map.change_count++;
			break;
		}
		last = temp;
		temp = temp->aspace_next;
	}
	if (area == addressSpace->virtual_map.area_hint)
		addressSpace->virtual_map.area_hint = NULL;

	if (!locked)
		release_sem_etc(addressSpace->virtual_map.sem, WRITE_COUNT, 0);

	if (temp == NULL)
		panic("vm_area_release_ref: area not found in aspace's area list\n");
}


static void
_vm_put_area(vm_area *area, bool aspaceLocked)
{
	vm_address_space *aspace;
	bool removeit = false;

	// we should never get here, but if we do, we can handle it
	if (area->id == RESERVED_REGION_ID)
		return;

	acquire_sem_etc(sAreaHashLock, WRITE_COUNT, 0, 0);
	if (atomic_add(&area->ref_count, -1) == 1) {
		hash_remove(sAreaHash, area);
		removeit = true;
	}
	release_sem_etc(sAreaHashLock, WRITE_COUNT, 0);

	if (!removeit)
		return;

	aspace = area->aspace;

	remove_area_from_virtual_map(aspace, area, aspaceLocked);

	vm_cache_remove_area(area->cache_ref, area);
	vm_cache_release_ref(area->cache_ref);

	(*aspace->translation_map.ops->lock)(&aspace->translation_map);
	(*aspace->translation_map.ops->unmap)(&aspace->translation_map, area->base,
		area->base + (area->size - 1));
	(*aspace->translation_map.ops->unlock)(&aspace->translation_map);

	// now we can give up the area's reference to the address space
	vm_put_aspace(aspace);

	free(area->name);
	free(area);
}


static void
vm_put_area(vm_area *area)
{
	return _vm_put_area(area, false);
}


static status_t
vm_copy_on_write_area(vm_area *area)
{
	vm_store *store;
	vm_cache *upperCache, *lowerCache;
	vm_cache_ref *upperCacheRef, *lowerCacheRef;
	vm_translation_map *map;
	vm_page *page;
	uint32 protection;
	status_t status;

	// We need to separate the vm_cache from its vm_cache_ref: the area
	// and its cache_ref goes into a new layer on top of the old one.
	// So the old cache gets a new cache_ref and the area a new cache.

	upperCacheRef = area->cache_ref;
	lowerCache = upperCacheRef->cache;

	// create an anonymous store object
	store = vm_store_create_anonymous_noswap(false, 0);
	if (store == NULL)
		return B_NO_MEMORY;

	upperCache = vm_cache_create(store);
	if (upperCache == NULL) {
		status = B_NO_MEMORY;
		goto err1;
	}

	lowerCacheRef = vm_cache_ref_create(lowerCache);
	if (lowerCacheRef == NULL) {
		status = B_NO_MEMORY;
		goto err2;
	}

	// The area must be readable in the same way it was previously writable
	protection = B_KERNEL_READ_AREA;
	if (area->protection & B_READ_AREA)
		protection |= B_READ_AREA;

	// we need to hold the cache_ref lock when we want to switch its cache
	mutex_lock(&upperCacheRef->lock);
	mutex_lock(&lowerCacheRef->lock);

	// ToDo: add a child counter to vm_cache - so that we can collapse a
	//		cache layer when possible (ie. "the other" area was deleted)
	upperCache->temporary = 1;
	upperCache->scan_skip = lowerCache->scan_skip;
	upperCache->source = lowerCache;
	upperCache->ref = upperCacheRef;
	upperCacheRef->cache = upperCache;

	// we need to manually alter the ref_count
	lowerCacheRef->ref_count = upperCacheRef->ref_count;
	upperCacheRef->ref_count = 1;

	// grab a ref to the cache object we're now linked to as a source
	vm_cache_acquire_ref(lowerCacheRef, true);

	// We now need to remap all pages from the area read-only, so that
	// a copy will be created on next write access

	map = &area->aspace->translation_map;
	map->ops->lock(map);
	map->ops->unmap(map, area->base, area->base + area->size - 1);

	for (page = lowerCache->page_list; page; page = page->cache_next) {
		map->ops->map(map, area->base + page->offset, page->ppn * B_PAGE_SIZE, protection);
	}

	map->ops->unlock(map);

	mutex_unlock(&lowerCacheRef->lock);
	mutex_unlock(&upperCacheRef->lock);

	return B_OK;

err2:
	free(upperCache);
err1:
	store->ops->destroy(store);
	return status;
}


area_id
vm_copy_area(aspace_id addressSpaceID, const char *name, void **_address, uint32 addressSpec,
	uint32 protection, area_id sourceID)
{
	vm_address_space *addressSpace;
	vm_cache_ref *cacheRef;
	vm_area *target, *source;
	status_t status;

	if ((protection & B_KERNEL_PROTECTION) == 0)
		protection |= B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA;

	if ((source = vm_get_area(sourceID)) == NULL)
		return B_BAD_VALUE;

	addressSpace = vm_get_aspace_by_id(addressSpaceID);
	cacheRef = source->cache_ref;

	if (addressSpec == B_CLONE_ADDRESS) {
		addressSpec = B_EXACT_ADDRESS;
		*_address = (void *)source->base;
	}

	// First, create a cache on top of the source area

	status = map_backing_store(addressSpace, cacheRef->cache->store, _address,
		source->cache_offset, source->size, addressSpec, source->wiring, protection,
		protection & (B_KERNEL_WRITE_AREA | B_WRITE_AREA) ? REGION_PRIVATE_MAP : REGION_NO_PRIVATE_MAP,
		&target, name);

	if (status < B_OK)
		goto err;

	// If the source area is writable, we need to move it one layer up as well

	if ((source->protection & (B_KERNEL_WRITE_AREA | B_WRITE_AREA)) != 0)
		vm_copy_on_write_area(source);

	// we want to return the ID of the newly created area
	status = target->id;

err:
	vm_put_aspace(addressSpace);
	vm_put_area(source);

	return status;
}


static int32
count_writable_areas(vm_cache_ref *ref, vm_area *ignoreArea)
{
	struct vm_area *area = ref->areas;
	uint32 count = 0;

	for (; area != NULL; area = area->cache_next) {
		if (area != ignoreArea
			&& (area->protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0)
			count++;
	}

	return count;
}


static status_t
vm_set_area_protection(aspace_id aspaceID, area_id areaID, uint32 newProtection)
{
	vm_cache_ref *cacheRef;
	vm_cache *cache;
	vm_area *area;
	status_t status = B_OK;

	TRACE(("vm_set_area_protection(aspace = %#lx, area = %#lx, protection = %#lx)\n",
		aspaceID, areaID, newProtection));

	if ((newProtection & (B_EXECUTE_AREA | B_KERNEL_EXECUTE_AREA)) != 0
		&& (newProtection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0) {
		// executable areas must not be writable
		return B_NOT_ALLOWED;
	}

	area = vm_get_area(areaID);
	if (area == NULL)
		return B_BAD_VALUE;

	if (aspaceID != vm_get_kernel_aspace_id() && area->aspace->id != aspaceID) {
		// unless you're the kernel, you are only allowed to set
		// the protection of your own areas
		vm_put_area(area);
		return B_NOT_ALLOWED;
	}

	cacheRef = area->cache_ref;
	cache = cacheRef->cache;

	mutex_lock(&cacheRef->lock);

	if ((area->protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0
		&& (newProtection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) == 0) {
		// change from read/write to read-only

		if (cache->source != NULL && cache->temporary) {
			if (count_writable_areas(cacheRef, area) == 0) {
				// Since this cache now lives from the pages in its source cache,
				// we can change the cache's commitment to take only those pages
				// into account that really are in this cache.

				// count existing pages in this cache
				struct vm_page *page = cache->page_list;
				uint32 count = 0;

				for (; page != NULL; page = page->cache_next) {
					count++;
				}

				status = cache->store->ops->commit(cache->store, count * B_PAGE_SIZE);

				// ToDo: we may be able to join with our source cache, if count == 0
			}
		}
	} else if ((area->protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) == 0
		&& (newProtection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0) {
		// change from read-only to read/write

		// ToDo: if this is a shared cache, insert new cache (we only know about other
		//	areas in this cache yet, though, not about child areas)
		//	-> use this call with care, it might currently have unwanted consequences
		//	   because of this. It should always be safe though, if there are no other
		//	   (child) areas referencing this area's cache (you just might not know).
		if (count_writable_areas(cacheRef, area) == 0
			&& (cacheRef->areas != area || area->cache_next)) {
			// ToDo: child areas are not tested for yet
			dprintf("set_area_protection(): warning, would need to insert a new cache_ref (not yet implemented)!\n");
			status = B_NOT_ALLOWED;
		} else
			dprintf("set_area_protection() may not work correctly yet in this direction!\n");

		if (status == B_OK && cache->source != NULL && cache->temporary) {
			// the cache's commitment must contain all possible pages
			status = cache->store->ops->commit(cache->store, cache->virtual_size);
		}
	} else {
		// we don't have anything special to do in all other cases
	}

	if (status == B_OK && area->protection != newProtection) {
		// remap existing pages in this cache
		struct vm_translation_map *map = &area->aspace->translation_map;
		struct vm_page *page = cache->page_list;

		map->ops->lock(map);

		for (; page != NULL; page = page->cache_next) {
			// only map those pages inside the area
			if (page->offset >= area->cache_offset
				&& page->offset < area->cache_offset + area->size) {
				map->ops->map(map, area->base + page->offset - area->cache_offset,
					page->ppn * B_PAGE_SIZE, newProtection);
			}
		}

		map->ops->unlock(map);
		area->protection = newProtection;
	}

	mutex_unlock(&cacheRef->lock);
	vm_put_area(area);

	return status;
}


status_t
vm_get_page_mapping(aspace_id aid, addr_t vaddr, addr_t *paddr)
{
	vm_address_space *aspace;
	uint32 null_flags;
	status_t err;

	aspace = vm_get_aspace_by_id(aid);
	if (aspace == NULL)
		return ERR_VM_INVALID_ASPACE;

	err = aspace->translation_map.ops->query(&aspace->translation_map,
		vaddr, paddr, &null_flags);

	vm_put_aspace(aspace);
	return err;
}


static int
display_mem(int argc, char **argv)
{
	int item_size;
	int display_width;
	int num = 1;
	addr_t address;
	int i;
	int j;

	if (argc < 2) {
		dprintf("usage: dw/ds/db <address> [num]\n"
			"\tdw - 4 bytes\n"
			"\tds - 2 bytes\n"
			"\tdb - 1 byte\n");
		return 0;
	}

	address = strtoul(argv[1], NULL, 0);

	if (argc >= 3) {
		num = -1;
		num = atoi(argv[2]);
	}

	// build the format string
	if (strcmp(argv[0], "db") == 0) {
		item_size = 1;
		display_width = 16;
	} else if (strcmp(argv[0], "ds") == 0) {
		item_size = 2;
		display_width = 8;
	} else if (strcmp(argv[0], "dw") == 0) {
		item_size = 4;
		display_width = 4;
	} else {
		dprintf("display_mem called in an invalid way!\n");
		return 0;
	}

	dprintf("[0x%lx] '", address);
	for (j = 0; j < min(display_width, num) * item_size; j++) {
		char c = *((char *)address + j);
		if (!isalnum(c)) {
			c = '.';
		}
		dprintf("%c", c);
	}
	dprintf("'");
	for (i = 0; i < num; i++) {
		if ((i % display_width) == 0 && i != 0) {
			dprintf("\n[0x%lx] '", address + i * item_size);
			for (j = 0; j < min(display_width, (num-i)) * item_size; j++) {
				char c = *((char *)address + i * item_size + j);
				if (!isalnum(c)) {
					c = '.';
				}
				dprintf("%c", c);
			}
			dprintf("'");
		}

		switch (item_size) {
			case 1:
				dprintf(" 0x%02x", *((uint8 *)address + i));
				break;
			case 2:
				dprintf(" 0x%04x", *((uint16 *)address + i));
				break;
			case 4:
				dprintf(" 0x%08lx", *((uint32 *)address + i));
				break;
			default:
				dprintf("huh?\n");
		}
	}
	dprintf("\n");
	return 0;
}


static int
dump_cache_ref(int argc, char **argv)
{
	addr_t address;
	vm_area *area;
	vm_cache_ref *cache_ref;

	if (argc < 2) {
		dprintf("cache_ref: not enough arguments\n");
		return 0;
	}
	if (strlen(argv[1]) < 2 || argv[1][0] != '0' || argv[1][1] != 'x') {
		dprintf("cache_ref: invalid argument, pass address\n");
		return 0;
	}

	address = atoul(argv[1]);
	cache_ref = (vm_cache_ref *)address;

	dprintf("cache_ref at %p:\n", cache_ref);
	dprintf("cache: %p\n", cache_ref->cache);
	dprintf("lock.holder: %ld\n", cache_ref->lock.holder);
	dprintf("lock.sem: 0x%lx\n", cache_ref->lock.sem);
	dprintf("areas:\n");
	for (area = cache_ref->areas; area != NULL; area = area->cache_next) {
		dprintf(" area 0x%lx: ", area->id);
		dprintf("base_addr = 0x%lx ", area->base);
		dprintf("size = 0x%lx ", area->size);
		dprintf("name = '%s' ", area->name);
		dprintf("protection = 0x%lx\n", area->protection);
	}
	dprintf("ref_count: %ld\n", cache_ref->ref_count);
	return 0;
}


static const char *
page_state_to_text(int state)
{
	switch(state) {
		case PAGE_STATE_ACTIVE:
			return "active";
		case PAGE_STATE_INACTIVE:
			return "inactive";
		case PAGE_STATE_BUSY:
			return "busy";
		case PAGE_STATE_MODIFIED:
			return "modified";
		case PAGE_STATE_FREE:
			return "free";
		case PAGE_STATE_CLEAR:
			return "clear";
		case PAGE_STATE_WIRED:
			return "wired";
		case PAGE_STATE_UNUSED:
			return "unused";
		default:
			return "unknown";
	}
}


static int
dump_cache(int argc, char **argv)
{
	addr_t address;
	vm_cache *cache;
	vm_page *page;

	if (argc < 2) {
		dprintf("cache: not enough arguments\n");
		return 0;
	}
	if (strlen(argv[1]) < 2 || argv[1][0] != '0' || argv[1][1] != 'x') {
		dprintf("cache: invalid argument, pass address\n");
		return 0;
	}

	address = atoul(argv[1]);
	cache = (vm_cache *)address;

	dprintf("cache at %p:\n", cache);
	dprintf("cache_ref: %p\n", cache->ref);
	dprintf("source: %p\n", cache->source);
	dprintf("store: %p\n", cache->store);
	// XXX 64-bit
	dprintf("virtual_size: 0x%Lx\n", cache->virtual_size);
	dprintf("temporary: %d\n", cache->temporary);
	dprintf("scan_skip: %d\n", cache->scan_skip);
	dprintf("page_list:\n");
	for(page = cache->page_list; page != NULL; page = page->cache_next) {
		// XXX offset is 64-bit
		if(page->type == PAGE_TYPE_PHYSICAL)
			dprintf(" %p ppn 0x%lx offset 0x%Lx type %d state %d (%s) ref_count %ld\n",
				page, page->ppn, page->offset, page->type, page->state, page_state_to_text(page->state), page->ref_count);
		else if(page->type == PAGE_TYPE_DUMMY)
			dprintf(" %p DUMMY PAGE state %d (%s)\n", page, page->state, page_state_to_text(page->state));
		else
			dprintf(" %p UNKNOWN PAGE type %d\n", page, page->type);
	}
	return 0;
}


static void
_dump_area(vm_area *area)
{
	dprintf("dump of area at %p:\n", area);
	dprintf("name: '%s'\n", area->name);
	dprintf("id: 0x%lx\n", area->id);
	dprintf("base: 0x%lx\n", area->base);
	dprintf("size: 0x%lx\n", area->size);
	dprintf("protection: 0x%lx\n", area->protection);
	dprintf("wiring: 0x%lx\n", area->wiring);
	dprintf("ref_count: %ld\n", area->ref_count);
	dprintf("cache_ref: %p\n", area->cache_ref);
	// XXX 64-bit
	dprintf("cache_offset: 0x%Lx\n", area->cache_offset);
	dprintf("cache_next: %p\n", area->cache_next);
	dprintf("cache_prev: %p\n", area->cache_prev);
}


static int
dump_area(int argc, char **argv)
{
//	int i;
	vm_area *area;

	if (argc < 2) {
		dprintf("area: not enough arguments\n");
		return 0;
	}

	// if the argument looks like a hex number, treat it as such
	if (strlen(argv[1]) > 2 && argv[1][0] == '0' && argv[1][1] == 'x') {
		unsigned long num = strtoul(argv[1], NULL, 16);
		area_id id = num;

		area = hash_lookup(sAreaHash, &id);
		if (area == NULL) {
			dprintf("invalid area id\n");
		} else {
			_dump_area(area);
		}
		return 0;
	} else {
		// walk through the area list, looking for the arguments as a name
		struct hash_iterator iter;

		hash_open(sAreaHash, &iter);
		while ((area = hash_next(sAreaHash, &iter)) != NULL) {
			if (area->name != NULL && strcmp(argv[1], area->name) == 0) {
				_dump_area(area);
			}
		}
	}
	return 0;
}


static int
dump_area_list(int argc, char **argv)
{
	vm_area *area;
	struct hash_iterator iter;

	dprintf("addr\t      id  base\t\tsize\t\tprotect\tlock\tname\n");

	hash_open(sAreaHash, &iter);
	while ((area = hash_next(sAreaHash, &iter)) != NULL) {
		dprintf("%p %5lx  %p\t%p\t%ld\t%ld\t%s\n", area, area->id, (void *)area->base,
			(void *)area->size, area->protection, area->wiring, area->name);
	}
	hash_close(sAreaHash, &iter, false);
	return 0;
}


status_t
vm_delete_areas(struct vm_address_space *aspace)
{
	vm_area *area;
	vm_area *next;

	TRACE(("vm_delete_areas: called on aspace 0x%lx\n", aspace->id));

	acquire_sem_etc(aspace->virtual_map.sem, WRITE_COUNT, 0, 0);

	// delete all the areas in this aspace

	for (area = aspace->virtual_map.areas; area; area = next) {
		next = area->aspace_next;

		if (area->id == RESERVED_REGION_ID) {
			// just remove it
			free(area);
			continue;
		}

		// decrement the ref on this area, may actually push the ref < 0, if there
		// is a concurrent delete_area() on that specific area, but that's ok here
		_vm_put_area(area, true);
	}

	release_sem_etc(aspace->virtual_map.sem, WRITE_COUNT, 0);

	return B_OK;
}


static area_id
vm_area_for(aspace_id aid, addr_t address)
{
	vm_address_space *addressSpace;
	area_id id = B_ERROR;
	vm_area *area;

	addressSpace = vm_get_aspace_by_id(aid);
	if (addressSpace == NULL)
		return ERR_VM_INVALID_ASPACE;

	acquire_sem_etc(addressSpace->virtual_map.sem, READ_COUNT, 0, 0);

	area = addressSpace->virtual_map.areas;
	for (; area != NULL; area = area->aspace_next) {
		// ignore reserved space regions
		if (area->id == RESERVED_REGION_ID)
			continue;

		if (address >= area->base && address < area->base + area->size) {
			id = area->id;
			break;
		}
	}

	release_sem_etc(addressSpace->virtual_map.sem, READ_COUNT, 0);
	vm_put_aspace(addressSpace);

	return id;
}


static void
unmap_and_free_physical_pages(vm_translation_map *map, addr_t start, addr_t end)
{
	addr_t current = start;

	// free all physical pages behind the specified range

	while (current < end) {
		addr_t physicalAddress;
		uint32 flags;

		if (map->ops->query(map, current, &physicalAddress, &flags) == B_OK) {
			vm_page *page = vm_lookup_page(current / B_PAGE_SIZE);
			if (page != NULL)
				vm_page_set_state(page, PAGE_STATE_FREE);
		}

		current += B_PAGE_SIZE;
	}

	// unmap the memory
	map->ops->unmap(map, start, end - 1);
}


void
vm_free_unused_boot_loader_range(addr_t start, addr_t size)
{
	vm_translation_map *map = &kernel_aspace->translation_map;
	addr_t end = start + size;
	addr_t lastEnd = start;
	vm_area *area;

	TRACE(("vm_free_unused_boot_loader_range(): asked to free %p - %p\n", (void *)start, (void *)end));

	// The areas are sorted in virtual address space order, so
	// we just have to find the holes between them that fall
	// into the area we should dispose

	map->ops->lock(map);

	for (area = kernel_aspace->virtual_map.areas; area; area = area->aspace_next) {
		addr_t areaStart = area->base;
		addr_t areaEnd = areaStart + area->size;

		if (area->id == RESERVED_REGION_ID)
			continue;

		if (areaEnd >= end) {
			// we are done, the areas are already beyond of what we have to free
			lastEnd = end;
			break;
		}

		if (areaStart > lastEnd) {
			// this is something we can free
			TRACE(("free boot range: get rid of %p - %p\n", (void *)lastEnd, (void *)areaStart));
			unmap_and_free_physical_pages(map, lastEnd, areaStart);
		}

		lastEnd = areaEnd;
	}

	if (lastEnd < end) {
		// we can also get rid of some space at the end of the area
		TRACE(("free boot range: also remove %p - %p\n", (void *)lastEnd, (void *)end));
		unmap_and_free_physical_pages(map, lastEnd, end);
	}

	map->ops->unlock(map);
}


static void
create_preloaded_image_areas(struct preloaded_image *image)
{
	char name[B_OS_NAME_LENGTH];
	void *address;
	int32 length;

	// use file name to create a good area name
	char *fileName = strrchr(image->name, '/');
	if (fileName == NULL)
		fileName = image->name;
	else
		fileName++;

	length = strlen(fileName);
	// make sure there is enough space for the suffix
	if (length > 25)
		length = 25;

	memcpy(name, fileName, length);
	strcpy(name + length, "_text");
	address = (void *)ROUNDOWN(image->text_region.start, B_PAGE_SIZE);
	image->text_region.id = create_area(name, &address, B_EXACT_ADDRESS,
		PAGE_ALIGN(image->text_region.size), B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	strcpy(name + length, "_data");
	address = (void *)ROUNDOWN(image->data_region.start, B_PAGE_SIZE);
	image->data_region.id = create_area(name, &address, B_EXACT_ADDRESS,
		PAGE_ALIGN(image->data_region.size), B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
}


/**	Frees all previously kernel arguments areas from the kernel_args structure.
 *	Any boot loader resources contained in that arguments must not be accessed
 *	anymore past this point.
 */

void
vm_free_kernel_args(kernel_args *args)
{
	uint32 i;

	TRACE(("vm_free_kernel_args()\n"));

	for (i = 0; i < args->num_kernel_args_ranges; i++) {
		area_id area = area_for((void *)args->kernel_args_range[i].start);
		if (area >= B_OK)
			delete_area(area);
	}
}


static void
allocate_kernel_args(kernel_args *args)
{
	uint32 i;

	TRACE(("allocate_kernel_args()\n"));

	for (i = 0; i < args->num_kernel_args_ranges; i++) {
		void *address = (void *)args->kernel_args_range[i].start;

		create_area("_kernel args_", &address, B_EXACT_ADDRESS, args->kernel_args_range[i].size,
			B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	}
}


static void
unreserve_boot_loader_ranges(kernel_args *args)
{
	uint32 i;

	TRACE(("unreserve_boot_loader_ranges()\n"));

	for (i = 0; i < args->num_virtual_allocated_ranges; i++) {
		vm_unreserve_address_range(vm_get_kernel_aspace_id(),
			(void *)args->virtual_allocated_range[i].start,
			args->virtual_allocated_range[i].size);
	}
}


static void
reserve_boot_loader_ranges(kernel_args *args)
{
	uint32 i;

	TRACE(("reserve_boot_loader_ranges()\n"));

	for (i = 0; i < args->num_virtual_allocated_ranges; i++) {
		void *address = (void *)args->virtual_allocated_range[i].start;
		status_t status = vm_reserve_address_range(vm_get_kernel_aspace_id(), &address,
			B_EXACT_ADDRESS, args->virtual_allocated_range[i].size);
		if (status < B_OK)
			panic("could not reserve boot loader ranges\n");
	}
}


status_t
vm_init(kernel_args *args)
{
	struct preloaded_image *image;
	addr_t heap_base;
	void *address;
	status_t err = 0;
	uint32 i;

	TRACE(("vm_init: entry\n"));
	err = arch_vm_translation_map_init(args);
	err = arch_vm_init(args);

	// initialize some globals
	sNextAreaID = 1;
	sAreaHashLock = -1;

	// map in the new heap and initialize it
	heap_base = vm_alloc_from_kernel_args(args, HEAP_SIZE, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	TRACE(("heap at 0x%lx\n", heap_base));
	heap_init(heap_base);

	// initialize the free page list and physical page mapper
	vm_page_init(args);
	sAvailableMemory = vm_page_num_pages() * B_PAGE_SIZE;

	// initialize the hash table that stores the pages mapped to caches
	vm_cache_init(args);

	{
		vm_area *area;
		sAreaHash = hash_init(REGION_HASH_TABLE_SIZE, (addr_t)&area->hash_next - (addr_t)area,
			&area_compare, &area_hash);
		if (sAreaHash == NULL)
			panic("vm_init: error creating aspace hash table\n");
	}

	vm_aspace_init();
	reserve_boot_loader_ranges(args);

	// do any further initialization that the architecture dependant layers may need now
	arch_vm_translation_map_init_post_area(args);
	arch_vm_init_post_area(args);
	vm_page_init_post_area(args);

	// allocate areas to represent stuff that already exists

	address = (void *)ROUNDOWN(heap_base, B_PAGE_SIZE);
	create_area("kernel heap", &address, B_EXACT_ADDRESS, HEAP_SIZE,
		B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	allocate_kernel_args(args);

	args->kernel_image.name = "kernel";
		// the lazy boot loader currently doesn't set the kernel's name...
	create_preloaded_image_areas(&args->kernel_image);

	// allocate areas for preloaded images
	for (image = args->preloaded_images; image != NULL; image = image->next) {
		create_preloaded_image_areas(image);
	}

	// allocate kernel stacks
	for (i = 0; i < args->num_cpus; i++) {
		char name[64];

		sprintf(name, "idle thread %lu kstack", i);
		address = (void *)args->cpu_kstack[i].start;
		create_area(name, &address, B_EXACT_ADDRESS, args->cpu_kstack[i].size,
			B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	}
	{
		void *null;
		vm_map_physical_memory(vm_get_kernel_aspace_id(), "bootdir", &null, B_ANY_KERNEL_ADDRESS,
			args->bootdir_addr.size, B_KERNEL_READ_AREA, args->bootdir_addr.start);
	}

	// add some debugger commands
	add_debugger_command("areas", &dump_area_list, "Dump a list of all areas");
	add_debugger_command("area", &dump_area, "Dump info about a particular area");
	add_debugger_command("cache_ref", &dump_cache_ref, "Dump cache_ref data structure");
	add_debugger_command("cache", &dump_cache, "Dump cache_ref data structure");
//	add_debugger_command("dl", &display_mem, "dump memory long words (64-bit)");
	add_debugger_command("dw", &display_mem, "dump memory words (32-bit)");
	add_debugger_command("ds", &display_mem, "dump memory shorts (16-bit)");
	add_debugger_command("db", &display_mem, "dump memory bytes (8-bit)");

	TRACE(("vm_init: exit\n"));

	return err;
}


status_t
vm_init_post_sem(kernel_args *args)
{
	vm_area *area;

	// This frees all unused boot loader resources and makes its space available again
	arch_vm_init_end(args);
	unreserve_boot_loader_ranges(args);

	// fill in all of the semaphores that were not allocated before
	// since we're still single threaded and only the kernel address space exists,
	// it isn't that hard to find all of the ones we need to create

	benaphore_init(&sAvailableMemoryLock, "available memory lock");
	arch_vm_translation_map_init_post_sem(args);
	vm_aspace_init_post_sem();

	for (area = kernel_aspace->virtual_map.areas; area; area = area->aspace_next) {
		if (area->id == RESERVED_REGION_ID)
			continue;

		if (area->cache_ref->lock.sem < 0)
			mutex_init(&area->cache_ref->lock, "cache_ref_mutex");
	}

	sAreaHashLock = create_sem(WRITE_COUNT, "area hash");

	return heap_init_post_sem(args);
}


status_t
vm_init_post_thread(kernel_args *args)
{
	vm_page_init_post_thread(args);
	vm_daemon_init();

	return heap_init_post_thread(args);
}


void
permit_page_faults(void)
{
	struct thread *thread = thread_get_current_thread();
	if (thread != NULL)
		atomic_add(&thread->page_faults_allowed, 1);
}


void
forbid_page_faults(void)
{
	struct thread *thread = thread_get_current_thread();
	if (thread != NULL)
		atomic_add(&thread->page_faults_allowed, -1);
}


status_t
vm_page_fault(addr_t address, addr_t fault_address, bool is_write, bool is_user, addr_t *newip)
{
	int err;

	FTRACE(("vm_page_fault: page fault at 0x%lx, ip 0x%lx\n", address, fault_address));

	*newip = 0;

	err = vm_soft_fault(address, is_write, is_user);
	if (err < 0) {
		dprintf("vm_page_fault: vm_soft_fault returned error %d on fault at 0x%lx, ip 0x%lx, write %d, user %d, thread 0x%lx\n",
			err, address, fault_address, is_write, is_user, thread_get_current_thread_id());
		if (!is_user) {
			struct thread *t = thread_get_current_thread();
			if (t && t->fault_handler != 0) {
				// this will cause the arch dependant page fault handler to
				// modify the IP on the interrupt frame or whatever to return
				// to this address
				*newip = t->fault_handler;
			} else {
				// unhandled page fault in the kernel
				panic("vm_page_fault: unhandled page fault in kernel space at 0x%lx, ip 0x%lx\n",
					address, fault_address);
			}
		} else {
#if 1
			// ToDo: remove me once we have proper userland debugging support (and tools)
			vm_address_space *aspace = vm_get_current_user_aspace();
			vm_virtual_map *map = &aspace->virtual_map;
			vm_area *area;

			acquire_sem_etc(map->sem, READ_COUNT, 0, 0);
			area = vm_virtual_map_lookup(map, fault_address);

			dprintf("vm_page_fault: killing team 0x%lx, ip %#lx (\"%s\" +%#lx)\n",
				thread_get_current_thread()->team->id, fault_address,
				area ? area->name : "???", fault_address - (area ? area->base : 0x0));

			release_sem_etc(map->sem, READ_COUNT, 0);
			vm_put_aspace(aspace);
#endif
			if (user_debug_exception_occurred(B_SEGMENT_VIOLATION, SIGSEGV))
				send_signal(team_get_current_team_id(), SIGSEGV);
		}
	}

	return B_HANDLED_INTERRUPT;
}


static status_t
vm_soft_fault(addr_t originalAddress, bool isWrite, bool isUser)
{
	vm_address_space *aspace;
	vm_virtual_map *map;
	vm_area *area;
	vm_cache_ref *cache_ref;
	vm_cache_ref *last_cache_ref;
	vm_cache_ref *top_cache_ref;
	off_t cache_offset;
	vm_page dummy_page;
	vm_page *page = NULL;
	addr_t address;
	int change_count;
	int err;

	FTRACE(("vm_soft_fault: thid 0x%lx address 0x%lx, isWrite %d, isUser %d\n",
		thread_get_current_thread_id(), originalAddress, isWrite, isUser));

	address = ROUNDOWN(originalAddress, B_PAGE_SIZE);

	if (IS_KERNEL_ADDRESS(address)) {
		aspace = vm_get_kernel_aspace();
	} else if (IS_USER_ADDRESS(address)) {
		aspace = vm_get_current_user_aspace();
		if (aspace == NULL) {
			if (isUser == false) {
				dprintf("vm_soft_fault: kernel thread accessing invalid user memory!\n");
				return B_BAD_ADDRESS;
			} else {
				// XXX weird state.
				panic("vm_soft_fault: non kernel thread accessing user memory that doesn't exist!\n");
			}
		}
	} else {
		// the hit was probably in the 64k DMZ between kernel and user space
		// this keeps a user space thread from passing a buffer that crosses into kernel space
		return B_BAD_ADDRESS;
	}
	map = &aspace->virtual_map;
	atomic_add(&aspace->fault_count, 1);

	// Get the area the fault was in

	acquire_sem_etc(map->sem, READ_COUNT, 0, 0);
	area = vm_virtual_map_lookup(map, address);
	if (area == NULL) {
		release_sem_etc(map->sem, READ_COUNT, 0);
		vm_put_aspace(aspace);
		dprintf("vm_soft_fault: va 0x%lx not covered by area in address space\n", originalAddress);
		return B_BAD_ADDRESS;
	}

	// check permissions
	if (isUser && (area->protection & B_USER_PROTECTION) == 0) {
		release_sem_etc(map->sem, READ_COUNT, 0);
		vm_put_aspace(aspace);
		dprintf("user access on kernel area 0x%lx at %p\n", area->id, (void *)originalAddress);
		return B_PERMISSION_DENIED;
	}
	if (isWrite && (area->protection & (B_WRITE_AREA | (isUser ? 0 : B_KERNEL_WRITE_AREA))) == 0) {
		release_sem_etc(map->sem, READ_COUNT, 0);
		vm_put_aspace(aspace);
		dprintf("write access attempted on read-only area 0x%lx at %p\n", area->id, (void *)originalAddress);
		return B_PERMISSION_DENIED;
	}

	// We have the area, it was a valid access, so let's try to resolve the page fault now.
	// At first, the top most cache from the area is investigated

	top_cache_ref = area->cache_ref;
	cache_offset = address - area->base + area->cache_offset;
	vm_cache_acquire_ref(top_cache_ref, true);
	change_count = map->change_count;
	release_sem_etc(map->sem, READ_COUNT, 0);

	// See if this cache has a fault handler - this will do all the work for us
	if (top_cache_ref->cache->store->ops->fault != NULL) {
		// Note, since the page fault is resolved with interrupts enabled, the
		// fault handler could be called more than once for the same reason -
		// the store must take this into account
		status_t status = (*top_cache_ref->cache->store->ops->fault)(top_cache_ref->cache->store, aspace, cache_offset);
		if (status != B_BAD_HANDLER) {
			vm_cache_release_ref(top_cache_ref);
			vm_put_aspace(aspace);
			return status;
		}
	}

	// The top most cache has no fault handler, so let's see if the cache or its sources
	// already have the page we're searching for (we're going from top to bottom)

	dummy_page.state = PAGE_STATE_INACTIVE;
	dummy_page.type = PAGE_TYPE_DUMMY;

	last_cache_ref = top_cache_ref;
	for (cache_ref = top_cache_ref; cache_ref; cache_ref = (cache_ref->cache->source) ? cache_ref->cache->source->ref : NULL) {
		mutex_lock(&cache_ref->lock);

		for (;;) {
			page = vm_cache_lookup_page(cache_ref, cache_offset);
			if (page != NULL && page->state != PAGE_STATE_BUSY) {
				vm_page_set_state(page, PAGE_STATE_BUSY);
				mutex_unlock(&cache_ref->lock);
				break;
			}

			if (page == NULL)
				break;

			// page must be busy
			// ToDo: don't wait forever!
			mutex_unlock(&cache_ref->lock);
			snooze(20000);
			mutex_lock(&cache_ref->lock);
		}

		if (page != NULL)
			break;

		// The current cache does not contain the page we're looking for

		// If we're at the top most cache, insert the dummy page here to keep other threads
		// from faulting on the same address and chasing us up the cache chain
		if (cache_ref == top_cache_ref) {
			dummy_page.state = PAGE_STATE_BUSY;
			vm_cache_insert_page(cache_ref, &dummy_page, cache_offset);
		}

		// see if the vm_store has it
		if (cache_ref->cache->store->ops->has_page != NULL
			&& cache_ref->cache->store->ops->has_page(cache_ref->cache->store, cache_offset)) {
			size_t bytesRead;
			iovec vec;

			vec.iov_len = bytesRead = B_PAGE_SIZE;

			mutex_unlock(&cache_ref->lock);

			page = vm_page_allocate_page(PAGE_STATE_FREE);
			aspace->translation_map.ops->get_physical_page(page->ppn * B_PAGE_SIZE, (addr_t *)&vec.iov_base, PHYSICAL_PAGE_CAN_WAIT);
			// ToDo: handle errors here
			err = cache_ref->cache->store->ops->read(cache_ref->cache->store, cache_offset, &vec, 1, &bytesRead);
			aspace->translation_map.ops->put_physical_page((addr_t)vec.iov_base);

			mutex_lock(&cache_ref->lock);

			if (cache_ref == top_cache_ref) {
				vm_cache_remove_page(cache_ref, &dummy_page);
				dummy_page.state = PAGE_STATE_INACTIVE;
			}
			vm_cache_insert_page(cache_ref, page, cache_offset);
			mutex_unlock(&cache_ref->lock);
			break;
		}
		mutex_unlock(&cache_ref->lock);
		last_cache_ref = cache_ref;
	}

	if (!cache_ref) {
		// We rolled off the end of the cache chain, so we need to decide which
		// cache will get the new page we're about to create.
		
		cache_ref = isWrite ? top_cache_ref : last_cache_ref;
			// Read-only pages come in the deepest cache - only the 
			// top most cache may have direct write access.
	}

	if (page == NULL) {
		// we still haven't found a page, so we allocate a clean one
		page = vm_page_allocate_page(PAGE_STATE_CLEAR);
		FTRACE(("vm_soft_fault: just allocated page 0x%lx\n", page->ppn));

		// Insert the new page into our cache, and replace it with the dummy page if necessary

		mutex_lock(&cache_ref->lock);

		// if we inserted a dummy page into this cache, we have to remove it now
		if (dummy_page.state == PAGE_STATE_BUSY && dummy_page.cache == cache_ref->cache) {
			vm_cache_remove_page(cache_ref, &dummy_page);
			dummy_page.state = PAGE_STATE_INACTIVE;
		}

		vm_cache_insert_page(cache_ref, page, cache_offset);
		mutex_unlock(&cache_ref->lock);

		if (dummy_page.state == PAGE_STATE_BUSY) {
			// we had inserted the dummy cache in another cache, so let's remove it from there
			vm_cache_ref *temp_cache = dummy_page.cache->ref;
			mutex_lock(&temp_cache->lock);
			vm_cache_remove_page(temp_cache, &dummy_page);
			mutex_unlock(&temp_cache->lock);
			dummy_page.state = PAGE_STATE_INACTIVE;
		}
	}

	// We now have the page and a cache it belongs to - we now need to make
	// sure that the area's cache can access it, too, and sees the correct data

	if (page->cache != top_cache_ref->cache && isWrite) {
		// now we have a page that has the data we want, but in the wrong cache object
		// so we need to copy it and stick it into the top cache
		vm_page *src_page = page;
		void *src, *dest;

		FTRACE(("get new page, copy it, and put it into the topmost cache\n"));
		page = vm_page_allocate_page(PAGE_STATE_FREE);

		// try to get a mapping for the src and dest page so we can copy it
		for (;;) {
			(*aspace->translation_map.ops->get_physical_page)(src_page->ppn * B_PAGE_SIZE, (addr_t *)&src, PHYSICAL_PAGE_CAN_WAIT);
			err = (*aspace->translation_map.ops->get_physical_page)(page->ppn * B_PAGE_SIZE, (addr_t *)&dest, PHYSICAL_PAGE_NO_WAIT);
			if (err == B_NO_ERROR)
				break;

			// it couldn't map the second one, so sleep and retry
			// keeps an extremely rare deadlock from occuring
			(*aspace->translation_map.ops->put_physical_page)((addr_t)src);
			snooze(5000);
		}

		memcpy(dest, src, B_PAGE_SIZE);
		(*aspace->translation_map.ops->put_physical_page)((addr_t)src);
		(*aspace->translation_map.ops->put_physical_page)((addr_t)dest);

		vm_page_set_state(src_page, PAGE_STATE_ACTIVE);

		mutex_lock(&top_cache_ref->lock);

		// Insert the new page into our cache, and replace it with the dummy page if necessary

		// if we inserted a dummy page into this cache, we have to remove it now
		if (dummy_page.state == PAGE_STATE_BUSY && dummy_page.cache == top_cache_ref->cache) {
			vm_cache_remove_page(top_cache_ref, &dummy_page);
			dummy_page.state = PAGE_STATE_INACTIVE;
		}

		vm_cache_insert_page(top_cache_ref, page, cache_offset);
		mutex_unlock(&top_cache_ref->lock);

		if (dummy_page.state == PAGE_STATE_BUSY) {
			// we had inserted the dummy cache in another cache, so let's remove it from there
			vm_cache_ref *temp_cache = dummy_page.cache->ref;
			mutex_lock(&temp_cache->lock);
			vm_cache_remove_page(temp_cache, &dummy_page);
			mutex_unlock(&temp_cache->lock);
			dummy_page.state = PAGE_STATE_INACTIVE;
		}
	}

	err = 0;
	acquire_sem_etc(map->sem, READ_COUNT, 0, 0);
	if (change_count != map->change_count) {
		// something may have changed, see if the address is still valid
		area = vm_virtual_map_lookup(map, address);
		if (area == NULL
			|| area->cache_ref != top_cache_ref
			|| (address - area->base + area->cache_offset) != cache_offset) {
			dprintf("vm_soft_fault: address space layout changed effecting ongoing soft fault\n");
			err = B_BAD_ADDRESS;
		}
	}

	if (err == 0) {
		// All went fine, all there is left to do is to map the page into the address space

		// If the page doesn't reside in the area's cache, we need to make sure it's
		// mapped in read-only, so that we cannot overwrite someone else's data (copy-on-write)
		uint32 newProtection = area->protection;
		if (page->cache != top_cache_ref->cache && !isWrite)
			newProtection &= ~(isUser ? B_WRITE_AREA : B_KERNEL_WRITE_AREA);

		atomic_add(&page->ref_count, 1);
		(*aspace->translation_map.ops->lock)(&aspace->translation_map);
		(*aspace->translation_map.ops->map)(&aspace->translation_map, address,
			page->ppn * B_PAGE_SIZE, newProtection);
		(*aspace->translation_map.ops->unlock)(&aspace->translation_map);
	}

	release_sem_etc(map->sem, READ_COUNT, 0);

	if (dummy_page.state == PAGE_STATE_BUSY) {
		// We still have the dummy page in the cache - that happens if we didn't need
		// to allocate a new page before, but could use one in another cache
		vm_cache_ref *temp_cache = dummy_page.cache->ref;
		mutex_lock(&temp_cache->lock);
		vm_cache_remove_page(temp_cache, &dummy_page);
		mutex_unlock(&temp_cache->lock);
		dummy_page.state = PAGE_STATE_INACTIVE;
	}

	vm_page_set_state(page, PAGE_STATE_ACTIVE);

	vm_cache_release_ref(top_cache_ref);
	vm_put_aspace(aspace);

	return err;
}


static vm_area *
vm_virtual_map_lookup(vm_virtual_map *map, addr_t address)
{
	vm_area *area;

	// check the areas list first
	area = map->area_hint;
	if (area && area->base <= address && (area->base + area->size) > address)
		return area;

	for (area = map->areas; area != NULL; area = area->aspace_next) {
		if (area->id == RESERVED_REGION_ID)
			continue;

		if (area->base <= address && (area->base + area->size) > address)
			break;
	}

	if (area)
		map->area_hint = area;
	return area;
}


status_t
vm_get_physical_page(addr_t paddr, addr_t *_vaddr, int flags)
{
	return (*kernel_aspace->translation_map.ops->get_physical_page)(paddr, _vaddr, flags);
}


status_t
vm_put_physical_page(addr_t vaddr)
{
	return (*kernel_aspace->translation_map.ops->put_physical_page)(vaddr);
}


void
vm_unreserve_memory(size_t amount)
{
	benaphore_lock(&sAvailableMemoryLock);

	sAvailableMemory += amount;

	benaphore_unlock(&sAvailableMemoryLock);
}


status_t
vm_try_reserve_memory(size_t amount)
{
	status_t status;
	benaphore_lock(&sAvailableMemoryLock);

	if (sAvailableMemory > amount) {
		sAvailableMemory -= amount;
		status = B_OK;
	} else
		status = B_NO_MEMORY;

	benaphore_unlock(&sAvailableMemoryLock);
	return status;
}


//	#pragma mark -


status_t
user_memcpy(void *to, const void *from, size_t size)
{
	return arch_cpu_user_memcpy(to, from, size, &thread_get_current_thread()->fault_handler);
}


/**	\brief Copies at most (\a size - 1) characters from the string in \a from to
 *	the string in \a to, NULL-terminating the result.
 *
 *	\param to Pointer to the destination C-string.
 *	\param from Pointer to the source C-string.
 *	\param size Size in bytes of the string buffer pointed to by \a to.
 *	
 *	\return strlen(\a from).
 */

ssize_t
user_strlcpy(char *to, const char *from, size_t size)
{
	return arch_cpu_user_strlcpy(to, from, size, &thread_get_current_thread()->fault_handler);
}


status_t
user_memset(void *s, char c, size_t count)
{
	return arch_cpu_user_memset(s, c, count, &thread_get_current_thread()->fault_handler);
}


//	#pragma mark -


long
lock_memory(void *address, ulong numBytes, ulong flags)
{
	addr_t base = (addr_t)address;
	addr_t end = base + numBytes;
	bool isUser = IS_USER_ADDRESS(address);

	// ToDo: Our VM currently doesn't support locking, this function
	//	will now at least make sure that the memory is paged in, but
	//	that's about it.
	//	Nevertheless, it must be implemented as soon as we're able to
	//	swap pages out of memory.

	// ToDo: this is a hack, too; the iospace area is a null region and
	//	officially cannot be written to or read; ie. vm_soft_fault() will
	//	fail there. Furthermore, this is x86 specific as well.
	#define IOSPACE_SIZE (256 * 1024 * 1024)
	if (base >= KERNEL_BASE + IOSPACE_SIZE && base + numBytes < KERNEL_BASE + 2 * IOSPACE_SIZE)
		return B_OK;

	for (; base < end; base += B_PAGE_SIZE) {
		status_t status = vm_soft_fault(base, (flags & B_READ_DEVICE) != 0, isUser);
		if (status != B_OK)	{
			dprintf("lock_memory(address = %p, numBytes = %lu, flags = %lu) failed: %s\n",
				address, numBytes, flags, strerror(status));
			return status;
		}
	}

	return B_OK;
}


long
unlock_memory(void *buffer, ulong numBytes, ulong flags)
{
	return B_OK;
}


/** According to the BeBook, this function should always succeed.
 *	This is no longer the case.
 */

long
get_memory_map(const void *address, ulong numBytes, physical_entry *table, long numEntries)
{
	vm_address_space *addressSpace;
	addr_t virtualAddress = (addr_t)address;
	addr_t pageOffset = virtualAddress & (B_PAGE_SIZE - 1);
	addr_t physicalAddress;
	status_t status = B_OK;
	int32 index = -1;
	addr_t offset = 0;
	uint32 flags;

	TRACE(("get_memory_map(%p, %lu bytes, %ld entries)\n", address, numBytes, numEntries));

	if (numEntries == 0 || numBytes == 0)
		return B_BAD_VALUE;

	// in which address space is the address to be found?	
	if (IS_USER_ADDRESS(virtualAddress))
		addressSpace = vm_get_current_user_aspace();
	else
		addressSpace = vm_get_kernel_aspace();

	if (addressSpace == NULL)
		return B_ERROR;

	(*addressSpace->translation_map.ops->lock)(&addressSpace->translation_map);

	while (offset < numBytes) {
		addr_t bytes = min(numBytes - offset, B_PAGE_SIZE);

		status = (*addressSpace->translation_map.ops->query)(&addressSpace->translation_map,
					(addr_t)address + offset, &physicalAddress, &flags);
		if (status < 0)
			break;

		if (index < 0 && pageOffset > 0) {
			physicalAddress += pageOffset;
			if (bytes > B_PAGE_SIZE - pageOffset)
				bytes = B_PAGE_SIZE - pageOffset;
		}

		// need to switch to the next physical_entry?
		if (index < 0 || (addr_t)table[index].address != physicalAddress - table[index].size) {
			if (++index + 1 > numEntries) {
				// table to small
				status = B_BUFFER_OVERFLOW;
				break;
			}
			table[index].address = (void *)physicalAddress;
			table[index].size = bytes;
		} else {
			// page does fit in current entry
			table[index].size += bytes;
		}

		offset += bytes;
	}
	(*addressSpace->translation_map.ops->unlock)(&addressSpace->translation_map);

	// close the entry list

	if (status == B_OK) {
		// if it's only one entry, we will silently accept the missing ending
		if (numEntries == 1)
			return B_OK;

		if (++index + 1 > numEntries)
			return B_BUFFER_OVERFLOW;

		table[index].address = NULL;
		table[index].size = 0;
	}

	return status;
}


area_id
area_for(void *address)
{
	return vm_area_for(vm_get_kernel_aspace_id(), (addr_t)address);
}


area_id
find_area(const char *name)
{
	struct hash_iterator iterator;
	vm_area *area;
	area_id id = B_NAME_NOT_FOUND;

	acquire_sem_etc(sAreaHashLock, READ_COUNT, 0, 0);
	hash_open(sAreaHash, &iterator);

	while ((area = hash_next(sAreaHash, &iterator)) != NULL) {
		if (area->id == RESERVED_REGION_ID)
			continue;

		if (!strcmp(area->name, name)) {
			id = area->id;
			break;
		}
	}

	hash_close(sAreaHash, &iterator, false);
	release_sem_etc(sAreaHashLock, READ_COUNT, 0);

	return id;
}


static void
fill_area_info(struct vm_area *area, area_info *info, size_t size)
{
	strlcpy(info->name, area->name, B_OS_NAME_LENGTH);
	info->area = area->id;
	info->address = (void *)area->base;
	info->size = area->size;
	info->protection = area->protection & B_USER_PROTECTION;
	info->lock = B_FULL_LOCK;
	info->team = area->aspace->id;
	info->ram_size = area->size;
	info->copy_count = 0;
	info->in_count = 0;
	info->out_count = 0;
		// ToDo: retrieve real values here!
}


status_t
_get_area_info(area_id id, area_info *info, size_t size)
{
	vm_area *area;

	if (size != sizeof(area_info) || info == NULL)
		return B_BAD_VALUE;

	area = vm_get_area(id);
	if (area == NULL)
		return B_BAD_VALUE;

	fill_area_info(area, info, size);
	vm_put_area(area);

	return B_OK;
}


status_t
_get_next_area_info(team_id team, int32 *cookie, area_info *info, size_t size)
{
	addr_t nextBase = *(addr_t *)cookie;
	vm_address_space *addressSpace;
	vm_area *area;

	// we're already through the list
	if (nextBase == (addr_t)-1)
		return B_ENTRY_NOT_FOUND;

	if (team == B_CURRENT_TEAM)
		team = team_get_current_team_id();

	if (!team_is_valid(team)
		|| team_get_address_space(team, &addressSpace) != B_OK)
		return B_BAD_VALUE;

	acquire_sem_etc(addressSpace->virtual_map.sem, READ_COUNT, 0, 0);

	for (area = addressSpace->virtual_map.areas; area; area = area->aspace_next) {
		if (area->id == RESERVED_REGION_ID)
			continue;

		if (area->base > nextBase)
			break;
	}

	// make sure this area won't go away
	if (area != NULL)
		area = vm_get_area(area->id);

	release_sem_etc(addressSpace->virtual_map.sem, READ_COUNT, 0);
	vm_put_aspace(addressSpace);

	if (area == NULL) {
		nextBase = (addr_t)-1;
		return B_ENTRY_NOT_FOUND;
	}

	fill_area_info(area, info, size);
	*cookie = (int32)(area->base);

	vm_put_area(area);

	return B_OK;
}


status_t
set_area_protection(area_id area, uint32 newProtection)
{
	return vm_set_area_protection(vm_get_kernel_aspace_id(), area, newProtection);
}


status_t
resize_area(area_id areaID, size_t newSize)
{
	vm_cache_ref *cache;
	vm_area *area, *current;
	status_t status = B_OK;
	size_t oldSize;

	// is newSize a multiple of B_PAGE_SIZE?
	if (newSize & (B_PAGE_SIZE - 1))
		return B_BAD_VALUE;

	area = vm_get_area(areaID);
	if (area == NULL)
		return B_BAD_VALUE;

	// Resize all areas of this area's cache

	cache = area->cache_ref;
	oldSize = area->size;

	// ToDo: we should only allow to resize anonymous memory areas!
	if (!cache->cache->temporary)
		return B_NOT_ALLOWED;

	mutex_lock(&cache->lock);

	if (oldSize < newSize) {
		// We need to check if all areas of this cache can be resized

		for (current = cache->areas; current; current = current->cache_next) {
			if (current->aspace_next && current->aspace_next->base <= (current->base + newSize)) {
				status = B_ERROR;
				goto out;
			}
		}
	}

	// Okay, looks good so far, so let's do it

	for (current = cache->areas; current; current = current->cache_next) {
		if (current->aspace_next && current->aspace_next->base <= (current->base + newSize)) {
			status = B_ERROR;
			break;
		}

		current->size = newSize;

		// we also need to unmap all pages beyond the new size, if the area has shrinked
		if (newSize < oldSize) {
			vm_translation_map *map = &current->aspace->translation_map;

			map->ops->lock(map);
			map->ops->unmap(map, current->base + newSize, current->base + oldSize - 1);
			map->ops->unlock(map);
		}
	}

	if (status == B_OK)
		status = vm_cache_resize(cache, newSize);

	if (status < B_OK) {
		// This shouldn't really be possible, but hey, who knows
		for (current = cache->areas; current; current = current->cache_next)
			current->size = oldSize;
	}

out:
	mutex_unlock(&cache->lock);

	// ToDo: we must honour the lock restrictions of this area
	return status;
}


/**	Transfers the specified area to a new team. The caller must be the owner
 *	of the area (not yet enforced but probably should be).
 *	This function is currently not exported to the kernel namespace, but is
 *	only accessible using the _kern_transfer_area() syscall.
 */

static status_t
transfer_area(area_id id, void **_address, uint32 addressSpec, team_id target)
{
	vm_address_space *sourceAddressSpace, *targetAddressSpace;
	vm_translation_map *map;
	vm_area *area, *reserved;
	void *reservedAddress;
	status_t status;

	area = vm_get_area(id);
	if (area == NULL)
		return B_BAD_VALUE;

	// ToDo: check if the current team owns the area

	status = team_get_address_space(target, &targetAddressSpace);
	if (status != B_OK)
		goto err1;

	// We will first remove the area, and then reserve its former
	// address range so that we can later reclaim it if the
	// transfer failed.

	sourceAddressSpace = area->aspace;

	reserved = _vm_create_reserved_region_struct(&sourceAddressSpace->virtual_map);
	if (reserved == NULL) {
		status = B_NO_MEMORY;
		goto err2;
	}

	acquire_sem_etc(sourceAddressSpace->virtual_map.sem, WRITE_COUNT, 0, 0);

	reservedAddress = (void *)area->base;
	remove_area_from_virtual_map(sourceAddressSpace, area, true);
	status = insert_area(sourceAddressSpace, &reservedAddress, B_EXACT_ADDRESS,
		area->size, reserved);
		// famous last words: this cannot fail :)

	release_sem_etc(sourceAddressSpace->virtual_map.sem, WRITE_COUNT, 0);

	if (status != B_OK)
		goto err3;

	// unmap the area in the source address space
	map = &sourceAddressSpace->translation_map;
	map->ops->lock(map);
	map->ops->unmap(map, area->base, area->base + (area->size - 1));
	map->ops->unlock(map);

	// insert the area into the target address space

	acquire_sem_etc(targetAddressSpace->virtual_map.sem, WRITE_COUNT, 0, 0);
	// check to see if this aspace has entered DELETE state
	if (targetAddressSpace->state == VM_ASPACE_STATE_DELETION) {
		// okay, someone is trying to delete this aspace now, so we can't
		// insert the area, so back out
		status = B_BAD_TEAM_ID;
		goto err4;
	}

	status = insert_area(targetAddressSpace, _address, addressSpec, area->size, area);
	if (status < B_OK)
		goto err4;

	// The area was successfully transferred to the new team when we got here
	area->aspace = targetAddressSpace;

	release_sem_etc(targetAddressSpace->virtual_map.sem, WRITE_COUNT, 0);

	vm_unreserve_address_range(sourceAddressSpace->id, reservedAddress, area->size);
	vm_put_aspace(sourceAddressSpace);
		// we keep the reference of the target address space for the
		// area, so we only have to put the one from the source
	vm_put_area(area);

	return B_OK;

err4:
	release_sem_etc(targetAddressSpace->virtual_map.sem, WRITE_COUNT, 0);
err3:
	// insert the area again into the source address space
	acquire_sem_etc(sourceAddressSpace->virtual_map.sem, WRITE_COUNT, 0, 0);
	// check to see if this aspace has entered DELETE state
	if (sourceAddressSpace->state == VM_ASPACE_STATE_DELETION
		|| insert_area(sourceAddressSpace, &reservedAddress, B_EXACT_ADDRESS, area->size, area) != B_OK) {
		// We can't insert the area anymore - we have to delete it manually
		vm_cache_remove_area(area->cache_ref, area);
		vm_cache_release_ref(area->cache_ref);
		free(area->name);
		free(area);
		area = NULL;
	}
	release_sem_etc(sourceAddressSpace->virtual_map.sem, WRITE_COUNT, 0);
err2:
	vm_put_aspace(targetAddressSpace);
err1:
	if (area != NULL)
		vm_put_area(area);
	return status;
}


area_id
map_physical_memory(const char *name, void *physicalAddress, size_t numBytes,
	uint32 addressSpec, uint32 protection, void **_virtualAddress)
{
	if ((protection & B_KERNEL_PROTECTION) == 0)
		protection |= B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA;

	return vm_map_physical_memory(vm_get_kernel_aspace_id(), name, _virtualAddress,
		addressSpec, numBytes, protection, (addr_t)physicalAddress);
}


area_id
clone_area(const char *name, void **_address, uint32 addressSpec, uint32 protection,
	area_id source)
{
	if ((protection & B_KERNEL_PROTECTION) == 0)
		protection |= B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA;

	return vm_clone_area(vm_get_kernel_aspace_id(), name, _address, addressSpec,
				protection, REGION_NO_PRIVATE_MAP, source);
}


area_id
create_area_etc(struct team *team, const char *name, void **address, uint32 addressSpec,
	uint32 size, uint32 lock, uint32 protection)
{
	return vm_create_anonymous_area(team->aspace->id, (char *)name, address, 
				addressSpec, size, lock, protection);
}


area_id
create_area(const char *name, void **_address, uint32 addressSpec, size_t size, uint32 lock,
	uint32 protection)
{
	if ((protection & B_KERNEL_PROTECTION) == 0)
		protection |= B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA;

	return vm_create_anonymous_area(vm_get_kernel_aspace_id(), (char *)name, _address, 
				addressSpec, size, lock, protection);
}


status_t
delete_area_etc(struct team *team, area_id area)
{
	return vm_delete_area(team->aspace->id, area);
}


status_t
delete_area(area_id area)
{
	return vm_delete_area(vm_get_kernel_aspace_id(), area);
}


//	#pragma mark -


area_id
_user_area_for(void *address)
{
	return vm_area_for(vm_get_current_user_aspace_id(), (addr_t)address);
}


area_id
_user_find_area(const char *userName)
{
	char name[B_OS_NAME_LENGTH];
	
	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return find_area(name);
}


status_t
_user_get_area_info(area_id area, area_info *userInfo)
{
	area_info info;
	status_t status;

	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = get_area_info(area, &info);
	if (status < B_OK)
		return status;

	if (user_memcpy(userInfo, &info, sizeof(area_info)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_get_next_area_info(team_id team, int32 *userCookie, area_info *userInfo)
{
	status_t status;
	area_info info;
	int32 cookie;

	if (!IS_USER_ADDRESS(userCookie)
		|| !IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&cookie, userCookie, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	status = _get_next_area_info(team, &cookie, &info, sizeof(area_info));
	if (status != B_OK)
		return status;

	if (user_memcpy(userCookie, &cookie, sizeof(int32)) < B_OK
		|| user_memcpy(userInfo, &info, sizeof(area_info)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_set_area_protection(area_id area, uint32 newProtection)
{
	if ((newProtection & ~B_USER_PROTECTION) != 0)
		return B_BAD_VALUE;

	return vm_set_area_protection(vm_get_current_user_aspace_id(), area,
		newProtection | B_KERNEL_READ_AREA
			| (newProtection & B_WRITE_AREA ? B_KERNEL_WRITE_AREA : 0));
}


status_t
_user_resize_area(area_id area, size_t newSize)
{
	return resize_area(area, newSize);
}


status_t
_user_transfer_area(area_id area, void **userAddress, uint32 addressSpec, team_id target)
{
	status_t status;
	void *address;

	// filter out some unavailable values (for userland)
	switch (addressSpec) {
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
			return B_BAD_VALUE;
	}

	if (!IS_USER_ADDRESS(userAddress)
		|| user_memcpy(&address, userAddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	status = transfer_area(area, &address, addressSpec, target);
	if (status < B_OK)
		return status;

	if (user_memcpy(userAddress, &address, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


area_id
_user_clone_area(const char *userName, void **userAddress, uint32 addressSpec,
	uint32 protection, area_id sourceArea)
{
	char name[B_OS_NAME_LENGTH];
	void *address;
	area_id clonedArea;

	// filter out some unavailable values (for userland)
	switch (addressSpec) {
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
			return B_BAD_VALUE;
	}
	if ((protection & ~B_USER_PROTECTION) != 0)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userName)
		|| !IS_USER_ADDRESS(userAddress)
		|| user_strlcpy(name, userName, sizeof(name)) < B_OK
		|| user_memcpy(&address, userAddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	clonedArea = vm_clone_area(vm_get_current_user_aspace_id(), name, &address,
		addressSpec, protection | B_KERNEL_READ_AREA | (protection & B_WRITE_AREA ? B_KERNEL_WRITE_AREA : 0),
		REGION_NO_PRIVATE_MAP, sourceArea);
	if (clonedArea < B_OK)
		return clonedArea;

	if (user_memcpy(userAddress, &address, sizeof(address)) < B_OK) {
		delete_area(clonedArea);
		return B_BAD_ADDRESS;
	}

	return clonedArea;
}


area_id
_user_create_area(const char *userName, void **userAddress, uint32 addressSpec,
	size_t size, uint32 lock, uint32 protection)
{
	char name[B_OS_NAME_LENGTH];
	area_id area;
	void *address;

	// filter out some unavailable values (for userland)
	switch (addressSpec) {
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
			return B_BAD_VALUE;
	}
	if ((protection & ~B_USER_PROTECTION) != 0)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userName)
		|| !IS_USER_ADDRESS(userAddress)
		|| user_strlcpy(name, userName, sizeof(name)) < B_OK
		|| user_memcpy(&address, userAddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	if (addressSpec == B_EXACT_ADDRESS
		&& IS_KERNEL_ADDRESS(address))
		return B_BAD_VALUE;

	area = vm_create_anonymous_area(vm_get_current_user_aspace_id(), (char *)name, &address, 
				addressSpec, size, lock, protection | B_KERNEL_READ_AREA
					| (protection & B_WRITE_AREA ? B_KERNEL_WRITE_AREA : 0));

	if (area >= B_OK && user_memcpy(userAddress, &address, sizeof(address)) < B_OK) {
		delete_area(area);
		return B_BAD_ADDRESS;
	}

	return area;
}


status_t
_user_delete_area(area_id area)
{
	// Unlike the BeOS implementation, you can now only delete areas
	// that you have created yourself from userland.
	// The documentation to delete_area() explicetly states that this
	// will be restricted in the future, and so it will.
	return vm_delete_area(vm_get_current_user_aspace_id(), area);
}


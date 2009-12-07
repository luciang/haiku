/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <vm/vm.h>

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <algorithm>

#include <OS.h>
#include <KernelExport.h>

#include <AutoDeleter.h>

#include <arch/cpu.h>
#include <arch/vm.h>
#include <boot/elf.h>
#include <boot/stage2.h>
#include <condition_variable.h>
#include <console.h>
#include <debug.h>
#include <file_cache.h>
#include <fs/fd.h>
#include <heap.h>
#include <kernel.h>
#include <int.h>
#include <lock.h>
#include <low_resource_manager.h>
#include <smp.h>
#include <system_info.h>
#include <thread.h>
#include <team.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <util/khash.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>

#include "VMAddressSpaceLocking.h"
#include "VMAnonymousCache.h"
#include "IORequest.h"


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


class AreaCacheLocking {
public:
	inline bool Lock(VMCache* lockable)
	{
		return false;
	}

	inline void Unlock(VMCache* lockable)
	{
		vm_area_put_locked_cache(lockable);
	}
};

class AreaCacheLocker : public AutoLocker<VMCache, AreaCacheLocking> {
public:
	inline AreaCacheLocker(VMCache* cache = NULL)
		: AutoLocker<VMCache, AreaCacheLocking>(cache, true)
	{
	}

	inline AreaCacheLocker(VMArea* area)
		: AutoLocker<VMCache, AreaCacheLocking>()
	{
		SetTo(area);
	}

	inline void SetTo(VMArea* area)
	{
		return AutoLocker<VMCache, AreaCacheLocking>::SetTo(
			area != NULL ? vm_area_get_locked_cache(area) : NULL, true, true);
	}
};


static mutex sMappingLock = MUTEX_INITIALIZER("page mappings");
static mutex sAreaCacheLock = MUTEX_INITIALIZER("area->cache");

static off_t sAvailableMemory;
static off_t sNeededMemory;
static mutex sAvailableMemoryLock = MUTEX_INITIALIZER("available memory lock");
static uint32 sPageFaults;

#if DEBUG_CACHE_LIST

struct cache_info {
	VMCache*	cache;
	addr_t		page_count;
	addr_t		committed;
};

static const int kCacheInfoTableCount = 100 * 1024;
static cache_info* sCacheInfoTable;

#endif	// DEBUG_CACHE_LIST


// function declarations
static void delete_area(VMAddressSpace* addressSpace, VMArea* area);
static status_t vm_soft_fault(VMAddressSpace* addressSpace, addr_t address,
	bool isWrite, bool isUser);
static status_t map_backing_store(VMAddressSpace* addressSpace,
	VMCache* cache, void** _virtualAddress, off_t offset, addr_t size,
	uint32 addressSpec, int wiring, int protection, int mapping,
	VMArea** _area, const char* areaName, bool unmapAddressRange, bool kernel);


//	#pragma mark -


#if VM_PAGE_FAULT_TRACING

namespace VMPageFaultTracing {

class PageFaultStart : public AbstractTraceEntry {
public:
	PageFaultStart(addr_t address, bool write, bool user, addr_t pc)
		:
		fAddress(address),
		fPC(pc),
		fWrite(write),
		fUser(user)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page fault %#lx %s %s, pc: %#lx", fAddress,
			fWrite ? "write" : "read", fUser ? "user" : "kernel", fPC);
	}

private:
	addr_t	fAddress;
	addr_t	fPC;
	bool	fWrite;
	bool	fUser;
};


// page fault errors
enum {
	PAGE_FAULT_ERROR_NO_AREA		= 0,
	PAGE_FAULT_ERROR_KERNEL_ONLY,
	PAGE_FAULT_ERROR_WRITE_PROTECTED,
	PAGE_FAULT_ERROR_READ_PROTECTED,
	PAGE_FAULT_ERROR_KERNEL_BAD_USER_MEMORY,
	PAGE_FAULT_ERROR_NO_ADDRESS_SPACE
};


class PageFaultError : public AbstractTraceEntry {
public:
	PageFaultError(area_id area, status_t error)
		:
		fArea(area),
		fError(error)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		switch (fError) {
			case PAGE_FAULT_ERROR_NO_AREA:
				out.Print("page fault error: no area");
				break;
			case PAGE_FAULT_ERROR_KERNEL_ONLY:
				out.Print("page fault error: area: %ld, kernel only", fArea);
				break;
			case PAGE_FAULT_ERROR_WRITE_PROTECTED:
				out.Print("page fault error: area: %ld, write protected",
					fArea);
				break;
			case PAGE_FAULT_ERROR_READ_PROTECTED:
				out.Print("page fault error: area: %ld, read protected", fArea);
				break;
			case PAGE_FAULT_ERROR_KERNEL_BAD_USER_MEMORY:
				out.Print("page fault error: kernel touching bad user memory");
				break;
			case PAGE_FAULT_ERROR_NO_ADDRESS_SPACE:
				out.Print("page fault error: no address space");
				break;
			default:
				out.Print("page fault error: area: %ld, error: %s", fArea,
					strerror(fError));
				break;
		}
	}

private:
	area_id		fArea;
	status_t	fError;
};


class PageFaultDone : public AbstractTraceEntry {
public:
	PageFaultDone(area_id area, VMCache* topCache, VMCache* cache,
			vm_page* page)
		:
		fArea(area),
		fTopCache(topCache),
		fCache(cache),
		fPage(page)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("page fault done: area: %ld, top cache: %p, cache: %p, "
			"page: %p", fArea, fTopCache, fCache, fPage);
	}

private:
	area_id		fArea;
	VMCache*	fTopCache;
	VMCache*	fCache;
	vm_page*	fPage;
};

}	// namespace VMPageFaultTracing

#	define TPF(x) new(std::nothrow) VMPageFaultTracing::x;
#else
#	define TPF(x) ;
#endif	// VM_PAGE_FAULT_TRACING


//	#pragma mark -


//! You need to have the address space locked when calling this function
static VMArea*
lookup_area(VMAddressSpace* addressSpace, area_id id)
{
	VMAreaHash::ReadLock();

	VMArea* area = VMAreaHash::LookupLocked(id);
	if (area != NULL && area->address_space != addressSpace)
		area = NULL;

	VMAreaHash::ReadUnlock();

	return area;
}


static inline void
set_area_page_protection(VMArea* area, addr_t pageAddress, uint32 protection)
{
	protection &= B_READ_AREA | B_WRITE_AREA | B_EXECUTE_AREA;
	uint32 pageIndex = (pageAddress - area->Base()) / B_PAGE_SIZE;
	uint8& entry = area->page_protections[pageIndex / 2];
	if (pageIndex % 2 == 0)
		entry = (entry & 0xf0) | protection;
	else
		entry = (entry & 0x0f) | (protection << 4);
}


static inline uint32
get_area_page_protection(VMArea* area, addr_t pageAddress)
{
	if (area->page_protections == NULL)
		return area->protection;

	uint32 pageIndex = (pageAddress - area->Base()) / B_PAGE_SIZE;
	uint32 protection = area->page_protections[pageIndex / 2];
	if (pageIndex % 2 == 0)
		protection &= 0x0f;
	else
		protection >>= 4;

	return protection | B_KERNEL_READ_AREA
		| (protection & B_WRITE_AREA ? B_KERNEL_WRITE_AREA : 0);
}


/*!	Cuts a piece out of an area. If the given cut range covers the complete
	area, it is deleted. If it covers the beginning or the end, the area is
	resized accordingly. If the range covers some part in the middle of the
	area, it is split in two; in this case the second area is returned via
	\a _secondArea (the variable is left untouched in the other cases).
	The address space must be write locked.
*/
static status_t
cut_area(VMAddressSpace* addressSpace, VMArea* area, addr_t address,
	addr_t lastAddress, VMArea** _secondArea, bool kernel)
{
	// Does the cut range intersect with the area at all?
	addr_t areaLast = area->Base() + (area->Size() - 1);
	if (area->Base() > lastAddress || areaLast < address)
		return B_OK;

	// Is the area fully covered?
	if (area->Base() >= address && areaLast <= lastAddress) {
		delete_area(addressSpace, area);
		return B_OK;
	}

	AreaCacheLocker cacheLocker(area);
	VMCache* cache = area->cache;

	// Cut the end only?
	if (areaLast <= lastAddress) {
		size_t oldSize = area->Size();
		size_t newSize = address - area->Base();

		status_t error = addressSpace->ShrinkAreaTail(area, newSize);
		if (error != B_OK)
			return error;

		// unmap pages
		vm_unmap_pages(area, address, oldSize - newSize, false);

		// If no one else uses the area's cache, we can resize it, too.
		if (cache->areas == area && area->cache_next == NULL
			&& list_is_empty(&cache->consumers)) {
			error = cache->Resize(cache->virtual_base + newSize);
			if (error != B_OK) {
				addressSpace->ShrinkAreaTail(area, oldSize);
				return error;
			}
		}

		return B_OK;
	}

	// Cut the beginning only?
	if (area->Base() >= address) {
		addr_t oldBase = area->Base();
		addr_t newBase = lastAddress + 1;
		size_t newSize = areaLast - lastAddress;

		// unmap pages
		vm_unmap_pages(area, oldBase, newBase - oldBase, false);

		// resize the area
		status_t error = addressSpace->ShrinkAreaHead(area, newSize);
		if (error != B_OK)
			return error;

		// TODO: If no one else uses the area's cache, we should resize it, too!

		area->cache_offset += newBase - oldBase;

		return B_OK;
	}

	// The tough part -- cut a piece out of the middle of the area.
	// We do that by shrinking the area to the begin section and creating a
	// new area for the end section.

	addr_t firstNewSize = address - area->Base();
	addr_t secondBase = lastAddress + 1;
	addr_t secondSize = areaLast - lastAddress;

	// unmap pages
	vm_unmap_pages(area, address, area->Size() - firstNewSize, false);

	// resize the area
	addr_t oldSize = area->Size();
	status_t error = addressSpace->ShrinkAreaTail(area, firstNewSize);
	if (error != B_OK)
		return error;

	// TODO: If no one else uses the area's cache, we might want to create a
	// new cache for the second area, transfer the concerned pages from the
	// first cache to it and resize the first cache.

	// map the second area
	VMArea* secondArea;
	void* secondBaseAddress = (void*)secondBase;
	error = map_backing_store(addressSpace, cache, &secondBaseAddress,
		area->cache_offset + (secondBase - area->Base()), secondSize,
		B_EXACT_ADDRESS, area->wiring, area->protection, REGION_NO_PRIVATE_MAP,
		&secondArea, area->name, false, kernel);
	if (error != B_OK) {
		addressSpace->ShrinkAreaTail(area, oldSize);
		return error;
	}

	// We need a cache reference for the new area.
	cache->AcquireRefLocked();

	if (_secondArea != NULL)
		*_secondArea = secondArea;

	return B_OK;
}


static inline void
increment_page_wired_count(vm_page* page)
{
	// TODO: needs to be atomic on all platforms!
	// ... but at least the check isn't. Consequently we should hold
	// sMappingLock, which would allows us to even avoid atomic_add() on
	// gMappedPagesCount.
	if (page->wired_count++ == 0) {
		if (page->mappings.IsEmpty())
			atomic_add(&gMappedPagesCount, 1);
	}
}


static inline void
decrement_page_wired_count(vm_page* page)
{
	if (--page->wired_count == 0) {
		// TODO: needs to be atomic on all platforms!
		// See above!
		if (page->mappings.IsEmpty())
			atomic_add(&gMappedPagesCount, -1);
	}
}


/*!	Deletes all areas in the given address range.
	The address space must be write-locked.
*/
static status_t
unmap_address_range(VMAddressSpace* addressSpace, addr_t address, addr_t size,
	bool kernel)
{
	size = PAGE_ALIGN(size);
	addr_t lastAddress = address + (size - 1);

	// Check, whether the caller is allowed to modify the concerned areas.
	if (!kernel) {
		for (VMAddressSpace::AreaIterator it = addressSpace->GetAreaIterator();
				VMArea* area = it.Next();) {
			addr_t areaLast = area->Base() + (area->Size() - 1);
			if (area->Base() < lastAddress && address < areaLast) {
				if ((area->protection & B_KERNEL_AREA) != 0)
					return B_NOT_ALLOWED;
			}
		}
	}

	for (VMAddressSpace::AreaIterator it = addressSpace->GetAreaIterator();
			VMArea* area = it.Next();) {
		addr_t areaLast = area->Base() + (area->Size() - 1);
		if (area->Base() < lastAddress && address < areaLast) {
			status_t error = cut_area(addressSpace, area, address,
				lastAddress, NULL, kernel);
			if (error != B_OK)
				return error;
				// Failing after already messing with areas is ugly, but we
				// can't do anything about it.
		}
	}

	return B_OK;
}


/*! You need to hold the lock of the cache and the write lock of the address
	space when calling this function.
	Note, that in case of error your cache will be temporarily unlocked.
*/
static status_t
map_backing_store(VMAddressSpace* addressSpace, VMCache* cache,
	void** _virtualAddress, off_t offset, addr_t size, uint32 addressSpec,
	int wiring, int protection, int mapping, VMArea** _area,
	const char* areaName, bool unmapAddressRange, bool kernel)
{
	TRACE(("map_backing_store: aspace %p, cache %p, *vaddr %p, offset 0x%Lx, "
		"size %lu, addressSpec %ld, wiring %d, protection %d, area %p, areaName "
		"'%s'\n", addressSpace, cache, *_virtualAddress, offset, size,
		addressSpec, wiring, protection, _area, areaName));
	cache->AssertLocked();

	VMArea* area = addressSpace->CreateArea(areaName, wiring, protection);
	if (area == NULL)
		return B_NO_MEMORY;

	status_t status;

	// if this is a private map, we need to create a new cache
	// to handle the private copies of pages as they are written to
	VMCache* sourceCache = cache;
	if (mapping == REGION_PRIVATE_MAP) {
		VMCache* newCache;

		// create an anonymous cache
		status = VMCacheFactory::CreateAnonymousCache(newCache,
			(protection & B_STACK_AREA) != 0, 0, USER_STACK_GUARD_PAGES, true);
		if (status != B_OK)
			goto err1;

		newCache->Lock();
		newCache->temporary = 1;
		newCache->scan_skip = cache->scan_skip;
		newCache->virtual_base = offset;
		newCache->virtual_end = offset + size;

		cache->AddConsumer(newCache);

		cache = newCache;
	}

	status = cache->SetMinimalCommitment(size);
	if (status != B_OK)
		goto err2;

	// check to see if this address space has entered DELETE state
	if (addressSpace->IsBeingDeleted()) {
		// okay, someone is trying to delete this address space now, so we can't
		// insert the area, so back out
		status = B_BAD_TEAM_ID;
		goto err2;
	}

	if (addressSpec == B_EXACT_ADDRESS && unmapAddressRange) {
		status = unmap_address_range(addressSpace, (addr_t)*_virtualAddress,
			size, kernel);
		if (status != B_OK)
			goto err2;
	}

	status = addressSpace->InsertArea(_virtualAddress, addressSpec, size, area);
	if (status != B_OK) {
		// TODO: wait and try again once this is working in the backend
#if 0
		if (status == B_NO_MEMORY && addressSpec == B_ANY_KERNEL_ADDRESS) {
			low_resource(B_KERNEL_RESOURCE_ADDRESS_SPACE, size,
				0, 0);
		}
#endif
		goto err2;
	}

	// attach the cache to the area
	area->cache = cache;
	area->cache_offset = offset;

	// point the cache back to the area
	cache->InsertAreaLocked(area);
	if (mapping == REGION_PRIVATE_MAP)
		cache->Unlock();

	// insert the area in the global area hash table
	VMAreaHash::Insert(area);

	// grab a ref to the address space (the area holds this)
	addressSpace->Get();

//	ktrace_printf("map_backing_store: cache: %p (source: %p), \"%s\" -> %p",
//		cache, sourceCache, areaName, area);

	*_area = area;
	return B_OK;

err2:
	if (mapping == REGION_PRIVATE_MAP) {
		// We created this cache, so we must delete it again. Note, that we
		// need to temporarily unlock the source cache or we'll otherwise
		// deadlock, since VMCache::_RemoveConsumer() will try to lock it, too.
		sourceCache->Unlock();
		cache->ReleaseRefAndUnlock();
		sourceCache->Lock();
	}
err1:
	addressSpace->DeleteArea(area);
	return status;
}


status_t
vm_block_address_range(const char* name, void* address, addr_t size)
{
	if (!arch_vm_supports_protection(0))
		return B_NOT_SUPPORTED;

	AddressSpaceWriteLocker locker;
	status_t status = locker.SetTo(VMAddressSpace::KernelID());
	if (status != B_OK)
		return status;

	VMAddressSpace* addressSpace = locker.AddressSpace();

	// create an anonymous cache
	VMCache* cache;
	status = VMCacheFactory::CreateAnonymousCache(cache, false, 0, 0, false);
	if (status != B_OK)
		return status;

	cache->temporary = 1;
	cache->virtual_end = size;
	cache->scan_skip = 1;
	cache->Lock();

	VMArea* area;
	void* areaAddress = address;
	status = map_backing_store(addressSpace, cache, &areaAddress, 0, size,
		B_EXACT_ADDRESS, B_ALREADY_WIRED, 0, REGION_NO_PRIVATE_MAP, &area, name,
		false, true);
	if (status != B_OK) {
		cache->ReleaseRefAndUnlock();
		return status;
	}

	cache->Unlock();
	area->cache_type = CACHE_TYPE_RAM;
	return area->id;
}


status_t
vm_unreserve_address_range(team_id team, void* address, addr_t size)
{
	AddressSpaceWriteLocker locker(team);
	if (!locker.IsLocked())
		return B_BAD_TEAM_ID;

	return locker.AddressSpace()->UnreserveAddressRange((addr_t)address, size);
}


status_t
vm_reserve_address_range(team_id team, void** _address, uint32 addressSpec,
	addr_t size, uint32 flags)
{
	if (size == 0)
		return B_BAD_VALUE;

	AddressSpaceWriteLocker locker(team);
	if (!locker.IsLocked())
		return B_BAD_TEAM_ID;

	return locker.AddressSpace()->ReserveAddressRange(_address, addressSpec,
		size, flags);
}


area_id
vm_create_anonymous_area(team_id team, const char* name, void** address,
	uint32 addressSpec, addr_t size, uint32 wiring, uint32 protection,
	addr_t physicalAddress, uint32 flags, bool kernel)
{
	VMArea* area;
	VMCache* cache;
	vm_page* page = NULL;
	bool isStack = (protection & B_STACK_AREA) != 0;
	page_num_t guardPages;
	bool canOvercommit = false;
	uint32 newPageState = (flags & CREATE_AREA_DONT_CLEAR) != 0
		? PAGE_STATE_FREE : PAGE_STATE_CLEAR;

	TRACE(("create_anonymous_area [%d] %s: size 0x%lx\n", team, name, size));

	size = PAGE_ALIGN(size);

	if (size == 0)
		return B_BAD_VALUE;
	if (!arch_vm_supports_protection(protection))
		return B_NOT_SUPPORTED;

	if (isStack || (protection & B_OVERCOMMITTING_AREA) != 0)
		canOvercommit = true;

#ifdef DEBUG_KERNEL_STACKS
	if ((protection & B_KERNEL_STACK_AREA) != 0)
		isStack = true;
#endif

	// check parameters
	switch (addressSpec) {
		case B_ANY_ADDRESS:
		case B_EXACT_ADDRESS:
		case B_BASE_ADDRESS:
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
			break;
		case B_PHYSICAL_BASE_ADDRESS:
			physicalAddress = (addr_t)*address;
			addressSpec = B_ANY_KERNEL_ADDRESS;
			break;

		default:
			return B_BAD_VALUE;
	}

	if (physicalAddress != 0)
		wiring = B_CONTIGUOUS;

	bool doReserveMemory = false;
	switch (wiring) {
		case B_NO_LOCK:
			break;
		case B_FULL_LOCK:
		case B_LAZY_LOCK:
		case B_CONTIGUOUS:
			doReserveMemory = true;
			break;
		case B_ALREADY_WIRED:
			break;
		case B_LOMEM:
		//case B_SLOWMEM:
			dprintf("B_LOMEM/SLOWMEM is not yet supported!\n");
			wiring = B_FULL_LOCK;
			doReserveMemory = true;
			break;
		default:
			return B_BAD_VALUE;
	}

	// For full lock or contiguous areas we're also going to map the pages and
	// thus need to reserve pages for the mapping backend upfront.
	addr_t reservedMapPages = 0;
	if (wiring == B_FULL_LOCK || wiring == B_CONTIGUOUS) {
		AddressSpaceWriteLocker locker;
		status_t status = locker.SetTo(team);
		if (status != B_OK)
			return status;

		vm_translation_map* map = &locker.AddressSpace()->TranslationMap();
		reservedMapPages = map->ops->map_max_pages_need(map, 0, size - 1);
	}

	// Reserve memory before acquiring the address space lock. This reduces the
	// chances of failure, since while holding the write lock to the address
	// space (if it is the kernel address space that is), the low memory handler
	// won't be able to free anything for us.
	addr_t reservedMemory = 0;
	if (doReserveMemory) {
		bigtime_t timeout = (flags & CREATE_AREA_DONT_WAIT) != 0 ? 0 : 1000000;
		if (vm_try_reserve_memory(size, timeout) != B_OK)
			return B_NO_MEMORY;
		reservedMemory = size;
		// TODO: We don't reserve the memory for the pages for the page
		// directories/tables. We actually need to do since we currently don't
		// reclaim them (and probably can't reclaim all of them anyway). Thus
		// there are actually less physical pages than there should be, which
		// can get the VM into trouble in low memory situations.
	}

	AddressSpaceWriteLocker locker;
	VMAddressSpace* addressSpace;
	status_t status;

	// For full lock areas reserve the pages before locking the address
	// space. E.g. block caches can't release their memory while we hold the
	// address space lock.
	page_num_t reservedPages = reservedMapPages;
	if (wiring == B_FULL_LOCK)
		reservedPages += size / B_PAGE_SIZE;
	if (reservedPages > 0) {
		if ((flags & CREATE_AREA_DONT_WAIT) != 0) {
			if (!vm_page_try_reserve_pages(reservedPages)) {
				reservedPages = 0;
				status = B_WOULD_BLOCK;
				goto err0;
			}
		} else
			vm_page_reserve_pages(reservedPages);
	}

	status = locker.SetTo(team);
	if (status != B_OK)
		goto err0;

	addressSpace = locker.AddressSpace();

	if (wiring == B_CONTIGUOUS) {
		// we try to allocate the page run here upfront as this may easily
		// fail for obvious reasons
		page = vm_page_allocate_page_run(newPageState, physicalAddress,
			size / B_PAGE_SIZE);
		if (page == NULL) {
			status = B_NO_MEMORY;
			goto err0;
		}
	}

	// create an anonymous cache
	// if it's a stack, make sure that two pages are available at least
	guardPages = isStack ? ((protection & B_USER_PROTECTION) != 0
		? USER_STACK_GUARD_PAGES : KERNEL_STACK_GUARD_PAGES) : 0;
	status = VMCacheFactory::CreateAnonymousCache(cache, canOvercommit,
		isStack ? (min_c(2, size / B_PAGE_SIZE - guardPages)) : 0, guardPages,
		wiring == B_NO_LOCK);
	if (status != B_OK)
		goto err1;

	cache->temporary = 1;
	cache->virtual_end = size;
	cache->committed_size = reservedMemory;
		// TODO: This should be done via a method.
	reservedMemory = 0;

	switch (wiring) {
		case B_LAZY_LOCK:
		case B_FULL_LOCK:
		case B_CONTIGUOUS:
		case B_ALREADY_WIRED:
			cache->scan_skip = 1;
			break;
		case B_NO_LOCK:
			cache->scan_skip = 0;
			break;
	}

	cache->Lock();

	status = map_backing_store(addressSpace, cache, address, 0, size,
		addressSpec, wiring, protection, REGION_NO_PRIVATE_MAP, &area, name,
		(flags & CREATE_AREA_UNMAP_ADDRESS_RANGE) != 0, kernel);

	if (status != B_OK) {
		cache->ReleaseRefAndUnlock();
		goto err1;
	}

	locker.DegradeToReadLock();

	switch (wiring) {
		case B_NO_LOCK:
		case B_LAZY_LOCK:
			// do nothing - the pages are mapped in as needed
			break;

		case B_FULL_LOCK:
		{
			// Allocate and map all pages for this area

			off_t offset = 0;
			for (addr_t address = area->Base();
					address < area->Base() + (area->Size() - 1);
					address += B_PAGE_SIZE, offset += B_PAGE_SIZE) {
#ifdef DEBUG_KERNEL_STACKS
#	ifdef STACK_GROWS_DOWNWARDS
				if (isStack && address < area->Base()
						+ KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE)
#	else
				if (isStack && address >= area->Base() + area->Size()
						- KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE)
#	endif
					continue;
#endif
				vm_page* page = vm_page_allocate_page(newPageState, true);
				cache->InsertPage(page, offset);
				vm_map_page(area, page, address, protection);

				// Periodically unreserve pages we've already allocated, so that
				// we don't unnecessarily increase the pressure on the VM.
				if (offset > 0 && offset % (128 * B_PAGE_SIZE) == 0) {
					page_num_t toUnreserve = 128;
					vm_page_unreserve_pages(toUnreserve);
					reservedPages -= toUnreserve;
				}
			}

			break;
		}

		case B_ALREADY_WIRED:
		{
			// The pages should already be mapped. This is only really useful
			// during boot time. Find the appropriate vm_page objects and stick
			// them in the cache object.
			vm_translation_map* map = &addressSpace->TranslationMap();
			off_t offset = 0;

			if (!gKernelStartup)
				panic("ALREADY_WIRED flag used outside kernel startup\n");

			map->ops->lock(map);

			for (addr_t virtualAddress = area->Base();
					virtualAddress < area->Base() + (area->Size() - 1);
					virtualAddress += B_PAGE_SIZE, offset += B_PAGE_SIZE) {
				addr_t physicalAddress;
				uint32 flags;
				status = map->ops->query(map, virtualAddress,
					&physicalAddress, &flags);
				if (status < B_OK) {
					panic("looking up mapping failed for va 0x%lx\n",
						virtualAddress);
				}
				page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
				if (page == NULL) {
					panic("looking up page failed for pa 0x%lx\n",
						physicalAddress);
				}

				increment_page_wired_count(page);
				vm_page_set_state(page, PAGE_STATE_WIRED);
				cache->InsertPage(page, offset);
			}

			map->ops->unlock(map);
			break;
		}

		case B_CONTIGUOUS:
		{
			// We have already allocated our continuous pages run, so we can now
			// just map them in the address space
			vm_translation_map* map = &addressSpace->TranslationMap();
			addr_t physicalAddress = page->physical_page_number * B_PAGE_SIZE;
			addr_t virtualAddress = area->Base();
			off_t offset = 0;

			map->ops->lock(map);

			for (virtualAddress = area->Base(); virtualAddress < area->Base()
					+ (area->Size() - 1); virtualAddress += B_PAGE_SIZE,
					offset += B_PAGE_SIZE, physicalAddress += B_PAGE_SIZE) {
				page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
				if (page == NULL)
					panic("couldn't lookup physical page just allocated\n");

				status = map->ops->map(map, virtualAddress, physicalAddress,
					protection);
				if (status < B_OK)
					panic("couldn't map physical page in page run\n");

				increment_page_wired_count(page);
				vm_page_set_state(page, PAGE_STATE_WIRED);
				cache->InsertPage(page, offset);
			}

			map->ops->unlock(map);
			break;
		}

		default:
			break;
	}

	cache->Unlock();

	if (reservedPages > 0)
		vm_page_unreserve_pages(reservedPages);

	TRACE(("vm_create_anonymous_area: done\n"));

	area->cache_type = CACHE_TYPE_RAM;
	return area->id;

err1:
	if (wiring == B_CONTIGUOUS) {
		// we had reserved the area space upfront...
		addr_t pageNumber = page->physical_page_number;
		int32 i;
		for (i = size / B_PAGE_SIZE; i-- > 0; pageNumber++) {
			page = vm_lookup_page(pageNumber);
			if (page == NULL)
				panic("couldn't lookup physical page just allocated\n");

			vm_page_set_state(page, PAGE_STATE_FREE);
		}
	}

err0:
	if (reservedPages > 0)
		vm_page_unreserve_pages(reservedPages);
	if (reservedMemory > 0)
		vm_unreserve_memory(reservedMemory);

	return status;
}


area_id
vm_map_physical_memory(team_id team, const char* name, void** _address,
	uint32 addressSpec, addr_t size, uint32 protection, addr_t physicalAddress)
{
	VMArea* area;
	VMCache* cache;
	addr_t mapOffset;

	TRACE(("vm_map_physical_memory(aspace = %ld, \"%s\", virtual = %p, "
		"spec = %ld, size = %lu, protection = %ld, phys = %#lx)\n", team,
		name, _address, addressSpec, size, protection, physicalAddress));

	if (!arch_vm_supports_protection(protection))
		return B_NOT_SUPPORTED;

	AddressSpaceWriteLocker locker(team);
	if (!locker.IsLocked())
		return B_BAD_TEAM_ID;

	// if the physical address is somewhat inside a page,
	// move the actual area down to align on a page boundary
	mapOffset = physicalAddress % B_PAGE_SIZE;
	size += mapOffset;
	physicalAddress -= mapOffset;

	size = PAGE_ALIGN(size);

	// create a device cache
	status_t status = VMCacheFactory::CreateDeviceCache(cache, physicalAddress);
	if (status != B_OK)
		return status;

	// tell the page scanner to skip over this area, it's pages are special
	cache->scan_skip = 1;
	cache->virtual_end = size;

	cache->Lock();

	status = map_backing_store(locker.AddressSpace(), cache, _address,
		0, size, addressSpec & ~B_MTR_MASK, B_FULL_LOCK, protection,
		REGION_NO_PRIVATE_MAP, &area, name, false, true);

	if (status < B_OK)
		cache->ReleaseRefLocked();

	cache->Unlock();

	if (status >= B_OK && (addressSpec & B_MTR_MASK) != 0) {
		// set requested memory type
		status = arch_vm_set_memory_type(area, physicalAddress,
			addressSpec & B_MTR_MASK);
		if (status < B_OK)
			delete_area(locker.AddressSpace(), area);
	}

	if (status >= B_OK) {
		// make sure our area is mapped in completely

		vm_translation_map* map = &locker.AddressSpace()->TranslationMap();
		size_t reservePages = map->ops->map_max_pages_need(map, area->Base(),
			area->Base() + (size - 1));

		vm_page_reserve_pages(reservePages);
		map->ops->lock(map);

		for (addr_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
			map->ops->map(map, area->Base() + offset, physicalAddress + offset,
				protection);
		}

		map->ops->unlock(map);
		vm_page_unreserve_pages(reservePages);
	}

	if (status < B_OK)
		return status;

	// modify the pointer returned to be offset back into the new area
	// the same way the physical address in was offset
	*_address = (void*)((addr_t)*_address + mapOffset);

	area->cache_type = CACHE_TYPE_DEVICE;
	return area->id;
}


area_id
vm_map_physical_memory_vecs(team_id team, const char* name, void** _address,
	uint32 addressSpec, addr_t* _size, uint32 protection, struct iovec* vecs,
	uint32 vecCount)
{
	TRACE(("vm_map_physical_memory_vecs(team = %ld, \"%s\", virtual = %p, "
		"spec = %ld, size = %lu, protection = %ld, phys = %#lx)\n", team,
		name, _address, addressSpec, size, protection, physicalAddress));

	if (!arch_vm_supports_protection(protection)
		|| (addressSpec & B_MTR_MASK) != 0) {
		return B_NOT_SUPPORTED;
	}

	AddressSpaceWriteLocker locker(team);
	if (!locker.IsLocked())
		return B_BAD_TEAM_ID;

	if (vecCount == 0)
		return B_BAD_VALUE;

	addr_t size = 0;
	for (uint32 i = 0; i < vecCount; i++) {
		if ((addr_t)vecs[i].iov_base % B_PAGE_SIZE != 0
			|| vecs[i].iov_len % B_PAGE_SIZE != 0) {
			return B_BAD_VALUE;
		}

		size += vecs[i].iov_len;
	}

	// create a device cache
	VMCache* cache;
	status_t result = VMCacheFactory::CreateDeviceCache(cache,
		(addr_t)vecs[0].iov_base);
	if (result != B_OK)
		return result;

	// tell the page scanner to skip over this area, it's pages are special
	cache->scan_skip = 1;
	cache->virtual_end = size;

	cache->Lock();

	VMArea* area;
	result = map_backing_store(locker.AddressSpace(), cache, _address,
		0, size, addressSpec & ~B_MTR_MASK, B_FULL_LOCK, protection,
		REGION_NO_PRIVATE_MAP, &area, name, false, true);

	if (result != B_OK)
		cache->ReleaseRefLocked();

	cache->Unlock();

	if (result != B_OK)
		return result;

	vm_translation_map* map = &locker.AddressSpace()->TranslationMap();
	size_t reservePages = map->ops->map_max_pages_need(map, area->Base(),
		area->Base() + (size - 1));

	vm_page_reserve_pages(reservePages);
	map->ops->lock(map);

	uint32 vecIndex = 0;
	size_t vecOffset = 0;
	for (addr_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
		while (vecOffset >= vecs[vecIndex].iov_len && vecIndex < vecCount) {
			vecOffset = 0;
			vecIndex++;
		}

		if (vecIndex >= vecCount)
			break;

		map->ops->map(map, area->Base() + offset,
			(addr_t)vecs[vecIndex].iov_base + vecOffset, protection);

		vecOffset += B_PAGE_SIZE;
	}

	map->ops->unlock(map);
	vm_page_unreserve_pages(reservePages);

	if (_size != NULL)
		*_size = size;

	area->cache_type = CACHE_TYPE_DEVICE;
	return area->id;
}


area_id
vm_create_null_area(team_id team, const char* name, void** address,
	uint32 addressSpec, addr_t size)
{
	VMArea* area;
	VMCache* cache;
	status_t status;

	AddressSpaceWriteLocker locker(team);
	if (!locker.IsLocked())
		return B_BAD_TEAM_ID;

	size = PAGE_ALIGN(size);

	// create an null cache
	status = VMCacheFactory::CreateNullCache(cache);
	if (status != B_OK)
		return status;

	// tell the page scanner to skip over this area, no pages will be mapped here
	cache->scan_skip = 1;
	cache->virtual_end = size;

	cache->Lock();

	status = map_backing_store(locker.AddressSpace(), cache, address, 0, size,
		addressSpec, 0, B_KERNEL_READ_AREA, REGION_NO_PRIVATE_MAP, &area, name,
		false, true);

	if (status < B_OK) {
		cache->ReleaseRefAndUnlock();
		return status;
	}

	cache->Unlock();

	area->cache_type = CACHE_TYPE_NULL;
	return area->id;
}


/*!	Creates the vnode cache for the specified \a vnode.
	The vnode has to be marked busy when calling this function.
*/
status_t
vm_create_vnode_cache(struct vnode* vnode, struct VMCache** cache)
{
	return VMCacheFactory::CreateVnodeCache(*cache, vnode);
}


/*!	\a cache must be locked. The area's address space must be read-locked.
*/
static void
pre_map_area_pages(VMArea* area, VMCache* cache)
{
	addr_t baseAddress = area->Base();
	addr_t cacheOffset = area->cache_offset;
	page_num_t firstPage = cacheOffset / B_PAGE_SIZE;
	page_num_t endPage = firstPage + area->Size() / B_PAGE_SIZE;

	for (VMCachePagesTree::Iterator it
				= cache->pages.GetIterator(firstPage, true, true);
			vm_page* page = it.Next();) {
		if (page->cache_offset >= endPage)
			break;

		// skip inactive pages
		if (page->state == PAGE_STATE_BUSY || page->usage_count <= 0)
			continue;

		vm_map_page(area, page,
			baseAddress + (page->cache_offset * B_PAGE_SIZE - cacheOffset),
			B_READ_AREA | B_KERNEL_READ_AREA);
	}
}


/*!	Will map the file specified by \a fd to an area in memory.
	The file will be mirrored beginning at the specified \a offset. The
	\a offset and \a size arguments have to be page aligned.
*/
static area_id
_vm_map_file(team_id team, const char* name, void** _address,
	uint32 addressSpec, size_t size, uint32 protection, uint32 mapping,
	bool unmapAddressRange, int fd, off_t offset, bool kernel)
{
	// TODO: for binary files, we want to make sure that they get the
	//	copy of a file at a given time, ie. later changes should not
	//	make it into the mapped copy -- this will need quite some changes
	//	to be done in a nice way
	TRACE(("_vm_map_file(fd = %d, offset = %Ld, size = %lu, mapping %ld)\n",
		fd, offset, size, mapping));

	offset = ROUNDDOWN(offset, B_PAGE_SIZE);
	size = PAGE_ALIGN(size);

	if (mapping == REGION_NO_PRIVATE_MAP)
		protection |= B_SHARED_AREA;
	if (addressSpec != B_EXACT_ADDRESS)
		unmapAddressRange = false;

	if (fd < 0) {
		uint32 flags = unmapAddressRange ? CREATE_AREA_UNMAP_ADDRESS_RANGE : 0;
		return vm_create_anonymous_area(team, name, _address, addressSpec, size,
			B_NO_LOCK, protection, 0, flags, kernel);
	}

	// get the open flags of the FD
	file_descriptor* descriptor = get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return EBADF;
	int32 openMode = descriptor->open_mode;
	put_fd(descriptor);

	// The FD must open for reading at any rate. For shared mapping with write
	// access, additionally the FD must be open for writing.
	if ((openMode & O_ACCMODE) == O_WRONLY
		|| (mapping == REGION_NO_PRIVATE_MAP
			&& (protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0
			&& (openMode & O_ACCMODE) == O_RDONLY)) {
		return EACCES;
	}

	// get the vnode for the object, this also grabs a ref to it
	struct vnode* vnode = NULL;
	status_t status = vfs_get_vnode_from_fd(fd, kernel, &vnode);
	if (status < B_OK)
		return status;
	CObjectDeleter<struct vnode> vnodePutter(vnode, vfs_put_vnode);

	// If we're going to pre-map pages, we need to reserve the pages needed by
	// the mapping backend upfront.
	page_num_t reservedPreMapPages = 0;
	if ((protection & B_READ_AREA) != 0) {
		AddressSpaceWriteLocker locker;
		status = locker.SetTo(team);
		if (status != B_OK)
			return status;

		vm_translation_map* map = &locker.AddressSpace()->TranslationMap();
		reservedPreMapPages = map->ops->map_max_pages_need(map, 0, size - 1);

		locker.Unlock();

		vm_page_reserve_pages(reservedPreMapPages);
	}

	struct PageUnreserver {
		PageUnreserver(page_num_t count)
			: fCount(count)
		{
		}

		~PageUnreserver()
		{
			if (fCount > 0)
				vm_page_unreserve_pages(fCount);
		}

		page_num_t	fCount;
	} pageUnreserver(reservedPreMapPages);

	AddressSpaceWriteLocker locker(team);
	if (!locker.IsLocked())
		return B_BAD_TEAM_ID;

	// TODO: this only works for file systems that use the file cache
	VMCache* cache;
	status = vfs_get_vnode_cache(vnode, &cache, false);
	if (status < B_OK)
		return status;

	cache->Lock();

	VMArea* area;
	status = map_backing_store(locker.AddressSpace(), cache, _address,
		offset, size, addressSpec, 0, protection, mapping, &area, name,
		unmapAddressRange, kernel);

	if (status != B_OK || mapping == REGION_PRIVATE_MAP) {
		// map_backing_store() cannot know we no longer need the ref
		cache->ReleaseRefLocked();
	}

	if (status == B_OK && (protection & B_READ_AREA) != 0)
		pre_map_area_pages(area, cache);

	cache->Unlock();

	if (status == B_OK) {
		// TODO: this probably deserves a smarter solution, ie. don't always
		// prefetch stuff, and also, probably don't trigger it at this place.
		cache_prefetch_vnode(vnode, offset, min_c(size, 10LL * 1024 * 1024));
			// prefetches at max 10 MB starting from "offset"
	}

	if (status != B_OK)
		return status;

	area->cache_type = CACHE_TYPE_VNODE;
	return area->id;
}


area_id
vm_map_file(team_id aid, const char* name, void** address, uint32 addressSpec,
	addr_t size, uint32 protection, uint32 mapping, bool unmapAddressRange,
	int fd, off_t offset)
{
	if (!arch_vm_supports_protection(protection))
		return B_NOT_SUPPORTED;

	return _vm_map_file(aid, name, address, addressSpec, size, protection,
		mapping, unmapAddressRange, fd, offset, true);
}


VMCache*
vm_area_get_locked_cache(VMArea* area)
{
	mutex_lock(&sAreaCacheLock);

	while (true) {
		VMCache* cache = area->cache;

		if (!cache->SwitchLock(&sAreaCacheLock)) {
			// cache has been deleted
			mutex_lock(&sAreaCacheLock);
			continue;
		}

		mutex_lock(&sAreaCacheLock);

		if (cache == area->cache) {
			cache->AcquireRefLocked();
			mutex_unlock(&sAreaCacheLock);
			return cache;
		}

		// the cache changed in the meantime
		cache->Unlock();
	}
}


void
vm_area_put_locked_cache(VMCache* cache)
{
	cache->ReleaseRefAndUnlock();
}


area_id
vm_clone_area(team_id team, const char* name, void** address,
	uint32 addressSpec, uint32 protection, uint32 mapping, area_id sourceID,
	bool kernel)
{
	VMArea* newArea = NULL;
	VMArea* sourceArea;

	// Check whether the source area exists and is cloneable. If so, mark it
	// B_SHARED_AREA, so that we don't get problems with copy-on-write.
	{
		AddressSpaceWriteLocker locker;
		status_t status = locker.SetFromArea(sourceID, sourceArea);
		if (status != B_OK)
			return status;

		if (!kernel && (sourceArea->protection & B_KERNEL_AREA) != 0)
			return B_NOT_ALLOWED;

		sourceArea->protection |= B_SHARED_AREA;
		protection |= B_SHARED_AREA;
	}

	// Now lock both address spaces and actually do the cloning.

	MultiAddressSpaceLocker locker;
	VMAddressSpace* sourceAddressSpace;
	status_t status = locker.AddArea(sourceID, false, &sourceAddressSpace);
	if (status != B_OK)
		return status;

	VMAddressSpace* targetAddressSpace;
	status = locker.AddTeam(team, true, &targetAddressSpace);
	if (status != B_OK)
		return status;

	status = locker.Lock();
	if (status != B_OK)
		return status;

	sourceArea = lookup_area(sourceAddressSpace, sourceID);
	if (sourceArea == NULL)
		return B_BAD_VALUE;

	if (!kernel && (sourceArea->protection & B_KERNEL_AREA) != 0)
		return B_NOT_ALLOWED;

	VMCache* cache = vm_area_get_locked_cache(sourceArea);

	// TODO: for now, B_USER_CLONEABLE is disabled, until all drivers
	//	have been adapted. Maybe it should be part of the kernel settings,
	//	anyway (so that old drivers can always work).
#if 0
	if (sourceArea->aspace == VMAddressSpace::Kernel()
		&& addressSpace != VMAddressSpace::Kernel()
		&& !(sourceArea->protection & B_USER_CLONEABLE_AREA)) {
		// kernel areas must not be cloned in userland, unless explicitly
		// declared user-cloneable upon construction
		status = B_NOT_ALLOWED;
	} else
#endif
	if (sourceArea->cache_type == CACHE_TYPE_NULL)
		status = B_NOT_ALLOWED;
	else {
		status = map_backing_store(targetAddressSpace, cache, address,
			sourceArea->cache_offset, sourceArea->Size(), addressSpec,
			sourceArea->wiring, protection, mapping, &newArea, name, false,
			kernel);
	}
	if (status == B_OK && mapping != REGION_PRIVATE_MAP) {
		// If the mapping is REGION_PRIVATE_MAP, map_backing_store() needed
		// to create a new cache, and has therefore already acquired a reference
		// to the source cache - but otherwise it has no idea that we need
		// one.
		cache->AcquireRefLocked();
	}
	if (status == B_OK && newArea->wiring == B_FULL_LOCK) {
		// we need to map in everything at this point
		if (sourceArea->cache_type == CACHE_TYPE_DEVICE) {
			// we don't have actual pages to map but a physical area
			vm_translation_map* map
				= &sourceArea->address_space->TranslationMap();
			map->ops->lock(map);

			addr_t physicalAddress;
			uint32 oldProtection;
			map->ops->query(map, sourceArea->Base(), &physicalAddress,
				&oldProtection);

			map->ops->unlock(map);

			map = &targetAddressSpace->TranslationMap();
			size_t reservePages = map->ops->map_max_pages_need(map,
				newArea->Base(), newArea->Base() + (newArea->Size() - 1));

			vm_page_reserve_pages(reservePages);
			map->ops->lock(map);

			for (addr_t offset = 0; offset < newArea->Size();
					offset += B_PAGE_SIZE) {
				map->ops->map(map, newArea->Base() + offset,
					physicalAddress + offset, protection);
			}

			map->ops->unlock(map);
			vm_page_unreserve_pages(reservePages);
		} else {
			vm_translation_map* map = &targetAddressSpace->TranslationMap();
			size_t reservePages = map->ops->map_max_pages_need(map,
				newArea->Base(), newArea->Base() + (newArea->Size() - 1));
			vm_page_reserve_pages(reservePages);

			// map in all pages from source
			for (VMCachePagesTree::Iterator it = cache->pages.GetIterator();
					vm_page* page  = it.Next();) {
				vm_map_page(newArea, page, newArea->Base()
					+ ((page->cache_offset << PAGE_SHIFT)
					- newArea->cache_offset), protection);
			}

			vm_page_unreserve_pages(reservePages);
		}
	}
	if (status == B_OK)
		newArea->cache_type = sourceArea->cache_type;

	vm_area_put_locked_cache(cache);

	if (status < B_OK)
		return status;

	return newArea->id;
}


static void
delete_area(VMAddressSpace* addressSpace, VMArea* area)
{
	VMAreaHash::Remove(area);

	// At this point the area is removed from the global hash table, but
	// still exists in the area list.

	// Unmap the virtual address space the area occupied
	vm_unmap_pages(area, area->Base(), area->Size(), !area->cache->temporary);

	if (!area->cache->temporary)
		area->cache->WriteModified();

	arch_vm_unset_memory_type(area);
	addressSpace->RemoveArea(area);
	addressSpace->Put();

	area->cache->RemoveArea(area);
	area->cache->ReleaseRef();

	addressSpace->DeleteArea(area);
}


status_t
vm_delete_area(team_id team, area_id id, bool kernel)
{
	TRACE(("vm_delete_area(team = 0x%lx, area = 0x%lx)\n", team, id));

	AddressSpaceWriteLocker locker;
	VMArea* area;
	status_t status = locker.SetFromArea(team, id, area);
	if (status != B_OK)
		return status;

	if (!kernel && (area->protection & B_KERNEL_AREA) != 0)
		return B_NOT_ALLOWED;

	delete_area(locker.AddressSpace(), area);
	return B_OK;
}


/*!	Creates a new cache on top of given cache, moves all areas from
	the old cache to the new one, and changes the protection of all affected
	areas' pages to read-only.
	Preconditions:
	- The given cache must be locked.
	- All of the cache's areas' address spaces must be read locked.
*/
static status_t
vm_copy_on_write_area(VMCache* lowerCache)
{
	VMCache* upperCache;

	TRACE(("vm_copy_on_write_area(cache = %p)\n", lowerCache));

	// We need to separate the cache from its areas. The cache goes one level
	// deeper and we create a new cache inbetween.

	// create an anonymous cache
	status_t status = VMCacheFactory::CreateAnonymousCache(upperCache, false, 0,
		0, true);
	if (status != B_OK)
		return status;

	upperCache->Lock();

	upperCache->temporary = 1;
	upperCache->scan_skip = lowerCache->scan_skip;
	upperCache->virtual_base = lowerCache->virtual_base;
	upperCache->virtual_end = lowerCache->virtual_end;

	// transfer the lower cache areas to the upper cache
	mutex_lock(&sAreaCacheLock);

	upperCache->areas = lowerCache->areas;
	lowerCache->areas = NULL;

	for (VMArea* tempArea = upperCache->areas; tempArea != NULL;
			tempArea = tempArea->cache_next) {
		tempArea->cache = upperCache;
		upperCache->AcquireRefLocked();
		lowerCache->ReleaseRefLocked();
	}

	mutex_unlock(&sAreaCacheLock);

	lowerCache->AddConsumer(upperCache);

	// We now need to remap all pages from all of the cache's areas read-only, so
	// that a copy will be created on next write access

	for (VMArea* tempArea = upperCache->areas; tempArea != NULL;
			tempArea = tempArea->cache_next) {
		// The area must be readable in the same way it was previously writable
		uint32 protection = B_KERNEL_READ_AREA;
		if ((tempArea->protection & B_READ_AREA) != 0)
			protection |= B_READ_AREA;

		vm_translation_map* map = &tempArea->address_space->TranslationMap();
		map->ops->lock(map);
		map->ops->protect(map, tempArea->Base(),
			tempArea->Base() - 1 + tempArea->Size(), protection);
		map->ops->unlock(map);
	}

	vm_area_put_locked_cache(upperCache);

	return B_OK;
}


area_id
vm_copy_area(team_id team, const char* name, void** _address,
	uint32 addressSpec, uint32 protection, area_id sourceID)
{
	bool writableCopy = (protection & (B_KERNEL_WRITE_AREA | B_WRITE_AREA)) != 0;

	if ((protection & B_KERNEL_PROTECTION) == 0) {
		// set the same protection for the kernel as for userland
		protection |= B_KERNEL_READ_AREA;
		if (writableCopy)
			protection |= B_KERNEL_WRITE_AREA;
	}

	// Do the locking: target address space, all address spaces associated with
	// the source cache, and the cache itself.
	MultiAddressSpaceLocker locker;
	VMAddressSpace* targetAddressSpace;
	VMCache* cache;
	VMArea* source;
	status_t status = locker.AddTeam(team, true, &targetAddressSpace);
	if (status == B_OK) {
		status = locker.AddAreaCacheAndLock(sourceID, false, false, source,
			&cache);
	}
	if (status != B_OK)
		return status;

	AreaCacheLocker cacheLocker(cache);	// already locked

	if (addressSpec == B_CLONE_ADDRESS) {
		addressSpec = B_EXACT_ADDRESS;
		*_address = (void*)source->Base();
	}

	bool sharedArea = (source->protection & B_SHARED_AREA) != 0;

	// First, create a cache on top of the source area, respectively use the
	// existing one, if this is a shared area.

	VMArea* target;
	status = map_backing_store(targetAddressSpace, cache, _address,
		source->cache_offset, source->Size(), addressSpec, source->wiring,
		protection, sharedArea ? REGION_NO_PRIVATE_MAP : REGION_PRIVATE_MAP,
		&target, name, false, true);
	if (status < B_OK)
		return status;

	if (sharedArea) {
		// The new area uses the old area's cache, but map_backing_store()
		// hasn't acquired a ref. So we have to do that now.
		cache->AcquireRefLocked();
	}

	// If the source area is writable, we need to move it one layer up as well

	if (!sharedArea) {
		if ((source->protection & (B_KERNEL_WRITE_AREA | B_WRITE_AREA)) != 0) {
			// TODO: do something more useful if this fails!
			if (vm_copy_on_write_area(cache) < B_OK)
				panic("vm_copy_on_write_area() failed!\n");
		}
	}

	// we return the ID of the newly created area
	return target->id;
}


//! You need to hold the cache lock when calling this function
static int32
count_writable_areas(VMCache* cache, VMArea* ignoreArea)
{
	struct VMArea* area = cache->areas;
	uint32 count = 0;

	for (; area != NULL; area = area->cache_next) {
		if (area != ignoreArea
			&& (area->protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0)
			count++;
	}

	return count;
}


static status_t
vm_set_area_protection(team_id team, area_id areaID, uint32 newProtection,
	bool kernel)
{
	TRACE(("vm_set_area_protection(team = %#lx, area = %#lx, protection = "
		"%#lx)\n", team, areaID, newProtection));

	if (!arch_vm_supports_protection(newProtection))
		return B_NOT_SUPPORTED;

	// lock address spaces and cache
	MultiAddressSpaceLocker locker;
	VMCache* cache;
	VMArea* area;
	status_t status = locker.AddAreaCacheAndLock(areaID, true, false, area,
		&cache);
	AreaCacheLocker cacheLocker(cache);	// already locked

	if (!kernel && (area->protection & B_KERNEL_AREA) != 0)
		return B_NOT_ALLOWED;

	if (area->protection == newProtection)
		return B_OK;

	if (team != VMAddressSpace::KernelID()
		&& area->address_space->ID() != team) {
		// unless you're the kernel, you are only allowed to set
		// the protection of your own areas
		return B_NOT_ALLOWED;
	}

	bool changePageProtection = true;

	if ((area->protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0
		&& (newProtection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) == 0) {
		// writable -> !writable

		if (cache->source != NULL && cache->temporary) {
			if (count_writable_areas(cache, area) == 0) {
				// Since this cache now lives from the pages in its source cache,
				// we can change the cache's commitment to take only those pages
				// into account that really are in this cache.

				status = cache->Commit(cache->page_count * B_PAGE_SIZE);

				// TODO: we may be able to join with our source cache, if
				// count == 0
			}
		}
	} else if ((area->protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) == 0
		&& (newProtection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0) {
		// !writable -> writable

		if (!list_is_empty(&cache->consumers)) {
			// There are consumers -- we have to insert a new cache. Fortunately
			// vm_copy_on_write_area() does everything that's needed.
			changePageProtection = false;
			status = vm_copy_on_write_area(cache);
		} else {
			// No consumers, so we don't need to insert a new one.
			if (cache->source != NULL && cache->temporary) {
				// the cache's commitment must contain all possible pages
				status = cache->Commit(cache->virtual_end
					- cache->virtual_base);
			}

			if (status == B_OK && cache->source != NULL) {
				// There's a source cache, hence we can't just change all pages'
				// protection or we might allow writing into pages belonging to
				// a lower cache.
				changePageProtection = false;

				struct vm_translation_map* map
					= &area->address_space->TranslationMap();
				map->ops->lock(map);

				for (VMCachePagesTree::Iterator it = cache->pages.GetIterator();
						vm_page* page = it.Next();) {
					addr_t address = area->Base()
						+ (page->cache_offset << PAGE_SHIFT);
					map->ops->protect(map, address, address - 1 + B_PAGE_SIZE,
						newProtection);
				}

				map->ops->unlock(map);
			}
		}
	} else {
		// we don't have anything special to do in all other cases
	}

	if (status == B_OK) {
		// remap existing pages in this cache
		struct vm_translation_map* map = &area->address_space->TranslationMap();

		if (changePageProtection) {
			map->ops->lock(map);
			map->ops->protect(map,
				area->Base(), area->Base() - 1 + area->Size(), newProtection);
			map->ops->unlock(map);
		}

		area->protection = newProtection;
	}

	return status;
}


status_t
vm_get_page_mapping(team_id team, addr_t vaddr, addr_t* paddr)
{
	VMAddressSpace* addressSpace = VMAddressSpace::Get(team);
	if (addressSpace == NULL)
		return B_BAD_TEAM_ID;

	uint32 dummyFlags;
	status_t status = addressSpace->TranslationMap().ops->query(
		&addressSpace->TranslationMap(), vaddr, paddr, &dummyFlags);

	addressSpace->Put();
	return status;
}


static inline addr_t
virtual_page_address(VMArea* area, vm_page* page)
{
	return area->Base()
		+ ((page->cache_offset << PAGE_SHIFT) - area->cache_offset);
}


bool
vm_test_map_modification(vm_page* page)
{
	MutexLocker locker(sMappingLock);

	vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
	vm_page_mapping* mapping;
	while ((mapping = iterator.Next()) != NULL) {
		VMArea* area = mapping->area;
		vm_translation_map* map = &area->address_space->TranslationMap();

		addr_t physicalAddress;
		uint32 flags;
		map->ops->lock(map);
		map->ops->query(map, virtual_page_address(area, page),
			&physicalAddress, &flags);
		map->ops->unlock(map);

		if ((flags & PAGE_MODIFIED) != 0)
			return true;
	}

	return false;
}


int32
vm_test_map_activation(vm_page* page, bool* _modified)
{
	int32 activation = 0;
	bool modified = false;

	MutexLocker locker(sMappingLock);

	vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
	vm_page_mapping* mapping;
	while ((mapping = iterator.Next()) != NULL) {
		VMArea* area = mapping->area;
		vm_translation_map* map = &area->address_space->TranslationMap();

		addr_t physicalAddress;
		uint32 flags;
		map->ops->lock(map);
		map->ops->query(map, virtual_page_address(area, page),
			&physicalAddress, &flags);
		map->ops->unlock(map);

		if ((flags & PAGE_ACCESSED) != 0)
			activation++;
		if ((flags & PAGE_MODIFIED) != 0)
			modified = true;
	}

	if (_modified != NULL)
		*_modified = modified;

	return activation;
}


void
vm_clear_map_flags(vm_page* page, uint32 flags)
{
	MutexLocker locker(sMappingLock);

	vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
	vm_page_mapping* mapping;
	while ((mapping = iterator.Next()) != NULL) {
		VMArea* area = mapping->area;
		vm_translation_map* map = &area->address_space->TranslationMap();

		map->ops->lock(map);
		map->ops->clear_flags(map, virtual_page_address(area, page), flags);
		map->ops->unlock(map);
	}
}


/*!	Removes all mappings from a page.
	After you've called this function, the page is unmapped from memory.
	The accumulated page flags of all mappings can be found in \a _flags.
*/
void
vm_remove_all_page_mappings(vm_page* page, uint32* _flags)
{
	uint32 accumulatedFlags = 0;
	MutexLocker locker(sMappingLock);

	vm_page_mappings queue;
	queue.MoveFrom(&page->mappings);

	vm_page_mappings::Iterator iterator = queue.GetIterator();
	vm_page_mapping* mapping;
	while ((mapping = iterator.Next()) != NULL) {
		VMArea* area = mapping->area;
		vm_translation_map* map = &area->address_space->TranslationMap();
		addr_t physicalAddress;
		uint32 flags;

		map->ops->lock(map);
		addr_t address = virtual_page_address(area, page);
		map->ops->unmap(map, address, address + (B_PAGE_SIZE - 1));
		map->ops->flush(map);
		map->ops->query(map, address, &physicalAddress, &flags);
		map->ops->unlock(map);

		area->mappings.Remove(mapping);

		accumulatedFlags |= flags;
	}

	if (page->wired_count == 0 && !queue.IsEmpty())
		atomic_add(&gMappedPagesCount, -1);

	locker.Unlock();

	// free now unused mappings

	while ((mapping = queue.RemoveHead()) != NULL) {
		free(mapping);
	}

	if (_flags != NULL)
		*_flags = accumulatedFlags;
}


bool
vm_unmap_page(VMArea* area, addr_t virtualAddress, bool preserveModified)
{
	vm_translation_map* map = &area->address_space->TranslationMap();

	map->ops->lock(map);

	addr_t physicalAddress;
	uint32 flags;
	status_t status = map->ops->query(map, virtualAddress, &physicalAddress,
		&flags);
	if (status < B_OK || (flags & PAGE_PRESENT) == 0) {
		map->ops->unlock(map);
		return false;
	}
	vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
	if (page == NULL && area->cache_type != CACHE_TYPE_DEVICE) {
		panic("area %p looking up page failed for pa 0x%lx\n", area,
			physicalAddress);
	}

	if (area->wiring != B_NO_LOCK && area->cache_type != CACHE_TYPE_DEVICE)
		decrement_page_wired_count(page);

	map->ops->unmap(map, virtualAddress, virtualAddress + B_PAGE_SIZE - 1);

	if (preserveModified) {
		map->ops->flush(map);

		status = map->ops->query(map, virtualAddress, &physicalAddress, &flags);
		if ((flags & PAGE_MODIFIED) != 0 && page->state != PAGE_STATE_MODIFIED)
			vm_page_set_state(page, PAGE_STATE_MODIFIED);
	}

	map->ops->unlock(map);

	if (area->wiring == B_NO_LOCK) {
		vm_page_mapping* mapping;

		mutex_lock(&sMappingLock);
		map->ops->lock(map);

		vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
		while (iterator.HasNext()) {
			mapping = iterator.Next();

			if (mapping->area == area) {
				area->mappings.Remove(mapping);
				page->mappings.Remove(mapping);

				if (page->mappings.IsEmpty() && page->wired_count == 0)
					atomic_add(&gMappedPagesCount, -1);

				map->ops->unlock(map);
				mutex_unlock(&sMappingLock);

				free(mapping);

				return true;
			}
		}

		map->ops->unlock(map);
		mutex_unlock(&sMappingLock);

		dprintf("vm_unmap_page: couldn't find mapping for area %p in page %p\n",
			area, page);
	}

	return true;
}


status_t
vm_unmap_pages(VMArea* area, addr_t base, size_t size, bool preserveModified)
{
	vm_translation_map* map = &area->address_space->TranslationMap();
	addr_t end = base + (size - 1);

	map->ops->lock(map);

	if (area->wiring != B_NO_LOCK && area->cache_type != CACHE_TYPE_DEVICE) {
		// iterate through all pages and decrease their wired count
		for (addr_t virtualAddress = base; virtualAddress < end;
				virtualAddress += B_PAGE_SIZE) {
			addr_t physicalAddress;
			uint32 flags;
			status_t status = map->ops->query(map, virtualAddress,
				&physicalAddress, &flags);
			if (status < B_OK || (flags & PAGE_PRESENT) == 0)
				continue;

			vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
			if (page == NULL) {
				panic("area %p looking up page failed for pa 0x%lx\n", area,
					physicalAddress);
			}

			decrement_page_wired_count(page);
		}
	}

	map->ops->unmap(map, base, end);
	if (preserveModified) {
		map->ops->flush(map);

		for (addr_t virtualAddress = base; virtualAddress < end;
				virtualAddress += B_PAGE_SIZE) {
			addr_t physicalAddress;
			uint32 flags;
			status_t status = map->ops->query(map, virtualAddress,
				&physicalAddress, &flags);
			if (status < B_OK || (flags & PAGE_PRESENT) == 0)
				continue;

			vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
			if (page == NULL) {
				panic("area %p looking up page failed for pa 0x%lx\n", area,
					physicalAddress);
			}

			if ((flags & PAGE_MODIFIED) != 0
				&& page->state != PAGE_STATE_MODIFIED)
				vm_page_set_state(page, PAGE_STATE_MODIFIED);
		}
	}
	map->ops->unlock(map);

	if (area->wiring == B_NO_LOCK) {
		uint32 startOffset = (area->cache_offset + base - area->Base())
			>> PAGE_SHIFT;
		uint32 endOffset = startOffset + (size >> PAGE_SHIFT);
		vm_page_mapping* mapping;
		VMAreaMappings queue;

		mutex_lock(&sMappingLock);
		map->ops->lock(map);

		VMAreaMappings::Iterator iterator = area->mappings.GetIterator();
		while (iterator.HasNext()) {
			mapping = iterator.Next();

			vm_page* page = mapping->page;
			if (page->cache_offset < startOffset
				|| page->cache_offset >= endOffset)
				continue;

			page->mappings.Remove(mapping);
			iterator.Remove();

			if (page->mappings.IsEmpty() && page->wired_count == 0)
				atomic_add(&gMappedPagesCount, -1);

			queue.Add(mapping);
		}

		map->ops->unlock(map);
		mutex_unlock(&sMappingLock);

		while ((mapping = queue.RemoveHead()) != NULL) {
			free(mapping);
		}
	}

	return B_OK;
}


/*!	When calling this function, you need to have pages reserved! */
status_t
vm_map_page(VMArea* area, vm_page* page, addr_t address, uint32 protection)
{
	vm_translation_map* map = &area->address_space->TranslationMap();
	vm_page_mapping* mapping = NULL;

	if (area->wiring == B_NO_LOCK) {
		mapping = (vm_page_mapping*)malloc_nogrow(sizeof(vm_page_mapping));
		if (mapping == NULL)
			return B_NO_MEMORY;

		mapping->page = page;
		mapping->area = area;
	}

	map->ops->lock(map);
	map->ops->map(map, address, page->physical_page_number * B_PAGE_SIZE,
		protection);
	map->ops->unlock(map);

	if (area->wiring != B_NO_LOCK) {
		increment_page_wired_count(page);
	} else {
		// insert mapping into lists
		MutexLocker locker(sMappingLock);

		if (page->mappings.IsEmpty() && page->wired_count == 0)
			atomic_add(&gMappedPagesCount, 1);

		page->mappings.Add(mapping);
		area->mappings.Add(mapping);
	}

	if (page->usage_count < 0)
		page->usage_count = 1;

	if (page->state != PAGE_STATE_MODIFIED)
		vm_page_set_state(page, PAGE_STATE_ACTIVE);

	return B_OK;
}


static int
display_mem(int argc, char** argv)
{
	bool physical = false;
	addr_t copyAddress;
	int32 displayWidth;
	int32 itemSize;
	int32 num = -1;
	addr_t address;
	int i = 1, j;

	if (argc > 1 && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-p") || !strcmp(argv[1], "--physical")) {
			physical = true;
			i++;
		} else
			i = 99;
	}

	if (argc < i + 1 || argc > i + 2) {
		kprintf("usage: dl/dw/ds/db/string [-p|--physical] <address> [num]\n"
			"\tdl - 8 bytes\n"
			"\tdw - 4 bytes\n"
			"\tds - 2 bytes\n"
			"\tdb - 1 byte\n"
			"\tstring - a whole string\n"
			"  -p or --physical only allows memory from a single page to be "
			"displayed.\n");
		return 0;
	}

	address = parse_expression(argv[i]);

	if (argc > i + 1)
		num = parse_expression(argv[i + 1]);

	// build the format string
	if (strcmp(argv[0], "db") == 0) {
		itemSize = 1;
		displayWidth = 16;
	} else if (strcmp(argv[0], "ds") == 0) {
		itemSize = 2;
		displayWidth = 8;
	} else if (strcmp(argv[0], "dw") == 0) {
		itemSize = 4;
		displayWidth = 4;
	} else if (strcmp(argv[0], "dl") == 0) {
		itemSize = 8;
		displayWidth = 2;
	} else if (strcmp(argv[0], "string") == 0) {
		itemSize = 1;
		displayWidth = -1;
	} else {
		kprintf("display_mem called in an invalid way!\n");
		return 0;
	}

	if (num <= 0)
		num = displayWidth;

	void* physicalPageHandle = NULL;

	if (physical) {
		int32 offset = address & (B_PAGE_SIZE - 1);
		if (num * itemSize + offset > B_PAGE_SIZE) {
			num = (B_PAGE_SIZE - offset) / itemSize;
			kprintf("NOTE: number of bytes has been cut to page size\n");
		}

		address = ROUNDDOWN(address, B_PAGE_SIZE);

		if (vm_get_physical_page_debug(address, &copyAddress,
				&physicalPageHandle) != B_OK) {
			kprintf("getting the hardware page failed.");
			return 0;
		}

		address += offset;
		copyAddress += offset;
	} else
		copyAddress = address;

	if (!strcmp(argv[0], "string")) {
		kprintf("%p \"", (char*)copyAddress);

		// string mode
		for (i = 0; true; i++) {
			char c;
			if (debug_memcpy(&c, (char*)copyAddress + i, 1) != B_OK
				|| c == '\0')
				break;

			if (c == '\n')
				kprintf("\\n");
			else if (c == '\t')
				kprintf("\\t");
			else {
				if (!isprint(c))
					c = '.';

				kprintf("%c", c);
			}
		}

		kprintf("\"\n");
	} else {
		// number mode
		for (i = 0; i < num; i++) {
			uint32 value;

			if ((i % displayWidth) == 0) {
				int32 displayed = min_c(displayWidth, (num-i)) * itemSize;
				if (i != 0)
					kprintf("\n");

				kprintf("[0x%lx]  ", address + i * itemSize);

				for (j = 0; j < displayed; j++) {
					char c;
					if (debug_memcpy(&c, (char*)copyAddress + i * itemSize + j,
							1) != B_OK) {
						displayed = j;
						break;
					}
					if (!isprint(c))
						c = '.';

					kprintf("%c", c);
				}
				if (num > displayWidth) {
					// make sure the spacing in the last line is correct
					for (j = displayed; j < displayWidth * itemSize; j++)
						kprintf(" ");
				}
				kprintf("  ");
			}

			if (debug_memcpy(&value, (uint8*)copyAddress + i * itemSize,
					itemSize) != B_OK) {
				kprintf("read fault");
				break;
			}

			switch (itemSize) {
				case 1:
					kprintf(" %02x", *(uint8*)&value);
					break;
				case 2:
					kprintf(" %04x", *(uint16*)&value);
					break;
				case 4:
					kprintf(" %08lx", *(uint32*)&value);
					break;
				case 8:
					kprintf(" %016Lx", *(uint64*)&value);
					break;
			}
		}

		kprintf("\n");
	}

	if (physical) {
		copyAddress = ROUNDDOWN(copyAddress, B_PAGE_SIZE);
		vm_put_physical_page_debug(copyAddress, physicalPageHandle);
	}
	return 0;
}


static void
dump_cache_tree_recursively(VMCache* cache, int level,
	VMCache* highlightCache)
{
	// print this cache
	for (int i = 0; i < level; i++)
		kprintf("  ");
	if (cache == highlightCache)
		kprintf("%p <--\n", cache);
	else
		kprintf("%p\n", cache);

	// recursively print its consumers
	VMCache* consumer = NULL;
	while ((consumer = (VMCache*)list_get_next_item(&cache->consumers,
			consumer)) != NULL) {
		dump_cache_tree_recursively(consumer, level + 1, highlightCache);
	}
}


static int
dump_cache_tree(int argc, char** argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <address>\n", argv[0]);
		return 0;
	}

	addr_t address = parse_expression(argv[1]);
	if (address == 0)
		return 0;

	VMCache* cache = (VMCache*)address;
	VMCache* root = cache;

	// find the root cache (the transitive source)
	while (root->source != NULL)
		root = root->source;

	dump_cache_tree_recursively(root, 0, cache);

	return 0;
}


static const char*
cache_type_to_string(int32 type)
{
	switch (type) {
		case CACHE_TYPE_RAM:
			return "RAM";
		case CACHE_TYPE_DEVICE:
			return "device";
		case CACHE_TYPE_VNODE:
			return "vnode";
		case CACHE_TYPE_NULL:
			return "null";

		default:
			return "unknown";
	}
}


#if DEBUG_CACHE_LIST

static void
update_cache_info_recursively(VMCache* cache, cache_info& info)
{
	info.page_count += cache->page_count;
	if (cache->type == CACHE_TYPE_RAM)
		info.committed += cache->committed_size;

	// recurse
	VMCache* consumer = NULL;
	while ((consumer = (VMCache*)list_get_next_item(&cache->consumers,
			consumer)) != NULL) {
		update_cache_info_recursively(consumer, info);
	}
}


static int
cache_info_compare_page_count(const void* _a, const void* _b)
{
	const cache_info* a = (const cache_info*)_a;
	const cache_info* b = (const cache_info*)_b;
	if (a->page_count == b->page_count)
		return 0;
	return a->page_count < b->page_count ? 1 : -1;
}


static int
cache_info_compare_committed(const void* _a, const void* _b)
{
	const cache_info* a = (const cache_info*)_a;
	const cache_info* b = (const cache_info*)_b;
	if (a->committed == b->committed)
		return 0;
	return a->committed < b->committed ? 1 : -1;
}


static void
dump_caches_recursively(VMCache* cache, cache_info& info, int level)
{
	for (int i = 0; i < level; i++)
		kprintf("  ");

	kprintf("%p: type: %s, base: %lld, size: %lld, pages: %lu", cache,
		cache_type_to_string(cache->type), cache->virtual_base,
		cache->virtual_end, cache->page_count);

	if (level == 0)
		kprintf("/%lu", info.page_count);

	if (cache->type == CACHE_TYPE_RAM || (level == 0 && info.committed > 0)) {
		kprintf(", committed: %lld", cache->committed_size);

		if (level == 0)
			kprintf("/%lu", info.committed);
	}

	// areas
	if (cache->areas != NULL) {
		VMArea* area = cache->areas;
		kprintf(", areas: %ld (%s, team: %ld)", area->id, area->name,
			area->address_space->ID());

		while (area->cache_next != NULL) {
			area = area->cache_next;
			kprintf(", %ld", area->id);
		}
	}

	kputs("\n");

	// recurse
	VMCache* consumer = NULL;
	while ((consumer = (VMCache*)list_get_next_item(&cache->consumers,
			consumer)) != NULL) {
		dump_caches_recursively(consumer, info, level + 1);
	}
}


static int
dump_caches(int argc, char** argv)
{
	if (sCacheInfoTable == NULL) {
		kprintf("No cache info table!\n");
		return 0;
	}

	bool sortByPageCount = true;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0) {
			sortByPageCount = false;
		} else {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	uint32 totalCount = 0;
	uint32 rootCount = 0;
	off_t totalCommitted = 0;
	page_num_t totalPages = 0;

	VMCache* cache = gDebugCacheList;
	while (cache) {
		totalCount++;
		if (cache->source == NULL) {
			cache_info stackInfo;
			cache_info& info = rootCount < (uint32)kCacheInfoTableCount
				? sCacheInfoTable[rootCount] : stackInfo;
			rootCount++;
			info.cache = cache;
			info.page_count = 0;
			info.committed = 0;
			update_cache_info_recursively(cache, info);
			totalCommitted += info.committed;
			totalPages += info.page_count;
		}

		cache = cache->debug_next;
	}

	if (rootCount <= (uint32)kCacheInfoTableCount) {
		qsort(sCacheInfoTable, rootCount, sizeof(cache_info),
			sortByPageCount
				? &cache_info_compare_page_count
				: &cache_info_compare_committed);
	}

	kprintf("total committed memory: %lld, total used pages: %lu\n",
		totalCommitted, totalPages);
	kprintf("%lu caches (%lu root caches), sorted by %s per cache "
		"tree...\n\n", totalCount, rootCount,
		sortByPageCount ? "page count" : "committed size");

	if (rootCount <= (uint32)kCacheInfoTableCount) {
		for (uint32 i = 0; i < rootCount; i++) {
			cache_info& info = sCacheInfoTable[i];
			dump_caches_recursively(info.cache, info, 0);
		}
	} else
		kprintf("Cache info table too small! Can't sort and print caches!\n");

	return 0;
}

#endif	// DEBUG_CACHE_LIST


static int
dump_cache(int argc, char** argv)
{
	VMCache* cache;
	bool showPages = false;
	int i = 1;

	if (argc < 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [-ps] <address>\n"
			"  if -p is specified, all pages are shown, if -s is used\n"
			"  only the cache info is shown respectively.\n", argv[0]);
		return 0;
	}
	while (argv[i][0] == '-') {
		char* arg = argv[i] + 1;
		while (arg[0]) {
			if (arg[0] == 'p')
				showPages = true;
			arg++;
		}
		i++;
	}
	if (argv[i] == NULL) {
		kprintf("%s: invalid argument, pass address\n", argv[0]);
		return 0;
	}

	addr_t address = parse_expression(argv[i]);
	if (address == 0)
		return 0;

	cache = (VMCache*)address;

	kprintf("CACHE %p:\n", cache);
	kprintf("  ref_count:    %ld\n", cache->RefCount());
	kprintf("  source:       %p\n", cache->source);
	kprintf("  type:         %s\n", cache_type_to_string(cache->type));
	kprintf("  virtual_base: 0x%Lx\n", cache->virtual_base);
	kprintf("  virtual_end:  0x%Lx\n", cache->virtual_end);
	kprintf("  temporary:    %ld\n", cache->temporary);
	kprintf("  scan_skip:    %ld\n", cache->scan_skip);
	kprintf("  lock:         %p\n", cache->GetLock());
#if KDEBUG
	kprintf("  lock.holder:  %ld\n", cache->GetLock()->holder);
#endif
	kprintf("  areas:\n");

	for (VMArea* area = cache->areas; area != NULL; area = area->cache_next) {
		kprintf("    area 0x%lx, %s\n", area->id, area->name);
		kprintf("\tbase_addr:  0x%lx, size: 0x%lx\n", area->Base(),
			area->Size());
		kprintf("\tprotection: 0x%lx\n", area->protection);
		kprintf("\towner:      0x%lx\n", area->address_space->ID());
	}

	kprintf("  consumers:\n");
	VMCache* consumer = NULL;
	while ((consumer = (VMCache*)list_get_next_item(&cache->consumers,
				consumer)) != NULL) {
		kprintf("\t%p\n", consumer);
	}

	kprintf("  pages:\n");
	if (showPages) {
		for (VMCachePagesTree::Iterator it = cache->pages.GetIterator();
				vm_page* page = it.Next();) {
			if (page->type == PAGE_TYPE_PHYSICAL) {
				kprintf("\t%p ppn 0x%lx offset 0x%lx type %u state %u (%s) "
					"wired_count %u\n", page, page->physical_page_number,
					page->cache_offset, page->type, page->state,
					page_state_to_string(page->state), page->wired_count);
			} else if(page->type == PAGE_TYPE_DUMMY) {
				kprintf("\t%p DUMMY PAGE state %u (%s)\n",
					page, page->state, page_state_to_string(page->state));
			} else
				kprintf("\t%p UNKNOWN PAGE type %u\n", page, page->type);
		}
	} else
		kprintf("\t%ld in cache\n", cache->page_count);

	return 0;
}


static void
dump_area_struct(VMArea* area, bool mappings)
{
	kprintf("AREA: %p\n", area);
	kprintf("name:\t\t'%s'\n", area->name);
	kprintf("owner:\t\t0x%lx\n", area->address_space->ID());
	kprintf("id:\t\t0x%lx\n", area->id);
	kprintf("base:\t\t0x%lx\n", area->Base());
	kprintf("size:\t\t0x%lx\n", area->Size());
	kprintf("protection:\t0x%lx\n", area->protection);
	kprintf("wiring:\t\t0x%x\n", area->wiring);
	kprintf("memory_type:\t0x%x\n", area->memory_type);
	kprintf("cache:\t\t%p\n", area->cache);
	kprintf("cache_type:\t%s\n", cache_type_to_string(area->cache_type));
	kprintf("cache_offset:\t0x%Lx\n", area->cache_offset);
	kprintf("cache_next:\t%p\n", area->cache_next);
	kprintf("cache_prev:\t%p\n", area->cache_prev);

	VMAreaMappings::Iterator iterator = area->mappings.GetIterator();
	if (mappings) {
		kprintf("page mappings:\n");
		while (iterator.HasNext()) {
			vm_page_mapping* mapping = iterator.Next();
			kprintf("  %p", mapping->page);
		}
		kprintf("\n");
	} else {
		uint32 count = 0;
		while (iterator.Next() != NULL) {
			count++;
		}
		kprintf("page mappings:\t%lu\n", count);
	}
}


static int
dump_area(int argc, char** argv)
{
	bool mappings = false;
	bool found = false;
	int32 index = 1;
	VMArea* area;
	addr_t num;

	if (argc < 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: area [-m] [id|contains|address|name] <id|address|name>\n"
			"All areas matching either id/address/name are listed. You can\n"
			"force to check only a specific item by prefixing the specifier\n"
			"with the id/contains/address/name keywords.\n"
			"-m shows the area's mappings as well.\n");
		return 0;
	}

	if (!strcmp(argv[1], "-m")) {
		mappings = true;
		index++;
	}

	int32 mode = 0xf;
	if (!strcmp(argv[index], "id"))
		mode = 1;
	else if (!strcmp(argv[index], "contains"))
		mode = 2;
	else if (!strcmp(argv[index], "name"))
		mode = 4;
	else if (!strcmp(argv[index], "address"))
		mode = 0;
	if (mode != 0xf)
		index++;

	if (index >= argc) {
		kprintf("No area specifier given.\n");
		return 0;
	}

	num = parse_expression(argv[index]);

	if (mode == 0) {
		dump_area_struct((struct VMArea*)num, mappings);
	} else {
		// walk through the area list, looking for the arguments as a name

		VMAreaHashTable::Iterator it = VMAreaHash::GetIterator();
		while ((area = it.Next()) != NULL) {
			if (((mode & 4) != 0 && area->name != NULL
					&& !strcmp(argv[index], area->name))
				|| (num != 0 && (((mode & 1) != 0 && (addr_t)area->id == num)
					|| (((mode & 2) != 0 && area->Base() <= num
						&& area->Base() + area->Size() > num))))) {
				dump_area_struct(area, mappings);
				found = true;
			}
		}

		if (!found)
			kprintf("could not find area %s (%ld)\n", argv[index], num);
	}

	return 0;
}


static int
dump_area_list(int argc, char** argv)
{
	VMArea* area;
	const char* name = NULL;
	int32 id = 0;

	if (argc > 1) {
		id = parse_expression(argv[1]);
		if (id == 0)
			name = argv[1];
	}

	kprintf("addr          id  base\t\tsize    protect lock  name\n");

	VMAreaHashTable::Iterator it = VMAreaHash::GetIterator();
	while ((area = it.Next()) != NULL) {
		if ((id != 0 && area->address_space->ID() != id)
			|| (name != NULL && strstr(area->name, name) == NULL))
			continue;

		kprintf("%p %5lx  %p\t%p %4lx\t%4d  %s\n", area, area->id,
			(void*)area->Base(), (void*)area->Size(), area->protection,
			area->wiring, area->name);
	}
	return 0;
}


static int
dump_available_memory(int argc, char** argv)
{
	kprintf("Available memory: %Ld/%lu bytes\n",
		sAvailableMemory, vm_page_num_pages() * B_PAGE_SIZE);
	return 0;
}


status_t
vm_delete_areas(struct VMAddressSpace* addressSpace)
{
	TRACE(("vm_delete_areas: called on address space 0x%lx\n",
		addressSpace->ID()));

	addressSpace->WriteLock();

	// remove all reserved areas in this address space
	addressSpace->UnreserveAllAddressRanges();

	// delete all the areas in this address space
	while (VMArea* area = addressSpace->FirstArea())
		delete_area(addressSpace, area);

	addressSpace->WriteUnlock();
	return B_OK;
}


static area_id
vm_area_for(addr_t address, bool kernel)
{
	team_id team;
	if (IS_USER_ADDRESS(address)) {
		// we try the user team address space, if any
		team = VMAddressSpace::CurrentID();
		if (team < 0)
			return team;
	} else
		team = VMAddressSpace::KernelID();

	AddressSpaceReadLocker locker(team);
	if (!locker.IsLocked())
		return B_BAD_TEAM_ID;

	VMArea* area = locker.AddressSpace()->LookupArea(address);
	if (area != NULL) {
		if (!kernel && (area->protection & (B_READ_AREA | B_WRITE_AREA)) == 0)
			return B_ERROR;

		return area->id;
	}

	return B_ERROR;
}


/*!	Frees physical pages that were used during the boot process.
	\a end is inclusive.
*/
static void
unmap_and_free_physical_pages(vm_translation_map* map, addr_t start, addr_t end)
{
	// free all physical pages in the specified range

	for (addr_t current = start; current < end; current += B_PAGE_SIZE) {
		addr_t physicalAddress;
		uint32 flags;

		if (map->ops->query(map, current, &physicalAddress, &flags) == B_OK) {
			vm_page* page = vm_lookup_page(current / B_PAGE_SIZE);
			if (page != NULL)
				vm_page_set_state(page, PAGE_STATE_FREE);
		}
	}

	// unmap the memory
	map->ops->unmap(map, start, end);
}


void
vm_free_unused_boot_loader_range(addr_t start, addr_t size)
{
	vm_translation_map* map = &VMAddressSpace::Kernel()->TranslationMap();
	addr_t end = start + (size - 1);
	addr_t lastEnd = start;

	TRACE(("vm_free_unused_boot_loader_range(): asked to free %p - %p\n",
		(void*)start, (void*)end));

	// The areas are sorted in virtual address space order, so
	// we just have to find the holes between them that fall
	// into the area we should dispose

	map->ops->lock(map);

	for (VMAddressSpace::AreaIterator it
				= VMAddressSpace::Kernel()->GetAreaIterator();
			VMArea* area = it.Next();) {
		addr_t areaStart = area->Base();
		addr_t areaEnd = areaStart + (area->Size() - 1);

		if (areaEnd < start)
			continue;

		if (areaStart > end) {
			// we are done, the area is already beyond of what we have to free
			end = areaStart - 1;
			break;
		}

		if (areaStart > lastEnd) {
			// this is something we can free
			TRACE(("free boot range: get rid of %p - %p\n", (void*)lastEnd,
				(void*)areaStart));
			unmap_and_free_physical_pages(map, lastEnd, areaStart - 1);
		}

		if (areaEnd >= end) {
			lastEnd = areaEnd;
				// no +1 to prevent potential overflow
			break;
		}

		lastEnd = areaEnd + 1;
	}

	if (lastEnd < end) {
		// we can also get rid of some space at the end of the area
		TRACE(("free boot range: also remove %p - %p\n", (void*)lastEnd,
			(void*)end));
		unmap_and_free_physical_pages(map, lastEnd, end);
	}

	map->ops->unlock(map);
}


static void
create_preloaded_image_areas(struct preloaded_image* image)
{
	char name[B_OS_NAME_LENGTH];
	void* address;
	int32 length;

	// use file name to create a good area name
	char* fileName = strrchr(image->name, '/');
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
	address = (void*)ROUNDDOWN(image->text_region.start, B_PAGE_SIZE);
	image->text_region.id = create_area(name, &address, B_EXACT_ADDRESS,
		PAGE_ALIGN(image->text_region.size), B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		// this will later be remapped read-only/executable by the
		// ELF initialization code

	strcpy(name + length, "_data");
	address = (void*)ROUNDDOWN(image->data_region.start, B_PAGE_SIZE);
	image->data_region.id = create_area(name, &address, B_EXACT_ADDRESS,
		PAGE_ALIGN(image->data_region.size), B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
}


/*!	Frees all previously kernel arguments areas from the kernel_args structure.
	Any boot loader resources contained in that arguments must not be accessed
	anymore past this point.
*/
void
vm_free_kernel_args(kernel_args* args)
{
	uint32 i;

	TRACE(("vm_free_kernel_args()\n"));

	for (i = 0; i < args->num_kernel_args_ranges; i++) {
		area_id area = area_for((void*)args->kernel_args_range[i].start);
		if (area >= B_OK)
			delete_area(area);
	}
}


static void
allocate_kernel_args(kernel_args* args)
{
	TRACE(("allocate_kernel_args()\n"));

	for (uint32 i = 0; i < args->num_kernel_args_ranges; i++) {
		void* address = (void*)args->kernel_args_range[i].start;

		create_area("_kernel args_", &address, B_EXACT_ADDRESS,
			args->kernel_args_range[i].size, B_ALREADY_WIRED,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	}
}


static void
unreserve_boot_loader_ranges(kernel_args* args)
{
	TRACE(("unreserve_boot_loader_ranges()\n"));

	for (uint32 i = 0; i < args->num_virtual_allocated_ranges; i++) {
		vm_unreserve_address_range(VMAddressSpace::KernelID(),
			(void*)args->virtual_allocated_range[i].start,
			args->virtual_allocated_range[i].size);
	}
}


static void
reserve_boot_loader_ranges(kernel_args* args)
{
	TRACE(("reserve_boot_loader_ranges()\n"));

	for (uint32 i = 0; i < args->num_virtual_allocated_ranges; i++) {
		void* address = (void*)args->virtual_allocated_range[i].start;

		// If the address is no kernel address, we just skip it. The
		// architecture specific code has to deal with it.
		if (!IS_KERNEL_ADDRESS(address)) {
			dprintf("reserve_boot_loader_ranges(): Skipping range: %p, %lu\n",
				address, args->virtual_allocated_range[i].size);
			continue;
		}

		status_t status = vm_reserve_address_range(VMAddressSpace::KernelID(),
			&address, B_EXACT_ADDRESS, args->virtual_allocated_range[i].size, 0);
		if (status < B_OK)
			panic("could not reserve boot loader ranges\n");
	}
}


static addr_t
allocate_early_virtual(kernel_args* args, size_t size)
{
	addr_t spot = 0;
	uint32 i;
	int last_valloc_entry = 0;

	size = PAGE_ALIGN(size);
	// find a slot in the virtual allocation addr range
	for (i = 1; i < args->num_virtual_allocated_ranges; i++) {
		addr_t previousRangeEnd = args->virtual_allocated_range[i - 1].start
			+ args->virtual_allocated_range[i - 1].size;
		last_valloc_entry = i;
		// check to see if the space between this one and the last is big enough
		if (previousRangeEnd >= KERNEL_BASE
			&& args->virtual_allocated_range[i].start
				- previousRangeEnd >= size) {
			spot = previousRangeEnd;
			args->virtual_allocated_range[i - 1].size += size;
			goto out;
		}
	}
	if (spot == 0) {
		// we hadn't found one between allocation ranges. this is ok.
		// see if there's a gap after the last one
		addr_t lastRangeEnd
			= args->virtual_allocated_range[last_valloc_entry].start
				+ args->virtual_allocated_range[last_valloc_entry].size;
		if (KERNEL_BASE + (KERNEL_SIZE - 1) - lastRangeEnd >= size) {
			spot = lastRangeEnd;
			args->virtual_allocated_range[last_valloc_entry].size += size;
			goto out;
		}
		// see if there's a gap before the first one
		if (args->virtual_allocated_range[0].start > KERNEL_BASE) {
			if (args->virtual_allocated_range[0].start - KERNEL_BASE >= size) {
				args->virtual_allocated_range[0].start -= size;
				spot = args->virtual_allocated_range[0].start;
				goto out;
			}
		}
	}

out:
	return spot;
}


static bool
is_page_in_physical_memory_range(kernel_args* args, addr_t address)
{
	// TODO: horrible brute-force method of determining if the page can be
	// allocated
	for (uint32 i = 0; i < args->num_physical_memory_ranges; i++) {
		if (address >= args->physical_memory_range[i].start
			&& address < args->physical_memory_range[i].start
				+ args->physical_memory_range[i].size)
			return true;
	}
	return false;
}


static addr_t
allocate_early_physical_page(kernel_args* args)
{
	for (uint32 i = 0; i < args->num_physical_allocated_ranges; i++) {
		addr_t nextPage;

		nextPage = args->physical_allocated_range[i].start
			+ args->physical_allocated_range[i].size;
		// see if the page after the next allocated paddr run can be allocated
		if (i + 1 < args->num_physical_allocated_ranges
			&& args->physical_allocated_range[i + 1].size != 0) {
			// see if the next page will collide with the next allocated range
			if (nextPage >= args->physical_allocated_range[i+1].start)
				continue;
		}
		// see if the next physical page fits in the memory block
		if (is_page_in_physical_memory_range(args, nextPage)) {
			// we got one!
			args->physical_allocated_range[i].size += B_PAGE_SIZE;
			return nextPage / B_PAGE_SIZE;
		}
	}

	return 0;
		// could not allocate a block
}


/*!	This one uses the kernel_args' physical and virtual memory ranges to
	allocate some pages before the VM is completely up.
*/
addr_t
vm_allocate_early(kernel_args* args, size_t virtualSize, size_t physicalSize,
	uint32 attributes)
{
	if (physicalSize > virtualSize)
		physicalSize = virtualSize;

	// find the vaddr to allocate at
	addr_t virtualBase = allocate_early_virtual(args, virtualSize);
	//dprintf("vm_allocate_early: vaddr 0x%lx\n", virtualAddress);

	// map the pages
	for (uint32 i = 0; i < PAGE_ALIGN(physicalSize) / B_PAGE_SIZE; i++) {
		addr_t physicalAddress = allocate_early_physical_page(args);
		if (physicalAddress == 0)
			panic("error allocating early page!\n");

		//dprintf("vm_allocate_early: paddr 0x%lx\n", physicalAddress);

		arch_vm_translation_map_early_map(args, virtualBase + i * B_PAGE_SIZE,
			physicalAddress * B_PAGE_SIZE, attributes,
			&allocate_early_physical_page);
	}

	return virtualBase;
}


/*!	The main entrance point to initialize the VM. */
status_t
vm_init(kernel_args* args)
{
	struct preloaded_image* image;
	void* address;
	status_t err = 0;
	uint32 i;

	TRACE(("vm_init: entry\n"));
	err = arch_vm_translation_map_init(args);
	err = arch_vm_init(args);

	// initialize some globals
	vm_page_init_num_pages(args);
	sAvailableMemory = vm_page_num_pages() * B_PAGE_SIZE;

	size_t heapSize = INITIAL_HEAP_SIZE;
	// try to accomodate low memory systems
	while (heapSize > sAvailableMemory / 8)
		heapSize /= 2;
	if (heapSize < 1024 * 1024)
		panic("vm_init: go buy some RAM please.");

	// map in the new heap and initialize it
	addr_t heapBase = vm_allocate_early(args, heapSize, heapSize,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	TRACE(("heap at 0x%lx\n", heapBase));
	heap_init(heapBase, heapSize);

	size_t slabInitialSize = args->num_cpus * 2 * B_PAGE_SIZE;
	addr_t slabInitialBase = vm_allocate_early(args, slabInitialSize,
		slabInitialSize, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	slab_init(args, slabInitialBase, slabInitialSize);

	// initialize the free page list and physical page mapper
	vm_page_init(args);

	// initialize the hash table that stores the pages mapped to caches
	vm_cache_init(args);

	{
		status_t error = VMAreaHash::Init();
		if (error != B_OK)
			panic("vm_init: error initializing area hash table\n");
	}

	VMAddressSpace::Init();
	reserve_boot_loader_ranges(args);

	// Do any further initialization that the architecture dependant layers may
	// need now
	arch_vm_translation_map_init_post_area(args);
	arch_vm_init_post_area(args);
	vm_page_init_post_area(args);

	// allocate areas to represent stuff that already exists

	address = (void*)ROUNDDOWN(heapBase, B_PAGE_SIZE);
	create_area("kernel heap", &address, B_EXACT_ADDRESS, heapSize,
		B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	address = (void*)ROUNDDOWN(slabInitialBase, B_PAGE_SIZE);
	create_area("initial slab space", &address, B_EXACT_ADDRESS,
		slabInitialSize, B_ALREADY_WIRED, B_KERNEL_READ_AREA
		| B_KERNEL_WRITE_AREA);

	allocate_kernel_args(args);

	create_preloaded_image_areas(&args->kernel_image);

	// allocate areas for preloaded images
	for (image = args->preloaded_images; image != NULL; image = image->next) {
		create_preloaded_image_areas(image);
	}

	// allocate kernel stacks
	for (i = 0; i < args->num_cpus; i++) {
		char name[64];

		sprintf(name, "idle thread %lu kstack", i + 1);
		address = (void*)args->cpu_kstack[i].start;
		create_area(name, &address, B_EXACT_ADDRESS, args->cpu_kstack[i].size,
			B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	}

	void* lastPage = (void*)ROUNDDOWN(~(addr_t)0, B_PAGE_SIZE);
	vm_block_address_range("overflow protection", lastPage, B_PAGE_SIZE);

#if DEBUG_CACHE_LIST
	create_area("cache info table", (void**)&sCacheInfoTable,
		B_ANY_KERNEL_ADDRESS,
		ROUNDUP(kCacheInfoTableCount * sizeof(cache_info), B_PAGE_SIZE),
		B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
#endif	// DEBUG_CACHE_LIST

	// add some debugger commands
	add_debugger_command("areas", &dump_area_list, "Dump a list of all areas");
	add_debugger_command("area", &dump_area,
		"Dump info about a particular area");
	add_debugger_command("cache", &dump_cache, "Dump VMCache");
	add_debugger_command("cache_tree", &dump_cache_tree, "Dump VMCache tree");
#if DEBUG_CACHE_LIST
	add_debugger_command_etc("caches", &dump_caches,
		"List all VMCache trees",
		"[ \"-c\" ]\n"
		"All cache trees are listed sorted in decreasing order by number of\n"
		"used pages or, if \"-c\" is specified, by size of committed memory.\n",
		0);
#endif
	add_debugger_command("avail", &dump_available_memory,
		"Dump available memory");
	add_debugger_command("dl", &display_mem, "dump memory long words (64-bit)");
	add_debugger_command("dw", &display_mem, "dump memory words (32-bit)");
	add_debugger_command("ds", &display_mem, "dump memory shorts (16-bit)");
	add_debugger_command("db", &display_mem, "dump memory bytes (8-bit)");
	add_debugger_command("string", &display_mem, "dump strings");

	TRACE(("vm_init: exit\n"));

	return err;
}


status_t
vm_init_post_sem(kernel_args* args)
{
	// This frees all unused boot loader resources and makes its space available
	// again
	arch_vm_init_end(args);
	unreserve_boot_loader_ranges(args);

	// fill in all of the semaphores that were not allocated before
	// since we're still single threaded and only the kernel address space
	// exists, it isn't that hard to find all of the ones we need to create

	arch_vm_translation_map_init_post_sem(args);
	VMAddressSpace::InitPostSem();

	slab_init_post_sem();
	return heap_init_post_sem();
}


status_t
vm_init_post_thread(kernel_args* args)
{
	vm_page_init_post_thread(args);
	vm_daemon_init();
	slab_init_post_thread();
	return heap_init_post_thread();
}


status_t
vm_init_post_modules(kernel_args* args)
{
	return arch_vm_init_post_modules(args);
}


void
permit_page_faults(void)
{
	struct thread* thread = thread_get_current_thread();
	if (thread != NULL)
		atomic_add(&thread->page_faults_allowed, 1);
}


void
forbid_page_faults(void)
{
	struct thread* thread = thread_get_current_thread();
	if (thread != NULL)
		atomic_add(&thread->page_faults_allowed, -1);
}


status_t
vm_page_fault(addr_t address, addr_t faultAddress, bool isWrite, bool isUser,
	addr_t* newIP)
{
	FTRACE(("vm_page_fault: page fault at 0x%lx, ip 0x%lx\n", address,
		faultAddress));

	TPF(PageFaultStart(address, isWrite, isUser, faultAddress));

	addr_t pageAddress = ROUNDDOWN(address, B_PAGE_SIZE);
	VMAddressSpace* addressSpace = NULL;

	status_t status = B_OK;
	*newIP = 0;
	atomic_add((int32*)&sPageFaults, 1);

	if (IS_KERNEL_ADDRESS(pageAddress)) {
		addressSpace = VMAddressSpace::GetKernel();
	} else if (IS_USER_ADDRESS(pageAddress)) {
		addressSpace = VMAddressSpace::GetCurrent();
		if (addressSpace == NULL) {
			if (!isUser) {
				dprintf("vm_page_fault: kernel thread accessing invalid user "
					"memory!\n");
				status = B_BAD_ADDRESS;
				TPF(PageFaultError(-1,
					VMPageFaultTracing
						::PAGE_FAULT_ERROR_KERNEL_BAD_USER_MEMORY));
			} else {
				// XXX weird state.
				panic("vm_page_fault: non kernel thread accessing user memory "
					"that doesn't exist!\n");
				status = B_BAD_ADDRESS;
			}
		}
	} else {
		// the hit was probably in the 64k DMZ between kernel and user space
		// this keeps a user space thread from passing a buffer that crosses
		// into kernel space
		status = B_BAD_ADDRESS;
		TPF(PageFaultError(-1,
			VMPageFaultTracing::PAGE_FAULT_ERROR_NO_ADDRESS_SPACE));
	}

	if (status == B_OK)
		status = vm_soft_fault(addressSpace, pageAddress, isWrite, isUser);

	if (status < B_OK) {
		dprintf("vm_page_fault: vm_soft_fault returned error '%s' on fault at "
			"0x%lx, ip 0x%lx, write %d, user %d, thread 0x%lx\n",
			strerror(status), address, faultAddress, isWrite, isUser,
			thread_get_current_thread_id());
		if (!isUser) {
			struct thread* thread = thread_get_current_thread();
			if (thread != NULL && thread->fault_handler != 0) {
				// this will cause the arch dependant page fault handler to
				// modify the IP on the interrupt frame or whatever to return
				// to this address
				*newIP = thread->fault_handler;
			} else {
				// unhandled page fault in the kernel
				panic("vm_page_fault: unhandled page fault in kernel space at "
					"0x%lx, ip 0x%lx\n", address, faultAddress);
			}
		} else {
#if 1
			addressSpace->ReadLock();

			// TODO: remove me once we have proper userland debugging support
			// (and tools)
			VMArea* area = addressSpace->LookupArea(faultAddress);

			struct thread* thread = thread_get_current_thread();
			dprintf("vm_page_fault: thread \"%s\" (%ld) in team \"%s\" (%ld) "
				"tried to %s address %#lx, ip %#lx (\"%s\" +%#lx)\n",
				thread->name, thread->id, thread->team->name, thread->team->id,
				isWrite ? "write" : "read", address, faultAddress,
				area ? area->name : "???",
				faultAddress - (area ? area->Base() : 0x0));

			// We can print a stack trace of the userland thread here.
// TODO: The user_memcpy() below can cause a deadlock, if it causes a page
// fault and someone is already waiting for a write lock on the same address
// space. This thread will then try to acquire the lock again and will
// be queued after the writer.
#	if 0
			if (area) {
				struct stack_frame {
					#if defined(__INTEL__) || defined(__POWERPC__) || defined(__M68K__)
						struct stack_frame*	previous;
						void*				return_address;
					#else
						// ...
					#warning writeme
					#endif
				} frame;
#		ifdef __INTEL__
				struct iframe* iframe = i386_get_user_iframe();
				if (iframe == NULL)
					panic("iframe is NULL!");

				status_t status = user_memcpy(&frame, (void*)iframe->ebp,
					sizeof(struct stack_frame));
#		elif defined(__POWERPC__)
				struct iframe* iframe = ppc_get_user_iframe();
				if (iframe == NULL)
					panic("iframe is NULL!");

				status_t status = user_memcpy(&frame, (void*)iframe->r1,
					sizeof(struct stack_frame));
#		else
#			warning "vm_page_fault() stack trace won't work"
				status = B_ERROR;
#		endif

				dprintf("stack trace:\n");
				int32 maxFrames = 50;
				while (status == B_OK && --maxFrames >= 0
						&& frame.return_address != NULL) {
					dprintf("  %p", frame.return_address);
					area = addressSpace->LookupArea(
						(addr_t)frame.return_address);
					if (area) {
						dprintf(" (%s + %#lx)", area->name,
							(addr_t)frame.return_address - area->Base());
					}
					dprintf("\n");

					status = user_memcpy(&frame, frame.previous,
						sizeof(struct stack_frame));
				}
			}
#	endif	// 0 (stack trace)

			addressSpace->ReadUnlock();
#endif

			// TODO: the fault_callback is a temporary solution for vm86
			if (thread->fault_callback == NULL
				|| thread->fault_callback(address, faultAddress, isWrite)) {
				// If the thread has a signal handler for SIGSEGV, we simply
				// send it the signal. Otherwise we notify the user debugger
				// first.
				struct sigaction action;
				if (sigaction(SIGSEGV, NULL, &action) == 0
					&& action.sa_handler != SIG_DFL
					&& action.sa_handler != SIG_IGN) {
					send_signal(thread->id, SIGSEGV);
				} else if (user_debug_exception_occurred(B_SEGMENT_VIOLATION,
						SIGSEGV)) {
					send_signal(thread->id, SIGSEGV);
				}
			}
		}
	}

	if (addressSpace != NULL)
		addressSpace->Put();

	return B_HANDLED_INTERRUPT;
}


class VMCacheChainLocker {
public:
	VMCacheChainLocker()
		:
		fTopCache(NULL),
		fBottomCache(NULL)
	{
	}

	void SetTo(VMCache* topCache)
	{
		fTopCache = topCache;
		fBottomCache = topCache;
	}

	VMCache* LockSourceCache()
	{
		if (fBottomCache == NULL || fBottomCache->source == NULL)
			return NULL;

		fBottomCache = fBottomCache->source;
		fBottomCache->Lock();
		fBottomCache->AcquireRefLocked();

		return fBottomCache;
	}

	void Unlock()
	{
		if (fTopCache == NULL)
			return;

		VMCache* cache = fTopCache;
		while (cache != NULL) {
			VMCache* nextCache = cache->source;
			cache->ReleaseRefAndUnlock();

			if (cache == fBottomCache)
				break;

			cache = nextCache;
		}

		fTopCache = NULL;
		fBottomCache = NULL;
	}

private:
	VMCache*	fTopCache;
	VMCache*	fBottomCache;
};


struct PageFaultContext {
	AddressSpaceReadLocker	addressSpaceLocker;
	VMCacheChainLocker		cacheChainLocker;

	vm_translation_map*		map;
	VMCache*				topCache;
	off_t					cacheOffset;
	bool					isWrite;

	// return values
	vm_page*				page;
	bool					restart;


	PageFaultContext(VMAddressSpace* addressSpace, bool isWrite)
		:
		addressSpaceLocker(addressSpace, true),
		map(&addressSpace->TranslationMap()),
		isWrite(isWrite)
	{
	}

	~PageFaultContext()
	{
		UnlockAll();
	}

	void Prepare(VMCache* topCache, off_t cacheOffset)
	{
		this->topCache = topCache;
		this->cacheOffset = cacheOffset;
		page = NULL;
		restart = false;

		cacheChainLocker.SetTo(topCache);
	}

	void UnlockAll()
	{
		topCache = NULL;
		addressSpaceLocker.Unlock();
		cacheChainLocker.Unlock();
	}
};


/*!	Gets the page that should be mapped into the area.
	Returns an error code other than \c B_OK, if the page couldn't be found or
	paged in. The locking state of the address space and the caches is undefined
	in that case.
	Returns \c B_OK with \c context.restart set to \c true, if the functions
	had to unlock the address space and all caches and is supposed to be called
	again.
	Returns \c B_OK with \c context.restart set to \c false, if the page was
	found. It is returned in \c context.page. The address space will still be
	locked as well as all caches starting from the top cache to at least the
	cache the page lives in.
*/
static inline status_t
fault_get_page(PageFaultContext& context)
{
	VMCache* cache = context.topCache;
	VMCache* lastCache = NULL;
	vm_page* page = NULL;

	while (cache != NULL) {
		// We already hold the lock of the cache at this point.

		lastCache = cache;

		for (;;) {
			page = cache->LookupPage(context.cacheOffset);
			if (page == NULL || page->state != PAGE_STATE_BUSY) {
				// Either there is no page or there is one and it is not busy.
				break;
			}

			// page must be busy -- wait for it to become unbusy
			ConditionVariableEntry entry;
			entry.Add(page);
			context.UnlockAll();
			entry.Wait();

			// restart the whole process
			context.restart = true;
			return B_OK;
		}

		if (page != NULL)
			break;

		// The current cache does not contain the page we're looking for.

		// see if the backing store has it
		if (cache->HasPage(context.cacheOffset)) {
			// insert a fresh page and mark it busy -- we're going to read it in
			page = vm_page_allocate_page(PAGE_STATE_FREE, true);
			cache->InsertPage(page, context.cacheOffset);

			ConditionVariable busyCondition;
			busyCondition.Publish(page, "page");

			// We need to unlock all caches and the address space while reading
			// the page in. Keep a reference to the cache around.
			cache->AcquireRefLocked();
			context.UnlockAll();

			// read the page in
			iovec vec;
			vec.iov_base = (void*)(page->physical_page_number * B_PAGE_SIZE);
			size_t bytesRead = vec.iov_len = B_PAGE_SIZE;

			status_t status = cache->Read(context.cacheOffset, &vec, 1,
				B_PHYSICAL_IO_REQUEST, &bytesRead);

			cache->Lock();

			if (status < B_OK) {
				// on error remove and free the page
				dprintf("reading page from cache %p returned: %s!\n",
					cache, strerror(status));

				busyCondition.Unpublish();
				cache->RemovePage(page);
				vm_page_set_state(page, PAGE_STATE_FREE);

				cache->ReleaseRefAndUnlock();
				return status;
			}

			// mark the page unbusy again
			page->state = PAGE_STATE_ACTIVE;
			busyCondition.Unpublish();

			// Since we needed to unlock everything temporarily, the area
			// situation might have changed. So we need to restart the whole
			// process.
			cache->ReleaseRefAndUnlock();
			context.restart = true;
			return B_OK;
		}

		cache = context.cacheChainLocker.LockSourceCache();
	}

	if (page == NULL) {
		// There was no adequate page, determine the cache for a clean one.
		// Read-only pages come in the deepest cache, only the top most cache
		// may have direct write access.
		cache = context.isWrite ? context.topCache : lastCache;

		// allocate a clean page
		page = vm_page_allocate_page(PAGE_STATE_CLEAR, true);
		FTRACE(("vm_soft_fault: just allocated page 0x%lx\n",
			page->physical_page_number));

		// insert the new page into our cache
		cache->InsertPage(page, context.cacheOffset);

	} else if (page->cache != context.topCache && context.isWrite) {
		// We have a page that has the data we want, but in the wrong cache
		// object so we need to copy it and stick it into the top cache.
		vm_page* sourcePage = page;

		// TODO: If memory is low, it might be a good idea to steal the page
		// from our source cache -- if possible, that is.
		FTRACE(("get new page, copy it, and put it into the topmost cache\n"));
		page = vm_page_allocate_page(PAGE_STATE_FREE, true);

		// copy the page
		vm_memcpy_physical_page(page->physical_page_number * B_PAGE_SIZE,
			sourcePage->physical_page_number * B_PAGE_SIZE);

		// insert the new page into our cache
		context.topCache->InsertPage(page, context.cacheOffset);
	}

	context.page = page;
	return B_OK;
}


static status_t
vm_soft_fault(VMAddressSpace* addressSpace, addr_t originalAddress,
	bool isWrite, bool isUser)
{
	FTRACE(("vm_soft_fault: thid 0x%lx address 0x%lx, isWrite %d, isUser %d\n",
		thread_get_current_thread_id(), originalAddress, isWrite, isUser));

	PageFaultContext context(addressSpace, isWrite);

	addr_t address = ROUNDDOWN(originalAddress, B_PAGE_SIZE);
	status_t status = B_OK;

	addressSpace->IncrementFaultCount();

	// We may need up to 2 pages plus pages needed for mapping them -- reserving
	// the pages upfront makes sure we don't have any cache locked, so that the
	// page daemon/thief can do their job without problems.
	size_t reservePages = 2 + context.map->ops->map_max_pages_need(context.map,
		originalAddress, originalAddress);
	context.addressSpaceLocker.Unlock();
	vm_page_reserve_pages(reservePages);

	while (true) {
		context.addressSpaceLocker.Lock();

		// get the area the fault was in
		VMArea* area = addressSpace->LookupArea(address);
		if (area == NULL) {
			dprintf("vm_soft_fault: va 0x%lx not covered by area in address "
				"space\n", originalAddress);
			TPF(PageFaultError(-1,
				VMPageFaultTracing::PAGE_FAULT_ERROR_NO_AREA));
			status = B_BAD_ADDRESS;
			break;
		}

		// check permissions
		uint32 protection = get_area_page_protection(area, address);
		if (isUser && (protection & B_USER_PROTECTION) == 0) {
			dprintf("user access on kernel area 0x%lx at %p\n", area->id,
				(void*)originalAddress);
			TPF(PageFaultError(area->id,
				VMPageFaultTracing::PAGE_FAULT_ERROR_KERNEL_ONLY));
			status = B_PERMISSION_DENIED;
			break;
		}
		if (isWrite && (protection
				& (B_WRITE_AREA | (isUser ? 0 : B_KERNEL_WRITE_AREA))) == 0) {
			dprintf("write access attempted on write-protected area 0x%lx at"
				" %p\n", area->id, (void*)originalAddress);
			TPF(PageFaultError(area->id,
				VMPageFaultTracing::PAGE_FAULT_ERROR_WRITE_PROTECTED));
			status = B_PERMISSION_DENIED;
			break;
		} else if (!isWrite && (protection
				& (B_READ_AREA | (isUser ? 0 : B_KERNEL_READ_AREA))) == 0) {
			dprintf("read access attempted on read-protected area 0x%lx at"
				" %p\n", area->id, (void*)originalAddress);
			TPF(PageFaultError(area->id,
				VMPageFaultTracing::PAGE_FAULT_ERROR_READ_PROTECTED));
			status = B_PERMISSION_DENIED;
			break;
		}

		// We have the area, it was a valid access, so let's try to resolve the
		// page fault now.
		// At first, the top most cache from the area is investigated.

		context.Prepare(vm_area_get_locked_cache(area),
			address - area->Base() + area->cache_offset);

		// See if this cache has a fault handler -- this will do all the work
		// for us.
		{
			// Note, since the page fault is resolved with interrupts enabled,
			// the fault handler could be called more than once for the same
			// reason -- the store must take this into account.
			status = context.topCache->Fault(addressSpace, context.cacheOffset);
			if (status != B_BAD_HANDLER)
				break;
		}

		// The top most cache has no fault handler, so let's see if the cache or
		// its sources already have the page we're searching for (we're going
		// from top to bottom).
		status = fault_get_page(context);
		if (status != B_OK) {
			TPF(PageFaultError(area->id, status));
			break;
		}

		if (context.restart)
			continue;

		// All went fine, all there is left to do is to map the page into the
		// address space.
		TPF(PageFaultDone(area->id, context.topCache, context.page->cache,
			context.page));

		// If the page doesn't reside in the area's cache, we need to make sure
		// it's mapped in read-only, so that we cannot overwrite someone else's
		// data (copy-on-write)
		uint32 newProtection = protection;
		if (context.page->cache != context.topCache && !isWrite)
			newProtection &= ~(B_WRITE_AREA | B_KERNEL_WRITE_AREA);

		bool unmapPage = false;
		bool mapPage = true;

		// check whether there's already a page mapped at the address
		context.map->ops->lock(context.map);

		addr_t physicalAddress;
		uint32 flags;
		vm_page* mappedPage;
		if (context.map->ops->query(context.map, address, &physicalAddress,
				&flags) == B_OK
			&& (flags & PAGE_PRESENT) != 0
			&& (mappedPage = vm_lookup_page(physicalAddress / B_PAGE_SIZE))
				!= NULL) {
			// Yep there's already a page. If it's ours, we can simply adjust
			// its protection. Otherwise we have to unmap it.
			if (mappedPage == context.page) {
				context.map->ops->protect(context.map, address,
					address + (B_PAGE_SIZE - 1), newProtection);

				mapPage = false;
			} else
				unmapPage = true;
		}

		context.map->ops->unlock(context.map);

		if (unmapPage)
			vm_unmap_page(area, address, true);

		if (mapPage)
			vm_map_page(area, context.page, address, newProtection);

		break;
	}

	vm_page_unreserve_pages(reservePages);

	return status;
}


status_t
vm_get_physical_page(addr_t paddr, addr_t* _vaddr, void** _handle)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops->get_physical_page(
		paddr, _vaddr, _handle);
}

status_t
vm_put_physical_page(addr_t vaddr, void* handle)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops->put_physical_page(
		vaddr, handle);
}


status_t
vm_get_physical_page_current_cpu(addr_t paddr, addr_t* _vaddr, void** _handle)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops
		->get_physical_page_current_cpu(paddr, _vaddr, _handle);
}

status_t
vm_put_physical_page_current_cpu(addr_t vaddr, void* handle)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops
		->put_physical_page_current_cpu(vaddr, handle);
}


status_t
vm_get_physical_page_debug(addr_t paddr, addr_t* _vaddr, void** _handle)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops
		->get_physical_page_debug(paddr, _vaddr, _handle);
}

status_t
vm_put_physical_page_debug(addr_t vaddr, void* handle)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops
		->put_physical_page_debug(vaddr, handle);
}


void
vm_get_info(system_memory_info* info)
{
	swap_get_info(info);

	info->max_memory = vm_page_num_pages() * B_PAGE_SIZE;
	info->page_faults = sPageFaults;

	MutexLocker locker(sAvailableMemoryLock);
	info->free_memory = sAvailableMemory;
	info->needed_memory = sNeededMemory;
}


uint32
vm_num_page_faults(void)
{
	return sPageFaults;
}


off_t
vm_available_memory(void)
{
	MutexLocker locker(sAvailableMemoryLock);
	return sAvailableMemory;
}


off_t
vm_available_not_needed_memory(void)
{
	MutexLocker locker(sAvailableMemoryLock);
	return sAvailableMemory - sNeededMemory;
}


size_t
vm_kernel_address_space_left(void)
{
	return VMAddressSpace::Kernel()->FreeSpace();
}


void
vm_unreserve_memory(size_t amount)
{
	mutex_lock(&sAvailableMemoryLock);

	sAvailableMemory += amount;

	mutex_unlock(&sAvailableMemoryLock);
}


status_t
vm_try_reserve_memory(size_t amount, bigtime_t timeout)
{
	MutexLocker locker(sAvailableMemoryLock);

	//dprintf("try to reserve %lu bytes, %Lu left\n", amount, sAvailableMemory);

	if (sAvailableMemory >= amount) {
		sAvailableMemory -= amount;
		return B_OK;
	}

	if (timeout <= 0)
		return B_NO_MEMORY;

	// turn timeout into an absolute timeout
	timeout += system_time();

	// loop until we've got the memory or the timeout occurs
	do {
		sNeededMemory += amount;

		// call the low resource manager
		locker.Unlock();
		low_resource(B_KERNEL_RESOURCE_MEMORY, sNeededMemory - sAvailableMemory,
			B_ABSOLUTE_TIMEOUT, timeout);
		locker.Lock();

		sNeededMemory -= amount;

		if (sAvailableMemory >= amount) {
			sAvailableMemory -= amount;
			return B_OK;
		}
	} while (timeout > system_time());

	return B_NO_MEMORY;
}


status_t
vm_set_area_memory_type(area_id id, addr_t physicalBase, uint32 type)
{
	AddressSpaceReadLocker locker;
	VMArea* area;
	status_t status = locker.SetFromArea(id, area);
	if (status != B_OK)
		return status;

	return arch_vm_set_memory_type(area, physicalBase, type);
}


/*!	This function enforces some protection properties:
	 - if B_WRITE_AREA is set, B_WRITE_KERNEL_AREA is set as well
	 - if only B_READ_AREA has been set, B_KERNEL_READ_AREA is also set
	 - if no protection is specified, it defaults to B_KERNEL_READ_AREA
	   and B_KERNEL_WRITE_AREA.
*/
static void
fix_protection(uint32* protection)
{
	if ((*protection & B_KERNEL_PROTECTION) == 0) {
		if ((*protection & B_USER_PROTECTION) == 0
			|| (*protection & B_WRITE_AREA) != 0)
			*protection |= B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA;
		else
			*protection |= B_KERNEL_READ_AREA;
	}
}


static void
fill_area_info(struct VMArea* area, area_info* info, size_t size)
{
	strlcpy(info->name, area->name, B_OS_NAME_LENGTH);
	info->area = area->id;
	info->address = (void*)area->Base();
	info->size = area->Size();
	info->protection = area->protection;
	info->lock = B_FULL_LOCK;
	info->team = area->address_space->ID();
	info->copy_count = 0;
	info->in_count = 0;
	info->out_count = 0;
		// TODO: retrieve real values here!

	VMCache* cache = vm_area_get_locked_cache(area);

	// Note, this is a simplification; the cache could be larger than this area
	info->ram_size = cache->page_count * B_PAGE_SIZE;

	vm_area_put_locked_cache(cache);
}


/*!
	Tests whether or not the area that contains the specified address
	needs any kind of locking, and actually exists.
	Used by both lock_memory() and unlock_memory().
*/
static status_t
test_lock_memory(VMAddressSpace* addressSpace, addr_t address,
	bool& needsLocking)
{
	addressSpace->ReadLock();

	VMArea* area = addressSpace->LookupArea(address);
	if (area != NULL) {
		// This determines if we need to lock the memory at all
		needsLocking = area->cache_type != CACHE_TYPE_NULL
			&& area->cache_type != CACHE_TYPE_DEVICE
			&& area->wiring != B_FULL_LOCK
			&& area->wiring != B_CONTIGUOUS;
	}

	addressSpace->ReadUnlock();

	if (area == NULL)
		return B_BAD_ADDRESS;

	return B_OK;
}


static status_t
vm_resize_area(area_id areaID, size_t newSize, bool kernel)
{
	// is newSize a multiple of B_PAGE_SIZE?
	if (newSize & (B_PAGE_SIZE - 1))
		return B_BAD_VALUE;

	// lock all affected address spaces and the cache
	VMArea* area;
	VMCache* cache;

	MultiAddressSpaceLocker locker;
	status_t status = locker.AddAreaCacheAndLock(areaID, true, true, area,
		&cache);
	if (status != B_OK)
		return status;
	AreaCacheLocker cacheLocker(cache);	// already locked

	// enforce restrictions
	if (!kernel) {
		if ((area->protection & B_KERNEL_AREA) != 0)
			return B_NOT_ALLOWED;
		// TODO: Enforce all restrictions (team, etc.)!
	}

	size_t oldSize = area->Size();
	if (newSize == oldSize)
		return B_OK;

	// Resize all areas of this area's cache

	if (cache->type != CACHE_TYPE_RAM)
		return B_NOT_ALLOWED;

	if (oldSize < newSize) {
		// We need to check if all areas of this cache can be resized
		for (VMArea* current = cache->areas; current != NULL;
				current = current->cache_next) {
			if (!current->address_space->CanResizeArea(current, newSize))
				return B_ERROR;
		}
	}

	// Okay, looks good so far, so let's do it

	if (oldSize < newSize) {
		// Growing the cache can fail, so we do it first.
		status = cache->Resize(cache->virtual_base + newSize);
		if (status != B_OK)
			return status;
	}

	for (VMArea* current = cache->areas; current != NULL;
			current = current->cache_next) {
		status = current->address_space->ResizeArea(current, newSize);
		if (status != B_OK)
			break;

		// We also need to unmap all pages beyond the new size, if the area has
		// shrunk
		if (newSize < oldSize) {
			vm_unmap_pages(current, current->Base() + newSize, oldSize - newSize,
				false);
		}
	}

	// shrinking the cache can't fail, so we do it now
	if (status == B_OK && newSize < oldSize)
		status = cache->Resize(cache->virtual_base + newSize);

	if (status != B_OK) {
		// Something failed -- resize the areas back to their original size.
		// This can fail, too, in which case we're seriously screwed.
		for (VMArea* current = cache->areas; current != NULL;
				current = current->cache_next) {
			if (current->address_space->ResizeArea(current, oldSize)
					!= B_OK) {
				panic("vm_resize_area(): Failed and not being able to restore "
					"original state.");
			}
		}

		cache->Resize(cache->virtual_base + oldSize);
	}

	// TODO: we must honour the lock restrictions of this area
	return status;
}


status_t
vm_memset_physical(addr_t address, int value, size_t length)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops->memset_physical(
		address, value, length);
}


status_t
vm_memcpy_from_physical(void* to, addr_t from, size_t length, bool user)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops->memcpy_from_physical(
		to, from, length, user);
}


status_t
vm_memcpy_to_physical(addr_t to, const void* _from, size_t length, bool user)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops->memcpy_to_physical(
		to, _from, length, user);
}


void
vm_memcpy_physical_page(addr_t to, addr_t from)
{
	return VMAddressSpace::Kernel()->TranslationMap().ops->memcpy_physical_page(
		to, from);
}


//	#pragma mark - kernel public API


status_t
user_memcpy(void* to, const void* from, size_t size)
{
	// don't allow address overflows
	if ((addr_t)from + size < (addr_t)from || (addr_t)to + size < (addr_t)to)
		return B_BAD_ADDRESS;

	if (arch_cpu_user_memcpy(to, from, size,
			&thread_get_current_thread()->fault_handler) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


/*!	\brief Copies at most (\a size - 1) characters from the string in \a from to
	the string in \a to, NULL-terminating the result.

	\param to Pointer to the destination C-string.
	\param from Pointer to the source C-string.
	\param size Size in bytes of the string buffer pointed to by \a to.

	\return strlen(\a from).
*/
ssize_t
user_strlcpy(char* to, const char* from, size_t size)
{
	if (to == NULL && size != 0)
		return B_BAD_VALUE;
	if (from == NULL)
		return B_BAD_ADDRESS;

	// limit size to avoid address overflows
	size_t maxSize = std::min(size,
		~(addr_t)0 - std::max((addr_t)from, (addr_t)to) + 1);
		// NOTE: Since arch_cpu_user_strlcpy() determines the length of \a from,
		// the source address might still overflow.

	ssize_t result = arch_cpu_user_strlcpy(to, from, maxSize,
		&thread_get_current_thread()->fault_handler);

	// If we hit the address overflow boundary, fail.
	if (result >= 0 && (size_t)result >= maxSize && maxSize < size)
		return B_BAD_ADDRESS;

	return result;
}


status_t
user_memset(void* s, char c, size_t count)
{
	// don't allow address overflows
	if ((addr_t)s + count < (addr_t)s)
		return B_BAD_ADDRESS;

	if (arch_cpu_user_memset(s, c, count,
			&thread_get_current_thread()->fault_handler) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


status_t
lock_memory_etc(team_id team, void* address, size_t numBytes, uint32 flags)
{
	VMAddressSpace* addressSpace = NULL;
	struct vm_translation_map* map;
	addr_t unalignedBase = (addr_t)address;
	addr_t end = unalignedBase + numBytes;
	addr_t base = ROUNDDOWN(unalignedBase, B_PAGE_SIZE);
	bool isUser = IS_USER_ADDRESS(address);
	bool needsLocking = true;

	if (isUser) {
		if (team == B_CURRENT_TEAM)
			addressSpace = VMAddressSpace::GetCurrent();
		else
			addressSpace = VMAddressSpace::Get(team);
	} else
		addressSpace = VMAddressSpace::GetKernel();
	if (addressSpace == NULL)
		return B_ERROR;

	// test if we're on an area that allows faults at all

	map = &addressSpace->TranslationMap();

	status_t status = test_lock_memory(addressSpace, base, needsLocking);
	if (status < B_OK)
		goto out;
	if (!needsLocking)
		goto out;

	for (; base < end; base += B_PAGE_SIZE) {
		addr_t physicalAddress;
		uint32 protection;
		status_t status;

		map->ops->lock(map);
		status = map->ops->query(map, base, &physicalAddress, &protection);
		map->ops->unlock(map);

		if (status < B_OK)
			goto out;

		if ((protection & PAGE_PRESENT) != 0) {
			// if B_READ_DEVICE is set, the caller intents to write to the locked
			// memory, so if it hasn't been mapped writable, we'll try the soft
			// fault anyway
			if ((flags & B_READ_DEVICE) == 0
				|| (protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0) {
				// update wiring
				vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
				if (page == NULL)
					panic("couldn't lookup physical page just allocated\n");

				increment_page_wired_count(page);
				continue;
			}
		}

		status = vm_soft_fault(addressSpace, base, (flags & B_READ_DEVICE) != 0,
			isUser);
		if (status != B_OK)	{
			dprintf("lock_memory(address = %p, numBytes = %lu, flags = %lu) "
				"failed: %s\n", (void*)unalignedBase, numBytes, flags,
				strerror(status));
			goto out;
		}

		// TODO: Here's a race condition. We should probably add a parameter
		// to vm_soft_fault() that would cause the page's wired count to be
		// incremented immediately.
		// TODO: After memory has been locked in an area, we need to prevent the
		// area from being deleted, resized, cut, etc. That could be done using
		// a "locked pages" count in VMArea, and maybe a condition variable, if
		// we want to allow waiting for the area to become eligible for these
		// operations again.

		map->ops->lock(map);
		status = map->ops->query(map, base, &physicalAddress, &protection);
		map->ops->unlock(map);

		if (status < B_OK)
			goto out;

		// update wiring
		vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
		if (page == NULL)
			panic("couldn't lookup physical page");

		increment_page_wired_count(page);
			// TODO: needs to be atomic on all platforms!
	}

out:
	addressSpace->Put();
	return status;
}


status_t
lock_memory(void* address, size_t numBytes, uint32 flags)
{
	return lock_memory_etc(B_CURRENT_TEAM, address, numBytes, flags);
}


status_t
unlock_memory_etc(team_id team, void* address, size_t numBytes, uint32 flags)
{
	VMAddressSpace* addressSpace = NULL;
	struct vm_translation_map* map;
	addr_t unalignedBase = (addr_t)address;
	addr_t end = unalignedBase + numBytes;
	addr_t base = ROUNDDOWN(unalignedBase, B_PAGE_SIZE);
	bool needsLocking = true;

	if (IS_USER_ADDRESS(address)) {
		if (team == B_CURRENT_TEAM)
			addressSpace = VMAddressSpace::GetCurrent();
		else
			addressSpace = VMAddressSpace::Get(team);
	} else
		addressSpace = VMAddressSpace::GetKernel();
	if (addressSpace == NULL)
		return B_ERROR;

	map = &addressSpace->TranslationMap();

	status_t status = test_lock_memory(addressSpace, base, needsLocking);
	if (status < B_OK)
		goto out;
	if (!needsLocking)
		goto out;

	for (; base < end; base += B_PAGE_SIZE) {
		map->ops->lock(map);

		addr_t physicalAddress;
		uint32 protection;
		status = map->ops->query(map, base, &physicalAddress,
			&protection);

		map->ops->unlock(map);

		if (status < B_OK)
			goto out;
		if ((protection & PAGE_PRESENT) == 0)
			panic("calling unlock_memory() on unmapped memory!");

		// update wiring
		vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
		if (page == NULL)
			panic("couldn't lookup physical page");

		decrement_page_wired_count(page);
	}

out:
	addressSpace->Put();
	return status;
}


status_t
unlock_memory(void* address, size_t numBytes, uint32 flags)
{
	return unlock_memory_etc(B_CURRENT_TEAM, address, numBytes, flags);
}


/*!	Similar to get_memory_map(), but also allows to specify the address space
	for the memory in question and has a saner semantics.
	Returns \c B_OK when the complete range could be translated or
	\c B_BUFFER_OVERFLOW, if the provided array wasn't big enough. In either
	case the actual number of entries is written to \c *_numEntries. Any other
	error case indicates complete failure; \c *_numEntries will be set to \c 0
	in this case.
*/
status_t
get_memory_map_etc(team_id team, const void* address, size_t numBytes,
	physical_entry* table, uint32* _numEntries)
{
	uint32 numEntries = *_numEntries;
	*_numEntries = 0;

	VMAddressSpace* addressSpace;
	addr_t virtualAddress = (addr_t)address;
	addr_t pageOffset = virtualAddress & (B_PAGE_SIZE - 1);
	addr_t physicalAddress;
	status_t status = B_OK;
	int32 index = -1;
	addr_t offset = 0;
	bool interrupts = are_interrupts_enabled();

	TRACE(("get_memory_map_etc(%ld, %p, %lu bytes, %ld entries)\n", team,
		address, numBytes, numEntries));

	if (numEntries == 0 || numBytes == 0)
		return B_BAD_VALUE;

	// in which address space is the address to be found?
	if (IS_USER_ADDRESS(virtualAddress)) {
		if (team == B_CURRENT_TEAM)
			addressSpace = VMAddressSpace::GetCurrent();
		else
			addressSpace = VMAddressSpace::Get(team);
	} else
		addressSpace = VMAddressSpace::GetKernel();

	if (addressSpace == NULL)
		return B_ERROR;

	vm_translation_map* map = &addressSpace->TranslationMap();

	if (interrupts)
		map->ops->lock(map);

	while (offset < numBytes) {
		addr_t bytes = min_c(numBytes - offset, B_PAGE_SIZE);
		uint32 flags;

		if (interrupts) {
			status = map->ops->query(map, (addr_t)address + offset,
				&physicalAddress, &flags);
		} else {
			status = map->ops->query_interrupt(map, (addr_t)address + offset,
				&physicalAddress, &flags);
		}
		if (status < B_OK)
			break;
		if ((flags & PAGE_PRESENT) == 0) {
			panic("get_memory_map() called on unmapped memory!");
			return B_BAD_ADDRESS;
		}

		if (index < 0 && pageOffset > 0) {
			physicalAddress += pageOffset;
			if (bytes > B_PAGE_SIZE - pageOffset)
				bytes = B_PAGE_SIZE - pageOffset;
		}

		// need to switch to the next physical_entry?
		if (index < 0 || (addr_t)table[index].address
				!= physicalAddress - table[index].size) {
			if ((uint32)++index + 1 > numEntries) {
				// table to small
				status = B_BUFFER_OVERFLOW;
				break;
			}
			table[index].address = (void*)physicalAddress;
			table[index].size = bytes;
		} else {
			// page does fit in current entry
			table[index].size += bytes;
		}

		offset += bytes;
	}

	if (interrupts)
		map->ops->unlock(map);

	if (status != B_OK)
		return status;

	if ((uint32)index + 1 > numEntries) {
		*_numEntries = index;
		return B_BUFFER_OVERFLOW;
	}

	*_numEntries = index + 1;
	return B_OK;
}


/*!	According to the BeBook, this function should always succeed.
	This is no longer the case.
*/
long
get_memory_map(const void* address, ulong numBytes, physical_entry* table,
	long numEntries)
{
	uint32 entriesRead = numEntries;
	status_t error = get_memory_map_etc(B_CURRENT_TEAM, address, numBytes,
		table, &entriesRead);
	if (error != B_OK)
		return error;

	// close the entry list

	// if it's only one entry, we will silently accept the missing ending
	if (numEntries == 1)
		return B_OK;

	if (entriesRead + 1 > (uint32)numEntries)
		return B_BUFFER_OVERFLOW;

	table[entriesRead].address = NULL;
	table[entriesRead].size = 0;

	return B_OK;
}


area_id
area_for(void* address)
{
	return vm_area_for((addr_t)address, true);
}


area_id
find_area(const char* name)
{
	return VMAreaHash::Find(name);
}


status_t
_get_area_info(area_id id, area_info* info, size_t size)
{
	if (size != sizeof(area_info) || info == NULL)
		return B_BAD_VALUE;

	AddressSpaceReadLocker locker;
	VMArea* area;
	status_t status = locker.SetFromArea(id, area);
	if (status != B_OK)
		return status;

	fill_area_info(area, info, size);
	return B_OK;
}


status_t
_get_next_area_info(team_id team, int32* cookie, area_info* info, size_t size)
{
	addr_t nextBase = *(addr_t*)cookie;

	// we're already through the list
	if (nextBase == (addr_t)-1)
		return B_ENTRY_NOT_FOUND;

	if (team == B_CURRENT_TEAM)
		team = team_get_current_team_id();

	AddressSpaceReadLocker locker(team);
	if (!locker.IsLocked())
		return B_BAD_TEAM_ID;

	VMArea* area;
	for (VMAddressSpace::AreaIterator it
				= locker.AddressSpace()->GetAreaIterator();
			(area = it.Next()) != NULL;) {
		if (area->Base() > nextBase)
			break;
	}

	if (area == NULL) {
		nextBase = (addr_t)-1;
		return B_ENTRY_NOT_FOUND;
	}

	fill_area_info(area, info, size);
	*cookie = (int32)(area->Base());
		// TODO: Not 64 bit safe!

	return B_OK;
}


status_t
set_area_protection(area_id area, uint32 newProtection)
{
	fix_protection(&newProtection);

	return vm_set_area_protection(VMAddressSpace::KernelID(), area,
		newProtection, true);
}


status_t
resize_area(area_id areaID, size_t newSize)
{
	return vm_resize_area(areaID, newSize, true);
}


/*!	Transfers the specified area to a new team. The caller must be the owner
	of the area (not yet enforced but probably should be).
*/
area_id
transfer_area(area_id id, void** _address, uint32 addressSpec, team_id target,
	bool kernel)
{
	area_info info;
	status_t status = get_area_info(id, &info);
	if (status != B_OK)
		return status;

	area_id clonedArea = vm_clone_area(target, info.name, _address,
		addressSpec, info.protection, REGION_NO_PRIVATE_MAP, id, kernel);
	if (clonedArea < 0)
		return clonedArea;

	status = vm_delete_area(info.team, id, kernel);
	if (status != B_OK) {
		vm_delete_area(target, clonedArea, kernel);
		return status;
	}

	// TODO: The clonedArea is B_SHARED_AREA, which is not really desired.

	return clonedArea;
}


area_id
map_physical_memory(const char* name, void* physicalAddress, size_t numBytes,
	uint32 addressSpec, uint32 protection, void** _virtualAddress)
{
	if (!arch_vm_supports_protection(protection))
		return B_NOT_SUPPORTED;

	fix_protection(&protection);

	return vm_map_physical_memory(VMAddressSpace::KernelID(), name,
		_virtualAddress, addressSpec, numBytes, protection,
		(addr_t)physicalAddress);
}


area_id
clone_area(const char* name, void** _address, uint32 addressSpec,
	uint32 protection, area_id source)
{
	if ((protection & B_KERNEL_PROTECTION) == 0)
		protection |= B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA;

	return vm_clone_area(VMAddressSpace::KernelID(), name, _address,
		addressSpec, protection, REGION_NO_PRIVATE_MAP, source, true);
}


area_id
create_area_etc(team_id team, const char* name, void** address,
	uint32 addressSpec, uint32 size, uint32 lock, uint32 protection,
	addr_t physicalAddress, uint32 flags)
{
	fix_protection(&protection);

	return vm_create_anonymous_area(team, (char*)name, address, addressSpec,
		size, lock, protection, physicalAddress, flags, true);
}


area_id
create_area(const char* name, void** _address, uint32 addressSpec, size_t size,
	uint32 lock, uint32 protection)
{
	fix_protection(&protection);

	return vm_create_anonymous_area(VMAddressSpace::KernelID(), (char*)name,
		_address, addressSpec, size, lock, protection, 0, 0, true);
}


status_t
delete_area(area_id area)
{
	return vm_delete_area(VMAddressSpace::KernelID(), area, true);
}


//	#pragma mark - Userland syscalls


status_t
_user_reserve_address_range(addr_t* userAddress, uint32 addressSpec,
	addr_t size)
{
	// filter out some unavailable values (for userland)
	switch (addressSpec) {
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
			return B_BAD_VALUE;
	}

	addr_t address;

	if (!IS_USER_ADDRESS(userAddress)
		|| user_memcpy(&address, userAddress, sizeof(address)) != B_OK)
		return B_BAD_ADDRESS;

	status_t status = vm_reserve_address_range(
		VMAddressSpace::CurrentID(), (void**)&address, addressSpec, size,
		RESERVED_AVOID_BASE);
	if (status != B_OK)
		return status;

	if (user_memcpy(userAddress, &address, sizeof(address)) != B_OK) {
		vm_unreserve_address_range(VMAddressSpace::CurrentID(),
			(void*)address, size);
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


status_t
_user_unreserve_address_range(addr_t address, addr_t size)
{
	return vm_unreserve_address_range(VMAddressSpace::CurrentID(),
		(void*)address, size);
}


area_id
_user_area_for(void* address)
{
	return vm_area_for((addr_t)address, false);
}


area_id
_user_find_area(const char* userName)
{
	char name[B_OS_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return find_area(name);
}


status_t
_user_get_area_info(area_id area, area_info* userInfo)
{
	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	area_info info;
	status_t status = get_area_info(area, &info);
	if (status < B_OK)
		return status;

	// TODO: do we want to prevent userland from seeing kernel protections?
	//info.protection &= B_USER_PROTECTION;

	if (user_memcpy(userInfo, &info, sizeof(area_info)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_get_next_area_info(team_id team, int32* userCookie, area_info* userInfo)
{
	int32 cookie;

	if (!IS_USER_ADDRESS(userCookie)
		|| !IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&cookie, userCookie, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	area_info info;
	status_t status = _get_next_area_info(team, &cookie, &info,
		sizeof(area_info));
	if (status != B_OK)
		return status;

	//info.protection &= B_USER_PROTECTION;

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

	fix_protection(&newProtection);

	return vm_set_area_protection(VMAddressSpace::CurrentID(), area,
		newProtection, false);
}


status_t
_user_resize_area(area_id area, size_t newSize)
{
	// TODO: Since we restrict deleting of areas to those owned by the team,
	// we should also do that for resizing (check other functions, too).
	return vm_resize_area(area, newSize, false);
}


area_id
_user_transfer_area(area_id area, void** userAddress, uint32 addressSpec,
	team_id target)
{
	// filter out some unavailable values (for userland)
	switch (addressSpec) {
		case B_ANY_KERNEL_ADDRESS:
		case B_ANY_KERNEL_BLOCK_ADDRESS:
			return B_BAD_VALUE;
	}

	void* address;
	if (!IS_USER_ADDRESS(userAddress)
		|| user_memcpy(&address, userAddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	area_id newArea = transfer_area(area, &address, addressSpec, target, false);
	if (newArea < B_OK)
		return newArea;

	if (user_memcpy(userAddress, &address, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	return newArea;
}


area_id
_user_clone_area(const char* userName, void** userAddress, uint32 addressSpec,
	uint32 protection, area_id sourceArea)
{
	char name[B_OS_NAME_LENGTH];
	void* address;

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

	fix_protection(&protection);

	area_id clonedArea = vm_clone_area(VMAddressSpace::CurrentID(), name,
		&address, addressSpec, protection, REGION_NO_PRIVATE_MAP, sourceArea,
		false);
	if (clonedArea < B_OK)
		return clonedArea;

	if (user_memcpy(userAddress, &address, sizeof(address)) < B_OK) {
		delete_area(clonedArea);
		return B_BAD_ADDRESS;
	}

	return clonedArea;
}


area_id
_user_create_area(const char* userName, void** userAddress, uint32 addressSpec,
	size_t size, uint32 lock, uint32 protection)
{
	char name[B_OS_NAME_LENGTH];
	void* address;

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

	fix_protection(&protection);

	area_id area = vm_create_anonymous_area(VMAddressSpace::CurrentID(),
		(char*)name, &address, addressSpec, size, lock, protection, 0, 0,
		false);

	if (area >= B_OK
		&& user_memcpy(userAddress, &address, sizeof(address)) < B_OK) {
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
	// The documentation to delete_area() explicitly states that this
	// will be restricted in the future, and so it will.
	return vm_delete_area(VMAddressSpace::CurrentID(), area, false);
}


// TODO: create a BeOS style call for this!

area_id
_user_map_file(const char* userName, void** userAddress, int addressSpec,
	size_t size, int protection, int mapping, bool unmapAddressRange, int fd,
	off_t offset)
{
	char name[B_OS_NAME_LENGTH];
	void* address;
	area_id area;

	if (!IS_USER_ADDRESS(userName) || !IS_USER_ADDRESS(userAddress)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK
		|| user_memcpy(&address, userAddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	if (addressSpec == B_EXACT_ADDRESS) {
		if ((addr_t)address + size < (addr_t)address)
			return B_BAD_VALUE;
		if (!IS_USER_ADDRESS(address)
				|| !IS_USER_ADDRESS((addr_t)address + size)) {
			return B_BAD_ADDRESS;
		}
	}

	// userland created areas can always be accessed by the kernel
	protection |= B_KERNEL_READ_AREA
		| (protection & B_WRITE_AREA ? B_KERNEL_WRITE_AREA : 0);

	area = _vm_map_file(VMAddressSpace::CurrentID(), name, &address,
		addressSpec, size, protection, mapping, unmapAddressRange, fd, offset,
		false);
	if (area < B_OK)
		return area;

	if (user_memcpy(userAddress, &address, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	return area;
}


status_t
_user_unmap_memory(void* _address, size_t size)
{
	addr_t address = (addr_t)_address;

	// check params
	if (size == 0 || (addr_t)address + size < (addr_t)address)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(address) || !IS_USER_ADDRESS((addr_t)address + size))
		return B_BAD_ADDRESS;

	// write lock the address space
	AddressSpaceWriteLocker locker;
	status_t status = locker.SetTo(team_get_current_team_id());
	if (status != B_OK)
		return status;

	// unmap
	return unmap_address_range(locker.AddressSpace(), address, size, false);
}


status_t
_user_set_memory_protection(void* _address, size_t size, int protection)
{
	// check address range
	addr_t address = (addr_t)_address;
	size = PAGE_ALIGN(size);

	if ((address % B_PAGE_SIZE) != 0)
		return B_BAD_VALUE;
	if ((addr_t)address + size < (addr_t)address || !IS_USER_ADDRESS(address)
		|| !IS_USER_ADDRESS((addr_t)address + size)) {
		// weird error code required by POSIX
		return ENOMEM;
	}

	// extend and check protection
	protection &= B_READ_AREA | B_WRITE_AREA | B_EXECUTE_AREA;
	uint32 actualProtection = protection | B_KERNEL_READ_AREA
		| (protection & B_WRITE_AREA ? B_KERNEL_WRITE_AREA : 0);

	if (!arch_vm_supports_protection(actualProtection))
		return B_NOT_SUPPORTED;

	// We need to write lock the address space, since we're going to play with
	// the areas.
	AddressSpaceWriteLocker locker;
	status_t status = locker.SetTo(team_get_current_team_id());
	if (status != B_OK)
		return status;

	// First round: Check whether the whole range is covered by areas and we are
	// allowed to modify them.
	addr_t currentAddress = address;
	size_t sizeLeft = size;
	while (sizeLeft > 0) {
		VMArea* area = locker.AddressSpace()->LookupArea(currentAddress);
		if (area == NULL)
			return B_NO_MEMORY;

		if ((area->protection & B_KERNEL_AREA) != 0)
			return B_NOT_ALLOWED;

		// TODO: For (shared) mapped files we should check whether the new
		// protections are compatible with the file permissions. We don't have
		// a way to do that yet, though.

		addr_t offset = currentAddress - area->Base();
		size_t rangeSize = min_c(area->Size() - offset, sizeLeft);

		currentAddress += rangeSize;
		sizeLeft -= rangeSize;
	}

	// Second round: If the protections differ from that of the area, create a
	// page protection array and re-map mapped pages.
	vm_translation_map* map = &locker.AddressSpace()->TranslationMap();
	currentAddress = address;
	sizeLeft = size;
	while (sizeLeft > 0) {
		VMArea* area = locker.AddressSpace()->LookupArea(currentAddress);
		if (area == NULL)
			return B_NO_MEMORY;

		addr_t offset = currentAddress - area->Base();
		size_t rangeSize = min_c(area->Size() - offset, sizeLeft);

		currentAddress += rangeSize;
		sizeLeft -= rangeSize;

		if (area->page_protections == NULL) {
			if (area->protection == actualProtection)
				continue;

			// In the page protections we store only the three user protections,
			// so we use 4 bits per page.
			uint32 bytes = (area->Size() / B_PAGE_SIZE + 1) / 2;
			area->page_protections = (uint8*)malloc(bytes);
			if (area->page_protections == NULL)
				return B_NO_MEMORY;

			// init the page protections for all pages to that of the area
			uint32 areaProtection = area->protection
				& (B_READ_AREA | B_WRITE_AREA | B_EXECUTE_AREA);
			memset(area->page_protections,
				areaProtection | (areaProtection << 4), bytes);
		}

		for (addr_t pageAddress = area->Base() + offset;
				pageAddress < currentAddress; pageAddress += B_PAGE_SIZE) {
			map->ops->lock(map);

			set_area_page_protection(area, pageAddress, protection);

			addr_t physicalAddress;
			uint32 flags;

			status_t error = map->ops->query(map, pageAddress, &physicalAddress,
				&flags);
			if (error != B_OK || (flags & PAGE_PRESENT) == 0) {
				map->ops->unlock(map);
				continue;
			}

			vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
			if (page == NULL) {
				panic("area %p looking up page failed for pa 0x%lx\n", area,
					physicalAddress);
				map->ops->unlock(map);
				return B_ERROR;;
			}

			// If the page is not in the topmost cache and write access is
			// requested, we have to unmap it. Otherwise we can re-map it with
			// the new protection.
			bool unmapPage = page->cache != area->cache
				&& (protection & B_WRITE_AREA) != 0;

			if (!unmapPage) {
				map->ops->unmap(map, pageAddress,
					pageAddress + B_PAGE_SIZE - 1);
				map->ops->map(map, pageAddress, physicalAddress,
					actualProtection);
			}

			map->ops->unlock(map);

			if (unmapPage)
				vm_unmap_page(area, pageAddress, true);
		}
	}

	return B_OK;
}


status_t
_user_sync_memory(void* _address, size_t size, int flags)
{
	addr_t address = (addr_t)_address;
	size = PAGE_ALIGN(size);

	// check params
	if ((address % B_PAGE_SIZE) != 0)
		return B_BAD_VALUE;
	if ((addr_t)address + size < (addr_t)address || !IS_USER_ADDRESS(address)
		|| !IS_USER_ADDRESS((addr_t)address + size)) {
		// weird error code required by POSIX
		return ENOMEM;
	}

	bool writeSync = (flags & MS_SYNC) != 0;
	bool writeAsync = (flags & MS_ASYNC) != 0;
	if (writeSync && writeAsync)
		return B_BAD_VALUE;

	if (size == 0 || (!writeSync && !writeAsync))
		return B_OK;

	// iterate through the range and sync all concerned areas
	while (size > 0) {
		// read lock the address space
		AddressSpaceReadLocker locker;
		status_t error = locker.SetTo(team_get_current_team_id());
		if (error != B_OK)
			return error;

		// get the first area
		VMArea* area = locker.AddressSpace()->LookupArea(address);
		if (area == NULL)
			return B_NO_MEMORY;

		uint32 offset = address - area->Base();
		size_t rangeSize = min_c(area->Size() - offset, size);
		offset += area->cache_offset;

		// lock the cache
		AreaCacheLocker cacheLocker(area);
		if (!cacheLocker)
			return B_BAD_VALUE;
		VMCache* cache = area->cache;

		locker.Unlock();

		uint32 firstPage = offset >> PAGE_SHIFT;
		uint32 endPage = firstPage + (rangeSize >> PAGE_SHIFT);

		// write the pages
		if (cache->type == CACHE_TYPE_VNODE) {
			if (writeSync) {
				// synchronous
				error = vm_page_write_modified_page_range(cache, firstPage,
					endPage);
				if (error != B_OK)
					return error;
			} else {
				// asynchronous
				vm_page_schedule_write_page_range(cache, firstPage, endPage);
				// TODO: This is probably not quite what is supposed to happen.
				// Especially when a lot has to be written, it might take ages
				// until it really hits the disk.
			}
		}

		address += rangeSize;
		size -= rangeSize;
	}

	// NOTE: If I understand it correctly the purpose of MS_INVALIDATE is to
	// synchronize multiple mappings of the same file. In our VM they never get
	// out of sync, though, so we don't have to do anything.

	return B_OK;
}


status_t
_user_memory_advice(void* address, size_t size, int advice)
{
	// TODO: Implement!
	return B_OK;
}

/*
 * Copyright 2002-2010, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef KERNEL_VM_VM_TRANSLATION_MAP_H
#define KERNEL_VM_VM_TRANSLATION_MAP_H


#include <kernel.h>
#include <lock.h>


struct kernel_args;
struct vm_page_reservation;
struct VMArea;


struct VMTranslationMap {
								VMTranslationMap();
	virtual						~VMTranslationMap();

	virtual	status_t			InitPostSem() = 0;

	virtual	bool	 			Lock() = 0;
	virtual	void				Unlock() = 0;

	virtual	addr_t				MappedSize() const = 0;
	virtual	size_t				MaxPagesNeededToMap(addr_t start,
									addr_t end) const = 0;

	virtual	status_t			Map(addr_t virtualAddress,
									addr_t physicalAddress,
									uint32 attributes,
									vm_page_reservation* reservation) = 0;
	virtual	status_t			Unmap(addr_t start, addr_t end) = 0;

	// map not locked
	virtual	status_t			UnmapPage(VMArea* area, addr_t address,
									bool updatePageQueue) = 0;
	virtual	void				UnmapPages(VMArea* area, addr_t base,
									size_t size, bool updatePageQueue);
	virtual	void				UnmapArea(VMArea* area,
									bool deletingAddressSpace,
									bool ignoreTopCachePageFlags);

	virtual	status_t			Query(addr_t virtualAddress,
									addr_t* _physicalAddress,
									uint32* _flags) = 0;
	virtual	status_t			QueryInterrupt(addr_t virtualAddress,
									addr_t* _physicalAddress,
									uint32* _flags) = 0;

	virtual	status_t			Protect(addr_t base, addr_t top,
									uint32 attributes) = 0;
			status_t			ProtectPage(VMArea* area, addr_t address,
									uint32 attributes);
			status_t			ProtectArea(VMArea* area,
									uint32 attributes);

	virtual	status_t			ClearFlags(addr_t virtualAddress,
									uint32 flags) = 0;

	virtual	bool				ClearAccessedAndModified(
									VMArea* area, addr_t address,
									bool unmapIfUnaccessed,
									bool& _modified) = 0;

	virtual	void				Flush() = 0;

protected:
			recursive_lock		fLock;
			int32				fMapCount;
};


struct VMPhysicalPageMapper {
								VMPhysicalPageMapper();
	virtual						~VMPhysicalPageMapper();

	// get/put virtual address for physical page -- will be usuable on all CPUs
	// (usually more expensive than the *_current_cpu() versions)
	virtual	status_t			GetPage(addr_t physicalAddress,
									addr_t* _virtualAddress,
									void** _handle) = 0;
	virtual	status_t			PutPage(addr_t virtualAddress,
									void* handle) = 0;

	// get/put virtual address for physical page -- thread must be pinned the
	// whole time
	virtual	status_t			GetPageCurrentCPU(
									addr_t physicalAddress,
									addr_t* _virtualAddress,
									void** _handle) = 0;
	virtual	status_t			PutPageCurrentCPU(addr_t virtualAddress,
									void* _handle) = 0;

	// get/put virtual address for physical in KDL
	virtual	status_t			GetPageDebug(addr_t physicalAddress,
									addr_t* _virtualAddress,
									void** _handle) = 0;
	virtual	status_t			PutPageDebug(addr_t virtualAddress,
									void* handle) = 0;

	// memory operations on pages
	virtual	status_t			MemsetPhysical(addr_t address, int value,
									size_t length) = 0;
	virtual	status_t			MemcpyFromPhysical(void* to, addr_t from,
									size_t length, bool user) = 0;
	virtual	status_t			MemcpyToPhysical(addr_t to, const void* from,
									size_t length, bool user) = 0;
	virtual	void				MemcpyPhysicalPage(addr_t to, addr_t from) = 0;
};



inline status_t
VMTranslationMap::ProtectPage(VMArea* area, addr_t address, uint32 attributes)
{
	return Protect(address, address + B_PAGE_SIZE - 1, attributes);
}


#include <vm/VMArea.h>
inline status_t
VMTranslationMap::ProtectArea(VMArea* area, uint32 attributes)
{
	return Protect(area->Base(), area->Base() + area->Size() - 1, attributes);
}


#include <arch/vm_translation_map.h>

#endif	/* KERNEL_VM_VM_TRANSLATION_MAP_H */

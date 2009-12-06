/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <vm/VMAddressSpace.h>

#include <stdlib.h>

#include <new>

#include <KernelExport.h>

#include <util/OpenHashTable.h>

#include <heap.h>
#include <thread.h>
#include <vm/vm.h>
#include <vm/VMArea.h>

#include "VMKernelAddressSpace.h"
#include "VMUserAddressSpace.h"


//#define TRACE_VM
#ifdef TRACE_VM
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#define ASPACE_HASH_TABLE_SIZE 1024


// #pragma mark - AddressSpaceHashDefinition


struct AddressSpaceHashDefinition {
	typedef team_id			KeyType;
	typedef VMAddressSpace	ValueType;

	size_t HashKey(team_id key) const
	{
		return key;
	}

	size_t Hash(const VMAddressSpace* value) const
	{
		return HashKey(value->ID());
	}

	bool Compare(team_id key, const VMAddressSpace* value) const
	{
		return value->ID() == key;
	}

	VMAddressSpace*& GetLink(VMAddressSpace* value) const
	{
		return value->HashTableLink();
	}
};

typedef BOpenHashTable<AddressSpaceHashDefinition> AddressSpaceTable;

static AddressSpaceTable	sAddressSpaceTable;
static rw_lock				sAddressSpaceTableLock;

VMAddressSpace* VMAddressSpace::sKernelAddressSpace;


// #pragma mark - VMAddressSpace


VMAddressSpace::VMAddressSpace(team_id id, addr_t base, size_t size,
	const char* name)
	:
	fBase(base),
	fEndAddress(base + (size - 1)),
	fFreeSpace(size),
	fID(id),
	fRefCount(1),
	fFaultCount(0),
	fChangeCount(0),
	fDeleting(false)
{
	rw_lock_init(&fLock, name);
//	rw_lock_init(&fLock, kernel ? "kernel address space" : "address space");
}


VMAddressSpace::~VMAddressSpace()
{
	TRACE(("VMAddressSpace::~VMAddressSpace: called on aspace %" B_PRId32 "\n",
		ID()));

	WriteLock();

	fTranslationMap.ops->destroy(&fTranslationMap);

	rw_lock_destroy(&fLock);
}


/*static*/ status_t
VMAddressSpace::Init()
{
	rw_lock_init(&sAddressSpaceTableLock, "address spaces table");

	// create the area and address space hash tables
	{
		new(&sAddressSpaceTable) AddressSpaceTable;
		status_t error = sAddressSpaceTable.Init(ASPACE_HASH_TABLE_SIZE);
		if (error != B_OK)
			panic("vm_init: error creating aspace hash table\n");
	}

	// create the initial kernel address space
	if (Create(B_SYSTEM_TEAM, KERNEL_BASE, KERNEL_SIZE, true,
			&sKernelAddressSpace) != B_OK) {
		panic("vm_init: error creating kernel address space!\n");
	}

	add_debugger_command("aspaces", &_DumpListCommand,
		"Dump a list of all address spaces");
	add_debugger_command("aspace", &_DumpCommand,
		"Dump info about a particular address space");

	return B_OK;
}


/*static*/ status_t
VMAddressSpace::InitPostSem()
{
	status_t status = arch_vm_translation_map_init_kernel_map_post_sem(
		&sKernelAddressSpace->fTranslationMap);
	if (status != B_OK)
		return status;

	return B_OK;
}


void
VMAddressSpace::Put()
{
	bool remove = false;

	rw_lock_write_lock(&sAddressSpaceTableLock);
	if (atomic_add(&fRefCount, -1) == 1) {
		sAddressSpaceTable.RemoveUnchecked(this);
		remove = true;
	}
	rw_lock_write_unlock(&sAddressSpaceTableLock);

	if (remove)
		delete this;
}


/*! Deletes all areas in the specified address space, and the address
	space by decreasing all reference counters. It also marks the
	address space of being in deletion state, so that no more areas
	can be created in it.
	After this, the address space is not operational anymore, but might
	still be in memory until the last reference has been released.
*/
void
VMAddressSpace::RemoveAndPut()
{
	WriteLock();
	fDeleting = true;
	WriteUnlock();

	vm_delete_areas(this);
	Put();
}


status_t
VMAddressSpace::InitObject()
{
	return B_OK;
}


void
VMAddressSpace::Dump() const
{
	kprintf("dump of address space at %p:\n", this);
	kprintf("id: %" B_PRId32 "\n", fID);
	kprintf("ref_count: %" B_PRId32 "\n", fRefCount);
	kprintf("fault_count: %" B_PRId32 "\n", fFaultCount);
	kprintf("translation_map: %p\n", &fTranslationMap);
	kprintf("base: %#" B_PRIxADDR "\n", fBase);
	kprintf("end: %#" B_PRIxADDR "\n", fEndAddress);
	kprintf("change_count: %" B_PRId32 "\n", fChangeCount);
}


/*static*/ status_t
VMAddressSpace::Create(team_id teamID, addr_t base, size_t size, bool kernel,
	VMAddressSpace** _addressSpace)
{
	VMAddressSpace* addressSpace = kernel
		? (VMAddressSpace*)new(nogrow) VMKernelAddressSpace(teamID, base, size)
		: (VMAddressSpace*)new(nogrow) VMUserAddressSpace(teamID, base, size);
	if (addressSpace == NULL)
		return B_NO_MEMORY;

	status_t status = addressSpace->InitObject();
	if (status != B_OK) {
		delete addressSpace;
		return status;
	}

	TRACE(("vm_create_aspace: team %ld (%skernel): %#lx bytes starting at "
		"%#lx => %p\n", id, kernel ? "!" : "", size, base, addressSpace));

	// initialize the corresponding translation map
	status = arch_vm_translation_map_init_map(
		&addressSpace->fTranslationMap, kernel);
	if (status != B_OK) {
		delete addressSpace;
		return status;
	}

	// add the aspace to the global hash table
	rw_lock_write_lock(&sAddressSpaceTableLock);
	sAddressSpaceTable.InsertUnchecked(addressSpace);
	rw_lock_write_unlock(&sAddressSpaceTableLock);

	*_addressSpace = addressSpace;
	return B_OK;
}


/*static*/ VMAddressSpace*
VMAddressSpace::GetKernel()
{
	// we can treat this one a little differently since it can't be deleted
	sKernelAddressSpace->Get();
	return sKernelAddressSpace;
}


/*static*/ team_id
VMAddressSpace::CurrentID()
{
	struct thread* thread = thread_get_current_thread();

	if (thread != NULL && thread->team->address_space != NULL)
		return thread->team->id;

	return B_ERROR;
}


/*static*/ VMAddressSpace*
VMAddressSpace::GetCurrent()
{
	struct thread* thread = thread_get_current_thread();

	if (thread != NULL) {
		VMAddressSpace* addressSpace = thread->team->address_space;
		if (addressSpace != NULL) {
			addressSpace->Get();
			return addressSpace;
		}
	}

	return NULL;
}


/*static*/ VMAddressSpace*
VMAddressSpace::Get(team_id teamID)
{
	rw_lock_read_lock(&sAddressSpaceTableLock);
	VMAddressSpace* addressSpace = sAddressSpaceTable.Lookup(teamID);
	if (addressSpace)
		addressSpace->Get();
	rw_lock_read_unlock(&sAddressSpaceTableLock);

	return addressSpace;
}


/*static*/ int
VMAddressSpace::_DumpCommand(int argc, char** argv)
{
	VMAddressSpace* aspace;

	if (argc < 2) {
		kprintf("aspace: not enough arguments\n");
		return 0;
	}

	// if the argument looks like a number, treat it as such

	{
		team_id id = strtoul(argv[1], NULL, 0);

		aspace = sAddressSpaceTable.Lookup(id);
		if (aspace == NULL) {
			kprintf("invalid aspace id\n");
		} else {
			aspace->Dump();
		}
		return 0;
	}
	return 0;
}


/*static*/ int
VMAddressSpace::_DumpListCommand(int argc, char** argv)
{
	kprintf("   address      id         base          end   area count   "
		" area size\n");

	AddressSpaceTable::Iterator it = sAddressSpaceTable.GetIterator();
	while (VMAddressSpace* space = it.Next()) {
		int32 areaCount = 0;
		off_t areaSize = 0;
		for (VMAddressSpace::AreaIterator areaIt = space->GetAreaIterator();
				VMArea* area = areaIt.Next();) {
			if (area->cache->type != CACHE_TYPE_NULL) {
				areaCount++;
				areaSize += area->Size();
			}
		}
		kprintf("%p  %6" B_PRId32 "   %#010" B_PRIxADDR "   %#10" B_PRIxADDR
			"   %10" B_PRId32 "   %10" B_PRIdOFF "\n", space, space->ID(),
			space->Base(), space->EndAddress(), areaCount, areaSize);
	}

	return 0;
}

/*
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include "vm_store_anonymous_noswap.h"

#include <heap.h>
#include <KernelExport.h>
#include <vm_priv.h>
#include <arch_config.h>

#include <stdlib.h>


//#define TRACE_STORE
#ifdef TRACE_STORE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

// The stack functionality looks like a good candidate to put into its own
// store. I have not done this because once we have a swap file backing up
// the memory, it would probably not be a good idea to separate this
// anymore.

typedef struct anonymous_store {
	vm_store	vm;
	bool		can_overcommit;
	bool		has_precommitted;
	uint8		precommitted_pages;
	int32		guarded_size;
} anonymous_store;


static void
anonymous_destroy(struct vm_store *store)
{
	vm_unreserve_memory(store->committed_size);
	free(store);
}


static status_t
anonymous_commit(struct vm_store *_store, off_t size)
{
	anonymous_store *store = (anonymous_store *)_store;

	size -= store->vm.cache->virtual_base;
		// anonymous stores don't need to span over their whole source

	// if we can overcommit, we don't commit here, but in anonymous_fault()
	if (store->can_overcommit) {
		if (store->has_precommitted)
			return B_OK;

		// pre-commit some pages to make a later failure less probable
		store->has_precommitted = true;
		uint32 precommitted = store->precommitted_pages * B_PAGE_SIZE;
		if (size > precommitted)
			size = precommitted;
	}

	// Check to see how much we could commit - we need real memory

	if (size > store->vm.committed_size) {
		// try to commit
		if (vm_try_reserve_memory(size - store->vm.committed_size) != B_OK)
			return B_NO_MEMORY;
	} else {
		// we can release some
		vm_unreserve_memory(store->vm.committed_size - size);
	}

	store->vm.committed_size = size;
	return B_OK;
}


static bool
anonymous_has_page(struct vm_store *store, off_t offset)
{
	return false;
}


static status_t
anonymous_read(struct vm_store *store, off_t offset, const iovec *vecs,
	size_t count, size_t *_numBytes, bool fsReenter)
{
	panic("anonymous_store: read called. Invalid!\n");
	return B_ERROR;
}


static status_t
anonymous_write(struct vm_store *store, off_t offset, const iovec *vecs,
	size_t count, size_t *_numBytes, bool fsReenter)
{
	// no place to write, this will cause the page daemon to skip this store
	return B_ERROR;
}


static status_t
anonymous_fault(struct vm_store *_store, struct vm_address_space *aspace,
	off_t offset)
{
	anonymous_store *store = (anonymous_store *)_store;

	if (store->can_overcommit) {
		if (store->guarded_size > 0) {
			uint32 guardOffset;

#ifdef STACK_GROWS_DOWNWARDS
			guardOffset = 0;
#elif defined(STACK_GROWS_UPWARDS)
			guardOffset = store->vm.cache->virtual_size - store->guarded_size;
#else
#	error Stack direction has not been defined in arch_config.h
#endif

			// report stack fault, guard page hit!
			if (offset >= guardOffset && offset
					< guardOffset + store->guarded_size) {
				TRACE(("stack overflow!\n"));
				return B_BAD_ADDRESS;
			}
		}

		if (store->precommitted_pages == 0) {
			// try to commit additional memory
			if (vm_try_reserve_memory(B_PAGE_SIZE) != B_OK)
				return B_NO_MEMORY;

			store->vm.committed_size += B_PAGE_SIZE;
		} else
			store->precommitted_pages--;
	}

	// This will cause vm_soft_fault() to handle the fault
	return B_BAD_HANDLER;
}


static vm_store_ops anonymous_ops = {
	&anonymous_destroy,
	&anonymous_commit,
	&anonymous_has_page,
	&anonymous_read,
	&anonymous_write,
	&anonymous_fault,
	NULL,		// acquire unreferenced ref
	NULL,		// acquire ref
	NULL		// release ref
};


/*!	Create a new vm_store that uses anonymous noswap memory */
vm_store *
vm_store_create_anonymous_noswap(bool canOvercommit,
	int32 numPrecommittedPages, int32 numGuardPages)
{
	anonymous_store *store = (anonymous_store *)malloc_nogrow(
		sizeof(anonymous_store));
	if (store == NULL)
		return NULL;

	TRACE(("vm_store_create_anonymous(canOvercommit = %s, numGuardPages = %ld) at %p\n",
		canOvercommit ? "yes" : "no", numGuardPages, store));

	store->vm.ops = &anonymous_ops;
	store->vm.cache = NULL;
	store->vm.committed_size = 0;
	store->can_overcommit = canOvercommit;
	store->has_precommitted = false;
	store->precommitted_pages = min_c(numPrecommittedPages, 255);
	store->guarded_size = numGuardPages * B_PAGE_SIZE;

	return &store->vm;
}


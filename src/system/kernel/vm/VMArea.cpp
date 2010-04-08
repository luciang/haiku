/*
 * Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <vm/VMArea.h>

#include <new>

#include <heap.h>
#include <vm/VMAddressSpace.h>


#define AREA_HASH_TABLE_SIZE 1024


rw_lock VMAreaHash::sLock = RW_LOCK_INITIALIZER("area hash");
VMAreaHashTable VMAreaHash::sTable;
static area_id sNextAreaID = 1;


// #pragma mark - VMArea

VMArea::VMArea(VMAddressSpace* addressSpace, uint32 wiring, uint32 protection)
	:
	name(NULL),
	protection(protection),
	wiring(wiring),
	memory_type(0),
	cache(NULL),
	no_cache_change(0),
	cache_offset(0),
	cache_type(0),
	page_protections(NULL),
	address_space(addressSpace),
	cache_next(NULL),
	cache_prev(NULL),
	hash_next(NULL)
{
	new (&mappings) VMAreaMappings;
}


VMArea::~VMArea()
{
	const uint32 flags = HEAP_DONT_WAIT_FOR_MEMORY
		| HEAP_DONT_LOCK_KERNEL_SPACE;
		// TODO: This might be stricter than necessary.

	free_etc(page_protections, flags);
	free_etc(name, flags);
}


status_t
VMArea::Init(const char* name, uint32 allocationFlags)
{
	// restrict the area name to B_OS_NAME_LENGTH
	size_t length = strlen(name) + 1;
	if (length > B_OS_NAME_LENGTH)
		length = B_OS_NAME_LENGTH;

	// clone the name
	this->name = (char*)malloc_etc(length, allocationFlags);
	if (this->name == NULL)
		return B_NO_MEMORY;
	strlcpy(this->name, name, length);

	id = atomic_add(&sNextAreaID, 1);
	return B_OK;
}


/*!	Returns whether any part of the given address range intersects with a wired
	range of this area.
	The area's top cache must be locked.
*/
bool
VMArea::IsWired(addr_t base, size_t size) const
{
	for (VMAreaWiredRangeList::Iterator it = fWiredRanges.GetIterator();
			VMAreaWiredRange* range = it.Next();) {
		if (range->IntersectsWith(base, size))
			return true;
	}

	return false;
}


/*!	Adds the given wired range to this area.
	The area's top cache must be locked.
*/
void
VMArea::Wire(VMAreaWiredRange* range)
{
	ASSERT(range->area == NULL);

	range->area = this;
	fWiredRanges.Add(range);
}


/*!	Removes the given wired range from this area.
	Must balance a previous Wire() call.
	The area's top cache must be locked.
*/
void
VMArea::Unwire(VMAreaWiredRange* range)
{
	ASSERT(range->area == this);

	// remove the range
	range->area = NULL;
	fWiredRanges.Remove(range);

	// wake up waiters
	for (VMAreaUnwiredWaiterList::Iterator it = range->waiters.GetIterator();
			VMAreaUnwiredWaiter* waiter = it.Next();) {
		waiter->condition.NotifyAll();
	}

	range->waiters.MakeEmpty();
}


/*!	Removes a wired range from this area.

	Must balance a previous Wire() call. The first implicit range with matching
	\a base, \a size, and \a writable attributes is removed and returned. It's
	waiters are woken up as well.
	The area's top cache must be locked.
*/
VMAreaWiredRange*
VMArea::Unwire(addr_t base, size_t size, bool writable)
{
	for (VMAreaWiredRangeList::Iterator it = fWiredRanges.GetIterator();
			VMAreaWiredRange* range = it.Next();) {
		if (range->implicit && range->base == base && range->size == size
				&& range->writable == writable) {
			Unwire(range);
			return range;
		}
	}

	panic("VMArea::Unwire(%#" B_PRIxADDR ", %#" B_PRIxADDR ", %d): no such "
		"range", base, size, writable);
	return NULL;
}


/*!	If the area has any wired range, the given waiter is added to the range and
	prepared for waiting.

	\return \c true, if the waiter has been added, \c false otherwise.
*/
bool
VMArea::AddWaiterIfWired(VMAreaUnwiredWaiter* waiter)
{
	VMAreaWiredRange* range = fWiredRanges.Head();
	if (range == NULL)
		return false;

	waiter->area = this;
	waiter->base = fBase;
	waiter->size = fSize;
	waiter->condition.Init(this, "area unwired");
	waiter->condition.Add(&waiter->waitEntry);

	range->waiters.Add(waiter);

	return true;
}


/*!	If the given address range intersect with a wired range of this area, the
	given waiter is added to the range and prepared for waiting.

	\param waiter The waiter structure that will be added to the wired range
		that intersects with the given address range.
	\param base The base of the address range to check.
	\param size The size of the address range to check.
	\param ignoreRange If given, this wired range of the area is not checked
		whether it intersects with the given address range. Useful when the
		caller has added the range and only wants to check intersection with
		other ranges.
	\return \c true, if the waiter has been added, \c false otherwise.
*/
bool
VMArea::AddWaiterIfWired(VMAreaUnwiredWaiter* waiter, addr_t base, size_t size,
	VMAreaWiredRange* ignoreRange)
{
	for (VMAreaWiredRangeList::Iterator it = fWiredRanges.GetIterator();
			VMAreaWiredRange* range = it.Next();) {
		if (range != ignoreRange && range->IntersectsWith(base, size)) {
			waiter->area = this;
			waiter->base = base;
			waiter->size = size;
			waiter->condition.Init(this, "area unwired");
			waiter->condition.Add(&waiter->waitEntry);

			range->waiters.Add(waiter);

			return true;
		}
	}

	return false;
}


// #pragma mark - VMAreaHash


/*static*/ status_t
VMAreaHash::Init()
{
	return sTable.Init(AREA_HASH_TABLE_SIZE);
}


/*static*/ VMArea*
VMAreaHash::Lookup(area_id id)
{
	ReadLock();
	VMArea* area = LookupLocked(id);
	ReadUnlock();
	return area;
}


/*static*/ area_id
VMAreaHash::Find(const char* name)
{
	ReadLock();

	area_id id = B_NAME_NOT_FOUND;

	// TODO: Iterating through the whole table can be very slow and the whole
	// time we're holding the lock! Use a second hash table!

	for (VMAreaHashTable::Iterator it = sTable.GetIterator();
			VMArea* area = it.Next();) {
		if (strcmp(area->name, name) == 0) {
			id = area->id;
			break;
		}
	}

	ReadUnlock();

	return id;
}


/*static*/ void
VMAreaHash::Insert(VMArea* area)
{
	WriteLock();
	sTable.InsertUnchecked(area);
	WriteUnlock();
}


/*static*/ void
VMAreaHash::Remove(VMArea* area)
{
	WriteLock();
	sTable.RemoveUnchecked(area);
	WriteUnlock();
}

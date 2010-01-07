/*
 * Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <vm/VMCache.h>

#include <stddef.h>
#include <stdlib.h>

#include <algorithm>

#include <arch/cpu.h>
#include <condition_variable.h>
#include <heap.h>
#include <int.h>
#include <kernel.h>
#include <smp.h>
#include <tracing.h>
#include <util/khash.h>
#include <util/AutoLock.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/vm_types.h>
#include <vm/VMArea.h>


//#define TRACE_VM_CACHE
#ifdef TRACE_VM_CACHE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#if DEBUG_CACHE_LIST
VMCache* gDebugCacheList;
#endif
static mutex sCacheListLock = MUTEX_INITIALIZER("global VMCache list");
	// The lock is also needed when the debug feature is disabled.


struct VMCache::PageEventWaiter {
	struct thread*		thread;
	PageEventWaiter*	next;
	vm_page*			page;
	uint32				events;
};


#if VM_CACHE_TRACING

namespace VMCacheTracing {

class VMCacheTraceEntry : public AbstractTraceEntry {
	public:
		VMCacheTraceEntry(VMCache* cache)
			:
			fCache(cache)
		{
#if VM_CACHE_TRACING_STACK_TRACE
			fStackTrace = capture_tracing_stack_trace(
				VM_CACHE_TRACING_STACK_TRACE, 0, true);
				// Don't capture userland stack trace to avoid potential
				// deadlocks.
#endif
		}

#if VM_CACHE_TRACING_STACK_TRACE
		virtual void DumpStackTrace(TraceOutput& out)
		{
			out.PrintStackTrace(fStackTrace);
		}
#endif

		VMCache* Cache() const
		{
			return fCache;
		}

	protected:
		VMCache*	fCache;
#if VM_CACHE_TRACING_STACK_TRACE
		tracing_stack_trace* fStackTrace;
#endif
};


class Create : public VMCacheTraceEntry {
	public:
		Create(VMCache* cache)
			:
			VMCacheTraceEntry(cache)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache create: -> cache: %p", fCache);
		}
};


class Delete : public VMCacheTraceEntry {
	public:
		Delete(VMCache* cache)
			:
			VMCacheTraceEntry(cache)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache delete: cache: %p", fCache);
		}
};


class SetMinimalCommitment : public VMCacheTraceEntry {
	public:
		SetMinimalCommitment(VMCache* cache, off_t commitment)
			:
			VMCacheTraceEntry(cache),
			fOldCommitment(cache->committed_size),
			fCommitment(commitment)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache set min commitment: cache: %p, "
				"commitment: %lld -> %lld", fCache, fOldCommitment,
				fCommitment);
		}

	private:
		off_t	fOldCommitment;
		off_t	fCommitment;
};


class Resize : public VMCacheTraceEntry {
	public:
		Resize(VMCache* cache, off_t size)
			:
			VMCacheTraceEntry(cache),
			fOldSize(cache->virtual_end),
			fSize(size)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache resize: cache: %p, size: %lld -> %lld", fCache,
				fOldSize, fSize);
		}

	private:
		off_t	fOldSize;
		off_t	fSize;
};


class AddConsumer : public VMCacheTraceEntry {
	public:
		AddConsumer(VMCache* cache, VMCache* consumer)
			:
			VMCacheTraceEntry(cache),
			fConsumer(consumer)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache add consumer: cache: %p, consumer: %p", fCache,
				fConsumer);
		}

		VMCache* Consumer() const
		{
			return fConsumer;
		}

	private:
		VMCache*	fConsumer;
};


class RemoveConsumer : public VMCacheTraceEntry {
	public:
		RemoveConsumer(VMCache* cache, VMCache* consumer)
			:
			VMCacheTraceEntry(cache),
			fConsumer(consumer)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache remove consumer: cache: %p, consumer: %p",
				fCache, fConsumer);
		}

	private:
		VMCache*	fConsumer;
};


class Merge : public VMCacheTraceEntry {
	public:
		Merge(VMCache* cache, VMCache* consumer)
			:
			VMCacheTraceEntry(cache),
			fConsumer(consumer)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache merge with consumer: cache: %p, consumer: %p",
				fCache, fConsumer);
		}

	private:
		VMCache*	fConsumer;
};


class InsertArea : public VMCacheTraceEntry {
	public:
		InsertArea(VMCache* cache, VMArea* area)
			:
			VMCacheTraceEntry(cache),
			fArea(area)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache insert area: cache: %p, area: %p", fCache,
				fArea);
		}

		VMArea*	Area() const
		{
			return fArea;
		}

	private:
		VMArea*	fArea;
};


class RemoveArea : public VMCacheTraceEntry {
	public:
		RemoveArea(VMCache* cache, VMArea* area)
			:
			VMCacheTraceEntry(cache),
			fArea(area)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache remove area: cache: %p, area: %p", fCache,
				fArea);
		}

	private:
		VMArea*	fArea;
};

}	// namespace VMCacheTracing

#	define T(x) new(std::nothrow) VMCacheTracing::x;

#	if VM_CACHE_TRACING >= 2

namespace VMCacheTracing {

class InsertPage : public VMCacheTraceEntry {
	public:
		InsertPage(VMCache* cache, vm_page* page, off_t offset)
			:
			VMCacheTraceEntry(cache),
			fPage(page),
			fOffset(offset)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache insert page: cache: %p, page: %p, offset: %lld",
				fCache, fPage, fOffset);
		}

	private:
		vm_page*	fPage;
		off_t		fOffset;
};


class RemovePage : public VMCacheTraceEntry {
	public:
		RemovePage(VMCache* cache, vm_page* page)
			:
			VMCacheTraceEntry(cache),
			fPage(page)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("vm cache remove page: cache: %p, page: %p", fCache,
				fPage);
		}

	private:
		vm_page*	fPage;
};

}	// namespace VMCacheTracing

#		define T2(x) new(std::nothrow) VMCacheTracing::x;
#	else
#		define T2(x) ;
#	endif
#else
#	define T(x) ;
#	define T2(x) ;
#endif


//	#pragma mark - debugger commands


#if VM_CACHE_TRACING


static void*
cache_stack_find_area_cache(const TraceEntryIterator& baseIterator, void* area)
{
	using namespace VMCacheTracing;

	// find the previous "insert area" entry for the given area
	TraceEntryIterator iterator = baseIterator;
	TraceEntry* entry = iterator.Current();
	while (entry != NULL) {
		if (InsertArea* insertAreaEntry = dynamic_cast<InsertArea*>(entry)) {
			if (insertAreaEntry->Area() == area)
				return insertAreaEntry->Cache();
		}

		entry = iterator.Previous();
	}

	return NULL;
}


static void*
cache_stack_find_consumer(const TraceEntryIterator& baseIterator, void* cache)
{
	using namespace VMCacheTracing;

	// find the previous "add consumer" or "create" entry for the given cache
	TraceEntryIterator iterator = baseIterator;
	TraceEntry* entry = iterator.Current();
	while (entry != NULL) {
		if (Create* createEntry = dynamic_cast<Create*>(entry)) {
			if (createEntry->Cache() == cache)
				return NULL;
		} else if (AddConsumer* addEntry = dynamic_cast<AddConsumer*>(entry)) {
			if (addEntry->Consumer() == cache)
				return addEntry->Cache();
		}

		entry = iterator.Previous();
	}

	return NULL;
}


static int
command_cache_stack(int argc, char** argv)
{
	if (argc < 3 || argc > 4) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	bool isArea = false;

	int argi = 1;
	if (argc == 4) {
		if (strcmp(argv[argi], "area") != 0) {
			print_debugger_command_usage(argv[0]);
			return 0;
		}

		argi++;
		isArea = true;
	}

	uint64 addressValue;
	uint64 debugEntryIndex;
	if (!evaluate_debug_expression(argv[argi++], &addressValue, false)
		|| !evaluate_debug_expression(argv[argi++], &debugEntryIndex, false)) {
		return 0;
	}

	TraceEntryIterator baseIterator;
	if (baseIterator.MoveTo((int32)debugEntryIndex) == NULL) {
		kprintf("Invalid tracing entry index %" B_PRIu64 "\n", debugEntryIndex);
		return 0;
	}

	void* address = (void*)(addr_t)addressValue;

	kprintf("cache stack for %s %p at %" B_PRIu64 ":\n",
		isArea ? "area" : "cache", address, debugEntryIndex);
	if (isArea) {
		address = cache_stack_find_area_cache(baseIterator, address);
		if (address == NULL) {
			kprintf("  cache not found\n");
			return 0;
		}
	}

	while (address != NULL) {
		kprintf("  %p\n", address);
		address = cache_stack_find_consumer(baseIterator, address);
	}

	return 0;
}


#endif	// VM_CACHE_TRACING


//	#pragma mark -


status_t
vm_cache_init(kernel_args* args)
{
	return B_OK;
}


void
vm_cache_init_post_heap()
{
#if VM_CACHE_TRACING
	add_debugger_command_etc("cache_stack", &command_cache_stack,
		"List the ancestors (sources) of a VMCache at the time given by "
			"tracing entry index",
		"[ \"area\" ] <address> <tracing entry index>\n"
		"All ancestors (sources) of a given VMCache at the time given by the\n"
		"tracing entry index are listed. If \"area\" is given the supplied\n"
		"address is an area instead of a cache address. The listing will\n"
		"start with the area's cache at that point.\n",
		0);
#endif	// VM_CACHE_TRACING
}


VMCache*
vm_cache_acquire_locked_page_cache(vm_page* page, bool dontWait)
{
	mutex_lock(&sCacheListLock);

	while (dontWait) {
		VMCache* cache = page->cache;
		if (cache == NULL || !cache->TryLock()) {
			mutex_unlock(&sCacheListLock);
			return NULL;
		}

		if (cache == page->cache) {
			cache->AcquireRefLocked();
			mutex_unlock(&sCacheListLock);
			return cache;
		}

		// the cache changed in the meantime
		cache->Unlock();
	}

	while (true) {
		VMCache* cache = page->cache;
		if (cache == NULL) {
			mutex_unlock(&sCacheListLock);
			return NULL;
		}

		if (!cache->SwitchLock(&sCacheListLock)) {
			// cache has been deleted
			mutex_lock(&sCacheListLock);
			continue;
		}

		if (cache == page->cache) {
			cache->AcquireRefLocked();
			return cache;
		}

		// the cache changed in the meantime
		cache->Unlock();
		mutex_lock(&sCacheListLock);
	}
}


// #pragma mark - VMCache


bool
VMCache::_IsMergeable() const
{
	return (areas == NULL && temporary
		&& !list_is_empty(const_cast<list*>(&consumers))
		&& consumers.link.next == consumers.link.prev);
}


VMCache::VMCache()
{
}


VMCache::~VMCache()
{
}


status_t
VMCache::Init(uint32 cacheType)
{
	mutex_init(&fLock, "VMCache");
	VMCache dummyCache;
	list_init_etc(&consumers, offset_of_member(dummyCache, consumer_link));
	areas = NULL;
	fRefCount = 1;
	source = NULL;
	virtual_base = 0;
	virtual_end = 0;
	committed_size = 0;
	temporary = 0;
	scan_skip = 0;
	page_count = 0;
	type = cacheType;
	fPageEventWaiters = NULL;

#if DEBUG_CACHE_LIST
	mutex_lock(&sCacheListLock);

	if (gDebugCacheList)
		gDebugCacheList->debug_previous = this;
	debug_previous = NULL;
	debug_next = gDebugCacheList;
	gDebugCacheList = this;

	mutex_unlock(&sCacheListLock);
#endif

	return B_OK;
}


void
VMCache::Delete()
{
	if (areas != NULL)
		panic("cache %p to be deleted still has areas", this);
	if (!list_is_empty(&consumers))
		panic("cache %p to be deleted still has consumers", this);

	T(Delete(this));

	// free all of the pages in the cache
	while (vm_page* page = pages.Root()) {
		if (!page->mappings.IsEmpty() || page->wired_count != 0) {
			panic("remove page %p from cache %p: page still has mappings!\n",
				page, this);
		}

		// remove it
		pages.Remove(page);
		page->cache = NULL;
		// TODO: we also need to remove all of the page's mappings!

		TRACE(("vm_cache_release_ref: freeing page 0x%lx\n",
			oldPage->physical_page_number));
		DEBUG_PAGE_ACCESS_START(page);
		vm_page_free(this, page);
	}

	// remove the ref to the source
	if (source)
		source->_RemoveConsumer(this);

	// We lock and unlock the sCacheListLock, even if the DEBUG_CACHE_LIST is
	// not enabled. This synchronization point is needed for
	// vm_cache_acquire_locked_page_cache().
	mutex_lock(&sCacheListLock);

#if DEBUG_CACHE_LIST
	if (debug_previous)
		debug_previous->debug_next = debug_next;
	if (debug_next)
		debug_next->debug_previous = debug_previous;
	if (this == gDebugCacheList)
		gDebugCacheList = debug_next;
#endif

	mutex_destroy(&fLock);

	mutex_unlock(&sCacheListLock);

	delete this;
}


void
VMCache::Unlock()
{
	while (fRefCount == 1 && _IsMergeable()) {
		VMCache* consumer = (VMCache*)list_get_first_item(&consumers);
		if (consumer->TryLock()) {
			_MergeWithOnlyConsumer();
		} else {
			// Someone else has locked the consumer ATM. Unlock this cache and
			// wait for the consumer lock. Increment the cache's ref count
			// temporarily, so that no one else will try what we are doing or
			// delete the cache.
			fRefCount++;
			bool consumerLocked = consumer->SwitchLock(&fLock);
			Lock();
			fRefCount--;

			if (consumerLocked) {
				if (fRefCount == 1 && _IsMergeable()
						&& consumer == list_get_first_item(&consumers)) {
					_MergeWithOnlyConsumer();
				} else {
					// something changed, get rid of the consumer lock
					consumer->Unlock();
				}
			}
		}
	}

	if (fRefCount == 0) {
		// delete this cache
		Delete();
	} else
		mutex_unlock(&fLock);
}


vm_page*
VMCache::LookupPage(off_t offset)
{
	AssertLocked();

	vm_page* page = pages.Lookup((page_num_t)(offset >> PAGE_SHIFT));

#if KDEBUG
	if (page != NULL && page->cache != this)
		panic("page %p not in cache %p\n", page, this);
#endif

	return page;
}


void
VMCache::InsertPage(vm_page* page, off_t offset)
{
	TRACE(("VMCache::InsertPage(): cache %p, page %p, offset %Ld\n",
		this, page, offset));
	AssertLocked();

	if (page->cache != NULL) {
		panic("insert page %p into cache %p: page cache is set to %p\n",
			page, this, page->cache);
	}

	T2(InsertPage(this, page, offset));

	page->cache_offset = (page_num_t)(offset >> PAGE_SHIFT);
	page_count++;
	page->usage_count = 2;
	page->cache = this;

#if KDEBUG
	vm_page* otherPage = pages.Lookup(page->cache_offset);
	if (otherPage != NULL) {
		panic("VMCache::InsertPage(): there's already page %p with cache "
			"offset %lu in cache %p; inserting page %p", otherPage,
			page->cache_offset, this, page);
	}
#endif	// KDEBUG

	pages.Insert(page);
}


/*!	Removes the vm_page from this cache. Of course, the page must
	really be in this cache or evil things will happen.
	The cache lock must be held.
*/
void
VMCache::RemovePage(vm_page* page)
{
	TRACE(("VMCache::RemovePage(): cache %p, page %p\n", this, page));
	AssertLocked();

	if (page->cache != this) {
		panic("remove page %p from cache %p: page cache is set to %p\n", page,
			this, page->cache);
	}

	T2(RemovePage(this, page));

	pages.Remove(page);
	page->cache = NULL;
	page_count--;
}


/*!	Moves the given page from its current cache inserts it into this cache.
	Both caches must be locked.
*/
void
VMCache::MovePage(vm_page* page)
{
	VMCache* oldCache = page->cache;

	AssertLocked();
	oldCache->AssertLocked();

	// remove from old cache
	oldCache->pages.Remove(page);
	oldCache->page_count--;
	T2(RemovePage(oldCache, page));

	// insert here
	pages.Insert(page);
	page_count++;
	page->cache = this;
	T2(InsertPage(this, page, page->cache_offset << PAGE_SHIFT));
}


/*!	Moves all pages from the given cache to this one.
	Both caches must be locked. This cache must be empty.
*/
void
VMCache::MoveAllPages(VMCache* fromCache)
{
	AssertLocked();
	fromCache->AssertLocked();
	ASSERT(page_count == 0);

	std::swap(fromCache->pages, pages);
	page_count = fromCache->page_count;
	fromCache->page_count = 0;

	for (VMCachePagesTree::Iterator it = pages.GetIterator();
			vm_page* page = it.Next();) {
		page->cache = this;
		T2(RemovePage(fromCache, page));
		T2(InsertPage(this, page, page->cache_offset << PAGE_SHIFT));
	}
}


/*!	Waits until one or more events happened for a given page which belongs to
	this cache.
	The cache must be locked. It will be unlocked by the method. \a relock
	specifies whether the method shall re-lock the cache before returning.
	\param page The page for which to wait.
	\param events The mask of events the caller is interested in.
	\param relock If \c true, the cache will be locked when returning,
		otherwise it won't be locked.
*/
void
VMCache::WaitForPageEvents(vm_page* page, uint32 events, bool relock)
{
	PageEventWaiter waiter;
	waiter.thread = thread_get_current_thread();
	waiter.next = fPageEventWaiters;
	waiter.page = page;
	waiter.events = events;

	fPageEventWaiters = &waiter;

	thread_prepare_to_block(waiter.thread, 0, THREAD_BLOCK_TYPE_OTHER,
		"cache page events");

	Unlock();
	thread_block();

	if (relock)
		Lock();
}


/*!	Makes this case the source of the \a consumer cache,
	and adds the \a consumer to its list.
	This also grabs a reference to the source cache.
	Assumes you have the cache and the consumer's lock held.
*/
void
VMCache::AddConsumer(VMCache* consumer)
{
	TRACE(("add consumer vm cache %p to cache %p\n", consumer, cache));
	AssertLocked();
	consumer->AssertLocked();

	T(AddConsumer(this, consumer));

	consumer->source = this;
	list_add_item(&consumers, consumer);

	AcquireRefLocked();
	AcquireStoreRef();
}


/*!	Adds the \a area to this cache.
	Assumes you have the locked the cache.
*/
status_t
VMCache::InsertAreaLocked(VMArea* area)
{
	TRACE(("VMCache::InsertAreaLocked(cache %p, area %p)\n", this, area));
	AssertLocked();

	T(InsertArea(this, area));

	area->cache_next = areas;
	if (area->cache_next)
		area->cache_next->cache_prev = area;
	area->cache_prev = NULL;
	areas = area;

	AcquireStoreRef();

	return B_OK;
}


status_t
VMCache::RemoveArea(VMArea* area)
{
	TRACE(("VMCache::RemoveArea(cache %p, area %p)\n", this, area));

	T(RemoveArea(this, area));

	// We release the store reference first, since otherwise we would reverse
	// the locking order or even deadlock ourselves (... -> free_vnode() -> ...
	// -> bfs_remove_vnode() -> ... -> file_cache_set_size() -> mutex_lock()).
	// Also cf. _RemoveConsumer().
	ReleaseStoreRef();

	AutoLocker<VMCache> locker(this);

	if (area->cache_prev)
		area->cache_prev->cache_next = area->cache_next;
	if (area->cache_next)
		area->cache_next->cache_prev = area->cache_prev;
	if (areas == area)
		areas = area->cache_next;

	return B_OK;
}


/*!	Transfers the areas from \a fromCache to this cache. This cache must not
	have areas yet. Both caches must be locked.
*/
void
VMCache::TransferAreas(VMCache* fromCache)
{
	AssertLocked();
	fromCache->AssertLocked();
	ASSERT(areas == NULL);

	areas = fromCache->areas;
	fromCache->areas = NULL;

	for (VMArea* area = areas; area != NULL; area = area->cache_next) {
		area->cache = this;
		AcquireRefLocked();
		fromCache->ReleaseRefLocked();

		T(RemoveArea(fromCache, area));
		T(InsertArea(this, area));
	}
}


uint32
VMCache::CountWritableAreas(VMArea* ignoreArea) const
{
	uint32 count = 0;

	for (VMArea* area = areas; area != NULL; area = area->cache_next) {
		if (area != ignoreArea
			&& (area->protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) != 0) {
			count++;
		}
	}

	return count;
}


status_t
VMCache::WriteModified()
{
	TRACE(("VMCache::WriteModified(cache = %p)\n", this));

	if (temporary)
		return B_OK;

	Lock();
	status_t status = vm_page_write_modified_pages(this);
	Unlock();

	return status;
}


/*!	Commits the memory to the store if the \a commitment is larger than
	what's committed already.
	Assumes you have the cache's lock held.
*/
status_t
VMCache::SetMinimalCommitment(off_t commitment)
{
	TRACE(("VMCache::SetMinimalCommitment(cache %p, commitment %Ld)\n",
		this, commitment));
	AssertLocked();

	T(SetMinimalCommitment(this, commitment));

	status_t status = B_OK;

	// If we don't have enough committed space to cover through to the new end
	// of the area...
	if (committed_size < commitment) {
		// ToDo: should we check if the cache's virtual size is large
		//	enough for a commitment of that size?

		// try to commit more memory
		status = Commit(commitment);
	}

	return status;
}


/*!	This function updates the size field of the cache.
	If needed, it will free up all pages that don't belong to the cache anymore.
	The cache lock must be held when you call it.
	Since removed pages don't belong to the cache any longer, they are not
	written back before they will be removed.

	Note, this function may temporarily release the cache lock in case it
	has to wait for busy pages.
*/
status_t
VMCache::Resize(off_t newSize)
{
	TRACE(("VMCache::Resize(cache %p, newSize %Ld) old size %Ld\n",
		this, newSize, this->virtual_end));
	this->AssertLocked();

	T(Resize(this, newSize));

	status_t status = Commit(newSize - virtual_base);
	if (status != B_OK)
		return status;

	uint32 oldPageCount = (uint32)((virtual_end + B_PAGE_SIZE - 1)
		>> PAGE_SHIFT);
	uint32 newPageCount = (uint32)((newSize + B_PAGE_SIZE - 1) >> PAGE_SHIFT);

	if (newPageCount < oldPageCount) {
		// we need to remove all pages in the cache outside of the new virtual
		// size
		for (VMCachePagesTree::Iterator it
					= pages.GetIterator(newPageCount, true, true);
				vm_page* page = it.Next();) {
			if (page->state == PAGE_STATE_BUSY) {
				if (page->busy_writing) {
					// We cannot wait for the page to become available
					// as we might cause a deadlock this way
					page->busy_writing = false;
						// this will notify the writer to free the page
				} else {
					// wait for page to become unbusy
					WaitForPageEvents(page, PAGE_EVENT_NOT_BUSY, true);

					// restart from the start of the list
					it = pages.GetIterator(newPageCount, true, true);
				}
				continue;
			}

			// remove the page and put it into the free queue
			DEBUG_PAGE_ACCESS_START(page);
			vm_remove_all_page_mappings(page, NULL);
			ASSERT(page->wired_count == 0);
				// TODO: Find a real solution! Unmapping is probably fine, but
				// we have no way of unmapping wired pages here.
			RemovePage(page);
			vm_page_free(this, page);
				// Note: When iterating through a IteratableSplayTree
				// removing the current node is safe.
		}
	}

	virtual_end = newSize;
	return B_OK;
}


/*!	You have to call this function with the VMCache lock held. */
status_t
VMCache::FlushAndRemoveAllPages()
{
	ASSERT_LOCKED_MUTEX(&fLock);

	while (page_count > 0) {
		// write back modified pages
		status_t status = vm_page_write_modified_pages(this);
		if (status != B_OK)
			return status;

		// remove pages
		for (VMCachePagesTree::Iterator it = pages.GetIterator();
				vm_page* page = it.Next();) {
			if (page->state == PAGE_STATE_BUSY) {
				// wait for page to become unbusy
				WaitForPageEvents(page, PAGE_EVENT_NOT_BUSY, true);

				// restart from the start of the list
				it = pages.GetIterator();
				continue;
			}

			// skip modified pages -- they will be written back in the next
			// iteration
			if (page->state == PAGE_STATE_MODIFIED)
				continue;

			// We can't remove mapped pages.
			if (page->wired_count > 0 || !page->mappings.IsEmpty())
				return B_BUSY;

			DEBUG_PAGE_ACCESS_START(page);
			RemovePage(page);
			vm_page_free(this, page);
				// Note: When iterating through a IteratableSplayTree
				// removing the current node is safe.
		}
	}

	return B_OK;
}


status_t
VMCache::Commit(off_t size)
{
	committed_size = size;
	return B_OK;
}


bool
VMCache::HasPage(off_t offset)
{
	return offset >= virtual_base && offset <= virtual_end;
}


status_t
VMCache::Read(off_t offset, const iovec *vecs, size_t count, uint32 flags,
	size_t *_numBytes)
{
	return B_ERROR;
}


status_t
VMCache::Write(off_t offset, const iovec *vecs, size_t count, uint32 flags,
	size_t *_numBytes)
{
	return B_ERROR;
}


status_t
VMCache::WriteAsync(off_t offset, const iovec* vecs, size_t count,
	size_t numBytes, uint32 flags, AsyncIOCallback* callback)
{
	// Not supported, fall back to the synchronous hook.
	size_t transferred = numBytes;
	status_t error = Write(offset, vecs, count, flags, &transferred);

	if (callback != NULL)
		callback->IOFinished(error, transferred != numBytes, transferred);

	return error;
}


/*!	\brief Returns whether the cache can write the page at the given offset.

	The cache must be locked when this function is invoked.

	@param offset The page offset.
	@return \c true, if the page can be written, \c false otherwise.
*/
bool
VMCache::CanWritePage(off_t offset)
{
	return false;
}


status_t
VMCache::Fault(struct VMAddressSpace *aspace, off_t offset)
{
	return B_BAD_ADDRESS;
}


void
VMCache::Merge(VMCache* source)
{
	for (VMCachePagesTree::Iterator it = source->pages.GetIterator();
			vm_page* page = it.Next();) {
		// Note: Removing the current node while iterating through a
		// IteratableSplayTree is safe.
		vm_page* consumerPage = LookupPage(
			(off_t)page->cache_offset << PAGE_SHIFT);
		if (consumerPage == NULL) {
			// the page is not yet in the consumer cache - move it upwards
			MovePage(page);
		}
	}
}


status_t
VMCache::AcquireUnreferencedStoreRef()
{
	return B_OK;
}


void
VMCache::AcquireStoreRef()
{
}


void
VMCache::ReleaseStoreRef()
{
}


/*!	Wakes up threads waiting for page events.
	\param page The page for which events occurred.
	\param events The mask of events that occurred.
*/
void
VMCache::_NotifyPageEvents(vm_page* page, uint32 events)
{
	PageEventWaiter** it = &fPageEventWaiters;
	while (PageEventWaiter* waiter = *it) {
		if (waiter->page == page && (waiter->events & events) != 0) {
			// remove from list and unblock
			*it = waiter->next;
			InterruptsSpinLocker threadsLocker(gThreadSpinlock);
			thread_unblock_locked(waiter->thread, B_OK);
		} else
			it = &waiter->next;
	}
}


/*!	Merges the given cache with its only consumer.
	The caller must hold both the cache's and the consumer's lock. The method
	will unlock the consumer lock.
*/
void
VMCache::_MergeWithOnlyConsumer()
{
	VMCache* consumer = (VMCache*)list_remove_head_item(&consumers);

	TRACE(("merge vm cache %p (ref == %ld) with vm cache %p\n",
		this, this->fRefCount, consumer));

	T(Merge(this, consumer));

	// merge the cache
	consumer->Merge(this);

	// The remaining consumer has got a new source.
	if (source != NULL) {
		VMCache* newSource = source;

		newSource->Lock();

		list_remove_item(&newSource->consumers, this);
		list_add_item(&newSource->consumers, consumer);
		consumer->source = newSource;
		source = NULL;

		newSource->Unlock();
	} else
		consumer->source = NULL;

	// Release the reference the cache's consumer owned. The consumer takes
	// over the cache's ref to its source (if any) instead.
	ReleaseRefLocked();

	consumer->Unlock();
}


/*!	Removes the \a consumer from this cache.
	It will also release the reference to the cache owned by the consumer.
	Assumes you have the consumer's cache lock held. This cache must not be
	locked.
*/
void
VMCache::_RemoveConsumer(VMCache* consumer)
{
	TRACE(("remove consumer vm cache %p from cache %p\n", consumer, this));
	consumer->AssertLocked();

	T(RemoveConsumer(this, consumer));

	// Remove the store ref before locking the cache. Otherwise we'd call into
	// the VFS while holding the cache lock, which would reverse the usual
	// locking order.
	ReleaseStoreRef();

	// remove the consumer from the cache, but keep its reference until later
	Lock();
	list_remove_item(&consumers, consumer);
	consumer->source = NULL;

	ReleaseRefAndUnlock();
}


// #pragma mark - VMCacheFactory
	// TODO: Move to own source file!


#include <heap.h>

#include "VMAnonymousCache.h"
#include "VMAnonymousNoSwapCache.h"
#include "VMDeviceCache.h"
#include "VMNullCache.h"
#include "../cache/vnode_store.h"


/*static*/ status_t
VMCacheFactory::CreateAnonymousCache(VMCache*& _cache, bool canOvercommit,
	int32 numPrecommittedPages, int32 numGuardPages, bool swappable)
{
#if ENABLE_SWAP_SUPPORT
	if (swappable) {
		VMAnonymousCache* cache = new(nogrow) VMAnonymousCache;
		if (cache == NULL)
			return B_NO_MEMORY;

		status_t error = cache->Init(canOvercommit, numPrecommittedPages,
			numGuardPages);
		if (error != B_OK) {
			cache->Delete();
			return error;
		}

		T(Create(cache));

		_cache = cache;
		return B_OK;
	}
#endif

	VMAnonymousNoSwapCache* cache = new(nogrow) VMAnonymousNoSwapCache;
	if (cache == NULL)
		return B_NO_MEMORY;

	status_t error = cache->Init(canOvercommit, numPrecommittedPages,
		numGuardPages);
	if (error != B_OK) {
		cache->Delete();
		return error;
	}

	T(Create(cache));

	_cache = cache;
	return B_OK;
}


/*static*/ status_t
VMCacheFactory::CreateVnodeCache(VMCache*& _cache, struct vnode* vnode)
{
	VMVnodeCache* cache = new(nogrow) VMVnodeCache;
	if (cache == NULL)
		return B_NO_MEMORY;

	status_t error = cache->Init(vnode);
	if (error != B_OK) {
		cache->Delete();
		return error;
	}

	T(Create(cache));

	_cache = cache;
	return B_OK;
}


/*static*/ status_t
VMCacheFactory::CreateDeviceCache(VMCache*& _cache, addr_t baseAddress)
{
	VMDeviceCache* cache = new(nogrow) VMDeviceCache;
	if (cache == NULL)
		return B_NO_MEMORY;

	status_t error = cache->Init(baseAddress);
	if (error != B_OK) {
		cache->Delete();
		return error;
	}

	T(Create(cache));

	_cache = cache;
	return B_OK;
}


/*static*/ status_t
VMCacheFactory::CreateNullCache(VMCache*& _cache)
{
	VMNullCache* cache = new(nogrow) VMNullCache;
	if (cache == NULL)
		return B_NO_MEMORY;

	status_t error = cache->Init();
	if (error != B_OK) {
		cache->Delete();
		return error;
	}

	T(Create(cache));

	_cache = cache;
	return B_OK;
}

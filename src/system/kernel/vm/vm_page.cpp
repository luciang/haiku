/*
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <KernelExport.h>
#include <OS.h>

#include <arch/cpu.h>
#include <arch/vm_translation_map.h>
#include <boot/kernel_args.h>
#include <condition_variable.h>
#include <kernel.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <vm.h>
#include <vm_address_space.h>
#include <vm_low_memory.h>
#include <vm_priv.h>
#include <vm_page.h>
#include <vm_cache.h>

#include "PageCacheLocker.h"


//#define TRACE_VM_PAGE
#ifdef TRACE_VM_PAGE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define SCRUB_SIZE 16
	// this many pages will be cleared at once in the page scrubber thread

typedef struct page_queue {
	vm_page *head;
	vm_page *tail;
	uint32	count;
} page_queue;

extern bool trimming_cycle;

static page_queue sFreePageQueue;
static page_queue sClearPageQueue;
static page_queue sModifiedPageQueue;
static page_queue sActivePageQueue;

static vm_page *sPages;
static addr_t sPhysicalPageOffset;
static size_t sNumPages;
static size_t sReservedPages;

static ConditionVariable<page_queue> sFreePageCondition;
static spinlock sPageLock;

static sem_id sThiefWaitSem;
static sem_id sWriterWaitSem;


/*!	Dequeues a page from the head of the given queue */
static vm_page *
dequeue_page(page_queue *queue)
{
	vm_page *page;

	page = queue->head;
	if (page != NULL) {
		if (queue->tail == page)
			queue->tail = NULL;
		if (page->queue_next != NULL)
			page->queue_next->queue_prev = NULL;

		queue->head = page->queue_next;
		queue->count--;

#ifdef DEBUG_PAGE_QUEUE
		if (page->queue != queue) {
			panic("dequeue_page(queue: %p): page %p thinks it is in queue "
				"%p", queue, page, page->queue);
		}

		page->queue = NULL;
#endif	// DEBUG_PAGE_QUEUE
	}

	return page;
}


/*!	Enqueues a page to the tail of the given queue */
static void
enqueue_page(page_queue *queue, vm_page *page)
{
#ifdef DEBUG_PAGE_QUEUE
	if (page->queue != NULL) {
		panic("enqueue_page(queue: %p, page: %p): page thinks it is "
			"already in queue %p", queue, page, page->queue);
	}
#endif	// DEBUG_PAGE_QUEUE

	if (queue->tail != NULL)
		queue->tail->queue_next = page;
	page->queue_prev = queue->tail;
	queue->tail = page;
	page->queue_next = NULL;
	if (queue->head == NULL)
		queue->head = page;
	queue->count++;

#ifdef DEBUG_PAGE_QUEUE
	page->queue = queue;
#endif
}


/*!	Enqueues a page to the head of the given queue */
static void
enqueue_page_to_head(page_queue *queue, vm_page *page)
{
#ifdef DEBUG_PAGE_QUEUE
	if (page->queue != NULL) {
		panic("enqueue_page_to_head(queue: %p, page: %p): page thinks it is "
			"already in queue %p", queue, page, page->queue);
	}
#endif	// DEBUG_PAGE_QUEUE

	if (queue->head != NULL)
		queue->head->queue_prev = page;
	page->queue_next = queue->head;
	queue->head = page;
	page->queue_prev = NULL;
	if (queue->tail == NULL)
		queue->tail = page;
	queue->count++;

#ifdef DEBUG_PAGE_QUEUE
	page->queue = queue;
#endif
}


static void
remove_page_from_queue(page_queue *queue, vm_page *page)
{
#ifdef DEBUG_PAGE_QUEUE
	if (page->queue != queue) {
		panic("remove_page_from_queue(queue: %p, page: %p): page thinks it "
			"is in queue %p", queue, page, page->queue);
	}
#endif	// DEBUG_PAGE_QUEUE

	if (page->queue_next != NULL)
		page->queue_next->queue_prev = page->queue_prev;
	else
		queue->tail = page->queue_prev;

	if (page->queue_prev != NULL)
		page->queue_prev->queue_next = page->queue_next;
	else
		queue->head = page->queue_next;

	queue->count--;

#ifdef DEBUG_PAGE_QUEUE
	page->queue = NULL;
#endif
}


/*!	Moves a page to the tail of the given queue, but only does so if
	the page is currently in another queue.
*/
static void
move_page_to_queue(page_queue *fromQueue, page_queue *toQueue, vm_page *page)
{
	if (fromQueue != toQueue) {
		remove_page_from_queue(fromQueue, page);
		enqueue_page(toQueue, page);
	}
}


static int
dump_free_page_table(int argc, char **argv)
{
	dprintf("not finished\n");
	return 0;
}


static int
find_page(int argc, char **argv)
{
	struct vm_page *page;
	addr_t address;
	int32 index = 1;
	int i;

	struct {
		const char*	name;
		page_queue*	queue;
	} pageQueueInfos[] = {
		{ "free",		&sFreePageQueue },
		{ "clear",		&sClearPageQueue },
		{ "modified",	&sModifiedPageQueue },
		{ "active",		&sActivePageQueue },
		{ NULL, NULL }
	};

	if (argc < 2
		|| strlen(argv[index]) <= 2
		|| argv[index][0] != '0'
		|| argv[index][1] != 'x') {
		kprintf("usage: find_page <address>\n");
		return 0;
	}

	address = strtoul(argv[index], NULL, 0);
	page = (vm_page*)address;

	for (i = 0; pageQueueInfos[i].name; i++) {
		vm_page* p = pageQueueInfos[i].queue->head;
		while (p) {
			if (p == page) {
				kprintf("found page %p in queue %p (%s)\n", page,
					pageQueueInfos[i].queue, pageQueueInfos[i].name);
				return 0;
			}
			p = p->queue_next;
		}
	}

	kprintf("page %p isn't in any queue\n", page);

	return 0;
}


const char *
page_state_to_string(int state)
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
dump_page(int argc, char **argv)
{
	struct vm_page *page;
	addr_t address;
	bool physical = false;
	int32 index = 1;

	if (argc > 2) {
		if (!strcmp(argv[1], "-p")) {
			physical = true;
			index++;
		} else if (!strcmp(argv[1], "-v"))
			index++;
	}

	if (argc < 2
		|| strlen(argv[index]) <= 2
		|| argv[index][0] != '0'
		|| argv[index][1] != 'x') {
		kprintf("usage: page [-p|-v] <address>\n"
			"  -v looks up a virtual address for the page, -p a physical address.\n"
			"  Default is to look for the page structure address directly\n.");
		return 0;
	}

	address = strtoul(argv[index], NULL, 0);

	if (index == 2) {
		if (!physical) {
			vm_address_space *addressSpace = vm_kernel_address_space();
			uint32 flags;

			if (thread_get_current_thread()->team->address_space != NULL)
				addressSpace = thread_get_current_thread()->team->address_space;

			addressSpace->translation_map.ops->query_interrupt(
				&addressSpace->translation_map, address, &address, &flags);

		}
		page = vm_lookup_page(address / B_PAGE_SIZE);
	} else
		page = (struct vm_page *)address;

	kprintf("PAGE: %p\n", page);
	kprintf("queue_next,prev: %p, %p\n", page->queue_next, page->queue_prev);
	kprintf("hash_next:       %p\n", page->hash_next);
	kprintf("physical_number: %lx\n", page->physical_page_number);
	kprintf("cache:           %p\n", page->cache);
	kprintf("cache_offset:    %ld\n", page->cache_offset);
	kprintf("cache_next,prev: %p, %p\n", page->cache_next, page->cache_prev);
	kprintf("type:            %d\n", page->type);
	kprintf("state:           %s\n", page_state_to_string(page->state));
	kprintf("wired_count:     %d\n", page->wired_count);
	kprintf("usage_count:     %d\n", page->usage_count);
	#ifdef DEBUG_PAGE_QUEUE
		kprintf("queue:           %p\n", page->queue);
	#endif
	#ifdef DEBUG_PAGE_CACHE_TRANSITIONS
		kprintf("debug_flags:     0x%lx\n", page->debug_flags);
		kprintf("collided page:   %p\n", page->collided_page);
	#endif	// DEBUG_PAGE_CACHE_TRANSITIONS
	kprintf("area mappings:\n");

	vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
	vm_page_mapping *mapping;
	while ((mapping = iterator.Next()) != NULL) {
		kprintf("  %p (%#lx)\n", mapping->area, mapping->area->id);
		mapping = mapping->page_link.next;
	}

	return 0;
}


static int
dump_page_queue(int argc, char **argv)
{
	struct page_queue *queue;

	if (argc < 2) {
		kprintf("usage: page_queue <address/name> [list]\n");
		return 0;
	}

	if (strlen(argv[1]) >= 2 && argv[1][0] == '0' && argv[1][1] == 'x')
		queue = (struct page_queue *)strtoul(argv[1], NULL, 16);
	if (!strcmp(argv[1], "free"))
		queue = &sFreePageQueue;
	else if (!strcmp(argv[1], "clear"))
		queue = &sClearPageQueue;
	else if (!strcmp(argv[1], "modified"))
		queue = &sModifiedPageQueue;
	else if (!strcmp(argv[1], "active"))
		queue = &sActivePageQueue;
	else {
		kprintf("page_queue: unknown queue \"%s\".\n", argv[1]);
		return 0;
	}

	kprintf("queue = %p, queue->head = %p, queue->tail = %p, queue->count = %ld\n",
		queue, queue->head, queue->tail, queue->count);

	if (argc == 3) {
		struct vm_page *page = queue->head;
		int i;

		kprintf("page        cache          state  wired  usage\n");
		for (i = 0; page; i++, page = page->queue_next) {
			kprintf("%p  %p  %8s  %5d  %5d\n", page, page->cache,
				page_state_to_string(page->state),
				page->wired_count, page->usage_count);
		}
	}
	return 0;
}


static int
dump_page_stats(int argc, char **argv)
{
	uint32 counter[8];
	int32 totalActive;
	addr_t i;

	memset(counter, 0, sizeof(counter));

	for (i = 0; i < sNumPages; i++) {
		if (sPages[i].state > 7)
			panic("page %li at %p has invalid state!\n", i, &sPages[i]);

		counter[sPages[i].state]++;
	}

	kprintf("page stats:\n");
	kprintf("active: %lu\ninactive: %lu\nbusy: %lu\nunused: %lu\n",
		counter[PAGE_STATE_ACTIVE], counter[PAGE_STATE_INACTIVE],
		counter[PAGE_STATE_BUSY], counter[PAGE_STATE_UNUSED]);
	kprintf("wired: %lu\nmodified: %lu\nfree: %lu\nclear: %lu\n",
		counter[PAGE_STATE_WIRED], counter[PAGE_STATE_MODIFIED],
		counter[PAGE_STATE_FREE], counter[PAGE_STATE_CLEAR]);

	kprintf("\nfree_queue: %p, count = %ld\n", &sFreePageQueue,
		sFreePageQueue.count);
	kprintf("clear_queue: %p, count = %ld\n", &sClearPageQueue,
		sClearPageQueue.count);
	kprintf("modified_queue: %p, count = %ld\n", &sModifiedPageQueue,
		sModifiedPageQueue.count);
	kprintf("active_queue: %p, count = %ld\n", &sActivePageQueue,
		sActivePageQueue.count);

	return 0;
}


static status_t
set_page_state_nolock(vm_page *page, int pageState)
{
	page_queue *fromQueue = NULL;
	page_queue *toQueue = NULL;

	switch (page->state) {
		case PAGE_STATE_BUSY:
		case PAGE_STATE_ACTIVE:
		case PAGE_STATE_INACTIVE:
		case PAGE_STATE_WIRED:
		case PAGE_STATE_UNUSED:
			fromQueue = &sActivePageQueue;
			break;
		case PAGE_STATE_MODIFIED:
			fromQueue = &sModifiedPageQueue;
			break;
		case PAGE_STATE_FREE:
			fromQueue = &sFreePageQueue;
			break;
		case PAGE_STATE_CLEAR:
			fromQueue = &sClearPageQueue;
			break;
		default:
			panic("vm_page_set_state: vm_page %p in invalid state %d\n",
				page, page->state);
			break;
	}

	if (page->state == PAGE_STATE_CLEAR || page->state == PAGE_STATE_FREE) {
		if (page->cache != NULL)
			panic("free page %p has cache", page);
	}

	switch (pageState) {
		case PAGE_STATE_BUSY:
		case PAGE_STATE_ACTIVE:
		case PAGE_STATE_INACTIVE:
		case PAGE_STATE_WIRED:
		case PAGE_STATE_UNUSED:
			toQueue = &sActivePageQueue;
			break;
		case PAGE_STATE_MODIFIED:
			toQueue = &sModifiedPageQueue;
			break;
		case PAGE_STATE_FREE:
			toQueue = &sFreePageQueue;
			break;
		case PAGE_STATE_CLEAR:
			toQueue = &sClearPageQueue;
			break;
		default:
			panic("vm_page_set_state: invalid target state %d\n", pageState);
	}

	if (pageState == PAGE_STATE_CLEAR || pageState == PAGE_STATE_FREE) {
		if (sFreePageQueue.count + sClearPageQueue.count <= sReservedPages)
			sFreePageCondition.NotifyAll();

		if (page->cache != NULL)
			panic("to be freed page %p has cache", page);
	}

	page->state = pageState;
	move_page_to_queue(fromQueue, toQueue, page);

	return B_OK;
}


static void
move_page_to_active_or_inactive_queue(vm_page *page, bool dequeued)
{
	// Note, this logic must be in sync with what the page daemon does
	int32 state;
	if (!page->mappings.IsEmpty() || page->usage_count >= 0
		|| page->wired_count)
		state = PAGE_STATE_ACTIVE;
	else
		state = PAGE_STATE_INACTIVE;

	if (dequeued) {
		page->state = state;
		enqueue_page(&sActivePageQueue, page);
	} else
		set_page_state_nolock(page, state);
}


static void
clear_page(addr_t pa)
{
	addr_t va;

//	dprintf("clear_page: clearing page 0x%x\n", pa);

	vm_get_physical_page(pa, &va, PHYSICAL_PAGE_CAN_WAIT);

	memset((void *)va, 0, B_PAGE_SIZE);

	vm_put_physical_page(va);
}


/*!
	This is a background thread that wakes up every now and then (every 100ms)
	and moves some pages from the free queue over to the clear queue.
	Given enough time, it will clear out all pages from the free queue - we
	could probably slow it down after having reached a certain threshold.
*/
static int32
page_scrubber(void *unused)
{
	(void)(unused);

	TRACE(("page_scrubber starting...\n"));

	for (;;) {
		snooze(100000); // 100ms

		if (sFreePageQueue.count > 0) {
			cpu_status state;
			vm_page *page[SCRUB_SIZE];
			int32 i, scrubCount;

			// get some pages from the free queue

			state = disable_interrupts();
			acquire_spinlock(&sPageLock);

			for (i = 0; i < SCRUB_SIZE; i++) {
				page[i] = dequeue_page(&sFreePageQueue);
				if (page[i] == NULL)
					break;
				page[i]->state = PAGE_STATE_BUSY;
			}

			release_spinlock(&sPageLock);
			restore_interrupts(state);

			// clear them

			scrubCount = i;

			for (i = 0; i < scrubCount; i++) {
				clear_page(page[i]->physical_page_number * B_PAGE_SIZE);
			}

			state = disable_interrupts();
			acquire_spinlock(&sPageLock);

			// and put them into the clear queue

			for (i = 0; i < scrubCount; i++) {
				page[i]->state = PAGE_STATE_CLEAR;
				enqueue_page(&sClearPageQueue, page[i]);
			}

			release_spinlock(&sPageLock);
			restore_interrupts(state);
		}
	}

	return 0;
}


static status_t
write_page(vm_page *page, bool mayBlock, bool fsReenter)
{
	vm_store *store = page->cache->store;
	size_t length = B_PAGE_SIZE;
	status_t status;
	iovec vecs[1];

	TRACE(("write_page(page = %p): offset = %Ld\n", page, (off_t)page->cache_offset << PAGE_SHIFT));

	status = vm_get_physical_page(page->physical_page_number * B_PAGE_SIZE,
		(addr_t *)&vecs[0].iov_base, PHYSICAL_PAGE_CAN_WAIT);
	if (status < B_OK)
		panic("could not map page!");
	vecs->iov_len = B_PAGE_SIZE;

	status = store->ops->write(store, (off_t)page->cache_offset << PAGE_SHIFT,
		vecs, 1, &length, mayBlock, fsReenter);

	vm_put_physical_page((addr_t)vecs[0].iov_base);

	if (status < B_OK) {
		dprintf("write_page(page = %p): offset = %lx, status = %ld\n",
			page, page->cache_offset, status);
	}

	return status;
}


/*!	The page writer continuously takes some pages from the modified
	queue, writes them back, and moves them back to the active queue.
	It runs in its own thread, and is only there to keep the number
	of modified pages low, so that more pages can be reused with
	fewer costs.
*/
status_t
page_writer(void* /*unused*/)
{
	while (true) {
		acquire_sem_etc(sWriterWaitSem, 1, B_RELATIVE_TIMEOUT, 3000000);
			// all 3 seconds when no one triggers us

		const uint32 kNumPages = 32;
		ConditionVariable<vm_page> busyConditions[kNumPages];
		vm_page *pages[kNumPages];
		uint32 numPages = 0;

		// TODO: once the I/O scheduler is there, we should write
		// a lot more pages back.
		// TODO: make this laptop friendly, too (ie. only start doing
		// something if someone else did something or there is really
		// enough to do).

		// collect pages to be written

		while (numPages < kNumPages) {
			InterruptsSpinLocker locker(sPageLock);

			vm_page *page = sModifiedPageQueue.head;
			while (page != NULL && page->state == PAGE_STATE_BUSY) {
				// skip busy pages
				page = page->queue_next;
			}
			if (page == NULL)
				break;

			locker.Unlock();

			PageCacheLocker cacheLocker(page);
			if (!cacheLocker.IsLocked())
				continue;

			locker.Lock();
			remove_page_from_queue(&sModifiedPageQueue, page);
			page->state = PAGE_STATE_BUSY;

			busyConditions[numPages].Publish(page, "page");

			locker.Unlock();

			//dprintf("write page %p, cache %p (%ld)\n", page, page->cache, page->cache->ref_count);
			vm_clear_map_flags(page, PAGE_MODIFIED);
			vm_cache_acquire_ref(page->cache);
			pages[numPages++] = page;
		}

		if (numPages == 0)
			continue;

		// write pages to disk

		// TODO: put this as requests into the I/O scheduler
		status_t writeStatus[kNumPages];
		for (uint32 i = 0; i < numPages; i++) {
			writeStatus[i] = write_page(pages[i], false, false);
		}

		// mark pages depending on whether they could be written or not

		for (uint32 i = 0; i < numPages; i++) {
			vm_cache *cache = pages[i]->cache;
			mutex_lock(&cache->lock);

			if (writeStatus[i] == B_OK) {
				// put it into the active queue
				InterruptsSpinLocker locker(sPageLock);
				move_page_to_active_or_inactive_queue(pages[i], true);
			} else {
				// We don't have to put the PAGE_MODIFIED bit back, as it's
				// still in the modified pages list.
				InterruptsSpinLocker locker(sPageLock);
				pages[i]->state = PAGE_STATE_MODIFIED;
				enqueue_page(&sModifiedPageQueue, pages[i]);
			}

			busyConditions[i].Unpublish();

			mutex_unlock(&cache->lock);
			vm_cache_release_ref(cache);
		}
	}

	return B_OK;
}


/*! The page thief is a "service" that runs in its own thread, and only
	gets active in low memory situations.
	Its job is to remove unused pages from caches and recycle it. When
	low memory is critical, this is potentially the only source of free
	pages for the system, so it cannot block on an external service.
*/
static status_t
page_thief(void* /*unused*/)
{
	bigtime_t timeout = B_INFINITE_TIMEOUT;

	while (true) {
		acquire_sem_etc(sThiefWaitSem, 1, B_RELATIVE_TIMEOUT, timeout);

		int32 state = vm_low_memory_state();
		uint32 steal;
		int32 score;
		switch (state) {
			default:
				timeout = B_INFINITE_TIMEOUT;
				continue;

			case B_LOW_MEMORY_NOTE:
				steal = 10;
				score = -20;
				timeout = 1000000;
				break;
			case B_LOW_MEMORY_WARNING:
				steal = 50;
				score = -5;
				timeout = 1000000;
				break;
			case B_LOW_MEMORY_CRITICAL:
				steal = 500;
				score = -1;
				timeout = 100000;
				break;
		}
		//dprintf("page thief running - target %ld...\n", steal);

		vm_page* page = NULL;
		bool stealActive = false, desperate = false;
		InterruptsSpinLocker locker(sPageLock);

		while (steal > 0) {
			if (!locker.IsLocked())
				locker.Lock();

			// find a candidate to steal from the inactive queue

			for (int32 i = sActivePageQueue.count; i-- > 0;) {
				// move page to the tail of the queue so that we don't
				// scan it again directly
				page = dequeue_page(&sActivePageQueue);
				enqueue_page(&sActivePageQueue, page);

				if ((page->state == PAGE_STATE_INACTIVE
						|| (stealActive && page->state == PAGE_STATE_ACTIVE
							&& page->wired_count == 0))
					&& page->usage_count <= score)
					break;
			}

			if (page == NULL) {
				if (score == 0) {
					if (state != B_LOW_MEMORY_CRITICAL)
						break;
					if (stealActive && score > 5)
						break;

					// when memory is really low, we really want pages
					if (stealActive) {
						score = 127;
						desperate = true;
					} else {
						stealActive = true;
						score = 5;
						steal = 5;
					}
					continue;
				}

				score = 0;
				continue;
			}

			locker.Unlock();

			// try to lock the page's cache

			class PageCacheTryLocker {
			public:
				PageCacheTryLocker(vm_page *page)
					: fIsLocked(false)
				{
					fCache = vm_cache_acquire_page_cache_ref(page);
					if (fCache != NULL
						&& mutex_trylock(&fCache->lock) == B_OK) {
						if (fCache == page->cache)
							fIsLocked = true;
						else
							mutex_unlock(&fCache->lock);
					}
				}

				~PageCacheTryLocker()
				{
					if (fIsLocked)
						mutex_unlock(&fCache->lock);
					if (fCache != NULL)
						vm_cache_release_ref(fCache);
				}

				bool IsLocked() { return fIsLocked; }

			private:
				vm_cache *fCache;
				bool fIsLocked;
			} cacheLocker(page);

			if (!cacheLocker.IsLocked())
				continue;

			// check again if that page is still a candidate
			if (page->state != PAGE_STATE_INACTIVE
				&& (!stealActive || page->state != PAGE_STATE_ACTIVE
					|| page->wired_count != 0))
				continue;

			// recheck eventual last minute changes
			uint32 flags;
			vm_remove_all_page_mappings(page, &flags);
			if ((flags & PAGE_MODIFIED) != 0) {
				// page was modified, don't steal it
				vm_page_set_state(page, PAGE_STATE_MODIFIED);
				continue;
			} else if ((flags & PAGE_ACCESSED) != 0 && !desperate) {
				// page is in active use, don't steal it
				vm_page_set_state(page, PAGE_STATE_ACTIVE);
				continue;
			}

			// we can now steal this page

			//dprintf("  steal page %p from cache %p\n", page, page->cache);

			vm_cache_remove_page(page->cache, page);
			vm_page_set_state(page, PAGE_STATE_FREE);
			steal--;
		}
	}

	return B_OK;
}


/*!	This triggers a run of the page thief - depending on the memory
	pressure, it might not actually do anything, though.
	This function is registered as a low memory handler.
*/
static void
schedule_page_thief(void* /*unused*/, int32 level)
{
	release_sem(sThiefWaitSem);
}


//	#pragma mark - private kernel API


/*!	You need to hold the vm_cache lock when calling this function.
	Note that the cache lock is released in this function.
*/
status_t
vm_page_write_modified_pages(vm_cache *cache, bool fsReenter)
{
	// ToDo: join adjacent pages into one vec list

	for (vm_page *page = cache->page_list; page; page = page->cache_next) {
		bool dequeuedPage = false;

		if (page->state == PAGE_STATE_MODIFIED) {
			InterruptsSpinLocker locker(&sPageLock);
			remove_page_from_queue(&sModifiedPageQueue, page);
			dequeuedPage = true;
		} else if (page->state == PAGE_STATE_BUSY
				|| !vm_test_map_modification(page)) {
			continue;
		}

		page->state = PAGE_STATE_BUSY;

		ConditionVariable<vm_page> busyCondition;
		busyCondition.Publish(page, "page");

		// We have a modified page - however, while we're writing it back,
		// the page is still mapped. In order not to lose any changes to the
		// page, we mark it clean before actually writing it back; if writing
		// the page fails for some reason, we just keep it in the modified page
		// list, but that should happen only rarely.

		// If the page is changed after we cleared the dirty flag, but before we
		// had the chance to write it back, then we'll write it again later -
		// that will probably not happen that often, though.

		// clear the modified flag
		vm_clear_map_flags(page, PAGE_MODIFIED);

		mutex_unlock(&cache->lock);
		status_t status = write_page(page, true, fsReenter);
		mutex_lock(&cache->lock);

		InterruptsSpinLocker locker(&sPageLock);

		if (status == B_OK) {
			// put it into the active/inactive queue
			move_page_to_active_or_inactive_queue(page, dequeuedPage);
		} else {
			// We don't have to put the PAGE_MODIFIED bit back, as it's still
			// in the modified pages list.
			if (dequeuedPage) {
				page->state = PAGE_STATE_MODIFIED;
				enqueue_page(&sModifiedPageQueue, page);
			} else
				set_page_state_nolock(page, PAGE_STATE_MODIFIED);
		}

		busyCondition.Unpublish();
	}

	return B_OK;
}


/*!	Schedules the page writer to write back the specified \a page.
	Note, however, that it doesn't do this immediately, and it might
	take several seconds until the page is actually written out.
*/
void
vm_page_schedule_write_page(vm_page *page)
{
	ASSERT(page->state == PAGE_STATE_MODIFIED);

	vm_page_requeue(page, false);

	release_sem_etc(sWriterWaitSem, 1,
		B_RELEASE_IF_WAITING_ONLY | B_DO_NOT_RESCHEDULE);
}


void
vm_page_init_num_pages(kernel_args *args)
{
	uint32 i;

	// calculate the size of memory by looking at the physical_memory_range array
	addr_t physicalPagesEnd = 0;
	sPhysicalPageOffset = args->physical_memory_range[0].start / B_PAGE_SIZE;

	for (i = 0; i < args->num_physical_memory_ranges; i++) {
		physicalPagesEnd = (args->physical_memory_range[i].start
			+ args->physical_memory_range[i].size) / B_PAGE_SIZE;
	}

	TRACE(("first phys page = 0x%lx, end 0x%x\n", sPhysicalPageOffset,
		physicalPagesEnd));

	sNumPages = physicalPagesEnd - sPhysicalPageOffset;
}


status_t
vm_page_init(kernel_args *args)
{
	uint32 i;

	TRACE(("vm_page_init: entry\n"));

	sPageLock = 0;

	// initialize queues
	sFreePageQueue.head = NULL;
	sFreePageQueue.tail = NULL;
	sFreePageQueue.count = 0;
	sClearPageQueue.head = NULL;
	sClearPageQueue.tail = NULL;
	sClearPageQueue.count = 0;
	sModifiedPageQueue.head = NULL;
	sModifiedPageQueue.tail = NULL;
	sModifiedPageQueue.count = 0;
	sActivePageQueue.head = NULL;
	sActivePageQueue.tail = NULL;
	sActivePageQueue.count = 0;

	// map in the new free page table
	sPages = (vm_page *)vm_allocate_early(args, sNumPages * sizeof(vm_page),
		~0L, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	TRACE(("vm_init: putting free_page_table @ %p, # ents %d (size 0x%x)\n",
		sPages, sNumPages, (unsigned int)(sNumPages * sizeof(vm_page))));

	// initialize the free page table
	for (i = 0; i < sNumPages; i++) {
		sPages[i].physical_page_number = sPhysicalPageOffset + i;
		sPages[i].type = PAGE_TYPE_PHYSICAL;
		sPages[i].state = PAGE_STATE_FREE;
		new(&sPages[i].mappings) vm_page_mappings();
		sPages[i].wired_count = 0;
		sPages[i].usage_count = 0;
		sPages[i].cache = NULL;
		#ifdef DEBUG_PAGE_QUEUE
			sPages[i].queue = NULL;
		#endif
		#ifdef DEBUG_PAGE_CACHE_TRANSITIONS
			sPages[i].debug_flags = 0;
			sPages[i].collided_page = NULL;
		#endif	// DEBUG_PAGE_CACHE_TRANSITIONS
		enqueue_page(&sFreePageQueue, &sPages[i]);
	}

	TRACE(("initialized table\n"));

	// mark some of the page ranges inuse
	for (i = 0; i < args->num_physical_allocated_ranges; i++) {
		vm_mark_page_range_inuse(args->physical_allocated_range[i].start / B_PAGE_SIZE,
			args->physical_allocated_range[i].size / B_PAGE_SIZE);
	}

	TRACE(("vm_page_init: exit\n"));

	return B_OK;
}


status_t
vm_page_init_post_area(kernel_args *args)
{
	void *dummy;

	dummy = sPages;
	create_area("page structures", &dummy, B_EXACT_ADDRESS,
		PAGE_ALIGN(sNumPages * sizeof(vm_page)), B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	add_debugger_command("page_stats", &dump_page_stats, "Dump statistics about page usage");
	add_debugger_command("free_pages", &dump_free_page_table, "Dump list of free pages");
	add_debugger_command("page", &dump_page, "Dump page info");
	add_debugger_command("page_queue", &dump_page_queue, "Dump page queue");
	add_debugger_command("find_page", &find_page,
		"Find out which queue a page is actually in");

	return B_OK;
}


status_t
vm_page_init_post_thread(kernel_args *args)
{
	// create a kernel thread to clear out pages

	thread_id thread = spawn_kernel_thread(&page_scrubber, "page scrubber",
		B_LOWEST_ACTIVE_PRIORITY, NULL);
	send_signal_etc(thread, SIGCONT, B_DO_NOT_RESCHEDULE);

	new (&sFreePageCondition) ConditionVariable<page_queue>;
	sFreePageCondition.Publish(&sFreePageQueue, "free page");

	// start page writer and page thief

	sWriterWaitSem = create_sem(0, "page writer");
	sThiefWaitSem = create_sem(0, "page thief");

	thread = spawn_kernel_thread(&page_writer, "page writer",
		B_LOW_PRIORITY, NULL);
	send_signal_etc(thread, SIGCONT, B_DO_NOT_RESCHEDULE);

	thread = spawn_kernel_thread(&page_thief, "page thief",
		B_LOW_PRIORITY, NULL);
	send_signal_etc(thread, SIGCONT, B_DO_NOT_RESCHEDULE);

	register_low_memory_handler(schedule_page_thief, NULL, 100);
	return B_OK;
}


status_t
vm_mark_page_inuse(addr_t page)
{
	return vm_mark_page_range_inuse(page, 1);
}


status_t
vm_mark_page_range_inuse(addr_t startPage, addr_t length)
{
	TRACE(("vm_mark_page_range_inuse: start 0x%lx, len 0x%lx\n",
		startPage, length));

	if (sPhysicalPageOffset > startPage) {
		TRACE(("vm_mark_page_range_inuse: start page %ld is before free list\n",
			startPage));
		return B_BAD_VALUE;
	}
	startPage -= sPhysicalPageOffset;
	if (startPage + length > sNumPages) {
		TRACE(("vm_mark_page_range_inuse: range would extend past free list\n"));
		return B_BAD_VALUE;
	}

	cpu_status state = disable_interrupts();
	acquire_spinlock(&sPageLock);

	for (addr_t i = 0; i < length; i++) {
		vm_page *page = &sPages[startPage + i];
		switch (page->state) {
			case PAGE_STATE_FREE:
			case PAGE_STATE_CLEAR:
				set_page_state_nolock(page, PAGE_STATE_UNUSED);
				break;
			case PAGE_STATE_WIRED:
				break;
			case PAGE_STATE_ACTIVE:
			case PAGE_STATE_INACTIVE:
			case PAGE_STATE_BUSY:
			case PAGE_STATE_MODIFIED:
			case PAGE_STATE_UNUSED:
			default:
				// uh
				dprintf("vm_mark_page_range_inuse: page 0x%lx in non-free state %d!\n",
					startPage + i, page->state);
				break;
		}
	}

	release_spinlock(&sPageLock);
	restore_interrupts(state);

	return B_OK;
}


/*!	Unreserve pages previously reserved with vm_page_reserve_pages().
	Note, you specify the same \a count here that you specified when
	reserving the pages - you don't need to keep track how many pages
	you actually needed of that upfront allocation.
*/
void
vm_page_unreserve_pages(uint32 count)
{
	InterruptsSpinLocker locker(sPageLock);
	ASSERT(sReservedPages >= count);

	sReservedPages -= count;
	
	if (vm_page_num_free_pages() <= sReservedPages)
		sFreePageCondition.NotifyAll();
}


/*!	With this call, you can reserve a number of free pages in the system.
	They will only be handed out to someone who has actually reserved them.
	This call returns as soon as the number of requested pages has been
	reached.
*/
void
vm_page_reserve_pages(uint32 count)
{
	InterruptsSpinLocker locker(sPageLock);

	sReservedPages += count;
	size_t freePages = vm_page_num_free_pages();
	if (sReservedPages < freePages)
		return;

	ConditionVariableEntry<page_queue> freeConditionEntry;
	freeConditionEntry.Add(&sFreePageQueue);
	vm_low_memory(sReservedPages - freePages);

	// we need to wait until new pages become available
	locker.Unlock();

	freeConditionEntry.Wait();
}


vm_page *
vm_page_allocate_page(int pageState, bool reserved)
{
	ConditionVariableEntry<page_queue> freeConditionEntry;
	page_queue *queue;
	page_queue *otherQueue;

	switch (pageState) {
		case PAGE_STATE_FREE:
			queue = &sFreePageQueue;
			otherQueue = &sClearPageQueue;
			break;
		case PAGE_STATE_CLEAR:
			queue = &sClearPageQueue;
			otherQueue = &sFreePageQueue;
			break;
		default:
			return NULL; // invalid
	}

	InterruptsSpinLocker locker(sPageLock);

	vm_page *page = NULL;
	while (true) {
		if (reserved || sReservedPages < vm_page_num_free_pages()) {
			page = dequeue_page(queue);
			if (page == NULL) {
#ifdef DEBUG
				if (queue->count != 0)
					panic("queue %p corrupted, count = %d\n", queue, queue->count);
#endif

				// if the primary queue was empty, grap the page from the
				// secondary queue
				page = dequeue_page(otherQueue);
			}
		}

		if (page == NULL) {
#ifdef DEBUG
			if (otherQueue->count != 0) {
				panic("other queue %p corrupted, count = %d\n", otherQueue,
					otherQueue->count);
			}
#endif

			freeConditionEntry.Add(&sFreePageQueue);
			vm_low_memory(sReservedPages + 1);
		}

		if (page != NULL)
			break;

		// we need to wait until new pages become available
		locker.Unlock();

		freeConditionEntry.Wait();

		locker.Lock();
	}

	if (page->cache != NULL)
		panic("supposed to be free page %p has cache\n", page);

	int oldPageState = page->state;
	page->state = PAGE_STATE_BUSY;
	page->usage_count = 2;

	enqueue_page(&sActivePageQueue, page);

	locker.Unlock();

	// if needed take the page from the free queue and zero it out
	if (pageState == PAGE_STATE_CLEAR && oldPageState != PAGE_STATE_CLEAR)
		clear_page(page->physical_page_number * B_PAGE_SIZE);

	return page;
}


/*!	Allocates a number of pages and puts their pointers into the provided
	array. All pages are marked busy.
	Returns B_OK on success, and B_NO_MEMORY when there aren't any free
	pages left to allocate.
*/
status_t
vm_page_allocate_pages(int pageState, vm_page **pages, uint32 numPages)
{
	uint32 i;
	
	for (i = 0; i < numPages; i++) {
		pages[i] = vm_page_allocate_page(pageState, false);
		if (pages[i] == NULL) {
			// allocation failed, we need to free what we already have
			while (i-- > 0)
				vm_page_set_state(pages[i], pageState);

			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


vm_page *
vm_page_allocate_page_run(int pageState, addr_t length)
{
	// TODO: make sure this one doesn't steal reserved pages!
	vm_page *firstPage = NULL;
	uint32 start = 0;

	cpu_status state = disable_interrupts();
	acquire_spinlock(&sPageLock);

	for (;;) {
		bool foundRun = true;
		if (start + length > sNumPages)
			break;

		uint32 i;
		for (i = 0; i < length; i++) {
			if (sPages[start + i].state != PAGE_STATE_FREE
				&& sPages[start + i].state != PAGE_STATE_CLEAR) {
				foundRun = false;
				i++;
				break;
			}
		}
		if (foundRun) {
			// pull the pages out of the appropriate queues
			for (i = 0; i < length; i++) {
				sPages[start + i].is_cleared
					= sPages[start + i].state == PAGE_STATE_CLEAR;
				set_page_state_nolock(&sPages[start + i], PAGE_STATE_BUSY);
				sPages[i].usage_count = 2;
			}
			firstPage = &sPages[start];
			break;
		} else {
			start += i;
		}
	}
	release_spinlock(&sPageLock);
	restore_interrupts(state);

	if (firstPage != NULL && pageState == PAGE_STATE_CLEAR) {
		for (uint32 i = 0; i < length; i++) {
			if (!sPages[start + i].is_cleared) {
	 			clear_page(sPages[start + i].physical_page_number
	 				* B_PAGE_SIZE);
			}
		}
	}

	return firstPage;
}


vm_page *
vm_page_at_index(int32 index)
{
	return &sPages[index];
}


vm_page *
vm_lookup_page(addr_t pageNumber)
{
	if (pageNumber < sPhysicalPageOffset)
		return NULL;

	pageNumber -= sPhysicalPageOffset;
	if (pageNumber >= sNumPages)
		return NULL;

	return &sPages[pageNumber];
}


status_t
vm_page_set_state(vm_page *page, int pageState)
{
	InterruptsSpinLocker _(sPageLock);

	return set_page_state_nolock(page, pageState);
}


/*!	Moves a page to either the tail of the head of its current queue,
	depending on \a tail.
*/
void
vm_page_requeue(struct vm_page *page, bool tail)
{
	InterruptsSpinLocker _(sPageLock);
	page_queue *queue = NULL;

	switch (page->state) {
		case PAGE_STATE_BUSY:
		case PAGE_STATE_ACTIVE:
		case PAGE_STATE_INACTIVE:
		case PAGE_STATE_WIRED:
		case PAGE_STATE_UNUSED:
			queue = &sActivePageQueue;
			break;
		case PAGE_STATE_MODIFIED:
			queue = &sModifiedPageQueue;
			break;
		case PAGE_STATE_FREE:
			queue = &sFreePageQueue;
			break;
		case PAGE_STATE_CLEAR:
			queue = &sClearPageQueue;
			break;
		default:
			panic("vm_page_touch: vm_page %p in invalid state %d\n",
				page, page->state);
			break;
	}

	remove_page_from_queue(queue, page);

	if (tail)
		enqueue_page(queue, page);
	else
		enqueue_page_to_head(queue, page);
}


size_t
vm_page_num_pages(void)
{
	return sNumPages;
}


size_t
vm_page_num_free_pages(void)
{
	return sFreePageQueue.count + sClearPageQueue.count;
}


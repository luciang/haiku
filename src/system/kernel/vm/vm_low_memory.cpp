/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <KernelExport.h>
#include <signal.h>

#include <vm_low_memory.h>
#include <vm_page.h>
#include <lock.h>
#include <util/DoublyLinkedList.h>
#include <util/AutoLock.h>


static const bigtime_t kLowMemoryInterval = 2000000;	// 2 secs

static const size_t kNoteLimit = 1024;
static const size_t kWarnLimit = 256;
static const size_t kCriticalLimit = 32;


struct low_memory_handler : public DoublyLinkedListLinkImpl<low_memory_handler> {
	low_memory_func	function;
	void			*data;
	int32			priority;
};

typedef DoublyLinkedList<low_memory_handler> HandlerList;

static mutex sLowMemoryMutex;
static sem_id sLowMemoryWaitSem;
static HandlerList sLowMemoryHandlers;


static void
call_handlers(int32 level)
{
	MutexLocker locker(&sLowMemoryMutex);
	HandlerList::Iterator iterator = sLowMemoryHandlers.GetIterator();

	while (iterator.HasNext()) {
		low_memory_handler *handler = iterator.Next();

		handler->function(handler->data, level);
	}
}


static int32
low_memory(void *)
{
	while (true) {
		snooze(kLowMemoryInterval);

		uint32 freePages = vm_page_num_free_pages();
		if (freePages >= kNoteLimit)
			continue;

		// specify low memory level
		int32 level = B_LOW_MEMORY_NOTE;
		if (freePages < kCriticalLimit)
			level = B_LOW_MEMORY_CRITICAL;
		else if (freePages < kWarnLimit)
			level = B_LOW_MEMORY_WARNING;

		call_handlers(level);
	}
	return 0;
}


void
vm_low_memory(size_t requirements)
{
	// ToDo: compute level with requirements in mind

	call_handlers(B_LOW_MEMORY_NOTE);
}


status_t
vm_low_memory_init(void)
{
	thread_id thread;

	if (mutex_init(&sLowMemoryMutex, "low memory") < B_OK)
		return B_ERROR;
	
	sLowMemoryWaitSem = create_sem(0, "low memory wait");
	if (sLowMemoryWaitSem < B_OK)
		return sLowMemoryWaitSem;

	new(&sLowMemoryHandlers) HandlerList;
		// static initializers do not work in the kernel,
		// so we have to do it here, manually

	thread = spawn_kernel_thread(&low_memory, "low memory handler", B_LOW_PRIORITY, NULL);
	send_signal_etc(thread, SIGCONT, B_DO_NOT_RESCHEDULE);

	return B_OK;
}


status_t
unregister_low_memory_handler(low_memory_func function, void *data)
{
	MutexLocker locker(&sLowMemoryMutex);

	HandlerList::Iterator iterator = sLowMemoryHandlers.GetIterator();

	while (iterator.HasNext()) {
		low_memory_handler *handler = iterator.Next();

		if (handler->function == function && handler->data == data) {
			sLowMemoryHandlers.Remove(handler);
			free(handler);
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
register_low_memory_handler(low_memory_func function, void *data, int32 priority)
{
	low_memory_handler *handler = (low_memory_handler *)malloc(sizeof(low_memory_handler));
	if (handler == NULL)
		return B_NO_MEMORY;

	handler->function = function;
	handler->data = data;
	handler->priority = priority;

	MutexLocker locker(&sLowMemoryMutex);
	sLowMemoryHandlers.Add(handler);
	return B_OK;
}


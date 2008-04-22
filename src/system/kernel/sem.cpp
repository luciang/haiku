/*
 * Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*! Semaphore code */


#include <OS.h>

#include <sem.h>
#include <kernel.h>
#include <kscheduler.h>
#include <ksignal.h>
#include <smp.h>
#include <int.h>
#include <arch/int.h>
#include <debug.h>
#include <thread.h>
#include <team.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <vfs.h>
#include <vm_low_memory.h>
#include <vm_page.h>
#include <boot/kernel_args.h>
#include <syscall_restart.h>
#include <wait_for_objects.h>

#include <string.h>
#include <stdlib.h>


//#define TRACE_SEM
#ifdef TRACE_SEM
#	define TRACE(x) dprintf_no_syslog x
#else
#	define TRACE(x) ;
#endif

//#define KTRACE_SEM
#ifdef KTRACE_SEM
#	define KTRACE(x...) ktrace_printf(x)
#else
#	define KTRACE(x...) do {} while (false)
#endif

#define DEBUG_LAST_ACQUIRER

struct queued_thread : DoublyLinkedListLinkImpl<queued_thread> {
	queued_thread(struct thread *thread, int32 count)
		:
		thread(thread),
		count(count),
		queued(false)
	{
	}

	struct thread	*thread;
	int32			count;
	bool			queued;
};

typedef DoublyLinkedList<queued_thread> ThreadQueue;

struct sem_entry {
	sem_id		id;
	spinlock	lock;	// protects only the id field when unused
	union {
		// when slot in use
		struct {
			int32				count;
			int32				net_count;
									// count + acquisition count of all blocked
									// threads
			char				*name;
			team_id				owner;	// if set to -1, means owned by a port
			select_info			*select_infos;
			thread_id			last_acquirer;
#ifdef DEBUG_LAST_ACQUIRER
			int32				last_acquire_count;
			thread_id			last_releaser;
			int32				last_release_count;
#endif
		} used;

		// when slot unused
		struct {
			sem_id				next_id;
			struct sem_entry	*next;
		} unused;
	} u;

	ThreadQueue			queue;	// should be in u.used, but has a constructor
};

static const int32 kMaxSemaphores = 131072;
static int32 sMaxSems = 4096;
	// Final value is computed based on the amount of available memory
static int32 sUsedSems = 0;

static struct sem_entry *sSems = NULL;
static bool sSemsActive = false;
static struct sem_entry	*sFreeSemsHead = NULL;
static struct sem_entry	*sFreeSemsTail = NULL;

static spinlock sem_spinlock = 0;
#define GRAB_SEM_LIST_LOCK()     acquire_spinlock(&sem_spinlock)
#define RELEASE_SEM_LIST_LOCK()  release_spinlock(&sem_spinlock)
#define GRAB_SEM_LOCK(s)         acquire_spinlock(&(s).lock)
#define RELEASE_SEM_LOCK(s)      release_spinlock(&(s).lock)


static int
dump_sem_list(int argc, char **argv)
{
	const char *name = NULL;
	team_id owner = -1;
	thread_id last = -1;
	int32 i;

	if (argc > 2) {
		if (!strcmp(argv[1], "team") || !strcmp(argv[1], "owner"))
			owner = strtoul(argv[2], NULL, 0);
		else if (!strcmp(argv[1], "name"))
			name = argv[2];
		else if (!strcmp(argv[1], "last"))
			last = strtoul(argv[2], NULL, 0);
	} else if (argc > 1)
		owner = strtoul(argv[1], NULL, 0);

	kprintf("sem            id count   team   last  name\n");

	for (i = 0; i < sMaxSems; i++) {
		struct sem_entry *sem = &sSems[i];
		if (sem->id < 0
			|| (last != -1 && sem->u.used.last_acquirer != last)
			|| (name != NULL && strstr(sem->u.used.name, name) == NULL)
			|| (owner != -1 && sem->u.used.owner != owner))
			continue;

		kprintf("%p %6ld %5ld %6ld "
			"%6ld "
			" %s\n", sem, sem->id, sem->u.used.count,
			sem->u.used.owner,
			sem->u.used.last_acquirer > 0 ? sem->u.used.last_acquirer : 0,
			sem->u.used.name);
	}

	return 0;
}


static void
dump_sem(struct sem_entry *sem)
{
	kprintf("SEM: %p\n", sem);
	kprintf("id:      %ld (%#lx)\n", sem->id, sem->id);
	if (sem->id >= 0) {
		kprintf("name:    '%s'\n", sem->u.used.name);
		kprintf("owner:   %ld\n", sem->u.used.owner);
		kprintf("count:   %ld\n", sem->u.used.count);
		kprintf("queue:  ");
		if (!sem->queue.IsEmpty()) {
			ThreadQueue::Iterator it = sem->queue.GetIterator();
			while (queued_thread* entry = it.Next())
				kprintf(" %ld", entry->thread->id);
			kprintf("\n");
		} else
			kprintf(" -\n");

		set_debug_variable("_sem", (addr_t)sem);
		set_debug_variable("_semID", sem->id);
		set_debug_variable("_owner", sem->u.used.owner);

#ifdef DEBUG_LAST_ACQUIRER
		kprintf("last acquired by: %ld, count: %ld\n", sem->u.used.last_acquirer,
			sem->u.used.last_acquire_count);
		kprintf("last released by: %ld, count: %ld\n", sem->u.used.last_releaser,
			sem->u.used.last_release_count);

		if (sem->u.used.last_releaser != 0)
			set_debug_variable("_releaser", sem->u.used.last_releaser);
		else
			unset_debug_variable("_releaser");
#else
		kprintf("last acquired by: %ld\n", sem->u.used.last_acquirer);
#endif

		if (sem->u.used.last_acquirer != 0)
			set_debug_variable("_acquirer", sem->u.used.last_acquirer);
		else
			unset_debug_variable("_acquirer");

	} else {
		kprintf("next:    %p\n", sem->u.unused.next);
		kprintf("next_id: %ld\n", sem->u.unused.next_id);
	}
}


static int
dump_sem_info(int argc, char **argv)
{
	bool found = false;
	addr_t num;
	int32 i;

	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	num = strtoul(argv[1], NULL, 0);

	if (IS_KERNEL_ADDRESS(num)) {
		dump_sem((struct sem_entry *)num);
		return 0;
	} else if (num > 0) {
		uint32 slot = num % sMaxSems;
		if (sSems[slot].id != (int)num) {
			kprintf("sem %ld (%#lx) doesn't exist!\n", num, num);
			return 0;
		}

		dump_sem(&sSems[slot]);
		return 0;
	}

	// walk through the sem list, trying to match name
	for (i = 0; i < sMaxSems; i++) {
		if (sSems[i].u.used.name != NULL
			&& strcmp(argv[1], sSems[i].u.used.name) == 0) {
			dump_sem(&sSems[i]);
			found = true;
		}
	}

	if (!found)
		kprintf("sem \"%s\" doesn't exist!\n", argv[1]);
	return 0;
}


/*!	\brief Appends a semaphore slot to the free list.

	The semaphore list must be locked.
	The slot's id field is not changed. It should already be set to -1.

	\param slot The index of the semaphore slot.
	\param nextID The ID the slot will get when reused. If < 0 the \a slot
		   is used.
*/
static void
free_sem_slot(int slot, sem_id nextID)
{
	struct sem_entry *sem = sSems + slot;
	// set next_id to the next possible value; for sanity check the current ID
	if (nextID < 0)
		sem->u.unused.next_id = slot;
	else
		sem->u.unused.next_id = nextID;
	// append the entry to the list
	if (sFreeSemsTail)
		sFreeSemsTail->u.unused.next = sem;
	else
		sFreeSemsHead = sem;
	sFreeSemsTail = sem;
	sem->u.unused.next = NULL;
}


static inline void
notify_sem_select_events(struct sem_entry* sem, uint16 events)
{
	if (sem->u.used.select_infos)
		notify_select_events_list(sem->u.used.select_infos, events);
}


/*!	Fills the thread_info structure with information from the specified
	thread.
	The thread lock must be held when called.
*/
static void
fill_sem_info(struct sem_entry *sem, sem_info *info, size_t size)
{
	info->sem = sem->id;
	info->team = sem->u.used.owner;
	strlcpy(info->name, sem->u.used.name, sizeof(info->name));
	info->count = sem->u.used.count;
	info->latest_holder = sem->u.used.last_acquirer;
}


//	#pragma mark - Private Kernel API


status_t
sem_init(kernel_args *args)
{
	area_id area;
	int32 i;

	TRACE(("sem_init: entry\n"));

	// compute maximal number of semaphores depending on the available memory
	// 128 MB -> 16384 semaphores, 448 kB fixed array size
	// 256 MB -> 32768, 896 kB
	// 512 MB -> 65536, 1.75 MB
	// 1024 MB and more -> 131072, 3.5 MB
	i = vm_page_num_pages() / 2;
	while (sMaxSems < i && sMaxSems < kMaxSemaphores)
		sMaxSems <<= 1;

	// create and initialize semaphore table
	area = create_area("sem_table", (void **)&sSems, B_ANY_KERNEL_ADDRESS,
		sizeof(struct sem_entry) * sMaxSems, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < 0)
		panic("unable to allocate semaphore table!\n");

	memset(sSems, 0, sizeof(struct sem_entry) * sMaxSems);
	for (i = 0; i < sMaxSems; i++) {
		sSems[i].id = -1;
		free_sem_slot(i, i);
	}

	// add debugger commands
	add_debugger_command_etc("sems", &dump_sem_list,
		"Dump a list of all active semaphores (for team, with name, etc.)",
		"[ ([ \"team\" | \"owner\" ] <team>) | (\"name\" <name>) ]"
			" | (\"last\" <last acquirer>)\n"
		"Prints a list of all active semaphores meeting the given\n"
		"requirement. If no argument is given, all sems are listed.\n"
		"  <team>             - The team owning the semaphores.\n"
		"  <name>             - Part of the name of the semaphores.\n"
		"  <last acquirer>    - The thread that last acquired the semaphore.\n"
		, 0);
	add_debugger_command_etc("sem", &dump_sem_info,
		"Dump info about a particular semaphore",
		"<sem>\n"
		"Prints info about the specified semaphore.\n"
		"  <sem>  - pointer to the semaphore structure, semaphore ID, or name\n"
		"           of the semaphore to print info for.\n", 0);

	TRACE(("sem_init: exit\n"));

	sSemsActive = true;

	return 0;
}


/*!	Creates a semaphore with the given parameters.
	Note, the team_id is not checked, it must be correct, or else
	that semaphore might not be deleted.
	This function is only available from within the kernel, and
	should not be made public - if possible, we should remove it
	completely (and have only create_sem() exported).
*/
sem_id
create_sem_etc(int32 count, const char *name, team_id owner)
{
	struct sem_entry *sem = NULL;
	cpu_status state;
	sem_id id = B_NO_MORE_SEMS;
	char *tempName;
	size_t nameLength;

	if (sSemsActive == false)
		return B_NO_MORE_SEMS;

#if 0
	// TODO: the code below might cause unwanted deadlocks,
	// we need an asynchronously running low resource handler.
	if (sUsedSems == sMaxSems) {
		// The vnode cache may have collected lots of semaphores.
		// Freeing some unused vnodes should improve our situation.
		// TODO: maybe create a generic "low resources" handler, instead
		//	of only the specialised low memory thing?
		vfs_free_unused_vnodes(B_LOW_MEMORY_WARNING);
	}
	if (sUsedSems == sMaxSems) {
		// try again with more enthusiasm
		vfs_free_unused_vnodes(B_LOW_MEMORY_CRITICAL);
	}
#endif
	if (sUsedSems == sMaxSems)
		return B_NO_MORE_SEMS;

	if (name == NULL)
		name = "unnamed semaphore";

	nameLength = strlen(name) + 1;
	nameLength = min_c(nameLength, B_OS_NAME_LENGTH);
	tempName = (char *)malloc(nameLength);
	if (tempName == NULL)
		return B_NO_MEMORY;
	strlcpy(tempName, name, nameLength);

	state = disable_interrupts();
	GRAB_SEM_LIST_LOCK();

	// get the first slot from the free list
	sem = sFreeSemsHead;
	if (sem) {
		// remove it from the free list
		sFreeSemsHead = sem->u.unused.next;
		if (!sFreeSemsHead)
			sFreeSemsTail = NULL;

		// init the slot
		GRAB_SEM_LOCK(*sem);
		sem->id = sem->u.unused.next_id;
		sem->u.used.count = count;
		sem->u.used.net_count = count;
		new(&sem->queue) ThreadQueue;
		sem->u.used.name = tempName;
		sem->u.used.owner = owner;
		sem->u.used.select_infos = NULL;
		id = sem->id;
		RELEASE_SEM_LOCK(*sem);

		atomic_add(&sUsedSems, 1);

		KTRACE("create_sem_etc(count: %ld, name: %s, owner: %ld) -> %ld",
			count, name, owner, id);
	}

	RELEASE_SEM_LIST_LOCK();
	restore_interrupts(state);

	if (!sem)
		free(tempName);

	return id;
}


status_t
select_sem(int32 id, struct select_info* info, bool kernel)
{
	cpu_status state;
	int32 slot;
	status_t error = B_OK;

	if (id < 0)
		return B_BAD_SEM_ID;

	slot = id % sMaxSems;

	state = disable_interrupts();
	GRAB_SEM_LOCK(sSems[slot]);

	if (sSems[slot].id != id) {
		// bad sem ID
		error = B_BAD_SEM_ID;
	} else if (!kernel
		&& sSems[slot].u.used.owner == team_get_kernel_team_id()) {
		// kernel semaphore, but call from userland
		error = B_NOT_ALLOWED;
	} else {
		info->selected_events &= B_EVENT_ACQUIRE_SEMAPHORE | B_EVENT_INVALID;

		if (info->selected_events != 0) {
			info->next = sSems[slot].u.used.select_infos;
			sSems[slot].u.used.select_infos = info;

			if (sSems[slot].u.used.count > 0)
				notify_select_events(info, B_EVENT_ACQUIRE_SEMAPHORE);
		}
	}

	RELEASE_SEM_LOCK(sSems[slot]);
	restore_interrupts(state);

	return error;
}


status_t
deselect_sem(int32 id, struct select_info* info, bool kernel)
{
	cpu_status state;
	int32 slot;

	if (id < 0)
		return B_BAD_SEM_ID;

	if (info->selected_events == 0)
		return B_OK;

	slot = id % sMaxSems;

	state = disable_interrupts();
	GRAB_SEM_LOCK(sSems[slot]);

	if (sSems[slot].id == id) {
		select_info** infoLocation = &sSems[slot].u.used.select_infos;
		while (*infoLocation != NULL && *infoLocation != info)
			infoLocation = &(*infoLocation)->next;

		if (*infoLocation == info)
			*infoLocation = info->next;
	}

	RELEASE_SEM_LOCK(sSems[slot]);
	restore_interrupts(state);

	return B_OK;
}


/*!	Forcibly removes a thread from a semaphores wait queue. May have to wake up
	other threads in the process.
	Must be called with semaphore lock held. The thread lock must not be held.
*/
static void
remove_thread_from_sem(queued_thread *entry, struct sem_entry *sem)
{
	if (!entry->queued)
		return;

	sem->queue.Remove(entry);
	entry->queued = false;
	sem->u.used.count += entry->count;

	// We're done with this entry. We only have to check, if other threads
	// need unblocking, too.

	// Now see if more threads need to be woken up. We get the thread lock for
	// that time, so the blocking state of threads won't change. We need that
	// lock anyway when unblocking a thread.
	GRAB_THREAD_LOCK();

	while ((entry = sem->queue.Head()) != NULL) {
		if (thread_is_blocked(entry->thread)) {
			// The thread is still waiting. If its count is satisfied, unblock
			// it. Otherwise we can't unblock any other thread.
			if (entry->count > sem->u.used.net_count)
				break;

			thread_unblock_locked(entry->thread, B_OK);
			sem->u.used.net_count -= entry->count;
		} else {
			// The thread is no longer waiting, but still queued, which means
			// acquiration failed and we can just remove it.
			sem->u.used.count += entry->count;
		}

		sem->queue.Remove(entry);
		entry->queued = false;
	}

	RELEASE_THREAD_LOCK();

	// select notification, if the semaphore is now acquirable
	if (sem->u.used.count > 0)
		notify_sem_select_events(sem, B_EVENT_ACQUIRE_SEMAPHORE);
}


/*!	This function cycles through the sem table, deleting all the sems
	that are owned by the specified team.
*/
int
sem_delete_owned_sems(team_id owner)
{
	int state;
	int i;
	int count = 0;

	// ToDo: that looks horribly inefficient - maybe it would be better
	//	to have them in a list in the team

	if (owner < 0)
		return B_BAD_TEAM_ID;

	state = disable_interrupts();
	GRAB_SEM_LIST_LOCK();

	for (i = 0; i < sMaxSems; i++) {
		if (sSems[i].id != -1 && sSems[i].u.used.owner == owner) {
			sem_id id = sSems[i].id;

			RELEASE_SEM_LIST_LOCK();
			restore_interrupts(state);

			delete_sem(id);
			count++;

			state = disable_interrupts();
			GRAB_SEM_LIST_LOCK();
		}
	}

	RELEASE_SEM_LIST_LOCK();
	restore_interrupts(state);

	return count;
}


int32
sem_max_sems(void)
{
	return sMaxSems;
}


int32
sem_used_sems(void)
{
	return sUsedSems;
}


//	#pragma mark - Public Kernel API


sem_id
create_sem(int32 count, const char *name)
{
	return create_sem_etc(count, name, team_get_kernel_team_id());
}


status_t
delete_sem(sem_id id)
{
	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;

	int32 slot = id % sMaxSems;

	cpu_status state = disable_interrupts();
	GRAB_SEM_LOCK(sSems[slot]);

	if (sSems[slot].id != id) {
		RELEASE_SEM_LOCK(sSems[slot]);
		restore_interrupts(state);
		TRACE(("delete_sem: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	KTRACE("delete_sem(sem: %ld)", id);

	notify_sem_select_events(&sSems[slot], B_EVENT_INVALID);
	sSems[slot].u.used.select_infos = NULL;

	// free any threads waiting for this semaphore
	GRAB_THREAD_LOCK();
	while (queued_thread* entry = sSems[slot].queue.RemoveHead()) {
		entry->queued = false;
		thread_unblock_locked(entry->thread, B_BAD_SEM_ID);
	}
	RELEASE_THREAD_LOCK();

	sSems[slot].id = -1;
	char *name = sSems[slot].u.used.name;
	sSems[slot].u.used.name = NULL;

	RELEASE_SEM_LOCK(sSems[slot]);

	// append slot to the free list
	GRAB_SEM_LIST_LOCK();
	free_sem_slot(slot, id + sMaxSems);
	atomic_add(&sUsedSems, -1);
	RELEASE_SEM_LIST_LOCK();

	restore_interrupts(state);

	free(name);

	return B_OK;
}


status_t
acquire_sem(sem_id id)
{
	return switch_sem_etc(-1, id, 1, 0, 0);
}


status_t
acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
	return switch_sem_etc(-1, id, count, flags, timeout);
}


status_t
switch_sem(sem_id toBeReleased, sem_id toBeAcquired)
{
	return switch_sem_etc(toBeReleased, toBeAcquired, 1, 0, 0);
}


status_t
switch_sem_etc(sem_id semToBeReleased, sem_id id, int32 count,
	uint32 flags, bigtime_t timeout)
{
	int slot = id % sMaxSems;
	int state;
	status_t status = B_OK;

	if (kernel_startup)
		return B_OK;
	if (sSemsActive == false)
		return B_NO_MORE_SEMS;

	if (!are_interrupts_enabled()) {
		panic("switch_sem_etc: called with interrupts disabled for sem %ld\n",
			id);
	}

	if (id < 0)
		return B_BAD_SEM_ID;
	if (count <= 0
		|| (flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) == (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) {
		return B_BAD_VALUE;
	}

	state = disable_interrupts();
	GRAB_SEM_LOCK(sSems[slot]);

	if (sSems[slot].id != id) {
		TRACE(("switch_sem_etc: bad sem %ld\n", id));
		status = B_BAD_SEM_ID;
		goto err;
	}

	// ToDo: the B_CHECK_PERMISSION flag should be made private, as it
	//	doesn't have any use outside the kernel
	if ((flags & B_CHECK_PERMISSION) != 0
		&& sSems[slot].u.used.owner == team_get_kernel_team_id()) {
		dprintf("thread %ld tried to acquire kernel semaphore %ld.\n",
			thread_get_current_thread_id(), id);
		status = B_NOT_ALLOWED;
		goto err;
	}

	if (sSems[slot].u.used.count - count < 0) {
		if ((flags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0) {
			// immediate timeout
			status = B_WOULD_BLOCK;
			goto err;
		} else if ((flags & B_ABSOLUTE_TIMEOUT) != 0 && timeout < 0) {
			// absolute negative timeout
			status = B_TIMED_OUT;
			goto err;
		}
	}

	KTRACE("switch_sem_etc(semToBeReleased: %ld, sem: %ld, count: %ld, "
		"flags: 0x%lx, timeout: %lld)", semToBeReleased, id, count, flags,
		timeout);

	if ((sSems[slot].u.used.count -= count) < 0) {
		// we need to block
		struct thread *thread = thread_get_current_thread();

		TRACE(("switch_sem_etc(id = %ld): block name = %s, thread = %p,"
			" name = %s\n", id, sSems[slot].u.used.name, thread, thread->name));

		// do a quick check to see if the thread has any pending signals
		// this should catch most of the cases where the thread had a signal
		if (thread_is_interrupted(thread, flags)) {
			sSems[slot].u.used.count += count;
			status = B_INTERRUPTED;
				// the other semaphore will be released later
			goto err;
		}

		if ((flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) == 0)
			timeout = B_INFINITE_TIMEOUT;

		// enqueue in the semaphore queue and get ready to wait
		queued_thread queueEntry(thread, count);
		sSems[slot].queue.Add(&queueEntry);
		queueEntry.queued = true;

		thread_prepare_to_block(thread, flags, THREAD_BLOCK_TYPE_SEMAPHORE,
			(void*)(addr_t)id);

		RELEASE_SEM_LOCK(sSems[slot]);

		// release the other semaphore, if any
		if (semToBeReleased >= 0) {
			release_sem_etc(semToBeReleased, 1, B_DO_NOT_RESCHEDULE);
			semToBeReleased = -1;
		}

		GRAB_THREAD_LOCK();

		status_t acquireStatus = timeout == B_INFINITE_TIMEOUT
			? thread_block_locked(thread)
			: thread_block_with_timeout_locked(flags, timeout);

		RELEASE_THREAD_LOCK();
		GRAB_SEM_LOCK(sSems[slot]);

		// If we're still queued, this means the acquiration failed, and we
		// need to remove our entry and (potentially) wake up other threads.
		if (queueEntry.queued)
			remove_thread_from_sem(&queueEntry, &sSems[slot]);

		if (acquireStatus >= B_OK) {
			sSems[slot].u.used.last_acquirer = thread_get_current_thread_id();
#ifdef DEBUG_LAST_ACQUIRER
			sSems[slot].u.used.last_acquire_count = count;
#endif
		}

		RELEASE_SEM_LOCK(sSems[slot]);
		restore_interrupts(state);

		TRACE(("switch_sem_etc(sem %ld): exit block name %s, "
			"thread %ld (%s)\n", id, sSems[slot].u.used.name, thread->id,
			thread->name));
		KTRACE("switch_sem_etc() done: 0x%lx", acquireStatus);
		return acquireStatus;
	} else {
		sSems[slot].u.used.net_count -= count;
		sSems[slot].u.used.last_acquirer = thread_get_current_thread_id();
#ifdef DEBUG_LAST_ACQUIRER
		sSems[slot].u.used.last_acquire_count = count;
#endif
	}

err:
	RELEASE_SEM_LOCK(sSems[slot]);
	restore_interrupts(state);

	if (status == B_INTERRUPTED && semToBeReleased >= B_OK) {
		// depending on when we were interrupted, we need to still
		// release the semaphore to always leave in a consistent
		// state
		release_sem_etc(semToBeReleased, 1, B_DO_NOT_RESCHEDULE);
	}

#if 0
	if (status == B_NOT_ALLOWED)
	_user_debugger("Thread tried to acquire kernel semaphore.");
#endif

	KTRACE("switch_sem_etc() done: 0x%lx", status);

	return status;
}


status_t
release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


status_t
release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	int32 slot = id % sMaxSems;

	if (kernel_startup)
		return B_OK;
	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (count <= 0 && (flags & B_RELEASE_ALL) == 0)
		return B_BAD_VALUE;

	InterruptsLocker _;
	SpinLocker semLocker(sSems[slot].lock);

	if (sSems[slot].id != id) {
		TRACE(("sem_release_etc: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	// ToDo: the B_CHECK_PERMISSION flag should be made private, as it
	//	doesn't have any use outside the kernel
	if ((flags & B_CHECK_PERMISSION) != 0
		&& sSems[slot].u.used.owner == team_get_kernel_team_id()) {
		dprintf("thread %ld tried to release kernel semaphore.\n",
			thread_get_current_thread_id());
		return B_NOT_ALLOWED;
	}

	KTRACE("release_sem_etc(sem: %ld, count: %ld, flags: 0x%lx)", id, count,
		flags);

	sSems[slot].u.used.last_acquirer = -sSems[slot].u.used.last_acquirer;
#ifdef DEBUG_LAST_ACQUIRER
	sSems[slot].u.used.last_releaser = thread_get_current_thread_id();
	sSems[slot].u.used.last_release_count = count;
#endif

	if (flags & B_RELEASE_ALL) {
		count = sSems[slot].u.used.net_count - sSems[slot].u.used.count;

		// is there anything to do for us at all?
		if (count == 0)
			return B_OK;

		// Don't release more than necessary -- there might be interrupted/
		// timed out threads in the queue.
		flags |= B_RELEASE_IF_WAITING_ONLY;
	}

	SpinLocker threadLocker(thread_spinlock);

	while (count > 0) {
		queued_thread* entry = sSems[slot].queue.Head();
		if (entry == NULL) {
			if ((flags & B_RELEASE_IF_WAITING_ONLY) == 0) {
				sSems[slot].u.used.count += count;
				sSems[slot].u.used.net_count += count;
			}
			break;
		}

		if (thread_is_blocked(entry->thread)) {
			// The thread is still waiting. If its count is satisfied,
			// unblock it. Otherwise we can't unblock any other thread.
			if (entry->count > sSems[slot].u.used.net_count + count) {
				sSems[slot].u.used.count += count;
				sSems[slot].u.used.net_count += count;
				break;
			}

			thread_unblock_locked(entry->thread, B_OK);

			int delta = min_c(count, entry->count);
			sSems[slot].u.used.count += delta;
			sSems[slot].u.used.net_count += delta - entry->count;
			count -= delta;
		} else {
			// The thread is no longer waiting, but still queued, which
			// means acquiration failed and we can just remove it.
			sSems[slot].u.used.count += entry->count;
		}

		sSems[slot].queue.Remove(entry);
		entry->queued = false;
	}

	threadLocker.Unlock();

	if (sSems[slot].u.used.count > 0)
		notify_sem_select_events(&sSems[slot], B_EVENT_ACQUIRE_SEMAPHORE);

	// reschedule, if we've not explicitly been told not to
	if ((flags & B_DO_NOT_RESCHEDULE) == 0) {
		semLocker.Unlock();
		threadLocker.Lock();
		scheduler_reschedule();
	}

	return B_OK;
}


status_t
get_sem_count(sem_id id, int32 *_count)
{
	int slot;
	int state;

	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (_count == NULL)
		return B_BAD_VALUE;

	slot = id % sMaxSems;

	state = disable_interrupts();
	GRAB_SEM_LOCK(sSems[slot]);

	if (sSems[slot].id != id) {
		RELEASE_SEM_LOCK(sSems[slot]);
		restore_interrupts(state);
		TRACE(("sem_get_count: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	*_count = sSems[slot].u.used.count;

	RELEASE_SEM_LOCK(sSems[slot]);
	restore_interrupts(state);

	return B_OK;
}


/*!	Called by the get_sem_info() macro. */
status_t
_get_sem_info(sem_id id, struct sem_info *info, size_t size)
{
	status_t status = B_OK;
	int state;
	int slot;

	if (!sSemsActive)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;

	slot = id % sMaxSems;

	state = disable_interrupts();
	GRAB_SEM_LOCK(sSems[slot]);

	if (sSems[slot].id != id) {
		status = B_BAD_SEM_ID;
		TRACE(("get_sem_info: invalid sem_id %ld\n", id));
	} else
		fill_sem_info(&sSems[slot], info, size);

	RELEASE_SEM_LOCK(sSems[slot]);
	restore_interrupts(state);

	return status;
}


/*!	Called by the get_next_sem_info() macro. */
status_t
_get_next_sem_info(team_id team, int32 *_cookie, struct sem_info *info,
	size_t size)
{
	int state;
	int slot;
	bool found = false;

	if (!sSemsActive)
		return B_NO_MORE_SEMS;
	if (_cookie == NULL || info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;

	if (team == B_CURRENT_TEAM)
		team = team_get_current_team_id();
	/* prevents sSems[].owner == -1 >= means owned by a port */
	if (team < 0 || !team_is_valid(team))
		return B_BAD_TEAM_ID; 

	slot = *_cookie;
	if (slot >= sMaxSems)
		return B_BAD_VALUE;

	state = disable_interrupts();
	GRAB_SEM_LIST_LOCK();

	while (slot < sMaxSems) {
		if (sSems[slot].id != -1 && sSems[slot].u.used.owner == team) {
			GRAB_SEM_LOCK(sSems[slot]);
			if (sSems[slot].id != -1 && sSems[slot].u.used.owner == team) {
				// found one!
				fill_sem_info(&sSems[slot], info, size);

				RELEASE_SEM_LOCK(sSems[slot]);
				slot++;
				found = true;
				break;
			}
			RELEASE_SEM_LOCK(sSems[slot]);
		}
		slot++;
	}
	RELEASE_SEM_LIST_LOCK();
	restore_interrupts(state);

	if (!found)
		return B_BAD_VALUE;

	*_cookie = slot;
	return B_OK;
}


status_t
set_sem_owner(sem_id id, team_id team)
{
	int state;
	int slot;

	if (sSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (team < 0 || !team_is_valid(team))
		return B_BAD_TEAM_ID;

	slot = id % sMaxSems;

	state = disable_interrupts();
	GRAB_SEM_LOCK(sSems[slot]);

	if (sSems[slot].id != id) {
		RELEASE_SEM_LOCK(sSems[slot]);
		restore_interrupts(state);
		TRACE(("set_sem_owner: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	// ToDo: this is a small race condition: the team ID could already
	// be invalid at this point - we would lose one semaphore slot in
	// this case!
	// The only safe way to do this is to prevent either team (the new
	// or the old owner) from dying until we leave the spinlock.
	sSems[slot].u.used.owner = team;

	RELEASE_SEM_LOCK(sSems[slot]);
	restore_interrupts(state);

	return B_NO_ERROR;
}


//	#pragma mark - Syscalls


sem_id
_user_create_sem(int32 count, const char *userName)
{
	char name[B_OS_NAME_LENGTH];

	if (userName == NULL)
		return create_sem_etc(count, NULL, team_get_current_team_id());

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return create_sem_etc(count, name, team_get_current_team_id());
}


status_t
_user_delete_sem(sem_id id)
{
	return delete_sem(id);
}


status_t
_user_acquire_sem(sem_id id)
{
	status_t error = switch_sem_etc(-1, id, 1,
		B_CAN_INTERRUPT | B_CHECK_PERMISSION, 0);

	return syscall_restart_handle_post(error);
}


status_t
_user_acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
	syscall_restart_handle_timeout_pre(flags, timeout);

	status_t error = switch_sem_etc(-1, id, count,
		flags | B_CAN_INTERRUPT | B_CHECK_PERMISSION, timeout);

	return syscall_restart_handle_timeout_post(error, timeout);
}


status_t
_user_switch_sem(sem_id releaseSem, sem_id id)
{
	status_t error = switch_sem_etc(releaseSem, id, 1,
		B_CAN_INTERRUPT | B_CHECK_PERMISSION, 0);

	if (releaseSem < 0)
		return syscall_restart_handle_post(error);

	return error;
}


status_t
_user_switch_sem_etc(sem_id releaseSem, sem_id id, int32 count, uint32 flags,
	bigtime_t timeout)
{
	if (releaseSem < 0)
		syscall_restart_handle_timeout_pre(flags, timeout);

	status_t error = switch_sem_etc(releaseSem, id, count,
		flags | B_CAN_INTERRUPT | B_CHECK_PERMISSION, timeout);

	if (releaseSem < 0)
		return syscall_restart_handle_timeout_post(error, timeout);

	return error;
}


status_t
_user_release_sem(sem_id id)
{
	return release_sem_etc(id, 1, B_CHECK_PERMISSION);
}


status_t
_user_release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	return release_sem_etc(id, count, flags | B_CHECK_PERMISSION);
}


status_t
_user_get_sem_count(sem_id id, int32 *userCount)
{
	status_t status;
	int32 count;

	if (userCount == NULL || !IS_USER_ADDRESS(userCount))
		return B_BAD_ADDRESS;

	status = get_sem_count(id, &count);
	if (status == B_OK && user_memcpy(userCount, &count, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_get_sem_info(sem_id id, struct sem_info *userInfo, size_t size)
{
	struct sem_info info;
	status_t status;

	if (userInfo == NULL || !IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = _get_sem_info(id, &info, size);
	if (status == B_OK && user_memcpy(userInfo, &info, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_get_next_sem_info(team_id team, int32 *userCookie, struct sem_info *userInfo,
	size_t size)
{
	struct sem_info info;
	int32 cookie;
	status_t status;

	if (userCookie == NULL || userInfo == NULL
		|| !IS_USER_ADDRESS(userCookie) || !IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&cookie, userCookie, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	status = _get_next_sem_info(team, &cookie, &info, size);

	if (status == B_OK) {
		if (user_memcpy(userInfo, &info, size) < B_OK
			|| user_memcpy(userCookie, &cookie, sizeof(int32)) < B_OK)
			return B_BAD_ADDRESS;
	}

	return status;
}


status_t
_user_set_sem_owner(sem_id id, team_id team)
{
	return set_sem_owner(id, team);
}

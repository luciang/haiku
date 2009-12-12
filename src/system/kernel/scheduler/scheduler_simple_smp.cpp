/*
 * Copyright 2008-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2002, Angelo Mottola, a.mottola@libero.it.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


/*! The thread scheduler */


#include <OS.h>

#include <cpu.h>
#include <debug.h>
#include <int.h>
#include <kernel.h>
#include <kscheduler.h>
#include <listeners.h>
#include <scheduler_defs.h>
#include <smp.h>
#include <thread.h>
#include <timer.h>
#include <user_debugger.h>

#include "scheduler_tracing.h"


//#define TRACE_SCHEDULER
#ifdef TRACE_SCHEDULER
#	define TRACE(x) dprintf_no_syslog x
#else
#	define TRACE(x) ;
#endif


// The run queue. Holds the threads ready to run ordered by priority.
static struct thread *sRunQueue = NULL;
static int32 sCPUCount = 1;
static int32 sNextCPUForSelection = 0;


static int
_rand(void)
{
	static int next = 0;

	if (next == 0)
		next = system_time();

	next = next * 1103515245 + 12345;
	return (next >> 16) & 0x7FFF;
}


static int
dump_run_queue(int argc, char **argv)
{
	struct thread *thread;

	thread = sRunQueue;
	if (!thread)
		kprintf("Run queue is empty!\n");
	else {
		kprintf("thread    id      priority name\n");
		while (thread) {
			kprintf("%p  %-7ld %-8ld %s\n", thread, thread->id,
				thread->priority, thread->name);
			thread = thread->queue_next;
		}
	}

	return 0;
}


static int32
select_cpu(int32 currentCPU, struct thread* thread, int32& targetPriority)
{
	if (thread->pinned_to_cpu > 0) {
		// the thread is pinned to a specific CPU
		int32 targetCPU = thread->previous_cpu->cpu_num;
		targetPriority = gCPU[targetCPU].running_thread->priority;
		return targetCPU;
	}

	// Choose the CPU running the lowest priority thread. Favor the current CPU
	// as it doesn't require ICI to be notified.
	int32 targetCPU = currentCPU;
	targetPriority = B_IDLE_PRIORITY;
	if (gCPU[currentCPU].disabled)
		targetCPU = -1;
	else
		targetPriority = gCPU[currentCPU].running_thread->priority;

	int32 cpu = sNextCPUForSelection;
	for (int32 i = 0; i < sCPUCount; i++, cpu++) {
		if (cpu >= sCPUCount)
			cpu = 0;

		if (!gCPU[cpu].disabled) {
			int32 cpuPriority = gCPU[cpu].running_thread->priority;
			if (targetCPU < 0 || cpuPriority < targetPriority) {
				targetCPU = cpu;
				targetPriority = cpuPriority;
			}
		}
	}

	if (++sNextCPUForSelection >= sCPUCount)
		sNextCPUForSelection = 0;

	return targetCPU;
}


/*!	Enqueues the thread into the run queue.
	Note: thread lock must be held when entering this function
*/
static bool
enqueue_in_run_queue(struct thread *thread)
{
	if (thread->state == B_THREAD_RUNNING) {
		// The thread is currently running (on another CPU) and we cannot
		// insert it into the run queue. Set the next state to ready so the
		// thread is inserted into the run queue on the next reschedule.
		thread->next_state = B_THREAD_READY;
		return false;
	}

	thread->state = thread->next_state = B_THREAD_READY;

	struct thread *curr, *prev;
	for (curr = sRunQueue, prev = NULL; curr
			&& curr->priority >= thread->next_priority;
			curr = curr->queue_next) {
		if (prev)
			prev = prev->queue_next;
		else
			prev = sRunQueue;
	}

	T(EnqueueThread(thread, prev, curr));

	thread->queue_next = curr;
	if (prev)
		prev->queue_next = thread;
	else
		sRunQueue = thread;

	thread->next_priority = thread->priority;

	bool reschedule = false;
	if (thread->priority != B_IDLE_PRIORITY) {
		// Select a CPU for the thread to run on. It's not certain that the
		// thread will actually run on it, but we will notify the CPU to
		// preempt the thread it is currently running, if the new thread has
		// a higher priority.
		int32 currentCPU = smp_get_current_cpu();
		int32 targetPriority;
		int32 targetCPU = select_cpu(currentCPU, thread, targetPriority);

		// If the target CPU runs a thread with a lower priority, tell it to
		// reschedule.
		if (thread->priority > targetPriority) {
			if (targetCPU == currentCPU) {
				reschedule = true;
			} else {
				if (targetPriority == B_IDLE_PRIORITY) {
					smp_send_ici(targetCPU, SMP_MSG_RESCHEDULE_IF_IDLE, 0, 0,
						0, NULL, SMP_MSG_FLAG_ASYNC);
				} else {
					smp_send_ici(targetCPU, SMP_MSG_RESCHEDULE, 0, 0, 0, NULL,
						SMP_MSG_FLAG_ASYNC);
				}
			}
		}
	}

	// notify listeners
	NotifySchedulerListeners(&SchedulerListener::ThreadEnqueuedInRunQueue,
		thread);

	return reschedule;
}


/*!	Sets the priority of a thread.
	Note: thread lock must be held when entering this function
*/
static void
set_thread_priority(struct thread *thread, int32 priority)
{
	if (priority == thread->priority)
		return;

	if (thread->state != B_THREAD_READY) {
		thread->priority = priority;
		return;
	}

	// The thread is in the run queue. We need to remove it and re-insert it at
	// a new position.

	T(RemoveThread(thread));

	// notify listeners
	NotifySchedulerListeners(&SchedulerListener::ThreadRemovedFromRunQueue,
		thread);

	// find thread in run queue
	struct thread *item, *prev;
	for (item = sRunQueue, prev = NULL; item && item != thread;
			item = item->queue_next) {
		if (prev)
			prev = prev->queue_next;
		else
			prev = sRunQueue;
	}

	ASSERT(item == thread);

	// remove the thread
	if (prev)
		prev->queue_next = item->queue_next;
	else
		sRunQueue = item->queue_next;

	// set priority and re-insert
	thread->priority = thread->next_priority = priority;
	enqueue_in_run_queue(thread);
}


static void
context_switch(struct thread *fromThread, struct thread *toThread)
{
	if ((fromThread->flags & THREAD_FLAGS_DEBUGGER_INSTALLED) != 0)
		user_debug_thread_unscheduled(fromThread);

	cpu_ent* cpu = fromThread->cpu;
	toThread->previous_cpu = toThread->cpu = cpu;
	fromThread->cpu = NULL;
	cpu->running_thread = toThread;

	arch_thread_set_current_thread(toThread);
	arch_thread_context_switch(fromThread, toThread);

	// Looks weird, but is correct. fromThread had been unscheduled earlier,
	// but is back now. The notification for a thread scheduled the first time
	// happens in thread.cpp:thread_kthread_entry().
	if ((fromThread->flags & THREAD_FLAGS_DEBUGGER_INSTALLED) != 0)
		user_debug_thread_scheduled(fromThread);
}


static int32
reschedule_event(timer *unused)
{
	// this function is called as a result of the timer event set by the
	// scheduler returning this causes a reschedule on the timer event
	thread_get_current_thread()->cpu->invoke_scheduler = true;
	thread_get_current_thread()->cpu->invoke_scheduler_if_idle = false;
	thread_get_current_thread()->cpu->preempted = 1;
	return B_INVOKE_SCHEDULER;
}


/*!	Runs the scheduler.
	Note: expects thread spinlock to be held
*/
static void
reschedule(void)
{
	struct thread *oldThread = thread_get_current_thread();
	struct thread *nextThread, *prevThread;

	TRACE(("reschedule(): cpu %ld, cur_thread = %ld\n", smp_get_current_cpu(), thread_get_current_thread()->id));

	oldThread->cpu->invoke_scheduler = false;

	oldThread->state = oldThread->next_state;
	switch (oldThread->next_state) {
		case B_THREAD_RUNNING:
		case B_THREAD_READY:
			TRACE(("enqueueing thread %ld into run q. pri = %ld\n", oldThread->id, oldThread->priority));
			enqueue_in_run_queue(oldThread);
			break;
		case B_THREAD_SUSPENDED:
			TRACE(("reschedule(): suspending thread %ld\n", oldThread->id));
			break;
		case THREAD_STATE_FREE_ON_RESCHED:
			break;
		default:
			TRACE(("not enqueueing thread %ld into run q. next_state = %ld\n", oldThread->id, oldThread->next_state));
			break;
	}

	nextThread = sRunQueue;
	prevThread = NULL;

	if (oldThread->cpu->disabled) {
		// CPU is disabled - service any threads we may have that are pinned,
		// otherwise just select the idle thread
		while (nextThread && nextThread->priority > B_IDLE_PRIORITY) {
			if (nextThread->pinned_to_cpu > 0 &&
				nextThread->previous_cpu == oldThread->cpu)
					break;
			prevThread = nextThread;
			nextThread = nextThread->queue_next;
		}
	} else {
		while (nextThread) {
			// select next thread from the run queue
			while (nextThread && nextThread->priority > B_IDLE_PRIORITY) {
#if 0
				if (oldThread == nextThread && nextThread->was_yielded) {
					// ignore threads that called thread_yield() once
					nextThread->was_yielded = false;
					prevThread = nextThread;
					nextThread = nextThread->queue_next;
				}
#endif

				// skip thread, if it doesn't want to run on this CPU
				if (nextThread->pinned_to_cpu > 0
					&& nextThread->previous_cpu != oldThread->cpu) {
					prevThread = nextThread;
					nextThread = nextThread->queue_next;
					continue;
				}

				// always extract real time threads
				if (nextThread->priority >= B_FIRST_REAL_TIME_PRIORITY)
					break;

				// find next thread with lower priority
				struct thread *lowerNextThread = nextThread->queue_next;
				struct thread *lowerPrevThread = nextThread;
				int32 priority = nextThread->priority;

				while (lowerNextThread != NULL
					&& priority == lowerNextThread->priority) {
					lowerPrevThread = lowerNextThread;
					lowerNextThread = lowerNextThread->queue_next;
				}
				// never skip last non-idle normal thread
				if (lowerNextThread == NULL
					|| lowerNextThread->priority == B_IDLE_PRIORITY)
					break;

				int32 priorityDiff = priority - lowerNextThread->priority;
				if (priorityDiff > 15)
					break;

				// skip normal threads sometimes
				// (twice as probable per priority level)
				if ((_rand() >> (15 - priorityDiff)) != 0)
					break;

				nextThread = lowerNextThread;
				prevThread = lowerPrevThread;
			}

			if (nextThread->cpu
				&& nextThread->cpu->cpu_num != oldThread->cpu->cpu_num) {
				panic("thread in run queue that's still running on another CPU!\n");
				// ToDo: remove this check completely when we're sure that this
				// cannot happen anymore.
				prevThread = nextThread;
				nextThread = nextThread->queue_next;
				continue;
			}

			break;
		}
	}

	if (!nextThread)
		panic("reschedule(): run queue is empty!\n");

	// extract selected thread from the run queue
	if (prevThread)
		prevThread->queue_next = nextThread->queue_next;
	else
		sRunQueue = nextThread->queue_next;

	T(ScheduleThread(nextThread, oldThread));

	// notify listeners
	NotifySchedulerListeners(&SchedulerListener::ThreadScheduled,
		oldThread, nextThread);

	nextThread->state = B_THREAD_RUNNING;
	nextThread->next_state = B_THREAD_READY;
	oldThread->was_yielded = false;

	// track kernel time (user time is tracked in thread_at_kernel_entry())
	bigtime_t now = system_time();
	oldThread->kernel_time += now - oldThread->last_time;
	nextThread->last_time = now;

	// track CPU activity
	if (!thread_is_idle_thread(oldThread)) {
		oldThread->cpu->active_time +=
			(oldThread->kernel_time - oldThread->cpu->last_kernel_time)
			+ (oldThread->user_time - oldThread->cpu->last_user_time);
	}

	if (!thread_is_idle_thread(nextThread)) {
		oldThread->cpu->last_kernel_time = nextThread->kernel_time;
		oldThread->cpu->last_user_time = nextThread->user_time;
	}

	if (nextThread != oldThread || oldThread->cpu->preempted) {
		bigtime_t quantum = 3000;	// ToDo: calculate quantum!
		timer *quantumTimer = &oldThread->cpu->quantum_timer;

		if (!oldThread->cpu->preempted)
			cancel_timer(quantumTimer);

		oldThread->cpu->preempted = 0;
		add_timer(quantumTimer, &reschedule_event, quantum,
			B_ONE_SHOT_RELATIVE_TIMER | B_TIMER_ACQUIRE_THREAD_LOCK);

		if (nextThread != oldThread)
			context_switch(oldThread, nextThread);
	}
}


static void
on_thread_create(struct thread* thread)
{
	// do nothing
}


static void
on_thread_init(struct thread* thread)
{
	// do nothing
}


static void
on_thread_destroy(struct thread* thread)
{
	// do nothing
}


/*!	This starts the scheduler. Must be run in the context of the initial idle
	thread. Interrupts must be disabled and will be disabled when returning.
*/
static void
start(void)
{
	GRAB_THREAD_LOCK();

	reschedule();

	RELEASE_THREAD_LOCK();
}


static scheduler_ops kSimpleSMPOps = {
	enqueue_in_run_queue,
	reschedule,
	set_thread_priority,
	on_thread_create,
	on_thread_init,
	on_thread_destroy,
	start
};


// #pragma mark -


void
scheduler_simple_smp_init()
{
	sCPUCount = smp_get_num_cpus();

	gScheduler = &kSimpleSMPOps;

	add_debugger_command_etc("run_queue", &dump_run_queue,
		"List threads in run queue", "\nLists threads in run queue", 0);
}

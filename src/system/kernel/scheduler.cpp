/*
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2002, Angelo Mottola, a.mottola@libero.it.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*! The thread scheduler */


#include <OS.h>

#include <kscheduler.h>
#include <thread.h>
#include <timer.h>
#include <int.h>
#include <smp.h>
#include <cpu.h>
#include <debug.h>
#include <util/khash.h>


//#define TRACE_SCHEDULER
#ifdef TRACE_SCHEDULER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// The run queue. Holds the threads ready to run ordered by priority.
static struct thread *sRunQueue = NULL;


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


/*!	Enqueues the thread into the run queue.
	Note: thread lock must be held when entering this function
*/
void
scheduler_enqueue_in_run_queue(struct thread *thread)
{
	if (thread->state == B_THREAD_RUNNING) {
		// The thread is currently running (on another CPU) and we cannot
		// insert it into the run queue. Set the next state to ready so the
		// thread is inserted into the run queue on the next reschedule.
		thread->next_state = B_THREAD_READY;
		return;
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

	thread->queue_next = curr;
	if (prev)
		prev->queue_next = thread;
	else
		sRunQueue = thread;

	thread->next_priority = thread->priority;
}


/*!	Removes a thread from the run queue.
	Note: thread lock must be held when entering this function
*/
void
scheduler_remove_from_run_queue(struct thread *thread)
{
	struct thread *item, *prev;

	// find thread in run queue
	for (item = sRunQueue, prev = NULL; item && item != thread;
			item = item->queue_next) {
		if (prev)
			prev = prev->queue_next;
		else
			prev = sRunQueue;
	}

	ASSERT(item == thread);

	if (prev)
		prev->queue_next = item->queue_next;
	else
		sRunQueue = item->queue_next;
}


static void
context_switch(struct thread *fromThread, struct thread *toThread)
{
	toThread->cpu = fromThread->cpu;
	fromThread->cpu = NULL;

	arch_thread_set_current_thread(toThread);
	arch_thread_context_switch(fromThread, toThread);
}


static int32
reschedule_event(timer *unused)
{
	// this function is called as a result of the timer event set by the scheduler
	// returning this causes a reschedule on the timer event
	thread_get_current_thread()->cpu->preempted = 1;
	return B_INVOKE_SCHEDULER;
}


/*!	Runs the scheduler.
	Note: expects thread spinlock to be held
*/
void
scheduler_reschedule(void)
{
	struct thread *oldThread = thread_get_current_thread();
	struct thread *nextThread, *prevThread;

	TRACE(("reschedule(): cpu %d, cur_thread = %ld\n", smp_get_current_cpu(), thread_get_current_thread()->id));

	oldThread->state = oldThread->next_state;
	switch (oldThread->next_state) {
		case B_THREAD_RUNNING:
		case B_THREAD_READY:
			TRACE(("enqueueing thread %ld into run q. pri = %ld\n", oldThread->id, oldThread->priority));
			scheduler_enqueue_in_run_queue(oldThread);
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
		// CPU is disabled - just select an idle thread
		while (nextThread && nextThread->priority > B_IDLE_PRIORITY) {
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

				// always extract real time threads
				if (nextThread->priority >= B_FIRST_REAL_TIME_PRIORITY)
					break;

				// never skip last non-idle normal thread
				if (nextThread->queue_next && nextThread->queue_next->priority == B_IDLE_PRIORITY)
					break;

				// skip normal threads sometimes (roughly 20%)
				if (_rand() > 0x1a00)
					break;

				// skip until next lower priority
				int32 priority = nextThread->priority;
				do {
					prevThread = nextThread;
					nextThread = nextThread->queue_next;
				} while (nextThread->queue_next != NULL
					&& priority == nextThread->queue_next->priority
					&& nextThread->queue_next->priority > B_IDLE_PRIORITY);
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


void
scheduler_init(void)
{
	add_debugger_command("run_queue", &dump_run_queue, "list threads in run queue");
}


/*!	This starts the scheduler. Must be run under the context of
	the initial idle thread.
*/
void
scheduler_start(void)
{
	cpu_status state = disable_interrupts();
	GRAB_THREAD_LOCK();

	scheduler_reschedule();

	RELEASE_THREAD_LOCK();
	restore_interrupts(state);
}



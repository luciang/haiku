/*
 * Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <kscheduler.h>
#include <listeners.h>
#include <smp.h>

#include "scheduler_affine.h"
#include "scheduler_simple.h"
#include "scheduler_simple_smp.h"
#include "scheduler_tracing.h"


struct scheduler_ops* gScheduler;
SchedulerListenerList gSchedulerListeners;


SchedulerListener::~SchedulerListener()
{
}


/*!	Add the given scheduler listener. Thread lock must be held.
*/
void
scheduler_add_listener(struct SchedulerListener* listener)
{
	gSchedulerListeners.Add(listener);
}


/*!	Remove the given scheduler listener. Thread lock must be held.
*/
void
scheduler_remove_listener(struct SchedulerListener* listener)
{
	gSchedulerListeners.Remove(listener);
}


void
scheduler_init(void)
{
	int32 cpuCount = smp_get_num_cpus();
	dprintf("scheduler_init: found %ld logical cpu%s\n", cpuCount,
		cpuCount != 1 ? "s" : "");

	if (cpuCount > 1) {
#if 0
		dprintf("scheduler_init: using affine scheduler\n");
		scheduler_affine_init();
#else
		dprintf("scheduler_init: using simple SMP scheduler\n");
		scheduler_simple_smp_init();
#endif
	} else {
		dprintf("scheduler_init: using simple scheduler\n");
		scheduler_simple_init();
	}

#if SCHEDULER_TRACING
	add_debugger_command_etc("scheduler", &cmd_scheduler,
		"Analyze scheduler tracing information",
		"<thread>\n"
		"Analyzes scheduler tracing information for a given thread.\n"
		"  <thread>  - ID of the thread.\n", 0);
#endif
}


// #pragma mark - Syscalls


bigtime_t
_user_estimate_max_scheduling_latency(thread_id id)
{
	syscall_64_bit_return_value();

	InterruptsSpinLocker locker(gThreadSpinlock);

	struct thread* thread = id < 0
		? thread_get_current_thread() : thread_get_thread_struct_locked(id);
	if (thread == NULL)
		return 0;

	return gScheduler->estimate_max_scheduling_latency(thread);
}

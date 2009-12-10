/*
 * Copyright 2008-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include <kscheduler.h>
#include <listeners.h>
#include <smp.h>

#include "scheduler_affine.h"
#include "scheduler_simple.h"
#include "scheduler_simple_smp.h"


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

/*
 * Copyright 2002-2006, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/* This file contains the cpu functions (init, etc). */


#include <cpu.h>
#include <thread_types.h>
#include <arch/cpu.h>
#include <boot/kernel_args.h>

#include <string.h>


/* global per-cpu structure */
cpu_ent gCPU[MAX_BOOT_CPUS];

static spinlock sSetCpuLock;


status_t
cpu_init(kernel_args *args)
{
	int i;

	memset(gCPU, 0, sizeof(gCPU));
	for (i = 0; i < MAX_BOOT_CPUS; i++) {
		gCPU[i].cpu_num = i;
	}

	return arch_cpu_init(args);
}


status_t
cpu_init_post_vm(kernel_args *args)
{
	return arch_cpu_init_post_vm(args);
}


status_t
cpu_init_post_modules(kernel_args *args)
{
	return arch_cpu_init_post_modules(args);
}


status_t
cpu_preboot_init(kernel_args *args)
{
	return arch_cpu_preboot_init(args);
}


bigtime_t
cpu_get_active_time(int32 cpu)
{
	bigtime_t activeTime;
	cpu_status state;

	if (cpu < 0 || cpu > smp_get_num_cpus())
		return 0;

	// We need to grab the thread lock here, because the thread activity
	// time is not maintained atomically (because there is no need to)

	state = disable_interrupts();
	GRAB_THREAD_LOCK();

	activeTime = gCPU[cpu].active_time;

	RELEASE_THREAD_LOCK();
	restore_interrupts(state);

	return activeTime;
}


void
clear_caches(void *address, size_t length, uint32 flags)
{
	// ToDo: implement me!
}


//	#pragma mark -


void
_user_clear_caches(void *address, size_t length, uint32 flags)
{
	clear_caches(address, length, flags);
}


bool
_user_cpu_enabled(int32 cpu)
{
	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return B_BAD_VALUE;

	return !gCPU[cpu].disabled;
}


status_t
_user_set_cpu_enabled(int32 cpu, bool enabled)
{
	status_t status = B_OK;
	cpu_status state;
	int32 i, count;

	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return B_BAD_VALUE;

	// We need to lock here to make sure that no one can disable
	// the last CPU

	state = disable_interrupts();
	acquire_spinlock(&sSetCpuLock);

	if (!enabled) {
		// check if this is the last CPU to be disabled
		for (i = 0, count = 0; i < smp_get_num_cpus(); i++) {
			if (!gCPU[i].disabled)
				count++;
		}

		if (count == 1)
			status = B_NOT_ALLOWED;
	}

	if (status == B_OK)
		gCPU[cpu].disabled = !enabled;

	release_spinlock(&sSetCpuLock);
	restore_interrupts(state);
	return status;
}


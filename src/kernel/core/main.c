/* This is main - initializes processors and starts init */

/*
** Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

#include <stage2.h>
#include <Errors.h>
#include <kernel.h>
#include <console.h>
#include <debug.h>
#include <faults.h>
#include <arch/int.h>
#include <vm.h>
#include <timer.h>
#include <smp.h>
#include <OS.h>
#include <sem.h>
#include <port.h>
#include <vfs.h>
#include <dev.h>
#include <cbuf.h>
#include <elf.h>
#include <cpu.h>
#include <devs.h>
#include <bus.h>
#include <kmodule.h>
#include <int.h>

#include <string.h>

#include <arch/cpu.h>
#include <arch/faults.h>


#define TRACE_BOOT 1
#if TRACE_BOOT
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

bool kernel_startup;

static kernel_args ka;

static int main2(void *);

int _start(kernel_args *oldka, int cpu);	/* keep compiler happy */

int
_start(kernel_args *oldka, int cpu_num)
{
	kernel_startup = true;

	memcpy(&ka, oldka, sizeof(kernel_args));

	smp_set_num_cpus(ka.num_cpus);

	// do any pre-booting cpu config
	cpu_preboot_init(&ka);

	// if we're not a boot cpu, spin here until someone wakes us up
	if (smp_trap_non_boot_cpus(&ka, cpu_num) == B_NO_ERROR) {
		// we're the boot processor, so wait for all of the APs to enter the kernel
		smp_wait_for_ap_cpus(&ka);

		// setup debug output
		dbg_init(&ka);
		dbg_set_serial_debug(true);
		dprintf("Welcome to kernel debugger output!\n");

		// init modules
		cpu_init(&ka);
		int_init(&ka);

		vm_init(&ka);
		TRACE(("vm up\n"));

		// now we can use the heap and create areas
		dbg_init2(&ka);
		int_init2(&ka);

		faults_init(&ka);
		smp_init(&ka);
		timer_init(&ka);

		arch_cpu_init2(&ka);

		sem_init(&ka);

		dprintf("##################################################################\n");
		dprintf("semaphores now available\n");
		dprintf("##################################################################\n");

		// now we can create and use semaphores
		vm_init_postsem(&ka);
		cbuf_init();
		vfs_init(&ka);
		team_init(&ka);
		thread_init(&ka);
		port_init(&ka);

		vm_init_postthread(&ka);
		elf_init(&ka);

		// start a thread to finish initializing the rest of the system
		{
			thread_id tid;
			tid = thread_create_kernel_thread("main2", &main2, NULL);
			thread_resume_thread(tid);
		}

		smp_wake_up_all_non_boot_cpus();
		smp_enable_ici(); // ici's were previously being ignored
		start_scheduler();
	} else {
		// this is run per cpu for each AP processor after they've been set loose
		thread_init_percpu(cpu_num);
	}
	dprintf("##################################################################\n");
	dprintf("interrupts now enabled\n");
	dprintf("##################################################################\n");
	kernel_startup = false;
	enable_interrupts();

	TRACE(("main: done... begin idle loop on cpu %d\n", cpu_num));
	for(;;)
		arch_cpu_idle();

	return 0;
}


static int
main2(void *unused)
{
	(void)(unused);

	TRACE(("start of main2: initializing devices\n"));

	/* bootstrap all the filesystems */
	TRACE(("Bootstrap all filesystems\n"));
	vfs_bootstrap_all_filesystems();

	TRACE(("Init modules\n"));
	module_init(&ka, NULL);

	TRACE(("Init busses\n"));
	bus_init(&ka);

	TRACE(("Init devices\n"));
	dev_init(&ka);

	TRACE(("Init console\n"));
	con_init(&ka);

	//net_init_postdev(&ka);

#if 0
		// XXX remove
		vfs_test();
#endif
#if 0
		// XXX remove
		thread_test();
#endif
#if 0
		vm_test();
#endif
#if 0
	panic("debugger_test\n");
#endif
#if 0
	cbuf_test();
#endif
#if 0
	port_test();
#endif
	// start the init process
	{
		team_id pid;
		pid = team_create_team("/boot/bin/init", "init", NULL, 0, NULL, 0, 5);
		
		TRACE(("Init started\n"));
		if (pid < 0)
			kprintf("error starting 'init' error = %ld \n", pid);
	}

	return 0;
}


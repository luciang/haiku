/*
 * Copyright 2003-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <OS.h>
#include <boot/platform.h>
#include <boot/stage2.h>
#include <boot/heap.h>
#include <platform/openfirmware/openfirmware.h>
#include <platform_arch.h>

#include <string.h>

#include "console.h"
#include "machine.h"
#include "real_time_clock.h"


#define HEAP_SIZE 65536


extern "C" void _start(uint32 _unused1, uint32 _unused2,
	void *openFirmwareEntry);
extern "C" void start(void *openFirmwareEntry);

// GCC defined globals
extern void (*__ctor_list)(void);
extern void (*__ctor_end)(void);
extern uint8 __bss_start;
extern uint8 _end;

uint32 gMachine;


static void
call_ctors(void)
{ 
	void (**f)(void);

	for (f = &__ctor_list; f < &__ctor_end; f++) {		
		(**f)();
	}
}


static void
clear_bss(void)
{
	memset(&__bss_start, 0, &_end - &__bss_start);
}


static void 
determine_machine(void)
{
	gMachine = MACHINE_UNKNOWN;

	int root = of_finddevice("/");
	char buffer[64];
	int length;
	if ((length = of_getprop(root, "device_type", buffer, sizeof(buffer) - 1)) == OF_FAILED)
		return;
	buffer[length] = '\0';

	// ToDo: add more, and be as generic as possible

	if (!strcasecmp("chrp", buffer))
		gMachine = MACHINE_CHRP;
	else if (!strcasecmp("bootrom", buffer))
		gMachine = MACHINE_MAC;

	if ((length = of_getprop(root, "model", buffer, sizeof(buffer) - 1)) == OF_FAILED)
		return;
	buffer[length] = '\0';

	if (!strcasecmp("pegasos", buffer))
		gMachine |= MACHINE_PEGASOS;
}


extern "C" void
platform_start_kernel(void)
{
	addr_t kernelEntry = gKernelArgs.kernel_image.elf_header.e_entry;
	addr_t stackTop = gKernelArgs.cpu_kstack[0].start
		+ gKernelArgs.cpu_kstack[0].size;

	printf("kernel entry at %p\n", (void*)kernelEntry);
	printf("kernel stack top: %p\n", (void*)stackTop);

	/* TODO: ? 
	mmu_init_for_kernel();
	smp_boot_other_cpus();
	*/

	status_t error = arch_start_kernel(&gKernelArgs, kernelEntry, stackTop);

	panic("Kernel returned! Return value: %ld\n", error);
}


extern "C" void
platform_exit(void)
{
	of_interpret("reset-all", 0, 0);
}


extern "C" uint32
platform_boot_options(void)
{
	// ToDo: implement me!
	return 0;
}


extern "C" void
_start(uint32 _unused1, uint32 _unused3, void *openFirmwareEntry)
{
	// According to the PowerPC bindings, OpenFirmware should have created
	// a stack of 32kB or higher for us at this point

	clear_bss();
	call_ctors();
		// call C++ constructors before doing anything else

	start(openFirmwareEntry);
}


extern "C" void
start(void *openFirmwareEntry)
{
	char bootargs[512];

	// stage2 args - might be set via the command line one day
	stage2_args args;
	args.heap_size = HEAP_SIZE;
	args.arguments = NULL;

	of_init((int (*)(void*))openFirmwareEntry);

	// check for arguments
	if (of_getprop(gChosen, "bootargs", bootargs, sizeof(bootargs)) != OF_FAILED) {
		static const char *sArgs[] = { NULL, NULL };
		sArgs[0] = (const char *)bootargs;
		args.arguments = sArgs;
	}

	determine_machine();
	console_init();

	// Initialize and take over MMU and set the OpenFirmware callbacks - it 
	// will ask us for memory after that instead of maintaining it itself
	// (the kernel will need to adjust the callback later on as well)
	arch_mmu_init();

	if (boot_arch_cpu_init() != B_OK)
		of_exit();

	if (init_real_time_clock() != B_OK)
		of_exit();

	gKernelArgs.platform_args.openfirmware_entry = openFirmwareEntry;

	main(&args);
		// if everything goes fine, main() never returns

	of_exit();
}

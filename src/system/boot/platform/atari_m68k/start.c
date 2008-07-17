/*
 * Copyright 2003-2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <KernelExport.h>
#include <boot/platform.h>
#include <boot/heap.h>
#include <boot/stage2.h>
#include <arch/cpu.h>

#include <string.h>

#include "console.h"
#include "cpu.h"
#include "mmu.h"
#include "keyboard.h"
#include "toscalls.h"


#define HEAP_SIZE 65536

// GCC defined globals
extern void (*__ctor_list)(void);
extern void (*__ctor_end)(void);
extern uint8 __bss_start;
extern uint8 _end;

extern int main(stage2_args *args);
void _start(void);


uint32 sBootOptions;


static void
clear_bss(void)
{
	memset(&__bss_start, 0, &_end - &__bss_start);
}


static void
call_ctors(void)
{
	void (**f)(void);

	for (f = &__ctor_list; f < &__ctor_end; f++) {		
		(**f)();
	}
}


uint32
platform_boot_options(void)
{
#if 0
	if (!gKernelArgs.fb.enabled)
		sBootOptions |= check_for_boot_keys();
#endif
	return sBootOptions;
}


void
platform_start_kernel(void)
{
	static struct kernel_args *args = &gKernelArgs;
		// something goes wrong when we pass &gKernelArgs directly
		// to the assembler inline below - might be a bug in GCC
		// or I don't see something important...
	addr_t stackTop = gKernelArgs.cpu_kstack[0].start + gKernelArgs.cpu_kstack[0].size;

	//smp_init_other_cpus();
	//serial_cleanup();
	mmu_init_for_kernel();
	//smp_boot_other_cpus();

	dprintf("kernel entry at %lx\n", gKernelArgs.kernel_image.elf_header.e_entry);

	asm("move.l	%0, %%sp;	"			// move stack out of way
		: : "m" (stackTop));
	asm("move.l  #0x0,-(%%sp); "		// we're the BSP cpu (0)
		"move.l 	%0,-(%%sp);	"		// kernel args
		"move.l 	#0x0,-(%%sp);"		// dummy retval for call to main
		"move.l 	%1,-(%%sp);	"		// this is the start address
		"rts;		"					// jump.
		: : "g" (args), "g" (gKernelArgs.kernel_image.elf_header.e_entry));

	panic("kernel returned!\n");
}


void
platform_exit(void)
{
	// Terminate
	// XXX: Puntaes() instead ?
	Pterm0();
}


void
_start(void)
{
	stage2_args args;
	Bconout(DEV_CON, 'H');


	//asm("cld");			// Ain't nothing but a GCC thang.
	//asm("fninit");		// initialize floating point unit

	clear_bss();
	Bconout(DEV_CON, 'A');
	call_ctors();
		// call C++ constructors before doing anything else
	Bconout(DEV_CON, 'I');

	args.heap_size = HEAP_SIZE;
	
	// so we can dprintf
	init_nat_features();

	//serial_init();
	Bconout(DEV_CON, 'K');
	console_init();
	Bconout(DEV_CON, 'U');
	dprintf("membot  = %p\n", *TOSVAR_membot);
	dprintf("memtop  = %p\n", *TOSVAR_memtop);
	dprintf("v_bas_ad= %p\n", *TOSVAR_v_bas_ad);
	dprintf("phystop = %p\n", *TOSVARphystop);
	dprintf("ramtop  = %p\n", *TOSVARramtop);
	cpu_init();
	mmu_init();

	// wait a bit to give the user the opportunity to press a key
	spin(750000);

	// reading the keyboard doesn't seem to work in graphics mode (maybe a bochs problem)
	sBootOptions = check_for_boot_keys();
	//if (sBootOptions & BOOT_OPTION_DEBUG_OUTPUT)
		//serial_enable();

	//apm_init();
	//smp_init();
	main(&args);
}


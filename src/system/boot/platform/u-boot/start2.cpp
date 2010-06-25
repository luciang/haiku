/*
 * Copyright 2003-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "serial.h"
#include "console.h"
#include "cpu.h"
#include "mmu.h"
#include "smp.h"
#include "uimage.h"
#include "keyboard.h"

#include <KernelExport.h>
#include <boot/platform.h>
#include <boot/heap.h>
#include <boot/stage2.h>
#include <arch/cpu.h>

#include <string.h>


#define HEAP_SIZE (128 * 1024)


typedef struct uboot_arm_gd {
	struct board_data *bd;
	uint32 flags;
	uint32 baudrate;
	uint32 have_console;
	uint32 reloc_off;
	uint32 env_addr;
	uint32 env_valid;
	uint32 fb_base;
} uboot_arm_gd;


// GCC defined globals
extern void (*__ctor_list)(void);
extern void (*__ctor_end)(void);
extern uint8 __bss_start;
extern uint8 _end;

extern "C" int main(stage2_args *args);
extern "C" void _start(void);
extern "C" int start_raw(int argc, const char **argv);
extern "C" void dump_uimage(struct image_header *image);

extern struct image_header *gUImage;
extern uboot_arm_gd *gUBootGlobalData;
extern uint8 gUBootOS;


register volatile uboot_arm_gd *gGD asm ("r8");

static uint32 sBootOptions;


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


/* needed for libgcc unwind XXX */
extern "C" void
abort(void)
{
	panic("abort");
}


extern "C" void
platform_start_kernel(void)
{
	static struct kernel_args *args = &gKernelArgs;
		// something goes wrong when we pass &gKernelArgs directly
		// to the assembler inline below - might be a bug in GCC
		// or I don't see something important...
	addr_t stackTop
		= gKernelArgs.cpu_kstack[0].start + gKernelArgs.cpu_kstack[0].size;

//	smp_init_other_cpus();
	serial_cleanup();
	mmu_init_for_kernel();
//	smp_boot_other_cpus();

	dprintf("kernel entry at %lx\n",
		gKernelArgs.kernel_image.elf_header.e_entry);

	asm("MOV sp, %[adr]"::[adr] "r" (stackTop));
	asm("MOV r0, %[args]"::[args] "r" (args));
	asm("MOV r1, #0"::);
	asm("MOV pc, %[entry]"::[entry] "r"
		(gKernelArgs.kernel_image.elf_header.e_entry));

/*	asm("movl	%0, %%eax;	"			// move stack out of way
		"movl	%%eax, %%esp; "
		: : "m" (stackTop));
	asm("pushl  $0x0; "					// we're the BSP cpu (0)
		"pushl 	%0;	"					// kernel args
		"pushl 	$0x0;"					// dummy retval for call to main
		"pushl 	%1;	"					// this is the start address
		"ret;		"					// jump.
		: : "g" (args), "g" (gKernelArgs.kernel_image.elf_header.e_entry));
*/
	panic("kernel returned!\n");
}


extern "C" void
platform_exit(void)
{
}


extern "C" int
start_netbsd(struct board_info *bd, struct image_header *image,
	const char *consdev, const char *cmdline)
{
	const char *argv[] = { "haiku", cmdline };
	int argc = 1;
	if (cmdline)
		argc++;
	gUImage = image;
	return start_raw(argc, argv);
}


extern "C" int
start_linux(int argc, int archnum, void *atags)
{
	return 1;
}


extern "C" int
start_raw(int argc, const char **argv)
{
	stage2_args args;

	clear_bss();
		// call C++ constructors before doing anything else
	call_ctors();
	args.heap_size = HEAP_SIZE;
	args.arguments = NULL;
	args.platform.boot_tgz_data = NULL;
	args.platform.boot_tgz_size = 0;

	serial_init();
	console_init();
	cpu_init();

	// if we get passed a uimage, try to find the second blob
	if (gUImage != NULL
		&& image_multi_getimg(gUImage, 1, (uint32*)&args.platform.boot_tgz_data,
			&args.platform.boot_tgz_size)) {
		dprintf("Found boot tgz @ %p, %" B_PRIu32 " bytes\n",
			args.platform.boot_tgz_data, args.platform.boot_tgz_size);
	}

	{ //DEBUG:
		int i;
		dprintf("argc = %d\n", argc);
		for (i = 0; i < argc; i++)
			dprintf("argv[%d] @%lx = '%s'\n", i, (uint32)argv[i], argv[i]);
		dprintf("os: %d\n", gUBootOS);
		dprintf("gd @ %p\n", gGD);
		dprintf("gd->bd @ %p\n", gGD->bd);
		dprintf("fb_base %p\n", (void*)gGD->fb_base);
		dprintf("uimage @ %p\n", gUImage);
		if (gUImage)
			dump_uimage(gUImage);
	}
	
	mmu_init();

	// wait a bit to give the user the opportunity to press a key
//	spin(750000);

	// reading the keyboard doesn't seem to work in graphics mode
	// (maybe a bochs problem)
//	sBootOptions = check_for_boot_keys();
	//if (sBootOptions & BOOT_OPTION_DEBUG_OUTPUT)
		serial_enable();

	main(&args);
	return 0;
}


extern "C" uint32
platform_boot_options(void)
{
	return sBootOptions;
}

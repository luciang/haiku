/*
 * Copyright 2007, François Revol, revol@free.fr.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2003-2005, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#include <KernelExport.h>

#include <kernel.h>
#include <boot/kernel_args.h>

#include <vm.h>
#include <vm_types.h>
#include <arch/vm.h>
#include <arch_mmu.h>


//#define TRACE_ARCH_VM
#ifdef TRACE_ARCH_VM
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#warning M68K: WRITEME

status_t
arch_vm_init(kernel_args *args)
{
	return B_OK;
}


status_t
arch_vm_init2(kernel_args *args)
{
//	int bats[8];
//	int i;

	/**/
#warning M68K: disable TT0 and TT1, set up pmmu

	return B_OK;
}


status_t
arch_vm_init_post_area(kernel_args *args)
{
	return B_OK;
}


status_t
arch_vm_init_end(kernel_args *args)
{
#if 0
	TRACE(("arch_vm_init_end(): %lu virtual ranges to keep:\n",
		args->arch_args.num_virtual_ranges_to_keep));

	for (int i = 0; i < (int)args->arch_args.num_virtual_ranges_to_keep; i++) {
		addr_range &range = args->arch_args.virtual_ranges_to_keep[i];

		TRACE(("  start: %p, size: 0x%lx\n", (void*)range.start, range.size));

		// skip ranges outside the kernel address space
		if (!IS_KERNEL_ADDRESS(range.start)) {
			TRACE(("    no kernel address, skipping...\n"));
			continue;
		}

		void *address = (void*)range.start;
		area_id area = create_area("boot loader reserved area", &address,
			B_EXACT_ADDRESS, range.size, B_ALREADY_WIRED,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (area < 0) {
			panic("arch_vm_init_end(): Failed to create area for boot loader "
				"reserved area: %p - %p\n", (void*)range.start,
				(void*)(range.start + range.size));
		}
	}

	// Throw away any address space mappings we've inherited from the boot
	// loader and have not yet turned into an area.
	vm_free_unused_boot_loader_range(0, 0xffffffff - B_PAGE_SIZE + 1);
#endif

#warning M68K: unset TT0 now
	return B_OK;
}


status_t
arch_vm_init_post_modules(kernel_args *args)
{
	return B_OK;
}


void
arch_vm_aspace_swap(struct VMAddressSpace *from, struct VMAddressSpace *to)
{
	m68k_set_pgdir(m68k_translation_map_get_pgdir(&to->translation_map));
}


bool
arch_vm_supports_protection(uint32 protection)
{
	return true;
}


void
arch_vm_unset_memory_type(vm_area *area)
{
}


status_t
arch_vm_set_memory_type(vm_area *area, addr_t physicalBase, uint32 type)
{
	if (type == 0)
		return B_OK;

	return B_ERROR;
}

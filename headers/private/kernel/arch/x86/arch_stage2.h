/*
** Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#ifndef _KERNEL_ARCH_x86_STAGE2_H
#define _KERNEL_ARCH_x86_STAGE2_H

#include <stage2_struct.h>

#define MAX_BOOT_PTABLES 4

#define _PACKED __attribute__((packed))

#define IDT_LIMIT 0x800
#define GDT_LIMIT 0x800

// kernel args
typedef struct {
	// architecture specific
	uint32	system_time_cv_factor;
	uint32	phys_pgdir;
	uint32	vir_pgdir;
	uint32	num_pgtables;
	uint32	pgtables[MAX_BOOT_PTABLES];
	uint32	phys_idt;
	uint32	vir_idt;
	uint32	phys_gdt;
	uint32	vir_gdt;
	uint32	page_hole;
	// smp stuff
	uint32	apic_time_cv_factor; // apic ticks per second
	uint32	apic_phys;
	uint32	*apic;
	uint32	ioapic_phys;
	uint32	*ioapic;
	uint32	cpu_apic_id[MAX_BOOT_CPUS];
	uint32	cpu_os_id[MAX_BOOT_CPUS];
	uint32	cpu_apic_version[MAX_BOOT_CPUS];
} arch_kernel_args;

#endif	/* _KERNEL_ARCH_x86_STAGE2_H */

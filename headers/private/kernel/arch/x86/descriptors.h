/*
** Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#ifndef _KERNEL_ARCH_x86_DESCRIPTORS_H
#define _KERNEL_ARCH_x86_DESCRIPTORS_H


#define KERNEL_CODE_SEG	0x8
#define KERNEL_DATA_SEG	0x10

#define USER_CODE_SEG	0x1b
#define USER_DATA_SEG	0x23


#ifndef _ASSEMBLER
	// this file can also be included from assembler as well
	// (and is in arch_interrupts.S)

#define TSS_BASE_SEGMENT 5
#define TLS_BASE_SEGMENT (TSS_BASE_SEGMENT + smp_get_num_cpus())


// defines entries in the GDT/LDT

typedef struct segment_descriptor {
	uint16 limit_00_15;				// bit	 0 - 15
	uint16 base_00_15;				//		16 - 31
	uint32 base_23_16 : 8;			//		 0 -  7
	uint32 type : 4;				//		 8 - 11
	uint32 desc_type : 1;			//		12		(0 = system, 1 = code/data)
	uint32 privilege_level : 2;		//		13 - 14
	uint32 present : 1;				//		15
	uint32 limit_19_16 : 4;			//		16 - 19
	uint32 available : 1;			//		20
	uint32 zero : 1;				//		21
	uint32 d_b : 1;					//		22
	uint32 granularity : 1;			//		23
	uint32 base_31_24 : 8;			//		24 - 31
} segment_descriptor;

enum descriptor_privilege_levels {
	DPL_KERNEL = 0,
	DPL_USER = 3,
};

enum descriptor_types {
	// segment types
	DT_CODE_EXECUTE_ONLY = 0x8,
	DT_CODE_ACCESSED = 0x9,
	DT_CODE_READABLE = 0xa,
	DT_CODE_CONFORM = 0xc,
	DT_DATA_READ_ONLY = 0x0,
	DT_DATA_ACCESSED = 0x1,
	DT_DATA_WRITEABLE = 0x2,
	DT_DATA_EXPANSION_DOWN = 0x4,

	DT_TSS = 9,

	// descriptor types
	DT_SYSTEM_SEGMENT = 0,
	DT_CODE_DATA_SEGMENT = 1,
};

static inline void
clear_segment_descriptor(struct segment_descriptor *desc)
{
	*(long long *)desc = 0;
}


static inline void
set_segment_descriptor_base(struct segment_descriptor *desc, addr base)
{
	desc->base_00_15 = (addr)base & 0xffff;	// base is 32 bits long
	desc->base_23_16 = ((addr)base >> 16) & 0xff;
	desc->base_31_24 = ((addr)base >> 24) & 0xff;
}


static inline void
set_segment_descriptor(struct segment_descriptor *desc, addr base, uint32 limit,
	uint8 type, uint8 privilegeLevel)
{
	set_segment_descriptor_base(desc, base);

	desc->limit_00_15 = (addr)limit & 0x0ffff;	// limit is 20 bits long
	desc->limit_19_16 = ((addr)limit >> 16) & 0xf;

	desc->type = type;
	desc->desc_type = DT_CODE_DATA_SEGMENT;
	desc->privilege_level = privilegeLevel;

	desc->present = 1;
	desc->granularity = 1;	// 4 GB size (in page size steps)
	desc->available = 0;	// system available bit is currently not used
	desc->d_b = 1;

	desc->zero = 0;
}


static inline void
set_tss_descriptor(struct segment_descriptor *desc, addr base, uint32 limit)
{
	// the TSS descriptor has a special layout different from the standard descriptor
	set_segment_descriptor_base(desc, base);

	desc->limit_00_15 = (addr)limit & 0x0ffff;
	desc->limit_19_16 = 0;

	desc->type = DT_TSS;
	desc->desc_type = DT_SYSTEM_SEGMENT;
	desc->privilege_level = DPL_KERNEL;

	desc->present = 1;
	desc->granularity = 1;	// 4 GB size (in page size steps)
	desc->available = 0;	// system available bit is currently not used
	desc->d_b = 0;

	desc->zero = 0;
}

#endif	/* _ASSEMBLER */

#endif	/* _KERNEL_ARCH_x86_DESCRIPTORS_H */

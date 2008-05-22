/*
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef _KERNEL_ARCH_x86_CPU_H
#define _KERNEL_ARCH_x86_CPU_H


#ifndef _ASSEMBLER

#include <module.h>
#include <arch/x86/descriptors.h>

#endif	// !_ASSEMBLER


// MSR registers (possibly Intel specific)
#define IA32_MSR_APIC_BASE				0x1b

#define IA32_MSR_MTRR_CAPABILITIES		0xfe
#define IA32_MSR_SYSENTER_CS			0x174
#define IA32_MSR_SYSENTER_ESP			0x175
#define IA32_MSR_SYSENTER_EIP			0x176
#define IA32_MSR_MTRR_DEFAULT_TYPE		0x2ff
#define IA32_MSR_MTRR_PHYSICAL_BASE_0	0x200
#define IA32_MSR_MTRR_PHYSICAL_MASK_0	0x201

// x86 features from cpuid eax 1, edx register
#define IA32_FEATURE_FPU   0x00000001 // x87 fpu
#define IA32_FEATURE_VME   0x00000002 // virtual 8086
#define IA32_FEATURE_DE    0x00000004 // debugging extensions
#define IA32_FEATURE_PSE   0x00000008 // page size extensions
#define IA32_FEATURE_TSC   0x00000010 // rdtsc instruction
#define IA32_FEATURE_MSR   0x00000020 // rdmsr/wrmsr instruction
#define IA32_FEATURE_PAE   0x00000040 // extended 3 level page table addressing
#define IA32_FEATURE_MCE   0x00000080 // machine check exception
#define IA32_FEATURE_CX8   0x00000100 // cmpxchg8b instruction
#define IA32_FEATURE_APIC  0x00000200 // local apic on chip
#define IA32_FEATURE_SEP   0x00000800 // SYSENTER/SYSEXIT
#define IA32_FEATURE_MTRR  0x00001000 // MTRR
#define IA32_FEATURE_PGE   0x00002000 // paging global bit
#define IA32_FEATURE_MCA   0x00004000 // machine check architecture
#define IA32_FEATURE_CMOV  0x00008000 // cmov instruction
#define IA32_FEATURE_PAT   0x00010000 // page attribute table
#define IA32_FEATURE_PSE36 0x00020000 // page size extensions with 4MB pages
#define IA32_FEATURE_PSN   0x00040000 // processor serial number
#define IA32_FEATURE_CLFSH 0x00080000 // cflush instruction
#define IA32_FEATURE_DS    0x00200000 // debug store
#define IA32_FEATURE_ACPI  0x00400000 // thermal monitor and clock ctrl
#define IA32_FEATURE_MMX   0x00800000 // mmx instructions
#define IA32_FEATURE_FXSR  0x01000000 // FXSAVE/FXRSTOR instruction
#define IA32_FEATURE_SSE   0x02000000 // SSE
#define IA32_FEATURE_SSE2  0x04000000 // SSE2
#define IA32_FEATURE_SS    0x08000000 // self snoop
#define IA32_FEATURE_HTT   0x10000000 // hyperthreading
#define IA32_FEATURE_TM    0x20000000 // thermal monitor
#define IA32_FEATURE_PBE   0x80000000 // pending break enable

// x86 features from cpuid eax 1, ecx register
#define IA32_FEATURE_EXT_SSE3  0x00000001 // SSE3
#define IA32_FEATURE_EXT_MONITOR 0x00000008 // MONITOR/MWAIT
#define IA32_FEATURE_EXT_DSCPL 0x00000010 // CPL qualified debug store
#define IA32_FEATURE_EXT_EST   0x00000080 // speedstep
#define IA32_FEATURE_EXT_TM2   0x00000100 // thermal monitor 2
#define IA32_FEATURE_EXT_CNXTID 0x00000400 // L1 context ID

// x86 features from cpuid eax 0x80000001, edx register (AMD)
// only care about the ones that are unique to this register
#define IA32_FEATURE_AMD_EXT_SYSCALL (1<<11) 	// SYSCALL/SYSRET
#define IA32_FEATURE_AMD_EXT_NX      (1<<20)    // no execute bit
#define IA32_FEATURE_AMD_EXT_MMXEXT  (1<<22)    // mmx extensions
#define IA32_FEATURE_AMD_EXT_FFXSR   (1<<25)    // fast FXSAVE/FXRSTOR
#define IA32_FEATURE_AMD_EXT_RDTSCP  (1<<27)    // rdtscp instruction
#define IA32_FEATURE_AMD_EXT_LONG    (1<<29)    // long mode
#define IA32_FEATURE_AMD_EXT_3DNOWEXT (1<<30)   // 3DNow! extensions
#define IA32_FEATURE_AMD_EXT_3DNOW   (1<<31)   	// 3DNow!

// cr4 flags
#define IA32_CR4_GLOBAL_PAGES		(1UL << 7)

// Memory type ranges
#define IA32_MTR_UNCACHED			0
#define IA32_MTR_WRITE_COMBINING	1
#define IA32_MTR_WRITE_THROUGH		4
#define IA32_MTR_WRITE_PROTECTED	5
#define IA32_MTR_WRITE_BACK			6


// iframe types
#define IFRAME_TYPE_SYSCALL	0x1
#define IFRAME_TYPE_OTHER	0x2
#define IFRAME_TYPE_MASK	0xf


#ifndef _ASSEMBLER

typedef struct x86_optimized_functions {
	void 	(*memcpy)(void* dest, const void* source, size_t count);
	void*	memcpy_end;
} x86_optimized_functions;

typedef struct x86_cpu_module_info {
	module_info	info;
	uint32		(*count_mtrrs)(void);
	void		(*init_mtrrs)(void);

	void		(*set_mtrr)(uint32 index, uint64 base, uint64 length, uint8 type);
	status_t	(*get_mtrr)(uint32 index, uint64 *_base, uint64 *_length,
					uint8 *_type);

	void		(*get_optimized_functions)(x86_optimized_functions* functions);
} x86_cpu_module_info;


struct tss {
	uint16 prev_task;
	uint16 unused0;
	uint32 sp0;
	uint32 ss0;
	uint32 sp1;
	uint32 ss1;
	uint32 sp2;
	uint32 ss2;
	uint32 cr3;
	uint32 eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
	uint32 es, cs, ss, ds, fs, gs;
	uint32 ldt_seg_selector;
	uint16 unused1;
	uint16 io_map_base;
};

struct iframe {
	uint32 type;	// iframe type
	uint32 gs;
	uint32 fs;
	uint32 es;
	uint32 ds;
	uint32 edi;
	uint32 esi;
	uint32 ebp;
	uint32 esp;
	uint32 ebx;
	uint32 edx;
	uint32 ecx;
	uint32 eax;
	uint32 orig_eax;
	uint32 orig_edx;
	uint32 vector;
	uint32 error_code;
	uint32 eip;
	uint32 cs;
	uint32 flags;
	uint32 user_esp;
	uint32 user_ss;
};

struct vm86_iframe {
	uint32 type;	// iframe type
	uint32 __null_gs;
	uint32 __null_fs;
	uint32 __null_es;
	uint32 __null_ds;
	uint32 edi;
	uint32 esi;
	uint32 ebp;
	uint32 __kern_esp;
	uint32 ebx;
	uint32 edx;
	uint32 ecx;
	uint32 eax;
	uint32 orig_eax;
	uint32 orig_edx;
	uint32 vector;
	uint32 error_code;
	uint32 eip;
	uint16 cs, __csh;
	uint32 flags;
	uint32 esp;
	uint16 ss, __ssh;

	/* vm86 mode specific part */
	uint16 es, __esh;
	uint16 ds, __dsh;
	uint16 fs, __fsh;
	uint16 gs, __gsh;
};

#define IFRAME_IS_USER(f) ( ((f)->cs == USER_CODE_SEG) \
                            || (((f)->flags & 0x20000) != 0 ))
#define IFRAME_IS_VM86(f) ( ((f)->flags & 0x20000) != 0 )

// features
enum x86_feature_type {
	FEATURE_COMMON = 0,     // cpuid eax=1, ecx register
	FEATURE_EXT,            // cpuid eax=1, edx register
	FEATURE_EXT_AMD,        // cpuid eax=0x80000001, edx register (AMD)

	FEATURE_NUM
};

enum x86_vendors {
	VENDOR_INTEL = 0,
	VENDOR_AMD,
	VENDOR_CYRIX,
	VENDOR_UMC,
	VENDOR_NEXGEN,
	VENDOR_CENTAUR,
	VENDOR_RISE,
	VENDOR_TRANSMETA,
	VENDOR_NSC,

	VENDOR_NUM,
	VENDOR_UNKNOWN,
};

typedef struct arch_cpu_info {
	// saved cpu info
	enum x86_vendors vendor;
	enum x86_feature_type feature[FEATURE_NUM];
	char model_name[49];
	const char *vendor_name;
	int type;
	int family;
	int extended_family;
	int stepping;
	int model;
	int extended_model;
	char feature_string[256];

	// local TSS for this cpu
	struct tss tss;
	struct tss double_fault_tss;
} arch_cpu_info;

#ifdef __cplusplus
extern "C" {
#endif

#define nop() __asm__ ("nop"::)

struct arch_thread;

void __x86_setup_system_time(uint32 cv_factor);
void i386_context_switch(struct arch_thread *old_state, struct arch_thread *new_state, addr_t new_pgdir);
void x86_userspace_thread_exit(void);
void x86_end_userspace_thread_exit(void);
void x86_enter_userspace(addr_t entry, addr_t stackTop);
void i386_set_tss_and_kstack(addr_t kstack);
void i386_switch_stack_and_call(addr_t stack, void (*func)(void *), void *arg);
void i386_swap_pgdir(addr_t new_pgdir);
void i386_fnsave(void *fpu_state);
void i386_fxsave(void *fpu_state);
void i386_frstor(const void *fpu_state);
void i386_fxrstor(const void *fpu_state);
void i386_fnsave_swap(void *old_fpu_state, const void *new_fpu_state);
void i386_fxsave_swap(void *old_fpu_state, const void *new_fpu_state);
uint32 x86_read_ebp();
uint32 x86_read_cr0();
void x86_write_cr0(uint32 value);
uint32 x86_read_cr4();
void x86_write_cr4(uint32 value);
uint64 x86_read_msr(uint32 registerNumber);
void x86_write_msr(uint32 registerNumber, uint64 value);
void x86_set_task_gate(int32 n, int32 segment);
uint32 x86_count_mtrrs(void);
void x86_set_mtrr(uint32 index, uint64 base, uint64 length, uint8 type);
status_t x86_get_mtrr(uint32 index, uint64 *_base, uint64 *_length, uint8 *_type);
bool x86_check_feature(uint32 feature, enum x86_feature_type type);


#define read_cr3(value) \
	__asm__("movl	%%cr3,%0" : "=r" (value))

#define write_cr3(value) \
	__asm__("movl	%0,%%cr3" : : "r" (value))

#define read_dr3(value) \
	__asm__("movl	%%dr3,%0" : "=r" (value))

#define write_dr3(value) \
	__asm__("movl	%0,%%dr3" : : "r" (value))

#define invalidate_TLB(va) \
	__asm__("invlpg (%0)" : : "r" (va))

#define wbinvd() \
	__asm__("wbinvd")

#define out8(value,port) \
	__asm__ ("outb %%al,%%dx" : : "a" (value), "d" (port))

#define out16(value,port) \
	__asm__ ("outw %%ax,%%dx" : : "a" (value), "d" (port))

#define out32(value,port) \
	__asm__ ("outl %%eax,%%dx" : : "a" (value), "d" (port))

#define in8(port) ({ \
	uint8 _v; \
	__asm__ volatile ("inb %%dx,%%al" : "=a" (_v) : "d" (port)); \
	_v; \
})

#define in16(port) ({ \
	uint16 _v; \
	__asm__ volatile ("inw %%dx,%%ax":"=a" (_v) : "d" (port)); \
	_v; \
})

#define in32(port) ({ \
	uint32 _v; \
	__asm__ volatile ("inl %%dx,%%eax":"=a" (_v) : "d" (port)); \
	_v; \
})

#define out8_p(value,port) \
	__asm__ ("outb %%al,%%dx\n" \
		"\tjmp 1f\n" \
		"1:\tjmp 1f\n" \
		"1:" : : "a" (value), "d" (port))

#define in8_p(port) ({ \
	uint8 _v; \
	__asm__ volatile ("inb %%dx,%%al\n" \
		"\tjmp 1f\n" \
		"1:\tjmp 1f\n" \
		"1:" : "=a" (_v) : "d" (port)); \
	_v; \
})

extern segment_descriptor *gGDT;


#ifdef __cplusplus
}	// extern "C" {
#endif

#endif	// !_ASSEMBLER

#endif	/* _KERNEL_ARCH_x86_CPU_H */

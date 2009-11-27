/*
 * Copyright 2007-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

// This file is used to get C structure offsets into assembly code.
// The build system assembles the file and processes the output to create
// a header file with macro definitions, that can be included from assembly
// code.


#include <computed_asm_macros.h>

#include <arch_cpu.h>
#include <cpu.h>
#include <ksyscalls.h>
#include <thread_types.h>


#define DEFINE_MACRO(macro, value) DEFINE_COMPUTED_ASM_MACRO(macro, value)

#define DEFINE_OFFSET_MACRO(prefix, structure, member) \
    DEFINE_MACRO(prefix##_##member, offsetof(struct structure, member));

#define DEFINE_SIZEOF_MACRO(prefix, structure) \
    DEFINE_MACRO(prefix##_sizeof, sizeof(struct structure));


void
dummy()
{
	// struct cpu_ent
	DEFINE_OFFSET_MACRO(CPU_ENT, cpu_ent, fault_handler);
	DEFINE_OFFSET_MACRO(CPU_ENT, cpu_ent, fault_handler_stack_pointer);

	// struct thread
	DEFINE_OFFSET_MACRO(THREAD, thread, kernel_time);
	DEFINE_OFFSET_MACRO(THREAD, thread, user_time);
	DEFINE_OFFSET_MACRO(THREAD, thread, last_time);
	DEFINE_OFFSET_MACRO(THREAD, thread, in_kernel);
	DEFINE_OFFSET_MACRO(THREAD, thread, flags);
	DEFINE_OFFSET_MACRO(THREAD, thread, kernel_stack_top);
	DEFINE_OFFSET_MACRO(THREAD, thread, fault_handler);

	// struct iframe
	DEFINE_OFFSET_MACRO(IFRAME, iframe, cs);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, eax);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, edx);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, orig_eax);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, vector);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, eip);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, flags);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, user_esp);

	// struct vm86_iframe
	DEFINE_SIZEOF_MACRO(VM86_IFRAME, vm86_iframe);
	DEFINE_OFFSET_MACRO(VM86_IFRAME, vm86_iframe, flags);

	// struct syscall_info
	DEFINE_SIZEOF_MACRO(SYSCALL_INFO, syscall_info);
	DEFINE_OFFSET_MACRO(SYSCALL_INFO, syscall_info, function);
	DEFINE_OFFSET_MACRO(SYSCALL_INFO, syscall_info, parameter_size);

	// struct x86_optimized_functions
	DEFINE_OFFSET_MACRO(X86_OPTIMIZED_FUNCTIONS, x86_optimized_functions,
		memcpy);
	DEFINE_OFFSET_MACRO(X86_OPTIMIZED_FUNCTIONS, x86_optimized_functions,
		memset);
}

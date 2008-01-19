/*
 * Copyright 2002-2005, The Haiku Team. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef _KERNEL_ARCH_x86_THREAD_H
#define _KERNEL_ARCH_x86_THREAD_H


#include <arch/cpu.h>


#ifdef __cplusplus
extern "C" {
#endif

struct iframe *i386_get_user_iframe(void);
void *x86_next_page_directory(struct thread *from, struct thread *to);

void i386_return_from_signal();
void i386_end_return_from_signal();

// override empty macro
#undef arch_syscall_64_bit_return_value
void arch_syscall_64_bit_return_value(void);


static
inline struct thread *
arch_thread_get_current_thread(void)
{
	struct thread *t;
	read_dr3(t);
	return t;
}

static inline void
arch_thread_set_current_thread(struct thread *t)
{
	write_dr3(t);
}

#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_ARCH_x86_THREAD_H */


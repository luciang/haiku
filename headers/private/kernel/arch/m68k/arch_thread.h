/*
 * Copyright 2003-2006, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Axel Dörfler <axeld@pinc-software.de>
 * 		Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */
#ifndef _KERNEL_ARCH_M68K_THREAD_H
#define _KERNEL_ARCH_M68K_THREAD_H

#include <arch/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

void m68k_push_iframe(struct iframe_stack *stack, struct iframe *frame);
void m68k_pop_iframe(struct iframe_stack *stack);
struct iframe *m68k_get_user_iframe(void);

/* as we won't support SMP on m68k (yet?) we can use a global here */
extern struct thread *gCurrentThread;

extern inline struct thread *
arch_thread_get_current_thread(void)
{
	return gCurrentThread;
}


extern inline void
arch_thread_set_current_thread(struct thread *t)
{
	gCurrentThread = t;
}

#if 0
/* this would only work on 030... */

extern inline struct thread *
arch_thread_get_current_thread(void)
{
	uint64 v = 0;
	asm volatile("pmove %%srp,(%0)" : : "a"(&v));
	return (struct thread *)(uint32)(v & 0xffffffff);
}


extern inline void
arch_thread_set_current_thread(struct thread *t)
{
	uint64 v;
	asm volatile("pmove %%srp,(%0)\n" \
			"move %1,(4,%0)\n" \
			"pmove (%0),%%srp" : : "a"(&v), "d"(t));
}
#endif

#ifdef __cplusplus
}
#endif


#endif /* _KERNEL_ARCH_M68K_THREAD_H */

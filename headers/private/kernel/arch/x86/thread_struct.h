/*
** Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#ifndef _KERNEL_ARCH_x86_THREAD_STRUCT_H
#define _KERNEL_ARCH_x86_THREAD_STRUCT_H

struct farcall {
	unsigned int *esp;
	unsigned int *ss;
};

#define	IFRAME_TRACE_DEPTH 4

// architecture specific thread info
struct arch_thread {
	struct farcall current_stack;
	struct farcall interrupt_stack;

	// used to track interrupts on this thread
	struct iframe *iframes[IFRAME_TRACE_DEPTH];
	int iframe_ptr;

	// 512 byte floating point save point
	uint8 fpu_state[512];
};

struct arch_team {
	// nothing here
};

#endif	/* _KERNEL_ARCH_x86_THREAD_STRUCT_H */

/*
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef KERNEL_ARCH_THREAD_H
#define KERNEL_ARCH_THREAD_H


#include <thread_types.h>


#ifdef __cplusplus
extern "C" {
#endif

status_t arch_thread_init(struct kernel_args *args);
status_t arch_team_init_team_struct(struct team *t, bool kernel);
status_t arch_thread_init_thread_struct(struct thread *t);
status_t arch_thread_init_tls(struct thread *thread);
void arch_thread_context_switch(struct thread *t_from, struct thread *t_to);
status_t arch_thread_init_kthread_stack(struct thread *t,
	int (*start_func)(void), void (*entry_func)(void), void (*exit_func)(void));
void arch_thread_dump_info(void *info);
status_t arch_thread_enter_userspace(struct thread *t, addr_t entry,
	void *args1, void *args2);

bool arch_on_signal_stack(struct thread *thread);
status_t arch_setup_signal_frame(struct thread *t, struct sigaction *sa,
	int signal, int signalMask);
int64 arch_restore_signal_frame(void);

void arch_store_fork_frame(struct arch_fork_arg *arg);
void arch_restore_fork_frame(struct arch_fork_arg *arg);

#define arch_syscall_64_bit_return_value()
	// overridden by architectures that need special handling

#ifdef __cplusplus
}
#endif

// for any inline overrides
#include <arch_thread.h>

#endif	/* KERNEL_ARCH_THREAD_H */

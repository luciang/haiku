/*
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef _THREAD_H
#define _THREAD_H


#include <OS.h>
#include <thread_types.h>
#include <arch/thread.h>

struct kernel_args;


#ifdef __cplusplus
extern "C" {
#endif

void thread_enqueue(struct thread *t, struct thread_queue *q);
struct thread *thread_lookat_queue(struct thread_queue *q);
struct thread *thread_dequeue(struct thread_queue *q);
struct thread *thread_dequeue_id(struct thread_queue *q, thread_id id);

void thread_at_kernel_entry(void);
	// called when the thread enters the kernel on behalf of the thread
void thread_at_kernel_exit(void);
void thread_reset_for_exec(void);

status_t thread_init(struct kernel_args *args);
status_t thread_preboot_init_percpu(struct kernel_args *args, int32 cpuNum);
void thread_yield(void);
void thread_exit(void);

int32 thread_max_threads(void);
int32 thread_used_threads(void);

#define thread_get_current_thread arch_thread_get_current_thread

struct thread *thread_get_thread_struct(thread_id id);
struct thread *thread_get_thread_struct_locked(thread_id id);

static thread_id thread_get_current_thread_id(void);
static inline thread_id
thread_get_current_thread_id(void)
{
	struct thread *thread = thread_get_current_thread();
	return thread ? thread->id : 0;
}

static inline bool
thread_is_idle_thread(struct thread *thread)
{
	return thread->entry == NULL;
}

thread_id allocate_thread_id(void);
thread_id peek_next_thread_id(void);

thread_id spawn_kernel_thread_etc(thread_func, const char *name, int32 priority,
	void *args, team_id team, thread_id threadID);
status_t wait_for_thread_etc(thread_id id, uint32 flags, bigtime_t timeout,
	status_t *_returnCode);

// used in syscalls.c
status_t _user_set_thread_priority(thread_id thread, int32 newPriority);
status_t _user_rename_thread(thread_id thread, const char *name);
status_t _user_suspend_thread(thread_id thread);
status_t _user_resume_thread(thread_id thread);
status_t _user_rename_thread(thread_id thread, const char *name);
thread_id _user_spawn_thread(thread_entry_func entry, const char *name, int32 priority, void *arg1, void *arg2);
status_t _user_wait_for_thread(thread_id id, status_t *_returnCode);
status_t _user_snooze_etc(bigtime_t timeout, int timebase, uint32 flags);
status_t _user_kill_thread(thread_id thread);
void _user_thread_yield(void);
void _user_exit_thread(status_t return_value);
bool _user_has_data(thread_id thread);
status_t _user_send_data(thread_id thread, int32 code, const void *buffer, size_t buffer_size);
status_t _user_receive_data(thread_id *_sender, void *buffer, size_t buffer_size);
thread_id _user_find_thread(const char *name);
status_t _user_get_thread_info(thread_id id, thread_info *info);
status_t _user_get_next_thread_info(team_id team, int32 *cookie, thread_info *info);

// ToDo: these don't belong here
struct rlimit;
int _user_getrlimit(int resource, struct rlimit * rlp);
int _user_setrlimit(int resource, const struct rlimit * rlp);

#ifdef __cplusplus
}
#endif

#endif /* _THREAD_H */

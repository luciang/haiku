/*
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <OS.h>

#include <stdlib.h>
#include <stdio.h>

#include <libroot_private.h>
#include <pthread_private.h>
#include <thread_defs.h>
#include <tls.h>
#include <syscalls.h>


#undef thread_entry
	// thread_entry is still defined in OS.h for compatibility reasons


typedef struct callback_node {
	struct callback_node *next;
	void (*function)(void *);
	void *argument;
} callback_node;


void _thread_do_exit_work(void);


static status_t
thread_entry(thread_func entry, void* _thread)
{
	pthread_thread* thread = (pthread_thread*)_thread;
	status_t returnCode;

	*tls_address(TLS_PTHREAD_SLOT) = thread;

	returnCode = entry(thread->entry_argument);

	_thread_do_exit_work();

	return returnCode;
}


#if __GNUC__ < 3
void
_thread_do_exit_notification(void)
{
	// empty stub for R5 compability
}
#endif


void
_thread_do_exit_work(void)
{
	callback_node *node = tls_get(TLS_ON_EXIT_THREAD_SLOT);
	callback_node *next;

	while (node != NULL) {
		next = node->next;

		node->function(node->argument);
		free(node);

		node = next;
	}

	tls_set(TLS_ON_EXIT_THREAD_SLOT, NULL);

	__pthread_destroy_thread();
}


// #pragma mark -


thread_id
spawn_thread(thread_func entry, const char *name, int32 priority, void *data)
{
	struct thread_creation_attributes attributes;
	pthread_thread* thread;

	thread = __allocate_pthread(data);
	if (thread == NULL)
		return B_NO_MEMORY;

	_single_threaded = false;
		// used for I/O locking - BeOS compatibility issue

	attributes.entry = &thread_entry;
	attributes.name = name;
	attributes.priority = priority;
	attributes.args1 = entry;
	attributes.args2 = thread;
	attributes.stack_address = NULL;
	attributes.stack_size = 0;

	return _kern_spawn_thread(&attributes);
}


status_t
kill_thread(thread_id thread)
{
	return _kern_kill_thread(thread);
}


status_t
resume_thread(thread_id thread)
{
	return _kern_resume_thread(thread);
}


status_t
suspend_thread(thread_id thread)
{
	return _kern_suspend_thread(thread);
}


status_t
rename_thread(thread_id thread, const char *name)
{
	return _kern_rename_thread(thread, name);
}


status_t
set_thread_priority(thread_id thread, int32 priority)
{
	return _kern_set_thread_priority(thread, priority);
}


void
exit_thread(status_t status)
{
	_thread_do_exit_notification();
	_kern_exit_thread(status);
}


status_t
wait_for_thread(thread_id thread, status_t *_returnCode)
{
	return _kern_wait_for_thread(thread, _returnCode);
}


status_t
on_exit_thread(void (*callback)(void *), void *data)
{
	callback_node **head = (callback_node **)tls_address(TLS_ON_EXIT_THREAD_SLOT);

	callback_node *node = malloc(sizeof(callback_node));
	if (node == NULL)
		return B_NO_MEMORY;

	node->function = callback;
	node->argument = data;

	// add this node to the list
	node->next = *head;
	*head = node;

	return B_OK;
}


status_t
_get_thread_info(thread_id thread, thread_info *info, size_t size)
{
	if (info == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	return _kern_get_thread_info(thread, info);
}


status_t
_get_next_thread_info(team_id team, int32 *cookie, thread_info *info, size_t size)
{
	if (info == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	return _kern_get_next_thread_info(team, cookie, info);
}


status_t
send_data(thread_id thread, int32 code, const void *buffer, size_t bufferSize)
{
	return _kern_send_data(thread, code, buffer, bufferSize);
}


int32
receive_data(thread_id *_sender, void *buffer, size_t bufferSize)
{
	return _kern_receive_data(_sender, buffer, bufferSize);
}


bool
has_data(thread_id thread)
{
	return _kern_has_data(thread);
}


status_t
snooze_etc(bigtime_t timeout, int timeBase, uint32 flags)
{
	return _kern_snooze_etc(timeout, timeBase, flags);
}


status_t
snooze(bigtime_t timeout)
{
	return _kern_snooze_etc(timeout, B_SYSTEM_TIMEBASE, B_RELATIVE_TIMEOUT);
}


status_t
snooze_until(bigtime_t timeout, int timeBase)
{
	return snooze_etc(timeout, timeBase, B_ABSOLUTE_TIMEOUT);
}


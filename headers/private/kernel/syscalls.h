/*
** Copyright 2001, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#ifndef _LIBSYS_SYSCALLS_H
#define _LIBSYS_SYSCALLS_H

#include <ktypes.h>
#include <defines.h>
#include <resource.h>
#include <vfs_types.h>
#include <vm_types.h>
#include <thread_types.h>
#include <OS.h>
#include <image.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

int     sys_null();

int     sys_getrlimit(int resource, struct rlimit * rlp);
int     sys_setrlimit(int resource, const struct rlimit * rlp);

bigtime_t sys_system_time();
status_t  sys_snooze_etc(bigtime_t time, int timebase, int32 flags);

/* sem functions */
sem_id sys_create_sem(int count, const char *name);
int    sys_delete_sem(sem_id id);
int    sys_acquire_sem(sem_id id);
int    sys_acquire_sem_etc(sem_id id, uint32 count, uint32 flags, bigtime_t timeout);
int    sys_release_sem(sem_id id);
int    sys_release_sem_etc(sem_id id, uint32 count, uint32 flags);
int    sys_sem_get_count(sem_id id, int32* thread_count);
int    sys_get_sem_info(sem_id, struct sem_info *, size_t);
int    sys_get_next_sem_info(team_id, uint32 *, struct sem_info *, size_t);
int    sys_set_sem_owner(sem_id id, team_id proc);

void sys_exit(int retcode);
team_id sys_create_team(const char *path, const char *name, char **args, int argc, char **envp, int envc, int priority);

thread_id sys_spawn_thread(int32 (*func)(thread_func, void *), const char *, int32, void *, void *);
thread_id sys_get_current_thread_id(void);
int       sys_suspend_thread(thread_id tid);
int       sys_resume_thread(thread_id tid);
int       sys_kill_thread(thread_id tid);
void      sys_exit_thread(status_t return_value);

status_t sys_wait_on_thread(thread_id tid, status_t *retcode);
int sys_kill_team(team_id tid);

team_id sys_get_current_team_id();
int sys_wait_on_team(team_id tid, int *retcode);

status_t sys_get_thread_info(thread_id id, thread_info *info);
status_t sys_get_next_thread_info(team_id team, int32 *cookie, thread_info *info);
status_t sys_get_team_info(team_id id, team_info *info);
status_t sys_get_next_team_info(int32 *cookie, team_info *info);

int sys_send_signal(pid_t tid, uint sig);
int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
bigtime_t sys_set_alarm(bigtime_t time, uint32 mode);

// image functions
image_id sys_register_image(image_info *info, size_t size);
status_t sys_unregister_image(image_id id);
status_t sys_get_image_info(image_id id, image_info *info, size_t size);
status_t sys_get_next_image_info(team_id team, int32 *cookie, image_info *info, size_t size);

// node monitor functions
status_t sys_stop_notifying(port_id port, uint32 token);
status_t sys_start_watching(dev_t device, ino_t node, uint32 flags,
			port_id port, uint32 token);
status_t sys_stop_watching(dev_t device, ino_t node, uint32 flags,
			port_id port, uint32 token);

// area functions
area_id _kern_create_area(const char *name, void **address, uint32 addressSpec,
			size_t size, uint32 lock, uint32 protection);
region_id sys_vm_map_file(const char *name, void **address, int addr_type,
			addr size, int lock, int mapping, const char *path, off_t offset);
status_t _kern_delete_area(area_id area);
area_id _kern_area_for(void *address);
area_id _kern_find_area(const char *name);
status_t _kern_get_area_info(area_id area, area_info *info);
status_t _kern_get_next_area_info(team_id team, int32 *cookie, area_info *info);
status_t _kern_resize_area(area_id area, size_t newSize);
status_t _kern_set_area_protection(area_id area, uint32 newProtection);
area_id _kern_clone_area(const char *name, void **_address, uint32 addressSpec, 
			uint32 protection, area_id sourceArea);

/* kernel port functions */
port_id		sys_port_create(int32 queue_length, const char *name);
int			sys_port_close(port_id id);
int			sys_port_delete(port_id id);
port_id		sys_port_find(const char *port_name);
int			sys_port_get_info(port_id id, struct port_info *info);
int		 	sys_port_get_next_port_info(team_id team, uint32 *cookie, struct port_info *info);
ssize_t		sys_port_buffer_size(port_id port);
ssize_t		sys_port_buffer_size_etc(port_id port, uint32 flags, bigtime_t timeout);
int32		sys_port_count(port_id port);
ssize_t		sys_port_read(port_id port, int32 *msg_code, void *msg_buffer, size_t buffer_size);
ssize_t		sys_port_read_etc(port_id port,	int32 *msg_code, void *msg_buffer, size_t buffer_size, uint32 flags, bigtime_t timeout);
int			sys_port_set_owner(port_id port, team_id team);
int			sys_port_write(port_id port, int32 msg_code, const void *msg_buffer, size_t buffer_size);
int			sys_port_write_etc(port_id port, int32 msg_code, const void *msg_buffer, size_t buffer_size, uint32 flags, bigtime_t timeout);

/* atomic_* ops (needed for cpus that dont support them directly) */
int sys_atomic_add(int *val, int incr);
int sys_atomic_and(int *val, int incr);
int sys_atomic_or(int *val, int incr);
int sys_atomic_set(int *val, int set_to);
int sys_test_and_set(int *val, int set_to, int test_val);

int sys_sysctl(int *, uint, void *, size_t *, void *, size_t);
int sys_socket(int, int, int);

int sys_setenv(const char *, const char *, int);
int sys_getenv(const char *, char **);
 
/* region prototypes */
area_id sys_find_region_by_name(const char *);

/* This is a real BSD'ism :) Basically it returns the size of the
 * descriptor table for the current process as an integer.
 */
int sys_getdtablesize(void);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBSYS_SYSCALLS_H */

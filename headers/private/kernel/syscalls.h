/*
** Copyright 2002-2004, The Haiku Team. All rights reserved.
** Distributed under the terms of the Haiku License.
**
** Copyright 2001, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#ifndef _LIBSYS_SYSCALLS_H
#define _LIBSYS_SYSCALLS_H


#include <OS.h>
#include <image.h>
#include <sys/select.h>

#include <disk_device_manager/ddm_userland_interface.h>


#ifdef __cplusplus
extern "C" {
#endif

struct sigaction;
struct rlimit;
struct stat;
struct pollfd;
struct fs_info;
struct dirent;

// This marks the beginning of the syscalls prototypes for gensyscallinfos.
// NOTE:
// * Nothing but those prototypes may live here.
// * The arguments of the functions must be named to be processed properly.

#ifdef GEN_SYSCALL_INFOS_PROCESSING
#pragma syscalls begin
#endif

extern int     		_kern_null();

extern int			_kern_getrlimit(int resource, struct rlimit * rlp);
extern int			_kern_setrlimit(int resource, const struct rlimit * rlp);

extern status_t		_kern_shutdown(bool reboot);
extern status_t		_kern_get_safemode_option(const char *parameter, char *buffer, size_t *_bufferSize);

/* sem functions */
sem_id sys_create_sem(int count, const char *name);
int    sys_delete_sem(sem_id id);
int    sys_acquire_sem(sem_id id);
int    sys_acquire_sem_etc(sem_id id, uint32 count, uint32 flags, bigtime_t timeout);
int    sys_release_sem(sem_id id);
int    sys_release_sem_etc(sem_id id, uint32 count, uint32 flags);
int    sys_get_sem_count(sem_id id, int32* thread_count);
int    sys_get_sem_info(sem_id semaphore, struct sem_info *info, size_t size);
int    sys_get_next_sem_info(team_id team, uint32 *cookie,
			struct sem_info *info, size_t size);
int    sys_set_sem_owner(sem_id id, team_id proc);

/* team & thread syscalls */

extern void			_kern_exit(int returnCode);
extern thread_id	_kern_load_image(int32 argCount, const char **args,
						int32 envCount, const char **envp, int32 priority);
extern status_t		_kern_kill_team(team_id team);
extern team_id		_kern_get_current_team();
extern status_t		_kern_wait_for_team(team_id team, status_t *_returnCode);
extern thread_id	_kern_wait_for_child(thread_id child, uint32 flags,
						int32 *_reason, status_t *_returnCode);
extern status_t		_kern_exec(const char *path, int32 argc, char * const *argv, int32 envCount,
						char * const *environment);
extern thread_id	_kern_fork(void);
extern pid_t		_kern_process_info(pid_t process, int32 which);
extern pid_t		_kern_setpgid(pid_t process, pid_t group);
extern pid_t		_kern_setsid(void);

extern thread_id	_kern_spawn_thread(int32 (*func)(thread_func, void *),
						const char *name, int32 priority, void *data1, void *data2);
extern thread_id	_kern_find_thread(const char *name);
extern status_t		_kern_suspend_thread(thread_id thread);
extern status_t		_kern_resume_thread(thread_id thread);
extern status_t		_kern_rename_thread(thread_id thread, const char *newName);
extern status_t		_kern_set_thread_priority(thread_id thread, int32 newPriority);
extern status_t		_kern_kill_thread(thread_id thread);
extern void			_kern_exit_thread(status_t returnValue);
extern status_t		_kern_wait_for_thread(thread_id thread, status_t *_returnCode);
extern bool			_kern_has_data(thread_id thread);
extern status_t		_kern_send_data(thread_id thread, int32 code, const void *buffer, size_t bufferSize);
extern status_t		_kern_receive_data(thread_id *_sender, void *buffer, size_t bufferSize);
extern int64		_kern_restore_signal_frame();

extern status_t		_kern_get_thread_info(thread_id id, thread_info *info);
extern status_t		_kern_get_next_thread_info(team_id team, int32 *cookie, thread_info *info);
extern status_t		_kern_get_team_info(team_id id, team_info *info);
extern status_t		_kern_get_next_team_info(int32 *cookie, team_info *info);
extern status_t		_kern_get_team_usage_info(team_id team, int32 who, team_usage_info *info, size_t size);

// signal functions
extern int			_kern_send_signal(pid_t tid, uint sig);
extern int			_kern_sigprocmask(int how, const sigset_t *set, sigset_t *oldSet);
extern int			_kern_sigaction(int sig, const struct sigaction *action, struct sigaction *oldAction);
extern bigtime_t	_kern_set_alarm(bigtime_t time, uint32 mode);

// image functions
extern image_id		_kern_register_image(image_info *info, size_t size);
extern status_t		_kern_unregister_image(image_id id);
extern status_t		_kern_get_image_info(image_id id, image_info *info, size_t size);
extern status_t		_kern_get_next_image_info(team_id team, int32 *cookie, image_info *info, size_t size);

// VFS functions
extern status_t		_kern_mount(const char *path, const char *device,
						const char *fs_name, uint32 flags, const char *args);
extern status_t		_kern_unmount(const char *path);
extern status_t		_kern_read_fs_info(dev_t device, struct fs_info *info);
extern status_t		_kern_write_fs_info(dev_t device, const struct fs_info *info, int mask);
extern dev_t		_kern_next_device(int32 *_cookie);
extern status_t		_kern_sync(void);
extern status_t		_kern_entry_ref_to_path(dev_t device, ino_t inode,
						const char *leaf, char *userPath, size_t pathLength);
extern int			_kern_open_entry_ref(dev_t device, ino_t inode, const char *name, int omode);
extern int			_kern_open(int fd, const char *path, int omode);
extern int			_kern_open_dir_entry_ref(dev_t device, ino_t inode, const char *name);
extern int			_kern_open_dir(int fd, const char *path);
extern int			_kern_open_parent_dir(int fd, char *name,
						size_t nameLength);
extern status_t		_kern_fcntl(int fd, int op, uint32 argument);
extern status_t		_kern_fsync(int fd);
extern off_t		_kern_seek(int fd, off_t pos, int seekType);
extern int			_kern_create_entry_ref(dev_t device, ino_t inode, const char *uname, int omode, int perms);
extern int			_kern_create(const char *path, int omode, int perms);
extern status_t		_kern_create_dir_entry_ref(dev_t device, ino_t inode, const char *name, int perms);
extern status_t		_kern_create_dir(int fd, const char *path, int perms);
extern status_t		_kern_remove_dir(const char *path);
extern ssize_t		_kern_read_link(int fd, const char *path, char *buffer,
						size_t bufferSize);
extern status_t		_kern_write_link(const char *path, const char *toPath);
extern status_t		_kern_create_symlink(int fd, const char *path,
						const char *toPath, int mode);
extern status_t		_kern_create_link(const char *path, const char *toPath);
extern status_t		_kern_unlink(int fd, const char *path);
extern status_t		_kern_rename(int oldDir, const char *oldpath, int newDir,
						const char *newpath);
extern status_t		_kern_access(const char *path, int mode);
extern ssize_t		_kern_select(int numfds, fd_set *readSet, fd_set *writeSet, fd_set *errorSet,
						bigtime_t timeout, const sigset_t *sigMask);
extern ssize_t		_kern_poll(struct pollfd *fds, int numfds, bigtime_t timeout);
extern int			_kern_open_attr_dir(int fd, const char *path);
extern int			_kern_create_attr(int fd, const char *name, uint32 type, int openMode);
extern int			_kern_open_attr(int fd, const char *name, int openMode);
extern status_t		_kern_remove_attr(int fd, const char *name);
extern status_t		_kern_rename_attr(int fromFile, const char *fromName, int toFile, const char *toName);
extern int			_kern_open_index_dir(dev_t device);
extern status_t		_kern_create_index(dev_t device, const char *name, uint32 type, uint32 flags);
extern status_t		_kern_read_index_stat(dev_t device, const char *name, struct stat *stat);
extern status_t		_kern_remove_index(dev_t device, const char *name);
extern status_t		_kern_getcwd(char *buffer, size_t size);
extern status_t		_kern_setcwd(int fd, const char *path);
extern int			_kern_open_query(dev_t device, const char *query,
						uint32 flags, port_id port, int32 token);

// file descriptor functions
extern ssize_t		_kern_read(int fd, off_t pos, void *buffer, size_t bufferSize);
extern ssize_t		_kern_readv(int fd, off_t pos, const iovec *vecs, size_t count);
extern ssize_t		_kern_write(int fd, off_t pos, const void *buffer, size_t bufferSize);
extern ssize_t		_kern_writev(int fd, off_t pos, const iovec *vecs, size_t count);
extern status_t		_kern_ioctl(int fd, ulong cmd, void *data, size_t length);
extern ssize_t		_kern_read_dir(int fd, struct dirent *buffer, size_t bufferSize, uint32 maxCount);
extern status_t		_kern_rewind_dir(int fd);
extern status_t		_kern_read_stat(int fd, const char *path, bool traverseLink,
						struct stat *stat, size_t statSize);
extern status_t		_kern_write_stat(int fd, const char *path,
						bool traverseLink, const struct stat *stat,
						size_t statSize, int statMask);
extern status_t		_kern_close(int fd);
extern int			_kern_dup(int fd);
extern int			_kern_dup2(int ofd, int nfd);
extern status_t		_kern_lock_node(int fd);
extern status_t		_kern_unlock_node(int fd);

// node monitor functions
extern status_t		_kern_stop_notifying(port_id port, uint32 token);
extern status_t		_kern_start_watching(dev_t device, ino_t node, uint32 flags,
						port_id port, uint32 token);
extern status_t		_kern_stop_watching(dev_t device, ino_t node, uint32 flags,
						port_id port, uint32 token);

// time functions
extern status_t		_kern_set_real_time_clock(uint32 time);
extern status_t		_kern_set_tzspecs(int32 timezone_offset, bool dst_observed);
extern status_t		_kern_set_tzfilename(const char *filename, size_t length, bool is_gmt);
extern status_t		_kern_get_tzfilename(char *filename, size_t length, bool *is_gmt);

extern bigtime_t	_kern_system_time();
extern status_t		_kern_snooze_etc(bigtime_t time, int timebase, int32 flags);

// area functions
area_id _kern_create_area(const char *name, void **address, uint32 addressSpec,
			size_t size, uint32 lock, uint32 protection);
area_id sys_vm_map_file(const char *name, void **address, int addr_type,
			addr_t size, int lock, int mapping, const char *path, off_t offset);
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
port_id		_kern_create_port(int32 queue_length, const char *name);
int			_kern_close_port(port_id id);
int			_kern_delete_port(port_id id);
port_id		_kern_find_port(const char *port_name);
int			_kern_get_port_info(port_id id, struct port_info *info);
int		 	_kern_get_next_port_info(team_id team, uint32 *cookie, struct port_info *info);
ssize_t		_kern_port_buffer_size_etc(port_id port, uint32 flags, bigtime_t timeout);
int32		_kern_port_count(port_id port);
ssize_t		_kern_read_port_etc(port_id port,	int32 *msg_code, void *msg_buffer, size_t buffer_size, uint32 flags, bigtime_t timeout);
int			_kern_set_port_owner(port_id port, team_id team);
int			_kern_write_port_etc(port_id port, int32 msg_code, const void *msg_buffer, size_t buffer_size, uint32 flags, bigtime_t timeout);

/* atomic_* ops (needed for CPUs that don't support them directly) */
#ifdef ATOMIC_FUNCS_ARE_SYSCALLS
int32 _kern_atomic_set(vint32 *value, int32 newValue);
int32 _kern_atomic_test_and_set(vint32 *value, int32 newValue, int32 testAgainst);
int32 _kern_atomic_add(vint32 *value, int32 addValue);
int32 _kern_atomic_and(vint32 *value, int32 andValue);
int32 _kern_atomic_or(vint32 *value, int32 orValue);	
int32 _kern_atomic_get(vint32 *value);
#endif	// ATOMIC_FUNCS_ARE_SYSCALLS

#ifdef ATOMIC64_FUNCS_ARE_SYSCALLS
int64 _kern_atomic_set64(vint64 *value, int64 newValue);
int64 _kern_atomic_test_and_set64(vint64 *value, int64 newValue, int64 testAgainst);
int64 _kern_atomic_add64(vint64 *value, int64 addValue);
int64 _kern_atomic_and64(vint64 *value, int64 andValue);
int64 _kern_atomic_or64(vint64 *value, int64 orValue);	
int64 _kern_atomic_get64(vint64 *value);
#endif	// ATOMIC64_FUNCS_ARE_SYSCALLS

int sys_sysctl(int *name, uint namlen, void *oldp, size_t *oldlen,
		void *newp, size_t newlen);
int sys_socket(int family, int type, int proto);

int sys_setenv(const char *userName, const char *userValue, int overwrite);
int sys_getenv(const char *name, char **value);

/* System informations */
extern status_t		_kern_get_system_info(system_info *info, size_t size);

void _kern_debug_output(const char *message);

/* This is a real BSD'ism :) Basically it returns the size of the
 * descriptor table for the current process as an integer.
 */
//int sys_getdtablesize();


/* DDM syscalls */

// iterating, retrieving device/partition data
partition_id _kern_get_next_disk_device_id(int32 *cookie, size_t *neededSize);
partition_id _kern_find_disk_device(const char *filename, size_t *neededSize);
partition_id _kern_find_partition(const char *filename, size_t *neededSize);
status_t _kern_get_disk_device_data(partition_id deviceID, bool deviceOnly,
									bool shadow, user_disk_device_data *buffer,
									size_t bufferSize, size_t *neededSize);

partition_id _kern_register_file_device(const char *filename);
status_t _kern_unregister_file_device(partition_id deviceID,
									  const char *filename);
	// Only a valid deviceID or filename need to be passed. The other one
	// is -1/NULL. If both is given only filename is ignored.

// disk systems
status_t _kern_get_disk_system_info(disk_system_id id,
									user_disk_system_info *info);
status_t _kern_get_next_disk_system_info(int32 *cookie,
										 user_disk_system_info *info);
status_t _kern_find_disk_system(const char *name, user_disk_system_info *info);

bool _kern_supports_defragmenting_partition(partition_id partitionID,
											int32 changeCounter,
											bool *whileMounted);
bool _kern_supports_repairing_partition(partition_id partitionID,
										int32 changeCounter, bool checkOnly,
										bool *whileMounted);
bool _kern_supports_resizing_partition(partition_id partitionID,
									   int32 changeCounter,
									   bool *canResizeContents,
									   bool *whileMounted);
bool _kern_supports_moving_partition(partition_id partitionID,
									 int32 changeCounter,
									 partition_id *unmovable,
									 partition_id *needUnmounting,
									 size_t bufferSize);
bool _kern_supports_setting_partition_name(partition_id partitionID,
										   int32 changeCounter);
bool _kern_supports_setting_partition_content_name(partition_id partitionID,
												   int32 changeCounter,
												   bool *whileMounted);
bool _kern_supports_setting_partition_type(partition_id partitionID,
										   int32 changeCounter);
bool _kern_supports_setting_partition_parameters(partition_id partitionID,
												 int32 changeCounter);
bool _kern_supports_setting_partition_content_parameters(
	partition_id partitionID, int32 changeCounter, bool *whileMounted);
bool _kern_supports_initializing_partition(partition_id partitionID,
										   int32 changeCounter,
										   const char *diskSystemName);
bool _kern_supports_creating_child_partition(partition_id partitionID,
											 int32 changeCounter);
bool _kern_supports_deleting_child_partition(partition_id partitionID,
											 int32 changeCounter);
bool _kern_is_sub_disk_system_for(disk_system_id diskSystemID,
								  partition_id partitionID,
								  int32 changeCounter);

status_t _kern_validate_resize_partition(partition_id partitionID,
										 int32 changeCounter, off_t *size);
status_t _kern_validate_move_partition(partition_id partitionID,
									   int32 changeCounter, off_t *newOffset);
status_t _kern_validate_set_partition_name(partition_id partitionID,
										   int32 changeCounter, char *name);
status_t _kern_validate_set_partition_content_name(partition_id partitionID,
												   int32 changeCounter,
												   char *name);
status_t _kern_validate_set_partition_type(partition_id partitionID,
										   int32 changeCounter,
										   const char *type);
status_t _kern_validate_initialize_partition(partition_id partitionID,
											 int32 changeCounter,
											 const char *diskSystemName,
											 char *name,
											 const char *parameters,
											 size_t parametersSize);
status_t _kern_validate_create_child_partition(partition_id partitionID,
											   int32 changeCounter,
											   off_t *offset, off_t *size,
											   const char *type,
											   const char *parameters,
											   size_t parametersSize);
status_t _kern_get_partitionable_spaces(partition_id partitionID,
										int32 changeCounter,
										partitionable_space_data *buffer,
										int32 count, int32 *actualCount);
status_t _kern_get_next_supported_partition_type(partition_id partitionID,
												 int32 changeCounter,
												 int32 *cookie, char *type);
status_t _kern_get_partition_type_for_content_type(disk_system_id diskSystemID,
												   const char *contentType,
												   char *type);

// disk device modification
status_t _kern_prepare_disk_device_modifications(partition_id deviceID);
status_t _kern_commit_disk_device_modifications(partition_id deviceID,
												port_id port, int32 token,
												bool completeProgress);
status_t _kern_cancel_disk_device_modifications(partition_id deviceID);
bool _kern_is_disk_device_modified(partition_id deviceID);

status_t _kern_defragment_partition(partition_id partitionID,
									int32 changeCounter);
status_t _kern_repair_partition(partition_id partitionID, int32 changeCounter,
								bool checkOnly);
status_t _kern_resize_partition(partition_id partitionID, int32 changeCounter,
								off_t size);
status_t _kern_move_partition(partition_id partitionID, int32 changeCounter,
							  off_t newOffset);
status_t _kern_set_partition_name(partition_id partitionID,
								  int32 changeCounter, const char *name);
status_t _kern_set_partition_content_name(partition_id partitionID,
										  int32 changeCounter,
										  const char *name);
status_t _kern_set_partition_type(partition_id partitionID,
								  int32 changeCounter, const char *type);
status_t _kern_set_partition_parameters(partition_id partitionID,
										int32 changeCounter,
										const char *parameters,
										size_t parametersSize);
status_t _kern_set_partition_content_parameters(partition_id partitionID,
												int32 changeCounter,
												const char *parameters,
												size_t parametersSize);
status_t _kern_initialize_partition(partition_id partitionID,
									int32 changeCounter,
									const char *diskSystemName,
									const char *name, const char *parameters,
									size_t parametersSize);
status_t _kern_uninitialize_partition(partition_id partitionID,
									  int32 changeCounter);
status_t _kern_create_child_partition(partition_id partitionID,
									  int32 changeCounter, off_t offset,
									  off_t size, const char *type,
									  const char *parameters,
									  size_t parametersSize,
									  partition_id *childID);
status_t _kern_delete_partition(partition_id partitionID, int32 changeCounter);

// jobs
status_t _kern_get_next_disk_device_job_info(int32 *cookie,
											 user_disk_device_job_info *info);
status_t _kern_get_disk_device_job_info(disk_job_id id,
										user_disk_device_job_info *info);
status_t _kern_get_disk_device_job_progress_info(disk_job_id id,
	disk_device_job_progress_info *info);
status_t _kern_pause_disk_device_job(disk_job_id id);
status_t _kern_cancel_disk_device_job(disk_job_id id, bool reverse);

#if 0

// watching
status_t start_disk_device_watching(port_id, int32 token, uint32 flags);
status_t start_disk_device_job_watching(disk_job_id job, port_id, int32 token,
										uint32 flags);
status_t stop_disk_device_watching(port_id, int32 token);

#endif	// 0


// The end mark for gensyscallinfos.
#ifdef GEN_SYSCALL_INFOS_PROCESSING
#pragma syscalls end
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _LIBSYS_SYSCALLS_H */

/* 
** Copyright 2002, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include <sys/stat.h>
#include <syscalls.h>
#include <errno.h>


#define RETURN_AND_SET_ERRNO(err) \
	if (err < 0) { \
		errno = err; \
		return -1; \
	} \
	return err;


int
chmod(const char *path, mode_t mode)
{
	struct stat stat;
	status_t status;

	stat.st_mode = mode;
	status = sys_write_path_stat(path, true, &stat, FS_WRITE_STAT_MODE);

	RETURN_AND_SET_ERRNO(status);
}


int
fchmod(int fd, mode_t mode)
{
	struct stat stat;
	status_t status;

	stat.st_mode = mode;
	status = sys_write_stat(fd, &stat, FS_WRITE_STAT_MODE);

	RETURN_AND_SET_ERRNO(status);
}

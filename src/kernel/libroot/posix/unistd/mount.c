/* 
** Copyright 2002, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include <unistd.h>
#include <syscalls.h>
#include <errno.h>


#define RETURN_AND_SET_ERRNO(err) \
	if (err < 0) { \
		errno = err; \
		return -1; \
	} \
	return err;


int 
mount(const char *filesystem, const char *where, const char *device, ulong flags, void *parms, int len)
{
	// ToDo: consider parsing "parms" string in userland
	int status = sys_mount(where, device, filesystem/*, flags*/, parms);

	(void)len;

	RETURN_AND_SET_ERRNO(status);
}


int 
unmount(const char *path)
{
	int status = sys_unmount(path);
	
	RETURN_AND_SET_ERRNO(status);
}


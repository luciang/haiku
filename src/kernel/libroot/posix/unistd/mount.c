/* 
** Copyright 2002-2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the Haiku License.
*/


#if 0

#include <errno.h>
#include <sys/types.h>

int mount(const char *filesystem, const char *where, const char *device, ulong flags, void *parms, int len);
int unmount(const char *path);

int 
mount(const char *filesystem, const char *where, const char *device, ulong flags, void *parms, int len)
{
	errno = EOPNOTSUPP;
	return -1;
}


int 
unmount(const char *path)
{
	errno = EOPNOTSUPP;
	return -1;
}

#endif

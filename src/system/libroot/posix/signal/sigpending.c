/*
 *  Copyright (c) 2005, Haiku Project. All rights reserved.
 *  Distributed under the terms of the Haiku license.
 *
 *  Author(s):
 *  J�r�me Duval
 */

#include <errno.h>
#include <syscalls.h>
#include <signal.h>

int
sigpending(sigset_t *set)
{
	int err = _kern_sigpending(set);
	if (err < B_OK) {
		errno = err;
		return -1;
	}
	return 0;
}


/*
** Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the Haiku License.
*/


#include <syscalls.h>

#include <signal.h>
#include <errno.h>


int
sigprocmask(int how, const sigset_t *set, sigset_t *oldSet)
{
/*	int status = sigprocmask(how, set, oldSet);
	if (status < B_OK) {
		errno = status;
		return -1;
	}
*/
	return 0;
}


/*
** Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the Haiku License.
*/


#include <OS.h>

#include <signal.h>
#include <errno.h>


int
kill(pid_t pid, int sig)
{
	status_t status = send_signal(pid, (uint)sig);
	if (status < B_OK) {
		errno = status;
		return -1;
	}

	return 0;
}

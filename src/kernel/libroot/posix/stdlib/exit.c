/*
 *  Copyright (c) 2002, OpenBeOS Project.
 *  All rights reserved.
 *  Distributed under the terms of the OpenBeOS license. 
 *
 *
 *  exit.c:
 *  implements the standard C library functions:
 *    abort, atexit, exit
 *
 *
 *  Author(s):
 *  Daniel Reinhold (danielre@users.sf.net)
 *
 */


#include <syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>

#include "../stdio/local.h"
//#include "stdio_private.h"

extern void _thread_do_exit_notification(void);

static void (*_gExitStack[ATEXIT_MAX])(void) = {0};
static int32 _gExitStackIndex = 0;


void
abort()
{
	raise(SIGABRT);
	exit(EXIT_FAILURE);
}


int
atexit(void (*func)(void))
{
	// push the function pointer onto the exit stack
	int32 index = atomic_add(&_gExitStackIndex, 1);

	if (index >= ATEXIT_MAX)
		return -1;

	_gExitStack[index] = func;
	return 0;
}


void
exit(int status)
{
	// BeOS on exit notification for the main thread
	_thread_do_exit_notification();

	// unwind the exit stack, calling the registered functions
	while (_gExitStackIndex-- > 0)
		(*_gExitStack[_gExitStackIndex])();

	// close all open files
	_IO_cleanup();

	// exit with status code
	sys_exit(status);
}


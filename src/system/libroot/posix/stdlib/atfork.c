/* 
** Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the Haiku License.
*/


#include <fork.h>

#include <stdlib.h>
#include <errno.h>


/**	This is the BeOS compatible atfork() function; since it's not part of POSIX,
 *	it should probably go away over time.
 *	Use pthread_atfork() instead.
 */

int
atfork(void (*function)(void))
{
	status_t status = __register_atfork(NULL, NULL, function);
	if (status < B_OK) {
		errno = status;
		return -1;
	}

	return 0;
}


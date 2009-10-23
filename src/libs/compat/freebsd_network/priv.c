/*
 * Copyright 2009, Colin Günther, coling@gmx.de.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include <thread.h>

#include <sys/param.h>
#include <sys/priv.h>


/*
 * FreeBSD has a more sophisticated privilege checking system.
 * We only check for superuser rights.
 */
int	priv_check(struct thread *thread, int privilegeLevel)
{
	if (thread_get_current_thread()->team->effective_uid == 0)
		return ENOERR;

	return EPERM;
}

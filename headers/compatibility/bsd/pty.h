/*
 * Copyright 2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BSD_PTY_H_
#define _BSD_PTY_H_

#include <sys/cdefs.h>
#include <termios.h>


__BEGIN_DECLS

extern int		openpty(int* master, int* slave, char* name,
					struct termios* termAttrs, struct winsize* windowSize);

__END_DECLS

#endif	// _BSD_PTY_H_

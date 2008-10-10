/*
 * Copyright 2007, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYSTEM_ARCH_x86_COMMPAGE_DEFS_H
#define _SYSTEM_ARCH_x86_COMMPAGE_DEFS_H

#ifndef _SYSTEM_COMMPAGE_DEFS_H
#	error Must not be included directly. Include <commpage_defs.h> instead!
#endif

#define COMMPAGE_ENTRY_X86_SYSCALL	(COMMPAGE_ENTRY_FIRST_ARCH_SPECIFIC + 0)
#define COMMPAGE_ENTRY_X86_MEMCPY	(COMMPAGE_ENTRY_FIRST_ARCH_SPECIFIC + 1)
#define COMMPAGE_ENTRY_X86_MEMSET	(COMMPAGE_ENTRY_FIRST_ARCH_SPECIFIC + 2)

#define ARCH_USER_COMMPAGE_ADDR (0xffff0000)

#endif	/* _SYSTEM_ARCH_x86_COMMPAGE_DEFS_H */

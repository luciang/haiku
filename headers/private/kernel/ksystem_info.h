/*
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_SYSTEM_INFO_H
#define _KERNEL_SYSTEM_INFO_H


#include <OS.h>

struct kernel_args;


#ifdef __cplusplus
extern "C" {
#endif

extern status_t system_info_init(struct kernel_args *args);
extern uint32 get_haiku_revision(void);

extern status_t _user_get_system_info(system_info *userInfo, size_t size);
extern status_t _user_get_system_info_etc(int32 id, void *buffer,
	size_t bufferSize);

#ifdef __cplusplus
}
#endif

#endif	/* _KERNEL_SYSTEM_INFO_H */

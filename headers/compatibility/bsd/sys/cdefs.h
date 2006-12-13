/*
 * Copyright 2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BSD_SYS_CDEFS_H_
#define _BSD_SYS_CDEFS_H_


#include_next <sys/cdefs.h>


#define __FBSDID(x)
#define __unused
#define protected

#define __printflike(a, b)	__attribute__ ((format (__printf__, (a), (b))))
#define __printf0like(a, b)

#endif	/* _BSD_SYS_CDEFS_H_ */

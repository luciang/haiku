/*
 * errno.h
 */

#ifndef _POSIX_ERRNO_H
#define _POSIX_ERRNO_H

#ifdef __cplusplus
extern "C"
{
#endif  

#include <Errors.h>

#define ENOERR          0
#define EOK 			ENOERR  /* some code assumes EOK exists */

extern int *_errnop(void);
#define errno (*(_errnop()))

#ifdef __cplusplus
} /* "C" */
#endif 

#endif /* _POSIX_ERRNO_H */

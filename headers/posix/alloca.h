#ifndef	_ALLOCA_H
#define	_ALLOCA_H
/* 
** Distributed under the terms of the OpenBeOS License.
*/


#include <sys/types.h>


#undef	__alloca
#undef	alloca

#ifdef __cplusplus
extern "C" {
#endif

extern void * __alloca (size_t __size);
extern void * alloca (size_t __size);

#ifdef __cplusplus
}
#endif

#define	__alloca(size)	__builtin_alloca (size)
#define alloca(size)	__alloca (size)

#endif	/* _ALLOCA_H */

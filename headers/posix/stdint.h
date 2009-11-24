/*
 * Copyright 2003-2009, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _STDINT_H_
#define _STDINT_H_


#include <config/types.h>


/* Exact-width integer types */
typedef __haiku_std_int8	int8_t;
typedef __haiku_std_uint8	uint8_t;

typedef __haiku_std_int16	int16_t;
typedef __haiku_std_uint16	uint16_t;

typedef __haiku_std_int32	int32_t;
typedef __haiku_std_uint32	uint32_t;

typedef __haiku_std_int64	int64_t;
typedef __haiku_std_uint64	uint64_t;

/* Minimum-width integer types */
typedef int8_t int_least8_t;
typedef uint8_t uint_least8_t;

typedef int16_t int_least16_t;
typedef uint16_t uint_least16_t;

typedef int32_t int_least32_t;
typedef uint32_t uint_least32_t;

typedef int64_t int_least64_t;
typedef uint64_t uint_least64_t;

/* Fastest minimum-width integer types */
typedef int32_t int_fast8_t;
typedef uint32_t uint_fast8_t;

typedef int32_t int_fast16_t;
typedef uint32_t uint_fast16_t;

typedef int32_t int_fast32_t;
typedef uint32_t uint_fast32_t;

typedef int64_t int_fast64_t;
typedef uint64_t uint_fast64_t;

/* Integer types capable of holding object pointers */
typedef __haiku_saddr_t intptr_t;
typedef __haiku_addr_t uintptr_t;

/* Greatest-width integer types */
typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

/* Limits of exact-width integer types */
#define INT8_MIN 	(-128)
#define INT8_MAX 	(127)
#define UINT8_MAX	(255U)

#define INT16_MIN 	(-32768)
#define INT16_MAX 	(32767)
#define UINT16_MAX	(65535U)

#define INT32_MAX	(2147483647)
#define INT32_MIN 	(-INT32_MAX-1)
#define UINT32_MAX	(4294967295U)

#define INT64_MAX	(9223372036854775807LL)
#define INT64_MIN	(-INT64_MAX-1)
#define UINT64_MAX	(18446744073709551615ULL)

/* Limits of minimum-width integer types */
#define INT_LEAST8_MIN  	INT8_MIN
#define INT_LEAST8_MAX  	INT8_MAX
#define UINT_LEAST8_MAX 	UINT8_MAX

#define INT_LEAST16_MIN 	INT16_MIN
#define INT_LEAST16_MAX 	INT16_MAX
#define UINT_LEAST16_MAX	UINT16_MAX

#define INT_LEAST32_MIN 	INT32_MIN
#define INT_LEAST32_MAX 	INT32_MAX
#define UINT_LEAST32_MAX	UINT32_MAX

#define INT_LEAST64_MIN 	INT64_MIN
#define INT_LEAST64_MAX 	INT64_MAX
#define UINT_LEAST64_MAX	UINT64_MAX

/* Limits of fastest minimum-width integer types */
#define INT_FAST8_MIN  	INT8_MIN
#define INT_FAST8_MAX  	INT8_MAX
#define UINT_FAST8_MAX 	UINT8_MAX

#define INT_FAST16_MIN 	INT16_MIN
#define INT_FAST16_MAX 	INT16_MAX
#define UINT_FAST16_MAX	UINT16_MAX

#define INT_FAST32_MIN 	INT32_MIN
#define INT_FAST32_MAX 	INT32_MAX
#define UINT_FAST32_MAX	UINT32_MAX

#define INT_FAST64_MIN 	INT64_MIN
#define INT_FAST64_MAX 	INT64_MAX
#define UINT_FAST64_MAX	UINT64_MAX

/* Limits of Integer types capable of holding object pointers */
#define INTPTR_MIN	__HAIKU_SADDR_MIN
#define INTPTR_MAX	__HAIKU_SADDR_MAX
#define UINTPTR_MAX	__HAIKU_ADDR_MAX

/* Limits of Greatest-width integer types */
#define INTMAX_MIN	INT64_MIN
#define INTMAX_MAX	INT64_MAX
#define UINTMAX_MAX	UINT64_MAX

/* Limits of other integer types */
#define PTDIFF_MIN __HAIKU_SADDR_MIN
#define PTDIFF_MAX __HAIKU_SADDR_MAX

#define SIG_ATOMIC_MIN INT32_MIN
#define SIG_ATOMIC_MAX INT32_MAX

#define SIZE_MAX 	__HAIKU_ADDR_MAX

#define WINT_MIN 	0
#define WINT_MAX 	((wint_t)-1)


#if !defined(__cplusplus) || defined(__STDC_CONSTANT_MACROS)

/* Macros of Integer Constant Expressions */
#define INT8_C(value) 	value
#define INT16_C(value) 	value
#define INT32_C(value) 	value
#define INT64_C(value) 	value ## LL

#define UINT8_C(value) 	value ## U
#define UINT16_C(value) value ## U
#define UINT32_C(value) value ## U
#define UINT64_C(value) value ## ULL

/* Macros for greatest-width integer constant expressions */
#define INTMAX_C(value) 	value ## LL
#define UINTMAX_C(value) 	value ## ULL

#endif /* !defined(__cplusplus) || defined(__STDC_CONSTANT_MACROS) */


/* BSD compatibility */
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;


#endif	/* _STDINT_H_ */

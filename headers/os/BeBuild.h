/*
 * Copyright 2007-2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BE_BUILD_H
#define _BE_BUILD_H


#include <config/HaikuConfig.h>


#define B_BEOS_VERSION_4				0x0400
#define B_BEOS_VERSION_4_5				0x0450
#define B_BEOS_VERSION_5				0x0500

#define B_BEOS_VERSION					B_BEOS_VERSION_5
#define B_BEOS_VERSION_MAUI				B_BEOS_VERSION_5

/* Haiku (API) version */
#define B_HAIKU_VERSION_BEOS			0x00000001
#define B_HAIKU_VERSION_BONE			0x00000002
#define B_HAIKU_VERSION_DANO			0x00000003
#define B_HAIKU_VERSION_1_ALPHA_1		0x00000100
#define B_HAIKU_VERSION_1_PRE_ALPHA_2	0x00000101
#define B_HAIKU_VERSION_1_ALPHA_2		0x00000200
#define B_HAIKU_VERSION_1_PRE_ALPHA_3	0x00000201
#define B_HAIKU_VERSION_1				0x00010000

#define B_HAIKU_VERSION					B_HAIKU_VERSION_1_PRE_ALPHA_3

/* Haiku ABI */
#define B_HAIKU_ABI_MAJOR				0xffff0000
#define B_HAIKU_ABI_GCC_2				0x00020000
#define B_HAIKU_ABI_GCC_4				0x00040000

#define B_HAIKU_ABI_GCC_2_ANCIENT		0x00020000
#define B_HAIKU_ABI_GCC_2_BEOS			0x00020001
#define B_HAIKU_ABI_GCC_2_HAIKU			0x00020002

#if __GNUC__ == 2
#	define B_HAIKU_ABI					B_HAIKU_ABI_GCC_2_HAIKU
#elif __GNUC__ == 4
#	define B_HAIKU_ABI					B_HAIKU_ABI_GCC_4
#else
#	error Unsupported gcc version!
#endif


#ifdef __HAIKU_ARCH_64_BIT
#	define B_HAIKU_64_BIT				1
#else
#	define B_HAIKU_32_BIT				1
#endif

#ifdef __HAIKU_BEOS_COMPATIBLE
#	define B_HAIKU_BEOS_COMPATIBLE		1
#endif


#define _UNUSED(argument) argument
#define _PACKED __attribute__((packed))
#define _PRINTFLIKE(_format_, _args_) \
	__attribute__((format(__printf__, _format_, _args_)))
#define _EXPORT
#define _IMPORT

#define B_DEFINE_SYMBOL_VERSION(function, versionedSymbol)	\
	__asm__(".symver " function "," versionedSymbol)

#endif	/* _BE_BUILD_H */

/*
 * Copyright 2009 Haiku Inc.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef SYSTEM_ARCH_MIPSEL_ASM_DEFS_H
#define SYSTEM_ARCH_MIPSEL_ASM_DEFS_H


#define SYMBOL(name)			.global name; name
#define SYMBOL_END(name)		1: .size name, 1b - name
#define STATIC_FUNCTION(name)	.type name, @function; name
#define FUNCTION(name)			.global name; .type name, @function; name
#define FUNCTION_END(name)		1: .size name, 1b - name


#endif	/* SYSTEM_ARCH_MIPSEL_ASM_DEFS_H */


/* Copyright (C) 2003 Marcus Overhagen
 * Released under terms of the MIT license.
 *
 * A simple byte order swapping class for the audio mixer.
 */

#include <MediaDefs.h>
#include <ByteOrder.h>
#include "ByteSwap.h"
#include "debug.h"

static void swap_float(void *buffer, size_t bytecount);
static void swap_int32(void *buffer, size_t bytecount);
static void swap_int16(void *buffer, size_t bytecount);
static void do_nothing(void *buffer, size_t bytecount);

ByteSwap::ByteSwap(uint32 format)
{
	switch (format) {
		case media_raw_audio_format::B_AUDIO_FLOAT:
			fFunc = &swap_float;
			break;
		case media_raw_audio_format::B_AUDIO_INT:
			fFunc = &swap_int32;
			break;
		case media_raw_audio_format::B_AUDIO_SHORT:
			fFunc = &swap_int16;
			break;
		default:
			fFunc = &do_nothing;
			break;
	}
}

ByteSwap::~ByteSwap()
{
}

void
do_nothing(void *buffer, size_t bytecount)
{
}

#ifdef __INTEL__

// optimized for IA32 platform

void
swap_float(void *buffer, size_t bytecount)
{
	swap_data(B_FLOAT_TYPE, buffer, bytecount, B_SWAP_ALWAYS); // XXX should be optimized
}

void
swap_int32(void *buffer, size_t bytecount)
{
	swap_data(B_INT32_TYPE, buffer, bytecount, B_SWAP_ALWAYS); // XXX should be optimized
}

void
swap_int16(void *buffer, size_t bytecount)
{
	// GCC FAQ: To write an asm which modifies an input operand but does
	//          not output anything usable, specify that operand as an
	//          output operand outputting to an unused dummy variable.
	uint32 dummy1;
	uint32 dummy2;
	asm (
	"pushl	%%ebx	\n\t"
	"movl	%%eax, %%ebx	\n\t"
	"movl	%%edx, %%eax	\n\t"
	"andl   $0xFFFFFFE0,%%eax	\n\t"
	"pushl	%%eax	\n\t"
	"swap_int16_swap: \n\t"
		"rolw	$8,-32(%%ebx,%%eax)	\n\t"
		"rolw	$8,-30(%%ebx,%%eax)	\n\t"
		"rolw	$8,-28(%%ebx,%%eax)	\n\t"
		"rolw	$8,-26(%%ebx,%%eax)	\n\t"
		"rolw	$8,-24(%%ebx,%%eax)	\n\t"
		"rolw	$8,-22(%%ebx,%%eax)	\n\t"
		"rolw	$8,-20(%%ebx,%%eax)	\n\t"
		"rolw	$8,-18(%%ebx,%%eax)	\n\t"
		"rolw	$8,-16(%%ebx,%%eax)	\n\t"
		"rolw	$8,-14(%%ebx,%%eax)	\n\t"
		"rolw	$8,-12(%%ebx,%%eax)	\n\t"
		"rolw	$8,-10(%%ebx,%%eax)	\n\t"
		"rolw	$8,-8(%%ebx,%%eax)	\n\t"
		"rolw	$8,-6(%%ebx,%%eax)	\n\t"
		"rolw	$8,-4(%%ebx,%%eax)	\n\t"
		"rolw	$8,-2(%%ebx,%%eax)	\n\t"
		"subl	$32,%%eax	\n\t"
		"jnz	swap_int16_swap	\n\t"
	"popl	%%eax	\n\t"
	"addl	%%eax,%%ebx	\n\t"
	"andl	$0x1F,%%edx	\n\t"
	"jz		swap_int16_end	\n\t"
	"addl	%%ebx, %%edx	\n\t"
	"swap_int16_swap2: \n\t"
		"rolw	$8,(%%ebx)	\n\t"
		"addl	$2,%%ebx	\n\t"
		"cmpl	%%edx,%%ebx	\n\t"
		"jne	swap_int16_swap2 \n\t"
	"swap_int16_end:	\n\t"
	"popl	%%ebx \n\t"
	: "=a" (dummy1), "=d" (dummy2)
	: "d" (bytecount), "a" (buffer)
	: "cc", "memory");
}

#else

// non optimized default versions, do not remove

void
swap_float(void *buffer, size_t bytecount)
{
	swap_data(B_FLOAT_TYPE, buffer, bytecount, B_SWAP_ALWAYS);
}

void
swap_int32(void *buffer, size_t bytecount)
{
	swap_data(B_INT32_TYPE, buffer, bytecount, B_SWAP_ALWAYS);
}

void
swap_int16(void *buffer, size_t bytecount)
{
	swap_data(B_INT16_TYPE, buffer, bytecount, B_SWAP_ALWAYS);
}

#endif

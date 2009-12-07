/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2004-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * calculate_cpu_conversion_factor() was written by Travis Geiselbrecht and
 * licensed under the NewOS license.
 */


#include "cpu.h"

#include <OS.h>
#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>
#include <boot/stage2.h>
#include <arch/cpu.h>
#include <arch_kernel.h>
#include <arch_system_info.h>

#include <string.h>


//#define TRACE_CPU
#ifdef TRACE_CPU
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


extern "C" uint64 rdtsc();

uint32 gTimeConversionFactor;

#define TIMER_CLKNUM_HZ (14318180/12)

#define CPUID_EFLAGS	(1UL << 21)
#define RDTSC_FEATURE	(1UL << 4)


struct uint128 {
	uint128(uint64 low, uint64 high = 0)
		:
		low(low),
		high(high)
	{
	}

	bool operator<(const uint128& other) const
	{
		return high < other.high || (high == other.high && low < other.low);
	}

	bool operator<=(const uint128& other) const
	{
		return !(other < *this);
	}

	uint128 operator<<(int count) const
	{
		if (count == 0)
			return *this;

		if (count >= 128)
			return 0;

		if (count >= 64)
			return uint128(0, low << (count - 64));

		return uint128(low << count, (high << count) | (low >> (64 - count)));
	}

	uint128 operator>>(int count) const
	{
		if (count == 0)
			return *this;

		if (count >= 128)
			return 0;

		if (count >= 64)
			return uint128(high >> (count - 64), 0);

		return uint128((low >> count) | (high << (64 - count)), high >> count);
	}

	uint128 operator+(const uint128& other) const
	{
		uint64 resultLow = low + other.low;
		return uint128(resultLow,
			high + other.high + (resultLow < low ? 1 : 0));
	}

	uint128 operator-(const uint128& other) const
	{
		uint64 resultLow = low - other.low;
		return uint128(resultLow,
			high - other.high - (resultLow > low ? 1 : 0));
	}

	uint128 operator*(uint32 other) const
	{
		uint64 resultMid = (low >> 32) * other;
		uint64 resultLow = (low & 0xffffffff) * other + (resultMid << 32);
		return uint128(resultLow,
			high * other + (resultMid >> 32)
				+ (resultLow < resultMid << 32 ? 1 : 0));
	}

	uint128 operator/(const uint128& other) const
	{
		int shift = 0;
		uint128 shiftedDivider = other;
		while (shiftedDivider.high >> 63 == 0 && shiftedDivider < *this) {
			shiftedDivider = shiftedDivider << 1;
			shift++;
		}

		uint128 result = 0;
		uint128 temp = *this;
		for (; shift >= 0; shift--, shiftedDivider = shiftedDivider >> 1) {
			if (shiftedDivider <= temp) {
				result = result + (uint128(1) << shift);
				temp = temp - shiftedDivider;
			}
		}

		return result;
	}

	operator uint64() const
	{
		return low;
	}

private:
	uint64	low;
	uint64	high;
};


static void
calculate_cpu_conversion_factor()
{
	uint32 s_low, s_high;
	uint32 low, high;
	uint32 expired;
	uint64 t1, t2;
	uint64 p1, p2, p3;
	double r1, r2, r3;

	out8(0x34, 0x43);	/* program the timer to count down mode */
	out8(0xff, 0x40);	/* low and then high */
	out8(0xff, 0x40);

	/* quick sample */
quick_sample:
	do {
		out8(0x00, 0x43); /* latch counter value */
		s_low = in8(0x40);
		s_high = in8(0x40);
	} while (s_high != 255);
	t1 = rdtsc();
	do {
		out8(0x00, 0x43); /* latch counter value */
		low = in8(0x40);
		high = in8(0x40);
	} while (high > 224);
	t2 = rdtsc();

	p1 = t2-t1;
	r1 = (double)(p1) / (double)(((s_high << 8) | s_low) - ((high << 8) | low));

	/* not so quick sample */
not_so_quick_sample:
	do {
		out8(0x00, 0x43); /* latch counter value */
		s_low = in8(0x40);
		s_high = in8(0x40);
	} while (s_high != 255);
	t1 = rdtsc();
	do {
		out8(0x00, 0x43); /* latch counter value */
		low = in8(0x40);
		high = in8(0x40);
	} while (high > 192);
	t2 = rdtsc();
	p2 = t2-t1;
	r2 = (double)(p2) / (double)(((s_high << 8) | s_low) - ((high << 8) | low));
	if ((r1/r2) > 1.01) {
		//dprintf("Tuning loop(1)\n");
		goto quick_sample;
	}
	if ((r1/r2) < 0.99) {
		//dprintf("Tuning loop(1)\n");
		goto quick_sample;
	}

	/* slow sample */
	do {
		out8(0x00, 0x43); /* latch counter value */
		s_low = in8(0x40);
		s_high = in8(0x40);
	} while (s_high != 255);
	t1 = rdtsc();
	do {
		out8(0x00, 0x43); /* latch counter value */
		low = in8(0x40);
		high = in8(0x40);
	} while (high > 128);
	t2 = rdtsc();

	p3 = t2-t1;
	r3 = (double)(p3) / (double)(((s_high << 8) | s_low) - ((high << 8) | low));
	if ((r2/r3) > 1.01) {
		TRACE(("Tuning loop(2)\n"));
		goto not_so_quick_sample;
	}
	if ((r2/r3) < 0.99) {
		TRACE(("Tuning loop(2)\n"));
		goto not_so_quick_sample;
	}

	expired = ((s_high << 8) | s_low) - ((high << 8) | low);
	p3 *= TIMER_CLKNUM_HZ;

	gTimeConversionFactor = ((uint128(expired) * uint32(1000000)) << 32)
		/ uint128(p3);

#ifdef TRACE_CPU
	if (p3 / expired / 1000000000LL)
		dprintf("CPU at %Ld.%03Ld GHz\n", p3/expired/1000000000LL, ((p3/expired)%1000000000LL)/1000000LL);
	else
		dprintf("CPU at %Ld.%03Ld MHz\n", p3/expired/1000000LL, ((p3/expired)%1000000LL)/1000LL);
#endif

	gKernelArgs.arch_args.system_time_cv_factor = gTimeConversionFactor;
	gKernelArgs.arch_args.cpu_clock_speed = p3 / expired;
}


static status_t
check_cpu_features()
{
	// check the eflags register to see if the cpuid instruction exists
	if ((get_eflags() & CPUID_EFLAGS) == 0) {
		// it's not set yet, but maybe we can set it manually
		set_eflags(get_eflags() | CPUID_EFLAGS);
		if ((get_eflags() & CPUID_EFLAGS) == 0)
			return B_ERROR;
	}

	cpuid_info info;
	if (get_current_cpuid(&info, 1) != B_OK)
		return B_ERROR;

	if ((info.eax_1.features & RDTSC_FEATURE) == 0) {
		// we currently require RDTSC
		return B_ERROR;
	}

	return B_OK;
}


//	#pragma mark -


extern "C" void
spin(bigtime_t microseconds)
{
	bigtime_t time = system_time();

	while ((system_time() - time) < microseconds)
		asm volatile ("pause;");
}


extern "C" void
cpu_init()
{
	if (check_cpu_features() != B_OK)
		panic("You need a Pentium or higher in order to boot!\n");

	calculate_cpu_conversion_factor();

	gKernelArgs.num_cpus = 1;
		// this will eventually be corrected later on
}


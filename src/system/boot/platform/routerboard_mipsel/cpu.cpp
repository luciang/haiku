/*
 * Copyright 2009 Jonas Sundström, jonas@kirilla.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "cpu.h"

#include <OS.h>
#include <arch/cpu.h>
#include <arch_kernel.h>
#include <arch_system_info.h>

#include <string.h>


static status_t
check_cpu_features()
{
#warning IMPLEMENT check_cpu_features
	return B_ERROR;
}


//	#pragma mark -


extern "C" void
spin(bigtime_t microseconds)
{
#warning IMPLEMENT spin
}


extern "C" void
cpu_init()
{
#warning IMPLEMENT cpu_init
}


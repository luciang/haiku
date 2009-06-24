/*
 * Copyright 2007, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <commpage.h>

#include <string.h>

#include <KernelExport.h>

#include <cpu.h>
#include <smp.h>


status_t
arch_commpage_init(void)
{
	/* no optimized memcpy or anything yet */
	/* we don't use it for syscall yet either */
	return B_OK;
}


status_t
arch_commpage_init_post_cpus(void)
{
	return B_OK;
}

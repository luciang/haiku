/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <drivers/fs_interface.h>
#include <drivers/module.h>

// Amount of memory reserved by LKL
// This includes all caches, all dynamically allocated structures
const int LKL_MEMORY_SIZE = 64 * 1024 * 1024; // 64MiB

// We can't include Haiku's headers directly because Haiku and LKL
// define types with the same name in different ways.
extern int lkl_env_init(int memsize);
extern void lkl_env_fini(void);

// defined in lklfs_file_system_info.c
extern file_system_module_info lklfs_file_system_info;


status_t lklfs_std_ops(int32 op, ...);

extern sem_id lklfs_vnode_sem;

status_t
lklfs_std_ops(int32 op, ...)
{
	switch (op) {
	case B_MODULE_INIT:
		lklfs_vnode_sem = create_sem(1, "lklfs-vnode-path-protector");
		if (lklfs_vnode_sem < B_OK)
			return lklfs_vnode_sem;
		lkl_env_init(LKL_MEMORY_SIZE);
		return B_OK;
	case B_MODULE_UNINIT:
		lkl_env_fini();
		delete_sem(lklfs_vnode_sem);
		return B_OK;
	default:
		return B_ERROR;
	}
}


module_info * modules[] = {
	(module_info *) &lklfs_file_system_info,
	NULL,
};


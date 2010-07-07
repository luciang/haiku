/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <fs_volume.h>
#include <drivers/fs_interface.h>
#include <KernelExport.h>


#define BRIDGE_HAIKU
#include "lkl-haiku-bridge.h"

#define NIMPL dprintf("[[%s]] called -- NOT Implemented yet!\n", __func__); return B_ERROR;


static status_t
lklfs_unmount(fs_volume * _volume)
{
	return lklfs_umount_impl(_volume->private_volume);
}


static status_t
lklfs_read_fs_info(fs_volume * volume, struct fs_info * info)
{
	NIMPL;
}


static status_t
lklfs_get_vnode(fs_volume * volume, ino_t id,
	fs_vnode * vnode, int * _type, uint32 * _flags, bool reenter)
{
	NIMPL;
}


static status_t
lklfs_write_fs_info(fs_volume * volume, const struct fs_info * info, uint32 mask)
{
	NIMPL;
}


static status_t lklfs_sync(fs_volume * volume)
{
	NIMPL;
}


fs_volume_ops lklfs_volume_ops = {
	lklfs_unmount,
	lklfs_read_fs_info,
	lklfs_write_fs_info,
	lklfs_sync,
	lklfs_get_vnode,

    /* index directory & index operations */
	NULL, //	lklfs_open_index_dir
	NULL, //	lklfs_close_index_dir
	NULL, //	lklfs_free_index_dir_cookie
	NULL, //	lklfs_read_index_dir
	NULL, //	lklfs_rewind_index_dir
	NULL, //	lklfs_create_index
	NULL, //	lklfs_remove_index
	NULL, //	lklfs_read_index_stat

	/* query operations */
	NULL, //	lklfs_open_query
	NULL, //	lklfs_close_query
	NULL, //	lklfs_free_query_cookie
	NULL, //	lklfs_read_query
	NULL, //	lklfs_rewind_query

	/* support for FS layers */
	NULL, //	lklfs_all_layers_mounted
	NULL, //	lklfs_create_sub_vnode
	NULL, //	lklfs_delete_sub_vnode
};



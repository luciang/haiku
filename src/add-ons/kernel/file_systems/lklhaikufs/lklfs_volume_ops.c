/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <fs_volume.h>
#include <fs_info.h>
#include <string.h>
#include <drivers/fs_interface.h>
#include <KernelExport.h>


#define BRIDGE_HAIKU
#include "lkl-haiku-bridge.h"
#include "lh-error.h"

#define NIMPL dprintf("[[%s]] called -- NOT Implemented yet!\n", __func__); return B_ERROR;

// defined in lklfs_vnode_ops.c
extern fs_vnode_ops lklfs_vnode_ops;


static status_t
lklfs_unmount(fs_volume * _volume)
{
	return lklfs_umount_impl(_volume->private_volume);
}


static status_t
lklfs_read_fs_info(fs_volume * volume, struct fs_info * info)
{
	struct lh_fs_info fi;
	int rc;
	rc = lklfs_read_fs_info_impl(volume->private_volume, &fi);
	if (rc != 0)
		return lh_to_haiku_error(-rc);

	info->flags		   = (fi.flags & LH_FS_READONLY) ? B_FS_IS_READONLY : 0;
	info->block_size   = fi.block_size;
	info->io_size	   = fi.io_size;
	info->total_blocks = fi.total_blocks;
	info->free_blocks  = fi.free_blocks;
	info->total_nodes  = fi.total_nodes;
	info->free_nodes   = fi.free_nodes;
	strncpy(info->volume_name, fi.volume_name, sizeof(info->volume_name));

	return B_OK;
}


static status_t
lklfs_get_vnode(fs_volume * volume, ino_t id,
	fs_vnode * vnode, int * _type, uint32 * _flags, bool reenter)
{
	vnode->ops = &lklfs_vnode_ops;
	return B_OK;
}


static status_t
lklfs_write_fs_info(fs_volume * volume, const struct fs_info * info, uint32 mask)
{
	NIMPL;
}


static status_t
lklfs_sync(fs_volume * volume)
{
	int rc = lklfs_sync_impl();
	return lh_to_haiku_error(-rc);
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



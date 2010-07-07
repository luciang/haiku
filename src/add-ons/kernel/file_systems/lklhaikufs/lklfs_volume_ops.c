/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <fs_volume.h>
#include <drivers/fs_interface.h>

static status_t
lklfs_unmount(fs_volume* _volume)
{
	return wrap_lkl_umount(_volume->private_volume);
}


static status_t
lklfs_read_fs_info(fs_volume* volume, struct fs_info* info)
{
	return B_ERROR;
}


static status_t
lklfs_get_vnode(fs_volume* volume, ino_t id,
	fs_vnode* vnode, int* _type, uint32* _flags, bool reenter)
{
	return B_ERROR;
}


static status_t
lklfs_write_fs_info(fs_volume* volume, const struct fs_info* info, uint32 mask)
{
	return B_ERROR;
}


static status_t lklfs_sync(fs_volume* volume)
{
	return B_ERROR;
}


fs_volume_ops lklfs_volume_ops = {
	&lklfs_unmount,
	&lklfs_read_fs_info,
	&lklfs_write_fs_info,
	&lklfs_sync,
	&lklfs_get_vnode,
};


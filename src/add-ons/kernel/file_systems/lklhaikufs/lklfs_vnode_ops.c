/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <drivers/fs_interface.h>
#include <KernelExport.h>

#define BRIDGE_HAIKU
#include "lkl-haiku-bridge.h"

#define NIMPL 															\
	do {																\
		dprintf("[[%s]] called -- NOT Implemented yet!\n", __func__);	\
		return B_ERROR;													\
	} while(0)


static status_t lklfs_lookup(fs_volume* volume, fs_vnode* dir,
	const char* name,  ino_t* _id)
{
	NIMPL;
}


static status_t
lklfs_put_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter)
{
	NIMPL;
}


static status_t
lklfs_remove_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter)
{
	NIMPL;
}


static status_t
lklfs_read_symlink(fs_volume* volume, fs_vnode* link, char* buffer,
	size_t* _bufferSize)
{
	NIMPL;
}


static status_t
lklfs_create_symlink(fs_volume* volume, fs_vnode* dir,
	const char* name, const char* path, int mode)
{
	NIMPL;
}

static status_t lklfs_unlink(fs_volume* volume, fs_vnode* dir, const char* name)
{
	NIMPL;
}


static status_t
lklfs_rename(fs_volume* volume, fs_vnode* fromDir,
	const char* fromName, fs_vnode* toDir, const char* toName)
{
	NIMPL;
}


static status_t
lklfs_access(fs_volume* volume, fs_vnode* vnode, int mode)
{
	NIMPL;
}


static status_t
lklfs_read_stat(fs_volume* volume, fs_vnode* vnode,
	struct stat* stat)
{
	NIMPL;
}


static status_t
lklfs_write_stat(fs_volume* volume, fs_vnode* vnode,
	const struct stat* stat, uint32 statMask)
{
	NIMPL;
}


/* file operations */
static status_t
lklfs_create(fs_volume* volume, fs_vnode* dir, const char* name,
	int openMode, int perms, void** _cookie, ino_t* _newVnodeID)
{
	NIMPL;
}


static status_t
lklfs_open(fs_volume* volume, fs_vnode* vnode, int openMode,
	void** _cookie)
{
	NIMPL;
}


static status_t
lklfs_close(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	NIMPL;
}


static status_t
lklfs_free_cookie(fs_volume* volume, fs_vnode* vnode,
	void* cookie)
{
	NIMPL;
}


static status_t
lklfs_read(fs_volume* volume, fs_vnode* vnode, void* cookie,
	off_t pos, void* buffer, size_t* length)
{
	NIMPL;
}


static status_t
lklfs_write(fs_volume* volume, fs_vnode* vnode, void* cookie,
	off_t pos, const void* buffer, size_t* length)
{
	NIMPL;
}


/* directory operations */
static status_t
lklfs_create_dir(fs_volume* volume, fs_vnode* parent,
	const char* name, int perms)
{
	NIMPL;
}


static status_t
lklfs_remove_dir(fs_volume* volume, fs_vnode* parent,
	const char* name)
{
	NIMPL;
}


static status_t
lklfs_open_dir(fs_volume* volume, fs_vnode* vnode,
	void** _cookie)
{
	NIMPL;
}


static status_t
lklfs_close_dir(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	NIMPL;
}


static status_t
lklfs_free_dir_cookie(fs_volume* volume, fs_vnode* vnode,
	void* cookie)
{
	NIMPL;
}


static status_t
lklfs_read_dir(fs_volume* volume, fs_vnode* vnode, void* cookie,
	struct dirent* buffer, size_t bufferSize, uint32* _num)
{
	NIMPL;
}


static status_t
lklfs_rewind_dir(fs_volume* volume, fs_vnode* vnode,
	void* cookie)
{
	NIMPL;
}


fs_vnode_ops lklfs_vnode_ops = {
	/* vnode operations */
	lklfs_lookup,
	NULL, // lklfs_get_vnode_name
	lklfs_put_vnode,
	lklfs_remove_vnode,

	/* VM file access */
	NULL,	// lklfs_can_page
	NULL,	// lklfs_read_pages
	NULL,	// lklfs_write_pages

	NULL,	// io()
	NULL,	// cancel_io()

	NULL,	// lklfs_get_file_map,

	NULL,	// lklfs_ioctl
	NULL,	// lklfs_setflags,
	NULL,	// lklfs_select
	NULL,	// lklfs_deselect
	NULL,	// lklfs_fsync

	lklfs_read_symlink,
	lklfs_create_symlink,

	NULL,	// lklfs_link,
	lklfs_unlink,
	lklfs_rename,

	lklfs_access,
	lklfs_read_stat,
	lklfs_write_stat,

	/* file operations */
	lklfs_create,
	lklfs_open,
	lklfs_close,
	lklfs_free_cookie,
	lklfs_read,
	lklfs_write,

	/* directory operations */
	lklfs_create_dir,
	lklfs_remove_dir,
	lklfs_open_dir,
	lklfs_close_dir,
	lklfs_free_dir_cookie,
	lklfs_read_dir,
	lklfs_rewind_dir,

	/* attribute directory operations */
	NULL,	// lklfs_open_attrdir,
	NULL,	// lklfs_close_attrdir,
	NULL,	// lklfs_free_attrdircookie,
	NULL,	// lklfs_read_attrdir,
	NULL,	// lklfs_rewind_attrdir,

	/* attribute operations */
	NULL,	// lklfs_create_attr
	NULL,	// lklfs_open_attr_h,
	NULL,	// lklfs_close_attr_h,
	NULL,	// lklfs_free_attr_cookie_h,
	NULL,	// lklfs_read_attr_h,
	NULL,	// lklfs_write_attr_h,

	NULL,	// lklfs_read_attr_stat_h,
	NULL,	// lklfs_write_attr_stat
	NULL,	// lklfs_rename_attr
	NULL,	// lklfs_remove_attr
};


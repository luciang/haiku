/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <drivers/fs_interface.h>
#include <KernelExport.h>
#include <stdlib.h>
#include <string.h>
#include <posix/dirent.h>
#include "util.h"

#define BRIDGE_HAIKU
#include "lkl-haiku-bridge.h"
#include "lh-fcntl.h"
#include "lh-error.h"


#define NIMPL 															\
	do {																\
		dprintf("[[%s]] called -- NOT Implemented yet!\n", __func__);	\
		return B_ERROR;													\
	} while(0)



extern status_t get_vnode_set_private_node(fs_volume* volume, ino_t vnodeID, void* privateNode);

static status_t lklfs_lookup(fs_volume* volume, fs_vnode* dir,
	const char* name,  ino_t* _id)
{
	ino_t ino;
	char * filePath = pathJoin(dir->private_node, name);
	if (filePath == NULL)
		return B_NO_MEMORY;

	ino = lklfs_get_ino(volume->private_volume, filePath);
	if (ino < 0)
		return lh_to_haiku_error(-ino);

	*_id = ino;

	// TODO: FIXME: get_vnode + the get_vnode hook do not permit
	// us to put the file path into the fs_vnode->private_node field.
	// get_vnode_set_private_node is a new function exported that
	// permits setting the private_node field in a safe manner.

	// return get_vnode(volume, ino, &private_node);
	return get_vnode_set_private_node(volume, ino, filePath);
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
	int rc;
	rc = lklfs_access_impl(volume->private_volume, vnode->private_node, mode);
	if (rc != 0)
		return lh_to_haiku_error(-rc);

	return B_OK;
}


static status_t
lklfs_read_stat(fs_volume* volume, fs_vnode* vnode,
	struct stat* stat)
{
	struct lh_stat ls;
	int rc;
	rc = lklfs_read_stat_impl(volume->private_volume, vnode->private_node, &ls);
	if (rc != 0)
		return lh_to_haiku_error(-rc);

	stat->st_mode	 = ls.st_mode;
	stat->st_nlink	 = ls.st_nlink;
	stat->st_uid	 = ls.st_uid;
	stat->st_gid	 = ls.st_gid;
	stat->st_size	 = ls.st_size;
	stat->st_blksize = ls.st_blksize;
	stat->st_blocks	 = ls.st_blocks;
	stat->st_atim.tv_sec	 = ls.st_atim;
	stat->st_atim.tv_nsec	 = ls.st_atim_nsec;
	stat->st_mtim.tv_sec	 = ls.st_mtim;
	stat->st_mtim.tv_nsec	 = ls.st_mtim_nsec;
	stat->st_ctim.tv_sec	 = ls.st_ctim;
	stat->st_ctim.tv_nsec	 = ls.st_ctim_nsec;
	stat->st_crtim.tv_sec	 = ls.st_crtim;
	stat->st_crtim.tv_nsec	 = ls.st_crtim_nsec;

	return B_OK;
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
	int rc;

	if ((openMode & O_NOTRAVERSE) != 0) {
		// FIXME: currently a cookie associated with a opened file is
		// the LKL file descriptor to that file. The LKL system call
		// API cannot at this moment open file descriptor for symlinks.
		dprintf("lklfs_open(%s, O_NOTRAVERSE) called. NOT Implemented yet!\n",
			(char *) vnode->private_node);
		return B_BAD_VALUE;
	}

	if ((openMode & O_TEMPORARY) != 0) {
		// FIXME: this does not seem to be used in Haiku at this
		// moment, and I don't know how to enforce this behavior
		// through LKL's system call API.
		//
		// As I cannot enforce this upon each Linux file system
		// implementation I think this will not be implemented in this
		// driver.
		//
		// TODO: TBD: Other drivers simply ignore this flag and succeed
		// regardless. Should we do the same? Is this flag going to be
		// managed at Haiku's VFS layer or deep down in each file
		// system add-on?
		dprintf("lklfs_open(%s, O_TEMPORARY) called. NOT Implemented yet!\n",
			(char *) vnode->private_node);
		return B_BAD_VALUE;
	}

	rc = lklfs_open_impl(volume->private_volume, vnode->private_node,
		haiku_to_lh_openMode(openMode), _cookie);
	if (rc != 0)
		return lh_to_haiku_error(-rc);

	return B_OK;
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


//	Directory cookies are in fact the LKL file descriptor values
static status_t
lklfs_open_dir(fs_volume* volume, fs_vnode* vnode,
	void** _cookie)
{
	int rc;
	rc = lklfs_open_dir_impl(volume->private_volume, vnode->private_node, _cookie);
	if (rc != 0)
		return lh_to_haiku_error(-rc);

	return B_OK;
}


static status_t
lklfs_close_dir(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	int rc = lklfs_close_dir_impl(cookie);
	if (rc != 0)
		return lh_to_haiku_error(-rc);

	return B_OK;
}


static status_t
lklfs_free_dir_cookie(fs_volume* volume, fs_vnode* vnode,
	void* cookie)
{
	// nothing was allocated for the cookie => nothing to be freed
	// the cookie is in fact a the value of a file descriptor
	// coresponding to the opened directory in LKL.
	return B_OK;
}


static status_t
lklfs_read_dir(fs_volume* volume, fs_vnode* vnode, void* cookie,
	struct dirent* buffer, size_t bufferSize, uint32* pnum)
{
	int i, num;
	struct lh_dirent * ld;
	size_t remaining = bufferSize;

	// we allocate a similarly sized buffer in which we read the only
	// info we're interested in from the Linux kernel
	ld = (struct lh_dirent *) malloc(bufferSize);
	if (ld == NULL)
		return B_NO_MEMORY;

	num = lklfs_read_dir_impl(cookie, ld, bufferSize);
	if (num < 0) {
		free(ld);
		return lh_to_haiku_error(-num);
	}

	if (num == 0) {
		*pnum = 0;
		free(ld);
		return B_OK;
	}



	for(i = 0; (i < num) && (remaining > 0); i++) {
		// TODO: check for nodes that are mount points for other volumes
		// If one dentry is a mount point for another volume, I think
		// we must return the dev_t corresponding to the other volume.
		buffer->d_dev = volume->id;
		buffer->d_ino = ld[i].d_ino;

		// space for '\0' is aleady allocated in 'dirent':
		// the last field is 'char d_name [1]'.
		buffer->d_reclen = sizeof(struct dirent) + strlen(ld[i].d_name);
		strncpy((char *) &buffer->d_name, ld[i].d_name, remaining);
		remaining -= buffer->d_reclen;
		buffer = (struct dirent *) ((char *) buffer + buffer->d_reclen);
	}

	*pnum = i;

	free(ld);
	return B_OK;
}


static status_t
lklfs_rewind_dir(fs_volume* volume, fs_vnode* vnode,
	void* cookie)
{
	int rc;
	rc = lklfs_rewind_dir_impl(cookie);
	if (rc != 0)
		return lh_to_haiku_error(-rc);

	return B_OK;
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


/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <drivers/fs_interface.h>
#include <fs_volume.h>
#include <syscalls.h>
#include <OS.h>
#include <KernelExport.h>
#include <Debug.h>

#define BRIDGE_HAIKU
#include "lkl-haiku-bridge.h"


// We can't include Haiku's headers directly because Haiku and LKL
// define types with the same name in different ways.
extern int lkl_env_init(int memsize);
extern void lkl_env_fini(void);

const int LKL_MEMORY_SIZE = 64 * 1024 * 1024; // 64MiB


static status_t
lklfs_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			// boot LKL
			lkl_env_init(LKL_MEMORY_SIZE);
			return B_OK;
		case B_MODULE_UNINIT:
			// shutdown LKL
			lkl_env_fini();
			dprintf("[lklfs] std ops UNinit -- kernel shut down\n");
			return B_OK;
		default:
			dprintf("[lklfs] STD_OPS default -- unknown command!\n");
			return B_ERROR;
	}
}


/*! Check whether LKL can mount this file system.

	@_cookie will hold a pointer to a structure defined by this driver
	to identify the file system and will be passed to other functions.
*/
static float
lklfs_identify_partition(int fd, partition_data* partition, void** _cookie)
{
	int rc;
	rc = cookie_lklfs_identify_partition(fd, partition->size, _cookie);
	dprintf("lklfs_identify_partition: rc=%d\n", rc);
	if (rc != 0)
		return -1;

	// most Haiku file systems return 0.8f. If there's a native driver
	// that can handle this partition type, we give it priority to by
	// returning something less than 0.8f */
	return 0.6f;
}


// Get information about the current partition.
static status_t
lklfs_scan_partition(int fd, partition_data* p, void* _cookie)
{
	lklfs_partition_id * part = (lklfs_partition_id*) _cookie;
	if (part == NULL || part->content_name == NULL)
		return B_NO_MEMORY;

	p->status		= B_PARTITION_VALID;
	p->flags		|= B_PARTITION_FILE_SYSTEM;
	p->content_size	= part->content_size;;
	p->block_size	= part->block_size;
	p->content_name	= part->content_name;

	return B_OK;
}


// Free the cookie related information allocated at by lklfs_identify_partition()
static void
lklfs_free_identify_partition_cookie(partition_data* partition, void* _cookie)
{
	free(_cookie);
}


static status_t
lklfs_unmount(fs_volume * _volume)
{
	return wrap_lkl_umount(_volume->private_volume);
}


static status_t
lklfs_read_fs_info(fs_volume * volume, struct fs_info * info)
{
	return B_ERROR;
}


static status_t
lklfs_get_vnode(fs_volume * volume, ino_t id,
	fs_vnode * vnode, int * _type, uint32 * _flags, bool reenter)
{
	return B_ERROR;
}


static status_t
lklfs_write_fs_info(fs_volume * volume, const struct fs_info * info, uint32 mask)
{
	return B_ERROR;
}


static status_t lklfs_sync(fs_volume * volume)
{
	return B_ERROR;
}


fs_volume_ops gLklfsVolumeOps = {
	&lklfs_unmount,
	&lklfs_read_fs_info,
	&lklfs_write_fs_info,
	&lklfs_sync,
	&lklfs_get_vnode,
};


// Mount a the LKL managed file system into Haiku's VFS
static status_t
lklfs_mount(fs_volume * _volume, const char * device, uint32 flags,
	const char * args, ino_t * _rootID)
{
	status_t rc;
	int fd = -1;
	int open_flags = O_NOCACHE;
	struct stat st;

	open_flags |= ((flags & B_MOUNT_READ_ONLY) != 0) ? O_RDONLY : O_RDWR;
	fd = open(device, O_NOCACHE | open_flags);
	if (fd < B_OK)
		return fd;

	rc = _kern_read_stat(fd, NULL, 0, &st, sizeof(struct stat));
	if (rc != B_OK) {
		close(fd);
		return rc;
	}

	_volume->private_volume = wrap_lkl_mount(fd, st.st_size, flags & B_MOUNT_READ_ONLY);
	if (_volume->private_volume == NULL)
		return B_ERROR;

	_volume->ops = &gLklfsVolumeOps;
	dprintf("lklfs_mount:: stuff succeeded\n");
	return B_OK;
}


static uint32
lklfs_get_supported_operations(partition_data * partition, uint32 mask)
{
	dprintf("[lklfs] lklfs_get_supported_operations\n");
	return B_DISK_SYSTEM_SUPPORTS_INITIALIZING
		| B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME
		| B_DISK_SYSTEM_SUPPORTS_WRITING;
}


static status_t
lklfs_initialize(int fd, partition_id partitionID, const char * name,
	const char * parameterString, off_t partitionSize, disk_job_id job)
{
	dprintf("[lklfs] lklfs_initialize\n");
	return B_OK;
}


static file_system_module_info sBeFileSystem = {
	{
		"file_systems/lklhaikufs" B_CURRENT_FS_API_VERSION,
		0,
		lklfs_std_ops,
	},

	"lklhaikufs",						// short_name
	"LKL Haiku File System",			// pretty_name

	// DDM flags
	0
//	| B_DISK_SYSTEM_SUPPORTS_CHECKING
//	| B_DISK_SYSTEM_SUPPORTS_REPAIRING
//	| B_DISK_SYSTEM_SUPPORTS_RESIZING
//	| B_DISK_SYSTEM_SUPPORTS_MOVING
//	| B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_NAME
//	| B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_PARAMETERS
	| B_DISK_SYSTEM_SUPPORTS_INITIALIZING
	| B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME
//	| B_DISK_SYSTEM_SUPPORTS_DEFRAGMENTING
//	| B_DISK_SYSTEM_SUPPORTS_DEFRAGMENTING_WHILE_MOUNTED
//	| B_DISK_SYSTEM_SUPPORTS_CHECKING_WHILE_MOUNTED
//	| B_DISK_SYSTEM_SUPPORTS_REPAIRING_WHILE_MOUNTED
//	| B_DISK_SYSTEM_SUPPORTS_RESIZING_WHILE_MOUNTED
//	| B_DISK_SYSTEM_SUPPORTS_MOVING_WHILE_MOUNTED
//	| B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_NAME_WHILE_MOUNTED
//	| B_DISK_SYSTEM_SUPPORTS_SETTING_CONTENT_PARAMETERS_WHILE_MOUNTED
	| B_DISK_SYSTEM_SUPPORTS_WRITING
	,

	// scanning
	lklfs_identify_partition,
	lklfs_scan_partition,
	lklfs_free_identify_partition_cookie,
	NULL,	// free_partition_content_cookie()

	&lklfs_mount,

	/* capability querying operations */
	&lklfs_get_supported_operations,

	NULL,	// validate_resize
	NULL,	// validate_move
	NULL,	// validate_set_content_name
	NULL,	// validate_set_content_parameters
	NULL,	// validate_initialize,

	/* shadow partition modification */
	NULL,	// shadow_changed

	/* writing */
	NULL,	// defragment
	NULL,	// repair
	NULL,	// resize
	NULL,	// move
	NULL,	// set_content_name
	NULL,	// set_content_parameters
	lklfs_initialize,
};


module_info * modules[] = {
	(module_info *) &sBeFileSystem,
	NULL,
};


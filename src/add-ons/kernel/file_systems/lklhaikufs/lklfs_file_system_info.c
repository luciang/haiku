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
#include <KernelExport.h>
#include <Debug.h>
#include <string.h>

#define BRIDGE_HAIKU
#include "lkl-haiku-bridge.h"

// defined in lklfs_volume_ops.c
extern fs_volume_ops lklfs_volume_ops;
// defined in lklfs_vnode_ops.c
extern fs_vnode_ops lklfs_vnode_ops;
// defined in kernel_interface.c
extern status_t lklfs_std_ops(int32 op, ...);



/*! Check whether LKL can mount this file system.

	@_cookie will hold a pointer to a structure defined by this driver
	to identify the file system and will be passed to other functions.
*/
static float
lklfs_identify_partition(int fd, partition_data* partition, void** _cookie)
{
	int rc;
	rc = lklfs_identify_partition_impl(fd, partition->size, _cookie);
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
	lklfs_partition_id* part = (lklfs_partition_id*) _cookie;
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


// Mount a the LKL managed file system into Haiku's VFS
static status_t
lklfs_mount(fs_volume* _volume, const char* device, uint32 flags,
	const char* args, ino_t* _rootID)
{
	status_t rc;
	int fd = -1;
	int open_flags = O_NOCACHE;
	int readonly = (flags & B_MOUNT_READ_ONLY) != 0;
	struct stat st;
	lklfs_vnode* root_vnode = lklfs_vnode_create_root_vnode();
	if (root_vnode == NULL)
		return B_NO_MEMORY;

	open_flags |= readonly ? O_RDONLY : O_RDWR;
	fd = open(device, O_NOCACHE | open_flags);
	if (fd < B_OK) {
		lklfs_vnode_free(root_vnode);
		return fd;
	}

	rc = _kern_read_stat(fd, NULL, 0, &st, sizeof(struct stat));
	if (rc != B_OK) {
		close(fd);
		lklfs_vnode_free(root_vnode);
		return rc;
	}

	_volume->ops = &lklfs_volume_ops;
	_volume->private_volume = lklfs_mount_impl(fd, st.st_size, readonly);
	if (_volume->private_volume == NULL) {
		close(fd);
		lklfs_vnode_free(root_vnode);
		return B_ERROR;
	}

	*_rootID = lklfs_get_ino(_volume->private_volume, root_vnode);
	rc = publish_vnode(_volume, *_rootID, root_vnode,
		&lklfs_vnode_ops, S_IFDIR, 0);
	if (rc < B_OK) {
		// TODO: FIXME: umount?
		close(fd);
		lklfs_vnode_free(root_vnode);
		return rc;
	}

	return B_OK;
}


static uint32
lklfs_get_supported_operations(partition_data* partition, uint32 mask)
{
	return B_DISK_SYSTEM_SUPPORTS_INITIALIZING
		| B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME
		| B_DISK_SYSTEM_SUPPORTS_WRITING;
}


file_system_module_info lklfs_file_system_info = {
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
	NULL,	// initialize
};

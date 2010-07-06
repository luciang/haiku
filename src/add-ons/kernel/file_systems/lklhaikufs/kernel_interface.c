/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <drivers/fs_interface.h>
#include <OS.h>
#include <KernelExport.h>
#include <Debug.h>

extern int lkl_env_init(int memsize);
extern void lkl_env_fini(void);
extern int cookie_lklfs_identify_partition(int fd, off_t size, void** _cookie);
extern int cookie_lklfs_scan_partition(void* _cookie, off_t* p_content_size, uint32* p_block_size,
									   char** p_content_name);
extern void cookie_lklfs_free_cookie(void* _cookie);

static status_t
lklfs_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			lkl_env_init(64*1024*1024);
			return B_OK;
		case B_MODULE_UNINIT:
			lkl_env_fini();
			return B_OK;
		default:
			return B_ERROR;
	}
}


static float
lklfs_identify_partition(int fd, partition_data* partition, void** _cookie)
{
	int rc;
	rc = cookie_lklfs_identify_partition(fd, partition->size, _cookie);
	if (rc != 0)
		return -1;

	/* most Haiku file systems return 0.8f. We give priority to native
	   file system drivers returning something less than 0.8f */
	return 0.6f;
}


static status_t
lklfs_scan_partition(int fd, partition_data* p, void* _cookie)
{
	int rc;
	rc = cookie_lklfs_scan_partition(_cookie, &p->content_size,
		 &p->block_size, &p->content_name);
	if (rc == -1)
		return B_ERROR;
	if (p->content_name == NULL)
		return B_NO_MEMORY;
	p->status = B_PARTITION_VALID;
	p->flags |= B_PARTITION_FILE_SYSTEM;
	return B_OK;
}

static void
lklfs_free_identify_partition_cookie(partition_data* partition, void* _cookie)
{
	cookie_lklfs_free_cookie(_cookie);
}


static status_t
lklfs_mount(fs_volume* _volume, const char* device, uint32 flags,
	const char* args, ino_t* _rootID)
{
	return B_BAD_VALUE;
}


static uint32
lklfs_get_supported_operations(partition_data* partition, uint32 mask)
{
	// TODO: We should at least check the partition size.
	return B_DISK_SYSTEM_SUPPORTS_INITIALIZING
		| B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME
		| B_DISK_SYSTEM_SUPPORTS_WRITING;
}


static status_t
lklfs_initialize(int fd, partition_id partitionID, const char* name,
	const char* parameterString, off_t partitionSize, disk_job_id job)
{
	return B_OK;
}


static file_system_module_info sLklFileSystem = {
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


module_info *modules[] = {
	(module_info *)&sLklFileSystem,
	NULL,
};


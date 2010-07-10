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

extern int wrap_lkl_env_init(int memsize);
extern void wrap_lkl_env_fini(void);
extern int wrap_lkl_identify_partition(int fd, unsigned int size);


static status_t
lklfs_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			dprintf("[lklfs] std ops init -- before kernel boot\n");
			wrap_lkl_env_init(64*1024*1024);
			dprintf("[lklfs] std ops init -- kernel booted\n");
			return B_OK;
		case B_MODULE_UNINIT:
			dprintf("[lklfs] std ops UNinit -- before kernel shutdown\n");
			wrap_lkl_env_fini();
			dprintf("[lklfs] std ops UNinit -- kernel shut down\n");
			return B_OK;
		default:
			dprintf("[lklfs] std ops default\n");
			return B_ERROR;
	}
}


static float
lklfs_identify_partition(int fd, partition_data* partition, void** _cookie)
{
	int rc;
	rc = wrap_lkl_identify_partition(fd, partition->size);
	if (rc != 0)
		return -1;
	return 0.8f;
}


static status_t
lklfs_scan_partition(int fd, partition_data* partition, void* _cookie)
{
	dprintf("[lklfs] lklfs_scan_partition\n");
	return B_NO_MEMORY;
}

static void
lklfs_free_identify_partition_cookie(partition_data* partition, void* _cookie)
{
	dprintf("[lklfs] lklfs_free_identify_partition_cookie\n");
}
static status_t
lklfs_mount(fs_volume* _volume, const char* device, uint32 flags,
	const char* args, ino_t* _rootID)
{
	dprintf("[lklfs] lklfs_mount\n");
	return B_BAD_VALUE;
}

static uint32
lklfs_get_supported_operations(partition_data* partition, uint32 mask)
{
	dprintf("[lklfs] lklfs_get_supported_operations\n");
	// TODO: We should at least check the partition size.
	return B_DISK_SYSTEM_SUPPORTS_INITIALIZING
		| B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME
		| B_DISK_SYSTEM_SUPPORTS_WRITING;
}

static status_t
lklfs_initialize(int fd, partition_id partitionID, const char* name,
	const char* parameterString, off_t partitionSize, disk_job_id job)
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

module_info *modules[] = {
	(module_info *)&sBeFileSystem,
	NULL,
};


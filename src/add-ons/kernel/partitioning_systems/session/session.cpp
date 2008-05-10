/*
 * Copyright 2003 Tyler Akidau, haiku@akidau.net
 * Distributed under the terms of the MIT License.
 */

/*!
	\file session.cpp
	\brief Disk device manager partition module for CD/DVD sessions.
*/

#include <unistd.h>

#include <ddm_modules.h>
#include <DiskDeviceTypes.h>
#include <KernelExport.h>

#include "Debug.h"
#include "Disc.h"


#define SESSION_PARTITION_MODULE_NAME "partitioning_systems/session/v1"
#define SESSION_PARTITION_NAME "Multisession Storage Device"


static status_t
standard_operations(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
	}

	return B_ERROR;
}


static float
identify_partition(int fd, partition_data *partition, void **cookie)
{
	DEBUG_INIT_ETC(NULL, ("fd: %d, id: %ld, offset: %Ld, "
		"size: %Ld, block_size: %ld, flags: 0x%lx", fd,
		partition->id, partition->offset, partition->size,
		partition->block_size, partition->flags));

	device_geometry geometry;
	float result = -1;
	if (partition->flags & B_PARTITION_IS_DEVICE
		&& partition->block_size == 2048
		&& ioctl(fd, B_GET_GEOMETRY, &geometry) == 0
		&& geometry.device_type == B_CD) {
		Disc *disc = new Disc(fd);
		if (disc && disc->InitCheck() == B_OK) {
			*cookie = static_cast<void*>(disc);
			result = 0.7;		
		} 	
	}
	PRINT(("returning %ld\n", int32(result * 10000)));
	return result;
}


static status_t
scan_partition(int fd, partition_data *partition, void *cookie)
{
	DEBUG_INIT_ETC(NULL, ("fd: %d, id: %ld, offset: %Ld, size: %Ld, "
		"block_size: %ld, cookie: %p", fd, partition->id, partition->offset,
		partition->size, partition->block_size, cookie));

	Disc *disc = static_cast<Disc*>(cookie);
	partition->status = B_PARTITION_VALID;
	partition->flags |= B_PARTITION_PARTITIONING_SYSTEM
		| B_PARTITION_READ_ONLY;
	partition->content_size = partition->size;

	Session *session = NULL;
	status_t error = B_OK;
	for (int i = 0; (session = disc->GetSession(i)); i++) {	
		partition_data *child = create_child_partition(partition->id,
			i, -1);
		if (!child) {
			PRINT(("Unable to create child at index %d.\n", i));
			// something went wrong
			error = B_ERROR;
			break;
		}
		child->offset = partition->offset + session->Offset();
		child->size = session->Size();
		child->block_size = session->BlockSize();
		child->flags |= session->Flags();
		child->type = strdup(session->Type());
		if (!child->type) {
			error = B_NO_MEMORY;
			break;
		}
		child->parameters = NULL;
	}
	PRINT(("error: 0x%lx, `%s'\n", error, strerror(error)));
	RETURN(error);
}


static void
free_identify_partition_cookie(partition_data */*partition*/, void *cookie)
{
	DEBUG_INIT_ETC(NULL, ("cookie: %p", cookie));
	delete static_cast<Disc*>(cookie);
}


static partition_module_info sSessionModule = {
	{
		SESSION_PARTITION_MODULE_NAME,
		0,
		standard_operations
	},
	"session",							// short_name
	SESSION_PARTITION_NAME,				// pretty_name
	0,									// flags

	// scanning
	identify_partition,					// identify_partition
	scan_partition,						// scan_partition
	free_identify_partition_cookie,		// free_identify_partition_cookie
	NULL,								// free_partition_cookie
	NULL,								// free_partition_content_cookie
};

partition_module_info *modules[] = {
	&sSessionModule,
	NULL
};


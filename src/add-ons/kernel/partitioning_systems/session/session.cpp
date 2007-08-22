//----------------------------------------------------------------------
//  This software is part of the Haiku distribution and is covered 
//  by the MIT license.
//
//  Copyright (c) 2003 Tyler Dauwalder, tyler@dauwalder.net
//---------------------------------------------------------------------
/*!
	\file session.cpp
	\brief Disk device manager partition module for CD/DVD sessions.
*/

#include <ddm_modules.h>
#include <DiskDeviceTypes.h>
#include <KernelExport.h>
#include <unistd.h>

#include "Debug.h"
#include "Disc.h"

#define SESSION_PARTITION_MODULE_NAME "partitioning_systems/session/v1"
#define SESSION_PARTITION_NAME "Multisession Storage Device"

static status_t standard_operations(int32 op, ...);
static float identify_partition(int fd, partition_data *partition,
                                void **cookie);
static status_t scan_partition(int fd, partition_data *partition,
                               void *cookie);
static void free_identify_partition_cookie(partition_data *partition,
                                           void *cookie);
static void free_partition_cookie(partition_data *partition);
static void free_partition_content_cookie(partition_data *partition);


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
	    && geometry.device_type == B_CD)
	{
		Disc *disc = new Disc(fd);
		if (disc && disc->InitCheck() == B_OK) {
			*cookie = static_cast<void*>(disc);
			result = 0.5;		
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

	status_t error = fd >= 0 && partition && cookie ? B_OK : B_BAD_VALUE;
	if (!error) {
		Disc *disc = static_cast<Disc*>(cookie);
		partition->status = B_PARTITION_VALID;
		partition->flags |= B_PARTITION_PARTITIONING_SYSTEM
							| B_PARTITION_READ_ONLY;
		partition->content_size = partition->size;
		partition->content_cookie = disc;
		
		Session *session = NULL;
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
			child->cookie = session;
		}
	}
	// cleanup on error
	if (error) {
		delete static_cast<Disc*>(cookie);
		partition->content_cookie = NULL;
		for (int32 i = 0; i < partition->child_count; i++) {
			if (partition_data *child = get_child_partition(partition->id, i)) {
				delete static_cast<Session *>(child->cookie);
				child->cookie = NULL;
			}
		}
	}
	PRINT(("error: 0x%lx, `%s'\n", error, strerror(error)));
	RETURN(error);
}


static void
free_identify_partition_cookie(partition_data */*partition*/, void *cookie)
{
	if (cookie) {
		DEBUG_INIT_ETC(NULL, ("cookie: %p", cookie));
		delete static_cast<Disc*>(cookie);
	}
}


static void
free_partition_cookie(partition_data *partition)
{
	if (partition && partition->cookie) {
		DEBUG_INIT_ETC(NULL, ("partition->cookie: %p", partition->cookie));
		delete static_cast<Session *>(partition->cookie);
		partition->cookie = NULL;
	}
}


static void
free_partition_content_cookie(partition_data *partition)
{
	if (partition && partition->content_cookie) {
		DEBUG_INIT_ETC(NULL, ("partition->content_cookie: %p", partition->content_cookie));
		delete static_cast<Disc*>(partition->content_cookie);
		partition->content_cookie = NULL;
	}
}


static partition_module_info sSessionModule = {
	{
		SESSION_PARTITION_MODULE_NAME,
		0,
		standard_operations
	},
	SESSION_PARTITION_NAME,				// pretty_name
	0,									// flags

	// scanning
	identify_partition,					// identify_partition
	scan_partition,						// scan_partition
	free_identify_partition_cookie,		// free_identify_partition_cookie
	free_partition_cookie,				// free_partition_cookie
	free_partition_content_cookie,		// free_partition_content_cookie
};

partition_module_info *modules[] = {
	&sSessionModule,
	NULL
};


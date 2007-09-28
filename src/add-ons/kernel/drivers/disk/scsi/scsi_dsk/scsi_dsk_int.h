/*
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*
	Part of Open SCSI Disk Driver

	SCSI Direct Access Storage Device Driver (aka SCSI Disk Driver)
*/

#include <device_manager.h>
#include "scsi_dsk.h"
#include <scsi_periph.h>
#include <block_io.h>

#define debug_level_flow 0
#define debug_level_info 1
#define debug_level_error 2

#define DEBUG_MSG_PREFIX "SCSI_DSK -- "

#include "wrapper.h"


typedef struct das_device_info {
	device_node_handle node;
	scsi_periph_device scsi_periph_device;
	scsi_device scsi_device;
	scsi_device_interface *scsi;
	block_io_device block_io_device;
	
	uint64 capacity;
	uint32 block_size;

	bool removable;			// true, if device is removable	
} das_device_info;
	
typedef struct das_handle_info {
	scsi_periph_handle scsi_periph_handle;
	das_device_info *device;
} das_handle_info;

extern scsi_periph_interface *scsi_periph;
extern device_manager_info *pnp;
extern scsi_periph_callbacks callbacks;
extern block_io_for_driver_interface *gBlockIO;


// device_mgr.c

status_t das_init_device(device_node_handle node, void *user_cookie, void **cookie);
status_t das_uninit_device(das_device_info *device);
status_t das_device_added(device_node_handle node);


// handle_mgr.c

status_t das_open(das_device_info *device, das_handle_info **handle_out);
status_t das_close(das_handle_info *handle);
status_t das_free(das_handle_info *handle);


// scsi_dsk.c

void das_set_capacity(das_device_info *device, uint64 capacity,
	uint32 block_size);

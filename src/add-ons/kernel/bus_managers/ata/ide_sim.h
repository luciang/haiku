/*
** Copyright 2002/03, Thomas Kurschel. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/

/*
	Part of Open IDE bus manager

	Interface between ide and scsi bus manager
*/


#ifndef __IDE_SIM_H__
#define __IDE_SIM_H__


#include "scsi_cmds.h"

extern scsi_for_sim_interface *scsi;
extern scsi_sim_interface ide_sim_module;

// set sense of current ccb
static inline void
set_sense(ide_device_info *device, int sense_key, int sense_asc)
{
	device->new_combined_sense = (sense_key << 16) | sense_asc;
}

// retrieve key from combined sense
static inline uint8
decode_sense_key(uint32 combined_sense)
{
	return (combined_sense >> 16) & 0xff;
}

// retrieve asc from combined sense
static inline uint8
decode_sense_asc(uint32 combined_sense)
{
	return (combined_sense >> 8) & 0xff;
}

// retrieve ascq from combined sense
static inline uint8
decode_sense_ascq(uint32 combined_sense)
{
	return combined_sense & 0xff;
}

// retrieve asc and ascq from combined sense
static inline uint16
decode_sense_asc_ascq(uint32 combined_sense)
{
	return combined_sense & 0xffff;
}

void finish_request(ata_request *request, bool resubmit);
void finish_reset_queue(ata_request *request);
void finish_retry(ata_request *request);
void finish_all_requests(ide_device_info *device, ata_request *ignore,
		int subsys_status, bool resubmit);
void finish_checksense(ata_request *request);


// start ccb by resetting sense
static inline void
start_request(ide_device_info *device, ata_request *request)
{
	device->new_combined_sense = 0;
	device->subsys_status = SCSI_REQ_CMP;
	request->ccb->device_status = SCSI_STATUS_GOOD;
}


void create_sense(ide_device_info *device, scsi_sense *sense);


#endif

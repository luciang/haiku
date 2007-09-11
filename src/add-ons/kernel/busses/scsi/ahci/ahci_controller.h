/*
 * Copyright 2007, Marcus Overhagen. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _AHCI_CONTROLLER_H
#define _AHCI_CONTROLLER_H


#include "ahci_defs.h"
#include "ahci_port.h"


class AHCIController {
public:
				AHCIController(device_node_handle node,
					pci_device_info *pciDevice);
				~AHCIController();

	status_t	Init();
	void		Uninit();

	void		ExecuteRequest(scsi_ccb *request);
	uchar		AbortRequest(scsi_ccb *request);
	uchar		TerminateRequest(scsi_ccb *request);
	uchar		ResetDevice(uchar targetID, uchar targetLUN);

	device_node_handle DeviceNode() { return fNode; }

private:
	bool		IsDevicePresent(uint device);
	status_t	ResetController();
	void		RegsFlush();

static int32	Interrupt(void *data);

	friend class AHCIPort;

private:
	device_node_handle 			fNode;
	pci_device_info*			fPCIDevice;
	uint16						fPCIVendorID;
	uint16						fPCIDeviceID;

	volatile ahci_hba *			fRegs;
	area_id						fRegsArea;
	int							fCommandSlotCount;
	int							fPortCountMax;
	int							fPortCountAvail;
	uint8						fIRQ;
	AHCIPort *					fPort[32];


// --- Instance check workaround begin
	port_id fInstanceCheck;
// --- Instance check workaround end

};


inline void
AHCIController::RegsFlush()
{
	volatile uint32 dummy = fRegs->ghc;
	dummy = dummy;
}


#endif	// _AHCI_CONTROLLER_H

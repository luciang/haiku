/*
 * Copyright 2007-2008, Marcus Overhagen. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _AHCI_PORT_H
#define _AHCI_PORT_H

#include "ahci_defs.h"

class AHCIController;
class sata_request;

class AHCIPort {
public:
				AHCIPort(AHCIController *controller, int index);
				~AHCIPort();

	status_t	Init1();
	status_t	Init2();
	void		Uninit();

	void		Interrupt();
	void		InterruptErrorHandler(uint32 is);


	void		ScsiExecuteRequest(scsi_ccb *request);
	uchar		ScsiAbortRequest(scsi_ccb *request);
	uchar		ScsiTerminateRequest(scsi_ccb *request);
	uchar		ScsiResetDevice();
	void		ScsiGetRestrictions(bool *isATAPI, bool *noAutoSense, uint32 *maxBlocks);

private:
	void		ScsiTestUnitReady(scsi_ccb *request);
	void		ScsiInquiry(scsi_ccb *request);
	void		ScsiReadCapacity(scsi_ccb *request);
	void		ScsiReadWrite(scsi_ccb *request, uint64 lba, size_t sectorCount, bool isWrite);
	void		ScsiSynchronizeCache(scsi_ccb *request);

	void		ExecuteSataRequest(sata_request *request, bool isWrite = false);

	void		ResetDevice();
	status_t	ResetPort(bool forceDeviceReset = false);
	status_t	PostReset();
	void		FlushPostedWrites();
	void		DumpD2HFis();

	void		StartTransfer();
	status_t	WaitForTransfer(int *tfd, bigtime_t timeout);
	void		FinishTransfer();


//	uint8 *		SetCommandFis(volatile command_list_entry *cmd, volatile fis *fis, const void *data, size_t dataSize);
	status_t	FillPrdTable(volatile prd *prdTable, int *prdCount, int prdMax, const void *data, size_t dataSize);
	status_t	FillPrdTable(volatile prd *prdTable, int *prdCount, int prdMax, const physical_entry *sgTable, int sgCount, size_t dataSize);

private:
	AHCIController*			fController;
	int						fIndex;
	volatile ahci_port *	fRegs;
	area_id					fArea;
	spinlock						fSpinlock;
	volatile uint32					fCommandsActive;
	sem_id							fRequestSem;
	sem_id							fResponseSem;
	bool							fDevicePresent;
	bool							fUse48BitCommands;
	uint32							fSectorSize;
	uint64							fSectorCount;
	bool							fIsATAPI;
	bool							fTestUnitReadyActive;
	bool							fResetPort;
	bool							fError;

	volatile fis *					fFIS;
	volatile command_list_entry *	fCommandList;
	volatile command_table *		fCommandTable;
	volatile prd *					fPRDTable;
};

inline void
AHCIPort::FlushPostedWrites()
{
	volatile uint32 dummy = fRegs->cmd;
	dummy = dummy;
}
#endif	// _AHCI_PORT_H

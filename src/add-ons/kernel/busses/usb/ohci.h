/*
 * Copyright 2005-2008, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jan-Rixt Van Hoye
 *		Salvatore Benedetto <salvatore.benedetto@gmail.com>
 *		Michael Lotz <mmlr@mlotz.ch>
 */
#ifndef OHCI_H
#define OHCI_H

#include "usb_private.h"
#include "ohci_hardware.h"
#include <lock.h>

struct pci_info;
struct pci_module_info;
class OHCIRootHub;

typedef struct transfer_data {
	Transfer *					transfer;
	ohci_endpoint_descriptor *	endpoint;
	ohci_general_td *			first_descriptor;
	ohci_general_td *			data_descriptor;
	ohci_general_td *			last_descriptor;
	bool						incoming;
	bool						canceled;
	transfer_data *				link;
} transfer_data;


class OHCI : public BusManager {
public:
									OHCI(pci_info *info, Stack *stack);
									~OHCI();

		status_t					Start();
virtual	status_t					SubmitTransfer(Transfer *transfer);
virtual status_t					CancelQueuedTransfers(Pipe *pipe,
										bool force);

virtual	status_t					NotifyPipeChange(Pipe *pipe,
										usb_change change);

static	status_t					AddTo(Stack *stack);

		// Port operations
		uint8						PortCount() { return fPortCount; };
		status_t					GetPortStatus(uint8 index,
										usb_port_status *status);
		status_t					SetPortFeature(uint8 index, uint16 feature);
		status_t					ClearPortFeature(uint8 index, uint16 feature);

		status_t					ResetPort(uint8 index);

virtual	const char *				TypeName() const { return "ohci"; };

private:
		// Interrupt functions
static	int32						_InterruptHandler(void *data);
		int32						_Interrupt();

		// Transfer functions
		status_t					_AddPendingTransfer(Transfer *transfer,
										ohci_endpoint_descriptor *endpoint,
										ohci_general_td *firstDescriptor,
										ohci_general_td *dataDescriptor,
										ohci_general_td *lastDescriptor,
										bool directionIn);
		status_t					_CancelQueuedIsochronousTransfers(
										Pipe *pipe, bool force);
		status_t					_UnlinkTransfer(transfer_data *transfer);

static	int32						_FinishThread(void *data);
		void						_FinishTransfers();

		status_t					_SubmitRequest(Transfer *transfer);
		status_t					_SubmitTransfer(Transfer *transfer);
		status_t					_SubmitIsochronousTransfer(
										Transfer *transfer);

		void						_SwitchEndpointTail(
										ohci_endpoint_descriptor *endpoint,
										ohci_general_td *first,
										ohci_general_td *last);
		void						_RemoveTransferFromEndpoint(
										transfer_data *transfer);

		// Endpoint related methods
		ohci_endpoint_descriptor *	_AllocateEndpoint();
		void						_FreeEndpoint(
										ohci_endpoint_descriptor *endpoint);
		status_t					_InsertEndpointForPipe(Pipe *pipe);
		status_t					_RemoveEndpointForPipe(Pipe *pipe);
		ohci_endpoint_descriptor *	_FindInterruptEndpoint(uint8 interval);

		// Transfer descriptor related methods
		ohci_general_td *			_CreateGeneralDescriptor(
										size_t bufferSize);
		void						_FreeGeneralDescriptor(
										ohci_general_td *descriptor);

		status_t					_CreateDescriptorChain(
										ohci_general_td **firstDescriptor,
										ohci_general_td **lastDescriptor,
										uint32 direction,
										size_t bufferSize);
		void						_FreeDescriptorChain(
										ohci_general_td *topDescriptor);

		size_t						_WriteDescriptorChain(
										ohci_general_td *topDescriptor,
										iovec *vector, size_t vectorCount);
		size_t						_ReadDescriptorChain(
										ohci_general_td *topDescriptor,
										iovec *vector, size_t vectorCount);
		size_t						_ReadActualLength(
										ohci_general_td *topDescriptor);

		void						_LinkDescriptors(ohci_general_td *first,
										ohci_general_td *second);

		ohci_isochronous_td *		_CreateIsochronousDescriptor();
		void						_FreeIsochronousDescriptor(
										ohci_isochronous_td *descriptor);

		// Private locking
		bool						_LockEndpoints();
		void						_UnlockEndpoints();

		// Register functions
inline	void						_WriteReg(uint32 reg, uint32 value);
inline	uint32						_ReadReg(uint32 reg);

		// Debug functions
		void						_PrintEndpoint(
										ohci_endpoint_descriptor *endpoint);
		void						_PrintDescriptorChain(
										ohci_general_td *topDescriptor);

static	pci_module_info *			sPCIModule;
		pci_info *					fPCIInfo;
		Stack *						fStack;

		uint8 *						fOperationalRegisters;
		area_id						fRegisterArea;

		// Host Controller Communication Area related stuff
		area_id						fHccaArea;
		ohci_hcca *					fHcca;
		ohci_endpoint_descriptor **	fInterruptEndpoints;

		// Endpoint management
		mutex						fEndpointLock;
		ohci_endpoint_descriptor *	fDummyControl;
		ohci_endpoint_descriptor *	fDummyBulk;
		ohci_endpoint_descriptor *	fDummyIsochronous;

		// Maintain a linked list of transfer
		transfer_data *				fFirstTransfer;
		transfer_data *				fLastTransfer;
		sem_id						fFinishTransfersSem;
		thread_id					fFinishThread;
		bool						fStopFinishThread;
		Pipe *						fProcessingPipe;

		// Root Hub
		OHCIRootHub *				fRootHub;
		uint8						fRootHubAddress;

		// Port management
		uint8						fPortCount;
};


class OHCIRootHub : public Hub {
public:
									OHCIRootHub(Object *rootObject,
										int8 deviceAddress);

static	status_t					ProcessTransfer(OHCI *ohci,
										Transfer *transfer);
};


#endif // OHCI_H

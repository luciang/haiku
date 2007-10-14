/*
 * Copyright 2005-2008, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *             Jan-Rixt Van Hoye
 *             Salvatore Benedetto <salvatore.benedetto@gmail.com>
 */

#include <module.h>
#include <PCI.h>
#include <USB3.h>
#include <KernelExport.h>
#include <stdlib.h>

#include "ohci.h"

pci_module_info *OHCI::sPCIModule = NULL;
 

static int32
ohci_std_ops( int32 op , ... )
{
	switch (op)	{
		case B_MODULE_INIT:
			TRACE(("usb_ohci_module: init module\n"));
			return B_OK;
		case B_MODULE_UNINIT:
			TRACE(("usb_ohci_module: uninit module\n"));
			return B_OK;
	}

	return EINVAL;
}


host_controller_info ohci_module = {
	{
		"busses/usb/ohci",
		0,
		ohci_std_ops
	},
	NULL,
	OHCI::AddTo
};


module_info *modules[] = {
	(module_info *) &ohci_module,
	NULL
};


//------------------------------------------------------
//	OHCI:: 	Reverse the bits in a value between 0 and 31
//			(Section 3.3.2) 
//------------------------------------------------------
static uint8 revbits[OHCI_NUMBER_OF_INTERRUPTS] =
  { 0x00, 0x10, 0x08, 0x18, 0x04, 0x14, 0x0c, 0x1c,
    0x02, 0x12, 0x0a, 0x1a, 0x06, 0x16, 0x0e, 0x1e,
    0x01, 0x11, 0x09, 0x19, 0x05, 0x15, 0x0d, 0x1d,
    0x03, 0x13, 0x0b, 0x1b, 0x07, 0x17, 0x0f, 0x1f };
 

OHCI::OHCI(pci_info *info, Stack *stack)
	:	BusManager(stack),
		fPCIInfo(info),
		fStack(stack),
		fRegisterArea(-1),
		fHccaArea(-1),
		fDummyControl(0),
		fDummyBulk(0),
		fDummyIsochronous(0),
		fRootHub(0),
		fRootHubAddress(0),
		fNumPorts(0)
{
	int i;
	TRACE(("usb_ohci: constructing new BusManager\n"));
	
	fInitOK = false;
	
	fInterruptEndpoints = new(std::nothrow) uint32[OHCI_NUMBER_OF_INTERRUPTS];
	for(i = 0; i < OHCI_NUMBER_OF_INTERRUPTS; i++) //Clear the interrupt list
		fInterruptEndpoints[i] = 0;

	// enable busmaster and memory mapped access 
	uint16 cmd = sPCIModule->read_pci_config(fPCIInfo->bus, fPCIInfo->device, fPCIInfo->function, PCI_command, 2);
	cmd &= ~PCI_command_io;
	cmd |= PCI_command_master | PCI_command_memory;
	sPCIModule->write_pci_config(fPCIInfo->bus, fPCIInfo->device, fPCIInfo->function, PCI_command, 2, cmd );

	//
	// 5.1.1.2 map the registers
	//
	addr_t registeroffset = sPCIModule->read_pci_config(info->bus,
		info->device, info->function, PCI_base_registers, 4);
	registeroffset &= PCI_address_memory_32_mask;	
	TRACE(("OHCI: iospace offset: %lx\n" , registeroffset));
	fRegisterArea = map_physical_memory("OHCI base registers", (void *)registeroffset,
		B_PAGE_SIZE, B_ANY_KERNEL_BLOCK_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_READ_AREA | B_WRITE_AREA, (void **)&fRegisterBase);
	if (fRegisterArea < B_OK) {
		TRACE(("usb_ohci: error mapping the registers\n"));
		return;
	}
	
	//	Get the revision of the controller
	uint32 rev = ReadReg(OHCI_REVISION) & 0xff;
	
	//	Check the revision of the controller. The revision should be 10xh
	TRACE((" OHCI: Version %ld.%ld%s\n", OHCI_REV_HI(rev), OHCI_REV_LO(rev),OHCI_REV_LEGACY(rev) ? ", legacy support" : ""));
	if (OHCI_REV_HI(rev) != 1 || OHCI_REV_LO(rev) != 0) {
		TRACE(("usb_ohci: Unsupported OHCI revision of the ohci device\n"));
		return;
	}
	
	// Set up the Host Controller Communications Area
	void *hcca_phy;
	fHccaArea = fStack->AllocateArea((void**)&fHcca, &hcca_phy,
										B_PAGE_SIZE, "OHCI HCCA");
	if (fHccaArea < B_OK) {
		TRACE(("usb_ohci: Error allocating HCCA block\n"));
		return;
	}
	memset((void*)fHcca, 0, sizeof(ohci_hcca));

	//
	// 5.1.1.3 Take control of the host controller
	//
	if (ReadReg(OHCI_CONTROL) & OHCI_IR) {
		TRACE(("usb_ohci: SMM is in control of the host controller\n"));
		WriteReg(OHCI_COMMAND_STATUS, OHCI_OCR);
		for (int i = 0; i < 100 && (ReadReg(OHCI_CONTROL) & OHCI_IR); i++)
			snooze(1000);
		if (ReadReg(OHCI_CONTROL) & OHCI_IR)
			TRACE(("usb_ohci: SMM doesn't respond... continueing anyway...\n"));
	} else if (!(ReadReg(OHCI_CONTROL) & OHCI_HCFS_RESET)) {
		TRACE(("usb_ohci: BIOS is in control of the host controller\n"));
		if (!(ReadReg(OHCI_CONTROL) & OHCI_HCFS_OPERATIONAL)) {
			WriteReg(OHCI_CONTROL, OHCI_HCFS_RESUME);
			snooze(USB_DELAY_BUS_RESET);
		}
	} else if (ReadReg(OHCI_CONTROL) & OHCI_HCFS_RESET) //Only if no BIOS/SMM control
		snooze(USB_DELAY_BUS_RESET);

	//	
	// 5.1.1.4 Set Up Host controller
	//
	// Dummy endpoints
	fDummyControl = AllocateEndpoint();
	if (!fDummyControl)
		return;
	fDummyBulk = AllocateEndpoint();
	if (!fDummyBulk)
		return;
	fDummyIsochronous = AllocateEndpoint();
	if (!fDummyIsochronous)
		return;
	//Create the interrupt tree
	//Algorhythm kindly borrowed from NetBSD code
	for(i = 0; i < OHCI_NO_EDS; i++) {
		fInterruptEndpoints[i] = AllocateEndpoint();
		if (!fInterruptEndpoints[i])
			return;
		if (i != 0)
			fInterruptEndpoints[i]->SetNext(fInterruptEndpoints[(i-1) / 2]);
		else
			fInterruptEndpoints[i]->SetNext(fDummyIsochronous);
	}
	for (i = 0; i < OHCI_NUMBER_OF_INTERRUPTS; i++)
		fHcca->hcca_interrupt_table[revbits[i]] =
			fInterruptEndpoints[OHCI_NO_EDS-OHCI_NUMBER_OF_INTERRUPTS+i]->physicaladdress;
	
	//Go to the hardware part of the initialisation
	uint32 frameinterval = ReadReg(OHCI_FM_INTERVAL);
	
	WriteReg(OHCI_COMMAND_STATUS, OHCI_HCR);
	for (i = 0; i < 10; i++){
		snooze(10);				//Should be okay in one run: 10 microseconds for reset
		if (!(ReadReg(OHCI_COMMAND_STATUS) & OHCI_HCR))
			break;
	}
	if (ReadReg(OHCI_COMMAND_STATUS) & OHCI_HCR) {
		TRACE(("usb_ohci: Error resetting the host controller\n"));
		return;
	}
	
	WriteReg(OHCI_FM_INTERVAL, frameinterval);
	//We now have 2 ms to finish the following sequence. 
	//TODO: maybe add spinlock protection???
	
	WriteReg(OHCI_CONTROL_HEAD_ED, (uint32)fDummyControl->physicaladdress);
	WriteReg(OHCI_BULK_HEAD_ED, (uint32)fDummyBulk->physicaladdress);
	WriteReg(OHCI_HCCA, (uint32)hcca_phy);
	
	WriteReg(OHCI_INTERRUPT_DISABLE, OHCI_ALL_INTRS);
	WriteReg(OHCI_INTERRUPT_ENABLE, OHCI_NORMAL_INTRS);
	
	//
	// 5.1.1.5 Begin Sending SOFs
	//
	uint32 control = ReadReg(OHCI_CONTROL);
	control &= ~(OHCI_CBSR_MASK | OHCI_LES | OHCI_HCFS_MASK | OHCI_IR);
	control |= OHCI_PLE | OHCI_IE | OHCI_CLE | OHCI_BLE |
		OHCI_RATIO_1_4 | OHCI_HCFS_OPERATIONAL;
	WriteReg(OHCI_CONTROL, control);
	
	//The controller is now operational, end of 2ms block.
	uint32 interval = OHCI_GET_IVAL(frameinterval);
	WriteReg(OHCI_PERIODIC_START, OHCI_PERIODIC(interval));
	
	//Work on some Roothub settings
	uint32 desca = ReadReg(OHCI_RH_DESCRIPTOR_A);
	WriteReg(OHCI_RH_DESCRIPTOR_A, desca | OHCI_NOCP); //FreeBSD source does this to avoid a chip bug
	//Enable power
	WriteReg(OHCI_RH_STATUS, OHCI_LPSC);
	snooze(5000); //Wait for power to stabilize (5ms)
	WriteReg(OHCI_RH_DESCRIPTOR_A, desca);
	snooze(5000); //Delay required by the AMD756 because else the # of ports might be misread
	
	fInitOK = true;
}


OHCI::~OHCI()
{
	if (fHccaArea > 0)
		delete_area(fHccaArea);
	if (fRegisterArea > 0)
		delete_area(fRegisterArea);
	if (fDummyControl)
		FreeEndpoint(fDummyControl);
	if (fDummyBulk)
		FreeEndpoint(fDummyBulk);
	if (fDummyIsochronous)
		FreeEndpoint(fDummyIsochronous);
	if (fRootHub)
		delete fRootHub;
	for (int i = 0; i < OHCI_NO_EDS; i++)
		if (fInterruptEndpoints[i])
			FreeEndpoint(fInterruptEndpoints[i]);
}


status_t
OHCI::Start()
{
	TRACE(("OHCI::%s()\n", __FUNCTION__));
	if (InitCheck())
		return B_ERROR;
	
	if (!(ReadReg(OHCI_CONTROL) & OHCI_HCFS_OPERATIONAL)) {
		TRACE(("usb_ohci::Start(): Controller not started. TODO: find out what happens.\n"));
		return B_ERROR;
	}
	
	fRootHubAddress = AllocateAddress();
	fNumPorts = OHCI_GET_PORT_COUNT(ReadReg(OHCI_RH_DESCRIPTOR_A));
	
	fRootHub = new(std::nothrow) OHCIRootHub(this, fRootHubAddress);
	if (!fRootHub) {
		TRACE_ERROR(("usb_ohci::Start(): no memory to allocate root hub\n"));
		return B_NO_MEMORY;
	}

	if (fRootHub->InitCheck() < B_OK) {
		TRACE_ERROR(("usb_ohci::Start(): root hub failed init check\n"));
		return B_ERROR;
	}

	SetRootHub(fRootHub);
	TRACE(("usb_ohci::Start(): Succesful start\n"));
	return B_OK;
}


status_t
OHCI::SubmitTransfer(Transfer *t)
{
	TRACE(("usb OHCI::SubmitTransfer: called for device %d\n", t->TransferPipe()->DeviceAddress()));

	if (t->TransferPipe()->DeviceAddress() == fRootHubAddress)
		return fRootHub->ProcessTransfer(t, this);

	return B_ERROR;
}


status_t
OHCI::NotifyPipeChange(Pipe *pipe, usb_change change)
{
	TRACE(("OHCI::%s(%p, %d)\n", __FUNCTION__, pipe, (int)change));
	if (InitCheck())
		return B_ERROR;

	switch (change) {
	case USB_CHANGE_CREATED:
		return InsertEndpointForPipe(pipe);
	case USB_CHANGE_DESTROYED:
		// Do something
		return B_ERROR;
	case USB_CHANGE_PIPE_POLICY_CHANGED:
	default:
		break;
	}
	return B_ERROR; //We should never go here
}


status_t
OHCI::AddTo(Stack *stack)
{
#ifdef TRACE_USB
	set_dprintf_enabled(true); 
	load_driver_symbols("ohci");
#endif
	
	if (!sPCIModule) {
		status_t status = get_module(B_PCI_MODULE_NAME, (module_info **)&sPCIModule);
		if (status < B_OK) {
			TRACE_ERROR(("usb_ohci: AddTo(): getting pci module failed! 0x%08lx\n",
				status));
			return status;
		}
	}

	TRACE(("usb_ohci: AddTo(): setting up hardware\n"));

	bool found = false;
	pci_info *item = new(std::nothrow) pci_info;
	if (!item) {
		sPCIModule = NULL;
		put_module(B_PCI_MODULE_NAME);
		return B_NO_MEMORY;
	}

	for (uint32 i = 0 ; sPCIModule->get_nth_pci_info(i, item) >= B_OK; i++) {

		if (item->class_base == PCI_serial_bus && item->class_sub == PCI_usb
			&& item->class_api == PCI_usb_ohci) {
			if (item->u.h0.interrupt_line == 0
				|| item->u.h0.interrupt_line == 0xFF) {
				TRACE_ERROR(("usb_ohci: AddTo(): found with invalid IRQ -"
					" check IRQ assignement\n"));
				continue;
			}

			TRACE(("usb_ohci: AddTo(): found at IRQ %u\n",
				item->u.h0.interrupt_line));
			OHCI *bus = new(std::nothrow) OHCI(item, stack);
			if (!bus) {
				delete item;
				sPCIModule = NULL;
				put_module(B_PCI_MODULE_NAME);
				return B_NO_MEMORY;
			}

			if (bus->InitCheck() < B_OK) {
				TRACE_ERROR(("usb_ohci: AddTo(): InitCheck() failed 0x%08lx\n",
					bus->InitCheck()));
				delete bus;
				continue;
			}

			// the bus took it away
			item = new(std::nothrow) pci_info;

			bus->Start();
			stack->AddBusManager(bus);
			found = true;
		}
	}

	if (!found) {
		TRACE_ERROR(("usb_ohci: no devices found\n"));
		delete item;
		put_module(B_PCI_MODULE_NAME);
		return ENODEV;
	}

	delete item;
	return B_OK;
}


status_t
OHCI::GetPortStatus(uint8 index, usb_port_status *status)
{
	TRACE(("OHCI::%s(%ud, )\n", __FUNCTION__, index));
	if (index >= fNumPorts)
		return B_BAD_INDEX;
	
	status->status = status->change = 0;
	uint32 portStatus = ReadReg(OHCI_RH_PORT_STATUS(index));
	
	TRACE(("OHCIRootHub::GetPortStatus: Port %i Value 0x%lx\n", OHCI_RH_PORT_STATUS(index), portStatus));
	
	// status
	if (portStatus & OHCI_PORTSTATUS_CCS)
		status->status |= PORT_STATUS_CONNECTION;
	if (portStatus & OHCI_PORTSTATUS_PES)
		status->status |= PORT_STATUS_ENABLE;
	if (portStatus & OHCI_PORTSTATUS_PRS)
		status->status |= PORT_STATUS_RESET;
	if (portStatus & OHCI_PORTSTATUS_LSDA)
		status->status |= PORT_STATUS_LOW_SPEED;
	if (portStatus & OHCI_PORTSTATUS_PSS)
		status->status |= PORT_STATUS_SUSPEND;
	if (portStatus & OHCI_PORTSTATUS_POCI)
		status->status |= PORT_STATUS_OVER_CURRENT;
	if (portStatus & OHCI_PORTSTATUS_PPS)
		status->status |= PORT_STATUS_POWER;
	
	
	// change
	if (portStatus & OHCI_PORTSTATUS_CSC)
		status->change |= PORT_STATUS_CONNECTION;
	if (portStatus & OHCI_PORTSTATUS_PESC)
		status->change |= PORT_STATUS_ENABLE;
	if (portStatus & OHCI_PORTSTATUS_PSSC)
		status->change |= PORT_STATUS_SUSPEND;
	if (portStatus & OHCI_PORTSTATUS_OCIC)
		status->change |= PORT_STATUS_OVER_CURRENT;
	if (portStatus & OHCI_PORTSTATUS_PRSC)
		status->change |= PORT_STATUS_RESET;
	
	return B_OK;
}


status_t
OHCI::SetPortFeature(uint8 index, uint16 feature)
{
	TRACE(("OHCI::%s(%ud, %ud)\n", __FUNCTION__, index, feature));
	if (index > fNumPorts)
		return B_BAD_INDEX;
	
	switch (feature) {
		case PORT_RESET:
			WriteReg(OHCI_RH_PORT_STATUS(index), OHCI_PORTSTATUS_PRS);
			return B_OK;
		
		case PORT_POWER:
			WriteReg(OHCI_RH_PORT_STATUS(index), OHCI_PORTSTATUS_PPS);
			return B_OK;
	}
	
	return B_BAD_VALUE;
}


status_t
OHCI::ClearPortFeature(uint8 index, uint16 feature)
{
	TRACE(("OHCI::%s(%ud, %ud)\n", __FUNCTION__, index, feature));
	if (index > fNumPorts)
		return B_BAD_INDEX;
	
	switch (feature) {
		case C_PORT_RESET:
			WriteReg(OHCI_RH_PORT_STATUS(index), OHCI_PORTSTATUS_CSC);
			return B_OK;
		
		case C_PORT_CONNECTION:
			WriteReg(OHCI_RH_PORT_STATUS(index), OHCI_PORTSTATUS_CSC);
			return B_OK;
	}
	
	return B_BAD_VALUE;
}


Endpoint *
OHCI::AllocateEndpoint()
{
	TRACE(("OHCI::%s()\n", __FUNCTION__));
	//Allocate memory chunk
	Endpoint *endpoint = new(std::nothrow) Endpoint;
	void *phy;
	if (fStack->AllocateChunk((void **)&endpoint->ed, &phy, sizeof(ohci_endpoint_descriptor)) != B_OK) {
		TRACE(("OHCI::AllocateEndpoint(): Error Allocating Endpoint\n"));
		return 0;
	}
	endpoint->physicaladdress = (addr_t)phy;
	
	//Initialize the physical part
	memset((void *)endpoint->ed, 0, sizeof(ohci_endpoint_descriptor));
	endpoint->ed->flags = OHCI_ENDPOINT_SKIP;
	
	//Add a NULL list by creating one TransferDescriptor
	TransferDescriptor *trans = new(std::nothrow) TransferDescriptor;
	endpoint->head = endpoint->tail = trans;
	endpoint->ed->head_pointer = endpoint->ed->tail_pointer = trans->physicaladdress; 
	
	return endpoint;
}


void
OHCI::FreeEndpoint(Endpoint *end)
{
	TRACE(("OHCI::%s(%p)\n", __FUNCTION__, end));
	fStack->FreeChunk((void *)end->ed, (void *) end->physicaladdress, sizeof(ohci_endpoint_descriptor));
	delete end;
}


TransferDescriptor *
OHCI::AllocateTransfer()
{
	TRACE(("OHCI::%s()\n", __FUNCTION__));
	TransferDescriptor *transfer = new TransferDescriptor;
	void *phy;
	if (fStack->AllocateChunk((void **)&transfer->td, &phy, sizeof(ohci_general_transfer_descriptor)) != B_OK) {
		TRACE(("OHCI::AllocateTransfer(): Error Allocating Transfer\n"));
		return 0;
	}
	transfer->physicaladdress = (addr_t)phy;
	memset((void *)transfer->td, 0, sizeof(ohci_general_transfer_descriptor));
	return transfer;
}	


void
OHCI::FreeTransfer(TransferDescriptor *trans)
{
	TRACE(("OHCI::%s(%p)\n", __FUNCTION__, trans));
	fStack->FreeChunk((void *)trans->td, (void *) trans->physicaladdress, sizeof(ohci_general_transfer_descriptor));
	delete trans;
}


status_t
OHCI::InsertEndpointForPipe(Pipe *p)
{
	TRACE(("OHCI: Inserting Endpoint for device %u function %u\n", p->DeviceAddress(), p->EndpointAddress()));

	if (InitCheck())
		return B_ERROR;
	
	Endpoint *endpoint = AllocateEndpoint();
	if (!endpoint)
		return B_NO_MEMORY;
	
	//Set up properties of the endpoint
	//TODO: does this need its own utility function?
	{
		uint32 properties = 0;
		
		//Set the device address
		properties |= OHCI_ENDPOINT_SET_DEVICE_ADDRESS(p->DeviceAddress());
		
		//Set the endpoint number
		properties |= OHCI_ENDPOINT_SET_ENDPOINT_NUMBER(p->EndpointAddress());
		
		//Set the direction
		switch (p->Direction()) {
		case Pipe::In:
			properties |= OHCI_ENDPOINT_DIRECTION_IN;
			break;
		case Pipe::Out:
			properties |= OHCI_ENDPOINT_DIRECTION_OUT;
			break;
		case Pipe::Default:
			properties |= OHCI_ENDPOINT_DIRECTION_DESCRIPTOR;
			break;
		default:
			//TODO: error
			break;
		}
		
		//Set the speed
		switch (p->Speed()) {
		case USB_SPEED_LOWSPEED:
			//the bit is 0
			break;
		case USB_SPEED_FULLSPEED:
			properties |= OHCI_ENDPOINT_SPEED;
			break;
		case USB_SPEED_HIGHSPEED:
		default:
			//TODO: error
			break;
		}
		
		//Assign the format. Isochronous endpoints require this switch
		if (p->Type() & USB_OBJECT_ISO_PIPE)
			properties |= OHCI_ENDPOINT_ISOCHRONOUS_FORMAT;
		
		//Set the maximum packet size
		properties |= OHCI_ENDPOINT_SET_MAX_PACKET_SIZE(p->MaxPacketSize());
		
		endpoint->ed->flags = properties;
	}
	
	//Check which list we need to add the endpoint in
	Endpoint *listhead;
	if (p->Type() & USB_OBJECT_CONTROL_PIPE)
		listhead = fDummyControl;
	else if (p->Type() & USB_OBJECT_BULK_PIPE)
		listhead = fDummyBulk;
	else if (p->Type() & USB_OBJECT_ISO_PIPE)
		listhead = fDummyIsochronous;
	else {
		FreeEndpoint(endpoint);
		return B_ERROR;
	}

	//Add the endpoint to the queues
	if ((p->Type() & USB_OBJECT_ISO_PIPE) == 0) {
		//Link the endpoint into the head of the list
		endpoint->SetNext(listhead->next);
		listhead->SetNext(endpoint);
	} else { 
		//Link the endpoint into the tail of the list
		Endpoint *tail = listhead;
		while (tail->next != 0)
			tail = tail->next;
		tail->SetNext(endpoint);
	}

	return B_OK;
}


void
OHCI::WriteReg(uint32 reg, uint32 value)
{
	TRACE(("OHCI::%s(%lu, %lu)\n", __FUNCTION__, reg, value));
	*(volatile uint32 *)(fRegisterBase + reg) = value;
}


uint32
OHCI::ReadReg(uint32 reg)
{
	TRACE(("OHCI::%s(%lu)\n", __FUNCTION__, reg));
	return *(volatile uint32 *)(fRegisterBase + reg);
}


status_t
OHCI::CancelQueuedTransfers(Pipe *pipe)
{
	return B_ERROR;
}

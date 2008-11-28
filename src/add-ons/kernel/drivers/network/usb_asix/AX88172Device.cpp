/*
 *	ASIX AX88172/AX88772/AX88178 USB 2.0 Ethernet Driver.
 *	Copyright (c) 2008 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *	
 *	Heavily based on code of the 
 *	Driver for USB Ethernet Control Model devices
 *	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
 *	Distributed under the terms of the MIT license.
 *
 */

#include "Settings.h"
#include "AX88172Device.h"


enum AX88172_Requests {
	READ_RXTX_SRAM		= 0x02, // C0 02 XX YY 0M 00 0200 Read Rx/Tx SRAM
							    //     	                 M = 0 : Rx,   M=1 : Tx
	WRITE_RX_SRAM		= 0x03, // 40 03 XX YY PP QQ 0000 Write Rx SRAM
	WRITE_TX_SRAM		= 0x04, // 40 04 XX YY PP QQ 0000 Write Tx SRAM
	SW_MII_OP			= 0x06, // 40 06 00 00 00 00 0000 Software MII Operation
	READ_MII			= 0x07, // C0 07 PI 00 RG 00 0200 Read MII Register
	WRITE_MII			= 0x08, // 40 08 PI 00 RG 00 0200 Write MII Register
	READ_MII_OP_MODE	= 0x09, // C0 09 00 00 00 00 0100 Read MII Operation Mode
	HW_MII_OP			= 0x0A, // 40 0A 00 00 00 00 0000 Hardware MII Operation
	READ_SROM			= 0x0B, // C0 0B DR 00 00 00 0200 Read SROM
	WRITE_SROM			= 0x0C, // 40 0C DR 00 MM SS 0000 Write SROM
	WRITE_SROM_ENABLE	= 0x0D, // 40 0D 00 00 00 00 0000 Write SROM Enable
	WRITE_SROM_DISABLE	= 0x0E, // 40 0E 00 00 00 00 0000 Write SROM Disable
	READ_RX_CONTROL		= 0x0F, // C0 0F 00 00 00 00 0200 Read Rx Control Register
	WRITE_RX_CONTROL	= 0x10, // 40 10 RR 00 00 00 0000 Write Rx Control Register
	READ_IPGS			= 0x11, // C0 11 00 00 00 00 0300 Read IPG/IPG1/IPG2 Register
	WRITE_IPG0			= 0x12, // 40 12 II 00 00 00 0000 Write IPG Register
	WRITE_IPG1			= 0x13, // 40 13 II 00 00 00 0000 Write IPG1 Register
	WRITE_IPG2			= 0x14, // 40 14 II 00 00 00 0000 Write IPG2 Register
	READ_MF_ARRAY		= 0x15, // C0 15 00 00 00 00 0800 Read Multi-Filter Array
	WRITE_MF_ARRAY		= 0x16, // 40 16 00 00 00 00 0800 Write Multi-Filter Array
	READ_NODEID			= 0x17, // C0 17 00 00 00 00 0600 Read Node ID
	WRITE_NODEID		= 0x18, // 
	READ_PHYID			= 0x19, // C0 19 00 00 00 00 0200 Read Ethernet/HomePNA PhyID
	READ_MEDIUM_STATUS	= 0x1A, // C0 1A 00 00 00 00 0100 Read Medium Status
	WRITE_MEDIUM_MODE	= 0x1B, // 40 1B MM 00 00 00 0000 Write Medium Mode
	GET_MONITOR_MODE	= 0x1C, // C0 1C 00 00 00 00 0100 Get Monitor Mode Status
	SET_MONITOR_MODE	= 0x1D, // 40 1D MM 00 00 00 0000 Set Monitor Mode On/Off
	READ_GPIOS			= 0x1E, // C0 1E 00 00 00 00 0100 Read GPIOs
	WRITE_GPIOS			= 0x1F  // 40 1F MM 00 00 00 0000 Write GPIOs
};

// RX Control Register bits
enum AX88172_RXControl {
	RXCTL_PROMISCUOUS	= 0x0001, // 
	RXCTL_ALL_MULTICAT	= 0x0002, // 
	RXCTL_UNICAST		= 0x0004, //  ???
	RXCTL_BROADCAST		= 0x0008, //
	RXCTL_MULTICAST		= 0x0010, // 
	RXCTL_START			= 0x0080  //
};

// PHY IDs request answer data layout
struct PhyIDs {
	uint8 PhyID1; 
	uint8 PhyID2;
} _PACKED;

// Medium state bits 
enum AX88172_MediumState {
	MEDIUM_STATE_FULL_DUPLEX	= 0x02,
	MEDIUM_STATE_TX_ABORT_ALLOW	= 0x04,
	MEDIUM_STATE_FLOW_CONTOL_EN	= 0x10
};

// Monitor Mode bits
enum AX88172_MonitorMode {
	MONITOR_MODE					= 0x01,
	MONITOR_MODE_LINK_UP_WAKE		= 0x02,
	MONITOR_MODE_MAGIC_PACKET_EN	= 0x04,
	MONITOR_MODE_HS_FS 				= 0x10
};

// General Purpose I/O Register
enum AX88172_GPIO {
	GPIO_OO_0EN	= 0x01,
	GPIO_IO_0	= 0x02,
	GPIO_OO_1EN	= 0x04,
	GPIO_IO_1	= 0x08,
	GPIO_OO_2EN	= 0x10,
	GPIO_IO_2	= 0x20
};

// Notification data layout 
struct AX88172Notify {
	uint8 btA1;	
	uint8 bt01;	
	uint8 btNN; // AX88172_LinkState below 
	uint8 bt03;	
	uint8 bt04;	
	uint8 bt80;	// 90h 
	uint8 bt06;	
	uint8 bt07;	
} _PACKED;

// Link-State bits 
enum AX88172_LinkState {
	LINK_STATE_PHY1	= 0x01,
	LINK_STATE_PHY2	= 0x02
};

const uint16 maxFrameSize = 1518;

AX88172Device::AX88172Device(usb_device device, const char *description)
	:	ASIXDevice(device, description)
{
	fStatus = InitDevice();
}


status_t
AX88172Device::InitDevice()
{
	fFrameSize = maxFrameSize;
	
	fReadNodeIDRequest = READ_NODEID;
	fReadRXControlRequest = READ_RX_CONTROL;
	fWriteRXControlRequest = WRITE_RX_CONTROL;

	fPromiscuousBits = RXCTL_PROMISCUOUS;

	fNotifyBufferLength = sizeof(AX88172Notify);
	fNotifyBuffer = (uint8 *)malloc(fNotifyBufferLength);
	if (fNotifyBuffer == NULL) {
		TRACE_ALWAYS("Error of allocating memory for notify buffer.\n");
		return B_NO_MEMORY;
	}
	
	TRACE_RET(B_OK);
	return B_OK;
}
status_t
AX88172Device::SetupDevice(bool deviceReplugged)
{
	status_t result = ASIXDevice::SetupDevice(deviceReplugged);
	if(result != B_OK) {
		return result;
	}

	result = fMII.Init(fDevice,
			SW_MII_OP, READ_MII, WRITE_MII, 
			READ_MII_OP_MODE, HW_MII_OP, READ_PHYID);

	if(result == B_OK) 
		return fMII.SetupPHY();
	
	TRACE_RET(result);
	return result;
}


status_t
AX88172Device::StartDevice()
{
	size_t actualLength = 0;

	for(size_t i = 0; i < sizeof(fIPG)/sizeof(fIPG[0]); i++) {
		status_t result = gUSBModule->send_request(fDevice, 
					USB_REQTYPE_VENDOR | USB_REQTYPE_DEVICE_OUT,
					WRITE_IPG0, 0, 0, sizeof(fIPG[i]), &fIPG[i], &actualLength);
	
		if(result != B_OK) {
			TRACE_ALWAYS("Error writing IPG%d: %#010x\n", i, result);
			return result;
		}

		if(actualLength != sizeof(fIPG[i])) {
			TRACE_ALWAYS("Mismatch of written IPG%d data. "
					"%d bytes of %d written.\n", i, actualLength, sizeof(fIPG[i]));
		}
	}

	uint16 rxcontrol = RXCTL_START | RXCTL_MULTICAST | RXCTL_UNICAST;
	status_t result = WriteRXControlRegister(rxcontrol);
	if(result != B_OK) {
		TRACE_ALWAYS("Error of writing %#04x RX Control:%#010x\n", rxcontrol, result);
	} 

	TRACE_RET(result);
	return result; 
}


status_t
AX88172Device::OnNotify(uint32 actualLength)
{
	if (actualLength < sizeof(AX88172Notify)) {
		TRACE_ALWAYS("Data underrun error. %d of %d bytes received\n",
										actualLength, sizeof(AX88172Notify));
		return B_BAD_DATA; 
	}
		
	AX88172Notify *notification	= (AX88172Notify *)fNotifyBuffer; 

	if(notification->btA1 != 0xa1) {
		TRACE_ALWAYS("Notify magic byte is invalid: %#02x\n", 
														notification->btA1);
	}
	
	uint phyIndex = 0;
	bool linkIsUp = fHasConnection;
	switch(fMII.ActivePHY()) {
		case PrimaryPHY:
			phyIndex = 1;
			linkIsUp = (notification->btNN & LINK_STATE_PHY1) == LINK_STATE_PHY1; 
			break;
		case SecondaryPHY:
			phyIndex = 2;
			linkIsUp = (notification->btNN & LINK_STATE_PHY2) == LINK_STATE_PHY2; 
			break;
		default:
		case CurrentPHY:
			TRACE_ALWAYS("Error: PHY is not initialized.\n");
			return B_NO_INIT;
	}

	bool linkStateChange = linkIsUp != fHasConnection;
	fHasConnection = linkIsUp;

	if(linkStateChange) {
		TRACE("Link state of PHY%d has been changed to '%s'\n", 
									phyIndex, fHasConnection ? "up" : "down");
	}

	if (linkStateChange && fLinkStateChangeSem >= B_OK)
		release_sem_etc(fLinkStateChangeSem, 1, B_DO_NOT_RESCHEDULE);

	return B_OK;
}

status_t
AX88172Device::GetLinkState(ether_link_state *linkState)
{
	uint16 miiANAR = 0;
	uint16 miiANLPAR = 0;

	status_t result = fMII.Read(MII_ANAR, &miiANAR);
	if(result != B_OK) {
		TRACE_ALWAYS("Error reading MII ANAR register:%#010x\n", result);
		return result;
	}

	result = fMII.Read(MII_ANLPAR, &miiANLPAR);
	if(result != B_OK) {
		TRACE_ALWAYS("Error reading MII ANLPAR register:%#010x\n", result);
		return result;
	}

	TRACE_FLOW("ANAR:%04x ANLPAR:%04x\n", miiANAR, miiANLPAR);
	
	uint16 mediumStatus = miiANAR & miiANLPAR;
		
	linkState->quality = 1000;
	
	linkState->media   = IFM_ETHER | (fHasConnection ? IFM_ACTIVE : 0);
    linkState->media  |= mediumStatus & (ANLPAR_TX_FD | ANLPAR_10_FD) ? 
											IFM_FULL_DUPLEX : IFM_HALF_DUPLEX;
	
	linkState->speed   = mediumStatus & (ANLPAR_TX_FD | ANLPAR_TX_HD) ? 100000 : 10000;
	
	TRACE_FLOW("Medium state: %s, %lld MBit/s, %s duplex.\n", 
						(linkState->media & IFM_ACTIVE) ? "active" : "inactive",
						linkState->speed / 1000,
						(linkState->media & IFM_FULL_DUPLEX) ? "full" : "half");
	return B_OK;
}


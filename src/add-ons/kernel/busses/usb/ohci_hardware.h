//------------------------------------------------------------------------------
//     Copyright (c) 2005, Jan-Rixt Van Hoye
//
//     Permission is hereby granted, free of charge, to any person obtaining a
//     copy of this software and associated documentation files (the "Software"),
//     to deal in the Software without restriction, including without limitation
//     the rights to use, copy, modify, merge, publish, distribute, sublicense,
//     and/or sell copies of the Software, and to permit persons to whom the
//     Software is furnished to do so, subject to the following conditions:
//
//     The above copyright notice and this permission notice shall be included in
//     all copies or substantial portions of the Software.
//
//     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//     IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//     FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//     AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//     LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//     FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//     DEALINGS IN THE SOFTWARE.
//
//     ----------------------------------------------------------------------------
//     Authors:
//     		Salvatore Benedetto <salvatore.benedetto@gmail.com>


#ifndef OHCI_HARD_H
#define OHCI_HARD_H

// --------------------------------
//	The OHCI registers 
// --------------------------------

// --------------------------------
//	Revision register (section 7.1.1)
// --------------------------------

#define	OHCI_REVISION				0x00
#define		OHCI_REV_LO(rev)		((rev) & 0x0f)
#define		OHCI_REV_HI(rev)		(((rev) >> 4) & 0x03)
#define		OHCI_REV_LEGACY(rev)	((rev) & 0x10)

// --------------------------------
//	Control register (section 7.1.2)
// --------------------------------

#define	OHCI_CONTROL				0x04
#define		OHCI_CBSR_MASK			0x00000003 // Control-Bulk Service Ratio
#define			OHCI_RATIO_1_1		0x00000000
#define			OHCI_RATIO_1_2		0x00000001
#define			OHCI_RATIO_1_3		0x00000002
#define			OHCI_RATIO_1_4		0x00000003
#define		OHCI_PLE				0x00000004 // Periodic List Enable 
#define		OHCI_IE					0x00000008 // Isochronous Enable 
#define		OHCI_CLE				0x00000010 // Control List Enable 
#define		OHCI_BLE				0x00000020 // Bulk List Enable 
#define		OHCI_HCFS_MASK			0x000000c0 // HostControllerFunctionalState 
#define		OHCI_HCFS_RESET			0x00000000
#define		OHCI_HCFS_RESUME		0x00000040
#define		OHCI_HCFS_OPERATIONAL	0x00000080
#define		OHCI_HCFS_SUSPEND		0x000000c0
#define		OHCI_IR					0x00000100 // Interrupt Routing 
#define		OHCI_RWC				0x00000200 // Remote Wakeup Connected 
#define		OHCI_RWE				0x00000400 // Remote Wakeup Enabled 

// --------------------------------
//	Command status register (section 7.1.3)
// --------------------------------

#define OHCI_COMMAND_STATUS	0x08
#define		OHCI_HCR				0x00000001 // Host Controller Reset 
#define		OHCI_CLF				0x00000002 // Control List Filled 
#define		OHCI_BLF				0x00000004 // Bulk List Filled 
#define		OHCI_OCR				0x00000008 // Ownership Change Request 
#define		OHCI_SOC_MASK			0x00030000 // Scheduling Overrun Count 

// --------------------------------
//	Interrupt status register (section 7.1.4)
// --------------------------------

#define OHCI_INTERRUPT_STATUS	0x0c
#define		OHCI_SO					0x00000001 // Scheduling Overrun
#define		OHCI_WDH				0x00000002 // Writeback Done Head 
#define		OHCI_SF					0x00000004 // Start of Frame 
#define		OHCI_RD					0x00000008 // Resume Detected 
#define		OHCI_UE					0x00000010 // Unrecoverable Error 
#define		OHCI_FNO				0x00000020 // Frame Number Overflow 
#define		OHCI_RHSC				0x00000040 // Root Hub Status Change 
#define		OHCI_OC					0x40000000 // Ownership Change 
#define		OHCI_MIE				0x80000000 // Master Interrupt Enable 

// --------------------------------
//	Interupt enable register (section 7.1.5)
// --------------------------------

#define OHCI_INTERRUPT_ENABLE		0x10

// --------------------------------
//	Interupt disable register (section 7.1.6)
// --------------------------------

#define OHCI_INTERRUPT_DISABLE		0x14

// -------------------------------------
//	Memory Pointer Partition (section 7.2)
// -------------------------------------

// --------------------------------
//	HCCA register (section 7.2.1)
// --------------------------------

#define OHCI_HCCA					0x18

// --------------------------------
//	Period current ED  register (section 7.2.2)
// --------------------------------

#define OHCI_PERIOD_CURRENT_ED		0x1c

// --------------------------------
//	Control head ED register (section 7.2.3)
// --------------------------------

#define OHCI_CONTROL_HEAD_ED		0x20

// --------------------------------
//	Current control ED register (section 7.2.4)
// --------------------------------

#define OHCI_CONTROL_CURRENT_ED		0x24

// --------------------------------
//	Bulk head ED register (section 7.2.5)
// --------------------------------

#define OHCI_BULK_HEAD_ED			0x28

// --------------------------------
//	Current bulk ED register (section 7.2.6)
// --------------------------------

#define OHCI_BULK_CURRENT_ED		0x2c

// --------------------------------
//	Done head register (section 7.2.7)
// --------------------------------

#define OHCI_DONE_HEAD				0x30

// --------------------------------
//	Frame Counter partition (section 7.3)
// --------------------------------

// --------------------------------
//	Frame interval register (section 7.3.1)
// --------------------------------

#define OHCI_FM_INTERVAL			0x34
#define		OHCI_GET_IVAL(s)		((s) & 0x3fff)
#define		OHCI_GET_FSMPS(s)		(((s) >> 16) & 0x7fff)
#define		OHCI_FIT				0x80000000

// --------------------------------
//	Frame remaining register (section 7.3.2)
// --------------------------------

#define OHCI_FM_REMAINING			0x38

// --------------------------------
//	Frame number register	(section 7.3.3)
// --------------------------------

#define OHCI_FM_NUMBER				0x3c

// --------------------------------
//	Periodic start register (section 7.3.4)
// --------------------------------

#define OHCI_PERIODIC_START			0x40

// --------------------------------
//	LS treshold register (section 7.3.5)
// --------------------------------

#define OHCI_LS_THRESHOLD			0x44

// --------------------------------
//	Root Hub Partition (section 7.4)
// --------------------------------

// --------------------------------
//	Root Hub Descriptor A register (section 7.4.1)
// --------------------------------

#define OHCI_RH_DESCRIPTOR_A		0x48
#define		OHCI_GET_PORT_COUNT(s)	((s) & 0xff)
#define		OHCI_PSM				0x0100     // Power Switching Mode
#define		OHCI_NPS				0x0200	   // No Power Switching
#define		OHCI_DT					0x0400     // Device Type
#define		OHCI_OCPM				0x0800     // Overcurrent Protection Mode 
#define		OHCI_NOCP				0x1000     // No Overcurrent Protection 
#define		OHCI_GET_POTPGT(s)		((s) >> 24)

// --------------------------------
//	Root Hub Descriptor B register (section 7.4.2)
// --------------------------------

#define OHCI_RH_DESCRIPTOR_B		0x4c

// --------------------------------
//	Root Hub status register (section 7.4.3)
// --------------------------------

#define OHCI_RH_STATUS				0x50
#define		OHCI_LPS				0x00000001 // Local Power Status 
#define		OHCI_OCI				0x00000002 // OverCurrent Indicator 
#define		OHCI_DRWE				0x00008000 // Device Remote Wakeup Enable 
#define		OHCI_LPSC				0x00010000 // Local Power Status Change
#define		OHCI_CCIC				0x00020000 // OverCurrent Indicator Change
#define		OHCI_CRWE				0x80000000 // Clear Remote Wakeup Enable

// --------------------------------
//	Root Hub port status (n) register (section 7.4.4)
// --------------------------------

#define OHCI_RH_PORT_STATUS(n)		(0x50 + (n)*4) // 1 based indexing
#define     OHCI_PORTSTATUS_CCS     0x00000001 // Current Connection Status
#define     OHCI_PORTSTATUS_PES     0x00000002 // Port Enable Status
#define     OHCI_PORTSTATUS_PSS     0x00000004 // Port Suspend Status
#define     OHCI_PORTSTATUS_POCI    0x00000008 // Port Overcurrent Indicator
#define     OHCI_PORTSTATUS_PRS     0x00000010 // Port Reset Status
#define     OHCI_PORTSTATUS_PPS     0x00000100 // Port Power Status
#define     OHCI_PORTSTATUS_LSDA    0x00000200 // Low Speed Device Attached
#define     OHCI_PORTSTATUS_CSC     0x00010000 // Connection Status Change
#define     OHCI_PORTSTATUS_PESC    0x00020000 // Port Enable Status Change
#define     OHCI_PORTSTATUS_PSSC    0x00040000 // Port Suspend Status change
#define     OHCI_PORTSTATUS_OCIC    0x00080000 // Port Overcurrent Change
#define     OHCI_PORTSTATUS_PRSC    0x00100000 // Port Reset Status Change

// --------------------------------
//	????
// --------------------------------

#define OHCI_LES					(OHCI_PLE | OHCI_IE | OHCI_CLE | OHCI_BLE)

// --------------------------------
//	All interupts
// --------------------------------

#define OHCI_ALL_INTRS				(OHCI_SO | OHCI_WDH | OHCI_SF | OHCI_RD | OHCI_UE | OHCI_FNO | OHCI_RHSC | OHCI_OC)

// --------------------------------
//	All normal interupts
// --------------------------------	
					
#define OHCI_NORMAL_INTRS			(OHCI_SO | OHCI_WDH | OHCI_RD | OHCI_UE | OHCI_RHSC)

// --------------------------------
//	FSMPS
// --------------------------------

#define OHCI_FSMPS(i)				(((i-210)*6/7) << 16)

// --------------------------------
//	Periodic
// --------------------------------

#define OHCI_PERIODIC(i)			((i)*9/10)

// --------------------------------
//	OHCI physical address
// --------------------------------

typedef uint32 ohci_physaddr_t;

// --------------------------------
//	HCCA structure (section 4.4)
// --------------------------------

#define OHCI_NUMBER_OF_INTERRUPTS	32

typedef struct ohci_hcca 
{
	addr_t		hcca_interrupt_table[OHCI_NUMBER_OF_INTERRUPTS];
	uint32		hcca_frame_number;
	addr_t		hcca_done_head;
	uint8		hcca_reserved_for_hc[116];
};

#define OHCI_DONE_INTRS				1
#define OHCI_HCCA_SIZE				256
#define OHCI_HCCA_ALIGN				256
#define OHCI_PAGE_SIZE				0x1000
#define OHCI_PAGE(x)				((x) &~ 0xfff)
#define OHCI_PAGE_OFFSET(x)			((x) & 0xfff)

// --------------------------------
//	Endpoint descriptor structure (section 4.2)
// --------------------------------

typedef struct ohci_endpoint_descriptor
{
	uint32		flags;
	uint32		tail_pointer;		// Queue tail pointer
	uint32		head_pointer;		// Queue head pointer
	uint32		next_endpoint;		// Next endpoint in the list
};

#define OHCI_ENDPOINT_ADDRESS_MASK				0x0000007f
#define OHCI_ENDPOINT_GET_DEVICE_ADDRESS(s)		((s) & 0x7f)
#define OHCI_ENDPOINT_SET_DEVICE_ADDRESS(s)		(s)
#define OHCI_ENDPOINT_GET_ENDPOINT_NUMBER(s)	(((s) >> 7) & 0xf)
#define OHCI_ENDPOINT_SET_ENDPOINT_NUMBER(s)	((s) << 7)
#define OHCI_ENDPOINT_DIRECTION_MASK			0x00001800
#define	OHCI_ENDPOINT_DIRECTION_DESCRIPTOR		0x00000000
#define	OHCI_ENDPOINT_DIRECTION_OUT				0x00000800
#define	OHCI_ENDPOINT_DIRECTION_IN				0x00001000
#define OHCI_ENDPOINT_SPEED						0x00002000
#define OHCI_ENDPOINT_SKIP						0x00004000
#define OHCI_ENDPOINT_GENERAL_FORMAT			0x00000000
#define OHCI_ENDPOINT_ISOCHRONOUS_FORMAT		0x00008000
#define OHCI_ENDPOINT_MAX_PACKET_SIZE_MASK		(0x7ff << 16)
#define OHCI_ENDPOINT_GET_MAX_PACKET_SIZE(s)	(((s) >> 16) & 0x07ff)
#define OHCI_ENDPOINT_SET_MAX_PACKET_SIZE(s)	((s) << 16)
#define OHCI_ENDPOINT_HALTED					0x00000001
#define OHCI_ENDPOINT_TOGGLE_CARRY				0x00000002
#define OHCI_ENDPOINT_HEAD_MASK					0xfffffffc


// --------------------------------
//	General transfer descriptor structure (section 4.3.1)
// --------------------------------

typedef struct ohci_general_transfer_descriptor
{
	uint32		flags;
	uint32		buffer_phy;			// Physical buffer pointer 
	uint32 		next_descriptor;	// Next transfer descriptor 
	uint32 		last_byte_address;	// Physical buffer end 
};

#define OHCI_BUFFER_ROUNDING			0x00040000		// Buffer Rounding 
#define OHCI_TD_DIRECTION_PID_MASK		0x00180000		// Direction / PID 
#define OHCI_TD_DIRECTION_PID_SETUP		0x00000000
#define OHCI_TD_DIRECTION_PID_OUT		0x00080000
#define OHCI_TD_DIRECTION_PID_IN		0x00100000
#define OHCI_TD_GET_DELAY_INTERRUPT(x)	(((x) >> 21) & 7)	// Delay Interrupt 
#define OHCI_TD_SET_DELAY_INTERRUPT(x)	((x) << 21)
#define OHCI_TD_NO_INTERRUPT			0x00e00000
#define OHCI_TD_INTERRUPT_MASK			0x00e00000
#define OHCI_TD_TOGGLE_CARRY			0x00000000
#define OHCI_TD_TOGGLE_0				0x02000000
#define OHCI_TD_TOGGLE_1				0x03000000
#define OHCI_TD_TOGGLE_MASK				0x03000000
#define OHCI_TD_GET_ERROR_COUNT(x)		(((x) >> 26) & 3)	// Error Count 
#define OHCI_TD_GET_CONDITION_CODE(x)	((x) >> 28)			// Condition Code 
#define OHCI_TD_NO_CONDITION_CODE		0xf0000000

#define OHCI_GENERAL_TD_ALIGN 16

// --------------------------------
//	Isonchronous transfer descriptor structure (section 4.3.2)
// --------------------------------

#define OHCI_ITD_NOFFSET 8
typedef struct ohci_isochronous_transfer_descriptor
{
	uint32		flags;
	uint32		buffer_page_byte_0;			// Physical page number of byte 0
	uint32		next_descriptor;			// Next isochronous transfer descriptor
	uint32		last_byte_address;			// Physical buffer end
	uint16		offset[OHCI_ITD_NOFFSET];	// Buffer offsets
};

#define OHCI_ITD_GET_STARTING_FRAME(x)			((x) & 0x0000ffff)
#define OHCI_ITD_SET_STARTING_FRAME(x)			((x) & 0xffff)
#define OHCI_ITD_GET_DELAY_INTERRUPT(x)			(((x) >> 21) & 7)
#define OHCI_ITD_SET_DELAY_INTERRUPT(x)			((x) << 21)
#define OHCI_ITD_NO_INTERRUPT					0x00e00000
#define OHCI_ITD_GET_FRAME_COUNT(x)				((((x) >> 24) & 7) + 1)
#define OHCI_ITD_SET_FRAME_COUNT(x)				(((x) - 1) << 24)
#define OHCI_ITD_GET_CONDITION_CODE(x)			((x) >> 28)
#define OHCI_ITD_NO_CONDITION_CODE				0xf0000000

// TO FIX
#define itd_pswn itd_offset						// Packet Status Word
#define OHCI_ITD_PAGE_SELECT					0x00001000
#define OHCI_ITD_MK_OFFS(len)					(0xe000 | ((len) & 0x1fff))
#define OHCI_ITD_GET_BUFFER_LENGTH(x)			((x) & 0xfff)		// Transfer length
#define OHCI_ITD_GET_BUFFER_CONDITION_CODE(x)	((x) >> 12)			// Condition Code

#define OHCI_ISOCHRONOUS_TD_ALIGN 32

// --------------------------------
//	Completion Codes (section 4.3.3)
// --------------------------------

#define OHCI_NO_ERROR				0
#define OHCI_CRC					1
#define OHCI_BIT_STUFFING			2
#define OHCI_DATA_TOGGLE_MISMATCH	3
#define OHCI_STALL					4
#define OHCI_DEVICE_NOT_RESPONDING	5
#define OHCI_PID_CHECK_FAILURE		6
#define OHCI_UNEXPECTED_PID			7
#define OHCI_DATA_OVERRUN			8
#define OHCI_DATA_UNDERRUN			9
#define OHCI_BUFFER_OVERRUN			12
#define OHCI_BUFFER_UNDERRUN		13
#define OHCI_NOT_ACCESSED			15

// --------------------------------
// 	Some delay needed when changing 
//	certain registers.
// --------------------------------

#define OHCI_ENABLE_POWER_DELAY			5
#define OHCI_READ_DESC_DELAY			5

#endif // OHCI_HARD_H 

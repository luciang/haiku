//------------------------------------------------------------------------------
//	Copyright (c) 2005, Jan-Rixt Van Hoye
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//-------------------------------------------------------------------------------


#ifndef OHCI_SOFT_H
#define OHCI_SOFT_H

//	---------------------------
//	OHCI::	Includes
//	---------------------------

#include <usb_p.h>
#include "ohci_hardware.h"

// ------------------------------------
//	OHCI:: Software endpoint descriptor
// ------------------------------------

typedef struct hcd_soft_endpoint
{	
	addr_t 						physaddr;	// physical address of the host controller endpoint
	hc_endpoint_descriptor 		ed; 		// host controller endpoint descriptor
	struct hcd_soft_endpoint 	*next; 		// next software endpoint descriptor
}hcd_soft_endpoint;

#define OHCI_SED_SIZE ((sizeof (struct hcd_soft_endpoint) + OHCI_ED_ALIGN - 1) / OHCI_ED_ALIGN * OHCI_ED_ALIGN)
#define OHCI_SED_CHUNK 128

// ------------------------------------
//	OHCI:: 	Software general 
//			transfer descriptor
// ------------------------------------

typedef struct hcd_soft_transfer 
{
	hc_transfer_descriptor			td;
	struct hcd_soft_transfer		*nexttd; 	// mirrors nexttd in TD
	struct hcd_soft_trasfer			*dnext; 	// next in done list
	addr_t 							physaddr;	// physical address to the host controller transfer
	//LIST_ENTRY(hcd_soft_transfer) 	hnext;
	uint16 							len;		// the lenght
	uint16 							flags;		// flags
}hcd_soft_transfer;

#define OHCI_CALL_DONE	0x0001
#define OHCI_ADD_LEN	0x0002

#define OHCI_STD_SIZE ((sizeof (struct hcd_soft_transfer) + OHCI_TD_ALIGN - 1) / OHCI_TD_ALIGN * OHCI_TD_ALIGN)
#define OHCI_STD_CHUNK 128

// --------------------------------------
//	OHCI:: 	Software isonchronous 
//			transfer descriptor
// --------------------------------------
typedef struct  hcd_soft_itransfer
{
	hc_itransfer_descriptor			itd;
	struct hcd_soft_itransfer			*nextitd; 	// mirrors nexttd in ITD
	struct hcd_soft_itransfer			*dnext; 	// next in done list
	addr_t 								physaddr;	// physical address to the host controller isonchronous transfer
	//LIST_ENTRY(hcd_soft_itransfer) 		hnext;
	uint16								flags;		// flags
#ifdef DIAGNOSTIC
	char 								isdone;		// is the transfer done?
#endif
}hcd_soft_itransfer;

#define OHCI_SITD_SIZE ((sizeof (struct hcd_soft_itransfer) + OHCI_ITD_ALIGN - 1) / OHCI_ITD_ALIGN * OHCI_ITD_ALIGN)
#define OHCI_SITD_CHUNK 64

// ------------------------------------------
//	OHCI:: Number of enpoint descriptors (63)
// ------------------------------------------

#define OHCI_NO_EDS (2*OHCI_NO_INTRS-1)

// --------------------------
//	OHCI:: Hash size for the 
// --------------------------

#define OHCI_HASH_SIZE 128

// ------------------------------
//	OHCI:: OHCI device structure
// ------------------------------
/*
typedef struct 		 
{
	usb_dma_t 							sc_hccadma;
	struct ohci_hcca 					*sc_hcca;
	ohci_soft_endpoint					*sc_eds[OHCI_NO_EDS];
	int	 								sc_bws[OHCI_NO_INTRS];

	uint32								sc_eintrs;
	ohci_soft_endpoint 					*sc_isoc_head;
	ohci_soft_endpoint 					*sc_ctrl_head;
	ohci_soft_endpoint 					*sc_bulk_head;

	LIST_HEAD(, ohci_soft_transfer)  	sc_hash_tds[OHCI_HASH_SIZE];
	LIST_HEAD(, ohci_soft_itransfer) 	sc_hash_itds[OHCI_HASH_SIZE];

	int 								sc_noport;
	uint8	 							sc_addr;		// device address
	uint8	 							sc_conf;		// device configuration

#ifdef USB_USE_SOFTINTR
	char 								sc_softwake;
#endif 													// USB_USE_SOFTINTR

	ohci_soft_endpoint					*sc_freeeds;
	ohci_soft_transfer					*sc_freetds;
	ohci_soft_itransfer					*sc_freeitds;

	usbd_xfer_handle 					sc_intrxfer;

	ohci_soft_itransfer					*sc_sidone;
	ohci_soft_transfer 					*sc_sdone;

	char 								sc_vendor[16];
	int 								sc_id_vendor;

	void 								*sc_powerhook;		// cookie from power hook
	void 								*sc_shutdownhook;		// cookie from shutdown hook
	
	uint32	 							sc_control;		// Preserved during suspend/standby
	uint32	 							sc_intre;

} ohci_soft;
*/
#endif // OHCI_SOFT

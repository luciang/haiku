//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Waldemar Kornewald, Waldemar.Kornewald@web.de
//---------------------------------------------------------------------

#ifndef _K_PPP_DEFS__H
#define _K_PPP_DEFS__H

#include <PPPDefs.h>


typedef uint32 interface_id;

// various constants
#define PPP_PULSE_RATE						500000


// module key types (used when loading a module)
enum {
	PPP_LOAD_MODULE_TYPE,
	PPP_DEVICE_TYPE,
	PPP_PROTOCOL_TYPE,
	PPP_AUTHENTICATOR_TYPE,
	PPP_PEER_AUTHENTICATOR_TYPE,
	PPP_MULTILINK_TYPE
};

// PPP events as defined in RFC 1661 (with one exception: PPP_UP_FAILED_EVENT)
enum PPP_EVENT {
	PPP_UP_FAILED_EVENT,
	PPP_UP_EVENT,
	PPP_DOWN_EVENT,
	PPP_OPEN_EVENT,
	PPP_CLOSE_EVENT,
	PPP_TO_GOOD_EVENT,
	PPP_TO_BAD_EVENT,
	PPP_RCR_GOOD_EVENT,
	PPP_RCR_BAD_EVENT,
	PPP_RCA_EVENT,
	PPP_RCN_EVENT,
	PPP_RTR_EVENT,
	PPP_RTA_EVENT,
	PPP_RUC_EVENT,
	PPP_RXJ_GOOD_EVENT,
	PPP_RXJ_BAD_EVENT,
	PPP_RXR_EVENT
};

// LCP protocol types as defined in RFC 1661
// ToDo: add LCP extensions
enum PPP_LCP_TYPE {
	PPP_CONFIGURE_REQUEST = 1,
	PPP_CONFIGURE_ACK = 2,
	PPP_CONFIGURE_NAK = 3,
	PPP_CONFIGURE_REJECT = 4,
	PPP_TERMINATE_REQUEST = 5,
	PPP_TERMINATE_ACK = 6,
	PPP_CODE_REJECT = 7,
	PPP_PROTOCOL_REJECT = 8,
	PPP_ECHO_REQUEST = 9,
	PPP_ECHO_REPLY = 10,
	PPP_DISCARD_REQUEST = 11
};

#define PPP_MIN_LCP_CODE PPP_CONFIGURE_REQUEST
#define PPP_MAX_LCP_CODE PPP_DISCARD_REQUEST

#endif

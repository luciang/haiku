//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Waldemar Kornewald, Waldemar.Kornewald@web.de
//---------------------------------------------------------------------

#ifndef _PPP_REPORT_DEFS__H
#define _PPP_REPORT_DEFS__H


#define PPP_REPORT_TIMEOUT			10

#define PPP_REPORT_DATA_LIMIT		128
	// how much optional data can be added to the report
#define PPP_REPORT_CODE				'_3PR'
	// the code of receive_data() must have this value

// report flags
enum ppp_report_flags {
	PPP_WAIT_FOR_REPLY = 0x1,
	PPP_REMOVE_AFTER_REPORT = 0x2,
	PPP_NO_REPLY_TIMEOUT = 0x4
};

// report types
// the first 16 report types are reserved for the interface manager
enum ppp_report_type {
	PPP_ALL_REPORTS = -1,
		// used only when disabling reports
	PPP_DESTRUCTION_REPORT = 16,
		// the interface is being destroyed (no code is needed)
		// this report is sent even if it was not requested
	PPP_CONNECTION_REPORT = 17
};

// report codes (type-specific)
enum ppp_connection_report_codes {
	PPP_REPORT_GOING_UP = 0,
	PPP_REPORT_UP_SUCCESSFUL = 1,
	PPP_REPORT_DOWN_SUCCESSFUL = 2,
	PPP_REPORT_UP_ABORTED = 3,
	PPP_REPORT_DEVICE_UP_FAILED = 4,
	PPP_REPORT_AUTHENTICATION_SUCCESSFUL = 5,
	PPP_REPORT_PEER_AUTHENTICATION_SUCCESSFUL = 6,
	PPP_REPORT_AUTHENTICATION_FAILED = 7,
	PPP_REPORT_CONNECTION_LOST = 8
};


typedef struct ppp_report_packet {
	int32 type;
	int32 code;
	uint8 length;
	char data[PPP_REPORT_DATA_LIMIT];
} ppp_report_packet;


typedef struct ppp_report_request {
	ppp_report_type type;
	thread_id thread;
	int32 flags;
} ppp_report_request;


#endif

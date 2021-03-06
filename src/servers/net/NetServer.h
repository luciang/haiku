/*
 * Copyright 2006-2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef NET_SERVER_H
#define NET_SERVER_H


#include <SupportDefs.h>

#include <net/if.h>


// NOTE: this header is used by other applications (such as ifconfig,
// and Network) because of these three defines
#define kNetServerSignature		"application/x-vnd.haiku-net_server"
#define kMsgConfigureInterface	'COif'
#define kMsgConfigureResolver	'COrs'


extern bool get_family_index(const char* name, int32& familyIndex);
extern int family_at_index(int32 index);
extern bool parse_address(int32 familyIndex, const char* argument,
	struct sockaddr& address);
extern void set_any_address(int32 familyIndex, struct sockaddr& address);
extern void set_port(int32 familyIndex, struct sockaddr& address, int32 port);

extern bool prepare_request(ifreq& request, const char* name);
extern status_t get_mac_address(const char* device, uint8* address);

#endif	// NET_SERVER_H

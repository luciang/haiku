/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "AutoconfigLooper.h"
#include "DHCPClient.h"
#include "NetServer.h"

#include <errno.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/sockio.h>


static const uint32 kMsgReadyToRun = 'rdyr';


AutoconfigLooper::AutoconfigLooper(BMessenger target, const char* device)
	: BLooper(device),
	fTarget(target),
	fDevice(device)
{
	BMessage ready(kMsgReadyToRun);
	PostMessage(&ready);
}


AutoconfigLooper::~AutoconfigLooper()
{
}


void
AutoconfigLooper::_ReadyToRun()
{
	ifreq request;
	if (!prepare_request(request, fDevice.String()))
		return;

	// set IFF_CONFIGURING flag on interface

	int socket = ::socket(AF_LINK, SOCK_DGRAM, 0);
	if (socket < 0)
		return;

	if (ioctl(socket, SIOCGIFFLAGS, &request, sizeof(struct ifreq)) == 0) {
		request.ifr_flags |= IFF_CONFIGURING;
		ioctl(socket, SIOCSIFFLAGS, &request, sizeof(struct ifreq));
	}

	close(socket);

	// start with DHCP

	DHCPClient* client = new DHCPClient(fTarget, fDevice.String());
	AddHandler(client);

	if (client->Initialize() == B_OK)
		return;

	RemoveHandler(client);
	delete client;

	puts("DHCP failed miserably!");

	// DHCP obviously didn't work out, take some default values for now
	// TODO: have a look at zeroconf
	// TODO: this could also be done add-on based

	BMessage interface(kMsgConfigureInterface);
	interface.AddString("device", fDevice.String());
	interface.AddBool("auto", true);

	uint8 mac[6];
	uint8 last = 56;
	if (get_mac_address(fDevice.String(), mac) == B_OK) {
		// choose IP address depending on the MAC address, if available
		last = mac[0] ^ mac[1] ^ mac[2] ^ mac[3] ^ mac[4] ^ mac[5];
		if (last > 253)
			last = 253;
		else if (last == 0)
			last = 1;
	}

	char string[64];
	// IANA defined the default autoconfig network (for when a DHCP request
	// fails for some reason) as being 169.254.0.0/255.255.0.0. We are only
	// generating the last octet but we could also use the 2 last octets if
	// wanted.
	snprintf(string, sizeof(string), "169.254.0.%u", last);

	BMessage address;
	address.AddString("family", "inet");
	address.AddString("address", string);
	interface.AddMessage("address", &address);

	fTarget.SendMessage(&interface);
}


void
AutoconfigLooper::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgReadyToRun:
			_ReadyToRun();
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


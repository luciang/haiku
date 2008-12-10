/*
 * Copyright 2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef NET_NOTIFICATIONS_H
#define NET_NOTIFICATIONS_H


#include <module.h>


#define NET_NOTIFICATIONS_MODULE_NAME "network/notifications/v1"

namespace BPrivate {
	class KMessage;
};

struct net_notifications_module_info {
	module_info info;

	status_t (*send_notification)(const BPrivate::KMessage* event);
};

// generic syscall interface
#define NET_NOTIFICATIONS_SYSCALLS "network/notifications"

#define NET_NOTIFICATIONS_START_WATCHING	1
#define NET_NOTIFICATIONS_STOP_WATCHING		2

struct net_notifications_control {
	uint32		flags;
	port_id		port;
	uint32		token;
};

// TODO: the following part of this header should end up in a public header
// some day!

#define B_NETWORK_INTERFACE_ADDED		1
#define B_NETWORK_INTERFACE_REMOVED		2
#define B_NETWORK_INTERFACE_CHANGED		3
#define B_NETWORK_DEVICE_LINK_CHANGED	4
// TODO: add routes, stack unloaded/loaded, ... events

enum {
	B_WATCH_NETWORK_INTERFACE_CHANGES	= 0x0001,
	B_WATCH_NETWORK_LINK_CHANGES	 	= 0x0002
};

#define B_NETWORK_MONITOR				'_NTN'

#ifndef _KERNEL_MODE
#	include <Messenger.h>

status_t	start_watching_network(uint32 flags, const BMessenger& target);
status_t	start_watching_network(uint32 flags, const BHandler* handler,
				const BLooper* looper = NULL);

status_t	stop_watching_network(const BMessenger& target);
status_t	stop_watching_network(const BHandler* handler,
				const BLooper* looper = NULL);

#endif	// _KERNEL_MODE

#endif	// NET_NOTIFICATIONS_H

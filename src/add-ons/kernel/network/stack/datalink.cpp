/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Hugo Santos, hugosantos@gmail.com
 */


#include "datalink.h"
#include "domains.h"
#include "interfaces.h"
#include "routes.h"
#include "stack_private.h"
#include "utility.h"

#include <net_device.h>
#include <KernelExport.h>
#include <util/AutoLock.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/route.h>
#include <sys/sockio.h>

#include <new>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


struct datalink_protocol : net_protocol {
	struct net_domain_private *domain;
};

struct interface_protocol : net_datalink_protocol {
	struct net_device_module_info *device_module;
	struct net_device *device;
};


static status_t
device_reader_thread(void *_interface)
{
	net_device_interface *interface = (net_device_interface *)_interface;
	net_device *device = interface->device;
	status_t status = B_OK;

	RecursiveLocker locker(interface->rx_lock);

	while (device->flags & IFF_UP) {
		locker.Unlock();

		net_buffer *buffer;
		status = device->module->receive_data(device, &buffer);

		locker.Lock();

		if (status == B_OK) {
			// feed device monitors
			DeviceMonitorList::Iterator iterator =
				interface->monitor_funcs.GetIterator();
			while (iterator.HasNext()) {
				net_device_monitor *monitor = iterator.Next();
				monitor->receive(monitor, buffer);
			}

			buffer->interface = NULL;
			buffer->type = interface->deframe_func(interface->device, buffer);
			if (buffer->type < 0) {
				gNetBufferModule.free(buffer);
				continue;
			}

			fifo_enqueue_buffer(&interface->receive_queue, buffer);
		} else {
			// In case of error, give the other threads some
			// time to run since this is a near real time thread.
			//
			// TODO: can this value be lower? 1000 works fine in
			//       my system. 10ms seems a bit too much and adds
			//       as latency.
			snooze(10000);
		}

		// if the interface went down IFF_UP was removed
		// and the receive_data() above should have been
		// interrupted. One check should be enough, specially
		// considering the snooze above.
	}

	return status;
}


static struct sockaddr **
interface_address(net_interface *interface, int32 option)
{
	switch (option) {
		case SIOCSIFADDR:
		case SIOCGIFADDR:
			return &interface->address;

		case SIOCSIFNETMASK:
		case SIOCGIFNETMASK:
			return &interface->mask;

		case SIOCSIFBRDADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
			return &interface->destination;

		default:
			return NULL;
	}
}


void
remove_default_routes(net_interface_private *interface, int32 option)
{
	net_route route;
	route.destination = interface->address;
	route.gateway = NULL;
	route.interface = interface;

	if (interface->mask != NULL
		&& (option == SIOCSIFNETMASK || option == SIOCSIFADDR)) {
		route.mask = interface->mask;
		route.flags = 0;
		remove_route(interface->domain, &route);
	}

	if (option == SIOCSIFADDR) {
		route.mask = NULL;
		route.flags = RTF_LOCAL | RTF_HOST;
		remove_route(interface->domain, &route);
	}
}


void
add_default_routes(net_interface_private *interface, int32 option)
{
	net_route route;
	route.destination = interface->address;
	route.gateway = NULL;
	route.interface = interface;

	if (interface->mask != NULL
		&& (option == SIOCSIFNETMASK || option == SIOCSIFADDR)) {
		route.mask = interface->mask;
		route.flags = 0;
		add_route(interface->domain, &route);
	}

	if (option == SIOCSIFADDR) {
		route.mask = NULL;
		route.flags = RTF_LOCAL | RTF_HOST;
		add_route(interface->domain, &route);
	}
}


sockaddr *
reallocate_address(sockaddr **_address, uint32 size)
{
	sockaddr *address = *_address;

	size = max_c(size, sizeof(struct sockaddr));
	if (address != NULL && address->sa_len >= size)
		return address;

	address = (sockaddr *)malloc(size);
	if (address == NULL)
		return NULL;

	free(*_address);
	*_address = address;

	return address;
}


static status_t
datalink_control_interface(net_domain_private *domain, int32 option,
	void *value, size_t *_length, size_t expected, bool getByName)
{
	if (*_length < expected)
		return B_BAD_VALUE;

	ifreq request;
	memset(&request, 0, sizeof(request));

	if (user_memcpy(&request, value, expected) < B_OK)
		return B_BAD_ADDRESS;

	MutexLocker _(domain->lock);
	net_interface *interface = NULL;

	if (getByName)
		interface = find_interface(domain, request.ifr_name);
	else
		interface = find_interface(domain, request.ifr_index);

	status_t status = (interface == NULL) ? ENODEV : B_OK;

	switch (option) {
		case SIOCGIFINDEX:
			if (interface)
				request.ifr_index = interface->index;
			else
				request.ifr_index = 0;
			break;

		case SIOCGIFNAME:
			if (interface)
				strlcpy(request.ifr_name, interface->name, IF_NAMESIZE);
			else
				status = B_BAD_VALUE; // TODO: should be ENXIO?
			break;
	}

	if (status < B_OK)
		return status;

	return user_memcpy(value, &request, sizeof(ifreq));
}


//	#pragma mark - datalink module


status_t
datalink_control(net_domain *_domain, int32 option, void *value,
	size_t *_length)
{
	net_domain_private *domain = (net_domain_private *)_domain;
	if (domain == NULL || domain->family == AF_LINK) {
		// the AF_LINK family is already handled completely in the link protocol
		return B_BAD_VALUE;
	}

	switch (option) {
		case SIOCGIFINDEX:
			return datalink_control_interface(domain, option, value, _length,
				IF_NAMESIZE, true);
		case SIOCGIFNAME:
			return datalink_control_interface(domain, option, value, _length,
				sizeof(ifreq), false);

		case SIOCDIFADDR:
		case SIOCSIFFLAGS:
		{
			struct ifreq request;
			if (user_memcpy(&request, value, sizeof(struct ifreq)) < B_OK)
				return B_BAD_ADDRESS;

			return domain_interface_control(domain, option, &request);
		}

		case SIOCAIFADDR:
		{
			// add new interface address
			struct ifreq request;
			if (user_memcpy(&request, value, sizeof(struct ifreq)) < B_OK)
				return B_BAD_ADDRESS;

			return add_interface_to_domain(domain, request);
		}

		case SIOCGIFCOUNT:
		{
			// count number of interfaces
			struct ifconf config;
			config.ifc_value = count_domain_interfaces();

			return user_memcpy(value, &config, sizeof(struct ifconf));
		}

		case SIOCGIFCONF:
		{
			// retrieve ifreqs for all interfaces
			struct ifconf config;
			if (user_memcpy(&config, value, sizeof(struct ifconf)) < B_OK)
				return B_BAD_ADDRESS;

			status_t result = list_domain_interfaces(config.ifc_buf,
				(size_t *)&config.ifc_len);
			if (result != B_OK)
				return result;

			return user_memcpy(value, &config, sizeof(struct ifconf));
		}

		case SIOCGRTSIZE:
		{
			// determine size of buffer to hold the routing table
			struct ifconf config;
			config.ifc_value = route_table_size(domain);

			return user_memcpy(value, &config, sizeof(struct ifconf));
		}
		case SIOCGRTTABLE:
		{
			// retrieve all routes for this domain
			struct ifconf config;
			if (user_memcpy(&config, value, sizeof(struct ifconf)) < B_OK)
				return B_BAD_ADDRESS;

			return list_routes(domain, config.ifc_buf, config.ifc_len);
		}
		case SIOCGETRT:
			return get_route_information(domain, value, *_length);

		default:
		{
			// try to pass the request to an existing interface
			struct ifreq request;
			if (user_memcpy(&request, value, sizeof(struct ifreq)) < B_OK)
				return B_BAD_ADDRESS;

			MutexLocker _(domain->lock);

			net_interface *interface = find_interface(domain,
				request.ifr_name);
			if (interface == NULL)
				return B_BAD_VALUE;

			// pass the request into the datalink protocol stack
			return interface->first_info->control(
				interface->first_protocol, option, value, *_length);
		}
	}
	return B_BAD_VALUE;
}


status_t
datalink_send_data(struct net_route *route, net_buffer *buffer)
{
	net_interface_private *interface =
		(net_interface_private *)route->interface;

	//dprintf("send buffer (%ld bytes) to interface %s (route flags %lx)\n",
	//	buffer->size, interface->name, route->flags);

	if (route->flags & RTF_REJECT)
		return ENETUNREACH;

	if (route->flags & RTF_LOCAL) {
		// we set the interface here so the buffer is delivered directly
		// to the domain in interfaces.cpp:device_consumer_thread()
		buffer->interface = interface;
		// this one goes back to the domain directly
		return fifo_enqueue_buffer(
			&interface->device_interface->receive_queue, buffer);
	}

	if (route->flags & RTF_GATEWAY) {
		// this route involves a gateway, we need to use the gateway address
		// instead of the destination address:
		if (route->gateway == NULL)
			return B_MISMATCHED_VALUES;
		memcpy(buffer->destination, route->gateway, route->gateway->sa_len);
	}

	// this goes out to the datalink protocols
	return interface->first_info->send_data(interface->first_protocol, buffer);
}


status_t
datalink_send_datagram(net_protocol *protocol, net_domain *domain,
	net_buffer *buffer)
{
	if (protocol == NULL && domain == NULL)
		return B_BAD_VALUE;

	net_protocol_module_info *module = protocol ? protocol->module
		: domain->module;

	if (domain == NULL)
		domain = protocol->module->get_domain(protocol);

	net_route *route = NULL;
	status_t status;
	if (protocol != NULL && protocol->socket->bound_to_device > 0) {
		status = get_device_route(domain, protocol->socket->bound_to_device,
			&route);
	} else
		status = get_buffer_route(domain, buffer, &route);
	if (status < B_OK)
		return status;

	status = module->send_routed_data(protocol, route, buffer);
	put_route(domain, route);
	return status;
}


/*!
	Tests if \a address is a local address in the domain.
	\param _interface will be set to the interface belonging to that address
		if non-NULL.
	\param _matchedType will be set to either zero or MSG_BCAST if non-NULL.
*/
bool
datalink_is_local_address(net_domain *_domain, const struct sockaddr *address,
	net_interface **_interface, uint32 *_matchedType)
{
	net_domain_private *domain = (net_domain_private *)_domain;
	if (domain == NULL || address == NULL)
		return false;

	MutexLocker locker(domain->lock);

	net_interface *interface = NULL;
	net_interface *fallback = NULL;
	uint32 matchedType = 0;

	while (true) {
		interface = (net_interface *)list_get_next_item(
			&domain->interfaces, interface);
		if (interface == NULL)
			break;
		if (interface->address == NULL) {
			fallback = interface;
			continue;
		}

		// check for matching unicast address first
		if (domain->address_module->equal_addresses(interface->address, address))
			break;

		// check for matching broadcast address if interface supports
		// broadcasting (IFF_BROADCAST is a link-level flag, so it is
		// a property of the device)
		if ((interface->device->flags & IFF_BROADCAST)
			&& domain->address_module->equal_addresses(interface->destination,
				address)) {
			matchedType = MSG_BCAST;
			break;
		}
	}

	if (interface == NULL) {
		interface = fallback;
		if (interface == NULL)
			return false;
	}

	if (_interface != NULL)
		*_interface = interface;
	if (_matchedType != NULL)
		*_matchedType = matchedType;
	return true;
}


net_interface *
datalink_get_interface_with_address(net_domain *_domain,
	const sockaddr *address)
{
	net_domain_private *domain = (net_domain_private *)_domain;
	if (domain == NULL)
		return NULL;

	MutexLocker _(domain->lock);

	net_interface *interface = NULL;

	while (true) {
		interface = (net_interface *)list_get_next_item(
			&domain->interfaces, interface);
		if (interface == NULL)
			break;

		if (address == NULL)
			return interface;

		if (domain->address_module->equal_addresses(interface->address,
				address))
			return interface;
	}

	return NULL;
}


net_interface *
datalink_get_interface(net_domain *domain, uint32 index)
{
	if (index == 0)
		return datalink_get_interface_with_address(domain, NULL);

	return find_interface(domain, index);
}


static status_t
datalink_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			return B_ERROR;
	}
}


//	#pragma mark - net_datalink_protocol


status_t
interface_protocol_init(struct net_interface *_interface,
	net_datalink_protocol **_protocol)
{
	net_interface_private *interface = (net_interface_private *)_interface;

	interface_protocol *protocol = new (std::nothrow) interface_protocol;
	if (protocol == NULL)
		return B_NO_MEMORY;

	protocol->device_module = interface->device->module;
	protocol->device = interface->device;

	*_protocol = protocol;
	return B_OK;
}


status_t
interface_protocol_uninit(net_datalink_protocol *protocol)
{
	delete protocol;
	return B_OK;
}


status_t
interface_protocol_send_data(net_datalink_protocol *_protocol,
	net_buffer *buffer)
{
	interface_protocol *protocol = (interface_protocol *)_protocol;
	net_interface_private *interface
		= (net_interface_private *)protocol->interface;

	// TODO: Need to think about this locking. We can't obtain the
	//       RX Lock here (nor would it make sense) as the ARP
	//       module calls send_data() with it's lock held (similiar
	//       to the domain lock, which would violate the locking
	//       protocol).

	DeviceMonitorList::Iterator iterator =
		interface->device_interface->monitor_funcs.GetIterator();
	while (iterator.HasNext()) {
		net_device_monitor *monitor = iterator.Next();
		monitor->receive(monitor, buffer);
	}

	return protocol->device_module->send_data(protocol->device, buffer);
}


status_t
interface_protocol_up(net_datalink_protocol *_protocol)
{
	interface_protocol *protocol = (interface_protocol *)_protocol;
	net_device_interface *deviceInterface =
		((net_interface_private *)protocol->interface)->device_interface;
	net_device *device = protocol->device;

	// This function is called with the RX lock held.

	if (deviceInterface->up_count != 0) {
		deviceInterface->up_count++;
		return B_OK;
	}

	status_t status = protocol->device_module->up(device);
	if (status < B_OK)
		return status;

	if (device->module->receive_data != NULL) {
		// give the thread a nice name
		char name[B_OS_NAME_LENGTH];
		snprintf(name, sizeof(name), "%s reader", device->name);

		deviceInterface->reader_thread =
			spawn_kernel_thread(device_reader_thread, name,
				B_REAL_TIME_DISPLAY_PRIORITY - 10, deviceInterface);
		if (deviceInterface->reader_thread < B_OK)
			return deviceInterface->reader_thread;
	}

	device->flags |= IFF_UP;

	if (device->module->receive_data != NULL)
		resume_thread(deviceInterface->reader_thread);

	deviceInterface->up_count = 1;
	return B_OK;
}


void
interface_protocol_down(net_datalink_protocol *_protocol)
{
	interface_protocol *protocol = (interface_protocol *)_protocol;
	net_device_interface *deviceInterface =
		((net_interface_private *)protocol->interface)->device_interface;

	// This function is called with the RX lock held.
	if (deviceInterface->up_count == 0)
		return;

	deviceInterface->up_count--;

	domain_interface_went_down(protocol->interface);

	if (deviceInterface->up_count > 0)
		return;

	down_device_interface(deviceInterface);
}


status_t
interface_protocol_control(net_datalink_protocol *_protocol,
	int32 option, void *argument, size_t length)
{
	interface_protocol *protocol = (interface_protocol *)_protocol;
	net_interface_private *interface = (net_interface_private *)protocol->interface;

	switch (option) {
		case SIOCSIFADDR:
		case SIOCSIFNETMASK:
		case SIOCSIFBRDADDR:
		case SIOCSIFDSTADDR:
		{
			// set logical interface address
			struct ifreq request;
			if (user_memcpy(&request, argument, sizeof(struct ifreq)) < B_OK)
				return B_BAD_ADDRESS;

			sockaddr **_address = interface_address(interface, option);
			if (_address == NULL)
				return B_BAD_VALUE;

			// allocate new address if needed
			sockaddr *address = reallocate_address(_address,
				request.ifr_addr.sa_len);

			// copy new address over
			if (address != NULL) {
				remove_default_routes(interface, option);
				memcpy(address, &request.ifr_addr, request.ifr_addr.sa_len);

				if (option == SIOCSIFADDR || option == SIOCSIFNETMASK) {
					// reset netmask and broadcast addresses to defaults
					sockaddr *netmask = NULL;
					sockaddr *oldNetmask = NULL;
					if (option == SIOCSIFADDR) {
						netmask = reallocate_address(&interface->mask,
							request.ifr_addr.sa_len);
					} else
						oldNetmask = address;

					sockaddr *broadcast = reallocate_address(
						&interface->destination, request.ifr_addr.sa_len);

					interface->domain->address_module->set_to_defaults(
						netmask, broadcast, interface->address, oldNetmask);
				}

				add_default_routes(interface, option);
			}

			return address != NULL ? B_OK : B_NO_MEMORY;
		}

		case SIOCGIFADDR:
		case SIOCGIFNETMASK:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
		{
			// get logical interface address
			sockaddr **_address = interface_address(interface, option);
			if (_address == NULL)
				return B_BAD_VALUE;

			struct ifreq request;

			sockaddr *address = *_address;
			if (address != NULL)
				memcpy(&request.ifr_addr, address, address->sa_len);
			else {
				request.ifr_addr.sa_len = 2;
				request.ifr_addr.sa_family = AF_UNSPEC;
			}

			// copy address over
			return user_memcpy(&((struct ifreq *)argument)->ifr_addr,
				&request.ifr_addr, request.ifr_addr.sa_len);
		}

		case SIOCGIFFLAGS:
		{
			// get flags
			struct ifreq request;
			request.ifr_flags = interface->flags | interface->device->flags;

			return user_memcpy(&((struct ifreq *)argument)->ifr_flags,
				&request.ifr_flags, sizeof(request.ifr_flags));
		}

		case SIOCGIFPARAM:
		{
			// get interface parameter
			struct ifreq request;
			strlcpy(request.ifr_parameter.base_name, interface->base_name, IF_NAMESIZE);
			strlcpy(request.ifr_parameter.device,
				interface->device_interface->device->name, IF_NAMESIZE);
			request.ifr_parameter.sub_type = 0;
				// TODO: for now, we ignore the sub type...

			return user_memcpy(&((struct ifreq *)argument)->ifr_parameter,
				&request.ifr_parameter, sizeof(request.ifr_parameter));
		}

		case SIOCGIFSTATS:
		{
			// get stats
			return user_memcpy(&((struct ifreq *)argument)->ifr_stats,
				&interface->device_interface->device->stats,
				sizeof(struct ifreq_stats));
		}

		case SIOCGIFTYPE:
		{
			// get type
			struct ifreq request;
			request.ifr_type = interface->type;

			return user_memcpy(&((struct ifreq *)argument)->ifr_type,
				&request.ifr_type, sizeof(request.ifr_type));
		}

		case SIOCGIFMTU:
		{
			// get MTU
			struct ifreq request;
			request.ifr_mtu = interface->mtu;

			return user_memcpy(&((struct ifreq *)argument)->ifr_mtu,
				&request.ifr_mtu, sizeof(request.ifr_mtu));
		}
		case SIOCSIFMTU:
		{
			// set MTU
			struct ifreq request;
			if (user_memcpy(&request, argument, sizeof(struct ifreq)) < B_OK)
				return B_BAD_ADDRESS;

			// check for valid bounds
			if (request.ifr_mtu < 100
				|| (uint32)request.ifr_mtu > interface->device->mtu)
				return B_BAD_VALUE;

			interface->mtu = request.ifr_mtu;
			return B_OK;
		}

		case SIOCSIFMEDIA:
		{
			// set media
			struct ifreq request;
			if (user_memcpy(&request, argument, sizeof(struct ifreq)) < B_OK)
				return B_BAD_ADDRESS;

			return interface->device_interface->device->module->set_media(
				interface->device, request.ifr_media);
		}
		case SIOCGIFMEDIA:
		{
			// get media
			struct ifreq request;
			request.ifr_media = interface->device->media;

			return user_memcpy(&((struct ifreq *)argument)->ifr_media,
				&request.ifr_media, sizeof(request.ifr_media));
		}

		case SIOCGIFMETRIC:
		{
			// get metric
			struct ifreq request;
			request.ifr_metric = interface->metric;

			return user_memcpy(&((struct ifreq *)argument)->ifr_metric,
				&request.ifr_metric, sizeof(request.ifr_metric));
		}
		case SIOCSIFMETRIC:
		{
			// set metric
			struct ifreq request;
			if (user_memcpy(&request, argument, sizeof(struct ifreq)) < B_OK)
				return B_BAD_ADDRESS;

			interface->metric = request.ifr_metric;
			return B_OK;
		}

		case SIOCADDRT:
		case SIOCDELRT:
			// interface related route options
			return control_routes(interface, option, argument, length);
	}

	return protocol->device_module->control(protocol->device,
		option, argument, length);
}


static status_t
interface_protocol_join_multicast(net_datalink_protocol *_protocol,
	const sockaddr *address)
{
	interface_protocol *protocol = (interface_protocol *)_protocol;

	return protocol->device_module->add_multicast(protocol->device, address);
}


static status_t
interface_protocol_leave_multicast(net_datalink_protocol *_protocol,
	const sockaddr *address)
{
	interface_protocol *protocol = (interface_protocol *)_protocol;

	return protocol->device_module->remove_multicast(protocol->device,
		address);
}


net_datalink_module_info gNetDatalinkModule = {
	{
		NET_DATALINK_MODULE_NAME,
		0,
		datalink_std_ops
	},

	datalink_control,
	datalink_send_data,
	datalink_send_datagram,
	datalink_is_local_address,
	datalink_get_interface,
	datalink_get_interface_with_address,

	add_route,
	remove_route,
	get_route,
	get_buffer_route,
	put_route,
	register_route_info,
	unregister_route_info,
	update_route_info
};

net_datalink_protocol_module_info gDatalinkInterfaceProtocolModule = {
	{
		NULL,
		0,
		NULL
	},
	interface_protocol_init,
	interface_protocol_uninit,
	interface_protocol_send_data,
	interface_protocol_up,
	interface_protocol_down,
	interface_protocol_control,
	interface_protocol_join_multicast,
	interface_protocol_leave_multicast,
};

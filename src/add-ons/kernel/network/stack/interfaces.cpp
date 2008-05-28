/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "domains.h"
#include "interfaces.h"
#include "stack_private.h"
#include "utility.h"

#include <net_device.h>

#include <lock.h>
#include <util/AutoLock.h>

#include <KernelExport.h>

#include <net/if_dl.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define TRACE_INTERFACES
#ifdef TRACE_INTERFACES
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


static benaphore sInterfaceLock;
static DeviceInterfaceList sInterfaces;
static uint32 sInterfaceIndex;
static uint32 sDeviceIndex;


static status_t
device_consumer_thread(void *_interface)
{
	net_device_interface *interface = (net_device_interface *)_interface;
	net_device *device = interface->device;
	net_buffer *buffer;

	while (true) {
		ssize_t status = fifo_dequeue_buffer(&interface->receive_queue, 0,
			B_INFINITE_TIMEOUT, &buffer);
		if (status == B_INTERRUPTED)
			continue;
		else if (status < B_OK)
			break;

		if (buffer->interface != NULL) {
			// if the interface is already specified this buffer was
			// delivered locally.

			net_domain *domain = buffer->interface->domain;

			if (domain->module->receive_data(buffer) == B_OK)
				buffer = NULL;
		} else {
			// find handler for this packet
			DeviceHandlerList::Iterator it2 =
				interface->receive_funcs.GetIterator();
			while (buffer && it2.HasNext()) {
				net_device_handler *handler = it2.Next();

				// if the handler returns B_OK, it consumed the buffer
				if (handler->type == buffer->type
					&& handler->func(handler->cookie, device, buffer) == B_OK)
					buffer = NULL;
			}
		}

		if (buffer)
			gNetBufferModule.free(buffer);
	}

	return B_OK;
}


static net_device_interface *
find_device_interface(const char *name)
{
	DeviceInterfaceList::Iterator iterator = sInterfaces.GetIterator();

	while (iterator.HasNext()) {
		net_device_interface *interface = iterator.Next();

		if (!strcmp(interface->device->name, name))
			return interface;
	}

	return NULL;
}


static status_t
domain_receive_adapter(void *cookie, net_device *device, net_buffer *buffer)
{
	net_domain_private *domain = (net_domain_private *)cookie;

	buffer->interface = find_interface(domain, device->index);
	return domain->module->receive_data(buffer);
}


static net_device_interface *
allocate_device_interface(net_device *device, net_device_module_info *module)
{
	net_device_interface *interface = new (std::nothrow) net_device_interface;
	if (interface == NULL)
		goto error_0;

	recursive_lock_init(&interface->rx_lock, "rx lock");

	char name[128];
	snprintf(name, sizeof(name), "%s receive queue", device->name);

	if (init_fifo(&interface->receive_queue, name, 16 * 1024 * 1024) < B_OK)
		goto error_2;

	interface->device = device;
	interface->up_count = 0;
	interface->ref_count = 1;
	interface->deframe_func = NULL;
	interface->deframe_ref_count = 0;

	snprintf(name, sizeof(name), "%s consumer", device->name);

	interface->reader_thread   = -1;
	interface->consumer_thread = spawn_kernel_thread(device_consumer_thread,
		name, B_DISPLAY_PRIORITY, interface);
	if (interface->consumer_thread < B_OK)
		goto error_3;
	resume_thread(interface->consumer_thread);

	// TODO: proper interface index allocation
	device->index = ++sDeviceIndex;
	device->module = module;

	sInterfaces.Add(interface);
	return interface;

error_3:
	uninit_fifo(&interface->receive_queue);

error_2:
	recursive_lock_destroy(&interface->rx_lock);
	delete interface;

error_0:
	return NULL;
}


net_device_interface *
grab_device_interface(net_device_interface *interface)
{
	if (interface == NULL || atomic_add(&interface->ref_count, 1) == 0)
		return NULL;

	return interface;
}


static void
notify_device_monitors(net_device_interface *interface, int32 event)
{
	DeviceMonitorList::Iterator iterator = interface->monitor_funcs.GetIterator();
	while (iterator.HasNext()) {
		// when we call Next() the next item in the list is obtained
		// so it's safe for the "current" item to remove itself.
		net_device_monitor *monitor = iterator.Next();

		monitor->event(monitor, event);
	}
}


//	#pragma mark - interfaces


/*!
	Searches for a specific interface in a domain by name.
	You need to have the domain's lock hold when calling this function.
*/
struct net_interface_private *
find_interface(struct net_domain *domain, const char *name)
{
	net_interface_private *interface = NULL;

	while (true) {
		interface = (net_interface_private *)list_get_next_item(
			&domain->interfaces, interface);
		if (interface == NULL)
			break;

		if (!strcmp(interface->name, name))
			return interface;
	}

	return NULL;
}


/*!
	Searches for a specific interface in a domain by index.
	You need to have the domain's lock hold when calling this function.
*/
struct net_interface_private *
find_interface(struct net_domain *domain, uint32 index)
{
	net_interface_private *interface = NULL;

	while (true) {
		interface = (net_interface_private *)list_get_next_item(
			&domain->interfaces, interface);
		if (interface == NULL)
			break;

		if (interface->index == index)
			return interface;
	}

	return NULL;
}


status_t
create_interface(net_domain *domain, const char *name, const char *baseName,
	net_device_interface *deviceInterface, net_interface_private **_interface)
{
	net_interface_private *interface =
		new (std::nothrow) net_interface_private;
	if (interface == NULL)
		return B_NO_MEMORY;

	strlcpy(interface->name, name, IF_NAMESIZE);
	strlcpy(interface->base_name, baseName, IF_NAMESIZE);
	interface->domain = domain;
	interface->device = deviceInterface->device;

	interface->address = NULL;
	interface->destination = NULL;
	interface->mask = NULL;

	interface->index = ++sInterfaceIndex;
	interface->flags = 0;
	interface->type = 0;
	interface->mtu = deviceInterface->device->mtu;
	interface->metric = 0;
	interface->device_interface = grab_device_interface(deviceInterface);

	status_t status = get_domain_datalink_protocols(interface);
	if (status < B_OK) {
		delete interface;
		return status;
	}

	// Grab a reference to the networking stack, to make sure it won't be
	// unloaded as long as an interface exists
	module_info *module;
	get_module(gNetStackInterfaceModule.info.name, &module);

	*_interface = interface;
	return B_OK;
}


void
interface_set_down(net_interface *interface)
{
	if ((interface->flags & IFF_UP) == 0)
		return;

	interface->flags &= ~IFF_UP;
	interface->first_info->interface_down(interface->first_protocol);
}


void
delete_interface(net_interface_private *interface)
{
	// deleting an interface is fairly complex as we need
	// to clear all references to it throughout the stack

	// this will possibly call (if IFF_UP):
	//  interface_protocol_down()
	//   domain_interface_went_down()
	//    invalidate_routes()
	//     remove_route()
	//      update_route_infos()
	//       get_route_internal()
	//   down_device_interface() -- if upcount reaches 0
	interface_set_down(interface);

	// This call requires the RX Lock to be a recursive
	// lock since each uninit_protocol() call may call
	// again into the stack to unregister a reader for
	// instance, which tries to obtain the RX lock again.
	put_domain_datalink_protocols(interface);

	put_device_interface(interface->device_interface);

	free(interface->address);
	free(interface->destination);
	free(interface->mask);

	delete interface;

	// Release reference of the stack - at this point, our stack may be unloaded
	// if no other interfaces or sockets are left
	put_module(gNetStackInterfaceModule.info.name);
}


void
put_interface(struct net_interface_private *interface)
{
	// TODO: reference counting
	// TODO: better locking scheme
	benaphore_unlock(&((net_domain_private *)interface->domain)->lock);
}


struct net_interface_private *
get_interface(net_domain *_domain, const char *name)
{
	net_domain_private *domain = (net_domain_private *)_domain;
	benaphore_lock(&domain->lock);

	net_interface_private *interface = NULL;
	while (true) {
		interface = (net_interface_private *)list_get_next_item(
			&domain->interfaces, interface);
		if (interface == NULL)
			break;

		if (!strcmp(interface->name, name))
			return interface;
	}

	benaphore_unlock(&domain->lock);
	return NULL;
}


//	#pragma mark - device interfaces


void
get_device_interface_address(net_device_interface *interface, sockaddr *_address)
{
	sockaddr_dl &address = *(sockaddr_dl *)_address;

	address.sdl_family = AF_LINK;
	address.sdl_index = interface->device->index;
	address.sdl_type = interface->device->type;
	address.sdl_nlen = strlen(interface->device->name);
	address.sdl_slen = 0;
	memcpy(address.sdl_data, interface->device->name, address.sdl_nlen);

	address.sdl_alen = interface->device->address.length;
	memcpy(LLADDR(&address), interface->device->address.data, address.sdl_alen);

	address.sdl_len = sizeof(sockaddr_dl) - sizeof(address.sdl_data)
		+ address.sdl_nlen + address.sdl_alen;
}


uint32
count_device_interfaces()
{
	BenaphoreLocker locker(sInterfaceLock);

	DeviceInterfaceList::Iterator iterator = sInterfaces.GetIterator();
	uint32 count = 0;

	while (iterator.HasNext()) {
		iterator.Next();
		count++;
	}

	return count;
}


/*!
	Dumps a list of all interfaces into the supplied userland buffer.
	If the interfaces don't fit into the buffer, an error (\c ENOBUFS) is
	returned.
*/
status_t
list_device_interfaces(void *_buffer, size_t *bufferSize)
{
	BenaphoreLocker locker(sInterfaceLock);

	DeviceInterfaceList::Iterator iterator = sInterfaces.GetIterator();
	UserBuffer buffer(_buffer, *bufferSize);

	while (iterator.HasNext()) {
		net_device_interface *interface = iterator.Next();

		ifreq request;
		strlcpy(request.ifr_name, interface->device->name, IF_NAMESIZE);
		get_device_interface_address(interface, &request.ifr_addr);

		if (buffer.Copy(&request, IF_NAMESIZE
				+ request.ifr_addr.sa_len) == NULL)
			return buffer.Status();
	}

	*bufferSize = buffer.ConsumedAmount();
	return B_OK;
}


/*!
	Releases the reference for the interface. When all references are
	released, the interface is removed.
*/
void
put_device_interface(struct net_device_interface *interface)
{
	if (atomic_add(&interface->ref_count, -1) != 1)
		return;

	{
		BenaphoreLocker locker(sInterfaceLock);
		sInterfaces.Remove(interface);
	}

	uninit_fifo(&interface->receive_queue);
	status_t status;
	wait_for_thread(interface->consumer_thread, &status);

	net_device *device = interface->device;
	device->module->uninit_device(device);
	put_module(device->module->info.name);

	recursive_lock_destroy(&interface->rx_lock);
	delete interface;
}


/*!
	Finds an interface by the specified index and grabs a reference to it.
*/
struct net_device_interface *
get_device_interface(uint32 index)
{
	BenaphoreLocker locker(sInterfaceLock);
	DeviceInterfaceList::Iterator iterator = sInterfaces.GetIterator();

	while (iterator.HasNext()) {
		net_device_interface *interface = iterator.Next();

		if (interface->device->index == index) {
			if (atomic_add(&interface->ref_count, 1) != 0)
				return interface;
		}
	}

	return NULL;
}


/*!
	Finds an interface by the specified name and grabs a reference to it.
	If the interface does not yet exist, a new one is created.
*/
struct net_device_interface *
get_device_interface(const char *name, bool create)
{
	BenaphoreLocker locker(sInterfaceLock);

	net_device_interface *interface = find_device_interface(name);
	if (interface != NULL) {
		if (atomic_add(&interface->ref_count, 1) != 0)
			return interface;

		// try to recreate interface - it just got removed
	}

	if (!create)
		return NULL;

	void *cookie = open_module_list("network/devices");
	if (cookie == NULL)
		return NULL;

	while (true) {
		char moduleName[B_FILE_NAME_LENGTH];
		size_t length = sizeof(moduleName);
		if (read_next_module_name(cookie, moduleName, &length) != B_OK)
			break;

		TRACE(("get_device_interface: ask \"%s\" for %s\n", moduleName, name));

		net_device_module_info *module;
		if (get_module(moduleName, (module_info **)&module) == B_OK) {
			net_device *device;
			status_t status = module->init_device(name, &device);
			if (status == B_OK) {
				interface = allocate_device_interface(device, module);
				if (interface)
					return interface;
				module->uninit_device(device);
			}
			put_module(moduleName);
		}
	}

	return NULL;
}


void
down_device_interface(net_device_interface *interface)
{
	// RX lock must be held when calling down_device_interface.
	// Known callers are `interface_protocol_down' which gets
	// here via one of the following paths:
	//
	// - domain_interface_control() [rx lock held, domain lock held]
	//    interface_set_down()
	//     interface_protocol_down()
	//
	// - domain_interface_control() [rx lock held, domain lock held]
	//    remove_interface_from_domain()
	//     delete_interface()
	//      interface_set_down()

	net_device *device = interface->device;

	device->flags &= ~IFF_UP;
	device->module->down(device);

	notify_device_monitors(interface, B_DEVICE_GOING_DOWN);

	if (device->module->receive_data != NULL) {
		thread_id reader_thread = interface->reader_thread;

		// TODO when setting the interface down,
		//      should we clear the receive queue?

		// one of the callers must hold a reference to the net_device_interface
		// usually it is one of the net_interfaces.
		recursive_lock_unlock(&interface->rx_lock);

		// make sure the reader thread is gone before shutting down the interface
		status_t status;
		wait_for_thread(reader_thread, &status);

		recursive_lock_lock(&interface->rx_lock);
	}
}


//	#pragma mark - devices


/*!
	Unregisters a previously registered deframer function.
	This function is part of the net_manager_module_info API.
*/
status_t
unregister_device_deframer(net_device *device)
{
	BenaphoreLocker locker(sInterfaceLock);

	// find device interface for this device
	net_device_interface *interface = find_device_interface(device->name);
	if (interface == NULL)
		return ENODEV;

	RecursiveLocker _(interface->rx_lock);

	if (--interface->deframe_ref_count == 0)
		interface->deframe_func = NULL;

	return B_OK;
}


/*!
	Registers the deframer function for the specified \a device.
	Note, however, that right now, you can only register one single
	deframer function per device.

	If the need arises, we might want to lift that limitation at a
	later time (which would require a slight API change, though).

	This function is part of the net_manager_module_info API.
*/
status_t
register_device_deframer(net_device *device, net_deframe_func deframeFunc)
{
	BenaphoreLocker locker(sInterfaceLock);

	// find device interface for this device
	net_device_interface *interface = find_device_interface(device->name);
	if (interface == NULL)
		return ENODEV;

	RecursiveLocker _(interface->rx_lock);

	if (interface->deframe_func != NULL && interface->deframe_func != deframeFunc)
		return B_ERROR;

	interface->deframe_func = deframeFunc;
	interface->deframe_ref_count++;
	return B_OK;
}


status_t
register_domain_device_handler(struct net_device *device, int32 type,
	struct net_domain *_domain)
{
	net_domain_private *domain = (net_domain_private *)_domain;
	if (domain->module == NULL || domain->module->receive_data == NULL)
		return B_BAD_VALUE;

	return register_device_handler(device, type, &domain_receive_adapter, domain);
}


status_t
register_device_handler(struct net_device *device, int32 type,
	net_receive_func receiveFunc, void *cookie)
{
	BenaphoreLocker locker(sInterfaceLock);

	// find device interface for this device
	net_device_interface *interface = find_device_interface(device->name);
	if (interface == NULL)
		return ENODEV;

	RecursiveLocker _(interface->rx_lock);

	// see if such a handler already for this device

	DeviceHandlerList::Iterator iterator = interface->receive_funcs.GetIterator();
	while (iterator.HasNext()) {
		net_device_handler *handler = iterator.Next();

		if (handler->type == type)
			return B_ERROR;
	}

	// Add new handler

	net_device_handler *handler = new (std::nothrow) net_device_handler;
	if (handler == NULL)
		return B_NO_MEMORY;

	handler->func = receiveFunc;
	handler->type = type;
	handler->cookie = cookie;
	interface->receive_funcs.Add(handler);
	return B_OK;
}


status_t
unregister_device_handler(struct net_device *device, int32 type)
{
	BenaphoreLocker locker(sInterfaceLock);

	// find device interface for this device
	net_device_interface *interface = find_device_interface(device->name);
	if (interface == NULL)
		return ENODEV;

	RecursiveLocker _(interface->rx_lock);

	// search for the handler

	DeviceHandlerList::Iterator iterator = interface->receive_funcs.GetIterator();
	while (iterator.HasNext()) {
		net_device_handler *handler = iterator.Next();

		if (handler->type == type) {
			// found it
			iterator.Remove();
			delete handler;
			return B_OK;
		}
	}

	return B_BAD_VALUE;
}


status_t
register_device_monitor(net_device *device, net_device_monitor *monitor)
{
	if (monitor->receive == NULL || monitor->event == NULL)
		return B_BAD_VALUE;

	BenaphoreLocker locker(sInterfaceLock);

	// find device interface for this device
	net_device_interface *interface = find_device_interface(device->name);
	if (interface == NULL)
		return ENODEV;

	RecursiveLocker _(interface->rx_lock);
	interface->monitor_funcs.Add(monitor);
	return B_OK;
}


status_t
unregister_device_monitor(net_device *device, net_device_monitor *monitor)
{
	BenaphoreLocker locker(sInterfaceLock);

	// find device interface for this device
	net_device_interface *interface = find_device_interface(device->name);
	if (interface == NULL)
		return ENODEV;

	RecursiveLocker _(interface->rx_lock);

	// search for the monitor

	DeviceMonitorList::Iterator iterator = interface->monitor_funcs.GetIterator();
	while (iterator.HasNext()) {
		if (iterator.Next() == monitor) {
			iterator.Remove();
			return B_OK;
		}
	}

	return B_BAD_VALUE;
}


/*!
	This function is called by device modules in case their link
	state changed, ie. if an ethernet cable was plugged in or
	removed.
*/
status_t
device_link_changed(net_device *device)
{
	return B_OK;
}


/*!
	This function is called by device modules once their device got
	physically removed, ie. a USB networking card is unplugged.
	It is part of the net_manager_module_info API.
*/
status_t
device_removed(net_device *device)
{
	BenaphoreLocker locker(sInterfaceLock);

	// hold a reference to the device interface being removed
	// so our put_() will (eventually) do the final cleanup
	net_device_interface *interface = get_device_interface(device->name, false);
	if (interface == NULL)
		return ENODEV;

	// Propagate the loss of the device throughout the stack.
	// This is very complex, refer to delete_interface() for
	// further details.

	RecursiveLocker _(interface->rx_lock);

	// this will possibly call:
	//  remove_interface_from_domain() [domain gets locked]
	//   delete_interface()
	//    ... [see delete_interface()]
	domain_removed_device_interface(interface);

	notify_device_monitors(interface, B_DEVICE_BEING_REMOVED);

	// By now all of the monitors must have removed themselves. If they
	// didn't, they'll probably wait forever to be callback'ed again.
	interface->monitor_funcs.RemoveAll();

	// All of the readers should be gone as well since we are out of
	// interfaces and `put_domain_datalink_protocols' is called for
	// each delete_interface().

	put_device_interface(interface);

	return B_OK;
}


status_t
device_enqueue_buffer(net_device *device, net_buffer *buffer)
{
	net_device_interface *interface = get_device_interface(device->index);

	if (interface == NULL)
		return ENODEV;

	status_t status = fifo_enqueue_buffer(&interface->receive_queue, buffer);

	put_device_interface(interface);
	return status;
}


//	#pragma mark -


status_t
init_interfaces()
{
	if (benaphore_init(&sInterfaceLock, "net interfaces") < B_OK)
		return B_ERROR;

	new (&sInterfaces) DeviceInterfaceList;
		// static C++ objects are not initialized in the module startup
	return B_OK;
}


status_t
uninit_interfaces()
{
	benaphore_destroy(&sInterfaceLock);
	return B_OK;
}


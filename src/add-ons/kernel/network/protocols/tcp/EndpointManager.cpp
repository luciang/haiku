/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "EndpointManager.h"
#include "TCPEndpoint.h"

#include <NetUtilities.h>

#include <util/AutoLock.h>

#include <KernelExport.h>


//#define TRACE_ENDPOINT_MANAGER
#ifdef TRACE_ENDPOINT_MANAGER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


static const uint16 kLastReservedPort = 1023;
static const uint16 kFirstEphemeralPort = 40000;


size_t
ConnectionHashDefinition::HashKey(EndpointManager *manager, const KeyType &key)
{
	return manager->AddressModule()->hash_address_pair(key.first, key.second);
}


size_t
ConnectionHashDefinition::Hash(EndpointManager *manager, TCPEndpoint *endpoint)
{
	return manager->AddressModule()->hash_address_pair(
			endpoint->LocalAddress(), endpoint->PeerAddress());
}


bool
ConnectionHashDefinition::Compare(EndpointManager *manager, const KeyType &key,
	TCPEndpoint *endpoint)
{
	net_address_module_info *module = manager->AddressModule();

	return module->equal_addresses_and_ports(key.first, endpoint->LocalAddress())
		&& module->equal_addresses_and_ports(key.second, endpoint->PeerAddress());
}


size_t
EndpointHashDefinition::HashKey(EndpointManager *manager, uint16 port)
{
	return port;
}


size_t
EndpointHashDefinition::Hash(EndpointManager *manager, TCPEndpoint *endpoint)
{
	return endpoint->AddressModule()->get_port(endpoint->LocalAddress());
}


bool
EndpointHashDefinition::Compare(EndpointManager *manager, uint16 port,
	TCPEndpoint *endpoint)
{
	return endpoint->AddressModule()->get_port(endpoint->LocalAddress()) == port;
}


EndpointManager::EndpointManager(net_domain *domain)
	: fDomain(domain), fConnectionHash(this), fEndpointHash(this)
{
	benaphore_init(&fLock, "endpoint manager");
}


EndpointManager::~EndpointManager()
{
	benaphore_destroy(&fLock);
}


status_t
EndpointManager::InitCheck() const
{
	if (fConnectionHash.InitCheck() < B_OK)
		return fConnectionHash.InitCheck();

	if (fEndpointHash.InitCheck() < B_OK)
		return fEndpointHash.InitCheck();

	if (fLock.sem < B_OK)
		return fLock.sem;

	return B_OK;
}


//	#pragma mark - connections


/*!
	Returns the endpoint matching the connection.
	You must hold the manager's lock when calling this method.
*/
TCPEndpoint *
EndpointManager::_LookupConnection(const sockaddr *local, const sockaddr *peer)
{
	return fConnectionHash.Lookup(std::make_pair(local, peer));
}


status_t
EndpointManager::SetConnection(TCPEndpoint *endpoint,
	const sockaddr *local, const sockaddr *peer, const sockaddr *interfaceLocal)
{
	TRACE(("EndpointManager::SetConnection(%p)\n", endpoint));

	BenaphoreLocker _(fLock);
	sockaddr localBuffer;

	// need to associate this connection with a real address, not INADDR_ANY
	if (AddressModule()->is_empty_address(local, false)) {
		AddressModule()->set_to(&localBuffer, interfaceLocal);
		AddressModule()->set_port(&localBuffer, AddressModule()->get_port(local));
		local = &localBuffer;
	}

	if (_LookupConnection(local, peer) != NULL)
		return EADDRINUSE;

	AddressModule()->set_to(endpoint->LocalAddress(), local);
	AddressModule()->set_to(endpoint->PeerAddress(), peer);

	if (!fConnectionHash.Insert(endpoint))
		return B_NO_MEMORY;

	return B_OK;
}


status_t
EndpointManager::SetPassive(TCPEndpoint *endpoint)
{
	BenaphoreLocker _(fLock);

	if (!endpoint->IsBound()) {
		// if the socket is unbound first bind it to ephemeral
		sockaddr_storage localAddress;
		AddressModule()->set_to_empty_address((sockaddr *)&localAddress);

		status_t status = _BindToEphemeral(endpoint,
			(sockaddr *)&localAddress);
		if (status < B_OK)
			return status;
	}

	sockaddr_storage passive;
	AddressModule()->set_to_empty_address((sockaddr *)&passive);

	if (_LookupConnection(endpoint->LocalAddress(), (sockaddr *)&passive))
		return EADDRINUSE;

	AddressModule()->set_to(endpoint->PeerAddress(), (sockaddr *)&passive);
	if (!fConnectionHash.Insert(endpoint))
		return B_NO_MEMORY;

	return B_OK;
}


TCPEndpoint *
EndpointManager::FindConnection(sockaddr *local, sockaddr *peer)
{
	BenaphoreLocker _(fLock);

	TCPEndpoint *endpoint = _LookupConnection(local, peer);
	if (endpoint != NULL) {
		TRACE(("TCP: Received packet corresponds to explicit endpoint %p\n", endpoint));
		return endpoint;
	}

	// no explicit endpoint exists, check for wildcard endpoints

	sockaddr wildcard;
	AddressModule()->set_to_empty_address(&wildcard);

	endpoint = _LookupConnection(local, &wildcard);
	if (endpoint != NULL) {
		TRACE(("TCP: Received packet corresponds to wildcard endpoint %p\n", endpoint));
		return endpoint;
	}

	sockaddr localWildcard;
	AddressModule()->set_to_empty_address(&localWildcard);
	AddressModule()->set_port(&localWildcard, AddressModule()->get_port(local));

	endpoint = _LookupConnection(&localWildcard, &wildcard);
	if (endpoint != NULL) {
		TRACE(("TCP: Received packet corresponds to local wildcard endpoint %p\n", endpoint));
		return endpoint;
	}

	// no matching endpoint exists	
	TRACE(("TCP: no matching endpoint!\n"));

	return NULL;
}


//	#pragma mark - endpoints


status_t
EndpointManager::Bind(TCPEndpoint *endpoint, const sockaddr *address)
{
	// TODO check the family:
	//
	// if (!AddressModule()->is_understandable(address))
	//	return EAFNOSUPPORT;

	BenaphoreLocker _(fLock);

	if (AddressModule()->get_port(address) == 0)
		return _BindToEphemeral(endpoint, address);

	return _BindToAddress(endpoint, address);
}


status_t
EndpointManager::_BindToAddress(TCPEndpoint *endpoint, const sockaddr *address)
{
	TRACE(("EndpointManager::BindToAddress(%p)\n", endpoint));

	uint16 port = AddressModule()->get_port(address);

	// TODO this check follows very typical UNIX semantics
	//      and generally should be improved.
	if (ntohs(port) <= kLastReservedPort && geteuid() != 0)
		return B_PERMISSION_DENIED;

	return _Bind(endpoint, address);
}


status_t
EndpointManager::_BindToEphemeral(TCPEndpoint *endpoint,
	const sockaddr *address)
{
	TRACE(("EndpointManager::BindToEphemeral(%p)\n", endpoint));

	uint32 max = kFirstEphemeralPort + 65536;

	for (int32 i = 1; i < 5; i++) {
		// try to retrieve a more or less random port
		uint32 counter = kFirstEphemeralPort;
		uint32 step = i == 4 ? 1 : (system_time() & 0x1f) + 1;

		while (counter < max) {
			uint16 port = counter & 0xffff;
			if (port <= kLastReservedPort)
				port += kLastReservedPort;

			port = htons(port);

			TCPEndpoint *other = fEndpointHash.Lookup(port);
			if (other == NULL) {
				sockaddr_storage newAddress;
				AddressModule()->set_to((sockaddr *)&newAddress, address);
				AddressModule()->set_port((sockaddr *)&newAddress, port);

				// found a port
				TRACE(("   EndpointManager::BindToEphemeral(%p) -> %s\n", endpoint,
					AddressString(Domain(), (sockaddr *)&newAddress, true).Data()));

				return _Bind(endpoint, (sockaddr *)&newAddress);
			}

			counter += step;
		}
	}

	// could not find a port!
	return EADDRINUSE;
}


status_t
EndpointManager::_Bind(TCPEndpoint *endpoint, const sockaddr *address)
{
	uint16 port = AddressModule()->get_port(address);

	TCPEndpoint *first = fEndpointHash.Lookup(port);

	// If there is already an endpoint bound to that port, SO_REUSEADDR has to be
	// specified by the new endpoint to be allowed to bind to that same port.
	// Alternatively, all endpoints must have the SO_REUSEPORT option set.
	if (first != NULL
		&& (endpoint->socket->options & SO_REUSEADDR) == 0
		&& ((endpoint->socket->options & SO_REUSEPORT) == 0
			|| (first->socket->options & SO_REUSEPORT) == 0))
		return EADDRINUSE;

	TCPEndpoint *insertionPoint = NULL;

	if (first != NULL) {
		while (true) {
			// check if this endpoint binds to a wildcard address
			if (AddressModule()->is_empty_address(first->LocalAddress(), false)) {
				// you cannot specialize a wildcard endpoint - you have to open the
				// wildcard endpoint last
				return B_PERMISSION_DENIED;
			}

			if (first->fEndpointNextWithSamePort == NULL)
				break;

			first = first->fEndpointNextWithSamePort;
		}

		insertionPoint = first;
	}

	// Thus far we have checked if the Bind() is allowed

	status_t status = endpoint->next->module->bind(endpoint->next, address);
	if (status < B_OK)
		return status;

	endpoint->fEndpointNextWithSamePort = NULL;

	if (insertionPoint)
		insertionPoint->fEndpointNextWithSamePort = endpoint;
	else
		fEndpointHash.Insert(endpoint);

	return B_OK;
}


status_t
EndpointManager::Unbind(TCPEndpoint *endpoint)
{
	TRACE(("EndpointManager::Unbind(%p)\n", endpoint));

	if (endpoint == NULL || !endpoint->IsBound()) {
		TRACE(("  endpoint is unbound.\n"));
		return B_BAD_VALUE;
	}

	BenaphoreLocker _(fLock);

	TCPEndpoint *other = fEndpointHash.Lookup(
			AddressModule()->get_port(endpoint->LocalAddress()));
	if (other != endpoint) {
		// remove endpoint from the list of endpoints with the same port
		while (other != NULL && other->fEndpointNextWithSamePort != endpoint)
			other = other->fEndpointNextWithSamePort;

		if (other != NULL)
			other->fEndpointNextWithSamePort = endpoint->fEndpointNextWithSamePort;
		else if (!endpoint->fSpawned)
			panic("bound endpoint %p not in hash!", endpoint);
	} else {
		// we need to replace the first endpoint in the list
		fEndpointHash.Remove(endpoint);

		other = endpoint->fEndpointNextWithSamePort;
		if (other != NULL)
			fEndpointHash.Insert(other);
	}

	endpoint->fEndpointNextWithSamePort = NULL;
	fConnectionHash.Remove(endpoint);

	endpoint->LocalAddress()->sa_len = 0;

	return B_OK;
}


status_t
EndpointManager::ReplyWithReset(tcp_segment_header &segment,
	net_buffer *buffer)
{
	TRACE(("TCP: Sending RST...\n"));

	net_buffer *reply = gBufferModule->create(512);
	if (reply == NULL)
		return B_NO_MEMORY;

	AddressModule()->set_to((sockaddr *)&reply->source,
		(sockaddr *)&buffer->destination);
	AddressModule()->set_to((sockaddr *)&reply->destination,
		(sockaddr *)&buffer->source);

	tcp_segment_header outSegment(TCP_FLAG_RESET);
	outSegment.sequence = 0;
	outSegment.acknowledge = 0;
	outSegment.advertised_window = 0;
	outSegment.urgent_offset = 0;

	if ((segment.flags & TCP_FLAG_ACKNOWLEDGE) == 0) {
		outSegment.flags |= TCP_FLAG_ACKNOWLEDGE;
		outSegment.acknowledge = segment.sequence + buffer->size;
	} else
		outSegment.sequence = segment.acknowledge;

	status_t status = add_tcp_header(AddressModule(), outSegment, reply);
	if (status == B_OK)
		status = Domain()->module->send_data(NULL, reply);

	if (status != B_OK)
		gBufferModule->free(reply);

	return status;
}


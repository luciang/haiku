/* 
 * Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "L2capEndpoint.h"
#include "l2cap_address.h"
#include "l2cap_upper.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>


#include <bluetooth/L2CAP/btL2CAP.h>
#define BT_DEBUG_THIS_MODULE
#define MODULE_NAME "l2cap"
#define SUBMODULE_NAME "Endpoint"
#define SUBMODULE_COLOR 32
#include <btDebug.h>


static inline bigtime_t
absolute_timeout(bigtime_t timeout)
{
	if (timeout == 0 || timeout == B_INFINITE_TIMEOUT)
		return timeout;

	// TODO: Make overflow safe!
	return timeout + system_time();
}


L2capEndpoint::L2capEndpoint(net_socket* socket)
	:
	ProtocolSocket(socket),
	fConfigurationSet(false),
	fAcceptSemaphore(-1),
	fPeerEndpoint(NULL),
	fChannel(NULL)
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	/* Set MTU and flow control settings to defaults */
	fConfiguration.imtu = L2CAP_MTU_DEFAULT;
	memcpy(&fConfiguration.iflow, &default_qos , sizeof(l2cap_flow_t) );

	fConfiguration.omtu = L2CAP_MTU_DEFAULT;
	memcpy(&fConfiguration.oflow, &default_qos , sizeof(l2cap_flow_t) );

	fConfiguration.flush_timo = L2CAP_FLUSH_TIMO_DEFAULT;
	fConfiguration.link_timo  = L2CAP_LINK_TIMO_DEFAULT;

	// TODO: XXX not for listening endpoints, imtu should be known first
	gStackModule->init_fifo(&fReceivingFifo, "l2cap recvfifo", L2CAP_MTU_DEFAULT);
}


L2capEndpoint::~L2capEndpoint()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	gStackModule->uninit_fifo(&fReceivingFifo);
}


status_t
L2capEndpoint::Init()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	return B_OK;
}


void
L2capEndpoint::Uninit()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

}


status_t
L2capEndpoint::Open()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	status_t error = ProtocolSocket::Open();
	if (error != B_OK)
		return error;

	return B_OK;
}


status_t
L2capEndpoint::Close()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	if (fAcceptSemaphore != -1) {
		debugf("server socket not handling any channel %p\n", this);
		
		delete_sem(fAcceptSemaphore);
		// TODO: Clean needed stuff
		// Unbind?
		return B_OK;
		
	} else {
		// Client endpoint
		if (fState == CLOSING) {
			debugf("Already closed by peer %p\n", this);
			// TODO: Clean needed stuff
	
			return B_OK;
		} else {
			// Issue Disconnection request over the channel
			fState = CLOSED;
			return l2cap_upper_dis_req(fChannel);
		}
	}
	
}


status_t
L2capEndpoint::Free()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	return B_OK;
}


status_t
L2capEndpoint::Bind(const struct sockaddr *_address)
{
	if (_address == NULL) 
		return B_ERROR;
	
	if (_address->sa_family != AF_BLUETOOTH )
		return EAFNOSUPPORT;

	//if (_address->sa_len != sizeof(struct sockaddr_l2cap))
	//	return EAFNOSUPPORT;

	// TODO: Check if that PSM is already bound
	// return EADDRINUSE;

	// TODO: Check if the PSM is valid, check assigned numbers document for valid
	// psm available to applications.
	// All PSM values shall be ODD, that is, the least significant bit of the least
	// significant octet must be ’1’. Also, all PSM values shall have the least 
	// significant bit of the most significant octet equal to ’0’. This allows the
	// PSM field to be extended beyond 16 bits.
	if ((((struct sockaddr_l2cap*)_address)->l2cap_psm & 1) == 0)
		return B_ERROR;
	
	flowf("\n")
	memcpy(&socket->address, _address, sizeof(struct sockaddr_l2cap));
	socket->address.ss_len = sizeof(struct sockaddr_l2cap);

	fState = BOUND;

	return B_OK;
}


status_t
L2capEndpoint::Unbind()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	return B_OK;
}


status_t
L2capEndpoint::Listen(int backlog)
{
	debugf("[%ld] %p\n", find_thread(NULL), this);
	
	if (fState != BOUND) {
		debugf("Invalid State %p\n", this);
		return B_BAD_VALUE;
	}

	fAcceptSemaphore = create_sem(0, "l2cap serv accept");
	if (fAcceptSemaphore < B_OK) {
		flowf("Semaphore could not be created\n");
		return ENOBUFS;
	}

	gSocketModule->set_max_backlog(socket, backlog);

	fState = LISTEN;

	return B_OK;
}


status_t
L2capEndpoint::Connect(const struct sockaddr *_address)
{
	if (_address->sa_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	debugf("[%ld] %p->L2capEndpoint::Connect(\"%s\")\n", find_thread(NULL), this,
		ConstSocketAddress(&gL2cap4AddressModule, _address).AsString().Data());

	const sockaddr_l2cap* address = (const sockaddr_l2cap*)_address;

	/**/
	TOUCH(address);

	return B_OK;
}


status_t
L2capEndpoint::Accept(net_socket **_acceptedSocket)
{
	debugf("[%ld]\n", find_thread(NULL));

	// MutexLocker locker(fLock);

	status_t status;
	bigtime_t timeout = absolute_timeout(300*1000*1000);

	do {
		// locker.Unlock();

		status = acquire_sem_etc(fAcceptSemaphore, 1, B_ABSOLUTE_TIMEOUT
			| B_CAN_INTERRUPT, timeout);

		if (status != B_OK)
			return status;

		// locker.Lock();
		status = gSocketModule->dequeue_connected(socket, _acceptedSocket);
		
		if (status != B_OK) {
			debugf("Could not dequeue socket %s\n", strerror(status));
		} else {
						
			((L2capEndpoint*)((*_acceptedSocket)->first_protocol))->fState = ESTABLISHED;
			// unassign any channel for the parent endpoint
			fChannel = NULL;
			// we are listening again
			fState = LISTEN;
		}
		
	} while (status != B_OK);

	return status;
}


ssize_t
L2capEndpoint::Send(const iovec *vecs, size_t vecCount,
	ancillary_data_container *ancillaryData)
{
	debugf("[%ld] %p Send(%p, %ld, %p)\n", find_thread(NULL),
		this, vecs, vecCount, ancillaryData);

	return B_OK;
}


ssize_t
L2capEndpoint::Receive(const iovec *vecs, size_t vecCount,
	ancillary_data_container **_ancillaryData, struct sockaddr *_address,
	socklen_t *_addressLength)
{
	debugf("[%ld] %p Receive(%p, %ld)\n", find_thread(NULL),
		this, vecs, vecCount);

	if (fState != ESTABLISHED) {
		debugf("Invalid State %p\n", this);
		return B_BAD_VALUE;
	}

	return B_OK;
}


ssize_t
L2capEndpoint::ReadData(size_t numBytes, uint32 flags, net_buffer** _buffer)
{
	debugf("e->%p num=%ld, f=%ld)\n", this, numBytes, flags);

	if (fState != ESTABLISHED) {
		debugf("Invalid State %p\n", this);
		return B_BAD_VALUE;
	}	

	return gStackModule->fifo_dequeue_buffer(&fReceivingFifo, flags,
		B_INFINITE_TIMEOUT, _buffer);
}


ssize_t
L2capEndpoint::Sendable()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	return B_OK;
}


ssize_t
L2capEndpoint::Receivable()
{
	debugf("[%ld] %p\n", find_thread(NULL), this);

	return 0;
}


L2capEndpoint*
L2capEndpoint::ForPsm(uint16 psm)
{
	L2capEndpoint* endpoint;

	DoublyLinkedList<L2capEndpoint>::Iterator iterator = EndpointList.GetIterator();

	while (iterator.HasNext()) {

		endpoint = iterator.Next();
		if (((struct sockaddr_l2cap*)&endpoint->socket->address)->l2cap_psm == psm
			&& endpoint->fState == LISTEN) {
			// TODO endpoint ocupied, lock it! define a channel for it
			return endpoint;
		}
	}

	return NULL;
}


void
L2capEndpoint::BindToChannel(L2capChannel* channel)
{
	net_socket* newSocket;
	status_t error = gSocketModule->spawn_pending_socket(socket, &newSocket);
	if (error != B_OK) {
		debugf("Could not spawn child for Endpoint %p\n", this);
		// TODO: Handle situation
		return;	
	}

	L2capEndpoint* endpoint = (L2capEndpoint*)newSocket->first_protocol;
	
	endpoint->fChannel = channel;
	endpoint->fPeerEndpoint = this;
	
	channel->endpoint = endpoint;

	debugf("new socket %p/e->%p from parent %p/e->%p\n", newSocket, endpoint, socket, this);

	// Provide the channel the configuration set by the user socket
	channel->configuration = &fConfiguration;
	
	// It might be used keep the last negotiated channel
	// fChannel = channel;

	debugf("New endpoint %p for psm %d, schannel %x dchannel %x\n", endpoint, 
		channel->psm, channel->scid, channel->dcid);
}


status_t
L2capEndpoint::MarkEstablished()
{
	debugf("Endpoint %p for psm %d, schannel %x dchannel %x\n", this, 
		fChannel->psm, fChannel->scid, fChannel->dcid);	
	
	status_t error = gSocketModule->set_connected(socket);
	if (error == B_OK) {
		release_sem(fPeerEndpoint->fAcceptSemaphore);
	} else {
		debugf("Could not set child Endpoint %p %s\n", this, strerror(error));
	}
	
	return error;
}


status_t
L2capEndpoint::MarkClosed()
{
	flowf("\n");
	fState = CLOSED;
	
	return B_OK;
}
	

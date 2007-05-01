/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "ipv4_address.h"
#include "multicast.h"

#include <net_datalink.h>
#include <net_datalink_protocol.h>
#include <net_protocol.h>
#include <net_stack.h>
#include <NetBufferUtilities.h>
#include <ProtocolUtilities.h>

#include <ByteOrder.h>
#include <KernelExport.h>
#include <util/AutoLock.h>
#include <util/list.h>
#include <util/khash.h>
#include <util/DoublyLinkedList.h>
#include <util/MultiHashTable.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <new>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <utility>


//#define TRACE_IPV4
#ifdef TRACE_IPV4
#	define TRACE(format, args...) \
		dprintf("IPv4 [%llu] " format "\n", system_time() , ##args)
#	define TRACE_SK(protocol, format, args...) \
		dprintf("IPv4 [%llu] %p " format "\n", system_time(), \
			protocol , ##args)
#else
#	define TRACE(args...)		do { } while (0)
#	define TRACE_SK(args...)	do { } while (0)
#endif

struct ipv4_header {
#if B_HOST_IS_LENDIAN == 1
	uint8		header_length : 4;	// header length in 32-bit words
	uint8		version : 4;
#else
	uint8		version : 4;
	uint8		header_length : 4;
#endif
	uint8		service_type;
	uint16		total_length;
	uint16		id;
	uint16		fragment_offset;
	uint8		time_to_live;
	uint8		protocol;
	uint16		checksum;
	in_addr_t	source;
	in_addr_t	destination;

	uint16 HeaderLength() const { return header_length << 2; }
	uint16 TotalLength() const { return ntohs(total_length); }
	uint16 FragmentOffset() const { return ntohs(fragment_offset); }
} _PACKED;

#define IP_VERSION				4

// fragment flags
#define IP_RESERVED_FLAG		0x8000
#define IP_DONT_FRAGMENT		0x4000
#define IP_MORE_FRAGMENTS		0x2000
#define IP_FRAGMENT_OFFSET_MASK	0x1fff

#define MAX_HASH_FRAGMENTS 		64
	// slots in the fragment packet's hash
#define FRAGMENT_TIMEOUT		60000000LL
	// discard fragment after 60 seconds

typedef DoublyLinkedList<struct net_buffer,
	DoublyLinkedListCLink<struct net_buffer> > FragmentList;

typedef NetBufferField<uint16, offsetof(ipv4_header, checksum)> IPChecksumField;

struct ipv4_packet_key {
	in_addr_t	source;
	in_addr_t	destination;
	uint16		id;
	uint8		protocol;
};

class FragmentPacket {
	public:
		FragmentPacket(const ipv4_packet_key &key);
		~FragmentPacket();

		status_t AddFragment(uint16 start, uint16 end, net_buffer *buffer,
					bool lastFragment);
		status_t Reassemble(net_buffer *to);

		bool IsComplete() const { return fReceivedLastFragment && fBytesLeft == 0; }

		static uint32 Hash(void *_packet, const void *_key, uint32 range);
		static int Compare(void *_packet, const void *_key);
		static int32 NextOffset() { return offsetof(FragmentPacket, fNext); }
		static void StaleTimer(struct net_timer *timer, void *data);

	private:
		FragmentPacket	*fNext;
		struct ipv4_packet_key fKey;
		bool			fReceivedLastFragment;
		int32			fBytesLeft;
		FragmentList	fFragments;
		net_timer		fTimer;
};


class RawSocket : public DoublyLinkedListLinkImpl<RawSocket>, public DatagramSocket<> {
	public:
		RawSocket(net_socket *socket);
};

typedef DoublyLinkedList<RawSocket> RawSocketList;

typedef MulticastGroupInterface<IPv4Multicast> IPv4GroupInterface;
typedef MulticastFilter<IPv4Multicast> IPv4MulticastFilter;

struct MulticastStateHash {
	typedef void ParentType;
	typedef std::pair<const in_addr *, uint32> KeyType;
	typedef IPv4GroupInterface ValueType;

	size_t HashKey(const KeyType &key) const
		{ return key.first->s_addr ^ key.second; }
	size_t Hash(ValueType *value) const
		{ return HashKey(std::make_pair(&value->Address(),
			value->Interface()->index)); }
	bool Compare(const KeyType &key, ValueType *value) const
		{ return value->Interface()->index == key.second
			&& value->Address().s_addr == key.first->s_addr; }
	bool CompareValues(ValueType *value1, ValueType *value2) const
		{ return value1->Interface()->index == value2->Interface()->index
			&& value1->Address().s_addr == value2->Address().s_addr; }
	HashTableLink<ValueType> *GetLink(ValueType *value) const { return value; }
};


struct ipv4_protocol : net_protocol {
	ipv4_protocol()
		: multicast_filter(this) {}

	RawSocket	*raw;
	uint8		service_type;
	uint8		time_to_live;
	uint8		multicast_time_to_live;
	uint32		flags;

	IPv4MulticastFilter multicast_filter;
};

// protocol flags
#define IP_FLAG_HEADER_INCLUDED	0x01


static const int kDefaultTTL = 254;
static const int kDefaultMulticastTTL = 1;


extern net_protocol_module_info gIPv4Module;
	// we need this in ipv4_std_ops() for registering the AF_INET domain

net_stack_module_info *gStackModule;
net_buffer_module_info *gBufferModule;

static struct net_domain *sDomain;
static net_datalink_module_info *sDatalinkModule;
static net_socket_module_info *sSocketModule;
static int32 sPacketID;
static RawSocketList sRawSockets;
static benaphore sRawSocketsLock;
static benaphore sFragmentLock;
static hash_table *sFragmentHash;
static benaphore sMulticastGroupsLock;

typedef MultiHashTable<MulticastStateHash> MulticastState;
static MulticastState *sMulticastState;

static net_protocol_module_info *sReceivingProtocol[256];
static benaphore sReceivingProtocolLock;


static const char *
print_address(const in_addr *address, char *buf, size_t bufLen)
{
	unsigned int addr = ntohl(address->s_addr);

	snprintf(buf, bufLen, "%u.%u.%u.%u", (addr >> 24) & 0xff,
		(addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);

	return buf;
}


RawSocket::RawSocket(net_socket *socket)
	: DatagramSocket<>("ipv4 raw socket", socket)
{
}


//	#pragma mark -


FragmentPacket::FragmentPacket(const ipv4_packet_key &key)
	:
	fKey(key),
	fReceivedLastFragment(false),
	fBytesLeft(IP_MAXPACKET)
{
	gStackModule->init_timer(&fTimer, StaleTimer, this);
}


FragmentPacket::~FragmentPacket()
{
	// cancel the kill timer
	gStackModule->set_timer(&fTimer, -1);

	// delete all fragments
	net_buffer *buffer;
	while ((buffer = fFragments.RemoveHead()) != NULL) {
		gBufferModule->free(buffer);
	}
}


status_t
FragmentPacket::AddFragment(uint16 start, uint16 end, net_buffer *buffer,
	bool lastFragment)
{
	// restart the timer
	gStackModule->set_timer(&fTimer, FRAGMENT_TIMEOUT);

	if (start >= end) {
		// invalid fragment
		return B_BAD_DATA;
	}

	// Search for a position in the list to insert the fragment

	FragmentList::ReverseIterator iterator = fFragments.GetReverseIterator();
	net_buffer *previous = NULL;
	net_buffer *next = NULL;
	while ((previous = iterator.Next()) != NULL) {
		if (previous->fragment.start <= start) {
			// The new fragment can be inserted after this one
			break;
		}

		next = previous;
	}

	// See if we already have the fragment's data

	if (previous != NULL && previous->fragment.start <= start
		&& previous->fragment.end >= end) {
		// we do, so we can just drop this fragment
		gBufferModule->free(buffer);
		return B_OK;
	}

	TRACE("    previous: %p, next: %p", previous, next);

	// If we have parts of the data already, truncate as needed

	if (previous != NULL && previous->fragment.end > start) {
		TRACE("    remove header %d bytes", previous->fragment.end - start);
		gBufferModule->remove_header(buffer, previous->fragment.end - start);
		start = previous->fragment.end;
	}
	if (next != NULL && next->fragment.start < end) {
		TRACE("    remove trailer %d bytes", next->fragment.start - end);
		gBufferModule->remove_trailer(buffer, next->fragment.start - end);
		end = next->fragment.start;
	}

	// Now try if we can already merge the fragments together

	// We will always keep the last buffer received, so that we can still
	// report an error (in which case we're not responsible for freeing it)

	if (previous != NULL && previous->fragment.end == start) {
		fFragments.Remove(previous);

		buffer->fragment.start = previous->fragment.start;
		buffer->fragment.end = end;

		status_t status = gBufferModule->merge(buffer, previous, false);
		TRACE("    merge previous: %s", strerror(status));
		if (status < B_OK) {
			fFragments.Insert(next, previous);
			return status;
		}

		fFragments.Insert(next, buffer);

		// cut down existing hole
		fBytesLeft -= end - start;

		if (lastFragment && !fReceivedLastFragment) {
			fReceivedLastFragment = true;
			fBytesLeft -= IP_MAXPACKET - end;
		}

		TRACE("    hole length: %d", (int)fBytesLeft);

		return B_OK;
	} else if (next != NULL && next->fragment.start == end) {
		fFragments.Remove(next);

		buffer->fragment.start = start;
		buffer->fragment.end = next->fragment.end;

		status_t status = gBufferModule->merge(buffer, next, true);
		TRACE("    merge next: %s", strerror(status));
		if (status < B_OK) {
			fFragments.Insert((net_buffer *)previous->link.next, next);
			return status;
		}

		fFragments.Insert((net_buffer *)previous->link.next, buffer);

		// cut down existing hole
		fBytesLeft -= end - start;

		if (lastFragment && !fReceivedLastFragment) {
			fReceivedLastFragment = true;
			fBytesLeft -= IP_MAXPACKET - end;
		}

		TRACE("    hole length: %d", (int)fBytesLeft);

		return B_OK;
	}

	// We couldn't merge the fragments, so we need to add it as is

	TRACE("    new fragment: %p, bytes %d-%d", buffer, start, end);

	buffer->fragment.start = start;
	buffer->fragment.end = end;
	fFragments.Insert(next, buffer);

	// update length of the hole, if any
	fBytesLeft -= end - start;

	if (lastFragment && !fReceivedLastFragment) {
		fReceivedLastFragment = true;
		fBytesLeft -= IP_MAXPACKET - end;
	}

	TRACE("    hole length: %d", (int)fBytesLeft);

	return B_OK;
}


/*!
	Reassembles the fragments to the specified buffer \a to.
	This buffer must have been added via AddFragment() before.
*/
status_t
FragmentPacket::Reassemble(net_buffer *to)
{
	if (!IsComplete())
		return NULL;

	net_buffer *buffer = NULL;

	net_buffer *fragment;
	while ((fragment = fFragments.RemoveHead()) != NULL) {
		if (buffer != NULL) {
			status_t status;
			if (to == fragment) {
				status = gBufferModule->merge(fragment, buffer, false);
				buffer = fragment;
			} else
				status = gBufferModule->merge(buffer, fragment, true);
			if (status < B_OK)
				return status;
		} else
			buffer = fragment;
	}

	if (buffer != to)
		panic("ipv4 packet reassembly did not work correctly.\n");

	return B_OK;
}


int
FragmentPacket::Compare(void *_packet, const void *_key)
{
	const ipv4_packet_key *key = (ipv4_packet_key *)_key;
	ipv4_packet_key *packetKey = &((FragmentPacket *)_packet)->fKey;

	if (packetKey->id == key->id
		&& packetKey->source == key->source
		&& packetKey->destination == key->destination
		&& packetKey->protocol == key->protocol)
		return 0;

	return 1;
}


uint32
FragmentPacket::Hash(void *_packet, const void *_key, uint32 range)
{
	const struct ipv4_packet_key *key = (struct ipv4_packet_key *)_key;
	FragmentPacket *packet = (FragmentPacket *)_packet;
	if (packet != NULL)
		key = &packet->fKey;

	return (key->source ^ key->destination ^ key->protocol ^ key->id) % range;
}


/*static*/ void
FragmentPacket::StaleTimer(struct net_timer *timer, void *data)
{
	FragmentPacket *packet = (FragmentPacket *)data;
	TRACE("Assembling FragmentPacket %p timed out!", packet);

	BenaphoreLocker locker(&sFragmentLock);

	hash_remove(sFragmentHash, packet);
	delete packet;
}


//	#pragma mark -


#if 0
static void
dump_ipv4_header(ipv4_header &header)
{
	struct pretty_ipv4 {
	#if B_HOST_IS_LENDIAN == 1
		uint8 a;
		uint8 b;
		uint8 c;
		uint8 d;
	#else
		uint8 d;
		uint8 c;
		uint8 b;
		uint8 a;
	#endif
	};
	struct pretty_ipv4 *src = (struct pretty_ipv4 *)&header.source;
	struct pretty_ipv4 *dst = (struct pretty_ipv4 *)&header.destination;
	dprintf("  version: %d\n", header.version);
	dprintf("  header_length: 4 * %d\n", header.header_length);
	dprintf("  service_type: %d\n", header.service_type);
	dprintf("  total_length: %d\n", header.TotalLength());
	dprintf("  id: %d\n", ntohs(header.id));
	dprintf("  fragment_offset: %d (flags: %c%c%c)\n",
		header.FragmentOffset() & IP_FRAGMENT_OFFSET_MASK,
		(header.FragmentOffset() & IP_RESERVED_FLAG) ? 'r' : '-',
		(header.FragmentOffset() & IP_DONT_FRAGMENT) ? 'd' : '-',
		(header.FragmentOffset() & IP_MORE_FRAGMENTS) ? 'm' : '-');
	dprintf("  time_to_live: %d\n", header.time_to_live);
	dprintf("  protocol: %d\n", header.protocol);
	dprintf("  checksum: %d\n", ntohs(header.checksum));
	dprintf("  source: %d.%d.%d.%d\n", src->a, src->b, src->c, src->d);
	dprintf("  destination: %d.%d.%d.%d\n", dst->a, dst->b, dst->c, dst->d);
}
#endif


/*!
	Attempts to re-assemble fragmented packets.
	\return B_OK if everything went well; if it could reassemble the packet, \a _buffer
		will point to its buffer, otherwise, it will be \c NULL.
	\return various error codes if something went wrong (mostly B_NO_MEMORY)
*/
static status_t
reassemble_fragments(const ipv4_header &header, net_buffer **_buffer)
{
	net_buffer *buffer = *_buffer;
	status_t status;

	struct ipv4_packet_key key;
	key.source = (in_addr_t)header.source;
	key.destination = (in_addr_t)header.destination;
	key.id = header.id;
	key.protocol = header.protocol;

	// TODO: Make locking finer grained.
	BenaphoreLocker locker(&sFragmentLock);

	FragmentPacket *packet = (FragmentPacket *)hash_lookup(sFragmentHash, &key);
	if (packet == NULL) {
		// New fragment packet
		packet = new (std::nothrow) FragmentPacket(key);
		if (packet == NULL)
			return B_NO_MEMORY;

		// add packet to hash
		status = hash_insert(sFragmentHash, packet);
		if (status != B_OK) {
			delete packet;
			return status;
		}
	}

	uint16 fragmentOffset = header.FragmentOffset();
	uint16 start = (fragmentOffset & IP_FRAGMENT_OFFSET_MASK) << 3;
	uint16 end = start + header.TotalLength() - header.HeaderLength();
	bool lastFragment = (fragmentOffset & IP_MORE_FRAGMENTS) == 0;

	TRACE("   Received IPv4 %sfragment of size %d, offset %d.",
		lastFragment ? "last ": "", end - start, start);

	// Remove header unless this is the first fragment
	if (start != 0)
		gBufferModule->remove_header(buffer, header.HeaderLength());

	status = packet->AddFragment(start, end, buffer, lastFragment);
	if (status != B_OK)
		return status;

	if (packet->IsComplete()) {
		hash_remove(sFragmentHash, packet);
			// no matter if reassembling succeeds, we won't need this packet anymore

		status = packet->Reassemble(buffer);
		delete packet;

		// _buffer does not change
		return status;
	}

	// This indicates that the packet is not yet complete
	*_buffer = NULL;
	return B_OK;
}


/*!
	Fragments the incoming buffer and send all fragments via the specified
	\a route.
*/
static status_t
send_fragments(ipv4_protocol *protocol, struct net_route *route,
	net_buffer *buffer, uint32 mtu)
{
	TRACE_SK(protocol, "SendFragments(%lu bytes, mtu %lu)", buffer->size, mtu);

	NetBufferHeaderReader<ipv4_header> originalHeader(buffer);
	if (originalHeader.Status() < B_OK)
		return originalHeader.Status();

	uint16 headerLength = originalHeader->HeaderLength();
	uint32 bytesLeft = buffer->size - headerLength;
	uint32 fragmentOffset = 0;
	status_t status = B_OK;

	net_buffer *headerBuffer = gBufferModule->split(buffer, headerLength);
	if (headerBuffer == NULL)
		return B_NO_MEMORY;

	// TODO we need to make sure ipv4_header is contiguous or
	//      use another construct.
	NetBufferHeaderReader<ipv4_header> bufferHeader(headerBuffer);
	ipv4_header *header = &bufferHeader.Data();

	// adapt MTU to be a multiple of 8 (fragment offsets can only be specified this way)
	mtu -= headerLength;
	mtu &= ~7;
	dprintf("  adjusted MTU to %ld\n", mtu);

	dprintf("  bytesLeft = %ld\n", bytesLeft);
	while (bytesLeft > 0) {
		uint32 fragmentLength = min_c(bytesLeft, mtu);
		bytesLeft -= fragmentLength;
		bool lastFragment = bytesLeft == 0;

		header->total_length = htons(fragmentLength + headerLength);
		header->fragment_offset = htons((lastFragment ? 0 : IP_MORE_FRAGMENTS)
			| (fragmentOffset >> 3));
		header->checksum = 0;
		header->checksum = gStackModule->checksum((uint8 *)header, headerLength);
			// TODO: compute the checksum only for those parts that changed?

		dprintf("  send fragment of %ld bytes (%ld bytes left)\n", fragmentLength, bytesLeft);

		net_buffer *fragmentBuffer;
		if (!lastFragment) {
			fragmentBuffer = gBufferModule->split(buffer, fragmentLength);
			fragmentOffset += fragmentLength;
		} else
			fragmentBuffer = buffer;

		if (fragmentBuffer == NULL) {
			status = B_NO_MEMORY;
			break;
		}

		// copy header to fragment
		status = gBufferModule->prepend(fragmentBuffer, header, headerLength);

		// send fragment
		if (status == B_OK)
			status = sDatalinkModule->send_data(route, fragmentBuffer);

		if (lastFragment) {
			// we don't own the last buffer, so we don't have to free it
			break;
		}

		if (status < B_OK) {
			gBufferModule->free(fragmentBuffer);
			break;
		}
	}

	gBufferModule->free(headerBuffer);
	return status;
}


static status_t
deliver_multicast(net_protocol_module_info *module, net_buffer *buffer,
	bool deliverToRaw)
{
	if (module->deliver_data == NULL)
		return B_OK;

	BenaphoreLocker _(sMulticastGroupsLock);

	sockaddr_in *multicastAddr = (sockaddr_in *)&buffer->destination;

	MulticastState::Iterator it = sMulticastState->Lookup(std::make_pair(
		&multicastAddr->sin_addr, buffer->interface->index));

	while (it.HasNext()) {
		IPv4GroupInterface *state = it.Next();

		if (state->Interface()->index != buffer->interface->index
			|| state->Address().s_addr != multicastAddr->sin_addr.s_addr)
			break;

		if (deliverToRaw && state->Parent()->Socket()->raw == NULL)
			continue;

		if (state->FilterAccepts(buffer)) {
			// as Multicast filters are installed with an IPv4 protocol
			// reference, we need to go and find the appropriate instance
			// related to the 'receiving protocol' with module 'module'.
			net_protocol *proto =
				state->Parent()->Socket()->socket->first_protocol;

			while (proto && proto->module != module)
				proto = proto->next;

			if (proto)
				module->deliver_data(proto, buffer);
		}
	}

	return B_OK;
}


static void
raw_receive_data(net_buffer *buffer)
{
	BenaphoreLocker locker(sRawSocketsLock);

	if (sRawSockets.IsEmpty())
		return;

	TRACE("RawReceiveData(%i)", buffer->protocol);

	if (buffer->flags & MSG_MCAST) {
		// we need to call deliver_multicast here separately as
		// buffer still has the IP header, and it won't in the
		// next call. This isn't very optimized but works for now.
		// A better solution would be to hold separate hash tables
		// and lists for RAW and non-RAW sockets.
		deliver_multicast(&gIPv4Module, buffer, true);
	} else {
		RawSocketList::Iterator iterator = sRawSockets.GetIterator();

		while (iterator.HasNext()) {
			RawSocket *raw = iterator.Next();

			if (raw->Socket()->protocol == buffer->protocol)
				raw->SocketEnqueue(buffer);
		}
	}
}


status_t
IPv4Multicast::JoinGroup(IPv4GroupInterface *state)
{
	BenaphoreLocker _(sMulticastGroupsLock);

	sockaddr_in groupAddr;
	memset(&groupAddr, 0, sizeof(groupAddr));
	groupAddr.sin_addr = state->Address();

	net_interface *intf = state->Interface();

	status_t status =
		intf->first_protocol->module->join_multicast(intf->first_protocol,
			(sockaddr *)&groupAddr);
	if (status < B_OK)
		return status;

	sMulticastState->Insert(state);
	return B_OK;
}


status_t
IPv4Multicast::LeaveGroup(IPv4GroupInterface *state)
{
	BenaphoreLocker _(sMulticastGroupsLock);

	sMulticastState->Remove(state);

	sockaddr_in groupAddr;
	memset(&groupAddr, 0, sizeof(groupAddr));
	groupAddr.sin_addr = state->Address();

	net_interface *intf = state->Interface();

	return intf->first_protocol->module->join_multicast(intf->first_protocol,
		(sockaddr *)&groupAddr);
}


static net_protocol_module_info *
receiving_protocol(uint8 protocol)
{
	net_protocol_module_info *module = sReceivingProtocol[protocol];
	if (module != NULL)
		return module;

	BenaphoreLocker locker(sReceivingProtocolLock);

	module = sReceivingProtocol[protocol];
	if (module != NULL)
		return module;

	if (gStackModule->get_domain_receiving_protocol(sDomain, protocol, &module) == B_OK)
		sReceivingProtocol[protocol] = module;

	return module;
}


static inline void
fill_sockaddr_in(sockaddr_in *target, in_addr_t address)
{
	memset(target, 0, sizeof(sockaddr_in));
	target->sin_family = AF_INET;
	target->sin_len = sizeof(sockaddr_in);
	target->sin_addr.s_addr = address;
}


static status_t
ipv4_delta_group(IPv4GroupInterface *group, int option,
	net_interface *interface, const in_addr *sourceAddr)
{
	switch (option) {
		case IP_ADD_MEMBERSHIP:
			return group->Add();
		case IP_DROP_MEMBERSHIP:
			return group->Drop();
		case IP_BLOCK_SOURCE:
			return group->BlockSource(*sourceAddr);
		case IP_UNBLOCK_SOURCE:
			return group->UnblockSource(*sourceAddr);
		case IP_ADD_SOURCE_MEMBERSHIP:
			return group->AddSSM(*sourceAddr);
		case IP_DROP_SOURCE_MEMBERSHIP:
			return group->DropSSM(*sourceAddr);
	}

	return B_ERROR;
}


static status_t
ipv4_delta_membership(ipv4_protocol *protocol, int option,
	net_interface *interface, const in_addr *groupAddr,
	const in_addr *sourceAddr)
{
	IPv4MulticastFilter &filter = protocol->multicast_filter;
	IPv4GroupInterface *state = NULL;
	status_t status = B_OK;

	switch (option) {
		case IP_ADD_MEMBERSHIP:
		case IP_ADD_SOURCE_MEMBERSHIP:
			status = filter.GetState(*groupAddr, interface, state, true);
			break;

		case IP_DROP_MEMBERSHIP:
		case IP_BLOCK_SOURCE:
		case IP_UNBLOCK_SOURCE:
		case IP_DROP_SOURCE_MEMBERSHIP:
			filter.GetState(*groupAddr, interface, state, false);
			if (state == NULL) {
				if (option == IP_DROP_MEMBERSHIP
					|| option == IP_DROP_SOURCE_MEMBERSHIP)
					return EADDRNOTAVAIL;
				else
					return EINVAL;
			}
			break;
	}

	if (status < B_OK)
		return status;

	status = ipv4_delta_group(state, option, interface, sourceAddr);
	filter.ReturnState(state);
	return status;
}


static int
generic_to_ipv4(int option)
{
	switch (option) {
		case MCAST_JOIN_GROUP:
			return IP_ADD_MEMBERSHIP;
		case MCAST_JOIN_SOURCE_GROUP:
			return IP_ADD_SOURCE_MEMBERSHIP;
		case MCAST_LEAVE_GROUP:
			return IP_DROP_MEMBERSHIP;
		case MCAST_BLOCK_SOURCE:
			return IP_BLOCK_SOURCE;
		case MCAST_UNBLOCK_SOURCE:
			return IP_UNBLOCK_SOURCE;
		case MCAST_LEAVE_SOURCE_GROUP:
			return IP_DROP_SOURCE_MEMBERSHIP;
	}

	return -1;
}


static status_t
ipv4_delta_membership(ipv4_protocol *protocol, int option,
	in_addr *interfaceAddr, in_addr *groupAddr, in_addr *sourceAddr)
{
	net_interface *interface = NULL;

	if (interfaceAddr->s_addr == INADDR_ANY) {
		interface = sDatalinkModule->get_interface_with_address(sDomain, NULL);
	} else {
		sockaddr_in address;
		fill_sockaddr_in(&address, interfaceAddr->s_addr);

		interface = sDatalinkModule->get_interface_with_address(sDomain,
			(sockaddr *)&address);
	}

	if (interface == NULL)
		return ENODEV;

	return ipv4_delta_membership(protocol, option, interface,
		groupAddr, sourceAddr);
}


static status_t
ipv4_generic_delta_membership(ipv4_protocol *protocol, int option,
	uint32 index, const sockaddr_storage *_groupAddr,
	const sockaddr_storage *_sourceAddr)
{
	if (_groupAddr->ss_family != AF_INET)
		return EINVAL;

	if (_sourceAddr && _sourceAddr->ss_family != AF_INET)
		return EINVAL;

	net_interface *interface = sDatalinkModule->get_interface(sDomain, index);
	if (interface == NULL)
		return ENODEV;

	const in_addr *groupAddr, *sourceAddr = NULL;

	groupAddr = &((const sockaddr_in *)_groupAddr)->sin_addr;

	if (_sourceAddr)
		sourceAddr = &((const sockaddr_in *)_sourceAddr)->sin_addr;

	return ipv4_delta_membership(protocol, generic_to_ipv4(option), interface,
		groupAddr, sourceAddr);
}


//	#pragma mark -


net_protocol *
ipv4_init_protocol(net_socket *socket)
{
	ipv4_protocol *protocol = new (std::nothrow) ipv4_protocol();
	if (protocol == NULL)
		return NULL;

	protocol->raw = NULL;
	protocol->service_type = 0;
	protocol->time_to_live = kDefaultTTL;
	protocol->multicast_time_to_live = kDefaultMulticastTTL;
	protocol->flags = 0;
	return protocol;
}


status_t
ipv4_uninit_protocol(net_protocol *_protocol)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;

	delete protocol->raw;
	delete protocol;
	return B_OK;
}


/*!
	Since open() is only called on the top level protocol, when we get here
	it means we are on a SOCK_RAW socket.
*/
status_t
ipv4_open(net_protocol *_protocol)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;

	RawSocket *raw = new (std::nothrow) RawSocket(protocol->socket);
	if (raw == NULL)
		return B_NO_MEMORY;

	status_t status = raw->InitCheck();
	if (status < B_OK) {
		delete raw;
		return status;
	}

	TRACE_SK(protocol, "Open()");

	protocol->raw = raw;

	BenaphoreLocker locker(sRawSocketsLock);
	sRawSockets.Add(raw);
	return B_OK;
}


status_t
ipv4_close(net_protocol *_protocol)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;
	RawSocket *raw = protocol->raw;
	if (raw == NULL)
		return B_ERROR;

	TRACE_SK(protocol, "Close()");

	BenaphoreLocker locker(sRawSocketsLock);
	sRawSockets.Remove(raw);
	delete raw;
	protocol->raw = NULL;

	return B_OK;
}


status_t
ipv4_free(net_protocol *protocol)
{
	return B_OK;
}


status_t
ipv4_connect(net_protocol *protocol, const struct sockaddr *address)
{
	return B_ERROR;
}


status_t
ipv4_accept(net_protocol *protocol, struct net_socket **_acceptedSocket)
{
	return EOPNOTSUPP;
}


static status_t
get_int_option(void *target, size_t length, int value)
{
	if (length != sizeof(int))
		return B_BAD_VALUE;

	return user_memcpy(target, &value, sizeof(int));
}


template<typename Type> static status_t
set_int_option(Type &target, const void *_value, size_t length)
{
	int value;

	if (length != sizeof(int))
		return B_BAD_VALUE;

	if (user_memcpy(&value, _value, sizeof(int)) < B_OK)
		return B_BAD_ADDRESS;

	target = value;
	return B_OK;
}


status_t
ipv4_control(net_protocol *_protocol, int level, int option, void *value,
	size_t *_length)
{
	if ((level & LEVEL_MASK) != IPPROTO_IP)
		return sDatalinkModule->control(sDomain, option, value, _length);

	return B_BAD_VALUE;
}


status_t
ipv4_getsockopt(net_protocol *_protocol, int level, int option, void *value,
	int *_length)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;

	if (level == IPPROTO_IP) {
		if (option == IP_HDRINCL)
			return get_int_option(value, *_length,
				(protocol->flags & IP_FLAG_HEADER_INCLUDED) != 0);
		else if (option == IP_TTL)
			return get_int_option(value, *_length, protocol->time_to_live);
		else if (option == IP_TOS)
			return get_int_option(value, *_length, protocol->service_type);
		else if (IP_MULTICAST_TTL)
			return get_int_option(value, *_length,
				protocol->multicast_time_to_live);
		else if (option == IP_ADD_MEMBERSHIP
			|| option == IP_DROP_MEMBERSHIP
			|| option == IP_BLOCK_SOURCE
			|| option == IP_UNBLOCK_SOURCE
			|| option == IP_ADD_SOURCE_MEMBERSHIP
			|| option == IP_DROP_SOURCE_MEMBERSHIP
			|| option == MCAST_JOIN_GROUP
			|| option == MCAST_LEAVE_GROUP
			|| option == MCAST_BLOCK_SOURCE
			|| option == MCAST_UNBLOCK_SOURCE
			|| option == MCAST_JOIN_SOURCE_GROUP
			|| option == MCAST_LEAVE_SOURCE_GROUP) {
				// RFC 3678, Section 4.1:
				// ``An error of EOPNOTSUPP is returned if these options are
				// used with getsockopt().''
				return EOPNOTSUPP;
		} else {
			dprintf("IPv4::getsockopt(): get unknown option: %d\n", option);
			return ENOPROTOOPT;
		}
	}

	return sSocketModule->get_option(protocol->socket, level, option, value,
		_length);
}


status_t
ipv4_setsockopt(net_protocol *_protocol, int level, int option,
	const void *value, int length)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;

	if (level == IPPROTO_IP) {
		if (option == IP_HDRINCL) {
			int headerIncluded;
			if (length != sizeof(int))
				return B_BAD_VALUE;
			if (user_memcpy(&headerIncluded, value, sizeof(headerIncluded)) < B_OK)
				return B_BAD_ADDRESS;

			if (headerIncluded)
				protocol->flags |= IP_FLAG_HEADER_INCLUDED;
			else
				protocol->flags &= ~IP_FLAG_HEADER_INCLUDED;
			return B_OK;
		} else if (option == IP_TTL) {
			return set_int_option(protocol->time_to_live, value, length);
		} else if (option == IP_TOS) {
			return set_int_option(protocol->service_type, value, length);
		} else if (option == IP_MULTICAST_TTL) {
			return set_int_option(protocol->multicast_time_to_live, value,
				length);
		} else if (option == IP_ADD_MEMBERSHIP
			|| option == IP_DROP_MEMBERSHIP) {
			ip_mreq mreq;
			if (length != sizeof(ip_mreq))
				return B_BAD_VALUE;
			if (user_memcpy(&mreq, value, sizeof(ip_mreq)) < B_OK)
				return B_BAD_ADDRESS;

			return ipv4_delta_membership(protocol, option, &mreq.imr_interface,
				&mreq.imr_multiaddr, NULL);
		} else if (option == IP_BLOCK_SOURCE
			|| option == IP_UNBLOCK_SOURCE
			|| option == IP_ADD_SOURCE_MEMBERSHIP
			|| option == IP_DROP_SOURCE_MEMBERSHIP) {
			ip_mreq_source mreq;
			if (length != sizeof(ip_mreq_source))
				return B_BAD_VALUE;
			if (user_memcpy(&mreq, value, sizeof(ip_mreq_source)) < B_OK)
				return B_BAD_ADDRESS;

			return ipv4_delta_membership(protocol, option, &mreq.imr_interface,
				&mreq.imr_multiaddr, &mreq.imr_sourceaddr);
		} else if (option == MCAST_LEAVE_GROUP
			|| option == MCAST_JOIN_GROUP) {
			group_req greq;
			if (length != sizeof(group_req))
				return B_BAD_VALUE;
			if (user_memcpy(&greq, value, sizeof(group_req)) < B_OK)
				return B_BAD_ADDRESS;

			return ipv4_generic_delta_membership(protocol, option,
				greq.gr_interface, &greq.gr_group, NULL);
		} else if (option == MCAST_BLOCK_SOURCE
			|| option == MCAST_UNBLOCK_SOURCE
			|| option == MCAST_JOIN_SOURCE_GROUP
			|| option == MCAST_LEAVE_SOURCE_GROUP) {
			group_source_req greq;
			if (length != sizeof(group_source_req))
				return B_BAD_VALUE;
			if (user_memcpy(&greq, value, sizeof(group_source_req)) < B_OK)
				return B_BAD_ADDRESS;

			return ipv4_generic_delta_membership(protocol, option,
				greq.gsr_interface, &greq.gsr_group, &greq.gsr_source);
		} else {
			dprintf("IPv4::setsockopt(): set unknown option: %d\n", option);
			return ENOPROTOOPT;
		}
	}

	return sSocketModule->set_option(protocol->socket, level, option,
		value, length);
}


status_t
ipv4_bind(net_protocol *protocol, const struct sockaddr *address)
{
	if (address->sa_family != AF_INET)
		return EAFNOSUPPORT;

	// only INADDR_ANY and addresses of local interfaces are accepted:
	if (((sockaddr_in *)address)->sin_addr.s_addr == INADDR_ANY
		|| IN_MULTICAST(((sockaddr_in *)address)->sin_addr.s_addr)
		|| sDatalinkModule->is_local_address(sDomain, address, NULL, NULL)) {
		memcpy(&protocol->socket->address, address, sizeof(struct sockaddr_in));
		protocol->socket->address.ss_len = sizeof(struct sockaddr_in);
			// explicitly set length, as our callers can't be trusted to
			// always provide the correct length!
		return B_OK;
	}

	return B_ERROR;
		// address is unknown on this host
}


status_t
ipv4_unbind(net_protocol *protocol, struct sockaddr *address)
{
	// nothing to do here
	return B_OK;
}


status_t
ipv4_listen(net_protocol *protocol, int count)
{
	return EOPNOTSUPP;
}


status_t
ipv4_shutdown(net_protocol *protocol, int direction)
{
	return EOPNOTSUPP;
}


status_t
ipv4_send_routed_data(net_protocol *_protocol, struct net_route *route,
	net_buffer *buffer)
{
	if (route == NULL)
		return B_BAD_VALUE;

	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;
	net_interface *interface = route->interface;

	TRACE_SK(protocol, "SendRoutedData(%p, %p [%ld bytes])", route, buffer,
		buffer->size);

	sockaddr_in &source = *(sockaddr_in *)&buffer->source;
	sockaddr_in &destination = *(sockaddr_in *)&buffer->destination;

	bool headerIncluded = false, checksumNeeded = true;
	if (protocol != NULL)
		headerIncluded = (protocol->flags & IP_FLAG_HEADER_INCLUDED) != 0;

	buffer->flags &= ~(MSG_BCAST | MSG_MCAST);

	if (destination.sin_addr.s_addr == INADDR_ANY)
		return EDESTADDRREQ;
	else if (destination.sin_addr.s_addr == INADDR_BROADCAST) {
		// TODO check for local broadcast addresses as well?
		if (protocol && !(protocol->socket->options & SO_BROADCAST))
			return B_BAD_VALUE;
		buffer->flags |= MSG_BCAST;
	} else if (IN_MULTICAST(destination.sin_addr.s_addr)) {
		buffer->flags |= MSG_MCAST;
	}

	// Add IP header (if needed)

	if (!headerIncluded) {
		NetBufferPrepend<ipv4_header> header(buffer);
		if (header.Status() < B_OK)
			return header.Status();

		header->version = IP_VERSION;
		header->header_length = sizeof(ipv4_header) / 4;
		header->service_type = protocol ? protocol->service_type : 0;
		header->total_length = htons(buffer->size);
		header->id = htons(atomic_add(&sPacketID, 1));
		header->fragment_offset = 0;
		if (protocol)
			header->time_to_live = (buffer->flags & MSG_MCAST) ?
				protocol->multicast_time_to_live : protocol->time_to_live;
		else
			header->time_to_live = (buffer->flags & MSG_MCAST) ?
				kDefaultMulticastTTL : kDefaultTTL;
		header->protocol = protocol ? protocol->socket->protocol : buffer->protocol;
		header->checksum = 0;

		header->source = source.sin_addr.s_addr;
		header->destination = destination.sin_addr.s_addr;
	} else {
		// if IP_HDRINCL, check if the source address is set
		NetBufferHeaderReader<ipv4_header> header(buffer);
		if (header.Status() < B_OK)
			return header.Status();

		if (header->source == 0) {
			header->source = source.sin_addr.s_addr;
			header->checksum = 0;
			header.Sync();
		} else
			checksumNeeded = false;
	}

	if (buffer->size > 0xffff)
		return EMSGSIZE;

	if (checksumNeeded)
		*IPChecksumField(buffer) = gBufferModule->checksum(buffer, 0,
			sizeof(ipv4_header), true);

	TRACE_SK(protocol, "  SendRoutedData(): header chksum: %ld, buffer checksum: %ld",
		gBufferModule->checksum(buffer, 0, sizeof(ipv4_header), true),
		gBufferModule->checksum(buffer, 0, buffer->size, true));

	TRACE_SK(protocol, "  SendRoutedData(): destination: %08lx",
		ntohl(destination.sin_addr.s_addr));

	uint32 mtu = route->mtu ? route->mtu : interface->mtu;
	if (buffer->size > mtu) {
		// we need to fragment the packet
		return send_fragments(protocol, route, buffer, mtu);
	}

	return sDatalinkModule->send_data(route, buffer);
}


status_t
ipv4_send_data(net_protocol *_protocol, net_buffer *buffer)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;

	TRACE_SK(protocol, "SendData(%p [%ld bytes])", buffer, buffer->size);

	if (protocol && (protocol->flags & IP_FLAG_HEADER_INCLUDED)) {
		if (buffer->size < sizeof(ipv4_header))
			return EINVAL;

		sockaddr_in *source = (sockaddr_in *)&buffer->source;
		sockaddr_in *destination = (sockaddr_in *)&buffer->destination;

		fill_sockaddr_in(source, *NetBufferField<in_addr_t,
			offsetof(ipv4_header, source)>(buffer));
		fill_sockaddr_in(destination, *NetBufferField<in_addr_t,
			offsetof(ipv4_header, destination)>(buffer));
	}

	return sDatalinkModule->send_datagram(protocol, sDomain, buffer);
}


ssize_t
ipv4_send_avail(net_protocol *protocol)
{
	return B_ERROR;
}


status_t
ipv4_read_data(net_protocol *_protocol, size_t numBytes, uint32 flags,
	net_buffer **_buffer)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;
	RawSocket *raw = protocol->raw;
	if (raw == NULL)
		return B_ERROR;

	TRACE_SK(protocol, "ReadData(%lu, 0x%lx)", numBytes, flags);

	return raw->SocketDequeue(flags, _buffer);
}


ssize_t
ipv4_read_avail(net_protocol *_protocol)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;
	RawSocket *raw = protocol->raw;
	if (raw == NULL)
		return B_ERROR;

	return raw->AvailableData();
}


struct net_domain *
ipv4_get_domain(net_protocol *protocol)
{
	return sDomain;
}


size_t
ipv4_get_mtu(net_protocol *protocol, const struct sockaddr *address)
{
	net_route *route = sDatalinkModule->get_route(sDomain, address);
	if (route == NULL)
		return 0;

	size_t mtu;
	if (route->mtu != 0)
		mtu = route->mtu;
	else
		mtu = route->interface->mtu;

	sDatalinkModule->put_route(sDomain, route);
	return mtu - sizeof(ipv4_header);
}


status_t
ipv4_receive_data(net_buffer *buffer)
{
	TRACE("ReceiveData(%p [%ld bytes])", buffer, buffer->size);

	NetBufferHeaderReader<ipv4_header> bufferHeader(buffer);
	if (bufferHeader.Status() < B_OK)
		return bufferHeader.Status();

	ipv4_header &header = bufferHeader.Data();
	//dump_ipv4_header(header);

	if (header.version != IP_VERSION)
		return B_BAD_TYPE;

	uint16 packetLength = header.TotalLength();
	uint16 headerLength = header.HeaderLength();
	if (packetLength > buffer->size
		|| headerLength < sizeof(ipv4_header))
		return B_BAD_DATA;

	// TODO: would be nice to have a direct checksum function somewhere
	if (gBufferModule->checksum(buffer, 0, headerLength, true) != 0)
		return B_BAD_DATA;

	struct sockaddr_in &source = *(struct sockaddr_in *)&buffer->source;
	struct sockaddr_in &destination = *(struct sockaddr_in *)&buffer->destination;

	fill_sockaddr_in(&source, header.source);
	fill_sockaddr_in(&destination, header.destination);

	// lower layers notion of Broadcast or Multicast have no relevance to us
	buffer->flags &= ~(MSG_BCAST | MSG_MCAST);

	if (header.destination == INADDR_BROADCAST) {
		buffer->flags |= MSG_BCAST;
	} else if (IN_MULTICAST(header.destination)) {
		buffer->flags |= MSG_MCAST;
	} else {
		uint32 matchedAddressType = 0;
		// test if the packet is really for us
		if (!sDatalinkModule->is_local_address(sDomain, (sockaddr*)&destination,
			&buffer->interface, &matchedAddressType)) {
			TRACE("  ReceiveData(): packet was not for us %lx -> %lx",
				ntohl(header.source), ntohl(header.destination));
			return B_ERROR;
		}

		// copy over special address types (MSG_BCAST or MSG_MCAST):
		buffer->flags |= matchedAddressType;
	}

	uint8 protocol = buffer->protocol = header.protocol;

	// remove any trailing/padding data
	status_t status = gBufferModule->trim(buffer, packetLength);
	if (status < B_OK)
		return status;

	// check for fragmentation
	uint16 fragmentOffset = ntohs(header.fragment_offset);
	if ((fragmentOffset & IP_MORE_FRAGMENTS) != 0
		|| (fragmentOffset & IP_FRAGMENT_OFFSET_MASK) != 0) {
		// this is a fragment
		TRACE("  ReceiveData(): Found a Fragment!");
		status = reassemble_fragments(header, &buffer);
		TRACE("  ReceiveData():  -> %s", strerror(status));
		if (status != B_OK)
			return status;

		if (buffer == NULL) {
			// buffer was put into fragment packet
			TRACE("  ReceiveData(): Not yet assembled.");
			return B_OK;
		}
	}

	// Since the buffer might have been changed (reassembled fragment)
	// we must no longer access bufferHeader or header anymore after
	// this point

	raw_receive_data(buffer);

	gBufferModule->remove_header(buffer, headerLength);
		// the header is of variable size and may include IP options
		// (that we ignore for now)

	net_protocol_module_info *module = receiving_protocol(protocol);
	if (module == NULL) {
		// no handler for this packet
		return EAFNOSUPPORT;
	}

	if (buffer->flags & MSG_MCAST) {
		// Unfortunely historical reasons dictate that the IP multicast
		// model be a little different from the unicast one. We deliver
		// this frame directly to all sockets registered with interest
		// for this multicast group.
		return deliver_multicast(module, buffer, false);
	}

	return module->receive_data(buffer);
}


status_t
ipv4_deliver_data(net_protocol *_protocol, net_buffer *buffer)
{
	ipv4_protocol *protocol = (ipv4_protocol *)_protocol;

	if (protocol->raw == NULL)
		return B_ERROR;

	return protocol->raw->SocketEnqueue(buffer);
}


status_t
ipv4_error(uint32 code, net_buffer *data)
{
	return B_ERROR;
}


status_t
ipv4_error_reply(net_protocol *protocol, net_buffer *causedError, uint32 code,
	void *errorData)
{
	return B_ERROR;
}


static int
dump_ipv4_multicast(int argc, char *argv[])
{
	MulticastState::Iterator it = sMulticastState->GetIterator();

	while (it.HasNext()) {
		IPv4GroupInterface *state = it.Next();

		char addrBuf[64];

		kprintf("%p: group <%s, %s> sock %p\n", state,
			state->Interface()->name, print_address(&state->Address(),
				addrBuf, sizeof(addrBuf)), state->Parent()->Socket());
	}

	return 0;
}


//	#pragma mark -


status_t
init_ipv4()
{
	sPacketID = (int32)system_time();

	status_t status = benaphore_init(&sRawSocketsLock, "raw sockets");
	if (status < B_OK)
		return status;

	status = benaphore_init(&sFragmentLock, "IPv4 Fragments");
	if (status < B_OK)
		goto err1;

	status = benaphore_init(&sMulticastGroupsLock, "IPv4 multicast groups");
	if (status < B_OK)
		goto err2;

	status = benaphore_init(&sReceivingProtocolLock, "IPv4 receiving protocols");
	if (status < B_OK)
		goto err3;

	sMulticastState = new MulticastState();
	if (sMulticastState == NULL)
		goto err4;

	status = sMulticastState->InitCheck();
	if (status < B_OK)
		goto err5;

	sFragmentHash = hash_init(MAX_HASH_FRAGMENTS, FragmentPacket::NextOffset(),
		&FragmentPacket::Compare, &FragmentPacket::Hash);
	if (sFragmentHash == NULL)
		goto err5;

	new (&sRawSockets) RawSocketList;
		// static initializers do not work in the kernel,
		// so we have to do it here, manually
		// TODO: for modules, this shouldn't be required

	status = gStackModule->register_domain_protocols(AF_INET, SOCK_RAW, 0,
		"network/protocols/ipv4/v1", NULL);
	if (status < B_OK)
		goto err6;

	status = gStackModule->register_domain(AF_INET, "internet", &gIPv4Module,
		&gIPv4AddressModule, &sDomain);
	if (status < B_OK)
		goto err6;

	add_debugger_command("ipv4_multicast", dump_ipv4_multicast,
		"list all current IPv4 multicast states");

	return B_OK;

err6:
	hash_uninit(sFragmentHash);
err5:
	delete sMulticastState;
err4:
	benaphore_destroy(&sReceivingProtocolLock);
err3:
	benaphore_destroy(&sMulticastGroupsLock);
err2:
	benaphore_destroy(&sFragmentLock);
err1:
	benaphore_destroy(&sRawSocketsLock);
	return status;
}


status_t
uninit_ipv4()
{
	benaphore_lock(&sReceivingProtocolLock);

	remove_debugger_command("ipv4_multicast", dump_ipv4_multicast);

	// put all the domain receiving protocols we gathered so far
	for (uint32 i = 0; i < 256; i++) {
		if (sReceivingProtocol[i] != NULL)
			gStackModule->put_domain_receiving_protocol(sDomain, i);
	}

	gStackModule->unregister_domain(sDomain);
	benaphore_unlock(&sReceivingProtocolLock);

	delete sMulticastState;
	hash_uninit(sFragmentHash);

	benaphore_destroy(&sMulticastGroupsLock);
	benaphore_destroy(&sFragmentLock);
	benaphore_destroy(&sRawSocketsLock);
	benaphore_destroy(&sReceivingProtocolLock);

	put_module(NET_STACK_MODULE_NAME);
	return B_OK;
}


static status_t
ipv4_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return init_ipv4();
		case B_MODULE_UNINIT:
			return uninit_ipv4();

		default:
			return B_ERROR;
	}
}


net_protocol_module_info gIPv4Module = {
	{
		"network/protocols/ipv4/v1",
		0,
		ipv4_std_ops
	},
	ipv4_init_protocol,
	ipv4_uninit_protocol,
	ipv4_open,
	ipv4_close,
	ipv4_free,
	ipv4_connect,
	ipv4_accept,
	ipv4_control,
	ipv4_getsockopt,
	ipv4_setsockopt,
	ipv4_bind,
	ipv4_unbind,
	ipv4_listen,
	ipv4_shutdown,
	ipv4_send_data,
	ipv4_send_routed_data,
	ipv4_send_avail,
	ipv4_read_data,
	ipv4_read_avail,
	ipv4_get_domain,
	ipv4_get_mtu,
	ipv4_receive_data,
	ipv4_deliver_data,
	ipv4_error,
	ipv4_error_reply,
};

module_dependency module_dependencies[] = {
	{NET_STACK_MODULE_NAME, (module_info **)&gStackModule},
	{NET_BUFFER_MODULE_NAME, (module_info **)&gBufferModule},
	{NET_DATALINK_MODULE_NAME, (module_info **)&sDatalinkModule},
	{NET_SOCKET_MODULE_NAME, (module_info **)&sSocketModule},
	{}
};

module_info *modules[] = {
	(module_info *)&gIPv4Module,
	NULL
};

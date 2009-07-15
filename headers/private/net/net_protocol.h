/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H


#include <net_buffer.h>
#include <net_socket.h>


// level flags to pass to control()
#define LEVEL_SET_OPTION		0x10000000
#define LEVEL_GET_OPTION		0x20000000
#define LEVEL_DRIVER_IOCTL		0x0f000000
#define LEVEL_MASK				0x0fffffff

struct ancillary_data_container;
struct ancillary_data_header;

struct iovec;

typedef struct net_protocol {
	struct net_protocol				*next;
	struct net_protocol_module_info	*module;
	net_socket						*socket;
} net_protocol;

// net_protocol_module_info::flags field
#define NET_PROTOCOL_ATOMIC_MESSAGES 0x01

struct net_protocol_module_info {
	module_info info;
	uint32		flags;

	net_protocol *(*init_protocol)(net_socket *socket);
	status_t	(*uninit_protocol)(net_protocol *self);

	status_t	(*open)(net_protocol *self);
	status_t	(*close)(net_protocol *self);
	status_t	(*free)(net_protocol *self);

	status_t	(*connect)(net_protocol *self, const struct sockaddr *address);
	status_t	(*accept)(net_protocol *self, struct net_socket **_acceptedSocket);
	status_t	(*control)(net_protocol *self, int level, int option, void *value,
					size_t *_length);
	status_t	(*getsockopt)(net_protocol *self, int level, int option,
					void *value, int *_length);
	status_t	(*setsockopt)(net_protocol *self, int level, int option,
					const void *value, int length);

	status_t	(*bind)(net_protocol *self, const struct sockaddr *address);
	status_t	(*unbind)(net_protocol *self, struct sockaddr *address);
	status_t	(*listen)(net_protocol *self, int count);
	status_t	(*shutdown)(net_protocol *self, int direction);

	status_t	(*send_data)(net_protocol *self, net_buffer *buffer);
	status_t	(*send_routed_data)(net_protocol *self,
					struct net_route *route, net_buffer *buffer);
	ssize_t		(*send_avail)(net_protocol *self);

	status_t	(*read_data)(net_protocol *self, size_t numBytes, uint32 flags,
					net_buffer **_buffer);
	ssize_t		(*read_avail)(net_protocol *self);

	struct net_domain *(*get_domain)(net_protocol *self);
	size_t		(*get_mtu)(net_protocol *self, const struct sockaddr *address);

	status_t	(*receive_data)(net_buffer *data);
	status_t	(*deliver_data)(net_protocol *protocol, net_buffer *data);

	status_t	(*error)(uint32 code, net_buffer *data);
	status_t	(*error_reply)(net_protocol *self, net_buffer *causedError,
					uint32 code, void *errorData);

	status_t	(*add_ancillary_data)(net_protocol *self,
					ancillary_data_container *container, const cmsghdr *header);
	ssize_t		(*process_ancillary_data)(net_protocol *self,
					const ancillary_data_header *header, const void *data,
					void *buffer, size_t bufferSize);
	ssize_t		(*process_ancillary_data_no_container)(net_protocol *self,
					net_buffer *buffer, void *buffer, size_t bufferSize);

	ssize_t		(*send_data_no_buffer)(net_protocol *self, const iovec *vecs,
					size_t vecCount, ancillary_data_container *ancillaryData,
					const struct sockaddr *address, socklen_t addressLength);
	ssize_t		(*read_data_no_buffer)(net_protocol *self, const iovec *vecs,
					size_t vecCount, ancillary_data_container **_ancillaryData,
					struct sockaddr *_address, socklen_t *_addressLength);
};

#endif	// NET_PROTOCOL_H

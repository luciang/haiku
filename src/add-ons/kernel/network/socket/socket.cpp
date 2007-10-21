/*
 * Copyright 2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/*!
	The kernel socket API directly forwards all requests into the stack
	via the networking stack driver.
*/

#include <socket_interface.h>

#include <net_stack_driver.h>

#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>


int
socket(int family, int type, int protocol)
{
	int socket = open(NET_STACK_DRIVER_PATH, O_RDWR);
	if (socket < 0)
		return -1;

	socket_args args;
	args.family = family;
	args.type = type;
	args.protocol = protocol;

	if (ioctl(socket, NET_STACK_SOCKET, &args, sizeof(args)) < 0) {
		close(socket);
		return -1;
	}

	return socket;
}


int
bind(int socket, const struct sockaddr *address, socklen_t addressLength)
{
	sockaddr_args args;
	args.address = const_cast<struct sockaddr *>(address);
	args.address_length = addressLength;

	return ioctl(socket, NET_STACK_BIND, &args, sizeof(args));
}


int
shutdown(int socket, int how)
{
	return ioctl(socket, NET_STACK_SHUTDOWN, (void *)how, 0);
}


int
connect(int socket, const struct sockaddr *address, socklen_t addressLength)
{
	sockaddr_args args;
	args.address = const_cast<struct sockaddr *>(address);
	args.address_length = addressLength;

	return ioctl(socket, NET_STACK_CONNECT, &args, sizeof(args));
}


int
listen(int socket, int backlog)
{
	return ioctl(socket, NET_STACK_LISTEN, (void *)backlog, 0);
}


int
accept(int socket, struct sockaddr *address, socklen_t *_addressLength)
{
	int acceptSocket = open(NET_STACK_DRIVER_PATH, O_RDWR);
	if (acceptSocket < 0)
		return -1;

	accept_args args;
	args.accept_socket = acceptSocket;
	args.address = address;
	args.address_length = _addressLength ? *_addressLength : 0;

	if (ioctl(socket, NET_STACK_ACCEPT, &args, sizeof(args)) < 0) {
		close(acceptSocket);
		return -1;
	}

	if (_addressLength != NULL)
		*_addressLength = args.address_length;

	return acceptSocket;
}


ssize_t
recv(int socket, void *data, size_t length, int flags)
{
	message_args args;
	args.data = data;
	args.length = length;
	args.flags = flags;
	args.header = NULL;

	return ioctl(socket, NET_STACK_RECEIVE, &args, sizeof(args));
}


ssize_t
recvfrom(int socket, void *data, size_t length, int flags,
	struct sockaddr *address, socklen_t *_addressLength)
{
	message_args args;
	msghdr header;

	memset(&header, 0, sizeof(header));

	args.data = data;
	args.length = length;
	args.flags = flags;
	args.header = &header;

	header.msg_name = (char *)address;
	header.msg_namelen = _addressLength ? *_addressLength : 0;

	ssize_t bytesReceived = ioctl(socket, NET_STACK_RECEIVE, &args, sizeof(args));
	if (bytesReceived < 0)
		return -1;

	if (_addressLength != NULL)
		*_addressLength = header.msg_namelen;

	return bytesReceived;
}


ssize_t
recvmsg(int socket, struct msghdr *message, int flags)
{
	message_args args;

	if (message == NULL || (message->msg_iovlen > 0 && message->msg_iov == NULL))
		return B_BAD_VALUE;

	args.header = message;
	args.flags = flags;

	if (message->msg_iovlen > 0) {
		args.data = message->msg_iov[0].iov_base;
		args.length = message->msg_iov[0].iov_len;
	} else {
		args.data = NULL;
		args.length = 0;
	}

	return ioctl(socket, NET_STACK_RECEIVE, &args, sizeof(args));
}


ssize_t
send(int socket, const void *data, size_t length, int flags)
{
	message_args args;
	args.data = const_cast<void *>(data);
	args.length = length;
	args.flags = flags;
	args.header = NULL;

	return ioctl(socket, NET_STACK_SEND, &args, sizeof(args));
}


ssize_t
sendto(int socket, const void *data, size_t length, int flags,
	const struct sockaddr *address, socklen_t addressLength)
{
	message_args args;
	msghdr header;
	memset(&header, 0, sizeof(header));

	args.data = const_cast<void *>(data);
	args.length = length;
	args.flags = flags;
	args.header = &header;
	header.msg_name = (char *)const_cast<sockaddr *>(address);
	header.msg_namelen = addressLength;

	return ioctl(socket, NET_STACK_SEND, &args, sizeof(args));
}


ssize_t
sendmsg(int socket, const struct msghdr *message, int flags)
{
	message_args args;

	if (message == NULL || (message->msg_iovlen > 0 && message->msg_iov == NULL))
		return B_BAD_VALUE;

	args.header = (msghdr *)message;
	args.flags = flags;

	if (message->msg_iovlen > 0) {
		args.data = message->msg_iov[0].iov_base;
		args.length = message->msg_iov[0].iov_len;
	} else {
		args.data = NULL;
		args.length = 0;
	}

	return ioctl(socket, NET_STACK_SEND, &args, sizeof(args));
}


int
getsockopt(int socket, int level, int option, void *value, socklen_t *_length)
{
	sockopt_args args;
	args.level = level;
	args.option = option;
	args.value = value;
	args.length = _length ? *_length : 0;

	if (ioctl(socket, NET_STACK_GETSOCKOPT, &args, sizeof(args)) < 0)
		return -1;

	if (_length)
		*_length = args.length;

	return 0;
}


int
setsockopt(int socket, int level, int option, const void *value, socklen_t length)
{
	sockopt_args args;
	args.level = level;
	args.option = option;
	args.value = const_cast<void *>(value);
	args.length = length;

	return ioctl(socket, NET_STACK_SETSOCKOPT, &args, sizeof(args));
}


int
getpeername(int socket, struct sockaddr *address, socklen_t *_addressLength)
{
	sockaddr_args args;
	args.address = address;
	args.address_length = _addressLength ? *_addressLength : 0;

	if (ioctl(socket, NET_STACK_GETPEERNAME, &args, sizeof(args)) < 0)
		return -1;

	if (_addressLength != NULL)
		*_addressLength = args.address_length;

	return 0;
}


int
getsockname(int socket, struct sockaddr *address, socklen_t *_addressLength)
{
	sockaddr_args args;
	args.address = address;
	args.address_length = _addressLength ? *_addressLength : 0;

	if (ioctl(socket, NET_STACK_GETSOCKNAME, &args, sizeof(args)) < 0)
		return -1;

	if (_addressLength != NULL)
		*_addressLength = args.address_length;

	return 0;
}


int
sockatmark(int socket)
{
	// TODO: implement me!
	return -1;
}


int
socketpair(int family, int type, int protocol, int socketVector[2])
{
	socketVector[0] = socket(family, type, protocol);
	if (socketVector[0] < 0)
		return -1;

	socketVector[1] = socket(family, type, protocol);
	if (socketVector[1] < 0)
		goto err1;

	socketpair_args args;
	args.second_socket = socketVector[1];

	if (ioctl(socketVector[0], NET_STACK_SOCKETPAIR, &args, sizeof(args)) < 0)
		goto err2;

	return 0;

err2:
	close(socketVector[1]);
err1:
	close(socketVector[0]);
	return -1;
}


//	#pragma mark -


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			return B_ERROR;
	}
}


static socket_module_info sSocketModule = {
	{
		B_SOCKET_MODULE_NAME,
		0,
		std_ops
	},

	accept,
	bind,
	connect,
	getpeername,
	getsockname,
	getsockopt,
	listen,
	recv,
	recvfrom,
	recvmsg,
	send,
	sendmsg,
	sendto,
	setsockopt,
	shutdown,
	socket,
	sockatmark,
	socketpair,
};

module_info *modules[] = {
	(module_info *)&sSocketModule,
	NULL
};

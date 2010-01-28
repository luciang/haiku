/*
 * Copyright 2006-2010, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NETINET6_IN6_H_
#define _NETINET6_IN6_H_


#include <sys/types.h>
#include <stdint.h>


struct in6_addr {
	uint8_t		s6_addr[16];
};

/* IP Version 6 socket address. */
struct sockaddr_in6 {
	uint8_t		sin6_len;
	uint8_t		sin6_family;
	uint16_t	sin6_port;
	uint32_t	sin6_flowinfo;
	struct in6_addr	sin6_addr;
	uint32_t	sin6_scope_id;
};


#define IN6ADDR_ANY_INIT {{ \
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }}
#define IN6ADDR_LOOPBACK_INIT {{ \
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }}

extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;


/* Non-standard helper defines (same as in FreeBSD, though) */
#define __IPV6_ADDR_SCOPE_NODELOCAL			0x01
#define __IPV6_ADDR_SCOPE_INTFACELOCAL		0x01
#define __IPV6_ADDR_SCOPE_LINKLOCAL			0x02
#define __IPV6_ADDR_SCOPE_SITELOCAL			0x05
#define __IPV6_ADDR_SCOPE_ORGLOCAL			0x08
#define __IPV6_ADDR_SCOPE_GLOBAL			0x0e

#define __IPV6_ADDR_MC_SCOPE(a)				((a)->s6_addr[1] & 0x0f)


#define IN6_IS_ADDR_UNSPECIFIED(a) \
	(!memcmp((a)->s6_addr, in6addr_any.s6_addr, sizeof(struct in6_addr)))

#define IN6_IS_ADDR_LOOPBACK(a) \
	(!memcmp((a)->s6_addr, in6addr_loopback.s6_addr, sizeof(struct in6_addr)))

#define IN6_IS_ADDR_MULTICAST(a) \
	((a)->s6_addr[0] == 0xff)

#define IN6_IS_ADDR_LINKLOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0x80))

#define IN6_IS_ADDR_SITELOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0xc0))

#define IN6_IS_ADDR_V4MAPPED(a) \
	((a)->s6_addr[0] == 0x00 && (a)->s6_addr[1] == 0x00 \
	&& (a)->s6_addr[2] == 0x00 && (a)->s6_addr[3] == 0x00 \
	&& (a)->s6_addr[4] == 0x00 && (a)->s6_addr[5] == 0x00 \
	&& (a)->s6_addr[6] == 0x00 && (a)->s6_addr[9] == 0x00 \
	&& (a)->s6_addr[8] == 0x00 && (a)->s6_addr[9] == 0x00 \
	&& (a)->s6_addr[10] == 0xff && (a)->s6_addr[11] == 0xff)

#define IN6_IS_ADDR_V4COMPAT(a) \
	((a)->s6_addr[0] == 0x00 && (a)->s6_addr[1] == 0x00 \
	&& (a)->s6_addr[2] == 0x00 && (a)->s6_addr[3] == 0x00 \
	&& (a)->s6_addr[4] == 0x00 && (a)->s6_addr[5] == 0x00 \
	&& (a)->s6_addr[6] == 0x00 && (a)->s6_addr[9] == 0x00 \
	&& (a)->s6_addr[8] == 0x00 && (a)->s6_addr[9] == 0x00 \
	&& (a)->s6_addr[10] == 0x00 && (a)->s6_addr[11] == 0x01)

#define IN6_IS_ADDR_MC_NODELOCAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && __IPV6_ADDR_MC_SCOPE(a) \
		== __IPV6_ADDR_SCOPE_NODELOCAL)

#define IN6_IS_ADDR_MC_LINKLOCAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && __IPV6_ADDR_MC_SCOPE(a) \
		== __IPV6_ADDR_SCOPE_LINKLOCAL)

#define IN6_IS_ADDR_MC_SITELOCAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && __IPV6_ADDR_MC_SCOPE(a) \
		== __IPV6_ADDR_SCOPE_SITELOCAL)

#define IN6_IS_ADDR_MC_ORGLOCAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && __IPV6_ADDR_MC_SCOPE(a) \
		== __IPV6_ADDR_SCOPE_ORGLOCAL)

#define IN6_IS_ADDR_MC_GLOBAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && __IPV6_ADDR_MC_SCOPE(a) \
		== __IPV6_ADDR_SCOPE_GLOBAL)

/* maximal length of the string representation of an IPv6 address */
#define INET6_ADDRSTRLEN				46


#endif	/* _NETINET6_IN6_H_ */

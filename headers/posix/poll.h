#ifndef _POLL_H
#define _POLL_H
/* 
** Distributed under the terms of the OpenBeOS License.
*/

typedef unsigned long nfds_t;

struct pollfd {
	int		fd;
	short	events;		/* events to look for */
	short	revents;	/* events that occured */
};

/* events & revents - compatible with the B_SELECT_xxx definitions in Drivers.h */
#define	POLLIN		0x0001		/* any readable data available */
#define	POLLOUT		0x0002		/* file descriptor is writeable */
#define	POLLRDNORM	POLLIN
#define	POLLWRNORM	POLLOUT
#define	POLLRDBAND	0x0008		/* priority readable data */
#define	POLLWRBAND	0x0010		/* priority data can be written */
#define	POLLPRI		0x0020		/* high priority readable data */

/* revents only */
#define	POLLERR		0x0004		/* errors pending */
#define	POLLHUP		0x0080		/* disconnected */
#define	POLLNVAL	0x1000		/* invalid file descriptor */


extern
#ifdef __cplusplus
"C"
#endif
int poll(struct pollfd *fds, nfds_t numfds, int timeout);

#endif /* _POLL_H */

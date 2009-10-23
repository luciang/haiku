/*
 * Copyright 2009, Colin Günther, coling@gmx.de.
 * Copyright 2007, Hugo Santos. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FBSD_COMPAT_SYS_CALLOUT_H_
#define _FBSD_COMPAT_SYS_CALLOUT_H_


#include <sys/haiku-module.h>

#include <sys/queue.h>


struct callout {
	struct net_timer	c_timer;
	void *				c_arg;
	void				(*c_func)(void *);
	struct mtx *		c_mtx;
	int					c_flags;
};


#define CALLOUT_MPSAFE	0x0001


void callout_init_mtx(struct callout *c, struct mtx *mutex, int flags);
int	callout_reset(struct callout *c, int, void (*func)(void *), void *arg);
int callout_pending(struct callout *c);
int callout_active(struct callout *c);

#define	callout_drain(c)	_callout_stop_safe(c, 1)
#define	callout_stop(c)		_callout_stop_safe(c, 0)
int	_callout_stop_safe(struct callout *, int);

inline void
callout_init(struct callout *c, int mpsafe);

int	callout_schedule(struct callout *, int);

#endif	/* _FBSD_COMPAT_SYS_CALLOUT_H_ */

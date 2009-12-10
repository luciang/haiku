/*
 * Copyright 2009 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FBSD_COMPAT_SYS_SYSCTL_H_
#define _FBSD_COMPAT_SYS_SYSCTL_H_


#include <sys/queue.h>


#ifdef _KERNEL

struct sysctl_req {
	void *newptr;
};

struct sysctl_ctx_list {
};


#define SYSCTL_HANDLER_ARGS void *oidp, void *arg1, int arg2, \
	struct sysctl_req *req

#define OID_AUTO	(-1)

#define CTLTYPE		0xf	/* Mask for the type */
#define	CTLTYPE_NODE	1	/* name is a node */
#define	CTLTYPE_INT	2	/* name describes an integer */
#define	CTLTYPE_STRING	3	/* name describes a string */
#define	CTLTYPE_QUAD	4	/* name describes a 64-bit number */
#define	CTLTYPE_OPAQUE	5	/* name describes a structure */
#define	CTLTYPE_STRUCT	CTLTYPE_OPAQUE	/* name describes a structure */
#define	CTLTYPE_UINT	6	/* name describes an unsigned integer */
#define	CTLTYPE_LONG	7	/* name describes a long */
#define	CTLTYPE_ULONG	8	/* name describes an unsigned long */

#define CTLFLAG_RD	0x80000000	/* Allow reads of variable */
#define CTLFLAG_WR	0x40000000	/* Allow writes to the variable */
#define CTLFLAG_RW	(CTLFLAG_RD|CTLFLAG_WR)
#define CTLFLAG_NOLOCK	0x20000000	/* XXX Don't Lock */
#define CTLFLAG_ANYBODY	0x10000000	/* All users can set this var */
#define CTLFLAG_SECURE	0x08000000	/* Permit set only if securelevel<=0 */
#define CTLFLAG_PRISON	0x04000000	/* Prisoned roots can fiddle */
#define CTLFLAG_DYN	0x02000000	/* Dynamic oid - can be freed */
#define CTLFLAG_SKIP	0x01000000	/* Skip this sysctl when listing */
#define CTLMASK_SECURE	0x00F00000	/* Secure level */
#define CTLFLAG_TUN	0x00080000	/* Tunable variable */
#define CTLFLAG_RDTUN	(CTLFLAG_RD|CTLFLAG_TUN)


static inline int
sysctl_ctx_init(struct sysctl_ctx_list *clist)
{
	return -1;
}


static inline int
sysctl_ctx_free(struct sysctl_ctx_list *clist)
{
	return -1;
}


static inline void *
sysctl_add_oid(struct sysctl_ctx_list *clist, void *parent, int nbr, 
	const char *name, int kind, void *arg1, int arg2,
	int (*handler) (SYSCTL_HANDLER_ARGS), const char *fmt, const char *descr)
{
	return NULL;
}


static inline int sysctl_handle_long(SYSCTL_HANDLER_ARGS) { return -1; }
static inline int sysctl_handle_opaque(SYSCTL_HANDLER_ARGS) { return -1; }
static inline int sysctl_handle_quad(SYSCTL_HANDLER_ARGS) { return -1; }
static inline int sysctl_handle_int(SYSCTL_HANDLER_ARGS) { return -1; }
static inline int sysctl_handle_string(SYSCTL_HANDLER_ARGS) { return -1; }


#define SYSCTL_OUT(r, p, l) -1

#define __DESCR(x) ""

#define SYSCTL_ADD_OID(ctx, parent, nbr, name, kind, a1, a2, handler, fmt, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, kind, a1, a2, handler, fmt, \
	__DESCR(descr))

#define SYSCTL_ADD_NODE(ctx, parent, nbr, name, access, handler, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_NODE|(access),		\
	0, 0, handler, "N", __DESCR(descr))

#define SYSCTL_ADD_STRING(ctx, parent, nbr, name, access, arg, len, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_STRING|(access),			\
	arg, len, sysctl_handle_string, "A", __DESCR(descr))

#define SYSCTL_ADD_INT(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_INT|(access),		\
	ptr, val, sysctl_handle_int, "I", __DESCR(descr))

#define SYSCTL_ADD_UINT(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_UINT|(access),			\
	ptr, val, sysctl_handle_int, "IU", __DESCR(descr))

#define SYSCTL_ADD_XINT(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_UINT|(access),			\
	ptr, val, sysctl_handle_int, "IX", __DESCR(descr))

#define SYSCTL_ADD_LONG(ctx, parent, nbr, name, access, ptr, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_LONG|(access),	\
	ptr, 0, sysctl_handle_long, "L", __DESCR(descr))

#define SYSCTL_ADD_ULONG(ctx, parent, nbr, name, access, ptr, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_ULONG|(access),		\
	ptr, 0, sysctl_handle_long, "LU", __DESCR(descr))

#define SYSCTL_ADD_QUAD(ctx, parent, nbr, name, access, ptr, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_QUAD|(access),	\
	ptr, 0, sysctl_handle_quad, "Q", __DESCR(descr))

#define SYSCTL_ADD_OPAQUE(ctx, parent, nbr, name, access, ptr, len, fmt, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_OPAQUE|(access), \
	ptr, len, sysctl_handle_opaque, fmt, __DESCR(descr))

#define SYSCTL_ADD_STRUCT(ctx, parent, nbr, name, access, ptr, type, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_OPAQUE|(access),			\
	ptr, sizeof(struct type), sysctl_handle_opaque, "S," #type, __DESCR(descr))

#define SYSCTL_ADD_PROC(ctx, parent, nbr, name, access, ptr, arg, handler, fmt, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, (access), ptr, arg, handler, fmt, \
	__DESCR(descr))


static inline void *
SYSCTL_CHILDREN(void *ptr)
{
	return NULL;
}


#define SYSCTL_STATIC_CHILDREN(...) NULL

#define SYSCTL_DECL(name) \
	extern struct sysctl_oid_list sysctl_##name##_children

#define SYSCTL_NODE(...)
#define SYSCTL_INT(...)
#define SYSCTL_PROC(...)

#endif

#endif

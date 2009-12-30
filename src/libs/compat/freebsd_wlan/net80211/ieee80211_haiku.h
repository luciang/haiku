/*-
 * Copyright (c) 2003-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _FBSD_COMPAT_NET80211_IEEE80211_HAIKU_H_
#define _FBSD_COMPAT_NET80211_IEEE80211_HAIKU_H_

#ifdef _KERNEL

#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>


#define IEEE80211_CRYPTO_MODULE(name, version) \
	void \
	ieee80211_crypto_##name##_load() { \
		ieee80211_crypto_register(&name); \
	} \
\
\
	void \
	ieee80211_crypto_##name##_unload() \
	{ \
		ieee80211_crypto_unregister(&name); \
	}


/*
 * Common state locking definitions.
 */
typedef struct {
	char		name[16];		/* e.g. "ath0_com_lock" */
	struct mtx	mtx;
} ieee80211_com_lock_t;
#define	IEEE80211_LOCK_INIT(_ic, _name) do {				\
	ieee80211_com_lock_t *cl = &(_ic)->ic_comlock;			\
	snprintf(cl->name, sizeof(cl->name), "%s_com_lock", _name);	\
	mtx_init(&cl->mtx, cl->name, NULL, MTX_DEF | MTX_RECURSE);	\
} while (0)
#define	IEEE80211_LOCK_OBJ(_ic)	(&(_ic)->ic_comlock.mtx)
#define	IEEE80211_LOCK_DESTROY(_ic) mtx_destroy(IEEE80211_LOCK_OBJ(_ic))
#define	IEEE80211_LOCK(_ic)	   mtx_lock(IEEE80211_LOCK_OBJ(_ic))
#define	IEEE80211_UNLOCK(_ic)	   mtx_unlock(IEEE80211_LOCK_OBJ(_ic))
#define	IEEE80211_LOCK_ASSERT(_ic) \
	mtx_assert(IEEE80211_LOCK_OBJ(_ic), MA_OWNED)

/*
 * Node locking definitions.
 */
typedef struct {
	char		name[16];		/* e.g. "ath0_node_lock" */
	struct mtx	mtx;
} ieee80211_node_lock_t;
#define	IEEE80211_NODE_LOCK_INIT(_nt, _name) do {			\
	ieee80211_node_lock_t *nl = &(_nt)->nt_nodelock;		\
	snprintf(nl->name, sizeof(nl->name), "%s_node_lock", _name);	\
	mtx_init(&nl->mtx, nl->name, NULL, MTX_DEF | MTX_RECURSE);	\
} while (0)
#define	IEEE80211_NODE_LOCK_OBJ(_nt)	(&(_nt)->nt_nodelock.mtx)
#define	IEEE80211_NODE_LOCK_DESTROY(_nt) \
	mtx_destroy(IEEE80211_NODE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_LOCK(_nt) \
	mtx_lock(IEEE80211_NODE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_IS_LOCKED(_nt) \
	mtx_owned(IEEE80211_NODE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_UNLOCK(_nt) \
	mtx_unlock(IEEE80211_NODE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_LOCK_ASSERT(_nt)	\
	mtx_assert(IEEE80211_NODE_LOCK_OBJ(_nt), MA_OWNED)

/*
 * Node table iteration locking definitions; this protects the
 * scan generation # used to iterate over the station table
 * while grabbing+releasing the node lock.
 */
typedef struct {
	char		name[16];		/* e.g. "ath0_scan_lock" */
	struct mtx	mtx;
} ieee80211_scan_lock_t;
#define	IEEE80211_NODE_ITERATE_LOCK_INIT(_nt, _name) do {		\
	ieee80211_scan_lock_t *sl = &(_nt)->nt_scanlock;		\
	snprintf(sl->name, sizeof(sl->name), "%s_scan_lock", _name);	\
	mtx_init(&sl->mtx, sl->name, NULL, MTX_DEF);			\
} while (0)
#define	IEEE80211_NODE_ITERATE_LOCK_OBJ(_nt)	(&(_nt)->nt_scanlock.mtx)
#define	IEEE80211_NODE_ITERATE_LOCK_DESTROY(_nt) \
	mtx_destroy(IEEE80211_NODE_ITERATE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_ITERATE_LOCK(_nt) \
	mtx_lock(IEEE80211_NODE_ITERATE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_ITERATE_UNLOCK(_nt) \
	mtx_unlock(IEEE80211_NODE_ITERATE_LOCK_OBJ(_nt))

/*
 * Power-save queue definitions.
 */
typedef struct mtx ieee80211_psq_lock_t;
#define	IEEE80211_PSQ_INIT(_psq, _name) \
	mtx_init(&(_psq)->psq_lock, _name, "802.11 ps q", MTX_DEF)
#define	IEEE80211_PSQ_DESTROY(_psq)	mtx_destroy(&(_psq)->psq_lock)
#define	IEEE80211_PSQ_LOCK(_psq)	mtx_lock(&(_psq)->psq_lock)
#define	IEEE80211_PSQ_UNLOCK(_psq)	mtx_unlock(&(_psq)->psq_lock)

#ifndef IF_PREPEND_LIST
#define _IF_PREPEND_LIST(ifq, mhead, mtail, mcount) do {	\
	(mtail)->m_nextpkt = (ifq)->ifq_head;			\
	if ((ifq)->ifq_tail == NULL)				\
		(ifq)->ifq_tail = (mtail);			\
	(ifq)->ifq_head = (mhead);				\
	(ifq)->ifq_len += (mcount);				\
} while (0)
#define IF_PREPEND_LIST(ifq, mhead, mtail, mcount) do {		\
	IF_LOCK(ifq);						\
	_IF_PREPEND_LIST(ifq, mhead, mtail, mcount);		\
	IF_UNLOCK(ifq);						\
} while (0)
#endif /* IF_PREPEND_LIST */

/*
 * Age queue definitions.
 */
typedef struct mtx ieee80211_ageq_lock_t;
#define	IEEE80211_AGEQ_INIT(_aq, _name) \
	mtx_init(&(_aq)->aq_lock, _name, "802.11 age q", MTX_DEF)
#define	IEEE80211_AGEQ_DESTROY(_aq)	mtx_destroy(&(_aq)->aq_lock)
#define	IEEE80211_AGEQ_LOCK(_aq)	mtx_lock(&(_aq)->aq_lock)
#define	IEEE80211_AGEQ_UNLOCK(_aq)	mtx_unlock(&(_aq)->aq_lock)

/*
 * Node reference counting definitions.
 *
 * ieee80211_node_initref	initialize the reference count to 1
 * ieee80211_node_incref	add a reference
 * ieee80211_node_decref	remove a reference
 * ieee80211_node_dectestref	remove a reference and return 1 if this
 *				is the last reference, otherwise 0
 * ieee80211_node_refcnt	reference count for printing (only)
 */
#include <machine/atomic.h>

#define ieee80211_node_initref(_ni) \
	do { ((_ni)->ni_refcnt = 1); } while (0)
#define ieee80211_node_incref(_ni) \
	atomic_add_int(&(_ni)->ni_refcnt, 1)
#define	ieee80211_node_decref(_ni) \
	atomic_subtract_int(&(_ni)->ni_refcnt, 1)
struct ieee80211_node;
int	ieee80211_node_dectestref(struct ieee80211_node *ni);
#define	ieee80211_node_refcnt(_ni)	(_ni)->ni_refcnt

struct ifqueue;
struct ieee80211vap;
void	ieee80211_drain_ifq(struct ifqueue *);
void	ieee80211_flush_ifq(struct ifqueue *, struct ieee80211vap *);

void	ieee80211_vap_destroy(struct ieee80211vap *);

#define	IFNET_IS_UP_RUNNING(_ifp) \
	(((_ifp)->if_flags & IFF_UP) && \
	 ((_ifp)->if_drv_flags & IFF_DRV_RUNNING))

#define	msecs_to_ticks(ms)	(((ms)*hz)/1000)
#define	ticks_to_msecs(t)	(1000*(t) / hz)
#define	ticks_to_secs(t)	((t) / hz)
#define time_after(a,b) 	((long long)(b) - (long long)(a) < 0)
#define time_before(a,b)	time_after(b,a)
#define time_after_eq(a,b)	((long long)(a) - (long long)(b) >= 0)
#define time_before_eq(a,b)	time_after_eq(b,a)

struct mbuf *ieee80211_getmgtframe(uint8_t **frm, int headroom, int pktlen);

/* tx path usage */
#define	M_ENCAP		M_PROTO1		/* 802.11 encap done */
#define	M_EAPOL		M_PROTO3		/* PAE/EAPOL frame */
#define	M_PWR_SAV	M_PROTO4		/* bypass PS handling */
#define	M_MORE_DATA	M_PROTO5		/* more data frames to follow */
#define	M_FF		M_PROTO6		/* fast frame */
#define	M_TXCB		M_PROTO7		/* do tx complete callback */
#define	M_AMPDU_MPDU	M_PROTO8		/* ok for A-MPDU aggregation */
#define	M_80211_TX \
	(M_FRAG|M_FIRSTFRAG|M_LASTFRAG|M_ENCAP|M_EAPOL|M_PWR_SAV|\
	 M_MORE_DATA|M_FF|M_TXCB|M_AMPDU_MPDU)

/* rx path usage */
#define	M_AMPDU		M_PROTO1		/* A-MPDU subframe */
#define	M_WEP		M_PROTO2		/* WEP done by hardware */
#if 0
#define	M_AMPDU_MPDU	M_PROTO8		/* A-MPDU re-order done */
#endif
#define	M_80211_RX	(M_AMPDU|M_WEP|M_AMPDU_MPDU)

/*
 * Store WME access control bits in the vlan tag.
 * This is safe since it's done after the packet is classified
 * (where we use any previous tag) and because it's passed
 * directly in to the driver and there's no chance someone
 * else will clobber them on us.
 */
#define	M_WME_SETAC(m, ac) \
	((m)->m_pkthdr.ether_vtag = (ac))
#define	M_WME_GETAC(m)	((m)->m_pkthdr.ether_vtag)

/*
 * Mbufs on the power save queue are tagged with an age and
 * timed out.  We reuse the hardware checksum field in the
 * mbuf packet header to store this data.
 */
#define	M_AGE_SET(m,v)		(m->m_pkthdr.csum_data = v)
#define	M_AGE_GET(m)		(m->m_pkthdr.csum_data)
#define	M_AGE_SUB(m,adj)	(m->m_pkthdr.csum_data -= adj)

/*
 * Store the sequence number.
 */
#define	M_SEQNO_SET(m, seqno) \
	((m)->m_pkthdr.tso_segsz = (seqno))
#define	M_SEQNO_GET(m)	((m)->m_pkthdr.tso_segsz)

#define	MTAG_ABI_NET80211	1132948340	/* net80211 ABI */

struct ieee80211_cb {
	void	(*func)(struct ieee80211_node *, void *, int status);
	void	*arg;
};
#define	NET80211_TAG_CALLBACK	0	/* xmit complete callback */

int	ieee80211_add_callback(struct mbuf *m,
		void (*func)(struct ieee80211_node *, void *, int), void *arg);
void	ieee80211_process_callback(struct ieee80211_node *, struct mbuf *, int);

void	get_random_bytes(void *, size_t);

struct ieee80211com;

void	ieee80211_sysctl_attach(struct ieee80211com *);
void	ieee80211_sysctl_detach(struct ieee80211com *);
void	ieee80211_sysctl_vattach(struct ieee80211vap *);
void	ieee80211_sysctl_vdetach(struct ieee80211vap *);

void	ieee80211_load_module(const char *);

/*
 * A "policy module" is an adjunct module to net80211 that provides
 * functionality that typically includes policy decisions.  This
 * modularity enables extensibility and vendor-supplied functionality.
 */
#define	_IEEE80211_POLICY_MODULE(policy, name, version)			\
typedef void (*policy##_setup)(int);					\
SET_DECLARE(policy##_set, policy##_setup);

/*
 * Scanner modules provide scanning policy.
 */
#define	IEEE80211_SCANNER_MODULE(name, version)
#define	IEEE80211_SCANNER_ALG(name, alg, v)


void	ieee80211_scan_sta_init(void);
void	ieee80211_scan_sta_uninit(void);


struct ieee80211req;
typedef int ieee80211_ioctl_getfunc(struct ieee80211vap *,
    struct ieee80211req *);
SET_DECLARE(ieee80211_ioctl_getset, ieee80211_ioctl_getfunc);
#define	IEEE80211_IOCTL_GET(_name, _get) TEXT_SET(ieee80211_ioctl_getset, _get)

typedef int ieee80211_ioctl_setfunc(struct ieee80211vap *,
    struct ieee80211req *);
SET_DECLARE(ieee80211_ioctl_setset, ieee80211_ioctl_setfunc);
#define	IEEE80211_IOCTL_SET(_name, _set) TEXT_SET(ieee80211_ioctl_setset, _set)
#endif /* _KERNEL */

/*
 * Structure prepended to raw packets sent through the bpf
 * interface when set to DLT_IEEE802_11_RADIO.  This allows
 * user applications to specify pretty much everything in
 * an Atheros tx descriptor.  XXX need to generalize.
 *
 * XXX cannot be more than 14 bytes as it is copied to a sockaddr's
 * XXX sa_data area.
 */
struct ieee80211_bpf_params {
	uint8_t		ibp_vers;	/* version */
#define	IEEE80211_BPF_VERSION	0
	uint8_t		ibp_len;	/* header length in bytes */
	uint8_t		ibp_flags;
#define	IEEE80211_BPF_SHORTPRE	0x01	/* tx with short preamble */
#define	IEEE80211_BPF_NOACK	0x02	/* tx with no ack */
#define	IEEE80211_BPF_CRYPTO	0x04	/* tx with h/w encryption */
#define	IEEE80211_BPF_FCS	0x10	/* frame incldues FCS */
#define	IEEE80211_BPF_DATAPAD	0x20	/* frame includes data padding */
#define	IEEE80211_BPF_RTS	0x40	/* tx with RTS/CTS */
#define	IEEE80211_BPF_CTS	0x80	/* tx with CTS only */
	uint8_t		ibp_pri;	/* WME/WMM AC+tx antenna */
	uint8_t		ibp_try0;	/* series 1 try count */
	uint8_t		ibp_rate0;	/* series 1 IEEE tx rate */
	uint8_t		ibp_power;	/* tx power (device units) */
	uint8_t		ibp_ctsrate;	/* IEEE tx rate for CTS */
	uint8_t		ibp_try1;	/* series 2 try count */
	uint8_t		ibp_rate1;	/* series 2 IEEE tx rate */
	uint8_t		ibp_try2;	/* series 3 try count */
	uint8_t		ibp_rate2;	/* series 3 IEEE tx rate */
	uint8_t		ibp_try3;	/* series 4 try count */
	uint8_t		ibp_rate3;	/* series 4 IEEE tx rate */
};

#endif /* _FBSD_COMPAT_NET80211_IEEE80211_HAIKU_H_ */

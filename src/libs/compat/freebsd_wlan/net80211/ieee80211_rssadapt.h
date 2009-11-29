/*	$FreeBSD$	*/
/* $NetBSD: ieee80211_rssadapt.h,v 1.4 2005/02/26 22:45:09 perry Exp $ */
/*-
 * Copyright (c) 2003, 2004 David Young.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. The name of David Young may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY David Young ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL David
 * Young BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
#ifndef _FBSD_COMPAT_NET80211_IEEE80211_RSSADAPT_H_
#define _FBSD_COMPAT_NET80211_IEEE80211_RSSADAPT_H_

/* Data-rate adaptation loosely based on "Link Adaptation Strategy
 * for IEEE 802.11 WLAN via Received Signal Strength Measurement"
 * by Javier del Prado Pavon and Sunghyun Choi.
 */

/* Buckets for frames 0-128 bytes long, 129-1024, 1025-maximum. */
#define	IEEE80211_RSSADAPT_BKTS		3
#define IEEE80211_RSSADAPT_BKT0		128
#define	IEEE80211_RSSADAPT_BKTPOWER	3	/* 2**_BKTPOWER */

struct ieee80211_rssadapt {
	struct ieee80211vap *vap;
	int	interval;			/* update interval (ticks) */
};

struct ieee80211_rssadapt_node {
	struct ieee80211_rssadapt *ra_rs;	/* backpointer */
	struct ieee80211_rateset ra_rates;	/* negotiated rates */
	int	ra_rix;				/* current rate index */
	int	ra_ticks;			/* time of last update */
	int	ra_last_raise;			/* time of last rate raise */
	int	ra_raise_interval;		/* rate raise time threshold */

	/* Tx failures in this update interval */
	uint32_t		ra_nfail;
	/* Tx successes in this update interval */
	uint32_t		ra_nok;
	/* exponential average packets/second */
	uint32_t		ra_pktrate;
	/* RSSI threshold for each Tx rate */
	uint16_t		ra_rate_thresh[IEEE80211_RSSADAPT_BKTS]
					      [IEEE80211_RATE_SIZE];
};

void	ieee80211_rssadapt_init(struct ieee80211_rssadapt *,
	    struct ieee80211vap *, int);
void	ieee80211_rssadapt_cleanup(struct ieee80211_rssadapt *);
void	ieee80211_rssadapt_setinterval(struct ieee80211_rssadapt *, int);
void	ieee80211_rssadapt_node_init(struct ieee80211_rssadapt *,
	    struct ieee80211_rssadapt_node *, struct ieee80211_node *);
int	ieee80211_rssadapt_choose(struct ieee80211_node *,
	    struct ieee80211_rssadapt_node *, u_int);

/* NB: these are public only for the inline below */
void	ieee80211_rssadapt_raise_rate(struct ieee80211_rssadapt_node *,
	    int pktlen, int rssi);
void	ieee80211_rssadapt_lower_rate(struct ieee80211_rssadapt_node *,
	    int pktlen, int rssi);

#define	IEEE80211_RSSADAPT_SUCCESS	1
#define	IEEE80211_RSSADAPT_FAILURE	0

static __inline void
ieee80211_rssadapt_tx_complete(struct ieee80211_rssadapt_node *ra,
    int success, int pktlen, int rssi)
{
	if (success) {
		ra->ra_nok++;
		if ((ra->ra_rix + 1) < ra->ra_rates.rs_nrates &&
		    (ticks - ra->ra_last_raise) >= ra->ra_raise_interval)
			ieee80211_rssadapt_raise_rate(ra, pktlen, rssi);
	} else {
		ra->ra_nfail++;
		ieee80211_rssadapt_lower_rate(ra, pktlen, rssi);
	}
}
#endif /* _FBSD_COMPAT_NET80211_IEEE80211_RSSADAPT_H_ */

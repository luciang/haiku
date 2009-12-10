/*
 * Copyright 2002-2007, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYS_SOCKIO_H
#define _SYS_SOCKIO_H


#define SIOCADDRT			8900	/* add route */
#define SIOCDELRT			8901	/* delete route */
#define SIOCSIFADDR			8902	/* set interface address */
#define SIOCGIFADDR			8903	/* get interface address */
#define SIOCSIFDSTADDR		8904	/* set point-to-point address */
#define SIOCGIFDSTADDR		8905	/* get point-to-point address */
#define SIOCSIFFLAGS		8906	/* set interface flags */
#define SIOCGIFFLAGS		8907	/* get interface flags */
#define SIOCGIFBRDADDR		8908	/* get broadcast address */
#define SIOCSIFBRDADDR		8909	/* set broadcast address */
#define SIOCGIFCOUNT		8910	/* count interfaces */
#define SIOCGIFCONF			8911	/* get interface list */
#define SIOCGIFINDEX		8912	/* interface name -> index */
#define SIOCGIFNAME			8913	/* interface index -> name */
#define SIOCGIFNETMASK		8914	/* get net address mask */
#define SIOCSIFNETMASK		8915	/* set net address mask */
#define SIOCGIFMETRIC		8916	/* get interface metric */
#define SIOCSIFMETRIC		8917	/* set interface metric */
#define SIOCDIFADDR			8918	/* delete interface address */
#define SIOCAIFADDR			8919	/* configure interface alias */
#define SIOCADDMULTI		8920	/* add multicast address */
#define SIOCDELMULTI		8921	/* delete multicast address */
#define SIOCGIFMTU			8922	/* get interface MTU */
#define SIOCSIFMTU			8923	/* set interface MTU */
#define SIOCSIFMEDIA		8924	/* set net media */
#define SIOCGIFMEDIA		8925	/* get net media */

#define SIOCGRTSIZE			8926	/* get route table size */
#define SIOCGRTTABLE		8927	/* get route table */
#define SIOCGETRT			8928	/* get route information for destination */

#define SIOCGIFSTATS		8929	/* get interface stats */
#define SIOCGIFPARAM		8930	/* get interface parameter */
#define SIOCGIFTYPE			8931	/* get interface type */

#define SIOCSPACKETCAP		8932	/* Start capturing packets on an interface */
#define SIOCCPACKETCAP		8933	/* Stop capturing packets on an interface */

#define SIOCSHIWAT			8934	/* set high watermark */
#define SIOCGHIWAT			8935	/* get high watermark */
#define SIOCSLOWAT			8936	/* set low watermark */
#define SIOCGLOWAT			8937	/* get low watermark */
#define SIOCATMARK			8938	/* at out-of-band mark? */
#define SIOCSPGRP			8939	/* set process group */
#define SIOCGPGRP			8940	/* get process group */

#define SIOCGPRIVATE_0		8941	/* device private 0 */
#define SIOCGPRIVATE_1		8942	/* device private 1 */
#define SIOCSDRVSPEC		8943	/* set driver-specific parameters */
#define SIOCGDRVSPEC		8944	/* get driver-specific parameters */

#define SIOCSIFGENERIC		8945	/* generic IF set op */
#define SIOCGIFGENERIC		8946	/* generic IF get op */

#define SIOCEND				9000	/* SIOCEND >= highest SIOC* */

#endif	/* _SYS_SOCKIO_H */

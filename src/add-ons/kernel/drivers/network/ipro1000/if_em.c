/**************************************************************************

Copyright (c) 2001-2003, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 3. Neither the name of the Intel Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

/*$FreeBSD: /repoman/r/ncvs/src/sys/dev/em/if_em.c,v 1.2.2.19 2004/04/22 22:03:26 ru Exp $*/

#include "debug.h"
#include "if_em.h"
#include "util.h"


/*********************************************************************
 *  Function prototypes            
 *********************************************************************/
int  em_attach(device_t);
int  em_detach(device_t);
static int32 em_intr(void *);
static int32 event_handler(void *);
static int  start_event_thread(struct adapter *);
static void stop_event_thread(struct adapter *);
static void em_start(struct ifnet *);
static int  em_ioctl(struct ifnet *, u_long, caddr_t);
static void em_watchdog(struct ifnet *);
static void em_init(void *);
static void em_stop(void *);
void em_media_status(struct ifnet *, struct ifmediareq *);
//static int  em_media_change(struct ifnet *);
static void em_identify_hardware(struct adapter *);
static int  em_allocate_pci_resources(struct adapter *);
static void em_free_pci_resources(struct adapter *);
static void em_local_timer(void *);
static int  em_hardware_init(struct adapter *);
static void em_setup_interface(device_t, struct adapter *);
static int  em_setup_transmit_structures(struct adapter *);
static void em_initialize_transmit_unit(struct adapter *);
static int  em_setup_receive_structures(struct adapter *);
static void em_initialize_receive_unit(struct adapter *);
static void em_enable_intr(struct adapter *);
static void em_disable_intr(struct adapter *);
static void em_free_transmit_structures(struct adapter *);
static void em_free_receive_structures(struct adapter *);
static void em_update_stats_counters(struct adapter *);
static void em_clean_transmit_interrupts(struct adapter *);
static int  em_allocate_receive_structures(struct adapter *);
static int  em_allocate_transmit_structures(struct adapter *);
static void em_process_receive_interrupts(struct adapter *, int);
static void em_receive_checksum(struct adapter *, 
				struct em_rx_desc *,
				struct mbuf *);
static void em_transmit_checksum_setup(struct adapter *,
				       struct mbuf *,
				       u_int32_t *,
				       u_int32_t *);
static void em_set_promisc(struct adapter *);
static void em_disable_promisc(struct adapter *);
static void em_set_multi(struct adapter *);
static void em_print_hw_stats(struct adapter *);
static void em_print_link_status(struct adapter *);
static int  em_get_buf(int i, struct adapter *,
		       struct mbuf *);
static void em_enable_vlans(struct adapter *);
static int  em_encap(struct adapter *, struct mbuf *);
static void em_smartspeed(struct adapter *);
static int  em_82547_fifo_workaround(struct adapter *, int);
static void em_82547_update_fifo_head(struct adapter *, int);
static int  em_82547_tx_fifo_reset(struct adapter *);
static void em_82547_move_tail(void *arg);
static void em_print_debug_info(struct adapter *);
static int  em_is_valid_ether_addr(u_int8_t *);
//static int  em_sysctl_stats(SYSCTL_HANDLER_ARGS);
//static int  em_sysctl_debug_info(SYSCTL_HANDLER_ARGS);
static u_int32_t em_fill_descriptors (u_int64_t address, 
                              u_int32_t length, 
                              PDESC_ARRAY desc_array);
//static int  em_sysctl_int_delay(SYSCTL_HANDLER_ARGS);
static void em_add_int_delay_sysctl(struct adapter *, const char *,
                                    const char *, struct em_int_delay_info *,
                                    int, int); 

/*********************************************************************
 *  Tunable default values.
 *********************************************************************/

#define E1000_TICKS_TO_USECS(ticks)     ((1024 * (ticks) + 500) / 1000)
#define E1000_USECS_TO_TICKS(usecs)     ((1000 * (usecs) + 512) / 1024)

static int em_tx_int_delay_dflt = E1000_TICKS_TO_USECS(EM_TIDV);
static int em_rx_int_delay_dflt = E1000_TICKS_TO_USECS(EM_RDTR);
static int em_tx_abs_int_delay_dflt = E1000_TICKS_TO_USECS(EM_TADV);
static int em_rx_abs_int_delay_dflt = E1000_TICKS_TO_USECS(EM_RADV);

TUNABLE_INT("hw.em.tx_int_delay", &em_tx_int_delay_dflt);
TUNABLE_INT("hw.em.rx_int_delay", &em_rx_int_delay_dflt);
TUNABLE_INT("hw.em.tx_abs_int_delay", &em_tx_abs_int_delay_dflt);
TUNABLE_INT("hw.em.rx_abs_int_delay", &em_rx_abs_int_delay_dflt);

/* sysctl vars */
/*
SYSCTL_INT(_hw, OID_AUTO, em_tx_int_delay, CTLFLAG_RD, &em_tx_int_delay_dflt, 0,
           "Transmit interrupt delay");
SYSCTL_INT(_hw, OID_AUTO, em_rx_int_delay, CTLFLAG_RD, &em_rx_int_delay_dflt, 0,
           "Receive interrupt delay");
SYSCTL_INT(_hw, OID_AUTO, em_tx_abs_int_delay, CTLFLAG_RD, &em_tx_abs_int_delay_dflt, 
	   0, "Transmit absolute interrupt delay");
SYSCTL_INT(_hw, OID_AUTO, em_rx_ans_int_delay, CTLFLAG_RD, &em_rx_abs_int_delay_dflt, 
	   0,
           "Receive absolute interrupt delay");
*/

/*********************************************************************
 *  Device initialization routine
 *
 *  The attach entry point is called when the driver is being loaded.
 *  This routine identifies the type of hardware, allocates all resources 
 *  and initializes the hardware.     
 *  
 *  return 0 on success, positive on failure
 *********************************************************************/

int
em_attach(device_t dev)
{
	struct adapter * adapter;
	int             s;
	int             tsize, rsize;
	int             error = 0;

	INIT_DEBUGOUT("em_attach: begin");
	s = splimp();

	/* Allocate, clear, and link in our adapter structure */
	if (!(adapter = device_get_softc(dev))) {
		ERROROUT("adapter structure allocation failed");
		splx(s);
		return(ENOMEM);
	}
	bzero(adapter, sizeof(struct adapter ));
	adapter->dev = dev;
	adapter->osdep.dev = dev;
	adapter->unit = device_get_unit(dev);

	/* SYSCTL stuff */
        sysctl_ctx_init(&adapter->sysctl_ctx);
        adapter->sysctl_tree = SYSCTL_ADD_NODE(&adapter->sysctl_ctx,
					       SYSCTL_STATIC_CHILDREN(_hw),
					       OID_AUTO, 
					       device_get_nameunit(dev),
					       CTLFLAG_RD,
					       0, "");
        if (adapter->sysctl_tree == NULL) {
		error = EIO;
		goto err_sysctl;
        }
 
        SYSCTL_ADD_PROC(&adapter->sysctl_ctx,  
			SYSCTL_CHILDREN(adapter->sysctl_tree),
			OID_AUTO, "debug_info", CTLTYPE_INT|CTLFLAG_RW, 
			(void *)adapter, 0,
                        em_sysctl_debug_info, "I", "Debug Information");

	SYSCTL_ADD_PROC(&adapter->sysctl_ctx,  
			SYSCTL_CHILDREN(adapter->sysctl_tree),
			OID_AUTO, "stats", CTLTYPE_INT|CTLFLAG_RW, 
			(void *)adapter, 0,
                        em_sysctl_stats, "I", "Statistics");

	callout_handle_init(&adapter->timer_handle);
	callout_handle_init(&adapter->tx_fifo_timer_handle);

	/* create event processing thread */
	if (start_event_thread(adapter) < 0) {
		error = EIO;
		goto err_event;
	}

	/* Determine hardware revision */
	em_identify_hardware(adapter);
      
	/* Set up some sysctls for the tunable interrupt delays */
        em_add_int_delay_sysctl(adapter, "rx_int_delay",
            "receive interrupt delay in usecs", &adapter->rx_int_delay,
            E1000_REG_OFFSET(&adapter->hw, RDTR), em_rx_int_delay_dflt);
        em_add_int_delay_sysctl(adapter, "tx_int_delay",
            "transmit interrupt delay in usecs", &adapter->tx_int_delay,
            E1000_REG_OFFSET(&adapter->hw, TIDV), em_tx_int_delay_dflt);
        if (adapter->hw.mac_type >= em_82540) {
                em_add_int_delay_sysctl(adapter, "rx_abs_int_delay",
                    "receive interrupt delay limit in usecs",
                    &adapter->rx_abs_int_delay,
                    E1000_REG_OFFSET(&adapter->hw, RADV),
                    em_rx_abs_int_delay_dflt);
                em_add_int_delay_sysctl(adapter, "tx_abs_int_delay",
                    "transmit interrupt delay limit in usecs",
                    &adapter->tx_abs_int_delay,
                    E1000_REG_OFFSET(&adapter->hw, TADV),
                    em_tx_abs_int_delay_dflt);
        }

	/* Parameters (to be read from user) */   
        adapter->num_tx_desc = EM_MAX_TXD;
        adapter->num_rx_desc = EM_MAX_RXD;
        adapter->hw.autoneg = DO_AUTO_NEG;
        adapter->hw.wait_autoneg_complete = WAIT_FOR_AUTO_NEG_DEFAULT;
        adapter->hw.autoneg_advertised = AUTONEG_ADV_DEFAULT;
        adapter->hw.tbi_compatibility_en = TRUE;
        adapter->rx_buffer_len = EM_RXBUFFER_2048;
	
                        
	/* 
	 * These parameters control the automatic generation(Tx) and 
	 * response(Rx) to Ethernet PAUSE frames.
	 */
        adapter->hw.fc_high_water = FC_DEFAULT_HI_THRESH;
        adapter->hw.fc_low_water  = FC_DEFAULT_LO_THRESH;
        adapter->hw.fc_pause_time = FC_DEFAULT_TX_TIMER;
        adapter->hw.fc_send_xon   = TRUE;
        adapter->hw.fc = em_fc_full;
	
	adapter->hw.phy_init_script = 1;
	adapter->hw.phy_reset_disable = FALSE;

#ifndef EM_MASTER_SLAVE
	adapter->hw.master_slave = em_ms_hw_default;
#else
	adapter->hw.master_slave = EM_MASTER_SLAVE;
#endif

	/* 
	 * Set the max frame size assuming standard ethernet 
	 * sized frames 
	 */   
	adapter->hw.max_frame_size = 
		ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN;

	adapter->hw.min_frame_size = 
		MINIMUM_ETHERNET_PACKET_SIZE + ETHER_CRC_LEN;

	/* 
	 * This controls when hardware reports transmit completion 
	 * status. 
	 */
	adapter->hw.report_tx_early = 1;


	if (em_allocate_pci_resources(adapter)) {
		dprintf("ipro1000/%d: Allocation of PCI resources failed\n", 
		       adapter->unit);
		error = ENXIO;
		goto err_pci;
	}
  	
        em_init_eeprom_params(&adapter->hw);

	tsize = EM_ROUNDUP(adapter->num_tx_desc *
			   sizeof(struct em_tx_desc), 4096);

	/* Allocate Transmit Descriptor ring */
	if (!(adapter->tx_desc_base = (struct em_tx_desc *)
	      contigmalloc(tsize, M_DEVBUF, M_NOWAIT, 0, ~0, 
			   PAGE_SIZE, 0))) {
		dprintf("ipro1000/%d: Unable to allocate TxDescriptor memory\n", 
		       adapter->unit);
		error = ENOMEM;
		goto err_tx_desc;
	}

	rsize = EM_ROUNDUP(adapter->num_rx_desc *
			   sizeof(struct em_rx_desc), 4096);

	/* Allocate Receive Descriptor ring */
	if (!(adapter->rx_desc_base = (struct em_rx_desc *)
	      contigmalloc(rsize, M_DEVBUF, M_NOWAIT, 0, ~0, 
			   PAGE_SIZE, 0))) {
		dprintf("ipro1000/%d: Unable to allocate rx_desc memory\n", 
		       adapter->unit);
		error = ENOMEM;
		goto err_rx_desc;
	}

	/* Initialize the hardware */
	if (em_hardware_init(adapter)) {
		dprintf("ipro1000/%d: Unable to initialize the hardware\n",
		       adapter->unit);
		error = EIO;
		goto err_hw_init;
	}

	/* Copy the permanent MAC address out of the EEPROM */
	if (em_read_mac_addr(&adapter->hw) < 0) {
		dprintf("ipro1000/%d: EEPROM read error while reading mac address\n",
		       adapter->unit);
		error = EIO;
		goto err_mac_addr;
	}

	if (!em_is_valid_ether_addr(adapter->hw.mac_addr)) {
		dprintf("ipro1000/%d: Invalid mac address\n", adapter->unit);
		error = EIO;
		goto err_mac_addr;
	}


	bcopy(adapter->hw.mac_addr, adapter->interface_data.ac_enaddr,
	      ETHER_ADDR_LEN);

	/* Setup OS specific network interface */
	em_setup_interface(dev, adapter);

	/* Initialize statistics */
	em_clear_hw_cntrs(&adapter->hw);
	em_update_stats_counters(adapter);
	adapter->hw.get_link_status = 1;
	em_check_for_link(&adapter->hw);

	/* Print the link status */
	if (adapter->link_active == 1) {
		em_get_speed_and_duplex(&adapter->hw, &adapter->link_speed, 
					&adapter->link_duplex);
		dprintf("ipro1000/%d:  Speed:%d Mbps  Duplex:%s\n",
		       adapter->unit,
		       adapter->link_speed,
		       adapter->link_duplex == FULL_DUPLEX ? "Full" : "Half");
	} else
		dprintf("ipro1000/%d:  Speed:N/A  Duplex:N/A\n", adapter->unit);

	/* Identify 82544 on PCIX */
 	em_get_bus_info(&adapter->hw);	
	if(adapter->hw.bus_type == em_bus_type_pcix &&
           adapter->hw.mac_type == em_82544) {
                adapter->pcix_82544 = TRUE;
	}
        else {
                adapter->pcix_82544 = FALSE;
 	}	
	INIT_DEBUGOUT("em_attach: end");
	splx(s);
	return(error);


err_mac_addr:
err_hw_init:
	contigfree(adapter->rx_desc_base, rsize, M_DEVBUF);
err_rx_desc:
	contigfree(adapter->tx_desc_base, tsize, M_DEVBUF);
err_tx_desc:
err_pci:
	em_free_pci_resources(adapter);
err_event:
	sysctl_ctx_free(&adapter->sysctl_ctx);
err_sysctl:
        splx(s);
	return(error);
}

/*********************************************************************
 *  Device removal routine
 *
 *  The detach entry point is called when the driver is being removed.
 *  This routine stops the adapter and deallocates all the resources
 *  that were allocated for driver operation.
 *  
 *  return 0 on success, positive on failure
 *********************************************************************/

int
em_detach(device_t dev)
{
	struct adapter * adapter = device_get_softc(dev);
	struct ifnet   *ifp = &adapter->interface_data.ac_if;
	int             s;
	int             size;

	INIT_DEBUGOUT("em_detach: begin");
	s = splimp();

	adapter->in_detach = 1;

	em_stop(adapter);
	em_phy_hw_reset(&adapter->hw);

	stop_event_thread(adapter);

#if __FreeBSD_version < 500000
        ether_ifdetach(&adapter->interface_data.ac_if, ETHER_BPF_SUPPORTED);
#else
        ether_ifdetach(&adapter->interface_data.ac_if);
#endif
	em_free_pci_resources(adapter);
	bus_generic_detach(dev);

	size = EM_ROUNDUP(adapter->num_tx_desc *
			  sizeof(struct em_tx_desc), 4096);

	/* Free Transmit Descriptor ring */
	if (adapter->tx_desc_base) {
		contigfree(adapter->tx_desc_base, size, M_DEVBUF);
		adapter->tx_desc_base = NULL;
	}

	size = EM_ROUNDUP(adapter->num_rx_desc *
			  sizeof(struct em_rx_desc), 4096);

	/* Free Receive Descriptor ring */
	if (adapter->rx_desc_base) {
		contigfree(adapter->rx_desc_base, size, M_DEVBUF);
		adapter->rx_desc_base = NULL;
	}

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;

	adapter->sysctl_tree = NULL;
	sysctl_ctx_free(&adapter->sysctl_ctx);

	splx(s);
	return(0);
}

static int
start_event_thread(struct adapter *adapter)
{
	INIT_DEBUGOUT("start_event_thread enter");

	adapter->event_thread = spawn_kernel_thread(event_handler, "ipro1000 event", 80, adapter);
	adapter->event_flags = 0;
	adapter->event_sem = create_sem(0, "ipro1000 event");
	set_sem_owner(adapter->event_sem, B_SYSTEM_TEAM);
	
	if (adapter->event_thread >= 0 && adapter->event_sem >= 0) {
		resume_thread(adapter->event_thread);
		INIT_DEBUGOUT("start_event_thread leave");
		return 0;
	}

	ERROROUT("start_event_thread failed");

	delete_sem(adapter->event_sem);
	kill_thread(adapter->event_thread);

	INIT_DEBUGOUT("start_event_thread leave");
	return -1;
}

static void
stop_event_thread(struct adapter *adapter)
{
	status_t thread_return_value;
	
	INIT_DEBUGOUT("stop_event_thread enter");
	
	delete_sem(adapter->event_sem);
	wait_for_thread(adapter->event_thread, &thread_return_value);	
	adapter->event_thread = -1;
	adapter->event_sem = -1;

	INIT_DEBUGOUT("stop_event_thread leave");
}


/*********************************************************************
 *  Transmit entry point
 *
 *  em_start is called by the stack to initiate a transmit.
 *  The driver will remain in this routine as long as there are
 *  packets to transmit and transmit resources are available.
 *  In case resources are not available stack is notified and
 *  the packet is requeued.
 **********************************************************************/

static void
em_start(struct ifnet *ifp)
{
        int             s;
        struct mbuf    *m_head;
        struct adapter *adapter = ifp->if_softc;

        if (!adapter->link_active)
                return;

        s = splimp();
        while (ifp->if_snd.ifq_head != NULL) {

                IF_DEQUEUE(&ifp->if_snd, m_head);
                
                if (m_head == NULL) break;
                        
		if (em_encap(adapter, m_head)) { 
			ifp->if_flags |= IFF_OACTIVE;
			IF_PREPEND(&ifp->if_snd, m_head);
			break;
                }

		/* Send a copy of the frame to the BPF listener */
#if __FreeBSD_version < 500000
                if (ifp->if_bpf)
                        bpf_mtap(ifp, m_head);
#else
		BPF_MTAP(ifp, m_head);
#endif
        
                /* Set timeout in case hardware has problems transmitting */
                ifp->if_timer = EM_TX_TIMEOUT;
        
        }
        splx(s);
        return;
}

/*********************************************************************
 *  Ioctl entry point
 *
 *  em_ioctl is called when the user wants to configure the
 *  interface.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/

static int
em_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	int             s, mask, error = 0;
	struct ifreq   *ifr = (struct ifreq *) data;
	struct adapter * adapter = ifp->if_softc;

	s = splimp();

	if (adapter->in_detach) goto out;

	switch (command) {
	case SIOCSIFADDR:
	case SIOCGIFADDR:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCxIFADDR (Get/Set Interface Addr)");
		ether_ioctl(ifp, command, data);
		break;
	case SIOCSIFMTU:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCSIFMTU (Set Interface MTU)");
		if (ifr->ifr_mtu > MAX_JUMBO_FRAME_SIZE - ETHER_HDR_LEN) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
			adapter->hw.max_frame_size = 
			ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;
			em_init(adapter);
		}
		break;
	case SIOCSIFFLAGS:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCSIFFLAGS (Set Interface Flags)");
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING)) {
				em_init(adapter);
			}
			em_disable_promisc(adapter);
			em_set_promisc(adapter);
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				em_stop(adapter);
			}
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOC(ADD|DEL)MULTI");
		if (ifp->if_flags & IFF_RUNNING) {
			em_disable_intr(adapter);
			em_set_multi(adapter);
			if (adapter->hw.mac_type == em_82542_rev2_0) {
				em_initialize_receive_unit(adapter);
			}
			em_enable_intr(adapter);
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCxIFMEDIA (Get/Set Interface Media)");
		error = ifmedia_ioctl(ifp, ifr, &adapter->media, command);
		break;
	case SIOCSIFCAP:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCSIFCAP (Set Capabilities)");
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_HWCSUM) {
			ifp->if_capenable ^= IFCAP_HWCSUM;
			if (ifp->if_flags & IFF_RUNNING)
				em_init(adapter);
		}
		break;
	default:
		IOCTL_DEBUGOUT1("ioctl received: UNKNOWN (0x%x)", (int)command);
		error = EINVAL;
	}

out:
	splx(s);
	return(error);
}

/*********************************************************************
 *  Watchdog entry point
 *
 *  This routine is called whenever hardware quits transmitting.
 *
 **********************************************************************/

static void
em_watchdog(struct ifnet *ifp)
{
	struct adapter * adapter;
	adapter = ifp->if_softc;

	/* If we are in this routine because of pause frames, then
	 * don't reset the hardware.
	 */
	if (E1000_READ_REG(&adapter->hw, STATUS) & E1000_STATUS_TXOFF) {
		ifp->if_timer = EM_TX_TIMEOUT;
		return;
	}

	if (em_check_for_link(&adapter->hw))
		dprintf("ipro1000/%d: watchdog timeout -- resetting\n", adapter->unit);

	ifp->if_flags &= ~IFF_RUNNING;

	em_stop(adapter);
	em_init(adapter);

	ifp->if_oerrors++;
	return;
}

/*********************************************************************
 *  Init entry point
 *
 *  This routine is used in two ways. It is used by the stack as
 *  init entry point in network interface structure. It is also used
 *  by the driver as a hw/sw initialization routine to get to a 
 *  consistent state.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/

static void
em_init(void *arg)
{
	int             s;
	struct ifnet   *ifp;
	struct adapter * adapter = arg;

	INIT_DEBUGOUT("em_init: begin");

	s = splimp();

	em_stop(adapter);

	/* Get the latest mac address, User can use a LAA */
	bcopy(adapter->interface_data.ac_enaddr, adapter->hw.mac_addr,
              ETHER_ADDR_LEN);

	/* Initialize the hardware */
	if (em_hardware_init(adapter)) {
		dprintf("ipro1000/%d: Unable to initialize the hardware\n", 
		       adapter->unit);
		splx(s);
		return;
	}

	em_enable_vlans(adapter);

	/* Prepare transmit descriptors and buffers */
	if (em_setup_transmit_structures(adapter)) {
		dprintf("ipro1000/%d: Could not setup transmit structures\n", 
		       adapter->unit);
		em_stop(adapter); 
		splx(s);
		return;
	}
	em_initialize_transmit_unit(adapter);

	/* Setup Multicast table */
	em_set_multi(adapter);

	/* Prepare receive descriptors and buffers */
	if (em_setup_receive_structures(adapter)) {
		dprintf("ipro1000/%d: Could not setup receive structures\n", 
		       adapter->unit);
		em_stop(adapter);
		splx(s);
		return;
	}
	em_initialize_receive_unit(adapter);
	
	/* Don't loose promiscuous settings */
	em_set_promisc(adapter);

	ifp = &adapter->interface_data.ac_if;
	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	if (adapter->hw.mac_type >= em_82543) {
		if (ifp->if_capenable & IFCAP_TXCSUM)
			ifp->if_hwassist = EM_CHECKSUM_FEATURES;
		else
			ifp->if_hwassist = 0;
	}

	adapter->timer_handle = timeout(em_local_timer, adapter, 2*hz);
	em_clear_hw_cntrs(&adapter->hw);
	em_enable_intr(adapter);

	/* Don't reset the phy next time init gets called */
	adapter->hw.phy_reset_disable = TRUE;

	splx(s);
	return;
}


/*********************************************************************
 *
 *  Interrupt Service routine  
 *
 **********************************************************************/
static int32
em_intr(void *arg)
{
        u_int32_t       loop_cnt = EM_MAX_INTR;
        u_int32_t       reg_icr;
        struct ifnet    *ifp;
        struct adapter  *adapter = arg;
        bool			release_event_sem = false;

        ifp = &adapter->interface_data.ac_if;  

	reg_icr = E1000_READ_REG(&adapter->hw, ICR);
	if (!reg_icr) {
		return B_UNHANDLED_INTERRUPT;
	}

	/* Link status change */
	if (reg_icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		atomic_or(&adapter->event_flags, EVENT_LINK_CHANGED);
		release_event_sem = true;
	}

        while (loop_cnt > 0) {
                if (ifp->if_flags & IFF_RUNNING) {
                        em_process_receive_interrupts(adapter, -1);
                        em_clean_transmit_interrupts(adapter);
                }
                loop_cnt--;
        }

//	if (ifp->if_flags & IFF_RUNNING && ifp->if_snd.ifq_head != NULL) {
//		atomic_or(&adapter->event_flags, EVENT_RESTART_TX);
//		release_event_sem = true;
//	}
	
	if (release_event_sem)
		release_sem_etc(adapter->event_sem, 1, B_DO_NOT_RESCHEDULE);

	return B_INVOKE_SCHEDULER;
}

static int32
event_handler(void *cookie)
{
	struct adapter * adapter = cookie;
	int32 events;
	
	for (;;) {
		if (acquire_sem_etc(adapter->event_sem, 1, B_CAN_INTERRUPT, 0) == B_BAD_SEM_ID)
			return 0;

		events = atomic_read(&adapter->event_flags); // read
		atomic_and(&adapter->event_flags, ~events);	 // and clear

		if (events & EVENT_LINK_CHANGED) {
			INIT_DEBUGOUT("EVENT_LINK_CHANGED");
			untimeout(em_local_timer, adapter, adapter->timer_handle);
			adapter->hw.get_link_status = 1;
			em_check_for_link(&adapter->hw);
			em_print_link_status(adapter);
			adapter->timer_handle =	timeout(em_local_timer, adapter, 2*hz);

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
			if (adapter->osdep.dev->linkChangeSem != -1)
				release_sem_etc(adapter->osdep.dev->linkChangeSem, 1,
								B_DO_NOT_RESCHEDULE);
#endif
		}

	}
}


/*********************************************************************
 *
 *  Media Ioctl callback
 *
 *  This routine is called whenever the user queries the status of
 *  the interface using ifconfig.
 *
 **********************************************************************/
void
em_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct adapter * adapter = ifp->if_softc;

	INIT_DEBUGOUT("em_media_status: begin");

	em_check_for_link(&adapter->hw);
	if (E1000_READ_REG(&adapter->hw, STATUS) & E1000_STATUS_LU) {
		if (adapter->link_active == 0) {
			em_get_speed_and_duplex(&adapter->hw, 
						&adapter->link_speed, 
						&adapter->link_duplex);
			adapter->link_active = 1;
		}
	} else {
		if (adapter->link_active == 1) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;
			adapter->link_active = 0;
		}
	}

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!adapter->link_active)
		return;

	ifmr->ifm_status |= IFM_ACTIVE;

	if (adapter->hw.media_type == em_media_type_fiber) {
		ifmr->ifm_active |= IFM_1000_SX | IFM_FDX;
	} else {
		switch (adapter->link_speed) {
		case 10:
			ifmr->ifm_active |= IFM_10_T;
			break;
		case 100:
			ifmr->ifm_active |= IFM_100_TX;
			break;
		case 1000:
#if __FreeBSD_version < 500000 
			ifmr->ifm_active |= IFM_1000_TX;
#else
			ifmr->ifm_active |= IFM_1000_T;
#endif
			break;
		}
		if (adapter->link_duplex == FULL_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;
	}
	return;
}

#if 0
/*********************************************************************
 *
 *  Media Ioctl callback
 *
 *  This routine is called when the user changes speed/duplex using
 *  media/mediopt option with ifconfig.
 *
 **********************************************************************/
static int
em_media_change(struct ifnet *ifp)
{
	struct adapter * adapter = ifp->if_softc;
	struct ifmedia  *ifm = &adapter->media;

	INIT_DEBUGOUT("em_media_change: begin");

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return(EINVAL);

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		adapter->hw.autoneg = DO_AUTO_NEG;
		adapter->hw.autoneg_advertised = AUTONEG_ADV_DEFAULT;
		break;
	case IFM_1000_SX:
#if __FreeBSD_version < 500000 
	case IFM_1000_TX:
#else
	case IFM_1000_T:
#endif
		adapter->hw.autoneg = DO_AUTO_NEG;
		adapter->hw.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case IFM_100_TX:
		adapter->hw.autoneg = FALSE;
		adapter->hw.autoneg_advertised = 0;
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			adapter->hw.forced_speed_duplex = em_100_full;
		else
			adapter->hw.forced_speed_duplex	= em_100_half;
		break;
	case IFM_10_T:
		adapter->hw.autoneg = FALSE;
		adapter->hw.autoneg_advertised = 0;
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			adapter->hw.forced_speed_duplex = em_10_full;
		else
			adapter->hw.forced_speed_duplex	= em_10_half;
		break;
	default:
		dprintf("ipro1000/%d: Unsupported media type\n", adapter->unit);
	}

	/* As the speed/duplex settings my have changed we nee to
	 * reset the PHY.
	 */
	adapter->hw.phy_reset_disable = FALSE;

	em_init(adapter);

	return(0);
}
#endif // #if 0

#define EM_FIFO_HDR              0x10
#define EM_82547_PKT_THRESH      0x3e0
#define EM_82547_TX_FIFO_SIZE    0x2800
#define EM_82547_TX_FIFO_BEGIN   0xf00
/*********************************************************************
 *
 *  This routine maps the mbufs to tx descriptors.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/

static int
em_encap(struct adapter *adapter, struct mbuf *m_head)
{ 
        vm_offset_t     virtual_addr;
        u_int32_t       txd_upper;
        u_int32_t       txd_lower;
        int             txd_used, i, txd_saved;
        struct mbuf     *mp;
	u_int64_t	address;

/* For 82544 Workaround */
    	DESC_ARRAY              desc_array;
    	u_int32_t               array_elements;
    	u_int32_t               counter;

#if __FreeBSD_version < 500000
        struct ifvlan *ifv = NULL;
#else
        struct m_tag    *mtag;
#endif
        struct em_buffer   *tx_buffer = NULL;
        struct em_tx_desc *current_tx_desc = NULL;
        struct ifnet   *ifp = &adapter->interface_data.ac_if;

	/* 
	 * Force a cleanup if number of TX descriptors 
	 * available hits the threshold 
	 */
	if (adapter->num_tx_desc_avail <= EM_TX_CLEANUP_THRESHOLD)
		em_clean_transmit_interrupts(adapter);

	if (adapter->num_tx_desc_avail <= EM_TX_CLEANUP_THRESHOLD) {
		adapter->no_tx_desc_avail1++;
		return (ENOBUFS);
	}

	if (ifp->if_hwassist > 0) {
		em_transmit_checksum_setup(adapter,  m_head,
					   &txd_upper, &txd_lower);
	}
	else 
		txd_upper = txd_lower = 0;


        /* Find out if we are in vlan mode */
#if __FreeBSD_version < 500000
        if ((m_head->m_flags & (M_PROTO1|M_PKTHDR)) == (M_PROTO1|M_PKTHDR) &&
            m_head->m_pkthdr.rcvif != NULL &&
            m_head->m_pkthdr.rcvif->if_type == IFT_L2VLAN)
                ifv = m_head->m_pkthdr.rcvif->if_softc;
#else
	mtag = VLAN_OUTPUT_TAG(ifp, m_head);
#endif

	i = adapter->next_avail_tx_desc;
	txd_saved = i;
	txd_used = 0;

	for (mp = m_head; mp != NULL; mp = mp->m_next) {
		if (mp->m_len == 0)
			continue;
	/* If adapter is 82544 and on PCIX bus */ 	
        	if(adapter->pcix_82544) {
			array_elements = 0;
			virtual_addr= mtod(mp, vm_offset_t);
			address = vtophys(virtual_addr);
			/* Check the Address and Length combination and split the data accordingly */
			array_elements = em_fill_descriptors(
			  address,
                          mp->m_len,  
                          &desc_array);

			for (counter = 0; counter < array_elements; counter++) {
				if (txd_used == adapter->num_tx_desc_avail) {
                               		 adapter->next_avail_tx_desc = txd_saved;
                              		  adapter->no_tx_desc_avail2++;
                              		  return (ENOBUFS);
                        	}

				tx_buffer = &adapter->tx_buffer_area[i];
	               		current_tx_desc = &adapter->tx_desc_base[i];
           			/*  Put in the buffer address*/
           			current_tx_desc->buffer_addr = desc_array.descriptor[counter].address;
           		 	/*  Put in the length */
           		   	current_tx_desc->lower.data = (adapter->txd_cmd | txd_lower 
						| (u_int16_t)desc_array.descriptor[counter].length);
				current_tx_desc->upper.data = (txd_upper);	
				if (++i == adapter->num_tx_desc)
       		                	 i = 0;
                		tx_buffer->m_head = NULL;
                		txd_used++;
			}
        	}
		else {
			if (txd_used == adapter->num_tx_desc_avail) {
                       		 adapter->next_avail_tx_desc = txd_saved;
                       		 adapter->no_tx_desc_avail2++;
                       		 return (ENOBUFS);
               		 }

			tx_buffer = &adapter->tx_buffer_area[i];
			current_tx_desc = &adapter->tx_desc_base[i];
			virtual_addr = mtod(mp, vm_offset_t);
			current_tx_desc->buffer_addr = vtophys(virtual_addr);
			current_tx_desc->lower.data = (adapter->txd_cmd | txd_lower | mp->m_len);
			current_tx_desc->upper.data = (txd_upper);

			if (++i == adapter->num_tx_desc)
				i = 0;

			tx_buffer->m_head = NULL;

			txd_used++;
		}
	}
        adapter->num_tx_desc_avail -= txd_used;
        adapter->next_avail_tx_desc = i;

#if __FreeBSD_version < 500000
        if (ifv != NULL) {
                /* Set the vlan id */
                current_tx_desc->upper.fields.special = ifv->ifv_tag;
#else
        if (mtag != NULL) {
                /* Set the vlan id */
                current_tx_desc->upper.fields.special = VLAN_TAG_VALUE(mtag);
#endif
                /* Tell hardware to add tag */
                current_tx_desc->lower.data |= E1000_TXD_CMD_VLE;
        }

	tx_buffer->m_head = m_head;
	
	/* 
	 * Last Descriptor of Packet needs End Of Packet (EOP)
	 */
	current_tx_desc->lower.data |= (E1000_TXD_CMD_EOP);

	/* 
	 * Advance the Transmit Descriptor Tail (Tdt), this tells the E1000
	 * that this frame is available to transmit.
	 */
	if (adapter->hw.mac_type == em_82547 &&
	    adapter->link_duplex == HALF_DUPLEX) {
		em_82547_move_tail(adapter);
	}
	else {
		E1000_WRITE_REG(&adapter->hw, TDT, i);
		if (adapter->hw.mac_type == em_82547) {
			em_82547_update_fifo_head(adapter, m_head->m_pkthdr.len);
		}
	}

	return (0);
}


/*********************************************************************
 *
 * 82547 workaround to avoid controller hang in half-duplex environment.
 * The workaround is to avoid queuing a large packet that would span   
 * the internal Tx FIFO ring boundary. We need to reset the FIFO pointers
 * in this case. We do that only when FIFO is queiced.
 *
 **********************************************************************/
static void
em_82547_move_tail(void *arg)
{
	int s;
	struct adapter *adapter = arg;
	uint16_t hw_tdt;
	uint16_t sw_tdt;
	struct em_tx_desc *tx_desc;
	uint16_t length = 0;
	boolean_t eop = 0;

	s = splimp();
	hw_tdt = E1000_READ_REG(&adapter->hw, TDT);
	sw_tdt = adapter->next_avail_tx_desc;
	
	while (hw_tdt != sw_tdt) {
		tx_desc = &adapter->tx_desc_base[hw_tdt];
		length += tx_desc->lower.flags.length;
		eop = tx_desc->lower.data & E1000_TXD_CMD_EOP;
		if(++hw_tdt == adapter->num_tx_desc)
			hw_tdt = 0;

		if(eop) {
			if (em_82547_fifo_workaround(adapter, length)) {
				adapter->tx_fifo_wrk++;
				adapter->tx_fifo_timer_handle = 
					timeout(em_82547_move_tail,
						adapter, 100);
				splx(s);
				return;
			}
			else {
				E1000_WRITE_REG(&adapter->hw, TDT, hw_tdt);
				em_82547_update_fifo_head(adapter, length);
				length = 0;
			}
		}
	}	
	splx(s);
	return;
}

static int
em_82547_fifo_workaround(struct adapter *adapter, int len)
{	
	int fifo_space, fifo_pkt_len;

	fifo_pkt_len = EM_ROUNDUP(len + EM_FIFO_HDR, EM_FIFO_HDR);

	if (adapter->link_duplex == HALF_DUPLEX) {
		fifo_space = EM_82547_TX_FIFO_SIZE - adapter->tx_fifo_head;

		if (fifo_pkt_len >= (EM_82547_PKT_THRESH + fifo_space)) {
			if (em_82547_tx_fifo_reset(adapter)) {
				return(0);
			}
			else {
				return(1);
			}
		}
	}

	return(0);
}

static void
em_82547_update_fifo_head(struct adapter *adapter, int len)
{
	int fifo_pkt_len = EM_ROUNDUP(len + EM_FIFO_HDR, EM_FIFO_HDR);
	
	/* tx_fifo_head is always 16 byte aligned */
	adapter->tx_fifo_head += fifo_pkt_len;
	if (adapter->tx_fifo_head >= EM_82547_TX_FIFO_SIZE) {
		adapter->tx_fifo_head -= EM_82547_TX_FIFO_SIZE;
	}

	return;
}


static int
em_82547_tx_fifo_reset(struct adapter *adapter)
{	
	uint32_t tctl;

	if ( (E1000_READ_REG(&adapter->hw, TDT) ==
	      E1000_READ_REG(&adapter->hw, TDH)) &&
	     (E1000_READ_REG(&adapter->hw, TDFT) == 
	      E1000_READ_REG(&adapter->hw, TDFH)) &&
	     (E1000_READ_REG(&adapter->hw, TDFTS) ==
	      E1000_READ_REG(&adapter->hw, TDFHS)) &&
	     (E1000_READ_REG(&adapter->hw, TDFPC) == 0)) {

		/* Disable TX unit */
		tctl = E1000_READ_REG(&adapter->hw, TCTL);
		E1000_WRITE_REG(&adapter->hw, TCTL, tctl & ~E1000_TCTL_EN);

		/* Reset FIFO pointers */
		E1000_WRITE_REG(&adapter->hw, TDFT, EM_82547_TX_FIFO_BEGIN);
		E1000_WRITE_REG(&adapter->hw, TDFH, EM_82547_TX_FIFO_BEGIN);
		E1000_WRITE_REG(&adapter->hw, TDFTS, EM_82547_TX_FIFO_BEGIN);
		E1000_WRITE_REG(&adapter->hw, TDFHS, EM_82547_TX_FIFO_BEGIN);

		/* Re-enable TX unit */
		E1000_WRITE_REG(&adapter->hw, TCTL, tctl);
		E1000_WRITE_FLUSH(&adapter->hw);

		adapter->tx_fifo_head = 0;
		adapter->tx_fifo_reset++;

		return(TRUE);
	}
	else {
		return(FALSE);
	}
}

static void
em_set_promisc(struct adapter * adapter)
{

	u_int32_t       reg_rctl;
	struct ifnet   *ifp = &adapter->interface_data.ac_if;

	reg_rctl = E1000_READ_REG(&adapter->hw, RCTL);

	if (ifp->if_flags & IFF_PROMISC) {
		reg_rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
		E1000_WRITE_REG(&adapter->hw, RCTL, reg_rctl);
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		reg_rctl |= E1000_RCTL_MPE;
		reg_rctl &= ~E1000_RCTL_UPE;
		E1000_WRITE_REG(&adapter->hw, RCTL, reg_rctl);
	}

	return;
}

static void
em_disable_promisc(struct adapter * adapter)
{
	u_int32_t       reg_rctl;

	reg_rctl = E1000_READ_REG(&adapter->hw, RCTL);

	reg_rctl &=  (~E1000_RCTL_UPE);
	reg_rctl &=  (~E1000_RCTL_MPE);
	E1000_WRITE_REG(&adapter->hw, RCTL, reg_rctl);

	return;
}


/*********************************************************************
 *  Multicast Update
 *
 *  This routine is called whenever multicast address list is updated.
 *
 **********************************************************************/

static void
em_set_multi(struct adapter * adapter)
{
        u_int32_t reg_rctl = 0;
        u_int8_t  mta[MAX_NUM_MULTICAST_ADDRESSES * ETH_LENGTH_OF_ADDRESS];
        struct ifmultiaddr  *ifma;
        int mcnt = 0;
        struct ifnet   *ifp = &adapter->interface_data.ac_if;
    
        IOCTL_DEBUGOUT("em_set_multi: begin");
 
        if (adapter->hw.mac_type == em_82542_rev2_0) {
                reg_rctl = E1000_READ_REG(&adapter->hw, RCTL);
                if (adapter->hw.pci_cmd_word & CMD_MEM_WRT_INVALIDATE) { 
                        em_pci_clear_mwi(&adapter->hw);
                }
                reg_rctl |= E1000_RCTL_RST;
                E1000_WRITE_REG(&adapter->hw, RCTL, reg_rctl);
                msec_delay(5);
        }
        
#if __FreeBSD_version < 500000
        LIST_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
#else
        TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
#endif  
                if (ifma->ifma_addr->sa_family != AF_LINK)
                        continue;
 
		if (mcnt == MAX_NUM_MULTICAST_ADDRESSES) break;

                bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
                      &mta[mcnt*ETH_LENGTH_OF_ADDRESS], ETH_LENGTH_OF_ADDRESS);
                mcnt++;
        }

        if (mcnt >= MAX_NUM_MULTICAST_ADDRESSES) {
                reg_rctl = E1000_READ_REG(&adapter->hw, RCTL);
                reg_rctl |= E1000_RCTL_MPE;
                E1000_WRITE_REG(&adapter->hw, RCTL, reg_rctl);
        } else
                em_mc_addr_list_update(&adapter->hw, mta, mcnt, 0, 1);

        if (adapter->hw.mac_type == em_82542_rev2_0) {
                reg_rctl = E1000_READ_REG(&adapter->hw, RCTL);
                reg_rctl &= ~E1000_RCTL_RST;
                E1000_WRITE_REG(&adapter->hw, RCTL, reg_rctl);
                msec_delay(5);
                if (adapter->hw.pci_cmd_word & CMD_MEM_WRT_INVALIDATE) {
                        em_pci_set_mwi(&adapter->hw);
                }
        }

        return;
}


/*********************************************************************
 *  Timer routine
 *
 *  This routine checks for link status and updates statistics.
 *
 **********************************************************************/

static void
em_local_timer(void *arg)
{
	int s;
	struct ifnet   *ifp;
	struct adapter * adapter = arg;
	ifp = &adapter->interface_data.ac_if;

	s = splimp();

	em_check_for_link(&adapter->hw);
	em_print_link_status(adapter);
	em_update_stats_counters(adapter);   
	if (DEBUG_DISPLAY_STATS && ifp->if_flags & IFF_RUNNING) {
		em_print_hw_stats(adapter);
		em_print_debug_info(adapter);
	}
	em_smartspeed(adapter);

	adapter->timer_handle = timeout(em_local_timer, adapter, 2*hz);

	splx(s);
	return;
}

static void
em_print_link_status(struct adapter * adapter)
{
	if (E1000_READ_REG(&adapter->hw, STATUS) & E1000_STATUS_LU) {
		if (adapter->link_active == 0) {
			em_get_speed_and_duplex(&adapter->hw, 
						&adapter->link_speed, 
						&adapter->link_duplex);
			dprintf("ipro1000/%d: Link is up %d Mbps %s\n",
			       adapter->unit,
			       adapter->link_speed,
			       ((adapter->link_duplex == FULL_DUPLEX) ?
				"Full Duplex" : "Half Duplex"));
			adapter->link_active = 1;
			adapter->smartspeed = 0;
		}
	} else {
		if (adapter->link_active == 1) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;
			dprintf("ipro1000/%d: Link is Down\n", adapter->unit);
			adapter->link_active = 0;
		}
	}

	return;
}

/*********************************************************************
 *
 *  This routine disables all traffic on the adapter by issuing a
 *  global reset on the MAC and deallocates TX/RX buffers. 
 *
 **********************************************************************/

static void
em_stop(void *arg)
{
	struct ifnet   *ifp;
	struct adapter * adapter = arg;
	ifp = &adapter->interface_data.ac_if;

	INIT_DEBUGOUT("em_stop: begin");
	em_disable_intr(adapter);
	em_reset_hw(&adapter->hw);
	untimeout(em_local_timer, adapter, adapter->timer_handle);	
	untimeout(em_82547_move_tail, adapter, 
		  adapter->tx_fifo_timer_handle);
	em_free_transmit_structures(adapter);
	em_free_receive_structures(adapter);


	/* Tell the stack that the interface is no longer active */
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	return;
}


/*********************************************************************
 *
 *  Determine hardware revision.
 *
 **********************************************************************/
static void
em_identify_hardware(struct adapter * adapter)
{
	device_t dev = adapter->dev;

	/* Make sure our PCI config space has the necessary stuff set */
	adapter->hw.pci_cmd_word = pci_read_config(dev, PCIR_COMMAND, 2);
	if (!((adapter->hw.pci_cmd_word & PCIM_CMD_BUSMASTEREN) &&
	      (adapter->hw.pci_cmd_word & PCIM_CMD_MEMEN))) {
		dprintf("ipro1000/%d: Memory Access and/or Bus Master bits were not set!\n", 
		       adapter->unit);
		adapter->hw.pci_cmd_word |= 
		(PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN);
		pci_write_config(dev, PCIR_COMMAND, adapter->hw.pci_cmd_word, 2);
	}

	/* Save off the information about this board */
	adapter->hw.vendor_id = pci_get_vendor(dev);
	adapter->hw.device_id = pci_get_device(dev);
	adapter->hw.revision_id = pci_read_config(dev, PCIR_REVID, 1);
	adapter->hw.subsystem_vendor_id = pci_read_config(dev, PCIR_SUBVEND_0, 2);
	adapter->hw.subsystem_id = pci_read_config(dev, PCIR_SUBDEV_0, 2);

	/* Identify the MAC */
   if (em_set_mac_type(&adapter->hw))
           dprintf("ipro1000/%d: Unknown MAC Type\n", adapter->unit);

   if(adapter->hw.mac_type == em_82541 || adapter->hw.mac_type == em_82541_rev_2 ||
      adapter->hw.mac_type == em_82547 || adapter->hw.mac_type == em_82547_rev_2)
		   adapter->hw.phy_init_script = TRUE;


        return;
}

static int
em_allocate_pci_resources(struct adapter * adapter)
{
	int             i, val, rid;
	device_t        dev = adapter->dev;

	rid = EM_MMBA;
	adapter->res_memory = bus_alloc_resource(dev, SYS_RES_MEMORY,
						 &rid, 0, ~0, 1,
						 RF_ACTIVE);
	if (!(adapter->res_memory)) {
		dprintf("ipro1000/%d: Unable to allocate bus resource: memory\n", 
		       adapter->unit);
		return(ENXIO);
	}
	
	dev->regAddr = adapter->res_memory;

	if (adapter->hw.mac_type > em_82543) {
		/* Enable IO space access, added for BeOS */
		if (!(adapter->hw.pci_cmd_word & PCIM_CMD_IOEN)) {
			dprintf("ipro1000/%d: IO Access bit was not set!\n", 
			       adapter->unit);
			adapter->hw.pci_cmd_word |= PCIM_CMD_IOEN;
			pci_write_config(dev, PCIR_COMMAND, adapter->hw.pci_cmd_word, 2);
		}
	
		/* Figure our where our IO BAR is ? */
		rid = EM_MMBA;
		for (i = 0; i < 5; i++) {
			val = pci_read_config(dev, rid, 4);
			if (val & 0x00000001) {
				adapter->io_rid = rid;
				break;
			}
			rid += 4;
		}

		adapter->res_ioport = bus_alloc_resource(dev, SYS_RES_IOPORT,  
							 &adapter->io_rid, 0, ~0, 1,
							 RF_ACTIVE);   
		if (!(adapter->res_ioport)) {
			dprintf("ipro1000/%d: Unable to allocate bus resource: ioport\n",
			       adapter->unit);
			return(ENXIO);  
		}

		adapter->hw.io_base =
		rman_get_start(adapter->res_ioport);
	}

	rid = 0x0;
	adapter->res_interrupt = bus_alloc_resource(dev, SYS_RES_IRQ,
						    &rid, 0, ~0, 1,
						    RF_SHAREABLE | RF_ACTIVE);
	if (!(adapter->res_interrupt)) {
		dprintf("ipro1000/%d: Unable to allocate bus resource: interrupt\n", 
		       adapter->unit);
		return(ENXIO);
	}

	adapter->hw.back = &adapter->osdep;

	if (bus_setup_intr(dev, adapter->res_interrupt, INTR_TYPE_NET,
			   em_intr, adapter,
			   &adapter->int_handler_tag)) {
		dprintf("ipro1000/%d: Error registering interrupt handler!\n", 
		       adapter->unit);
		return(ENXIO);
	}

	return 0;
}

static void
em_free_pci_resources(struct adapter * adapter)
{
	device_t dev = adapter->dev;

	if (adapter->res_interrupt != NULL) {
		bus_teardown_intr(dev, adapter->res_interrupt, 
				  adapter->int_handler_tag);
		bus_release_resource(dev, SYS_RES_IRQ, 0, 
				     adapter->res_interrupt);
	}
	if (adapter->res_memory != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, EM_MMBA, 
				     adapter->res_memory);
	}

	if (adapter->res_ioport != NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT, adapter->io_rid, 
				     adapter->res_ioport);
	}
	return;
}

/*********************************************************************
 *
 *  Initialize the hardware to a configuration as specified by the
 *  adapter structure. The controller is reset, the EEPROM is
 *  verified, the MAC address is set, then the shared initialization
 *  routines are called.
 *
 **********************************************************************/
static int
em_hardware_init(struct adapter * adapter)
{
	INIT_DEBUGOUT("em_hardware_init: begin");
	/* Issue a global reset */
	em_reset_hw(&adapter->hw);

	/* When hardware is reset, fifo_head is also reset */
	adapter->tx_fifo_head = 0;

	/* Make sure we have a good EEPROM before we read from it */
	if (em_validate_eeprom_checksum(&adapter->hw) < 0) {
		dprintf("ipro1000/%d: The EEPROM Checksum Is Not Valid\n",
		       adapter->unit);
		return(EIO);
	}

	if (em_read_part_num(&adapter->hw, &(adapter->part_num)) < 0) {
		dprintf("ipro1000/%d: EEPROM read error while reading part number\n",
		       adapter->unit);
		return(EIO);
	}

	if (em_init_hw(&adapter->hw) < 0) {
		dprintf("ipro1000/%d: Hardware Initialization Failed",
		       adapter->unit);
		return(EIO);
	}

	em_check_for_link(&adapter->hw);
	if (E1000_READ_REG(&adapter->hw, STATUS) & E1000_STATUS_LU)
		adapter->link_active = 1;
	else
		adapter->link_active = 0;

	if (adapter->link_active) {
		em_get_speed_and_duplex(&adapter->hw, 
					&adapter->link_speed, 
					&adapter->link_duplex);
	} else {
		adapter->link_speed = 0;
		adapter->link_duplex = 0;
	}

	return(0);
}

/*********************************************************************
 *
 *  Setup networking device structure and register an interface.
 *
 **********************************************************************/
static void
em_setup_interface(device_t dev, struct adapter * adapter)
{
	struct ifnet   *ifp;
	INIT_DEBUGOUT("em_setup_interface: begin");

	ifp = &adapter->interface_data.ac_if;
	ifp->if_unit = adapter->unit;
	ifp->if_name = "em";
	ifp->if_mtu = ETHERMTU;
	ifp->if_output = ether_output;
	ifp->if_baudrate = 1000000000;
	ifp->if_init =  em_init;
	ifp->if_softc = adapter;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = em_ioctl;
	ifp->if_start = em_start;
	ifp->if_watchdog = em_watchdog;
	ifp->if_snd.ifq_maxlen = adapter->num_tx_desc - 1;

#if __FreeBSD_version < 500000
        ether_ifattach(ifp, ETHER_BPF_SUPPORTED);
#else
        ether_ifattach(ifp, adapter->interface_data.ac_enaddr);
#endif

	if (adapter->hw.mac_type >= em_82543) {
		ifp->if_capabilities = IFCAP_HWCSUM;
		ifp->if_capenable = ifp->if_capabilities;
	}

 	/*
         * Tell the upper layer(s) we support long frames.
         */
        ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);
#if __FreeBSD_version >= 500000
        ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU;
#endif

	/* 
	 * Specify the media types supported by this adapter and register
	 * callbacks to update media and link information
	 */
	ifmedia_init(&adapter->media, IFM_IMASK, em_media_change,
		     em_media_status);
	if (adapter->hw.media_type == em_media_type_fiber) {
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_1000_SX | IFM_FDX, 
			    0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_1000_SX, 
			    0, NULL);
	} else {
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_10_T, 0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_10_T | IFM_FDX, 
			    0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_100_TX, 
			    0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_100_TX | IFM_FDX, 
			    0, NULL);
#if __FreeBSD_version < 500000 
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_1000_TX | IFM_FDX, 
			    0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_1000_TX, 0, NULL);
#else
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 
			    0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_1000_T, 0, NULL);
#endif
	}
	ifmedia_add(&adapter->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&adapter->media, IFM_ETHER | IFM_AUTO);

	return;
}


/*********************************************************************
 *
 *  Workaround for SmartSpeed on 82541 and 82547 controllers
 *
 **********************************************************************/        
static void
em_smartspeed(struct adapter *adapter)
{
        uint16_t phy_tmp;
 
	if(adapter->link_active || (adapter->hw.phy_type != em_phy_igp) || 
	   !adapter->hw.autoneg || !(adapter->hw.autoneg_advertised & ADVERTISE_1000_FULL))
		return;

        if(adapter->smartspeed == 0) {
                /* If Master/Slave config fault is asserted twice,
                 * we assume back-to-back */
                em_read_phy_reg(&adapter->hw, PHY_1000T_STATUS, &phy_tmp);
                if(!(phy_tmp & SR_1000T_MS_CONFIG_FAULT)) return;
                em_read_phy_reg(&adapter->hw, PHY_1000T_STATUS, &phy_tmp);
                if(phy_tmp & SR_1000T_MS_CONFIG_FAULT) {
                        em_read_phy_reg(&adapter->hw, PHY_1000T_CTRL,
					&phy_tmp);
                        if(phy_tmp & CR_1000T_MS_ENABLE) {
                                phy_tmp &= ~CR_1000T_MS_ENABLE;
                                em_write_phy_reg(&adapter->hw,
                                                    PHY_1000T_CTRL, phy_tmp);
                                adapter->smartspeed++;
                                if(adapter->hw.autoneg &&
                                   !em_phy_setup_autoneg(&adapter->hw) &&
				   !em_read_phy_reg(&adapter->hw, PHY_CTRL,
                                                       &phy_tmp)) {
                                        phy_tmp |= (MII_CR_AUTO_NEG_EN |  
                                                    MII_CR_RESTART_AUTO_NEG);
                                        em_write_phy_reg(&adapter->hw,
							 PHY_CTRL, phy_tmp);
                                }
                        }
                }
                return;
        } else if(adapter->smartspeed == EM_SMARTSPEED_DOWNSHIFT) {
                /* If still no link, perhaps using 2/3 pair cable */
                em_read_phy_reg(&adapter->hw, PHY_1000T_CTRL, &phy_tmp);
                phy_tmp |= CR_1000T_MS_ENABLE;
                em_write_phy_reg(&adapter->hw, PHY_1000T_CTRL, phy_tmp);
                if(adapter->hw.autoneg &&
                   !em_phy_setup_autoneg(&adapter->hw) &&
                   !em_read_phy_reg(&adapter->hw, PHY_CTRL, &phy_tmp)) {
                        phy_tmp |= (MII_CR_AUTO_NEG_EN |
                                    MII_CR_RESTART_AUTO_NEG);
                        em_write_phy_reg(&adapter->hw, PHY_CTRL, phy_tmp);
                }
        }
        /* Restart process after EM_SMARTSPEED_MAX iterations */
        if(adapter->smartspeed++ == EM_SMARTSPEED_MAX)
                adapter->smartspeed = 0;

	return;
}


/*********************************************************************
 *
 *  Allocate memory for tx_buffer structures. The tx_buffer stores all 
 *  the information needed to transmit a packet on the wire. 
 *
 **********************************************************************/
static int
em_allocate_transmit_structures(struct adapter * adapter)
{
	if (!(adapter->tx_buffer_area =
	      (struct em_buffer *) malloc(sizeof(struct em_buffer) *
					     adapter->num_tx_desc, M_DEVBUF,
					     M_NOWAIT))) {
		dprintf("ipro1000/%d: Unable to allocate tx_buffer memory\n", 
		       adapter->unit);
		return ENOMEM;
	}

	bzero(adapter->tx_buffer_area,
	      sizeof(struct em_buffer) * adapter->num_tx_desc);

	return 0;
}

/*********************************************************************
 *
 *  Allocate and initialize transmit structures. 
 *
 **********************************************************************/
static int
em_setup_transmit_structures(struct adapter * adapter)
{
	if (em_allocate_transmit_structures(adapter))
		return ENOMEM;

        bzero((void *) adapter->tx_desc_base,
              (sizeof(struct em_tx_desc)) * adapter->num_tx_desc);
                          
        adapter->next_avail_tx_desc = 0;
	adapter->oldest_used_tx_desc = 0;

	/* Set number of descriptors available */
	adapter->num_tx_desc_avail = adapter->num_tx_desc;

	/* Set checksum context */
	adapter->active_checksum_context = OFFLOAD_NONE;

	return 0;
}

/*********************************************************************
 *
 *  Enable transmit unit.
 *
 **********************************************************************/
static void
em_initialize_transmit_unit(struct adapter * adapter)
{
	u_int32_t       reg_tctl;
	u_int32_t       reg_tipg = 0;
	u_int64_t       tdba = vtophys((vm_offset_t)adapter->tx_desc_base);

	INIT_DEBUGOUT("em_initialize_transmit_unit: begin");

	/* Setup the Base and Length of the Tx Descriptor Ring */
	E1000_WRITE_REG(&adapter->hw, TDBAL,
			(tdba & 0x00000000ffffffffULL));
	E1000_WRITE_REG(&adapter->hw, TDBAH, (tdba >> 32));
	E1000_WRITE_REG(&adapter->hw, TDLEN, 
			adapter->num_tx_desc *
			sizeof(struct em_tx_desc));

	/* Setup the HW Tx Head and Tail descriptor pointers */
	E1000_WRITE_REG(&adapter->hw, TDH, 0);
	E1000_WRITE_REG(&adapter->hw, TDT, 0);


	HW_DEBUGOUT2("Base = %lx, Length = %lx", 
		     E1000_READ_REG(&adapter->hw, TDBAL),
		     E1000_READ_REG(&adapter->hw, TDLEN));


	/* Set the default values for the Tx Inter Packet Gap timer */
	switch (adapter->hw.mac_type) {
	case em_82542_rev2_0:
	case em_82542_rev2_1:
		reg_tipg = DEFAULT_82542_TIPG_IPGT;
		reg_tipg |= DEFAULT_82542_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
		reg_tipg |= DEFAULT_82542_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
		break;
	default:
		if (adapter->hw.media_type == em_media_type_fiber)
			reg_tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
		else
			reg_tipg = DEFAULT_82543_TIPG_IPGT_COPPER;
		reg_tipg |= DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
		reg_tipg |= DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
	}

	E1000_WRITE_REG(&adapter->hw, TIPG, reg_tipg);
	E1000_WRITE_REG(&adapter->hw, TIDV, adapter->tx_int_delay.value);
	if(adapter->hw.mac_type >= em_82540)
		E1000_WRITE_REG(&adapter->hw, TADV, adapter->tx_abs_int_delay.value);

	/* Program the Transmit Control Register */
	reg_tctl = E1000_TCTL_PSP | E1000_TCTL_EN |
		   (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);
	if (adapter->link_duplex == 1) {
		reg_tctl |= E1000_FDX_COLLISION_DISTANCE << E1000_COLD_SHIFT;
	} else {
		reg_tctl |= E1000_HDX_COLLISION_DISTANCE << E1000_COLD_SHIFT;
	}
	E1000_WRITE_REG(&adapter->hw, TCTL, reg_tctl);

	/* Setup Transmit Descriptor Settings for this adapter */   
	adapter->txd_cmd = E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;

	if (adapter->tx_int_delay.value > 0)
		adapter->txd_cmd |= E1000_TXD_CMD_IDE;

	return;
}

/*********************************************************************
 *
 *  Free all transmit related data structures.
 *
 **********************************************************************/
static void
em_free_transmit_structures(struct adapter * adapter)
{
	struct em_buffer   *tx_buffer;
	int             i;

	INIT_DEBUGOUT("free_transmit_structures: begin");

	if (adapter->tx_buffer_area != NULL) {
		tx_buffer = adapter->tx_buffer_area;
		for (i = 0; i < adapter->num_tx_desc; i++, tx_buffer++) {
			if (tx_buffer->m_head != NULL)
				m_freem(tx_buffer->m_head);
			tx_buffer->m_head = NULL;
		}
	}
	if (adapter->tx_buffer_area != NULL) {
		free(adapter->tx_buffer_area, M_DEVBUF);
		adapter->tx_buffer_area = NULL;
	}
	return;
}

/*********************************************************************
 *
 *  The offload context needs to be set when we transfer the first
 *  packet of a particular protocol (TCP/UDP). We change the
 *  context only if the protocol type changes.
 *
 **********************************************************************/
static void
em_transmit_checksum_setup(struct adapter * adapter,
			   struct mbuf *mp,
			   u_int32_t *txd_upper,
			   u_int32_t *txd_lower) 
{
	struct em_context_desc *TXD;
	struct em_buffer *tx_buffer;
	int curr_txd;

	if (mp->m_pkthdr.csum_flags) {

		if (mp->m_pkthdr.csum_flags & CSUM_TCP) {
			*txd_upper = E1000_TXD_POPTS_TXSM << 8;
			*txd_lower = E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
			if (adapter->active_checksum_context == OFFLOAD_TCP_IP)
				return;
			else
				adapter->active_checksum_context = OFFLOAD_TCP_IP;

		} else if (mp->m_pkthdr.csum_flags & CSUM_UDP) {
			*txd_upper = E1000_TXD_POPTS_TXSM << 8;
			*txd_lower = E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
			if (adapter->active_checksum_context == OFFLOAD_UDP_IP)
				return;
			else
				adapter->active_checksum_context = OFFLOAD_UDP_IP;
		} else {
			*txd_upper = 0;
			*txd_lower = 0;
			return;
		}
	} else {
		*txd_upper = 0;
		*txd_lower = 0;
		return;
	}

	/* If we reach this point, the checksum offload context
	 * needs to be reset.
	 */
	curr_txd = adapter->next_avail_tx_desc;
	tx_buffer = &adapter->tx_buffer_area[curr_txd];
	TXD = (struct em_context_desc *) &adapter->tx_desc_base[curr_txd];

	TXD->lower_setup.ip_fields.ipcss = ETHER_HDR_LEN;
	TXD->lower_setup.ip_fields.ipcso = 
		ETHER_HDR_LEN + OFFSETOF_IPHDR_SUM;
	TXD->lower_setup.ip_fields.ipcse = 
		ETHER_HDR_LEN + IP_HEADER_SIZE - 1;

	TXD->upper_setup.tcp_fields.tucss = 
		ETHER_HDR_LEN + IP_HEADER_SIZE;
	TXD->upper_setup.tcp_fields.tucse = 0;

	if (adapter->active_checksum_context == OFFLOAD_TCP_IP) {
		TXD->upper_setup.tcp_fields.tucso = 
			ETHER_HDR_LEN + IP_HEADER_SIZE + 
			OFFSETOF_TCPHDR_SUM;
	} else if (adapter->active_checksum_context == OFFLOAD_UDP_IP) {
		TXD->upper_setup.tcp_fields.tucso = 
			ETHER_HDR_LEN + IP_HEADER_SIZE + 
			OFFSETOF_UDPHDR_SUM;
	}

	TXD->tcp_seg_setup.data = 0;
	TXD->cmd_and_length = (adapter->txd_cmd | E1000_TXD_CMD_DEXT);

	tx_buffer->m_head = NULL;

	if (++curr_txd == adapter->num_tx_desc)
		curr_txd = 0;

	adapter->num_tx_desc_avail--;
	adapter->next_avail_tx_desc = curr_txd;

	return;
}

/**********************************************************************
 *
 *  Examine each tx_buffer in the used queue. If the hardware is done
 *  processing the packet then free associated resources. The
 *  tx_buffer is put back on the free queue.
 *
 **********************************************************************/
static void
em_clean_transmit_interrupts(struct adapter * adapter)
{
        int s;
        int i, num_avail;
	struct em_buffer *tx_buffer;
	struct em_tx_desc   *tx_desc;
	struct ifnet   *ifp = &adapter->interface_data.ac_if;

        if (adapter->num_tx_desc_avail == adapter->num_tx_desc)
                return;

        s = splimp();
#if DEBUG_DISPLAY_STATS
        adapter->clean_tx_interrupts++;
#endif
        num_avail = adapter->num_tx_desc_avail;	
	i = adapter->oldest_used_tx_desc;

	tx_buffer = &adapter->tx_buffer_area[i];
	tx_desc = &adapter->tx_desc_base[i];

	while(tx_desc->upper.fields.status & E1000_TXD_STAT_DD) {

		tx_desc->upper.data = 0;
		num_avail++;                        

		if (tx_buffer->m_head) {
			ifp->if_opackets++;
			m_freem(tx_buffer->m_head);
			tx_buffer->m_head = NULL;
		}
                
                if (++i == adapter->num_tx_desc)
                        i = 0;

		tx_buffer = &adapter->tx_buffer_area[i];
		tx_desc = &adapter->tx_desc_base[i];
        }

	adapter->oldest_used_tx_desc = i;

        /*
         * If we have enough room, clear IFF_OACTIVE to tell the stack
         * that it is OK to send packets.
         * If there are no pending descriptors, clear the timeout. Otherwise,
         * if some descriptors have been freed, restart the timeout.
         */
        if (num_avail > EM_TX_CLEANUP_THRESHOLD) {
                ifp->if_flags &= ~IFF_OACTIVE;
                if (num_avail == adapter->num_tx_desc)
                        ifp->if_timer = 0;
                else if (num_avail == adapter->num_tx_desc_avail)
                        ifp->if_timer = EM_TX_TIMEOUT;
        }
        adapter->num_tx_desc_avail = num_avail;
        splx(s);
        return;
}

/*********************************************************************
 *
 *  Get a buffer from system mbuf buffer pool.
 *
 **********************************************************************/
static int
em_get_buf(int i, struct adapter *adapter,
	   struct mbuf *nmp)
{
	register struct mbuf    *mp = nmp;
	struct ifnet   *ifp;

	ifp = &adapter->interface_data.ac_if;

	if (mp == NULL) {
		MGETHDR(mp, M_DONTWAIT, MT_DATA);
		if (mp == NULL) {
			adapter->mbuf_alloc_failed++;
			return(ENOBUFS);
		}
		MCLGET(mp, M_DONTWAIT);
		if ((mp->m_flags & M_EXT) == 0) {
			m_freem(mp);
			adapter->mbuf_cluster_failed++;
			return(ENOBUFS);
		}
		mp->m_len = mp->m_pkthdr.len = MCLBYTES;
	} else {
		mp->m_len = mp->m_pkthdr.len = MCLBYTES;
		mp->m_data = mp->m_ext.ext_buf;
		mp->m_next = NULL;
	}

	if (ifp->if_mtu <= ETHERMTU) {
		m_adj(mp, ETHER_ALIGN);
	}
 
	adapter->rx_buffer_area[i].m_head = mp;
        adapter->rx_desc_base[i].buffer_addr =
                vtophys(mtod(mp, vm_offset_t));

	return(0);
}

/*********************************************************************
 *
 *  Allocate memory for rx_buffer structures. Since we use one 
 *  rx_buffer per received packet, the maximum number of rx_buffer's 
 *  that we'll need is equal to the number of receive descriptors 
 *  that we've allocated.
 *
 **********************************************************************/
static int
em_allocate_receive_structures(struct adapter * adapter)
{
	int             i;

	if (!(adapter->rx_buffer_area =
	      (struct em_buffer *) malloc(sizeof(struct em_buffer) *
					     adapter->num_rx_desc, M_DEVBUF,
					     M_NOWAIT))) {
		dprintf("ipro1000/%d: Unable to allocate rx_buffer memory\n", 
		       adapter->unit);
		return(ENOMEM);
	}

	bzero(adapter->rx_buffer_area,
	      sizeof(struct em_buffer) * adapter->num_rx_desc);

	for (i = 0; i < adapter->num_rx_desc; i++) {
		if (em_get_buf(i, adapter, NULL) == ENOBUFS) {
			adapter->rx_buffer_area[i].m_head = NULL;
			adapter->rx_desc_base[i].buffer_addr = 0;
			return(ENOBUFS);
		}
	}

	return(0);
}

/*********************************************************************
 *
 *  Allocate and initialize receive structures.
 *  
 **********************************************************************/
static int
em_setup_receive_structures(struct adapter * adapter)
{
	bzero((void *) adapter->rx_desc_base,
              (sizeof(struct em_rx_desc)) * adapter->num_rx_desc);

	if (em_allocate_receive_structures(adapter))
		return ENOMEM;

	/* Setup our descriptor pointers */
        adapter->next_rx_desc_to_check = 0;
	return(0);
}

/*********************************************************************
 *
 *  Enable receive unit.
 *  
 **********************************************************************/
static void
em_initialize_receive_unit(struct adapter * adapter)
{
	u_int32_t       reg_rctl;
	u_int32_t       reg_rxcsum;
	struct ifnet    *ifp;
	u_int64_t       rdba = vtophys((vm_offset_t)adapter->rx_desc_base);

	INIT_DEBUGOUT("em_initialize_receive_unit: begin");

	ifp = &adapter->interface_data.ac_if;

	/* Make sure receives are disabled while setting up the descriptor ring */
	E1000_WRITE_REG(&adapter->hw, RCTL, 0);

	/* Set the Receive Delay Timer Register */
	E1000_WRITE_REG(&adapter->hw, RDTR, 
			adapter->rx_int_delay.value | E1000_RDT_FPDB);

	if(adapter->hw.mac_type >= em_82540) {
		E1000_WRITE_REG(&adapter->hw, RADV, adapter->rx_abs_int_delay.value);

                /* Set the interrupt throttling rate.  Value is calculated
                 * as DEFAULT_ITR = 1/(MAX_INTS_PER_SEC * 256ns) */
#define MAX_INTS_PER_SEC        8000
#define DEFAULT_ITR             1000000000/(MAX_INTS_PER_SEC * 256)
                E1000_WRITE_REG(&adapter->hw, ITR, DEFAULT_ITR);
        }       

	/* Setup the Base and Length of the Rx Descriptor Ring */
	E1000_WRITE_REG(&adapter->hw, RDBAL, 
			(rdba & 0x00000000ffffffffULL));
	E1000_WRITE_REG(&adapter->hw, RDBAH, (rdba >> 32));
	E1000_WRITE_REG(&adapter->hw, RDLEN, 
			adapter->num_rx_desc *
			sizeof(struct em_rx_desc));

	/* Setup the HW Rx Head and Tail Descriptor Pointers */
	E1000_WRITE_REG(&adapter->hw, RDH, 0);
	E1000_WRITE_REG(&adapter->hw, RDT, adapter->num_rx_desc - 1);

	/* Setup the Receive Control Register */
	reg_rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_LBM_NO |
		   E1000_RCTL_RDMTS_HALF |
		   (adapter->hw.mc_filter_type << E1000_RCTL_MO_SHIFT);

	if (adapter->hw.tbi_compatibility_on == TRUE)
		reg_rctl |= E1000_RCTL_SBP;


	switch (adapter->rx_buffer_len) {
	default:
	case EM_RXBUFFER_2048:
		reg_rctl |= E1000_RCTL_SZ_2048;
		break;
	case EM_RXBUFFER_4096:
		reg_rctl |= E1000_RCTL_SZ_4096 | E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;            
	case EM_RXBUFFER_8192:
		reg_rctl |= E1000_RCTL_SZ_8192 | E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;
	case EM_RXBUFFER_16384:
		reg_rctl |= E1000_RCTL_SZ_16384 | E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;
	}

	if (ifp->if_mtu > ETHERMTU)
		reg_rctl |= E1000_RCTL_LPE;

	/* Enable 82543 Receive Checksum Offload for TCP and UDP */
	if ((adapter->hw.mac_type >= em_82543) && 
	    (ifp->if_capenable & IFCAP_RXCSUM)) {
		reg_rxcsum = E1000_READ_REG(&adapter->hw, RXCSUM);
		reg_rxcsum |= (E1000_RXCSUM_IPOFL | E1000_RXCSUM_TUOFL);
		E1000_WRITE_REG(&adapter->hw, RXCSUM, reg_rxcsum);
	}

	/* Enable Receives */
	E1000_WRITE_REG(&adapter->hw, RCTL, reg_rctl);	

	return;
}

/*********************************************************************
 *
 *  Free receive related data structures.
 *
 **********************************************************************/
static void
em_free_receive_structures(struct adapter *adapter)
{
	struct em_buffer   *rx_buffer;
	int             i;

	INIT_DEBUGOUT("free_receive_structures: begin");

	if (adapter->rx_buffer_area != NULL) {
		rx_buffer = adapter->rx_buffer_area;
		for (i = 0; i < adapter->num_rx_desc; i++, rx_buffer++) {
			if (rx_buffer->m_head != NULL)
				m_freem(rx_buffer->m_head);
			rx_buffer->m_head = NULL;
		}
	}
	if (adapter->rx_buffer_area != NULL) {
		free(adapter->rx_buffer_area, M_DEVBUF);
		adapter->rx_buffer_area = NULL;
	}
	return;
}

/*********************************************************************
 *
 *  This routine executes in interrupt context. It replenishes
 *  the mbufs in the descriptor and sends data which has been
 *  dma'ed into host memory to upper layer.
 *
 *  We loop at most count times if count is > 0, or until done if
 *  count < 0.
 *
 *********************************************************************/
static void
em_process_receive_interrupts(struct adapter * adapter, int count)
{
	struct ifnet        *ifp;
	struct mbuf         *mp;
#if __FreeBSD_version < 500000
        struct ether_header *eh;
#endif
	u_int8_t            accept_frame = 0;
 	u_int8_t            eop = 0;
        u_int16_t           len, desc_len, prev_len_adj;
	int                 i;

	/* Pointer to the receive descriptor being examined. */
	struct em_rx_desc   *current_desc;

	ifp = &adapter->interface_data.ac_if;
	i = adapter->next_rx_desc_to_check;
        current_desc = &adapter->rx_desc_base[i];

	if (!((current_desc->status) & E1000_RXD_STAT_DD)) {
#if DEBUG_DISPLAY_STATS
		adapter->no_pkts_avail++;
#endif
		return;
	}

	while ((current_desc->status & E1000_RXD_STAT_DD) && (count != 0)) {
		
		mp = adapter->rx_buffer_area[i].m_head;

		accept_frame = 1;
		prev_len_adj = 0;
		desc_len = current_desc->length;
		if (current_desc->status & E1000_RXD_STAT_EOP) {
			count--;
			eop = 1;
			if (desc_len < ETHER_CRC_LEN) {
				len = 0;
				prev_len_adj = ETHER_CRC_LEN - desc_len;
			}
			else {
				len = desc_len - ETHER_CRC_LEN;
			}
		} else {
			eop = 0;
			len = desc_len;
		}

		if (current_desc->errors & E1000_RXD_ERR_FRAME_ERR_MASK) {
			u_int8_t            last_byte;
			u_int32_t           pkt_len = desc_len;

			if (adapter->fmp != NULL)
				pkt_len += adapter->fmp->m_pkthdr.len; 
 
			last_byte = *(mtod(mp, caddr_t) + desc_len - 1);			

			if (TBI_ACCEPT(&adapter->hw, current_desc->status, 
				       current_desc->errors, 
				       pkt_len, last_byte)) {
				em_tbi_adjust_stats(&adapter->hw, 
						    &adapter->stats, 
						    pkt_len, 
						    adapter->hw.mac_addr);
				if (len > 0) len--;
			} 
			else {
				accept_frame = 0;
			}
		}

		if (accept_frame) {

			if (em_get_buf(i, adapter, NULL) == ENOBUFS) {
				adapter->dropped_pkts++;
				em_get_buf(i, adapter, mp);
				if (adapter->fmp != NULL) 
					m_freem(adapter->fmp);
				adapter->fmp = NULL;
				adapter->lmp = NULL;
				break;
			}

			/* Assign correct length to the current fragment */
			mp->m_len = len;

			if (adapter->fmp == NULL) {
				mp->m_pkthdr.len = len;
				adapter->fmp = mp;	 /* Store the first mbuf */
				adapter->lmp = mp;
			} else {
				/* Chain mbuf's together */
				mp->m_flags &= ~M_PKTHDR;
				/* 
				 * Adjust length of previous mbuf in chain if we 
				 * received less than 4 bytes in the last descriptor.
				 */
				if (prev_len_adj > 0) {
					adapter->lmp->m_len -= prev_len_adj;
					adapter->fmp->m_pkthdr.len -= prev_len_adj;
				}
				adapter->lmp->m_next = mp;
				adapter->lmp = adapter->lmp->m_next;
				adapter->fmp->m_pkthdr.len += len;
			}

                        if (eop) {
                                adapter->fmp->m_pkthdr.rcvif = ifp;
				ifp->if_ipackets++;

#if __FreeBSD_version < 500000
                                eh = mtod(adapter->fmp, struct ether_header *);
                                /* Remove ethernet header from mbuf */
                                m_adj(adapter->fmp, sizeof(struct ether_header));
                                em_receive_checksum(adapter, current_desc,
                                                    adapter->fmp);
                                if (current_desc->status & E1000_RXD_STAT_VP)
                                        VLAN_INPUT_TAG(eh, adapter->fmp,
                                                       (current_desc->special & 
							E1000_RXD_SPC_VLAN_MASK));
                                else
                                        ether_input(ifp, eh, adapter->fmp);
#else

                                em_receive_checksum(adapter, current_desc,
                                                    adapter->fmp);
                                if (current_desc->status & E1000_RXD_STAT_VP)
                                        VLAN_INPUT_TAG(ifp, adapter->fmp,
                                                       (current_desc->special &
							E1000_RXD_SPC_VLAN_MASK),
						       adapter->fmp = NULL);
 
                                if (adapter->fmp != NULL)
                                        (*ifp->if_input)(ifp, adapter->fmp);
#endif
                                adapter->fmp = NULL;
                                adapter->lmp = NULL;
                        }
		} else {
			adapter->dropped_pkts++;
			em_get_buf(i, adapter, mp);
			if (adapter->fmp != NULL) 
				m_freem(adapter->fmp);
			adapter->fmp = NULL;
			adapter->lmp = NULL;
		}

		/* Zero out the receive descriptors status  */
		current_desc->status = 0;
 
		/* Advance the E1000's Receive Queue #0  "Tail Pointer". */
                E1000_WRITE_REG(&adapter->hw, RDT, i);

                /* Advance our pointers to the next descriptor */
                if (++i == adapter->num_rx_desc) {
                        i = 0;
                        current_desc = adapter->rx_desc_base;
                } else
			current_desc++;
	}
	adapter->next_rx_desc_to_check = i;
	return;
}

/*********************************************************************
 *
 *  Verify that the hardware indicated that the checksum is valid. 
 *  Inform the stack about the status of checksum so that stack
 *  doesn't spend time verifying the checksum.
 *
 *********************************************************************/
static void
em_receive_checksum(struct adapter *adapter,
		    struct em_rx_desc *rx_desc,
		    struct mbuf *mp)
{
	/* 82543 or newer only */
	if ((adapter->hw.mac_type < em_82543) ||
	    /* Ignore Checksum bit is set */
	    (rx_desc->status & E1000_RXD_STAT_IXSM)) {
		mp->m_pkthdr.csum_flags = 0;
		return;
	}

	if (rx_desc->status & E1000_RXD_STAT_IPCS) {
		/* Did it pass? */
		if (!(rx_desc->errors & E1000_RXD_ERR_IPE)) {
			/* IP Checksum Good */
			mp->m_pkthdr.csum_flags = CSUM_IP_CHECKED;
			mp->m_pkthdr.csum_flags |= CSUM_IP_VALID;

		} else {
			mp->m_pkthdr.csum_flags = 0;
		}
	}

	if (rx_desc->status & E1000_RXD_STAT_TCPCS) {
		/* Did it pass? */        
		if (!(rx_desc->errors & E1000_RXD_ERR_TCPE)) {
			mp->m_pkthdr.csum_flags |= 
			(CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
			mp->m_pkthdr.csum_data = htons(0xffff);
		}
	}

	return;
}


static void 
em_enable_vlans(struct adapter *adapter)
{
	uint32_t ctrl;

	E1000_WRITE_REG(&adapter->hw, VET, ETHERTYPE_VLAN);

	ctrl = E1000_READ_REG(&adapter->hw, CTRL);
	ctrl |= E1000_CTRL_VME; 
	E1000_WRITE_REG(&adapter->hw, CTRL, ctrl);

	return;
}

// can be called from within interrupt
static void
em_enable_intr(struct adapter * adapter)
{
	E1000_WRITE_REG(&adapter->hw, IMS, (IMS_ENABLE_MASK));
	return;
}

// can be called from within interrupt
static void
em_disable_intr(struct adapter *adapter)
{
	E1000_WRITE_REG(&adapter->hw, IMC, 
			(0xffffffff & ~E1000_IMC_RXSEQ));
	return;
}

static int
em_is_valid_ether_addr(u_int8_t *addr)
{
	char zero_addr[6] = { 0, 0, 0, 0, 0, 0 };

	if ((addr[0] & 1) || (!bcmp(addr, zero_addr, ETHER_ADDR_LEN))) {
		return (FALSE);
	}

	return(TRUE);
}

void 
em_write_pci_cfg(struct em_hw *hw,
		      uint32_t reg,
		      uint16_t *value)
{
	pci_write_config(((struct em_osdep *)hw->back)->dev, reg, 
			 *value, 2);
}

void 
em_read_pci_cfg(struct em_hw *hw, uint32_t reg,
		     uint16_t *value)
{
	*value = pci_read_config(((struct em_osdep *)hw->back)->dev,
				 reg, 2);
	return;
}

void
em_pci_set_mwi(struct em_hw *hw)
{
        pci_write_config(((struct em_osdep *)hw->back)->dev,
                         PCIR_COMMAND,
                         (hw->pci_cmd_word | CMD_MEM_WRT_INVALIDATE), 2);
        return;
}

void
em_pci_clear_mwi(struct em_hw *hw)
{
        pci_write_config(((struct em_osdep *)hw->back)->dev,
                         PCIR_COMMAND,
                         (hw->pci_cmd_word & ~CMD_MEM_WRT_INVALIDATE), 2);
        return;
}

uint32_t 
em_io_read(struct em_hw *hw, unsigned long port)
{
	return(inl(port));
}

void 
em_io_write(struct em_hw *hw, unsigned long port, uint32_t value)
{
	outl(port, value);
	return;
}

/*********************************************************************
* 82544 Coexistence issue workaround. 
*    There are 2 issues.
*	1. Transmit Hang issue.
*    To detect this issue, following equation can be used...
*          SIZE[3:0] + ADDR[2:0] = SUM[3:0].
*          If SUM[3:0] is in between 1 to 4, we will have this issue.
*
*	2. DAC issue.
*    To detect this issue, following equation can be used...
*          SIZE[3:0] + ADDR[2:0] = SUM[3:0].
*          If SUM[3:0] is in between 9 to c, we will have this issue.
*
*
*    WORKAROUND:
*          Make sure we do not have ending address as 1,2,3,4(Hang) or 9,a,b,c (DAC)
*
*** *********************************************************************/
static u_int32_t	
em_fill_descriptors (u_int64_t address, 
                              u_int32_t length, 
                              PDESC_ARRAY desc_array)
{
	/* Since issue is sensitive to length and address.*/
	/* Let us first check the address...*/
	u_int32_t safe_terminator;
	if (length <= 4) {
		desc_array->descriptor[0].address = address;
        	desc_array->descriptor[0].length = length;
        	desc_array->elements = 1;
		return desc_array->elements;
    	}
    	safe_terminator = (u_int32_t)((((u_int32_t)address & 0x7) + (length & 0xF)) & 0xF);
	/* if it does not fall between 0x1 to 0x4 and 0x9 to 0xC then return */ 
	if (safe_terminator == 0   ||
        (safe_terminator > 4   &&
        safe_terminator < 9)   || 
        (safe_terminator > 0xC &&
        safe_terminator <= 0xF)) {
        	desc_array->descriptor[0].address = address;
        	desc_array->descriptor[0].length = length;
        	desc_array->elements = 1;
		return desc_array->elements;
    	}
	
	desc_array->descriptor[0].address = address;
    	desc_array->descriptor[0].length = length - 4;
    	desc_array->descriptor[1].address = address + (length - 4);
    	desc_array->descriptor[1].length = 4;
    	desc_array->elements = 2;
	return desc_array->elements;
}



		
/**********************************************************************
 *
 *  Update the board statistics counters. 
 *
 **********************************************************************/
static void
em_update_stats_counters(struct adapter *adapter)
{
	struct ifnet   *ifp;

	if(adapter->hw.media_type == em_media_type_copper ||
	   (E1000_READ_REG(&adapter->hw, STATUS) & E1000_STATUS_LU)) {
		adapter->stats.symerrs += E1000_READ_REG(&adapter->hw, SYMERRS);
		adapter->stats.sec += E1000_READ_REG(&adapter->hw, SEC);
	}
	adapter->stats.crcerrs += E1000_READ_REG(&adapter->hw, CRCERRS);
	adapter->stats.mpc += E1000_READ_REG(&adapter->hw, MPC);
	adapter->stats.scc += E1000_READ_REG(&adapter->hw, SCC);
	adapter->stats.ecol += E1000_READ_REG(&adapter->hw, ECOL);

	adapter->stats.mcc += E1000_READ_REG(&adapter->hw, MCC);
	adapter->stats.latecol += E1000_READ_REG(&adapter->hw, LATECOL);
	adapter->stats.colc += E1000_READ_REG(&adapter->hw, COLC);
	adapter->stats.dc += E1000_READ_REG(&adapter->hw, DC);
	adapter->stats.rlec += E1000_READ_REG(&adapter->hw, RLEC);
	adapter->stats.xonrxc += E1000_READ_REG(&adapter->hw, XONRXC);
	adapter->stats.xontxc += E1000_READ_REG(&adapter->hw, XONTXC);
	adapter->stats.xoffrxc += E1000_READ_REG(&adapter->hw, XOFFRXC);
	adapter->stats.xofftxc += E1000_READ_REG(&adapter->hw, XOFFTXC);
	adapter->stats.fcruc += E1000_READ_REG(&adapter->hw, FCRUC);
	adapter->stats.prc64 += E1000_READ_REG(&adapter->hw, PRC64);
	adapter->stats.prc127 += E1000_READ_REG(&adapter->hw, PRC127);
	adapter->stats.prc255 += E1000_READ_REG(&adapter->hw, PRC255);
	adapter->stats.prc511 += E1000_READ_REG(&adapter->hw, PRC511);
	adapter->stats.prc1023 += E1000_READ_REG(&adapter->hw, PRC1023);
	adapter->stats.prc1522 += E1000_READ_REG(&adapter->hw, PRC1522);
	adapter->stats.gprc += E1000_READ_REG(&adapter->hw, GPRC);
	adapter->stats.bprc += E1000_READ_REG(&adapter->hw, BPRC);
	adapter->stats.mprc += E1000_READ_REG(&adapter->hw, MPRC);
	adapter->stats.gptc += E1000_READ_REG(&adapter->hw, GPTC);

	/* For the 64-bit byte counters the low dword must be read first. */
	/* Both registers clear on the read of the high dword */

	adapter->stats.gorcl += E1000_READ_REG(&adapter->hw, GORCL); 
	adapter->stats.gorch += E1000_READ_REG(&adapter->hw, GORCH);
	adapter->stats.gotcl += E1000_READ_REG(&adapter->hw, GOTCL);
	adapter->stats.gotch += E1000_READ_REG(&adapter->hw, GOTCH);

	adapter->stats.rnbc += E1000_READ_REG(&adapter->hw, RNBC);
	adapter->stats.ruc += E1000_READ_REG(&adapter->hw, RUC);
	adapter->stats.rfc += E1000_READ_REG(&adapter->hw, RFC);
	adapter->stats.roc += E1000_READ_REG(&adapter->hw, ROC);
	adapter->stats.rjc += E1000_READ_REG(&adapter->hw, RJC);

	adapter->stats.torl += E1000_READ_REG(&adapter->hw, TORL);
	adapter->stats.torh += E1000_READ_REG(&adapter->hw, TORH);
	adapter->stats.totl += E1000_READ_REG(&adapter->hw, TOTL);
	adapter->stats.toth += E1000_READ_REG(&adapter->hw, TOTH);

	adapter->stats.tpr += E1000_READ_REG(&adapter->hw, TPR);
	adapter->stats.tpt += E1000_READ_REG(&adapter->hw, TPT);
	adapter->stats.ptc64 += E1000_READ_REG(&adapter->hw, PTC64);
	adapter->stats.ptc127 += E1000_READ_REG(&adapter->hw, PTC127);
	adapter->stats.ptc255 += E1000_READ_REG(&adapter->hw, PTC255);
	adapter->stats.ptc511 += E1000_READ_REG(&adapter->hw, PTC511);
	adapter->stats.ptc1023 += E1000_READ_REG(&adapter->hw, PTC1023);
	adapter->stats.ptc1522 += E1000_READ_REG(&adapter->hw, PTC1522);
	adapter->stats.mptc += E1000_READ_REG(&adapter->hw, MPTC);
	adapter->stats.bptc += E1000_READ_REG(&adapter->hw, BPTC);

	if (adapter->hw.mac_type >= em_82543) {
		adapter->stats.algnerrc += 
		E1000_READ_REG(&adapter->hw, ALGNERRC);
		adapter->stats.rxerrc += 
		E1000_READ_REG(&adapter->hw, RXERRC);
		adapter->stats.tncrs += 
		E1000_READ_REG(&adapter->hw, TNCRS);
		adapter->stats.cexterr += 
		E1000_READ_REG(&adapter->hw, CEXTERR);
		adapter->stats.tsctc += 
		E1000_READ_REG(&adapter->hw, TSCTC);
		adapter->stats.tsctfc += 
		E1000_READ_REG(&adapter->hw, TSCTFC);
	}
	ifp = &adapter->interface_data.ac_if;

	/* Fill out the OS statistics structure */
	ifp->if_ibytes = adapter->stats.gorcl;
	ifp->if_obytes = adapter->stats.gotcl;
	ifp->if_imcasts = adapter->stats.mprc;
	ifp->if_collisions = adapter->stats.colc;

	/* Rx Errors */
	ifp->if_ierrors =
	adapter->dropped_pkts +
	adapter->stats.rxerrc +
	adapter->stats.crcerrs +
	adapter->stats.algnerrc +
	adapter->stats.rlec + adapter->stats.rnbc + 
	adapter->stats.mpc + adapter->stats.cexterr;

	/* Tx Errors */
	ifp->if_oerrors = adapter->stats.ecol + adapter->stats.latecol;

}


/**********************************************************************
 *
 *  This routine is called only when DEBUG_DISPLAY_STATS is enabled.
 *  This routine provides a way to take a look at important statistics
 *  maintained by the driver and hardware.
 *
 **********************************************************************/
static void
em_print_debug_info(struct adapter *adapter)
{
#if DEBUG_DISPLAY_STATS
	int unit = adapter->unit;
	uint8_t *hw_addr = adapter->hw.hw_addr;

	dprintf("ipro1000/%d: Adapter hardware address = %p \n", unit, hw_addr);
	dprintf("ipro1000/%d:tx_int_delay = %ld, tx_abs_int_delay = %ld\n", unit, 
	       E1000_READ_REG(&adapter->hw, TIDV),
	       E1000_READ_REG(&adapter->hw, TADV));
	dprintf("ipro1000/%d:rx_int_delay = %ld, rx_abs_int_delay = %ld\n", unit, 
	       E1000_READ_REG(&adapter->hw, RDTR),
	       E1000_READ_REG(&adapter->hw, RADV));
	dprintf("ipro1000/%d: Packets not Avail = %ld\n", unit, 
	       adapter->no_pkts_avail);
	dprintf("ipro1000/%d: CleanTxInterrupts = %ld\n", unit, 
	       adapter->clean_tx_interrupts);
	snooze(10000); // give syslog reader some time to catchup
	dprintf("ipro1000/%d: fifo workaround = %Ld, fifo_reset = %Ld\n", unit, 
	       (long long)adapter->tx_fifo_wrk, 
	       (long long)adapter->tx_fifo_reset);
	dprintf("ipro1000/%d: hw tdh = %ld, hw tdt = %ld\n", unit,
	       E1000_READ_REG(&adapter->hw, TDH), 
	       E1000_READ_REG(&adapter->hw, TDT));
	dprintf("ipro1000/%d: Num Tx descriptors avail = %d\n", unit,
	       adapter->num_tx_desc_avail);
	dprintf("ipro1000/%d: Tx Descriptors not avail1 = %ld\n", unit, 
	       adapter->no_tx_desc_avail1);
	dprintf("ipro1000/%d: Tx Descriptors not avail2 = %ld\n", unit, 
	       adapter->no_tx_desc_avail2);
	dprintf("ipro1000/%d: Std mbuf failed = %ld\n", unit, 
	       adapter->mbuf_alloc_failed);
	dprintf("ipro1000/%d: Std mbuf cluster failed = %ld\n", unit, 
	       adapter->mbuf_cluster_failed);
	dprintf("ipro1000/%d: Driver dropped packets = %ld\n", unit, 
	       adapter->dropped_pkts);
	snooze(10000); // give syslog reader some time to catchup
#endif
}

static void
em_print_hw_stats(struct adapter *adapter)
{
#if DEBUG_DISPLAY_STATS
	int unit = adapter->unit;

	dprintf("ipro1000/%d: Excessive collisions = %Ld\n", unit,
	       (long long)adapter->stats.ecol);
	dprintf("ipro1000/%d: Symbol errors = %Ld\n", unit, 
	       (long long)adapter->stats.symerrs);
	dprintf("ipro1000/%d: Sequence errors = %Ld\n", unit, 
	       (long long)adapter->stats.sec);
	dprintf("ipro1000/%d: Defer count = %Ld\n", unit, 
	       (long long)adapter->stats.dc);
	snooze(10000); // give syslog reader some time to catchup

	dprintf("ipro1000/%d: Missed Packets = %Ld\n", unit, 
	       (long long)adapter->stats.mpc);
	dprintf("ipro1000/%d: Receive No Buffers = %Ld\n", unit, 
	       (long long)adapter->stats.rnbc);
	dprintf("ipro1000/%d: Receive length errors = %Ld\n", unit, 
	       (long long)adapter->stats.rlec);
	dprintf("ipro1000/%d: Receive errors = %Ld\n", unit, 
	       (long long)adapter->stats.rxerrc);
	dprintf("ipro1000/%d: Crc errors = %Ld\n", unit, 
	       (long long)adapter->stats.crcerrs);
	dprintf("ipro1000/%d: Alignment errors = %Ld\n", unit, 
	       (long long)adapter->stats.algnerrc);
	dprintf("ipro1000/%d: Carrier extension errors = %Ld\n", unit,
	       (long long)adapter->stats.cexterr);
	snooze(10000); // give syslog reader some time to catchup

	dprintf("ipro1000/%d: XON Rcvd = %Ld\n", unit, 
	       (long long)adapter->stats.xonrxc);
	dprintf("ipro1000/%d: XON Xmtd = %Ld\n", unit, 
	       (long long)adapter->stats.xontxc);
	dprintf("ipro1000/%d: XOFF Rcvd = %Ld\n", unit, 
	       (long long)adapter->stats.xoffrxc);
	dprintf("ipro1000/%d: XOFF Xmtd = %Ld\n", unit, 
	       (long long)adapter->stats.xofftxc);
	snooze(10000); // give syslog reader some time to catchup

	dprintf("ipro1000/%d: Good Packets Rcvd = %Ld\n", unit,
	       (long long)adapter->stats.gprc);
	dprintf("ipro1000/%d: Good Packets Xmtd = %Ld\n", unit,
	       (long long)adapter->stats.gptc);
#endif
}

#if 0

static int
em_sysctl_debug_info(SYSCTL_HANDLER_ARGS)
{
	int error;
	int result;
	struct adapter *adapter;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	
	if (error || !req->newptr)
		return (error);

	if (result == 1) {
		adapter = (struct adapter *)arg1;
		em_print_debug_info(adapter);
	}

	return error;
}


static int
em_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	int error;
	int result;
	struct adapter *adapter;
	
	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	
	if (error || !req->newptr)
		return (error);

	if (result == 1) {
		adapter = (struct adapter *)arg1;
		em_print_hw_stats(adapter);
	}

	return error;
}

static int
em_sysctl_int_delay(SYSCTL_HANDLER_ARGS)
{
        struct em_int_delay_info *info;
        struct adapter *adapter;
        u_int32_t regval;
        int error;
        int usecs;
        int ticks;
        int s;

        info = (struct em_int_delay_info *)arg1;
        adapter = info->adapter;
        usecs = info->value;
        error = sysctl_handle_int(oidp, &usecs, 0, req);
        if (error != 0 || req->newptr == NULL)
                return error;
        if (usecs < 0 || usecs > E1000_TICKS_TO_USECS(65535))
                return EINVAL;
        info->value = usecs;
        ticks = E1000_USECS_TO_TICKS(usecs);

        s = splimp();
        regval = E1000_READ_OFFSET(&adapter->hw, info->offset);
        regval = (regval & ~0xffff) | (ticks & 0xffff);
        /* Handle a few special cases. */
        switch (info->offset) {
        case E1000_RDTR:
        case E1000_82542_RDTR:
                regval |= E1000_RDT_FPDB;
                break;
        case E1000_TIDV:
        case E1000_82542_TIDV:
                if (ticks == 0) {
                        adapter->txd_cmd &= ~E1000_TXD_CMD_IDE;
                        /* Don't write 0 into the TIDV register. */
                        regval++;
                } else
                        adapter->txd_cmd |= E1000_TXD_CMD_IDE;
                break;
        }
        E1000_WRITE_OFFSET(&adapter->hw, info->offset, regval);
        splx(s);
        return 0;
}

#endif

static void
em_add_int_delay_sysctl(struct adapter *adapter, const char *name,
    const char *description, struct em_int_delay_info *info,
    int offset, int value)
{
        info->adapter = adapter;
        info->offset = offset;
        info->value = value;
        SYSCTL_ADD_PROC(&adapter->sysctl_ctx,
            SYSCTL_CHILDREN(adapter->sysctl_tree),
            OID_AUTO, name, CTLTYPE_INT|CTLFLAG_RW,
            info, 0, em_sysctl_int_delay, "I", description);
}


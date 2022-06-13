// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx Axi Ethernet device driver
 *
 * Copyright (c) 2008 Nissin Systems Co., Ltd.,  Yoshio Kashiwagi
 * Copyright (c) 2005-2008 DLA Systems,  David H. Lynch Jr. <dhlii@dlasys.net>
 * Copyright (c) 2008-2009 Secret Lab Technologies Ltd.
 * Copyright (c) 2010 - 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2010 - 2011 PetaLogix
 * Copyright (c) 2019 SED Systems, a division of Calian Ltd.
 * Copyright (c) 2010 - 2012 Xilinx, Inc. All rights reserved.
 *
 * This is a driver for the Xilinx Axi Ethernet which is used in the Virtex6
 * and Spartan6.
 *
 * TODO:
 *  - Add Axi Fifo support.
 *  - Factor out Axi DMA code into separate driver.
 *  - Test and fix basic multicast filtering.
 *  - Add support for extended multicast filtering.
 *  - Test basic VLAN support.
 *  - Add support for extended VLAN support.
 */

#include <linux/clk.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/iopoll.h>
#include <linux/ptp_classify.h>
#include <linux/net_tstamp.h>
#include <linux/random.h>
#include <net/sock.h>
#include <linux/xilinx_phy.h>
#include <linux/clk.h>

#include "xilinx_axienet_tsn.h"

/* Descriptors defines for Tx and Rx DMA */
#define TX_BD_NUM_DEFAULT		64
#define RX_BD_NUM_DEFAULT		128
#define TX_BD_NUM_MAX			4096
#define RX_BD_NUM_MAX			4096

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME		"xaxienet"
#define DRIVER_DESCRIPTION	"Xilinx Axi Ethernet driver"
#define DRIVER_VERSION		"1.00a"

#define AXIENET_REGS_N		40
#define AXIENET_TS_HEADER_LEN	8
#define XXVENET_TS_HEADER_LEN	4
#define MRMAC_TS_HEADER_LEN		16
#define MRMAC_TS_HEADER_WORDS   (MRMAC_TS_HEADER_LEN / 4)
#define NS_PER_SEC              1000000000ULL /* Nanoseconds per second */

#define MRMAC_RESET_DELAY	1 /* Delay in msecs*/

/* IEEE1588 Message Type field values  */
#define PTP_TYPE_SYNC		0
#define PTP_TYPE_PDELAY_REQ	2
#define PTP_TYPE_PDELAY_RESP	3
#define PTP_TYPE_OFFSET		42
/* SW flags used to convey message type for command FIFO handling */
#define MSG_TYPE_SHIFT			4
#define MSG_TYPE_SYNC_FLAG		((PTP_TYPE_SYNC + 1) << MSG_TYPE_SHIFT)
#define MSG_TYPE_PDELAY_RESP_FLAG	((PTP_TYPE_PDELAY_RESP + 1) << \
									 MSG_TYPE_SHIFT)

#define FILTER_SELECT		0x100	   /* Filter select */
#define ETHERTYPE_FILTER_IPV4	0x00000008 /* Ethertype field 0x08 for IPv4 packets */
#define ETHERTYPE_FILTER_PTP    0x0000F788 /* Ethertype field 0x88F7 for PTP packets */
#define PROTO_FILTER_UDP	0x11000000 /* protocol field 0x11 for UDP packets */
#define PTP_UDP_PORT		0x00003F01 /* dest port field 0x013F for PTP over UDP packes */
#define PTP_VERSION		0x02000000 /* PTPv2 */

#define DESTMAC_FILTER_ENABLE_MASK_MSB		0xFFFFFFFF /* Enable filtering for bytes 0-3 in a
							    * packet corresponding to 4 Most
							    * significant bytes of
							    * Destination MAC address
							    */
#define DESTMAC_FILTER_ENABLE_MASK_LSB		0xFF000000 /* Enable filtering for bytes
							    * 4-5 in a packet
							    * corresponding to 2 Least
							    * significant bytes of
							    * Destination MAC address
							    */
#define PROTO_FILTER_DISABLE_MASK		0x0 /* Disable protocol based filtering */
#define PORT_NUM_FILTER_DISABLE_MASK		0x0 /* Disable port number based filtering */
#define VERSION_FILTER_DISABLE_MASK		0x0 /* Disable filtering based on PTP version */

#define DESTMAC_FILTER_DISABLE_MASK_MSB		0 /* Disable Dest MAC address filtering(4 MSB'S) */
#define DESTMAC_FILTER_DISABLE_MASK_LSB		0 /* Disable Dest MAC address filtering(2 LSB's) */
#define PROTO_FILTER_ENABLE_MASK		0xFF000000 /* Enable protocol based filtering */
#define PORT_NUM_FILTER_ENABLE_MASK		0x0000FFFF /* Enable port number based filtering */
#define VERSION_FILTER_ENABLE_MASK		0xFF000000 /* Enable PTP version based filtering */

#ifdef CONFIG_XILINX_TSN_PTP
int axienet_phc_index = -1;
EXPORT_SYMBOL(axienet_phc_index);
#endif

/* Option table for setting up Axi Ethernet hardware options */
static struct axienet_option axienet_options[] = {
	/* Turn on jumbo packet support for both Rx and Tx */
	{
		.opt = XAE_OPTION_JUMBO,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_JUM_MASK,
	}, {
		.opt = XAE_OPTION_JUMBO,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_JUM_MASK,
	}, { /* Turn on VLAN packet support for both Rx and Tx */
		.opt = XAE_OPTION_VLAN,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_VLAN_MASK,
	}, {
		.opt = XAE_OPTION_VLAN,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_VLAN_MASK,
	}, { /* Turn on FCS stripping on receive packets */
		.opt = XAE_OPTION_FCS_STRIP,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_FCS_MASK,
	}, { /* Turn on FCS insertion on transmit packets */
		.opt = XAE_OPTION_FCS_INSERT,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_FCS_MASK,
	}, { /* Turn off length/type field checking on receive packets */
		.opt = XAE_OPTION_LENTYPE_ERR,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_LT_DIS_MASK,
	}, { /* Turn on Rx flow control */
		.opt = XAE_OPTION_FLOW_CONTROL,
		.reg = XAE_FCC_OFFSET,
		.m_or = XAE_FCC_FCRX_MASK,
	}, { /* Turn on Tx flow control */
		.opt = XAE_OPTION_FLOW_CONTROL,
		.reg = XAE_FCC_OFFSET,
		.m_or = XAE_FCC_FCTX_MASK,
	}, { /* Turn on promiscuous frame filtering */
		.opt = XAE_OPTION_PROMISC,
		.reg = XAE_FMC_OFFSET,
		.m_or = XAE_FMC_PM_MASK,
	}, { /* Enable transmitter */
		.opt = XAE_OPTION_TXEN,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_TX_MASK,
	}, { /* Enable receiver */
		.opt = XAE_OPTION_RXEN,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_RX_MASK,
	},
	{}
};

struct axienet_ethtools_stat {
	const char *name;
};

static struct axienet_ethtools_stat axienet_get_ethtools_strings_stats[] = {
	{ "tx_packets" },
	{ "rx_packets" },
	{ "tx_bytes" },
	{ "rx_bytes" },
	{ "tx_errors" },
	{ "rx_errors" },
};

/**
 * axienet_dma_bd_release_tsn - Release buffer descriptor rings
 * @ndev:	Pointer to the net_device structure
 *
 * This function is used to release the descriptors allocated in
 * axienet_dma_bd_init. axienet_dma_bd_release is called when Axi Ethernet
 * driver stop api is called.
 */
void axienet_dma_bd_release_tsn(struct net_device *ndev)
{
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

	for_each_tx_dma_queue(lp, i) {
		axienet_mcdma_tx_bd_free_tsn(ndev, lp->dq[i]);
	}
	for_each_rx_dma_queue(lp, i) {
		axienet_mcdma_rx_bd_free_tsn(ndev, lp->dq[i]);
	}
}

/**
 * axienet_set_mac_address_tsn - Write the MAC address
 * @ndev:	Pointer to the net_device structure
 * @address:	6 byte Address to be written as MAC address
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It writes to the UAW0 and UAW1 registers of the core.
 */
void axienet_set_mac_address_tsn(struct net_device *ndev,
				 const void *address)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (address)
		ether_addr_copy(ndev->dev_addr, address);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	if (lp->axienet_config->mactype != XAXIENET_1G &&
	    lp->axienet_config->mactype != XAXIENET_2_5G)
		return;

	/* Set up unicast MAC address filter set its mac address */
	axienet_iow(lp, XAE_UAW0_OFFSET,
		    (ndev->dev_addr[0]) |
		    (ndev->dev_addr[1] << 8) |
		    (ndev->dev_addr[2] << 16) |
		    (ndev->dev_addr[3] << 24));
	axienet_iow(lp, XAE_UAW1_OFFSET,
		    (((axienet_ior(lp, XAE_UAW1_OFFSET)) &
		      ~XAE_UAW1_UNICASTADDR_MASK) |
		     (ndev->dev_addr[4] |
		     (ndev->dev_addr[5] << 8))));
}

/**
 * netdev_set_mac_address - Write the MAC address (from outside the driver)
 * @ndev:	Pointer to the net_device structure
 * @p:		6 byte Address to be written as MAC address
 *
 * Return: 0 for all conditions. Presently, there is no failure case.
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It calls the core specific axienet_set_mac_address_tsn. This is the
 * function that goes into net_device_ops structure entry ndo_set_mac_address.
 */
static int netdev_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;

	axienet_set_mac_address_tsn(ndev, addr->sa_data);
	return 0;
}

/**
 * axienet_set_multicast_list_tsn - Prepare the multicast table
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to initialize the multicast table during
 * initialization. The Axi Ethernet basic multicast support has a four-entry
 * multicast table which is initialized here. Additionally this function
 * goes into the net_device_ops structure entry ndo_set_multicast_list. This
 * means whenever the multicast table entries need to be updated this
 * function gets called.
 */
void axienet_set_multicast_list_tsn(struct net_device *ndev)
{
	int i;
	u32 reg, af0reg, af1reg;
	struct axienet_local *lp = netdev_priv(ndev);

	if (lp->axienet_config->mactype != XAXIENET_1G || lp->eth_hasnobuf)
		return;

	if (ndev->flags & (IFF_ALLMULTI | IFF_PROMISC) ||
	    netdev_mc_count(ndev) > XAE_MULTICAST_CAM_TABLE_NUM) {
		/* We must make the kernel realize we had to move into
		 * promiscuous mode. If it was a promiscuous mode request
		 * the flag is already set. If not we set it.
		 */
		ndev->flags |= IFF_PROMISC;
		reg = axienet_ior(lp, XAE_FMC_OFFSET);
		reg |= XAE_FMC_PM_MASK;
		axienet_iow(lp, XAE_FMC_OFFSET, reg);
		dev_info(&ndev->dev, "Promiscuous mode enabled.\n");
	} else if (!netdev_mc_empty(ndev)) {
		struct netdev_hw_addr *ha;

		i = 0;
		netdev_for_each_mc_addr(ha, ndev) {
			if (i >= XAE_MULTICAST_CAM_TABLE_NUM)
				break;

			af0reg = (ha->addr[0]);
			af0reg |= (ha->addr[1] << 8);
			af0reg |= (ha->addr[2] << 16);
			af0reg |= (ha->addr[3] << 24);

			af1reg = (ha->addr[4]);
			af1reg |= (ha->addr[5] << 8);

			reg = axienet_ior(lp, XAE_FMC_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMC_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, af0reg);
			axienet_iow(lp, XAE_AF1_OFFSET, af1reg);
			i++;
		}
	} else {
		reg = axienet_ior(lp, XAE_FMC_OFFSET);
		reg &= ~XAE_FMC_PM_MASK;

		axienet_iow(lp, XAE_FMC_OFFSET, reg);

		for (i = 0; i < XAE_MULTICAST_CAM_TABLE_NUM; i++) {
			reg = axienet_ior(lp, XAE_FMC_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMC_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, 0);
			axienet_iow(lp, XAE_AF1_OFFSET, 0);
		}

		dev_info(&ndev->dev, "Promiscuous mode disabled.\n");
	}
}

/**
 * axienet_setoptions_tsn - Set an Axi Ethernet option
 * @ndev:	Pointer to the net_device structure
 * @options:	Option to be enabled/disabled
 *
 * The Axi Ethernet core has multiple features which can be selectively turned
 * on or off. The typical options could be jumbo frame option, basic VLAN
 * option, promiscuous mode option etc. This function is used to set or clear
 * these options in the Axi Ethernet hardware. This is done through
 * axienet_option structure .
 */
void axienet_setoptions_tsn(struct net_device *ndev, u32 options)
{
	int reg;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_option *tp = &axienet_options[0];

	while (tp->opt) {
		reg = ((axienet_ior(lp, tp->reg)) & ~(tp->m_or));
		if (options & tp->opt)
			reg |= tp->m_or;
		axienet_iow(lp, tp->reg, reg);
		tp++;
	}

	lp->options |= options;
}

void __axienet_device_reset_tsn(struct axienet_dma_q *q)
{
	u32 timeout;
	/* Reset Axi DMA. This would reset Axi Ethernet core as well. The reset
	 * process of Axi DMA takes a while to complete as all pending
	 * commands/transfers will be flushed or completed during this
	 * reset process.
	 * Note that even though both TX and RX have their own reset register,
	 * they both reset the entire DMA core, so only one needs to be used.
	 */
	axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, XAXIDMA_CR_RESET_MASK);
	timeout = DELAY_OF_ONE_MILLISEC;
	while (axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET) &
				XAXIDMA_CR_RESET_MASK) {
		udelay(1);
		if (--timeout == 0) {
			netdev_err(q->lp->ndev, "%s: DMA reset timeout!\n",
				   __func__);
			break;
		}
	}
}

/**
 * axienet_adjust_link_tsn - Adjust the PHY link speed/duplex.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to change the speed and duplex setting after
 * auto negotiation is done by the PHY. This is the function that gets
 * registered with the PHY interface through the "of_phy_connect" call.
 */
void axienet_adjust_link_tsn(struct net_device *ndev)
{
	u32 emmc_reg;
	u32 link_state;
	u32 setspeed = 1;
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;

	link_state = phy->speed | (phy->duplex << 1) | phy->link;
	if (lp->last_link != link_state) {
		if (phy->speed == SPEED_10 || phy->speed == SPEED_100) {
			if (lp->phy_mode == PHY_INTERFACE_MODE_1000BASEX)
				setspeed = 0;
		} else {
			if (phy->speed == SPEED_1000 &&
			    lp->phy_mode == PHY_INTERFACE_MODE_MII)
				setspeed = 0;
		}

		if (setspeed == 1) {
			emmc_reg = axienet_ior(lp, XAE_EMMC_OFFSET);
			emmc_reg &= ~XAE_EMMC_LINKSPEED_MASK;

			switch (phy->speed) {
			case SPEED_2500:
				emmc_reg |= XAE_EMMC_LINKSPD_2500;
				break;
			case SPEED_1000:
				emmc_reg |= XAE_EMMC_LINKSPD_1000;
				break;
			case SPEED_100:
				emmc_reg |= XAE_EMMC_LINKSPD_100;
				break;
			case SPEED_10:
				emmc_reg |= XAE_EMMC_LINKSPD_10;
				break;
			default:
				dev_err(&ndev->dev, "Speed other than 10, 100 ");
				dev_err(&ndev->dev, "or 1Gbps is not supported\n");
				break;
			}

			axienet_iow(lp, XAE_EMMC_OFFSET, emmc_reg);
			phy_print_status(phy);
		} else {
			netdev_err(ndev,
				   "Error setting Axi Ethernet mac speed\n");
		}

		lp->last_link = link_state;
	}
}

/**
 * axienet_start_xmit_done_tsn - Invoked once a transmit is completed by the
 * Axi DMA Tx channel.
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to DMA queue structure
 *
 * This function is invoked from the Axi DMA Tx isr to notify the completion
 * of transmit operation. It clears fields in the corresponding Tx BDs and
 * unmaps the corresponding buffer so that CPU can regain ownership of the
 * buffer. It finally invokes "netif_wake_queue" to restart transmission if
 * required.
 */
void axienet_start_xmit_done_tsn(struct net_device *ndev,
				 struct axienet_dma_q *q)
{
	u32 size = 0;
	u32 packets = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct aximcdma_bd *cur_p;
	unsigned int status = 0;

	cur_p = &q->txq_bd_v[q->tx_bd_ci];
	status = cur_p->sband_stats;
	while (status & XAXIDMA_BD_STS_COMPLETE_MASK) {
		if (cur_p->tx_desc_mapping == DESC_DMA_MAP_PAGE)
			dma_unmap_page(ndev->dev.parent, cur_p->phys,
				       cur_p->cntrl &
				       XAXIDMA_BD_CTRL_LENGTH_MASK,
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(ndev->dev.parent, cur_p->phys,
					 cur_p->cntrl &
					 XAXIDMA_BD_CTRL_LENGTH_MASK,
					 DMA_TO_DEVICE);
		if (cur_p->tx_skb)
			dev_kfree_skb_irq((struct sk_buff *)cur_p->tx_skb);
		/*cur_p->phys = 0;*/
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app4 = 0;
		cur_p->status = 0;
		cur_p->tx_skb = 0;
		cur_p->sband_stats = 0;

		size += status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		packets++;

		if (++q->tx_bd_ci >= lp->tx_bd_num)
			q->tx_bd_ci = 0;
		cur_p = &q->txq_bd_v[q->tx_bd_ci];
		status = cur_p->sband_stats;
	}

	ndev->stats.tx_packets += packets;
	ndev->stats.tx_bytes += size;
	q->tx_packets += packets;
	q->tx_bytes += size;

	/* Matches barrier in axienet_start_xmit */
	smp_mb();

	/* Fixme: With the existing multiqueue implementation
	 * in the driver it is difficult to get the exact queue info.
	 * We should wake only the particular queue
	 * instead of waking all ndev queues.
	 */
	netif_tx_wake_all_queues(ndev);
}

/**
 * axienet_check_tx_bd_space - Checks if a BD/group of BDs are currently busy
 * @q:		Pointer to DMA queue structure
 * @num_frag:	The number of BDs to check for
 *
 * Return: 0, on success
 *	    NETDEV_TX_BUSY, if any of the descriptors are not free
 *
 * This function is invoked before BDs are allocated and transmission starts.
 * This function returns 0 if a BD or group of BDs can be allocated for
 * transmission. If the BD or any of the BDs are not free the function
 * returns a busy status. This is invoked from axienet_start_xmit.
 */
static inline int axienet_check_tx_bd_space(struct axienet_dma_q *q,
					    int num_frag)
{
	struct axienet_local *lp = q->lp;
	struct aximcdma_bd *cur_p;

	if (CIRC_SPACE(q->tx_bd_tail, q->tx_bd_ci, lp->tx_bd_num) < (num_frag + 1))
		return NETDEV_TX_BUSY;

	cur_p = &q->txq_bd_v[(q->tx_bd_tail + num_frag) % lp->tx_bd_num];
	if (cur_p->sband_stats & XMCDMA_BD_STS_ALL_MASK)
		return NETDEV_TX_BUSY;
	return 0;
}

int axienet_queue_xmit_tsn(struct sk_buff *skb,
			   struct net_device *ndev, u16 map)
{
	u32 ii;
	u32 num_frag;
	u32 csum_start_off;
	u32 csum_index_off;
	dma_addr_t tail_p;
	struct axienet_local *lp = netdev_priv(ndev);
	struct aximcdma_bd *cur_p;
	unsigned long flags;
	struct axienet_dma_q *q;

	num_frag = skb_shinfo(skb)->nr_frags;

	q = lp->dq[map];

	cur_p = &q->txq_bd_v[q->tx_bd_tail];
	spin_lock_irqsave(&q->tx_lock, flags);
	if (axienet_check_tx_bd_space(q, num_frag)) {
		if (netif_queue_stopped(ndev)) {
			spin_unlock_irqrestore(&q->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}

		netif_stop_queue(ndev);

		/* Matches barrier in axienet_start_xmit_done_tsn */
		smp_mb();

		/* Space might have just been freed - check again */
		if (axienet_check_tx_bd_space(q, num_frag)) {
			spin_unlock_irqrestore(&q->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}

		netif_wake_queue(ndev);
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL && !lp->eth_hasnobuf &&
	    lp->axienet_config->mactype == XAXIENET_1G) {
		if (lp->features & XAE_FEATURE_FULL_TX_CSUM) {
			/* Tx Full Checksum Offload Enabled */
			cur_p->app0 |= 2;
		} else if (lp->features & XAE_FEATURE_PARTIAL_RX_CSUM) {
			csum_start_off = skb_transport_offset(skb);
			csum_index_off = csum_start_off + skb->csum_offset;
			/* Tx Partial Checksum Offload Enabled */
			cur_p->app0 |= 1;
			cur_p->app1 = (csum_start_off << 16) | csum_index_off;
		}
	} else if (skb->ip_summed == CHECKSUM_UNNECESSARY &&
		   !lp->eth_hasnobuf &&
		   (lp->axienet_config->mactype == XAXIENET_1G)) {
		cur_p->app0 |= 2; /* Tx Full Checksum Offload Enabled */
	}

	cur_p->cntrl = (skb_headlen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK);

	if (!q->eth_hasdre &&
	    (((phys_addr_t)skb->data & 0x3) || num_frag > 0)) {
		skb_copy_and_csum_dev(skb, q->tx_buf[q->tx_bd_tail]);

		cur_p->phys = q->tx_bufs_dma +
			      (q->tx_buf[q->tx_bd_tail] - q->tx_bufs);

		cur_p->cntrl = skb_pagelen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK;
		goto out;
	} else {
		cur_p->phys = dma_map_single(ndev->dev.parent, skb->data,
					     skb_headlen(skb), DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(ndev->dev.parent, cur_p->phys))) {
			cur_p->phys = 0;
			spin_unlock_irqrestore(&q->tx_lock, flags);
			dev_err(&ndev->dev, "TX buffer map failed\n");
			return NETDEV_TX_BUSY;
		}
	}
	cur_p->tx_desc_mapping = DESC_DMA_MAP_SINGLE;

	for (ii = 0; ii < num_frag; ii++) {
		u32 len;
		skb_frag_t *frag;

		if (++q->tx_bd_tail >= lp->tx_bd_num)
			q->tx_bd_tail = 0;

		cur_p = &q->txq_bd_v[q->tx_bd_tail];
		frag = &skb_shinfo(skb)->frags[ii];
		len = skb_frag_size(frag);
		cur_p->phys = skb_frag_dma_map(ndev->dev.parent, frag, 0, len,
					       DMA_TO_DEVICE);
		cur_p->cntrl = len;
		cur_p->tx_desc_mapping = DESC_DMA_MAP_PAGE;
	}

out:
	cur_p->cntrl |= XMCDMA_BD_CTRL_TXEOF_MASK;
	tail_p = q->tx_bd_p + sizeof(*q->txq_bd_v) * q->tx_bd_tail;
	cur_p->tx_skb = (phys_addr_t)skb;
	cur_p->tx_skb = (phys_addr_t)skb;

	tail_p = q->tx_bd_p + sizeof(*q->tx_bd_v) * q->tx_bd_tail;
	/* Ensure BD write before starting transfer */
	wmb();

	/* Start the transfer */
	axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id),
			  tail_p);
	if (++q->tx_bd_tail >= lp->tx_bd_num)
		q->tx_bd_tail = 0;

	spin_unlock_irqrestore(&q->tx_lock, flags);

	return NETDEV_TX_OK;
}

/**
 * axienet_recv - Is called from Axi DMA Rx Isr to complete the received
 *		  BD processing.
 * @ndev:	Pointer to net_device structure.
 * @budget:	NAPI budget
 * @q:		Pointer to axienet DMA queue structure
 *
 * This function is invoked from the Axi DMA Rx isr(poll) to process the Rx BDs
 * It does minimal processing and invokes "netif_receive_skb" to complete
 * further processing.
 * Return: Number of BD's processed.
 */
static int axienet_recv(struct net_device *ndev, int budget,
			struct axienet_dma_q *q)
{
	u32 length;
	u32 csumstatus;
	u32 size = 0;
	u32 packets = 0;
	dma_addr_t tail_p = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct sk_buff *skb, *new_skb;
	u32 sband_status = 0;
	struct net_device *temp_ndev = NULL;
	struct aximcdma_bd *cur_p;
	unsigned int numbdfree = 0;

	/* Get relevat BD status value */
	rmb();
	cur_p = &q->rxq_bd_v[q->rx_bd_ci];
	sband_status = cur_p->sband_stats;

	while ((numbdfree < budget) &&
	       (cur_p->status & XAXIDMA_BD_STS_COMPLETE_MASK)) {
		new_skb = netdev_alloc_skb(ndev, lp->max_frm_size);
		if (!new_skb)
			break;
		tail_p = q->rx_bd_p + sizeof(*q->rxq_bd_v) * q->rx_bd_ci;

		dma_unmap_single(ndev->dev.parent, cur_p->phys,
				 lp->max_frm_size,
				 DMA_FROM_DEVICE);

		skb = (struct sk_buff *)(cur_p->sw_id_offset);

		if (lp->eth_hasnobuf ||
		    lp->axienet_config->mactype != XAXIENET_1G)
			length = cur_p->status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		else
			length = cur_p->app4 & 0x0000FFFF;

		skb_put(skb, length);
		skb->protocol = eth_type_trans(skb, ndev);
		/*skb_checksum_none_assert(skb);*/
		skb->ip_summed = CHECKSUM_NONE;

		/* if we're doing Rx csum offload, set it up */
		if (lp->features & XAE_FEATURE_FULL_RX_CSUM &&
		    lp->axienet_config->mactype == XAXIENET_1G &&
		    !lp->eth_hasnobuf) {
			csumstatus = (cur_p->app2 &
				      XAE_FULL_CSUM_STATUS_MASK) >> 3;
			if (csumstatus == XAE_IP_TCP_CSUM_VALIDATED ||
			    csumstatus == XAE_IP_UDP_CSUM_VALIDATED) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			}
		} else if ((lp->features & XAE_FEATURE_PARTIAL_RX_CSUM) != 0 &&
			   skb->protocol == htons(ETH_P_IP) &&
			   skb->len > 64 && !lp->eth_hasnobuf &&
			   (lp->axienet_config->mactype == XAXIENET_1G)) {
			skb->csum = be32_to_cpu(cur_p->app3 & 0xFFFF);
			skb->ip_summed = CHECKSUM_COMPLETE;
		}
		if (unlikely(q->flags & MCDMA_MGMT_CHAN)) {
			/* received packet on mgmt channel */
			if ((sband_status & XMCDMA_BD_SD_STS_ALL_MASK)
			    == XMCDMA_BD_SD_STS_TUSER_MAC_1) {
				temp_ndev = lp->slaves[0];
			} else if ((sband_status & XMCDMA_BD_SD_STS_ALL_MASK)
				 == XMCDMA_BD_SD_STS_TUSER_MAC_2) {
				temp_ndev = lp->slaves[1];
			} else if ((sband_status & XMCDMA_BD_SD_STS_ALL_MASK)
				 == XMCDMA_BD_SD_STS_TUSER_EP) {
				temp_ndev = lp->ndev;
			} else if (lp->ex_ep && ((sband_status &
				XMCDMA_BD_SD_STS_ALL_MASK) ==
				XMCDMA_BD_SD_STS_TUSER_EX_EP)) {
				temp_ndev = lp->ex_ep;
			}

			/* send to one of the front panel port */
			if (temp_ndev && netif_running(temp_ndev)) {
				skb->dev = temp_ndev;
				netif_receive_skb(skb);
			} else {
				kfree(skb); /* dont send up the stack */
			}
		} else if (unlikely(q->flags & MCDMA_EP_EX_CHAN)) {
			temp_ndev = lp->ex_ep;
			if (temp_ndev && netif_running(temp_ndev)) {
				skb->dev = temp_ndev;
				netif_receive_skb(skb);
			} else {
				kfree(skb); /* dont send up the stack */
			}
		} else {
			netif_receive_skb(skb); /* send on normal data path */
		}

		size += length;
		packets++;

		/* Ensure that the skb is completely updated
		 * prio to mapping the DMA
		 */
		wmb();

		cur_p->phys = dma_map_single(ndev->dev.parent, new_skb->data,
					     lp->max_frm_size,
					   DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(ndev->dev.parent, cur_p->phys))) {
			cur_p->phys = 0;
			dev_kfree_skb(new_skb);
			dev_err(lp->dev, "RX buffer map failed\n");
			break;
		}
		cur_p->cntrl = lp->max_frm_size;
		cur_p->status = 0;
		cur_p->sw_id_offset = (phys_addr_t)new_skb;

		if (++q->rx_bd_ci >= lp->rx_bd_num)
			q->rx_bd_ci = 0;

		/* Get relevat BD status value */
		rmb();
		cur_p = &q->rxq_bd_v[q->rx_bd_ci];
		numbdfree++;
	}

	ndev->stats.rx_packets += packets;
	ndev->stats.rx_bytes += size;
	q->rx_packets += packets;
	q->rx_bytes += size;

	if (tail_p) {
		axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id) +
				  q->rx_offset, tail_p);
	}

	return numbdfree;
}

/**
 * xaxienet_rx_poll_tsn - Poll routine for rx packets (NAPI)
 * @napi:	napi structure pointer
 * @quota:	Max number of rx packets to be processed.
 *
 * This is the poll routine for rx part.
 * It will process the packets maximux quota value.
 *
 * Return: number of packets received
 */
int xaxienet_rx_poll_tsn(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct axienet_local *lp = netdev_priv(ndev);
	int work_done = 0;
	unsigned int status, cr;

	int map = napi - lp->napi;

	struct axienet_dma_q *q = lp->dq[map];

	spin_lock(&q->rx_lock);
	status = axienet_dma_in32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
				  q->rx_offset);
	while ((status & (XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK)) &&
	       (work_done < quota)) {
		axienet_dma_out32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
				  q->rx_offset, status);
		if (status & XMCDMA_IRQ_ERR_MASK) {
			dev_err(lp->dev, "Rx error 0x%x\n\r", status);
			break;
		}
		work_done += axienet_recv(lp->ndev, quota - work_done, q);
		status = axienet_dma_in32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
					  q->rx_offset);
	}
	spin_unlock(&q->rx_lock);

	if (work_done < quota) {
		napi_complete(napi);
		/* Enable the interrupts again */
		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				      XMCDMA_RX_OFFSET);
		cr |= (XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK);
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				  XMCDMA_RX_OFFSET, cr);
	}

	return work_done;
}

/**
 * axienet_change_mtu - Driver change mtu routine.
 * @ndev:	Pointer to net_device structure
 * @new_mtu:	New mtu value to be applied
 *
 * Return: Always returns 0 (success).
 *
 * This is the change mtu driver routine. It checks if the Axi Ethernet
 * hardware supports jumbo frames before changing the mtu. This can be
 * called only when the device is not up.
 */
static int axienet_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev))
		return -EBUSY;

	if ((new_mtu + VLAN_ETH_HLEN +
		XAE_TRL_SIZE) > lp->rxmem)
		return -EINVAL;

	ndev->mtu = new_mtu;

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * axienet_poll_controller - Axi Ethernet poll mechanism.
 * @ndev:	Pointer to net_device structure
 *
 * This implements Rx/Tx ISR poll mechanisms. The interrupts are disabled prior
 * to polling the ISRs and are enabled back after the polling is done.
 */
static void axienet_poll_controller(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int i;

	for_each_tx_dma_queue(lp, i)
		disable_irq(lp->dq[i]->tx_irq);
	for_each_rx_dma_queue(lp, i)
		disable_irq(lp->dq[i]->rx_irq);

	for_each_rx_dma_queue(lp, i)
		axienet_mcdma_rx_irq_tsn(lp->dq[i]->rx_irq, ndev);
	for_each_tx_dma_queue(lp, i)
		axienet_mcdma_tx_irq_tsn(lp->dq[i]->tx_irq, ndev);
	for_each_tx_dma_queue(lp, i)
		enable_irq(lp->dq[i]->tx_irq);
	for_each_rx_dma_queue(lp, i)
		enable_irq(lp->dq[i]->rx_irq);
}
#endif

#if defined(CONFIG_XILINX_TSN_PTP)
/**
 *  axienet_set_timestamp_mode - sets up the hardware for the requested mode
 *  @lp: Pointer to axienet local structure
 *  @config: the hwtstamp configuration requested
 *
 * Return: 0 on success, Negative value on errors
 */
static int axienet_set_timestamp_mode(struct axienet_local *lp,
				      struct hwtstamp_config *config)
{
	u32 regval;

#ifdef CONFIG_XILINX_TSN_PTP
	/* reserved for future extensions */
	if (config->flags)
		return -EINVAL;

	if (config->tx_type < HWTSTAMP_TX_OFF ||
	    config->tx_type > HWTSTAMP_TX_ONESTEP_SYNC)
		return -ERANGE;

	lp->ptp_ts_type = config->tx_type;

	/* On RX always timestamp everything */
	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	default:
		config->rx_filter = lp->current_rx_filter;
	}
	return 0;

#endif

	/* reserved for future extensions */
	if (config->flags)
		return -EINVAL;

	/* Read the current value in the MAC TX CTRL register */
	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC)
		regval = axienet_ior(lp, XAE_TC_OFFSET);

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		regval &= ~XAE_TC_INBAND1588_MASK;
		break;
	case HWTSTAMP_TX_ON:
		config->tx_type = HWTSTAMP_TX_ON;
		regval |= XAE_TC_INBAND1588_MASK;
		if (lp->axienet_config->mactype == XAXIENET_MRMAC)
			axienet_iow(lp, MRMAC_CFG1588_OFFSET, 0x0);
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		config->tx_type = HWTSTAMP_TX_ONESTEP_SYNC;
		regval |= XAE_TC_INBAND1588_MASK;
		if (lp->axienet_config->mactype == XAXIENET_MRMAC)
			axienet_iow(lp, MRMAC_CFG1588_OFFSET, MRMAC_ONE_STEP_EN);
		break;
	case HWTSTAMP_TX_ONESTEP_P2P:
		if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
			config->tx_type = HWTSTAMP_TX_ONESTEP_P2P;
			axienet_iow(lp, MRMAC_CFG1588_OFFSET, MRMAC_ONE_STEP_EN);
		} else {
			return -ERANGE;
		}
		break;
	default:
		return -ERANGE;
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC)
		axienet_iow(lp, XAE_TC_OFFSET, regval);

	/* Read the current value in the MAC RX RCW1 register */
	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC)
		regval = axienet_ior(lp, XAE_RCW1_OFFSET);

	/* On RX always timestamp everything */
	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		regval &= ~XAE_RCW1_INBAND1588_MASK;
		break;
	default:
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		regval |= XAE_RCW1_INBAND1588_MASK;
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC)
		axienet_iow(lp, XAE_RCW1_OFFSET, regval);

	return 0;
}

static void change_filter_values_to_udp(struct axienet_local *lp)
{
	axienet_iow(lp, XAE_FMC_OFFSET, FILTER_SELECT);
	/**
	 * axienet_iow(lp, 0x70C, 0x0); values may not
	 * be written on to the specified address if this is not given
	 */
	axienet_iow(lp, XAE_FF_3_OFFSET, ETHERTYPE_FILTER_IPV4);
	axienet_iow(lp, XAE_FF_5_OFFSET, PROTO_FILTER_UDP);
	axienet_iow(lp, XAE_FF_9_OFFSET, PTP_UDP_PORT);
	axienet_iow(lp, XAE_FF_10_OFFSET, PTP_VERSION);

	axienet_iow(lp, XAE_AF0_MASK_OFFSET, DESTMAC_FILTER_DISABLE_MASK_MSB);
	axienet_iow(lp, XAE_AF1_MASK_OFFSET, DESTMAC_FILTER_DISABLE_MASK_LSB);
	axienet_iow(lp, XAE_FF_5_MASK_OFFSET, PROTO_FILTER_ENABLE_MASK);
	axienet_iow(lp, XAE_FF_9_MASK_OFFSET, PORT_NUM_FILTER_ENABLE_MASK);
	axienet_iow(lp, XAE_FF_10_MASK_OFFSET, VERSION_FILTER_DISABLE_MASK);
}

static void change_filter_values_to_gptp(struct axienet_local *lp)
{
	axienet_iow(lp, XAE_FF_3_OFFSET, ETHERTYPE_FILTER_PTP);
	axienet_iow(lp, XAE_AF0_MASK_OFFSET, DESTMAC_FILTER_ENABLE_MASK_MSB);
	axienet_iow(lp, XAE_AF1_MASK_OFFSET, DESTMAC_FILTER_ENABLE_MASK_LSB);
	axienet_iow(lp, XAE_FF_5_MASK_OFFSET, PROTO_FILTER_DISABLE_MASK);
	axienet_iow(lp, XAE_FF_9_MASK_OFFSET, PORT_NUM_FILTER_ENABLE_MASK);
	axienet_iow(lp, XAE_FF_10_MASK_OFFSET, VERSION_FILTER_DISABLE_MASK);
}

/**
 * axienet_set_ts_config - user entry point for timestamp mode
 * @lp: Pointer to axienet local structure
 * @ifr: ioctl data
 *
 * Set hardware to the requested more. If unsupported return an error
 * with no changes. Otherwise, store the mode for future reference
 *
 * Return: 0 on success, Negative value on errors
 */
static int axienet_set_ts_config(struct axienet_local *lp, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;
	if (config.rx_filter == HWTSTAMP_FILTER_PTP_V2_L2_EVENT &&
	    lp->current_rx_filter == HWTSTAMP_FILTER_PTP_V2_L4_EVENT) {
		lp->current_rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		change_filter_values_to_gptp(lp);
	}
	if (config.rx_filter == HWTSTAMP_FILTER_PTP_V2_L4_EVENT &&
	    lp->current_rx_filter == HWTSTAMP_FILTER_PTP_V2_L2_EVENT) {
		lp->current_rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		change_filter_values_to_udp(lp);
	}
	err = axienet_set_timestamp_mode(lp, &config);
	if (err)
		return err;

	/* save these settings for future reference */
	memcpy(&lp->tstamp_config, &config, sizeof(lp->tstamp_config));

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

/**
 * axienet_get_ts_config - return the current timestamp configuration
 * to the user
 * @lp: pointer to axienet local structure
 * @ifr: ioctl data
 *
 * Return: 0 on success, Negative value on errors
 */
static int axienet_get_ts_config(struct axienet_local *lp, struct ifreq *ifr)
{
	struct hwtstamp_config *config = &lp->tstamp_config;

	return copy_to_user(ifr->ifr_data, config,
			    sizeof(*config)) ? -EFAULT : 0;
}
#endif

/* Ioctl MII Interface */
static int axienet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
#if defined(CONFIG_XILINX_TSN_PTP)
	struct axienet_local *lp = netdev_priv(dev);
#endif

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		if (!dev->phydev)
			return -EOPNOTSUPP;
		return phy_mii_ioctl(dev->phydev, rq, cmd);
#if defined(CONFIG_XILINX_TSN_PTP)
	case SIOCSHWTSTAMP:
		return axienet_set_ts_config(lp, rq);
	case SIOCGHWTSTAMP:
		return axienet_get_ts_config(lp, rq);
#endif
	default:
		return -EOPNOTSUPP;
	}
}

static int axienet_ioctl_siocdevprivate(struct net_device *dev,
					struct ifreq *rq, void __user *data, int cmd)
{
	struct axienet_local *lp = netdev_priv(dev);

	switch (cmd) {
#ifdef CONFIG_XILINX_TSN_QBV
	case SIOCCHIOCTL:
		if (lp->qbv_regs)
			return axienet_set_schedule(dev, data);
		return -EINVAL;
	case SIOC_GET_SCHED:
		if (lp->qbv_regs)
			return axienet_get_schedule(dev, data);
		return -EINVAL;
#endif
#ifdef CONFIG_AXIENET_HAS_TADMA
	case SIOC_TADMA_OFF:
		if (!(lp->abl_reg & TSN_BRIDGEEP_EPONLY))
			return -ENOENT;
		return axienet_tadma_off(dev, data);
	case SIOC_TADMA_STR_ADD:
		if (!(lp->abl_reg & TSN_BRIDGEEP_EPONLY))
			return -ENOENT;
		return axienet_tadma_add_stream(dev, data);
	case SIOC_TADMA_PROG_ALL:
		if (!(lp->abl_reg & TSN_BRIDGEEP_EPONLY))
			return -ENOENT;
		return axienet_tadma_program(dev, data);
	case SIOC_TADMA_STR_FLUSH:
		if (!(lp->abl_reg & TSN_BRIDGEEP_EPONLY))
			return -ENOENT;
		return axienet_tadma_flush_stream(dev, data);
#endif
#ifdef CONFIG_XILINX_TSN_QBR
	case SIOC_PREEMPTION_CFG:
		return axienet_preemption(dev, data);
	case SIOC_PREEMPTION_CTRL:
		return axienet_preemption_ctrl(dev, data);
	case SIOC_PREEMPTION_STS:
		return axienet_preemption_sts(dev, data);
	case SIOC_PREEMPTION_RECEIVE:
		return axienet_preemption_receive(dev);
	case SIOC_PREEMPTION_COUNTER:
		return axienet_preemption_cnt(dev, data);
#ifdef CONFIG_XILINX_TSN_QBV
	case SIOC_QBU_USER_OVERRIDE:
		return axienet_qbu_user_override(dev, data);
	case SIOC_QBU_STS:
		return axienet_qbu_sts(dev, data);
#endif
#endif

	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops axienet_netdev_ops = {
	.ndo_open = axienet_tsn_open,
	.ndo_stop = axienet_tsn_stop,
	.ndo_start_xmit = axienet_tsn_xmit,
	.ndo_change_mtu	= axienet_change_mtu,
	.ndo_set_mac_address = netdev_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_eth_ioctl = axienet_ioctl,
	.ndo_siocdevprivate = axienet_ioctl_siocdevprivate,
	.ndo_set_rx_mode = axienet_set_multicast_list_tsn,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = axienet_poll_controller,
#endif
#ifdef CONFIG_XILINX_TSN
	.ndo_select_queue = axienet_tsn_select_queue,
#if defined(CONFIG_XILINX_TSN_SWITCH)
	.ndo_get_port_parent_id = tsn_switch_get_port_parent_id,
#endif
#endif
};

/**
 * axienet_ethtools_get_drvinfo - Get various Axi Ethernet driver information.
 * @ndev:	Pointer to net_device structure
 * @ed:		Pointer to ethtool_drvinfo structure
 *
 * This implements ethtool command for getting the driver information.
 * Issue "ethtool -i ethX" under linux prompt to execute this function.
 */
static void axienet_ethtools_get_drvinfo(struct net_device *ndev,
					 struct ethtool_drvinfo *ed)
{
	strscpy(ed->driver, DRIVER_NAME, sizeof(ed->driver));
	strscpy(ed->version, DRIVER_VERSION, sizeof(ed->version));
}

/**
 * axienet_ethtools_get_regs_len - Get the total regs length present in the
 *				   AxiEthernet core.
 * @ndev:	Pointer to net_device structure
 *
 * This implements ethtool command for getting the total register length
 * information.
 *
 * Return: the total regs length
 */
static int axienet_ethtools_get_regs_len(struct net_device *ndev)
{
	return sizeof(u32) * AXIENET_REGS_N;
}

/**
 * axienet_ethtools_get_regs - Dump the contents of all registers present
 *			       in AxiEthernet core.
 * @ndev:	Pointer to net_device structure
 * @regs:	Pointer to ethtool_regs structure
 * @ret:	Void pointer used to return the contents of the registers.
 *
 * This implements ethtool command for getting the Axi Ethernet register dump.
 * Issue "ethtool -d ethX" to execute this function.
 */
static void axienet_ethtools_get_regs(struct net_device *ndev,
				      struct ethtool_regs *regs, void *ret)
{
	u32 *data = (u32 *)ret;
	size_t len = sizeof(u32) * AXIENET_REGS_N;
	struct axienet_local *lp = netdev_priv(ndev);

	regs->version = 0;
	regs->len = len;

	memset(data, 0, len);
	data[13] = axienet_ior(lp, XAE_RCW0_OFFSET);
	data[14] = axienet_ior(lp, XAE_RCW1_OFFSET);
	data[15] = axienet_ior(lp, XAE_TC_OFFSET);
	data[16] = axienet_ior(lp, XAE_FCC_OFFSET);
	data[17] = axienet_ior(lp, XAE_EMMC_OFFSET);
	data[18] = axienet_ior(lp, XAE_RMFC_OFFSET);
	data[19] = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	data[20] = axienet_ior(lp, XAE_MDIO_MCR_OFFSET);
	data[21] = axienet_ior(lp, XAE_MDIO_MWD_OFFSET);
	data[22] = axienet_ior(lp, XAE_MDIO_MRD_OFFSET);
	data[23] = axienet_ior(lp, XAE_TEMAC_IS_OFFSET);
	data[24] = axienet_ior(lp, XAE_TEMAC_IP_OFFSET);
	data[25] = axienet_ior(lp, XAE_TEMAC_IE_OFFSET);
	data[26] = axienet_ior(lp, XAE_TEMAC_IC_OFFSET);
	data[27] = axienet_ior(lp, XAE_UAW0_OFFSET);
	data[28] = axienet_ior(lp, XAE_UAW1_OFFSET);
	data[29] = axienet_ior(lp, XAE_FMC_OFFSET);
	data[30] = axienet_ior(lp, XAE_AF0_OFFSET);
	data[31] = axienet_ior(lp, XAE_AF1_OFFSET);
	/* Support only single DMA queue */
	data[32] = axienet_dma_in32(lp->dq[0], XAXIDMA_TX_CR_OFFSET);
	data[33] = axienet_dma_in32(lp->dq[0], XAXIDMA_TX_SR_OFFSET);
	data[34] = axienet_dma_in32(lp->dq[0], XAXIDMA_TX_CDESC_OFFSET);
	data[35] = axienet_dma_in32(lp->dq[0], XAXIDMA_TX_TDESC_OFFSET);
	data[36] = axienet_dma_in32(lp->dq[0], XAXIDMA_RX_CR_OFFSET);
	data[37] = axienet_dma_in32(lp->dq[0], XAXIDMA_RX_SR_OFFSET);
	data[38] = axienet_dma_in32(lp->dq[0], XAXIDMA_RX_CDESC_OFFSET);
	data[39] = axienet_dma_in32(lp->dq[0], XAXIDMA_RX_TDESC_OFFSET);
}

static void axienet_ethtools_get_ringparam(struct net_device *ndev,
					   struct ethtool_ringparam *ering)
{
	struct axienet_local *lp = netdev_priv(ndev);

	ering->rx_max_pending = RX_BD_NUM_MAX;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = 0;
	ering->tx_max_pending = TX_BD_NUM_MAX;
	ering->rx_pending = lp->rx_bd_num;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = 0;
	ering->tx_pending = lp->tx_bd_num;
}

static int axienet_ethtools_set_ringparam(struct net_device *ndev,
					  struct ethtool_ringparam *ering)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (ering->rx_pending > RX_BD_NUM_MAX ||
	    ering->rx_mini_pending ||
	    ering->rx_jumbo_pending ||
	    ering->rx_pending > TX_BD_NUM_MAX)
		return -EINVAL;

	if (netif_running(ndev))
		return -EBUSY;

	lp->rx_bd_num = ering->rx_pending;
	lp->tx_bd_num = ering->tx_pending;
	return 0;
}

/**
 * axienet_ethtools_get_pauseparam - Get the pause parameter setting for
 *				     Tx and Rx paths.
 * @ndev:	Pointer to net_device structure
 * @epauseparm:	Pointer to ethtool_pauseparam structure.
 *
 * This implements ethtool command for getting axi ethernet pause frame
 * setting. Issue "ethtool -a ethX" to execute this function.
 */
static void
axienet_ethtools_get_pauseparam(struct net_device *ndev,
				struct ethtool_pauseparam *epauseparm)
{
	u32 regval;
	struct axienet_local *lp = netdev_priv(ndev);

	epauseparm->autoneg  = 0;
	regval = axienet_ior(lp, XAE_FCC_OFFSET);
	epauseparm->tx_pause = regval & XAE_FCC_FCTX_MASK;
	epauseparm->rx_pause = regval & XAE_FCC_FCRX_MASK;
}

/**
 * axienet_ethtools_set_pauseparam - Set device pause parameter(flow control)
 *				     settings.
 * @ndev:	Pointer to net_device structure
 * @epauseparm:	Pointer to ethtool_pauseparam structure
 *
 * This implements ethtool command for enabling flow control on Rx and Tx
 * paths. Issue "ethtool -A ethX tx on|off" under linux prompt to execute this
 * function.
 *
 * Return: 0 on success, -EFAULT if device is running
 */
static int
axienet_ethtools_set_pauseparam(struct net_device *ndev,
				struct ethtool_pauseparam *epauseparm)
{
	u32 regval = 0;
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netdev_err(ndev,
			   "Please stop netif before applying configuration\n");
		return -EFAULT;
	}

	regval = axienet_ior(lp, XAE_FCC_OFFSET);
	if (epauseparm->tx_pause)
		regval |= XAE_FCC_FCTX_MASK;
	else
		regval &= ~XAE_FCC_FCTX_MASK;
	if (epauseparm->rx_pause)
		regval |= XAE_FCC_FCRX_MASK;
	else
		regval &= ~XAE_FCC_FCRX_MASK;
	axienet_iow(lp, XAE_FCC_OFFSET, regval);

	return 0;
}

/**
 * axienet_ethtools_get_coalesce - Get DMA interrupt coalescing count.
 * @ndev:	Pointer to net_device structure
 * @ecoalesce:	Pointer to ethtool_coalesce structure
 * @kernel_coal: ethtool CQE mode setting structure
 * @extack:	extack for reporting error messages
 *
 * This implements ethtool command for getting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -c ethX" under linux prompt to
 * execute this function.
 *
 * Return: 0 always
 */
int axienet_ethtools_get_coalesce(struct net_device *ndev,
				  struct ethtool_coalesce *ecoalesce,
				  struct kernel_ethtool_coalesce *kernel_coal,
				  struct netlink_ext_ack *extack)
{
	u32 regval = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	int i;

	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];
		if (!q)
			return 0;

		regval = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		ecoalesce->rx_max_coalesced_frames +=
						(regval & XAXIDMA_COALESCE_MASK)
						     >> XAXIDMA_COALESCE_SHIFT;
	}
	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
		if (!q)
			return 0;
		regval = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
		ecoalesce->tx_max_coalesced_frames +=
						(regval & XAXIDMA_COALESCE_MASK)
						     >> XAXIDMA_COALESCE_SHIFT;
	}
	return 0;
}

/**
 * axienet_ethtools_set_coalesce - Set DMA interrupt coalescing count.
 * @ndev:	Pointer to net_device structure
 * @ecoalesce:	Pointer to ethtool_coalesce structure
 * @kernel_coal: ethtool CQE mode setting structure
 * @extack:	extack for reporting error messages
 *
 * This implements ethtool command for setting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -C ethX rx-frames 5" under linux
 * prompt to execute this function.
 *
 * Return: 0, on success, Non-zero error value on failure.
 */
int axienet_ethtools_set_coalesce(struct net_device *ndev,
				  struct ethtool_coalesce *ecoalesce,
				  struct kernel_ethtool_coalesce *kernel_coal,
				  struct netlink_ext_ack *extack)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netdev_err(ndev,
			   "Please stop netif before applying configuration\n");
		return -EFAULT;
	}

	if (ecoalesce->rx_coalesce_usecs ||
	    ecoalesce->rx_coalesce_usecs_irq ||
	    ecoalesce->rx_max_coalesced_frames_irq ||
	    ecoalesce->tx_coalesce_usecs ||
	    ecoalesce->tx_coalesce_usecs_irq ||
	    ecoalesce->tx_max_coalesced_frames_irq ||
	    ecoalesce->stats_block_coalesce_usecs ||
	    ecoalesce->use_adaptive_rx_coalesce ||
	    ecoalesce->use_adaptive_tx_coalesce ||
	    ecoalesce->pkt_rate_low ||
	    ecoalesce->rx_coalesce_usecs_low ||
	    ecoalesce->rx_max_coalesced_frames_low ||
	    ecoalesce->tx_coalesce_usecs_low ||
	    ecoalesce->tx_max_coalesced_frames_low ||
	    ecoalesce->pkt_rate_high ||
	    ecoalesce->rx_coalesce_usecs_high ||
	    ecoalesce->rx_max_coalesced_frames_high ||
	    ecoalesce->tx_coalesce_usecs_high ||
	    ecoalesce->tx_max_coalesced_frames_high ||
	    ecoalesce->rate_sample_interval)
		return -EOPNOTSUPP;
	if (ecoalesce->rx_max_coalesced_frames)
		lp->coalesce_count_rx = ecoalesce->rx_max_coalesced_frames;
	if (ecoalesce->tx_max_coalesced_frames)
		lp->coalesce_count_tx = ecoalesce->tx_max_coalesced_frames;

	return 0;
}

#if defined(CONFIG_XILINX_TSN_PTP)
/**
 * axienet_ethtools_get_ts_info - Get h/w timestamping capabilities.
 * @ndev:	Pointer to net_device structure
 * @info:	Pointer to ethtool_ts_info structure
 *
 * Return: 0, on success, Non-zero error value on failure.
 */
static int axienet_ethtools_get_ts_info(struct net_device *ndev,
					struct ethtool_ts_info *info)
{
	struct axienet_local *lp = netdev_priv(ndev);

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON) |
			(1 << HWTSTAMP_TX_ONESTEP_SYNC) |
			(1 << HWTSTAMP_TX_ONESTEP_P2P);
	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_ALL);
	info->phc_index = lp->phc_index;

#ifdef CONFIG_XILINX_TSN_PTP
	info->phc_index = axienet_phc_index;
#endif
	return 0;
}
#endif

/**
 * axienet_ethtools_sset_count - Get number of strings that
 *				 get_strings will write.
 * @ndev:	Pointer to net_device structure
 * @sset:	Get the set strings
 *
 * Return: number of strings, on success, Non-zero error value on
 *	   failure.
 */
static int axienet_ethtools_sset_count(struct net_device *ndev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return axienet_sset_count_tsn(ndev, sset);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * axienet_ethtools_get_stats - Get the extended statistics
 *				about the device.
 * @ndev:	Pointer to net_device structure
 * @stats:	Pointer to ethtool_stats structure
 * @data:	To store the statistics values
 *
 * Return: None.
 */
static void axienet_ethtools_get_stats(struct net_device *ndev,
				       struct ethtool_stats *stats,
				       u64 *data)
{
	unsigned int i = 0;

	data[i++] = ndev->stats.tx_packets;
	data[i++] = ndev->stats.rx_packets;
	data[i++] = ndev->stats.tx_bytes;
	data[i++] = ndev->stats.rx_bytes;
	data[i++] = ndev->stats.tx_errors;
	data[i++] = ndev->stats.rx_missed_errors + ndev->stats.rx_frame_errors;

	axienet_get_stats_tsn(ndev, stats, data);
}

/**
 * axienet_ethtools_strings - Set of strings that describe
 *			 the requested objects.
 * @ndev:	Pointer to net_device structure
 * @sset:	Get the set strings
 * @data:	Data of Transmit and Receive statistics
 *
 * Return: None.
 */
static void axienet_ethtools_strings(struct net_device *ndev, u32 sset, u8 *data)
{
	int i;

	for (i = 0; i < AXIENET_ETHTOOLS_SSTATS_LEN; i++) {
		if (sset == ETH_SS_STATS)
			memcpy(data + i * ETH_GSTRING_LEN,
			       axienet_get_ethtools_strings_stats[i].name,
			       ETH_GSTRING_LEN);
	}
	axienet_strings_tsn(ndev, sset, data);
}

static const struct ethtool_ops axienet_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_MAX_FRAMES,
	.get_drvinfo    = axienet_ethtools_get_drvinfo,
	.get_regs_len   = axienet_ethtools_get_regs_len,
	.get_regs       = axienet_ethtools_get_regs,
	.get_link       = ethtool_op_get_link,
	.get_ringparam	= axienet_ethtools_get_ringparam,
	.set_ringparam  = axienet_ethtools_set_ringparam,
	.get_pauseparam = axienet_ethtools_get_pauseparam,
	.set_pauseparam = axienet_ethtools_set_pauseparam,
	.get_coalesce   = axienet_ethtools_get_coalesce,
	.set_coalesce   = axienet_ethtools_set_coalesce,
	.get_sset_count	= axienet_ethtools_sset_count,
	.get_ethtool_stats = axienet_ethtools_get_stats,
	.get_strings = axienet_ethtools_strings,
#if defined(CONFIG_XILINX_TSN_PTP)
	.get_ts_info    = axienet_ethtools_get_ts_info,
#endif
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static int axienet_clk_init(struct platform_device *pdev,
			    struct clk **axi_aclk, struct clk **axis_clk,
			    struct clk **ref_clk, struct clk **tmpclk)
{
	int err;

	*tmpclk = NULL;

	/* The "ethernet_clk" is deprecated and will be removed sometime in
	 * the future. For proper clock usage check axiethernet binding
	 * documentation.
	 */
	*axi_aclk = devm_clk_get(&pdev->dev, "ethernet_clk");
	if (IS_ERR(*axi_aclk)) {
		if (PTR_ERR(*axi_aclk) != -ENOENT) {
			err = PTR_ERR(*axi_aclk);
			return err;
		}

		*axi_aclk = devm_clk_get(&pdev->dev, "s_axi_lite_clk");
		if (IS_ERR(*axi_aclk)) {
			if (PTR_ERR(*axi_aclk) != -ENOENT) {
				err = PTR_ERR(*axi_aclk);
				return err;
			}
			*axi_aclk = NULL;
		}

	} else {
		dev_warn(&pdev->dev, "ethernet_clk is deprecated and will be removed sometime in the future\n");
	}

	*axis_clk = devm_clk_get(&pdev->dev, "axis_clk");
	if (IS_ERR(*axis_clk)) {
		if (PTR_ERR(*axis_clk) != -ENOENT) {
			err = PTR_ERR(*axis_clk);
			return err;
		}
		*axis_clk = NULL;
	}

	*ref_clk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(*ref_clk)) {
		if (PTR_ERR(*ref_clk) != -ENOENT) {
			err = PTR_ERR(*ref_clk);
			return err;
		}
		*ref_clk = NULL;
	}

	err = clk_prepare_enable(*axi_aclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_aclk/ethernet_clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(*axis_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axis_clk (%d)\n", err);
		goto err_disable_axi_aclk;
	}

	err = clk_prepare_enable(*ref_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable ref_clk (%d)\n", err);
		goto err_disable_axis_clk;
	}

	return 0;

err_disable_axis_clk:
	clk_disable_unprepare(*axis_clk);
err_disable_axi_aclk:
	clk_disable_unprepare(*axi_aclk);

	return err;
}

static void axienet_clk_disable(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);

	clk_disable_unprepare(lp->dma_sg_clk);
	clk_disable_unprepare(lp->dma_tx_clk);
	clk_disable_unprepare(lp->dma_rx_clk);
	clk_disable_unprepare(lp->eth_sclk);
	clk_disable_unprepare(lp->eth_refclk);
	clk_disable_unprepare(lp->eth_dclk);
	clk_disable_unprepare(lp->aclk);
}

static const struct axienet_config axienet_1g_config_tsn = {
	.mactype = XAXIENET_1G,
	.setoptions = axienet_setoptions_tsn,
	.clk_init = axienet_clk_init,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

/* Match table for of_platform binding */
static const struct of_device_id axienet_of_match[] = {
	{ .compatible = "xlnx,tsn-ethernet-1.00.a", .data = &axienet_1g_config_tsn},
	{},
};

MODULE_DEVICE_TABLE(of, axienet_of_match);

/**
 * axienet_probe - Axi Ethernet probe function.
 * @pdev:	Pointer to platform device structure.
 *
 * Return: 0, on success
 *	    Non-zero error value on failure.
 *
 * This is the probe routine for Axi Ethernet driver. This is called before
 * any other driver routines are invoked. It allocates and sets up the Ethernet
 * device. Parses through device tree and populates fields of
 * axienet_local. It registers the Ethernet device.
 */
static int axienet_probe(struct platform_device *pdev)
{
	int (*axienet_clk_init)(struct platform_device *pdev,
				struct clk **axi_aclk, struct clk **axis_clk,
				struct clk **ref_clk, struct clk **tmpclk) =
					axienet_clk_init;
	int ret = 0;
	struct axienet_local *lp;
	struct net_device *ndev;
	u8 mac_addr[ETH_ALEN];
	struct resource *ethres;
	u32 value;
	u16 num_queues = XAE_MAX_QUEUES;

	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-queues",
				   &num_queues);
	if (num_queues < XAE_TSN_MIN_QUEUES)
		num_queues = XAE_TSN_MIN_QUEUES;
	else if (num_queues > XAE_MAX_QUEUES)
		num_queues = XAE_MAX_QUEUES;

	ndev = alloc_etherdev_mq(sizeof(*lp), num_queues);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->flags &= ~IFF_MULTICAST;  /* clear multicast */
	ndev->features = NETIF_F_SG;
	ndev->netdev_ops = &axienet_netdev_ops;
	ndev->ethtool_ops = &axienet_ethtool_ops;

	/* MTU range: 64 - 9000 */
	ndev->min_mtu = 64;
	ndev->max_mtu = XAE_JUMBO_MTU;

	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &pdev->dev;
	lp->options = XAE_OPTION_DEFAULTS;
	lp->num_tx_queues = num_queues;
	lp->num_rx_queues = num_queues;
	lp->rx_bd_num = RX_BD_NUM_DEFAULT;
	lp->tx_bd_num = TX_BD_NUM_DEFAULT;

	lp->axi_clk = devm_clk_get_optional(&pdev->dev, "s_axi_lite_clk");
	if (!lp->axi_clk) {
		/* For backward compatibility, if named AXI clock is not present,
		 * treat the first clock specified as the AXI clock.
		 */
		lp->axi_clk = devm_clk_get_optional(&pdev->dev, NULL);
	}
	if (IS_ERR(lp->axi_clk)) {
		ret = PTR_ERR(lp->axi_clk);
		goto free_netdev;
	}
	ret = clk_prepare_enable(lp->axi_clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable AXI clock: %d\n", ret);
		goto free_netdev;
	}

	lp->misc_clks[0].id = "axis_clk";
	lp->misc_clks[1].id = "ref_clk";
	lp->misc_clks[2].id = "mgt_clk";

	ret = devm_clk_bulk_get_optional(&pdev->dev, XAE_NUM_MISC_CLOCKS, lp->misc_clks);
	if (ret)
		goto cleanup_clk;

	ret = clk_bulk_prepare_enable(XAE_NUM_MISC_CLOCKS, lp->misc_clks);
	if (ret)
		goto cleanup_clk;
	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-tc",
				   &lp->num_tc);
	if (ret || (lp->num_tc != 2 && lp->num_tc != 3))
		lp->num_tc = XAE_MAX_TSN_TC;

	/* Map device registers */
	lp->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &ethres);
	if (IS_ERR(lp->regs)) {
		ret = PTR_ERR(lp->regs);
		goto cleanup_clk;
	}
	lp->regs_start = ethres->start;

	/* Setup checksum offload, but default to off if not specified */
	lp->features = 0;

	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_node(axienet_of_match, pdev->dev.of_node);
		if (match && match->data) {
			lp->axienet_config = match->data;
			axienet_clk_init = lp->axienet_config->clk_init;
		}
	}

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,txcsum", &value);
	if (!ret) {
		dev_info(&pdev->dev, "TX_CSUM %d\n", value);

		switch (value) {
		case 1:
			lp->csum_offload_on_tx_path =
				XAE_FEATURE_PARTIAL_TX_CSUM;
			lp->features |= XAE_FEATURE_PARTIAL_TX_CSUM;
			/* Can checksum TCP/UDP over IPv4. */
			ndev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
			break;
		case 2:
			lp->csum_offload_on_tx_path =
				XAE_FEATURE_FULL_TX_CSUM;
			lp->features |= XAE_FEATURE_FULL_TX_CSUM;
			/* Can checksum TCP/UDP over IPv4. */
			ndev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
			break;
		default:
			lp->csum_offload_on_tx_path = XAE_NO_CSUM_OFFLOAD;
		}
	}
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,rxcsum", &value);
	if (!ret) {
		dev_info(&pdev->dev, "RX_CSUM %d\n", value);

		switch (value) {
		case 1:
			lp->csum_offload_on_rx_path =
				XAE_FEATURE_PARTIAL_RX_CSUM;
			lp->features |= XAE_FEATURE_PARTIAL_RX_CSUM;
			break;
		case 2:
			lp->csum_offload_on_rx_path =
				XAE_FEATURE_FULL_RX_CSUM;
			lp->features |= XAE_FEATURE_FULL_RX_CSUM;
			break;
		default:
			lp->csum_offload_on_rx_path = XAE_NO_CSUM_OFFLOAD;
		}
	}
	/* For supporting jumbo frames, the Axi Ethernet hardware must have
	 * a larger Rx/Tx Memory. Typically, the size must be large so that
	 * we can enable jumbo option and start supporting jumbo frames.
	 * Here we check for memory allocated for Rx/Tx in the hardware from
	 * the device-tree and accordingly set flags.
	 */
	of_property_read_u32(pdev->dev.of_node, "xlnx,rxmem", &lp->rxmem);

	/* The phy_mode is optional but when it is not specified it should not
	 *  be a value that alters the driver behavior so set it to an invalid
	 *  value as the default.
	 */
	lp->phy_mode = PHY_INTERFACE_MODE_NA;
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,phy-type", &lp->phy_mode);
	if (!ret)
		netdev_warn(ndev, "xlnx,phy-type is deprecated, Please upgrade your device tree to use phy-mode");

	/* Set default USXGMII rate */
	lp->usxgmii_rate = SPEED_1000;
	of_property_read_u32(pdev->dev.of_node, "xlnx,usxgmii-rate",
			     &lp->usxgmii_rate);

	/* Set default MRMAC rate */
	lp->mrmac_rate = SPEED_10000;
	of_property_read_u32(pdev->dev.of_node, "xlnx,mrmac-rate",
			     &lp->mrmac_rate);

	lp->eth_hasnobuf = of_property_read_bool(pdev->dev.of_node,
						 "xlnx,eth-hasnobuf");
	lp->eth_hasptp = of_property_read_bool(pdev->dev.of_node,
					       "xlnx,eth-hasptp");

	if (lp->axienet_config->mactype == XAXIENET_1G && !lp->eth_hasnobuf)
		lp->eth_irq = platform_get_irq(pdev, 0);

	ret = axienet_tsn_probe(pdev, lp, ndev);

	ret = axienet_clk_init(pdev, &lp->aclk, &lp->eth_sclk,
			       &lp->eth_refclk, &lp->eth_dclk);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Ethernet clock init failed %d\n", ret);
		goto err_disable_clk;
	}

	lp->eth_irq = platform_get_irq(pdev, 0);
	/* Check for Ethernet core IRQ (optional) */
	if (lp->eth_irq <= 0)
		dev_info(&pdev->dev, "Ethernet core IRQ not defined\n");

	/* Retrieve the MAC address */
	ret = of_get_mac_address(pdev->dev.of_node, mac_addr);
	if (!ret) {
		axienet_set_mac_address_tsn(ndev, mac_addr);
	} else {
		dev_warn(&pdev->dev, "could not find MAC address property: %d\n",
			 ret);
		axienet_set_mac_address_tsn(ndev, NULL);
	}

	lp->coalesce_count_rx = XAXIDMA_DFT_RX_THRESHOLD;
	lp->coalesce_count_tx = XAXIDMA_DFT_TX_THRESHOLD;

	ret = of_get_phy_mode(pdev->dev.of_node, &lp->phy_mode);
	if (ret < 0)
		dev_warn(&pdev->dev, "couldn't find phy i/f\n");
	if (lp->phy_mode == PHY_INTERFACE_MODE_1000BASEX)
		lp->phy_flags = XAE_PHY_TYPE_1000BASE_X;

	lp->phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (lp->phy_node) {
		ret = axienet_mdio_setup(lp);
		if (ret)
			dev_warn(&pdev->dev,
				 "error registering MDIO bus: %d\n", ret);
	}

	/* Create sysfs file entries for the device */
	ret = axeinet_mcdma_create_sysfs_tsn(&lp->dev->kobj);
	if (ret < 0) {
		dev_err(lp->dev, "unable to create sysfs entries\n");
		return ret;
	}

	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
		axienet_mdio_teardown(lp);
		goto cleanup_clk;
	}

	return 0;

cleanup_clk:
	clk_bulk_disable_unprepare(XAE_NUM_MISC_CLOCKS, lp->misc_clks);
	clk_disable_unprepare(lp->axi_clk);

err_disable_clk:
	axienet_clk_disable(pdev);

free_netdev:
	free_netdev(ndev);

	return ret;
}

static int axienet_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);

#ifdef CONFIG_XILINX_TSN_PTP
	if (lp->timer_priv)
		axienet_ptp_timer_remove(lp->timer_priv);
#ifdef CONFIG_XILINX_TSN_QBV
		axienet_qbv_remove(ndev);
#endif
#endif
	unregister_netdev(ndev);
	axienet_clk_disable(pdev);

	if (lp->mii_bus)
		axienet_mdio_teardown(lp);

	clk_bulk_disable_unprepare(XAE_NUM_MISC_CLOCKS, lp->misc_clks);
	clk_disable_unprepare(lp->axi_clk);

	axeinet_mcdma_remove_sysfs_tsn(&lp->dev->kobj);
	of_node_put(lp->phy_node);
	lp->phy_node = NULL;

	free_netdev(ndev);

	return 0;
}

static void axienet_shutdown(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	rtnl_lock();
	netif_device_detach(ndev);

	if (netif_running(ndev))
		dev_close(ndev);

	rtnl_unlock();
}

static struct platform_driver axienet_driver_tsn = {
	.probe = axienet_probe,
	.remove = axienet_remove,
	.shutdown = axienet_shutdown,
	.driver = {
		 .name = "xilinx_axienet_tsn",
		 .of_match_table = axienet_of_match,
	},
};

module_platform_driver(axienet_driver_tsn);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL");

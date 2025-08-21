// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx Axi Ethernet device driver
 *
 * Copyright (c) 2008 Nissin Systems Co., Ltd.,  Yoshio Kashiwagi
 * Copyright (c) 2005-2008 DLA Systems,  David H. Lynch Jr. <dhlii@dlasys.net>
 * Copyright (c) 2008-2009 Secret Lab Technologies Ltd.
 * Copyright (c) 2010 - 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2010 - 2011 PetaLogix
 * Copyright (c) 2019 - 2022 Calian Advanced Technologies
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
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/math64.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/circ_buf.h>
#include <net/netdev_queues.h>
#include <linux/iopoll.h>
#include <linux/ptp_classify.h>
#include <linux/net_tstamp.h>
#include <linux/random.h>
#include <net/sock.h>
#include <linux/ptp/ptp_xilinx.h>
#include <linux/gpio/consumer.h>
#include <linux/inetdevice.h>

#include "xilinx_axienet.h"
#include "xilinx_axienet_eoe.h"

/* Descriptors defines for Tx and Rx DMA */
#define RX_BD_NUM_DEFAULT		128
#define TX_BD_NUM_MIN			(MAX_SKB_FRAGS + 1)
#define TX_BD_NUM_MAX			4096
#define RX_BD_NUM_MAX			4096
#define DMA_NUM_APP_WORDS		5
#define LEN_APP				4
#define RX_BUF_NUM_DEFAULT		128

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME		"xaxienet"
#define DRIVER_DESCRIPTION	"Xilinx Axi Ethernet driver"
#define DRIVER_VERSION		"1.00a"

#define AXIENET_REGS_N		40
#define AXIENET_TS_HEADER_LEN	8
#define XXVENET_TS_HEADER_LEN	4
#define MRMAC_TS_HEADER_LEN	16
#define MRMAC_TS_HEADER_WORDS	(MRMAC_TS_HEADER_LEN / 4)
#define NS_PER_SEC              1000000000ULL /* Nanoseconds per second */

#define	DELAY_1MS	1	/* 1 msecs delay*/

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

void __iomem *mrmac_gt_pll;
EXPORT_SYMBOL(mrmac_gt_pll);

void __iomem *mrmac_gt_ctrl;
EXPORT_SYMBOL(mrmac_gt_ctrl);

int mrmac_pll_reg;
EXPORT_SYMBOL(mrmac_pll_reg);

int mrmac_pll_rst;
EXPORT_SYMBOL(mrmac_pll_rst);

static void axienet_rx_submit_desc(struct net_device *ndev);

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
		.reg = XAE_FMI_OFFSET,
		.m_or = XAE_FMI_PM_MASK,
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

static struct skbuf_dma_descriptor *axienet_get_rx_desc(struct axienet_local *lp, int i)
{
	return lp->rx_skb_ring[i & (RX_BUF_NUM_DEFAULT - 1)];
}

static struct skbuf_dma_descriptor *axienet_get_tx_desc(struct axienet_local *lp, int i)
{
	return lp->tx_skb_ring[i & (TX_BD_NUM_MAX - 1)];
}

/* Option table for setting up Axi Ethernet hardware options */
static struct xxvenet_option xxvenet_options[] = {
	{ /* Turn on FCS stripping on receive packets */
		.opt = XAE_OPTION_FCS_STRIP,
		.reg = XXV_RCW1_OFFSET,
		.m_or = XXV_RCW1_FCS_MASK,
	}, { /* Turn on FCS insertion on transmit packets */
		.opt = XAE_OPTION_FCS_INSERT,
		.reg = XXV_TC_OFFSET,
		.m_or = XXV_TC_FCS_MASK,
	}, { /* Enable transmitter */
		.opt = XAE_OPTION_TXEN,
		.reg = XXV_TC_OFFSET,
		.m_or = XXV_TC_TX_MASK,
	}, { /* Enable receiver */
		.opt = XAE_OPTION_RXEN,
		.reg = XXV_RCW1_OFFSET,
		.m_or = XXV_RCW1_RX_MASK,
	},
	{}
};

/* Option table for setting up MRMAC hardware options */
static struct xxvenet_option mrmacenet_options[] = {
	{ /* Turn on FCS stripping on receive packets */
		.opt = XAE_OPTION_FCS_STRIP,
		.reg = MRMAC_CONFIG_RX_OFFSET,
		.m_or = MRMAC_RX_DEL_FCS_MASK,
	}, { /* Turn on FCS insertion on transmit packets */
		.opt = XAE_OPTION_FCS_INSERT,
		.reg = MRMAC_CONFIG_TX_OFFSET,
		.m_or = MRMAC_TX_INS_FCS_MASK,
	}, { /* Enable transmitter */
		.opt = XAE_OPTION_TXEN,
		.reg = MRMAC_CONFIG_TX_OFFSET,
		.m_or = MRMAC_TX_EN_MASK,
	}, { /* Enable receiver */
		.opt = XAE_OPTION_RXEN,
		.reg = MRMAC_CONFIG_RX_OFFSET,
		.m_or = MRMAC_RX_EN_MASK,
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
 * axienet_dma_bd_release - Release buffer descriptor rings
 * @ndev:	Pointer to the net_device structure
 *
 * This function is used to release the descriptors allocated in
 * axienet_dma_bd_init. axienet_dma_bd_release is called when Axi Ethernet
 * driver stop api is called.
 */
void axienet_dma_bd_release(struct net_device *ndev)
{
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

#ifdef CONFIG_AXIENET_HAS_MCDMA
	for_each_tx_dma_queue(lp, i) {
		axienet_mcdma_tx_bd_free(ndev, lp->dq[i]);
	}
#endif
	for_each_rx_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		if (axienet_eoe_is_channel_gro(lp, lp->dq[i]))
			axienet_eoe_mcdma_gro_bd_free(ndev, lp->dq[i]);
		else
			axienet_mcdma_rx_bd_free(ndev, lp->dq[i]);
#else
		axienet_bd_free(ndev, lp->dq[i]);
#endif
	}
}

/**
 * axienet_usec_to_timer - Calculate IRQ delay timer value
 * @lp:		Pointer to the axienet_local structure
 * @coalesce_usec: Microseconds to convert into timer value
 */
u32 axienet_usec_to_timer(struct axienet_local *lp, u32 coalesce_usec)
{
	u32 result;
	u64 clk_rate = 125000000; /* arbitrary guess if no clock rate set */

	if (lp->axi_clk)
		clk_rate = clk_get_rate(lp->axi_clk);

	/* 1 Timeout Interval = 125 * (clock period of SG clock) */
	result = DIV64_U64_ROUND_CLOSEST((u64)coalesce_usec * clk_rate,
					 (u64)125000000);
	if (result > 255)
		result = 255;

	return result;
}

/**
 * axienet_dma_start - Set up DMA registers and start DMA operation
 * @dq:		Pointer to the axienet_dma_q structure
 */
void axienet_dma_start(struct axienet_dma_q *dq)
{
	struct axienet_local *lp = dq->lp;

	/* Start updating the Rx channel control register */
	dq->rx_dma_cr = (lp->coalesce_count_rx << XAXIDMA_COALESCE_SHIFT) |
			XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK;
	/* Only set interrupt delay timer if not generating an interrupt on
	 * the first RX packet. Otherwise leave at 0 to disable delay interrupt.
	 */
	if (lp->coalesce_count_rx > 1)
		dq->rx_dma_cr |= (axienet_usec_to_timer(lp, lp->coalesce_usec_rx)
					<< XAXIDMA_DELAY_SHIFT) |
				 XAXIDMA_IRQ_DELAY_MASK;
	axienet_dma_out32(dq, XAXIDMA_RX_CR_OFFSET, dq->rx_dma_cr);

	/* Start updating the Tx channel control register */
	dq->tx_dma_cr = (lp->coalesce_count_tx << XAXIDMA_COALESCE_SHIFT) |
			XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK;
	/* Only set interrupt delay timer if not generating an interrupt on
	 * the first TX packet. Otherwise leave at 0 to disable delay interrupt.
	 */
	if (lp->coalesce_count_tx > 1)
		dq->tx_dma_cr |= (axienet_usec_to_timer(lp, lp->coalesce_usec_tx)
					<< XAXIDMA_DELAY_SHIFT) |
				 XAXIDMA_IRQ_DELAY_MASK;
	axienet_dma_out32(dq, XAXIDMA_TX_CR_OFFSET, dq->tx_dma_cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	axienet_dma_bdout(dq, XAXIDMA_RX_CDESC_OFFSET, dq->rx_bd_p);
	dq->rx_dma_cr |= XAXIDMA_CR_RUNSTOP_MASK;
	axienet_dma_out32(dq, XAXIDMA_RX_CR_OFFSET, dq->rx_dma_cr);
	axienet_dma_bdout(dq, XAXIDMA_RX_TDESC_OFFSET, dq->rx_bd_p +
			  (sizeof(*dq->rx_bd_v) * (lp->rx_bd_num - 1)));

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting.
	 */
	axienet_dma_bdout(dq, XAXIDMA_TX_CDESC_OFFSET, dq->tx_bd_p);
	dq->tx_dma_cr |= XAXIDMA_CR_RUNSTOP_MASK;
	axienet_dma_out32(dq, XAXIDMA_TX_CR_OFFSET, dq->tx_dma_cr);
}

/**
 * axienet_dma_bd_init - Setup buffer descriptor rings for Axi DMA
 * @ndev:	Pointer to the net_device structure
 *
 * Return: 0, on success -ENOMEM, on failure -EINVAL, on default return
 *
 * This function is called to initialize the Rx and Tx DMA descriptor
 * rings. This initializes the descriptors with required default values
 * and is called when Axi Ethernet driver reset is called.
 */
static int axienet_dma_bd_init(struct net_device *ndev)
{
	int i, ret = -EINVAL;
	struct axienet_local *lp = netdev_priv(ndev);

#ifdef CONFIG_AXIENET_HAS_MCDMA
	for_each_tx_dma_queue(lp, i) {
		ret = axienet_mcdma_tx_q_init(ndev, lp->dq[i]);
		if (ret != 0)
			break;
	}
#endif
	for_each_rx_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		ret = axienet_mcdma_rx_q_init(ndev, lp->dq[i]);
#else
		ret = axienet_dma_q_init(ndev, lp->dq[i]);
#endif
		if (ret != 0) {
			netdev_err(ndev, "%s: Failed to init DMA buf %d\n", __func__, ret);
			break;
		}
	}
	return ret;
}

/**
 * axienet_set_mac_address - Write the MAC address
 * @ndev:	Pointer to the net_device structure
 * @address:	6 byte Address to be written as MAC address
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It writes to the UAW0 and UAW1 registers of the core.
 */
void axienet_set_mac_address(struct net_device *ndev,
			     const void *address)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (address)
		eth_hw_addr_set(ndev, address);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	if (lp->axienet_config->mactype != XAXIENET_1_2p5G)
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
 * core. It calls the core specific axienet_set_mac_address. This is the
 * function that goes into net_device_ops structure entry ndo_set_mac_address.
 */
static int netdev_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;

	axienet_set_mac_address(ndev, addr->sa_data);
	return 0;
}

/**
 * axienet_set_multicast_list - Prepare the multicast table
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to initialize the multicast table during
 * initialization. The Axi Ethernet basic multicast support has a four-entry
 * multicast table which is initialized here. Additionally this function
 * goes into the net_device_ops structure entry ndo_set_multicast_list. This
 * means whenever the multicast table entries need to be updated this
 * function gets called.
 */
void axienet_set_multicast_list(struct net_device *ndev)
{
	int i = 0;
	u32 reg, af0reg, af1reg;
	struct axienet_local *lp = netdev_priv(ndev);

	if (lp->axienet_config->mactype != XAXIENET_1_2p5G ||
	    lp->eth_hasnobuf)
		return;
	reg = axienet_ior(lp, XAE_FMI_OFFSET);
	reg &= ~XAE_FMI_PM_MASK;
	if (ndev->flags & IFF_PROMISC)
		reg |= XAE_FMI_PM_MASK;
	else
		reg &= ~XAE_FMI_PM_MASK;
	axienet_iow(lp, XAE_FMI_OFFSET, reg);

	if (ndev->flags & IFF_ALLMULTI ||
	    netdev_mc_count(ndev) > XAE_MULTICAST_CAM_TABLE_NUM) {
		reg &= 0xFFFFFF00;
		axienet_iow(lp, XAE_FMI_OFFSET, reg);
		axienet_iow(lp, XAE_AF0_OFFSET, 1); /* Multicast bit */
		axienet_iow(lp, XAE_AF1_OFFSET, 0);
		axienet_iow(lp, XAE_AM0_OFFSET, 1); /* ditto */
		axienet_iow(lp, XAE_AM1_OFFSET, 0);
		axienet_iow(lp, XAE_FFE_OFFSET, 1);
		i = 1;
	} else if (!netdev_mc_empty(ndev)) {
		struct netdev_hw_addr *ha;

		netdev_for_each_mc_addr(ha, ndev) {
			if (i >= XAE_MULTICAST_CAM_TABLE_NUM)
				break;

			af0reg = (ha->addr[0]);
			af0reg |= (ha->addr[1] << 8);
			af0reg |= (ha->addr[2] << 16);
			af0reg |= (ha->addr[3] << 24);

			af1reg = (ha->addr[4]);
			af1reg |= (ha->addr[5] << 8);

			reg &= 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMI_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, af0reg);
			axienet_iow(lp, XAE_AF1_OFFSET, af1reg);
			axienet_iow(lp, XAE_AM0_OFFSET, 0xffffffff);
			axienet_iow(lp, XAE_AM1_OFFSET, 0x0000ffff);
			axienet_iow(lp, XAE_FFE_OFFSET, 1);
			i++;
		}
	}

	for (; i < XAE_MULTICAST_CAM_TABLE_NUM; i++) {
		reg &= 0xFFFFFF00;
		reg |= i;
		axienet_iow(lp, XAE_FMI_OFFSET, reg);
		axienet_iow(lp, XAE_FFE_OFFSET, 0);
	}
}

/**
 * axienet_setoptions - Set an Axi Ethernet option
 * @ndev:	Pointer to the net_device structure
 * @options:	Option to be enabled/disabled
 *
 * The Axi Ethernet core has multiple features which can be selectively turned
 * on or off. The typical options could be jumbo frame option, basic VLAN
 * option, promiscuous mode option etc. This function is used to set or clear
 * these options in the Axi Ethernet hardware. This is done through
 * axienet_option structure .
 */
static void axienet_setoptions(struct net_device *ndev, u32 options)
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

static u64 axienet_stat(struct axienet_local *lp, enum temac_stat stat)
{
	u32 counter;

	if (lp->reset_in_progress)
		return lp->hw_stat_base[stat];

	counter = axienet_ior(lp, XAE_STATS_OFFSET + stat * 8);
	return lp->hw_stat_base[stat] + (counter - lp->hw_last_counter[stat]);
}

static void axienet_stats_update(struct axienet_local *lp, bool reset)
{
	enum temac_stat stat;

	write_seqcount_begin(&lp->hw_stats_seqcount);
	lp->reset_in_progress = reset;
	for (stat = 0; stat < STAT_COUNT; stat++) {
		u32 counter = axienet_ior(lp, XAE_STATS_OFFSET + stat * 8);

		lp->hw_stat_base[stat] += counter - lp->hw_last_counter[stat];
		lp->hw_last_counter[stat] = counter;
	}
	write_seqcount_end(&lp->hw_stats_seqcount);
}

static void axienet_refresh_stats(struct work_struct *work)
{
	struct axienet_local *lp = container_of(work, struct axienet_local,
						stats_work.work);

	mutex_lock(&lp->stats_lock);
	axienet_stats_update(lp, false);
	mutex_unlock(&lp->stats_lock);

	/* Just less than 2^32 bytes at 2.5 GBit/s */
	schedule_delayed_work(&lp->stats_work, 13 * HZ);
}

static void xxvenet_setoptions(struct net_device *ndev, u32 options)
{
	int reg;
	struct axienet_local *lp = netdev_priv(ndev);
	struct xxvenet_option *tp;

	if (lp->axienet_config->mactype == XAXIENET_MRMAC)
		tp = &mrmacenet_options[0];
	else
		tp = &xxvenet_options[0];

	while (tp->opt) {
		reg = ((axienet_ior(lp, tp->reg)) & ~(tp->m_or));
		if (options & tp->opt)
			reg |= tp->m_or;
		axienet_iow(lp, tp->reg, reg);
		tp++;
	}

	lp->options |= options;
}

static inline void axienet_mrmac_reset(struct axienet_local *lp)
{
	u32 val, reg, serdes_width, axis_cfg;

	val = axienet_ior(lp, MRMAC_RESET_OFFSET);
	val |= (MRMAC_RX_SERDES_RST_MASK | MRMAC_TX_SERDES_RST_MASK |
		MRMAC_RX_RST_MASK | MRMAC_TX_RST_MASK);
	axienet_iow(lp, MRMAC_RESET_OFFSET, val);
	mdelay(DELAY_1MS);

	reg = axienet_ior(lp, MRMAC_MODE_OFFSET);
	reg &= ~MRMAC_CTL_RATE_CFG_MASK;

	if (lp->max_speed == SPEED_25000) {
		axis_cfg = (lp->mrmac_stream_dwidth == MRMAC_STREAM_DWIDTH_128 ?
			    MRMAC_CTL_AXIS_CFG_25G_IND_128 :
			    MRMAC_CTL_AXIS_CFG_25G_IND_64);
		serdes_width = (lp->gt_mode_narrow ?
				MRMAC_CTL_SERDES_WIDTH_25G_NRW :
				MRMAC_CTL_SERDES_WIDTH_25G_WIDE);
		reg |= MRMAC_CTL_DATA_RATE_25G;
	} else {
		axis_cfg = MRMAC_CTL_AXIS_CFG_10G_IND;
		serdes_width = (lp->gt_mode_narrow ?
				MRMAC_CTL_SERDES_WIDTH_10G_NRW :
				MRMAC_CTL_SERDES_WIDTH_10G_WIDE);
		reg |= MRMAC_CTL_DATA_RATE_10G;
	}
	reg |= (axis_cfg << MRMAC_CTL_AXIS_CFG_SHIFT);
	reg |= (serdes_width <<	MRMAC_CTL_SERDES_WIDTH_SHIFT);

	/* For tick reg */
	reg |= MRMAC_CTL_PM_TICK_MASK;
	axienet_iow(lp, MRMAC_MODE_OFFSET, reg);

	val = axienet_ior(lp, MRMAC_RESET_OFFSET);
	val &= ~(MRMAC_RX_SERDES_RST_MASK | MRMAC_TX_SERDES_RST_MASK |
		MRMAC_RX_RST_MASK | MRMAC_TX_RST_MASK);
	axienet_iow(lp, MRMAC_RESET_OFFSET, val);
}

static ulong dcmac_gt_tx_reset_status(struct axienet_local *lp)
{
	ulong val;

	gpiod_get_array_value_cansleep(lp->gds_gt_tx_reset_done->ndescs,
				       lp->gds_gt_tx_reset_done->desc,
				       lp->gds_gt_tx_reset_done->info, &val);
	return val;
}

static ulong dcmac_gt_rx_reset_status(struct axienet_local *lp)
{
	ulong val;

	gpiod_get_array_value_cansleep(lp->gds_gt_rx_reset_done->ndescs,
				       lp->gds_gt_rx_reset_done->desc,
				       lp->gds_gt_rx_reset_done->info, &val);
	return val;
}

static void dcmac_init(struct axienet_local *lp)
{
	u32 val, val_tx, val_rx;

	val = (DCMAC_TX_ACTV_PRT_ALL_MASK | DCMAC_RX_ACTV_PRT_ALL_MASK |
		DCMAC_RX_ERR_IND_STD_MASK | DCMAC_TX_FEC_UNIQUE_FLIP_MASK |
		DCMAC_RX_FEC_UNIQUE_FLIP_MASK);
	axienet_iow(lp, DCMAC_G_MODE_OFFSET, val);

	val = (DCMAC_CH_RX_FCS_MASK | DCMAC_CH_RX_PREAMBLE_MASK |
		DCMAC_RX_IGNR_INRANGE_MASK | DCMAC_RX_MAX_PKT_LEN_MASK);
	axienet_iow(lp, DCMAC_CH_CFG_RX_OFFSET, val);

	val = (DCMAC_CH_TX_FCS_MASK | DCMAC_CH_TX_IPG_MASK);
	axienet_iow(lp, DCMAC_CH_CFG_TX_OFFSET, val);

	/* Set data rate and FEC mode */
	val_tx = 0x0;
	val_rx = 0x0;

	switch (lp->max_speed) {
	case SPEED_100000:
		val_tx &= DCMAC_P_SPEED_100G_MASK;
		val_rx &= DCMAC_P_SPEED_100G_MASK;
		/* 100G KR4 FEC operating mode */
		val_tx |= DCMAC_CH_MD_FEC_KR4;
		val_rx |= DCMAC_CH_MD_FEC_KR4;
		break;
	case SPEED_200000:
		val_tx |= DCMAC_P_SPEED_200G_MASK;
		val_rx |= DCMAC_P_SPEED_200G_MASK;
		/* 200G FEC operating mode */
		val_tx |= DCMAC_CH_MD_FEC_200G;
		val_rx |= DCMAC_CH_MD_FEC_200G;
		break;
	case SPEED_400000:
		val_tx |= DCMAC_P_SPEED_400G_MASK;
		val_rx |= DCMAC_P_SPEED_400G_MASK;
		/* 400G FEC operating mode */
		val_tx |= DCMAC_CH_MD_FEC_400G;
		val_rx |= DCMAC_CH_MD_FEC_400G;

		break;
	default:
		break;
	}
	/* pm_tick triggered by internal registers for channel statistics */
	val_tx |= DCMAC_CH_TXMD_PM_TICK_INTERNAL_MASK;
	val_rx |= DCMAC_CH_RXMD_PM_TICK_INTERNAL_MASK;

	axienet_iow(lp, DCMAC_CH_MODE_TX_OFFSET, val_tx);
	axienet_iow(lp, DCMAC_CH_MODE_RX_OFFSET, val_rx);
}

static ulong dcmac_rx_phy_status(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	ulong val;
	int ret;

	/* Reset GT Rx datapath */
	val = DCMAC_GT_RXDPATH_RST;
	gpiod_set_array_value_cansleep(lp->gds_gt_rx_dpath->ndescs,
				       lp->gds_gt_rx_dpath->desc,
				       lp->gds_gt_rx_dpath->info, &val);
	mdelay(DELAY_1MS);
	val = 0;
	gpiod_set_array_value_cansleep(lp->gds_gt_rx_dpath->ndescs,
				       lp->gds_gt_rx_dpath->desc,
				       lp->gds_gt_rx_dpath->info, &val);

	/* Tx and Rx serdes reset */
	gpiod_set_array_value_cansleep(lp->gds_gt_rsts->ndescs,
				       lp->gds_gt_rsts->desc,
				       lp->gds_gt_rsts->info, &val);
	mdelay(DELAY_1MS);

	ret = readx_poll_timeout(dcmac_gt_rx_reset_status, lp, val,
				 val == (ulong)DCMAC_GT_RESET_DONE_MASK, 10,
				 100 * DELAY_OF_ONE_MILLISEC);
	if (ret) {
		netdev_err(ndev,
			   "GT RX reset done not achieved (Status = 0x%lx)\n",
			   val);
		return ret;
	}

	mdelay(DELAY_1MS);
	/* Assert and deassert DCMAC Rx port reset */
	axienet_iow(lp, DCMAC_P_CTRL_RX_OFFSET,
		    DCMAC_P_CTRL_CLR_SERDES);
	mdelay(DELAY_1MS);
	axienet_iow(lp, DCMAC_P_CTRL_RX_OFFSET, 0);

	/* Delay of 2ms is needed */
	mdelay(2 * DELAY_1MS);

	/* Clear previous status */
	axienet_iow(lp, DCMAC_STS_RX_PHY_OFFSET, 0xFFFFFFFF);
	mdelay(DELAY_1MS);

	/* Read phy status for PCS alignment, Rx status and Block lock */
	val = axienet_ior(lp, DCMAC_STS_RX_PHY_OFFSET);
	return val;
}

static void dcmac_assert_reset(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u32 val;

	val = DCMAC_G_CTRL_RESET_ALL;
	axienet_iow(lp, DCMAC_G_CTRL_RX_OFFSET, val);
	axienet_iow(lp, DCMAC_G_CTRL_TX_OFFSET, val);
	val = DCMAC_P_CTRL_CLEAR_ALL;
	axienet_iow(lp, DCMAC_P_CTRL_RX_OFFSET, val);
	axienet_iow(lp, DCMAC_P_CTRL_TX_OFFSET, val);

	/* Assert channel resets */
	val = DCMAC_CH_CTRL_CLEAR_STATE;
	axienet_iow(lp, DCMAC_CH_CTRL_RX_OFFSET, val);
	axienet_iow(lp, DCMAC_CH_CTRL_TX_OFFSET, val);
	mdelay(DELAY_1MS);
	val = DCMAC_P_CTRL_CLEAR_ALL;
	axienet_iow(lp, DCMAC_P_CTRL_RX_OFFSET, val);
	axienet_iow(lp, DCMAC_P_CTRL_TX_OFFSET, val);
}

static void dcmac_release_reset(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);

	/* Release DCMAC global port and channel reset */
	axienet_iow(lp, DCMAC_G_CTRL_TX_OFFSET, DCMAC_RELEASE_RESET);
	axienet_iow(lp, DCMAC_G_CTRL_RX_OFFSET, DCMAC_RELEASE_RESET);
	axienet_iow(lp, DCMAC_P_CTRL_TX_OFFSET, DCMAC_RELEASE_RESET);
	axienet_iow(lp, DCMAC_P_CTRL_RX_OFFSET, DCMAC_RELEASE_RESET);
	mdelay(DELAY_1MS);
	axienet_iow(lp, DCMAC_CH_CTRL_TX_OFFSET, DCMAC_RELEASE_RESET);
	axienet_iow(lp, DCMAC_CH_CTRL_RX_OFFSET, DCMAC_RELEASE_RESET);
	mdelay(DELAY_1MS);
}

static int dcmac_gt_reset(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	ulong val_gpio;
	u32 ret;

	val_gpio = (DCMAC_GT_RESET_ALL | DCMAC_GT_TX_PRECURSOR |
	       DCMAC_GT_TX_POSTCURSOR | DCMAC_GT_MAINCURSOR);
	gpiod_set_array_value_cansleep(lp->gds_gt_ctrl->ndescs,
				       lp->gds_gt_ctrl->desc,
				       lp->gds_gt_ctrl->info, &val_gpio);
	mdelay(DELAY_1MS);

	val_gpio &= ~DCMAC_GT_RESET_ALL;
	gpiod_set_array_value_cansleep(lp->gds_gt_ctrl->ndescs,
				       lp->gds_gt_ctrl->desc,
				       lp->gds_gt_ctrl->info, &val_gpio);

	/* Ensure the GT TX Datapath Reset is not asserted */
	val_gpio = 0;
	gpiod_set_array_value_cansleep(lp->gds_gt_tx_dpath->ndescs,
				       lp->gds_gt_tx_dpath->desc,
				       lp->gds_gt_tx_dpath->info, &val_gpio);

	mdelay(DELAY_1MS);

	/* Check for GT TX RESET DONE */
	ret = readx_poll_timeout(dcmac_gt_tx_reset_status, lp, val_gpio,
				 val_gpio == (ulong)DCMAC_GT_RESET_DONE_MASK,
				 10, 100 * DELAY_OF_ONE_MILLISEC);
	if (ret) {
		netdev_err(ndev,
			   "GT TX Reset Done not achieved (Status = 0x%lx)\n",
			   val_gpio);
		return ret;
	}

	/* Check for GT RX RESET DONE */
	ret = readx_poll_timeout(dcmac_gt_rx_reset_status, lp, val_gpio,
				 val_gpio == (ulong)DCMAC_GT_RESET_DONE_MASK, 10,
				 100 * DELAY_OF_ONE_MILLISEC);
	if (ret) {
		netdev_err(ndev,
			   "GT RX Reset Done not achieved (Status = 0x%lx)\n",
			   val_gpio);
		return ret;
	}

	return ret;
}

static inline int axienet_mrmac_gt_reset(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u32 err, val;
	int i;

	if (mrmac_pll_rst == 0) {
		for (i = 0; i < MRMAC_MAX_GT_LANES; i++) {
			iowrite32(MRMAC_GT_RST_ALL_MASK, (lp->gt_ctrl +
				  (MRMAC_GT_LANE_OFFSET * i) +
				  MRMAC_GT_CTRL_OFFSET));
			mdelay(DELAY_1MS);
			iowrite32(0, (lp->gt_ctrl + (MRMAC_GT_LANE_OFFSET * i) +
				      MRMAC_GT_CTRL_OFFSET));
		}

		/* Wait for PLL lock with timeout */
		err = readl_poll_timeout(lp->gt_pll + MRMAC_GT_PLL_STS_OFFSET,
					 val, (val & MRMAC_GT_PLL_DONE_MASK),
					 10, DELAY_OF_ONE_MILLISEC * 100);
		if (err) {
			netdev_err(ndev, "MRMAC PLL lock not complete! Cross-check the MAC ref clock configuration\n");
			return -ENODEV;
		}
		mrmac_pll_rst = 1;
	}

	if (lp->max_speed == SPEED_25000)
		iowrite32(MRMAC_GT_25G_MASK, (lp->gt_ctrl +
			  MRMAC_GT_LANE_OFFSET * lp->gt_lane +
			  MRMAC_GT_RATE_OFFSET));
	else
		iowrite32(MRMAC_GT_10G_MASK, (lp->gt_ctrl +
			  MRMAC_GT_LANE_OFFSET * lp->gt_lane +
			  MRMAC_GT_RATE_OFFSET));

	iowrite32(MRMAC_GT_RST_RX_MASK | MRMAC_GT_RST_TX_MASK,
		  (lp->gt_ctrl + MRMAC_GT_LANE_OFFSET * lp->gt_lane +
		  MRMAC_GT_CTRL_OFFSET));
	mdelay(DELAY_1MS);
	iowrite32(0, (lp->gt_ctrl + MRMAC_GT_LANE_OFFSET * lp->gt_lane +
		  MRMAC_GT_CTRL_OFFSET));
	mdelay(DELAY_1MS);

	return 0;
}

static inline int xxv_gt_reset(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u32 val;

	val = axienet_ior(lp, XXV_GT_RESET_OFFSET);
	val |= XXV_GT_RESET_MASK;
	axienet_iow(lp, XXV_GT_RESET_OFFSET, val);
	/* Wait for 1ms for GT reset to complete as per spec */
	mdelay(DELAY_1MS);
	val = axienet_ior(lp, XXV_GT_RESET_OFFSET);
	val &= ~XXV_GT_RESET_MASK;
	axienet_iow(lp, XXV_GT_RESET_OFFSET, val);

	return 0;
}

int __axienet_device_reset(struct axienet_dma_q *q)
{
	struct axienet_local *lp = q->lp;
	u32 value;
	int ret;

	/* Save statistics counters in case they will be reset */
	mutex_lock(&lp->stats_lock);
	if (lp->features & XAE_FEATURE_STATS)
		axienet_stats_update(lp, true);

	/* Reset Axi DMA. This would reset Axi Ethernet core as well. The reset
	 * process of Axi DMA takes a while to complete as all pending
	 * commands/transfers will be flushed or completed during this
	 * reset process.
	 * Note that even though both TX and RX have their own reset register,
	 * they both reset the entire DMA core, so only one needs to be used.
	 */
	axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, XAXIDMA_CR_RESET_MASK);
	ret = read_poll_timeout(axienet_dma_in32, value,
				!(value & XAXIDMA_CR_RESET_MASK),
				DELAY_OF_ONE_MILLISEC, 50000, false, q,
				XAXIDMA_TX_CR_OFFSET);
	if (ret) {
		dev_err(lp->dev, "%s: DMA reset timeout!\n", __func__);
		goto out;
	}

	if (lp->axienet_config->mactype == XAXIENET_1_2p5G) {
	/* Wait for PhyRstCmplt bit to be set, indicating the PHY reset has finished */
		ret = read_poll_timeout(axienet_ior, value,
					value & XAE_INT_PHYRSTCMPLT_MASK,
					DELAY_OF_ONE_MILLISEC, 50000, false, lp,
					XAE_IS_OFFSET);
		if (ret) {
			dev_err(lp->dev, "%s: timeout waiting for PhyRstCmplt\n", __func__);
			goto out;
		}
	}

	/* Update statistics counters with new values */
	if (lp->features & XAE_FEATURE_STATS) {
		enum temac_stat stat;

		write_seqcount_begin(&lp->hw_stats_seqcount);
		lp->reset_in_progress = false;
		for (stat = 0; stat < STAT_COUNT; stat++) {
			u32 counter =
				axienet_ior(lp, XAE_STATS_OFFSET + stat * 8);

			lp->hw_stat_base[stat] +=
				lp->hw_last_counter[stat] - counter;
			lp->hw_last_counter[stat] = counter;
		}
		write_seqcount_end(&lp->hw_stats_seqcount);
	}

out:
	mutex_unlock(&lp->stats_lock);
	return ret;
}

/**
 * axienet_dma_stop - Stop DMA operation
 * @dq:		Pointer to the axienet_dma_q structure
 */
void axienet_dma_stop(struct axienet_dma_q *dq)
{
	int count;
	u32 cr, sr;
	struct axienet_local *lp = dq->lp;

	cr = axienet_dma_in32(dq, XAXIDMA_RX_CR_OFFSET);
	cr &= ~(XAXIDMA_CR_RUNSTOP_MASK | XAXIDMA_IRQ_ALL_MASK);
	axienet_dma_out32(dq, XAXIDMA_RX_CR_OFFSET, cr);
	synchronize_irq(dq->rx_irq);

	cr = axienet_dma_in32(dq, XAXIDMA_TX_CR_OFFSET);
	cr &= ~(XAXIDMA_CR_RUNSTOP_MASK | XAXIDMA_IRQ_ALL_MASK);
	axienet_dma_out32(dq, XAXIDMA_TX_CR_OFFSET, cr);
	synchronize_irq(dq->tx_irq);

	/* Give DMAs a chance to halt gracefully */
	sr = axienet_dma_in32(dq, XAXIDMA_RX_SR_OFFSET);
	for (count = 0; !(sr & XAXIDMA_SR_HALT_MASK) && count < 5; ++count) {
		msleep(20);
		sr = axienet_dma_in32(dq, XAXIDMA_RX_SR_OFFSET);
	}

	sr = axienet_dma_in32(dq, XAXIDMA_TX_SR_OFFSET);
	for (count = 0; !(sr & XAXIDMA_SR_HALT_MASK) && count < 5; ++count) {
		msleep(20);
		sr = axienet_dma_in32(dq, XAXIDMA_TX_SR_OFFSET);
	}

	/* Do a reset to ensure DMA is really stopped */
	axienet_lock_mii(lp);
	__axienet_device_reset(dq);
	axienet_unlock_mii(lp);
}

/**
 * axienet_device_reset - Reset and initialize the Axi Ethernet hardware.
 * @ndev:	Pointer to the net_device structure
 *
 * Return: 0 on success, Negative value on errors
 *
 * This function is called to reset and initialize the Axi Ethernet core. This
 * is typically called during initialization. It does a reset of the Axi DMA
 * Rx/Tx channels and initializes the Axi DMA BDs. Since Axi DMA reset lines
 * are connected to Axi Ethernet reset lines, this in turn resets the Axi
 * Ethernet core. No separate hardware reset is done for the Axi Ethernet
 * core.
 * Returns 0 on success or a negative error number otherwise.
 */
static int axienet_device_reset(struct net_device *ndev)
{
	u32 axienet_status;
	struct axienet_local *lp = netdev_priv(ndev);
	int ret;
	u32 err, val;
	u8 maj, minor;
	struct axienet_dma_q *q;
	u32 i;

	if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
		/* Reset MRMAC */
		axienet_mrmac_reset(lp);
	}

	if (lp->axienet_config->gt_reset) {
		ret = lp->axienet_config->gt_reset(ndev);
		if (ret)
			return ret;
	}

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;
	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_1G_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC) {
		lp->options |= XAE_OPTION_VLAN;
		lp->options &= (~XAE_OPTION_JUMBO);
	}

	if (ndev->mtu > XAE_MTU && ndev->mtu <= XAE_JUMBO_MTU) {
		lp->max_frm_size = ndev->mtu + VLAN_ETH_HLEN +
					XAE_TRL_SIZE;

		if (lp->max_frm_size <= lp->rxmem &&
		    lp->axienet_config->mactype != XAXIENET_10G_25G &&
		    lp->axienet_config->mactype != XAXIENET_1G_10G_25G &&
		    lp->axienet_config->mactype != XAXIENET_MRMAC)
			lp->options |= XAE_OPTION_JUMBO;
	}

	if (!lp->use_dmaengine) {
		for_each_rx_dma_queue(lp, i) {
			q = lp->dq[i];
			__axienet_device_reset(q);
		}

		ret = axienet_dma_bd_init(ndev);
		if (ret) {
			netdev_err(ndev, "%s: descriptor allocation failed\n",
				   __func__);
			return ret;
		}
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_1G_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC &&
	    lp->axienet_config->mactype != XAXIENET_DCMAC) {
		axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
		axienet_status &= ~XAE_RCW1_RX_MASK;
		axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);
	}

	if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_1G_10G_25G) {
		/* Check for block lock bit got set or not
		 * This ensures that 10G ethernet IP
		 * is functioning normally or not.
		 * IP version 3.2 and above, check GT status
		 * before reading any register
		 */
		maj = lp->xxv_ip_version & XXV_MAJ_MASK;
		minor = (lp->xxv_ip_version & XXV_MIN_MASK) >> 8;

		if (maj == 3 ? minor >= 2 : maj > 3) {
			err = readl_poll_timeout(lp->regs + XXV_STAT_GTWIZ_OFFSET,
						 val, (val & XXV_GTWIZ_RESET_DONE),
						 10, DELAY_OF_ONE_MILLISEC);
			if (err) {
				netdev_err(ndev, "XXV MAC GT reset not complete! Cross-check the MAC ref clock configuration\n");
				axienet_dma_bd_release(ndev);
				return err;
			}
		}
		err = readl_poll_timeout(lp->regs + XXV_STATRX_BLKLCK_OFFSET,
					 val, (val & XXV_RX_BLKLCK_MASK),
					 10, DELAY_OF_ONE_MILLISEC);
		if (err)
			netdev_err(ndev, "XXV MAC block lock not complete! Cross-check the MAC ref clock configuration\n");

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		axienet_rxts_iow(lp, XAXIFIFO_TXTS_RDFR,
				 XAXIFIFO_TXTS_RESET_MASK);
		axienet_rxts_iow(lp, XAXIFIFO_TXTS_SRR,
				 XAXIFIFO_TXTS_RESET_MASK);
		axienet_txts_iow(lp, XAXIFIFO_TXTS_RDFR,
				 XAXIFIFO_TXTS_RESET_MASK);
		axienet_txts_iow(lp, XAXIFIFO_TXTS_SRR,
				 XAXIFIFO_TXTS_RESET_MASK);
#endif
	}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
		axienet_rxts_iow(lp, XAXIFIFO_TXTS_RDFR,
				 XAXIFIFO_TXTS_RESET_MASK);
		axienet_rxts_iow(lp, XAXIFIFO_TXTS_SRR,
				 XAXIFIFO_TXTS_RESET_MASK);
		axienet_txts_iow(lp, XAXIFIFO_TXTS_RDFR,
				 XAXIFIFO_TXTS_RESET_MASK);
		axienet_txts_iow(lp, XAXIFIFO_TXTS_SRR,
				 XAXIFIFO_TXTS_RESET_MASK);
	}
#endif

	if (lp->axienet_config->mactype == XAXIENET_1_2p5G &&
	    !lp->eth_hasnobuf) {
		axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
		if (axienet_status & XAE_INT_RXRJECT_MASK)
			axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
		/* Enable receive erros */
		axienet_iow(lp, XAE_IE_OFFSET, lp->eth_irq > 0 ?
			    XAE_INT_RECV_ERROR_MASK : 0);
	}

	if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_1G_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_MRMAC) {
		lp->options |= XAE_OPTION_FCS_STRIP;
		lp->options |= XAE_OPTION_FCS_INSERT;
	} else {
		axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);
	}
	if (lp->axienet_config->setoptions)
		lp->axienet_config->setoptions(ndev, lp->options &
					~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	if (lp->axienet_config->mactype == XAXIENET_DCMAC) {
		dcmac_assert_reset(ndev);
		dcmac_init(lp);
		dcmac_release_reset(ndev);

		/* Check for alignment */
		ret = readx_poll_timeout(dcmac_rx_phy_status, ndev, val,
					 (val > 0) &&
					 (val & DCMAC_RXPHY_RX_STS_MASK) &&
					 (val & DCMAC_RXPHY_RX_ALIGN_MASK),
					 10, 100 * DELAY_OF_ONE_MILLISEC);

		if (ret) {
			netdev_err(ndev, "Alignment not achieved. Failed to reset DCMAC\n");
			return -ENODEV;
		}
	}

	if (lp->axienet_config->setoptions)
		lp->axienet_config->setoptions(ndev, lp->options);

	netif_trans_update(ndev);

	return 0;
}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
/**
 * axienet_tx_hwtstamp - Read tx timestamp from hw and update it to the skbuff
 * @lp:		Pointer to axienet local structure
 * @cur_p:	Pointer to the axi_dma/axi_mcdma current bd
 *
 * Return:	None.
 */
#ifdef CONFIG_AXIENET_HAS_MCDMA
void axienet_tx_hwtstamp(struct axienet_local *lp,
			 struct aximcdma_bd *cur_p)
#else
void axienet_tx_hwtstamp(struct axienet_local *lp,
			 struct axidma_bd *cur_p)
#endif
{
	u32 sec = 0, nsec = 0, val;
	u64 time64;
	int err = 0;
	u32 count, len = lp->axienet_config->tx_ptplen;
	struct skb_shared_hwtstamps *shhwtstamps =
		skb_hwtstamps((struct sk_buff *)cur_p->ptp_tx_skb);

	val = axienet_txts_ior(lp, XAXIFIFO_TXTS_ISR);
	if (unlikely(!(val & XAXIFIFO_TXTS_INT_RC_MASK)))
		dev_info(lp->dev, "Did't get FIFO tx interrupt %d\n", val);

	/* Ensure to read Occupany register before accessing Length register */
	if (!axienet_txts_ior(lp, XAXIFIFO_TXTS_RFO)) {
		netdev_err(lp->ndev, "%s: TX Timestamp FIFO is empty", __func__);
		goto skb_exit;
	}

	/* If FIFO is configured in cut through Mode we will get Rx complete
	 * interrupt even one byte is there in the fifo wait for the full packet
	 */
	err = readl_poll_timeout_atomic(lp->tx_ts_regs + XAXIFIFO_TXTS_RLR, val,
					((val & XAXIFIFO_TXTS_RXFD_MASK) >=
					len), 0, 1000000);
	if (err) {
		netdev_err(lp->ndev, "%s: Didn't get the full timestamp packet",
			   __func__);
		goto skb_exit;
	}

	nsec = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	sec  = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	val = ((val & XAXIFIFO_TXTS_TAG_MASK) >> XAXIFIFO_TXTS_TAG_SHIFT);
	dev_dbg(lp->dev, "tx_stamp:[%04x] %04x %u %9u\n",
		cur_p->ptp_tx_ts_tag, val, sec, nsec);

	if (val != cur_p->ptp_tx_ts_tag) {
		count = axienet_txts_ior(lp, XAXIFIFO_TXTS_RFO);
		while (count) {
			nsec = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			sec  = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			val = ((val & XAXIFIFO_TXTS_TAG_MASK) >>
				XAXIFIFO_TXTS_TAG_SHIFT);

			dev_dbg(lp->dev, "tx_stamp:[%04x] %04x %u %9u\n",
				cur_p->ptp_tx_ts_tag, val, sec, nsec);
			if (val == cur_p->ptp_tx_ts_tag)
				break;
			count = axienet_txts_ior(lp, XAXIFIFO_TXTS_RFO);
		}
		if (val != cur_p->ptp_tx_ts_tag) {
			dev_info(lp->dev, "Mismatching 2-step tag. Got %x",
				 val);
			dev_info(lp->dev, "Expected %x\n",
				 cur_p->ptp_tx_ts_tag);
		}
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC)
		val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);

skb_exit:
	time64 = sec * NS_PER_SEC + nsec;
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = ns_to_ktime(time64);
	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC)
		skb_pull((struct sk_buff *)cur_p->ptp_tx_skb,
			 AXIENET_TS_HEADER_LEN);

	skb_tstamp_tx((struct sk_buff *)cur_p->ptp_tx_skb, shhwtstamps);
	dev_kfree_skb_any((struct sk_buff *)cur_p->ptp_tx_skb);
	cur_p->ptp_tx_skb = 0;
}

static inline bool is_ptp_os_pdelay_req(struct sk_buff *skb,
					struct axienet_local *lp)
{
	u8 *msg_type;

	msg_type = (u8 *)skb->data + PTP_TYPE_OFFSET;
	return (((*msg_type & 0xF) == PTP_TYPE_PDELAY_REQ) &&
		(lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_P2P));
}

/**
 * axienet_rx_hwtstamp - Read rx timestamp from hw and update it to the skbuff
 * @lp:		Pointer to axienet local structure
 * @skb:	Pointer to the sk_buff structure
 *
 * Return:	None.
 */
static void axienet_rx_hwtstamp(struct axienet_local *lp,
				struct sk_buff *skb)
{
	u32 sec = 0, nsec = 0, val;
	u64 time64;
	int err = 0;
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);

	val = axienet_rxts_ior(lp, XAXIFIFO_TXTS_ISR);
	if (unlikely(!(val & XAXIFIFO_TXTS_INT_RC_MASK))) {
		dev_info(lp->dev, "Did't get FIFO rx interrupt %d\n", val);
		return;
	}

	val = axienet_rxts_ior(lp, XAXIFIFO_TXTS_RFO);
	if (!val)
		return;

	/* If FIFO is configured in cut through Mode we will get Rx complete
	 * interrupt even one byte is there in the fifo wait for the full packet
	 */
	err = readl_poll_timeout_atomic(lp->rx_ts_regs + XAXIFIFO_TXTS_RLR, val,
					((val & XAXIFIFO_TXTS_RXFD_MASK) >= 12),
					0, 1000000);
	if (err) {
		netdev_err(lp->ndev, "%s: Didn't get the full timestamp packet",
			   __func__);
		return;
	}

	nsec = axienet_rxts_ior(lp, XAXIFIFO_TXTS_RXFD);
	sec  = axienet_rxts_ior(lp, XAXIFIFO_TXTS_RXFD);
	val = axienet_rxts_ior(lp, XAXIFIFO_TXTS_RXFD);

	if (is_ptp_os_pdelay_req(skb, lp)) {
		/* Need to save PDelay resp RX time for HW 1 step
		 * timestamping on PDelay Response.
		 */
		lp->ptp_os_cf = mul_u32_u32(sec, NSEC_PER_SEC);
		lp->ptp_os_cf += nsec;
		lp->ptp_os_cf = (lp->ptp_os_cf << 16);
	}

	if (lp->tstamp_config.rx_filter == HWTSTAMP_FILTER_ALL) {
		time64 = sec * NS_PER_SEC + nsec;
		shhwtstamps->hwtstamp = ns_to_ktime(time64);
	}
}
#endif

/**
 * axienet_free_tx_chain - Clean up a series of linked TX descriptors.
 * @q:		Pointer to the axienet_dma_q structure
 * @first_bd:	Index of first descriptor to clean up
 * @nr_bds:	Max number of descriptors to clean up
 * @force:	Whether to clean descriptors even if not complete
 * @sizep:	Pointer to a u32 filled with the total sum of all bytes
 *		in all cleaned-up descriptors. Ignored if NULL.
 * @budget:	NAPI budget (use 0 when not called from NAPI poll)
 *
 * Would either be called after a successful transmit operation, or after
 * there was an error when setting up the chain.
 * Returns the number of packets handled.
 */

static int axienet_free_tx_chain(struct axienet_dma_q *q, u32 first_bd,
				 int nr_bds, bool force, u32 *sizep, int budget)
{
	struct axienet_local *lp = q->lp;
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	unsigned int status;
	int i, packets = 0;
	dma_addr_t phys;

	for (i = 0; i < nr_bds; i++) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p = &q->txq_bd_v[(first_bd + i) % lp->tx_bd_num];
		status = cur_p->sband_stats;
#else
		cur_p = &q->tx_bd_v[(first_bd + i) % lp->tx_bd_num];
		status = cur_p->status;
#endif

		/* If force is not specified, clean up only descriptors
		 * that have been completed by the MAC.
		 */
		if (!force && !(status & XAXIDMA_BD_STS_COMPLETE_MASK))
			break;
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		if (cur_p->ptp_tx_skb)
			axienet_tx_hwtstamp(lp, cur_p);
#endif
		/* Ensure we see complete descriptor update */
		dma_rmb();
#ifdef CONFIG_AXIENET_HAS_MCDMA
		phys = mcdma_desc_get_phys_addr(lp, cur_p);
#else
		phys = desc_get_phys_addr(lp, cur_p);
#endif

		if (cur_p->tx_desc_mapping == DESC_DMA_MAP_PAGE)
			dma_unmap_page(lp->dev, phys,
				       cur_p->cntrl &
				       XAXIDMA_BD_CTRL_LENGTH_MASK,
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(lp->dev, phys,
					 cur_p->cntrl &
					 XAXIDMA_BD_CTRL_LENGTH_MASK,
					 DMA_TO_DEVICE);

		if (cur_p->tx_skb && (status & XAXIDMA_BD_STS_COMPLETE_MASK)) {
			napi_consume_skb((struct sk_buff *)cur_p->tx_skb, budget);
			packets++;
		}

		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app4 = 0;
		cur_p->tx_skb = 0;

		/* ensure our transmit path and device don't prematurely see status cleared */
		wmb();
		cur_p->cntrl = 0;
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p->sband_stats = 0;
#endif
		cur_p->status = 0;
		if (sizep)
			*sizep += status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
	}

	if (!force) {
		q->tx_bd_ci += i;
		if (q->tx_bd_ci >= lp->tx_bd_num)
			q->tx_bd_ci %= lp->tx_bd_num;
	}

	return packets;
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
 * returns a busy status.
 */
static inline int axienet_check_tx_bd_space(struct axienet_dma_q *q,
					    int num_frag)
{
	struct axienet_local *lp = q->lp;
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;

	if (CIRC_SPACE(q->tx_bd_tail, q->tx_bd_ci, lp->tx_bd_num) < (num_frag + 1))
		return NETDEV_TX_BUSY;

	cur_p = &q->txq_bd_v[(q->tx_bd_tail + num_frag) % lp->tx_bd_num];
	if (cur_p->sband_stats & XMCDMA_BD_STS_ALL_MASK)
		return NETDEV_TX_BUSY;
#else
	struct axidma_bd *cur_p;

	if (CIRC_SPACE(q->tx_bd_tail, q->tx_bd_ci, lp->tx_bd_num) < (num_frag + 1))
		return NETDEV_TX_BUSY;

	cur_p = &q->tx_bd_v[(q->tx_bd_tail + num_frag) % lp->tx_bd_num];
	if (cur_p->status & XAXIDMA_BD_STS_ALL_MASK)
		return NETDEV_TX_BUSY;
#endif
	return 0;
}

/**
 * axienet_dma_tx_cb - DMA engine callback for TX channel.
 * @data:       Pointer to the axienet_local structure.
 * @result:     error reporting through dmaengine_result.
 * This function is called by dmaengine driver for TX channel to notify
 * that the transmit is done.
 */
static void axienet_dma_tx_cb(void *data, const struct dmaengine_result *result)
{
	struct skbuf_dma_descriptor *skbuf_dma;
	struct axienet_local *lp = data;
	struct netdev_queue *txq;
	int len;

	skbuf_dma = axienet_get_tx_desc(lp, lp->tx_ring_tail++);
	len = skbuf_dma->skb->len;
	txq = skb_get_tx_queue(lp->ndev, skbuf_dma->skb);
	u64_stats_update_begin(&lp->tx_stat_sync);
	u64_stats_add(&lp->tx_bytes, len);
	u64_stats_add(&lp->tx_packets, 1);
	u64_stats_update_end(&lp->tx_stat_sync);
	dma_unmap_sg(lp->dev, skbuf_dma->sgl, skbuf_dma->sg_len, DMA_TO_DEVICE);
	dev_consume_skb_any(skbuf_dma->skb);
	netif_txq_completed_wake(txq, 1, len,
				 CIRC_SPACE(lp->tx_ring_head, lp->tx_ring_tail, TX_BD_NUM_MAX),
				 2);
}

/**
 * axienet_start_xmit_dmaengine - Starts the transmission.
 * @skb:        sk_buff pointer that contains data to be Txed.
 * @ndev:       Pointer to net_device structure.
 *
 * Return: NETDEV_TX_OK on success or any non space errors.
 *         NETDEV_TX_BUSY when free element in TX skb ring buffer
 *         is not available.
 *
 * This function is invoked to initiate transmission. The
 * function sets the skbs, register dma callback API and submit
 * the dma transaction.
 * Additionally if checksum offloading is supported,
 * it populates AXI Stream Control fields with appropriate values.
 */
static netdev_tx_t
axienet_start_xmit_dmaengine(struct sk_buff *skb, struct net_device *ndev)
{
	struct dma_async_tx_descriptor *dma_tx_desc = NULL;
	struct axienet_local *lp = netdev_priv(ndev);
	u32 app_metadata[DMA_NUM_APP_WORDS] = {0};
	struct skbuf_dma_descriptor *skbuf_dma;
	struct dma_device *dma_dev;
	struct netdev_queue *txq;
	u32 csum_start_off;
	u32 csum_index_off;
	int sg_len;
	int ret;

	dma_dev = lp->tx_chan->device;
	sg_len = skb_shinfo(skb)->nr_frags + 1;
	if (CIRC_SPACE(lp->tx_ring_head, lp->tx_ring_tail, TX_BD_NUM_MAX) <= 1) {
		netif_stop_queue(ndev);
		if (net_ratelimit())
			netdev_warn(ndev, "TX ring unexpectedly full\n");
		return NETDEV_TX_BUSY;
	}

	skbuf_dma = axienet_get_tx_desc(lp, lp->tx_ring_head);
	if (!skbuf_dma)
		goto xmit_error_drop_skb;

	lp->tx_ring_head++;
	sg_init_table(skbuf_dma->sgl, sg_len);
	ret = skb_to_sgvec(skb, skbuf_dma->sgl, 0, skb->len);
	if (ret < 0)
		goto xmit_error_drop_skb;

	ret = dma_map_sg(lp->dev, skbuf_dma->sgl, sg_len, DMA_TO_DEVICE);
	if (!ret)
		goto xmit_error_drop_skb;

	/* Fill up app fields for checksum */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (lp->features & XAE_FEATURE_FULL_TX_CSUM) {
			/* Tx Full Checksum Offload Enabled */
			app_metadata[0] |= 2;
		} else if (lp->features & XAE_FEATURE_PARTIAL_TX_CSUM) {
			csum_start_off = skb_transport_offset(skb);
			csum_index_off = csum_start_off + skb->csum_offset;
			/* Tx Partial Checksum Offload Enabled */
			app_metadata[0] |= 1;
			app_metadata[1] = (csum_start_off << 16) | csum_index_off;
		}
	} else if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		app_metadata[0] |= 2; /* Tx Full Checksum Offload Enabled */
	}

	dma_tx_desc = dma_dev->device_prep_slave_sg(lp->tx_chan, skbuf_dma->sgl,
			sg_len, DMA_MEM_TO_DEV,
			DMA_PREP_INTERRUPT, (void *)app_metadata);
	if (!dma_tx_desc)
		goto xmit_error_unmap_sg;

	skbuf_dma->skb = skb;
	skbuf_dma->sg_len = sg_len;
	dma_tx_desc->callback_param = lp;
	dma_tx_desc->callback_result = axienet_dma_tx_cb;
	txq = skb_get_tx_queue(lp->ndev, skb);
	netdev_tx_sent_queue(txq, skb->len);
	netif_txq_maybe_stop(txq, CIRC_SPACE(lp->tx_ring_head, lp->tx_ring_tail, TX_BD_NUM_MAX),
			     1, 2);

	dmaengine_submit(dma_tx_desc);
	dma_async_issue_pending(lp->tx_chan);
	return NETDEV_TX_OK;

xmit_error_unmap_sg:
	dma_unmap_sg(lp->dev, skbuf_dma->sgl, sg_len, DMA_TO_DEVICE);
xmit_error_drop_skb:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * axienet_tx_poll - Invoked once a transmit is completed by the
 * Axi DMA Tx channel.
 * @napi:	Pointer to NAPI structure.
 * @budget:	Max number of TX packets to process.
 *
 * Return: Number of TX packets processed.
 *
 * This function is invoked from the NAPI processing to notify the completion
 * of transmit operation. It clears fields in the corresponding Tx BDs and
 * unmaps the corresponding buffer so that CPU can regain ownership of the
 * buffer. It finally invokes "netif_wake_queue" to restart transmission if
 * required.
 */
int axienet_tx_poll(struct napi_struct *napi, int budget)
{
	struct axienet_dma_q *q = container_of(napi, struct axienet_dma_q, napi_tx);
	struct axienet_local *lp = q->lp;
	struct net_device *ndev = lp->ndev;
	u32 size = 0;
	int packets;

	packets = axienet_free_tx_chain(q, q->tx_bd_ci, lp->tx_bd_num, false,
					&size, budget);
	if (packets) {
		u64_stats_update_begin(&lp->tx_stat_sync);
		u64_stats_add(&lp->tx_packets, packets);
		u64_stats_add(&lp->tx_bytes, size);
		u64_stats_update_end(&lp->tx_stat_sync);
		ndev->stats.tx_packets += packets;
		ndev->stats.tx_bytes += size;
		q->txq_packets += packets;
		q->txq_bytes += size;

		/* Matches barrier in axienet_start_xmit */
		smp_mb();

		if (!axienet_check_tx_bd_space(q, MAX_SKB_FRAGS + 1))
			netif_wake_queue(ndev);
	}

	if (packets < budget && napi_complete_done(napi, packets)) {
		/* Re-enable TX completion interrupts. This should
		 * cause an immediate interrupt if any TX packets are
		 * already pending.
		 */
#ifdef CONFIG_AXIENET_HAS_MCDMA
		u32 cr;

		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id));
		cr |= (XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK);
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id), cr);
#else
		axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, q->tx_dma_cr);

#endif
	}
	return packets;
}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
/**
 * axienet_create_tsheader - Create timestamp header for tx
 * @q:		Pointer to DMA queue structure
 * @buf:	Pointer to the buf to copy timestamp header
 * @msg_type:	PTP message type
 *
 * Return: 0, on success
 *	    NETDEV_TX_BUSY, if timestamp FIFO has no vacancy
 */
static int axienet_create_tsheader(u8 *buf, u8 msg_type,
				   struct axienet_dma_q *q)
{
	struct axienet_local *lp = q->lp;
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	u64 val;
	u32 tmp[MRMAC_TS_HEADER_WORDS];
	int i;
	unsigned long flags;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
	cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif

	if ((msg_type & 0xF) == TX_TS_OP_NOOP) {
		buf[0] = TX_TS_OP_NOOP;
	} else if ((msg_type & 0xF) == TX_TS_OP_ONESTEP) {
		if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
			/* For Sync Packet */
			if ((msg_type & 0xF0) == MSG_TYPE_SYNC_FLAG) {
				buf[0] = TX_TS_OP_ONESTEP | TX_TS_CSUM_UPDATE_MRMAC;
				buf[2] = cur_p->ptp_tx_ts_tag & 0xFF;
				buf[3] = (cur_p->ptp_tx_ts_tag >> 8) & 0xFF;
				buf[4] = TX_PTP_CF_OFFSET;
				buf[6] = TX_PTP_CSUM_OFFSET;
			}
			/* For PDelay Response packet */
			if ((msg_type & 0xF0) == MSG_TYPE_PDELAY_RESP_FLAG) {
				buf[0] = TX_TS_OP_ONESTEP | TX_TS_CSUM_UPDATE_MRMAC |
					TX_TS_PDELAY_UPDATE_MRMAC;
				buf[2] = cur_p->ptp_tx_ts_tag & 0xFF;
				buf[3] = (cur_p->ptp_tx_ts_tag >> 8) & 0xFF;
				buf[4] = TX_PTP_CF_OFFSET;
				buf[6] = TX_PTP_CSUM_OFFSET;
				/* Prev saved TS */
				memcpy(&buf[8], &lp->ptp_os_cf, 8);
			}
		} else {
			/* Legacy */
			buf[0] = TX_TS_OP_ONESTEP;
			buf[1] = TX_TS_CSUM_UPDATE;
			buf[4] = TX_PTP_TS_OFFSET;
			buf[6] = TX_PTP_CSUM_OFFSET;
		}
	} else {
		buf[0] = TX_TS_OP_TWOSTEP;
		buf[2] = cur_p->ptp_tx_ts_tag & 0xFF;
		buf[3] = (cur_p->ptp_tx_ts_tag >> 8) & 0xFF;
	}

	if (lp->axienet_config->mactype == XAXIENET_1_2p5G) {
		memcpy(&val, buf, AXIENET_TS_HEADER_LEN);
		swab64s(&val);
		memcpy(buf, &val, AXIENET_TS_HEADER_LEN);
	} else if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
		   lp->axienet_config->mactype == XAXIENET_MRMAC) {
		memcpy(&tmp[0], buf, lp->axienet_config->ts_header_len);
		/* Check for Transmit Data FIFO Vacancy */
		spin_lock_irqsave(&lp->ptp_tx_lock, flags);
		if (!axienet_txts_ior(lp, XAXIFIFO_TXTS_TDFV)) {
			spin_unlock_irqrestore(&lp->ptp_tx_lock, flags);
			return NETDEV_TX_BUSY;
		}
		for (i = 0; i < lp->axienet_config->ts_header_len / 4; i++)
			axienet_txts_iow(lp, XAXIFIFO_TXTS_TXFD, tmp[i]);

		axienet_txts_iow(lp, XAXIFIFO_TXTS_TLR, lp->axienet_config->ts_header_len);
		spin_unlock_irqrestore(&lp->ptp_tx_lock, flags);
	}

	return 0;
}
#endif

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
static inline u8 ptp_os(struct sk_buff *skb, struct axienet_local *lp)
{
	u8 *msg_type;
	int packet_flags = 0;

	/* Identify and return packets requiring PTP one step TS */
	msg_type = (u8 *)skb->data + PTP_TYPE_OFFSET;
	if ((*msg_type & 0xF) == PTP_TYPE_SYNC)
		packet_flags = MSG_TYPE_SYNC_FLAG;
	else if (((*msg_type & 0xF) == PTP_TYPE_PDELAY_RESP) &&
		 (lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_P2P))
		packet_flags = MSG_TYPE_PDELAY_RESP_FLAG;

	return packet_flags;
}

static int axienet_skb_tstsmp(struct sk_buff **__skb, struct axienet_dma_q *q,
			      struct net_device *ndev)
{
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	struct axienet_local *lp = netdev_priv(ndev);
	struct sk_buff *old_skb = *__skb;
	struct sk_buff *skb = *__skb;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
	cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif

	if (((lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_SYNC ||
	      lp->tstamp_config.tx_type == HWTSTAMP_TX_ON) ||
	     lp->eth_hasptp) && lp->axienet_config->mactype !=
	    XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC) {
		u8 *tmp;
		struct sk_buff *new_skb;

		if (skb_headroom(old_skb) < AXIENET_TS_HEADER_LEN) {
			new_skb =
			skb_realloc_headroom(old_skb,
					     AXIENET_TS_HEADER_LEN);
			if (!new_skb) {
				dev_err(&ndev->dev, "failed to allocate new socket buffer\n");
				dev_kfree_skb_any(old_skb);
				return NETDEV_TX_BUSY;
			}

			/*  Transfer the ownership to the
			 *  new socket buffer if required
			 */
			if (old_skb->sk)
				skb_set_owner_w(new_skb, old_skb->sk);
			dev_kfree_skb_any(old_skb);
			*__skb = new_skb;
			skb = new_skb;
		}

		tmp = skb_push(skb, AXIENET_TS_HEADER_LEN);
		memset(tmp, 0, AXIENET_TS_HEADER_LEN);
		cur_p->ptp_tx_ts_tag++;

		if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
			if (lp->tstamp_config.tx_type ==
				HWTSTAMP_TX_ONESTEP_SYNC) {
				axienet_create_tsheader(tmp,
							TX_TS_OP_ONESTEP
							, q);
			} else {
				axienet_create_tsheader(tmp,
							TX_TS_OP_TWOSTEP, q);
				skb_shinfo(skb)->tx_flags |=
						SKBTX_IN_PROGRESS;
				cur_p->ptp_tx_skb = skb_get(skb);
			}
		}
	} else if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		   (lp->axienet_config->mactype == XAXIENET_10G_25G ||
		   lp->axienet_config->mactype == XAXIENET_MRMAC)) {
		cur_p->ptp_tx_ts_tag = get_random_u32_below(XAXIFIFO_TXTS_TAG_MAX) + 1;
			dev_dbg(lp->dev, "tx_tag:[%04x]\n",
				cur_p->ptp_tx_ts_tag);
			if (lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_SYNC ||
			    lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_P2P) {
				u8 packet_flags = ptp_os(skb, lp);

				/* Pass one step flag with packet type (sync/pdelay resp)
				 * to command FIFO helper only when one step TS is required.
				 * Pass the default two step flag for other PTP events.
				 */
				if (!packet_flags)
					packet_flags = TX_TS_OP_TWOSTEP;
				else
					packet_flags |= TX_TS_OP_ONESTEP;

				if (axienet_create_tsheader(lp->tx_ptpheader,
							    packet_flags,
							    q))
					return NETDEV_TX_BUSY;

				/* skb TS passing is required for non one step TS packets */
				if (packet_flags == TX_TS_OP_TWOSTEP) {
					skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
					cur_p->ptp_tx_skb = skb_get(skb);
				}
			} else {
				if (axienet_create_tsheader(lp->tx_ptpheader,
							    TX_TS_OP_TWOSTEP,
							    q))
					return NETDEV_TX_BUSY;
				skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
				cur_p->ptp_tx_skb = skb_get(skb);
			}
	} else if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
		   lp->axienet_config->mactype == XAXIENET_MRMAC) {
		dev_dbg(lp->dev, "tx_tag:NOOP\n");
			if (axienet_create_tsheader(lp->tx_ptpheader,
						    TX_TS_OP_NOOP, q))
				return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}
#endif

static int axienet_queue_xmit(struct sk_buff *skb,
			      struct net_device *ndev, u16 map)
{
	u32 ii;
	u32 num_frag;
	u32 csum_start_off;
	u32 csum_index_off;
	dma_addr_t tail_p, phys;
	struct axienet_local *lp = netdev_priv(ndev);
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	unsigned long flags;
	struct axienet_dma_q *q;

	if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_1G_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_MRMAC ||
	    lp->axienet_config->mactype == XAXIENET_DCMAC) {
		/* Need to manually pad the small frames in case of XXV MAC
		 * because the pad field is not added by the IP. We must present
		 * a packet that meets the minimum length to the IP core.
		 * When the IP core is configured to calculate and add the FCS
		 * to the packet the minimum packet length is 60 bytes.
		 */
		if (eth_skb_pad(skb)) {
			ndev->stats.tx_dropped++;
			ndev->stats.tx_errors++;
			return NETDEV_TX_OK;
		}
	}
	num_frag = skb_shinfo(skb)->nr_frags;

	q = lp->dq[map];

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
	cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif
	spin_lock_irqsave(&q->tx_lock, flags);
	if (axienet_check_tx_bd_space(q, num_frag)) {
		if (netif_queue_stopped(ndev)) {
			spin_unlock_irqrestore(&q->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}
		netif_stop_queue(ndev);

		/* Matches barrier in axienet_free_tx_chain */
		smp_mb();

		/* Space might have just been freed - check again */
		if (axienet_check_tx_bd_space(q, num_frag)) {
			spin_unlock_irqrestore(&q->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}

		netif_wake_queue(ndev);
	}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (axienet_skb_tstsmp(&skb, q, ndev)) {
		spin_unlock_irqrestore(&q->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}
#endif

	if (skb->ip_summed == CHECKSUM_PARTIAL && !lp->eth_hasnobuf &&
	    lp->axienet_config->mactype == XAXIENET_1_2p5G &&
	    !(lp->eoe_connected)) {
		if (lp->features & XAE_FEATURE_FULL_TX_CSUM) {
			/* Tx Full Checksum Offload Enabled */
			cur_p->app0 |= 2;
		} else if (lp->features & XAE_FEATURE_PARTIAL_TX_CSUM) {
			csum_start_off = skb_transport_offset(skb);
			csum_index_off = csum_start_off + skb->csum_offset;
			/* Tx Partial Checksum Offload Enabled */
			cur_p->app0 |= 1;
			cur_p->app1 = (csum_start_off << 16) | csum_index_off;
		}
	} else if (skb->ip_summed == CHECKSUM_UNNECESSARY &&
		   !lp->eth_hasnobuf &&
		   (lp->axienet_config->mactype == XAXIENET_1_2p5G) &&
		   !(lp->eoe_connected)) {
		cur_p->app0 |= 2; /* Tx Full Checksum Offload Enabled */
	}

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p->cntrl = (skb_headlen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK);
#else
	cur_p->cntrl = (skb_headlen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK);
#endif

	if (!q->eth_hasdre &&
	    (((uintptr_t)skb->data & 0x3) || num_frag > 0)) {
		skb_copy_and_csum_dev(skb, q->tx_buf[q->tx_bd_tail]);

		phys = q->tx_bufs_dma + (q->tx_buf[q->tx_bd_tail] - q->tx_bufs);

#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p->cntrl = skb_pagelen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK;
		mcdma_desc_set_phys_addr(lp, phys, cur_p);
#else
		desc_set_phys_addr(lp, phys, cur_p);
		cur_p->cntrl = skb_pagelen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK;
#endif
		goto out;
	} else {
		phys = dma_map_single(ndev->dev.parent, skb->data,
				      skb_headlen(skb), DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(ndev->dev.parent, phys))) {
			phys = 0;
#ifdef CONFIG_AXIENET_HAS_MCDMA
			mcdma_desc_set_phys_addr(lp, phys, cur_p);
#else
			desc_set_phys_addr(lp, phys, cur_p);
#endif

			spin_unlock_irqrestore(&q->tx_lock, flags);
			dev_err(&ndev->dev, "TX buffer map failed\n");
			return NETDEV_TX_BUSY;
		}
#ifdef CONFIG_AXIENET_HAS_MCDMA
			mcdma_desc_set_phys_addr(lp, phys, cur_p);
#else
			desc_set_phys_addr(lp, phys, cur_p);
#endif
	}

	cur_p->tx_desc_mapping = DESC_DMA_MAP_SINGLE;

	/* Update the APP fields for UDP segmentation by HW, if it is enabled.
	 * This automatically enables the checksum calculation by HW.
	 * If UDP segmentation by HW is not supported, then update the APP fields for
	 * checksum calculation by HW, if it is enabled.
	 */
#ifdef CONFIG_AXIENET_HAS_MCDMA
	if (ndev->hw_features & NETIF_F_GSO_UDP_L4)
		axienet_eoe_config_hwgso(ndev, skb, cur_p);
	else if (ndev->hw_features & NETIF_F_IP_CSUM)
		axienet_eoe_config_hwcso(ndev, cur_p);
#endif

	for (ii = 0; ii < num_frag; ii++) {
		u32 len;
		skb_frag_t *frag;

		if (++q->tx_bd_tail >= lp->tx_bd_num)
			q->tx_bd_tail = 0;

#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
		cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif
		frag = &skb_shinfo(skb)->frags[ii];
		len = skb_frag_size(frag);
		phys = skb_frag_dma_map(ndev->dev.parent, frag, 0, len,
					DMA_TO_DEVICE);
#ifdef CONFIG_AXIENET_HAS_MCDMA
			mcdma_desc_set_phys_addr(lp, phys, cur_p);
#else
			desc_set_phys_addr(lp, phys, cur_p);
#endif
		cur_p->cntrl = len;
		cur_p->tx_desc_mapping = DESC_DMA_MAP_PAGE;
	}

out:
#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p->cntrl |= XMCDMA_BD_CTRL_TXEOF_MASK;
	tail_p = q->tx_bd_p + sizeof(*q->txq_bd_v) * q->tx_bd_tail;
#else
	cur_p->cntrl |= XAXIDMA_BD_CTRL_TXEOF_MASK;
	tail_p = q->tx_bd_p + sizeof(*q->tx_bd_v) * q->tx_bd_tail;
#endif
	cur_p->tx_skb = skb;

	tail_p = q->tx_bd_p + sizeof(*q->tx_bd_v) * q->tx_bd_tail;
	/* Ensure BD write before starting transfer */
	wmb();

	/* Start the transfer */
#ifdef CONFIG_AXIENET_HAS_MCDMA
	axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id),
			  tail_p);
#else
	axienet_dma_bdout(q, XAXIDMA_TX_TDESC_OFFSET, tail_p);
#endif
	if (++q->tx_bd_tail >= lp->tx_bd_num)
		q->tx_bd_tail = 0;

	spin_unlock_irqrestore(&q->tx_lock, flags);
	return NETDEV_TX_OK;
}

/**
 * axienet_start_xmit - Starts the transmission.
 * @skb:	sk_buff pointer that contains data to be Txed.
 * @ndev:	Pointer to net_device structure.
 *
 * Return: NETDEV_TX_OK, on success
 *	    NETDEV_TX_BUSY, if any of the descriptors are not free
 *
 * This function is invoked from upper layers to initiate transmission. The
 * function uses the next available free BDs and populates their fields to
 * start the transmission. Additionally if checksum offloading is supported,
 * it populates AXI Stream Control fields with appropriate values.
 */
static int axienet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	u16 map = skb_get_queue_mapping(skb); /* Single dma queue default*/

	return axienet_queue_xmit(skb, ndev, map);
}

/**
 * axienet_dma_rx_cb - DMA engine callback for RX channel.
 * @data:       Pointer to the skbuf_dma_descriptor structure.
 * @result:     error reporting through dmaengine_result.
 * This function is called by dmaengine driver for RX channel to notify
 * that the packet is received.
 */
static void axienet_dma_rx_cb(void *data, const struct dmaengine_result *result)
{
	struct skbuf_dma_descriptor *skbuf_dma;
	size_t meta_len, meta_max_len, rx_len;
	struct axienet_local *lp = data;
	struct sk_buff *skb;
	u32 *app_metadata;

	skbuf_dma = axienet_get_rx_desc(lp, lp->rx_ring_tail++);
	skb = skbuf_dma->skb;
	app_metadata = dmaengine_desc_get_metadata_ptr(skbuf_dma->desc, &meta_len,
						       &meta_max_len);
	dma_unmap_single(lp->dev, skbuf_dma->dma_address, lp->max_frm_size,
			 DMA_FROM_DEVICE);
	/* TODO: Derive app word index programmatically */
	rx_len = (app_metadata[LEN_APP] & 0xFFFF);
	skb_put(skb, rx_len);
	skb->protocol = eth_type_trans(skb, lp->ndev);
	skb->ip_summed = CHECKSUM_NONE;

	__netif_rx(skb);
	u64_stats_update_begin(&lp->rx_stat_sync);
	u64_stats_add(&lp->rx_packets, 1);
	u64_stats_add(&lp->rx_bytes, rx_len);
	u64_stats_update_end(&lp->rx_stat_sync);
	axienet_rx_submit_desc(lp->ndev);
	dma_async_issue_pending(lp->rx_chan);
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
	dma_addr_t phys, tail_p = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct sk_buff *skb, *new_skb;
	struct napi_struct *napi = &q->napi_rx;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	unsigned int numbdfree = 0;

	/* Get relevat BD status value */
	rmb();
#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->rxq_bd_v[q->rx_bd_ci];
#else
	cur_p = &q->rx_bd_v[q->rx_bd_ci];
#endif

	while ((numbdfree < budget) &&
	       (cur_p->status & XAXIDMA_BD_STS_COMPLETE_MASK)) {
		new_skb = netdev_alloc_skb(ndev, lp->max_frm_size);
		if (!new_skb)
			break;

#ifdef CONFIG_AXIENET_HAS_MCDMA
		tail_p = q->rx_bd_p + sizeof(*q->rxq_bd_v) * q->rx_bd_ci;
		phys = mcdma_desc_get_phys_addr(lp, cur_p);
#else
		tail_p = q->rx_bd_p + sizeof(*q->rx_bd_v) * q->rx_bd_ci;
		phys = desc_get_phys_addr(lp, cur_p);
#endif

		dma_unmap_single(ndev->dev.parent, phys,
				 lp->max_frm_size,
				 DMA_FROM_DEVICE);

		skb = (struct sk_buff *)(cur_p->sw_id_offset);

		/* skb could be NULL if a previous pass already received the
		 * packet for this slot in the ring, but failed to refill it
		 * with a newly allocated buffer. In this case, don't try to
		 * receive it again.
		 */
		if (likely(skb)) {
			if (lp->eth_hasnobuf ||
			    lp->axienet_config->mactype != XAXIENET_1_2p5G)
				length = cur_p->status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
			else
				length = cur_p->app4 & 0x0000FFFF;

			skb_put(skb, length);

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
			if ((lp->tstamp_config.rx_filter == HWTSTAMP_FILTER_ALL ||
			     lp->eth_hasptp) &&
			    lp->axienet_config->mactype != XAXIENET_10G_25G &&
			    lp->axienet_config->mactype != XAXIENET_MRMAC) {
				u32 sec, nsec;
				u64 time64;
				struct skb_shared_hwtstamps *shhwtstamps;

				if (lp->axienet_config->mactype == XAXIENET_1_2p5G) {
					/* The first 8 bytes will be the timestamp */
					memcpy(&sec, &skb->data[0], 4);
					memcpy(&nsec, &skb->data[4], 4);

					sec = cpu_to_be32(sec);
					nsec = cpu_to_be32(nsec);
				} else {
					/* The first 8 bytes will be the timestamp */
					memcpy(&nsec, &skb->data[0], 4);
					memcpy(&sec, &skb->data[4], 4);
				}

				/* Remove these 8 bytes from the buffer */
				skb_pull(skb, 8);
				time64 = sec * NS_PER_SEC + nsec;
				shhwtstamps = skb_hwtstamps(skb);
				shhwtstamps->hwtstamp = ns_to_ktime(time64);
			} else if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
				   lp->axienet_config->mactype == XAXIENET_MRMAC) {
				axienet_rx_hwtstamp(lp, skb);
			}
#endif
			skb->protocol = eth_type_trans(skb, ndev);
			/*skb_checksum_none_assert(skb);*/
			skb->ip_summed = CHECKSUM_NONE;

			/* if we're doing Rx csum offload, set it up */
			if (lp->features & XAE_FEATURE_FULL_RX_CSUM &&
			    lp->axienet_config->mactype == XAXIENET_1_2p5G &&
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
				   lp->axienet_config->mactype == XAXIENET_1_2p5G) {
				skb->csum = be32_to_cpu(cur_p->app3 & 0xFFFF);
				skb->ip_summed = CHECKSUM_COMPLETE;
			}

		napi_gro_receive(napi, skb);

			size += length;
			packets++;
		}

		/* Ensure that the skb is completely updated
		 * prio to mapping the DMA
		 */
		wmb();

		phys = dma_map_single(ndev->dev.parent, new_skb->data,
				      lp->max_frm_size,
				      DMA_FROM_DEVICE);
#ifdef CONFIG_AXIENET_HAS_MCDMA
			mcdma_desc_set_phys_addr(lp, phys, cur_p);
#else
			desc_set_phys_addr(lp, phys, cur_p);
#endif
		if (unlikely(dma_mapping_error(ndev->dev.parent, phys))) {
			phys = 0;
#ifdef CONFIG_AXIENET_HAS_MCDMA
			mcdma_desc_set_phys_addr(lp, phys, cur_p);
#else
			desc_set_phys_addr(lp, phys, cur_p);
#endif
			dev_kfree_skb(new_skb);
			dev_err(lp->dev, "RX buffer map failed\n");
			break;
		}
		cur_p->cntrl = lp->max_frm_size;
		cur_p->status = 0;
		cur_p->sw_id_offset = new_skb;

		if (++q->rx_bd_ci >= lp->rx_bd_num)
			q->rx_bd_ci = 0;

		/* Get relevat BD status value */
		rmb();
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p = &q->rxq_bd_v[q->rx_bd_ci];
#else
		cur_p = &q->rx_bd_v[q->rx_bd_ci];
#endif
		numbdfree++;
	}

	u64_stats_update_begin(&lp->rx_stat_sync);
	u64_stats_add(&lp->rx_packets, packets);
	u64_stats_add(&lp->rx_bytes, size);
	u64_stats_update_end(&lp->rx_stat_sync);
	ndev->stats.rx_packets += packets;
	ndev->stats.rx_bytes += size;
	q->rxq_packets += packets;
	q->rxq_bytes += size;

	if (tail_p) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id) +
				  q->rx_offset, tail_p);
#else
		axienet_dma_bdout(q, XAXIDMA_RX_TDESC_OFFSET, tail_p);
#endif
	}

	return numbdfree;
}

/**
 * xaxienet_rx_poll - Poll routine for rx packets (NAPI)
 * @napi:	napi structure pointer
 * @quota:	Max number of rx packets to be processed.
 *
 * This is the poll routine for rx part.
 * It will process the packets maximux quota value.
 *
 * Return: number of packets received
 */
int xaxienet_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = container_of(napi, struct axienet_dma_q, napi_rx);
	int work_done = 0;
	unsigned int status, cr;

#ifdef CONFIG_AXIENET_HAS_MCDMA
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

		if (axienet_eoe_is_channel_gro(lp, q))
			work_done += axienet_eoe_recv_gro(lp->ndev, quota - work_done, q);
		else
			work_done += axienet_recv(lp->ndev, quota - work_done, q);

		status = axienet_dma_in32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
					  q->rx_offset);
	}
#else

	status = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
	while ((status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) &&
	       (work_done < quota)) {
		axienet_dma_out32(q, XAXIDMA_RX_SR_OFFSET, status);
		if (status & XAXIDMA_IRQ_ERROR_MASK) {
			dev_err(lp->dev, "Rx error 0x%x\n\r", status);
			break;
		}
		work_done += axienet_recv(lp->ndev, quota - work_done, q);
		status = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
	}

#endif
	if (work_done < quota) {
		napi_complete(napi);
#ifdef CONFIG_AXIENET_HAS_MCDMA
		/* Enable the interrupts again */
		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				      XMCDMA_RX_OFFSET);
		cr |= (XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK);
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				  XMCDMA_RX_OFFSET, cr);
#else
		/* Enable the interrupts again */
		cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		cr |= (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK);
		axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);
#endif
	}

	return work_done;
}

/**
 * axienet_eth_irq - Ethernet core Isr.
 * @irq:	irq number
 * @_ndev:	net_device pointer
 *
 * Return: IRQ_HANDLED if device generated a core interrupt, IRQ_NONE otherwise.
 *
 * Handle miscellaneous conditions indicated by Ethernet core IRQ.
 */
static irqreturn_t axienet_eth_irq(int irq, void *_ndev)
{
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	unsigned int pending;

	pending = axienet_ior(lp, XAE_IP_OFFSET);
	if (!pending)
		return IRQ_NONE;

	if (pending & XAE_INT_RXFIFOOVR_MASK)
		ndev->stats.rx_missed_errors++;

	if (pending & XAE_INT_RXRJECT_MASK)
		ndev->stats.rx_dropped++;

	axienet_iow(lp, XAE_IS_OFFSET, pending);
	return IRQ_HANDLED;
}

static void
axienet_config_autoneg_link_training(struct axienet_local *lp, unsigned int speed_config)
{
	struct net_device *ndev = lp->ndev;
	unsigned int ret, val;

	spin_lock(&lp->switch_lock);

	axienet_iow(lp, XXVS_LT_CTL_OFFSET, 0);
	axienet_iow(lp, XXVS_LT_TRAINED_OFFSET, 0);
	axienet_iow(lp, XXVS_LT_COEF_OFFSET, 0);
	axienet_iow(lp, XXVS_AN_ABILITY_OFFSET, speed_config);
	if (speed_config != XXVS_SPEED_1G) {
		axienet_iow(lp, XXVS_AN_CTL1_OFFSET,
			    (XXVS_AN_ENABLE_MASK | XXVS_AN_NONCE_SEED));
		axienet_iow(lp, XXVS_LT_CTL_OFFSET, XXVS_LT_ENABLE_MASK);
		axienet_iow(lp, XXVS_LT_TRAINED_OFFSET, XXVS_LT_TRAINED_MASK);
		axienet_iow(lp, XXVS_LT_SEED_OFFSET, XXVS_LT_SEED);
		axienet_iow(lp, XXVS_LT_COEF_OFFSET,
			    XXVS_LT_COEF_P1 << XXVS_LT_COEF_P1_SHIFT |
			    XXVS_LT_COEF_STATE0 << XXVS_LT_COEF_STATE0_SHIFT |
			    XXVS_LT_COEF_M1 << XXVS_LT_COEF_M1_SHIFT);

	} else {
		axienet_iow(lp, XXVS_AN_CTL1_OFFSET,
			    (XXVS_AN_ENABLE_MASK | XXVS_AN_NONCE_SEED1));
	}
	axienet_iow(lp, XXVS_RESET_OFFSET, XXVS_RX_SERDES_RESET);

	spin_unlock(&lp->switch_lock);

	ret = readl_poll_timeout(lp->regs + XXVS_AN_STATUS_OFFSET,
				 val, (val & XXVS_AN_COMPLETE_MASK),
				 100, DELAY_OF_ONE_MILLISEC * 15000);

	if (speed_config == XXVS_SPEED_1G) {
		if (ret) {
			netdev_err(ndev, "Autoneg failed");
			return;
		}
	}
	if (!ret) {
		ret = readl_poll_timeout(lp->regs + XXVS_LT_STATUS_OFFSET,
					 val, (val & XXVS_LT_DETECT_MASK),
					 100, DELAY_OF_ONE_MILLISEC * 15000);
		if (ret)
			netdev_err(ndev, "Link Training failed\n");
	}
}

/**
 * axienet_rx_submit_desc - Submit the rx descriptors to dmaengine.
 * allocate skbuff, map the scatterlist and obtain a descriptor
 * and then add the callback information and submit descriptor.
 *
 * @ndev:	net_device pointer
 *
 */
static void axienet_rx_submit_desc(struct net_device *ndev)
{
	struct dma_async_tx_descriptor *dma_rx_desc = NULL;
	struct axienet_local *lp = netdev_priv(ndev);
	struct skbuf_dma_descriptor *skbuf_dma;
	struct sk_buff *skb;
	dma_addr_t addr;

	skbuf_dma = axienet_get_rx_desc(lp, lp->rx_ring_head);
	if (!skbuf_dma)
		return;

	lp->rx_ring_head++;
	skb = netdev_alloc_skb(ndev, lp->max_frm_size);
	if (!skb)
		return;

	sg_init_table(skbuf_dma->sgl, 1);
	addr = dma_map_single(lp->dev, skb->data, lp->max_frm_size, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(lp->dev, addr))) {
		if (net_ratelimit())
			netdev_err(ndev, "DMA mapping error\n");
		goto rx_submit_err_free_skb;
	}
	sg_dma_address(skbuf_dma->sgl) = addr;
	sg_dma_len(skbuf_dma->sgl) = lp->max_frm_size;
	dma_rx_desc = dmaengine_prep_slave_sg(lp->rx_chan, skbuf_dma->sgl,
					      1, DMA_DEV_TO_MEM,
					      DMA_PREP_INTERRUPT);
	if (!dma_rx_desc)
		goto rx_submit_err_unmap_skb;

	skbuf_dma->skb = skb;
	skbuf_dma->dma_address = sg_dma_address(skbuf_dma->sgl);
	skbuf_dma->desc = dma_rx_desc;
	dma_rx_desc->callback_param = lp;
	dma_rx_desc->callback_result = axienet_dma_rx_cb;
	dmaengine_submit(dma_rx_desc);

	return;

rx_submit_err_unmap_skb:
	dma_unmap_single(lp->dev, addr, lp->max_frm_size, DMA_FROM_DEVICE);
rx_submit_err_free_skb:
	dev_kfree_skb(skb);
}

/**
 * axienet_init_dmaengine - init the dmaengine code.
 * @ndev:       Pointer to net_device structure
 *
 * Return: 0, on success.
 *          non-zero error value on failure
 *
 * This is the dmaengine initialization code.
 */
static int axienet_init_dmaengine(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct skbuf_dma_descriptor *skbuf_dma;
	int i, ret;

	lp->tx_chan = dma_request_chan(lp->dev, "tx_chan0");
	if (IS_ERR(lp->tx_chan)) {
		dev_err(lp->dev, "No Ethernet DMA (TX) channel found\n");
		return PTR_ERR(lp->tx_chan);
	}

	lp->rx_chan = dma_request_chan(lp->dev, "rx_chan0");
	if (IS_ERR(lp->rx_chan)) {
		ret = PTR_ERR(lp->rx_chan);
		dev_err(lp->dev, "No Ethernet DMA (RX) channel found\n");
		goto err_dma_release_tx;
}

	lp->tx_ring_tail = 0;
	lp->tx_ring_head = 0;
	lp->rx_ring_tail = 0;
	lp->rx_ring_head = 0;
	lp->tx_skb_ring = kcalloc(TX_BD_NUM_MAX, sizeof(*lp->tx_skb_ring),
				  GFP_KERNEL);
	if (!lp->tx_skb_ring) {
		ret = -ENOMEM;
		goto err_dma_release_rx;
	}
	for (i = 0; i < TX_BD_NUM_MAX; i++) {
		skbuf_dma = kzalloc(sizeof(*skbuf_dma), GFP_KERNEL);
		if (!skbuf_dma) {
			ret = -ENOMEM;
			goto err_free_tx_skb_ring;
		}
		lp->tx_skb_ring[i] = skbuf_dma;
	}

	lp->rx_skb_ring = kcalloc(RX_BUF_NUM_DEFAULT, sizeof(*lp->rx_skb_ring),
				  GFP_KERNEL);
	if (!lp->rx_skb_ring) {
		ret = -ENOMEM;
		goto err_free_tx_skb_ring;
	}
	for (i = 0; i < RX_BUF_NUM_DEFAULT; i++) {
		skbuf_dma = kzalloc(sizeof(*skbuf_dma), GFP_KERNEL);
		if (!skbuf_dma) {
			ret = -ENOMEM;
			goto err_free_rx_skb_ring;
		}
		lp->rx_skb_ring[i] = skbuf_dma;
	}
	/* TODO: Instead of BD_NUM_DEFAULT use runtime support */
	for (i = 0; i < RX_BUF_NUM_DEFAULT; i++)
		axienet_rx_submit_desc(ndev);
	dma_async_issue_pending(lp->rx_chan);

	return 0;

err_free_rx_skb_ring:
	for (i = 0; i < RX_BUF_NUM_DEFAULT; i++)
		kfree(lp->rx_skb_ring[i]);
	kfree(lp->rx_skb_ring);
err_free_tx_skb_ring:
	for (i = 0; i < TX_BD_NUM_MAX; i++)
		kfree(lp->tx_skb_ring[i]);
	kfree(lp->tx_skb_ring);
err_dma_release_rx:
	dma_release_channel(lp->rx_chan);
err_dma_release_tx:
	dma_release_channel(lp->tx_chan);
	return ret;
}

/**
 * axienet_init_legacy_dma - init the dma legacy code.
 * @ndev:       Pointer to net_device structure
 *
 * Return: 0, on success.
 *          non-zero error value on failure
 *
 * This is the dma  initialization code. It also allocates interrupt
 * service routines, enables the interrupt lines and ISR handling.
 *
 */
static int axienet_init_legacy_dma(struct net_device *ndev)
{
	int ret, i;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;

	lp->stopping = false;
	/* Enable tasklets for Axi DMA error handling */
	for_each_rx_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		tasklet_init(&lp->dma_err_tasklet[i],
			     axienet_mcdma_err_handler,
			     (unsigned long)lp->dq[i]);
#else
		tasklet_init(&lp->dma_err_tasklet[i],
			     axienet_dma_err_handler,
			     (unsigned long)lp->dq[i]);
#endif

		/* Enable NAPI scheduling before enabling Axi DMA Rx IRQ, or you
		 * might run into a race condition; the RX ISR disables IRQ processing
		 * before scheduling the NAPI function to complete the processing.
		 * If NAPI scheduling is (still) disabled at that time, no more RX IRQs
		 * will be processed as only the NAPI function re-enables them!
		 */
		napi_enable(&lp->dq[i]->napi_rx);
		napi_enable(&lp->dq[i]->napi_tx);
	}
	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
#ifdef CONFIG_AXIENET_HAS_MCDMA
		/* Enable interrupts for Axi MCDMA Tx */
		ret = request_irq(q->tx_irq, axienet_mcdma_tx_irq,
				  IRQF_SHARED, ndev->name, ndev);
		if (ret)
			goto err_tx_irq;
#else
		/* Enable interrupts for Axi DMA Tx */
		ret = request_irq(q->tx_irq, axienet_tx_irq,
				  0, ndev->name, ndev);
		if (ret)
			goto err_tx_irq;
#endif
		}

	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];
#ifdef CONFIG_AXIENET_HAS_MCDMA
		/* Enable interrupts for Axi MCDMA Rx */
		ret = request_irq(q->rx_irq, axienet_mcdma_rx_irq,
				  IRQF_SHARED, ndev->name, ndev);
		if (ret)
			goto err_rx_irq;
#else
		/* Enable interrupts for Axi DMA Rx */
		ret = request_irq(q->rx_irq, axienet_rx_irq,
				  0, ndev->name, ndev);
		if (ret)
			goto err_rx_irq;
#endif
	}

	/* Enable interrupts for Axi Ethernet core (if defined) */
	if (!lp->eth_hasnobuf && lp->axienet_config->mactype == XAXIENET_1_2p5G) {
		ret = request_irq(lp->eth_irq, axienet_eth_irq, IRQF_SHARED,
				  ndev->name, ndev);
		if (ret)
			goto err_eth_irq;
	}

	return 0;

err_eth_irq:
	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
		free_irq(q->rx_irq, ndev);
	}
	i = lp->num_tx_queues;
err_rx_irq:
	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
		free_irq(q->tx_irq, ndev);
	}
err_tx_irq:
	for_each_tx_dma_queue(lp, i) {
		napi_disable(&lp->dq[i]->napi_tx);
		napi_disable(&lp->dq[i]->napi_rx);
		tasklet_kill(&lp->dma_err_tasklet[i]);
	}

	dev_err(lp->dev, "request_irq() failed\n");
	return ret;
}

static void axienet_eoe_set_gro_address(struct axienet_local *lp)
{
	struct in_ifaddr *ifa = NULL;
	struct in_device *in_dev;
	struct axienet_dma_q *q;
	int i;

	in_dev = __in_dev_get_rcu(lp->ndev);
	if (in_dev)
		ifa = rcu_dereference((in_dev)->ifa_list);

	if (!ifa) {
		netdev_dbg(lp->ndev, "IP address not assigned\n");
		return;
	}

	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];
		if (axienet_eoe_is_channel_gro(lp, q))
			axienet_eoe_iow(lp,
					XEOE_UDP_GRO_DST_IP_OFFSET(q->chan_id),
					ntohl(ifa->ifa_address));
	}
}

/**
 * axienet_open - Driver open routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *	    non-zero error value on failure
 *
 * This is the driver open routine. It calls phylink_start to start the
 * PHY device.
 * It also allocates interrupt service routines, enables the interrupt lines
 * and ISR handling. Axi Ethernet core is reset through Axi DMA core. Buffer
 * descriptors are initialized.
 */
static int axienet_open(struct net_device *ndev)
{
	int ret = 0;
	u32 reg;
	struct axienet_local *lp = netdev_priv(ndev);

	/* When we do an Axi Ethernet reset, it resets the complete core
	 * including the MDIO. MDIO must be disabled before resetting.
	 * Hold MDIO bus lock to avoid MDIO accesses during the reset.
	 */
	axienet_lock_mii(lp);
	ret = axienet_device_reset(ndev);
	axienet_unlock_mii(lp);

	if (ret < 0) {
		dev_err(lp->dev, "axienet_device_reset failed\n");
		return ret;
	}

	if (lp->phylink) {
		ret = phylink_of_phy_connect(lp->phylink, lp->dev->of_node, 0);
		if (ret) {
			dev_err(lp->dev, "phylink_of_phy_connect() failed: %d\n", ret);
			return ret;
		}

		phylink_start(lp->phylink);
	}

	if (lp->features & XAE_FEATURE_STATS) {
		/* Start the statistics refresh work */
		schedule_delayed_work(&lp->stats_work, 0);
	}

	if (lp->phy_mode == PHY_INTERFACE_MODE_USXGMII) {
		netdev_dbg(ndev, "RX reg: 0x%x\n",
			   axienet_ior(lp, XXV_RCW1_OFFSET));
		/* USXGMII setup at selected speed */
		reg = axienet_ior(lp, XXV_USXGMII_AN_OFFSET);
		reg &= ~USXGMII_RATE_MASK;
		netdev_dbg(ndev, "usxgmii_rate %d\n", lp->usxgmii_rate);
		switch (lp->usxgmii_rate) {
		case SPEED_1000:
			reg |= USXGMII_RATE_1G;
			break;
		case SPEED_2500:
			reg |= USXGMII_RATE_2G5;
			break;
		case SPEED_10:
			reg |= USXGMII_RATE_10M;
			break;
		case SPEED_100:
			reg |= USXGMII_RATE_100M;
			break;
		case SPEED_5000:
			reg |= USXGMII_RATE_5G;
			break;
		case SPEED_10000:
			reg |= USXGMII_RATE_10G;
			break;
		default:
			reg |= USXGMII_RATE_1G;
		}
		reg |= USXGMII_FD;
		reg |= (USXGMII_EN | USXGMII_LINK_STS);
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET, reg);
		reg |= USXGMII_AN_EN;
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET, reg);
		/* AN Restart bit should be reset, set and then reset as per
		 * spec with a 1 ms delay for a raising edge trigger
		 */
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET,
			    reg & ~USXGMII_AN_RESTART);
		mdelay(1);
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET,
			    reg | USXGMII_AN_RESTART);
		mdelay(1);
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET,
			    reg & ~USXGMII_AN_RESTART);

		/* Check block lock bit to make sure RX path is ok with
		 * USXGMII initialization.
		 */
		ret = readl_poll_timeout(lp->regs + XXV_STATRX_BLKLCK_OFFSET,
					 reg, (reg & XXV_RX_BLKLCK_MASK),
					 100, DELAY_OF_ONE_MILLISEC);
		if (ret) {
			netdev_err(ndev, "%s: USXGMII Block lock bit not set",
				   __func__);
			ret = -ENODEV;
			goto err_phy;
		}

		ret = readl_poll_timeout(lp->regs + XXV_USXGMII_AN_STS_OFFSET,
					 reg, (reg & USXGMII_AN_STS_COMP_MASK),
					 1000000, DELAY_OF_ONE_MILLISEC);
		if (ret) {
			netdev_err(ndev, "%s: USXGMII AN not complete",
				   __func__);
			ret = -ENODEV;
			goto err_phy;
		}

		netdev_info(ndev, "USXGMII setup at %d\n", lp->usxgmii_rate);
	}

	if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
		u32 val;

		/* Reset MRMAC */
		axienet_mrmac_reset(lp);

		mdelay(DELAY_1MS * 100);
		/* Check for block lock bit to be set. This ensures that
		 * MRMAC ethernet IP is functioning normally.
		 */
		axienet_iow(lp, MRMAC_TX_STS_OFFSET, MRMAC_STS_ALL_MASK);
		axienet_iow(lp, MRMAC_RX_STS_OFFSET, MRMAC_STS_ALL_MASK);
		ret = readx_poll_timeout(axienet_get_mrmac_blocklock, lp, val,
					 (val & MRMAC_RX_BLKLCK_MASK), 10, DELAY_OF_ONE_MILLISEC);
		if (ret)
			netdev_err(ndev, "MRMAC block lock not complete! Cross-check the MAC ref clock configuration\n");

		ret = readx_poll_timeout(axienet_get_mrmac_rx_status, lp, val,
					 (val & MRMAC_RX_STATUS_MASK), 10, DELAY_OF_ONE_MILLISEC);
		if (ret) {
			netdev_err(ndev, "MRMAC Link is down!\n");
			ret = -ENODEV;
			goto err_phy;
		}

		axienet_iow(lp, MRMAC_STATRX_VALID_CTRL_OFFSET, MRMAC_STS_ALL_MASK);
		val = axienet_ior(lp, MRMAC_STATRX_VALID_CTRL_OFFSET);

		if (!(val & MRMAC_RX_VALID_MASK)) {
			netdev_err(ndev, "MRMAC Link is down! No recent RX Valid Control Code\n");
			ret = -ENODEV;
			goto err_phy;
		}
		netdev_info(ndev, "MRMAC setup at %d\n", lp->max_speed);
		axienet_iow(lp, MRMAC_TICK_OFFSET, MRMAC_TICK_TRIGGER);
	}

	/* If Runtime speed switching supported */
	if (lp->axienet_config->mactype == XAXIENET_10G_25G &&
	    (axienet_ior(lp, XXV_STAT_CORE_SPEED_OFFSET) &
	     XXV_STAT_CORE_SPEED_RTSW_MASK)) {
		axienet_iow(lp, XXVS_AN_ABILITY_OFFSET,
			    XXV_AN_10G_ABILITY_MASK | XXV_AN_25G_ABILITY_MASK);
		axienet_iow(lp, XXVS_AN_CTL1_OFFSET,
			    (XXVS_AN_ENABLE_MASK | XXVS_AN_NONCE_SEED));
	}

	if (lp->use_dmaengine) {
		/* Enable interrupts for Axi Ethernet core (if defined) */
		if (lp->eth_irq > 0) {
			ret = request_irq(lp->eth_irq, axienet_eth_irq, IRQF_SHARED,
					  ndev->name, ndev);
			if (ret)
				goto err_phy;
		}

		ret = axienet_init_dmaengine(ndev);
		if (ret < 0)
			goto err_free_eth_irq;
	} else {
		ret = axienet_init_legacy_dma(ndev);
		if (ret)
			goto err_phy;
	}

	if (lp->eoe_features & RX_HW_UDP_GRO)
		axienet_eoe_set_gro_address(lp);

	netif_tx_start_all_queues(ndev);
	return 0;

err_free_eth_irq:
	if (lp->eth_irq > 0)
		free_irq(lp->eth_irq, ndev);
err_phy:
	cancel_delayed_work_sync(&lp->stats_work);
	if (lp->phylink) {
		phylink_stop(lp->phylink);
		phylink_disconnect_phy(lp->phylink);
	}
	return ret;
}

/**
 * axienet_stop - Driver stop routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *
 * This is the driver stop routine. It calls phylink_disconnect to stop the PHY
 * device. It also removes the interrupt handlers and disables the interrupts.
 * The Axi DMA Tx/Rx BDs are released.
 */
static int axienet_stop(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int i;

	if (!lp->use_dmaengine)
		WRITE_ONCE(lp->stopping, true);

	cancel_delayed_work_sync(&lp->stats_work);

	if (lp->phylink) {
		phylink_stop(lp->phylink);
		phylink_disconnect_phy(lp->phylink);
	}

	if (lp->axienet_config->setoptions)
		lp->axienet_config->setoptions(ndev, lp->options &
				~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	if (!lp->use_dmaengine) {
		for_each_tx_dma_queue(lp, i)  {
			axienet_dma_stop(lp->dq[i]);
			netif_stop_queue(ndev);
			napi_disable(&lp->dq[i]->napi_tx);
			napi_disable(&lp->dq[i]->napi_rx);
			tasklet_kill(&lp->dma_err_tasklet[i]);
			free_irq(lp->dq[i]->tx_irq, ndev);
			free_irq(lp->dq[i]->rx_irq, ndev);
}

		axienet_dma_bd_release(ndev);
	} else {
		dmaengine_terminate_sync(lp->tx_chan);
		dmaengine_synchronize(lp->tx_chan);
		dmaengine_terminate_sync(lp->rx_chan);
		dmaengine_synchronize(lp->rx_chan);

		for (i = 0; i < TX_BD_NUM_MAX; i++)
			kfree(lp->tx_skb_ring[i]);
		kfree(lp->tx_skb_ring);
		for (i = 0; i < RX_BUF_NUM_DEFAULT; i++)
			kfree(lp->rx_skb_ring[i]);
		kfree(lp->rx_skb_ring);

		dma_release_channel(lp->rx_chan);
		dma_release_channel(lp->tx_chan);
	}
	if (lp->axienet_config->mactype == XAXIENET_1_2p5G)
		axienet_iow(lp, XAE_IE_OFFSET, 0);

	if (lp->axienet_config->mactype == XAXIENET_1_2p5G && !lp->eth_hasnobuf)
		free_irq(lp->eth_irq, ndev);

	/* Delete the GRO Filter Rules when Reset is done */
	if (lp->eoe_features & RX_HW_UDP_GRO && lp->rx_fs_list.count > 0) {
		struct ethtool_rx_fs_item *item, *tmp;

		list_for_each_entry_safe(item, tmp, &lp->rx_fs_list.list, list) {
			lp->assigned_rx_port[item->fs.location] = 0;
			list_del(&item->list);
			lp->rx_fs_list.count--;
			kfree(item);
		}
	}

	return 0;
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

	WRITE_ONCE(ndev->mtu, new_mtu);

	return 0;
}

static netdev_features_t axienet_features_check(struct sk_buff *skb,
						struct net_device *dev,
						netdev_features_t features)
{
	struct axienet_local *lp = netdev_priv(dev);

	if (lp->eoe_connected && (ip_hdr(skb)->version != 4))
		features &= ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);

	return features;
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

	for_each_rx_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_mcdma_rx_irq(lp->dq[i]->rx_irq, ndev);
		axienet_mcdma_tx_irq(lp->dq[i]->tx_irq, ndev);
#else
		axienet_rx_irq(lp->dq[i]->rx_irq, ndev);
		axienet_tx_irq(lp->dq[i]->tx_irq, ndev);
#endif
	}
	for_each_tx_dma_queue(lp, i)
#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_mcdma_tx_irq(lp->dq[i]->tx_irq, ndev);
#else
		axienet_tx_irq(lp->dq[i]->tx_irq, ndev);
#endif
	for_each_tx_dma_queue(lp, i)
		enable_irq(lp->dq[i]->tx_irq);
	for_each_rx_dma_queue(lp, i)
		enable_irq(lp->dq[i]->rx_irq);
}
#endif

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
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
	struct axienet_local *lp = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		if (!lp->phylink)
			return -EOPNOTSUPP;
		return phylink_mii_ioctl(lp->phylink, rq, cmd);
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	case SIOCSHWTSTAMP:
		return axienet_set_ts_config(lp, rq);
	case SIOCGHWTSTAMP:
		return axienet_get_ts_config(lp, rq);
#endif
	default:
		return -EOPNOTSUPP;
	}
}

static void
axienet_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct axienet_local *lp = netdev_priv(dev);
	unsigned int start;

	netdev_stats_to_stats64(stats, &dev->stats);

	if (lp->axienet_config->mactype != XAXIENET_1_2p5G) {
		stats->rx_packets = dev->stats.rx_packets;
		stats->rx_bytes = dev->stats.rx_bytes;
		stats->tx_packets = dev->stats.tx_packets;
		stats->tx_bytes = dev->stats.tx_bytes;
		return;
	}

	do {
		start = u64_stats_fetch_begin(&lp->rx_stat_sync);
		stats->rx_packets = u64_stats_read(&lp->rx_packets);
		stats->rx_bytes = u64_stats_read(&lp->rx_bytes);
	} while (u64_stats_fetch_retry(&lp->rx_stat_sync, start));

	do {
		start = u64_stats_fetch_begin(&lp->tx_stat_sync);
		stats->tx_packets = u64_stats_read(&lp->tx_packets);
		stats->tx_bytes = u64_stats_read(&lp->tx_bytes);
	} while (u64_stats_fetch_retry(&lp->tx_stat_sync, start));

	if (!(lp->features & XAE_FEATURE_STATS))
		return;

	do {
		start = read_seqcount_begin(&lp->hw_stats_seqcount);
		stats->rx_length_errors =
			axienet_stat(lp, STAT_RX_LENGTH_ERRORS);
		stats->rx_crc_errors = axienet_stat(lp, STAT_RX_FCS_ERRORS);
		stats->rx_frame_errors =
			axienet_stat(lp, STAT_RX_ALIGNMENT_ERRORS);
		stats->rx_errors = axienet_stat(lp, STAT_UNDERSIZE_FRAMES) +
				   axienet_stat(lp, STAT_FRAGMENT_FRAMES) +
				   stats->rx_length_errors +
				   stats->rx_crc_errors +
				   stats->rx_frame_errors;
		stats->multicast = axienet_stat(lp, STAT_RX_MULTICAST_FRAMES);

		stats->tx_aborted_errors =
			axienet_stat(lp, STAT_TX_EXCESS_COLLISIONS);
		stats->tx_fifo_errors =
			axienet_stat(lp, STAT_TX_UNDERRUN_ERRORS);
		stats->tx_window_errors =
			axienet_stat(lp, STAT_TX_LATE_COLLISIONS);
		stats->tx_errors = axienet_stat(lp, STAT_TX_EXCESS_DEFERRAL) +
				   stats->tx_aborted_errors +
				   stats->tx_fifo_errors +
				   stats->tx_window_errors;
	} while (read_seqcount_retry(&lp->hw_stats_seqcount, start));
}

static const struct net_device_ops axienet_netdev_ops = {
	.ndo_open = axienet_open,
	.ndo_stop = axienet_stop,
	.ndo_start_xmit = axienet_start_xmit,
	.ndo_get_stats64 = axienet_get_stats64,
	.ndo_change_mtu	= axienet_change_mtu,
	.ndo_set_mac_address = netdev_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_eth_ioctl = axienet_ioctl,
	.ndo_set_rx_mode = axienet_set_multicast_list,
	.ndo_do_ioctl = axienet_ioctl,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = axienet_poll_controller,
#endif
	.ndo_features_check = axienet_features_check,
};

static const struct net_device_ops axienet_netdev_dmaengine_ops = {
	.ndo_open = axienet_open,
	.ndo_stop = axienet_stop,
	.ndo_start_xmit = axienet_start_xmit_dmaengine,
	.ndo_get_stats64 = axienet_get_stats64,
	.ndo_change_mtu	= axienet_change_mtu,
	.ndo_set_mac_address = netdev_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_eth_ioctl = axienet_ioctl,
	.ndo_set_rx_mode = axienet_set_multicast_list,
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
	data[0] = axienet_ior(lp, XAE_RAF_OFFSET);
	data[1] = axienet_ior(lp, XAE_TPF_OFFSET);
	data[2] = axienet_ior(lp, XAE_IFGP_OFFSET);
	data[3] = axienet_ior(lp, XAE_IS_OFFSET);
	data[4] = axienet_ior(lp, XAE_IP_OFFSET);
	data[5] = axienet_ior(lp, XAE_IE_OFFSET);
	data[6] = axienet_ior(lp, XAE_TTAG_OFFSET);
	data[7] = axienet_ior(lp, XAE_RTAG_OFFSET);
	data[8] = axienet_ior(lp, XAE_UAWL_OFFSET);
	data[9] = axienet_ior(lp, XAE_UAWU_OFFSET);
	data[10] = axienet_ior(lp, XAE_TPID0_OFFSET);
	data[11] = axienet_ior(lp, XAE_TPID1_OFFSET);
	data[12] = axienet_ior(lp, XAE_PPST_OFFSET);
	data[13] = axienet_ior(lp, XAE_RCW0_OFFSET);
	data[14] = axienet_ior(lp, XAE_RCW1_OFFSET);
	data[15] = axienet_ior(lp, XAE_TC_OFFSET);
	data[16] = axienet_ior(lp, XAE_FCC_OFFSET);
	data[17] = axienet_ior(lp, XAE_EMMC_OFFSET);
	data[18] = axienet_ior(lp, XAE_PHYC_OFFSET);
	data[19] = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	data[20] = axienet_ior(lp, XAE_MDIO_MCR_OFFSET);
	data[21] = axienet_ior(lp, XAE_MDIO_MWD_OFFSET);
	data[22] = axienet_ior(lp, XAE_MDIO_MRD_OFFSET);
	data[27] = axienet_ior(lp, XAE_UAW0_OFFSET);
	data[28] = axienet_ior(lp, XAE_UAW1_OFFSET);
	data[29] = axienet_ior(lp, XAE_FMI_OFFSET);
	data[30] = axienet_ior(lp, XAE_AF0_OFFSET);
	data[31] = axienet_ior(lp, XAE_AF1_OFFSET);
	if (!lp->use_dmaengine) {
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
}

static void
axienet_ethtools_get_ringparam(struct net_device *ndev,
			       struct ethtool_ringparam *ering,
			       struct kernel_ethtool_ringparam *kernel_ering,
			       struct netlink_ext_ack *extack)
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

static int
axienet_ethtools_set_ringparam(struct net_device *ndev,
			       struct ethtool_ringparam *ering,
			       struct kernel_ethtool_ringparam *kernel_ering,
			       struct netlink_ext_ack *extack)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (ering->rx_pending > RX_BD_NUM_MAX ||
	    ering->rx_mini_pending ||
	    ering->rx_jumbo_pending ||
	    ering->tx_pending < TX_BD_NUM_MIN ||
	    ering->tx_pending > TX_BD_NUM_MAX)
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
	struct axienet_local *lp = netdev_priv(ndev);

	if (lp->phylink)
		phylink_ethtool_get_pauseparam(lp->phylink, epauseparm);
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
	struct axienet_local *lp = netdev_priv(ndev);

	if (!lp->phylink)
		return -EOPNOTSUPP;

	return phylink_ethtool_set_pauseparam(lp->phylink, epauseparm);
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
static int
axienet_ethtools_get_coalesce(struct net_device *ndev,
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

		regval = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		ecoalesce->rx_max_coalesced_frames +=
						(regval & XAXIDMA_COALESCE_MASK)
						     >> XAXIDMA_COALESCE_SHIFT;
		ecoalesce->rx_coalesce_usecs = lp->coalesce_usec_rx;
	}
	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
		regval = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
		ecoalesce->tx_max_coalesced_frames +=
						(regval & XAXIDMA_COALESCE_MASK)
						     >> XAXIDMA_COALESCE_SHIFT;
		ecoalesce->tx_coalesce_usecs = lp->coalesce_usec_tx;
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
static int
axienet_ethtools_set_coalesce(struct net_device *ndev,
			      struct ethtool_coalesce *ecoalesce,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		NL_SET_ERR_MSG(extack,
			       "Please stop netif before applying configuration");
		return -EBUSY;
	}

	if (ecoalesce->rx_max_coalesced_frames > 255 ||
	    ecoalesce->tx_max_coalesced_frames > 255) {
		NL_SET_ERR_MSG(extack, "frames must be less than 256");
		return -EINVAL;
	}

	if (ecoalesce->rx_max_coalesced_frames)
		lp->coalesce_count_rx = ecoalesce->rx_max_coalesced_frames;
	if (ecoalesce->rx_coalesce_usecs)
		lp->coalesce_usec_rx = ecoalesce->rx_coalesce_usecs;
	if (ecoalesce->tx_max_coalesced_frames)
		lp->coalesce_count_tx = ecoalesce->tx_max_coalesced_frames;
	if (ecoalesce->tx_coalesce_usecs)
		lp->coalesce_usec_tx = ecoalesce->tx_coalesce_usecs;

	return 0;
}

static int
axienet_ethtools_get_link_ksettings(struct net_device *ndev,
				    struct ethtool_link_ksettings *cmd)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (!lp->phylink)
		return -EOPNOTSUPP;

	return phylink_ethtool_ksettings_get(lp->phylink, cmd);
}

static int
axienet_ethtools_set_link_ksettings(struct net_device *ndev,
				    const struct ethtool_link_ksettings *cmd)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (!lp->phylink)
		return -EOPNOTSUPP;

	return phylink_ethtool_ksettings_set(lp->phylink, cmd);
}

static int axienet_ethtools_nway_reset(struct net_device *dev)
{
	struct axienet_local *lp = netdev_priv(dev);

	return phylink_ethtool_nway_reset(lp->phylink);
}

static void axienet_ethtools_get_ethtool_stats(struct net_device *dev,
					       struct ethtool_stats *stats,
					       u64 *data)
{
	struct axienet_local *lp = netdev_priv(dev);
	unsigned int start, i = 0;

	if (!(lp->features & XAE_FEATURE_STATS)) {
		data[i++] = dev->stats.tx_packets;
		data[i++] = dev->stats.rx_packets;
		data[i++] = dev->stats.tx_bytes;
		data[i++] = dev->stats.rx_bytes;
		data[i++] = dev->stats.tx_errors;
		data[i++] = dev->stats.rx_missed_errors + dev->stats.rx_frame_errors;

#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_get_stats(dev, stats, data);
#endif
		return;
	}

	do {
		start = read_seqcount_begin(&lp->hw_stats_seqcount);
		data[0] = axienet_stat(lp, STAT_RX_BYTES);
		data[1] = axienet_stat(lp, STAT_TX_BYTES);
		data[2] = axienet_stat(lp, STAT_RX_VLAN_FRAMES);
		data[3] = axienet_stat(lp, STAT_TX_VLAN_FRAMES);
		data[6] = axienet_stat(lp, STAT_TX_PFC_FRAMES);
		data[7] = axienet_stat(lp, STAT_RX_PFC_FRAMES);
		data[8] = axienet_stat(lp, STAT_USER_DEFINED0);
		data[9] = axienet_stat(lp, STAT_USER_DEFINED1);
		data[10] = axienet_stat(lp, STAT_USER_DEFINED2);
	} while (read_seqcount_retry(&lp->hw_stats_seqcount, start));
}

static const char axienet_ethtool_stats_strings[][ETH_GSTRING_LEN] = {
	"Received bytes",
	"Transmitted bytes",
	"RX Good VLAN Tagged Frames",
	"TX Good VLAN Tagged Frames",
	"TX Good PFC Frames",
	"RX Good PFC Frames",
	"User Defined Counter 0",
	"User Defined Counter 1",
	"User Defined Counter 2",
};

static void axienet_ethtools_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct axienet_local *lp = netdev_priv(dev);
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		if (lp->features & XAE_FEATURE_STATS) {
			memcpy(data, axienet_ethtool_stats_strings,
			       sizeof(axienet_ethtool_stats_strings));
		} else {
			for (i = 0; i < AXIENET_ETHTOOLS_SSTATS_LEN; i++) {
				memcpy(data + i * ETH_GSTRING_LEN,
				       axienet_get_ethtools_strings_stats[i].name,
				       ETH_GSTRING_LEN);
			}
#ifdef CONFIG_AXIENET_HAS_MCDMA
			axienet_strings(dev, stringset, data);
#endif
		}
		break;
	}
}

static int axienet_ethtools_get_sset_count(struct net_device *dev, int sset)
{
	struct axienet_local *lp = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		if (lp->features & XAE_FEATURE_STATS)
			return ARRAY_SIZE(axienet_ethtool_stats_strings);
#ifdef CONFIG_AXIENET_HAS_MCDMA
		return axienet_sset_count(dev, sset);
#else
		return AXIENET_ETHTOOLS_SSTATS_LEN;
#endif
		fallthrough;
	default:
		return -EOPNOTSUPP;
	}
}

static void
axienet_ethtools_get_pause_stats(struct net_device *dev,
				 struct ethtool_pause_stats *pause_stats)
{
	struct axienet_local *lp = netdev_priv(dev);
	unsigned int start;

	if (!(lp->features & XAE_FEATURE_STATS))
		return;

	do {
		start = read_seqcount_begin(&lp->hw_stats_seqcount);
		pause_stats->tx_pause_frames =
			axienet_stat(lp, STAT_TX_PAUSE_FRAMES);
		pause_stats->rx_pause_frames =
			axienet_stat(lp, STAT_RX_PAUSE_FRAMES);
	} while (read_seqcount_retry(&lp->hw_stats_seqcount, start));
}

static void
axienet_ethtool_get_eth_mac_stats(struct net_device *dev,
				  struct ethtool_eth_mac_stats *mac_stats)
{
	struct axienet_local *lp = netdev_priv(dev);
	unsigned int start;

	if (!(lp->features & XAE_FEATURE_STATS))
		return;

	do {
		start = read_seqcount_begin(&lp->hw_stats_seqcount);
		mac_stats->FramesTransmittedOK =
			axienet_stat(lp, STAT_TX_GOOD_FRAMES);
		mac_stats->SingleCollisionFrames =
			axienet_stat(lp, STAT_TX_SINGLE_COLLISION_FRAMES);
		mac_stats->MultipleCollisionFrames =
			axienet_stat(lp, STAT_TX_MULTIPLE_COLLISION_FRAMES);
		mac_stats->FramesReceivedOK =
			axienet_stat(lp, STAT_RX_GOOD_FRAMES);
		mac_stats->FrameCheckSequenceErrors =
			axienet_stat(lp, STAT_RX_FCS_ERRORS);
		mac_stats->AlignmentErrors =
			axienet_stat(lp, STAT_RX_ALIGNMENT_ERRORS);
		mac_stats->FramesWithDeferredXmissions =
			axienet_stat(lp, STAT_TX_DEFERRED_FRAMES);
		mac_stats->LateCollisions =
			axienet_stat(lp, STAT_TX_LATE_COLLISIONS);
		mac_stats->FramesAbortedDueToXSColls =
			axienet_stat(lp, STAT_TX_EXCESS_COLLISIONS);
		mac_stats->MulticastFramesXmittedOK =
			axienet_stat(lp, STAT_TX_MULTICAST_FRAMES);
		mac_stats->BroadcastFramesXmittedOK =
			axienet_stat(lp, STAT_TX_BROADCAST_FRAMES);
		mac_stats->FramesWithExcessiveDeferral =
			axienet_stat(lp, STAT_TX_EXCESS_DEFERRAL);
		mac_stats->MulticastFramesReceivedOK =
			axienet_stat(lp, STAT_RX_MULTICAST_FRAMES);
		mac_stats->BroadcastFramesReceivedOK =
			axienet_stat(lp, STAT_RX_BROADCAST_FRAMES);
		mac_stats->InRangeLengthErrors =
			axienet_stat(lp, STAT_RX_LENGTH_ERRORS);
	} while (read_seqcount_retry(&lp->hw_stats_seqcount, start));
}

static void
axienet_ethtool_get_eth_ctrl_stats(struct net_device *dev,
				   struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct axienet_local *lp = netdev_priv(dev);
	unsigned int start;

	if (!(lp->features & XAE_FEATURE_STATS))
		return;

	do {
		start = read_seqcount_begin(&lp->hw_stats_seqcount);
		ctrl_stats->MACControlFramesTransmitted =
			axienet_stat(lp, STAT_TX_CONTROL_FRAMES);
		ctrl_stats->MACControlFramesReceived =
			axienet_stat(lp, STAT_RX_CONTROL_FRAMES);
		ctrl_stats->UnsupportedOpcodesReceived =
			axienet_stat(lp, STAT_RX_CONTROL_OPCODE_ERRORS);
	} while (read_seqcount_retry(&lp->hw_stats_seqcount, start));
}

static const struct ethtool_rmon_hist_range axienet_rmon_ranges[] = {
	{   64,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519, 16384 },
	{ },
};

static void
axienet_ethtool_get_rmon_stats(struct net_device *dev,
			       struct ethtool_rmon_stats *rmon_stats,
			       const struct ethtool_rmon_hist_range **ranges)
{
	struct axienet_local *lp = netdev_priv(dev);
	unsigned int start;

	if (!(lp->features & XAE_FEATURE_STATS))
		return;

	do {
		start = read_seqcount_begin(&lp->hw_stats_seqcount);
		rmon_stats->undersize_pkts =
			axienet_stat(lp, STAT_UNDERSIZE_FRAMES);
		rmon_stats->oversize_pkts =
			axienet_stat(lp, STAT_RX_OVERSIZE_FRAMES);
		rmon_stats->fragments =
			axienet_stat(lp, STAT_FRAGMENT_FRAMES);

		rmon_stats->hist[0] =
			axienet_stat(lp, STAT_RX_64_BYTE_FRAMES);
		rmon_stats->hist[1] =
			axienet_stat(lp, STAT_RX_65_127_BYTE_FRAMES);
		rmon_stats->hist[2] =
			axienet_stat(lp, STAT_RX_128_255_BYTE_FRAMES);
		rmon_stats->hist[3] =
			axienet_stat(lp, STAT_RX_256_511_BYTE_FRAMES);
		rmon_stats->hist[4] =
			axienet_stat(lp, STAT_RX_512_1023_BYTE_FRAMES);
		rmon_stats->hist[5] =
			axienet_stat(lp, STAT_RX_1024_MAX_BYTE_FRAMES);
		rmon_stats->hist[6] =
			rmon_stats->oversize_pkts;

		rmon_stats->hist_tx[0] =
			axienet_stat(lp, STAT_TX_64_BYTE_FRAMES);
		rmon_stats->hist_tx[1] =
			axienet_stat(lp, STAT_TX_65_127_BYTE_FRAMES);
		rmon_stats->hist_tx[2] =
			axienet_stat(lp, STAT_TX_128_255_BYTE_FRAMES);
		rmon_stats->hist_tx[3] =
			axienet_stat(lp, STAT_TX_256_511_BYTE_FRAMES);
		rmon_stats->hist_tx[4] =
			axienet_stat(lp, STAT_TX_512_1023_BYTE_FRAMES);
		rmon_stats->hist_tx[5] =
			axienet_stat(lp, STAT_TX_1024_MAX_BYTE_FRAMES);
		rmon_stats->hist_tx[6] =
			axienet_stat(lp, STAT_TX_OVERSIZE_FRAMES);
	} while (read_seqcount_retry(&lp->hw_stats_seqcount, start));

	*ranges = axienet_rmon_ranges;
}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
/**
 * axienet_ethtools_get_ts_info - Get h/w timestamping capabilities.
 * @ndev:	Pointer to net_device structure
 * @info:	Pointer to ethtool_ts_info structure
 *
 * Return: 0, on success, Non-zero error value on failure.
 */
static int axienet_ethtools_get_ts_info(struct net_device *ndev,
					struct kernel_ethtool_ts_info *info)
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

	if (lp->axienet_config->mactype == XAXIENET_MRMAC ||
	    lp->axienet_config->mactype == XAXIENET_10G_25G) {
		struct device_node *np;
		struct xlnx_ptp_timer *timer = NULL;
		struct platform_device *ptpnode;

		np = of_parse_phandle(lp->dev->of_node, "ptp-hardware-clock", 0);

		ptpnode = of_find_device_by_node(np);

		if (ptpnode)
			timer = platform_get_drvdata(ptpnode);

		if (timer)
			info->phc_index = timer->phc_index;
		else if (!timer)
			netdev_warn(ndev, "PTP timer node not found\n");

		of_node_put(np);
		platform_device_put(ptpnode);
	}

	return 0;
}
#endif

#ifdef CONFIG_XILINX_AXI_EOE
static int axienet_eoe_get_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int ret = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = lp->num_rx_queues;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = lp->rx_fs_list.count;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = axienet_eoe_get_flow_entry(ndev, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = axienet_eoe_get_all_flow_entries(ndev, cmd, rule_locs);
		break;
	default:
		netdev_err(ndev, "Command parameter %d is not supported\n", cmd->cmd);
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int axienet_eoe_set_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *cmd)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int ret = -EOPNOTSUPP;

	if (!(lp->eoe_features & RX_HW_UDP_GRO)) {
		netdev_err(ndev, "HW GRO is not supported\n");
		ret = -EINVAL;
		return ret;
	}

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		if (cmd->fs.location >= lp->num_rx_queues || cmd->fs.location == 0) {
			netdev_err(ndev, "Invalid Location, 1 to 15 are valid GRO locations.");
			ret = -EINVAL;
			break;
		}
		ret = axienet_eoe_add_flow_filter(ndev, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = axienet_eoe_del_flow_filter(ndev, cmd);
		break;
	default:
		netdev_err(ndev, "Command parameter %d is not supported\n", cmd->cmd);
		ret = -EOPNOTSUPP;
	}

	return ret;
}
#endif

static const struct ethtool_ops axienet_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USECS,
	.get_drvinfo    = axienet_ethtools_get_drvinfo,
	.get_regs_len   = axienet_ethtools_get_regs_len,
	.get_regs       = axienet_ethtools_get_regs,
	.get_link       = ethtool_op_get_link,
	.get_ringparam	= axienet_ethtools_get_ringparam,
	.set_ringparam	= axienet_ethtools_set_ringparam,
	.get_pauseparam = axienet_ethtools_get_pauseparam,
	.set_pauseparam = axienet_ethtools_set_pauseparam,
	.get_coalesce   = axienet_ethtools_get_coalesce,
	.set_coalesce   = axienet_ethtools_set_coalesce,
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	.get_ts_info    = axienet_ethtools_get_ts_info,
#endif
	.get_link_ksettings = axienet_ethtools_get_link_ksettings,
	.set_link_ksettings = axienet_ethtools_set_link_ksettings,
	.nway_reset	= axienet_ethtools_nway_reset,
	.get_ethtool_stats = axienet_ethtools_get_ethtool_stats,
	.get_strings    = axienet_ethtools_get_strings,
	.get_sset_count = axienet_ethtools_get_sset_count,
	.get_pause_stats = axienet_ethtools_get_pause_stats,
	.get_eth_mac_stats = axienet_ethtool_get_eth_mac_stats,
	.get_eth_ctrl_stats = axienet_ethtool_get_eth_ctrl_stats,
	.get_rmon_stats = axienet_ethtool_get_rmon_stats,
#ifdef CONFIG_XILINX_AXI_EOE
	.get_rxnfc = axienet_eoe_get_rxnfc,
	.set_rxnfc = axienet_eoe_set_rxnfc,
#endif
};

#ifdef CONFIG_AXIENET_HAS_MCDMA
static int __maybe_unused axienet_mcdma_probe(struct platform_device *pdev,
					      struct axienet_local *lp,
					      struct net_device *ndev)
{
	int i, ret = 0;
	struct axienet_dma_q *q;
	struct device_node *np;
	struct resource dmares;
	const char *str;

	ret = of_property_count_strings(pdev->dev.of_node, "xlnx,channel-ids");
	if (ret < 0)
		return -EINVAL;

	for_each_rx_dma_queue(lp, i) {
		q = kzalloc(sizeof(*q), GFP_KERNEL);

		/* parent */
		q->lp = lp;
		lp->dq[i] = q;
		ret = of_property_read_string_index(pdev->dev.of_node,
						    "xlnx,channel-ids", i,
						    &str);
		ret = kstrtou16(str, 16, &q->chan_id);
		lp->qnum[i] = i;
		lp->chan_num[i] = q->chan_id;
	}

	np = of_parse_phandle(pdev->dev.of_node, "axistream-connected",
			      0);
	if (IS_ERR(np)) {
		dev_err(&pdev->dev, "could not find DMA node\n");
		return ret;
	}

	ret = of_address_to_resource(np, 0, &dmares);
	if (ret) {
		dev_err(&pdev->dev, "unable to get DMA resource\n");
		return ret;
	}

	ret = of_property_read_u32(np, "xlnx,addrwidth", &lp->dma_mask);
	if (ret < 0 || lp->dma_mask < XAE_DMA_MASK_MIN ||
	    lp->dma_mask > XAE_DMA_MASK_MAX) {
		dev_info(&pdev->dev, "missing/invalid xlnx,addrwidth property, using default\n");
		lp->dma_mask = XAE_DMA_MASK_MIN;
	}

	lp->mcdma_regs = devm_ioremap_resource(&pdev->dev, &dmares);
	if (IS_ERR(lp->mcdma_regs)) {
		dev_err(&pdev->dev, "iormeap failed for the dma\n");
		ret = PTR_ERR(lp->mcdma_regs);
		return ret;
	}

	axienet_mcdma_tx_probe(pdev, np, lp);
	axienet_mcdma_rx_probe(pdev, lp, ndev);

	return 0;
}
#endif

static int __maybe_unused axienet_dma_probe(struct platform_device *pdev,
					    struct net_device *ndev)
{
	int i, ret;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	struct device_node *np = NULL;
	struct resource dmares;

	for_each_rx_dma_queue(lp, i) {
		q = devm_kzalloc(&pdev->dev, sizeof(*q), GFP_KERNEL);
		if (!q)
			return -ENOMEM;

		/* parent */
		q->lp = lp;

		lp->dq[i] = q;
	}

	/* Find the DMA node, map the DMA registers, and decode the DMA IRQs */
	/* TODO handle error ret */
	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];

		np = of_parse_phandle(pdev->dev.of_node, "axistream-connected",
				      i);
		if (np) {
			ret = of_address_to_resource(np, 0, &dmares);
			if (ret >= 0) {
				q->dma_regs = devm_ioremap_resource(&pdev->dev,
								    &dmares);
			} else {
				dev_err(&pdev->dev, "unable to get DMA resource for %pOF\n",
					np);
				return -ENODEV;
			}

			lp->dq[i]->tx_irq = irq_of_parse_and_map(np, 0);
			lp->dq[i]->rx_irq = irq_of_parse_and_map(np, 1);

			q->eth_hasdre = of_property_read_bool(np,
							      "xlnx,include-dre");
			ret = of_property_read_u32(np, "xlnx,addrwidth",
						   &lp->dma_mask);
			if (ret <  0 || lp->dma_mask < XAE_DMA_MASK_MIN ||
			    lp->dma_mask > XAE_DMA_MASK_MAX) {
				dev_info(&pdev->dev, "missing/invalid xlnx,addrwidth property, using default\n");
				lp->dma_mask = XAE_DMA_MASK_MIN;
			}
		} else {
			/* Check for these resources directly on the Ethernet node. */
			q->dma_regs = devm_platform_get_and_ioremap_resource(pdev, 1, NULL);
			q->rx_irq = platform_get_irq(pdev, 1);
			q->tx_irq = platform_get_irq(pdev, 0);
			if (IS_ERR(q->dma_regs)) {
				dev_err(&pdev->dev, "unable to get DMA resource for %pOF\n",
					np);
				return -ENODEV;
			}
			if (q->rx_irq <= 0 || q->tx_irq <= 0) {
				dev_err(&pdev->dev, "could not determine irqs\n");
				return -ENOMEM;
			}
		}
		netif_napi_add(ndev, &q->napi_tx, axienet_tx_poll);
		netif_napi_add(ndev, &q->napi_rx, xaxienet_rx_poll);

		spin_lock_init(&q->tx_lock);
	}

	of_node_put(np);

	return 0;
}

static struct axienet_local *pcs_to_axienet_local(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct axienet_local, pcs);
}

static void axienet_dcmac_get_fixed_state(struct phylink_config *config,
					  struct phylink_link_state *state)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);
	u32 rx_phy_stat;

	state->duplex = DUPLEX_FULL;
	state->speed = lp->max_speed;
	state->an_complete = PHYLINK_PCS_NEG_NONE;

	/* Clear previous status */
	axienet_iow(lp, DCMAC_STS_RX_PHY_OFFSET, DCMAC_STS_ALL_MASK);
	rx_phy_stat = axienet_ior(lp, DCMAC_STS_RX_PHY_OFFSET);

	state->link = (rx_phy_stat & DCMAC_RXPHY_RX_STS_MASK &&
			rx_phy_stat & DCMAC_RXPHY_RX_ALIGN_MASK);
	phylink_clear(state->advertising, Autoneg);
}

static void axienet_pcs_get_state(struct phylink_pcs *pcs,
				  struct phylink_link_state *state)
{
	struct axienet_local *lp = pcs_to_axienet_local(pcs);
	u32 speed, an_status, val;
	bool tx_pause, rx_pause;

	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		int gt_rst, blk_lock;

		speed = axienet_ior(lp, XXV_STAT_CORE_SPEED_OFFSET);
		if (speed & XXV_STAT_CORE_SPEED_10G_MASK)
			state->speed = SPEED_10000;
		else
			state->speed = SPEED_25000;

		state->duplex = DUPLEX_FULL;
		if (lp->auto_neg) {
			an_status = axienet_ior(lp, XXV_STAT_AN_STS_OFFSET);
			tx_pause = an_status & XXV_TX_PAUSE_MASK;
			rx_pause = an_status & XXV_RX_PAUSE_MASK;

			state->pause = (tx_pause & MLO_PAUSE_TX) | (rx_pause & MLO_PAUSE_RX);
			state->an_complete = an_status & XXV_AN_COMPLETE_MASK;
		}
		state->link = 0;

		gt_rst = readl_poll_timeout(lp->regs + XXV_STAT_GTWIZ_OFFSET,
					    val, (val & XXV_GTWIZ_RESET_DONE),
					    10, DELAY_OF_ONE_MILLISEC);

		if (!gt_rst) {
			blk_lock = readl_poll_timeout(lp->regs + XXV_STATRX_BLKLCK_OFFSET,
						      val, (val & XXV_RX_BLKLCK_MASK),
						      10, DELAY_OF_ONE_MILLISEC);
			if (!blk_lock)
				state->link = 1;
		}
	} else if (lp->axienet_config->mactype == XAXIENET_1G_10G_25G) {
		speed = axienet_ior(lp, XXVS_SPEED_OFFSET);
		if (speed & XXVS_SPEED_1G)
			state->speed = SPEED_1000;
		else if (speed & XXVS_SPEED_10G)
			state->speed = SPEED_10000;
		else if (!(speed & ~XXVS_SPEED_25G))
			state->speed = SPEED_25000;
		else
			state->speed = SPEED_UNKNOWN;

		state->duplex = DUPLEX_FULL;
		an_status = axienet_ior(lp, XXV_STAT_AN_STS_OFFSET);
		tx_pause = an_status & XXV_TX_PAUSE_MASK;
		rx_pause = an_status & XXV_RX_PAUSE_MASK;

		state->pause = (tx_pause & MLO_PAUSE_TX) | (rx_pause & MLO_PAUSE_RX);
		state->an_complete = an_status & XXV_AN_COMPLETE_MASK;

		/* rx status bit indicates current status of link */
		state->link = axienet_ior(lp, XXVS_RX_STATUS_REG1) & XXVS_RX_STATUS_MASK;
	} else {
		struct mdio_device *pcs_phy = pcs_to_axienet_local(pcs)->pcs_phy;

		phylink_mii_c22_pcs_get_state(pcs_phy, state);
	}
}

static void axienet_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct axienet_local *lp = pcs_to_axienet_local(pcs);

	if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_1G_10G_25G) {
		axienet_iow(lp, XXV_AN_CTL1_OFFSET,
			    (axienet_ior(lp, XXV_AN_CTL1_OFFSET) |
			     XXV_AN_RESTART_MASK));
	} else {
		struct mdio_device *pcs_phy = pcs_to_axienet_local(pcs)->pcs_phy;

		phylink_mii_c22_pcs_an_restart(pcs_phy);
	}
}

static int axienet_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			      phy_interface_t interface,
			      const unsigned long *advertising,
			      bool permit_pause_to_mac)
{
	struct axienet_local *lp = pcs_to_axienet_local(pcs);
	struct mdio_device *pcs_phy = lp->pcs_phy;
	struct net_device *ndev = lp->ndev;
	int ret;

	if (lp->switch_x_sgmii) {
		ret = mdiodev_write(pcs_phy, XLNX_MII_STD_SELECT_REG,
				    interface == PHY_INTERFACE_MODE_SGMII ?
					XLNX_MII_STD_SELECT_SGMII : 0);
		if (ret < 0) {
			netdev_warn(ndev,
				    "Failed to switch PHY interface: %d\n",
				    ret);
			return ret;
		}
	}
	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		if (!lp->auto_neg)
			return 0;

		u32 autoneg_complete;

		autoneg_complete = (axienet_ior(lp, XXV_STAT_AN_STS_OFFSET) &
				    XXV_AN_COMPLETE_MASK);

		/* If auto-negotiation is not completed, restart auto-neg */
		return (neg_mode == (unsigned int)PHYLINK_PCS_NEG_INBAND_ENABLED &&
			autoneg_complete == 0);
	} else if (lp->axienet_config->mactype == XAXIENET_1G_10G_25G) {
		bool an_enabled = false;

		if (phylink_test(advertising, Autoneg))
			an_enabled = true;

		if (!an_enabled) {
			/* Disable autoneg */
			axienet_iow(lp, XXVS_AN_CTL1_OFFSET,
				    (axienet_ior(lp, XXVS_AN_CTL1_OFFSET) &
				    (~XXVS_AN_ENABLE_MASK | XXVS_AN_BYPASS)));
			axienet_iow(lp, XXVS_RESET_OFFSET, XXVS_RX_SERDES_RESET);
			axienet_iow(lp, XXVS_LT_CTL_OFFSET, 0);
			axienet_iow(lp, XXVS_RESET_OFFSET, XXVS_RX_RESET | XXVS_TX_RESET);
			axienet_iow(lp, XXVS_RESET_OFFSET, 0);
		}

		/* Workaround: pcs_config expects to configure pcs based on link modes,
		 * but we're using advertise to depict what to configure.
		 */
		if (linkmode_test_bit(ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
				      advertising)) {
			if (an_enabled)
				axienet_config_autoneg_link_training(lp, XXVS_AN_25G_ABILITY_MASK);
			else
				axienet_iow(lp, XXVS_TC_OFFSET, (axienet_ior(lp, XXVS_TC_OFFSET) &
					     XXVS_CTRL_CORE_SPEED_SEL_CLEAR));
		} else if (linkmode_test_bit(ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
			   advertising)) {
			if (an_enabled)
				axienet_config_autoneg_link_training(lp, XXVS_AN_10G_ABILITY_MASK);
			else
				axienet_iow(lp, XXVS_TC_OFFSET, (axienet_ior(lp, XXVS_TC_OFFSET) &
					    XXVS_CTRL_CORE_SPEED_SEL_CLEAR) |
					    XXVS_CTRL_CORE_SPEED_SEL_10G);

		} else if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
			   advertising)) {
			if (an_enabled)
				axienet_config_autoneg_link_training(lp, XXVS_AN_1G_ABILITY_MASK);
			else
				axienet_iow(lp, XXVS_TC_OFFSET, (axienet_ior(lp, XXVS_TC_OFFSET) &
					    XXVS_CTRL_CORE_SPEED_SEL_CLEAR) |
					    XXVS_CTRL_CORE_SPEED_SEL_1G);
		}
		return 0;
	} else if (lp->axienet_config->mactype == XAXIENET_DCMAC) {
		/* Nothing to change for fixed link */
		return 0;
	}

	ret = phylink_mii_c22_pcs_config(pcs_phy, interface, advertising,
					 neg_mode);
	if (ret < 0)
		netdev_warn(ndev, "Failed to configure PCS: %d\n", ret);

	return ret;
}

static int axienet_pcs_validate(struct phylink_pcs *pcs, unsigned long *supported,
				const struct phylink_link_state *state)
{
	struct axienet_local *lp = pcs_to_axienet_local(pcs);

	if (lp->axienet_config->mactype == XAXIENET_1G_10G_25G) {
		int inf;

		for_each_set_bit(inf, lp->phylink_config.supported_interfaces,
				 PHY_INTERFACE_MODE_MAX)
			__set_bit(inf, supported);
	}

	return 0;
}

static const struct phylink_pcs_ops axienet_pcs_ops = {
	.pcs_get_state = axienet_pcs_get_state,
	.pcs_config = axienet_pcs_config,
	.pcs_an_restart = axienet_pcs_an_restart,
	.pcs_validate = axienet_pcs_validate,
};

static struct phylink_pcs *axienet_mac_select_pcs(struct phylink_config *config,
						  phy_interface_t interface)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);

	return &lp->pcs;
}

static void axienet_mac_config(struct phylink_config *config, unsigned int mode,
			       const struct phylink_link_state *state)
{
	/* nothing meaningful to do */
}

static void axienet_mac_link_down(struct phylink_config *config,
				  unsigned int mode,
				  phy_interface_t interface)
{
	/* nothing meaningful to do */
}

static void axienet_mac_link_up(struct phylink_config *config,
				struct phy_device *phy,
				unsigned int mode, phy_interface_t interface,
				int speed, int duplex,
				bool tx_pause, bool rx_pause)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);
	u32 emmc_reg, fcc_reg;

	if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_1G_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_DCMAC) {
		/* nothing meaningful to do */
		return;
	}

	emmc_reg = axienet_ior(lp, XAE_EMMC_OFFSET);
	emmc_reg &= ~XAE_EMMC_LINKSPEED_MASK;

	switch (speed) {
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
		dev_err(&ndev->dev,
			"Speed other than 10, 100 or 1Gbps is not supported\n");
		break;
	}

	axienet_iow(lp, XAE_EMMC_OFFSET, emmc_reg);

	fcc_reg = axienet_ior(lp, XAE_FCC_OFFSET);
	if (tx_pause)
		fcc_reg |= XAE_FCC_FCTX_MASK;
	else
		fcc_reg &= ~XAE_FCC_FCTX_MASK;
	if (rx_pause)
		fcc_reg |= XAE_FCC_FCRX_MASK;
	else
		fcc_reg &= ~XAE_FCC_FCRX_MASK;
	axienet_iow(lp, XAE_FCC_OFFSET, fcc_reg);
}

static const struct phylink_mac_ops axienet_phylink_ops = {
	.mac_select_pcs = axienet_mac_select_pcs,
	.mac_config = axienet_mac_config,
	.mac_link_down = axienet_mac_link_down,
	.mac_link_up = axienet_mac_link_up,
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

static int axienet_dma_clk_init(struct platform_device *pdev)
{
	int err;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);

	/* The "dma_clk" is deprecated and will be removed sometime in
	 * the future. For proper clock usage check axiethernet binding
	 * documentation.
	 */
	lp->dma_tx_clk = devm_clk_get(&pdev->dev, "dma_clk");
	if (IS_ERR(lp->dma_tx_clk)) {
		if (PTR_ERR(lp->dma_tx_clk) != -ENOENT) {
			err = PTR_ERR(lp->dma_tx_clk);
			return err;
		}

		lp->dma_tx_clk = devm_clk_get(&pdev->dev, "m_axi_mm2s_aclk");
		if (IS_ERR(lp->dma_tx_clk)) {
			if (PTR_ERR(lp->dma_tx_clk) != -ENOENT) {
				err = PTR_ERR(lp->dma_tx_clk);
				return err;
			}
			lp->dma_tx_clk = NULL;
		}
	} else {
		dev_warn(&pdev->dev, "dma_clk is deprecated and will be removed sometime in the future\n");
	}

	lp->dma_rx_clk = devm_clk_get(&pdev->dev, "m_axi_s2mm_aclk");
	if (IS_ERR(lp->dma_rx_clk)) {
		if (PTR_ERR(lp->dma_rx_clk) != -ENOENT) {
			err = PTR_ERR(lp->dma_rx_clk);
			return err;
		}
		lp->dma_rx_clk = NULL;
	}

	lp->dma_sg_clk = devm_clk_get(&pdev->dev, "m_axi_sg_aclk");
	if (IS_ERR(lp->dma_sg_clk)) {
		if (PTR_ERR(lp->dma_sg_clk) != -ENOENT) {
			err = PTR_ERR(lp->dma_sg_clk);
			return err;
		}
		lp->dma_sg_clk = NULL;
	}

	err = clk_prepare_enable(lp->dma_tx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable tx_clk/dma_clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(lp->dma_rx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable rx_clk (%d)\n", err);
		goto err_disable_txclk;
	}

	err = clk_prepare_enable(lp->dma_sg_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable sg_clk (%d)\n", err);
		goto err_disable_rxclk;
	}

	return 0;

err_disable_rxclk:
	clk_disable_unprepare(lp->dma_rx_clk);
err_disable_txclk:
	clk_disable_unprepare(lp->dma_tx_clk);

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

static int xxvenet_clk_init(struct platform_device *pdev,
			    struct clk **axi_aclk, struct clk **axis_clk,
			    struct clk **tmpclk, struct clk **dclk)
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

		*axi_aclk = devm_clk_get(&pdev->dev, "s_axi_aclk");
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

	*axis_clk = devm_clk_get(&pdev->dev, "rx_core_clk");
	if (IS_ERR(*axis_clk)) {
		if (PTR_ERR(*axis_clk) != -ENOENT) {
			err = PTR_ERR(*axis_clk);
			return err;
		}
		*axis_clk = NULL;
	}

	*dclk = devm_clk_get(&pdev->dev, "dclk");
	if (IS_ERR(*dclk)) {
		if (PTR_ERR(*dclk) != -ENOENT) {
			err = PTR_ERR(*dclk);
			return err;
		}
		*dclk = NULL;
	}

	err = clk_prepare_enable(*axi_aclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_clk/ethernet_clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(*axis_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axis_clk (%d)\n", err);
		goto err_disable_axi_aclk;
	}

	err = clk_prepare_enable(*dclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dclk (%d)\n", err);
		goto err_disable_axis_clk;
	}

	return 0;

err_disable_axis_clk:
	clk_disable_unprepare(*axis_clk);
err_disable_axi_aclk:
	clk_disable_unprepare(*axi_aclk);

	return err;
}

static const struct axienet_config axienet_1_2p5g_config = {
	.mactype = XAXIENET_1_2p5G,
	.setoptions = axienet_setoptions,
	.clk_init = axienet_clk_init,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_10g_config = {
	.mactype = XAXIENET_LEGACY_10G,
	.setoptions = axienet_setoptions,
	.clk_init = xxvenet_clk_init,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_10g25g_config = {
	.mactype = XAXIENET_10G_25G,
	.setoptions = xxvenet_setoptions,
	.clk_init = xxvenet_clk_init,
	.tx_ptplen = XXV_TX_PTP_LEN,
	.ts_header_len = XXVENET_TS_HEADER_LEN,
	.gt_reset = xxv_gt_reset,
};

static const struct axienet_config axienet_1g10g25g_config = {
	.mactype = XAXIENET_1G_10G_25G,
	.setoptions = xxvenet_setoptions,
	.clk_init = xxvenet_clk_init,
	.tx_ptplen = XXV_TX_PTP_LEN,
	.gt_reset = xxv_gt_reset,
};

static const struct axienet_config axienet_usxgmii_config = {
	.mactype = XAXIENET_10G_25G,
	.setoptions = xxvenet_setoptions,
	.clk_init = xxvenet_clk_init,
	.tx_ptplen = 0,
};

static const struct axienet_config axienet_mrmac_config = {
	.mactype = XAXIENET_MRMAC,
	.setoptions = xxvenet_setoptions,
	.clk_init = xxvenet_clk_init,
	.tx_ptplen = XXV_TX_PTP_LEN,
	.ts_header_len = MRMAC_TS_HEADER_LEN,
	.gt_reset = axienet_mrmac_gt_reset,
};

static const struct axienet_config axienet_dcmac_config = {
	.mactype = XAXIENET_DCMAC,
	.clk_init = xxvenet_clk_init,
	.gt_reset = dcmac_gt_reset,
};

/* Match table for of_platform binding */
static const struct of_device_id axienet_of_match[] = {
	{ .compatible = "xlnx,axi-ethernet-1.00.a", .data = &axienet_1_2p5g_config},
	{ .compatible = "xlnx,axi-ethernet-1.01.a", .data = &axienet_1_2p5g_config},
	{ .compatible = "xlnx,axi-ethernet-2.01.a", .data = &axienet_1_2p5g_config},
	{ .compatible = "xlnx,axi-2_5-gig-ethernet-1.0",
						.data = &axienet_1_2p5g_config},
	{ .compatible = "xlnx,ten-gig-eth-mac", .data = &axienet_10g_config},
	{ .compatible = "xlnx,xxv-ethernet-1.0",
						.data = &axienet_10g25g_config},
	{ .compatible = "xlnx,xxv-usxgmii-ethernet-1.0",
					.data = &axienet_usxgmii_config},
	{ .compatible = "xlnx,mrmac-ethernet-1.0",
					.data = &axienet_mrmac_config},
	{ .compatible = "xlnx,ethernet-1-10-25g-2.7",
					.data = &axienet_1g10g25g_config},
	{ .compatible = "xlnx,dcmac-2.4",
					.data = &axienet_dcmac_config},
	{},
};

MODULE_DEVICE_TABLE(of, axienet_of_match);

static int axienet_eoe_netdev_event(struct notifier_block *this, unsigned long event,
				    void *ptr)
{
	struct axienet_local *lp = container_of(this, struct axienet_local,
						inetaddr_notifier);
	struct in_ifaddr *ifa = ptr;
	struct axienet_dma_q *q;
	int i;

	struct net_device *ndev = ifa->ifa_dev->dev;

	if (lp->ndev != ndev) {
		dev_err(lp->dev, " ndev is not matched to configure GRO IP address\n");
	} else {
		switch (event) {
		case NETDEV_UP:
			dev_dbg(lp->dev, "%s:NETDEV_UP\n", __func__);
			for_each_rx_dma_queue(lp, i) {
				q = lp->dq[i];
				if (axienet_eoe_is_channel_gro(lp, q))
					axienet_eoe_iow(lp,
							XEOE_UDP_GRO_DST_IP_OFFSET(q->chan_id),
							ntohl(ifa->ifa_address));
			}
		break;
		case NETDEV_DOWN:
			dev_dbg(lp->dev, "%s:NETDEV_DOWN\n", __func__);
			for_each_rx_dma_queue(lp, i) {
				q = lp->dq[i];
				if (axienet_eoe_is_channel_gro(lp, q))
					axienet_eoe_iow(lp,
							XEOE_UDP_GRO_DST_IP_OFFSET(q->chan_id),
							0);
			}
		break;
		default:
			dev_err(lp->dev, "IPv4 Ethernet address is not set\n");
		}
	}

	return NOTIFY_DONE;
}

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
	struct device_node *np;
	struct axienet_local *lp;
	struct net_device *ndev;
	struct resource *ethres;
	u8 mac_addr[ETH_ALEN];
	u32 value;

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	struct resource txtsres, rxtsres;
#endif
	u16 num_queues = XAE_MAX_QUEUES;

	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-queues",
				   &num_queues);
	if (ret) {
#ifndef CONFIG_AXIENET_HAS_MCDMA
		num_queues = 1;
#endif
	}

	ndev = alloc_etherdev_mq(sizeof(*lp), num_queues);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->features = NETIF_F_SG;
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

	u64_stats_init(&lp->rx_stat_sync);
	u64_stats_init(&lp->tx_stat_sync);

	mutex_init(&lp->stats_lock);
	seqcount_mutex_init(&lp->hw_stats_seqcount, &lp->stats_lock);
	INIT_DEFERRABLE_WORK(&lp->stats_work, axienet_refresh_stats);
	INIT_LIST_HEAD(&lp->rx_fs_list.list);

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

	/* Map device registers */
	lp->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &ethres);
	if (IS_ERR(lp->regs)) {
		ret = PTR_ERR(lp->regs);
		goto cleanup_clk;
	}
	lp->regs_start = ethres->start;

	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_node(axienet_of_match, pdev->dev.of_node);
		if (match && match->data) {
			lp->axienet_config = match->data;
			axienet_clk_init = lp->axienet_config->clk_init;
		}
	}

	/* Setup checksum offload, but default to off if not specified */
	lp->features = 0;
#ifndef CONFIG_AXIENET_HAS_MCDMA
	if (lp->axienet_config->mactype == XAXIENET_1_2p5G) {
		if (axienet_ior(lp, XAE_ABILITY_OFFSET) & XAE_ABILITY_STATS)
			lp->features |= XAE_FEATURE_STATS;
	}
#endif

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,txcsum", &value);
	if (!ret) {
		switch (value) {
		case 1:
			lp->features |= XAE_FEATURE_PARTIAL_TX_CSUM;
			/* Can checksum any contiguous range */
			ndev->features |= NETIF_F_HW_CSUM;
			break;
		case 2:
			lp->features |= XAE_FEATURE_FULL_TX_CSUM;
			/* Can checksum TCP/UDP over IPv4. */
			ndev->features |= NETIF_F_IP_CSUM;
			break;
		}
	}
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,rxcsum", &value);
	if (!ret) {
		switch (value) {
		case 1:
			lp->features |= XAE_FEATURE_PARTIAL_RX_CSUM;
			ndev->features |= NETIF_F_RXCSUM;
			break;
		case 2:
			lp->features |= XAE_FEATURE_FULL_RX_CSUM;
			ndev->features |= NETIF_F_RXCSUM;
			break;
		}
	}
	/* For supporting jumbo frames, the Axi Ethernet hardware must have
	 * a larger Rx/Tx Memory. Typically, the size must be large so that
	 * we can enable jumbo option and start supporting jumbo frames.
	 * Here we check for memory allocated for Rx/Tx in the hardware from
	 * the device-tree and accordingly set flags.
	 */
	of_property_read_u32(pdev->dev.of_node, "xlnx,rxmem", &lp->rxmem);

	lp->switch_x_sgmii = of_property_read_bool(pdev->dev.of_node,
						   "xlnx,switch-x-sgmii");

	/* Start with the proprietary, and broken phy_type */
	if (lp->axienet_config->mactype != XAXIENET_MRMAC) {
		ret = of_property_read_u32(pdev->dev.of_node, "xlnx,phy-type", &value);
		if (!ret) {
			switch (value) {
			case XAE_PHY_TYPE_MII:
				lp->phy_mode = PHY_INTERFACE_MODE_MII;
				break;
			case XAE_PHY_TYPE_GMII:
				lp->phy_mode = PHY_INTERFACE_MODE_GMII;
				break;
			case XAE_PHY_TYPE_RGMII_2_0:
				lp->phy_mode = PHY_INTERFACE_MODE_RGMII_ID;
				break;
			case XAE_PHY_TYPE_SGMII:
				lp->phy_mode = PHY_INTERFACE_MODE_SGMII;
				break;
			case XAE_PHY_TYPE_1000BASE_X:
				lp->phy_mode = PHY_INTERFACE_MODE_1000BASEX;
				break;
			case XXE_PHY_TYPE_USXGMII:
				lp->phy_mode = PHY_INTERFACE_MODE_USXGMII;
				break;
			default:
				/* Don't error out as phy-type is an optional property */
				break;
			}
		} else {
			ret = of_get_phy_mode(pdev->dev.of_node, &lp->phy_mode);
			if (ret)
				goto cleanup_clk;
		}
	}

	/* Set default USXGMII rate */
	lp->usxgmii_rate = SPEED_1000;
	of_property_read_u32(pdev->dev.of_node, "xlnx,usxgmii-rate",
			     &lp->usxgmii_rate);

	if (lp->axienet_config->mactype == XAXIENET_1_2p5G ||
	    lp->axienet_config->mactype == XAXIENET_MRMAC ||
	    lp->axienet_config->mactype == XAXIENET_DCMAC) {
		ret = of_property_read_u32(pdev->dev.of_node, "max-speed",
					   &lp->max_speed);

		if (ret && lp->axienet_config->mactype == XAXIENET_MRMAC) {
			ret = of_property_read_u32(pdev->dev.of_node,
						   "xlnx,mrmac-rate",
						   &lp->max_speed);
			if (!ret) {
				dev_warn(&pdev->dev,
					 "xlnx,mrmac-rate is deprecated, please use max-speed instead\n");
			}
		}
		if (ret) {
			dev_err(&pdev->dev, "couldn't find MAC Rate\n");
			goto cleanup_clk;
		}
	}
	if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
		const char *gt_mode;

		/* Default to GT wide mode */
		lp->gt_mode_narrow = false;

		ret = of_property_read_string(pdev->dev.of_node,
					      "xlnx,gt-mode",
					      &gt_mode);
		if (ret != -EINVAL && !strcasecmp(gt_mode, GT_MODE_NARROW))
			lp->gt_mode_narrow = true;

		/* Default AXI4-stream data widths */
		if (lp->max_speed == SPEED_10000)
			lp->mrmac_stream_dwidth = MRMAC_STREAM_DWIDTH_32;
		else
			lp->mrmac_stream_dwidth = MRMAC_STREAM_DWIDTH_64;

		of_property_read_u32(pdev->dev.of_node,
				     "xlnx,axistream-dwidth",
				     &lp->mrmac_stream_dwidth);
	}

	lp->eth_hasnobuf = of_property_read_bool(pdev->dev.of_node,
						 "xlnx,eth-hasnobuf");
	lp->eth_hasptp = of_property_read_bool(pdev->dev.of_node,
					       "xlnx,eth-hasptp");

	if (lp->axienet_config->mactype == XAXIENET_10G_25G)
		lp->auto_neg = of_property_read_bool(pdev->dev.of_node,
						     "xlnx,has-auto-neg");

	if (lp->axienet_config->mactype == XAXIENET_10G_25G  ||
	    lp->axienet_config->mactype == XAXIENET_1G_10G_25G)
		lp->xxv_ip_version = axienet_ior(lp, XXV_CONFIG_REVISION);

	if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
		struct resource gtpll, gtctrl;

		if (mrmac_pll_reg) {
			lp->gt_pll = mrmac_gt_pll;
			lp->gt_ctrl = mrmac_gt_ctrl;
		} else {
			np = of_parse_phandle(pdev->dev.of_node,
					      "xlnx,gtpll", 0);
			if (IS_ERR(np)) {
				dev_err(&pdev->dev,
					"couldn't find GT PLL\n");
				ret = PTR_ERR(np);
				goto cleanup_clk;
			}

			ret = of_address_to_resource(np, 0, &gtpll);
			if (ret) {
				dev_err(&pdev->dev,
					"unable to get GT PLL resource\n");
				goto cleanup_clk;
			}

			lp->gt_pll = devm_ioremap_resource(&pdev->dev,
							   &gtpll);
			if (IS_ERR(lp->gt_pll)) {
				dev_err(&pdev->dev,
					"couldn't map GT PLL regs\n");
				ret = PTR_ERR(lp->gt_pll);
				goto cleanup_clk;
			}

			np = of_parse_phandle(pdev->dev.of_node,
					      "xlnx,gtctrl", 0);
			if (IS_ERR(np)) {
				dev_err(&pdev->dev,
					"couldn't find GT control\n");
				ret = PTR_ERR(np);
				goto cleanup_clk;
			}

			ret = of_address_to_resource(np, 0, &gtctrl);
			if (ret) {
				dev_err(&pdev->dev,
					"unable to get GT control resource\n");
				goto cleanup_clk;
			}

			lp->gt_ctrl = devm_ioremap_resource(&pdev->dev,
							    &gtctrl);
			if (IS_ERR(lp->gt_ctrl)) {
				dev_err(&pdev->dev,
					"couldn't map GT control regs\n");
				ret = PTR_ERR(lp->gt_ctrl);
				goto cleanup_clk;
			}

			mrmac_gt_pll = lp->gt_pll;
			mrmac_gt_ctrl = lp->gt_ctrl;
			mrmac_pll_reg = 1;
		}
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		ret = of_property_read_u32(pdev->dev.of_node, "xlnx,phcindex",
					   &lp->phc_index);
		if (!ret)
			dev_warn(&pdev->dev, "xlnx,phcindex is deprecated, please use ptp-hardware-clock instead\n");
#endif
		ret = of_property_read_u32(pdev->dev.of_node, "xlnx,gtlane",
					   &lp->gt_lane);
		if (ret) {
			dev_err(&pdev->dev, "MRMAC GT lane information missing\n");
			goto cleanup_clk;
		}
		dev_info(&pdev->dev, "GT lane: %d\n", lp->gt_lane);
	} else if (lp->axienet_config->mactype == XAXIENET_DCMAC) {
		lp->gds_gt_ctrl = devm_gpiod_get_array(&pdev->dev,
						       "gt_ctrl",
						       GPIOD_OUT_LOW);
		if (IS_ERR(lp->gds_gt_ctrl)) {
			dev_err(&pdev->dev,
				"Failed to request GT control GPIO\n");
			ret = PTR_ERR(lp->gds_gt_ctrl);
			goto cleanup_clk;
		}

		lp->gds_gt_rx_dpath = devm_gpiod_get_array(&pdev->dev,
							   "gt_rx_dpath",
							    GPIOD_OUT_LOW);
		if (IS_ERR(lp->gds_gt_rx_dpath)) {
			dev_err(&pdev->dev,
				"Failed to request GT Rx dpath GPIO\n");
			ret = PTR_ERR(lp->gds_gt_rx_dpath);
			goto cleanup_clk;
		}

		lp->gds_gt_tx_dpath = devm_gpiod_get_array(&pdev->dev,
							   "gt_tx_dpath",
							   GPIOD_OUT_LOW);
		if (IS_ERR(lp->gds_gt_tx_dpath)) {
			dev_err(&pdev->dev,
				"Failed to request GT Tx dpath GPIO\n");
			ret = PTR_ERR(lp->gds_gt_tx_dpath);
			goto cleanup_clk;
		}

		lp->gds_gt_rsts = devm_gpiod_get_array(&pdev->dev,
						       "gt_rsts",
						       GPIOD_OUT_LOW);
		if (IS_ERR(lp->gds_gt_rsts)) {
			dev_err(&pdev->dev,
				"Failed to request GT Resets GPIO\n");
			ret = PTR_ERR(lp->gds_gt_rsts);
			goto cleanup_clk;
		}

		lp->gds_gt_tx_reset_done =  devm_gpiod_get_array(&pdev->dev,
								 "gt_tx_rst_done",
								 GPIOD_IN);
		if (IS_ERR(lp->gds_gt_tx_reset_done)) {
			dev_err(&pdev->dev,
				"Failed to request GT Tx Reset Done GPIO\n");
			ret = PTR_ERR(lp->gds_gt_tx_reset_done);
			goto cleanup_clk;
		}

		lp->gds_gt_rx_reset_done =  devm_gpiod_get_array(&pdev->dev,
								 "gt_rx_rst_done",
								 GPIOD_IN);
		if (IS_ERR(lp->gds_gt_rx_reset_done)) {
			dev_err(&pdev->dev,
				"Failed to request GT Rx Reset Done GPIO\n");
			ret = PTR_ERR(lp->gds_gt_rx_reset_done);
			goto cleanup_clk;
		}
	}

	if (!of_property_present(pdev->dev.of_node, "dmas")) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		ret = axienet_mcdma_probe(pdev, lp, ndev);
#else
		ret = axienet_dma_probe(pdev, ndev);
#endif
		if (ret)
			goto cleanup_clk;
		if (lp->axienet_config->mactype == XAXIENET_1_2p5G &&
		    !lp->eth_hasnobuf)
			lp->eth_irq = platform_get_irq(pdev, 0);

		/* Check for Ethernet core IRQ (optional) */
		if (lp->eth_irq <= 0)
			dev_info(&pdev->dev, "Ethernet core IRQ not defined\n");

		if (dma_set_mask_and_coherent(lp->dev, DMA_BIT_MASK(lp->dma_mask)) != 0) {
			dev_warn(&pdev->dev, "default to %d-bit dma mask\n", XAE_DMA_MASK_MIN);
			if (dma_set_mask_and_coherent(lp->dev,
						      DMA_BIT_MASK(XAE_DMA_MASK_MIN)) != -3) {
				dev_err(&pdev->dev, "dma_set_mask_and_coherent failed, aborting\n");
				goto cleanup_clk;
			}
		}

		ret = axienet_dma_clk_init(pdev);
		if (ret) {
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "DMA clock init failed %d\n", ret);
			goto cleanup_clk;
		}

		ret = axienet_clk_init(pdev, &lp->aclk, &lp->eth_sclk,
				       &lp->eth_refclk, &lp->eth_dclk);
		if (ret) {
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "Ethernet clock init failed %d\n", ret);
			goto err_disable_clk;
		}

#ifdef CONFIG_AXIENET_HAS_MCDMA
		/* Create sysfs file entries for the device */
		ret = axeinet_mcdma_create_sysfs(&lp->dev->kobj);
		if (ret < 0) {
			dev_err(lp->dev, "unable to create sysfs entries\n");
			goto err_disable_clk;
		}
#endif

		/* Autodetect the need for 64-bit DMA pointers.
		 * When the IP is configured for a bus width bigger than 32 bits,
		 * writing the MSB registers is mandatory, even if they are all 0.
		 * We can detect this case by writing all 1's to one such register
		 * and see if that sticks: when the IP is configured for 32 bits
		 * only, those registers are RES0.
		 * Those MSB registers were introduced in IP v7.1, which we check first.
		 */
		if (lp->axienet_config->mactype == XAXIENET_1_2p5G) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
			if (lp->dma_mask > XAE_DMA_MASK_MIN)
				lp->features |= XAE_FEATURE_DMA_64BIT;
#else
			if ((axienet_ior(lp, XAE_ID_OFFSET) >> 24) >= 0x9) {
				void __iomem *desc = lp->dq[0]->dma_regs +
						     XAXIDMA_TX_CDESC_OFFSET + 4;
				iowrite32(0x0, desc);
				if (ioread32(desc) == 0) {	/* sanity check */
					iowrite32(0xffffffff, desc);
					if (ioread32(desc) > 0) {
						lp->features |= XAE_FEATURE_DMA_64BIT;
						dev_info(&pdev->dev,
							 "autodetected 64-bit DMA range\n");
					}
					iowrite32(0x0, desc);
				}
			}
			if (!IS_ENABLED(CONFIG_64BIT) && lp->features & XAE_FEATURE_DMA_64BIT) {
				dev_err(&pdev->dev, "64-bit addressable DMA is not compatible with 32-bit archecture\n");
				ret = -EINVAL;
				goto err_disable_clk;
			}
#endif
		} else if (lp->dma_mask > XAE_DMA_MASK_MIN) {
			/* High speed MACs with 64-bit DMA */
			lp->features |= XAE_FEATURE_DMA_64BIT;
		}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		/* Find AXI Stream FIFO */
		np = of_parse_phandle(pdev->dev.of_node, "axififo-connected", 0);
		if (IS_ERR(np)) {
			dev_err(&pdev->dev, "could not find TX Timestamp FIFO\n");
			ret = PTR_ERR(np);
			goto err_disable_clk;
		}

		ret = of_address_to_resource(np, 0, &txtsres);
		if (ret) {
			dev_err(&pdev->dev, "unable to get Tx Timestamp resource\n");
			goto err_disable_clk;
		}

		lp->tx_ts_regs = devm_ioremap_resource(&pdev->dev, &txtsres);
		if (IS_ERR(lp->tx_ts_regs)) {
			dev_err(&pdev->dev, "could not map Tx Timestamp regs\n");
			ret = PTR_ERR(lp->tx_ts_regs);
			goto err_disable_clk;
		}

		if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
		    lp->axienet_config->mactype == XAXIENET_MRMAC) {
			np = of_parse_phandle(pdev->dev.of_node, "xlnx,rxtsfifo",
					      0);
			if (IS_ERR(np)) {
				dev_err(&pdev->dev,
					"couldn't find rx-timestamp FIFO\n");
				ret = PTR_ERR(np);
				goto err_disable_clk;
			}

			ret = of_address_to_resource(np, 0, &rxtsres);
			if (ret) {
				dev_err(&pdev->dev,
					"unable to get rx-timestamp resource\n");
				goto err_disable_clk;
			}

			lp->rx_ts_regs = devm_ioremap_resource(&pdev->dev, &rxtsres);
			if (IS_ERR(lp->rx_ts_regs)) {
				dev_err(&pdev->dev, "couldn't map rx-timestamp regs\n");
				ret = PTR_ERR(lp->rx_ts_regs);
				goto err_disable_clk;
			}
			lp->tx_ptpheader = devm_kzalloc(&pdev->dev,
							XXVENET_TS_HEADER_LEN,
							GFP_KERNEL);
		}
		spin_lock_init(&lp->ptp_tx_lock);
		of_node_put(np);
#endif
		lp->eoe_connected = of_property_read_bool(pdev->dev.of_node,
							  "xlnx,has-hw-offload");

		if (lp->eoe_connected) {
			ret = axienet_eoe_probe(pdev);
			if (ret) {
				dev_err(&pdev->dev, "Ethernet Offload not Supported\n");
				goto cleanup_clk;
			}
		}
	} else {
		struct xilinx_vdma_config cfg;
		struct dma_chan *tx_chan;

		lp->eth_irq = platform_get_irq_optional(pdev, 0);
		if (lp->eth_irq < 0 && lp->eth_irq != -ENXIO) {
			ret = lp->eth_irq;
			goto cleanup_clk;
		}
		tx_chan = dma_request_chan(lp->dev, "tx_chan0");
		if (IS_ERR(tx_chan)) {
			ret = PTR_ERR(tx_chan);
			dev_err_probe(lp->dev, ret, "No Ethernet DMA (TX) channel found\n");
			goto cleanup_clk;
		}

		cfg.reset = 1;
		/* As name says VDMA but it has support for DMA channel reset */
		ret = xilinx_vdma_channel_set_config(tx_chan, &cfg);
		if (ret < 0) {
			dev_err(&pdev->dev, "Reset channel failed\n");
			dma_release_channel(tx_chan);
			goto cleanup_clk;
		}

		dma_release_channel(tx_chan);
		lp->use_dmaengine = 1;
	}

	if (lp->use_dmaengine)
		ndev->netdev_ops = &axienet_netdev_dmaengine_ops;
	else
		ndev->netdev_ops = &axienet_netdev_ops;

	lp->eth_irq = platform_get_irq(pdev, 0);
	/* Check for Ethernet core IRQ (optional) */
	if (lp->eth_irq <= 0)
		dev_info(&pdev->dev, "Ethernet core IRQ not defined\n");

	/* Retrieve the MAC address */
	ret = of_get_mac_address(pdev->dev.of_node, mac_addr);
	if (!ret) {
		axienet_set_mac_address(ndev, mac_addr);
	} else {
		dev_warn(&pdev->dev, "could not find MAC address property: %d\n",
			 ret);
		axienet_set_mac_address(ndev, NULL);
	}
	if (!lp->use_dmaengine) {
		lp->coalesce_count_rx = XAXIDMA_DFT_RX_THRESHOLD;
		lp->coalesce_count_tx = XAXIDMA_DFT_TX_THRESHOLD;
		lp->coalesce_usec_rx = XAXIDMA_DFT_RX_USEC;
		lp->coalesce_usec_tx = XAXIDMA_DFT_TX_USEC;

		/* Set the TX coalesce count to 1. With offload enabled, there are not as
		 * many interrupts as before and the interrupt for every 64KB segment needs
		 * to be handled immediately to ensure better performance.
		 */
		if (ndev->hw_features & NETIF_F_GSO_UDP_L4)
			lp->coalesce_count_tx = XMCDMA_DFT_TX_THRESHOLD;

		/* Update the required thresholds for Rx HW UDP GRO
		 * GRO receives 16 segmented data packets from MAC
		 * and packet coalescing increases performance.
		 */
		if (lp->eoe_features & RX_HW_UDP_GRO)
			lp->coalesce_count_rx = XMCDMA_DFT_RX_THRESHOLD;
	}
	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_1G_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC &&
	    lp->axienet_config->mactype != XAXIENET_DCMAC) {
		np = of_parse_phandle(pdev->dev.of_node, "pcs-handle", 0);
		if (!np) {
			/* For SGMII/1000BaseX:
			 * "phy-handle" is deprecated; always use "pcs-handle"
			 * for pcs_phy. Falling back to "phy-handle" here is
			 * only for backward compatiblility with old device trees"
			 * For RGMII:
			 * "phy-handle" is used to describe external PHY.
			 */
			np = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
		}
		if (!np) {
			dev_err(&pdev->dev, "pcs-handle (preferred for 1000BaseX/SGMII) or phy-handle required for external PHY\n");
			ret = -EINVAL;
			goto cleanup_mdio;
		}
		if (np) {
			ret = axienet_mdio_setup(lp);
			if (ret)
				dev_warn(&pdev->dev,
					 "error registering MDIO bus: %d\n", ret);
		}

		lp->pcs_phy = of_mdio_find_device(np);
		if (!lp->pcs_phy) {
			ret = -EPROBE_DEFER;
			of_node_put(np);
			goto cleanup_mdio;
		}
		of_node_put(np);
	}

	if (lp->axienet_config->mactype != XAXIENET_MRMAC) {
		lp->pcs.ops = &axienet_pcs_ops;
		lp->pcs.neg_mode = true;
		lp->pcs.poll = true;

		lp->phylink_config.dev = &ndev->dev;
		lp->phylink_config.type = PHYLINK_NETDEV;
		lp->phylink_config.mac_managed_pm = true;
		lp->phylink_config.mac_capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE;

		if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
			u32 core_speed;

			core_speed = axienet_ior(lp, XXV_STAT_CORE_SPEED_OFFSET);
			if (core_speed & XXV_STAT_CORE_SPEED_RTSW_MASK) {
				/* Runtime 10G/25G speed switching supported */
				lp->phylink_config.mac_capabilities |= (MAC_10000FD |
									MAC_25000FD);
				__set_bit(PHY_INTERFACE_MODE_10GBASER,
					  lp->phylink_config.supported_interfaces);
				__set_bit(PHY_INTERFACE_MODE_25GBASER,
					  lp->phylink_config.supported_interfaces);
			} else {
				if (core_speed & XXV_STAT_CORE_SPEED_10G_MASK) {
					/* Standalone 10G supported */
					lp->phylink_config.mac_capabilities |= MAC_10000FD;
					__set_bit(PHY_INTERFACE_MODE_10GBASER,
						  lp->phylink_config.supported_interfaces);
				} else {
					/* Standalone 25G supported */
					lp->phylink_config.mac_capabilities |= MAC_25000FD;
					__set_bit(PHY_INTERFACE_MODE_25GBASER,
						  lp->phylink_config.supported_interfaces);
				}
			}
		} else if (lp->axienet_config->mactype == XAXIENET_1G_10G_25G) {
			const char *rt_switch = NULL;

			of_property_read_string(pdev->dev.of_node,
						"xlnx,runtime-switch",
						&rt_switch);

			/* 1G/10G switching by default */
			lp->phylink_config.mac_capabilities |= (MAC_1000FD |
								MAC_10000FD);
			__set_bit(PHY_INTERFACE_MODE_1000BASEX,
				  lp->phylink_config.supported_interfaces);
			__set_bit(PHY_INTERFACE_MODE_10GBASER,
				  lp->phylink_config.supported_interfaces);

			if (rt_switch &&
			    !strcmp(rt_switch,
			    (const char *)XXVS_RT_SWITCH_1G_10G_25G)) {
				lp->phylink_config.mac_capabilities |= MAC_25000FD;

				__set_bit(PHY_INTERFACE_MODE_25GBASER,
					  lp->phylink_config.supported_interfaces);
			}
		} else if (lp->axienet_config->mactype == XAXIENET_DCMAC) {
			if (lp->max_speed == SPEED_100000) {
				lp->phylink_config.mac_capabilities |= MAC_100000FD;
				__set_bit(PHY_INTERFACE_MODE_100GBASER,
					  lp->phylink_config.supported_interfaces);
			} else if (lp->max_speed == SPEED_200000) {
				lp->phylink_config.mac_capabilities |= MAC_200000FD;
				__set_bit(PHY_INTERFACE_MODE_200GBASER,
					  lp->phylink_config.supported_interfaces);
			} else if (lp->max_speed == SPEED_400000) {
				lp->phylink_config.mac_capabilities |= MAC_400000FD;
				__set_bit(PHY_INTERFACE_MODE_400GBASER,
					  lp->phylink_config.supported_interfaces);
			}

			lp->phylink_config.get_fixed_state = axienet_dcmac_get_fixed_state;
		} else {
			/* AXI 1G/2.5G */
			if (lp->max_speed == SPEED_1000) {
				lp->phylink_config.mac_capabilities = (MAC_10FD | MAC_100FD |
								       MAC_1000FD);
				if (lp->switch_x_sgmii)
					__set_bit(PHY_INTERFACE_MODE_SGMII |
						  PHY_INTERFACE_MODE_1000BASEX,
						  lp->phylink_config.supported_interfaces);

			} else {
				/* 2.5G speed */
				lp->phylink_config.mac_capabilities |= MAC_2500FD;
				if (lp->switch_x_sgmii)
					__set_bit(PHY_INTERFACE_MODE_SGMII |
						  PHY_INTERFACE_MODE_1000BASEX,
						  lp->phylink_config.supported_interfaces);
			}
		}
	}

	__set_bit(lp->phy_mode, lp->phylink_config.supported_interfaces);

	if (lp->axienet_config->mactype != XAXIENET_MRMAC)
		lp->phylink = phylink_create(&lp->phylink_config, pdev->dev.fwnode,
					     lp->phy_mode,
					     &axienet_phylink_ops);
	if (IS_ERR(lp->phylink)) {
		ret = PTR_ERR(lp->phylink);
		dev_err(&pdev->dev, "phylink_create error (%i)\n", ret);
		goto cleanup_mdio;
	}

	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
		goto cleanup_phylink;
	}

	/* Register notifier for inet address additions/deletions.
	 * It should be called after register_netdev to access the interface's
	 * network configuration parameters.
	 */

	if (lp->eoe_features & RX_HW_UDP_GRO) {
		lp->inetaddr_notifier.notifier_call = axienet_eoe_netdev_event;
		ret = register_inetaddr_notifier(&lp->inetaddr_notifier);
		if (ret) {
			dev_err(lp->dev, "register_netdevice_notifier() error\n");
			goto err_unregister_netdev;
		}
	}

	return 0;

err_unregister_netdev:
	unregister_netdev(ndev);

cleanup_phylink:
	phylink_destroy(lp->phylink);

cleanup_mdio:
	if (lp->pcs_phy)
		put_device(&lp->pcs_phy->dev);
	if (lp->mii_bus)
		axienet_mdio_teardown(lp);
err_disable_clk:
	axienet_clk_disable(pdev);

cleanup_clk:
	clk_bulk_disable_unprepare(XAE_NUM_MISC_CLOCKS, lp->misc_clks);
	clk_disable_unprepare(lp->axi_clk);

free_netdev:
	free_netdev(ndev);

	return ret;
}

static void axienet_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);
	int i;

	for_each_rx_dma_queue(lp, i) {
		netif_napi_del(&lp->dq[i]->napi_rx);
		netif_napi_del(&lp->dq[i]->napi_tx);
	}

	if (lp->eoe_features & RX_HW_UDP_GRO)
		unregister_inetaddr_notifier(&lp->inetaddr_notifier);

	unregister_netdev(ndev);
	axienet_clk_disable(pdev);

	if (lp->phylink)
		phylink_destroy(lp->phylink);

	if (lp->pcs_phy)
		put_device(&lp->pcs_phy->dev);

	if (lp->mii_bus)
		axienet_mdio_teardown(lp);

	clk_bulk_disable_unprepare(XAE_NUM_MISC_CLOCKS, lp->misc_clks);
	clk_disable_unprepare(lp->axi_clk);
#ifdef CONFIG_AXIENET_HAS_MCDMA
	axeinet_mcdma_remove_sysfs(&lp->dev->kobj);
#endif

	free_netdev(ndev);
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

static int axienet_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	if (!netif_running(ndev))
		return 0;

	netif_device_detach(ndev);

	rtnl_lock();
	axienet_stop(ndev);
	rtnl_unlock();

	return 0;
}

static int axienet_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	if (!netif_running(ndev))
		return 0;

	rtnl_lock();
	axienet_open(ndev);
	rtnl_unlock();

	netif_device_attach(ndev);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(axienet_pm_ops,
				axienet_suspend, axienet_resume);

static struct platform_driver axienet_driver = {
	.probe = axienet_probe,
	.remove_new = axienet_remove,
	.shutdown = axienet_shutdown,
	.driver = {
		 .name = "xilinx_axienet",
		 .pm = &axienet_pm_ops,
		 .of_match_table = axienet_of_match,
	},
};

module_platform_driver(axienet_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL");

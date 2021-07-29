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

#include "xilinx_axienet.h"

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

#ifdef CONFIG_XILINX_TSN_PTP
int axienet_phc_index = -1;
EXPORT_SYMBOL(axienet_phc_index);
#endif

void __iomem *mrmac_gt_pll;
EXPORT_SYMBOL(mrmac_gt_pll);

void __iomem *mrmac_gt_ctrl;
EXPORT_SYMBOL(mrmac_gt_ctrl);

int mrmac_pll_reg;
EXPORT_SYMBOL(mrmac_pll_reg);

int mrmac_pll_rst;
EXPORT_SYMBOL(mrmac_pll_rst);

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
		axienet_mcdma_rx_bd_free(ndev, lp->dq[i]);
#else
		axienet_bd_free(ndev, lp->dq[i]);
#endif
	}
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
	int i;
	u32 reg, af0reg, af1reg;
	struct axienet_local *lp = netdev_priv(ndev);

	if ((lp->axienet_config->mactype != XAXIENET_1G) || lp->eth_hasnobuf)
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
void axienet_setoptions(struct net_device *ndev, u32 options)
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
	u32 val, reg;

	val = axienet_ior(lp, MRMAC_RESET_OFFSET);
	val |= (MRMAC_RX_SERDES_RST_MASK | MRMAC_TX_SERDES_RST_MASK |
		MRMAC_RX_RST_MASK | MRMAC_TX_RST_MASK);
	axienet_iow(lp, MRMAC_RESET_OFFSET, val);
	mdelay(MRMAC_RESET_DELAY);

	reg = axienet_ior(lp, MRMAC_MODE_OFFSET);
	if (lp->mrmac_rate == SPEED_25000) {
		reg &= ~MRMAC_CTL_RATE_CFG_MASK;
		reg |= MRMAC_CTL_DATA_RATE_25G;
		reg |= (MRMAC_CTL_AXIS_CFG_25G_IND << MRMAC_CTL_AXIS_CFG_SHIFT);
		reg |= (MRMAC_CTL_SERDES_WIDTH_25G <<
			MRMAC_CTL_SERDES_WIDTH_SHIFT);
	} else {
		reg &= ~MRMAC_CTL_RATE_CFG_MASK;
		reg |= MRMAC_CTL_DATA_RATE_10G;
		reg |= (MRMAC_CTL_AXIS_CFG_10G_IND << MRMAC_CTL_AXIS_CFG_SHIFT);
		reg |= (MRMAC_CTL_SERDES_WIDTH_10G <<
			MRMAC_CTL_SERDES_WIDTH_SHIFT);
	}

	/* For tick reg */
	reg |= MRMAC_CTL_PM_TICK_MASK;
	axienet_iow(lp, MRMAC_MODE_OFFSET, reg);

	val = axienet_ior(lp, MRMAC_RESET_OFFSET);
	val &= ~(MRMAC_RX_SERDES_RST_MASK | MRMAC_TX_SERDES_RST_MASK |
		MRMAC_RX_RST_MASK | MRMAC_TX_RST_MASK);
	axienet_iow(lp, MRMAC_RESET_OFFSET, val);
}

static inline int axienet_mrmac_gt_reset(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u32 err, val;
	int i;

	if (mrmac_pll_rst == 0) {
		/* PLL reset for all lanes */

		for (i = 0; i < MRMAC_MAX_GT_LANES; i++) {
			iowrite32(MRMAC_GT_RST_ALL_MASK, (lp->gt_ctrl +
				  (MRMAC_GT_LANE_OFFSET * i) +
				  MRMAC_GT_CTRL_OFFSET));
			mdelay(MRMAC_RESET_DELAY);
			iowrite32(0, (lp->gt_ctrl + (MRMAC_GT_LANE_OFFSET * i) +
				      MRMAC_GT_CTRL_OFFSET));
		}

		/* Wait for PLL lock with timeout */
		err = readl_poll_timeout(lp->gt_pll + MRMAC_GT_PLL_STS_OFFSET,
					 val, (val & MRMAC_GT_PLL_DONE_MASK),
					 10, DELAY_OF_ONE_MILLISEC);
		if (err) {
			netdev_err(ndev, "MRMAC PLL lock not complete! Cross-check the MAC ref clock configuration\n");
			return -ENODEV;
		}
		mrmac_pll_rst = 1;
	}

	if (lp->mrmac_rate == SPEED_25000)
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
	mdelay(MRMAC_RESET_DELAY);
	iowrite32(0, (lp->gt_ctrl + MRMAC_GT_LANE_OFFSET * lp->gt_lane +
		  MRMAC_GT_CTRL_OFFSET));
	mdelay(MRMAC_RESET_DELAY);

	return 0;
}

void __axienet_device_reset(struct axienet_dma_q *q)
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
 * axienet_device_reset - Reset and initialize the Axi Ethernet hardware.
 * @ndev:	Pointer to the net_device structure
 *
 * Return: 0 on success, Negative value on errors
 *
 * This function is called to reset and initialize the Axi Ethernet core. This
 * is typically called during initialization. It does a reset of the Axi DMA
 * Rx/Tx channels and initializes the Axi DMA BDs. Since Axi DMA reset lines
 * areconnected to Axi Ethernet reset lines, this in turn resets the Axi
 * Ethernet core. No separate hardware reset is done for the Axi Ethernet
 * core.
 */
static int axienet_device_reset(struct net_device *ndev)
{
	u32 axienet_status;
	struct axienet_local *lp = netdev_priv(ndev);
	u32 err, val;
	struct axienet_dma_q *q;
	u32 i;
	int ret;

	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		/* Reset the XXV MAC */
		val = axienet_ior(lp, XXV_GT_RESET_OFFSET);
		val |= XXV_GT_RESET_MASK;
		axienet_iow(lp, XXV_GT_RESET_OFFSET, val);
		/* Wait for 1ms for GT reset to complete as per spec */
		mdelay(1);
		val = axienet_ior(lp, XXV_GT_RESET_OFFSET);
		val &= ~XXV_GT_RESET_MASK;
		axienet_iow(lp, XXV_GT_RESET_OFFSET, val);
	}

	if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
		/* Reset MRMAC */
		axienet_mrmac_reset(lp);
		ret = axienet_mrmac_gt_reset(ndev);
		if (ret < 0)
			return ret;
	}

	if (!lp->is_tsn) {
		for_each_rx_dma_queue(lp, i) {
			q = lp->dq[i];
			__axienet_device_reset(q);
#ifndef CONFIG_AXIENET_HAS_MCDMA
			__axienet_device_reset(q);
#endif
		}
	}

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;
	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC) {
		lp->options |= XAE_OPTION_VLAN;
		lp->options &= (~XAE_OPTION_JUMBO);
	}

	if ((ndev->mtu > XAE_MTU) && (ndev->mtu <= XAE_JUMBO_MTU)) {
		lp->max_frm_size = ndev->mtu + VLAN_ETH_HLEN +
					XAE_TRL_SIZE;
		if (lp->max_frm_size <= lp->rxmem &&
		    (lp->axienet_config->mactype != XAXIENET_10G_25G &&
		     lp->axienet_config->mactype != XAXIENET_MRMAC))
			lp->options |= XAE_OPTION_JUMBO;
	}

	if (!lp->is_tsn) {
		ret = axienet_dma_bd_init(ndev);
		if (ret < 0) {
			netdev_err(ndev, "%s: descriptor allocation failed\n",
				   __func__);
			return ret;
		}
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC) {
		axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
		axienet_status &= ~XAE_RCW1_RX_MASK;
		axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);
	}

	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		/* Check for block lock bit got set or not
		 * This ensures that 10G ethernet IP
		 * is functioning normally or not.
		 */
		err = readl_poll_timeout(lp->regs + XXV_STATRX_BLKLCK_OFFSET,
					 val, (val & XXV_RX_BLKLCK_MASK),
					 10, DELAY_OF_ONE_MILLISEC);
		if (err) {
			netdev_err(ndev, "XXV MAC block lock not complete! Cross-check the MAC ref clock configuration\n");
		}
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		if (!lp->is_tsn) {
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

	if ((lp->axienet_config->mactype == XAXIENET_1G) &&
	    !lp->eth_hasnobuf) {
		axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
		if (axienet_status & XAE_INT_RXRJECT_MASK)
			axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
		/* Enable receive erros */
		axienet_iow(lp, XAE_IE_OFFSET, lp->eth_irq > 0 ?
			    XAE_INT_RECV_ERROR_MASK : 0);
	}

	if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_MRMAC) {
		lp->options |= XAE_OPTION_FCS_STRIP;
		lp->options |= XAE_OPTION_FCS_INSERT;
	} else {
		axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);
	}
	lp->axienet_config->setoptions(ndev, lp->options &
				       ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	lp->axienet_config->setoptions(ndev, lp->options);

	netif_trans_update(ndev);

	return 0;
}

/**
 * axienet_adjust_link - Adjust the PHY link speed/duplex.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to change the speed and duplex setting after
 * auto negotiation is done by the PHY. This is the function that gets
 * registered with the PHY interface through the "of_phy_connect" call.
 */
void axienet_adjust_link(struct net_device *ndev)
{
	u32 emmc_reg;
	u32 link_state;
	u32 setspeed = 1;
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;

	link_state = phy->speed | (phy->duplex << 1) | phy->link;
	if (lp->last_link != link_state) {
		if ((phy->speed == SPEED_10) || (phy->speed == SPEED_100)) {
			if (lp->phy_mode == PHY_INTERFACE_MODE_1000BASEX)
				setspeed = 0;
		} else {
			if ((phy->speed == SPEED_1000) &&
			    (lp->phy_mode == PHY_INTERFACE_MODE_MII))
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
 * axienet_start_xmit_done - Invoked once a transmit is completed by the
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
void axienet_start_xmit_done(struct net_device *ndev,
			     struct axienet_dma_q *q)
{
	u32 size = 0;
	u32 packets = 0;
	struct axienet_local *lp = netdev_priv(ndev);

#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	unsigned int status = 0;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_ci];
	status = cur_p->sband_stats;
#else
	cur_p = &q->tx_bd_v[q->tx_bd_ci];
	status = cur_p->status;
#endif
	while (status & XAXIDMA_BD_STS_COMPLETE_MASK) {
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		if (cur_p->ptp_tx_skb)
			axienet_tx_hwtstamp(lp, cur_p);
#endif
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
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p->sband_stats = 0;
#endif

		size += status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		packets++;

		if (++q->tx_bd_ci >= lp->tx_bd_num)
			q->tx_bd_ci = 0;
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p = &q->txq_bd_v[q->tx_bd_ci];
		status = cur_p->sband_stats;
#else
		cur_p = &q->tx_bd_v[q->tx_bd_ci];
		status = cur_p->status;
#endif
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
	unsigned long flags;
	int i;

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

	if (lp->axienet_config->mactype == XAXIENET_1G ||
	    lp->axienet_config->mactype == XAXIENET_2_5G) {
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

	if ((((lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_SYNC) ||
	      (lp->tstamp_config.tx_type == HWTSTAMP_TX_ON)) ||
	       lp->eth_hasptp) && (lp->axienet_config->mactype !=
	       XAXIENET_10G_25G) &&
	       (lp->axienet_config->mactype != XAXIENET_MRMAC)) {
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
							TX_TS_OP_TWOSTEP
							, q);
				skb_shinfo(skb)->tx_flags
						|= SKBTX_IN_PROGRESS;
				cur_p->ptp_tx_skb =
					(unsigned long)skb_get(skb);
			}
		}
	} else if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		   (lp->axienet_config->mactype == XAXIENET_10G_25G ||
		   lp->axienet_config->mactype == XAXIENET_MRMAC)) {
			cur_p->ptp_tx_ts_tag = prandom_u32_max(XAXIFIFO_TXTS_TAG_MAX) + 1;
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
					cur_p->ptp_tx_skb = (phys_addr_t)skb_get(skb);
				}
			} else {
				if (axienet_create_tsheader(lp->tx_ptpheader,
							    TX_TS_OP_TWOSTEP,
							    q))
					return NETDEV_TX_BUSY;
				skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
				cur_p->ptp_tx_skb = (phys_addr_t)skb_get(skb);
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

int axienet_queue_xmit(struct sk_buff *skb,
		       struct net_device *ndev, u16 map)
{
	u32 ii;
	u32 num_frag;
	u32 csum_start_off;
	u32 csum_index_off;
	dma_addr_t tail_p;
	struct axienet_local *lp = netdev_priv(ndev);
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	unsigned long flags;
	struct axienet_dma_q *q;

	if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_MRMAC) {
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

		/* Matches barrier in axienet_start_xmit_done */
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
	    (lp->axienet_config->mactype == XAXIENET_1G)) {
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

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p->cntrl = (skb_headlen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK);
#else
	cur_p->cntrl = (skb_headlen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK);
#endif

	if (!q->eth_hasdre &&
	    (((phys_addr_t)skb->data & 0x3) || num_frag > 0)) {
		skb_copy_and_csum_dev(skb, q->tx_buf[q->tx_bd_tail]);

		cur_p->phys = q->tx_bufs_dma +
			      (q->tx_buf[q->tx_bd_tail] - q->tx_bufs);

#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p->cntrl = skb_pagelen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK;
#else
		cur_p->cntrl = skb_pagelen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK;
#endif
		goto out;
	} else {
		cur_p->phys = dma_map_single(ndev->dev.parent, skb->data,
					     skb_headlen(skb), DMA_TO_DEVICE);
	}
	cur_p->tx_desc_mapping = DESC_DMA_MAP_SINGLE;

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
		cur_p->phys = skb_frag_dma_map(ndev->dev.parent, frag, 0, len,
					       DMA_TO_DEVICE);
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
	cur_p->tx_skb = (phys_addr_t)skb;
	cur_p->tx_skb = (phys_addr_t)skb;

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
		if (!new_skb) {
			dev_err(lp->dev, "No memory for new_skb\n");
			break;
		}
#ifdef CONFIG_AXIENET_HAS_MCDMA
		tail_p = q->rx_bd_p + sizeof(*q->rxq_bd_v) * q->rx_bd_ci;
#else
		tail_p = q->rx_bd_p + sizeof(*q->rx_bd_v) * q->rx_bd_ci;
#endif

		dma_unmap_single(ndev->dev.parent, cur_p->phys,
				 lp->max_frm_size,
				 DMA_FROM_DEVICE);

		skb = (struct sk_buff *)(cur_p->sw_id_offset);

		if (lp->eth_hasnobuf ||
		    (lp->axienet_config->mactype != XAXIENET_1G))
			length = cur_p->status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		else
			length = cur_p->app4 & 0x0000FFFF;

		skb_put(skb, length);
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (!lp->is_tsn) {
		if ((lp->tstamp_config.rx_filter == HWTSTAMP_FILTER_ALL ||
			lp->eth_hasptp) &&
			(lp->axienet_config->mactype != XAXIENET_10G_25G) &&
			(lp->axienet_config->mactype != XAXIENET_MRMAC)) {
			u32 sec, nsec;
			u64 time64;
			struct skb_shared_hwtstamps *shhwtstamps;

			if (lp->axienet_config->mactype == XAXIENET_1G ||
			    lp->axienet_config->mactype == XAXIENET_2_5G) {
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
	}
#endif
		skb->protocol = eth_type_trans(skb, ndev);
		/*skb_checksum_none_assert(skb);*/
		skb->ip_summed = CHECKSUM_NONE;

		/* if we're doing Rx csum offload, set it up */
		if (lp->features & XAE_FEATURE_FULL_RX_CSUM &&
		    (lp->axienet_config->mactype == XAXIENET_1G) &&
		    !lp->eth_hasnobuf) {
			csumstatus = (cur_p->app2 &
				      XAE_FULL_CSUM_STATUS_MASK) >> 3;
			if ((csumstatus == XAE_IP_TCP_CSUM_VALIDATED) ||
			    (csumstatus == XAE_IP_UDP_CSUM_VALIDATED)) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			}
		} else if ((lp->features & XAE_FEATURE_PARTIAL_RX_CSUM) != 0 &&
			   skb->protocol == htons(ETH_P_IP) &&
			   skb->len > 64 && !lp->eth_hasnobuf &&
			   (lp->axienet_config->mactype == XAXIENET_1G)) {
			skb->csum = be32_to_cpu(cur_p->app3 & 0xFFFF);
			skb->ip_summed = CHECKSUM_COMPLETE;
		}
#ifdef CONFIG_XILINX_TSN
		if (unlikely(q->flags & MCDMA_MGMT_CHAN)) {
			struct net_device *ndev = NULL;

			/* received packet on mgmt channel */
			if (q->flags & MCDMA_MGMT_CHAN_PORT0)
				ndev = lp->slaves[0];
			else if (q->flags & MCDMA_MGMT_CHAN_PORT1)
				ndev = lp->slaves[1];

			/* send to one of the front panel port */
			if (ndev && netif_running(ndev)) {
				skb->dev = ndev;
				netif_receive_skb(skb);
			} else {
				kfree(skb); /* dont send up the stack */
			}
		} else {
			netif_receive_skb(skb); /* send on normal data path */
		}
#else

		netif_receive_skb(skb);
#endif

		size += length;
		packets++;

		/* Ensure that the skb is completely updated
		 * prio to mapping the DMA
		 */
		wmb();

		cur_p->phys = dma_map_single(ndev->dev.parent, new_skb->data,
					   lp->max_frm_size,
					   DMA_FROM_DEVICE);
		cur_p->cntrl = lp->max_frm_size;
		cur_p->status = 0;
		cur_p->sw_id_offset = (phys_addr_t)new_skb;

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

	ndev->stats.rx_packets += packets;
	ndev->stats.rx_bytes += size;
	q->rx_packets += packets;
	q->rx_bytes += size;

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
	int work_done = 0;
	unsigned int status, cr;

	int map = napi - lp->napi;

	struct axienet_dma_q *q = lp->dq[map];

#ifdef CONFIG_AXIENET_HAS_MCDMA
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
#else
	spin_lock(&q->rx_lock);

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
	spin_unlock(&q->rx_lock);
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
		ndev->stats.rx_frame_errors++;

	axienet_iow(lp, XAE_IS_OFFSET, pending);
	return IRQ_HANDLED;
}

/**
 * axienet_open - Driver open routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *	    non-zero error value on failure
 *
 * This is the driver open routine. It calls phy_start to start the PHY device.
 * It also allocates interrupt service routines, enables the interrupt lines
 * and ISR handling. Axi Ethernet core is reset through Axi DMA core. Buffer
 * descriptors are initialized.
 */
static int axienet_open(struct net_device *ndev)
{
	int ret = 0, i = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	u32 reg, err;
	struct phy_device *phydev = NULL;

	dev_dbg(&ndev->dev, "axienet_open()\n");

	ret  = axienet_device_reset(ndev);
	if (ret < 0) {
		dev_err(lp->dev, "axienet_device_reset failed\n");
		return ret;
	}

	if (lp->phy_node) {
		phydev = of_phy_connect(lp->ndev, lp->phy_node,
					axienet_adjust_link,
					lp->phy_flags,
					lp->phy_interface);

		if (!phydev)
			dev_err(lp->dev, "of_phy_connect() failed\n");
		else
			phy_start(phydev);
	}
	if (!lp->is_tsn) {
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

			/* Enable NAPI scheduling before enabling Axi DMA Rx
			 * IRQ, or you might run into a race condition; the RX
			 * ISR disables IRQ processing before scheduling the
			 * NAPI function to complete the processing. If NAPI
			 * scheduling is (still) disabled at that time, no more
			 * RX IRQs will be processed as only the NAPI function
			 * re-enables them!
			 */
			napi_enable(&lp->napi[i]);
		}
		for_each_tx_dma_queue(lp, i) {
			struct axienet_dma_q *q = lp->dq[i];
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
			struct axienet_dma_q *q = lp->dq[i];
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
		err = readl_poll_timeout(lp->regs + XXV_STATRX_BLKLCK_OFFSET,
					 reg, (reg & XXV_RX_BLKLCK_MASK),
					 100, DELAY_OF_ONE_MILLISEC);
		if (err) {
			netdev_err(ndev, "%s: USXGMII Block lock bit not set",
				   __func__);
			ret = -ENODEV;
			goto err_eth_irq;
		}

		err = readl_poll_timeout(lp->regs + XXV_USXGMII_AN_STS_OFFSET,
					 reg, (reg & USXGMII_AN_STS_COMP_MASK),
					 1000000, DELAY_OF_ONE_MILLISEC);
		if (err) {
			netdev_err(ndev, "%s: USXGMII AN not complete",
				   __func__);
			ret = -ENODEV;
			goto err_eth_irq;
		}

		netdev_info(ndev, "USXGMII setup at %d\n", lp->usxgmii_rate);
	}

	if (lp->axienet_config->mactype == XAXIENET_MRMAC) {
		u32 val;

		/* Reset MRMAC */
		axienet_mrmac_reset(lp);

		mdelay(MRMAC_RESET_DELAY);
		/* Check for block lock bit to be set. This ensures that
		 * MRMAC ethernet IP is functioning normally.
		 */
		axienet_iow(lp, MRMAC_TX_STS_OFFSET, MRMAC_STS_ALL_MASK);
		axienet_iow(lp, MRMAC_RX_STS_OFFSET, MRMAC_STS_ALL_MASK);
		err = readx_poll_timeout(axienet_get_mrmac_blocklock, lp, val,
					 (val & MRMAC_RX_BLKLCK_MASK), 10, DELAY_OF_ONE_MILLISEC);
		if (err) {
			netdev_err(ndev, "MRMAC block lock not complete! Cross-check the MAC ref clock configuration\n");
			ret = -ENODEV;
			goto err_eth_irq;
		}
		netdev_info(ndev, "MRMAC setup at %d\n", lp->mrmac_rate);
		axienet_iow(lp, MRMAC_TICK_OFFSET, MRMAC_TICK_TRIGGER);
	}

	/* Enable interrupts for Axi Ethernet core (if defined) */
	if (!lp->eth_hasnobuf && (lp->axienet_config->mactype == XAXIENET_1G)) {
		ret = request_irq(lp->eth_irq, axienet_eth_irq, IRQF_SHARED,
				  ndev->name, ndev);
		if (ret)
			goto err_eth_irq;
	}

	netif_tx_start_all_queues(ndev);
	return 0;

err_eth_irq:
	while (i--) {
		q = lp->dq[i];
		free_irq(q->rx_irq, ndev);
	}
	i = lp->num_tx_queues;
err_rx_irq:
	while (i--) {
		q = lp->dq[i];
		free_irq(q->tx_irq, ndev);
	}
err_tx_irq:
	for_each_rx_dma_queue(lp, i)
		napi_disable(&lp->napi[i]);
	if (phydev)
		phy_disconnect(phydev);
	for_each_rx_dma_queue(lp, i)
		tasklet_kill(&lp->dma_err_tasklet[i]);
	dev_err(lp->dev, "request_irq() failed\n");
	return ret;
}

/**
 * axienet_stop - Driver stop routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *
 * This is the driver stop routine. It calls phy_disconnect to stop the PHY
 * device. It also removes the interrupt handlers and disables the interrupts.
 * The Axi DMA Tx/Rx BDs are released.
 */
static int axienet_stop(struct net_device *ndev)
{
	u32 cr, sr;
	int count;
	u32 i;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;

	dev_dbg(&ndev->dev, "axienet_close()\n");

	lp->axienet_config->setoptions(ndev, lp->options &
			   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	if (!lp->is_tsn) {
		for_each_tx_dma_queue(lp, i) {
			q = lp->dq[i];
			cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
			cr &= ~(XAXIDMA_CR_RUNSTOP_MASK | XAXIDMA_IRQ_ALL_MASK);
			axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);

			cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
			cr &= ~(XAXIDMA_CR_RUNSTOP_MASK | XAXIDMA_IRQ_ALL_MASK);
			axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, cr);

			axienet_iow(lp, XAE_IE_OFFSET, 0);

			/* Give DMAs a chance to halt gracefully */
			sr = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
			for (count = 0; !(sr & XAXIDMA_SR_HALT_MASK) && count < 5; ++count) {
				msleep(20);
				sr = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
			}

			sr = axienet_dma_in32(q, XAXIDMA_TX_SR_OFFSET);
			for (count = 0; !(sr & XAXIDMA_SR_HALT_MASK) && count < 5; ++count) {
				msleep(20);
				sr = axienet_dma_in32(q, XAXIDMA_TX_SR_OFFSET);
			}

			__axienet_device_reset(q);
			free_irq(q->tx_irq, ndev);
		}

		for_each_rx_dma_queue(lp, i) {
			q = lp->dq[i];
			netif_stop_queue(ndev);
			napi_disable(&lp->napi[i]);
			tasklet_kill(&lp->dma_err_tasklet[i]);
			free_irq(q->rx_irq, ndev);
		}
#ifdef CONFIG_XILINX_TSN_PTP
		if (lp->is_tsn) {
			free_irq(lp->ptp_tx_irq, ndev);
			free_irq(lp->ptp_rx_irq, ndev);
		}
#endif
		if ((lp->axienet_config->mactype == XAXIENET_1G) && !lp->eth_hasnobuf)
			free_irq(lp->eth_irq, ndev);

		if (ndev->phydev)
			phy_disconnect(ndev->phydev);

		if (!lp->is_tsn)
			axienet_dma_bd_release(ndev);
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
#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_mcdma_rx_irq(lp->dq[i]->rx_irq, ndev);
#else
		axienet_rx_irq(lp->dq[i]->rx_irq, ndev);
#endif
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

#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
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
	if (lp->is_tsn) {
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
			config->rx_filter = HWTSTAMP_FILTER_ALL;
		}
		return 0;
	}
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
#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
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
#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
	case SIOCSHWTSTAMP:
		return axienet_set_ts_config(lp, rq);
	case SIOCGHWTSTAMP:
		return axienet_get_ts_config(lp, rq);
#endif
#ifdef CONFIG_XILINX_TSN_QBV
	case SIOCCHIOCTL:
		if (lp->qbv_regs)
			return axienet_set_schedule(dev, rq->ifr_data);
		return -EINVAL;
	case SIOC_GET_SCHED:
		if (lp->qbv_regs)
			return axienet_get_schedule(dev, rq->ifr_data);
		return -EINVAL;
#endif
#ifdef CONFIG_XILINX_TSN_QBR
	case SIOC_PREEMPTION_CFG:
		return axienet_preemption(dev, rq->ifr_data);
	case SIOC_PREEMPTION_CTRL:
		return axienet_preemption_ctrl(dev, rq->ifr_data);
	case SIOC_PREEMPTION_STS:
		return axienet_preemption_sts(dev, rq->ifr_data);
	case SIOC_PREEMPTION_COUNTER:
		return axienet_preemption_cnt(dev, rq->ifr_data);
#ifdef CONFIG_XILINX_TSN_QBV
	case SIOC_QBU_USER_OVERRIDE:
		return axienet_qbu_user_override(dev, rq->ifr_data);
	case SIOC_QBU_STS:
		return axienet_qbu_sts(dev, rq->ifr_data);
#endif
#endif

	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops axienet_netdev_ops = {
#ifdef CONFIG_XILINX_TSN
	.ndo_open = axienet_tsn_open,
#else
	.ndo_open = axienet_open,
#endif
	.ndo_stop = axienet_stop,
#ifdef CONFIG_XILINX_TSN
	.ndo_start_xmit = axienet_tsn_xmit,
#else
	.ndo_start_xmit = axienet_start_xmit,
#endif
	.ndo_change_mtu	= axienet_change_mtu,
	.ndo_set_mac_address = netdev_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_rx_mode = axienet_set_multicast_list,
	.ndo_do_ioctl = axienet_ioctl,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = axienet_poll_controller,
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
	strlcpy(ed->driver, DRIVER_NAME, sizeof(ed->driver));
	strlcpy(ed->version, DRIVER_VERSION, sizeof(ed->version));
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
 *
 * This implements ethtool command for getting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -c ethX" under linux prompt to
 * execute this function.
 *
 * Return: 0 always
 */
static int axienet_ethtools_get_coalesce(struct net_device *ndev,
					 struct ethtool_coalesce *ecoalesce)
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
	}
	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
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
 *
 * This implements ethtool command for setting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -C ethX rx-frames 5" under linux
 * prompt to execute this function.
 *
 * Return: 0, on success, Non-zero error value on failure.
 */
static int axienet_ethtools_set_coalesce(struct net_device *ndev,
					 struct ethtool_coalesce *ecoalesce)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netdev_err(ndev,
			   "Please stop netif before applying configuration\n");
		return -EFAULT;
	}

	if ((ecoalesce->rx_coalesce_usecs) ||
	    (ecoalesce->rx_coalesce_usecs_irq) ||
	    (ecoalesce->rx_max_coalesced_frames_irq) ||
	    (ecoalesce->tx_coalesce_usecs) ||
	    (ecoalesce->tx_coalesce_usecs_irq) ||
	    (ecoalesce->tx_max_coalesced_frames_irq) ||
	    (ecoalesce->stats_block_coalesce_usecs) ||
	    (ecoalesce->use_adaptive_rx_coalesce) ||
	    (ecoalesce->use_adaptive_tx_coalesce) ||
	    (ecoalesce->pkt_rate_low) ||
	    (ecoalesce->rx_coalesce_usecs_low) ||
	    (ecoalesce->rx_max_coalesced_frames_low) ||
	    (ecoalesce->tx_coalesce_usecs_low) ||
	    (ecoalesce->tx_max_coalesced_frames_low) ||
	    (ecoalesce->pkt_rate_high) ||
	    (ecoalesce->rx_coalesce_usecs_high) ||
	    (ecoalesce->rx_max_coalesced_frames_high) ||
	    (ecoalesce->tx_coalesce_usecs_high) ||
	    (ecoalesce->tx_max_coalesced_frames_high) ||
	    (ecoalesce->rate_sample_interval))
		return -EOPNOTSUPP;
	if (ecoalesce->rx_max_coalesced_frames)
		lp->coalesce_count_rx = ecoalesce->rx_max_coalesced_frames;
	if (ecoalesce->tx_max_coalesced_frames)
		lp->coalesce_count_tx = ecoalesce->tx_max_coalesced_frames;

	return 0;
}

#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
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
int axienet_ethtools_sset_count(struct net_device *ndev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
#ifdef CONFIG_AXIENET_HAS_MCDMA
		return axienet_sset_count(ndev, sset);
#else
		return AXIENET_ETHTOOLS_SSTATS_LEN;
#endif
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
void axienet_ethtools_get_stats(struct net_device *ndev,
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

#ifdef CONFIG_AXIENET_HAS_MCDMA
	axienet_get_stats(ndev, stats, data);
#endif
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
void axienet_ethtools_strings(struct net_device *ndev, u32 sset, u8 *data)
{
	int i;

	for (i = 0; i < AXIENET_ETHTOOLS_SSTATS_LEN; i++) {
		if (sset == ETH_SS_STATS)
			memcpy(data + i * ETH_GSTRING_LEN,
			       axienet_get_ethtools_strings_stats[i].name,
			       ETH_GSTRING_LEN);
	}
#ifdef CONFIG_AXIENET_HAS_MCDMA
	axienet_strings(ndev, sset, data);
#endif
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
#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
	.get_ts_info    = axienet_ethtools_get_ts_info,
#endif
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
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

	ret = of_property_read_u8(np, "xlnx,addrwidth", (u8 *)&lp->dma_mask);
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
			q->eth_hasdre = of_property_read_bool(np,
							      "xlnx,include-dre");
			ret = of_property_read_u8(np, "xlnx,addrwidth",
						  (u8 *)&lp->dma_mask);
			if (ret <  0 || lp->dma_mask < XAE_DMA_MASK_MIN ||
			    lp->dma_mask > XAE_DMA_MASK_MAX) {
				dev_info(&pdev->dev, "missing/invalid xlnx,addrwidth property, using default\n");
				lp->dma_mask = XAE_DMA_MASK_MIN;
			}

		} else {
			dev_err(&pdev->dev, "missing axistream-connected property\n");
			return -EINVAL;
		}
		lp->dq[i]->tx_irq = irq_of_parse_and_map(np, 0);
		lp->dq[i]->rx_irq = irq_of_parse_and_map(np, 1);

	}

	of_node_put(np);

	for_each_rx_dma_queue(lp, i) {
		struct axienet_dma_q *q = lp->dq[i];

		spin_lock_init(&q->tx_lock);
		spin_lock_init(&q->rx_lock);
	}

	for_each_rx_dma_queue(lp, i) {
		netif_napi_add(ndev, &lp->napi[i], xaxienet_rx_poll,
			       XAXIENET_NAPI_WEIGHT);
	}

	return 0;
}

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

static const struct axienet_config axienet_1g_config = {
	.mactype = XAXIENET_1G,
	.setoptions = axienet_setoptions,
	.clk_init = axienet_clk_init,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_2_5g_config = {
	.mactype = XAXIENET_2_5G,
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
};

/* Match table for of_platform binding */
static const struct of_device_id axienet_of_match[] = {
	{ .compatible = "xlnx,axi-ethernet-1.00.a", .data = &axienet_1g_config},
	{ .compatible = "xlnx,axi-ethernet-1.01.a", .data = &axienet_1g_config},
	{ .compatible = "xlnx,axi-ethernet-2.01.a", .data = &axienet_1g_config},
	{ .compatible = "xlnx,axi-2_5-gig-ethernet-1.0",
						.data = &axienet_2_5g_config},
	{ .compatible = "xlnx,ten-gig-eth-mac", .data = &axienet_10g_config},
	{ .compatible = "xlnx,xxv-ethernet-1.0",
						.data = &axienet_10g25g_config},
	{ .compatible = "xlnx,tsn-ethernet-1.00.a", .data = &axienet_1g_config},
	{ .compatible = "xlnx,xxv-usxgmii-ethernet-1.0",
					.data = &axienet_usxgmii_config},
	{ .compatible = "xlnx,mrmac-ethernet-1.0",
					.data = &axienet_mrmac_config},
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
	struct device_node *np;
	struct axienet_local *lp;
	struct net_device *ndev;
	const void *mac_addr;
	struct resource *ethres;
	u32 value;
	u16 num_queues = XAE_MAX_QUEUES;
	bool is_tsn = false;

	is_tsn = of_property_read_bool(pdev->dev.of_node, "xlnx,tsn");
	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-queues",
				   &num_queues);
	if (ret) {
		if (!is_tsn) {
#ifndef CONFIG_AXIENET_HAS_MCDMA
			num_queues = 1;
#endif
		}
	}
#ifdef CONFIG_XILINX_TSN
	if (is_tsn && (num_queues < XAE_TSN_MIN_QUEUES ||
		       num_queues > XAE_MAX_QUEUES))
		num_queues = XAE_MAX_QUEUES;
#endif

	ndev = alloc_etherdev_mq(sizeof(*lp), num_queues);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);
#ifdef CONFIG_XILINX_TSN
	bool slave = false;
	if (is_tsn) {
		slave = of_property_read_bool(pdev->dev.of_node,
					      "xlnx,tsn-slave");
		if (slave)
			snprintf(ndev->name, sizeof(ndev->name), "eth2");
		else
			snprintf(ndev->name, sizeof(ndev->name), "eth1");
	}
#endif

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
	lp->is_tsn = is_tsn;
	lp->rx_bd_num = RX_BD_NUM_DEFAULT;
	lp->tx_bd_num = TX_BD_NUM_DEFAULT;

#ifdef CONFIG_XILINX_TSN
	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-tc",
				   &lp->num_tc);
	if (ret || (lp->num_tc != 2 && lp->num_tc != 3))
		lp->num_tc = XAE_MAX_TSN_TC;
#endif

	/* Map device registers */
	ethres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp->regs = devm_ioremap_resource(&pdev->dev, ethres);
	if (IS_ERR(lp->regs)) {
		ret = PTR_ERR(lp->regs);
		goto free_netdev;
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

	if ((lp->axienet_config->mactype == XAXIENET_1G) && !lp->eth_hasnobuf)
		lp->eth_irq = platform_get_irq(pdev, 0);

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
				goto free_netdev;
			}

			ret = of_address_to_resource(np, 0, &gtpll);
			if (ret) {
				dev_err(&pdev->dev,
					"unable to get GT PLL resource\n");
				goto free_netdev;
			}

			lp->gt_pll = devm_ioremap_resource(&pdev->dev,
							   &gtpll);
			if (IS_ERR(lp->gt_pll)) {
				dev_err(&pdev->dev,
					"couldn't map GT PLL regs\n");
				ret = PTR_ERR(lp->gt_pll);
				goto free_netdev;
			}

			np = of_parse_phandle(pdev->dev.of_node,
					      "xlnx,gtctrl", 0);
			if (IS_ERR(np)) {
				dev_err(&pdev->dev,
					"couldn't find GT control\n");
				ret = PTR_ERR(np);
				goto free_netdev;
			}

			ret = of_address_to_resource(np, 0, &gtctrl);
			if (ret) {
				dev_err(&pdev->dev,
					"unable to get GT control resource\n");
				goto free_netdev;
			}

			lp->gt_ctrl = devm_ioremap_resource(&pdev->dev,
							    &gtctrl);
			if (IS_ERR(lp->gt_ctrl)) {
				dev_err(&pdev->dev,
					"couldn't map GT control regs\n");
				ret = PTR_ERR(lp->gt_ctrl);
				goto free_netdev;
			}

			mrmac_gt_pll = lp->gt_pll;
			mrmac_gt_ctrl = lp->gt_ctrl;
			mrmac_pll_reg = 1;
		}
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		ret = of_property_read_u32(pdev->dev.of_node, "xlnx,phcindex",
					   &lp->phc_index);
		if (ret)
			dev_warn(&pdev->dev, "No phc index defaulting to 0\n");
#endif
		ret = of_property_read_u32(pdev->dev.of_node, "xlnx,gtlane",
					   &lp->gt_lane);
		if (ret) {
			dev_err(&pdev->dev, "MRMAC GT lane information missing\n");
			goto free_netdev;
		}
		dev_info(&pdev->dev, "GT lane: %d\n", lp->gt_lane);
	}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (!lp->is_tsn) {
		struct resource txtsres, rxtsres;

		/* Find AXI Stream FIFO */
		np = of_parse_phandle(pdev->dev.of_node, "axififo-connected",
				      0);
		if (IS_ERR(np)) {
			dev_err(&pdev->dev, "could not find TX Timestamp FIFO\n");
			ret = PTR_ERR(np);
			goto free_netdev;
		}

		ret = of_address_to_resource(np, 0, &txtsres);
		if (ret) {
			dev_err(&pdev->dev,
				"unable to get Tx Timestamp resource\n");
			goto free_netdev;
		}

		lp->tx_ts_regs = devm_ioremap_resource(&pdev->dev, &txtsres);
		if (IS_ERR(lp->tx_ts_regs)) {
			dev_err(&pdev->dev, "could not map Tx Timestamp regs\n");
			ret = PTR_ERR(lp->tx_ts_regs);
			goto free_netdev;
		}

		if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
		    lp->axienet_config->mactype == XAXIENET_MRMAC) {
			np = of_parse_phandle(pdev->dev.of_node,
					      "xlnx,rxtsfifo", 0);
			if (IS_ERR(np)) {
				dev_err(&pdev->dev,
					"couldn't find rx-timestamp FIFO\n");
				ret = PTR_ERR(np);
				goto free_netdev;
			}

			ret = of_address_to_resource(np, 0, &rxtsres);
			if (ret) {
				dev_err(&pdev->dev,
					"unable to get rx-timestamp resource\n");
				goto free_netdev;
			}

			lp->rx_ts_regs = devm_ioremap_resource(&pdev->dev,
								&rxtsres);
			if (IS_ERR(lp->rx_ts_regs)) {
				dev_err(&pdev->dev,
					"couldn't map rx-timestamp regs\n");
				ret = PTR_ERR(lp->rx_ts_regs);
				goto free_netdev;
			}

			lp->tx_ptpheader = devm_kzalloc(&pdev->dev,
							lp->axienet_config->ts_header_len,
							GFP_KERNEL);
			spin_lock_init(&lp->ptp_tx_lock);
		}

		of_node_put(np);
	}
#endif

#ifdef CONFIG_XILINX_TSN
	if (lp->is_tsn)
		ret = axienet_tsn_probe(pdev, lp, ndev);
#endif
	if (!lp->is_tsn) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		ret = axienet_mcdma_probe(pdev, lp, ndev);
#else
		ret = axienet_dma_probe(pdev, ndev);
#endif
		if (ret) {
			pr_err("Getting DMA resource failed\n");
			goto free_netdev;
		}

		if (dma_set_mask_and_coherent(lp->dev, DMA_BIT_MASK(lp->dma_mask)) != 0) {
			dev_warn(&pdev->dev, "default to %d-bit dma mask\n", XAE_DMA_MASK_MIN);
			if (dma_set_mask_and_coherent(lp->dev, DMA_BIT_MASK(XAE_DMA_MASK_MIN)) != 0) {
				dev_err(&pdev->dev, "dma_set_mask_and_coherent failed, aborting\n");
				goto free_netdev;
			}
		}

		ret = axienet_dma_clk_init(pdev);
		if (ret) {
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "DMA clock init failed %d\n", ret);
			goto free_netdev;
		}
	}

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
	mac_addr = of_get_mac_address(pdev->dev.of_node);
	if (IS_ERR(mac_addr)) {
		dev_warn(&pdev->dev, "could not find MAC address property: %ld\n",
			 PTR_ERR(mac_addr));
		mac_addr = NULL;
	}
	axienet_set_mac_address(ndev, mac_addr);

	lp->coalesce_count_rx = XAXIDMA_DFT_RX_THRESHOLD;
	lp->coalesce_count_tx = XAXIDMA_DFT_TX_THRESHOLD;

	ret = of_get_phy_mode(pdev->dev.of_node, &lp->phy_mode);
	if (ret < 0)
		dev_warn(&pdev->dev, "couldn't find phy i/f\n");
	lp->phy_interface = ret;
	if (lp->phy_mode == PHY_INTERFACE_MODE_1000BASEX)
		lp->phy_flags = XAE_PHY_TYPE_1000BASE_X;

	lp->phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (lp->phy_node) {
		lp->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(lp->clk)) {
			dev_warn(&pdev->dev, "Failed to get clock: %ld\n",
				 PTR_ERR(lp->clk));
			lp->clk = NULL;
		} else {
			ret = clk_prepare_enable(lp->clk);
			if (ret) {
				dev_err(&pdev->dev, "Unable to enable clock: %d\n",
					ret);
				goto free_netdev;
			}
		}

		ret = axienet_mdio_setup(lp);
		if (ret)
			dev_warn(&pdev->dev,
				 "error registering MDIO bus: %d\n", ret);
	}

#ifdef CONFIG_AXIENET_HAS_MCDMA
	/* Create sysfs file entries for the device */
	ret = axeinet_mcdma_create_sysfs(&lp->dev->kobj);
	if (ret < 0) {
		dev_err(lp->dev, "unable to create sysfs entries\n");
		return ret;
	}
#endif

	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
		axienet_mdio_teardown(lp);
		goto err_disable_clk;
	}

	return 0;

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
	int i;

	if (!lp->is_tsn) {
		for_each_rx_dma_queue(lp, i)
			netif_napi_del(&lp->napi[i]);
	}
#ifdef CONFIG_XILINX_TSN_PTP
		axienet_ptp_timer_remove(lp->timer_priv);
#ifdef CONFIG_XILINX_TSN_QBV
		axienet_qbv_remove(ndev);
#endif
#endif
	unregister_netdev(ndev);
	axienet_clk_disable(pdev);

	if (lp->mii_bus)
		axienet_mdio_teardown(lp);

	if (lp->clk)
		clk_disable_unprepare(lp->clk);

#ifdef CONFIG_AXIENET_HAS_MCDMA
	axeinet_mcdma_remove_sysfs(&lp->dev->kobj);
#endif
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

static struct platform_driver axienet_driver = {
	.probe = axienet_probe,
	.remove = axienet_remove,
	.shutdown = axienet_shutdown,
	.driver = {
		 .name = "xilinx_axienet",
		 .of_match_table = axienet_of_match,
	},
};

module_platform_driver(axienet_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL");

/*
 * Xilinx Axi Ethernet device driver
 *
 * Copyright (c) 2008 Nissin Systems Co., Ltd.,  Yoshio Kashiwagi
 * Copyright (c) 2005-2008 DLA Systems,  David H. Lynch Jr. <dhlii@dlasys.net>
 * Copyright (c) 2008-2009 Secret Lab Technologies Ltd.
 * Copyright (c) 2010 - 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2010 - 2011 PetaLogix
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

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
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

#ifdef CONFIG_XILINX_TSN_PTP
#include "xilinx_tsn_ptp.h"
#include "xilinx_tsn_timer.h"
#endif
/* Descriptors defines for Tx and Rx DMA - 2^n for the best performance */
#define TX_BD_NUM		64
#define RX_BD_NUM		128

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME		"xaxienet"
#define DRIVER_DESCRIPTION	"Xilinx Axi Ethernet driver"
#define DRIVER_VERSION		"1.00a"

#define AXIENET_REGS_N		32
#define AXIENET_TS_HEADER_LEN	8
#define XXVENET_TS_HEADER_LEN	4
#define NS_PER_SEC              1000000000ULL /* Nanoseconds per second */

#define XAE_NUM_QUEUES(lp)	((lp)->num_queues)
#define for_each_dma_queue(lp, var) \
	for ((var) = 0; (var) < XAE_NUM_QUEUES(lp); (var)++)

#ifdef CONFIG_XILINX_TSN_PTP
int axienet_phc_index = -1;
EXPORT_SYMBOL(axienet_phc_index);
#endif

#ifdef CONFIG_AXIENET_HAS_MCDMA
struct axienet_stat {
	const char *name;
};

static struct axienet_stat axienet_get_strings_stats[] = {
	{ "txq0_packets" },
	{ "txq0_bytes"   },
	{ "rxq0_packets" },
	{ "rxq0_bytes"   },
	{ "txq1_packets" },
	{ "txq1_bytes"   },
	{ "rxq1_packets" },
	{ "rxq1_bytes"   },
	{ "txq2_packets" },
	{ "txq2_bytes"   },
	{ "rxq2_packets" },
	{ "rxq2_bytes"   },
	{ "txq3_packets" },
	{ "txq3_bytes"   },
	{ "rxq3_packets" },
	{ "rxq3_bytes"   },
	{ "txq4_packets" },
	{ "txq4_bytes"   },
	{ "rxq4_packets" },
	{ "rxq4_bytes"   },
	{ "txq5_packets" },
	{ "txq5_bytes"   },
	{ "rxq5_packets" },
	{ "rxq5_bytes"   },
	{ "txq6_packets" },
	{ "txq6_bytes"   },
	{ "rxq6_packets" },
	{ "rxq6_bytes"   },
	{ "txq7_packets" },
	{ "txq7_bytes"   },
	{ "rxq7_packets" },
	{ "rxq7_bytes"   },
	{ "txq8_packets" },
	{ "txq8_bytes"   },
	{ "rxq8_packets" },
	{ "rxq8_bytes"   },
	{ "txq9_packets" },
	{ "txq9_bytes"   },
	{ "rxq9_packets" },
	{ "rxq9_bytes"   },
	{ "txq10_packets" },
	{ "txq10_bytes"   },
	{ "rxq10_packets" },
	{ "rxq10_bytes"   },
	{ "txq11_packets" },
	{ "txq11_bytes"   },
	{ "rxq11_packets" },
	{ "rxq11_bytes"   },
	{ "txq12_packets" },
	{ "txq12_bytes"   },
	{ "rxq12_packets" },
	{ "rxq12_bytes"   },
	{ "txq13_packets" },
	{ "txq13_bytes"   },
	{ "rxq13_packets" },
	{ "rxq13_bytes"   },
	{ "txq14_packets" },
	{ "txq14_bytes"   },
	{ "rxq14_packets" },
	{ "rxq14_bytes"   },
	{ "txq15_packets" },
	{ "txq15_bytes"   },
	{ "rxq15_packets" },
	{ "rxq15_bytes"   },
};
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

/**
 * axienet_dma_in32 - Memory mapped Axi DMA register read
 * @q:		Pointer to DMA queue structure
 * @reg:	Address offset from the base address of the Axi DMA core
 *
 * Return: The contents of the Axi DMA register
 *
 * This function returns the contents of the corresponding Axi DMA register.
 */
static inline u32 axienet_dma_in32(struct axienet_dma_q *q, off_t reg)
{
	return in_be32(q->dma_regs + reg);
}

/**
 * axienet_dma_out32 - Memory mapped Axi DMA register write.
 * @q:		Pointer to DMA queue structure
 * @reg:	Address offset from the base address of the Axi DMA core
 * @value:	Value to be written into the Axi DMA register
 *
 * This function writes the desired value into the corresponding Axi DMA
 * register.
 */
static inline void axienet_dma_out32(struct axienet_dma_q *q,
				     off_t reg, u32 value)
{
	out_be32((q->dma_regs + reg), value);
}

/**
 * axienet_dma_bdout - Memory mapped Axi DMA register Buffer Descriptor write.
 * @q:		Pointer to DMA queue structure
 * @reg:	Address offset from the base address of the Axi DMA core
 * @value:	Value to be written into the Axi DMA register
 *
 * This function writes the desired value into the corresponding Axi DMA
 * register.
 */
static inline void axienet_dma_bdout(struct axienet_dma_q *q,
				     off_t reg, dma_addr_t value)
{
#if defined(CONFIG_PHYS_ADDR_T_64BIT)
	writeq(value, (q->dma_regs + reg));
#else
	writel(value, (q->dma_regs + reg));
#endif
}

/**
 * axienet_bd_free - Release buffer descriptor rings for individual dma queue
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to DMA queue structure
 *
 * This function is helper function to axienet_dma_bd_release.
 */

static void __maybe_unused axienet_bd_free(struct net_device *ndev,
					   struct axienet_dma_q *q)
{
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

	for (i = 0; i < RX_BD_NUM; i++) {
		dma_unmap_single(ndev->dev.parent, q->rx_bd_v[i].phys,
				 lp->max_frm_size, DMA_FROM_DEVICE);
		dev_kfree_skb((struct sk_buff *)
			      (q->rx_bd_v[i].sw_id_offset));
	}

	if (q->rx_bd_v) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*q->rx_bd_v) * RX_BD_NUM,
				  q->rx_bd_v,
				  q->rx_bd_p);
	}
	if (q->tx_bd_v) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*q->tx_bd_v) * TX_BD_NUM,
				  q->tx_bd_v,
				  q->tx_bd_p);
	}
}

static void __maybe_unused axienet_mcdma_bd_free(struct net_device *ndev,
						 struct axienet_dma_q *q)
{
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

	for (i = 0; i < RX_BD_NUM; i++) {
		dma_unmap_single(ndev->dev.parent, q->rxq_bd_v[i].phys,
				 lp->max_frm_size, DMA_FROM_DEVICE);
		dev_kfree_skb((struct sk_buff *)
			      (q->rxq_bd_v[i].sw_id_offset));
	}

	if (q->rxq_bd_v) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*q->rxq_bd_v) * RX_BD_NUM,
				  q->rxq_bd_v,
				  q->rx_bd_p);
	}

	if (q->txq_bd_v) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*q->txq_bd_v) * TX_BD_NUM,
				  q->txq_bd_v,
				  q->tx_bd_p);
	}
}

/**
 * axienet_dma_bd_release - Release buffer descriptor rings
 * @ndev:	Pointer to the net_device structure
 *
 * This function is used to release the descriptors allocated in
 * axienet_dma_bd_init. axienet_dma_bd_release is called when Axi Ethernet
 * driver stop api is called.
 */
static void axienet_dma_bd_release(struct net_device *ndev)
{
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

	for_each_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_mcdma_bd_free(ndev, lp->dq[i]);
#else
		axienet_bd_free(ndev, lp->dq[i]);
#endif
	}
}

/**
 * axienet_mcdma_q_init - Setup buffer qriptor rings for individual Axi DMA
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to MCDMA queue structure
 *
 * Return: 0, on success -ENOMEM, on failure
 *
 * This function is helper function to axienet_mcdma_bd_init
 */
static int __maybe_unused axienet_mcdma_q_init(struct net_device *ndev,
					       struct axienet_dma_q *q)
{
	u32 cr, chan_en;
	int i;
	struct sk_buff *skb;
	struct axienet_local *lp = netdev_priv(ndev);

	/* Reset the indexes which are used for accessing the BDs */
	q->tx_bd_ci = 0;
	q->tx_bd_tail = 0;
	q->rx_bd_ci = 0;
	q->rx_offset = XMCDMA_CHAN_RX_OFFSET;

	/* Allocate the Tx and Rx buffer qriptors. */
	q->txq_bd_v = dma_zalloc_coherent(ndev->dev.parent,
					  sizeof(*q->txq_bd_v) * TX_BD_NUM,
					  &q->tx_bd_p, GFP_KERNEL);
	if (!q->txq_bd_v)
		goto out;

	q->rxq_bd_v = dma_zalloc_coherent(ndev->dev.parent,
					  sizeof(*q->rxq_bd_v) * RX_BD_NUM,
					  &q->rx_bd_p, GFP_KERNEL);
	if (!q->rxq_bd_v)
		goto out;

	if (!q->eth_hasdre) {
		q->tx_bufs = dma_zalloc_coherent(ndev->dev.parent,
						  XAE_MAX_PKT_LEN * TX_BD_NUM,
						  &q->tx_bufs_dma,
						  GFP_KERNEL);
		if (!q->tx_bufs)
			goto out;

		for (i = 0; i < TX_BD_NUM; i++)
			q->tx_buf[i] = &q->tx_bufs[i * XAE_MAX_PKT_LEN];
	}

	for (i = 0; i < TX_BD_NUM; i++) {
		q->txq_bd_v[i].next = q->tx_bd_p +
				      sizeof(*q->txq_bd_v) *
				      ((i + 1) % TX_BD_NUM);
	}

	for (i = 0; i < RX_BD_NUM; i++) {
		q->rxq_bd_v[i].next = q->rx_bd_p +
				      sizeof(*q->rxq_bd_v) *
				      ((i + 1) % RX_BD_NUM);

		skb = netdev_alloc_skb(ndev, lp->max_frm_size);
		if (!skb)
			goto out;

		/* Ensure that the skb is completely updated
		 * prio to mapping the DMA
		 */
		wmb();

		q->rxq_bd_v[i].sw_id_offset = (phys_addr_t)skb;
		q->rxq_bd_v[i].phys = dma_map_single(ndev->dev.parent,
						     skb->data,
						     lp->max_frm_size,
						     DMA_FROM_DEVICE);
		q->rxq_bd_v[i].cntrl = lp->max_frm_size;
	}

	/* Start updating the Rx channel control register */
	cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
			      q->rx_offset);
	/* Update the interrupt coalesce count */
	cr = ((cr & ~XMCDMA_COALESCE_MASK) |
	      ((lp->coalesce_count_rx) << XMCDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = ((cr & ~XMCDMA_DELAY_MASK) |
	      (XAXIDMA_DFT_RX_WAITBOUND << XMCDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XMCDMA_IRQ_ALL_MASK;
	/* Write to the Rx channel control register */
	axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
			  q->rx_offset, cr);

	/* Start updating the Tx channel control register */
	cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id));
	/* Update the interrupt coalesce count */
	cr = (((cr & ~XMCDMA_COALESCE_MASK)) |
	      ((lp->coalesce_count_tx) << XMCDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = (((cr & ~XMCDMA_DELAY_MASK)) |
	      (XAXIDMA_DFT_TX_WAITBOUND << XMCDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XMCDMA_IRQ_ALL_MASK;
	/* Write to the Tx channel control register */
	axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id), cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	axienet_dma_bdout(q, XMCDMA_CHAN_CURDESC_OFFSET(q->chan_id) +
			    q->rx_offset, q->rx_bd_p);
	cr = axienet_dma_in32(q, XMCDMA_CR_OFFSET +  q->rx_offset);
	axienet_dma_out32(q, XMCDMA_CR_OFFSET +  q->rx_offset,
			  cr | XMCDMA_CR_RUNSTOP_MASK);
	cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				q->rx_offset);
	axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) + q->rx_offset,
			  cr | XMCDMA_CR_RUNSTOP_MASK);
	axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id) +
			    q->rx_offset, q->rx_bd_p + (sizeof(*q->rxq_bd_v) *
			    (RX_BD_NUM - 1)));
	chan_en = axienet_dma_in32(q, XMCDMA_CHEN_OFFSET + q->rx_offset);
	chan_en |= (1 << (q->chan_id - 1));
	axienet_dma_out32(q, XMCDMA_CHEN_OFFSET + q->rx_offset, chan_en);

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting.
	 */
	axienet_dma_bdout(q, XMCDMA_CHAN_CURDESC_OFFSET(q->chan_id),
			  q->tx_bd_p);
	cr = axienet_dma_in32(q, XMCDMA_CR_OFFSET);
	axienet_dma_out32(q, XMCDMA_CR_OFFSET,
			  cr | XMCDMA_CR_RUNSTOP_MASK);
	cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id));
	axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id),
			  cr | XMCDMA_CR_RUNSTOP_MASK);
	chan_en = axienet_dma_in32(q, XMCDMA_CHEN_OFFSET);
	chan_en |= (1 << (q->chan_id - 1));
	axienet_dma_out32(q, XMCDMA_CHEN_OFFSET, chan_en);

	return 0;
out:
	axienet_dma_bd_release(ndev);
	return -ENOMEM;
}

/**
 * axienet_dma_q_init - Setup buffer descriptor rings for individual Axi DMA
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to DMA queue structure
 *
 * Return: 0, on success -ENOMEM, on failure
 *
 * This function is helper function to axienet_dma_bd_init
 */
static int __maybe_unused axienet_dma_q_init(struct net_device *ndev,
					     struct axienet_dma_q *q)
{
	u32 cr;
	int i;
	struct sk_buff *skb;
	struct axienet_local *lp = netdev_priv(ndev);

	/* Reset the indexes which are used for accessing the BDs */
	q->tx_bd_ci = 0;
	q->tx_bd_tail = 0;
	q->rx_bd_ci = 0;

	/* Allocate the Tx and Rx buffer descriptors. */
	q->tx_bd_v = dma_zalloc_coherent(ndev->dev.parent,
					  sizeof(*q->tx_bd_v) * TX_BD_NUM,
					  &q->tx_bd_p, GFP_KERNEL);
	if (!q->tx_bd_v)
		goto out;

	q->rx_bd_v = dma_zalloc_coherent(ndev->dev.parent,
					  sizeof(*q->rx_bd_v) * RX_BD_NUM,
					  &q->rx_bd_p, GFP_KERNEL);
	if (!q->rx_bd_v)
		goto out;

	for (i = 0; i < TX_BD_NUM; i++) {
		q->tx_bd_v[i].next = q->tx_bd_p +
				      sizeof(*q->tx_bd_v) *
				      ((i + 1) % TX_BD_NUM);
	}

	if (!q->eth_hasdre) {
		q->tx_bufs = dma_zalloc_coherent(ndev->dev.parent,
						  XAE_MAX_PKT_LEN * TX_BD_NUM,
						  &q->tx_bufs_dma,
						  GFP_KERNEL);
		if (!q->tx_bufs)
			goto out;

		for (i = 0; i < TX_BD_NUM; i++)
			q->tx_buf[i] = &q->tx_bufs[i * XAE_MAX_PKT_LEN];
	}

	for (i = 0; i < RX_BD_NUM; i++) {
		q->rx_bd_v[i].next = q->rx_bd_p +
				      sizeof(*q->rx_bd_v) *
				      ((i + 1) % RX_BD_NUM);

		skb = netdev_alloc_skb(ndev, lp->max_frm_size);
		if (!skb)
			goto out;

		/* Ensure that the skb is completely updated
		 * prio to mapping the DMA
		 */
		wmb();

		q->rx_bd_v[i].sw_id_offset = (phys_addr_t)skb;
		q->rx_bd_v[i].phys = dma_map_single(ndev->dev.parent,
						     skb->data,
						     lp->max_frm_size,
						     DMA_FROM_DEVICE);
		q->rx_bd_v[i].cntrl = lp->max_frm_size;
	}

	/* Start updating the Rx channel control register */
	cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = ((cr & ~XAXIDMA_COALESCE_MASK) |
	      ((lp->coalesce_count_rx) << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = ((cr & ~XAXIDMA_DELAY_MASK) |
	      (XAXIDMA_DFT_RX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Write to the Rx channel control register */
	axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);

	/* Start updating the Tx channel control register */
	cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = (((cr & ~XAXIDMA_COALESCE_MASK)) |
	      ((lp->coalesce_count_tx) << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = (((cr & ~XAXIDMA_DELAY_MASK)) |
	      (XAXIDMA_DFT_TX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Write to the Tx channel control register */
	axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	axienet_dma_bdout(q, XAXIDMA_RX_CDESC_OFFSET, q->rx_bd_p);
	cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
	axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET,
			  cr | XAXIDMA_CR_RUNSTOP_MASK);
	axienet_dma_bdout(q, XAXIDMA_RX_TDESC_OFFSET, q->rx_bd_p +
			  (sizeof(*q->rx_bd_v) * (RX_BD_NUM - 1)));

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting.
	 */
	axienet_dma_bdout(q, XAXIDMA_TX_CDESC_OFFSET, q->tx_bd_p);
	cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
	axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET,
			  cr | XAXIDMA_CR_RUNSTOP_MASK);

	return 0;
out:
	axienet_dma_bd_release(ndev);
	return -ENOMEM;
}

/**
 * axienet_dma_bd_init - Setup buffer descriptor rings for Axi DMA
 * @ndev:	Pointer to the net_device structure
 *
 * Return: 0, on success -ENOMEM, on failure
 *
 * This function is called to initialize the Rx and Tx DMA descriptor
 * rings. This initializes the descriptors with required default values
 * and is called when Axi Ethernet driver reset is called.
 */
static int axienet_dma_bd_init(struct net_device *ndev)
{
	int i, ret;
	struct axienet_local *lp = netdev_priv(ndev);

	for_each_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		ret = axienet_mcdma_q_init(ndev, lp->dq[i]);
#else
		ret = axienet_dma_q_init(ndev, lp->dq[i]);
#endif
		if (ret != 0)
			break;
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
static void axienet_set_mac_address(struct net_device *ndev, void *address)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (address)
		ether_addr_copy(ndev->dev_addr, address);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_random_addr(ndev->dev_addr);

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
static void axienet_set_multicast_list(struct net_device *ndev)
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
		reg = axienet_ior(lp, XAE_FMI_OFFSET);
		reg |= XAE_FMI_PM_MASK;
		axienet_iow(lp, XAE_FMI_OFFSET, reg);
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

			reg = axienet_ior(lp, XAE_FMI_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMI_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, af0reg);
			axienet_iow(lp, XAE_AF1_OFFSET, af1reg);
			i++;
		}
	} else {
		reg = axienet_ior(lp, XAE_FMI_OFFSET);
		reg &= ~XAE_FMI_PM_MASK;

		axienet_iow(lp, XAE_FMI_OFFSET, reg);

		for (i = 0; i < XAE_MULTICAST_CAM_TABLE_NUM; i++) {
			reg = axienet_ior(lp, XAE_FMI_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMI_OFFSET, reg);
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

static void xxvenet_setoptions(struct net_device *ndev, u32 options)
{
	int reg;
	struct axienet_local *lp = netdev_priv(ndev);
	struct xxvenet_option *tp = &xxvenet_options[0];

	while (tp->opt) {
		reg = ((axienet_ior(lp, tp->reg)) & ~(tp->m_or));
		if (options & tp->opt)
			reg |= tp->m_or;
		axienet_iow(lp, tp->reg, reg);
		tp++;
	}

	lp->options |= options;
}

static void __axienet_device_reset(struct axienet_dma_q *q, off_t offset)
{
	u32 timeout;
	/* Reset Axi DMA. This would reset Axi Ethernet core as well. The reset
	 * process of Axi DMA takes a while to complete as all pending
	 * commands/transfers will be flushed or completed during this
	 * reset process.
	 */
	axienet_dma_out32(q, offset, XAXIDMA_CR_RESET_MASK);
	timeout = DELAY_OF_ONE_MILLISEC;
	while (axienet_dma_in32(q, offset) & XAXIDMA_CR_RESET_MASK) {
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
 * This function is called to reset and initialize the Axi Ethernet core. This
 * is typically called during initialization. It does a reset of the Axi DMA
 * Rx/Tx channels and initializes the Axi DMA BDs. Since Axi DMA reset lines
 * areconnected to Axi Ethernet reset lines, this in turn resets the Axi
 * Ethernet core. No separate hardware reset is done for the Axi Ethernet
 * core.
 */
static void axienet_device_reset(struct net_device *ndev)
{
	u32 axienet_status;
	struct axienet_local *lp = netdev_priv(ndev);
	u32 err, val;
	struct axienet_dma_q *q;
	u32 i;

	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		for_each_dma_queue(lp, i) {
			q = lp->dq[i];
			__axienet_device_reset(q, XAXIDMA_TX_CR_OFFSET);
#ifndef CONFIG_AXIENET_HAS_MCDMA
			__axienet_device_reset(q, XAXIDMA_RX_CR_OFFSET);
#endif
		}
	}

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;
	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
		lp->options |= XAE_OPTION_VLAN;
		lp->options &= (~XAE_OPTION_JUMBO);
	}

	if ((ndev->mtu > XAE_MTU) && (ndev->mtu <= XAE_JUMBO_MTU)) {
		lp->max_frm_size = ndev->mtu + VLAN_ETH_HLEN +
					XAE_TRL_SIZE;
		if (lp->max_frm_size <= lp->rxmem &&
		    (lp->axienet_config->mactype != XAXIENET_10G_25G))
			lp->options |= XAE_OPTION_JUMBO;
	}

	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		if (axienet_dma_bd_init(ndev)) {
		netdev_err(ndev, "%s: descriptor allocation failed\n",
			   __func__);
		}
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
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
			netdev_err(ndev, "%s: Block lock bit of XXV MAC didn't",
				   __func__);
			netdev_err(ndev, "Got Set cross check the ref clock");
			netdev_err(ndev, "Configuration for the mac");
		}
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		if (!lp->is_tsn) {
			axienet_rxts_iow(lp, XAXIFIFO_TXTS_RDFR,
					 XAXIFIFO_TXTS_RESET_MASK);
			axienet_rxts_iow(lp, XAXIFIFO_TXTS_SRR,
					 XAXIFIFO_TXTS_RESET_MASK);
		}
#endif
	}

	if ((lp->axienet_config->mactype == XAXIENET_1G) &&
	    !lp->eth_hasnobuf) {
		axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
		if (axienet_status & XAE_INT_RXRJECT_MASK)
			axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);

		/* Enable Receive errors */
		axienet_iow(lp, XAE_IE_OFFSET, XAE_INT_RECV_ERROR_MASK);
	}

	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
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
}

/**
 * axienet_adjust_link - Adjust the PHY link speed/duplex.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to change the speed and duplex setting after
 * auto negotiation is done by the PHY. This is the function that gets
 * registered with the PHY interface through the "of_phy_connect" call.
 */
static void axienet_adjust_link(struct net_device *ndev)
{
	u32 emmc_reg;
	u32 link_state;
	u32 setspeed = 1;
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;

	link_state = phy->speed | (phy->duplex << 1) | phy->link;
	if (lp->last_link != link_state) {
		if ((phy->speed == SPEED_10) || (phy->speed == SPEED_100)) {
			if (lp->phy_type == XAE_PHY_TYPE_1000BASE_X)
				setspeed = 0;
		} else {
			if ((phy->speed == SPEED_1000) &&
			    (lp->phy_type == XAE_PHY_TYPE_MII))
				setspeed = 0;
		}

		if (setspeed == 1) {
			emmc_reg = axienet_ior(lp, XAE_EMMC_OFFSET);
			emmc_reg &= ~XAE_EMMC_LINKSPEED_MASK;

			switch (phy->speed) {
			case SPEED_2500:
				emmc_reg |= XAE_EMMC_LINKSPD_2500;
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
static void axienet_tx_hwtstamp(struct axienet_local *lp,
				struct aximcdma_bd *cur_p)
#else
static void axienet_tx_hwtstamp(struct axienet_local *lp,
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
		dev_info(lp->dev, "Did't get FIFO rx interrupt %d\n", val);

	/* If FIFO is configured in cut through Mode we will get Rx complete
	 * interrupt even one byte is there in the fifo wait for the full packet
	 */
	err = readl_poll_timeout_atomic(lp->tx_ts_regs + XAXIFIFO_TXTS_RLR, val,
					((val & XAXIFIFO_TXTS_RXFD_MASK) >=
					len), 0, 1000000);
	if (err)
		netdev_err(lp->ndev, "%s: Didn't get the full timestamp packet",
			   __func__);

	nsec = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	sec  = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	val = ((val & XAXIFIFO_TXTS_TAG_MASK) >> XAXIFIFO_TXTS_TAG_SHIFT);
	if (val != cur_p->ptp_tx_ts_tag) {
		count = axienet_txts_ior(lp, XAXIFIFO_TXTS_RFO);
		while (count) {
			nsec = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			sec  = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			val = ((val & XAXIFIFO_TXTS_TAG_MASK) >>
				XAXIFIFO_TXTS_TAG_SHIFT);
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

	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);

	time64 = sec * NS_PER_SEC + nsec;
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = ns_to_ktime(time64);
	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		skb_pull((struct sk_buff *)cur_p->ptp_tx_skb,
			 AXIENET_TS_HEADER_LEN);

	skb_tstamp_tx((struct sk_buff *)cur_p->ptp_tx_skb, shhwtstamps);
	dev_kfree_skb_any((struct sk_buff *)cur_p->ptp_tx_skb);
	cur_p->ptp_tx_skb = 0;
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
static void axienet_start_xmit_done(struct net_device *ndev,
				    struct axienet_dma_q *q)
{
	u32 size = 0;
	u32 packets = 0;
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	struct axienet_local *lp = netdev_priv(ndev);
#endif
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

		++q->tx_bd_ci;
		q->tx_bd_ci %= TX_BD_NUM;
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
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;

	cur_p = &q->txq_bd_v[(q->tx_bd_tail + num_frag) % TX_BD_NUM];
	if (cur_p->sband_stats & XMCDMA_BD_STS_ALL_MASK)
		return NETDEV_TX_BUSY;
#else
	struct axidma_bd *cur_p;

	cur_p = &q->tx_bd_v[(q->tx_bd_tail + num_frag) % TX_BD_NUM];
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
 * Return:	None.
 */
static void axienet_create_tsheader(u8 *buf, u8 msg_type,
				    struct axienet_dma_q *q)
{
	struct axienet_local *lp = q->lp;
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	u64 val;
	u32 tmp;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
	cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif

	if (msg_type == TX_TS_OP_ONESTEP) {
		buf[0] = TX_TS_OP_ONESTEP;
		buf[1] = TX_TS_CSUM_UPDATE;
		buf[4] = TX_PTP_TS_OFFSET;
		buf[6] = TX_PTP_CSUM_OFFSET;
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
	} else if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		memcpy(&tmp, buf, XXVENET_TS_HEADER_LEN);
		axienet_txts_iow(lp, XAXIFIFO_TXTS_TXFD, tmp);
		axienet_txts_iow(lp, XAXIFIFO_TXTS_TLR, XXVENET_TS_HEADER_LEN);
	}
}
#endif

#ifdef CONFIG_XILINX_TSN
static inline u16 tsn_queue_mapping(const struct sk_buff *skb)
{
	int queue = XAE_BE;
	u16 vlan_tci;
	u8 pcp;

	struct ethhdr *hdr = (struct ethhdr *)skb->data;
	u16 ether_type = ntohs(hdr->h_proto);

	if (unlikely(ether_type == ETH_P_8021Q)) {
		struct vlan_ethhdr *vhdr = (struct vlan_ethhdr *)skb->data;

		/* ether_type = ntohs(vhdr->h_vlan_encapsulated_proto); */

		vlan_tci = ntohs(vhdr->h_vlan_TCI);

		pcp = (vlan_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
		pr_debug("vlan_tci: %x\n", vlan_tci);
		pr_debug("pcp: %d\n", pcp);

		if (pcp == 4)
			queue = XAE_ST;
		else if (pcp == 2 || pcp == 3)
			queue = XAE_RE;
	}
	pr_debug("selected queue: %d\n", queue);
	return queue;
}
#endif

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
	u32 pad = 0;
	struct axienet_dma_q *q;
	u16 map = skb_get_queue_mapping(skb); /* Single dma queue default*/

#ifdef CONFIG_XILINX_TSN
	if (lp->is_tsn) {
		map = tsn_queue_mapping(skb);
#ifdef CONFIG_XILINX_TSN_PTP
		const struct ethhdr *eth;

		eth = (struct ethhdr *)skb->data;
		/* check if skb is a PTP frame ? */
		if (eth->h_proto == htons(ETH_P_1588))
			return axienet_ptp_xmit(skb, ndev);
#endif
		if (lp->temac_no == XAE_TEMAC2) {
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	}
#endif
	num_frag = skb_shinfo(skb)->nr_frags;

	q = lp->dq[map];

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
	cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif

	spin_lock_irqsave(&q->tx_lock, flags);
	if (axienet_check_tx_bd_space(q, num_frag)) {
		if (!__netif_subqueue_stopped(ndev, map))
			netif_stop_subqueue(ndev, map);
		spin_unlock_irqrestore(&q->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (!lp->is_tsn) {
		if ((((lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_SYNC) ||
		      (lp->tstamp_config.tx_type == HWTSTAMP_TX_ON)) ||
		       lp->eth_hasptp) && (lp->axienet_config->mactype !=
		       XAXIENET_10G_25G)) {
			u8 *tmp;
			struct sk_buff *new_skb;

			if (skb_headroom(skb) < AXIENET_TS_HEADER_LEN) {
				new_skb = skb_realloc_headroom(skb,
							       AXIENET_TS_HEADER_LEN);
				if (!new_skb) {
					dev_err(&ndev->dev, "failed to allocate new socket buffer\n");
					dev_kfree_skb_any(skb);
					spin_unlock_irqrestore(&q->tx_lock,
							       flags);
					return NETDEV_TX_OK;
				}

				/*  Transfer the ownership to the
				 *  new socket buffer if required
				 */
				if (skb->sk)
					skb_set_owner_w(new_skb, skb->sk);
				dev_kfree_skb(skb);
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
			   (lp->axienet_config->mactype == XAXIENET_10G_25G)) {
			cur_p->ptp_tx_ts_tag = (prandom_u32() &
						~XAXIFIFO_TXTS_TAG_MASK) + 1;
			if (lp->tstamp_config.tx_type ==
						HWTSTAMP_TX_ONESTEP_SYNC) {
				axienet_create_tsheader(lp->tx_ptpheader,
							TX_TS_OP_ONESTEP, q);
			} else {
				axienet_create_tsheader(lp->tx_ptpheader,
							TX_TS_OP_TWOSTEP, q);
				skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
				cur_p->ptp_tx_skb = (phys_addr_t)skb_get(skb);
			}
		}
	}
#endif
	/* Work around for XXV MAC as MAC will drop the packets
	 * of size less than 64 bytes we need to append data
	 * to make packet length greater than or equal to 64
	 */
	if (skb->len < XXV_MAC_MIN_PKT_LEN &&
	    (lp->axienet_config->mactype == XAXIENET_10G_25G))
		pad = XXV_MAC_MIN_PKT_LEN - skb->len;

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
	cur_p->cntrl = (skb_headlen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK) + pad;
#else
	cur_p->cntrl = (skb_headlen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK) + pad;
#endif
	if (!q->eth_hasdre &&
	    (((phys_addr_t)skb->data & 0x3) || (num_frag > 0))) {
		skb_copy_and_csum_dev(skb, q->tx_buf[q->tx_bd_tail]);

		cur_p->phys = q->tx_bufs_dma +
			      (q->tx_buf[q->tx_bd_tail] - q->tx_bufs);

		if (num_frag > 0) {
			pad = skb_pagelen(skb) - skb_headlen(skb);
#ifdef CONFIG_AXIENET_HAS_MCDMA
			cur_p->cntrl = (skb_headlen(skb) |
					XMCDMA_BD_CTRL_TXSOF_MASK) + pad;
#else
			cur_p->cntrl = (skb_headlen(skb) |
					XAXIDMA_BD_CTRL_TXSOF_MASK) + pad;
#endif
		}
		goto out;
	} else {
		cur_p->phys = dma_map_single(ndev->dev.parent, skb->data,
					     skb_headlen(skb), DMA_TO_DEVICE);
	}
	cur_p->tx_desc_mapping = DESC_DMA_MAP_SINGLE;

	for (ii = 0; ii < num_frag; ii++) {
		u32 len;
		skb_frag_t *frag;

		++q->tx_bd_tail;
		q->tx_bd_tail %= TX_BD_NUM;
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
		cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif
		frag = &skb_shinfo(skb)->frags[ii];
		len = skb_frag_size(frag);
		cur_p->phys = skb_frag_dma_map(ndev->dev.parent, frag, 0, len,
					       DMA_TO_DEVICE);
		cur_p->cntrl = len + pad;
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

	/* Ensure BD write before starting transfer */
	wmb();

	/* Start the transfer */
#ifdef CONFIG_AXIENET_HAS_MCDMA
	axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id),
			  tail_p);
#else
	axienet_dma_bdout(q, XAXIDMA_TX_TDESC_OFFSET, tail_p);
#endif
	++q->tx_bd_tail;
	q->tx_bd_tail %= TX_BD_NUM;

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
#ifdef CONFIG_AXIENET_HAS_MCDMA
		tail_p = q->rx_bd_p + sizeof(*q->rxq_bd_v) * q->rx_bd_ci;
#else
		tail_p = q->rx_bd_p + sizeof(*q->rx_bd_v) * q->rx_bd_ci;
#endif
		skb = (struct sk_buff *)(cur_p->sw_id_offset);

		if (lp->eth_hasnobuf ||
		    (lp->axienet_config->mactype != XAXIENET_1G))
			length = cur_p->status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		else
			length = cur_p->app4 & 0x0000FFFF;

		dma_unmap_single(ndev->dev.parent, cur_p->phys,
				 lp->max_frm_size,
				 DMA_FROM_DEVICE);

		skb_put(skb, length);
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (!lp->is_tsn) {
		if ((lp->tstamp_config.rx_filter == HWTSTAMP_FILTER_ALL ||
		     lp->eth_hasptp) && (lp->axienet_config->mactype !=
		     XAXIENET_10G_25G)) {
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
		} else if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
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

		netif_receive_skb(skb);

		size += length;
		packets++;

		new_skb = netdev_alloc_skb(ndev, lp->max_frm_size);
		if (!new_skb) {
			dev_err(lp->dev, "No memory for new_skb\n\r");
			break;
		}

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

		++q->rx_bd_ci;
		q->rx_bd_ci %= RX_BD_NUM;

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
static int xaxienet_rx_poll(struct napi_struct *napi, int quota)
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
 * axienet_err_irq - Axi Ethernet error irq.
 * @irq:	irq number
 * @_ndev:	net_device pointer
 *
 * Return: IRQ_HANDLED for all cases.
 *
 * This is the Axi DMA error ISR. It updates the rx memory over run condition.
 */
static irqreturn_t axienet_err_irq(int irq, void *_ndev)
{
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);

	status = axienet_ior(lp, XAE_IS_OFFSET);
	if (status & XAE_INT_RXFIFOOVR_MASK) {
		ndev->stats.rx_fifo_errors++;
		axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXFIFOOVR_MASK);
	}

	if (status & XAE_INT_RXRJECT_MASK) {
		ndev->stats.rx_dropped++;
		axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
	}

	return IRQ_HANDLED;
}

static inline int get_mcdma_q(struct axienet_local *lp, u32 chan_id)
{
	int i;

	for_each_dma_queue(lp, i) {
		if (chan_id == lp->chan_num[i])
			return lp->qnum[i];
	}

	return -ENODEV;
}

static inline int map_dma_q_txirq(int irq, struct axienet_local *lp)
{
	int i, chan_sermask;
	u16 chan_id = 1;
	struct axienet_dma_q *q = lp->dq[0];

	chan_sermask = axienet_dma_in32(q, XMCDMA_TXINT_SER_OFFSET);

	for (i = 1, chan_id = 1; i != 0 && i <= chan_sermask;
	     i <<= 1, chan_id++) {
		if (chan_sermask & i)
			return chan_id;
	}

	return -ENODEV;
}

static irqreturn_t __maybe_unused axienet_mcdma_tx_irq(int irq, void *_ndev)
{
	u32 cr;
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	int i, j = map_dma_q_txirq(irq, lp);
	struct axienet_dma_q *q;

	if (j < 0)
		return IRQ_NONE;

	i = get_mcdma_q(lp, j);
	q = lp->dq[i];

	status = axienet_dma_in32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id));
	if (status & (XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK)) {
		axienet_dma_out32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id), status);
		axienet_start_xmit_done(lp->ndev, q);
		goto out;
	}
	if (!(status & XMCDMA_IRQ_ALL_MASK))
		return IRQ_NONE;
	if (status & XMCDMA_IRQ_ERR_MASK) {
		dev_err(&ndev->dev, "DMA Tx error 0x%x\n", status);
		dev_err(&ndev->dev, "Current BD is at: 0x%x\n",
			(q->txq_bd_v[q->tx_bd_ci]).phys);

		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id));
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XMCDMA_IRQ_ALL_MASK);
		/* Finally write to the Tx channel control register */
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id), cr);

		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				      q->rx_offset);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XMCDMA_IRQ_ALL_MASK);
		/* write to the Rx channel control register */
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				  q->rx_offset, cr);

		tasklet_schedule(&lp->dma_err_tasklet[i]);
		axienet_dma_out32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
				  q->rx_offset, status);
	}
out:
	return IRQ_HANDLED;
}

static inline int map_dma_q_rxirq(int irq, struct axienet_local *lp)
{
	int i, chan_sermask;
	u16 chan_id = 1;
	struct axienet_dma_q *q = lp->dq[0];

	chan_sermask = axienet_dma_in32(q, XMCDMA_RXINT_SER_OFFSET +
					q->rx_offset);

	for (i = 1, chan_id = 1; i != 0 && i <= chan_sermask;
		i <<= 1, chan_id++) {
		if (chan_sermask & i)
			return chan_id;
	}

	return -ENODEV;
}

static irqreturn_t __maybe_unused axienet_mcdma_rx_irq(int irq, void *_ndev)
{
	u32 cr;
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	int i, j = map_dma_q_rxirq(irq, lp);
	struct axienet_dma_q *q;

	if (j < 0)
		return IRQ_NONE;

	i = get_mcdma_q(lp, j);
	q = lp->dq[i];

	status = axienet_dma_in32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
				  q->rx_offset);
	if (status & (XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK)) {
		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				      q->rx_offset);
		cr &= ~(XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK);
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				  q->rx_offset, cr);
		napi_schedule(&lp->napi[i]);
	}

	if (!(status & XMCDMA_IRQ_ALL_MASK))
		return IRQ_NONE;

	if (status & XMCDMA_IRQ_ERR_MASK) {
		dev_err(&ndev->dev, "DMA Rx error 0x%x\n", status);
		dev_err(&ndev->dev, "Current BD is at: 0x%x\n",
			(q->rxq_bd_v[q->rx_bd_ci]).phys);

		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id));
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XMCDMA_IRQ_ALL_MASK);
		/* Finally write to the Tx channel control register */
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id), cr);

		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				      q->rx_offset);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XMCDMA_IRQ_ALL_MASK);
		/* write to the Rx channel control register */
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				  q->rx_offset, cr);

		tasklet_schedule(&lp->dma_err_tasklet[i]);
		axienet_dma_out32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
				  q->rx_offset, status);
	}

	return IRQ_HANDLED;
}

/**
 * map_dma_q_irq - Map dma q based on interrupt number.
 * @irq:	irq number
 * @lp:		axienet local structure
 *
 * Return: DMA queue.
 *
 * This returns the DMA number on which interrupt has occurred.
 */
static int map_dma_q_irq(int irq, struct axienet_local *lp)
{
	int i;

	for_each_dma_queue(lp, i) {
		if (irq == lp->dq[i]->tx_irq || irq == lp->dq[i]->rx_irq)
			return i;
	}
	pr_err("Error mapping DMA irq\n");
	return -ENODEV;
}

/**
 * axienet_tx_irq - Tx Done Isr.
 * @irq:	irq number
 * @_ndev:	net_device pointer
 *
 * Return: IRQ_HANDLED or IRQ_NONE.
 *
 * This is the Axi DMA Tx done Isr. It invokes "axienet_start_xmit_done"
 * to complete the BD processing.
 */
static irqreturn_t __maybe_unused axienet_tx_irq(int irq, void *_ndev)
{
	u32 cr;
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	int i = map_dma_q_irq(irq, lp);
	struct axienet_dma_q *q;

	if (i < 0)
		return IRQ_NONE;

	q = lp->dq[i];

	status = axienet_dma_in32(q, XAXIDMA_TX_SR_OFFSET);
	if (status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) {
		axienet_dma_out32(q, XAXIDMA_TX_SR_OFFSET, status);
		axienet_start_xmit_done(lp->ndev, q);
		goto out;
	}

	if (!(status & XAXIDMA_IRQ_ALL_MASK))
		dev_err(&ndev->dev, "No interrupts asserted in Tx path\n");

	if (status & XAXIDMA_IRQ_ERROR_MASK) {
		dev_err(&ndev->dev, "DMA Tx error 0x%x\n", status);
		dev_err(&ndev->dev, "Current BD is at: 0x%x\n",
			(q->tx_bd_v[q->tx_bd_ci]).phys);

		cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Write to the Tx channel control register */
		axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, cr);

		cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Write to the Rx channel control register */
		axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);

		tasklet_schedule(&lp->dma_err_tasklet[i]);
		axienet_dma_out32(q, XAXIDMA_TX_SR_OFFSET, status);
	}
out:
	return IRQ_HANDLED;
}

/**
 * axienet_rx_irq - Rx Isr.
 * @irq:	irq number
 * @_ndev:	net_device pointer
 *
 * Return: IRQ_HANDLED or IRQ_NONE.
 *
 * This is the Axi DMA Rx Isr. It invokes "axienet_recv" to complete the BD
 * processing.
 */
static irqreturn_t __maybe_unused axienet_rx_irq(int irq, void *_ndev)
{
	u32 cr;
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	int i = map_dma_q_irq(irq, lp);
	struct axienet_dma_q *q;

	if (i < 0)
		return IRQ_NONE;

	q = lp->dq[i];

	status = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
	if (status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) {
		cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		cr &= ~(XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK);
		axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);
		napi_schedule(&lp->napi[i]);
	}

	if (!(status & XAXIDMA_IRQ_ALL_MASK))
		dev_err(&ndev->dev, "No interrupts asserted in Rx path\n");

	if (status & XAXIDMA_IRQ_ERROR_MASK) {
		dev_err(&ndev->dev, "DMA Rx error 0x%x\n", status);
		dev_err(&ndev->dev, "Current BD is at: 0x%x\n",
			(q->rx_bd_v[q->rx_bd_ci]).phys);

		cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Finally write to the Tx channel control register */
		axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, cr);

		cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
			/* write to the Rx channel control register */
		axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);

		tasklet_schedule(&lp->dma_err_tasklet[i]);
		axienet_dma_out32(q, XAXIDMA_RX_SR_OFFSET, status);
	}

	return IRQ_HANDLED;
}

static void axienet_dma_err_handler(unsigned long data);
static void axienet_mcdma_err_handler(unsigned long data);

static int axienet_mii_init(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int ret, mdio_mcreg;

	mdio_mcreg = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	/* Disable the MDIO interface till Axi Ethernet Reset is completed.
	 * When we do an Axi Ethernet reset, it resets the complete core
	 * Including the MDIO. If MDIO is not disabled when the reset process is
	 * Started, MDIO will be broken afterwards.
	 */
	axienet_iow(lp, XAE_MDIO_MC_OFFSET,
		    (mdio_mcreg & (~XAE_MDIO_MC_MDIOEN_MASK)));
	axienet_device_reset(ndev);
	/* Enable the MDIO */
	axienet_iow(lp, XAE_MDIO_MC_OFFSET, mdio_mcreg);
	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * axienet_open - Driver open routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *	    -ENODEV, if PHY cannot be connected to
 *	    non-zero error value on failure
 *
 * This is the driver open routine. It calls phy_start to start the PHY device.
 * It also allocates interrupt service routines, enables the interrupt lines
 * and ISR handling. Axi Ethernet core is reset through Axi DMA core. Buffer
 * descriptors are initialized.
 */
static int axienet_open(struct net_device *ndev)
{
	int ret = 0, i;
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = NULL;
	struct axienet_dma_q *q;

	dev_dbg(&ndev->dev, "axienet_open()\n");

	if (lp->axienet_config->mactype == XAXIENET_10G_25G)
		axienet_device_reset(ndev);
	else
		ret = axienet_mii_init(ndev);
	if (ret < 0)
		return ret;

	if (lp->phy_node) {
		if (lp->phy_type == XAE_PHY_TYPE_GMII) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
						axienet_adjust_link, 0,
						PHY_INTERFACE_MODE_GMII);
		} else if (lp->phy_type == XAE_PHY_TYPE_RGMII_2_0) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
						axienet_adjust_link, 0,
						PHY_INTERFACE_MODE_RGMII_ID);
		} else if ((lp->axienet_config->mactype == XAXIENET_1G) ||
			     (lp->axienet_config->mactype == XAXIENET_2_5G)) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
						axienet_adjust_link,
						lp->phy_flags,
						lp->phy_interface);
		}

		if (!phydev)
			dev_err(lp->dev, "of_phy_connect() failed\n");
		else
			phy_start(phydev);
	}

	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		/* Enable tasklets for Axi DMA error handling */
		for_each_dma_queue(lp, i) {
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
			napi_enable(&lp->napi[i]);
		}
		for_each_dma_queue(lp, i) {
			struct axienet_dma_q *q = lp->dq[i];
#ifdef CONFIG_AXIENET_HAS_MCDMA
			/* Enable interrupts for Axi MCDMA Tx */
			ret = request_irq(q->tx_irq, axienet_mcdma_tx_irq,
					  IRQF_SHARED, ndev->name, ndev);
			if (ret)
				goto err_tx_irq;

			/* Enable interrupts for Axi MCDMA Rx */
			ret = request_irq(q->rx_irq, axienet_mcdma_rx_irq,
					  IRQF_SHARED, ndev->name, ndev);
			if (ret)
				goto err_rx_irq;
#else
			/* Enable interrupts for Axi DMA Tx */
			ret = request_irq(q->tx_irq, axienet_tx_irq,
					  0, ndev->name, ndev);
			if (ret)
				goto err_tx_irq;
			/* Enable interrupts for Axi DMA Rx */
			ret = request_irq(q->rx_irq, axienet_rx_irq,
					  0, ndev->name, ndev);
			if (ret)
				goto err_rx_irq;
#endif
		}
	}
#ifdef CONFIG_XILINX_TSN_PTP
	if (lp->is_tsn) {
		INIT_WORK(&lp->tx_tstamp_work, axienet_tx_tstamp);
		skb_queue_head_init(&lp->ptp_txq);

		lp->ptp_rx_hw_pointer = 0;
		lp->ptp_rx_sw_pointer = 0xff;

		axienet_iow(lp, PTP_RX_CONTROL_OFFSET, PTP_RX_PACKET_CLEAR);

		ret = request_irq(lp->ptp_rx_irq, axienet_ptp_rx_irq,
				  0, "ptp_rx", ndev);
		if (ret)
			goto err_ptp_rx_irq;

		ret = request_irq(lp->ptp_tx_irq, axienet_ptp_tx_irq,
				  0, "ptp_tx", ndev);
		if (ret)
			goto err_ptp_rx_irq;
	}
#endif

	if (!lp->eth_hasnobuf && (lp->axienet_config->mactype == XAXIENET_1G)) {
		/* Enable interrupts for Axi Ethernet */
		ret = request_irq(lp->eth_irq, axienet_err_irq, 0, ndev->name,
				  ndev);
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
	i = lp->num_queues;
err_rx_irq:
	while (i--) {
		q = lp->dq[i];
		free_irq(q->tx_irq, ndev);
	}
err_tx_irq:
	for_each_dma_queue(lp, i)
		napi_disable(&lp->napi[i]);
#ifdef CONFIG_XILINX_TSN_PTP
err_ptp_rx_irq:
#endif
	if (phydev)
		phy_disconnect(phydev);
	phydev = NULL;
	for_each_dma_queue(lp, i)
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
	u32 cr;
	u32 i;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;

	dev_dbg(&ndev->dev, "axienet_close()\n");

	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		for_each_dma_queue(lp, i) {
			q = lp->dq[i];
			cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
			axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET,
					  cr & (~XAXIDMA_CR_RUNSTOP_MASK));
			cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
			axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET,
					  cr & (~XAXIDMA_CR_RUNSTOP_MASK));
			lp->axienet_config->setoptions(ndev, lp->options &
				   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

			netif_stop_queue(ndev);
			napi_disable(&lp->napi[i]);
			tasklet_kill(&lp->dma_err_tasklet[i]);

			free_irq(q->tx_irq, ndev);
			free_irq(q->rx_irq, ndev);
		}
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

	if (lp->temac_no != XAE_TEMAC2)
		axienet_dma_bd_release(ndev);
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

	if ((new_mtu > XAE_JUMBO_MTU) || (new_mtu < 64))
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

	disable_irq(lp->tx_irq);
	disable_irq(lp->rx_irq);
	axienet_rx_irq(lp->tx_irq, ndev);
	axienet_tx_irq(lp->rx_irq, ndev);
	enable_irq(lp->tx_irq);
	enable_irq(lp->rx_irq);
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

#ifdef CONFIG_XILINX_TSN_PTP
	if (lp->is_tsn) {
		/* reserved for future extensions */
		if (config->flags)
			return -EINVAL;

		if ((config->tx_type != HWTSTAMP_TX_OFF) &&
		    (config->tx_type != HWTSTAMP_TX_ON))
			return -ERANGE;

		config->tx_type = HWTSTAMP_TX_ON;

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
	regval = axienet_ior(lp, XAE_TC_OFFSET);

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		regval &= ~XAE_TC_INBAND1588_MASK;
		break;
	case HWTSTAMP_TX_ON:
		config->tx_type = HWTSTAMP_TX_ON;
		regval |= XAE_TC_INBAND1588_MASK;
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		config->tx_type = HWTSTAMP_TX_ONESTEP_SYNC;
		regval |= XAE_TC_INBAND1588_MASK;
		break;
	default:
		return -ERANGE;
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		axienet_iow(lp, XAE_TC_OFFSET, regval);

	/* Read the current value in the MAC RX RCW1 register */
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

	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
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
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	struct axienet_local *lp = netdev_priv(dev);
#endif

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return phy_mii_ioctl(dev->phydev, rq, cmd);
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	case SIOCSHWTSTAMP:
		return axienet_set_ts_config(lp, rq);
	case SIOCGHWTSTAMP:
		return axienet_get_ts_config(lp, rq);
#endif
#ifdef CONFIG_XILINX_TSN_QBV
	case SIOCCHIOCTL:
		return axienet_set_schedule(dev, rq->ifr_data);
#endif
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops axienet_netdev_ops = {
	.ndo_open = axienet_open,
	.ndo_stop = axienet_stop,
	.ndo_start_xmit = axienet_start_xmit,
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
 * axienet_ethtools_get_settings - Get Axi Ethernet settings related to PHY.
 * @ndev:	Pointer to net_device structure
 * @ecmd:	Pointer to ethtool_cmd structure
 *
 * This implements ethtool command for getting PHY settings. If PHY could
 * not be found, the function returns -ENODEV. This function calls the
 * relevant PHY ethtool API to get the PHY settings.
 * Issue "ethtool ethX" under linux prompt to execute this function.
 *
 * Return: 0 on success, -ENODEV if PHY doesn't exist
 */
static int axienet_ethtools_get_settings(struct net_device *ndev,
					 struct ethtool_cmd *ecmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!phydev)
		return -ENODEV;
	return phy_ethtool_gset(phydev, ecmd);
}

/**
 * axienet_ethtools_set_settings - Set PHY settings as passed in the argument.
 * @ndev:	Pointer to net_device structure
 * @ecmd:	Pointer to ethtool_cmd structure
 *
 * This implements ethtool command for setting various PHY settings. If PHY
 * could not be found, the function returns -ENODEV. This function calls the
 * relevant PHY ethtool API to set the PHY.
 * Issue e.g. "ethtool -s ethX speed 1000" under linux prompt to execute this
 * function.
 *
 * Return: 0 on success, -ENODEV if PHY doesn't exist
 */
static int axienet_ethtools_set_settings(struct net_device *ndev,
					 struct ethtool_cmd *ecmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!phydev)
		return -ENODEV;
	return phy_ethtool_sset(phydev, ecmd);
}

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
	data[18] = axienet_ior(lp, XAE_PHYC_OFFSET);
	data[19] = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	data[20] = axienet_ior(lp, XAE_MDIO_MCR_OFFSET);
	data[21] = axienet_ior(lp, XAE_MDIO_MWD_OFFSET);
	data[22] = axienet_ior(lp, XAE_MDIO_MRD_OFFSET);
	data[23] = axienet_ior(lp, XAE_MDIO_MIS_OFFSET);
	data[24] = axienet_ior(lp, XAE_MDIO_MIP_OFFSET);
	data[25] = axienet_ior(lp, XAE_MDIO_MIE_OFFSET);
	data[26] = axienet_ior(lp, XAE_MDIO_MIC_OFFSET);
	data[27] = axienet_ior(lp, XAE_UAW0_OFFSET);
	data[28] = axienet_ior(lp, XAE_UAW1_OFFSET);
	data[29] = axienet_ior(lp, XAE_FMI_OFFSET);
	data[30] = axienet_ior(lp, XAE_AF0_OFFSET);
	data[31] = axienet_ior(lp, XAE_AF1_OFFSET);
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

	for_each_dma_queue(lp, i) {
		q = lp->dq[i];

		regval = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		ecoalesce->rx_max_coalesced_frames +=
						(regval & XAXIDMA_COALESCE_MASK)
						     >> XAXIDMA_COALESCE_SHIFT;
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

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
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
	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);
	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_ALL);
	info->phc_index = 0;

#ifdef CONFIG_XILINX_TSN_PTP
	info->phc_index = axienet_phc_index;
#endif
	return 0;
}
#endif

#ifdef CONFIG_AXIENET_HAS_MCDMA
static void axienet_strings(struct net_device *ndev,
			    u32 sset, u8 *data)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	int i, j, k = 0;

	for (i = 0, j = 0; i < AXIENET_SSTATS_LEN(lp);) {
		if (j >= lp->num_queues)
			break;
		q = lp->dq[j];
		if (i % 4 == 0)
			k = (q->chan_id - 1) * 4;
		if (sset == ETH_SS_STATS)
			memcpy(data + i * ETH_GSTRING_LEN,
			       axienet_get_strings_stats[k].name,
			       ETH_GSTRING_LEN);
		++i;
		k++;
		if (i % 4 == 0)
			++j;
	}
}

static int axienet_sset_count(struct net_device *ndev,
			      int sset)
{
	struct axienet_local *lp = netdev_priv(ndev);

	switch (sset) {
	case ETH_SS_STATS:
		return AXIENET_SSTATS_LEN(lp);
	default:
		return -EOPNOTSUPP;
	}
}

static void axienet_get_stats(struct net_device *ndev,
			      struct ethtool_stats *stats,
			      u64 *data)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	unsigned int i = 0, j;

	for (i = 0, j = 0; i < AXIENET_SSTATS_LEN(lp);) {
		if (j >= lp->num_queues)
			break;

		q = lp->dq[j];
		data[i++] = q->tx_packets;
		data[i++] = q->tx_bytes;
		data[i++] = q->rx_packets;
		data[i++] = q->rx_bytes;
		++j;
	}
}
#endif

static const struct ethtool_ops axienet_ethtool_ops = {
	.get_settings   = axienet_ethtools_get_settings,
	.set_settings   = axienet_ethtools_set_settings,
	.get_drvinfo    = axienet_ethtools_get_drvinfo,
	.get_regs_len   = axienet_ethtools_get_regs_len,
	.get_regs       = axienet_ethtools_get_regs,
	.get_link       = ethtool_op_get_link,
	.get_pauseparam = axienet_ethtools_get_pauseparam,
	.set_pauseparam = axienet_ethtools_set_pauseparam,
	.get_coalesce   = axienet_ethtools_get_coalesce,
	.set_coalesce   = axienet_ethtools_set_coalesce,
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	.get_ts_info    = axienet_ethtools_get_ts_info,
#endif
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
#ifdef CONFIG_AXIENET_HAS_MCDMA
	.get_sset_count	 = axienet_sset_count,
	.get_ethtool_stats = axienet_get_stats,
	.get_strings = axienet_strings,
#endif
};

/**
 * axienet_mcdma_err_handler - Tasklet handler for Axi MCDMA Error
 * @data:	Data passed
 *
 * Resets the Axi MCDMA and Axi Ethernet devices, and reconfigures the
 * Tx/Rx BDs.
 */
static void __maybe_unused axienet_mcdma_err_handler(unsigned long data)
{
	u32 axienet_status;
	u32 cr, i, chan_en;
	int mdio_mcreg = 0;
	struct axienet_dma_q *q = (struct axienet_dma_q *)data;
	struct axienet_local *lp = q->lp;
	struct net_device *ndev = lp->ndev;
	struct aximcdma_bd *cur_p;

	lp->axienet_config->setoptions(ndev, lp->options &
				       ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
		mdio_mcreg = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
		axienet_mdio_wait_until_ready(lp);
		/* Disable the MDIO interface till Axi Ethernet Reset is
		 * Completed. When we do an Axi Ethernet reset, it resets the
		 * Complete core including the MDIO. So if MDIO is not disabled
		 * When the reset process is started,
		 * MDIO will be broken afterwards.
		 */
		axienet_iow(lp, XAE_MDIO_MC_OFFSET, (mdio_mcreg &
			    ~XAE_MDIO_MC_MDIOEN_MASK));
	}

	__axienet_device_reset(q, XAXIDMA_TX_CR_OFFSET);

	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
		axienet_iow(lp, XAE_MDIO_MC_OFFSET, mdio_mcreg);
		axienet_mdio_wait_until_ready(lp);
	}

	for (i = 0; i < TX_BD_NUM; i++) {
		cur_p = &q->txq_bd_v[i];
		if (cur_p->phys)
			dma_unmap_single(ndev->dev.parent, cur_p->phys,
					 (cur_p->cntrl &
					  XAXIDMA_BD_CTRL_LENGTH_MASK),
					 DMA_TO_DEVICE);
		if (cur_p->tx_skb)
			dev_kfree_skb_irq((struct sk_buff *)cur_p->tx_skb);
		cur_p->phys = 0;
		cur_p->cntrl = 0;
		cur_p->status = 0;
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app3 = 0;
		cur_p->app4 = 0;
		cur_p->sw_id_offset = 0;
		cur_p->tx_skb = 0;
	}

	for (i = 0; i < RX_BD_NUM; i++) {
		cur_p = &q->rxq_bd_v[i];
		cur_p->status = 0;
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app3 = 0;
		cur_p->app4 = 0;
	}

	q->tx_bd_ci = 0;
	q->tx_bd_tail = 0;
	q->rx_bd_ci = 0;

	/* Start updating the Rx channel control register */
	cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
			      q->rx_offset);
	/* Update the interrupt coalesce count */
	cr = ((cr & ~XMCDMA_COALESCE_MASK) |
	      ((lp->coalesce_count_rx) << XMCDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = ((cr & ~XMCDMA_DELAY_MASK) |
	      (XAXIDMA_DFT_RX_WAITBOUND << XMCDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XMCDMA_IRQ_ALL_MASK;
	/* Write to the Rx channel control register */
	axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
			  q->rx_offset, cr);

	/* Start updating the Tx channel control register */
	cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id));
	/* Update the interrupt coalesce count */
	cr = (((cr & ~XMCDMA_COALESCE_MASK)) |
	      ((lp->coalesce_count_tx) << XMCDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = (((cr & ~XMCDMA_DELAY_MASK)) |
	      (XAXIDMA_DFT_TX_WAITBOUND << XMCDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XMCDMA_IRQ_ALL_MASK;
	/* Write to the Tx channel control register */
	axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id), cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	axienet_dma_bdout(q, XMCDMA_CHAN_CURDESC_OFFSET(q->chan_id) +
			    q->rx_offset, q->rx_bd_p);
	cr = axienet_dma_in32(q, XMCDMA_CR_OFFSET +  q->rx_offset);
	axienet_dma_out32(q, XMCDMA_CR_OFFSET +  q->rx_offset,
			  cr | XMCDMA_CR_RUNSTOP_MASK);
	cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				q->rx_offset);
	axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) + q->rx_offset,
			  cr | XMCDMA_CR_RUNSTOP_MASK);
	axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id) +
			    q->rx_offset, q->rx_bd_p + (sizeof(*q->rxq_bd_v) *
			    (RX_BD_NUM - 1)));
	chan_en = axienet_dma_in32(q, XMCDMA_CHEN_OFFSET + q->rx_offset);
	chan_en |= (1 << (q->chan_id - 1));
	axienet_dma_out32(q, XMCDMA_CHEN_OFFSET + q->rx_offset, chan_en);

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting.
	 */
	axienet_dma_bdout(q, XMCDMA_CHAN_CURDESC_OFFSET(q->chan_id),
			  q->tx_bd_p);
	cr = axienet_dma_in32(q, XMCDMA_CR_OFFSET);
	axienet_dma_out32(q, XMCDMA_CR_OFFSET,
			  cr | XMCDMA_CR_RUNSTOP_MASK);
	cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id));
	axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id),
			  cr | XMCDMA_CR_RUNSTOP_MASK);
	chan_en = axienet_dma_in32(q, XMCDMA_CHEN_OFFSET);
	chan_en |= (1 << (q->chan_id - 1));
	axienet_dma_out32(q, XMCDMA_CHEN_OFFSET, chan_en);

	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
		axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
		axienet_status &= ~XAE_RCW1_RX_MASK;
		axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);
	}

	if ((lp->axienet_config->mactype == XAXIENET_1G) && !lp->eth_hasnobuf) {
		axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
		if (axienet_status & XAE_INT_RXRJECT_MASK)
			axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);

	lp->axienet_config->setoptions(ndev, lp->options &
				       ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));
	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	lp->axienet_config->setoptions(ndev, lp->options);
}

/**
 * axienet_dma_err_handler - Tasklet handler for Axi DMA Error
 * @data:	Data passed
 *
 * Resets the Axi DMA and Axi Ethernet devices, and reconfigures the
 * Tx/Rx BDs.
 */
static void __maybe_unused axienet_dma_err_handler(unsigned long data)
{
	u32 axienet_status;
	u32 cr, i;
	int mdio_mcreg = 0;
	struct axienet_dma_q *q = (struct axienet_dma_q *)data;
	struct axienet_local *lp = q->lp;
	struct net_device *ndev = lp->ndev;
	struct axidma_bd *cur_p;

	lp->axienet_config->setoptions(ndev, lp->options &
				       ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
		mdio_mcreg = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
		axienet_mdio_wait_until_ready(lp);
		/* Disable the MDIO interface till Axi Ethernet Reset is
		 * Completed. When we do an Axi Ethernet reset, it resets the
		 * Complete core including the MDIO. So if MDIO is not disabled
		 * When the reset process is started,
		 * MDIO will be broken afterwards.
		 */
		axienet_iow(lp, XAE_MDIO_MC_OFFSET, (mdio_mcreg &
			    ~XAE_MDIO_MC_MDIOEN_MASK));
	}

	__axienet_device_reset(q, XAXIDMA_TX_CR_OFFSET);
	__axienet_device_reset(q, XAXIDMA_RX_CR_OFFSET);

	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
		axienet_iow(lp, XAE_MDIO_MC_OFFSET, mdio_mcreg);
		axienet_mdio_wait_until_ready(lp);
	}

	for (i = 0; i < TX_BD_NUM; i++) {
		cur_p = &q->tx_bd_v[i];
		if (cur_p->phys)
			dma_unmap_single(ndev->dev.parent, cur_p->phys,
					 (cur_p->cntrl &
					  XAXIDMA_BD_CTRL_LENGTH_MASK),
					 DMA_TO_DEVICE);
		if (cur_p->tx_skb)
			dev_kfree_skb_irq((struct sk_buff *)cur_p->tx_skb);
		cur_p->phys = 0;
		cur_p->cntrl = 0;
		cur_p->status = 0;
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app3 = 0;
		cur_p->app4 = 0;
		cur_p->sw_id_offset = 0;
		cur_p->tx_skb = 0;
	}

	for (i = 0; i < RX_BD_NUM; i++) {
		cur_p = &q->rx_bd_v[i];
		cur_p->status = 0;
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app3 = 0;
		cur_p->app4 = 0;
	}

	q->tx_bd_ci = 0;
	q->tx_bd_tail = 0;
	q->rx_bd_ci = 0;

	/* Start updating the Rx channel control register */
	cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = ((cr & ~XAXIDMA_COALESCE_MASK) |
	      (XAXIDMA_DFT_RX_THRESHOLD << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = ((cr & ~XAXIDMA_DELAY_MASK) |
	      (XAXIDMA_DFT_RX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Finally write to the Rx channel control register */
	axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);

	/* Start updating the Tx channel control register */
	cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = (((cr & ~XAXIDMA_COALESCE_MASK)) |
	      (XAXIDMA_DFT_TX_THRESHOLD << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = (((cr & ~XAXIDMA_DELAY_MASK)) |
	      (XAXIDMA_DFT_TX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Finally write to the Tx channel control register */
	axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	axienet_dma_bdout(q, XAXIDMA_RX_CDESC_OFFSET, q->rx_bd_p);
	cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
	axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET,
			  cr | XAXIDMA_CR_RUNSTOP_MASK);
	axienet_dma_bdout(q, XAXIDMA_RX_TDESC_OFFSET, q->rx_bd_p +
			  (sizeof(*q->rx_bd_v) * (RX_BD_NUM - 1)));

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting
	 */
	axienet_dma_bdout(q, XAXIDMA_TX_CDESC_OFFSET, q->tx_bd_p);
	cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
	axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET,
			  cr | XAXIDMA_CR_RUNSTOP_MASK);

	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
		axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
		axienet_status &= ~XAE_RCW1_RX_MASK;
		axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);
	}

	if ((lp->axienet_config->mactype == XAXIENET_1G) && !lp->eth_hasnobuf) {
		axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
		if (axienet_status & XAE_INT_RXRJECT_MASK)
			axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);

	lp->axienet_config->setoptions(ndev, lp->options &
			   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));
	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	lp->axienet_config->setoptions(ndev, lp->options);
}

static int __maybe_unused axienet_mcdma_probe(struct platform_device *pdev,
					      struct axienet_local *lp,
					      struct net_device *ndev)
{
	int i, ret = 0;
	struct axienet_dma_q *q;
	struct device_node *np;
	struct resource dmares;
	char dma_name[16];
	const char *str;

	ret = of_property_count_strings(pdev->dev.of_node, "xlnx,channel-ids");
	if (ret < 0)
		return -EINVAL;

	for_each_dma_queue(lp, i) {
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

	lp->mcdma_regs = devm_ioremap_resource(&pdev->dev, &dmares);
	if (IS_ERR(lp->mcdma_regs)) {
		dev_err(&pdev->dev, "iormeap failed for the dma\n");
		ret = PTR_ERR(lp->mcdma_regs);
		return ret;
	}

	/* Find the DMA node, map the DMA registers, and decode the DMA IRQs */
	for_each_dma_queue(lp, i) {
		struct axienet_dma_q *q;

		q = lp->dq[i];

		q->dma_regs = lp->mcdma_regs;
		sprintf(dma_name, "dma%d_tx", i);
		q->tx_irq = platform_get_irq_byname(pdev, dma_name);
		sprintf(dma_name, "dma%d_rx", i);
		q->rx_irq = platform_get_irq_byname(pdev, dma_name);
		q->eth_hasdre = of_property_read_bool(np,
						      "xlnx,include-dre");
	}
	of_node_put(np);

	for_each_dma_queue(lp, i) {
		struct axienet_dma_q *q = lp->dq[i];

		spin_lock_init(&q->tx_lock);
		spin_lock_init(&q->rx_lock);
	}

	for_each_dma_queue(lp, i) {
		netif_napi_add(ndev, &lp->napi[i], xaxienet_rx_poll,
			       XAXIENET_NAPI_WEIGHT);
	}

	return 0;
}

static int __maybe_unused axienet_dma_probe(struct platform_device *pdev,
					    struct net_device *ndev)
{
	int i, ret;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	struct device_node *np;
	struct resource dmares;
#ifdef CONFIG_XILINX_TSN
	char dma_name[10];
#endif

	for_each_dma_queue(lp, i) {
		q = kmalloc(sizeof(*q), GFP_KERNEL);

		/* parent */
		q->lp = lp;

		lp->dq[i] = q;
	}

	/* Find the DMA node, map the DMA registers, and decode the DMA IRQs */
	/* TODO handle error ret */
	for_each_dma_queue(lp, i) {
		q = lp->dq[i];

		np = of_parse_phandle(pdev->dev.of_node, "axistream-connected",
				      i);
		if (np) {
			ret = of_address_to_resource(np, 0, &dmares);
			if (ret >= 0)
				q->dma_regs = devm_ioremap_resource(&pdev->dev,
								&dmares);
			else
				return -ENODEV;
			q->eth_hasdre = of_property_read_bool(np,
							"xlnx,include-dre");
		} else {
			return -EINVAL;
		}
	}

#ifdef CONFIG_XILINX_TSN
	if (lp->is_tsn) {
		for_each_dma_queue(lp, i) {
			sprintf(dma_name, "dma%d_tx", i);
			lp->dq[i]->tx_irq = platform_get_irq_byname(pdev,
								    dma_name);
			sprintf(dma_name, "dma%d_rx", i);
			lp->dq[i]->rx_irq = platform_get_irq_byname(pdev,
								    dma_name);
			pr_info("lp->dq[%d]->tx_irq  %d\n", i,
				lp->dq[i]->tx_irq);
			pr_info("lp->dq[%d]->rx_irq  %d\n", i,
				lp->dq[i]->rx_irq);
		}
	} else {
#endif /* This should remove when axienet device tree irq comply to dma name */
		for_each_dma_queue(lp, i) {
			lp->dq[i]->tx_irq = irq_of_parse_and_map(np, 0);
			lp->dq[i]->rx_irq = irq_of_parse_and_map(np, 1);
		}
#ifdef CONFIG_XILINX_TSN
	}
#endif

	of_node_put(np);

	for_each_dma_queue(lp, i) {
		struct axienet_dma_q *q = lp->dq[i];

		spin_lock_init(&q->tx_lock);
		spin_lock_init(&q->rx_lock);
	}

	for_each_dma_queue(lp, i) {
		netif_napi_add(ndev, &lp->napi[i], xaxienet_rx_poll,
			       XAXIENET_NAPI_WEIGHT);
	}

	return 0;
}

static const struct axienet_config axienet_1g_config = {
	.mactype = XAXIENET_1G,
	.setoptions = axienet_setoptions,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_2_5g_config = {
	.mactype = XAXIENET_2_5G,
	.setoptions = axienet_setoptions,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_10g_config = {
	.mactype = XAXIENET_LEGACY_10G,
	.setoptions = axienet_setoptions,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_10g25g_config = {
	.mactype = XAXIENET_10G_25G,
	.setoptions = xxvenet_setoptions,
	.tx_ptplen = XXV_TX_PTP_LEN,
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
	{},
};

MODULE_DEVICE_TABLE(of, axienet_of_match);

#ifdef CONFIG_AXIENET_HAS_MCDMA
static ssize_t rxch_obs1_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS1_OFFSET + q->rx_offset);

	return sprintf(buf, "Ingress Channel Observer 1 Contents is 0x%x\n",
		       reg);
}

static ssize_t rxch_obs2_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS2_OFFSET + q->rx_offset);

	return sprintf(buf, "Ingress Channel Observer 2 Contents is 0x%x\n",
		       reg);
}

static ssize_t rxch_obs3_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS3_OFFSET + q->rx_offset);

	return sprintf(buf, "Ingress Channel Observer 3 Contents is 0x%x\n",
		       reg);
}

static ssize_t rxch_obs4_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS4_OFFSET + q->rx_offset);

	return sprintf(buf, "Ingress Channel Observer 4 Contents is 0x%x\n",
		       reg);
}

static ssize_t rxch_obs5_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS5_OFFSET + q->rx_offset);

	return sprintf(buf, "Ingress Channel Observer 5 Contents is 0x%x\n",
		       reg);
}

static ssize_t rxch_obs6_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS6_OFFSET + q->rx_offset);

	return sprintf(buf, "Ingress Channel Observer 6 Contents is 0x%x\n\r",
		       reg);
}

static ssize_t txch_obs1_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS1_OFFSET);

	return sprintf(buf, "Egress Channel Observer 1 Contents is 0x%x\n",
		       reg);
}

static ssize_t txch_obs2_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS2_OFFSET);

	return sprintf(buf, "Egress Channel Observer 2 Contents is 0x%x\n\r",
		       reg);
}

static ssize_t txch_obs3_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS3_OFFSET);

	return sprintf(buf, "Egress Channel Observer 3 Contents is 0x%x\n\r",
		       reg);
}

static ssize_t txch_obs4_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS4_OFFSET);

	return sprintf(buf, "Egress Channel Observer 4 Contents is 0x%x\n\r",
		       reg);
}

static ssize_t txch_obs5_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS5_OFFSET);

	return sprintf(buf, "Egress Channel Observer 5 Contents is 0x%x\n\r",
		       reg);
}

static ssize_t txch_obs6_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	u32 reg;

	reg = axienet_dma_in32(q, XMCDMA_CHOBS6_OFFSET);

	return sprintf(buf, "Egress Channel Observer 6 Contents is 0x%x\n\r",
		       reg);
}

static ssize_t chan_weight_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);

	return sprintf(buf, "chan_id is %d and weight is %d\n",
		       lp->chan_id, lp->weight);
}

static ssize_t chan_weight_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q = lp->dq[0];
	int ret;
	u16 flags, chan_id;
	u32 val;

	ret = kstrtou16(buf, 16, &flags);
	if (ret)
		return ret;

	lp->chan_id = (flags & 0xF0) >> 4;
	lp->weight = flags & 0x0F;

	if (lp->chan_id < 8)
		val = axienet_dma_in32(q, XMCDMA_TXWEIGHT0_OFFSET);
	else
		val = axienet_dma_in32(q, XMCDMA_TXWEIGHT1_OFFSET);

	if (lp->chan_id > 7)
		chan_id = lp->chan_id - 8;
	else
		chan_id = lp->chan_id;

	val &= ~XMCDMA_TXWEIGHT_CH_MASK(chan_id);
	val |= lp->weight << XMCDMA_TXWEIGHT_CH_SHIFT(chan_id);

	if (lp->chan_id < 8)
		axienet_dma_out32(q, XMCDMA_TXWEIGHT0_OFFSET, val);
	else
		axienet_dma_out32(q, XMCDMA_TXWEIGHT1_OFFSET, val);

	return count;
}

static DEVICE_ATTR_RW(chan_weight);
static DEVICE_ATTR_RO(rxch_obs1);
static DEVICE_ATTR_RO(rxch_obs2);
static DEVICE_ATTR_RO(rxch_obs3);
static DEVICE_ATTR_RO(rxch_obs4);
static DEVICE_ATTR_RO(rxch_obs5);
static DEVICE_ATTR_RO(rxch_obs6);
static DEVICE_ATTR_RO(txch_obs1);
static DEVICE_ATTR_RO(txch_obs2);
static DEVICE_ATTR_RO(txch_obs3);
static DEVICE_ATTR_RO(txch_obs4);
static DEVICE_ATTR_RO(txch_obs5);
static DEVICE_ATTR_RO(txch_obs6);
static const struct attribute *mcdma_attrs[] = {
	&dev_attr_chan_weight.attr,
	&dev_attr_rxch_obs1.attr,
	&dev_attr_rxch_obs2.attr,
	&dev_attr_rxch_obs3.attr,
	&dev_attr_rxch_obs4.attr,
	&dev_attr_rxch_obs5.attr,
	&dev_attr_rxch_obs6.attr,
	&dev_attr_txch_obs1.attr,
	&dev_attr_txch_obs2.attr,
	&dev_attr_txch_obs3.attr,
	&dev_attr_txch_obs4.attr,
	&dev_attr_txch_obs5.attr,
	&dev_attr_txch_obs6.attr,
	NULL,
};

static const struct attribute_group mcdma_attributes = {
	.attrs = (struct attribute **)mcdma_attrs,
};
#endif

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
	int ret = 0;
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	struct device_node *np;
#endif
	struct axienet_local *lp;
	struct net_device *ndev;
	u8 mac_addr[6];
	struct resource *ethres;
	u32 value, num_queues;
	bool slave = false;

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,num-queues",
				   &num_queues);
	if (ret)
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

	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &pdev->dev;
	lp->options = XAE_OPTION_DEFAULTS;
	lp->num_queues = num_queues;
	lp->is_tsn = of_property_read_bool(pdev->dev.of_node, "xlnx,tsn");
	/* Map device registers */
	ethres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp->regs = devm_ioremap_resource(&pdev->dev, ethres);

	if (IS_ERR(lp->regs)) {
		ret = PTR_ERR(lp->regs);
		goto free_netdev;
	}

#ifdef CONFIG_XILINX_TSN
	of_property_read_u32(pdev->dev.of_node, "xlnx,num-queue", &lp->num_q);
	pr_info("Number of TSN priority queues: %d\n", lp->num_q);

	slave = of_property_read_bool(pdev->dev.of_node,
				      "xlnx,tsn-slave");
	if (slave)
		lp->temac_no = XAE_TEMAC2;
	else
		lp->temac_no = XAE_TEMAC1;
#endif

	/* Setup checksum offload, but default to off if not specified */
	lp->features = 0;

	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_node(axienet_of_match, pdev->dev.of_node);
		if (match && match->data)
			lp->axienet_config = match->data;
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

	/* The phy_type is optional but when it is not specified it should not
	 *  be a value that alters the driver behavior so set it to an invalid
	 *  value as the default.
	 */
	lp->phy_type = ~0;
	of_property_read_u32(pdev->dev.of_node, "xlnx,phy-type", &lp->phy_type);

	lp->eth_hasnobuf = of_property_read_bool(pdev->dev.of_node,
						 "xlnx,eth-hasnobuf");
	lp->eth_hasptp = of_property_read_bool(pdev->dev.of_node,
					       "xlnx,eth-hasptp");

	if ((lp->axienet_config->mactype == XAXIENET_1G) && !lp->eth_hasnobuf)
		lp->eth_irq = platform_get_irq(pdev, 0);

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

		if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
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
						XXVENET_TS_HEADER_LEN,
						GFP_KERNEL);
		}

		of_node_put(np);
	}
#endif
	if (!slave) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		ret = axienet_mcdma_probe(pdev, lp, ndev);
#else
		ret = axienet_dma_probe(pdev, ndev);
#endif
		if (ret) {
			pr_err("Getting DMA resource failed\n");
			goto free_netdev;
		}
	}

	lp->dma_clk = devm_clk_get(&pdev->dev, "dma_clk");
	if (IS_ERR(lp->dma_clk)) {
		if (PTR_ERR(lp->dma_clk) != -ENOENT) {
			ret = PTR_ERR(lp->dma_clk);
			goto free_netdev;
		}

		/* Clock framework support is optional, continue on
		 * anyways if we don't find a matching clock.
		 */
		 lp->dma_clk = NULL;
	}

	ret = clk_prepare_enable(lp->dma_clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable dma clock.\n");
		goto free_netdev;
	}

	lp->eth_clk = devm_clk_get(&pdev->dev, "ethernet_clk");
	if (IS_ERR(lp->eth_clk)) {
		if (PTR_ERR(lp->eth_clk) != -ENOENT) {
			ret = PTR_ERR(lp->eth_clk);
			goto err_disable_dmaclk;
		}

		/* Clock framework support is optional, continue on
		 * anyways if we don't find a matching clock.
		 */
		 lp->eth_clk = NULL;
	}

	ret = clk_prepare_enable(lp->eth_clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable eth clock.\n");
		goto err_disable_dmaclk;
	}

	/* Retrieve the MAC address */
	ret = of_property_read_u8_array(pdev->dev.of_node,
					"local-mac-address", mac_addr, 6);
	if (ret) {
		dev_err(&pdev->dev, "could not find MAC address\n");
		goto err_disable_ethclk;
	}
	axienet_set_mac_address(ndev, (void *)mac_addr);

	lp->coalesce_count_rx = XAXIDMA_DFT_RX_THRESHOLD;
	lp->coalesce_count_tx = XAXIDMA_DFT_TX_THRESHOLD;

	ret = of_get_phy_mode(pdev->dev.of_node);
	if (ret < 0)
		dev_warn(&pdev->dev, "couldn't find phy i/f\n");
	lp->phy_interface = ret;
	if (lp->phy_type == XAE_PHY_TYPE_1000BASE_X)
		lp->phy_flags = XAE_PHY_TYPE_1000BASE_X;

	lp->phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (lp->phy_node) {
		ret = axienet_mdio_setup(lp, pdev->dev.of_node);
		if (ret)
			dev_warn(&pdev->dev, "error registering MDIO bus\n");
	}

#ifdef CONFIG_AXIENET_HAS_MCDMA
	/* Create sysfs file entries for the device */
	ret = sysfs_create_group(&lp->dev->kobj, &mcdma_attributes);
	if (ret < 0) {
		dev_err(lp->dev, "unable to create sysfs entries\n");
		return ret;
	}
#endif

	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
		axienet_mdio_teardown(lp);
		goto err_disable_ethclk;
	}

#ifdef CONFIG_XILINX_TSN_PTP
	if (lp->is_tsn) {
		lp->ptp_rx_irq = platform_get_irq_byname(pdev, "ptp_rx");

		lp->ptp_tx_irq = platform_get_irq_byname(pdev, "ptp_tx");

		lp->qbv_irq = platform_get_irq_byname(pdev, "qbv_irq");

		pr_debug("ptp RX irq: %d\n", lp->ptp_rx_irq);
		pr_debug("ptp TX irq: %d\n", lp->ptp_tx_irq);
		pr_debug("qbv_irq: %d\n", lp->qbv_irq);

		spin_lock_init(&lp->ptp_tx_lock);

		if (lp->temac_no == XAE_TEMAC1) {
			axienet_ptp_timer_probe(
				 (lp->regs + XAE_RTC_OFFSET), pdev);

		/* enable VLAN */
		lp->options |= XAE_OPTION_VLAN;
		axienet_setoptions(lp->ndev, lp->options);
#ifdef CONFIG_XILINX_TSN_QBV
			axienet_qbv_init(ndev);
#endif
		}
	}
#endif
	return 0;

err_disable_dmaclk:
	clk_disable_unprepare(lp->dma_clk);
err_disable_ethclk:
	clk_disable_unprepare(lp->eth_clk);
free_netdev:
	free_netdev(ndev);

	return ret;
}

static int axienet_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);
	int i;

	axienet_mdio_teardown(lp);

#ifdef CONFIG_XILINX_TSN_PTP
	axienet_ptp_timer_remove(lp->timer_priv);
#ifdef CONFIG_XILINX_TSN_QBV
	axienet_qbv_remove(ndev);
#endif
#endif
	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		for_each_dma_queue(lp, i)
			netif_napi_del(&lp->napi[i]);
	}
	unregister_netdev(ndev);
	clk_disable_unprepare(lp->eth_clk);
	clk_disable_unprepare(lp->dma_clk);

#ifdef CONFIG_AXIENET_HAS_MCDMA
	sysfs_remove_group(&lp->dev->kobj, &mcdma_attributes);
#endif
	of_node_put(lp->phy_node);
	lp->phy_node = NULL;

	free_netdev(ndev);

	return 0;
}

static struct platform_driver axienet_driver = {
	.probe = axienet_probe,
	.remove = axienet_remove,
	.driver = {
		 .name = "xilinx_axienet",
		 .of_match_table = axienet_of_match,
	},
};

module_platform_driver(axienet_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL");

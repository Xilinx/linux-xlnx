// SPDX-License-Identifier: GPL-2.0

/* Xilinx AXI Ethernet (MCDMA programming)
 *
 * Copyright (c) 2008 Nissin Systems Co., Ltd.,  Yoshio Kashiwagi
 * Copyright (c) 2005-2008 DLA Systems,  David H. Lynch Jr. <dhlii@dlasys.net>
 * Copyright (c) 2008-2009 Secret Lab Technologies Ltd.
 * Copyright (c) 2010 - 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2010 - 2011 PetaLogix
 * Copyright (c) 2010 - 2012 Xilinx, Inc.
 * Copyright (C) 2018 Xilinx, Inc. All rights reserved.
 *
 * This file contains helper functions for AXI MCDMA TX and RX programming.
 */

#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_net.h>

#include "xilinx_axienet.h"

struct axienet_stat {
	const char *name;
};

#ifdef CONFIG_XILINX_TSN
/* TODO
 * The channel numbers for managemnet frames in 5 channel mcdma on EP+Switch
 * system. These are not exposed via hdf/dtsi, so need to hardcode here
 */
#define TSN_MAX_RX_Q_EPSWITCH 5
#define TSN_MGMT_CHAN0 2
#define TSN_MGMT_CHAN1 3
#endif

static struct axienet_stat axienet_get_tx_strings_stats[] = {
	{ "txq0_packets" },
	{ "txq0_bytes"   },
	{ "txq1_packets" },
	{ "txq1_bytes"   },
	{ "txq2_packets" },
	{ "txq2_bytes"   },
	{ "txq3_packets" },
	{ "txq3_bytes"   },
	{ "txq4_packets" },
	{ "txq4_bytes"   },
	{ "txq5_packets" },
	{ "txq5_bytes"   },
	{ "txq6_packets" },
	{ "txq6_bytes"   },
	{ "txq7_packets" },
	{ "txq7_bytes"   },
	{ "txq8_packets" },
	{ "txq8_bytes"   },
	{ "txq9_packets" },
	{ "txq9_bytes"   },
	{ "txq10_packets" },
	{ "txq10_bytes"   },
	{ "txq11_packets" },
	{ "txq11_bytes"   },
	{ "txq12_packets" },
	{ "txq12_bytes"   },
	{ "txq13_packets" },
	{ "txq13_bytes"   },
	{ "txq14_packets" },
	{ "txq14_bytes"   },
	{ "txq15_packets" },
	{ "txq15_bytes"   },
};

static struct axienet_stat axienet_get_rx_strings_stats[] = {
	{ "rxq0_packets" },
	{ "rxq0_bytes"   },
	{ "rxq1_packets" },
	{ "rxq1_bytes"   },
	{ "rxq2_packets" },
	{ "rxq2_bytes"   },
	{ "rxq3_packets" },
	{ "rxq3_bytes"   },
	{ "rxq4_packets" },
	{ "rxq4_bytes"   },
	{ "rxq5_packets" },
	{ "rxq5_bytes"   },
	{ "rxq6_packets" },
	{ "rxq6_bytes"   },
	{ "rxq7_packets" },
	{ "rxq7_bytes"   },
	{ "rxq8_packets" },
	{ "rxq8_bytes"   },
	{ "rxq9_packets" },
	{ "rxq9_bytes"   },
	{ "rxq10_packets" },
	{ "rxq10_bytes"   },
	{ "rxq11_packets" },
	{ "rxq11_bytes"   },
	{ "rxq12_packets" },
	{ "rxq12_bytes"   },
	{ "rxq13_packets" },
	{ "rxq13_bytes"   },
	{ "rxq14_packets" },
	{ "rxq14_bytes"   },
	{ "rxq15_packets" },
	{ "rxq15_bytes"   },
};

/**
 * axienet_mcdma_tx_bd_free - Release MCDMA Tx buffer descriptor rings
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to DMA queue structure
 *
 * This function is used to release the descriptors allocated in
 * axienet_mcdma_tx_q_init.
 */
void __maybe_unused axienet_mcdma_tx_bd_free(struct net_device *ndev,
					     struct axienet_dma_q *q)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (q->txq_bd_v) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*q->txq_bd_v) * lp->tx_bd_num,
				  q->txq_bd_v,
				  q->tx_bd_p);
	}
	if (q->tx_bufs) {
		dma_free_coherent(ndev->dev.parent,
				  XAE_MAX_PKT_LEN * lp->tx_bd_num,
				  q->tx_bufs,
				  q->tx_bufs_dma);
	}
}

/**
 * axienet_mcdma_rx_bd_free - Release MCDMA Rx buffer descriptor rings
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to DMA queue structure
 *
 * This function is used to release the descriptors allocated in
 * axienet_mcdma_rx_q_init.
 */
void __maybe_unused axienet_mcdma_rx_bd_free(struct net_device *ndev,
					     struct axienet_dma_q *q)
{
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

	if (!q->rxq_bd_v)
		return;

	for (i = 0; i < lp->rx_bd_num; i++) {
		if (q->rxq_bd_v[i].phys)
			dma_unmap_single(ndev->dev.parent, q->rxq_bd_v[i].phys,
					 lp->max_frm_size, DMA_FROM_DEVICE);
		dev_kfree_skb((struct sk_buff *)
			      (q->rxq_bd_v[i].sw_id_offset));
	}

	dma_free_coherent(ndev->dev.parent,
			  sizeof(*q->rxq_bd_v) * lp->rx_bd_num,
			  q->rxq_bd_v,
			  q->rx_bd_p);
	q->rxq_bd_v = NULL;
}

/**
 * axienet_mcdma_tx_q_init - Setup buffer descriptor rings for individual Axi
 * MCDMA-Tx
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to DMA queue structure
 *
 * Return: 0, on success -ENOMEM, on failure
 *
 * This function is helper function to axienet_dma_bd_init
 */
int __maybe_unused axienet_mcdma_tx_q_init(struct net_device *ndev,
					   struct axienet_dma_q *q)
{
	u32 cr, chan_en;
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

	q->tx_bd_ci = 0;
	q->tx_bd_tail = 0;

	q->txq_bd_v = dma_alloc_coherent(ndev->dev.parent,
					 sizeof(*q->txq_bd_v) * lp->tx_bd_num,
					 &q->tx_bd_p, GFP_KERNEL);
	if (!q->txq_bd_v)
		goto out;

	if (!q->eth_hasdre) {
		q->tx_bufs = dma_alloc_coherent(ndev->dev.parent,
						XAE_MAX_PKT_LEN * lp->tx_bd_num,
						&q->tx_bufs_dma,
						GFP_KERNEL);
		if (!q->tx_bufs)
			goto out;

		for (i = 0; i < lp->tx_bd_num; i++)
			q->tx_buf[i] = &q->tx_bufs[i * XAE_MAX_PKT_LEN];
	}

	for (i = 0; i < lp->tx_bd_num; i++) {
		q->txq_bd_v[i].next = q->tx_bd_p +
				      sizeof(*q->txq_bd_v) *
				      ((i + 1) % lp->tx_bd_num);
	}

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
	for_each_tx_dma_queue(lp, i) {
		axienet_mcdma_tx_bd_free(ndev, lp->dq[i]);
	}
	return -ENOMEM;
}

/**
 * axienet_mcdma_rx_q_init - Setup buffer descriptor rings for individual Axi
 * MCDMA-Rx
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to DMA queue structure
 *
 * Return: 0, on success -ENOMEM, on failure
 *
 * This function is helper function to axienet_dma_bd_init
 */
int __maybe_unused axienet_mcdma_rx_q_init(struct net_device *ndev,
					   struct axienet_dma_q *q)
{
	u32 cr, chan_en;
	int i;
	struct sk_buff *skb;
	struct axienet_local *lp = netdev_priv(ndev);
	dma_addr_t mapping;

	q->rx_bd_ci = 0;
	q->rx_offset = XMCDMA_CHAN_RX_OFFSET;

	q->rxq_bd_v = dma_alloc_coherent(ndev->dev.parent,
					 sizeof(*q->rxq_bd_v) * lp->rx_bd_num,
					 &q->rx_bd_p, GFP_KERNEL);
	if (!q->rxq_bd_v)
		goto out;

	for (i = 0; i < lp->rx_bd_num; i++) {
		q->rxq_bd_v[i].next = q->rx_bd_p +
				      sizeof(*q->rxq_bd_v) *
				      ((i + 1) % lp->rx_bd_num);

		skb = netdev_alloc_skb(ndev, lp->max_frm_size);
		if (!skb)
			goto out;

		/* Ensure that the skb is completely updated
		 * prio to mapping the DMA
		 */
		wmb();

		q->rxq_bd_v[i].sw_id_offset = (phys_addr_t)skb;
		mapping = dma_map_single(ndev->dev.parent,
					 skb->data,
					 lp->max_frm_size,
					 DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(ndev->dev.parent, mapping))) {
			dev_err(&ndev->dev, "mcdma map error\n");
			goto out;
		}

		q->rxq_bd_v[i].phys = mapping;
		q->rxq_bd_v[i].cntrl = lp->max_frm_size;
	}

#ifdef CONFIG_XILINX_TSN
	/* check if this is a mgmt channel */
	if (lp->num_rx_queues == TSN_MAX_RX_Q_EPSWITCH) {
		if (q->chan_id == TSN_MGMT_CHAN0)
			q->flags |= (MCDMA_MGMT_CHAN | MCDMA_MGMT_CHAN_PORT0);
		else if (q->chan_id == TSN_MGMT_CHAN1)
			q->flags |= (MCDMA_MGMT_CHAN | MCDMA_MGMT_CHAN_PORT1);
	}
#endif

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
			    (lp->rx_bd_num - 1)));
	chan_en = axienet_dma_in32(q, XMCDMA_CHEN_OFFSET + q->rx_offset);
	chan_en |= (1 << (q->chan_id - 1));
	axienet_dma_out32(q, XMCDMA_CHEN_OFFSET + q->rx_offset, chan_en);

	return 0;

out:
	for_each_rx_dma_queue(lp, i) {
		axienet_mcdma_rx_bd_free(ndev, lp->dq[i]);
	}
	return -ENOMEM;
}

static inline int get_mcdma_tx_q(struct axienet_local *lp, u32 chan_id)
{
	int i;

	for_each_tx_dma_queue(lp, i) {
		if (chan_id == lp->chan_num[i])
			return lp->qnum[i];
	}

	return -ENODEV;
}

static inline int get_mcdma_rx_q(struct axienet_local *lp, u32 chan_id)
{
	int i;

	for_each_rx_dma_queue(lp, i) {
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

irqreturn_t __maybe_unused axienet_mcdma_tx_irq(int irq, void *_ndev)
{
	u32 cr;
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	int i, j = map_dma_q_txirq(irq, lp);
	struct axienet_dma_q *q;

	if (j < 0)
		return IRQ_NONE;

	i = get_mcdma_tx_q(lp, j);
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
		dev_err(&ndev->dev, "Current BD is at: %pa\n",
			&q->txq_bd_v[q->tx_bd_ci].phys);

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

irqreturn_t __maybe_unused axienet_mcdma_rx_irq(int irq, void *_ndev)
{
	u32 cr;
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	int i, j = map_dma_q_rxirq(irq, lp);
	struct axienet_dma_q *q;

	if (j < 0)
		return IRQ_NONE;

	i = get_mcdma_rx_q(lp, j);
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
		dev_err(&ndev->dev, "Current BD is at: %pa\n",
			&q->rxq_bd_v[q->rx_bd_ci].phys);

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

void axienet_strings(struct net_device *ndev, u32 sset, u8 *data)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	int i = AXIENET_ETHTOOLS_SSTATS_LEN, j, k = 0;

	for (j = 0; i < AXIENET_TX_SSTATS_LEN(lp) + AXIENET_ETHTOOLS_SSTATS_LEN;) {
		if (j >= lp->num_tx_queues)
			break;
		q = lp->dq[j];
		if (i % 2 == 0)
			k = (q->chan_id - 1) * 2;
		if (sset == ETH_SS_STATS)
			memcpy(data + i * ETH_GSTRING_LEN,
			       axienet_get_tx_strings_stats[k].name,
			       ETH_GSTRING_LEN);
		++i;
		k++;
		if (i % 2 == 0)
			++j;
	}
	k = 0;
	for (j = 0; i < AXIENET_TX_SSTATS_LEN(lp) + AXIENET_RX_SSTATS_LEN(lp) +
			AXIENET_ETHTOOLS_SSTATS_LEN;) {
		if (j >= lp->num_rx_queues)
			break;
		q = lp->dq[j];
		if (i % 2 == 0)
			k = (q->chan_id - 1) * 2;
		if (sset == ETH_SS_STATS)
			memcpy(data + i * ETH_GSTRING_LEN,
			       axienet_get_rx_strings_stats[k].name,
			       ETH_GSTRING_LEN);
		++i;
		k++;
		if (i % 2 == 0)
			++j;
	}
}

int axienet_sset_count(struct net_device *ndev, int sset)
{
	struct axienet_local *lp = netdev_priv(ndev);

	switch (sset) {
	case ETH_SS_STATS:
		return (AXIENET_TX_SSTATS_LEN(lp) + AXIENET_RX_SSTATS_LEN(lp) +
			AXIENET_ETHTOOLS_SSTATS_LEN);
	default:
		return -EOPNOTSUPP;
	}
}

void axienet_get_stats(struct net_device *ndev,
		       struct ethtool_stats *stats,
		       u64 *data)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	unsigned int i = AXIENET_ETHTOOLS_SSTATS_LEN, j;

	for (j = 0; i < AXIENET_TX_SSTATS_LEN(lp) + AXIENET_ETHTOOLS_SSTATS_LEN;) {
		if (j >= lp->num_tx_queues)
			break;

		q = lp->dq[j];
		data[i++] = q->tx_packets;
		data[i++] = q->tx_bytes;
		++j;
	}
	for (j = 0; i < AXIENET_TX_SSTATS_LEN(lp) + AXIENET_RX_SSTATS_LEN(lp) +
			AXIENET_ETHTOOLS_SSTATS_LEN;) {
		if (j >= lp->num_rx_queues)
			break;

		q = lp->dq[j];
		data[i++] = q->rx_packets;
		data[i++] = q->rx_bytes;
		++j;
	}
}

/**
 * axienet_mcdma_err_handler - Tasklet handler for Axi MCDMA Error
 * @data:	Data passed
 *
 * Resets the Axi MCDMA and Axi Ethernet devices, and reconfigures the
 * Tx/Rx BDs.
 */
void __maybe_unused axienet_mcdma_err_handler(unsigned long data)
{
	u32 axienet_status;
	u32 cr, i, chan_en;
	struct axienet_dma_q *q = (struct axienet_dma_q *)data;
	struct axienet_local *lp = q->lp;
	struct net_device *ndev = lp->ndev;
	struct aximcdma_bd *cur_p;

	lp->axienet_config->setoptions(ndev, lp->options &
				       ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));
	__axienet_device_reset(q);

	for (i = 0; i < lp->tx_bd_num; i++) {
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

	for (i = 0; i < lp->rx_bd_num; i++) {
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
			    (lp->rx_bd_num - 1)));
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

	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC) {
		axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
		axienet_status &= ~XAE_RCW1_RX_MASK;
		axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);
	}

	if (lp->axienet_config->mactype == XAXIENET_1G && !lp->eth_hasnobuf) {
		axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
		if (axienet_status & XAE_INT_RXRJECT_MASK)
			axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G &&
	    lp->axienet_config->mactype != XAXIENET_MRMAC)
		axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (lp->axienet_config->mactype == XAXIENET_10G_25G ||
	    lp->axienet_config->mactype == XAXIENET_MRMAC) {
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

	lp->axienet_config->setoptions(ndev, lp->options &
				       ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));
	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	lp->axienet_config->setoptions(ndev, lp->options);
}

int __maybe_unused axienet_mcdma_tx_probe(struct platform_device *pdev,
					  struct device_node *np,
					  struct axienet_local *lp)
{
	int i;
	char dma_name[24];
	int ret = 0;

#ifdef CONFIG_XILINX_TSN
	u32 num = XAE_TSN_MIN_QUEUES;
	/* get number of associated queues */
	ret = of_property_read_u32(np, "xlnx,num-mm2s-channels", &num);
	if (ret)
		num = XAE_TSN_MIN_QUEUES;
	lp->num_tx_queues = num;
#endif

	for_each_tx_dma_queue(lp, i) {
		struct axienet_dma_q *q;

		q = lp->dq[i];

		q->dma_regs = lp->mcdma_regs;
		snprintf(dma_name, sizeof(dma_name), "mm2s_ch%d_introut",
			 q->chan_id);
		q->tx_irq = platform_get_irq_byname(pdev, dma_name);
#ifdef CONFIG_XILINX_TSN
		q->eth_hasdre = of_property_read_bool(np,
						      "xlnx,include-mm2s-dre");
#else
		q->eth_hasdre = of_property_read_bool(np,
						      "xlnx,include-dre");
#endif
		spin_lock_init(&q->tx_lock);
	}
	of_node_put(np);

	return 0;
}

int __maybe_unused axienet_mcdma_rx_probe(struct platform_device *pdev,
					  struct axienet_local *lp,
					  struct net_device *ndev)
{
	int i;
	char dma_name[24];

	for_each_rx_dma_queue(lp, i) {
		struct axienet_dma_q *q;

		q = lp->dq[i];

		q->dma_regs = lp->mcdma_regs;
		snprintf(dma_name, sizeof(dma_name), "s2mm_ch%d_introut",
			 q->chan_id);
		q->rx_irq = platform_get_irq_byname(pdev, dma_name);

		spin_lock_init(&q->rx_lock);

		netif_napi_add(ndev, &lp->napi[i], xaxienet_rx_poll,
			       XAXIENET_NAPI_WEIGHT);
	}

	return 0;
}

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

int axeinet_mcdma_create_sysfs(struct kobject *kobj)
{
	return sysfs_create_group(kobj, &mcdma_attributes);
}

void axeinet_mcdma_remove_sysfs(struct kobject *kobj)
{
	sysfs_remove_group(kobj, &mcdma_attributes);
}

/*
 * DMA driver for Xilinx ZynqMP DMA Engine
 *
 * Copyright (C) 2015 Xilinx, Inc. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include "../dmaengine.h"

/* Register Offsets */
#define ZYNQMP_DMA_ISR			0x100
#define ZYNQMP_DMA_IMR			0x104
#define ZYNQMP_DMA_IER			0x108
#define ZYNQMP_DMA_IDS			0x10C
#define ZYNQMP_DMA_CTRL0		0x110
#define ZYNQMP_DMA_CTRL1		0x114
#define ZYNQMP_DMA_DATA_ATTR		0x120
#define ZYNQMP_DMA_DSCR_ATTR		0x124
#define ZYNQMP_DMA_SRC_DSCR_WRD0	0x128
#define ZYNQMP_DMA_SRC_DSCR_WRD1	0x12C
#define ZYNQMP_DMA_SRC_DSCR_WRD2	0x130
#define ZYNQMP_DMA_SRC_DSCR_WRD3	0x134
#define ZYNQMP_DMA_DST_DSCR_WRD0	0x138
#define ZYNQMP_DMA_DST_DSCR_WRD1	0x13C
#define ZYNQMP_DMA_DST_DSCR_WRD2	0x140
#define ZYNQMP_DMA_DST_DSCR_WRD3	0x144
#define ZYNQMP_DMA_SRC_START_LSB	0x158
#define ZYNQMP_DMA_SRC_START_MSB	0x15C
#define ZYNQMP_DMA_DST_START_LSB	0x160
#define ZYNQMP_DMA_DST_START_MSB	0x164
#define ZYNQMP_DMA_TOTAL_BYTE		0x188
#define ZYNQMP_DMA_RATE_CTRL		0x18C
#define ZYNQMP_DMA_IRQ_SRC_ACCT		0x190
#define ZYNQMP_DMA_IRQ_DST_ACCT		0x194
#define ZYNQMP_DMA_CTRL2		0x200

/* Interrupt registers bit field definitions */
#define ZYNQMP_DMA_DONE			BIT(10)
#define ZYNQMP_DMA_AXI_WR_DATA		BIT(9)
#define ZYNQMP_DMA_AXI_RD_DATA		BIT(8)
#define ZYNQMP_DMA_AXI_RD_DST_DSCR	BIT(7)
#define ZYNQMP_DMA_AXI_RD_SRC_DSCR	BIT(6)
#define ZYNQMP_DMA_IRQ_DST_ACCT_ERR	BIT(5)
#define ZYNQMP_DMA_IRQ_SRC_ACCT_ERR	BIT(4)
#define ZYNQMP_DMA_BYTE_CNT_OVRFL	BIT(3)
#define ZYNQMP_DMA_INV_APB		BIT(0)

/* Control 0 register bit field definitions */
#define ZYNQMP_DMA_OVR_FETCH		BIT(7)
#define ZYNQMP_DMA_POINT_TYPE_SG	BIT(6)
#define ZYNQMP_DMA_RATE_CTRL_EN		BIT(3)

/* Control 1 register bit field definitions */
#define ZYNQMP_DMA_SRC_ISSUE		GENMASK(4, 0)

/* Data Attribute register bit field definitions */
#define ZYNQMP_DMA_ARBURST		GENMASK(27, 26)
#define ZYNQMP_DMA_ARCACHE		GENMASK(25, 22)
#define ZYNQMP_DMA_ARCACHE_OFST		22
#define ZYNQMP_DMA_ARQOS		GENMASK(21, 18)
#define ZYNQMP_DMA_ARQOS_OFST		18
#define ZYNQMP_DMA_ARLEN		GENMASK(17, 14)
#define ZYNQMP_DMA_ARLEN_OFST		14
#define ZYNQMP_DMA_AWBURST		GENMASK(13, 12)
#define ZYNQMP_DMA_AWCACHE		GENMASK(11, 8)
#define ZYNQMP_DMA_AWCACHE_OFST		8
#define ZYNQMP_DMA_AWQOS		GENMASK(7, 4)
#define ZYNQMP_DMA_AWQOS_OFST		4
#define ZYNQMP_DMA_AWLEN		GENMASK(3, 0)
#define ZYNQMP_DMA_AWLEN_OFST		0

/* Descriptor Attribute register bit field definitions */
#define ZYNQMP_DMA_AXCOHRNT		BIT(8)
#define ZYNQMP_DMA_AXCACHE		GENMASK(7, 4)
#define ZYNQMP_DMA_AXCACHE_OFST		4
#define ZYNQMP_DMA_AXQOS		GENMASK(3, 0)
#define ZYNQMP_DMA_AXQOS_OFST		0

/* Control register 2 bit field definitions */
#define ZYNQMP_DMA_ENABLE		BIT(0)

/* Buffer Descriptor definitions */
#define ZYNQMP_DMA_DESC_CTRL_STOP	0x10
#define ZYNQMP_DMA_DESC_CTRL_COMP_INT	0x4
#define ZYNQMP_DMA_DESC_CTRL_SIZE_256	0x2
#define ZYNQMP_DMA_DESC_CTRL_COHRNT	0x1

/* Interrupt Mask specific definitions */
#define ZYNQMP_DMA_INT_ERR	(ZYNQMP_DMA_AXI_RD_DATA | \
				ZYNQMP_DMA_AXI_WR_DATA | \
				ZYNQMP_DMA_AXI_RD_DST_DSCR | \
				ZYNQMP_DMA_AXI_RD_SRC_DSCR | \
				ZYNQMP_DMA_INV_APB)
#define ZYNQMP_DMA_INT_OVRFL	(ZYNQMP_DMA_BYTE_CNT_OVRFL | \
				ZYNQMP_DMA_IRQ_SRC_ACCT_ERR | \
				ZYNQMP_DMA_IRQ_DST_ACCT_ERR)
#define ZYNQMP_DMA_INT_DONE	ZYNQMP_DMA_DONE
#define ZYNQMP_DMA_INT_EN_DEFAULT_MASK	(ZYNQMP_DMA_INT_DONE | \
					ZYNQMP_DMA_INT_ERR | \
					ZYNQMP_DMA_INT_OVRFL)

/* Max number of descriptors per channel */
#define ZYNQMP_DMA_NUM_DESCS	32

/* Max transfer size per descriptor */
#define ZYNQMP_DMA_MAX_TRANS_LEN	0x40000000

/* Reset values for data attributes */
#define ZYNQMP_DMA_ARCACHE_RST_VAL	0x2
#define ZYNQMP_DMA_ARLEN_RST_VAL	0xF
#define ZYNQMP_DMA_AWCACHE_RST_VAL	0x2
#define ZYNQMP_DMA_AWLEN_RST_VAL	0xF

#define ZYNQMP_DMA_SRC_ISSUE_RST_VAL	0x1F

#define ZYNQMP_DMA_IDS_DEFAULT_MASK	0xFFF

/* Bus width in bits */
#define ZYNQMP_DMA_BUS_WIDTH_64		64
#define ZYNQMP_DMA_BUS_WIDTH_128	128

#define ZYNQMP_DMA_DESC_SIZE(chan)	(chan->desc_size)
#define DST_DESC_BASE(chan)	(ZYNQMP_DMA_DESC_SIZE(chan) * \
				ZYNQMP_DMA_NUM_DESCS)

#define to_chan(chan)		container_of(chan, struct zynqmp_dma_chan, \
					     common)
#define tx_to_desc(tx)		container_of(tx, struct zynqmp_dma_desc_sw, \
					     async_tx)

/**
 * struct zynqmp_dma_desc_ll - Hw linked list descriptor
 * @addr: Buffer address
 * @size: Size of the buffer
 * @ctrl: Control word
 * @nxtdscraddr: Next descriptor base address
 * @rsvd: Reserved field and for Hw internal use.
 */
struct zynqmp_dma_desc_ll {
	u64 addr;
	u32 size;
	u32 ctrl;
	u64 nxtdscraddr;
	u64 rsvd;
}; __aligned(64)

/**
 * struct zynqmp_dma_desc_sw - Per Transaction structure
 * @src: Source address for simple mode dma
 * @dst: Destination address for simple mode dma
 * @len: Transfer length for simple mode dma
 * @node: Node in the channel descriptor list
 * @tx_list: List head for the current transfer
 * @async_tx: Async transaction descriptor
 * @src_v: Virtual address of the src descriptor
 * @src_p: Physical address of the src descriptor
 * @dst_v: Virtual address of the dst descriptor
 * @dst_p: Physical address of the dst descriptor
 */
struct zynqmp_dma_desc_sw {
	u64 src;
	u64 dst;
	u32 len;
	struct list_head node;
	struct list_head tx_list;
	struct dma_async_tx_descriptor async_tx;
	struct zynqmp_dma_desc_ll *src_v;
	dma_addr_t src_p;
	struct zynqmp_dma_desc_ll *dst_v;
	dma_addr_t dst_p;
};

/**
 * struct zynqmp_dma_chan - Driver specific DMA channel structure
 * @zdev: Driver specific device structure
 * @regs: Control registers offset
 * @lock: Descriptor operation lock
 * @pending_list: Descriptors waiting
 * @free_list: Descriptors free
 * @active_list: Descriptors active
 * @sw_desc_pool: SW descriptor pool
 * @done_list: Complete descriptors
 * @common: DMA common channel
 * @desc_pool_v: Statically allocated descriptor base
 * @desc_pool_p: Physical allocated descriptor base
 * @desc_free_cnt: Descriptor available count
 * @dev: The dma device
 * @irq: Channel IRQ
 * @has_sg: Support scatter gather transfers
 * @ovrfetch: Overfetch status
 * @ratectrl: Rate control value
 * @tasklet: Cleanup work after irq
 * @src_issue: Out standing transactions on source
 * @dst_issue: Out standing transactions on destination
 * @idle : Channel status;
 * @desc_size: Size of the low level descriptor
 * @err: Channel has errors
 * @bus_width: Bus width
 * @desc_axi_cohrnt: Descriptor axi coherent status
 * @desc_axi_cache: Descriptor axi cache attribute
 * @desc_axi_qos: Descriptor axi qos attribute
 * @src_axi_cohrnt: Source data axi coherent status
 * @src_axi_cache: Source data axi cache attribute
 * @src_axi_qos: Source data axi qos attribute
 * @dst_axi_cohrnt: Dest data axi coherent status
 * @dst_axi_cache: Dest data axi cache attribute
 * @dst_axi_qos: Dest data axi qos attribute
 * @src_burst_len: Source burst length
 * @dst_burst_len: Dest burst length
 */
struct zynqmp_dma_chan {
	struct zynqmp_dma_device *zdev;
	void __iomem *regs;
	spinlock_t lock;
	struct list_head pending_list;
	struct list_head free_list;
	struct list_head active_list;
	struct zynqmp_dma_desc_sw *sw_desc_pool;
	struct list_head done_list;
	struct dma_chan common;
	void *desc_pool_v;
	dma_addr_t desc_pool_p;
	u32 desc_free_cnt;
	struct device *dev;
	int irq;
	bool has_sg;
	bool ovrfetch;
	u32 ratectrl;
	struct tasklet_struct tasklet;
	u32 src_issue;
	u32 dst_issue;
	bool idle;
	u32 desc_size;
	bool err;
	u32 bus_width;
	u32 desc_axi_cohrnt;
	u32 desc_axi_cache;
	u32 desc_axi_qos;
	u32 src_axi_cohrnt;
	u32 src_axi_cache;
	u32 src_axi_qos;
	u32 dst_axi_cohrnt;
	u32 dst_axi_cache;
	u32 dst_axi_qos;
	u32 src_burst_len;
	u32 dst_burst_len;
};

/**
 * struct zynqmp_dma_device - DMA device structure
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific DMA channel
 */
struct zynqmp_dma_device {
	struct device *dev;
	struct dma_device common;
	struct zynqmp_dma_chan *chan;
};

/**
 * zynqmp_dma_chan_is_idle - Provides the channel idle status
 * @chan: ZynqMP DMA DMA channel pointer
 *
 * Return: 'true' if the channel is idle otherwise 'false'
 */
static inline bool zynqmp_dma_chan_is_idle(struct zynqmp_dma_chan *chan)
{
	return chan->idle;

}

/**
 * zynqmp_dma_update_desc_to_ctrlr - Updates descriptor to the controller
 * @chan: ZynqMP DMA DMA channel pointer
 * @desc: Transaction descriptor pointer
 */
static void zynqmp_dma_update_desc_to_ctrlr(struct zynqmp_dma_chan *chan,
				      struct zynqmp_dma_desc_sw *desc)
{
	dma_addr_t addr;

	addr = desc->src_p;
	writel(addr, chan->regs + ZYNQMP_DMA_SRC_START_LSB);
	writel(upper_32_bits(addr), chan->regs + ZYNQMP_DMA_SRC_START_MSB);
	addr = desc->dst_p;
	writel(addr, chan->regs + ZYNQMP_DMA_DST_START_LSB);
	writel(upper_32_bits(addr), chan->regs + ZYNQMP_DMA_DST_START_MSB);
}

/**
 * zynqmp_dma_desc_config_eod - Mark the descriptor as end descriptor
 * @chan: ZynqMP DMA channel pointer
 * @desc: Hw descriptor pointer
 */
static void zynqmp_dma_desc_config_eod(struct zynqmp_dma_chan *chan, void *desc)
{
	struct zynqmp_dma_desc_ll *hw = (struct zynqmp_dma_desc_ll *)desc;

	hw->ctrl |= ZYNQMP_DMA_DESC_CTRL_STOP;
	hw++;
	hw->ctrl |= ZYNQMP_DMA_DESC_CTRL_COMP_INT | ZYNQMP_DMA_DESC_CTRL_STOP;
}

/**
 * zynqmp_dma_config_simple_desc - Configure the transfer parameters
 * @chan: ZynqMP DMA channel pointer
 * @src: Source buffer address
 * @dst: Destination buffer address
 * @len: Transfer length
 */
static void zynqmp_dma_config_simple_desc(struct zynqmp_dma_chan *chan,
					  dma_addr_t src, dma_addr_t dst,
					  size_t len)
{
	u32 val;

	writel(src, chan->regs + ZYNQMP_DMA_SRC_DSCR_WRD0);
	writel(upper_32_bits(src), chan->regs + ZYNQMP_DMA_SRC_DSCR_WRD1);
	writel(len, chan->regs + ZYNQMP_DMA_SRC_DSCR_WRD2);

	if (chan->src_axi_cohrnt)
		writel(ZYNQMP_DMA_DESC_CTRL_COHRNT,
			chan->regs + ZYNQMP_DMA_SRC_DSCR_WRD3);
	else
		writel(0, chan->regs + ZYNQMP_DMA_SRC_DSCR_WRD3);

	writel(dst, chan->regs + ZYNQMP_DMA_DST_DSCR_WRD0);
	writel(upper_32_bits(dst), chan->regs + ZYNQMP_DMA_DST_DSCR_WRD1);
	writel(len, chan->regs + ZYNQMP_DMA_DST_DSCR_WRD2);

	if (chan->dst_axi_cohrnt)
		val = ZYNQMP_DMA_DESC_CTRL_COHRNT |
				ZYNQMP_DMA_DESC_CTRL_COMP_INT;
	else
		val = ZYNQMP_DMA_DESC_CTRL_COMP_INT;
	writel(val, chan->regs + ZYNQMP_DMA_DST_DSCR_WRD3);
}

/**
 * zynqmp_dma_config_sg_ll_desc - Configure the linked list descriptor
 * @chan: ZynqMP DMA channel pointer
 * @sdesc: Hw descriptor pointer
 * @src: Source buffer address
 * @dst: Destination buffer address
 * @len: Transfer length
 * @prev: Previous hw descriptor pointer
 */
static void zynqmp_dma_config_sg_ll_desc(struct zynqmp_dma_chan *chan,
				   struct zynqmp_dma_desc_ll *sdesc,
				   dma_addr_t src, dma_addr_t dst, size_t len,
				   struct zynqmp_dma_desc_ll *prev)
{
	struct zynqmp_dma_desc_ll *ddesc = sdesc + 1;

	sdesc->size = ddesc->size = len;
	sdesc->addr = src;
	ddesc->addr = dst;

	sdesc->ctrl = ddesc->ctrl = ZYNQMP_DMA_DESC_CTRL_SIZE_256;
	if (chan->src_axi_cohrnt)
		sdesc->ctrl |= ZYNQMP_DMA_DESC_CTRL_COHRNT;
	else
		ddesc->ctrl |= ZYNQMP_DMA_DESC_CTRL_COHRNT;

	if (prev) {
		dma_addr_t addr = chan->desc_pool_p +
			    ((dma_addr_t)sdesc - (dma_addr_t)chan->desc_pool_v);
		ddesc = prev + 1;
		prev->nxtdscraddr = addr;
		ddesc->nxtdscraddr = addr + ZYNQMP_DMA_DESC_SIZE(chan);
	}
}

/**
 * zynqmp_dma_init - Initialize the channel
 * @chan: ZynqMP DMA channel pointer
 */
static void zynqmp_dma_init(struct zynqmp_dma_chan *chan)
{
	u32 val;

	writel(ZYNQMP_DMA_IDS_DEFAULT_MASK, chan->regs + ZYNQMP_DMA_IDS);
	val = readl(chan->regs + ZYNQMP_DMA_ISR);
	writel(val, chan->regs + ZYNQMP_DMA_ISR);
	writel(0x0, chan->regs + ZYNQMP_DMA_TOTAL_BYTE);

	val = readl(chan->regs + ZYNQMP_DMA_CTRL1);
	if (chan->src_issue)
		val = (val & ~ZYNQMP_DMA_SRC_ISSUE) | chan->src_issue;
	writel(val, chan->regs + ZYNQMP_DMA_CTRL1);

	val = 0;
	if (chan->ovrfetch)
		val |= ZYNQMP_DMA_OVR_FETCH;
	if (chan->has_sg)
		val |= ZYNQMP_DMA_POINT_TYPE_SG;
	if (chan->ratectrl) {
		val |= ZYNQMP_DMA_RATE_CTRL_EN;
		writel(chan->ratectrl, chan->regs + ZYNQMP_DMA_RATE_CTRL);
	}
	writel(val, chan->regs + ZYNQMP_DMA_CTRL0);

	val = 0;
	if (chan->desc_axi_cohrnt)
		val |= ZYNQMP_DMA_AXCOHRNT;
	val |= chan->desc_axi_cache;
	val = (val & ~ZYNQMP_DMA_AXCACHE) |
			(chan->desc_axi_cache << ZYNQMP_DMA_AXCACHE_OFST);
	val |= chan->desc_axi_qos;
	val = (val & ~ZYNQMP_DMA_AXQOS) |
			(chan->desc_axi_qos << ZYNQMP_DMA_AXQOS_OFST);
	writel(val, chan->regs + ZYNQMP_DMA_DSCR_ATTR);

	val = readl(chan->regs + ZYNQMP_DMA_DATA_ATTR);
	val = (val & ~ZYNQMP_DMA_ARCACHE) |
		(chan->src_axi_cache << ZYNQMP_DMA_ARCACHE_OFST);
	val = (val & ~ZYNQMP_DMA_AWCACHE) |
		(chan->dst_axi_cache << ZYNQMP_DMA_AWCACHE_OFST);
	val = (val & ~ZYNQMP_DMA_ARQOS) |
		(chan->src_axi_qos << ZYNQMP_DMA_ARQOS_OFST);
	val = (val & ~ZYNQMP_DMA_AWQOS) |
		(chan->dst_axi_qos << ZYNQMP_DMA_AWQOS_OFST);
	val = (val & ~ZYNQMP_DMA_ARLEN) |
		(chan->src_burst_len << ZYNQMP_DMA_ARLEN_OFST);
	val = (val & ~ZYNQMP_DMA_AWLEN) |
		(chan->dst_burst_len << ZYNQMP_DMA_AWLEN_OFST);
	writel(val, chan->regs + ZYNQMP_DMA_DATA_ATTR);

	/* Clearing the interrupt account rgisters */
	val = readl(chan->regs + ZYNQMP_DMA_IRQ_SRC_ACCT);
	val = readl(chan->regs + ZYNQMP_DMA_IRQ_DST_ACCT);

	chan->idle = true;
}

/**
 * zynqmp_dma_tx_submit - Submit DMA transaction
 * @tx: Async transaction descriptor pointer
 *
 * Return: cookie value
 */
static dma_cookie_t zynqmp_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct zynqmp_dma_chan *chan = to_chan(tx->chan);
	struct zynqmp_dma_desc_sw *desc = tx_to_desc(tx);
	dma_cookie_t cookie;
	unsigned long flags;

	cookie = dma_cookie_assign(tx);
	spin_lock_irqsave(&chan->lock, flags);
	list_add_tail(&desc->node, &chan->pending_list);
	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * zynqmp_dma_get_descriptor - Get the sw descriptor from the pool
 * @chan: ZynqMP DMA channel pointer
 *
 * Return: The sw descriptor
 */
static struct zynqmp_dma_desc_sw *
zynqmp_dma_get_descriptor(struct zynqmp_dma_chan *chan)
{
	struct zynqmp_dma_desc_sw *desc;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	desc = list_first_entry(&chan->free_list, struct zynqmp_dma_desc_sw,
				 node);
	list_del(&desc->node);
	spin_unlock_irqrestore(&chan->lock, flags);

	INIT_LIST_HEAD(&desc->tx_list);
	/* Clear the src and dst descriptor memory */
	if (chan->has_sg) {
		memset((void *)desc->src_v, 0, ZYNQMP_DMA_DESC_SIZE(chan));
		memset((void *)desc->dst_v, 0, ZYNQMP_DMA_DESC_SIZE(chan));
	}

	return desc;
}

/**
 * zynqmp_dma_free_descriptor - Issue pending transactions
 * @chan: ZynqMP DMA channel pointer
 * @sdesc: Transaction descriptor pointer
 */
static void zynqmp_dma_free_descriptor(struct zynqmp_dma_chan *chan,
				 struct zynqmp_dma_desc_sw *sdesc)
{
	struct zynqmp_dma_desc_sw *child, *next;

	chan->desc_free_cnt++;
	list_add_tail(&sdesc->node, &chan->free_list);
	list_for_each_entry_safe(child, next, &sdesc->tx_list, node) {
		chan->desc_free_cnt++;
		INIT_LIST_HEAD(&child->tx_list);
		list_move_tail(&child->node, &chan->free_list);
	}
	INIT_LIST_HEAD(&sdesc->tx_list);
}

/**
 * zynqmp_dma_free_desc_list - Free descriptors list
 * @chan: ZynqMP DMA channel pointer
 * @list: List to parse and delete the descriptor
 */
static void zynqmp_dma_free_desc_list(struct zynqmp_dma_chan *chan,
				      struct list_head *list)
{
	struct zynqmp_dma_desc_sw *desc, *next;

	list_for_each_entry_safe(desc, next, list, node)
		zynqmp_dma_free_descriptor(chan, desc);
}

/**
 * zynqmp_dma_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: Number of descriptors on success and failure value on error
 */
static int zynqmp_dma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct zynqmp_dma_chan *chan = to_chan(dchan);
	struct zynqmp_dma_desc_sw *desc;
	int i;

	chan->sw_desc_pool = kzalloc(sizeof(*desc) * ZYNQMP_DMA_NUM_DESCS,
				     GFP_KERNEL);
	if (!chan->sw_desc_pool)
		return -ENOMEM;
	chan->idle = true;
	chan->desc_free_cnt = ZYNQMP_DMA_NUM_DESCS;

	INIT_LIST_HEAD(&chan->free_list);

	for (i = 0; i < ZYNQMP_DMA_NUM_DESCS; i++) {
		desc = chan->sw_desc_pool + i;
		dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
		desc->async_tx.tx_submit = zynqmp_dma_tx_submit;
		list_add_tail(&desc->node, &chan->free_list);
	}

	if (!chan->has_sg)
		return 0;

	chan->desc_pool_v = dma_zalloc_coherent(chan->dev,
				(2 * chan->desc_size * ZYNQMP_DMA_NUM_DESCS),
				&chan->desc_pool_p, GFP_KERNEL);
	if (!chan->desc_pool_v)
		return -ENOMEM;

	for (i = 0; i < ZYNQMP_DMA_NUM_DESCS; i++) {
		desc = chan->sw_desc_pool + i;
		desc->src_v = (struct zynqmp_dma_desc_ll *) (chan->desc_pool_v +
					(i * ZYNQMP_DMA_DESC_SIZE(chan) * 2));
		desc->dst_v = (struct zynqmp_dma_desc_ll *) (desc->src_v + 1);
		desc->src_p = chan->desc_pool_p +
				(i * ZYNQMP_DMA_DESC_SIZE(chan) * 2);
		desc->dst_p = desc->src_p + ZYNQMP_DMA_DESC_SIZE(chan);
	}

	return ZYNQMP_DMA_NUM_DESCS;
}

/**
 * zynqmp_dma_start - Start DMA channel
 * @chan: ZynqMP DMA channel pointer
 */
static void zynqmp_dma_start(struct zynqmp_dma_chan *chan)
{
	writel(ZYNQMP_DMA_INT_EN_DEFAULT_MASK, chan->regs + ZYNQMP_DMA_IER);
	writel(0, chan->regs + ZYNQMP_DMA_TOTAL_BYTE);
	chan->idle = false;
	writel(ZYNQMP_DMA_ENABLE, chan->regs + ZYNQMP_DMA_CTRL2);
}

/**
 * zynqmp_dma_handle_ovfl_int - Process the overflow interrupt
 * @chan: ZynqMP DMA channel pointer
 * @status: Interrupt status value
 */
static void zynqmp_dma_handle_ovfl_int(struct zynqmp_dma_chan *chan, u32 status)
{
	u32 val;

	if (status & ZYNQMP_DMA_BYTE_CNT_OVRFL) {
		val = readl(chan->regs + ZYNQMP_DMA_TOTAL_BYTE);
		writel(0, chan->regs + ZYNQMP_DMA_TOTAL_BYTE);
	}
	if (status & ZYNQMP_DMA_IRQ_DST_ACCT_ERR)
		val = readl(chan->regs + ZYNQMP_DMA_IRQ_DST_ACCT);
	if (status & ZYNQMP_DMA_IRQ_SRC_ACCT_ERR)
		val = readl(chan->regs + ZYNQMP_DMA_IRQ_SRC_ACCT);
}

/**
 * zynqmp_dma_start_transfer - Initiate the new transfer
 * @chan: ZynqMP DMA channel pointer
 */
static void zynqmp_dma_start_transfer(struct zynqmp_dma_chan *chan)
{
	struct zynqmp_dma_desc_sw *desc;

	if (!zynqmp_dma_chan_is_idle(chan))
		return;

	desc = list_first_entry_or_null(&chan->pending_list,
					struct zynqmp_dma_desc_sw, node);
	if (!desc)
		return;

	if (chan->has_sg)
		list_splice_tail_init(&chan->pending_list, &chan->active_list);
	else
		list_move_tail(&desc->node, &chan->active_list);

	if (chan->has_sg)
		zynqmp_dma_update_desc_to_ctrlr(chan, desc);
	else
		zynqmp_dma_config_simple_desc(chan, desc->src, desc->dst,
					      desc->len);

	zynqmp_dma_start(chan);
}


/**
 * zynqmp_dma_chan_desc_cleanup - Cleanup the completed descriptors
 * @chan: ZynqMP DMA channel
 */
static void zynqmp_dma_chan_desc_cleanup(struct zynqmp_dma_chan *chan)
{
	struct zynqmp_dma_desc_sw *desc, *next;

	list_for_each_entry_safe(desc, next, &chan->done_list, node) {
		dma_async_tx_callback callback;
		void *callback_param;

		list_del(&desc->node);

		callback = desc->async_tx.callback;
		callback_param = desc->async_tx.callback_param;
		if (callback)
			callback(callback_param);

		/* Run any dependencies, then free the descriptor */
		zynqmp_dma_free_descriptor(chan, desc);
	}
}

/**
 * zynqmp_dma_complete_descriptor - Mark the active descriptor as complete
 * @chan: ZynqMP DMA channel pointer
 */
static void zynqmp_dma_complete_descriptor(struct zynqmp_dma_chan *chan)
{
	struct zynqmp_dma_desc_sw *desc;

	desc = list_first_entry_or_null(&chan->active_list,
					struct zynqmp_dma_desc_sw, node);
	if (!desc)
		return;
	list_del(&desc->node);
	dma_cookie_complete(&desc->async_tx);
	list_add_tail(&desc->node, &chan->done_list);
}

/**
 * zynqmp_dma_issue_pending - Issue pending transactions
 * @dchan: DMA channel pointer
 */
static void zynqmp_dma_issue_pending(struct dma_chan *dchan)
{
	struct zynqmp_dma_chan *chan = to_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	zynqmp_dma_start_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * zynqmp_dma_free_chan_resources - Free channel resources
 * @dchan: DMA channel pointer
 */
static void zynqmp_dma_free_chan_resources(struct dma_chan *dchan)
{
	struct zynqmp_dma_chan *chan = to_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	zynqmp_dma_free_desc_list(chan, &chan->active_list);
	zynqmp_dma_free_desc_list(chan, &chan->pending_list);
	zynqmp_dma_free_desc_list(chan, &chan->done_list);

	spin_unlock_irqrestore(&chan->lock, flags);
	dma_free_coherent(chan->dev,
		(2 * ZYNQMP_DMA_DESC_SIZE(chan) * ZYNQMP_DMA_NUM_DESCS),
		chan->desc_pool_v, chan->desc_pool_p);
	kfree(chan->sw_desc_pool);
}

/**
 * zynqmp_dma_tx_status - Get dma transaction status
 * @dchan: DMA channel pointer
 * @cookie: Transaction identifier
 * @txstate: Transaction state
 *
 * Return: DMA transaction status
 */
static enum dma_status zynqmp_dma_tx_status(struct dma_chan *dchan,
				      dma_cookie_t cookie,
				      struct dma_tx_state *txstate)
{
	struct zynqmp_dma_chan *chan = to_chan(dchan);
	enum dma_status ret;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret != DMA_COMPLETE)
		dma_set_residue(txstate, readl(chan->regs +
					ZYNQMP_DMA_TOTAL_BYTE));

	return ret;
}

/**
 * zynqmp_dma_reset - Reset the channel
 * @chan: ZynqMP DMA channel pointer
 */
static void zynqmp_dma_reset(struct zynqmp_dma_chan *chan)
{
	writel(ZYNQMP_DMA_IDS_DEFAULT_MASK, chan->regs + ZYNQMP_DMA_IDS);

	zynqmp_dma_complete_descriptor(chan);
	zynqmp_dma_chan_desc_cleanup(chan);

	zynqmp_dma_free_desc_list(chan, &chan->active_list);
	zynqmp_dma_free_desc_list(chan, &chan->pending_list);
	zynqmp_dma_free_desc_list(chan, &chan->done_list);

	zynqmp_dma_init(chan);
}

/**
 * zynqmp_dma_irq_handler - ZynqMP DMA Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the ZynqMP DMA channel structure
 *
 * Return: IRQ_HANDLED/IRQ_NONE
 */
static irqreturn_t zynqmp_dma_irq_handler(int irq, void *data)
{
	struct zynqmp_dma_chan *chan = (struct zynqmp_dma_chan *)data;
	u32 isr, imr, status;
	irqreturn_t ret = IRQ_NONE;

	isr = readl(chan->regs + ZYNQMP_DMA_ISR);
	imr = readl(chan->regs + ZYNQMP_DMA_IMR);
	status = isr & ~imr;

	writel(isr, chan->regs + ZYNQMP_DMA_ISR);
	if (status & ZYNQMP_DMA_INT_DONE) {
		writel(ZYNQMP_DMA_INT_DONE, chan->regs + ZYNQMP_DMA_IDS);
		spin_lock(&chan->lock);
		zynqmp_dma_complete_descriptor(chan);
		chan->idle = true;
		zynqmp_dma_start_transfer(chan);
		spin_unlock(&chan->lock);
		tasklet_schedule(&chan->tasklet);
		ret = IRQ_HANDLED;
	}

	if (status & ZYNQMP_DMA_INT_ERR) {
		chan->err = true;
		writel(ZYNQMP_DMA_INT_ERR, chan->regs + ZYNQMP_DMA_IDS);
		tasklet_schedule(&chan->tasklet);
		dev_err(chan->dev, "Channel %p has has errors\n", chan);
		ret = IRQ_HANDLED;
	}

	if (status & ZYNQMP_DMA_INT_OVRFL) {
		writel(ZYNQMP_DMA_INT_OVRFL, chan->regs + ZYNQMP_DMA_IDS);
		zynqmp_dma_handle_ovfl_int(chan, status);
		dev_dbg(chan->dev, "Channel %p overflow interrupt\n", chan);
		ret = IRQ_HANDLED;
	}

	return ret;
}

/**
 * zynqmp_dma_do_tasklet - Schedule completion tasklet
 * @data: Pointer to the ZynqMP DMA channel structure
 */
static void zynqmp_dma_do_tasklet(unsigned long data)
{
	struct zynqmp_dma_chan *chan = (struct zynqmp_dma_chan *)data;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	if (chan->err) {
		zynqmp_dma_reset(chan);
		spin_unlock_irqrestore(&chan->lock, flags);
		chan->err = false;
		return;
	}

	val = readl(chan->regs + ZYNQMP_DMA_IRQ_SRC_ACCT);
	val = readl(chan->regs + ZYNQMP_DMA_IRQ_DST_ACCT);
	zynqmp_dma_chan_desc_cleanup(chan);

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * zynqmp_dma_device_terminate_all - Aborts all transfers on a channel
 * @dchan: DMA channel pointer
 *
 * Return: Always '0'
 */
static int zynqmp_dma_device_terminate_all(struct dma_chan *dchan)
{
	struct zynqmp_dma_chan *chan = to_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	writel(ZYNQMP_DMA_IDS_DEFAULT_MASK, chan->regs + ZYNQMP_DMA_IDS);

	zynqmp_dma_free_desc_list(chan, &chan->active_list);
	zynqmp_dma_free_desc_list(chan, &chan->done_list);
	zynqmp_dma_free_desc_list(chan, &chan->pending_list);

	spin_unlock_irqrestore(&chan->lock, flags);

	return 0;
}

/**
 * zynqmp_dma_prep_memcpy - prepare descriptors for memcpy transaction
 * @dchan: DMA channel
 * @dma_dst: Destination buffer address
 * @dma_src: Source buffer address
 * @len: Transfer length
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *zynqmp_dma_prep_memcpy(
				struct dma_chan *dchan, dma_addr_t dma_dst,
				dma_addr_t dma_src, size_t len, ulong flags)
{
	struct zynqmp_dma_chan *chan;
	struct zynqmp_dma_desc_sw *new, *first = NULL;
	void *desc = NULL, *prev = NULL;
	size_t copy;
	u32 desc_cnt;
	unsigned long irqflags;

	chan = to_chan(dchan);

	if ((len > ZYNQMP_DMA_MAX_TRANS_LEN) && !chan->has_sg)
		return NULL;

	desc_cnt = DIV_ROUND_UP(len, ZYNQMP_DMA_MAX_TRANS_LEN);

	spin_lock_irqsave(&chan->lock, irqflags);
	if ((desc_cnt > chan->desc_free_cnt) && chan->has_sg) {
		spin_unlock_irqrestore(&chan->lock, irqflags);
		dev_dbg(chan->dev, "chan %p descs are not available\n", chan);
		return NULL;
	}
	chan->desc_free_cnt = chan->desc_free_cnt - desc_cnt;
	spin_unlock_irqrestore(&chan->lock, irqflags);

	do {
		/* Allocate and populate the descriptor */
		new = zynqmp_dma_get_descriptor(chan);

		copy = min_t(size_t, len, ZYNQMP_DMA_MAX_TRANS_LEN);
		if (chan->has_sg) {
			desc = (struct zynqmp_dma_desc_ll *)new->src_v;
			zynqmp_dma_config_sg_ll_desc(chan, desc, dma_src,
						     dma_dst, copy, prev);
		} else {
			new->src = dma_src;
			new->dst = dma_dst;
			new->len = len;
		}

		prev = desc;
		len -= copy;
		dma_src += copy;
		dma_dst += copy;
		if (!first)
			first = new;
		else
			list_add_tail(&new->node, &first->tx_list);
	} while (len);

	if (chan->has_sg)
		zynqmp_dma_desc_config_eod(chan, desc);

	async_tx_ack(&first->async_tx);
	first->async_tx.flags = flags;
	return &first->async_tx;
}

/**
 * zynqmp_dma_prep_slave_sg - prepare descriptors for a memory sg transaction
 * @dchan: DMA channel
 * @dst_sg: Destination scatter list
 * @dst_sg_len: Number of entries in destination scatter list
 * @src_sg: Source scatter list
 * @src_sg_len: Number of entries in source scatter list
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *zynqmp_dma_prep_sg(
			struct dma_chan *dchan, struct scatterlist *dst_sg,
			unsigned int dst_sg_len, struct scatterlist *src_sg,
			unsigned int src_sg_len, unsigned long flags)
{
	struct zynqmp_dma_desc_sw *new, *first = NULL;
	struct zynqmp_dma_chan *chan = to_chan(dchan);
	void *desc = NULL, *prev = NULL;
	size_t len, dst_avail, src_avail;
	dma_addr_t dma_dst, dma_src;
	u32 desc_cnt = 0, i;
	struct scatterlist *sg;
	unsigned long irqflags;

	if (!chan->has_sg)
		return NULL;

	for_each_sg(src_sg, sg, src_sg_len, i)
		desc_cnt += DIV_ROUND_UP(sg_dma_len(sg),
					 ZYNQMP_DMA_MAX_TRANS_LEN);

	spin_lock_irqsave(&chan->lock, irqflags);
	if (desc_cnt > chan->desc_free_cnt) {
		spin_unlock_irqrestore(&chan->lock, irqflags);
		dev_dbg(chan->dev, "chan %p descs are not available\n", chan);
		return NULL;
	}
	chan->desc_free_cnt = chan->desc_free_cnt - desc_cnt;
	spin_unlock_irqrestore(&chan->lock, irqflags);

	dst_avail = sg_dma_len(dst_sg);
	src_avail = sg_dma_len(src_sg);

	/* Run until we are out of scatterlist entries */
	while (true) {
		/* Allocate and populate the descriptor */
		new = zynqmp_dma_get_descriptor(chan);
		desc = (struct zynqmp_dma_desc_ll *)new->src_v;
		len = min_t(size_t, src_avail, dst_avail);
		len = min_t(size_t, len, ZYNQMP_DMA_MAX_TRANS_LEN);
		if (len == 0)
			goto fetch;
		dma_dst = sg_dma_address(dst_sg) + sg_dma_len(dst_sg) -
			dst_avail;
		dma_src = sg_dma_address(src_sg) + sg_dma_len(src_sg) -
			src_avail;

		zynqmp_dma_config_sg_ll_desc(chan, desc, dma_src, dma_dst,
					     len, prev);
		prev = desc;
		dst_avail -= len;
		src_avail -= len;

		if (!first)
			first = new;
		else
			list_add_tail(&new->node, &first->tx_list);
fetch:
		/* Fetch the next dst scatterlist entry */
		if (dst_avail == 0) {
			if (dst_sg_len == 0)
				break;
			dst_sg = sg_next(dst_sg);
			if (dst_sg == NULL)
				break;
			dst_sg_len--;
			dst_avail = sg_dma_len(dst_sg);
		}
		/* Fetch the next src scatterlist entry */
		if (src_avail == 0) {
			if (src_sg_len == 0)
				break;
			src_sg = sg_next(src_sg);
			if (src_sg == NULL)
				break;
			src_sg_len--;
			src_avail = sg_dma_len(src_sg);
		}
	}

	zynqmp_dma_desc_config_eod(chan, desc);
	first->async_tx.flags = flags;
	return &first->async_tx;
}

/**
 * zynqmp_dma_chan_remove - Channel remove function
 * @chan: ZynqMP DMA channel pointer
 */
static void zynqmp_dma_chan_remove(struct zynqmp_dma_chan *chan)
{
	if (!chan)
		return;

	devm_free_irq(chan->zdev->dev, chan->irq, chan);
	tasklet_kill(&chan->tasklet);
	list_del(&chan->common.device_node);
}

/**
 * zynqmp_dma_chan_probe - Per Channel Probing
 * @zdev: Driver specific device structure
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int zynqmp_dma_chan_probe(struct zynqmp_dma_device *zdev,
			   struct platform_device *pdev)
{
	struct zynqmp_dma_chan *chan;
	struct resource *res;
	struct device_node *node = pdev->dev.of_node;
	int err;

	chan = devm_kzalloc(zdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;
	chan->dev = zdev->dev;
	chan->zdev = zdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chan->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chan->regs))
		return PTR_ERR(chan->regs);

	chan->bus_width = ZYNQMP_DMA_BUS_WIDTH_64;
	chan->src_issue = ZYNQMP_DMA_SRC_ISSUE_RST_VAL;
	chan->dst_burst_len = ZYNQMP_DMA_AWLEN_RST_VAL;
	chan->src_burst_len = ZYNQMP_DMA_ARLEN_RST_VAL;
	chan->dst_axi_cache = ZYNQMP_DMA_AWCACHE_RST_VAL;
	chan->src_axi_cache = ZYNQMP_DMA_ARCACHE_RST_VAL;
	err = of_property_read_u32(node, "xlnx,bus-width", &chan->bus_width);
	if ((err < 0) && ((chan->bus_width != ZYNQMP_DMA_BUS_WIDTH_64) ||
			  (chan->bus_width != ZYNQMP_DMA_BUS_WIDTH_128))) {
		dev_err(zdev->dev, "invalid bus-width value");
		return err;
	}

	chan->has_sg = of_property_read_bool(node, "xlnx,include-sg");
	chan->ovrfetch = of_property_read_bool(node, "xlnx,overfetch");
	chan->desc_axi_cohrnt =
			of_property_read_bool(node, "xlnx,desc-axi-cohrnt");
	chan->src_axi_cohrnt =
			of_property_read_bool(node, "xlnx,src-axi-cohrnt");
	chan->dst_axi_cohrnt =
			of_property_read_bool(node, "xlnx,dst-axi-cohrnt");

	of_property_read_u32(node, "xlnx,desc-axi-qos", &chan->desc_axi_qos);
	of_property_read_u32(node, "xlnx,desc-axi-cache",
			     &chan->desc_axi_cache);
	of_property_read_u32(node, "xlnx,src-axi-qos", &chan->src_axi_qos);
	of_property_read_u32(node, "xlnx,src-axi-cache", &chan->src_axi_cache);
	of_property_read_u32(node, "xlnx,dst-axi-qos", &chan->dst_axi_qos);
	of_property_read_u32(node, "xlnx,dst-axi-cache", &chan->dst_axi_cache);
	of_property_read_u32(node, "xlnx,src-burst-len", &chan->src_burst_len);
	of_property_read_u32(node, "xlnx,dst-burst-len", &chan->dst_burst_len);
	of_property_read_u32(node, "xlnx,ratectrl", &chan->ratectrl);
	of_property_read_u32(node, "xlnx,src-issue", &chan->src_issue);

	zdev->chan = chan;
	tasklet_init(&chan->tasklet, zynqmp_dma_do_tasklet, (ulong)chan);
	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->active_list);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);
	INIT_LIST_HEAD(&chan->free_list);

	dma_cookie_init(&chan->common);
	chan->common.device = &zdev->common;
	list_add_tail(&chan->common.device_node, &zdev->common.channels);

	zynqmp_dma_init(chan);
	chan->irq = platform_get_irq(pdev, 0);
	if (chan->irq < 0)
		return -ENXIO;
	err = devm_request_irq(&pdev->dev, chan->irq, zynqmp_dma_irq_handler, 0,
			       "zynqmp-dma", chan);
	if (err)
		return err;

	chan->desc_size = sizeof(struct zynqmp_dma_desc_ll);
	chan->idle = true;
	return 0;
}

/**
 * of_zynqmp_dma_xlate - Translation function
 * @dma_spec: Pointer to DMA specifier as found in the device tree
 * @ofdma: Pointer to DMA controller data
 *
 * Return: DMA channel pointer on success and NULL on error
 */
static struct dma_chan *of_zynqmp_dma_xlate(struct of_phandle_args *dma_spec,
					    struct of_dma *ofdma)
{
	struct zynqmp_dma_device *zdev = ofdma->of_dma_data;

	return dma_get_slave_channel(&zdev->chan->common);
}

/**
 * zynqmp_dma_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int zynqmp_dma_probe(struct platform_device *pdev)
{
	struct zynqmp_dma_device *zdev;
	struct dma_device *p;
	int ret;

	zdev = devm_kzalloc(&pdev->dev, sizeof(*zdev), GFP_KERNEL);
	if (!zdev)
		return -ENOMEM;

	zdev->dev = &pdev->dev;
	INIT_LIST_HEAD(&zdev->common.channels);

	dma_set_mask(&pdev->dev, DMA_BIT_MASK(44));
	dma_cap_set(DMA_SG, zdev->common.cap_mask);
	dma_cap_set(DMA_MEMCPY, zdev->common.cap_mask);

	p = &zdev->common;
	p->device_prep_dma_sg = zynqmp_dma_prep_sg;
	p->device_prep_dma_memcpy = zynqmp_dma_prep_memcpy;
	p->device_terminate_all = zynqmp_dma_device_terminate_all;
	p->device_issue_pending = zynqmp_dma_issue_pending;
	p->device_alloc_chan_resources = zynqmp_dma_alloc_chan_resources;
	p->device_free_chan_resources = zynqmp_dma_free_chan_resources;
	p->device_tx_status = zynqmp_dma_tx_status;
	p->dev = &pdev->dev;

	platform_set_drvdata(pdev, zdev);

	ret = zynqmp_dma_chan_probe(zdev, pdev);
	if (ret) {
		dev_err(&pdev->dev, "Probing channel failed\n");
		goto free_chan_resources;
	}

	p->dst_addr_widths = zdev->chan->bus_width / 8;
	p->src_addr_widths = zdev->chan->bus_width / 8;

	dma_async_device_register(&zdev->common);

	ret = of_dma_controller_register(pdev->dev.of_node,
					 of_zynqmp_dma_xlate, zdev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to register DMA to DT\n");
		dma_async_device_unregister(&zdev->common);
		goto free_chan_resources;
	}

	dev_info(&pdev->dev, "ZynqMP DMA driver Probe success\n");

	return 0;

free_chan_resources:
	zynqmp_dma_chan_remove(zdev->chan);
	return ret;
}

/**
 * zynqmp_dma_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: Always '0'
 */
static int zynqmp_dma_remove(struct platform_device *pdev)
{
	struct zynqmp_dma_device *zdev = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&zdev->common);

	zynqmp_dma_chan_remove(zdev->chan);

	return 0;
}

static const struct of_device_id zynqmp_dma_of_match[] = {
	{ .compatible = "xlnx,zynqmp-dma-1.0", },
	{}
};
MODULE_DEVICE_TABLE(of, zynqmp_dma_of_match);

static struct platform_driver zynqmp_dma_driver = {
	.driver = {
		.name = "xilinx-zynqmp-dma",
		.of_match_table = zynqmp_dma_of_match,
	},
	.probe = zynqmp_dma_probe,
	.remove = zynqmp_dma_remove,
};

module_platform_driver(zynqmp_dma_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx ZynqMP DMA DMA driver");
MODULE_LICENSE("GPL");

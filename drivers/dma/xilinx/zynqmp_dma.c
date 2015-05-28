/*
 * DMA driver for Xilinx ZMA Engine
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
#define ISR				0x100
#define IMR				0x104
#define IER				0x108
#define IDS				0x10C
#define CTRL0				0x110
#define CTRL1				0x114
#define DATA_ATTR			0x120
#define DSCR_ATTR			0x124
#define SRC_DSCR_WRD0			0x128
#define SRC_DSCR_WRD1			0x12C
#define SRC_DSCR_WRD2			0x130
#define SRC_DSCR_WRD3			0x134
#define DST_DSCR_WRD0			0x138
#define DST_DSCR_WRD1			0x13C
#define DST_DSCR_WRD2			0x140
#define DST_DSCR_WRD3			0x144
#define SRC_START_LSB			0x158
#define SRC_START_MSB			0x15C
#define DST_START_LSB			0x160
#define DST_START_MSB			0x164
#define TOTAL_BYTE			0x188
#define RATE_CTRL			0x18C
#define IRQ_SRC_ACCT			0x190
#define IRQ_DST_ACCT			0x194
#define CTRL2				0x200

/* Interrupt registers bit field definitions */
#define DMA_DONE			BIT(10)
#define AXI_WR_DATA			BIT(9)
#define AXI_RD_DATA			BIT(8)
#define AXI_RD_DST_DSCR			BIT(7)
#define AXI_RD_SRC_DSCR			BIT(6)
#define IRQ_DST_ACCT_ERR		BIT(5)
#define IRQ_SRC_ACCT_ERR		BIT(4)
#define BYTE_CNT_OVRFL			BIT(3)
#define INV_APB				BIT(0)

/* Control 0 register bit field definitions */
#define OVR_FETCH			BIT(7)
#define POINT_TYPE_SG			BIT(6)
#define RATE_CTRL_EN			BIT(3)

/* Control 1 register bit field definitions */
#define SRC_ISSUE			GENMASK(4, 0)

/* Data Attribute register bit field definitions */
#define ARBURST				GENMASK(27, 26)
#define ARCACHE				GENMASK(25, 22)
#define ARCACHE_OFST			22
#define ARQOS				GENMASK(21, 18)
#define ARQOS_OFST			18
#define ARLEN				GENMASK(17, 14)
#define ARLEN_OFST			14
#define AWBURST				GENMASK(13, 12)
#define AWCACHE				GENMASK(11, 8)
#define AWCACHE_OFST			8
#define AWQOS				GENMASK(7, 4)
#define AWQOS_OFST			4
#define AWLEN				GENMASK(3, 0)
#define AWLEN_OFST			0

/* Descriptor Attribute register bit field definitions */
#define AXCOHRNT			BIT(8)
#define AXCACHE				GENMASK(7, 4)
#define AXCACHE_OFST			4
#define AXQOS				GENMASK(3, 0)
#define AXQOS_OFST			0

/* Control register 2 bit field definitions */
#define ENABLE				BIT(0)

/* Buffer Descriptor definitions */
#define DESC_CTRL_STOP			0x10
#define DESC_CTRL_COMP_INT		0x4
#define DESC_CTRL_SIZE_256		0x2
#define DESC_CTRL_COHRNT		0x1

/* Interrupt Mask specific definitions */
#define INT_ERR		(AXI_RD_DATA | AXI_WR_DATA | AXI_RD_DST_DSCR | \
			  AXI_RD_SRC_DSCR | INV_APB)
#define INT_OVRFL	(BYTE_CNT_OVRFL | IRQ_SRC_ACCT_ERR | IRQ_DST_ACCT_ERR)
#define INT_DONE	DMA_DONE
#define INT_EN_DEFAULT_MASK	(INT_DONE | INT_ERR | INT_OVRFL)

/* Max number of descriptors per channel */
#define ZDMA_NUM_DESCS		32

/* Max transfer size per descriptor */
#define ZDMA_MAX_TRANS_LEN	0x40000000

/* Reset values for data attributes */
#define ARCACHE_RST_VAL		0x2
#define ARLEN_RST_VAL		0xF
#define AWCACHE_RST_VAL		0x2
#define AWLEN_RST_VAL		0xF

#define SRC_ISSUE_RST_VAL	0x1F

#define IDS_DEFAULT_MASK	0xFFF

#define DESC_SIZE(chan)		(chan->desc_size)
#define DST_DESC_BASE(chan)	(DESC_SIZE(chan) * ZDMA_NUM_DESCS)

#define to_chan(chan)		container_of(chan, struct zdma_chan, common)
#define tx_to_desc(tx)		container_of(tx, struct zdma_desc_sw, async_tx)

/**
 * struct zdma_desc_ll - Hw linked list descriptor
 * @addr: Buffer address
 * @size: Size of the buffer
 * @ctrl: Control word
 * @nxtdscraddr: Next descriptor base address
 * @rsvd: Reserved field and for Hw internal use.
 */
struct zdma_desc_ll {
	u64 addr;
	u32 size;
	u32 ctrl;
	u64 nxtdscraddr;
	u64 rsvd;
}; __aligned(64)

/**
 * struct zdma_desc_sw - Per Transaction structure
 * @cnt: Descriptor count required for this transfer
 * @index: Dma pool index of the first desc
 * @src: Source address for simple mode dma
 * @dst: Destination address for simple mode dma
 * @len: Transfer length for simple mode dma
 * @node: Node in the channel descriptor list
 * @async_tx: Async transaction descriptor
 * @direction: Transfer direction
 */
struct zdma_desc_sw {
	u32 cnt;
	u32 index;
	u64 src;
	u64 dst;
	u32 len;
	struct list_head node;
	struct dma_async_tx_descriptor async_tx;
	enum dma_transfer_direction direction;
};

/**
 * struct zdma_chan - Driver specific DMA channel structure
 * @xdev: Driver specific device structure
 * @regs: Control registers offset
 * @lock: Descriptor operation lock
 * @pending_list: Descriptors waiting
 * @active_desc: Active descriptor
 * @done_list: Complete descriptors
 * @common: DMA common channel
 * @desc_pool_v: Statically allocated descriptor base
 * @desc_pool_p: Physical allocated descriptor base
 * @desc_tail: Current descriptor available index
 * @desc_free_cnt: Descriptor available count
 * @dev: The dma device
 * @irq: Channel IRQ
 * @id: Channel ID
 * @has_sg: Support scatter gather transfers
 * @ovrfetch: Overfetch status
 * @ratectrl: Rate control value
 * @tasklet: Cleanup work after irq
 * @src_issue: Out standing transactions on source
 * @dst_issue: Out standing transactions on destination
 * @name: Name of the channel
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
struct zdma_chan {
	struct zdma_device *xdev;
	void __iomem *regs;
	spinlock_t lock;
	struct list_head pending_list;
	struct zdma_desc_sw *active_desc;
	struct list_head done_list;
	struct dma_chan common;
	void *desc_pool_v;
	dma_addr_t desc_pool_p;
	u32 desc_tail;
	u32 desc_free_cnt;
	struct device *dev;
	int irq;
	int id;
	bool has_sg;
	bool ovrfetch;
	u32 ratectrl;
	struct tasklet_struct tasklet;
	u32 src_issue;
	u32 dst_issue;
	char name[40];
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
 * struct zdma_device - DMA device structure
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific DMA channel
 */
struct zdma_device {
	struct device *dev;
	struct dma_device common;
	struct zdma_chan *chan;
};

/**
 * zdma_update_desc_to_ctrlr - Updates descriptor to the controller
 * @chan: ZDMA channel pointer
 * @desc: Transaction descriptor pointer
 */
static void zdma_update_desc_to_ctrlr(struct zdma_chan *chan,
				      struct zdma_desc_sw *desc)
{
	dma_addr_t addr;

	addr = chan->desc_pool_p  + (desc->index * DESC_SIZE(chan));
	writel(addr, chan->regs + SRC_START_LSB);
	writel((addr >> 32), chan->regs + SRC_START_MSB);
	addr = addr + (DESC_SIZE(chan) * ZDMA_NUM_DESCS);
	writel(addr, chan->regs + DST_START_LSB);
	writel((addr >> 32), chan->regs + DST_START_MSB);
}

/**
 * zdma_desc_config_eod - Mark the descriptor as end descriptor
 * @chan: ZDMA channel pointer
 * @desc: Hw descriptor pointer
 */
static void zdma_desc_config_eod(struct zdma_chan *chan, void *desc)
{
	struct zdma_desc_ll *hw = (struct zdma_desc_ll *)desc;

	hw->ctrl |= DESC_CTRL_STOP;
	hw += ZDMA_NUM_DESCS;
	hw->ctrl |= DESC_CTRL_COMP_INT | DESC_CTRL_STOP;
}

/**
 * zdma_config_simple_desc - Configure the transfer params to channel registers
 * @chan: ZDMA channel pointer
 * @src: Source buffer address
 * @dst: Destination buffer address
 * @len: Transfer length
 */
static void zdma_config_simple_desc(struct zdma_chan *chan, dma_addr_t src,
				    dma_addr_t dst, size_t len)
{
	u32 val;

	writel(src, chan->regs + SRC_DSCR_WRD0);
	writel(src >> 32, chan->regs + SRC_DSCR_WRD1);
	writel(len, chan->regs + SRC_DSCR_WRD2);

	if (chan->src_axi_cohrnt)
		writel(DESC_CTRL_COHRNT, chan->regs + SRC_DSCR_WRD3);
	else
		writel(0, chan->regs + SRC_DSCR_WRD3);

	writel(dst, chan->regs + DST_DSCR_WRD0);
	writel(dst >> 32, chan->regs + DST_DSCR_WRD1);
	writel(len, chan->regs + DST_DSCR_WRD2);

	if (chan->dst_axi_cohrnt)
		val = DESC_CTRL_COHRNT | DESC_CTRL_COMP_INT;
	else
		val = DESC_CTRL_COMP_INT;
	writel(val, chan->regs + DST_DSCR_WRD3);
}

/**
 * zdma_config_sg_ll_desc - Configure the linked list descriptor
 * @chan: ZDMA channel pointer
 * @sdesc: Hw descriptor pointer
 * @src: Source buffer address
 * @dst: Destination buffer address
 * @len: Transfer length
 * @prev: Previous hw descriptor pointer
 */
static void zdma_config_sg_ll_desc(struct zdma_chan *chan,
				   struct zdma_desc_ll *sdesc, dma_addr_t src,
				   dma_addr_t dst, size_t len,
				   struct zdma_desc_ll *prev)
{
	struct zdma_desc_ll *ddesc = sdesc + ZDMA_NUM_DESCS;

	sdesc->size = ddesc->size = len;
	sdesc->addr = src;
	ddesc->addr = dst;

	sdesc->ctrl = ddesc->ctrl = DESC_CTRL_SIZE_256;
	if (chan->src_axi_cohrnt)
		sdesc->ctrl |= DESC_CTRL_COHRNT;
	else
		ddesc->ctrl |= DESC_CTRL_COHRNT;

	if (prev) {
		dma_addr_t addr = chan->desc_pool_p +
				  ((u64)sdesc - (u64)chan->desc_pool_v);
		ddesc = prev + ZDMA_NUM_DESCS;
		prev->nxtdscraddr = addr;
		ddesc->nxtdscraddr = addr + DST_DESC_BASE(chan);
	}
}

/**
 * zdma_init - Initialize the channel
 * @chan: ZDMA channel pointer
 */
static void zdma_init(struct zdma_chan *chan)
{
	u32 val;

	writel(IDS_DEFAULT_MASK, chan->regs + IDS);
	val = readl(chan->regs + ISR);
	writel(val, chan->regs + ISR);
	writel(0x0, chan->regs + TOTAL_BYTE);

	val = readl(chan->regs + CTRL1);
	if (chan->src_issue)
		val = (val & ~SRC_ISSUE) | chan->src_issue;
	writel(val, chan->regs + CTRL1);

	val = 0;
	if (chan->ovrfetch)
		val |= OVR_FETCH;
	if (chan->has_sg)
		val |= POINT_TYPE_SG;
	if (chan->ratectrl) {
		val |= RATE_CTRL_EN;
		writel(chan->ratectrl, chan->regs + RATE_CTRL);
	}
	writel(val, chan->regs + CTRL0);

	val = 0;
	if (chan->desc_axi_cohrnt)
		val |= AXCOHRNT;
	val |= chan->desc_axi_cache;
	val =  (val & ~AXCACHE) | (chan->desc_axi_cache << AXCACHE_OFST);
	val |= chan->desc_axi_qos;
	val =  (val & ~AXQOS) | (chan->desc_axi_qos << AXQOS_OFST);
	writel(val, chan->regs + DSCR_ATTR);

	val = readl(chan->regs + DATA_ATTR);
	val = (val & ~ARCACHE) | (chan->src_axi_cache << ARCACHE_OFST);
	val = (val & ~AWCACHE) | (chan->dst_axi_cache << AWCACHE_OFST);
	val = (val & ~ARQOS) | (chan->src_axi_qos << ARQOS_OFST);
	val = (val & ~AWQOS) | (chan->dst_axi_qos << AWQOS_OFST);
	val = (val & ~ARLEN) | (chan->src_burst_len << ARLEN_OFST);
	val = (val & ~AWLEN) | (chan->dst_burst_len << AWLEN_OFST);
	writel(val, chan->regs + DATA_ATTR);

	/* Clearing the interrupt account rgisters */
	val = readl(chan->regs + IRQ_SRC_ACCT);
	val = readl(chan->regs + IRQ_DST_ACCT);

	chan->idle = true;
}

/**
 * zdma_tx_submit - Submit DMA transaction
 * @tx: Async transaction descriptor pointer
 *
 * Return: cookie value
 */
static dma_cookie_t zdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct zdma_chan *chan = to_chan(tx->chan);
	struct zdma_desc_sw *desc = tx_to_desc(tx);
	dma_cookie_t cookie;
	unsigned long flags;

	cookie = dma_cookie_assign(tx);
	spin_lock_irqsave(&chan->lock, flags);
	list_add_tail(&desc->node, &chan->pending_list);
	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * zdma_alloc_tx_descriptor - Allocate transaction descriptor
 * @chan: ZDMA channel pointer
 *
 * Return: The allocated descriptor on success and NULL on failure
 */
static struct zdma_desc_sw *zdma_alloc_tx_descriptor(struct zdma_chan *chan)
{
	struct zdma_desc_sw *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = zdma_tx_submit;
	desc->async_tx.cookie = 0;
	desc->cnt = 0;
	async_tx_ack(&desc->async_tx);

	desc->async_tx.cookie = -EBUSY;

	return desc;
}

/**
 * zdma_get_descriptor - Allocates the hw descriptor
 * @chan: ZDMA channel pointer
 * @sdesc: Transaction descriptor pointer
 *
 * Return: The hw descriptor and NULL for simple dma mode
 */
static void *zdma_get_descriptor(struct zdma_chan *chan,
				 struct zdma_desc_sw *sdesc)
{
	u32 size;
	void *mem;
	unsigned long flags;

	if (!chan->has_sg)
		return NULL;

	size = DESC_SIZE(chan);
	spin_lock_irqsave(&chan->lock, flags);
	mem = chan->desc_pool_v + (chan->desc_tail * size);
	if (!sdesc->cnt)
		sdesc->index = chan->desc_tail;
	chan->desc_tail =  (chan->desc_tail + 1) % ZDMA_NUM_DESCS;
	spin_unlock_irqrestore(&chan->lock, flags);
	/* Clear the src and dst descriptor memory */
	memset(mem, 0, DESC_SIZE(chan));
	memset(mem + DST_DESC_BASE(chan), 0, DESC_SIZE(chan));
	sdesc->cnt = sdesc->cnt + 1;
	return mem;
}

/**
 * zdma_free_descriptor - Issue pending transactions
 * @chan: ZDMA channel pointer
 * @sdesc: Transaction descriptor pointer
 */
static void zdma_free_descriptor(struct zdma_chan *chan,
				 struct zdma_desc_sw *sdesc)
{
	if (!chan->has_sg)
		return;

	chan->desc_free_cnt += sdesc->cnt;
}

/**
 * zdma_free_desc_list - Free descriptors list
 * @chan: ZDMA channel pointer
 * @list: List to parse and delete the descriptor
 */
static void zdma_free_desc_list(struct zdma_chan *chan, struct list_head *list)
{
	struct zdma_desc_sw *desc, *next;

	list_for_each_entry_safe(desc, next, list, node)
		zdma_free_descriptor(chan, desc);
}

/**
 * zdma_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int zdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct zdma_chan *chan = to_chan(dchan);

	if (!chan->has_sg)
		return 0;

	chan->desc_pool_v = dma_zalloc_coherent(chan->dev,
					 (2 * chan->desc_size * ZDMA_NUM_DESCS),
					 &chan->desc_pool_p, GFP_KERNEL);
	if (!chan->desc_pool_v)
		return -ENOMEM;

	chan->desc_free_cnt = ZDMA_NUM_DESCS;
	chan->desc_tail = 0;

	return 0;
}

/**
 * zdma_start - Start DMA channel
 * @chan: ZDMA channel pointer
 */
static void zdma_start(struct zdma_chan *chan)
{
	writel(INT_EN_DEFAULT_MASK, chan->regs + IER);
	writel(0, chan->regs + TOTAL_BYTE);
	writel(ENABLE, chan->regs + CTRL2);
}

/**
 * zdma_device_slave_caps - Slave channel capabilities
 * @dchan: DMA channel pointer
 * @caps: Slave capabilities to set
 *
 * Return: Always '0'
 */
static int zdma_device_slave_caps(struct dma_chan *dchan,
				  struct dma_slave_caps *caps)
{
	struct zdma_chan *chan = to_chan(dchan);

	caps->src_addr_widths = chan->bus_width;
	caps->dstn_addr_widths = chan->bus_width;
	caps->cmd_pause = false;
	caps->cmd_terminate = false;
	caps->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	return 0;
}

/**
 * zdma_handle_ovfl_int - Process the overflow interrupt
 * @chan: ZDMA channel pointer
 * @status: Interrupt status value
 */
static void zdma_handle_ovfl_int(struct zdma_chan *chan, u32 status)
{
	u32 val;

	if (status & BYTE_CNT_OVRFL) {
		val = readl(chan->regs + TOTAL_BYTE);
		writel(0, chan->regs + TOTAL_BYTE);
	}
	if (status & IRQ_DST_ACCT_ERR)
		val = readl(chan->regs + IRQ_DST_ACCT);
	if (status & IRQ_SRC_ACCT_ERR)
		val = readl(chan->regs + IRQ_SRC_ACCT);
}

/**
 * zdma_start_transfer - Initiate the new transfer
 * @chan: ZDMA channel pointer
 */
void zdma_start_transfer(struct zdma_chan *chan)
{
	struct zdma_desc_sw *desc;

	if (list_empty(&chan->pending_list))
		return;

	if (!chan->idle)
		return;

	desc = list_first_entry(&chan->pending_list,
				struct zdma_desc_sw, node);
	list_del(&desc->node);
	chan->idle = false;
	chan->active_desc = desc;
	if (chan->has_sg)
		zdma_update_desc_to_ctrlr(chan, desc);
	else
		zdma_config_simple_desc(chan, desc->src, desc->dst, desc->len);

	zdma_start(chan);
}


/**
 * zdma_chan_desc_cleanup - Cleanup the completed descriptors
 * @chan: ZDMA channel
 */
static void zdma_chan_desc_cleanup(struct zdma_chan *chan)
{
	struct zdma_desc_sw *desc, *next;

	list_for_each_entry_safe(desc, next, &chan->done_list, node) {
		dma_async_tx_callback callback;
		void *callback_param;

		list_del(&desc->node);

		callback = desc->async_tx.callback;
		callback_param = desc->async_tx.callback_param;
		if (callback)
			callback(callback_param);

		/* Run any dependencies, then free the descriptor */
		dma_run_dependencies(&desc->async_tx);
		zdma_free_descriptor(chan, desc);
		kfree(desc);
	}
}

/**
 * zdma_complete_descriptor - Mark the active descriptor as complete
 * @chan: ZDMA channel pointer
 */
void zdma_complete_descriptor(struct zdma_chan *chan)
{
	struct zdma_desc_sw *desc = chan->active_desc;

	if (!desc)
		return;

	dma_cookie_complete(&desc->async_tx);
	list_add_tail(&desc->node, &chan->done_list);

	chan->active_desc = NULL;
}

/**
 * zdma_issue_pending - Issue pending transactions
 * @dchan: DMA channel pointer
 */
static void zdma_issue_pending(struct dma_chan *dchan)
{
	struct zdma_chan *chan = to_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	zdma_start_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * zdma_free_chan_resources - Free channel resources
 * @dchan: DMA channel pointer
 */
static void zdma_free_chan_resources(struct dma_chan *dchan)
{
	struct zdma_chan *chan = to_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	zdma_free_desc_list(chan, &chan->pending_list);
	zdma_free_desc_list(chan, &chan->done_list);
	kfree(chan->active_desc);
	chan->active_desc = NULL;

	spin_unlock_irqrestore(&chan->lock, flags);
	dma_free_coherent(chan->dev, (DESC_SIZE(chan) * ZDMA_NUM_DESCS),
			  chan->desc_pool_v, chan->desc_pool_p);
}

/**
 * zdma_tx_status - Get dma transaction status
 * @dchan: DMA channel pointer
 * @cookie: Transaction identifier
 * @txstate: Transaction state
 *
 * Return: DMA transaction status
 */
static enum dma_status zdma_tx_status(struct dma_chan *dchan,
				      dma_cookie_t cookie,
				      struct dma_tx_state *txstate)
{
	struct zdma_chan *chan = to_chan(dchan);
	enum dma_status ret;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret != DMA_COMPLETE)
		dma_set_residue(txstate, readl(chan->regs + TOTAL_BYTE));

	return ret;
}

/**
 * zdma_reset - Reset the channel
 * @chan: ZDMA channel pointer
 */
static void zdma_reset(struct zdma_chan *chan)
{
	writel(IDS_DEFAULT_MASK, chan->regs + IDS);

	zdma_complete_descriptor(chan);
	zdma_chan_desc_cleanup(chan);

	zdma_free_desc_list(chan, &chan->pending_list);
	zdma_free_desc_list(chan, &chan->done_list);
	kfree(chan->active_desc);
	chan->active_desc = NULL;
	zdma_init(chan);
}

/**
 * zdma_irq_handler - ZDMA Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the ZDMA channel structure
 *
 * Return: IRQ_HANDLED/IRQ_NONE
 */
static irqreturn_t zdma_irq_handler(int irq, void *data)
{
	struct zdma_chan *chan = (struct zdma_chan *)data;
	u32 isr, imr, status;

	isr = readl(chan->regs + ISR);
	imr = readl(chan->regs + IMR);
	status = isr & ~imr;

	writel(isr, chan->regs+ISR);
	if (status & INT_DONE) {
		writel(INT_DONE, chan->regs + IDS);
		spin_lock(&chan->lock);
		zdma_complete_descriptor(chan);
		chan->idle = true;
		zdma_start_transfer(chan);
		spin_unlock(&chan->lock);
		tasklet_schedule(&chan->tasklet);
		return IRQ_HANDLED;
	}

	if (status & INT_ERR) {
		chan->err = true;
		writel(INT_ERR, chan->regs + IDS);
		tasklet_schedule(&chan->tasklet);
		dev_err(chan->dev, "Channel %p has has errors\n", chan);
		return IRQ_HANDLED;
	}

	if (status & INT_OVRFL) {
		writel(INT_OVRFL, chan->regs + IDS);
		zdma_handle_ovfl_int(chan, status);
		dev_dbg(chan->dev, "Channel %p overflow interrupt\n", chan);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/**
 * zdma_do_tasklet - Schedule completion tasklet
 * @data: Pointer to the ZDMA channel structure
 */
static void zdma_do_tasklet(unsigned long data)
{
	struct zdma_chan *chan = (struct zdma_chan *)data;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	if (chan->err) {
		zdma_reset(chan);
		spin_unlock_irqrestore(&chan->lock, flags);
		chan->err = false;
		return;
	}

	val = readl(chan->regs + IRQ_SRC_ACCT);
	val = readl(chan->regs + IRQ_DST_ACCT);
	zdma_chan_desc_cleanup(chan);

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * zdma_device_control - Configure DMA channel of the device
 * @dchan: DMA channel pointer
 * @cmd: DMA control command
 * @arg: Channel configuration
 *
 * Return: '0' on success and failure value on error
 */
static int zdma_device_control(struct dma_chan *dchan,
			       enum dma_ctrl_cmd cmd, unsigned long arg)
{
	struct zdma_chan *chan = to_chan(dchan);
	unsigned long flags;

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		spin_lock_irqsave(&chan->lock, flags);
		zdma_reset(chan);
		spin_unlock_irqrestore(&chan->lock, flags);
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

/**
 * zdma_prep_memcpy - prepare descriptors for memcpy transaction
 * @dchan: DMA channel
 * @dma_dst: Destination buffer address
 * @dma_src: Source buffer address
 * @len: Transfer length
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *zdma_prep_memcpy(struct dma_chan *dchan,
			dma_addr_t dma_dst, dma_addr_t dma_src, size_t len,
			unsigned long flags)
{
	struct zdma_chan *chan;
	struct zdma_desc_sw *new;
	void *desc, *prev = NULL;
	size_t copy;
	u32 desc_cnt;
	unsigned long irqflags;

	chan = to_chan(dchan);

	if ((len > ZDMA_MAX_TRANS_LEN) && !chan->has_sg)
		return NULL;

	desc_cnt = DIV_ROUND_UP(len, ZDMA_MAX_TRANS_LEN);

	spin_lock_irqsave(&chan->lock, irqflags);
	if ((desc_cnt > chan->desc_free_cnt) && chan->has_sg) {
		spin_unlock_irqrestore(&chan->lock, irqflags);
		dev_dbg(chan->dev, "chan %p descs are not available\n", chan);
		return NULL;
	}
	chan->desc_free_cnt = chan->desc_free_cnt - desc_cnt;
	spin_unlock_irqrestore(&chan->lock, irqflags);

	new = zdma_alloc_tx_descriptor(chan);
	if (!new)
		return NULL;

	do {
		/* Allocate and populate the descriptor */
		desc = zdma_get_descriptor(chan, new);

		copy = min_t(size_t, len, ZDMA_MAX_TRANS_LEN);
		if (chan->has_sg) {
			zdma_config_sg_ll_desc(chan, desc, dma_src, dma_dst,
						copy, prev);
		} else {
			new->src = dma_src;
			new->dst = dma_dst;
			new->len = len;
		}

		prev = desc;
		len -= copy;
		dma_src += copy;
		dma_dst += copy;

	} while (len);

	if (chan->has_sg)
		zdma_desc_config_eod(chan, desc);

	new->async_tx.flags = flags;
	return &new->async_tx;
}

/**
 * zdma_prep_slave_sg - prepare descriptors for a memory sg transaction
 * @dchan: DMA channel
 * @dst_sg: Destination scatter list
 * @dst_sg_len: Number of entries in destination scatter list
 * @src_sg: Source scatter list
 * @src_sg_len: Number of entries in source scatter list
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *zdma_prep_sg(struct dma_chan *dchan,
			struct scatterlist *dst_sg, unsigned int dst_sg_len,
			struct scatterlist *src_sg, unsigned int src_sg_len,
			unsigned long flags)
{
	struct zdma_desc_sw *new;
	struct zdma_chan *chan = to_chan(dchan);
	void *desc = NULL, *prev = NULL;
	size_t len, dst_avail, src_avail;
	dma_addr_t dma_dst, dma_src;
	u32 desc_cnt = 0, i;
	struct scatterlist *sg;
	unsigned long irqflags;

	if (!chan->has_sg)
		return NULL;

	for_each_sg(src_sg, sg, src_sg_len, i)
		desc_cnt += DIV_ROUND_UP(sg_dma_len(sg), ZDMA_MAX_TRANS_LEN);

	spin_lock_irqsave(&chan->lock, irqflags);
	if ((desc_cnt > chan->desc_free_cnt) && chan->has_sg) {
		spin_unlock_irqrestore(&chan->lock, irqflags);
		dev_dbg(chan->dev, "chan %p descs are not available\n", chan);
		return NULL;
	}
	chan->desc_free_cnt = chan->desc_free_cnt - desc_cnt;
	spin_unlock_irqrestore(&chan->lock, irqflags);

	new = zdma_alloc_tx_descriptor(chan);
	if (!new)
		return NULL;

	dst_avail = sg_dma_len(dst_sg);
	src_avail = sg_dma_len(src_sg);

	/* Run until we are out of scatterlist entries */
	while (true) {
		/* Allocate and populate the descriptor */
		desc = zdma_get_descriptor(chan, new);
		len = min_t(size_t, src_avail, dst_avail);
		len = min_t(size_t, len, ZDMA_MAX_TRANS_LEN);
		if (len == 0)
			goto fetch;
		dma_dst = sg_dma_address(dst_sg) + sg_dma_len(dst_sg) -
			dst_avail;
		dma_src = sg_dma_address(src_sg) + sg_dma_len(src_sg) -
			src_avail;

		zdma_config_sg_ll_desc(chan, desc, dma_src, dma_dst, len, prev);
		prev = desc;
		dst_avail -= len;
		src_avail -= len;
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

	zdma_desc_config_eod(chan, desc);
	new->async_tx.flags = flags;
	return &new->async_tx;
}

/**
 * zdma_chan_remove - Channel remove function
 * @chan: ZDMA channel pointer
 */
static void zdma_chan_remove(struct zdma_chan *chan)
{
	tasklet_kill(&chan->tasklet);
	list_del(&chan->common.device_node);
}

/**
 * zdma_chan_probe - Per Channel Probing
 * @xdev: Driver specific device structure
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int zdma_chan_probe(struct zdma_device *xdev,
			   struct platform_device *pdev)
{
	struct zdma_chan *chan;
	struct resource *res;
	struct device_node *node = pdev->dev.of_node;
	int err;

	chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;
	chan->dev = xdev->dev;
	chan->xdev = xdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chan->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chan->regs))
		return PTR_ERR(chan->regs);

	chan->has_sg = of_property_read_bool(node, "xlnx,include-sg");
	chan->ovrfetch = of_property_read_bool(node, "xlnx,overfetch");
	chan->desc_axi_cohrnt =
			of_property_read_bool(node, "xlnx,desc-axi-cohrnt");
	chan->src_axi_cohrnt =
			of_property_read_bool(node, "xlnx,src-axi-cohrnt");
	chan->dst_axi_cohrnt =
			of_property_read_bool(node, "xlnx,dst-axi-cohrnt");

	err = of_property_read_u32(node, "xlnx,desc-axi-qos",
				   &chan->desc_axi_qos);
	if (err < 0)
		chan->desc_axi_qos = 0;

	err = of_property_read_u32(node, "xlnx,desc-axi-cache",
				   &chan->desc_axi_cache);
	if (err < 0)
		chan->desc_axi_cache = 0;

	err = of_property_read_u32(node, "xlnx,src-axi-qos",
				   &chan->src_axi_qos);
	if (err < 0)
		chan->src_axi_qos = 0;
	err = of_property_read_u32(node, "xlnx,src-axi-cache",
				   &chan->src_axi_cache);
	if (err < 0)
		chan->src_axi_cache = ARCACHE_RST_VAL;
	err = of_property_read_u32(node, "xlnx,dst-axi-qos",
				   &chan->dst_axi_qos);
	if (err < 0)
		chan->dst_axi_qos = 0;
	err = of_property_read_u32(node, "xlnx,dst-axi-cache",
				   &chan->dst_axi_cache);
	if (err < 0)
		chan->dst_axi_cache = AWCACHE_RST_VAL;
	err = of_property_read_u32(node, "xlnx,src-burst-len",
				   &chan->src_burst_len);
	if (err < 0)
		chan->src_burst_len = ARLEN_RST_VAL;
	err = of_property_read_u32(node, "xlnx,dst-burst-len",
				   &chan->dst_burst_len);
	if (err < 0)
		chan->dst_burst_len = AWLEN_RST_VAL;
	err = of_property_read_u32(node, "xlnx,ratectrl", &chan->ratectrl);
	if (err < 0)
		chan->ratectrl = 0;
	err = of_property_read_u32(node, "xlnx,src-issue",
				   &chan->src_issue);
	if (err < 0)
		chan->src_issue = SRC_ISSUE_RST_VAL;

	err = of_property_read_u32(node, "xlnx,id", &chan->id);
	if (err < 0)
		dev_err(xdev->dev, "unable to read id property");

	if (of_device_is_compatible(node, "xlnx,gdma-1.0"))
		chan->bus_width = DMA_SLAVE_BUSWIDTH_16_BYTES;
	else
		chan->bus_width = DMA_SLAVE_BUSWIDTH_8_BYTES;

	xdev->chan = chan;
	tasklet_init(&chan->tasklet, zdma_do_tasklet, (unsigned long)chan);
	snprintf(chan->name, sizeof(chan->name), "zdmachan%d", chan->id);
	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);

	dma_cookie_init(&chan->common);
	chan->common.device = &xdev->common;
	list_add_tail(&chan->common.device_node, &xdev->common.channels);

	zdma_init(chan);
	chan->irq = platform_get_irq(pdev, 0);
	err = devm_request_irq(&pdev->dev, chan->irq, zdma_irq_handler, 0,
			       chan->name, chan);
	if (err)
		return err;

	chan->desc_size = sizeof(struct zdma_desc_ll);
	chan->idle = true;
	return 0;
}

/**
 * of_dma_zdma_xlate - Translation function
 * @dma_spec: Pointer to DMA specifier as found in the device tree
 * @ofdma: Pointer to DMA controller data
 *
 * Return: DMA channel pointer on success and NULL on error
 */
static struct dma_chan *of_dma_zdma_xlate(struct of_phandle_args *dma_spec,
					    struct of_dma *ofdma)
{
	struct zdma_device *xdev = ofdma->of_dma_data;

	return dma_get_slave_channel(&xdev->chan->common);
}

/**
 * zdma_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int zdma_probe(struct platform_device *pdev)
{
	struct zdma_device *xdev;
	int ret;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &pdev->dev;
	INIT_LIST_HEAD(&xdev->common.channels);

	dma_set_mask(&pdev->dev, DMA_BIT_MASK(44));
	dma_cap_set(DMA_SG, xdev->common.cap_mask);
	dma_cap_set(DMA_MEMCPY, xdev->common.cap_mask);

	xdev->common.device_prep_dma_sg = zdma_prep_sg;
	xdev->common.device_prep_dma_memcpy = zdma_prep_memcpy;
	xdev->common.device_control = zdma_device_control;
	xdev->common.device_issue_pending = zdma_issue_pending;
	xdev->common.device_alloc_chan_resources = zdma_alloc_chan_resources;
	xdev->common.device_free_chan_resources = zdma_free_chan_resources;
	xdev->common.device_tx_status = zdma_tx_status;
	xdev->common.device_slave_caps = zdma_device_slave_caps;
	xdev->common.dev = &pdev->dev;

	platform_set_drvdata(pdev, xdev);

	ret = zdma_chan_probe(xdev, pdev);
	if (ret) {
		dev_err(&pdev->dev, "Probing channel failed\n");
		goto free_chan_resources;
	}

	dma_async_device_register(&xdev->common);

	ret = of_dma_controller_register(pdev->dev.of_node,
					 of_dma_zdma_xlate, xdev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to register DMA to DT\n");
		dma_async_device_unregister(&xdev->common);
		goto free_chan_resources;
	}

	dev_info(&pdev->dev, "ZDMA driver Probe success\n");

	return 0;

free_chan_resources:
	if (xdev->chan)
		zdma_chan_remove(xdev->chan);
	return ret;
}

/**
 * zdma_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: Always '0'
 */
static int zdma_remove(struct platform_device *pdev)
{
	struct zdma_device *xdev = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&xdev->common);

	if (xdev->chan)
		zdma_chan_remove(xdev->chan);

	return 0;
}

static const struct of_device_id zdma_of_match[] = {
	{ .compatible = "xlnx,gdma-1.0", },
	{ .compatible = "xlnx,adma-1.0", },
	{}
};
MODULE_DEVICE_TABLE(of, zdma_of_match);

static struct platform_driver zdma_driver = {
	.driver = {
		.name = "xilinx-zdma",
		.of_match_table = zdma_of_match,
	},
	.probe = zdma_probe,
	.remove = zdma_remove,
};

module_platform_driver(zdma_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx ZDMA driver");
MODULE_LICENSE("GPL v2");

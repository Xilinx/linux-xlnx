/*
 * DMA driver for Xilinx Central DMA Engine
 *
 * Copyright (C) 2010 - 2015 Xilinx, Inc. All rights reserved.
 *
 * Based on the Freescale DMA driver.
 *
 * Description:
 *  The AXI CDMA, is a soft IP, which provides high-bandwidth Direct Memory
 *  Access (DMA) between a memory-mapped source address and a memory-mapped
 *  destination address.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/amba/xilinx_dma.h>
#include <linux/dmapool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../dmaengine.h"

/* Hw specific definitions */
#define XILINX_CDMA_MAX_TRANS_LEN      GENMASK(22, 0)

/* Register Offsets */
#define XILINX_CDMA_CONTROL_OFFSET	0x00 /* Control Reg */
#define XILINX_CDMA_STATUS_OFFSET	0x04 /* Status Reg */
#define XILINX_CDMA_CDESC_OFFSET	0x08 /* Current descriptor Reg */
#define XILINX_CDMA_TDESC_OFFSET	0x10 /* Tail descriptor Reg */
#define XILINX_CDMA_SRCADDR_OFFSET	0x18 /* Source Address Reg */
#define XILINX_CDMA_DSTADDR_OFFSET	0x20 /* Dest Address Reg */
#define XILINX_CDMA_BTT_OFFSET		0x28 /* Bytes to transfer Reg */

/* General register bits definitions */
#define XILINX_CDMA_CR_RESET_MASK 	BIT(2)
#define XILINX_CDMA_CR_SGMODE          BIT(3)

#define XILINX_CDMA_SR_IDLE_MASK       BIT(1)

#define XILINX_CDMA_XR_IRQ_IOC_MASK    BIT(12)
#define XILINX_CDMA_XR_IRQ_DELAY_MASK  BIT(13)
#define XILINX_CDMA_XR_IRQ_ERROR_MASK  BIT(14)
#define XILINX_CDMA_XR_IRQ_ALL_MASK    GENMASK(14, 12)

#define XILINX_CDMA_XR_DELAY_MASK      GENMASK(31, 24)
#define XILINX_CDMA_XR_COALESCE_MASK   GENMASK(23, 16)

#define XILINX_CDMA_DELAY_SHIFT		24 /* Delay counter shift */
#define XILINX_CDMA_COALESCE_SHIFT	16 /* Coaelsce counter shift */

#define XILINX_CDMA_DELAY_MAX		0xFF /* Maximum delay counter value */
/* Maximum coalescing counter value */
#define XILINX_CDMA_COALESCE_MAX	0xFF

#define XILINX_CDMA_CR_SGMODE_MASK	0x00000008 /* Scatter gather mode */

/* BD definitions for Axi Cdma */
#define XILINX_CDMA_BD_STS_ALL_MASK	0xF0000000

/* Feature encodings */
#define XILINX_CDMA_FTR_DATA_WIDTH_MASK	0x000000FF /* Data width mask, 1024 */
#define XILINX_CDMA_FTR_HAS_SG		0x00000100 /* Has SG */
#define XILINX_CDMA_FTR_HAS_SG_SHIFT	8 /* Has SG shift */

/* Delay loop counter to prevent hardware failure */
#define XILINX_CDMA_RESET_LOOP	1000000
#define XILINX_CDMA_HALT_LOOP	1000000

/* Hardware descriptor */
struct xilinx_cdma_desc_hw {
	u32 next_desc;	/* 0x00 */
	u32 pad1;	/* 0x04 */
	u32 src_addr;	/* 0x08 */
	u32 pad2;	/* 0x0C */
	u32 dest_addr;	/* 0x10 */
	u32 pad3;	/* 0x14 */
	u32 control;	/* 0x18 */
	u32 status;	/* 0x1C */
} __aligned(64);

/**
 * struct xilinx_cdma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_cdma_tx_segment {
	struct xilinx_cdma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
} __aligned(64);

/**
 * struct xilinx_cdma_tx_descriptor - Per Transaction structure
 * @async_tx: Async transaction descriptor
 * @segments: TX segments list
 * @node: Node in the channel descriptors list
 */
struct xilinx_cdma_tx_descriptor {
	struct dma_async_tx_descriptor async_tx;
	struct list_head segments;
	struct list_head node;
};

/* Per DMA specific operations should be embedded in the channel structure */
struct xilinx_cdma_chan {
	void __iomem *regs;			/* Control status registers */
	dma_cookie_t completed_cookie;		/* Maximum cookie completed */
	dma_cookie_t cookie;			/* The current cookie */
	spinlock_t lock;			/* Descriptor operation lock */
	bool sg_waiting;			/* SG transfer waiting */
	struct list_head done_list;
	struct list_head pending_list;
	struct xilinx_cdma_tx_descriptor *active_desc;
	struct dma_chan common;			/* DMA common channel */
	struct dma_pool *desc_pool;		/* Descriptors pool */
	struct device *dev;			/* The dma device */
	int irq;				/* Channel IRQ */
	int id;					/* Channel ID */
	enum dma_transfer_direction direction;	/* Transfer direction */
	int max_len;				/* Max data len per transfer */
	bool is_lite;				/* Whether is light build */
	bool has_sg;				/* Support scatter transfers */
	bool has_dre;				/* For unaligned transfers */
	int err;				/* Channel has errors */
	bool idle;
	struct tasklet_struct tasklet;		/* Cleanup work after irq */
	u32 feature;				/* IP feature */
};

struct xilinx_cdma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_cdma_chan *chan;
	bool has_sg;				/* Support scatter transfers */
	u32 feature;
};

#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_cdma_chan, common)
#define to_cdma_tx_descriptor(tx) \
	container_of(tx, struct xilinx_cdma_tx_descriptor, async_tx)

/* IO accessors */
static inline void
cdma_write(struct xilinx_cdma_chan *chan, u32 reg, u32 val)
{
	writel(val, chan->regs + reg);
}

static inline u32 cdma_read(struct xilinx_cdma_chan *chan, u32 reg)
{
	return readl(chan->regs + reg);
}

static inline void cdma_ctrl_clr(struct xilinx_cdma_chan *chan, u32 reg,
				u32 clr)
{
	cdma_write(chan, reg, cdma_read(chan, reg) & ~clr);
}

static inline void cdma_ctrl_set(struct xilinx_cdma_chan *chan, u32 reg,
				u32 set)
{
	cdma_write(chan, reg, cdma_read(chan, reg) | set);
}

/* Required functions */

/**
 * xilinx_cdma_alloc_tx_segment - Allocate transaction segment
 * @chan: Driver specific cdma channel
 *
 * Return: The allocated segment on success and NULL on failure.
 */
static struct xilinx_cdma_tx_segment *
xilinx_cdma_alloc_tx_segment(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_tx_segment *segment;
	dma_addr_t phys;

	segment = dma_pool_alloc(chan->desc_pool, GFP_ATOMIC, &phys);
	if (!segment)
		return NULL;

	memset(segment, 0, sizeof(*segment));
	segment->phys = phys;

	return segment;
}

/**
 * xilinx_cdma_free_tx_segment - Free transaction segment
 * @chan: Driver specific cdma channel
 * @segment: cdma transaction segment
 */
static void xilinx_cdma_free_tx_segment(struct xilinx_cdma_chan *chan,
					struct xilinx_cdma_tx_segment *segment)
{
	dma_pool_free(chan->desc_pool, segment, segment->phys);
}

/**
 * xilinx_cdma_tx_descriptor - Allocate transaction descriptor
 * @chan: Driver specific cdma channel
 *
 * Return: The allocated descriptor on success and NULL on failure.
 */
static struct xilinx_cdma_tx_descriptor *
xilinx_cdma_alloc_tx_descriptor(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_tx_descriptor *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	INIT_LIST_HEAD(&desc->segments);

	return desc;
}

/**
 * xilinx_cdma_free_tx_descriptor - Free transaction descriptor
 * @chan: Driver specific cdma channel
 * @desc: cdma transaction descriptor
 */
static void
xilinx_cdma_free_tx_descriptor(struct xilinx_cdma_chan *chan,
			       struct xilinx_cdma_tx_descriptor *desc)
{
	struct xilinx_cdma_tx_segment *segment, *next;

	if (!desc)
		return;

	list_for_each_entry_safe(segment, next, &desc->segments, node) {
		list_del(&segment->node);
		xilinx_cdma_free_tx_segment(chan, segment);
	}

	kfree(desc);
}

/**
 * xilinx_cdma_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_cdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);

	/* Has this channel already been allocated? */
	if (chan->desc_pool)
		return 0;

	/*
	 * We need the descriptor to be aligned to 64bytes
	 * for meeting Xilinx DMA specification requirement.
	 */
	chan->desc_pool = dma_pool_create("xilinx_cdma_desc_pool",
				chan->dev,
				sizeof(struct xilinx_cdma_tx_segment),
				__alignof__(struct xilinx_cdma_tx_segment), 0);
	if (!chan->desc_pool) {
		dev_err(chan->dev,
			"unable to allocate channel descriptor pool\n");
		return -ENOMEM;
	}

	dma_cookie_init(dchan);
	return 0;
}

static void xilinx_cdma_free_desc_list(struct xilinx_cdma_chan *chan,
				       struct list_head *list)
{
	struct xilinx_cdma_tx_descriptor *desc, *next;

	list_for_each_entry_safe(desc, next, list, node) {
		list_del(&desc->node);
		xilinx_cdma_free_tx_descriptor(chan, desc);
	}
}

static void xilinx_cdma_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_cdma_free_desc_list(chan, &chan->done_list);
	xilinx_cdma_free_desc_list(chan, &chan->pending_list);
	spin_unlock_irqrestore(&chan->lock, flags);

	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

static void xilinx_cdma_chan_desc_cleanup(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_tx_descriptor *desc, *next;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	list_for_each_entry_safe(desc, next, &chan->done_list, node) {
		dma_async_tx_callback callback;
		void *callback_param;

		/* Remove from the list of running transactions */
		list_del(&desc->node);

		/* Run the link descriptor callback function */
		callback = desc->async_tx.callback;
		callback_param = desc->async_tx.callback_param;
		if (callback) {
			spin_unlock_irqrestore(&chan->lock, flags);
			callback(callback_param);
			spin_lock_irqsave(&chan->lock, flags);
		}

		/* Run any dependencies, then free the descriptor */
		dma_run_dependencies(&desc->async_tx);
		xilinx_cdma_free_tx_descriptor(chan, desc);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

static enum dma_status xilinx_tx_status(struct dma_chan *dchan,
					dma_cookie_t cookie,
					struct dma_tx_state *txstate)
{
	return dma_cookie_status(dchan, cookie, txstate);
}

/**
 * cdma_is_idle - Check if cdma channel is idle
 * @chan: Driver specific cdma channel
 *
 * Return: 'true' if idle, 'false' if not.
 */
static bool cdma_is_idle(struct xilinx_cdma_chan *chan)
{
	return cdma_read(chan, XILINX_CDMA_STATUS_OFFSET) &
	       XILINX_CDMA_SR_IDLE_MASK;
}

static void xilinx_cdma_start_transfer(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_tx_descriptor *desc;
	struct xilinx_cdma_tx_segment *head, *tail;

	if (chan->err)
		return;

	if (list_empty(&chan->pending_list))
		return;

	if (!chan->idle)
		return;

	/* If hardware is busy, cannot submit */
	if (chan->has_sg && !cdma_is_idle(chan)) {
		tail = list_entry(desc->segments.prev,
                                  struct xilinx_cdma_tx_segment, node);
		cdma_write(chan, XILINX_CDMA_TDESC_OFFSET, tail->phys);
                goto out_free_desc;
	}

	desc = list_first_entry(&chan->pending_list,
				struct xilinx_cdma_tx_descriptor, node);

	if (chan->has_sg) {
		head = list_first_entry(&desc->segments,
					struct xilinx_cdma_tx_segment, node);
		tail = list_entry(desc->segments.prev,
				  struct xilinx_cdma_tx_segment, node);

		cdma_write(chan, XILINX_CDMA_CDESC_OFFSET, head->phys);

		/* Update tail ptr register which will start the transfer */
		cdma_write(chan, XILINX_CDMA_TDESC_OFFSET, tail->phys);
	} else {
		/* In simple mode */
		struct xilinx_cdma_tx_segment *segment;
		struct xilinx_cdma_desc_hw *hw;

		segment = list_first_entry(&desc->segments,
					   struct xilinx_cdma_tx_segment,
					   node);

		hw = &segment->hw;

		cdma_write(chan, XILINX_CDMA_SRCADDR_OFFSET, hw->src_addr);
		cdma_write(chan, XILINX_CDMA_DSTADDR_OFFSET, hw->dest_addr);

		/* Start the transfer */
		cdma_write(chan, XILINX_CDMA_BTT_OFFSET,
				hw->control & XILINX_CDMA_MAX_TRANS_LEN);
	}

out_free_desc:
	list_del(&desc->node);
	chan->idle = false;
	chan->active_desc = desc;
}

/*
 * If sg mode, link the pending list to running list; if simple mode, get the
 * head of the pending list and submit it to hw
 */
static void xilinx_cdma_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_cdma_start_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_cdma_complete_descriptor - Mark the active descriptor as complete
 * @chan : xilinx DMA channel
 */
static void xilinx_cdma_complete_descriptor(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_tx_descriptor *desc;

	desc = chan->active_desc;
	if (!desc) {
		dev_dbg(chan->dev, "no running descriptors\n");
		return;
	}

	dma_cookie_complete(&desc->async_tx);
	list_add_tail(&desc->node, &chan->done_list);

	chan->active_desc = NULL;
}

/* Reset hardware */
static int cdma_reset(struct xilinx_cdma_chan *chan)
{
	int loop = XILINX_CDMA_RESET_LOOP;
	u32 tmp;

	cdma_ctrl_set(chan, XILINX_CDMA_CONTROL_OFFSET,
			XILINX_CDMA_CR_RESET_MASK);

	tmp = cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET) &
	      XILINX_CDMA_CR_RESET_MASK;

	/* Wait for the hardware to finish reset */
	while (loop && tmp) {
		tmp = cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET) &
		      XILINX_CDMA_CR_RESET_MASK;
		loop -= 1;
	}

	if (!loop) {
		dev_err(chan->dev, "reset timeout, cr %x, sr %x\n",
			cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET),
			cdma_read(chan, XILINX_CDMA_STATUS_OFFSET));
		return -EBUSY;
	}

	/* For Axi CDMA, always do sg transfers if sg mode is built in */
	if (chan->has_sg)
		cdma_ctrl_set(chan, XILINX_CDMA_CONTROL_OFFSET,
				XILINX_CDMA_CR_SGMODE_MASK);

	return 0;
}


static irqreturn_t cdma_intr_handler(int irq, void *data)
{
	struct xilinx_cdma_chan *chan = data;
	u32 stat;

	stat = cdma_read(chan, XILINX_CDMA_STATUS_OFFSET);
	if (!(stat & XILINX_CDMA_XR_IRQ_ALL_MASK))
		return IRQ_NONE;

	/* Ack the interrupts */
	cdma_write(chan, XILINX_CDMA_STATUS_OFFSET,
		   XILINX_CDMA_XR_IRQ_ALL_MASK);

	if (stat & XILINX_CDMA_XR_IRQ_ERROR_MASK) {
		dev_err(chan->dev,
			"Channel %x has errors %x, cdr %x tdr %x\n",
			(u32)chan,
			(u32)cdma_read(chan, XILINX_CDMA_STATUS_OFFSET),
			(u32)cdma_read(chan, XILINX_CDMA_CDESC_OFFSET),
			(u32)cdma_read(chan, XILINX_CDMA_TDESC_OFFSET));
		chan->err = 1;
	}

	/*
	 * Device takes too long to do the transfer when user requires
	 * responsiveness
	 */
	if (stat & XILINX_CDMA_XR_IRQ_DELAY_MASK)
		dev_dbg(chan->dev, "Inter-packet latency too long\n");

	if (stat & XILINX_CDMA_XR_IRQ_IOC_MASK) {
		spin_lock(&chan->lock);
		xilinx_cdma_complete_descriptor(chan);
		chan->idle = true;
		xilinx_cdma_start_transfer(chan);
		spin_unlock(&chan->lock);
	}

	tasklet_schedule(&chan->tasklet);
	return IRQ_HANDLED;
}

static void cdma_do_tasklet(unsigned long data)
{
	struct xilinx_cdma_chan *chan = (struct xilinx_cdma_chan *)data;

	xilinx_cdma_chan_desc_cleanup(chan);
}

/*
 * Assign cookie to each descriptor, and append the descriptors to the pending
 * list
 */
static dma_cookie_t xilinx_cdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(tx->chan);
	struct xilinx_cdma_tx_descriptor *desc = to_cdma_tx_descriptor(tx);
	dma_cookie_t cookie;
	unsigned long flags;
	int err;

	if (chan->err) {
		/*
		 * If reset fails, need to hard reset the system.
		 * Channel is no longer functional
		 */
		err = cdma_reset(chan);
		if (err < 0)
			return err;
	}

	spin_lock_irqsave(&chan->lock, flags);

	cookie = dma_cookie_assign(tx);

	/* Append the transaction to the pending transactions queue. */
	list_add_tail(&desc->node, &chan->pending_list);

	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * xilinx_cdma_prep_memcpy - prepare descriptors for a memcpy transaction
 * @dchan: DMA channel
 * @dma_dst: destination address
 * @dma_src: source address
 * @len: transfer length
 * @flags: transfer ack flags
 */
static struct dma_async_tx_descriptor *xilinx_cdma_prep_memcpy(
	struct dma_chan *dchan, dma_addr_t dma_dst, dma_addr_t dma_src,
	size_t len, unsigned long flags)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_cdma_desc_hw *hw;
	struct xilinx_cdma_tx_descriptor *desc;
	struct xilinx_cdma_tx_segment *segment, *prev;

	if (!len || len > XILINX_CDMA_MAX_TRANS_LEN)
		return NULL;

	desc = xilinx_cdma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_cdma_tx_submit;
	async_tx_ack(&desc->async_tx);

	/* Allocate the link descriptor from DMA pool */
	segment = xilinx_cdma_alloc_tx_segment(chan);
	if (!segment)
		goto error;

	hw = &segment->hw;
	hw->control = len;
	hw->src_addr = dma_src;
	hw->dest_addr = dma_dst;

	/* Fill the previous next descriptor with current */
	prev = list_last_entry(&desc->segments,
				struct xilinx_cdma_tx_segment, node);
	prev->hw.next_desc = segment->phys;

	/* Insert the segment into the descriptor segments list. */
	list_add_tail(&segment->node, &desc->segments);

	prev = segment;

	/* Link the last hardware descriptor with the first. */
	segment = list_first_entry(&desc->segments,
				struct xilinx_cdma_tx_segment, node);
	prev->hw.next_desc = segment->phys;

	return &desc->async_tx;

error:
	xilinx_cdma_free_tx_descriptor(chan, desc);
	return NULL;
}

/**
 * xilinx_cdma_terminate_all - Free the descriptors
 * @dchan: DMA Channel pointer
 *
 * Return: '0' always
 */
static int xilinx_cdma_terminate_all(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	/* Reset the channel */
	cdma_reset(chan);

	/* Remove and free all of the descriptors in the lists */
	xilinx_cdma_free_desc_list(chan, &chan->pending_list);
	xilinx_cdma_free_desc_list(chan, &chan->done_list);

	spin_unlock_irqrestore(&chan->lock, flags);

	return 0;
}

/**
 * xilinx_cdma_channel_set_config - Configure cdma channel
 * @dchan: DMA channel
 * @cfg: cdma device configuration pointer
 *
 * Return: '0' on success and failure value on error
 */
int xilinx_cdma_channel_set_config(struct dma_chan *dchan,
					struct xilinx_cdma_config *cfg)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	u32 reg = cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET);

	if (!cdma_is_idle(chan))
		return -EBUSY;

	if (cfg->reset)
		return cdma_reset(chan);

	if (cfg->coalesc <= XILINX_CDMA_COALESCE_MAX) {
		reg &= ~XILINX_CDMA_XR_COALESCE_MASK;
		reg |= cfg->coalesc << XILINX_CDMA_COALESCE_SHIFT;
	}

	if (cfg->delay <= XILINX_CDMA_DELAY_MAX) {
		reg &= ~XILINX_CDMA_XR_DELAY_MASK;
		reg |= cfg->delay << XILINX_CDMA_DELAY_SHIFT;
	}

	cdma_write(chan, XILINX_CDMA_CONTROL_OFFSET, reg);

	return 0;
}
EXPORT_SYMBOL(xilinx_cdma_channel_set_config);

static void xilinx_cdma_free_channels(struct xilinx_cdma_device *xdev)
{

	list_del(&xdev->chan->common.device_node);
	tasklet_kill(&xdev->chan->tasklet);
	irq_dispose_mapping(xdev->chan->irq);
}

/*
 * Probing channels
 *
 * . Get channel features from the device tree entry
 * . Initialize special channel handling routines
 */
static int xilinx_cdma_chan_probe(struct xilinx_cdma_device *xdev,
				  struct device_node *node, u32 feature)
{
	struct xilinx_cdma_chan *chan;
	int err;
	u32 value, width = 0;

	/* alloc channel */
	chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->feature = feature;
	chan->max_len = XILINX_CDMA_MAX_TRANS_LEN;

	chan->has_dre = of_property_read_bool(node, "xlnx,include-dre");

	err = of_property_read_u32(node, "xlnx,datawidth", &value);
	if (err) {
		dev_err(xdev->dev, "unable to read datawidth property");
		return err;
	} else {
		width = value >> 3; /* convert bits to bytes */

		/* If data width is greater than 8 bytes, DRE is not in hw */
		if (width > 8)
			chan->has_dre = 0;

		chan->feature |= width - 1;
	}

	chan->direction = DMA_MEM_TO_MEM;
	chan->has_sg = xdev->has_sg;

	chan->is_lite = of_property_read_bool(node, "xlnx,lite-mode");
	if (chan->is_lite) {
		err = of_property_read_u32(node, "xlnx,max-burst-len", &value);
		if (err) {
			dev_err(xdev->dev, "unable to read max burstlen property");
			return err;
		}
		if (value) {
			if (!width) {
				dev_err(xdev->dev,
					"Lite mode w/o data width property\n");
				return -EPERM;
			}
			chan->max_len = width * value;
		}
	}

	chan->regs = xdev->regs;

	if (!chan->has_dre)
		xdev->common.copy_align = fls(width - 1);

	chan->dev = xdev->dev;
	xdev->chan = chan;

	/* Initialize the channel */
	err = cdma_reset(chan);
	if (err) {
		dev_err(xdev->dev, "Reset channel failed\n");
		return err;
	}

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);

	chan->common.device = &xdev->common;

	/* Find the IRQ line, if it exists in the device tree */
	chan->irq = irq_of_parse_and_map(node, 0);
	err = devm_request_irq(xdev->dev, chan->irq, cdma_intr_handler,
			       IRQF_SHARED,
			       "xilinx-cdma-controller", chan);
	if (err) {
		dev_err(xdev->dev, "unable to request IRQ\n");
		return err;
	}

	tasklet_init(&chan->tasklet, cdma_do_tasklet, (unsigned long)chan);

	/* Add the channel to DMA device channel list */
	list_add_tail(&chan->common.device_node, &xdev->common.channels);

	chan->idle = true;

	return 0;
}

/**
 * of_dma_xilinx_xlate - Translation function
 * @dma_spec: Pointer to DMA specifier as found in the device tree
 * @ofdma: Pointer to DMA controller data
 *
 * Return: DMA channel pointer on success and NULL on error
 */
static struct dma_chan *of_dma_xilinx_xlate(struct of_phandle_args *dma_spec,
						struct of_dma *ofdma)
{
	struct xilinx_cdma_device *xdev = ofdma->of_dma_data;

	return dma_get_slave_channel(&xdev->chan->common);
}

static int xilinx_cdma_probe(struct platform_device *pdev)
{
	struct xilinx_cdma_device *xdev;
	struct device_node *child, *node;
	struct resource *res;
	int ret;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &(pdev->dev);
	INIT_LIST_HEAD(&xdev->common.channels);

	node = pdev->dev.of_node;

	/* iomap registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xdev->regs))
		return PTR_ERR(xdev->regs);

	/* Check if SG is enabled */
	xdev->has_sg = of_property_read_bool(node, "xlnx,include-sg");

	/* Axi CDMA only does memcpy */
	dma_cap_set(DMA_MEMCPY, xdev->common.cap_mask);
	xdev->common.device_prep_dma_memcpy = xilinx_cdma_prep_memcpy;
	xdev->common.device_terminate_all = xilinx_cdma_terminate_all;
	xdev->common.device_issue_pending = xilinx_cdma_issue_pending;
	xdev->common.device_alloc_chan_resources =
		xilinx_cdma_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
		xilinx_cdma_free_chan_resources;
	xdev->common.device_tx_status = xilinx_tx_status;
	xdev->common.dev = &pdev->dev;

	platform_set_drvdata(pdev, xdev);

	for_each_child_of_node(node, child) {
		ret = xilinx_cdma_chan_probe(xdev, child, xdev->feature);
		if (ret) {
			dev_err(&pdev->dev, "Probing channels failed\n");
			goto free_chan_resources;
		}
	}

	dma_async_device_register(&xdev->common);

	ret = of_dma_controller_register(node, of_dma_xilinx_xlate, xdev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to register DMA to DT\n");
		dma_async_device_unregister(&xdev->common);
		goto free_chan_resources;
	}

	dev_info(&pdev->dev, "Probing xilinx axi cdma engine...Successful\n");

	return 0;

free_chan_resources:
	xilinx_cdma_free_channels(xdev);

	return ret;
}

static int xilinx_cdma_remove(struct platform_device *pdev)
{
	struct xilinx_cdma_device *xdev;

	xdev = platform_get_drvdata(pdev);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&xdev->common);

	xilinx_cdma_free_channels(xdev);

	return 0;
}

static const struct of_device_id xilinx_cdma_of_match[] = {
	{ .compatible = "xlnx,axi-cdma-1.00.a", },
	{}
};
MODULE_DEVICE_TABLE(of, xilinx_cdma_of_match);

static struct platform_driver xilinx_cdma_driver = {
	.driver = {
		.name = "xilinx-cdma",
		.of_match_table = xilinx_cdma_of_match,
	},
	.probe = xilinx_cdma_probe,
	.remove = xilinx_cdma_remove,
};

module_platform_driver(xilinx_cdma_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx CDMA driver");
MODULE_LICENSE("GPL v2");

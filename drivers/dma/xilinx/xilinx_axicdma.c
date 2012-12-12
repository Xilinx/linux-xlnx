/*
 * Xilinx Central DMA Engine support
 *
 * Copyright (C) 2010 Xilinx, Inc. All rights reserved.
 *
 * Based on the Freescale DMA driver.
 *
 * Description:
 *  . Axi CDMA engine, it does transfers between memory and memory
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/dmapool.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/amba/xilinx_dma.h>

/* Hw specific definitions */
#define XILINX_CDMA_MAX_CHANS_PER_DEVICE	0x1
#define XILINX_CDMA_MAX_TRANS_LEN		0x7FFFFF

/* General register bits definitions */
#define XILINX_CDMA_CR_RESET_MASK		0x00000004
						/* Reset DMA engine */

#define XILINX_CDMA_SR_IDLE_MASK		0x00000002
						/* DMA channel idle */

#define XILINX_CDMA_SR_ERR_INTERNAL_MASK	0x00000010
						/* Datamover internal err */
#define XILINX_CDMA_SR_ERR_SLAVE_MASK		0x00000020
						/* Datamover slave err */
#define XILINX_CDMA_SR_ERR_DECODE_MASK		0x00000040
						/* Datamover decode err */
#define XILINX_CDMA_SR_ERR_SG_INT_MASK		0x00000100
						/* SG internal err */
#define XILINX_CDMA_SR_ERR_SG_SLV_MASK		0x00000200
						/* SG slave err */
#define XILINX_CDMA_SR_ERR_SG_DEC_MASK		0x00000400
						/* SG decode err */
#define XILINX_CDMA_SR_ERR_ALL_MASK		0x00000770
						/* All errors */

#define XILINX_CDMA_XR_IRQ_IOC_MASK	0x00001000
						/* Completion interrupt */
#define XILINX_CDMA_XR_IRQ_DELAY_MASK	0x00002000
						/* Delay interrupt */
#define XILINX_CDMA_XR_IRQ_ERROR_MASK	0x00004000
						/* Error interrupt */
#define XILINX_CDMA_XR_IRQ_ALL_MASK	0x00007000
						/* All interrupts */

#define XILINX_CDMA_XR_DELAY_MASK	0xFF000000
						/* Delay timeout counter */
#define XILINX_CDMA_XR_COALESCE_MASK	0x00FF0000
						/* Coalesce counter */

#define XILINX_CDMA_IRQ_SHIFT		12
#define XILINX_CDMA_DELAY_SHIFT		24
#define XILINX_CDMA_COALESCE_SHIFT	16

#define XILINX_CDMA_DELAY_MAX		0xFF
					/* Maximum delay counter value */
#define XILINX_CDMA_COALESCE_MAX	0xFF
					/* Maximum coalescing counter value */

#define XILINX_CDMA_CR_SGMODE_MASK	0x00000008
					/* Scatter gather mode */

#define XILINX_CDMA_SR_SGINCLD_MASK		0x00000008
					/* Hybrid build */
#define XILINX_CDMA_XR_IRQ_SIMPLE_ALL_MASK	0x00005000
					/* All interrupts for simple mode */

/* BD definitions for Axi Cdma */
#define XILINX_CDMA_BD_STS_COMPL_MASK	0x80000000
#define XILINX_CDMA_BD_STS_ERR_MASK	0x70000000
#define XILINX_CDMA_BD_STS_ALL_MASK	0xF0000000

/* Feature encodings */
#define XILINX_CDMA_FTR_DATA_WIDTH_MASK	0x000000FF
						/* Data width mask, 1024 */
#define XILINX_CDMA_FTR_HAS_SG		0x00000100
						/* Has SG */
#define XILINX_CDMA_FTR_HAS_SG_SHIFT	8
						/* Has SG shift */

/* Delay loop counter to prevent hardware failure */
#define XILINX_CDMA_RESET_LOOP	1000000
#define XILINX_CDMA_HALT_LOOP	1000000

/* Device Id in the private structure */
#define XILINX_CDMA_DEVICE_ID_SHIFT	28

/* IO accessors */
#define CDMA_OUT(addr, val)	(iowrite32(val, addr))
#define CDMA_IN(addr)		(ioread32(addr))

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

/* Software descriptor */
struct xilinx_cdma_desc_sw {
	struct xilinx_cdma_desc_hw hw;
	struct list_head node;
	struct list_head tx_list;
	struct dma_async_tx_descriptor async_tx;
} __aligned(64);

/* AXI CDMA Registers Structure */
struct xcdma_regs {
	u32 cr;		/* 0x00 Control Register */
	u32 sr;		/* 0x04 Status Register */
	u32 cdr;	/* 0x08 Current Descriptor Register */
	u32 pad1;
	u32 tdr;	/* 0x10 Tail Descriptor Register */
	u32 pad2;
	u32 src;	/* 0x18 Source Address Register */
	u32 pad3;
	u32 dst;	/* 0x20 Destination Address Register */
	u32 pad4;
	u32 btt_ref;	/* 0x28 Bytes To Transfer */
};

/* Per DMA specific operations should be embedded in the channel structure */
struct xilinx_cdma_chan {
	struct xcdma_regs __iomem *regs;	/* Control status registers */
	dma_cookie_t completed_cookie;		/* Maximum cookie completed */
	dma_cookie_t cookie;			/* The current cookie */
	spinlock_t lock;			/* Descriptor operation lock */
	bool sg_waiting;			/* SG transfer waiting */
	struct list_head active_list;		/* Active descriptors */
	struct list_head pending_list;		/* Descriptors waiting */
	struct dma_chan common;			/* DMA common channel */
	struct dma_pool *desc_pool;		/* Descriptors pool */
	struct device *dev;			/* The dma device */
	int irq;				/* Channel IRQ */
	int id;					/* Channel ID */
	enum dma_transfer_direction direction;	/* Transfer direction */
	int max_len;				/* Max data len per transfer */
	int is_lite;				/* Whether is light build */
	int has_SG;				/* Support scatter transfers */
	int has_DRE;				/* For unaligned transfers */
	int err;				/* Channel has errors */
	struct tasklet_struct tasklet;		/* Cleanup work after irq */
	u32 feature;				/* IP feature */
	u32 private;				/* Match info for
							channel request */
	void (*start_transfer)(struct xilinx_cdma_chan *chan);
	struct xilinx_cdma_config config;	/* Device configuration info */
};

struct xilinx_cdma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_cdma_chan *chan[XILINX_CDMA_MAX_CHANS_PER_DEVICE];
	u32 feature;
	int irq;
};

#define to_xilinx_chan(chan) \
			container_of(chan, struct xilinx_cdma_chan, common)

/* Required functions */

static int xilinx_cdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);

	/* Has this channel already been allocated? */
	if (chan->desc_pool)
		return 1;

	/*
	 * We need the descriptor to be aligned to 64bytes
	 * for meeting Xilinx DMA specification requirement.
	 */
	chan->desc_pool = dma_pool_create("xilinx_cdma_desc_pool",
				chan->dev,
				sizeof(struct xilinx_cdma_desc_sw),
				__alignof__(struct xilinx_cdma_desc_sw), 0);
	if (!chan->desc_pool) {
		dev_err(chan->dev,
			"unable to allocate channel %d descriptor pool\n",
			chan->id);
		return -ENOMEM;
	}

	chan->completed_cookie = 1;
	chan->cookie = 1;

	/* there is at least one descriptor free to be allocated */
	return 1;
}

static void xilinx_cdma_free_desc_list(struct xilinx_cdma_chan *chan,
					struct list_head *list)
{
	struct xilinx_cdma_desc_sw *desc, *_desc;

	list_for_each_entry_safe(desc, _desc, list, node) {
		list_del(&desc->node);
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}
}

static void xilinx_cdma_free_desc_list_reverse(struct xilinx_cdma_chan *chan,
						struct list_head *list)
{
	struct xilinx_cdma_desc_sw *desc, *_desc;

	list_for_each_entry_safe_reverse(desc, _desc, list, node) {
		list_del(&desc->node);
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}
}

static void xilinx_cdma_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	dev_dbg(chan->dev, "Free all channel resources.\n");
	spin_lock_irqsave(&chan->lock, flags);
	xilinx_cdma_free_desc_list(chan, &chan->active_list);
	xilinx_cdma_free_desc_list(chan, &chan->pending_list);
	spin_unlock_irqrestore(&chan->lock, flags);

	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

static enum dma_status xilinx_cdma_desc_status(struct xilinx_cdma_chan *chan,
					struct xilinx_cdma_desc_sw *desc)
{
	return dma_async_is_complete(desc->async_tx.cookie,
					chan->completed_cookie,
					chan->cookie);
}

static void xilinx_cdma_chan_desc_cleanup(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_desc_sw *desc, *_desc;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	list_for_each_entry_safe(desc, _desc, &chan->active_list, node) {
		dma_async_tx_callback callback;
		void *callback_param;

		if (xilinx_cdma_desc_status(chan, desc) == DMA_IN_PROGRESS)
			break;

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
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

static enum dma_status xilinx_tx_status(struct dma_chan *dchan,
					dma_cookie_t cookie,
					struct dma_tx_state *txstate)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;

	xilinx_cdma_chan_desc_cleanup(chan);

	last_used = dchan->cookie;
	last_complete = chan->completed_cookie;

	dma_set_tx_state(txstate, last_complete, last_used, 0);

	return dma_async_is_complete(cookie, last_complete, last_used);
}

static int cdma_is_idle(struct xilinx_cdma_chan *chan)
{
	return CDMA_IN(&chan->regs->sr) & XILINX_CDMA_SR_IDLE_MASK;
}

/* Only needed for Axi CDMA v2_00_a or earlier core */
static void cdma_sg_toggle(struct xilinx_cdma_chan *chan)
{
	CDMA_OUT(&chan->regs->cr,
		CDMA_IN(&chan->regs->cr) & ~XILINX_CDMA_CR_SGMODE_MASK);

	CDMA_OUT(&chan->regs->cr,
		CDMA_IN(&chan->regs->cr) | XILINX_CDMA_CR_SGMODE_MASK);
}

#define XILINX_CDMA_DRIVER_DEBUG	0

#if (XILINX_CDMA_DRIVER_DEBUG == 1)
static void desc_dump(struct xilinx_cdma_desc_hw *hw)
{
	pr_info("hw desc %x:\n", (unsigned int)hw);
	pr_info("\tnext_desc %x\n", hw->next_desc);
	pr_info("\tsrc_addr %x\n", hw->src_addr);
	pr_info("\tdest_addr %x\n", hw->dest_addr);
	pr_info("\thsize %x\n", hw->hsize);
	pr_info("\tcontrol %x\n", hw->control);
	pr_info("\tstatus %x\n", hw->status);
}
#endif

static void xilinx_cdma_start_transfer(struct xilinx_cdma_chan *chan)
{
	unsigned long flags;
	struct xilinx_cdma_desc_sw *desch, *desct;
	struct xilinx_cdma_desc_hw *hw;

	if (chan->err)
		return;

	spin_lock_irqsave(&chan->lock, flags);

	if (list_empty(&chan->pending_list))
		goto out_unlock;

	/* If hardware is busy, cannot submit */
	if (!cdma_is_idle(chan)) {
		dev_dbg(chan->dev, "DMA controller still busy %x\n",
					CDMA_IN(&chan->regs->sr));
		goto out_unlock;
	}

	/* Enable interrupts */
	CDMA_OUT(&chan->regs->cr,
	    CDMA_IN(&chan->regs->cr) | XILINX_CDMA_XR_IRQ_ALL_MASK);

	desch = list_first_entry(&chan->pending_list,
			struct xilinx_cdma_desc_sw, node);

	if (chan->has_SG) {

		/* If hybrid mode, append pending list to active list */
		desct = container_of(chan->pending_list.prev,
				struct xilinx_cdma_desc_sw, node);

		list_splice_tail_init(&chan->pending_list, &chan->active_list);

		/*
		 * If hardware is idle, then all descriptors on the active list
		 * are done, start new transfers
		 */
		cdma_sg_toggle(chan);

		CDMA_OUT(&chan->regs->cdr, desch->async_tx.phys);

		/* Update tail ptr register and start the transfer */
		CDMA_OUT(&chan->regs->tdr, desct->async_tx.phys);
		goto out_unlock;
	}

	/* In simple mode */
	list_del(&desch->node);
	list_add_tail(&desch->node, &chan->active_list);

	hw = &desch->hw;

	CDMA_OUT(&chan->regs->src, hw->src_addr);
	CDMA_OUT(&chan->regs->dst, hw->dest_addr);

	/* Start the transfer */
	CDMA_OUT(&chan->regs->btt_ref,
		hw->control & XILINX_CDMA_MAX_TRANS_LEN);

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/*
 * If sg mode, link the pending list to running list; if simple mode, get the
 * head of the pending list and submit it to hw
 */
static void xilinx_cdma_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);

	xilinx_cdma_start_transfer(chan);
}

/**
 * xilinx_cdma_update_completed_cookie - Update the completed cookie.
 * @chan : xilinx DMA channel
 *
 * CONTEXT: hardirq
 */
static void xilinx_cdma_update_completed_cookie(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_desc_sw *desc = NULL;
	struct xilinx_cdma_desc_hw *hw = NULL;
	unsigned long flags;
	dma_cookie_t cookie = -EBUSY;
	int done = 0;

	spin_lock_irqsave(&chan->lock, flags);

	if (list_empty(&chan->active_list)) {
		dev_dbg(chan->dev, "no running descriptors\n");
		goto out_unlock;
	}

	/* Get the last completed descriptor, update the cookie to that */
	list_for_each_entry(desc, &chan->active_list, node) {
		if (chan->has_SG) {
			hw = &desc->hw;

			/* If a BD has no status bits set, hw has it */
			if (!(hw->status & XILINX_CDMA_BD_STS_ALL_MASK)) {
				break;
			} else {
				done = 1;
				cookie = desc->async_tx.cookie;
			}
		} else {
			/* In non-SG mode, all active entries are done */
			done = 1;
			cookie = desc->async_tx.cookie;
		}
	}

	if (done)
		chan->completed_cookie = cookie;

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/* Reset hardware */
static int cdma_init(struct xilinx_cdma_chan *chan)
{
	int loop = XILINX_CDMA_RESET_LOOP;
	u32 tmp;

	CDMA_OUT(&chan->regs->cr,
		CDMA_IN(&chan->regs->cr) | XILINX_CDMA_CR_RESET_MASK);

	tmp = CDMA_IN(&chan->regs->cr) & XILINX_CDMA_CR_RESET_MASK;

	/* Wait for the hardware to finish reset */
	while (loop && tmp) {
		tmp = CDMA_IN(&chan->regs->cr) & XILINX_CDMA_CR_RESET_MASK;
		loop -= 1;
	}

	if (!loop) {
		dev_err(chan->dev, "reset timeout, cr %x, sr %x\n",
			CDMA_IN(&chan->regs->cr), CDMA_IN(&chan->regs->sr));
		return 1;
	}

	/* For Axi CDMA, always do sg transfers if sg mode is built in */
	if ((chan->feature & XILINX_DMA_IP_CDMA) && chan->has_SG)
		CDMA_OUT(&chan->regs->cr, tmp | XILINX_CDMA_CR_SGMODE_MASK);

	return 0;
}


static irqreturn_t cdma_intr_handler(int irq, void *data)
{
	struct xilinx_cdma_chan *chan = data;
	int update_cookie = 0;
	int to_transfer = 0;
	u32 stat, reg;

	reg = CDMA_IN(&chan->regs->cr);

	/* Disable intr */
	CDMA_OUT(&chan->regs->cr,
		reg & ~XILINX_CDMA_XR_IRQ_ALL_MASK);

	stat = CDMA_IN(&chan->regs->sr);
	if (!(stat & XILINX_CDMA_XR_IRQ_ALL_MASK))
		return IRQ_NONE;

	/* Ack the interrupts */
	CDMA_OUT(&chan->regs->sr, XILINX_CDMA_XR_IRQ_ALL_MASK);

	/* Check for only the interrupts which are enabled */
	stat &= (reg & XILINX_CDMA_XR_IRQ_ALL_MASK);

	if (stat & XILINX_CDMA_XR_IRQ_ERROR_MASK) {
		dev_err(chan->dev,
			"Channel %x has errors %x, cdr %x tdr %x\n",
			(unsigned int)chan,
			(unsigned int)CDMA_IN(&chan->regs->sr),
			(unsigned int)CDMA_IN(&chan->regs->cdr),
			(unsigned int)CDMA_IN(&chan->regs->tdr));
		chan->err = 1;
	}

	/*
	 * Device takes too long to do the transfer when user requires
	 * responsiveness
	 */
	if (stat & XILINX_CDMA_XR_IRQ_DELAY_MASK)
		dev_dbg(chan->dev, "Inter-packet latency too long\n");

	if (stat & XILINX_CDMA_XR_IRQ_IOC_MASK) {
		update_cookie = 1;
		to_transfer = 1;
	}

	if (update_cookie)
		xilinx_cdma_update_completed_cookie(chan);

	if (to_transfer)
		chan->start_transfer(chan);

	tasklet_schedule(&chan->tasklet);
	return IRQ_HANDLED;
}

static void cdma_do_tasklet(unsigned long data)
{
	struct xilinx_cdma_chan *chan = (struct xilinx_cdma_chan *)data;

	xilinx_cdma_chan_desc_cleanup(chan);
}

/* Append the descriptor list to the pending list */
static void append_desc_queue(struct xilinx_cdma_chan *chan,
			struct xilinx_cdma_desc_sw *desc)
{
	struct xilinx_cdma_desc_sw *tail = container_of(chan->pending_list.prev,
					struct xilinx_cdma_desc_sw, node);
	struct xilinx_cdma_desc_hw *hw;

	if (list_empty(&chan->pending_list))
		goto out_splice;

	/*
	 * Add the hardware descriptor to the chain of hardware descriptors
	 * that already exists in memory.
	 */
	hw = &(tail->hw);
	hw->next_desc = (u32)desc->async_tx.phys;

	/*
	 * Add the software descriptor and all children to the list
	 * of pending transactions
	 */
out_splice:
	list_splice_tail_init(&desc->tx_list, &chan->pending_list);
}

/*
 * Assign cookie to each descriptor, and append the descriptors to the pending
 * list
 */
static dma_cookie_t xilinx_cdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(tx->chan);
	struct xilinx_cdma_desc_sw *desc = container_of(tx,
				struct xilinx_cdma_desc_sw, async_tx);
	struct xilinx_cdma_desc_sw *child;
	unsigned long flags;
	dma_cookie_t cookie = -EBUSY;

	if (chan->err) {
		/*
		 * If reset fails, need to hard reset the system.
		 * Channel is no longer functional
		 */
		if (!cdma_init(chan))
			chan->err = 0;
		else
			return cookie;
	}

	spin_lock_irqsave(&chan->lock, flags);

	/*
	 * assign cookies to all of the software descriptors
	 * that make up this transaction
	 */
	cookie = chan->cookie;
	list_for_each_entry(child, &desc->tx_list, node) {
		cookie++;
		if (cookie < 0)
			cookie = DMA_MIN_COOKIE;

		child->async_tx.cookie = cookie;
	}

	chan->cookie = cookie;

	/* put this transaction onto the tail of the pending queue */
	append_desc_queue(chan, desc);

	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

static struct xilinx_cdma_desc_sw *xilinx_cdma_alloc_descriptor(
					struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_desc_sw *desc;
	dma_addr_t pdesc;

	desc = dma_pool_alloc(chan->desc_pool, GFP_ATOMIC, &pdesc);
	if (!desc) {
		dev_dbg(chan->dev, "out of memory for desc\n");
		return NULL;
	}

	memset(desc, 0, sizeof(*desc));
	INIT_LIST_HEAD(&desc->tx_list);
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_cdma_tx_submit;
	desc->async_tx.phys = pdesc;

	return desc;
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
	struct xilinx_cdma_chan *chan;
	struct xilinx_cdma_desc_sw *first = NULL, *prev = NULL, *new;
	struct xilinx_cdma_desc_hw *hw, *prev_hw;
	size_t copy;
	dma_addr_t src = dma_src;
	dma_addr_t dst = dma_dst;

	if (!dchan)
		return NULL;

	if (!len)
		return NULL;

	chan = to_xilinx_chan(dchan);

	if (chan->err) {

		/*
		 * If reset fails, need to hard reset the system.
		 * Channel is no longer functional
		 */
		if (!cdma_init(chan))
			chan->err = 0;
		else
			return NULL;
	}

	/*
	 * If build does not have Data Realignment Engine (DRE),
	 * src has to be aligned
	 */
	if (!chan->has_DRE) {
		if ((dma_src &
			(chan->feature & XILINX_CDMA_FTR_DATA_WIDTH_MASK)) ||
			(dma_dst &
			(chan->feature & XILINX_CDMA_FTR_DATA_WIDTH_MASK))) {

			dev_err(chan->dev,
				"Src/Dest address not aligned when no DRE\n");

			return NULL;
		}
	}

	do {
		/* Allocate descriptor from DMA pool */
		new = xilinx_cdma_alloc_descriptor(chan);
		if (!new) {
			dev_err(chan->dev,
				"No free memory for link descriptor\n");
			goto fail;
		}

		copy = min_t(size_t, len, chan->max_len);

		/* if lite build, transfer cannot cross page boundary */
		if (chan->is_lite)
			copy = min(copy, (size_t)(PAGE_MASK -
						(src & PAGE_MASK)));

		if (!copy) {
			dev_err(chan->dev,
				"Got zero transfer length for %x\n",
					(unsigned int)src);
			goto fail;
		}

		hw = &(new->hw);
		hw->control =
			(hw->control & ~XILINX_CDMA_MAX_TRANS_LEN) | copy;
		hw->src_addr = src;
		hw->dest_addr = dst;

		if (!first)
			first = new;
		else {
			prev_hw = &(prev->hw);
			prev_hw->next_desc = new->async_tx.phys;
		}

		new->async_tx.cookie = 0;
		async_tx_ack(&new->async_tx);

		prev = new;
		len -= copy;
		src += copy;
		dst += copy;

		/* Insert the descriptor to the list */
		list_add_tail(&new->node, &first->tx_list);
	} while (len);

	/* Link the last BD with the first BD */
	hw->next_desc = first->async_tx.phys;

	new->async_tx.flags = flags; /* client is in control of this ack */
	new->async_tx.cookie = -EBUSY;

	return &first->async_tx;

fail:
	if (!first)
		return NULL;

	xilinx_cdma_free_desc_list_reverse(chan, &first->tx_list);
	return NULL;
}

/* Run-time device configuration for Axi CDMA */
static int xilinx_cdma_device_control(struct dma_chan *dchan,
				enum dma_ctrl_cmd cmd, unsigned long arg)
{
	struct xilinx_cdma_chan *chan;
	unsigned long flags;

	if (!dchan)
		return -EINVAL;

	chan = to_xilinx_chan(dchan);

	if (cmd == DMA_TERMINATE_ALL) {
		spin_lock_irqsave(&chan->lock, flags);

		/* Remove and free all of the descriptors in the lists */
		xilinx_cdma_free_desc_list(chan, &chan->pending_list);
		xilinx_cdma_free_desc_list(chan, &chan->active_list);

		spin_unlock_irqrestore(&chan->lock, flags);
		return 0;
	} else if (cmd == DMA_SLAVE_CONFIG) {
		/*
		 * Configure interrupt coalescing and delay counter
		 * Use value XILINX_CDMA_NO_CHANGE to signal no change
		 */
		struct xilinx_cdma_config *cfg =
				(struct xilinx_cdma_config *)arg;
		u32 reg = CDMA_IN(&chan->regs->cr);

		if (cfg->coalesc <= XILINX_CDMA_COALESCE_MAX) {
			reg &= ~XILINX_CDMA_XR_COALESCE_MASK;
			reg |= cfg->coalesc << XILINX_CDMA_COALESCE_SHIFT;

			chan->config.coalesc = cfg->coalesc;
		}

		if (cfg->delay <= XILINX_CDMA_DELAY_MAX) {
			reg &= ~XILINX_CDMA_XR_DELAY_MASK;
			reg |= cfg->delay << XILINX_CDMA_DELAY_SHIFT;
			chan->config.delay = cfg->delay;
		}

		CDMA_OUT(&chan->regs->cr, reg);

		return 0;
	}

	return -ENXIO;
}

/*
 * Logarithm function to compute alignment shift
 *
 * Only deals with value less than 4096.
 */
static int my_log(int value)
{
	int i = 0;
	while ((1 << i) < value) {
		i++;

		if (i >= 12)
			return 0;
	}

	return i;
}

static void xilinx_cdma_chan_remove(struct xilinx_cdma_chan *chan)
{
	irq_dispose_mapping(chan->irq);
	list_del(&chan->common.device_node);
	kfree(chan);
}

/*
 * Probing channels
 *
 * . Get channel features from the device tree entry
 * . Initialize special channel handling routines
 */
static int __devinit xilinx_cdma_chan_probe(struct xilinx_cdma_device *xdev,
	struct device_node *node, u32 feature)
{
	struct xilinx_cdma_chan *chan;
	int err;
	const __be32 *value;
	u32 width = 0, device_id = 0;

	/* alloc channel */
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		dev_err(xdev->dev, "no free memory for DMA channels!\n");
		err = -ENOMEM;
		goto out_return;
	}

	chan->feature = feature;
	chan->is_lite = 0;
	chan->has_DRE = 0;
	chan->has_SG = 0;
	chan->max_len = XILINX_CDMA_MAX_TRANS_LEN;

	value = of_get_property(node, "xlnx,include-dre", NULL);
	if (value)
		chan->has_DRE = be32_to_cpup(value);

	value = of_get_property(node, "xlnx,datawidth", NULL);
	if (value) {
		width = be32_to_cpup(value) >> 3; /* convert bits to bytes */

		/* If data width is greater than 8 bytes, DRE is not in hw */
		if (width > 8)
			chan->has_DRE = 0;

		chan->feature |= width - 1;
	}

	value = of_get_property(node, "xlnx,device-id", NULL);
	if (value)
		device_id = be32_to_cpup(value);

	chan->direction = DMA_MEM_TO_MEM;
	chan->start_transfer = xilinx_cdma_start_transfer;

	chan->has_SG = (xdev->feature & XILINX_CDMA_FTR_HAS_SG) >>
			XILINX_CDMA_FTR_HAS_SG_SHIFT;

	value = of_get_property(node, "xlnx,lite-mode", NULL);
	if (value) {
		if (be32_to_cpup(value) == 1) {
			chan->is_lite = 1;
			value = of_get_property(node,
					"xlnx,max-burst-len", NULL);
			if (value) {
				if (!width) {
					dev_err(xdev->dev,
						"Lite mode w/o data width property\n");
					goto out_free_chan;
				}
				chan->max_len = width *
					be32_to_cpup(value);
			}
		}
	}

	chan->regs = (struct xcdma_regs *)xdev->regs;
	chan->id = 0;

	/*
	 * Used by dmatest channel matching in slave transfers
	 * Can change it to be a structure to have more matching information
	 */
	chan->private = (chan->direction & 0xFF) |
		(chan->feature & XILINX_DMA_IP_MASK) |
		(device_id << XILINX_CDMA_DEVICE_ID_SHIFT);
	chan->common.private = (void *)&(chan->private);

	if (!chan->has_DRE)
		xdev->common.copy_align = my_log(width);

	chan->dev = xdev->dev;
	xdev->chan[chan->id] = chan;

	tasklet_init(&chan->tasklet, cdma_do_tasklet, (unsigned long)chan);

	/* Initialize the channel */
	if (cdma_init(chan)) {
		dev_err(xdev->dev, "Reset channel failed\n");
		goto out_free_chan;
	}

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->active_list);

	chan->common.device = &xdev->common;

	/* Find the IRQ line, if it exists in the device tree */
	chan->irq = irq_of_parse_and_map(node, 0);
	err = request_irq(chan->irq, cdma_intr_handler, IRQF_SHARED,
				"xilinx-cdma-controller", chan);
	if (err) {
		dev_err(xdev->dev, "unable to request IRQ\n");
		goto out_free_irq;
	}

	/* Add the channel to DMA device channel list */
	list_add_tail(&chan->common.device_node, &xdev->common.channels);
	xdev->common.chancnt++;

	return 0;

out_free_irq:
	irq_dispose_mapping(chan->irq);
out_free_chan:
	kfree(chan);
out_return:
	return err;
}

static int __devinit xilinx_cdma_of_probe(struct platform_device *op)
{
	struct xilinx_cdma_device *xdev;
	struct device_node *child, *node;
	int err;
	int *value;

	dev_info(&op->dev, "Probing xilinx axi cdma engine\n");

	xdev = kzalloc(sizeof(struct xilinx_cdma_device), GFP_KERNEL);
	if (!xdev) {
		dev_err(&op->dev, "Not enough memory for device\n");
		err = -ENOMEM;
		goto out_return;
	}

	xdev->dev = &(op->dev);
	INIT_LIST_HEAD(&xdev->common.channels);

	node = op->dev.of_node;
	xdev->feature = 0;

	/* iomap registers */
	xdev->regs = of_iomap(node, 0);
	if (!xdev->regs) {
		dev_err(&op->dev, "unable to iomap registers\n");
		err = -ENOMEM;
		goto out_free_xdev;
	}

	/* Axi CDMA only does memcpy */
	if (of_device_is_compatible(node, "xlnx,axi-cdma")) {
		xdev->feature |= XILINX_DMA_IP_CDMA;

		value = (int *)of_get_property(node, "xlnx,include-sg",
				NULL);
		if (value) {
			if (be32_to_cpup(value) == 1)
				xdev->feature |= XILINX_CDMA_FTR_HAS_SG;
		}

		dma_cap_set(DMA_MEMCPY, xdev->common.cap_mask);
		xdev->common.device_prep_dma_memcpy = xilinx_cdma_prep_memcpy;
		xdev->common.device_control = xilinx_cdma_device_control;
		xdev->common.device_issue_pending = xilinx_cdma_issue_pending;
	}

	xdev->common.device_alloc_chan_resources =
				xilinx_cdma_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
				xilinx_cdma_free_chan_resources;
	xdev->common.device_tx_status = xilinx_tx_status;
	xdev->common.dev = &op->dev;

	dev_set_drvdata(&op->dev, xdev);

	for_each_child_of_node(node, child) {
		xilinx_cdma_chan_probe(xdev, child, xdev->feature);
	}

	dma_async_device_register(&xdev->common);

	return 0;

out_free_xdev:
	kfree(xdev);

out_return:
	return err;
}

static int __devexit xilinx_cdma_of_remove(struct platform_device *op)
{
	struct xilinx_cdma_device *xdev;
	int i;

	xdev = dev_get_drvdata(&op->dev);
	dma_async_device_unregister(&xdev->common);

	for (i = 0; i < XILINX_CDMA_MAX_CHANS_PER_DEVICE; i++) {
		if (xdev->chan[i])
			xilinx_cdma_chan_remove(xdev->chan[i]);
	}

	iounmap(xdev->regs);
	dev_set_drvdata(&op->dev, NULL);
	kfree(xdev);

	return 0;
}

static const struct of_device_id xilinx_cdma_of_ids[] = {
	{ .compatible = "xlnx,axi-cdma",},
	{}
};

static struct platform_driver xilinx_cdma_of_driver = {
	.driver = {
		.name = "xilinx-cdma",
		.owner = THIS_MODULE,
		.of_match_table = xilinx_cdma_of_ids,
	},
	.probe = xilinx_cdma_of_probe,
	.remove = __devexit_p(xilinx_cdma_of_remove),
};

module_platform_driver(xilinx_cdma_of_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx CDMA driver");
MODULE_LICENSE("GPL v2");

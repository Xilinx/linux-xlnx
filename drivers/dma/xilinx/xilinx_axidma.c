/*
 * DMA driver for Xilinx DMA Engine
 *
 * Copyright (C) 2010 - 2015 Xilinx, Inc. All rights reserved.
 *
 * Based on the Freescale DMA driver.
 *
 * Description:
 *  The AXI DMA, is a soft IP, which provides high-bandwidth Direct Memory
 *  Access between memory and AXI4-Stream-type target peripherals. It can be
 *  configured to have one channel or two channels and if configured as two
 *  channels, one is to transmit data from memory to a device and another is
 *  to receive from a device.
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


/* Register Offsets */
#define XILINX_DMA_REG_CONTROL		0x00
#define XILINX_DMA_REG_STATUS		0x04
#define XILINX_DMA_REG_CURDESC		0x08
#define XILINX_DMA_REG_TAILDESC		0x10
#define XILINX_DMA_REG_SRCADDR		0x18
#define XILINX_DMA_REG_DSTADDR		0x20
#define XILINX_DMA_REG_BTT		0x28

/* General register bits definitions */
#define XILINX_DMA_CR_RUNSTOP_MASK	BIT(0)
#define XILINX_DMA_CR_RESET_MASK	BIT(2)


#define XILINX_DMA_XR_DELAY_MASK	0xFF000000 /* Delay timeout counter */
#define XILINX_DMA_XR_COALESCE_MASK	0x00FF0000 /* Coalesce counter */

#define XILINX_DMA_DELAY_SHIFT		24 /* Delay timeout counter shift */
#define XILINX_DMA_COALESCE_SHIFT	16 /* Coalesce counter shift */
#define XILINX_DMA_SR_HALTED_MASK	BIT(0)
#define XILINX_DMA_SR_IDLE_MASK		BIT(1)

#define XILINX_DMA_DELAY_MAX		0xFF /* Maximum delay counter value */
#define XILINX_DMA_COALESCE_MAX		0xFF /* Max coalescing counter value */
#define XILINX_DMA_XR_IRQ_IOC_MASK	BIT(12)
#define XILINX_DMA_XR_IRQ_DELAY_MASK	BIT(13)
#define XILINX_DMA_XR_IRQ_ERROR_MASK	BIT(14)
#define XILINX_DMA_XR_IRQ_ALL_MASK	GENMASK(14, 12)

#define XILINX_DMA_RX_CHANNEL_OFFSET	0x30 /* S2MM Channel Offset */
/* BD definitions */
#define XILINX_DMA_BD_STS_ALL_MASK	GENMASK(31, 28)
#define XILINX_DMA_BD_SOP		BIT(27)
#define XILINX_DMA_BD_EOP		BIT(26)

/* Hw specific definitions */
#define XILINX_DMA_MAX_CHANS_PER_DEVICE	0x2
#define XILINX_DMA_MAX_TRANS_LEN	GENMASK(22, 0)

/* Delay loop counter to prevent hardware failure */
#define XILINX_DMA_RESET_LOOP		1000000
#define XILINX_DMA_HALT_LOOP		1000000

#if defined(CONFIG_XILINX_DMATEST) || defined(CONFIG_XILINX_DMATEST_MODULE)
# define TEST_DMA_WITH_LOOPBACK
#endif

/* Hardware descriptor */
struct xilinx_dma_desc_hw {
	u32 next_desc;	/* 0x00 */
	u32 pad1;	/* 0x04 */
	u32 buf_addr;	/* 0x08 */
	u32 pad2;	/* 0x0C */
	u32 pad3;	/* 0x10 */
	u32 pad4;	/* 0x14 */
	u32 control;	/* 0x18 */
	u32 status;	/* 0x1C */
	u32 app_0;	/* 0x20 */
	u32 app_1;	/* 0x24 */
	u32 app_2;	/* 0x28 */
	u32 app_3;	/* 0x2C */
	u32 app_4;	/* 0x30 */
} __aligned(64);

/* Software descriptor */
struct xilinx_dma_desc_sw {
	struct xilinx_dma_desc_hw hw;
	struct list_head node;
	struct list_head tx_list;
	struct dma_async_tx_descriptor async_tx;
} __aligned(64);

/* Per DMA specific operations should be embedded in the channel structure */
struct xilinx_dma_chan {
	void __iomem *regs;		/* Control status registers */
	dma_cookie_t completed_cookie;	/* The maximum cookie completed */
	dma_cookie_t cookie;		/* The current cookie */
	spinlock_t lock;		/* Descriptor operation lock */
	bool sg_waiting;		/* Scatter gather transfer waiting */
	struct list_head active_list;	/* Active descriptors */
	struct list_head pending_list;	/* Descriptors waiting */
	struct dma_chan common;		/* DMA common channel */
	struct dma_pool *desc_pool;	/* Descriptors pool */
	struct device *dev;		/* The dma device */
	int irq;			/* Channel IRQ */
	int id;				/* Channel ID */
	enum dma_transfer_direction direction;
					/* Transfer direction */
	int max_len;			/* Maximum data len per transfer */
	bool has_sg;			/* Support scatter transfers */
	bool has_dre;			/* Support unaligned transfers */
	bool err;			/* Channel has errors */
	struct tasklet_struct tasklet;	/* Cleanup work after irq */
	struct xilinx_dma_config config;
					/* Device configuration info */
};

/* DMA Device Structure */
struct xilinx_dma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_dma_chan *chan[XILINX_DMA_MAX_CHANS_PER_DEVICE];
};

#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_dma_chan, common)

/* IO accessors */
static inline void dma_write(struct xilinx_dma_chan *chan, u32 reg, u32 val)
{
	writel(val, chan->regs + reg);
}

static inline u32 dma_read(struct xilinx_dma_chan *chan, u32 reg)
{
	return readl(chan->regs + reg);
}

static int xilinx_dma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);

	/* Has this channel already been allocated? */
	if (chan->desc_pool)
		return 1;

	/*
	 * We need the descriptor to be aligned to 64bytes
	 * for meeting Xilinx DMA specification requirement.
	 */
	chan->desc_pool =
		dma_pool_create("xilinx_dma_desc_pool", chan->dev,
				sizeof(struct xilinx_dma_desc_sw),
				__alignof__(struct xilinx_dma_desc_sw), 0);
	if (!chan->desc_pool) {
		dev_err(chan->dev,
			"unable to allocate channel %d descriptor pool\n",
			chan->id);
		return -ENOMEM;
	}

	chan->completed_cookie = 1;
	chan->cookie = 1;

	/* There is at least one descriptor free to be allocated */
	return 1;
}

static void xilinx_dma_free_desc_list(struct xilinx_dma_chan *chan,
				      struct list_head *list)
{
	struct xilinx_dma_desc_sw *desc, *_desc;

	list_for_each_entry_safe(desc, _desc, list, node) {
		list_del(&desc->node);
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}
}

static void xilinx_dma_free_desc_list_reverse(struct xilinx_dma_chan *chan,
					      struct list_head *list)
{
	struct xilinx_dma_desc_sw *desc, *_desc;

	list_for_each_entry_safe_reverse(desc, _desc, list, node) {
		list_del(&desc->node);
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}
}

static void xilinx_dma_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	dev_dbg(chan->dev, "Free all channel resources.\n");
	spin_lock_irqsave(&chan->lock, flags);
	xilinx_dma_free_desc_list(chan, &chan->active_list);
	xilinx_dma_free_desc_list(chan, &chan->pending_list);
	spin_unlock_irqrestore(&chan->lock, flags);

	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

static enum dma_status xilinx_dma_desc_status(struct xilinx_dma_chan *chan,
					      struct xilinx_dma_desc_sw *desc)
{
	return dma_async_is_complete(desc->async_tx.cookie,
				     chan->completed_cookie,
				     chan->cookie);
}

static void xilinx_chan_desc_cleanup(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_desc_sw *desc;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	while (!list_empty(&chan->active_list)) {
		dma_async_tx_callback callback;
		void *callback_param;

		desc = list_first_entry(&chan->active_list,
			struct xilinx_dma_desc_sw, node);

		if (xilinx_dma_desc_status(chan, desc) == DMA_IN_PROGRESS)
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
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;

	xilinx_chan_desc_cleanup(chan);

	last_used = dchan->cookie;
	last_complete = chan->completed_cookie;

	dma_set_tx_state(txstate, last_complete, last_used, 0);

	return dma_async_is_complete(cookie, last_complete, last_used);
}

/**
 * dma_is_running - Check if DMA channel is running
 * @chan: Driver specific DMA channel
 *
 * Return: 'true' if running, 'false' if not.
 */
static bool dma_is_running(struct xilinx_dma_chan *chan)
{
	return !(dma_read(chan, XILINX_DMA_REG_STATUS) &
		 XILINX_DMA_SR_HALTED_MASK) &&
	       (dma_read(chan, XILINX_DMA_REG_CONTROL) &
		XILINX_DMA_CR_RUNSTOP_MASK);
}

/**
 * dma_is_idle - Check if DMA channel is idle
 * @chan: Driver specific DMA channel
 *
 * Return: 'true' if idle, 'false' if not.
 */
static bool dma_is_idle(struct xilinx_dma_chan *chan)
{
	return dma_read(chan, XILINX_DMA_REG_STATUS) &
	       XILINX_DMA_SR_IDLE_MASK;
}

/* Stop the hardware, the ongoing transfer will be finished */
static void dma_halt(struct xilinx_dma_chan *chan)
{
	int loop = XILINX_DMA_HALT_LOOP;

	dma_write(chan, XILINX_DMA_REG_CONTROL,
		  dma_read(chan, XILINX_DMA_REG_CONTROL) &
		  ~XILINX_DMA_CR_RUNSTOP_MASK);

	/* Wait for the hardware to halt */
	do {
		if (dma_read(chan, XILINX_DMA_REG_STATUS) &
		      XILINX_DMA_SR_HALTED_MASK)
			break;
	} while (loop--);

	if (!loop) {
		dev_err(chan->dev, "Cannot stop channel %p: %x\n",
			chan, dma_read(chan, XILINX_DMA_REG_CONTROL));
		chan->err = true;
	}
}

/* Start the hardware. Transfers are not started yet */
static void dma_start(struct xilinx_dma_chan *chan)
{
	int loop = XILINX_DMA_HALT_LOOP;

	dma_write(chan, XILINX_DMA_REG_CONTROL,
		  dma_read(chan, XILINX_DMA_REG_CONTROL) |
		  XILINX_DMA_CR_RUNSTOP_MASK);

	/* Wait for the hardware to start */
	do {
		if (!(dma_read(chan, XILINX_DMA_REG_STATUS) &
		    XILINX_DMA_SR_HALTED_MASK))
			break;
	} while (loop--);

	if (!loop) {
		dev_err(chan->dev, "Cannot start channel %p: %x\n",
			 chan, dma_read(chan, XILINX_DMA_REG_CONTROL));

		chan->err = true;
	}
}

static void xilinx_dma_start_transfer(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_desc_sw *desch, *desct;
	struct xilinx_dma_desc_hw *hw;

	if (chan->err)
		return;

	if (list_empty(&chan->pending_list))
		return;

	/* If hardware is busy, cannot submit */
	if (dma_is_running(chan) && !dma_is_idle(chan)) {
		dev_dbg(chan->dev, "DMA controller still busy\n");
		return;
	}

	/*
	 * If hardware is idle, then all descriptors on active list are
	 * done, start new transfers
	 */
	dma_halt(chan);

	if (chan->err)
		return;

	if (chan->has_sg) {
		desch = list_first_entry(&chan->pending_list,
					 struct xilinx_dma_desc_sw, node);

		desct = list_last_entry(&chan->pending_list,
				     struct xilinx_dma_desc_sw, node);

		dma_write(chan, XILINX_DMA_REG_CURDESC, desch->async_tx.phys);

		dma_start(chan);

		if (chan->err)
			return;
		list_splice_tail_init(&chan->pending_list, &chan->active_list);

		/* Update tail ptr register and start the transfer */
		dma_write(chan, XILINX_DMA_REG_TAILDESC, desct->async_tx.phys);
	} else {
		desch = list_first_entry(&chan->pending_list,
					 struct xilinx_dma_desc_sw, node);

		list_del(&desch->node);
		list_add_tail(&desch->node, &chan->active_list);

		dma_start(chan);

		if (chan->err)
			return;

		hw = &desch->hw;

		dma_write(chan, XILINX_DMA_REG_SRCADDR, hw->buf_addr);

		/* Start the transfer */
		dma_write(chan, XILINX_DMA_REG_BTT,
			  hw->control & XILINX_DMA_MAX_TRANS_LEN);
	}
}

static void xilinx_dma_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_dma_start_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_update_completed_cookie - Update the completed cookie.
 * @chan : xilinx DMA channel
 *
 * CONTEXT: hardirq
 */
static void xilinx_dma_update_completed_cookie(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_desc_sw *desc = NULL;
	struct xilinx_dma_desc_hw *hw = NULL;

	if (list_empty(&chan->active_list)) {
		dev_dbg(chan->dev, "no running descriptors\n");
		return;
	}

	/* Get the last completed descriptor, update the cookie to that */
	list_for_each_entry(desc, &chan->active_list, node) {
		if (chan->has_sg) {
			hw = &desc->hw;

			/* If a BD has no status bits set, hw has it */
			if (hw->status & XILINX_DMA_BD_STS_ALL_MASK)
				chan->completed_cookie = desc->async_tx.cookie;
			else
				break;
		} else {
			/* In non-SG mode, all active entries are done */
			chan->completed_cookie = desc->async_tx.cookie;
		}
	}
}
/**
 * xilinx_dma_chan_config - Configure DMA Channel IRQThreshold, IRQDelay
 * and enable interrupts
 * @chan: DMA channel
 */
static void xilinx_dma_chan_config(struct xilinx_dma_chan *chan)
{
	u32 reg = dma_read(chan, XILINX_DMA_REG_CONTROL);

	reg &= ~XILINX_DMA_XR_COALESCE_MASK;
	reg |= chan->config.coalesc << XILINX_DMA_COALESCE_SHIFT;

	reg &= ~XILINX_DMA_XR_DELAY_MASK;
	reg |= chan->config.delay << XILINX_DMA_DELAY_SHIFT;

	reg |= XILINX_DMA_XR_IRQ_ALL_MASK;

	dma_write(chan, XILINX_DMA_REG_CONTROL, reg);
}

/* Reset hardware */
static int dma_reset(struct xilinx_dma_chan *chan)
{
	int loop = XILINX_DMA_RESET_LOOP;
	u32 tmp;

	dma_write(chan, XILINX_DMA_REG_CONTROL,
		  dma_read(chan, XILINX_DMA_REG_CONTROL) |
		  XILINX_DMA_CR_RESET_MASK);

	tmp = dma_read(chan, XILINX_DMA_REG_CONTROL) &
	      XILINX_DMA_CR_RESET_MASK;

	/* Wait for the hardware to finish reset */
	while (loop && tmp) {
		tmp = dma_read(chan, XILINX_DMA_REG_CONTROL) &
		      XILINX_DMA_CR_RESET_MASK;
		loop -= 1;
	}

	if (!loop) {
		dev_err(chan->dev, "reset timeout, cr %x, sr %x\n",
			dma_read(chan, XILINX_DMA_REG_CONTROL),
			dma_read(chan, XILINX_DMA_REG_STATUS));
		return -EBUSY;
	}

	return 0;
}

static irqreturn_t dma_intr_handler(int irq, void *data)
{
	struct xilinx_dma_chan *chan = data;
	u32 stat;
	irqreturn_t ret = IRQ_HANDLED;

	spin_lock(&chan->lock);

	stat = dma_read(chan, XILINX_DMA_REG_STATUS);
	if (!(stat & XILINX_DMA_XR_IRQ_ALL_MASK)) {
		ret = IRQ_NONE;
		goto out_unlock;
	}

	/* Ack the interrupts */
	dma_write(chan, XILINX_DMA_REG_STATUS,
		  XILINX_DMA_XR_IRQ_ALL_MASK);

	if (stat & XILINX_DMA_XR_IRQ_ERROR_MASK) {
		dev_err(chan->dev,
			"Channel %x has errors %x, cdr %x tdr %x\n",
			(unsigned int)chan,
			(unsigned int)dma_read(chan, XILINX_DMA_REG_STATUS),
			(unsigned int)dma_read(chan, XILINX_DMA_REG_CURDESC),
			(unsigned int)dma_read(chan, XILINX_DMA_REG_TAILDESC));
		chan->err = true;
	}

	/*
	 * Device takes too long to do the transfer when user requires
	 * responsiveness
	 */
	if (stat & XILINX_DMA_XR_IRQ_DELAY_MASK)
		dev_dbg(chan->dev, "Inter-packet latency too long\n");

	if (stat & XILINX_DMA_XR_IRQ_IOC_MASK) {
		xilinx_dma_update_completed_cookie(chan);
		xilinx_dma_start_transfer(chan);
	}

out_unlock:
	spin_unlock(&chan->lock);

	tasklet_schedule(&chan->tasklet);

	return ret;
}

static void dma_do_tasklet(unsigned long data)
{
	struct xilinx_dma_chan *chan = (struct xilinx_dma_chan *)data;

	xilinx_chan_desc_cleanup(chan);
}

/* Append the descriptor list to the pending list */
static void append_desc_queue(struct xilinx_dma_chan *chan,
			      struct xilinx_dma_desc_sw *desc)
{
	struct xilinx_dma_desc_sw *tail =
		list_last_entry(&chan->pending_list,
			     struct xilinx_dma_desc_sw, node);
	struct xilinx_dma_desc_hw *hw;

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
static dma_cookie_t xilinx_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(tx->chan);
	struct xilinx_dma_desc_sw *desc;
	struct xilinx_dma_desc_sw *child;
	unsigned long flags;
	dma_cookie_t cookie = -EBUSY;

	desc = container_of(tx, struct xilinx_dma_desc_sw, async_tx);

	spin_lock_irqsave(&chan->lock, flags);

	if (chan->err) {
		/*
		 * If reset fails, need to hard reset the system.
		 * Channel is no longer functional
		 */
		if (!dma_reset(chan))
			chan->err = false;
		else
			goto out_unlock;
	}

	/*
	 * Assign cookies to all of the software descriptors
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

	/* Put this transaction onto the tail of the pending queue */
	append_desc_queue(chan, desc);

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

static struct
xilinx_dma_desc_sw *xilinx_dma_alloc_descriptor(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_desc_sw *desc;
	dma_addr_t pdesc;

	desc = dma_pool_alloc(chan->desc_pool, GFP_ATOMIC, &pdesc);
	if (!desc)
		return NULL;

	memset(desc, 0, sizeof(*desc));
	INIT_LIST_HEAD(&desc->tx_list);
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;
	desc->async_tx.phys = pdesc;

	return desc;
}

/**
 * xilinx_dma_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @chan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: transfer ack flags
 */
static struct dma_async_tx_descriptor *xilinx_dma_prep_slave_sg(
	struct dma_chan *dchan, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_transfer_direction direction, unsigned long flags,
	void *context)
{
	struct xilinx_dma_chan *chan;
	struct xilinx_dma_desc_sw *first = NULL, *prev = NULL, *new = NULL;
	struct xilinx_dma_desc_hw *hw = NULL, *prev_hw = NULL;

	size_t copy;

	int i;
	struct scatterlist *sg;
	size_t sg_used;
	dma_addr_t dma_src;

#ifdef TEST_DMA_WITH_LOOPBACK
	int total_len;
#endif
	if (!dchan)
		return NULL;

	chan = to_xilinx_chan(dchan);

	if (chan->direction != direction)
		return NULL;

#ifdef TEST_DMA_WITH_LOOPBACK
	total_len = 0;

	for_each_sg(sgl, sg, sg_len, i) {
		total_len += sg_dma_len(sg);
	}
#endif
	/* Build transactions using information in the scatter gather list */
	for_each_sg(sgl, sg, sg_len, i) {
		sg_used = 0;

		/* Loop until the entire scatterlist entry is used */
		while (sg_used < sg_dma_len(sg)) {

			/* Allocate the link descriptor from DMA pool */
			new = xilinx_dma_alloc_descriptor(chan);
			if (!new) {
				dev_err(chan->dev,
					"No free memory for link descriptor\n");
				goto fail;
			}

			/*
			 * Calculate the maximum number of bytes to transfer,
			 * making sure it is less than the hw limit
			 */
			copy = min((size_t)(sg_dma_len(sg) - sg_used),
				   (size_t)chan->max_len);
			hw = &(new->hw);

			dma_src = sg_dma_address(sg) + sg_used;

			hw->buf_addr = dma_src;

			/* Fill in the descriptor */
			hw->control = copy;

			/*
			 * If this is not the first descriptor, chain the
			 * current descriptor after the previous descriptor
			 *
			 * For the first DMA_MEM_TO_DEV transfer, set SOP
			 */
			if (!first) {
				first = new;
				if (direction == DMA_MEM_TO_DEV) {
					hw->control |= XILINX_DMA_BD_SOP;
#ifdef TEST_DMA_WITH_LOOPBACK
					hw->app_4 = total_len;
#endif
				}
			} else {
				prev_hw = &(prev->hw);
				prev_hw->next_desc = new->async_tx.phys;
			}

			new->async_tx.cookie = 0;
			async_tx_ack(&new->async_tx);

			prev = new;
			sg_used += copy;

			/* Insert the link descriptor into the LD ring */
			list_add_tail(&new->node, &first->tx_list);
		}
	}

	/* Link the last BD with the first BD */
	hw->next_desc = first->async_tx.phys;

	if (direction == DMA_MEM_TO_DEV)
		hw->control |= XILINX_DMA_BD_EOP;

	/* All scatter gather list entries has length == 0 */
	if (!first || !new)
		return NULL;

	new->async_tx.flags = flags;
	new->async_tx.cookie = -EBUSY;

	/* Set EOP to the last link descriptor of new list */
	hw->control |= XILINX_DMA_BD_EOP;

	return &first->async_tx;

fail:
	/*
	 * If first was not set, then we failed to allocate the very first
	 * descriptor, and we're done
	 */
	if (!first)
		return NULL;

	/*
	 * First is set, so all of the descriptors we allocated have been added
	 * to first->tx_list, INCLUDING "first" itself. Therefore we
	 * must traverse the list backwards freeing each descriptor in turn
	 */
	xilinx_dma_free_desc_list_reverse(chan, &first->tx_list);

	return NULL;
}

/* Run-time device configuration for Axi DMA */
static int xilinx_dma_device_control(struct dma_chan *dchan,
				     enum dma_ctrl_cmd cmd, unsigned long arg)
{
	struct xilinx_dma_chan *chan;
	unsigned long flags;

	if (!dchan)
		return -EINVAL;

	chan = to_xilinx_chan(dchan);

	if (cmd == DMA_TERMINATE_ALL) {
		/* Halt the DMA engine */
		dma_halt(chan);

		spin_lock_irqsave(&chan->lock, flags);

		/* Remove and free all of the descriptors in the lists */
		xilinx_dma_free_desc_list(chan, &chan->pending_list);
		xilinx_dma_free_desc_list(chan, &chan->active_list);

		spin_unlock_irqrestore(&chan->lock, flags);
		return 0;
	} else if (cmd == DMA_SLAVE_CONFIG) {
		/*
		 * Configure interrupt coalescing and delay counter
		 * Use value XILINX_DMA_NO_CHANGE to signal no change
		 */
		struct xilinx_dma_config *cfg = (struct xilinx_dma_config *)arg;

		if (cfg->coalesc <= XILINX_DMA_COALESCE_MAX)
			chan->config.coalesc = cfg->coalesc;

		if (cfg->delay <= XILINX_DMA_DELAY_MAX)
			chan->config.delay = cfg->delay;

		xilinx_dma_chan_config(chan);

		return 0;
	} else
		return -ENXIO;
}

static void xilinx_dma_free_channels(struct xilinx_dma_device *xdev)
{
	int i;

	for (i = 0; i < XILINX_DMA_MAX_CHANS_PER_DEVICE; i++) {
		if (!xdev->chan[i])
			continue;

		list_del(&xdev->chan[i]->common.device_node);
		tasklet_kill(&xdev->chan[i]->tasklet);
		irq_dispose_mapping(xdev->chan[i]->irq);
	}
}

/*
 * Probing channels
 *
 * . Get channel features from the device tree entry
 * . Initialize special channel handling routines
 */
static int xilinx_dma_chan_probe(struct xilinx_dma_device *xdev,
				 struct device_node *node)
{
	struct xilinx_dma_chan *chan;
	int err;
	u32 value, width = 0;

	/* alloc channel */
	chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->max_len = XILINX_DMA_MAX_TRANS_LEN;
	chan->config.coalesc = 0x01;

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
	}

	if (of_device_is_compatible(node, "xlnx,axi-dma-mm2s-channel")) {
		chan->regs = xdev->regs;
		chan->id = 0;
		chan->direction = DMA_MEM_TO_DEV;
	} else if (of_device_is_compatible(node, "xlnx,axi-dma-s2mm-channel")) {
		chan->regs = (xdev->regs + XILINX_DMA_RX_CHANNEL_OFFSET);
		chan->id = 1;
		chan->direction = DMA_DEV_TO_MEM;
	} else {
		dev_err(xdev->dev, "Invalid channel compatible node\n");
		return -EINVAL;
	}

	if (!chan->has_dre)
		xdev->common.copy_align = fls(width - 1);

	chan->dev = xdev->dev;
	xdev->chan[chan->id] = chan;

	/* Initialize the channel */
	err = dma_reset(chan);
	if (err) {
		dev_err(xdev->dev, "Reset channel failed\n");
		return err;
	}

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->active_list);

	chan->common.device = &xdev->common;

	/* find the IRQ line, if it exists in the device tree */
	chan->irq = irq_of_parse_and_map(node, 0);
	err = devm_request_irq(xdev->dev, chan->irq, dma_intr_handler,
			       IRQF_SHARED,
			       "xilinx-dma-controller", chan);
	if (err) {
		dev_err(xdev->dev, "unable to request IRQ\n");
		return err;
	}

	tasklet_init(&chan->tasklet, dma_do_tasklet, (unsigned long)chan);

	/* Add the channel to DMA device channel list */
	list_add_tail(&chan->common.device_node, &xdev->common.channels);

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
	struct xilinx_dma_device *xdev = ofdma->of_dma_data;
	int chan_id = dma_spec->args[0];

	if (chan_id >= XILINX_DMA_MAX_CHANS_PER_DEVICE)
		return NULL;

	return dma_get_slave_channel(&xdev->chan[chan_id]->common);
}

static int xilinx_dma_probe(struct platform_device *pdev)
{
	struct xilinx_dma_device *xdev;
	struct device_node *child, *node;
	struct resource *res;
	int ret;
	bool has_sg;
	unsigned int i;

	node = pdev->dev.of_node;

	if (of_get_child_count(node) == 0) {
		dev_err(&pdev->dev, "no channels defined\n");
		return -ENODEV;
	}

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &(pdev->dev);
	INIT_LIST_HEAD(&xdev->common.channels);

	/* iomap registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xdev->regs))
		return PTR_ERR(xdev->regs);

	/* Check if SG is enabled */
	has_sg = of_property_read_bool(node, "xlnx,include-sg");

	/* Axi DMA only do slave transfers */
	dma_cap_set(DMA_SLAVE, xdev->common.cap_mask);
	dma_cap_set(DMA_PRIVATE, xdev->common.cap_mask);
	xdev->common.device_prep_slave_sg = xilinx_dma_prep_slave_sg;
	xdev->common.device_control = xilinx_dma_device_control;
	xdev->common.device_issue_pending = xilinx_dma_issue_pending;
	xdev->common.device_alloc_chan_resources =
		xilinx_dma_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
		xilinx_dma_free_chan_resources;
	xdev->common.device_tx_status = xilinx_tx_status;
	xdev->common.dev = &pdev->dev;

	platform_set_drvdata(pdev, xdev);

	for_each_child_of_node(node, child) {
		ret = xilinx_dma_chan_probe(xdev, child);
		if (ret) {
			dev_err(&pdev->dev, "Probing channels failed\n");
			goto free_chan_resources;
		}
	}

	for (i = 0; i < XILINX_DMA_MAX_CHANS_PER_DEVICE; ++i) {
		if (xdev->chan[i]) {
			xdev->chan[i]->has_sg = has_sg;
			xilinx_dma_chan_config(xdev->chan[i]);
		}
	}

	dma_async_device_register(&xdev->common);

	ret = of_dma_controller_register(node, of_dma_xilinx_xlate, xdev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to register DMA to DT\n");
		dma_async_device_unregister(&xdev->common);
		goto free_chan_resources;
	}

	dev_info(&pdev->dev, "Probing xilinx axi dma engine...Successful\n");

	return 0;

free_chan_resources:
	xilinx_dma_free_channels(xdev);

	return ret;
}

static int xilinx_dma_remove(struct platform_device *pdev)
{
	struct xilinx_dma_device *xdev;

	xdev = platform_get_drvdata(pdev);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&xdev->common);

	xilinx_dma_free_channels(xdev);

	return 0;
}

static const struct of_device_id xilinx_dma_of_match[] = {
	{ .compatible = "xlnx,axi-dma-1.00.a",},
	{}
};
MODULE_DEVICE_TABLE(of, xilinx_dma_of_match);

static struct platform_driver xilinx_dma_driver = {
	.driver = {
		.name = "xilinx-dma",
		.of_match_table = xilinx_dma_of_match,
	},
	.probe = xilinx_dma_probe,
	.remove = xilinx_dma_remove,
};

module_platform_driver(xilinx_dma_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx DMA driver");
MODULE_LICENSE("GPL v2");

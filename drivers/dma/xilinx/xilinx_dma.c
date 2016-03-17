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

#include <linux/dma/xilinx_dma.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/dmapool.h>

#include "../dmaengine.h"

/* Register Offsets */
#define XILINX_DMA_REG_CONTROL		0x00
#define XILINX_DMA_REG_STATUS		0x04
#define XILINX_DMA_REG_CURDESC		0x08
#define XILINX_DMA_REG_CURDESCMSB	0x0C
#define XILINX_DMA_REG_TAILDESC		0x10
#define XILINX_DMA_REG_TAILDESCMSB	0x14
#define XILINX_DMA_REG_SRCDSTADDR	0x18
#define XILINX_DMA_REG_SRCDSTADDRMSB	0x1C
#define XILINX_DMA_REG_BTT		0x28

/* Channel/Descriptor Offsets */
#define XILINX_DMA_MM2S_CTRL_OFFSET	0x00
#define XILINX_DMA_S2MM_CTRL_OFFSET	0x30

/* General register bits definitions */
#define XILINX_DMA_CR_RUNSTOP_MASK	BIT(0)
#define XILINX_DMA_CR_RESET_MASK	BIT(2)
#define XILINX_DMA_CR_CYCLIC_BD_EN_MASK	BIT(4)

#define XILINX_DMA_CR_DELAY_SHIFT	24
#define XILINX_DMA_CR_COALESCE_SHIFT	16

#define XILINX_DMA_CR_DELAY_MAX		GENMASK(31, 24)
#define XILINX_DMA_CR_COALESCE_MAX	GENMASK(23, 16)

#define XILINX_DMA_SR_HALTED_MASK	BIT(0)
#define XILINX_DMA_SR_IDLE_MASK		BIT(1)
#define XILINX_DMA_SR_SG_MASK		BIT(3)

#define XILINX_DMA_XR_IRQ_IOC_MASK	BIT(12)
#define XILINX_DMA_XR_IRQ_DELAY_MASK	BIT(13)
#define XILINX_DMA_XR_IRQ_ERROR_MASK	BIT(14)
#define XILINX_DMA_XR_IRQ_ALL_MASK	GENMASK(14, 12)

/* BD definitions */
#define XILINX_DMA_BD_STS_ALL_MASK	GENMASK(31, 28)
#define XILINX_DMA_BD_SOP		BIT(27)
#define XILINX_DMA_BD_EOP		BIT(26)
#define XILINX_DMA_BD_CMPLT		BIT(31)

/* Multi-Channel DMA Descriptor offsets*/
#define XILINX_DMA_MCRX_CDESC(x)	(0x40 + (x-1) * 0x20)
#define XILINX_DMA_MCRX_TDESC(x)	(0x48 + (x-1) * 0x20)

#define XILINX_DMA_BD_HSIZE_MASK    GENMASK(15, 0)
#define XILINX_DMA_BD_STRIDE_MASK   GENMASK(15, 0)
#define XILINX_DMA_BD_VSIZE_MASK    GENMASK(31, 19)

#define XILINX_DMA_BD_STRIDE_SHIFT   0
#define XILINX_DMA_BD_VSIZE_SHIFT    19

/* Hw specific definitions */
#define XILINX_DMA_MAX_CHANS_PER_DEVICE	0x20
#define XILINX_DMA_MAX_TRANS_LEN	GENMASK(22, 0)

/* Delay loop counter to prevent hardware failure */
#define XILINX_DMA_LOOP_COUNT		1000000

#define XILINX_DMA_NUM_APP_WORDS	5

#define mm2s_mcdmatx_control(tdest, tid, tuser, axcache, aruser) \
			     ((aruser << 28) | (axcache << 24) | \
			     (tuser << 16) | (tid << 8) | (tdest))

#define mm2s_mcdmarx_control(axcache, aruser) \
			     ((aruser << 28) | (axcache << 24))

#define xilinx_dma_poll_timeout(chan, reg, val, cond, delay_us, timeout_us) \
	readl_poll_timeout_atomic(chan->xdev->regs + chan->ctrl_offset + reg, val, \
			   cond, delay_us, timeout_us)

/**
 * struct xilinx_dma_desc_hw - Hardware Descriptor
 * @next_desc: Next Descriptor Pointer @0x00
 * @next_desc_msb: MSB of Next Descriptor Pointer @0x04
 * @buf_addr: Buffer address @0x08
 * @buf_addr_msb: MSB of Buffer address @0x0C
 * @pad1: Reserved @0x10
 * @pad2: Reserved @0x14
 * @control: Control field @0x18
 * @status: Status field @0x1C
 * @app: APP Fields @0x20 - 0x30
 */
struct xilinx_dma_desc_hw {
	u32 next_desc;
	u32 next_desc_msb;
	u32 buf_addr;
	u32 buf_addr_msb;
	u32 mcdma_fields;
	u32 vsize_stride;
	u32 control;
	u32 status;
	u32 app[XILINX_DMA_NUM_APP_WORDS];
} __aligned(64);

/**
 * struct xilinx_dma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_dma_tx_segment {
	struct xilinx_dma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
} __aligned(64);

/**
 * struct xilinx_dma_tx_descriptor - Per Transaction structure
 * @async_tx: Async transaction descriptor
 * @segments: TX segments list
 * @node: Node in the channel descriptors list
 * @direction: Transfer direction
 */
struct xilinx_dma_tx_descriptor {
	struct dma_async_tx_descriptor async_tx;
	struct list_head segments;
	struct list_head node;
	enum dma_transfer_direction direction;
	bool cyclic;
};

/**
 * struct xilinx_dma_chan - Driver specific DMA channel structure
 * @xdev: Driver specific device structure
 * @ctrl_offset: Control registers offset
 * @ctrl_reg: Control register value
 * @lock: Descriptor operation lock
 * @pending_list: Descriptors waiting
 * @active_list: Descriptors ready to submit
 * @done_list: Complete descriptors
 * @common: DMA common channel
 * @seg_v: Statically allocated segments base
 * @seg_p: Physical allocated segments base
 * @dev: The dma device
 * @irq: Channel IRQ
 * @id: Channel ID
 * @has_sg: Support scatter transfers
 * @idle: Check for channel idle
 * @err: Channel has errors
 * @tasklet: Cleanup work after irq
 * @residue: Residue
 * @seg_pool: DMA Segment (Buffer Descriptor) Pool
 * @seg_reserve: Extra allocated segment.
 */
struct xilinx_dma_chan {
	struct xilinx_dma_device *xdev;
	u32 ctrl_offset;
	u32 ctrl_reg;
	spinlock_t lock;
	struct list_head pending_list;
	struct list_head done_list;
	struct list_head active_list;
	struct dma_chan common;
	struct xilinx_dma_tx_segment *seg_v;
	struct xilinx_mcdma_config config;
	dma_addr_t seg_p;
	struct device *dev;
	int irq;
	int id;
	bool has_sg;
	bool cyclic;
	bool mcdma;
	int err;
	bool idle;
	struct tasklet_struct tasklet;
	u32 residue;
	struct dma_pool *seg_pool;

	/* seg_reserve: For non-cyclic mode, after submitting a pending_list, keep
	 * an extra segment allocated so that the "next descriptor" pointer on the
	 * tail descriptor always points to a valid descriptor, even when paused
	 * after reaching taildesc.  This way, it is possible to issue additional
	 * transfers without halting and restarting the channel.
	 */
	struct xilinx_dma_tx_segment *seg_reserve;
};

/**
 * struct xilinx_dma_device - DMA device structure
 * @regs: I/O mapped base address
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific DMA channel
 */
struct xilinx_dma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_dma_chan *chan[XILINX_DMA_MAX_CHANS_PER_DEVICE];
	bool mcdma;
	u32 nr_channels;
	u32 chan_id;
};

/* Macros */
#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_dma_chan, common)
#define to_dma_tx_descriptor(tx) \
	container_of(tx, struct xilinx_dma_tx_descriptor, async_tx)

/* IO accessors */
static inline void dma_write(struct xilinx_dma_chan *chan, u32 reg, u32 value)
{
	iowrite32(value, chan->xdev->regs + reg);
}

#if defined(CONFIG_PHYS_ADDR_T_64BIT)
static inline void dma_writeq(struct xilinx_dma_chan *chan, u32 reg, u64 value)
{
	writeq(value, chan->xdev->regs + reg);
}
#endif

static inline u32 dma_read(struct xilinx_dma_chan *chan, u32 reg)
{
	return ioread32(chan->xdev->regs + reg);
}

static inline u32 dma_ctrl_read(struct xilinx_dma_chan *chan, u32 reg)
{
	return dma_read(chan, chan->ctrl_offset + reg);
}

static inline void dma_ctrl_write(struct xilinx_dma_chan *chan, u32 reg,
				  u32 value)
{
	dma_write(chan, chan->ctrl_offset + reg, value);
}

#if defined(CONFIG_PHYS_ADDR_T_64BIT)
static inline void dma_ctrl_writeq(struct xilinx_dma_chan *chan, u32 reg,
				   u64 value)
{
	dma_writeq(chan, chan->ctrl_offset + reg, value);
}
#endif

/* -----------------------------------------------------------------------------
 * Descriptors and segments alloc and free
 */

/**
 * xilinx_dma_alloc_tx_segment - Allocate transaction segment
 * @chan: Driver specific dma channel
 *
 * Return: The allocated segment on success and NULL on failure.
 */
static struct xilinx_dma_tx_segment *
xilinx_dma_alloc_tx_segment(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_segment *segment = NULL;
	dma_addr_t pdesc;

	gfp_t gfp_flags = GFP_ATOMIC;

	segment = dma_pool_alloc(chan->seg_pool, gfp_flags, &pdesc);
	if (!segment)
		return NULL;

	memset(segment, 0, sizeof(*segment));
	INIT_LIST_HEAD(&segment->node);
	segment->phys = pdesc;

	return segment;
}

/**
 * xilinx_dma_free_tx_segment - Free transaction segment
 * @chan: Driver specific dma channel
 * @segment: dma transaction segment
 */
static void xilinx_dma_free_tx_segment(struct xilinx_dma_chan *chan,
				       struct xilinx_dma_tx_segment *segment)
{
	dma_pool_free(chan->seg_pool, segment, segment->phys);
}

/**
 * xilinx_dma_tx_descriptor - Allocate transaction descriptor
 * @chan: Driver specific dma channel
 *
 * Return: The allocated descriptor on success and NULL on failure.
 */
static struct xilinx_dma_tx_descriptor *
xilinx_dma_alloc_tx_descriptor(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *desc;

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (!desc)
		return NULL;

	INIT_LIST_HEAD(&desc->segments);

	return desc;
}

/**
 * xilinx_dma_free_tx_descriptor - Free transaction descriptor
 * @chan: Driver specific dma channel
 * @desc: dma transaction descriptor
 */
static void
xilinx_dma_free_tx_descriptor(struct xilinx_dma_chan *chan,
	struct xilinx_dma_tx_descriptor *desc)
{
	struct xilinx_dma_tx_segment *segment, *next;

	if (!desc)
		return;

	list_for_each_entry_safe(segment, next, &desc->segments, node) {
		list_del(&segment->node);
		xilinx_dma_free_tx_segment(chan, segment);
	}

	kfree(desc);
}

/**
 * xilinx_dma_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);

	chan->seg_pool =
		dma_pool_create("xilinx_dma_seg_pool", chan->dev,
				sizeof(struct xilinx_dma_tx_segment),
				__alignof__(struct xilinx_dma_tx_segment), 0);

	if (!chan->seg_pool) {
		dev_err(chan->dev,
			"unable to allocate channel %d descriptor pool\n",
			chan->id);
		return -ENOMEM;
	}

	BUG_ON(chan->seg_reserve);
	chan->seg_reserve = xilinx_dma_alloc_tx_segment(chan);

	if (!chan->seg_reserve) {
		dev_err(chan->dev,
			"unable to allocate segment from new descriptor pool\n");
		dma_pool_destroy( chan->seg_pool );
		chan->seg_pool = NULL;

		return -ENOMEM;
	}

	dma_cookie_init(dchan);

	/* Enable interrupts */
	chan->ctrl_reg |= XILINX_DMA_XR_IRQ_ALL_MASK;
	dma_ctrl_write(chan, XILINX_DMA_REG_CONTROL, chan->ctrl_reg);

	return 0;
}

/**
 * xilinx_dma_free_desc_list - Free descriptors list
 * @chan: Driver specific dma channel
 * @list: List to parse and delete the descriptor
 */
static void xilinx_dma_free_desc_list(struct xilinx_dma_chan *chan,
				      struct list_head *list)
{
	struct xilinx_dma_tx_descriptor *desc, *next;

	list_for_each_entry_safe(desc, next, list, node) {
		list_del(&desc->node);
		xilinx_dma_free_tx_descriptor(chan, desc);
	}
}

/**
 * xilinx_dma_free_descriptors - Free channel descriptors
 * @chan: Driver specific dma channel
 */
static void xilinx_dma_free_descriptors(struct xilinx_dma_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	xilinx_dma_free_desc_list(chan, &chan->pending_list);
	xilinx_dma_free_desc_list(chan, &chan->done_list);
	xilinx_dma_free_desc_list(chan, &chan->active_list);

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_free_chan_resources - Free channel resources
 * @dchan: DMA channel
 */
static void xilinx_dma_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	struct dma_pool *pool_to_free = NULL;
	struct xilinx_dma_tx_segment *seg_to_free = NULL;

	xilinx_dma_free_descriptors(chan);

	/* Free memory that was allocated for the segments (from non-atomic context) */
	spin_lock_irqsave(&chan->lock, flags);

	seg_to_free = chan->seg_reserve;
	chan->seg_reserve = NULL;

	pool_to_free = chan->seg_pool;
	chan->seg_pool = NULL;

	spin_unlock_irqrestore(&chan->lock, flags);

	if (pool_to_free) {
		if (seg_to_free) {
			/* no longer safe to call xilinx_dma_free_tx_segment() here */
			dma_pool_free(pool_to_free, seg_to_free, seg_to_free->phys);
		}
		dma_pool_destroy(pool_to_free);
	}
}

/**
 * xilinx_dma_chan_handle_cyclic - Cyclic dma callback
 * @chan: Driver specific dma channel
 * @desc: dma transaction descriptor
 * @flags: flags for spin lock
 */
static void xilinx_dma_chan_handle_cyclic(struct xilinx_dma_chan *chan,
					  struct xilinx_dma_tx_descriptor *desc,
					  unsigned long *flags)
{
	dma_async_tx_callback callback;
	void *callback_param;

	callback = desc->async_tx.callback;
	callback_param = desc->async_tx.callback_param;
	if (callback) {
		spin_unlock_irqrestore(&chan->lock, *flags);
		callback(callback_param);
		spin_lock_irqsave(&chan->lock, *flags);
	}
}


/**
 * xilinx_dma_chan_desc_cleanup - Clean channel descriptors
 * @chan: Driver specific dma channel
 */
static void xilinx_dma_chan_desc_cleanup(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *desc;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	while (!list_empty(&chan->done_list)) {
		dma_async_tx_callback callback;
		void *callback_param;

		desc = list_first_entry(&chan->done_list,
			struct xilinx_dma_tx_descriptor, node);

		if (desc->cyclic) {
			xilinx_dma_chan_handle_cyclic(chan, desc, &flags);
			break;
		}

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
		xilinx_dma_free_tx_descriptor(chan, desc);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_tx_status - Get dma transaction status
 * @dchan: DMA channel
 * @cookie: Transaction identifier
 * @txstate: Transaction state
 *
 * Return: DMA transaction status
 */
static enum dma_status xilinx_dma_tx_status(struct dma_chan *dchan,
					    dma_cookie_t cookie,
					    struct dma_tx_state *txstate)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_descriptor *desc;
	struct xilinx_dma_tx_segment *segment;
	struct xilinx_dma_desc_hw *hw;
	enum dma_status ret;
	unsigned long flags;
	u32 residue = 0;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	desc = list_last_entry(&chan->active_list,
			       struct xilinx_dma_tx_descriptor, node);

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->has_sg) {
		list_for_each_entry(segment, &desc->segments, node) {
			hw = &segment->hw;
			residue += (hw->control - hw->status) &
				   XILINX_DMA_MAX_TRANS_LEN;
		}
	}
	spin_unlock_irqrestore(&chan->lock, flags);

	chan->residue = residue;
	dma_set_residue(txstate, chan->residue);

	return ret;
}

/**
 * xilinx_dma_halt - Halt DMA channel
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_halt(struct xilinx_dma_chan *chan)
{
	int err = 0;
	u32 val;

	chan->ctrl_reg &= ~XILINX_DMA_CR_RUNSTOP_MASK;
	dma_ctrl_write(chan, XILINX_DMA_REG_CONTROL, chan->ctrl_reg);

	/* Wait for the hardware to halt */
	err = xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_STATUS, val,
				      (val & XILINX_DMA_SR_HALTED_MASK), 10,
				      XILINX_DMA_LOOP_COUNT);

	if (err) {
		dev_err(chan->dev, "Cannot stop channel %p: %x\n",
			chan, dma_ctrl_read(chan, XILINX_DMA_REG_STATUS));
		chan->err = true;
	} else {
		chan->idle = true;
	}
}

/**
 * xilinx_dma_start - Start DMA channel
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_start(struct xilinx_dma_chan *chan)
{
	int err = 0;
	u32 val;

	chan->ctrl_reg |= XILINX_DMA_CR_RUNSTOP_MASK;
	dma_ctrl_write(chan, XILINX_DMA_REG_CONTROL, chan->ctrl_reg);

	/* Wait for the hardware to start */
	err = xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_STATUS, val,
				      !(val & XILINX_DMA_SR_HALTED_MASK), 10,
				      XILINX_DMA_LOOP_COUNT);

	if (err) {
		dev_err(chan->dev, "Cannot start channel %p: %x\n",
			 chan, dma_ctrl_read(chan, XILINX_DMA_REG_STATUS));
		chan->err = true;
	}
}

/**
 * xilinx_dma_start_transfer - Starts DMA transfer
 * @chan: Driver specific channel struct pointer
 */
static void xilinx_dma_start_transfer(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *head_desc, *tail_desc;
	struct xilinx_dma_tx_segment *tail_segment;
	dma_addr_t head_seg_phys;
	struct xilinx_mcdma_config *config = &chan->config;

	if (chan->err)
		return;

	if (list_empty(&chan->pending_list))
		return;

	if (!chan->idle)
		return;

	head_desc = list_first_entry(&chan->pending_list,
				     struct xilinx_dma_tx_descriptor, node);
	tail_desc = list_last_entry(&chan->pending_list,
				    struct xilinx_dma_tx_descriptor, node);
	tail_segment = list_last_entry(&tail_desc->segments,
				       struct xilinx_dma_tx_segment, node);

	/* If channel is not halted, the tail descriptor's next_desc points to
	 * chan->seg_reserve.  Swap head_segment and chan->seg_reserve, keeping
	 * Buffer Descriptor contents from head_segment. */
	{
	    struct xilinx_dma_tx_segment *old_head, *new_head;

		old_head = list_first_entry(&head_desc->segments,
					struct xilinx_dma_tx_segment, node);
		new_head = chan->seg_reserve;

		/* Copy Buffer Descriptor fields. */
		new_head->hw = old_head->hw;

		/* Swap and save new reserve */
		list_replace_init(&old_head->node, &new_head->node);
		chan->seg_reserve = old_head;

#ifdef CONFIG_PHYS_ADDR_T_64BIT
		tail_segment->hw.next_desc     = lower_32_bits(chan->seg_reserve->phys);
		tail_segment->hw.next_desc_msb = upper_32_bits(chan->seg_reserve->phys);
#else
		tail_segment->hw.next_desc = chan->seg_reserve->phys;
#endif

		head_seg_phys = new_head->phys;
	}

	chan->ctrl_reg &= ~XILINX_DMA_CR_COALESCE_MAX;
	chan->ctrl_reg |= 1 << XILINX_DMA_CR_COALESCE_SHIFT;    // setting IrqThreshold != 1 is unreliable
	dma_ctrl_write(chan, XILINX_DMA_REG_CONTROL, chan->ctrl_reg);


	if (chan->has_sg && !chan->mcdma) {
		BUG_ON(head_seg_phys & 0x3F);
#ifdef CONFIG_PHYS_ADDR_T_64BIT
		dma_ctrl_writeq(chan, XILINX_DMA_REG_CURDESC,
			       head_seg_phys );
#else
		dma_ctrl_write(chan, XILINX_DMA_REG_CURDESC,
			       head_seg_phys);
#endif
	}

	if (chan->has_sg && chan->mcdma) {
		if (head_desc->direction == DMA_MEM_TO_DEV) {
			dma_ctrl_write(chan, XILINX_DMA_REG_CURDESC,
				       head_seg_phys);
		} else {
			if (!config->tdest) {
				dma_ctrl_write(chan, XILINX_DMA_REG_CURDESC,
				       head_seg_phys);
			} else {
				dma_ctrl_write(chan,
					XILINX_DMA_MCRX_CDESC(config->tdest),
				       head_seg_phys);
			}
		}
	}

	xilinx_dma_start(chan);

	if (chan->err)
		return;

	/* Start the transfer */
	if (chan->has_sg && !chan->mcdma) {

		BUG_ON(!tail_segment->phys || tail_segment->phys & 0x3F);

#ifdef CONFIG_PHYS_ADDR_T_64BIT
		dma_ctrl_writeq(chan, XILINX_DMA_REG_TAILDESC,
			       tail_segment->phys);
#else
		dma_ctrl_write(chan, XILINX_DMA_REG_TAILDESC,
			       tail_segment->phys);
#endif
	} else if (chan->has_sg && chan->mcdma) {

		if (head_desc->direction == DMA_MEM_TO_DEV) {
			dma_ctrl_write(chan, XILINX_DMA_REG_TAILDESC,
			       tail_segment->phys);
		} else {
			if (!config->tdest) {
				dma_ctrl_write(chan, XILINX_DMA_REG_TAILDESC,
					       tail_segment->phys);
			} else {
				dma_ctrl_write(chan,
					XILINX_DMA_MCRX_TDESC(config->tdest),
					tail_segment->phys);
			}
		}
	} else {
		struct xilinx_dma_tx_segment *segment;
		struct xilinx_dma_desc_hw *hw;

		segment = list_first_entry(&head_desc->segments,
					   struct xilinx_dma_tx_segment, node);
		hw = &segment->hw;

#ifdef CONFIG_PHYS_ADDR_T_64BIT
		dma_ctrl_writeq(chan, XILINX_DMA_REG_SRCDSTADDR, hw->buf_addr);
#else
		dma_ctrl_write(chan, XILINX_DMA_REG_SRCDSTADDR, hw->buf_addr);
#endif
		/* Start the transfer */
		dma_ctrl_write(chan, XILINX_DMA_REG_BTT,
			       hw->control & XILINX_DMA_MAX_TRANS_LEN);
	}

	list_splice_tail_init(&chan->pending_list, &chan->active_list);
	chan->idle = false;
}

/**
 * xilinx_dma_issue_pending - Issue pending transactions
 * @dchan: DMA channel
 */
static void xilinx_dma_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_dma_start_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_complete_descriptor - Mark the active descriptor as complete
 * @chan : xilinx DMA channel
 */
static void xilinx_dma_complete_descriptor(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *desc, *next;


	list_for_each_entry_safe(desc, next, &chan->active_list, node) {

		/* Check whether the last segment in this descriptor has been completed. */
		const struct xilinx_dma_tx_segment * const tail_segment =
			list_last_entry(&desc->segments, struct xilinx_dma_tx_segment, node);

		if (!(tail_segment->hw.status & XILINX_DMA_BD_CMPLT))
			break;  // we've processed all the completed descriptors so far
		// If we get here, this descriptor has been completed
		list_del(&desc->node);

		if (!desc->cyclic)
			dma_cookie_complete(&desc->async_tx);

		list_add_tail(&desc->node, &chan->done_list);
	}

	if (list_empty(&chan->active_list)) {
		chan->idle = true;
		xilinx_dma_start_transfer(chan);
	}
}

/**
 * xilinx_dma_chan_reset - Reset DMA channel
 * @chan: Driver specific DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_chan_reset(struct xilinx_dma_chan *chan)
{
	int err = 0;
	u32 val;

	chan->ctrl_reg = dma_ctrl_read(chan, XILINX_DMA_REG_CONTROL);
	dma_ctrl_write(chan, XILINX_DMA_REG_CONTROL, chan->ctrl_reg |
		       XILINX_DMA_CR_RESET_MASK);

	/* Wait for the hardware to finish reset */
	err = xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_CONTROL, val,
				      !(val & XILINX_DMA_CR_RESET_MASK), 10,
				      XILINX_DMA_LOOP_COUNT);

	if (err) {
		dev_err(chan->dev, "reset timeout, cr %x, sr %x\n",
			dma_ctrl_read(chan, XILINX_DMA_REG_CONTROL),
			dma_ctrl_read(chan, XILINX_DMA_REG_STATUS));
		return -EBUSY;
	}

	chan->err = false;
	chan->idle = true;

	return err;
}

/**
 * xilinx_dma_irq_handler - DMA Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the Xilinx DMA channel structure
 *
 * Return: IRQ_HANDLED/IRQ_NONE
 */
static irqreturn_t xilinx_dma_irq_handler(int irq, void *data)
{
	struct xilinx_dma_chan *chan = data;
	u32 status;

	/* Read the status and ack the interrupts. */
	status = dma_ctrl_read(chan, XILINX_DMA_REG_STATUS);
	if (!(status & XILINX_DMA_XR_IRQ_ALL_MASK))
		return IRQ_NONE;

	dma_ctrl_write(chan, XILINX_DMA_REG_STATUS,
		       status & XILINX_DMA_XR_IRQ_ALL_MASK);

	if (status & XILINX_DMA_XR_IRQ_ERROR_MASK) {
		dev_err(chan->dev,
			"Channel %p has errors %x cdr %x cdr msb %x tdr %x tdr msb %x",
			chan, status,
			dma_ctrl_read(chan, XILINX_DMA_REG_CURDESC),
			dma_ctrl_read(chan, XILINX_DMA_REG_CURDESCMSB),
			dma_ctrl_read(chan, XILINX_DMA_REG_TAILDESC),
			dma_ctrl_read(chan, XILINX_DMA_REG_TAILDESCMSB));
		chan->err = true;
	}

	/*
	 * Device takes too long to do the transfer when user requires
	 * responsiveness
	 */
	if (status & XILINX_DMA_XR_IRQ_DELAY_MASK)
		dev_dbg(chan->dev, "Inter-packet latency too long\n");

	if (status & XILINX_DMA_XR_IRQ_IOC_MASK) {
		spin_lock(&chan->lock);
		xilinx_dma_complete_descriptor(chan);
		spin_unlock(&chan->lock);
	}

	tasklet_schedule(&chan->tasklet);
	return IRQ_HANDLED;
}

/**
 * xilinx_dma_do_tasklet - Schedule completion tasklet
 * @data: Pointer to the Xilinx dma channel structure
 */
static void xilinx_dma_do_tasklet(unsigned long data)
{
	struct xilinx_dma_chan *chan = (struct xilinx_dma_chan *)data;

	xilinx_dma_chan_desc_cleanup(chan);
}

/**
 * append_desc_queue - Queuing descriptor
 * @chan: Driver specific dma channel
 * @desc: dma transaction descriptor
 */
static void append_desc_queue(struct xilinx_dma_chan *chan,
			      struct xilinx_dma_tx_descriptor *desc)
{
	struct xilinx_dma_tx_segment *tail_segment, *new_head_segment;
	struct xilinx_dma_tx_descriptor *tail_desc;

	if (list_empty(&chan->pending_list))
		goto append;

	/*
	 * Add the hardware descriptor to the chain of hardware descriptors
	 * that already exists in memory.
	 */
	tail_desc = list_last_entry(&chan->pending_list,
				    struct xilinx_dma_tx_descriptor, node);
	tail_segment = list_last_entry(&tail_desc->segments,
				       struct xilinx_dma_tx_segment, node);

	new_head_segment = list_first_entry(&desc->segments,
				       struct xilinx_dma_tx_segment, node);

#ifdef CONFIG_PHYS_ADDR_T_64BIT
	tail_segment->hw.next_desc     = lower_32_bits(new_head_segment->phys);
	tail_segment->hw.next_desc_msb = upper_32_bits(new_head_segment->phys);
#else
	tail_segment->hw.next_desc = new_head_segment->phys;
#endif

	/*
	 * Add the software descriptor and all children to the list
	 * of pending transactions
	 */
append:
	list_add_tail(&desc->node, &chan->pending_list);
}

/**
 * xilinx_dma_tx_submit - Submit DMA transaction
 * @tx: Async transaction descriptor
 *
 * Return: cookie value on success and failure value on error
 */
static dma_cookie_t xilinx_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_dma_tx_descriptor *desc = to_dma_tx_descriptor(tx);
	struct xilinx_dma_chan *chan = to_xilinx_chan(tx->chan);
	dma_cookie_t cookie;
	unsigned long flags;
	int err;

	if (chan->cyclic) {
		xilinx_dma_free_tx_descriptor(chan, desc);
		return -EBUSY;
	}

	if (chan->err) {
		/*
		 * If reset fails, need to hard reset the system.
		 * Channel is no longer functional
		 */
		err = xilinx_dma_chan_reset(chan);
		if (err < 0)
			return err;
	}

	spin_lock_irqsave(&chan->lock, flags);

	cookie = dma_cookie_assign(tx);

	/* Put this transaction onto the tail of the pending queue */
	append_desc_queue(chan, desc);

	if (desc->cyclic)
		chan->cyclic = true;

	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * xilinx_dma_prep_interleaved - prepare a descriptor for a
 *	DMA_SLAVE transaction
 * @dchan: DMA channel
 * @xt: Interleaved template pointer
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xilinx_dma_prep_interleaved(struct dma_chan *dchan,
				 struct dma_interleaved_template *xt,
				 unsigned long flags)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_mcdma_config *config = &chan->config;
	struct xilinx_dma_tx_descriptor *desc;
	struct xilinx_dma_tx_segment *segment;
	struct xilinx_dma_desc_hw *hw;

	if (!is_slave_direction(xt->dir))
		return NULL;

	if (!xt->numf || !xt->sgl[0].size)
		return NULL;

	if (xt->frame_size != 1)
		return NULL;

	/* Allocate a transaction descriptor. */
	desc = xilinx_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	desc->direction = xt->dir;
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;

	/* Get a free segment */
	segment = xilinx_dma_alloc_tx_segment(chan);
	if (!segment)
		goto error;

	hw = &segment->hw;

	/* Fill in the descriptor */
	if (xt->dir != DMA_MEM_TO_DEV) {
#ifdef CONFIG_PHYS_ADDR_T_64BIT
		hw->buf_addr =
			lower_32_bits(xt->dst_start);
		hw->buf_addr_msb =
			upper_32_bits(xt->dst_start);
#else
		hw->buf_addr = xt->dst_start;
#endif
		hw->mcdma_fields = mm2s_mcdmarx_control(config->ax_cache,
							config->ax_user);
	} else {
#ifdef CONFIG_PHYS_ADDR_T_64BIT
		hw->buf_addr =
			lower_32_bits(xt->src_start);
		hw->buf_addr_msb =
			upper_32_bits(xt->src_start);
#else
		hw->buf_addr = xt->src_start;
#endif
		hw->mcdma_fields = mm2s_mcdmatx_control(config->tdest,
							config->tid,
							config->tuser,
							config->ax_cache,
							config->ax_user);
	}

	hw->vsize_stride = (xt->numf << XILINX_DMA_BD_VSIZE_SHIFT) &
			    XILINX_DMA_BD_VSIZE_MASK;
	hw->vsize_stride |= (xt->sgl[0].icg + xt->sgl[0].size) &
			    XILINX_DMA_BD_STRIDE_MASK;
	hw->control = xt->sgl[0].size & XILINX_DMA_BD_HSIZE_MASK;

	/*
	 * Insert the segment into the descriptor segments
	 * list.
	 */
	list_add_tail(&segment->node, &desc->segments);


	segment = list_first_entry(&desc->segments,
				   struct xilinx_dma_tx_segment, node);
	desc->async_tx.phys = segment->phys;

	/* For the last DMA_MEM_TO_DEV transfer, set EOP */
	if (xt->dir == DMA_MEM_TO_DEV) {
		segment->hw.control |= XILINX_DMA_BD_SOP;
		segment = list_last_entry(&desc->segments,
					  struct xilinx_dma_tx_segment,
					  node);
		segment->hw.control |= XILINX_DMA_BD_EOP;
	}

	return &desc->async_tx;

error:
	xilinx_dma_free_tx_descriptor(chan, desc);
	return NULL;
}

/**
 * xilinx_dma_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @dchan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: transfer ack flags
 * @context: APP words of the descriptor
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *xilinx_dma_prep_slave_sg(
	struct dma_chan *dchan, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_transfer_direction direction, unsigned long flags,
	void *context)
{
	size_t copy, sg_used;
	int i;
	struct xilinx_dma_tx_descriptor *desc;
	struct scatterlist *sg;
	struct xilinx_dma_desc_hw *hw;
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_segment *last_segment, *prev_segment, *segment = NULL;
	u32 *app_w = (u32 *)context;

	if (!is_slave_direction(direction))
		return NULL;

	/* Allocate a transaction descriptor. */
	desc = xilinx_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	desc->direction = direction;
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;

	/* Build transactions using information in the scatter gather list */
	for_each_sg(sgl, sg, sg_len, i) {
		sg_used = 0;

		/* Loop until the entire scatterlist entry is used */
		while (sg_used < sg_dma_len(sg)) {

			prev_segment = segment;
			segment = xilinx_dma_alloc_tx_segment(chan);
			if (!segment)
				goto error;

			/*  link prev -> current */
			if (prev_segment) {
#ifdef CONFIG_PHYS_ADDR_T_64BIT
				prev_segment->hw.next_desc_msb = upper_32_bits(segment->phys);
				prev_segment->hw.next_desc     = lower_32_bits(segment->phys);
#else
				prev_segment->hw.next_desc = segment->phys;
#endif
			}

			/*
			 * Calculate the maximum number of bytes to transfer,
			 * making sure it is less than the hw limit
			 */
			copy = min_t(size_t, sg_dma_len(sg) - sg_used,
				     XILINX_DMA_MAX_TRANS_LEN);
			hw = &segment->hw;

			/* Fill in the descriptor */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
			hw->buf_addr =
				lower_32_bits(sg_dma_address(sg) + sg_used);
			hw->buf_addr_msb =
				upper_32_bits(sg_dma_address(sg) + sg_used);
#else
			hw->buf_addr = sg_dma_address(sg) + sg_used;
#endif

			hw->control = copy;

			if (direction == DMA_MEM_TO_DEV) {
				if (app_w)
					memcpy(hw->app, app_w, sizeof(u32) *
					       XILINX_DMA_NUM_APP_WORDS);
			}

			sg_used += copy;

			/*
			 * Insert the segment into the descriptor segments
			 * list.
			 */
			list_add_tail(&segment->node, &desc->segments);
		}
	}

	last_segment = segment;
	segment = list_first_entry(&desc->segments,
				struct xilinx_dma_tx_segment, node);

	/* Link last segment to first */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	last_segment->hw.next_desc     = lower_32_bits(segment->phys);
	last_segment->hw.next_desc_msb = upper_32_bits(segment->phys);
#else
	last_segment->hw.next_desc = segment->phys;
#endif
	desc->async_tx.phys = segment->phys;    // first segment's address

	/* Set SOP and EOP */
	segment->hw.control |= XILINX_DMA_BD_SOP;
	segment = list_last_entry(&desc->segments,
				struct xilinx_dma_tx_segment,
				node);
	segment->hw.control |= XILINX_DMA_BD_EOP;

	return &desc->async_tx;

error:
	xilinx_dma_free_tx_descriptor(chan, desc);
	return NULL;
}

/**
 * xilinx_dma_prep_dma_cyclic - prepare descriptors for a DMA_SLAVE transaction
 * @chan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: transfer ack flags
 */
static struct dma_async_tx_descriptor *xilinx_dma_prep_dma_cyclic(
	struct dma_chan *dchan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_descriptor *desc;
	struct xilinx_dma_tx_segment *segment;
	size_t copy, sg_used;
	unsigned int num_periods;
	int i;

	num_periods = buf_len / period_len;

	if (!is_slave_direction(direction))
		return NULL;

	/* Allocate a transaction descriptor. */
	desc = xilinx_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	desc->direction = direction;
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;

	for (i = 0; i < num_periods; ++i) {
		sg_used = 0;

		while (sg_used < period_len) {
			struct xilinx_dma_desc_hw *hw;

			/* Get a free segment */
			{
				struct xilinx_dma_tx_segment *prev_segment = segment;

				segment = xilinx_dma_alloc_tx_segment(chan);
				if (!segment)
					goto error;

				if (prev_segment) {   // link prev -> current
#ifdef CONFIG_PHYS_ADDR_T_64BIT
					prev_segment->hw.next_desc_msb = upper_32_bits(segment->phys);
					prev_segment->hw.next_desc     = lower_32_bits(segment->phys);
#else
					prev_segment->hw.next_desc = segment->phys;
#endif
				}
			}

			/*
			 * Calculate the maximum number of bytes to transfer,
			 * making sure it is less than the hw limit
			 */
			copy = min_t(size_t, period_len - sg_used,
				     XILINX_DMA_MAX_TRANS_LEN);
			hw = &segment->hw;

#ifdef CONFIG_PHYS_ADDR_T_64BIT
			hw->buf_addr = lower_32_bits(buf_addr + sg_used +
						     (period_len * i));
			hw->buf_addr_msb = upper_32_bits(buf_addr + sg_used +
							 (period_len * i));
#else
			hw->buf_addr = buf_addr + sg_used + (period_len*i);
#endif
			hw->control = copy;

			sg_used += copy;

			/*
			 * Insert the segment into the descriptor segments
			 * list.
			 */
			list_add_tail(&segment->node, &desc->segments);
		}
	}

	{
		struct xilinx_dma_tx_segment *last_segment = segment;

		segment = list_first_entry(&desc->segments,
					   struct xilinx_dma_tx_segment, node);
		/* Link last segment to first */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
		last_segment->hw.next_desc     = lower_32_bits(segment->phys);
		last_segment->hw.next_desc_msb = upper_32_bits(segment->phys);
#else
		last_segment->hw.next_desc = segment->phys;
#endif
	}

	desc->async_tx.phys = segment->phys;
	desc->cyclic = true;
	chan->ctrl_reg |= XILINX_DMA_CR_CYCLIC_BD_EN_MASK;
	dma_ctrl_write(chan, XILINX_DMA_REG_CONTROL, chan->ctrl_reg);

	/* For the last DMA_MEM_TO_DEV transfer, set EOP */
	if (direction == DMA_MEM_TO_DEV) {
		segment->hw.control |= XILINX_DMA_BD_SOP;
		segment = list_last_entry(&desc->segments,
					  struct xilinx_dma_tx_segment,
					  node);
		segment->hw.control |= XILINX_DMA_BD_EOP;
	}

	return &desc->async_tx;

error:
	xilinx_dma_free_tx_descriptor(chan, desc);
	return NULL;

}

/**
 * xilinx_dma_terminate_all - Halt the channel and free descriptors
 * @dchan: DMA Channel pointer
 *
 * Return: '0' always
 */
static int xilinx_dma_terminate_all(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);

	/* Halt the DMA engine */
	xilinx_dma_halt(chan);

	tasklet_kill(&chan->tasklet);

	if (chan->err)
		xilinx_dma_chan_reset(chan);

	/* Remove and free all of the descriptors in the lists */
	xilinx_dma_free_descriptors(chan);
	if (chan->cyclic) {
		chan->ctrl_reg &= ~XILINX_DMA_CR_CYCLIC_BD_EN_MASK;
		dma_ctrl_write(chan, XILINX_DMA_REG_CONTROL, chan->ctrl_reg);
		chan->cyclic = false;
	}

	return 0;
}

/**
 * xilinx_dma_chan_remove - Per Channel remove function
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_chan_remove(struct xilinx_dma_chan *chan)
{
	/* Disable interrupts */
	chan->ctrl_reg &= ~XILINX_DMA_XR_IRQ_ALL_MASK;
	dma_ctrl_write(chan, XILINX_DMA_REG_CONTROL, chan->ctrl_reg);

	if (chan->irq > 0)
		free_irq(chan->irq, chan);

	tasklet_kill(&chan->tasklet);
	list_del(&chan->common.device_node);
}

int xilinx_dma_channel_mcdma_set_config(struct dma_chan *dchan,
					struct xilinx_mcdma_config *cfg)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);

	chan->config.tdest = cfg->tdest;
	chan->config.tid = cfg->tid;
	chan->config.tuser = cfg->tuser;
	chan->config.ax_user = cfg->ax_user;
	chan->config.ax_cache = cfg->ax_cache;

	return 0;
}
EXPORT_SYMBOL(xilinx_dma_channel_mcdma_set_config);

/**
 * xilinx_dma_chan_probe - Per Channel Probing
 * It get channel features from the device tree entry and
 * initialize special channel handling routines
 *
 * @xdev: Driver specific device structure
 * @node: Device node
 * @chan_id: Channel id
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_chan_probe(struct xilinx_dma_device *xdev,
				 struct device_node *node, int chan_id)
{
	struct xilinx_dma_chan *chan;
	int err;
	bool has_dre;
	u32 value, width = 0;

	/* alloc channel */
	chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->dev = xdev->dev;
	chan->xdev = xdev;
	chan->mcdma = xdev->mcdma;

	has_dre = of_property_read_bool(node, "xlnx,include-dre");

	err = of_property_read_u32(node, "xlnx,datawidth", &value);
	if (err) {
		dev_err(xdev->dev, "unable to read datawidth property");
		return err;
	}

	width = value >> 3; /* Convert bits to bytes */

	/* If data width is greater than 8 bytes, DRE is not in hw */
	if (width > 8)
		has_dre = false;

	if (!has_dre)
		xdev->common.copy_align = fls(width - 1);

	if (of_device_is_compatible(node, "xlnx,axi-dma-mm2s-channel")) {
		chan->id = chan_id;
		chan->ctrl_offset = XILINX_DMA_MM2S_CTRL_OFFSET;
	} else if (of_device_is_compatible(node, "xlnx,axi-dma-s2mm-channel")) {
		chan->id = chan_id;
		chan->ctrl_offset = XILINX_DMA_S2MM_CTRL_OFFSET;
	} else {
		dev_err(xdev->dev, "Invalid channel compatible node\n");
		return -EINVAL;
	}

	xdev->chan[chan->id] = chan;

	/* Initialize the channel */
	err = xilinx_dma_chan_reset(chan);
	if (err) {
		dev_err(xdev->dev, "Reset channel failed\n");
		return err;
	}

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);
	INIT_LIST_HEAD(&chan->active_list);

	chan->common.device = &xdev->common;

	/* find the IRQ line, if it exists in the device tree */
	chan->irq = irq_of_parse_and_map(node, 0);
	err = request_irq(chan->irq, xilinx_dma_irq_handler,
			  IRQF_SHARED,
			  "xilinx-dma-controller", chan);
	if (err) {
		dev_err(xdev->dev, "unable to request IRQ %d\n", chan->irq);
		return err;
	}

	/* check if SG is enabled */
	if (dma_ctrl_read(chan, XILINX_DMA_REG_STATUS) & XILINX_DMA_SR_SG_MASK)
		chan->has_sg = true;
	dev_dbg(chan->dev, "ch %d: SG %s\n", chan->id,
		chan->has_sg ? "enabled" : "disabled");

	/* Initialize the tasklet */
	tasklet_init(&chan->tasklet, xilinx_dma_do_tasklet,
		     (unsigned long)chan);

	/* Add the channel to DMA device channel list */
	list_add_tail(&chan->common.device_node, &xdev->common.channels);

	chan->idle = true;

	return 0;
}

/**
 * xilinx_dma_channel_probe - Per channel node probe
 * It get channel features from the device tree entry and
 * initialize special channel handling routines
 *
 * @xdev: Driver specific device structure
 * @node: Device node
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_channel_probe(struct xilinx_dma_device *xdev,
				    struct device_node *node) {
	int ret, i, nr_channels;

	ret = of_property_read_u32(node, "dma-channels", &nr_channels);
	if (ret) {
		dev_err(xdev->dev, "unable to read dma-channels property");
		return ret;
	}

	xdev->nr_channels += nr_channels;

	for (i = 0; i < nr_channels; i++)
		xilinx_dma_chan_probe(xdev, node, xdev->chan_id++);

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

	if (chan_id >= xdev->nr_channels)
		return NULL;

	return dma_get_slave_channel(&xdev->chan[chan_id]->common);
}

/**
 * xilinx_dma_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_probe(struct platform_device *pdev)
{
	struct xilinx_dma_device *xdev;
	struct device_node *child, *node;
	struct resource *res;
	int i, ret;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &(pdev->dev);
	INIT_LIST_HEAD(&xdev->common.channels);

	node = pdev->dev.of_node;

	/* Map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xdev->regs))
		return PTR_ERR(xdev->regs);

	xdev->mcdma = of_property_read_bool(node, "xlnx,multichannel-dma");

	/* Axi DMA only do slave transfers */
	dma_cap_set(DMA_SLAVE, xdev->common.cap_mask);
	dma_cap_set(DMA_PRIVATE, xdev->common.cap_mask);
	dma_cap_set(DMA_CYCLIC, xdev->common.cap_mask);
	xdev->common.device_prep_slave_sg = xilinx_dma_prep_slave_sg;
	xdev->common.device_prep_dma_cyclic = xilinx_dma_prep_dma_cyclic;
	if (xdev->mcdma)
		xdev->common.device_prep_interleaved_dma =
					xilinx_dma_prep_interleaved;
	xdev->common.device_terminate_all = xilinx_dma_terminate_all;
	xdev->common.device_issue_pending = xilinx_dma_issue_pending;
	xdev->common.device_alloc_chan_resources =
		xilinx_dma_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
		xilinx_dma_free_chan_resources;
	xdev->common.device_tx_status = xilinx_dma_tx_status;
	xdev->common.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	xdev->common.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;
	xdev->common.dev = &pdev->dev;
	xdev->chan_id = 0;

	platform_set_drvdata(pdev, xdev);

	for_each_child_of_node(node, child) {
		ret = xilinx_dma_channel_probe(xdev, child);
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

	dev_info(&pdev->dev, "Xilinx AXI DMA Engine driver Probed!!\n");

	return 0;

free_chan_resources:
	for (i = 0; i < xdev->nr_channels; i++)
		if (xdev->chan[i])
			xilinx_dma_chan_remove(xdev->chan[i]);

	return ret;
}

/**
 * xilinx_dma_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: Always '0'
 */
static int xilinx_dma_remove(struct platform_device *pdev)
{
	struct xilinx_dma_device *xdev = platform_get_drvdata(pdev);
	int i;

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&xdev->common);

	for (i = 0; i < xdev->nr_channels; i++)
		if (xdev->chan[i])
			xilinx_dma_chan_remove(xdev->chan[i]);

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
MODULE_LICENSE("GPL");

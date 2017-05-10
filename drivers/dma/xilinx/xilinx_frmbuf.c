/*
 * DMAEngine driver for Xilinx Framebuffer IP
 *
 * Copyright (C) 2016,2017 Xilinx, Inc. All rights reserved.
 *
 * Authors: Radhey Shyam Pandey <radheys@xilinx.com>
 *          John Nichols <jnichol@xilinx.com>
 *          Jeffrey Mouroux <jmouroux@xilinx.com>
 *
 * Based on the Freescale DMA driver.
 *
 * Description:
 * The AXI Framebuffer core is a soft Xilinx IP core that
 * provides high-bandwidth direct memory access between memory
 * and AXI4-Stream.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/dmapool.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>

#include <uapi/drm/drm_fourcc.h>
#include <uapi/linux/videodev2.h>

#include "../dmaengine.h"

#define XILINX_FBMBUF_WR			0x0
#define XILINX_FBMBUF_RD			0x1

/* Register/Descriptor Offsets */
#define XILINX_FRMBUF_CTRL_OFFSET		0x00
#define XILINX_FRMBUF_GIE_OFFSET		0x04
#define XILINX_FRMBUF_IE_OFFSET			0x08
#define XILINX_FRMBUF_ISR_OFFSET		0x0c
#define XILINX_FRMBUF_WIDTH_OFFSET		0x10
#define XILINX_FRMBUF_HEIGHT_OFFSET		0x18
#define XILINX_FRMBUF_STRIDE_OFFSET		0x20
#define XILINX_FRMBUF_FMT_OFFSET		0x28
#define XILINX_FRMBUF_ADDR_OFFSET		0x30

/* Control Registers */
#define XILINX_FRMBUF_CTRL_AP_START		BIT(0)
#define XILINX_FRMBUF_CTRL_AP_DONE		BIT(1)
#define XILINX_FRMBUF_CTRL_AP_IDLE		BIT(2)
#define XILINX_FRMBUF_CTRL_AP_READY		BIT(3)
#define XILINX_FRMBUF_CTRL_AUTO_RESTART		BIT(7)
#define XILINX_FRMBUF_GIE_EN			BIT(0)

/* Interrupt Status and Control */
#define XILINX_FRMBUF_IE_AP_DONE		BIT(0)
#define XILINX_FRMBUF_IE_AP_READY		BIT(1)

#define XILINX_FRMBUF_ISR_AP_DONE_IRQ		BIT(0)
#define XILINX_FRMBUF_ISR_AP_READY_IRQ		BIT(1)

#define XILINX_FRMBUF_ISR_ALL_IRQ_MASK	\
		(XILINX_FRMBUF_ISR_AP_DONE_IRQ | \
		XILINX_FRMBUF_ISR_AP_READY_IRQ)

/**
 * struct xilinx_frmbuf_desc_hw - Hardware Descriptor
 * @buf_addr: Buffer address
 * @vsize: Vertical Size
 * @hsize: Horizontal Size
 * @stride: Number of bytes between the first
 *	    pixels of each horizontal line
 */
struct xilinx_frmbuf_desc_hw {
	u32 buf_addr;
	u32 vsize;
	u32 hsize;
	u32 stride;
};

/**
 * struct xilinx_frmbuf_tx_descriptor - Per Transaction structure
 * @async_tx: Async transaction descriptor
 * @hw: Hardware descriptor
 * @node: Node in the channel descriptors list
 */
struct xilinx_frmbuf_tx_descriptor {
	struct dma_async_tx_descriptor async_tx;
	struct xilinx_frmbuf_desc_hw hw;
	struct list_head node;
};

/**
 * struct xilinx_frmbuf_chan - Driver specific dma channel structure
 * @xdev: Driver specific device structure
 * @lock: Descriptor operation lock
 * @pending_list: Descriptors waiting
 * @done_list: Complete descriptors
 * @staged_desc: Next buffer to be programmed
 * @active_desc: Currently active buffer being read/written to
 * @common: DMA common channel
 * @dev: The dma device
 * @irq: Channel IRQ
 * @direction: Transfer direction
 * @err: Channel has errors
 * @idle: Channel idle state
 * @tasklet: Cleanup work after irq
 * @vid_fmt_id: IP-specific id/register value for current video format
 */
struct xilinx_frmbuf_chan {
	struct xilinx_frmbuf_device *xdev;
	/* Descriptor operation lock */
	spinlock_t lock;
	struct list_head pending_list;
	struct list_head done_list;
	struct xilinx_frmbuf_tx_descriptor *staged_desc;
	struct xilinx_frmbuf_tx_descriptor *active_desc;
	struct dma_chan common;
	struct device *dev;
	int irq;
	enum dma_transfer_direction direction;
	bool idle;
	struct tasklet_struct tasklet;
	unsigned int vid_fmt_id;
};

/**
 * struct xilinx_frmbuf_device - dma device structure
 * @regs: I/O mapped base address
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific dma channel
 * @rst_gpio: GPIO reset
 */
struct xilinx_frmbuf_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_frmbuf_chan chan;
	struct gpio_desc *rst_gpio;
};

/**
 * struct xilinx_frmbuf_format_desc
 * @id: Format ID
 * @bytes_per_pixel: Bytes per pixel
 * @drm_fmt: DRM video framework equivalent fourcc code
 * @v4l2_fmt: Video 4 Linux framework equivalent fourcc code
 */
struct xilinx_frmbuf_format_desc {
	unsigned int id;
	unsigned int bytes_per_pixel;
	u32 drm_fmt;
	u32 v4l2_fmt;
};

static const struct xilinx_frmbuf_format_desc xilinx_frmbuf_formats[] = {
	/* RGBX8*/
	{10, 4, DRM_FORMAT_RGBX8888, 0},
	/* YUVX8 */
	{11, 4, 0, 0},
	/* YUYV8 */
	{12, 2, DRM_FORMAT_YUYV, V4L2_PIX_FMT_YUYV},
	/* Y_UV8 */
	{18, 1, DRM_FORMAT_NV16, V4L2_PIX_FMT_NV16},
	/* Y_UV8_420 */
	{19, 1, DRM_FORMAT_NV12, V4L2_PIX_FMT_NV12},
	/* RGB8 */
	{20, 3, DRM_FORMAT_BGR888, V4L2_PIX_FMT_RGB24},
	/* YUV8 */
	{21, 3, 0, 0},
	/* Y8 */
	{24, 4, 0, V4L2_PIX_FMT_GREY}
};

static const struct of_device_id xilinx_frmbuf_of_ids[] = {
	{ .compatible = "xlnx,axi-frmbuf-wr-1.00.a",
		.data = (void *)DMA_DEV_TO_MEM},
	{ .compatible = "xlnx,axi-frmbuf-rd-1.00.a",
		.data = (void *)DMA_MEM_TO_DEV},
	{/* end of list */}
};

/******************************PROTOTYPES*************************************/
static void xilinx_frmbuf_chan_reset(struct xilinx_frmbuf_chan *chan);

static int xilinx_frmbuf_set_vid_fmt(struct xilinx_frmbuf_chan *chan);

#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_frmbuf_chan, common)
#define to_dma_tx_descriptor(tx) \
	container_of(tx, struct xilinx_frmbuf_tx_descriptor, async_tx)

static inline u32 frmbuf_read(struct xilinx_frmbuf_chan *chan, u32 reg)
{
	return ioread32(chan->xdev->regs + reg);
}

static inline void frmbuf_write(struct xilinx_frmbuf_chan *chan, u32 reg,
				u32 value)
{
	iowrite32(value, chan->xdev->regs + reg);
}

static inline void frmbuf_clr(struct xilinx_frmbuf_chan *chan, u32 reg,
			      u32 clr)
{
	frmbuf_write(chan, reg, frmbuf_read(chan, reg) & ~clr);
}

static inline void frmbuf_set(struct xilinx_frmbuf_chan *chan, u32 reg,
			      u32 set)
{
	frmbuf_write(chan, reg, frmbuf_read(chan, reg) | set);
}

static int xilinx_frmbuf_set_vid_fmt(struct xilinx_frmbuf_chan *chan)
{
	struct xilinx_xdma_config *config;
	int i;
	u32 fourcc;
	enum vid_frmwork_type type;
	struct device *dev = chan->xdev->dev;

	config = chan->common.private;
	if (!config) {
		dev_err(dev, "Missing dma config in dma_chan obj\n");
		return -EINVAL;
	}

	type = config->type;
	fourcc = config->fourcc;

	for (i = 0; i < ARRAY_SIZE(xilinx_frmbuf_formats); i++) {
		if (type == XDMA_DRM &&
		    fourcc == xilinx_frmbuf_formats[i].drm_fmt) {
			chan->vid_fmt_id = xilinx_frmbuf_formats[i].id;
			return 0;
		} else if (type == XDMA_V4L2 &&
			fourcc == xilinx_frmbuf_formats[i].v4l2_fmt) {
			chan->vid_fmt_id = xilinx_frmbuf_formats[i].id;
			return 0;
		}
	}

	dev_err(dev, "No matching video format for fourcc code = %u\n",
		config->fourcc);

	return -EINVAL;
}

/**
 * Get bytes per pixel of given format
 * @id IP-specific integer representing a specific video memory format
 *
 * Returns bytes per pixel; 0 otherwise
 */
unsigned int xilinx_frmbuf_format_bpp(unsigned int id)
{
	const struct xilinx_frmbuf_format_desc *format;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xilinx_frmbuf_formats); i++) {
		format = &xilinx_frmbuf_formats[i];
		if (id == format->id)
			return format->bytes_per_pixel;
	}

	return 0;
}

/**
 * of_dma_xilinx_xlate - Translation function
 * @dma_spec: Pointer to DMA specifier as found in the device tree
 * @ofdma: Pointer to DMA controller data
 *
 * Return: DMA channel pointer on success or error code on error
 */
static struct dma_chan *of_dma_xilinx_xlate(struct of_phandle_args *dma_spec,
					    struct of_dma *ofdma)
{
	struct xilinx_frmbuf_device *xdev = ofdma->of_dma_data;

	return dma_get_slave_channel(&xdev->chan.common);
}

/* -----------------------------------------------------------------------------
 * Descriptors alloc and free
 */

/**
 * xilinx_frmbuf_tx_descriptor - Allocate transaction descriptor
 * @chan: Driver specific dma channel
 *
 * Return: The allocated descriptor on success and NULL on failure.
 */
static struct xilinx_frmbuf_tx_descriptor *
xilinx_frmbuf_alloc_tx_descriptor(struct xilinx_frmbuf_chan *chan)
{
	struct xilinx_frmbuf_tx_descriptor *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	return desc;
}

/**
 * xilinx_frmbuf_free_desc_list - Free descriptors list
 * @chan: Driver specific dma channel
 * @list: List to parse and delete the descriptor
 */
static void xilinx_frmbuf_free_desc_list(struct xilinx_frmbuf_chan *chan,
					 struct list_head *list)
{
	struct xilinx_frmbuf_tx_descriptor *desc, *next;

	list_for_each_entry_safe(desc, next, list, node) {
		list_del(&desc->node);
		kfree(desc);
	}
}

/**
 * xilinx_frmbuf_free_descriptors - Free channel descriptors
 * @chan: Driver specific dma channel
 */
static void xilinx_frmbuf_free_descriptors(struct xilinx_frmbuf_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	xilinx_frmbuf_free_desc_list(chan, &chan->pending_list);
	xilinx_frmbuf_free_desc_list(chan, &chan->done_list);
	kfree(chan->active_desc);
	kfree(chan->staged_desc);

	chan->staged_desc = NULL;
	chan->active_desc = NULL;
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_frmbuf_free_chan_resources - Free channel resources
 * @dchan: DMA channel
 */
static void xilinx_frmbuf_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_frmbuf_chan *chan = to_xilinx_chan(dchan);

	xilinx_frmbuf_free_descriptors(chan);
}

/**
 * xilinx_frmbuf_chan_desc_cleanup - Clean channel descriptors
 * @chan: Driver specific dma channel
 */
static void xilinx_frmbuf_chan_desc_cleanup(struct xilinx_frmbuf_chan *chan)
{
	struct xilinx_frmbuf_tx_descriptor *desc, *next;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	list_for_each_entry_safe(desc, next, &chan->done_list, node) {
		dma_async_tx_callback callback;
		void *callback_param;

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
		kfree(desc);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_frmbuf_do_tasklet - Schedule completion tasklet
 * @data: Pointer to the Xilinx frmbuf channel structure
 */
static void xilinx_frmbuf_do_tasklet(unsigned long data)
{
	struct xilinx_frmbuf_chan *chan = (struct xilinx_frmbuf_chan *)data;

	xilinx_frmbuf_chan_desc_cleanup(chan);
}

/**
 * xilinx_frmbuf_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_frmbuf_alloc_chan_resources(struct dma_chan *dchan)
{
	dma_cookie_init(dchan);

	return 0;
}

/**
 * xilinx_frmbuf_tx_status - Get frmbuf transaction status
 * @dchan: DMA channel
 * @cookie: Transaction identifier
 * @txstate: Transaction state
 *
 * Return: fmrbuf transaction status
 */
static enum dma_status xilinx_frmbuf_tx_status(struct dma_chan *dchan,
					       dma_cookie_t cookie,
					       struct dma_tx_state *txstate)
{
	return dma_cookie_status(dchan, cookie, txstate);
}

/**
 * xilinx_frmbuf_halt - Halt frmbuf channel
 * @chan: Driver specific dma channel
 */
static void xilinx_frmbuf_halt(struct xilinx_frmbuf_chan *chan)
{
	frmbuf_clr(chan, XILINX_FRMBUF_CTRL_OFFSET,
		   XILINX_FRMBUF_CTRL_AP_START |
		   XILINX_FRMBUF_CTRL_AUTO_RESTART);
	chan->idle = true;
}

/**
 * xilinx_frmbuf_start - Start dma channel
 * @chan: Driver specific dma channel
 */
static void xilinx_frmbuf_start(struct xilinx_frmbuf_chan *chan)
{
	frmbuf_set(chan, XILINX_FRMBUF_CTRL_OFFSET,
		   XILINX_FRMBUF_CTRL_AP_START |
		   XILINX_FRMBUF_CTRL_AUTO_RESTART);
	chan->idle = false;
}

/**
 * xilinx_frmbuf_complete_descriptor - Mark the active descriptor as complete
 * This function is invoked with spinlock held
 * @chan : xilinx frmbuf channel
 *
 * CONTEXT: hardirq
 */
static void xilinx_frmbuf_complete_descriptor(struct xilinx_frmbuf_chan *chan)
{
	struct xilinx_frmbuf_tx_descriptor *desc = chan->active_desc;

	dma_cookie_complete(&desc->async_tx);
	list_add_tail(&desc->node, &chan->done_list);
}

/**
 * xilinx_frmbuf_start_transfer - Starts frmbuf transfer
 * @chan: Driver specific channel struct pointer
 */
static void xilinx_frmbuf_start_transfer(struct xilinx_frmbuf_chan *chan)
{
	struct xilinx_frmbuf_tx_descriptor *desc;

	if (!chan->idle)
		return;

	if (chan->active_desc) {
		xilinx_frmbuf_complete_descriptor(chan);
		chan->active_desc = NULL;
	}

	if (chan->staged_desc) {
		chan->active_desc = chan->staged_desc;
		chan->staged_desc = NULL;
	}

	if (list_empty(&chan->pending_list))
		return;

	desc = list_first_entry(&chan->pending_list,
				struct xilinx_frmbuf_tx_descriptor,
				node);

	/* Start the transfer */
	frmbuf_write(chan, XILINX_FRMBUF_ADDR_OFFSET, desc->hw.buf_addr);

	/* HW expects these parameters to be same for one transaction */
	frmbuf_write(chan, XILINX_FRMBUF_WIDTH_OFFSET, desc->hw.hsize);
	frmbuf_write(chan, XILINX_FRMBUF_STRIDE_OFFSET, desc->hw.stride);
	frmbuf_write(chan, XILINX_FRMBUF_HEIGHT_OFFSET, desc->hw.vsize);
	frmbuf_write(chan, XILINX_FRMBUF_FMT_OFFSET, chan->vid_fmt_id);
	frmbuf_write(chan, XILINX_FRMBUF_IE_OFFSET, XILINX_FRMBUF_IE_AP_READY);
	frmbuf_write(chan, XILINX_FRMBUF_GIE_OFFSET, XILINX_FRMBUF_GIE_EN);

	/* Start the hardware */
	xilinx_frmbuf_start(chan);
	list_del(&desc->node);
	chan->staged_desc = desc;
}

/**
 * xilinx_frmbuf_issue_pending - Issue pending transactions
 * @dchan: DMA channel
 */
static void xilinx_frmbuf_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_frmbuf_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_frmbuf_start_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_frmbuf_reset - Reset frmbuf channel
 * @chan: Driver specific dma channel
 */
static void xilinx_frmbuf_reset(struct xilinx_frmbuf_chan *chan)
{
	frmbuf_clr(chan, XILINX_FRMBUF_CTRL_OFFSET, ~0x0);
}

/**
 * xilinx_frmbuf_chan_reset - Reset frmbuf channel and enable interrupts
 * @chan: Driver specific frmbuf channel
 */
static void xilinx_frmbuf_chan_reset(struct xilinx_frmbuf_chan *chan)
{
	xilinx_frmbuf_reset(chan);

	frmbuf_set(chan, XILINX_FRMBUF_IE_OFFSET,
		   XILINX_FRMBUF_ISR_ALL_IRQ_MASK);
}

/**
 * xilinx_frmbuf_irq_handler - frmbuf Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the Xilinx frmbuf channel structure
 *
 * Return: IRQ_HANDLED/IRQ_NONE
 */
static irqreturn_t xilinx_frmbuf_irq_handler(int irq, void *data)
{
	struct xilinx_frmbuf_chan *chan = data;
	u32 status;

	status = frmbuf_read(chan, XILINX_FRMBUF_ISR_OFFSET);
	if (!(status & XILINX_FRMBUF_ISR_ALL_IRQ_MASK))
		return IRQ_NONE;

	frmbuf_write(chan, XILINX_FRMBUF_ISR_OFFSET,
		     status & XILINX_FRMBUF_ISR_ALL_IRQ_MASK);

	if (status & XILINX_FRMBUF_ISR_AP_READY_IRQ) {
		spin_lock(&chan->lock);
		chan->idle = true;
		xilinx_frmbuf_start_transfer(chan);
		spin_unlock(&chan->lock);
	}

	tasklet_schedule(&chan->tasklet);
	return IRQ_HANDLED;
}

/**
 * xilinx_frmbuf_tx_submit - Submit DMA transaction
 * @tx: Async transaction descriptor
 *
 * Return: cookie value on success and failure value on error
 */
static dma_cookie_t xilinx_frmbuf_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_frmbuf_tx_descriptor *desc = to_dma_tx_descriptor(tx);
	struct xilinx_frmbuf_chan *chan = to_xilinx_chan(tx->chan);
	dma_cookie_t cookie;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	cookie = dma_cookie_assign(tx);

	list_add_tail(&desc->node, &chan->pending_list);

	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * xilinx_frmbuf_dma_prep_interleaved - prepare a descriptor for a
 *	DMA_SLAVE transaction
 * @dchan: DMA channel
 * @xt: Interleaved template pointer
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xilinx_frmbuf_dma_prep_interleaved(struct dma_chan *dchan,
				   struct dma_interleaved_template *xt,
				   unsigned long flags)
{
	struct xilinx_frmbuf_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_frmbuf_tx_descriptor *desc;
	struct xilinx_frmbuf_desc_hw *hw;
	int bytes_per_pixel;

	if (chan->direction != xt->dir)
		return NULL;

	if (!xt->numf || !xt->sgl[0].size)
		return NULL;

	if (xt->frame_size != 1)
		return NULL;

	desc = xilinx_frmbuf_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_frmbuf_tx_submit;
	async_tx_ack(&desc->async_tx);

	if (xilinx_frmbuf_set_vid_fmt(chan))
		goto error;

	bytes_per_pixel = xilinx_frmbuf_format_bpp(chan->vid_fmt_id);

	if (bytes_per_pixel < 1)
		goto error;

	/* Fill in the hardware descriptor */
	hw = &desc->hw;

	/* vsize is number of active hor pixels */
	hw->vsize = xt->numf;

	/* hsize is number of active ver pixels */
	hw->hsize = xt->sgl[0].size / bytes_per_pixel;

	/* Stride is given in bytes.
	 * It is width x bytes/pixel rounded up to
	 * a multiple of AXI_MM_DATA_WIDTH in bytes.
	 */
	hw->stride = xt->sgl[0].icg + xt->sgl[0].size;

	if (chan->direction == DMA_MEM_TO_DEV)
		hw->buf_addr = xt->src_start;
	else
		hw->buf_addr = xt->dst_start;

	return &desc->async_tx;

error:
	kfree(desc);
	return NULL;
}

/**
 * xilinx_frmbuf_terminate_all - Halt the channel and free descriptors
 * @chan: Driver specific dma Channel pointer
 *
 * Return: 0
 */
static int xilinx_frmbuf_terminate_all(struct dma_chan *dchan)
{
	struct xilinx_frmbuf_chan *chan = to_xilinx_chan(dchan);

	xilinx_frmbuf_halt(chan);

	xilinx_frmbuf_free_descriptors(chan);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Probe and remove
 */

/**
 * xilinx_frmbuf_chan_remove - Per Channel remove function
 * @chan: Driver specific dma channel
 */
static void xilinx_frmbuf_chan_remove(struct xilinx_frmbuf_chan *chan)
{
	/* Disable all interrupts */
	frmbuf_clr(chan, XILINX_FRMBUF_IE_OFFSET,
		   XILINX_FRMBUF_ISR_ALL_IRQ_MASK);

	tasklet_kill(&chan->tasklet);
	list_del(&chan->common.device_node);
}

/**
 * xilinx_frmbuf_chan_probe - Per Channel Probing
 * It get channel features from the device tree entry and
 * initialize special channel handling routines
 *
 * @xdev: Driver specific device structure
 * @node: Device node
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_frmbuf_chan_probe(struct xilinx_frmbuf_device *xdev,
				    struct device_node *node)
{
	struct xilinx_frmbuf_chan *chan;
	int err;

	chan = &xdev->chan;

	chan->dev = xdev->dev;
	chan->xdev = xdev;
	chan->idle = true;

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);

	chan->irq = irq_of_parse_and_map(node, 0);
	err = devm_request_irq(xdev->dev, chan->irq, xilinx_frmbuf_irq_handler,
			       IRQF_SHARED, "xilinx_framebuffer", chan);

	if (err) {
		dev_err(xdev->dev, "unable to request IRQ %d\n", chan->irq);
		return err;
	}

	tasklet_init(&chan->tasklet, xilinx_frmbuf_do_tasklet,
		     (unsigned long)chan);

	/*
	 * Initialize the DMA channel and add it to the DMA engine channels
	 * list.
	 */
	chan->common.device = &xdev->common;

	list_add_tail(&chan->common.device_node, &xdev->common.channels);

	xilinx_frmbuf_chan_reset(chan);

	return 0;
}

/**
 * xilinx_frmbuf_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_frmbuf_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct xilinx_frmbuf_device *xdev;
	struct resource *io;
	enum dma_transfer_direction dma_dir;
	const struct of_device_id *match;
	int err;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &pdev->dev;

	match = of_match_node(xilinx_frmbuf_of_ids, node);
	if (!match)
		return -ENODEV;

	dma_dir = (enum dma_transfer_direction)match->data;

	xdev->rst_gpio = devm_gpiod_get(&pdev->dev, "reset",
					GPIOD_OUT_HIGH);
	if (IS_ERR(xdev->rst_gpio)) {
		err = PTR_ERR(xdev->rst_gpio);
		if (err == -EPROBE_DEFER)
			dev_info(&pdev->dev,
				 "Probe deferred due to GPIO reset defer\n");
		else
			dev_err(&pdev->dev,
				"Unable to locate reset property in dt\n");
		goto error;
	}

	gpiod_set_value_cansleep(xdev->rst_gpio, 0x0);

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->regs = devm_ioremap_resource(&pdev->dev, io);
	if (IS_ERR(xdev->regs))
		return PTR_ERR(xdev->regs);

	/* Initialize the DMA engine */
	xdev->common.dev = &pdev->dev;

	INIT_LIST_HEAD(&xdev->common.channels);
	dma_cap_set(DMA_SLAVE, xdev->common.cap_mask);
	dma_cap_set(DMA_PRIVATE, xdev->common.cap_mask);

	/* Initialize the channels */
	err = xilinx_frmbuf_chan_probe(xdev, node);
	if (err < 0)
		goto error;

	xdev->chan.direction = dma_dir;

	if (xdev->chan.direction == DMA_DEV_TO_MEM) {
		xdev->common.directions = BIT(DMA_DEV_TO_MEM);
		dev_info(&pdev->dev, "Xilinx AXI frmbuf DMA_DEV_TO_MEM\n");
	} else if (xdev->chan.direction == DMA_MEM_TO_DEV) {
		xdev->common.directions = BIT(DMA_MEM_TO_DEV);
		dev_info(&pdev->dev, "Xilinx AXI frmbuf DMA_MEM_TO_DEV\n");
	} else {
		return -EINVAL;
	}

	xdev->common.device_alloc_chan_resources =
				xilinx_frmbuf_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
				xilinx_frmbuf_free_chan_resources;
	xdev->common.device_prep_interleaved_dma =
				xilinx_frmbuf_dma_prep_interleaved;
	xdev->common.device_terminate_all = xilinx_frmbuf_terminate_all;
	xdev->common.device_tx_status = xilinx_frmbuf_tx_status;
	xdev->common.device_issue_pending = xilinx_frmbuf_issue_pending;

	platform_set_drvdata(pdev, xdev);

	/* Register the DMA engine with the core */
	dma_async_device_register(&xdev->common);
	err = of_dma_controller_register(node, of_dma_xilinx_xlate, xdev);

	if (err < 0) {
		dev_err(&pdev->dev, "Unable to register DMA to DT\n");
		dma_async_device_unregister(&xdev->common);
		goto error;
	}

	dev_info(&pdev->dev, "Xilinx AXI FrameBuffer Engine Driver Probed!!\n");

	return 0;

error:
	return err;
}

/**
 * xilinx_frmbuf_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: Always '0'
 */
static int xilinx_frmbuf_remove(struct platform_device *pdev)
{
	struct xilinx_frmbuf_device *xdev = platform_get_drvdata(pdev);

	dma_async_device_unregister(&xdev->common);
	xilinx_frmbuf_chan_remove(&xdev->chan);

	return 0;
}

MODULE_DEVICE_TABLE(of, xilinx_frmbuf_of_ids);

static struct platform_driver xilinx_frmbuf_driver = {
	.driver = {
		.name = "xilinx-frmbuf",
		.of_match_table = xilinx_frmbuf_of_ids,
	},
	.probe = xilinx_frmbuf_probe,
	.remove = xilinx_frmbuf_remove,
};

module_platform_driver(xilinx_frmbuf_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Framebuffer driver");
MODULE_LICENSE("GPL");

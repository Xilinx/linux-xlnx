//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Scene Change Detection DMA driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Authors: Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>
 *          Satish Kumar Nagireddy <satish.nagireddy.nagireddy@xilinx.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "xilinx-scenechange.h"

/**
 * xscd_dma_irq_handler - scdma Interrupt handler
 * @xscd: Pointer to the SCD device structure
 */
void xscd_dma_irq_handler(struct xscd_device *xscd)
{
	struct xscd_dma_chan *chan;

	if (xscd->memory_based) {
		u32 chan_en = 0, id;

		for (id = 0; id < xscd->num_streams; id++) {
			chan = xscd->channels[id];
			spin_lock(&chan->lock);
			chan->idle = true;

			if (chan->en && (!list_empty(&chan->pending_list))) {
				chan_en |= 1 << chan->id;
				chan->valid_interrupt = true;
			} else {
				chan->valid_interrupt = false;
			}

			xscd_dma_start_transfer(chan);
			spin_unlock(&chan->lock);
		}

		if (chan_en) {
			xscd_dma_reset(chan);
			xscd_dma_chan_enable(chan, chan_en);
			xscd_dma_start(chan);
		}

		for (id = 0; id < xscd->num_streams; id++) {
			chan = xscd->channels[id];
			tasklet_schedule(&chan->tasklet);
		}
	}
}

/* -----------------------------------------------------------------------------
 * Descriptors alloc and free
 */

/**
 * xscd_dma_tx_descriptor - Allocate transaction descriptor
 * @chan: Driver specific dma channel
 *
 * Return: The allocated descriptor on success and NULL on failure.
 */
static struct xscd_dma_tx_descriptor *
xscd_dma_alloc_tx_descriptor(struct xscd_dma_chan *chan)
{
	struct xscd_dma_tx_descriptor *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	return desc;
}

/**
 * xscd_dma_tx_submit - Submit DMA transaction
 * @tx: Async transaction descriptor
 *
 * Return: cookie value on success and failure value on error
 */
static dma_cookie_t xscd_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xscd_dma_tx_descriptor *desc = to_xscd_dma_tx_descriptor(tx);
	struct xscd_dma_chan *chan = to_xscd_dma_chan(tx->chan);
	dma_cookie_t cookie;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	cookie = dma_cookie_assign(tx);
	list_add_tail(&desc->node, &chan->pending_list);
	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * xscd_dma_chan_enable - Enable dma channel
 * @chan: Driver specific dma channel
 * @chan_en: Channels ready for transfer, it is a bitmap
 */
void xscd_dma_chan_enable(struct xscd_dma_chan *chan, int chan_en)
{
	xscd_write(chan->xscd->iomem, XSCD_CHAN_EN_OFFSET, chan_en);
}

/**
 * xscd_dma_complete_descriptor - Mark the active descriptor as complete
 * This function is invoked with spinlock held
 * @chan : xilinx dma channel
 *
 */
static void xscd_dma_complete_descriptor(struct xscd_dma_chan *chan)
{
	struct xscd_dma_tx_descriptor *desc = chan->active_desc;

	dma_cookie_complete(&desc->async_tx);
	list_add_tail(&desc->node, &chan->done_list);
}

/**
 * xscd_dma_start_transfer - Starts dma transfer
 * @chan: Driver specific channel struct pointer
 */
void xscd_dma_start_transfer(struct xscd_dma_chan *chan)
{
	struct xscd_dma_tx_descriptor *desc;

	if (!chan->en)
		return;

	if (!chan->idle)
		return;

	if (chan->active_desc) {
		xscd_dma_complete_descriptor(chan);
		chan->active_desc = NULL;
	}

	if (chan->staged_desc) {
		chan->active_desc = chan->staged_desc;
		chan->staged_desc = NULL;
	}

	if (list_empty(&chan->pending_list))
		return;

	desc = list_first_entry(&chan->pending_list,
				struct xscd_dma_tx_descriptor, node);

	/* Start the transfer */
	xscd_write(chan->iomem, XSCD_ADDR_OFFSET, desc->sw.luma_plane_addr);

	list_del(&desc->node);
	chan->staged_desc = desc;
}

/**
 * xscd_dma_free_desc_list - Free descriptors list
 * @chan: Driver specific dma channel
 * @list: List to parse and delete the descriptor
 */
static void xscd_dma_free_desc_list(struct xscd_dma_chan *chan,
				    struct list_head *list)
{
	struct xscd_dma_tx_descriptor *desc, *next;

	list_for_each_entry_safe(desc, next, list, node) {
		list_del(&desc->node);
		kfree(desc);
	}
}

/**
 * xscd_dma_free_descriptors - Free channel descriptors
 * @chan: Driver specific dma channel
 */
static void xscd_dma_free_descriptors(struct xscd_dma_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	xscd_dma_free_desc_list(chan, &chan->pending_list);
	xscd_dma_free_desc_list(chan, &chan->done_list);
	kfree(chan->active_desc);
	kfree(chan->staged_desc);

	chan->staged_desc = NULL;
	chan->active_desc = NULL;
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * scd_dma_chan_desc_cleanup - Clean channel descriptors
 * @chan: Driver specific dma channel
 */
static void xscd_dma_chan_desc_cleanup(struct xscd_dma_chan *chan)
{
	struct xscd_dma_tx_descriptor *desc, *next;
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

		kfree(desc);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xscd_dma_dma_prep_interleaved - prepare a descriptor for a
 * DMA_SLAVE transaction
 * @dchan: DMA channel
 * @xt: Interleaved template pointer
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xscd_dma_prep_interleaved(struct dma_chan *dchan,
			  struct dma_interleaved_template *xt,
			  unsigned long flags)
{
	struct xscd_dma_chan *chan = to_xscd_dma_chan(dchan);
	struct xscd_dma_tx_descriptor *desc;
	struct xscd_dma_desc *sw;

	desc = xscd_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xscd_dma_tx_submit;
	async_tx_ack(&desc->async_tx);

	sw = &desc->sw;
	sw->vsize = xt->numf;
	sw->hsize = xt->sgl[0].size;
	sw->stride = xt->sgl[0].size + xt->sgl[0].icg;
	sw->luma_plane_addr = xt->src_start;

	return &desc->async_tx;
}

/**
 * xscd_dma_terminate_all - Halt the channel and free descriptors
 * @dchan: Driver specific dma channel pointer
 *
 * Return: 0
 */
static int xscd_dma_terminate_all(struct dma_chan *dchan)
{
	struct xscd_dma_chan *chan = to_xscd_dma_chan(dchan);

	xscd_dma_halt(chan);
	xscd_dma_free_descriptors(chan);

	/* Worst case frame-to-frame boundary, ensure frame output complete */
	msleep(50);
	xscd_dma_reset(chan);

	return 0;
}

/**
 * xscd_dma_issue_pending - Issue pending transactions
 * @dchan: DMA channel
 */
static void xscd_dma_issue_pending(struct dma_chan *dchan)
{
	struct xscd_dma_chan *chan = to_xscd_dma_chan(dchan);
	struct xscd_device *xscd = chan->xscd;
	u32 chan_en = 0, id;

	for (id = 0; id < xscd->num_streams; id++) {
		chan = xscd->channels[id];
		spin_lock(&chan->lock);
		chan->idle = true;

		if (chan->en && (!list_empty(&chan->pending_list))) {
			chan_en |= 1 << chan->id;
			chan->valid_interrupt = true;
		} else {
			chan->valid_interrupt = false;
		}

		xscd_dma_start_transfer(chan);
		spin_unlock(&chan->lock);
	}

	if (chan_en) {
		xscd_dma_reset(chan);
		xscd_dma_chan_enable(chan, chan_en);
		xscd_dma_start(chan);
	}
}

static enum dma_status xscd_dma_tx_status(struct dma_chan *dchan,
					  dma_cookie_t cookie,
					  struct dma_tx_state *txstate)
{
	return dma_cookie_status(dchan, cookie, txstate);
}

/**
 * xscd_dma_halt - Halt dma channel
 * @chan: Driver specific dma channel
 */
void xscd_dma_halt(struct xscd_dma_chan *chan)
{
	struct xscd_device *xscd = chan->xscd;

	if (xscd->memory_based)
		xscd_clr(chan->xscd->iomem, XSCD_CTRL_OFFSET,
			 XSCD_CTRL_AP_START);
	else
		/* Streaming based */
		xscd_clr(chan->xscd->iomem, XSCD_CTRL_OFFSET,
			 XSCD_CTRL_AP_START | XSCD_CTRL_AUTO_RESTART);

	chan->idle = true;
}

/**
 * xscd_dma_start - Start dma channel
 * @chan: Driver specific dma channel
 */
void xscd_dma_start(struct xscd_dma_chan *chan)
{
	struct xscd_device *xscd = chan->xscd;

	if (xscd->memory_based)
		xscd_set(chan->xscd->iomem, XSCD_CTRL_OFFSET,
			 XSCD_CTRL_AP_START);
	else
		/* Streaming based */
		xscd_set(chan->xscd->iomem, XSCD_CTRL_OFFSET,
			 XSCD_CTRL_AP_START | XSCD_CTRL_AUTO_RESTART);

	chan->idle = false;
}

/**
 * xscd_dma_reset - Reset dma channel and enable interrupts
 * @chan: Driver specific dma channel
 */
void xscd_dma_reset(struct xscd_dma_chan *chan)
{
	xscd_write(chan->xscd->iomem, XSCD_IE_OFFSET, XSCD_IE_AP_DONE);
	xscd_write(chan->xscd->iomem, XSCD_GIE_OFFSET, XSCD_GIE_EN);
}

/**
 * xscd_dma_free_chan_resources - Free channel resources
 * @dchan: DMA channel
 */
static void xscd_dma_free_chan_resources(struct dma_chan *dchan)
{
	struct xscd_dma_chan *chan = to_xscd_dma_chan(dchan);

	xscd_dma_free_descriptors(chan);
}

/**
 * xscd_dma_do_tasklet - Schedule completion tasklet
 * @data: Pointer to the Xilinx scdma channel structure
 */
static void xscd_dma_do_tasklet(unsigned long data)
{
	struct xscd_dma_chan *chan = (struct xscd_dma_chan *)data;

	xscd_dma_chan_desc_cleanup(chan);
}

/**
 * xscd_dma_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xscd_dma_alloc_chan_resources(struct dma_chan *dchan)
{
	dma_cookie_init(dchan);
	return 0;
}

/**
 * of_scdma_xilinx_xlate - Translation function
 * @dma_spec: Pointer to DMA specifier as found in the device tree
 * @ofdma: Pointer to DMA controller data
 *
 * Return: DMA channel pointer on success and NULL on error
 */
static struct dma_chan *of_scdma_xilinx_xlate(struct of_phandle_args *dma_spec,
					      struct of_dma *ofdma)
{
	struct xscd_device *xscd = ofdma->of_dma_data;
	u32 chan_id = dma_spec->args[0];

	if (chan_id >= xscd->num_streams)
		return NULL;

	if (!xscd->channels[chan_id])
		return NULL;

	return dma_get_slave_channel(&xscd->channels[chan_id]->common);
}

static void xscd_dma_chan_init(struct xscd_device *xscd, int chan_id)
{
	struct xscd_dma_chan *chan = &xscd->chans[chan_id]->dmachan;

	chan->id = chan_id;
	chan->iomem = xscd->iomem + chan->id * XSCD_CHAN_OFFSET;
	chan->xscd = xscd;
	chan->idle = true;

	xscd->channels[chan->id] = chan;

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);
	tasklet_init(&chan->tasklet, xscd_dma_do_tasklet,
		     (unsigned long)chan);
	chan->common.device = &xscd->dma_device;
	list_add_tail(&chan->common.device_node, &xscd->dma_device.channels);
}

/**
 * xscd_dma_chan_remove - Per Channel remove function
 * @chan: Driver specific DMA channel
 */
static void xscd_dma_chan_remove(struct xscd_dma_chan *chan)
{
	list_del(&chan->common.device_node);
}

/**
 * xscd_dma_init - Initialize the SCD DMA engine
 * @xscd: Pointer to the SCD device structure
 *
 * Return: '0' on success and failure value on error
 */
int xscd_dma_init(struct xscd_device *xscd)
{
	struct dma_device *ddev = &xscd->dma_device;
	unsigned int chan_id;
	int ret;

	/* Initialize the DMA engine */
	ddev->dev = xscd->dev;
	dma_set_mask(xscd->dev, DMA_BIT_MASK(32));

	INIT_LIST_HEAD(&ddev->channels);
	dma_cap_set(DMA_SLAVE, ddev->cap_mask);
	dma_cap_set(DMA_PRIVATE, ddev->cap_mask);
	ddev->device_alloc_chan_resources = xscd_dma_alloc_chan_resources;
	ddev->device_free_chan_resources = xscd_dma_free_chan_resources;
	ddev->device_tx_status = xscd_dma_tx_status;
	ddev->device_issue_pending = xscd_dma_issue_pending;
	ddev->device_terminate_all = xscd_dma_terminate_all;
	ddev->device_prep_interleaved_dma = xscd_dma_prep_interleaved;

	for (chan_id = 0; chan_id < xscd->num_streams; chan_id++)
		xscd_dma_chan_init(xscd, chan_id);

	ret = dma_async_device_register(ddev);
	if (ret) {
		dev_err(xscd->dev, "failed to register the dma device\n");
		goto error;
	}

	ret = of_dma_controller_register(xscd->dev->of_node,
					 of_scdma_xilinx_xlate, xscd);
	if (ret) {
		dev_err(xscd->dev, "failed to register DMA to DT DMA helper\n");
		goto error_of_dma;
	}

	dev_info(xscd->dev, "Xilinx Scene Change DMA is probed!\n");
	return 0;

error_of_dma:
	dma_async_device_unregister(ddev);

error:
	for (chan_id = 0; chan_id < xscd->num_streams; chan_id++)
		xscd_dma_chan_remove(xscd->channels[chan_id]);

	return ret;
}

/**
 * xscd_dma_cleanup - Clean up the SCD DMA engine
 * @xscd: Pointer to the SCD device structure
 *
 * This function is the counterpart of xscd_dma_init() and cleans up the
 * resources related to the DMA engine.
 */
void xscd_dma_cleanup(struct xscd_device *xscd)
{
	dma_async_device_unregister(&xscd->dma_device);
	of_dma_controller_free(xscd->dev->of_node);
}

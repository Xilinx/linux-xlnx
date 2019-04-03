//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Scene Change Detection DMA driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Authors: Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>
 *          Satish Kumar Nagireddy <satish.nagireddy.nagireddy@xilinx.com>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/of_dma.h>
#include <linux/slab.h>

#include "../../../dma/dmaengine.h"

#include "xilinx-scenechange.h"

/**
 * xscd_dma_start - Start the SCD core
 * @xscd: The SCD device
 * @channels: Bitmask of enabled channels
 */
static void xscd_dma_start(struct xscd_device *xscd, unsigned int channels)
{
	xscd_write(xscd->iomem, XSCD_IE_OFFSET, XSCD_IE_AP_DONE);
	xscd_write(xscd->iomem, XSCD_GIE_OFFSET, XSCD_GIE_EN);
	xscd_write(xscd->iomem, XSCD_CHAN_EN_OFFSET, channels);

	xscd_set(xscd->iomem, XSCD_CTRL_OFFSET,
		 xscd->memory_based ? XSCD_CTRL_AP_START
				    : XSCD_CTRL_AP_START |
				      XSCD_CTRL_AUTO_RESTART);

	xscd->running = true;
}

/**
 * xscd_dma_stop - Stop the SCD core
 * @xscd: The SCD device
 */
static void xscd_dma_stop(struct xscd_device *xscd)
{
	xscd_clr(xscd->iomem, XSCD_CTRL_OFFSET,
		 xscd->memory_based ? XSCD_CTRL_AP_START
				    : XSCD_CTRL_AP_START |
				      XSCD_CTRL_AUTO_RESTART);

	xscd->running = false;
}

/**
 * xscd_dma_setup_channel - Setup a channel for transfer
 * @chan: Driver specific channel struct pointer
 *
 * Return: 1 if the channel starts to run for a new transfer. Otherwise, 0.
 */
static int xscd_dma_setup_channel(struct xscd_dma_chan *chan)
{
	struct xscd_dma_tx_descriptor *desc;

	if (!chan->enabled)
		return 0;

	if (list_empty(&chan->pending_list))
		return 0;

	desc = list_first_entry(&chan->pending_list,
				struct xscd_dma_tx_descriptor, node);
	list_del(&desc->node);

	xscd_write(chan->iomem, XSCD_ADDR_OFFSET, desc->sw.luma_plane_addr);
	chan->active_desc = desc;

	return 1;
}

/**
 * xscd_dma_kick - Start a run of the SCD core if channels are ready
 * @xscd: The SCD device
 *
 * This function starts a single run of the SCD core when all the following
 * conditions are met:
 *
 * - The SCD is not currently running
 * - At least one channel is enabled and has buffers available
 *
 * It can be used to start the SCD when a buffer is queued, when a channel
 * starts streaming, or to start the next run. Calling this function is only
 * valid for memory-based mode and is not permitted for stream-based mode.
 *
 * The running state for all channels is updated. Channels that are being
 * stopped are signalled through the channel wait queue.
 *
 * The function must be called with the xscd_device lock held.
 */
static void xscd_dma_kick(struct xscd_device *xscd)
{
	unsigned int channels = 0;
	unsigned int i;

	lockdep_assert_held(&xscd->lock);

	if (xscd->running)
		return;

	for (i = 0; i < xscd->num_streams; i++) {
		struct xscd_dma_chan *chan = xscd->channels[i];
		unsigned long flags;
		unsigned int running;
		bool stopped;

		spin_lock_irqsave(&chan->lock, flags);
		running = xscd_dma_setup_channel(chan);
		stopped = chan->running && !running;
		chan->running = running;
		spin_unlock_irqrestore(&chan->lock, flags);

		channels |= running << chan->id;
		if (stopped)
			wake_up(&chan->wait);
	}

	if (channels)
		xscd_dma_start(xscd, channels);
	else
		xscd_dma_stop(xscd);
}

/**
 * xscd_dma_enable_channel - Enable/disable a channel
 * @chan: Driver specific channel struct pointer
 * @enable: True to enable the channel, false to disable it
 *
 * This function enables or disable a channel. When operating in memory-based
 * mode, enabling a channel kicks processing if buffers are available for any
 * enabled channel and the SCD core is idle. When operating in stream-based
 * mode, the SCD core is started or stopped synchronously when then channel is
 * enabled or disabled.
 *
 * This function must be called in non-atomic, non-interrupt context.
 */
void xscd_dma_enable_channel(struct xscd_dma_chan *chan, bool enable)
{
	struct xscd_device *xscd = chan->xscd;

	if (enable) {
		/*
		 * FIXME: Don't set chan->enabled to false here, it will be
		 * done in xscd_dma_terminate_all(). This works around a bug
		 * introduced in commit 2e77607047c6 ("xilinx: v4l2: dma: Add
		 * multiple output support") that stops all channels when the
		 * first one is stopped, even though they are part of
		 * independent pipelines. This workaround should be safe as
		 * long as dmaengine_terminate_all() is called after
		 * xvip_pipeline_set_stream().
		 */
		spin_lock_irq(&chan->lock);
		chan->enabled = true;
		spin_unlock_irq(&chan->lock);
	}

	if (xscd->memory_based) {
		if (enable) {
			spin_lock_irq(&xscd->lock);
			xscd_dma_kick(xscd);
			spin_unlock_irq(&xscd->lock);
		}
	} else {
		if (enable)
			xscd_dma_start(xscd, BIT(chan->id));
		else
			xscd_dma_stop(xscd);
	}
}

/**
 * xscd_dma_irq_handler - scdma Interrupt handler
 * @xscd: Pointer to the SCD device structure
 */
void xscd_dma_irq_handler(struct xscd_device *xscd)
{
	unsigned int i;

	/*
	 * Mark the active descriptors as complete, move them to the done list
	 * and schedule the tasklet to clean them up.
	 */
	for (i = 0; i < xscd->num_streams; ++i) {
		struct xscd_dma_chan *chan = xscd->channels[i];
		struct xscd_dma_tx_descriptor *desc = chan->active_desc;

		if (!desc)
			continue;

		dma_cookie_complete(&desc->async_tx);
		xscd_chan_event_notify(&xscd->chans[i]);

		spin_lock(&chan->lock);
		list_add_tail(&desc->node, &chan->done_list);
		chan->active_desc = NULL;
		spin_unlock(&chan->lock);

		tasklet_schedule(&chan->tasklet);
	}

	/* Start the next run, if any. */
	spin_lock(&xscd->lock);
	xscd->running = false;
	xscd_dma_kick(xscd);
	spin_unlock(&xscd->lock);
}

/* -----------------------------------------------------------------------------
 * DMA Engine
 */

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

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
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

static bool xscd_dma_is_running(struct xscd_dma_chan *chan)
{
	bool running;

	spin_lock_irq(&chan->lock);
	running = chan->running;
	spin_unlock_irq(&chan->lock);

	return running;
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
	int ret;

	spin_lock_irq(&chan->lock);
	chan->enabled = false;
	spin_unlock_irq(&chan->lock);

	/* Wait for any on-going transfer to complete. */
	ret = wait_event_timeout(chan->wait, !xscd_dma_is_running(chan),
				 msecs_to_jiffies(100));
	WARN_ON(ret == 0);

	xscd_dma_free_descriptors(chan);
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
	unsigned long flags;

	spin_lock_irqsave(&xscd->lock, flags);
	xscd_dma_kick(xscd);
	spin_unlock_irqrestore(&xscd->lock, flags);
}

static enum dma_status xscd_dma_tx_status(struct dma_chan *dchan,
					  dma_cookie_t cookie,
					  struct dma_tx_state *txstate)
{
	return dma_cookie_status(dchan, cookie, txstate);
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
	struct xscd_dma_chan *chan = &xscd->chans[chan_id].dmachan;

	chan->id = chan_id;
	chan->iomem = xscd->iomem + chan->id * XSCD_CHAN_OFFSET;
	chan->xscd = xscd;

	xscd->channels[chan->id] = chan;

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);
	tasklet_init(&chan->tasklet, xscd_dma_do_tasklet,
		     (unsigned long)chan);
	init_waitqueue_head(&chan->wait);

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

	dev_info(xscd->dev, "Xilinx Scene Change DMA is initialized!\n");
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

// SPDX-License-Identifier: GPL-2.0
/*
 * DMA Interface for Multimedia Integrated Display Controller Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include "mmi_dc_dma.h"

#include <linux/dmaengine.h>
#include <linux/dma/xilinx_dpdma.h>

/**
 * struct mmi_dc_dma_chan - DC DMA channel
 * @dev: DMA channel client device
 * @dma_chan: DMA engine channel
 * @xt: interleaved DMA transfer template
 */
struct mmi_dc_dma_chan {
	struct device			*dev;
	struct dma_chan			*dma_chan;
	struct dma_interleaved_template	*xt;
};

/**
 * mmi_dc_dma_request_channel - Request named DMA channel
 * @dev: the MMI DC device
 * @name: requested DMA channel name
 *
 * Return: Managed pointer to the new instance of MMI DC DMA channel on
 *	   success or error pointer otherwise.
 */
struct mmi_dc_dma_chan *mmi_dc_dma_request_channel(struct device *dev,
						   const char *name)
{
	struct mmi_dc_dma_chan *chan = devm_kzalloc(dev, sizeof(*chan),
						    GFP_KERNEL);
	size_t xt_alloc_size;

	if (!chan)
		return ERR_PTR(-ENOMEM);

	chan->dev = dev;
	chan->dma_chan = dma_request_chan(dev, name);

	if (IS_ERR(chan->dma_chan))
		return (void *)chan->dma_chan;

	xt_alloc_size = sizeof(struct dma_interleaved_template) +
			sizeof(struct data_chunk);
	chan->xt = devm_kzalloc(dev, xt_alloc_size, GFP_KERNEL);
	if (!chan->xt)
		return ERR_PTR(-ENOMEM);

	chan->xt->dir = DMA_MEM_TO_DEV;
	chan->xt->src_sgl = true;
	chan->xt->frame_size = 1;

	return chan;
}

/**
 * mmi_dc_dma_release_channel - Release DMA channel
 * @chan: the MMI DC DMA channel to release
 */
void mmi_dc_dma_release_channel(struct mmi_dc_dma_chan *chan)
{
	if (WARN_ON(!chan))
		return;

	dmaengine_terminate_sync(chan->dma_chan);
	dma_release_channel(chan->dma_chan);
}

/**
 * mmi_dc_dma_copy_align - Request the DMA device copy alignment
 * @chan: the MMI DC DMA channel
 *
 * Return: DMA device data buffer alignment constraint.
 */
unsigned int mmi_dc_dma_copy_align(struct mmi_dc_dma_chan *chan)
{
	if (WARN_ON(!chan))
		return 0;

	return 1 << chan->dma_chan->device->copy_align;
}

/**
 * mmi_dc_dma_config_channel - Configure DMA channel
 * @chan: the MMI DC DMA channel to configure
 * @target_addr: in-device target DMA address
 * @video_group: true if the channel belongs to a video group
 */
void mmi_dc_dma_config_channel(struct mmi_dc_dma_chan *chan,
			       dma_addr_t target_addr, bool video_group)
{
	struct xilinx_dpdma_peripheral_config platform_config = {
		.video_group = video_group,
	};
	struct dma_slave_config dma_config = {
		.direction = DMA_MEM_TO_DEV,
		.dst_addr = target_addr,
		.peripheral_config = &platform_config,
		.peripheral_size = sizeof(platform_config),
	};

	if (WARN_ON(!chan))
		return;

	dmaengine_slave_config(chan->dma_chan, &dma_config);
}

/**
 * mmi_dc_dma_start_transfer - Start the DMA transfer
 * @chan: the MMI DC DMA channel
 * @buffer_addr: frame buffer data DMA address
 * @line_size: size of the active pixel line in bytes
 * @line_stride: distance between two consecutive pixel lines in bytes
 * @num_lines: the number of pixel lines in the buffer
 * @auto_repeat: true if DMA transfer should loop after completion
 */
void mmi_dc_dma_start_transfer(struct mmi_dc_dma_chan *chan,
			       dma_addr_t buffer_addr, size_t line_size,
			       size_t line_stride, size_t num_lines,
			       bool auto_repeat)
{
	struct dma_async_tx_descriptor *desc;
	unsigned int flags = DMA_CTRL_ACK;

	if (WARN_ON(!chan))
		return;

	if (auto_repeat)
		flags |= DMA_PREP_REPEAT | DMA_PREP_LOAD_EOT;
	chan->xt->numf = num_lines;
	chan->xt->src_start = buffer_addr;
	chan->xt->sgl[0].size = line_size;
	chan->xt->sgl[0].icg = line_stride - line_size;

	desc = dmaengine_prep_interleaved_dma(chan->dma_chan, chan->xt, flags);
	if (!desc) {
		dev_err(chan->dev, "failed to prepare DMA descriptor\n");
		return;
	}

	dmaengine_submit(desc);
	dma_async_issue_pending(chan->dma_chan);
}

/**
 * mmi_dc_dma_stop_transfer - Stop the current DMA transfer
 * @chan: the MMI DC DMA channel
 */
void mmi_dc_dma_stop_transfer(struct mmi_dc_dma_chan *chan)
{
	if (WARN_ON(!chan))
		return;

	dmaengine_terminate_sync(chan->dma_chan);
}

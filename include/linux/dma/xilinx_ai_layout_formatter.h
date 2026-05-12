/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Xilinx AI Layout Formatter DMA support header file
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Mounik Katikala <mounik.katikala@amd.com>
 */

#ifndef __XILINX_AI_LAYOUT_FORMATTER_DMA_H
#define __XILINX_AI_LAYOUT_FORMATTER_DMA_H

#include <linux/dma/xilinx_video_dma_common.h>
#include <linux/dmaengine.h>
#include <linux/types.h>

#if IS_REACHABLE(CONFIG_XILINX_AI_LAYOUT_FORMATTER)
/**
 * xilinx_ai_xdma_set_mode - Set operation mode for AI Layout Formatter DMA
 * @chan: DMA channel instance
 * @mode: Operation mode (default or auto-restart)
 *
 * This API configures the operation mode for the Xilinx AI Layout Formatter DMA
 * (for example, enabling auto-restart for streaming pipelines, or default mode
 * for memory-to-memory use cases). Must be invoked before dma_async_issue_pending().
 */
void xilinx_ai_xdma_set_mode(struct dma_chan *chan, enum xilinx_vid_dma_mode mode);

/**
 * xilinx_ai_xdma_v4l2_config - Configure video format for AI Layout Formatter DMA
 * @chan: DMA channel instance
 * @v4l2_fourcc: V4L2 fourcc code describing the memory layout of video data
 *
 * This API sets the video format to be used by the AI Layout Formatter DMA hardware.
 * It should be called before dma_async_issue_pending(), and is intended for "video
 * format aware" Xilinx DMA IP blocks (such as AI Layout Formatter Write). This call
 * establishes the hardware's expected video data layout based on the specified V4L2
 * fourcc format code.
 */
void xilinx_ai_xdma_v4l2_config(struct dma_chan *chan, u32 v4l2_fourcc);

/**
 * xilinx_ai_xdma_get_v4l2_vid_fmts - Obtain list of supported V4L2 memory formats
 * @chan: DMA channel instance
 * @fmt_cnt: Output parameter - receives total count of supported V4L2 fourcc codes
 * @fmts: Output parameter - pointer to internal array of V4L2 fourcc codes (do not free)
 *
 * Returns: 0 on success, negative errno on failure. On success, @fmt_cnt is set to the
 * number of supported V4L2 fourcc codes, and @fmts points to an internal array of
 * fourcc codes supported by this instance of the AI Layout Formatter DMA.
 */
int xilinx_ai_xdma_get_v4l2_vid_fmts(struct dma_chan *chan, u32 *fmt_cnt,
				     u32 **fmts);

/**
 * xilinx_ai_xdma_get_width_align - Retrieve the required width alignment for DMA
 * @chan: DMA channel instance
 * @width_align: Output parameter to receive the width alignment value
 *
 * Return: 0 on success, or -ENODEV if no compatible AI Layout Formatter device is found
 */
int xilinx_ai_xdma_get_width_align(struct dma_chan *chan, u32 *width_align);

/**
 * xilinx_ai_layout_formatter_is_dma_device - Check if this is an AI Layout Formatter DMA device
 * @chan: DMA channel instance
 *
 * Returns: true if the channel is an AI Layout Formatter DMA device, false otherwise.
 */
bool xilinx_ai_layout_formatter_is_dma_device(const struct dma_chan *chan);

#else
static inline void xilinx_ai_xdma_set_mode(struct dma_chan *chan,
					   enum xilinx_vid_dma_mode mode)
{ }

static inline void xilinx_ai_xdma_v4l2_config(struct dma_chan *chan,
					      u32 v4l2_fourcc)
{ }

static inline int xilinx_ai_xdma_get_v4l2_vid_fmts(struct dma_chan *chan,
						   u32 *fmt_cnt, u32 **fmts)
{
	return -ENODEV;
}

static inline int xilinx_ai_xdma_get_width_align(struct dma_chan *chan, u32 *width_align)
{
	return -ENODEV;
}

static inline bool xilinx_ai_layout_formatter_is_dma_device(const struct dma_chan *chan)
{
	return false;
}
#endif

#endif /* __XILINX_AI_LAYOUT_FORMATTER_DMA_H */

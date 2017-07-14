/*
 * Xilinx Framebuffer DMA support header file
 *
 * Copyright (C) 2017 Xilinx, Inc. All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __XILINX_FRMBUF_DMA_H
#define __XILINX_FRMBUF_DMA_H

#include <linux/dmaengine.h>

/**
 * enum vid_frmwork_type - Linux video framework type
 * @XDMA_DRM: fourcc is of type DRM
 * @XDMA_V4L2: fourcc is of type V4L2
 */
enum vid_frmwork_type {
	XDMA_DRM = 0,
	XDMA_V4L2,
};

#ifdef CONFIG_XILINX_FRMBUF
/**
 * xilinx_xdma_drm_config - configure video format in video aware DMA
 * @chan: dma channel instance
 * @drm_fourcc: DRM fourcc code describing the memory layout of video data
 *
 * This routine is used when utilizing "video format aware" Xilinx DMA IP
 * (such as Video Framebuffer Read or Video Framebuffer Write).  This call
 * must be made prior to dma_async_issue_pending() to enstablish the video
 * data memory format within the hardware DMA.
 */
void xilinx_xdma_drm_config(struct dma_chan *chan, u32 drm_fourcc);

/**
 * xilinx_xdma_drm_config - configure video format in video aware DMA
 * @chan: dma channel instance
 * @v4l2_fourcc: V4L2 fourcc code describing the memory layout of video data
 *
 * This routine is used when utilizing "video format aware" Xilinx DMA IP
 * (such as Video Framebuffer Read or Video Framebuffer Write).  This call
 * must be made prior to dma_async_issue_pending() to enstablish the video
 * data memory format within the hardware DMA.
 */
void xilinx_xdma_v4l2_config(struct dma_chan *chan, u32 v4l2_fourcc);
#else
static inline void xilinx_xdma_drm_config(struct dma_chan *chan, u32 drm_fourcc)
{ }

static inline void xilinx_xdma_v4l2_config(struct dma_chan *chan,
					   u32 v4l2_fourcc)
{ }
#endif

#endif /*__XILINX_FRMBUF_DMA_H*/

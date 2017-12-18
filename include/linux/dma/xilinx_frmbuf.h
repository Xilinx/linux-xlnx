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

#if IS_ENABLED(CONFIG_XILINX_FRMBUF)
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

/**
 * xilinx_xdma_get_drm_vid_fmts - obtain list of supported DRM mem formats
 * @chan: dma channel instance
 * @fmt_cnt: Output param - total count of supported DRM fourcc codes
 * @fmts: Output param - pointer to array of DRM fourcc codes (not a copy)
 *
 * Return: a reference to an array of DRM fourcc codes supported by this
 * instance of the Video Framebuffer Driver
 */
int xilinx_xdma_get_drm_vid_fmts(struct dma_chan *chan, u32 *fmt_cnt,
				 u32 **fmts);

/**
 * xilinx_xdma_get_v4l2_vid_fmts - obtain list of supported V4L2 mem formats
 * @chan: dma channel instance
 * @fmt_cnt: Output param - total count of supported V4L2 fourcc codes
 * @fmts: Output param - pointer to array of V4L2 fourcc codes (not a copy)
 *
 * Return: a reference to an array of V4L2 fourcc codes supported by this
 * instance of the Video Framebuffer Driver
 */
int xilinx_xdma_get_v4l2_vid_fmts(struct dma_chan *chan, u32 *fmt_cnt,
				  u32 **fmts);
#else
static inline void xilinx_xdma_drm_config(struct dma_chan *chan, u32 drm_fourcc)
{ }

static inline void xilinx_xdma_v4l2_config(struct dma_chan *chan,
					   u32 v4l2_fourcc)
{ }

int xilinx_xdma_get_drm_vid_fmts(struct dma_chan *chan, u32 *fmt_cnt,
				 u32 **fmts);
{
	return -ENODEV;
}

int xilinx_xdma_get_v4l2_vid_fmts(struct dma_chan *chan, u32 *fmt_cnt,
				  u32 **fmts);
{
	return -ENODEV;
}
#endif

#endif /*__XILINX_FRMBUF_DMA_H*/

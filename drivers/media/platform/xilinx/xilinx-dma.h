/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Video DMA
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef __XILINX_VIP_DMA_H__
#define __XILINX_VIP_DMA_H__

#include <linux/dma/xilinx_video_dma_common.h>
#include <linux/dmaengine.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>

struct dma_chan;
struct xvip_composite_device;
struct xvip_video_format;

/**
 * struct xvip_xdma_ops - Video-aware Xilinx DMA hooks (framebuffer vs AI layout formatter)
 * @v4l2_config: Program DMA for the active V4L2 memory layout
 * @get_v4l2_vid_fmts: List supported V4L2 fourcc codes for this channel
 * @set_mode: Set default vs auto-restart mode before dma_async_issue_pending()
 * @get_width_align: Query required horizontal width alignment in pixels
 *
 * Interlaced field-ID and early-callback setup stay as direct calls to
 * xilinx_xdma_get_fid(), xilinx_xdma_set_fid(), and xilinx_xdma_set_earlycb()
 * in xilinx-dma.c: they are implemented only for framebuffer DMA. AI layout
 * formatter channels get -ENODEV from those helpers; see comments there.
 */
struct xvip_xdma_ops {
	void (*v4l2_config)(struct dma_chan *chan, u32 v4l2_fourcc);
	int (*get_v4l2_vid_fmts)(struct dma_chan *chan, u32 *fmt_cnt, u32 **fmts);
	void (*set_mode)(struct dma_chan *chan, enum xilinx_vid_dma_mode mode);
	int (*get_width_align)(struct dma_chan *chan, u32 *width_align);
};

/**
 * struct xvip_pipeline - Xilinx Video IP pipeline structure
 * @pipe: media pipeline
 * @lock: protects the pipeline @stream_count
 * @use_count: number of DMA engines using the pipeline
 * @stream_count: number of DMA engines currently streaming
 * @num_dmas: number of DMA engines in the pipeline
 * @xdev: Composite device the pipe belongs to
 */
struct xvip_pipeline {
	struct media_pipeline pipe;

	struct mutex lock;
	unsigned int use_count;
	unsigned int stream_count;

	unsigned int num_dmas;
	struct xvip_composite_device *xdev;
};

static inline struct xvip_pipeline *to_xvip_pipeline(struct video_device *vdev)
{
	struct media_pipeline *pipe = video_device_pipeline(vdev);

	if (!pipe)
		return NULL;

	return container_of(pipe, struct xvip_pipeline, pipe);
}

/**
 * struct xvip_dma - Video DMA channel
 * @list: list entry in a composite device dmas list
 * @video: V4L2 video device associated with the DMA channel
 * @pad: media pad for the video device entity
 * @remote_subdev_med_bus: media bus format of sub-device
 * @ctrl_handler: V4L2 ctrl_handler for inheritance ctrls from subdev
 * @xdev: composite device the DMA channel belongs to
 * @pipe: pipeline belonging to the DMA channel
 * @port: composite device DT node port number for the DMA channel
 * @lock: protects the @format, @fmtinfo and @queue fields
 * @format: active V4L2 pixel format
 * @r: crop rectangle parameters
 * @fmtinfo: format information corresponding to the active @format
 * @poss_v4l2_fmts: All possible v4l formats supported
 * @poss_v4l2_fmt_cnt: number of supported v4l formats
 * @queue: vb2 buffers queue
 * @sequence: V4L2 buffers sequence number
 * @queued_bufs: list of queued buffers
 * @queued_lock: protects the buf_queued list
 * @dma: DMA engine channel
 * @align: transfer alignment required by the DMA channel (in bytes)
 * @width_align: width alignment required by the DMA channel (in bytes)
 * @xt: dma interleaved template for dma configuration
 * @sgl: data chunk structure for dma_interleaved_template
 * @prev_fid: Previous Field ID
 * @low_latency_cap: Low latency capture mode
 * @layout_formatter: DMA channel is AI layout formatter device
 * @xdma: Read-only ops table for this channel (framebuffer vs AI layout formatter)
 */
struct xvip_dma {
	struct list_head list;
	struct video_device video;
	struct media_pad pad;
	u32 remote_subdev_med_bus;

	struct v4l2_ctrl_handler ctrl_handler;

	struct xvip_composite_device *xdev;
	struct xvip_pipeline pipe;
	unsigned int port;

	struct mutex lock;
	struct v4l2_format format;
	struct v4l2_rect r;
	const struct xvip_video_format *fmtinfo;
	u32 *poss_v4l2_fmts;
	u32 poss_v4l2_fmt_cnt;

	struct vb2_queue queue;
	unsigned int sequence;

	struct list_head queued_bufs;
	spinlock_t queued_lock;

	struct dma_chan *dma;
	unsigned int align;
	unsigned int width_align;
	struct dma_interleaved_template xt;
	struct data_chunk sgl;

	u32 prev_fid;
	u32 low_latency_cap;
	bool layout_formatter;
	const struct xvip_xdma_ops *xdma;
};

#define to_xvip_dma(vdev)	container_of(vdev, struct xvip_dma, video)

int xvip_dma_init(struct xvip_composite_device *xdev, struct xvip_dma *dma,
		  enum v4l2_buf_type type, unsigned int port);
void xvip_dma_cleanup(struct xvip_dma *dma);

#endif /* __XILINX_VIP_DMA_H__ */

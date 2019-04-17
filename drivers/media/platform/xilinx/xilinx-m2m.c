//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx V4L2 mem2mem driver
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 * Author: Satish Kumar Nagireddy <satish.nagireddy.nagireddy@xilinx.com>
 */

#include <drm/drm_fourcc.h>
#include <linux/delay.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/lcm.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "xilinx-vip.h"

#define XVIP_M2M_NAME		"xilinx-mem2mem"
#define XVIP_M2M_DEFAULT_FMT	V4L2_PIX_FMT_RGB24

/* Minimum and maximum widths are expressed in bytes */
#define XVIP_M2M_MIN_WIDTH	1U
#define XVIP_M2M_MAX_WIDTH	65535U
#define XVIP_M2M_MIN_HEIGHT	1U
#define XVIP_M2M_MAX_HEIGHT	8191U

#define XVIP_M2M_DEF_WIDTH	1920
#define XVIP_M2M_DEF_HEIGHT	1080

#define XVIP_M2M_PAD_SINK	1
#define XVIP_M2M_PAD_SOURCE	0

/**
 * struct xvip_graph_entity - Entity in the video graph
 * @list: list entry in a graph entities list
 * @node: the entity's DT node
 * @entity: media entity, from the corresponding V4L2 subdev
 * @asd: subdev asynchronous registration information
 * @subdev: V4L2 subdev
 * @streaming: status of the V4L2 subdev if streaming or not
 */
struct xvip_graph_entity {
	struct list_head list;
	struct device_node *node;
	struct media_entity *entity;

	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;
	bool streaming;
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

	/* protects the pipeline @stream_count */
	struct mutex lock;
	unsigned int use_count;
	unsigned int stream_count;

	unsigned int num_dmas;
	struct xvip_m2m_dev *xdev;
};

struct xventity_list {
	struct list_head list;
	struct media_entity *entity;
};

/**
 * struct xvip_m2m_dev - Xilinx Video mem2mem device structure
 * @v4l2_dev: V4L2 device
 * @dev: (OF) device
 * @media_dev: media device
 * @notifier: V4L2 asynchronous subdevs notifier
 * @entities: entities in the graph as a list of xvip_graph_entity
 * @num_subdevs: number of subdevs in the pipeline
 * @lock: This is to protect mem2mem context structure data
 * @queued_lock: This is to protect video buffer information
 * @dma: Video DMA channels
 * @m2m_dev: V4L2 mem2mem device structure
 * @v4l2_caps: V4L2 capabilities of the whole device
 */
struct xvip_m2m_dev {
	struct v4l2_device v4l2_dev;
	struct device *dev;

	struct media_device media_dev;
	struct v4l2_async_notifier notifier;
	struct list_head entities;
	unsigned int num_subdevs;

	/* Protects to m2m context data */
	struct mutex lock;

	/* Protects vb2_v4l2_buffer data */
	spinlock_t queued_lock;
	struct xvip_m2m_dma *dma;
	struct v4l2_m2m_dev *m2m_dev;
	u32 v4l2_caps;
};

static inline struct xvip_pipeline *to_xvip_pipeline(struct media_entity *e)
{
	return container_of(e->pipe, struct xvip_pipeline, pipe);
}

/**
 * struct xvip_m2m_dma - Video DMA channel
 * @video: V4L2 video device associated with the DMA channel
 * @xdev: composite mem2mem device the DMA channels belongs to
 * @chan_tx: DMA engine channel for MEM2DEV transfer
 * @chan_rx: DMA engine channel for DEV2MEM transfer
 * @outfmt: active V4L2 OUTPUT port pixel format
 * @capfmt: active V4L2 CAPTURE port pixel format
 * @r: crop rectangle parameters
 * @outinfo: format information corresponding to the active @outfmt
 * @capinfo: format information corresponding to the active @capfmt
 * @align: transfer alignment required by the DMA channel (in bytes)
 * @crop: boolean flag to indicate if crop is requested
 * @pads: media pads for the video M2M device entity
 * @pipe: pipeline belonging to the DMA channel
 */
struct xvip_m2m_dma {
	struct video_device video;
	struct xvip_m2m_dev *xdev;
	struct dma_chan *chan_tx;
	struct dma_chan *chan_rx;
	struct v4l2_format outfmt;
	struct v4l2_format capfmt;
	struct v4l2_rect r;
	const struct xvip_video_format *outinfo;
	const struct xvip_video_format *capinfo;
	u32 align;
	bool crop;

	struct media_pad pads[2];
	struct xvip_pipeline pipe;
};

/**
 * struct xvip_m2m_ctx - VIPP mem2mem context
 * @fh: V4L2 file handler
 * @xdev: composite mem2mem device the DMA channels belongs to
 * @xt: dma interleaved template for dma configuration
 * @sgl: data chunk structure for dma_interleaved_template
 */
struct xvip_m2m_ctx {
	struct v4l2_fh fh;
	struct xvip_m2m_dev *xdev;
	struct dma_interleaved_template xt;
	struct data_chunk sgl[1];
};

static inline struct xvip_m2m_ctx *file2ctx(struct file *file)
{
	return container_of(file->private_data, struct xvip_m2m_ctx, fh);
}

static struct v4l2_subdev *
xvip_dma_remote_subdev(struct media_pad *local, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(local);
	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int xvip_dma_verify_format(struct xvip_m2m_dma *dma)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	int ret;
	int width, height;

	subdev = xvip_dma_remote_subdev(&dma->pads[XVIP_PAD_SOURCE], &fmt.pad);
	if (!subdev)
		return -EPIPE;

	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	if (dma->outinfo->code != fmt.format.code)
		return -EINVAL;

	if (V4L2_TYPE_IS_MULTIPLANAR(dma->outfmt.type)) {
		width = dma->outfmt.fmt.pix_mp.width;
		height = dma->outfmt.fmt.pix_mp.height;
	} else {
		width = dma->outfmt.fmt.pix.width;
		height = dma->outfmt.fmt.pix.height;
	}

	if (width != fmt.format.width || height != fmt.format.height)
		return -EINVAL;

	return 0;
}

#define to_xvip_dma(vdev)	container_of(vdev, struct xvip_m2m_dma, video)
/* -----------------------------------------------------------------------------
 * Pipeline Stream Management
 */

/**
 * xvip_subdev_set_streaming - Find and update streaming status of subdev
 * @xdev: Composite video device
 * @subdev: V4L2 sub-device
 * @enable: enable/disable streaming status
 *
 * Walk the xvip graph entities list and find if subdev is present. Returns
 * streaming status of subdev and update the status as requested
 *
 * Return: streaming status (true or false) if successful or warn_on if subdev
 * is not present and return false
 */
static bool xvip_subdev_set_streaming(struct xvip_m2m_dev *xdev,
				      struct v4l2_subdev *subdev, bool enable)
{
	struct xvip_graph_entity *entity;

	list_for_each_entry(entity, &xdev->entities, list)
		if (entity->node == subdev->dev->of_node) {
			bool status = entity->streaming;

			entity->streaming = enable;
			return status;
		}

	WARN(1, "Should never get here\n");
	return false;
}

static int xvip_entity_start_stop(struct xvip_m2m_dev *xdev,
				  struct media_entity *entity, bool start)
{
	struct v4l2_subdev *subdev;
	bool is_streaming;
	int ret = 0;

	dev_dbg(xdev->dev, "%s entity %s\n",
		start ? "Starting" : "Stopping", entity->name);
	subdev = media_entity_to_v4l2_subdev(entity);

	/* This is to maintain list of stream on/off devices */
	is_streaming = xvip_subdev_set_streaming(xdev, subdev, start);

	/*
	 * start or stop the subdev only once in case if they are
	 * shared between sub-graphs
	 */
	if (start && !is_streaming) {
		/* power-on subdevice */
		ret = v4l2_subdev_call(subdev, core, s_power, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			dev_err(xdev->dev,
				"s_power on failed on subdev\n");
			xvip_subdev_set_streaming(xdev, subdev, 0);
			return ret;
		}

		/* stream-on subdevice */
		ret = v4l2_subdev_call(subdev, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			dev_err(xdev->dev,
				"s_stream on failed on subdev\n");
			v4l2_subdev_call(subdev, core, s_power, 0);
			xvip_subdev_set_streaming(xdev, subdev, 0);
		}
	} else if (!start && is_streaming) {
		/* stream-off subdevice */
		ret = v4l2_subdev_call(subdev, video, s_stream, 0);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			dev_err(xdev->dev,
				"s_stream off failed on subdev\n");
			xvip_subdev_set_streaming(xdev, subdev, 1);
		}

		/* power-off subdevice */
		ret = v4l2_subdev_call(subdev, core, s_power, 0);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			dev_err(xdev->dev,
				"s_power off failed on subdev\n");
	}

	return ret;
}

/**
 * xvip_pipeline_start_stop - Start ot stop streaming on a pipeline
 * @xdev: Composite video device
 * @dma: xvip dma
 * @start: Start (when true) or stop (when false) the pipeline
 *
 * Walk the entities chain starting @dma and start or stop all of them
 *
 * Return: 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise.
 */
static int xvip_pipeline_start_stop(struct xvip_m2m_dev *xdev,
				    struct xvip_m2m_dma *dma, bool start)
{
	struct media_graph graph;
	struct media_entity *entity = &dma->video.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	struct xventity_list *temp, *_temp;
	LIST_HEAD(ent_list);
	int ret = 0;

	mutex_lock(&mdev->graph_mutex);

	/* Walk the graph to locate the subdev nodes */
	ret = media_graph_walk_init(&graph, mdev);
	if (ret)
		goto error;

	media_graph_walk_start(&graph, entity);

	/* get the list of entities */
	while ((entity = media_graph_walk_next(&graph))) {
		struct xventity_list *ele;

		/* We want to stream on/off only subdevs */
		if (!is_media_entity_v4l2_subdev(entity))
			continue;

		/* Maintain the pipeline sequence in a list */
		ele = kzalloc(sizeof(*ele), GFP_KERNEL);
		if (!ele) {
			ret = -ENOMEM;
			goto error;
		}

		ele->entity = entity;
		list_add(&ele->list, &ent_list);
	}

	if (start) {
		list_for_each_entry_safe(temp, _temp, &ent_list, list) {
			/* Enable all subdevs from sink to source */
			ret = xvip_entity_start_stop(xdev, temp->entity, start);
			if (ret < 0) {
				dev_err(xdev->dev, "ret = %d for entity %s\n",
					ret, temp->entity->name);
				break;
			}
		}
	} else {
		list_for_each_entry_safe_reverse(temp, _temp, &ent_list, list)
			/* Enable all subdevs from source to sink */
			xvip_entity_start_stop(xdev, temp->entity, start);
	}

	list_for_each_entry_safe(temp, _temp, &ent_list, list) {
		list_del(&temp->list);
		kfree(temp);
	}

error:
	mutex_unlock(&mdev->graph_mutex);
	media_graph_walk_cleanup(&graph);
	return ret;
}

/**
 * xvip_pipeline_set_stream - Enable/disable streaming on a pipeline
 * @pipe: The pipeline
 * @on: Turn the stream on when true or off when false
 *
 * The pipeline is shared between all DMA engines connect at its input and
 * output. While the stream state of DMA engines can be controlled
 * independently, pipelines have a shared stream state that enable or disable
 * all entities in the pipeline. For this reason the pipeline uses a streaming
 * counter that tracks the number of DMA engines that have requested the stream
 * to be enabled. This will walk the graph starting from each DMA and enable or
 * disable the entities in the path.
 *
 * When called with the @on argument set to true, this function will increment
 * the pipeline streaming count. If the streaming count reaches the number of
 * DMA engines in the pipeline it will enable all entities that belong to the
 * pipeline.
 *
 * Similarly, when called with the @on argument set to false, this function will
 * decrement the pipeline streaming count and disable all entities in the
 * pipeline when the streaming count reaches zero.
 *
 * Return: 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise. Stopping the pipeline never fails. The pipeline state is
 * not updated when the operation fails.
 */
static int xvip_pipeline_set_stream(struct xvip_pipeline *pipe, bool on)
{
	struct xvip_m2m_dev *xdev;
	struct xvip_m2m_dma *dma;
	int ret = 0;

	mutex_lock(&pipe->lock);
	xdev = pipe->xdev;
	dma = xdev->dma;

	if (on) {
		ret = xvip_pipeline_start_stop(xdev, dma, true);
		if (ret < 0)
			goto done;
		pipe->stream_count++;
	} else {
		if (--pipe->stream_count == 0)
			xvip_pipeline_start_stop(xdev, dma, false);
	}

done:
	mutex_unlock(&pipe->lock);
	return ret;
}

static int xvip_pipeline_validate(struct xvip_pipeline *pipe,
				  struct xvip_m2m_dma *start)
{
	struct media_graph graph;
	struct media_entity *entity = &start->video.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	unsigned int num_inputs = 0;
	unsigned int num_outputs = 0;
	int ret;

	mutex_lock(&mdev->graph_mutex);

	/* Walk the graph to locate the video nodes. */
	ret = media_graph_walk_init(&graph, mdev);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		return ret;
	}

	media_graph_walk_start(&graph, entity);

	while ((entity = media_graph_walk_next(&graph))) {
		struct xvip_m2m_dma *dma;

		if (entity->function != MEDIA_ENT_F_IO_V4L)
			continue;

		dma = to_xvip_dma(media_entity_to_video_device(entity));

		num_outputs++;
		num_inputs++;
	}

	mutex_unlock(&mdev->graph_mutex);

	media_graph_walk_cleanup(&graph);

	/* We need at least one DMA to proceed */
	if (num_outputs == 0 && num_inputs == 0)
		return -EPIPE;

	pipe->num_dmas = num_inputs + num_outputs;
	pipe->xdev = start->xdev;

	return 0;
}

static void __xvip_pipeline_cleanup(struct xvip_pipeline *pipe)
{
	pipe->num_dmas = 0;
}

/**
 * xvip_pipeline_cleanup - Cleanup the pipeline after streaming
 * @pipe: the pipeline
 *
 * Decrease the pipeline use count and clean it up if we were the last user.
 */
static void xvip_pipeline_cleanup(struct xvip_pipeline *pipe)
{
	mutex_lock(&pipe->lock);

	/* If we're the last user clean up the pipeline. */
	if (--pipe->use_count == 0)
		__xvip_pipeline_cleanup(pipe);

	mutex_unlock(&pipe->lock);
}

/**
 * xvip_pipeline_prepare - Prepare the pipeline for streaming
 * @pipe: the pipeline
 * @dma: DMA engine at one end of the pipeline
 *
 * Validate the pipeline if no user exists yet, otherwise just increase the use
 * count.
 *
 * Return: 0 if successful or -EPIPE if the pipeline is not valid.
 */
static int xvip_pipeline_prepare(struct xvip_pipeline *pipe,
				 struct xvip_m2m_dma *dma)
{
	int ret;

	mutex_lock(&pipe->lock);

	/* If we're the first user validate and initialize the pipeline. */
	if (pipe->use_count == 0) {
		ret = xvip_pipeline_validate(pipe, dma);
		if (ret < 0) {
			__xvip_pipeline_cleanup(pipe);
			goto done;
		}
	}

	pipe->use_count++;
	ret = 0;

done:
	mutex_unlock(&pipe->lock);
	return ret;
}

static void xvip_m2m_dma_callback_mem2dev(void *data)
{
}

static void xvip_m2m_dma_callback(void *data)
{
	struct xvip_m2m_ctx *ctx = data;
	struct xvip_m2m_dev *xdev = ctx->xdev;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;

	spin_lock(&xdev->queued_lock);
	src_vb = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;
	dst_vb->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_vb->flags |=
		src_vb->flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_vb->timecode = src_vb->timecode;

	v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
	v4l2_m2m_job_finish(xdev->m2m_dev, ctx->fh.m2m_ctx);
	spin_unlock(&xdev->queued_lock);
}

/*
 * Queue operations
 */

static int xvip_m2m_queue_setup(struct vb2_queue *vq,
				u32 *nbuffers, u32 *nplanes,
				u32 sizes[], struct device *alloc_devs[])
{
	struct xvip_m2m_ctx *ctx = vb2_get_drv_priv(vq);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct v4l2_format *f;
	const struct xvip_video_format *info;
	u32 i;

	if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		f = &dma->outfmt;
		info = dma->outinfo;
	} else {
		f = &dma->capfmt;
		info = dma->capinfo;
	}

	if (*nplanes) {
		if (*nplanes != f->fmt.pix_mp.num_planes)
			return -EINVAL;

		for (i = 0; i < *nplanes; i++) {
			if (sizes[i] < f->fmt.pix_mp.plane_fmt[i].sizeimage)
				return -EINVAL;
		}
	} else {
		*nplanes = info->buffers;
		for (i = 0; i < info->buffers; i++)
			sizes[i] = f->fmt.pix_mp.plane_fmt[i].sizeimage;
	}

	return 0;
}

static int xvip_m2m_buf_prepare(struct vb2_buffer *vb)
{
	struct xvip_m2m_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct v4l2_format *f;
	const struct xvip_video_format *info;
	u32 i;

	if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		f = &dma->outfmt;
		info = dma->outinfo;
	} else {
		f = &dma->capfmt;
		info = dma->capinfo;
	}

	for (i = 0; i < info->buffers; i++) {
		if (vb2_plane_size(vb, i) <
			f->fmt.pix_mp.plane_fmt[i].sizeimage) {
			dev_err(ctx->xdev->dev,
				"insufficient plane size (%u < %u)\n",
				(u32)vb2_plane_size(vb, i),
				f->fmt.pix_mp.plane_fmt[i].sizeimage);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i,
				      f->fmt.pix_mp.plane_fmt[i].sizeimage);
	}

	return 0;
}

static void xvip_m2m_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct xvip_m2m_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void xvip_m2m_stop_streaming(struct vb2_queue *q)
{
	struct xvip_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct xvip_pipeline *pipe = to_xvip_pipeline(&dma->video.entity);
	struct vb2_v4l2_buffer *vbuf;

	dma->crop = false;
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		dmaengine_terminate_sync(dma->chan_tx);
	else
		dmaengine_terminate_sync(dma->chan_rx);

	if (ctx->xdev->num_subdevs) {
		/* Stop the pipeline. */
		xvip_pipeline_set_stream(pipe, false);

		/* Cleanup the pipeline and mark it as being stopped. */
		xvip_pipeline_cleanup(pipe);
		media_pipeline_stop(&dma->video.entity);
	}

	for (;;) {
		if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!vbuf)
			return;

		spin_lock(&ctx->xdev->queued_lock);
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
		spin_unlock(&ctx->xdev->queued_lock);
	}
}

static int xvip_m2m_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct xvip_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct xvip_m2m_dev *xdev = ctx->xdev;
	struct xvip_pipeline *pipe;
	int ret;

	if (!xdev->num_subdevs)
		return 0;

	pipe = dma->video.entity.pipe
	     ? to_xvip_pipeline(&dma->video.entity) : &dma->pipe;

	ret = media_pipeline_start(&dma->video.entity, &pipe->pipe);
	if (ret < 0)
		goto error;

	/* Verify that the configured format matches the output of the
	 * connected subdev.
	 */
	ret = xvip_dma_verify_format(dma);
	if (ret < 0)
		goto error_stop;

	ret = xvip_pipeline_prepare(pipe, dma);
	if (ret < 0)
		goto error_stop;

	/* Start the pipeline. */
	ret = xvip_pipeline_set_stream(pipe, true);
	if (ret < 0)
		goto error_stop;

	return 0;
error_stop:
	media_pipeline_stop(&dma->video.entity);

error:
	xvip_m2m_stop_streaming(q);

	return ret;
}

static const struct vb2_ops m2m_vb2_ops = {
	.queue_setup = xvip_m2m_queue_setup,
	.buf_prepare = xvip_m2m_buf_prepare,
	.buf_queue = xvip_m2m_buf_queue,
	.start_streaming = xvip_m2m_start_streaming,
	.stop_streaming = xvip_m2m_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int xvip_m2m_queue_init(void *priv, struct vb2_queue *src_vq,
			       struct vb2_queue *dst_vq)
{
	struct xvip_m2m_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &m2m_vb2_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->dev = ctx->xdev->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &m2m_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->dev = ctx->xdev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int
xvip_dma_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	strlcpy(cap->driver, XVIP_M2M_NAME, sizeof(cap->driver));
	strlcpy(cap->card, XVIP_M2M_NAME, sizeof(cap->card));
	strlcpy(cap->bus_info, XVIP_M2M_NAME, sizeof(cap->card));

	return 0;
}

static int
xvip_m2m_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct xvip_m2m_ctx *ctx = file2ctx(file);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	const struct xvip_video_format *fmtinfo;
	const struct xvip_video_format *fmt;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_format v4l_fmt;
	struct xvip_m2m_dev *xdev = ctx->xdev;
	u32 i, fmt_cnt, *fmts;
	int ret;

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		ret = xilinx_xdma_get_v4l2_vid_fmts(dma->chan_rx,
						    &fmt_cnt, &fmts);
	else
		ret = xilinx_xdma_get_v4l2_vid_fmts(dma->chan_tx,
						    &fmt_cnt, &fmts);
	if (ret)
		return ret;

	if (f->index >= fmt_cnt)
		return -EINVAL;

	if (!xdev->num_subdevs) {
		fmt = xvip_get_format_by_fourcc(fmts[f->index]);
		if (IS_ERR(fmt))
			return PTR_ERR(fmt);

		f->pixelformat = fmt->fourcc;
		strlcpy(f->description, fmt->description,
			sizeof(f->description));
		return 0;
	}

	if (f->index > 0)
		return -EINVAL;

	/* Establish media pad format */
	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		subdev = xvip_dma_remote_subdev(&dma->pads[XVIP_PAD_SOURCE],
						&v4l_fmt.pad);
	else
		subdev = xvip_dma_remote_subdev(&dma->pads[XVIP_PAD_SINK],
						&v4l_fmt.pad);
	if (!subdev)
		return -EPIPE;

	v4l_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &v4l_fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	for (i = 0; i < fmt_cnt; i++) {
		fmt = xvip_get_format_by_fourcc(fmts[i]);
		if (IS_ERR(fmt))
			return PTR_ERR(fmt);

		if (fmt->code == v4l_fmt.format.code)
			break;
	}

	if (i >= fmt_cnt)
		return -EINVAL;

	fmtinfo = xvip_get_format_by_fourcc(fmts[i]);
	f->pixelformat = fmtinfo->fourcc;
	strlcpy(f->description, fmtinfo->description, sizeof(f->description));

	return 0;
}

static int xvip_m2m_get_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct xvip_m2m_ctx *ctx = file2ctx(file);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		f->fmt.pix_mp = dma->outfmt.fmt.pix_mp;
	else
		f->fmt.pix_mp = dma->capfmt.fmt.pix_mp;

	return 0;
}

static int __xvip_m2m_try_fmt(struct xvip_m2m_ctx *ctx, struct v4l2_format *f)
{
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	const struct xvip_video_format *info;
	struct v4l2_pix_format_mplane *pix_mp;
	struct v4l2_plane_pix_format *plane_fmt;
	u32 align, min_width, max_width;
	u32 bpl, min_bpl, max_bpl;
	u32 padding_factor_nume, padding_factor_deno;
	u32 bpl_nume, bpl_deno;
	u32 i, plane_width, plane_height;
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	struct xvip_m2m_dev *xdev = ctx->xdev;
	int ret;

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	    f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	if (xdev->num_subdevs) {
		if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			subdev = xvip_dma_remote_subdev
				(&dma->pads[XVIP_PAD_SOURCE], &fmt.pad);
		else
			subdev = xvip_dma_remote_subdev
				(&dma->pads[XVIP_PAD_SINK], &fmt.pad);

		if (!subdev)
			return -EPIPE;

		fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
		if (ret < 0)
			return -EINVAL;
	}

	pix_mp = &f->fmt.pix_mp;
	plane_fmt = pix_mp->plane_fmt;
	info = xvip_get_format_by_fourcc(f->fmt.pix_mp.pixelformat);
	if (info) {
		if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			dma->outinfo = info;
		else
			dma->capinfo = info;
	} else {
		info = xvip_get_format_by_fourcc(XVIP_M2M_DEFAULT_FMT);
	}

	if (xdev->num_subdevs) {
		if (info->code != fmt.format.code ||
		    fmt.format.width != pix_mp->width ||
		    fmt.format.height != pix_mp->height) {
			dev_err(xdev->dev, "Failed to set format\n");
			dev_info(xdev->dev,
				 "Reqed Code = %d, Width = %d, Height = %d\n",
				 info->code, pix_mp->width, pix_mp->height);
			dev_info(xdev->dev,
				 "Subdev Code = %d, Width = %d, Height = %d",
				 fmt.format.code, fmt.format.width,
				 fmt.format.height);
			return -EINVAL;
		}
	}

	xvip_width_padding_factor(info->fourcc, &padding_factor_nume,
				  &padding_factor_deno);
	xvip_bpl_scaling_factor(info->fourcc, &bpl_nume, &bpl_deno);

	/*
	 * V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported
	 */
	align = lcm(dma->align, info->bpp >> 3);
	min_width = roundup(XVIP_M2M_MIN_WIDTH, align);
	max_width = rounddown(XVIP_M2M_MAX_WIDTH, align);
	pix_mp->width = clamp(pix_mp->width, min_width, max_width);
	pix_mp->height = clamp(pix_mp->height, XVIP_M2M_MIN_HEIGHT,
			       XVIP_M2M_MAX_HEIGHT);

	/*
	 * Clamp the requested bytes per line value. If the maximum
	 * bytes per line value is zero, the module doesn't support
	 * user configurable line sizes. Override the requested value
	 * with the minimum in that case.
	 */
	max_bpl = rounddown(XVIP_M2M_MAX_WIDTH, align);

	if (info->buffers == 1) {
		/* Handling contiguous data with mplanes */
		min_bpl = (pix_mp->width * info->bpl_factor *
			   padding_factor_nume * bpl_nume) /
			   (padding_factor_deno * bpl_deno);
		min_bpl = roundup(min_bpl, align);
		bpl = roundup(plane_fmt[0].bytesperline, align);
		plane_fmt[0].bytesperline = clamp(bpl, min_bpl, max_bpl);

		if (info->num_planes == 1) {
			/* Single plane formats */
			plane_fmt[0].sizeimage = plane_fmt[0].bytesperline *
						 pix_mp->height;
		} else {
			/* Multi plane formats in contiguous buffer*/
			plane_fmt[0].sizeimage =
				DIV_ROUND_UP(plane_fmt[0].bytesperline *
					     pix_mp->height *
					     info->bpp, 8);
		}
	} else {
		/* Handling non-contiguous data with mplanes */
		for (i = 0; i < info->num_planes; i++) {
			plane_width = pix_mp->width / (i ? info->hsub : 1);
			plane_height = pix_mp->height / (i ? info->vsub : 1);
			min_bpl = (plane_width * info->bpl_factor *
				   padding_factor_nume * bpl_nume) /
				   (padding_factor_deno * bpl_deno);
			min_bpl = roundup(min_bpl, align);
			bpl = rounddown(plane_fmt[i].bytesperline, align);
			plane_fmt[i].bytesperline = clamp(bpl, min_bpl,
							  max_bpl);
			plane_fmt[i].sizeimage = plane_fmt[i].bytesperline *
						 plane_height;
		}
	}

	return 0;
}

static int xvip_m2m_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct xvip_m2m_ctx *ctx = file2ctx(file);
	int ret;

	ret = __xvip_m2m_try_fmt(ctx, f);
	if (ret < 0)
		return ret;

	return 0;
}

static int xvip_m2m_set_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct xvip_m2m_ctx *ctx = file2ctx(file);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->xdev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	ret = __xvip_m2m_try_fmt(ctx, f);
	if (ret < 0)
		return ret;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		dma->outfmt.fmt.pix_mp = f->fmt.pix_mp;
	else
		dma->capfmt.fmt.pix_mp = f->fmt.pix_mp;

	return 0;
}

static int
xvip_m2m_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct xvip_m2m_ctx *ctx = file2ctx(file);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	int ret = 0;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
		ret = -ENOTTY;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = dma->r.width;
		s->r.height = dma->r.height;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int
xvip_m2m_s_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct xvip_m2m_ctx *ctx = file2ctx(file);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	u32 min_width, max_width;
	int ret = 0;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
		ret = -ENOTTY;
		break;
	case V4L2_SEL_TGT_CROP:
		if (s->r.width > dma->outfmt.fmt.pix_mp.width ||
		    s->r.height > dma->outfmt.fmt.pix_mp.height ||
		    s->r.top != 0 || s->r.left != 0)
			return -EINVAL;

		dma->crop = true;
		min_width = roundup(XVIP_M2M_MIN_WIDTH, dma->align);
		max_width = rounddown(XVIP_M2M_MAX_WIDTH, dma->align);
		dma->r.width = clamp(s->r.width, min_width, max_width);
		dma->r.height = s->r.height;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ioctl_ops xvip_m2m_ioctl_ops = {
	.vidioc_querycap		= xvip_dma_querycap,

	.vidioc_enum_fmt_vid_cap_mplane	= xvip_m2m_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= xvip_m2m_get_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= xvip_m2m_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= xvip_m2m_set_fmt,

	.vidioc_enum_fmt_vid_out_mplane	= xvip_m2m_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane	= xvip_m2m_get_fmt,
	.vidioc_try_fmt_vid_out_mplane	= xvip_m2m_try_fmt,
	.vidioc_s_fmt_vid_out_mplane	= xvip_m2m_set_fmt,
	.vidioc_s_selection		= xvip_m2m_s_selection,
	.vidioc_g_selection		= xvip_m2m_g_selection,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,
};

/*
 * File operations
 */
static int xvip_m2m_open(struct file *file)
{
	struct xvip_m2m_dev *xdev = video_drvdata(file);
	struct xvip_m2m_ctx *ctx = NULL;
	int ret;

	ctx = devm_kzalloc(xdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->xdev = xdev;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(xdev->m2m_dev, ctx,
					    &xvip_m2m_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		v4l2_fh_exit(&ctx->fh);
		return ret;
	}

	v4l2_fh_add(&ctx->fh);
	dev_info(xdev->dev, "Created instance %p, m2m_ctx: %p\n", ctx,
		 ctx->fh.m2m_ctx);
	return 0;
}

static int xvip_m2m_release(struct file *file)
{
	struct xvip_m2m_ctx *ctx = file->private_data;

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	return 0;
}

static u32 xvip_m2m_poll(struct file *file,
			 struct poll_table_struct *wait)
{
	struct xvip_m2m_ctx *ctx = file->private_data;
	int ret;

	mutex_lock(&ctx->xdev->lock);
	ret = v4l2_m2m_poll(file, ctx->fh.m2m_ctx, wait);
	mutex_unlock(&ctx->xdev->lock);

	return ret;
}

static int xvip_m2m_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct xvip_m2m_ctx *ctx = file->private_data;

	return v4l2_m2m_mmap(file, ctx->fh.m2m_ctx, vma);
}

/*
 * mem2mem callbacks
 */

static int xvip_m2m_job_ready(void *priv)
{
	struct xvip_m2m_ctx *ctx = priv;

	if ((v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) > 0) &&
	    (v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx) > 0))
		return 1;

	return 0;
}

static void xvip_m2m_job_abort(void *priv)
{
	struct xvip_m2m_ctx *ctx = priv;

	/* Will cancel the transaction in the next interrupt handler */
	v4l2_m2m_job_finish(ctx->xdev->m2m_dev, ctx->fh.m2m_ctx);
}

static void xvip_m2m_prep_submit_dev2mem_desc(struct xvip_m2m_ctx *ctx,
					      struct vb2_v4l2_buffer *dst_buf)
{
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct xvip_m2m_dev *xdev = ctx->xdev;
	struct dma_async_tx_descriptor *desc;
	dma_addr_t p_out;
	const struct xvip_video_format *info;
	struct v4l2_pix_format_mplane *pix_mp;
	u32 padding_factor_nume, padding_factor_deno;
	u32 bpl_nume, bpl_deno;
	u32 luma_size;
	u32 flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
	enum operation_mode mode = DEFAULT;

	p_out = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);

	if (!p_out) {
		dev_err(xdev->dev,
			"Acquiring kernel pointer to buffer failed\n");
		return;
	}

	ctx->xt.dir = DMA_DEV_TO_MEM;
	ctx->xt.src_sgl = false;
	ctx->xt.dst_sgl = true;
	ctx->xt.dst_start = p_out;

	pix_mp = &dma->capfmt.fmt.pix_mp;
	info = dma->capinfo;
	xilinx_xdma_set_mode(dma->chan_rx, mode);
	xilinx_xdma_v4l2_config(dma->chan_rx, pix_mp->pixelformat);
	xvip_width_padding_factor(pix_mp->pixelformat, &padding_factor_nume,
				  &padding_factor_deno);
	xvip_bpl_scaling_factor(pix_mp->pixelformat, &bpl_nume, &bpl_deno);

	ctx->xt.frame_size = info->num_planes;
	ctx->sgl[0].size = (pix_mp->width * info->bpl_factor *
			    padding_factor_nume * bpl_nume) /
			    (padding_factor_deno * bpl_deno);
	ctx->sgl[0].icg = pix_mp->plane_fmt[0].bytesperline - ctx->sgl[0].size;
	ctx->xt.numf = pix_mp->height;

	/*
	 * dst_icg is the number of bytes to jump after last luma addr
	 * and before first chroma addr
	 */
	ctx->sgl[0].src_icg = 0;

	if (info->buffers == 1) {
		/* Handling contiguous data with mplanes */
		ctx->sgl[0].dst_icg = 0;
	} else {
		/* Handling non-contiguous data with mplanes */
		if (info->buffers == 2) {
			dma_addr_t chroma_cap =
			vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 1);
			luma_size = pix_mp->plane_fmt[0].bytesperline *
				    ctx->xt.numf;
			if (chroma_cap > p_out)
				ctx->sgl[0].dst_icg = chroma_cap - p_out -
						      luma_size;
			}
	}

	desc = dmaengine_prep_interleaved_dma(dma->chan_rx, &ctx->xt, flags);
	if (!desc) {
		dev_err(xdev->dev, "Failed to prepare DMA rx transfer\n");
		return;
	}

	desc->callback = xvip_m2m_dma_callback;
	desc->callback_param = ctx;
	dmaengine_submit(desc);
	dma_async_issue_pending(dma->chan_rx);
}

static void xvip_m2m_prep_submit_mem2dev_desc(struct xvip_m2m_ctx *ctx,
					      struct vb2_v4l2_buffer *src_buf)
{
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct xvip_m2m_dev *xdev = ctx->xdev;
	struct dma_async_tx_descriptor *desc;
	dma_addr_t p_in;
	const struct xvip_video_format *info;
	struct v4l2_pix_format_mplane *pix_mp;
	u32 padding_factor_nume, padding_factor_deno;
	u32 bpl_nume, bpl_deno;
	u32 luma_size;
	u32 flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
	enum operation_mode mode = DEFAULT;
	u32 bpl, src_width, src_height;

	p_in = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);

	if (!p_in) {
		dev_err(xdev->dev,
			"Acquiring kernel pointer to buffer failed\n");
		return;
	}

	ctx->xt.dir = DMA_MEM_TO_DEV;
	ctx->xt.src_sgl = true;
	ctx->xt.dst_sgl = false;
	ctx->xt.src_start = p_in;

	pix_mp = &dma->outfmt.fmt.pix_mp;
	bpl = pix_mp->plane_fmt[0].bytesperline;
	if (dma->crop) {
		src_width = dma->r.width;
		src_height = dma->r.height;
	} else {
		src_width = pix_mp->width;
		src_height = pix_mp->height;
	}

	info = dma->outinfo;
	xilinx_xdma_set_mode(dma->chan_tx, mode);
	xilinx_xdma_v4l2_config(dma->chan_tx, pix_mp->pixelformat);
	xvip_width_padding_factor(pix_mp->pixelformat, &padding_factor_nume,
				  &padding_factor_deno);
	xvip_bpl_scaling_factor(pix_mp->pixelformat, &bpl_nume, &bpl_deno);

	ctx->xt.frame_size = info->num_planes;
	ctx->sgl[0].size = (src_width * info->bpl_factor *
			    padding_factor_nume * bpl_nume) /
			    (padding_factor_deno * bpl_deno);
	ctx->sgl[0].icg = bpl - ctx->sgl[0].size;
	ctx->xt.numf = src_height;

	/*
	 * src_icg is the number of bytes to jump after last luma addr
	 * and before first chroma addr
	 */
	ctx->sgl[0].dst_icg = 0;

	if (info->buffers == 1) {
		/* Handling contiguous data with mplanes */
		ctx->sgl[0].src_icg = 0;
		if (dma->crop)
			ctx->sgl[0].src_icg = bpl *
					      (pix_mp->height - src_height);
	} else {
		/* Handling non-contiguous data with mplanes */
		if (info->buffers == 2) {
			dma_addr_t chroma_out =
			vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 1);
			luma_size = bpl * ctx->xt.numf;
			if (chroma_out > p_in)
				ctx->sgl[0].src_icg = chroma_out - p_in -
						      luma_size;
			}
	}

	desc = dmaengine_prep_interleaved_dma(dma->chan_tx, &ctx->xt, flags);
	if (!desc) {
		dev_err(xdev->dev, "Failed to prepare DMA tx transfer\n");
		return;
	}

	desc->callback = xvip_m2m_dma_callback_mem2dev;
	desc->callback_param = ctx;
	dmaengine_submit(desc);
	dma_async_issue_pending(dma->chan_tx);
}

/**
 * xvip_m2m_device_run - prepares and starts the device
 *
 * @priv: Instance private data
 *
 * This simulates all the immediate preparations required before starting
 * a device. This will be called by the framework when it decides to schedule
 * a particular instance.
 */
static void xvip_m2m_device_run(void *priv)
{
	struct xvip_m2m_ctx *ctx = priv;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Prepare and submit mem2dev transaction */
	xvip_m2m_prep_submit_mem2dev_desc(ctx, src_buf);

	/* Prepare and submit dev2mem transaction */
	xvip_m2m_prep_submit_dev2mem_desc(ctx, dst_buf);
}

static const struct v4l2_file_operations xvip_m2m_fops = {
	.owner		= THIS_MODULE,
	.open		= xvip_m2m_open,
	.release	= xvip_m2m_release,
	.poll		= xvip_m2m_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= xvip_m2m_mmap,
};

static struct video_device xvip_m2m_videodev = {
	.name		= XVIP_M2M_NAME,
	.fops		= &xvip_m2m_fops,
	.ioctl_ops	= &xvip_m2m_ioctl_ops,
	.release	= video_device_release_empty,
	.vfl_dir	= VFL_DIR_M2M,
	.device_caps	= V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING,
	.vfl_type	= VFL_TYPE_GRABBER,
};

static const struct v4l2_m2m_ops xvip_m2m_ops = {
	.device_run	= xvip_m2m_device_run,
	.job_ready	= xvip_m2m_job_ready,
	.job_abort	= xvip_m2m_job_abort,
};

static int xvip_m2m_dma_init(struct xvip_m2m_dma *dma)
{
	struct xvip_m2m_dev *xdev;
	struct v4l2_pix_format_mplane *pix_mp;
	int ret;

	xdev = dma->xdev;
	mutex_init(&xdev->lock);
	mutex_init(&dma->pipe.lock);
	spin_lock_init(&xdev->queued_lock);

	/* Format info on capture port - NV12 is the default format */
	dma->capinfo = xvip_get_format_by_fourcc(XVIP_M2M_DEFAULT_FMT);
	pix_mp = &dma->capfmt.fmt.pix_mp;
	pix_mp->pixelformat = dma->capinfo->fourcc;

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->width = XVIP_M2M_DEF_WIDTH;
	pix_mp->height = XVIP_M2M_DEF_HEIGHT;
	pix_mp->plane_fmt[0].bytesperline = pix_mp->width *
					    dma->capinfo->bpl_factor;
	pix_mp->plane_fmt[0].sizeimage =
			DIV_ROUND_UP(pix_mp->plane_fmt[0].bytesperline *
				     pix_mp->height * dma->capinfo->bpp, 8);

	/* Format info on output port - NV12 is the default format */
	dma->outinfo = xvip_get_format_by_fourcc(XVIP_M2M_DEFAULT_FMT);
	pix_mp = &dma->capfmt.fmt.pix_mp;
	pix_mp->pixelformat = dma->outinfo->fourcc;
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->width = XVIP_M2M_DEF_WIDTH;
	pix_mp->height = XVIP_M2M_DEF_HEIGHT;
	pix_mp->plane_fmt[0].bytesperline = pix_mp->width *
					    dma->outinfo->bpl_factor;
	pix_mp->plane_fmt[0].sizeimage =
			DIV_ROUND_UP(pix_mp->plane_fmt[0].bytesperline *
				     pix_mp->height * dma->outinfo->bpp, 8);

	/* DMA channels for mem2mem */
	dma->chan_tx = dma_request_chan(xdev->dev, "tx");
	if (IS_ERR(dma->chan_tx)) {
		ret = PTR_ERR(dma->chan_tx);
		if (ret != -EPROBE_DEFER)
			dev_err(xdev->dev, "mem2mem DMA tx channel not found");

		return ret;
	}

	dma->chan_rx = dma_request_chan(xdev->dev, "rx");
	if (IS_ERR(dma->chan_rx)) {
		ret = PTR_ERR(dma->chan_rx);
		if (ret != -EPROBE_DEFER)
			dev_err(xdev->dev, "mem2mem DMA rx channel not found");

		goto tx;
	}

	dma->align = BIT(dma->chan_tx->device->copy_align);

	/* Video node */
	dma->video = xvip_m2m_videodev;
	dma->video.v4l2_dev = &xdev->v4l2_dev;
	dma->video.lock = &xdev->lock;

	dma->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	dma->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&dma->video.entity, 2, dma->pads);
	if (ret < 0)
		goto error;

	ret = video_register_device(&dma->video, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(xdev->dev, "Failed to register mem2mem video device\n");
		goto tx_rx;
	}

	video_set_drvdata(&dma->video, dma->xdev);
	return 0;

tx_rx:
	dma_release_channel(dma->chan_rx);
tx:
	dma_release_channel(dma->chan_tx);
error:
	return ret;
}

static void xvip_m2m_dma_deinit(struct xvip_m2m_dma *dma)
{
	if (video_is_registered(&dma->video))
		video_unregister_device(&dma->video);

	mutex_destroy(&dma->pipe.lock);
	mutex_destroy(&dma->xdev->lock);
	dma_release_channel(dma->chan_tx);
	dma_release_channel(dma->chan_rx);
}

static int xvip_m2m_dma_alloc_init(struct xvip_m2m_dev *xdev)
{
	struct xvip_m2m_dma *dma = NULL;
	int ret;

	dma = devm_kzalloc(xdev->dev, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	dma->xdev = xdev;
	xdev->dma = dma;

	ret = xvip_m2m_dma_init(xdev->dma);
	if (ret) {
		dev_err(xdev->dev, "DMA initialization failed\n");
		return ret;
	}

	xdev->v4l2_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	return 0;
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */
static void xvip_composite_v4l2_cleanup(struct xvip_m2m_dev *xdev)
{
	v4l2_device_unregister(&xdev->v4l2_dev);
	media_device_unregister(&xdev->media_dev);
	media_device_cleanup(&xdev->media_dev);
}

static int xvip_composite_v4l2_init(struct xvip_m2m_dev *xdev)
{
	int ret;

	xdev->media_dev.dev = xdev->dev;
	strlcpy(xdev->media_dev.model, "Xilinx Videoi M2M Composite Device",
		sizeof(xdev->media_dev.model));
	xdev->media_dev.hw_revision = 0;

	media_device_init(&xdev->media_dev);

	xdev->v4l2_dev.mdev = &xdev->media_dev;
	ret = v4l2_device_register(xdev->dev, &xdev->v4l2_dev);
	if (ret < 0) {
		dev_err(xdev->dev, "V4L2 device registration failed (%d)\n",
			ret);
		media_device_cleanup(&xdev->media_dev);
		return ret;
	}

	return 0;
}

static struct xvip_graph_entity *
xvip_graph_find_entity(struct xvip_m2m_dev *xdev,
		       const struct device_node *node)
{
	struct xvip_graph_entity *entity;

	list_for_each_entry(entity, &xdev->entities, list) {
		if (entity->node == node)
			return entity;
	}

	return NULL;
}

static int xvip_graph_build_one(struct xvip_m2m_dev *xdev,
				struct xvip_graph_entity *entity)
{
	u32 link_flags = MEDIA_LNK_FL_ENABLED;
	struct media_entity *local = entity->entity;
	struct media_entity *remote;
	struct media_pad *local_pad;
	struct media_pad *remote_pad;
	struct xvip_graph_entity *ent;
	struct v4l2_fwnode_link link;
	struct device_node *ep = NULL;
	struct device_node *next;
	int ret = 0;

	dev_dbg(xdev->dev, "creating links for entity %s\n", local->name);

	while (1) {
		/* Get the next endpoint and parse its link. */
		next = of_graph_get_next_endpoint(entity->node, ep);
		if (!next)
			break;

		ep = next;

		dev_dbg(xdev->dev, "processing endpoint %pOF\n", ep);

		ret = v4l2_fwnode_parse_link(of_fwnode_handle(ep), &link);
		if (ret < 0) {
			dev_err(xdev->dev, "failed to parse link for %pOF\n",
				ep);
			continue;
		}

		/* Skip sink ports, they will be processed from the other end of
		 * the link.
		 */
		if (link.local_port >= local->num_pads) {
			dev_err(xdev->dev, "invalid port number %u for %pOF\n",
				link.local_port,
				to_of_node(link.local_node));
			v4l2_fwnode_put_link(&link);
			ret = -EINVAL;
			break;
		}

		local_pad = &local->pads[link.local_port];

		if (local_pad->flags & MEDIA_PAD_FL_SINK) {
			dev_dbg(xdev->dev, "skipping sink port %pOF:%u\n",
				to_of_node(link.local_node),
				link.local_port);
			v4l2_fwnode_put_link(&link);
			continue;
		}

		/* Skip DMA engines, they will be processed separately. */
		if (link.remote_node == of_fwnode_handle(xdev->dev->of_node)) {
			dev_dbg(xdev->dev, "skipping DMA port %pOF:%u\n",
				to_of_node(link.local_node),
				link.local_port);
			v4l2_fwnode_put_link(&link);
			continue;
		}

		/* Find the remote entity. */
		ent = xvip_graph_find_entity(xdev,
					     to_of_node(link.remote_node));
		if (!ent) {
			dev_err(xdev->dev, "no entity found for %pOF\n",
				to_of_node(link.remote_node));
			v4l2_fwnode_put_link(&link);
			ret = -ENODEV;
			break;
		}

		remote = ent->entity;

		if (link.remote_port >= remote->num_pads) {
			dev_err(xdev->dev, "invalid port number %u on %pOF\n",
				link.remote_port, to_of_node(link.remote_node));
			v4l2_fwnode_put_link(&link);
			ret = -EINVAL;
			break;
		}

		remote_pad = &remote->pads[link.remote_port];

		v4l2_fwnode_put_link(&link);

		/* Create the media link. */
		dev_dbg(xdev->dev, "creating %s:%u -> %s:%u link\n",
			local->name, local_pad->index,
			remote->name, remote_pad->index);

		ret = media_create_pad_link(local, local_pad->index,
					    remote, remote_pad->index,
					    link_flags);
		if (ret < 0) {
			dev_err(xdev->dev,
				"failed to create %s:%u -> %s:%u link\n",
				local->name, local_pad->index,
				remote->name, remote_pad->index);
			break;
		}
	}

	return ret;
}

static int xvip_graph_parse_one(struct xvip_m2m_dev *xdev,
				struct device_node *node)
{
	struct xvip_graph_entity *entity;
	struct device_node *remote;
	struct device_node *ep = NULL;
	int ret = 0;

	dev_dbg(xdev->dev, "parsing node %pOF\n", node);

	while (1) {
		ep = of_graph_get_next_endpoint(node, ep);
		if (!ep)
			break;

		dev_dbg(xdev->dev, "handling endpoint %pOF %s\n",
			ep, ep->name);

		remote = of_graph_get_remote_port_parent(ep);
		if (!remote) {
			ret = -EINVAL;
			break;
		}
		dev_dbg(xdev->dev, "Remote endpoint %pOF %s\n",
			remote, remote->name);

		/* Skip entities that we have already processed. */
		if (remote == xdev->dev->of_node ||
		    xvip_graph_find_entity(xdev, remote)) {
			of_node_put(remote);
			continue;
		}

		entity = devm_kzalloc(xdev->dev, sizeof(*entity), GFP_KERNEL);
		if (!entity) {
			of_node_put(remote);
			ret = -ENOMEM;
			break;
		}

		entity->node = remote;
		entity->asd.match_type = V4L2_ASYNC_MATCH_FWNODE;
		entity->asd.match.fwnode = of_fwnode_handle(remote);
		list_add_tail(&entity->list, &xdev->entities);
		xdev->num_subdevs++;
	}

	of_node_put(ep);
	return ret;
}

static int xvip_graph_parse(struct xvip_m2m_dev *xdev)
{
	struct xvip_graph_entity *entity;
	int ret;

	/*
	 * Walk the links to parse the full graph. Start by parsing the
	 * composite node and then parse entities in turn. The list_for_each
	 * loop will handle entities added at the end of the list while walking
	 * the links.
	 */
	ret = xvip_graph_parse_one(xdev, xdev->dev->of_node);
	if (ret < 0)
		return 0;

	list_for_each_entry(entity, &xdev->entities, list) {
		ret = xvip_graph_parse_one(xdev, entity->node);
		if (ret < 0)
			break;
	}

	return ret;
}

static int xvip_graph_build_dma(struct xvip_m2m_dev *xdev)
{
	u32 link_flags = MEDIA_LNK_FL_ENABLED;
	struct device_node *node = xdev->dev->of_node;
	struct media_entity *source;
	struct media_entity *sink;
	struct media_pad *source_pad;
	struct media_pad *sink_pad;
	struct xvip_graph_entity *ent;
	struct v4l2_fwnode_link link;
	struct device_node *ep = NULL;
	struct device_node *next;
	struct xvip_m2m_dma *dma = xdev->dma;
	int ret = 0;

	dev_dbg(xdev->dev, "creating links for DMA engines\n");

	while (1) {
		/* Get the next endpoint and parse its link. */
		next = of_graph_get_next_endpoint(node, ep);
		if (!next)
			break;

		ep = next;

		dev_dbg(xdev->dev, "processing endpoint %pOF\n", ep);

		ret = v4l2_fwnode_parse_link(of_fwnode_handle(ep), &link);
		if (ret < 0) {
			dev_err(xdev->dev, "failed to parse link for %pOF\n",
				ep);
			continue;
		}

		dev_dbg(xdev->dev, "creating link for DMA engine %s\n",
			dma->video.name);

		/* Find the remote entity. */
		ent = xvip_graph_find_entity(xdev,
					     to_of_node(link.remote_node));
		if (!ent) {
			dev_err(xdev->dev, "no entity found for %pOF\n",
				to_of_node(link.remote_node));
			v4l2_fwnode_put_link(&link);
			ret = -ENODEV;
			break;
		}
		if (link.remote_port >= ent->entity->num_pads) {
			dev_err(xdev->dev, "invalid port number %u on %pOF\n",
				link.remote_port,
				to_of_node(link.remote_node));
			v4l2_fwnode_put_link(&link);
			ret = -EINVAL;
			break;
		}

		dev_dbg(xdev->dev, "Entity %s %s\n", ent->node->name,
			ent->node->full_name);
		dev_dbg(xdev->dev, "port number %u on %pOF\n",
			link.remote_port, to_of_node(link.remote_node));
		dev_dbg(xdev->dev, "local port number %u on %pOF\n",
			link.local_port, to_of_node(link.local_node));

		if (link.local_port == XVIP_PAD_SOURCE) {
			source = &dma->video.entity;
			source_pad = &dma->pads[XVIP_PAD_SOURCE];
			sink = ent->entity;
			sink_pad = &sink->pads[XVIP_PAD_SINK];

		} else {
			source = ent->entity;
			source_pad = &source->pads[XVIP_PAD_SOURCE];
			sink = &dma->video.entity;
			sink_pad = &dma->pads[XVIP_PAD_SINK];
		}

		v4l2_fwnode_put_link(&link);

		/* Create the media link. */
		dev_dbg(xdev->dev, "creating %s:%u -> %s:%u link\n",
			source->name, source_pad->index,
			sink->name, sink_pad->index);

		ret = media_create_pad_link(source, source_pad->index,
					    sink, sink_pad->index,
					    link_flags);
		if (ret < 0) {
			dev_err(xdev->dev,
				"failed to create %s:%u -> %s:%u link\n",
				source->name, source_pad->index,
				sink->name, sink_pad->index);
			break;
		}
	}

	return ret;
}

static int xvip_graph_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct xvip_m2m_dev *xdev =
		container_of(notifier, struct xvip_m2m_dev, notifier);
	struct xvip_graph_entity *entity;
	int ret;

	dev_dbg(xdev->dev, "notify complete, all subdevs registered\n");

	/* Create links for every entity. */
	list_for_each_entry(entity, &xdev->entities, list) {
		ret = xvip_graph_build_one(xdev, entity);
		if (ret < 0)
			return ret;
	}

	/* Create links for DMA channels. */
	ret = xvip_graph_build_dma(xdev);
	if (ret < 0)
		return ret;

	ret = v4l2_device_register_subdev_nodes(&xdev->v4l2_dev);
	if (ret < 0)
		dev_err(xdev->dev, "failed to register subdev nodes\n");

	return media_device_register(&xdev->media_dev);
}

static int xvip_graph_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct xvip_m2m_dev *xdev =
		container_of(notifier, struct xvip_m2m_dev, notifier);
	struct xvip_graph_entity *entity;

	/* Locate the entity corresponding to the bound subdev and store the
	 * subdev pointer.
	 */
	list_for_each_entry(entity, &xdev->entities, list) {
		if (entity->node != subdev->dev->of_node)
			continue;

		if (entity->subdev) {
			dev_err(xdev->dev, "duplicate subdev for node %pOF\n",
				entity->node);
			return -EINVAL;
		}

		dev_dbg(xdev->dev, "subdev %s bound\n", subdev->name);
		entity->entity = &subdev->entity;
		entity->subdev = subdev;
		return 0;
	}

	dev_err(xdev->dev, "no entity for subdev %s\n", subdev->name);
	return -EINVAL;
}

static const struct v4l2_async_notifier_operations xvip_graph_notify_ops = {
	.bound = xvip_graph_notify_bound,
	.complete = xvip_graph_notify_complete,
};

static void xvip_graph_cleanup(struct xvip_m2m_dev *xdev)
{
	struct xvip_graph_entity *entityp;
	struct xvip_graph_entity *entity;

	v4l2_async_notifier_unregister(&xdev->notifier);

	list_for_each_entry_safe(entity, entityp, &xdev->entities, list) {
		of_node_put(entity->node);
		list_del(&entity->list);
	}
}

static int xvip_graph_init(struct xvip_m2m_dev *xdev)
{
	struct xvip_graph_entity *entity;
	struct v4l2_async_subdev **subdevs = NULL;
	unsigned int num_subdevs;
	unsigned int i;
	int ret;

	/* Init the DMA channels. */
	ret = xvip_m2m_dma_alloc_init(xdev);
	if (ret < 0) {
		dev_err(xdev->dev, "DMA initialization failed\n");
		goto done;
	}

	/* Parse the graph to extract a list of subdevice DT nodes. */
	ret = xvip_graph_parse(xdev);
	if (ret < 0) {
		dev_err(xdev->dev, "graph parsing failed\n");
		goto done;
	}
	dev_dbg(xdev->dev, "Number of subdev = %d\n", xdev->num_subdevs);

	if (!xdev->num_subdevs) {
		dev_err(xdev->dev, "no subdev found in graph\n");
		goto done;
	}

	/* Register the subdevices notifier. */
	num_subdevs = xdev->num_subdevs;
	subdevs = devm_kzalloc(xdev->dev, sizeof(*subdevs) * num_subdevs,
			       GFP_KERNEL);
	if (!subdevs) {
		ret = -ENOMEM;
		goto done;
	}

	i = 0;
	list_for_each_entry(entity, &xdev->entities, list)
		subdevs[i++] = &entity->asd;

	xdev->notifier.subdevs = subdevs;
	xdev->notifier.num_subdevs = num_subdevs;
	xdev->notifier.ops = &xvip_graph_notify_ops;

	ret = v4l2_async_notifier_register(&xdev->v4l2_dev, &xdev->notifier);
	if (ret < 0) {
		dev_err(xdev->dev, "notifier registration failed\n");
		goto done;
	}

	ret = 0;

done:
	if (ret < 0)
		xvip_graph_cleanup(xdev);

	return ret;
}

static int xvip_composite_remove(struct platform_device *pdev)
{
	struct xvip_m2m_dev *xdev = platform_get_drvdata(pdev);

	xvip_graph_cleanup(xdev);
	xvip_composite_v4l2_cleanup(xdev);

	return 0;
}

static int xvip_m2m_probe(struct platform_device *pdev)
{
	struct xvip_m2m_dev *xdev = NULL;
	int ret;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &pdev->dev;
	INIT_LIST_HEAD(&xdev->entities);

	ret = xvip_composite_v4l2_init(xdev);
	if (ret)
		return -EINVAL;

	ret = xvip_graph_init(xdev);
	if (ret < 0)
		goto error;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		goto dma_cleanup;
	}

	platform_set_drvdata(pdev, xdev);

	xdev->m2m_dev = v4l2_m2m_init(&xvip_m2m_ops);
	if (IS_ERR(xdev->m2m_dev)) {
		dev_err(xdev->dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(xdev->m2m_dev);
		goto dma_cleanup;
	}

	dev_info(xdev->dev, "mem2mem device registered\n");
	return 0;

dma_cleanup:
	xvip_m2m_dma_deinit(xdev->dma);

error:
	v4l2_device_unregister(&xdev->v4l2_dev);
	return ret;
}

static int xvip_m2m_remove(struct platform_device *pdev)
{
	xvip_composite_remove(pdev);
	return 0;
}

static const struct of_device_id xvip_m2m_of_id_table[] = {
	{ .compatible = "xlnx,mem2mem" },
	{ }
};
MODULE_DEVICE_TABLE(of, xvip_m2m_of_id_table);

static struct platform_driver xvip_m2m_driver = {
	.driver = {
		.name = XVIP_M2M_NAME,
		.of_match_table = xvip_m2m_of_id_table,
	},
	.probe = xvip_m2m_probe,
	.remove = xvip_m2m_remove,
};

module_platform_driver(xvip_m2m_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx V4L2 mem2mem driver");
MODULE_LICENSE("GPL v2");

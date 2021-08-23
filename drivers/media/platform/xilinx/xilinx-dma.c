// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Video DMA
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/dma/xilinx_dma.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/lcm.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "xilinx-dma.h"
#include "xilinx-vip.h"
#include "xilinx-vipp.h"

#define XVIP_DMA_DEF_FORMAT		V4L2_PIX_FMT_YUYV
#define XVIP_DMA_DEF_WIDTH		1920
#define XVIP_DMA_DEF_HEIGHT		1080
#define XVIP_DMA_DEF_WIDTH_ALIGN	2
/* Minimum and maximum widths are expressed in bytes */
#define XVIP_DMA_MIN_WIDTH		1U
#define XVIP_DMA_MAX_WIDTH		65535U
#define XVIP_DMA_MIN_HEIGHT		1U
#define XVIP_DMA_MAX_HEIGHT		8191U

/* -----------------------------------------------------------------------------
 * Helper functions
 */

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

static int xvip_dma_verify_format(struct xvip_dma *dma)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	int ret;

	subdev = xvip_dma_remote_subdev(&dma->pad, &fmt.pad);
	if (!subdev)
		return -EPIPE;

	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	if (dma->fmtinfo->code != fmt.format.code)
		return -EINVAL;

	/*
	 * Crop rectangle contains format resolution by default, and crop
	 * rectangle if s_selection is executed.
	 */
	if (dma->r.width != fmt.format.width ||
	    dma->r.height != fmt.format.height)
		return -EINVAL;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Pipeline Stream Management
 */

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
	struct xvip_composite_device *xdev;
	int ret = 0;

	mutex_lock(&pipe->lock);
	xdev = pipe->xdev;

	if (on) {
		if (pipe->stream_count == pipe->num_dmas - 1 || xdev->atomic_streamon) {
			ret = xvip_graph_pipeline_start_stop(xdev, pipe, true);
			if (ret < 0)
				goto done;
		}
		pipe->stream_count++;
	} else {
		if (--pipe->stream_count == 0)
			xvip_graph_pipeline_start_stop(xdev, pipe, false);
	}

done:
	mutex_unlock(&pipe->lock);
	return ret;
}

static int xvip_pipeline_validate(struct xvip_pipeline *pipe,
				  struct xvip_dma *start)
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
		struct xvip_dma *dma;

		if (entity->function != MEDIA_ENT_F_IO_V4L)
			continue;

		dma = to_xvip_dma(media_entity_to_video_device(entity));

		if (dma->pad.flags & MEDIA_PAD_FL_SINK)
			num_outputs++;
		else
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
				 struct xvip_dma *dma)
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

/* -----------------------------------------------------------------------------
 * videobuf2 queue operations
 */

/**
 * struct xvip_dma_buffer - Video DMA buffer
 * @buf: vb2 buffer base object
 * @queue: buffer list entry in the DMA engine queued buffers list
 * @dma: DMA channel that uses the buffer
 * @desc: Descriptor associated with this structure
 */
struct xvip_dma_buffer {
	struct vb2_v4l2_buffer buf;
	struct list_head queue;
	struct xvip_dma *dma;
	struct dma_async_tx_descriptor *desc;
};

#define to_xvip_dma_buffer(vb)	container_of(vb, struct xvip_dma_buffer, buf)

static void xvip_dma_complete(void *param)
{
	struct xvip_dma_buffer *buf = param;
	struct xvip_dma *dma = buf->dma;
	int i, sizeimage;
	u32 fid;
	int status;

	spin_lock(&dma->queued_lock);
	list_del(&buf->queue);
	spin_unlock(&dma->queued_lock);

	buf->buf.field = V4L2_FIELD_NONE;
	buf->buf.sequence = dma->sequence++;
	buf->buf.vb2_buf.timestamp = ktime_get_ns();

	status = xilinx_xdma_get_fid(dma->dma, buf->desc, &fid);
	if (!status) {
		if (((V4L2_TYPE_IS_MULTIPLANAR(dma->format.type)) &&
		     dma->format.fmt.pix_mp.field == V4L2_FIELD_ALTERNATE) ||
		     dma->format.fmt.pix.field == V4L2_FIELD_ALTERNATE) {
			/*
			 * fid = 1 is odd field i.e. V4L2_FIELD_TOP.
			 * fid = 0 is even field i.e. V4L2_FIELD_BOTTOM.
			 */
			buf->buf.field = fid ?
					 V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM;

			if (fid == dma->prev_fid)
				buf->buf.sequence = dma->sequence++;

			buf->buf.sequence >>= 1;
			dma->prev_fid = fid;
		}
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type)) {
		for (i = 0; i < dma->fmtinfo->buffers; i++) {
			sizeimage =
				dma->format.fmt.pix_mp.plane_fmt[i].sizeimage;
			vb2_set_plane_payload(&buf->buf.vb2_buf, i, sizeimage);
		}
	} else {
		sizeimage = dma->format.fmt.pix.sizeimage;
		vb2_set_plane_payload(&buf->buf.vb2_buf, 0, sizeimage);
	}

	vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_DONE);
}

static int
xvip_dma_queue_setup(struct vb2_queue *vq,
		     unsigned int *nbuffers, unsigned int *nplanes,
		     unsigned int sizes[], struct device *alloc_devs[])
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);
	unsigned int i;
	int sizeimage;

	/* Multi planar case: Make sure the image size is large enough */
	if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type)) {
		if (*nplanes) {
			if (*nplanes != dma->format.fmt.pix_mp.num_planes)
				return -EINVAL;

			for (i = 0; i < *nplanes; i++) {
				sizeimage =
				  dma->format.fmt.pix_mp.plane_fmt[i].sizeimage;
				if (sizes[i] < sizeimage)
					return -EINVAL;
			}
		} else {
			*nplanes = dma->fmtinfo->buffers;
			for (i = 0; i < dma->fmtinfo->buffers; i++) {
				sizeimage =
				  dma->format.fmt.pix_mp.plane_fmt[i].sizeimage;
				sizes[i] = sizeimage;
			}
		}
		return 0;
	}

	/* Single planar case: Make sure the image size is large enough */
	sizeimage = dma->format.fmt.pix.sizeimage;
	if (*nplanes == 1)
		return sizes[0] < sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = sizeimage;

	return 0;
}

static int xvip_dma_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct xvip_dma *dma = vb2_get_drv_priv(vb->vb2_queue);
	struct xvip_dma_buffer *buf = to_xvip_dma_buffer(vbuf);

	buf->dma = dma;

	return 0;
}

static void xvip_dma_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct xvip_dma *dma = vb2_get_drv_priv(vb->vb2_queue);
	struct xvip_dma_buffer *buf = to_xvip_dma_buffer(vbuf);
	struct dma_async_tx_descriptor *desc;
	dma_addr_t addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	u32 flags = 0;
	u32 luma_size;
	u32 padding_factor_nume, padding_factor_deno, bpl_nume, bpl_deno;
	u32 fid = ~0;
	u32 bpl;

	if (dma->queue.type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    dma->queue.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
		dma->xt.dir = DMA_DEV_TO_MEM;
		dma->xt.src_sgl = false;
		dma->xt.dst_sgl = true;
		dma->xt.dst_start = addr;
	} else if (dma->queue.type == V4L2_BUF_TYPE_VIDEO_OUTPUT ||
		   dma->queue.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
		dma->xt.dir = DMA_MEM_TO_DEV;
		dma->xt.src_sgl = true;
		dma->xt.dst_sgl = false;
		dma->xt.src_start = addr;
	}

	/*
	 * DMA IP supports only 2 planes, so one datachunk is sufficient
	 * to get start address of 2nd plane
	 */
	if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type)) {
		struct v4l2_pix_format_mplane *pix_mp;
		size_t size;

		pix_mp = &dma->format.fmt.pix_mp;
		bpl = pix_mp->plane_fmt[0].bytesperline;

		xilinx_xdma_v4l2_config(dma->dma, pix_mp->pixelformat);
		xvip_width_padding_factor(pix_mp->pixelformat,
					  &padding_factor_nume,
					  &padding_factor_deno);
		xvip_bpl_scaling_factor(pix_mp->pixelformat, &bpl_nume,
					&bpl_deno);
		dma->xt.frame_size = dma->fmtinfo->num_planes;

		size = ((size_t)dma->r.width * dma->fmtinfo->bpl_factor *
			padding_factor_nume * bpl_nume) /
			((size_t)padding_factor_deno * bpl_deno);
		dma->sgl[0].size = size;

		dma->sgl[0].icg = bpl - dma->sgl[0].size;
		dma->xt.numf = dma->r.height;

		/*
		 * dst_icg is the number of bytes to jump after last luma addr
		 * and before first chroma addr
		 */

		/* Handling contiguous data with mplanes */
		if (dma->fmtinfo->buffers == 1) {
			dma->sgl[0].dst_icg = (size_t)bpl *
					      (pix_mp->height - dma->r.height);
		} else {
			/* Handling non-contiguous data with mplanes */
			if (dma->fmtinfo->buffers == 2 || dma->fmtinfo->buffers == 3) {
				dma_addr_t chroma_addr =
					vb2_dma_contig_plane_dma_addr(vb, 1);
				luma_size = bpl * dma->xt.numf;
				if (chroma_addr > addr)
					dma->sgl[0].dst_icg = chroma_addr -
						addr - luma_size;
			}
			/* Handle the 3rd plane for Y_U_V8 */
			if (dma->fmtinfo->buffers == 3) {
				dma_addr_t chroma_addr =
					vb2_dma_contig_plane_dma_addr(vb, 1);
				dma_addr_t third_plane_addr =
					vb2_dma_contig_plane_dma_addr(vb, 2);
				u32 chroma_size = bpl * dma->xt.numf;

				if (third_plane_addr > chroma_addr)
					dma->sgl[0].dst_icg = third_plane_addr -
						chroma_addr - chroma_size;
			}
		}
	} else {
		struct v4l2_pix_format *pix;
		size_t size;
		size_t dst_icg;

		pix = &dma->format.fmt.pix;
		bpl = pix->bytesperline;
		xilinx_xdma_v4l2_config(dma->dma, pix->pixelformat);
		xvip_width_padding_factor(pix->pixelformat,
					  &padding_factor_nume,
					  &padding_factor_deno);
		xvip_bpl_scaling_factor(pix->pixelformat, &bpl_nume,
					&bpl_deno);
		dma->xt.frame_size = dma->fmtinfo->num_planes;
		size = ((size_t)dma->r.width * dma->fmtinfo->bpl_factor *
			padding_factor_nume * bpl_nume) /
			((size_t)padding_factor_deno * bpl_deno);
		dma->sgl[0].size = size;
		dma->sgl[0].icg = bpl - dma->sgl[0].size;
		dma->xt.numf = dma->r.height;
		dma->sgl[0].dst_icg = 0;
		dst_icg = (size_t)bpl * (pix->height - dma->r.height);
		dma->sgl[0].dst_icg = dst_icg;
	}

	desc = dmaengine_prep_interleaved_dma(dma->dma, &dma->xt, flags);
	if (!desc) {
		dev_err(dma->xdev->dev, "Failed to prepare DMA transfer\n");
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}
	desc->callback = xvip_dma_complete;
	desc->callback_param = buf;
	buf->desc = desc;

	if (buf->buf.field == V4L2_FIELD_TOP)
		fid = 1;
	else if (buf->buf.field == V4L2_FIELD_BOTTOM)
		fid = 0;
	else if (buf->buf.field == V4L2_FIELD_NONE)
		fid = 0;

	xilinx_xdma_set_fid(dma->dma, desc, fid);

	spin_lock_irq(&dma->queued_lock);
	list_add_tail(&buf->queue, &dma->queued_bufs);
	spin_unlock_irq(&dma->queued_lock);

	/*
	 * Low latency capture: Give descriptor callback at start of
	 * processing the descriptor
	 */
	if (dma->low_latency_cap)
		xilinx_xdma_set_earlycb(dma->dma, desc,
					EARLY_CALLBACK_START_DESC);
	dmaengine_submit(desc);

	if (vb2_is_streaming(&dma->queue))
		dma_async_issue_pending(dma->dma);
}

static int xvip_dma_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);
	struct xvip_dma_buffer *buf, *nbuf;
	struct xvip_pipeline *pipe;
	int ret;

	dma->sequence = 0;
	dma->prev_fid = ~0;

	/*
	 * Start streaming on the pipeline. No link touching an entity in the
	 * pipeline can be activated or deactivated once streaming is started.
	 *
	 * Use the pipeline object embedded in the first DMA object that starts
	 * streaming.
	 */
	mutex_lock(&dma->xdev->lock);
	pipe = dma->video.entity.pipe
	     ? to_xvip_pipeline(&dma->video.entity) : &dma->pipe;

	ret = media_pipeline_start(&dma->video.entity, &pipe->pipe);
	mutex_unlock(&dma->xdev->lock);
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

	/* Start the DMA engine. This must be done before starting the blocks
	 * in the pipeline to avoid DMA synchronization issues.
	 * We dont't want to start DMA in case of low latency capture mode,
	 * applications will start DMA using S_CTRL at later point of time.
	 */
	if (!dma->low_latency_cap) {
		dma_async_issue_pending(dma->dma);
	} else {
		/* For low latency capture, return the first buffer early
		 * so that consumer can initialize until we start DMA.
		 */
		buf = list_first_entry(&dma->queued_bufs,
				       struct xvip_dma_buffer, queue);
		xvip_dma_complete(buf);
		buf->desc->callback = NULL;
	}

	/* Start the pipeline. */
	ret = xvip_pipeline_set_stream(pipe, true);
	if (ret < 0)
		goto error_stop;

	return 0;

error_stop:
	media_pipeline_stop(&dma->video.entity);

error:
	dmaengine_terminate_all(dma->dma);
	/* Give back all queued buffers to videobuf2. */
	spin_lock_irq(&dma->queued_lock);
	list_for_each_entry_safe(buf, nbuf, &dma->queued_bufs, queue) {
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_QUEUED);
		list_del(&buf->queue);
	}
	spin_unlock_irq(&dma->queued_lock);

	return ret;
}

static void xvip_dma_stop_streaming(struct vb2_queue *vq)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);
	struct xvip_pipeline *pipe = to_xvip_pipeline(&dma->video.entity);
	struct xvip_dma_buffer *buf, *nbuf;

	/* Stop the pipeline. */
	xvip_pipeline_set_stream(pipe, false);

	/* Stop and reset the DMA engine. */
	dmaengine_terminate_all(dma->dma);

	/* Cleanup the pipeline and mark it as being stopped. */
	xvip_pipeline_cleanup(pipe);
	media_pipeline_stop(&dma->video.entity);

	/* Give back all queued buffers to videobuf2. */
	spin_lock_irq(&dma->queued_lock);
	list_for_each_entry_safe(buf, nbuf, &dma->queued_bufs, queue) {
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_ERROR);
		list_del(&buf->queue);
	}
	spin_unlock_irq(&dma->queued_lock);
}

static const struct vb2_ops xvip_dma_queue_qops = {
	.queue_setup = xvip_dma_queue_setup,
	.buf_prepare = xvip_dma_buffer_prepare,
	.buf_queue = xvip_dma_buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = xvip_dma_start_streaming,
	.stop_streaming = xvip_dma_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int
xvip_dma_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	cap->capabilities = dma->xdev->v4l2_caps | V4L2_CAP_STREAMING |
			    V4L2_CAP_DEVICE_CAPS;

	strscpy((char *)cap->driver, "xilinx-vipp", sizeof(cap->driver));
	strscpy((char *)cap->card, (char *)dma->video.name, sizeof(cap->card));
	snprintf((char *)cap->bus_info, sizeof(cap->bus_info),
		 "platform:%pOFn:%u", dma->xdev->dev->of_node, dma->port);

	return 0;
}

static int xvip_xdma_enum_fmt(struct xvip_dma *dma, struct v4l2_fmtdesc *f,
			      struct v4l2_subdev_format *v4l_fmt)
{
	const struct xvip_video_format *fmt;
	int ret;
	u32 i, fmt_cnt, *fmts;

	ret = xilinx_xdma_get_v4l2_vid_fmts(dma->dma, &fmt_cnt, &fmts);
	if (ret)
		return ret;

	/* Has media pad value changed? */
	if (v4l_fmt->format.code != dma->remote_subdev_med_bus ||
	    !dma->remote_subdev_med_bus) {
		/* Re-generate legal list of fourcc codes */
		dma->poss_v4l2_fmt_cnt = 0;
		dma->remote_subdev_med_bus = v4l_fmt->format.code;

		if (!dma->poss_v4l2_fmts) {
			dma->poss_v4l2_fmts =
				devm_kzalloc(&dma->video.dev,
					     sizeof(u32) * fmt_cnt,
					     GFP_KERNEL);
			if (!dma->poss_v4l2_fmts)
				return -ENOMEM;
		}

		for (i = 0; i < fmt_cnt; i++) {
			fmt = xvip_get_format_by_fourcc(fmts[i]);
			if (IS_ERR(fmt))
				return PTR_ERR(fmt);

			if (fmt->code != dma->remote_subdev_med_bus)
				continue;

			dma->poss_v4l2_fmts[dma->poss_v4l2_fmt_cnt++] =	fmts[i];
		}
	}

	/* Return err if index is greater than count of legal values */
	if (f->index >= dma->poss_v4l2_fmt_cnt)
		return -EINVAL;

	/* Else return pix format in table */
	fmt = xvip_get_format_by_fourcc(dma->poss_v4l2_fmts[f->index]);
	if (IS_ERR(fmt))
		return PTR_ERR(fmt);

	f->pixelformat = fmt->fourcc;

	return 0;
}

static int
xvip_dma_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	struct v4l2_subdev *subdev;

	if (i->index > 0)
		return -EINVAL;

	subdev = xvip_dma_remote_subdev(&dma->pad, NULL);
	if (!subdev)
		return -EPIPE;

	/*
	 * FIXME: right now only camera input type is handled.
	 * There should be mechanism to distinguish other types of
	 * input like V4L2_INPUT_TYPE_TUNER and V4L2_INPUT_TYPE_TOUCH.
	 */
	i->type = V4L2_INPUT_TYPE_CAMERA;
	strlcpy((char *)i->name, (char *)subdev->name, sizeof(i->name));

	return 0;
}

static int
xvip_dma_get_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int
xvip_dma_set_input(struct file *file, void *fh, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return 0;
}

/* FIXME: without this callback function, some applications are not configured
 * with correct formats, and it results in frames in wrong format. Whether this
 * callback needs to be required is not clearly defined, so it should be
 * clarified through the mailing list.
 */
static int
xvip_dma_enum_format(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_format v4l_fmt;
	const struct xvip_video_format *fmt;
	int err, ret;

	/* Establish media pad format */
	subdev = xvip_dma_remote_subdev(&dma->pad, &v4l_fmt.pad);
	if (!subdev)
		return -EPIPE;

	v4l_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &v4l_fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	/*
	 * In case of frmbuf DMA, this will invoke frambuf driver specific APIs
	 * to enumerate formats otherwise return the pix format corresponding
	 * to subdev's media bus format. This kind of separation would be
	 * helpful for clean up and upstreaming.
	 */
	err = xvip_xdma_enum_fmt(dma, f, &v4l_fmt);
	if (!err)
		return err;

	/*
	 * This logic will just return one pix format based on subdev's
	 * media bus format
	 */
	if (f->index > 0)
		return -EINVAL;

	fmt = xvip_get_format_by_code(v4l_fmt.format.code);
	if (IS_ERR(fmt))
		return PTR_ERR(fmt);

	f->pixelformat = fmt->fourcc;

	return 0;
}

static int
xvip_dma_get_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type))
		format->fmt.pix_mp = dma->format.fmt.pix_mp;
	else
		format->fmt.pix = dma->format.fmt.pix;

	return 0;
}

static void
__xvip_dma_try_format(struct xvip_dma *dma,
		      struct v4l2_format *format,
		      const struct xvip_video_format **fmtinfo)
{
	const struct xvip_video_format *info;
	unsigned int min_width;
	unsigned int max_width;
	unsigned int min_bpl;
	unsigned int max_bpl;
	unsigned int width;
	unsigned int bpl;
	unsigned int i, hsub, vsub, plane_width, plane_height;
	unsigned int fourcc;
	unsigned int padding_factor_nume, padding_factor_deno;
	unsigned int bpl_nume, bpl_deno;
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	int ret;

	subdev = xvip_dma_remote_subdev(&dma->pad, &fmt.pad);
	if (!subdev)
		return;

	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return;

	if (fmt.format.field == V4L2_FIELD_ALTERNATE) {
		if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type))
			dma->format.fmt.pix_mp.field = V4L2_FIELD_ALTERNATE;
		else
			dma->format.fmt.pix.field = V4L2_FIELD_ALTERNATE;
	} else {
		if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type))
			dma->format.fmt.pix_mp.field = V4L2_FIELD_NONE;
		else
			dma->format.fmt.pix.field = V4L2_FIELD_NONE;
	}

	/* Retrieve format information and select the default format if the
	 * requested format isn't supported.
	 */
	if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type))
		fourcc = format->fmt.pix_mp.pixelformat;
	else
		fourcc = format->fmt.pix.pixelformat;

	info = xvip_get_format_by_fourcc(fourcc);

	if (IS_ERR(info))
		info = xvip_get_format_by_fourcc(XVIP_DMA_DEF_FORMAT);

	xvip_width_padding_factor(info->fourcc, &padding_factor_nume,
				  &padding_factor_deno);
	xvip_bpl_scaling_factor(info->fourcc, &bpl_nume, &bpl_deno);

	/* The transfer alignment requirements are expressed in bytes. Compute
	 * the minimum and maximum values, clamp the requested width and convert
	 * it back to pixels.
	 */
	min_width = roundup(XVIP_DMA_MIN_WIDTH, dma->width_align);
	max_width = rounddown(XVIP_DMA_MAX_WIDTH, dma->width_align);

	if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type)) {
		struct v4l2_pix_format_mplane *pix_mp;
		struct v4l2_plane_pix_format *plane_fmt;

		pix_mp = &format->fmt.pix_mp;
		plane_fmt = pix_mp->plane_fmt;
		pix_mp->field = dma->format.fmt.pix_mp.field;
		width = rounddown(pix_mp->width * info->bpl_factor,
				  dma->width_align);
		pix_mp->width = clamp(width, min_width, max_width) /
				info->bpl_factor;
		pix_mp->height = clamp(pix_mp->height, XVIP_DMA_MIN_HEIGHT,
				       XVIP_DMA_MAX_HEIGHT);

		/*
		 * Clamp the requested bytes per line value. If the maximum
		 * bytes per line value is zero, the module doesn't support
		 * user configurable line sizes. Override the requested value
		 * with the minimum in that case.
		 */

		max_bpl = rounddown(XVIP_DMA_MAX_WIDTH, dma->align);

		/* Handling contiguous data with mplanes */
		if (info->buffers == 1) {
			min_bpl = (pix_mp->width * info->bpl_factor *
				   padding_factor_nume * bpl_nume) /
				   (padding_factor_deno * bpl_deno);
			min_bpl = roundup(min_bpl, dma->align);
			bpl = roundup(plane_fmt[0].bytesperline, dma->align);
			plane_fmt[0].bytesperline = clamp(bpl, min_bpl,
							  max_bpl);

			if (info->num_planes == 1) {
				/* Single plane formats */
				plane_fmt[0].sizeimage =
						plane_fmt[0].bytesperline *
						pix_mp->height;
			} else {
				/* Multi plane formats */
				plane_fmt[0].sizeimage =
					DIV_ROUND_UP(plane_fmt[0].bytesperline *
						     pix_mp->height *
						     info->bpp, 8);
			}
		} else {
			/* Handling non-contiguous data with mplanes */
			hsub = info->hsub;
			vsub = info->vsub;
			for (i = 0; i < info->num_planes; i++) {
				plane_width = pix_mp->width / (i ? hsub : 1);
				plane_height = pix_mp->height / (i ? vsub : 1);
				min_bpl = (plane_width * info->bpl_factor *
					   padding_factor_nume * bpl_nume) /
					   (padding_factor_deno * bpl_deno);
				min_bpl = roundup(min_bpl, dma->align);
				bpl = rounddown(plane_fmt[i].bytesperline,
						dma->align);
				plane_fmt[i].bytesperline =
						clamp(bpl, min_bpl, max_bpl);
				plane_fmt[i].sizeimage =
						plane_fmt[i].bytesperline *
						plane_height;
			}
		}
	} else {
		struct v4l2_pix_format *pix;

		pix = &format->fmt.pix;
		pix->field = dma->format.fmt.pix.field;
		width = rounddown(pix->width * info->bpl_factor,
				  dma->width_align);
		pix->width = clamp(width, min_width, max_width) /
			     info->bpl_factor;
		pix->height = clamp(pix->height, XVIP_DMA_MIN_HEIGHT,
				    XVIP_DMA_MAX_HEIGHT);

		min_bpl = (pix->width * info->bpl_factor *
			  padding_factor_nume * bpl_nume) /
			  (padding_factor_deno * bpl_deno);
		min_bpl = roundup(min_bpl, dma->align);
		max_bpl = rounddown(XVIP_DMA_MAX_WIDTH, dma->align);
		bpl = rounddown(pix->bytesperline, dma->align);
		pix->bytesperline = clamp(bpl, min_bpl, max_bpl);
		pix->sizeimage = pix->width * pix->height * info->bpp / 8;
	}

	if (fmtinfo)
		*fmtinfo = info;
}

static int
xvip_dma_try_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	__xvip_dma_try_format(dma, format, NULL);
	return 0;
}

static int
xvip_dma_set_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	const struct xvip_video_format *info;

	__xvip_dma_try_format(dma, format, &info);

	if (vb2_is_busy(&dma->queue))
		return -EBUSY;

	if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type)) {
		dma->format.fmt.pix_mp = format->fmt.pix_mp;

		/*
		 * Save format resolution in crop rectangle. This will be
		 * updated when s_slection is called.
		 */
		dma->r.width = format->fmt.pix_mp.width;
		dma->r.height = format->fmt.pix_mp.height;
	} else {
		dma->format.fmt.pix = format->fmt.pix;
		dma->r.width = format->fmt.pix.width;
		dma->r.height = format->fmt.pix.height;
	}

	dma->fmtinfo = info;

	return 0;
}

static int
xvip_dma_g_selection(struct file *file, void *fh, struct v4l2_selection *sel)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	u32 width, height;
	bool crop_frame = false;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE:
		if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;

		crop_frame = true;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_CROP:
		if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;

		crop_frame = true;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	sel->r.left = 0;
	sel->r.top = 0;

	if (crop_frame) {
		sel->r.width = dma->r.width;
		sel->r.height = dma->r.height;
	} else {
		if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type)) {
			width = dma->format.fmt.pix_mp.width;
			height = dma->format.fmt.pix_mp.height;
		} else {
			width = dma->format.fmt.pix.width;
			height = dma->format.fmt.pix.height;
		}

		sel->r.width = width;
		sel->r.height = height;
	}

	return 0;
}

static int
xvip_dma_s_selection(struct file *file, void *fh, struct v4l2_selection *sel)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	u32 width, height;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE:
		/* COMPOSE target is only valid for capture buftype */
		if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_CROP:
		/* CROP target is only valid for output buftype */
		if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(dma->format.type)) {
		width = dma->format.fmt.pix_mp.width;
		height = dma->format.fmt.pix_mp.height;
	} else {
		width = dma->format.fmt.pix.width;
		height = dma->format.fmt.pix.height;
	}

	if (sel->r.width > width || sel->r.height > height ||
	    sel->r.top != 0 || sel->r.left != 0)
		return -EINVAL;

	sel->r.width = rounddown(max(XVIP_DMA_MIN_WIDTH, sel->r.width),
				 dma->width_align);
	sel->r.height = max(XVIP_DMA_MIN_HEIGHT, sel->r.height);
	dma->r.width = sel->r.width;
	dma->r.height = sel->r.height;

	return 0;
}

static const struct v4l2_ioctl_ops xvip_dma_ioctl_ops = {
	.vidioc_querycap		= xvip_dma_querycap,
	.vidioc_enum_fmt_vid_cap	= xvip_dma_enum_format,
	.vidioc_enum_fmt_vid_out	= xvip_dma_enum_format,
	.vidioc_g_fmt_vid_cap		= xvip_dma_get_format,
	.vidioc_g_fmt_vid_cap_mplane	= xvip_dma_get_format,
	.vidioc_g_fmt_vid_out		= xvip_dma_get_format,
	.vidioc_g_fmt_vid_out_mplane	= xvip_dma_get_format,
	.vidioc_s_fmt_vid_cap		= xvip_dma_set_format,
	.vidioc_s_fmt_vid_cap_mplane	= xvip_dma_set_format,
	.vidioc_s_fmt_vid_out		= xvip_dma_set_format,
	.vidioc_s_fmt_vid_out_mplane	= xvip_dma_set_format,
	.vidioc_try_fmt_vid_cap		= xvip_dma_try_format,
	.vidioc_try_fmt_vid_cap_mplane	= xvip_dma_try_format,
	.vidioc_try_fmt_vid_out		= xvip_dma_try_format,
	.vidioc_try_fmt_vid_out_mplane	= xvip_dma_try_format,
	.vidioc_s_selection		= xvip_dma_s_selection,
	.vidioc_g_selection		= xvip_dma_g_selection,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_enum_input	= &xvip_dma_enum_input,
	.vidioc_g_input		= &xvip_dma_get_input,
	.vidioc_s_input		= &xvip_dma_set_input,
};

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static int xvip_dma_s_ctrl(struct v4l2_ctrl *ctl)
{
	struct xvip_dma *dma = container_of(ctl->handler, struct xvip_dma,
					    ctrl_handler);
	int ret = 0;

	switch (ctl->id)  {
	case V4L2_CID_XILINX_LOW_LATENCY:
		if (ctl->val == XVIP_LOW_LATENCY_ENABLE) {
			if (vb2_is_busy(&dma->queue))
				return -EBUSY;

			dma->low_latency_cap = true;
			/*
			 * Don't use auto-restart for low latency
			 * to avoid extra one frame delay between
			 * programming and actual writing of data
			 */
			xilinx_xdma_set_mode(dma->dma, DEFAULT);
		} else if (ctl->val == XVIP_LOW_LATENCY_DISABLE) {
			if (vb2_is_busy(&dma->queue))
				return -EBUSY;

			dma->low_latency_cap = false;
			xilinx_xdma_set_mode(dma->dma, AUTO_RESTART);
		} else if (ctl->val == XVIP_START_DMA) {
			/*
			 * In low latency capture, the driver allows application
			 * to start dma when queue has buffers. That's why we
			 * don't check for vb2_is_busy().
			 */
			if (dma->low_latency_cap &&
			    vb2_is_streaming(&dma->queue))
				dma_async_issue_pending(dma->dma);
			else
				ret = -EINVAL;
		} else {
			ret = -EINVAL;
		}

		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int xvip_dma_open(struct file *file)
{
	int ret;

	ret = v4l2_fh_open(file);
	if (ret)
		return ret;

	/* Disable the low latency mode as default */
	if (v4l2_fh_is_singular_file(file)) {
		struct xvip_dma *dma = video_drvdata(file);

		mutex_lock(&dma->lock);
		dma->low_latency_cap = false;
		xilinx_xdma_set_mode(dma->dma, AUTO_RESTART);
		mutex_unlock(&dma->lock);
	}

	return 0;
}

static const struct v4l2_ctrl_ops xvip_dma_ctrl_ops = {
	.s_ctrl = xvip_dma_s_ctrl,
};

static const struct v4l2_ctrl_config xvip_dma_ctrls[] = {
	{
		.ops = &xvip_dma_ctrl_ops,
		.id = V4L2_CID_XILINX_LOW_LATENCY,
		.name = "Low Latency Controls",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XVIP_LOW_LATENCY_ENABLE,
		.max = XVIP_START_DMA,
		.step = 1,
		.def = XVIP_LOW_LATENCY_DISABLE,
	}
};

/* -----------------------------------------------------------------------------
 * V4L2 file operations
 */

static const struct v4l2_file_operations xvip_dma_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= xvip_dma_open,
	.release	= vb2_fop_release,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Xilinx Video DMA Core
 */

int xvip_dma_init(struct xvip_composite_device *xdev, struct xvip_dma *dma,
		  enum v4l2_buf_type type, unsigned int port)
{
	char name[16];
	int ret;
	u32 i, hsub, vsub, width, height;

	dma->xdev = xdev;
	dma->port = port;
	mutex_init(&dma->lock);
	mutex_init(&dma->pipe.lock);
	INIT_LIST_HEAD(&dma->queued_bufs);
	spin_lock_init(&dma->queued_lock);

	dma->fmtinfo = xvip_get_format_by_fourcc(XVIP_DMA_DEF_FORMAT);
	dma->format.type = type;

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		struct v4l2_pix_format_mplane *pix_mp;

		pix_mp = &dma->format.fmt.pix_mp;
		pix_mp->pixelformat = dma->fmtinfo->fourcc;
		pix_mp->colorspace = V4L2_COLORSPACE_SRGB;
		pix_mp->field = V4L2_FIELD_NONE;
		pix_mp->width = XVIP_DMA_DEF_WIDTH;

		/* Handling contiguous data with mplanes */
		if (dma->fmtinfo->buffers == 1) {
			pix_mp->plane_fmt[0].bytesperline =
				pix_mp->width * dma->fmtinfo->bpl_factor;
			pix_mp->plane_fmt[0].sizeimage =
					pix_mp->width * pix_mp->height *
					dma->fmtinfo->bpp / 8;
		} else {
		    /* Handling non-contiguous data with mplanes */
			hsub = dma->fmtinfo->hsub;
			vsub = dma->fmtinfo->vsub;
			for (i = 0; i < dma->fmtinfo->buffers; i++) {
				width = pix_mp->width / (i ? hsub : 1);
				height = pix_mp->height / (i ? vsub : 1);
				pix_mp->plane_fmt[i].bytesperline =
					width *	dma->fmtinfo->bpl_factor;
				pix_mp->plane_fmt[i].sizeimage = width * height;
			}
		}
	} else {
		struct v4l2_pix_format *pix;

		pix = &dma->format.fmt.pix;
		pix->pixelformat = dma->fmtinfo->fourcc;
		pix->colorspace = V4L2_COLORSPACE_SRGB;
		pix->field = V4L2_FIELD_NONE;
		pix->width = XVIP_DMA_DEF_WIDTH;
		pix->height = XVIP_DMA_DEF_HEIGHT;
		pix->bytesperline = pix->width * dma->fmtinfo->bpl_factor;
		pix->sizeimage =
			pix->width * pix->height * dma->fmtinfo->bpp / 8;
	}

	/* Initialize the media entity... */
	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		dma->pad.flags = MEDIA_PAD_FL_SINK;
	else
		dma->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&dma->video.entity, 1, &dma->pad);
	if (ret < 0)
		goto error;

	ret = v4l2_ctrl_handler_init(&dma->ctrl_handler,
				     ARRAY_SIZE(xvip_dma_ctrls));
	if (ret < 0) {
		dev_err(dma->xdev->dev, "failed to initialize V4L2 ctrl\n");
		goto error;
	}

	for (i = 0; i < ARRAY_SIZE(xvip_dma_ctrls); i++) {
		struct v4l2_ctrl *ctrl;

		dev_dbg(dma->xdev->dev, "%d ctrl = 0x%x\n", i,
			xvip_dma_ctrls[i].id);
		ctrl = v4l2_ctrl_new_custom(&dma->ctrl_handler,
					    &xvip_dma_ctrls[i], NULL);
		if (!ctrl) {
			dev_err(dma->xdev->dev, "Failed for %s ctrl\n",
				xvip_dma_ctrls[i].name);
			goto error;
		}
	}

	if (dma->ctrl_handler.error) {
		dev_err(dma->xdev->dev, "failed to add controls\n");
		ret = dma->ctrl_handler.error;
		goto error;
	}

	ret = v4l2_ctrl_handler_setup(&dma->ctrl_handler);
	if (ret < 0) {
		dev_err(dma->xdev->dev, "failed to set controls\n");
		goto error;
	}

	/* ... and the video node... */
	dma->video.fops = &xvip_dma_fops;
	dma->video.v4l2_dev = &xdev->v4l2_dev;
	dma->video.v4l2_dev->ctrl_handler = &dma->ctrl_handler;
	dma->video.queue = &dma->queue;
	snprintf(dma->video.name, sizeof(dma->video.name), "%pOFn %s %u",
		 xdev->dev->of_node,
		 (type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
		  type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
					? "output" : "input",
		 port);

	dma->video.vfl_type = VFL_TYPE_VIDEO;
	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		dma->video.vfl_dir = VFL_DIR_RX;
	else
		dma->video.vfl_dir = VFL_DIR_TX;

	dma->video.release = video_device_release_empty;
	dma->video.ioctl_ops = &xvip_dma_ioctl_ops;
	dma->video.lock = &dma->lock;
	dma->video.device_caps = V4L2_CAP_STREAMING;
	switch (dma->format.type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		dma->video.device_caps |= V4L2_CAP_VIDEO_CAPTURE_MPLANE;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		dma->video.device_caps |= V4L2_CAP_VIDEO_CAPTURE;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		dma->video.device_caps |= V4L2_CAP_VIDEO_OUTPUT_MPLANE;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		dma->video.device_caps |= V4L2_CAP_VIDEO_OUTPUT;
		break;
	}

	video_set_drvdata(&dma->video, dma);

	/* ... and the buffers queue... */
	/* Don't enable VB2_READ and VB2_WRITE, as using the read() and write()
	 * V4L2 APIs would be inefficient. Testing on the command line with a
	 * 'cat /dev/video?' thus won't be possible, but given that the driver
	 * anyway requires a test tool to setup the pipeline before any video
	 * stream can be started, requiring a specific V4L2 test tool as well
	 * instead of 'cat' isn't really a drawback.
	 */
	dma->queue.type = type;
	dma->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dma->queue.lock = &dma->lock;
	dma->queue.drv_priv = dma;
	dma->queue.buf_struct_size = sizeof(struct xvip_dma_buffer);
	dma->queue.ops = &xvip_dma_queue_qops;
	dma->queue.mem_ops = &vb2_dma_contig_memops;
	dma->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
				   | V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
	dma->queue.dev = dma->xdev->dev;
	ret = vb2_queue_init(&dma->queue);
	if (ret < 0) {
		dev_err(dma->xdev->dev, "failed to initialize VB2 queue\n");
		goto error;
	}

	/* ... and the DMA channel. */
	snprintf(name, sizeof(name), "port%u", port);
	dma->dma = dma_request_chan(dma->xdev->dev, name);
	if (IS_ERR(dma->dma)) {
		ret = PTR_ERR(dma->dma);
		if (ret != -EPROBE_DEFER)
			dev_err(dma->xdev->dev, "no VDMA channel found\n");
		goto error;
	}

	xilinx_xdma_get_width_align(dma->dma, &dma->width_align);
	if (!dma->width_align) {
		dev_dbg(dma->xdev->dev,
			"Using width align %d\n", XVIP_DMA_DEF_WIDTH_ALIGN);
		dma->width_align = XVIP_DMA_DEF_WIDTH_ALIGN;
	}

	dma->align = 1 << dma->dma->device->copy_align;

	ret = video_register_device(&dma->video, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(dma->xdev->dev, "failed to register video device\n");
		goto error;
	}

	return 0;

error:
	xvip_dma_cleanup(dma);
	return ret;
}

void xvip_dma_cleanup(struct xvip_dma *dma)
{
	if (video_is_registered(&dma->video))
		video_unregister_device(&dma->video);

	if (!IS_ERR_OR_NULL(dma->dma))
		dma_release_channel(dma->dma);

	v4l2_ctrl_handler_free(&dma->ctrl_handler);
	media_entity_cleanup(&dma->video.entity);

	mutex_destroy(&dma->lock);
	mutex_destroy(&dma->pipe.lock);
}

//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx V4L2 mem2mem driver
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 * Author: Satish Kumar Nagireddy <satish.nagireddy.nagireddy@xilinx.com>
 */

#include <drm/drm_fourcc.h>
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
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "xilinx-vip.h"

#define XVIP_M2M_NAME		"xilinx-mem2mem"
#define XVIP_M2M_DEFAULT_FMT	V4L2_PIX_FMT_NV12

/* Minimum and maximum widths are expressed in bytes */
#define XVIP_M2M_MIN_WIDTH	1U
#define XVIP_M2M_MAX_WIDTH	65535U
#define XVIP_M2M_MIN_HEIGHT	1U
#define XVIP_M2M_MAX_HEIGHT	8191U

#define XVIP_M2M_DEF_WIDTH	1920
#define XVIP_M2M_DEF_HEIGHT	1080

/**
 * struct xvip_m2m_dev - Xilinx Video mem2mem device structure
 * @v4l2_dev: V4L2 device
 * @dev: (OF) device
 * @lock: This is to protect mem2mem context structure data
 * @queued_lock: This is to protect video buffer information
 * @dma: Video DMA channels
 * @m2m_dev: V4L2 mem2mem device structure
 * @v4l2_caps: V4L2 capabilities of the whole device
 */
struct xvip_m2m_dev {
	struct v4l2_device v4l2_dev;
	struct device *dev;

	/* Protects to m2m context data */
	struct mutex lock;

	/* Protects vb2_v4l2_buffer data */
	spinlock_t queued_lock;
	struct xvip_m2m_dma *dma;
	struct v4l2_m2m_dev *m2m_dev;
	u32 v4l2_caps;
};

/**
 * struct xvip_m2m_dma - Video DMA channel
 * @video: V4L2 video device associated with the DMA channel
 * @xdev: composite mem2mem device the DMA channels belongs to
 * @chan_tx: DMA engine channel for MEM2DEV transfer
 * @chan_rx: DMA engine channel for DEV2MEM transfer
 * @outfmt: active V4L2 OUTPUT port pixel format
 * @capfmt: active V4L2 CAPTURE port pixel format
 * @outinfo: format information corresponding to the active @outfmt
 * @capinfo: format information corresponding to the active @capfmt
 * @align: transfer alignment required by the DMA channel (in bytes)
 */
struct xvip_m2m_dma {
	struct video_device video;
	struct xvip_m2m_dev *xdev;
	struct dma_chan *chan_tx;
	struct dma_chan *chan_rx;
	struct v4l2_format outfmt;
	struct v4l2_format capfmt;
	const struct xvip_video_format *outinfo;
	const struct xvip_video_format *capinfo;
	u32 align;
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

static int xvip_m2m_start_streaming(struct vb2_queue *q, unsigned int count)
{
	return 0;
}

static void xvip_m2m_stop_streaming(struct vb2_queue *q)
{
	struct xvip_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct xvip_m2m_dma *dma = ctx->xdev->dma;
	struct vb2_v4l2_buffer *vbuf;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		dmaengine_terminate_sync(dma->chan_tx);
	else
		dmaengine_terminate_sync(dma->chan_rx);

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
	struct v4l2_format format = dma->capfmt;
	const struct xvip_video_format *fmtinfo;

	/* This logic will just return one pix format */
	if (f->index > 0)
		return -EINVAL;

	fmtinfo = xvip_get_format_by_fourcc(format.fmt.pix_mp.pixelformat);
	f->pixelformat = fmtinfo->fourcc;

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

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	    f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

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

static const struct v4l2_ioctl_ops xvip_m2m_ioctl_ops = {
	.vidioc_querycap		= xvip_dma_querycap,

	.vidioc_enum_fmt_vid_cap	= xvip_m2m_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= xvip_m2m_get_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= xvip_m2m_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= xvip_m2m_set_fmt,

	.vidioc_enum_fmt_vid_out	= xvip_m2m_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane	= xvip_m2m_get_fmt,
	.vidioc_try_fmt_vid_out_mplane	= xvip_m2m_try_fmt,
	.vidioc_s_fmt_vid_out_mplane	= xvip_m2m_set_fmt,

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
	info = dma->outinfo;
	xilinx_xdma_set_mode(dma->chan_tx, mode);
	xilinx_xdma_v4l2_config(dma->chan_tx, pix_mp->pixelformat);
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
	 * src_icg is the number of bytes to jump after last luma addr
	 * and before first chroma addr
	 */
	ctx->sgl[0].dst_icg = 0;

	if (info->buffers == 1) {
		/* Handling contiguous data with mplanes */
		ctx->sgl[0].src_icg = 0;
	} else {
		/* Handling non-contiguous data with mplanes */
		if (info->buffers == 2) {
			dma_addr_t chroma_out =
			vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 1);
			luma_size = pix_mp->plane_fmt[0].bytesperline *
				    ctx->xt.numf;
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
	.vfl_type	= VFL_TYPE_VIDEO,
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

	ret = video_register_device(&dma->video, VFL_TYPE_VIDEO, -1);
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

	return ret;
}

static void xvip_m2m_dma_deinit(struct xvip_m2m_dma *dma)
{
	if (video_is_registered(&dma->video))
		video_unregister_device(&dma->video);

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

static int xvip_m2m_probe(struct platform_device *pdev)
{
	struct xvip_m2m_dev *xdev = NULL;
	int ret;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &pdev->dev;

	ret = v4l2_device_register(&pdev->dev, &xdev->v4l2_dev);
	if (ret)
		return -EINVAL;

	ret = xvip_m2m_dma_alloc_init(xdev);
	if (ret)
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
	struct xvip_m2m_dev *xdev = platform_get_drvdata(pdev);

	v4l2_device_unregister(&xdev->v4l2_dev);
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

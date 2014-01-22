/*
 * Xilinx Video DMA
 *
 * Copyright (C) 2013 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/amba/xilinx_dma.h>
#include <linux/dmaengine.h>
#include <linux/lcm.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "xilinx-dma.h"
#include "xilinx-vip.h"
#include "xilinx-vipp.h"

#define XVIP_DMA_DEF_FORMAT		V4L2_PIX_FMT_YUYV
#define XVIP_DMA_DEF_WIDTH		1920
#define XVIP_DMA_DEF_HEIGHT		1080

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
	if (remote == NULL ||
	    media_entity_type(remote->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
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
	if (subdev == NULL)
		return -EINVAL;

	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	if (dma->fmtinfo->code != fmt.format.code ||
	    dma->format.height != fmt.format.height ||
	    dma->format.width != fmt.format.width)
		return -EINVAL;

	return 0;
}

/* -----------------------------------------------------------------------------
 * videobuf2 queue operations
 */

/**
 * struct xvip_dma_buffer - Video DMA buffer
 * @buf: vb2 buffer base object
 * @dma: DMA channel that uses the buffer
 * @addr: DMA bus address for the buffer memory
 * @length: total length of the buffer in bytes
 * @bytesused: number of bytes used in the buffer
 */
struct xvip_dma_buffer {
	struct vb2_buffer buf;

	struct xvip_dma *dma;

	dma_addr_t addr;
	unsigned int length;
	unsigned int bytesused;
};

#define to_xvip_dma_buffer(vb)	container_of(vb, struct xvip_dma_buffer, buf)

static void xvip_dma_complete(void *param)
{
	struct xvip_dma_buffer *buf = param;
	struct xvip_dma *dma = buf->dma;

	buf->buf.v4l2_buf.sequence = dma->sequence++;
	v4l2_get_timestamp(&buf->buf.v4l2_buf.timestamp);
	vb2_set_plane_payload(&buf->buf, 0, buf->length);
	vb2_buffer_done(&buf->buf, VB2_BUF_STATE_DONE);
}

static int
xvip_dma_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
		     unsigned int *nbuffers, unsigned int *nplanes,
		     unsigned int sizes[], void *alloc_ctxs[])
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);

	*nplanes = 1;

	sizes[0] = dma->format.sizeimage;
	alloc_ctxs[0] = dma->alloc_ctx;

	return 0;
}

static int xvip_dma_buffer_prepare(struct vb2_buffer *vb)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vb->vb2_queue);
	struct xvip_dma_buffer *buf = to_xvip_dma_buffer(vb);

	buf->dma = dma;
	buf->addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	buf->length = vb2_plane_size(vb, 0);
	buf->bytesused = 0;

	return 0;
}

static void xvip_dma_buffer_queue(struct vb2_buffer *vb)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vb->vb2_queue);
	struct xvip_dma_buffer *buf = to_xvip_dma_buffer(vb);
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction dir;
	u32 flags;

	if (dma->queue.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
		dir = DMA_DEV_TO_MEM;
	} else {
		flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
		dir = DMA_MEM_TO_DEV;
	}

	desc = dmaengine_prep_slave_single(dma->dma, buf->addr, buf->length,
					   dir, flags);
	desc->callback = xvip_dma_complete;
	desc->callback_param = buf;

	dmaengine_submit(desc);

	if (vb2_is_streaming(&dma->queue))
		dma_async_issue_pending(dma->dma);
}

static void xvip_dma_wait_prepare(struct vb2_queue *vq)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);

	mutex_unlock(&dma->lock);
}

static void xvip_dma_wait_finish(struct vb2_queue *vq)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);

	mutex_lock(&dma->lock);
}

static int xvip_dma_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);
	int ret;

	dma->sequence = 0;

	/* Mark the pipeline as streaming. */
	ret = media_entity_pipeline_start(&dma->video.entity,
					  &dma->xvipp->pipe);
	if (ret < 0)
		return ret;

	/* Verify that the configured format matches the output of the
	 * connected subdev.
	 */
	ret = xvip_dma_verify_format(dma);
	if (ret < 0) {
		media_entity_pipeline_stop(&dma->video.entity);
		return ret;
	}

	/* Start the DMA engine. This must be done before starting the blocks
	 * in the pipeline to avoid DMA synchronization issues.
	 */
	dma_async_issue_pending(dma->dma);

	/* Start the pipeline. */
	xvip_pipeline_set_stream(dma->xvipp, true);

	return 0;
}

static int xvip_dma_stop_streaming(struct vb2_queue *vq)
{
	struct xvip_dma *dma = vb2_get_drv_priv(vq);
	struct xilinx_vdma_config config;

	/* Stop the pipeline. */
	xvip_pipeline_set_stream(dma->xvipp, false);

	/* Stop and reset the DMA engine. */
	dmaengine_device_control(dma->dma, DMA_TERMINATE_ALL, 0);

	config.reset = 1;

	dmaengine_device_control(dma->dma, DMA_SLAVE_CONFIG,
				 (unsigned long)&config);

	/* Mark the pipeline as being stopped. */
	media_entity_pipeline_stop(&dma->video.entity);

	return 0;
}

static struct vb2_ops xvip_dma_queue_qops = {
	.queue_setup = xvip_dma_queue_setup,
	.buf_prepare = xvip_dma_buffer_prepare,
	.buf_queue = xvip_dma_buffer_queue,
	.wait_prepare = xvip_dma_wait_prepare,
	.wait_finish = xvip_dma_wait_finish,
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

	if (dma->queue.type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	else
		cap->capabilities = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING;

	strlcpy(cap->driver, "xilinx-vipp", sizeof(cap->driver));
	strlcpy(cap->card, dma->video.name, sizeof(cap->card));
	strlcpy(cap->bus_info, "media", sizeof(cap->bus_info));

	return 0;
}

static int
xvip_dma_get_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	mutex_lock(&dma->lock);
	format->fmt.pix = dma->format;
	mutex_unlock(&dma->lock);

	return 0;
}

static void
__xvip_dma_try_format(struct xvip_dma *dma, struct v4l2_pix_format *pix,
		      const struct xvip_video_format **fmtinfo)
{
	const struct xvip_video_format *info;
	unsigned int min_width;
	unsigned int max_width;
	unsigned int min_bpl;
	unsigned int max_bpl;
	unsigned int width;
	unsigned int align;
	unsigned int bpl;

	/* Retrieve format information and select the default format if the
	 * requested format isn't supported.
	 */
	info = xvip_get_format_by_fourcc(pix->pixelformat);
	if (info == NULL)
		info = xvip_get_format_by_fourcc(XVIP_DMA_DEF_FORMAT);

	pix->pixelformat = info->fourcc;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->field = V4L2_FIELD_NONE;

	/* The transfer alignment requirements are expressed in bytes. Compute
	 * the minimum and maximum values, clamp the requested width and convert
	 * it back to pixels.
	 */
	align = lcm(dma->align, info->bpp);
	min_width = roundup(XVIP_DMA_MIN_WIDTH, align);
	max_width = rounddown(XVIP_DMA_MAX_WIDTH, align);
	width = rounddown(pix->width * info->bpp, align);

	pix->width = clamp(width, min_width, max_width) / info->bpp;
	pix->height = clamp(pix->height, XVIP_DMA_MIN_HEIGHT,
			    XVIP_DMA_MAX_HEIGHT);

	/* Clamp the requested bytes per line value. If the maximum bytes per
	 * line value is zero, the module doesn't support user configurable line
	 * sizes. Override the requested value with the minimum in that case.
	 */
	min_bpl = pix->width * info->bpp;
	max_bpl = rounddown(XVIP_DMA_MAX_WIDTH, dma->align);
	bpl = rounddown(pix->bytesperline, dma->align);

	pix->bytesperline = clamp(bpl, min_bpl, max_bpl);
	pix->sizeimage = pix->bytesperline * pix->height;

	if (fmtinfo)
		*fmtinfo = info;
}

static int
xvip_dma_try_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);

	__xvip_dma_try_format(dma, &format->fmt.pix, NULL);
	return 0;
}

static int
xvip_dma_set_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	const struct xvip_video_format *info;
	struct xilinx_vdma_config config;
	int ret;

	__xvip_dma_try_format(dma, &format->fmt.pix, &info);

	mutex_lock(&dma->lock);

	if (vb2_is_streaming(&dma->queue)) {
		ret = -EBUSY;
		goto done;
	}

	dma->format = format->fmt.pix;
	dma->fmtinfo = info;

	/* Configure the DMA engine. */
	memset(&config, 0, sizeof(config));

	config.park = 1;
	config.park_frm = 0;
	config.vsize = dma->format.height;
	config.hsize = dma->format.width * info->bpp;
	config.stride = dma->format.bytesperline;
	config.ext_fsync = 2;
	config.frm_cnt_en = 1;
	config.coalesc = 1;

	dmaengine_device_control(dma->dma, DMA_SLAVE_CONFIG,
				 (unsigned long)&config);

	ret = 0;

done:
	mutex_unlock(&dma->lock);
	return ret;
}

static int
xvip_dma_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *rb)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);

	if (dma->queue.owner && dma->queue.owner != vfh) {
		ret = -EBUSY;
		goto done;
	}

	ret = vb2_reqbufs(&dma->queue, rb);
	if (ret < 0)
		goto done;

	dma->queue.owner = vfh;

done:
	mutex_unlock(&dma->lock);
	return ret ? ret : rb->count;
}

static int
xvip_dma_querybuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);
	ret = vb2_querybuf(&dma->queue, buf);
	mutex_unlock(&dma->lock);

	return ret;
}

static int
xvip_dma_qbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);

	if (dma->queue.owner && dma->queue.owner != vfh) {
		ret = -EBUSY;
		goto done;
	}

	ret = vb2_qbuf(&dma->queue, buf);

done:
	mutex_unlock(&dma->lock);
	return ret;
}

static int
xvip_dma_dqbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);

	if (dma->queue.owner && dma->queue.owner != vfh) {
		ret = -EBUSY;
		goto done;
	}

	ret = vb2_dqbuf(&dma->queue, buf, file->f_flags & O_NONBLOCK);

done:
	mutex_unlock(&dma->lock);
	return ret;
}

static int
xvip_dma_expbuf(struct file *file, void *priv, struct v4l2_exportbuffer *eb)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);

	if (dma->queue.owner && dma->queue.owner != vfh) {
		ret = -EBUSY;
		goto done;
	}

	ret = vb2_expbuf(&dma->queue, eb);

done:
	mutex_unlock(&dma->lock);
	return ret;
}

static int
xvip_dma_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);

	if (dma->queue.owner && dma->queue.owner != vfh) {
		ret = -EBUSY;
		goto done;
	}

	ret = vb2_streamon(&dma->queue, type);

done:
	mutex_unlock(&dma->lock);
	return ret;
}

static int
xvip_dma_streamoff(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);

	if (dma->queue.owner && dma->queue.owner != vfh) {
		ret = -EBUSY;
		goto done;
	}

	ret = vb2_streamoff(&dma->queue, type);

done:
	mutex_unlock(&dma->lock);
	return ret;
}

static const struct v4l2_ioctl_ops xvip_dma_ioctl_ops = {
	.vidioc_querycap		= xvip_dma_querycap,
	.vidioc_g_fmt_vid_cap		= xvip_dma_get_format,
	.vidioc_g_fmt_vid_out		= xvip_dma_get_format,
	.vidioc_s_fmt_vid_cap		= xvip_dma_set_format,
	.vidioc_s_fmt_vid_out		= xvip_dma_set_format,
	.vidioc_try_fmt_vid_cap		= xvip_dma_try_format,
	.vidioc_try_fmt_vid_out		= xvip_dma_try_format,
	.vidioc_reqbufs			= xvip_dma_reqbufs,
	.vidioc_querybuf		= xvip_dma_querybuf,
	.vidioc_qbuf			= xvip_dma_qbuf,
	.vidioc_dqbuf			= xvip_dma_dqbuf,
	.vidioc_expbuf			= xvip_dma_expbuf,
	.vidioc_streamon		= xvip_dma_streamon,
	.vidioc_streamoff		= xvip_dma_streamoff,
};

/* -----------------------------------------------------------------------------
 * V4L2 file operations
 */

static int xvip_dma_open(struct file *file)
{
	struct xvip_dma *dma = video_drvdata(file);
	struct v4l2_fh *vfh;

	vfh = kzalloc(sizeof(*vfh), GFP_KERNEL);
	if (vfh == NULL)
		return -ENOMEM;

	v4l2_fh_init(vfh, &dma->video);
	v4l2_fh_add(vfh);

	file->private_data = vfh;

	return 0;
}

static int xvip_dma_release(struct file *file)
{
	struct xvip_dma *dma = video_drvdata(file);
	struct v4l2_fh *vfh = file->private_data;

	mutex_lock(&dma->lock);
	if (dma->queue.owner == vfh) {
		vb2_queue_release(&dma->queue);
		dma->queue.owner = NULL;
	}
	mutex_unlock(&dma->lock);

	v4l2_fh_release(file);

	file->private_data = NULL;

	return 0;
}

static unsigned int xvip_dma_poll(struct file *file, poll_table *wait)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);
	ret = vb2_poll(&dma->queue, file, wait);
	mutex_unlock(&dma->lock);

	return ret;
}

static int xvip_dma_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct v4l2_fh *vfh = file->private_data;
	struct xvip_dma *dma = to_xvip_dma(vfh->vdev);
	int ret;

	mutex_lock(&dma->lock);
	ret = vb2_mmap(&dma->queue, vma);
	mutex_unlock(&dma->lock);

	return ret;
}

static struct v4l2_file_operations xvip_dma_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = xvip_dma_open,
	.release = xvip_dma_release,
	.poll = xvip_dma_poll,
	.mmap = xvip_dma_mmap,
};

/* -----------------------------------------------------------------------------
 * Xilinx Video DMA Core
 */

int xvip_dma_init(struct xvip_pipeline *xvipp, struct xvip_dma *dma,
		  enum v4l2_buf_type type)
{
	char name[10];
	int ret;

	dma->xvipp = xvipp;
	mutex_init(&dma->lock);

	dma->fmtinfo = xvip_get_format_by_fourcc(XVIP_DMA_DEF_FORMAT);
	dma->format.pixelformat = dma->fmtinfo->fourcc;
	dma->format.colorspace = V4L2_COLORSPACE_SRGB;
	dma->format.field = V4L2_FIELD_NONE;
	dma->format.width = XVIP_DMA_DEF_WIDTH;
	dma->format.height = XVIP_DMA_DEF_HEIGHT;
	dma->format.bytesperline = dma->format.width * dma->fmtinfo->bpp;
	dma->format.sizeimage = dma->format.bytesperline * dma->format.height;

	/* Initialize the media entity... */
	dma->pad.flags = type == V4L2_BUF_TYPE_VIDEO_CAPTURE
		       ? MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

	ret = media_entity_init(&dma->video.entity, 1, &dma->pad, 0);
	if (ret < 0)
		return ret;

	/* ... and the video node... */
	dma->video.v4l2_dev = &xvipp->v4l2_dev;
	dma->video.fops = &xvip_dma_fops;
	snprintf(dma->video.name, sizeof(dma->video.name), "%s %s",
		 xvipp->dev->of_node->full_name,
		 type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? "output" : "input");
	dma->video.vfl_type = VFL_TYPE_GRABBER;
	dma->video.vfl_dir = type == V4L2_BUF_TYPE_VIDEO_CAPTURE
			   ? VFL_DIR_RX : VFL_DIR_TX;
	dma->video.release = video_device_release_empty;
	dma->video.ioctl_ops = &xvip_dma_ioctl_ops;

	video_set_drvdata(&dma->video, dma);

	/* ... and the buffers queue... */
	dma->alloc_ctx = vb2_dma_contig_init_ctx(dma->xvipp->dev);
	if (IS_ERR(dma->alloc_ctx))
		goto error;

	dma->queue.type = type;
	dma->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dma->queue.drv_priv = dma;
	dma->queue.buf_struct_size = sizeof(struct xvip_dma_buffer);
	dma->queue.ops = &xvip_dma_queue_qops;
	dma->queue.mem_ops = &vb2_dma_contig_memops;
	dma->queue.timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ret = vb2_queue_init(&dma->queue);
	if (ret < 0) {
		dev_err(dma->xvipp->dev, "failed to initialize VB2 queue\n");
		goto error;
	}

	/* ... and the DMA channel. */
	sprintf(name, "vdma-%s",
		type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? "s2mm" : "mm2s");
	dma->dma = dma_request_slave_channel(dma->xvipp->dev, name);
	if (dma->dma == NULL) {
		dev_err(dma->xvipp->dev, "no VDMA channel found\n");
		ret = -ENODEV;
		goto error;
	}

	dma->align = 1 << dma->dma->device->copy_align;

	ret = video_register_device(&dma->video, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(dma->xvipp->dev, "failed to register video device\n");
		goto error;
	}

	return 0;

error:
	vb2_dma_contig_cleanup_ctx(dma->alloc_ctx);
	xvip_dma_cleanup(dma);
	return ret;
}

void xvip_dma_cleanup(struct xvip_dma *dma)
{
	if (video_is_registered(&dma->video))
		video_unregister_device(&dma->video);

	if (dma->dma)
		dma_release_channel(dma->dma);

	vb2_dma_contig_cleanup_ctx(dma->alloc_ctx);
	media_entity_cleanup(&dma->video.entity);
}

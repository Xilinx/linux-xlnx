//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Scene Change Detection driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Authors: Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>
 *          Satish Kumar Nagireddy <satish.nagireddy.nagireddy@xilinx.com>
 */

#include <linux/of.h>
#include <linux/xilinx-v4l2-events.h>

#include <media/v4l2-async.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "xilinx-scenechange.h"
#include "xilinx-vip.h"

#define XSCD_MAX_WIDTH		3840
#define XSCD_MAX_HEIGHT		2160
#define XSCD_MIN_WIDTH		640
#define XSCD_MIN_HEIGHT		480

#define XSCD_V_SUBSAMPLING		16
#define XSCD_BYTE_ALIGN			16
#define MULTIPLICATION_FACTOR		100
#define SCENE_CHANGE_THRESHOLD		0.5

#define XSCD_SCENE_CHANGE		1
#define XSCD_NO_SCENE_CHANGE		0

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int xscd_enum_mbus_code(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	return 0;
}

static int xscd_enum_frame_size(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	return 0;
}

static struct v4l2_mbus_framefmt *
__xscd_get_pad_format(struct xscd_chan *chan,
		      struct v4l2_subdev_pad_config *cfg,
		      unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&chan->subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &chan->format;
	default:
		return NULL;
	}
	return NULL;
}

static int xscd_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xscd_chan *chan = to_xscd_chan(subdev);

	fmt->format = *__xscd_get_pad_format(chan, cfg, fmt->pad, fmt->which);
	return 0;
}

static int xscd_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xscd_chan *chan = to_xscd_chan(subdev);
	struct v4l2_mbus_framefmt *format;

	format = __xscd_get_pad_format(chan, cfg, fmt->pad, fmt->which);
	format->width = clamp_t(unsigned int, fmt->format.width,
				XSCD_MIN_WIDTH, XSCD_MAX_WIDTH);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 XSCD_MIN_HEIGHT, XSCD_MAX_HEIGHT);
	format->code = fmt->format.code;
	fmt->format = *format;

	return 0;
}

static int xscd_chan_get_vid_fmt(u32 media_bus_fmt, bool memory_based)
{
	u32 vid_fmt;

	if (memory_based) {
		switch (media_bus_fmt) {
		case MEDIA_BUS_FMT_VYYUYY8_1X24:
		case MEDIA_BUS_FMT_UYVY8_1X16:
		case MEDIA_BUS_FMT_VUY8_1X24:
			vid_fmt = XSCD_VID_FMT_Y8;
			break;
		case MEDIA_BUS_FMT_VYYUYY10_4X20:
		case MEDIA_BUS_FMT_UYVY10_1X20:
		case MEDIA_BUS_FMT_VUY10_1X30:
			vid_fmt = XSCD_VID_FMT_Y10;
			break;
		default:
			vid_fmt = XSCD_VID_FMT_Y8;
		}

		return vid_fmt;
	}

	/* Streaming based */
	switch (media_bus_fmt) {
	case MEDIA_BUS_FMT_VYYUYY8_1X24:
	case MEDIA_BUS_FMT_VYYUYY10_4X20:
		vid_fmt = XSCD_VID_FMT_YUV_420;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYVY10_1X20:
		vid_fmt = XSCD_VID_FMT_YUV_422;
		break;
	case MEDIA_BUS_FMT_VUY8_1X24:
	case MEDIA_BUS_FMT_VUY10_1X30:
		vid_fmt = XSCD_VID_FMT_YUV_444;
		break;
	case MEDIA_BUS_FMT_RBG888_1X24:
	case MEDIA_BUS_FMT_RBG101010_1X30:
		vid_fmt = XSCD_VID_FMT_RGB;
		break;
	default:
		vid_fmt = XSCD_VID_FMT_YUV_420;
	}

	return vid_fmt;
}

/**
 * xscd_chan_configure_params - Program parameters to HW registers
 * @chan: Driver specific channel struct pointer
 */
static void xscd_chan_configure_params(struct xscd_chan *chan)
{
	u32 vid_fmt, stride;

	xscd_write(chan->iomem, XSCD_WIDTH_OFFSET, chan->format.width);

	/* Stride is required only for memory based IP, not for streaming IP */
	if (chan->xscd->memory_based) {
		stride = roundup(chan->format.width, XSCD_BYTE_ALIGN);
		xscd_write(chan->iomem, XSCD_STRIDE_OFFSET, stride);
	}

	xscd_write(chan->iomem, XSCD_HEIGHT_OFFSET, chan->format.height);

	/* Hardware video format */
	vid_fmt = xscd_chan_get_vid_fmt(chan->format.code,
					chan->xscd->memory_based);
	xscd_write(chan->iomem, XSCD_VID_FMT_OFFSET, vid_fmt);

	/*
	 * This is the vertical subsampling factor of the input image. Instead
	 * of sampling every line to calculate the histogram, IP uses this
	 * register value to sample only specific lines of the frame.
	 */
	xscd_write(chan->iomem, XSCD_SUBSAMPLE_OFFSET, XSCD_V_SUBSAMPLING);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */
static int xscd_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xscd_chan *chan = to_xscd_chan(subdev);

	if (enable)
		xscd_chan_configure_params(chan);

	xscd_dma_enable_channel(&chan->dmachan, enable);
	return 0;
}

static int xscd_subscribe_event(struct v4l2_subdev *sd,
				struct v4l2_fh *fh,
				struct v4l2_event_subscription *sub)
{
	int ret;
	struct xscd_chan *chan = to_xscd_chan(sd);

	mutex_lock(&chan->lock);

	switch (sub->type) {
	case V4L2_EVENT_XLNXSCD:
		ret = v4l2_event_subscribe(fh, sub, 1, NULL);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&chan->lock);

	return ret;
}

static int xscd_unsubscribe_event(struct v4l2_subdev *sd,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	int ret;
	struct xscd_chan *chan = to_xscd_chan(sd);

	mutex_lock(&chan->lock);
	ret = v4l2_event_unsubscribe(fh, sub);
	mutex_unlock(&chan->lock);

	return ret;
}

static int xscd_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xscd_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_core_ops xscd_core_ops = {
	.subscribe_event = xscd_subscribe_event,
	.unsubscribe_event = xscd_unsubscribe_event
};

static struct v4l2_subdev_video_ops xscd_video_ops = {
	.s_stream = xscd_s_stream,
};

static struct v4l2_subdev_pad_ops xscd_pad_ops = {
	.enum_mbus_code = xscd_enum_mbus_code,
	.enum_frame_size = xscd_enum_frame_size,
	.get_fmt = xscd_get_format,
	.set_fmt = xscd_set_format,
};

static struct v4l2_subdev_ops xscd_ops = {
	.core = &xscd_core_ops,
	.video = &xscd_video_ops,
	.pad = &xscd_pad_ops,
};

static const struct v4l2_subdev_internal_ops xscd_internal_ops = {
	.open = xscd_open,
	.close = xscd_close,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xscd_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

void xscd_chan_event_notify(struct xscd_chan *chan)
{
	u32 *eventdata;
	u32 sad, scd_threshold;

	sad = xscd_read(chan->iomem, XSCD_SAD_OFFSET);
	sad = (sad * XSCD_V_SUBSAMPLING * MULTIPLICATION_FACTOR) /
	       (chan->format.width * chan->format.height);
	eventdata = (u32 *)&chan->event.u.data;
	scd_threshold = SCENE_CHANGE_THRESHOLD * MULTIPLICATION_FACTOR;

	if (sad > scd_threshold)
		eventdata[0] = XSCD_SCENE_CHANGE;
	else
		eventdata[0] = XSCD_NO_SCENE_CHANGE;

	chan->event.type = V4L2_EVENT_XLNXSCD;
	v4l2_subdev_notify_event(&chan->subdev, &chan->event);
}

/**
 * xscd_chan_init - Initialize the V4L2 subdev for a channel
 * @xscd: Pointer to the SCD device structure
 * @chan_id: Channel id
 * @node: device node
 *
 * Return: '0' on success and failure value on error
 */
int xscd_chan_init(struct xscd_device *xscd, unsigned int chan_id,
		   struct device_node *node)
{
	struct xscd_chan *chan = &xscd->chans[chan_id];
	struct v4l2_subdev *subdev;
	unsigned int num_pads;
	int ret;

	mutex_init(&chan->lock);
	chan->xscd = xscd;
	chan->id = chan_id;
	chan->iomem = chan->xscd->iomem + chan->id * XSCD_CHAN_OFFSET;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &chan->subdev;
	v4l2_subdev_init(subdev, &xscd_ops);
	subdev->dev = chan->xscd->dev;
	subdev->fwnode = of_fwnode_handle(node);
	subdev->internal_ops = &xscd_internal_ops;
	snprintf(subdev->name, sizeof(subdev->name), "xlnx-scdchan.%u",
		 chan_id);
	v4l2_set_subdevdata(subdev, chan);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	/* Initialize default format */
	chan->format.code = MEDIA_BUS_FMT_VYYUYY8_1X24;
	chan->format.field = V4L2_FIELD_NONE;
	chan->format.width = XSCD_MAX_WIDTH;
	chan->format.height = XSCD_MAX_HEIGHT;

	/* Initialize media pads */
	num_pads = xscd->memory_based ? 1 : 2;

	chan->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	if (!xscd->memory_based)
		chan->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&subdev->entity, num_pads, chan->pads);
	if (ret < 0)
		goto error;

	subdev->entity.ops = &xscd_media_ops;
	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(chan->xscd->dev, "failed to register subdev\n");
		goto error;
	}

	dev_info(chan->xscd->dev, "Scene change detection channel found!\n");
	return 0;

error:
	media_entity_cleanup(&subdev->entity);
	return ret;
}

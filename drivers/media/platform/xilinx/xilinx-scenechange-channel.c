//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Scene Change Detection driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Author: Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/xilinx-v4l2-controls.h>
#include <linux/xilinx-v4l2-events.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>
#include "xilinx-scenechange.h"

#define XSCD_MAX_WIDTH		3840
#define XSCD_MAX_HEIGHT		2160
#define XSCD_MIN_WIDTH		640
#define XSCD_MIN_HEIGHT		480

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
	struct xscd_chan *chan = to_chan(subdev);

	fmt->format = *__xscd_get_pad_format(chan, cfg, fmt->pad, fmt->which);
	return 0;
}

static int xscd_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xscd_chan *chan = to_chan(subdev);
	struct v4l2_mbus_framefmt *format;

	format = __xscd_get_pad_format(chan, cfg, fmt->pad, fmt->which);
	format->width = clamp_t(unsigned int, fmt->format.width,
				XSCD_MIN_WIDTH, XSCD_MAX_WIDTH);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 XSCD_MIN_HEIGHT, XSCD_MAX_HEIGHT);
	fmt->format = *format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */
static int xscd_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xscd_chan *chan = to_chan(subdev);
	struct xscd_shared_data *shared_data;
	unsigned long flags;

	/* TODO: Re-organise shared data in a better way */
	shared_data = (struct xscd_shared_data *)chan->dev->parent->driver_data;
	chan->dmachan.en = enable;

	spin_lock_irqsave(&chan->dmachan.lock, flags);

	if (enable) {
		if (!shared_data->active_streams) {
			chan->dmachan.valid_interrupt = true;
			shared_data->active_streams++;
			xscd_dma_start_transfer(&chan->dmachan);
			xscd_dma_reset(&chan->dmachan);
			xscd_dma_chan_enable(&chan->dmachan, BIT(chan->id));
			xscd_dma_start(&chan->dmachan);
		} else {
			shared_data->active_streams++;
		}
	} else {
		shared_data->active_streams--;
	}

	spin_unlock_irqrestore(&chan->dmachan.lock, flags);
	return 0;
}

static int xscd_subscribe_event(struct v4l2_subdev *sd,
				struct v4l2_fh *fh,
				struct v4l2_event_subscription *sub)
{
	int ret;
	struct xscd_chan *chan = to_chan(sd);

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
	struct xscd_chan *chan = to_chan(sd);

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

static irqreturn_t xscd_chan_irq_handler(int irq, void *data)
{
	struct xscd_chan *chan = (struct xscd_chan *)data;
	u32 sad;
	u32 *eventdata;

	spin_lock(&chan->dmachan.lock);
	if (chan->dmachan.valid_interrupt) {
		spin_unlock(&chan->dmachan.lock);
		sad = xscd_read(chan->iomem, XILINX_XSCD_SAD_OFFSET +
				(chan->id * XILINX_XSCD_CHAN_OFFSET));
		sad = (sad * 16) / (chan->format.width * chan->format.height);
		memset(&chan->event, 0, sizeof(chan->event));
		eventdata = (u32 *)&chan->event.u.data;

		if (sad >= 1)
			eventdata[0] = 1;
		else
			eventdata[0] = 0;

		chan->event.type = V4L2_EVENT_XLNXSCD;
		v4l2_subdev_notify(&chan->subdev, V4L2_DEVICE_NOTIFY_EVENT,
				   &chan->event);
		return IRQ_HANDLED;
	}

	spin_unlock(&chan->dmachan.lock);
	return IRQ_NONE;
}

static int xscd_chan_parse_of(struct xscd_chan *chan)
{
	struct device_node *parent_node;
	struct xscd_shared_data *shared_data;
	int err;

	parent_node = chan->dev->parent->of_node;
	shared_data = (struct xscd_shared_data *)chan->dev->parent->driver_data;
	shared_data->dma_chan_list[chan->id] = &chan->dmachan;
	chan->iomem = shared_data->iomem;
	chan->irq = irq_of_parse_and_map(parent_node, 0);
	err = devm_request_irq(chan->dev, chan->irq, xscd_chan_irq_handler,
			       IRQF_SHARED, dev_name(chan->dev), chan);
	if (err) {
		dev_err(chan->dev, "unable to request IRQ %d\n", chan->irq);
		return err;
	}

	chan->dmachan.iomem = shared_data->iomem;
	chan->dmachan.id = chan->id;

	return 0;
}

/**
 * xscd_chan_probe - Driver probe function
 * @pdev: Pointer to the device structure
 *
 * Return: '0' on success and failure value on error
 */
static int xscd_chan_probe(struct platform_device *pdev)
{
	struct xscd_chan *chan;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *default_format;
	int ret;

	chan = devm_kzalloc(&pdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	mutex_init(&chan->lock);
	chan->dev = &pdev->dev;
	chan->id = pdev->id;
	ret = xscd_chan_parse_of(chan);
	if (ret < 0)
		return ret;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &chan->subdev;
	v4l2_subdev_init(subdev, &xscd_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xscd_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, chan);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	/* Initialize default format */
	default_format = &chan->format;
	default_format->code = MEDIA_BUS_FMT_VYYUYY8_1X24;
	default_format->field = V4L2_FIELD_NONE;
	default_format->colorspace = V4L2_COLORSPACE_SRGB;
	default_format->width = XSCD_MAX_WIDTH;
	default_format->height = XSCD_MAX_HEIGHT;
	chan->format = *default_format;
	chan->pad.flags = MEDIA_PAD_FL_SINK;
	subdev->entity.ops = &xscd_media_ops;

	ret = media_entity_pads_init(&subdev->entity, 1, &chan->pad);
	if (ret < 0)
		goto error;

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	dev_info(chan->dev, "Scene change detection channel found!\n");
	return 0;

error:
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xscd_chan_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver xscd_chan_driver = {
	.probe		= xscd_chan_probe,
	.remove		= xscd_chan_remove,
	.driver		= {
		.name	= "xlnx-scdchan",
	},
};

static int __init xscd_chan_init(void)
{
	platform_driver_register(&xscd_chan_driver);
	return 0;
}

static void __exit xscd_chan_exit(void)
{
	platform_driver_unregister(&xscd_chan_driver);
}

module_init(xscd_chan_init);
module_exit(xscd_chan_exit);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx Scene Change Detection");
MODULE_LICENSE("GPL v2");

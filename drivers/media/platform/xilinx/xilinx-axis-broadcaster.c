// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AXI4-Stream Video Broadcaster
 *
 * Copyright (C) 2021 Xilinx, Inc.
 *
 * Author: Ronak Shah <ronak.shah@xilinx.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define MAX_VBR_SINKS			1
#define MIN_VBR_SRCS			2
#define MAX_VBR_SRCS			16

/**
 * struct xvbroadcaster_device - AXI4-Stream Broadcaster device structure
 * @dev: Platform structure
 * @subdev: The v4l2 subdev structure
 * @pads: media pads
 * @formats: active V4L2 media bus formats on each pad
 * @npads: number of pads
 */
struct xvbroadcaster_device {
	struct device *dev;
	struct v4l2_subdev subdev;
	struct media_pad *pads;
	struct v4l2_mbus_framefmt formats;
	u32 npads;
};

static inline struct xvbroadcaster_device *to_xvbr(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xvbroadcaster_device, subdev);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int xvbr_s_stream(struct v4l2_subdev *subdev, int enable)
{
	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static struct v4l2_mbus_framefmt *
xvbr_get_pad_format(struct xvbroadcaster_device *xvbr,
		    struct v4l2_subdev_state *sd_state,
		    unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xvbr->subdev, sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xvbr->formats;
	default:
		return NULL;
	}
}

static int xvbr_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct xvbroadcaster_device *xvbr = to_xvbr(subdev);

	fmt->format = *xvbr_get_pad_format(xvbr, sd_state, fmt->pad, fmt->which);

	return 0;
}

static int xvbr_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct xvbroadcaster_device *xvbr = to_xvbr(subdev);
	struct v4l2_mbus_framefmt *format;

	format = xvbr_get_pad_format(xvbr, sd_state, fmt->pad, fmt->which);

	*format = fmt->format;

	xvip_set_format_size(format, fmt);

	fmt->format = *format;

	return 0;
}

static int xvbr_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xvbroadcaster_device *xvbr = to_xvbr(subdev);
	struct v4l2_mbus_framefmt *format;
	unsigned int i;

	for (i = 0; i < xvbr->npads; ++i) {
		format = v4l2_subdev_get_try_format(subdev, fh->state, i);
		*format = xvbr->formats;
	}

	return 0;
}

static int xvbr_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_video_ops xvbr_video_ops = {
	.s_stream = xvbr_s_stream,
};

static struct v4l2_subdev_pad_ops xvbr_pad_ops = {
	.enum_mbus_code = xvip_enum_mbus_code,
	.enum_frame_size = xvip_enum_frame_size,
	.get_fmt = xvbr_get_format,
	.set_fmt = xvbr_set_format,
};

static struct v4l2_subdev_ops xvbr_ops = {
	.video = &xvbr_video_ops,
	.pad = &xvbr_pad_ops,
};

static const struct v4l2_subdev_internal_ops xvbr_internal_ops = {
	.open = xvbr_open,
	.close = xvbr_close,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xvbr_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xvbr_parse_of(struct xvbroadcaster_device *xvbr)
{
	struct device_node *node = xvbr->dev->of_node;
	struct device_node *ports;
	struct device_node *port;

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;

	for_each_child_of_node(ports, port) {
		struct device_node *endpoint;

		if (!port->name || of_node_cmp(port->name, "port"))
			continue;

		endpoint = of_get_next_child(port, NULL);
		if (!endpoint) {
			dev_err(xvbr->dev, "No port at\n");
			return -EINVAL;
		}

		/* Count the number of ports. */
		xvbr->npads++;
	}

	/* validate number of ports */
	if ((xvbr->npads > (MAX_VBR_SINKS + MAX_VBR_SRCS)) ||
	    (xvbr->npads < (MAX_VBR_SINKS + MIN_VBR_SRCS))) {
		dev_err(xvbr->dev, "invalid number of ports %u\n", xvbr->npads);
		return -EINVAL;
	}

	return 0;
}

static int xvbr_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xvbroadcaster_device *xvbr;
	unsigned int i;
	int ret;

	xvbr = devm_kzalloc(&pdev->dev, sizeof(*xvbr), GFP_KERNEL);
	if (!xvbr)
		return -ENOMEM;

	xvbr->dev = &pdev->dev;

	ret = xvbr_parse_of(xvbr);
	if (ret < 0)
		return ret;

	/*
	 * Initialize V4L2 subdevice and media entity
	 */
	xvbr->pads = devm_kzalloc(&pdev->dev, xvbr->npads * sizeof(*xvbr->pads),
				  GFP_KERNEL);
	if (!xvbr->pads)
		return -ENOMEM;

	xvbr->pads[0].flags = MEDIA_PAD_FL_SINK;

	for (i = 1; i < xvbr->npads; ++i)
		xvbr->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	xvbr->formats.code = MEDIA_BUS_FMT_RGB888_1X24;
	xvbr->formats.field = V4L2_FIELD_NONE;
	xvbr->formats.colorspace = V4L2_COLORSPACE_SRGB;
	xvbr->formats.width = XVIP_MAX_WIDTH;
	xvbr->formats.height = XVIP_MAX_HEIGHT;

	subdev = &xvbr->subdev;
	v4l2_subdev_init(subdev, &xvbr_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xvbr_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xvbr);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.ops = &xvbr_media_ops;

	ret = media_entity_pads_init(&subdev->entity, xvbr->npads, xvbr->pads);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, xvbr);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	dev_info(xvbr->dev, "Xilinx AXI4-Stream Broadcaster found!\n");

	return 0;

error:
	media_entity_cleanup(&subdev->entity);

	return ret;
}

static int xvbr_remove(struct platform_device *pdev)
{
	struct xvbroadcaster_device *xvbr = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xvbr->subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct of_device_id xvbr_of_id_table[] = {
	{ .compatible = "xlnx,axis-broadcaster-1.1" },
	{ }
};
MODULE_DEVICE_TABLE(of, xvbr_of_id_table);

static struct platform_driver xvbr_driver = {
	.driver = {
		.name		= "xilinx-axis-broadcaster",
		.of_match_table	= xvbr_of_id_table,
	},
	.probe			= xvbr_probe,
	.remove			= xvbr_remove,
};

module_platform_driver(xvbr_driver);

MODULE_AUTHOR("Ronak Shah <ronak.shah@xilinx.com>");
MODULE_DESCRIPTION("Xilinx AXI4-Stream Broadcaster Driver");
MODULE_LICENSE("GPL v2");

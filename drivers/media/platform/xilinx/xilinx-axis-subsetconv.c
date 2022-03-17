// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Axis Subset Converter Driver
 *
 * Copyright (C) 2022 Xilinx, Inc.
 *
 * Authors: Anil Kumar M <anil.mamidal@xilinx.com>
 *          Karthikeyan T <karthikeyan.thangavel@xilinx.com>
 *
 * This converter driver is for matching the format of source pad
 * and sink pad in the media pipeline. The format of a source does
 * not match the sink pad if it is converted by a non-memory mapped
 * hardware IP. This subset converter driver is for non-memory mapped
 * axi stream subset converter which converts the format of the stream.
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/v4l2-subdev.h>
#include <media/media-entity.h>
#include <media/v4l2-subdev.h>
#include "xilinx-vip.h"

/* Number of media pads */
#define XSUBSETCONV_MEDIA_PADS		(2)

#define XSUBSETCONV_DEFAULT_WIDTH	(1920)
#define XSUBSETCONV_DEFAULT_HEIGHT	(1080)

/**
 * struct xsubsetconv_state - SW format converter device structure
 * @dev: Core structure for SW format converter
 * @subdev: The v4l2 subdev structure
 * @formats: Active V4L2 formats on each pad
 * @lock: mutex for serializing operations
 * @pads: media pads
 *
 * This structure contains the device driver related parameters
 */
struct xsubsetconv_state {
	struct device *dev;
	struct v4l2_subdev subdev;
	struct v4l2_mbus_framefmt formats[2];
	struct mutex lock; /* mutex lock for serializing operations */
	struct media_pad pads[XSUBSETCONV_MEDIA_PADS];
};

static const struct of_device_id xsubsetconv_of_id_table[] = {
	{ .compatible = "xlnx,axis-subsetconv-1.1"},
	{ }
};
MODULE_DEVICE_TABLE(of, xsubsetconv_of_id_table);

static inline struct xsubsetconv_state *
to_xsubsetconvstate(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xsubsetconv_state, subdev);
}

static struct v4l2_mbus_framefmt *
xsubsetconv_get_pad_format(struct xsubsetconv_state *xsubsetconv,
			   struct v4l2_subdev_state *state,
			   unsigned int pad, u32 which)
{
	struct v4l2_mbus_framefmt *format;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		format = v4l2_subdev_get_try_format(&xsubsetconv->subdev, state, pad);
		break;

	case V4L2_SUBDEV_FORMAT_ACTIVE:
		format =  &xsubsetconv->formats[pad];
		break;

	default:
		format = NULL;
	}

	return format;
}

/**
 * xsubsetconv_get_format - Get the pad format
 * @sd: pointer to v4l2 sub device structure
 * @state: pointer to sub device pad information structure
 * @fmt: pointer to pad level media bus format
 *
 * This function is used to get the pad format information.
 *
 * Return: -EINVAL or 0 on success
 */
static int xsubsetconv_get_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *fmt)
{
	struct xsubsetconv_state *xsubsetconv = to_xsubsetconvstate(sd);
	struct v4l2_mbus_framefmt *get_fmt;
	int ret = 0;

	mutex_lock(&xsubsetconv->lock);
	get_fmt = xsubsetconv_get_pad_format(xsubsetconv, state,
					     fmt->pad, fmt->which);

	if (!get_fmt) {
		ret = -EINVAL;
		goto unlock_get_format;
	}
	fmt->format = *get_fmt;

unlock_get_format:
	mutex_unlock(&xsubsetconv->lock);

	return ret;
}

/**
 * xsubsetconv_set_format - This is used to set the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @state: Pointer to sub device pad information structure
 * @fmt: Pointer to pad level media bus format
 *
 * This function is used to set the pad format.
 * Since the pad format is converted in hardware which is not
 * memory based IP, this driver will convert the source pad format
 * to the hardware outputting sink pad format. It actually cannot
 * convert any format.
 *
 * Return: -EINVAL or 0 on success
 */
static int xsubsetconv_set_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;
	struct xsubsetconv_state *xsubsetconv = to_xsubsetconvstate(sd);
	unsigned int src_code;
	int ret = 0;

	mutex_lock(&xsubsetconv->lock);

	format = xsubsetconv_get_pad_format(xsubsetconv, state,
					    fmt->pad, fmt->which);
	if (!format) {
		dev_err(xsubsetconv->dev, "get pad format error\n");
		ret = -EINVAL;
		goto unlock_set_fmt;
	}

	/* Restore the original pad format code */
	if (fmt->pad == XVIP_PAD_SOURCE) {
		struct v4l2_mbus_framefmt *sink_fmt;

		sink_fmt = xsubsetconv_get_pad_format(xsubsetconv, state,
						      XVIP_PAD_SINK, fmt->which);
		if (!sink_fmt) {
			dev_err(xsubsetconv->dev, "get sink pad format error\n");
			ret = -EINVAL;
			goto unlock_set_fmt;
		}
		/*
		 * TODO: Need to add a check to compare sink format and possible
		 *		 src format supported by subset converter
		 */
		*format = *sink_fmt;
		format->code = fmt->format.code;

	} else {
		struct v4l2_mbus_framefmt *src_fmt;

		src_fmt = xsubsetconv_get_pad_format(xsubsetconv, state,
						     XVIP_PAD_SOURCE, fmt->which);
		if (!src_fmt) {
			dev_err(xsubsetconv->dev, "get source pad format error\n");
			ret = -EINVAL;
			goto unlock_set_fmt;
		}

		*format = fmt->format;
		src_code = src_fmt->code;
		*src_fmt = *format;
		src_fmt->code = src_code;
	}

unlock_set_fmt:
	mutex_unlock(&xsubsetconv->lock);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xsubsetconv_media_ops = {
	.link_validate = v4l2_subdev_link_validate
};

static struct v4l2_subdev_pad_ops xsubsetconv_pad_ops = {
	.get_fmt = xsubsetconv_get_format,
	.set_fmt = xsubsetconv_set_format,
};

static struct v4l2_subdev_ops xsubsetconv_ops = {
	.pad = &xsubsetconv_pad_ops
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xsubsetconv_parse_of(struct xsubsetconv_state *xsubsetconv)
{
	struct device_node *node = xsubsetconv->dev->of_node;
	struct device_node *ports = NULL;
	struct device_node *port = NULL;
	unsigned int nports = 0;

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;

	for_each_child_of_node(ports, port) {
		struct device_node *endpoint;

		if (!port->name || of_node_cmp(port->name, "port"))
			continue;

		endpoint = of_get_next_child(port, NULL);
		if (!endpoint) {
			dev_err(xsubsetconv->dev, "No port at\n");
			return -EINVAL;
		}

		dev_dbg(xsubsetconv->dev, "%s : port %d\n", __func__, nports);

		/* Count the number of ports. */
		nports++;
	}

	if (nports != XSUBSETCONV_MEDIA_PADS) {
		dev_err(xsubsetconv->dev, "invalid number of ports %u\n", nports);
		return -EINVAL;
	}

	return 0;
}

static int xsubsetconv_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xsubsetconv_state *xsubsetconv;
	int ret;

	xsubsetconv = devm_kzalloc(&pdev->dev, sizeof(*xsubsetconv), GFP_KERNEL);
	if (!xsubsetconv)
		return -ENOMEM;

	xsubsetconv->dev = &pdev->dev;

	ret = xsubsetconv_parse_of(xsubsetconv);
	if (ret < 0) {
		dev_err(&pdev->dev, "xsubsetconv_parse_of ret = %d\n", ret);
		return ret;
	}

	mutex_init(&xsubsetconv->lock);

	/* Initialize V4L2 subdevice and media entity */
	xsubsetconv->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xsubsetconv->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize the sink format */
	memset(&xsubsetconv->formats[XVIP_PAD_SINK], 0,
	       sizeof(xsubsetconv->formats[0]));
	xsubsetconv->formats[XVIP_PAD_SINK].code = MEDIA_BUS_FMT_RGB888_1X24;
	xsubsetconv->formats[XVIP_PAD_SINK].field = V4L2_FIELD_NONE;
	xsubsetconv->formats[XVIP_PAD_SINK].colorspace = V4L2_COLORSPACE_SRGB;
	xsubsetconv->formats[XVIP_PAD_SINK].width = XSUBSETCONV_DEFAULT_WIDTH;
	xsubsetconv->formats[XVIP_PAD_SINK].height = XSUBSETCONV_DEFAULT_HEIGHT;

	xsubsetconv->formats[XVIP_PAD_SOURCE] = xsubsetconv->formats[XVIP_PAD_SINK];

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xsubsetconv->subdev;

	v4l2_subdev_init(subdev, &xsubsetconv_ops);

	subdev->dev = &pdev->dev;
	strscpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));

	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	subdev->entity.ops = &xsubsetconv_media_ops;

	v4l2_set_subdevdata(subdev, xsubsetconv);

	ret = media_entity_pads_init(&subdev->entity, XSUBSETCONV_MEDIA_PADS,
				     xsubsetconv->pads);
	if (ret < 0) {
		dev_err(&pdev->dev, "media pad init failed = %d\n", ret);
		mutex_destroy(&xsubsetconv->lock);
		return ret;
	}

	platform_set_drvdata(pdev, xsubsetconv);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	dev_info(&pdev->dev, "Xilinx AXI4-Stream Subset Converter found!\n");

	return 0;

error:
	media_entity_cleanup(&subdev->entity);
	mutex_destroy(&xsubsetconv->lock);

	return ret;
}

static int xsubsetconv_remove(struct platform_device *pdev)
{
	struct xsubsetconv_state *xsubsetconv = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xsubsetconv->subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	mutex_destroy(&xsubsetconv->lock);

	return 0;
}

static struct platform_driver xsubsetconv_driver = {
	.driver = {
		.name		= "xlnx,axis-subsetconv-1.1",
		.of_match_table	= xsubsetconv_of_id_table,
	},
	.probe			= xsubsetconv_probe,
	.remove			= xsubsetconv_remove,
};

module_platform_driver(xsubsetconv_driver);

MODULE_AUTHOR("Anil Kumar M <anil.mamidal@xilinx.com>");
MODULE_AUTHOR("Karthikeyan T <karthikeyan.thangavel@xilinx.com>");
MODULE_DESCRIPTION("Xilinx AXI4-Stream Subset Converter Driver");
MODULE_LICENSE("GPL v2");

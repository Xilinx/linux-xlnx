/*
 * Xilinx Video Switch
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XSW_CORE_CH_CTRL			0x0100
#define XSW_CORE_CH_CTRL_FORCE			(1 << 3)

#define XSW_SWITCH_STATUS			0x0104

/**
 * struct xswitch_device - Xilinx Video Switch device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @nsinks: number of sink pads (2 to 8)
 * @nsources: number of source pads (1 to 8)
 * @routing: sink pad connected to each source pad (-1 if none)
 * @formats: active V4L2 media bus formats on sink pads
 */
struct xswitch_device {
	struct xvip_device xvip;

	struct media_pad *pads;
	unsigned int nsinks;
	unsigned int nsources;

	int routing[8];

	struct v4l2_mbus_framefmt *formats;
};

static inline struct xswitch_device *to_xsw(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xswitch_device, xvip.subdev);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int xsw_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	unsigned int unused_input;
	unsigned int i;
	u32 routing;

	if (!enable) {
		xvip_stop(&xsw->xvip);
		return 0;
	}

	/*
	 * All outputs must be routed to an input. When less than 8 inputs are
	 * synthesized we can use input 7 for that purpose. Otherwise find an
	 * unused input to connect to unused outputs.
	 */
	if (xsw->nsinks == 8) {
		u32 mask;

		for (i = 0, mask = 0xff; i < xsw->nsources; ++i) {
			if (xsw->routing[i] != -1)
				mask &= ~BIT(xsw->routing[i]);
		}

		/*
		 * If all inputs are used all outputs are also used. We don't
		 * need an unused input in that case, use a zero value.
		 */
		unused_input = mask ? ffs(mask) - 1 : 0;
	} else {
		unused_input = 7;
	}

	/* Configure routing. */
	for (i = 0, routing = 0; i < xsw->nsources; ++i) {
		unsigned int route;

		route = xsw->routing[i] == -1 ?  unused_input : xsw->routing[i];
		routing |= (XSW_CORE_CH_CTRL_FORCE | route)
			<< (i * 4);
	}

	xvip_write(&xsw->xvip, XSW_CORE_CH_CTRL, routing);

	xvip_write(&xsw->xvip, XVIP_CTRL_CONTROL,
		   (((1 << xsw->nsources) - 1) << 4) |
		   XVIP_CTRL_CONTROL_SW_ENABLE);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static struct v4l2_mbus_framefmt *
xsw_get_pad_format(struct xswitch_device *xsw,
		   struct v4l2_subdev_pad_config *cfg,
		   unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xsw->xvip.subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xsw->formats[pad];
	default:
		return NULL;
	}
}

static int xsw_get_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	int pad = fmt->pad;

	if (pad >= xsw->nsinks) {
		pad = xsw->routing[pad - xsw->nsinks];
		if (pad < 0) {
			memset(&fmt->format, 0, sizeof(fmt->format));
			return 0;
		}
	}

	fmt->format = *xsw_get_pad_format(xsw, cfg, pad, fmt->which);

	return 0;
}

static int xsw_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	struct v4l2_mbus_framefmt *format;

	/* The source pad format is always identical to the sink pad format and
	 * can't be modified.
	 */
	if (fmt->pad >= xsw->nsinks)
		return xsw_get_format(subdev, cfg, fmt);

	format = xsw_get_pad_format(xsw, cfg, fmt->pad, fmt->which);

	format->code = fmt->format.code;
	format->width = clamp_t(unsigned int, fmt->format.width,
				XVIP_MIN_WIDTH, XVIP_MAX_WIDTH);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 XVIP_MIN_HEIGHT, XVIP_MAX_HEIGHT);
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *format;

	return 0;
}

static int xsw_get_routing(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_routing *route)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	unsigned int i;

	mutex_lock(&subdev->entity.graph_obj.mdev->graph_mutex);

	for (i = 0; i < min(xsw->nsources, route->num_routes); ++i) {
		route->routes[i].sink = xsw->routing[i];
		route->routes[i].source = i;
	}

	route->num_routes = xsw->nsources;

	mutex_unlock(&subdev->entity.graph_obj.mdev->graph_mutex);

	return 0;
}

static int xsw_set_routing(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_routing *route)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	unsigned int i;
	int ret = 0;

	mutex_lock(&subdev->entity.graph_obj.mdev->graph_mutex);

	if (subdev->entity.stream_count) {
		ret = -EBUSY;
		goto done;
	}

	for (i = 0; i < xsw->nsources; ++i)
		xsw->routing[i] = -1;

	for (i = 0; i < route->num_routes; ++i)
		xsw->routing[route->routes[i].source - xsw->nsinks] =
			route->routes[i].sink;

done:
	mutex_unlock(&subdev->entity.graph_obj.mdev->graph_mutex);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/**
 * xsw_init_formats - Initialize formats on all pads
 * @subdev: tpgper V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 *
 * The function sets the format on pad 0 only. In two pads mode, this is the
 * sink pad and the set format handler will propagate the format to the source
 * pad. In one pad mode this is the source pad.
 */
static void xsw_init_formats(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	struct v4l2_subdev_format format;
	unsigned int i;

	for (i = 0; i < xsw->nsinks; ++i) {
		memset(&format, 0, sizeof(format));

		format.pad = 0;
		format.which = fh ? V4L2_SUBDEV_FORMAT_TRY
			     : V4L2_SUBDEV_FORMAT_ACTIVE;
		format.format.width = 1920;
		format.format.height = 1080;

		xsw_set_format(subdev, fh ? fh->pad : NULL, &format);
	}
}

static int xsw_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xsw_init_formats(subdev, fh);

	return 0;
}

static int xsw_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_video_ops xsw_video_ops = {
	.s_stream = xsw_s_stream,
};

static struct v4l2_subdev_pad_ops xsw_pad_ops = {
	.enum_mbus_code = xvip_enum_mbus_code,
	.enum_frame_size = xvip_enum_frame_size,
	.get_fmt = xsw_get_format,
	.set_fmt = xsw_set_format,
	.get_routing = xsw_get_routing,
	.set_routing = xsw_set_routing,
};

static struct v4l2_subdev_ops xsw_ops = {
	.video = &xsw_video_ops,
	.pad = &xsw_pad_ops,
};

static const struct v4l2_subdev_internal_ops xsw_internal_ops = {
	.open = xsw_open,
	.close = xsw_close,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static bool xsw_has_route(struct media_entity *entity, unsigned int pad0,
			  unsigned int pad1)
{
	struct xswitch_device *xsw = container_of(entity, struct xswitch_device,
						  xvip.subdev.entity);
	unsigned int sink0, sink1;

	/* Two sinks are never connected together. */
	if (pad0 < xsw->nsinks && pad1 < xsw->nsinks)
		return false;

	sink0 = pad0 < xsw->nsinks ? pad0 : xsw->routing[pad0 - xsw->nsinks];
	sink1 = pad1 < xsw->nsinks ? pad1 : xsw->routing[pad1 - xsw->nsinks];

	return sink0 == sink1;
}

static const struct media_entity_operations xsw_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.has_route = xsw_has_route,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xsw_parse_of(struct xswitch_device *xsw)
{
	struct device_node *node = xsw->xvip.dev->of_node;
	int ret;

	ret = of_property_read_u32(node, "#xlnx,inputs", &xsw->nsinks);
	if (ret < 0) {
		dev_err(xsw->xvip.dev, "missing or invalid #xlnx,%s property\n",
			"inputs");
		return ret;
	}

	ret = of_property_read_u32(node, "#xlnx,outputs", &xsw->nsources);
	if (ret < 0) {
		dev_err(xsw->xvip.dev, "missing or invalid #xlnx,%s property\n",
			"outputs");
		return ret;
	}

	return 0;
}

static int xsw_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xswitch_device *xsw;
	unsigned int npads;
	unsigned int i;
	int ret;

	xsw = devm_kzalloc(&pdev->dev, sizeof(*xsw), GFP_KERNEL);
	if (!xsw)
		return -ENOMEM;

	xsw->xvip.dev = &pdev->dev;

	ret = xsw_parse_of(xsw);
	if (ret < 0)
		return ret;

	ret = xvip_init_resources(&xsw->xvip);
	if (ret < 0)
		return ret;

	/* Initialize V4L2 subdevice and media entity. Pad numbers depend on the
	 * number of pads.
	 */
	npads = xsw->nsinks + xsw->nsources;
	xsw->pads = devm_kzalloc(&pdev->dev, npads * sizeof(*xsw->pads),
				 GFP_KERNEL);
	if (!xsw->pads)
		goto error;

	for (i = 0; i < xsw->nsinks; ++i)
		xsw->pads[i].flags = MEDIA_PAD_FL_SINK;
	for (; i < npads; ++i)
		xsw->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	xsw->formats = devm_kzalloc(&pdev->dev,
				    xsw->nsinks * sizeof(*xsw->formats),
				    GFP_KERNEL);
	if (!xsw->formats)
		goto error;

	for (i = 0; i < xsw->nsources; ++i)
		xsw->routing[i] = i < xsw->nsinks ? i : -1;

	subdev = &xsw->xvip.subdev;
	v4l2_subdev_init(subdev, &xsw_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xsw_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xsw);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.ops = &xsw_media_ops;

	xsw_init_formats(subdev, NULL);

	ret = media_entity_pads_init(&subdev->entity, npads, xsw->pads);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, xsw);

	xvip_print_version(&xsw->xvip);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	return 0;

error:
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xsw->xvip);
	return ret;
}

static int xsw_remove(struct platform_device *pdev)
{
	struct xswitch_device *xsw = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xsw->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	xvip_cleanup_resources(&xsw->xvip);

	return 0;
}

static const struct of_device_id xsw_of_id_table[] = {
	{ .compatible = "xlnx,v-switch-1.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, xsw_of_id_table);

static struct platform_driver xsw_driver = {
	.driver = {
		.name		= "xilinx-switch",
		.of_match_table	= xsw_of_id_table,
	},
	.probe			= xsw_probe,
	.remove			= xsw_remove,
};

module_platform_driver(xsw_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Video Switch Driver");
MODULE_LICENSE("GPL v2");

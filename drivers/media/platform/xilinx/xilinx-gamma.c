/*
 * Xilinx Gamma Correction IP
 *
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-gamma-coeff.h"
#include "xilinx-vip.h"

#define XGAMMA_MIN_HEIGHT	(64)
#define XGAMMA_MAX_HEIGHT	(4320)
#define XGAMMA_DEF_HEIGHT	(720)
#define XGAMMA_MIN_WIDTH	(64)
#define XGAMMA_MAX_WIDTH	(8192)
#define XGAMMA_DEF_WIDTH	(1280)

#define XGAMMA_AP_CTRL			(0x0000)
#define XGAMMA_GIE			(0x0004)
#define XGAMMA_IER			(0x0008)
#define XGAMMA_ISR			(0x000c)
#define XGAMMA_WIDTH			(0x0010)
#define XGAMMA_HEIGHT			(0x0018)
#define XGAMMA_VIDEO_FORMAT		(0x0020)
#define XGAMMA_GAMMA_LUT_0_BASE		(0x0800)
#define XGAMMA_GAMMA_LUT_1_BASE		(0x1000)
#define XGAMMA_GAMMA_LUT_2_BASE		(0x1800)

#define XGAMMA_RESET_DEASSERT	(0)
#define XGAMMA_RESET_ASSERT	(1)
#define XGAMMA_START		BIT(0)
#define XGAMMA_AUTO_RESTART	BIT(7)
#define XGAMMA_STREAM_ON	(XGAMMA_START | XGAMMA_AUTO_RESTART)

enum xgamma_video_format {
	XGAMMA_RGB = 0,
};

/**
 * struct xgamma_dev - Xilinx Video Gamma LUT device structure
 * @xvip: Xilinx Video IP device
 * @pads: Scaler sub-device media pads
 * @formats: V4L2 media bus formats at the sink and source pads
 * @default_formats: default V4L2 media bus formats
 * @ctrl_handler: V4L2 Control Handler for R,G,B Gamma Controls
 * @red_lut: Pointer to the gamma coefficient as per the Red Gamma control
 * @green_lut: Pointer to the gamma coefficient as per the Green Gamma control
 * @blue_lut: Pointer to the gamma coefficient as per the Blue Gamma control
 * @color_depth: Color depth of the Video Gamma IP
 * @gamma_table: Pointer to the table containing various gamma values
 * @rst_gpio: GPIO reset line to bring VPSS Scaler out of reset
 * @max_width: Maximum width supported by this instance.
 * @max_height: Maximum height supported by this instance.
 */
struct xgamma_dev {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_formats[2];
	struct v4l2_ctrl_handler ctrl_handler;

	const u16 *red_lut;
	const u16 *green_lut;
	const u16 *blue_lut;
	u32 color_depth;
	const u16 **gamma_table;
	struct gpio_desc *rst_gpio;
	u32 max_width;
	u32 max_height;
};

static inline u32 xg_read(struct xgamma_dev *xg, u32 reg)
{
	u32 data;

	data = xvip_read(&xg->xvip, reg);
	dev_dbg(xg->xvip.dev,
		"Reading 0x%x from reg offset 0x%x", data, reg);
	return data;
}

static inline void xg_write(struct xgamma_dev *xg, u32 reg, u32 data)
{
	dev_dbg(xg->xvip.dev,
		"Writing 0x%x to reg offset 0x%x", data, reg);
	xvip_write(&xg->xvip, reg, data);
#ifdef DEBUG
	if (xg_read(xg, reg) != data)
		dev_err(xg->xvip.dev,
			"Write 0x%x does not match read back", data);
#endif
}

static inline struct xgamma_dev *to_xg(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xgamma_dev, xvip.subdev);
}

static struct v4l2_mbus_framefmt *
__xg_get_pad_format(struct xgamma_dev *xg,
		    struct v4l2_subdev_pad_config *cfg,
		    unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(
					&xg->xvip.subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xg->formats[pad];
	default:
		return NULL;
	}
}

static void xg_set_lut_entries(struct xgamma_dev *xg,
			       const u16 *lut, const u32 lut_base)
{
	int itr;
	u32 lut_offset, lut_data;

	lut_offset = lut_base;
	/* Write LUT Entries */
	for (itr = 0; itr < BIT(xg->color_depth - 1); itr++) {
		lut_data = (lut[2 * itr + 1] << 16) | lut[2 * itr];
		xg_write(xg, lut_offset, lut_data);
		lut_offset += 4;
	}
}

static int xg_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xgamma_dev *xg = to_xg(subdev);

	if (!enable) {
		dev_dbg(xg->xvip.dev, "%s : Off", __func__);
		gpiod_set_value_cansleep(xg->rst_gpio, XGAMMA_RESET_ASSERT);
		gpiod_set_value_cansleep(xg->rst_gpio, XGAMMA_RESET_DEASSERT);
		return 0;
	}
	dev_dbg(xg->xvip.dev, "%s : Started", __func__);

	dev_dbg(xg->xvip.dev, "%s : Setting width %d and height %d",
		__func__, xg->formats[XVIP_PAD_SINK].width,
		xg->formats[XVIP_PAD_SINK].height);
	xg_write(xg, XGAMMA_WIDTH, xg->formats[XVIP_PAD_SINK].width);
	xg_write(xg, XGAMMA_HEIGHT, xg->formats[XVIP_PAD_SINK].height);
	xg_write(xg, XGAMMA_VIDEO_FORMAT, XGAMMA_RGB);
	xg_set_lut_entries(xg, xg->red_lut, XGAMMA_GAMMA_LUT_0_BASE);
	xg_set_lut_entries(xg, xg->green_lut, XGAMMA_GAMMA_LUT_1_BASE);
	xg_set_lut_entries(xg, xg->blue_lut, XGAMMA_GAMMA_LUT_2_BASE);

	/* Start GAMMA Correction LUT Video IP */
	xg_write(xg, XGAMMA_AP_CTRL, XGAMMA_STREAM_ON);
	return 0;
}

static const struct v4l2_subdev_video_ops xg_video_ops = {
	.s_stream = xg_s_stream,
};

static int xg_get_format(struct v4l2_subdev *subdev,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *fmt)
{
	struct xgamma_dev *xg = to_xg(subdev);

	fmt->format = *__xg_get_pad_format(xg, cfg, fmt->pad, fmt->which);
	return 0;
}

static int xg_set_format(struct v4l2_subdev *subdev,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *fmt)
{
	struct xgamma_dev *xg = to_xg(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xg_get_pad_format(xg, cfg, fmt->pad, fmt->which);
	*__format = fmt->format;

	if (fmt->pad == XVIP_PAD_SINK) {
		if (__format->code != MEDIA_BUS_FMT_RBG888_1X24) {
			dev_dbg(xg->xvip.dev,
				"Unsupported sink media bus code format");
			__format->code = MEDIA_BUS_FMT_RBG888_1X24;
		}
	}
	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XGAMMA_MIN_WIDTH, xg->max_width);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XGAMMA_MIN_HEIGHT, xg->max_height);

	fmt->format = *__format;
	/* Propagate to Source Pad */
	__format = __xg_get_pad_format(xg, cfg, XVIP_PAD_SOURCE, fmt->which);
	*__format = fmt->format;
	return 0;
}

static int xg_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xgamma_dev *xg = to_xg(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SINK);
	*format = xg->default_formats[XVIP_PAD_SINK];

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SOURCE);
	*format = xg->default_formats[XVIP_PAD_SOURCE];
	return 0;
}

static int xg_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops xg_internal_ops = {
	.open = xg_open,
	.close = xg_close,
};

static const struct v4l2_subdev_pad_ops xg_pad_ops = {
	.enum_mbus_code = xvip_enum_mbus_code,
	.enum_frame_size = xvip_enum_frame_size,
	.get_fmt = xg_get_format,
	.set_fmt = xg_set_format,
};

static const struct v4l2_subdev_ops xg_ops = {
	.video = &xg_video_ops,
	.pad = &xg_pad_ops,
};

static int
select_gamma(s32 value, const u16 **coeff, const u16 **xgamma_curves)
{
	if (!coeff)
		return -EINVAL;
	if (value <= 0 || value > GAMMA_CURVE_LENGTH)
		return -EINVAL;

	*coeff = *(xgamma_curves + value - 1);
	return 0;
}

static int xg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int rval;
	struct xgamma_dev *xg =
		container_of(ctrl->handler,
			     struct xgamma_dev, ctrl_handler);
	dev_dbg(xg->xvip.dev, "%s called", __func__);
	switch (ctrl->id) {
	case V4L2_CID_XILINX_GAMMA_CORR_RED_GAMMA:
		rval = select_gamma(ctrl->val, &xg->red_lut, xg->gamma_table);
		if (rval < 0) {
			dev_err(xg->xvip.dev, "Invalid Red Gamma");
			return rval;
		}
		dev_dbg(xg->xvip.dev, "%s: Setting Red Gamma to %d.%d",
			__func__, ctrl->val / 10, ctrl->val % 10);
		xg_set_lut_entries(xg, xg->red_lut, XGAMMA_GAMMA_LUT_0_BASE);
		break;
	case V4L2_CID_XILINX_GAMMA_CORR_BLUE_GAMMA:
		rval = select_gamma(ctrl->val, &xg->blue_lut, xg->gamma_table);
		if (rval < 0) {
			dev_err(xg->xvip.dev, "Invalid Blue Gamma");
			return rval;
		}
		dev_dbg(xg->xvip.dev, "%s: Setting Blue Gamma to %d.%d",
			__func__, ctrl->val / 10, ctrl->val % 10);
		xg_set_lut_entries(xg, xg->blue_lut, XGAMMA_GAMMA_LUT_1_BASE);
		break;
	case V4L2_CID_XILINX_GAMMA_CORR_GREEN_GAMMA:
		rval = select_gamma(ctrl->val, &xg->green_lut, xg->gamma_table);
		if (rval < 0) {
			dev_err(xg->xvip.dev, "Invalid Green Gamma");
			return -EINVAL;
		}
		dev_dbg(xg->xvip.dev, "%s: Setting Green Gamma to %d.%d",
			__func__, ctrl->val / 10, ctrl->val % 10);
		xg_set_lut_entries(xg, xg->green_lut, XGAMMA_GAMMA_LUT_2_BASE);
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops xg_ctrl_ops = {
	.s_ctrl = xg_s_ctrl,
};

static struct v4l2_ctrl_config xg_ctrls[] = {
	/* Red Gamma */
	{
		.ops = &xg_ctrl_ops,
		.id = V4L2_CID_XILINX_GAMMA_CORR_RED_GAMMA,
		.name = "Red Gamma Correction|1->0.1|10->1.0",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 10,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Blue Gamma */
	{
		.ops = &xg_ctrl_ops,
		.id = V4L2_CID_XILINX_GAMMA_CORR_BLUE_GAMMA,
		.name = "Blue Gamma Correction|1->0.1|10->1.0",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 10,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Green Gamma */
	{
		.ops = &xg_ctrl_ops,
		.id = V4L2_CID_XILINX_GAMMA_CORR_GREEN_GAMMA,
		.name = "Green Gamma Correction|1->0.1|10->1.0)",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 10,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static const struct media_entity_operations xg_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int xg_parse_of(struct xgamma_dev *xg)
{
	struct device *dev = xg->xvip.dev;
	struct device_node *node = dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	u32 port_id = 0;
	int rval;

	rval = of_property_read_u32(node, "xlnx,max-height", &xg->max_height);
	if (rval < 0) {
		dev_err(dev, "xlnx,max-height is missing!");
		return -EINVAL;
	} else if (xg->max_height > XGAMMA_MAX_HEIGHT ||
		   xg->max_height < XGAMMA_MIN_HEIGHT) {
		dev_err(dev, "Invalid height in dt");
		return -EINVAL;
	}

	rval = of_property_read_u32(node, "xlnx,max-width", &xg->max_width);
	if (rval < 0) {
		dev_err(dev, "xlnx,max-width is missing!");
		return -EINVAL;
	} else if (xg->max_width > XGAMMA_MAX_WIDTH ||
		   xg->max_width < XGAMMA_MIN_WIDTH) {
		dev_err(dev, "Invalid width in dt");
		return -EINVAL;
	}

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;

	/* Get the format description for each pad */
	for_each_child_of_node(ports, port) {
		if (port->name && (of_node_cmp(port->name, "port") == 0)) {
			rval = of_property_read_u32(port, "reg", &port_id);
			if (rval < 0) {
				dev_err(dev, "No reg in DT");
				return rval;
			}
			if (port_id != 0 && port_id != 1) {
				dev_err(dev, "Invalid reg in DT");
				return -EINVAL;
			}

			rval = of_property_read_u32(port, "xlnx,video-width",
						    &xg->color_depth);
			if (rval < 0) {
				dev_err(dev, "Missing xlnx-video-width in DT");
				return rval;
			}
			switch (xg->color_depth) {
			case GAMMA_BPC_8:
				xg->gamma_table = xgamma8_curves;
				break;
			case GAMMA_BPC_10:
				xg->gamma_table = xgamma10_curves;
				break;
			default:
				dev_err(dev, "Unsupported color depth %d",
					xg->color_depth);
				return -EINVAL;
			}
		}
	}

	xg->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xg->rst_gpio)) {
		if (PTR_ERR(xg->rst_gpio) != -EPROBE_DEFER)
			dev_err(dev, "Reset GPIO not setup in DT");
		return PTR_ERR(xg->rst_gpio);
	}
	return 0;
}

static int xg_probe(struct platform_device *pdev)
{
	struct xgamma_dev *xg;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *def_fmt;
	int rval, itr;

	dev_dbg(&pdev->dev, "Gamma LUT Probe Started");
	xg = devm_kzalloc(&pdev->dev, sizeof(*xg), GFP_KERNEL);
	if (!xg)
		return -ENOMEM;
	xg->xvip.dev = &pdev->dev;
	rval = xg_parse_of(xg);
	if (rval < 0)
		return rval;
	rval = xvip_init_resources(&xg->xvip);

	dev_dbg(xg->xvip.dev, "Reset Xilinx Video Gamma Corrrection");
	gpiod_set_value_cansleep(xg->rst_gpio, XGAMMA_RESET_DEASSERT);

	/* Init V4L2 subdev */
	subdev = &xg->xvip.subdev;
	v4l2_subdev_init(subdev, &xg_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xg_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Default Formats Initialization */
	def_fmt = &xg->default_formats[XVIP_PAD_SINK];
	/* GAMMA LUT IP only to be supported for RGB */
	def_fmt->code = MEDIA_BUS_FMT_RBG888_1X24;
	def_fmt->field = V4L2_FIELD_NONE;
	def_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	def_fmt->width = XGAMMA_DEF_WIDTH;
	def_fmt->height = XGAMMA_DEF_HEIGHT;
	xg->formats[XVIP_PAD_SINK] = *def_fmt;

	def_fmt = &xg->default_formats[XVIP_PAD_SOURCE];
	*def_fmt = xg->default_formats[XVIP_PAD_SINK];
	xg->formats[XVIP_PAD_SOURCE] = *def_fmt;

	xg->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xg->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Init Media Entity */
	subdev->entity.ops = &xg_media_ops;
	rval = media_entity_pads_init(&subdev->entity, 2, xg->pads);
	if (rval < 0)
		goto media_error;

	/* V4L2 Controls */
	v4l2_ctrl_handler_init(&xg->ctrl_handler, ARRAY_SIZE(xg_ctrls));
	for (itr = 0; itr < ARRAY_SIZE(xg_ctrls); itr++) {
		v4l2_ctrl_new_custom(&xg->ctrl_handler,
				     &xg_ctrls[itr], NULL);
	}
	if (xg->ctrl_handler.error) {
		dev_err(&pdev->dev, "Failed to add V4L2 controls");
		rval = xg->ctrl_handler.error;
		goto ctrl_error;
	}
	subdev->ctrl_handler = &xg->ctrl_handler;
	rval = v4l2_ctrl_handler_setup(&xg->ctrl_handler);
	if (rval < 0) {
		dev_err(&pdev->dev, "Failed to setup control handler");
		goto  ctrl_error;
	}

	platform_set_drvdata(pdev, xg);
	rval = v4l2_async_register_subdev(subdev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to register subdev");
		goto v4l2_subdev_error;
	}
	dev_info(&pdev->dev,
		 "Xilinx %d-bit Video Gamma Correction LUT registered",
		 xg->color_depth);
	return 0;
ctrl_error:
	v4l2_ctrl_handler_free(&xg->ctrl_handler);
v4l2_subdev_error:
	media_entity_cleanup(&subdev->entity);
media_error:
	xvip_cleanup_resources(&xg->xvip);
	return rval;
}

static int xg_remove(struct platform_device *pdev)
{
	struct xgamma_dev *xg = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xg->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	/* Add entry to cleanup v4l2 control handle */
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xg->xvip);
	return 0;
}

static const struct of_device_id xg_of_id_table[] = {
	{.compatible = "xlnx,v-gamma-lut"},
	{ }
};
MODULE_DEVICE_TABLE(of, xg_of_id_table);

static struct platform_driver xg_driver = {
	.driver = {
		.name = "xilinx-gamma-lut",
		.of_match_table = xg_of_id_table,
	},
	.probe = xg_probe,
	.remove = xg_remove,
};

module_platform_driver(xg_driver);
MODULE_DESCRIPTION("Xilinx Video Gamma Correction LUT Driver");
MODULE_LICENSE("GPL v2");

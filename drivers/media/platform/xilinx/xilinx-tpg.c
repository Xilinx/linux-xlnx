/*
 * Xilinx Test Pattern Generator
 *
 * Copyright (C) 2013 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XTPG_CTRL_STATUS_SLAVE_ERROR		(1 << 16)
#define XTPG_CTRL_IRQ_SLAVE_ERROR		(1 << 16)

#define XTPG_PATTERN_CONTROL			0x0100
#define XTPG_PATTERN_MASK			(0xf << 0)
#define XTPG_PATTERN_CONTROL_CROSS_HAIRS	(1 << 4)
#define XTPG_PATTERN_CONTROL_MOVING_BOX		(1 << 5)
#define XTPG_PATTERN_CONTROL_COLOR_MASK_SHIFT	6
#define XTPG_PATTERN_CONTROL_COLOR_MASK_MASK	(0xf << 6)
#define XTPG_PATTERN_CONTROL_STUCK_PIXEL	(1 << 9)
#define XTPG_PATTERN_CONTROL_NOISE		(1 << 10)
#define XTPG_PATTERN_CONTROL_MOTION		(1 << 12)
#define XTPG_MOTION_SPEED			0x0104
#define XTPG_CROSS_HAIRS			0x0108
#define XTPG_CROSS_HAIRS_ROW_SHIFT		0
#define XTPG_CROSS_HAIRS_ROW_MASK		(0xfff << 0)
#define XTPG_CROSS_HAIRS_COLUMN_SHIFT		16
#define XTPG_CROSS_HAIRS_COLUMN_MASK		(0xfff << 16)
#define XTPG_ZPLATE_HOR_CONTROL			0x010c
#define XTPG_ZPLATE_VER_CONTROL			0x0110
#define XTPG_ZPLATE_START_SHIFT			0
#define XTPG_ZPLATE_START_MASK			(0xffff << 0)
#define XTPG_ZPLATE_SPEED_SHIFT			16
#define XTPG_ZPLATE_SPEED_MASK			(0xffff << 16)
#define XTPG_BOX_SIZE				0x0114
#define XTPG_BOX_COLOR				0x0118
#define XTPG_STUCK_PIXEL_THRESH			0x011c
#define XTPG_NOISE_GAIN				0x0120
#define XTPG_BAYER_PHASE			0x0124

/*
 * Private Controls for Xilinx TPG Video IP
 */

#define V4L2_CID_XILINX_TPG			(V4L2_CID_USER_BASE + 0xb000)

/* Draw cross hairs */
#define V4L2_CID_XILINX_TPG_CROSS_HAIRS		(V4L2_CID_XILINX_TPG + 1)
/* Enable a moving box */
#define V4L2_CID_XILINX_TPG_MOVING_BOX		(V4L2_CID_XILINX_TPG + 2)
/* Mask out a color component */
#define V4L2_CID_XILINX_TPG_COLOR_MASK		(V4L2_CID_XILINX_TPG + 3)
/* Enable a stuck pixel feature */
#define V4L2_CID_XILINX_TPG_STUCK_PIXEL		(V4L2_CID_XILINX_TPG + 4)
/* Enable a noisy output */
#define V4L2_CID_XILINX_TPG_NOISE		(V4L2_CID_XILINX_TPG + 5)
/* Enable the motion feature */
#define V4L2_CID_XILINX_TPG_MOTION		(V4L2_CID_XILINX_TPG + 6)
/* Configure the motion speed of moving patterns */
#define V4L2_CID_XILINX_TPG_MOTION_SPEED	(V4L2_CID_XILINX_TPG + 7)
/* The row of horizontal cross hair location */
#define V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW	(V4L2_CID_XILINX_TPG + 8)
/* The colum of vertical cross hair location */
#define V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN	(V4L2_CID_XILINX_TPG + 9)
/* Set starting point of sine wave for horizontal component */
#define V4L2_CID_XILINX_TPG_ZPLATE_HOR_START	(V4L2_CID_XILINX_TPG + 10)
/* Set speed of the horizontal component */
#define V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED	(V4L2_CID_XILINX_TPG + 11)
/* Set starting point of sine wave for vertical component */
#define V4L2_CID_XILINX_TPG_ZPLATE_VER_START	(V4L2_CID_XILINX_TPG + 12)
/* Set speed of the vertical component */
#define V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED	(V4L2_CID_XILINX_TPG + 13)
/* Moving box size */
#define V4L2_CID_XILINX_TPG_BOX_SIZE		(V4L2_CID_XILINX_TPG + 14)
/* Moving box color */
#define V4L2_CID_XILINX_TPG_BOX_COLOR		(V4L2_CID_XILINX_TPG + 15)
/* Upper limit count of generated stuck pixels */
#define V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH	(V4L2_CID_XILINX_TPG + 16)
/* Noise level */
#define V4L2_CID_XILINX_TPG_NOISE_GAIN		(V4L2_CID_XILINX_TPG + 17)

/**
 * struct xtpg_device - Xilinx Test Pattern Generator device structure
 * @xvip: Xilinx Video IP device
 * @pad: media pad
 * @format: active V4L2 media bus format at the source pad
 * @default_format: default V4L2 media bus format
 * @vip_format: format information corresponding to the active format
 * @ctrl_handler: control handler
 */
struct xtpg_device {
	struct xvip_device xvip;

	struct media_pad pad;

	struct v4l2_mbus_framefmt format;
	struct v4l2_mbus_framefmt default_format;
	const struct xvip_video_format *vip_format;

	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xtpg_device *to_tpg(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xtpg_device, xvip.subdev);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int xtpg_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	const u32 width = xtpg->format.width;
	const u32 height = xtpg->format.height;

	if (!enable) {
		/* Stopping the TPG without resetting it confuses the VDMA and
		 * results in VDMA errors the next time the stream is started.
		 * Reset the TPG when stopping the stream for now.
		 */
		xvip_write(&xtpg->xvip, XVIP_CTRL_CONTROL,
			   XVIP_CTRL_CONTROL_SW_RESET);
		xvip_write(&xtpg->xvip, XVIP_CTRL_CONTROL, 0);
		return 0;
	}

	xvip_write(&xtpg->xvip, XVIP_ACTIVE_SIZE,
		   (height << XVIP_ACTIVE_VSIZE_SHIFT) |
		   (width << XVIP_ACTIVE_HSIZE_SHIFT));

	xvip_write(&xtpg->xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_SW_ENABLE |
		   XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static struct v4l2_mbus_framefmt *
__xtpg_get_pad_format(struct xtpg_device *xtpg, struct v4l2_subdev_fh *fh,
		      unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xtpg->format;
	default:
		return NULL;
	}
}

static int xtpg_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xtpg_device *xtpg = to_tpg(subdev);

	fmt->format = *__xtpg_get_pad_format(xtpg, fh, fmt->pad, fmt->which);

	return 0;
}

static int xtpg_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xtpg_get_pad_format(xtpg, fh, fmt->pad, fmt->which);

	xvip_set_format_size(__format, fmt);

	fmt->format = *__format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static int xtpg_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xtpg_device *xtpg = to_tpg(subdev);

	*v4l2_subdev_get_try_format(fh, 0) = xtpg->default_format;

	return 0;
}

static int xtpg_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xtpg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xtpg_device *xtpg = container_of(ctrl->handler,
						struct xtpg_device,
						ctrl_handler);
	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		xvip_clr_and_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				 XTPG_PATTERN_MASK, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIRS:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_CROSS_HAIRS, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_MOVING_BOX:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_MOVING_BOX, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_COLOR_MASK:
		xvip_clr_and_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				 XTPG_PATTERN_CONTROL_COLOR_MASK_MASK,
				 ctrl->val <<
				 XTPG_PATTERN_CONTROL_COLOR_MASK_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_STUCK_PIXEL:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_STUCK_PIXEL, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_NOISE:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_NOISE, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_MOTION:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_MOTION, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_MOTION_SPEED:
		xvip_write(&xtpg->xvip, XTPG_MOTION_SPEED, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW:
		xvip_clr_and_set(&xtpg->xvip, XTPG_CROSS_HAIRS,
				 XTPG_CROSS_HAIRS_ROW_MASK,
				 ctrl->val << XTPG_CROSS_HAIRS_ROW_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN:
		xvip_clr_and_set(&xtpg->xvip, XTPG_CROSS_HAIRS,
				 XTPG_CROSS_HAIRS_COLUMN_MASK,
				 ctrl->val << XTPG_CROSS_HAIRS_COLUMN_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_HOR_START:
		xvip_clr_and_set(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL,
				 XTPG_ZPLATE_START_MASK,
				 ctrl->val << XTPG_ZPLATE_START_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED:
		xvip_clr_and_set(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL,
				 XTPG_ZPLATE_SPEED_MASK,
				 ctrl->val << XTPG_ZPLATE_SPEED_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_VER_START:
		xvip_clr_and_set(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL,
				 XTPG_ZPLATE_START_MASK,
				 ctrl->val << XTPG_ZPLATE_START_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED:
		xvip_clr_and_set(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL,
				 XTPG_ZPLATE_SPEED_MASK,
				 ctrl->val << XTPG_ZPLATE_SPEED_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_BOX_SIZE:
		xvip_write(&xtpg->xvip, XTPG_BOX_SIZE, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_BOX_COLOR:
		xvip_write(&xtpg->xvip, XTPG_BOX_COLOR, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH:
		xvip_write(&xtpg->xvip, XTPG_STUCK_PIXEL_THRESH, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_NOISE_GAIN:
		xvip_write(&xtpg->xvip, XTPG_NOISE_GAIN, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xtpg_ctrl_ops = {
	.s_ctrl	= xtpg_s_ctrl,
};

static struct v4l2_subdev_core_ops xtpg_core_ops = {
};

static struct v4l2_subdev_video_ops xtpg_video_ops = {
	.s_stream = xtpg_s_stream,
};

static struct v4l2_subdev_pad_ops xtpg_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xvip_enum_frame_size,
	.get_fmt		= xtpg_get_format,
	.set_fmt		= xtpg_set_format,
};

static struct v4l2_subdev_ops xtpg_ops = {
	.core   = &xtpg_core_ops,
	.video  = &xtpg_video_ops,
	.pad    = &xtpg_pad_ops,
};

static const struct v4l2_subdev_internal_ops xtpg_internal_ops = {
	.open	= xtpg_open,
	.close	= xtpg_close,
};

/*
 * Control Config
 */

static const char *const xtpg_pattern_strings[] = {
	"Passthrough",
	"Horizontal Ramp",
	"Vertical Ramp",
	"Temporal Ramp",
	"Solid Red",
	"Solid Green",
	"Solid Blue",
	"Solid Black",
	"Solid White",
	"Color Bars",
	"Zone Plate",
	"Tartan Color Bars",
	"Cross Hatch",
	"None",
	"Vertical/Horizontal Ramps",
	"Black/White Checker Board",
};

static struct v4l2_ctrl_config xtpg_ctrls[] = {
	{
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_CROSS_HAIRS,
		.name	= "Test Pattern: Cross Hairs",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_MOVING_BOX,
		.name	= "Test Pattern: Moving Box",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_COLOR_MASK,
		.name	= "Test Pattern: Color Mask",
		.type	= V4L2_CTRL_TYPE_BITMASK,
		.min	= 0,
		.max	= 0xf,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_STUCK_PIXEL,
		.name	= "Test Pattern: Stuck Pixel",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_NOISE,
		.name	= "Test Pattern: Noise",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_MOTION,
		.name	= "Test Pattern: Motion",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_MOTION_SPEED,
		.name	= "Test Pattern: Motion Speed",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 8) - 1,
		.step	= 1,
		.def	= 4,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW,
		.name	= "Test Pattern: Cross Hairs Row",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 12) - 1,
		.step	= 1,
		.def	= 0x64,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN,
		.name	= "Test Pattern: Cross Hairs Column",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 12) - 1,
		.step	= 1,
		.def	= 0x64,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_ZPLATE_HOR_START,
		.name	= "Test Pattern: Zplate Horizontal Start Pos",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 0x1e,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED,
		.name	= "Test Pattern: Zplate Horizontal Speed",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 0,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_ZPLATE_VER_START,
		.name	= "Test Pattern: Zplate Vertical Start Pos",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 1,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED,
		.name	= "Test Pattern: Zplate Vertical Speed",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 0,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_BOX_SIZE,
		.name	= "Test Pattern: Box Size",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 12) - 1,
		.step	= 1,
		.def	= 0x32,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_BOX_COLOR,
		.name	= "Test Pattern: Box Color(RGB)",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 24) - 1,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH,
		.name	= "Test Pattern: Stuck Pixel threshhold",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 0,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_NOISE_GAIN,
		.name	= "Test Pattern: Noise Gain",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 8) - 1,
		.step	= 1,
		.def	= 0,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	},
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xtpg_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xtpg_parse_of(struct xtpg_device *xtpg)
{
	struct device_node *node = xtpg->xvip.dev->of_node;

	xtpg->vip_format = xvip_of_get_format(node);
	if (IS_ERR(xtpg->vip_format)) {
		dev_err(xtpg->xvip.dev, "invalid format in DT");
		return PTR_ERR(xtpg->vip_format);
	}

	return 0;
}

static int xtpg_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xtpg_device *xtpg;
	struct resource *res;
	u32 version, i;
	int ret;

	xtpg = devm_kzalloc(&pdev->dev, sizeof(*xtpg), GFP_KERNEL);
	if (!xtpg)
		return -ENOMEM;

	xtpg->xvip.dev = &pdev->dev;

	ret = xtpg_parse_of(xtpg);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xtpg->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (xtpg->xvip.iomem == NULL)
		return -ENODEV;

	/* Reset and initialize the core */
	xvip_reset(&xtpg->xvip);

	/* Initialize the default format */
	xtpg->default_format.code = xtpg->vip_format->code;
	xtpg->default_format.field = V4L2_FIELD_NONE;
	xtpg->default_format.colorspace = V4L2_COLORSPACE_SRGB;
	xvip_get_frame_size(&xtpg->xvip, &xtpg->default_format);

	xtpg->format = xtpg->default_format;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xtpg->xvip.subdev;
	v4l2_subdev_init(subdev, &xtpg_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xtpg_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xtpg);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xtpg->pad.flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xtpg_media_ops;
	ret = media_entity_init(&subdev->entity, 1, &xtpg->pad, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xtpg->ctrl_handler, 18);

	v4l2_ctrl_new_std_menu_items(&xtpg->ctrl_handler, &xtpg_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(xtpg_pattern_strings) - 1,
				     1, 1, xtpg_pattern_strings);

	for (i = 0; i < ARRAY_SIZE(xtpg_ctrls); i++)
		v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_ctrls[i], NULL);

	if (xtpg->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xtpg->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xtpg->ctrl_handler;

	ret = v4l2_ctrl_handler_setup(&xtpg->ctrl_handler);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to set controls\n");
		goto error;
	}

	platform_set_drvdata(pdev, xtpg);

	version = xvip_read(&xtpg->xvip, XVIP_CTRL_VERSION);

	dev_info(&pdev->dev, "device found, version %u.%02x%x\n",
		 ((version & XVIP_CTRL_VERSION_MAJOR_MASK) >>
		  XVIP_CTRL_VERSION_MAJOR_SHIFT),
		 ((version & XVIP_CTRL_VERSION_MINOR_MASK) >>
		  XVIP_CTRL_VERSION_MINOR_SHIFT),
		 ((version & XVIP_CTRL_VERSION_REVISION_MASK) >>
		  XVIP_CTRL_VERSION_REVISION_SHIFT));

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	return 0;

error:
	v4l2_ctrl_handler_free(&xtpg->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xtpg_remove(struct platform_device *pdev)
{
	struct xtpg_device *xtpg = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xtpg->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xtpg->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct of_device_id xtpg_of_id_table[] = {
	{ .compatible = "xlnx,axi-tpg-5.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, xtpg_of_id_table);

static struct platform_driver xtpg_driver = {
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xilinx-axi-tpg",
		.of_match_table	= of_match_ptr(xtpg_of_id_table),
	},
	.probe			= xtpg_probe,
	.remove			= xtpg_remove,
};

module_platform_driver(xtpg_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Test Pattern Generator Driver");
MODULE_LICENSE("GPL v2");

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

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XTPG_MIN_WIDTH				32
#define XTPG_DEF_WIDTH				1920
#define XTPG_MAX_WIDTH				7680
#define XTPG_MIN_HEIGHT				32
#define XTPG_DEF_HEIGHT				1080
#define XTPG_MAX_HEIGHT				7680

#define XTPG_CTRL_STATUS_SLAVE_ERROR		(1 << 16)
#define XTPG_CTRL_IRQ_SLAVE_ERROR		(1 << 16)

#define XTPG_PATTERN_CONTROL			0x0100
#define XTPG_MOTION_SPEED			0x0104
#define XTPG_CROSS_HAIRS			0x0108
#define XTPG_ZPLATE_HOR_CONTROL			0x010c
#define XTPG_ZPLATE_VER_CONTROL			0x0110
#define XTPG_BOX_SIZE				0x0114
#define XTPG_BOX_COLOR				0x0118
#define XTPG_STUCK_PIXEL_THRESH			0x011c
#define XTPG_NOISE_GAIN				0x0120

/**
 * struct xtpg_device - Xilinx Test Pattern Generator device structure
 * @pad: media pad
 * @xvip: Xilinx Video IP device
 * @format: active V4L2 media bus format at the source pad
 * @vip_format: format information corresponding to the active format
 */
struct xtpg_device {
	struct xvip_device xvip;
	struct media_pad pad;

	struct v4l2_mbus_framefmt format;
	const struct xvip_video_format *vip_format;
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

	xvip_write(&xtpg->xvip, XTPG_PATTERN_CONTROL, 0x00001029);
	xvip_write(&xtpg->xvip, XTPG_MOTION_SPEED, 1);
	xvip_write(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL, (74 * 1920) / width);
	xvip_write(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL, (3 * 1080) / height);
	xvip_write(&xtpg->xvip, XTPG_BOX_SIZE, (112 * height) / 1080);
	xvip_write(&xtpg->xvip, XTPG_BOX_COLOR, 0x76543200);

	xvip_write(&xtpg->xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_SW_ENABLE |
		   XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int xtpg_enum_mbus_code(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_fh *fh,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	struct xtpg_device *xtpg = to_tpg(subdev);

	if (code->index)
		return -EINVAL;

	code->code = xtpg->vip_format->code;

	return 0;
}

static int xtpg_enum_frame_size(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_fh *fh,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	struct xtpg_device *xtpg = to_tpg(subdev);

	if (fse->index || fse->code != xtpg->vip_format->code)
		return -EINVAL;

	fse->min_width = XTPG_MIN_WIDTH;
	fse->max_width = XTPG_MAX_WIDTH;
	fse->min_height = XTPG_MIN_HEIGHT;
	fse->max_height = XTPG_MAX_HEIGHT;

	return 0;
}

static struct v4l2_mbus_framefmt *
__xtpg_get_pad_format(struct xtpg_device *xtpg,
			    struct v4l2_subdev_fh *fh,
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

	fmt->format =
		*__xtpg_get_pad_format(xtpg, fh, fmt->pad, fmt->which);

	return 0;
}

static int xtpg_set_format(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_format *format)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xtpg_get_pad_format(xtpg, fh, format->pad,
					       format->which);
	__format->width = clamp_t(unsigned int, format->format.width,
				  XTPG_MIN_WIDTH, XTPG_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, format->format.height,
				   XTPG_MIN_HEIGHT, XTPG_MAX_HEIGHT);

	format->format = *__format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static int xtpg_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, 0);

	format->code = xtpg->vip_format->code;
	format->width = XTPG_DEF_WIDTH;
	format->height = XTPG_DEF_HEIGHT;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int xtpg_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_core_ops xtpg_core_ops = {
};

static struct v4l2_subdev_video_ops xtpg_video_ops = {
	.s_stream = xtpg_s_stream,
};

static struct v4l2_subdev_pad_ops xtpg_pad_ops = {
	.enum_mbus_code = xtpg_enum_mbus_code,
	.enum_frame_size = xtpg_enum_frame_size,
	.get_fmt = xtpg_get_format,
	.set_fmt = xtpg_set_format,
};

static struct v4l2_subdev_ops xtpg_ops = {
	.core   = &xtpg_core_ops,
	.video  = &xtpg_video_ops,
	.pad    = &xtpg_pad_ops,
};

static const struct v4l2_subdev_internal_ops xtpg_internal_ops = {
	.open = xtpg_open,
	.close = xtpg_close,
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
	u32 version;
	int ret;

	xtpg = devm_kzalloc(&pdev->dev, sizeof(*xtpg), GFP_KERNEL);
	if (!xtpg)
		return -ENOMEM;

	xtpg->xvip.dev = &pdev->dev;

	ret = xtpg_parse_of(xtpg);
	if (ret < 0)
		return ret;

	xtpg->format.code = xtpg->vip_format->code;
	xtpg->format.width = XTPG_DEF_WIDTH;
	xtpg->format.height = XTPG_DEF_HEIGHT;
	xtpg->format.field = V4L2_FIELD_NONE;
	xtpg->format.colorspace = V4L2_COLORSPACE_SRGB;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xtpg->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (xtpg->xvip.iomem == NULL)
		return -ENODEV;

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
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xtpg_remove(struct platform_device *pdev)
{
	struct xtpg_device *xtpg = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xtpg->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct of_device_id xtpg_of_id_table[] = {
	{ .compatible = "xlnx,axi-tpg" },
	{ }
};
MODULE_DEVICE_TABLE(of, xtpg_of_id_table);

static struct platform_driver xtpg_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "xilinx-axi-tpg",
		.of_match_table = of_match_ptr(xtpg_of_id_table),
	},
	.probe = xtpg_probe,
	.remove = xtpg_remove,
};

module_platform_driver(xtpg_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Test Pattern Generator Driver");
MODULE_LICENSE("GPL v2");

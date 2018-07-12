/*
 * Regulus No Operation Branch Device Driver
 *
 * Based on the xilinx video drivers (xilinx-switch.c)
 *
 * Copyright (C) 2016 Regulus co.,ltd.
 * Contacts: Yuta Hasegawa <hasegawa@reglus.co.jp>
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


/**
 * struct rbranch_device - Regulus NOP branch device structure
 * @subdev:
 * @dev:
 * @pads: media pads
 * @nsinks: number of sink pads (2 to 8)
 * @nsources: number of source pads (1 to 8)
 */
struct rbranch_device {
	struct v4l2_subdev subdev;
	struct device *dev;

	struct media_pad *pads;
	unsigned int nsinks;
	unsigned int nsources;
};


/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int rbrc_s_stream(struct v4l2_subdev *subdev, int enable)
{
	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int rbrc_get_format(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
{
	memset(&fmt->format, 0, sizeof(fmt->format));
	return 0;
}

static int rbrc_set_format(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
{
	return 0;
}

int rbrc_enum_mbus_code(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	code->code = MEDIA_BUS_FMT_YUYV8_2X8;

	return 0;
}

int rbrc_enum_frame_size(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_frame_size_enum *fse)
{
	fse->min_width = 1920;
	fse->max_width = 1920;
	fse->min_height = 960;
	fse->max_height = 960;

	return 0;
}


/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static int rbrc_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int rbrc_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_video_ops rbrc_video_ops = {
	.s_stream = rbrc_s_stream,
};

static struct v4l2_subdev_pad_ops rbrc_pad_ops = {
	.enum_mbus_code = rbrc_enum_mbus_code,
	.enum_frame_size = rbrc_enum_frame_size,
	.get_fmt = rbrc_get_format,
	.set_fmt = rbrc_set_format,
};

static struct v4l2_subdev_ops rbrc_ops = {
	.video = &rbrc_video_ops,
	.pad = &rbrc_pad_ops,
};

static const struct v4l2_subdev_internal_ops rbrc_internal_ops = {
	.open = rbrc_open,
	.close = rbrc_close,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations rbrc_media_ops = {
	//.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int rbrc_parse_of(struct rbranch_device *rbrc)
{
	struct device_node *node = rbrc->dev->of_node;
	int ret;

	ret = of_property_read_u32(node, "#rgls,inputs", &rbrc->nsinks);
	if (ret < 0) {
		dev_err(rbrc->dev, "missing or invalid #rgls,%s property\n",
			"inputs");
		return ret;
	}

	ret = of_property_read_u32(node, "#rgls,outputs", &rbrc->nsources);
	if (ret < 0) {
		dev_err(rbrc->dev, "missing or invalid #rgls,%s property\n",
			"outputs");
		return ret;
	}

	return 0;
}

static int rbrc_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct rbranch_device *rbrc;
	unsigned int npads;
	unsigned int i;
	int ret;

	rbrc = devm_kzalloc(&pdev->dev, sizeof(*rbrc), GFP_KERNEL);
	if (!rbrc)
		return -ENOMEM;

	rbrc->dev = &pdev->dev;


	ret = rbrc_parse_of(rbrc);
	if (ret < 0)
		return ret;

	/* Initialize V4L2 subdevice and media entity. Pad numbers depend on the
	 * number of pads.
	 */
	npads = rbrc->nsinks + rbrc->nsources;
	rbrc->pads = devm_kzalloc(&pdev->dev, npads * sizeof(*rbrc->pads),
				 GFP_KERNEL);
	if (!rbrc->pads)
		goto error;

	for (i = 0; i < rbrc->nsinks; ++i)
		rbrc->pads[i].flags = MEDIA_PAD_FL_SINK;

	for (; i < npads; ++i)
		rbrc->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	subdev = &rbrc->subdev;
	v4l2_subdev_init(subdev, &rbrc_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &rbrc_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, rbrc);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.ops = &rbrc_media_ops;

	ret = media_entity_pads_init(&subdev->entity, npads, rbrc->pads);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, rbrc);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	dev_err(&pdev->dev, "Regulus NOP Branch Probed.\n");

	return 0;

error:
	dev_err(&pdev->dev, "probe err 0\n");
	media_entity_cleanup(&subdev->entity);
	dev_err(&pdev->dev, "probe err 1\n");
	return ret;
}

static int rbrc_remove(struct platform_device *pdev)
{
	struct rbranch_device *rbrc = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &rbrc->subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);


	return 0;
}

static const struct of_device_id rbrc_of_id_table[] = {
	{ .compatible = "rgls,branch" },
	{ .compatible = "rgls,branch-1.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, rbrc_of_id_table);

static struct platform_driver rbrc_driver = {
	.driver = {
		.name		= "reglus_branch",
		.of_match_table	= rbrc_of_id_table,
	},
	.probe			= rbrc_probe,
	.remove			= rbrc_remove,
};

module_platform_driver(rbrc_driver);

MODULE_AUTHOR("Yuta Hasegawa <hasegawa@reglus.co.jp>");
MODULE_DESCRIPTION("Regulus NOP Branch Driver");
MODULE_LICENSE("GPL v2");

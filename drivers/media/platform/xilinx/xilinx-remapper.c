/*
 * Xilinx Video Remapper
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

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XREMAP_MIN_WIDTH			1
#define XREMAP_DEF_WIDTH			1920
#define XREMAP_MAX_WIDTH			65535
#define XREMAP_MIN_HEIGHT			1
#define XREMAP_DEF_HEIGHT			1080
#define XREMAP_MAX_HEIGHT			65535

#define XREMAP_PAD_SINK				0
#define XREMAP_PAD_SOURCE			1

/**
 * struct xremap_mapping_output - Output format description
 * @code: media bus pixel core after remapping
 * @num_components: number of pixel components after remapping
 * @component_maps: configuration array corresponding to this output
 */
struct xremap_mapping_output {
	u32 code;
	unsigned int num_components;
	unsigned int component_maps[4];
};

/**
 * struct xremap_mapping - Input-output remapping description
 * @code: media bus pixel code before remapping
 * @width: video bus width in bits
 * @num_components: number of pixel components before remapping
 * @outputs: array of possible output formats
 */
struct xremap_mapping {
	u32 code;
	unsigned int width;
	unsigned int num_components;
	const struct xremap_mapping_output *outputs;
};

/**
 * struct xremap_device - Xilinx Test Pattern Generator device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @formats: V4L2 media bus formats at the sink and source pads
 * @config: device configuration parsed from its DT node
 * @config.width: video bus width in bits
 * @config.num_s_components: number of pixel components at the input
 * @config.num_m_components: number of pixel components at the output
 * @config.component_maps: component remapping configuration
 * @default_mapping: Default mapping compatible with the configuration
 * @default_output: Default output format for the default mapping
 */
struct xremap_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];

	struct {
		unsigned int width;
		unsigned int num_s_components;
		unsigned int num_m_components;
		unsigned int component_maps[4];
	} config;

	const struct xremap_mapping *default_mapping;
	const struct xremap_mapping_output *default_output;
};

static inline struct xremap_device *to_remap(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xremap_device, xvip.subdev);
}

/* -----------------------------------------------------------------------------
 * Mappings
 */

static const struct xremap_mapping xremap_mappings[] = {
	{
		.code = MEDIA_BUS_FMT_RBG888_1X24,
		.width = 8,
		.num_components = 3,
		.outputs = (const struct xremap_mapping_output[]) {
			{ MEDIA_BUS_FMT_RGB888_1X32_PADHI, 4, { 1, 0, 2, 4 } },
			{ },
		},
	},
};

static const struct xremap_mapping_output *
xremap_match_mapping(struct xremap_device *xremap,
		     const struct xremap_mapping *mapping)
{
	const struct xremap_mapping_output *output;

	if (mapping->width != xremap->config.width ||
	    mapping->num_components != xremap->config.num_s_components)
		return NULL;

	for (output = mapping->outputs; output->code; ++output) {
		unsigned int i;

		if (output->num_components != xremap->config.num_m_components)
			continue;

		for (i = 0; i < output->num_components; ++i) {
			if (output->component_maps[i] !=
			    xremap->config.component_maps[i])
				break;
		}

		if (i == output->num_components)
			return output;
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int xremap_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct xremap_device *xremap = to_remap(subdev);
	struct v4l2_mbus_framefmt *format;

	if (code->pad == XREMAP_PAD_SINK) {
		const struct xremap_mapping *mapping = NULL;
		unsigned int index = code->index + 1;
		unsigned int i;

		/* Iterate through the mappings and skip the ones that don't
		 * match the remapper configuration until we reach the requested
		 * index.
		 */
		for (i = 0; i < ARRAY_SIZE(xremap_mappings) && index; ++i) {
			mapping = &xremap_mappings[i];

			if (xremap_match_mapping(xremap, mapping))
				index--;
		}

		/* If the index was larger than the number of supported mappings
		 * return -EINVAL.
		 */
		if (index > 0)
			return -EINVAL;

		code->code = mapping->code;
	} else {
		if (code->index)
			return -EINVAL;

		format = v4l2_subdev_get_try_format(subdev, cfg, code->pad);
		code->code = format->code;
	}

	return 0;
}

static int xremap_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, cfg, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == XREMAP_PAD_SINK) {
		/* The remapper doesn't restrict the size on the sink pad. */
		fse->min_width = XREMAP_MIN_WIDTH;
		fse->max_width = XREMAP_MAX_WIDTH;
		fse->min_height = XREMAP_MIN_HEIGHT;
		fse->max_height = XREMAP_MAX_HEIGHT;
	} else {
		/* The size on the source pad are fixed and always identical to
		 * the size on the sink pad.
		 */
		fse->min_width = format->width;
		fse->max_width = format->width;
		fse->min_height = format->height;
		fse->max_height = format->height;
	}

	return 0;
}

static struct v4l2_mbus_framefmt *
xremap_get_pad_format(struct xremap_device *xremap,
		      struct v4l2_subdev_pad_config *cfg,
		      unsigned int pad, u32 which)
{
	struct v4l2_mbus_framefmt *format;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		format = v4l2_subdev_get_try_format(&xremap->xvip.subdev, cfg,
						    pad);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		format = &xremap->formats[pad];
		break;
	default:
		format = NULL;
		break;
	}

	return format;
}

static int xremap_get_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct xremap_device *xremap = to_remap(subdev);
	struct v4l2_mbus_framefmt *format;

	format = xremap_get_pad_format(xremap, cfg, fmt->pad, fmt->which);
	if (!format)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int xremap_set_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct xremap_device *xremap = to_remap(subdev);
	const struct xremap_mapping_output *output = NULL;
	const struct xremap_mapping *mapping;
	struct v4l2_mbus_framefmt *format;
	unsigned int i;

	format = xremap_get_pad_format(xremap, cfg, fmt->pad, fmt->which);
	if (!format)
		return -EINVAL;

	if (fmt->pad == XREMAP_PAD_SOURCE) {
		fmt->format = *format;
		return 0;
	}

	/* Find the mapping. If the requested format has no mapping, use the
	 * default.
	 */
	for (i = 0; i < ARRAY_SIZE(xremap_mappings); ++i) {
		mapping = &xremap_mappings[i];
		if (mapping->code != fmt->format.code)
			continue;

		output = xremap_match_mapping(xremap, mapping);
		if (output)
			break;
	}

	if (!output) {
		mapping = xremap->default_mapping;
		output = xremap->default_output;
	}

	format->code = mapping->code;
	format->width = clamp_t(unsigned int, fmt->format.width,
				XREMAP_MIN_WIDTH, XREMAP_MAX_WIDTH);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 XREMAP_MIN_HEIGHT, XREMAP_MAX_HEIGHT);
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *format;

	/* Propagate the format to the source pad. */
	format = xremap_get_pad_format(xremap, cfg, XREMAP_PAD_SOURCE,
				       fmt->which);
	if (!format)
		return -EINVAL;
	*format = fmt->format;
	format->code = output->code;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/*
 * xremap_init_formats - Initialize formats on all pads
 * @subdev: remapper V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xremap_init_formats(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh)
{
	struct xremap_device *xremap = to_remap(subdev);
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));

	format.pad = XREMAP_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = xremap->default_mapping->code;
	format.format.width = XREMAP_DEF_WIDTH;
	format.format.height = XREMAP_DEF_HEIGHT;

	xremap_set_format(subdev, fh ? fh->pad : NULL, &format);
}

static int xremap_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xremap_init_formats(subdev, fh);

	return 0;
}

static int xremap_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_core_ops xremap_core_ops = {
};

static struct v4l2_subdev_video_ops xremap_video_ops = {
};

static struct v4l2_subdev_pad_ops xremap_pad_ops = {
	.enum_mbus_code = xremap_enum_mbus_code,
	.enum_frame_size = xremap_enum_frame_size,
	.get_fmt = xremap_get_format,
	.set_fmt = xremap_set_format,
};

static struct v4l2_subdev_ops xremap_ops = {
	.core   = &xremap_core_ops,
	.video  = &xremap_video_ops,
	.pad    = &xremap_pad_ops,
};

static const struct v4l2_subdev_internal_ops xremap_internal_ops = {
	.open = xremap_open,
	.close = xremap_close,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xremap_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xremap_parse_of(struct xremap_device *xremap)
{
	struct device_node *node = xremap->xvip.dev->of_node;
	unsigned int i;
	int ret;

	/* Parse the DT properties. */
	ret = of_property_read_u32(node, "xlnx,video-width",
				   &xremap->config.width);
	if (ret < 0) {
		dev_dbg(xremap->xvip.dev, "unable to parse %s property\n",
			"xlnx,video-width");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "#xlnx,s-components",
				   &xremap->config.num_s_components);
	if (ret < 0) {
		dev_dbg(xremap->xvip.dev, "unable to parse %s property\n",
			"#xlnx,s-components");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "#xlnx,m-components",
				   &xremap->config.num_m_components);
	if (ret < 0) {
		dev_dbg(xremap->xvip.dev, "unable to parse %s property\n",
			"#xlnx,m-components");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(node, "xlnx,component-maps",
					 xremap->config.component_maps,
					 xremap->config.num_m_components);
	if (ret < 0) {
		dev_dbg(xremap->xvip.dev, "unable to parse %s property\n",
			"xlnx,component-maps");
		return -EINVAL;
	}

	/* Validate the parsed values. */
	if (xremap->config.num_s_components > 4 ||
	    xremap->config.num_m_components > 4) {
		dev_dbg(xremap->xvip.dev,
			"invalid number of components (s %u m %u)\n",
			xremap->config.num_s_components,
			xremap->config.num_m_components);
		return -EINVAL;
	}

	for (i = 0; i < xremap->config.num_m_components; ++i) {
		if (xremap->config.component_maps[i] > 4) {
			dev_dbg(xremap->xvip.dev, "invalid map %u @%u\n",
				xremap->config.component_maps[i], i);
			return -EINVAL;
		}
	}

	/* Find the first mapping that matches the remapper configuration and
	 * store it as the default mapping.
	 */
	for (i = 0; i < ARRAY_SIZE(xremap_mappings); ++i) {
		const struct xremap_mapping_output *output;
		const struct xremap_mapping *mapping;

		mapping = &xremap_mappings[i];
		output = xremap_match_mapping(xremap, mapping);

		if (output) {
			xremap->default_mapping = mapping;
			xremap->default_output = output;
			return 0;
		}
	}

	dev_err(xremap->xvip.dev,
		"No format compatible with device configuration\n");

	return -EINVAL;
}

static int xremap_probe(struct platform_device *pdev)
{
	struct xremap_device *xremap;
	struct v4l2_subdev *subdev;
	int ret;

	xremap = devm_kzalloc(&pdev->dev, sizeof(*xremap), GFP_KERNEL);
	if (!xremap)
		return -ENOMEM;

	xremap->xvip.dev = &pdev->dev;

	ret = xremap_parse_of(xremap);
	if (ret < 0)
		return ret;

	xremap->xvip.clk = devm_clk_get(xremap->xvip.dev, NULL);
	if (IS_ERR(xremap->xvip.clk))
		return PTR_ERR(xremap->xvip.clk);

	ret = clk_prepare_enable(xremap->xvip.clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clk (%d)\n", ret);
		return ret;
	}

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xremap->xvip.subdev;
	v4l2_subdev_init(subdev, &xremap_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xremap_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xremap);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xremap_init_formats(subdev, NULL);

	xremap->pads[XREMAP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xremap->pads[XREMAP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xremap_media_ops;
	ret = media_entity_pads_init(&subdev->entity, 2, xremap->pads);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, xremap);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	dev_info(&pdev->dev, "device registered\n");

	return 0;

error:
	media_entity_cleanup(&subdev->entity);
	clk_disable_unprepare(xremap->xvip.clk);
	return ret;
}

static int xremap_remove(struct platform_device *pdev)
{
	struct xremap_device *xremap = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xremap->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	clk_disable_unprepare(xremap->xvip.clk);

	return 0;
}

static const struct of_device_id xremap_of_id_table[] = {
	{ .compatible = "xlnx,v-remapper" },
	{ }
};
MODULE_DEVICE_TABLE(of, xremap_of_id_table);

static struct platform_driver xremap_driver = {
	.driver = {
		.name = "xilinx-remapper",
		.of_match_table = xremap_of_id_table,
	},
	.probe = xremap_probe,
	.remove = xremap_remove,
};

module_platform_driver(xremap_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Video Remapper Driver");
MODULE_LICENSE("GPL v2");

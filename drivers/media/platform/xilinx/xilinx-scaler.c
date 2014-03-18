/*
 * Xilinx Scaler
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/fixp-arith.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XSCALER_MIN_WIDTH			32
#define XSCALER_MAX_WIDTH			4096
#define XSCALER_MIN_HEIGHT			32
#define XSCALER_MAX_HEIGHT			4096

#define XSCALER_HSF				0x0100
#define XSCALER_VSF				0x0104
#define XSCALER_SF_SHIFT			20
#define XSCALER_SF_MASK				0xffffff
#define XSCALER_SOURCE_SIZE			0x0108
#define XSCALER_SIZE_HORZ_SHIFT			0
#define XSCALER_SIZE_VERT_SHIFT			16
#define XSCALER_SIZE_MASK			0xfff
#define XSCALER_HAPERTURE			0x010c
#define XSCALER_VAPERTURE			0x0110
#define XSCALER_APERTURE_START_SHIFT		0
#define XSCALER_APERTURE_END_SHIFT		16
#define XSCALER_OUTPUT_SIZE			0x0114
#define XSCALER_COEF_DATA_IN			0x0134
#define XSCALER_COEF_DATA_IN_SHIFT		16

/* Fixed point operations */
#define FRAC_N	8

static inline s16 fixp_new(s16 a)
{
	return a << FRAC_N;
}

static inline s16 fixp_mult(s16 a, s16 b)
{
	return ((s32)(a * b)) >> FRAC_N;
}

/**
 * struct xscaler_device - Xilinx Scaler device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @formats: V4L2 media bus formats at the sink and source pads
 * @default_formats: default V4L2 media bus formats
 * @vip_format: Xilinx Video IP format
 * @crop: Active crop rectangle for the sink pad
 * @num_hori_taps: number of vertical taps
 * @num_vert_taps: number of vertical taps
 * @max_num_phases: maximum number of phases
 * @separate_yc_coef: separate coefficients for Luma(y) and Chroma(c)
 * @separate_hv_coef: separate coefficients for Horizontal(h) and Vertical(v)
 */
struct xscaler_device {
	struct xvip_device xvip;

	struct media_pad pads[2];

	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_formats[2];
	const struct xvip_video_format *vip_format;
	struct v4l2_rect crop;

	u32 num_hori_taps;
	u32 num_vert_taps;
	u32 max_num_phases;
	bool separate_yc_coef;
	bool separate_hv_coef;
};

static inline struct xscaler_device *to_scaler(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xscaler_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

/**
 * lanczos - Lanczos 2D FIR kernel convolution
 * @x: phase
 * @a: Lanczos kernel size
 *
 * Return: the coefficient value in fixed point format.
 */
static s16 lanczos(s16 x, s16 a)
{
	s16 pi;
	s16 numerator;
	s16 denominator;
	s16 temp;

	if (x < -a || x > a)
		return 0;
	else if (x == 0)
		return fixp_new(1);

	/* a * sin(pi * x) * sin(pi * x / a) / (pi * pi * x * x) */

	pi = (fixp_new(157) << FRAC_N) / fixp_new(50);

	if (x < 0)
		x = -x;

	/* sin(pi * x) */
	temp = fixp_mult(fixp_new(180), x);
	temp = fixp_sin16(temp >> FRAC_N);

	/* a * sin(pi * x) */
	numerator = fixp_mult(temp, a);

	/* sin(pi * x / a) */
	temp = (fixp_mult(fixp_new(180), x) << FRAC_N) / a;
	temp = fixp_sin16(temp >> FRAC_N);

	/* a * sin(pi * x) * sin(pi * x / a) */
	numerator = fixp_mult(temp, numerator);

	/* pi * pi * x * x */
	denominator = fixp_mult(pi, pi);
	temp = fixp_mult(x, x);
	denominator = fixp_mult(temp, denominator);

	return (numerator << FRAC_N) / denominator;
}

/**
 * xscaler_set_coefs - generate and program the coefficient table
 * @xscaler: scaler device
 * @taps: maximum coefficient tap index
 *
 * Generate the coefficient table using Lanczos resampling, and program
 * generated coefficients to the scaler. The generated coefficients are
 * supposed to work regardless of resolutions.
 *
 * Return: 0 if the coefficient table is programmed, and -ENOMEM if memory
 * allocation for the table fails.
 */
static int xscaler_set_coefs(struct xscaler_device *xscaler, s16 taps)
{
	s16 *coef;
	s16 dy;
	u32 coef_val;
	u16 phases = xscaler->max_num_phases;
	u16 i;
	u16 j;

	coef = kcalloc(phases, sizeof(*coef), GFP_KERNEL);
	if (!coef)
		return -ENOMEM;

	for (i = 0; i < phases; i++) {
		s16 sum = 0;

		dy = ((fixp_new(i) << FRAC_N) / fixp_new(phases));

		/* Generate Lanczos coefficients */
		for (j = 0; j < taps; j++) {
			coef[j] = lanczos(fixp_new(j - (taps >> 1)) + dy,
					  fixp_new(taps >> 1));
			sum += coef[j];
		}

		/* Program coefficients */
		for (j = 0; j < taps; j += 2) {
			/* Normalize and multiply coefficients */
			coef_val = (((coef[j] << FRAC_N) << (FRAC_N - 2)) /
				    sum) & 0xffff;
			if (j + 1 < taps)
				coef_val |= ((((coef[j + 1] << FRAC_N) <<
					      (FRAC_N - 2)) / sum) & 0xffff) <<
					    16;

			xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
				   coef_val);
		}
	}

	kfree(coef);

	return 0;
}

static void xscaler_set_aperture(struct xscaler_device *xscaler)
{
	u16 start;
	u16 end;
	u32 scale_factor;

	xvip_disable_reg_update(&xscaler->xvip);

	/* set horizontal aperture */
	start = xscaler->crop.left;
	end = start + xscaler->crop.width - 1;
	xvip_write(&xscaler->xvip, XSCALER_HAPERTURE,
		   (end << XSCALER_APERTURE_END_SHIFT) |
		   (start << XSCALER_APERTURE_START_SHIFT));

	/* set vertical aperture */
	start = xscaler->crop.top;
	end = start + xscaler->crop.height - 1;
	xvip_write(&xscaler->xvip, XSCALER_VAPERTURE,
		   (end << XSCALER_APERTURE_END_SHIFT) |
		   (start << XSCALER_APERTURE_START_SHIFT));

	/* set scaling factors */
	scale_factor = ((xscaler->crop.width << XSCALER_SF_SHIFT) /
			xscaler->formats[XVIP_PAD_SOURCE].width) &
		       XSCALER_SF_MASK;
	xvip_write(&xscaler->xvip, XSCALER_HSF, scale_factor);

	scale_factor = ((xscaler->crop.height << XSCALER_SF_SHIFT) /
			xscaler->formats[XVIP_PAD_SOURCE].height) &
		       XSCALER_SF_MASK;
	xvip_write(&xscaler->xvip, XSCALER_VSF, scale_factor);

	xvip_enable_reg_update(&xscaler->xvip);
}

static int xscaler_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	u32 width;
	u32 height;

	if (!enable) {
		xvip_stop(&xscaler->xvip);
		return 0;
	}

	/* set input width / height */
	width = xscaler->formats[XVIP_PAD_SINK].width;
	height = xscaler->formats[XVIP_PAD_SINK].height;
	xvip_write(&xscaler->xvip, XSCALER_SOURCE_SIZE,
		   (height << XSCALER_SIZE_VERT_SHIFT) |
		   (width << XSCALER_SIZE_HORZ_SHIFT));

	/* set output width / height */
	width = xscaler->formats[XVIP_PAD_SOURCE].width;
	height = xscaler->formats[XVIP_PAD_SOURCE].height;
	xvip_write(&xscaler->xvip, XSCALER_OUTPUT_SIZE,
		   (height << XSCALER_SIZE_VERT_SHIFT) |
		   (width << XSCALER_SIZE_HORZ_SHIFT));

	/* set aperture */
	xscaler_set_aperture(xscaler);

	xvip_start(&xscaler->xvip);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xscaler_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, cfg, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	fse->min_width = XSCALER_MIN_WIDTH;
	fse->max_width = XSCALER_MAX_WIDTH;
	fse->min_height = XSCALER_MIN_HEIGHT;
	fse->max_height = XSCALER_MAX_HEIGHT;

	return 0;
}

static struct v4l2_mbus_framefmt *
__xscaler_get_pad_format(struct xscaler_device *xscaler,
			 struct v4l2_subdev_pad_config *cfg,
			 unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xscaler->xvip.subdev, cfg,
						  pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xscaler->formats[pad];
	default:
		return NULL;
	}
}

static struct v4l2_rect *__xscaler_get_crop(struct xscaler_device *xscaler,
					    struct v4l2_subdev_pad_config *cfg,
					    u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&xscaler->xvip.subdev, cfg,
						XVIP_PAD_SINK);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xscaler->crop;
	default:
		return NULL;
	}
}

static int xscaler_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct xscaler_device *xscaler = to_scaler(subdev);

	fmt->format = *__xscaler_get_pad_format(xscaler, cfg, fmt->pad,
						fmt->which);

	return 0;
}

static void xscaler_try_crop(const struct v4l2_mbus_framefmt *sink,
			     struct v4l2_rect *crop)
{

	crop->left = min_t(u32, crop->left, sink->width - XSCALER_MIN_WIDTH);
	crop->top = min_t(u32, crop->top, sink->height - XSCALER_MIN_HEIGHT);
	crop->width = clamp_t(u32, crop->width, XSCALER_MIN_WIDTH,
			      sink->width - crop->left);
	crop->height = clamp_t(u32, crop->height, XSCALER_MIN_HEIGHT,
			       sink->height - crop->top);
}

static int xscaler_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	format = __xscaler_get_pad_format(xscaler, cfg, fmt->pad, fmt->which);

	format->width = clamp_t(unsigned int, fmt->format.width,
				  XSCALER_MIN_WIDTH, XSCALER_MAX_WIDTH);
	format->height = clamp_t(unsigned int, fmt->format.height,
				   XSCALER_MIN_HEIGHT, XSCALER_MAX_HEIGHT);

	fmt->format = *format;

	if (fmt->pad == XVIP_PAD_SINK) {
		/* Set the crop rectangle to the full frame */
		crop = __xscaler_get_crop(xscaler, cfg, fmt->which);
		crop->left = 0;
		crop->top = 0;
		crop->width = fmt->format.width;
		crop->height = fmt->format.height;
	}

	return 0;
}

static int xscaler_get_selection(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	struct v4l2_mbus_framefmt *format;

	if (sel->pad != XVIP_PAD_SINK)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		format = __xscaler_get_pad_format(xscaler, cfg, XVIP_PAD_SINK,
						  sel->which);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = format->width;
		sel->r.height = format->height;
		return 0;
	case V4L2_SEL_TGT_CROP:
		sel->r = *__xscaler_get_crop(xscaler, cfg, sel->which);
		return 0;
	default:
		return -EINVAL;
	}
}

static int xscaler_set_selection(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	struct v4l2_mbus_framefmt *format;

	if ((sel->target != V4L2_SEL_TGT_CROP) || (sel->pad != XVIP_PAD_SINK))
		return -EINVAL;

	format = __xscaler_get_pad_format(xscaler, cfg, XVIP_PAD_SINK,
					  sel->which);
	xscaler_try_crop(format, &sel->r);
	*__xscaler_get_crop(xscaler, cfg, sel->which) = sel->r;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

static int xscaler_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	struct v4l2_mbus_framefmt *format;

	/* Initialize with default formats */
	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SINK);
	*format = xscaler->default_formats[XVIP_PAD_SINK];

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SOURCE);
	*format = xscaler->default_formats[XVIP_PAD_SOURCE];

	return 0;
}

static int xscaler_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_video_ops xscaler_video_ops = {
	.s_stream = xscaler_s_stream,
};

static struct v4l2_subdev_pad_ops xscaler_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xscaler_enum_frame_size,
	.get_fmt		= xscaler_get_format,
	.set_fmt		= xscaler_set_format,
	.get_selection		= xscaler_get_selection,
	.set_selection		= xscaler_set_selection,
};

static struct v4l2_subdev_ops xscaler_ops = {
	.video  = &xscaler_video_ops,
	.pad    = &xscaler_pad_ops,
};

static const struct v4l2_subdev_internal_ops xscaler_internal_ops = {
	.open	= xscaler_open,
	.close	= xscaler_close,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xscaler_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

static int __maybe_unused xscaler_pm_suspend(struct device *dev)
{
	struct xscaler_device *xscaler = dev_get_drvdata(dev);

	xvip_suspend(&xscaler->xvip);

	return 0;
}

static int __maybe_unused xscaler_pm_resume(struct device *dev)
{
	struct xscaler_device *xscaler = dev_get_drvdata(dev);

	xvip_resume(&xscaler->xvip);

	return 0;
}

/*
 * Platform Device Driver
 */

static int xscaler_parse_of(struct xscaler_device *xscaler)
{
	struct device *dev = xscaler->xvip.dev;
	struct device_node *node = xscaler->xvip.dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	int ret;

	ports = of_get_child_by_name(node, "ports");
	if (ports == NULL)
		ports = node;

	/* Get the format description for each pad */
	for_each_child_of_node(ports, port) {
		if (port->name && (of_node_cmp(port->name, "port") == 0)) {
			const struct xvip_video_format *vip_format;

			vip_format = xvip_of_get_format(port);
			if (IS_ERR(vip_format)) {
				dev_err(dev, "invalid format in DT");
				return PTR_ERR(vip_format);
			}

			if (!xscaler->vip_format) {
				xscaler->vip_format = vip_format;
			} else if (xscaler->vip_format != vip_format) {
				dev_err(dev, "in/out format mismatch in DT");
				return -EINVAL;
			}
		}
	}

	ret = of_property_read_u32(node, "xlnx,num-hori-taps",
				   &xscaler->num_hori_taps);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "xlnx,num-vert-taps",
				   &xscaler->num_vert_taps);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "xlnx,max-num-phases",
				   &xscaler->max_num_phases);
	if (ret < 0)
		return ret;

	xscaler->separate_yc_coef =
		of_property_read_bool(node, "xlnx,separate-yc-coef");

	xscaler->separate_hv_coef =
		of_property_read_bool(node, "xlnx,separate-hv-coef");

	return 0;
}

static int xscaler_probe(struct platform_device *pdev)
{
	struct xscaler_device *xscaler;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *default_format;
	u32 size;
	int ret;

	xscaler = devm_kzalloc(&pdev->dev, sizeof(*xscaler), GFP_KERNEL);
	if (!xscaler)
		return -ENOMEM;

	xscaler->xvip.dev = &pdev->dev;

	ret = xscaler_parse_of(xscaler);
	if (ret < 0)
		return ret;

	ret = xvip_init_resources(&xscaler->xvip);
	if (ret < 0)
		return ret;

	/* Reset and initialize the core */
	xvip_reset(&xscaler->xvip);

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xscaler->xvip.subdev;
	v4l2_subdev_init(subdev, &xscaler_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xscaler_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xscaler);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Initialize default and active formats */
	default_format = &xscaler->default_formats[XVIP_PAD_SINK];
	default_format->code = xscaler->vip_format->code;
	default_format->field = V4L2_FIELD_NONE;
	default_format->colorspace = V4L2_COLORSPACE_SRGB;
	size = xvip_read(&xscaler->xvip, XSCALER_SOURCE_SIZE);
	default_format->width = (size >> XSCALER_SIZE_HORZ_SHIFT) &
				 XSCALER_SIZE_MASK;
	default_format->height = (size >> XSCALER_SIZE_VERT_SHIFT) &
				 XSCALER_SIZE_MASK;

	xscaler->formats[XVIP_PAD_SINK] = *default_format;

	default_format = &xscaler->default_formats[XVIP_PAD_SOURCE];
	*default_format = xscaler->default_formats[XVIP_PAD_SINK];
	size = xvip_read(&xscaler->xvip, XSCALER_OUTPUT_SIZE);
	default_format->width = (size >> XSCALER_SIZE_HORZ_SHIFT) &
				 XSCALER_SIZE_MASK;
	default_format->height = (size >> XSCALER_SIZE_VERT_SHIFT) &
				 XSCALER_SIZE_MASK;

	xscaler->formats[XVIP_PAD_SOURCE] = *default_format;

	xscaler->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xscaler->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xscaler_media_ops;

	ret = media_entity_pads_init(&subdev->entity, 2, xscaler->pads);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, xscaler);

	xvip_print_version(&xscaler->xvip);

	ret = xscaler_set_coefs(xscaler, (s16)xscaler->num_hori_taps);
	if (ret < 0)
		goto error;

	if (xscaler->separate_hv_coef) {
		ret = xscaler_set_coefs(xscaler, (s16)xscaler->num_vert_taps);
		if (ret < 0)
			goto error;
	}

	if (xscaler->separate_yc_coef) {
		ret = xscaler_set_coefs(xscaler, (s16)xscaler->num_hori_taps);
		if (ret < 0)
			goto error;

		if (xscaler->separate_hv_coef) {
			ret = xscaler_set_coefs(xscaler,
						(s16)xscaler->num_vert_taps);
			if (ret < 0)
				goto error;
		}
	}

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	return 0;

error:
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xscaler->xvip);
	return ret;
}

static int xscaler_remove(struct platform_device *pdev)
{
	struct xscaler_device *xscaler = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xscaler->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	xvip_cleanup_resources(&xscaler->xvip);

	return 0;
}

static SIMPLE_DEV_PM_OPS(xscaler_pm_ops, xscaler_pm_suspend, xscaler_pm_resume);

static const struct of_device_id xscaler_of_id_table[] = {
	{ .compatible = "xlnx,v-scaler-8.1" },
	{ }
};
MODULE_DEVICE_TABLE(of, xscaler_of_id_table);

static struct platform_driver xscaler_driver = {
	.driver			= {
		.name		= "xilinx-scaler",
		.of_match_table	= xscaler_of_id_table,
	},
	.probe			= xscaler_probe,
	.remove			= xscaler_remove,
};

module_platform_driver(xscaler_driver);

MODULE_DESCRIPTION("Xilinx Scaler Driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Video IP Core
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/clk.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/media/xilinx-vip.h>

#include "xilinx-vip.h"

/* -----------------------------------------------------------------------------
 * Helper functions
 */

static const struct xvip_video_format xvip_video_formats[] = {
	{ XVIP_VF_YUV_420, 8, NULL, MEDIA_BUS_FMT_VYYUYY8_1X24,
	  1, 12, V4L2_PIX_FMT_NV12, 2, 1, 1, 2 },
	{ XVIP_VF_YUV_420, 8, NULL, MEDIA_BUS_FMT_VYYUYY8_1X24,
	  1, 12, V4L2_PIX_FMT_NV12M, 2, 2, 1, 2 },
	{ XVIP_VF_YUV_420, 10, NULL, MEDIA_BUS_FMT_VYYUYY10_4X20,
	  1, 12, V4L2_PIX_FMT_XV15, 2, 1, 2, 2 },
	{ XVIP_VF_YUV_420, 10, NULL, MEDIA_BUS_FMT_VYYUYY10_4X20,
	  1, 12, V4L2_PIX_FMT_XV15M, 2, 2, 1, 2 },
	{ XVIP_VF_YUV_420, 12, NULL, MEDIA_BUS_FMT_UYYVYY12_4X24,
	  1, 12, V4L2_PIX_FMT_X012, 2, 1, 2, 2 },
	{ XVIP_VF_YUV_420, 12, NULL, MEDIA_BUS_FMT_UYYVYY12_4X24,
	  1, 12, V4L2_PIX_FMT_X012M, 2, 2, 1, 2 },
	{ XVIP_VF_YUV_420, 16, NULL, MEDIA_BUS_FMT_UYYVYY16_4X32,
	  2, 12, V4L2_PIX_FMT_X016, 2, 1, 2, 2 },
	{ XVIP_VF_YUV_420, 16, NULL, MEDIA_BUS_FMT_UYYVYY16_4X32,
	  2, 12, V4L2_PIX_FMT_X016M, 2, 2, 1, 2 },
	{ XVIP_VF_YUV_422, 8, NULL, MEDIA_BUS_FMT_UYVY8_1X16,
	  1, 16, V4L2_PIX_FMT_NV16, 2, 1, 1, 1 },
	{ XVIP_VF_YUV_422, 8, NULL, MEDIA_BUS_FMT_UYVY8_1X16,
	  1, 16, V4L2_PIX_FMT_NV16M, 2, 2, 1, 1 },
	{ XVIP_VF_YUV_422, 8, NULL, MEDIA_BUS_FMT_UYVY8_1X16,
	  2, 16, V4L2_PIX_FMT_YUYV, 1, 1, 2, 1 },
	{ XVIP_VF_VUY_422, 8, NULL, MEDIA_BUS_FMT_UYVY8_1X16,
	  2, 16, V4L2_PIX_FMT_UYVY, 1, 1, 2, 1 },
	{ XVIP_VF_YUV_422, 10, NULL, MEDIA_BUS_FMT_UYVY10_1X20,
	  1, 16, V4L2_PIX_FMT_XV20, 2, 1, 2, 1 },
	{ XVIP_VF_YUV_422, 10, NULL, MEDIA_BUS_FMT_UYVY10_1X20,
	  1, 16, V4L2_PIX_FMT_XV20M, 2, 2, 1, 1 },
	{ XVIP_VF_YUV_422, 12, NULL, MEDIA_BUS_FMT_UYVY12_1X24,
	  1, 16, V4L2_PIX_FMT_X212, 2, 1, 2, 1 },
	{ XVIP_VF_YUV_422, 12, NULL, MEDIA_BUS_FMT_UYVY12_1X24,
	  1, 16, V4L2_PIX_FMT_X212M, 2, 2, 1, 1 },
	{ XVIP_VF_YUV_422, 16, NULL, MEDIA_BUS_FMT_UYVY16_2X32,
	  2, 16, V4L2_PIX_FMT_X216, 2, 1, 2, 1 },
	{ XVIP_VF_YUV_422, 16, NULL, MEDIA_BUS_FMT_UYVY16_2X32,
	  2, 16, V4L2_PIX_FMT_X216M, 2, 2, 1, 1 },
	{ XVIP_VF_YUV_444, 8, NULL, MEDIA_BUS_FMT_VUY8_1X24,
	  3, 24, V4L2_PIX_FMT_VUY24, 1, 1, 1, 1 },
	{ XVIP_VF_YUV_444, 8, NULL, MEDIA_BUS_FMT_VUY8_1X24,
	  1, 8, V4L2_PIX_FMT_YUV444M, 3, 3, 1, 1 },
	{ XVIP_VF_YUVX, 8, NULL, MEDIA_BUS_FMT_VUY8_1X24,
	  4, 32, V4L2_PIX_FMT_XVUY32, 1, 1, 1, 1 },
	{ XVIP_VF_YUVX, 10, NULL, MEDIA_BUS_FMT_VUY10_1X30,
	  3, 32, V4L2_PIX_FMT_XVUY10, 1, 1, 1, 1 },
	{ XVIP_VF_YUV_444, 12, NULL, MEDIA_BUS_FMT_VUY12_1X36,
	  1, 24, V4L2_PIX_FMT_X412, 1, 1, 1, 1 },
	{ XVIP_VF_YUV_444, 12, NULL, MEDIA_BUS_FMT_VUY12_1X36,
	  1, 24, V4L2_PIX_FMT_X412M, 1, 1, 1, 1 },
	{ XVIP_VF_YUV_444, 16, NULL, MEDIA_BUS_FMT_VUY16_1X48,
	  2, 24, V4L2_PIX_FMT_X416, 1, 1, 1, 1 },
	{ XVIP_VF_YUV_444, 16, NULL, MEDIA_BUS_FMT_VUY16_1X48,
	  2, 24, V4L2_PIX_FMT_X416M, 1, 1, 1, 1 },
	{ XVIP_VF_RBG, 8, NULL, MEDIA_BUS_FMT_RBG888_1X24,
	  3, 24, V4L2_PIX_FMT_BGR24, 1, 1, 1, 1 },
	{ XVIP_VF_RBG, 8, NULL, MEDIA_BUS_FMT_RBG888_1X24,
	  3, 24, V4L2_PIX_FMT_RGB24, 1, 1, 1, 1 },
	{ XVIP_VF_BGRX, 8, NULL, MEDIA_BUS_FMT_RBG888_1X24,
	  4, 32, V4L2_PIX_FMT_BGRX32, 1, 1, 1, 1 },
	{ XVIP_VF_XRGB, 8, NULL, MEDIA_BUS_FMT_RBG888_1X24,
	  4, 32, V4L2_PIX_FMT_XBGR32, 1, 1, 1, 1 },
	{ XVIP_VF_XBGR, 10, NULL, MEDIA_BUS_FMT_RBG101010_1X30,
	  3, 32, V4L2_PIX_FMT_XBGR30, 1, 1, 1, 1 },
	{ XVIP_VF_XBGR, 12, NULL, MEDIA_BUS_FMT_RBG121212_1X36,
	  3, 40, V4L2_PIX_FMT_XBGR40, 1, 1, 1, 1 },
	{ XVIP_VF_RBG, 16, NULL, MEDIA_BUS_FMT_RBG161616_1X48,
	  6, 48, V4L2_PIX_FMT_BGR48, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 8, "mono", MEDIA_BUS_FMT_Y8_1X8,
	  1, 8, V4L2_PIX_FMT_GREY, 1, 1, 1, 1 },
	{ XVIP_VF_Y_GREY, 10, NULL, MEDIA_BUS_FMT_Y10_1X10,
	  1, 32, V4L2_PIX_FMT_XY10, 1, 1, 1, 1 },
	{ XVIP_VF_Y_GREY, 12, NULL, MEDIA_BUS_FMT_Y12_1X12,
	  1, 12, V4L2_PIX_FMT_XY12, 1, 1, 1, 1 },
	{ XVIP_VF_Y_GREY, 16, NULL, MEDIA_BUS_FMT_Y16_1X16,
	  2, 16, V4L2_PIX_FMT_Y16, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 8, "rggb", MEDIA_BUS_FMT_SRGGB8_1X8,
	  1, 8, V4L2_PIX_FMT_SGRBG8, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 8, "grbg", MEDIA_BUS_FMT_SGRBG8_1X8,
	  1, 8, V4L2_PIX_FMT_SGRBG8, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 8, "gbrg", MEDIA_BUS_FMT_SGBRG8_1X8,
	  1, 8, V4L2_PIX_FMT_SGBRG8, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 8, "bggr", MEDIA_BUS_FMT_SBGGR8_1X8,
	  1, 8, V4L2_PIX_FMT_SBGGR8, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 10, "rggb", MEDIA_BUS_FMT_SRGGB10_1X10,
	  2, 10, V4L2_PIX_FMT_SRGGB10, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 10, "grbg", MEDIA_BUS_FMT_SGRBG10_1X10,
	  2, 10, V4L2_PIX_FMT_SGRBG10, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 10, "gbrg", MEDIA_BUS_FMT_SGBRG10_1X10,
	  2, 10, V4L2_PIX_FMT_SGBRG10, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 10, "bggr", MEDIA_BUS_FMT_SBGGR10_1X10,
	  2, 10, V4L2_PIX_FMT_SBGGR10, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 12, "rggb", MEDIA_BUS_FMT_SRGGB12_1X12,
	  2, 12, V4L2_PIX_FMT_SRGGB12, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 12, "grbg", MEDIA_BUS_FMT_SGRBG12_1X12,
	  2, 12, V4L2_PIX_FMT_SGRBG12, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 12, "gbrg", MEDIA_BUS_FMT_SGBRG12_1X12,
	  2, 12, V4L2_PIX_FMT_SGBRG12, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 12, "bggr", MEDIA_BUS_FMT_SBGGR12_1X12,
	  2, 12, V4L2_PIX_FMT_SBGGR12, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 16, "rggb", MEDIA_BUS_FMT_SRGGB16_1X16,
	  2, 16, V4L2_PIX_FMT_SRGGB16, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 16, "grbg", MEDIA_BUS_FMT_SGRBG16_1X16,
	  2, 16, V4L2_PIX_FMT_SGRBG16, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 16, "gbrg", MEDIA_BUS_FMT_SGBRG16_1X16,
	  2, 16, V4L2_PIX_FMT_SGBRG16, 1, 1, 1, 1 },
	{ XVIP_VF_MONO_SENSOR, 16, "bggr", MEDIA_BUS_FMT_SBGGR16_1X16,
	  2, 16, V4L2_PIX_FMT_SBGGR16, 1, 1, 1, 1 },
};

/**
 * xvip_get_format_by_code - Retrieve format information for a media bus code
 * @code: the format media bus code
 *
 * Return: a pointer to the format information structure corresponding to the
 * given V4L2 media bus format @code, or ERR_PTR if no corresponding format can
 * be found.
 */
const struct xvip_video_format *xvip_get_format_by_code(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (format->code == code)
			return format;
	}

	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(xvip_get_format_by_code);

/**
 * xvip_get_format_by_fourcc - Retrieve format information for a 4CC
 * @fourcc: the format 4CC
 *
 * Return: a pointer to the format information structure corresponding to the
 * given V4L2 format @fourcc, or ERR_PTR if no corresponding format can be
 * found.
 */
const struct xvip_video_format *xvip_get_format_by_fourcc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (format->fourcc == fourcc)
			return format;
	}

	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(xvip_get_format_by_fourcc);

/**
 * xvip_bpl_scaling_factor - Retrieve bpl scaling factor for a 4CC
 * @fourcc: the format 4CC
 * @numerator: returning numerator of scaling factor
 * @denominator: returning denominator of scaling factor
 *
 * Return: Return numerator and denominator values by address
 */
void xvip_bpl_scaling_factor(u32 fourcc, u32 *numerator, u32 *denominator)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_XY10:
	case V4L2_PIX_FMT_XV15:
	case V4L2_PIX_FMT_XV20:
	case V4L2_PIX_FMT_XV15M:
	case V4L2_PIX_FMT_XV20M:
	case V4L2_PIX_FMT_XBGR30:
	case V4L2_PIX_FMT_XVUY10:
		*numerator = 10;
		*denominator = 8;
		break;
	case V4L2_PIX_FMT_XBGR40:
	case V4L2_PIX_FMT_XY12:
	case V4L2_PIX_FMT_X012:
	case V4L2_PIX_FMT_X012M:
	case V4L2_PIX_FMT_X212:
	case V4L2_PIX_FMT_X212M:
	case V4L2_PIX_FMT_X412:
	case V4L2_PIX_FMT_X412M:
		*numerator = 12;
		*denominator = 8;
		break;
	default:
		*numerator = 1;
		*denominator = 1;
		break;
	}
}
EXPORT_SYMBOL_GPL(xvip_bpl_scaling_factor);

/**
 * xvip_width_padding_factor - Retrieve width's padding factor for a 4CC
 * @fourcc: the format 4CC
 * @numerator: returning numerator of padding factor
 * @denominator: returning denominator of padding factor
 *
 * Return: Return numerator and denominator values by address
 */
void xvip_width_padding_factor(u32 fourcc, u32 *numerator, u32 *denominator)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_XY10:
	case V4L2_PIX_FMT_XV15:
	case V4L2_PIX_FMT_XV20:
	case V4L2_PIX_FMT_XV15M:
	case V4L2_PIX_FMT_XV20M:
	case V4L2_PIX_FMT_XBGR30:
	case V4L2_PIX_FMT_XVUY10:
		/* 32 bits are required per 30 bits of data */
		*numerator = 32;
		*denominator = 30;
		break;
	case V4L2_PIX_FMT_XBGR40:
	case V4L2_PIX_FMT_XY12:
	case V4L2_PIX_FMT_X012:
	case V4L2_PIX_FMT_X012M:
	case V4L2_PIX_FMT_X212:
	case V4L2_PIX_FMT_X212M:
	case V4L2_PIX_FMT_X412:
	case V4L2_PIX_FMT_X412M:
		*numerator = 40;
		*denominator = 36;
		break;
	default:
		*numerator = 1;
		*denominator = 1;
		break;
	}
}
EXPORT_SYMBOL_GPL(xvip_width_padding_factor);

/**
 * xvip_of_get_format - Parse a device tree node and return format information
 * @node: the device tree node
 *
 * Read the xlnx,video-format, xlnx,video-width and xlnx,cfa-pattern properties
 * from the device tree @node passed as an argument and return the corresponding
 * format information.
 *
 * Return: a pointer to the format information structure corresponding to the
 * format name and width, or ERR_PTR if no corresponding format can be found.
 */
const struct xvip_video_format *xvip_of_get_format(struct device_node *node)
{
	const char *pattern = "mono";
	unsigned int vf_code = 0;
	unsigned int i;
	u32 width = 0;
	int ret;

	ret = of_property_read_u32(node, "xlnx,video-format", &vf_code);
	if (ret < 0)
		return ERR_PTR(ret);

	ret = of_property_read_u32(node, "xlnx,video-width", &width);
	if (ret < 0)
		return ERR_PTR(ret);

	if (vf_code == XVIP_VF_MONO_SENSOR) {
		ret = of_property_read_string(node,
					      "xlnx,cfa-pattern",
					      &pattern);
		if (ret < 0)
			return ERR_PTR(ret);
	}

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (format->vf_code != vf_code || format->width != width)
			continue;

		if (vf_code == XVIP_VF_MONO_SENSOR &&
		    strcmp(pattern, format->pattern))
			continue;

		return format;
	}

	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(xvip_of_get_format);

/**
 * xvip_set_format_size - Set the media bus frame format size
 * @format: V4L2 frame format on media bus
 * @fmt: media bus format
 *
 * Set the media bus frame format size. The width / height from the subdevice
 * format are set to the given media bus format. The new format size is stored
 * in @format. The width and height are clamped using default min / max values.
 */
void xvip_set_format_size(struct v4l2_mbus_framefmt *format,
			  const struct v4l2_subdev_format *fmt)
{
	format->width = clamp_t(unsigned int, fmt->format.width,
				XVIP_MIN_WIDTH, XVIP_MAX_WIDTH);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 XVIP_MIN_HEIGHT, XVIP_MAX_HEIGHT);
}
EXPORT_SYMBOL_GPL(xvip_set_format_size);

/**
 * xvip_clr_or_set - Clear or set the register with a bitmask
 * @xvip: Xilinx Video IP device
 * @addr: address of register
 * @mask: bitmask to be set or cleared
 * @set: boolean flag indicating whether to set or clear
 *
 * Clear or set the register at address @addr with a bitmask @mask depending on
 * the boolean flag @set. When the flag @set is true, the bitmask is set in
 * the register, otherwise the bitmask is cleared from the register
 * when the flag @set is false.
 *
 * Fox example, this function can be used to set a control with a boolean value
 * requested by users. If the caller knows whether to set or clear in the first
 * place, the caller should call xvip_clr() or xvip_set() directly instead of
 * using this function.
 */
void xvip_clr_or_set(struct xvip_device *xvip, u32 addr, u32 mask, bool set)
{
	u32 reg;

	reg = xvip_read(xvip, addr);
	reg = set ? reg | mask : reg & ~mask;
	xvip_write(xvip, addr, reg);
}
EXPORT_SYMBOL_GPL(xvip_clr_or_set);

/**
 * xvip_clr_and_set - Clear and set the register with a bitmask
 * @xvip: Xilinx Video IP device
 * @addr: address of register
 * @clr: bitmask to be cleared
 * @set: bitmask to be set
 *
 * Clear a bit(s) of mask @clr in the register at address @addr, then set
 * a bit(s) of mask @set in the register after.
 */
void xvip_clr_and_set(struct xvip_device *xvip, u32 addr, u32 clr, u32 set)
{
	u32 reg;

	reg = xvip_read(xvip, addr);
	reg &= ~clr;
	reg |= set;
	xvip_write(xvip, addr, reg);
}
EXPORT_SYMBOL_GPL(xvip_clr_and_set);

int xvip_init_resources(struct xvip_device *xvip)
{
	struct platform_device *pdev = to_platform_device(xvip->dev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xvip->iomem = devm_ioremap_resource(xvip->dev, res);
	if (IS_ERR(xvip->iomem))
		return PTR_ERR(xvip->iomem);

	xvip->clk = devm_clk_get(xvip->dev, NULL);
	if (IS_ERR(xvip->clk))
		return PTR_ERR(xvip->clk);

	return clk_prepare_enable(xvip->clk);
}
EXPORT_SYMBOL_GPL(xvip_init_resources);

void xvip_cleanup_resources(struct xvip_device *xvip)
{
	clk_disable_unprepare(xvip->clk);
}
EXPORT_SYMBOL_GPL(xvip_cleanup_resources);

/* -----------------------------------------------------------------------------
 * Subdev operations handlers
 */

/**
 * xvip_enum_mbus_code - Enumerate the media format code
 * @subdev: V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @code: returning media bus code
 *
 * Enumerate the media bus code of the subdevice. Return the corresponding
 * pad format code. This function only works for subdevices with fixed format
 * on all pads. Subdevices with multiple format should have their own
 * function to enumerate mbus codes.
 *
 * Return: 0 if the media bus code is found, or -EINVAL if the format index
 * is not valid.
 */
int xvip_enum_mbus_code(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct v4l2_mbus_framefmt *format;

	/* Enumerating frame sizes based on the active configuration isn't
	 * supported yet.
	 */
	if (code->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	if (code->index)
		return -EINVAL;

	format = v4l2_subdev_get_try_format(subdev, cfg, code->pad);

	code->code = format->code;

	return 0;
}
EXPORT_SYMBOL_GPL(xvip_enum_mbus_code);

/**
 * xvip_enum_frame_size - Enumerate the media bus frame size
 * @subdev: V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fse: returning media bus frame size
 *
 * This function is a drop-in implementation of the subdev enum_frame_size pad
 * operation. It assumes that the subdevice has one sink pad and one source
 * pad, and that the format on the source pad is always identical to the
 * format on the sink pad. Entities with different requirements need to
 * implement their own enum_frame_size handlers.
 *
 * Return: 0 if the media bus frame size is found, or -EINVAL
 * if the index or the code is not valid.
 */
int xvip_enum_frame_size(struct v4l2_subdev *subdev,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	/* Enumerating frame sizes based on the active configuration isn't
	 * supported yet.
	 */
	if (fse->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	format = v4l2_subdev_get_try_format(subdev, cfg, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == XVIP_PAD_SINK) {
		fse->min_width = XVIP_MIN_WIDTH;
		fse->max_width = XVIP_MAX_WIDTH;
		fse->min_height = XVIP_MIN_HEIGHT;
		fse->max_height = XVIP_MAX_HEIGHT;
	} else {
		/* The size on the source pad is fixed and always identical to
		 * the size on the sink pad.
		 */
		fse->min_width = format->width;
		fse->max_width = format->width;
		fse->min_height = format->height;
		fse->max_height = format->height;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xvip_enum_frame_size);

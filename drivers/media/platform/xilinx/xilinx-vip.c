/*
 * Xilinx Video IP Core
 *
 * Copyright (C) 2013 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>

#include "xilinx-vip.h"

/* -----------------------------------------------------------------------------
 * Helper functions
 */

static const struct xvip_video_format xvip_video_formats[] = {
	{ "rbg", 8, 3, V4L2_MBUS_FMT_RBG888_1X24, 0 },
	{ "xrgb", 8, 4, V4L2_MBUS_FMT_RGB888_1X32_PADHI, V4L2_PIX_FMT_BGR32 },
	{ "yuv422", 8, 2, V4L2_MBUS_FMT_UYVY8_1X16, V4L2_PIX_FMT_YUYV },
	{ "yuv444", 8, 3, V4L2_MBUS_FMT_VUY8_1X24, V4L2_PIX_FMT_YUV444 },
	{ "rggb", 8, 1, V4L2_MBUS_FMT_SRGGB8_1X8, V4L2_PIX_FMT_SGRBG8 },
	{ "grbg", 8, 1, V4L2_MBUS_FMT_SGRBG8_1X8, V4L2_PIX_FMT_SGRBG8 },
	{ "gbrg", 8, 1, V4L2_MBUS_FMT_SGBRG8_1X8, V4L2_PIX_FMT_SGBRG8 },
	{ "bggr", 8, 1, V4L2_MBUS_FMT_SBGGR8_1X8, V4L2_PIX_FMT_SBGGR8 },
};

/**
 * xvip_get_format_by_code - Retrieve format information for a media bus code
 * @code: the format media bus code
 *
 * Return: a pointer to the format information structure corresponding to the
 * given V4L2 media bus format @code, or %NULL if no corresponding format can be
 * found.
 */
const struct xvip_video_format *xvip_get_format_by_code(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (format->code == code)
			return format;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(xvip_get_format_by_code);

/**
 * xvip_get_format_by_fourcc - Retrieve format information for a 4CC
 * @fourcc: the format 4CC
 *
 * Return: a pointer to the format information structure corresponding to the
 * given V4L2 format @fourcc, or %NULL if no corresponding format can be found.
 */
const struct xvip_video_format *xvip_get_format_by_fourcc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (format->fourcc == fourcc)
			return format;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(xvip_get_format_by_fourcc);

/**
 * xvip_of_get_format - Parse a device tree node and return format information
 * @node: the device tree node
 *
 * Read the xlnx,axi-video-format and xlnx,axi-video-width properties from the
 * device tree @node passed as an argument and return the corresponding format
 * information.
 *
 * Return: a pointer to the format information structure corresponding to the
 * format name and width, or %NULL if no corresponding format can be found.
 */
const struct xvip_video_format *xvip_of_get_format(struct device_node *node)
{
	const char *name;
	unsigned int i;
	u32 width;
	int ret;

	ret = of_property_read_string(node, "xlnx,axi-video-format", &name);
	if (ret < 0)
		return NULL;

	ret = of_property_read_u32(node, "xlnx,axi-video-width", &width);
	if (ret < 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (strcmp(format->name, name) == 0 && format->width == width)
			return format;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(xvip_of_get_format);

/*
 * Xilinx Color Space Converter
 *
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"
#include "xilinx-vpss-csc.h"

enum xcsc_color_fmt {
	XVIDC_CSF_RGB = 0,
	XVIDC_CSF_YCRCB_444
};

enum xcsc_color_std {
	XVIDC_BT_2020 = 1,
	XVIDC_BT_709,
	XVIDC_BT_601
};

enum xcsc_output_range {
	XVIDC_CR_0_255 = 1,
	XVIDC_CR_16_240,
	XVIDC_CR_16_235
};

enum xcsc_color_depth {
	XVIDC_BPC_8 = 8
};

struct xcsc_dev {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_formats[2];
	const struct xvip_video_format *vip_formats[2];
	struct v4l2_ctrl_handler ctrl_handler;

	enum xcsc_color_fmt cft_in;
	enum xcsc_color_fmt cft_out;
	enum xcsc_color_std std_in;
	enum xcsc_color_std std_out;
	enum xcsc_output_range output_range;
	enum xcsc_color_depth color_depth;
	s32 brightness;
	s32 contrast;
	s32 red_gain;
	s32 green_gain;
	s32 blue_gain;
	s32 brightness_active;
	s32 contrast_active;
	s32 red_gain_active;
	s32 green_gain_active;
	s32 blue_gain_active;
	s32 k_hw[3][4];
	s32 clip_max;
	s32 clamp_min;
	struct gpio_desc *rst_gpio;
};

static u32 xcsc_read(struct xcsc_dev *xcsc, u32 reg)
{
	u32 data;

	data = xvip_read(&xcsc->xvip, reg);
	dev_dbg(xcsc->xvip.dev,
		"Reading 0x%x from register offset 0x%x", data, reg);
	return data;
}

static void xcsc_get_coeff(struct xcsc_dev *xcsc, s32 C[3][4])
{
	C[0][0] = xcsc_read(xcsc, XV_CSC_K11);
	C[0][1] = xcsc_read(xcsc, XV_CSC_K12);
	C[0][2] = xcsc_read(xcsc, XV_CSC_K13);
	C[1][0] = xcsc_read(xcsc, XV_CSC_K21);
	C[1][1] = xcsc_read(xcsc, XV_CSC_K22);
	C[1][2] = xcsc_read(xcsc, XV_CSC_K23);
	C[2][0] = xcsc_read(xcsc, XV_CSC_K31);
	C[2][1] = xcsc_read(xcsc, XV_CSC_K32);
	C[2][2] = xcsc_read(xcsc, XV_CSC_K33);
	C[0][3] = xcsc_read(xcsc, XV_CSC_ROFFSET);
	C[1][3] = xcsc_read(xcsc, XV_CSC_GOFFSET);
	C[2][3] = xcsc_read(xcsc, XV_CSC_BOFFSET);
}

static void xcsc_print_coeff(struct xcsc_dev *xcsc)
{
	s32 C[3][4];

	xcsc_get_coeff(xcsc, C);

	dev_dbg(xcsc->xvip.dev,
		"-------------CSC Coeff Dump Start------\n");
	dev_dbg(xcsc->xvip.dev,
		" R row : %5d  %5d  %5d\n",
		 (s16)C[0][0], (s16)C[0][1], (s16)C[0][2]);
	dev_dbg(xcsc->xvip.dev,
		" G row : %5d  %5d  %5d\n",
		 (s16)C[1][0], (s16)C[1][1], (s16)C[1][2]);
	dev_dbg(xcsc->xvip.dev,
		" B row : %5d  %5d  %5d\n",
		 (s16)C[2][0], (s16)C[2][1], (s16)C[2][2]);
	dev_dbg(xcsc->xvip.dev,
		"Offset : %5d  %5d  %5d\n",
		 (s16)C[0][3], (s16)C[1][3], (s16)C[2][3]);
	dev_dbg(xcsc->xvip.dev,
		"ClampMin: %3d  ClipMax %3d",
		 xcsc_read(xcsc, XV_CSC_CLAMPMIN),
		 xcsc_read(xcsc, XV_CSC_CLIPMAX));
	dev_dbg(xcsc->xvip.dev,
		"-------------CSC Coeff Dump Stop-------\n");
}

static void xcsc_print_k_hw(struct xcsc_dev *xcsc)
{
	dev_dbg(xcsc->xvip.dev, "-------------CSC Driver k_hw[][] Dump------------\n");
	dev_dbg(xcsc->xvip.dev, "k_hw[0][x] R row : %5d  %5d  %5d\n",
		xcsc->k_hw[0][0],  xcsc->k_hw[0][1],  xcsc->k_hw[0][2]);
	dev_dbg(xcsc->xvip.dev, "k_hw[1][x] G row : %5d  %5d  %5d\n",
		xcsc->k_hw[1][0],  xcsc->k_hw[1][1],  xcsc->k_hw[1][2]);
	dev_dbg(xcsc->xvip.dev, "k_hw[2][x] B row : %5d  %5d  %5d\n",
		xcsc->k_hw[2][0],  xcsc->k_hw[2][1],  xcsc->k_hw[2][2]);
	dev_dbg(xcsc->xvip.dev, "k_hw[x][3] Offset : %5d  %5d  %5d\n",
		xcsc->k_hw[0][3],  xcsc->k_hw[1][3],  xcsc->k_hw[2][3]);
	dev_dbg(xcsc->xvip.dev, "-------------------------------------------------\n");
}

static void xcsc_write(struct xcsc_dev *xcsc, u32 reg,
		       u32 data, const char *func)
{
	u32 rb;

	dev_dbg(xcsc->xvip.dev,
		"Writing 0x%x to register offset 0x%x", data, reg);
	xvip_write(&xcsc->xvip, reg, data);
	rb = xcsc_read(xcsc, reg);
	if (rb != data) {
		dev_dbg(xcsc->xvip.dev,
			"%s : Wrote 0x%x does not match read back 0x%x for reg 0x%x",
			func, data, rb, reg);
		xcsc_print_k_hw(xcsc);
		xcsc_print_coeff(xcsc);
	}
}

static void xcsc_write_rgb_3x3(struct xcsc_dev *xcsc)
{
	/* Write Matrix Coefficients */
	xcsc_write(xcsc, XV_CSC_K11, xcsc->k_hw[0][0], __func__);
	xcsc_write(xcsc, XV_CSC_K12, xcsc->k_hw[0][1], __func__);
	xcsc_write(xcsc, XV_CSC_K13, xcsc->k_hw[0][2], __func__);
	xcsc_write(xcsc, XV_CSC_K21, xcsc->k_hw[1][0], __func__);
	xcsc_write(xcsc, XV_CSC_K22, xcsc->k_hw[1][1], __func__);
	xcsc_write(xcsc, XV_CSC_K23, xcsc->k_hw[1][2], __func__);
	xcsc_write(xcsc, XV_CSC_K31, xcsc->k_hw[2][0], __func__);
	xcsc_write(xcsc, XV_CSC_K32, xcsc->k_hw[2][1], __func__);
	xcsc_write(xcsc, XV_CSC_K33, xcsc->k_hw[2][2], __func__);
}

static void xcsc_write_rgb_offset(struct xcsc_dev *xcsc)
{
	/* Write RGB Offsets */
	xcsc_write(xcsc, XV_CSC_ROFFSET, xcsc->k_hw[0][3], __func__);
	xcsc_write(xcsc, XV_CSC_GOFFSET, xcsc->k_hw[1][3], __func__);
	xcsc_write(xcsc, XV_CSC_BOFFSET, xcsc->k_hw[2][3], __func__);
}

static void xcsc_write_coeff(struct xcsc_dev *xcsc)
{
	xcsc_write_rgb_3x3(xcsc);
	xcsc_write_rgb_offset(xcsc);
}

static void xcsc_rgb_to_ycrcb(struct xcsc_dev *xcsc,
			      s32 *clamp_min, s32 *clip_max)
{
	s32 bpc_scale = (1 << (xcsc->color_depth - 8));

	switch (xcsc->std_out) {
	case XVIDC_BT_709:
		dev_info(xcsc->xvip.dev, "Performing RGB to YCrCb BT 709");
		xcsc->k_hw[0][0] =
			(1826 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[0][1] =
			(6142 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[0][2] =
			(620 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[1][0] =
			(-1006 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[1][1] =
			(-3386 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[1][2] =
			(4392 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[2][0] =
			(4392 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[2][1] =
			(-3989 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[2][2] =
			(-403 * XV_CSC_SCALE_FACTOR) / XV_CSC_DIVISOR;
		xcsc->k_hw[0][3] = 16 * bpc_scale;
		xcsc->k_hw[1][3] = 128 * bpc_scale;
		xcsc->k_hw[2][3] = 128 * bpc_scale;
		break;
	default:
		dev_err(xcsc->xvip.dev,
			"%s : Unsupported Output Standard", __func__);
	}

	*clamp_min = 0;
	*clip_max = ((1 <<  xcsc->color_depth) - 1);
}

static int xcsc_set_coeff(struct xcsc_dev *xcsc)
{
	u32 color_in, color_out;

	/* Write In and Out Video Formats */
	color_in = xcsc->formats[XVIP_PAD_SINK].code;
	color_out = xcsc->formats[XVIP_PAD_SOURCE].code;
	if (color_in != MEDIA_BUS_FMT_RBG888_1X24 &&
	    xcsc->cft_in != XVIDC_CSF_RGB) {
		dev_err(xcsc->xvip.dev, "Unsupported sink pad media code");
		xcsc->cft_in = XVIDC_CSF_RGB;
		xcsc->formats[XVIP_PAD_SINK].code = MEDIA_BUS_FMT_RBG888_1X24;
	}

	if (color_out == MEDIA_BUS_FMT_RBG888_1X24) {
		xcsc->cft_out = XVIDC_CSF_RGB;
	} else if (color_out == MEDIA_BUS_FMT_VUY8_1X24) {
		xcsc->cft_out = XVIDC_CSF_YCRCB_444;
		xcsc_rgb_to_ycrcb(xcsc, &xcsc->clamp_min, &xcsc->clip_max);
	} else {
		dev_err(xcsc->xvip.dev, "Unsupported source pad media code");
		xcsc->cft_out = XVIDC_CSF_RGB;
		xcsc->formats[XVIP_PAD_SOURCE].code = MEDIA_BUS_FMT_RBG888_1X24;
	}

	xcsc_write(xcsc, XV_CSC_INVIDEOFORMAT, xcsc->cft_in, __func__);
	xcsc_write(xcsc, XV_CSC_OUTVIDEOFORMAT, xcsc->cft_out, __func__);

	xcsc_write_coeff(xcsc);

	xcsc_write(xcsc, XV_CSC_CLIPMAX, xcsc->clip_max, __func__);
	xcsc_write(xcsc, XV_CSC_CLAMPMIN, xcsc->clamp_min, __func__);

	xcsc_print_coeff(xcsc);
	return 0;
}

static inline struct xcsc_dev *to_csc(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xcsc_dev, xvip.subdev);
}

static struct v4l2_mbus_framefmt *
__xcsc_get_pad_format(struct xcsc_dev *xcsc,
		      struct v4l2_subdev_pad_config *cfg,
		      unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xcsc->xvip.subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xcsc->formats[pad];
	default:
		return NULL;
	}
}

static void xcsc_set_default_state(struct xcsc_dev *xcsc)
{
	xcsc->cft_in = XVIDC_CSF_RGB;
	xcsc->cft_out = XVIDC_CSF_RGB;
	xcsc->std_in = XVIDC_BT_709;
	xcsc->std_out = XVIDC_BT_709;
	xcsc->output_range = XVIDC_CR_0_255;
	xcsc->color_depth = XVIDC_BPC_8;
	xcsc->brightness  = 120;
	xcsc->contrast = 0;
	xcsc->red_gain = 120;
	xcsc->blue_gain = 120;
	xcsc->green_gain = 120;
	xcsc->brightness_active	= 120;
	xcsc->contrast_active = 0;
	xcsc->red_gain_active = 120;
	xcsc->blue_gain_active = 120;
	xcsc->green_gain_active = 120;
	xcsc->k_hw[0][0] = XV_CSC_SCALE_FACTOR;
	xcsc->k_hw[0][1] = 0;
	xcsc->k_hw[0][2] = 0;
	xcsc->k_hw[1][0] = 0;
	xcsc->k_hw[1][1] = XV_CSC_SCALE_FACTOR;
	xcsc->k_hw[1][2] = 0;
	xcsc->k_hw[2][0] = 0;
	xcsc->k_hw[2][1] = 0;
	xcsc->k_hw[2][2] = XV_CSC_SCALE_FACTOR;
	xcsc->k_hw[0][3] = 0;
	xcsc->k_hw[1][3] = 0;
	xcsc->k_hw[2][3] = 0;
	xcsc->clamp_min = 0;
	xcsc->clip_max = ((1 << xcsc->color_depth) - 1);

	xcsc_write(xcsc, XV_CSC_INVIDEOFORMAT, xcsc->cft_in, __func__);
	xcsc_write(xcsc, XV_CSC_OUTVIDEOFORMAT, xcsc->cft_out, __func__);

	xcsc_write_coeff(xcsc);

	xcsc_write(xcsc, XV_CSC_CLIPMAX, xcsc->clip_max, __func__);
	xcsc_write(xcsc, XV_CSC_CLAMPMIN, xcsc->clamp_min, __func__);
}

static void xcsc_set_brightness(struct xcsc_dev *xcsc)
{
	int i, j;

	dev_dbg(xcsc->xvip.dev,
		"%s : Brightness %d Brightness Active %d",
		__func__,
		((xcsc->brightness - 20) / 2),
		((xcsc->brightness_active - 20) / 2));
	if (xcsc->brightness == xcsc->brightness_active)
		return;
	for (i = 0; i < XV_CSC_K_MAX_ROWS; i++) {
		for (j = 0; j < XV_CSC_K_MAX_COLUMNS; j++) {
			xcsc->k_hw[i][j] =
			((xcsc->k_hw[i][j] * xcsc->brightness) /
					xcsc->brightness_active);
		}
	}
	xcsc->brightness_active = xcsc->brightness;
	xcsc_write_rgb_3x3(xcsc);
}

static void xcsc_set_contrast(struct xcsc_dev *xcsc)
{
	s32 contrast;

	contrast = xcsc->contrast - xcsc->contrast_active;
	dev_dbg(xcsc->xvip.dev,
		"%s : Contrast Difference %d", __func__, contrast);

	/* Update RGB Offsets */
	xcsc->k_hw[0][3] +=
		XV_CSC_RGB_OFFSET_WR(contrast * XV_CSC_SCALE_FACTOR);
	xcsc->k_hw[1][3] +=
		XV_CSC_RGB_OFFSET_WR(contrast * XV_CSC_SCALE_FACTOR);
	xcsc->k_hw[2][3] +=
		XV_CSC_RGB_OFFSET_WR(contrast * XV_CSC_SCALE_FACTOR);
	xcsc->contrast_active = xcsc->contrast;
	xcsc_write_rgb_offset(xcsc);
}

static void xcsc_set_red_gain(struct xcsc_dev *xcsc)
{
	/* Red Gain */
	dev_dbg(xcsc->xvip.dev,
		"%s: Red Gain %d Red Gain Active %d", __func__,
		(xcsc->red_gain - 20) / 2,
		(xcsc->red_gain_active - 20) / 2);

	if (xcsc->red_gain != xcsc->red_gain_active) {
		xcsc->k_hw[0][0] = ((xcsc->k_hw[0][0] *
			xcsc->red_gain) / xcsc->red_gain_active);
		xcsc->k_hw[0][1] = ((xcsc->k_hw[0][1] *
			xcsc->red_gain) / xcsc->red_gain_active);
		xcsc->k_hw[0][2] = ((xcsc->k_hw[0][2] *
			xcsc->red_gain) / xcsc->red_gain_active);
		xcsc->red_gain_active = xcsc->red_gain;
	}

	xcsc_write(xcsc, XV_CSC_K11, xcsc->k_hw[0][0], __func__);
	xcsc_write(xcsc, XV_CSC_K12, xcsc->k_hw[0][1], __func__);
	xcsc_write(xcsc, XV_CSC_K13, xcsc->k_hw[0][2], __func__);
}

static void xcsc_set_green_gain(struct xcsc_dev *xcsc)
{
	/* Green Gain */
	dev_dbg(xcsc->xvip.dev,
		"%s: Green Gain %d Green Gain Active %d", __func__,
		 (xcsc->green_gain - 20) / 2,
		 (xcsc->green_gain_active - 20) / 2);

	if (xcsc->green_gain != xcsc->green_gain_active) {
		xcsc->k_hw[1][0] = ((xcsc->k_hw[1][0] *
			xcsc->green_gain) / xcsc->green_gain_active);
		xcsc->k_hw[1][1] = ((xcsc->k_hw[1][1] *
			xcsc->green_gain) / xcsc->green_gain_active);
		xcsc->k_hw[1][2] = ((xcsc->k_hw[1][2] *
			xcsc->green_gain) / xcsc->green_gain_active);
		xcsc->green_gain_active = xcsc->green_gain;
	}
	xcsc_write(xcsc, XV_CSC_K21, xcsc->k_hw[1][0], __func__);
	xcsc_write(xcsc, XV_CSC_K22, xcsc->k_hw[1][1], __func__);
	xcsc_write(xcsc, XV_CSC_K23, xcsc->k_hw[1][2], __func__);
}

static void xcsc_set_blue_gain(struct xcsc_dev *xcsc)
{
	/* Blue Gain */
	dev_dbg(xcsc->xvip.dev,
		"%s: Blue Gain %d Blue Gain Active %d", __func__,
		 (xcsc->blue_gain - 20) / 2,
		 (xcsc->blue_gain_active - 20) / 2);

	if (xcsc->blue_gain != xcsc->blue_gain_active) {
		xcsc->k_hw[2][0] = ((xcsc->k_hw[2][0] *
			xcsc->blue_gain) / xcsc->blue_gain_active);
		xcsc->k_hw[2][1] = ((xcsc->k_hw[2][1] *
			xcsc->blue_gain) / xcsc->blue_gain_active);
		xcsc->k_hw[2][2] = ((xcsc->k_hw[2][2] *
			xcsc->blue_gain) / xcsc->blue_gain_active);
		xcsc->blue_gain_active = xcsc->blue_gain;
	}

	xcsc_write(xcsc, XV_CSC_K31, xcsc->k_hw[2][0], __func__);
	xcsc_write(xcsc, XV_CSC_K32, xcsc->k_hw[2][1], __func__);
	xcsc_write(xcsc, XV_CSC_K33, xcsc->k_hw[2][2], __func__);
}

static void xcsc_set_size(struct xcsc_dev *xcsc)
{
	u32 width, height;

	width = xcsc->formats[XVIP_PAD_SINK].width;
	height = xcsc->formats[XVIP_PAD_SINK].height;
	dev_dbg(xcsc->xvip.dev, "%s : Setting width %d and height %d",
		__func__, width, height);
	xcsc_write(xcsc, XV_CSC_WIDTH, width, __func__);
	xcsc_write(xcsc, XV_CSC_HEIGHT, height, __func__);
}

static int xcsc_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xcsc_dev *xcsc = to_csc(subdev);

	if (!enable) {
		dev_dbg(xcsc->xvip.dev, "%s : Off", __func__);
		/* Reset the Global IP Reset through PS GPIO */
		gpiod_set_value_cansleep(xcsc->rst_gpio, 0x1);
		usleep_range(150, 200);
		gpiod_set_value_cansleep(xcsc->rst_gpio, 0x0);
		usleep_range(150, 200);
		/* Restore hw state */
		xcsc_set_coeff(xcsc);
		xcsc_set_size(xcsc);
		return 0;
	}
	dev_dbg(xcsc->xvip.dev, "%s : On", __func__);

	xcsc_set_size(xcsc);
	xcsc_set_coeff(xcsc);

	/* Start VPSS CSC IP */
	xcsc_write(xcsc, XV_CSC_AP_CTRL, 0x81, __func__);
	return 0;
}

static const struct v4l2_subdev_video_ops xcsc_video_ops = {
	.s_stream = xcsc_s_stream,
};

static int xcsc_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xcsc_dev *xcsc = to_csc(subdev);

	fmt->format = *__xcsc_get_pad_format(xcsc, cfg, fmt->pad, fmt->which);
	return 0;
}

static int xcsc_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xcsc_dev *xcsc = to_csc(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_mbus_framefmt *__propagate;

	__format = __xcsc_get_pad_format(xcsc, cfg, fmt->pad, fmt->which);
	/* Propagate to Source Pad */
	__propagate = __xcsc_get_pad_format(xcsc, cfg,
					    XVIP_PAD_SOURCE, fmt->which);
	*__format = fmt->format;

	if (fmt->pad == XVIP_PAD_SINK) {
		if (fmt->format.code != MEDIA_BUS_FMT_RBG888_1X24)
			dev_err(xcsc->xvip.dev, "Not supported Sink Media Format");
		xcsc->cft_in = XVIDC_CSF_RGB;
		__format->code = MEDIA_BUS_FMT_RBG888_1X24;

	} else if (fmt->pad == XVIP_PAD_SOURCE) {
		if ((fmt->format.code != MEDIA_BUS_FMT_VUY8_1X24) &&
		    (fmt->format.code != MEDIA_BUS_FMT_RBG888_1X24)) {
			dev_err(xcsc->xvip.dev, "Not supported Source Format");
			xcsc->cft_out = XVIDC_CSF_RGB;
			__format->code = MEDIA_BUS_FMT_RBG888_1X24;
		} else {
			if (fmt->format.code == MEDIA_BUS_FMT_RBG888_1X24)
				xcsc->cft_out = XVIDC_CSF_RGB;
			else
				xcsc->cft_out = XVIDC_CSF_YCRCB_444;
		}

	} else {
		/* Should never get here */
		dev_err(xcsc->xvip.dev, "Undefined media pad");
		return -EINVAL;
	}

	__propagate->width  = __format->width;
	__propagate->height = __format->height;

	fmt->format = *__format;
	return 0;
}

static const struct v4l2_subdev_pad_ops xcsc_pad_ops = {
	.enum_mbus_code = xvip_enum_mbus_code,
	.enum_frame_size = xvip_enum_frame_size,
	.get_fmt = xcsc_get_format,
	.set_fmt = xcsc_set_format,
};

static const struct v4l2_subdev_ops xcsc_ops = {
	.video = &xcsc_video_ops,
	.pad = &xcsc_pad_ops
};

static int xcsc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xcsc_dev *xcsc = container_of(ctrl->handler,
					struct xcsc_dev,
					ctrl_handler);

	dev_dbg(xcsc->xvip.dev, "%s  called", __func__);
	switch (ctrl->id) {
	case V4L2_CID_XILINX_CSC_BRIGHTNESS:
		xcsc->brightness = (2 * ctrl->val) + 20;
		xcsc_set_brightness(xcsc);
		break;
	case V4L2_CID_XILINX_CSC_CONTRAST:
		xcsc->contrast = (4 * ctrl->val) - 200;
		xcsc_set_contrast(xcsc);
		break;
	case V4L2_CID_XILINX_CSC_RED_GAIN:
		xcsc->red_gain =  (2 * ctrl->val) + 20;
		xcsc_set_red_gain(xcsc);
		break;
	case V4L2_CID_XILINX_CSC_BLUE_GAIN:
		xcsc->blue_gain =  (2 * ctrl->val) + 20;
		xcsc_set_blue_gain(xcsc);
		break;
	case V4L2_CID_XILINX_CSC_GREEN_GAIN:
		xcsc->green_gain =  (2 * ctrl->val) + 20;
		xcsc_set_green_gain(xcsc);
		break;
	}
	xcsc_print_coeff(xcsc);
	return 0;
}

static const struct v4l2_ctrl_ops xcsc_ctrl_ops = {
	.s_ctrl = xcsc_s_ctrl,
};

static struct v4l2_ctrl_config xcsc_ctrls[] = {
	/* Brightness */
	{
		.ops = &xcsc_ctrl_ops,
		.id = V4L2_CID_XILINX_CSC_BRIGHTNESS,
		.name = "CSC Brightness",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 100,
		.step = 1,
		.def = 50,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Contrast */
	{
		.ops = &xcsc_ctrl_ops,
		.id = V4L2_CID_XILINX_CSC_CONTRAST,
		.name = "CSC Contrast",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 100,
		.step = 1,
		.def = 50,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Red Gain */
	{
		.ops = &xcsc_ctrl_ops,
		.id = V4L2_CID_XILINX_CSC_RED_GAIN,
		.name = "CSC Red Gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 100,
		.step = 1,
		.def = 50,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Blue Gain */
	{
		.ops = &xcsc_ctrl_ops,
		.id = V4L2_CID_XILINX_CSC_BLUE_GAIN,
		.name = "CSC Blue Gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 100,
		.step = 1,
		.def = 50,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Green Gain */
	{
		.ops = &xcsc_ctrl_ops,
		.id = V4L2_CID_XILINX_CSC_GREEN_GAIN,
		.name = "CSC Green Gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 100,
		.step = 1,
		.def = 50,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static int xcsc_open(struct v4l2_subdev *subdev,
		     struct v4l2_subdev_fh *fh)
{
	struct xcsc_dev *xcsc = to_csc(subdev);
	struct v4l2_mbus_framefmt *format;

	/* Initialize with default formats */
	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SINK);
	*format = xcsc->default_formats[XVIP_PAD_SINK];

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SOURCE);
	*format = xcsc->default_formats[XVIP_PAD_SOURCE];

	return 0;
}

static int xcsc_close(struct v4l2_subdev *subdev,
		      struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops xcsc_internal_ops = {
	.open  = xcsc_open,
	.close = xcsc_close,
};

static const struct media_entity_operations xcsc_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int xcsc_parse_of(struct xcsc_dev *xcsc)
{
	struct device *dev = xcsc->xvip.dev;
	struct device_node *node = xcsc->xvip.dev->of_node;
	const struct xvip_video_format *vip_format;
	struct device_node *ports, *port;
	int rval;
	u32 port_id = 0;

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;

	/* Get the format description for each pad */
	for_each_child_of_node(ports, port) {
		if (port->name && (of_node_cmp(port->name, "port") == 0)) {
			vip_format = xvip_of_get_format(port);
			if (IS_ERR(vip_format)) {
				dev_err(dev, "Invalid media pad format in DT");
				return PTR_ERR(vip_format);
			}

			rval = of_property_read_u32(port, "reg", &port_id);
			if (rval < 0) {
				dev_err(dev, "No reg in DT to specify pad");
				return rval;
			}

			if (port_id != 0 && port_id != 1) {
				dev_err(dev, "Invalid reg in DT");
				return -EINVAL;
			}
			xcsc->vip_formats[port_id] = vip_format;
		}
	}
	/* Reset GPIO */
	xcsc->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xcsc->rst_gpio)) {
		if (PTR_ERR(xcsc->rst_gpio) != -EPROBE_DEFER)
			dev_err(dev, "Reset GPIO not setup in DT");
		return PTR_ERR(xcsc->rst_gpio);
	}
	return 0;
}

static int xcsc_probe(struct platform_device *pdev)
{
	struct xcsc_dev *xcsc;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *def_fmt;
	int rval, itr;

	dev_dbg(&pdev->dev, "VPSS CSC Probe Started");
	xcsc = devm_kzalloc(&pdev->dev, sizeof(*xcsc), GFP_KERNEL);
	if (!xcsc)
		return -ENOMEM;

	xcsc->xvip.dev = &pdev->dev;

	rval = xcsc_parse_of(xcsc);
	if (rval < 0)
		return rval;

	/* Reset and initialize the core */
	dev_dbg(xcsc->xvip.dev, "Reset VPSS CSC");
	/* Use GPIO to bring IP out of reset */
	gpiod_set_value_cansleep(xcsc->rst_gpio, 0x0);
	usleep_range(150, 200);

	rval = xvip_init_resources(&xcsc->xvip);

	if (rval < 0)
		return rval;

	/* Init v4l2 subdev */
	subdev = &xcsc->xvip.subdev;
	v4l2_subdev_init(subdev, &xcsc_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xcsc_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xcsc);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Default Formats Initialization */
	def_fmt = &xcsc->default_formats[XVIP_PAD_SINK];
	def_fmt->code = xcsc->vip_formats[XVIP_PAD_SINK]->code;
	/* Sink only supports RGB888 */
	if (xcsc->vip_formats[XVIP_PAD_SINK]->code !=
				MEDIA_BUS_FMT_RBG888_1X24) {
		def_fmt->code = MEDIA_BUS_FMT_RBG888_1X24;
		xcsc->cft_in = XVIDC_CSF_RGB;
	}
	def_fmt->field = V4L2_FIELD_NONE;
	def_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	def_fmt->width = XV_CSC_DEFAULT_WIDTH;
	def_fmt->height = XV_CSC_DEFAULT_HEIGHT;
	xcsc->formats[XVIP_PAD_SINK] = *def_fmt;

	/* Source supports only YUV 444 or RGB 888 */
	def_fmt = &xcsc->default_formats[XVIP_PAD_SOURCE];
	*def_fmt = xcsc->default_formats[XVIP_PAD_SINK];
	def_fmt->code = xcsc->vip_formats[XVIP_PAD_SOURCE]->code;
	if (def_fmt->code == MEDIA_BUS_FMT_VUY8_1X24) {
		xcsc->cft_out = XVIDC_CSF_YCRCB_444;
	} else if (def_fmt->code == MEDIA_BUS_FMT_RBG888_1X24) {
		xcsc->cft_out = XVIDC_CSF_RGB;
	} else {
		/* Default to RGB */
		xcsc->cft_out = XVIDC_CSF_RGB;
		def_fmt->code = MEDIA_BUS_FMT_RBG888_1X24;
	}
	def_fmt->width = XV_CSC_DEFAULT_WIDTH;
	def_fmt->height = XV_CSC_DEFAULT_HEIGHT;
	xcsc->formats[XVIP_PAD_SOURCE] = *def_fmt;

	xcsc->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xcsc->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	xcsc_set_default_state(xcsc);

	/* Init Media Entity */
	subdev->entity.ops = &xcsc_media_ops;
	rval = media_entity_pads_init(&subdev->entity, 2, xcsc->pads);
	if (rval < 0)
		goto media_error;

	v4l2_ctrl_handler_init(&xcsc->ctrl_handler, ARRAY_SIZE(xcsc_ctrls));
	for (itr = 0; itr < ARRAY_SIZE(xcsc_ctrls); itr++) {
		v4l2_ctrl_new_custom(&xcsc->ctrl_handler,
				     &xcsc_ctrls[itr], NULL);
	}
	if (xcsc->ctrl_handler.error) {
		dev_err(&pdev->dev, "Failed to add  v4l2 controls");
		rval = xcsc->ctrl_handler.error;
		goto ctrl_error;
	}
	subdev->ctrl_handler = &xcsc->ctrl_handler;
	rval = v4l2_ctrl_handler_setup(&xcsc->ctrl_handler);
	if (rval < 0) {
		dev_err(xcsc->xvip.dev, "Failed to setup control handler");
		goto ctrl_error;
	}

	platform_set_drvdata(pdev, xcsc);
	rval = v4l2_async_register_subdev(subdev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto ctrl_error;
	}
	dev_info(&pdev->dev, "VPSS CSC Probe Successful");
	return 0;
ctrl_error:
	v4l2_ctrl_handler_free(&xcsc->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
media_error:
	xvip_cleanup_resources(&xcsc->xvip);
	return rval;
}

static int xcsc_remove(struct platform_device *pdev)
{
	struct xcsc_dev *xcsc = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xcsc->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xcsc->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xcsc->xvip);
	return 0;
}

static const struct of_device_id xcsc_of_id_table[] = {
	{.compatible = "xlnx,v-vpss-csc"},
	{ }
};

MODULE_DEVICE_TABLE(of, xcsc_of_id_table);

static struct platform_driver xcsc_driver = {
	.driver = {
		.name = "xilinx-vpss-csc",
		.of_match_table = xcsc_of_id_table,
	},
	.probe = xcsc_probe,
	.remove = xcsc_remove,
};

module_platform_driver(xcsc_driver);
MODULE_DESCRIPTION("Xilinx VPSS CSC Driver");
MODULE_LICENSE("GPL v2");

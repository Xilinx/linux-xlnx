/*
 * Xilinx VPSS Color Space Converter
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

#include "xilinx-vip.h"

#define XV_CSC_AP_CTRL					(0x000)
#define XV_CSC_INVIDEOFORMAT				(0x010)
#define XV_CSC_OUTVIDEOFORMAT				(0x018)
#define XV_CSC_WIDTH					(0x020)
#define XV_CSC_HEIGHT					(0x028)
#define XV_CSC_K11					(0x050)
#define XV_CSC_K12					(0x058)
#define XV_CSC_K13					(0x060)
#define XV_CSC_K21					(0x068)
#define XV_CSC_K22					(0x070)
#define XV_CSC_K23					(0x078)
#define XV_CSC_K31					(0x080)
#define XV_CSC_K32					(0x088)
#define XV_CSC_K33					(0x090)
#define XV_CSC_ROFFSET					(0x098)
#define XV_CSC_GOFFSET					(0x0a0)
#define XV_CSC_BOFFSET					(0x0a8)
#define XV_CSC_CLAMPMIN					(0x0b0)
#define XV_CSC_CLIPMAX					(0x0b8)

#define XV_CSC_FRACTIONAL_BITS	(12)
#define XV_CSC_SCALE_FACTOR	(4096)
/* This a VPSS CSC specific macro used to calculate Contrast */
#define XV_CSC_DIVISOR		(10000)
#define XV_CSC_DEFAULT_HEIGHT	(720)
#define XV_CSC_DEFAULT_WIDTH	(1280)
#define XV_CSC_K_MAX_ROWS	(3)
#define XV_CSC_K_MAX_COLUMNS	(3)
#define XV_CSC_MIN_WIDTH	(64)
#define XV_CSC_MAX_WIDTH	(8192)
#define XV_CSC_MIN_HEIGHT	(64)
#define XV_CSC_MAX_HEIGHT	(4320)

/* GPIO Reset Assert/De-assert */
#define XCSC_RESET_ASSERT	(1)
#define XCSC_RESET_DEASSERT	(0)
/* Streaming Macros */
#define XCSC_CLAMP_MIN_ZERO	(0)
#define XCSC_AP_START		BIT(0)
#define XCSC_AP_AUTO_RESTART	BIT(7)
#define XCSC_STREAM_ON	(XCSC_AP_START | XCSC_AP_AUTO_RESTART)
/* Color Control Macros */
#define XCSC_COLOR_CTRL_COUNT		(5)
#define XCSC_COLOR_CTRL_DEFAULT		(50)

enum xcsc_color_fmt {
	XVIDC_CSF_RGB = 0,
	XVIDC_CSF_YCRCB_444,
	XVIDC_CSF_YCRCB_422,
	XVIDC_CSF_YCRCB_420,
};

enum xcsc_output_range {
	XVIDC_CR_0_255 = 1,
	XVIDC_CR_16_240,
	XVIDC_CR_16_235
};

enum xcsc_color_depth {
	XVIDC_BPC_8 = 8,
	XVIDC_BPC_10 = 10,
};

static const s32
rgb_unity_matrix[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1] = {
	{XV_CSC_SCALE_FACTOR, 0, 0, 0},
	{0, XV_CSC_SCALE_FACTOR, 0, 0},
	{0, 0, XV_CSC_SCALE_FACTOR, 0},
};

static const s32
ycrcb_to_rgb_unity[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1] = {
	{
	 11644 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 0,
	 17927 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 0
	},
	{
	 11644 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 -2132 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 -5329 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 0
	},
	{
	 11644 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 21124 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 0,
	 0
	},
};

static const s32
rgb_to_ycrcb_unity[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1] = {
	{
	 1826 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 6142 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 620 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 0
	},
	{
	 -1006 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 -3386 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 4392 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 0
	},
	{
	 4392 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 -3989 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 -403 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR,
	 0,
	},
};

/**
 * struct xcsc_dev - xilinx vpss csc device structure
 * @xvip: Xilinx Video IP core struct
 * @pads: Media bus pads for VPSS CSC
 * @formats: Current media bus formats
 * @default_formats: Default media bus formats for VPSS CSC
 * @vip_formats: Pointer to DT specified media bus code info
 * @ctrl_handler: V4L2 Control Handler struct
 * @custom_ctrls: Array of pointers to various custom controls
 * @cft_in: IP or Hardware specific input video format
 * @cft_out: IP or Hardware specific output video format
 * @output_range: Color range for Outgoing video
 * @color_depth: Data width used to represent color
 * @brightness: Expected brightness value
 * @contrast: Expected contrast value
 * @red_gain: Expected red gain
 * @green_gain: Expect green gain
 * @blue_gain: Expected blue gain
 * @brightness_active: Current brightness value
 * @contrast_active: Current contrast value
 * @red_gain_active: Current red gain
 * @green_gain_active: Current green gain
 * @blue_gain_active: Current blue gain
 * @k_hw : Coefficients to be written to IP/Hardware
 * @shadow_coeff: Coefficients to track RGB equivalents for color controls
 * @clip_max: Maximum value to clip output color range
 * @rst_gpio: Handle to PS GPIO specifier to assert/de-assert the reset line
 * @max_width: Maximum width supported by IP.
 * @max_height: Maximum height supported by IP.
 */
struct xcsc_dev {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_formats[2];
	const struct xvip_video_format *vip_formats[2];
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *custom_ctrls[XCSC_COLOR_CTRL_COUNT];

	enum xcsc_color_fmt cft_in;
	enum xcsc_color_fmt cft_out;
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
	s32 k_hw[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1];
	s32 shadow_coeff[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1];
	s32 clip_max;
	struct gpio_desc *rst_gpio;
	u32 max_width;
	u32 max_height;
};

#ifdef DEBUG
static u32 xcsc_read(struct xcsc_dev *xcsc, u32 reg)
{
	u32 data;

	data = xvip_read(&xcsc->xvip, reg);
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

	dev_info(xcsc->xvip.dev,
		 "-------------CSC Coeff Dump Start------\n");
	dev_info(xcsc->xvip.dev,
		 " R row : %5d  %5d  %5d\n",
		 (s16)C[0][0], (s16)C[0][1], (s16)C[0][2]);
	dev_info(xcsc->xvip.dev,
		 " G row : %5d  %5d  %5d\n",
		 (s16)C[1][0], (s16)C[1][1], (s16)C[1][2]);
	dev_info(xcsc->xvip.dev,
		 " B row : %5d  %5d  %5d\n",
		 (s16)C[2][0], (s16)C[2][1], (s16)C[2][2]);
	dev_info(xcsc->xvip.dev,
		 "Offset : %5d  %5d  %5d\n",
		 (s16)C[0][3], (s16)C[1][3], (s16)C[2][3]);
	dev_info(xcsc->xvip.dev,
		 "ClampMin: %3d  ClipMax %3d",
		 xcsc_read(xcsc, XV_CSC_CLAMPMIN),
		 xcsc_read(xcsc, XV_CSC_CLIPMAX));
	dev_info(xcsc->xvip.dev,
		 "-------------CSC Coeff Dump Stop-------\n");
}

static void
xcsc_log_coeff(struct device *dev,
	       s32 coeff[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1])
{
	if (!dev)
		return;
	dev_dbg(dev, "--- %s : Start Coeff Log ---", __func__);
	dev_dbg(dev, "R row : %5d  %5d  %5d\n",
		coeff[0][0], coeff[0][1], coeff[0][2]);
	dev_dbg(dev, "G row : %5d  %5d  %5d\n",
		coeff[1][0], coeff[1][1], coeff[1][2]);
	dev_dbg(dev, "B row : %5d  %5d  %5d\n",
		coeff[2][0], coeff[2][1], coeff[2][2]);
	dev_dbg(dev, "Offset: %5d  %5d  %5d\n",
		coeff[0][3], coeff[1][3], coeff[2][3]);
	dev_dbg(dev, "---  %s : Stop Coeff Log ---", __func__);
}

static void xcsc_print_k_hw(struct xcsc_dev *xcsc)
{
	dev_dbg(xcsc->xvip.dev,
		"-------------CSC Driver k_hw[][] Dump------------\n");
	xcsc_log_coeff(xcsc->xvip.dev, xcsc->k_hw);
	dev_dbg(xcsc->xvip.dev,
		"-------------------------------------------------\n");
}
#endif /* DEBUG */

static void xcsc_write(struct xcsc_dev *xcsc, u32 reg, u32 data)
{
	xvip_write(&xcsc->xvip, reg, data);
}

static void xcsc_write_rgb_3x3(struct xcsc_dev *xcsc)
{
	/* Write Matrix Coefficients */
	xcsc_write(xcsc, XV_CSC_K11, xcsc->k_hw[0][0]);
	xcsc_write(xcsc, XV_CSC_K12, xcsc->k_hw[0][1]);
	xcsc_write(xcsc, XV_CSC_K13, xcsc->k_hw[0][2]);
	xcsc_write(xcsc, XV_CSC_K21, xcsc->k_hw[1][0]);
	xcsc_write(xcsc, XV_CSC_K22, xcsc->k_hw[1][1]);
	xcsc_write(xcsc, XV_CSC_K23, xcsc->k_hw[1][2]);
	xcsc_write(xcsc, XV_CSC_K31, xcsc->k_hw[2][0]);
	xcsc_write(xcsc, XV_CSC_K32, xcsc->k_hw[2][1]);
	xcsc_write(xcsc, XV_CSC_K33, xcsc->k_hw[2][2]);
}

static void xcsc_write_rgb_offset(struct xcsc_dev *xcsc)
{
	/* Write RGB Offsets */
	xcsc_write(xcsc, XV_CSC_ROFFSET, xcsc->k_hw[0][3]);
	xcsc_write(xcsc, XV_CSC_GOFFSET, xcsc->k_hw[1][3]);
	xcsc_write(xcsc, XV_CSC_BOFFSET, xcsc->k_hw[2][3]);
}

static void xcsc_write_coeff(struct xcsc_dev *xcsc)
{
	xcsc_write_rgb_3x3(xcsc);
	xcsc_write_rgb_offset(xcsc);
}

static void xcsc_set_v4l2_ctrl_defaults(struct xcsc_dev *xcsc)
{
	unsigned int i;

	mutex_lock(xcsc->ctrl_handler.lock);
	for (i = 0; i < XCSC_COLOR_CTRL_COUNT; i++)
		xcsc->custom_ctrls[i]->cur.val = XCSC_COLOR_CTRL_DEFAULT;
	mutex_unlock(xcsc->ctrl_handler.lock);
}

static void xcsc_set_control_defaults(struct xcsc_dev *xcsc)
{
	/* These are VPSS CSC IP specific defaults */
	xcsc->brightness = 120;
	xcsc->contrast = 0;
	xcsc->red_gain = 120;
	xcsc->blue_gain = 120;
	xcsc->green_gain = 120;
	xcsc->brightness_active	= 120;
	xcsc->contrast_active = 0;
	xcsc->red_gain_active = 120;
	xcsc->blue_gain_active = 120;
	xcsc->green_gain_active = 120;
}

static void xcsc_copy_coeff(
	s32 dest[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1],
	s32 const src[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1])
{
	unsigned int i, j;

	for (i = 0; i < XV_CSC_K_MAX_ROWS; i++)
		for (j = 0; j < XV_CSC_K_MAX_COLUMNS + 1; j++)
			memcpy(&dest[i][j], &src[i][j], sizeof(dest[0][0]));
}

static void xcsc_set_unity_matrix(struct xcsc_dev *xcsc)
{
	xcsc_copy_coeff(xcsc->k_hw, rgb_unity_matrix);
	xcsc_copy_coeff(xcsc->shadow_coeff, rgb_unity_matrix);
}

static void xcsc_set_default_state(struct xcsc_dev *xcsc)
{
	xcsc->cft_in = XVIDC_CSF_RGB;
	xcsc->cft_out = XVIDC_CSF_RGB;
	xcsc->output_range = XVIDC_CR_0_255;
	/* Needed to add 10, 12 and 16 bit color depth support */
	xcsc->clip_max = BIT(xcsc->color_depth) - 1;
	xcsc_set_control_defaults(xcsc);
	xcsc_set_unity_matrix(xcsc);
	xcsc_write(xcsc, XV_CSC_INVIDEOFORMAT, xcsc->cft_in);
	xcsc_write(xcsc, XV_CSC_OUTVIDEOFORMAT, xcsc->cft_out);
	xcsc_write_coeff(xcsc);
	xcsc_write(xcsc, XV_CSC_CLIPMAX, xcsc->clip_max);
	xcsc_write(xcsc, XV_CSC_CLAMPMIN, XCSC_CLAMP_MIN_ZERO);
}

static void
xcsc_ycrcb_to_rgb(struct xcsc_dev *xcsc, s32 *clip_max,
		  s32 temp[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1])
{
	u16 bpc_scale = BIT(xcsc->color_depth - 8);

	/*
	 * See http://graficaobscura.com/matrix/index.html for
	 * how these numbers are derived. The VPSS CSC IP is
	 * derived from this Matrix style algorithm. And the
	 * 'magic' numbers here are derived from the algorithm.
	 *
	 * XV_CSC_DIVISOR is used to help with floating constants
	 * while performing multiplicative operations
	 *
	 * Coefficients valid only for BT 709
	 */
	dev_dbg(xcsc->xvip.dev, "Performing YCrCb to RGB BT 709");
	xcsc_copy_coeff(temp, ycrcb_to_rgb_unity);
	temp[0][3] = -248 * bpc_scale;
	temp[1][3] = 77 * bpc_scale;
	temp[2][3] = -289 * bpc_scale;
	*clip_max = BIT(xcsc->color_depth) - 1;
}

static void
xcsc_matrix_multiply(s32 K1[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1],
		     s32 K2[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1],
		     s32 kout[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1])
{
	s32 A, B, C, D, E, F, G, H, I, J, K, L, M, N;
	s32 O, P, Q, R, S, T, U, V, W, X;

	A = K1[0][0]; B = K1[0][1]; C = K1[0][2]; J = K1[0][3];
	D = K1[1][0]; E = K1[1][1]; F = K1[1][2]; K = K1[1][3];
	G = K1[2][0]; H = K1[2][1]; I = K1[2][2]; L = K1[2][3];

	M = K2[0][0]; N = K2[0][1]; O = K2[0][2]; V = K2[0][3];
	P = K2[1][0]; Q = K2[1][1]; R = K2[1][2]; W = K2[1][3];
	S = K2[2][0]; T = K2[2][1]; U = K2[2][2]; X = K2[2][3];

	kout[0][0] = (M * A + N * D + O * G) / XV_CSC_SCALE_FACTOR;
	kout[0][1] = (M * B + N * E + O * H) / XV_CSC_SCALE_FACTOR;
	kout[0][2] = (M * C + N * F + O * I) / XV_CSC_SCALE_FACTOR;
	kout[1][0] = (P * A + Q * D + R * G) / XV_CSC_SCALE_FACTOR;
	kout[1][1] = (P * B + Q * E + R * H) / XV_CSC_SCALE_FACTOR;
	kout[1][2] = (P * C + Q * F + R * I) / XV_CSC_SCALE_FACTOR;
	kout[2][0] = (S * A + T * D + U * G) / XV_CSC_SCALE_FACTOR;
	kout[2][1] = (S * B + T * E + U * H) / XV_CSC_SCALE_FACTOR;
	kout[2][2] = (S * C + T * F + U * I) / XV_CSC_SCALE_FACTOR;
	kout[0][3] = ((M * J + N * K + O * L) / XV_CSC_SCALE_FACTOR) + V;
	kout[1][3] = ((P * J + Q * K + R * L) / XV_CSC_SCALE_FACTOR) + W;
	kout[2][3] = ((S * J + T * K + U * L) / XV_CSC_SCALE_FACTOR) + X;
}

static void
xcsc_rgb_to_ycrcb(struct xcsc_dev *xcsc, s32 *clip_max,
		  s32 temp[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1])
{
	u16 bpc_scale = BIT(xcsc->color_depth - 8);

	/*
	 * See http://graficaobscura.com/matrix/index.html for
	 * how these numbers are derived. The VPSS CSC IP is
	 * derived from this Matrix style algorithm. And the
	 * 'magic' numbers here are derived from the algorithm.
	 *
	 * XV_CSC_DIVISOR is used to help with floating constants
	 * while performing multiplicative operations
	 *
	 * Coefficients valid only for BT 709
	 */
	dev_dbg(xcsc->xvip.dev, "Performing RGB to YCrCb BT 709");
	xcsc_copy_coeff(temp, rgb_to_ycrcb_unity);
	temp[0][3] = 16 * bpc_scale;
	temp[1][3] = 128 * bpc_scale;
	temp[2][3] = 128 * bpc_scale;
	*clip_max = BIT(xcsc->color_depth) - 1;
}

static int xcsc_update_formats(struct xcsc_dev *xcsc)
{
	u32 color_in, color_out;

	/* Write In and Out Video Formats */
	color_in = xcsc->formats[XVIP_PAD_SINK].code;
	color_out = xcsc->formats[XVIP_PAD_SOURCE].code;

	switch (color_in) {
	case MEDIA_BUS_FMT_RBG888_1X24:
	case MEDIA_BUS_FMT_RBG101010_1X30:
		dev_dbg(xcsc->xvip.dev, "Media Format In : RGB");
		xcsc->cft_in = XVIDC_CSF_RGB;
		break;
	case MEDIA_BUS_FMT_VUY8_1X24:
	case MEDIA_BUS_FMT_VUY10_1X30:
		dev_dbg(xcsc->xvip.dev, "Media Format In : YUV 444");
		xcsc->cft_in = XVIDC_CSF_YCRCB_444;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYVY10_1X20:
		dev_dbg(xcsc->xvip.dev, "Media Format In : YUV 422");
		xcsc->cft_in = XVIDC_CSF_YCRCB_422;
		break;
	case MEDIA_BUS_FMT_VYYUYY8_1X24:
	case MEDIA_BUS_FMT_VYYUYY10_4X20:
		dev_dbg(xcsc->xvip.dev, "Media Format In : YUV 420");
		xcsc->cft_in = XVIDC_CSF_YCRCB_420;
		break;
	}

	switch (color_out) {
	case MEDIA_BUS_FMT_RBG888_1X24:
	case MEDIA_BUS_FMT_RBG101010_1X30:
		xcsc->cft_out = XVIDC_CSF_RGB;
		dev_dbg(xcsc->xvip.dev, "Media Format Out : RGB");
		if (color_in != MEDIA_BUS_FMT_RBG888_1X24)
			xcsc_ycrcb_to_rgb(xcsc, &xcsc->clip_max, xcsc->k_hw);
		else
			xcsc_set_unity_matrix(xcsc);
		break;
	case MEDIA_BUS_FMT_VUY8_1X24:
	case MEDIA_BUS_FMT_VUY10_1X30:
		xcsc->cft_out = XVIDC_CSF_YCRCB_444;
		dev_dbg(xcsc->xvip.dev, "Media Format Out : YUV 444");
		if (color_in == MEDIA_BUS_FMT_RBG888_1X24)
			xcsc_rgb_to_ycrcb(xcsc, &xcsc->clip_max, xcsc->k_hw);
		else
			xcsc_set_unity_matrix(xcsc);
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYVY10_1X20:
		xcsc->cft_out = XVIDC_CSF_YCRCB_422;
		dev_dbg(xcsc->xvip.dev, "Media Format Out : YUV 422");
		if (color_in == MEDIA_BUS_FMT_RBG888_1X24)
			xcsc_rgb_to_ycrcb(xcsc, &xcsc->clip_max, xcsc->k_hw);
		else
			xcsc_set_unity_matrix(xcsc);
		break;
	case MEDIA_BUS_FMT_VYYUYY8_1X24:
	case MEDIA_BUS_FMT_VYYUYY10_4X20:
		xcsc->cft_out = XVIDC_CSF_YCRCB_420;
		dev_dbg(xcsc->xvip.dev, "Media Format Out : YUV 420");
		if (color_in ==  MEDIA_BUS_FMT_RBG888_1X24)
			xcsc_rgb_to_ycrcb(xcsc, &xcsc->clip_max, xcsc->k_hw);
		else
			xcsc_set_unity_matrix(xcsc);
		break;
	}

	xcsc_write(xcsc, XV_CSC_INVIDEOFORMAT, xcsc->cft_in);
	xcsc_write(xcsc, XV_CSC_OUTVIDEOFORMAT, xcsc->cft_out);

	xcsc_write_coeff(xcsc);

	xcsc_write(xcsc, XV_CSC_CLIPMAX, xcsc->clip_max);
	xcsc_write(xcsc, XV_CSC_CLAMPMIN, XCSC_CLAMP_MIN_ZERO);
#ifdef DEBUG
	xcsc_print_k_hw(xcsc);
	xcsc_print_coeff(xcsc);
#endif
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
	struct v4l2_mbus_framefmt *format;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		format = v4l2_subdev_get_try_format(&xcsc->xvip.subdev,
						    cfg, pad);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		format = &xcsc->formats[pad];
		break;
	default:
		format = NULL;
		break;
	}

	return format;
}

static void
xcsc_correct_coeff(struct xcsc_dev *xcsc,
		   s32 temp[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1])
{
	s32 csc_change[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1] = { {0} };
	s32 csc_extra[XV_CSC_K_MAX_ROWS][XV_CSC_K_MAX_COLUMNS + 1] = { {0} };
	u32 mbus_in = xcsc->formats[XVIP_PAD_SINK].code;
	u32 mbus_out = xcsc->formats[XVIP_PAD_SOURCE].code;

#ifdef DEBUG
	xcsc_log_coeff(xcsc->xvip.dev, temp);
#endif
	if (mbus_in == MEDIA_BUS_FMT_RBG888_1X24 && mbus_out == mbus_in) {
		dev_dbg(xcsc->xvip.dev, "%s : RGB to RGB", __func__);
		xcsc_copy_coeff(xcsc->k_hw,
				(const s32 (*)[XV_CSC_K_MAX_COLUMNS + 1])temp);
	} else if (mbus_in == MEDIA_BUS_FMT_RBG888_1X24 &&
		   mbus_out != MEDIA_BUS_FMT_RBG888_1X24) {
		dev_dbg(xcsc->xvip.dev, "%s : RGB to YUV", __func__);
		xcsc_rgb_to_ycrcb(xcsc, &xcsc->clip_max, csc_change);
		xcsc_matrix_multiply(temp, csc_change, xcsc->k_hw);
	} else if (mbus_in != MEDIA_BUS_FMT_RBG888_1X24 &&
		   mbus_out == MEDIA_BUS_FMT_RBG888_1X24) {
		dev_dbg(xcsc->xvip.dev, "%s : YUV to RGB", __func__);
		xcsc_ycrcb_to_rgb(xcsc, &xcsc->clip_max, csc_change);
		xcsc_matrix_multiply(csc_change, temp, xcsc->k_hw);
	} else if (mbus_in != MEDIA_BUS_FMT_RBG888_1X24 &&
		   mbus_out != MEDIA_BUS_FMT_RBG888_1X24) {
		dev_dbg(xcsc->xvip.dev, "%s : YUV to YUV", __func__);
		xcsc_ycrcb_to_rgb(xcsc, &xcsc->clip_max, csc_change);
		xcsc_matrix_multiply(csc_change, temp, csc_extra);
		xcsc_rgb_to_ycrcb(xcsc, &xcsc->clip_max, csc_change);
		xcsc_matrix_multiply(csc_extra, csc_change, xcsc->k_hw);
	} else {
		/* Should never get here */
		WARN_ON(1);
	}
}

static void xcsc_set_brightness(struct xcsc_dev *xcsc)
{
	unsigned int i, j;

	dev_dbg(xcsc->xvip.dev,
		"%s : Brightness %d Brightness Active %d",
		__func__,
		((xcsc->brightness - 20) / 2),
		((xcsc->brightness_active - 20) / 2));
	if (xcsc->brightness == xcsc->brightness_active)
		return;
	for (i = 0; i < XV_CSC_K_MAX_ROWS; i++) {
		for (j = 0; j < XV_CSC_K_MAX_COLUMNS; j++) {
			xcsc->shadow_coeff[i][j] = (xcsc->shadow_coeff[i][j] *
						    xcsc->brightness) /
						    xcsc->brightness_active;
		}
	}
	xcsc->brightness_active = xcsc->brightness;
	xcsc_correct_coeff(xcsc, xcsc->shadow_coeff);
	xcsc_write_coeff(xcsc);
}

static void xcsc_set_contrast(struct xcsc_dev *xcsc)
{
	s32 contrast;
	u8 scale = BIT(xcsc->color_depth - 8);

	contrast = xcsc->contrast - xcsc->contrast_active;
	dev_dbg(xcsc->xvip.dev,
		"%s : Contrast Difference %d scale = %d",
		__func__, contrast, scale);
	/* Avoid updates if same */
	if (!contrast)
		return;
	/* Update RGB Offsets */
	xcsc->shadow_coeff[0][3] += contrast * scale;
	xcsc->shadow_coeff[1][3] += contrast * scale;
	xcsc->shadow_coeff[2][3] += contrast * scale;
	xcsc->contrast_active = xcsc->contrast;
	xcsc_correct_coeff(xcsc, xcsc->shadow_coeff);
	xcsc_write_coeff(xcsc);
}

static void xcsc_set_red_gain(struct xcsc_dev *xcsc)
{
	dev_dbg(xcsc->xvip.dev,
		"%s: Red Gain %d Red Gain Active %d", __func__,
		(xcsc->red_gain - 20) / 2,
		(xcsc->red_gain_active - 20) / 2);

	if (xcsc->red_gain != xcsc->red_gain_active) {
		xcsc->shadow_coeff[0][0] = (xcsc->shadow_coeff[0][0] *
					    xcsc->red_gain) /
					    xcsc->red_gain_active;
		xcsc->shadow_coeff[0][1] = (xcsc->shadow_coeff[0][1] *
					    xcsc->red_gain) /
					    xcsc->red_gain_active;
		xcsc->shadow_coeff[0][2] = (xcsc->shadow_coeff[0][2] *
					    xcsc->red_gain) /
					    xcsc->red_gain_active;
		xcsc->red_gain_active = xcsc->red_gain;
		xcsc_correct_coeff(xcsc, xcsc->shadow_coeff);
		xcsc_write_coeff(xcsc);
	}
}

static void xcsc_set_green_gain(struct xcsc_dev *xcsc)
{
	dev_dbg(xcsc->xvip.dev,
		"%s: Green Gain %d Green Gain Active %d", __func__,
		 (xcsc->green_gain - 20) / 2,
		 (xcsc->green_gain_active - 20) / 2);

	if (xcsc->green_gain != xcsc->green_gain_active) {
		xcsc->shadow_coeff[1][0] = (xcsc->shadow_coeff[1][0] *
					    xcsc->green_gain) /
					    xcsc->green_gain_active;
		xcsc->shadow_coeff[1][1] = (xcsc->shadow_coeff[1][1] *
					    xcsc->green_gain) /
					    xcsc->green_gain_active;
		xcsc->shadow_coeff[1][2] = (xcsc->shadow_coeff[1][2] *
					    xcsc->green_gain) /
					    xcsc->green_gain_active;
		xcsc->green_gain_active = xcsc->green_gain;
		xcsc_correct_coeff(xcsc, xcsc->shadow_coeff);
		xcsc_write_coeff(xcsc);
	}
}

static void xcsc_set_blue_gain(struct xcsc_dev *xcsc)
{
	dev_dbg(xcsc->xvip.dev,
		"%s: Blue Gain %d Blue Gain Active %d", __func__,
		 (xcsc->blue_gain - 20) / 2,
		 (xcsc->blue_gain_active - 20) / 2);

	if (xcsc->blue_gain != xcsc->blue_gain_active) {
		xcsc->shadow_coeff[2][0] = (xcsc->shadow_coeff[2][0] *
					    xcsc->blue_gain) /
					     xcsc->blue_gain_active;
		xcsc->shadow_coeff[2][1] = (xcsc->shadow_coeff[2][1] *
					    xcsc->blue_gain) /
					     xcsc->blue_gain_active;
		xcsc->shadow_coeff[2][2] = (xcsc->shadow_coeff[2][2] *
					    xcsc->blue_gain) /
					     xcsc->blue_gain_active;
		xcsc->blue_gain_active = xcsc->blue_gain;
		xcsc_correct_coeff(xcsc, xcsc->shadow_coeff);
		xcsc_write_coeff(xcsc);
	}
}

static void xcsc_set_size(struct xcsc_dev *xcsc)
{
	u32 width, height;

	width = xcsc->formats[XVIP_PAD_SINK].width;
	height = xcsc->formats[XVIP_PAD_SINK].height;
	dev_dbg(xcsc->xvip.dev, "%s : Setting width %d and height %d",
		__func__, width, height);
	xcsc_write(xcsc, XV_CSC_WIDTH, width);
	xcsc_write(xcsc, XV_CSC_HEIGHT, height);
}

static int xcsc_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xcsc_dev *xcsc = to_csc(subdev);

	dev_dbg(xcsc->xvip.dev, "%s : Stream %s", __func__,
		enable ? "On" : "Off");
	if (!enable) {
		/* Reset the Global IP Reset through PS GPIO */
		gpiod_set_value_cansleep(xcsc->rst_gpio, XCSC_RESET_ASSERT);
		gpiod_set_value_cansleep(xcsc->rst_gpio, XCSC_RESET_DEASSERT);
		return 0;
	}
	xcsc_write(xcsc, XV_CSC_INVIDEOFORMAT, xcsc->cft_in);
	xcsc_write(xcsc, XV_CSC_OUTVIDEOFORMAT, xcsc->cft_out);
	xcsc_write(xcsc, XV_CSC_CLIPMAX, xcsc->clip_max);
	xcsc_write(xcsc, XV_CSC_CLAMPMIN, XCSC_CLAMP_MIN_ZERO);
	xcsc_set_size(xcsc);
	xcsc_write_coeff(xcsc);
#ifdef DEBUG
	xcsc_print_coeff(xcsc);
	dev_dbg(xcsc->xvip.dev, "cft_in = %d cft_out = %d",
		xcsc_read(xcsc, XV_CSC_INVIDEOFORMAT),
		xcsc_read(xcsc, XV_CSC_OUTVIDEOFORMAT));
	dev_dbg(xcsc->xvip.dev, "clipmax = %d clampmin = %d",
		xcsc_read(xcsc, XV_CSC_CLIPMAX),
		xcsc_read(xcsc, XV_CSC_CLAMPMIN));
	dev_dbg(xcsc->xvip.dev, "height = %d width = %d",
		xcsc_read(xcsc, XV_CSC_HEIGHT),
		xcsc_read(xcsc, XV_CSC_WIDTH));
#endif
	/* Start VPSS CSC IP */
	xcsc_write(xcsc, XV_CSC_AP_CTRL, XCSC_STREAM_ON);
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
	struct v4l2_mbus_framefmt *format;

	format = __xcsc_get_pad_format(xcsc, cfg, fmt->pad, fmt->which);
	if (!format)
		return -EINVAL;

	fmt->format = *format;
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
	if (!__format)
		return -EINVAL;

	/* Propagate to Source Pad */
	__propagate = __xcsc_get_pad_format(xcsc, cfg,
					    XVIP_PAD_SOURCE, fmt->which);
	if (!__propagate)
		return -EINVAL;

	*__format = fmt->format;

	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XV_CSC_MIN_WIDTH, xcsc->max_width);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XV_CSC_MIN_HEIGHT, xcsc->max_height);

	switch (__format->code) {
	case MEDIA_BUS_FMT_VUY8_1X24:
	case MEDIA_BUS_FMT_RBG888_1X24:
	case MEDIA_BUS_FMT_RBG101010_1X30:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYYUYY8_1X24:
	case MEDIA_BUS_FMT_VYYUYY10_4X20:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_VUY10_1X30:
		break;
	default:
		/* Unsupported Format. Default to RGB */
		__format->code = MEDIA_BUS_FMT_RBG888_1X24;
		return -EINVAL;
	}

	/* Always propagate Sink image size to Source */
	__propagate->width  = __format->width;
	__propagate->height = __format->height;

	fmt->format = *__format;
	xcsc_update_formats(xcsc);
	xcsc_set_control_defaults(xcsc);
	xcsc_set_v4l2_ctrl_defaults(xcsc);
	dev_info(xcsc->xvip.dev, "VPSS CSC color controls reset to defaults");
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
#ifdef DEBUG
	xcsc_print_k_hw(xcsc);
	xcsc_print_coeff(xcsc);
#endif
	return 0;
}

static const struct v4l2_ctrl_ops xcsc_ctrl_ops = {
	.s_ctrl = xcsc_s_ctrl,
};

static struct v4l2_ctrl_config xcsc_color_ctrls[XCSC_COLOR_CTRL_COUNT] = {
	/* Brightness */
	{
		.ops = &xcsc_ctrl_ops,
		.id = V4L2_CID_XILINX_CSC_BRIGHTNESS,
		.name = "CSC Brightness",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 100,
		.step = 1,
		.def = XCSC_COLOR_CTRL_DEFAULT,
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
		.def = XCSC_COLOR_CTRL_DEFAULT,
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
		.def = XCSC_COLOR_CTRL_DEFAULT,
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
		.def = XCSC_COLOR_CTRL_DEFAULT,
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
		.def = XCSC_COLOR_CTRL_DEFAULT,
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
	u32 video_width[2] = {0};

	rval = of_property_read_u32(node, "xlnx,max-height", &xcsc->max_height);
	if (rval < 0) {
		dev_err(dev, "xlnx,max-height is missing!");
		return -EINVAL;
	} else if (xcsc->max_height > XV_CSC_MAX_HEIGHT ||
		   xcsc->max_height < XV_CSC_MIN_HEIGHT) {
		dev_err(dev, "Invalid height in dt");
		return -EINVAL;
	}

	rval = of_property_read_u32(node, "xlnx,max-width", &xcsc->max_width);
	if (rval < 0) {
		dev_err(dev, "xlnx,max-width is missing!");
		return -EINVAL;
	} else if (xcsc->max_width > XV_CSC_MAX_WIDTH ||
		   xcsc->max_width < XV_CSC_MIN_WIDTH) {
		dev_err(dev, "Invalid width in dt");
		return -EINVAL;
	}

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

			rval = of_property_read_u32(port, "xlnx,video-width",
						    &video_width[port_id]);
			if (rval < 0) {
				dev_err(dev,
					"DT Port%d xlnx,video-width not found",
					port_id);
				return rval;
			}
		}
	}
	if (video_width[0] != video_width[1]) {
		dev_err(dev, "Changing video width in DT not supported");
		return -EINVAL;
	}
	switch (video_width[0]) {
	case XVIDC_BPC_8:
		xcsc->color_depth = XVIDC_BPC_8;
		break;
	case XVIDC_BPC_10:
		xcsc->color_depth = XVIDC_BPC_10;
		break;
	default:
		dev_err(dev, "Unsupported color depth %d", video_width[0]);
		return -EINVAL;
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

	xcsc = devm_kzalloc(&pdev->dev, sizeof(*xcsc), GFP_KERNEL);
	if (!xcsc)
		return -ENOMEM;

	xcsc->xvip.dev = &pdev->dev;

	rval = xcsc_parse_of(xcsc);
	if (rval < 0)
		return rval;

	/* Reset and initialize the core */
	gpiod_set_value_cansleep(xcsc->rst_gpio, XCSC_RESET_DEASSERT);
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
	xcsc_set_default_state(xcsc);
	def_fmt = &xcsc->default_formats[XVIP_PAD_SINK];
	def_fmt->code = xcsc->vip_formats[XVIP_PAD_SINK]->code;
	def_fmt->field = V4L2_FIELD_NONE;
	def_fmt->colorspace = V4L2_COLORSPACE_REC709;
	def_fmt->width = XV_CSC_DEFAULT_WIDTH;
	def_fmt->height = XV_CSC_DEFAULT_HEIGHT;
	xcsc->formats[XVIP_PAD_SINK] = *def_fmt;
	/* Source supports only YUV 444, YUV 422, and RGB */
	def_fmt = &xcsc->default_formats[XVIP_PAD_SOURCE];
	*def_fmt = xcsc->default_formats[XVIP_PAD_SINK];
	def_fmt->code = xcsc->vip_formats[XVIP_PAD_SOURCE]->code;
	def_fmt->width = XV_CSC_DEFAULT_WIDTH;
	def_fmt->height = XV_CSC_DEFAULT_HEIGHT;
	xcsc->formats[XVIP_PAD_SOURCE] = *def_fmt;
	xcsc->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xcsc->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Init Media Entity */
	subdev->entity.ops = &xcsc_media_ops;
	rval = media_entity_pads_init(&subdev->entity, 2, xcsc->pads);
	if (rval < 0)
		goto media_error;
	/* V4L2 Control Setup */
	v4l2_ctrl_handler_init(&xcsc->ctrl_handler,
			       ARRAY_SIZE(xcsc_color_ctrls));
	for (itr = 0; itr < ARRAY_SIZE(xcsc_color_ctrls); itr++) {
		xcsc->custom_ctrls[itr] =
			v4l2_ctrl_new_custom(&xcsc->ctrl_handler,
					     &xcsc_color_ctrls[itr], NULL);
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
	dev_info(&pdev->dev, "VPSS CSC %d-bit Color Depth Probe Successful",
		 xcsc->color_depth);
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

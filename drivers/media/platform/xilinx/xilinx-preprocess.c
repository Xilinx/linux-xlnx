// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 * Author: Mounik Katikala <mounik.katikala@amd.com>
 *
 * The Xilinx Video Preprocess IP core is a hardware block that performs
 * per-channel normalization (alpha/beta parameters) and data type conversion
 * for video streams. It exposes these capabilities through the V4L2 subdev
 * and control frameworks.
 *
 * Channel packing (1 / 3 / 4) is fixed from the first ``xlnx,vid-formats``
 * device-tree string at probe; the source pad rejects other packings and
 * alpha/beta controls are registered only for that count (see DT binding).
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XPREPROCESS_AP_CTRL			0x00
#define XPREPROCESS_IN_IMG_WIDTH_REG		0x10
#define XPREPROCESS_IN_IMG_HEIGHT_REG		0x18
#define XPREPROCESS_DATA_TYPE_REG		0x20
#define XPREPROCESS_OUT_WIDTH_REG		0x40
#define XPREPROCESS_OUT_HEIGHT_REG		0x48
#define XPREPROCESS_PARAMS_REG			0x80
#define XPREPROCESS_PARAM_CHAN_STRIDE		8
#define XPREPROCESS_BETA_OFFSET_WITHIN_CHAN	4
#define XPREPROCESS_PARAM_ALPHA_OFF(n) \
	(XPREPROCESS_PARAMS_REG + (n) * XPREPROCESS_PARAM_CHAN_STRIDE)
#define XPREPROCESS_PARAM_BETA_OFF(n) \
	(XPREPROCESS_PARAMS_REG + (n) * XPREPROCESS_PARAM_CHAN_STRIDE + \
	 XPREPROCESS_BETA_OFFSET_WITHIN_CHAN)

#define XPREPROCESS_MIN_HEIGHT		64
#define XPREPROCESS_MAX_HEIGHT		4320
#define XPREPROCESS_MIN_WIDTH		64
#define XPREPROCESS_MAX_WIDTH		8192

/*
 * Per channel the IP computes (x - alpha) * beta. Beta is unsigned Q0.23:
 * 0 .. 2^23 represents scale 0 .. 1.0; 2^23 (BIT(23)) is unity gain.
 * Alpha spans 0 .. 2^23 * 255 (0x7f800000).
 */
#define XPREPROCESS_PARAM_ALPHA_MAX			0x7f800000
#define XPREPROCESS_PARAM_BETA_MAX			BIT(23)

/* Defaults: no offset, unit scale (beta = 1.0 in Q0.23). */
#define XPREPROCESS_PARAM_ALPHA_DEF			0
#define XPREPROCESS_PARAM_BETA_DEF			BIT(23)

#define XPREPROCESS_INT8			0
#define XPREPROCESS_FP16			1
#define XPREPROCESS_BF16			2
#define XPREPROCESS_FP32			3

#define XPREPROCESS_RESET_DEASSERT			0
#define XPREPROCESS_RESET_ASSERT			1
#define XPREPROCESS_NO_OF_PADS				2
#define XPREPROCESS_MAX_CHANNELS			4

#define XPREPROCESS_CTRL_REL_ALPHA_FIRST		1
#define XPREPROCESS_CTRL_REL_ALPHA_LAST		XPREPROCESS_MAX_CHANNELS
#define XPREPROCESS_CTRL_REL_BETA_FIRST		(XPREPROCESS_MAX_CHANNELS + 1)
#define XPREPROCESS_CTRL_REL_BETA_LAST		(2 * XPREPROCESS_MAX_CHANNELS)
#define XPREPROCESS_START			BIT(0)
#define XPREPROCESS_AUTO_RESTART		BIT(7)
#define XPREPROCESS_STREAM_ON			(XPREPROCESS_AUTO_RESTART | XPREPROCESS_START)

/**
 * struct xilinx_preprocess_out_format_desc - Output format description
 * @dts_name:	String used in the DT property "xlnx,vid-formats"
 * @channels:	Number of output color channels: 1, 3, or 4
 * @code:	Media bus format code used on the subdev pads
 */
struct xilinx_preprocess_out_format_desc {
	const char *dts_name;
	u32 channels;
	unsigned int code;
};

/* Table mapping DT format names to V4L2/mbus formats and channel counts */
static const struct xilinx_preprocess_out_format_desc xilinx_preprocess_formats[] = {
	{
		.dts_name = "bgr888",
		.code = MEDIA_BUS_FMT_RBG888_1X24,
		.channels = 3
	},
	{
		.dts_name = "rgb_bf161616",
		.code = MEDIA_BUS_FMT_RGB_BF161616_1X48,
		.channels = 3
	},
	{
		.dts_name = "rgb_fp161616",
		.code = MEDIA_BUS_FMT_RGB_FP161616_1X48,
		.channels = 3
	},
	{
		.dts_name = "rgb323232",
		.code = MEDIA_BUS_FMT_RGB_FP323232_1X96,
		.channels = 3
	},
	{
		.dts_name = "rgba8888",
		.code = MEDIA_BUS_FMT_RGBA8888_1X32,
		.channels = 4
	},
	{
		.dts_name = "rgba_bf16161616",
		.code = MEDIA_BUS_FMT_RGBA_BF16161616_1X64,
		.channels = 4
	},
	{
		.dts_name = "rgba_fp16161616",
		.code = MEDIA_BUS_FMT_RGBA_FP16161616_1X64,
		.channels = 4
	},
	{
		.dts_name = "rgba32323232",
		.code = MEDIA_BUS_FMT_RGBA_FP32323232_1X128,
		.channels = 4
	},
	{
		.dts_name = "y8",
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.channels = 1
	},
	{
		.dts_name = "gray_bf16",
		.code = MEDIA_BUS_FMT_Y_BF16_1X16,
		.channels = 1
	},
	{
		.dts_name = "gray_fp16",
		.code = MEDIA_BUS_FMT_Y_FP16_1X16,
		.channels = 1
	},
	{
		.dts_name = "gray32",
		.code = MEDIA_BUS_FMT_Y_FP32_1X32,
		.channels = 1
	},
};

/**
 * struct xpreprocess_dev - Xilinx Preprocess device structure
 * @xvip:		Embedded xvip_device (registers, clocks, etc.)
 * @pads:		Media pads (sink/source)
 * @formats:		Current mbus formats for each pad
 * @ctrl_handler:	V4L2 control handler for alpha/beta parameters
 * @rst_gpio:		Reset GPIO used to reset the hardware
 * @data_type_enabled:	Array of mbus codes enabled via DT "xlnx,vid-formats"
 * @width:		Current input frame width
 * @height:		Current input frame height
 * @out_width:		Current output frame width
 * @out_height:		Current output frame height
 * @channels:		Number of channels for the selected output format
 * @data_type:		Quantization and precision: INT8, BF16, FP16, or FP32
 * @preprocess_param_alpha:	Cached per-channel alpha coefficients for HW registers
 * @preprocess_param_beta:	Cached per-channel beta coefficients for HW registers
 * @npads:		Number of pads discovered from DT
 */
struct xpreprocess_dev {
	struct xvip_device xvip;
	struct media_pad pads[XPREPROCESS_NO_OF_PADS];
	struct v4l2_mbus_framefmt formats[XPREPROCESS_NO_OF_PADS];
	struct v4l2_ctrl_handler ctrl_handler;
	struct gpio_desc *rst_gpio;
	u32 data_type_enabled[ARRAY_SIZE(xilinx_preprocess_formats)];
	u32 width;
	u32 height;
	u32 out_width;
	u32 out_height;
	u32 channels;
	u32 data_type;
	u32 preprocess_param_alpha[XPREPROCESS_MAX_CHANNELS];
	u32 preprocess_param_beta[XPREPROCESS_MAX_CHANNELS];
	u32 npads;
};

/**
 * xpreprocess_write - Debug wrapper around xvip_write()
 * @xpreprocess: Preprocess device
 * @reg:        Register offset
 * @data:       Value to write
 *
 * Writes register and logs the operation in debug builds.
 */
static inline void xpreprocess_write(struct xpreprocess_dev *xpreprocess,
				     u32 reg, u32 data)
{
	xvip_write(&xpreprocess->xvip, reg, data);
	dev_dbg(xpreprocess->xvip.dev,
		"Writing 0x%x to reg offset 0x%x\n", data, reg);
}

/**
 * xpreprocess_s_ctrl - V4L2 control set callback
 * @ctrl: Control being updated
 *
 * Programs alpha/beta coefficients into the hardware parameter registers.
 *
 * Return: 0 on success, or %-EINVAL if @ctrl->id is not a registered preprocess CID.
 */
static int xpreprocess_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xpreprocess_dev *xpreprocess = container_of(ctrl->handler,
							   struct xpreprocess_dev,
							   ctrl_handler);
	unsigned int rel = ctrl->id - V4L2_CID_XILINX_PREPROCESS;
	unsigned int ch;

	if (rel < XPREPROCESS_CTRL_REL_ALPHA_FIRST ||
	    rel > XPREPROCESS_CTRL_REL_BETA_LAST)
		return -EINVAL;

	if (rel <= XPREPROCESS_CTRL_REL_ALPHA_LAST) {
		ch = rel - XPREPROCESS_CTRL_REL_ALPHA_FIRST;
		xpreprocess->preprocess_param_alpha[ch] = ctrl->val;
		xpreprocess_write(xpreprocess, XPREPROCESS_PARAM_ALPHA_OFF(ch),
				  ctrl->val);
	} else {
		ch = rel - XPREPROCESS_CTRL_REL_BETA_FIRST;
		xpreprocess->preprocess_param_beta[ch] = ctrl->val;
		xpreprocess_write(xpreprocess, XPREPROCESS_PARAM_BETA_OFF(ch),
				  ctrl->val);
	}

	return 0;
}

/* V4L2 control operations for the preprocess block */
static const struct v4l2_ctrl_ops xpreprocess_ctrl_ops = {
	.s_ctrl = xpreprocess_s_ctrl,
};

/*
 * V4L2 controls used to expose alpha/beta parameters (per-channel).
 * Values are raw register encodings: see (x - alpha) * beta and Q0.23 beta above.
 */
static const struct v4l2_ctrl_config xpreprocess_ctrls[] = {
	{
		.ops = &xpreprocess_ctrl_ops,
		.id = V4L2_CID_XILINX_PREPROCESS_PARAM_ALPHA_1,
		.name = "preprocess_param_alpha_1",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = XPREPROCESS_PARAM_ALPHA_MAX,
		.step = 1,
		.def = XPREPROCESS_PARAM_ALPHA_DEF,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* PREPROCESS PARAM BETA_1 */
	{
		.ops = &xpreprocess_ctrl_ops,
		.id = V4L2_CID_XILINX_PREPROCESS_PARAM_BETA_1,
		.name = "preprocess_param_beta_1",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = XPREPROCESS_PARAM_BETA_MAX,
		.step = 1,
		.def = XPREPROCESS_PARAM_BETA_DEF,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* PREPROCESS PARAM ALPHA_2 */
	{
		.ops = &xpreprocess_ctrl_ops,
		.id = V4L2_CID_XILINX_PREPROCESS_PARAM_ALPHA_2,
		.name = "preprocess_param_alpha_2",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = XPREPROCESS_PARAM_ALPHA_MAX,
		.step = 1,
		.def = XPREPROCESS_PARAM_ALPHA_DEF,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* PREPROCESS PARAM BETA_2 */
	{
		.ops = &xpreprocess_ctrl_ops,
		.id = V4L2_CID_XILINX_PREPROCESS_PARAM_BETA_2,
		.name = "preprocess_param_beta_2",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = XPREPROCESS_PARAM_BETA_MAX,
		.step = 1,
		.def = XPREPROCESS_PARAM_BETA_DEF,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* PREPROCESS PARAM ALPHA_3 */
	{
		.ops = &xpreprocess_ctrl_ops,
		.id = V4L2_CID_XILINX_PREPROCESS_PARAM_ALPHA_3,
		.name = "preprocess_param_alpha_3",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = XPREPROCESS_PARAM_ALPHA_MAX,
		.step = 1,
		.def = XPREPROCESS_PARAM_ALPHA_DEF,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* PREPROCESS PARAM BETA_3 */
	{
		.ops = &xpreprocess_ctrl_ops,
		.id = V4L2_CID_XILINX_PREPROCESS_PARAM_BETA_3,
		.name = "preprocess_param_beta_3",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = XPREPROCESS_PARAM_BETA_MAX,
		.step = 1,
		.def = XPREPROCESS_PARAM_BETA_DEF,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* PREPROCESS PARAM ALPHA_4 */
	{
		.ops = &xpreprocess_ctrl_ops,
		.id = V4L2_CID_XILINX_PREPROCESS_PARAM_ALPHA_4,
		.name = "preprocess_param_alpha_4",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = XPREPROCESS_PARAM_ALPHA_MAX,
		.step = 1,
		.def = XPREPROCESS_PARAM_ALPHA_DEF,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* PREPROCESS PARAM BETA_4 */
	{
		.ops = &xpreprocess_ctrl_ops,
		.id = V4L2_CID_XILINX_PREPROCESS_PARAM_BETA_4,
		.name = "preprocess_param_beta_4",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = XPREPROCESS_PARAM_BETA_MAX,
		.step = 1,
		.def = XPREPROCESS_PARAM_BETA_DEF,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

/**
 * to_xpreprocess - Helper to get xpreprocess_dev from subdev
 * @subdev: V4L2 subdevice
 *
 * Return: Pointer to the preprocess device embedded in @subdev.
 */
static inline struct xpreprocess_dev *to_xpreprocess(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xpreprocess_dev, xvip.subdev);
}

/**
 * __xpreprocess_get_pad_format - Get mbus format for a pad
 * @xpreprocess: Preprocess device
 * @sd_state:   Subdev state (try formats)
 * @pad:        Pad index
 * @which:      ACTIVE or TRY
 *
 * Return: Pointer to the mbus format for @pad and @which, or %NULL when
 *	   @which is neither %V4L2_SUBDEV_FORMAT_TRY nor %V4L2_SUBDEV_FORMAT_ACTIVE.
 */
static struct v4l2_mbus_framefmt *
__xpreprocess_get_pad_format(struct xpreprocess_dev *xpreprocess,
			     struct v4l2_subdev_state *sd_state,
			     unsigned int pad, u32 which)
{
	struct v4l2_mbus_framefmt *get_fmt;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		get_fmt = v4l2_subdev_state_get_format(sd_state, pad);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		get_fmt = &xpreprocess->formats[pad];
		break;
	default:
		get_fmt = NULL;
		break;
	}

	return get_fmt;
}

static u32 xpreprocess_data_type_from_code(unsigned int code)
{
	switch (code) {
	case MEDIA_BUS_FMT_RBG888_1X24:
	case MEDIA_BUS_FMT_RGBA8888_1X32:
	case MEDIA_BUS_FMT_Y8_1X8:
		return XPREPROCESS_INT8;
	case MEDIA_BUS_FMT_RGB_BF161616_1X48:
	case MEDIA_BUS_FMT_RGBA_BF16161616_1X64:
	case MEDIA_BUS_FMT_Y_BF16_1X16:
		return XPREPROCESS_BF16;
	case MEDIA_BUS_FMT_RGB_FP161616_1X48:
	case MEDIA_BUS_FMT_RGBA_FP16161616_1X64:
	case MEDIA_BUS_FMT_Y_FP16_1X16:
		return XPREPROCESS_FP16;
	case MEDIA_BUS_FMT_RGB_FP323232_1X96:
	case MEDIA_BUS_FMT_RGBA_FP32323232_1X128:
	case MEDIA_BUS_FMT_Y_FP32_1X32:
		return XPREPROCESS_FP32;
	default:
		return XPREPROCESS_INT8;
	}
}

/**
 * xpreprocess_reset - Reset Preprocess IP via GPIO
 * @xpreprocess: Preprocess device
 */
static void xpreprocess_reset(struct xpreprocess_dev *xpreprocess)
{
	/* Assert reset, wait, then deassert (cansleep GPIOs -> usleep_range) */
	gpiod_set_value_cansleep(xpreprocess->rst_gpio, XPREPROCESS_RESET_ASSERT);
	usleep_range(1, 10);
	gpiod_set_value_cansleep(xpreprocess->rst_gpio, XPREPROCESS_RESET_DEASSERT);
}

/**
 * xpreprocess_s_stream - Start/stop streaming
 * @subdev:  V4L2 subdevice
 * @enable:  1 to start, 0 to stop
 *
 * On stop, asserts GPIO reset. On start, programs frame sizes and data type,
 * replays V4L2 controls to parameter registers (reset clears them), then
 * starts the core.
 *
 * Return: Always 0.
 */
static int xpreprocess_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xpreprocess_dev *xpreprocess = to_xpreprocess(subdev);
	unsigned int i;

	if (!enable) {
		dev_dbg(xpreprocess->xvip.dev, "%s : Off\n", __func__);
		xpreprocess_reset(xpreprocess);
		return 0;
	}

	/* Cache input / output sizes from pad formats */
	xpreprocess->width = xpreprocess->formats[XVIP_PAD_SINK].width;
	xpreprocess->height = xpreprocess->formats[XVIP_PAD_SINK].height;

	xpreprocess->out_width = xpreprocess->formats[XVIP_PAD_SOURCE].width;
	xpreprocess->out_height = xpreprocess->formats[XVIP_PAD_SOURCE].height;

	/* Program input image size */
	xpreprocess_write(xpreprocess, XPREPROCESS_IN_IMG_WIDTH_REG,
			  xpreprocess->width);
	xpreprocess_write(xpreprocess, XPREPROCESS_IN_IMG_HEIGHT_REG,
			  xpreprocess->height);

	/* Program output image size and data type */
	xpreprocess_write(xpreprocess, XPREPROCESS_OUT_WIDTH_REG,
			  xpreprocess->out_width);
	xpreprocess_write(xpreprocess, XPREPROCESS_OUT_HEIGHT_REG,
			  xpreprocess->out_height);
	xpreprocess_write(xpreprocess, XPREPROCESS_DATA_TYPE_REG,
			  xpreprocess->data_type);
	for (i = 0; i < XPREPROCESS_MAX_CHANNELS; i++) {
		xpreprocess_write(xpreprocess, XPREPROCESS_PARAM_ALPHA_OFF(i),
				  xpreprocess->preprocess_param_alpha[i]);
		xpreprocess_write(xpreprocess, XPREPROCESS_PARAM_BETA_OFF(i),
				  xpreprocess->preprocess_param_beta[i]);
	}
	/* Start Preprocess Video IP with auto-restart */
	xpreprocess_write(xpreprocess, XPREPROCESS_AP_CTRL,
			  XPREPROCESS_STREAM_ON);

	return 0;
}

/* V4L2 video operations */
static const struct v4l2_subdev_video_ops xpreprocess_video_ops = {
	.s_stream = xpreprocess_s_stream,
};

/**
 * xpreprocess_enum_mbus_code - Enumerate mbus codes per pad
 * @subdev:   V4L2 subdevice
 * @sd_state: Subdev state (unused; formats come from device / DT)
 * @code:     Enumeration index and return code
 *
 * Sink pad supports RGB888 only. Source pad lists formats enabled via
 * the device-tree property "xlnx,vid-formats".
 *
 * Return: 0 on success, %-EINVAL if @code->pad or @code->index is invalid.
 */
static int xpreprocess_enum_mbus_code(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	struct xpreprocess_dev *xpreprocess = to_xpreprocess(subdev);
	unsigned int i, n;

	switch (code->pad) {
	case XVIP_PAD_SINK:
		if (code->index)
			return -EINVAL;
		code->code = MEDIA_BUS_FMT_RBG888_1X24;
		return 0;
	case XVIP_PAD_SOURCE:
		n = 0;
		for (i = 0; i < ARRAY_SIZE(xpreprocess->data_type_enabled); i++) {
			if (!xpreprocess->data_type_enabled[i])
				continue;
			if (n == code->index) {
				code->code = xpreprocess->data_type_enabled[i];
				return 0;
			}
			n++;
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

/**
 * xpreprocess_get_format - Get pad format
 * @subdev:   V4L2 subdevice
 * @sd_state: Subdev state
 * @fmt:      Format struct to fill
 *
 * Return: 0 on success, %-EINVAL if the pad format cannot be resolved.
 */
static int xpreprocess_get_format(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct xpreprocess_dev *xpreprocess = to_xpreprocess(subdev);
	struct v4l2_mbus_framefmt *get_fmt;

	get_fmt = __xpreprocess_get_pad_format(xpreprocess, sd_state,
					       fmt->pad, fmt->which);
	if (!get_fmt)
		return -EINVAL;

	fmt->format = *get_fmt;

	return 0;
}

/**
 * xpreprocess_set_format - Set pad format
 * @subdev:   V4L2 subdevice
 * @sd_state: Subdev state
 * @fmt:      Requested format
 *
 * Clamps resolution to IP limits, validates/sets mbus code and
 * updates data type according to the selected output format.
 *
 * Return: 0 on success, %-EINVAL if the pad format cannot be resolved or no
 *	   DT-enabled output format exists to fall back to.
 */
static int xpreprocess_set_format(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct xpreprocess_dev *xpreprocess = to_xpreprocess(subdev);
	struct v4l2_mbus_framefmt *__format;
	unsigned int width, height, code;
	bool found = false;
	int i;

	__format = __xpreprocess_get_pad_format(xpreprocess, sd_state,
						fmt->pad, fmt->which);
	if (!__format)
		return -EINVAL;

	if (fmt->pad == XVIP_PAD_SOURCE) {
		width = clamp_t(unsigned int, fmt->format.width,
				XPREPROCESS_MIN_WIDTH, XPREPROCESS_MAX_WIDTH);
		height = clamp_t(unsigned int, fmt->format.height,
				 XPREPROCESS_MIN_HEIGHT, XPREPROCESS_MAX_HEIGHT);

		switch (fmt->format.code) {
		case MEDIA_BUS_FMT_RBG888_1X24:
		case MEDIA_BUS_FMT_RGB_BF161616_1X48:
		case MEDIA_BUS_FMT_RGB_FP161616_1X48:
		case MEDIA_BUS_FMT_RGB_FP323232_1X96:
		case MEDIA_BUS_FMT_RGBA8888_1X32:
		case MEDIA_BUS_FMT_RGBA_BF16161616_1X64:
		case MEDIA_BUS_FMT_RGBA_FP16161616_1X64:
		case MEDIA_BUS_FMT_RGBA_FP32323232_1X128:
		case MEDIA_BUS_FMT_Y8_1X8:
		case MEDIA_BUS_FMT_Y_BF16_1X16:
		case MEDIA_BUS_FMT_Y_FP16_1X16:
		case MEDIA_BUS_FMT_Y_FP32_1X32:
			code = fmt->format.code;
			break;
		default:
			code = MEDIA_BUS_FMT_RBG888_1X24;
			break;
		}

		for (i = 0; i < ARRAY_SIZE(xpreprocess->data_type_enabled);
		     i++) {
			if (code == xpreprocess->data_type_enabled[i]) {
				found = true;
				break;
			}
		}
		if (!found) {
			code = xpreprocess->data_type_enabled[0];
			if (!code)
				return -EINVAL;
			dev_dbg(xpreprocess->xvip.dev,
				"mbus code 0x%x not enabled; using 0x%x\n",
				fmt->format.code, code);
		}

		*__format = fmt->format;
		__format->width = width;
		__format->height = height;
		__format->code = code;

		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			xpreprocess->out_width = width;
			xpreprocess->out_height = height;
			xpreprocess->data_type =
				xpreprocess_data_type_from_code(code);
		}
	} else if (fmt->pad == XVIP_PAD_SINK) {
		width = clamp_t(unsigned int, fmt->format.width,
				XPREPROCESS_MIN_WIDTH, XPREPROCESS_MAX_WIDTH);
		height = clamp_t(unsigned int, fmt->format.height,
				 XPREPROCESS_MIN_HEIGHT, XPREPROCESS_MAX_HEIGHT);

		*__format = fmt->format;
		__format->width = width;
		__format->height = height;
		__format->code = MEDIA_BUS_FMT_RBG888_1X24;
	}

	fmt->format = *__format;

	return 0;
}

/**
 * xpreprocess_open - Initialize per-filehandle TRY pad formats
 * @subdev: V4L2 subdevice
 * @fh:     Subdevice file handle
 *
 * Seeds TRY state from the active pad formats so
 * %V4L2_SUBDEV_FORMAT_TRY operations do not read zeroed structs.
 *
 * Return: Always 0.
 */
static int xpreprocess_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xpreprocess_dev *xpreprocess = to_xpreprocess(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_state_get_format(fh->state, XVIP_PAD_SINK);
	*format = xpreprocess->formats[XVIP_PAD_SINK];

	format = v4l2_subdev_state_get_format(fh->state, XVIP_PAD_SOURCE);
	*format = xpreprocess->formats[XVIP_PAD_SOURCE];

	return 0;
}

static const struct v4l2_subdev_internal_ops xpreprocess_internal_ops = {
	.open = xpreprocess_open,
};

/* Pad operations: format negotiation on sink/source pads */
static const struct v4l2_subdev_pad_ops xpreprocess_pad_ops = {
	.enum_mbus_code = xpreprocess_enum_mbus_code,
	.set_fmt = xpreprocess_set_format,
	.get_fmt = xpreprocess_get_format,
};

/* Aggregate subdev operations */
static const struct v4l2_subdev_ops xpreprocess_ops = {
	.video = &xpreprocess_video_ops,
	.pad = &xpreprocess_pad_ops,
};

/* Media entity operations (default link validation) */
static const struct media_entity_operations xpreprocess_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/**
 * xpreprocess_parse_of - Parse firmware graph and GPIO properties
 * @xpreprocess: Preprocess device
 *
 * Counts media graph endpoints via fwnode and acquires reset GPIO.
 *
 * Return: 0 on success, %-EINVAL if the graph endpoint count is wrong, or a
 *	   negative error code if reset GPIO lookup fails.
 */
static int xpreprocess_parse_of(struct xpreprocess_dev *xpreprocess)
{
	struct device *dev = xpreprocess->xvip.dev;
	const struct fwnode_handle *fwnode = dev_fwnode(dev);

	if (!fwnode)
		return -EINVAL;

	/*
	 * Count every graph endpoint under this device (same as
	 * of_graph_get_endpoint_count(of_node)).  Plain flags==0 would only
	 * count endpoints with a remote link; FWNODE_GRAPH_DEVICE_DISABLED
	 * includes unconnected endpoints in the tally.
	 */
	xpreprocess->npads =
		fwnode_graph_get_endpoint_count(fwnode, FWNODE_GRAPH_DEVICE_DISABLED);
	if (xpreprocess->npads != XPREPROCESS_NO_OF_PADS) {
		dev_err(dev, "invalid graph: expected %u endpoints, found %u\n",
			XPREPROCESS_NO_OF_PADS, xpreprocess->npads);
		return -EINVAL;
	}

	/* reset-gpios is required by binding; con_id "reset" matches gpio-reset */
	xpreprocess->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xpreprocess->rst_gpio))
		return dev_err_probe(dev, PTR_ERR(xpreprocess->rst_gpio),
				     "failed to get reset GPIO\n");

	return 0;
}

/**
 * xpreprocess_probe - Platform driver probe
 * @pdev: Platform device
 *
 * Allocates and initializes driver, parses DT, sets up subdev,
 * media entity and V4L2 controls.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int xpreprocess_probe(struct platform_device *pdev)
{
	const char *dts_name[ARRAY_SIZE(xilinx_preprocess_formats)] = {};
	struct xpreprocess_dev *xpreprocess;
	struct v4l2_subdev *subdev;
	const char *first_fmt;
	bool matched_first;
	int hw_vid_fmt_cnt;
	unsigned int vid_fmt_cnt;
	int ret, j;
	unsigned int i;

	/* Allocate driver data */
	xpreprocess = devm_kzalloc(&pdev->dev, sizeof(*xpreprocess),
				   GFP_KERNEL);
	if (!xpreprocess)
		return -ENOMEM;
	xpreprocess->xvip.dev = &pdev->dev;

	/* Parse graph (pads) and reset GPIO */
	ret = xpreprocess_parse_of(xpreprocess);
	if (ret < 0)
		return ret;

	/* Initialize common XVIP resources (clocks, regs) */
	ret = xvip_init_resources(&xpreprocess->xvip);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to initialize XVIP resources: %d\n",
			ret);
		return ret;
	}

	/* Reset hardware before configuration */
	xpreprocess_reset(xpreprocess);

	/* Init V4L2 subdev */
	subdev = &xpreprocess->xvip.subdev;
	v4l2_subdev_init(subdev, &xpreprocess_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xpreprocess_internal_ops;
	strscpy(subdev->name, dev_name(&pdev->dev),
		sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dev_dbg(&pdev->dev, "xpreprocess_reset done\n");

	/*
	 * Configure default sink pad format.
	 * Treat sink as RGB 8-bit by default.
	 */
	xpreprocess->formats[XVIP_PAD_SINK].field = V4L2_FIELD_NONE;
	xpreprocess->formats[XVIP_PAD_SINK].colorspace = V4L2_COLORSPACE_SRGB;
	xpreprocess->formats[XVIP_PAD_SINK].width = XPREPROCESS_MIN_WIDTH;
	xpreprocess->formats[XVIP_PAD_SINK].height = XPREPROCESS_MIN_HEIGHT;
	xpreprocess->formats[XVIP_PAD_SINK].code = MEDIA_BUS_FMT_RBG888_1X24;

	/*
	 * Default source pad format will be taken from DT
	 * property "xlnx,vid-formats".
	 */
	xpreprocess->formats[XVIP_PAD_SOURCE].field = V4L2_FIELD_NONE;
	xpreprocess->formats[XVIP_PAD_SOURCE].colorspace =
		V4L2_COLORSPACE_SRGB;
	xpreprocess->formats[XVIP_PAD_SOURCE].width = XPREPROCESS_MIN_WIDTH;
	xpreprocess->formats[XVIP_PAD_SOURCE].height = XPREPROCESS_MIN_HEIGHT;

	/* Read number of video formats listed in DT */
	hw_vid_fmt_cnt = device_property_string_array_count(&pdev->dev,
							    "xlnx,vid-formats");
	if (hw_vid_fmt_cnt <= 0) {
		dev_err(&pdev->dev,
			"invalid or empty xlnx,vid-formats property (%d)\n",
			hw_vid_fmt_cnt);
		ret = hw_vid_fmt_cnt < 0 ? hw_vid_fmt_cnt : -EINVAL;
		goto media_error;
	}
	vid_fmt_cnt = (unsigned int)hw_vid_fmt_cnt;
	if (vid_fmt_cnt > ARRAY_SIZE(xilinx_preprocess_formats)) {
		dev_err(&pdev->dev, "Too many strings in xlnx,vid-formats\n");
		ret = -EINVAL;
		goto media_error;
	}

	/* Read format names into dts_name[] */
	ret = device_property_read_string_array(&pdev->dev, "xlnx,vid-formats",
						dts_name, vid_fmt_cnt);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to read 'xlnx,vid-formats' property\n");
		ret = -EINVAL;
		goto media_error;
	}
	/*
	 * Use the first DT format as the default source pad format.
	 * Also derive number of channels and data type.
	 */
	first_fmt = dts_name[0];
	matched_first = false;
	for (j = 0; j < ARRAY_SIZE(xilinx_preprocess_formats); j++) {
		if (strcmp(xilinx_preprocess_formats[j].dts_name,
			   first_fmt))
			continue;

		matched_first = true;
		xpreprocess->formats[XVIP_PAD_SOURCE].code =
			xilinx_preprocess_formats[j].code;
		xpreprocess->channels =
			xilinx_preprocess_formats[j].channels;
		xpreprocess->data_type =
			xpreprocess_data_type_from_code(xpreprocess->formats[XVIP_PAD_SOURCE].code);
		xpreprocess_write(xpreprocess, XPREPROCESS_DATA_TYPE_REG,
				  xpreprocess->data_type);
		break;
	}
	if (!matched_first) {
		dev_err(&pdev->dev,
			"Unknown xlnx,vid-formats entry '%s'\n", first_fmt);
		ret = -EINVAL;
		goto media_error;
	}

	/*
	 * Build list of enabled mbus codes from DT "xlnx,vid-formats"
	 * to be used for format validation in set_fmt().
	 */
	for (i = 0; i < vid_fmt_cnt; i++) {
		bool matched = false;

		for (j = 0; j < ARRAY_SIZE(xilinx_preprocess_formats); j++) {
			if (strcmp(xilinx_preprocess_formats[j].dts_name,
				   dts_name[i]) == 0) {
				xpreprocess->data_type_enabled[i] =
					xilinx_preprocess_formats[j].code;
				matched = true;
				/* go to next DT entry */
				break;
			}
		}
		if (!matched) {
			dev_warn(&pdev->dev,
				 "xlnx,vid-formats: unknown format '%s', ignored\n",
				 dts_name[i]);
		}
	}

	/* Initialize media pads */
	xpreprocess->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xpreprocess->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Init Media Entity for the subdev */
	subdev->entity.ops = &xpreprocess_media_ops;
	ret = media_entity_pads_init(&subdev->entity, XPREPROCESS_NO_OF_PADS,
				     xpreprocess->pads);
	if (ret < 0)
		goto media_error;

	/* Initialize V4L2 controls: allocate handler and add controls */
	v4l2_ctrl_handler_init(&xpreprocess->ctrl_handler,
			       ARRAY_SIZE(xpreprocess_ctrls));

	/*
	 * For each color channel we expose an alpha and beta control.
	 * Thus, total controls = 2 * channels.
	 */
	for (i = 0; i < 2 * xpreprocess->channels; i++) {
		v4l2_ctrl_new_custom(&xpreprocess->ctrl_handler,
				     &xpreprocess_ctrls[i], NULL);
	}

	if (xpreprocess->ctrl_handler.error) {
		dev_err(&pdev->dev, "Failed to add V4L2 controls\n");
		ret = xpreprocess->ctrl_handler.error;
		goto ctrl_error;
	}

	subdev->ctrl_handler = &xpreprocess->ctrl_handler;

	/* Apply default values for controls */
	ret = v4l2_ctrl_handler_setup(&xpreprocess->ctrl_handler);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to setup control handler\n");
		goto ctrl_error;
	}

	/* Store driver data and register subdev with V4L2 core */
	platform_set_drvdata(pdev, xpreprocess);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto ctrl_error;
	}

	return 0;

ctrl_error:
	v4l2_ctrl_handler_free(&xpreprocess->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
media_error:
	xvip_cleanup_resources(&xpreprocess->xvip);
	return ret;
}

/**
 * xpreprocess_remove - Platform driver remove
 * @pdev: Platform device
 *
 * Asserts reset to stop the core (e.g. auto-restart left active), then
 * unregisters the subdev and frees resources.
 */
static void xpreprocess_remove(struct platform_device *pdev)
{
	struct xpreprocess_dev *xpreprocess = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev;

	if (!xpreprocess)
		return;

	xpreprocess_reset(xpreprocess);

	subdev = &xpreprocess->xvip.subdev;
	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xpreprocess->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xpreprocess->xvip);
}

/* Device tree match table */
static const struct of_device_id xpreprocess_of_id_table[] = {
	{ .compatible = "xlnx,preprocess-1.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, xpreprocess_of_id_table);

/* Platform driver definition */
static struct platform_driver xpreprocess_driver = {
	.driver = {
		.name = "xilinx-preprocess",
		.of_match_table = xpreprocess_of_id_table,
	},
	.probe = xpreprocess_probe,
	.remove = xpreprocess_remove,
};

module_platform_driver(xpreprocess_driver);

MODULE_AUTHOR("Mounik Katikala <mounik.katikala@amd.com>");
MODULE_DESCRIPTION("Xilinx Preprocess IP Driver");
MODULE_LICENSE("GPL");

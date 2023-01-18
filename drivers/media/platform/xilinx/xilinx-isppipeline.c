// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 - 2023, Advanced Micro Devices, Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

#include "xilinx-gamma-correction.h"
#include "xilinx-vip.h"

#define XISP_AP_CTRL_REG		(0x0)
#define XISP_WIDTH_REG			(0x10)
#define XISP_HEIGHT_REG			(0x18)
#define XISP_MODE_REG			(0x20)
#define XISP_INPUT_BAYER_FORMAT_REG	(0x28)
#define XISP_RGAIN_REG			(0x30)
#define XISP_BGAIN_REG			(0x38)
#define XISP_PAWB_REG			(0x54)
#define XISP_GAMMA_RED_REG		(0x800)
#define XISP_GAMMA_BLUE_REG		(0x900)
#define XISP_GAMMA_GREEN_REG		(0xA00)

#define XISP_MAX_HEIGHT			(4320)
#define XISP_MAX_WIDTH			(8192)
#define XISP_MIN_HEIGHT			(64)
#define XISP_MIN_WIDTH			(64)
#define XISP_GAMMA_LUT_LEN		(64)
#define XISP_NO_OF_PADS			(2)

#define XISP_RESET_DEASSERT		(0)
#define XISP_RESET_ASSERT		(1)
#define XISP_START			BIT(0)
#define XISP_AUTO_RESTART		BIT(7)
#define XISP_STREAM_ON			(XISP_AUTO_RESTART | XISP_START)

enum xisp_bayer_format {
	XISP_RGGB = 0,
	XISP_GRBG,
	XISP_GBRG,
	XISP_BGGR,
};

/*
 * struct xisp_dev - Xilinx ISP pipeline device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @formats: V4L2 media bus formats
 * @ctrl_handler: V4L2 Control Handler
 * @bayer_fmt: IP or Hardware specific video format
 * @rst_gpio: GPIO reset line to bring ISP pipeline out of reset
 * @npads: number of pads
 * @max_width: Maximum width supported by this instance
 * @max_height: Maximum height supported by this instance
 * @rgain: Expected red gain
 * @bgain: Expected blue gain
 * @mode_reg: Track if AWB is enabled or not
 * @pawb: Expected threshold
 * @red_lut: Pointer to the gamma coefficient as per the Red Gamma control
 * @green_lut: Pointer to the gamma coefficient as per the Green Gamma control
 * @blue_lut: Pointer to the gamma coefficient as per the Blue Gamma control
 * @gamma_table: Pointer to the table containing various gamma values
 */
struct xisp_dev {
	struct xvip_device xvip;
	struct media_pad pads[XISP_NO_OF_PADS];
	struct v4l2_mbus_framefmt formats[XISP_NO_OF_PADS];
	struct v4l2_ctrl_handler ctrl_handler;
	enum xisp_bayer_format bayer_fmt;
	struct gpio_desc *rst_gpio;
	u16 npads;
	u16 max_width;
	u16 max_height;
	u16 rgain;
	u16 bgain;
	bool mode_reg;
	u16 pawb;
	const u32 *red_lut;
	const u32 *green_lut;
	const u32 *blue_lut;
	const u32 **gamma_table;
};

static inline struct xisp_dev *to_xisp(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xisp_dev, xvip.subdev);
}

/*
 * xisp_set_lut_entries - Write to a field in ISP pipeline registers
 *
 * @xisp:	The xisp_dev
 * @lut:	The value to write
 * @lut_base:	The field to write to
 *
 * This function allows writing to gamma lut array.
 */

static void xisp_set_lut_entries(struct xisp_dev *xisp, const u32 *lut, const u32 lut_base)
{
	int itr;
	u32 lut_offset;

	lut_offset = lut_base;

	for (itr = 0; itr < XISP_GAMMA_LUT_LEN; itr = itr + 1) {
		xvip_write(&xisp->xvip, lut_offset, lut[itr]);
		lut_offset += 4;
	}
}

static void select_gamma(u32 value, const u32 **coeff, const u32 **xgamma_curves)
{
	*coeff = *(xgamma_curves + value - 1);
}

static int xisp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xisp_dev *xisp =
		container_of(ctrl->handler,
			     struct xisp_dev, ctrl_handler);
	switch (ctrl->id) {
	case V4L2_CID_XILINX_ISP_RED_GAIN:
		xisp->rgain = ctrl->val;
		xvip_write(&xisp->xvip, XISP_RGAIN_REG, xisp->rgain);
		break;
	case V4L2_CID_XILINX_ISP_BLUE_GAIN:
		xisp->bgain = ctrl->val;
		xvip_write(&xisp->xvip, XISP_BGAIN_REG, xisp->bgain);
		break;
	case V4L2_CID_XILINX_ISP_AWB:
		xisp->mode_reg = ctrl->val;
		xvip_write(&xisp->xvip, XISP_MODE_REG, xisp->mode_reg);
		break;
	case V4L2_CID_XILINX_ISP_THRESHOLD:
		xisp->pawb = ctrl->val;
		xvip_write(&xisp->xvip, XISP_PAWB_REG, xisp->pawb);
		break;
	case V4L2_CID_XILINX_ISP_RED_GAMMA:
		select_gamma(ctrl->val, &xisp->red_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Red Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		xisp_set_lut_entries(xisp, xisp->red_lut, XISP_GAMMA_RED_REG);
		break;
	case V4L2_CID_XILINX_ISP_GREEN_GAMMA:
		select_gamma(ctrl->val, &xisp->green_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Green Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		xisp_set_lut_entries(xisp, xisp->green_lut, XISP_GAMMA_GREEN_REG);
		break;
	case V4L2_CID_XILINX_ISP_BLUE_GAMMA:
		select_gamma(ctrl->val, &xisp->blue_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Blue Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		xisp_set_lut_entries(xisp, xisp->blue_lut, XISP_GAMMA_BLUE_REG);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops xisp_ctrl_ops = {
	.s_ctrl = xisp_s_ctrl,
};

static struct v4l2_ctrl_config xisp_ctrls[] = {
	/* Red Gain*/
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RED_GAIN,
		.name = "red_gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 65535,
		.step = 1,
		.def = 100,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Blue Gain */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BLUE_GAIN,
		.name = "blue_gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 65535,
		.step = 1,
		.def = 350,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* AWB Enable */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_AWB,
		.name = "awb_en",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 1,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* THRESHOLD */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_THRESHOLD,
		.name = "threshold",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 65535,
		.step = 1,
		.def = 512,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Red Gamma */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RED_GAMMA,
		.name = "red_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 20,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Green Gamma */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GREEN_GAMMA,
		.name = "green_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 15,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Blue Gamma */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BLUE_GAMMA,
		.name = "blue_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 20,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_mbus_framefmt
*__xisp_get_pad_format(struct xisp_dev *xisp,
			struct v4l2_subdev_state *sd_state,
			unsigned int pad, u32 which)
{
	struct v4l2_mbus_framefmt *get_fmt;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		get_fmt = v4l2_subdev_get_try_format(&xisp->xvip.subdev,
						     sd_state, pad);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		get_fmt = &xisp->formats[pad];
		break;
	default:
		get_fmt = NULL;
		break;
	}

	return get_fmt;
}

/*
 * xisp_reset - Reset ISP pipeline IP
 */
static void xisp_reset(struct xisp_dev *xisp)
{
	/* reset ip */
	gpiod_set_value_cansleep(xisp->rst_gpio, XISP_RESET_ASSERT);
	udelay(1);
	gpiod_set_value_cansleep(xisp->rst_gpio, XISP_RESET_DEASSERT);
}

static int xisp_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xisp_dev *xisp = to_xisp(subdev);

	if (!enable) {
		dev_dbg(xisp->xvip.dev, "%s : Off", __func__);
		xisp_reset(xisp);
		return 0;
	}

	xvip_write(&xisp->xvip, XISP_WIDTH_REG, xisp->formats[XVIP_PAD_SINK].width);
	xvip_write(&xisp->xvip, XISP_HEIGHT_REG, xisp->formats[XVIP_PAD_SINK].height);
	xvip_write(&xisp->xvip, XISP_INPUT_BAYER_FORMAT_REG, xisp->bayer_fmt);
	xvip_write(&xisp->xvip, XISP_RGAIN_REG, xisp->rgain);
	xvip_write(&xisp->xvip, XISP_BGAIN_REG, xisp->bgain);
	xvip_write(&xisp->xvip, XISP_MODE_REG, xisp->mode_reg);
	xvip_write(&xisp->xvip, XISP_PAWB_REG, xisp->pawb);
	xisp_set_lut_entries(xisp, xisp->red_lut, XISP_GAMMA_RED_REG);
	xisp_set_lut_entries(xisp, xisp->green_lut, XISP_GAMMA_GREEN_REG);
	xisp_set_lut_entries(xisp, xisp->blue_lut, XISP_GAMMA_BLUE_REG);

	/* Start ISP pipeline IP */
	xvip_write(&xisp->xvip, XISP_AP_CTRL_REG, XISP_STREAM_ON);

	return 0;
}

static const struct v4l2_subdev_video_ops xisp_video_ops = {
	.s_stream = xisp_s_stream,
};

static int xisp_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct xisp_dev *xisp = to_xisp(subdev);
	struct v4l2_mbus_framefmt *get_fmt;

	get_fmt = __xisp_get_pad_format(xisp, sd_state, fmt->pad, fmt->which);
	if (!get_fmt)
		return -EINVAL;

	fmt->format = *get_fmt;

	return 0;
}

static bool
xisp_get_bayer_format(struct xisp_dev *xisp, u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_SRGGB16_1X16:
		xisp->bayer_fmt = XISP_RGGB;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SGRBG16_1X16:
		xisp->bayer_fmt = XISP_GRBG;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGBRG16_1X16:
		xisp->bayer_fmt = XISP_GBRG;
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SBGGR16_1X16:
		xisp->bayer_fmt = XISP_BGGR;
		break;
	default:
		dev_dbg(xisp->xvip.dev, "Unsupported format for Sink Pad");
		return false;
	}

	return true;
}

static int xisp_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct xisp_dev *xisp = to_xisp(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_mbus_framefmt *__propagate;

	__format = __xisp_get_pad_format(xisp, sd_state, fmt->pad, fmt->which);
	if (!__format)
		return -EINVAL;

	/* Propagate to Source Pad */
	__propagate = __xisp_get_pad_format(xisp, sd_state,
					    XVIP_PAD_SOURCE, fmt->which);
	if (!__propagate)
		return -EINVAL;

	*__format = fmt->format;

	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XISP_MIN_WIDTH, xisp->max_width);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XISP_MIN_HEIGHT, xisp->max_height);

	if (fmt->pad == XVIP_PAD_SOURCE) {
		if (__format->code != MEDIA_BUS_FMT_RBG888_1X24 &&
		    __format->code != MEDIA_BUS_FMT_RBG101010_1X30 &&
		    __format->code != MEDIA_BUS_FMT_RBG121212_1X36 &&
		    __format->code != MEDIA_BUS_FMT_RBG161616_1X48) {
			dev_dbg(xisp->xvip.dev,
				"%s : Unsupported source media bus code format",
				__func__);
			__format->code = MEDIA_BUS_FMT_RBG888_1X24;
		}
	}

	if (fmt->pad == XVIP_PAD_SINK) {
		if (!xisp_get_bayer_format(xisp, __format->code)) {
			dev_dbg(xisp->xvip.dev,
				"Unsupported Sink Pad Media format, defaulting to RGGB");
			__format->code = MEDIA_BUS_FMT_SRGGB10_1X10;
		}
	}

	/* Always propagate Sink image size to Source */
	__propagate->width  = __format->width;
	__propagate->height = __format->height;

	fmt->format = *__format;

	return 0;
}

static const struct v4l2_subdev_pad_ops xisp_pad_ops = {
	.get_fmt = xisp_get_format,
	.set_fmt = xisp_set_format,
};

static const struct v4l2_subdev_ops xisp_ops = {
	.video = &xisp_video_ops,
	.pad = &xisp_pad_ops,
};

static const struct media_entity_operations xisp_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int xisp_parse_of(struct xisp_dev *xisp)
{
	struct device *dev = xisp->xvip.dev;
	struct device_node *node = dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	int rval;

	rval = of_property_read_u16(node, "xlnx,max-height",
				    &xisp->max_height);
	if (rval < 0) {
		dev_err(dev, "missing xlnx,max-height property!");
		return -EINVAL;
	}

	if (xisp->max_height > XISP_MAX_HEIGHT ||
	    xisp->max_height < XISP_MIN_HEIGHT) {
		dev_err(dev, "Invalid height in dt");
		return -EINVAL;
	}

	rval = of_property_read_u16(node, "xlnx,max-width",
				    &xisp->max_width);
	if (rval < 0) {
		dev_err(dev, "missing xlnx,max-width property!");
		return -EINVAL;
	}

	if (xisp->max_width > XISP_MAX_WIDTH ||
	    xisp->max_width < XISP_MIN_WIDTH) {
		dev_err(dev, "Invalid width in dt");
		return -EINVAL;
	}

	rval = of_property_read_u16(node, "xlnx,rgain",
				    &xisp->rgain);
	if (rval < 0) {
		dev_err(dev, "missing xlnx,rgain!");
		return -EINVAL;
	}

	rval = of_property_read_u16(node, "xlnx,bgain",
				    &xisp->bgain);
	if (rval < 0) {
		dev_err(dev, "missing xlnx,bgain!");
		return -EINVAL;
	}

	rval = of_property_read_u16(node, "xlnx,pawb",
				    &xisp->pawb);
	if (rval < 0) {
		dev_err(dev, "missing xlnx,pawb!");
		return -EINVAL;
	}

	rval = of_property_read_bool(node, "xlnx,mode-reg");
	if (rval)
		xisp->mode_reg = of_property_read_bool(node, "xlnx,mode-reg");

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;

	/* Get the format description for each pad */
	for_each_child_of_node(ports, port) {
		struct device_node *endpoint;

		if (!port->name || of_node_cmp(port->name, "port"))
			continue;

		endpoint = of_get_next_child(port, NULL);
		if (!endpoint) {
			dev_err(dev, "No port at\n");
			return -EINVAL;
		}

		/* Count the number of ports. */
		xisp->npads++;
	}

	/* validate number of ports */
	if (xisp->npads > XISP_NO_OF_PADS) {
		dev_err(dev, "invalid number of ports %u\n", xisp->npads);
		return -EINVAL;
	}

	xisp->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xisp->rst_gpio)) {
		if (PTR_ERR(xisp->rst_gpio) != -EPROBE_DEFER)
			dev_err(dev, "Reset GPIO not setup in DT");
		return PTR_ERR(xisp->rst_gpio);
	}

	return 0;
}

static int xisp_probe(struct platform_device *pdev)
{
	struct xisp_dev *xisp;
	struct v4l2_subdev *subdev;
	int rval, itr;

	xisp = devm_kzalloc(&pdev->dev, sizeof(*xisp), GFP_KERNEL);
	if (!xisp)
		return -ENOMEM;

	xisp->xvip.dev = &pdev->dev;

	rval = xisp_parse_of(xisp);
	if (rval < 0)
		return rval;

	rval = xvip_init_resources(&xisp->xvip);
	if (rval)
		return -EIO;

	/* Reset ISP pipeline IP */
	xisp_reset(xisp);

	/* Init V4L2 subdev */
	subdev = &xisp->xvip.subdev;
	v4l2_subdev_init(subdev, &xisp_ops);
	subdev->dev = &pdev->dev;
	strscpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	xisp->gamma_table = xgamma_curves;

	/*
	 * Sink Pad can be any Bayer format.
	 * Default Sink Pad format is RGGB.
	 */
	xisp->formats[XVIP_PAD_SINK].field = V4L2_FIELD_NONE;
	xisp->formats[XVIP_PAD_SINK].colorspace = V4L2_COLORSPACE_SRGB;
	xisp->formats[XVIP_PAD_SINK].width = XISP_MIN_WIDTH;
	xisp->formats[XVIP_PAD_SINK].height = XISP_MIN_HEIGHT;
	xisp->formats[XVIP_PAD_SINK].code = MEDIA_BUS_FMT_SRGGB10_1X10;

	/* Source Pad has a fixed media bus format of RGB */
	xisp->formats[XVIP_PAD_SOURCE].field = V4L2_FIELD_NONE;
	xisp->formats[XVIP_PAD_SOURCE].colorspace = V4L2_COLORSPACE_SRGB;
	xisp->formats[XVIP_PAD_SOURCE].width = XISP_MIN_WIDTH;
	xisp->formats[XVIP_PAD_SOURCE].height = XISP_MIN_HEIGHT;
	xisp->formats[XVIP_PAD_SOURCE].code = MEDIA_BUS_FMT_RBG888_1X24;

	xisp->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xisp->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Init Media Entity */
	subdev->entity.ops = &xisp_media_ops;
	rval = media_entity_pads_init(&subdev->entity, XISP_NO_OF_PADS, xisp->pads);
	if (rval < 0)
		goto media_error;

	/* V4L2 Controls */
	v4l2_ctrl_handler_init(&xisp->ctrl_handler, ARRAY_SIZE(xisp_ctrls));
	for (itr = 0; itr < ARRAY_SIZE(xisp_ctrls); itr++) {
		v4l2_ctrl_new_custom(&xisp->ctrl_handler,
				     &xisp_ctrls[itr], NULL);
	}

	if (xisp->ctrl_handler.error) {
		dev_err(&pdev->dev, "Failed to add V4L2 controls");
		rval = xisp->ctrl_handler.error;
		goto ctrl_error;
	}

	subdev->ctrl_handler = &xisp->ctrl_handler;
	rval = v4l2_ctrl_handler_setup(&xisp->ctrl_handler);
	if (rval < 0) {
		dev_err(&pdev->dev, "Failed to setup control handler");
		goto  ctrl_error;
	}

	platform_set_drvdata(pdev, xisp);
	rval = v4l2_async_register_subdev(subdev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to register subdev");
		goto ctrl_error;
	}

	dev_dbg(&pdev->dev, "Xilinx Video ISP Pipeline Probe Successful");
	return 0;

ctrl_error:
	v4l2_ctrl_handler_free(&xisp->ctrl_handler);
media_error:
	xvip_cleanup_resources(&xisp->xvip);

	return rval;
}

static int xisp_remove(struct platform_device *pdev)
{
	struct xisp_dev *xisp = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xisp->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xisp->xvip);

	return 0;
}

static const struct of_device_id xisp_of_id_table[] = {
	{.compatible = "xlnx,isppipeline-1.0"},
	{ }
};

MODULE_DEVICE_TABLE(of, xisp_of_id_table);

static struct platform_driver xisp_driver = {
	.driver = {
		.name = "xilinx-isppipeline",
		.of_match_table = xisp_of_id_table,
	},
	.probe = xisp_probe,
	.remove = xisp_remove,

};

module_platform_driver(xisp_driver);
MODULE_DESCRIPTION("Xilinx Video ISP Pipeline IP Driver");
MODULE_LICENSE("GPL");

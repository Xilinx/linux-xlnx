// SPDX-License-Identifier: GPL-2.0
/*
 * VPSS CSC DRM bridge driver
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 * Author: Venkateshwar rao G <vgannava@xilinx.com>
 */

/*
 * Overview:
 * This experimentatl driver works as a bridge driver and
 * reused the code from V4L2.
 * TODO:
 * Need to implement in a modular approach to share driver code between
 * V4L2 and DRM frameworks.
 * Should be integrated with plane
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <uapi/linux/media-bus-format.h>

#include "xlnx_bridge.h"

/* Register offset */
#define XV_CSC_AP_CTRL			(0x000)
#define XV_CSC_INVIDEOFORMAT		(0x010)
#define XV_CSC_OUTVIDEOFORMAT		(0x018)
#define XV_CSC_WIDTH			(0x020)
#define XV_CSC_HEIGHT			(0x028)
#define XV_CSC_K11			(0x050)
#define XV_CSC_K12			(0x058)
#define XV_CSC_K13			(0x060)
#define XV_CSC_K21			(0x068)
#define XV_CSC_K22			(0x070)
#define XV_CSC_K23			(0x078)
#define XV_CSC_K31			(0x080)
#define XV_CSC_K32			(0x088)
#define XV_CSC_K33			(0x090)
#define XV_CSC_ROFFSET			(0x098)
#define XV_CSC_GOFFSET			(0x0a0)
#define XV_CSC_BOFFSET			(0x0a8)
#define XV_CSC_CLAMPMIN			(0x0b0)
#define XV_CSC_CLIPMAX			(0x0b8)
#define XV_CSC_SCALE_FACTOR		(4096)
#define XV_CSC_DIVISOR			(10000)
/* Streaming Macros */
#define XCSC_CLAMP_MIN_ZERO		(0)
#define XCSC_AP_START			BIT(0)
#define XCSC_AP_AUTO_RESTART		BIT(7)
#define XCSC_STREAM_ON			(XCSC_AP_START | XCSC_AP_AUTO_RESTART)
#define XCSC_STREAM_OFF			(0)
/* GPIO Reset Assert/De-assert */
#define XCSC_RESET_ASSERT		(1)
#define XCSC_RESET_DEASSERT		(0)

#define XCSC_MIN_WIDTH			(64)
#define XCSC_MAX_WIDTH			(8192)
#define XCSC_MIN_HEIGHT			(64)
#define XCSC_MAX_HEIGHT			(4320)

static const u32 xilinx_csc_video_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_VUY8_1X24,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_VYYUYY8_1X24,
};

/* vpss_csc_color_fmt - Color format type */
enum vpss_csc_color_fmt {
	XVIDC_CSF_RGB = 0,
	XVIDC_CSF_YCRCB_444,
	XVIDC_CSF_YCRCB_422,
	XVIDC_CSF_YCRCB_420,
};

/**
 * struct xilinx_csc - Core configuration of csc device structure
 * @base: pointer to register base address
 * @dev: device structure
 * @bridge: xilinx bridge
 * @cft_in: input color format
 * @cft_out: output color format
 * @color_depth: color depth
 * @k_hw: array of hardware values
 * @clip_max: clipping maximum value
 * @width: width of the video
 * @height: height of video
 * @max_width: maximum number of pixels in a line
 * @max_height: maximum number of lines per frame
 * @rst_gpio: Handle to GPIO specifier to assert/de-assert the reset line
 * @aclk: IP clock struct
 */
struct xilinx_csc {
	void __iomem *base;
	struct device *dev;
	struct xlnx_bridge bridge;
	enum vpss_csc_color_fmt cft_in;
	enum vpss_csc_color_fmt cft_out;
	u32 color_depth;
	s32 k_hw[3][4];
	s32 clip_max;
	u32 width;
	u32 height;
	u32 max_width;
	u32 max_height;
	struct gpio_desc *rst_gpio;
	struct clk *aclk;
};

static inline void xilinx_csc_write(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + offset);
}

static inline u32 xilinx_csc_read(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

/**
 * bridge_to_layer - Gets the parent structure
 * @bridge: pointer to the member.
 *
 * Return: parent structure pointer
 */
static inline struct xilinx_csc *bridge_to_layer(struct xlnx_bridge *bridge)
{
	return container_of(bridge, struct xilinx_csc, bridge);
}

static void xilinx_csc_write_rgb_3x3(struct xilinx_csc *csc)
{
	xilinx_csc_write(csc->base, XV_CSC_K11, csc->k_hw[0][0]);
	xilinx_csc_write(csc->base, XV_CSC_K12, csc->k_hw[0][1]);
	xilinx_csc_write(csc->base, XV_CSC_K13, csc->k_hw[0][2]);
	xilinx_csc_write(csc->base, XV_CSC_K21, csc->k_hw[1][0]);
	xilinx_csc_write(csc->base, XV_CSC_K22, csc->k_hw[1][1]);
	xilinx_csc_write(csc->base, XV_CSC_K23, csc->k_hw[1][2]);
	xilinx_csc_write(csc->base, XV_CSC_K31, csc->k_hw[2][0]);
	xilinx_csc_write(csc->base, XV_CSC_K32, csc->k_hw[2][1]);
	xilinx_csc_write(csc->base, XV_CSC_K33, csc->k_hw[2][2]);
}

static void xilinx_csc_write_rgb_offset(struct xilinx_csc *csc)
{
	xilinx_csc_write(csc->base, XV_CSC_ROFFSET, csc->k_hw[0][3]);
	xilinx_csc_write(csc->base, XV_CSC_GOFFSET, csc->k_hw[1][3]);
	xilinx_csc_write(csc->base, XV_CSC_BOFFSET, csc->k_hw[2][3]);
}

static void xilinx_csc_write_coeff(struct xilinx_csc *csc)
{
	xilinx_csc_write_rgb_3x3(csc);
	xilinx_csc_write_rgb_offset(csc);
}

static void xcsc_set_default_state(struct xilinx_csc *csc)
{
	csc->cft_in = XVIDC_CSF_YCRCB_422;
	csc->cft_out = XVIDC_CSF_YCRCB_422;

	/* This represents an identity matrix mutliped by 2^12 */
	csc->k_hw[0][0] = XV_CSC_SCALE_FACTOR;
	csc->k_hw[0][1] = 0;
	csc->k_hw[0][2] = 0;
	csc->k_hw[1][0] = 0;
	csc->k_hw[1][1] = XV_CSC_SCALE_FACTOR;
	csc->k_hw[1][2] = 0;
	csc->k_hw[2][0] = 0;
	csc->k_hw[2][1] = 0;
	csc->k_hw[2][2] = XV_CSC_SCALE_FACTOR;
	csc->k_hw[0][3] = 0;
	csc->k_hw[1][3] = 0;
	csc->k_hw[2][3] = 0;
	csc->clip_max = ((1 << csc->color_depth) - 1);
	xilinx_csc_write(csc->base, XV_CSC_INVIDEOFORMAT, csc->cft_in);
	xilinx_csc_write(csc->base, XV_CSC_OUTVIDEOFORMAT, csc->cft_out);
	xilinx_csc_write_coeff(csc);
	xilinx_csc_write(csc->base, XV_CSC_CLIPMAX, csc->clip_max);
	xilinx_csc_write(csc->base, XV_CSC_CLAMPMIN, XCSC_CLAMP_MIN_ZERO);
}

static void xcsc_ycrcb_to_rgb(struct xilinx_csc *csc, s32 *clip_max)
{
	u16 bpc_scale = (1 << (csc->color_depth - 8));
	/*
	 * See http://graficaobscura.com/matrix/index.html for
	 * how these numbers are derived. The VPSS CSC IP is
	 * derived from this Matrix style algorithm. And the
	 * 'magic' numbers here are derived from the algorithm.
	 *
	 * XV_CSC_DIVISOR is used to help with floating constants
	 * while performing multiplicative operations.
	 *
	 * Coefficients valid only for BT 709
	 */
	csc->k_hw[0][0] = 11644 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[0][1] = 0;
	csc->k_hw[0][2] = 17927 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[1][0] = 11644 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[1][1] = -2132 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[1][2] = -5329 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[2][0] = 11644 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[2][1] = 21124 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[2][2] = 0;
	csc->k_hw[0][3] = -248 * bpc_scale;
	csc->k_hw[1][3] = 77 * bpc_scale;
	csc->k_hw[2][3] = -289 * bpc_scale;
	*clip_max = ((1 << csc->color_depth) - 1);
}

static void xcsc_rgb_to_ycrcb(struct xilinx_csc *csc, s32 *clip_max)
{
	u16 bpc_scale = (1 << (csc->color_depth - 8));
	/*
	 * See http://graficaobscura.com/matrix/index.html for
	 * how these numbers are derived. The VPSS CSC
	 * derived from this Matrix style algorithm. And the
	 * 'magic' numbers here are derived from the algorithm.
	 *
	 * XV_CSC_DIVISOR is used to help with floating constants
	 * while performing multiplicative operations.
	 *
	 * Coefficients valid only for BT 709
	 */
	csc->k_hw[0][0] = 1826 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[0][1] = 6142 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[0][2] = 620 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[1][0] = -1006 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[1][1] = -3386 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[1][2] = 4392 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[2][0] = 4392 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[2][1] = -3989 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[2][2] = -403 * XV_CSC_SCALE_FACTOR / XV_CSC_DIVISOR;
	csc->k_hw[0][3] = 16 * bpc_scale;
	csc->k_hw[1][3] = 128 * bpc_scale;
	csc->k_hw[2][3] = 128 * bpc_scale;
	*clip_max = ((1 << csc->color_depth) - 1);
}

/**
 * xcsc_set_coeff- Sets the coefficients
 * @csc: Pointer to csc device structure
 *
 * This function set the coefficients
 *
 */
static void xcsc_set_coeff(struct xilinx_csc *csc)
{
	xilinx_csc_write(csc->base, XV_CSC_INVIDEOFORMAT, csc->cft_in);
	xilinx_csc_write(csc->base, XV_CSC_OUTVIDEOFORMAT, csc->cft_out);
	xilinx_csc_write_coeff(csc);
	xilinx_csc_write(csc->base, XV_CSC_CLIPMAX, csc->clip_max);
	xilinx_csc_write(csc->base, XV_CSC_CLAMPMIN, XCSC_CLAMP_MIN_ZERO);
}

/**
 * xilinx_csc_bridge_enable - enabes csc core
 * @bridge: bridge instance
 *
 * This function enables the csc core
 *
 * Return: 0 on success.
 *
 */
static int xilinx_csc_bridge_enable(struct xlnx_bridge *bridge)
{
	struct xilinx_csc *csc = bridge_to_layer(bridge);

	xilinx_csc_write(csc->base, XV_CSC_AP_CTRL, XCSC_STREAM_ON);

	return 0;
}

/**
 * xilinx_csc_bridge_disable - disables csc core
 * @bridge: bridge instance
 *
 * This function disables the csc core
 */
static void xilinx_csc_bridge_disable(struct xlnx_bridge *bridge)
{
	struct xilinx_csc *csc = bridge_to_layer(bridge);

	xilinx_csc_write(csc->base, XV_CSC_AP_CTRL, XCSC_STREAM_OFF);
	/* Reset the Global IP Reset through GPIO */
	gpiod_set_value_cansleep(csc->rst_gpio, XCSC_RESET_ASSERT);
	gpiod_set_value_cansleep(csc->rst_gpio, XCSC_RESET_DEASSERT);
}

/**
 * xilinx_csc_bridge_set_input - Sets the input parameters of csc
 * @bridge: bridge instance
 * @width: width of video
 * @height: height of video
 * @bus_fmt: video bus format
 *
 * This function sets the input parameters of csc
 * Return: 0 on success. -EINVAL for invalid parameters.
 */
static int xilinx_csc_bridge_set_input(struct xlnx_bridge *bridge, u32 width,
				       u32 height, u32 bus_fmt)
{
	struct xilinx_csc *csc = bridge_to_layer(bridge);

	xcsc_set_default_state(csc);

	if (height > csc->max_height || height < XCSC_MIN_HEIGHT)
		return -EINVAL;

	if (width > csc->max_width || width < XCSC_MIN_WIDTH)
		return -EINVAL;

	csc->height = height;
	csc->width = width;

	switch (bus_fmt) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		csc->cft_in = XVIDC_CSF_RGB;
		break;
	case MEDIA_BUS_FMT_VUY8_1X24:
		csc->cft_in = XVIDC_CSF_YCRCB_444;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
		csc->cft_in = XVIDC_CSF_YCRCB_422;
		break;
	case MEDIA_BUS_FMT_VYYUYY8_1X24:
		csc->cft_in = XVIDC_CSF_YCRCB_420;
		break;
	default:
		dev_dbg(csc->dev, "unsupported input video format\n");
		return -EINVAL;
	}

	xilinx_csc_write(csc->base, XV_CSC_WIDTH, width);
	xilinx_csc_write(csc->base, XV_CSC_HEIGHT, height);

	return 0;
}

/**
 * xilinx_csc_bridge_get_input_fmts - input formats supported by csc
 * @bridge: bridge instance
 * @fmts: Pointer to be updated with formats information
 * @count: count of video bus formats
 *
 * This function provides the input video formats information csc
 * Return: 0 on success.
 */
static int xilinx_csc_bridge_get_input_fmts(struct xlnx_bridge *bridge,
					    const u32 **fmts, u32 *count)
{
	*fmts = xilinx_csc_video_fmts;
	*count = ARRAY_SIZE(xilinx_csc_video_fmts);

	return 0;
}

/**
 * xilinx_csc_bridge_set_output - Sets the output parameters of csc
 * @bridge: bridge instance
 * @width: width of video
 * @height: height of video
 * @bus_fmt: video bus format
 *
 * This function sets the output parameters of csc
 * Return: 0 on success. -EINVAL for invalid parameters.
 */
static int xilinx_csc_bridge_set_output(struct xlnx_bridge *bridge, u32 width,
					u32 height, u32 bus_fmt)
{
	struct xilinx_csc *csc = bridge_to_layer(bridge);

	if (width != csc->width || height != csc->height)
		return -EINVAL;

	switch (bus_fmt) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		csc->cft_out = XVIDC_CSF_RGB;
		dev_dbg(csc->dev, "Media Format Out : RGB");
		if (csc->cft_in != MEDIA_BUS_FMT_RBG888_1X24)
			xcsc_ycrcb_to_rgb(csc, &csc->clip_max);
		break;
	case MEDIA_BUS_FMT_VUY8_1X24:
		csc->cft_out = XVIDC_CSF_YCRCB_444;
		dev_dbg(csc->dev, "Media Format Out : YUV 444");
		if (csc->cft_in == MEDIA_BUS_FMT_RBG888_1X24)
			xcsc_rgb_to_ycrcb(csc, &csc->clip_max);
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
		csc->cft_out = XVIDC_CSF_YCRCB_422;
		dev_dbg(csc->dev, "Media Format Out : YUV 422");
		if (csc->cft_in == MEDIA_BUS_FMT_RBG888_1X24)
			xcsc_rgb_to_ycrcb(csc, &csc->clip_max);
		break;
	case MEDIA_BUS_FMT_VYYUYY8_1X24:
		csc->cft_out = XVIDC_CSF_YCRCB_420;
		dev_dbg(csc->dev, "Media Format Out : YUV 420");
		if (csc->cft_in == MEDIA_BUS_FMT_RBG888_1X24)
			xcsc_rgb_to_ycrcb(csc, &csc->clip_max);
		break;
	default:
		dev_info(csc->dev, "unsupported output video format\n");
		return -EINVAL;
	}
	xcsc_set_coeff(csc);

	return 0;
}

/**
 * xilinx_csc_bridge_get_output_fmts - output formats supported by csc
 * @bridge: bridge instance
 * @fmts: Pointer to be updated with formats information
 * @count: count of video bus formats
 *
 * This function provides the output video formats information csc
 * Return: 0 on success.
 */
static int xilinx_csc_bridge_get_output_fmts(struct xlnx_bridge *bridge,
					     const u32 **fmts, u32 *count)
{
	*fmts = xilinx_csc_video_fmts;
	*count = ARRAY_SIZE(xilinx_csc_video_fmts);
	return 0;
}

static int xcsc_parse_of(struct xilinx_csc *csc)
{
	int ret;
	struct device_node *node = csc->dev->of_node;

	csc->aclk = devm_clk_get(csc->dev, NULL);
	if (IS_ERR(csc->aclk)) {
		ret = PTR_ERR(csc->aclk);
		dev_err(csc->dev, "failed to get aclk %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,video-width",
				   &csc->color_depth);
	if (ret < 0) {
		dev_info(csc->dev, "video width not present in DT\n");
		return ret;
	}
	if (csc->color_depth != 8 && csc->color_depth != 10 &&
	    csc->color_depth != 12 && csc->color_depth != 16) {
		dev_err(csc->dev, "Invalid video width in DT\n");
		return -EINVAL;
	}
	/* Reset GPIO */
	csc->rst_gpio = devm_gpiod_get(csc->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(csc->rst_gpio)) {
		if (PTR_ERR(csc->rst_gpio) != -EPROBE_DEFER)
			dev_err(csc->dev, "Reset GPIO not setup in DT");
		return PTR_ERR(csc->rst_gpio);
	}

	ret = of_property_read_u32(node, "xlnx,max-height", &csc->max_height);
	if (ret < 0) {
		dev_err(csc->dev, "xlnx,max-height is missing!");
		return -EINVAL;
	} else if (csc->max_height > XCSC_MAX_HEIGHT ||
		   csc->max_height < XCSC_MIN_HEIGHT) {
		dev_err(csc->dev, "Invalid height in dt");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,max-width", &csc->max_width);
	if (ret < 0) {
		dev_err(csc->dev, "xlnx,max-width is missing!");
		return -EINVAL;
	} else if (csc->max_width > XCSC_MAX_WIDTH ||
		   csc->max_width < XCSC_MIN_WIDTH) {
		dev_err(csc->dev, "Invalid width in dt");
		return -EINVAL;
	}

	return 0;
}

static int xilinx_csc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct xilinx_csc *csc;
	int ret;

	csc = devm_kzalloc(dev, sizeof(*csc), GFP_KERNEL);
	if (!csc)
		return -ENOMEM;

	csc->dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	csc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(csc->base))
		return -ENOMEM;

	platform_set_drvdata(pdev, csc);
	ret = xcsc_parse_of(csc);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(csc->aclk);
	if (ret) {
		dev_err(csc->dev, "failed to enable clock %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(csc->rst_gpio, XCSC_RESET_DEASSERT);
	csc->bridge.enable = &xilinx_csc_bridge_enable;
	csc->bridge.disable = &xilinx_csc_bridge_disable;
	csc->bridge.set_input = &xilinx_csc_bridge_set_input;
	csc->bridge.get_input_fmts = &xilinx_csc_bridge_get_input_fmts;
	csc->bridge.set_output = &xilinx_csc_bridge_set_output;
	csc->bridge.get_output_fmts = &xilinx_csc_bridge_get_output_fmts;
	csc->bridge.of_node = dev->of_node;

	ret = xlnx_bridge_register(&csc->bridge);
	if (ret) {
		dev_info(csc->dev, "Bridge registration failed\n");
		goto err_clk;
	}

	dev_info(csc->dev, "Xilinx VPSS CSC DRM experimental driver probed\n");

	return 0;

err_clk:
	clk_disable_unprepare(csc->aclk);
	return ret;
}

static int xilinx_csc_remove(struct platform_device *pdev)
{
	struct xilinx_csc *csc = platform_get_drvdata(pdev);

	xlnx_bridge_unregister(&csc->bridge);
	clk_disable_unprepare(csc->aclk);

	return 0;
}

static const struct of_device_id xilinx_csc_of_match[] = {
	{ .compatible = "xlnx,vpss-csc"},
	{ }
};
MODULE_DEVICE_TABLE(of, xilinx_csc_of_match);

static struct platform_driver csc_bridge_driver = {
	.probe = xilinx_csc_probe,
	.remove = xilinx_csc_remove,
	.driver = {
		.name = "xlnx,csc-bridge",
		.of_match_table = xilinx_csc_of_match,
	},
};

module_platform_driver(csc_bridge_driver);

MODULE_AUTHOR("Venkateshwar Rao <vgannava@xilinx.com>");
MODULE_DESCRIPTION("Xilinx FPGA CSC Bridge Driver");
MODULE_LICENSE("GPL v2");

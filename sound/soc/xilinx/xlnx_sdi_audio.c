// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SDI embed and extract audio support
 *
 * Copyright (c) 2018 Xilinx Pvt., Ltd
 *
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "xlnx-sdi-audio"

#define XSDIAUD_CNTRL_REG_OFFSET		0x00
#define XSDIAUD_SOFT_RST_REG_OFFSET		0x04
#define XSDIAUD_VER_REG_OFFSET			0x08
#define XSDIAUD_INT_EN_REG_OFFSET		0x0C
#define XSDIAUD_INT_STS_REG_OFFSET		0x10
#define XSDIAUD_EMB_VID_CNTRL_REG_OFFSET	0X14
#define XSDIAUD_AUD_CNTRL_REG_OFFSET		0x18
#define XSDIAUD_AXIS_CHCOUNT_REG_OFFSET		0x1C
#define XSDIAUD_MUX1_OR_DMUX1_CNTRL_REG_OFFSET	0x20
#define XSDIAUD_DMUX1_CNTRL_REG_OFFSET		0x20
#define XSDIAUD_GRP_PRES_REG_OFFSET		0X40
#define XSDIAUD_EXT_CNTRL_PKTSTAT_REG_OFFSET	0X44
#define XSDIAUD_EXT_CH_STAT0_REG_OFFSET		0X48
#define XSDIAUD_GUI_PARAM_REG_OFFSET		0XFC

#define XSDIAUD_CNTRL_EN_MASK		BIT(0)
#define XSDIAUD_SOFT_RST_ACLK_MASK	BIT(0)
#define XSDIAUD_SOFT_RST_SCLK_MASK	BIT(1)
#define XSDIAUD_VER_MASK		GENMASK(7, 0)

#define XSDIAUD_EMB_VID_CNT_STD_MASK	GENMASK(4, 0)
#define XSDIAUD_EMB_VID_CNT_ELE_SHIFT	(5)
#define XSDIAUD_EMB_VID_CNT_ELE_MASK	BIT(5)
#define XSDIAUD_EMB_AUD_CNT_SR_MASK	GENMASK(2, 0)
#define XSDIAUD_EMB_AUD_CNT_SS_SHIFT	(3)
#define XSDIAUD_EMB_AUD_CNT_SS_MASK	BIT(3)
#define XSDIAUD_EMB_AXIS_CHCOUNT_MASK	GENMASK(4, 0)
#define XSDIAUD_EMD_MUX_CNT_GS_MASK	GENMASK(1, 0)

#define XSDIAUD_GRP_PRESNT_MASK		GENMASK(3, 0)
#define XSDIAUD_GRP_PRESNTV_MASK	BIT(4)

#define XSDIAUD_INT_EN_GRP_CHG_MASK	BIT(0)
#define XSDIAUD_EXT_INT_EN_PKT_CHG_MASK	BIT(1)
#define XSDIAUD_EXT_INT_EN_STS_CHG_MASK	BIT(2)
#define XSDIAUD_EXT_INT_EN_FIFO_OF_MASK	BIT(3)
#define XSDIAUD_EXT_INT_EN_PERR_MASK	BIT(4)
#define XSDIAUD_EXT_INT_EN_CERR_MASK	BIT(5)
#define XSDIAUD_INT_ST_GRP_CHG_MASK	BIT(0)
#define XSDIAUD_EXT_INT_ST_PKT_CHG_MASK	BIT(1)
#define XSDIAUD_EXT_INT_ST_STS_CHG_MASK	BIT(2)
#define XSDIAUD_EXT_INT_ST_FIFO_OF_MASK	BIT(3)
#define XSDIAUD_EXT_INT_ST_PERR_MASK	BIT(4)
#define XSDIAUD_EXT_INT_ST_CERR_MASK	BIT(5)
#define XSDIAUD_EXT_AUD_CNT_CP_EN_MASK	BIT(0)
#define XSDIAUD_EXT_AXIS_CHCOUNT_MASK	GENMASK(4, 0)
#define XSDIAUD_EXT_DMUX_GRPS_MASK	GENMASK(1, 0)
#define XSDIAUD_EXT_DMUX_MUTEALL_MASK	GENMASK(5, 2)
#define XSDIAUD_EXT_DMUX_MUTE1_MASK	BIT(2)
#define XSDIAUD_EXT_DMUX_MUTE2_MASK	BIT(3)
#define XSDIAUD_EXT_PKTST_AC_MASK	GENMASK(27, 12)

/* audio params macros */
#define PROF_SAMPLERATE_MASK		GENMASK(7, 6)
#define PROF_SAMPLERATE_SHIFT		6
#define PROF_CHANNEL_COUNT_MASK		GENMASK(11, 8)
#define PROF_CHANNEL_COUNT_SHIFT	8
#define PROF_MAX_BITDEPTH_MASK		GENMASK(18, 16)
#define PROF_MAX_BITDEPTH_SHIFT		16
#define PROF_BITDEPTH_MASK		GENMASK(21, 19)
#define PROF_BITDEPTH_SHIFT		19

#define AES_FORMAT_MASK			BIT(0)
#define PROF_SAMPLERATE_UNDEFINED	0
#define PROF_SAMPLERATE_44100		1
#define PROF_SAMPLERATE_48000		2
#define PROF_SAMPLERATE_32000		3
#define PROF_CHANNELS_UNDEFINED		0
#define PROF_TWO_CHANNELS		8
#define PROF_STEREO_CHANNELS		2
#define PROF_MAX_BITDEPTH_UNDEFINED	0
#define PROF_MAX_BITDEPTH_20		2
#define PROF_MAX_BITDEPTH_24		4

#define CON_SAMPLE_RATE_MASK		GENMASK(27, 24)
#define CON_SAMPLE_RATE_SHIFT		24
#define CON_CHANNEL_COUNT_MASK		GENMASK(23, 20)
#define CON_CHANNEL_COUNT_SHIFT		20
#define CON_MAX_BITDEPTH_MASK		BIT(1)
#define CON_BITDEPTH_MASK		GENMASK(3, 1)
#define CON_BITDEPTH_SHIFT		0x1

#define CON_SAMPLERATE_44100		0
#define CON_SAMPLERATE_48000		2
#define CON_SAMPLERATE_32000		3

enum IP_MODE {
	EMBED,
	EXTRACT,
};

struct dev_ctx {
	enum IP_MODE mode;
	void __iomem *base;
	struct device *dev;
};

static void audio_enable(void __iomem *aud_base)
{
	u32 val;

	val = readl(aud_base + XSDIAUD_CNTRL_REG_OFFSET);
	val |= XSDIAUD_CNTRL_EN_MASK;
	writel(val, aud_base + XSDIAUD_CNTRL_REG_OFFSET);
}

static void audio_disable(void __iomem *aud_base)
{
	u32 val;

	val = readl(aud_base + XSDIAUD_CNTRL_REG_OFFSET);
	val &= ~XSDIAUD_CNTRL_EN_MASK;
	writel(val, aud_base + XSDIAUD_CNTRL_REG_OFFSET);
}

static void audio_reset_core(void __iomem *aud_base, bool reset)
{
	u32 val;

	if (reset) {
		/* reset the core */
		val = readl(aud_base + XSDIAUD_SOFT_RST_REG_OFFSET);
		val |= XSDIAUD_SOFT_RST_SCLK_MASK;
		writel(val, aud_base + XSDIAUD_SOFT_RST_REG_OFFSET);
	} else {
		/* bring the core out of reset */
		val = readl(aud_base + XSDIAUD_SOFT_RST_REG_OFFSET);
		val &= ~XSDIAUD_SOFT_RST_SCLK_MASK;
		writel(val, aud_base + XSDIAUD_SOFT_RST_REG_OFFSET);
	}
}

static int xlnx_sdi_audio_probe(struct platform_device *pdev)
{
	u32 val;
	struct dev_ctx *ctx;
	struct resource *res;

	ctx = devm_kzalloc(&pdev->dev, sizeof(struct dev_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No IO MEM resource found\n");
		return -ENODEV;
	}

	ctx->base = devm_ioremap_resource(&pdev->dev, res);
	if (!ctx->base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -EADDRNOTAVAIL;
	}

	ctx->dev = &pdev->dev;

	val = readl(ctx->base + XSDIAUD_GUI_PARAM_REG_OFFSET);
	if (val & BIT(6))
		ctx->mode = EXTRACT;
	else
		ctx->mode = EMBED;

	dev_set_drvdata(&pdev->dev, ctx);

	audio_reset_core(ctx->base, true);
	audio_reset_core(ctx->base, false);
	audio_enable(ctx->base);

	return 0;
}

static int xlnx_sdi_audio_remove(struct platform_device *pdev)
{
	struct dev_ctx *ctx = dev_get_drvdata(&pdev->dev);

	audio_disable(ctx->base);
	audio_reset_core(ctx->base, true);

	return 0;
}

static const struct of_device_id xlnx_sdi_audio_of_match[] = {
	{ .compatible = "xlnx,v-uhdsdi-audio-1.0"},
	{ }
};
MODULE_DEVICE_TABLE(of, xlnx_sdi_audio_of_match);

static struct platform_driver xlnx_sdi_audio_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = xlnx_sdi_audio_of_match,
	},
	.probe = xlnx_sdi_audio_probe,
	.remove = xlnx_sdi_audio_remove,
};

module_platform_driver(xlnx_sdi_audio_driver);

MODULE_DESCRIPTION("xilinx sdi audio codec driver");
MODULE_AUTHOR("Maruthi Srinivas Bayyavarapu");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);

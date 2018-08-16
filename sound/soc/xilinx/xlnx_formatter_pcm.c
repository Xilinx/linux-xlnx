// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ASoC audio formatter support
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#define XLNX_S2MM_OFFSET	0
#define XLNX_MM2S_OFFSET	0x100

#define XLNX_AUD_CORE_CONFIG	0x4
#define XLNX_AUD_CTRL		0x10
#define XLNX_AUD_STS		0x14

#define AUD_CTRL_RESET_MASK	BIT(1)
#define AUD_CFG_MM2S_MASK	BIT(15)
#define AUD_CFG_S2MM_MASK	BIT(31)

struct xlnx_pcm_drv_data {
	void __iomem *mmio;
	bool s2mm_presence;
	bool mm2s_presence;
	unsigned int s2mm_irq;
	unsigned int mm2s_irq;
};

void xlnx_formatter_pcm_reset(void __iomem *mmio_base)
{
	u32 val;

	val = readl(mmio_base + XLNX_AUD_CTRL);
	val |= AUD_CTRL_RESET_MASK;
	writel(val, mmio_base + XLNX_AUD_CTRL);
}

static int xlnx_formatter_pcm_probe(struct platform_device *pdev)
{
	int ret = 0;
	u32 val;
	struct xlnx_pcm_drv_data *aud_drv_data;
	struct resource *res;

	aud_drv_data = devm_kzalloc(&pdev->dev,
				    sizeof(*aud_drv_data), GFP_KERNEL);
	if (!aud_drv_data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"audio formatter node:addr to resource failed\n");
		return -ENXIO;
	}
	aud_drv_data->mmio = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(aud_drv_data->mmio)) {
		dev_err(&pdev->dev, "audio formatter ioremap failed\n");
		return PTR_ERR(aud_drv_data->mmio);
	}

	val = readl(aud_drv_data->mmio + XLNX_AUD_CORE_CONFIG);
	if (val & AUD_CFG_MM2S_MASK) {
		aud_drv_data->mm2s_presence = true;
		aud_drv_data->mm2s_irq = platform_get_irq_byname(pdev,
								 "irq_mm2s");
		if (!aud_drv_data->mm2s_irq) {
			dev_err(&pdev->dev, "xlnx audio mm2s irq resource failed\n");
			return aud_drv_data->mm2s_irq;
		}
		xlnx_formatter_pcm_reset(aud_drv_data->mmio + XLNX_MM2S_OFFSET);
	}
	if (val & AUD_CFG_S2MM_MASK) {
		aud_drv_data->s2mm_presence = true;
		aud_drv_data->s2mm_irq = platform_get_irq_byname(pdev,
								 "irq_s2mm");
		if (!aud_drv_data->s2mm_irq) {
			dev_err(&pdev->dev, "xlnx audio s2mm irq resource failed\n");
			return aud_drv_data->s2mm_irq;
		}
		xlnx_formatter_pcm_reset(aud_drv_data->mmio + XLNX_S2MM_OFFSET);
	}

	dev_set_drvdata(&pdev->dev, aud_drv_data);

	return ret;
}

static int xlnx_formatter_pcm_remove(struct platform_device *pdev)
{
	struct xlnx_pcm_drv_data *adata = dev_get_drvdata(&pdev->dev);

	if (adata->s2mm_presence)
		xlnx_formatter_pcm_reset(adata->mmio + XLNX_S2MM_OFFSET);

	if (adata->mm2s_presence)
		xlnx_formatter_pcm_reset(adata->mmio + XLNX_MM2S_OFFSET);

	return 0;
}

static const struct of_device_id xlnx_formatter_pcm_of_match[] = {
	{ .compatible = "xlnx,audio-formatter-1.0"},
	{},
};
MODULE_DEVICE_TABLE(of, xlnx_formatter_pcm_of_match);

static struct platform_driver xlnx_formatter_pcm_driver = {
	.probe	= xlnx_formatter_pcm_probe,
	.remove	= xlnx_formatter_pcm_remove,
	.driver	= {
		.name	= "xlnx_formatter_pcm",
		.of_match_table	= xlnx_formatter_pcm_of_match,
	},
};

module_platform_driver(xlnx_formatter_pcm_driver);
MODULE_AUTHOR("Maruthi Srinivas Bayyavarapu");
MODULE_LICENSE("GPL v2");

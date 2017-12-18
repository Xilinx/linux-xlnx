/*
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

static int qfprom_reg_read(void *context,
			unsigned int reg, void *_val, size_t bytes)
{
	void __iomem *base = context;
	u32 *val = _val;
	int i = 0, words = bytes / 4;

	while (words--)
		*val++ = readl(base + reg + (i++ * 4));

	return 0;
}

static int qfprom_reg_write(void *context,
			 unsigned int reg, void *_val, size_t bytes)
{
	void __iomem *base = context;
	u32 *val = _val;
	int i = 0, words = bytes / 4;

	while (words--)
		writel(*val++, base + reg + (i++ * 4));

	return 0;
}

static int qfprom_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static struct nvmem_config econfig = {
	.name = "qfprom",
	.owner = THIS_MODULE,
	.stride = 4,
	.word_size = 1,
	.reg_read = qfprom_reg_read,
	.reg_write = qfprom_reg_write,
};

static int qfprom_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	void __iomem *base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	econfig.size = resource_size(res);
	econfig.dev = dev;
	econfig.priv = base;

	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static const struct of_device_id qfprom_of_match[] = {
	{ .compatible = "qcom,qfprom",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, qfprom_of_match);

static struct platform_driver qfprom_driver = {
	.probe = qfprom_probe,
	.remove = qfprom_remove,
	.driver = {
		.name = "qcom,qfprom",
		.of_match_table = qfprom_of_match,
	},
};
module_platform_driver(qfprom_driver);
MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org>");
MODULE_DESCRIPTION("Qualcomm QFPROM driver");
MODULE_LICENSE("GPL v2");

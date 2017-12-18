/*
 * Allwinner sunXi SoCs Security ID support.
 *
 * Copyright (c) 2013 Oliver Schinagl <oliver@schinagl.nl>
 * Copyright (C) 2014 Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/random.h>

static struct nvmem_config econfig = {
	.name = "sunxi-sid",
	.read_only = true,
	.stride = 4,
	.word_size = 1,
	.owner = THIS_MODULE,
};

struct sunxi_sid {
	void __iomem		*base;
};

/* We read the entire key, due to a 32 bit read alignment requirement. Since we
 * want to return the requested byte, this results in somewhat slower code and
 * uses 4 times more reads as needed but keeps code simpler. Since the SID is
 * only very rarely probed, this is not really an issue.
 */
static u8 sunxi_sid_read_byte(const struct sunxi_sid *sid,
			      const unsigned int offset)
{
	u32 sid_key;

	sid_key = ioread32be(sid->base + round_down(offset, 4));
	sid_key >>= (offset % 4) * 8;

	return sid_key; /* Only return the last byte */
}

static int sunxi_sid_read(void *context, unsigned int offset,
			  void *val, size_t bytes)
{
	struct sunxi_sid *sid = context;
	u8 *buf = val;

	while (bytes--)
		*buf++ = sunxi_sid_read_byte(sid, offset++);

	return 0;
}

static int sunxi_sid_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct sunxi_sid *sid;
	int ret, i, size;
	char *randomness;

	sid = devm_kzalloc(dev, sizeof(*sid), GFP_KERNEL);
	if (!sid)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sid->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(sid->base))
		return PTR_ERR(sid->base);

	size = resource_size(res) - 1;
	econfig.size = resource_size(res);
	econfig.dev = dev;
	econfig.reg_read = sunxi_sid_read;
	econfig.priv = sid;
	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	randomness = kzalloc(sizeof(u8) * (size), GFP_KERNEL);
	if (!randomness) {
		ret = -EINVAL;
		goto err_unreg_nvmem;
	}

	for (i = 0; i < size; i++)
		randomness[i] = sunxi_sid_read_byte(sid, i);

	add_device_randomness(randomness, size);
	kfree(randomness);

	platform_set_drvdata(pdev, nvmem);

	return 0;

err_unreg_nvmem:
	nvmem_unregister(nvmem);
	return ret;
}

static int sunxi_sid_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static const struct of_device_id sunxi_sid_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-sid" },
	{ .compatible = "allwinner,sun7i-a20-sid" },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, sunxi_sid_of_match);

static struct platform_driver sunxi_sid_driver = {
	.probe = sunxi_sid_probe,
	.remove = sunxi_sid_remove,
	.driver = {
		.name = "eeprom-sunxi-sid",
		.of_match_table = sunxi_sid_of_match,
	},
};
module_platform_driver(sunxi_sid_driver);

MODULE_AUTHOR("Oliver Schinagl <oliver@schinagl.nl>");
MODULE_DESCRIPTION("Allwinner sunxi security id driver");
MODULE_LICENSE("GPL");

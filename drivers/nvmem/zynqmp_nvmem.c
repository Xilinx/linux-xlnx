/*
 * Copyright (C) 2017 Xilinx, Inc.
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

#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/firmware/xilinx/zynqmp/firmware.h>

#define SILICON_REVISION_MASK 0xF

static int zynqmp_nvmem_read(void *context, unsigned int offset,
					void *val, size_t bytes)
{
	int ret;
	int idcode, version;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->get_chipid)
		return -ENXIO;

	ret = eemi_ops->get_chipid(&idcode, &version);
	if (ret < 0)
		return ret;

	pr_debug("Read chipid val %x %x\n", idcode, version);
	*(int *)val = version & SILICON_REVISION_MASK;

	return 0;
}

static struct nvmem_config econfig = {
	.name = "zynqmp-nvmem",
	.owner = THIS_MODULE,
	.word_size = 1,
	.size = 1,
	.read_only = true,
};

static const struct of_device_id zynqmp_nvmem_match[] = {
	{ .compatible = "xlnx,zynqmp-nvmem-fw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_nvmem_match);

static int zynqmp_nvmem_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;

	econfig.dev = &pdev->dev;
	econfig.reg_read = zynqmp_nvmem_read;

	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int zynqmp_nvmem_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static struct platform_driver zynqmp_nvmem_driver = {
	.probe = zynqmp_nvmem_probe,
	.remove = zynqmp_nvmem_remove,
	.driver = {
		.name = "zynqmp-nvmem",
		.of_match_table = zynqmp_nvmem_match,
	},
};

module_platform_driver(zynqmp_nvmem_driver);

MODULE_AUTHOR("Michal Simek <michal.simek@xilinx.com>, Nava kishore Manne <navam@xilinx.com>");
MODULE_DESCRIPTION("ZynqMP NVMEM driver");
MODULE_LICENSE("GPL");

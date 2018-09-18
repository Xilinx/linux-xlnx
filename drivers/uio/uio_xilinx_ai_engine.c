// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx UIO driver for AI Engine
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_data/uio_dmem_genirq.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "xilinx-aiengine"

static uint xilinx_ai_engine_mem_cnt = 1;
module_param_named(mem_cnt, xilinx_ai_engine_mem_cnt, uint, 0444);
MODULE_PARM_DESC(mem_cnt, "Dynamic memory allocation count (default: 1)");

static uint xilinx_ai_engine_mem_size = 32 * 1024 * 1024;
module_param_named(mem_size, xilinx_ai_engine_mem_size, uint, 0444);
MODULE_PARM_DESC(mem_size,
		 "Dynamic memory allocation size in bytes (default: 32 MB)");

static int xilinx_ai_engine_probe(struct platform_device *pdev)
{
	struct platform_device *uio;
	struct uio_dmem_genirq_pdata *pdata;
	unsigned int i;
	int ret;

	uio = platform_device_alloc(DRIVER_NAME, PLATFORM_DEVID_NONE);
	if (!uio)
		return -ENOMEM;
	uio->driver_override = "uio_dmem_genirq";
	uio->dev.parent = &pdev->dev;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		ret = -ENOMEM;
		goto err_out;
	}

	pdata->num_dynamic_regions = xilinx_ai_engine_mem_cnt;
	pdata->dynamic_region_sizes = &xilinx_ai_engine_mem_size;
	pdata->uioinfo.name = DRIVER_NAME;
	pdata->uioinfo.version = "devicetree";
	pdata->uioinfo.irq = UIO_IRQ_CUSTOM;
	/* Set the offset value as it's map index for each memory */
	for (i = 0; i < MAX_UIO_MAPS; i++)
		pdata->uioinfo.mem[i].offs = i << PAGE_SHIFT;
	ret = platform_device_add_data(uio, pdata, sizeof(*pdata));
	if (ret)
		goto err_out;

	/* Mirror the parent device resource to uio device */
	ret = platform_device_add_resources(uio, pdev->resource,
					    pdev->num_resources);
	if (ret)
		goto err_out;

	/* Configure the dma for uio device using the parent of_node */
	uio->dev.bus = &platform_bus_type;
	ret = of_dma_configure(&uio->dev, of_node_get(pdev->dev.of_node), true);
	of_node_put(pdev->dev.of_node);
	if (ret)
		goto err_out;

	ret = platform_device_add(uio);
	if (ret)
		goto err_out;
	platform_set_drvdata(uio, pdata);

	dev_info(&pdev->dev, "Xilinx AI Engine UIO driver probed");
	return 0;

err_out:
	platform_device_put(pdev);
	dev_err(&pdev->dev,
		"failed to probe Xilinx AI Engine UIO driver");
	return ret;
}

static int xilinx_ai_engine_remove(struct platform_device *pdev)
{
	struct platform_device *uio = platform_get_drvdata(pdev);

	platform_device_unregister(uio);
	of_node_put(pdev->dev.of_node);

	return 0;
}

static const struct of_device_id xilinx_ai_engine_of_match[] = {
	{ .compatible = "xlnx,ai_engine", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_ai_engine_of_match);

static struct platform_driver xilinx_ai_engine_driver = {
	.probe			= xilinx_ai_engine_probe,
	.remove			= xilinx_ai_engine_remove,
	.driver			= {
		.name		= DRIVER_NAME,
		.of_match_table	= xilinx_ai_engine_of_match,
	},
};

module_platform_driver(xilinx_ai_engine_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_LICENSE("GPL v2");

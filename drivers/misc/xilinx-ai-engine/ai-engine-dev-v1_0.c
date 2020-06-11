// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/anon_inodes.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

#define VERSAL_SILICON_REV_MASK		GENMASK(31, 28)

/**
 * xilinx_ai_engine_probe_v1() - probe device tree v1.0 AI engine device
 * @pdev: AI engine platform device
 * @return: 0 for success, negative value for failure
 */
int xilinx_ai_engine_probe_v1(struct platform_device *pdev)
{
	struct aie_device *adev;
	struct aie_aperture *aperture;
	struct aie_range *range;
	struct device *dev;
	struct device_node *nc;
	struct resource *res;
	u32 idcode, version, pm_reg[2], regs[4];
	int ret;

	dev_info(&pdev->dev, "probing xlnx,ai-engine-v1.0 device.\n");

	adev = devm_kzalloc(&pdev->dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	platform_set_drvdata(pdev, adev);
	INIT_LIST_HEAD(&adev->apertures);
	mutex_init(&adev->mlock);

	/* Initialize AIE device specific instance. */
	ret = aie_device_init(adev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize device instance.\n");
		return ret;
	}

	/*
	 * AI Engine platform management node ID is required for requesting
	 * services from firmware driver.
	 */
	ret = of_property_read_u32_array(pdev->dev.of_node, "power-domains",
					 pm_reg, ARRAY_SIZE(pm_reg));
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to read power manangement information\n");
		return ret;
	}
	adev->pm_node_id = pm_reg[1];

	ret = zynqmp_pm_get_chipid(&idcode, &version);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get chip ID\n");
		return ret;
	}
	adev->version = FIELD_GET(VERSAL_SILICON_REV_MASK, idcode);

	adev->clk = devm_clk_get(&pdev->dev, NULL);
	if (!adev->clk) {
		dev_err(&pdev->dev, "Failed to get device clock.\n");
		return -EINVAL;
	}

	ret = xilinx_ai_engine_add_dev(adev, pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add AI engine device.\n");
		return ret;
	}

	/* v1 support single aperture only */
	nc = of_get_next_child(pdev->dev.of_node, NULL);
	if (!nc) {
		dev_err(&pdev->dev,
			"device tree node v1.0, no child node.\n");
		put_device(dev);
		return -EINVAL;
	}

	aperture = kzalloc(sizeof(*aperture), GFP_KERNEL);
	if (!aperture) {
		put_device(dev);
		return -ENOMEM;
	}

	aperture->adev = adev;
	INIT_LIST_HEAD(&aperture->partitions);
	mutex_init(&aperture->mlock);

	ret = of_property_read_u32_array(nc, "reg", regs,
					 ARRAY_SIZE(regs));
	if (ret < 0) {
		dev_err(&adev->dev,
			"probe %pOF failed, no tiles range information.\n",
			nc);
		kfree(aperture);
		put_device(dev);
		return ret;
	}
	range = &aperture->range;
	range->start.col = regs[0] & aligned_byte_mask(1);
	range->size.col = regs[2] & aligned_byte_mask(1);
	range->start.row = 0;
	range->size.row = adev->ttype_attr[AIE_TILE_TYPE_SHIMPL].num_rows +
			  adev->ttype_attr[AIE_TILE_TYPE_TILE].num_rows;

	/* register device for aperture */
	ret = aie_aperture_add_dev(aperture, nc);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to add AI engine aperture device\n");
		kfree(aperture);
		put_device(dev);
		return ret;
	}

	/**
	 * Initialize columns resource map to remember which columns have been
	 * assigned. Used for partition management.
	 */
	ret = aie_resource_initialize(&aperture->cols_res,
				      aperture->range.size.col);
	if (ret) {
		dev_err(dev, "failed to initialize columns resource.\n");
		put_device(dev);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No memory resource.\n");
		put_device(&aperture->dev);
		put_device(dev);
		return -EINVAL;
	}
	/* resource information will be used by ready only register mmap */
	memcpy(&aperture->res, res, sizeof(*res));
	aperture->base = devm_ioremap_resource(&aperture->dev, &aperture->res);
	if (IS_ERR(aperture->base)) {
		dev_err(&pdev->dev, "no io memory resource.\n");
		ret = PTR_ERR(aperture->base);
		put_device(&aperture->dev);
		put_device(dev);
		return ret;
	}

	/* Get device node DMA setting */
	aperture->dev.coherent_dma_mask = DMA_BIT_MASK(48);
	aperture->dev.dma_mask = &aperture->dev.coherent_dma_mask;
	ret = of_dma_configure(&aperture->dev, nc, true);
	if (ret)
		dev_warn(&aperture->dev, "Failed to configure DMA.\n");

	INIT_WORK(&aperture->backtrack, aie_aperture_backtrack);
	ret = aie_aperture_create_l2_bitmap(aperture);
	if (ret) {
		dev_err(&aperture->dev,
			"failed to initialize l2 mask resource.\n");
		put_device(&aperture->dev);
		put_device(dev);
		return ret;
	}

	ret = platform_get_irq_byname(pdev, "interrupt1");
	if (ret < 0) {
		put_device(&aperture->dev);
		put_device(dev);
		return ret;
	}
	aperture->irq = ret;

	ret = devm_request_threaded_irq(&aperture->dev, aperture->irq, NULL,
					aie_interrupt, IRQF_ONESHOT,
					dev_name(&aperture->dev), aperture);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request AIE IRQ.\n");
		put_device(&aperture->dev);
		put_device(dev);
		return ret;
	}

	of_node_get(nc);

	list_add_tail(&aperture->node, &adev->apertures);

	dev_info(&pdev->dev, "ai-enginee-v1.0 device node is probed.\n");
	return 0;
}

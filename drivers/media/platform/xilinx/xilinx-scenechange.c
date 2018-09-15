//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Scene Change Detection driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Author: Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>
 */

#include "xilinx-scenechange.h"

#define XSCD_RESET_DEASSERT	(0)
#define XSCD_RESET_ASSERT	(1)

static irqreturn_t xscd_irq_handler(int irq, void *data)
{
	struct xscd_device *xscd = (struct xscd_device *)data;
	u32 status;

	status = xscd_read(xscd->iomem, XILINX_XSCD_ISR_OFFSET);
	if (!(status & XILINX_XSCD_IE_AP_DONE))
		return IRQ_NONE;

	xscd_write(xscd->iomem, XILINX_XSCD_ISR_OFFSET, XILINX_XSCD_IE_AP_DONE);
	return IRQ_HANDLED;
}

static struct platform_device *xscd_chan_alloc(struct platform_device *pdev,
					       struct device_node *subdev,
					       int id)
{
	struct platform_device *xscd_chan_pdev;
	int ret;

	xscd_chan_pdev = platform_device_alloc("xlnx-scdchan", id);
	if (!xscd_chan_pdev)
		return ERR_PTR(-ENOMEM);

	xscd_chan_pdev->dev.parent = &pdev->dev;
	xscd_chan_pdev->dev.of_node = subdev;

	ret = platform_device_add(xscd_chan_pdev);
	if (ret)
		goto error;

	return xscd_chan_pdev;

error:
	platform_device_unregister(xscd_chan_pdev);
	return ERR_PTR(ret);
}

static void xscd_chan_remove(struct platform_device *dev)
{
	platform_device_unregister(dev);
}

static
struct platform_device *xlnx_scdma_device_init(struct platform_device *pdev,
					       struct device_node *node)
{
	struct platform_device *dma;
	int ret;

	dma = platform_device_alloc("xlnx,scdma", 0);
	if (!dma)
		return ERR_PTR(-ENOMEM);

	dma->dev.parent = &pdev->dev;
	ret = platform_device_add(dma);
	if (ret)
		goto error;

	return dma;

error:
	platform_device_unregister(dma);
	return ERR_PTR(ret);
}

static void xilinx_scdma_device_exit(struct platform_device *dev)
{
	platform_device_unregister(dev);
}

static int xscd_init_resources(struct xscd_device *xscd)
{
	struct platform_device *pdev = to_platform_device(xscd->dev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xscd->iomem = devm_ioremap_resource(xscd->dev, res);
	if (IS_ERR(xscd->iomem))
		return PTR_ERR(xscd->iomem);

	xscd->clk = devm_clk_get(xscd->dev, NULL);
	if (IS_ERR(xscd->clk))
		return PTR_ERR(xscd->clk);

	clk_prepare_enable(xscd->clk);
	return 0;
}

static int xscd_parse_of(struct xscd_device *xscd)
{
	struct device *dev = xscd->dev;
	struct device_node *node = xscd->dev->of_node;
	int ret;

	xscd->memorybased = of_property_read_bool(node, "xlnx,memorybased");
	xscd->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xscd->rst_gpio)) {
		if (PTR_ERR(xscd->rst_gpio) != -EPROBE_DEFER)
			dev_err(dev, "Reset GPIO not setup in DT\n");

		return PTR_ERR(xscd->rst_gpio);
	}

	ret = of_property_read_u32(node, "xlnx,numstreams",
				   &xscd->numstreams);
	if (ret < 0)
		return ret;

	xscd->irq = irq_of_parse_and_map(node, 0);
	ret = devm_request_irq(xscd->dev, xscd->irq, xscd_irq_handler,
			       IRQF_SHARED, dev_name(xscd->dev), xscd);

	return 0;
}

static int xscd_probe(struct platform_device *pdev)
{
	struct xscd_device *xscd;
	int ret;
	u32 id = 0, i;
	struct device_node *subdev_node, *node;
	struct platform_device *subdev;

	xscd = devm_kzalloc(&pdev->dev, sizeof(*xscd), GFP_KERNEL);
	if (!xscd)
		return -ENOMEM;

	/*
	 * Memory based is enabled by default, this can be used for streaming
	 * based driver
	 */
	xscd->memorybased = true;
	xscd->dev = &pdev->dev;
	node = pdev->dev.of_node;

	ret = xscd_parse_of(xscd);
	if (ret < 0)
		return ret;

	ret = xscd_init_resources(xscd);
	if (ret < 0)
		return ret;

	/* Reset Scene Change Detection IP */
	gpiod_set_value_cansleep(xscd->rst_gpio, XSCD_RESET_ASSERT);
	gpiod_set_value_cansleep(xscd->rst_gpio, XSCD_RESET_DEASSERT);

	xscd->shared_data.iomem = xscd->iomem;
	platform_set_drvdata(pdev, (void *)&xscd->shared_data);
	for_each_child_of_node(node, subdev_node) {
		subdev = xscd_chan_alloc(pdev, subdev_node, id);
		if (IS_ERR(subdev)) {
			dev_err(&pdev->dev,
				"Failed to initialize the subdev@%d\n", id);
			ret = PTR_ERR(subdev);
			goto cleanup;
		}
		xscd->subdevs[id] = subdev;
		id++;
	}

	if (xscd->memorybased) {
		xscd->dma_device = xlnx_scdma_device_init(pdev, xscd->dma_node);
		if (IS_ERR(xscd->dma_node)) {
			ret = IS_ERR(xscd->dma_node);
			dev_err(&pdev->dev, "Failed to initialize the DMA\n");
			goto cleanup;
		}
	}

	dev_info(xscd->dev, "scene change detect device found!\n");
	return 0;

cleanup:
	for (i = 0; i < xscd->numstreams; i++) {
		if (xscd->subdevs[i])
			xscd_chan_remove(xscd->subdevs[i]);
	}

	return ret;
}

static int xscd_remove(struct platform_device *pdev)
{
	struct xscd_device *xscd = platform_get_drvdata(pdev);
	u32 i;

	if (xscd->memorybased) {
		xilinx_scdma_device_exit(xscd->dma_device);
		xscd->dma_node = NULL;
	}

	for (i = 0; i < xscd->numstreams; i++)
		xscd_chan_remove(xscd->subdevs[i]);

	clk_disable_unprepare(xscd->clk);
	return 0;
}

static const struct of_device_id xscd_of_id_table[] = {
	{ .compatible = "xlnx,v-scd" },
	{ }
};
MODULE_DEVICE_TABLE(of, xscd_of_id_table);

static struct platform_driver xscd_driver = {
	.driver = {
		.name		= "xilinx-scd",
		.of_match_table	= xscd_of_id_table,
	},
	.probe			= xscd_probe,
	.remove			= xscd_remove,
};

module_platform_driver(xscd_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx Scene Change Detection");
MODULE_LICENSE("GPL v2");

//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Scene Change Detection driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Authors: Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>
 *          Satish Kumar Nagireddy <satish.nagireddy.nagireddy@xilinx.com>
 */

#include "xilinx-scenechange.h"

#define XSCD_RESET_DEASSERT	(0)
#define XSCD_RESET_ASSERT	(1)

static irqreturn_t xscd_irq_handler(int irq, void *data)
{
	struct xscd_device *xscd = (struct xscd_device *)data;
	unsigned int i;
	u32 status;

	status = xscd_read(xscd->iomem, XSCD_ISR_OFFSET);
	if (!(status & XSCD_IE_AP_DONE))
		return IRQ_NONE;

	xscd_write(xscd->iomem, XSCD_ISR_OFFSET, XSCD_IE_AP_DONE);

	for (i = 0; i < xscd->numstreams; ++i)
		xscd_chan_irq_handler(xscd->chans[i]);

	return IRQ_HANDLED;
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

	xscd->shared_data.memory_based =
			of_property_read_bool(node, "xlnx,memorybased");
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

	if (!xscd->shared_data.memory_based && xscd->numstreams != 1) {
		dev_err(dev, "Stream-based mode only supports one stream\n");
		return -EINVAL;
	}

	xscd->irq = irq_of_parse_and_map(node, 0);
	if (!xscd->irq) {
		dev_err(xscd->dev, "No valid irq found\n");
		return -EINVAL;
	}

	ret = devm_request_irq(xscd->dev, xscd->irq, xscd_irq_handler,
			       IRQF_SHARED, dev_name(xscd->dev), xscd);

	return 0;
}

static int xscd_probe(struct platform_device *pdev)
{
	struct xscd_device *xscd;
	struct device_node *subdev_node;
	unsigned int id;
	int ret;

	xscd = devm_kzalloc(&pdev->dev, sizeof(*xscd), GFP_KERNEL);
	if (!xscd)
		return -ENOMEM;

	xscd->dev = &pdev->dev;

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
	xscd->shared_data.dma_chan_list = xscd->channels;
	platform_set_drvdata(pdev, (void *)&xscd->shared_data);

	id = 0;
	for_each_child_of_node(xscd->dev->of_node, subdev_node) {
		if (id >= xscd->numstreams) {
			dev_warn(&pdev->dev,
				 "Too many channels, limiting to %u\n",
				 xscd->numstreams);
			of_node_put(subdev_node);
			break;
		}

		ret = xscd_chan_init(xscd, id, subdev_node);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to initialize channel %u\n",
				id);
			return ret;
		}

		id++;
	}

	ret = xscd_dma_init(xscd);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to initialize the DMA\n");

	dev_info(xscd->dev, "scene change detect device found!\n");
	return 0;
}

static int xscd_remove(struct platform_device *pdev)
{
	struct xscd_device *xscd = platform_get_drvdata(pdev);

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

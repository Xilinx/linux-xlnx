//SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Scene Change Detection driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Authors: Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>
 *          Satish Kumar Nagireddy <satish.nagireddy.nagireddy@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "xilinx-scenechange.h"

#define XSCD_RESET_DEASSERT	(0)
#define XSCD_RESET_ASSERT	(1)

static irqreturn_t xscd_irq_handler(int irq, void *data)
{
	struct xscd_device *xscd = (struct xscd_device *)data;
	u32 status;

	status = xscd_read(xscd->iomem, XSCD_ISR_OFFSET);
	if (!(status & XSCD_IE_AP_DONE))
		return IRQ_NONE;

	xscd_write(xscd->iomem, XSCD_ISR_OFFSET, XSCD_IE_AP_DONE);

	if (xscd->memory_based)
		xscd_dma_irq_handler(xscd);
	else
		xscd_chan_event_notify(&xscd->chans[0]);

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

	xscd->irq = platform_get_irq(pdev, 0);
	if (xscd->irq < 0) {
		dev_err(xscd->dev, "No valid irq found\n");
		return -EINVAL;
	}

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

	xscd->memory_based = of_property_read_bool(node, "xlnx,memorybased");
	xscd->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xscd->rst_gpio)) {
		if (PTR_ERR(xscd->rst_gpio) != -EPROBE_DEFER)
			dev_err(dev, "Reset GPIO not setup in DT\n");

		return PTR_ERR(xscd->rst_gpio);
	}

	ret = of_property_read_u32(node, "xlnx,numstreams",
				   &xscd->num_streams);
	if (ret < 0)
		return ret;

	if (!xscd->memory_based && xscd->num_streams != 1) {
		dev_err(dev, "Stream-based mode only supports one stream\n");
		return -EINVAL;
	}

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

	spin_lock_init(&xscd->lock);

	xscd->dev = &pdev->dev;
	platform_set_drvdata(pdev, xscd);

	ret = xscd_parse_of(xscd);
	if (ret < 0)
		return ret;

	ret = xscd_init_resources(xscd);
	if (ret < 0)
		return ret;

	/* Reset Scene Change Detection IP */
	gpiod_set_value_cansleep(xscd->rst_gpio, XSCD_RESET_ASSERT);
	gpiod_set_value_cansleep(xscd->rst_gpio, XSCD_RESET_DEASSERT);

	/* Initialize the channels. */
	xscd->chans = devm_kcalloc(xscd->dev, xscd->num_streams,
				   sizeof(*xscd->chans), GFP_KERNEL);
	if (!xscd->chans)
		return -ENOMEM;

	id = 0;
	for_each_child_of_node(xscd->dev->of_node, subdev_node) {
		if (id >= xscd->num_streams) {
			dev_warn(&pdev->dev,
				 "Too many channels, limiting to %u\n",
				 xscd->num_streams);
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

	/* Initialize the DMA engine. */
	ret = xscd_dma_init(xscd);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to initialize the DMA\n");

	ret = devm_request_irq(xscd->dev, xscd->irq, xscd_irq_handler,
			       IRQF_SHARED, dev_name(xscd->dev), xscd);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to request IRQ\n");

	dev_info(xscd->dev, "scene change detect device found!\n");
	return 0;
}

static int xscd_remove(struct platform_device *pdev)
{
	struct xscd_device *xscd = platform_get_drvdata(pdev);

	xscd_dma_cleanup(xscd);
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

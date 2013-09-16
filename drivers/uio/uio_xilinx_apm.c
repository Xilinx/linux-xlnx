/*
 * Xilinx AXI Performance Monitor
 *
 * Copyright (C) 2013 Xilinx, Inc. All rights reserved.
 *
 * Description:
 * This driver is developed for AXI Performance Monitor IP,
 * designed to monitor AXI4 traffic for performance analysis
 * of AXI bus in the system. Driver maps HW registers and parameters
 * to userspace. Userspace need not clear the interrupt of IP since
 * driver clears the interrupt.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>

#define XAPM_IS_OFFSET		0x0038  /* Interrupt Status Register */
#define DRV_NAME		"xilinxapm_uio"
#define DRV_VERSION		"1.0"
#define UIO_DUMMY_MEMSIZE	4096
#define XAPM_MODE_ADVANCED	1
#define XAPM_MODE_PROFILE	2
#define XAPM_MODE_TRACE		3

/**
 * struct xapm_param - HW parameters structure
 * @mode: Mode in which APM is working
 * @maxslots: Maximum number of Slots in APM
 * @eventcnt: Event counting enabled in APM
 * @eventlog: Event logging enabled in APM
 * @sampledcnt: Sampled metric counters enabled in APM
 * @numcounters: Number of counters in APM
 * @metricwidth: Metric Counter width (32/64)
 * @sampledwidth: Sampled metric counter width
 * @globalcntwidth: Global Clock counter width
 * @scalefactor: Scaling factor
 * @isr: Interrupts info shared to userspace
 */
struct xapm_param {
	u32 mode;
	u32 maxslots;
	u32 eventcnt;
	u32 eventlog;
	u32 sampledcnt;
	u32 numcounters;
	u32 metricwidth;
	u32 sampledwidth;
	u32 globalcntwidth;
	u32 scalefactor;
	u32 isr;
};

/**
 * struct xapm_dev - Global driver structure
 * @info: uio_info structure
 * @param: xapm_param structure
 * @regs: IOmapped base address
 */
struct xapm_dev {
	struct uio_info info;
	struct xapm_param param;
	void __iomem *regs;
};

/**
 * xapm_handler - Interrupt handler for APM
 * @irq: IRQ number
 * @info: Pointer to uio_info structure
 */
static irqreturn_t xapm_handler(int irq, struct uio_info *info)
{
	struct xapm_dev *xapm = (struct xapm_dev *)info->priv;
	void *ptr;

	ptr = (unsigned long *)xapm->info.mem[1].addr;
	/* Clear the interrupt and copy the ISR value to userspace */
	xapm->param.isr = readl(xapm->regs + XAPM_IS_OFFSET);
	writel(xapm->param.isr, xapm->regs + XAPM_IS_OFFSET);
	memcpy(ptr, &xapm->param, sizeof(struct xapm_param));

	return IRQ_HANDLED;
}

/**
 * xapm_getprop - Retrieves dts properties to param structure
 * @pdev: Pointer to platform device
 * @param: Pointer to param structure
 */
static int xapm_getprop(struct platform_device *pdev, struct xapm_param *param)
{
	u32 mode = 0;
	int ret;
	struct device_node *node;

	node = pdev->dev.of_node;

	/* Retrieve required dts properties and fill param structure */
	ret = of_property_read_u32(node, "xlnx,enable-profile", &mode);
	if (ret < 0)
		dev_info(&pdev->dev, "no property xlnx,enable-profile\n");
	else if (mode)
		param->mode = XAPM_MODE_PROFILE;

	ret = of_property_read_u32(node, "xlnx,enable-trace", &mode);
	if (ret < 0)
		dev_info(&pdev->dev, "no property xlnx,enable-trace\n");
	else if (mode)
		param->mode = XAPM_MODE_TRACE;

	ret = of_property_read_u32(node, "xlnx,num-monitor-slots",
				   &param->maxslots);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property xlnx,num-monitor-slots");
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,enable-event-count",
				   &param->eventcnt);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property xlnx,enable-event-count");
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,enable-event-log",
				   &param->eventlog);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property xlnx,enable-event-log");
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,have-sampled-metric-cnt",
				   &param->sampledcnt);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property xlnx,have-sampled-metric-cnt");
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,num-of-counters",
				   &param->numcounters);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property xlnx,num-of-counters");
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,metric-count-width",
				   &param->metricwidth);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property xlnx,metric-count-width");
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,metrics-sample-count-width",
				   &param->sampledwidth);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property metrics-sample-count-width");
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,global-count-width",
				   &param->globalcntwidth);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property xlnx,global-count-width");
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,metric-count-scale",
				   &param->scalefactor);
	if (ret < 0) {
		dev_err(&pdev->dev, "no property xlnx,metric-count-scale");
		return ret;
	}

	return 0;
}

/**
 * xapm_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Returns '0' on success and failure value on error
 */

static int xapm_probe(struct platform_device *pdev)
{
	struct xapm_dev *xapm;
	struct resource *res;
	int irq;
	int ret;
	void *ptr;

	xapm = devm_kzalloc(&pdev->dev, (sizeof(struct xapm_dev)), GFP_KERNEL);
	if (!xapm)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xapm->regs = devm_ioremap_resource(&pdev->dev, res);
	if (!xapm->regs) {
		dev_err(&pdev->dev, "unable to iomap registers\n");
		return -ENOMEM;
	}

	/* Initialize mode as Advanced so that if no mode in dts, default
	 * is Advanced
	 */
	xapm->param.mode = XAPM_MODE_ADVANCED;
	ret = xapm_getprop(pdev, &xapm->param);
	if (ret < 0)
		return ret;

	xapm->info.mem[0].name = "xilinx_apm";
	xapm->info.mem[0].addr = res->start;
	xapm->info.mem[0].size = resource_size(res);
	xapm->info.mem[0].memtype = UIO_MEM_PHYS;

	xapm->info.mem[1].addr = (unsigned long)kzalloc(UIO_DUMMY_MEMSIZE,
							GFP_KERNEL);
	ptr = (unsigned long *)xapm->info.mem[1].addr;
	xapm->info.mem[1].size = UIO_DUMMY_MEMSIZE;
	xapm->info.mem[1].memtype = UIO_MEM_LOGICAL;

	xapm->info.name = "axi-pmon";
	xapm->info.version = DRV_VERSION;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "unable to get irq\n");
		return irq;
	}

	xapm->info.irq = irq;
	xapm->info.handler = xapm_handler;
	xapm->info.priv = xapm;

	memcpy(ptr, &xapm->param, sizeof(struct xapm_param));

	ret = uio_register_device(&pdev->dev, &xapm->info);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register to UIO\n");
		return ret;
	}

	platform_set_drvdata(pdev, xapm);

	dev_info(&pdev->dev, "Probed Xilinx APM\n");

	return 0;
}

/**
 * xapm_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Always returns '0'
 */
static int xapm_remove(struct platform_device *pdev)
{
	struct xapm_dev *xapm = platform_get_drvdata(pdev);

	uio_unregister_device(&xapm->info);

	return 0;
}

static struct of_device_id xapm_of_match[] = {
	{ .compatible = "xlnx,axi-perf-monitor", },
	{ /* end of table*/ }
};

MODULE_DEVICE_TABLE(of, xapm_of_match);

static struct platform_driver xapm_driver = {
	.driver = {
		.name = "xilinx-axipmon",
		.owner = THIS_MODULE,
		.of_match_table = xapm_of_match,
	},
	.probe = xapm_probe,
	.remove = xapm_remove,
};

module_platform_driver(xapm_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx AXI Performance Monitor driver");
MODULE_LICENSE("GPL v2");

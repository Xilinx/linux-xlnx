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

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
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
 * @is_32bit_filter: Flags for 32bit filter
 * @clk: Clock handle
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
	bool is_32bit_filter;
	struct clk *clk;
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
 *
 * Return: Always returns IRQ_HANDLED
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
 *
 * Returns: '0' on success and failure value on error
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

	param->is_32bit_filter = of_property_read_bool(node,
						"xlnx,id-filter-32bit");

	return 0;
}

/**
 * xapm_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Returns: '0' on success and failure value on error
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
	if (IS_ERR(xapm->regs)) {
		dev_err(&pdev->dev, "unable to iomap registers\n");
		return PTR_ERR(xapm->regs);
	}

	xapm->param.clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(xapm->param.clk)) {
		dev_err(&pdev->dev, "axi clock error\n");
		return PTR_ERR(xapm->param.clk);
	}

	ret = clk_prepare_enable(xapm->param.clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		return ret;
	}
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	/* Initialize mode as Advanced so that if no mode in dts, default
	 * is Advanced
	 */
	xapm->param.mode = XAPM_MODE_ADVANCED;
	ret = xapm_getprop(pdev, &xapm->param);
	if (ret < 0)
		goto err_clk_dis;

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
		ret = irq;
		goto err_clk_dis;
	}

	xapm->info.irq = irq;
	xapm->info.handler = xapm_handler;
	xapm->info.priv = xapm;

	memcpy(ptr, &xapm->param, sizeof(struct xapm_param));

	ret = uio_register_device(&pdev->dev, &xapm->info);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register to UIO\n");
		goto err_clk_dis;
	}

	platform_set_drvdata(pdev, xapm);

	dev_info(&pdev->dev, "Probed Xilinx APM\n");

	return 0;

err_clk_dis:
	clk_disable_unprepare(xapm->param.clk);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	return ret;
}

/**
 * xapm_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: Always returns '0'
 */
static int xapm_remove(struct platform_device *pdev)
{
	struct xapm_dev *xapm = platform_get_drvdata(pdev);

	uio_unregister_device(&xapm->info);
	clk_disable_unprepare(xapm->param.clk);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	return 0;
}

static int __maybe_unused xapm_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xapm_dev *xapm = platform_get_drvdata(pdev);

	clk_disable_unprepare(xapm->param.clk);
	return 0;
};

static int __maybe_unused xapm_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xapm_dev *xapm = platform_get_drvdata(pdev);
	int ret;

	ret = clk_prepare_enable(xapm->param.clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		return ret;
	}
	return 0;
};

static const struct dev_pm_ops xapm_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xapm_runtime_suspend, xapm_runtime_resume)
	SET_RUNTIME_PM_OPS(xapm_runtime_suspend,
			   xapm_runtime_resume, NULL)
};

static const struct of_device_id xapm_of_match[] = {
	{ .compatible = "xlnx,axi-perf-monitor", },
	{ /* end of table*/ }
};

MODULE_DEVICE_TABLE(of, xapm_of_match);

static struct platform_driver xapm_driver = {
	.driver = {
		.name = "xilinx-axipmon",
		.of_match_table = xapm_of_match,
		.pm = &xapm_dev_pm_ops,
	},
	.probe = xapm_probe,
	.remove = xapm_remove,
};

module_platform_driver(xapm_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx AXI Performance Monitor driver");
MODULE_LICENSE("GPL v2");

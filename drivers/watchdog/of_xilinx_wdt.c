// SPDX-License-Identifier: GPL-2.0+
/*
 * Watchdog Device Driver for Xilinx axi/xps_timebase_wdt
 *
 * (C) Copyright 2013 - 2019 Xilinx, Inc.
 * (C) Copyright 2011 (Alejandro Cabrera <aldaya@gmail.com>)
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

#define XWT_WWDT_DEFAULT_TIMEOUT	10
#define XWT_WWDT_MIN_TIMEOUT		1
#define XWT_WWDT_MAX_TIMEOUT		80

/* Register offsets for the Wdt device */
#define XWT_TWCSR0_OFFSET   0x0 /* Control/Status Register0 */
#define XWT_TWCSR1_OFFSET   0x4 /* Control/Status Register1 */
#define XWT_TBR_OFFSET      0x8 /* Timebase Register Offset */
#define XWT_WWREF_OFFSET	0x1000 /* Refresh Register */
#define XWT_WWCSR_OFFSET	0x2000 /* Control/Status Register */
#define XWT_WWOFF_OFFSET	0x2008 /* Offset Register */
#define XWT_WWCMP0_OFFSET	0x2010 /* Compare Value Register0 */
#define XWT_WWCMP1_OFFSET	0x2014 /* Compare Value Register1 */
#define XWT_WWWRST_OFFSET	0x2FD0 /* Warm Reset Register */

/* Control/Status Register Masks  */
#define XWT_CSR0_WRS_MASK	BIT(3) /* Reset status */
#define XWT_CSR0_WDS_MASK	BIT(2) /* Timer state  */
#define XWT_CSR0_EWDT1_MASK	BIT(1) /* Enable bit 1 */

/* Control/Status Register 0/1 bits  */
#define XWT_CSRX_EWDT2_MASK	BIT(0) /* Enable bit 2 */

/* Refresh Register Masks */
#define XWT_WWREF_GWRR_MASK	BIT(0) /* Refresh and start new period */

/* Generic Control/Status Register Masks  */
#define XWT_WWCSR_GWEN_MASK	BIT(0) /* Enable Bit */

/* Warm Reset Register Masks */
#define XWT_WWRST_GWWRR_MASK	BIT(0) /* Warm Reset Register */

/* SelfTest constants */
#define XWT_MAX_SELFTEST_LOOP_COUNT 0x00010000
#define XWT_TIMER_FAILED            0xFFFFFFFF

#define WATCHDOG_NAME     "Xilinx Watchdog"

static int wdt_timeout;

module_param(wdt_timeout, int, 0644);
MODULE_PARM_DESC(wdt_timeout,
		 "Watchdog time in seconds. (default="
		 __MODULE_STRING(XWDT_WWDT_DEFAULT_TIMEOUT) ")");

/**
 * enum xwdt_ip_type - WDT IP type.
 *
 * @XWDT_WDT: Soft wdt ip.
 * @XWDT_WWDT: Window wdt ip.
 */
enum xwdt_ip_type {
	XWDT_WDT = 0,
	XWDT_WWDT,
};

struct xwdt_devtype_data {
	enum xwdt_ip_type wdttype;
	const struct watchdog_ops *xwdt_ops;
	const struct watchdog_info *xwdt_info;
};

struct xwdt_device {
	void __iomem *base;
	u32 wdt_interval;
	spinlock_t spinlock; /* spinlock for register handling */
	struct watchdog_device xilinx_wdt_wdd;
	struct clk		*clk;
};

static int xilinx_wdt_start(struct watchdog_device *wdd)
{
	int ret;
	u32 control_status_reg;
	struct xwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wdt_wdd = &xdev->xilinx_wdt_wdd;

	ret = clk_enable(xdev->clk);
	if (ret) {
		dev_err(wdd->parent, "Failed to enable clock\n");
		return ret;
	}

	spin_lock(&xdev->spinlock);

	/* Clean previous status and enable the watchdog timer */
	control_status_reg = ioread32(xdev->base + XWT_TWCSR0_OFFSET);
	control_status_reg |= (XWT_CSR0_WRS_MASK | XWT_CSR0_WDS_MASK);

	iowrite32((control_status_reg | XWT_CSR0_EWDT1_MASK),
		  xdev->base + XWT_TWCSR0_OFFSET);

	iowrite32(XWT_CSRX_EWDT2_MASK, xdev->base + XWT_TWCSR1_OFFSET);

	spin_unlock(&xdev->spinlock);

	dev_dbg(xilinx_wdt_wdd->parent, "Watchdog Started!\n");

	return 0;
}

static int xilinx_wdt_stop(struct watchdog_device *wdd)
{
	u32 control_status_reg;
	struct xwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wdt_wdd = &xdev->xilinx_wdt_wdd;

	spin_lock(&xdev->spinlock);

	control_status_reg = ioread32(xdev->base + XWT_TWCSR0_OFFSET);

	iowrite32((control_status_reg & ~XWT_CSR0_EWDT1_MASK),
		  xdev->base + XWT_TWCSR0_OFFSET);

	iowrite32(0, xdev->base + XWT_TWCSR1_OFFSET);

	spin_unlock(&xdev->spinlock);

	clk_disable(xdev->clk);

	dev_dbg(xilinx_wdt_wdd->parent, "Watchdog Stopped!\n");

	return 0;
}

static int xilinx_wdt_keepalive(struct watchdog_device *wdd)
{
	u32 control_status_reg;
	struct xwdt_device *xdev = watchdog_get_drvdata(wdd);

	spin_lock(&xdev->spinlock);

	control_status_reg = ioread32(xdev->base + XWT_TWCSR0_OFFSET);
	control_status_reg |= (XWT_CSR0_WRS_MASK | XWT_CSR0_WDS_MASK);
	iowrite32(control_status_reg, xdev->base + XWT_TWCSR0_OFFSET);

	spin_unlock(&xdev->spinlock);

	return 0;
}

static const struct watchdog_info xilinx_wdt_ident = {
	.options =  WDIOF_MAGICCLOSE |
		    WDIOF_KEEPALIVEPING,
	.firmware_version =	1,
	.identity =	WATCHDOG_NAME,
};

static const struct watchdog_ops xilinx_wdt_ops = {
	.owner = THIS_MODULE,
	.start = xilinx_wdt_start,
	.stop = xilinx_wdt_stop,
	.ping = xilinx_wdt_keepalive,
};

static int xilinx_wwdt_start(struct watchdog_device *wdd)
{
	int ret;
	u32 control_status_reg;
	u64 count;
	struct xwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wdt_wdd = &xdev->xilinx_wdt_wdd;

	unsigned long clock_f = clk_get_rate(xdev->clk);

	/* Calculate timeout count */
	count = wdd->timeout * clock_f;
	ret  = clk_enable(xdev->clk);
	if (ret) {
		dev_err(wdd->parent, "Failed to enable clock\n");
		return ret;
	}

	spin_lock(&xdev->spinlock);

	/*
	 * Timeout count is half as there are two windows
	 * first window overflow is ignored (interrupt),
	 * reset is only generated at second window overflow
	 */
	count = count >> 1;

	/* Disable the generic watchdog timer */
	control_status_reg = ioread32(xdev->base + XWT_WWCSR_OFFSET);
	control_status_reg &= ~(XWT_WWCSR_GWEN_MASK);
	iowrite32(control_status_reg, xdev->base + XWT_WWCSR_OFFSET);

	/* Set compare and offset registers for generic watchdog timeout */
	iowrite32((u32)count, xdev->base + XWT_WWCMP0_OFFSET);
	iowrite32((u32)0, xdev->base + XWT_WWCMP1_OFFSET);
	iowrite32((u32)count, xdev->base + XWT_WWOFF_OFFSET);

	/* Enable the generic watchdog timer */
	control_status_reg = ioread32(xdev->base + XWT_WWCSR_OFFSET);
	control_status_reg |= (XWT_WWCSR_GWEN_MASK);
	iowrite32(control_status_reg, xdev->base + XWT_WWCSR_OFFSET);

	spin_unlock(&xdev->spinlock);

	dev_dbg(xilinx_wdt_wdd->parent, "Watchdog Started!\n");

	return 0;
}

static int xilinx_wwdt_stop(struct watchdog_device *wdd)
{
	u32 control_status_reg;
	struct xwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wdt_wdd = &xdev->xilinx_wdt_wdd;

	spin_lock(&xdev->spinlock);

	/* Disable the generic watchdog timer */
	control_status_reg = ioread32(xdev->base + XWT_WWCSR_OFFSET);
	control_status_reg &= ~(XWT_WWCSR_GWEN_MASK);
	iowrite32(control_status_reg, xdev->base + XWT_WWCSR_OFFSET);

	spin_unlock(&xdev->spinlock);

	clk_disable(xdev->clk);

	dev_dbg(xilinx_wdt_wdd->parent, "Watchdog Stopped!\n");

	return 0;
}

static int xilinx_wwdt_keepalive(struct watchdog_device *wdd)
{
	struct xwdt_device *xdev = watchdog_get_drvdata(wdd);

	spin_lock(&xdev->spinlock);

	iowrite32(XWT_WWREF_GWRR_MASK, xdev->base + XWT_WWREF_OFFSET);

	spin_unlock(&xdev->spinlock);

	return 0;
}

static int xilinx_wwdt_set_timeout(struct watchdog_device *wdd,
				   unsigned int new_time)
{
	struct xwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wdt_wdd = &xdev->xilinx_wdt_wdd;

	if (new_time < XWT_WWDT_MIN_TIMEOUT ||
	    new_time > XWT_WWDT_MAX_TIMEOUT) {
		dev_warn(xilinx_wdt_wdd->parent,
			 "timeout value must be %d<=x<=%d, using %d\n",
				XWT_WWDT_MIN_TIMEOUT,
				XWT_WWDT_MAX_TIMEOUT, new_time);
		return -EINVAL;
	}

	wdd->timeout = new_time;

	return xilinx_wwdt_start(wdd);
}

static const struct watchdog_info xilinx_wwdt_ident = {
	.options =  WDIOF_MAGICCLOSE |
		WDIOF_KEEPALIVEPING |
		WDIOF_SETTIMEOUT,
	.firmware_version =	1,
	.identity = "xlnx_wwdt watchdog",
};

static const struct watchdog_ops xilinx_wwdt_ops = {
	.owner = THIS_MODULE,
	.start = xilinx_wwdt_start,
	.stop = xilinx_wwdt_stop,
	.ping = xilinx_wwdt_keepalive,
	.set_timeout = xilinx_wwdt_set_timeout,
};

static u32 xwdt_selftest(struct xwdt_device *xdev)
{
	int i;
	u32 timer_value1;
	u32 timer_value2;

	spin_lock(&xdev->spinlock);

	timer_value1 = ioread32(xdev->base + XWT_TBR_OFFSET);
	timer_value2 = ioread32(xdev->base + XWT_TBR_OFFSET);

	for (i = 0;
		((i <= XWT_MAX_SELFTEST_LOOP_COUNT) &&
			(timer_value2 == timer_value1)); i++) {
		timer_value2 = ioread32(xdev->base + XWT_TBR_OFFSET);
	}

	spin_unlock(&xdev->spinlock);

	if (timer_value2 != timer_value1)
		return ~XWT_TIMER_FAILED;
	else
		return XWT_TIMER_FAILED;
}

static const struct xwdt_devtype_data xwdt_wdt_data = {
	.wdttype = XWDT_WDT,
	.xwdt_info = &xilinx_wdt_ident,
	.xwdt_ops = &xilinx_wdt_ops,
};

static const struct xwdt_devtype_data xwdt_wwdt_data = {
	.wdttype = XWDT_WWDT,
	.xwdt_info = &xilinx_wwdt_ident,
	.xwdt_ops = &xilinx_wwdt_ops,
};

static const struct of_device_id xwdt_of_match[] = {
	{ .compatible = "xlnx,xps-timebase-wdt-1.00.a",
		.data = &xwdt_wdt_data },
	{ .compatible = "xlnx,xps-timebase-wdt-1.01.a",
		.data = &xwdt_wdt_data },
	{ .compatible = "xlnx,versal-wwdt-1.0",
		.data = &xwdt_wwdt_data },
	{},
};
MODULE_DEVICE_TABLE(of, xwdt_of_match);

static int xwdt_probe(struct platform_device *pdev)
{
	int rc;
	u32 pfreq = 0, enable_once = 0;
	struct resource *res;
	struct xwdt_device *xdev;
	struct watchdog_device *xilinx_wdt_wdd;
	const struct of_device_id *of_id;
	enum xwdt_ip_type wdttype;
	const struct xwdt_devtype_data *devtype;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xilinx_wdt_wdd = &xdev->xilinx_wdt_wdd;

	of_id = of_match_device(xwdt_of_match, &pdev->dev);
	if (!of_id)
		return -EINVAL;

	devtype = of_id->data;

	wdttype = devtype->wdttype;

	xilinx_wdt_wdd->info = devtype->xwdt_info;
	xilinx_wdt_wdd->ops = devtype->xwdt_ops;
	xilinx_wdt_wdd->parent = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xdev->base))
		return PTR_ERR(xdev->base);

	if (wdttype == XWDT_WDT) {
		rc = of_property_read_u32(pdev->dev.of_node,
					  "xlnx,wdt-interval",
					  &xdev->wdt_interval);
		if (rc)
			dev_warn(&pdev->dev,
				 "Parameter \"xlnx,wdt-interval\" not found\n");

		rc = of_property_read_u32(pdev->dev.of_node,
					  "xlnx,wdt-enable-once",
					  &enable_once);
		if (rc)
			dev_warn(&pdev->dev,
				 "Parameter \"xlnx,wdt-enable-once\" not found\n");

		watchdog_set_nowayout(xilinx_wdt_wdd, enable_once);
	}

	xdev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(xdev->clk)) {
		if (PTR_ERR(xdev->clk) != -ENOENT)
			return PTR_ERR(xdev->clk);

		/*
		 * Clock framework support is optional, continue on
		 * anyways if we don't find a matching clock.
		 */
		xdev->clk = NULL;

		rc = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
					  &pfreq);
		if (rc)
			dev_warn(&pdev->dev,
				 "The watchdog clock freq cannot be obtained\n");
	} else {
		pfreq = clk_get_rate(xdev->clk);
	}

	if (wdttype == XWDT_WDT) {
		/*
		 * Twice of the 2^wdt_interval / freq  because
		 * the first wdt overflow is ignored (interrupt),
		 * reset is only generated at second wdt overflow
		 */
		if (pfreq && xdev->wdt_interval)
			xilinx_wdt_wdd->timeout =
				2 * ((1 << xdev->wdt_interval) /
					pfreq);
	} else {
		xilinx_wdt_wdd->timeout = XWT_WWDT_DEFAULT_TIMEOUT;
		xilinx_wdt_wdd->min_timeout = XWT_WWDT_MIN_TIMEOUT;
		xilinx_wdt_wdd->max_timeout = XWT_WWDT_MAX_TIMEOUT;

		rc = watchdog_init_timeout(xilinx_wdt_wdd,
					   wdt_timeout, &pdev->dev);
		if (rc) {
			dev_err(&pdev->dev, "unable to set timeout value\n");
			return rc;
		}
	}

	spin_lock_init(&xdev->spinlock);
	watchdog_set_drvdata(xilinx_wdt_wdd, xdev);

	rc = clk_prepare_enable(xdev->clk);
	if (rc) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		return rc;
	}

	if (wdttype == XWDT_WDT) {
		rc = xwdt_selftest(xdev);
		if (rc == XWT_TIMER_FAILED) {
			dev_err(&pdev->dev, "SelfTest routine error\n");
			goto err_clk_disable;
		}
	}

	rc = watchdog_register_device(xilinx_wdt_wdd);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register watchdog (err=%d)\n", rc);
		goto err_clk_disable;
	}

	clk_disable(xdev->clk);

	dev_info(&pdev->dev, "Xilinx Watchdog Timer at %p with timeout %ds\n",
		 xdev->base, xilinx_wdt_wdd->timeout);

	platform_set_drvdata(pdev, xdev);

	return 0;
err_clk_disable:
	clk_disable_unprepare(xdev->clk);

	return rc;
}

static int xwdt_remove(struct platform_device *pdev)
{
	struct xwdt_device *xdev = platform_get_drvdata(pdev);

	watchdog_unregister_device(&xdev->xilinx_wdt_wdd);
	clk_disable_unprepare(xdev->clk);

	return 0;
}

/**
 * xwdt_suspend - Suspend the device.
 *
 * @dev: handle to the device structure.
 * Return: 0 always.
 */
static int __maybe_unused xwdt_suspend(struct device *dev)
{
	struct xwdt_device *xdev = dev_get_drvdata(dev);
	struct watchdog_device *xilinx_wdt_wdd = &xdev->xilinx_wdt_wdd;

	if (watchdog_active(xilinx_wdt_wdd))
		xilinx_wdt_wdd->ops->stop(xilinx_wdt_wdd);

	return 0;
}

/**
 * xwdt_resume - Resume the device.
 *
 * @dev: handle to the device structure.
 * Return: 0 on success, errno otherwise.
 */
static int __maybe_unused xwdt_resume(struct device *dev)
{
	struct xwdt_device *xdev = dev_get_drvdata(dev);
	struct watchdog_device *xilinx_wdt_wdd = &xdev->xilinx_wdt_wdd;
	int ret = 0;

	if (watchdog_active(xilinx_wdt_wdd))
		ret = xilinx_wdt_wdd->ops->start(xilinx_wdt_wdd);

	return ret;
}

static SIMPLE_DEV_PM_OPS(xwdt_pm_ops, xwdt_suspend, xwdt_resume);

static struct platform_driver xwdt_driver = {
	.probe       = xwdt_probe,
	.remove      = xwdt_remove,
	.driver = {
		.name  = WATCHDOG_NAME,
		.of_match_table = xwdt_of_match,
		.pm = &xwdt_pm_ops,
	},
};

module_platform_driver(xwdt_driver);

MODULE_AUTHOR("Alejandro Cabrera <aldaya@gmail.com>");
MODULE_DESCRIPTION("Xilinx Watchdog driver");
MODULE_LICENSE("GPL");

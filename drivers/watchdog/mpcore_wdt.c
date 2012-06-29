/*
 *	Watchdog driver for the mpcore watchdog timer
 *
 *	(c) Copyright 2004 ARM Limited
 *
 *	Based on the SoftDog driver:
 *	(c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/smp_twd.h>

#define MPCORE_DEFAULT_TIMEOUT	60
#define MPCORE_MIN_TIMEOUT		0x0001
#define MPCORE_MAX_TIMEOUT		0xFFFF

struct mpcore_wdt {
	void __iomem	*base;
	int		irq;
	unsigned int	clk;
};

static DEFINE_SPINLOCK(wdt_lock);

static int mpcore_margin = MPCORE_DEFAULT_TIMEOUT;
module_param(mpcore_margin, int, 0);
MODULE_PARM_DESC(mpcore_margin,
	"MPcore timer margin in seconds. (0 < mpcore_margin < 65536, default="
				__MODULE_STRING(MPCORE_DEFAULT_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define ONLY_TESTING	0
static int mpcore_noboot = ONLY_TESTING;
module_param(mpcore_noboot, int, 0);
MODULE_PARM_DESC(mpcore_noboot, "MPcore watchdog action, "
	"set to 1 to ignore reboots, 0 to reboot (default="
					__MODULE_STRING(ONLY_TESTING) ")");

/*
 *	This is the interrupt handler.  Note that we only use this
 *	in testing mode, so don't actually do a reboot here.
 */
static irqreturn_t mpcore_wdt_fire(int irq, void *arg)
{
	struct watchdog_device *wdd = arg;
	struct mpcore_wdt *wdt = watchdog_get_drvdata(wdd);

	/* Check it really was our interrupt */
	if (readl(wdt->base + TWD_WDOG_INTSTAT)) {
		dev_printk(KERN_CRIT, wdd->dev,
					"Triggered - Reboot ignored.\n");
		/* Clear the interrupt on the watchdog */
		writel(1, wdt->base + TWD_WDOG_INTSTAT);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

/*
 *	mpcore_wdt_keepalive - reload the timer
 *
 *	Using 64 bit math to prevent overflow prior to prescalar
 *  for high clock values.
 */
static int mpcore_wdt_keepalive(struct watchdog_device *wdd)
{
	struct mpcore_wdt *wdt = watchdog_get_drvdata(wdd);
	u64 dividend = (u64) wdd->timeout * wdt->clk;
	unsigned long wdt_count = (u32) div64_u64(dividend, 256 + 1) - 1;

	spin_lock(&wdt_lock);
	writel(wdt_count, wdt->base + TWD_WDOG_LOAD);
	spin_unlock(&wdt_lock);
	return 0;
}

static int mpcore_wdt_stop(struct watchdog_device *wdd)
{
	struct mpcore_wdt *wdt = watchdog_get_drvdata(wdd);
	spin_lock(&wdt_lock);
	writel(0x12345678, wdt->base + TWD_WDOG_DISABLE);
	writel(0x87654321, wdt->base + TWD_WDOG_DISABLE);
	writel(0x0, wdt->base + TWD_WDOG_CONTROL);
	spin_unlock(&wdt_lock);
	return 0;
}

static int mpcore_wdt_start(struct watchdog_device *wdd)
{
	struct mpcore_wdt *wdt = watchdog_get_drvdata(wdd);
	dev_printk(KERN_INFO, wdd->dev, "enabling watchdog.\n");

	/* This loads the count register but does NOT start the count yet */
	mpcore_wdt_keepalive(wdd);

	if (mpcore_noboot) {
		/* Enable watchdog - prescale=256, watchdog mode=0, enable=1 */
		writel(0x0000FF01, wdt->base + TWD_WDOG_CONTROL);
	} else {
		/* Enable watchdog - prescale=256, watchdog mode=1, enable=1 */
		writel(0x0000FF09, wdt->base + TWD_WDOG_CONTROL);
	}
	return 0;
}

static int mpcore_wdt_set_heartbeat(struct watchdog_device *wdd, unsigned int t)
{
	wdd->timeout = t;
	return 0;
}

static const struct watchdog_info ident = {
	.options		= WDIOF_SETTIMEOUT |
				  WDIOF_KEEPALIVEPING |
				  WDIOF_MAGICCLOSE,
	.identity		= "MPcore Watchdog",
};

/* Watchdog Core Ops */
static struct watchdog_ops mpcore_wdt_ops = {
	.owner = THIS_MODULE,
	.start = mpcore_wdt_start,
	.stop = mpcore_wdt_stop,
	.ping = mpcore_wdt_keepalive,
	.set_timeout = mpcore_wdt_set_heartbeat,
};

/* Watchdog Core Device */
static struct watchdog_device mpcore_dev = {
	.info = &ident,
	.ops = &mpcore_wdt_ops,
	.timeout = MPCORE_DEFAULT_TIMEOUT,
	.min_timeout = MPCORE_MIN_TIMEOUT,
	.max_timeout = MPCORE_MAX_TIMEOUT,
};

/*
 *	System shutdown handler.  Turn off the watchdog if we're
 *	restarting or halting the system.
 */
static void mpcore_wdt_shutdown(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);

	if (system_state == SYSTEM_RESTART || system_state == SYSTEM_HALT)
		mpcore_wdt_stop(wdd);
}

/*
 *	Kernel Interfaces
 */
static int __devinit mpcore_wdt_probe(struct platform_device *pdev)
{
	struct mpcore_wdt *wdt;
	struct resource *res;
	int ret;
#ifdef CONFIG_OF
	const void *prop;
#endif

	/* We only accept one device, and it must have an id of -1 */
	if (pdev->id != -1)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	wdt = devm_kzalloc(&pdev->dev, sizeof(struct mpcore_wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->irq = platform_get_irq(pdev, 0);
	if (wdt->irq >= 0) {
		ret = devm_request_irq(&pdev->dev, wdt->irq, mpcore_wdt_fire, 0,
				"mpcore_wdt", wdt);
		if (ret) {
			dev_printk(KERN_ERR, &pdev->dev,
					"cannot register IRQ%d for watchdog\n",
					wdt->irq);
			return ret;
		}
	}

#ifdef CONFIG_OF
	/* Subtract 0x20 from the register starting address to allow
	 * device trees to specify the WDT start address, not the local
	 * timer start address. This does not break previous uses of
	 * platform_data. */
	res->start -= 0x20;

	/* Get clock speed from device tree */
	prop = of_get_property(pdev->dev.of_node, "clock-frequency", NULL);
	wdt->clk = prop ? (u32)be32_to_cpup(prop) : HZ;
#else
	wdt->clk = HZ;
#endif

	wdt->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!wdt->base)
		return -ENOMEM;

	mpcore_dev.parent = &pdev->dev;
	ret = watchdog_register_device(&mpcore_dev);
	if (ret) {
		dev_printk(KERN_ERR, &pdev->dev,
			"cannot register watchdog device (err=%d)\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, &mpcore_dev);
	watchdog_set_drvdata(&mpcore_dev, wdt);
	mpcore_wdt_stop(&mpcore_dev);

	pr_info("MPcore Watchdog Timer: 0.1. mpcore_noboot=%d mpcore_margin=%d sec (nowayout= %d)\n",
		mpcore_noboot, mpcore_dev.timeout, nowayout);
	return 0;
}

static int __devexit mpcore_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, NULL);
	watchdog_unregister_device(wdd);
	return 0;
}

#ifdef CONFIG_PM
static int mpcore_wdt_suspend(struct platform_device *pdev, pm_message_t msg)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	mpcore_wdt_stop(wdd);		/* Turn the WDT off */
	return 0;
}

static int mpcore_wdt_resume(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	/* re-activate timer */
	if (test_bit(WDOG_ACTIVE, &wdd->status))
		mpcore_wdt_start(wdd);
	return 0;
}
#else
#define mpcore_wdt_suspend	NULL
#define mpcore_wdt_resume	NULL
#endif

#ifdef CONFIG_OF
static struct of_device_id mpcore_wdt_of_match[] __devinitdata = {
	{ .compatible = "arm,mpcore_wdt", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, mpcore_wdt_of_match);
#endif

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:mpcore_wdt");

static struct platform_driver mpcore_wdt_driver = {
	.probe		= mpcore_wdt_probe,
	.remove		= __devexit_p(mpcore_wdt_remove),
	.suspend	= mpcore_wdt_suspend,
	.resume		= mpcore_wdt_resume,
	.shutdown	= mpcore_wdt_shutdown,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "mpcore_wdt",
#ifdef CONFIG_OF
		.of_match_table = mpcore_wdt_of_match,
#endif
	},
};

static int __init mpcore_wdt_init(void)
{
	return platform_driver_register(&mpcore_wdt_driver);
}

static void __exit mpcore_wdt_exit(void)
{
	platform_driver_unregister(&mpcore_wdt_driver);
}

module_init(mpcore_wdt_init);
module_exit(mpcore_wdt_exit);

MODULE_AUTHOR("ARM Limited");
MODULE_DESCRIPTION("MPcore Watchdog Device Driver");
MODULE_LICENSE("GPL");

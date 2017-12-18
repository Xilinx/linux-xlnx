/*
 * Driver for Atmel SAMA5D4 Watchdog Timer
 *
 * Copyright (C) 2015 Atmel Corporation
 *
 * Licensed under GPLv2.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>

#include "at91sam9_wdt.h"

/* minimum and maximum watchdog timeout, in seconds */
#define MIN_WDT_TIMEOUT		1
#define MAX_WDT_TIMEOUT		16
#define WDT_DEFAULT_TIMEOUT	MAX_WDT_TIMEOUT

#define WDT_SEC2TICKS(s)	((s) ? (((s) << 8) - 1) : 0)

struct sama5d4_wdt {
	struct watchdog_device	wdd;
	void __iomem		*reg_base;
	u32	config;
};

static int wdt_timeout = WDT_DEFAULT_TIMEOUT;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_timeout, int, 0);
MODULE_PARM_DESC(wdt_timeout,
	"Watchdog timeout in seconds. (default = "
	__MODULE_STRING(WDT_DEFAULT_TIMEOUT) ")");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define wdt_read(wdt, field) \
	readl_relaxed((wdt)->reg_base + (field))

#define wdt_write(wtd, field, val) \
	writel_relaxed((val), (wdt)->reg_base + (field))

static int sama5d4_wdt_start(struct watchdog_device *wdd)
{
	struct sama5d4_wdt *wdt = watchdog_get_drvdata(wdd);
	u32 reg;

	reg = wdt_read(wdt, AT91_WDT_MR);
	reg &= ~AT91_WDT_WDDIS;
	wdt_write(wdt, AT91_WDT_MR, reg);

	return 0;
}

static int sama5d4_wdt_stop(struct watchdog_device *wdd)
{
	struct sama5d4_wdt *wdt = watchdog_get_drvdata(wdd);
	u32 reg;

	reg = wdt_read(wdt, AT91_WDT_MR);
	reg |= AT91_WDT_WDDIS;
	wdt_write(wdt, AT91_WDT_MR, reg);

	return 0;
}

static int sama5d4_wdt_ping(struct watchdog_device *wdd)
{
	struct sama5d4_wdt *wdt = watchdog_get_drvdata(wdd);

	wdt_write(wdt, AT91_WDT_CR, AT91_WDT_KEY | AT91_WDT_WDRSTT);

	return 0;
}

static int sama5d4_wdt_set_timeout(struct watchdog_device *wdd,
				 unsigned int timeout)
{
	struct sama5d4_wdt *wdt = watchdog_get_drvdata(wdd);
	u32 value = WDT_SEC2TICKS(timeout);
	u32 reg;

	reg = wdt_read(wdt, AT91_WDT_MR);
	reg &= ~AT91_WDT_WDV;
	reg &= ~AT91_WDT_WDD;
	reg |= AT91_WDT_SET_WDV(value);
	reg |= AT91_WDT_SET_WDD(value);
	wdt_write(wdt, AT91_WDT_MR, reg);

	wdd->timeout = timeout;

	return 0;
}

static const struct watchdog_info sama5d4_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "Atmel SAMA5D4 Watchdog",
};

static struct watchdog_ops sama5d4_wdt_ops = {
	.owner = THIS_MODULE,
	.start = sama5d4_wdt_start,
	.stop = sama5d4_wdt_stop,
	.ping = sama5d4_wdt_ping,
	.set_timeout = sama5d4_wdt_set_timeout,
};

static irqreturn_t sama5d4_wdt_irq_handler(int irq, void *dev_id)
{
	struct sama5d4_wdt *wdt = platform_get_drvdata(dev_id);

	if (wdt_read(wdt, AT91_WDT_SR)) {
		pr_crit("Atmel Watchdog Software Reset\n");
		emergency_restart();
		pr_crit("Reboot didn't succeed\n");
	}

	return IRQ_HANDLED;
}

static int of_sama5d4_wdt_init(struct device_node *np, struct sama5d4_wdt *wdt)
{
	const char *tmp;

	wdt->config = AT91_WDT_WDDIS;

	if (!of_property_read_string(np, "atmel,watchdog-type", &tmp) &&
	    !strcmp(tmp, "software"))
		wdt->config |= AT91_WDT_WDFIEN;
	else
		wdt->config |= AT91_WDT_WDRSTEN;

	if (of_property_read_bool(np, "atmel,idle-halt"))
		wdt->config |= AT91_WDT_WDIDLEHLT;

	if (of_property_read_bool(np, "atmel,dbg-halt"))
		wdt->config |= AT91_WDT_WDDBGHLT;

	return 0;
}

static int sama5d4_wdt_init(struct sama5d4_wdt *wdt)
{
	struct watchdog_device *wdd = &wdt->wdd;
	u32 value = WDT_SEC2TICKS(wdd->timeout);
	u32 reg;

	/*
	 * Because the fields WDV and WDD must not be modified when the WDDIS
	 * bit is set, so clear the WDDIS bit before writing the WDT_MR.
	 */
	reg = wdt_read(wdt, AT91_WDT_MR);
	reg &= ~AT91_WDT_WDDIS;
	wdt_write(wdt, AT91_WDT_MR, reg);

	reg = wdt->config;
	reg |= AT91_WDT_SET_WDD(value);
	reg |= AT91_WDT_SET_WDV(value);

	wdt_write(wdt, AT91_WDT_MR, reg);

	return 0;
}

static int sama5d4_wdt_probe(struct platform_device *pdev)
{
	struct watchdog_device *wdd;
	struct sama5d4_wdt *wdt;
	struct resource *res;
	void __iomem *regs;
	u32 irq = 0;
	int ret;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdd = &wdt->wdd;
	wdd->timeout = wdt_timeout;
	wdd->info = &sama5d4_wdt_info;
	wdd->ops = &sama5d4_wdt_ops;
	wdd->min_timeout = MIN_WDT_TIMEOUT;
	wdd->max_timeout = MAX_WDT_TIMEOUT;

	watchdog_set_drvdata(wdd, wdt);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	wdt->reg_base = regs;

	if (pdev->dev.of_node) {
		irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
		if (!irq)
			dev_warn(&pdev->dev, "failed to get IRQ from DT\n");

		ret = of_sama5d4_wdt_init(pdev->dev.of_node, wdt);
		if (ret)
			return ret;
	}

	if ((wdt->config & AT91_WDT_WDFIEN) && irq) {
		ret = devm_request_irq(&pdev->dev, irq, sama5d4_wdt_irq_handler,
				       IRQF_SHARED | IRQF_IRQPOLL |
				       IRQF_NO_SUSPEND, pdev->name, pdev);
		if (ret) {
			dev_err(&pdev->dev,
				"cannot register interrupt handler\n");
			return ret;
		}
	}

	ret = watchdog_init_timeout(wdd, wdt_timeout, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "unable to set timeout value\n");
		return ret;
	}

	ret = sama5d4_wdt_init(wdt);
	if (ret)
		return ret;

	watchdog_set_nowayout(wdd, nowayout);

	ret = watchdog_register_device(wdd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register watchdog device\n");
		return ret;
	}

	platform_set_drvdata(pdev, wdt);

	dev_info(&pdev->dev, "initialized (timeout = %d sec, nowayout = %d)\n",
		 wdt_timeout, nowayout);

	return 0;
}

static int sama5d4_wdt_remove(struct platform_device *pdev)
{
	struct sama5d4_wdt *wdt = platform_get_drvdata(pdev);

	sama5d4_wdt_stop(&wdt->wdd);

	watchdog_unregister_device(&wdt->wdd);

	return 0;
}

static const struct of_device_id sama5d4_wdt_of_match[] = {
	{ .compatible = "atmel,sama5d4-wdt", },
	{ }
};
MODULE_DEVICE_TABLE(of, sama5d4_wdt_of_match);

static struct platform_driver sama5d4_wdt_driver = {
	.probe		= sama5d4_wdt_probe,
	.remove		= sama5d4_wdt_remove,
	.driver		= {
		.name	= "sama5d4_wdt",
		.of_match_table = sama5d4_wdt_of_match,
	}
};
module_platform_driver(sama5d4_wdt_driver);

MODULE_AUTHOR("Atmel Corporation");
MODULE_DESCRIPTION("Atmel SAMA5D4 Watchdog Timer driver");
MODULE_LICENSE("GPL v2");

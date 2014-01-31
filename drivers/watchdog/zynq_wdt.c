/*
 * Xilinx Zynq WDT driver
 *
 * Copyright (C) 2010 - 2014 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>

#define ZYNQ_WDT_DEFAULT_TIMEOUT	10
/* Supports 1 - 516 sec */
#define ZYNQ_WDT_MIN_TIMEOUT	1
#define ZYNQ_WDT_MAX_TIMEOUT	516

static int wdt_timeout = ZYNQ_WDT_DEFAULT_TIMEOUT;
static int nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_timeout, int, 0);
MODULE_PARM_DESC(wdt_timeout,
		 "Watchdog time in seconds. (default="
		 __MODULE_STRING(ZYNQ_WDT_DEFAULT_TIMEOUT) ")");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/**
 * struct zynq_wdt - Watchdog device structure.
 * @regs: baseaddress of device.
 * @rst: reset flag
 * @clk: struct clk * of a clock source
 * @prescaler: for saving prescaler value
 * @ctrl_clksel: counter clock prescaler selection
 * @io_lock: spinlock for IO register access
 *
 * Structure containing parameters specific to ps watchdog.
 */
struct zynq_wdt {
	void __iomem		*regs;
	u32			rst;
	struct clk		*clk;
	u32			prescaler;
	u32			ctrl_clksel;
	spinlock_t		io_lock;
};
static struct zynq_wdt *wdt;

/*
 * Info structure used to indicate the features supported by the device
 * to the upper layers. This is defined in watchdog.h header file.
 */
static struct watchdog_info zynq_wdt_info = {
	.identity	= "zynq_wdt watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
};

/* Write access to Registers */
#define zynq_wdt_writereg(val, offset) __raw_writel(val, (wdt->regs) + offset)

/*************************Register Map**************************************/

/* Register Offsets for the WDT */
#define ZYNQ_WDT_ZMR_OFFSET	0x0	/* Zero Mode Register */
#define ZYNQ_WDT_CCR_OFFSET	0x4	/* Counter Control Register */
#define ZYNQ_WDT_RESTART_OFFSET	0x8	/* Restart Register */
#define ZYNQ_WDT_SR_OFFSET	0xC	/* Status Register */

/*
 * Zero Mode Register - This register controls how the time out is indicated
 * and also contains the access code to allow writes to the register (0xABC).
 */
#define ZYNQ_WDT_ZMR_WDEN_MASK	0x00000001 /* Enable the WDT */
#define ZYNQ_WDT_ZMR_RSTEN_MASK	0x00000002 /* Enable the reset output */
#define ZYNQ_WDT_ZMR_IRQEN_MASK	0x00000004 /* Enable IRQ output */
#define ZYNQ_WDT_ZMR_RSTLEN_16	0x00000030 /* Reset pulse of 16 pclk cycles */
#define ZYNQ_WDT_ZMR_ZKEY_VAL	0x00ABC000 /* Access key, 0xABC << 12 */
/*
 * Counter Control register - This register controls how fast the timer runs
 * and the reset value and also contains the access code to allow writes to
 * the register.
 */
#define ZYNQ_WDT_CCR_CRV_MASK	0x00003FFC /* Counter reset value */

/**
 * zynq_wdt_stop -  Stop the watchdog.
 *
 * @wdd: watchdog device
 *
 * Read the contents of the ZMR register, clear the WDEN bit
 * in the register and set the access key for successful write.
 *
 * Return: always 0
 */
static int zynq_wdt_stop(struct watchdog_device *wdd)
{
	spin_lock(&wdt->io_lock);
	zynq_wdt_writereg((ZYNQ_WDT_ZMR_ZKEY_VAL & (~ZYNQ_WDT_ZMR_WDEN_MASK)),
			 ZYNQ_WDT_ZMR_OFFSET);
	spin_unlock(&wdt->io_lock);
	return 0;
}

/**
 * zynq_wdt_reload -  Reload the watchdog timer (i.e. pat the watchdog).
 *
 * @wdd: watchdog device
 *
 * Write the restart key value (0x00001999) to the restart register.
 *
 * Return: always 0
 */
static int zynq_wdt_reload(struct watchdog_device *wdd)
{
	spin_lock(&wdt->io_lock);
	zynq_wdt_writereg(0x00001999, ZYNQ_WDT_RESTART_OFFSET);
	spin_unlock(&wdt->io_lock);
	return 0;
}

/**
 * zynq_wdt_start -  Enable and start the watchdog.
 *
 * @wdd: watchdog device
 *
 * The counter value is calculated according to the formula:
 *		calculated count = (timeout * clock) / prescaler + 1.
 * The calculated count is divided by 0x1000 to obtain the field value
 * to write to counter control register.
 * Clears the contents of prescaler and counter reset value. Sets the
 * prescaler to 4096 and the calculated count and access key
 * to write to CCR Register.
 * Sets the WDT (WDEN bit) and either the Reset signal(RSTEN bit)
 * or Interrupt signal(IRQEN) with a specified cycles and the access
 * key to write to ZMR Register.
 *
 * Return: always 0
 */
static int zynq_wdt_start(struct watchdog_device *wdd)
{
	unsigned int data = 0;
	unsigned short count;
	unsigned long clock_f = clk_get_rate(wdt->clk);

	/*
	 * 0x1000	- Counter Value Divide, to obtain the value of counter
	 *		  reset to write to control register.
	 */
	count = (wdd->timeout * (clock_f / wdt->prescaler)) / 0x1000 + 1;

	/* Check for boundary conditions of counter value */
	if (count > 0xFFF)
		count = 0xFFF;

	spin_lock(&wdt->io_lock);
	zynq_wdt_writereg(ZYNQ_WDT_ZMR_ZKEY_VAL, ZYNQ_WDT_ZMR_OFFSET);

	/* Shift the count value to correct bit positions */
	count = (count << 2) & ZYNQ_WDT_CCR_CRV_MASK;

	/* 0x00920000 - Counter register key value. */
	data = (count | 0x00920000 | wdt->ctrl_clksel);
	zynq_wdt_writereg(data, ZYNQ_WDT_CCR_OFFSET);
	data = ZYNQ_WDT_ZMR_WDEN_MASK | ZYNQ_WDT_ZMR_RSTLEN_16 |
			ZYNQ_WDT_ZMR_ZKEY_VAL;

	/* Reset on timeout if specified in device tree. */
	if (wdt->rst) {
		data |= ZYNQ_WDT_ZMR_RSTEN_MASK;
		data &= ~ZYNQ_WDT_ZMR_IRQEN_MASK;
	} else {
		data &= ~ZYNQ_WDT_ZMR_RSTEN_MASK;
		data |= ZYNQ_WDT_ZMR_IRQEN_MASK;
	}
	zynq_wdt_writereg(data, ZYNQ_WDT_ZMR_OFFSET);
	spin_unlock(&wdt->io_lock);
	zynq_wdt_writereg(0x00001999, ZYNQ_WDT_RESTART_OFFSET);
	return 0;
}

/**
 * zynq_wdt_settimeout -  Set a new timeout value for the watchdog device.
 *
 * @wdd: watchdog device
 * @new_time: new timeout value that needs to be set.
 * Return: 0 on success.
 *
 * Update the watchdog_device timeout with new value which is used when
 * zynq_wdt_start is called.
 */
static int zynq_wdt_settimeout(struct watchdog_device *wdd,
			       unsigned int new_time)
{
	wdd->timeout = new_time;
	return zynq_wdt_start(wdd);
}

/**
 * zynq_wdt_irq_handler - Notifies of watchdog timeout.
 *
 * @irq: interrupt number
 * @dev_id: pointer to a platform device structure
 * Return: IRQ_HANDLED
 *
 * The handler is invoked when the watchdog times out and a
 * reset on timeout has not been enabled.
 */
static irqreturn_t zynq_wdt_irq_handler(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	dev_info(&pdev->dev, "Watchdog timed out.\n");
	return IRQ_HANDLED;
}

/* Watchdog Core Ops */
static struct watchdog_ops zynq_wdt_ops = {
	.owner = THIS_MODULE,
	.start = zynq_wdt_start,
	.stop = zynq_wdt_stop,
	.ping = zynq_wdt_reload,
	.set_timeout = zynq_wdt_settimeout,
};

/* Watchdog Core Device */
static struct watchdog_device zynq_wdt_device = {
	.info = &zynq_wdt_info,
	.ops = &zynq_wdt_ops,
	.timeout = ZYNQ_WDT_DEFAULT_TIMEOUT,
	.min_timeout = ZYNQ_WDT_MIN_TIMEOUT,
	.max_timeout = ZYNQ_WDT_MAX_TIMEOUT,
};

/**
 * zynq_wdt_notify_sys -  Notifier for reboot or shutdown.
 *
 * @this: handle to notifier block.
 * @code: turn off indicator.
 * @unused: unused.
 * Return: NOTIFY_DONE.
 *
 * This notifier is invoked whenever the system reboot or shutdown occur
 * because we need to disable the WDT before system goes down as WDT might
 * reset on the next boot.
 */
static int zynq_wdt_notify_sys(struct notifier_block *this, unsigned long code,
			      void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		/* Stop the watchdog */
		zynq_wdt_stop(&zynq_wdt_device);
	return NOTIFY_DONE;
}

/* Notifier Structure */
static struct notifier_block zynq_wdt_notifier = {
	.notifier_call = zynq_wdt_notify_sys,
};

/************************Platform Operations*****************************/
/**
 * zynq_wdt_probe -  Probe call for the device.
 *
 * @pdev: handle to the platform device structure.
 * Return: 0 on success, negative error otherwise.
 *
 * It does all the memory allocation and registration for the device.
 */
static int zynq_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;
	int irq;
	unsigned long clock_f;

	/* Check whether WDT is in use, just for safety */
	if (wdt) {
		dev_err(&pdev->dev,
			"Device Busy, only 1 zynq_wdt instance supported.\n");
		return -EBUSY;
	}

	/* Allocate an instance of the zynq_wdt structure */
	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(wdt->regs))
		return PTR_ERR(wdt->regs);

	/* Register the reboot notifier */
	ret = register_reboot_notifier(&zynq_wdt_notifier);
	if (ret != 0) {
		dev_err(&pdev->dev, "cannot register reboot notifier err=%d)\n",
			ret);
		return ret;
	}

	/* Register the interrupt */
	of_property_read_u32(pdev->dev.of_node, "reset", &wdt->rst);
	irq = platform_get_irq(pdev, 0);
	if (!wdt->rst && irq >= 0) {
		ret = devm_request_irq(&pdev->dev, irq, zynq_wdt_irq_handler, 0,
				       pdev->name, pdev);
		if (ret) {
			dev_err(&pdev->dev,
				   "cannot register interrupt handler err=%d\n",
				   ret);
			goto err_notifier;
		}
	}

	/* Initialize the members of zynq_wdt structure */
	zynq_wdt_device.parent = &pdev->dev;
	of_get_property(pdev->dev.of_node, "timeout", &zynq_wdt_device.timeout);
	if (wdt_timeout < ZYNQ_WDT_MAX_TIMEOUT &&
			wdt_timeout > ZYNQ_WDT_MIN_TIMEOUT)
		zynq_wdt_device.timeout = wdt_timeout;
	else
		dev_info(&pdev->dev,
			    "timeout limited to 1 - %d sec, using default=%d\n",
			    ZYNQ_WDT_MAX_TIMEOUT, ZYNQ_WDT_DEFAULT_TIMEOUT);

	watchdog_set_nowayout(&zynq_wdt_device, nowayout);
	watchdog_set_drvdata(&zynq_wdt_device, &wdt);

	wdt->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(wdt->clk)) {
		dev_err(&pdev->dev, "input clock not found\n");
		ret = PTR_ERR(wdt->clk);
		goto err_notifier;
	}

	ret = clk_prepare_enable(wdt->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		goto err_notifier;
	}

	clock_f = clk_get_rate(wdt->clk);
	if (clock_f <= 10000000) {/* For PEEP */
		wdt->prescaler = 64;
		wdt->ctrl_clksel = 1;
	} else if (clock_f <= 75000000) {
		wdt->prescaler = 256;
		wdt->ctrl_clksel = 2;
	} else { /* For Zynq */
		wdt->prescaler = 4096;
		wdt->ctrl_clksel = 3;
	}

	spin_lock_init(&wdt->io_lock);

	/* Register the WDT */
	ret = watchdog_register_device(&zynq_wdt_device);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register wdt device\n");
		goto err_clk_disable;
	}
	platform_set_drvdata(pdev, wdt);

	dev_info(&pdev->dev, "Xilinx Watchdog Timer at %p with timeout %ds%s\n",
		 wdt->regs, zynq_wdt_device.timeout,
		 nowayout ? ", nowayout" : "");

	return 0;

err_clk_disable:
	clk_disable_unprepare(wdt->clk);
err_notifier:
	unregister_reboot_notifier(&zynq_wdt_notifier);
	return ret;
}

/**
 * zynq_wdt_remove -  Probe call for the device.
 *
 * @pdev: handle to the platform device structure.
 * Return: 0 on success, otherwise negative error.
 *
 * Unregister the device after releasing the resources.
 * Stop is allowed only when nowayout is disabled.
 */
static int __exit zynq_wdt_remove(struct platform_device *pdev)
{
	int res = 0;
	int irq;

	if (wdt && !nowayout) {
		zynq_wdt_stop(&zynq_wdt_device);
		watchdog_unregister_device(&zynq_wdt_device);
		unregister_reboot_notifier(&zynq_wdt_notifier);
		irq = platform_get_irq(pdev, 0);
		clk_disable_unprepare(wdt->clk);
	} else {
		dev_err(&pdev->dev, "Cannot stop watchdog, still ticking\n");
		return -ENOTSUPP;
	}
	return res;
}

/**
 * zynq_wdt_shutdown -  Stop the device.
 *
 * @pdev: handle to the platform structure.
 *
 */
static void zynq_wdt_shutdown(struct platform_device *pdev)
{
	/* Stop the device */
	zynq_wdt_stop(&zynq_wdt_device);
	clk_disable_unprepare(wdt->clk);
}

#ifdef CONFIG_PM_SLEEP
/**
 * zynq_wdt_suspend -  Stop the device.
 *
 * @dev: handle to the device structure.
 * Return: 0 always.
 */
static int zynq_wdt_suspend(struct device *dev)
{
	/* Stop the device */
	zynq_wdt_stop(&zynq_wdt_device);
	clk_disable(wdt->clk);
	return 0;
}

/**
 * zynq_wdt_resume -  Resume the device.
 *
 * @dev: handle to the device structure.
 * Return: 0 on success, errno otherwise.
 */
static int zynq_wdt_resume(struct device *dev)
{
	int ret;

	ret = clk_enable(wdt->clk);
	if (ret) {
		dev_err(dev, "unable to enable clock\n");
		return ret;
	}
	/* Start the device */
	zynq_wdt_start(&zynq_wdt_device);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(zynq_wdt_pm_ops, zynq_wdt_suspend, zynq_wdt_resume);

static struct of_device_id zynq_wdt_of_match[] = {
	{ .compatible = "xlnx,ps7-wdt-1.00.a", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, zynq_wdt_of_match);

/* Driver Structure */
static struct platform_driver zynq_wdt_driver = {
	.probe		= zynq_wdt_probe,
	.remove		= zynq_wdt_remove,
	.shutdown	= zynq_wdt_shutdown,
	.driver		= {
		.name	= "zynq-wdt",
		.owner	= THIS_MODULE,
		.of_match_table = zynq_wdt_of_match,
		.pm	= &zynq_wdt_pm_ops,
	},
};

/**
 * zynq_wdt_init -  Register the WDT.
 *
 * Return: 0 on success, otherwise negative error.
 *
 * If using noway out, the use count will be incremented.
 * This will prevent unloading the module. An attempt to
 * unload the module will result in a warning from the kernel.
 */
static int __init zynq_wdt_init(void)
{
	int res = platform_driver_register(&zynq_wdt_driver);
	if (!res && nowayout)
		try_module_get(THIS_MODULE);
	return res;
}

/**
 * zynq_wdt_exit -  Unregister the WDT.
 */
static void __exit zynq_wdt_exit(void)
{
	platform_driver_unregister(&zynq_wdt_driver);
}

module_init(zynq_wdt_init);
module_exit(zynq_wdt_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Watchdog driver for PS WDT");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform: zynq_wdt");

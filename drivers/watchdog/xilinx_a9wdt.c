/*
 * Xilinx PSS A9 Private WDT driver
 *
 * Copyright (c) 2010 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */

#include <linux/io.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* These are temporary values. Need to finalize when we have a fixed clock */
#define XA9WDT_CLOCK		5000000
#define XA9WDT_MAX_TIMEOUT	600
#define XA9WDT_DEFAULT_TIMEOUT	 10
#define XA9WDT_PRESCALER	 00

static int wdt_timeout = XA9WDT_DEFAULT_TIMEOUT;
static u32 wdt_count;
static int nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_timeout, int, 0);
MODULE_PARM_DESC(wdt_timeout,
		 "Watchdog timeout in seconds. (default="
		 __MODULE_STRING(XA9WDT_DEFAULT_TIMEOUT) ")");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
#endif

/**
 * struct xa9wdt - Watchdog device structure.
 * @regs:	baseaddress of device.
 * @busy:	flag for the device.
 * @miscdev:	miscdev structure.
 * @io_lock:	lock used for synchronization.
 *
 * Structure containing the standard miscellaneous device 'miscdev'
 * structure along with the parameters specific to pss watchdog.
 */
struct xa9wdt {
	void __iomem		*regs;
	unsigned long		busy;
	struct miscdevice	miscdev;
	spinlock_t		io_lock;
};

static struct xa9wdt *wdt;

/*
 * Info structure used to indicate the features supported by the device
 * to the upper layers. This is defined in watchdog.h header file.
 */
static struct watchdog_info xa9wdt_info = {
	.identity	= "xa9wdt watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
};

/* Write access to Registers */
#define xa9wdt_writereg(val, offset) \
				__raw_writel(val, (wdt->regs) + offset)
#define xa9wdt_readreg(offset) __raw_readl((wdt->regs) + offset)

/*************************Register Map**************************************/

/* Register Offsets for the WDT */
#define XA9WDT_LOAD_OFFSET		0x00 /* Watchdog Load Register */
#define XA9WDT_CONTROL_OFFSET		0x08 /* Watchdog Control Register */
#define XA9WDT_DISABLE_OFFSET		0x14 /* Watchdog Disable Register */

/**
 * xa9wdt_start -  Enable and start the watchdog.
 *
 * The clock to the WDT is 5 MHz and the counter value is calculated
 * according to the formula:
 *	load count = ((timeout * clock) / (prescalar + 1)) - 1.
 * This needs to be re-visited when the PERIPHCLK clock changes in HW.
 *
 **/
static void xa9wdt_start(void)
{
	wdt_count = ((wdt_timeout * XA9WDT_CLOCK) / (XA9WDT_PRESCALER + 1)) - 1;

	spin_lock(&wdt->io_lock);
	xa9wdt_writereg(wdt_count, XA9WDT_LOAD_OFFSET);

	xa9wdt_writereg(0x09 | (XA9WDT_PRESCALER << 8), XA9WDT_CONTROL_OFFSET);
	spin_unlock(&wdt->io_lock);
}

/**
 * xa9wdt_stop -  Stop the watchdog.
 *
 * Read the contents of the Watchdog Control register, and clear the
 * watchdog enable bit in the register.
 **/
static void xa9wdt_stop(void)
{
	spin_lock(&wdt->io_lock);
	xa9wdt_writereg(0x12345678, XA9WDT_DISABLE_OFFSET);
	xa9wdt_writereg(0x87654321, XA9WDT_DISABLE_OFFSET);
	xa9wdt_writereg(0x00, XA9WDT_CONTROL_OFFSET);
	spin_unlock(&wdt->io_lock);
}

/**
 * xa9wdt_reload -  Reload the watchdog timer.
 *
 * Write the wdt_count to the Watchdog Load register.
 **/
static void xa9wdt_reload(void)
{
	spin_lock(&wdt->io_lock);
	xa9wdt_writereg(wdt_count, XA9WDT_LOAD_OFFSET);
	spin_unlock(&wdt->io_lock);
}

/**
 * xa9wdt_settimeout -  Set a new timeout value for the watchdog device.
 *
 * @new_time:	new timeout value that needs to be set.
 *
 * Check whether the timeout is in the valid range. If not, don't update the
 * timeout value, otherwise update the global variable wdt_timeout with new
 * value which is used when xa9wdt_start is called.
 * Returns -ENOTSUPP, if timeout value is out-of-range.
 * Returns 0 on success.
 **/
static int xa9wdt_settimeout(int new_time)
{
	if ((new_time <= 0) || (new_time > XA9WDT_MAX_TIMEOUT))
		return -ENOTSUPP;
	wdt_timeout = new_time;
	return 0;
}

/*************************WDT Device Operations****************************/

/**
 * xa9wdt_open -  Open the watchdog device.
 *
 * @inode:	inode of device.
 * @file:	file handle to device.
 *
 * Check whether the device is already in use and then only start the watchdog
 * timer. Returns 0 on success, otherwise -EBUSY.
 **/
static int xa9wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &(wdt->busy)))
		return -EBUSY;
	xa9wdt_start();
	return nonseekable_open(inode, file);
}

/**
 * xa9wdt_close -  Close the watchdog device only when nowayout is disabled.
 *
 * @inode:	inode of device.
 * @file:	file handle to device.
 *
 * Stops the watchdog and clears the busy flag.
 * Returns 0 on success, -ENOTSUPP when the nowayout is enabled.
 **/
static int xa9wdt_close(struct inode *inode, struct file *file)
{
	if (!nowayout) {
		/* Disable the watchdog */
		xa9wdt_stop();
		clear_bit(0, &(wdt->busy));
		return 0;
	}
	return -ENOTSUPP;
}

/**
 * xa9wdt_ioctl -  Handle IOCTL operations on the device.
 *
 * @inode:	inode of the device.
 * @file:	file handle to the device.
 * @cmd:	watchdog command.
 * @arg:	argument pointer.
 *
 * The watchdog API defines a common set of functions for all
 * watchdogs according to available features. The IOCTL's are defined in
 * watchdog.h header file, based on the features of device, we support
 * the following IOCTL's - WDIOC_KEEPALIVE, WDIOC_GETSUPPORT,
 * WDIOC_SETTIMEOUT, WDIOC_GETTIMEOUT, WDIOC_SETOPTIONS.
 * Returns 0 on success, negative error otherwise.
 **/
static int xa9wdt_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_value;

	switch (cmd) {
	case WDIOC_KEEPALIVE:
		/* pat the watchdog */
		xa9wdt_reload();
		return 0;

	case WDIOC_GETSUPPORT:
		/*
		 * Indicate the features supported to the user through the
		 * instance of watchdog_info structure.
		 */
		return copy_to_user(argp, &xa9wdt_info,
				    sizeof(xa9wdt_info)) ? -EFAULT : 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;

		/* Check for the validity */
		if (xa9wdt_settimeout(new_value))
			return -EINVAL;
		xa9wdt_start();
		/* Return current value */
		return put_user(wdt_timeout, p);

	case WDIOC_GETTIMEOUT:
		/* Return the current timeout */
		return put_user(wdt_timeout, p);

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_SETOPTIONS:
		if (get_user(new_value, p))
			return -EFAULT;
		/* Based on the flag, enable or disable the watchdog */
		if (new_value & WDIOS_DISABLECARD)
			xa9wdt_stop();
		if (new_value & WDIOS_ENABLECARD)
			xa9wdt_start();
		return 0;

	default:
		return -ENOIOCTLCMD;
	}
}

/**
 * xa9wdt_write -  Pats the watchdog, i.e. reload the counter.
 *
 * @file:	file handle to the device.
 * @data:	value is ignored.
 * @len:	number of bytes to be processed.
 * @ppos:	value is ignored.
 *
 * A write to watchdog device is similar to keepalive signal.
 * Returns the value of len.
 **/
static ssize_t xa9wdt_write(struct file *file, const char __user *data,
			    size_t len, loff_t *ppos)
{
	xa9wdt_reload();		/* pat the watchdog */
	return len;
}

/**
 * xa9wdt_notify_sys -  Notifier for reboot or shutdown.
 *
 * @this:	handle to notifier block.
 * @code:	turn off indicator.
 * @unused:	unused.
 *
 * This notifier is invoked whenever the system reboot or shutdown occur
 * because we need to disable the WDT before system goes down as WDT might
 * reset on the next boot.
 * Returns NOTIFY_DONE.
 **/
static int xa9wdt_notify_sys(struct notifier_block *this, unsigned long code,
			     void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Stop the watchdog */
		xa9wdt_stop();
	}
	return NOTIFY_DONE;
}

/* File operations structure */
static const struct file_operations xa9wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= xa9wdt_ioctl,
	.open		= xa9wdt_open,
	.release	= xa9wdt_close,
	.write		= xa9wdt_write,
};

/* Notifier Structure */
static struct notifier_block xa9wdt_notifier = {
	.notifier_call = xa9wdt_notify_sys,
};

/************************Platform Operations*****************************/
/**
 * xa9wdt_probe -  Probe call for the device.
 *
 * @pdev:	handle to the platform device structure.
 *
 * It does all the memory allocation and registration for the device.
 * Returns 0 on success, negative error otherwise.
 **/
static int __init xa9wdt_probe(struct platform_device *pdev)
{
	struct resource *regs;
	int ret;

	/* Check whether WDT is in use, just for safety */
	if (wdt) {
		dev_err(&pdev->dev, "Device Busy, only 1 xa9wdt instance "
			"supported.\n");
		return -EBUSY;
	}

	/* Get the device base address */
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "Unable to locate mmio resource\n");
		return -ENODEV;
	}

	/* Allocate an instance of the xa9wdt structure */
	wdt = kzalloc(sizeof(struct xa9wdt), GFP_KERNEL);
	if (!wdt) {
		dev_err(&pdev->dev, "No memory for wdt structure\n");
		return -ENOMEM;
	}

	wdt->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!wdt->regs) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Could not map I/O memory\n");
		goto err_free;
	}

	/* Switch to Watchdog mode */
	xa9wdt_writereg(0x08, XA9WDT_CONTROL_OFFSET);

	/* Register the reboot notifier */
	ret = register_reboot_notifier(&xa9wdt_notifier);
	if (ret != 0) {
		dev_err(&pdev->dev, "cannot register reboot notifier err=%d)\n",
			ret);
		goto err_iounmap;
	}

	/* Initialize the members of xa9wdt structure */
	wdt->miscdev.minor	= WATCHDOG_MINOR,
	wdt->miscdev.name	= "watchdog",
	wdt->miscdev.fops	= &xa9wdt_fops,

	/* Initialize the busy flag to zero */
	clear_bit(0, &wdt->busy);
	spin_lock_init(&wdt->io_lock);

	/* Register the WDT */
	ret = misc_register(&wdt->miscdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register wdt miscdev\n");
		goto err_notifier;
	}

	platform_set_drvdata(pdev, wdt);
	wdt->miscdev.parent = &pdev->dev;

	dev_info(&pdev->dev, "Xilinx A9 Watchdog Timer at 0x%p with timeout "
		 "%d seconds%s\n", wdt->regs, wdt_timeout,
		 nowayout ? ", nowayout" : "");

	return 0;

err_notifier:
	unregister_reboot_notifier(&xa9wdt_notifier);
err_iounmap:
	iounmap(wdt->regs);
err_free:
	kfree(wdt);
	wdt = NULL;
	return ret;
}

/**
 * xa9wdt_remove -  Probe call for the device.
 *
 * @pdev:	handle to the platform device structure.
 *
 * Unregister the device after releasing the resources.
 * Stop is allowed only when nowayout is disabled.
 * Returns 0 on success, otherwise negative error.
 **/
static int __exit xa9wdt_remove(struct platform_device *pdev)
{
	int ret = 0;

	if (wdt && !nowayout) {
		xa9wdt_stop();
		ret = misc_deregister(&wdt->miscdev);
		if (!ret)
			wdt->miscdev.parent = NULL;
		unregister_reboot_notifier(&xa9wdt_notifier);
		iounmap(wdt->regs);
		kfree(wdt);
		wdt = NULL;
		platform_set_drvdata(pdev, NULL);
	} else {
		dev_err(&pdev->dev, "Cannot stop watchdog, still ticking\n");
		return -ENOTSUPP;
	}
	return ret;
}

/**
 * xa9wdt_shutdown -  Stop the device.
 *
 * @pdev:	handle to the platform structure.
 *
 **/
static void xa9wdt_shutdown(struct platform_device *pdev)
{
	/* Stop the device */
	xa9wdt_stop();
}

#ifdef CONFIG_PM
/**
 * xa9wdt_suspend -  Stop the device.
 *
 * @pdev:	handle to the platform structure.
 * @message:	message to the device.
 *
 * Returns 0, always.
 **/
static int xa9wdt_suspend(struct platform_device *pdev, pm_message_t message)
{
	/* Stop the device */
	xa9wdt_stop();
	return 0;
}

/**
 * xa9wdt_resume -  Resume the device.
 *
 * @pdev:	handle to the platform structure.
 *
 * Returns 0, always.
 **/
static int xa9wdt_resume(struct platform_device *pdev)
{
	/* Start the device */
	xa9wdt_start();
	return 0;
}
#else
#define xa9wdt_suspend	NULL
#define xa9wdt_resume	NULL
#endif

/* Driver Structure */
static struct platform_driver xa9wdt_driver = {
	.probe		= xa9wdt_probe,
	.remove		= __exit_p(xa9wdt_remove),
	.shutdown	= xa9wdt_shutdown,
	.suspend	= xa9wdt_suspend,
	.resume		= xa9wdt_resume,
	.driver		= {
		.name	= "xilinx_a9wdt",
		.owner	= THIS_MODULE,
	},
};

/**
 * xa9wdt_init -  Register the WDT.
 *
 * Returns 0 on success, otherwise negative error.
 */
static int __init xa9wdt_init(void)
{
	/*
	 * Check that the timeout value is within range. If not, reset to the
	 * default.
	 */
	if (xa9wdt_settimeout(wdt_timeout)) {
		xa9wdt_settimeout(XA9WDT_DEFAULT_TIMEOUT);
		pr_info("xa9wdt: wdt_timeout value limited to 1 - %d sec, "
		"using default %dsec timeout\n",
		XA9WDT_MAX_TIMEOUT, XA9WDT_DEFAULT_TIMEOUT);
	}
	return platform_driver_register(&xa9wdt_driver);
}

/**
 * xa9wdt_exit -  Unregister the WDT.
 */
static void __exit xa9wdt_exit(void)
{
	platform_driver_unregister(&xa9wdt_driver);
}

module_init(xa9wdt_init);
module_exit(xa9wdt_exit);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Driver for Pele A9 WDT");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform: xilinx_a9wdt");

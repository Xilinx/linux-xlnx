/*
 * Xilinx PSS WDT driver
 *
 * Copyright (c) 20010 Xilinx Inc.
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
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/slab.h>


#define XWDTPSS_DEFAULT_TIMEOUT	10	/* Supports 1 - 600 sec */

static int wdt_timeout = XWDTPSS_DEFAULT_TIMEOUT;
static int nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_timeout, int, 0);
MODULE_PARM_DESC(wdt_timeout,
		 "Watchdog time in seconds. (default="
		 __MODULE_STRING(XWDTPSS_DEFAULT_TIMEOUT) ")");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
#endif

/**
 * struct xwdtpss - Watchdog device structure.
 * @regs: baseaddress of device.
 * @busy: flag for the device.
 * @miscdev: miscdev structure.
 *
 * Structure containing the standard miscellaneous device 'miscdev'
 * structure along with the parameters specific to pss watchdog.
 */
struct xwdtpss {
	void __iomem		*regs;		/* Base address */
	unsigned long		busy;		/* Device Status */
	struct miscdevice	miscdev;	/* Device structure */
	spinlock_t		io_lock;
};
static struct xwdtpss *wdt;

/*
 * Info structure used to indicate the features supported by the device
 * to the upper layers. This is defined in watchdog.h header file.
 */
static struct watchdog_info xwdtpss_info = {
	.identity	= "xwdtpss watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
};

/* Write access to Registers */
#define xwdtpss_writereg(val, offset) \
				__raw_writel(val, (wdt->regs) + offset)

/*************************Register Map**************************************/

/* Register Offsets for the WDT */
#define XWDTPSS_ZMR_OFFSET	0x0	/* Zero Mode Register */
#define XWDTPSS_CCR_OFFSET	0x4	/* Counter Control Register */
#define XWDTPSS_RESTART_OFFSET	0x8	/* Restart Register */
#define XWDTPSS_SR_OFFSET	0xC	/* Status Register */

/*
 * Zero Mode Register - This register controls how the time out is indicated
 * and also contains the access code to allow writes to the register (0xABC).
 */
#define XWDTPSS_ZMR_WDEN_MASK	0x00000001 /* Enable the WDT */
#define XWDTPSS_ZMR_RSTEN_MASK	0x00000002 /* Enable the reset output */
#define XWDTPSS_ZMR_RSTLEN_2	0x00000000 /* Reset pulse of 2 pclk cycles */
#define XWDTPSS_ZMR_ZKEY_VAL	0x00ABC000 /* Access key, 0xABC << 12 */
/*
 * Counter Control register - This register controls how fast the timer runs
 * and the reset value and also contains the access code to allow writes to
 * the register.
 */
#define XWDTPSS_CCR_CRV_MASK	0x00003FFC /* Counter reset value */


/**
 * xwdtpss_stop -  Stop the watchdog.
 *
 * Read the contents of the ZMR register, clear the WDEN bit
 * in the register and set the access key for successful write.
 **/
static void xwdtpss_stop(void)
{
	spin_lock(&wdt->io_lock);
	xwdtpss_writereg((XWDTPSS_ZMR_ZKEY_VAL & (~XWDTPSS_ZMR_WDEN_MASK)),
			 XWDTPSS_ZMR_OFFSET);
	spin_unlock(&wdt->io_lock);
}

/**
 * xwdtpss_reload -  Reload the watchdog timer (i.e. pat the watchdog).
 *
 * Write the restart key value (0x00001999) to the restart register.
 **/
static void xwdtpss_reload(void)
{
	spin_lock(&wdt->io_lock);
	xwdtpss_writereg(0x00001999, XWDTPSS_RESTART_OFFSET);
	spin_unlock(&wdt->io_lock);
}

/**
 * xwdtpss_start -  Enable and start the watchdog.
 *
 * The clock to the WDT is 100 MHz, the prescalar is set to divide
 * the clock by 4096 and the counter value is calculated according to
 * the formula:
 *		calculated count = (timeout * clock) / prescalar + 1.
 * The calculated count is divided by 0x1000 to obtain the field value
 * to write to counter control register.
 * Clears the contents of prescalar and counter reset value. Sets the
 * prescalar to 4096 and the calculated count and access key
 * to write to CCR Register.
 * Sets the WDT (WDEN bit) and Reset signal(RSTEN bit) with length as 2 pclk
 * cycles and the access key to write to ZMR Register.
 **/
static void xwdtpss_start(void)
{
	unsigned int data = 0;
	int count;

	/*
	 * 64		- Prescalar divide value.
	 * 0x1000	- Counter Value Divide, to obtain the value of counter
	 *		  reset to write to control register.
	 * 781250	- Input clock value.
	 * This code needs to be modified when the clock value increases
	 * in H/W.
	 */
	count = (wdt_timeout * 781250) / (64 * 0x1000) + 1;

	/* Check for boundary conditions of counter value */
	if (count > 0xFFF)
		count = 0xFFF;

	spin_lock(&wdt->io_lock);
	xwdtpss_writereg(XWDTPSS_ZMR_ZKEY_VAL, XWDTPSS_ZMR_OFFSET);

	/* Shift the count value to correct bit positions */
	count = (count << 2) & XWDTPSS_CCR_CRV_MASK;

	/*
	 * 0x00000001 - Bit value to set 64 prescalar divide.
	 * 0x00920000 - Counter register key value.
	 */
	data = (count | 0x00920000 | 0x00000001);
	xwdtpss_writereg(data, XWDTPSS_CCR_OFFSET);

	data = (XWDTPSS_ZMR_WDEN_MASK | XWDTPSS_ZMR_RSTEN_MASK | \
		XWDTPSS_ZMR_RSTLEN_2 | XWDTPSS_ZMR_ZKEY_VAL);
	xwdtpss_writereg(data, XWDTPSS_ZMR_OFFSET);
	spin_unlock(&wdt->io_lock);
	xwdtpss_writereg(0x00001999, XWDTPSS_RESTART_OFFSET);
}

/**
 * xwdtpss_settimeout -  Set a new timeout value for the watchdog device.
 *
 * @new_time: new timeout value that needs to be set.
 *
 * Check whether the timeout is in the valid range. If not, don't update the
 * timeout value, otherwise update the global variable wdt_timeout with new
 * value which is used when xwdtpss_start is called.
 * Returns -ENOTSUPP, if timeout value is out-of-range.
 * Returns 0 on success.
 **/
static int xwdtpss_settimeout(int new_time)
{
	if ((new_time <= 0) || (new_time > 600))
		return -ENOTSUPP;
	wdt_timeout = new_time;
	return 0;
}

/*************************WDT Device Operations****************************/

/**
 * xwdtpss_open -  Open the watchdog device.
 *
 * @inode: inode of device.
 * @file: file handle to device.
 *
 * Check whether the device is already in use and then only start the watchdog
 * timer. Returns 0 on success, otherwise -EBUSY.
 **/
static int xwdtpss_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &(wdt->busy)))
		return -EBUSY;
	xwdtpss_start();
	return nonseekable_open(inode, file);
}

/**
 * xwdtpss_close -  Close the watchdog device only when nowayout is disabled.
 *
 * @inode: inode of device.
 * @file: file handle to device.
 *
 * Stops the watchdog and clears the busy flag.
 * Returns 0 on success, -ENOTSUPP when the nowayout is enabled.
 **/
static int xwdtpss_close(struct inode *inode, struct file *file)
{
	if (!nowayout) {
		/* Disable the watchdog */
		xwdtpss_stop();
		clear_bit(0, &(wdt->busy));
		return 0;
	}
	return -ENOTSUPP;
}

/**
 * xwdtpss_ioctl -  Handle IOCTL operations on the device.
 *
 * @inode: inode of the device.
 * @file: file handle to the device.
 * @cmd: watchdog command.
 * @arg: argument pointer.
 *
 * The watchdog API defines a common set of functions for all
 * watchdogs according to available features. The IOCTL's are defined in
 * watchdog.h header file, based on the features of device, we support
 * the following IOCTL's - WDIOC_KEEPALIVE, WDIOC_GETSUPPORT,
 * WDIOC_SETTIMEOUT, WDIOC_GETTIMEOUT, WDIOC_SETOPTIONS.
 * Returns 0 on success, negative error otherwise.
 **/
static int xwdtpss_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_value;

	switch (cmd) {
	case WDIOC_KEEPALIVE:
		/* pat the watchdog */
		xwdtpss_reload();
		return 0;

	case WDIOC_GETSUPPORT:
		/*
		 * Indicate the features supported to the user through the
		 * instance of watchdog_info structure.
		 */
		return copy_to_user(argp, &xwdtpss_info,
				    sizeof(xwdtpss_info)) ? -EFAULT : 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;

		/* Check for the validity */
		if (xwdtpss_settimeout(new_value))
			return -EINVAL;
		xwdtpss_start();
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
			xwdtpss_stop();
		if (new_value & WDIOS_ENABLECARD)
			xwdtpss_start();
		return 0;

	default:
		return -ENOIOCTLCMD;
	}
}

/**
 * xwdtpss_write -  Pats the watchdog, i.e. reload the counter.
 *
 * @file: file handle to the device.
 * @data: value is ignored.
 * @len:  count of bytes to be processed.
 * @ppos: value is ignored.
 *
 * A write to watchdog device is similar to keepalive signal.
 * Returns the len value.
 **/
static ssize_t xwdtpss_write(struct file *file, const char __user *data,
			     size_t len, loff_t *ppos)
{
	xwdtpss_reload();		/* pat the watchdog */
	return len;
}

/**
 * xwdtpss_notify_sys -  Notifier for reboot or shutdown.
 *
 * @this: handle to notifier block.
 * @code: turn off indicator.
 * @unused: unused.
 *
 * This notifier is invoked whenever the system reboot or shutdown occur
 * because we need to disable the WDT before system goes down as WDT might
 * reset on the next boot.
 * Returns NOTIFY_DONE.
 **/
static int xwdtpss_notify_sys(struct notifier_block *this, unsigned long code,
			      void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Stop the watchdog */
		xwdtpss_stop();
	}
	return NOTIFY_DONE;
}

/* File operations structure */
static const struct file_operations xwdtpss_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= xwdtpss_ioctl,
	.open		= xwdtpss_open,
	.release	= xwdtpss_close,
	.write		= xwdtpss_write,
};

/* Notifier Structure */
static struct notifier_block xwdtpss_notifier = {
	.notifier_call = xwdtpss_notify_sys,
};

/************************Platform Operations*****************************/
/**
 * xwdtpss_probe -  Probe call for the device.
 *
 * @pdev: handle to the platform device structure.
 *
 * It does all the memory allocation and registration for the device.
 * Returns 0 on success, negative error otherwise.
 **/
static int __init xwdtpss_probe(struct platform_device *pdev)
{
	struct resource *regs;
	int res;

	/* Check whether WDT is in use, just for safety */
	if (wdt) {
		dev_err(&pdev->dev, "Device Busy, only 1 xwdtpss instance \
			supported.\n");
		return -EBUSY;
	}

	/* Get the device base address */
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "Unable to locate mmio resource\n");
		return -ENODEV;
	}

	/* Allocate an instance of the xwdtpss structure */
	wdt = kzalloc(sizeof(struct xwdtpss), GFP_KERNEL);
	if (!wdt) {
		dev_err(&pdev->dev, "No memory for wdt structure\n");
		return -ENOMEM;
	}

	wdt->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!wdt->regs) {
		res = -ENOMEM;
		dev_err(&pdev->dev, "Could not map I/O memory\n");
		goto err_free;
	}

	/* Register the reboot notifier */
	res = register_reboot_notifier(&xwdtpss_notifier);
	if (res != 0) {
		dev_err(&pdev->dev, "cannot register reboot notifier err=%d)\n",
			res);
		goto err_iounmap;
	}

	/* Initialize the members of xwdtpss structure */
	wdt->miscdev.minor	= WATCHDOG_MINOR,
	wdt->miscdev.name	= "watchdog",
	wdt->miscdev.fops	= &xwdtpss_fops,

	/* Initialize the busy flag to zero */
	clear_bit(0, &wdt->busy);
	spin_lock_init(&wdt->io_lock);

	/* Register the WDT */
	res = misc_register(&wdt->miscdev);
	if (res) {
		dev_err(&pdev->dev, "Failed to register wdt miscdev\n");
		goto err_notifier;
	}
	platform_set_drvdata(pdev, wdt);
	wdt->miscdev.parent = &pdev->dev;

	dev_info(&pdev->dev, "Xilinx Watchdog Timer at 0x%p with timeout "
		 "%d seconds%s\n", wdt->regs, wdt_timeout,
		 nowayout ? ", nowayout" : "");

	return 0;

err_notifier:
	unregister_reboot_notifier(&xwdtpss_notifier);
err_iounmap:
	iounmap(wdt->regs);
err_free:
	kfree(wdt);
	wdt = NULL;
	return res;
}

/**
 * xwdtpss_remove -  Probe call for the device.
 *
 * @pdev: handle to the platform device structure.
 *
 * Unregister the device after releasing the resources.
 * Stop is allowed only when nowayout is disabled.
 * Returns 0 on success, otherwise negative error.
 **/
static int __exit xwdtpss_remove(struct platform_device *pdev)
{
	int res = 0;

	if (wdt && !nowayout) {
		xwdtpss_stop();
		res = misc_deregister(&wdt->miscdev);
		if (!res)
			wdt->miscdev.parent = NULL;
		unregister_reboot_notifier(&xwdtpss_notifier);
		iounmap(wdt->regs);
		kfree(wdt);
		wdt = NULL;
		platform_set_drvdata(pdev, NULL);
	} else {
		dev_err(&pdev->dev, "Cannot stop watchdog, still ticking\n");
		return -ENOTSUPP;
	}
	return res;
}

/**
 * xwdtpss_shutdown -  Stop the device.
 *
 * @pdev: handle to the platform structure.
 *
 **/
static void xwdtpss_shutdown(struct platform_device *pdev)
{
	/* Stop the device */
	xwdtpss_stop();
}

#ifdef CONFIG_PM
/**
 * xwdtpss_suspend -  Stop the device.
 *
 * @pdev: handle to the platform structure.
 * @message: message to the device.
 *
 * Returns 0 always.
 **/
static int xwdtpss_suspend(struct platform_device *pdev, pm_message_t message)
{
	/* Stop the device */
	xwdtpss_stop();
	return 0;
}

/**
 * xwdtpss_resume -  Resume the device.
 *
 * @pdev: handle to the platform structure.
 *
 * Returns 0 always.
 **/
static int xwdtpss_resume(struct platform_device *pdev)
{
	/* Start the device */
	xwdtpss_start();
	return 0;
}
#else
#define xwdtpss_suspend NULL
#define xwdtpss_resume	NULL
#endif

/* Driver Structure */
static struct platform_driver xwdtpss_driver = {
	.probe		= xwdtpss_probe,
	.remove		= __exit_p(xwdtpss_remove),
	.shutdown	= xwdtpss_shutdown,
	.suspend	= xwdtpss_suspend,
	.resume		= xwdtpss_resume,
	.driver		= {
		.name	= "xilinx_pss_wdt",
		.owner	= THIS_MODULE,
	},
};

/**
 * xwdtpss_init -  Register the WDT.
 *
 * Returns 0 on success, otherwise negative error.
 */
static int __init xwdtpss_init(void)
{
	/*
	 * Check that the timeout value is within range. If not, reset to the
	 * default.
	 */
	if (xwdtpss_settimeout(wdt_timeout)) {
		xwdtpss_settimeout(XWDTPSS_DEFAULT_TIMEOUT);
		pr_info("xwdtpss: wdt_timeout value limited to 1 - 600 sec, "
			"using default timeout of %dsec\n",
			XWDTPSS_DEFAULT_TIMEOUT);
	}
	return platform_driver_register(&xwdtpss_driver);
}

/**
 * xwdtpss_exit -  Unregister the WDT.
 */
static void __exit xwdtpss_exit(void)
{
	platform_driver_unregister(&xwdtpss_driver);
}

module_init(xwdtpss_init);
module_exit(xwdtpss_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Watchdog driver for PSS WDT");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform: pss wdt");

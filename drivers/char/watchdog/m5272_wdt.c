/*
 *	Watchdog driver for the MCF5272
 *
 *      (c) Copyright 2005 Javier Herrero <jherrero@hvsistemas.es>
 *          Based on SoftDog driver by Alan Cox <alan@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Oleg Drokin nor iXcelerator.com admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 2005           Javier Herrero <jherrero@hvsistemas.es>
 *
 *      03/05/2005 Initial release
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>

#include <asm/coldfire.h>
#include <asm/m5272sim.h>

#define M5272_CLOSE_MAGIC	(0x5afc4453)

static unsigned long m5272wdt_users;
static int expect_close;
static unsigned short wdt_reset_ref;
static int boot_status;
#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

/*
 *	Allow only one person to hold it open
 */
static int m5272dog_open(struct inode *inode, struct file *file)
{
	unsigned short* w;
	
	nonseekable_open(inode, file);
	if (test_and_set_bit(1,&m5272wdt_users))
		return -EBUSY;

	/* Activate M5272 Watchdog timer */
	
	w = (unsigned short*)(MCF_MBAR + MCFSIM_WRRR);
	*w = wdt_reset_ref | 0x0001;
	w = (unsigned short*)(MCF_MBAR + MCFSIM_WCR);
	*w = 0;
	return 0;
}

/*
 *	Shut off the timer.
 * 	Lock it in if it's a module and we defined ...NOWAYOUT
 *	Oddly, the watchdog can only be enabled, but we can turn off
 *	the interrupt, which appears to prevent the watchdog timing out.
 */
static int m5272dog_release(struct inode *inode, struct file *file)
{
	unsigned short* w;

	w = (unsigned short*)(MCF_MBAR + MCFSIM_WRRR);
	
	if (expect_close == M5272_CLOSE_MAGIC) {
		*w = 0;
	} else {
		printk(KERN_CRIT "WATCHDOG: WDT device closed unexpectedly.  WDT will not stop!\n");
	}
	clear_bit(1, &m5272wdt_users);
	expect_close = 0;
	return 0;
}

static ssize_t m5272dog_write(struct file *file, const char *data, size_t len, loff_t *ppos)
{
	unsigned short* w;
	
	if (len) {
		if (!nowayout) {
			size_t i;

			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = M5272_CLOSE_MAGIC;
			}
		}
		/* Refresh watchdog timer. */
		w = (unsigned short*)(MCF_MBAR + MCFSIM_WCR);
	       	*w = 0;
	}

	return len;
}

static struct watchdog_info ident = {
	.options	= WDIOF_CARDRESET | WDIOF_MAGICCLOSE |
			  WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity	= "MCF5272 Watchdog",
};

static int m5272dog_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int ret = -ENOIOCTLCMD;
	int time;
	unsigned short* w;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user((struct watchdog_info *)arg, &ident,
				   sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
		ret = put_user(0, (int *)arg);
		break;

	case WDIOC_GETBOOTSTATUS:
		ret = put_user(boot_status, (int *)arg);
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(time, (int *)arg);
		if (ret)
			break;

		if (time <= 0 || time > (32768*16384/MCF_CLK)) {
			ret = -EINVAL;
			break;
		}

		wdt_reset_ref = (MCF_CLK / 16384) * time;
		w = (unsigned short*)(MCF_MBAR + MCFSIM_WRRR);
		*w = wdt_reset_ref | 0x0001;
		/*fall through*/

	case WDIOC_GETTIMEOUT:
		ret = put_user(wdt_reset_ref * 16384 / MCF_CLK, (int *)arg);
		break;

	case WDIOC_KEEPALIVE:
		w = (unsigned short*)(MCF_MBAR + MCFSIM_WCR);
		*w = 0;
		ret = 0;
		break;
	}
	return ret;
}

static struct file_operations m5272dog_fops =
{
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= m5272dog_write,
	.ioctl		= m5272dog_ioctl,
	.open		= m5272dog_open,
	.release	= m5272dog_release,
};

static struct miscdevice m5272dog_miscdev =
{
	.minor		= WATCHDOG_MINOR,
	.name		= "MCF5272 watchdog",
	.fops		= &m5272dog_fops,
};

static int margin __initdata = 16;		/* (secs) Default is 1 minute */

static int __init m5272dog_init(void)
{
	int ret;

	/*
	 * Read the reset status, and save it for later.  If
	 * we suspend, RCSR will be cleared, and the watchdog
	 * reset reason will be lost.
	 */
//	boot_status = (RCSR & RCSR_WDR) ? WDIOF_CARDRESET : 0;
	wdt_reset_ref = (MCF_CLK / 16384) * margin;

	ret = misc_register(&m5272dog_miscdev);
	if (ret == 0)
		printk("MCF5272 Watchdog Timer: timer margin %d sec\n",
		       margin);

	return ret;
}

static void __exit m5272dog_exit(void)
{
	misc_deregister(&m5272dog_miscdev);
}

module_init(m5272dog_init);
module_exit(m5272dog_exit);

MODULE_AUTHOR("Javier Herrero <jherrero@hvsistemas.es>");
MODULE_DESCRIPTION("MCF5272 Watchdog");

module_param(margin, int, 0);
MODULE_PARM_DESC(margin, "Watchdog margin in seconds (default 16s)");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started");

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

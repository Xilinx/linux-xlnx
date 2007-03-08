/****************************************************************************/
/*
 *	SnapGear Hardware Watchdog driver (this WD cannot be stopped)
 *
 *	Copyright 2004 David McCullough <davidm@snapgear.com>, All Rights Reserved.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 *	based on softdog.c by Alan Cox <alan@redhat.com>
 */
/****************************************************************************/
 
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <asm/uaccess.h>
#include <asm/irq_regs.h>

/****************************************************************************/
/*
 * here we put the platform specific bits (headers/poke function)
 */

#ifdef CONFIG_SH_SECUREEDGE5410
	#include <asm/io.h>

	static inline void enable_dog(void) {}

	static inline void poke_the_dog(void)
	{
		volatile char dummy;
		dummy = * (volatile char *) 0xb8000000;
	}

	static inline void the_dog_is_dead(void) {}

	#define HAS_HW_SERVICE 1
#endif

#ifdef CONFIG_MACH_IPD
	#include <asm/io.h>

	static volatile char *dog_addr = NULL;

	static inline void enable_dog(void)
	{
		dog_addr = (char *) ioremap(0x20000000, 32);
	}

	static inline void poke_the_dog(void)
	{
		if (dog_addr) {
			volatile char dummy = *dog_addr;
		}
	}

	static inline void the_dog_is_dead(void) {}

	#define HAS_HW_SERVICE 1
#endif

#if defined(CONFIG_MACH_ESS710) || defined(CONFIG_MACH_IVPN) || \
    defined(CONFIG_MACH_SG560) || defined(CONFIG_MACH_SG580) || \
    defined(CONFIG_MACH_SG640) || defined(CONFIG_MACH_SG720) || \
    defined(CONFIG_MACH_SG590)
	#include <asm/io.h>

	static inline void enable_dog(void)
	{
		*IXP4XX_GPIO_GPCLKR &= 0xffff0000;
	}

	static inline void poke_the_dog(void)
	{
		*IXP4XX_GPIO_GPOUTR ^= 0x4000;
	}

	static inline void the_dog_is_dead(void) {}

	#define HAS_HW_SERVICE 1
#endif

#if defined(CONFIG_MACH_SG8100)
	#include <asm/io.h>

	static inline void enable_dog(void)
	{
	}

	static inline void poke_the_dog(void)
	{
		*IXP4XX_GPIO_GPOUTR ^= 0x2000;
	}

	static inline void the_dog_is_dead(void) {}

	#define HAS_HW_SERVICE 1
#endif

#if defined(CONFIG_MACH_SG565) || defined(CONFIG_MACH_SHIVA1100)
	#include <asm/io.h>

	static volatile unsigned char *wdtcs2;

	static inline void enable_dog(void)
	{
		/* CS7 is watchdog alive. Set it to 8bit and writable */
		*SG565_WATCHDOG_EXP_CS = 0xbfff0003;
		wdtcs2 = (volatile unsigned char *) ioremap(SG565_WATCHDOG_BASE_PHYS, 512);
	}

	static inline void poke_the_dog(void)
	{
		if (wdtcs2)
			*wdtcs2 = 0;
	}

	static inline void the_dog_is_dead(void) {}

	#define HAS_HW_SERVICE 1
#endif

#ifdef CONFIG_GEODEWATCHDOG
	#include <asm/io.h>

	static inline void enable_dog(void) {}

	static inline void poke_the_dog(void)
	{
		unsigned int v;
		v = inl(0x6410);
		outl((v | 0x200), 0x6410);
		outl((v & ~0x200), 0x6410);
	}

	static inline void the_dog_is_dead(void) {}

	#define HAS_HW_SERVICE 1
#endif

#ifndef HAS_HW_SERVICE
	static inline void enable_dog(void) {}
	static inline void poke_the_dog(void) {}
	static inline void the_dog_is_dead(void)
	{
		machine_restart(NULL);
		printk(KERN_CRIT "snapdog: reboot failed!.\n");
	}
#endif

/****************************************************************************/

static unsigned long snapdog_last = 0;
static unsigned long snapdog_next = 0;
static int           snapdog_service_required = 0;
static unsigned long snapdog_busy = 0;
static int           snapdog_kernel = 0;
static int           snapdog_timeout = 60;
static int           snapdog_ltimeout = 300;
static int           snapdog_use_long_timeout = 0;
static int           snapdog_quiet = 0;
static int           snapdog_warned = 0;
static int           snapdog_stackdump = 64;

module_param(snapdog_kernel, int, 0);
MODULE_PARM_DESC(snapdog_kernel,
		"Watchdog is kernel only (userland servicing not required)");

module_param(snapdog_timeout, int, 0);
MODULE_PARM_DESC(snapdog_timeout,
		"Watchdog timeout for user service in seconds");

module_param(snapdog_ltimeout, int, 0);
MODULE_PARM_DESC(snapdog_ltimeout,
		"Watchdog 'long' timeout for user service in seconds");

module_param(snapdog_stackdump, int, 0);
MODULE_PARM_DESC(snapdog_stackdump,
		"Number of long words to dump from the stack");

/****************************************************************************/
/*
 * a really dumb stack dump,  we may need better on some platforms
 * at least this one is implemented,  unlike dump_stack which is largely
 * just a stub :-(
 */

static void snapdog_show_stack(struct pt_regs *regs)
{
	unsigned long i;
	unsigned long *addr = &i;

	printk("Kernel stack:");
	for (i = 0; i < snapdog_stackdump; i++) {
		if (i % 4 == 0)
			printk("\n%08lx:", (unsigned long) addr);
		printk(" 0x%08lx", *addr++);
	}
	printk("\n");
}

/****************************************************************************/
/*
 *	Because we need to service this guy from deep in other more critical
 *	code,  we export a function to do this that we can call where
 *	appropriate.
 *
 *	Also the watchdog never stops,  it is always running.  We must service
 *	it until we are opened,  then we stop servicing it if we are not looked
 *	after appropriately.
 *
 *	I know there are much more clever ways to code the following,  but then
 *	who would understand it,  let alone know it did the right thing when
 *	jiffies wraps ;-)
 */

void
snapdog_service(void)
{
	struct pt_regs *regs;
	int the_dog_is_alive = 0;

	if (snapdog_kernel) {
		the_dog_is_alive = 1;
	} else if (!snapdog_service_required) {
		the_dog_is_alive = 1;
	} else if (snapdog_next < snapdog_last) {
		if (jiffies < snapdog_next || jiffies > snapdog_last)
			the_dog_is_alive = 1;
	} else if (jiffies >= snapdog_last && jiffies < snapdog_next) {
		the_dog_is_alive = 1;
	}

	if (the_dog_is_alive)
		poke_the_dog();
	else if (!snapdog_warned) {
		snapdog_warned = 1;
		printk(KERN_CRIT "snapdog: expired, allowing system reboot.\n");
		regs = get_irq_regs();
		if (regs) {
			show_regs(regs);
			snapdog_show_stack(regs);
		}
		the_dog_is_dead();
	}
}

EXPORT_SYMBOL(snapdog_service);

/****************************************************************************/
/*
 * bump the userland expiry
 */

static inline void
snapdog_user_service(void)
{
	snapdog_last = jiffies;
	if (snapdog_use_long_timeout)
		snapdog_next = snapdog_last + HZ * snapdog_ltimeout;
	else
		snapdog_next = snapdog_last + HZ * snapdog_timeout;
	snapdog_warned = 0;
}

/****************************************************************************/
/*
 *	Allow only one person to hold it open
 */

static int
snapdog_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &snapdog_busy))
		return -EBUSY;

	/* Activate timer */
	snapdog_service_required = 1;
	if (snapdog_use_long_timeout) {
		/* Opening reverts to using short timeouts */
		snapdog_use_long_timeout = 0;

		if (!snapdog_quiet) {
			printk(KERN_INFO "snapdog: now using short timeouts.\n");
		}
	}
	snapdog_user_service();

	if (!snapdog_quiet) {
		printk(KERN_INFO "snapdog: user servicing enabled (short=%d,long=%d).\n",
				snapdog_timeout, snapdog_ltimeout);
	}


	/* Opening turns off quiet mode */
	snapdog_quiet = 0;

	
	return 0;
}

/****************************************************************************/

static int
snapdog_release(struct inode *inode, struct file *file)
{
	lock_kernel();
	if (!snapdog_quiet) {
		if (!snapdog_service_required) {
			printk(KERN_INFO
					"snapdog: disabled user servicing of watchdog timer.\n");
		} else if (snapdog_use_long_timeout) {
			printk(KERN_CRIT
					"snapdog: device closed, watchdog will reboot!\n");
		}
	}
	clear_bit(0, &snapdog_busy);
	unlock_kernel();

	return 0;
}

/****************************************************************************/

static ssize_t
snapdog_write(struct file *file, const char __user *data, size_t len, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (*ppos != file->f_pos)
		return -ESPIPE;

	/*
	 *	Refresh the timer.
	 */
	if (len) {
		size_t i;

		for (i = 0; i != len; i++) {
			char c;
			if (get_user(c, data + i))
				return -EFAULT;
			if (c == 'V') {
				snapdog_service_required = 0;
			}
			else if (c == 'T') {
				if (!snapdog_quiet) {
					printk(KERN_INFO "snapdog: now using long timeouts.\n");
				}
				snapdog_use_long_timeout = 1;
			}
			else if (c == 'Q') {
				/* Go quiet */
				snapdog_quiet = 1;
			}
		}
		snapdog_user_service();
		return 1;
	}
	return 0;
}

/****************************************************************************/

static int
snapdog_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	static struct watchdog_info ident = {
		.options = WDIOF_MAGICCLOSE,
		.identity = "HW/SW Watchdog for SnapGear",
	};

	switch (cmd) {
	default:
		return(-ENOIOCTLCMD);

	case WDIOC_GETSUPPORT:
		if (copy_to_user((struct watchdog_info __user *) arg, &ident, sizeof(ident)))
			return -EFAULT;
		return(0);

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return(put_user(0, (int __user *) arg));

	case WDIOC_KEEPALIVE:
		snapdog_user_service();
		return(0);
	}
}

/****************************************************************************/

static struct file_operations snapdog_fops = {
	.owner		= THIS_MODULE,
	.write		= snapdog_write,
	.ioctl		= snapdog_ioctl,
	.open		= snapdog_open,
	.release	= snapdog_release,
};


static struct miscdevice snapdog_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &snapdog_fops,
};

/****************************************************************************/

static const char banner[] __initdata =
	KERN_INFO "snapdog: HW/SW watchdog timer for SnapGear/Others\n";

static int __init
watchdog_init(void)
{
	int ret;

	enable_dog();

       	ret = misc_register(&snapdog_miscdev);
	if (ret)
		return ret;

	printk(banner);

	return 0;
}

/****************************************************************************/

static void __exit
watchdog_exit(void)
{
	misc_deregister(&snapdog_miscdev);
}

/****************************************************************************/

module_init(watchdog_init);
module_exit(watchdog_exit);
MODULE_AUTHOR("David McCullough <davidm@snapgear.com>");
MODULE_DESCRIPTION("Driver for SnapGear HW/SW watchdog timer(s)");
MODULE_LICENSE("GPL");

/****************************************************************************/

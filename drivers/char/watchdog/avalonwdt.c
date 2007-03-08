/*
 *	WDT driver for the Nios2
 *
 *	(c) Copyright 2005 Walter Goossens <walter.goossens@emdes.nl>
 *	Neither Walter Goossens nor Emdes Embedded Systems admit liability 
 *  nor provide warranty for any of this software. This material is 
 *  provided "AS-IS" and at no charge.
 *
 *	Based on wdt.c.
 *	Original copyright messages:
 *
 *	(c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *				http://www.redhat.com
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
 *	(c) Copyright 1995    Alan Cox <alan@redhat.com>
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

// Change this line if you used another name in your PTF file.

#define AVALON_WDT_BASE		na_watchdog
#define AVALON_WDT_STATUS	AVALON_WDT_BASE
#define AVALON_WDT_CONTROL	(AVALON_WDT_BASE + 0x04)
#define AVALON_WDT_PERIODL	(AVALON_WDT_BASE + 0x08)
#define AVALON_WDT_PERIODH	(AVALON_WDT_BASE + 0x0C)
#define AVALON_WDT_SIZE		0x18

#define AVALON_WDT_RUN_BIT	0x04

static unsigned long wdt_is_open;

/**
 *	avalon_wdt_start:
 *
 *	Start the watchdog driver.
 */
static int avalon_wdt_start(void) {
	outw_p(inw_p(AVALON_WDT_CONTROL) | AVALON_WDT_RUN_BIT, AVALON_WDT_CONTROL);
	printk(KERN_INFO "avalonwdt: Starting watchdog timer\n");
	return 0;
}

/**
 *	avalon_wdt_ping:
 *
 *	Reload counter one with the watchdog heartbeat.
 */
static int avalon_wdt_ping(void) {
	//It doesn't matter what value we write
	outw_p(1,AVALON_WDT_PERIODL);
	return 0;
}

/**
 *	avalon_wdt_write:
 *	@file: file handle to the watchdog
 *	@buf: buffer to write (unused as data does not matter here
 *	@count: count of bytes
 *	@ppos: pointer to the position to write. No seeks allowed
 *
 *	A write to a watchdog device is defined as a keepalive signal. Any
 *	write of data will do, as we we don't define content meaning.
 */

static ssize_t avalon_wdt_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
	if(count) {
		avalon_wdt_ping();
	}
	return count;
}

/**
 *	avalon_wdt_ioctl:
 *	@inode: inode of the device
 *	@file: file handle to the device
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The watchdog API defines a common set of functions for all watchdogs
 *	according to their available features. We only actually usefully support
 *	querying capabilities and current status.
 */

static int avalon_wdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg) {
	void __user *argp = (void __user *)arg;

	static struct watchdog_info ident = {
		.options =	WDIOF_KEEPALIVEPING,
		.firmware_version =	1,
		.identity =		"Nios2_avalon_wdt",
	};

	switch(cmd) {
		case WDIOC_GETSUPPORT:
			return copy_to_user(argp, &ident, sizeof(ident))?-EFAULT:0;
		case WDIOC_KEEPALIVE:
			avalon_wdt_ping();
			return 0;
		default:
			return -ENOIOCTLCMD;
	}
}

/**
 *	avalon_wdt_open:
 *	@inode: inode of device
 *	@file: file handle to device
 *
 *	The watchdog device has been opened. The watchdog device is single
 *	open and on opening we load the counters. 
 *  The timeout depends on the value you selected in SOPC-builder.
 */
static int avalon_wdt_open(struct inode *inode, struct file *file) {
	if(test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;
	avalon_wdt_start();
	return nonseekable_open(inode, file);
}

/**
 *	avalon_wdt_release:
 *	@inode: inode to board
 *	@file: file handle to board
 *
 */
static int avalon_wdt_release(struct inode *inode, struct file *file) {
	clear_bit(0, &wdt_is_open);
	printk(KERN_CRIT "avalonwdt: WDT device closed unexpectedly.  WDT will (can) not stop!\n");
	avalon_wdt_ping();
	return 0;
}

/*
 *	Kernel Interfaces
 */

static struct file_operations avalon_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= avalon_wdt_write,
	.ioctl		= avalon_wdt_ioctl,
	.open		= avalon_wdt_open,
	.release	= avalon_wdt_release,
};

static struct miscdevice avalon_wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &avalon_wdt_fops,
};

/**
 *	cleanup_module:
 *
 *	Unload the watchdog. You cannot do this with any file handles open.
 *	If your watchdog is set to continue ticking on close and you unload
 *	it, well it keeps ticking. We won't get the interrupt but the board
 *	will not touch PC memory so all is fine. You just have to load a new
 *	module in 30 seconds or reboot.
 */

static void __exit avalon_wdt_exit(void)
{
	misc_deregister(&avalon_wdt_miscdev);
	release_region(AVALON_WDT_BASE,AVALON_WDT_SIZE);
}

/**
 * 	avalon_wdt_init:
 *
 *	Set up the WDT watchdog board. All we have to do is grab the
 *	resources we require and bitch if anyone beat us to them.
 *	The open() function will actually kick the board off.
 */

static int __init avalon_wdt_init(void)
{
	int ret;

	if (!request_region(AVALON_WDT_BASE, AVALON_WDT_SIZE, "Nios2_avalon_wdt")) {
		printk(KERN_ERR "wdt: I/O address 0x%08x already in use\n", AVALON_WDT_BASE);
		return -EBUSY;
	}
	ret = misc_register(&avalon_wdt_miscdev);
	if (ret) {
		printk(KERN_ERR "wdt: cannot register miscdev on minor=%d (err=%d)\n", WATCHDOG_MINOR, ret);
		release_region(AVALON_WDT_BASE,AVALON_WDT_SIZE);
		return ret;
	}
	
	printk(KERN_INFO "Nios2 Avalon Watchdog driver 0.01 at 0x%08x\n", AVALON_WDT_BASE);
	return 0;
}

module_init(avalon_wdt_init);
module_exit(avalon_wdt_exit);

MODULE_AUTHOR("Walter Goossens");
MODULE_DESCRIPTION("Driver for Nios2 Watchdog");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_LICENSE("GPL");

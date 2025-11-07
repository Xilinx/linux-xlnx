// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#include <linux/liveupdate.h>
#include <linux/miscdevice.h>

struct luo_device_state {
	struct miscdevice miscdev;
};

static const struct file_operations luo_fops = {
	.owner		= THIS_MODULE,
};

static struct luo_device_state luo_dev = {
	.miscdev = {
		.minor = MISC_DYNAMIC_MINOR,
		.name  = "liveupdate",
		.fops  = &luo_fops,
	},
};

static int __init liveupdate_ioctl_init(void)
{
	if (!liveupdate_enabled())
		return 0;

	return misc_register(&luo_dev.miscdev);
}
module_init(liveupdate_ioctl_init);

static void __exit liveupdate_exit(void)
{
	misc_deregister(&luo_dev.miscdev);
}
module_exit(liveupdate_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pasha Tatashin");
MODULE_DESCRIPTION("Live Update Orchestrator");
MODULE_VERSION("0.1");

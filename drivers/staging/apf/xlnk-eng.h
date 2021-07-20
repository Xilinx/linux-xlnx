/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx XLNK Engine Driver
 *
 * Copyright (C) 2010 Xilinx, Inc. All rights reserved.
 */

#ifndef XLNK_ENG_H
#define XLNK_ENG_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/spinlock_types.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/string.h>

struct xlnk_eng_device {
	struct list_head global_node;
	struct xlnk_eng_device * (*alloc)(struct xlnk_eng_device *xdev);
	void (*free)(struct xlnk_eng_device *xdev);
	struct device *dev;
};
extern int xlnk_eng_register_device(struct xlnk_eng_device *xlnk_dev);
extern void xlnk_eng_unregister_device(struct xlnk_eng_device *xlnk_dev);
extern struct xlnk_eng_device *xlnk_eng_request_by_name(char *name);

#endif


// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Vasileios Bimpikas <vasileios.bimpikas@xilinx.com>
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "roe_radio_ctrl.h"
#include "xroe-traffic-gen.h"

static int xroe_size;
static char xroe_tmp[XROE_SIZE_MAX];

/**
 * utils_sysfs_store_wrapper - Wraps the storing function for sysfs entries
 * @dev:	The structure containing the device's information
 * @address:	The address of the register to be written
 * @offset:	The offset from the address of the register
 * @mask:	The mask to be used on the value to be written
 * @value:	The value to be written to the register
 *
 * Wraps the core functionality of all "store" functions of sysfs entries.
 */
static void utils_sysfs_store_wrapper(struct device *dev, u32 address,
				      u32 offset, u32 mask, u32 value)
{
	void __iomem *working_address;
	u32 read_register_value = 0;
	u32 register_value_to_write = 0;
	u32 delta = 0;
	u32 buffer = 0;
	struct xroe_traffic_gen_local *lp = dev_get_drvdata(dev);

	working_address = (void __iomem *)(lp->base_addr + address);
	read_register_value = ioread32(working_address);
	buffer = (value << offset);
	register_value_to_write = read_register_value & ~mask;
	delta = buffer & mask;
	register_value_to_write |= delta;
	iowrite32(register_value_to_write, working_address);
}

/**
 * utils_sysfs_show_wrapper - Wraps the "show" function for sysfs entries
 * @dev:	The structure containing the device's information
 * @address:	The address of the register to be read
 * @offset:	The offset from the address of the register
 * @mask:	The mask to be used on the value to be read
 *
 * Wraps the core functionality of all "show" functions of sysfs entries.
 *
 * Return: The value designated by the address, offset and mask
 */
static u32 utils_sysfs_show_wrapper(struct device *dev, u32 address, u32 offset,
				    u32 mask)
{
	void __iomem *working_address;
	u32 buffer;
	struct xroe_traffic_gen_local *lp = dev_get_drvdata(dev);

	working_address = (void __iomem *)(lp->base_addr + address);
	buffer = ioread32(working_address);
	return (buffer & mask) >> offset;
}

/**
 * radio_id_show - Returns the block's ID number
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the ID number string
 *
 * Returns the traffic gen's ID (0x1179649 by default)
 *
 * Return: The number of characters printed on success
 */
static ssize_t radio_id_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	u32 radio_id;

	radio_id = utils_sysfs_show_wrapper(dev, RADIO_ID_ADDR,
					    RADIO_ID_OFFSET,
					    RADIO_ID_MASK);
	return sprintf(buf, "%d\n", radio_id);
}
static DEVICE_ATTR_RO(radio_id);

/**
 * timeout_enable_show - Returns the traffic gen's timeout enable status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 *
 * Reads and writes the traffic gen's timeout enable status to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t timeout_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	u32 timeout_enable;

	timeout_enable = utils_sysfs_show_wrapper(dev,
						  RADIO_TIMEOUT_ENABLE_ADDR,
						  RADIO_TIMEOUT_ENABLE_OFFSET,
						  RADIO_TIMEOUT_ENABLE_MASK);
	if (timeout_enable)
		return sprintf(buf, "true\n");
	else
		return sprintf(buf, "false\n");
}

/**
 * timeout_enable_store - Writes to the traffic gens's timeout enable
 * status register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's timeout enable
 * status to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t timeout_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	u32 enable = 0;

	strncpy(xroe_tmp, buf, xroe_size);
	if (strncmp(xroe_tmp, "true", xroe_size) == 0)
		enable = 1;
	else if (strncmp(xroe_tmp, "false", xroe_size) == 0)
		enable = 0;
	utils_sysfs_store_wrapper(dev, RADIO_TIMEOUT_ENABLE_ADDR,
				  RADIO_TIMEOUT_ENABLE_OFFSET,
				  RADIO_TIMEOUT_ENABLE_MASK, enable);
	return count;
}
static DEVICE_ATTR_RW(timeout_enable);

static struct attribute *xroe_traffic_gen_attrs[] = {
	&dev_attr_radio_id.attr,
	&dev_attr_timeout_enable.attr,
	NULL,
};
ATTRIBUTE_GROUPS(xroe_traffic_gen);

/**
 * xroe_traffic_gen_sysfs_init - Creates the xroe sysfs directory and entries
 * @dev:	The device's structure
 *
 * Return: 0 on success, negative value in case of failure to
 * create the sysfs group
 *
 * Creates the xroetrafficgen sysfs directory and entries
 */
int xroe_traffic_gen_sysfs_init(struct device *dev)
{
	int ret;

	dev->groups = xroe_traffic_gen_groups;
	ret = sysfs_create_group(&dev->kobj, *xroe_traffic_gen_groups);
	if (ret)
		dev_err(dev, "sysfs creation failed\n");

	return ret;
}

/**
 * xroe_traffic_gen_sysfs_exit - Deletes the xroe sysfs directory and entries
 * @dev:	The device's structure
 *
 * Deletes the xroetrafficgen sysfs directory and entries
 */
void xroe_traffic_gen_sysfs_exit(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, *xroe_traffic_gen_groups);
}

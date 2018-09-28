// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Vasileios Bimpikas <vasileios.bimpikas@xilinx.com>
 */

#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "xroe_framer.h"

enum { XROE_SIZE_MAX = 15 };
static int xroe_size;

/**
 * udp_source_port_show - Returns the UDP source port
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the UDP source port
 *
 * Returns the UDP source port
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t udp_source_port_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buff)
{
	u32 offset = ETH_UDP_SOURCE_PORT_OFFSET;
	u32 mask = ETH_UDP_SOURCE_PORT_MASK;
	u32 buffer = 0;
	u32 source_port = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_UDP_SOURCE_PORT_ADDR);

	buffer = ioread32(working_address);
	source_port = (buffer & mask) >> offset;

	sprintf(buff, "%d\n", source_port);

	return XROE_SIZE_MAX;
}

/**
 * udp_source_port_store - Writes to the UDP source port sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the UDP source port
 * @count:	The number of characters typed by the user
 *
 * Writes to the UDP source port sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t udp_source_port_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buff, size_t count)
{
	int ret;
	u32 offset = ETH_UDP_SOURCE_PORT_OFFSET;
	u32 mask = ETH_UDP_SOURCE_PORT_MASK;
	unsigned int source_port = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_UDP_SOURCE_PORT_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = 0;
	ret = kstrtouint(buff, 10, &source_port);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)source_port, mask, offset);
	return xroe_size;
}

/**
 * udp_destination_port_show - Returns the UDP destination port
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the UDP destination port
 *
 * Returns the UDP destination port
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t udp_destination_port_show(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 char *buff)
{
	u32 offset = ETH_UDP_DESTINATION_PORT_OFFSET;
	unsigned long mask = ETH_UDP_DESTINATION_PORT_MASK;
	u32 buffer = 0;
	u32 dest_port = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_UDP_DESTINATION_PORT_ADDR);

	buffer = ioread32(working_address);
	dest_port = (buffer & mask) >> offset;

	sprintf(buff, "%d\n", dest_port);

	return XROE_SIZE_MAX;
}

/**
 * udp_destination_port_store - Writes to the UDP destination port sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the UDP destination port
 * @count:	The number of characters typed by the user
 *
 * Writes to the UDP destination port sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t udp_destination_port_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buff, size_t count)
{
	int ret;
	u32 offset = ETH_UDP_DESTINATION_PORT_OFFSET;
	unsigned long mask = ETH_UDP_DESTINATION_PORT_MASK;
	unsigned int dest_port = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_UDP_DESTINATION_PORT_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = 0;
	ret = kstrtouint(buff, 10, &dest_port);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)dest_port,
			      mask, offset);
	return xroe_size;
}

/* TODO Use DEVICE_ATTR/_RW/_RO macros */

static struct kobj_attribute source_port =
	__ATTR(source_port, 0660, udp_source_port_show,
	       udp_source_port_store);
static struct kobj_attribute dest_port =
	__ATTR(dest_port, 0660, udp_destination_port_show,
	       udp_destination_port_store);

static struct attribute *attrs[] = {
	&source_port.attr,
	&dest_port.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *kobj_udp;

/**
 * xroe_sysfs_udp_init - Creates the xroe sysfs "udp" subdirectory and entries
 *
 * Return: 0 on success, negative value in case of failure to
 * create the sysfs group
 *
 * Creates the xroe sysfs "udp" subdirectory and entries under "xroe"
 */
int xroe_sysfs_udp_init(void)
{
	int ret;

	kobj_udp = kobject_create_and_add("udp", kobj_framer);
	if (!kobj_udp)
		return -ENOMEM;
	ret = sysfs_create_group(kobj_udp, &attr_group);
	if (ret)
		kobject_put(kobj_udp);
	return ret;
}

/**
 * xroe_sysfs_ipv6_exit - Deletes the xroe sysfs "udp" subdirectory & entries
 *
 * Deletes the xroe sysfs "udp" subdirectory and entries,
 * under the "xroe" entry
 *
 */
void xroe_sysfs_udp_exit(void)
{
	kobject_put(kobj_udp);
}

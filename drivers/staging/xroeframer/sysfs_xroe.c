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
static char xroe_tmp[XROE_SIZE_MAX];

/**
 * version_show - Returns the block's revision number
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the revision string
 *
 * Returns the block's major, minor & version revision numbers
 * in a %d.%d.%d format
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buff)
{
	u32 major_rev;
	u32 minor_rev;
	u32 version_rev;

	major_rev = utils_sysfs_show_wrapper(CFG_MAJOR_REVISION_ADDR,
					     CFG_MAJOR_REVISION_OFFSET,
					     CFG_MAJOR_REVISION_MASK, kobj);
	minor_rev = utils_sysfs_show_wrapper(CFG_MINOR_REVISION_ADDR,
					     CFG_MINOR_REVISION_OFFSET,
					     CFG_MINOR_REVISION_MASK, kobj);
	version_rev = utils_sysfs_show_wrapper(CFG_VERSION_REVISION_ADDR,
					       CFG_VERSION_REVISION_OFFSET,
					       CFG_VERSION_REVISION_MASK, kobj);
	sprintf(buff, "%d.%d.%d\n", major_rev, minor_rev, version_rev);
	return XROE_SIZE_MAX;
}

/**
 * version_store - Writes to the framer version sysfs entry (not permitted)
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the revision string
 * @count:	The number of characters typed by the user
 *
 * Writes to the framer version sysfs entry (not permitted)
 *
 * Return: 0
 */
static ssize_t version_store(struct  kobject *kobj, struct kobj_attribute *attr,
			     const char *buff, size_t count)
{
	return 0;
}

/**
 * enable_show - Returns the framer's enable status
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the enable status
 *
 * Reads and writes the framer's enable status to the sysfs entry
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buff)
{
	u32 enable;

	enable = utils_sysfs_show_wrapper(CFG_MASTER_INT_ENABLE_ADDR,
					  CFG_MASTER_INT_ENABLE_OFFSET,
					  CFG_MASTER_INT_ENABLE_MASK, kobj);
	if (enable)
		sprintf(buff, "true\n");
	else
		sprintf(buff, "false\n");
	return XROE_SIZE_MAX;
}

/**
 * version_store - Writes to the framer's enable status register
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the enable status
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the framer's enable status
 * to the sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buff, size_t count)
{
	u32 enable = 0;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (strncmp(xroe_tmp, "true", xroe_size) == 0)
		enable = 1;
	else if (strncmp(xroe_tmp, "false", xroe_size) == 0)
		enable = 0;
	utils_sysfs_store_wrapper(CFG_MASTER_INT_ENABLE_ADDR,
				  CFG_MASTER_INT_ENABLE_OFFSET,
				  CFG_MASTER_INT_ENABLE_MASK, enable, kobj);
	return xroe_size;
}

/**
 * framer_restart_show - Returns the framer's restart status
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the restart status
 *
 * Reads and writes the framer's restart status to the sysfs entry
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t framer_restart_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	u32 restart;

	restart = utils_sysfs_show_wrapper(FRAM_DISABLE_ADDR,
					   FRAM_DISABLE_OFFSET,
					   FRAM_DISABLE_MASK, kobj);
	if (restart)
		sprintf(buff, "true\n");

	else
		sprintf(buff, "false\n");

	return XROE_SIZE_MAX;
}

/**
 * framer_restart_store - Writes to the framer's restart status register
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the restart status
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the framer's restart status
 * to the sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t framer_restart_store(struct  kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buff, size_t count)
{
	u32 restart = 0;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (strncmp(xroe_tmp, "true", xroe_size) == 0)
		restart = 0x01;
	else if (strncmp(xroe_tmp, "false", xroe_size) == 0)
		restart = 0x00;
	utils_sysfs_store_wrapper(FRAM_DISABLE_ADDR, FRAM_DISABLE_OFFSET,
				  FRAM_DISABLE_MASK, restart, kobj);
	return xroe_size;
}

/**
 * deframer_restart_show - Returns the deframer's restart status
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the restart status
 *
 * Reads and writes the deframer's restart status to the sysfs entry
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t deframer_restart_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buff)
{
	u32 offset = DEFM_RESTART_OFFSET;
	u32 mask = DEFM_RESTART_MASK;
	u32 buffer = 0;
	u32 restart = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ DEFM_RESTART_ADDR);

	buffer = ioread32(working_address);
	restart = (buffer & mask) >> offset;

	if (restart)
		sprintf(buff, "true\n");

	else
		sprintf(buff, "false\n");

	return XROE_SIZE_MAX;
}

/**
 * deframer_restart_store - Writes to the deframer's restart status register
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the restart status
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the deframer's restart status
 * to the sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t deframer_restart_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buff, size_t count)
{
	u32 offset = DEFM_RESTART_OFFSET;
	u32 mask = DEFM_RESTART_MASK;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ DEFM_RESTART_ADDR);
	u32 restart = 0;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (strncmp(xroe_tmp, "true", xroe_size) == 0) {
		restart = 0x01;
		utils_write32withmask(working_address, restart,
				      mask, offset);
	} else if (strncmp(xroe_tmp, "false", xroe_size) == 0) {
		restart = 0x00;
		utils_write32withmask(working_address, restart,
				      mask, offset);
	}

	return xroe_size;
}

/**
 * xxv_reset_show - Returns the XXV's reset status
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the reset status
 *
 * Reads and writes the XXV's reset status to the sysfs entry
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t xxv_reset_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buff)
{
	u32 offset = CFG_USER_RW_OUT_OFFSET;
	u32 mask = CFG_USER_RW_OUT_MASK;
	u32 buffer = 0;
	u32 restart = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	CFG_USER_RW_OUT_ADDR);

	buffer = ioread32(working_address);
	restart = (buffer & mask) >> offset;
	if (restart)
		sprintf(buff, "true\n");
	else
		sprintf(buff, "false\n");
	return XROE_SIZE_MAX;
}

/**
 * xxv_reset_store - Writes to the XXV's reset register
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the reset status
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the XXV's reset status
 * to the sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t xxv_reset_store(struct  kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buff, size_t count)
{
	u32 offset = CFG_USER_RW_OUT_OFFSET;
	u32 mask = CFG_USER_RW_OUT_MASK;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	CFG_USER_RW_OUT_ADDR);
	u32 restart = 0;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);

	if (strncmp(xroe_tmp, "true", xroe_size) == 0) {
		restart = 0x01;
		utils_write32withmask(working_address, restart,
				      mask, offset);
	} else if (strncmp(xroe_tmp, "false", xroe_size) == 0) {
		restart = 0x00;
		utils_write32withmask(working_address, restart,
				      mask, offset);
	}
	return xroe_size;
}

/**
 * framing_show - Returns the current framing
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the reset status
 *
 * Reads and writes the current framing type to the sysfs entry
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t framing_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buff)
{
	u32 offset = (DEFM_DATA_PKT_MESSAGE_TYPE_ADDR +
	DEFM_DATA_PKT_MESSAGE_TYPE_OFFSET);
	u8 buffer = 0;
	u8 framing = 0xff;
	void __iomem *working_address = ((u8 *)lp->base_addr + offset);

	buffer = ioread8(working_address);
	framing = buffer;
	if (framing == 0)
		sprintf(buff, "eCPRI\n");
	else if (framing == 1)
		sprintf(buff, "1914.3\n");
	return XROE_SIZE_MAX;
}

/**
 * framing_store - Writes to the current framing register
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the reset status
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the current framing
 * to the sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t framing_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buff, size_t count)
{
	u32 offset = (DEFM_DATA_PKT_MESSAGE_TYPE_ADDR +
	DEFM_DATA_PKT_MESSAGE_TYPE_OFFSET);
	void __iomem *working_address = ((u8 *)lp->base_addr + offset);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (strncmp(xroe_tmp, "eCPRI", xroe_size) == 0)
		iowrite8(0, working_address);
	else if (strncmp(xroe_tmp, "1914.3", xroe_size) == 0)
		iowrite8(1, working_address);
	return xroe_size;
}

/* TODO Use DEVICE_ATTR/_RW/_RO macros */

static struct kobj_attribute version_attribute =
	__ATTR(version, 0444, version_show, version_store);

static struct kobj_attribute enable_attribute =
	__ATTR(enable, 0660, enable_show, enable_store);

static struct kobj_attribute framer_restart =
	__ATTR(framer_restart, 0660, framer_restart_show, framer_restart_store);

static struct kobj_attribute deframer_restart =
	__ATTR(deframer_restart, 0660, deframer_restart_show,
	       deframer_restart_store);

static struct kobj_attribute xxv_reset =
	__ATTR(xxv_reset, 0660, xxv_reset_show, xxv_reset_store);

static struct kobj_attribute framing_attribute =
	__ATTR(framing, 0660, framing_show, framing_store);

static struct attribute *attrs[] = {
	&version_attribute.attr,
	&enable_attribute.attr,
	&framer_restart.attr,
	&deframer_restart.attr,
	&xxv_reset.attr,
	&framing_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

struct kobject *root_xroe_kobj;

/**
 * xroe_sysfs_init - Creates the xroe sysfs directory and entries
 *
 * Return: 0 on success, negative value in case of failure to
 * create the sysfs group
 *
 * Creates the xroe sysfs directory and entries, as well as the
 * subdirectories for IPv4, IPv6 & UDP
 */
int xroe_sysfs_init(void)
{
	int ret;

	root_xroe_kobj = kobject_create_and_add("xroe", kernel_kobj);
	if (!root_xroe_kobj)
		return -ENOMEM;
	ret = sysfs_create_group(root_xroe_kobj, &attr_group);
	if (ret)
		kobject_put(root_xroe_kobj);
	ret = xroe_sysfs_ipv4_init();
	if (ret)
		return ret;
	ret = xroe_sysfs_ipv6_init();
	if (ret)
		return ret;
	ret = xroe_sysfs_udp_init();
	if (ret)
		return ret;
	ret = xroe_sysfs_stats_init();
	return ret;
}

/**
 * xroe_sysfs_exit - Deletes the xroe sysfs directory and entries
 *
 * Deletes the xroe sysfs directory and entries, as well as the
 * subdirectories for IPv4, IPv6 & UDP
 *
 */
void xroe_sysfs_exit(void)
{
	int i;

	xroe_sysfs_ipv4_exit();
	xroe_sysfs_ipv6_exit();
	xroe_sysfs_udp_exit();
	xroe_sysfs_stats_exit();
	for (i = 0; i < MAX_NUM_ETH_PORTS; i++)
		kobject_put(kobj_eth_ports[i]);
	kobject_put(kobj_framer);
	kobject_put(root_xroe_kobj);
}

/**
 * utils_write32withmask - Writes a masked 32-bit value
 * @working_address:	The starting address to write
 * @value:			The value to be written
 * @mask:			The mask to be used
 * @offset:			The offset from the provided starting address
 *
 * Writes a 32-bit value to the provided address with the input mask
 *
 * Return: 0 on success
 */
int utils_write32withmask(void __iomem *working_address, u32 value,
			  u32 mask, u32 offset)
{
	u32 read_register_value = 0;
	u32 register_value_to_write = 0;
	u32 delta = 0, buffer = 0;

	read_register_value = ioread32(working_address);
	buffer = (value << offset);
	register_value_to_write = read_register_value & ~mask;
	delta = buffer & mask;
	register_value_to_write |= delta;
	iowrite32(register_value_to_write, working_address);
	return 0;
}

/**
 * utils_sysfs_path_to_eth_port_num - Get the current ethernet port
 * @kobj:	The kobject of the entry calling the function
 *
 * Extracts the number of the current ethernet port instance
 *
 * Return: The number of the ethernet port instance (0 - MAX_NUM_ETH_PORTS) on
 * success, -1 otherwise
 */
static int utils_sysfs_path_to_eth_port_num(struct kobject *kobj)
{
	char *current_path = NULL;
	int port;
	int ret;

	current_path = kobject_get_path(kobj, GFP_KERNEL);
	ret = sscanf(current_path, "/kernel/xroe/framer/eth_port_%d/", &port);
	/* if sscanf() returns 0, no fields were assigned, therefore no
	 * adjustments will be made for port number
	 */
	if (ret == 0)
		port = 0;
//	printk(KERN_ALERT "current_path: %s port: %d\n", current_path, port);
	kfree(current_path);
	return port;
}

/**
 * utils_sysfs_store_wrapper - Wraps the storing function for sysfs entries
 * @address:	The address of the register to be written
 * @offset:	The offset from the address of the register
 * @mask:	The mask to be used on the value to be written
 * @value:	The value to be written to the register
 * @kobj:	The kobject of the entry calling the function
 *
 * Wraps the core functionality of all "store" functions of sysfs entries.
 * After calculating the ethernet port number (in N/A cases, it's 0), the value
 * is written to the designated register
 *
 */
void utils_sysfs_store_wrapper(u32 address, u32 offset, u32 mask, u32 value,
			       struct kobject *kobj)
{
	int port;
	void __iomem *working_address;

	port = utils_sysfs_path_to_eth_port_num(kobj);
	working_address = (void __iomem *)(lp->base_addr +
			  (address + (0x100 * port)));
	utils_write32withmask(working_address, value, mask, offset);
}

/**
 * utils_sysfs_store_wrapper - Wraps the storing function for sysfs entries
 * @address:	The address of the register to be read
 * @offset:	The offset from the address of the register
 * @mask:	The mask to be used on the value to be read
 * @kobj:	The kobject of the entry calling the function
 *
 * Wraps the core functionality of all "show" functions of sysfs entries.
 * After calculating the ethernet port number (in N/A cases, it's 0), the value
 * is read from the designated register and returned.
 *
 * Return: The value designated by the address, offset and mask
 */
u32 utils_sysfs_show_wrapper(u32 address, u32 offset, u32 mask,
			     struct kobject *kobj)
{
	int port;
	void __iomem *working_address;
	u32 buffer;

	port = utils_sysfs_path_to_eth_port_num(kobj);
	working_address = (void __iomem *)(lp->base_addr +
			  (address + (0x100 * port)));
	buffer = ioread32(working_address);
	return (buffer & mask) >> offset;
}

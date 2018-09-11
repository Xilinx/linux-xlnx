// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Xilinx, Inc.
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

/**
 * total_rx_good_pkt_show - Returns the total good rx packet count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total good rx packet count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_good_pkt_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_TOTAL_RX_GOOD_PKT_CNT_ADDR,
					 STATS_TOTAL_RX_GOOD_PKT_CNT_OFFSET,
					 STATS_TOTAL_RX_GOOD_PKT_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_bad_pkt_show - Returns the total bad rx packet count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total bad rx packet count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_bad_pkt_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_TOTAL_RX_BAD_PKT_CNT_ADDR,
					 STATS_TOTAL_RX_BAD_PKT_CNT_OFFSET,
					 STATS_TOTAL_RX_BAD_PKT_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_bad_fcs_show - Returns the total bad fcs count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total bad frame check sequences count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_bad_fcs_show(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_TOTAL_RX_BAD_FCS_CNT_ADDR,
					 STATS_TOTAL_RX_BAD_FCS_CNT_OFFSET,
					 STATS_TOTAL_RX_BAD_FCS_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_user_pkt_show - Returns the total user rx packet count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total user rx packet count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_user_pkt_show(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_USER_DATA_RX_PACKETS_CNT_ADDR,
					 STATS_USER_DATA_RX_PACKETS_CNT_OFFSET,
					 STATS_USER_DATA_RX_PACKETS_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_good_user_pkt_show - Returns the total good user rx packet count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total good user rx packet count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_good_user_pkt_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_USER_DATA_RX_GOOD_PKT_CNT_ADDR,
					 STATS_USER_DATA_RX_GOOD_PKT_CNT_OFFSET,
					 STATS_USER_DATA_RX_GOOD_PKT_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_bad_user_pkt_show - Returns the total bad user rx packet count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total bad user rx packet count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_bad_user_pkt_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_USER_DATA_RX_BAD_PKT_CNT_ADDR,
					 STATS_USER_DATA_RX_BAD_PKT_CNT_OFFSET,
					 STATS_USER_DATA_RX_BAD_PKT_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_bad_user_fcs_show - Returns the total bad user rx fcs count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total bad user frame check sequences count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_bad_user_fcs_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_USER_DATA_RX_BAD_FCS_CNT_ADDR,
					 STATS_USER_DATA_RX_BAD_FCS_CNT_OFFSET,
					 STATS_USER_DATA_RX_BAD_FCS_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_user_ctrl_pkt_show - Returns the total user rx control packet count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total user rx control packet count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_user_ctrl_pkt_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_USER_CTRL_RX_PACKETS_CNT_ADDR,
					 STATS_USER_CTRL_RX_PACKETS_CNT_OFFSET,
					 STATS_USER_CTRL_RX_PACKETS_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_good_user_ctrl_pkt_show - Returns the total good user rx
 * control packet count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total good user rx control packet count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_good_user_ctrl_pkt_show(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_USER_CTRL_RX_GOOD_PKT_CNT_ADDR,
					 STATS_USER_CTRL_RX_GOOD_PKT_CNT_OFFSET,
					 STATS_USER_CTRL_RX_GOOD_PKT_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_bad_user_ctrl_pkt_show - Returns the total bad user rx
 * control packet count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total bad user rx control packet count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_bad_user_ctrl_pkt_show(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_USER_CTRL_RX_BAD_PKT_CNT_ADDR,
					 STATS_USER_CTRL_RX_BAD_PKT_CNT_OFFSET,
					 STATS_USER_CTRL_RX_BAD_PKT_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * total_rx_bad_user_ctrl_fcs_show - Returns the total bad user rx
 * control fcs count
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total bad user control frame check sequences count
 *
 * Return: The number of characters printed on success
 */
static ssize_t total_rx_bad_user_ctrl_fcs_show(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       char *buff)
{
	u32 count;

	count = utils_sysfs_show_wrapper(STATS_USER_CTRL_RX_BAD_FCS_CNT_ADDR,
					 STATS_USER_CTRL_RX_BAD_FCS_CNT_OFFSET,
					 STATS_USER_CTRL_RX_BAD_FCS_CNT_MASK,
					 kobj);
	return sprintf(buff, "%d\n", count);
}

/**
 * rx_user_pkt_rate_show - Returns the rate of user packets
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total user rx packet count
 *
 * Return: Returns the rate of user packets
 */
static ssize_t rx_user_pkt_rate_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buff)
{
	u32 rate;

	rate = utils_sysfs_show_wrapper(STATS_USER_DATA_RX_PKTS_RATE_ADDR,
					STATS_USER_DATA_RX_PKTS_RATE_OFFSET,
					STATS_USER_DATA_RX_PKTS_RATE_MASK,
					kobj);
	return sprintf(buff, "%d\n", rate);
}

/**
 * rx_user_ctrl_pkt_rate_show - Returns the rate of user control packets
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer the value will be written to
 *
 * Returns the total user rx packet count
 *
 * Return: Returns the rate of user control packets
 */
static ssize_t rx_user_ctrl_pkt_rate_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buff)
{
	u32 rate;

	rate = utils_sysfs_show_wrapper(STATS_USER_CTRL_RX_PKTS_RATE_ADDR,
					STATS_USER_CTRL_RX_PKTS_RATE_OFFSET,
					STATS_USER_CTRL_RX_PKTS_RATE_MASK,
					kobj);
	return sprintf(buff, "%d\n", rate);
}

/* TODO Use DEVICE_ATTR/_RW/_RO macros */
static struct kobj_attribute total_rx_good_pkt_attribute =
	__ATTR(total_rx_good_pkt, 0444, total_rx_good_pkt_show, NULL);
static struct kobj_attribute total_rx_bad_pkt_attribute =
	__ATTR(total_rx_bad_pkt, 0444, total_rx_bad_pkt_show, NULL);
static struct kobj_attribute total_rx_bad_fcs_attribute =
	__ATTR(total_rx_bad_fcs, 0444, total_rx_bad_fcs_show, NULL);
static struct kobj_attribute total_rx_user_pkt_attribute =
	__ATTR(total_rx_user_pkt, 0444, total_rx_user_pkt_show, NULL);
static struct kobj_attribute total_rx_good_user_pkt_attribute =
	__ATTR(total_rx_good_user_pkt, 0444, total_rx_good_user_pkt_show, NULL);
static struct kobj_attribute total_rx_bad_user_pkt_attribute =
	__ATTR(total_rx_bad_user_pkt, 0444, total_rx_bad_user_pkt_show, NULL);
static struct kobj_attribute total_rx_bad_user_fcs_attribute =
	__ATTR(total_rx_bad_user_fcs, 0444, total_rx_bad_user_fcs_show, NULL);
static struct kobj_attribute total_rx_user_ctrl_pkt_attribute =
	__ATTR(total_rx_user_ctrl_pkt, 0444, total_rx_user_ctrl_pkt_show, NULL);
static struct kobj_attribute total_rx_good_user_ctrl_pkt_attribute =
	__ATTR(total_rx_good_user_ctrl_pkt, 0444,
	       total_rx_good_user_ctrl_pkt_show, NULL);
static struct kobj_attribute total_rx_bad_user_ctrl_pkt_attribute =
	__ATTR(total_rx_bad_user_ctrl_pkt, 0444,
	       total_rx_bad_user_ctrl_pkt_show, NULL);
static struct kobj_attribute total_rx_bad_user_ctrl_fcs_attribute =
	__ATTR(total_rx_bad_user_ctrl_fcs, 0444,
	       total_rx_bad_user_ctrl_fcs_show, NULL);
static struct kobj_attribute rx_user_pkt_rate_attribute =
	__ATTR(rx_user_pkt_rate, 0444, rx_user_pkt_rate_show, NULL);
static struct kobj_attribute rx_user_ctrl_pkt_rate_attribute =
	__ATTR(rx_user_ctrl_pkt_rate, 0444, rx_user_ctrl_pkt_rate_show, NULL);

static struct attribute *attrs[] = {
	&total_rx_good_pkt_attribute.attr,
	&total_rx_bad_pkt_attribute.attr,
	&total_rx_bad_fcs_attribute.attr,
	&total_rx_user_pkt_attribute.attr,
	&total_rx_good_user_pkt_attribute.attr,
	&total_rx_bad_user_pkt_attribute.attr,
	&total_rx_bad_user_fcs_attribute.attr,
	&total_rx_user_ctrl_pkt_attribute.attr,
	&total_rx_good_user_ctrl_pkt_attribute.attr,
	&total_rx_bad_user_ctrl_pkt_attribute.attr,
	&total_rx_bad_user_ctrl_fcs_attribute.attr,
	&rx_user_pkt_rate_attribute.attr,
	&rx_user_ctrl_pkt_rate_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

struct kobject *kobj_stats;

/**
 * xroe_sysfs_stats_init - Creates the xroe sysfs "stats" subdirectory & entries
 *
 * Return: 0 on success, negative value in case of failure to
 * create the sysfs group
 *
 * Creates the xroe sysfs "stats" subdirectory and entries under "xroe"
 */
int xroe_sysfs_stats_init(void)
{
	int ret;

	kobj_stats = kobject_create_and_add("stats", root_xroe_kobj);
	if (!kobj_stats)
		return -ENOMEM;

	ret = sysfs_create_group(kobj_stats, &attr_group);
	if (ret)
		kobject_put(kobj_stats);

	return ret;
}

/**
 * xroe_sysfs_stats_exit - Deletes the xroe sysfs "ipv4" subdirectory & entries
 *
 * Deletes the xroe sysfs "stats" subdirectory and entries,
 * under the "xroe" entry
 */
void xroe_sysfs_stats_exit(void)
{
	kobject_put(kobj_stats);
}

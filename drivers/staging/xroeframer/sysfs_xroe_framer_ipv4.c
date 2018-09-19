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

static void utils_ipv4addr_hextochar(u32 ip, unsigned char *bytes);
static int utils_ipv4addr_chartohex(char *ip_addr, uint32_t *p_ip_addr);

/**
 * ipv4_version_show - Returns the IPv4 version number
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 version number
 *
 * Returns the IPv4 version number
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_version_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_VERSION_OFFSET;
	u32 mask = ETH_IPV4_VERSION_MASK;
	u32 buffer = 0;
	u32 version = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_VERSION_ADDR);

	buffer = ioread32(working_address);
	version = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", version);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_version_store - Writes to the IPv4 version number sysfs entry
 * (not permitted)
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 version
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 version number sysfs entry (not permitted)
 *
 * Return: 0
 */
static ssize_t ipv4_version_store(struct kobject *kobj,
				  struct kobj_attribute *attr, const char *buff,
				  size_t count)
{
	return 0;
}

/**
 * ipv4_ihl_show - Returns the IPv4 IHL
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 IHL
 *
 * Returns the IPv4 IHL
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_ihl_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_IHL_OFFSET;
	u32 mask = ETH_IPV4_IHL_MASK;
	u32 buffer = 0;
	u32 ihl = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_IHL_ADDR);

	buffer = ioread32(working_address);
	ihl = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", ihl);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_ihl_store - Writes to the IPv4 IHL sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 IHL
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 IHL sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_ihl_store(struct kobject *kobj,
			      struct kobj_attribute *attr, const char *buff,
			      size_t count)
{
	int ret = 0;
	u32 offset = ETH_IPV4_IHL_OFFSET;
	u32 mask = ETH_IPV4_IHL_MASK;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_IHL_ADDR);
	unsigned int ihl = 0;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &ihl);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)ihl, mask, offset);
	return xroe_size;
}

/**
 * ipv4_dscp_show - Returns the IPv4 DSCP
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 DSCP
 *
 * Returns the IPv4 DSCP
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_dscp_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_DSCP_OFFSET;
	u32 mask = ETH_IPV4_DSCP_MASK;
	u32 buffer = 0;
	u32 dscp = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_DSCP_ADDR);

	buffer = ioread32(working_address);
	dscp = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", dscp);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_dscp_store - Writes to the IPv4 DSCP sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 DSCP
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 DSCP sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_dscp_store(struct kobject *kobj,
			       struct kobj_attribute *attr, const char *buff,
			       size_t count)
{
	int ret = 0;
	u32 offset = ETH_IPV4_DSCP_OFFSET;
	u32 mask = ETH_IPV4_DSCP_MASK;
	unsigned int dscp = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_DSCP_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &dscp);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)dscp, mask, offset);
	return xroe_size;
}

/**
 * ipv4_ecn_show - Returns the IPv4 ECN
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 ECN
 *
 * Returns the IPv4 ECN
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_ecn_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_ECN_OFFSET;
	u32 mask = ETH_IPV4_ECN_MASK;
	u32 buffer = 0;
	u32 ecn = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_ECN_ADDR);

	buffer = ioread32(working_address);
	ecn = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", ecn);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_ecn_store - Writes to the IPv4 ECN sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 ECN
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 ECN sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_ecn_store(struct kobject *kobj,
			      struct kobj_attribute *attr, const char *buff,
			      size_t count)
{
	int ret = 0;
	u32 offset = ETH_IPV4_ECN_OFFSET;
	u32 mask = ETH_IPV4_ECN_MASK;
	unsigned int ecn = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_ECN_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &ecn);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)ecn, mask, offset);
	return xroe_size;
}

/**
 * ipv4_id_show - Returns the IPv4 ID
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 ID
 *
 * Returns the IPv4 ID
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_id_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_ID_OFFSET;
	u32 mask = ETH_IPV4_ID_MASK;
	u32 buffer = 0;
	u32 id = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_ID_ADDR);

	buffer = ioread32(working_address);
	id = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", id);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_id_store - Writes to the IPv4 ID sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 ID
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 ID sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_id_store(struct kobject *kobj,
			     struct kobj_attribute *attr, const char *buff,
			     size_t count)
{
	int ret = 0;
	u32 offset = ETH_IPV4_ID_OFFSET;
	u32 mask = ETH_IPV4_ID_MASK;
	unsigned int id = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_ID_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &id);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)id, mask, offset);
	return xroe_size;
}

/**
 * ipv4_flags_show - Returns the IPv4 flags
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 flags
 *
 * Returns the IPv4 flags
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_flags_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_FLAGS_OFFSET;
	u32 mask = ETH_IPV4_FLAGS_MASK;
	u32 buffer = 0;
	u32 flags = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_FLAGS_ADDR);

	buffer = ioread32(working_address);
	flags = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", flags);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_flags_store - Writes to the IPv4 flags sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 flags
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 flags sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_flags_store(struct kobject *kobj,
				struct kobj_attribute *attr, const char *buff,
				size_t count)
{
	int ret = 0;
	u32 offset = ETH_IPV4_FLAGS_OFFSET;
	u32 mask = ETH_IPV4_FLAGS_MASK;
	unsigned int flags = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV4_FLAGS_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &flags);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)flags, mask, offset);
	return xroe_size;
}

/**
 * ipv4_fragment_offset_show - Returns the IPv4 fragment offset
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 fragment offset
 *
 * Returns the IPv4 fragment offset
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_fragment_offset_show
(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_FRAGMENT_OFFSET_OFFSET;
	u32 mask = ETH_IPV4_FRAGMENT_OFFSET_MASK;
	u32 buffer = 0;
	u32 fragment_offset = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_FRAGMENT_OFFSET_ADDR);

	buffer = ioread32(working_address);
	fragment_offset = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", fragment_offset);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_fragment_offset_store - Writes to the IPv4 fragment offset sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 fragment offset
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 fragment offset sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_fragment_offset_store
(struct kobject *kobj, struct kobj_attribute *attr, const char *buff,
size_t count)
{
	int ret = 0;
	u32 offset = ETH_IPV4_FRAGMENT_OFFSET_OFFSET;
	u32 mask = ETH_IPV4_FRAGMENT_OFFSET_MASK;
	unsigned int fragment_offset = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_FRAGMENT_OFFSET_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &fragment_offset);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)fragment_offset,
			      mask, offset);
	return xroe_size;
}

/**
 * ipv4_ttl_show - Returns the IPv4 TTL
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 TTL
 *
 * Returns the IPv4 TTL
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_ttl_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buff)
{
	u32 offset = ETH_IPV4_TIME_TO_LIVE_OFFSET;
	u32 mask = ETH_IPV4_TIME_TO_LIVE_MASK;
	u32 buffer = 0;
	u32 ttl = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_TIME_TO_LIVE_ADDR);

	buffer = ioread32(working_address);
	ttl = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", ttl);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_ttl_store - Writes to the IPv4 TTL sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 TTL
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 TTL sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_ttl_store(struct kobject *kobj,
			      struct kobj_attribute *attr, const char *buff,
			      size_t count)
{
	int ret = 0;
	u32 offset = ETH_IPV4_TIME_TO_LIVE_OFFSET;
	u32 mask = ETH_IPV4_TIME_TO_LIVE_MASK;
	unsigned int ttl = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_TIME_TO_LIVE_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &ttl);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)ttl, mask, offset);
	return xroe_size;
}

/**
 * ipv4_protocol_show - Returns the IPv4 protocol
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 protocol
 *
 * Returns the IPv4 protocol
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_protocol_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_PROTOCOL_OFFSET;
	u32 mask = ETH_IPV4_PROTOCOL_MASK;
	u32 buffer = 0;
	u32 protocol = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_PROTOCOL_ADDR);

	buffer = ioread32(working_address);
	protocol = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", protocol);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_protocol_store - Writes to the IPv4 protocol sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 protocol
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 protocol sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_protocol_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buff, size_t count)
{
	int ret = 0;
	u32 offset = ETH_IPV4_PROTOCOL_OFFSET;
	u32 mask = ETH_IPV4_PROTOCOL_MASK;
	unsigned int protocol = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_PROTOCOL_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &protocol);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)protocol,
			      mask, offset);
	return xroe_size;
}

/**
 * ipv4_source_address_show - Returns the IPv4 source address
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 source address
 *
 * Returns the IPv4 source address in x.x.x.x format
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_source_address_show
(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_SOURCE_ADD_OFFSET;
	unsigned long mask = ETH_IPV4_SOURCE_ADD_MASK;
	u32 buffer = 0;
	u32 source_add = 0;
	unsigned char *ip_addr_char = NULL;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_SOURCE_ADD_ADDR);

	buffer = ioread32(working_address);
	source_add = (buffer & mask) >> offset;
	utils_ipv4addr_hextochar(source_add, ip_addr_char);
	sprintf(buff, "%d.%d.%d.%d\n", ip_addr_char[3], ip_addr_char[2],
		ip_addr_char[1], ip_addr_char[0]);

	return XROE_SIZE_MAX;
}

/**
 * ipv4_source_address_store - Writes to the IPv4 source address sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 source address
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 source address sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_source_address_store
(struct kobject *kobj, struct kobj_attribute *attr, const char *buff,
size_t count)
{
	u32 offset = ETH_IPV4_SOURCE_ADD_OFFSET;
	unsigned long mask = ETH_IPV4_SOURCE_ADD_MASK;
	u32 source_add = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_SOURCE_ADD_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (utils_ipv4addr_chartohex(xroe_tmp, &source_add) == 4)
		utils_write32withmask(working_address,
				      source_add, mask, offset);
	return xroe_size;
}

/**
 * ipv4_destination_address_show - Returns the IPv4 destination address
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 destination address
 *
 * Returns the IPv4 destination address in x.x.x.x format
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv4_destination_address_show
(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV4_DESTINATION_ADD_OFFSET;
	unsigned long mask = ETH_IPV4_DESTINATION_ADD_MASK;
	u32 buffer = 0;
	u32 destination_add = 0;
	unsigned char *ip_addr_char = NULL;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_DESTINATION_ADD_ADDR);

	buffer = ioread32(working_address);
	destination_add = (buffer & mask) >> offset;
	utils_ipv4addr_hextochar(destination_add, ip_addr_char);
	sprintf(buff, "%d.%d.%d.%d\n", ip_addr_char[3], ip_addr_char[2],
		ip_addr_char[1], ip_addr_char[0]);
	return XROE_SIZE_MAX;
}

/**
 * ipv4_destination_address_store - Writes to the IPv4 destination address
 * sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 destination address
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv4 destination address sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv4_destination_address_store
(struct kobject *kobj, struct kobj_attribute *attr, const char *buff,
size_t count)
{
	u32 offset = ETH_IPV4_DESTINATION_ADD_OFFSET;
	unsigned long mask = ETH_IPV4_DESTINATION_ADD_MASK;
	u32 destination_add = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV4_DESTINATION_ADD_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (utils_ipv4addr_chartohex(xroe_tmp,
				     &destination_add) == 4)
		utils_write32withmask(working_address,
				      destination_add, mask, offset);
	return xroe_size;
}

/* TODO Use DEVICE_ATTR/_RW/_RO macros */

static struct kobj_attribute version_attribute =
	__ATTR(version, 0444, ipv4_version_show, ipv4_version_store);
static struct kobj_attribute ihl_attribute =
	__ATTR(ihl, 0660, ipv4_ihl_show, ipv4_ihl_store);
static struct kobj_attribute dscp_attribute =
	__ATTR(dscp, 0660, ipv4_dscp_show, ipv4_dscp_store);
static struct kobj_attribute ecn_attribute =
	__ATTR(ecn, 0660, ipv4_ecn_show, ipv4_ecn_store);
static struct kobj_attribute id_attribute =
	__ATTR(id, 0660, ipv4_id_show, ipv4_id_store);
static struct kobj_attribute flags_attribute =
	__ATTR(flags, 0660, ipv4_flags_show, ipv4_flags_store);
static struct kobj_attribute fragment_offset_attribute =
	__ATTR(fragment_offset, 0660, ipv4_fragment_offset_show,
	       ipv4_fragment_offset_store);
static struct kobj_attribute ttl_attribute =
	__ATTR(ttl, 0660, ipv4_ttl_show, ipv4_ttl_store);
static struct kobj_attribute protocol_attribute =
	__ATTR(protocol, 0660, ipv4_protocol_show, ipv4_protocol_store);
static struct kobj_attribute source_add_attribute =
	__ATTR(source_add, 0660, ipv4_source_address_show,
	       ipv4_source_address_store);
static struct kobj_attribute destination_add_attribute =
	__ATTR(dest_add, 0660, ipv4_destination_address_show,
	       ipv4_destination_address_store);

static struct attribute *attrs[] = {
	&version_attribute.attr,
	&ihl_attribute.attr,
	&dscp_attribute.attr,
	&ecn_attribute.attr,
	&id_attribute.attr,
	&flags_attribute.attr,
	&fragment_offset_attribute.attr,
	&ttl_attribute.attr,
	&protocol_attribute.attr,
	&source_add_attribute.attr,
	&destination_add_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

struct kobject *kobj_framer;
static struct kobject *kobj_ipv4;

/**
 * xroe_sysfs_ipv4_init - Creates the xroe sysfs "ipv4" subdirectory & entries
 *
 * Return: 0 on success, negative value in case of failure to
 * create the sysfs group
 *
 * Creates the xroe sysfs "ipv4" subdirectory and entries under "xroe"
 */
int xroe_sysfs_ipv4_init(void)
{
	int ret;

	kobj_framer = kobject_create_and_add("framer", root_xroe_kobj);
	if (!kobj_framer)
		return -ENOMEM;
	kobj_ipv4 = kobject_create_and_add("ipv4", kobj_framer);
	if (!kobj_ipv4)
		return -ENOMEM;
	ret = sysfs_create_group(kobj_ipv4, &attr_group);
	if (ret)
		kobject_put(kobj_ipv4);
	return ret;
}

/**
 * xroe_sysfs_ipv4_exit - Deletes the xroe sysfs "ipv4" subdirectory & entries
 *
 * Deletes the xroe sysfs "ipv4" subdirectory and entries,
 * under the "xroe" entry
 */
void xroe_sysfs_ipv4_exit(void)
{
	kobject_put(kobj_ipv4);
}

/**
 * utils_ipv4addr_hextochar - Integer to char array for IPv4 addresses
 * @ip:		The IP address in integer format
 * @bytes:	The IP address in a 4-byte array
 *
 * Coverts an IPv4 address given in unsigned integer format to a character array
 */
static void utils_ipv4addr_hextochar(u32 ip, unsigned char *bytes)
{
	bytes[0] = ip & 0xFF;
	bytes[1] = (ip >> 8) & 0xFF;
	bytes[2] = (ip >> 16) & 0xFF;
	bytes[3] = (ip >> 24) & 0xFF;
}

/**
 * utils_ipv4addr_chartohex - Character to char array for IPv4 addresses
 * @ip_addr:	The character array containing the IP address
 * @p_ip_addr:	The converted IPv4 address
 *
 * Coverts an IPv4 address given as a character array to integer format
 *
 * Return: 4 (the length of the resulting character array) on success,
 * -1 in case of wrong input
 */
static int utils_ipv4addr_chartohex(char *ip_addr, uint32_t *p_ip_addr)
{
	int count = 0, ret = -1;
	char *string;
	unsigned char *found;
	u32 byte_array[4];
	u32 byte = 0;

	string = ip_addr;
	while ((found = (unsigned char *)strsep(&string, ".")) != NULL) {
		if (count <= 4) {
			ret = kstrtouint(found, 10, &byte);
			if (ret)
				return ret;
			byte_array[count] = byte;
		} else {
			break;
		}
		count++;
	}

	if (count == 4) {
		ret = count;
		*p_ip_addr = byte_array[3] | (byte_array[2] << 8)
		| (byte_array[1] << 16) | (byte_array[0] << 24);
	}
	return ret;
}

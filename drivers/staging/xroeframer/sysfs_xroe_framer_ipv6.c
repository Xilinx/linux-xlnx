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

enum { XROE_SIZE_MAX = 60 };
static int xroe_size;
static char xroe_tmp[XROE_SIZE_MAX];

static void utils_ipv6addr_32to16(u32 *ip32, uint16_t *ip16);
static int utils_ipv6addr_chartohex(char *ip_addr, uint32_t *p_ip_addr);

/**
 * ipv6_version_show - Returns the IPv6 version number
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 version number
 *
 * Returns the IPv6 version number
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv6_version_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV6_V_OFFSET;
	u32 mask = ETH_IPV6_V_MASK;
	u32 buffer = 0;
	u32 version = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr
	+ ETH_IPV6_V_ADDR);

	buffer = ioread32(working_address);
	version = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", version);
	return XROE_SIZE_MAX;
}

/**
 * ipv6_version_store - Writes to the IPv6 version number sysfs entry
 * (not permitted)
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 version
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv6 version number sysfs entry (not permitted)
 *
 * Return: 0
 */
static ssize_t ipv6_version_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buff, size_t count)
{
	return 0;
}

/**
 * ipv6_traffic_class_show - Returns the IPv6 traffic class
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 traffic class
 *
 * Returns the IPv6 traffic class
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv6_traffic_class_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV6_TRAFFIC_CLASS_OFFSET;
	u32 mask = ETH_IPV6_TRAFFIC_CLASS_MASK;
	u32 buffer = 0;
	u32 traffic_class = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_TRAFFIC_CLASS_ADDR);

	buffer = ioread32(working_address);
	traffic_class = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", traffic_class);
	return XROE_SIZE_MAX;
}

/**
 * ipv6_traffic_class_store - Writes to the IPv6 traffic class
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 traffic class
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv6 traffic class sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv6_traffic_class_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buff, size_t count)
{
	int ret;
	u32 offset = ETH_IPV6_TRAFFIC_CLASS_OFFSET;
	u32 mask = ETH_IPV6_TRAFFIC_CLASS_MASK;
	unsigned int traffic_class = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_TRAFFIC_CLASS_ADDR);

	ret = 0;
	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &traffic_class);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)traffic_class,
			      mask, offset);
	return xroe_size;
}

/**
 * ipv6_flow_label_show - Returns the IPv6 flow label
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 flow label
 *
 * Returns the IPv6 flow label
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv6_flow_label_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV6_FLOW_LABEL_OFFSET;
	u32 mask = ETH_IPV6_FLOW_LABEL_MASK;
	u32 buffer = 0;
	u32 flow_label = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_FLOW_LABEL_ADDR);

	buffer = ioread32(working_address);
	flow_label = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", flow_label);
	return XROE_SIZE_MAX;
}

/**
 * ipv6_flow_label_store - Writes to the IPv6 flow label
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 flow label
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv6 flow label sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv6_flow_label_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buff, size_t count)
{
	int ret;
	u32 offset = ETH_IPV6_FLOW_LABEL_OFFSET;
	u32 mask = ETH_IPV6_FLOW_LABEL_MASK;
	unsigned int flow_label = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_FLOW_LABEL_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = 0;
	ret = kstrtouint(buff, 10, &flow_label);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)flow_label,
			      mask, offset);
	return xroe_size;
}

/**
 * ipv6_next_header_show - Returns the IPv6 next header
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 next header
 *
 * Returns the IPv6 next header
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv6_next_header_show(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     char *buff)
{
	u32 offset = ETH_IPV6_NEXT_HEADER_OFFSET;
	u32 mask = ETH_IPV6_NEXT_HEADER_MASK;
	u32 buffer = 0;
	u32 next_header = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_NEXT_HEADER_ADDR);

	buffer = ioread32(working_address);
	next_header = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", next_header);
	return XROE_SIZE_MAX;
}

/**
 * ipv6_next_header_store - Writes to the IPv6 next header
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 next header
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv6 next header sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv6_next_header_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buff, size_t count)
{
	int ret;
	u32 offset = ETH_IPV6_NEXT_HEADER_OFFSET;
	u32 mask = ETH_IPV6_NEXT_HEADER_MASK;
	unsigned int next_header = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_NEXT_HEADER_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = 0;
	ret = kstrtouint(buff, 10, &next_header);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)next_header,
			      mask, offset);
	return xroe_size;
}

/**
 * ipv6_hop_limit_show - Returns the IPv6 hop limit
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 hop limit
 *
 * Returns the IPv6 hop limit
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv6_hop_limit_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV6_HOP_LIMIT_OFFSET;
	u32 mask = ETH_IPV6_HOP_LIMIT_MASK;
	u32 buffer = 0;
	u32 hop_limit = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_HOP_LIMIT_ADDR);

	buffer = ioread32(working_address);
	hop_limit = (buffer & mask) >> offset;
	sprintf(buff, "%d\n", hop_limit);
	return XROE_SIZE_MAX;
}

/**
 * ipv6_hop_limit_store - Writes to the IPv6 hop limit
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv6 hop limit
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv6 hop limit sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv6_hop_limit_store
(struct kobject *kobj, struct kobj_attribute *attr, const char *buff,
size_t count)
{
	int ret;
	u32 offset = ETH_IPV6_HOP_LIMIT_OFFSET;
	u32 mask = ETH_IPV6_HOP_LIMIT_MASK;
	unsigned int hop_limit = 0;
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_HOP_LIMIT_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = 0;
	ret = kstrtouint(buff, 10, &hop_limit);
	if (ret)
		return ret;
	utils_write32withmask(working_address, (u32)hop_limit,
			      mask, offset);
	return xroe_size;
}

/**
 * ipv6_source_address_show - Returns the IPv6 source address
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 source address
 *
 * Returns the IPv6 source address in xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx
 * format
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv6_source_address_show
(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV6_SOURCE_ADD_31_0_OFFSET;
	unsigned long mask = ETH_IPV6_SOURCE_ADD_31_0_MASK;
	u32 buffer = 0;
	u32 source_add[4];
	u16 source_add16[8];
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_SOURCE_ADD_31_0_ADDR);

	buffer = ioread32(working_address);
	source_add[0] = (buffer & mask) >> offset;

	working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_SOURCE_ADD_63_32_ADDR);
	mask = ETH_IPV6_SOURCE_ADD_63_32_MASK;
	offset = ETH_IPV6_SOURCE_ADD_63_32_OFFSET;

	buffer = ioread32(working_address);
	source_add[1] = (buffer & mask) >> offset;

	working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_SOURCE_ADD_95_64_ADDR);
	mask = ETH_IPV6_SOURCE_ADD_95_64_MASK;
	offset = ETH_IPV6_SOURCE_ADD_95_64_OFFSET;

	buffer = ioread32(working_address);
	source_add[2] = (buffer & mask) >> offset;

	working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_SOURCE_ADD_127_96_ADDR);
	mask = ETH_IPV6_SOURCE_ADD_127_96_MASK;
	offset = ETH_IPV6_SOURCE_ADD_127_96_OFFSET;

	buffer = ioread32(working_address);
	source_add[3] = (buffer & mask) >> offset;

	utils_ipv6addr_32to16(source_add, source_add16);
	sprintf(buff, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
		source_add16[0], source_add16[1], source_add16[2],
		source_add16[3],
		source_add16[4], source_add16[5], source_add16[6],
		source_add16[7]);
	return XROE_SIZE_MAX;
}

/**
 * ipv6_source_address_store - Writes to the IPv6 source address sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 source address
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv6 source address sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv6_source_address_store
(struct kobject *kobj, struct kobj_attribute *attr, const char *buff,
size_t count)
{
	u32 offset = ETH_IPV6_SOURCE_ADD_31_0_OFFSET;
	unsigned long mask = ETH_IPV6_SOURCE_ADD_31_0_MASK;
	u32 source_add[4];
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_SOURCE_ADD_31_0_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (utils_ipv6addr_chartohex(xroe_tmp, source_add) == 8) {
		utils_write32withmask(working_address, source_add[0],
				      mask, offset);

		working_address = ((u8 *)lp->base_addr +
		ETH_IPV6_SOURCE_ADD_63_32_ADDR);
		mask = ETH_IPV6_SOURCE_ADD_63_32_MASK;
		offset = ETH_IPV6_SOURCE_ADD_63_32_OFFSET;

		utils_write32withmask(working_address, source_add[1],
				      mask, offset);

		working_address = ((u8 *)lp->base_addr +
		ETH_IPV6_SOURCE_ADD_95_64_ADDR);
		mask = ETH_IPV6_SOURCE_ADD_95_64_MASK;
		offset = ETH_IPV6_SOURCE_ADD_95_64_OFFSET;

		utils_write32withmask(working_address, source_add[2],
				      mask, offset);

		working_address = ((u8 *)lp->base_addr +
		ETH_IPV6_SOURCE_ADD_127_96_ADDR);
		mask = ETH_IPV6_SOURCE_ADD_127_96_MASK;
		offset = ETH_IPV6_SOURCE_ADD_127_96_OFFSET;

		utils_write32withmask(working_address, source_add[3],
				      mask, offset);
	}
	return xroe_size;
}

/**
 * ipv6_destination_address_show - Returns the IPv6 destination address
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 destination address
 *
 * Returns the IPv6 destination address in
 * xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx format
 *
 * Return: XROE_SIZE_MAX on success
 */
static ssize_t ipv6_destination_address_show
(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	u32 offset = ETH_IPV6_DESTINATION_ADD_31_0_OFFSET;
	unsigned long mask = ETH_IPV6_DESTINATION_ADD_31_0_MASK;
	u32 buffer = 0;
	u32 dest_add[4];
	u16 dest_add16[8];
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_DESTINATION_ADD_31_0_ADDR);

	buffer = ioread32(working_address);
	dest_add[0] = (buffer & mask) >> offset;

	working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_DESTINATION_ADD_63_32_ADDR);
	mask = ETH_IPV6_DESTINATION_ADD_63_32_MASK;
	offset = ETH_IPV6_DESTINATION_ADD_63_32_OFFSET;

	buffer = ioread32(working_address);
	dest_add[1] = (buffer & mask) >> offset;

	working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_DESTINATION_ADD_95_64_ADDR);
	mask = ETH_IPV6_DESTINATION_ADD_95_64_MASK;
	offset = ETH_IPV6_DESTINATION_ADD_95_64_OFFSET;

	buffer = ioread32(working_address);
	dest_add[2] = (buffer & mask) >> offset;

	working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_DESTINATION_ADD_127_96_ADDR);
	mask = ETH_IPV6_DESTINATION_ADD_127_96_MASK;
	offset = ETH_IPV6_DESTINATION_ADD_127_96_OFFSET;

	buffer = ioread32(working_address);
	dest_add[3] = (buffer & mask) >> offset;

	utils_ipv6addr_32to16(dest_add, dest_add16);
	sprintf(buff, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
		dest_add16[0], dest_add16[1], dest_add16[2], dest_add16[3],
		dest_add16[4], dest_add16[5], dest_add16[6], dest_add16[7]);
	return XROE_SIZE_MAX;
}

/**
 * ipv6_destination_address_store - Writes to the IPv6 destination address
 * sysfs entry
 * @kobj:	The kernel object of the entry
 * @attr:	The attributes of the kernel object
 * @buff:	The buffer containing the IPv4 destination address
 * @count:	The number of characters typed by the user
 *
 * Writes to the IPv6 destination address sysfs entry
 *
 * Return: XROE_SIZE_MAX or the value of "count", if that's lesser, on success
 */
static ssize_t ipv6_destination_address_store
(struct kobject *kobj, struct kobj_attribute *attr, const char *buff,
size_t count)
{
	u32 offset = ETH_IPV6_DESTINATION_ADD_31_0_OFFSET;
	unsigned long mask = ETH_IPV6_DESTINATION_ADD_31_0_MASK;
	u32 dest_add[4];
	void __iomem *working_address = ((u8 *)lp->base_addr +
	ETH_IPV6_DESTINATION_ADD_31_0_ADDR);

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (utils_ipv6addr_chartohex(xroe_tmp, dest_add) == 8) {
		utils_write32withmask(working_address, dest_add[0],
				      mask, offset);

		working_address = ((u8 *)lp->base_addr +
		ETH_IPV6_DESTINATION_ADD_63_32_ADDR);
		mask = ETH_IPV6_DESTINATION_ADD_63_32_MASK;
		offset = ETH_IPV6_DESTINATION_ADD_63_32_OFFSET;

		utils_write32withmask(working_address, dest_add[1],
				      mask, offset);

		working_address = ((u8 *)lp->base_addr +
		ETH_IPV6_DESTINATION_ADD_95_64_ADDR);
		mask = ETH_IPV6_DESTINATION_ADD_95_64_MASK;
		offset = ETH_IPV6_DESTINATION_ADD_95_64_OFFSET;

		utils_write32withmask(working_address, dest_add[2],
				      mask, offset);

		working_address = ((u8 *)lp->base_addr +
		ETH_IPV6_DESTINATION_ADD_127_96_ADDR);
		mask = ETH_IPV6_DESTINATION_ADD_127_96_MASK;
		offset = ETH_IPV6_DESTINATION_ADD_127_96_OFFSET;

		utils_write32withmask(working_address, dest_add[3],
				      mask, offset);
	}
	return xroe_size;
}

/* TODO Use DEVICE_ATTR/_RW/_RO macros */

static struct kobj_attribute version_attribute =
	__ATTR(version, 0444, ipv6_version_show, ipv6_version_store);
static struct kobj_attribute traffic_class =
	__ATTR(traffic_class, 0660, ipv6_traffic_class_show,
	       ipv6_traffic_class_store);
static struct kobj_attribute flow_label =
	__ATTR(flow_label, 0660, ipv6_flow_label_show, ipv6_flow_label_store);
static struct kobj_attribute next_header =
	__ATTR(next_header, 0660, ipv6_next_header_show,
	       ipv6_next_header_store);
static struct kobj_attribute hop_limit =
	__ATTR(hop_limit, 0660, ipv6_hop_limit_show, ipv6_hop_limit_store);
static struct kobj_attribute source_add_attribute =
	__ATTR(source_add, 0660, ipv6_source_address_show,
	       ipv6_source_address_store);
static struct kobj_attribute dest_add_attribute =
	__ATTR(dest_add, 0660, ipv6_destination_address_show,
	       ipv6_destination_address_store);

static struct attribute *attrs[] = {
	&version_attribute.attr,
	&traffic_class.attr,
	&flow_label.attr,
	&next_header.attr,
	&hop_limit.attr,
	&source_add_attribute.attr,
	&dest_add_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *kobj_ipv6;

/**
 * xroe_sysfs_ipv6_init - Creates the xroe sysfs "ipv6" subdirectory & entries
 *
 * Return: 0 on success, negative value in case of failure to
 * create the sysfs group
 *
 * Creates the xroe sysfs "ipv6" subdirectory and entries under "xroe"
 */
int xroe_sysfs_ipv6_init(void)
{
	int ret;

	kobj_ipv6 = kobject_create_and_add("ipv6", kobj_framer);
	if (!kobj_ipv6)
		return -ENOMEM;
	ret = sysfs_create_group(kobj_ipv6, &attr_group);
	if (ret)
		kobject_put(kobj_ipv6);
	return ret;
}

/**
 * xroe_sysfs_ipv4_exit - Deletes the xroe sysfs "ipv6" subdirectory & entries
 *
 * Deletes the xroe sysfs "ipv6" subdirectory and entries,
 * under the "xroe" entry
 *
 */
void xroe_sysfs_ipv6_exit(void)
{
	kobject_put(kobj_ipv6);
}

/**
 * utils_ipv6addr_32to16 - uint32_t to uint16_t for IPv6 addresses
 * @ip32:	The IPv6 address in uint32_t format
 * @ip16:	The IPv6 address in uint16_t format
 *
 * Coverts an IPv6 address given in uint32_t format to uint16_t
 */
static void utils_ipv6addr_32to16(u32 *ip32, uint16_t *ip16)
{
	ip16[0] = ip32[0] >> 16;
	ip16[1] = ip32[0] & 0x0000FFFF;
	ip16[2] = ip32[1] >> 16;
	ip16[3] = ip32[1] & 0x0000FFFF;
	ip16[4] = ip32[2] >> 16;
	ip16[5] = ip32[2] & 0x0000FFFF;
	ip16[6] = ip32[3] >> 16;
	ip16[7] = ip32[3] & 0x0000FFFF;
}

/**
 * utils_ipv6addr_chartohex - Character to char array for IPv6 addresses
 * @ip_addr:	The character array containing the IP address
 * @p_ip_addr:	The converted IPv4 address
 *
 * Coverts an IPv6 address given as a character array to integer format
 *
 * Return: 8 (the length of the resulting character array) on success,
 * -1 in case of wrong input
 */
static int utils_ipv6addr_chartohex(char *ip_addr, uint32_t *p_ip_addr)
{
	int ret;
	int count;
	char *string;
	unsigned char *found;
	u16 ip_array_16[8];
	u32 field;

	ret = -1;
	count = 0;
	string = ip_addr;
	while ((found = (unsigned char *)strsep(&string, ":")) != NULL) {
		if (count <= 8) {
			ret = kstrtouint(found, 16, &field);
			if (ret)
				return ret;
			ip_array_16[count] = (uint16_t)field;
		} else {
			break;
		}
		count++;
	}
	if (count == 8) {
		p_ip_addr[0] = ip_array_16[1] | (ip_array_16[0] << 16);
		p_ip_addr[1] = ip_array_16[3] | (ip_array_16[2] << 16);
		p_ip_addr[2] = ip_array_16[5] | (ip_array_16[4] << 16);
		p_ip_addr[3] = ip_array_16[7] | (ip_array_16[6] << 16);
		ret = count;
	}
	return ret;
}

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
	u32 version;

	version = utils_sysfs_show_wrapper(ETH_IPV4_VERSION_ADDR,
					   ETH_IPV4_VERSION_OFFSET,
					   ETH_IPV4_VERSION_MASK, kobj);
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
	u32 ihl;

	ihl = utils_sysfs_show_wrapper(ETH_IPV4_IHL_ADDR, ETH_IPV4_IHL_OFFSET,
				       ETH_IPV4_IHL_MASK, kobj);
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
	int ret;
	u32 ihl;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &ihl);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(ETH_IPV4_IHL_ADDR, ETH_IPV4_IHL_OFFSET,
				  ETH_IPV4_IHL_MASK, ihl, kobj);
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
	u32 dscp;

	dscp = utils_sysfs_show_wrapper(ETH_IPV4_DSCP_ADDR,
					ETH_IPV4_DSCP_OFFSET,
					ETH_IPV4_DSCP_MASK, kobj);
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
	int ret;
	u32 dscp;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &dscp);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(ETH_IPV4_DSCP_ADDR, ETH_IPV4_DSCP_OFFSET,
				  ETH_IPV4_DSCP_MASK, dscp, kobj);
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
	u32 ecn;

	ecn = utils_sysfs_show_wrapper(ETH_IPV4_ECN_ADDR, ETH_IPV4_ECN_OFFSET,
				       ETH_IPV4_ECN_MASK, kobj);
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
	int ret;
	u32 ecn;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &ecn);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(ETH_IPV4_ECN_ADDR, ETH_IPV4_ECN_OFFSET,
				  ETH_IPV4_ECN_MASK, ecn, kobj);
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
	u32 id;

	id = utils_sysfs_show_wrapper(ETH_IPV4_ID_ADDR, ETH_IPV4_ID_OFFSET,
				      ETH_IPV4_ID_MASK, kobj);
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
	int ret;
	u32 id;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &id);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(ETH_IPV4_ID_ADDR, ETH_IPV4_ID_OFFSET,
				  ETH_IPV4_ID_MASK, id, kobj);
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
	u32 flags;

	flags = utils_sysfs_show_wrapper(ETH_IPV4_FLAGS_ADDR,
					 ETH_IPV4_FLAGS_OFFSET,
					 ETH_IPV4_FLAGS_MASK, kobj);
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
	int ret;
	u32 flags;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &flags);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(ETH_IPV4_FLAGS_ADDR, ETH_IPV4_FLAGS_OFFSET,
				  ETH_IPV4_FLAGS_MASK, flags, kobj);
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
	u32 fragment;

	fragment = utils_sysfs_show_wrapper(ETH_IPV4_FRAGMENT_OFFSET_ADDR,
					    ETH_IPV4_FRAGMENT_OFFSET_OFFSET,
					    ETH_IPV4_FRAGMENT_OFFSET_MASK,
					    kobj);
	sprintf(buff, "%d\n", fragment);
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
	int ret;
	u32 fragment;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &fragment);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(ETH_IPV4_FRAGMENT_OFFSET_ADDR,
				  ETH_IPV4_FRAGMENT_OFFSET_OFFSET,
				  ETH_IPV4_FRAGMENT_OFFSET_MASK, fragment,
				  kobj);
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
	u32 ttl;

	ttl = utils_sysfs_show_wrapper(ETH_IPV4_TIME_TO_LIVE_ADDR,
				       ETH_IPV4_TIME_TO_LIVE_OFFSET,
				       ETH_IPV4_TIME_TO_LIVE_MASK, kobj);
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
	int ret;
	u32 ttl;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &ttl);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(ETH_IPV4_TIME_TO_LIVE_ADDR,
				  ETH_IPV4_TIME_TO_LIVE_OFFSET,
				  ETH_IPV4_TIME_TO_LIVE_MASK, ttl, kobj);
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
	u32 protocol;

	protocol = utils_sysfs_show_wrapper(ETH_IPV4_PROTOCOL_ADDR,
					    ETH_IPV4_PROTOCOL_OFFSET,
					    ETH_IPV4_PROTOCOL_MASK, kobj);
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
	int ret;
	u32 protocol;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	ret = kstrtouint(buff, 10, &protocol);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(ETH_IPV4_PROTOCOL_ADDR,
				  ETH_IPV4_PROTOCOL_OFFSET,
				  ETH_IPV4_PROTOCOL_MASK, protocol, kobj);
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
	u32 source_add = 0;
	unsigned char ip_addr_char[4];

	source_add = utils_sysfs_show_wrapper(ETH_IPV4_SOURCE_ADD_ADDR,
					      ETH_IPV4_SOURCE_ADD_OFFSET,
					      ETH_IPV4_SOURCE_ADD_MASK, kobj);
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
	u32 source_add = 0;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (utils_ipv4addr_chartohex(xroe_tmp, &source_add) == 4)
		utils_sysfs_store_wrapper(ETH_IPV4_SOURCE_ADD_ADDR,
					  ETH_IPV4_SOURCE_ADD_OFFSET,
					  ETH_IPV4_SOURCE_ADD_MASK, source_add,
					  kobj);
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
	u32 dest_add = 0;
	unsigned char ip_addr_char[4];

	dest_add = utils_sysfs_show_wrapper(ETH_IPV4_DESTINATION_ADD_ADDR,
					    ETH_IPV4_DESTINATION_ADD_OFFSET,
					    ETH_IPV4_DESTINATION_ADD_MASK,
					    kobj);
	utils_ipv4addr_hextochar(dest_add, ip_addr_char);
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
	u32 dest_add = 0;

	xroe_size = min_t(size_t, count, (size_t)XROE_SIZE_MAX);
	strncpy(xroe_tmp, buff, xroe_size);
	if (utils_ipv4addr_chartohex(xroe_tmp, &dest_add) == 4)
		utils_sysfs_store_wrapper(ETH_IPV4_DESTINATION_ADD_ADDR,
					  ETH_IPV4_DESTINATION_ADD_OFFSET,
					  ETH_IPV4_DESTINATION_ADD_MASK,
					  dest_add, kobj);
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
static struct kobject *kobj_ipv4[MAX_NUM_ETH_PORTS];
struct kobject *kobj_eth_ports[MAX_NUM_ETH_PORTS];

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
	int i;
	char eth_port_dir_name[11];

	kobj_framer = kobject_create_and_add("framer", root_xroe_kobj);
	if (!kobj_framer)
		return -ENOMEM;
	for (i = 0; i < 4; i++) {
		snprintf(eth_port_dir_name, sizeof(eth_port_dir_name),
			 "eth_port_%d", i);
		kobj_eth_ports[i] = kobject_create_and_add(eth_port_dir_name,
							   kobj_framer);
		if (!kobj_eth_ports[i])
			return -ENOMEM;
		kobj_ipv4[i] = kobject_create_and_add("ipv4",
						      kobj_eth_ports[i]);
		if (!kobj_ipv4[i])
			return -ENOMEM;
		ret = sysfs_create_group(kobj_ipv4[i], &attr_group);
		if (ret)
			kobject_put(kobj_ipv4[i]);
	}
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
	int i;

	for (i = 0; i < MAX_NUM_ETH_PORTS; i++)
		kobject_put(kobj_ipv4[i]);
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

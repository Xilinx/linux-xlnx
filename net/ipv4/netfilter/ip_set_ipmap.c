/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 * Copyright (C) 2003-2004 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.  
 */

/* Kernel module implementing an IP set type: the single bitmap type */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_set.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <linux/spinlock.h>

#include <linux/netfilter_ipv4/ip_set_ipmap.h>

static inline ip_set_ip_t
ip_to_id(const struct ip_set_ipmap *map, ip_set_ip_t ip)
{
	return (ip - map->first_ip)/map->hosts;
}

static inline int
__testip(struct ip_set *set, ip_set_ip_t ip, ip_set_ip_t *hash_ip)
{
	struct ip_set_ipmap *map = (struct ip_set_ipmap *) set->data;
	
	if (ip < map->first_ip || ip > map->last_ip)
		return -ERANGE;

	*hash_ip = ip & map->netmask;
	DP("set: %s, ip:%u.%u.%u.%u, %u.%u.%u.%u",
	   set->name, HIPQUAD(ip), HIPQUAD(*hash_ip));
	return !!test_bit(ip_to_id(map, *hash_ip), map->members);
}

static int
testip(struct ip_set *set, const void *data, size_t size,
       ip_set_ip_t *hash_ip)
{
	struct ip_set_req_ipmap *req = 
	    (struct ip_set_req_ipmap *) data;

	if (size != sizeof(struct ip_set_req_ipmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_ipmap),
			      size);
		return -EINVAL;
	}
	return __testip(set, req->ip, hash_ip);
}

static int
testip_kernel(struct ip_set *set, 
	      const struct sk_buff *skb,
	      ip_set_ip_t *hash_ip,
	      const u_int32_t *flags,
	      unsigned char index)
{
	int res;
	
	DP("flag: %s src: %u.%u.%u.%u dst: %u.%u.%u.%u",
	   flags[index] & IPSET_SRC ? "SRC" : "DST",
	   NIPQUAD(skb->nh.iph->saddr),
	   NIPQUAD(skb->nh.iph->daddr));

	res =  __testip(set,
			ntohl(flags[index] & IPSET_SRC 
				? skb->nh.iph->saddr 
				: skb->nh.iph->daddr),
			hash_ip);
	return (res < 0 ? 0 : res);
}

static inline int
__addip(struct ip_set *set, ip_set_ip_t ip, ip_set_ip_t *hash_ip)
{
	struct ip_set_ipmap *map = (struct ip_set_ipmap *) set->data;

	if (ip < map->first_ip || ip > map->last_ip)
		return -ERANGE;

	*hash_ip = ip & map->netmask;
	DP("%u.%u.%u.%u, %u.%u.%u.%u", HIPQUAD(ip), HIPQUAD(*hash_ip));
	if (test_and_set_bit(ip_to_id(map, *hash_ip), map->members))
		return -EEXIST;

	return 0;
}

static int
addip(struct ip_set *set, const void *data, size_t size,
      ip_set_ip_t *hash_ip)
{
	struct ip_set_req_ipmap *req = 
	    (struct ip_set_req_ipmap *) data;

	if (size != sizeof(struct ip_set_req_ipmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_ipmap),
			      size);
		return -EINVAL;
	}
	DP("%u.%u.%u.%u", HIPQUAD(req->ip));
	return __addip(set, req->ip, hash_ip);
}

static int
addip_kernel(struct ip_set *set, 
	     const struct sk_buff *skb,
	     ip_set_ip_t *hash_ip,
	     const u_int32_t *flags,
	     unsigned char index)
{
	return __addip(set,
		       ntohl(flags[index] & IPSET_SRC 
		       		? skb->nh.iph->saddr 
				: skb->nh.iph->daddr),
		       hash_ip);
}

static inline int 
__delip(struct ip_set *set, ip_set_ip_t ip, ip_set_ip_t *hash_ip)
{
	struct ip_set_ipmap *map = (struct ip_set_ipmap *) set->data;

	if (ip < map->first_ip || ip > map->last_ip)
		return -ERANGE;

	*hash_ip = ip & map->netmask;
	DP("%u.%u.%u.%u, %u.%u.%u.%u", HIPQUAD(ip), HIPQUAD(*hash_ip));
	if (!test_and_clear_bit(ip_to_id(map, *hash_ip), map->members))
		return -EEXIST;
	
	return 0;
}

static int
delip(struct ip_set *set, const void *data, size_t size,
      ip_set_ip_t *hash_ip)
{
	struct ip_set_req_ipmap *req =
	    (struct ip_set_req_ipmap *) data;

	if (size != sizeof(struct ip_set_req_ipmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_ipmap),
			      size);
		return -EINVAL;
	}
	return __delip(set, req->ip, hash_ip);
}

static int
delip_kernel(struct ip_set *set,
	     const struct sk_buff *skb,
	     ip_set_ip_t *hash_ip,
	     const u_int32_t *flags,
	     unsigned char index)
{
	return __delip(set,
		       ntohl(flags[index] & IPSET_SRC 
				? skb->nh.iph->saddr 
				: skb->nh.iph->daddr),
		       hash_ip);
}

static int create(struct ip_set *set, const void *data, size_t size)
{
	int newbytes;
	struct ip_set_req_ipmap_create *req =
	    (struct ip_set_req_ipmap_create *) data;
	struct ip_set_ipmap *map;

	if (size != sizeof(struct ip_set_req_ipmap_create)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_ipmap_create),
			      size);
		return -EINVAL;
	}

	DP("from %u.%u.%u.%u to %u.%u.%u.%u",
	   HIPQUAD(req->from), HIPQUAD(req->to));

	if (req->from > req->to) {
		DP("bad ip range");
		return -ENOEXEC;
	}

	map = kmalloc(sizeof(struct ip_set_ipmap), GFP_KERNEL);
	if (!map) {
		DP("out of memory for %d bytes",
		   sizeof(struct ip_set_ipmap));
		return -ENOMEM;
	}
	map->first_ip = req->from;
	map->last_ip = req->to;
	map->netmask = req->netmask;

	if (req->netmask == 0xFFFFFFFF) {
		map->hosts = 1;
		map->sizeid = map->last_ip - map->first_ip + 1;
	} else {
		unsigned int mask_bits, netmask_bits;
		ip_set_ip_t mask;
		
		map->first_ip &= map->netmask;	/* Should we better bark? */
		
		mask = range_to_mask(map->first_ip, map->last_ip, &mask_bits);
		netmask_bits = mask_to_bits(map->netmask);
		
		if ((!mask && (map->first_ip || map->last_ip != 0xFFFFFFFF))
		    || netmask_bits <= mask_bits)
			return -ENOEXEC;

		DP("mask_bits %u, netmask_bits %u",
		   mask_bits, netmask_bits);
		map->hosts = 2 << (32 - netmask_bits - 1);
		map->sizeid = 2 << (netmask_bits - mask_bits - 1);
	}
	if (map->sizeid > MAX_RANGE + 1) {
		ip_set_printk("range too big (max %d addresses)",
			       MAX_RANGE+1);
		kfree(map);
		return -ENOEXEC;
	}
	DP("hosts %u, sizeid %u", map->hosts, map->sizeid);
	newbytes = bitmap_bytes(0, map->sizeid - 1);
	map->members = kmalloc(newbytes, GFP_KERNEL);
	if (!map->members) {
		DP("out of memory for %d bytes", newbytes);
		kfree(map);
		return -ENOMEM;
	}
	memset(map->members, 0, newbytes);
	
	set->data = map;
	return 0;
}

static void destroy(struct ip_set *set)
{
	struct ip_set_ipmap *map = (struct ip_set_ipmap *) set->data;
	
	kfree(map->members);
	kfree(map);
	
	set->data = NULL;
}

static void flush(struct ip_set *set)
{
	struct ip_set_ipmap *map = (struct ip_set_ipmap *) set->data;
	memset(map->members, 0, bitmap_bytes(0, map->sizeid - 1));
}

static void list_header(const struct ip_set *set, void *data)
{
	struct ip_set_ipmap *map = (struct ip_set_ipmap *) set->data;
	struct ip_set_req_ipmap_create *header =
	    (struct ip_set_req_ipmap_create *) data;

	header->from = map->first_ip;
	header->to = map->last_ip;
	header->netmask = map->netmask;
}

static int list_members_size(const struct ip_set *set)
{
	struct ip_set_ipmap *map = (struct ip_set_ipmap *) set->data;

	return bitmap_bytes(0, map->sizeid - 1);
}

static void list_members(const struct ip_set *set, void *data)
{
	struct ip_set_ipmap *map = (struct ip_set_ipmap *) set->data;
	int bytes = bitmap_bytes(0, map->sizeid - 1);

	memcpy(data, map->members, bytes);
}

static struct ip_set_type ip_set_ipmap = {
	.typename		= SETTYPE_NAME,
	.features		= IPSET_TYPE_IP | IPSET_DATA_SINGLE,
	.protocol_version	= IP_SET_PROTOCOL_VERSION,
	.create			= &create,
	.destroy		= &destroy,
	.flush			= &flush,
	.reqsize		= sizeof(struct ip_set_req_ipmap),
	.addip			= &addip,
	.addip_kernel		= &addip_kernel,
	.delip			= &delip,
	.delip_kernel		= &delip_kernel,
	.testip			= &testip,
	.testip_kernel		= &testip_kernel,
	.header_size		= sizeof(struct ip_set_req_ipmap_create),
	.list_header		= &list_header,
	.list_members_size	= &list_members_size,
	.list_members		= &list_members,
	.me			= THIS_MODULE,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("ipmap type of IP sets");

static int __init init(void)
{
	return ip_set_register_set_type(&ip_set_ipmap);
}

static void __exit fini(void)
{
	/* FIXME: possible race with ip_set_create() */
	ip_set_unregister_set_type(&ip_set_ipmap);
}

module_init(init);
module_exit(fini);

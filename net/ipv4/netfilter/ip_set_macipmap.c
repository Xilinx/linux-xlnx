/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 *                         Martin Josefsson <gandalf@wlug.westbo.se>
 * Copyright (C) 2003-2004 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.  
 */

/* Kernel module implementing an IP set type: the macipmap type */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_set.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <linux/spinlock.h>
#include <linux/if_ether.h>
#include <linux/vmalloc.h>

#include <linux/netfilter_ipv4/ip_set_malloc.h>
#include <linux/netfilter_ipv4/ip_set_macipmap.h>

static int
testip(struct ip_set *set, const void *data, size_t size, ip_set_ip_t *hash_ip)
{
	struct ip_set_macipmap *map = (struct ip_set_macipmap *) set->data;
	struct ip_set_macip *table = (struct ip_set_macip *) map->members;	
	struct ip_set_req_macipmap *req = (struct ip_set_req_macipmap *) data;

	if (size != sizeof(struct ip_set_req_macipmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_macipmap),
			      size);
		return -EINVAL;
	}

	if (req->ip < map->first_ip || req->ip > map->last_ip)
		return -ERANGE;

	*hash_ip = req->ip;
	DP("set: %s, ip:%u.%u.%u.%u, %u.%u.%u.%u",
	   set->name, HIPQUAD(req->ip), HIPQUAD(*hash_ip));		
	if (test_bit(IPSET_MACIP_ISSET,
		     (void *) &table[req->ip - map->first_ip].flags)) {
		return (memcmp(req->ethernet,
			       &table[req->ip - map->first_ip].ethernet,
			       ETH_ALEN) == 0);
	} else {
		return (map->flags & IPSET_MACIP_MATCHUNSET ? 1 : 0);
	}
}

static int
testip_kernel(struct ip_set *set, 
	      const struct sk_buff *skb,
	      ip_set_ip_t *hash_ip,
	      const u_int32_t *flags,
	      unsigned char index)
{
	struct ip_set_macipmap *map =
	    (struct ip_set_macipmap *) set->data;
	struct ip_set_macip *table =
	    (struct ip_set_macip *) map->members;
	ip_set_ip_t ip;
	
	ip = ntohl(flags[index] & IPSET_SRC
			? skb->nh.iph->saddr
			: skb->nh.iph->daddr);
	DP("flag: %s src: %u.%u.%u.%u dst: %u.%u.%u.%u",
	   flags[index] & IPSET_SRC ? "SRC" : "DST",
	   NIPQUAD(skb->nh.iph->saddr),
	   NIPQUAD(skb->nh.iph->daddr));

	if (ip < map->first_ip || ip > map->last_ip)
		return 0;

	*hash_ip = ip;	
	DP("set: %s, ip:%u.%u.%u.%u, %u.%u.%u.%u",
	   set->name, HIPQUAD(ip), HIPQUAD(*hash_ip));		
	if (test_bit(IPSET_MACIP_ISSET,
	    (void *) &table[ip - map->first_ip].flags)) {
		/* Is mac pointer valid?
		 * If so, compare... */
		return (skb->mac.raw >= skb->head
			&& (skb->mac.raw + ETH_HLEN) <= skb->data
			&& (memcmp(eth_hdr(skb)->h_source,
				   &table[ip - map->first_ip].ethernet,
				   ETH_ALEN) == 0));
	} else {
		return (map->flags & IPSET_MACIP_MATCHUNSET ? 1 : 0);
	}
}

/* returns 0 on success */
static inline int
__addip(struct ip_set *set, 
	ip_set_ip_t ip, unsigned char *ethernet, ip_set_ip_t *hash_ip)
{
	struct ip_set_macipmap *map =
	    (struct ip_set_macipmap *) set->data;
	struct ip_set_macip *table =
	    (struct ip_set_macip *) map->members;

	if (ip < map->first_ip || ip > map->last_ip)
		return -ERANGE;
	if (test_and_set_bit(IPSET_MACIP_ISSET, 
			     (void *) &table[ip - map->first_ip].flags))
		return -EEXIST;

	*hash_ip = ip;
	DP("%u.%u.%u.%u, %u.%u.%u.%u", HIPQUAD(ip), HIPQUAD(*hash_ip));
	memcpy(&table[ip - map->first_ip].ethernet, ethernet, ETH_ALEN);
	return 0;
}

static int
addip(struct ip_set *set, const void *data, size_t size,
      ip_set_ip_t *hash_ip)
{
	struct ip_set_req_macipmap *req =
	    (struct ip_set_req_macipmap *) data;

	if (size != sizeof(struct ip_set_req_macipmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_macipmap),
			      size);
		return -EINVAL;
	}
	return __addip(set, req->ip, req->ethernet, hash_ip);
}

static int
addip_kernel(struct ip_set *set, 
	     const struct sk_buff *skb,
	     ip_set_ip_t *hash_ip,
	     const u_int32_t *flags,
	     unsigned char index)
{
	ip_set_ip_t ip;
	
	ip = ntohl(flags[index] & IPSET_SRC
			? skb->nh.iph->saddr
			: skb->nh.iph->daddr);

	if (!(skb->mac.raw >= skb->head
	      && (skb->mac.raw + ETH_HLEN) <= skb->data))
		return -EINVAL;

	return __addip(set, ip, eth_hdr(skb)->h_source, hash_ip);
}

static inline int
__delip(struct ip_set *set, ip_set_ip_t ip, ip_set_ip_t *hash_ip)
{
	struct ip_set_macipmap *map =
	    (struct ip_set_macipmap *) set->data;
	struct ip_set_macip *table =
	    (struct ip_set_macip *) map->members;

	if (ip < map->first_ip || ip > map->last_ip)
		return -ERANGE;
	if (!test_and_clear_bit(IPSET_MACIP_ISSET, 
				(void *)&table[ip - map->first_ip].flags))
		return -EEXIST;

	*hash_ip = ip;
	DP("%u.%u.%u.%u, %u.%u.%u.%u", HIPQUAD(ip), HIPQUAD(*hash_ip));
	return 0;
}

static int
delip(struct ip_set *set, const void *data, size_t size,
     ip_set_ip_t *hash_ip)
{
	struct ip_set_req_macipmap *req =
	    (struct ip_set_req_macipmap *) data;

	if (size != sizeof(struct ip_set_req_macipmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_macipmap),
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

static inline size_t members_size(ip_set_id_t from, ip_set_id_t to)
{
	return (size_t)((to - from + 1) * sizeof(struct ip_set_macip));
}

static int create(struct ip_set *set, const void *data, size_t size)
{
	int newbytes;
	struct ip_set_req_macipmap_create *req =
	    (struct ip_set_req_macipmap_create *) data;
	struct ip_set_macipmap *map;

	if (size != sizeof(struct ip_set_req_macipmap_create)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_macipmap_create),
			      size);
		return -EINVAL;
	}

	DP("from %u.%u.%u.%u to %u.%u.%u.%u",
	   HIPQUAD(req->from), HIPQUAD(req->to));

	if (req->from > req->to) {
		DP("bad ip range");
		return -ENOEXEC;
	}

	if (req->to - req->from > MAX_RANGE) {
		ip_set_printk("range too big (max %d addresses)",
			       MAX_RANGE+1);
		return -ENOEXEC;
	}

	map = kmalloc(sizeof(struct ip_set_macipmap), GFP_KERNEL);
	if (!map) {
		DP("out of memory for %d bytes",
		   sizeof(struct ip_set_macipmap));
		return -ENOMEM;
	}
	map->flags = req->flags;
	map->first_ip = req->from;
	map->last_ip = req->to;
	newbytes = members_size(map->first_ip, map->last_ip);
	map->members = ip_set_malloc(newbytes);
	DP("members: %u %p", newbytes, map->members);
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
	struct ip_set_macipmap *map =
	    (struct ip_set_macipmap *) set->data;

	ip_set_free(map->members, members_size(map->first_ip, map->last_ip));
	kfree(map);

	set->data = NULL;
}

static void flush(struct ip_set *set)
{
	struct ip_set_macipmap *map =
	    (struct ip_set_macipmap *) set->data;
	memset(map->members, 0, members_size(map->first_ip, map->last_ip));
}

static void list_header(const struct ip_set *set, void *data)
{
	struct ip_set_macipmap *map =
	    (struct ip_set_macipmap *) set->data;
	struct ip_set_req_macipmap_create *header =
	    (struct ip_set_req_macipmap_create *) data;

	DP("list_header %x %x %u", map->first_ip, map->last_ip,
	   map->flags);

	header->from = map->first_ip;
	header->to = map->last_ip;
	header->flags = map->flags;
}

static int list_members_size(const struct ip_set *set)
{
	struct ip_set_macipmap *map =
	    (struct ip_set_macipmap *) set->data;

	DP("%u", members_size(map->first_ip, map->last_ip));
	return members_size(map->first_ip, map->last_ip);
}

static void list_members(const struct ip_set *set, void *data)
{
	struct ip_set_macipmap *map =
	    (struct ip_set_macipmap *) set->data;

	int bytes = members_size(map->first_ip, map->last_ip);

	DP("members: %u %p", bytes, map->members);
	memcpy(data, map->members, bytes);
}

static struct ip_set_type ip_set_macipmap = {
	.typename		= SETTYPE_NAME,
	.features		= IPSET_TYPE_IP | IPSET_DATA_SINGLE,
	.protocol_version	= IP_SET_PROTOCOL_VERSION,
	.create			= &create,
	.destroy		= &destroy,
	.flush			= &flush,
	.reqsize		= sizeof(struct ip_set_req_macipmap),
	.addip			= &addip,
	.addip_kernel		= &addip_kernel,
	.delip			= &delip,
	.delip_kernel		= &delip_kernel,
	.testip			= &testip,
	.testip_kernel		= &testip_kernel,
	.header_size		= sizeof(struct ip_set_req_macipmap_create),
	.list_header		= &list_header,
	.list_members_size	= &list_members_size,
	.list_members		= &list_members,
	.me			= THIS_MODULE,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("macipmap type of IP sets");

static int __init init(void)
{
	init_max_malloc_size();
	return ip_set_register_set_type(&ip_set_macipmap);
}

static void __exit fini(void)
{
	/* FIXME: possible race with ip_set_create() */
	ip_set_unregister_set_type(&ip_set_macipmap);
}

module_init(init);
module_exit(fini);

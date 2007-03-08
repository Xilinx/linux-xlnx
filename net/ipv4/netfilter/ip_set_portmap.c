/* Copyright (C) 2003-2004 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.  
 */

/* Kernel module implementing a port set type as a bitmap */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_set.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <linux/spinlock.h>

#include <net/ip.h>

#include <linux/netfilter_ipv4/ip_set_portmap.h>

/* We must handle non-linear skbs */
static inline ip_set_ip_t
get_port(const struct sk_buff *skb, u_int32_t flags)
{
	struct iphdr *iph = skb->nh.iph;
	u_int16_t offset = ntohs(iph->frag_off) & IP_OFFSET;

	switch (iph->protocol) {
	case IPPROTO_TCP: {
		struct tcphdr tcph;
		
		/* See comments at tcp_match in ip_tables.c */
		if (offset)
			return INVALID_PORT;

		if (skb_copy_bits(skb, skb->nh.iph->ihl*4, &tcph, sizeof(tcph)) < 0)
			/* No choice either */
			return INVALID_PORT;
	     	
	     	return ntohs(flags & IPSET_SRC ?
			     tcph.source : tcph.dest);
	    }
	case IPPROTO_UDP: {
		struct udphdr udph;

		if (offset)
			return INVALID_PORT;

		if (skb_copy_bits(skb, skb->nh.iph->ihl*4, &udph, sizeof(udph)) < 0)
			/* No choice either */
			return INVALID_PORT;
	     	
	     	return ntohs(flags & IPSET_SRC ?
			     udph.source : udph.dest);
	    }
	default:
		return INVALID_PORT;
	}
}

static inline int
__testport(struct ip_set *set, ip_set_ip_t port, ip_set_ip_t *hash_port)
{
	struct ip_set_portmap *map = (struct ip_set_portmap *) set->data;

	if (port < map->first_port || port > map->last_port)
		return -ERANGE;
		
	*hash_port = port;
	DP("set: %s, port:%u, %u", set->name, port, *hash_port);
	return !!test_bit(port - map->first_port, map->members);
}

static int
testport(struct ip_set *set, const void *data, size_t size,
         ip_set_ip_t *hash_port)
{
	struct ip_set_req_portmap *req = 
	    (struct ip_set_req_portmap *) data;

	if (size != sizeof(struct ip_set_req_portmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_portmap),
			      size);
		return -EINVAL;
	}
	return __testport(set, req->port, hash_port);
}

static int
testport_kernel(struct ip_set *set, 
	        const struct sk_buff *skb,
	        ip_set_ip_t *hash_port,
	        const u_int32_t *flags,
	        unsigned char index)
{
	int res;
	ip_set_ip_t port = get_port(skb, flags[index]);

	DP("flag %s port %u", flags[index] & IPSET_SRC ? "SRC" : "DST", port);	
	if (port == INVALID_PORT)
		return 0;	

	res =  __testport(set, port, hash_port);
	
	return (res < 0 ? 0 : res);
}

static inline int
__addport(struct ip_set *set, ip_set_ip_t port, ip_set_ip_t *hash_port)
{
	struct ip_set_portmap *map = (struct ip_set_portmap *) set->data;

	if (port < map->first_port || port > map->last_port)
		return -ERANGE;
	if (test_and_set_bit(port - map->first_port, map->members))
		return -EEXIST;
		
	*hash_port = port;
	DP("port %u", port);
	return 0;
}

static int
addport(struct ip_set *set, const void *data, size_t size,
        ip_set_ip_t *hash_port)
{
	struct ip_set_req_portmap *req = 
	    (struct ip_set_req_portmap *) data;

	if (size != sizeof(struct ip_set_req_portmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_portmap),
			      size);
		return -EINVAL;
	}
	return __addport(set, req->port, hash_port);
}

static int
addport_kernel(struct ip_set *set, 
	       const struct sk_buff *skb,
	       ip_set_ip_t *hash_port,
	       const u_int32_t *flags,
	       unsigned char index)
{
	ip_set_ip_t port = get_port(skb, flags[index]);
	
	if (port == INVALID_PORT)
		return -EINVAL;

	return __addport(set, port, hash_port);
}

static inline int
__delport(struct ip_set *set, ip_set_ip_t port, ip_set_ip_t *hash_port)
{
	struct ip_set_portmap *map = (struct ip_set_portmap *) set->data;

	if (port < map->first_port || port > map->last_port)
		return -ERANGE;
	if (!test_and_clear_bit(port - map->first_port, map->members))
		return -EEXIST;
		
	*hash_port = port;
	DP("port %u", port);
	return 0;
}

static int
delport(struct ip_set *set, const void *data, size_t size,
        ip_set_ip_t *hash_port)
{
	struct ip_set_req_portmap *req =
	    (struct ip_set_req_portmap *) data;

	if (size != sizeof(struct ip_set_req_portmap)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_portmap),
			      size);
		return -EINVAL;
	}
	return __delport(set, req->port, hash_port);
}

static int
delport_kernel(struct ip_set *set, 
	       const struct sk_buff *skb,
	       ip_set_ip_t *hash_port,
	       const u_int32_t *flags,
	       unsigned char index)
{
	ip_set_ip_t port = get_port(skb, flags[index]);
	
	if (port == INVALID_PORT)
		return -EINVAL;

	return __delport(set, port, hash_port);
}

static int create(struct ip_set *set, const void *data, size_t size)
{
	int newbytes;
	struct ip_set_req_portmap_create *req =
	    (struct ip_set_req_portmap_create *) data;
	struct ip_set_portmap *map;

	if (size != sizeof(struct ip_set_req_portmap_create)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			       sizeof(struct ip_set_req_portmap_create),
			       size);
		return -EINVAL;
	}

	DP("from %u to %u", req->from, req->to);

	if (req->from > req->to) {
		DP("bad port range");
		return -ENOEXEC;
	}

	if (req->to - req->from > MAX_RANGE) {
		ip_set_printk("range too big (max %d ports)",
			       MAX_RANGE+1);
		return -ENOEXEC;
	}

	map = kmalloc(sizeof(struct ip_set_portmap), GFP_KERNEL);
	if (!map) {
		DP("out of memory for %d bytes",
		   sizeof(struct ip_set_portmap));
		return -ENOMEM;
	}
	map->first_port = req->from;
	map->last_port = req->to;
	newbytes = bitmap_bytes(req->from, req->to);
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
	struct ip_set_portmap *map = (struct ip_set_portmap *) set->data;

	kfree(map->members);
	kfree(map);

	set->data = NULL;
}

static void flush(struct ip_set *set)
{
	struct ip_set_portmap *map = (struct ip_set_portmap *) set->data;
	memset(map->members, 0, bitmap_bytes(map->first_port, map->last_port));
}

static void list_header(const struct ip_set *set, void *data)
{
	struct ip_set_portmap *map = (struct ip_set_portmap *) set->data;
	struct ip_set_req_portmap_create *header =
	    (struct ip_set_req_portmap_create *) data;

	DP("list_header %u %u", map->first_port, map->last_port);

	header->from = map->first_port;
	header->to = map->last_port;
}

static int list_members_size(const struct ip_set *set)
{
	struct ip_set_portmap *map = (struct ip_set_portmap *) set->data;

	return bitmap_bytes(map->first_port, map->last_port);
}

static void list_members(const struct ip_set *set, void *data)
{
	struct ip_set_portmap *map = (struct ip_set_portmap *) set->data;
	int bytes = bitmap_bytes(map->first_port, map->last_port);

	memcpy(data, map->members, bytes);
}

static struct ip_set_type ip_set_portmap = {
	.typename		= SETTYPE_NAME,
	.features		= IPSET_TYPE_PORT | IPSET_DATA_SINGLE,
	.protocol_version	= IP_SET_PROTOCOL_VERSION,
	.create			= &create,
	.destroy		= &destroy,
	.flush			= &flush,
	.reqsize		= sizeof(struct ip_set_req_portmap),
	.addip			= &addport,
	.addip_kernel		= &addport_kernel,
	.delip			= &delport,
	.delip_kernel		= &delport_kernel,
	.testip			= &testport,
	.testip_kernel		= &testport_kernel,
	.header_size		= sizeof(struct ip_set_req_portmap_create),
	.list_header		= &list_header,
	.list_members_size	= &list_members_size,
	.list_members		= &list_members,
	.me			= THIS_MODULE,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("portmap type of IP sets");

static int __init init(void)
{
	return ip_set_register_set_type(&ip_set_portmap);
}

static void __exit fini(void)
{
	/* FIXME: possible race with ip_set_create() */
	ip_set_unregister_set_type(&ip_set_portmap);
}

module_init(init);
module_exit(fini);

/* Copyright (C) 2006 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.  
 */

/* Kernel module implementing the union of sets */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_set.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#include <net/ip.h>

#define ASSERT_READ_LOCK(x)     /* dont use that */
#define ASSERT_WRITE_LOCK(x)
#include <linux/netfilter_ipv4/listhelp.h>
#include <linux/netfilter_ipv4/ip_set_malloc.h>
#include <linux/netfilter_ipv4/ip_set_union.h>

/* Two-headed animal: 
 * from userspace, we add/del/test the individual sets;
 * in kernel space we test the elements in the sets. */

static inline int 
find_set(struct list_head *members, ip_set_id_t index)
{
	struct ip_set_union_elem *elem;

	list_for_each_entry(elem, members, list) {
		if (elem->index == index)
			return 1;
	}
	return 0;
}

static int
testip(struct ip_set *set, const void *data, size_t size,
       ip_set_ip_t *hash_ip)
{
	struct ip_set_req_union *req = 
	    (struct ip_set_req_union *) data;
	struct ip_set_union *set_union = 
		(struct ip_set_union *) set->data;
	ip_set_id_t index;
	int ret = 0;
	
	if (size != sizeof(struct ip_set_req_union)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_union),
			      size);
		return -EINVAL;
	}
	index = __ip_set_get_byname(req->name);
	if (index == IP_SET_INVALID_ID)
		return 0;
	
	if (find_set(&set_union->members, index)) {
		*hash_ip = (ip_set_ip_t) index;
		ret = 1;
	}
	__ip_set_put_byindex(index);
	return ret;
}

static int
testip_kernel(struct ip_set *set, 
	      const struct sk_buff *skb,
	      ip_set_ip_t *hash_ip,
	      const u_int32_t *flags,
	      unsigned char index)
{
	struct ip_set_union *set_union = 
		(struct ip_set_union *) set->data;
	struct ip_set_union_elem *elem;

	list_for_each_entry(elem, &set_union->members, list) {
		if (ip_set_testip_kernel(elem->index, skb, flags))
			return 1;
	}
	return 0;
}

static int
addip(struct ip_set *set, const void *data, size_t size,
        ip_set_ip_t *hash_ip)
{
	struct ip_set_req_union *req = 
	    (struct ip_set_req_union *) data;
	struct ip_set_union *set_union = 
		(struct ip_set_union *) set->data;
	ip_set_id_t index;
	struct ip_set_union_elem *elem;

	if (size != sizeof(struct ip_set_req_union)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_union),
			      size);
		return -EINVAL;
	}
	index = __ip_set_get_byname(req->name);
	if (index == IP_SET_INVALID_ID)
		return -ENOENT;
	
	if (find_set(&set_union->members, index)) {
		__ip_set_put_byindex(index);
		return -EEXIST;
	}

	/* Release refcount at del */
	elem = kmalloc(sizeof(struct ip_set_union), GFP_ATOMIC);
	if (elem == NULL)
		return -ENOMEM;
	elem->index = index;
	list_append(&set_union->members, &elem->list);
	*hash_ip = (ip_set_ip_t) index;
	return 0;
}

static int
addip_kernel(struct ip_set *set, 
	     const struct sk_buff *skb,
	     ip_set_ip_t *hash_ip,
	     const u_int32_t *flags,
	     unsigned char index)
{
	/* One must add elements to the members. */
	return 0;
}

static int
delip(struct ip_set *set, const void *data, size_t size,
        ip_set_ip_t *hash_ip)
{
	struct ip_set_req_union *req = 
	    (struct ip_set_req_union *) data;
	struct ip_set_union *set_union = 
		(struct ip_set_union *) set->data;
	ip_set_id_t index;
	struct ip_set_union_elem *elem = NULL, *i;

	if (size != sizeof(struct ip_set_req_union)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_union),
			      size);
		return -EINVAL;
	}
	index = __ip_set_get_byname(req->name);
	if (index == IP_SET_INVALID_ID)
		return -ENOENT;

	list_for_each_entry(i, &set_union->members, list) {
		if (i->index == index) {
			elem = i;
			break;
		}
	}
	__ip_set_put_byindex(index);

	if (!elem)
		return -EEXIST;

	/* Release refcount */
	__ip_set_put_byindex(index);
	list_del(&elem->list);
	kfree(elem);
	*hash_ip = (ip_set_ip_t) index;
	return 0;
}

static int
delip_kernel(struct ip_set *set, 
	     const struct sk_buff *skb,
	     ip_set_ip_t *hash_ip,
	     const u_int32_t *flags,
	     unsigned char index)
{
	/* One must delete elements to the members. */
	return 0;
}

static int create(struct ip_set *set, const void *data, size_t size)
{
	struct ip_set_union *map;

	if (size != sizeof(struct ip_set_req_union_create)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			       sizeof(struct ip_set_req_union_create),
			       size);
		return -EINVAL;
	}

	map = kmalloc(sizeof(struct ip_set_union), GFP_KERNEL);
	if (!map) {
		DP("out of memory for %d bytes",
		   sizeof(struct ip_set_union));
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&map->members);
	set->data = map;
	return 0;
}

static void flush(struct ip_set *set)
{
	struct ip_set_union *map = (struct ip_set_union *) set->data;
	struct ip_set_union_elem *i, *tmp;
	
	list_for_each_entry_safe(i, tmp, &map->members, list) {
		__ip_set_put_byindex(i->index);
		list_del(&i->list);
		kfree(i);
	}
}

static void destroy(struct ip_set *set)
{
	struct ip_set_union *map = (struct ip_set_union *) set->data;

	flush(set);
	kfree(map);

	set->data = NULL;
}

static void list_header(const struct ip_set *set, void *data)
{
}

static int list_members_size(const struct ip_set *set)
{
	struct ip_set_union *map = (struct ip_set_union *) set->data;
	int size = 0;
	struct ip_set_union_elem *elem;

	list_for_each_entry(elem, &map->members, list) {
		size += sizeof(ip_set_id_t);
	}

	return size;
}

static void list_members(const struct ip_set *set, void *data)
{
	struct ip_set_union *map = (struct ip_set_union *) set->data;
	struct ip_set_union_elem *elem;
	ip_set_id_t *members = (ip_set_id_t *) data;
	int i = 0;

	list_for_each_entry(elem, &map->members, list) {
		members[i++] = elem->index;
	}
}

static struct ip_set_type ip_set_union = {
	.typename		= SETTYPE_NAME,
	/* Meaningless: */
	.features		= IPSET_TYPE_IP | IPSET_TYPE_PORT 
				  | IPSET_DATA_SINGLE | IPSET_DATA_DOUBLE,
	.protocol_version	= IP_SET_PROTOCOL_VERSION,
	.create			= &create,
	.destroy		= &destroy,
	.flush			= &flush,
	.reqsize		= sizeof(struct ip_set_req_union),
	.addip			= &addip,
	.addip_kernel		= &addip_kernel,
	.retry			= NULL,
	.delip			= &delip,
	.delip_kernel		= &delip_kernel,
	.testip			= &testip,
	.testip_kernel		= &testip_kernel,
	.header_size		= sizeof(struct ip_set_req_union_create),
	.list_header		= &list_header,
	.list_members_size	= &list_members_size,
	.list_members		= &list_members,
	.me			= THIS_MODULE,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("union type of IP sets");

static int __init init(void)
{
	return ip_set_register_set_type(&ip_set_union);
}

static void __exit fini(void)
{
	/* FIXME: possible race with ip_set_create() */
	ip_set_unregister_set_type(&ip_set_union);
}

module_init(init);
module_exit(fini);

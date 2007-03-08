/* Copyright (C) 2005 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.  
 */

/* Kernel module implementing an IP set type: the iptree type */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_set.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <linux/spinlock.h>

/* Backward compatibility */
#ifndef __nocast
#define __nocast
#endif

#include <linux/netfilter_ipv4/ip_set_iptree.h>

static int limit = MAX_RANGE;

/* Garbage collection interval in seconds: */
#define IPTREE_GC_TIME		5*60
/* Sleep so many milliseconds before trying again 
 * to delete the gc timer at destroying/flushing a set */ 
#define IPTREE_DESTROY_SLEEP	100

static kmem_cache_t *branch_cachep;
static kmem_cache_t *leaf_cachep;

#define ABCD(a,b,c,d,addrp) do {		\
	a = ((unsigned char *)addrp)[3];	\
	b = ((unsigned char *)addrp)[2];	\
	c = ((unsigned char *)addrp)[1];	\
	d = ((unsigned char *)addrp)[0];	\
} while (0)

#define TESTIP_WALK(map, elem, branch) do {	\
	if ((map)->tree[elem]) {		\
		branch = (map)->tree[elem];	\
	} else 					\
		return 0;			\
} while (0)

static inline int
__testip(struct ip_set *set, ip_set_ip_t ip, ip_set_ip_t *hash_ip)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	struct ip_set_iptreeb *btree;
	struct ip_set_iptreec *ctree;
	struct ip_set_iptreed *dtree;
	unsigned char a,b,c,d;

	if (!ip)
		return -ERANGE;
	
	*hash_ip = ip;
	ABCD(a, b, c, d, hash_ip);
	DP("%u %u %u %u timeout %u", a, b, c, d, map->timeout);
	TESTIP_WALK(map, a, btree);
	TESTIP_WALK(btree, b, ctree);
	TESTIP_WALK(ctree, c, dtree);
	DP("%lu %lu", dtree->expires[d], jiffies);
	return !!(map->timeout ? (time_after(dtree->expires[d], jiffies))
			       : dtree->expires[d]);
}

static int
testip(struct ip_set *set, const void *data, size_t size,
       ip_set_ip_t *hash_ip)
{
	struct ip_set_req_iptree *req = 
	    (struct ip_set_req_iptree *) data;

	if (size != sizeof(struct ip_set_req_iptree)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_iptree),
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

#define ADDIP_WALK(map, elem, branch, type, cachep, flags) do {	\
	if ((map)->tree[elem]) {				\
		DP("found %u", elem);				\
		branch = (map)->tree[elem];			\
	} else {						\
		branch = (type *)				\
			kmem_cache_alloc(cachep, flags);	\
		if (branch == NULL)				\
			return -ENOMEM;				\
		memset(branch, 0, sizeof(*branch));		\
		(map)->tree[elem] = branch;			\
		DP("alloc %u", elem);				\
	}							\
} while (0)	

static inline int
__addip(struct ip_set *set, ip_set_ip_t ip, unsigned int timeout,
	ip_set_ip_t *hash_ip,
	unsigned int __nocast flags)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	struct ip_set_iptreeb *btree;
	struct ip_set_iptreec *ctree;
	struct ip_set_iptreed *dtree;
	unsigned char a,b,c,d;
	int ret = 0;
	
	if (!ip || map->elements > limit)
		/* We could call the garbage collector
		 * but it's probably overkill */
		return -ERANGE;
	
	*hash_ip = ip;
	ABCD(a, b, c, d, hash_ip);
	DP("%u %u %u %u timeout %u", a, b, c, d, timeout);
	ADDIP_WALK(map, a, btree, struct ip_set_iptreeb, branch_cachep, flags);
	ADDIP_WALK(btree, b, ctree, struct ip_set_iptreec, branch_cachep, flags);
	ADDIP_WALK(ctree, c, dtree, struct ip_set_iptreed, leaf_cachep, flags);
	if (dtree->expires[d]
	    && (!map->timeout || time_after(dtree->expires[d], jiffies)))
	    	ret = -EEXIST;
	dtree->expires[d] = map->timeout ? (timeout * HZ + jiffies) : 1;
	/* Lottery */
	if (dtree->expires[d] == 0)
		dtree->expires[d] = 1;
	DP("%u %lu", d, dtree->expires[d]);
	if (ret == 0)
		map->elements++;
	return ret;
}

static int
addip(struct ip_set *set, const void *data, size_t size,
      ip_set_ip_t *hash_ip)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	struct ip_set_req_iptree *req = 
		(struct ip_set_req_iptree *) data;

	if (size != sizeof(struct ip_set_req_iptree)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_iptree),
			      size);
		return -EINVAL;
	}
	DP("%u.%u.%u.%u %u", HIPQUAD(req->ip), req->timeout);
	return __addip(set, req->ip,
		       req->timeout ? req->timeout : map->timeout,
		       hash_ip,
		       GFP_ATOMIC);
}

static int
addip_kernel(struct ip_set *set, 
	     const struct sk_buff *skb,
	     ip_set_ip_t *hash_ip,
	     const u_int32_t *flags,
	     unsigned char index)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;

	return __addip(set,
		       ntohl(flags[index] & IPSET_SRC 
		       		? skb->nh.iph->saddr 
				: skb->nh.iph->daddr),
		       map->timeout,
		       hash_ip,
		       GFP_ATOMIC);
}

#define DELIP_WALK(map, elem, branch) do {	\
	if ((map)->tree[elem]) {		\
		branch = (map)->tree[elem];	\
	} else 					\
		return -EEXIST;			\
} while (0)

static inline int 
__delip(struct ip_set *set, ip_set_ip_t ip, ip_set_ip_t *hash_ip)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	struct ip_set_iptreeb *btree;
	struct ip_set_iptreec *ctree;
	struct ip_set_iptreed *dtree;
	unsigned char a,b,c,d;
	
	if (!ip)
		return -ERANGE;
		
	*hash_ip = ip;
	ABCD(a, b, c, d, hash_ip);
	DELIP_WALK(map, a, btree);
	DELIP_WALK(btree, b, ctree);
	DELIP_WALK(ctree, c, dtree);

	if (dtree->expires[d]) {
		dtree->expires[d] = 0;
		map->elements--;
		return 0;
	}
	return -EEXIST;
}

static int
delip(struct ip_set *set, const void *data, size_t size,
      ip_set_ip_t *hash_ip)
{
	struct ip_set_req_iptree *req =
	    (struct ip_set_req_iptree *) data;

	if (size != sizeof(struct ip_set_req_iptree)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_iptree),
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

#define LOOP_WALK_BEGIN(map, i, branch) \
	for (i = 0; i < 256; i++) {	\
		if (!(map)->tree[i])	\
			continue;	\
		branch = (map)->tree[i]

#define LOOP_WALK_END }

static void ip_tree_gc(unsigned long ul_set)
{
	struct ip_set *set = (void *) ul_set;
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	struct ip_set_iptreeb *btree;
	struct ip_set_iptreec *ctree;
	struct ip_set_iptreed *dtree;
	unsigned int a,b,c,d;
	unsigned char i,j,k;

	i = j = k = 0;
	DP("gc: %s", set->name);
	write_lock_bh(&set->lock);
	LOOP_WALK_BEGIN(map, a, btree);
	LOOP_WALK_BEGIN(btree, b, ctree);
	LOOP_WALK_BEGIN(ctree, c, dtree);
	for (d = 0; d < 256; d++) {
		if (dtree->expires[d]) {
			DP("gc: %u %u %u %u: expires %lu jiffies %lu",
			    a, b, c, d,
			    dtree->expires[d], jiffies);
			if (map->timeout
			    && time_before(dtree->expires[d], jiffies)) {
			    	dtree->expires[d] = 0;
			    	map->elements--;
			} else
				k = 1;
		}
	}
	if (k == 0) {
		DP("gc: %s: leaf %u %u %u empty",
		    set->name, a, b, c);
		kmem_cache_free(leaf_cachep, dtree);
		ctree->tree[c] = NULL;
	} else {
		DP("gc: %s: leaf %u %u %u not empty",
		    set->name, a, b, c);
		j = 1;
		k = 0;
	}
	LOOP_WALK_END;
	if (j == 0) {
		DP("gc: %s: branch %u %u empty",
		    set->name, a, b);
		kmem_cache_free(branch_cachep, ctree);
		btree->tree[b] = NULL;
	} else {
		DP("gc: %s: branch %u %u not empty",
		    set->name, a, b);
		i = 1;
		j = k = 0;
	}
	LOOP_WALK_END;
	if (i == 0) {
		DP("gc: %s: branch %u empty",
		    set->name, a);
		kmem_cache_free(branch_cachep, btree);
		map->tree[a] = NULL;
	} else {
		DP("gc: %s: branch %u not empty",
		    set->name, a);
		i = j = k = 0;
	}
	LOOP_WALK_END;
	write_unlock_bh(&set->lock);
	
	map->gc.expires = jiffies + map->gc_interval * HZ;
	add_timer(&map->gc);
}

static inline void init_gc_timer(struct ip_set *set)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;

	/* Even if there is no timeout for the entries,
	 * we still have to call gc because delete
	 * do not clean up empty branches */
	map->gc_interval = IPTREE_GC_TIME;
	init_timer(&map->gc);
	map->gc.data = (unsigned long) set;
	map->gc.function = ip_tree_gc;
	map->gc.expires = jiffies + map->gc_interval * HZ;
	add_timer(&map->gc);
}

static int create(struct ip_set *set, const void *data, size_t size)
{
	struct ip_set_req_iptree_create *req =
	    (struct ip_set_req_iptree_create *) data;
	struct ip_set_iptree *map;

	if (size != sizeof(struct ip_set_req_iptree_create)) {
		ip_set_printk("data length wrong (want %zu, have %zu)",
			      sizeof(struct ip_set_req_iptree_create),
			      size);
		return -EINVAL;
	}

	map = kmalloc(sizeof(struct ip_set_iptree), GFP_KERNEL);
	if (!map) {
		DP("out of memory for %d bytes",
		   sizeof(struct ip_set_iptree));
		return -ENOMEM;
	}
	memset(map, 0, sizeof(*map));
	map->timeout = req->timeout;
	map->elements = 0;
	set->data = map;

	init_gc_timer(set);

	return 0;
}

static void __flush(struct ip_set_iptree *map)
{
	struct ip_set_iptreeb *btree;
	struct ip_set_iptreec *ctree;
	struct ip_set_iptreed *dtree;
	unsigned int a,b,c;

	LOOP_WALK_BEGIN(map, a, btree);
	LOOP_WALK_BEGIN(btree, b, ctree);
	LOOP_WALK_BEGIN(ctree, c, dtree);
	kmem_cache_free(leaf_cachep, dtree);
	LOOP_WALK_END;
	kmem_cache_free(branch_cachep, ctree);
	LOOP_WALK_END;
	kmem_cache_free(branch_cachep, btree);
	LOOP_WALK_END;
	map->elements = 0;
}

static void destroy(struct ip_set *set)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;

	/* gc might be running */
	while (!del_timer(&map->gc))
		msleep(IPTREE_DESTROY_SLEEP);
	__flush(map);
	kfree(map);
	set->data = NULL;
}

static void flush(struct ip_set *set)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	unsigned int timeout = map->timeout;
	
	/* gc might be running */
	while (!del_timer(&map->gc))
		msleep(IPTREE_DESTROY_SLEEP);
	__flush(map);
	memset(map, 0, sizeof(*map));
	map->timeout = timeout;

	init_gc_timer(set);
}

static void list_header(const struct ip_set *set, void *data)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	struct ip_set_req_iptree_create *header =
	    (struct ip_set_req_iptree_create *) data;

	header->timeout = map->timeout;
}

static int list_members_size(const struct ip_set *set)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	struct ip_set_iptreeb *btree;
	struct ip_set_iptreec *ctree;
	struct ip_set_iptreed *dtree;
	unsigned int a,b,c,d;
	unsigned int count = 0;

	LOOP_WALK_BEGIN(map, a, btree);
	LOOP_WALK_BEGIN(btree, b, ctree);
	LOOP_WALK_BEGIN(ctree, c, dtree);
	for (d = 0; d < 256; d++) {
		if (dtree->expires[d]
		    && (!map->timeout || time_after(dtree->expires[d], jiffies)))
		    	count++;
	}
	LOOP_WALK_END;
	LOOP_WALK_END;
	LOOP_WALK_END;

	DP("members %u", count);
	return (count * sizeof(struct ip_set_req_iptree));
}

static void list_members(const struct ip_set *set, void *data)
{
	struct ip_set_iptree *map = (struct ip_set_iptree *) set->data;
	struct ip_set_iptreeb *btree;
	struct ip_set_iptreec *ctree;
	struct ip_set_iptreed *dtree;
	unsigned int a,b,c,d;
	size_t offset = 0;
	struct ip_set_req_iptree *entry;

	LOOP_WALK_BEGIN(map, a, btree);
	LOOP_WALK_BEGIN(btree, b, ctree);
	LOOP_WALK_BEGIN(ctree, c, dtree);
	for (d = 0; d < 256; d++) {
		if (dtree->expires[d]
		    && (!map->timeout || time_after(dtree->expires[d], jiffies))) {
		    	entry = (struct ip_set_req_iptree *)(data + offset);
		    	entry->ip = ((a << 24) | (b << 16) | (c << 8) | d);
		    	entry->timeout = !map->timeout ? 0 
				: (dtree->expires[d] - jiffies)/HZ;
			offset += sizeof(struct ip_set_req_iptree);
		}
	}
	LOOP_WALK_END;
	LOOP_WALK_END;
	LOOP_WALK_END;
}

static struct ip_set_type ip_set_iptree = {
	.typename		= SETTYPE_NAME,
	.features		= IPSET_TYPE_IP | IPSET_DATA_SINGLE,
	.protocol_version	= IP_SET_PROTOCOL_VERSION,
	.create			= &create,
	.destroy		= &destroy,
	.flush			= &flush,
	.reqsize		= sizeof(struct ip_set_req_iptree),
	.addip			= &addip,
	.addip_kernel		= &addip_kernel,
	.delip			= &delip,
	.delip_kernel		= &delip_kernel,
	.testip			= &testip,
	.testip_kernel		= &testip_kernel,
	.header_size		= sizeof(struct ip_set_req_iptree_create),
	.list_header		= &list_header,
	.list_members_size	= &list_members_size,
	.list_members		= &list_members,
	.me			= THIS_MODULE,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("iptree type of IP sets");
module_param(limit, int, 0600);
MODULE_PARM_DESC(limit, "maximal number of elements stored in the sets");

static int __init init(void)
{
	int ret;
	
	branch_cachep = kmem_cache_create("ip_set_iptreeb",
				sizeof(struct ip_set_iptreeb),
				0, 0, NULL, NULL);
	if (!branch_cachep) {
		printk(KERN_ERR "Unable to create ip_set_iptreeb slab cache\n");
		ret = -ENOMEM;
		goto out;
	}
	leaf_cachep = kmem_cache_create("ip_set_iptreed",
				sizeof(struct ip_set_iptreed),
				0, 0, NULL, NULL);
	if (!leaf_cachep) {
		printk(KERN_ERR "Unable to create ip_set_iptreed slab cache\n");
		ret = -ENOMEM;
		goto free_branch;
	}
	ret = ip_set_register_set_type(&ip_set_iptree);
	if (ret == 0)
		goto out;

	kmem_cache_destroy(leaf_cachep);
    free_branch:	
	kmem_cache_destroy(branch_cachep);
    out:
	return ret;
}

static void __exit fini(void)
{
	/* FIXME: possible race with ip_set_create() */
	ip_set_unregister_set_type(&ip_set_iptree);
	kmem_cache_destroy(leaf_cachep);
	kmem_cache_destroy(branch_cachep);
}

module_init(init);
module_exit(fini);

/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 * Copyright (C) 2003-2004 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.  
 */

/* Kernel module for IP set management */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kmod.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/semaphore.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#define ASSERT_READ_LOCK(x)	/* dont use that */
#define ASSERT_WRITE_LOCK(x)
#include <linux/netfilter_ipv4/listhelp.h>
#include <linux/netfilter_ipv4/ip_set.h>

static struct list_head set_type_list;		/* all registered sets */
static struct ip_set **ip_set_list;		/* all individual sets */
static DEFINE_RWLOCK(ip_set_lock);		/* protects the lists and the hash */
static DECLARE_MUTEX(ip_set_app_mutex);		/* serializes user access */
static ip_set_id_t ip_set_max = CONFIG_IP_NF_SET_MAX;
static ip_set_id_t ip_set_bindings_hash_size =  CONFIG_IP_NF_SET_HASHSIZE;
static struct list_head *ip_set_hash;		/* hash of bindings */
static unsigned int ip_set_hash_random;		/* random seed */

/*
 * Sets are identified either by the index in ip_set_list or by id.
 * The id never changes and is used to find a key in the hash. 
 * The index may change by swapping and used at all other places 
 * (set/SET netfilter modules, binding value, etc.)
 *
 * Userspace requests are serialized by ip_set_mutex and sets can
 * be deleted only from userspace. Therefore ip_set_list locking 
 * must obey the following rules:
 *
 * - kernel requests: read and write locking mandatory
 * - user requests: read locking optional, write locking mandatory
 */

static inline void
__ip_set_get(ip_set_id_t index)
{
	atomic_inc(&ip_set_list[index]->ref);
}

static inline void
__ip_set_put(ip_set_id_t index)
{
	atomic_dec(&ip_set_list[index]->ref);
}

/*
 * Binding routines
 */

static inline int
ip_hash_cmp(const struct ip_set_hash *set_hash,
	    ip_set_id_t id, ip_set_ip_t ip)
{
	return set_hash->id == id && set_hash->ip == ip;
}

static ip_set_id_t
ip_set_find_in_hash(ip_set_id_t id, ip_set_ip_t ip)
{
	u_int32_t key = jhash_2words(id, ip, ip_set_hash_random) 
				% ip_set_bindings_hash_size;
	struct ip_set_hash *set_hash;

	ASSERT_READ_LOCK(&ip_set_lock);
	IP_SET_ASSERT(ip_set_list[id]);
	DP("set: %s, ip: %u.%u.%u.%u", ip_set_list[id]->name, HIPQUAD(ip));	
	
	set_hash = LIST_FIND(&ip_set_hash[key], ip_hash_cmp,
			     struct ip_set_hash *, id, ip);
	
	DP("set: %s, ip: %u.%u.%u.%u, binding: %s", ip_set_list[id]->name, 
	   HIPQUAD(ip),
	   set_hash != NULL ? ip_set_list[set_hash->binding]->name : "");

	return (set_hash != NULL ? set_hash->binding : IP_SET_INVALID_ID);
}

static inline void 
__set_hash_del(struct ip_set_hash *set_hash)
{
	ASSERT_WRITE_LOCK(&ip_set_lock);
	IP_SET_ASSERT(ip_set_list[set_hash->binding]);	

	__ip_set_put(set_hash->binding);
	list_del(&set_hash->list);
	kfree(set_hash);
}

static int
ip_set_hash_del(ip_set_id_t id, ip_set_ip_t ip)
{
	u_int32_t key = jhash_2words(id, ip, ip_set_hash_random)
				% ip_set_bindings_hash_size;
	struct ip_set_hash *set_hash;
	
	IP_SET_ASSERT(ip_set_list[id]);
	DP("set: %s, ip: %u.%u.%u.%u", ip_set_list[id]->name, HIPQUAD(ip));	
	write_lock_bh(&ip_set_lock);
	set_hash = LIST_FIND(&ip_set_hash[key], ip_hash_cmp,
			     struct ip_set_hash *, id, ip);
	DP("set: %s, ip: %u.%u.%u.%u, binding: %s", ip_set_list[id]->name,
	   HIPQUAD(ip),
	   set_hash != NULL ? ip_set_list[set_hash->binding]->name : "");

	if (set_hash != NULL)
		__set_hash_del(set_hash);
	write_unlock_bh(&ip_set_lock);
	return 0;
}

static int 
ip_set_hash_add(ip_set_id_t id, ip_set_ip_t ip, ip_set_id_t binding)
{
	u_int32_t key = jhash_2words(id, ip, ip_set_hash_random)
				% ip_set_bindings_hash_size;
	struct ip_set_hash *set_hash;
	int ret = 0;
	
	IP_SET_ASSERT(ip_set_list[id]);
	IP_SET_ASSERT(ip_set_list[binding]);
	DP("set: %s, ip: %u.%u.%u.%u, binding: %s", ip_set_list[id]->name, 
	   HIPQUAD(ip), ip_set_list[binding]->name);
	write_lock_bh(&ip_set_lock);
	set_hash = LIST_FIND(&ip_set_hash[key], ip_hash_cmp,
			     struct ip_set_hash *, id, ip);
	if (!set_hash) {
		set_hash = kmalloc(sizeof(struct ip_set_hash), GFP_KERNEL);
		if (!set_hash) {
			ret = -ENOMEM;
			goto unlock;
		}
		INIT_LIST_HEAD(&set_hash->list);
		set_hash->id = id;
		set_hash->ip = ip;
		list_add(&ip_set_hash[key], &set_hash->list);
	} else {
		IP_SET_ASSERT(ip_set_list[set_hash->binding]);	
		DP("overwrite binding: %s",
		   ip_set_list[set_hash->binding]->name);
		__ip_set_put(set_hash->binding);
	}
	set_hash->binding = binding;
	__ip_set_get(set_hash->binding);
    unlock:
	write_unlock_bh(&ip_set_lock);
	return ret;
}

#define FOREACH_HASH_DO(fn, args...) 						\
({										\
	ip_set_id_t __key;							\
	struct ip_set_hash *__set_hash;						\
										\
	for (__key = 0; __key < ip_set_bindings_hash_size; __key++) {		\
		list_for_each_entry(__set_hash, &ip_set_hash[__key], list)	\
			fn(__set_hash , ## args);				\
	}									\
})

#define FOREACH_HASH_RW_DO(fn, args...) 						\
({										\
	ip_set_id_t __key;							\
	struct ip_set_hash *__set_hash, *__n;					\
										\
	ASSERT_WRITE_LOCK(&ip_set_lock);					\
	for (__key = 0; __key < ip_set_bindings_hash_size; __key++) {		\
		list_for_each_entry_safe(__set_hash, __n, &ip_set_hash[__key], list)\
			fn(__set_hash , ## args);				\
	}									\
})

/* Add, del and test set entries from kernel */

#define follow_bindings(index, set, ip)					\
((index = ip_set_find_in_hash((set)->id, ip)) != IP_SET_INVALID_ID	\
 || (index = (set)->binding) != IP_SET_INVALID_ID)

int
ip_set_testip_kernel(ip_set_id_t index,
		     const struct sk_buff *skb,
		     const u_int32_t *flags)
{
	struct ip_set *set;
	ip_set_ip_t ip;
	int res;
	unsigned char i = 0;
	
	IP_SET_ASSERT(flags[i]);
	read_lock_bh(&ip_set_lock);
	do {
		set = ip_set_list[index];
		IP_SET_ASSERT(set);
		DP("set %s, index %u", set->name, index);
		read_lock_bh(&set->lock);
		res = set->type->testip_kernel(set, skb, &ip, flags, i++);
		read_unlock_bh(&set->lock);
		i += !!(set->type->features & IPSET_DATA_DOUBLE);
	} while (res > 0 
		 && flags[i] 
		 && follow_bindings(index, set, ip));
	read_unlock_bh(&ip_set_lock);

	return res;
}

void
ip_set_addip_kernel(ip_set_id_t index,
		    const struct sk_buff *skb,
		    const u_int32_t *flags)
{
	struct ip_set *set;
	ip_set_ip_t ip;
	int res;
	unsigned char i = 0;

	IP_SET_ASSERT(flags[i]);
   retry:
	read_lock_bh(&ip_set_lock);
	do {
		set = ip_set_list[index];
		IP_SET_ASSERT(set);
		DP("set %s, index %u", set->name, index);
		write_lock_bh(&set->lock);
		res = set->type->addip_kernel(set, skb, &ip, flags, i++);
		write_unlock_bh(&set->lock);
		i += !!(set->type->features & IPSET_DATA_DOUBLE);
	} while ((res == 0 || res == -EEXIST)
		 && flags[i] 
		 && follow_bindings(index, set, ip));
	read_unlock_bh(&ip_set_lock);

	if (res == -EAGAIN
	    && set->type->retry
	    && (res = set->type->retry(set)) == 0)
	    	goto retry;
}

void
ip_set_delip_kernel(ip_set_id_t index,
		    const struct sk_buff *skb,
		    const u_int32_t *flags)
{
	struct ip_set *set;
	ip_set_ip_t ip;
	int res;
	unsigned char i = 0;

	IP_SET_ASSERT(flags[i]);
	read_lock_bh(&ip_set_lock);
	do {
		set = ip_set_list[index];
		IP_SET_ASSERT(set);
		DP("set %s, index %u", set->name, index);
		write_lock_bh(&set->lock);
		res = set->type->delip_kernel(set, skb, &ip, flags, i++);
		write_unlock_bh(&set->lock);
		i += !!(set->type->features & IPSET_DATA_DOUBLE);
	} while ((res == 0 || res == -EEXIST)
		 && flags[i] 
		 && follow_bindings(index, set, ip));
	read_unlock_bh(&ip_set_lock);
}

/* Register and deregister settype */

static inline int
set_type_equal(const struct ip_set_type *set_type, const char *str2)
{
	return !strncmp(set_type->typename, str2, IP_SET_MAXNAMELEN - 1);
}

static inline struct ip_set_type *
find_set_type(const char *name)
{
	return LIST_FIND(&set_type_list,
			 set_type_equal,
			 struct ip_set_type *,
			 name);
}

int 
ip_set_register_set_type(struct ip_set_type *set_type)
{
	int ret = 0;
	
	if (set_type->protocol_version != IP_SET_PROTOCOL_VERSION) {
		ip_set_printk("'%s' uses wrong protocol version %u (want %u)",
			      set_type->typename,
			      set_type->protocol_version,
			      IP_SET_PROTOCOL_VERSION);
		return -EINVAL;
	}

	write_lock_bh(&ip_set_lock);
	if (find_set_type(set_type->typename)) {
		/* Duplicate! */
		ip_set_printk("'%s' already registered!", 
			      set_type->typename);
		ret = -EINVAL;
		goto unlock;
	}
	if (!try_module_get(THIS_MODULE)) {
		ret = -EFAULT;
		goto unlock;
	}
	list_append(&set_type_list, set_type);
	DP("'%s' registered.", set_type->typename);
   unlock:
	write_unlock_bh(&ip_set_lock);
	return ret;
}

void
ip_set_unregister_set_type(struct ip_set_type *set_type)
{
	write_lock_bh(&ip_set_lock);
	if (!find_set_type(set_type->typename)) {
		ip_set_printk("'%s' not registered?",
			      set_type->typename);
		goto unlock;
	}
	LIST_DELETE(&set_type_list, set_type);
	module_put(THIS_MODULE);
	DP("'%s' unregistered.", set_type->typename);
   unlock:
	write_unlock_bh(&ip_set_lock);

}

/*
 * Userspace routines
 */

/*
 * Find set by name, reference it once. The reference makes sure the
 * thing pointed to, does not go away under our feet. Drop the reference
 * later, using ip_set_put().
 */
ip_set_id_t
ip_set_get_byname(const char *name)
{
	ip_set_id_t i, index = IP_SET_INVALID_ID;
	
	down(&ip_set_app_mutex);
	for (i = 0; i < ip_set_max; i++) {
		if (ip_set_list[i] != NULL
		    && strcmp(ip_set_list[i]->name, name) == 0) {
			__ip_set_get(i);
			index = i;
			break;
		}
	}
	up(&ip_set_app_mutex);
	return index;
}

/*
 * Find set by index, reference it once. The reference makes sure the
 * thing pointed to, does not go away under our feet. Drop the reference
 * later, using ip_set_put().
 */
ip_set_id_t
ip_set_get_byindex(ip_set_id_t index)
{
	down(&ip_set_app_mutex);

	if (index >= ip_set_max)
		return IP_SET_INVALID_ID;
	
	if (ip_set_list[index])
		__ip_set_get(index);
	else
		index = IP_SET_INVALID_ID;
		
	up(&ip_set_app_mutex);
	return index;
}

/*
 * If the given set pointer points to a valid set, decrement
 * reference count by 1. The caller shall not assume the index
 * to be valid, after calling this function.
 */
void ip_set_put(ip_set_id_t index)
{
	down(&ip_set_app_mutex);
	if (ip_set_list[index])
		__ip_set_put(index);
	up(&ip_set_app_mutex);
}

/* Find a set by name or index */
static ip_set_id_t
ip_set_find_byname(const char *name)
{
	ip_set_id_t i, index = IP_SET_INVALID_ID;
	
	for (i = 0; i < ip_set_max; i++) {
		if (ip_set_list[i] != NULL
		    && strcmp(ip_set_list[i]->name, name) == 0) {
			index = i;
			break;
		}
	}
	return index;
}

static ip_set_id_t
ip_set_find_byindex(ip_set_id_t index)
{
	if (index >= ip_set_max || ip_set_list[index] == NULL)
		index = IP_SET_INVALID_ID;
	
	return index;
}

/*
 * Add, del, test, bind and unbind
 */

static inline int
__ip_set_testip(struct ip_set *set,
	        const void *data,
	        size_t size,
	        ip_set_ip_t *ip)
{
	int res;

	read_lock_bh(&set->lock);
	res = set->type->testip(set, data, size, ip);
	read_unlock_bh(&set->lock);

	return res;
}

static int
__ip_set_addip(ip_set_id_t index,
	       const void *data,
	       size_t size)
{
	struct ip_set *set = ip_set_list[index];
	ip_set_ip_t ip;
	int res;
	
	IP_SET_ASSERT(set);
	do {
		write_lock_bh(&set->lock);
		res = set->type->addip(set, data, size, &ip);
		write_unlock_bh(&set->lock);
	} while (res == -EAGAIN
		 && set->type->retry
		 && (res = set->type->retry(set)) == 0);

	return res;
}

static int
ip_set_addip(ip_set_id_t index,
	     const void *data,
	     size_t size)
{

	return __ip_set_addip(index,
			      data + sizeof(struct ip_set_req_adt),
			      size - sizeof(struct ip_set_req_adt));
}

static int
ip_set_delip(ip_set_id_t index,
	     const void *data,
	     size_t size)
{
	struct ip_set *set = ip_set_list[index];
	ip_set_ip_t ip;
	int res;
	
	IP_SET_ASSERT(set);
	write_lock_bh(&set->lock);
	res = set->type->delip(set,
			       data + sizeof(struct ip_set_req_adt),
			       size - sizeof(struct ip_set_req_adt),
			       &ip);
	write_unlock_bh(&set->lock);

	return res;
}

static int
ip_set_testip(ip_set_id_t index,
	      const void *data,
	      size_t size)
{
	struct ip_set *set = ip_set_list[index];
	ip_set_ip_t ip;
	int res;

	IP_SET_ASSERT(set);
	res = __ip_set_testip(set,
			      data + sizeof(struct ip_set_req_adt),
			      size - sizeof(struct ip_set_req_adt),
			      &ip);

	return (res > 0 ? -EEXIST : res);
}

static int
ip_set_bindip(ip_set_id_t index,
	      const void *data,
	      size_t size)
{
	struct ip_set *set = ip_set_list[index];
	struct ip_set_req_bind *req_bind;
	ip_set_id_t binding;
	ip_set_ip_t ip;
	int res;

	IP_SET_ASSERT(set);
	if (size < sizeof(struct ip_set_req_bind))
		return -EINVAL;
		
	req_bind = (struct ip_set_req_bind *) data;
	req_bind->binding[IP_SET_MAXNAMELEN - 1] = '\0';

	if (strcmp(req_bind->binding, IPSET_TOKEN_DEFAULT) == 0) {
		/* Default binding of a set */
		char *binding_name;
		
		if (size != sizeof(struct ip_set_req_bind) + IP_SET_MAXNAMELEN)
			return -EINVAL;

		binding_name = (char *)(data + sizeof(struct ip_set_req_bind));	
		binding_name[IP_SET_MAXNAMELEN - 1] = '\0';

		binding = ip_set_find_byname(binding_name);
		if (binding == IP_SET_INVALID_ID)
			return -ENOENT;

		write_lock_bh(&ip_set_lock);
		/* Sets as binding values are referenced */
		if (set->binding != IP_SET_INVALID_ID)
			__ip_set_put(set->binding);
		set->binding = binding;
		__ip_set_get(set->binding);
		write_unlock_bh(&ip_set_lock);

		return 0;
	}
	binding = ip_set_find_byname(req_bind->binding);
	if (binding == IP_SET_INVALID_ID)
		return -ENOENT;

	res = __ip_set_testip(set,
			      data + sizeof(struct ip_set_req_bind),
			      size - sizeof(struct ip_set_req_bind),
			      &ip);
	DP("set %s, ip: %u.%u.%u.%u, binding %s",
	   set->name, HIPQUAD(ip), ip_set_list[binding]->name);
	
	if (res >= 0)
		res = ip_set_hash_add(set->id, ip, binding);

	return res;
}

#define FOREACH_SET_DO(fn, args...) 				\
({								\
	ip_set_id_t __i;					\
	struct ip_set *__set;					\
								\
	for (__i = 0; __i < ip_set_max; __i++) {		\
		__set = ip_set_list[__i];			\
		if (__set != NULL)				\
			fn(__set , ##args);			\
	}							\
})

static inline void
__set_hash_del_byid(struct ip_set_hash *set_hash, ip_set_id_t id)
{
	if (set_hash->id == id)
		__set_hash_del(set_hash);
}

static inline void
__unbind_default(struct ip_set *set)
{
	if (set->binding != IP_SET_INVALID_ID) {
		/* Sets as binding values are referenced */
		__ip_set_put(set->binding);
		set->binding = IP_SET_INVALID_ID;
	}
}

static int
ip_set_unbindip(ip_set_id_t index,
	        const void *data,
	        size_t size)
{
	struct ip_set *set;
	struct ip_set_req_bind *req_bind;
	ip_set_ip_t ip;
	int res;

	DP("");
	if (size < sizeof(struct ip_set_req_bind))
		return -EINVAL;
		
	req_bind = (struct ip_set_req_bind *) data;
	req_bind->binding[IP_SET_MAXNAMELEN - 1] = '\0';
	
	DP("%u %s", index, req_bind->binding);
	if (index == IP_SET_INVALID_ID) {
		/* unbind :all: */
		if (strcmp(req_bind->binding, IPSET_TOKEN_DEFAULT) == 0) {
			/* Default binding of sets */
			write_lock_bh(&ip_set_lock);
			FOREACH_SET_DO(__unbind_default);
			write_unlock_bh(&ip_set_lock);
			return 0;
		} else if (strcmp(req_bind->binding, IPSET_TOKEN_ALL) == 0) {
			/* Flush all bindings of all sets*/
			write_lock_bh(&ip_set_lock);
			FOREACH_HASH_RW_DO(__set_hash_del);
			write_unlock_bh(&ip_set_lock);
			return 0;
		}
		DP("unreachable reached!");
		return -EINVAL;
	}
	
	set = ip_set_list[index];
	IP_SET_ASSERT(set);
	if (strcmp(req_bind->binding, IPSET_TOKEN_DEFAULT) == 0) {
		/* Default binding of set */
		ip_set_id_t binding = ip_set_find_byindex(set->binding);

		if (binding == IP_SET_INVALID_ID)
			return -ENOENT;
			
		write_lock_bh(&ip_set_lock);
		/* Sets in hash values are referenced */
		__ip_set_put(set->binding);
		set->binding = IP_SET_INVALID_ID;
		write_unlock_bh(&ip_set_lock);

		return 0;
	} else if (strcmp(req_bind->binding, IPSET_TOKEN_ALL) == 0) {
		/* Flush all bindings */

		write_lock_bh(&ip_set_lock);
		FOREACH_HASH_RW_DO(__set_hash_del_byid, set->id);
		write_unlock_bh(&ip_set_lock);
		return 0;
	}
	
	res = __ip_set_testip(set,
			      data + sizeof(struct ip_set_req_bind),
			      size - sizeof(struct ip_set_req_bind),
			      &ip);

	DP("set %s, ip: %u.%u.%u.%u", set->name, HIPQUAD(ip));
	if (res >= 0)
		res = ip_set_hash_del(set->id, ip);

	return res;
}

static int
ip_set_testbind(ip_set_id_t index,
	        const void *data,
	        size_t size)
{
	struct ip_set *set = ip_set_list[index];
	struct ip_set_req_bind *req_bind;
	ip_set_id_t binding;
	ip_set_ip_t ip;
	int res;

	IP_SET_ASSERT(set);
	if (size < sizeof(struct ip_set_req_bind))
		return -EINVAL;
		
	req_bind = (struct ip_set_req_bind *) data;
	req_bind->binding[IP_SET_MAXNAMELEN - 1] = '\0';

	if (strcmp(req_bind->binding, IPSET_TOKEN_DEFAULT) == 0) {
		/* Default binding of set */
		char *binding_name;
		
		if (size != sizeof(struct ip_set_req_bind) + IP_SET_MAXNAMELEN)
			return -EINVAL;

		binding_name = (char *)(data + sizeof(struct ip_set_req_bind));	
		binding_name[IP_SET_MAXNAMELEN - 1] = '\0';

		binding = ip_set_find_byname(binding_name);
		if (binding == IP_SET_INVALID_ID)
			return -ENOENT;
		
		res = (set->binding == binding) ? -EEXIST : 0;

		return res;
	}
	binding = ip_set_find_byname(req_bind->binding);
	if (binding == IP_SET_INVALID_ID)
		return -ENOENT;
		
	
	res = __ip_set_testip(set,
			      data + sizeof(struct ip_set_req_bind),
			      size - sizeof(struct ip_set_req_bind),
			      &ip);
	DP("set %s, ip: %u.%u.%u.%u, binding %s",
	   set->name, HIPQUAD(ip), ip_set_list[binding]->name);
	   
	if (res >= 0)
		res = (ip_set_find_in_hash(set->id, ip) == binding)
			? -EEXIST : 0;

	return res;
}

static struct ip_set_type *
find_set_type_rlock(const char *typename)
{
	struct ip_set_type *type;
	
	read_lock_bh(&ip_set_lock);
	type = find_set_type(typename);
	if (type == NULL)
		read_unlock_bh(&ip_set_lock);

	return type;
}

static int
find_free_id(const char *name,
	     ip_set_id_t *index,
	     ip_set_id_t *id)
{
	ip_set_id_t i;

	*id = IP_SET_INVALID_ID;
	for (i = 0;  i < ip_set_max; i++) {
		if (ip_set_list[i] == NULL) {
			if (*id == IP_SET_INVALID_ID)
				*id = *index = i;
		} else if (strcmp(name, ip_set_list[i]->name) == 0)
			/* Name clash */
			return -EEXIST;
	}
	if (*id == IP_SET_INVALID_ID)
		/* No free slot remained */
		return -ERANGE;
	/* Check that index is usable as id (swapping) */
    check:	
	for (i = 0;  i < ip_set_max; i++) {
		if (ip_set_list[i] != NULL
		    && ip_set_list[i]->id == *id) {
		    *id = i;
		    goto check;
		}
	}
	return 0;
}

/*
 * Create a set
 */
static int
ip_set_create(const char *name,
	      const char *typename,
	      ip_set_id_t restore,
	      const void *data,
	      size_t size)
{
	struct ip_set *set;
	ip_set_id_t index = 0, id;
	int res = 0;

	DP("setname: %s, typename: %s, id: %u", name, typename, restore);
	/*
	 * First, and without any locks, allocate and initialize
	 * a normal base set structure.
	 */
	set = kmalloc(sizeof(struct ip_set), GFP_KERNEL);
	if (!set)
		return -ENOMEM;
	set->lock = RW_LOCK_UNLOCKED;
	strncpy(set->name, name, IP_SET_MAXNAMELEN);
	set->binding = IP_SET_INVALID_ID;
	atomic_set(&set->ref, 0);

	/*
	 * Next, take the &ip_set_lock, check that we know the type,
	 * and take a reference on the type, to make sure it
	 * stays available while constructing our new set.
	 *
	 * After referencing the type, we drop the &ip_set_lock,
	 * and let the new set construction run without locks.
	 */
	set->type = find_set_type_rlock(typename);
	if (set->type == NULL) {
		/* Try loading the module */
		char modulename[IP_SET_MAXNAMELEN + strlen("ip_set_") + 1];
		strcpy(modulename, "ip_set_");
		strcat(modulename, typename);
		DP("try to load %s", modulename);
		request_module(modulename);
		set->type = find_set_type_rlock(typename);
	}
	if (set->type == NULL) {
		ip_set_printk("no set type '%s', set '%s' not created",
			      typename, name);
		res = -ENOENT;
		goto out;
	}
	if (!try_module_get(set->type->me)) {
		read_unlock_bh(&ip_set_lock);
		res = -EFAULT;
		goto out;
	}
	read_unlock_bh(&ip_set_lock);

	/*
	 * Without holding any locks, create private part.
	 */
	res = set->type->create(set, data, size);
	if (res != 0)
		goto put_out;

	/* BTW, res==0 here. */

	/*
	 * Here, we have a valid, constructed set. &ip_set_lock again,
	 * find free id/index and check that it is not already in 
	 * ip_set_list.
	 */
	write_lock_bh(&ip_set_lock);
	if ((res = find_free_id(set->name, &index, &id)) != 0) {
		DP("no free id!");
		goto cleanup;
	}

	/* Make sure restore gets the same index */
	if (restore != IP_SET_INVALID_ID && index != restore) {
		DP("Can't restore, sets are screwed up");
		res = -ERANGE;
		goto cleanup;
	}
	 
	/*
	 * Finally! Add our shiny new set to the list, and be done.
	 */
	DP("create: '%s' created with index %u, id %u!", set->name, index, id);
	set->id = id;
	ip_set_list[index] = set;
	write_unlock_bh(&ip_set_lock);
	return res;
	
    cleanup:
	write_unlock_bh(&ip_set_lock);
	set->type->destroy(set);
    put_out:
	module_put(set->type->me);
    out:
	kfree(set);
	return res;
}

/*
 * Destroy a given existing set
 */
static void
ip_set_destroy_set(ip_set_id_t index)
{
	struct ip_set *set = ip_set_list[index];

	IP_SET_ASSERT(set);
	DP("set: %s",  set->name);
	write_lock_bh(&ip_set_lock);
	FOREACH_HASH_RW_DO(__set_hash_del_byid, set->id);
	if (set->binding != IP_SET_INVALID_ID)
		__ip_set_put(set->binding);
	ip_set_list[index] = NULL;
	write_unlock_bh(&ip_set_lock);

	/* Must call it without holding any lock */
	set->type->destroy(set);
	module_put(set->type->me);
	kfree(set);
}

/*
 * Destroy a set - or all sets
 * Sets must not be referenced/used.
 */
static int
ip_set_destroy(ip_set_id_t index)
{
	ip_set_id_t i;

	/* ref modification always protected by the mutex */
	if (index != IP_SET_INVALID_ID) {
		if (atomic_read(&ip_set_list[index]->ref))
			return -EBUSY;
		ip_set_destroy_set(index);
	} else {
		for (i = 0; i < ip_set_max; i++) {
			if (ip_set_list[i] != NULL 
			    && (atomic_read(&ip_set_list[i]->ref)))
			    	return -EBUSY;
		}

		for (i = 0; i < ip_set_max; i++) {
			if (ip_set_list[i] != NULL)
				ip_set_destroy_set(i);
		}
	}
	return 0;
}

static void
ip_set_flush_set(struct ip_set *set)
{
	DP("set: %s %u",  set->name, set->id);

	write_lock_bh(&set->lock);
	set->type->flush(set);
	write_unlock_bh(&set->lock);
}

/* 
 * Flush data in a set - or in all sets
 */
static int
ip_set_flush(ip_set_id_t index)
{
	if (index != IP_SET_INVALID_ID) {
		IP_SET_ASSERT(ip_set_list[index]);
		ip_set_flush_set(ip_set_list[index]);
	} else
		FOREACH_SET_DO(ip_set_flush_set);

	return 0;
}

/* Rename a set */
static int
ip_set_rename(ip_set_id_t index, const char *name)
{
	struct ip_set *set = ip_set_list[index];
	ip_set_id_t i;
	int res = 0;

	DP("set: %s to %s",  set->name, name);
	write_lock_bh(&ip_set_lock);
	for (i = 0; i < ip_set_max; i++) {
		if (ip_set_list[i] != NULL
		    && strncmp(ip_set_list[i]->name, 
			       name,
			       IP_SET_MAXNAMELEN - 1) == 0) {
			res = -EEXIST;
			goto unlock;
		}
	}
	strncpy(set->name, name, IP_SET_MAXNAMELEN);
    unlock:
	write_unlock_bh(&ip_set_lock);
	return res;
}

/*
 * Swap two sets so that name/index points to the other.
 * References are also swapped.
 */
static int
ip_set_swap(ip_set_id_t from_index, ip_set_id_t to_index)
{
	struct ip_set *from = ip_set_list[from_index];
	struct ip_set *to = ip_set_list[to_index];
	char from_name[IP_SET_MAXNAMELEN];
	u_int32_t from_ref;

	DP("set: %s to %s",  from->name, to->name);
	/* Features must not change. Artifical restriction. */
	if (from->type->features != to->type->features)
		return -ENOEXEC;

	/* No magic here: ref munging protected by the mutex */	
	write_lock_bh(&ip_set_lock);
	strncpy(from_name, from->name, IP_SET_MAXNAMELEN);
	from_ref = atomic_read(&from->ref);

	strncpy(from->name, to->name, IP_SET_MAXNAMELEN);
	atomic_set(&from->ref, atomic_read(&to->ref));
	strncpy(to->name, from_name, IP_SET_MAXNAMELEN);
	atomic_set(&to->ref, from_ref);
	
	ip_set_list[from_index] = to;
	ip_set_list[to_index] = from;
	
	write_unlock_bh(&ip_set_lock);
	return 0;
}

/*
 * List set data
 */

static inline void
__set_hash_bindings_size_list(struct ip_set_hash *set_hash,
			      ip_set_id_t id, size_t *size)
{
	if (set_hash->id == id)
		*size += sizeof(struct ip_set_hash_list);
}

static inline void
__set_hash_bindings_size_save(struct ip_set_hash *set_hash,
			      ip_set_id_t id, size_t *size)
{
	if (set_hash->id == id)
		*size += sizeof(struct ip_set_hash_save);
}

static inline void
__set_hash_bindings(struct ip_set_hash *set_hash,
		    ip_set_id_t id, void *data, int *used)
{
	if (set_hash->id == id) {
		struct ip_set_hash_list *hash_list = 
			(struct ip_set_hash_list *)(data + *used);

		hash_list->ip = set_hash->ip;
		hash_list->binding = set_hash->binding;
		*used += sizeof(struct ip_set_hash_list);
	}
}

static int ip_set_list_set(ip_set_id_t index,
			   void *data,
			   int *used,
			   int len)
{
	struct ip_set *set = ip_set_list[index];
	struct ip_set_list *set_list;

	/* Pointer to our header */
	set_list = (struct ip_set_list *) (data + *used);

	DP("set: %s, used: %d %p %p", set->name, *used, data, data + *used);

	/* Get and ensure header size */
	if (*used + sizeof(struct ip_set_list) > len)
		goto not_enough_mem;
	*used += sizeof(struct ip_set_list);

	read_lock_bh(&set->lock);
	/* Get and ensure set specific header size */
	set_list->header_size = set->type->header_size;
	if (*used + set_list->header_size > len)
		goto unlock_set;

	/* Fill in the header */
	set_list->index = index;
	set_list->binding = set->binding;
	set_list->ref = atomic_read(&set->ref);

	/* Fill in set spefific header data */
	set->type->list_header(set, data + *used);
	*used += set_list->header_size;

	/* Get and ensure set specific members size */
	set_list->members_size = set->type->list_members_size(set);
	if (*used + set_list->members_size > len)
		goto unlock_set;

	/* Fill in set spefific members data */
	set->type->list_members(set, data + *used);
	*used += set_list->members_size;
	read_unlock_bh(&set->lock);

	/* Bindings */

	/* Get and ensure set specific bindings size */
	set_list->bindings_size = 0;
	FOREACH_HASH_DO(__set_hash_bindings_size_list,
			set->id, &set_list->bindings_size);
	if (*used + set_list->bindings_size > len)
		goto not_enough_mem;

	/* Fill in set spefific bindings data */
	FOREACH_HASH_DO(__set_hash_bindings, set->id, data, used);
	
	return 0;

    unlock_set:
	read_unlock_bh(&set->lock);
    not_enough_mem:
	DP("not enough mem, try again");
	return -EAGAIN;
}

/*
 * Save sets
 */
static int ip_set_save_set(ip_set_id_t index,
			   void *data,
			   int *used,
			   int len)
{
	struct ip_set *set;
	struct ip_set_save *set_save;

	/* Pointer to our header */
	set_save = (struct ip_set_save *) (data + *used);

	/* Get and ensure header size */
	if (*used + sizeof(struct ip_set_save) > len)
		goto not_enough_mem;
	*used += sizeof(struct ip_set_save);

	set = ip_set_list[index];
	DP("set: %s, used: %u(%u) %p %p", set->name, *used, len, 
	   data, data + *used);

	read_lock_bh(&set->lock);
	/* Get and ensure set specific header size */
	set_save->header_size = set->type->header_size;
	if (*used + set_save->header_size > len)
		goto unlock_set;

	/* Fill in the header */
	set_save->index = index;
	set_save->binding = set->binding;

	/* Fill in set spefific header data */
	set->type->list_header(set, data + *used);
	*used += set_save->header_size;

	DP("set header filled: %s, used: %u %p %p", set->name, *used,
	   data, data + *used);
	/* Get and ensure set specific members size */
	set_save->members_size = set->type->list_members_size(set);
	if (*used + set_save->members_size > len)
		goto unlock_set;

	/* Fill in set spefific members data */
	set->type->list_members(set, data + *used);
	*used += set_save->members_size;
	read_unlock_bh(&set->lock);
	DP("set members filled: %s, used: %u %p %p", set->name, *used,
	   data, data + *used);
	return 0;

    unlock_set:
	read_unlock_bh(&set->lock);
    not_enough_mem:
	DP("not enough mem, try again");
	return -EAGAIN;
}

static inline void
__set_hash_save_bindings(struct ip_set_hash *set_hash,
			 ip_set_id_t id,
			 void *data,
			 int *used,
			 int len,
			 int *res)
{
	if (*res == 0
	    && (id == IP_SET_INVALID_ID || set_hash->id == id)) {
		struct ip_set_hash_save *hash_save = 
			(struct ip_set_hash_save *)(data + *used);
		/* Ensure bindings size */
		if (*used + sizeof(struct ip_set_hash_save) > len) {
			*res = -ENOMEM;
			return;
		}
		hash_save->id = set_hash->id;
		hash_save->ip = set_hash->ip;
		hash_save->binding = set_hash->binding;
		*used += sizeof(struct ip_set_hash_save);
	}
}

static int ip_set_save_bindings(ip_set_id_t index,
			   	void *data,
			   	int *used,
			   	int len)
{
	int res = 0;
	struct ip_set_save *set_save;

	DP("used %u, len %u", *used, len);
	/* Get and ensure header size */
	if (*used + sizeof(struct ip_set_save) > len)
		return -ENOMEM;

	/* Marker */
	set_save = (struct ip_set_save *) (data + *used);
	set_save->index = IP_SET_INVALID_ID;
	*used += sizeof(struct ip_set_save);

	DP("marker added used %u, len %u", *used, len);
	/* Fill in bindings data */
	if (index != IP_SET_INVALID_ID)
		/* Sets are identified by id in hash */
		index = ip_set_list[index]->id;
	FOREACH_HASH_DO(__set_hash_save_bindings, index, data, used, len, &res);

	return res;	
}

/*
 * Restore sets
 */
static int ip_set_restore(void *data,
			  int len)
{
	int res = 0;
	int line = 0, used = 0, members_size;
	struct ip_set *set;
	struct ip_set_hash_save *hash_save;
	struct ip_set_restore *set_restore;
	ip_set_id_t index;

	/* Loop to restore sets */
	while (1) {
		line++;
		
		DP("%u %u %u", used, sizeof(struct ip_set_restore), len);
		/* Get and ensure header size */
		if (used + sizeof(struct ip_set_restore) > len)
			return line;
		set_restore = (struct ip_set_restore *) (data + used);
		used += sizeof(struct ip_set_restore);

		/* Ensure data size */
		if (used 
		    + set_restore->header_size 
		    + set_restore->members_size > len)
			return line;

		/* Check marker */
		if (set_restore->index == IP_SET_INVALID_ID) {
			line--;
			goto bindings;
		}
		
		/* Try to create the set */
		DP("restore %s %s", set_restore->name, set_restore->typename);
		res = ip_set_create(set_restore->name,
				    set_restore->typename,
				    set_restore->index,
				    data + used,
				    set_restore->header_size);
		
		if (res != 0)
			return line;
		used += set_restore->header_size;

		index = ip_set_find_byindex(set_restore->index);
		DP("index %u, restore_index %u", index, set_restore->index);
		if (index != set_restore->index)
			return line;
		/* Try to restore members data */
		set = ip_set_list[index];
		members_size = 0;
		DP("members_size %u reqsize %u",
		   set_restore->members_size, set->type->reqsize);
		while (members_size + set->type->reqsize <=
		       set_restore->members_size) {
			line++;
		       	DP("members: %u, line %u", members_size, line);
			res = __ip_set_addip(index,
					   data + used + members_size,
					   set->type->reqsize);
			if (!(res == 0 || res == -EEXIST)) 
				return line;
			members_size += set->type->reqsize;
		}

		DP("members_size %u  %u",
		   set_restore->members_size, members_size);
		if (members_size != set_restore->members_size)
			return line++;
		used += set_restore->members_size;		
	}
	
   bindings:
   	/* Loop to restore bindings */
   	while (used < len) {
		line++;

		DP("restore binding, line %u", line);		
		/* Get and ensure size */
		if (used + sizeof(struct ip_set_hash_save) > len)
			return line;
		hash_save = (struct ip_set_hash_save *) (data + used);
		used += sizeof(struct ip_set_hash_save);
		
		/* hash_save->id is used to store the index */
		index = ip_set_find_byindex(hash_save->id);
		DP("restore binding index %u, id %u, %u -> %u",
		   index, hash_save->id, hash_save->ip, hash_save->binding);		
		if (index != hash_save->id)
			return line;
			
		set = ip_set_list[hash_save->id];
		/* Null valued IP means default binding */
		if (hash_save->ip)
			res = ip_set_hash_add(set->id, 
					      hash_save->ip,
					      hash_save->binding);
		else {
			IP_SET_ASSERT(set->binding == IP_SET_INVALID_ID);
			write_lock_bh(&ip_set_lock);
			set->binding = hash_save->binding;
			__ip_set_get(set->binding);
			write_unlock_bh(&ip_set_lock);
			DP("default binding: %u", set->binding);
		}
		if (res != 0)
			return line;
   	}
   	if (used != len)
   		return line;
   	
	return 0;	
}

static int
ip_set_sockfn_set(struct sock *sk, int optval, void *user, unsigned int len)
{
	void *data;
	int res = 0;		/* Assume OK */
	unsigned *op;
	struct ip_set_req_adt *req_adt;
	ip_set_id_t index = IP_SET_INVALID_ID;
	int (*adtfn)(ip_set_id_t index,
		     const void *data, size_t size);
	struct fn_table {
		int (*fn)(ip_set_id_t index,
			  const void *data, size_t size);
	} adtfn_table[] =
	{ { ip_set_addip }, { ip_set_delip }, { ip_set_testip},
	  { ip_set_bindip}, { ip_set_unbindip }, { ip_set_testbind },
	};

	DP("optval=%d, user=%p, len=%d", optval, user, len);
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	if (optval != SO_IP_SET)
		return -EBADF;
	if (len <= sizeof(unsigned)) {
		ip_set_printk("short userdata (want >%zu, got %u)",
			      sizeof(unsigned), len);
		return -EINVAL;
	}
	data = vmalloc(len);
	if (!data) {
		DP("out of mem for %u bytes", len);
		return -ENOMEM;
	}
	if (copy_from_user(data, user, len) != 0) {
		res = -EFAULT;
		goto done;
	}
	if (down_interruptible(&ip_set_app_mutex)) {
		res = -EINTR;
		goto done;
	}

	op = (unsigned *)data;
	DP("op=%x", *op);
	
	if (*op < IP_SET_OP_VERSION) {
		/* Check the version at the beginning of operations */
		struct ip_set_req_version *req_version =
			(struct ip_set_req_version *) data;
		if (req_version->version != IP_SET_PROTOCOL_VERSION) {
			res = -EPROTO;
			goto done;
		}
	}

	switch (*op) {
	case IP_SET_OP_CREATE:{
		struct ip_set_req_create *req_create
			= (struct ip_set_req_create *) data;
		
		if (len < sizeof(struct ip_set_req_create)) {
			ip_set_printk("short CREATE data (want >=%zu, got %u)",
				      sizeof(struct ip_set_req_create), len);
			res = -EINVAL;
			goto done;
		}
		req_create->name[IP_SET_MAXNAMELEN - 1] = '\0';
		req_create->typename[IP_SET_MAXNAMELEN - 1] = '\0';
		res = ip_set_create(req_create->name,
				    req_create->typename,
				    IP_SET_INVALID_ID,
				    data + sizeof(struct ip_set_req_create),
				    len - sizeof(struct ip_set_req_create));
		goto done;
	}
	case IP_SET_OP_DESTROY:{
		struct ip_set_req_std *req_destroy
			= (struct ip_set_req_std *) data;
		
		if (len != sizeof(struct ip_set_req_std)) {
			ip_set_printk("invalid DESTROY data (want %zu, got %u)",
				      sizeof(struct ip_set_req_std), len);
			res = -EINVAL;
			goto done;
		}
		if (strcmp(req_destroy->name, IPSET_TOKEN_ALL) == 0) {
			/* Destroy all sets */
			index = IP_SET_INVALID_ID;
		} else {
			req_destroy->name[IP_SET_MAXNAMELEN - 1] = '\0';
			index = ip_set_find_byname(req_destroy->name);

			if (index == IP_SET_INVALID_ID) {
				res = -ENOENT;
				goto done;
			}
		}
			
		res = ip_set_destroy(index);
		goto done;
	}
	case IP_SET_OP_FLUSH:{
		struct ip_set_req_std *req_flush =
			(struct ip_set_req_std *) data;

		if (len != sizeof(struct ip_set_req_std)) {
			ip_set_printk("invalid FLUSH data (want %zu, got %u)",
				      sizeof(struct ip_set_req_std), len);
			res = -EINVAL;
			goto done;
		}
		if (strcmp(req_flush->name, IPSET_TOKEN_ALL) == 0) {
			/* Flush all sets */
			index = IP_SET_INVALID_ID;
		} else {
			req_flush->name[IP_SET_MAXNAMELEN - 1] = '\0';
			index = ip_set_find_byname(req_flush->name);

			if (index == IP_SET_INVALID_ID) {
				res = -ENOENT;
				goto done;
			}
		}
		res = ip_set_flush(index);
		goto done;
	}
	case IP_SET_OP_RENAME:{
		struct ip_set_req_create *req_rename
			= (struct ip_set_req_create *) data;

		if (len != sizeof(struct ip_set_req_create)) {
			ip_set_printk("invalid RENAME data (want %zu, got %u)",
				      sizeof(struct ip_set_req_create), len);
			res = -EINVAL;
			goto done;
		}

		req_rename->name[IP_SET_MAXNAMELEN - 1] = '\0';
		req_rename->typename[IP_SET_MAXNAMELEN - 1] = '\0';
			
		index = ip_set_find_byname(req_rename->name);
		if (index == IP_SET_INVALID_ID) {
			res = -ENOENT;
			goto done;
		}
		res = ip_set_rename(index, req_rename->typename);
		goto done;
	}
	case IP_SET_OP_SWAP:{
		struct ip_set_req_create *req_swap
			= (struct ip_set_req_create *) data;
		ip_set_id_t to_index;

		if (len != sizeof(struct ip_set_req_create)) {
			ip_set_printk("invalid SWAP data (want %zu, got %u)",
				      sizeof(struct ip_set_req_create), len);
			res = -EINVAL;
			goto done;
		}

		req_swap->name[IP_SET_MAXNAMELEN - 1] = '\0';
		req_swap->typename[IP_SET_MAXNAMELEN - 1] = '\0';

		index = ip_set_find_byname(req_swap->name);
		if (index == IP_SET_INVALID_ID) {
			res = -ENOENT;
			goto done;
		}
		to_index = ip_set_find_byname(req_swap->typename);
		if (to_index == IP_SET_INVALID_ID) {
			res = -ENOENT;
			goto done;
		}
		res = ip_set_swap(index, to_index);
		goto done;
	}
	default: 
		break;	/* Set identified by id */
	}
	
	/* There we may have add/del/test/bind/unbind/test_bind operations */
	if (*op < IP_SET_OP_ADD_IP || *op > IP_SET_OP_TEST_BIND_SET) {
		res = -EBADMSG;
		goto done;
	}
	adtfn = adtfn_table[*op - IP_SET_OP_ADD_IP].fn;

	if (len < sizeof(struct ip_set_req_adt)) {
		ip_set_printk("short data in adt request (want >=%zu, got %u)",
			      sizeof(struct ip_set_req_adt), len);
		res = -EINVAL;
		goto done;
	}
	req_adt = (struct ip_set_req_adt *) data;

	/* -U :all: :all:|:default: uses IP_SET_INVALID_ID */
	if (!(*op == IP_SET_OP_UNBIND_SET 
	      && req_adt->index == IP_SET_INVALID_ID)) {
		index = ip_set_find_byindex(req_adt->index);
		if (index == IP_SET_INVALID_ID) {
			res = -ENOENT;
			goto done;
		}
	}
	res = adtfn(index, data, len);

    done:
	up(&ip_set_app_mutex);
	vfree(data);
	if (res > 0)
		res = 0;
	DP("final result %d", res);
	return res;
}

static int 
ip_set_sockfn_get(struct sock *sk, int optval, void *user, int *len)
{
	int res = 0;
	unsigned *op;
	ip_set_id_t index = IP_SET_INVALID_ID;
	void *data;
	int copylen = *len;

	DP("optval=%d, user=%p, len=%d", optval, user, *len);
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	if (optval != SO_IP_SET)
		return -EBADF;
	if (*len < sizeof(unsigned)) {
		ip_set_printk("short userdata (want >=%zu, got %d)",
			      sizeof(unsigned), *len);
		return -EINVAL;
	}
	data = vmalloc(*len);
	if (!data) {
		DP("out of mem for %d bytes", *len);
		return -ENOMEM;
	}
	if (copy_from_user(data, user, *len) != 0) {
		res = -EFAULT;
		goto done;
	}
	if (down_interruptible(&ip_set_app_mutex)) {
		res = -EINTR;
		goto done;
	}

	op = (unsigned *) data;
	DP("op=%x", *op);

	if (*op < IP_SET_OP_VERSION) {
		/* Check the version at the beginning of operations */
		struct ip_set_req_version *req_version =
			(struct ip_set_req_version *) data;
		if (req_version->version != IP_SET_PROTOCOL_VERSION) {
			res = -EPROTO;
			goto done;
		}
	}

	switch (*op) {
	case IP_SET_OP_VERSION: {
		struct ip_set_req_version *req_version =
		    (struct ip_set_req_version *) data;

		if (*len != sizeof(struct ip_set_req_version)) {
			ip_set_printk("invalid VERSION (want %zu, got %d)",
				      sizeof(struct ip_set_req_version),
				      *len);
			res = -EINVAL;
			goto done;
		}

		req_version->version = IP_SET_PROTOCOL_VERSION;
		res = copy_to_user(user, req_version,
				   sizeof(struct ip_set_req_version));
		goto done;
	}
	case IP_SET_OP_GET_BYNAME: {
		struct ip_set_req_get_set *req_get
			= (struct ip_set_req_get_set *) data;

		if (*len != sizeof(struct ip_set_req_get_set)) {
			ip_set_printk("invalid GET_BYNAME (want %zu, got %d)",
				      sizeof(struct ip_set_req_get_set), *len);
			res = -EINVAL;
			goto done;
		}
		req_get->set.name[IP_SET_MAXNAMELEN - 1] = '\0';
		index = ip_set_find_byname(req_get->set.name);
		req_get->set.index = index;
		goto copy;
	}
	case IP_SET_OP_GET_BYINDEX: {
		struct ip_set_req_get_set *req_get
			= (struct ip_set_req_get_set *) data;

		if (*len != sizeof(struct ip_set_req_get_set)) {
			ip_set_printk("invalid GET_BYINDEX (want %zu, got %d)",
				      sizeof(struct ip_set_req_get_set), *len);
			res = -EINVAL;
			goto done;
		}
		req_get->set.name[IP_SET_MAXNAMELEN - 1] = '\0';
		index = ip_set_find_byindex(req_get->set.index);
		strncpy(req_get->set.name,
			index == IP_SET_INVALID_ID ? ""
			: ip_set_list[index]->name, IP_SET_MAXNAMELEN);
		goto copy;
	}
	case IP_SET_OP_ADT_GET: {
		struct ip_set_req_adt_get *req_get
			= (struct ip_set_req_adt_get *) data;

		if (*len != sizeof(struct ip_set_req_adt_get)) {
			ip_set_printk("invalid ADT_GET (want %zu, got %d)",
				      sizeof(struct ip_set_req_adt_get), *len);
			res = -EINVAL;
			goto done;
		}
		req_get->set.name[IP_SET_MAXNAMELEN - 1] = '\0';
		index = ip_set_find_byname(req_get->set.name);
		if (index != IP_SET_INVALID_ID) {
			req_get->set.index = index;
			strncpy(req_get->typename,
				ip_set_list[index]->type->typename,
				IP_SET_MAXNAMELEN - 1);
		} else {
			res = -ENOENT;
			goto done;
		}
		goto copy;
	}
	case IP_SET_OP_MAX_SETS: {
		struct ip_set_req_max_sets *req_max_sets
			= (struct ip_set_req_max_sets *) data;
		ip_set_id_t i;

		if (*len != sizeof(struct ip_set_req_max_sets)) {
			ip_set_printk("invalid MAX_SETS (want %zu, got %d)",
				      sizeof(struct ip_set_req_max_sets), *len);
			res = -EINVAL;
			goto done;
		}

		if (strcmp(req_max_sets->set.name, IPSET_TOKEN_ALL) == 0) {
			req_max_sets->set.index = IP_SET_INVALID_ID;
		} else {
			req_max_sets->set.name[IP_SET_MAXNAMELEN - 1] = '\0';
			req_max_sets->set.index = 
				ip_set_find_byname(req_max_sets->set.name);
			if (req_max_sets->set.index == IP_SET_INVALID_ID) {
				res = -ENOENT;
				goto done;
			}
		}
		req_max_sets->max_sets = ip_set_max;
		req_max_sets->sets = 0;
		for (i = 0; i < ip_set_max; i++) {
			if (ip_set_list[i] != NULL)
				req_max_sets->sets++;
		}
		goto copy;
	}
	case IP_SET_OP_LIST_SIZE: 
	case IP_SET_OP_SAVE_SIZE: {
		struct ip_set_req_setnames *req_setnames
			= (struct ip_set_req_setnames *) data;
		struct ip_set_name_list *name_list;
		struct ip_set *set;
		ip_set_id_t i;
		int used;

		if (*len < sizeof(struct ip_set_req_setnames)) {
			ip_set_printk("short LIST_SIZE (want >=%zu, got %d)",
				      sizeof(struct ip_set_req_setnames), *len);
			res = -EINVAL;
			goto done;
		}

		req_setnames->size = 0;
		used = sizeof(struct ip_set_req_setnames);
		for (i = 0; i < ip_set_max; i++) {
			if (ip_set_list[i] == NULL)
				continue;
			name_list = (struct ip_set_name_list *) 
				(data + used);
			used += sizeof(struct ip_set_name_list);
			if (used > copylen) {
				res = -EAGAIN;
				goto done;
			}
			set = ip_set_list[i];
			/* Fill in index, name, etc. */
			name_list->index = i;
			name_list->id = set->id;
			strncpy(name_list->name,
				set->name,
				IP_SET_MAXNAMELEN - 1);
			strncpy(name_list->typename,
				set->type->typename,
				IP_SET_MAXNAMELEN - 1);
			DP("filled %s of type %s, index %u\n",
			   name_list->name, name_list->typename,
			   name_list->index);
			if (!(req_setnames->index == IP_SET_INVALID_ID
			      || req_setnames->index == i))
			      continue;
			/* Update size */
			switch (*op) {
			case IP_SET_OP_LIST_SIZE: {
				req_setnames->size += sizeof(struct ip_set_list)
					+ set->type->header_size
					+ set->type->list_members_size(set);
				/* Sets are identified by id in the hash */
				FOREACH_HASH_DO(__set_hash_bindings_size_list, 
						set->id, &req_setnames->size);
				break;
			}
			case IP_SET_OP_SAVE_SIZE: {
				req_setnames->size += sizeof(struct ip_set_save)
					+ set->type->header_size
					+ set->type->list_members_size(set);
				FOREACH_HASH_DO(__set_hash_bindings_size_save,
						set->id, &req_setnames->size);
				break;
			}
			default:
				break;
			}
		}
		if (copylen != used) {
			res = -EAGAIN;
			goto done;
		}
		goto copy;
	}
	case IP_SET_OP_LIST: {
		struct ip_set_req_list *req_list
			= (struct ip_set_req_list *) data;
		ip_set_id_t i;
		int used;

		if (*len < sizeof(struct ip_set_req_list)) {
			ip_set_printk("short LIST (want >=%zu, got %d)",
				      sizeof(struct ip_set_req_list), *len);
			res = -EINVAL;
			goto done;
		}
		index = req_list->index;
		if (index != IP_SET_INVALID_ID
		    && ip_set_find_byindex(index) != index) {
		    	res = -ENOENT;
		    	goto done;
		}
		used = 0;
		if (index == IP_SET_INVALID_ID) {
			/* List all sets */
			for (i = 0; i < ip_set_max && res == 0; i++) {
				if (ip_set_list[i] != NULL)
					res = ip_set_list_set(i, data, &used, *len);
			}
		} else {
			/* List an individual set */
			res = ip_set_list_set(index, data, &used, *len);
		}
		if (res != 0)
			goto done;
		else if (copylen != used) {
			res = -EAGAIN;
			goto done;
		}
		goto copy;
	}
	case IP_SET_OP_SAVE: {
		struct ip_set_req_list *req_save
			= (struct ip_set_req_list *) data;
		ip_set_id_t i;
		int used;

		if (*len < sizeof(struct ip_set_req_list)) {
			ip_set_printk("short SAVE (want >=%zu, got %d)",
				      sizeof(struct ip_set_req_list), *len);
			res = -EINVAL;
			goto done;
		}
		index = req_save->index;
		if (index != IP_SET_INVALID_ID
		    && ip_set_find_byindex(index) != index) {
		    	res = -ENOENT;
		    	goto done;
		}
		used = 0;
		if (index == IP_SET_INVALID_ID) {
			/* Save all sets */
			for (i = 0; i < ip_set_max && res == 0; i++) {
				if (ip_set_list[i] != NULL)
					res = ip_set_save_set(i, data, &used, *len);
			}
		} else {
			/* Save an individual set */
			res = ip_set_save_set(index, data, &used, *len);
		}
		if (res == 0)
			res = ip_set_save_bindings(index, data, &used, *len);
			
		if (res != 0)
			goto done;
		else if (copylen != used) {
			res = -EAGAIN;
			goto done;
		}
		goto copy;
	}
	case IP_SET_OP_RESTORE: {
		struct ip_set_req_setnames *req_restore
			= (struct ip_set_req_setnames *) data;
		int line;

		if (*len < sizeof(struct ip_set_req_setnames)
		    || *len != req_restore->size) {
			ip_set_printk("invalid RESTORE (want =%zu, got %d)",
				      req_restore->size, *len);
			res = -EINVAL;
			goto done;
		}
		line = ip_set_restore(data + sizeof(struct ip_set_req_setnames),
				      req_restore->size - sizeof(struct ip_set_req_setnames));
		DP("ip_set_restore: %u", line);
		if (line != 0) {
			res = -EAGAIN;
			req_restore->size = line;
			copylen = sizeof(struct ip_set_req_setnames);
			goto copy;
		}
		goto done;
	}
	default:
		res = -EBADMSG;
		goto done;
	}	/* end of switch(op) */

    copy:
   	DP("set %s, copylen %u", index != IP_SET_INVALID_ID
   	             		 && ip_set_list[index]
   	             ? ip_set_list[index]->name
   	             : ":all:", copylen);
	res = copy_to_user(user, data, copylen);
    	
    done:
	up(&ip_set_app_mutex);
	vfree(data);
	if (res > 0)
		res = 0;
	DP("final result %d", res);
	return res;
}

static struct nf_sockopt_ops so_set = {
	.pf 		= PF_INET,
	.set_optmin 	= SO_IP_SET,
	.set_optmax 	= SO_IP_SET + 1,
	.set 		= &ip_set_sockfn_set,
	.get_optmin 	= SO_IP_SET,
	.get_optmax	= SO_IP_SET + 1,
	.get		= &ip_set_sockfn_get,
	.use		= 0
};

static int max_sets, hash_size;
module_param(max_sets, int, 0600);
MODULE_PARM_DESC(max_sets, "maximal number of sets");
module_param(hash_size, int, 0600);
MODULE_PARM_DESC(hash_size, "hash size for bindings");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("module implementing core IP set support");

static int __init init(void)
{
	int res;
	ip_set_id_t i;

	get_random_bytes(&ip_set_hash_random, 4);
	if (max_sets)
		ip_set_max = max_sets;
	ip_set_list = vmalloc(sizeof(struct ip_set *) * ip_set_max);
	if (!ip_set_list) {
		printk(KERN_ERR "Unable to create ip_set_list\n");
		return -ENOMEM;
	}
	memset(ip_set_list, 0, sizeof(struct ip_set *) * ip_set_max);
	if (hash_size)
		ip_set_bindings_hash_size = hash_size;
	ip_set_hash = vmalloc(sizeof(struct list_head) * ip_set_bindings_hash_size);
	if (!ip_set_hash) {
		printk(KERN_ERR "Unable to create ip_set_hash\n");
		vfree(ip_set_list);
		return -ENOMEM;
	}
	for (i = 0; i < ip_set_bindings_hash_size; i++)
		INIT_LIST_HEAD(&ip_set_hash[i]);

	INIT_LIST_HEAD(&set_type_list);

	res = nf_register_sockopt(&so_set);
	if (res != 0) {
		ip_set_printk("SO_SET registry failed: %d", res);
		vfree(ip_set_list);
		vfree(ip_set_hash);
		return res;
	}
	return 0;
}

static void __exit fini(void)
{
	/* There can't be any existing set or binding */
	nf_unregister_sockopt(&so_set);
	vfree(ip_set_list);
	vfree(ip_set_hash);
	DP("these are the famous last words");
}

EXPORT_SYMBOL(ip_set_register_set_type);
EXPORT_SYMBOL(ip_set_unregister_set_type);

EXPORT_SYMBOL(ip_set_get_byname);
EXPORT_SYMBOL(ip_set_get_byindex);
EXPORT_SYMBOL(ip_set_put);

EXPORT_SYMBOL(ip_set_addip_kernel);
EXPORT_SYMBOL(ip_set_delip_kernel);
EXPORT_SYMBOL(ip_set_testip_kernel);

module_init(init);
module_exit(fini);

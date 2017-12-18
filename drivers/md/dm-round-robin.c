/*
 * Copyright (C) 2003 Sistina Software.
 * Copyright (C) 2004-2005 Red Hat, Inc. All rights reserved.
 *
 * Module Author: Heinz Mauelshagen
 *
 * This file is released under the GPL.
 *
 * Round-robin path selector.
 */

#include <linux/device-mapper.h>

#include "dm-path-selector.h"

#include <linux/slab.h>
#include <linux/module.h>

#define DM_MSG_PREFIX "multipath round-robin"
#define RR_MIN_IO     1000
#define RR_VERSION    "1.1.0"

/*-----------------------------------------------------------------
 * Path-handling code, paths are held in lists
 *---------------------------------------------------------------*/
struct path_info {
	struct list_head list;
	struct dm_path *path;
	unsigned repeat_count;
};

static void free_paths(struct list_head *paths)
{
	struct path_info *pi, *next;

	list_for_each_entry_safe(pi, next, paths, list) {
		list_del(&pi->list);
		kfree(pi);
	}
}

/*-----------------------------------------------------------------
 * Round-robin selector
 *---------------------------------------------------------------*/

struct selector {
	struct list_head valid_paths;
	struct list_head invalid_paths;
	spinlock_t lock;
	struct dm_path * __percpu *current_path;
	struct percpu_counter repeat_count;
};

static void set_percpu_current_path(struct selector *s, struct dm_path *path)
{
	int cpu;

	for_each_possible_cpu(cpu)
		*per_cpu_ptr(s->current_path, cpu) = path;
}

static struct selector *alloc_selector(void)
{
	struct selector *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		return NULL;

	INIT_LIST_HEAD(&s->valid_paths);
	INIT_LIST_HEAD(&s->invalid_paths);
	spin_lock_init(&s->lock);

	s->current_path = alloc_percpu(struct dm_path *);
	if (!s->current_path)
		goto out_current_path;
	set_percpu_current_path(s, NULL);

	if (percpu_counter_init(&s->repeat_count, 0, GFP_KERNEL))
		goto out_repeat_count;

	return s;

out_repeat_count:
	free_percpu(s->current_path);
out_current_path:
	kfree(s);
	return NULL;;
}

static int rr_create(struct path_selector *ps, unsigned argc, char **argv)
{
	struct selector *s;

	s = alloc_selector();
	if (!s)
		return -ENOMEM;

	ps->context = s;
	return 0;
}

static void rr_destroy(struct path_selector *ps)
{
	struct selector *s = ps->context;

	free_paths(&s->valid_paths);
	free_paths(&s->invalid_paths);
	free_percpu(s->current_path);
	percpu_counter_destroy(&s->repeat_count);
	kfree(s);
	ps->context = NULL;
}

static int rr_status(struct path_selector *ps, struct dm_path *path,
		     status_type_t type, char *result, unsigned int maxlen)
{
	struct path_info *pi;
	int sz = 0;

	if (!path)
		DMEMIT("0 ");
	else {
		switch(type) {
		case STATUSTYPE_INFO:
			break;
		case STATUSTYPE_TABLE:
			pi = path->pscontext;
			DMEMIT("%u ", pi->repeat_count);
			break;
		}
	}

	return sz;
}

/*
 * Called during initialisation to register each path with an
 * optional repeat_count.
 */
static int rr_add_path(struct path_selector *ps, struct dm_path *path,
		       int argc, char **argv, char **error)
{
	struct selector *s = ps->context;
	struct path_info *pi;
	unsigned repeat_count = RR_MIN_IO;
	char dummy;
	unsigned long flags;

	if (argc > 1) {
		*error = "round-robin ps: incorrect number of arguments";
		return -EINVAL;
	}

	/* First path argument is number of I/Os before switching path */
	if ((argc == 1) && (sscanf(argv[0], "%u%c", &repeat_count, &dummy) != 1)) {
		*error = "round-robin ps: invalid repeat count";
		return -EINVAL;
	}

	/* allocate the path */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "round-robin ps: Error allocating path context";
		return -ENOMEM;
	}

	pi->path = path;
	pi->repeat_count = repeat_count;

	path->pscontext = pi;

	spin_lock_irqsave(&s->lock, flags);
	list_add_tail(&pi->list, &s->valid_paths);
	spin_unlock_irqrestore(&s->lock, flags);

	return 0;
}

static void rr_fail_path(struct path_selector *ps, struct dm_path *p)
{
	unsigned long flags;
	struct selector *s = ps->context;
	struct path_info *pi = p->pscontext;

	spin_lock_irqsave(&s->lock, flags);
	if (p == *this_cpu_ptr(s->current_path))
		set_percpu_current_path(s, NULL);

	list_move(&pi->list, &s->invalid_paths);
	spin_unlock_irqrestore(&s->lock, flags);
}

static int rr_reinstate_path(struct path_selector *ps, struct dm_path *p)
{
	unsigned long flags;
	struct selector *s = ps->context;
	struct path_info *pi = p->pscontext;

	spin_lock_irqsave(&s->lock, flags);
	list_move(&pi->list, &s->valid_paths);
	spin_unlock_irqrestore(&s->lock, flags);

	return 0;
}

static struct dm_path *rr_select_path(struct path_selector *ps, size_t nr_bytes)
{
	unsigned long flags;
	struct selector *s = ps->context;
	struct path_info *pi = NULL;
	struct dm_path *current_path = NULL;

	local_irq_save(flags);
	current_path = *this_cpu_ptr(s->current_path);
	if (current_path) {
		percpu_counter_dec(&s->repeat_count);
		if (percpu_counter_read_positive(&s->repeat_count) > 0) {
			local_irq_restore(flags);
			return current_path;
		}
	}

	spin_lock(&s->lock);
	if (!list_empty(&s->valid_paths)) {
		pi = list_entry(s->valid_paths.next, struct path_info, list);
		list_move_tail(&pi->list, &s->valid_paths);
		percpu_counter_set(&s->repeat_count, pi->repeat_count);
		set_percpu_current_path(s, pi->path);
		current_path = pi->path;
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return current_path;
}

static struct path_selector_type rr_ps = {
	.name = "round-robin",
	.module = THIS_MODULE,
	.table_args = 1,
	.info_args = 0,
	.create = rr_create,
	.destroy = rr_destroy,
	.status = rr_status,
	.add_path = rr_add_path,
	.fail_path = rr_fail_path,
	.reinstate_path = rr_reinstate_path,
	.select_path = rr_select_path,
};

static int __init dm_rr_init(void)
{
	int r = dm_register_path_selector(&rr_ps);

	if (r < 0)
		DMERR("register failed %d", r);

	DMINFO("version " RR_VERSION " loaded");

	return r;
}

static void __exit dm_rr_exit(void)
{
	int r = dm_unregister_path_selector(&rr_ps);

	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(dm_rr_init);
module_exit(dm_rr_exit);

MODULE_DESCRIPTION(DM_NAME " round-robin multipath path selector");
MODULE_AUTHOR("Sistina Software <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");

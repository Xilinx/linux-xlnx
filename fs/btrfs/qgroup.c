/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/btrfs.h>

#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "locking.h"
#include "ulist.h"
#include "backref.h"
#include "extent_io.h"
#include "qgroup.h"


/* TODO XXX FIXME
 *  - subvol delete -> delete when ref goes to 0? delete limits also?
 *  - reorganize keys
 *  - compressed
 *  - sync
 *  - copy also limits on subvol creation
 *  - limit
 *  - caches fuer ulists
 *  - performance benchmarks
 *  - check all ioctl parameters
 */

/*
 * one struct for each qgroup, organized in fs_info->qgroup_tree.
 */
struct btrfs_qgroup {
	u64 qgroupid;

	/*
	 * state
	 */
	u64 rfer;	/* referenced */
	u64 rfer_cmpr;	/* referenced compressed */
	u64 excl;	/* exclusive */
	u64 excl_cmpr;	/* exclusive compressed */

	/*
	 * limits
	 */
	u64 lim_flags;	/* which limits are set */
	u64 max_rfer;
	u64 max_excl;
	u64 rsv_rfer;
	u64 rsv_excl;

	/*
	 * reservation tracking
	 */
	u64 reserved;

	/*
	 * lists
	 */
	struct list_head groups;  /* groups this group is member of */
	struct list_head members; /* groups that are members of this group */
	struct list_head dirty;   /* dirty groups */
	struct rb_node node;	  /* tree of qgroups */

	/*
	 * temp variables for accounting operations
	 * Refer to qgroup_shared_accounting() for details.
	 */
	u64 old_refcnt;
	u64 new_refcnt;
};

static void btrfs_qgroup_update_old_refcnt(struct btrfs_qgroup *qg, u64 seq,
					   int mod)
{
	if (qg->old_refcnt < seq)
		qg->old_refcnt = seq;
	qg->old_refcnt += mod;
}

static void btrfs_qgroup_update_new_refcnt(struct btrfs_qgroup *qg, u64 seq,
					   int mod)
{
	if (qg->new_refcnt < seq)
		qg->new_refcnt = seq;
	qg->new_refcnt += mod;
}

static inline u64 btrfs_qgroup_get_old_refcnt(struct btrfs_qgroup *qg, u64 seq)
{
	if (qg->old_refcnt < seq)
		return 0;
	return qg->old_refcnt - seq;
}

static inline u64 btrfs_qgroup_get_new_refcnt(struct btrfs_qgroup *qg, u64 seq)
{
	if (qg->new_refcnt < seq)
		return 0;
	return qg->new_refcnt - seq;
}

/*
 * glue structure to represent the relations between qgroups.
 */
struct btrfs_qgroup_list {
	struct list_head next_group;
	struct list_head next_member;
	struct btrfs_qgroup *group;
	struct btrfs_qgroup *member;
};

#define ptr_to_u64(x) ((u64)(uintptr_t)x)
#define u64_to_ptr(x) ((struct btrfs_qgroup *)(uintptr_t)x)

static int
qgroup_rescan_init(struct btrfs_fs_info *fs_info, u64 progress_objectid,
		   int init_flags);
static void qgroup_rescan_zero_tracking(struct btrfs_fs_info *fs_info);

/* must be called with qgroup_ioctl_lock held */
static struct btrfs_qgroup *find_qgroup_rb(struct btrfs_fs_info *fs_info,
					   u64 qgroupid)
{
	struct rb_node *n = fs_info->qgroup_tree.rb_node;
	struct btrfs_qgroup *qgroup;

	while (n) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		if (qgroup->qgroupid < qgroupid)
			n = n->rb_left;
		else if (qgroup->qgroupid > qgroupid)
			n = n->rb_right;
		else
			return qgroup;
	}
	return NULL;
}

/* must be called with qgroup_lock held */
static struct btrfs_qgroup *add_qgroup_rb(struct btrfs_fs_info *fs_info,
					  u64 qgroupid)
{
	struct rb_node **p = &fs_info->qgroup_tree.rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_qgroup *qgroup;

	while (*p) {
		parent = *p;
		qgroup = rb_entry(parent, struct btrfs_qgroup, node);

		if (qgroup->qgroupid < qgroupid)
			p = &(*p)->rb_left;
		else if (qgroup->qgroupid > qgroupid)
			p = &(*p)->rb_right;
		else
			return qgroup;
	}

	qgroup = kzalloc(sizeof(*qgroup), GFP_ATOMIC);
	if (!qgroup)
		return ERR_PTR(-ENOMEM);

	qgroup->qgroupid = qgroupid;
	INIT_LIST_HEAD(&qgroup->groups);
	INIT_LIST_HEAD(&qgroup->members);
	INIT_LIST_HEAD(&qgroup->dirty);

	rb_link_node(&qgroup->node, parent, p);
	rb_insert_color(&qgroup->node, &fs_info->qgroup_tree);

	return qgroup;
}

static void __del_qgroup_rb(struct btrfs_qgroup *qgroup)
{
	struct btrfs_qgroup_list *list;

	list_del(&qgroup->dirty);
	while (!list_empty(&qgroup->groups)) {
		list = list_first_entry(&qgroup->groups,
					struct btrfs_qgroup_list, next_group);
		list_del(&list->next_group);
		list_del(&list->next_member);
		kfree(list);
	}

	while (!list_empty(&qgroup->members)) {
		list = list_first_entry(&qgroup->members,
					struct btrfs_qgroup_list, next_member);
		list_del(&list->next_group);
		list_del(&list->next_member);
		kfree(list);
	}
	kfree(qgroup);
}

/* must be called with qgroup_lock held */
static int del_qgroup_rb(struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	struct btrfs_qgroup *qgroup = find_qgroup_rb(fs_info, qgroupid);

	if (!qgroup)
		return -ENOENT;

	rb_erase(&qgroup->node, &fs_info->qgroup_tree);
	__del_qgroup_rb(qgroup);
	return 0;
}

/* must be called with qgroup_lock held */
static int add_relation_rb(struct btrfs_fs_info *fs_info,
			   u64 memberid, u64 parentid)
{
	struct btrfs_qgroup *member;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup_list *list;

	member = find_qgroup_rb(fs_info, memberid);
	parent = find_qgroup_rb(fs_info, parentid);
	if (!member || !parent)
		return -ENOENT;

	list = kzalloc(sizeof(*list), GFP_ATOMIC);
	if (!list)
		return -ENOMEM;

	list->group = parent;
	list->member = member;
	list_add_tail(&list->next_group, &member->groups);
	list_add_tail(&list->next_member, &parent->members);

	return 0;
}

/* must be called with qgroup_lock held */
static int del_relation_rb(struct btrfs_fs_info *fs_info,
			   u64 memberid, u64 parentid)
{
	struct btrfs_qgroup *member;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup_list *list;

	member = find_qgroup_rb(fs_info, memberid);
	parent = find_qgroup_rb(fs_info, parentid);
	if (!member || !parent)
		return -ENOENT;

	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent) {
			list_del(&list->next_group);
			list_del(&list->next_member);
			kfree(list);
			return 0;
		}
	}
	return -ENOENT;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
int btrfs_verify_qgroup_counts(struct btrfs_fs_info *fs_info, u64 qgroupid,
			       u64 rfer, u64 excl)
{
	struct btrfs_qgroup *qgroup;

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup)
		return -EINVAL;
	if (qgroup->rfer != rfer || qgroup->excl != excl)
		return -EINVAL;
	return 0;
}
#endif

/*
 * The full config is read in one go, only called from open_ctree()
 * It doesn't use any locking, as at this point we're still single-threaded
 */
int btrfs_read_qgroup_config(struct btrfs_fs_info *fs_info)
{
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_path *path = NULL;
	struct extent_buffer *l;
	int slot;
	int ret = 0;
	u64 flags = 0;
	u64 rescan_progress = 0;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		return 0;

	fs_info->qgroup_ulist = ulist_alloc(GFP_NOFS);
	if (!fs_info->qgroup_ulist) {
		ret = -ENOMEM;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/* default this to quota off, in case no status key is found */
	fs_info->qgroup_flags = 0;

	/*
	 * pass 1: read status, all qgroup infos and limits
	 */
	key.objectid = 0;
	key.type = 0;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(quota_root, &key, path, 1, 1);
	if (ret)
		goto out;

	while (1) {
		struct btrfs_qgroup *qgroup;

		slot = path->slots[0];
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);

		if (found_key.type == BTRFS_QGROUP_STATUS_KEY) {
			struct btrfs_qgroup_status_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_status_item);

			if (btrfs_qgroup_status_version(l, ptr) !=
			    BTRFS_QGROUP_STATUS_VERSION) {
				btrfs_err(fs_info,
				 "old qgroup version, quota disabled");
				goto out;
			}
			if (btrfs_qgroup_status_generation(l, ptr) !=
			    fs_info->generation) {
				flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
				btrfs_err(fs_info,
					"qgroup generation mismatch, marked as inconsistent");
			}
			fs_info->qgroup_flags = btrfs_qgroup_status_flags(l,
									  ptr);
			rescan_progress = btrfs_qgroup_status_rescan(l, ptr);
			goto next1;
		}

		if (found_key.type != BTRFS_QGROUP_INFO_KEY &&
		    found_key.type != BTRFS_QGROUP_LIMIT_KEY)
			goto next1;

		qgroup = find_qgroup_rb(fs_info, found_key.offset);
		if ((qgroup && found_key.type == BTRFS_QGROUP_INFO_KEY) ||
		    (!qgroup && found_key.type == BTRFS_QGROUP_LIMIT_KEY)) {
			btrfs_err(fs_info, "inconsistent qgroup config");
			flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		}
		if (!qgroup) {
			qgroup = add_qgroup_rb(fs_info, found_key.offset);
			if (IS_ERR(qgroup)) {
				ret = PTR_ERR(qgroup);
				goto out;
			}
		}
		switch (found_key.type) {
		case BTRFS_QGROUP_INFO_KEY: {
			struct btrfs_qgroup_info_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_info_item);
			qgroup->rfer = btrfs_qgroup_info_rfer(l, ptr);
			qgroup->rfer_cmpr = btrfs_qgroup_info_rfer_cmpr(l, ptr);
			qgroup->excl = btrfs_qgroup_info_excl(l, ptr);
			qgroup->excl_cmpr = btrfs_qgroup_info_excl_cmpr(l, ptr);
			/* generation currently unused */
			break;
		}
		case BTRFS_QGROUP_LIMIT_KEY: {
			struct btrfs_qgroup_limit_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_limit_item);
			qgroup->lim_flags = btrfs_qgroup_limit_flags(l, ptr);
			qgroup->max_rfer = btrfs_qgroup_limit_max_rfer(l, ptr);
			qgroup->max_excl = btrfs_qgroup_limit_max_excl(l, ptr);
			qgroup->rsv_rfer = btrfs_qgroup_limit_rsv_rfer(l, ptr);
			qgroup->rsv_excl = btrfs_qgroup_limit_rsv_excl(l, ptr);
			break;
		}
		}
next1:
		ret = btrfs_next_item(quota_root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;
	}
	btrfs_release_path(path);

	/*
	 * pass 2: read all qgroup relations
	 */
	key.objectid = 0;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(quota_root, &key, path, 1, 0);
	if (ret)
		goto out;
	while (1) {
		slot = path->slots[0];
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);

		if (found_key.type != BTRFS_QGROUP_RELATION_KEY)
			goto next2;

		if (found_key.objectid > found_key.offset) {
			/* parent <- member, not needed to build config */
			/* FIXME should we omit the key completely? */
			goto next2;
		}

		ret = add_relation_rb(fs_info, found_key.objectid,
				      found_key.offset);
		if (ret == -ENOENT) {
			btrfs_warn(fs_info,
				"orphan qgroup relation 0x%llx->0x%llx",
				found_key.objectid, found_key.offset);
			ret = 0;	/* ignore the error */
		}
		if (ret)
			goto out;
next2:
		ret = btrfs_next_item(quota_root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;
	}
out:
	fs_info->qgroup_flags |= flags;
	if (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON))
		clear_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
	else if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN &&
		 ret >= 0)
		ret = qgroup_rescan_init(fs_info, rescan_progress, 0);
	btrfs_free_path(path);

	if (ret < 0) {
		ulist_free(fs_info->qgroup_ulist);
		fs_info->qgroup_ulist = NULL;
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	}

	return ret < 0 ? ret : 0;
}

/*
 * This is called from close_ctree() or open_ctree() or btrfs_quota_disable(),
 * first two are in single-threaded paths.And for the third one, we have set
 * quota_root to be null with qgroup_lock held before, so it is safe to clean
 * up the in-memory structures without qgroup_lock held.
 */
void btrfs_free_qgroup_config(struct btrfs_fs_info *fs_info)
{
	struct rb_node *n;
	struct btrfs_qgroup *qgroup;

	while ((n = rb_first(&fs_info->qgroup_tree))) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		rb_erase(n, &fs_info->qgroup_tree);
		__del_qgroup_rb(qgroup);
	}
	/*
	 * we call btrfs_free_qgroup_config() when umounting
	 * filesystem and disabling quota, so we set qgroup_ulist
	 * to be null here to avoid double free.
	 */
	ulist_free(fs_info->qgroup_ulist);
	fs_info->qgroup_ulist = NULL;
}

static int add_qgroup_relation_item(struct btrfs_trans_handle *trans,
				    struct btrfs_root *quota_root,
				    u64 src, u64 dst)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = src;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = dst;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key, 0);

	btrfs_mark_buffer_dirty(path->nodes[0]);

	btrfs_free_path(path);
	return ret;
}

static int del_qgroup_relation_item(struct btrfs_trans_handle *trans,
				    struct btrfs_root *quota_root,
				    u64 src, u64 dst)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = src;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = dst;

	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);
out:
	btrfs_free_path(path);
	return ret;
}

static int add_qgroup_item(struct btrfs_trans_handle *trans,
			   struct btrfs_root *quota_root, u64 qgroupid)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_qgroup_info_item *qgroup_info;
	struct btrfs_qgroup_limit_item *qgroup_limit;
	struct extent_buffer *leaf;
	struct btrfs_key key;

	if (btrfs_is_testing(quota_root->fs_info))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroupid;

	/*
	 * Avoid a transaction abort by catching -EEXIST here. In that
	 * case, we proceed by re-initializing the existing structure
	 * on disk.
	 */

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*qgroup_info));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	qgroup_info = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_info_item);
	btrfs_set_qgroup_info_generation(leaf, qgroup_info, trans->transid);
	btrfs_set_qgroup_info_rfer(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_rfer_cmpr(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_excl(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_excl_cmpr(leaf, qgroup_info, 0);

	btrfs_mark_buffer_dirty(leaf);

	btrfs_release_path(path);

	key.type = BTRFS_QGROUP_LIMIT_KEY;
	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*qgroup_limit));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	qgroup_limit = btrfs_item_ptr(leaf, path->slots[0],
				  struct btrfs_qgroup_limit_item);
	btrfs_set_qgroup_limit_flags(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_max_rfer(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_max_excl(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_rsv_rfer(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_rsv_excl(leaf, qgroup_limit, 0);

	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int del_qgroup_item(struct btrfs_trans_handle *trans,
			   struct btrfs_root *quota_root, u64 qgroupid)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroupid;
	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);
	if (ret)
		goto out;

	btrfs_release_path(path);

	key.type = BTRFS_QGROUP_LIMIT_KEY;
	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_limit_item(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    struct btrfs_qgroup *qgroup)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_limit_item *qgroup_limit;
	int ret;
	int slot;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_LIMIT_KEY;
	key.offset = qgroup->qgroupid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	qgroup_limit = btrfs_item_ptr(l, slot, struct btrfs_qgroup_limit_item);
	btrfs_set_qgroup_limit_flags(l, qgroup_limit, qgroup->lim_flags);
	btrfs_set_qgroup_limit_max_rfer(l, qgroup_limit, qgroup->max_rfer);
	btrfs_set_qgroup_limit_max_excl(l, qgroup_limit, qgroup->max_excl);
	btrfs_set_qgroup_limit_rsv_rfer(l, qgroup_limit, qgroup->rsv_rfer);
	btrfs_set_qgroup_limit_rsv_excl(l, qgroup_limit, qgroup->rsv_excl);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_info_item(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_qgroup *qgroup)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_info_item *qgroup_info;
	int ret;
	int slot;

	if (btrfs_is_testing(root->fs_info))
		return 0;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroup->qgroupid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	qgroup_info = btrfs_item_ptr(l, slot, struct btrfs_qgroup_info_item);
	btrfs_set_qgroup_info_generation(l, qgroup_info, trans->transid);
	btrfs_set_qgroup_info_rfer(l, qgroup_info, qgroup->rfer);
	btrfs_set_qgroup_info_rfer_cmpr(l, qgroup_info, qgroup->rfer_cmpr);
	btrfs_set_qgroup_info_excl(l, qgroup_info, qgroup->excl);
	btrfs_set_qgroup_info_excl_cmpr(l, qgroup_info, qgroup->excl_cmpr);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_status_item(struct btrfs_trans_handle *trans,
				     struct btrfs_fs_info *fs_info,
				    struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_status_item *ptr;
	int ret;
	int slot;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_STATUS_KEY;
	key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr(l, slot, struct btrfs_qgroup_status_item);
	btrfs_set_qgroup_status_flags(l, ptr, fs_info->qgroup_flags);
	btrfs_set_qgroup_status_generation(l, ptr, trans->transid);
	btrfs_set_qgroup_status_rescan(l, ptr,
				fs_info->qgroup_rescan_progress.objectid);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * called with qgroup_lock held
 */
static int btrfs_clean_quota_tree(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf = NULL;
	int ret;
	int nr = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;

	key.objectid = 0;
	key.offset = 0;
	key.type = 0;

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;
		leaf = path->nodes[0];
		nr = btrfs_header_nritems(leaf);
		if (!nr)
			break;
		/*
		 * delete the leaf one by one
		 * since the whole tree is going
		 * to be deleted.
		 */
		path->slots[0] = 0;
		ret = btrfs_del_items(trans, root, path, 0, nr);
		if (ret)
			goto out;

		btrfs_release_path(path);
	}
	ret = 0;
out:
	set_bit(BTRFS_FS_QUOTA_DISABLING, &root->fs_info->flags);
	btrfs_free_path(path);
	return ret;
}

int btrfs_quota_enable(struct btrfs_trans_handle *trans,
		       struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *quota_root;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_path *path = NULL;
	struct btrfs_qgroup_status_item *ptr;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_qgroup *qgroup = NULL;
	int ret = 0;
	int slot;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (fs_info->quota_root) {
		set_bit(BTRFS_FS_QUOTA_ENABLING, &fs_info->flags);
		goto out;
	}

	fs_info->qgroup_ulist = ulist_alloc(GFP_NOFS);
	if (!fs_info->qgroup_ulist) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * initially create the quota tree
	 */
	quota_root = btrfs_create_tree(trans, fs_info,
				       BTRFS_QUOTA_TREE_OBJECTID);
	if (IS_ERR(quota_root)) {
		ret =  PTR_ERR(quota_root);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out_free_root;
	}

	key.objectid = 0;
	key.type = BTRFS_QGROUP_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*ptr));
	if (ret)
		goto out_free_path;

	leaf = path->nodes[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_status_item);
	btrfs_set_qgroup_status_generation(leaf, ptr, trans->transid);
	btrfs_set_qgroup_status_version(leaf, ptr, BTRFS_QGROUP_STATUS_VERSION);
	fs_info->qgroup_flags = BTRFS_QGROUP_STATUS_FLAG_ON |
				BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	btrfs_set_qgroup_status_flags(leaf, ptr, fs_info->qgroup_flags);
	btrfs_set_qgroup_status_rescan(leaf, ptr, 0);

	btrfs_mark_buffer_dirty(leaf);

	key.objectid = 0;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = 0;

	btrfs_release_path(path);
	ret = btrfs_search_slot_for_read(tree_root, &key, path, 1, 0);
	if (ret > 0)
		goto out_add_root;
	if (ret < 0)
		goto out_free_path;


	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.type == BTRFS_ROOT_REF_KEY) {
			ret = add_qgroup_item(trans, quota_root,
					      found_key.offset);
			if (ret)
				goto out_free_path;

			qgroup = add_qgroup_rb(fs_info, found_key.offset);
			if (IS_ERR(qgroup)) {
				ret = PTR_ERR(qgroup);
				goto out_free_path;
			}
		}
		ret = btrfs_next_item(tree_root, path);
		if (ret < 0)
			goto out_free_path;
		if (ret)
			break;
	}

out_add_root:
	btrfs_release_path(path);
	ret = add_qgroup_item(trans, quota_root, BTRFS_FS_TREE_OBJECTID);
	if (ret)
		goto out_free_path;

	qgroup = add_qgroup_rb(fs_info, BTRFS_FS_TREE_OBJECTID);
	if (IS_ERR(qgroup)) {
		ret = PTR_ERR(qgroup);
		goto out_free_path;
	}
	spin_lock(&fs_info->qgroup_lock);
	fs_info->quota_root = quota_root;
	set_bit(BTRFS_FS_QUOTA_ENABLING, &fs_info->flags);
	spin_unlock(&fs_info->qgroup_lock);
out_free_path:
	btrfs_free_path(path);
out_free_root:
	if (ret) {
		free_extent_buffer(quota_root->node);
		free_extent_buffer(quota_root->commit_root);
		kfree(quota_root);
	}
out:
	if (ret) {
		ulist_free(fs_info->qgroup_ulist);
		fs_info->qgroup_ulist = NULL;
	}
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_quota_disable(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *quota_root;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root)
		goto out;
	clear_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
	set_bit(BTRFS_FS_QUOTA_DISABLING, &fs_info->flags);
	btrfs_qgroup_wait_for_completion(fs_info, false);
	spin_lock(&fs_info->qgroup_lock);
	quota_root = fs_info->quota_root;
	fs_info->quota_root = NULL;
	fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_ON;
	spin_unlock(&fs_info->qgroup_lock);

	btrfs_free_qgroup_config(fs_info);

	ret = btrfs_clean_quota_tree(trans, quota_root);
	if (ret)
		goto out;

	ret = btrfs_del_root(trans, tree_root, &quota_root->root_key);
	if (ret)
		goto out;

	list_del(&quota_root->dirty_list);

	btrfs_tree_lock(quota_root->node);
	clean_tree_block(trans, tree_root->fs_info, quota_root->node);
	btrfs_tree_unlock(quota_root->node);
	btrfs_free_tree_block(trans, quota_root, quota_root->node, 0, 1);

	free_extent_buffer(quota_root->node);
	free_extent_buffer(quota_root->commit_root);
	kfree(quota_root);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

static void qgroup_dirty(struct btrfs_fs_info *fs_info,
			 struct btrfs_qgroup *qgroup)
{
	if (list_empty(&qgroup->dirty))
		list_add(&qgroup->dirty, &fs_info->dirty_qgroups);
}

/*
 * The easy accounting, if we are adding/removing the only ref for an extent
 * then this qgroup and all of the parent qgroups get their reference and
 * exclusive counts adjusted.
 *
 * Caller should hold fs_info->qgroup_lock.
 */
static int __qgroup_excl_accounting(struct btrfs_fs_info *fs_info,
				    struct ulist *tmp, u64 ref_root,
				    u64 num_bytes, int sign)
{
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup_list *glist;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	int ret = 0;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	qgroup->rfer += sign * num_bytes;
	qgroup->rfer_cmpr += sign * num_bytes;

	WARN_ON(sign < 0 && qgroup->excl < num_bytes);
	qgroup->excl += sign * num_bytes;
	qgroup->excl_cmpr += sign * num_bytes;
	if (sign > 0)
		qgroup->reserved -= num_bytes;

	qgroup_dirty(fs_info, qgroup);

	/* Get all of the parent groups that contain this qgroup */
	list_for_each_entry(glist, &qgroup->groups, next_group) {
		ret = ulist_add(tmp, glist->group->qgroupid,
				ptr_to_u64(glist->group), GFP_ATOMIC);
		if (ret < 0)
			goto out;
	}

	/* Iterate all of the parents and adjust their reference counts */
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(tmp, &uiter))) {
		qgroup = u64_to_ptr(unode->aux);
		qgroup->rfer += sign * num_bytes;
		qgroup->rfer_cmpr += sign * num_bytes;
		WARN_ON(sign < 0 && qgroup->excl < num_bytes);
		qgroup->excl += sign * num_bytes;
		if (sign > 0)
			qgroup->reserved -= num_bytes;
		qgroup->excl_cmpr += sign * num_bytes;
		qgroup_dirty(fs_info, qgroup);

		/* Add any parents of the parents */
		list_for_each_entry(glist, &qgroup->groups, next_group) {
			ret = ulist_add(tmp, glist->group->qgroupid,
					ptr_to_u64(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;
out:
	return ret;
}


/*
 * Quick path for updating qgroup with only excl refs.
 *
 * In that case, just update all parent will be enough.
 * Or we needs to do a full rescan.
 * Caller should also hold fs_info->qgroup_lock.
 *
 * Return 0 for quick update, return >0 for need to full rescan
 * and mark INCONSISTENT flag.
 * Return < 0 for other error.
 */
static int quick_update_accounting(struct btrfs_fs_info *fs_info,
				   struct ulist *tmp, u64 src, u64 dst,
				   int sign)
{
	struct btrfs_qgroup *qgroup;
	int ret = 1;
	int err = 0;

	qgroup = find_qgroup_rb(fs_info, src);
	if (!qgroup)
		goto out;
	if (qgroup->excl == qgroup->rfer) {
		ret = 0;
		err = __qgroup_excl_accounting(fs_info, tmp, dst,
					       qgroup->excl, sign);
		if (err < 0) {
			ret = err;
			goto out;
		}
	}
out:
	if (ret)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	return ret;
}

int btrfs_add_qgroup_relation(struct btrfs_trans_handle *trans,
			      struct btrfs_fs_info *fs_info, u64 src, u64 dst)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup *member;
	struct btrfs_qgroup_list *list;
	struct ulist *tmp;
	int ret = 0;

	/* Check the level of src and dst first */
	if (btrfs_qgroup_level(src) >= btrfs_qgroup_level(dst))
		return -EINVAL;

	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp)
		return -ENOMEM;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}
	member = find_qgroup_rb(fs_info, src);
	parent = find_qgroup_rb(fs_info, dst);
	if (!member || !parent) {
		ret = -EINVAL;
		goto out;
	}

	/* check if such qgroup relation exist firstly */
	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent) {
			ret = -EEXIST;
			goto out;
		}
	}

	ret = add_qgroup_relation_item(trans, quota_root, src, dst);
	if (ret)
		goto out;

	ret = add_qgroup_relation_item(trans, quota_root, dst, src);
	if (ret) {
		del_qgroup_relation_item(trans, quota_root, src, dst);
		goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	ret = add_relation_rb(quota_root->fs_info, src, dst);
	if (ret < 0) {
		spin_unlock(&fs_info->qgroup_lock);
		goto out;
	}
	ret = quick_update_accounting(fs_info, tmp, src, dst, 1);
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	ulist_free(tmp);
	return ret;
}

int __del_qgroup_relation(struct btrfs_trans_handle *trans,
			      struct btrfs_fs_info *fs_info, u64 src, u64 dst)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup *member;
	struct btrfs_qgroup_list *list;
	struct ulist *tmp;
	int ret = 0;
	int err;

	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp)
		return -ENOMEM;

	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	member = find_qgroup_rb(fs_info, src);
	parent = find_qgroup_rb(fs_info, dst);
	if (!member || !parent) {
		ret = -EINVAL;
		goto out;
	}

	/* check if such qgroup relation exist firstly */
	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent)
			goto exist;
	}
	ret = -ENOENT;
	goto out;
exist:
	ret = del_qgroup_relation_item(trans, quota_root, src, dst);
	err = del_qgroup_relation_item(trans, quota_root, dst, src);
	if (err && !ret)
		ret = err;

	spin_lock(&fs_info->qgroup_lock);
	del_relation_rb(fs_info, src, dst);
	ret = quick_update_accounting(fs_info, tmp, src, dst, -1);
	spin_unlock(&fs_info->qgroup_lock);
out:
	ulist_free(tmp);
	return ret;
}

int btrfs_del_qgroup_relation(struct btrfs_trans_handle *trans,
			      struct btrfs_fs_info *fs_info, u64 src, u64 dst)
{
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	ret = __del_qgroup_relation(trans, fs_info, src, dst);
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	return ret;
}

int btrfs_create_qgroup(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}
	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (qgroup) {
		ret = -EEXIST;
		goto out;
	}

	ret = add_qgroup_item(trans, quota_root, qgroupid);
	if (ret)
		goto out;

	spin_lock(&fs_info->qgroup_lock);
	qgroup = add_qgroup_rb(fs_info, qgroupid);
	spin_unlock(&fs_info->qgroup_lock);

	if (IS_ERR(qgroup))
		ret = PTR_ERR(qgroup);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_remove_qgroup(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup_list *list;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	} else {
		/* check if there are no children of this qgroup */
		if (!list_empty(&qgroup->members)) {
			ret = -EBUSY;
			goto out;
		}
	}
	ret = del_qgroup_item(trans, quota_root, qgroupid);

	while (!list_empty(&qgroup->groups)) {
		list = list_first_entry(&qgroup->groups,
					struct btrfs_qgroup_list, next_group);
		ret = __del_qgroup_relation(trans, fs_info,
					   qgroupid,
					   list->group->qgroupid);
		if (ret)
			goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	del_qgroup_rb(quota_root->fs_info, qgroupid);
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_limit_qgroup(struct btrfs_trans_handle *trans,
		       struct btrfs_fs_info *fs_info, u64 qgroupid,
		       struct btrfs_qgroup_limit *limit)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	int ret = 0;
	/* Sometimes we would want to clear the limit on this qgroup.
	 * To meet this requirement, we treat the -1 as a special value
	 * which tell kernel to clear the limit on this qgroup.
	 */
	const u64 CLEAR_VALUE = -1;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	if (limit->flags & BTRFS_QGROUP_LIMIT_MAX_RFER) {
		if (limit->max_rfer == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_MAX_RFER;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_MAX_RFER;
			qgroup->max_rfer = 0;
		} else {
			qgroup->max_rfer = limit->max_rfer;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_MAX_EXCL) {
		if (limit->max_excl == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_MAX_EXCL;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_MAX_EXCL;
			qgroup->max_excl = 0;
		} else {
			qgroup->max_excl = limit->max_excl;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_RSV_RFER) {
		if (limit->rsv_rfer == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_RSV_RFER;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_RSV_RFER;
			qgroup->rsv_rfer = 0;
		} else {
			qgroup->rsv_rfer = limit->rsv_rfer;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_RSV_EXCL) {
		if (limit->rsv_excl == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_RSV_EXCL;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_RSV_EXCL;
			qgroup->rsv_excl = 0;
		} else {
			qgroup->rsv_excl = limit->rsv_excl;
		}
	}
	qgroup->lim_flags |= limit->flags;

	spin_unlock(&fs_info->qgroup_lock);

	ret = update_qgroup_limit_item(trans, quota_root, qgroup);
	if (ret) {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		btrfs_info(fs_info, "unable to update quota limit for %llu",
		       qgroupid);
	}

out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_qgroup_prepare_account_extents(struct btrfs_trans_handle *trans,
					 struct btrfs_fs_info *fs_info)
{
	struct btrfs_qgroup_extent_record *record;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct rb_node *node;
	u64 qgroup_to_skip;
	int ret = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	qgroup_to_skip = delayed_refs->qgroup_to_skip;

	/*
	 * No need to do lock, since this function will only be called in
	 * btrfs_commit_transaction().
	 */
	node = rb_first(&delayed_refs->dirty_extent_root);
	while (node) {
		record = rb_entry(node, struct btrfs_qgroup_extent_record,
				  node);
		ret = btrfs_find_all_roots(NULL, fs_info, record->bytenr, 0,
					   &record->old_roots);
		if (ret < 0)
			break;
		if (qgroup_to_skip)
			ulist_del(record->old_roots, qgroup_to_skip, 0);
		node = rb_next(node);
	}
	return ret;
}

int btrfs_qgroup_insert_dirty_extent_nolock(struct btrfs_fs_info *fs_info,
				struct btrfs_delayed_ref_root *delayed_refs,
				struct btrfs_qgroup_extent_record *record)
{
	struct rb_node **p = &delayed_refs->dirty_extent_root.rb_node;
	struct rb_node *parent_node = NULL;
	struct btrfs_qgroup_extent_record *entry;
	u64 bytenr = record->bytenr;

	assert_spin_locked(&delayed_refs->lock);
	trace_btrfs_qgroup_insert_dirty_extent(fs_info, record);

	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct btrfs_qgroup_extent_record,
				 node);
		if (bytenr < entry->bytenr)
			p = &(*p)->rb_left;
		else if (bytenr > entry->bytenr)
			p = &(*p)->rb_right;
		else
			return 1;
	}

	rb_link_node(&record->node, parent_node, p);
	rb_insert_color(&record->node, &delayed_refs->dirty_extent_root);
	return 0;
}

int btrfs_qgroup_insert_dirty_extent(struct btrfs_trans_handle *trans,
		struct btrfs_fs_info *fs_info, u64 bytenr, u64 num_bytes,
		gfp_t gfp_flag)
{
	struct btrfs_qgroup_extent_record *record;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags)
	    || bytenr == 0 || num_bytes == 0)
		return 0;
	if (WARN_ON(trans == NULL))
		return -EINVAL;
	record = kmalloc(sizeof(*record), gfp_flag);
	if (!record)
		return -ENOMEM;

	delayed_refs = &trans->transaction->delayed_refs;
	record->bytenr = bytenr;
	record->num_bytes = num_bytes;
	record->old_roots = NULL;

	spin_lock(&delayed_refs->lock);
	ret = btrfs_qgroup_insert_dirty_extent_nolock(fs_info, delayed_refs,
						      record);
	spin_unlock(&delayed_refs->lock);
	if (ret > 0)
		kfree(record);
	return 0;
}

#define UPDATE_NEW	0
#define UPDATE_OLD	1
/*
 * Walk all of the roots that points to the bytenr and adjust their refcnts.
 */
static int qgroup_update_refcnt(struct btrfs_fs_info *fs_info,
				struct ulist *roots, struct ulist *tmp,
				struct ulist *qgroups, u64 seq, int update_old)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct ulist_node *tmp_unode;
	struct ulist_iterator tmp_uiter;
	struct btrfs_qgroup *qg;
	int ret = 0;

	if (!roots)
		return 0;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(roots, &uiter))) {
		qg = find_qgroup_rb(fs_info, unode->val);
		if (!qg)
			continue;

		ulist_reinit(tmp);
		ret = ulist_add(qgroups, qg->qgroupid, ptr_to_u64(qg),
				GFP_ATOMIC);
		if (ret < 0)
			return ret;
		ret = ulist_add(tmp, qg->qgroupid, ptr_to_u64(qg), GFP_ATOMIC);
		if (ret < 0)
			return ret;
		ULIST_ITER_INIT(&tmp_uiter);
		while ((tmp_unode = ulist_next(tmp, &tmp_uiter))) {
			struct btrfs_qgroup_list *glist;

			qg = u64_to_ptr(tmp_unode->aux);
			if (update_old)
				btrfs_qgroup_update_old_refcnt(qg, seq, 1);
			else
				btrfs_qgroup_update_new_refcnt(qg, seq, 1);
			list_for_each_entry(glist, &qg->groups, next_group) {
				ret = ulist_add(qgroups, glist->group->qgroupid,
						ptr_to_u64(glist->group),
						GFP_ATOMIC);
				if (ret < 0)
					return ret;
				ret = ulist_add(tmp, glist->group->qgroupid,
						ptr_to_u64(glist->group),
						GFP_ATOMIC);
				if (ret < 0)
					return ret;
			}
		}
	}
	return 0;
}

/*
 * Update qgroup rfer/excl counters.
 * Rfer update is easy, codes can explain themselves.
 *
 * Excl update is tricky, the update is split into 2 part.
 * Part 1: Possible exclusive <-> sharing detect:
 *	|	A	|	!A	|
 *  -------------------------------------
 *  B	|	*	|	-	|
 *  -------------------------------------
 *  !B	|	+	|	**	|
 *  -------------------------------------
 *
 * Conditions:
 * A:	cur_old_roots < nr_old_roots	(not exclusive before)
 * !A:	cur_old_roots == nr_old_roots	(possible exclusive before)
 * B:	cur_new_roots < nr_new_roots	(not exclusive now)
 * !B:	cur_new_roots == nr_new_roots	(possible exclusive now)
 *
 * Results:
 * +: Possible sharing -> exclusive	-: Possible exclusive -> sharing
 * *: Definitely not changed.		**: Possible unchanged.
 *
 * For !A and !B condition, the exception is cur_old/new_roots == 0 case.
 *
 * To make the logic clear, we first use condition A and B to split
 * combination into 4 results.
 *
 * Then, for result "+" and "-", check old/new_roots == 0 case, as in them
 * only on variant maybe 0.
 *
 * Lastly, check result **, since there are 2 variants maybe 0, split them
 * again(2x2).
 * But this time we don't need to consider other things, the codes and logic
 * is easy to understand now.
 */
static int qgroup_update_counters(struct btrfs_fs_info *fs_info,
				  struct ulist *qgroups,
				  u64 nr_old_roots,
				  u64 nr_new_roots,
				  u64 num_bytes, u64 seq)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_qgroup *qg;
	u64 cur_new_count, cur_old_count;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(qgroups, &uiter))) {
		bool dirty = false;

		qg = u64_to_ptr(unode->aux);
		cur_old_count = btrfs_qgroup_get_old_refcnt(qg, seq);
		cur_new_count = btrfs_qgroup_get_new_refcnt(qg, seq);

		trace_qgroup_update_counters(fs_info, qg->qgroupid,
					     cur_old_count, cur_new_count);

		/* Rfer update part */
		if (cur_old_count == 0 && cur_new_count > 0) {
			qg->rfer += num_bytes;
			qg->rfer_cmpr += num_bytes;
			dirty = true;
		}
		if (cur_old_count > 0 && cur_new_count == 0) {
			qg->rfer -= num_bytes;
			qg->rfer_cmpr -= num_bytes;
			dirty = true;
		}

		/* Excl update part */
		/* Exclusive/none -> shared case */
		if (cur_old_count == nr_old_roots &&
		    cur_new_count < nr_new_roots) {
			/* Exclusive -> shared */
			if (cur_old_count != 0) {
				qg->excl -= num_bytes;
				qg->excl_cmpr -= num_bytes;
				dirty = true;
			}
		}

		/* Shared -> exclusive/none case */
		if (cur_old_count < nr_old_roots &&
		    cur_new_count == nr_new_roots) {
			/* Shared->exclusive */
			if (cur_new_count != 0) {
				qg->excl += num_bytes;
				qg->excl_cmpr += num_bytes;
				dirty = true;
			}
		}

		/* Exclusive/none -> exclusive/none case */
		if (cur_old_count == nr_old_roots &&
		    cur_new_count == nr_new_roots) {
			if (cur_old_count == 0) {
				/* None -> exclusive/none */

				if (cur_new_count != 0) {
					/* None -> exclusive */
					qg->excl += num_bytes;
					qg->excl_cmpr += num_bytes;
					dirty = true;
				}
				/* None -> none, nothing changed */
			} else {
				/* Exclusive -> exclusive/none */

				if (cur_new_count == 0) {
					/* Exclusive -> none */
					qg->excl -= num_bytes;
					qg->excl_cmpr -= num_bytes;
					dirty = true;
				}
				/* Exclusive -> exclusive, nothing changed */
			}
		}

		if (dirty)
			qgroup_dirty(fs_info, qg);
	}
	return 0;
}

int
btrfs_qgroup_account_extent(struct btrfs_trans_handle *trans,
			    struct btrfs_fs_info *fs_info,
			    u64 bytenr, u64 num_bytes,
			    struct ulist *old_roots, struct ulist *new_roots)
{
	struct ulist *qgroups = NULL;
	struct ulist *tmp = NULL;
	u64 seq;
	u64 nr_new_roots = 0;
	u64 nr_old_roots = 0;
	int ret = 0;

	if (new_roots)
		nr_new_roots = new_roots->nnodes;
	if (old_roots)
		nr_old_roots = old_roots->nnodes;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		goto out_free;
	BUG_ON(!fs_info->quota_root);

	trace_btrfs_qgroup_account_extent(fs_info, bytenr, num_bytes,
					  nr_old_roots, nr_new_roots);

	qgroups = ulist_alloc(GFP_NOFS);
	if (!qgroups) {
		ret = -ENOMEM;
		goto out_free;
	}
	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp) {
		ret = -ENOMEM;
		goto out_free;
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		if (fs_info->qgroup_rescan_progress.objectid <= bytenr) {
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			ret = 0;
			goto out_free;
		}
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	spin_lock(&fs_info->qgroup_lock);
	seq = fs_info->qgroup_seq;

	/* Update old refcnts using old_roots */
	ret = qgroup_update_refcnt(fs_info, old_roots, tmp, qgroups, seq,
				   UPDATE_OLD);
	if (ret < 0)
		goto out;

	/* Update new refcnts using new_roots */
	ret = qgroup_update_refcnt(fs_info, new_roots, tmp, qgroups, seq,
				   UPDATE_NEW);
	if (ret < 0)
		goto out;

	qgroup_update_counters(fs_info, qgroups, nr_old_roots, nr_new_roots,
			       num_bytes, seq);

	/*
	 * Bump qgroup_seq to avoid seq overlap
	 */
	fs_info->qgroup_seq += max(nr_old_roots, nr_new_roots) + 1;
out:
	spin_unlock(&fs_info->qgroup_lock);
out_free:
	ulist_free(tmp);
	ulist_free(qgroups);
	ulist_free(old_roots);
	ulist_free(new_roots);
	return ret;
}

int btrfs_qgroup_account_extents(struct btrfs_trans_handle *trans,
				 struct btrfs_fs_info *fs_info)
{
	struct btrfs_qgroup_extent_record *record;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct ulist *new_roots = NULL;
	struct rb_node *node;
	u64 qgroup_to_skip;
	int ret = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	qgroup_to_skip = delayed_refs->qgroup_to_skip;
	while ((node = rb_first(&delayed_refs->dirty_extent_root))) {
		record = rb_entry(node, struct btrfs_qgroup_extent_record,
				  node);

		trace_btrfs_qgroup_account_extents(fs_info, record);

		if (!ret) {
			/*
			 * Use (u64)-1 as time_seq to do special search, which
			 * doesn't lock tree or delayed_refs and search current
			 * root. It's safe inside commit_transaction().
			 */
			ret = btrfs_find_all_roots(trans, fs_info,
					record->bytenr, (u64)-1, &new_roots);
			if (ret < 0)
				goto cleanup;
			if (qgroup_to_skip)
				ulist_del(new_roots, qgroup_to_skip, 0);
			ret = btrfs_qgroup_account_extent(trans, fs_info,
					record->bytenr, record->num_bytes,
					record->old_roots, new_roots);
			record->old_roots = NULL;
			new_roots = NULL;
		}
cleanup:
		ulist_free(record->old_roots);
		ulist_free(new_roots);
		new_roots = NULL;
		rb_erase(node, &delayed_refs->dirty_extent_root);
		kfree(record);

	}
	return ret;
}

/*
 * called from commit_transaction. Writes all changed qgroups to disk.
 */
int btrfs_run_qgroups(struct btrfs_trans_handle *trans,
		      struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *quota_root = fs_info->quota_root;
	int ret = 0;
	int start_rescan_worker = 0;

	if (!quota_root)
		goto out;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags) &&
	    test_bit(BTRFS_FS_QUOTA_ENABLING, &fs_info->flags))
		start_rescan_worker = 1;

	if (test_and_clear_bit(BTRFS_FS_QUOTA_ENABLING, &fs_info->flags))
		set_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
	if (test_and_clear_bit(BTRFS_FS_QUOTA_DISABLING, &fs_info->flags))
		clear_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);

	spin_lock(&fs_info->qgroup_lock);
	while (!list_empty(&fs_info->dirty_qgroups)) {
		struct btrfs_qgroup *qgroup;
		qgroup = list_first_entry(&fs_info->dirty_qgroups,
					  struct btrfs_qgroup, dirty);
		list_del_init(&qgroup->dirty);
		spin_unlock(&fs_info->qgroup_lock);
		ret = update_qgroup_info_item(trans, quota_root, qgroup);
		if (ret)
			fs_info->qgroup_flags |=
					BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		ret = update_qgroup_limit_item(trans, quota_root, qgroup);
		if (ret)
			fs_info->qgroup_flags |=
					BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		spin_lock(&fs_info->qgroup_lock);
	}
	if (test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_ON;
	else
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_ON;
	spin_unlock(&fs_info->qgroup_lock);

	ret = update_qgroup_status_item(trans, fs_info, quota_root);
	if (ret)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;

	if (!ret && start_rescan_worker) {
		ret = qgroup_rescan_init(fs_info, 0, 1);
		if (!ret) {
			qgroup_rescan_zero_tracking(fs_info);
			btrfs_queue_work(fs_info->qgroup_rescan_workers,
					 &fs_info->qgroup_rescan_work);
		}
		ret = 0;
	}

out:

	return ret;
}

/*
 * Copy the accounting information between qgroups. This is necessary
 * when a snapshot or a subvolume is created. Throwing an error will
 * cause a transaction abort so we take extra care here to only error
 * when a readonly fs is a reasonable outcome.
 */
int btrfs_qgroup_inherit(struct btrfs_trans_handle *trans,
			 struct btrfs_fs_info *fs_info, u64 srcid, u64 objectid,
			 struct btrfs_qgroup_inherit *inherit)
{
	int ret = 0;
	int i;
	u64 *i_qgroups;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_qgroup *srcgroup;
	struct btrfs_qgroup *dstgroup;
	u32 level_size = 0;
	u64 nums;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		goto out;

	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	if (inherit) {
		i_qgroups = (u64 *)(inherit + 1);
		nums = inherit->num_qgroups + 2 * inherit->num_ref_copies +
		       2 * inherit->num_excl_copies;
		for (i = 0; i < nums; ++i) {
			srcgroup = find_qgroup_rb(fs_info, *i_qgroups);

			/*
			 * Zero out invalid groups so we can ignore
			 * them later.
			 */
			if (!srcgroup ||
			    ((srcgroup->qgroupid >> 48) <= (objectid >> 48)))
				*i_qgroups = 0ULL;

			++i_qgroups;
		}
	}

	/*
	 * create a tracking group for the subvol itself
	 */
	ret = add_qgroup_item(trans, quota_root, objectid);
	if (ret)
		goto out;

	if (srcid) {
		struct btrfs_root *srcroot;
		struct btrfs_key srckey;

		srckey.objectid = srcid;
		srckey.type = BTRFS_ROOT_ITEM_KEY;
		srckey.offset = (u64)-1;
		srcroot = btrfs_read_fs_root_no_name(fs_info, &srckey);
		if (IS_ERR(srcroot)) {
			ret = PTR_ERR(srcroot);
			goto out;
		}

		rcu_read_lock();
		level_size = srcroot->nodesize;
		rcu_read_unlock();
	}

	/*
	 * add qgroup to all inherited groups
	 */
	if (inherit) {
		i_qgroups = (u64 *)(inherit + 1);
		for (i = 0; i < inherit->num_qgroups; ++i, ++i_qgroups) {
			if (*i_qgroups == 0)
				continue;
			ret = add_qgroup_relation_item(trans, quota_root,
						       objectid, *i_qgroups);
			if (ret && ret != -EEXIST)
				goto out;
			ret = add_qgroup_relation_item(trans, quota_root,
						       *i_qgroups, objectid);
			if (ret && ret != -EEXIST)
				goto out;
		}
		ret = 0;
	}


	spin_lock(&fs_info->qgroup_lock);

	dstgroup = add_qgroup_rb(fs_info, objectid);
	if (IS_ERR(dstgroup)) {
		ret = PTR_ERR(dstgroup);
		goto unlock;
	}

	if (inherit && inherit->flags & BTRFS_QGROUP_INHERIT_SET_LIMITS) {
		dstgroup->lim_flags = inherit->lim.flags;
		dstgroup->max_rfer = inherit->lim.max_rfer;
		dstgroup->max_excl = inherit->lim.max_excl;
		dstgroup->rsv_rfer = inherit->lim.rsv_rfer;
		dstgroup->rsv_excl = inherit->lim.rsv_excl;

		ret = update_qgroup_limit_item(trans, quota_root, dstgroup);
		if (ret) {
			fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
			btrfs_info(fs_info,
				   "unable to update quota limit for %llu",
				   dstgroup->qgroupid);
			goto unlock;
		}
	}

	if (srcid) {
		srcgroup = find_qgroup_rb(fs_info, srcid);
		if (!srcgroup)
			goto unlock;

		/*
		 * We call inherit after we clone the root in order to make sure
		 * our counts don't go crazy, so at this point the only
		 * difference between the two roots should be the root node.
		 */
		dstgroup->rfer = srcgroup->rfer;
		dstgroup->rfer_cmpr = srcgroup->rfer_cmpr;
		dstgroup->excl = level_size;
		dstgroup->excl_cmpr = level_size;
		srcgroup->excl = level_size;
		srcgroup->excl_cmpr = level_size;

		/* inherit the limit info */
		dstgroup->lim_flags = srcgroup->lim_flags;
		dstgroup->max_rfer = srcgroup->max_rfer;
		dstgroup->max_excl = srcgroup->max_excl;
		dstgroup->rsv_rfer = srcgroup->rsv_rfer;
		dstgroup->rsv_excl = srcgroup->rsv_excl;

		qgroup_dirty(fs_info, dstgroup);
		qgroup_dirty(fs_info, srcgroup);
	}

	if (!inherit)
		goto unlock;

	i_qgroups = (u64 *)(inherit + 1);
	for (i = 0; i < inherit->num_qgroups; ++i) {
		if (*i_qgroups) {
			ret = add_relation_rb(quota_root->fs_info, objectid,
					      *i_qgroups);
			if (ret)
				goto unlock;
		}
		++i_qgroups;
	}

	for (i = 0; i <  inherit->num_ref_copies; ++i, i_qgroups += 2) {
		struct btrfs_qgroup *src;
		struct btrfs_qgroup *dst;

		if (!i_qgroups[0] || !i_qgroups[1])
			continue;

		src = find_qgroup_rb(fs_info, i_qgroups[0]);
		dst = find_qgroup_rb(fs_info, i_qgroups[1]);

		if (!src || !dst) {
			ret = -EINVAL;
			goto unlock;
		}

		dst->rfer = src->rfer - level_size;
		dst->rfer_cmpr = src->rfer_cmpr - level_size;
	}
	for (i = 0; i <  inherit->num_excl_copies; ++i, i_qgroups += 2) {
		struct btrfs_qgroup *src;
		struct btrfs_qgroup *dst;

		if (!i_qgroups[0] || !i_qgroups[1])
			continue;

		src = find_qgroup_rb(fs_info, i_qgroups[0]);
		dst = find_qgroup_rb(fs_info, i_qgroups[1]);

		if (!src || !dst) {
			ret = -EINVAL;
			goto unlock;
		}

		dst->excl = src->excl + level_size;
		dst->excl_cmpr = src->excl_cmpr + level_size;
	}

unlock:
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

static int qgroup_reserve(struct btrfs_root *root, u64 num_bytes)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 ref_root = root->root_key.objectid;
	int ret = 0;
	struct ulist_node *unode;
	struct ulist_iterator uiter;

	if (!is_fstree(ref_root))
		return 0;

	if (num_bytes == 0)
		return 0;

	spin_lock(&fs_info->qgroup_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	/*
	 * in a first step, we check all affected qgroups if any limits would
	 * be exceeded
	 */
	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			(uintptr_t)qgroup, GFP_ATOMIC);
	if (ret < 0)
		goto out;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = u64_to_ptr(unode->aux);

		if ((qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_RFER) &&
		    qg->reserved + (s64)qg->rfer + num_bytes >
		    qg->max_rfer) {
			ret = -EDQUOT;
			goto out;
		}

		if ((qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_EXCL) &&
		    qg->reserved + (s64)qg->excl + num_bytes >
		    qg->max_excl) {
			ret = -EDQUOT;
			goto out;
		}

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					(uintptr_t)glist->group, GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;
	/*
	 * no limits exceeded, now record the reservation into all qgroups
	 */
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;

		qg = u64_to_ptr(unode->aux);

		qg->reserved += num_bytes;
	}

out:
	spin_unlock(&fs_info->qgroup_lock);
	return ret;
}

void btrfs_qgroup_free_refroot(struct btrfs_fs_info *fs_info,
			       u64 ref_root, u64 num_bytes)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	int ret = 0;

	if (!is_fstree(ref_root))
		return;

	if (num_bytes == 0)
		return;

	spin_lock(&fs_info->qgroup_lock);

	quota_root = fs_info->quota_root;
	if (!quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			(uintptr_t)qgroup, GFP_ATOMIC);
	if (ret < 0)
		goto out;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = u64_to_ptr(unode->aux);

		qg->reserved -= num_bytes;

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					(uintptr_t)glist->group, GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}

out:
	spin_unlock(&fs_info->qgroup_lock);
}

static inline void qgroup_free(struct btrfs_root *root, u64 num_bytes)
{
	return btrfs_qgroup_free_refroot(root->fs_info, root->objectid,
					 num_bytes);
}
void assert_qgroups_uptodate(struct btrfs_trans_handle *trans)
{
	if (list_empty(&trans->qgroup_ref_list) && !trans->delayed_ref_elem.seq)
		return;
	btrfs_err(trans->fs_info,
		"qgroups not uptodate in trans handle %p:  list is%s empty, seq is %#x.%x",
		trans, list_empty(&trans->qgroup_ref_list) ? "" : " not",
		(u32)(trans->delayed_ref_elem.seq >> 32),
		(u32)trans->delayed_ref_elem.seq);
	BUG();
}

/*
 * returns < 0 on error, 0 when more leafs are to be scanned.
 * returns 1 when done.
 */
static int
qgroup_rescan_leaf(struct btrfs_fs_info *fs_info, struct btrfs_path *path,
		   struct btrfs_trans_handle *trans)
{
	struct btrfs_key found;
	struct extent_buffer *scratch_leaf = NULL;
	struct ulist *roots = NULL;
	struct seq_list tree_mod_seq_elem = SEQ_LIST_INIT(tree_mod_seq_elem);
	u64 num_bytes;
	int slot;
	int ret;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	ret = btrfs_search_slot_for_read(fs_info->extent_root,
					 &fs_info->qgroup_rescan_progress,
					 path, 1, 0);

	btrfs_debug(fs_info,
		"current progress key (%llu %u %llu), search_slot ret %d",
		fs_info->qgroup_rescan_progress.objectid,
		fs_info->qgroup_rescan_progress.type,
		fs_info->qgroup_rescan_progress.offset, ret);

	if (ret) {
		/*
		 * The rescan is about to end, we will not be scanning any
		 * further blocks. We cannot unset the RESCAN flag here, because
		 * we want to commit the transaction if everything went well.
		 * To make the live accounting work in this phase, we set our
		 * scan progress pointer such that every real extent objectid
		 * will be smaller.
		 */
		fs_info->qgroup_rescan_progress.objectid = (u64)-1;
		btrfs_release_path(path);
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		return ret;
	}

	btrfs_item_key_to_cpu(path->nodes[0], &found,
			      btrfs_header_nritems(path->nodes[0]) - 1);
	fs_info->qgroup_rescan_progress.objectid = found.objectid + 1;

	btrfs_get_tree_mod_seq(fs_info, &tree_mod_seq_elem);
	scratch_leaf = btrfs_clone_extent_buffer(path->nodes[0]);
	if (!scratch_leaf) {
		ret = -ENOMEM;
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		goto out;
	}
	extent_buffer_get(scratch_leaf);
	btrfs_tree_read_lock(scratch_leaf);
	btrfs_set_lock_blocking_rw(scratch_leaf, BTRFS_READ_LOCK);
	slot = path->slots[0];
	btrfs_release_path(path);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	for (; slot < btrfs_header_nritems(scratch_leaf); ++slot) {
		btrfs_item_key_to_cpu(scratch_leaf, &found, slot);
		if (found.type != BTRFS_EXTENT_ITEM_KEY &&
		    found.type != BTRFS_METADATA_ITEM_KEY)
			continue;
		if (found.type == BTRFS_METADATA_ITEM_KEY)
			num_bytes = fs_info->extent_root->nodesize;
		else
			num_bytes = found.offset;

		ret = btrfs_find_all_roots(NULL, fs_info, found.objectid, 0,
					   &roots);
		if (ret < 0)
			goto out;
		/* For rescan, just pass old_roots as NULL */
		ret = btrfs_qgroup_account_extent(trans, fs_info,
				found.objectid, num_bytes, NULL, roots);
		if (ret < 0)
			goto out;
	}
out:
	if (scratch_leaf) {
		btrfs_tree_read_unlock_blocking(scratch_leaf);
		free_extent_buffer(scratch_leaf);
	}
	btrfs_put_tree_mod_seq(fs_info, &tree_mod_seq_elem);

	return ret;
}

static void btrfs_qgroup_rescan_worker(struct btrfs_work *work)
{
	struct btrfs_fs_info *fs_info = container_of(work, struct btrfs_fs_info,
						     qgroup_rescan_work);
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans = NULL;
	int err = -ENOMEM;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	fs_info->qgroup_rescan_running = true;
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	path = btrfs_alloc_path();
	if (!path)
		goto out;

	err = 0;
	while (!err && !btrfs_fs_closing(fs_info)) {
		trans = btrfs_start_transaction(fs_info->fs_root, 0);
		if (IS_ERR(trans)) {
			err = PTR_ERR(trans);
			break;
		}
		if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags)) {
			err = -EINTR;
		} else {
			err = qgroup_rescan_leaf(fs_info, path, trans);
		}
		if (err > 0)
			btrfs_commit_transaction(trans, fs_info->fs_root);
		else
			btrfs_end_transaction(trans, fs_info->fs_root);
	}

out:
	btrfs_free_path(path);

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (!btrfs_fs_closing(fs_info))
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;

	if (err > 0 &&
	    fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	} else if (err < 0) {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	/*
	 * only update status, since the previous part has already updated the
	 * qgroup info.
	 */
	trans = btrfs_start_transaction(fs_info->quota_root, 1);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		btrfs_err(fs_info,
			  "fail to start transaction for status update: %d\n",
			  err);
		goto done;
	}
	ret = update_qgroup_status_item(trans, fs_info, fs_info->quota_root);
	if (ret < 0) {
		err = ret;
		btrfs_err(fs_info, "fail to update qgroup status: %d", err);
	}
	btrfs_end_transaction(trans, fs_info->quota_root);

	if (btrfs_fs_closing(fs_info)) {
		btrfs_info(fs_info, "qgroup scan paused");
	} else if (err >= 0) {
		btrfs_info(fs_info, "qgroup scan completed%s",
			err > 0 ? " (inconsistency flag cleared)" : "");
	} else {
		btrfs_err(fs_info, "qgroup scan failed with %d", err);
	}

done:
	mutex_lock(&fs_info->qgroup_rescan_lock);
	fs_info->qgroup_rescan_running = false;
	mutex_unlock(&fs_info->qgroup_rescan_lock);
	complete_all(&fs_info->qgroup_rescan_completion);
}

/*
 * Checks that (a) no rescan is running and (b) quota is enabled. Allocates all
 * memory required for the rescan context.
 */
static int
qgroup_rescan_init(struct btrfs_fs_info *fs_info, u64 progress_objectid,
		   int init_flags)
{
	int ret = 0;

	if (!init_flags &&
	    (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) ||
	     !(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON))) {
		ret = -EINVAL;
		goto err;
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);
	spin_lock(&fs_info->qgroup_lock);

	if (init_flags) {
		if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN)
			ret = -EINPROGRESS;
		else if (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON))
			ret = -EINVAL;

		if (ret) {
			spin_unlock(&fs_info->qgroup_lock);
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			goto err;
		}
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	}

	memset(&fs_info->qgroup_rescan_progress, 0,
		sizeof(fs_info->qgroup_rescan_progress));
	fs_info->qgroup_rescan_progress.objectid = progress_objectid;
	init_completion(&fs_info->qgroup_rescan_completion);

	spin_unlock(&fs_info->qgroup_lock);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	memset(&fs_info->qgroup_rescan_work, 0,
	       sizeof(fs_info->qgroup_rescan_work));
	btrfs_init_work(&fs_info->qgroup_rescan_work,
			btrfs_qgroup_rescan_helper,
			btrfs_qgroup_rescan_worker, NULL, NULL);

	if (ret) {
err:
		btrfs_info(fs_info, "qgroup_rescan_init failed with %d", ret);
		return ret;
	}

	return 0;
}

static void
qgroup_rescan_zero_tracking(struct btrfs_fs_info *fs_info)
{
	struct rb_node *n;
	struct btrfs_qgroup *qgroup;

	spin_lock(&fs_info->qgroup_lock);
	/* clear all current qgroup tracking information */
	for (n = rb_first(&fs_info->qgroup_tree); n; n = rb_next(n)) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		qgroup->rfer = 0;
		qgroup->rfer_cmpr = 0;
		qgroup->excl = 0;
		qgroup->excl_cmpr = 0;
	}
	spin_unlock(&fs_info->qgroup_lock);
}

int
btrfs_qgroup_rescan(struct btrfs_fs_info *fs_info)
{
	int ret = 0;
	struct btrfs_trans_handle *trans;

	ret = qgroup_rescan_init(fs_info, 0, 1);
	if (ret)
		return ret;

	/*
	 * We have set the rescan_progress to 0, which means no more
	 * delayed refs will be accounted by btrfs_qgroup_account_ref.
	 * However, btrfs_qgroup_account_ref may be right after its call
	 * to btrfs_find_all_roots, in which case it would still do the
	 * accounting.
	 * To solve this, we're committing the transaction, which will
	 * ensure we run all delayed refs and only after that, we are
	 * going to clear all tracking information for a clean start.
	 */

	trans = btrfs_join_transaction(fs_info->fs_root);
	if (IS_ERR(trans)) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		return PTR_ERR(trans);
	}
	ret = btrfs_commit_transaction(trans, fs_info->fs_root);
	if (ret) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		return ret;
	}

	qgroup_rescan_zero_tracking(fs_info);

	btrfs_queue_work(fs_info->qgroup_rescan_workers,
			 &fs_info->qgroup_rescan_work);

	return 0;
}

int btrfs_qgroup_wait_for_completion(struct btrfs_fs_info *fs_info,
				     bool interruptible)
{
	int running;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	spin_lock(&fs_info->qgroup_lock);
	running = fs_info->qgroup_rescan_running;
	spin_unlock(&fs_info->qgroup_lock);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	if (!running)
		return 0;

	if (interruptible)
		ret = wait_for_completion_interruptible(
					&fs_info->qgroup_rescan_completion);
	else
		wait_for_completion(&fs_info->qgroup_rescan_completion);

	return ret;
}

/*
 * this is only called from open_ctree where we're still single threaded, thus
 * locking is omitted here.
 */
void
btrfs_qgroup_rescan_resume(struct btrfs_fs_info *fs_info)
{
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN)
		btrfs_queue_work(fs_info->qgroup_rescan_workers,
				 &fs_info->qgroup_rescan_work);
}

/*
 * Reserve qgroup space for range [start, start + len).
 *
 * This function will either reserve space from related qgroups or doing
 * nothing if the range is already reserved.
 *
 * Return 0 for successful reserve
 * Return <0 for error (including -EQUOT)
 *
 * NOTE: this function may sleep for memory allocation.
 */
int btrfs_qgroup_reserve_data(struct inode *inode, u64 start, u64 len)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_changeset changeset;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	int ret;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &root->fs_info->flags) ||
	    !is_fstree(root->objectid) || len == 0)
		return 0;

	changeset.bytes_changed = 0;
	changeset.range_changed = ulist_alloc(GFP_NOFS);
	ret = set_record_extent_bits(&BTRFS_I(inode)->io_tree, start,
			start + len -1, EXTENT_QGROUP_RESERVED, &changeset);
	trace_btrfs_qgroup_reserve_data(inode, start, len,
					changeset.bytes_changed,
					QGROUP_RESERVE);
	if (ret < 0)
		goto cleanup;
	ret = qgroup_reserve(root, changeset.bytes_changed);
	if (ret < 0)
		goto cleanup;

	ulist_free(changeset.range_changed);
	return ret;

cleanup:
	/* cleanup already reserved ranges */
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(changeset.range_changed, &uiter)))
		clear_extent_bit(&BTRFS_I(inode)->io_tree, unode->val,
				 unode->aux, EXTENT_QGROUP_RESERVED, 0, 0, NULL,
				 GFP_NOFS);
	ulist_free(changeset.range_changed);
	return ret;
}

static int __btrfs_qgroup_release_data(struct inode *inode, u64 start, u64 len,
				       int free)
{
	struct extent_changeset changeset;
	int trace_op = QGROUP_RELEASE;
	int ret;

	changeset.bytes_changed = 0;
	changeset.range_changed = ulist_alloc(GFP_NOFS);
	if (!changeset.range_changed)
		return -ENOMEM;

	ret = clear_record_extent_bits(&BTRFS_I(inode)->io_tree, start, 
			start + len -1, EXTENT_QGROUP_RESERVED, &changeset);
	if (ret < 0)
		goto out;

	if (free) {
		qgroup_free(BTRFS_I(inode)->root, changeset.bytes_changed);
		trace_op = QGROUP_FREE;
	}
	trace_btrfs_qgroup_release_data(inode, start, len,
					changeset.bytes_changed, trace_op);
out:
	ulist_free(changeset.range_changed);
	return ret;
}

/*
 * Free a reserved space range from io_tree and related qgroups
 *
 * Should be called when a range of pages get invalidated before reaching disk.
 * Or for error cleanup case.
 *
 * For data written to disk, use btrfs_qgroup_release_data().
 *
 * NOTE: This function may sleep for memory allocation.
 */
int btrfs_qgroup_free_data(struct inode *inode, u64 start, u64 len)
{
	return __btrfs_qgroup_release_data(inode, start, len, 1);
}

/*
 * Release a reserved space range from io_tree only.
 *
 * Should be called when a range of pages get written to disk and corresponding
 * FILE_EXTENT is inserted into corresponding root.
 *
 * Since new qgroup accounting framework will only update qgroup numbers at
 * commit_transaction() time, its reserved space shouldn't be freed from
 * related qgroups.
 *
 * But we should release the range from io_tree, to allow further write to be
 * COWed.
 *
 * NOTE: This function may sleep for memory allocation.
 */
int btrfs_qgroup_release_data(struct inode *inode, u64 start, u64 len)
{
	return __btrfs_qgroup_release_data(inode, start, len, 0);
}

int btrfs_qgroup_reserve_meta(struct btrfs_root *root, int num_bytes)
{
	int ret;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &root->fs_info->flags) ||
	    !is_fstree(root->objectid) || num_bytes == 0)
		return 0;

	BUG_ON(num_bytes != round_down(num_bytes, root->nodesize));
	ret = qgroup_reserve(root, num_bytes);
	if (ret < 0)
		return ret;
	atomic_add(num_bytes, &root->qgroup_meta_rsv);
	return ret;
}

void btrfs_qgroup_free_meta_all(struct btrfs_root *root)
{
	int reserved;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &root->fs_info->flags) ||
	    !is_fstree(root->objectid))
		return;

	reserved = atomic_xchg(&root->qgroup_meta_rsv, 0);
	if (reserved == 0)
		return;
	qgroup_free(root, reserved);
}

void btrfs_qgroup_free_meta(struct btrfs_root *root, int num_bytes)
{
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &root->fs_info->flags) ||
	    !is_fstree(root->objectid))
		return;

	BUG_ON(num_bytes != round_down(num_bytes, root->nodesize));
	WARN_ON(atomic_read(&root->qgroup_meta_rsv) < num_bytes);
	atomic_sub(num_bytes, &root->qgroup_meta_rsv);
	qgroup_free(root, num_bytes);
}

/*
 * Check qgroup reserved space leaking, normally at destroy inode
 * time
 */
void btrfs_qgroup_check_reserved_leak(struct inode *inode)
{
	struct extent_changeset changeset;
	struct ulist_node *unode;
	struct ulist_iterator iter;
	int ret;

	changeset.bytes_changed = 0;
	changeset.range_changed = ulist_alloc(GFP_NOFS);
	if (WARN_ON(!changeset.range_changed))
		return;

	ret = clear_record_extent_bits(&BTRFS_I(inode)->io_tree, 0, (u64)-1,
			EXTENT_QGROUP_RESERVED, &changeset);

	WARN_ON(ret < 0);
	if (WARN_ON(changeset.bytes_changed)) {
		ULIST_ITER_INIT(&iter);
		while ((unode = ulist_next(changeset.range_changed, &iter))) {
			btrfs_warn(BTRFS_I(inode)->root->fs_info,
				"leaking qgroup reserved space, ino: %lu, start: %llu, end: %llu",
				inode->i_ino, unode->val, unode->aux);
		}
		qgroup_free(BTRFS_I(inode)->root, changeset.bytes_changed);
	}
	ulist_free(changeset.range_changed);
}

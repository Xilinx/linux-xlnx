/*
 * fs/f2fs/node.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/mpage.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/pagevec.h>
#include <linux/swap.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include <trace/events/f2fs.h>

static struct kmem_cache *nat_entry_slab;
static struct kmem_cache *free_nid_slab;

static void clear_node_page_dirty(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct f2fs_sb_info *sbi = F2FS_SB(mapping->host->i_sb);
	unsigned int long flags;

	if (PageDirty(page)) {
		spin_lock_irqsave(&mapping->tree_lock, flags);
		radix_tree_tag_clear(&mapping->page_tree,
				page_index(page),
				PAGECACHE_TAG_DIRTY);
		spin_unlock_irqrestore(&mapping->tree_lock, flags);

		clear_page_dirty_for_io(page);
		dec_page_count(sbi, F2FS_DIRTY_NODES);
	}
	ClearPageUptodate(page);
}

static struct page *get_current_nat_page(struct f2fs_sb_info *sbi, nid_t nid)
{
	pgoff_t index = current_nat_addr(sbi, nid);
	return get_meta_page(sbi, index);
}

static struct page *get_next_nat_page(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct page *src_page;
	struct page *dst_page;
	pgoff_t src_off;
	pgoff_t dst_off;
	void *src_addr;
	void *dst_addr;
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	src_off = current_nat_addr(sbi, nid);
	dst_off = next_nat_addr(sbi, src_off);

	/* get current nat block page with lock */
	src_page = get_meta_page(sbi, src_off);

	/* Dirty src_page means that it is already the new target NAT page. */
	if (PageDirty(src_page))
		return src_page;

	dst_page = grab_meta_page(sbi, dst_off);

	src_addr = page_address(src_page);
	dst_addr = page_address(dst_page);
	memcpy(dst_addr, src_addr, PAGE_CACHE_SIZE);
	set_page_dirty(dst_page);
	f2fs_put_page(src_page, 1);

	set_to_next_nat(nm_i, nid);

	return dst_page;
}

/*
 * Readahead NAT pages
 */
static void ra_nat_pages(struct f2fs_sb_info *sbi, int nid)
{
	struct address_space *mapping = sbi->meta_inode->i_mapping;
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct blk_plug plug;
	struct page *page;
	pgoff_t index;
	int i;

	blk_start_plug(&plug);

	for (i = 0; i < FREE_NID_PAGES; i++, nid += NAT_ENTRY_PER_BLOCK) {
		if (nid >= nm_i->max_nid)
			nid = 0;
		index = current_nat_addr(sbi, nid);

		page = grab_cache_page(mapping, index);
		if (!page)
			continue;
		if (PageUptodate(page)) {
			f2fs_put_page(page, 1);
			continue;
		}
		if (f2fs_readpage(sbi, page, index, READ))
			continue;

		f2fs_put_page(page, 0);
	}
	blk_finish_plug(&plug);
}

static struct nat_entry *__lookup_nat_cache(struct f2fs_nm_info *nm_i, nid_t n)
{
	return radix_tree_lookup(&nm_i->nat_root, n);
}

static unsigned int __gang_lookup_nat_cache(struct f2fs_nm_info *nm_i,
		nid_t start, unsigned int nr, struct nat_entry **ep)
{
	return radix_tree_gang_lookup(&nm_i->nat_root, (void **)ep, start, nr);
}

static void __del_from_nat_cache(struct f2fs_nm_info *nm_i, struct nat_entry *e)
{
	list_del(&e->list);
	radix_tree_delete(&nm_i->nat_root, nat_get_nid(e));
	nm_i->nat_cnt--;
	kmem_cache_free(nat_entry_slab, e);
}

int is_checkpointed_node(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
	int is_cp = 1;

	read_lock(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, nid);
	if (e && !e->checkpointed)
		is_cp = 0;
	read_unlock(&nm_i->nat_tree_lock);
	return is_cp;
}

static struct nat_entry *grab_nat_entry(struct f2fs_nm_info *nm_i, nid_t nid)
{
	struct nat_entry *new;

	new = kmem_cache_alloc(nat_entry_slab, GFP_ATOMIC);
	if (!new)
		return NULL;
	if (radix_tree_insert(&nm_i->nat_root, nid, new)) {
		kmem_cache_free(nat_entry_slab, new);
		return NULL;
	}
	memset(new, 0, sizeof(struct nat_entry));
	nat_set_nid(new, nid);
	list_add_tail(&new->list, &nm_i->nat_entries);
	nm_i->nat_cnt++;
	return new;
}

static void cache_nat_entry(struct f2fs_nm_info *nm_i, nid_t nid,
						struct f2fs_nat_entry *ne)
{
	struct nat_entry *e;
retry:
	write_lock(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, nid);
	if (!e) {
		e = grab_nat_entry(nm_i, nid);
		if (!e) {
			write_unlock(&nm_i->nat_tree_lock);
			goto retry;
		}
		nat_set_blkaddr(e, le32_to_cpu(ne->block_addr));
		nat_set_ino(e, le32_to_cpu(ne->ino));
		nat_set_version(e, ne->version);
		e->checkpointed = true;
	}
	write_unlock(&nm_i->nat_tree_lock);
}

static void set_node_addr(struct f2fs_sb_info *sbi, struct node_info *ni,
			block_t new_blkaddr)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
retry:
	write_lock(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, ni->nid);
	if (!e) {
		e = grab_nat_entry(nm_i, ni->nid);
		if (!e) {
			write_unlock(&nm_i->nat_tree_lock);
			goto retry;
		}
		e->ni = *ni;
		e->checkpointed = true;
		f2fs_bug_on(ni->blk_addr == NEW_ADDR);
	} else if (new_blkaddr == NEW_ADDR) {
		/*
		 * when nid is reallocated,
		 * previous nat entry can be remained in nat cache.
		 * So, reinitialize it with new information.
		 */
		e->ni = *ni;
		f2fs_bug_on(ni->blk_addr != NULL_ADDR);
	}

	if (new_blkaddr == NEW_ADDR)
		e->checkpointed = false;

	/* sanity check */
	f2fs_bug_on(nat_get_blkaddr(e) != ni->blk_addr);
	f2fs_bug_on(nat_get_blkaddr(e) == NULL_ADDR &&
			new_blkaddr == NULL_ADDR);
	f2fs_bug_on(nat_get_blkaddr(e) == NEW_ADDR &&
			new_blkaddr == NEW_ADDR);
	f2fs_bug_on(nat_get_blkaddr(e) != NEW_ADDR &&
			nat_get_blkaddr(e) != NULL_ADDR &&
			new_blkaddr == NEW_ADDR);

	/* increament version no as node is removed */
	if (nat_get_blkaddr(e) != NEW_ADDR && new_blkaddr == NULL_ADDR) {
		unsigned char version = nat_get_version(e);
		nat_set_version(e, inc_node_version(version));
	}

	/* change address */
	nat_set_blkaddr(e, new_blkaddr);
	__set_nat_cache_dirty(nm_i, e);
	write_unlock(&nm_i->nat_tree_lock);
}

int try_to_free_nats(struct f2fs_sb_info *sbi, int nr_shrink)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	if (nm_i->nat_cnt <= NM_WOUT_THRESHOLD)
		return 0;

	write_lock(&nm_i->nat_tree_lock);
	while (nr_shrink && !list_empty(&nm_i->nat_entries)) {
		struct nat_entry *ne;
		ne = list_first_entry(&nm_i->nat_entries,
					struct nat_entry, list);
		__del_from_nat_cache(nm_i, ne);
		nr_shrink--;
	}
	write_unlock(&nm_i->nat_tree_lock);
	return nr_shrink;
}

/*
 * This function returns always success
 */
void get_node_info(struct f2fs_sb_info *sbi, nid_t nid, struct node_info *ni)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_summary_block *sum = curseg->sum_blk;
	nid_t start_nid = START_NID(nid);
	struct f2fs_nat_block *nat_blk;
	struct page *page = NULL;
	struct f2fs_nat_entry ne;
	struct nat_entry *e;
	int i;

	memset(&ne, 0, sizeof(struct f2fs_nat_entry));
	ni->nid = nid;

	/* Check nat cache */
	read_lock(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, nid);
	if (e) {
		ni->ino = nat_get_ino(e);
		ni->blk_addr = nat_get_blkaddr(e);
		ni->version = nat_get_version(e);
	}
	read_unlock(&nm_i->nat_tree_lock);
	if (e)
		return;

	/* Check current segment summary */
	mutex_lock(&curseg->curseg_mutex);
	i = lookup_journal_in_cursum(sum, NAT_JOURNAL, nid, 0);
	if (i >= 0) {
		ne = nat_in_journal(sum, i);
		node_info_from_raw_nat(ni, &ne);
	}
	mutex_unlock(&curseg->curseg_mutex);
	if (i >= 0)
		goto cache;

	/* Fill node_info from nat page */
	page = get_current_nat_page(sbi, start_nid);
	nat_blk = (struct f2fs_nat_block *)page_address(page);
	ne = nat_blk->entries[nid - start_nid];
	node_info_from_raw_nat(ni, &ne);
	f2fs_put_page(page, 1);
cache:
	/* cache nat entry */
	cache_nat_entry(NM_I(sbi), nid, &ne);
}

/*
 * The maximum depth is four.
 * Offset[0] will have raw inode offset.
 */
static int get_node_path(struct f2fs_inode_info *fi, long block,
				int offset[4], unsigned int noffset[4])
{
	const long direct_index = ADDRS_PER_INODE(fi);
	const long direct_blks = ADDRS_PER_BLOCK;
	const long dptrs_per_blk = NIDS_PER_BLOCK;
	const long indirect_blks = ADDRS_PER_BLOCK * NIDS_PER_BLOCK;
	const long dindirect_blks = indirect_blks * NIDS_PER_BLOCK;
	int n = 0;
	int level = 0;

	noffset[0] = 0;

	if (block < direct_index) {
		offset[n] = block;
		goto got;
	}
	block -= direct_index;
	if (block < direct_blks) {
		offset[n++] = NODE_DIR1_BLOCK;
		noffset[n] = 1;
		offset[n] = block;
		level = 1;
		goto got;
	}
	block -= direct_blks;
	if (block < direct_blks) {
		offset[n++] = NODE_DIR2_BLOCK;
		noffset[n] = 2;
		offset[n] = block;
		level = 1;
		goto got;
	}
	block -= direct_blks;
	if (block < indirect_blks) {
		offset[n++] = NODE_IND1_BLOCK;
		noffset[n] = 3;
		offset[n++] = block / direct_blks;
		noffset[n] = 4 + offset[n - 1];
		offset[n] = block % direct_blks;
		level = 2;
		goto got;
	}
	block -= indirect_blks;
	if (block < indirect_blks) {
		offset[n++] = NODE_IND2_BLOCK;
		noffset[n] = 4 + dptrs_per_blk;
		offset[n++] = block / direct_blks;
		noffset[n] = 5 + dptrs_per_blk + offset[n - 1];
		offset[n] = block % direct_blks;
		level = 2;
		goto got;
	}
	block -= indirect_blks;
	if (block < dindirect_blks) {
		offset[n++] = NODE_DIND_BLOCK;
		noffset[n] = 5 + (dptrs_per_blk * 2);
		offset[n++] = block / indirect_blks;
		noffset[n] = 6 + (dptrs_per_blk * 2) +
			      offset[n - 1] * (dptrs_per_blk + 1);
		offset[n++] = (block / direct_blks) % dptrs_per_blk;
		noffset[n] = 7 + (dptrs_per_blk * 2) +
			      offset[n - 2] * (dptrs_per_blk + 1) +
			      offset[n - 1];
		offset[n] = block % direct_blks;
		level = 3;
		goto got;
	} else {
		BUG();
	}
got:
	return level;
}

/*
 * Caller should call f2fs_put_dnode(dn).
 * Also, it should grab and release a mutex by calling mutex_lock_op() and
 * mutex_unlock_op() only if ro is not set RDONLY_NODE.
 * In the case of RDONLY_NODE, we don't need to care about mutex.
 */
int get_dnode_of_data(struct dnode_of_data *dn, pgoff_t index, int mode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);
	struct page *npage[4];
	struct page *parent;
	int offset[4];
	unsigned int noffset[4];
	nid_t nids[4];
	int level, i;
	int err = 0;

	level = get_node_path(F2FS_I(dn->inode), index, offset, noffset);

	nids[0] = dn->inode->i_ino;
	npage[0] = dn->inode_page;

	if (!npage[0]) {
		npage[0] = get_node_page(sbi, nids[0]);
		if (IS_ERR(npage[0]))
			return PTR_ERR(npage[0]);
	}
	parent = npage[0];
	if (level != 0)
		nids[1] = get_nid(parent, offset[0], true);
	dn->inode_page = npage[0];
	dn->inode_page_locked = true;

	/* get indirect or direct nodes */
	for (i = 1; i <= level; i++) {
		bool done = false;

		if (!nids[i] && mode == ALLOC_NODE) {
			/* alloc new node */
			if (!alloc_nid(sbi, &(nids[i]))) {
				err = -ENOSPC;
				goto release_pages;
			}

			dn->nid = nids[i];
			npage[i] = new_node_page(dn, noffset[i], NULL);
			if (IS_ERR(npage[i])) {
				alloc_nid_failed(sbi, nids[i]);
				err = PTR_ERR(npage[i]);
				goto release_pages;
			}

			set_nid(parent, offset[i - 1], nids[i], i == 1);
			alloc_nid_done(sbi, nids[i]);
			done = true;
		} else if (mode == LOOKUP_NODE_RA && i == level && level > 1) {
			npage[i] = get_node_page_ra(parent, offset[i - 1]);
			if (IS_ERR(npage[i])) {
				err = PTR_ERR(npage[i]);
				goto release_pages;
			}
			done = true;
		}
		if (i == 1) {
			dn->inode_page_locked = false;
			unlock_page(parent);
		} else {
			f2fs_put_page(parent, 1);
		}

		if (!done) {
			npage[i] = get_node_page(sbi, nids[i]);
			if (IS_ERR(npage[i])) {
				err = PTR_ERR(npage[i]);
				f2fs_put_page(npage[0], 0);
				goto release_out;
			}
		}
		if (i < level) {
			parent = npage[i];
			nids[i + 1] = get_nid(parent, offset[i], false);
		}
	}
	dn->nid = nids[level];
	dn->ofs_in_node = offset[level];
	dn->node_page = npage[level];
	dn->data_blkaddr = datablock_addr(dn->node_page, dn->ofs_in_node);
	return 0;

release_pages:
	f2fs_put_page(parent, 1);
	if (i > 1)
		f2fs_put_page(npage[0], 0);
release_out:
	dn->inode_page = NULL;
	dn->node_page = NULL;
	return err;
}

static void truncate_node(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);
	struct node_info ni;

	get_node_info(sbi, dn->nid, &ni);
	if (dn->inode->i_blocks == 0) {
		f2fs_bug_on(ni.blk_addr != NULL_ADDR);
		goto invalidate;
	}
	f2fs_bug_on(ni.blk_addr == NULL_ADDR);

	/* Deallocate node address */
	invalidate_blocks(sbi, ni.blk_addr);
	dec_valid_node_count(sbi, dn->inode, 1);
	set_node_addr(sbi, &ni, NULL_ADDR);

	if (dn->nid == dn->inode->i_ino) {
		remove_orphan_inode(sbi, dn->nid);
		dec_valid_inode_count(sbi);
	} else {
		sync_inode_page(dn);
	}
invalidate:
	clear_node_page_dirty(dn->node_page);
	F2FS_SET_SB_DIRT(sbi);

	f2fs_put_page(dn->node_page, 1);
	dn->node_page = NULL;
	trace_f2fs_truncate_node(dn->inode, dn->nid, ni.blk_addr);
}

static int truncate_dnode(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);
	struct page *page;

	if (dn->nid == 0)
		return 1;

	/* get direct node */
	page = get_node_page(sbi, dn->nid);
	if (IS_ERR(page) && PTR_ERR(page) == -ENOENT)
		return 1;
	else if (IS_ERR(page))
		return PTR_ERR(page);

	/* Make dnode_of_data for parameter */
	dn->node_page = page;
	dn->ofs_in_node = 0;
	truncate_data_blocks(dn);
	truncate_node(dn);
	return 1;
}

static int truncate_nodes(struct dnode_of_data *dn, unsigned int nofs,
						int ofs, int depth)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);
	struct dnode_of_data rdn = *dn;
	struct page *page;
	struct f2fs_node *rn;
	nid_t child_nid;
	unsigned int child_nofs;
	int freed = 0;
	int i, ret;

	if (dn->nid == 0)
		return NIDS_PER_BLOCK + 1;

	trace_f2fs_truncate_nodes_enter(dn->inode, dn->nid, dn->data_blkaddr);

	page = get_node_page(sbi, dn->nid);
	if (IS_ERR(page)) {
		trace_f2fs_truncate_nodes_exit(dn->inode, PTR_ERR(page));
		return PTR_ERR(page);
	}

	rn = F2FS_NODE(page);
	if (depth < 3) {
		for (i = ofs; i < NIDS_PER_BLOCK; i++, freed++) {
			child_nid = le32_to_cpu(rn->in.nid[i]);
			if (child_nid == 0)
				continue;
			rdn.nid = child_nid;
			ret = truncate_dnode(&rdn);
			if (ret < 0)
				goto out_err;
			set_nid(page, i, 0, false);
		}
	} else {
		child_nofs = nofs + ofs * (NIDS_PER_BLOCK + 1) + 1;
		for (i = ofs; i < NIDS_PER_BLOCK; i++) {
			child_nid = le32_to_cpu(rn->in.nid[i]);
			if (child_nid == 0) {
				child_nofs += NIDS_PER_BLOCK + 1;
				continue;
			}
			rdn.nid = child_nid;
			ret = truncate_nodes(&rdn, child_nofs, 0, depth - 1);
			if (ret == (NIDS_PER_BLOCK + 1)) {
				set_nid(page, i, 0, false);
				child_nofs += ret;
			} else if (ret < 0 && ret != -ENOENT) {
				goto out_err;
			}
		}
		freed = child_nofs;
	}

	if (!ofs) {
		/* remove current indirect node */
		dn->node_page = page;
		truncate_node(dn);
		freed++;
	} else {
		f2fs_put_page(page, 1);
	}
	trace_f2fs_truncate_nodes_exit(dn->inode, freed);
	return freed;

out_err:
	f2fs_put_page(page, 1);
	trace_f2fs_truncate_nodes_exit(dn->inode, ret);
	return ret;
}

static int truncate_partial_nodes(struct dnode_of_data *dn,
			struct f2fs_inode *ri, int *offset, int depth)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);
	struct page *pages[2];
	nid_t nid[3];
	nid_t child_nid;
	int err = 0;
	int i;
	int idx = depth - 2;

	nid[0] = le32_to_cpu(ri->i_nid[offset[0] - NODE_DIR1_BLOCK]);
	if (!nid[0])
		return 0;

	/* get indirect nodes in the path */
	for (i = 0; i < depth - 1; i++) {
		/* refernece count'll be increased */
		pages[i] = get_node_page(sbi, nid[i]);
		if (IS_ERR(pages[i])) {
			depth = i + 1;
			err = PTR_ERR(pages[i]);
			goto fail;
		}
		nid[i + 1] = get_nid(pages[i], offset[i + 1], false);
	}

	/* free direct nodes linked to a partial indirect node */
	for (i = offset[depth - 1]; i < NIDS_PER_BLOCK; i++) {
		child_nid = get_nid(pages[idx], i, false);
		if (!child_nid)
			continue;
		dn->nid = child_nid;
		err = truncate_dnode(dn);
		if (err < 0)
			goto fail;
		set_nid(pages[idx], i, 0, false);
	}

	if (offset[depth - 1] == 0) {
		dn->node_page = pages[idx];
		dn->nid = nid[idx];
		truncate_node(dn);
	} else {
		f2fs_put_page(pages[idx], 1);
	}
	offset[idx]++;
	offset[depth - 1] = 0;
fail:
	for (i = depth - 3; i >= 0; i--)
		f2fs_put_page(pages[i], 1);

	trace_f2fs_truncate_partial_nodes(dn->inode, nid, depth, err);

	return err;
}

/*
 * All the block addresses of data and nodes should be nullified.
 */
int truncate_inode_blocks(struct inode *inode, pgoff_t from)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct address_space *node_mapping = sbi->node_inode->i_mapping;
	int err = 0, cont = 1;
	int level, offset[4], noffset[4];
	unsigned int nofs = 0;
	struct f2fs_node *rn;
	struct dnode_of_data dn;
	struct page *page;

	trace_f2fs_truncate_inode_blocks_enter(inode, from);

	level = get_node_path(F2FS_I(inode), from, offset, noffset);
restart:
	page = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(page)) {
		trace_f2fs_truncate_inode_blocks_exit(inode, PTR_ERR(page));
		return PTR_ERR(page);
	}

	set_new_dnode(&dn, inode, page, NULL, 0);
	unlock_page(page);

	rn = F2FS_NODE(page);
	switch (level) {
	case 0:
	case 1:
		nofs = noffset[1];
		break;
	case 2:
		nofs = noffset[1];
		if (!offset[level - 1])
			goto skip_partial;
		err = truncate_partial_nodes(&dn, &rn->i, offset, level);
		if (err < 0 && err != -ENOENT)
			goto fail;
		nofs += 1 + NIDS_PER_BLOCK;
		break;
	case 3:
		nofs = 5 + 2 * NIDS_PER_BLOCK;
		if (!offset[level - 1])
			goto skip_partial;
		err = truncate_partial_nodes(&dn, &rn->i, offset, level);
		if (err < 0 && err != -ENOENT)
			goto fail;
		break;
	default:
		BUG();
	}

skip_partial:
	while (cont) {
		dn.nid = le32_to_cpu(rn->i.i_nid[offset[0] - NODE_DIR1_BLOCK]);
		switch (offset[0]) {
		case NODE_DIR1_BLOCK:
		case NODE_DIR2_BLOCK:
			err = truncate_dnode(&dn);
			break;

		case NODE_IND1_BLOCK:
		case NODE_IND2_BLOCK:
			err = truncate_nodes(&dn, nofs, offset[1], 2);
			break;

		case NODE_DIND_BLOCK:
			err = truncate_nodes(&dn, nofs, offset[1], 3);
			cont = 0;
			break;

		default:
			BUG();
		}
		if (err < 0 && err != -ENOENT)
			goto fail;
		if (offset[1] == 0 &&
				rn->i.i_nid[offset[0] - NODE_DIR1_BLOCK]) {
			lock_page(page);
			if (page->mapping != node_mapping) {
				f2fs_put_page(page, 1);
				goto restart;
			}
			wait_on_page_writeback(page);
			rn->i.i_nid[offset[0] - NODE_DIR1_BLOCK] = 0;
			set_page_dirty(page);
			unlock_page(page);
		}
		offset[1] = 0;
		offset[0]++;
		nofs += err;
	}
fail:
	f2fs_put_page(page, 0);
	trace_f2fs_truncate_inode_blocks_exit(inode, err);
	return err > 0 ? 0 : err;
}

int truncate_xattr_node(struct inode *inode, struct page *page)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	nid_t nid = F2FS_I(inode)->i_xattr_nid;
	struct dnode_of_data dn;
	struct page *npage;

	if (!nid)
		return 0;

	npage = get_node_page(sbi, nid);
	if (IS_ERR(npage))
		return PTR_ERR(npage);

	F2FS_I(inode)->i_xattr_nid = 0;

	/* need to do checkpoint during fsync */
	F2FS_I(inode)->xattr_ver = cur_cp_version(F2FS_CKPT(sbi));

	set_new_dnode(&dn, inode, page, npage, nid);

	if (page)
		dn.inode_page_locked = 1;
	truncate_node(&dn);
	return 0;
}

/*
 * Caller should grab and release a mutex by calling mutex_lock_op() and
 * mutex_unlock_op().
 */
int remove_inode_page(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct page *page;
	nid_t ino = inode->i_ino;
	struct dnode_of_data dn;
	int err;

	page = get_node_page(sbi, ino);
	if (IS_ERR(page))
		return PTR_ERR(page);

	err = truncate_xattr_node(inode, page);
	if (err) {
		f2fs_put_page(page, 1);
		return err;
	}

	/* 0 is possible, after f2fs_new_inode() is failed */
	f2fs_bug_on(inode->i_blocks != 0 && inode->i_blocks != 1);
	set_new_dnode(&dn, inode, page, page, ino);
	truncate_node(&dn);
	return 0;
}

struct page *new_inode_page(struct inode *inode, const struct qstr *name)
{
	struct dnode_of_data dn;

	/* allocate inode page for new inode */
	set_new_dnode(&dn, inode, NULL, NULL, inode->i_ino);

	/* caller should f2fs_put_page(page, 1); */
	return new_node_page(&dn, 0, NULL);
}

struct page *new_node_page(struct dnode_of_data *dn,
				unsigned int ofs, struct page *ipage)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);
	struct address_space *mapping = sbi->node_inode->i_mapping;
	struct node_info old_ni, new_ni;
	struct page *page;
	int err;

	if (is_inode_flag_set(F2FS_I(dn->inode), FI_NO_ALLOC))
		return ERR_PTR(-EPERM);

	page = grab_cache_page(mapping, dn->nid);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (!inc_valid_node_count(sbi, dn->inode, 1)) {
		err = -ENOSPC;
		goto fail;
	}

	get_node_info(sbi, dn->nid, &old_ni);

	/* Reinitialize old_ni with new node page */
	f2fs_bug_on(old_ni.blk_addr != NULL_ADDR);
	new_ni = old_ni;
	new_ni.ino = dn->inode->i_ino;
	set_node_addr(sbi, &new_ni, NEW_ADDR);

	fill_node_footer(page, dn->nid, dn->inode->i_ino, ofs, true);
	set_cold_node(dn->inode, page);
	SetPageUptodate(page);
	set_page_dirty(page);

	if (ofs == XATTR_NODE_OFFSET)
		F2FS_I(dn->inode)->i_xattr_nid = dn->nid;

	dn->node_page = page;
	if (ipage)
		update_inode(dn->inode, ipage);
	else
		sync_inode_page(dn);
	if (ofs == 0)
		inc_valid_inode_count(sbi);

	return page;

fail:
	clear_node_page_dirty(page);
	f2fs_put_page(page, 1);
	return ERR_PTR(err);
}

/*
 * Caller should do after getting the following values.
 * 0: f2fs_put_page(page, 0)
 * LOCKED_PAGE: f2fs_put_page(page, 1)
 * error: nothing
 */
static int read_node_page(struct page *page, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(page->mapping->host->i_sb);
	struct node_info ni;

	get_node_info(sbi, page->index, &ni);

	if (ni.blk_addr == NULL_ADDR) {
		f2fs_put_page(page, 1);
		return -ENOENT;
	}

	if (PageUptodate(page))
		return LOCKED_PAGE;

	return f2fs_readpage(sbi, page, ni.blk_addr, type);
}

/*
 * Readahead a node page
 */
void ra_node_page(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct address_space *mapping = sbi->node_inode->i_mapping;
	struct page *apage;
	int err;

	apage = find_get_page(mapping, nid);
	if (apage && PageUptodate(apage)) {
		f2fs_put_page(apage, 0);
		return;
	}
	f2fs_put_page(apage, 0);

	apage = grab_cache_page(mapping, nid);
	if (!apage)
		return;

	err = read_node_page(apage, READA);
	if (err == 0)
		f2fs_put_page(apage, 0);
	else if (err == LOCKED_PAGE)
		f2fs_put_page(apage, 1);
}

struct page *get_node_page(struct f2fs_sb_info *sbi, pgoff_t nid)
{
	struct address_space *mapping = sbi->node_inode->i_mapping;
	struct page *page;
	int err;
repeat:
	page = grab_cache_page(mapping, nid);
	if (!page)
		return ERR_PTR(-ENOMEM);

	err = read_node_page(page, READ_SYNC);
	if (err < 0)
		return ERR_PTR(err);
	else if (err == LOCKED_PAGE)
		goto got_it;

	lock_page(page);
	if (!PageUptodate(page)) {
		f2fs_put_page(page, 1);
		return ERR_PTR(-EIO);
	}
	if (page->mapping != mapping) {
		f2fs_put_page(page, 1);
		goto repeat;
	}
got_it:
	f2fs_bug_on(nid != nid_of_node(page));
	mark_page_accessed(page);
	return page;
}

/*
 * Return a locked page for the desired node page.
 * And, readahead MAX_RA_NODE number of node pages.
 */
struct page *get_node_page_ra(struct page *parent, int start)
{
	struct f2fs_sb_info *sbi = F2FS_SB(parent->mapping->host->i_sb);
	struct address_space *mapping = sbi->node_inode->i_mapping;
	struct blk_plug plug;
	struct page *page;
	int err, i, end;
	nid_t nid;

	/* First, try getting the desired direct node. */
	nid = get_nid(parent, start, false);
	if (!nid)
		return ERR_PTR(-ENOENT);
repeat:
	page = grab_cache_page(mapping, nid);
	if (!page)
		return ERR_PTR(-ENOMEM);

	err = read_node_page(page, READ_SYNC);
	if (err < 0)
		return ERR_PTR(err);
	else if (err == LOCKED_PAGE)
		goto page_hit;

	blk_start_plug(&plug);

	/* Then, try readahead for siblings of the desired node */
	end = start + MAX_RA_NODE;
	end = min(end, NIDS_PER_BLOCK);
	for (i = start + 1; i < end; i++) {
		nid = get_nid(parent, i, false);
		if (!nid)
			continue;
		ra_node_page(sbi, nid);
	}

	blk_finish_plug(&plug);

	lock_page(page);
	if (page->mapping != mapping) {
		f2fs_put_page(page, 1);
		goto repeat;
	}
page_hit:
	if (!PageUptodate(page)) {
		f2fs_put_page(page, 1);
		return ERR_PTR(-EIO);
	}
	mark_page_accessed(page);
	return page;
}

void sync_inode_page(struct dnode_of_data *dn)
{
	if (IS_INODE(dn->node_page) || dn->inode_page == dn->node_page) {
		update_inode(dn->inode, dn->node_page);
	} else if (dn->inode_page) {
		if (!dn->inode_page_locked)
			lock_page(dn->inode_page);
		update_inode(dn->inode, dn->inode_page);
		if (!dn->inode_page_locked)
			unlock_page(dn->inode_page);
	} else {
		update_inode_page(dn->inode);
	}
}

int sync_node_pages(struct f2fs_sb_info *sbi, nid_t ino,
					struct writeback_control *wbc)
{
	struct address_space *mapping = sbi->node_inode->i_mapping;
	pgoff_t index, end;
	struct pagevec pvec;
	int step = ino ? 2 : 0;
	int nwritten = 0, wrote = 0;

	pagevec_init(&pvec, 0);

next_step:
	index = 0;
	end = LONG_MAX;

	while (index <= end) {
		int i, nr_pages;
		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
				PAGECACHE_TAG_DIRTY,
				min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
		if (nr_pages == 0)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			/*
			 * flushing sequence with step:
			 * 0. indirect nodes
			 * 1. dentry dnodes
			 * 2. file dnodes
			 */
			if (step == 0 && IS_DNODE(page))
				continue;
			if (step == 1 && (!IS_DNODE(page) ||
						is_cold_node(page)))
				continue;
			if (step == 2 && (!IS_DNODE(page) ||
						!is_cold_node(page)))
				continue;

			/*
			 * If an fsync mode,
			 * we should not skip writing node pages.
			 */
			if (ino && ino_of_node(page) == ino)
				lock_page(page);
			else if (!trylock_page(page))
				continue;

			if (unlikely(page->mapping != mapping)) {
continue_unlock:
				unlock_page(page);
				continue;
			}
			if (ino && ino_of_node(page) != ino)
				goto continue_unlock;

			if (!PageDirty(page)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (!clear_page_dirty_for_io(page))
				goto continue_unlock;

			/* called by fsync() */
			if (ino && IS_DNODE(page)) {
				int mark = !is_checkpointed_node(sbi, ino);
				set_fsync_mark(page, 1);
				if (IS_INODE(page))
					set_dentry_mark(page, mark);
				nwritten++;
			} else {
				set_fsync_mark(page, 0);
				set_dentry_mark(page, 0);
			}
			mapping->a_ops->writepage(page, wbc);
			wrote++;

			if (--wbc->nr_to_write == 0)
				break;
		}
		pagevec_release(&pvec);
		cond_resched();

		if (wbc->nr_to_write == 0) {
			step = 2;
			break;
		}
	}

	if (step < 2) {
		step++;
		goto next_step;
	}

	if (wrote)
		f2fs_submit_bio(sbi, NODE, wbc->sync_mode == WB_SYNC_ALL);

	return nwritten;
}

int wait_on_node_pages_writeback(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct address_space *mapping = sbi->node_inode->i_mapping;
	pgoff_t index = 0, end = LONG_MAX;
	struct pagevec pvec;
	int nr_pages;
	int ret2 = 0, ret = 0;

	pagevec_init(&pvec, 0);
	while ((index <= end) &&
			(nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
			PAGECACHE_TAG_WRITEBACK,
			min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1)) != 0) {
		unsigned i;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			/* until radix tree lookup accepts end_index */
			if (page->index > end)
				continue;

			if (ino && ino_of_node(page) == ino) {
				wait_on_page_writeback(page);
				if (TestClearPageError(page))
					ret = -EIO;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}

	if (test_and_clear_bit(AS_ENOSPC, &mapping->flags))
		ret2 = -ENOSPC;
	if (test_and_clear_bit(AS_EIO, &mapping->flags))
		ret2 = -EIO;
	if (!ret)
		ret = ret2;
	return ret;
}

static int f2fs_write_node_page(struct page *page,
				struct writeback_control *wbc)
{
	struct f2fs_sb_info *sbi = F2FS_SB(page->mapping->host->i_sb);
	nid_t nid;
	block_t new_addr;
	struct node_info ni;

	if (sbi->por_doing)
		goto redirty_out;

	wait_on_page_writeback(page);

	/* get old block addr of this node page */
	nid = nid_of_node(page);
	f2fs_bug_on(page->index != nid);

	get_node_info(sbi, nid, &ni);

	/* This page is already truncated */
	if (ni.blk_addr == NULL_ADDR) {
		dec_page_count(sbi, F2FS_DIRTY_NODES);
		unlock_page(page);
		return 0;
	}

	if (wbc->for_reclaim)
		goto redirty_out;

	mutex_lock(&sbi->node_write);
	set_page_writeback(page);
	write_node_page(sbi, page, nid, ni.blk_addr, &new_addr);
	set_node_addr(sbi, &ni, new_addr);
	dec_page_count(sbi, F2FS_DIRTY_NODES);
	mutex_unlock(&sbi->node_write);
	unlock_page(page);
	return 0;

redirty_out:
	dec_page_count(sbi, F2FS_DIRTY_NODES);
	wbc->pages_skipped++;
	set_page_dirty(page);
	return AOP_WRITEPAGE_ACTIVATE;
}

/*
 * It is very important to gather dirty pages and write at once, so that we can
 * submit a big bio without interfering other data writes.
 * Be default, 512 pages (2MB) * 3 node types, is more reasonable.
 */
#define COLLECT_DIRTY_NODES	1536
static int f2fs_write_node_pages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct f2fs_sb_info *sbi = F2FS_SB(mapping->host->i_sb);
	long nr_to_write = wbc->nr_to_write;

	/* balancing f2fs's metadata in background */
	f2fs_balance_fs_bg(sbi);

	/* collect a number of dirty node pages and write together */
	if (get_pages(sbi, F2FS_DIRTY_NODES) < COLLECT_DIRTY_NODES)
		return 0;

	/* if mounting is failed, skip writing node pages */
	wbc->nr_to_write = 3 * max_hw_blocks(sbi);
	sync_node_pages(sbi, 0, wbc);
	wbc->nr_to_write = nr_to_write - (3 * max_hw_blocks(sbi) -
						wbc->nr_to_write);
	return 0;
}

static int f2fs_set_node_page_dirty(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct f2fs_sb_info *sbi = F2FS_SB(mapping->host->i_sb);

	trace_f2fs_set_page_dirty(page, NODE);

	SetPageUptodate(page);
	if (!PageDirty(page)) {
		__set_page_dirty_nobuffers(page);
		inc_page_count(sbi, F2FS_DIRTY_NODES);
		SetPagePrivate(page);
		return 1;
	}
	return 0;
}

static void f2fs_invalidate_node_page(struct page *page, unsigned int offset,
				      unsigned int length)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	if (PageDirty(page))
		dec_page_count(sbi, F2FS_DIRTY_NODES);
	ClearPagePrivate(page);
}

static int f2fs_release_node_page(struct page *page, gfp_t wait)
{
	ClearPagePrivate(page);
	return 1;
}

/*
 * Structure of the f2fs node operations
 */
const struct address_space_operations f2fs_node_aops = {
	.writepage	= f2fs_write_node_page,
	.writepages	= f2fs_write_node_pages,
	.set_page_dirty	= f2fs_set_node_page_dirty,
	.invalidatepage	= f2fs_invalidate_node_page,
	.releasepage	= f2fs_release_node_page,
};

static struct free_nid *__lookup_free_nid_list(nid_t n, struct list_head *head)
{
	struct list_head *this;
	struct free_nid *i;
	list_for_each(this, head) {
		i = list_entry(this, struct free_nid, list);
		if (i->nid == n)
			return i;
	}
	return NULL;
}

static void __del_from_free_nid_list(struct free_nid *i)
{
	list_del(&i->list);
	kmem_cache_free(free_nid_slab, i);
}

static int add_free_nid(struct f2fs_nm_info *nm_i, nid_t nid, bool build)
{
	struct free_nid *i;
	struct nat_entry *ne;
	bool allocated = false;

	if (nm_i->fcnt > 2 * MAX_FREE_NIDS)
		return -1;

	/* 0 nid should not be used */
	if (nid == 0)
		return 0;

	if (build) {
		/* do not add allocated nids */
		read_lock(&nm_i->nat_tree_lock);
		ne = __lookup_nat_cache(nm_i, nid);
		if (ne && nat_get_blkaddr(ne) != NULL_ADDR)
			allocated = true;
		read_unlock(&nm_i->nat_tree_lock);
		if (allocated)
			return 0;
	}

	i = f2fs_kmem_cache_alloc(free_nid_slab, GFP_NOFS);
	i->nid = nid;
	i->state = NID_NEW;

	spin_lock(&nm_i->free_nid_list_lock);
	if (__lookup_free_nid_list(nid, &nm_i->free_nid_list)) {
		spin_unlock(&nm_i->free_nid_list_lock);
		kmem_cache_free(free_nid_slab, i);
		return 0;
	}
	list_add_tail(&i->list, &nm_i->free_nid_list);
	nm_i->fcnt++;
	spin_unlock(&nm_i->free_nid_list_lock);
	return 1;
}

static void remove_free_nid(struct f2fs_nm_info *nm_i, nid_t nid)
{
	struct free_nid *i;
	spin_lock(&nm_i->free_nid_list_lock);
	i = __lookup_free_nid_list(nid, &nm_i->free_nid_list);
	if (i && i->state == NID_NEW) {
		__del_from_free_nid_list(i);
		nm_i->fcnt--;
	}
	spin_unlock(&nm_i->free_nid_list_lock);
}

static void scan_nat_page(struct f2fs_nm_info *nm_i,
			struct page *nat_page, nid_t start_nid)
{
	struct f2fs_nat_block *nat_blk = page_address(nat_page);
	block_t blk_addr;
	int i;

	i = start_nid % NAT_ENTRY_PER_BLOCK;

	for (; i < NAT_ENTRY_PER_BLOCK; i++, start_nid++) {

		if (start_nid >= nm_i->max_nid)
			break;

		blk_addr = le32_to_cpu(nat_blk->entries[i].block_addr);
		f2fs_bug_on(blk_addr == NEW_ADDR);
		if (blk_addr == NULL_ADDR) {
			if (add_free_nid(nm_i, start_nid, true) < 0)
				break;
		}
	}
}

static void build_free_nids(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_summary_block *sum = curseg->sum_blk;
	int i = 0;
	nid_t nid = nm_i->next_scan_nid;

	/* Enough entries */
	if (nm_i->fcnt > NAT_ENTRY_PER_BLOCK)
		return;

	/* readahead nat pages to be scanned */
	ra_nat_pages(sbi, nid);

	while (1) {
		struct page *page = get_current_nat_page(sbi, nid);

		scan_nat_page(nm_i, page, nid);
		f2fs_put_page(page, 1);

		nid += (NAT_ENTRY_PER_BLOCK - (nid % NAT_ENTRY_PER_BLOCK));
		if (nid >= nm_i->max_nid)
			nid = 0;

		if (i++ == FREE_NID_PAGES)
			break;
	}

	/* go to the next free nat pages to find free nids abundantly */
	nm_i->next_scan_nid = nid;

	/* find free nids from current sum_pages */
	mutex_lock(&curseg->curseg_mutex);
	for (i = 0; i < nats_in_cursum(sum); i++) {
		block_t addr = le32_to_cpu(nat_in_journal(sum, i).block_addr);
		nid = le32_to_cpu(nid_in_journal(sum, i));
		if (addr == NULL_ADDR)
			add_free_nid(nm_i, nid, true);
		else
			remove_free_nid(nm_i, nid);
	}
	mutex_unlock(&curseg->curseg_mutex);
}

/*
 * If this function returns success, caller can obtain a new nid
 * from second parameter of this function.
 * The returned nid could be used ino as well as nid when inode is created.
 */
bool alloc_nid(struct f2fs_sb_info *sbi, nid_t *nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i = NULL;
	struct list_head *this;
retry:
	if (sbi->total_valid_node_count + 1 >= nm_i->max_nid)
		return false;

	spin_lock(&nm_i->free_nid_list_lock);

	/* We should not use stale free nids created by build_free_nids */
	if (nm_i->fcnt && !sbi->on_build_free_nids) {
		f2fs_bug_on(list_empty(&nm_i->free_nid_list));
		list_for_each(this, &nm_i->free_nid_list) {
			i = list_entry(this, struct free_nid, list);
			if (i->state == NID_NEW)
				break;
		}

		f2fs_bug_on(i->state != NID_NEW);
		*nid = i->nid;
		i->state = NID_ALLOC;
		nm_i->fcnt--;
		spin_unlock(&nm_i->free_nid_list_lock);
		return true;
	}
	spin_unlock(&nm_i->free_nid_list_lock);

	/* Let's scan nat pages and its caches to get free nids */
	mutex_lock(&nm_i->build_lock);
	sbi->on_build_free_nids = true;
	build_free_nids(sbi);
	sbi->on_build_free_nids = false;
	mutex_unlock(&nm_i->build_lock);
	goto retry;
}

/*
 * alloc_nid() should be called prior to this function.
 */
void alloc_nid_done(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i;

	spin_lock(&nm_i->free_nid_list_lock);
	i = __lookup_free_nid_list(nid, &nm_i->free_nid_list);
	f2fs_bug_on(!i || i->state != NID_ALLOC);
	__del_from_free_nid_list(i);
	spin_unlock(&nm_i->free_nid_list_lock);
}

/*
 * alloc_nid() should be called prior to this function.
 */
void alloc_nid_failed(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i;

	if (!nid)
		return;

	spin_lock(&nm_i->free_nid_list_lock);
	i = __lookup_free_nid_list(nid, &nm_i->free_nid_list);
	f2fs_bug_on(!i || i->state != NID_ALLOC);
	if (nm_i->fcnt > 2 * MAX_FREE_NIDS) {
		__del_from_free_nid_list(i);
	} else {
		i->state = NID_NEW;
		nm_i->fcnt++;
	}
	spin_unlock(&nm_i->free_nid_list_lock);
}

void recover_node_page(struct f2fs_sb_info *sbi, struct page *page,
		struct f2fs_summary *sum, struct node_info *ni,
		block_t new_blkaddr)
{
	rewrite_node_page(sbi, page, sum, ni->blk_addr, new_blkaddr);
	set_node_addr(sbi, ni, new_blkaddr);
	clear_node_page_dirty(page);
}

int recover_inode_page(struct f2fs_sb_info *sbi, struct page *page)
{
	struct address_space *mapping = sbi->node_inode->i_mapping;
	struct f2fs_node *src, *dst;
	nid_t ino = ino_of_node(page);
	struct node_info old_ni, new_ni;
	struct page *ipage;

	ipage = grab_cache_page(mapping, ino);
	if (!ipage)
		return -ENOMEM;

	/* Should not use this inode  from free nid list */
	remove_free_nid(NM_I(sbi), ino);

	get_node_info(sbi, ino, &old_ni);
	SetPageUptodate(ipage);
	fill_node_footer(ipage, ino, ino, 0, true);

	src = F2FS_NODE(page);
	dst = F2FS_NODE(ipage);

	memcpy(dst, src, (unsigned long)&src->i.i_ext - (unsigned long)&src->i);
	dst->i.i_size = 0;
	dst->i.i_blocks = cpu_to_le64(1);
	dst->i.i_links = cpu_to_le32(1);
	dst->i.i_xattr_nid = 0;

	new_ni = old_ni;
	new_ni.ino = ino;

	if (!inc_valid_node_count(sbi, NULL, 1))
		WARN_ON(1);
	set_node_addr(sbi, &new_ni, NEW_ADDR);
	inc_valid_inode_count(sbi);
	f2fs_put_page(ipage, 1);
	return 0;
}

int restore_node_summary(struct f2fs_sb_info *sbi,
			unsigned int segno, struct f2fs_summary_block *sum)
{
	struct f2fs_node *rn;
	struct f2fs_summary *sum_entry;
	struct page *page;
	block_t addr;
	int i, last_offset;

	/* alloc temporal page for read node */
	page = alloc_page(GFP_NOFS | __GFP_ZERO);
	if (!page)
		return -ENOMEM;
	lock_page(page);

	/* scan the node segment */
	last_offset = sbi->blocks_per_seg;
	addr = START_BLOCK(sbi, segno);
	sum_entry = &sum->entries[0];

	for (i = 0; i < last_offset; i++, sum_entry++) {
		/*
		 * In order to read next node page,
		 * we must clear PageUptodate flag.
		 */
		ClearPageUptodate(page);

		if (f2fs_readpage(sbi, page, addr, READ_SYNC))
			goto out;

		lock_page(page);
		rn = F2FS_NODE(page);
		sum_entry->nid = rn->footer.nid;
		sum_entry->version = 0;
		sum_entry->ofs_in_node = 0;
		addr++;
	}
	unlock_page(page);
out:
	__free_pages(page, 0);
	return 0;
}

static bool flush_nats_in_journal(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_summary_block *sum = curseg->sum_blk;
	int i;

	mutex_lock(&curseg->curseg_mutex);

	if (nats_in_cursum(sum) < NAT_JOURNAL_ENTRIES) {
		mutex_unlock(&curseg->curseg_mutex);
		return false;
	}

	for (i = 0; i < nats_in_cursum(sum); i++) {
		struct nat_entry *ne;
		struct f2fs_nat_entry raw_ne;
		nid_t nid = le32_to_cpu(nid_in_journal(sum, i));

		raw_ne = nat_in_journal(sum, i);
retry:
		write_lock(&nm_i->nat_tree_lock);
		ne = __lookup_nat_cache(nm_i, nid);
		if (ne) {
			__set_nat_cache_dirty(nm_i, ne);
			write_unlock(&nm_i->nat_tree_lock);
			continue;
		}
		ne = grab_nat_entry(nm_i, nid);
		if (!ne) {
			write_unlock(&nm_i->nat_tree_lock);
			goto retry;
		}
		nat_set_blkaddr(ne, le32_to_cpu(raw_ne.block_addr));
		nat_set_ino(ne, le32_to_cpu(raw_ne.ino));
		nat_set_version(ne, raw_ne.version);
		__set_nat_cache_dirty(nm_i, ne);
		write_unlock(&nm_i->nat_tree_lock);
	}
	update_nats_in_cursum(sum, -i);
	mutex_unlock(&curseg->curseg_mutex);
	return true;
}

/*
 * This function is called during the checkpointing process.
 */
void flush_nat_entries(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_summary_block *sum = curseg->sum_blk;
	struct list_head *cur, *n;
	struct page *page = NULL;
	struct f2fs_nat_block *nat_blk = NULL;
	nid_t start_nid = 0, end_nid = 0;
	bool flushed;

	flushed = flush_nats_in_journal(sbi);

	if (!flushed)
		mutex_lock(&curseg->curseg_mutex);

	/* 1) flush dirty nat caches */
	list_for_each_safe(cur, n, &nm_i->dirty_nat_entries) {
		struct nat_entry *ne;
		nid_t nid;
		struct f2fs_nat_entry raw_ne;
		int offset = -1;
		block_t new_blkaddr;

		ne = list_entry(cur, struct nat_entry, list);
		nid = nat_get_nid(ne);

		if (nat_get_blkaddr(ne) == NEW_ADDR)
			continue;
		if (flushed)
			goto to_nat_page;

		/* if there is room for nat enries in curseg->sumpage */
		offset = lookup_journal_in_cursum(sum, NAT_JOURNAL, nid, 1);
		if (offset >= 0) {
			raw_ne = nat_in_journal(sum, offset);
			goto flush_now;
		}
to_nat_page:
		if (!page || (start_nid > nid || nid > end_nid)) {
			if (page) {
				f2fs_put_page(page, 1);
				page = NULL;
			}
			start_nid = START_NID(nid);
			end_nid = start_nid + NAT_ENTRY_PER_BLOCK - 1;

			/*
			 * get nat block with dirty flag, increased reference
			 * count, mapped and lock
			 */
			page = get_next_nat_page(sbi, start_nid);
			nat_blk = page_address(page);
		}

		f2fs_bug_on(!nat_blk);
		raw_ne = nat_blk->entries[nid - start_nid];
flush_now:
		new_blkaddr = nat_get_blkaddr(ne);

		raw_ne.ino = cpu_to_le32(nat_get_ino(ne));
		raw_ne.block_addr = cpu_to_le32(new_blkaddr);
		raw_ne.version = nat_get_version(ne);

		if (offset < 0) {
			nat_blk->entries[nid - start_nid] = raw_ne;
		} else {
			nat_in_journal(sum, offset) = raw_ne;
			nid_in_journal(sum, offset) = cpu_to_le32(nid);
		}

		if (nat_get_blkaddr(ne) == NULL_ADDR &&
				add_free_nid(NM_I(sbi), nid, false) <= 0) {
			write_lock(&nm_i->nat_tree_lock);
			__del_from_nat_cache(nm_i, ne);
			write_unlock(&nm_i->nat_tree_lock);
		} else {
			write_lock(&nm_i->nat_tree_lock);
			__clear_nat_cache_dirty(nm_i, ne);
			ne->checkpointed = true;
			write_unlock(&nm_i->nat_tree_lock);
		}
	}
	if (!flushed)
		mutex_unlock(&curseg->curseg_mutex);
	f2fs_put_page(page, 1);

	/* 2) shrink nat caches if necessary */
	try_to_free_nats(sbi, nm_i->nat_cnt - NM_WOUT_THRESHOLD);
}

static int init_node_manager(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *sb_raw = F2FS_RAW_SUPER(sbi);
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned char *version_bitmap;
	unsigned int nat_segs, nat_blocks;

	nm_i->nat_blkaddr = le32_to_cpu(sb_raw->nat_blkaddr);

	/* segment_count_nat includes pair segment so divide to 2. */
	nat_segs = le32_to_cpu(sb_raw->segment_count_nat) >> 1;
	nat_blocks = nat_segs << le32_to_cpu(sb_raw->log_blocks_per_seg);
	nm_i->max_nid = NAT_ENTRY_PER_BLOCK * nat_blocks;
	nm_i->fcnt = 0;
	nm_i->nat_cnt = 0;

	INIT_LIST_HEAD(&nm_i->free_nid_list);
	INIT_RADIX_TREE(&nm_i->nat_root, GFP_ATOMIC);
	INIT_LIST_HEAD(&nm_i->nat_entries);
	INIT_LIST_HEAD(&nm_i->dirty_nat_entries);

	mutex_init(&nm_i->build_lock);
	spin_lock_init(&nm_i->free_nid_list_lock);
	rwlock_init(&nm_i->nat_tree_lock);

	nm_i->next_scan_nid = le32_to_cpu(sbi->ckpt->next_free_nid);
	nm_i->bitmap_size = __bitmap_size(sbi, NAT_BITMAP);
	version_bitmap = __bitmap_ptr(sbi, NAT_BITMAP);
	if (!version_bitmap)
		return -EFAULT;

	nm_i->nat_bitmap = kmemdup(version_bitmap, nm_i->bitmap_size,
					GFP_KERNEL);
	if (!nm_i->nat_bitmap)
		return -ENOMEM;
	return 0;
}

int build_node_manager(struct f2fs_sb_info *sbi)
{
	int err;

	sbi->nm_info = kzalloc(sizeof(struct f2fs_nm_info), GFP_KERNEL);
	if (!sbi->nm_info)
		return -ENOMEM;

	err = init_node_manager(sbi);
	if (err)
		return err;

	build_free_nids(sbi);
	return 0;
}

void destroy_node_manager(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i, *next_i;
	struct nat_entry *natvec[NATVEC_SIZE];
	nid_t nid = 0;
	unsigned int found;

	if (!nm_i)
		return;

	/* destroy free nid list */
	spin_lock(&nm_i->free_nid_list_lock);
	list_for_each_entry_safe(i, next_i, &nm_i->free_nid_list, list) {
		f2fs_bug_on(i->state == NID_ALLOC);
		__del_from_free_nid_list(i);
		nm_i->fcnt--;
	}
	f2fs_bug_on(nm_i->fcnt);
	spin_unlock(&nm_i->free_nid_list_lock);

	/* destroy nat cache */
	write_lock(&nm_i->nat_tree_lock);
	while ((found = __gang_lookup_nat_cache(nm_i,
					nid, NATVEC_SIZE, natvec))) {
		unsigned idx;
		for (idx = 0; idx < found; idx++) {
			struct nat_entry *e = natvec[idx];
			nid = nat_get_nid(e) + 1;
			__del_from_nat_cache(nm_i, e);
		}
	}
	f2fs_bug_on(nm_i->nat_cnt);
	write_unlock(&nm_i->nat_tree_lock);

	kfree(nm_i->nat_bitmap);
	sbi->nm_info = NULL;
	kfree(nm_i);
}

int __init create_node_manager_caches(void)
{
	nat_entry_slab = f2fs_kmem_cache_create("nat_entry",
			sizeof(struct nat_entry), NULL);
	if (!nat_entry_slab)
		return -ENOMEM;

	free_nid_slab = f2fs_kmem_cache_create("free_nid",
			sizeof(struct free_nid), NULL);
	if (!free_nid_slab) {
		kmem_cache_destroy(nat_entry_slab);
		return -ENOMEM;
	}
	return 0;
}

void destroy_node_manager_caches(void)
{
	kmem_cache_destroy(free_nid_slab);
	kmem_cache_destroy(nat_entry_slab);
}

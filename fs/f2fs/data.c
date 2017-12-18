/*
 * fs/f2fs/data.c
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
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/prefetch.h>
#include <linux/uio.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/cleancache.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "trace.h"
#include <trace/events/f2fs.h>

static void f2fs_read_end_io(struct bio *bio)
{
	struct bio_vec *bvec;
	int i;

#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (time_to_inject(F2FS_P_SB(bio->bi_io_vec->bv_page), FAULT_IO))
		bio->bi_error = -EIO;
#endif

	if (f2fs_bio_encrypted(bio)) {
		if (bio->bi_error) {
			fscrypt_release_ctx(bio->bi_private);
		} else {
			fscrypt_decrypt_bio_pages(bio->bi_private, bio);
			return;
		}
	}

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		if (!bio->bi_error) {
			if (!PageUptodate(page))
				SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	}
	bio_put(bio);
}

static void f2fs_write_end_io(struct bio *bio)
{
	struct f2fs_sb_info *sbi = bio->bi_private;
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		fscrypt_pullback_bio_page(&page, true);

		if (unlikely(bio->bi_error)) {
			mapping_set_error(page->mapping, -EIO);
			f2fs_stop_checkpoint(sbi, true);
		}
		end_page_writeback(page);
	}
	if (atomic_dec_and_test(&sbi->nr_wb_bios) &&
				wq_has_sleeper(&sbi->cp_wait))
		wake_up(&sbi->cp_wait);

	bio_put(bio);
}

/*
 * Low-level block read/write IO operations.
 */
static struct bio *__bio_alloc(struct f2fs_sb_info *sbi, block_t blk_addr,
				int npages, bool is_read)
{
	struct bio *bio;

	bio = f2fs_bio_alloc(npages);

	bio->bi_bdev = sbi->sb->s_bdev;
	bio->bi_iter.bi_sector = SECTOR_FROM_BLOCK(blk_addr);
	bio->bi_end_io = is_read ? f2fs_read_end_io : f2fs_write_end_io;
	bio->bi_private = is_read ? NULL : sbi;

	return bio;
}

static inline void __submit_bio(struct f2fs_sb_info *sbi,
				struct bio *bio, enum page_type type)
{
	if (!is_read_io(bio_op(bio))) {
		atomic_inc(&sbi->nr_wb_bios);
		if (f2fs_sb_mounted_hmsmr(sbi->sb) &&
			current->plug && (type == DATA || type == NODE))
			blk_finish_plug(current->plug);
	}
	submit_bio(bio);
}

static void __submit_merged_bio(struct f2fs_bio_info *io)
{
	struct f2fs_io_info *fio = &io->fio;

	if (!io->bio)
		return;

	if (is_read_io(fio->op))
		trace_f2fs_submit_read_bio(io->sbi->sb, fio, io->bio);
	else
		trace_f2fs_submit_write_bio(io->sbi->sb, fio, io->bio);

	bio_set_op_attrs(io->bio, fio->op, fio->op_flags);

	__submit_bio(io->sbi, io->bio, fio->type);
	io->bio = NULL;
}

static bool __has_merged_page(struct f2fs_bio_info *io, struct inode *inode,
						struct page *page, nid_t ino)
{
	struct bio_vec *bvec;
	struct page *target;
	int i;

	if (!io->bio)
		return false;

	if (!inode && !page && !ino)
		return true;

	bio_for_each_segment_all(bvec, io->bio, i) {

		if (bvec->bv_page->mapping)
			target = bvec->bv_page;
		else
			target = fscrypt_control_page(bvec->bv_page);

		if (inode && inode == target->mapping->host)
			return true;
		if (page && page == target)
			return true;
		if (ino && ino == ino_of_node(target))
			return true;
	}

	return false;
}

static bool has_merged_page(struct f2fs_sb_info *sbi, struct inode *inode,
						struct page *page, nid_t ino,
						enum page_type type)
{
	enum page_type btype = PAGE_TYPE_OF_BIO(type);
	struct f2fs_bio_info *io = &sbi->write_io[btype];
	bool ret;

	down_read(&io->io_rwsem);
	ret = __has_merged_page(io, inode, page, ino);
	up_read(&io->io_rwsem);
	return ret;
}

static void __f2fs_submit_merged_bio(struct f2fs_sb_info *sbi,
				struct inode *inode, struct page *page,
				nid_t ino, enum page_type type, int rw)
{
	enum page_type btype = PAGE_TYPE_OF_BIO(type);
	struct f2fs_bio_info *io;

	io = is_read_io(rw) ? &sbi->read_io : &sbi->write_io[btype];

	down_write(&io->io_rwsem);

	if (!__has_merged_page(io, inode, page, ino))
		goto out;

	/* change META to META_FLUSH in the checkpoint procedure */
	if (type >= META_FLUSH) {
		io->fio.type = META_FLUSH;
		io->fio.op = REQ_OP_WRITE;
		if (test_opt(sbi, NOBARRIER))
			io->fio.op_flags = WRITE_FLUSH | REQ_META | REQ_PRIO;
		else
			io->fio.op_flags = WRITE_FLUSH_FUA | REQ_META |
								REQ_PRIO;
	}
	__submit_merged_bio(io);
out:
	up_write(&io->io_rwsem);
}

void f2fs_submit_merged_bio(struct f2fs_sb_info *sbi, enum page_type type,
									int rw)
{
	__f2fs_submit_merged_bio(sbi, NULL, NULL, 0, type, rw);
}

void f2fs_submit_merged_bio_cond(struct f2fs_sb_info *sbi,
				struct inode *inode, struct page *page,
				nid_t ino, enum page_type type, int rw)
{
	if (has_merged_page(sbi, inode, page, ino, type))
		__f2fs_submit_merged_bio(sbi, inode, page, ino, type, rw);
}

void f2fs_flush_merged_bios(struct f2fs_sb_info *sbi)
{
	f2fs_submit_merged_bio(sbi, DATA, WRITE);
	f2fs_submit_merged_bio(sbi, NODE, WRITE);
	f2fs_submit_merged_bio(sbi, META, WRITE);
}

/*
 * Fill the locked page with data located in the block address.
 * Return unlocked page.
 */
int f2fs_submit_page_bio(struct f2fs_io_info *fio)
{
	struct bio *bio;
	struct page *page = fio->encrypted_page ?
			fio->encrypted_page : fio->page;

	trace_f2fs_submit_page_bio(page, fio);
	f2fs_trace_ios(fio, 0);

	/* Allocate a new bio */
	bio = __bio_alloc(fio->sbi, fio->new_blkaddr, 1, is_read_io(fio->op));

	if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
		bio_put(bio);
		return -EFAULT;
	}
	bio_set_op_attrs(bio, fio->op, fio->op_flags);

	__submit_bio(fio->sbi, bio, fio->type);
	return 0;
}

void f2fs_submit_page_mbio(struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	enum page_type btype = PAGE_TYPE_OF_BIO(fio->type);
	struct f2fs_bio_info *io;
	bool is_read = is_read_io(fio->op);
	struct page *bio_page;

	io = is_read ? &sbi->read_io : &sbi->write_io[btype];

	if (fio->old_blkaddr != NEW_ADDR)
		verify_block_addr(sbi, fio->old_blkaddr);
	verify_block_addr(sbi, fio->new_blkaddr);

	down_write(&io->io_rwsem);

	if (io->bio && (io->last_block_in_bio != fio->new_blkaddr - 1 ||
	    (io->fio.op != fio->op || io->fio.op_flags != fio->op_flags)))
		__submit_merged_bio(io);
alloc_new:
	if (io->bio == NULL) {
		int bio_blocks = MAX_BIO_BLOCKS(sbi);

		io->bio = __bio_alloc(sbi, fio->new_blkaddr,
						bio_blocks, is_read);
		io->fio = *fio;
	}

	bio_page = fio->encrypted_page ? fio->encrypted_page : fio->page;

	if (bio_add_page(io->bio, bio_page, PAGE_SIZE, 0) <
							PAGE_SIZE) {
		__submit_merged_bio(io);
		goto alloc_new;
	}

	io->last_block_in_bio = fio->new_blkaddr;
	f2fs_trace_ios(fio, 0);

	up_write(&io->io_rwsem);
	trace_f2fs_submit_page_mbio(fio->page, fio);
}

static void __set_data_blkaddr(struct dnode_of_data *dn)
{
	struct f2fs_node *rn = F2FS_NODE(dn->node_page);
	__le32 *addr_array;

	/* Get physical address of data block */
	addr_array = blkaddr_in_node(rn);
	addr_array[dn->ofs_in_node] = cpu_to_le32(dn->data_blkaddr);
}

/*
 * Lock ordering for the change of data block address:
 * ->data_page
 *  ->node_page
 *    update block addresses in the node page
 */
void set_data_blkaddr(struct dnode_of_data *dn)
{
	f2fs_wait_on_page_writeback(dn->node_page, NODE, true);
	__set_data_blkaddr(dn);
	if (set_page_dirty(dn->node_page))
		dn->node_changed = true;
}

void f2fs_update_data_blkaddr(struct dnode_of_data *dn, block_t blkaddr)
{
	dn->data_blkaddr = blkaddr;
	set_data_blkaddr(dn);
	f2fs_update_extent_cache(dn);
}

/* dn->ofs_in_node will be returned with up-to-date last block pointer */
int reserve_new_blocks(struct dnode_of_data *dn, blkcnt_t count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);

	if (!count)
		return 0;

	if (unlikely(is_inode_flag_set(dn->inode, FI_NO_ALLOC)))
		return -EPERM;
	if (unlikely(!inc_valid_block_count(sbi, dn->inode, &count)))
		return -ENOSPC;

	trace_f2fs_reserve_new_blocks(dn->inode, dn->nid,
						dn->ofs_in_node, count);

	f2fs_wait_on_page_writeback(dn->node_page, NODE, true);

	for (; count > 0; dn->ofs_in_node++) {
		block_t blkaddr =
			datablock_addr(dn->node_page, dn->ofs_in_node);
		if (blkaddr == NULL_ADDR) {
			dn->data_blkaddr = NEW_ADDR;
			__set_data_blkaddr(dn);
			count--;
		}
	}

	if (set_page_dirty(dn->node_page))
		dn->node_changed = true;
	return 0;
}

/* Should keep dn->ofs_in_node unchanged */
int reserve_new_block(struct dnode_of_data *dn)
{
	unsigned int ofs_in_node = dn->ofs_in_node;
	int ret;

	ret = reserve_new_blocks(dn, 1);
	dn->ofs_in_node = ofs_in_node;
	return ret;
}

int f2fs_reserve_block(struct dnode_of_data *dn, pgoff_t index)
{
	bool need_put = dn->inode_page ? false : true;
	int err;

	err = get_dnode_of_data(dn, index, ALLOC_NODE);
	if (err)
		return err;

	if (dn->data_blkaddr == NULL_ADDR)
		err = reserve_new_block(dn);
	if (err || need_put)
		f2fs_put_dnode(dn);
	return err;
}

int f2fs_get_block(struct dnode_of_data *dn, pgoff_t index)
{
	struct extent_info ei;
	struct inode *inode = dn->inode;

	if (f2fs_lookup_extent_cache(inode, index, &ei)) {
		dn->data_blkaddr = ei.blk + index - ei.fofs;
		return 0;
	}

	return f2fs_reserve_block(dn, index);
}

struct page *get_read_data_page(struct inode *inode, pgoff_t index,
						int op_flags, bool for_write)
{
	struct address_space *mapping = inode->i_mapping;
	struct dnode_of_data dn;
	struct page *page;
	struct extent_info ei;
	int err;
	struct f2fs_io_info fio = {
		.sbi = F2FS_I_SB(inode),
		.type = DATA,
		.op = REQ_OP_READ,
		.op_flags = op_flags,
		.encrypted_page = NULL,
	};

	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode))
		return read_mapping_page(mapping, index, NULL);

	page = f2fs_grab_cache_page(mapping, index, for_write);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (f2fs_lookup_extent_cache(inode, index, &ei)) {
		dn.data_blkaddr = ei.blk + index - ei.fofs;
		goto got_it;
	}

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err)
		goto put_err;
	f2fs_put_dnode(&dn);

	if (unlikely(dn.data_blkaddr == NULL_ADDR)) {
		err = -ENOENT;
		goto put_err;
	}
got_it:
	if (PageUptodate(page)) {
		unlock_page(page);
		return page;
	}

	/*
	 * A new dentry page is allocated but not able to be written, since its
	 * new inode page couldn't be allocated due to -ENOSPC.
	 * In such the case, its blkaddr can be remained as NEW_ADDR.
	 * see, f2fs_add_link -> get_new_data_page -> init_inode_metadata.
	 */
	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_SIZE);
		if (!PageUptodate(page))
			SetPageUptodate(page);
		unlock_page(page);
		return page;
	}

	fio.new_blkaddr = fio.old_blkaddr = dn.data_blkaddr;
	fio.page = page;
	err = f2fs_submit_page_bio(&fio);
	if (err)
		goto put_err;
	return page;

put_err:
	f2fs_put_page(page, 1);
	return ERR_PTR(err);
}

struct page *find_data_page(struct inode *inode, pgoff_t index)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;

	page = find_get_page(mapping, index);
	if (page && PageUptodate(page))
		return page;
	f2fs_put_page(page, 0);

	page = get_read_data_page(inode, index, READ_SYNC, false);
	if (IS_ERR(page))
		return page;

	if (PageUptodate(page))
		return page;

	wait_on_page_locked(page);
	if (unlikely(!PageUptodate(page))) {
		f2fs_put_page(page, 0);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * If it tries to access a hole, return an error.
 * Because, the callers, functions in dir.c and GC, should be able to know
 * whether this page exists or not.
 */
struct page *get_lock_data_page(struct inode *inode, pgoff_t index,
							bool for_write)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
repeat:
	page = get_read_data_page(inode, index, READ_SYNC, for_write);
	if (IS_ERR(page))
		return page;

	/* wait for read completion */
	lock_page(page);
	if (unlikely(page->mapping != mapping)) {
		f2fs_put_page(page, 1);
		goto repeat;
	}
	if (unlikely(!PageUptodate(page))) {
		f2fs_put_page(page, 1);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * Caller ensures that this data page is never allocated.
 * A new zero-filled data page is allocated in the page cache.
 *
 * Also, caller should grab and release a rwsem by calling f2fs_lock_op() and
 * f2fs_unlock_op().
 * Note that, ipage is set only by make_empty_dir, and if any error occur,
 * ipage should be released by this function.
 */
struct page *get_new_data_page(struct inode *inode,
		struct page *ipage, pgoff_t index, bool new_i_size)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	struct dnode_of_data dn;
	int err;

	page = f2fs_grab_cache_page(mapping, index, true);
	if (!page) {
		/*
		 * before exiting, we should make sure ipage will be released
		 * if any error occur.
		 */
		f2fs_put_page(ipage, 1);
		return ERR_PTR(-ENOMEM);
	}

	set_new_dnode(&dn, inode, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, index);
	if (err) {
		f2fs_put_page(page, 1);
		return ERR_PTR(err);
	}
	if (!ipage)
		f2fs_put_dnode(&dn);

	if (PageUptodate(page))
		goto got_it;

	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_SIZE);
		if (!PageUptodate(page))
			SetPageUptodate(page);
	} else {
		f2fs_put_page(page, 1);

		/* if ipage exists, blkaddr should be NEW_ADDR */
		f2fs_bug_on(F2FS_I_SB(inode), ipage);
		page = get_lock_data_page(inode, index, true);
		if (IS_ERR(page))
			return page;
	}
got_it:
	if (new_i_size && i_size_read(inode) <
				((loff_t)(index + 1) << PAGE_SHIFT))
		f2fs_i_size_write(inode, ((loff_t)(index + 1) << PAGE_SHIFT));
	return page;
}

static int __allocate_data_block(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct f2fs_summary sum;
	struct node_info ni;
	int seg = CURSEG_WARM_DATA;
	pgoff_t fofs;
	blkcnt_t count = 1;

	if (unlikely(is_inode_flag_set(dn->inode, FI_NO_ALLOC)))
		return -EPERM;

	dn->data_blkaddr = datablock_addr(dn->node_page, dn->ofs_in_node);
	if (dn->data_blkaddr == NEW_ADDR)
		goto alloc;

	if (unlikely(!inc_valid_block_count(sbi, dn->inode, &count)))
		return -ENOSPC;

alloc:
	get_node_info(sbi, dn->nid, &ni);
	set_summary(&sum, dn->nid, dn->ofs_in_node, ni.version);

	if (dn->ofs_in_node == 0 && dn->inode_page == dn->node_page)
		seg = CURSEG_DIRECT_IO;

	allocate_data_block(sbi, NULL, dn->data_blkaddr, &dn->data_blkaddr,
								&sum, seg);
	set_data_blkaddr(dn);

	/* update i_size */
	fofs = start_bidx_of_node(ofs_of_node(dn->node_page), dn->inode) +
							dn->ofs_in_node;
	if (i_size_read(dn->inode) < ((loff_t)(fofs + 1) << PAGE_SHIFT))
		f2fs_i_size_write(dn->inode,
				((loff_t)(fofs + 1) << PAGE_SHIFT));
	return 0;
}

ssize_t f2fs_preallocate_blocks(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct f2fs_map_blocks map;
	ssize_t ret = 0;

	map.m_lblk = F2FS_BLK_ALIGN(iocb->ki_pos);
	map.m_len = F2FS_BYTES_TO_BLK(iocb->ki_pos + iov_iter_count(from));
	if (map.m_len > map.m_lblk)
		map.m_len -= map.m_lblk;
	else
		map.m_len = 0;

	map.m_next_pgofs = NULL;

	if (iocb->ki_flags & IOCB_DIRECT) {
		ret = f2fs_convert_inline_inode(inode);
		if (ret)
			return ret;
		return f2fs_map_blocks(inode, &map, 1, F2FS_GET_BLOCK_PRE_DIO);
	}
	if (iocb->ki_pos + iov_iter_count(from) > MAX_INLINE_DATA) {
		ret = f2fs_convert_inline_inode(inode);
		if (ret)
			return ret;
	}
	if (!f2fs_has_inline_data(inode))
		return f2fs_map_blocks(inode, &map, 1, F2FS_GET_BLOCK_PRE_AIO);
	return ret;
}

/*
 * f2fs_map_blocks() now supported readahead/bmap/rw direct_IO with
 * f2fs_map_blocks structure.
 * If original data blocks are allocated, then give them to blockdev.
 * Otherwise,
 *     a. preallocate requested block addresses
 *     b. do not use extent cache for better performance
 *     c. give the block addresses to blockdev
 */
int f2fs_map_blocks(struct inode *inode, struct f2fs_map_blocks *map,
						int create, int flag)
{
	unsigned int maxblocks = map->m_len;
	struct dnode_of_data dn;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int mode = create ? ALLOC_NODE : LOOKUP_NODE;
	pgoff_t pgofs, end_offset, end;
	int err = 0, ofs = 1;
	unsigned int ofs_in_node, last_ofs_in_node;
	blkcnt_t prealloc;
	struct extent_info ei;
	bool allocated = false;
	block_t blkaddr;

	if (!maxblocks)
		return 0;

	map->m_len = 0;
	map->m_flags = 0;

	/* it only supports block size == page size */
	pgofs =	(pgoff_t)map->m_lblk;
	end = pgofs + maxblocks;

	if (!create && f2fs_lookup_extent_cache(inode, pgofs, &ei)) {
		map->m_pblk = ei.blk + pgofs - ei.fofs;
		map->m_len = min((pgoff_t)maxblocks, ei.fofs + ei.len - pgofs);
		map->m_flags = F2FS_MAP_MAPPED;
		goto out;
	}

next_dnode:
	if (create)
		f2fs_lock_op(sbi);

	/* When reading holes, we need its node page */
	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, pgofs, mode);
	if (err) {
		if (flag == F2FS_GET_BLOCK_BMAP)
			map->m_pblk = 0;
		if (err == -ENOENT) {
			err = 0;
			if (map->m_next_pgofs)
				*map->m_next_pgofs =
					get_next_page_offset(&dn, pgofs);
		}
		goto unlock_out;
	}

	prealloc = 0;
	ofs_in_node = dn.ofs_in_node;
	end_offset = ADDRS_PER_PAGE(dn.node_page, inode);

next_block:
	blkaddr = datablock_addr(dn.node_page, dn.ofs_in_node);

	if (blkaddr == NEW_ADDR || blkaddr == NULL_ADDR) {
		if (create) {
			if (unlikely(f2fs_cp_error(sbi))) {
				err = -EIO;
				goto sync_out;
			}
			if (flag == F2FS_GET_BLOCK_PRE_AIO) {
				if (blkaddr == NULL_ADDR) {
					prealloc++;
					last_ofs_in_node = dn.ofs_in_node;
				}
			} else {
				err = __allocate_data_block(&dn);
				if (!err) {
					set_inode_flag(inode, FI_APPEND_WRITE);
					allocated = true;
				}
			}
			if (err)
				goto sync_out;
			map->m_flags = F2FS_MAP_NEW;
			blkaddr = dn.data_blkaddr;
		} else {
			if (flag == F2FS_GET_BLOCK_BMAP) {
				map->m_pblk = 0;
				goto sync_out;
			}
			if (flag == F2FS_GET_BLOCK_FIEMAP &&
						blkaddr == NULL_ADDR) {
				if (map->m_next_pgofs)
					*map->m_next_pgofs = pgofs + 1;
			}
			if (flag != F2FS_GET_BLOCK_FIEMAP ||
						blkaddr != NEW_ADDR)
				goto sync_out;
		}
	}

	if (flag == F2FS_GET_BLOCK_PRE_AIO)
		goto skip;

	if (map->m_len == 0) {
		/* preallocated unwritten block should be mapped for fiemap. */
		if (blkaddr == NEW_ADDR)
			map->m_flags |= F2FS_MAP_UNWRITTEN;
		map->m_flags |= F2FS_MAP_MAPPED;

		map->m_pblk = blkaddr;
		map->m_len = 1;
	} else if ((map->m_pblk != NEW_ADDR &&
			blkaddr == (map->m_pblk + ofs)) ||
			(map->m_pblk == NEW_ADDR && blkaddr == NEW_ADDR) ||
			flag == F2FS_GET_BLOCK_PRE_DIO) {
		ofs++;
		map->m_len++;
	} else {
		goto sync_out;
	}

skip:
	dn.ofs_in_node++;
	pgofs++;

	/* preallocate blocks in batch for one dnode page */
	if (flag == F2FS_GET_BLOCK_PRE_AIO &&
			(pgofs == end || dn.ofs_in_node == end_offset)) {

		dn.ofs_in_node = ofs_in_node;
		err = reserve_new_blocks(&dn, prealloc);
		if (err)
			goto sync_out;
		allocated = dn.node_changed;

		map->m_len += dn.ofs_in_node - ofs_in_node;
		if (prealloc && dn.ofs_in_node != last_ofs_in_node + 1) {
			err = -ENOSPC;
			goto sync_out;
		}
		dn.ofs_in_node = end_offset;
	}

	if (pgofs >= end)
		goto sync_out;
	else if (dn.ofs_in_node < end_offset)
		goto next_block;

	f2fs_put_dnode(&dn);

	if (create) {
		f2fs_unlock_op(sbi);
		f2fs_balance_fs(sbi, allocated);
	}
	allocated = false;
	goto next_dnode;

sync_out:
	f2fs_put_dnode(&dn);
unlock_out:
	if (create) {
		f2fs_unlock_op(sbi);
		f2fs_balance_fs(sbi, allocated);
	}
out:
	trace_f2fs_map_blocks(inode, map, err);
	return err;
}

static int __get_data_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh, int create, int flag,
			pgoff_t *next_pgofs)
{
	struct f2fs_map_blocks map;
	int ret;

	map.m_lblk = iblock;
	map.m_len = bh->b_size >> inode->i_blkbits;
	map.m_next_pgofs = next_pgofs;

	ret = f2fs_map_blocks(inode, &map, create, flag);
	if (!ret) {
		map_bh(bh, inode->i_sb, map.m_pblk);
		bh->b_state = (bh->b_state & ~F2FS_MAP_FLAGS) | map.m_flags;
		bh->b_size = map.m_len << inode->i_blkbits;
	}
	return ret;
}

static int get_data_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create, int flag,
			pgoff_t *next_pgofs)
{
	return __get_data_block(inode, iblock, bh_result, create,
							flag, next_pgofs);
}

static int get_data_block_dio(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	return __get_data_block(inode, iblock, bh_result, create,
						F2FS_GET_BLOCK_DIO, NULL);
}

static int get_data_block_bmap(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	/* Block number less than F2FS MAX BLOCKS */
	if (unlikely(iblock >= F2FS_I_SB(inode)->max_file_blocks))
		return -EFBIG;

	return __get_data_block(inode, iblock, bh_result, create,
						F2FS_GET_BLOCK_BMAP, NULL);
}

static inline sector_t logical_to_blk(struct inode *inode, loff_t offset)
{
	return (offset >> inode->i_blkbits);
}

static inline loff_t blk_to_logical(struct inode *inode, sector_t blk)
{
	return (blk << inode->i_blkbits);
}

int f2fs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	struct buffer_head map_bh;
	sector_t start_blk, last_blk;
	pgoff_t next_pgofs;
	loff_t isize;
	u64 logical = 0, phys = 0, size = 0;
	u32 flags = 0;
	int ret = 0;

	ret = fiemap_check_flags(fieinfo, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	if (f2fs_has_inline_data(inode)) {
		ret = f2fs_inline_data_fiemap(inode, fieinfo, start, len);
		if (ret != -EAGAIN)
			return ret;
	}

	inode_lock(inode);

	isize = i_size_read(inode);
	if (start >= isize)
		goto out;

	if (start + len > isize)
		len = isize - start;

	if (logical_to_blk(inode, len) == 0)
		len = blk_to_logical(inode, 1);

	start_blk = logical_to_blk(inode, start);
	last_blk = logical_to_blk(inode, start + len - 1);

next:
	memset(&map_bh, 0, sizeof(struct buffer_head));
	map_bh.b_size = len;

	ret = get_data_block(inode, start_blk, &map_bh, 0,
					F2FS_GET_BLOCK_FIEMAP, &next_pgofs);
	if (ret)
		goto out;

	/* HOLE */
	if (!buffer_mapped(&map_bh)) {
		start_blk = next_pgofs;
		/* Go through holes util pass the EOF */
		if (blk_to_logical(inode, start_blk) < isize)
			goto prep_next;
		/* Found a hole beyond isize means no more extents.
		 * Note that the premise is that filesystems don't
		 * punch holes beyond isize and keep size unchanged.
		 */
		flags |= FIEMAP_EXTENT_LAST;
	}

	if (size) {
		if (f2fs_encrypted_inode(inode))
			flags |= FIEMAP_EXTENT_DATA_ENCRYPTED;

		ret = fiemap_fill_next_extent(fieinfo, logical,
				phys, size, flags);
	}

	if (start_blk > last_blk || ret)
		goto out;

	logical = blk_to_logical(inode, start_blk);
	phys = blk_to_logical(inode, map_bh.b_blocknr);
	size = map_bh.b_size;
	flags = 0;
	if (buffer_unwritten(&map_bh))
		flags = FIEMAP_EXTENT_UNWRITTEN;

	start_blk += logical_to_blk(inode, size);

prep_next:
	cond_resched();
	if (fatal_signal_pending(current))
		ret = -EINTR;
	else
		goto next;
out:
	if (ret == 1)
		ret = 0;

	inode_unlock(inode);
	return ret;
}

static struct bio *f2fs_grab_bio(struct inode *inode, block_t blkaddr,
				 unsigned nr_pages)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct fscrypt_ctx *ctx = NULL;
	struct block_device *bdev = sbi->sb->s_bdev;
	struct bio *bio;

	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode)) {
		ctx = fscrypt_get_ctx(inode, GFP_NOFS);
		if (IS_ERR(ctx))
			return ERR_CAST(ctx);

		/* wait the page to be moved by cleaning */
		f2fs_wait_on_encrypted_page_writeback(sbi, blkaddr);
	}

	bio = bio_alloc(GFP_KERNEL, min_t(int, nr_pages, BIO_MAX_PAGES));
	if (!bio) {
		if (ctx)
			fscrypt_release_ctx(ctx);
		return ERR_PTR(-ENOMEM);
	}
	bio->bi_bdev = bdev;
	bio->bi_iter.bi_sector = SECTOR_FROM_BLOCK(blkaddr);
	bio->bi_end_io = f2fs_read_end_io;
	bio->bi_private = ctx;

	return bio;
}

/*
 * This function was originally taken from fs/mpage.c, and customized for f2fs.
 * Major change was from block_size == page_size in f2fs by default.
 */
static int f2fs_mpage_readpages(struct address_space *mapping,
			struct list_head *pages, struct page *page,
			unsigned nr_pages)
{
	struct bio *bio = NULL;
	unsigned page_idx;
	sector_t last_block_in_bio = 0;
	struct inode *inode = mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocksize = 1 << blkbits;
	sector_t block_in_file;
	sector_t last_block;
	sector_t last_block_in_file;
	sector_t block_nr;
	struct f2fs_map_blocks map;

	map.m_pblk = 0;
	map.m_lblk = 0;
	map.m_len = 0;
	map.m_flags = 0;
	map.m_next_pgofs = NULL;

	for (page_idx = 0; nr_pages; page_idx++, nr_pages--) {

		prefetchw(&page->flags);
		if (pages) {
			page = list_entry(pages->prev, struct page, lru);
			list_del(&page->lru);
			if (add_to_page_cache_lru(page, mapping,
						  page->index,
						  readahead_gfp_mask(mapping)))
				goto next_page;
		}

		block_in_file = (sector_t)page->index;
		last_block = block_in_file + nr_pages;
		last_block_in_file = (i_size_read(inode) + blocksize - 1) >>
								blkbits;
		if (last_block > last_block_in_file)
			last_block = last_block_in_file;

		/*
		 * Map blocks using the previous result first.
		 */
		if ((map.m_flags & F2FS_MAP_MAPPED) &&
				block_in_file > map.m_lblk &&
				block_in_file < (map.m_lblk + map.m_len))
			goto got_it;

		/*
		 * Then do more f2fs_map_blocks() calls until we are
		 * done with this page.
		 */
		map.m_flags = 0;

		if (block_in_file < last_block) {
			map.m_lblk = block_in_file;
			map.m_len = last_block - block_in_file;

			if (f2fs_map_blocks(inode, &map, 0,
						F2FS_GET_BLOCK_READ))
				goto set_error_page;
		}
got_it:
		if ((map.m_flags & F2FS_MAP_MAPPED)) {
			block_nr = map.m_pblk + block_in_file - map.m_lblk;
			SetPageMappedToDisk(page);

			if (!PageUptodate(page) && !cleancache_get_page(page)) {
				SetPageUptodate(page);
				goto confused;
			}
		} else {
			zero_user_segment(page, 0, PAGE_SIZE);
			if (!PageUptodate(page))
				SetPageUptodate(page);
			unlock_page(page);
			goto next_page;
		}

		/*
		 * This page will go to BIO.  Do we need to send this
		 * BIO off first?
		 */
		if (bio && (last_block_in_bio != block_nr - 1)) {
submit_and_realloc:
			__submit_bio(F2FS_I_SB(inode), bio, DATA);
			bio = NULL;
		}
		if (bio == NULL) {
			bio = f2fs_grab_bio(inode, block_nr, nr_pages);
			if (IS_ERR(bio)) {
				bio = NULL;
				goto set_error_page;
			}
			bio_set_op_attrs(bio, REQ_OP_READ, 0);
		}

		if (bio_add_page(bio, page, blocksize, 0) < blocksize)
			goto submit_and_realloc;

		last_block_in_bio = block_nr;
		goto next_page;
set_error_page:
		SetPageError(page);
		zero_user_segment(page, 0, PAGE_SIZE);
		unlock_page(page);
		goto next_page;
confused:
		if (bio) {
			__submit_bio(F2FS_I_SB(inode), bio, DATA);
			bio = NULL;
		}
		unlock_page(page);
next_page:
		if (pages)
			put_page(page);
	}
	BUG_ON(pages && !list_empty(pages));
	if (bio)
		__submit_bio(F2FS_I_SB(inode), bio, DATA);
	return 0;
}

static int f2fs_read_data_page(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	int ret = -EAGAIN;

	trace_f2fs_readpage(page, DATA);

	/* If the file has inline data, try to read it directly */
	if (f2fs_has_inline_data(inode))
		ret = f2fs_read_inline_data(inode, page);
	if (ret == -EAGAIN)
		ret = f2fs_mpage_readpages(page->mapping, NULL, page, 1);
	return ret;
}

static int f2fs_read_data_pages(struct file *file,
			struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages)
{
	struct inode *inode = file->f_mapping->host;
	struct page *page = list_entry(pages->prev, struct page, lru);

	trace_f2fs_readpages(inode, page, nr_pages);

	/* If the file has inline data, skip readpages */
	if (f2fs_has_inline_data(inode))
		return 0;

	return f2fs_mpage_readpages(mapping, pages, NULL, nr_pages);
}

int do_write_data_page(struct f2fs_io_info *fio)
{
	struct page *page = fio->page;
	struct inode *inode = page->mapping->host;
	struct dnode_of_data dn;
	int err = 0;

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, page->index, LOOKUP_NODE);
	if (err)
		return err;

	fio->old_blkaddr = dn.data_blkaddr;

	/* This page is already truncated */
	if (fio->old_blkaddr == NULL_ADDR) {
		ClearPageUptodate(page);
		goto out_writepage;
	}

	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode)) {
		gfp_t gfp_flags = GFP_NOFS;

		/* wait for GCed encrypted page writeback */
		f2fs_wait_on_encrypted_page_writeback(F2FS_I_SB(inode),
							fio->old_blkaddr);
retry_encrypt:
		fio->encrypted_page = fscrypt_encrypt_page(inode, fio->page,
								gfp_flags);
		if (IS_ERR(fio->encrypted_page)) {
			err = PTR_ERR(fio->encrypted_page);
			if (err == -ENOMEM) {
				/* flush pending ios and wait for a while */
				f2fs_flush_merged_bios(F2FS_I_SB(inode));
				congestion_wait(BLK_RW_ASYNC, HZ/50);
				gfp_flags |= __GFP_NOFAIL;
				err = 0;
				goto retry_encrypt;
			}
			goto out_writepage;
		}
	}

	set_page_writeback(page);

	/*
	 * If current allocation needs SSR,
	 * it had better in-place writes for updated data.
	 */
	if (unlikely(fio->old_blkaddr != NEW_ADDR &&
			!is_cold_data(page) &&
			!IS_ATOMIC_WRITTEN_PAGE(page) &&
			need_inplace_update(inode))) {
		rewrite_data_page(fio);
		set_inode_flag(inode, FI_UPDATE_WRITE);
		trace_f2fs_do_write_data_page(page, IPU);
	} else {
		write_data_page(&dn, fio);
		trace_f2fs_do_write_data_page(page, OPU);
		set_inode_flag(inode, FI_APPEND_WRITE);
		if (page->index == 0)
			set_inode_flag(inode, FI_FIRST_BLOCK_WRITTEN);
	}
out_writepage:
	f2fs_put_dnode(&dn);
	return err;
}

static int f2fs_write_data_page(struct page *page,
					struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	loff_t i_size = i_size_read(inode);
	const pgoff_t end_index = ((unsigned long long) i_size)
							>> PAGE_SHIFT;
	loff_t psize = (page->index + 1) << PAGE_SHIFT;
	unsigned offset = 0;
	bool need_balance_fs = false;
	int err = 0;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.type = DATA,
		.op = REQ_OP_WRITE,
		.op_flags = (wbc->sync_mode == WB_SYNC_ALL) ? WRITE_SYNC : 0,
		.page = page,
		.encrypted_page = NULL,
	};

	trace_f2fs_writepage(page, DATA);

	if (page->index < end_index)
		goto write;

	/*
	 * If the offset is out-of-range of file size,
	 * this page does not have to be written to disk.
	 */
	offset = i_size & (PAGE_SIZE - 1);
	if ((page->index >= end_index + 1) || !offset)
		goto out;

	zero_user_segment(page, offset, PAGE_SIZE);
write:
	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto redirty_out;
	if (f2fs_is_drop_cache(inode))
		goto out;
	/* we should not write 0'th page having journal header */
	if (f2fs_is_volatile_file(inode) && (!page->index ||
			(!wbc->for_reclaim &&
			available_free_memory(sbi, BASE_CHECK))))
		goto redirty_out;

	/* we should bypass data pages to proceed the kworkder jobs */
	if (unlikely(f2fs_cp_error(sbi))) {
		mapping_set_error(page->mapping, -EIO);
		goto out;
	}

	/* Dentry blocks are controlled by checkpoint */
	if (S_ISDIR(inode->i_mode)) {
		err = do_write_data_page(&fio);
		goto done;
	}

	if (!wbc->for_reclaim)
		need_balance_fs = true;
	else if (has_not_enough_free_secs(sbi, 0, 0))
		goto redirty_out;

	err = -EAGAIN;
	f2fs_lock_op(sbi);
	if (f2fs_has_inline_data(inode))
		err = f2fs_write_inline_data(inode, page);
	if (err == -EAGAIN)
		err = do_write_data_page(&fio);
	if (F2FS_I(inode)->last_disk_size < psize)
		F2FS_I(inode)->last_disk_size = psize;
	f2fs_unlock_op(sbi);
done:
	if (err && err != -ENOENT)
		goto redirty_out;

	clear_cold_data(page);
out:
	inode_dec_dirty_pages(inode);
	if (err)
		ClearPageUptodate(page);

	if (wbc->for_reclaim) {
		f2fs_submit_merged_bio_cond(sbi, NULL, page, 0, DATA, WRITE);
		remove_dirty_inode(inode);
	}

	unlock_page(page);
	f2fs_balance_fs(sbi, need_balance_fs);

	if (unlikely(f2fs_cp_error(sbi)))
		f2fs_submit_merged_bio(sbi, DATA, WRITE);

	return 0;

redirty_out:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return err;
}

/*
 * This function was copied from write_cche_pages from mm/page-writeback.c.
 * The major change is making write step of cold data page separately from
 * warm/hot data page.
 */
static int f2fs_write_cache_pages(struct address_space *mapping,
					struct writeback_control *wbc)
{
	int ret = 0;
	int done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t uninitialized_var(writeback_index);
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index;
	int cycled;
	int range_whole = 0;
	int tag;
	int nwritten = 0;

	pagevec_init(&pvec, 0);

	if (wbc->range_cyclic) {
		writeback_index = mapping->writeback_index; /* prev offset */
		index = writeback_index;
		if (index == 0)
			cycled = 1;
		else
			cycled = 0;
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		cycled = 1; /* ignore range_cyclic tests */
	}
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);
	done_index = index;
	while (!done && (index <= end)) {
		int i;

		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index, tag,
			      min(end - index, (pgoff_t)PAGEVEC_SIZE - 1) + 1);
		if (nr_pages == 0)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			if (page->index > end) {
				done = 1;
				break;
			}

			done_index = page->index;

			lock_page(page);

			if (unlikely(page->mapping != mapping)) {
continue_unlock:
				unlock_page(page);
				continue;
			}

			if (!PageDirty(page)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (PageWriteback(page)) {
				if (wbc->sync_mode != WB_SYNC_NONE)
					f2fs_wait_on_page_writeback(page,
								DATA, true);
				else
					goto continue_unlock;
			}

			BUG_ON(PageWriteback(page));
			if (!clear_page_dirty_for_io(page))
				goto continue_unlock;

			ret = mapping->a_ops->writepage(page, wbc);
			if (unlikely(ret)) {
				done_index = page->index + 1;
				done = 1;
				break;
			} else {
				nwritten++;
			}

			if (--wbc->nr_to_write <= 0 &&
			    wbc->sync_mode == WB_SYNC_NONE) {
				done = 1;
				break;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}

	if (!cycled && !done) {
		cycled = 1;
		index = 0;
		end = writeback_index - 1;
		goto retry;
	}
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = done_index;

	if (nwritten)
		f2fs_submit_merged_bio_cond(F2FS_M_SB(mapping), mapping->host,
							NULL, 0, DATA, WRITE);

	return ret;
}

static int f2fs_write_data_pages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct blk_plug plug;
	int ret;

	/* deal with chardevs and other special file */
	if (!mapping->a_ops->writepage)
		return 0;

	/* skip writing if there is no dirty page in this inode */
	if (!get_dirty_pages(inode) && wbc->sync_mode == WB_SYNC_NONE)
		return 0;

	if (S_ISDIR(inode->i_mode) && wbc->sync_mode == WB_SYNC_NONE &&
			get_dirty_pages(inode) < nr_pages_to_skip(sbi, DATA) &&
			available_free_memory(sbi, DIRTY_DENTS))
		goto skip_write;

	/* skip writing during file defragment */
	if (is_inode_flag_set(inode, FI_DO_DEFRAG))
		goto skip_write;

	/* during POR, we don't need to trigger writepage at all. */
	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto skip_write;

	trace_f2fs_writepages(mapping->host, wbc, DATA);

	blk_start_plug(&plug);
	ret = f2fs_write_cache_pages(mapping, wbc);
	blk_finish_plug(&plug);
	/*
	 * if some pages were truncated, we cannot guarantee its mapping->host
	 * to detect pending bios.
	 */

	remove_dirty_inode(inode);
	return ret;

skip_write:
	wbc->pages_skipped += get_dirty_pages(inode);
	trace_f2fs_writepages(mapping->host, wbc, DATA);
	return 0;
}

static void f2fs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;
	loff_t i_size = i_size_read(inode);

	if (to > i_size) {
		truncate_pagecache(inode, i_size);
		truncate_blocks(inode, i_size, true);
	}
}

static int prepare_write_begin(struct f2fs_sb_info *sbi,
			struct page *page, loff_t pos, unsigned len,
			block_t *blk_addr, bool *node_changed)
{
	struct inode *inode = page->mapping->host;
	pgoff_t index = page->index;
	struct dnode_of_data dn;
	struct page *ipage;
	bool locked = false;
	struct extent_info ei;
	int err = 0;

	/*
	 * we already allocated all the blocks, so we don't need to get
	 * the block addresses when there is no need to fill the page.
	 */
	if (!f2fs_has_inline_data(inode) && len == PAGE_SIZE)
		return 0;

	if (f2fs_has_inline_data(inode) ||
			(pos & PAGE_MASK) >= i_size_read(inode)) {
		f2fs_lock_op(sbi);
		locked = true;
	}
restart:
	/* check inline_data */
	ipage = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto unlock_out;
	}

	set_new_dnode(&dn, inode, ipage, ipage, 0);

	if (f2fs_has_inline_data(inode)) {
		if (pos + len <= MAX_INLINE_DATA) {
			read_inline_data(page, ipage);
			set_inode_flag(inode, FI_DATA_EXIST);
			if (inode->i_nlink)
				set_inline_node(ipage);
		} else {
			err = f2fs_convert_inline_page(&dn, page);
			if (err)
				goto out;
			if (dn.data_blkaddr == NULL_ADDR)
				err = f2fs_get_block(&dn, index);
		}
	} else if (locked) {
		err = f2fs_get_block(&dn, index);
	} else {
		if (f2fs_lookup_extent_cache(inode, index, &ei)) {
			dn.data_blkaddr = ei.blk + index - ei.fofs;
		} else {
			/* hole case */
			err = get_dnode_of_data(&dn, index, LOOKUP_NODE);
			if (err || dn.data_blkaddr == NULL_ADDR) {
				f2fs_put_dnode(&dn);
				f2fs_lock_op(sbi);
				locked = true;
				goto restart;
			}
		}
	}

	/* convert_inline_page can make node_changed */
	*blk_addr = dn.data_blkaddr;
	*node_changed = dn.node_changed;
out:
	f2fs_put_dnode(&dn);
unlock_out:
	if (locked)
		f2fs_unlock_op(sbi);
	return err;
}

static int f2fs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct page *page = NULL;
	pgoff_t index = ((unsigned long long) pos) >> PAGE_SHIFT;
	bool need_balance = false;
	block_t blkaddr = NULL_ADDR;
	int err = 0;

	trace_f2fs_write_begin(inode, pos, len, flags);

	/*
	 * We should check this at this moment to avoid deadlock on inode page
	 * and #0 page. The locking rule for inline_data conversion should be:
	 * lock_page(page #0) -> lock_page(inode_page)
	 */
	if (index != 0) {
		err = f2fs_convert_inline_inode(inode);
		if (err)
			goto fail;
	}
repeat:
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		err = -ENOMEM;
		goto fail;
	}

	*pagep = page;

	err = prepare_write_begin(sbi, page, pos, len,
					&blkaddr, &need_balance);
	if (err)
		goto fail;

	if (need_balance && has_not_enough_free_secs(sbi, 0, 0)) {
		unlock_page(page);
		f2fs_balance_fs(sbi, true);
		lock_page(page);
		if (page->mapping != mapping) {
			/* The page got truncated from under us */
			f2fs_put_page(page, 1);
			goto repeat;
		}
	}

	f2fs_wait_on_page_writeback(page, DATA, false);

	/* wait for GCed encrypted page writeback */
	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode))
		f2fs_wait_on_encrypted_page_writeback(sbi, blkaddr);

	if (len == PAGE_SIZE || PageUptodate(page))
		return 0;

	if (blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_SIZE);
		SetPageUptodate(page);
	} else {
		struct bio *bio;

		bio = f2fs_grab_bio(inode, blkaddr, 1);
		if (IS_ERR(bio)) {
			err = PTR_ERR(bio);
			goto fail;
		}
		bio_set_op_attrs(bio, REQ_OP_READ, READ_SYNC);
		if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
			bio_put(bio);
			err = -EFAULT;
			goto fail;
		}

		__submit_bio(sbi, bio, DATA);

		lock_page(page);
		if (unlikely(page->mapping != mapping)) {
			f2fs_put_page(page, 1);
			goto repeat;
		}
		if (unlikely(!PageUptodate(page))) {
			err = -EIO;
			goto fail;
		}
	}
	return 0;

fail:
	f2fs_put_page(page, 1);
	f2fs_write_failed(mapping, pos + len);
	return err;
}

static int f2fs_write_end(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;

	trace_f2fs_write_end(inode, pos, len, copied);

	/*
	 * This should be come from len == PAGE_SIZE, and we expect copied
	 * should be PAGE_SIZE. Otherwise, we treat it with zero copied and
	 * let generic_perform_write() try to copy data again through copied=0.
	 */
	if (!PageUptodate(page)) {
		if (unlikely(copied != PAGE_SIZE))
			copied = 0;
		else
			SetPageUptodate(page);
	}
	if (!copied)
		goto unlock_out;

	set_page_dirty(page);
	clear_cold_data(page);

	if (pos + copied > i_size_read(inode))
		f2fs_i_size_write(inode, pos + copied);
unlock_out:
	f2fs_put_page(page, 1);
	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);
	return copied;
}

static int check_direct_IO(struct inode *inode, struct iov_iter *iter,
			   loff_t offset)
{
	unsigned blocksize_mask = inode->i_sb->s_blocksize - 1;

	if (offset & blocksize_mask)
		return -EINVAL;

	if (iov_iter_alignment(iter) & blocksize_mask)
		return -EINVAL;

	return 0;
}

static ssize_t f2fs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct address_space *mapping = iocb->ki_filp->f_mapping;
	struct inode *inode = mapping->host;
	size_t count = iov_iter_count(iter);
	loff_t offset = iocb->ki_pos;
	int rw = iov_iter_rw(iter);
	int err;

	err = check_direct_IO(inode, iter, offset);
	if (err)
		return err;

	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode))
		return 0;
	if (test_opt(F2FS_I_SB(inode), LFS))
		return 0;

	trace_f2fs_direct_IO_enter(inode, offset, count, rw);

	down_read(&F2FS_I(inode)->dio_rwsem[rw]);
	err = blockdev_direct_IO(iocb, inode, iter, get_data_block_dio);
	up_read(&F2FS_I(inode)->dio_rwsem[rw]);

	if (rw == WRITE) {
		if (err > 0)
			set_inode_flag(inode, FI_UPDATE_WRITE);
		else if (err < 0)
			f2fs_write_failed(mapping, offset + count);
	}

	trace_f2fs_direct_IO_exit(inode, offset, count, rw, err);

	return err;
}

void f2fs_invalidate_page(struct page *page, unsigned int offset,
							unsigned int length)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (inode->i_ino >= F2FS_ROOT_INO(sbi) &&
		(offset % PAGE_SIZE || length != PAGE_SIZE))
		return;

	if (PageDirty(page)) {
		if (inode->i_ino == F2FS_META_INO(sbi))
			dec_page_count(sbi, F2FS_DIRTY_META);
		else if (inode->i_ino == F2FS_NODE_INO(sbi))
			dec_page_count(sbi, F2FS_DIRTY_NODES);
		else
			inode_dec_dirty_pages(inode);
	}

	/* This is atomic written page, keep Private */
	if (IS_ATOMIC_WRITTEN_PAGE(page))
		return;

	set_page_private(page, 0);
	ClearPagePrivate(page);
}

int f2fs_release_page(struct page *page, gfp_t wait)
{
	/* If this is dirty page, keep PagePrivate */
	if (PageDirty(page))
		return 0;

	/* This is atomic written page, keep Private */
	if (IS_ATOMIC_WRITTEN_PAGE(page))
		return 0;

	set_page_private(page, 0);
	ClearPagePrivate(page);
	return 1;
}

/*
 * This was copied from __set_page_dirty_buffers which gives higher performance
 * in very high speed storages. (e.g., pmem)
 */
void f2fs_set_page_dirty_nobuffers(struct page *page)
{
	struct address_space *mapping = page->mapping;
	unsigned long flags;

	if (unlikely(!mapping))
		return;

	spin_lock(&mapping->private_lock);
	lock_page_memcg(page);
	SetPageDirty(page);
	spin_unlock(&mapping->private_lock);

	spin_lock_irqsave(&mapping->tree_lock, flags);
	WARN_ON_ONCE(!PageUptodate(page));
	account_page_dirtied(page, mapping);
	radix_tree_tag_set(&mapping->page_tree,
			page_index(page), PAGECACHE_TAG_DIRTY);
	spin_unlock_irqrestore(&mapping->tree_lock, flags);
	unlock_page_memcg(page);

	__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
	return;
}

static int f2fs_set_data_page_dirty(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;

	trace_f2fs_set_page_dirty(page, DATA);

	if (!PageUptodate(page))
		SetPageUptodate(page);

	if (f2fs_is_atomic_file(inode)) {
		if (!IS_ATOMIC_WRITTEN_PAGE(page)) {
			register_inmem_page(inode, page);
			return 1;
		}
		/*
		 * Previously, this page has been registered, we just
		 * return here.
		 */
		return 0;
	}

	if (!PageDirty(page)) {
		f2fs_set_page_dirty_nobuffers(page);
		update_dirty_page(inode, page);
		return 1;
	}
	return 0;
}

static sector_t f2fs_bmap(struct address_space *mapping, sector_t block)
{
	struct inode *inode = mapping->host;

	if (f2fs_has_inline_data(inode))
		return 0;

	/* make sure allocating whole blocks */
	if (mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		filemap_write_and_wait(mapping);

	return generic_block_bmap(mapping, block, get_data_block_bmap);
}

#ifdef CONFIG_MIGRATION
#include <linux/migrate.h>

int f2fs_migrate_page(struct address_space *mapping,
		struct page *newpage, struct page *page, enum migrate_mode mode)
{
	int rc, extra_count;
	struct f2fs_inode_info *fi = F2FS_I(mapping->host);
	bool atomic_written = IS_ATOMIC_WRITTEN_PAGE(page);

	BUG_ON(PageWriteback(page));

	/* migrating an atomic written page is safe with the inmem_lock hold */
	if (atomic_written && !mutex_trylock(&fi->inmem_lock))
		return -EAGAIN;

	/*
	 * A reference is expected if PagePrivate set when move mapping,
	 * however F2FS breaks this for maintaining dirty page counts when
	 * truncating pages. So here adjusting the 'extra_count' make it work.
	 */
	extra_count = (atomic_written ? 1 : 0) - page_has_private(page);
	rc = migrate_page_move_mapping(mapping, newpage,
				page, NULL, mode, extra_count);
	if (rc != MIGRATEPAGE_SUCCESS) {
		if (atomic_written)
			mutex_unlock(&fi->inmem_lock);
		return rc;
	}

	if (atomic_written) {
		struct inmem_pages *cur;
		list_for_each_entry(cur, &fi->inmem_pages, list)
			if (cur->page == page) {
				cur->page = newpage;
				break;
			}
		mutex_unlock(&fi->inmem_lock);
		put_page(page);
		get_page(newpage);
	}

	if (PagePrivate(page))
		SetPagePrivate(newpage);
	set_page_private(newpage, page_private(page));

	migrate_page_copy(newpage, page);

	return MIGRATEPAGE_SUCCESS;
}
#endif

const struct address_space_operations f2fs_dblock_aops = {
	.readpage	= f2fs_read_data_page,
	.readpages	= f2fs_read_data_pages,
	.writepage	= f2fs_write_data_page,
	.writepages	= f2fs_write_data_pages,
	.write_begin	= f2fs_write_begin,
	.write_end	= f2fs_write_end,
	.set_page_dirty	= f2fs_set_data_page_dirty,
	.invalidatepage	= f2fs_invalidate_page,
	.releasepage	= f2fs_release_page,
	.direct_IO	= f2fs_direct_IO,
	.bmap		= f2fs_bmap,
#ifdef CONFIG_MIGRATION
	.migratepage    = f2fs_migrate_page,
#endif
};

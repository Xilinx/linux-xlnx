/*
 * Copyright (c) 2009, Christoph Hellwig
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xfs

#if !defined(_TRACE_XFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XFS_H

#include <linux/tracepoint.h>

struct xfs_agf;
struct xfs_alloc_arg;
struct xfs_attr_list_context;
struct xfs_buf_log_item;
struct xfs_da_args;
struct xfs_da_node_entry;
struct xfs_dquot;
struct xfs_log_item;
struct xlog;
struct xlog_ticket;
struct xlog_recover;
struct xlog_recover_item;
struct xfs_buf_log_format;
struct xfs_inode_log_format;
struct xfs_bmbt_irec;
struct xfs_btree_cur;
struct xfs_refcount_irec;

DECLARE_EVENT_CLASS(xfs_attr_list_class,
	TP_PROTO(struct xfs_attr_list_context *ctx),
	TP_ARGS(ctx),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(u32, hashval)
		__field(u32, blkno)
		__field(u32, offset)
		__field(void *, alist)
		__field(int, bufsize)
		__field(int, count)
		__field(int, firstu)
		__field(int, dupcnt)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ctx->dp)->i_sb->s_dev;
		__entry->ino = ctx->dp->i_ino;
		__entry->hashval = ctx->cursor->hashval;
		__entry->blkno = ctx->cursor->blkno;
		__entry->offset = ctx->cursor->offset;
		__entry->alist = ctx->alist;
		__entry->bufsize = ctx->bufsize;
		__entry->count = ctx->count;
		__entry->firstu = ctx->firstu;
		__entry->flags = ctx->flags;
	),
	TP_printk("dev %d:%d ino 0x%llx cursor h/b/o 0x%x/0x%x/%u dupcnt %u "
		  "alist 0x%p size %u count %u firstu %u flags %d %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->ino,
		   __entry->hashval,
		   __entry->blkno,
		   __entry->offset,
		   __entry->dupcnt,
		   __entry->alist,
		   __entry->bufsize,
		   __entry->count,
		   __entry->firstu,
		   __entry->flags,
		   __print_flags(__entry->flags, "|", XFS_ATTR_FLAGS)
	)
)

#define DEFINE_ATTR_LIST_EVENT(name) \
DEFINE_EVENT(xfs_attr_list_class, name, \
	TP_PROTO(struct xfs_attr_list_context *ctx), \
	TP_ARGS(ctx))
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_sf);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_sf_all);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_leaf);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_leaf_end);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_full);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_add);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_wrong_blk);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_notfound);
DEFINE_ATTR_LIST_EVENT(xfs_attr_leaf_list);
DEFINE_ATTR_LIST_EVENT(xfs_attr_node_list);

DECLARE_EVENT_CLASS(xfs_perag_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, int refcount,
		 unsigned long caller_ip),
	TP_ARGS(mp, agno, refcount, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, refcount)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->refcount = refcount;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno %u refcount %d caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->refcount,
		  (char *)__entry->caller_ip)
);

#define DEFINE_PERAG_REF_EVENT(name)	\
DEFINE_EVENT(xfs_perag_class, name,	\
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, int refcount,	\
		 unsigned long caller_ip),					\
	TP_ARGS(mp, agno, refcount, caller_ip))
DEFINE_PERAG_REF_EVENT(xfs_perag_get);
DEFINE_PERAG_REF_EVENT(xfs_perag_get_tag);
DEFINE_PERAG_REF_EVENT(xfs_perag_put);
DEFINE_PERAG_REF_EVENT(xfs_perag_set_reclaim);
DEFINE_PERAG_REF_EVENT(xfs_perag_clear_reclaim);
DEFINE_PERAG_REF_EVENT(xfs_perag_set_eofblocks);
DEFINE_PERAG_REF_EVENT(xfs_perag_clear_eofblocks);
DEFINE_PERAG_REF_EVENT(xfs_perag_set_cowblocks);
DEFINE_PERAG_REF_EVENT(xfs_perag_clear_cowblocks);

DECLARE_EVENT_CLASS(xfs_ag_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno),
	TP_ARGS(mp, agno),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
	),
	TP_printk("dev %d:%d agno %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno)
);
#define DEFINE_AG_EVENT(name)	\
DEFINE_EVENT(xfs_ag_class, name,	\
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno),	\
	TP_ARGS(mp, agno))

DEFINE_AG_EVENT(xfs_read_agf);
DEFINE_AG_EVENT(xfs_alloc_read_agf);
DEFINE_AG_EVENT(xfs_read_agi);
DEFINE_AG_EVENT(xfs_ialloc_read_agi);

TRACE_EVENT(xfs_attr_list_node_descend,
	TP_PROTO(struct xfs_attr_list_context *ctx,
		 struct xfs_da_node_entry *btree),
	TP_ARGS(ctx, btree),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(u32, hashval)
		__field(u32, blkno)
		__field(u32, offset)
		__field(void *, alist)
		__field(int, bufsize)
		__field(int, count)
		__field(int, firstu)
		__field(int, dupcnt)
		__field(int, flags)
		__field(u32, bt_hashval)
		__field(u32, bt_before)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ctx->dp)->i_sb->s_dev;
		__entry->ino = ctx->dp->i_ino;
		__entry->hashval = ctx->cursor->hashval;
		__entry->blkno = ctx->cursor->blkno;
		__entry->offset = ctx->cursor->offset;
		__entry->alist = ctx->alist;
		__entry->bufsize = ctx->bufsize;
		__entry->count = ctx->count;
		__entry->firstu = ctx->firstu;
		__entry->flags = ctx->flags;
		__entry->bt_hashval = be32_to_cpu(btree->hashval);
		__entry->bt_before = be32_to_cpu(btree->before);
	),
	TP_printk("dev %d:%d ino 0x%llx cursor h/b/o 0x%x/0x%x/%u dupcnt %u "
		  "alist 0x%p size %u count %u firstu %u flags %d %s "
		  "node hashval %u, node before %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->ino,
		   __entry->hashval,
		   __entry->blkno,
		   __entry->offset,
		   __entry->dupcnt,
		   __entry->alist,
		   __entry->bufsize,
		   __entry->count,
		   __entry->firstu,
		   __entry->flags,
		   __print_flags(__entry->flags, "|", XFS_ATTR_FLAGS),
		   __entry->bt_hashval,
		   __entry->bt_before)
);

TRACE_EVENT(xfs_iext_insert,
	TP_PROTO(struct xfs_inode *ip, xfs_extnum_t idx,
		 struct xfs_bmbt_irec *r, int state, unsigned long caller_ip),
	TP_ARGS(ip, idx, r, state, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_extnum_t, idx)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
		__field(xfs_exntst_t, state)
		__field(int, bmap_state)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->idx = idx;
		__entry->startoff = r->br_startoff;
		__entry->startblock = r->br_startblock;
		__entry->blockcount = r->br_blockcount;
		__entry->state = r->br_state;
		__entry->bmap_state = state;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx state %s idx %ld "
		  "offset %lld block %lld count %lld flag %d caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->bmap_state, "|", XFS_BMAP_EXT_FLAGS),
		  (long)__entry->idx,
		  __entry->startoff,
		  (__int64_t)__entry->startblock,
		  __entry->blockcount,
		  __entry->state,
		  (char *)__entry->caller_ip)
);

DECLARE_EVENT_CLASS(xfs_bmap_class,
	TP_PROTO(struct xfs_inode *ip, xfs_extnum_t idx, int state,
		 unsigned long caller_ip),
	TP_ARGS(ip, idx, state, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_extnum_t, idx)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
		__field(xfs_exntst_t, state)
		__field(int, bmap_state)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		struct xfs_ifork	*ifp;
		struct xfs_bmbt_irec	r;

		ifp = xfs_iext_state_to_fork(ip, state);
		xfs_bmbt_get_all(xfs_iext_get_ext(ifp, idx), &r);
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->idx = idx;
		__entry->startoff = r.br_startoff;
		__entry->startblock = r.br_startblock;
		__entry->blockcount = r.br_blockcount;
		__entry->state = r.br_state;
		__entry->bmap_state = state;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx state %s idx %ld "
		  "offset %lld block %lld count %lld flag %d caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->bmap_state, "|", XFS_BMAP_EXT_FLAGS),
		  (long)__entry->idx,
		  __entry->startoff,
		  (__int64_t)__entry->startblock,
		  __entry->blockcount,
		  __entry->state,
		  (char *)__entry->caller_ip)
)

#define DEFINE_BMAP_EVENT(name) \
DEFINE_EVENT(xfs_bmap_class, name, \
	TP_PROTO(struct xfs_inode *ip, xfs_extnum_t idx, int state, \
		 unsigned long caller_ip), \
	TP_ARGS(ip, idx, state, caller_ip))
DEFINE_BMAP_EVENT(xfs_iext_remove);
DEFINE_BMAP_EVENT(xfs_bmap_pre_update);
DEFINE_BMAP_EVENT(xfs_bmap_post_update);
DEFINE_BMAP_EVENT(xfs_extlist);

DECLARE_EVENT_CLASS(xfs_buf_class,
	TP_PROTO(struct xfs_buf *bp, unsigned long caller_ip),
	TP_ARGS(bp, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(int, nblks)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(unsigned, flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = bp->b_bn;
		__entry->nblks = bp->b_length;
		__entry->hold = atomic_read(&bp->b_hold);
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = bp->b_sema.count;
		__entry->flags = bp->b_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d bno 0x%llx nblks 0x%x hold %d pincount %d "
		  "lock %d flags %s caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->nblks,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  (void *)__entry->caller_ip)
)

#define DEFINE_BUF_EVENT(name) \
DEFINE_EVENT(xfs_buf_class, name, \
	TP_PROTO(struct xfs_buf *bp, unsigned long caller_ip), \
	TP_ARGS(bp, caller_ip))
DEFINE_BUF_EVENT(xfs_buf_init);
DEFINE_BUF_EVENT(xfs_buf_free);
DEFINE_BUF_EVENT(xfs_buf_hold);
DEFINE_BUF_EVENT(xfs_buf_rele);
DEFINE_BUF_EVENT(xfs_buf_iodone);
DEFINE_BUF_EVENT(xfs_buf_submit);
DEFINE_BUF_EVENT(xfs_buf_submit_wait);
DEFINE_BUF_EVENT(xfs_buf_bawrite);
DEFINE_BUF_EVENT(xfs_buf_lock);
DEFINE_BUF_EVENT(xfs_buf_lock_done);
DEFINE_BUF_EVENT(xfs_buf_trylock_fail);
DEFINE_BUF_EVENT(xfs_buf_trylock);
DEFINE_BUF_EVENT(xfs_buf_unlock);
DEFINE_BUF_EVENT(xfs_buf_iowait);
DEFINE_BUF_EVENT(xfs_buf_iowait_done);
DEFINE_BUF_EVENT(xfs_buf_delwri_queue);
DEFINE_BUF_EVENT(xfs_buf_delwri_queued);
DEFINE_BUF_EVENT(xfs_buf_delwri_split);
DEFINE_BUF_EVENT(xfs_buf_get_uncached);
DEFINE_BUF_EVENT(xfs_bdstrat_shut);
DEFINE_BUF_EVENT(xfs_buf_item_relse);
DEFINE_BUF_EVENT(xfs_buf_item_iodone_async);
DEFINE_BUF_EVENT(xfs_buf_error_relse);
DEFINE_BUF_EVENT(xfs_buf_wait_buftarg);
DEFINE_BUF_EVENT(xfs_trans_read_buf_io);
DEFINE_BUF_EVENT(xfs_trans_read_buf_shut);

/* not really buffer traces, but the buf provides useful information */
DEFINE_BUF_EVENT(xfs_btree_corrupt);
DEFINE_BUF_EVENT(xfs_da_btree_corrupt);
DEFINE_BUF_EVENT(xfs_reset_dqcounts);
DEFINE_BUF_EVENT(xfs_inode_item_push);

/* pass flags explicitly */
DECLARE_EVENT_CLASS(xfs_buf_flags_class,
	TP_PROTO(struct xfs_buf *bp, unsigned flags, unsigned long caller_ip),
	TP_ARGS(bp, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(size_t, buffer_length)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(unsigned, flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = bp->b_bn;
		__entry->buffer_length = BBTOB(bp->b_length);
		__entry->flags = flags;
		__entry->hold = atomic_read(&bp->b_hold);
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = bp->b_sema.count;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d bno 0x%llx len 0x%zx hold %d pincount %d "
		  "lock %d flags %s caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->buffer_length,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  (void *)__entry->caller_ip)
)

#define DEFINE_BUF_FLAGS_EVENT(name) \
DEFINE_EVENT(xfs_buf_flags_class, name, \
	TP_PROTO(struct xfs_buf *bp, unsigned flags, unsigned long caller_ip), \
	TP_ARGS(bp, flags, caller_ip))
DEFINE_BUF_FLAGS_EVENT(xfs_buf_find);
DEFINE_BUF_FLAGS_EVENT(xfs_buf_get);
DEFINE_BUF_FLAGS_EVENT(xfs_buf_read);

TRACE_EVENT(xfs_buf_ioerror,
	TP_PROTO(struct xfs_buf *bp, int error, unsigned long caller_ip),
	TP_ARGS(bp, error, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(size_t, buffer_length)
		__field(unsigned, flags)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(int, error)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = bp->b_bn;
		__entry->buffer_length = BBTOB(bp->b_length);
		__entry->hold = atomic_read(&bp->b_hold);
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = bp->b_sema.count;
		__entry->error = error;
		__entry->flags = bp->b_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d bno 0x%llx len 0x%zx hold %d pincount %d "
		  "lock %d error %d flags %s caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->buffer_length,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __entry->error,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  (void *)__entry->caller_ip)
);

DECLARE_EVENT_CLASS(xfs_buf_item_class,
	TP_PROTO(struct xfs_buf_log_item *bip),
	TP_ARGS(bip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, buf_bno)
		__field(size_t, buf_len)
		__field(int, buf_hold)
		__field(int, buf_pincount)
		__field(int, buf_lockval)
		__field(unsigned, buf_flags)
		__field(unsigned, bli_recur)
		__field(int, bli_refcount)
		__field(unsigned, bli_flags)
		__field(void *, li_desc)
		__field(unsigned, li_flags)
	),
	TP_fast_assign(
		__entry->dev = bip->bli_buf->b_target->bt_dev;
		__entry->bli_flags = bip->bli_flags;
		__entry->bli_recur = bip->bli_recur;
		__entry->bli_refcount = atomic_read(&bip->bli_refcount);
		__entry->buf_bno = bip->bli_buf->b_bn;
		__entry->buf_len = BBTOB(bip->bli_buf->b_length);
		__entry->buf_flags = bip->bli_buf->b_flags;
		__entry->buf_hold = atomic_read(&bip->bli_buf->b_hold);
		__entry->buf_pincount = atomic_read(&bip->bli_buf->b_pin_count);
		__entry->buf_lockval = bip->bli_buf->b_sema.count;
		__entry->li_desc = bip->bli_item.li_desc;
		__entry->li_flags = bip->bli_item.li_flags;
	),
	TP_printk("dev %d:%d bno 0x%llx len 0x%zx hold %d pincount %d "
		  "lock %d flags %s recur %d refcount %d bliflags %s "
		  "lidesc 0x%p liflags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->buf_bno,
		  __entry->buf_len,
		  __entry->buf_hold,
		  __entry->buf_pincount,
		  __entry->buf_lockval,
		  __print_flags(__entry->buf_flags, "|", XFS_BUF_FLAGS),
		  __entry->bli_recur,
		  __entry->bli_refcount,
		  __print_flags(__entry->bli_flags, "|", XFS_BLI_FLAGS),
		  __entry->li_desc,
		  __print_flags(__entry->li_flags, "|", XFS_LI_FLAGS))
)

#define DEFINE_BUF_ITEM_EVENT(name) \
DEFINE_EVENT(xfs_buf_item_class, name, \
	TP_PROTO(struct xfs_buf_log_item *bip), \
	TP_ARGS(bip))
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_size);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_size_ordered);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_size_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_format);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_format_ordered);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_format_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_ordered);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_pin);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unpin);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unpin_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unlock);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unlock_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_committed);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_push);
DEFINE_BUF_ITEM_EVENT(xfs_trans_get_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_get_buf_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_getsb);
DEFINE_BUF_ITEM_EVENT(xfs_trans_getsb_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_read_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_read_buf_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_log_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_brelse);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bjoin);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bhold);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bhold_release);
DEFINE_BUF_ITEM_EVENT(xfs_trans_binval);
DEFINE_BUF_ITEM_EVENT(xfs_trans_buf_ordered);

DECLARE_EVENT_CLASS(xfs_filestream_class,
	TP_PROTO(struct xfs_inode *ip, xfs_agnumber_t agno),
	TP_ARGS(ip, agno),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_agnumber_t, agno)
		__field(int, streams)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->agno = agno;
		__entry->streams = xfs_filestream_peek_ag(ip->i_mount, agno);
	),
	TP_printk("dev %d:%d ino 0x%llx agno %u streams %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->agno,
		  __entry->streams)
)
#define DEFINE_FILESTREAM_EVENT(name) \
DEFINE_EVENT(xfs_filestream_class, name, \
	TP_PROTO(struct xfs_inode *ip, xfs_agnumber_t agno), \
	TP_ARGS(ip, agno))
DEFINE_FILESTREAM_EVENT(xfs_filestream_free);
DEFINE_FILESTREAM_EVENT(xfs_filestream_lookup);
DEFINE_FILESTREAM_EVENT(xfs_filestream_scan);

TRACE_EVENT(xfs_filestream_pick,
	TP_PROTO(struct xfs_inode *ip, xfs_agnumber_t agno,
		 xfs_extlen_t free, int nscan),
	TP_ARGS(ip, agno, free, nscan),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_agnumber_t, agno)
		__field(int, streams)
		__field(xfs_extlen_t, free)
		__field(int, nscan)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->agno = agno;
		__entry->streams = xfs_filestream_peek_ag(ip->i_mount, agno);
		__entry->free = free;
		__entry->nscan = nscan;
	),
	TP_printk("dev %d:%d ino 0x%llx agno %u streams %d free %d nscan %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->agno,
		  __entry->streams,
		  __entry->free,
		  __entry->nscan)
);

DECLARE_EVENT_CLASS(xfs_lock_class,
	TP_PROTO(struct xfs_inode *ip, unsigned lock_flags,
		 unsigned long caller_ip),
	TP_ARGS(ip,  lock_flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, lock_flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->lock_flags = lock_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx flags %s caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->lock_flags, "|", XFS_LOCK_FLAGS),
		  (void *)__entry->caller_ip)
)

#define DEFINE_LOCK_EVENT(name) \
DEFINE_EVENT(xfs_lock_class, name, \
	TP_PROTO(struct xfs_inode *ip, unsigned lock_flags, \
		 unsigned long caller_ip), \
	TP_ARGS(ip,  lock_flags, caller_ip))
DEFINE_LOCK_EVENT(xfs_ilock);
DEFINE_LOCK_EVENT(xfs_ilock_nowait);
DEFINE_LOCK_EVENT(xfs_ilock_demote);
DEFINE_LOCK_EVENT(xfs_iunlock);

DECLARE_EVENT_CLASS(xfs_inode_class,
	TP_PROTO(struct xfs_inode *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
	),
	TP_printk("dev %d:%d ino 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino)
)

#define DEFINE_INODE_EVENT(name) \
DEFINE_EVENT(xfs_inode_class, name, \
	TP_PROTO(struct xfs_inode *ip), \
	TP_ARGS(ip))
DEFINE_INODE_EVENT(xfs_iget_skip);
DEFINE_INODE_EVENT(xfs_iget_reclaim);
DEFINE_INODE_EVENT(xfs_iget_reclaim_fail);
DEFINE_INODE_EVENT(xfs_iget_hit);
DEFINE_INODE_EVENT(xfs_iget_miss);

DEFINE_INODE_EVENT(xfs_getattr);
DEFINE_INODE_EVENT(xfs_setattr);
DEFINE_INODE_EVENT(xfs_readlink);
DEFINE_INODE_EVENT(xfs_inactive_symlink);
DEFINE_INODE_EVENT(xfs_alloc_file_space);
DEFINE_INODE_EVENT(xfs_free_file_space);
DEFINE_INODE_EVENT(xfs_zero_file_space);
DEFINE_INODE_EVENT(xfs_collapse_file_space);
DEFINE_INODE_EVENT(xfs_insert_file_space);
DEFINE_INODE_EVENT(xfs_readdir);
#ifdef CONFIG_XFS_POSIX_ACL
DEFINE_INODE_EVENT(xfs_get_acl);
#endif
DEFINE_INODE_EVENT(xfs_vm_bmap);
DEFINE_INODE_EVENT(xfs_file_ioctl);
DEFINE_INODE_EVENT(xfs_file_compat_ioctl);
DEFINE_INODE_EVENT(xfs_ioctl_setattr);
DEFINE_INODE_EVENT(xfs_dir_fsync);
DEFINE_INODE_EVENT(xfs_file_fsync);
DEFINE_INODE_EVENT(xfs_destroy_inode);
DEFINE_INODE_EVENT(xfs_evict_inode);
DEFINE_INODE_EVENT(xfs_update_time);

DEFINE_INODE_EVENT(xfs_dquot_dqalloc);
DEFINE_INODE_EVENT(xfs_dquot_dqdetach);

DEFINE_INODE_EVENT(xfs_inode_set_eofblocks_tag);
DEFINE_INODE_EVENT(xfs_inode_clear_eofblocks_tag);
DEFINE_INODE_EVENT(xfs_inode_free_eofblocks_invalid);
DEFINE_INODE_EVENT(xfs_inode_set_cowblocks_tag);
DEFINE_INODE_EVENT(xfs_inode_clear_cowblocks_tag);
DEFINE_INODE_EVENT(xfs_inode_free_cowblocks_invalid);

DEFINE_INODE_EVENT(xfs_filemap_fault);
DEFINE_INODE_EVENT(xfs_filemap_pmd_fault);
DEFINE_INODE_EVENT(xfs_filemap_page_mkwrite);
DEFINE_INODE_EVENT(xfs_filemap_pfn_mkwrite);

DECLARE_EVENT_CLASS(xfs_iref_class,
	TP_PROTO(struct xfs_inode *ip, unsigned long caller_ip),
	TP_ARGS(ip, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, count)
		__field(int, pincount)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->count = atomic_read(&VFS_I(ip)->i_count);
		__entry->pincount = atomic_read(&ip->i_pincount);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx count %d pincount %d caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->count,
		  __entry->pincount,
		  (char *)__entry->caller_ip)
)

TRACE_EVENT(xfs_iomap_prealloc_size,
	TP_PROTO(struct xfs_inode *ip, xfs_fsblock_t blocks, int shift,
		 unsigned int writeio_blocks),
	TP_ARGS(ip, blocks, shift, writeio_blocks),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsblock_t, blocks)
		__field(int, shift)
		__field(unsigned int, writeio_blocks)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->blocks = blocks;
		__entry->shift = shift;
		__entry->writeio_blocks = writeio_blocks;
	),
	TP_printk("dev %d:%d ino 0x%llx prealloc blocks %llu shift %d "
		  "m_writeio_blocks %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		  __entry->blocks, __entry->shift, __entry->writeio_blocks)
)

TRACE_EVENT(xfs_irec_merge_pre,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, xfs_agino_t agino,
		 uint16_t holemask, xfs_agino_t nagino, uint16_t nholemask),
	TP_ARGS(mp, agno, agino, holemask, nagino, nholemask),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
		__field(uint16_t, holemask)
		__field(xfs_agino_t, nagino)
		__field(uint16_t, nholemask)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agino = agino;
		__entry->holemask = holemask;
		__entry->nagino = nagino;
		__entry->nholemask = holemask;
	),
	TP_printk("dev %d:%d agno %d inobt (%u:0x%x) new (%u:0x%x)",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->agno,
		  __entry->agino, __entry->holemask, __entry->nagino,
		  __entry->nholemask)
)

TRACE_EVENT(xfs_irec_merge_post,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, xfs_agino_t agino,
		 uint16_t holemask),
	TP_ARGS(mp, agno, agino, holemask),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
		__field(uint16_t, holemask)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agino = agino;
		__entry->holemask = holemask;
	),
	TP_printk("dev %d:%d agno %d inobt (%u:0x%x)", MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->agno, __entry->agino,
		  __entry->holemask)
)

#define DEFINE_IREF_EVENT(name) \
DEFINE_EVENT(xfs_iref_class, name, \
	TP_PROTO(struct xfs_inode *ip, unsigned long caller_ip), \
	TP_ARGS(ip, caller_ip))
DEFINE_IREF_EVENT(xfs_ihold);
DEFINE_IREF_EVENT(xfs_irele);
DEFINE_IREF_EVENT(xfs_inode_pin);
DEFINE_IREF_EVENT(xfs_inode_unpin);
DEFINE_IREF_EVENT(xfs_inode_unpin_nowait);

DECLARE_EVENT_CLASS(xfs_namespace_class,
	TP_PROTO(struct xfs_inode *dp, struct xfs_name *name),
	TP_ARGS(dp, name),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, dp_ino)
		__field(int, namelen)
		__dynamic_array(char, name, name->len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(dp)->i_sb->s_dev;
		__entry->dp_ino = dp->i_ino;
		__entry->namelen = name->len;
		memcpy(__get_str(name), name->name, name->len);
	),
	TP_printk("dev %d:%d dp ino 0x%llx name %.*s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dp_ino,
		  __entry->namelen,
		  __get_str(name))
)

#define DEFINE_NAMESPACE_EVENT(name) \
DEFINE_EVENT(xfs_namespace_class, name, \
	TP_PROTO(struct xfs_inode *dp, struct xfs_name *name), \
	TP_ARGS(dp, name))
DEFINE_NAMESPACE_EVENT(xfs_remove);
DEFINE_NAMESPACE_EVENT(xfs_link);
DEFINE_NAMESPACE_EVENT(xfs_lookup);
DEFINE_NAMESPACE_EVENT(xfs_create);
DEFINE_NAMESPACE_EVENT(xfs_symlink);

TRACE_EVENT(xfs_rename,
	TP_PROTO(struct xfs_inode *src_dp, struct xfs_inode *target_dp,
		 struct xfs_name *src_name, struct xfs_name *target_name),
	TP_ARGS(src_dp, target_dp, src_name, target_name),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, src_dp_ino)
		__field(xfs_ino_t, target_dp_ino)
		__field(int, src_namelen)
		__field(int, target_namelen)
		__dynamic_array(char, src_name, src_name->len)
		__dynamic_array(char, target_name, target_name->len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(src_dp)->i_sb->s_dev;
		__entry->src_dp_ino = src_dp->i_ino;
		__entry->target_dp_ino = target_dp->i_ino;
		__entry->src_namelen = src_name->len;
		__entry->target_namelen = target_name->len;
		memcpy(__get_str(src_name), src_name->name, src_name->len);
		memcpy(__get_str(target_name), target_name->name,
			target_name->len);
	),
	TP_printk("dev %d:%d src dp ino 0x%llx target dp ino 0x%llx"
		  " src name %.*s target name %.*s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->src_dp_ino,
		  __entry->target_dp_ino,
		  __entry->src_namelen,
		  __get_str(src_name),
		  __entry->target_namelen,
		  __get_str(target_name))
)

DECLARE_EVENT_CLASS(xfs_dquot_class,
	TP_PROTO(struct xfs_dquot *dqp),
	TP_ARGS(dqp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u32, id)
		__field(unsigned, flags)
		__field(unsigned, nrefs)
		__field(unsigned long long, res_bcount)
		__field(unsigned long long, bcount)
		__field(unsigned long long, icount)
		__field(unsigned long long, blk_hardlimit)
		__field(unsigned long long, blk_softlimit)
		__field(unsigned long long, ino_hardlimit)
		__field(unsigned long long, ino_softlimit)
	), \
	TP_fast_assign(
		__entry->dev = dqp->q_mount->m_super->s_dev;
		__entry->id = be32_to_cpu(dqp->q_core.d_id);
		__entry->flags = dqp->dq_flags;
		__entry->nrefs = dqp->q_nrefs;
		__entry->res_bcount = dqp->q_res_bcount;
		__entry->bcount = be64_to_cpu(dqp->q_core.d_bcount);
		__entry->icount = be64_to_cpu(dqp->q_core.d_icount);
		__entry->blk_hardlimit =
			be64_to_cpu(dqp->q_core.d_blk_hardlimit);
		__entry->blk_softlimit =
			be64_to_cpu(dqp->q_core.d_blk_softlimit);
		__entry->ino_hardlimit =
			be64_to_cpu(dqp->q_core.d_ino_hardlimit);
		__entry->ino_softlimit =
			be64_to_cpu(dqp->q_core.d_ino_softlimit);
	),
	TP_printk("dev %d:%d id 0x%x flags %s nrefs %u res_bc 0x%llx "
		  "bcnt 0x%llx bhardlimit 0x%llx bsoftlimit 0x%llx "
		  "icnt 0x%llx ihardlimit 0x%llx isoftlimit 0x%llx]",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->id,
		  __print_flags(__entry->flags, "|", XFS_DQ_FLAGS),
		  __entry->nrefs,
		  __entry->res_bcount,
		  __entry->bcount,
		  __entry->blk_hardlimit,
		  __entry->blk_softlimit,
		  __entry->icount,
		  __entry->ino_hardlimit,
		  __entry->ino_softlimit)
)

#define DEFINE_DQUOT_EVENT(name) \
DEFINE_EVENT(xfs_dquot_class, name, \
	TP_PROTO(struct xfs_dquot *dqp), \
	TP_ARGS(dqp))
DEFINE_DQUOT_EVENT(xfs_dqadjust);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_want);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_dirty);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_busy);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_done);
DEFINE_DQUOT_EVENT(xfs_dqattach_found);
DEFINE_DQUOT_EVENT(xfs_dqattach_get);
DEFINE_DQUOT_EVENT(xfs_dqalloc);
DEFINE_DQUOT_EVENT(xfs_dqtobp_read);
DEFINE_DQUOT_EVENT(xfs_dqread);
DEFINE_DQUOT_EVENT(xfs_dqread_fail);
DEFINE_DQUOT_EVENT(xfs_dqget_hit);
DEFINE_DQUOT_EVENT(xfs_dqget_miss);
DEFINE_DQUOT_EVENT(xfs_dqget_freeing);
DEFINE_DQUOT_EVENT(xfs_dqget_dup);
DEFINE_DQUOT_EVENT(xfs_dqput);
DEFINE_DQUOT_EVENT(xfs_dqput_wait);
DEFINE_DQUOT_EVENT(xfs_dqput_free);
DEFINE_DQUOT_EVENT(xfs_dqrele);
DEFINE_DQUOT_EVENT(xfs_dqflush);
DEFINE_DQUOT_EVENT(xfs_dqflush_force);
DEFINE_DQUOT_EVENT(xfs_dqflush_done);

DECLARE_EVENT_CLASS(xfs_loggrant_class,
	TP_PROTO(struct xlog *log, struct xlog_ticket *tic),
	TP_ARGS(log, tic),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(char, ocnt)
		__field(char, cnt)
		__field(int, curr_res)
		__field(int, unit_res)
		__field(unsigned int, flags)
		__field(int, reserveq)
		__field(int, writeq)
		__field(int, grant_reserve_cycle)
		__field(int, grant_reserve_bytes)
		__field(int, grant_write_cycle)
		__field(int, grant_write_bytes)
		__field(int, curr_cycle)
		__field(int, curr_block)
		__field(xfs_lsn_t, tail_lsn)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->ocnt = tic->t_ocnt;
		__entry->cnt = tic->t_cnt;
		__entry->curr_res = tic->t_curr_res;
		__entry->unit_res = tic->t_unit_res;
		__entry->flags = tic->t_flags;
		__entry->reserveq = list_empty(&log->l_reserve_head.waiters);
		__entry->writeq = list_empty(&log->l_write_head.waiters);
		xlog_crack_grant_head(&log->l_reserve_head.grant,
				&__entry->grant_reserve_cycle,
				&__entry->grant_reserve_bytes);
		xlog_crack_grant_head(&log->l_write_head.grant,
				&__entry->grant_write_cycle,
				&__entry->grant_write_bytes);
		__entry->curr_cycle = log->l_curr_cycle;
		__entry->curr_block = log->l_curr_block;
		__entry->tail_lsn = atomic64_read(&log->l_tail_lsn);
	),
	TP_printk("dev %d:%d t_ocnt %u t_cnt %u t_curr_res %u "
		  "t_unit_res %u t_flags %s reserveq %s "
		  "writeq %s grant_reserve_cycle %d "
		  "grant_reserve_bytes %d grant_write_cycle %d "
		  "grant_write_bytes %d curr_cycle %d curr_block %d "
		  "tail_cycle %d tail_block %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ocnt,
		  __entry->cnt,
		  __entry->curr_res,
		  __entry->unit_res,
		  __print_flags(__entry->flags, "|", XLOG_TIC_FLAGS),
		  __entry->reserveq ? "empty" : "active",
		  __entry->writeq ? "empty" : "active",
		  __entry->grant_reserve_cycle,
		  __entry->grant_reserve_bytes,
		  __entry->grant_write_cycle,
		  __entry->grant_write_bytes,
		  __entry->curr_cycle,
		  __entry->curr_block,
		  CYCLE_LSN(__entry->tail_lsn),
		  BLOCK_LSN(__entry->tail_lsn)
	)
)

#define DEFINE_LOGGRANT_EVENT(name) \
DEFINE_EVENT(xfs_loggrant_class, name, \
	TP_PROTO(struct xlog *log, struct xlog_ticket *tic), \
	TP_ARGS(log, tic))
DEFINE_LOGGRANT_EVENT(xfs_log_done_nonperm);
DEFINE_LOGGRANT_EVENT(xfs_log_done_perm);
DEFINE_LOGGRANT_EVENT(xfs_log_umount_write);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_sleep);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_wake);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_wake_up);
DEFINE_LOGGRANT_EVENT(xfs_log_reserve);
DEFINE_LOGGRANT_EVENT(xfs_log_reserve_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_reserve_enter);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_reserve_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_reserve_sub);
DEFINE_LOGGRANT_EVENT(xfs_log_ungrant_enter);
DEFINE_LOGGRANT_EVENT(xfs_log_ungrant_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_ungrant_sub);

DECLARE_EVENT_CLASS(xfs_log_item_class,
	TP_PROTO(struct xfs_log_item *lip),
	TP_ARGS(lip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(void *, lip)
		__field(uint, type)
		__field(uint, flags)
		__field(xfs_lsn_t, lsn)
	),
	TP_fast_assign(
		__entry->dev = lip->li_mountp->m_super->s_dev;
		__entry->lip = lip;
		__entry->type = lip->li_type;
		__entry->flags = lip->li_flags;
		__entry->lsn = lip->li_lsn;
	),
	TP_printk("dev %d:%d lip 0x%p lsn %d/%d type %s flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lip,
		  CYCLE_LSN(__entry->lsn), BLOCK_LSN(__entry->lsn),
		  __print_symbolic(__entry->type, XFS_LI_TYPE_DESC),
		  __print_flags(__entry->flags, "|", XFS_LI_FLAGS))
)

TRACE_EVENT(xfs_log_force,
	TP_PROTO(struct xfs_mount *mp, xfs_lsn_t lsn, unsigned long caller_ip),
	TP_ARGS(mp, lsn, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_lsn_t, lsn)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->lsn = lsn;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d lsn 0x%llx caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lsn, (void *)__entry->caller_ip)
)

#define DEFINE_LOG_ITEM_EVENT(name) \
DEFINE_EVENT(xfs_log_item_class, name, \
	TP_PROTO(struct xfs_log_item *lip), \
	TP_ARGS(lip))
DEFINE_LOG_ITEM_EVENT(xfs_ail_push);
DEFINE_LOG_ITEM_EVENT(xfs_ail_pinned);
DEFINE_LOG_ITEM_EVENT(xfs_ail_locked);
DEFINE_LOG_ITEM_EVENT(xfs_ail_flushing);

DECLARE_EVENT_CLASS(xfs_ail_class,
	TP_PROTO(struct xfs_log_item *lip, xfs_lsn_t old_lsn, xfs_lsn_t new_lsn),
	TP_ARGS(lip, old_lsn, new_lsn),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(void *, lip)
		__field(uint, type)
		__field(uint, flags)
		__field(xfs_lsn_t, old_lsn)
		__field(xfs_lsn_t, new_lsn)
	),
	TP_fast_assign(
		__entry->dev = lip->li_mountp->m_super->s_dev;
		__entry->lip = lip;
		__entry->type = lip->li_type;
		__entry->flags = lip->li_flags;
		__entry->old_lsn = old_lsn;
		__entry->new_lsn = new_lsn;
	),
	TP_printk("dev %d:%d lip 0x%p old lsn %d/%d new lsn %d/%d type %s flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lip,
		  CYCLE_LSN(__entry->old_lsn), BLOCK_LSN(__entry->old_lsn),
		  CYCLE_LSN(__entry->new_lsn), BLOCK_LSN(__entry->new_lsn),
		  __print_symbolic(__entry->type, XFS_LI_TYPE_DESC),
		  __print_flags(__entry->flags, "|", XFS_LI_FLAGS))
)

#define DEFINE_AIL_EVENT(name) \
DEFINE_EVENT(xfs_ail_class, name, \
	TP_PROTO(struct xfs_log_item *lip, xfs_lsn_t old_lsn, xfs_lsn_t new_lsn), \
	TP_ARGS(lip, old_lsn, new_lsn))
DEFINE_AIL_EVENT(xfs_ail_insert);
DEFINE_AIL_EVENT(xfs_ail_move);
DEFINE_AIL_EVENT(xfs_ail_delete);

TRACE_EVENT(xfs_log_assign_tail_lsn,
	TP_PROTO(struct xlog *log, xfs_lsn_t new_lsn),
	TP_ARGS(log, new_lsn),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_lsn_t, new_lsn)
		__field(xfs_lsn_t, old_lsn)
		__field(xfs_lsn_t, last_sync_lsn)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->new_lsn = new_lsn;
		__entry->old_lsn = atomic64_read(&log->l_tail_lsn);
		__entry->last_sync_lsn = atomic64_read(&log->l_last_sync_lsn);
	),
	TP_printk("dev %d:%d new tail lsn %d/%d, old lsn %d/%d, last sync %d/%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  CYCLE_LSN(__entry->new_lsn), BLOCK_LSN(__entry->new_lsn),
		  CYCLE_LSN(__entry->old_lsn), BLOCK_LSN(__entry->old_lsn),
		  CYCLE_LSN(__entry->last_sync_lsn), BLOCK_LSN(__entry->last_sync_lsn))
)

DECLARE_EVENT_CLASS(xfs_file_class,
	TP_PROTO(struct xfs_inode *ip, size_t count, loff_t offset),
	TP_ARGS(ip, count, offset),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(loff_t, offset)
		__field(size_t, count)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->offset = offset;
		__entry->count = count;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx offset 0x%llx count 0x%zx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->offset,
		  __entry->count)
)

#define DEFINE_RW_EVENT(name)		\
DEFINE_EVENT(xfs_file_class, name,	\
	TP_PROTO(struct xfs_inode *ip, size_t count, loff_t offset),	\
	TP_ARGS(ip, count, offset))
DEFINE_RW_EVENT(xfs_file_buffered_read);
DEFINE_RW_EVENT(xfs_file_direct_read);
DEFINE_RW_EVENT(xfs_file_dax_read);
DEFINE_RW_EVENT(xfs_file_buffered_write);
DEFINE_RW_EVENT(xfs_file_direct_write);
DEFINE_RW_EVENT(xfs_file_dax_write);

DECLARE_EVENT_CLASS(xfs_page_class,
	TP_PROTO(struct inode *inode, struct page *page, unsigned long off,
		 unsigned int len),
	TP_ARGS(inode, page, off, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(pgoff_t, pgoff)
		__field(loff_t, size)
		__field(unsigned long, offset)
		__field(unsigned int, length)
		__field(int, delalloc)
		__field(int, unwritten)
	),
	TP_fast_assign(
		int delalloc = -1, unwritten = -1;

		if (page_has_buffers(page))
			xfs_count_page_state(page, &delalloc, &unwritten);
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = XFS_I(inode)->i_ino;
		__entry->pgoff = page_offset(page);
		__entry->size = i_size_read(inode);
		__entry->offset = off;
		__entry->length = len;
		__entry->delalloc = delalloc;
		__entry->unwritten = unwritten;
	),
	TP_printk("dev %d:%d ino 0x%llx pgoff 0x%lx size 0x%llx offset %lx "
		  "length %x delalloc %d unwritten %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->pgoff,
		  __entry->size,
		  __entry->offset,
		  __entry->length,
		  __entry->delalloc,
		  __entry->unwritten)
)

#define DEFINE_PAGE_EVENT(name)		\
DEFINE_EVENT(xfs_page_class, name,	\
	TP_PROTO(struct inode *inode, struct page *page, unsigned long off, \
		 unsigned int len),	\
	TP_ARGS(inode, page, off, len))
DEFINE_PAGE_EVENT(xfs_writepage);
DEFINE_PAGE_EVENT(xfs_releasepage);
DEFINE_PAGE_EVENT(xfs_invalidatepage);

DECLARE_EVENT_CLASS(xfs_readpage_class,
	TP_PROTO(struct inode *inode, int nr_pages),
	TP_ARGS(inode, nr_pages),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, nr_pages)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->nr_pages = nr_pages;
	),
	TP_printk("dev %d:%d ino 0x%llx nr_pages %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->nr_pages)
)

#define DEFINE_READPAGE_EVENT(name)		\
DEFINE_EVENT(xfs_readpage_class, name,	\
	TP_PROTO(struct inode *inode, int nr_pages), \
	TP_ARGS(inode, nr_pages))
DEFINE_READPAGE_EVENT(xfs_vm_readpage);
DEFINE_READPAGE_EVENT(xfs_vm_readpages);

DECLARE_EVENT_CLASS(xfs_imap_class,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count,
		 int type, struct xfs_bmbt_irec *irec),
	TP_ARGS(ip, offset, count, type, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(loff_t, size)
		__field(loff_t, offset)
		__field(size_t, count)
		__field(int, type)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->offset = offset;
		__entry->count = count;
		__entry->type = type;
		__entry->startoff = irec ? irec->br_startoff : 0;
		__entry->startblock = irec ? irec->br_startblock : 0;
		__entry->blockcount = irec ? irec->br_blockcount : 0;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx offset 0x%llx count %zd "
		  "type %s startoff 0x%llx startblock %lld blockcount 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->offset,
		  __entry->count,
		  __print_symbolic(__entry->type, XFS_IO_TYPES),
		  __entry->startoff,
		  (__int64_t)__entry->startblock,
		  __entry->blockcount)
)

#define DEFINE_IOMAP_EVENT(name)	\
DEFINE_EVENT(xfs_imap_class, name,	\
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count,	\
		 int type, struct xfs_bmbt_irec *irec),		\
	TP_ARGS(ip, offset, count, type, irec))
DEFINE_IOMAP_EVENT(xfs_map_blocks_found);
DEFINE_IOMAP_EVENT(xfs_map_blocks_alloc);
DEFINE_IOMAP_EVENT(xfs_get_blocks_found);
DEFINE_IOMAP_EVENT(xfs_get_blocks_alloc);
DEFINE_IOMAP_EVENT(xfs_get_blocks_map_direct);
DEFINE_IOMAP_EVENT(xfs_iomap_alloc);
DEFINE_IOMAP_EVENT(xfs_iomap_found);

DECLARE_EVENT_CLASS(xfs_simple_io_class,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count),
	TP_ARGS(ip, offset, count),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(loff_t, isize)
		__field(loff_t, disize)
		__field(loff_t, offset)
		__field(size_t, count)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->isize = VFS_I(ip)->i_size;
		__entry->disize = ip->i_d.di_size;
		__entry->offset = offset;
		__entry->count = count;
	),
	TP_printk("dev %d:%d ino 0x%llx isize 0x%llx disize 0x%llx "
		  "offset 0x%llx count %zd",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->isize,
		  __entry->disize,
		  __entry->offset,
		  __entry->count)
);

#define DEFINE_SIMPLE_IO_EVENT(name)	\
DEFINE_EVENT(xfs_simple_io_class, name,	\
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count),	\
	TP_ARGS(ip, offset, count))
DEFINE_SIMPLE_IO_EVENT(xfs_delalloc_enospc);
DEFINE_SIMPLE_IO_EVENT(xfs_unwritten_convert);
DEFINE_SIMPLE_IO_EVENT(xfs_get_blocks_notfound);
DEFINE_SIMPLE_IO_EVENT(xfs_setfilesize);
DEFINE_SIMPLE_IO_EVENT(xfs_zero_eof);
DEFINE_SIMPLE_IO_EVENT(xfs_end_io_direct_write);
DEFINE_SIMPLE_IO_EVENT(xfs_end_io_direct_write_unwritten);
DEFINE_SIMPLE_IO_EVENT(xfs_end_io_direct_write_append);

DECLARE_EVENT_CLASS(xfs_itrunc_class,
	TP_PROTO(struct xfs_inode *ip, xfs_fsize_t new_size),
	TP_ARGS(ip, new_size),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_fsize_t, new_size)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->new_size = new_size;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx new_size 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->new_size)
)

#define DEFINE_ITRUNC_EVENT(name) \
DEFINE_EVENT(xfs_itrunc_class, name, \
	TP_PROTO(struct xfs_inode *ip, xfs_fsize_t new_size), \
	TP_ARGS(ip, new_size))
DEFINE_ITRUNC_EVENT(xfs_itruncate_extents_start);
DEFINE_ITRUNC_EVENT(xfs_itruncate_extents_end);

TRACE_EVENT(xfs_pagecache_inval,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t start, xfs_off_t finish),
	TP_ARGS(ip, start, finish),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_off_t, start)
		__field(xfs_off_t, finish)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->start = start;
		__entry->finish = finish;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx start 0x%llx finish 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->start,
		  __entry->finish)
);

TRACE_EVENT(xfs_bunmap,
	TP_PROTO(struct xfs_inode *ip, xfs_fileoff_t bno, xfs_filblks_t len,
		 int flags, unsigned long caller_ip),
	TP_ARGS(ip, bno, len, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_fileoff_t, bno)
		__field(xfs_filblks_t, len)
		__field(unsigned long, caller_ip)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->bno = bno;
		__entry->len = len;
		__entry->caller_ip = caller_ip;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx bno 0x%llx len 0x%llx"
		  "flags %s caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->bno,
		  __entry->len,
		  __print_flags(__entry->flags, "|", XFS_BMAPI_FLAGS),
		  (void *)__entry->caller_ip)

);

DECLARE_EVENT_CLASS(xfs_extent_busy_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len),
	TP_ARGS(mp, agno, agbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len)
);
#define DEFINE_BUSY_EVENT(name) \
DEFINE_EVENT(xfs_extent_busy_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_extlen_t len), \
	TP_ARGS(mp, agno, agbno, len))
DEFINE_BUSY_EVENT(xfs_extent_busy);
DEFINE_BUSY_EVENT(xfs_extent_busy_enomem);
DEFINE_BUSY_EVENT(xfs_extent_busy_force);
DEFINE_BUSY_EVENT(xfs_extent_busy_reuse);
DEFINE_BUSY_EVENT(xfs_extent_busy_clear);

TRACE_EVENT(xfs_extent_busy_trim,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len,
		 xfs_agblock_t tbno, xfs_extlen_t tlen),
	TP_ARGS(mp, agno, agbno, len, tbno, tlen),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(xfs_agblock_t, tbno)
		__field(xfs_extlen_t, tlen)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->tbno = tbno;
		__entry->tlen = tlen;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u tbno %u tlen %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->tbno,
		  __entry->tlen)
);

TRACE_EVENT(xfs_trans_commit_lsn,
	TP_PROTO(struct xfs_trans *trans),
	TP_ARGS(trans),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(struct xfs_trans *, tp)
		__field(xfs_lsn_t, lsn)
	),
	TP_fast_assign(
		__entry->dev = trans->t_mountp->m_super->s_dev;
		__entry->tp = trans;
		__entry->lsn = trans->t_commit_lsn;
	),
	TP_printk("dev %d:%d trans 0x%p commit_lsn 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tp,
		  __entry->lsn)
);

TRACE_EVENT(xfs_agf,
	TP_PROTO(struct xfs_mount *mp, struct xfs_agf *agf, int flags,
		 unsigned long caller_ip),
	TP_ARGS(mp, agf, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, flags)
		__field(__u32, length)
		__field(__u32, bno_root)
		__field(__u32, cnt_root)
		__field(__u32, bno_level)
		__field(__u32, cnt_level)
		__field(__u32, flfirst)
		__field(__u32, fllast)
		__field(__u32, flcount)
		__field(__u32, freeblks)
		__field(__u32, longest)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = be32_to_cpu(agf->agf_seqno),
		__entry->flags = flags;
		__entry->length = be32_to_cpu(agf->agf_length),
		__entry->bno_root = be32_to_cpu(agf->agf_roots[XFS_BTNUM_BNO]),
		__entry->cnt_root = be32_to_cpu(agf->agf_roots[XFS_BTNUM_CNT]),
		__entry->bno_level =
				be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]),
		__entry->cnt_level =
				be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]),
		__entry->flfirst = be32_to_cpu(agf->agf_flfirst),
		__entry->fllast = be32_to_cpu(agf->agf_fllast),
		__entry->flcount = be32_to_cpu(agf->agf_flcount),
		__entry->freeblks = be32_to_cpu(agf->agf_freeblks),
		__entry->longest = be32_to_cpu(agf->agf_longest);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno %u flags %s length %u roots b %u c %u "
		  "levels b %u c %u flfirst %u fllast %u flcount %u "
		  "freeblks %u longest %u caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __print_flags(__entry->flags, "|", XFS_AGF_FLAGS),
		  __entry->length,
		  __entry->bno_root,
		  __entry->cnt_root,
		  __entry->bno_level,
		  __entry->cnt_level,
		  __entry->flfirst,
		  __entry->fllast,
		  __entry->flcount,
		  __entry->freeblks,
		  __entry->longest,
		  (void *)__entry->caller_ip)
);

TRACE_EVENT(xfs_free_extent,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, xfs_agblock_t agbno,
		 xfs_extlen_t len, enum xfs_ag_resv_type resv, int haveleft,
		 int haveright),
	TP_ARGS(mp, agno, agbno, len, resv, haveleft, haveright),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(int, resv)
		__field(int, haveleft)
		__field(int, haveright)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->resv = resv;
		__entry->haveleft = haveleft;
		__entry->haveright = haveright;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u resv %d %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->resv,
		  __entry->haveleft ?
			(__entry->haveright ? "both" : "left") :
			(__entry->haveright ? "right" : "none"))

);

DECLARE_EVENT_CLASS(xfs_alloc_class,
	TP_PROTO(struct xfs_alloc_arg *args),
	TP_ARGS(args),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, minlen)
		__field(xfs_extlen_t, maxlen)
		__field(xfs_extlen_t, mod)
		__field(xfs_extlen_t, prod)
		__field(xfs_extlen_t, minleft)
		__field(xfs_extlen_t, total)
		__field(xfs_extlen_t, alignment)
		__field(xfs_extlen_t, minalignslop)
		__field(xfs_extlen_t, len)
		__field(short, type)
		__field(short, otype)
		__field(char, wasdel)
		__field(char, wasfromfl)
		__field(int, resv)
		__field(int, datatype)
		__field(xfs_fsblock_t, firstblock)
	),
	TP_fast_assign(
		__entry->dev = args->mp->m_super->s_dev;
		__entry->agno = args->agno;
		__entry->agbno = args->agbno;
		__entry->minlen = args->minlen;
		__entry->maxlen = args->maxlen;
		__entry->mod = args->mod;
		__entry->prod = args->prod;
		__entry->minleft = args->minleft;
		__entry->total = args->total;
		__entry->alignment = args->alignment;
		__entry->minalignslop = args->minalignslop;
		__entry->len = args->len;
		__entry->type = args->type;
		__entry->otype = args->otype;
		__entry->wasdel = args->wasdel;
		__entry->wasfromfl = args->wasfromfl;
		__entry->resv = args->resv;
		__entry->datatype = args->datatype;
		__entry->firstblock = args->firstblock;
	),
	TP_printk("dev %d:%d agno %u agbno %u minlen %u maxlen %u mod %u "
		  "prod %u minleft %u total %u alignment %u minalignslop %u "
		  "len %u type %s otype %s wasdel %d wasfromfl %d resv %d "
		  "datatype 0x%x firstblock 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->minlen,
		  __entry->maxlen,
		  __entry->mod,
		  __entry->prod,
		  __entry->minleft,
		  __entry->total,
		  __entry->alignment,
		  __entry->minalignslop,
		  __entry->len,
		  __print_symbolic(__entry->type, XFS_ALLOC_TYPES),
		  __print_symbolic(__entry->otype, XFS_ALLOC_TYPES),
		  __entry->wasdel,
		  __entry->wasfromfl,
		  __entry->resv,
		  __entry->datatype,
		  (unsigned long long)__entry->firstblock)
)

#define DEFINE_ALLOC_EVENT(name) \
DEFINE_EVENT(xfs_alloc_class, name, \
	TP_PROTO(struct xfs_alloc_arg *args), \
	TP_ARGS(args))
DEFINE_ALLOC_EVENT(xfs_alloc_exact_done);
DEFINE_ALLOC_EVENT(xfs_alloc_exact_notfound);
DEFINE_ALLOC_EVENT(xfs_alloc_exact_error);
DEFINE_ALLOC_EVENT(xfs_alloc_near_nominleft);
DEFINE_ALLOC_EVENT(xfs_alloc_near_first);
DEFINE_ALLOC_EVENT(xfs_alloc_near_greater);
DEFINE_ALLOC_EVENT(xfs_alloc_near_lesser);
DEFINE_ALLOC_EVENT(xfs_alloc_near_error);
DEFINE_ALLOC_EVENT(xfs_alloc_near_noentry);
DEFINE_ALLOC_EVENT(xfs_alloc_near_busy);
DEFINE_ALLOC_EVENT(xfs_alloc_size_neither);
DEFINE_ALLOC_EVENT(xfs_alloc_size_noentry);
DEFINE_ALLOC_EVENT(xfs_alloc_size_nominleft);
DEFINE_ALLOC_EVENT(xfs_alloc_size_done);
DEFINE_ALLOC_EVENT(xfs_alloc_size_error);
DEFINE_ALLOC_EVENT(xfs_alloc_size_busy);
DEFINE_ALLOC_EVENT(xfs_alloc_small_freelist);
DEFINE_ALLOC_EVENT(xfs_alloc_small_notenough);
DEFINE_ALLOC_EVENT(xfs_alloc_small_done);
DEFINE_ALLOC_EVENT(xfs_alloc_small_error);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_badargs);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_nofix);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_noagbp);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_loopfailed);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_allfailed);

DECLARE_EVENT_CLASS(xfs_da_class,
	TP_PROTO(struct xfs_da_args *args),
	TP_ARGS(args),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__dynamic_array(char, name, args->namelen)
		__field(int, namelen)
		__field(xfs_dahash_t, hashval)
		__field(xfs_ino_t, inumber)
		__field(int, op_flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		if (args->namelen)
			memcpy(__get_str(name), args->name, args->namelen);
		__entry->namelen = args->namelen;
		__entry->hashval = args->hashval;
		__entry->inumber = args->inumber;
		__entry->op_flags = args->op_flags;
	),
	TP_printk("dev %d:%d ino 0x%llx name %.*s namelen %d hashval 0x%x "
		  "inumber 0x%llx op_flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->namelen,
		  __entry->namelen ? __get_str(name) : NULL,
		  __entry->namelen,
		  __entry->hashval,
		  __entry->inumber,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS))
)

#define DEFINE_DIR2_EVENT(name) \
DEFINE_EVENT(xfs_da_class, name, \
	TP_PROTO(struct xfs_da_args *args), \
	TP_ARGS(args))
DEFINE_DIR2_EVENT(xfs_dir2_sf_addname);
DEFINE_DIR2_EVENT(xfs_dir2_sf_create);
DEFINE_DIR2_EVENT(xfs_dir2_sf_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_sf_replace);
DEFINE_DIR2_EVENT(xfs_dir2_sf_removename);
DEFINE_DIR2_EVENT(xfs_dir2_sf_toino4);
DEFINE_DIR2_EVENT(xfs_dir2_sf_toino8);
DEFINE_DIR2_EVENT(xfs_dir2_sf_to_block);
DEFINE_DIR2_EVENT(xfs_dir2_block_addname);
DEFINE_DIR2_EVENT(xfs_dir2_block_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_block_replace);
DEFINE_DIR2_EVENT(xfs_dir2_block_removename);
DEFINE_DIR2_EVENT(xfs_dir2_block_to_sf);
DEFINE_DIR2_EVENT(xfs_dir2_block_to_leaf);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_addname);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_replace);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_removename);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_to_block);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_to_node);
DEFINE_DIR2_EVENT(xfs_dir2_node_addname);
DEFINE_DIR2_EVENT(xfs_dir2_node_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_node_replace);
DEFINE_DIR2_EVENT(xfs_dir2_node_removename);
DEFINE_DIR2_EVENT(xfs_dir2_node_to_leaf);

DECLARE_EVENT_CLASS(xfs_attr_class,
	TP_PROTO(struct xfs_da_args *args),
	TP_ARGS(args),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__dynamic_array(char, name, args->namelen)
		__field(int, namelen)
		__field(int, valuelen)
		__field(xfs_dahash_t, hashval)
		__field(int, op_flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		if (args->namelen)
			memcpy(__get_str(name), args->name, args->namelen);
		__entry->namelen = args->namelen;
		__entry->valuelen = args->valuelen;
		__entry->hashval = args->hashval;
		__entry->op_flags = args->op_flags;
	),
	TP_printk("dev %d:%d ino 0x%llx name %.*s namelen %d valuelen %d "
		  "hashval 0x%x op_flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->namelen,
		  __entry->namelen ? __get_str(name) : NULL,
		  __entry->namelen,
		  __entry->valuelen,
		  __entry->hashval,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS))
)

#define DEFINE_ATTR_EVENT(name) \
DEFINE_EVENT(xfs_attr_class, name, \
	TP_PROTO(struct xfs_da_args *args), \
	TP_ARGS(args))
DEFINE_ATTR_EVENT(xfs_attr_sf_add);
DEFINE_ATTR_EVENT(xfs_attr_sf_addname);
DEFINE_ATTR_EVENT(xfs_attr_sf_create);
DEFINE_ATTR_EVENT(xfs_attr_sf_lookup);
DEFINE_ATTR_EVENT(xfs_attr_sf_remove);
DEFINE_ATTR_EVENT(xfs_attr_sf_removename);
DEFINE_ATTR_EVENT(xfs_attr_sf_to_leaf);

DEFINE_ATTR_EVENT(xfs_attr_leaf_add);
DEFINE_ATTR_EVENT(xfs_attr_leaf_add_old);
DEFINE_ATTR_EVENT(xfs_attr_leaf_add_new);
DEFINE_ATTR_EVENT(xfs_attr_leaf_add_work);
DEFINE_ATTR_EVENT(xfs_attr_leaf_addname);
DEFINE_ATTR_EVENT(xfs_attr_leaf_create);
DEFINE_ATTR_EVENT(xfs_attr_leaf_compact);
DEFINE_ATTR_EVENT(xfs_attr_leaf_get);
DEFINE_ATTR_EVENT(xfs_attr_leaf_lookup);
DEFINE_ATTR_EVENT(xfs_attr_leaf_replace);
DEFINE_ATTR_EVENT(xfs_attr_leaf_remove);
DEFINE_ATTR_EVENT(xfs_attr_leaf_removename);
DEFINE_ATTR_EVENT(xfs_attr_leaf_split);
DEFINE_ATTR_EVENT(xfs_attr_leaf_split_before);
DEFINE_ATTR_EVENT(xfs_attr_leaf_split_after);
DEFINE_ATTR_EVENT(xfs_attr_leaf_clearflag);
DEFINE_ATTR_EVENT(xfs_attr_leaf_setflag);
DEFINE_ATTR_EVENT(xfs_attr_leaf_flipflags);
DEFINE_ATTR_EVENT(xfs_attr_leaf_to_sf);
DEFINE_ATTR_EVENT(xfs_attr_leaf_to_node);
DEFINE_ATTR_EVENT(xfs_attr_leaf_rebalance);
DEFINE_ATTR_EVENT(xfs_attr_leaf_unbalance);
DEFINE_ATTR_EVENT(xfs_attr_leaf_toosmall);

DEFINE_ATTR_EVENT(xfs_attr_node_addname);
DEFINE_ATTR_EVENT(xfs_attr_node_get);
DEFINE_ATTR_EVENT(xfs_attr_node_lookup);
DEFINE_ATTR_EVENT(xfs_attr_node_replace);
DEFINE_ATTR_EVENT(xfs_attr_node_removename);

DEFINE_ATTR_EVENT(xfs_attr_fillstate);
DEFINE_ATTR_EVENT(xfs_attr_refillstate);

DEFINE_ATTR_EVENT(xfs_attr_rmtval_get);
DEFINE_ATTR_EVENT(xfs_attr_rmtval_set);
DEFINE_ATTR_EVENT(xfs_attr_rmtval_remove);

#define DEFINE_DA_EVENT(name) \
DEFINE_EVENT(xfs_da_class, name, \
	TP_PROTO(struct xfs_da_args *args), \
	TP_ARGS(args))
DEFINE_DA_EVENT(xfs_da_split);
DEFINE_DA_EVENT(xfs_da_join);
DEFINE_DA_EVENT(xfs_da_link_before);
DEFINE_DA_EVENT(xfs_da_link_after);
DEFINE_DA_EVENT(xfs_da_unlink_back);
DEFINE_DA_EVENT(xfs_da_unlink_forward);
DEFINE_DA_EVENT(xfs_da_root_split);
DEFINE_DA_EVENT(xfs_da_root_join);
DEFINE_DA_EVENT(xfs_da_node_add);
DEFINE_DA_EVENT(xfs_da_node_create);
DEFINE_DA_EVENT(xfs_da_node_split);
DEFINE_DA_EVENT(xfs_da_node_remove);
DEFINE_DA_EVENT(xfs_da_node_rebalance);
DEFINE_DA_EVENT(xfs_da_node_unbalance);
DEFINE_DA_EVENT(xfs_da_node_toosmall);
DEFINE_DA_EVENT(xfs_da_swap_lastblock);
DEFINE_DA_EVENT(xfs_da_grow_inode);
DEFINE_DA_EVENT(xfs_da_shrink_inode);
DEFINE_DA_EVENT(xfs_da_fixhashpath);
DEFINE_DA_EVENT(xfs_da_path_shift);

DECLARE_EVENT_CLASS(xfs_dir2_space_class,
	TP_PROTO(struct xfs_da_args *args, int idx),
	TP_ARGS(args, idx),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, op_flags)
		__field(int, idx)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		__entry->op_flags = args->op_flags;
		__entry->idx = idx;
	),
	TP_printk("dev %d:%d ino 0x%llx op_flags %s index %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS),
		  __entry->idx)
)

#define DEFINE_DIR2_SPACE_EVENT(name) \
DEFINE_EVENT(xfs_dir2_space_class, name, \
	TP_PROTO(struct xfs_da_args *args, int idx), \
	TP_ARGS(args, idx))
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_leafn_add);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_leafn_remove);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_grow_inode);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_shrink_inode);

TRACE_EVENT(xfs_dir2_leafn_moveents,
	TP_PROTO(struct xfs_da_args *args, int src_idx, int dst_idx, int count),
	TP_ARGS(args, src_idx, dst_idx, count),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, op_flags)
		__field(int, src_idx)
		__field(int, dst_idx)
		__field(int, count)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		__entry->op_flags = args->op_flags;
		__entry->src_idx = src_idx;
		__entry->dst_idx = dst_idx;
		__entry->count = count;
	),
	TP_printk("dev %d:%d ino 0x%llx op_flags %s "
		  "src_idx %d dst_idx %d count %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS),
		  __entry->src_idx,
		  __entry->dst_idx,
		  __entry->count)
);

#define XFS_SWAPEXT_INODES \
	{ 0,	"target" }, \
	{ 1,	"temp" }

#define XFS_INODE_FORMAT_STR \
	{ 0,	"invalid" }, \
	{ 1,	"local" }, \
	{ 2,	"extent" }, \
	{ 3,	"btree" }

DECLARE_EVENT_CLASS(xfs_swap_extent_class,
	TP_PROTO(struct xfs_inode *ip, int which),
	TP_ARGS(ip, which),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, which)
		__field(xfs_ino_t, ino)
		__field(int, format)
		__field(int, nex)
		__field(int, broot_size)
		__field(int, fork_off)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->which = which;
		__entry->ino = ip->i_ino;
		__entry->format = ip->i_d.di_format;
		__entry->nex = ip->i_d.di_nextents;
		__entry->broot_size = ip->i_df.if_broot_bytes;
		__entry->fork_off = XFS_IFORK_BOFF(ip);
	),
	TP_printk("dev %d:%d ino 0x%llx (%s), %s format, num_extents %d, "
		  "broot size %d, fork offset %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->which, XFS_SWAPEXT_INODES),
		  __print_symbolic(__entry->format, XFS_INODE_FORMAT_STR),
		  __entry->nex,
		  __entry->broot_size,
		  __entry->fork_off)
)

#define DEFINE_SWAPEXT_EVENT(name) \
DEFINE_EVENT(xfs_swap_extent_class, name, \
	TP_PROTO(struct xfs_inode *ip, int which), \
	TP_ARGS(ip, which))

DEFINE_SWAPEXT_EVENT(xfs_swap_extent_before);
DEFINE_SWAPEXT_EVENT(xfs_swap_extent_after);

TRACE_EVENT(xfs_log_recover_record,
	TP_PROTO(struct xlog *log, struct xlog_rec_header *rhead, int pass),
	TP_ARGS(log, rhead, pass),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_lsn_t, lsn)
		__field(int, len)
		__field(int, num_logops)
		__field(int, pass)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->lsn = be64_to_cpu(rhead->h_lsn);
		__entry->len = be32_to_cpu(rhead->h_len);
		__entry->num_logops = be32_to_cpu(rhead->h_num_logops);
		__entry->pass = pass;
	),
	TP_printk("dev %d:%d lsn 0x%llx len 0x%x num_logops 0x%x pass %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lsn, __entry->len, __entry->num_logops,
		   __entry->pass)
)

DECLARE_EVENT_CLASS(xfs_log_recover_item_class,
	TP_PROTO(struct xlog *log, struct xlog_recover *trans,
		struct xlog_recover_item *item, int pass),
	TP_ARGS(log, trans, item, pass),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, item)
		__field(xlog_tid_t, tid)
		__field(xfs_lsn_t, lsn)
		__field(int, type)
		__field(int, pass)
		__field(int, count)
		__field(int, total)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->item = (unsigned long)item;
		__entry->tid = trans->r_log_tid;
		__entry->lsn = trans->r_lsn;
		__entry->type = ITEM_TYPE(item);
		__entry->pass = pass;
		__entry->count = item->ri_cnt;
		__entry->total = item->ri_total;
	),
	TP_printk("dev %d:%d tid 0x%x lsn 0x%llx, pass %d, item 0x%p, "
		  "item type %s item region count/total %d/%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tid,
		  __entry->lsn,
		  __entry->pass,
		  (void *)__entry->item,
		  __print_symbolic(__entry->type, XFS_LI_TYPE_DESC),
		  __entry->count,
		  __entry->total)
)

#define DEFINE_LOG_RECOVER_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_item_class, name, \
	TP_PROTO(struct xlog *log, struct xlog_recover *trans, \
		struct xlog_recover_item *item, int pass), \
	TP_ARGS(log, trans, item, pass))

DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_add);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_add_cont);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_reorder_head);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_reorder_tail);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_recover);

DECLARE_EVENT_CLASS(xfs_log_recover_buf_item_class,
	TP_PROTO(struct xlog *log, struct xfs_buf_log_format *buf_f),
	TP_ARGS(log, buf_f),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(__int64_t, blkno)
		__field(unsigned short, len)
		__field(unsigned short, flags)
		__field(unsigned short, size)
		__field(unsigned int, map_size)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->blkno = buf_f->blf_blkno;
		__entry->len = buf_f->blf_len;
		__entry->flags = buf_f->blf_flags;
		__entry->size = buf_f->blf_size;
		__entry->map_size = buf_f->blf_map_size;
	),
	TP_printk("dev %d:%d blkno 0x%llx, len %u, flags 0x%x, size %d, "
			"map_size %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->blkno,
		  __entry->len,
		  __entry->flags,
		  __entry->size,
		  __entry->map_size)
)

#define DEFINE_LOG_RECOVER_BUF_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_buf_item_class, name, \
	TP_PROTO(struct xlog *log, struct xfs_buf_log_format *buf_f), \
	TP_ARGS(log, buf_f))

DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_not_cancel);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel_add);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel_ref_inc);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_recover);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_skip);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_inode_buf);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_reg_buf);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_dquot_buf);

DECLARE_EVENT_CLASS(xfs_log_recover_ino_item_class,
	TP_PROTO(struct xlog *log, struct xfs_inode_log_format *in_f),
	TP_ARGS(log, in_f),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned short, size)
		__field(int, fields)
		__field(unsigned short, asize)
		__field(unsigned short, dsize)
		__field(__int64_t, blkno)
		__field(int, len)
		__field(int, boffset)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->ino = in_f->ilf_ino;
		__entry->size = in_f->ilf_size;
		__entry->fields = in_f->ilf_fields;
		__entry->asize = in_f->ilf_asize;
		__entry->dsize = in_f->ilf_dsize;
		__entry->blkno = in_f->ilf_blkno;
		__entry->len = in_f->ilf_len;
		__entry->boffset = in_f->ilf_boffset;
	),
	TP_printk("dev %d:%d ino 0x%llx, size %u, fields 0x%x, asize %d, "
			"dsize %d, blkno 0x%llx, len %d, boffset %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->fields,
		  __entry->asize,
		  __entry->dsize,
		  __entry->blkno,
		  __entry->len,
		  __entry->boffset)
)
#define DEFINE_LOG_RECOVER_INO_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_ino_item_class, name, \
	TP_PROTO(struct xlog *log, struct xfs_inode_log_format *in_f), \
	TP_ARGS(log, in_f))

DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_recover);
DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_cancel);
DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_skip);

DECLARE_EVENT_CLASS(xfs_log_recover_icreate_item_class,
	TP_PROTO(struct xlog *log, struct xfs_icreate_log *in_f),
	TP_ARGS(log, in_f),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(unsigned int, count)
		__field(unsigned int, isize)
		__field(xfs_agblock_t, length)
		__field(unsigned int, gen)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->agno = be32_to_cpu(in_f->icl_ag);
		__entry->agbno = be32_to_cpu(in_f->icl_agbno);
		__entry->count = be32_to_cpu(in_f->icl_count);
		__entry->isize = be32_to_cpu(in_f->icl_isize);
		__entry->length = be32_to_cpu(in_f->icl_length);
		__entry->gen = be32_to_cpu(in_f->icl_gen);
	),
	TP_printk("dev %d:%d agno %u agbno %u count %u isize %u length %u "
		  "gen %u", MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno, __entry->agbno, __entry->count, __entry->isize,
		  __entry->length, __entry->gen)
)
#define DEFINE_LOG_RECOVER_ICREATE_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_icreate_item_class, name, \
	TP_PROTO(struct xlog *log, struct xfs_icreate_log *in_f), \
	TP_ARGS(log, in_f))

DEFINE_LOG_RECOVER_ICREATE_ITEM(xfs_log_recover_icreate_cancel);
DEFINE_LOG_RECOVER_ICREATE_ITEM(xfs_log_recover_icreate_recover);

DECLARE_EVENT_CLASS(xfs_discard_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len),
	TP_ARGS(mp, agno, agbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len)
)

#define DEFINE_DISCARD_EVENT(name) \
DEFINE_EVENT(xfs_discard_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_extlen_t len), \
	TP_ARGS(mp, agno, agbno, len))
DEFINE_DISCARD_EVENT(xfs_discard_extent);
DEFINE_DISCARD_EVENT(xfs_discard_toosmall);
DEFINE_DISCARD_EVENT(xfs_discard_exclude);
DEFINE_DISCARD_EVENT(xfs_discard_busy);

/* btree cursor events */
DECLARE_EVENT_CLASS(xfs_btree_cur_class,
	TP_PROTO(struct xfs_btree_cur *cur, int level, struct xfs_buf *bp),
	TP_ARGS(cur, level, bp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_btnum_t, btnum)
		__field(int, level)
		__field(int, nlevels)
		__field(int, ptr)
		__field(xfs_daddr_t, daddr)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->btnum = cur->bc_btnum;
		__entry->level = level;
		__entry->nlevels = cur->bc_nlevels;
		__entry->ptr = cur->bc_ptrs[level];
		__entry->daddr = bp ? bp->b_bn : -1;
	),
	TP_printk("dev %d:%d btnum %d level %d/%d ptr %d daddr 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->btnum,
		  __entry->level,
		  __entry->nlevels,
		  __entry->ptr,
		  (unsigned long long)__entry->daddr)
)

#define DEFINE_BTREE_CUR_EVENT(name) \
DEFINE_EVENT(xfs_btree_cur_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, int level, struct xfs_buf *bp), \
	TP_ARGS(cur, level, bp))
DEFINE_BTREE_CUR_EVENT(xfs_btree_updkeys);
DEFINE_BTREE_CUR_EVENT(xfs_btree_overlapped_query_range);

/* deferred ops */
struct xfs_defer_pending;
struct xfs_defer_intake;
struct xfs_defer_ops;

DECLARE_EVENT_CLASS(xfs_defer_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_ops *dop),
	TP_ARGS(mp, dop),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(void *, dop)
		__field(bool, committed)
		__field(bool, low)
	),
	TP_fast_assign(
		__entry->dev = mp ? mp->m_super->s_dev : 0;
		__entry->dop = dop;
		__entry->committed = dop->dop_committed;
		__entry->low = dop->dop_low;
	),
	TP_printk("dev %d:%d ops %p committed %d low %d\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dop,
		  __entry->committed,
		  __entry->low)
)
#define DEFINE_DEFER_EVENT(name) \
DEFINE_EVENT(xfs_defer_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_ops *dop), \
	TP_ARGS(mp, dop))

DECLARE_EVENT_CLASS(xfs_defer_error_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_ops *dop, int error),
	TP_ARGS(mp, dop, error),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(void *, dop)
		__field(bool, committed)
		__field(bool, low)
		__field(int, error)
	),
	TP_fast_assign(
		__entry->dev = mp ? mp->m_super->s_dev : 0;
		__entry->dop = dop;
		__entry->committed = dop->dop_committed;
		__entry->low = dop->dop_low;
		__entry->error = error;
	),
	TP_printk("dev %d:%d ops %p committed %d low %d err %d\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dop,
		  __entry->committed,
		  __entry->low,
		  __entry->error)
)
#define DEFINE_DEFER_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_defer_error_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_ops *dop, int error), \
	TP_ARGS(mp, dop, error))

DECLARE_EVENT_CLASS(xfs_defer_pending_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_pending *dfp),
	TP_ARGS(mp, dfp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, type)
		__field(void *, intent)
		__field(bool, committed)
		__field(int, nr)
	),
	TP_fast_assign(
		__entry->dev = mp ? mp->m_super->s_dev : 0;
		__entry->type = dfp->dfp_type->type;
		__entry->intent = dfp->dfp_intent;
		__entry->committed = dfp->dfp_done != NULL;
		__entry->nr = dfp->dfp_count;
	),
	TP_printk("dev %d:%d optype %d intent %p committed %d nr %d\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->intent,
		  __entry->committed,
		  __entry->nr)
)
#define DEFINE_DEFER_PENDING_EVENT(name) \
DEFINE_EVENT(xfs_defer_pending_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_pending *dfp), \
	TP_ARGS(mp, dfp))

DECLARE_EVENT_CLASS(xfs_phys_extent_deferred_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 int type, xfs_agblock_t agbno, xfs_extlen_t len),
	TP_ARGS(mp, agno, type, agbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, type)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->type = type;
		__entry->agbno = agbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d op %d agno %u agbno %u len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->agno,
		  __entry->agbno,
		  __entry->len)
);
#define DEFINE_PHYS_EXTENT_DEFERRED_EVENT(name) \
DEFINE_EVENT(xfs_phys_extent_deferred_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 int type, \
		 xfs_agblock_t bno, \
		 xfs_extlen_t len), \
	TP_ARGS(mp, agno, type, bno, len))

DECLARE_EVENT_CLASS(xfs_map_extent_deferred_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 int op,
		 xfs_agblock_t agbno,
		 xfs_ino_t ino,
		 int whichfork,
		 xfs_fileoff_t offset,
		 xfs_filblks_t len,
		 xfs_exntst_t state),
	TP_ARGS(mp, agno, op, agbno, ino, whichfork, offset, len, state),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_ino_t, ino)
		__field(xfs_agblock_t, agbno)
		__field(int, whichfork)
		__field(xfs_fileoff_t, l_loff)
		__field(xfs_filblks_t, l_len)
		__field(xfs_exntst_t, l_state)
		__field(int, op)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->ino = ino;
		__entry->agbno = agbno;
		__entry->whichfork = whichfork;
		__entry->l_loff = offset;
		__entry->l_len = len;
		__entry->l_state = state;
		__entry->op = op;
	),
	TP_printk("dev %d:%d op %d agno %u agbno %u owner %lld %s offset %llu len %llu state %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->op,
		  __entry->agno,
		  __entry->agbno,
		  __entry->ino,
		  __entry->whichfork == XFS_ATTR_FORK ? "attr" : "data",
		  __entry->l_loff,
		  __entry->l_len,
		  __entry->l_state)
);
#define DEFINE_MAP_EXTENT_DEFERRED_EVENT(name) \
DEFINE_EVENT(xfs_map_extent_deferred_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 int op, \
		 xfs_agblock_t agbno, \
		 xfs_ino_t ino, \
		 int whichfork, \
		 xfs_fileoff_t offset, \
		 xfs_filblks_t len, \
		 xfs_exntst_t state), \
	TP_ARGS(mp, agno, op, agbno, ino, whichfork, offset, len, state))

DEFINE_DEFER_EVENT(xfs_defer_init);
DEFINE_DEFER_EVENT(xfs_defer_cancel);
DEFINE_DEFER_EVENT(xfs_defer_trans_roll);
DEFINE_DEFER_EVENT(xfs_defer_trans_abort);
DEFINE_DEFER_EVENT(xfs_defer_finish);
DEFINE_DEFER_EVENT(xfs_defer_finish_done);

DEFINE_DEFER_ERROR_EVENT(xfs_defer_trans_roll_error);
DEFINE_DEFER_ERROR_EVENT(xfs_defer_finish_error);
DEFINE_DEFER_ERROR_EVENT(xfs_defer_op_finish_error);

DEFINE_DEFER_PENDING_EVENT(xfs_defer_intake_work);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_intake_cancel);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_pending_commit);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_pending_cancel);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_pending_finish);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_pending_abort);

#define DEFINE_BMAP_FREE_DEFERRED_EVENT DEFINE_PHYS_EXTENT_DEFERRED_EVENT
DEFINE_BMAP_FREE_DEFERRED_EVENT(xfs_bmap_free_defer);
DEFINE_BMAP_FREE_DEFERRED_EVENT(xfs_bmap_free_deferred);

/* rmap tracepoints */
DECLARE_EVENT_CLASS(xfs_rmap_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len, bool unwritten,
		 struct xfs_owner_info *oinfo),
	TP_ARGS(mp, agno, agbno, len, unwritten, oinfo),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(unsigned long, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->owner = oinfo->oi_owner;
		__entry->offset = oinfo->oi_offset;
		__entry->flags = oinfo->oi_flags;
		if (unwritten)
			__entry->flags |= XFS_RMAP_UNWRITTEN;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u owner %lld offset %llu flags 0x%lx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);
#define DEFINE_RMAP_EVENT(name) \
DEFINE_EVENT(xfs_rmap_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_extlen_t len, bool unwritten, \
		 struct xfs_owner_info *oinfo), \
	TP_ARGS(mp, agno, agbno, len, unwritten, oinfo))

/* simple AG-based error/%ip tracepoint class */
DECLARE_EVENT_CLASS(xfs_ag_error_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, int error,
		 unsigned long caller_ip),
	TP_ARGS(mp, agno, error, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, error)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->error = error;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno %u error %d caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->error,
		  (char *)__entry->caller_ip)
);

#define DEFINE_AG_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_ag_error_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, int error, \
		 unsigned long caller_ip), \
	TP_ARGS(mp, agno, error, caller_ip))

DEFINE_RMAP_EVENT(xfs_rmap_unmap);
DEFINE_RMAP_EVENT(xfs_rmap_unmap_done);
DEFINE_AG_ERROR_EVENT(xfs_rmap_unmap_error);
DEFINE_RMAP_EVENT(xfs_rmap_map);
DEFINE_RMAP_EVENT(xfs_rmap_map_done);
DEFINE_AG_ERROR_EVENT(xfs_rmap_map_error);
DEFINE_RMAP_EVENT(xfs_rmap_convert);
DEFINE_RMAP_EVENT(xfs_rmap_convert_done);
DEFINE_AG_ERROR_EVENT(xfs_rmap_convert_error);
DEFINE_AG_ERROR_EVENT(xfs_rmap_convert_state);

DECLARE_EVENT_CLASS(xfs_rmapbt_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len,
		 uint64_t owner, uint64_t offset, unsigned int flags),
	TP_ARGS(mp, agno, agbno, len, owner, offset, flags),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->owner = owner;
		__entry->offset = offset;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u owner %lld offset %llu flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);
#define DEFINE_RMAPBT_EVENT(name) \
DEFINE_EVENT(xfs_rmapbt_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_extlen_t len, \
		 uint64_t owner, uint64_t offset, unsigned int flags), \
	TP_ARGS(mp, agno, agbno, len, owner, offset, flags))

#define DEFINE_RMAP_DEFERRED_EVENT DEFINE_MAP_EXTENT_DEFERRED_EVENT
DEFINE_RMAP_DEFERRED_EVENT(xfs_rmap_defer);
DEFINE_RMAP_DEFERRED_EVENT(xfs_rmap_deferred);

DEFINE_BUSY_EVENT(xfs_rmapbt_alloc_block);
DEFINE_BUSY_EVENT(xfs_rmapbt_free_block);
DEFINE_RMAPBT_EVENT(xfs_rmap_update);
DEFINE_RMAPBT_EVENT(xfs_rmap_insert);
DEFINE_RMAPBT_EVENT(xfs_rmap_delete);
DEFINE_AG_ERROR_EVENT(xfs_rmap_insert_error);
DEFINE_AG_ERROR_EVENT(xfs_rmap_delete_error);
DEFINE_AG_ERROR_EVENT(xfs_rmap_update_error);

DEFINE_RMAPBT_EVENT(xfs_rmap_find_left_neighbor_candidate);
DEFINE_RMAPBT_EVENT(xfs_rmap_find_left_neighbor_query);
DEFINE_RMAPBT_EVENT(xfs_rmap_lookup_le_range_candidate);
DEFINE_RMAPBT_EVENT(xfs_rmap_lookup_le_range);
DEFINE_RMAPBT_EVENT(xfs_rmap_lookup_le_range_result);
DEFINE_RMAPBT_EVENT(xfs_rmap_find_right_neighbor_result);
DEFINE_RMAPBT_EVENT(xfs_rmap_find_left_neighbor_result);

/* deferred bmbt updates */
#define DEFINE_BMAP_DEFERRED_EVENT	DEFINE_RMAP_DEFERRED_EVENT
DEFINE_BMAP_DEFERRED_EVENT(xfs_bmap_defer);
DEFINE_BMAP_DEFERRED_EVENT(xfs_bmap_deferred);

/* per-AG reservation */
DECLARE_EVENT_CLASS(xfs_ag_resv_class,
	TP_PROTO(struct xfs_perag *pag, enum xfs_ag_resv_type resv,
		 xfs_extlen_t len),
	TP_ARGS(pag, resv, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, resv)
		__field(xfs_extlen_t, freeblks)
		__field(xfs_extlen_t, flcount)
		__field(xfs_extlen_t, reserved)
		__field(xfs_extlen_t, asked)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		struct xfs_ag_resv	*r = xfs_perag_resv(pag, resv);

		__entry->dev = pag->pag_mount->m_super->s_dev;
		__entry->agno = pag->pag_agno;
		__entry->resv = resv;
		__entry->freeblks = pag->pagf_freeblks;
		__entry->flcount = pag->pagf_flcount;
		__entry->reserved = r ? r->ar_reserved : 0;
		__entry->asked = r ? r->ar_asked : 0;
		__entry->len = len;
	),
	TP_printk("dev %d:%d agno %u resv %d freeblks %u flcount %u resv %u ask %u len %u\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->resv,
		  __entry->freeblks,
		  __entry->flcount,
		  __entry->reserved,
		  __entry->asked,
		  __entry->len)
)
#define DEFINE_AG_RESV_EVENT(name) \
DEFINE_EVENT(xfs_ag_resv_class, name, \
	TP_PROTO(struct xfs_perag *pag, enum xfs_ag_resv_type type, \
		 xfs_extlen_t len), \
	TP_ARGS(pag, type, len))

/* per-AG reservation tracepoints */
DEFINE_AG_RESV_EVENT(xfs_ag_resv_init);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_free);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_alloc_extent);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_free_extent);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_critical);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_needed);

DEFINE_AG_ERROR_EVENT(xfs_ag_resv_free_error);
DEFINE_AG_ERROR_EVENT(xfs_ag_resv_init_error);

/* refcount tracepoint classes */

/* reuse the discard trace class for agbno/aglen-based traces */
#define DEFINE_AG_EXTENT_EVENT(name) DEFINE_DISCARD_EVENT(name)

/* ag btree lookup tracepoint class */
#define XFS_AG_BTREE_CMP_FORMAT_STR \
	{ XFS_LOOKUP_EQ,	"eq" }, \
	{ XFS_LOOKUP_LE,	"le" }, \
	{ XFS_LOOKUP_GE,	"ge" }
DECLARE_EVENT_CLASS(xfs_ag_btree_lookup_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_lookup_t dir),
	TP_ARGS(mp, agno, agbno, dir),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_lookup_t, dir)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->dir = dir;
	),
	TP_printk("dev %d:%d agno %u agbno %u cmp %s(%d)\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __print_symbolic(__entry->dir, XFS_AG_BTREE_CMP_FORMAT_STR),
		  __entry->dir)
)

#define DEFINE_AG_BTREE_LOOKUP_EVENT(name) \
DEFINE_EVENT(xfs_ag_btree_lookup_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_lookup_t dir), \
	TP_ARGS(mp, agno, agbno, dir))

/* single-rcext tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_extent_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 struct xfs_refcount_irec *irec),
	TP_ARGS(mp, agno, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, startblock)
		__field(xfs_extlen_t, blockcount)
		__field(xfs_nlink_t, refcount)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->startblock = irec->rc_startblock;
		__entry->blockcount = irec->rc_blockcount;
		__entry->refcount = irec->rc_refcount;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u refcount %u\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->refcount)
)

#define DEFINE_REFCOUNT_EXTENT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_extent_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 struct xfs_refcount_irec *irec), \
	TP_ARGS(mp, agno, irec))

/* single-rcext and an agbno tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_extent_at_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 struct xfs_refcount_irec *irec, xfs_agblock_t agbno),
	TP_ARGS(mp, agno, irec, agbno),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, startblock)
		__field(xfs_extlen_t, blockcount)
		__field(xfs_nlink_t, refcount)
		__field(xfs_agblock_t, agbno)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->startblock = irec->rc_startblock;
		__entry->blockcount = irec->rc_blockcount;
		__entry->refcount = irec->rc_refcount;
		__entry->agbno = agbno;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u refcount %u @ agbno %u\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->refcount,
		  __entry->agbno)
)

#define DEFINE_REFCOUNT_EXTENT_AT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_extent_at_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 struct xfs_refcount_irec *irec, xfs_agblock_t agbno), \
	TP_ARGS(mp, agno, irec, agbno))

/* double-rcext tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_double_extent_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 struct xfs_refcount_irec *i1, struct xfs_refcount_irec *i2),
	TP_ARGS(mp, agno, i1, i2),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, i1_startblock)
		__field(xfs_extlen_t, i1_blockcount)
		__field(xfs_nlink_t, i1_refcount)
		__field(xfs_agblock_t, i2_startblock)
		__field(xfs_extlen_t, i2_blockcount)
		__field(xfs_nlink_t, i2_refcount)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->i1_startblock = i1->rc_startblock;
		__entry->i1_blockcount = i1->rc_blockcount;
		__entry->i1_refcount = i1->rc_refcount;
		__entry->i2_startblock = i2->rc_startblock;
		__entry->i2_blockcount = i2->rc_blockcount;
		__entry->i2_refcount = i2->rc_refcount;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u refcount %u -- "
		  "agbno %u len %u refcount %u\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->i1_startblock,
		  __entry->i1_blockcount,
		  __entry->i1_refcount,
		  __entry->i2_startblock,
		  __entry->i2_blockcount,
		  __entry->i2_refcount)
)

#define DEFINE_REFCOUNT_DOUBLE_EXTENT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_double_extent_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 struct xfs_refcount_irec *i1, struct xfs_refcount_irec *i2), \
	TP_ARGS(mp, agno, i1, i2))

/* double-rcext and an agbno tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_double_extent_at_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 struct xfs_refcount_irec *i1, struct xfs_refcount_irec *i2,
		 xfs_agblock_t agbno),
	TP_ARGS(mp, agno, i1, i2, agbno),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, i1_startblock)
		__field(xfs_extlen_t, i1_blockcount)
		__field(xfs_nlink_t, i1_refcount)
		__field(xfs_agblock_t, i2_startblock)
		__field(xfs_extlen_t, i2_blockcount)
		__field(xfs_nlink_t, i2_refcount)
		__field(xfs_agblock_t, agbno)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->i1_startblock = i1->rc_startblock;
		__entry->i1_blockcount = i1->rc_blockcount;
		__entry->i1_refcount = i1->rc_refcount;
		__entry->i2_startblock = i2->rc_startblock;
		__entry->i2_blockcount = i2->rc_blockcount;
		__entry->i2_refcount = i2->rc_refcount;
		__entry->agbno = agbno;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u refcount %u -- "
		  "agbno %u len %u refcount %u @ agbno %u\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->i1_startblock,
		  __entry->i1_blockcount,
		  __entry->i1_refcount,
		  __entry->i2_startblock,
		  __entry->i2_blockcount,
		  __entry->i2_refcount,
		  __entry->agbno)
)

#define DEFINE_REFCOUNT_DOUBLE_EXTENT_AT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_double_extent_at_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 struct xfs_refcount_irec *i1, struct xfs_refcount_irec *i2, \
		 xfs_agblock_t agbno), \
	TP_ARGS(mp, agno, i1, i2, agbno))

/* triple-rcext tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_triple_extent_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 struct xfs_refcount_irec *i1, struct xfs_refcount_irec *i2,
		 struct xfs_refcount_irec *i3),
	TP_ARGS(mp, agno, i1, i2, i3),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, i1_startblock)
		__field(xfs_extlen_t, i1_blockcount)
		__field(xfs_nlink_t, i1_refcount)
		__field(xfs_agblock_t, i2_startblock)
		__field(xfs_extlen_t, i2_blockcount)
		__field(xfs_nlink_t, i2_refcount)
		__field(xfs_agblock_t, i3_startblock)
		__field(xfs_extlen_t, i3_blockcount)
		__field(xfs_nlink_t, i3_refcount)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->i1_startblock = i1->rc_startblock;
		__entry->i1_blockcount = i1->rc_blockcount;
		__entry->i1_refcount = i1->rc_refcount;
		__entry->i2_startblock = i2->rc_startblock;
		__entry->i2_blockcount = i2->rc_blockcount;
		__entry->i2_refcount = i2->rc_refcount;
		__entry->i3_startblock = i3->rc_startblock;
		__entry->i3_blockcount = i3->rc_blockcount;
		__entry->i3_refcount = i3->rc_refcount;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u refcount %u -- "
		  "agbno %u len %u refcount %u -- "
		  "agbno %u len %u refcount %u\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->i1_startblock,
		  __entry->i1_blockcount,
		  __entry->i1_refcount,
		  __entry->i2_startblock,
		  __entry->i2_blockcount,
		  __entry->i2_refcount,
		  __entry->i3_startblock,
		  __entry->i3_blockcount,
		  __entry->i3_refcount)
);

#define DEFINE_REFCOUNT_TRIPLE_EXTENT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_triple_extent_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 struct xfs_refcount_irec *i1, struct xfs_refcount_irec *i2, \
		 struct xfs_refcount_irec *i3), \
	TP_ARGS(mp, agno, i1, i2, i3))

/* refcount btree tracepoints */
DEFINE_BUSY_EVENT(xfs_refcountbt_alloc_block);
DEFINE_BUSY_EVENT(xfs_refcountbt_free_block);
DEFINE_AG_BTREE_LOOKUP_EVENT(xfs_refcount_lookup);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_get);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_update);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_insert);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_delete);
DEFINE_AG_ERROR_EVENT(xfs_refcount_insert_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_delete_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_update_error);

/* refcount adjustment tracepoints */
DEFINE_AG_EXTENT_EVENT(xfs_refcount_increase);
DEFINE_AG_EXTENT_EVENT(xfs_refcount_decrease);
DEFINE_AG_EXTENT_EVENT(xfs_refcount_cow_increase);
DEFINE_AG_EXTENT_EVENT(xfs_refcount_cow_decrease);
DEFINE_REFCOUNT_TRIPLE_EXTENT_EVENT(xfs_refcount_merge_center_extents);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_modify_extent);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_recover_extent);
DEFINE_REFCOUNT_EXTENT_AT_EVENT(xfs_refcount_split_extent);
DEFINE_REFCOUNT_DOUBLE_EXTENT_EVENT(xfs_refcount_merge_left_extent);
DEFINE_REFCOUNT_DOUBLE_EXTENT_EVENT(xfs_refcount_merge_right_extent);
DEFINE_REFCOUNT_DOUBLE_EXTENT_AT_EVENT(xfs_refcount_find_left_extent);
DEFINE_REFCOUNT_DOUBLE_EXTENT_AT_EVENT(xfs_refcount_find_right_extent);
DEFINE_AG_ERROR_EVENT(xfs_refcount_adjust_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_adjust_cow_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_merge_center_extents_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_modify_extent_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_split_extent_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_merge_left_extent_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_merge_right_extent_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_find_left_extent_error);
DEFINE_AG_ERROR_EVENT(xfs_refcount_find_right_extent_error);

/* reflink helpers */
DEFINE_AG_EXTENT_EVENT(xfs_refcount_find_shared);
DEFINE_AG_EXTENT_EVENT(xfs_refcount_find_shared_result);
DEFINE_AG_ERROR_EVENT(xfs_refcount_find_shared_error);
#define DEFINE_REFCOUNT_DEFERRED_EVENT DEFINE_PHYS_EXTENT_DEFERRED_EVENT
DEFINE_REFCOUNT_DEFERRED_EVENT(xfs_refcount_defer);
DEFINE_REFCOUNT_DEFERRED_EVENT(xfs_refcount_deferred);

TRACE_EVENT(xfs_refcount_finish_one_leftover,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 int type, xfs_agblock_t agbno, xfs_extlen_t len,
		 xfs_agblock_t new_agbno, xfs_extlen_t new_len),
	TP_ARGS(mp, agno, type, agbno, len, new_agbno, new_len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, type)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(xfs_agblock_t, new_agbno)
		__field(xfs_extlen_t, new_len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->type = type;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->new_agbno = new_agbno;
		__entry->new_len = new_len;
	),
	TP_printk("dev %d:%d type %d agno %u agbno %u len %u new_agbno %u new_len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->new_agbno,
		  __entry->new_len)
);

/* simple inode-based error/%ip tracepoint class */
DECLARE_EVENT_CLASS(xfs_inode_error_class,
	TP_PROTO(struct xfs_inode *ip, int error, unsigned long caller_ip),
	TP_ARGS(ip, error, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, error)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->error = error;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino %llx error %d caller %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->error,
		  (char *)__entry->caller_ip)
);

#define DEFINE_INODE_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_inode_error_class, name, \
	TP_PROTO(struct xfs_inode *ip, int error, \
		 unsigned long caller_ip), \
	TP_ARGS(ip, error, caller_ip))

/* reflink allocator */
TRACE_EVENT(xfs_bmap_remap_alloc,
	TP_PROTO(struct xfs_inode *ip, xfs_fsblock_t fsbno,
		 xfs_extlen_t len),
	TP_ARGS(ip, fsbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsblock_t, fsbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->fsbno = fsbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d ino 0x%llx fsbno 0x%llx len %x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->fsbno,
		  __entry->len)
);
DEFINE_INODE_ERROR_EVENT(xfs_bmap_remap_alloc_error);

/* reflink tracepoint classes */

/* two-file io tracepoint class */
DECLARE_EVENT_CLASS(xfs_double_io_class,
	TP_PROTO(struct xfs_inode *src, xfs_off_t soffset, xfs_off_t len,
		 struct xfs_inode *dest, xfs_off_t doffset),
	TP_ARGS(src, soffset, len, dest, doffset),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, src_ino)
		__field(loff_t, src_isize)
		__field(loff_t, src_disize)
		__field(loff_t, src_offset)
		__field(size_t, len)
		__field(xfs_ino_t, dest_ino)
		__field(loff_t, dest_isize)
		__field(loff_t, dest_disize)
		__field(loff_t, dest_offset)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(src)->i_sb->s_dev;
		__entry->src_ino = src->i_ino;
		__entry->src_isize = VFS_I(src)->i_size;
		__entry->src_disize = src->i_d.di_size;
		__entry->src_offset = soffset;
		__entry->len = len;
		__entry->dest_ino = dest->i_ino;
		__entry->dest_isize = VFS_I(dest)->i_size;
		__entry->dest_disize = dest->i_d.di_size;
		__entry->dest_offset = doffset;
	),
	TP_printk("dev %d:%d count %zd "
		  "ino 0x%llx isize 0x%llx disize 0x%llx offset 0x%llx -> "
		  "ino 0x%llx isize 0x%llx disize 0x%llx offset 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->len,
		  __entry->src_ino,
		  __entry->src_isize,
		  __entry->src_disize,
		  __entry->src_offset,
		  __entry->dest_ino,
		  __entry->dest_isize,
		  __entry->dest_disize,
		  __entry->dest_offset)
)

#define DEFINE_DOUBLE_IO_EVENT(name)	\
DEFINE_EVENT(xfs_double_io_class, name,	\
	TP_PROTO(struct xfs_inode *src, xfs_off_t soffset, xfs_off_t len, \
		 struct xfs_inode *dest, xfs_off_t doffset), \
	TP_ARGS(src, soffset, len, dest, doffset))

/* two-file vfs io tracepoint class */
DECLARE_EVENT_CLASS(xfs_double_vfs_io_class,
	TP_PROTO(struct inode *src, u64 soffset, u64 len,
		 struct inode *dest, u64 doffset),
	TP_ARGS(src, soffset, len, dest, doffset),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, src_ino)
		__field(loff_t, src_isize)
		__field(loff_t, src_offset)
		__field(size_t, len)
		__field(unsigned long, dest_ino)
		__field(loff_t, dest_isize)
		__field(loff_t, dest_offset)
	),
	TP_fast_assign(
		__entry->dev = src->i_sb->s_dev;
		__entry->src_ino = src->i_ino;
		__entry->src_isize = i_size_read(src);
		__entry->src_offset = soffset;
		__entry->len = len;
		__entry->dest_ino = dest->i_ino;
		__entry->dest_isize = i_size_read(dest);
		__entry->dest_offset = doffset;
	),
	TP_printk("dev %d:%d count %zd "
		  "ino 0x%lx isize 0x%llx offset 0x%llx -> "
		  "ino 0x%lx isize 0x%llx offset 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->len,
		  __entry->src_ino,
		  __entry->src_isize,
		  __entry->src_offset,
		  __entry->dest_ino,
		  __entry->dest_isize,
		  __entry->dest_offset)
)

#define DEFINE_DOUBLE_VFS_IO_EVENT(name)	\
DEFINE_EVENT(xfs_double_vfs_io_class, name,	\
	TP_PROTO(struct inode *src, u64 soffset, u64 len, \
		 struct inode *dest, u64 doffset), \
	TP_ARGS(src, soffset, len, dest, doffset))

/* CoW write tracepoint */
DECLARE_EVENT_CLASS(xfs_copy_on_write_class,
	TP_PROTO(struct xfs_inode *ip, xfs_fileoff_t lblk, xfs_fsblock_t pblk,
		 xfs_extlen_t len, xfs_fsblock_t new_pblk),
	TP_ARGS(ip, lblk, pblk, len, new_pblk),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fileoff_t, lblk)
		__field(xfs_fsblock_t, pblk)
		__field(xfs_extlen_t, len)
		__field(xfs_fsblock_t, new_pblk)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->lblk = lblk;
		__entry->pblk = pblk;
		__entry->len = len;
		__entry->new_pblk = new_pblk;
	),
	TP_printk("dev %d:%d ino 0x%llx lblk 0x%llx pblk 0x%llx "
		  "len 0x%x new_pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->lblk,
		  __entry->pblk,
		  __entry->len,
		  __entry->new_pblk)
)

#define DEFINE_COW_EVENT(name)	\
DEFINE_EVENT(xfs_copy_on_write_class, name,	\
	TP_PROTO(struct xfs_inode *ip, xfs_fileoff_t lblk, xfs_fsblock_t pblk, \
		 xfs_extlen_t len, xfs_fsblock_t new_pblk), \
	TP_ARGS(ip, lblk, pblk, len, new_pblk))

/* inode/irec events */
DECLARE_EVENT_CLASS(xfs_inode_irec_class,
	TP_PROTO(struct xfs_inode *ip, struct xfs_bmbt_irec *irec),
	TP_ARGS(ip, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fileoff_t, lblk)
		__field(xfs_extlen_t, len)
		__field(xfs_fsblock_t, pblk)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->lblk = irec->br_startoff;
		__entry->len = irec->br_blockcount;
		__entry->pblk = irec->br_startblock;
	),
	TP_printk("dev %d:%d ino 0x%llx lblk 0x%llx len 0x%x pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->lblk,
		  __entry->len,
		  __entry->pblk)
);
#define DEFINE_INODE_IREC_EVENT(name) \
DEFINE_EVENT(xfs_inode_irec_class, name, \
	TP_PROTO(struct xfs_inode *ip, struct xfs_bmbt_irec *irec), \
	TP_ARGS(ip, irec))

/* refcount/reflink tracepoint definitions */

/* reflink tracepoints */
DEFINE_INODE_EVENT(xfs_reflink_set_inode_flag);
DEFINE_INODE_EVENT(xfs_reflink_unset_inode_flag);
DEFINE_ITRUNC_EVENT(xfs_reflink_update_inode_size);
DEFINE_IOMAP_EVENT(xfs_reflink_remap_imap);
TRACE_EVENT(xfs_reflink_remap_blocks_loop,
	TP_PROTO(struct xfs_inode *src, xfs_fileoff_t soffset,
		 xfs_filblks_t len, struct xfs_inode *dest,
		 xfs_fileoff_t doffset),
	TP_ARGS(src, soffset, len, dest, doffset),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, src_ino)
		__field(xfs_fileoff_t, src_lblk)
		__field(xfs_filblks_t, len)
		__field(xfs_ino_t, dest_ino)
		__field(xfs_fileoff_t, dest_lblk)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(src)->i_sb->s_dev;
		__entry->src_ino = src->i_ino;
		__entry->src_lblk = soffset;
		__entry->len = len;
		__entry->dest_ino = dest->i_ino;
		__entry->dest_lblk = doffset;
	),
	TP_printk("dev %d:%d len 0x%llx "
		  "ino 0x%llx offset 0x%llx blocks -> "
		  "ino 0x%llx offset 0x%llx blocks",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->len,
		  __entry->src_ino,
		  __entry->src_lblk,
		  __entry->dest_ino,
		  __entry->dest_lblk)
);
TRACE_EVENT(xfs_reflink_punch_range,
	TP_PROTO(struct xfs_inode *ip, xfs_fileoff_t lblk,
		 xfs_extlen_t len),
	TP_ARGS(ip, lblk, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fileoff_t, lblk)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->lblk = lblk;
		__entry->len = len;
	),
	TP_printk("dev %d:%d ino 0x%llx lblk 0x%llx len 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->lblk,
		  __entry->len)
);
TRACE_EVENT(xfs_reflink_remap,
	TP_PROTO(struct xfs_inode *ip, xfs_fileoff_t lblk,
		 xfs_extlen_t len, xfs_fsblock_t new_pblk),
	TP_ARGS(ip, lblk, len, new_pblk),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fileoff_t, lblk)
		__field(xfs_extlen_t, len)
		__field(xfs_fsblock_t, new_pblk)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->lblk = lblk;
		__entry->len = len;
		__entry->new_pblk = new_pblk;
	),
	TP_printk("dev %d:%d ino 0x%llx lblk 0x%llx len 0x%x new_pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->lblk,
		  __entry->len,
		  __entry->new_pblk)
);
DEFINE_DOUBLE_IO_EVENT(xfs_reflink_remap_range);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_remap_range_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_set_inode_flag_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_update_inode_size_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_reflink_main_loop_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_read_iomap_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_remap_blocks_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_remap_extent_error);

/* dedupe tracepoints */
DEFINE_DOUBLE_IO_EVENT(xfs_reflink_compare_extents);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_compare_extents_error);

/* ioctl tracepoints */
DEFINE_DOUBLE_VFS_IO_EVENT(xfs_ioctl_reflink);
DEFINE_DOUBLE_VFS_IO_EVENT(xfs_ioctl_clone_range);
DEFINE_DOUBLE_VFS_IO_EVENT(xfs_ioctl_file_extent_same);
TRACE_EVENT(xfs_ioctl_clone,
	TP_PROTO(struct inode *src, struct inode *dest),
	TP_ARGS(src, dest),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, src_ino)
		__field(loff_t, src_isize)
		__field(unsigned long, dest_ino)
		__field(loff_t, dest_isize)
	),
	TP_fast_assign(
		__entry->dev = src->i_sb->s_dev;
		__entry->src_ino = src->i_ino;
		__entry->src_isize = i_size_read(src);
		__entry->dest_ino = dest->i_ino;
		__entry->dest_isize = i_size_read(dest);
	),
	TP_printk("dev %d:%d "
		  "ino 0x%lx isize 0x%llx -> "
		  "ino 0x%lx isize 0x%llx\n",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->src_ino,
		  __entry->src_isize,
		  __entry->dest_ino,
		  __entry->dest_isize)
);

/* unshare tracepoints */
DEFINE_SIMPLE_IO_EVENT(xfs_reflink_unshare);
DEFINE_SIMPLE_IO_EVENT(xfs_reflink_cow_eof_block);
DEFINE_PAGE_EVENT(xfs_reflink_unshare_page);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_unshare_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_cow_eof_block_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_dirty_page_error);

/* copy on write */
DEFINE_INODE_IREC_EVENT(xfs_reflink_trim_around_shared);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_alloc);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_found);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_enospc);

DEFINE_RW_EVENT(xfs_reflink_reserve_cow);
DEFINE_RW_EVENT(xfs_reflink_allocate_cow_range);

DEFINE_INODE_IREC_EVENT(xfs_reflink_bounce_dio_write);
DEFINE_IOMAP_EVENT(xfs_reflink_find_cow_mapping);
DEFINE_INODE_IREC_EVENT(xfs_reflink_trim_irec);

DEFINE_SIMPLE_IO_EVENT(xfs_reflink_cancel_cow_range);
DEFINE_SIMPLE_IO_EVENT(xfs_reflink_end_cow);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_remap);

DEFINE_INODE_ERROR_EVENT(xfs_reflink_allocate_cow_range_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_cancel_cow_range_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_end_cow_error);

DEFINE_COW_EVENT(xfs_reflink_fork_buf);
DEFINE_COW_EVENT(xfs_reflink_finish_fork_buf);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_fork_buf_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_finish_fork_buf_error);

DEFINE_INODE_EVENT(xfs_reflink_cancel_pending_cow);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cancel_cow);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_cancel_pending_cow_error);

/* rmap swapext tracepoints */
DEFINE_INODE_IREC_EVENT(xfs_swap_extent_rmap_remap);
DEFINE_INODE_IREC_EVENT(xfs_swap_extent_rmap_remap_piece);
DEFINE_INODE_ERROR_EVENT(xfs_swap_extent_rmap_error);

#endif /* _TRACE_XFS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE xfs_trace
#include <trace/define_trace.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs

#if !defined(_TRACE_F2FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_F2FS_H

#include <linux/tracepoint.h>

#define show_dev(entry)		MAJOR(entry->dev), MINOR(entry->dev)
#define show_dev_ino(entry)	show_dev(entry), (unsigned long)entry->ino

#define show_block_type(type)						\
	__print_symbolic(type,						\
		{ NODE,		"NODE" },				\
		{ DATA,		"DATA" },				\
		{ META,		"META" },				\
		{ META_FLUSH,	"META_FLUSH" })

#define show_bio_type(type)						\
	__print_symbolic(type,						\
		{ READ, 	"READ" },				\
		{ READA, 	"READAHEAD" },				\
		{ READ_SYNC, 	"READ_SYNC" },				\
		{ WRITE, 	"WRITE" },				\
		{ WRITE_SYNC, 	"WRITE_SYNC" },				\
		{ WRITE_FLUSH,	"WRITE_FLUSH" },			\
		{ WRITE_FUA, 	"WRITE_FUA" })

#define show_data_type(type)						\
	__print_symbolic(type,						\
		{ CURSEG_HOT_DATA, 	"Hot DATA" },			\
		{ CURSEG_WARM_DATA, 	"Warm DATA" },			\
		{ CURSEG_COLD_DATA, 	"Cold DATA" },			\
		{ CURSEG_HOT_NODE, 	"Hot NODE" },			\
		{ CURSEG_WARM_NODE, 	"Warm NODE" },			\
		{ CURSEG_COLD_NODE, 	"Cold NODE" },			\
		{ NO_CHECK_TYPE, 	"No TYPE" })

#define show_file_type(type)						\
	__print_symbolic(type,						\
		{ 0,		"FILE" },				\
		{ 1,		"DIR" })

#define show_gc_type(type)						\
	__print_symbolic(type,						\
		{ FG_GC,	"Foreground GC" },			\
		{ BG_GC,	"Background GC" })

#define show_alloc_mode(type)						\
	__print_symbolic(type,						\
		{ LFS,	"LFS-mode" },					\
		{ SSR,	"SSR-mode" })

#define show_victim_policy(type)					\
	__print_symbolic(type,						\
		{ GC_GREEDY,	"Greedy" },				\
		{ GC_CB,	"Cost-Benefit" })

struct victim_sel_policy;

DECLARE_EVENT_CLASS(f2fs__inode,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(ino_t,	pino)
		__field(umode_t, mode)
		__field(loff_t,	size)
		__field(unsigned int, nlink)
		__field(blkcnt_t, blocks)
		__field(__u8,	advise)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pino	= F2FS_I(inode)->i_pino;
		__entry->mode	= inode->i_mode;
		__entry->nlink	= inode->i_nlink;
		__entry->size	= inode->i_size;
		__entry->blocks	= inode->i_blocks;
		__entry->advise	= F2FS_I(inode)->i_advise;
	),

	TP_printk("dev = (%d,%d), ino = %lu, pino = %lu, i_mode = 0x%hx, "
		"i_size = %lld, i_nlink = %u, i_blocks = %llu, i_advise = 0x%x",
		show_dev_ino(__entry),
		(unsigned long)__entry->pino,
		__entry->mode,
		__entry->size,
		(unsigned int)__entry->nlink,
		(unsigned long long)__entry->blocks,
		(unsigned char)__entry->advise)
);

DECLARE_EVENT_CLASS(f2fs__inode_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->ret	= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, ret = %d",
		show_dev_ino(__entry),
		__entry->ret)
);

DEFINE_EVENT(f2fs__inode, f2fs_sync_file_enter,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

TRACE_EVENT(f2fs_sync_file_exit,

	TP_PROTO(struct inode *inode, bool need_cp, int datasync, int ret),

	TP_ARGS(inode, need_cp, datasync, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(bool,	need_cp)
		__field(int,	datasync)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->need_cp	= need_cp;
		__entry->datasync	= datasync;
		__entry->ret		= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, checkpoint is %s, "
		"datasync = %d, ret = %d",
		show_dev_ino(__entry),
		__entry->need_cp ? "needed" : "not needed",
		__entry->datasync,
		__entry->ret)
);

TRACE_EVENT(f2fs_sync_fs,

	TP_PROTO(struct super_block *sb, int wait),

	TP_ARGS(sb, wait),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(int,	dirty)
		__field(int,	wait)
	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->dirty	= F2FS_SB(sb)->s_dirty;
		__entry->wait	= wait;
	),

	TP_printk("dev = (%d,%d), superblock is %s, wait = %d",
		show_dev(__entry),
		__entry->dirty ? "dirty" : "not dirty",
		__entry->wait)
);

DEFINE_EVENT(f2fs__inode, f2fs_iget,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_iget_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DEFINE_EVENT(f2fs__inode, f2fs_evict_inode,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_new_inode,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

TRACE_EVENT(f2fs_unlink_enter,

	TP_PROTO(struct inode *dir, struct dentry *dentry),

	TP_ARGS(dir, dentry),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	size)
		__field(blkcnt_t, blocks)
		__field(const char *,	name)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->ino	= dir->i_ino;
		__entry->size	= dir->i_size;
		__entry->blocks	= dir->i_blocks;
		__entry->name	= dentry->d_name.name;
	),

	TP_printk("dev = (%d,%d), dir ino = %lu, i_size = %lld, "
		"i_blocks = %llu, name = %s",
		show_dev_ino(__entry),
		__entry->size,
		(unsigned long long)__entry->blocks,
		__entry->name)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_unlink_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DEFINE_EVENT(f2fs__inode, f2fs_truncate,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

TRACE_EVENT(f2fs_truncate_data_blocks_range,

	TP_PROTO(struct inode *inode, nid_t nid, unsigned int ofs, int free),

	TP_ARGS(inode, nid,  ofs, free),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(nid_t,	nid)
		__field(unsigned int,	ofs)
		__field(int,	free)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->nid	= nid;
		__entry->ofs	= ofs;
		__entry->free	= free;
	),

	TP_printk("dev = (%d,%d), ino = %lu, nid = %u, offset = %u, freed = %d",
		show_dev_ino(__entry),
		(unsigned int)__entry->nid,
		__entry->ofs,
		__entry->free)
);

DECLARE_EVENT_CLASS(f2fs__truncate_op,

	TP_PROTO(struct inode *inode, u64 from),

	TP_ARGS(inode, from),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	size)
		__field(blkcnt_t, blocks)
		__field(u64,	from)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->size	= inode->i_size;
		__entry->blocks	= inode->i_blocks;
		__entry->from	= from;
	),

	TP_printk("dev = (%d,%d), ino = %lu, i_size = %lld, i_blocks = %llu, "
		"start file offset = %llu",
		show_dev_ino(__entry),
		__entry->size,
		(unsigned long long)__entry->blocks,
		(unsigned long long)__entry->from)
);

DEFINE_EVENT(f2fs__truncate_op, f2fs_truncate_blocks_enter,

	TP_PROTO(struct inode *inode, u64 from),

	TP_ARGS(inode, from)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_truncate_blocks_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DEFINE_EVENT(f2fs__truncate_op, f2fs_truncate_inode_blocks_enter,

	TP_PROTO(struct inode *inode, u64 from),

	TP_ARGS(inode, from)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_truncate_inode_blocks_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DECLARE_EVENT_CLASS(f2fs__truncate_node,

	TP_PROTO(struct inode *inode, nid_t nid, block_t blk_addr),

	TP_ARGS(inode, nid, blk_addr),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(nid_t,	nid)
		__field(block_t,	blk_addr)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->nid		= nid;
		__entry->blk_addr	= blk_addr;
	),

	TP_printk("dev = (%d,%d), ino = %lu, nid = %u, block_address = 0x%llx",
		show_dev_ino(__entry),
		(unsigned int)__entry->nid,
		(unsigned long long)__entry->blk_addr)
);

DEFINE_EVENT(f2fs__truncate_node, f2fs_truncate_nodes_enter,

	TP_PROTO(struct inode *inode, nid_t nid, block_t blk_addr),

	TP_ARGS(inode, nid, blk_addr)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_truncate_nodes_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DEFINE_EVENT(f2fs__truncate_node, f2fs_truncate_node,

	TP_PROTO(struct inode *inode, nid_t nid, block_t blk_addr),

	TP_ARGS(inode, nid, blk_addr)
);

TRACE_EVENT(f2fs_truncate_partial_nodes,

	TP_PROTO(struct inode *inode, nid_t nid[], int depth, int err),

	TP_ARGS(inode, nid, depth, err),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(nid_t,	nid[3])
		__field(int,	depth)
		__field(int,	err)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->nid[0]	= nid[0];
		__entry->nid[1]	= nid[1];
		__entry->nid[2]	= nid[2];
		__entry->depth	= depth;
		__entry->err	= err;
	),

	TP_printk("dev = (%d,%d), ino = %lu, "
		"nid[0] = %u, nid[1] = %u, nid[2] = %u, depth = %d, err = %d",
		show_dev_ino(__entry),
		(unsigned int)__entry->nid[0],
		(unsigned int)__entry->nid[1],
		(unsigned int)__entry->nid[2],
		__entry->depth,
		__entry->err)
);

TRACE_EVENT_CONDITION(f2fs_readpage,

	TP_PROTO(struct page *page, sector_t blkaddr, int type),

	TP_ARGS(page, blkaddr, type),

	TP_CONDITION(page->mapping),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(pgoff_t,	index)
		__field(sector_t,	blkaddr)
		__field(int,	type)
	),

	TP_fast_assign(
		__entry->dev		= page->mapping->host->i_sb->s_dev;
		__entry->ino		= page->mapping->host->i_ino;
		__entry->index		= page->index;
		__entry->blkaddr	= blkaddr;
		__entry->type		= type;
	),

	TP_printk("dev = (%d,%d), ino = %lu, page_index = 0x%lx, "
		"blkaddr = 0x%llx, bio_type = %s",
		show_dev_ino(__entry),
		(unsigned long)__entry->index,
		(unsigned long long)__entry->blkaddr,
		show_bio_type(__entry->type))
);

TRACE_EVENT(f2fs_get_data_block,
	TP_PROTO(struct inode *inode, sector_t iblock,
				struct buffer_head *bh, int ret),

	TP_ARGS(inode, iblock, bh, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(sector_t,	iblock)
		__field(sector_t,	bh_start)
		__field(size_t,	bh_size)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->iblock		= iblock;
		__entry->bh_start	= bh->b_blocknr;
		__entry->bh_size	= bh->b_size;
		__entry->ret		= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, file offset = %llu, "
		"start blkaddr = 0x%llx, len = 0x%llx bytes, err = %d",
		show_dev_ino(__entry),
		(unsigned long long)__entry->iblock,
		(unsigned long long)__entry->bh_start,
		(unsigned long long)__entry->bh_size,
		__entry->ret)
);

TRACE_EVENT(f2fs_get_victim,

	TP_PROTO(struct super_block *sb, int type, int gc_type,
			struct victim_sel_policy *p, unsigned int pre_victim,
			unsigned int prefree, unsigned int free),

	TP_ARGS(sb, type, gc_type, p, pre_victim, prefree, free),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(int,	type)
		__field(int,	gc_type)
		__field(int,	alloc_mode)
		__field(int,	gc_mode)
		__field(unsigned int,	victim)
		__field(unsigned int,	ofs_unit)
		__field(unsigned int,	pre_victim)
		__field(unsigned int,	prefree)
		__field(unsigned int,	free)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->type		= type;
		__entry->gc_type	= gc_type;
		__entry->alloc_mode	= p->alloc_mode;
		__entry->gc_mode	= p->gc_mode;
		__entry->victim		= p->min_segno;
		__entry->ofs_unit	= p->ofs_unit;
		__entry->pre_victim	= pre_victim;
		__entry->prefree	= prefree;
		__entry->free		= free;
	),

	TP_printk("dev = (%d,%d), type = %s, policy = (%s, %s, %s), victim = %u "
		"ofs_unit = %u, pre_victim_secno = %d, prefree = %u, free = %u",
		show_dev(__entry),
		show_data_type(__entry->type),
		show_gc_type(__entry->gc_type),
		show_alloc_mode(__entry->alloc_mode),
		show_victim_policy(__entry->gc_mode),
		__entry->victim,
		__entry->ofs_unit,
		(int)__entry->pre_victim,
		__entry->prefree,
		__entry->free)
);

TRACE_EVENT(f2fs_fallocate,

	TP_PROTO(struct inode *inode, int mode,
				loff_t offset, loff_t len, int ret),

	TP_ARGS(inode, mode, offset, len, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int,	mode)
		__field(loff_t,	offset)
		__field(loff_t,	len)
		__field(loff_t, size)
		__field(blkcnt_t, blocks)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->mode	= mode;
		__entry->offset	= offset;
		__entry->len	= len;
		__entry->size	= inode->i_size;
		__entry->blocks = inode->i_blocks;
		__entry->ret	= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, mode = %x, offset = %lld, "
		"len = %lld,  i_size = %lld, i_blocks = %llu, ret = %d",
		show_dev_ino(__entry),
		__entry->mode,
		(unsigned long long)__entry->offset,
		(unsigned long long)__entry->len,
		(unsigned long long)__entry->size,
		(unsigned long long)__entry->blocks,
		__entry->ret)
);

TRACE_EVENT(f2fs_reserve_new_block,

	TP_PROTO(struct inode *inode, nid_t nid, unsigned int ofs_in_node),

	TP_ARGS(inode, nid, ofs_in_node),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(nid_t, nid)
		__field(unsigned int, ofs_in_node)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->nid	= nid;
		__entry->ofs_in_node = ofs_in_node;
	),

	TP_printk("dev = (%d,%d), nid = %u, ofs_in_node = %u",
		show_dev(__entry),
		(unsigned int)__entry->nid,
		__entry->ofs_in_node)
);

TRACE_EVENT(f2fs_do_submit_bio,

	TP_PROTO(struct super_block *sb, int btype, bool sync, struct bio *bio),

	TP_ARGS(sb, btype, sync, bio),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(int,	btype)
		__field(bool,	sync)
		__field(sector_t,	sector)
		__field(unsigned int,	size)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->btype		= btype;
		__entry->sync		= sync;
		__entry->sector		= bio->bi_sector;
		__entry->size		= bio->bi_size;
	),

	TP_printk("dev = (%d,%d), type = %s, io = %s, sector = %lld, size = %u",
		show_dev(__entry),
		show_block_type(__entry->btype),
		__entry->sync ? "sync" : "no sync",
		(unsigned long long)__entry->sector,
		__entry->size)
);

DECLARE_EVENT_CLASS(f2fs__page,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int, type)
		__field(int, dir)
		__field(pgoff_t, index)
		__field(int, dirty)
	),

	TP_fast_assign(
		__entry->dev	= page->mapping->host->i_sb->s_dev;
		__entry->ino	= page->mapping->host->i_ino;
		__entry->type	= type;
		__entry->dir	= S_ISDIR(page->mapping->host->i_mode);
		__entry->index	= page->index;
		__entry->dirty	= PageDirty(page);
	),

	TP_printk("dev = (%d,%d), ino = %lu, %s, %s, index = %lu, dirty = %d",
		show_dev_ino(__entry),
		show_block_type(__entry->type),
		show_file_type(__entry->dir),
		(unsigned long)__entry->index,
		__entry->dirty)
);

DEFINE_EVENT(f2fs__page, f2fs_set_page_dirty,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

DEFINE_EVENT(f2fs__page, f2fs_vm_page_mkwrite,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

TRACE_EVENT(f2fs_submit_write_page,

	TP_PROTO(struct page *page, block_t blk_addr, int type),

	TP_ARGS(page, blk_addr, type),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int, type)
		__field(pgoff_t, index)
		__field(block_t, block)
	),

	TP_fast_assign(
		__entry->dev	= page->mapping->host->i_sb->s_dev;
		__entry->ino	= page->mapping->host->i_ino;
		__entry->type	= type;
		__entry->index	= page->index;
		__entry->block	= blk_addr;
	),

	TP_printk("dev = (%d,%d), ino = %lu, %s, index = %lu, blkaddr = 0x%llx",
		show_dev_ino(__entry),
		show_block_type(__entry->type),
		(unsigned long)__entry->index,
		(unsigned long long)__entry->block)
);

TRACE_EVENT(f2fs_write_checkpoint,

	TP_PROTO(struct super_block *sb, bool is_umount, char *msg),

	TP_ARGS(sb, is_umount, msg),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(bool,	is_umount)
		__field(char *,	msg)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->is_umount	= is_umount;
		__entry->msg		= msg;
	),

	TP_printk("dev = (%d,%d), checkpoint for %s, state = %s",
		show_dev(__entry),
		__entry->is_umount ? "clean umount" : "consistency",
		__entry->msg)
);

#endif /* _TRACE_F2FS_H */

 /* This part must be outside protection */
#include <trace/define_trace.h>

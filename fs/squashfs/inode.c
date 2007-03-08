/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005 Phillip Lougher <phillip@lougher.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * inode.c
 */

#define SQUASHFS_1_0_COMPATIBILITY

#include <linux/types.h>
#include <linux/squashfs_fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/squashfs_fs_sb.h>
#include <linux/squashfs_fs_i.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/init.h>
#include <linux/dcache.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include <asm/semaphore.h>

#ifdef CONFIG_SQUASHFS_LZMA
#include "LzmaWrapper.h"
#else
#include <linux/zlib.h>
#endif

#include <linux/blkdev.h>
#include <linux/vmalloc.h>

#ifdef SQUASHFS_TRACE
#define TRACE(s, args...)				printk(KERN_NOTICE "SQUASHFS: "s, ## args)
#else
#define TRACE(s, args...)				{}
#endif

#define ERROR(s, args...)				printk(KERN_ERR "SQUASHFS error: "s, ## args)

#define SERROR(s, args...)				if(!silent) printk(KERN_ERR "SQUASHFS error: "s, ## args)
#define WARNING(s, args...)				printk(KERN_WARNING "SQUASHFS: "s, ## args)

static void squashfs_put_super(struct super_block *);
static int squashfs_statfs(struct dentry *, struct kstatfs *);
static int squashfs_symlink_readpage(struct file *file, struct page *page);
static int squashfs_readpage(struct file *file, struct page *page);
static int squashfs_readpage4K(struct file *file, struct page *page);
static int squashfs_readdir(struct file *, void *, filldir_t);
static struct dentry *squashfs_lookup(struct inode *, struct dentry *, struct nameidata *);
static unsigned int read_data(struct super_block *s, char *buffer,
		unsigned int index, unsigned int length, unsigned int *next_index);
static int squashfs_get_cached_block(struct super_block *s, char *buffer,
		unsigned int block, unsigned int offset, int length,
		unsigned int *next_block, unsigned int *next_offset);
static struct inode *squashfs_iget(struct super_block *s, squashfs_inode inode);
static unsigned int read_blocklist(struct inode *inode, int index, int readahead_blks,
		char *block_list, unsigned short **block_p, unsigned int *bsize);
static void squashfs_put_super(struct super_block *s);
static int squashfs_get_sb(struct file_system_type *, int, const char *, void *, struct vfsmount *);
static struct inode *squashfs_alloc_inode(struct super_block *sb);
static void squashfs_destroy_inode(struct inode *inode);
static int init_inodecache(void);
static void destroy_inodecache(void);

#ifdef SQUASHFS_1_0_COMPATIBILITY
static int squashfs_readpage_lessthan4K(struct file *file, struct page *page);
static struct inode *squashfs_iget_1(struct super_block *s, squashfs_inode inode);
static unsigned int read_blocklist_1(struct inode *inode, int index, int readahead_blks,
		char *block_list, unsigned short **block_p, unsigned int *bsize);
#endif

DECLARE_MUTEX(read_data_mutex);

#ifdef CONFIG_SQUASHFS_LZMA
static char *lzma_data;
#else
static z_stream stream;
#endif

static struct file_system_type squashfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "squashfs",
	.get_sb = squashfs_get_sb,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV
	};

static unsigned char squashfs_filetype_table[] = {
	DT_UNKNOWN, DT_DIR, DT_REG, DT_LNK, DT_BLK, DT_CHR, DT_FIFO, DT_SOCK
};

static struct super_operations squashfs_ops = {
	.alloc_inode = squashfs_alloc_inode,
	.destroy_inode = squashfs_destroy_inode,
	.statfs = squashfs_statfs,
	.put_super = squashfs_put_super,
};

static struct address_space_operations squashfs_symlink_aops = {
	.readpage = squashfs_symlink_readpage
};

static struct address_space_operations squashfs_aops = {
	.readpage = squashfs_readpage
};

static struct address_space_operations squashfs_aops_4K = {
	.readpage = squashfs_readpage4K
};

#ifdef SQUASHFS_1_0_COMPATIBILITY
static struct address_space_operations squashfs_aops_lessthan4K = {
	.readpage = squashfs_readpage_lessthan4K
};
#endif

static struct file_operations squashfs_dir_ops = {
	.read = generic_read_dir,
	.readdir = squashfs_readdir
};

static struct inode_operations squashfs_dir_inode_ops = {
	.lookup = squashfs_lookup
};


static inline struct squashfs_inode_info *SQUASHFS_I(struct inode *inode)
{
	return list_entry(inode, struct squashfs_inode_info, vfs_inode);
}


static struct buffer_head *get_block_length(struct super_block *s,
				int *cur_index, int *offset, int *c_byte)
{
	squashfs_sb_info *msblk = s->s_fs_info;
	unsigned short temp;
	struct buffer_head *bh;

	if (!(bh = sb_bread(s, *cur_index)))
		goto out;

	if (msblk->devblksize - *offset == 1) {
		if (msblk->swap)
			((unsigned char *) &temp)[1] = *((unsigned char *)
				(bh->b_data + *offset));
		else
			((unsigned char *) &temp)[0] = *((unsigned char *)
				(bh->b_data + *offset));
		brelse(bh);
		if (!(bh = sb_bread(s, ++(*cur_index))))
			goto out;
		if (msblk->swap)
			((unsigned char *) &temp)[0] = *((unsigned char *)
				bh->b_data); 
		else
			((unsigned char *) &temp)[1] = *((unsigned char *)
				bh->b_data); 
		*c_byte = temp;
		*offset = 1;
	} else {
		if (msblk->swap) {
			((unsigned char *) &temp)[1] = *((unsigned char *)
				(bh->b_data + *offset));
			((unsigned char *) &temp)[0] = *((unsigned char *)
				(bh->b_data + *offset + 1)); 
		} else {
			((unsigned char *) &temp)[0] = *((unsigned char *)
				(bh->b_data + *offset));
			((unsigned char *) &temp)[1] = *((unsigned char *)
				(bh->b_data + *offset + 1)); 
		}
		*c_byte = temp;
		*offset += 2;
	}

	if (SQUASHFS_CHECK_DATA(msblk->sBlk.flags)) {
		if (*offset == msblk->devblksize) {
			brelse(bh);
			if (!(bh = sb_bread(s, ++(*cur_index))))
				goto out;
			*offset = 0;
		}
		if (*((unsigned char *) (bh->b_data + *offset)) !=
						SQUASHFS_MARKER_BYTE) {
			ERROR("Metadata block marker corrupt @ %x\n",
						*cur_index);
			brelse(bh);
			goto out;
		}
		(*offset)++;
	}
	return bh;

out:
	return NULL;
}


static unsigned int read_data(struct super_block *s, char *buffer,
		unsigned int index, unsigned int length, unsigned int *next_index)
{
	squashfs_sb_info *msBlk = (squashfs_sb_info *)s->s_fs_info;
	struct buffer_head *bh[((SQUASHFS_FILE_MAX_SIZE - 1) >> msBlk->devblksize_log2) + 2];
	unsigned int offset = index & ((1 << msBlk->devblksize_log2) - 1);
	unsigned int cur_index = index >> msBlk->devblksize_log2;
	int bytes, avail_bytes, b = 0, k;
	char *c_buffer;
	unsigned int compressed;
	unsigned int c_byte = length;

	if(c_byte) {
		bytes = msBlk->devblksize - offset;
		compressed = SQUASHFS_COMPRESSED_BLOCK(c_byte);
		c_buffer = compressed ? msBlk->read_data : buffer;
		c_byte = SQUASHFS_COMPRESSED_SIZE_BLOCK(c_byte);

		TRACE("Block @ 0x%x, %scompressed size %d\n", index, compressed ? "" : "un", (unsigned int) c_byte);

		if(!(bh[0] = sb_getblk(s, cur_index)))
			goto block_release;
		for(b = 1; bytes < c_byte; b++) {
			if(!(bh[b] = sb_getblk(s, ++cur_index)))
				goto block_release;
			bytes += msBlk->devblksize;
		}
		ll_rw_block(READ, b, bh);
	} else {
		if(!(bh[0] = get_block_length(s, &cur_index, &offset, &c_byte)))
			goto read_failure;

		bytes = msBlk->devblksize - offset;
		compressed = SQUASHFS_COMPRESSED(c_byte);
		c_buffer = compressed ? msBlk->read_data : buffer;
		c_byte = SQUASHFS_COMPRESSED_SIZE(c_byte);

		TRACE("Block @ 0x%x, %scompressed size %d\n", index, compressed ? "" : "un", (unsigned int) c_byte);

		for(b = 1; bytes < c_byte; b++) {
			if(!(bh[b] = sb_getblk(s, ++cur_index)))
				goto block_release;
			bytes += msBlk->devblksize;
		}
		ll_rw_block(READ, b - 1, bh + 1);
	}

	if(compressed)
		down(&read_data_mutex);

	for(bytes = 0, k = 0; k < b; k++) {
		avail_bytes = (c_byte - bytes) > (msBlk->devblksize - offset) ? msBlk->devblksize - offset : c_byte - bytes;
		wait_on_buffer(bh[k]);
		if (!buffer_uptodate(bh[k]))
			goto block_release;
		memcpy(c_buffer + bytes, bh[k]->b_data + offset, avail_bytes);
		bytes += avail_bytes;
		offset = 0;
		brelse(bh[k]);
	}

	/*
	 * uncompress block
	 */
	if(compressed) {
#ifdef CONFIG_SQUASHFS_LZMA
		int out_size;
		int lzma_err;
		out_size=msBlk->read_size;
		
		if((lzma_err=lzma_inflate(c_buffer, c_byte, buffer, &out_size))){
			ERROR("lzma returned unexpected result 0x%x\n", lzma_err);
			bytes = 0;
		} else
			bytes = out_size;
//		printk("out size = %d, processed = %d\n", msBlk->read_size, out_size);
#else
		int zlib_err;

		stream.next_in = c_buffer;
		stream.avail_in = c_byte;
		stream.next_out = buffer;
		stream.avail_out = msBlk->read_size;
		if(((zlib_err = zlib_inflateInit(&stream)) != Z_OK) ||
				((zlib_err = zlib_inflate(&stream, Z_FINISH)) != Z_STREAM_END) ||
				((zlib_err = zlib_inflateEnd(&stream)) != Z_OK)) {
			ERROR("zlib_fs returned unexpected result 0x%x\n", zlib_err);
			bytes = 0;
		} else
			bytes = stream.total_out;
#endif
		up(&read_data_mutex);
	}

	if(next_index)
		*next_index = index + c_byte + (length ? 0 : (SQUASHFS_CHECK_DATA(msBlk->sBlk.flags) ? 3 : 2));

	return bytes;

block_release:
	while(--b >= 0) brelse(bh[b]);

read_failure:
	ERROR("sb_bread failed reading block 0x%x\n", cur_index);
	return 0;
}


static int squashfs_get_cached_block(struct super_block *s, char *buffer,
		unsigned int block, unsigned int offset, int length,
		unsigned int *next_block, unsigned int *next_offset)
{
	squashfs_sb_info *msBlk = (squashfs_sb_info *)s->s_fs_info;
	int n, i, bytes, return_length = length;
	unsigned int next_index;

	TRACE("Entered squashfs_get_cached_block [%x:%x]\n", block, offset);

	for(;;) {
		for(i = 0; i < SQUASHFS_CACHED_BLKS; i++) 
			if(msBlk->block_cache[i].block == block)
				break; 
		
		down(&msBlk->block_cache_mutex);
		if(i == SQUASHFS_CACHED_BLKS) {
			/* read inode header block */
			for(i = msBlk->next_cache, n = SQUASHFS_CACHED_BLKS; n ; n --, i = (i + 1) % SQUASHFS_CACHED_BLKS)
				if(msBlk->block_cache[i].block != SQUASHFS_USED_BLK)
					break;
			if(n == 0) {
				wait_queue_t wait;

				init_waitqueue_entry(&wait, current);
				add_wait_queue(&msBlk->waitq, &wait);
 				up(&msBlk->block_cache_mutex);
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule();
				set_current_state(TASK_RUNNING);
				remove_wait_queue(&msBlk->waitq, &wait);
				continue;
			}
			msBlk->next_cache = (i + 1) % SQUASHFS_CACHED_BLKS;

			if(msBlk->block_cache[i].block == SQUASHFS_INVALID_BLK) {
				if(!(msBlk->block_cache[i].data = (unsigned char *)
							kmalloc(SQUASHFS_METADATA_SIZE, GFP_KERNEL))) {
					ERROR("Failed to allocate cache block\n");
					up(&msBlk->block_cache_mutex);
					return 0;
				}
			}
	
			msBlk->block_cache[i].block = SQUASHFS_USED_BLK;
			up(&msBlk->block_cache_mutex);
			if(!(msBlk->block_cache[i].length = read_data(s, msBlk->block_cache[i].data, block, 0,
							&next_index))) {
				ERROR("Unable to read cache block [%x:%x]\n", block, offset);
				return 0;
			}
			down(&msBlk->block_cache_mutex);
			wake_up(&msBlk->waitq);
			msBlk->block_cache[i].block = block;
			msBlk->block_cache[i].next_index = next_index;
			TRACE("Read cache block [%x:%x]\n", block, offset);
		}

		if(msBlk->block_cache[i].block != block) {
			up(&msBlk->block_cache_mutex);
			continue;
		}

		if((bytes = msBlk->block_cache[i].length - offset) >= length) {
			if(buffer)
				memcpy(buffer, msBlk->block_cache[i].data + offset, length);
			if(msBlk->block_cache[i].length - offset == length) {
				*next_block = msBlk->block_cache[i].next_index;
				*next_offset = 0;
			} else {
				*next_block = block;
				*next_offset = offset + length;
			}
	
			up(&msBlk->block_cache_mutex);
			return return_length;
		} else {
			if(buffer) {
				memcpy(buffer, msBlk->block_cache[i].data + offset, bytes);
				buffer += bytes;
			}
			block = msBlk->block_cache[i].next_index;
			up(&msBlk->block_cache_mutex);
			length -= bytes;
			offset = 0;
		}
	}
}


static int get_fragment_location(struct super_block *s, unsigned int fragment, unsigned int *fragment_start_block, unsigned int *fragment_size)
{
	squashfs_sb_info *msBlk = (squashfs_sb_info *)s->s_fs_info;
	unsigned int start_block = msBlk->fragment_index[SQUASHFS_FRAGMENT_INDEX(fragment)];
	int offset = SQUASHFS_FRAGMENT_INDEX_OFFSET(fragment);
	squashfs_fragment_entry fragment_entry;

	if(msBlk->swap) {
		squashfs_fragment_entry sfragment_entry;

		if(!squashfs_get_cached_block(s, (char *) &sfragment_entry, start_block, offset,
					sizeof(sfragment_entry), &start_block, &offset))
			return 0;
		SQUASHFS_SWAP_FRAGMENT_ENTRY(&fragment_entry, &sfragment_entry);
	} else
		if(!squashfs_get_cached_block(s, (char *) &fragment_entry, start_block, offset,
					sizeof(fragment_entry), &start_block, &offset))
			return 0;

	*fragment_start_block = fragment_entry.start_block;
	*fragment_size = fragment_entry.size;

	return 1;
}


void release_cached_fragment(squashfs_sb_info *msBlk, struct squashfs_fragment_cache *fragment)
{
	down(&msBlk->fragment_mutex);
	fragment->locked --;
	wake_up(&msBlk->fragment_wait_queue);
	up(&msBlk->fragment_mutex);
}


struct squashfs_fragment_cache *get_cached_fragment(struct super_block *s, unsigned int start_block, int length)
{
	int i, n;
	squashfs_sb_info *msBlk = (squashfs_sb_info *)s->s_fs_info;

	for(;;) {
		down(&msBlk->fragment_mutex);
		for(i = 0; i < SQUASHFS_CACHED_FRAGMENTS && msBlk->fragment[i].block != start_block; i++);
		if(i == SQUASHFS_CACHED_FRAGMENTS) {
			for(i = msBlk->next_fragment, n = SQUASHFS_CACHED_FRAGMENTS;
				n && msBlk->fragment[i].locked; n--, i = (i + 1) % SQUASHFS_CACHED_FRAGMENTS);

			if(n == 0) {
				wait_queue_t wait;

				init_waitqueue_entry(&wait, current);
				add_wait_queue(&msBlk->fragment_wait_queue, &wait);
				up(&msBlk->fragment_mutex);
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule();
				set_current_state(TASK_RUNNING);
				remove_wait_queue(&msBlk->fragment_wait_queue, &wait);
				continue;
			}
			msBlk->next_fragment = (msBlk->next_fragment + 1) % SQUASHFS_CACHED_FRAGMENTS;
			
			if(msBlk->fragment[i].data == NULL)
				if(!(msBlk->fragment[i].data = (unsigned char *)
							SQUASHFS_ALLOC(SQUASHFS_FILE_MAX_SIZE))) {
					ERROR("Failed to allocate fragment cache block\n");
					up(&msBlk->fragment_mutex);
					return NULL;
				}

			msBlk->fragment[i].block = SQUASHFS_INVALID_BLK;
			msBlk->fragment[i].locked = 1;
			up(&msBlk->fragment_mutex);
			if(!(msBlk->fragment[i].length = read_data(s, msBlk->fragment[i].data, start_block, length,
							NULL))) {
				ERROR("Unable to read fragment cache block [%x]\n", start_block);
				msBlk->fragment[i].locked = 0;
				return NULL;
			}
			msBlk->fragment[i].block = start_block;
			TRACE("New fragment %d, start block %d, locked %d\n", i, msBlk->fragment[i].block, msBlk->fragment[i].locked);
			return &msBlk->fragment[i];
		}

		msBlk->fragment[i].locked ++;
		up(&msBlk->fragment_mutex);
		
		TRACE("Got fragment %d, start block %d, locked %d\n", i, msBlk->fragment[i].block, msBlk->fragment[i].locked);
		return &msBlk->fragment[i];
	}
}


#ifdef SQUASHFS_1_0_COMPATIBILITY
static struct inode *squashfs_iget_1(struct super_block *s, squashfs_inode inode)
{
	struct inode *i = new_inode(s);
	squashfs_sb_info *msBlk = (squashfs_sb_info *)s->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	unsigned int block = SQUASHFS_INODE_BLK(inode) + sBlk->inode_table_start;
	unsigned int offset = SQUASHFS_INODE_OFFSET(inode);
	unsigned int next_block, next_offset;
	squashfs_base_inode_header_1 inodeb;

	TRACE("Entered squashfs_iget_1\n");

	if(msBlk->swap) {
		squashfs_base_inode_header_1 sinodeb;

		if(!squashfs_get_cached_block(s, (char *) &sinodeb, block,  offset,
					sizeof(sinodeb), &next_block, &next_offset))
			goto failed_read;
		SQUASHFS_SWAP_BASE_INODE_HEADER_1(&inodeb, &sinodeb, sizeof(sinodeb));
	} else
		if(!squashfs_get_cached_block(s, (char *) &inodeb, block,  offset,
					sizeof(inodeb), &next_block, &next_offset))
			goto failed_read;

	i->i_nlink = 1;

	i->i_mtime.tv_sec = sBlk->mkfs_time;
	i->i_atime.tv_sec = sBlk->mkfs_time;
	i->i_ctime.tv_sec = sBlk->mkfs_time;

	if(inodeb.inode_type != SQUASHFS_IPC_TYPE)
		i->i_uid = msBlk->uid[((inodeb.inode_type - 1) / SQUASHFS_TYPES) * 16 + inodeb.uid];
	i->i_ino = SQUASHFS_MK_VFS_INODE(block - sBlk->inode_table_start, offset);

	i->i_mode = inodeb.mode;

	switch(inodeb.inode_type == SQUASHFS_IPC_TYPE ? SQUASHFS_IPC_TYPE : (inodeb.inode_type - 1) % SQUASHFS_TYPES + 1) {
		case SQUASHFS_FILE_TYPE: {
			squashfs_reg_inode_header_1 inodep;

			if(msBlk->swap) {
				squashfs_reg_inode_header_1 sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_REG_INODE_HEADER_1(&inodep, &sinodep);
			} else
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = inodep.file_size;
			i->i_fop = &generic_ro_fops;
			if(sBlk->block_size > 4096)
				i->i_data.a_ops = &squashfs_aops;
			else if(sBlk->block_size == 4096)
				i->i_data.a_ops = &squashfs_aops_4K;
			else
				i->i_data.a_ops = &squashfs_aops_lessthan4K;
			i->i_mode |= S_IFREG;
			i->i_mtime.tv_sec = inodep.mtime;
			i->i_atime.tv_sec = inodep.mtime;
			i->i_ctime.tv_sec = inodep.mtime;
			i->i_blocks = ((i->i_size - 1) >> 9) + 1;
			SQUASHFS_I(i)->u.s1.fragment_start_block = SQUASHFS_INVALID_BLK;
			SQUASHFS_I(i)->u.s1.fragment_offset = 0;
			SQUASHFS_I(i)->start_block = inodep.start_block;
			SQUASHFS_I(i)->block_list_start = next_block;
			SQUASHFS_I(i)->offset = next_offset;
			TRACE("File inode %x:%x, start_block %x, block_list_start %x, offset %x\n",
					SQUASHFS_INODE_BLK(inode), offset, inodep.start_block, next_block, next_offset);
			break;
		}
		case SQUASHFS_DIR_TYPE: {
			squashfs_dir_inode_header_1 inodep;

			if(msBlk->swap) {
				squashfs_dir_inode_header_1 sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_DIR_INODE_HEADER_1(&inodep, &sinodep);
			} else
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = inodep.file_size;
			i->i_op = &squashfs_dir_inode_ops;
			i->i_fop = &squashfs_dir_ops;
			i->i_mode |= S_IFDIR;
			i->i_mtime.tv_sec = inodep.mtime;
			i->i_atime.tv_sec = inodep.mtime;
			i->i_ctime.tv_sec = inodep.mtime;
			SQUASHFS_I(i)->start_block = inodep.start_block;
			SQUASHFS_I(i)->offset = inodep.offset;
			SQUASHFS_I(i)->u.s2.directory_index_count = 0;
			TRACE("Directory inode %x:%x, start_block %x, offset %x\n", SQUASHFS_INODE_BLK(inode), offset,
					inodep.start_block, inodep.offset);
			break;
		}
		case SQUASHFS_SYMLINK_TYPE: {
			squashfs_symlink_inode_header_1 inodep;
	
			if(msBlk->swap) {
				squashfs_symlink_inode_header_1 sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_SYMLINK_INODE_HEADER_1(&inodep, &sinodep);
			} else
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = inodep.symlink_size;
			i->i_op = &page_symlink_inode_operations;
			i->i_data.a_ops = &squashfs_symlink_aops;
			i->i_mode |= S_IFLNK;
			SQUASHFS_I(i)->start_block = next_block;
			SQUASHFS_I(i)->offset = next_offset;
			TRACE("Symbolic link inode %x:%x, start_block %x, offset %x\n",
				SQUASHFS_INODE_BLK(inode), offset, next_block, next_offset);
			break;
		 }
		 case SQUASHFS_BLKDEV_TYPE:
		 case SQUASHFS_CHRDEV_TYPE: {
			squashfs_dev_inode_header_1 inodep;

			if(msBlk->swap) {
				squashfs_dev_inode_header_1 sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_DEV_INODE_HEADER_1(&inodep, &sinodep);
			} else	
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = 0;
			i->i_mode |= (inodeb.inode_type == SQUASHFS_CHRDEV_TYPE) ? S_IFCHR : S_IFBLK;
			init_special_inode(i, i->i_mode, old_decode_dev(inodep.rdev));
			TRACE("Device inode %x:%x, rdev %x\n", SQUASHFS_INODE_BLK(inode), offset, inodep.rdev);
			break;
		 }
		 case SQUASHFS_IPC_TYPE: {
			squashfs_ipc_inode_header_1 inodep;

			if(msBlk->swap) {
				squashfs_ipc_inode_header_1 sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_IPC_INODE_HEADER_1(&inodep, &sinodep);
			} else	
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = 0;
			i->i_mode |= (inodep.type == SQUASHFS_FIFO_TYPE) ? S_IFIFO : S_IFSOCK;
			i->i_uid = msBlk->uid[inodep.offset * 16 + inodeb.uid];
			init_special_inode(i, i->i_mode, 0);
			break;
		 }
		 default:
			ERROR("Unknown inode type %d in squashfs_iget!\n", inodeb.inode_type);
				goto failed_read1;
	}
	
	if(inodeb.guid == 15)
		i->i_gid = i->i_uid;
	else
		i->i_gid = msBlk->guid[inodeb.guid];

	insert_inode_hash(i);
	return i;

failed_read:
	ERROR("Unable to read inode [%x:%x]\n", block, offset);

failed_read1:
	return NULL;
}
#endif


static struct inode *squashfs_iget(struct super_block *s, squashfs_inode inode)
{
	struct inode *i = new_inode(s);
	squashfs_sb_info *msBlk = (squashfs_sb_info *)s->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	unsigned int block = SQUASHFS_INODE_BLK(inode) + sBlk->inode_table_start;
	unsigned int offset = SQUASHFS_INODE_OFFSET(inode);
	unsigned int next_block, next_offset;
	squashfs_base_inode_header inodeb;

	TRACE("Entered squashfs_iget\n");

	if(msBlk->swap) {
		squashfs_base_inode_header sinodeb;

		if(!squashfs_get_cached_block(s, (char *) &sinodeb, block,  offset,
					sizeof(sinodeb), &next_block, &next_offset))
			goto failed_read;
		SQUASHFS_SWAP_BASE_INODE_HEADER(&inodeb, &sinodeb, sizeof(sinodeb));
	} else
		if(!squashfs_get_cached_block(s, (char *) &inodeb, block,  offset,
					sizeof(inodeb), &next_block, &next_offset))
			goto failed_read;

	i->i_nlink = 1;

	i->i_mtime.tv_sec = sBlk->mkfs_time;
	i->i_atime.tv_sec = sBlk->mkfs_time;
	i->i_ctime.tv_sec = sBlk->mkfs_time;

	i->i_uid = msBlk->uid[inodeb.uid];
	i->i_ino = SQUASHFS_MK_VFS_INODE(block - sBlk->inode_table_start, offset);

	i->i_mode = inodeb.mode;

	switch(inodeb.inode_type) {
		case SQUASHFS_FILE_TYPE: {
			squashfs_reg_inode_header inodep;

			if(msBlk->swap) {
				squashfs_reg_inode_header sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_REG_INODE_HEADER(&inodep, &sinodep);
			} else
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			SQUASHFS_I(i)->u.s1.fragment_start_block = SQUASHFS_INVALID_BLK;
			if(inodep.fragment != SQUASHFS_INVALID_BLK && !get_fragment_location(s, inodep.fragment,
							&SQUASHFS_I(i)->u.s1.fragment_start_block, &SQUASHFS_I(i)->u.s1.fragment_size))
				goto failed_read;

			SQUASHFS_I(i)->u.s1.fragment_offset = inodep.offset;
			i->i_size = inodep.file_size;
			i->i_fop = &generic_ro_fops;
			if(sBlk->block_size > 4096)
				i->i_data.a_ops = &squashfs_aops;
			else
				i->i_data.a_ops = &squashfs_aops_4K;
			i->i_mode |= S_IFREG;
			i->i_mtime.tv_sec = inodep.mtime;
			i->i_atime.tv_sec = inodep.mtime;
			i->i_ctime.tv_sec = inodep.mtime;
			i->i_blocks = ((i->i_size - 1) >> 9) + 1;
			SQUASHFS_I(i)->start_block = inodep.start_block;
			SQUASHFS_I(i)->block_list_start = next_block;
			SQUASHFS_I(i)->offset = next_offset;
			TRACE("File inode %x:%x, start_block %x, block_list_start %x, offset %x\n",
					SQUASHFS_INODE_BLK(inode), offset, inodep.start_block, next_block, next_offset);
			break;
		}
		case SQUASHFS_DIR_TYPE: {
			squashfs_dir_inode_header inodep;

			if(msBlk->swap) {
				squashfs_dir_inode_header sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_DIR_INODE_HEADER(&inodep, &sinodep);
			} else
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = inodep.file_size;
			i->i_op = &squashfs_dir_inode_ops;
			i->i_fop = &squashfs_dir_ops;
			i->i_mode |= S_IFDIR;
			i->i_mtime.tv_sec = inodep.mtime;
			i->i_atime.tv_sec = inodep.mtime;
			i->i_ctime.tv_sec = inodep.mtime;
			SQUASHFS_I(i)->start_block = inodep.start_block;
			SQUASHFS_I(i)->offset = inodep.offset;
			SQUASHFS_I(i)->u.s2.directory_index_count = 0;
			TRACE("Directory inode %x:%x, start_block %x, offset %x\n", SQUASHFS_INODE_BLK(inode), offset,
					inodep.start_block, inodep.offset);
			break;
		}
		case SQUASHFS_LDIR_TYPE: {
			squashfs_ldir_inode_header inodep;

			if(msBlk->swap) {
				squashfs_ldir_inode_header sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_LDIR_INODE_HEADER(&inodep, &sinodep);
			} else
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = inodep.file_size;
			i->i_op = &squashfs_dir_inode_ops;
			i->i_fop = &squashfs_dir_ops;
			i->i_mode |= S_IFDIR;
			i->i_mtime.tv_sec = inodep.mtime;
			i->i_atime.tv_sec = inodep.mtime;
			i->i_ctime.tv_sec = inodep.mtime;
			SQUASHFS_I(i)->start_block = inodep.start_block;
			SQUASHFS_I(i)->offset = inodep.offset;
			SQUASHFS_I(i)->u.s2.directory_index_start = next_block;
			SQUASHFS_I(i)->u.s2.directory_index_offset = next_offset;
			SQUASHFS_I(i)->u.s2.directory_index_count = inodep.i_count;
			TRACE("Long directory inode %x:%x, start_block %x, offset %x\n", SQUASHFS_INODE_BLK(inode), offset,
					inodep.start_block, inodep.offset);
			break;
		}
		case SQUASHFS_SYMLINK_TYPE: {
			squashfs_symlink_inode_header inodep;
	
			if(msBlk->swap) {
				squashfs_symlink_inode_header sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_SYMLINK_INODE_HEADER(&inodep, &sinodep);
			} else
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = inodep.symlink_size;
			i->i_op = &page_symlink_inode_operations;
			i->i_data.a_ops = &squashfs_symlink_aops;
			i->i_mode |= S_IFLNK;
			SQUASHFS_I(i)->start_block = next_block;
			SQUASHFS_I(i)->offset = next_offset;
			TRACE("Symbolic link inode %x:%x, start_block %x, offset %x\n",
				SQUASHFS_INODE_BLK(inode), offset, next_block, next_offset);
			break;
		 }
		 case SQUASHFS_BLKDEV_TYPE:
		 case SQUASHFS_CHRDEV_TYPE: {
			squashfs_dev_inode_header inodep;

			if(msBlk->swap) {
				squashfs_dev_inode_header sinodep;

				if(!squashfs_get_cached_block(s, (char *) &sinodep, block,  offset, sizeof(sinodep),
							&next_block, &next_offset))
					goto failed_read;
				SQUASHFS_SWAP_DEV_INODE_HEADER(&inodep, &sinodep);
			} else	
				if(!squashfs_get_cached_block(s, (char *) &inodep, block,  offset, sizeof(inodep),
							&next_block, &next_offset))
					goto failed_read;

			i->i_size = 0;
			i->i_mode |= (inodeb.inode_type == SQUASHFS_CHRDEV_TYPE) ? S_IFCHR : S_IFBLK;
			init_special_inode(i, i->i_mode, old_decode_dev(inodep.rdev));
			TRACE("Device inode %x:%x, rdev %x\n", SQUASHFS_INODE_BLK(inode), offset, inodep.rdev);
			break;
		 }
		 case SQUASHFS_FIFO_TYPE:
		 case SQUASHFS_SOCKET_TYPE: {
			i->i_size = 0;
			i->i_mode |= (inodeb.inode_type == SQUASHFS_FIFO_TYPE) ? S_IFIFO : S_IFSOCK;
			init_special_inode(i, i->i_mode, 0);
			break;
		 }
		 default:
			ERROR("Unknown inode type %d in squashfs_iget!\n", inodeb.inode_type);
				goto failed_read1;
	}
	
	if(inodeb.guid == SQUASHFS_GUIDS)
		i->i_gid = i->i_uid;
	else
		i->i_gid = msBlk->guid[inodeb.guid];

	insert_inode_hash(i);
	return i;

failed_read:
	ERROR("Unable to read inode [%x:%x]\n", block, offset);

failed_read1:
	return NULL;
}


static int squashfs_fill_super(struct super_block *s,
		void *data, int silent)
{
	squashfs_sb_info *msBlk;
	squashfs_super_block *sBlk;
	int i;
	char b[BDEVNAME_SIZE];

	TRACE("Entered squashfs_read_superblock\n");

	if(!(s->s_fs_info = (void *) kmalloc(sizeof(squashfs_sb_info), GFP_KERNEL))) {
		ERROR("Failed to allocate superblock\n");
		return -ENOMEM;
	}
	msBlk = (squashfs_sb_info *) s->s_fs_info;
	sBlk = &msBlk->sBlk;
	
	msBlk->devblksize = sb_min_blocksize(s, BLOCK_SIZE);
	msBlk->devblksize_log2 = ffz(~msBlk->devblksize);

	init_MUTEX(&msBlk->read_page_mutex);
	init_MUTEX(&msBlk->block_cache_mutex);
	init_MUTEX(&msBlk->fragment_mutex);
	
	init_waitqueue_head(&msBlk->waitq);
	init_waitqueue_head(&msBlk->fragment_wait_queue);

	if(!read_data(s, (char *) sBlk, SQUASHFS_START, sizeof(squashfs_super_block) | SQUASHFS_COMPRESSED_BIT_BLOCK, NULL)) {
		SERROR("unable to read superblock\n");
		goto failed_mount;
	}

	/* Check it is a SQUASHFS superblock */
	msBlk->swap = 0;
	if((s->s_magic = sBlk->s_magic) != SQUASHFS_MAGIC) {
		if(sBlk->s_magic == SQUASHFS_MAGIC_SWAP) {
			squashfs_super_block sblk;
			WARNING("Mounting a different endian SQUASHFS filesystem on %s\n", bdevname(s->s_bdev, b));
			SQUASHFS_SWAP_SUPER_BLOCK(&sblk, sBlk);
			memcpy(sBlk, &sblk, sizeof(squashfs_super_block));
			msBlk->swap = 1;
		} else  {
			SERROR("Can't find a SQUASHFS superblock on %s\n", bdevname(s->s_bdev, b));
			goto failed_mount;
		}
	}

	/* Check the MAJOR & MINOR versions */
#ifdef SQUASHFS_1_0_COMPATIBILITY
	if((sBlk->s_major != 1) && (sBlk->s_major != 2 || sBlk->s_minor > SQUASHFS_MINOR)) {
		SERROR("Major/Minor mismatch, filesystem is (%d:%d), I support (1 : x) or (2 : <= %d)\n",
				sBlk->s_major, sBlk->s_minor, SQUASHFS_MINOR);
		goto failed_mount;
	}
	if(sBlk->s_major == 1)
		sBlk->block_size = sBlk->block_size_1;
#else
	if(sBlk->s_major != SQUASHFS_MAJOR || sBlk->s_minor > SQUASHFS_MINOR) {
		SERROR("Major/Minor mismatch, filesystem is (%d:%d), I support (%d: <= %d)\n",
				sBlk->s_major, sBlk->s_minor, SQUASHFS_MAJOR, SQUASHFS_MINOR);
		goto failed_mount;
	}
#endif

	TRACE("Found valid superblock on %s\n", bdevname(s->s_bdev, b));
	TRACE("Inodes are %scompressed\n", SQUASHFS_UNCOMPRESSED_INODES(sBlk->flags) ? "un" : "");
	TRACE("Data is %scompressed\n", SQUASHFS_UNCOMPRESSED_DATA(sBlk->flags) ? "un" : "");
	TRACE("Check data is %s present in the filesystem\n", SQUASHFS_CHECK_DATA(sBlk->flags) ? "" : "not");
	TRACE("Filesystem size %d bytes\n", sBlk->bytes_used);
	TRACE("Block size %d\n", sBlk->block_size);
	TRACE("Number of inodes %d\n", sBlk->inodes);
	if(sBlk->s_major > 1)
		TRACE("Number of fragments %d\n", sBlk->fragments);
	TRACE("Number of uids %d\n", sBlk->no_uids);
	TRACE("Number of gids %d\n", sBlk->no_guids);
	TRACE("sBlk->inode_table_start %x\n", sBlk->inode_table_start);
	TRACE("sBlk->directory_table_start %x\n", sBlk->directory_table_start);
		if(sBlk->s_major > 1)
	TRACE("sBlk->fragment_table_start %x\n", sBlk->fragment_table_start);
	TRACE("sBlk->uid_start %x\n", sBlk->uid_start);

	s->s_flags |= MS_RDONLY;
	s->s_op = &squashfs_ops;

	/* Init inode_table block pointer array */
	if(!(msBlk->block_cache = (squashfs_cache *) kmalloc(sizeof(squashfs_cache) * SQUASHFS_CACHED_BLKS, GFP_KERNEL))) {
		ERROR("Failed to allocate block cache\n");
		goto failed_mount;
	}

	for(i = 0; i < SQUASHFS_CACHED_BLKS; i++)
		msBlk->block_cache[i].block = SQUASHFS_INVALID_BLK;

	msBlk->next_cache = 0;

	/* Allocate read_data block */
	msBlk->read_size = (sBlk->block_size < SQUASHFS_METADATA_SIZE) ? SQUASHFS_METADATA_SIZE : sBlk->block_size;
	if(!(msBlk->read_data = (char *) kmalloc(msBlk->read_size, GFP_KERNEL))) {
		ERROR("Failed to allocate read_data block\n");
		goto failed_mount1;
	}

	/* Allocate read_page block */
	if(sBlk->block_size > PAGE_CACHE_SIZE) {
		if(!(msBlk->read_page = (char *) kmalloc(sBlk->block_size, GFP_KERNEL))) {
			ERROR("Failed to allocate read_page block\n");
			goto failed_mount2;
		}
	} else
		msBlk->read_page = NULL;

	/* Allocate uid and gid tables */
	if(!(msBlk->uid = (squashfs_uid *) kmalloc((sBlk->no_uids +
		sBlk->no_guids) * sizeof(squashfs_uid), GFP_KERNEL))) {
		ERROR("Failed to allocate uid/gid table\n");
		goto failed_mount3;
	}
	msBlk->guid = msBlk->uid + sBlk->no_uids;
   
	if(msBlk->swap) {
		squashfs_uid suid[sBlk->no_uids + sBlk->no_guids];

		if(!read_data(s, (char *) &suid, sBlk->uid_start, ((sBlk->no_uids + sBlk->no_guids) *
				sizeof(squashfs_uid)) | SQUASHFS_COMPRESSED_BIT_BLOCK, NULL)) {
			SERROR("unable to read uid/gid table\n");
			goto failed_mount4;
		}
		SQUASHFS_SWAP_DATA(msBlk->uid, suid, (sBlk->no_uids + sBlk->no_guids), (sizeof(squashfs_uid) * 8));
	} else
		if(!read_data(s, (char *) msBlk->uid, sBlk->uid_start, ((sBlk->no_uids + sBlk->no_guids) *
				sizeof(squashfs_uid)) | SQUASHFS_COMPRESSED_BIT_BLOCK, NULL)) {
			SERROR("unable to read uid/gid table\n");
			goto failed_mount4;
		}


#ifdef SQUASHFS_1_0_COMPATIBILITY
	if(sBlk->s_major == 1) {
		msBlk->iget = squashfs_iget_1;
		msBlk->read_blocklist = read_blocklist_1;
		msBlk->fragment = NULL;
		msBlk->fragment_index = NULL;
		goto allocate_root;
	}
#endif
	msBlk->iget = squashfs_iget;
	msBlk->read_blocklist = read_blocklist;

	if(!(msBlk->fragment = (struct squashfs_fragment_cache *) kmalloc(sizeof(struct squashfs_fragment_cache) * SQUASHFS_CACHED_FRAGMENTS, GFP_KERNEL))) {
		ERROR("Failed to allocate fragment block cache\n");
		goto failed_mount4;
	}

	for(i = 0; i < SQUASHFS_CACHED_FRAGMENTS; i++) {
		msBlk->fragment[i].locked = 0;
		msBlk->fragment[i].block = SQUASHFS_INVALID_BLK;
		msBlk->fragment[i].data = NULL;
	}

	msBlk->next_fragment = 0;

	/* Allocate fragment index table */
	if(!(msBlk->fragment_index = (squashfs_fragment_index *) kmalloc(SQUASHFS_FRAGMENT_INDEX_BYTES(sBlk->fragments), GFP_KERNEL))) {
		ERROR("Failed to allocate uid/gid table\n");
		goto failed_mount5;
	}
   
	if(SQUASHFS_FRAGMENT_INDEX_BYTES(sBlk->fragments) &&
	 	!read_data(s, (char *) msBlk->fragment_index, sBlk->fragment_table_start,
		SQUASHFS_FRAGMENT_INDEX_BYTES(sBlk->fragments) | SQUASHFS_COMPRESSED_BIT_BLOCK, NULL)) {
			SERROR("unable to read fragment index table\n");
			goto failed_mount6;
	}

	if(msBlk->swap) {
		int i;
		squashfs_fragment_index fragment;

		for(i = 0; i < SQUASHFS_FRAGMENT_INDEXES(sBlk->fragments); i++) {
			SQUASHFS_SWAP_FRAGMENT_INDEXES((&fragment), &msBlk->fragment_index[i], 1);
			msBlk->fragment_index[i] = fragment;
		}
	}

#ifdef SQUASHFS_1_0_COMPATIBILITY
allocate_root:
#endif
	if(!(s->s_root = d_alloc_root((msBlk->iget)(s, sBlk->root_inode)))) {
		ERROR("Root inode create failed\n");
		goto failed_mount5;
	}

	TRACE("Leaving squashfs_read_super\n");
	return 0;

failed_mount6:
	kfree(msBlk->fragment_index);
failed_mount5:
	kfree(msBlk->fragment);
failed_mount4:
	kfree(msBlk->uid);
failed_mount3:
	kfree(msBlk->read_page);
failed_mount2:
	kfree(msBlk->read_data);
failed_mount1:
	kfree(msBlk->block_cache);
failed_mount:
	kfree(s->s_fs_info);
	s->s_fs_info = NULL;
	return -EINVAL;
}


static int squashfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *s = dentry->d_sb;
	squashfs_super_block *sBlk = &((squashfs_sb_info *)s->s_fs_info)->sBlk;

	TRACE("Entered squashfs_statfs\n");
	buf->f_type = SQUASHFS_MAGIC;
	buf->f_bsize = sBlk->block_size;
	buf->f_blocks = ((sBlk->bytes_used - 1) >> sBlk->block_log) + 1;
	buf->f_bfree = buf->f_bavail = 0;
	buf->f_files = sBlk->inodes;
	buf->f_ffree = 0;
	buf->f_namelen = SQUASHFS_NAME_LEN;
	return 0;
}


static int squashfs_symlink_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	int index = page->index << PAGE_CACHE_SHIFT, length, bytes;
	unsigned int block = SQUASHFS_I(inode)->start_block;
	int offset = SQUASHFS_I(inode)->offset;
	void *pageaddr = kmap(page);

	TRACE("Entered squashfs_symlink_readpage, page index %d, start block %x, offset %x\n",
		page->index, SQUASHFS_I(inode)->start_block, SQUASHFS_I(inode)->offset);

	for(length = 0; length < index; length += bytes) {
		if(!(bytes = squashfs_get_cached_block(inode->i_sb, NULL, block, offset,
					PAGE_CACHE_SIZE, &block, &offset))) {
			ERROR("Unable to read symbolic link [%x:%x]\n", block, offset);
			goto skip_read;
		}
	}

	if(length != index) {
		ERROR("(squashfs_symlink_readpage) length != index\n");
		bytes = 0;
		goto skip_read;
	}

	bytes = (inode->i_size - length) > PAGE_CACHE_SIZE ? PAGE_CACHE_SIZE : inode->i_size - length;
	if(!(bytes = squashfs_get_cached_block(inode->i_sb, pageaddr, block, offset, bytes, &block, &offset)))
		ERROR("Unable to read symbolic link [%x:%x]\n", block, offset);

skip_read:
	memset(pageaddr + bytes, 0, PAGE_CACHE_SIZE - bytes);
	kunmap(page);
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);

	return 0;
}


#define SIZE 256

#ifdef SQUASHFS_1_0_COMPATIBILITY
static unsigned int read_blocklist_1(struct inode *inode, int index, int readahead_blks,
		char *block_list, unsigned short **block_p, unsigned int *bsize)
{
	squashfs_sb_info *msBlk = (squashfs_sb_info *)inode->i_sb->s_fs_info;
	unsigned short *block_listp;
	int i = 0;
	int block_ptr = SQUASHFS_I(inode)->block_list_start;
	int offset = SQUASHFS_I(inode)->offset;
	unsigned int block = SQUASHFS_I(inode)->start_block;

	for(;;) {
		int blocks = (index + readahead_blks - i);
		if(blocks > (SIZE >> 1)) {
			if((index - i) <= (SIZE >> 1))
				blocks = index - i;
			else
				blocks = SIZE >> 1;
		}

		if(msBlk->swap) {
			unsigned char sblock_list[SIZE];
			if(!squashfs_get_cached_block(inode->i_sb, (char *) sblock_list, block_ptr, offset, blocks << 1, &block_ptr, &offset)) {
				ERROR("Unable to read block list [%d:%x]\n", block_ptr, offset);
				return 0;
			}
			SQUASHFS_SWAP_SHORTS(((unsigned short *)block_list), ((unsigned short *)sblock_list), blocks);
		} else
			if(!squashfs_get_cached_block(inode->i_sb, (char *) block_list, block_ptr, offset, blocks << 1, &block_ptr, &offset)) {
				ERROR("Unable to read block list [%d:%x]\n", block_ptr, offset);
				return 0;
			}
		for(block_listp = (unsigned short *) block_list; i < index && blocks; i ++, block_listp ++, blocks --)
			block += SQUASHFS_COMPRESSED_SIZE(*block_listp);
		if(blocks >= readahead_blks)
			break;
	}

	if(bsize)
		*bsize = SQUASHFS_COMPRESSED_SIZE(*block_listp) | (!SQUASHFS_COMPRESSED(*block_listp) ? SQUASHFS_COMPRESSED_BIT_BLOCK : 0);
	else
		*block_p = block_listp;
	return block;
}
#endif


static unsigned int read_blocklist(struct inode *inode, int index, int readahead_blks,
		char *block_list, unsigned short **block_p, unsigned int *bsize)
{
	squashfs_sb_info *msBlk = (squashfs_sb_info *)inode->i_sb->s_fs_info;
	unsigned int *block_listp;
	int i = 0;
	int block_ptr = SQUASHFS_I(inode)->block_list_start;
	int offset = SQUASHFS_I(inode)->offset;
	unsigned int block = SQUASHFS_I(inode)->start_block;

	for(;;) {
		int blocks = (index + readahead_blks - i);
		if(blocks > (SIZE >> 2)) {
			if((index - i) <= (SIZE >> 2))
				blocks = index - i;
			else
				blocks = SIZE >> 2;
		}

		if(msBlk->swap) {
			unsigned char sblock_list[SIZE];
			if(!squashfs_get_cached_block(inode->i_sb, (char *) sblock_list, block_ptr, offset, blocks << 2, &block_ptr, &offset)) {
				ERROR("Unable to read block list [%d:%x]\n", block_ptr, offset);
				return 0;
			}
			SQUASHFS_SWAP_INTS(((unsigned int *)block_list), ((unsigned int *)sblock_list), blocks);
		} else
			if(!squashfs_get_cached_block(inode->i_sb, (char *) block_list, block_ptr, offset, blocks << 2, &block_ptr, &offset)) {
				ERROR("Unable to read block list [%d:%x]\n", block_ptr, offset);
				return 0;
			}
		for(block_listp = (unsigned int *) block_list; i < index && blocks; i ++, block_listp ++, blocks --)
			block += SQUASHFS_COMPRESSED_SIZE_BLOCK(*block_listp);
		if(blocks >= readahead_blks)
			break;
	}

	*bsize = *block_listp;
	return block;
}


static int squashfs_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	squashfs_sb_info *msBlk = (squashfs_sb_info *)inode->i_sb->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	unsigned char block_list[SIZE];
	unsigned int bsize, block, i = 0, bytes = 0, byte_offset = 0;
	int index = page->index >> (sBlk->block_log - PAGE_CACHE_SHIFT);
 	void *pageaddr = kmap(page);
	struct squashfs_fragment_cache *fragment = NULL;
	char *data_ptr = msBlk->read_page;
	
	int mask = (1 << (sBlk->block_log - PAGE_CACHE_SHIFT)) - 1;
	int start_index = page->index & ~mask;
	int end_index = start_index | mask;

	TRACE("Entered squashfs_readpage, page index %x, start block %x\n", (unsigned int) page->index,
		SQUASHFS_I(inode)->start_block);

	if(page->index >= ((inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)) {
		goto skip_read;
	}

	if(SQUASHFS_I(inode)->u.s1.fragment_start_block == SQUASHFS_INVALID_BLK || index < (inode->i_size >> sBlk->block_log)) {
		if((block = (msBlk->read_blocklist)(inode, index, 1, block_list, NULL, &bsize)) == 0)
			goto skip_read;

		down(&msBlk->read_page_mutex);
		if(!(bytes = read_data(inode->i_sb, msBlk->read_page, block, bsize, NULL))) {
			ERROR("Unable to read page, block %x, size %x\n", block, bsize);
			up(&msBlk->read_page_mutex);
			goto skip_read;
		}
	} else {
		if((fragment = get_cached_fragment(inode->i_sb, SQUASHFS_I(inode)->u.s1.fragment_start_block, SQUASHFS_I(inode)->u.s1.fragment_size)) == NULL) {
			ERROR("Unable to read page, block %x, size %x\n", SQUASHFS_I(inode)->u.s1.fragment_start_block, (int) SQUASHFS_I(inode)->u.s1.fragment_size);
			goto skip_read;
		}
		bytes = SQUASHFS_I(inode)->u.s1.fragment_offset + (inode->i_size & (sBlk->block_size - 1));
		byte_offset = SQUASHFS_I(inode)->u.s1.fragment_offset;
		data_ptr = fragment->data;
	}

	for(i = start_index; i <= end_index && byte_offset < bytes; i++, byte_offset += PAGE_CACHE_SIZE) {
		struct page *push_page;
		int available_bytes = (bytes - byte_offset) > PAGE_CACHE_SIZE ? PAGE_CACHE_SIZE : bytes - byte_offset;

		TRACE("bytes %d, i %d, byte_offset %d, available_bytes %d\n", bytes, i, byte_offset, available_bytes);

		if(i == page->index)  {
			memcpy(pageaddr, data_ptr + byte_offset, available_bytes);
			memset(pageaddr + available_bytes, 0, PAGE_CACHE_SIZE - available_bytes);
			kunmap(page);
			flush_dcache_page(page);
			SetPageUptodate(page);
			unlock_page(page);
		} else if((push_page = grab_cache_page_nowait(page->mapping, i))) {
 			void *pageaddr = kmap(push_page);
			memcpy(pageaddr, data_ptr + byte_offset, available_bytes);
			memset(pageaddr + available_bytes, 0, PAGE_CACHE_SIZE - available_bytes);
			kunmap(push_page);
			flush_dcache_page(push_page);
			SetPageUptodate(push_page);
			unlock_page(push_page);
			page_cache_release(push_page);
		}
	}

	if(SQUASHFS_I(inode)->u.s1.fragment_start_block == SQUASHFS_INVALID_BLK || index < (inode->i_size >> sBlk->block_log))
		up(&msBlk->read_page_mutex);
	else
		release_cached_fragment(msBlk, fragment);

	return 0;

skip_read:
	memset(pageaddr + bytes, 0, PAGE_CACHE_SIZE - bytes);
	kunmap(page);
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);

	return 0;
}


static int squashfs_readpage4K(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	squashfs_sb_info *msBlk = (squashfs_sb_info *)inode->i_sb->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	unsigned char block_list[SIZE];
	unsigned int bsize, block, bytes = 0;
 	void *pageaddr = kmap(page);
	
	TRACE("Entered squashfs_readpage4K, page index %x, start block %x\n", (unsigned int) page->index,
		SQUASHFS_I(inode)->start_block);

	if(page->index >= ((inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)) {
		goto skip_read;
	}

	if(SQUASHFS_I(inode)->u.s1.fragment_start_block == SQUASHFS_INVALID_BLK || page->index < (inode->i_size >> sBlk->block_log)) {
		block = (msBlk->read_blocklist)(inode, page->index, 1, block_list, NULL, &bsize);

		if(!(bytes = read_data(inode->i_sb, pageaddr, block, bsize, NULL)))
			ERROR("Unable to read page, block %x, size %x\n", block, bsize);
	} else {
		struct squashfs_fragment_cache *fragment;

		if((fragment = get_cached_fragment(inode->i_sb, SQUASHFS_I(inode)->u.s1.fragment_start_block, SQUASHFS_I(inode)->u.s1.fragment_size)) == NULL)
			ERROR("Unable to read page, block %x, size %x\n", SQUASHFS_I(inode)->u.s1.fragment_start_block, (int) SQUASHFS_I(inode)->u.s1.fragment_size);
		else {
			bytes = inode->i_size & (sBlk->block_size - 1);
			memcpy(pageaddr, fragment->data + SQUASHFS_I(inode)->u.s1.fragment_offset, bytes);
			release_cached_fragment(msBlk, fragment);
		}
	}

skip_read:
	memset(pageaddr + bytes, 0, PAGE_CACHE_SIZE - bytes);
	kunmap(page);
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);

	return 0;
}


#ifdef SQUASHFS_1_0_COMPATIBILITY
static int squashfs_readpage_lessthan4K(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	squashfs_sb_info *msBlk = (squashfs_sb_info *)inode->i_sb->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	unsigned char block_list[SIZE];
	unsigned short *block_listp, block, bytes = 0;
	int index = page->index << (PAGE_CACHE_SHIFT - sBlk->block_log);
	int file_blocks = ((inode->i_size - 1) >> sBlk->block_log) + 1;
	int readahead_blks = 1 << (PAGE_CACHE_SHIFT - sBlk->block_log);
 	void *pageaddr = kmap(page);
	
	int i_end = index + (1 << (PAGE_CACHE_SHIFT - sBlk->block_log));
	int byte;

	TRACE("Entered squashfs_readpage_lessthan4K, page index %x, start block %x\n", (unsigned int) page->index,
		SQUASHFS_I(inode)->start_block);

	block = read_blocklist_1(inode, index, readahead_blks, block_list, &block_listp, NULL);

	if(i_end > file_blocks)
		i_end = file_blocks;

	while(index < i_end) {
		int c_byte = !SQUASHFS_COMPRESSED(*block_listp) ? SQUASHFS_COMPRESSED_SIZE(*block_listp) | SQUASHFS_COMPRESSED_BIT_BLOCK : *block_listp;
		if(!(byte = read_data(inode->i_sb, pageaddr, block, c_byte, NULL))) {
			ERROR("Unable to read page, block %x, size %x\n", block, *block_listp);
			goto skip_read;
		}
		block += SQUASHFS_COMPRESSED_SIZE(*block_listp);
		pageaddr += byte;
		bytes += byte;
		index ++;
		block_listp ++;
	}

skip_read:
	memset(pageaddr, 0, PAGE_CACHE_SIZE - bytes);
	kunmap(page);
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);

	return 0;
}
#endif


static int get_dir_index_using_offset(struct super_block *s, unsigned int *next_block,
	unsigned int *next_offset, unsigned int index_start, unsigned int index_offset,
	int i_count, long long f_pos)
{
	squashfs_sb_info *msBlk = (squashfs_sb_info *)s->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	int i, length = 0;
	squashfs_dir_index index;

	TRACE("Entered get_dir_index_using_offset, i_count %d, f_pos %d\n", i_count, (unsigned int) f_pos);

	if(f_pos == 0)
		return 0;

	for(i = 0; i < i_count; i++) {
		if(msBlk->swap) {
			squashfs_dir_index sindex;
			squashfs_get_cached_block(s, (char *) &sindex, index_start, index_offset,
				sizeof(sindex), &index_start, &index_offset);
			SQUASHFS_SWAP_DIR_INDEX(&index, &sindex);
		} else
			squashfs_get_cached_block(s, (char *) &index, index_start, index_offset,
				sizeof(index), &index_start, &index_offset);

		if(index.index > f_pos)
			break;

		squashfs_get_cached_block(s, NULL, index_start, index_offset,
				index.size + 1, &index_start, &index_offset);

		length = index.index;
		*next_block = index.start_block + sBlk->directory_table_start;
	}

	*next_offset = (length + *next_offset) % SQUASHFS_METADATA_SIZE;
	return length;
}


static int get_dir_index_using_name(struct super_block *s, unsigned int *next_block,
	unsigned int *next_offset, unsigned int index_start, unsigned int index_offset,
	int i_count, const char *name, int size)
{
	squashfs_sb_info *msBlk = (squashfs_sb_info *)s->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	int i, length = 0;
	char buffer[sizeof(squashfs_dir_index) + SQUASHFS_NAME_LEN + 1];
	squashfs_dir_index *index = (squashfs_dir_index *) buffer;
	char str[SQUASHFS_NAME_LEN + 1];

	TRACE("Entered get_dir_index_using_name, i_count %d\n", i_count);
	
	if (size > SQUASHFS_NAME_LEN) {
		ERROR("Filename length %d > SQUASHFS_NAME_LEN\n", size);
		size = SQUASHFS_NAME_LEN;
	}

	strncpy(str, name, size);
	str[size] = '\0';

	for(i = 0; i < i_count; i++) {
		if(msBlk->swap) {
			squashfs_dir_index sindex;
			squashfs_get_cached_block(s, (char *) &sindex, index_start, index_offset,
				sizeof(sindex), &index_start, &index_offset);
			SQUASHFS_SWAP_DIR_INDEX(index, &sindex);
		} else
			squashfs_get_cached_block(s, (char *) index, index_start, index_offset,
				sizeof(squashfs_dir_index), &index_start, &index_offset);

		squashfs_get_cached_block(s, index->name, index_start, index_offset,
				index->size + 1, &index_start, &index_offset);

		index->name[index->size + 1] = '\0';

		if(strcmp(index->name, str) > 0)
			break;

		length = index->index;
		*next_block = index->start_block + sBlk->directory_table_start;
	}

	*next_offset = (length + *next_offset) % SQUASHFS_METADATA_SIZE;
	return length;
}

		
static int squashfs_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct inode *i = file->f_dentry->d_inode;
	squashfs_sb_info *msBlk = (squashfs_sb_info *)i->i_sb->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	int next_block = SQUASHFS_I(i)->start_block + sBlk->directory_table_start, next_offset =
		SQUASHFS_I(i)->offset, length = 0, dirs_read = 0, dir_count;
	squashfs_dir_header dirh;
	char buffer[sizeof(squashfs_dir_entry) + SQUASHFS_NAME_LEN + 1];
	squashfs_dir_entry *dire = (squashfs_dir_entry *) buffer;

	TRACE("Entered squashfs_readdir [%x:%x]\n", next_block, next_offset);

	lock_kernel();

	length = get_dir_index_using_offset(i->i_sb, &next_block, &next_offset, SQUASHFS_I(i)->u.s2.directory_index_start,
		SQUASHFS_I(i)->u.s2.directory_index_offset, SQUASHFS_I(i)->u.s2.directory_index_count, file->f_pos);

	while(length < i->i_size) {
		/* read directory header */
		if(msBlk->swap) {
			squashfs_dir_header sdirh;
			if(!squashfs_get_cached_block(i->i_sb, (char *) &sdirh, next_block,
						next_offset, sizeof(sdirh), &next_block, &next_offset))
				goto failed_read;
			length += sizeof(sdirh);
			SQUASHFS_SWAP_DIR_HEADER(&dirh, &sdirh);
		} else {
			if(!squashfs_get_cached_block(i->i_sb, (char *) &dirh, next_block,
						next_offset, sizeof(dirh), &next_block, &next_offset))
				goto failed_read;
			length += sizeof(dirh);
		}

		dir_count = dirh.count + 1;
		while(dir_count--) {
			if(msBlk->swap) {
				squashfs_dir_entry sdire;
				if(!squashfs_get_cached_block(i->i_sb, (char *) &sdire, next_block,
							next_offset, sizeof(sdire), &next_block, &next_offset))
					goto failed_read;
				length += sizeof(sdire);
				SQUASHFS_SWAP_DIR_ENTRY(dire, &sdire);
			} else {
				if(!squashfs_get_cached_block(i->i_sb, (char *) dire, next_block,
							next_offset, sizeof(*dire), &next_block, &next_offset))
					goto failed_read;
				length += sizeof(*dire);
			}

			if(!squashfs_get_cached_block(i->i_sb, dire->name, next_block,
						next_offset, dire->size + 1, &next_block, &next_offset))
				goto failed_read;
			length += dire->size + 1;

			if(file->f_pos >= length)
				continue;

			dire->name[dire->size + 1] = '\0';

			TRACE("Calling filldir(%x, %s, %d, %d, %x:%x, %d)\n", (unsigned int) dirent,
			dire->name, dire->size + 1, (int) file->f_pos,
			dirh.start_block, dire->offset, squashfs_filetype_table[dire->type]);

			if(filldir(dirent, dire->name, dire->size + 1, file->f_pos, SQUASHFS_MK_VFS_INODE(dirh.start_block,
							dire->offset), squashfs_filetype_table[dire->type]) < 0) {
				TRACE("Filldir returned less than 0\n");
				unlock_kernel();
				return dirs_read;
			}

			file->f_pos = length;
			dirs_read ++;
		}
	}

	unlock_kernel();
	return dirs_read;

failed_read:
	unlock_kernel();
	ERROR("Unable to read directory block [%x:%x]\n", next_block, next_offset);
	return 0;
}


static struct dentry *squashfs_lookup(struct inode *i, struct dentry *dentry, struct nameidata *nd)
{
	const unsigned char *name =dentry->d_name.name;
	int len = dentry->d_name.len;
	struct inode *inode = NULL;
	squashfs_sb_info *msBlk = (squashfs_sb_info *)i->i_sb->s_fs_info;
	squashfs_super_block *sBlk = &msBlk->sBlk;
	int next_block = SQUASHFS_I(i)->start_block + sBlk->directory_table_start, next_offset =
		SQUASHFS_I(i)->offset, length = 0, dir_count;
	squashfs_dir_header dirh;
	char buffer[sizeof(squashfs_dir_entry) + SQUASHFS_NAME_LEN];
	squashfs_dir_entry *dire = (squashfs_dir_entry *) buffer;
	int squashfs_2_1 = sBlk->s_major == 2 && sBlk->s_minor == 1;

	TRACE("Entered squashfs_lookup [%x:%x]\n", next_block, next_offset);

	if (len > SQUASHFS_NAME_LEN) return ERR_PTR(-ENAMETOOLONG);

	lock_kernel();

	length = get_dir_index_using_name(i->i_sb, &next_block, &next_offset, SQUASHFS_I(i)->u.s2.directory_index_start,
		SQUASHFS_I(i)->u.s2.directory_index_offset, SQUASHFS_I(i)->u.s2.directory_index_count, name, len);

	while(length < i->i_size) {
		/* read directory header */
		if(msBlk->swap) {
			squashfs_dir_header sdirh;
			if(!squashfs_get_cached_block(i->i_sb, (char *) &sdirh, next_block, next_offset,
						sizeof(sdirh), &next_block, &next_offset))
				goto failed_read;
			length += sizeof(sdirh);
			SQUASHFS_SWAP_DIR_HEADER(&dirh, &sdirh);
		} else {
			if(!squashfs_get_cached_block(i->i_sb, (char *) &dirh, next_block, next_offset,
						sizeof(dirh), &next_block, &next_offset))
				goto failed_read;
			length += sizeof(dirh);
		}

		dir_count = dirh.count + 1;
		while(dir_count--) {
			if(msBlk->swap) {
				squashfs_dir_entry sdire;
				if(!squashfs_get_cached_block(i->i_sb, (char *) &sdire,
							next_block,next_offset, sizeof(sdire), &next_block, &next_offset))
					goto failed_read;
				length += sizeof(sdire);
				SQUASHFS_SWAP_DIR_ENTRY(dire, &sdire);
			} else {
				if(!squashfs_get_cached_block(i->i_sb, (char *) dire,
							next_block,next_offset, sizeof(*dire), &next_block, &next_offset))
					goto failed_read;
				length += sizeof(*dire);
			}

			if(!squashfs_get_cached_block(i->i_sb, dire->name,
						next_block, next_offset, dire->size + 1, &next_block, &next_offset))
				goto failed_read;
			length += dire->size + 1;

			if(squashfs_2_1 && name[0] < dire->name[0])
				goto exit_loop;

			if((len == dire->size + 1) && !strncmp(name, dire->name, len)) {
				squashfs_inode ino = SQUASHFS_MKINODE(dirh.start_block, dire->offset);

				TRACE("calling squashfs_iget for directory entry %s, inode %x:%x\n",
						name, dirh.start_block, dire->offset);

				inode = (msBlk->iget)(i->i_sb, ino);

				goto exit_loop;
			}
		}
	}

exit_loop:
	d_add(dentry, inode);
	unlock_kernel();
	return ERR_PTR(0);

failed_read:
	ERROR("Unable to read directory block [%x:%x]\n", next_block, next_offset);
	goto exit_loop;
}


static void squashfs_put_super(struct super_block *s)
{
	int i;

	if(s->s_fs_info) {
		squashfs_sb_info *sbi = (squashfs_sb_info *) s->s_fs_info;
		if(sbi->block_cache) {
			for(i = 0; i < SQUASHFS_CACHED_BLKS; i++)
				if(sbi->block_cache[i].block != SQUASHFS_INVALID_BLK)
					kfree(sbi->block_cache[i].data);
			kfree(sbi->block_cache);
		}
		if(sbi->read_data) kfree(sbi->read_data);
		if(sbi->read_page) kfree(sbi->read_page);
		if(sbi->uid) kfree(sbi->uid);
		if(sbi->fragment) {
			for(i = 0; i < SQUASHFS_CACHED_FRAGMENTS; i++) 
				if(sbi->fragment[i].data != NULL)
					SQUASHFS_FREE(sbi->fragment[i].data);
			kfree(sbi->fragment);
		}
		if(sbi->fragment_index) kfree(sbi->fragment_index);
		kfree(s->s_fs_info);
		s->s_fs_info = NULL;
	}
}


static int squashfs_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, squashfs_fill_super, mnt);
}


static int __init init_squashfs_fs(void)
{
	int err = init_inodecache();
	if(err)
		return err;

	printk(KERN_INFO "Squashfs 2.2-r2 (released 2005/09/08) (C) 2002-2005 Phillip Lougher\n");

#ifdef CONFIG_SQUASHFS_LZMA
	printk(KERN_INFO "Squashfs 2.2 includes LZMA decompression support\n");
	if(!(lzma_data = (char *) vmalloc(lzma_workspace_size()))) {
		ERROR("Failed to allocate lzma workspace\n");
		return -ENOMEM;
	}
	lzma_init(lzma_data, lzma_workspace_size());
#else
	if(!(stream.workspace = (char *) vmalloc(zlib_inflate_workspacesize()))) {
		ERROR("Failed to allocate zlib workspace\n");
		destroy_inodecache();
		return -ENOMEM;
	}
#endif

	if((err = register_filesystem(&squashfs_fs_type))) {
#ifndef CONFIG_SQUASHFS_LZMA
		vfree(stream.workspace);
#endif
		destroy_inodecache();
	}

	return err;
}


static void __exit exit_squashfs_fs(void)
{
#ifdef CONFIG_SQUASHFS_LZMA
	vfree(lzma_data);
#else
	vfree(stream.workspace);
#endif
	unregister_filesystem(&squashfs_fs_type);
	destroy_inodecache();
}


static kmem_cache_t * squashfs_inode_cachep;


static struct inode *squashfs_alloc_inode(struct super_block *sb)
{
	struct squashfs_inode_info *ei;
	ei = (struct squashfs_inode_info *)kmem_cache_alloc(squashfs_inode_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}


static void squashfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(squashfs_inode_cachep, SQUASHFS_I(inode));
}


static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct squashfs_inode_info *ei = (struct squashfs_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&ei->vfs_inode);
}
 

static int init_inodecache(void)
{
	squashfs_inode_cachep = kmem_cache_create("squashfs_inode_cache",
					     sizeof(struct squashfs_inode_info),
					     0, SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					     init_once, NULL);
	if (squashfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}


static void destroy_inodecache(void)
{
	kmem_cache_destroy(squashfs_inode_cachep);
}


module_init(init_squashfs_fs);
module_exit(exit_squashfs_fs);
MODULE_DESCRIPTION("squashfs, a compressed read-only filesystem");
MODULE_AUTHOR("Phillip Lougher <phillip@lougher.demon.co.uk>");
MODULE_LICENSE("GPL");

/*
 * cpfile.h - NILFS checkpoint file.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by Koji Sato.
 */

#ifndef _NILFS_CPFILE_H
#define _NILFS_CPFILE_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/nilfs2_api.h>		/* nilfs_cpstat */
#include <linux/nilfs2_ondisk.h>	/* nilfs_inode, nilfs_checkpoint */


int nilfs_cpfile_get_checkpoint(struct inode *, __u64, int,
				struct nilfs_checkpoint **,
				struct buffer_head **);
void nilfs_cpfile_put_checkpoint(struct inode *, __u64, struct buffer_head *);
int nilfs_cpfile_delete_checkpoints(struct inode *, __u64, __u64);
int nilfs_cpfile_delete_checkpoint(struct inode *, __u64);
int nilfs_cpfile_change_cpmode(struct inode *, __u64, int);
int nilfs_cpfile_is_snapshot(struct inode *, __u64);
int nilfs_cpfile_get_stat(struct inode *, struct nilfs_cpstat *);
ssize_t nilfs_cpfile_get_cpinfo(struct inode *, __u64 *, int, void *,
				unsigned int, size_t);

int nilfs_cpfile_read(struct super_block *sb, size_t cpsize,
		      struct nilfs_inode *raw_inode, struct inode **inodep);

#endif	/* _NILFS_CPFILE_H */

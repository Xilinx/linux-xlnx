// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/gfs2_ondisk.h>
#include <linux/namei.h>
#include <linux/crc32.h>

#include "gfs2.h"
#include "incore.h"
#include "dir.h"
#include "glock.h"
#include "super.h"
#include "util.h"
#include "inode.h"

/**
 * gfs2_drevalidate - Check directory lookup consistency
 * @dir: expected parent directory inode
 * @name: expexted name
 * @dentry: dentry to check
 * @flags: lookup flags
 *
 * Check to make sure the lookup necessary to arrive at this inode from its
 * parent is still good.
 *
 * Returns: 1 if the dentry is ok, 0 if it isn't
 */

static int gfs2_drevalidate(struct inode *dir, const struct qstr *name,
			    struct dentry *dentry, unsigned int flags)
{
	struct gfs2_inode *dip = GFS2_I(dir);
	struct inode *inode;
	struct gfs2_holder d_gh;
	struct gfs2_inode *ip;
	int error, valid;
	unsigned long ver;

	gfs2_holder_mark_uninitialized(&d_gh);
	if (flags & LOOKUP_RCU) {
		inode = d_inode_rcu(dentry);
		if (!inode)
			return -ECHILD;
	} else {
		inode = d_inode(dentry);
		if (inode && is_bad_inode(inode))
			return 0;

		if (gfs2_glock_is_locked_by_me(dip->i_gl) == NULL) {
			error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0,
						   &d_gh);
			if (error)
				return 0;
		}
	}

	/*
	 * GFS2 doesn't have persistent inode versions.  Instead, when a
	 * directory is instantiated (which implies that we are holding the
	 * corresponding glock), we set i_version to a unique token based on
	 * sdp->sd_unique.  Later, when the directory is invalidated, we set
	 * i_version to 0.  The next time the directory is instantiated, a new
	 * unique token will be assigned to i_version and all cached dentries
	 * will be fully revalidated.
	 */

	ver = atomic64_read(&dir->i_version);
	if (ver && READ_ONCE(dentry->d_time) == ver)
		return 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	ip = inode ? GFS2_I(inode) : NULL;
	error = gfs2_dir_check(dir, name, ip);
	valid = inode ? !error : (error == -ENOENT);
	if (valid)
		WRITE_ONCE(dentry->d_time, ver);
	if (gfs2_holder_initialized(&d_gh))
		gfs2_glock_dq_uninit(&d_gh);

	return valid;
}

static int gfs2_dhash(const struct dentry *dentry, struct qstr *str)
{
	str->hash = gfs2_disk_hash(str->name, str->len);
	return 0;
}

static int gfs2_dentry_delete(const struct dentry *dentry)
{
	struct gfs2_inode *ginode;

	if (d_really_is_negative(dentry))
		return 0;

	ginode = GFS2_I(d_inode(dentry));
	if (!gfs2_holder_initialized(&ginode->i_iopen_gh))
		return 0;

	if (test_bit(GLF_DEMOTE, &ginode->i_iopen_gh.gh_gl->gl_flags))
		return 1;

	return 0;
}

const struct dentry_operations gfs2_nolock_dops = {
	.d_hash = gfs2_dhash,
	.d_delete = gfs2_dentry_delete,
};

const struct dentry_operations gfs2_dops = {
	.d_revalidate = gfs2_drevalidate,
	.d_hash = gfs2_dhash,
	.d_delete = gfs2_dentry_delete,
};


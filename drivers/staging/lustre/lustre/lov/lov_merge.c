/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LOV

#include "../../include/linux/libcfs/libcfs.h"

#include "../include/obd_class.h"
#include "lov_internal.h"

/** Merge the lock value block(&lvb) attributes and KMS from each of the
 * stripes in a file into a single lvb. It is expected that the caller
 * initializes the current atime, mtime, ctime to avoid regressing a more
 * uptodate time on the local client.
 */
int lov_merge_lvb_kms(struct lov_stripe_md *lsm,
		      struct ost_lvb *lvb, __u64 *kms_place)
{
	__u64 size = 0;
	__u64 kms = 0;
	__u64 blocks = 0;
	s64 current_mtime = lvb->lvb_mtime;
	s64 current_atime = lvb->lvb_atime;
	s64 current_ctime = lvb->lvb_ctime;
	int i;
	int rc = 0;

	assert_spin_locked(&lsm->lsm_lock);
	LASSERT(lsm->lsm_lock_owner == current_pid());

	CDEBUG(D_INODE, "MDT ID "DOSTID" initial value: s=%llu m=%llu a=%llu c=%llu b=%llu\n",
	       POSTID(&lsm->lsm_oi), lvb->lvb_size, lvb->lvb_mtime,
	       lvb->lvb_atime, lvb->lvb_ctime, lvb->lvb_blocks);
	for (i = 0; i < lsm->lsm_stripe_count; i++) {
		struct lov_oinfo *loi = lsm->lsm_oinfo[i];
		u64 lov_size, tmpsize;

		if (OST_LVB_IS_ERR(loi->loi_lvb.lvb_blocks)) {
			rc = OST_LVB_GET_ERR(loi->loi_lvb.lvb_blocks);
			continue;
		}

		tmpsize = loi->loi_kms;
		lov_size = lov_stripe_size(lsm, tmpsize, i);
		if (lov_size > kms)
			kms = lov_size;

		if (loi->loi_lvb.lvb_size > tmpsize)
			tmpsize = loi->loi_lvb.lvb_size;

		lov_size = lov_stripe_size(lsm, tmpsize, i);
		if (lov_size > size)
			size = lov_size;
		/* merge blocks, mtime, atime */
		blocks += loi->loi_lvb.lvb_blocks;
		if (loi->loi_lvb.lvb_mtime > current_mtime)
			current_mtime = loi->loi_lvb.lvb_mtime;
		if (loi->loi_lvb.lvb_atime > current_atime)
			current_atime = loi->loi_lvb.lvb_atime;
		if (loi->loi_lvb.lvb_ctime > current_ctime)
			current_ctime = loi->loi_lvb.lvb_ctime;

		CDEBUG(D_INODE, "MDT ID "DOSTID" on OST[%u]: s=%llu m=%llu a=%llu c=%llu b=%llu\n",
		       POSTID(&lsm->lsm_oi), loi->loi_ost_idx,
		       loi->loi_lvb.lvb_size, loi->loi_lvb.lvb_mtime,
		       loi->loi_lvb.lvb_atime, loi->loi_lvb.lvb_ctime,
		       loi->loi_lvb.lvb_blocks);
	}

	*kms_place = kms;
	lvb->lvb_size = size;
	lvb->lvb_blocks = blocks;
	lvb->lvb_mtime = current_mtime;
	lvb->lvb_atime = current_atime;
	lvb->lvb_ctime = current_ctime;
	return rc;
}

void lov_merge_attrs(struct obdo *tgt, struct obdo *src, u64 valid,
		     struct lov_stripe_md *lsm, int stripeno, int *set)
{
	valid &= src->o_valid;

	if (*set) {
		tgt->o_valid &= valid;
		if (valid & OBD_MD_FLSIZE) {
			/* this handles sparse files properly */
			u64 lov_size;

			lov_size = lov_stripe_size(lsm, src->o_size, stripeno);
			if (lov_size > tgt->o_size)
				tgt->o_size = lov_size;
		}
		if (valid & OBD_MD_FLBLOCKS)
			tgt->o_blocks += src->o_blocks;
		if (valid & OBD_MD_FLBLKSZ)
			tgt->o_blksize += src->o_blksize;
		if (valid & OBD_MD_FLCTIME && tgt->o_ctime < src->o_ctime)
			tgt->o_ctime = src->o_ctime;
		if (valid & OBD_MD_FLMTIME && tgt->o_mtime < src->o_mtime)
			tgt->o_mtime = src->o_mtime;
		if (valid & OBD_MD_FLDATAVERSION)
			tgt->o_data_version += src->o_data_version;

		/* handle flags */
		if (valid & OBD_MD_FLFLAGS)
			tgt->o_flags &= src->o_flags;
		else
			tgt->o_flags = 0;
	} else {
		memcpy(tgt, src, sizeof(*tgt));
		tgt->o_oi = lsm->lsm_oi;
		tgt->o_valid = valid;
		if (valid & OBD_MD_FLSIZE)
			tgt->o_size = lov_stripe_size(lsm, src->o_size,
						      stripeno);
		tgt->o_flags = 0;
		if (valid & OBD_MD_FLFLAGS)
			tgt->o_flags = src->o_flags;
	}

	/* data_version needs to be valid on all stripes to be correct! */
	if (!(valid & OBD_MD_FLDATAVERSION))
		tgt->o_valid &= ~OBD_MD_FLDATAVERSION;

	*set += 1;
}

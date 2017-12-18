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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/llite/dir.c
 *
 * Directory code for lustre client.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/buffer_head.h>   /* for wait_on_buffer */
#include <linux/pagevec.h>
#include <linux/prefetch.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/obd_support.h"
#include "../include/obd_class.h"
#include "../include/lustre/lustre_ioctl.h"
#include "../include/lustre_lib.h"
#include "../include/lustre_dlm.h"
#include "../include/lustre_fid.h"
#include "../include/lustre_kernelcomm.h"
#include "llite_internal.h"

/*
 * (new) readdir implementation overview.
 *
 * Original lustre readdir implementation cached exact copy of raw directory
 * pages on the client. These pages were indexed in client page cache by
 * logical offset in the directory file. This design, while very simple and
 * intuitive had some inherent problems:
 *
 *     . it implies that byte offset to the directory entry serves as a
 *     telldir(3)/seekdir(3) cookie, but that offset is not stable: in
 *     ext3/htree directory entries may move due to splits, and more
 *     importantly,
 *
 *     . it is incompatible with the design of split directories for cmd3,
 *     that assumes that names are distributed across nodes based on their
 *     hash, and so readdir should be done in hash order.
 *
 * New readdir implementation does readdir in hash order, and uses hash of a
 * file name as a telldir/seekdir cookie. This led to number of complications:
 *
 *     . hash is not unique, so it cannot be used to index cached directory
 *     pages on the client (note, that it requires a whole pageful of hash
 *     collided entries to cause two pages to have identical hashes);
 *
 *     . hash is not unique, so it cannot, strictly speaking, be used as an
 *     entry cookie. ext3/htree has the same problem and lustre implementation
 *     mimics their solution: seekdir(hash) positions directory at the first
 *     entry with the given hash.
 *
 * Client side.
 *
 * 0. caching
 *
 * Client caches directory pages using hash of the first entry as an index. As
 * noted above hash is not unique, so this solution doesn't work as is:
 * special processing is needed for "page hash chains" (i.e., sequences of
 * pages filled with entries all having the same hash value).
 *
 * First, such chains have to be detected. To this end, server returns to the
 * client the hash of the first entry on the page next to one returned. When
 * client detects that this hash is the same as hash of the first entry on the
 * returned page, page hash collision has to be handled. Pages in the
 * hash chain, except first one, are termed "overflow pages".
 *
 * Solution to index uniqueness problem is to not cache overflow
 * pages. Instead, when page hash collision is detected, all overflow pages
 * from emerging chain are immediately requested from the server and placed in
 * a special data structure (struct ll_dir_chain). This data structure is used
 * by ll_readdir() to process entries from overflow pages. When readdir
 * invocation finishes, overflow pages are discarded. If page hash collision
 * chain weren't completely processed, next call to readdir will again detect
 * page hash collision, again read overflow pages in, process next portion of
 * entries and again discard the pages. This is not as wasteful as it looks,
 * because, given reasonable hash, page hash collisions are extremely rare.
 *
 * 1. directory positioning
 *
 * When seekdir(hash) is called, original
 *
 *
 *
 *
 *
 *
 *
 *
 * Server.
 *
 * identification of and access to overflow pages
 *
 * page format
 *
 * Page in MDS_READPAGE RPC is packed in LU_PAGE_SIZE, and each page contains
 * a header lu_dirpage which describes the start/end hash, and whether this
 * page is empty (contains no dir entry) or hash collide with next page.
 * After client receives reply, several pages will be integrated into dir page
 * in PAGE_SIZE (if PAGE_SIZE greater than LU_PAGE_SIZE), and the lu_dirpage
 * for this integrated page will be adjusted. See lmv_adjust_dirpages().
 *
 */
struct page *ll_get_dir_page(struct inode *dir, struct md_op_data *op_data,
			     __u64 offset)
{
	struct md_callback cb_op;
	struct page *page;
	int rc;

	cb_op.md_blocking_ast = ll_md_blocking_ast;
	rc = md_read_page(ll_i2mdexp(dir), op_data, &cb_op, offset, &page);
	if (rc)
		return ERR_PTR(rc);

	return page;
}

void ll_release_page(struct inode *inode, struct page *page, bool remove)
{
	kunmap(page);

	/*
	 * Always remove the page for striped dir, because the page is
	 * built from temporarily in LMV layer
	 */
	if (inode && S_ISDIR(inode->i_mode) &&
	    ll_i2info(inode)->lli_lsm_md) {
		__free_page(page);
		return;
	}

	if (remove) {
		lock_page(page);
		if (likely(page->mapping))
			truncate_complete_page(page->mapping, page);
		unlock_page(page);
	}
	put_page(page);
}

/**
 * return IF_* type for given lu_dirent entry.
 * IF_* flag shld be converted to particular OS file type in
 * platform llite module.
 */
static __u16 ll_dirent_type_get(struct lu_dirent *ent)
{
	__u16 type = 0;
	struct luda_type *lt;
	int len = 0;

	if (le32_to_cpu(ent->lde_attrs) & LUDA_TYPE) {
		const unsigned int align = sizeof(struct luda_type) - 1;

		len = le16_to_cpu(ent->lde_namelen);
		len = (len + align) & ~align;
		lt = (void *)ent->lde_name + len;
		type = IFTODT(le16_to_cpu(lt->lt_type));
	}
	return type;
}

int ll_dir_read(struct inode *inode, __u64 *ppos, struct md_op_data *op_data,
		struct dir_context *ctx)
{
	struct ll_sb_info    *sbi	= ll_i2sbi(inode);
	__u64		   pos		= *ppos;
	int		   is_api32 = ll_need_32bit_api(sbi);
	int		   is_hash64 = sbi->ll_flags & LL_SBI_64BIT_HASH;
	struct page	  *page;
	bool		   done = false;
	int		   rc = 0;

	page = ll_get_dir_page(inode, op_data, pos);

	while (rc == 0 && !done) {
		struct lu_dirpage *dp;
		struct lu_dirent  *ent;
		__u64 hash;
		__u64 next;

		if (IS_ERR(page)) {
			rc = PTR_ERR(page);
			break;
		}

		hash = MDS_DIR_END_OFF;
		dp = page_address(page);
		for (ent = lu_dirent_start(dp); ent && !done;
		     ent = lu_dirent_next(ent)) {
			__u16	  type;
			int	    namelen;
			struct lu_fid  fid;
			__u64	  lhash;
			__u64	  ino;

			hash = le64_to_cpu(ent->lde_hash);
			if (hash < pos)
				/*
				 * Skip until we find target hash
				 * value.
				 */
				continue;

			namelen = le16_to_cpu(ent->lde_namelen);
			if (namelen == 0)
				/*
				 * Skip dummy record.
				 */
				continue;

			if (is_api32 && is_hash64)
				lhash = hash >> 32;
			else
				lhash = hash;
			fid_le_to_cpu(&fid, &ent->lde_fid);
			ino = cl_fid_build_ino(&fid, is_api32);
			type = ll_dirent_type_get(ent);
			ctx->pos = lhash;
			/* For 'll_nfs_get_name_filldir()', it will try
			 * to access the 'ent' through its 'lde_name',
			 * so the parameter 'name' for 'ctx->actor()'
			 * must be part of the 'ent'.
			 */
			done = !dir_emit(ctx, ent->lde_name,
					 namelen, ino, type);
		}

		if (done) {
			pos = hash;
			ll_release_page(inode, page, false);
			break;
		}

		next = le64_to_cpu(dp->ldp_hash_end);
		pos = next;
		if (pos == MDS_DIR_END_OFF) {
			/*
			 * End of directory reached.
			 */
			done = 1;
			ll_release_page(inode, page, false);
		} else {
			/*
			 * Normal case: continue to the next
			 * page.
			 */
			ll_release_page(inode, page,
					le32_to_cpu(dp->ldp_flags) &
					LDF_COLLIDE);
			next = pos;
			page = ll_get_dir_page(inode, op_data, pos);
		}
	}

	ctx->pos = pos;
	return rc;
}

static int ll_readdir(struct file *filp, struct dir_context *ctx)
{
	struct inode		*inode	= file_inode(filp);
	struct ll_file_data	*lfd	= LUSTRE_FPRIVATE(filp);
	struct ll_sb_info	*sbi	= ll_i2sbi(inode);
	__u64 pos = lfd ? lfd->lfd_pos : 0;
	int			hash64	= sbi->ll_flags & LL_SBI_64BIT_HASH;
	int			api32	= ll_need_32bit_api(sbi);
	struct md_op_data *op_data;
	int			rc;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p) pos/size %lu/%llu 32bit_api %d\n",
	       PFID(ll_inode2fid(inode)), inode, (unsigned long)pos,
	       i_size_read(inode), api32);

	if (pos == MDS_DIR_END_OFF) {
		/*
		 * end-of-file.
		 */
		rc = 0;
		goto out;
	}

	op_data = ll_prep_md_op_data(NULL, inode, inode, NULL, 0, 0,
				     LUSTRE_OPC_ANY, inode);
	if (IS_ERR(op_data)) {
		rc = PTR_ERR(op_data);
		goto out;
	}

	if (unlikely(op_data->op_mea1)) {
		/*
		 * This is only needed for striped dir to fill ..,
		 * see lmv_read_page
		 */
		if (file_dentry(filp)->d_parent &&
		    file_dentry(filp)->d_parent->d_inode) {
			__u64 ibits = MDS_INODELOCK_UPDATE;
			struct inode *parent;

			parent = file_dentry(filp)->d_parent->d_inode;
			if (ll_have_md_lock(parent, &ibits, LCK_MINMODE))
				op_data->op_fid3 = *ll_inode2fid(parent);
		}

		/*
		 * If it can not find in cache, do lookup .. on the master
		 * object
		 */
		if (fid_is_zero(&op_data->op_fid3)) {
			rc = ll_dir_get_parent_fid(inode, &op_data->op_fid3);
			if (rc) {
				ll_finish_md_op_data(op_data);
				return rc;
			}
		}
	}
	op_data->op_max_pages = sbi->ll_md_brw_pages;
	ctx->pos = pos;
	rc = ll_dir_read(inode, &pos, op_data, ctx);
	pos = ctx->pos;
	if (lfd)
		lfd->lfd_pos = pos;

	if (pos == MDS_DIR_END_OFF) {
		if (api32)
			pos = LL_DIR_END_OFF_32BIT;
		else
			pos = LL_DIR_END_OFF;
	} else {
		if (api32 && hash64)
			pos >>= 32;
	}
	ctx->pos = pos;
	ll_finish_md_op_data(op_data);
	filp->f_version = inode->i_version;

out:
	if (!rc)
		ll_stats_ops_tally(sbi, LPROC_LL_READDIR, 1);

	return rc;
}

static int ll_send_mgc_param(struct obd_export *mgc, char *string)
{
	struct mgs_send_param *msp;
	int rc = 0;

	msp = kzalloc(sizeof(*msp), GFP_NOFS);
	if (!msp)
		return -ENOMEM;

	strlcpy(msp->mgs_param, string, sizeof(msp->mgs_param));
	rc = obd_set_info_async(NULL, mgc, sizeof(KEY_SET_INFO), KEY_SET_INFO,
				sizeof(struct mgs_send_param), msp, NULL);
	if (rc)
		CERROR("Failed to set parameter: %d\n", rc);
	kfree(msp);

	return rc;
}

/**
 * Create striped directory with specified stripe(@lump)
 *
 * param[in] parent	the parent of the directory.
 * param[in] lump	the specified stripes.
 * param[in] dirname	the name of the directory.
 * param[in] mode	the specified mode of the directory.
 *
 * retval		=0 if striped directory is being created successfully.
 *			<0 if the creation is failed.
 */
static int ll_dir_setdirstripe(struct inode *parent, struct lmv_user_md *lump,
			       const char *dirname, umode_t mode)
{
	struct ptlrpc_request *request = NULL;
	struct md_op_data *op_data;
	struct ll_sb_info *sbi = ll_i2sbi(parent);
	int err;

	if (unlikely(lump->lum_magic != LMV_USER_MAGIC))
		return -EINVAL;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p) name %s stripe_offset %d, stripe_count: %u\n",
	       PFID(ll_inode2fid(parent)), parent, dirname,
	       (int)lump->lum_stripe_offset, lump->lum_stripe_count);

	if (lump->lum_magic != cpu_to_le32(LMV_USER_MAGIC))
		lustre_swab_lmv_user_md(lump);

	if (!IS_POSIXACL(parent) || !exp_connect_umask(ll_i2mdexp(parent)))
		mode &= ~current_umask();
	mode = (mode & (S_IRWXUGO | S_ISVTX)) | S_IFDIR;
	op_data = ll_prep_md_op_data(NULL, parent, NULL, dirname,
				     strlen(dirname), mode, LUSTRE_OPC_MKDIR,
				     lump);
	if (IS_ERR(op_data)) {
		err = PTR_ERR(op_data);
		goto err_exit;
	}

	op_data->op_cli_flags |= CLI_SET_MEA;
	err = md_create(sbi->ll_md_exp, op_data, lump, sizeof(*lump), mode,
			from_kuid(&init_user_ns, current_fsuid()),
			from_kgid(&init_user_ns, current_fsgid()),
			cfs_curproc_cap_pack(), 0, &request);
	ll_finish_md_op_data(op_data);
	if (err)
		goto err_exit;
err_exit:
	ptlrpc_req_finished(request);
	return err;
}

int ll_dir_setstripe(struct inode *inode, struct lov_user_md *lump,
		     int set_default)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct md_op_data *op_data;
	struct ptlrpc_request *req = NULL;
	int rc = 0;
	struct lustre_sb_info *lsi = s2lsi(inode->i_sb);
	struct obd_device *mgc = lsi->lsi_mgc;
	int lum_size;

	if (lump) {
		/*
		 * This is coming from userspace, so should be in
		 * local endian.  But the MDS would like it in little
		 * endian, so we swab it before we send it.
		 */
		switch (lump->lmm_magic) {
		case LOV_USER_MAGIC_V1: {
			if (lump->lmm_magic != cpu_to_le32(LOV_USER_MAGIC_V1))
				lustre_swab_lov_user_md_v1(lump);
			lum_size = sizeof(struct lov_user_md_v1);
			break;
		}
		case LOV_USER_MAGIC_V3: {
			if (lump->lmm_magic != cpu_to_le32(LOV_USER_MAGIC_V3))
				lustre_swab_lov_user_md_v3(
					(struct lov_user_md_v3 *)lump);
			lum_size = sizeof(struct lov_user_md_v3);
			break;
		}
		case LMV_USER_MAGIC: {
			if (lump->lmm_magic != cpu_to_le32(LMV_USER_MAGIC))
				lustre_swab_lmv_user_md(
					(struct lmv_user_md *)lump);
			lum_size = sizeof(struct lmv_user_md);
			break;
		}
		default: {
			CDEBUG(D_IOCTL, "bad userland LOV MAGIC: %#08x != %#08x nor %#08x\n",
			       lump->lmm_magic, LOV_USER_MAGIC_V1,
			       LOV_USER_MAGIC_V3);
			return -EINVAL;
		}
		}
	} else {
		lum_size = sizeof(struct lov_user_md_v1);
	}

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	/* swabbing is done in lov_setstripe() on server side */
	rc = md_setattr(sbi->ll_md_exp, op_data, lump, lum_size,
			NULL, 0, &req, NULL);
	ll_finish_md_op_data(op_data);
	ptlrpc_req_finished(req);
	if (rc) {
		if (rc != -EPERM && rc != -EACCES)
			CERROR("mdc_setattr fails: rc = %d\n", rc);
	}

	/* In the following we use the fact that LOV_USER_MAGIC_V1 and
	 * LOV_USER_MAGIC_V3 have the same initial fields so we do not
	 * need to make the distinction between the 2 versions
	 */
	if (set_default && mgc->u.cli.cl_mgc_mgsexp) {
		char *param = NULL;
		char *buf;

		param = kzalloc(MGS_PARAM_MAXLEN, GFP_NOFS);
		if (!param)
			return -ENOMEM;

		buf = param;
		/* Get fsname and assume devname to be -MDT0000. */
		ll_get_fsname(inode->i_sb, buf, MTI_NAME_MAXLEN);
		strcat(buf, "-MDT0000.lov");
		buf += strlen(buf);

		/* Set root stripesize */
		sprintf(buf, ".stripesize=%u",
			lump ? le32_to_cpu(lump->lmm_stripe_size) : 0);
		rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);
		if (rc)
			goto end;

		/* Set root stripecount */
		sprintf(buf, ".stripecount=%hd",
			lump ? le16_to_cpu(lump->lmm_stripe_count) : 0);
		rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);
		if (rc)
			goto end;

		/* Set root stripeoffset */
		sprintf(buf, ".stripeoffset=%hd",
			lump ? le16_to_cpu(lump->lmm_stripe_offset) :
			(typeof(lump->lmm_stripe_offset))(-1));
		rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);

end:
		kfree(param);
	}
	return rc;
}

/**
 * This function will be used to get default LOV/LMV/Default LMV
 * @valid will be used to indicate which stripe it will retrieve
 *	OBD_MD_MEA		LMV stripe EA
 *	OBD_MD_DEFAULT_MEA	Default LMV stripe EA
 *	otherwise		Default LOV EA.
 * Each time, it can only retrieve 1 stripe EA
 **/
int ll_dir_getstripe(struct inode *inode, void **plmm, int *plmm_size,
		     struct ptlrpc_request **request, u64 valid)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct mdt_body   *body;
	struct lov_mds_md *lmm = NULL;
	struct ptlrpc_request *req = NULL;
	int rc, lmmsize;
	struct md_op_data *op_data;

	rc = ll_get_max_mdsize(sbi, &lmmsize);
	if (rc)
		return rc;

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL,
				     0, lmmsize, LUSTRE_OPC_ANY,
				     NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	op_data->op_valid = OBD_MD_FLEASIZE | OBD_MD_FLDIREA;
	rc = md_getattr(sbi->ll_md_exp, op_data, &req);
	ll_finish_md_op_data(op_data);
	if (rc < 0) {
		CDEBUG(D_INFO, "md_getattr failed on inode "DFID": rc %d\n",
		       PFID(ll_inode2fid(inode)), rc);
		goto out;
	}

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);

	lmmsize = body->mbo_eadatasize;

	if (!(body->mbo_valid & (OBD_MD_FLEASIZE | OBD_MD_FLDIREA)) ||
	    lmmsize == 0) {
		rc = -ENODATA;
		goto out;
	}

	lmm = req_capsule_server_sized_get(&req->rq_pill,
					   &RMF_MDT_MD, lmmsize);
	LASSERT(lmm);

	/*
	 * This is coming from the MDS, so is probably in
	 * little endian.  We convert it to host endian before
	 * passing it to userspace.
	 */
	/* We don't swab objects for directories */
	switch (le32_to_cpu(lmm->lmm_magic)) {
	case LOV_MAGIC_V1:
		if (cpu_to_le32(LOV_MAGIC) != LOV_MAGIC)
			lustre_swab_lov_user_md_v1((struct lov_user_md_v1 *)lmm);
		break;
	case LOV_MAGIC_V3:
		if (cpu_to_le32(LOV_MAGIC) != LOV_MAGIC)
			lustre_swab_lov_user_md_v3((struct lov_user_md_v3 *)lmm);
		break;
	case LMV_MAGIC_V1:
		if (cpu_to_le32(LMV_MAGIC) != LMV_MAGIC)
			lustre_swab_lmv_mds_md((union lmv_mds_md *)lmm);
		break;
	case LMV_USER_MAGIC:
		if (cpu_to_le32(LMV_USER_MAGIC) != LMV_USER_MAGIC)
			lustre_swab_lmv_user_md((struct lmv_user_md *)lmm);
		break;
	default:
		CERROR("unknown magic: %lX\n", (unsigned long)lmm->lmm_magic);
		rc = -EPROTO;
	}
out:
	*plmm = lmm;
	*plmm_size = lmmsize;
	*request = req;
	return rc;
}

int ll_get_mdt_idx_by_fid(struct ll_sb_info *sbi, const struct lu_fid *fid)
{
	struct md_op_data *op_data;
	int mdt_index, rc;

	op_data = kzalloc(sizeof(*op_data), GFP_NOFS);
	if (!op_data)
		return -ENOMEM;

	op_data->op_flags |= MF_GET_MDT_IDX;
	op_data->op_fid1 = *fid;
	rc = md_getattr(sbi->ll_md_exp, op_data, NULL);
	mdt_index = op_data->op_mds;
	kvfree(op_data);
	if (rc < 0)
		return rc;

	return mdt_index;
}

/*
 *  Get MDT index for the inode.
 */
int ll_get_mdt_idx(struct inode *inode)
{
	return ll_get_mdt_idx_by_fid(ll_i2sbi(inode), ll_inode2fid(inode));
}

/**
 * Generic handler to do any pre-copy work.
 *
 * It sends a first hsm_progress (with extent length == 0) to coordinator as a
 * first information for it that real work has started.
 *
 * Moreover, for a ARCHIVE request, it will sample the file data version and
 * store it in \a copy.
 *
 * \return 0 on success.
 */
static int ll_ioc_copy_start(struct super_block *sb, struct hsm_copy *copy)
{
	struct ll_sb_info		*sbi = ll_s2sbi(sb);
	struct hsm_progress_kernel	 hpk;
	int				 rc;

	/* Forge a hsm_progress based on data from copy. */
	hpk.hpk_fid = copy->hc_hai.hai_fid;
	hpk.hpk_cookie = copy->hc_hai.hai_cookie;
	hpk.hpk_extent.offset = copy->hc_hai.hai_extent.offset;
	hpk.hpk_extent.length = 0;
	hpk.hpk_flags = 0;
	hpk.hpk_errval = 0;
	hpk.hpk_data_version = 0;

	/* For archive request, we need to read the current file version. */
	if (copy->hc_hai.hai_action == HSMA_ARCHIVE) {
		struct inode	*inode;
		__u64		 data_version = 0;

		/* Get inode for this fid */
		inode = search_inode_for_lustre(sb, &copy->hc_hai.hai_fid);
		if (IS_ERR(inode)) {
			hpk.hpk_flags |= HP_FLAG_RETRY;
			/* hpk_errval is >= 0 */
			hpk.hpk_errval = -PTR_ERR(inode);
			rc = PTR_ERR(inode);
			goto progress;
		}

		/* Read current file data version */
		rc = ll_data_version(inode, &data_version, LL_DV_RD_FLUSH);
		iput(inode);
		if (rc != 0) {
			CDEBUG(D_HSM, "Could not read file data version of "
				      DFID" (rc = %d). Archive request (%#llx) could not be done.\n",
				      PFID(&copy->hc_hai.hai_fid), rc,
				      copy->hc_hai.hai_cookie);
			hpk.hpk_flags |= HP_FLAG_RETRY;
			/* hpk_errval must be >= 0 */
			hpk.hpk_errval = -rc;
			goto progress;
		}

		/* Store in the hsm_copy for later copytool use.
		 * Always modified even if no lsm.
		 */
		copy->hc_data_version = data_version;
	}

progress:
	/* On error, the request should be considered as completed */
	if (hpk.hpk_errval > 0)
		hpk.hpk_flags |= HP_FLAG_COMPLETED;
	rc = obd_iocontrol(LL_IOC_HSM_PROGRESS, sbi->ll_md_exp, sizeof(hpk),
			   &hpk, NULL);

	return rc;
}

/**
 * Generic handler to do any post-copy work.
 *
 * It will send the last hsm_progress update to coordinator to inform it
 * that copy is finished and whether it was successful or not.
 *
 * Moreover,
 * - for ARCHIVE request, it will sample the file data version and compare it
 *   with the version saved in ll_ioc_copy_start(). If they do not match, copy
 *   will be considered as failed.
 * - for RESTORE request, it will sample the file data version and send it to
 *   coordinator which is useful if the file was imported as 'released'.
 *
 * \return 0 on success.
 */
static int ll_ioc_copy_end(struct super_block *sb, struct hsm_copy *copy)
{
	struct ll_sb_info		*sbi = ll_s2sbi(sb);
	struct hsm_progress_kernel	 hpk;
	int				 rc;

	/* If you modify the logic here, also check llapi_hsm_copy_end(). */
	/* Take care: copy->hc_hai.hai_action, len, gid and data are not
	 * initialized if copy_end was called with copy == NULL.
	 */

	/* Forge a hsm_progress based on data from copy. */
	hpk.hpk_fid = copy->hc_hai.hai_fid;
	hpk.hpk_cookie = copy->hc_hai.hai_cookie;
	hpk.hpk_extent = copy->hc_hai.hai_extent;
	hpk.hpk_flags = copy->hc_flags | HP_FLAG_COMPLETED;
	hpk.hpk_errval = copy->hc_errval;
	hpk.hpk_data_version = 0;

	/* For archive request, we need to check the file data was not changed.
	 *
	 * For restore request, we need to send the file data version, this is
	 * useful when the file was created using hsm_import.
	 */
	if (((copy->hc_hai.hai_action == HSMA_ARCHIVE) ||
	     (copy->hc_hai.hai_action == HSMA_RESTORE)) &&
	    (copy->hc_errval == 0)) {
		struct inode	*inode;
		__u64		 data_version = 0;

		/* Get lsm for this fid */
		inode = search_inode_for_lustre(sb, &copy->hc_hai.hai_fid);
		if (IS_ERR(inode)) {
			hpk.hpk_flags |= HP_FLAG_RETRY;
			/* hpk_errval must be >= 0 */
			hpk.hpk_errval = -PTR_ERR(inode);
			rc = PTR_ERR(inode);
			goto progress;
		}

		rc = ll_data_version(inode, &data_version, LL_DV_RD_FLUSH);
		iput(inode);
		if (rc) {
			CDEBUG(D_HSM, "Could not read file data version. Request could not be confirmed.\n");
			if (hpk.hpk_errval == 0)
				hpk.hpk_errval = -rc;
			goto progress;
		}

		/* Store in the hsm_copy for later copytool use.
		 * Always modified even if no lsm.
		 */
		hpk.hpk_data_version = data_version;

		/* File could have been stripped during archiving, so we need
		 * to check anyway.
		 */
		if ((copy->hc_hai.hai_action == HSMA_ARCHIVE) &&
		    (copy->hc_data_version != data_version)) {
			CDEBUG(D_HSM, "File data version mismatched. File content was changed during archiving. "
			       DFID", start:%#llx current:%#llx\n",
			       PFID(&copy->hc_hai.hai_fid),
			       copy->hc_data_version, data_version);
			/* File was changed, send error to cdt. Do not ask for
			 * retry because if a file is modified frequently,
			 * the cdt will loop on retried archive requests.
			 * The policy engine will ask for a new archive later
			 * when the file will not be modified for some tunable
			 * time
			 */
			/* we do not notify caller */
			hpk.hpk_flags &= ~HP_FLAG_RETRY;
			/* hpk_errval must be >= 0 */
			hpk.hpk_errval = EBUSY;
		}
	}

progress:
	rc = obd_iocontrol(LL_IOC_HSM_PROGRESS, sbi->ll_md_exp, sizeof(hpk),
			   &hpk, NULL);

	return rc;
}

static int copy_and_ioctl(int cmd, struct obd_export *exp,
			  const void __user *data, size_t size)
{
	void *copy;
	int rc;

	copy = memdup_user(data, size);
	if (IS_ERR(copy))
		return PTR_ERR(copy);

	rc = obd_iocontrol(cmd, exp, size, copy, NULL);
	kfree(copy);

	return rc;
}

static int quotactl_ioctl(struct ll_sb_info *sbi, struct if_quotactl *qctl)
{
	int cmd = qctl->qc_cmd;
	int type = qctl->qc_type;
	int id = qctl->qc_id;
	int valid = qctl->qc_valid;
	int rc = 0;

	switch (cmd) {
	case LUSTRE_Q_INVALIDATE:
	case LUSTRE_Q_FINVALIDATE:
	case Q_QUOTAON:
	case Q_QUOTAOFF:
	case Q_SETQUOTA:
	case Q_SETINFO:
		if (!capable(CFS_CAP_SYS_ADMIN))
			return -EPERM;
		break;
	case Q_GETQUOTA:
		if (((type == USRQUOTA &&
		      !uid_eq(current_euid(), make_kuid(&init_user_ns, id))) ||
		     (type == GRPQUOTA &&
		      !in_egroup_p(make_kgid(&init_user_ns, id)))) &&
		      !capable(CFS_CAP_SYS_ADMIN))
			return -EPERM;
		break;
	case Q_GETINFO:
		break;
	default:
		CERROR("unsupported quotactl op: %#x\n", cmd);
		return -ENOTTY;
	}

	if (valid != QC_GENERAL) {
		if (cmd == Q_GETINFO)
			qctl->qc_cmd = Q_GETOINFO;
		else if (cmd == Q_GETQUOTA)
			qctl->qc_cmd = Q_GETOQUOTA;
		else
			return -EINVAL;

		switch (valid) {
		case QC_MDTIDX:
			rc = obd_iocontrol(OBD_IOC_QUOTACTL, sbi->ll_md_exp,
					   sizeof(*qctl), qctl, NULL);
			break;
		case QC_OSTIDX:
			rc = obd_iocontrol(OBD_IOC_QUOTACTL, sbi->ll_dt_exp,
					   sizeof(*qctl), qctl, NULL);
			break;
		case QC_UUID:
			rc = obd_iocontrol(OBD_IOC_QUOTACTL, sbi->ll_md_exp,
					   sizeof(*qctl), qctl, NULL);
			if (rc == -EAGAIN)
				rc = obd_iocontrol(OBD_IOC_QUOTACTL,
						   sbi->ll_dt_exp,
						   sizeof(*qctl), qctl, NULL);
			break;
		default:
			rc = -EINVAL;
			break;
		}

		if (rc)
			return rc;

		qctl->qc_cmd = cmd;
	} else {
		struct obd_quotactl *oqctl;

		oqctl = kzalloc(sizeof(*oqctl), GFP_NOFS);
		if (!oqctl)
			return -ENOMEM;

		QCTL_COPY(oqctl, qctl);
		rc = obd_quotactl(sbi->ll_md_exp, oqctl);
		if (rc) {
			if (rc != -EALREADY && cmd == Q_QUOTAON) {
				oqctl->qc_cmd = Q_QUOTAOFF;
				obd_quotactl(sbi->ll_md_exp, oqctl);
			}
			kfree(oqctl);
			return rc;
		}
		/* If QIF_SPACE is not set, client should collect the
		 * space usage from OSSs by itself
		 */
		if (cmd == Q_GETQUOTA &&
		    !(oqctl->qc_dqblk.dqb_valid & QIF_SPACE) &&
		    !oqctl->qc_dqblk.dqb_curspace) {
			struct obd_quotactl *oqctl_tmp;

			oqctl_tmp = kzalloc(sizeof(*oqctl_tmp), GFP_NOFS);
			if (!oqctl_tmp) {
				rc = -ENOMEM;
				goto out;
			}

			oqctl_tmp->qc_cmd = Q_GETOQUOTA;
			oqctl_tmp->qc_id = oqctl->qc_id;
			oqctl_tmp->qc_type = oqctl->qc_type;

			/* collect space usage from OSTs */
			oqctl_tmp->qc_dqblk.dqb_curspace = 0;
			rc = obd_quotactl(sbi->ll_dt_exp, oqctl_tmp);
			if (!rc || rc == -EREMOTEIO) {
				oqctl->qc_dqblk.dqb_curspace =
					oqctl_tmp->qc_dqblk.dqb_curspace;
				oqctl->qc_dqblk.dqb_valid |= QIF_SPACE;
			}

			/* collect space & inode usage from MDTs */
			oqctl_tmp->qc_dqblk.dqb_curspace = 0;
			oqctl_tmp->qc_dqblk.dqb_curinodes = 0;
			rc = obd_quotactl(sbi->ll_md_exp, oqctl_tmp);
			if (!rc || rc == -EREMOTEIO) {
				oqctl->qc_dqblk.dqb_curspace +=
					oqctl_tmp->qc_dqblk.dqb_curspace;
				oqctl->qc_dqblk.dqb_curinodes =
					oqctl_tmp->qc_dqblk.dqb_curinodes;
				oqctl->qc_dqblk.dqb_valid |= QIF_INODES;
			} else {
				oqctl->qc_dqblk.dqb_valid &= ~QIF_SPACE;
			}

			kfree(oqctl_tmp);
		}
out:
		QCTL_COPY(qctl, oqctl);
		kfree(oqctl);
	}

	return rc;
}

/* This function tries to get a single name component,
 * to send to the server. No actual path traversal involved,
 * so we limit to NAME_MAX
 */
static char *ll_getname(const char __user *filename)
{
	int ret = 0, len;
	char *tmp;

	tmp = kzalloc(NAME_MAX + 1, GFP_KERNEL);
	if (!tmp)
		return ERR_PTR(-ENOMEM);

	len = strncpy_from_user(tmp, filename, NAME_MAX + 1);
	if (len < 0)
		ret = len;
	else if (len == 0)
		ret = -ENOENT;
	else if (len > NAME_MAX && tmp[NAME_MAX] != 0)
		ret = -ENAMETOOLONG;

	if (ret) {
		kfree(tmp);
		tmp =  ERR_PTR(ret);
	}
	return tmp;
}

#define ll_putname(filename) kfree(filename)

static long ll_dir_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct obd_ioctl_data *data;
	int rc = 0;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p), cmd=%#x\n",
	       PFID(ll_inode2fid(inode)), inode, cmd);

	/* asm-ppc{,64} declares TCGETS, et. al. as type 't' not 'T' */
	if (_IOC_TYPE(cmd) == 'T' || _IOC_TYPE(cmd) == 't') /* tty ioctls */
		return -ENOTTY;

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_IOCTL, 1);
	switch (cmd) {
	case FSFILT_IOC_GETFLAGS:
	case FSFILT_IOC_SETFLAGS:
		return ll_iocontrol(inode, file, cmd, arg);
	case FSFILT_IOC_GETVERSION_OLD:
	case FSFILT_IOC_GETVERSION:
		return put_user(inode->i_generation, (int __user *)arg);
	/* We need to special case any other ioctls we want to handle,
	 * to send them to the MDS/OST as appropriate and to properly
	 * network encode the arg field.
	case FSFILT_IOC_SETVERSION_OLD:
	case FSFILT_IOC_SETVERSION:
	*/
	case LL_IOC_GET_MDTIDX: {
		int mdtidx;

		mdtidx = ll_get_mdt_idx(inode);
		if (mdtidx < 0)
			return mdtidx;

		if (put_user((int)mdtidx, (int __user *)arg))
			return -EFAULT;

		return 0;
	}
	case IOC_MDC_LOOKUP: {
		int namelen, len = 0;
		char *buf = NULL;
		char *filename;

		rc = obd_ioctl_getdata(&buf, &len, (void __user *)arg);
		if (rc)
			return rc;
		data = (void *)buf;

		filename = data->ioc_inlbuf1;
		namelen = strlen(filename);

		if (namelen < 1) {
			CDEBUG(D_INFO, "IOC_MDC_LOOKUP missing filename\n");
			rc = -EINVAL;
			goto out_free;
		}

		rc = ll_get_fid_by_name(inode, filename, namelen, NULL);
		if (rc < 0) {
			CERROR("%s: lookup %.*s failed: rc = %d\n",
			       ll_get_fsname(inode->i_sb, NULL, 0), namelen,
			       filename, rc);
			goto out_free;
		}
out_free:
		obd_ioctl_freedata(buf, len);
		return rc;
	}
	case LL_IOC_LMV_SETSTRIPE: {
		struct lmv_user_md  *lum;
		char		*buf = NULL;
		char		*filename;
		int		 namelen = 0;
		int		 lumlen = 0;
		umode_t mode;
		int		 len;
		int		 rc;

		rc = obd_ioctl_getdata(&buf, &len, (void __user *)arg);
		if (rc)
			return rc;

		data = (void *)buf;
		if (!data->ioc_inlbuf1 || !data->ioc_inlbuf2 ||
		    data->ioc_inllen1 == 0 || data->ioc_inllen2 == 0) {
			rc = -EINVAL;
			goto lmv_out_free;
		}

		filename = data->ioc_inlbuf1;
		namelen = data->ioc_inllen1;

		if (namelen < 1) {
			CDEBUG(D_INFO, "IOC_MDC_LOOKUP missing filename\n");
			rc = -EINVAL;
			goto lmv_out_free;
		}
		lum = (struct lmv_user_md *)data->ioc_inlbuf2;
		lumlen = data->ioc_inllen2;

		if (lum->lum_magic != LMV_USER_MAGIC ||
		    lumlen != sizeof(*lum)) {
			CERROR("%s: wrong lum magic %x or size %d: rc = %d\n",
			       filename, lum->lum_magic, lumlen, -EFAULT);
			rc = -EINVAL;
			goto lmv_out_free;
		}

#if OBD_OCD_VERSION(2, 9, 50, 0) > LUSTRE_VERSION_CODE
		mode = data->ioc_type != 0 ? data->ioc_type : S_IRWXUGO;
#else
		mode = data->ioc_type;
#endif
		rc = ll_dir_setdirstripe(inode, lum, filename, mode);
lmv_out_free:
		obd_ioctl_freedata(buf, len);
		return rc;
	}
	case LL_IOC_LMV_SET_DEFAULT_STRIPE: {
		struct lmv_user_md __user *ulump;
		struct lmv_user_md lum;
		int rc;

		ulump = (struct lmv_user_md __user *)arg;
		if (copy_from_user(&lum, ulump, sizeof(lum)))
			return -EFAULT;

		if (lum.lum_magic != LMV_USER_MAGIC)
			return -EINVAL;

		rc = ll_dir_setstripe(inode, (struct lov_user_md *)&lum, 0);

		return rc;
	}
	case LL_IOC_LOV_SETSTRIPE: {
		struct lov_user_md_v3 lumv3;
		struct lov_user_md_v1 *lumv1 = (struct lov_user_md_v1 *)&lumv3;
		struct lov_user_md_v1 __user *lumv1p = (void __user *)arg;
		struct lov_user_md_v3 __user *lumv3p = (void __user *)arg;

		int set_default = 0;

		LASSERT(sizeof(lumv3) == sizeof(*lumv3p));
		LASSERT(sizeof(lumv3.lmm_objects[0]) ==
			sizeof(lumv3p->lmm_objects[0]));
		/* first try with v1 which is smaller than v3 */
		if (copy_from_user(lumv1, lumv1p, sizeof(*lumv1)))
			return -EFAULT;

		if (lumv1->lmm_magic == LOV_USER_MAGIC_V3) {
			if (copy_from_user(&lumv3, lumv3p, sizeof(lumv3)))
				return -EFAULT;
		}

		if (is_root_inode(inode))
			set_default = 1;

		/* in v1 and v3 cases lumv1 points to data */
		rc = ll_dir_setstripe(inode, lumv1, set_default);

		return rc;
	}
	case LL_IOC_LMV_GETSTRIPE: {
		struct lmv_user_md __user *ulmv;
		struct lmv_user_md lum;
		struct ptlrpc_request *request = NULL;
		struct lmv_user_md *tmp = NULL;
		union lmv_mds_md *lmm = NULL;
		u64 valid = 0;
		int stripe_count;
		int mdt_index;
		int lum_size;
		int lmmsize;
		int rc;
		int i;

		ulmv = (struct lmv_user_md __user *)arg;
		if (copy_from_user(&lum, ulmv, sizeof(*ulmv)))
			return -EFAULT;

		/*
		 * lum_magic will indicate which stripe the ioctl will like
		 * to get, LMV_MAGIC_V1 is for normal LMV stripe, LMV_USER_MAGIC
		 * is for default LMV stripe
		 */
		if (lum.lum_magic == LMV_MAGIC_V1)
			valid |= OBD_MD_MEA;
		else if (lum.lum_magic == LMV_USER_MAGIC)
			valid |= OBD_MD_DEFAULT_MEA;
		else
			return -EINVAL;

		rc = ll_dir_getstripe(inode, (void **)&lmm, &lmmsize, &request,
				      valid);
		if (rc)
			goto finish_req;

		/* Get default LMV EA */
		if (lum.lum_magic == LMV_USER_MAGIC) {
			if (rc)
				goto finish_req;

			if (lmmsize > sizeof(*ulmv)) {
				rc = -EINVAL;
				goto finish_req;
			}

			if (copy_to_user(ulmv, lmm, lmmsize))
				rc = -EFAULT;

			goto finish_req;
		}

		stripe_count = lmv_mds_md_stripe_count_get(lmm);
		lum_size = lmv_user_md_size(stripe_count, LMV_MAGIC_V1);
		tmp = kzalloc(lum_size, GFP_NOFS);
		if (!tmp) {
			rc = -ENOMEM;
			goto finish_req;
		}

		mdt_index = ll_get_mdt_idx(inode);
		if (mdt_index < 0) {
			rc = -ENOMEM;
			goto out_tmp;
		}
		tmp->lum_magic = LMV_MAGIC_V1;
		tmp->lum_stripe_count = 0;
		tmp->lum_stripe_offset = mdt_index;
		for (i = 0; i < stripe_count; i++) {
			struct lu_fid fid;

			fid_le_to_cpu(&fid, &lmm->lmv_md_v1.lmv_stripe_fids[i]);
			mdt_index = ll_get_mdt_idx_by_fid(sbi, &fid);
			if (mdt_index < 0) {
				rc = mdt_index;
				goto out_tmp;
			}
			tmp->lum_objects[i].lum_mds = mdt_index;
			tmp->lum_objects[i].lum_fid = fid;
			tmp->lum_stripe_count++;
		}

		if (copy_to_user(ulmv, tmp, lum_size)) {
			rc = -EFAULT;
			goto out_tmp;
		}
out_tmp:
		kfree(tmp);
finish_req:
		ptlrpc_req_finished(request);
		return rc;
	}

	case LL_IOC_LOV_SWAP_LAYOUTS:
		return -EPERM;
	case IOC_OBD_STATFS:
		return ll_obd_statfs(inode, (void __user *)arg);
	case LL_IOC_LOV_GETSTRIPE:
	case LL_IOC_MDC_GETINFO:
	case IOC_MDC_GETFILEINFO:
	case IOC_MDC_GETFILESTRIPE: {
		struct ptlrpc_request *request = NULL;
		struct lov_user_md __user *lump;
		struct lov_mds_md *lmm = NULL;
		struct mdt_body *body;
		char *filename = NULL;
		int lmmsize;

		if (cmd == IOC_MDC_GETFILEINFO ||
		    cmd == IOC_MDC_GETFILESTRIPE) {
			filename = ll_getname((const char __user *)arg);
			if (IS_ERR(filename))
				return PTR_ERR(filename);

			rc = ll_lov_getstripe_ea_info(inode, filename, &lmm,
						      &lmmsize, &request);
		} else {
			rc = ll_dir_getstripe(inode, (void **)&lmm, &lmmsize,
					      &request, 0);
		}

		if (request) {
			body = req_capsule_server_get(&request->rq_pill,
						      &RMF_MDT_BODY);
			LASSERT(body);
		} else {
			goto out_req;
		}

		if (rc < 0) {
			if (rc == -ENODATA && (cmd == IOC_MDC_GETFILEINFO ||
					       cmd == LL_IOC_MDC_GETINFO)) {
				rc = 0;
				goto skip_lmm;
			} else {
				goto out_req;
			}
		}

		if (cmd == IOC_MDC_GETFILESTRIPE ||
		    cmd == LL_IOC_LOV_GETSTRIPE) {
			lump = (struct lov_user_md __user *)arg;
		} else {
			struct lov_user_mds_data __user *lmdp;

			lmdp = (struct lov_user_mds_data __user *)arg;
			lump = &lmdp->lmd_lmm;
		}
		if (copy_to_user(lump, lmm, lmmsize)) {
			if (copy_to_user(lump, lmm, sizeof(*lump))) {
				rc = -EFAULT;
				goto out_req;
			}
			rc = -EOVERFLOW;
		}
skip_lmm:
		if (cmd == IOC_MDC_GETFILEINFO || cmd == LL_IOC_MDC_GETINFO) {
			struct lov_user_mds_data __user *lmdp;
			lstat_t st = { 0 };

			st.st_dev     = inode->i_sb->s_dev;
			st.st_mode    = body->mbo_mode;
			st.st_nlink   = body->mbo_nlink;
			st.st_uid     = body->mbo_uid;
			st.st_gid     = body->mbo_gid;
			st.st_rdev    = body->mbo_rdev;
			st.st_size    = body->mbo_size;
			st.st_blksize = PAGE_SIZE;
			st.st_blocks  = body->mbo_blocks;
			st.st_atime   = body->mbo_atime;
			st.st_mtime   = body->mbo_mtime;
			st.st_ctime   = body->mbo_ctime;
			st.st_ino     = cl_fid_build_ino(&body->mbo_fid1,
							 sbi->ll_flags &
							 LL_SBI_32BIT_API);

			lmdp = (struct lov_user_mds_data __user *)arg;
			if (copy_to_user(&lmdp->lmd_st, &st, sizeof(st))) {
				rc = -EFAULT;
				goto out_req;
			}
		}

out_req:
		ptlrpc_req_finished(request);
		if (filename)
			ll_putname(filename);
		return rc;
	}
	case IOC_LOV_GETINFO: {
		struct lov_user_mds_data __user *lumd;
		struct lov_stripe_md *lsm;
		struct lov_user_md __user *lum;
		struct lov_mds_md *lmm;
		int lmmsize;
		lstat_t st;

		lumd = (struct lov_user_mds_data __user *)arg;
		lum = &lumd->lmd_lmm;

		rc = ll_get_max_mdsize(sbi, &lmmsize);
		if (rc)
			return rc;

		lmm = libcfs_kvzalloc(lmmsize, GFP_NOFS);
		if (!lmm)
			return -ENOMEM;
		if (copy_from_user(lmm, lum, lmmsize)) {
			rc = -EFAULT;
			goto free_lmm;
		}

		switch (lmm->lmm_magic) {
		case LOV_USER_MAGIC_V1:
			if (cpu_to_le32(LOV_USER_MAGIC_V1) == LOV_USER_MAGIC_V1)
				break;
			/* swab objects first so that stripes num will be sane */
			lustre_swab_lov_user_md_objects(
				((struct lov_user_md_v1 *)lmm)->lmm_objects,
				((struct lov_user_md_v1 *)lmm)->lmm_stripe_count);
			lustre_swab_lov_user_md_v1((struct lov_user_md_v1 *)lmm);
			break;
		case LOV_USER_MAGIC_V3:
			if (cpu_to_le32(LOV_USER_MAGIC_V3) == LOV_USER_MAGIC_V3)
				break;
			/* swab objects first so that stripes num will be sane */
			lustre_swab_lov_user_md_objects(
				((struct lov_user_md_v3 *)lmm)->lmm_objects,
				((struct lov_user_md_v3 *)lmm)->lmm_stripe_count);
			lustre_swab_lov_user_md_v3((struct lov_user_md_v3 *)lmm);
			break;
		default:
			rc = -EINVAL;
			goto free_lmm;
		}

		rc = obd_unpackmd(sbi->ll_dt_exp, &lsm, lmm, lmmsize);
		if (rc < 0) {
			rc = -ENOMEM;
			goto free_lmm;
		}

		/* Perform glimpse_size operation. */
		memset(&st, 0, sizeof(st));

		rc = ll_glimpse_ioctl(sbi, lsm, &st);
		if (rc)
			goto free_lsm;

		if (copy_to_user(&lumd->lmd_st, &st, sizeof(st))) {
			rc = -EFAULT;
			goto free_lsm;
		}

free_lsm:
		obd_free_memmd(sbi->ll_dt_exp, &lsm);
free_lmm:
		kvfree(lmm);
		return rc;
	}
	case OBD_IOC_QUOTACHECK: {
		struct obd_quotactl *oqctl;
		int error = 0;

		if (!capable(CFS_CAP_SYS_ADMIN))
			return -EPERM;

		oqctl = kzalloc(sizeof(*oqctl), GFP_NOFS);
		if (!oqctl)
			return -ENOMEM;
		oqctl->qc_type = arg;
		rc = obd_quotacheck(sbi->ll_md_exp, oqctl);
		if (rc < 0) {
			CDEBUG(D_INFO, "md_quotacheck failed: rc %d\n", rc);
			error = rc;
		}

		rc = obd_quotacheck(sbi->ll_dt_exp, oqctl);
		if (rc < 0)
			CDEBUG(D_INFO, "obd_quotacheck failed: rc %d\n", rc);

		kfree(oqctl);
		return error ?: rc;
	}
	case OBD_IOC_POLL_QUOTACHECK: {
		struct if_quotacheck *check;

		if (!capable(CFS_CAP_SYS_ADMIN))
			return -EPERM;

		check = kzalloc(sizeof(*check), GFP_NOFS);
		if (!check)
			return -ENOMEM;

		rc = obd_iocontrol(cmd, sbi->ll_md_exp, 0, (void *)check,
				   NULL);
		if (rc) {
			CDEBUG(D_QUOTA, "mdc ioctl %d failed: %d\n", cmd, rc);
			if (copy_to_user((void __user *)arg, check,
					 sizeof(*check)))
				CDEBUG(D_QUOTA, "copy_to_user failed\n");
			goto out_poll;
		}

		rc = obd_iocontrol(cmd, sbi->ll_dt_exp, 0, (void *)check,
				   NULL);
		if (rc) {
			CDEBUG(D_QUOTA, "osc ioctl %d failed: %d\n", cmd, rc);
			if (copy_to_user((void __user *)arg, check,
					 sizeof(*check)))
				CDEBUG(D_QUOTA, "copy_to_user failed\n");
			goto out_poll;
		}
out_poll:
		kfree(check);
		return rc;
	}
	case OBD_IOC_QUOTACTL: {
		struct if_quotactl *qctl;

		qctl = kzalloc(sizeof(*qctl), GFP_NOFS);
		if (!qctl)
			return -ENOMEM;

		if (copy_from_user(qctl, (void __user *)arg, sizeof(*qctl))) {
			rc = -EFAULT;
			goto out_quotactl;
		}

		rc = quotactl_ioctl(sbi, qctl);

		if (rc == 0 && copy_to_user((void __user *)arg, qctl,
					    sizeof(*qctl)))
			rc = -EFAULT;

out_quotactl:
		kfree(qctl);
		return rc;
	}
	case OBD_IOC_GETDTNAME:
	case OBD_IOC_GETMDNAME:
		return ll_get_obd_name(inode, cmd, arg);
	case LL_IOC_FLUSHCTX:
		return ll_flush_ctx(inode);
	case LL_IOC_GETOBDCOUNT: {
		int count, vallen;
		struct obd_export *exp;

		if (copy_from_user(&count, (int __user *)arg, sizeof(int)))
			return -EFAULT;

		/* get ost count when count is zero, get mdt count otherwise */
		exp = count ? sbi->ll_md_exp : sbi->ll_dt_exp;
		vallen = sizeof(count);
		rc = obd_get_info(NULL, exp, sizeof(KEY_TGT_COUNT),
				  KEY_TGT_COUNT, &vallen, &count, NULL);
		if (rc) {
			CERROR("get target count failed: %d\n", rc);
			return rc;
		}

		if (copy_to_user((int __user *)arg, &count, sizeof(int)))
			return -EFAULT;

		return 0;
	}
	case LL_IOC_PATH2FID:
		if (copy_to_user((void __user *)arg, ll_inode2fid(inode),
				 sizeof(struct lu_fid)))
			return -EFAULT;
		return 0;
	case LL_IOC_GET_CONNECT_FLAGS: {
		return obd_iocontrol(cmd, sbi->ll_md_exp, 0, NULL,
				     (void __user *)arg);
	}
	case OBD_IOC_CHANGELOG_SEND:
	case OBD_IOC_CHANGELOG_CLEAR:
		if (!capable(CFS_CAP_SYS_ADMIN))
			return -EPERM;

		rc = copy_and_ioctl(cmd, sbi->ll_md_exp, (void __user *)arg,
				    sizeof(struct ioc_changelog));
		return rc;
	case OBD_IOC_FID2PATH:
		return ll_fid2path(inode, (void __user *)arg);
	case LL_IOC_GETPARENT:
		return ll_getparent(file, (void __user *)arg);
	case LL_IOC_FID2MDTIDX: {
		struct obd_export *exp = ll_i2mdexp(inode);
		struct lu_fid fid;
		__u32 index;

		if (copy_from_user(&fid, (const struct lu_fid __user *)arg,
				   sizeof(fid)))
			return -EFAULT;

		/* Call mdc_iocontrol */
		rc = obd_iocontrol(LL_IOC_FID2MDTIDX, exp, sizeof(fid), &fid,
				   &index);
		if (rc)
			return rc;

		return index;
	}
	case LL_IOC_HSM_REQUEST: {
		struct hsm_user_request	*hur;
		ssize_t			 totalsize;

		hur = memdup_user((void __user *)arg, sizeof(*hur));
		if (IS_ERR(hur))
			return PTR_ERR(hur);

		/* Compute the whole struct size */
		totalsize = hur_len(hur);
		kfree(hur);
		if (totalsize < 0)
			return -E2BIG;

		/* Final size will be more than double totalsize */
		if (totalsize >= MDS_MAXREQSIZE / 3)
			return -E2BIG;

		hur = libcfs_kvzalloc(totalsize, GFP_NOFS);
		if (!hur)
			return -ENOMEM;

		/* Copy the whole struct */
		if (copy_from_user(hur, (void __user *)arg, totalsize)) {
			kvfree(hur);
			return -EFAULT;
		}

		if (hur->hur_request.hr_action == HUA_RELEASE) {
			const struct lu_fid *fid;
			struct inode *f;
			int i;

			for (i = 0; i < hur->hur_request.hr_itemcount; i++) {
				fid = &hur->hur_user_item[i].hui_fid;
				f = search_inode_for_lustre(inode->i_sb, fid);
				if (IS_ERR(f)) {
					rc = PTR_ERR(f);
					break;
				}

				rc = ll_hsm_release(f);
				iput(f);
				if (rc != 0)
					break;
			}
		} else {
			rc = obd_iocontrol(cmd, ll_i2mdexp(inode), totalsize,
					   hur, NULL);
		}

		kvfree(hur);

		return rc;
	}
	case LL_IOC_HSM_PROGRESS: {
		struct hsm_progress_kernel	hpk;
		struct hsm_progress		hp;

		if (copy_from_user(&hp, (void __user *)arg, sizeof(hp)))
			return -EFAULT;

		hpk.hpk_fid = hp.hp_fid;
		hpk.hpk_cookie = hp.hp_cookie;
		hpk.hpk_extent = hp.hp_extent;
		hpk.hpk_flags = hp.hp_flags;
		hpk.hpk_errval = hp.hp_errval;
		hpk.hpk_data_version = 0;

		/* File may not exist in Lustre; all progress
		 * reported to Lustre root
		 */
		rc = obd_iocontrol(cmd, sbi->ll_md_exp, sizeof(hpk), &hpk,
				   NULL);
		return rc;
	}
	case LL_IOC_HSM_CT_START:
		if (!capable(CFS_CAP_SYS_ADMIN))
			return -EPERM;

		rc = copy_and_ioctl(cmd, sbi->ll_md_exp, (void __user *)arg,
				    sizeof(struct lustre_kernelcomm));
		return rc;

	case LL_IOC_HSM_COPY_START: {
		struct hsm_copy	*copy;
		int		 rc;

		copy = memdup_user((char __user *)arg, sizeof(*copy));
		if (IS_ERR(copy))
			return PTR_ERR(copy);

		rc = ll_ioc_copy_start(inode->i_sb, copy);
		if (copy_to_user((char __user *)arg, copy, sizeof(*copy)))
			rc = -EFAULT;

		kfree(copy);
		return rc;
	}
	case LL_IOC_HSM_COPY_END: {
		struct hsm_copy	*copy;
		int		 rc;

		copy = memdup_user((char __user *)arg, sizeof(*copy));
		if (IS_ERR(copy))
			return PTR_ERR(copy);

		rc = ll_ioc_copy_end(inode->i_sb, copy);
		if (copy_to_user((char __user *)arg, copy, sizeof(*copy)))
			rc = -EFAULT;

		kfree(copy);
		return rc;
	}
	case LL_IOC_MIGRATE: {
		char *buf = NULL;
		const char *filename;
		int namelen = 0;
		int len;
		int rc;
		int mdtidx;

		rc = obd_ioctl_getdata(&buf, &len, (void __user *)arg);
		if (rc < 0)
			return rc;

		data = (struct obd_ioctl_data *)buf;
		if (!data->ioc_inlbuf1 || !data->ioc_inlbuf2 ||
		    !data->ioc_inllen1 || !data->ioc_inllen2) {
			rc = -EINVAL;
			goto migrate_free;
		}

		filename = data->ioc_inlbuf1;
		namelen = data->ioc_inllen1;
		if (namelen < 1 || namelen != strlen(filename) + 1) {
			rc = -EINVAL;
			goto migrate_free;
		}

		if (data->ioc_inllen2 != sizeof(mdtidx)) {
			rc = -EINVAL;
			goto migrate_free;
		}
		mdtidx = *(int *)data->ioc_inlbuf2;

		rc = ll_migrate(inode, file, mdtidx, filename, namelen - 1);
migrate_free:
		obd_ioctl_freedata(buf, len);

		return rc;
	}

	default:
		return obd_iocontrol(cmd, sbi->ll_dt_exp, 0, NULL,
				     (void __user *)arg);
	}
}

static loff_t ll_dir_seek(struct file *file, loff_t offset, int origin)
{
	struct inode *inode = file->f_mapping->host;
	struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	int api32 = ll_need_32bit_api(sbi);
	loff_t ret = -EINVAL;

	switch (origin) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += file->f_pos;
		break;
	case SEEK_END:
		if (offset > 0)
			goto out;
		if (api32)
			offset += LL_DIR_END_OFF_32BIT;
		else
			offset += LL_DIR_END_OFF;
		break;
	default:
		goto out;
	}

	if (offset >= 0 &&
	    ((api32 && offset <= LL_DIR_END_OFF_32BIT) ||
	     (!api32 && offset <= LL_DIR_END_OFF))) {
		if (offset != file->f_pos) {
			if ((api32 && offset == LL_DIR_END_OFF_32BIT) ||
			    (!api32 && offset == LL_DIR_END_OFF))
				fd->lfd_pos = MDS_DIR_END_OFF;
			else if (api32 && sbi->ll_flags & LL_SBI_64BIT_HASH)
				fd->lfd_pos = offset << 32;
			else
				fd->lfd_pos = offset;
			file->f_pos = offset;
			file->f_version = 0;
		}
		ret = offset;
	}
	goto out;

out:
	return ret;
}

static int ll_dir_open(struct inode *inode, struct file *file)
{
	return ll_file_open(inode, file);
}

static int ll_dir_release(struct inode *inode, struct file *file)
{
	return ll_file_release(inode, file);
}

const struct file_operations ll_dir_operations = {
	.llseek   = ll_dir_seek,
	.open     = ll_dir_open,
	.release  = ll_dir_release,
	.read     = generic_read_dir,
	.iterate_shared  = ll_readdir,
	.unlocked_ioctl   = ll_dir_ioctl,
	.fsync    = ll_fsync,
};

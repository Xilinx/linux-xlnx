/*
 * Module for pnfs flexfile layout driver.
 *
 * Copyright (c) 2014, Primary Data, Inc. All rights reserved.
 *
 * Tao Peng <bergwolf@primarydata.com>
 */

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/module.h>

#include <linux/sunrpc/metrics.h>

#include "flexfilelayout.h"
#include "../nfs4session.h"
#include "../nfs4idmap.h"
#include "../internal.h"
#include "../delegation.h"
#include "../nfs4trace.h"
#include "../iostat.h"
#include "../nfs.h"
#include "../nfs42.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

#define FF_LAYOUT_POLL_RETRY_MAX     (15*HZ)

static struct group_info	*ff_zero_group;

static struct pnfs_layout_hdr *
ff_layout_alloc_layout_hdr(struct inode *inode, gfp_t gfp_flags)
{
	struct nfs4_flexfile_layout *ffl;

	ffl = kzalloc(sizeof(*ffl), gfp_flags);
	if (ffl) {
		INIT_LIST_HEAD(&ffl->error_list);
		INIT_LIST_HEAD(&ffl->mirrors);
		ffl->last_report_time = ktime_get();
		return &ffl->generic_hdr;
	} else
		return NULL;
}

static void
ff_layout_free_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct nfs4_ff_layout_ds_err *err, *n;

	list_for_each_entry_safe(err, n, &FF_LAYOUT_FROM_HDR(lo)->error_list,
				 list) {
		list_del(&err->list);
		kfree(err);
	}
	kfree(FF_LAYOUT_FROM_HDR(lo));
}

static int decode_pnfs_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS4_STATEID_SIZE);
	if (unlikely(p == NULL))
		return -ENOBUFS;
	stateid->type = NFS4_PNFS_DS_STATEID_TYPE;
	memcpy(stateid->data, p, NFS4_STATEID_SIZE);
	dprintk("%s: stateid id= [%x%x%x%x]\n", __func__,
		p[0], p[1], p[2], p[3]);
	return 0;
}

static int decode_deviceid(struct xdr_stream *xdr, struct nfs4_deviceid *devid)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS4_DEVICEID4_SIZE);
	if (unlikely(!p))
		return -ENOBUFS;
	memcpy(devid, p, NFS4_DEVICEID4_SIZE);
	nfs4_print_deviceid(devid);
	return 0;
}

static int decode_nfs_fh(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -ENOBUFS;
	fh->size = be32_to_cpup(p++);
	if (fh->size > sizeof(struct nfs_fh)) {
		printk(KERN_ERR "NFS flexfiles: Too big fh received %d\n",
		       fh->size);
		return -EOVERFLOW;
	}
	/* fh.data */
	p = xdr_inline_decode(xdr, fh->size);
	if (unlikely(!p))
		return -ENOBUFS;
	memcpy(&fh->data, p, fh->size);
	dprintk("%s: fh len %d\n", __func__, fh->size);

	return 0;
}

/*
 * Currently only stringified uids and gids are accepted.
 * I.e., kerberos is not supported to the DSes, so no pricipals.
 *
 * That means that one common function will suffice, but when
 * principals are added, this should be split to accomodate
 * calls to both nfs_map_name_to_uid() and nfs_map_group_to_gid().
 */
static int
decode_name(struct xdr_stream *xdr, u32 *id)
{
	__be32 *p;
	int len;

	/* opaque_length(4)*/
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -ENOBUFS;
	len = be32_to_cpup(p++);
	if (len < 0)
		return -EINVAL;

	dprintk("%s: len %u\n", __func__, len);

	/* opaque body */
	p = xdr_inline_decode(xdr, len);
	if (unlikely(!p))
		return -ENOBUFS;

	if (!nfs_map_string_to_numeric((char *)p, len, id))
		return -EINVAL;

	return 0;
}

static bool ff_mirror_match_fh(const struct nfs4_ff_layout_mirror *m1,
		const struct nfs4_ff_layout_mirror *m2)
{
	int i, j;

	if (m1->fh_versions_cnt != m2->fh_versions_cnt)
		return false;
	for (i = 0; i < m1->fh_versions_cnt; i++) {
		bool found_fh = false;
		for (j = 0; j < m2->fh_versions_cnt; j++) {
			if (nfs_compare_fh(&m1->fh_versions[i],
					&m2->fh_versions[j]) == 0) {
				found_fh = true;
				break;
			}
		}
		if (!found_fh)
			return false;
	}
	return true;
}

static struct nfs4_ff_layout_mirror *
ff_layout_add_mirror(struct pnfs_layout_hdr *lo,
		struct nfs4_ff_layout_mirror *mirror)
{
	struct nfs4_flexfile_layout *ff_layout = FF_LAYOUT_FROM_HDR(lo);
	struct nfs4_ff_layout_mirror *pos;
	struct inode *inode = lo->plh_inode;

	spin_lock(&inode->i_lock);
	list_for_each_entry(pos, &ff_layout->mirrors, mirrors) {
		if (mirror->mirror_ds != pos->mirror_ds)
			continue;
		if (!ff_mirror_match_fh(mirror, pos))
			continue;
		if (atomic_inc_not_zero(&pos->ref)) {
			spin_unlock(&inode->i_lock);
			return pos;
		}
	}
	list_add(&mirror->mirrors, &ff_layout->mirrors);
	mirror->layout = lo;
	spin_unlock(&inode->i_lock);
	return mirror;
}

static void
ff_layout_remove_mirror(struct nfs4_ff_layout_mirror *mirror)
{
	struct inode *inode;
	if (mirror->layout == NULL)
		return;
	inode = mirror->layout->plh_inode;
	spin_lock(&inode->i_lock);
	list_del(&mirror->mirrors);
	spin_unlock(&inode->i_lock);
	mirror->layout = NULL;
}

static struct nfs4_ff_layout_mirror *ff_layout_alloc_mirror(gfp_t gfp_flags)
{
	struct nfs4_ff_layout_mirror *mirror;

	mirror = kzalloc(sizeof(*mirror), gfp_flags);
	if (mirror != NULL) {
		spin_lock_init(&mirror->lock);
		atomic_set(&mirror->ref, 1);
		INIT_LIST_HEAD(&mirror->mirrors);
	}
	return mirror;
}

static void ff_layout_free_mirror(struct nfs4_ff_layout_mirror *mirror)
{
	struct rpc_cred	*cred;

	ff_layout_remove_mirror(mirror);
	kfree(mirror->fh_versions);
	cred = rcu_access_pointer(mirror->ro_cred);
	if (cred)
		put_rpccred(cred);
	cred = rcu_access_pointer(mirror->rw_cred);
	if (cred)
		put_rpccred(cred);
	nfs4_ff_layout_put_deviceid(mirror->mirror_ds);
	kfree(mirror);
}

static void ff_layout_put_mirror(struct nfs4_ff_layout_mirror *mirror)
{
	if (mirror != NULL && atomic_dec_and_test(&mirror->ref))
		ff_layout_free_mirror(mirror);
}

static void ff_layout_free_mirror_array(struct nfs4_ff_layout_segment *fls)
{
	int i;

	if (fls->mirror_array) {
		for (i = 0; i < fls->mirror_array_cnt; i++) {
			/* normally mirror_ds is freed in
			 * .free_deviceid_node but we still do it here
			 * for .alloc_lseg error path */
			ff_layout_put_mirror(fls->mirror_array[i]);
		}
		kfree(fls->mirror_array);
		fls->mirror_array = NULL;
	}
}

static int ff_layout_check_layout(struct nfs4_layoutget_res *lgr)
{
	int ret = 0;

	dprintk("--> %s\n", __func__);

	/* FIXME: remove this check when layout segment support is added */
	if (lgr->range.offset != 0 ||
	    lgr->range.length != NFS4_MAX_UINT64) {
		dprintk("%s Only whole file layouts supported. Use MDS i/o\n",
			__func__);
		ret = -EINVAL;
	}

	dprintk("--> %s returns %d\n", __func__, ret);
	return ret;
}

static void _ff_layout_free_lseg(struct nfs4_ff_layout_segment *fls)
{
	if (fls) {
		ff_layout_free_mirror_array(fls);
		kfree(fls);
	}
}

static bool
ff_lseg_range_is_after(const struct pnfs_layout_range *l1,
		const struct pnfs_layout_range *l2)
{
	u64 end1, end2;

	if (l1->iomode != l2->iomode)
		return l1->iomode != IOMODE_READ;
	end1 = pnfs_calc_offset_end(l1->offset, l1->length);
	end2 = pnfs_calc_offset_end(l2->offset, l2->length);
	if (end1 < l2->offset)
		return false;
	if (end2 < l1->offset)
		return true;
	return l2->offset <= l1->offset;
}

static bool
ff_lseg_merge(struct pnfs_layout_segment *new,
		struct pnfs_layout_segment *old)
{
	u64 new_end, old_end;

	if (test_bit(NFS_LSEG_LAYOUTRETURN, &old->pls_flags))
		return false;
	if (new->pls_range.iomode != old->pls_range.iomode)
		return false;
	old_end = pnfs_calc_offset_end(old->pls_range.offset,
			old->pls_range.length);
	if (old_end < new->pls_range.offset)
		return false;
	new_end = pnfs_calc_offset_end(new->pls_range.offset,
			new->pls_range.length);
	if (new_end < old->pls_range.offset)
		return false;

	/* Mergeable: copy info from 'old' to 'new' */
	if (new_end < old_end)
		new_end = old_end;
	if (new->pls_range.offset < old->pls_range.offset)
		new->pls_range.offset = old->pls_range.offset;
	new->pls_range.length = pnfs_calc_offset_length(new->pls_range.offset,
			new_end);
	if (test_bit(NFS_LSEG_ROC, &old->pls_flags))
		set_bit(NFS_LSEG_ROC, &new->pls_flags);
	return true;
}

static void
ff_layout_add_lseg(struct pnfs_layout_hdr *lo,
		struct pnfs_layout_segment *lseg,
		struct list_head *free_me)
{
	pnfs_generic_layout_insert_lseg(lo, lseg,
			ff_lseg_range_is_after,
			ff_lseg_merge,
			free_me);
}

static void ff_layout_sort_mirrors(struct nfs4_ff_layout_segment *fls)
{
	int i, j;

	for (i = 0; i < fls->mirror_array_cnt - 1; i++) {
		for (j = i + 1; j < fls->mirror_array_cnt; j++)
			if (fls->mirror_array[i]->efficiency <
			    fls->mirror_array[j]->efficiency)
				swap(fls->mirror_array[i],
				     fls->mirror_array[j]);
	}
}

static void ff_layout_mark_devices_valid(struct nfs4_ff_layout_segment *fls)
{
	struct nfs4_deviceid_node *node;
	int i;

	if (!(fls->flags & FF_FLAGS_NO_IO_THRU_MDS))
		return;
	for (i = 0; i < fls->mirror_array_cnt; i++) {
		node = &fls->mirror_array[i]->mirror_ds->id_node;
		clear_bit(NFS_DEVICEID_UNAVAILABLE, &node->flags);
	}
}

static struct pnfs_layout_segment *
ff_layout_alloc_lseg(struct pnfs_layout_hdr *lh,
		     struct nfs4_layoutget_res *lgr,
		     gfp_t gfp_flags)
{
	struct pnfs_layout_segment *ret;
	struct nfs4_ff_layout_segment *fls = NULL;
	struct xdr_stream stream;
	struct xdr_buf buf;
	struct page *scratch;
	u64 stripe_unit;
	u32 mirror_array_cnt;
	__be32 *p;
	int i, rc;

	dprintk("--> %s\n", __func__);
	scratch = alloc_page(gfp_flags);
	if (!scratch)
		return ERR_PTR(-ENOMEM);

	xdr_init_decode_pages(&stream, &buf, lgr->layoutp->pages,
			      lgr->layoutp->len);
	xdr_set_scratch_buffer(&stream, page_address(scratch), PAGE_SIZE);

	/* stripe unit and mirror_array_cnt */
	rc = -EIO;
	p = xdr_inline_decode(&stream, 8 + 4);
	if (!p)
		goto out_err_free;

	p = xdr_decode_hyper(p, &stripe_unit);
	mirror_array_cnt = be32_to_cpup(p++);
	dprintk("%s: stripe_unit=%llu mirror_array_cnt=%u\n", __func__,
		stripe_unit, mirror_array_cnt);

	if (mirror_array_cnt > NFS4_FLEXFILE_LAYOUT_MAX_MIRROR_CNT ||
	    mirror_array_cnt == 0)
		goto out_err_free;

	rc = -ENOMEM;
	fls = kzalloc(sizeof(*fls), gfp_flags);
	if (!fls)
		goto out_err_free;

	fls->mirror_array_cnt = mirror_array_cnt;
	fls->stripe_unit = stripe_unit;
	fls->mirror_array = kcalloc(fls->mirror_array_cnt,
				    sizeof(fls->mirror_array[0]), gfp_flags);
	if (fls->mirror_array == NULL)
		goto out_err_free;

	for (i = 0; i < fls->mirror_array_cnt; i++) {
		struct nfs4_ff_layout_mirror *mirror;
		struct nfs4_deviceid devid;
		struct nfs4_deviceid_node *idnode;
		struct auth_cred acred = { .group_info = ff_zero_group };
		struct rpc_cred	__rcu *cred;
		u32 ds_count, fh_count, id;
		int j;

		rc = -EIO;
		p = xdr_inline_decode(&stream, 4);
		if (!p)
			goto out_err_free;
		ds_count = be32_to_cpup(p);

		/* FIXME: allow for striping? */
		if (ds_count != 1)
			goto out_err_free;

		fls->mirror_array[i] = ff_layout_alloc_mirror(gfp_flags);
		if (fls->mirror_array[i] == NULL) {
			rc = -ENOMEM;
			goto out_err_free;
		}

		fls->mirror_array[i]->ds_count = ds_count;

		/* deviceid */
		rc = decode_deviceid(&stream, &devid);
		if (rc)
			goto out_err_free;

		idnode = nfs4_find_get_deviceid(NFS_SERVER(lh->plh_inode),
						&devid, lh->plh_lc_cred,
						gfp_flags);
		/*
		 * upon success, mirror_ds is allocated by previous
		 * getdeviceinfo, or newly by .alloc_deviceid_node
		 * nfs4_find_get_deviceid failure is indeed getdeviceinfo falure
		 */
		if (idnode)
			fls->mirror_array[i]->mirror_ds =
				FF_LAYOUT_MIRROR_DS(idnode);
		else
			goto out_err_free;

		/* efficiency */
		rc = -EIO;
		p = xdr_inline_decode(&stream, 4);
		if (!p)
			goto out_err_free;
		fls->mirror_array[i]->efficiency = be32_to_cpup(p);

		/* stateid */
		rc = decode_pnfs_stateid(&stream, &fls->mirror_array[i]->stateid);
		if (rc)
			goto out_err_free;

		/* fh */
		p = xdr_inline_decode(&stream, 4);
		if (!p)
			goto out_err_free;
		fh_count = be32_to_cpup(p);

		fls->mirror_array[i]->fh_versions =
			kzalloc(fh_count * sizeof(struct nfs_fh),
				gfp_flags);
		if (fls->mirror_array[i]->fh_versions == NULL) {
			rc = -ENOMEM;
			goto out_err_free;
		}

		for (j = 0; j < fh_count; j++) {
			rc = decode_nfs_fh(&stream,
					   &fls->mirror_array[i]->fh_versions[j]);
			if (rc)
				goto out_err_free;
		}

		fls->mirror_array[i]->fh_versions_cnt = fh_count;

		/* user */
		rc = decode_name(&stream, &id);
		if (rc)
			goto out_err_free;

		acred.uid = make_kuid(&init_user_ns, id);

		/* group */
		rc = decode_name(&stream, &id);
		if (rc)
			goto out_err_free;

		acred.gid = make_kgid(&init_user_ns, id);

		/* find the cred for it */
		rcu_assign_pointer(cred, rpc_lookup_generic_cred(&acred, 0, gfp_flags));
		if (IS_ERR(cred)) {
			rc = PTR_ERR(cred);
			goto out_err_free;
		}

		if (lgr->range.iomode == IOMODE_READ)
			rcu_assign_pointer(fls->mirror_array[i]->ro_cred, cred);
		else
			rcu_assign_pointer(fls->mirror_array[i]->rw_cred, cred);

		mirror = ff_layout_add_mirror(lh, fls->mirror_array[i]);
		if (mirror != fls->mirror_array[i]) {
			/* swap cred ptrs so free_mirror will clean up old */
			if (lgr->range.iomode == IOMODE_READ) {
				cred = xchg(&mirror->ro_cred, cred);
				rcu_assign_pointer(fls->mirror_array[i]->ro_cred, cred);
			} else {
				cred = xchg(&mirror->rw_cred, cred);
				rcu_assign_pointer(fls->mirror_array[i]->rw_cred, cred);
			}
			ff_layout_free_mirror(fls->mirror_array[i]);
			fls->mirror_array[i] = mirror;
		}

		dprintk("%s: iomode %s uid %u gid %u\n", __func__,
			lgr->range.iomode == IOMODE_READ ? "READ" : "RW",
			from_kuid(&init_user_ns, acred.uid),
			from_kgid(&init_user_ns, acred.gid));
	}

	p = xdr_inline_decode(&stream, 4);
	if (!p)
		goto out_sort_mirrors;
	fls->flags = be32_to_cpup(p);

	p = xdr_inline_decode(&stream, 4);
	if (!p)
		goto out_sort_mirrors;
	for (i=0; i < fls->mirror_array_cnt; i++)
		fls->mirror_array[i]->report_interval = be32_to_cpup(p);

out_sort_mirrors:
	ff_layout_sort_mirrors(fls);
	rc = ff_layout_check_layout(lgr);
	if (rc)
		goto out_err_free;
	ff_layout_mark_devices_valid(fls);

	ret = &fls->generic_hdr;
	dprintk("<-- %s (success)\n", __func__);
out_free_page:
	__free_page(scratch);
	return ret;
out_err_free:
	_ff_layout_free_lseg(fls);
	ret = ERR_PTR(rc);
	dprintk("<-- %s (%d)\n", __func__, rc);
	goto out_free_page;
}

static bool ff_layout_has_rw_segments(struct pnfs_layout_hdr *layout)
{
	struct pnfs_layout_segment *lseg;

	list_for_each_entry(lseg, &layout->plh_segs, pls_list)
		if (lseg->pls_range.iomode == IOMODE_RW)
			return true;

	return false;
}

static void
ff_layout_free_lseg(struct pnfs_layout_segment *lseg)
{
	struct nfs4_ff_layout_segment *fls = FF_LAYOUT_LSEG(lseg);

	dprintk("--> %s\n", __func__);

	if (lseg->pls_range.iomode == IOMODE_RW) {
		struct nfs4_flexfile_layout *ffl;
		struct inode *inode;

		ffl = FF_LAYOUT_FROM_HDR(lseg->pls_layout);
		inode = ffl->generic_hdr.plh_inode;
		spin_lock(&inode->i_lock);
		if (!ff_layout_has_rw_segments(lseg->pls_layout)) {
			ffl->commit_info.nbuckets = 0;
			kfree(ffl->commit_info.buckets);
			ffl->commit_info.buckets = NULL;
		}
		spin_unlock(&inode->i_lock);
	}
	_ff_layout_free_lseg(fls);
}

/* Return 1 until we have multiple lsegs support */
static int
ff_layout_get_lseg_count(struct nfs4_ff_layout_segment *fls)
{
	return 1;
}

static void
nfs4_ff_start_busy_timer(struct nfs4_ff_busy_timer *timer, ktime_t now)
{
	/* first IO request? */
	if (atomic_inc_return(&timer->n_ops) == 1) {
		timer->start_time = now;
	}
}

static ktime_t
nfs4_ff_end_busy_timer(struct nfs4_ff_busy_timer *timer, ktime_t now)
{
	ktime_t start;

	if (atomic_dec_return(&timer->n_ops) < 0)
		WARN_ON_ONCE(1);

	start = timer->start_time;
	timer->start_time = now;
	return ktime_sub(now, start);
}

static bool
nfs4_ff_layoutstat_start_io(struct nfs4_ff_layout_mirror *mirror,
			    struct nfs4_ff_layoutstat *layoutstat,
			    ktime_t now)
{
	static const ktime_t notime = {0};
	s64 report_interval = FF_LAYOUTSTATS_REPORT_INTERVAL;
	struct nfs4_flexfile_layout *ffl = FF_LAYOUT_FROM_HDR(mirror->layout);

	nfs4_ff_start_busy_timer(&layoutstat->busy_timer, now);
	if (ktime_equal(mirror->start_time, notime))
		mirror->start_time = now;
	if (mirror->report_interval != 0)
		report_interval = (s64)mirror->report_interval * 1000LL;
	else if (layoutstats_timer != 0)
		report_interval = (s64)layoutstats_timer * 1000LL;
	if (ktime_to_ms(ktime_sub(now, ffl->last_report_time)) >=
			report_interval) {
		ffl->last_report_time = now;
		return true;
	}

	return false;
}

static void
nfs4_ff_layout_stat_io_update_requested(struct nfs4_ff_layoutstat *layoutstat,
		__u64 requested)
{
	struct nfs4_ff_io_stat *iostat = &layoutstat->io_stat;

	iostat->ops_requested++;
	iostat->bytes_requested += requested;
}

static void
nfs4_ff_layout_stat_io_update_completed(struct nfs4_ff_layoutstat *layoutstat,
		__u64 requested,
		__u64 completed,
		ktime_t time_completed,
		ktime_t time_started)
{
	struct nfs4_ff_io_stat *iostat = &layoutstat->io_stat;
	ktime_t completion_time = ktime_sub(time_completed, time_started);
	ktime_t timer;

	iostat->ops_completed++;
	iostat->bytes_completed += completed;
	iostat->bytes_not_delivered += requested - completed;

	timer = nfs4_ff_end_busy_timer(&layoutstat->busy_timer, time_completed);
	iostat->total_busy_time =
			ktime_add(iostat->total_busy_time, timer);
	iostat->aggregate_completion_time =
			ktime_add(iostat->aggregate_completion_time,
					completion_time);
}

static void
nfs4_ff_layout_stat_io_start_read(struct inode *inode,
		struct nfs4_ff_layout_mirror *mirror,
		__u64 requested, ktime_t now)
{
	bool report;

	spin_lock(&mirror->lock);
	report = nfs4_ff_layoutstat_start_io(mirror, &mirror->read_stat, now);
	nfs4_ff_layout_stat_io_update_requested(&mirror->read_stat, requested);
	spin_unlock(&mirror->lock);

	if (report)
		pnfs_report_layoutstat(inode, GFP_KERNEL);
}

static void
nfs4_ff_layout_stat_io_end_read(struct rpc_task *task,
		struct nfs4_ff_layout_mirror *mirror,
		__u64 requested,
		__u64 completed)
{
	spin_lock(&mirror->lock);
	nfs4_ff_layout_stat_io_update_completed(&mirror->read_stat,
			requested, completed,
			ktime_get(), task->tk_start);
	spin_unlock(&mirror->lock);
}

static void
nfs4_ff_layout_stat_io_start_write(struct inode *inode,
		struct nfs4_ff_layout_mirror *mirror,
		__u64 requested, ktime_t now)
{
	bool report;

	spin_lock(&mirror->lock);
	report = nfs4_ff_layoutstat_start_io(mirror , &mirror->write_stat, now);
	nfs4_ff_layout_stat_io_update_requested(&mirror->write_stat, requested);
	spin_unlock(&mirror->lock);

	if (report)
		pnfs_report_layoutstat(inode, GFP_NOIO);
}

static void
nfs4_ff_layout_stat_io_end_write(struct rpc_task *task,
		struct nfs4_ff_layout_mirror *mirror,
		__u64 requested,
		__u64 completed,
		enum nfs3_stable_how committed)
{
	if (committed == NFS_UNSTABLE)
		requested = completed = 0;

	spin_lock(&mirror->lock);
	nfs4_ff_layout_stat_io_update_completed(&mirror->write_stat,
			requested, completed, ktime_get(), task->tk_start);
	spin_unlock(&mirror->lock);
}

static int
ff_layout_alloc_commit_info(struct pnfs_layout_segment *lseg,
			    struct nfs_commit_info *cinfo,
			    gfp_t gfp_flags)
{
	struct nfs4_ff_layout_segment *fls = FF_LAYOUT_LSEG(lseg);
	struct pnfs_commit_bucket *buckets;
	int size;

	if (cinfo->ds->nbuckets != 0) {
		/* This assumes there is only one RW lseg per file.
		 * To support multiple lseg per file, we need to
		 * change struct pnfs_commit_bucket to allow dynamic
		 * increasing nbuckets.
		 */
		return 0;
	}

	size = ff_layout_get_lseg_count(fls) * FF_LAYOUT_MIRROR_COUNT(lseg);

	buckets = kcalloc(size, sizeof(struct pnfs_commit_bucket),
			  gfp_flags);
	if (!buckets)
		return -ENOMEM;
	else {
		int i;

		spin_lock(&cinfo->inode->i_lock);
		if (cinfo->ds->nbuckets != 0)
			kfree(buckets);
		else {
			cinfo->ds->buckets = buckets;
			cinfo->ds->nbuckets = size;
			for (i = 0; i < size; i++) {
				INIT_LIST_HEAD(&buckets[i].written);
				INIT_LIST_HEAD(&buckets[i].committing);
				/* mark direct verifier as unset */
				buckets[i].direct_verf.committed =
					NFS_INVALID_STABLE_HOW;
			}
		}
		spin_unlock(&cinfo->inode->i_lock);
		return 0;
	}
}

static struct nfs4_pnfs_ds *
ff_layout_choose_best_ds_for_read(struct pnfs_layout_segment *lseg,
				  int start_idx,
				  int *best_idx)
{
	struct nfs4_ff_layout_segment *fls = FF_LAYOUT_LSEG(lseg);
	struct nfs4_pnfs_ds *ds;
	bool fail_return = false;
	int idx;

	/* mirrors are sorted by efficiency */
	for (idx = start_idx; idx < fls->mirror_array_cnt; idx++) {
		if (idx+1 == fls->mirror_array_cnt)
			fail_return = true;
		ds = nfs4_ff_layout_prepare_ds(lseg, idx, fail_return);
		if (ds) {
			*best_idx = idx;
			return ds;
		}
	}

	return NULL;
}

static void
ff_layout_pg_get_read(struct nfs_pageio_descriptor *pgio,
		      struct nfs_page *req,
		      bool strict_iomode)
{
retry_strict:
	pnfs_put_lseg(pgio->pg_lseg);
	pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
					   req->wb_context,
					   0,
					   NFS4_MAX_UINT64,
					   IOMODE_READ,
					   strict_iomode,
					   GFP_KERNEL);
	if (IS_ERR(pgio->pg_lseg)) {
		pgio->pg_error = PTR_ERR(pgio->pg_lseg);
		pgio->pg_lseg = NULL;
	}

	/* If we don't have checking, do get a IOMODE_RW
	 * segment, and the server wants to avoid READs
	 * there, then retry!
	 */
	if (pgio->pg_lseg && !strict_iomode &&
	    ff_layout_avoid_read_on_rw(pgio->pg_lseg)) {
		strict_iomode = true;
		goto retry_strict;
	}
}

static void
ff_layout_pg_init_read(struct nfs_pageio_descriptor *pgio,
			struct nfs_page *req)
{
	struct nfs_pgio_mirror *pgm;
	struct nfs4_ff_layout_mirror *mirror;
	struct nfs4_pnfs_ds *ds;
	int ds_idx;

retry:
	/* Use full layout for now */
	if (!pgio->pg_lseg)
		ff_layout_pg_get_read(pgio, req, false);
	else if (ff_layout_avoid_read_on_rw(pgio->pg_lseg))
		ff_layout_pg_get_read(pgio, req, true);

	/* If no lseg, fall back to read through mds */
	if (pgio->pg_lseg == NULL)
		goto out_mds;

	ds = ff_layout_choose_best_ds_for_read(pgio->pg_lseg, 0, &ds_idx);
	if (!ds) {
		if (!ff_layout_no_fallback_to_mds(pgio->pg_lseg))
			goto out_mds;
		pnfs_put_lseg(pgio->pg_lseg);
		pgio->pg_lseg = NULL;
		/* Sleep for 1 second before retrying */
		ssleep(1);
		goto retry;
	}

	mirror = FF_LAYOUT_COMP(pgio->pg_lseg, ds_idx);

	pgio->pg_mirror_idx = ds_idx;

	/* read always uses only one mirror - idx 0 for pgio layer */
	pgm = &pgio->pg_mirrors[0];
	pgm->pg_bsize = mirror->mirror_ds->ds_versions[0].rsize;

	return;
out_mds:
	pnfs_put_lseg(pgio->pg_lseg);
	pgio->pg_lseg = NULL;
	nfs_pageio_reset_read_mds(pgio);
}

static void
ff_layout_pg_init_write(struct nfs_pageio_descriptor *pgio,
			struct nfs_page *req)
{
	struct nfs4_ff_layout_mirror *mirror;
	struct nfs_pgio_mirror *pgm;
	struct nfs_commit_info cinfo;
	struct nfs4_pnfs_ds *ds;
	int i;
	int status;

retry:
	if (!pgio->pg_lseg) {
		pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
						   req->wb_context,
						   0,
						   NFS4_MAX_UINT64,
						   IOMODE_RW,
						   false,
						   GFP_NOFS);
		if (IS_ERR(pgio->pg_lseg)) {
			pgio->pg_error = PTR_ERR(pgio->pg_lseg);
			pgio->pg_lseg = NULL;
			return;
		}
	}
	/* If no lseg, fall back to write through mds */
	if (pgio->pg_lseg == NULL)
		goto out_mds;

	nfs_init_cinfo(&cinfo, pgio->pg_inode, pgio->pg_dreq);
	status = ff_layout_alloc_commit_info(pgio->pg_lseg, &cinfo, GFP_NOFS);
	if (status < 0)
		goto out_mds;

	/* Use a direct mapping of ds_idx to pgio mirror_idx */
	if (WARN_ON_ONCE(pgio->pg_mirror_count !=
	    FF_LAYOUT_MIRROR_COUNT(pgio->pg_lseg)))
		goto out_mds;

	for (i = 0; i < pgio->pg_mirror_count; i++) {
		ds = nfs4_ff_layout_prepare_ds(pgio->pg_lseg, i, true);
		if (!ds) {
			if (!ff_layout_no_fallback_to_mds(pgio->pg_lseg))
				goto out_mds;
			pnfs_put_lseg(pgio->pg_lseg);
			pgio->pg_lseg = NULL;
			/* Sleep for 1 second before retrying */
			ssleep(1);
			goto retry;
		}
		pgm = &pgio->pg_mirrors[i];
		mirror = FF_LAYOUT_COMP(pgio->pg_lseg, i);
		pgm->pg_bsize = mirror->mirror_ds->ds_versions[0].wsize;
	}

	return;

out_mds:
	pnfs_put_lseg(pgio->pg_lseg);
	pgio->pg_lseg = NULL;
	nfs_pageio_reset_write_mds(pgio);
}

static unsigned int
ff_layout_pg_get_mirror_count_write(struct nfs_pageio_descriptor *pgio,
				    struct nfs_page *req)
{
	if (!pgio->pg_lseg) {
		pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
						   req->wb_context,
						   0,
						   NFS4_MAX_UINT64,
						   IOMODE_RW,
						   false,
						   GFP_NOFS);
		if (IS_ERR(pgio->pg_lseg)) {
			pgio->pg_error = PTR_ERR(pgio->pg_lseg);
			pgio->pg_lseg = NULL;
			goto out;
		}
	}
	if (pgio->pg_lseg)
		return FF_LAYOUT_MIRROR_COUNT(pgio->pg_lseg);

	/* no lseg means that pnfs is not in use, so no mirroring here */
	nfs_pageio_reset_write_mds(pgio);
out:
	return 1;
}

static const struct nfs_pageio_ops ff_layout_pg_read_ops = {
	.pg_init = ff_layout_pg_init_read,
	.pg_test = pnfs_generic_pg_test,
	.pg_doio = pnfs_generic_pg_readpages,
	.pg_cleanup = pnfs_generic_pg_cleanup,
};

static const struct nfs_pageio_ops ff_layout_pg_write_ops = {
	.pg_init = ff_layout_pg_init_write,
	.pg_test = pnfs_generic_pg_test,
	.pg_doio = pnfs_generic_pg_writepages,
	.pg_get_mirror_count = ff_layout_pg_get_mirror_count_write,
	.pg_cleanup = pnfs_generic_pg_cleanup,
};

static void ff_layout_reset_write(struct nfs_pgio_header *hdr, bool retry_pnfs)
{
	struct rpc_task *task = &hdr->task;

	pnfs_layoutcommit_inode(hdr->inode, false);

	if (retry_pnfs) {
		dprintk("%s Reset task %5u for i/o through pNFS "
			"(req %s/%llu, %u bytes @ offset %llu)\n", __func__,
			hdr->task.tk_pid,
			hdr->inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(hdr->inode),
			hdr->args.count,
			(unsigned long long)hdr->args.offset);

		hdr->completion_ops->reschedule_io(hdr);
		return;
	}

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		dprintk("%s Reset task %5u for i/o through MDS "
			"(req %s/%llu, %u bytes @ offset %llu)\n", __func__,
			hdr->task.tk_pid,
			hdr->inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(hdr->inode),
			hdr->args.count,
			(unsigned long long)hdr->args.offset);

		task->tk_status = pnfs_write_done_resend_to_mds(hdr);
	}
}

static void ff_layout_reset_read(struct nfs_pgio_header *hdr)
{
	struct rpc_task *task = &hdr->task;

	pnfs_layoutcommit_inode(hdr->inode, false);

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		dprintk("%s Reset task %5u for i/o through MDS "
			"(req %s/%llu, %u bytes @ offset %llu)\n", __func__,
			hdr->task.tk_pid,
			hdr->inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(hdr->inode),
			hdr->args.count,
			(unsigned long long)hdr->args.offset);

		task->tk_status = pnfs_read_done_resend_to_mds(hdr);
	}
}

static int ff_layout_async_handle_error_v4(struct rpc_task *task,
					   struct nfs4_state *state,
					   struct nfs_client *clp,
					   struct pnfs_layout_segment *lseg,
					   int idx)
{
	struct pnfs_layout_hdr *lo = lseg->pls_layout;
	struct inode *inode = lo->plh_inode;
	struct nfs_server *mds_server = NFS_SERVER(inode);

	struct nfs4_deviceid_node *devid = FF_LAYOUT_DEVID_NODE(lseg, idx);
	struct nfs_client *mds_client = mds_server->nfs_client;
	struct nfs4_slot_table *tbl = &clp->cl_session->fc_slot_table;

	if (task->tk_status >= 0)
		return 0;

	switch (task->tk_status) {
	/* MDS state errors */
	case -NFS4ERR_DELEG_REVOKED:
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_BAD_STATEID:
		if (state == NULL)
			break;
		nfs_remove_bad_delegation(state->inode, NULL);
	case -NFS4ERR_OPENMODE:
		if (state == NULL)
			break;
		if (nfs4_schedule_stateid_recovery(mds_server, state) < 0)
			goto out_bad_stateid;
		goto wait_on_recovery;
	case -NFS4ERR_EXPIRED:
		if (state != NULL) {
			if (nfs4_schedule_stateid_recovery(mds_server, state) < 0)
				goto out_bad_stateid;
		}
		nfs4_schedule_lease_recovery(mds_client);
		goto wait_on_recovery;
	/* DS session errors */
	case -NFS4ERR_BADSESSION:
	case -NFS4ERR_BADSLOT:
	case -NFS4ERR_BAD_HIGH_SLOT:
	case -NFS4ERR_DEADSESSION:
	case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
	case -NFS4ERR_SEQ_FALSE_RETRY:
	case -NFS4ERR_SEQ_MISORDERED:
		dprintk("%s ERROR %d, Reset session. Exchangeid "
			"flags 0x%x\n", __func__, task->tk_status,
			clp->cl_exchange_flags);
		nfs4_schedule_session_recovery(clp->cl_session, task->tk_status);
		break;
	case -NFS4ERR_DELAY:
	case -NFS4ERR_GRACE:
		rpc_delay(task, FF_LAYOUT_POLL_RETRY_MAX);
		break;
	case -NFS4ERR_RETRY_UNCACHED_REP:
		break;
	/* Invalidate Layout errors */
	case -NFS4ERR_PNFS_NO_LAYOUT:
	case -ESTALE:           /* mapped NFS4ERR_STALE */
	case -EBADHANDLE:       /* mapped NFS4ERR_BADHANDLE */
	case -EISDIR:           /* mapped NFS4ERR_ISDIR */
	case -NFS4ERR_FHEXPIRED:
	case -NFS4ERR_WRONG_TYPE:
		dprintk("%s Invalid layout error %d\n", __func__,
			task->tk_status);
		/*
		 * Destroy layout so new i/o will get a new layout.
		 * Layout will not be destroyed until all current lseg
		 * references are put. Mark layout as invalid to resend failed
		 * i/o and all i/o waiting on the slot table to the MDS until
		 * layout is destroyed and a new valid layout is obtained.
		 */
		pnfs_destroy_layout(NFS_I(inode));
		rpc_wake_up(&tbl->slot_tbl_waitq);
		goto reset;
	/* RPC connection errors */
	case -ECONNREFUSED:
	case -EHOSTDOWN:
	case -EHOSTUNREACH:
	case -ENETUNREACH:
	case -EIO:
	case -ETIMEDOUT:
	case -EPIPE:
		dprintk("%s DS connection error %d\n", __func__,
			task->tk_status);
		nfs4_mark_deviceid_unavailable(devid);
		rpc_wake_up(&tbl->slot_tbl_waitq);
		/* fall through */
	default:
		if (ff_layout_avoid_mds_available_ds(lseg))
			return -NFS4ERR_RESET_TO_PNFS;
reset:
		dprintk("%s Retry through MDS. Error %d\n", __func__,
			task->tk_status);
		return -NFS4ERR_RESET_TO_MDS;
	}
out:
	task->tk_status = 0;
	return -EAGAIN;
out_bad_stateid:
	task->tk_status = -EIO;
	return 0;
wait_on_recovery:
	rpc_sleep_on(&mds_client->cl_rpcwaitq, task, NULL);
	if (test_bit(NFS4CLNT_MANAGER_RUNNING, &mds_client->cl_state) == 0)
		rpc_wake_up_queued_task(&mds_client->cl_rpcwaitq, task);
	goto out;
}

/* Retry all errors through either pNFS or MDS except for -EJUKEBOX */
static int ff_layout_async_handle_error_v3(struct rpc_task *task,
					   struct pnfs_layout_segment *lseg,
					   int idx)
{
	struct nfs4_deviceid_node *devid = FF_LAYOUT_DEVID_NODE(lseg, idx);

	if (task->tk_status >= 0)
		return 0;

	switch (task->tk_status) {
	/* File access problems. Don't mark the device as unavailable */
	case -EACCES:
	case -ESTALE:
	case -EISDIR:
	case -EBADHANDLE:
	case -ELOOP:
	case -ENOSPC:
		break;
	case -EJUKEBOX:
		nfs_inc_stats(lseg->pls_layout->plh_inode, NFSIOS_DELAY);
		goto out_retry;
	default:
		dprintk("%s DS connection error %d\n", __func__,
			task->tk_status);
		nfs4_mark_deviceid_unavailable(devid);
	}
	/* FIXME: Need to prevent infinite looping here. */
	return -NFS4ERR_RESET_TO_PNFS;
out_retry:
	task->tk_status = 0;
	rpc_restart_call_prepare(task);
	rpc_delay(task, NFS_JUKEBOX_RETRY_TIME);
	return -EAGAIN;
}

static int ff_layout_async_handle_error(struct rpc_task *task,
					struct nfs4_state *state,
					struct nfs_client *clp,
					struct pnfs_layout_segment *lseg,
					int idx)
{
	int vers = clp->cl_nfs_mod->rpc_vers->number;

	switch (vers) {
	case 3:
		return ff_layout_async_handle_error_v3(task, lseg, idx);
	case 4:
		return ff_layout_async_handle_error_v4(task, state, clp,
						       lseg, idx);
	default:
		/* should never happen */
		WARN_ON_ONCE(1);
		return 0;
	}
}

static void ff_layout_io_track_ds_error(struct pnfs_layout_segment *lseg,
					int idx, u64 offset, u64 length,
					u32 status, int opnum, int error)
{
	struct nfs4_ff_layout_mirror *mirror;
	int err;

	if (status == 0) {
		switch (error) {
		case -ETIMEDOUT:
		case -EPFNOSUPPORT:
		case -EPROTONOSUPPORT:
		case -EOPNOTSUPP:
		case -ECONNREFUSED:
		case -ECONNRESET:
		case -EHOSTDOWN:
		case -EHOSTUNREACH:
		case -ENETUNREACH:
		case -EADDRINUSE:
		case -ENOBUFS:
		case -EPIPE:
		case -EPERM:
			status = NFS4ERR_NXIO;
			break;
		case -EACCES:
			status = NFS4ERR_ACCESS;
			break;
		default:
			return;
		}
	}

	switch (status) {
	case NFS4ERR_DELAY:
	case NFS4ERR_GRACE:
		return;
	default:
		break;
	}

	mirror = FF_LAYOUT_COMP(lseg, idx);
	err = ff_layout_track_ds_error(FF_LAYOUT_FROM_HDR(lseg->pls_layout),
				       mirror, offset, length, status, opnum,
				       GFP_NOIO);
	pnfs_error_mark_layout_for_return(lseg->pls_layout->plh_inode, lseg);
	dprintk("%s: err %d op %d status %u\n", __func__, err, opnum, status);
}

/* NFS_PROTO call done callback routines */
static int ff_layout_read_done_cb(struct rpc_task *task,
				struct nfs_pgio_header *hdr)
{
	int err;

	trace_nfs4_pnfs_read(hdr, task->tk_status);
	if (task->tk_status < 0)
		ff_layout_io_track_ds_error(hdr->lseg, hdr->pgio_mirror_idx,
					    hdr->args.offset, hdr->args.count,
					    hdr->res.op_status, OP_READ,
					    task->tk_status);
	err = ff_layout_async_handle_error(task, hdr->args.context->state,
					   hdr->ds_clp, hdr->lseg,
					   hdr->pgio_mirror_idx);

	switch (err) {
	case -NFS4ERR_RESET_TO_PNFS:
		if (ff_layout_choose_best_ds_for_read(hdr->lseg,
					hdr->pgio_mirror_idx + 1,
					&hdr->pgio_mirror_idx))
			goto out_eagain;
		pnfs_read_resend_pnfs(hdr);
		return task->tk_status;
	case -NFS4ERR_RESET_TO_MDS:
		ff_layout_reset_read(hdr);
		return task->tk_status;
	case -EAGAIN:
		goto out_eagain;
	}

	return 0;
out_eagain:
	rpc_restart_call_prepare(task);
	return -EAGAIN;
}

static bool
ff_layout_need_layoutcommit(struct pnfs_layout_segment *lseg)
{
	return !(FF_LAYOUT_LSEG(lseg)->flags & FF_FLAGS_NO_LAYOUTCOMMIT);
}

/*
 * We reference the rpc_cred of the first WRITE that triggers the need for
 * a LAYOUTCOMMIT, and use it to send the layoutcommit compound.
 * rfc5661 is not clear about which credential should be used.
 *
 * Flexlayout client should treat DS replied FILE_SYNC as DATA_SYNC, so
 * to follow http://www.rfc-editor.org/errata_search.php?rfc=5661&eid=2751
 * we always send layoutcommit after DS writes.
 */
static void
ff_layout_set_layoutcommit(struct inode *inode,
		struct pnfs_layout_segment *lseg,
		loff_t end_offset)
{
	if (!ff_layout_need_layoutcommit(lseg))
		return;

	pnfs_set_layoutcommit(inode, lseg, end_offset);
	dprintk("%s inode %lu pls_end_pos %llu\n", __func__, inode->i_ino,
		(unsigned long long) NFS_I(inode)->layout->plh_lwb);
}

static bool
ff_layout_device_unavailable(struct pnfs_layout_segment *lseg, int idx)
{
	/* No mirroring for now */
	struct nfs4_deviceid_node *node = FF_LAYOUT_DEVID_NODE(lseg, idx);

	return ff_layout_test_devid_unavailable(node);
}

static void ff_layout_read_record_layoutstats_start(struct rpc_task *task,
		struct nfs_pgio_header *hdr)
{
	if (test_and_set_bit(NFS_IOHDR_STAT, &hdr->flags))
		return;
	nfs4_ff_layout_stat_io_start_read(hdr->inode,
			FF_LAYOUT_COMP(hdr->lseg, hdr->pgio_mirror_idx),
			hdr->args.count,
			task->tk_start);
}

static void ff_layout_read_record_layoutstats_done(struct rpc_task *task,
		struct nfs_pgio_header *hdr)
{
	if (!test_and_clear_bit(NFS_IOHDR_STAT, &hdr->flags))
		return;
	nfs4_ff_layout_stat_io_end_read(task,
			FF_LAYOUT_COMP(hdr->lseg, hdr->pgio_mirror_idx),
			hdr->args.count,
			hdr->res.count);
}

static int ff_layout_read_prepare_common(struct rpc_task *task,
					 struct nfs_pgio_header *hdr)
{
	if (unlikely(test_bit(NFS_CONTEXT_BAD, &hdr->args.context->flags))) {
		rpc_exit(task, -EIO);
		return -EIO;
	}
	if (ff_layout_device_unavailable(hdr->lseg, hdr->pgio_mirror_idx)) {
		rpc_exit(task, -EHOSTDOWN);
		return -EAGAIN;
	}

	ff_layout_read_record_layoutstats_start(task, hdr);
	return 0;
}

/*
 * Call ops for the async read/write cases
 * In the case of dense layouts, the offset needs to be reset to its
 * original value.
 */
static void ff_layout_read_prepare_v3(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (ff_layout_read_prepare_common(task, hdr))
		return;

	rpc_call_start(task);
}

static int ff_layout_setup_sequence(struct nfs_client *ds_clp,
				    struct nfs4_sequence_args *args,
				    struct nfs4_sequence_res *res,
				    struct rpc_task *task)
{
	if (ds_clp->cl_session)
		return nfs41_setup_sequence(ds_clp->cl_session,
					   args,
					   res,
					   task);
	return nfs40_setup_sequence(ds_clp->cl_slot_tbl,
				   args,
				   res,
				   task);
}

static void ff_layout_read_prepare_v4(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (ff_layout_setup_sequence(hdr->ds_clp,
				     &hdr->args.seq_args,
				     &hdr->res.seq_res,
				     task))
		return;

	if (ff_layout_read_prepare_common(task, hdr))
		return;

	if (nfs4_set_rw_stateid(&hdr->args.stateid, hdr->args.context,
			hdr->args.lock_context, FMODE_READ) == -EIO)
		rpc_exit(task, -EIO); /* lost lock, terminate I/O */
}

static void ff_layout_read_call_done(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	dprintk("--> %s task->tk_status %d\n", __func__, task->tk_status);

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags) &&
	    task->tk_status == 0) {
		nfs4_sequence_done(task, &hdr->res.seq_res);
		return;
	}

	/* Note this may cause RPC to be resent */
	hdr->mds_ops->rpc_call_done(task, hdr);
}

static void ff_layout_read_count_stats(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	ff_layout_read_record_layoutstats_done(task, hdr);
	rpc_count_iostats_metrics(task,
	    &NFS_CLIENT(hdr->inode)->cl_metrics[NFSPROC4_CLNT_READ]);
}

static void ff_layout_read_release(void *data)
{
	struct nfs_pgio_header *hdr = data;

	ff_layout_read_record_layoutstats_done(&hdr->task, hdr);
	pnfs_generic_rw_release(data);
}


static int ff_layout_write_done_cb(struct rpc_task *task,
				struct nfs_pgio_header *hdr)
{
	loff_t end_offs = 0;
	int err;

	trace_nfs4_pnfs_write(hdr, task->tk_status);
	if (task->tk_status < 0)
		ff_layout_io_track_ds_error(hdr->lseg, hdr->pgio_mirror_idx,
					    hdr->args.offset, hdr->args.count,
					    hdr->res.op_status, OP_WRITE,
					    task->tk_status);
	err = ff_layout_async_handle_error(task, hdr->args.context->state,
					   hdr->ds_clp, hdr->lseg,
					   hdr->pgio_mirror_idx);

	switch (err) {
	case -NFS4ERR_RESET_TO_PNFS:
		ff_layout_reset_write(hdr, true);
		return task->tk_status;
	case -NFS4ERR_RESET_TO_MDS:
		ff_layout_reset_write(hdr, false);
		return task->tk_status;
	case -EAGAIN:
		return -EAGAIN;
	}

	if (hdr->res.verf->committed == NFS_FILE_SYNC ||
	    hdr->res.verf->committed == NFS_DATA_SYNC)
		end_offs = hdr->mds_offset + (loff_t)hdr->res.count;

	/* Note: if the write is unstable, don't set end_offs until commit */
	ff_layout_set_layoutcommit(hdr->inode, hdr->lseg, end_offs);

	/* zero out fattr since we don't care DS attr at all */
	hdr->fattr.valid = 0;
	if (task->tk_status >= 0)
		nfs_writeback_update_inode(hdr);

	return 0;
}

static int ff_layout_commit_done_cb(struct rpc_task *task,
				     struct nfs_commit_data *data)
{
	int err;

	trace_nfs4_pnfs_commit_ds(data, task->tk_status);
	if (task->tk_status < 0)
		ff_layout_io_track_ds_error(data->lseg, data->ds_commit_index,
					    data->args.offset, data->args.count,
					    data->res.op_status, OP_COMMIT,
					    task->tk_status);
	err = ff_layout_async_handle_error(task, NULL, data->ds_clp,
					   data->lseg, data->ds_commit_index);

	switch (err) {
	case -NFS4ERR_RESET_TO_PNFS:
		pnfs_generic_prepare_to_resend_writes(data);
		return -EAGAIN;
	case -NFS4ERR_RESET_TO_MDS:
		pnfs_generic_prepare_to_resend_writes(data);
		return -EAGAIN;
	case -EAGAIN:
		rpc_restart_call_prepare(task);
		return -EAGAIN;
	}

	ff_layout_set_layoutcommit(data->inode, data->lseg, data->lwb);

	return 0;
}

static void ff_layout_write_record_layoutstats_start(struct rpc_task *task,
		struct nfs_pgio_header *hdr)
{
	if (test_and_set_bit(NFS_IOHDR_STAT, &hdr->flags))
		return;
	nfs4_ff_layout_stat_io_start_write(hdr->inode,
			FF_LAYOUT_COMP(hdr->lseg, hdr->pgio_mirror_idx),
			hdr->args.count,
			task->tk_start);
}

static void ff_layout_write_record_layoutstats_done(struct rpc_task *task,
		struct nfs_pgio_header *hdr)
{
	if (!test_and_clear_bit(NFS_IOHDR_STAT, &hdr->flags))
		return;
	nfs4_ff_layout_stat_io_end_write(task,
			FF_LAYOUT_COMP(hdr->lseg, hdr->pgio_mirror_idx),
			hdr->args.count, hdr->res.count,
			hdr->res.verf->committed);
}

static int ff_layout_write_prepare_common(struct rpc_task *task,
					  struct nfs_pgio_header *hdr)
{
	if (unlikely(test_bit(NFS_CONTEXT_BAD, &hdr->args.context->flags))) {
		rpc_exit(task, -EIO);
		return -EIO;
	}

	if (ff_layout_device_unavailable(hdr->lseg, hdr->pgio_mirror_idx)) {
		rpc_exit(task, -EHOSTDOWN);
		return -EAGAIN;
	}

	ff_layout_write_record_layoutstats_start(task, hdr);
	return 0;
}

static void ff_layout_write_prepare_v3(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (ff_layout_write_prepare_common(task, hdr))
		return;

	rpc_call_start(task);
}

static void ff_layout_write_prepare_v4(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (ff_layout_setup_sequence(hdr->ds_clp,
				     &hdr->args.seq_args,
				     &hdr->res.seq_res,
				     task))
		return;

	if (ff_layout_write_prepare_common(task, hdr))
		return;

	if (nfs4_set_rw_stateid(&hdr->args.stateid, hdr->args.context,
			hdr->args.lock_context, FMODE_WRITE) == -EIO)
		rpc_exit(task, -EIO); /* lost lock, terminate I/O */
}

static void ff_layout_write_call_done(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags) &&
	    task->tk_status == 0) {
		nfs4_sequence_done(task, &hdr->res.seq_res);
		return;
	}

	/* Note this may cause RPC to be resent */
	hdr->mds_ops->rpc_call_done(task, hdr);
}

static void ff_layout_write_count_stats(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	ff_layout_write_record_layoutstats_done(task, hdr);
	rpc_count_iostats_metrics(task,
	    &NFS_CLIENT(hdr->inode)->cl_metrics[NFSPROC4_CLNT_WRITE]);
}

static void ff_layout_write_release(void *data)
{
	struct nfs_pgio_header *hdr = data;

	ff_layout_write_record_layoutstats_done(&hdr->task, hdr);
	pnfs_generic_rw_release(data);
}

static void ff_layout_commit_record_layoutstats_start(struct rpc_task *task,
		struct nfs_commit_data *cdata)
{
	if (test_and_set_bit(NFS_IOHDR_STAT, &cdata->flags))
		return;
	nfs4_ff_layout_stat_io_start_write(cdata->inode,
			FF_LAYOUT_COMP(cdata->lseg, cdata->ds_commit_index),
			0, task->tk_start);
}

static void ff_layout_commit_record_layoutstats_done(struct rpc_task *task,
		struct nfs_commit_data *cdata)
{
	struct nfs_page *req;
	__u64 count = 0;

	if (!test_and_clear_bit(NFS_IOHDR_STAT, &cdata->flags))
		return;

	if (task->tk_status == 0) {
		list_for_each_entry(req, &cdata->pages, wb_list)
			count += req->wb_bytes;
	}
	nfs4_ff_layout_stat_io_end_write(task,
			FF_LAYOUT_COMP(cdata->lseg, cdata->ds_commit_index),
			count, count, NFS_FILE_SYNC);
}

static void ff_layout_commit_prepare_common(struct rpc_task *task,
		struct nfs_commit_data *cdata)
{
	ff_layout_commit_record_layoutstats_start(task, cdata);
}

static void ff_layout_commit_prepare_v3(struct rpc_task *task, void *data)
{
	ff_layout_commit_prepare_common(task, data);
	rpc_call_start(task);
}

static void ff_layout_commit_prepare_v4(struct rpc_task *task, void *data)
{
	struct nfs_commit_data *wdata = data;

	if (ff_layout_setup_sequence(wdata->ds_clp,
				 &wdata->args.seq_args,
				 &wdata->res.seq_res,
				 task))
		return;
	ff_layout_commit_prepare_common(task, data);
}

static void ff_layout_commit_done(struct rpc_task *task, void *data)
{
	pnfs_generic_write_commit_done(task, data);
}

static void ff_layout_commit_count_stats(struct rpc_task *task, void *data)
{
	struct nfs_commit_data *cdata = data;

	ff_layout_commit_record_layoutstats_done(task, cdata);
	rpc_count_iostats_metrics(task,
	    &NFS_CLIENT(cdata->inode)->cl_metrics[NFSPROC4_CLNT_COMMIT]);
}

static void ff_layout_commit_release(void *data)
{
	struct nfs_commit_data *cdata = data;

	ff_layout_commit_record_layoutstats_done(&cdata->task, cdata);
	pnfs_generic_commit_release(data);
}

static const struct rpc_call_ops ff_layout_read_call_ops_v3 = {
	.rpc_call_prepare = ff_layout_read_prepare_v3,
	.rpc_call_done = ff_layout_read_call_done,
	.rpc_count_stats = ff_layout_read_count_stats,
	.rpc_release = ff_layout_read_release,
};

static const struct rpc_call_ops ff_layout_read_call_ops_v4 = {
	.rpc_call_prepare = ff_layout_read_prepare_v4,
	.rpc_call_done = ff_layout_read_call_done,
	.rpc_count_stats = ff_layout_read_count_stats,
	.rpc_release = ff_layout_read_release,
};

static const struct rpc_call_ops ff_layout_write_call_ops_v3 = {
	.rpc_call_prepare = ff_layout_write_prepare_v3,
	.rpc_call_done = ff_layout_write_call_done,
	.rpc_count_stats = ff_layout_write_count_stats,
	.rpc_release = ff_layout_write_release,
};

static const struct rpc_call_ops ff_layout_write_call_ops_v4 = {
	.rpc_call_prepare = ff_layout_write_prepare_v4,
	.rpc_call_done = ff_layout_write_call_done,
	.rpc_count_stats = ff_layout_write_count_stats,
	.rpc_release = ff_layout_write_release,
};

static const struct rpc_call_ops ff_layout_commit_call_ops_v3 = {
	.rpc_call_prepare = ff_layout_commit_prepare_v3,
	.rpc_call_done = ff_layout_commit_done,
	.rpc_count_stats = ff_layout_commit_count_stats,
	.rpc_release = ff_layout_commit_release,
};

static const struct rpc_call_ops ff_layout_commit_call_ops_v4 = {
	.rpc_call_prepare = ff_layout_commit_prepare_v4,
	.rpc_call_done = ff_layout_commit_done,
	.rpc_count_stats = ff_layout_commit_count_stats,
	.rpc_release = ff_layout_commit_release,
};

static enum pnfs_try_status
ff_layout_read_pagelist(struct nfs_pgio_header *hdr)
{
	struct pnfs_layout_segment *lseg = hdr->lseg;
	struct nfs4_pnfs_ds *ds;
	struct rpc_clnt *ds_clnt;
	struct rpc_cred *ds_cred;
	loff_t offset = hdr->args.offset;
	u32 idx = hdr->pgio_mirror_idx;
	int vers;
	struct nfs_fh *fh;

	dprintk("--> %s ino %lu pgbase %u req %Zu@%llu\n",
		__func__, hdr->inode->i_ino,
		hdr->args.pgbase, (size_t)hdr->args.count, offset);

	ds = nfs4_ff_layout_prepare_ds(lseg, idx, false);
	if (!ds)
		goto out_failed;

	ds_clnt = nfs4_ff_find_or_create_ds_client(lseg, idx, ds->ds_clp,
						   hdr->inode);
	if (IS_ERR(ds_clnt))
		goto out_failed;

	ds_cred = ff_layout_get_ds_cred(lseg, idx, hdr->cred);
	if (!ds_cred)
		goto out_failed;

	vers = nfs4_ff_layout_ds_version(lseg, idx);

	dprintk("%s USE DS: %s cl_count %d vers %d\n", __func__,
		ds->ds_remotestr, atomic_read(&ds->ds_clp->cl_count), vers);

	hdr->pgio_done_cb = ff_layout_read_done_cb;
	atomic_inc(&ds->ds_clp->cl_count);
	hdr->ds_clp = ds->ds_clp;
	fh = nfs4_ff_layout_select_ds_fh(lseg, idx);
	if (fh)
		hdr->args.fh = fh;
	/*
	 * Note that if we ever decide to split across DSes,
	 * then we may need to handle dense-like offsets.
	 */
	hdr->args.offset = offset;
	hdr->mds_offset = offset;

	/* Perform an asynchronous read to ds */
	nfs_initiate_pgio(ds_clnt, hdr, ds_cred, ds->ds_clp->rpc_ops,
			  vers == 3 ? &ff_layout_read_call_ops_v3 :
				      &ff_layout_read_call_ops_v4,
			  0, RPC_TASK_SOFTCONN);
	put_rpccred(ds_cred);
	return PNFS_ATTEMPTED;

out_failed:
	if (ff_layout_avoid_mds_available_ds(lseg))
		return PNFS_TRY_AGAIN;
	return PNFS_NOT_ATTEMPTED;
}

/* Perform async writes. */
static enum pnfs_try_status
ff_layout_write_pagelist(struct nfs_pgio_header *hdr, int sync)
{
	struct pnfs_layout_segment *lseg = hdr->lseg;
	struct nfs4_pnfs_ds *ds;
	struct rpc_clnt *ds_clnt;
	struct rpc_cred *ds_cred;
	loff_t offset = hdr->args.offset;
	int vers;
	struct nfs_fh *fh;
	int idx = hdr->pgio_mirror_idx;

	ds = nfs4_ff_layout_prepare_ds(lseg, idx, true);
	if (!ds)
		return PNFS_NOT_ATTEMPTED;

	ds_clnt = nfs4_ff_find_or_create_ds_client(lseg, idx, ds->ds_clp,
						   hdr->inode);
	if (IS_ERR(ds_clnt))
		return PNFS_NOT_ATTEMPTED;

	ds_cred = ff_layout_get_ds_cred(lseg, idx, hdr->cred);
	if (!ds_cred)
		return PNFS_NOT_ATTEMPTED;

	vers = nfs4_ff_layout_ds_version(lseg, idx);

	dprintk("%s ino %lu sync %d req %Zu@%llu DS: %s cl_count %d vers %d\n",
		__func__, hdr->inode->i_ino, sync, (size_t) hdr->args.count,
		offset, ds->ds_remotestr, atomic_read(&ds->ds_clp->cl_count),
		vers);

	hdr->pgio_done_cb = ff_layout_write_done_cb;
	atomic_inc(&ds->ds_clp->cl_count);
	hdr->ds_clp = ds->ds_clp;
	hdr->ds_commit_idx = idx;
	fh = nfs4_ff_layout_select_ds_fh(lseg, idx);
	if (fh)
		hdr->args.fh = fh;

	/*
	 * Note that if we ever decide to split across DSes,
	 * then we may need to handle dense-like offsets.
	 */
	hdr->args.offset = offset;

	/* Perform an asynchronous write */
	nfs_initiate_pgio(ds_clnt, hdr, ds_cred, ds->ds_clp->rpc_ops,
			  vers == 3 ? &ff_layout_write_call_ops_v3 :
				      &ff_layout_write_call_ops_v4,
			  sync, RPC_TASK_SOFTCONN);
	put_rpccred(ds_cred);
	return PNFS_ATTEMPTED;
}

static u32 calc_ds_index_from_commit(struct pnfs_layout_segment *lseg, u32 i)
{
	return i;
}

static struct nfs_fh *
select_ds_fh_from_commit(struct pnfs_layout_segment *lseg, u32 i)
{
	struct nfs4_ff_layout_segment *flseg = FF_LAYOUT_LSEG(lseg);

	/* FIXME: Assume that there is only one NFS version available
	 * for the DS.
	 */
	return &flseg->mirror_array[i]->fh_versions[0];
}

static int ff_layout_initiate_commit(struct nfs_commit_data *data, int how)
{
	struct pnfs_layout_segment *lseg = data->lseg;
	struct nfs4_pnfs_ds *ds;
	struct rpc_clnt *ds_clnt;
	struct rpc_cred *ds_cred;
	u32 idx;
	int vers, ret;
	struct nfs_fh *fh;

	idx = calc_ds_index_from_commit(lseg, data->ds_commit_index);
	ds = nfs4_ff_layout_prepare_ds(lseg, idx, true);
	if (!ds)
		goto out_err;

	ds_clnt = nfs4_ff_find_or_create_ds_client(lseg, idx, ds->ds_clp,
						   data->inode);
	if (IS_ERR(ds_clnt))
		goto out_err;

	ds_cred = ff_layout_get_ds_cred(lseg, idx, data->cred);
	if (!ds_cred)
		goto out_err;

	vers = nfs4_ff_layout_ds_version(lseg, idx);

	dprintk("%s ino %lu, how %d cl_count %d vers %d\n", __func__,
		data->inode->i_ino, how, atomic_read(&ds->ds_clp->cl_count),
		vers);
	data->commit_done_cb = ff_layout_commit_done_cb;
	data->cred = ds_cred;
	atomic_inc(&ds->ds_clp->cl_count);
	data->ds_clp = ds->ds_clp;
	fh = select_ds_fh_from_commit(lseg, data->ds_commit_index);
	if (fh)
		data->args.fh = fh;

	ret = nfs_initiate_commit(ds_clnt, data, ds->ds_clp->rpc_ops,
				   vers == 3 ? &ff_layout_commit_call_ops_v3 :
					       &ff_layout_commit_call_ops_v4,
				   how, RPC_TASK_SOFTCONN);
	put_rpccred(ds_cred);
	return ret;
out_err:
	pnfs_generic_prepare_to_resend_writes(data);
	pnfs_generic_commit_release(data);
	return -EAGAIN;
}

static int
ff_layout_commit_pagelist(struct inode *inode, struct list_head *mds_pages,
			   int how, struct nfs_commit_info *cinfo)
{
	return pnfs_generic_commit_pagelist(inode, mds_pages, how, cinfo,
					    ff_layout_initiate_commit);
}

static struct pnfs_ds_commit_info *
ff_layout_get_ds_info(struct inode *inode)
{
	struct pnfs_layout_hdr *layout = NFS_I(inode)->layout;

	if (layout == NULL)
		return NULL;

	return &FF_LAYOUT_FROM_HDR(layout)->commit_info;
}

static void
ff_layout_free_deviceid_node(struct nfs4_deviceid_node *d)
{
	nfs4_ff_layout_free_deviceid(container_of(d, struct nfs4_ff_layout_ds,
						  id_node));
}

static int ff_layout_encode_ioerr(struct nfs4_flexfile_layout *flo,
				  struct xdr_stream *xdr,
				  const struct nfs4_layoutreturn_args *args)
{
	struct pnfs_layout_hdr *hdr = &flo->generic_hdr;
	__be32 *start;
	int count = 0, ret = 0;

	start = xdr_reserve_space(xdr, 4);
	if (unlikely(!start))
		return -E2BIG;

	/* This assume we always return _ALL_ layouts */
	spin_lock(&hdr->plh_inode->i_lock);
	ret = ff_layout_encode_ds_ioerr(flo, xdr, &count, &args->range);
	spin_unlock(&hdr->plh_inode->i_lock);

	*start = cpu_to_be32(count);

	return ret;
}

/* report nothing for now */
static void ff_layout_encode_iostats(struct nfs4_flexfile_layout *flo,
				     struct xdr_stream *xdr,
				     const struct nfs4_layoutreturn_args *args)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	if (likely(p))
		*p = cpu_to_be32(0);
}

static struct nfs4_deviceid_node *
ff_layout_alloc_deviceid_node(struct nfs_server *server,
			      struct pnfs_device *pdev, gfp_t gfp_flags)
{
	struct nfs4_ff_layout_ds *dsaddr;

	dsaddr = nfs4_ff_alloc_deviceid_node(server, pdev, gfp_flags);
	if (!dsaddr)
		return NULL;
	return &dsaddr->id_node;
}

static void
ff_layout_encode_layoutreturn(struct pnfs_layout_hdr *lo,
			      struct xdr_stream *xdr,
			      const struct nfs4_layoutreturn_args *args)
{
	struct nfs4_flexfile_layout *flo = FF_LAYOUT_FROM_HDR(lo);
	__be32 *start;

	dprintk("%s: Begin\n", __func__);
	start = xdr_reserve_space(xdr, 4);
	BUG_ON(!start);

	ff_layout_encode_ioerr(flo, xdr, args);
	ff_layout_encode_iostats(flo, xdr, args);

	*start = cpu_to_be32((xdr->p - start - 1) * 4);
	dprintk("%s: Return\n", __func__);
}

static int
ff_layout_ntop4(const struct sockaddr *sap, char *buf, const size_t buflen)
{
	const struct sockaddr_in *sin = (struct sockaddr_in *)sap;

	return snprintf(buf, buflen, "%pI4", &sin->sin_addr);
}

static size_t
ff_layout_ntop6_noscopeid(const struct sockaddr *sap, char *buf,
			  const int buflen)
{
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;
	const struct in6_addr *addr = &sin6->sin6_addr;

	/*
	 * RFC 4291, Section 2.2.2
	 *
	 * Shorthanded ANY address
	 */
	if (ipv6_addr_any(addr))
		return snprintf(buf, buflen, "::");

	/*
	 * RFC 4291, Section 2.2.2
	 *
	 * Shorthanded loopback address
	 */
	if (ipv6_addr_loopback(addr))
		return snprintf(buf, buflen, "::1");

	/*
	 * RFC 4291, Section 2.2.3
	 *
	 * Special presentation address format for mapped v4
	 * addresses.
	 */
	if (ipv6_addr_v4mapped(addr))
		return snprintf(buf, buflen, "::ffff:%pI4",
					&addr->s6_addr32[3]);

	/*
	 * RFC 4291, Section 2.2.1
	 */
	return snprintf(buf, buflen, "%pI6c", addr);
}

/* Derived from rpc_sockaddr2uaddr */
static void
ff_layout_encode_netaddr(struct xdr_stream *xdr, struct nfs4_pnfs_ds_addr *da)
{
	struct sockaddr *sap = (struct sockaddr *)&da->da_addr;
	char portbuf[RPCBIND_MAXUADDRPLEN];
	char addrbuf[RPCBIND_MAXUADDRLEN];
	char *netid;
	unsigned short port;
	int len, netid_len;
	__be32 *p;

	switch (sap->sa_family) {
	case AF_INET:
		if (ff_layout_ntop4(sap, addrbuf, sizeof(addrbuf)) == 0)
			return;
		port = ntohs(((struct sockaddr_in *)sap)->sin_port);
		netid = "tcp";
		netid_len = 3;
		break;
	case AF_INET6:
		if (ff_layout_ntop6_noscopeid(sap, addrbuf, sizeof(addrbuf)) == 0)
			return;
		port = ntohs(((struct sockaddr_in6 *)sap)->sin6_port);
		netid = "tcp6";
		netid_len = 4;
		break;
	default:
		/* we only support tcp and tcp6 */
		WARN_ON_ONCE(1);
		return;
	}

	snprintf(portbuf, sizeof(portbuf), ".%u.%u", port >> 8, port & 0xff);
	len = strlcat(addrbuf, portbuf, sizeof(addrbuf));

	p = xdr_reserve_space(xdr, 4 + netid_len);
	xdr_encode_opaque(p, netid, netid_len);

	p = xdr_reserve_space(xdr, 4 + len);
	xdr_encode_opaque(p, addrbuf, len);
}

static void
ff_layout_encode_nfstime(struct xdr_stream *xdr,
			 ktime_t t)
{
	struct timespec64 ts;
	__be32 *p;

	p = xdr_reserve_space(xdr, 12);
	ts = ktime_to_timespec64(t);
	p = xdr_encode_hyper(p, ts.tv_sec);
	*p++ = cpu_to_be32(ts.tv_nsec);
}

static void
ff_layout_encode_io_latency(struct xdr_stream *xdr,
			    struct nfs4_ff_io_stat *stat)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 5 * 8);
	p = xdr_encode_hyper(p, stat->ops_requested);
	p = xdr_encode_hyper(p, stat->bytes_requested);
	p = xdr_encode_hyper(p, stat->ops_completed);
	p = xdr_encode_hyper(p, stat->bytes_completed);
	p = xdr_encode_hyper(p, stat->bytes_not_delivered);
	ff_layout_encode_nfstime(xdr, stat->total_busy_time);
	ff_layout_encode_nfstime(xdr, stat->aggregate_completion_time);
}

static void
ff_layout_encode_layoutstats(struct xdr_stream *xdr,
			     struct nfs42_layoutstat_args *args,
			     struct nfs42_layoutstat_devinfo *devinfo)
{
	struct nfs4_ff_layout_mirror *mirror = devinfo->layout_private;
	struct nfs4_pnfs_ds_addr *da;
	struct nfs4_pnfs_ds *ds = mirror->mirror_ds->ds;
	struct nfs_fh *fh = &mirror->fh_versions[0];
	__be32 *p, *start;

	da = list_first_entry(&ds->ds_addrs, struct nfs4_pnfs_ds_addr, da_node);
	dprintk("%s: DS %s: encoding address %s\n",
		__func__, ds->ds_remotestr, da->da_remotestr);
	/* layoutupdate length */
	start = xdr_reserve_space(xdr, 4);
	/* netaddr4 */
	ff_layout_encode_netaddr(xdr, da);
	/* nfs_fh4 */
	p = xdr_reserve_space(xdr, 4 + fh->size);
	xdr_encode_opaque(p, fh->data, fh->size);
	/* ff_io_latency4 read */
	spin_lock(&mirror->lock);
	ff_layout_encode_io_latency(xdr, &mirror->read_stat.io_stat);
	/* ff_io_latency4 write */
	ff_layout_encode_io_latency(xdr, &mirror->write_stat.io_stat);
	spin_unlock(&mirror->lock);
	/* nfstime4 */
	ff_layout_encode_nfstime(xdr, ktime_sub(ktime_get(), mirror->start_time));
	/* bool */
	p = xdr_reserve_space(xdr, 4);
	*p = cpu_to_be32(false);

	*start = cpu_to_be32((xdr->p - start - 1) * 4);
}

static int
ff_layout_mirror_prepare_stats(struct nfs42_layoutstat_args *args,
			       struct pnfs_layout_hdr *lo,
			       int dev_limit)
{
	struct nfs4_flexfile_layout *ff_layout = FF_LAYOUT_FROM_HDR(lo);
	struct nfs4_ff_layout_mirror *mirror;
	struct nfs4_deviceid_node *dev;
	struct nfs42_layoutstat_devinfo *devinfo;
	int i = 0;

	list_for_each_entry(mirror, &ff_layout->mirrors, mirrors) {
		if (i >= dev_limit)
			break;
		if (!mirror->mirror_ds)
			continue;
		/* mirror refcount put in cleanup_layoutstats */
		if (!atomic_inc_not_zero(&mirror->ref))
			continue;
		dev = &mirror->mirror_ds->id_node; 
		devinfo = &args->devinfo[i];
		memcpy(&devinfo->dev_id, &dev->deviceid, NFS4_DEVICEID4_SIZE);
		devinfo->offset = 0;
		devinfo->length = NFS4_MAX_UINT64;
		devinfo->read_count = mirror->read_stat.io_stat.ops_completed;
		devinfo->read_bytes = mirror->read_stat.io_stat.bytes_completed;
		devinfo->write_count = mirror->write_stat.io_stat.ops_completed;
		devinfo->write_bytes = mirror->write_stat.io_stat.bytes_completed;
		devinfo->layout_type = LAYOUT_FLEX_FILES;
		devinfo->layoutstats_encode = ff_layout_encode_layoutstats;
		devinfo->layout_private = mirror;

		i++;
	}
	return i;
}

static int
ff_layout_prepare_layoutstats(struct nfs42_layoutstat_args *args)
{
	struct nfs4_flexfile_layout *ff_layout;
	struct nfs4_ff_layout_mirror *mirror;
	int dev_count = 0;

	spin_lock(&args->inode->i_lock);
	ff_layout = FF_LAYOUT_FROM_HDR(NFS_I(args->inode)->layout);
	list_for_each_entry(mirror, &ff_layout->mirrors, mirrors) {
		if (atomic_read(&mirror->ref) != 0)
			dev_count ++;
	}
	spin_unlock(&args->inode->i_lock);
	/* For now, send at most PNFS_LAYOUTSTATS_MAXDEV statistics */
	if (dev_count > PNFS_LAYOUTSTATS_MAXDEV) {
		dprintk("%s: truncating devinfo to limit (%d:%d)\n",
			__func__, dev_count, PNFS_LAYOUTSTATS_MAXDEV);
		dev_count = PNFS_LAYOUTSTATS_MAXDEV;
	}
	args->devinfo = kmalloc_array(dev_count, sizeof(*args->devinfo), GFP_NOIO);
	if (!args->devinfo)
		return -ENOMEM;

	spin_lock(&args->inode->i_lock);
	args->num_dev = ff_layout_mirror_prepare_stats(args,
			&ff_layout->generic_hdr, dev_count);
	spin_unlock(&args->inode->i_lock);

	return 0;
}

static void
ff_layout_cleanup_layoutstats(struct nfs42_layoutstat_data *data)
{
	struct nfs4_ff_layout_mirror *mirror;
	int i;

	for (i = 0; i < data->args.num_dev; i++) {
		mirror = data->args.devinfo[i].layout_private;
		data->args.devinfo[i].layout_private = NULL;
		ff_layout_put_mirror(mirror);
	}
}

static struct pnfs_layoutdriver_type flexfilelayout_type = {
	.id			= LAYOUT_FLEX_FILES,
	.name			= "LAYOUT_FLEX_FILES",
	.owner			= THIS_MODULE,
	.alloc_layout_hdr	= ff_layout_alloc_layout_hdr,
	.free_layout_hdr	= ff_layout_free_layout_hdr,
	.alloc_lseg		= ff_layout_alloc_lseg,
	.free_lseg		= ff_layout_free_lseg,
	.add_lseg		= ff_layout_add_lseg,
	.pg_read_ops		= &ff_layout_pg_read_ops,
	.pg_write_ops		= &ff_layout_pg_write_ops,
	.get_ds_info		= ff_layout_get_ds_info,
	.free_deviceid_node	= ff_layout_free_deviceid_node,
	.mark_request_commit	= pnfs_layout_mark_request_commit,
	.clear_request_commit	= pnfs_generic_clear_request_commit,
	.scan_commit_lists	= pnfs_generic_scan_commit_lists,
	.recover_commit_reqs	= pnfs_generic_recover_commit_reqs,
	.commit_pagelist	= ff_layout_commit_pagelist,
	.read_pagelist		= ff_layout_read_pagelist,
	.write_pagelist		= ff_layout_write_pagelist,
	.alloc_deviceid_node    = ff_layout_alloc_deviceid_node,
	.encode_layoutreturn    = ff_layout_encode_layoutreturn,
	.sync			= pnfs_nfs_generic_sync,
	.prepare_layoutstats	= ff_layout_prepare_layoutstats,
	.cleanup_layoutstats	= ff_layout_cleanup_layoutstats,
};

static int __init nfs4flexfilelayout_init(void)
{
	printk(KERN_INFO "%s: NFSv4 Flexfile Layout Driver Registering...\n",
	       __func__);
	if (!ff_zero_group) {
		ff_zero_group = groups_alloc(0);
		if (!ff_zero_group)
			return -ENOMEM;
	}
	return pnfs_register_layoutdriver(&flexfilelayout_type);
}

static void __exit nfs4flexfilelayout_exit(void)
{
	printk(KERN_INFO "%s: NFSv4 Flexfile Layout Driver Unregistering...\n",
	       __func__);
	pnfs_unregister_layoutdriver(&flexfilelayout_type);
	if (ff_zero_group) {
		put_group_info(ff_zero_group);
		ff_zero_group = NULL;
	}
}

MODULE_ALIAS("nfs-layouttype4-4");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("The NFSv4 flexfile layout driver");

module_init(nfs4flexfilelayout_init);
module_exit(nfs4flexfilelayout_exit);

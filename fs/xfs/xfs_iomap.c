/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * Copyright (c) 2016 Christoph Hellwig.
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
#include <linux/iomap.h>
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_error.h"
#include "xfs_trans.h"
#include "xfs_trans_space.h"
#include "xfs_iomap.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_quota.h"
#include "xfs_dquot_item.h"
#include "xfs_dquot.h"
#include "xfs_reflink.h"


#define XFS_WRITEIO_ALIGN(mp,off)	(((off) >> mp->m_writeio_log) \
						<< mp->m_writeio_log)

void
xfs_bmbt_to_iomap(
	struct xfs_inode	*ip,
	struct iomap		*iomap,
	struct xfs_bmbt_irec	*imap)
{
	struct xfs_mount	*mp = ip->i_mount;

	if (imap->br_startblock == HOLESTARTBLOCK) {
		iomap->blkno = IOMAP_NULL_BLOCK;
		iomap->type = IOMAP_HOLE;
	} else if (imap->br_startblock == DELAYSTARTBLOCK) {
		iomap->blkno = IOMAP_NULL_BLOCK;
		iomap->type = IOMAP_DELALLOC;
	} else {
		iomap->blkno = xfs_fsb_to_db(ip, imap->br_startblock);
		if (imap->br_state == XFS_EXT_UNWRITTEN)
			iomap->type = IOMAP_UNWRITTEN;
		else
			iomap->type = IOMAP_MAPPED;
	}
	iomap->offset = XFS_FSB_TO_B(mp, imap->br_startoff);
	iomap->length = XFS_FSB_TO_B(mp, imap->br_blockcount);
	iomap->bdev = xfs_find_bdev_for_inode(VFS_I(ip));
}

xfs_extlen_t
xfs_eof_alignment(
	struct xfs_inode	*ip,
	xfs_extlen_t		extsize)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_extlen_t		align = 0;

	if (!XFS_IS_REALTIME_INODE(ip)) {
		/*
		 * Round up the allocation request to a stripe unit
		 * (m_dalign) boundary if the file size is >= stripe unit
		 * size, and we are allocating past the allocation eof.
		 *
		 * If mounted with the "-o swalloc" option the alignment is
		 * increased from the strip unit size to the stripe width.
		 */
		if (mp->m_swidth && (mp->m_flags & XFS_MOUNT_SWALLOC))
			align = mp->m_swidth;
		else if (mp->m_dalign)
			align = mp->m_dalign;

		if (align && XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, align))
			align = 0;
	}

	/*
	 * Always round up the allocation request to an extent boundary
	 * (when file on a real-time subvolume or has di_extsize hint).
	 */
	if (extsize) {
		if (align)
			align = roundup_64(align, extsize);
		else
			align = extsize;
	}

	return align;
}

STATIC int
xfs_iomap_eof_align_last_fsb(
	struct xfs_inode	*ip,
	xfs_extlen_t		extsize,
	xfs_fileoff_t		*last_fsb)
{
	xfs_extlen_t		align = xfs_eof_alignment(ip, extsize);

	if (align) {
		xfs_fileoff_t	new_last_fsb = roundup_64(*last_fsb, align);
		int		eof, error;

		error = xfs_bmap_eof(ip, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error)
			return error;
		if (eof)
			*last_fsb = new_last_fsb;
	}
	return 0;
}

STATIC int
xfs_alert_fsblock_zero(
	xfs_inode_t	*ip,
	xfs_bmbt_irec_t	*imap)
{
	xfs_alert_tag(ip->i_mount, XFS_PTAG_FSBLOCK_ZERO,
			"Access to block zero in inode %llu "
			"start_block: %llx start_off: %llx "
			"blkcnt: %llx extent-state: %x",
		(unsigned long long)ip->i_ino,
		(unsigned long long)imap->br_startblock,
		(unsigned long long)imap->br_startoff,
		(unsigned long long)imap->br_blockcount,
		imap->br_state);
	return -EFSCORRUPTED;
}

int
xfs_iomap_write_direct(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	size_t		count,
	xfs_bmbt_irec_t *imap,
	int		nmaps)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	count_fsb, resaligned;
	xfs_fsblock_t	firstfsb;
	xfs_extlen_t	extsz, temp;
	int		nimaps;
	int		quota_flag;
	int		rt;
	xfs_trans_t	*tp;
	struct xfs_defer_ops dfops;
	uint		qblocks, resblks, resrtextents;
	int		error;
	int		lockmode;
	int		bmapi_flags = XFS_BMAPI_PREALLOC;
	uint		tflags = 0;

	rt = XFS_IS_REALTIME_INODE(ip);
	extsz = xfs_get_extsz_hint(ip);
	lockmode = XFS_ILOCK_SHARED;	/* locked by caller */

	ASSERT(xfs_isilocked(ip, lockmode));

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	if ((offset + count) > XFS_ISIZE(ip)) {
		/*
		 * Assert that the in-core extent list is present since this can
		 * call xfs_iread_extents() and we only have the ilock shared.
		 * This should be safe because the lock was held around a bmapi
		 * call in the caller and we only need it to access the in-core
		 * list.
		 */
		ASSERT(XFS_IFORK_PTR(ip, XFS_DATA_FORK)->if_flags &
								XFS_IFEXTENTS);
		error = xfs_iomap_eof_align_last_fsb(ip, extsz, &last_fsb);
		if (error)
			goto out_unlock;
	} else {
		if (nmaps && (imap->br_startblock == HOLESTARTBLOCK))
			last_fsb = MIN(last_fsb, (xfs_fileoff_t)
					imap->br_blockcount +
					imap->br_startoff);
	}
	count_fsb = last_fsb - offset_fsb;
	ASSERT(count_fsb > 0);

	resaligned = count_fsb;
	if (unlikely(extsz)) {
		if ((temp = do_mod(offset_fsb, extsz)))
			resaligned += temp;
		if ((temp = do_mod(resaligned, extsz)))
			resaligned += extsz - temp;
	}

	if (unlikely(rt)) {
		resrtextents = qblocks = resaligned;
		resrtextents /= mp->m_sb.sb_rextsize;
		resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
		quota_flag = XFS_QMOPT_RES_RTBLKS;
	} else {
		resrtextents = 0;
		resblks = qblocks = XFS_DIOSTRAT_SPACE_RES(mp, resaligned);
		quota_flag = XFS_QMOPT_RES_REGBLKS;
	}

	/*
	 * Drop the shared lock acquired by the caller, attach the dquot if
	 * necessary and move on to transaction setup.
	 */
	xfs_iunlock(ip, lockmode);
	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	/*
	 * For DAX, we do not allocate unwritten extents, but instead we zero
	 * the block before we commit the transaction.  Ideally we'd like to do
	 * this outside the transaction context, but if we commit and then crash
	 * we may not have zeroed the blocks and this will be exposed on
	 * recovery of the allocation. Hence we must zero before commit.
	 *
	 * Further, if we are mapping unwritten extents here, we need to zero
	 * and convert them to written so that we don't need an unwritten extent
	 * callback for DAX. This also means that we need to be able to dip into
	 * the reserve block pool for bmbt block allocation if there is no space
	 * left but we need to do unwritten extent conversion.
	 */
	if (IS_DAX(VFS_I(ip))) {
		bmapi_flags = XFS_BMAPI_CONVERT | XFS_BMAPI_ZERO;
		if (ISUNWRITTEN(imap)) {
			tflags |= XFS_TRANS_RESERVE;
			resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0) << 1;
		}
	}
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, resrtextents,
			tflags, &tp);
	if (error)
		return error;

	lockmode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lockmode);

	error = xfs_trans_reserve_quota_nblks(tp, ip, qblocks, 0, quota_flag);
	if (error)
		goto out_trans_cancel;

	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * From this point onwards we overwrite the imap pointer that the
	 * caller gave to us.
	 */
	xfs_defer_init(&dfops, &firstfsb);
	nimaps = 1;
	error = xfs_bmapi_write(tp, ip, offset_fsb, count_fsb,
				bmapi_flags, &firstfsb, resblks, imap,
				&nimaps, &dfops);
	if (error)
		goto out_bmap_cancel;

	/*
	 * Complete the transaction
	 */
	error = xfs_defer_finish(&tp, &dfops, NULL);
	if (error)
		goto out_bmap_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto out_unlock;

	/*
	 * Copy any maps to caller's array and return any error.
	 */
	if (nimaps == 0) {
		error = -ENOSPC;
		goto out_unlock;
	}

	if (!(imap->br_startblock || XFS_IS_REALTIME_INODE(ip)))
		error = xfs_alert_fsblock_zero(ip, imap);

out_unlock:
	xfs_iunlock(ip, lockmode);
	return error;

out_bmap_cancel:
	xfs_defer_cancel(&dfops);
	xfs_trans_unreserve_quota_nblks(tp, ip, (long)qblocks, 0, quota_flag);
out_trans_cancel:
	xfs_trans_cancel(tp);
	goto out_unlock;
}

STATIC bool
xfs_quota_need_throttle(
	struct xfs_inode *ip,
	int type,
	xfs_fsblock_t alloc_blocks)
{
	struct xfs_dquot *dq = xfs_inode_dquot(ip, type);

	if (!dq || !xfs_this_quota_on(ip->i_mount, type))
		return false;

	/* no hi watermark, no throttle */
	if (!dq->q_prealloc_hi_wmark)
		return false;

	/* under the lo watermark, no throttle */
	if (dq->q_res_bcount + alloc_blocks < dq->q_prealloc_lo_wmark)
		return false;

	return true;
}

STATIC void
xfs_quota_calc_throttle(
	struct xfs_inode *ip,
	int type,
	xfs_fsblock_t *qblocks,
	int *qshift,
	int64_t	*qfreesp)
{
	int64_t freesp;
	int shift = 0;
	struct xfs_dquot *dq = xfs_inode_dquot(ip, type);

	/* no dq, or over hi wmark, squash the prealloc completely */
	if (!dq || dq->q_res_bcount >= dq->q_prealloc_hi_wmark) {
		*qblocks = 0;
		*qfreesp = 0;
		return;
	}

	freesp = dq->q_prealloc_hi_wmark - dq->q_res_bcount;
	if (freesp < dq->q_low_space[XFS_QLOWSP_5_PCNT]) {
		shift = 2;
		if (freesp < dq->q_low_space[XFS_QLOWSP_3_PCNT])
			shift += 2;
		if (freesp < dq->q_low_space[XFS_QLOWSP_1_PCNT])
			shift += 2;
	}

	if (freesp < *qfreesp)
		*qfreesp = freesp;

	/* only overwrite the throttle values if we are more aggressive */
	if ((freesp >> shift) < (*qblocks >> *qshift)) {
		*qblocks = freesp;
		*qshift = shift;
	}
}

/*
 * If we are doing a write at the end of the file and there are no allocations
 * past this one, then extend the allocation out to the file system's write
 * iosize.
 *
 * If we don't have a user specified preallocation size, dynamically increase
 * the preallocation size as the size of the file grows.  Cap the maximum size
 * at a single extent or less if the filesystem is near full. The closer the
 * filesystem is to full, the smaller the maximum prealocation.
 *
 * As an exception we don't do any preallocation at all if the file is smaller
 * than the minimum preallocation and we are using the default dynamic
 * preallocation scheme, as it is likely this is the only write to the file that
 * is going to be done.
 *
 * We clean up any extra space left over when the file is closed in
 * xfs_inactive().
 */
STATIC xfs_fsblock_t
xfs_iomap_prealloc_size(
	struct xfs_inode	*ip,
	loff_t			offset,
	loff_t			count,
	xfs_extnum_t		idx,
	struct xfs_bmbt_irec	*prev)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	int			shift = 0;
	int64_t			freesp;
	xfs_fsblock_t		qblocks;
	int			qshift = 0;
	xfs_fsblock_t		alloc_blocks = 0;

	if (offset + count <= XFS_ISIZE(ip))
		return 0;

	if (!(mp->m_flags & XFS_MOUNT_DFLT_IOSIZE) &&
	    (XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, mp->m_writeio_blocks)))
		return 0;

	/*
	 * If an explicit allocsize is set, the file is small, or we
	 * are writing behind a hole, then use the minimum prealloc:
	 */
	if ((mp->m_flags & XFS_MOUNT_DFLT_IOSIZE) ||
	    XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, mp->m_dalign) ||
	    idx == 0 ||
	    prev->br_startoff + prev->br_blockcount < offset_fsb)
		return mp->m_writeio_blocks;

	/*
	 * Determine the initial size of the preallocation. We are beyond the
	 * current EOF here, but we need to take into account whether this is
	 * a sparse write or an extending write when determining the
	 * preallocation size.  Hence we need to look up the extent that ends
	 * at the current write offset and use the result to determine the
	 * preallocation size.
	 *
	 * If the extent is a hole, then preallocation is essentially disabled.
	 * Otherwise we take the size of the preceding data extent as the basis
	 * for the preallocation size. If the size of the extent is greater than
	 * half the maximum extent length, then use the current offset as the
	 * basis. This ensures that for large files the preallocation size
	 * always extends to MAXEXTLEN rather than falling short due to things
	 * like stripe unit/width alignment of real extents.
	 */
	if (prev->br_blockcount <= (MAXEXTLEN >> 1))
		alloc_blocks = prev->br_blockcount << 1;
	else
		alloc_blocks = XFS_B_TO_FSB(mp, offset);
	if (!alloc_blocks)
		goto check_writeio;
	qblocks = alloc_blocks;

	/*
	 * MAXEXTLEN is not a power of two value but we round the prealloc down
	 * to the nearest power of two value after throttling. To prevent the
	 * round down from unconditionally reducing the maximum supported prealloc
	 * size, we round up first, apply appropriate throttling, round down and
	 * cap the value to MAXEXTLEN.
	 */
	alloc_blocks = XFS_FILEOFF_MIN(roundup_pow_of_two(MAXEXTLEN),
				       alloc_blocks);

	freesp = percpu_counter_read_positive(&mp->m_fdblocks);
	if (freesp < mp->m_low_space[XFS_LOWSP_5_PCNT]) {
		shift = 2;
		if (freesp < mp->m_low_space[XFS_LOWSP_4_PCNT])
			shift++;
		if (freesp < mp->m_low_space[XFS_LOWSP_3_PCNT])
			shift++;
		if (freesp < mp->m_low_space[XFS_LOWSP_2_PCNT])
			shift++;
		if (freesp < mp->m_low_space[XFS_LOWSP_1_PCNT])
			shift++;
	}

	/*
	 * Check each quota to cap the prealloc size, provide a shift value to
	 * throttle with and adjust amount of available space.
	 */
	if (xfs_quota_need_throttle(ip, XFS_DQ_USER, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQ_USER, &qblocks, &qshift,
					&freesp);
	if (xfs_quota_need_throttle(ip, XFS_DQ_GROUP, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQ_GROUP, &qblocks, &qshift,
					&freesp);
	if (xfs_quota_need_throttle(ip, XFS_DQ_PROJ, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQ_PROJ, &qblocks, &qshift,
					&freesp);

	/*
	 * The final prealloc size is set to the minimum of free space available
	 * in each of the quotas and the overall filesystem.
	 *
	 * The shift throttle value is set to the maximum value as determined by
	 * the global low free space values and per-quota low free space values.
	 */
	alloc_blocks = MIN(alloc_blocks, qblocks);
	shift = MAX(shift, qshift);

	if (shift)
		alloc_blocks >>= shift;
	/*
	 * rounddown_pow_of_two() returns an undefined result if we pass in
	 * alloc_blocks = 0.
	 */
	if (alloc_blocks)
		alloc_blocks = rounddown_pow_of_two(alloc_blocks);
	if (alloc_blocks > MAXEXTLEN)
		alloc_blocks = MAXEXTLEN;

	/*
	 * If we are still trying to allocate more space than is
	 * available, squash the prealloc hard. This can happen if we
	 * have a large file on a small filesystem and the above
	 * lowspace thresholds are smaller than MAXEXTLEN.
	 */
	while (alloc_blocks && alloc_blocks >= freesp)
		alloc_blocks >>= 4;
check_writeio:
	if (alloc_blocks < mp->m_writeio_blocks)
		alloc_blocks = mp->m_writeio_blocks;
	trace_xfs_iomap_prealloc_size(ip, alloc_blocks, shift,
				      mp->m_writeio_blocks);
	return alloc_blocks;
}

static int
xfs_file_iomap_begin_delay(
	struct inode		*inode,
	loff_t			offset,
	loff_t			count,
	unsigned		flags,
	struct iomap		*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		maxbytes_fsb =
		XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes);
	xfs_fileoff_t		end_fsb, orig_end_fsb;
	int			error = 0, eof = 0;
	struct xfs_bmbt_irec	got;
	struct xfs_bmbt_irec	prev;
	xfs_extnum_t		idx;

	ASSERT(!XFS_IS_REALTIME_INODE(ip));
	ASSERT(!xfs_get_extsz_hint(ip));

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	if (unlikely(XFS_TEST_ERROR(
	    (XFS_IFORK_FORMAT(ip, XFS_DATA_FORK) != XFS_DINODE_FMT_EXTENTS &&
	     XFS_IFORK_FORMAT(ip, XFS_DATA_FORK) != XFS_DINODE_FMT_BTREE),
	     mp, XFS_ERRTAG_BMAPIFORMAT, XFS_RANDOM_BMAPIFORMAT))) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, mp);
		error = -EFSCORRUPTED;
		goto out_unlock;
	}

	XFS_STATS_INC(mp, xs_blk_mapw);

	if (!(ifp->if_flags & XFS_IFEXTENTS)) {
		error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
		if (error)
			goto out_unlock;
	}

	xfs_bmap_search_extents(ip, offset_fsb, XFS_DATA_FORK, &eof, &idx,
			&got, &prev);
	if (!eof && got.br_startoff <= offset_fsb) {
		if (xfs_is_reflink_inode(ip)) {
			bool		shared;

			end_fsb = min(XFS_B_TO_FSB(mp, offset + count),
					maxbytes_fsb);
			xfs_trim_extent(&got, offset_fsb, end_fsb - offset_fsb);
			error = xfs_reflink_reserve_cow(ip, &got, &shared);
			if (error)
				goto out_unlock;
		}

		trace_xfs_iomap_found(ip, offset, count, 0, &got);
		goto done;
	}

	error = xfs_qm_dqattach_locked(ip, 0);
	if (error)
		goto out_unlock;

	/*
	 * We cap the maximum length we map here to MAX_WRITEBACK_PAGES pages
	 * to keep the chunks of work done where somewhat symmetric with the
	 * work writeback does. This is a completely arbitrary number pulled
	 * out of thin air as a best guess for initial testing.
	 *
	 * Note that the values needs to be less than 32-bits wide until
	 * the lower level functions are updated.
	 */
	count = min_t(loff_t, count, 1024 * PAGE_SIZE);
	end_fsb = orig_end_fsb =
		min(XFS_B_TO_FSB(mp, offset + count), maxbytes_fsb);

	if (eof) {
		xfs_fsblock_t	prealloc_blocks;

		prealloc_blocks =
			xfs_iomap_prealloc_size(ip, offset, count, idx, &prev);
		if (prealloc_blocks) {
			xfs_extlen_t	align;
			xfs_off_t	end_offset;

			end_offset = XFS_WRITEIO_ALIGN(mp, offset + count - 1);
			end_fsb = XFS_B_TO_FSBT(mp, end_offset) +
				prealloc_blocks;

			align = xfs_eof_alignment(ip, 0);
			if (align)
				end_fsb = roundup_64(end_fsb, align);

			end_fsb = min(end_fsb, maxbytes_fsb);
			ASSERT(end_fsb > offset_fsb);
		}
	}

retry:
	error = xfs_bmapi_reserve_delalloc(ip, XFS_DATA_FORK, offset_fsb,
			end_fsb - offset_fsb, &got,
			&prev, &idx, eof);
	switch (error) {
	case 0:
		break;
	case -ENOSPC:
	case -EDQUOT:
		/* retry without any preallocation */
		trace_xfs_delalloc_enospc(ip, offset, count);
		if (end_fsb != orig_end_fsb) {
			end_fsb = orig_end_fsb;
			goto retry;
		}
		/*FALLTHRU*/
	default:
		goto out_unlock;
	}

	/*
	 * Tag the inode as speculatively preallocated so we can reclaim this
	 * space on demand, if necessary.
	 */
	if (end_fsb != orig_end_fsb)
		xfs_inode_set_eofblocks_tag(ip);

	trace_xfs_iomap_alloc(ip, offset, count, 0, &got);
done:
	if (isnullstartblock(got.br_startblock))
		got.br_startblock = DELAYSTARTBLOCK;

	if (!got.br_startblock) {
		error = xfs_alert_fsblock_zero(ip, &got);
		if (error)
			goto out_unlock;
	}

	xfs_bmbt_to_iomap(ip, iomap, &got);

out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Pass in a delayed allocate extent, convert it to real extents;
 * return to the caller the extent we create which maps on top of
 * the originating callers request.
 *
 * Called without a lock on the inode.
 *
 * We no longer bother to look at the incoming map - all we have to
 * guarantee is that whatever we allocate fills the required range.
 */
int
xfs_iomap_write_allocate(
	xfs_inode_t	*ip,
	int		whichfork,
	xfs_off_t	offset,
	xfs_bmbt_irec_t *imap)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_fileoff_t	offset_fsb, last_block;
	xfs_fileoff_t	end_fsb, map_start_fsb;
	xfs_fsblock_t	first_block;
	struct xfs_defer_ops	dfops;
	xfs_filblks_t	count_fsb;
	xfs_trans_t	*tp;
	int		nimaps;
	int		error = 0;
	int		flags = 0;
	int		nres;

	if (whichfork == XFS_COW_FORK)
		flags |= XFS_BMAPI_COWFORK;

	/*
	 * Make sure that the dquots are there.
	 */
	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	count_fsb = imap->br_blockcount;
	map_start_fsb = imap->br_startoff;

	XFS_STATS_ADD(mp, xs_xstrat_bytes, XFS_FSB_TO_B(mp, count_fsb));

	while (count_fsb != 0) {
		/*
		 * Set up a transaction with which to allocate the
		 * backing store for the file.  Do allocations in a
		 * loop until we get some space in the range we are
		 * interested in.  The other space that might be allocated
		 * is in the delayed allocation extent on which we sit
		 * but before our buffer starts.
		 */
		nimaps = 0;
		while (nimaps == 0) {
			nres = XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK);
			/*
			 * We have already reserved space for the extent and any
			 * indirect blocks when creating the delalloc extent,
			 * there is no need to reserve space in this transaction
			 * again.
			 */
			error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, 0,
					0, XFS_TRANS_RESERVE, &tp);
			if (error)
				return error;

			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip, 0);

			xfs_defer_init(&dfops, &first_block);

			/*
			 * it is possible that the extents have changed since
			 * we did the read call as we dropped the ilock for a
			 * while. We have to be careful about truncates or hole
			 * punchs here - we are not allowed to allocate
			 * non-delalloc blocks here.
			 *
			 * The only protection against truncation is the pages
			 * for the range we are being asked to convert are
			 * locked and hence a truncate will block on them
			 * first.
			 *
			 * As a result, if we go beyond the range we really
			 * need and hit an delalloc extent boundary followed by
			 * a hole while we have excess blocks in the map, we
			 * will fill the hole incorrectly and overrun the
			 * transaction reservation.
			 *
			 * Using a single map prevents this as we are forced to
			 * check each map we look for overlap with the desired
			 * range and abort as soon as we find it. Also, given
			 * that we only return a single map, having one beyond
			 * what we can return is probably a bit silly.
			 *
			 * We also need to check that we don't go beyond EOF;
			 * this is a truncate optimisation as a truncate sets
			 * the new file size before block on the pages we
			 * currently have locked under writeback. Because they
			 * are about to be tossed, we don't need to write them
			 * back....
			 */
			nimaps = 1;
			end_fsb = XFS_B_TO_FSB(mp, XFS_ISIZE(ip));
			error = xfs_bmap_last_offset(ip, &last_block,
							XFS_DATA_FORK);
			if (error)
				goto trans_cancel;

			last_block = XFS_FILEOFF_MAX(last_block, end_fsb);
			if ((map_start_fsb + count_fsb) > last_block) {
				count_fsb = last_block - map_start_fsb;
				if (count_fsb == 0) {
					error = -EAGAIN;
					goto trans_cancel;
				}
			}

			/*
			 * From this point onwards we overwrite the imap
			 * pointer that the caller gave to us.
			 */
			error = xfs_bmapi_write(tp, ip, map_start_fsb,
						count_fsb, flags, &first_block,
						nres, imap, &nimaps,
						&dfops);
			if (error)
				goto trans_cancel;

			error = xfs_defer_finish(&tp, &dfops, NULL);
			if (error)
				goto trans_cancel;

			error = xfs_trans_commit(tp);
			if (error)
				goto error0;

			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}

		/*
		 * See if we were able to allocate an extent that
		 * covers at least part of the callers request
		 */
		if (!(imap->br_startblock || XFS_IS_REALTIME_INODE(ip)))
			return xfs_alert_fsblock_zero(ip, imap);

		if ((offset_fsb >= imap->br_startoff) &&
		    (offset_fsb < (imap->br_startoff +
				   imap->br_blockcount))) {
			XFS_STATS_INC(mp, xs_xstrat_quick);
			return 0;
		}

		/*
		 * So far we have not mapped the requested part of the
		 * file, just surrounding data, try again.
		 */
		count_fsb -= imap->br_blockcount;
		map_start_fsb = imap->br_startoff + imap->br_blockcount;
	}

trans_cancel:
	xfs_defer_cancel(&dfops);
	xfs_trans_cancel(tp);
error0:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

int
xfs_iomap_write_unwritten(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	xfs_off_t	count)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_fileoff_t	offset_fsb;
	xfs_filblks_t	count_fsb;
	xfs_filblks_t	numblks_fsb;
	xfs_fsblock_t	firstfsb;
	int		nimaps;
	xfs_trans_t	*tp;
	xfs_bmbt_irec_t imap;
	struct xfs_defer_ops dfops;
	xfs_fsize_t	i_size;
	uint		resblks;
	int		error;

	trace_xfs_unwritten_convert(ip, offset, count);

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	count_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);
	count_fsb = (xfs_filblks_t)(count_fsb - offset_fsb);

	/*
	 * Reserve enough blocks in this transaction for two complete extent
	 * btree splits.  We may be converting the middle part of an unwritten
	 * extent and in this case we will insert two new extents in the btree
	 * each of which could cause a full split.
	 *
	 * This reservation amount will be used in the first call to
	 * xfs_bmbt_split() to select an AG with enough space to satisfy the
	 * rest of the operation.
	 */
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0) << 1;

	do {
		/*
		 * Set up a transaction to convert the range of extents
		 * from unwritten to real. Do allocations in a loop until
		 * we have covered the range passed in.
		 *
		 * Note that we can't risk to recursing back into the filesystem
		 * here as we might be asked to write out the same inode that we
		 * complete here and might deadlock on the iolock.
		 */
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, 0,
				XFS_TRANS_RESERVE | XFS_TRANS_NOFS, &tp);
		if (error)
			return error;

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, 0);

		/*
		 * Modify the unwritten extent state of the buffer.
		 */
		xfs_defer_init(&dfops, &firstfsb);
		nimaps = 1;
		error = xfs_bmapi_write(tp, ip, offset_fsb, count_fsb,
					XFS_BMAPI_CONVERT, &firstfsb, resblks,
					&imap, &nimaps, &dfops);
		if (error)
			goto error_on_bmapi_transaction;

		/*
		 * Log the updated inode size as we go.  We have to be careful
		 * to only log it up to the actual write offset if it is
		 * halfway into a block.
		 */
		i_size = XFS_FSB_TO_B(mp, offset_fsb + count_fsb);
		if (i_size > offset + count)
			i_size = offset + count;

		i_size = xfs_new_eof(ip, i_size);
		if (i_size) {
			ip->i_d.di_size = i_size;
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		}

		error = xfs_defer_finish(&tp, &dfops, NULL);
		if (error)
			goto error_on_bmapi_transaction;

		error = xfs_trans_commit(tp);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error)
			return error;

		if (!(imap.br_startblock || XFS_IS_REALTIME_INODE(ip)))
			return xfs_alert_fsblock_zero(ip, &imap);

		if ((numblks_fsb = imap.br_blockcount) == 0) {
			/*
			 * The numblks_fsb value should always get
			 * smaller, otherwise the loop is stuck.
			 */
			ASSERT(imap.br_blockcount);
			break;
		}
		offset_fsb += numblks_fsb;
		count_fsb -= numblks_fsb;
	} while (count_fsb > 0);

	return 0;

error_on_bmapi_transaction:
	xfs_defer_cancel(&dfops);
	xfs_trans_cancel(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

static inline bool imap_needs_alloc(struct inode *inode,
		struct xfs_bmbt_irec *imap, int nimaps)
{
	return !nimaps ||
		imap->br_startblock == HOLESTARTBLOCK ||
		imap->br_startblock == DELAYSTARTBLOCK ||
		(IS_DAX(inode) && ISUNWRITTEN(imap));
}

static int
xfs_file_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_bmbt_irec	imap;
	xfs_fileoff_t		offset_fsb, end_fsb;
	int			nimaps = 1, error = 0;
	bool			shared = false, trimmed = false;
	unsigned		lockmode;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	if ((flags & IOMAP_WRITE) && !IS_DAX(inode) &&
		   !xfs_get_extsz_hint(ip)) {
		/* Reserve delalloc blocks for regular writeback. */
		return xfs_file_iomap_begin_delay(inode, offset, length, flags,
				iomap);
	}

	/*
	 * COW writes will allocate delalloc space, so we need to make sure
	 * to take the lock exclusively here.
	 */
	if ((flags & (IOMAP_WRITE | IOMAP_ZERO)) && xfs_is_reflink_inode(ip)) {
		lockmode = XFS_ILOCK_EXCL;
		xfs_ilock(ip, XFS_ILOCK_EXCL);
	} else {
		lockmode = xfs_ilock_data_map_shared(ip);
	}

	ASSERT(offset <= mp->m_super->s_maxbytes);
	if ((xfs_fsize_t)offset + length > mp->m_super->s_maxbytes)
		length = mp->m_super->s_maxbytes - offset;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	end_fsb = XFS_B_TO_FSB(mp, offset + length);

	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb, &imap,
			       &nimaps, 0);
	if (error)
		goto out_unlock;

	if (flags & IOMAP_REPORT) {
		/* Trim the mapping to the nearest shared extent boundary. */
		error = xfs_reflink_trim_around_shared(ip, &imap, &shared,
				&trimmed);
		if (error)
			goto out_unlock;
	}

	if ((flags & (IOMAP_WRITE | IOMAP_ZERO)) && xfs_is_reflink_inode(ip)) {
		error = xfs_reflink_reserve_cow(ip, &imap, &shared);
		if (error)
			goto out_unlock;

		end_fsb = imap.br_startoff + imap.br_blockcount;
		length = XFS_FSB_TO_B(mp, end_fsb) - offset;
	}

	if ((flags & IOMAP_WRITE) && imap_needs_alloc(inode, &imap, nimaps)) {
		/*
		 * We cap the maximum length we map here to MAX_WRITEBACK_PAGES
		 * pages to keep the chunks of work done where somewhat symmetric
		 * with the work writeback does. This is a completely arbitrary
		 * number pulled out of thin air as a best guess for initial
		 * testing.
		 *
		 * Note that the values needs to be less than 32-bits wide until
		 * the lower level functions are updated.
		 */
		length = min_t(loff_t, length, 1024 * PAGE_SIZE);
		/*
		 * xfs_iomap_write_direct() expects the shared lock. It
		 * is unlocked on return.
		 */
		if (lockmode == XFS_ILOCK_EXCL)
			xfs_ilock_demote(ip, lockmode);
		error = xfs_iomap_write_direct(ip, offset, length, &imap,
				nimaps);
		if (error)
			return error;

		iomap->flags = IOMAP_F_NEW;
		trace_xfs_iomap_alloc(ip, offset, length, 0, &imap);
	} else {
		ASSERT(nimaps);

		xfs_iunlock(ip, lockmode);
		trace_xfs_iomap_found(ip, offset, length, 0, &imap);
	}

	xfs_bmbt_to_iomap(ip, iomap, &imap);
	if (shared)
		iomap->flags |= IOMAP_F_SHARED;
	return 0;
out_unlock:
	xfs_iunlock(ip, lockmode);
	return error;
}

static int
xfs_file_iomap_end_delalloc(
	struct xfs_inode	*ip,
	loff_t			offset,
	loff_t			length,
	ssize_t			written)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		start_fsb;
	xfs_fileoff_t		end_fsb;
	int			error = 0;

	start_fsb = XFS_B_TO_FSB(mp, offset + written);
	end_fsb = XFS_B_TO_FSB(mp, offset + length);

	/*
	 * Trim back delalloc blocks if we didn't manage to write the whole
	 * range reserved.
	 *
	 * We don't need to care about racing delalloc as we hold i_mutex
	 * across the reserve/allocate/unreserve calls. If there are delalloc
	 * blocks in the range, they are ours.
	 */
	if (start_fsb < end_fsb) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_bmap_punch_delalloc_range(ip, start_fsb,
					       end_fsb - start_fsb);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		if (error && !XFS_FORCED_SHUTDOWN(mp)) {
			xfs_alert(mp, "%s: unable to clean up ino %lld",
				__func__, ip->i_ino);
			return error;
		}
	}

	return 0;
}

static int
xfs_file_iomap_end(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	ssize_t			written,
	unsigned		flags,
	struct iomap		*iomap)
{
	if ((flags & IOMAP_WRITE) && iomap->type == IOMAP_DELALLOC)
		return xfs_file_iomap_end_delalloc(XFS_I(inode), offset,
				length, written);
	return 0;
}

struct iomap_ops xfs_iomap_ops = {
	.iomap_begin		= xfs_file_iomap_begin,
	.iomap_end		= xfs_file_iomap_end,
};

static int
xfs_xattr_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = XFS_B_TO_FSB(mp, offset + length);
	struct xfs_bmbt_irec	imap;
	int			nimaps = 1, error = 0;
	unsigned		lockmode;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	lockmode = xfs_ilock_data_map_shared(ip);

	/* if there are no attribute fork or extents, return ENOENT */
	if (XFS_IFORK_Q(ip) || !ip->i_d.di_anextents) {
		error = -ENOENT;
		goto out_unlock;
	}

	ASSERT(ip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL);
	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb, &imap,
			       &nimaps, XFS_BMAPI_ENTIRE | XFS_BMAPI_ATTRFORK);
out_unlock:
	xfs_iunlock(ip, lockmode);

	if (!error) {
		ASSERT(nimaps);
		xfs_bmbt_to_iomap(ip, iomap, &imap);
	}

	return error;
}

struct iomap_ops xfs_xattr_iomap_ops = {
	.iomap_begin		= xfs_xattr_iomap_begin,
};

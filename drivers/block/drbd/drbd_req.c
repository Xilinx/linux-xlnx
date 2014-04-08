/*
   drbd_req.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2001-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 1999-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2002-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#include <linux/module.h>

#include <linux/slab.h>
#include <linux/drbd.h>
#include "drbd_int.h"
#include "drbd_req.h"


static bool drbd_may_do_local_read(struct drbd_conf *mdev, sector_t sector, int size);

/* Update disk stats at start of I/O request */
static void _drbd_start_io_acct(struct drbd_conf *mdev, struct drbd_request *req)
{
	const int rw = bio_data_dir(req->master_bio);
	int cpu;
	cpu = part_stat_lock();
	part_round_stats(cpu, &mdev->vdisk->part0);
	part_stat_inc(cpu, &mdev->vdisk->part0, ios[rw]);
	part_stat_add(cpu, &mdev->vdisk->part0, sectors[rw], req->i.size >> 9);
	(void) cpu; /* The macro invocations above want the cpu argument, I do not like
		       the compiler warning about cpu only assigned but never used... */
	part_inc_in_flight(&mdev->vdisk->part0, rw);
	part_stat_unlock();
}

/* Update disk stats when completing request upwards */
static void _drbd_end_io_acct(struct drbd_conf *mdev, struct drbd_request *req)
{
	int rw = bio_data_dir(req->master_bio);
	unsigned long duration = jiffies - req->start_time;
	int cpu;
	cpu = part_stat_lock();
	part_stat_add(cpu, &mdev->vdisk->part0, ticks[rw], duration);
	part_round_stats(cpu, &mdev->vdisk->part0);
	part_dec_in_flight(&mdev->vdisk->part0, rw);
	part_stat_unlock();
}

static struct drbd_request *drbd_req_new(struct drbd_conf *mdev,
					       struct bio *bio_src)
{
	struct drbd_request *req;

	req = mempool_alloc(drbd_request_mempool, GFP_NOIO);
	if (!req)
		return NULL;

	drbd_req_make_private_bio(req, bio_src);
	req->rq_state    = bio_data_dir(bio_src) == WRITE ? RQ_WRITE : 0;
	req->w.mdev      = mdev;
	req->master_bio  = bio_src;
	req->epoch       = 0;

	drbd_clear_interval(&req->i);
	req->i.sector     = bio_src->bi_sector;
	req->i.size      = bio_src->bi_size;
	req->i.local = true;
	req->i.waiting = false;

	INIT_LIST_HEAD(&req->tl_requests);
	INIT_LIST_HEAD(&req->w.list);

	/* one reference to be put by __drbd_make_request */
	atomic_set(&req->completion_ref, 1);
	/* one kref as long as completion_ref > 0 */
	kref_init(&req->kref);
	return req;
}

void drbd_req_destroy(struct kref *kref)
{
	struct drbd_request *req = container_of(kref, struct drbd_request, kref);
	struct drbd_conf *mdev = req->w.mdev;
	const unsigned s = req->rq_state;

	if ((req->master_bio && !(s & RQ_POSTPONED)) ||
		atomic_read(&req->completion_ref) ||
		(s & RQ_LOCAL_PENDING) ||
		((s & RQ_NET_MASK) && !(s & RQ_NET_DONE))) {
		dev_err(DEV, "drbd_req_destroy: Logic BUG rq_state = 0x%x, completion_ref = %d\n",
				s, atomic_read(&req->completion_ref));
		return;
	}

	/* remove it from the transfer log.
	 * well, only if it had been there in the first
	 * place... if it had not (local only or conflicting
	 * and never sent), it should still be "empty" as
	 * initialized in drbd_req_new(), so we can list_del() it
	 * here unconditionally */
	list_del_init(&req->tl_requests);

	/* if it was a write, we may have to set the corresponding
	 * bit(s) out-of-sync first. If it had a local part, we need to
	 * release the reference to the activity log. */
	if (s & RQ_WRITE) {
		/* Set out-of-sync unless both OK flags are set
		 * (local only or remote failed).
		 * Other places where we set out-of-sync:
		 * READ with local io-error */

		/* There is a special case:
		 * we may notice late that IO was suspended,
		 * and postpone, or schedule for retry, a write,
		 * before it even was submitted or sent.
		 * In that case we do not want to touch the bitmap at all.
		 */
		if ((s & (RQ_POSTPONED|RQ_LOCAL_MASK|RQ_NET_MASK)) != RQ_POSTPONED) {
			if (!(s & RQ_NET_OK) || !(s & RQ_LOCAL_OK))
				drbd_set_out_of_sync(mdev, req->i.sector, req->i.size);

			if ((s & RQ_NET_OK) && (s & RQ_LOCAL_OK) && (s & RQ_NET_SIS))
				drbd_set_in_sync(mdev, req->i.sector, req->i.size);
		}

		/* one might be tempted to move the drbd_al_complete_io
		 * to the local io completion callback drbd_request_endio.
		 * but, if this was a mirror write, we may only
		 * drbd_al_complete_io after this is RQ_NET_DONE,
		 * otherwise the extent could be dropped from the al
		 * before it has actually been written on the peer.
		 * if we crash before our peer knows about the request,
		 * but after the extent has been dropped from the al,
		 * we would forget to resync the corresponding extent.
		 */
		if (s & RQ_IN_ACT_LOG) {
			if (get_ldev_if_state(mdev, D_FAILED)) {
				drbd_al_complete_io(mdev, &req->i);
				put_ldev(mdev);
			} else if (__ratelimit(&drbd_ratelimit_state)) {
				dev_warn(DEV, "Should have called drbd_al_complete_io(, %llu, %u), "
					 "but my Disk seems to have failed :(\n",
					 (unsigned long long) req->i.sector, req->i.size);
			}
		}
	}

	mempool_free(req, drbd_request_mempool);
}

static void wake_all_senders(struct drbd_tconn *tconn) {
	wake_up(&tconn->sender_work.q_wait);
}

/* must hold resource->req_lock */
void start_new_tl_epoch(struct drbd_tconn *tconn)
{
	/* no point closing an epoch, if it is empty, anyways. */
	if (tconn->current_tle_writes == 0)
		return;

	tconn->current_tle_writes = 0;
	atomic_inc(&tconn->current_tle_nr);
	wake_all_senders(tconn);
}

void complete_master_bio(struct drbd_conf *mdev,
		struct bio_and_error *m)
{
	bio_endio(m->bio, m->error);
	dec_ap_bio(mdev);
}


static void drbd_remove_request_interval(struct rb_root *root,
					 struct drbd_request *req)
{
	struct drbd_conf *mdev = req->w.mdev;
	struct drbd_interval *i = &req->i;

	drbd_remove_interval(root, i);

	/* Wake up any processes waiting for this request to complete.  */
	if (i->waiting)
		wake_up(&mdev->misc_wait);
}

/* Helper for __req_mod().
 * Set m->bio to the master bio, if it is fit to be completed,
 * or leave it alone (it is initialized to NULL in __req_mod),
 * if it has already been completed, or cannot be completed yet.
 * If m->bio is set, the error status to be returned is placed in m->error.
 */
static
void drbd_req_complete(struct drbd_request *req, struct bio_and_error *m)
{
	const unsigned s = req->rq_state;
	struct drbd_conf *mdev = req->w.mdev;
	int rw;
	int error, ok;

	/* we must not complete the master bio, while it is
	 *	still being processed by _drbd_send_zc_bio (drbd_send_dblock)
	 *	not yet acknowledged by the peer
	 *	not yet completed by the local io subsystem
	 * these flags may get cleared in any order by
	 *	the worker,
	 *	the receiver,
	 *	the bio_endio completion callbacks.
	 */
	if ((s & RQ_LOCAL_PENDING && !(s & RQ_LOCAL_ABORTED)) ||
	    (s & RQ_NET_QUEUED) || (s & RQ_NET_PENDING) ||
	    (s & RQ_COMPLETION_SUSP)) {
		dev_err(DEV, "drbd_req_complete: Logic BUG rq_state = 0x%x\n", s);
		return;
	}

	if (!req->master_bio) {
		dev_err(DEV, "drbd_req_complete: Logic BUG, master_bio == NULL!\n");
		return;
	}

	rw = bio_rw(req->master_bio);

	/*
	 * figure out whether to report success or failure.
	 *
	 * report success when at least one of the operations succeeded.
	 * or, to put the other way,
	 * only report failure, when both operations failed.
	 *
	 * what to do about the failures is handled elsewhere.
	 * what we need to do here is just: complete the master_bio.
	 *
	 * local completion error, if any, has been stored as ERR_PTR
	 * in private_bio within drbd_request_endio.
	 */
	ok = (s & RQ_LOCAL_OK) || (s & RQ_NET_OK);
	error = PTR_ERR(req->private_bio);

	/* remove the request from the conflict detection
	 * respective block_id verification hash */
	if (!drbd_interval_empty(&req->i)) {
		struct rb_root *root;

		if (rw == WRITE)
			root = &mdev->write_requests;
		else
			root = &mdev->read_requests;
		drbd_remove_request_interval(root, req);
	}

	/* Before we can signal completion to the upper layers,
	 * we may need to close the current transfer log epoch.
	 * We are within the request lock, so we can simply compare
	 * the request epoch number with the current transfer log
	 * epoch number.  If they match, increase the current_tle_nr,
	 * and reset the transfer log epoch write_cnt.
	 */
	if (rw == WRITE &&
	    req->epoch == atomic_read(&mdev->tconn->current_tle_nr))
		start_new_tl_epoch(mdev->tconn);

	/* Update disk stats */
	_drbd_end_io_acct(mdev, req);

	/* If READ failed,
	 * have it be pushed back to the retry work queue,
	 * so it will re-enter __drbd_make_request(),
	 * and be re-assigned to a suitable local or remote path,
	 * or failed if we do not have access to good data anymore.
	 *
	 * Unless it was failed early by __drbd_make_request(),
	 * because no path was available, in which case
	 * it was not even added to the transfer_log.
	 *
	 * READA may fail, and will not be retried.
	 *
	 * WRITE should have used all available paths already.
	 */
	if (!ok && rw == READ && !list_empty(&req->tl_requests))
		req->rq_state |= RQ_POSTPONED;

	if (!(req->rq_state & RQ_POSTPONED)) {
		m->error = ok ? 0 : (error ?: -EIO);
		m->bio = req->master_bio;
		req->master_bio = NULL;
	}
}

static int drbd_req_put_completion_ref(struct drbd_request *req, struct bio_and_error *m, int put)
{
	struct drbd_conf *mdev = req->w.mdev;
	D_ASSERT(m || (req->rq_state & RQ_POSTPONED));

	if (!atomic_sub_and_test(put, &req->completion_ref))
		return 0;

	drbd_req_complete(req, m);

	if (req->rq_state & RQ_POSTPONED) {
		/* don't destroy the req object just yet,
		 * but queue it for retry */
		drbd_restart_request(req);
		return 0;
	}

	return 1;
}

/* I'd like this to be the only place that manipulates
 * req->completion_ref and req->kref. */
static void mod_rq_state(struct drbd_request *req, struct bio_and_error *m,
		int clear, int set)
{
	struct drbd_conf *mdev = req->w.mdev;
	unsigned s = req->rq_state;
	int c_put = 0;
	int k_put = 0;

	if (drbd_suspended(mdev) && !((s | clear) & RQ_COMPLETION_SUSP))
		set |= RQ_COMPLETION_SUSP;

	/* apply */

	req->rq_state &= ~clear;
	req->rq_state |= set;

	/* no change? */
	if (req->rq_state == s)
		return;

	/* intent: get references */

	if (!(s & RQ_LOCAL_PENDING) && (set & RQ_LOCAL_PENDING))
		atomic_inc(&req->completion_ref);

	if (!(s & RQ_NET_PENDING) && (set & RQ_NET_PENDING)) {
		inc_ap_pending(mdev);
		atomic_inc(&req->completion_ref);
	}

	if (!(s & RQ_NET_QUEUED) && (set & RQ_NET_QUEUED))
		atomic_inc(&req->completion_ref);

	if (!(s & RQ_EXP_BARR_ACK) && (set & RQ_EXP_BARR_ACK))
		kref_get(&req->kref); /* wait for the DONE */

	if (!(s & RQ_NET_SENT) && (set & RQ_NET_SENT))
		atomic_add(req->i.size >> 9, &mdev->ap_in_flight);

	if (!(s & RQ_COMPLETION_SUSP) && (set & RQ_COMPLETION_SUSP))
		atomic_inc(&req->completion_ref);

	/* progress: put references */

	if ((s & RQ_COMPLETION_SUSP) && (clear & RQ_COMPLETION_SUSP))
		++c_put;

	if (!(s & RQ_LOCAL_ABORTED) && (set & RQ_LOCAL_ABORTED)) {
		D_ASSERT(req->rq_state & RQ_LOCAL_PENDING);
		/* local completion may still come in later,
		 * we need to keep the req object around. */
		kref_get(&req->kref);
		++c_put;
	}

	if ((s & RQ_LOCAL_PENDING) && (clear & RQ_LOCAL_PENDING)) {
		if (req->rq_state & RQ_LOCAL_ABORTED)
			++k_put;
		else
			++c_put;
	}

	if ((s & RQ_NET_PENDING) && (clear & RQ_NET_PENDING)) {
		dec_ap_pending(mdev);
		++c_put;
	}

	if ((s & RQ_NET_QUEUED) && (clear & RQ_NET_QUEUED))
		++c_put;

	if ((s & RQ_EXP_BARR_ACK) && !(s & RQ_NET_DONE) && (set & RQ_NET_DONE)) {
		if (req->rq_state & RQ_NET_SENT)
			atomic_sub(req->i.size >> 9, &mdev->ap_in_flight);
		++k_put;
	}

	/* potentially complete and destroy */

	if (k_put || c_put) {
		/* Completion does it's own kref_put.  If we are going to
		 * kref_sub below, we need req to be still around then. */
		int at_least = k_put + !!c_put;
		int refcount = atomic_read(&req->kref.refcount);
		if (refcount < at_least)
			dev_err(DEV,
				"mod_rq_state: Logic BUG: %x -> %x: refcount = %d, should be >= %d\n",
				s, req->rq_state, refcount, at_least);
	}

	/* If we made progress, retry conflicting peer requests, if any. */
	if (req->i.waiting)
		wake_up(&mdev->misc_wait);

	if (c_put)
		k_put += drbd_req_put_completion_ref(req, m, c_put);
	if (k_put)
		kref_sub(&req->kref, k_put, drbd_req_destroy);
}

static void drbd_report_io_error(struct drbd_conf *mdev, struct drbd_request *req)
{
        char b[BDEVNAME_SIZE];

	if (!__ratelimit(&drbd_ratelimit_state))
		return;

	dev_warn(DEV, "local %s IO error sector %llu+%u on %s\n",
			(req->rq_state & RQ_WRITE) ? "WRITE" : "READ",
			(unsigned long long)req->i.sector,
			req->i.size >> 9,
			bdevname(mdev->ldev->backing_bdev, b));
}

/* obviously this could be coded as many single functions
 * instead of one huge switch,
 * or by putting the code directly in the respective locations
 * (as it has been before).
 *
 * but having it this way
 *  enforces that it is all in this one place, where it is easier to audit,
 *  it makes it obvious that whatever "event" "happens" to a request should
 *  happen "atomically" within the req_lock,
 *  and it enforces that we have to think in a very structured manner
 *  about the "events" that may happen to a request during its life time ...
 */
int __req_mod(struct drbd_request *req, enum drbd_req_event what,
		struct bio_and_error *m)
{
	struct drbd_conf *mdev = req->w.mdev;
	struct net_conf *nc;
	int p, rv = 0;

	if (m)
		m->bio = NULL;

	switch (what) {
	default:
		dev_err(DEV, "LOGIC BUG in %s:%u\n", __FILE__ , __LINE__);
		break;

	/* does not happen...
	 * initialization done in drbd_req_new
	case CREATED:
		break;
		*/

	case TO_BE_SENT: /* via network */
		/* reached via __drbd_make_request
		 * and from w_read_retry_remote */
		D_ASSERT(!(req->rq_state & RQ_NET_MASK));
		rcu_read_lock();
		nc = rcu_dereference(mdev->tconn->net_conf);
		p = nc->wire_protocol;
		rcu_read_unlock();
		req->rq_state |=
			p == DRBD_PROT_C ? RQ_EXP_WRITE_ACK :
			p == DRBD_PROT_B ? RQ_EXP_RECEIVE_ACK : 0;
		mod_rq_state(req, m, 0, RQ_NET_PENDING);
		break;

	case TO_BE_SUBMITTED: /* locally */
		/* reached via __drbd_make_request */
		D_ASSERT(!(req->rq_state & RQ_LOCAL_MASK));
		mod_rq_state(req, m, 0, RQ_LOCAL_PENDING);
		break;

	case COMPLETED_OK:
		if (req->rq_state & RQ_WRITE)
			mdev->writ_cnt += req->i.size >> 9;
		else
			mdev->read_cnt += req->i.size >> 9;

		mod_rq_state(req, m, RQ_LOCAL_PENDING,
				RQ_LOCAL_COMPLETED|RQ_LOCAL_OK);
		break;

	case ABORT_DISK_IO:
		mod_rq_state(req, m, 0, RQ_LOCAL_ABORTED);
		break;

	case WRITE_COMPLETED_WITH_ERROR:
		drbd_report_io_error(mdev, req);
		__drbd_chk_io_error(mdev, DRBD_WRITE_ERROR);
		mod_rq_state(req, m, RQ_LOCAL_PENDING, RQ_LOCAL_COMPLETED);
		break;

	case READ_COMPLETED_WITH_ERROR:
		drbd_set_out_of_sync(mdev, req->i.sector, req->i.size);
		drbd_report_io_error(mdev, req);
		__drbd_chk_io_error(mdev, DRBD_READ_ERROR);
		/* fall through. */
	case READ_AHEAD_COMPLETED_WITH_ERROR:
		/* it is legal to fail READA, no __drbd_chk_io_error in that case. */
		mod_rq_state(req, m, RQ_LOCAL_PENDING, RQ_LOCAL_COMPLETED);
		break;

	case QUEUE_FOR_NET_READ:
		/* READ or READA, and
		 * no local disk,
		 * or target area marked as invalid,
		 * or just got an io-error. */
		/* from __drbd_make_request
		 * or from bio_endio during read io-error recovery */

		/* So we can verify the handle in the answer packet.
		 * Corresponding drbd_remove_request_interval is in
		 * drbd_req_complete() */
		D_ASSERT(drbd_interval_empty(&req->i));
		drbd_insert_interval(&mdev->read_requests, &req->i);

		set_bit(UNPLUG_REMOTE, &mdev->flags);

		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		D_ASSERT((req->rq_state & RQ_LOCAL_MASK) == 0);
		mod_rq_state(req, m, 0, RQ_NET_QUEUED);
		req->w.cb = w_send_read_req;
		drbd_queue_work(&mdev->tconn->sender_work, &req->w);
		break;

	case QUEUE_FOR_NET_WRITE:
		/* assert something? */
		/* from __drbd_make_request only */

		/* Corresponding drbd_remove_request_interval is in
		 * drbd_req_complete() */
		D_ASSERT(drbd_interval_empty(&req->i));
		drbd_insert_interval(&mdev->write_requests, &req->i);

		/* NOTE
		 * In case the req ended up on the transfer log before being
		 * queued on the worker, it could lead to this request being
		 * missed during cleanup after connection loss.
		 * So we have to do both operations here,
		 * within the same lock that protects the transfer log.
		 *
		 * _req_add_to_epoch(req); this has to be after the
		 * _maybe_start_new_epoch(req); which happened in
		 * __drbd_make_request, because we now may set the bit
		 * again ourselves to close the current epoch.
		 *
		 * Add req to the (now) current epoch (barrier). */

		/* otherwise we may lose an unplug, which may cause some remote
		 * io-scheduler timeout to expire, increasing maximum latency,
		 * hurting performance. */
		set_bit(UNPLUG_REMOTE, &mdev->flags);

		/* queue work item to send data */
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		mod_rq_state(req, m, 0, RQ_NET_QUEUED|RQ_EXP_BARR_ACK);
		req->w.cb =  w_send_dblock;
		drbd_queue_work(&mdev->tconn->sender_work, &req->w);

		/* close the epoch, in case it outgrew the limit */
		rcu_read_lock();
		nc = rcu_dereference(mdev->tconn->net_conf);
		p = nc->max_epoch_size;
		rcu_read_unlock();
		if (mdev->tconn->current_tle_writes >= p)
			start_new_tl_epoch(mdev->tconn);

		break;

	case QUEUE_FOR_SEND_OOS:
		mod_rq_state(req, m, 0, RQ_NET_QUEUED);
		req->w.cb =  w_send_out_of_sync;
		drbd_queue_work(&mdev->tconn->sender_work, &req->w);
		break;

	case READ_RETRY_REMOTE_CANCELED:
	case SEND_CANCELED:
	case SEND_FAILED:
		/* real cleanup will be done from tl_clear.  just update flags
		 * so it is no longer marked as on the worker queue */
		mod_rq_state(req, m, RQ_NET_QUEUED, 0);
		break;

	case HANDED_OVER_TO_NETWORK:
		/* assert something? */
		if (bio_data_dir(req->master_bio) == WRITE &&
		    !(req->rq_state & (RQ_EXP_RECEIVE_ACK | RQ_EXP_WRITE_ACK))) {
			/* this is what is dangerous about protocol A:
			 * pretend it was successfully written on the peer. */
			if (req->rq_state & RQ_NET_PENDING)
				mod_rq_state(req, m, RQ_NET_PENDING, RQ_NET_OK);
			/* else: neg-ack was faster... */
			/* it is still not yet RQ_NET_DONE until the
			 * corresponding epoch barrier got acked as well,
			 * so we know what to dirty on connection loss */
		}
		mod_rq_state(req, m, RQ_NET_QUEUED, RQ_NET_SENT);
		break;

	case OOS_HANDED_TO_NETWORK:
		/* Was not set PENDING, no longer QUEUED, so is now DONE
		 * as far as this connection is concerned. */
		mod_rq_state(req, m, RQ_NET_QUEUED, RQ_NET_DONE);
		break;

	case CONNECTION_LOST_WHILE_PENDING:
		/* transfer log cleanup after connection loss */
		mod_rq_state(req, m,
				RQ_NET_OK|RQ_NET_PENDING|RQ_COMPLETION_SUSP,
				RQ_NET_DONE);
		break;

	case CONFLICT_RESOLVED:
		/* for superseded conflicting writes of multiple primaries,
		 * there is no need to keep anything in the tl, potential
		 * node crashes are covered by the activity log.
		 *
		 * If this request had been marked as RQ_POSTPONED before,
		 * it will actually not be completed, but "restarted",
		 * resubmitted from the retry worker context. */
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		D_ASSERT(req->rq_state & RQ_EXP_WRITE_ACK);
		mod_rq_state(req, m, RQ_NET_PENDING, RQ_NET_DONE|RQ_NET_OK);
		break;

	case WRITE_ACKED_BY_PEER_AND_SIS:
		req->rq_state |= RQ_NET_SIS;
	case WRITE_ACKED_BY_PEER:
		D_ASSERT(req->rq_state & RQ_EXP_WRITE_ACK);
		/* protocol C; successfully written on peer.
		 * Nothing more to do here.
		 * We want to keep the tl in place for all protocols, to cater
		 * for volatile write-back caches on lower level devices. */

		goto ack_common;
	case RECV_ACKED_BY_PEER:
		D_ASSERT(req->rq_state & RQ_EXP_RECEIVE_ACK);
		/* protocol B; pretends to be successfully written on peer.
		 * see also notes above in HANDED_OVER_TO_NETWORK about
		 * protocol != C */
	ack_common:
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		mod_rq_state(req, m, RQ_NET_PENDING, RQ_NET_OK);
		break;

	case POSTPONE_WRITE:
		D_ASSERT(req->rq_state & RQ_EXP_WRITE_ACK);
		/* If this node has already detected the write conflict, the
		 * worker will be waiting on misc_wait.  Wake it up once this
		 * request has completed locally.
		 */
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		req->rq_state |= RQ_POSTPONED;
		if (req->i.waiting)
			wake_up(&mdev->misc_wait);
		/* Do not clear RQ_NET_PENDING. This request will make further
		 * progress via restart_conflicting_writes() or
		 * fail_postponed_requests(). Hopefully. */
		break;

	case NEG_ACKED:
		mod_rq_state(req, m, RQ_NET_OK|RQ_NET_PENDING, 0);
		break;

	case FAIL_FROZEN_DISK_IO:
		if (!(req->rq_state & RQ_LOCAL_COMPLETED))
			break;
		mod_rq_state(req, m, RQ_COMPLETION_SUSP, 0);
		break;

	case RESTART_FROZEN_DISK_IO:
		if (!(req->rq_state & RQ_LOCAL_COMPLETED))
			break;

		mod_rq_state(req, m,
				RQ_COMPLETION_SUSP|RQ_LOCAL_COMPLETED,
				RQ_LOCAL_PENDING);

		rv = MR_READ;
		if (bio_data_dir(req->master_bio) == WRITE)
			rv = MR_WRITE;

		get_ldev(mdev); /* always succeeds in this call path */
		req->w.cb = w_restart_disk_io;
		drbd_queue_work(&mdev->tconn->sender_work, &req->w);
		break;

	case RESEND:
		/* Simply complete (local only) READs. */
		if (!(req->rq_state & RQ_WRITE) && !req->w.cb) {
			mod_rq_state(req, m, RQ_COMPLETION_SUSP, 0);
			break;
		}

		/* If RQ_NET_OK is already set, we got a P_WRITE_ACK or P_RECV_ACK
		   before the connection loss (B&C only); only P_BARRIER_ACK
		   (or the local completion?) was missing when we suspended.
		   Throwing them out of the TL here by pretending we got a BARRIER_ACK.
		   During connection handshake, we ensure that the peer was not rebooted. */
		if (!(req->rq_state & RQ_NET_OK)) {
			/* FIXME could this possibly be a req->w.cb == w_send_out_of_sync?
			 * in that case we must not set RQ_NET_PENDING. */

			mod_rq_state(req, m, RQ_COMPLETION_SUSP, RQ_NET_QUEUED|RQ_NET_PENDING);
			if (req->w.cb) {
				drbd_queue_work(&mdev->tconn->sender_work, &req->w);
				rv = req->rq_state & RQ_WRITE ? MR_WRITE : MR_READ;
			} /* else: FIXME can this happen? */
			break;
		}
		/* else, fall through to BARRIER_ACKED */

	case BARRIER_ACKED:
		/* barrier ack for READ requests does not make sense */
		if (!(req->rq_state & RQ_WRITE))
			break;

		if (req->rq_state & RQ_NET_PENDING) {
			/* barrier came in before all requests were acked.
			 * this is bad, because if the connection is lost now,
			 * we won't be able to clean them up... */
			dev_err(DEV, "FIXME (BARRIER_ACKED but pending)\n");
		}
		/* Allowed to complete requests, even while suspended.
		 * As this is called for all requests within a matching epoch,
		 * we need to filter, and only set RQ_NET_DONE for those that
		 * have actually been on the wire. */
		mod_rq_state(req, m, RQ_COMPLETION_SUSP,
				(req->rq_state & RQ_NET_MASK) ? RQ_NET_DONE : 0);
		break;

	case DATA_RECEIVED:
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		mod_rq_state(req, m, RQ_NET_PENDING, RQ_NET_OK|RQ_NET_DONE);
		break;

	case QUEUE_AS_DRBD_BARRIER:
		start_new_tl_epoch(mdev->tconn);
		mod_rq_state(req, m, 0, RQ_NET_OK|RQ_NET_DONE);
		break;
	};

	return rv;
}

/* we may do a local read if:
 * - we are consistent (of course),
 * - or we are generally inconsistent,
 *   BUT we are still/already IN SYNC for this area.
 *   since size may be bigger than BM_BLOCK_SIZE,
 *   we may need to check several bits.
 */
static bool drbd_may_do_local_read(struct drbd_conf *mdev, sector_t sector, int size)
{
	unsigned long sbnr, ebnr;
	sector_t esector, nr_sectors;

	if (mdev->state.disk == D_UP_TO_DATE)
		return true;
	if (mdev->state.disk != D_INCONSISTENT)
		return false;
	esector = sector + (size >> 9) - 1;
	nr_sectors = drbd_get_capacity(mdev->this_bdev);
	D_ASSERT(sector  < nr_sectors);
	D_ASSERT(esector < nr_sectors);

	sbnr = BM_SECT_TO_BIT(sector);
	ebnr = BM_SECT_TO_BIT(esector);

	return drbd_bm_count_bits(mdev, sbnr, ebnr) == 0;
}

static bool remote_due_to_read_balancing(struct drbd_conf *mdev, sector_t sector,
		enum drbd_read_balancing rbm)
{
	struct backing_dev_info *bdi;
	int stripe_shift;

	switch (rbm) {
	case RB_CONGESTED_REMOTE:
		bdi = &mdev->ldev->backing_bdev->bd_disk->queue->backing_dev_info;
		return bdi_read_congested(bdi);
	case RB_LEAST_PENDING:
		return atomic_read(&mdev->local_cnt) >
			atomic_read(&mdev->ap_pending_cnt) + atomic_read(&mdev->rs_pending_cnt);
	case RB_32K_STRIPING:  /* stripe_shift = 15 */
	case RB_64K_STRIPING:
	case RB_128K_STRIPING:
	case RB_256K_STRIPING:
	case RB_512K_STRIPING:
	case RB_1M_STRIPING:   /* stripe_shift = 20 */
		stripe_shift = (rbm - RB_32K_STRIPING + 15);
		return (sector >> (stripe_shift - 9)) & 1;
	case RB_ROUND_ROBIN:
		return test_and_change_bit(READ_BALANCE_RR, &mdev->flags);
	case RB_PREFER_REMOTE:
		return true;
	case RB_PREFER_LOCAL:
	default:
		return false;
	}
}

/*
 * complete_conflicting_writes  -  wait for any conflicting write requests
 *
 * The write_requests tree contains all active write requests which we
 * currently know about.  Wait for any requests to complete which conflict with
 * the new one.
 *
 * Only way out: remove the conflicting intervals from the tree.
 */
static void complete_conflicting_writes(struct drbd_request *req)
{
	DEFINE_WAIT(wait);
	struct drbd_conf *mdev = req->w.mdev;
	struct drbd_interval *i;
	sector_t sector = req->i.sector;
	int size = req->i.size;

	i = drbd_find_overlap(&mdev->write_requests, sector, size);
	if (!i)
		return;

	for (;;) {
		prepare_to_wait(&mdev->misc_wait, &wait, TASK_UNINTERRUPTIBLE);
		i = drbd_find_overlap(&mdev->write_requests, sector, size);
		if (!i)
			break;
		/* Indicate to wake up device->misc_wait on progress.  */
		i->waiting = true;
		spin_unlock_irq(&mdev->tconn->req_lock);
		schedule();
		spin_lock_irq(&mdev->tconn->req_lock);
	}
	finish_wait(&mdev->misc_wait, &wait);
}

/* called within req_lock and rcu_read_lock() */
static void maybe_pull_ahead(struct drbd_conf *mdev)
{
	struct drbd_tconn *tconn = mdev->tconn;
	struct net_conf *nc;
	bool congested = false;
	enum drbd_on_congestion on_congestion;

	rcu_read_lock();
	nc = rcu_dereference(tconn->net_conf);
	on_congestion = nc ? nc->on_congestion : OC_BLOCK;
	rcu_read_unlock();
	if (on_congestion == OC_BLOCK ||
	    tconn->agreed_pro_version < 96)
		return;

	/* If I don't even have good local storage, we can not reasonably try
	 * to pull ahead of the peer. We also need the local reference to make
	 * sure mdev->act_log is there.
	 */
	if (!get_ldev_if_state(mdev, D_UP_TO_DATE))
		return;

	if (nc->cong_fill &&
	    atomic_read(&mdev->ap_in_flight) >= nc->cong_fill) {
		dev_info(DEV, "Congestion-fill threshold reached\n");
		congested = true;
	}

	if (mdev->act_log->used >= nc->cong_extents) {
		dev_info(DEV, "Congestion-extents threshold reached\n");
		congested = true;
	}

	if (congested) {
		/* start a new epoch for non-mirrored writes */
		start_new_tl_epoch(mdev->tconn);

		if (on_congestion == OC_PULL_AHEAD)
			_drbd_set_state(_NS(mdev, conn, C_AHEAD), 0, NULL);
		else  /*nc->on_congestion == OC_DISCONNECT */
			_drbd_set_state(_NS(mdev, conn, C_DISCONNECTING), 0, NULL);
	}
	put_ldev(mdev);
}

/* If this returns false, and req->private_bio is still set,
 * this should be submitted locally.
 *
 * If it returns false, but req->private_bio is not set,
 * we do not have access to good data :(
 *
 * Otherwise, this destroys req->private_bio, if any,
 * and returns true.
 */
static bool do_remote_read(struct drbd_request *req)
{
	struct drbd_conf *mdev = req->w.mdev;
	enum drbd_read_balancing rbm;

	if (req->private_bio) {
		if (!drbd_may_do_local_read(mdev,
					req->i.sector, req->i.size)) {
			bio_put(req->private_bio);
			req->private_bio = NULL;
			put_ldev(mdev);
		}
	}

	if (mdev->state.pdsk != D_UP_TO_DATE)
		return false;

	if (req->private_bio == NULL)
		return true;

	/* TODO: improve read balancing decisions, take into account drbd
	 * protocol, pending requests etc. */

	rcu_read_lock();
	rbm = rcu_dereference(mdev->ldev->disk_conf)->read_balancing;
	rcu_read_unlock();

	if (rbm == RB_PREFER_LOCAL && req->private_bio)
		return false; /* submit locally */

	if (remote_due_to_read_balancing(mdev, req->i.sector, rbm)) {
		if (req->private_bio) {
			bio_put(req->private_bio);
			req->private_bio = NULL;
			put_ldev(mdev);
		}
		return true;
	}

	return false;
}

/* returns number of connections (== 1, for drbd 8.4)
 * expected to actually write this data,
 * which does NOT include those that we are L_AHEAD for. */
static int drbd_process_write_request(struct drbd_request *req)
{
	struct drbd_conf *mdev = req->w.mdev;
	int remote, send_oos;

	remote = drbd_should_do_remote(mdev->state);
	send_oos = drbd_should_send_out_of_sync(mdev->state);

	/* Need to replicate writes.  Unless it is an empty flush,
	 * which is better mapped to a DRBD P_BARRIER packet,
	 * also for drbd wire protocol compatibility reasons.
	 * If this was a flush, just start a new epoch.
	 * Unless the current epoch was empty anyways, or we are not currently
	 * replicating, in which case there is no point. */
	if (unlikely(req->i.size == 0)) {
		/* The only size==0 bios we expect are empty flushes. */
		D_ASSERT(req->master_bio->bi_rw & REQ_FLUSH);
		if (remote)
			_req_mod(req, QUEUE_AS_DRBD_BARRIER);
		return remote;
	}

	if (!remote && !send_oos)
		return 0;

	D_ASSERT(!(remote && send_oos));

	if (remote) {
		_req_mod(req, TO_BE_SENT);
		_req_mod(req, QUEUE_FOR_NET_WRITE);
	} else if (drbd_set_out_of_sync(mdev, req->i.sector, req->i.size))
		_req_mod(req, QUEUE_FOR_SEND_OOS);

	return remote;
}

static void
drbd_submit_req_private_bio(struct drbd_request *req)
{
	struct drbd_conf *mdev = req->w.mdev;
	struct bio *bio = req->private_bio;
	const int rw = bio_rw(bio);

	bio->bi_bdev = mdev->ldev->backing_bdev;

	/* State may have changed since we grabbed our reference on the
	 * ->ldev member. Double check, and short-circuit to endio.
	 * In case the last activity log transaction failed to get on
	 * stable storage, and this is a WRITE, we may not even submit
	 * this bio. */
	if (get_ldev(mdev)) {
		if (drbd_insert_fault(mdev,
				      rw == WRITE ? DRBD_FAULT_DT_WR
				    : rw == READ  ? DRBD_FAULT_DT_RD
				    :               DRBD_FAULT_DT_RA))
			bio_endio(bio, -EIO);
		else
			generic_make_request(bio);
		put_ldev(mdev);
	} else
		bio_endio(bio, -EIO);
}

static void drbd_queue_write(struct drbd_conf *mdev, struct drbd_request *req)
{
	spin_lock(&mdev->submit.lock);
	list_add_tail(&req->tl_requests, &mdev->submit.writes);
	spin_unlock(&mdev->submit.lock);
	queue_work(mdev->submit.wq, &mdev->submit.worker);
}

/* returns the new drbd_request pointer, if the caller is expected to
 * drbd_send_and_submit() it (to save latency), or NULL if we queued the
 * request on the submitter thread.
 * Returns ERR_PTR(-ENOMEM) if we cannot allocate a drbd_request.
 */
struct drbd_request *
drbd_request_prepare(struct drbd_conf *mdev, struct bio *bio, unsigned long start_time)
{
	const int rw = bio_data_dir(bio);
	struct drbd_request *req;

	/* allocate outside of all locks; */
	req = drbd_req_new(mdev, bio);
	if (!req) {
		dec_ap_bio(mdev);
		/* only pass the error to the upper layers.
		 * if user cannot handle io errors, that's not our business. */
		dev_err(DEV, "could not kmalloc() req\n");
		bio_endio(bio, -ENOMEM);
		return ERR_PTR(-ENOMEM);
	}
	req->start_time = start_time;

	if (!get_ldev(mdev)) {
		bio_put(req->private_bio);
		req->private_bio = NULL;
	}

	/* Update disk stats */
	_drbd_start_io_acct(mdev, req);

	if (rw == WRITE && req->private_bio && req->i.size
	&& !test_bit(AL_SUSPENDED, &mdev->flags)) {
		if (!drbd_al_begin_io_fastpath(mdev, &req->i)) {
			drbd_queue_write(mdev, req);
			return NULL;
		}
		req->rq_state |= RQ_IN_ACT_LOG;
	}

	return req;
}

static void drbd_send_and_submit(struct drbd_conf *mdev, struct drbd_request *req)
{
	const int rw = bio_rw(req->master_bio);
	struct bio_and_error m = { NULL, };
	bool no_remote = false;

	spin_lock_irq(&mdev->tconn->req_lock);
	if (rw == WRITE) {
		/* This may temporarily give up the req_lock,
		 * but will re-aquire it before it returns here.
		 * Needs to be before the check on drbd_suspended() */
		complete_conflicting_writes(req);
		/* no more giving up req_lock from now on! */

		/* check for congestion, and potentially stop sending
		 * full data updates, but start sending "dirty bits" only. */
		maybe_pull_ahead(mdev);
	}


	if (drbd_suspended(mdev)) {
		/* push back and retry: */
		req->rq_state |= RQ_POSTPONED;
		if (req->private_bio) {
			bio_put(req->private_bio);
			req->private_bio = NULL;
			put_ldev(mdev);
		}
		goto out;
	}

	/* We fail READ/READA early, if we can not serve it.
	 * We must do this before req is registered on any lists.
	 * Otherwise, drbd_req_complete() will queue failed READ for retry. */
	if (rw != WRITE) {
		if (!do_remote_read(req) && !req->private_bio)
			goto nodata;
	}

	/* which transfer log epoch does this belong to? */
	req->epoch = atomic_read(&mdev->tconn->current_tle_nr);

	/* no point in adding empty flushes to the transfer log,
	 * they are mapped to drbd barriers already. */
	if (likely(req->i.size!=0)) {
		if (rw == WRITE)
			mdev->tconn->current_tle_writes++;

		list_add_tail(&req->tl_requests, &mdev->tconn->transfer_log);
	}

	if (rw == WRITE) {
		if (!drbd_process_write_request(req))
			no_remote = true;
	} else {
		/* We either have a private_bio, or we can read from remote.
		 * Otherwise we had done the goto nodata above. */
		if (req->private_bio == NULL) {
			_req_mod(req, TO_BE_SENT);
			_req_mod(req, QUEUE_FOR_NET_READ);
		} else
			no_remote = true;
	}

	if (req->private_bio) {
		/* needs to be marked within the same spinlock */
		_req_mod(req, TO_BE_SUBMITTED);
		/* but we need to give up the spinlock to submit */
		spin_unlock_irq(&mdev->tconn->req_lock);
		drbd_submit_req_private_bio(req);
		spin_lock_irq(&mdev->tconn->req_lock);
	} else if (no_remote) {
nodata:
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "IO ERROR: neither local nor remote data, sector %llu+%u\n",
					(unsigned long long)req->i.sector, req->i.size >> 9);
		/* A write may have been queued for send_oos, however.
		 * So we can not simply free it, we must go through drbd_req_put_completion_ref() */
	}

out:
	if (drbd_req_put_completion_ref(req, &m, 1))
		kref_put(&req->kref, drbd_req_destroy);
	spin_unlock_irq(&mdev->tconn->req_lock);

	if (m.bio)
		complete_master_bio(mdev, &m);
}

void __drbd_make_request(struct drbd_conf *mdev, struct bio *bio, unsigned long start_time)
{
	struct drbd_request *req = drbd_request_prepare(mdev, bio, start_time);
	if (IS_ERR_OR_NULL(req))
		return;
	drbd_send_and_submit(mdev, req);
}

static void submit_fast_path(struct drbd_conf *mdev, struct list_head *incoming)
{
	struct drbd_request *req, *tmp;
	list_for_each_entry_safe(req, tmp, incoming, tl_requests) {
		const int rw = bio_data_dir(req->master_bio);

		if (rw == WRITE /* rw != WRITE should not even end up here! */
		&& req->private_bio && req->i.size
		&& !test_bit(AL_SUSPENDED, &mdev->flags)) {
			if (!drbd_al_begin_io_fastpath(mdev, &req->i))
				continue;

			req->rq_state |= RQ_IN_ACT_LOG;
		}

		list_del_init(&req->tl_requests);
		drbd_send_and_submit(mdev, req);
	}
}

static bool prepare_al_transaction_nonblock(struct drbd_conf *mdev,
					    struct list_head *incoming,
					    struct list_head *pending)
{
	struct drbd_request *req, *tmp;
	int wake = 0;
	int err;

	spin_lock_irq(&mdev->al_lock);
	list_for_each_entry_safe(req, tmp, incoming, tl_requests) {
		err = drbd_al_begin_io_nonblock(mdev, &req->i);
		if (err == -EBUSY)
			wake = 1;
		if (err)
			continue;
		req->rq_state |= RQ_IN_ACT_LOG;
		list_move_tail(&req->tl_requests, pending);
	}
	spin_unlock_irq(&mdev->al_lock);
	if (wake)
		wake_up(&mdev->al_wait);

	return !list_empty(pending);
}

void do_submit(struct work_struct *ws)
{
	struct drbd_conf *mdev = container_of(ws, struct drbd_conf, submit.worker);
	LIST_HEAD(incoming);
	LIST_HEAD(pending);
	struct drbd_request *req, *tmp;

	for (;;) {
		spin_lock(&mdev->submit.lock);
		list_splice_tail_init(&mdev->submit.writes, &incoming);
		spin_unlock(&mdev->submit.lock);

		submit_fast_path(mdev, &incoming);
		if (list_empty(&incoming))
			break;

		wait_event(mdev->al_wait, prepare_al_transaction_nonblock(mdev, &incoming, &pending));
		/* Maybe more was queued, while we prepared the transaction?
		 * Try to stuff them into this transaction as well.
		 * Be strictly non-blocking here, no wait_event, we already
		 * have something to commit.
		 * Stop if we don't make any more progres.
		 */
		for (;;) {
			LIST_HEAD(more_pending);
			LIST_HEAD(more_incoming);
			bool made_progress;

			/* It is ok to look outside the lock,
			 * it's only an optimization anyways */
			if (list_empty(&mdev->submit.writes))
				break;

			spin_lock(&mdev->submit.lock);
			list_splice_tail_init(&mdev->submit.writes, &more_incoming);
			spin_unlock(&mdev->submit.lock);

			if (list_empty(&more_incoming))
				break;

			made_progress = prepare_al_transaction_nonblock(mdev, &more_incoming, &more_pending);

			list_splice_tail_init(&more_pending, &pending);
			list_splice_tail_init(&more_incoming, &incoming);

			if (!made_progress)
				break;
		}
		drbd_al_begin_io_commit(mdev, false);

		list_for_each_entry_safe(req, tmp, &pending, tl_requests) {
			list_del_init(&req->tl_requests);
			drbd_send_and_submit(mdev, req);
		}
	}
}

void drbd_make_request(struct request_queue *q, struct bio *bio)
{
	struct drbd_conf *mdev = (struct drbd_conf *) q->queuedata;
	unsigned long start_time;

	start_time = jiffies;

	/*
	 * what we "blindly" assume:
	 */
	D_ASSERT(IS_ALIGNED(bio->bi_size, 512));

	inc_ap_bio(mdev);
	__drbd_make_request(mdev, bio, start_time);
}

/* This is called by bio_add_page().
 *
 * q->max_hw_sectors and other global limits are already enforced there.
 *
 * We need to call down to our lower level device,
 * in case it has special restrictions.
 *
 * We also may need to enforce configured max-bio-bvecs limits.
 *
 * As long as the BIO is empty we have to allow at least one bvec,
 * regardless of size and offset, so no need to ask lower levels.
 */
int drbd_merge_bvec(struct request_queue *q, struct bvec_merge_data *bvm, struct bio_vec *bvec)
{
	struct drbd_conf *mdev = (struct drbd_conf *) q->queuedata;
	unsigned int bio_size = bvm->bi_size;
	int limit = DRBD_MAX_BIO_SIZE;
	int backing_limit;

	if (bio_size && get_ldev(mdev)) {
		unsigned int max_hw_sectors = queue_max_hw_sectors(q);
		struct request_queue * const b =
			mdev->ldev->backing_bdev->bd_disk->queue;
		if (b->merge_bvec_fn) {
			backing_limit = b->merge_bvec_fn(b, bvm, bvec);
			limit = min(limit, backing_limit);
		}
		put_ldev(mdev);
		if ((limit >> 9) > max_hw_sectors)
			limit = max_hw_sectors << 9;
	}
	return limit;
}

struct drbd_request *find_oldest_request(struct drbd_tconn *tconn)
{
	/* Walk the transfer log,
	 * and find the oldest not yet completed request */
	struct drbd_request *r;
	list_for_each_entry(r, &tconn->transfer_log, tl_requests) {
		if (atomic_read(&r->completion_ref))
			return r;
	}
	return NULL;
}

void request_timer_fn(unsigned long data)
{
	struct drbd_conf *mdev = (struct drbd_conf *) data;
	struct drbd_tconn *tconn = mdev->tconn;
	struct drbd_request *req; /* oldest request */
	struct net_conf *nc;
	unsigned long ent = 0, dt = 0, et, nt; /* effective timeout = ko_count * timeout */
	unsigned long now;

	rcu_read_lock();
	nc = rcu_dereference(tconn->net_conf);
	if (nc && mdev->state.conn >= C_WF_REPORT_PARAMS)
		ent = nc->timeout * HZ/10 * nc->ko_count;

	if (get_ldev(mdev)) { /* implicit state.disk >= D_INCONSISTENT */
		dt = rcu_dereference(mdev->ldev->disk_conf)->disk_timeout * HZ / 10;
		put_ldev(mdev);
	}
	rcu_read_unlock();

	et = min_not_zero(dt, ent);

	if (!et)
		return; /* Recurring timer stopped */

	now = jiffies;

	spin_lock_irq(&tconn->req_lock);
	req = find_oldest_request(tconn);
	if (!req) {
		spin_unlock_irq(&tconn->req_lock);
		mod_timer(&mdev->request_timer, now + et);
		return;
	}

	/* The request is considered timed out, if
	 * - we have some effective timeout from the configuration,
	 *   with above state restrictions applied,
	 * - the oldest request is waiting for a response from the network
	 *   resp. the local disk,
	 * - the oldest request is in fact older than the effective timeout,
	 * - the connection was established (resp. disk was attached)
	 *   for longer than the timeout already.
	 * Note that for 32bit jiffies and very stable connections/disks,
	 * we may have a wrap around, which is catched by
	 *   !time_in_range(now, last_..._jif, last_..._jif + timeout).
	 *
	 * Side effect: once per 32bit wrap-around interval, which means every
	 * ~198 days with 250 HZ, we have a window where the timeout would need
	 * to expire twice (worst case) to become effective. Good enough.
	 */
	if (ent && req->rq_state & RQ_NET_PENDING &&
		 time_after(now, req->start_time + ent) &&
		!time_in_range(now, tconn->last_reconnect_jif, tconn->last_reconnect_jif + ent)) {
		dev_warn(DEV, "Remote failed to finish a request within ko-count * timeout\n");
		_drbd_set_state(_NS(mdev, conn, C_TIMEOUT), CS_VERBOSE | CS_HARD, NULL);
	}
	if (dt && req->rq_state & RQ_LOCAL_PENDING && req->w.mdev == mdev &&
		 time_after(now, req->start_time + dt) &&
		!time_in_range(now, mdev->last_reattach_jif, mdev->last_reattach_jif + dt)) {
		dev_warn(DEV, "Local backing device failed to meet the disk-timeout\n");
		__drbd_chk_io_error(mdev, DRBD_FORCE_DETACH);
	}
	nt = (time_after(now, req->start_time + et) ? now : req->start_time) + et;
	spin_unlock_irq(&tconn->req_lock);
	mod_timer(&mdev->request_timer, nt);
}

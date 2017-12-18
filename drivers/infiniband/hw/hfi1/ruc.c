/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/spinlock.h>

#include "hfi.h"
#include "mad.h"
#include "qp.h"
#include "verbs_txreq.h"
#include "trace.h"

/*
 * Convert the AETH RNR timeout code into the number of microseconds.
 */
const u32 ib_hfi1_rnr_table[32] = {
	655360,	/* 00: 655.36 */
	10,	/* 01:    .01 */
	20,	/* 02     .02 */
	30,	/* 03:    .03 */
	40,	/* 04:    .04 */
	60,	/* 05:    .06 */
	80,	/* 06:    .08 */
	120,	/* 07:    .12 */
	160,	/* 08:    .16 */
	240,	/* 09:    .24 */
	320,	/* 0A:    .32 */
	480,	/* 0B:    .48 */
	640,	/* 0C:    .64 */
	960,	/* 0D:    .96 */
	1280,	/* 0E:   1.28 */
	1920,	/* 0F:   1.92 */
	2560,	/* 10:   2.56 */
	3840,	/* 11:   3.84 */
	5120,	/* 12:   5.12 */
	7680,	/* 13:   7.68 */
	10240,	/* 14:  10.24 */
	15360,	/* 15:  15.36 */
	20480,	/* 16:  20.48 */
	30720,	/* 17:  30.72 */
	40960,	/* 18:  40.96 */
	61440,	/* 19:  61.44 */
	81920,	/* 1A:  81.92 */
	122880,	/* 1B: 122.88 */
	163840,	/* 1C: 163.84 */
	245760,	/* 1D: 245.76 */
	327680,	/* 1E: 327.68 */
	491520	/* 1F: 491.52 */
};

/*
 * Validate a RWQE and fill in the SGE state.
 * Return 1 if OK.
 */
static int init_sge(struct rvt_qp *qp, struct rvt_rwqe *wqe)
{
	int i, j, ret;
	struct ib_wc wc;
	struct rvt_lkey_table *rkt;
	struct rvt_pd *pd;
	struct rvt_sge_state *ss;

	rkt = &to_idev(qp->ibqp.device)->rdi.lkey_table;
	pd = ibpd_to_rvtpd(qp->ibqp.srq ? qp->ibqp.srq->pd : qp->ibqp.pd);
	ss = &qp->r_sge;
	ss->sg_list = qp->r_sg_list;
	qp->r_len = 0;
	for (i = j = 0; i < wqe->num_sge; i++) {
		if (wqe->sg_list[i].length == 0)
			continue;
		/* Check LKEY */
		if (!rvt_lkey_ok(rkt, pd, j ? &ss->sg_list[j - 1] : &ss->sge,
				 &wqe->sg_list[i], IB_ACCESS_LOCAL_WRITE))
			goto bad_lkey;
		qp->r_len += wqe->sg_list[i].length;
		j++;
	}
	ss->num_sge = j;
	ss->total_len = qp->r_len;
	ret = 1;
	goto bail;

bad_lkey:
	while (j) {
		struct rvt_sge *sge = --j ? &ss->sg_list[j - 1] : &ss->sge;

		rvt_put_mr(sge->mr);
	}
	ss->num_sge = 0;
	memset(&wc, 0, sizeof(wc));
	wc.wr_id = wqe->wr_id;
	wc.status = IB_WC_LOC_PROT_ERR;
	wc.opcode = IB_WC_RECV;
	wc.qp = &qp->ibqp;
	/* Signal solicited completion event. */
	rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc, 1);
	ret = 0;
bail:
	return ret;
}

/**
 * hfi1_rvt_get_rwqe - copy the next RWQE into the QP's RWQE
 * @qp: the QP
 * @wr_id_only: update qp->r_wr_id only, not qp->r_sge
 *
 * Return -1 if there is a local error, 0 if no RWQE is available,
 * otherwise return 1.
 *
 * Can be called from interrupt level.
 */
int hfi1_rvt_get_rwqe(struct rvt_qp *qp, int wr_id_only)
{
	unsigned long flags;
	struct rvt_rq *rq;
	struct rvt_rwq *wq;
	struct rvt_srq *srq;
	struct rvt_rwqe *wqe;
	void (*handler)(struct ib_event *, void *);
	u32 tail;
	int ret;

	if (qp->ibqp.srq) {
		srq = ibsrq_to_rvtsrq(qp->ibqp.srq);
		handler = srq->ibsrq.event_handler;
		rq = &srq->rq;
	} else {
		srq = NULL;
		handler = NULL;
		rq = &qp->r_rq;
	}

	spin_lock_irqsave(&rq->lock, flags);
	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK)) {
		ret = 0;
		goto unlock;
	}

	wq = rq->wq;
	tail = wq->tail;
	/* Validate tail before using it since it is user writable. */
	if (tail >= rq->size)
		tail = 0;
	if (unlikely(tail == wq->head)) {
		ret = 0;
		goto unlock;
	}
	/* Make sure entry is read after head index is read. */
	smp_rmb();
	wqe = rvt_get_rwqe_ptr(rq, tail);
	/*
	 * Even though we update the tail index in memory, the verbs
	 * consumer is not supposed to post more entries until a
	 * completion is generated.
	 */
	if (++tail >= rq->size)
		tail = 0;
	wq->tail = tail;
	if (!wr_id_only && !init_sge(qp, wqe)) {
		ret = -1;
		goto unlock;
	}
	qp->r_wr_id = wqe->wr_id;

	ret = 1;
	set_bit(RVT_R_WRID_VALID, &qp->r_aflags);
	if (handler) {
		u32 n;

		/*
		 * Validate head pointer value and compute
		 * the number of remaining WQEs.
		 */
		n = wq->head;
		if (n >= rq->size)
			n = 0;
		if (n < tail)
			n += rq->size - tail;
		else
			n -= tail;
		if (n < srq->limit) {
			struct ib_event ev;

			srq->limit = 0;
			spin_unlock_irqrestore(&rq->lock, flags);
			ev.device = qp->ibqp.device;
			ev.element.srq = qp->ibqp.srq;
			ev.event = IB_EVENT_SRQ_LIMIT_REACHED;
			handler(&ev, srq->ibsrq.srq_context);
			goto bail;
		}
	}
unlock:
	spin_unlock_irqrestore(&rq->lock, flags);
bail:
	return ret;
}

static __be64 get_sguid(struct hfi1_ibport *ibp, unsigned index)
{
	if (!index) {
		struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);

		return cpu_to_be64(ppd->guid);
	}
	return ibp->guids[index - 1];
}

static int gid_ok(union ib_gid *gid, __be64 gid_prefix, __be64 id)
{
	return (gid->global.interface_id == id &&
		(gid->global.subnet_prefix == gid_prefix ||
		 gid->global.subnet_prefix == IB_DEFAULT_GID_PREFIX));
}

/*
 *
 * This should be called with the QP r_lock held.
 *
 * The s_lock will be acquired around the hfi1_migrate_qp() call.
 */
int hfi1_ruc_check_hdr(struct hfi1_ibport *ibp, struct ib_header *hdr,
		       int has_grh, struct rvt_qp *qp, u32 bth0)
{
	__be64 guid;
	unsigned long flags;
	u8 sc5 = ibp->sl_to_sc[qp->remote_ah_attr.sl];

	if (qp->s_mig_state == IB_MIG_ARMED && (bth0 & IB_BTH_MIG_REQ)) {
		if (!has_grh) {
			if (qp->alt_ah_attr.ah_flags & IB_AH_GRH)
				goto err;
		} else {
			if (!(qp->alt_ah_attr.ah_flags & IB_AH_GRH))
				goto err;
			guid = get_sguid(ibp, qp->alt_ah_attr.grh.sgid_index);
			if (!gid_ok(&hdr->u.l.grh.dgid, ibp->rvp.gid_prefix,
				    guid))
				goto err;
			if (!gid_ok(
				&hdr->u.l.grh.sgid,
				qp->alt_ah_attr.grh.dgid.global.subnet_prefix,
				qp->alt_ah_attr.grh.dgid.global.interface_id))
				goto err;
		}
		if (unlikely(rcv_pkey_check(ppd_from_ibp(ibp), (u16)bth0,
					    sc5, be16_to_cpu(hdr->lrh[3])))) {
			hfi1_bad_pqkey(ibp, OPA_TRAP_BAD_P_KEY,
				       (u16)bth0,
				       (be16_to_cpu(hdr->lrh[0]) >> 4) & 0xF,
				       0, qp->ibqp.qp_num,
				       be16_to_cpu(hdr->lrh[3]),
				       be16_to_cpu(hdr->lrh[1]));
			goto err;
		}
		/* Validate the SLID. See Ch. 9.6.1.5 and 17.2.8 */
		if (be16_to_cpu(hdr->lrh[3]) != qp->alt_ah_attr.dlid ||
		    ppd_from_ibp(ibp)->port != qp->alt_ah_attr.port_num)
			goto err;
		spin_lock_irqsave(&qp->s_lock, flags);
		hfi1_migrate_qp(qp);
		spin_unlock_irqrestore(&qp->s_lock, flags);
	} else {
		if (!has_grh) {
			if (qp->remote_ah_attr.ah_flags & IB_AH_GRH)
				goto err;
		} else {
			if (!(qp->remote_ah_attr.ah_flags & IB_AH_GRH))
				goto err;
			guid = get_sguid(ibp,
					 qp->remote_ah_attr.grh.sgid_index);
			if (!gid_ok(&hdr->u.l.grh.dgid, ibp->rvp.gid_prefix,
				    guid))
				goto err;
			if (!gid_ok(
			     &hdr->u.l.grh.sgid,
			     qp->remote_ah_attr.grh.dgid.global.subnet_prefix,
			     qp->remote_ah_attr.grh.dgid.global.interface_id))
				goto err;
		}
		if (unlikely(rcv_pkey_check(ppd_from_ibp(ibp), (u16)bth0,
					    sc5, be16_to_cpu(hdr->lrh[3])))) {
			hfi1_bad_pqkey(ibp, OPA_TRAP_BAD_P_KEY,
				       (u16)bth0,
				       (be16_to_cpu(hdr->lrh[0]) >> 4) & 0xF,
				       0, qp->ibqp.qp_num,
				       be16_to_cpu(hdr->lrh[3]),
				       be16_to_cpu(hdr->lrh[1]));
			goto err;
		}
		/* Validate the SLID. See Ch. 9.6.1.5 */
		if (be16_to_cpu(hdr->lrh[3]) != qp->remote_ah_attr.dlid ||
		    ppd_from_ibp(ibp)->port != qp->port_num)
			goto err;
		if (qp->s_mig_state == IB_MIG_REARM &&
		    !(bth0 & IB_BTH_MIG_REQ))
			qp->s_mig_state = IB_MIG_ARMED;
	}

	return 0;

err:
	return 1;
}

/**
 * ruc_loopback - handle UC and RC loopback requests
 * @sqp: the sending QP
 *
 * This is called from hfi1_do_send() to
 * forward a WQE addressed to the same HFI.
 * Note that although we are single threaded due to the send engine, we still
 * have to protect against post_send().  We don't have to worry about
 * receive interrupts since this is a connected protocol and all packets
 * will pass through here.
 */
static void ruc_loopback(struct rvt_qp *sqp)
{
	struct hfi1_ibport *ibp = to_iport(sqp->ibqp.device, sqp->port_num);
	struct rvt_qp *qp;
	struct rvt_swqe *wqe;
	struct rvt_sge *sge;
	unsigned long flags;
	struct ib_wc wc;
	u64 sdata;
	atomic64_t *maddr;
	enum ib_wc_status send_status;
	int release;
	int ret;
	int copy_last = 0;
	u32 to;
	int local_ops = 0;

	rcu_read_lock();

	/*
	 * Note that we check the responder QP state after
	 * checking the requester's state.
	 */
	qp = rvt_lookup_qpn(ib_to_rvt(sqp->ibqp.device), &ibp->rvp,
			    sqp->remote_qpn);

	spin_lock_irqsave(&sqp->s_lock, flags);

	/* Return if we are already busy processing a work request. */
	if ((sqp->s_flags & (RVT_S_BUSY | RVT_S_ANY_WAIT)) ||
	    !(ib_rvt_state_ops[sqp->state] & RVT_PROCESS_OR_FLUSH_SEND))
		goto unlock;

	sqp->s_flags |= RVT_S_BUSY;

again:
	smp_read_barrier_depends(); /* see post_one_send() */
	if (sqp->s_last == ACCESS_ONCE(sqp->s_head))
		goto clr_busy;
	wqe = rvt_get_swqe_ptr(sqp, sqp->s_last);

	/* Return if it is not OK to start a new work request. */
	if (!(ib_rvt_state_ops[sqp->state] & RVT_PROCESS_NEXT_SEND_OK)) {
		if (!(ib_rvt_state_ops[sqp->state] & RVT_FLUSH_SEND))
			goto clr_busy;
		/* We are in the error state, flush the work request. */
		send_status = IB_WC_WR_FLUSH_ERR;
		goto flush_send;
	}

	/*
	 * We can rely on the entry not changing without the s_lock
	 * being held until we update s_last.
	 * We increment s_cur to indicate s_last is in progress.
	 */
	if (sqp->s_last == sqp->s_cur) {
		if (++sqp->s_cur >= sqp->s_size)
			sqp->s_cur = 0;
	}
	spin_unlock_irqrestore(&sqp->s_lock, flags);

	if (!qp || !(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) ||
	    qp->ibqp.qp_type != sqp->ibqp.qp_type) {
		ibp->rvp.n_pkt_drops++;
		/*
		 * For RC, the requester would timeout and retry so
		 * shortcut the timeouts and just signal too many retries.
		 */
		if (sqp->ibqp.qp_type == IB_QPT_RC)
			send_status = IB_WC_RETRY_EXC_ERR;
		else
			send_status = IB_WC_SUCCESS;
		goto serr;
	}

	memset(&wc, 0, sizeof(wc));
	send_status = IB_WC_SUCCESS;

	release = 1;
	sqp->s_sge.sge = wqe->sg_list[0];
	sqp->s_sge.sg_list = wqe->sg_list + 1;
	sqp->s_sge.num_sge = wqe->wr.num_sge;
	sqp->s_len = wqe->length;
	switch (wqe->wr.opcode) {
	case IB_WR_REG_MR:
		goto send_comp;

	case IB_WR_LOCAL_INV:
		if (!(wqe->wr.send_flags & RVT_SEND_COMPLETION_ONLY)) {
			if (rvt_invalidate_rkey(sqp,
						wqe->wr.ex.invalidate_rkey))
				send_status = IB_WC_LOC_PROT_ERR;
			local_ops = 1;
		}
		goto send_comp;

	case IB_WR_SEND_WITH_INV:
		if (!rvt_invalidate_rkey(qp, wqe->wr.ex.invalidate_rkey)) {
			wc.wc_flags = IB_WC_WITH_INVALIDATE;
			wc.ex.invalidate_rkey = wqe->wr.ex.invalidate_rkey;
		}
		goto send;

	case IB_WR_SEND_WITH_IMM:
		wc.wc_flags = IB_WC_WITH_IMM;
		wc.ex.imm_data = wqe->wr.ex.imm_data;
		/* FALLTHROUGH */
	case IB_WR_SEND:
send:
		ret = hfi1_rvt_get_rwqe(qp, 0);
		if (ret < 0)
			goto op_err;
		if (!ret)
			goto rnr_nak;
		break;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_WRITE)))
			goto inv_err;
		wc.wc_flags = IB_WC_WITH_IMM;
		wc.ex.imm_data = wqe->wr.ex.imm_data;
		ret = hfi1_rvt_get_rwqe(qp, 1);
		if (ret < 0)
			goto op_err;
		if (!ret)
			goto rnr_nak;
		/* skip copy_last set and qp_access_flags recheck */
		goto do_write;
	case IB_WR_RDMA_WRITE:
		copy_last = ibpd_to_rvtpd(qp->ibqp.pd)->user;
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_WRITE)))
			goto inv_err;
do_write:
		if (wqe->length == 0)
			break;
		if (unlikely(!rvt_rkey_ok(qp, &qp->r_sge.sge, wqe->length,
					  wqe->rdma_wr.remote_addr,
					  wqe->rdma_wr.rkey,
					  IB_ACCESS_REMOTE_WRITE)))
			goto acc_err;
		qp->r_sge.sg_list = NULL;
		qp->r_sge.num_sge = 1;
		qp->r_sge.total_len = wqe->length;
		break;

	case IB_WR_RDMA_READ:
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_READ)))
			goto inv_err;
		if (unlikely(!rvt_rkey_ok(qp, &sqp->s_sge.sge, wqe->length,
					  wqe->rdma_wr.remote_addr,
					  wqe->rdma_wr.rkey,
					  IB_ACCESS_REMOTE_READ)))
			goto acc_err;
		release = 0;
		sqp->s_sge.sg_list = NULL;
		sqp->s_sge.num_sge = 1;
		qp->r_sge.sge = wqe->sg_list[0];
		qp->r_sge.sg_list = wqe->sg_list + 1;
		qp->r_sge.num_sge = wqe->wr.num_sge;
		qp->r_sge.total_len = wqe->length;
		break;

	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_ATOMIC)))
			goto inv_err;
		if (unlikely(!rvt_rkey_ok(qp, &qp->r_sge.sge, sizeof(u64),
					  wqe->atomic_wr.remote_addr,
					  wqe->atomic_wr.rkey,
					  IB_ACCESS_REMOTE_ATOMIC)))
			goto acc_err;
		/* Perform atomic OP and save result. */
		maddr = (atomic64_t *)qp->r_sge.sge.vaddr;
		sdata = wqe->atomic_wr.compare_add;
		*(u64 *)sqp->s_sge.sge.vaddr =
			(wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD) ?
			(u64)atomic64_add_return(sdata, maddr) - sdata :
			(u64)cmpxchg((u64 *)qp->r_sge.sge.vaddr,
				      sdata, wqe->atomic_wr.swap);
		rvt_put_mr(qp->r_sge.sge.mr);
		qp->r_sge.num_sge = 0;
		goto send_comp;

	default:
		send_status = IB_WC_LOC_QP_OP_ERR;
		goto serr;
	}

	sge = &sqp->s_sge.sge;
	while (sqp->s_len) {
		u32 len = sqp->s_len;

		if (len > sge->length)
			len = sge->length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		WARN_ON_ONCE(len == 0);
		hfi1_copy_sge(&qp->r_sge, sge->vaddr, len, release, copy_last);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (!release)
				rvt_put_mr(sge->mr);
			if (--sqp->s_sge.num_sge)
				*sge = *sqp->s_sge.sg_list++;
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= RVT_SEGSZ) {
				if (++sge->m >= sge->mr->mapsz)
					break;
				sge->n = 0;
			}
			sge->vaddr =
				sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length =
				sge->mr->map[sge->m]->segs[sge->n].length;
		}
		sqp->s_len -= len;
	}
	if (release)
		rvt_put_ss(&qp->r_sge);

	if (!test_and_clear_bit(RVT_R_WRID_VALID, &qp->r_aflags))
		goto send_comp;

	if (wqe->wr.opcode == IB_WR_RDMA_WRITE_WITH_IMM)
		wc.opcode = IB_WC_RECV_RDMA_WITH_IMM;
	else
		wc.opcode = IB_WC_RECV;
	wc.wr_id = qp->r_wr_id;
	wc.status = IB_WC_SUCCESS;
	wc.byte_len = wqe->length;
	wc.qp = &qp->ibqp;
	wc.src_qp = qp->remote_qpn;
	wc.slid = qp->remote_ah_attr.dlid;
	wc.sl = qp->remote_ah_attr.sl;
	wc.port_num = 1;
	/* Signal completion event if the solicited bit is set. */
	rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc,
		     wqe->wr.send_flags & IB_SEND_SOLICITED);

send_comp:
	spin_lock_irqsave(&sqp->s_lock, flags);
	ibp->rvp.n_loop_pkts++;
flush_send:
	sqp->s_rnr_retry = sqp->s_rnr_retry_cnt;
	hfi1_send_complete(sqp, wqe, send_status);
	if (local_ops) {
		atomic_dec(&sqp->local_ops_pending);
		local_ops = 0;
	}
	goto again;

rnr_nak:
	/* Handle RNR NAK */
	if (qp->ibqp.qp_type == IB_QPT_UC)
		goto send_comp;
	ibp->rvp.n_rnr_naks++;
	/*
	 * Note: we don't need the s_lock held since the BUSY flag
	 * makes this single threaded.
	 */
	if (sqp->s_rnr_retry == 0) {
		send_status = IB_WC_RNR_RETRY_EXC_ERR;
		goto serr;
	}
	if (sqp->s_rnr_retry_cnt < 7)
		sqp->s_rnr_retry--;
	spin_lock_irqsave(&sqp->s_lock, flags);
	if (!(ib_rvt_state_ops[sqp->state] & RVT_PROCESS_RECV_OK))
		goto clr_busy;
	to = ib_hfi1_rnr_table[qp->r_min_rnr_timer];
	hfi1_add_rnr_timer(sqp, to);
	goto clr_busy;

op_err:
	send_status = IB_WC_REM_OP_ERR;
	wc.status = IB_WC_LOC_QP_OP_ERR;
	goto err;

inv_err:
	send_status = IB_WC_REM_INV_REQ_ERR;
	wc.status = IB_WC_LOC_QP_OP_ERR;
	goto err;

acc_err:
	send_status = IB_WC_REM_ACCESS_ERR;
	wc.status = IB_WC_LOC_PROT_ERR;
err:
	/* responder goes to error state */
	hfi1_rc_error(qp, wc.status);

serr:
	spin_lock_irqsave(&sqp->s_lock, flags);
	hfi1_send_complete(sqp, wqe, send_status);
	if (sqp->ibqp.qp_type == IB_QPT_RC) {
		int lastwqe = rvt_error_qp(sqp, IB_WC_WR_FLUSH_ERR);

		sqp->s_flags &= ~RVT_S_BUSY;
		spin_unlock_irqrestore(&sqp->s_lock, flags);
		if (lastwqe) {
			struct ib_event ev;

			ev.device = sqp->ibqp.device;
			ev.element.qp = &sqp->ibqp;
			ev.event = IB_EVENT_QP_LAST_WQE_REACHED;
			sqp->ibqp.event_handler(&ev, sqp->ibqp.qp_context);
		}
		goto done;
	}
clr_busy:
	sqp->s_flags &= ~RVT_S_BUSY;
unlock:
	spin_unlock_irqrestore(&sqp->s_lock, flags);
done:
	rcu_read_unlock();
}

/**
 * hfi1_make_grh - construct a GRH header
 * @ibp: a pointer to the IB port
 * @hdr: a pointer to the GRH header being constructed
 * @grh: the global route address to send to
 * @hwords: the number of 32 bit words of header being sent
 * @nwords: the number of 32 bit words of data being sent
 *
 * Return the size of the header in 32 bit words.
 */
u32 hfi1_make_grh(struct hfi1_ibport *ibp, struct ib_grh *hdr,
		  struct ib_global_route *grh, u32 hwords, u32 nwords)
{
	hdr->version_tclass_flow =
		cpu_to_be32((IB_GRH_VERSION << IB_GRH_VERSION_SHIFT) |
			    (grh->traffic_class << IB_GRH_TCLASS_SHIFT) |
			    (grh->flow_label << IB_GRH_FLOW_SHIFT));
	hdr->paylen = cpu_to_be16((hwords - 2 + nwords + SIZE_OF_CRC) << 2);
	/* next_hdr is defined by C8-7 in ch. 8.4.1 */
	hdr->next_hdr = IB_GRH_NEXT_HDR;
	hdr->hop_limit = grh->hop_limit;
	/* The SGID is 32-bit aligned. */
	hdr->sgid.global.subnet_prefix = ibp->rvp.gid_prefix;
	hdr->sgid.global.interface_id =
		grh->sgid_index && grh->sgid_index < ARRAY_SIZE(ibp->guids) ?
		ibp->guids[grh->sgid_index - 1] :
			cpu_to_be64(ppd_from_ibp(ibp)->guid);
	hdr->dgid = grh->dgid;

	/* GRH header size in 32-bit words. */
	return sizeof(struct ib_grh) / sizeof(u32);
}

#define BTH2_OFFSET (offsetof(struct hfi1_sdma_header, hdr.u.oth.bth[2]) / 4)

/**
 * build_ahg - create ahg in s_ahg
 * @qp: a pointer to QP
 * @npsn: the next PSN for the request/response
 *
 * This routine handles the AHG by allocating an ahg entry and causing the
 * copy of the first middle.
 *
 * Subsequent middles use the copied entry, editing the
 * PSN with 1 or 2 edits.
 */
static inline void build_ahg(struct rvt_qp *qp, u32 npsn)
{
	struct hfi1_qp_priv *priv = qp->priv;

	if (unlikely(qp->s_flags & RVT_S_AHG_CLEAR))
		clear_ahg(qp);
	if (!(qp->s_flags & RVT_S_AHG_VALID)) {
		/* first middle that needs copy  */
		if (qp->s_ahgidx < 0)
			qp->s_ahgidx = sdma_ahg_alloc(priv->s_sde);
		if (qp->s_ahgidx >= 0) {
			qp->s_ahgpsn = npsn;
			priv->s_ahg->tx_flags |= SDMA_TXREQ_F_AHG_COPY;
			/* save to protect a change in another thread */
			priv->s_ahg->ahgidx = qp->s_ahgidx;
			qp->s_flags |= RVT_S_AHG_VALID;
		}
	} else {
		/* subsequent middle after valid */
		if (qp->s_ahgidx >= 0) {
			priv->s_ahg->tx_flags |= SDMA_TXREQ_F_USE_AHG;
			priv->s_ahg->ahgidx = qp->s_ahgidx;
			priv->s_ahg->ahgcount++;
			priv->s_ahg->ahgdesc[0] =
				sdma_build_ahg_descriptor(
					(__force u16)cpu_to_be16((u16)npsn),
					BTH2_OFFSET,
					16,
					16);
			if ((npsn & 0xffff0000) !=
					(qp->s_ahgpsn & 0xffff0000)) {
				priv->s_ahg->ahgcount++;
				priv->s_ahg->ahgdesc[1] =
					sdma_build_ahg_descriptor(
						(__force u16)cpu_to_be16(
							(u16)(npsn >> 16)),
						BTH2_OFFSET,
						0,
						16);
			}
		}
	}
}

void hfi1_make_ruc_header(struct rvt_qp *qp, struct ib_other_headers *ohdr,
			  u32 bth0, u32 bth2, int middle,
			  struct hfi1_pkt_state *ps)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ibport *ibp = ps->ibp;
	u16 lrh0;
	u32 nwords;
	u32 extra_bytes;
	u32 bth1;

	/* Construct the header. */
	extra_bytes = -qp->s_cur_size & 3;
	nwords = (qp->s_cur_size + extra_bytes) >> 2;
	lrh0 = HFI1_LRH_BTH;
	if (unlikely(qp->remote_ah_attr.ah_flags & IB_AH_GRH)) {
		qp->s_hdrwords += hfi1_make_grh(ibp,
						&ps->s_txreq->phdr.hdr.u.l.grh,
						&qp->remote_ah_attr.grh,
						qp->s_hdrwords, nwords);
		lrh0 = HFI1_LRH_GRH;
		middle = 0;
	}
	lrh0 |= (priv->s_sc & 0xf) << 12 | (qp->remote_ah_attr.sl & 0xf) << 4;
	/*
	 * reset s_ahg/AHG fields
	 *
	 * This insures that the ahgentry/ahgcount
	 * are at a non-AHG default to protect
	 * build_verbs_tx_desc() from using
	 * an include ahgidx.
	 *
	 * build_ahg() will modify as appropriate
	 * to use the AHG feature.
	 */
	priv->s_ahg->tx_flags = 0;
	priv->s_ahg->ahgcount = 0;
	priv->s_ahg->ahgidx = 0;
	if (qp->s_mig_state == IB_MIG_MIGRATED)
		bth0 |= IB_BTH_MIG_REQ;
	else
		middle = 0;
	if (middle)
		build_ahg(qp, bth2);
	else
		qp->s_flags &= ~RVT_S_AHG_VALID;
	ps->s_txreq->phdr.hdr.lrh[0] = cpu_to_be16(lrh0);
	ps->s_txreq->phdr.hdr.lrh[1] = cpu_to_be16(qp->remote_ah_attr.dlid);
	ps->s_txreq->phdr.hdr.lrh[2] =
		cpu_to_be16(qp->s_hdrwords + nwords + SIZE_OF_CRC);
	ps->s_txreq->phdr.hdr.lrh[3] = cpu_to_be16(ppd_from_ibp(ibp)->lid |
				       qp->remote_ah_attr.src_path_bits);
	bth0 |= hfi1_get_pkey(ibp, qp->s_pkey_index);
	bth0 |= extra_bytes << 20;
	ohdr->bth[0] = cpu_to_be32(bth0);
	bth1 = qp->remote_qpn;
	if (qp->s_flags & RVT_S_ECN) {
		qp->s_flags &= ~RVT_S_ECN;
		/* we recently received a FECN, so return a BECN */
		bth1 |= (HFI1_BECN_MASK << HFI1_BECN_SHIFT);
	}
	ohdr->bth[1] = cpu_to_be32(bth1);
	ohdr->bth[2] = cpu_to_be32(bth2);
}

/* when sending, force a reschedule every one of these periods */
#define SEND_RESCHED_TIMEOUT (5 * HZ)  /* 5s in jiffies */

void _hfi1_do_send(struct work_struct *work)
{
	struct iowait *wait = container_of(work, struct iowait, iowork);
	struct rvt_qp *qp = iowait_to_qp(wait);

	hfi1_do_send(qp);
}

/**
 * hfi1_do_send - perform a send on a QP
 * @work: contains a pointer to the QP
 *
 * Process entries in the send work queue until credit or queue is
 * exhausted.  Only allow one CPU to send a packet per QP.
 * Otherwise, two threads could send packets out of order.
 */
void hfi1_do_send(struct rvt_qp *qp)
{
	struct hfi1_pkt_state ps;
	struct hfi1_qp_priv *priv = qp->priv;
	int (*make_req)(struct rvt_qp *qp, struct hfi1_pkt_state *ps);
	unsigned long timeout;
	unsigned long timeout_int;
	int cpu;

	ps.dev = to_idev(qp->ibqp.device);
	ps.ibp = to_iport(qp->ibqp.device, qp->port_num);
	ps.ppd = ppd_from_ibp(ps.ibp);

	switch (qp->ibqp.qp_type) {
	case IB_QPT_RC:
		if (!loopback && ((qp->remote_ah_attr.dlid & ~((1 << ps.ppd->lmc
								) - 1)) ==
				 ps.ppd->lid)) {
			ruc_loopback(qp);
			return;
		}
		make_req = hfi1_make_rc_req;
		timeout_int = (qp->timeout_jiffies);
		break;
	case IB_QPT_UC:
		if (!loopback && ((qp->remote_ah_attr.dlid & ~((1 << ps.ppd->lmc
								) - 1)) ==
				 ps.ppd->lid)) {
			ruc_loopback(qp);
			return;
		}
		make_req = hfi1_make_uc_req;
		timeout_int = SEND_RESCHED_TIMEOUT;
		break;
	default:
		make_req = hfi1_make_ud_req;
		timeout_int = SEND_RESCHED_TIMEOUT;
	}

	spin_lock_irqsave(&qp->s_lock, ps.flags);

	/* Return if we are already busy processing a work request. */
	if (!hfi1_send_ok(qp)) {
		spin_unlock_irqrestore(&qp->s_lock, ps.flags);
		return;
	}

	qp->s_flags |= RVT_S_BUSY;

	timeout = jiffies + (timeout_int) / 8;
	cpu = priv->s_sde ? priv->s_sde->cpu :
			cpumask_first(cpumask_of_node(ps.ppd->dd->node));
	/* insure a pre-built packet is handled  */
	ps.s_txreq = get_waiting_verbs_txreq(qp);
	do {
		/* Check for a constructed packet to be sent. */
		if (qp->s_hdrwords != 0) {
			spin_unlock_irqrestore(&qp->s_lock, ps.flags);
			/*
			 * If the packet cannot be sent now, return and
			 * the send engine will be woken up later.
			 */
			if (hfi1_verbs_send(qp, &ps))
				return;
			/* Record that s_ahg is empty. */
			qp->s_hdrwords = 0;
			/* allow other tasks to run */
			if (unlikely(time_after(jiffies, timeout))) {
				if (workqueue_congested(cpu,
							ps.ppd->hfi1_wq)) {
					spin_lock_irqsave(
						&qp->s_lock,
						ps.flags);
					qp->s_flags &= ~RVT_S_BUSY;
					hfi1_schedule_send(qp);
					spin_unlock_irqrestore(
						&qp->s_lock,
						ps.flags);
					this_cpu_inc(
						*ps.ppd->dd->send_schedule);
					return;
				}
				if (!irqs_disabled()) {
					cond_resched();
					this_cpu_inc(
					   *ps.ppd->dd->send_schedule);
				}
				timeout = jiffies + (timeout_int) / 8;
			}
			spin_lock_irqsave(&qp->s_lock, ps.flags);
		}
	} while (make_req(qp, &ps));

	spin_unlock_irqrestore(&qp->s_lock, ps.flags);
}

/*
 * This should be called with s_lock held.
 */
void hfi1_send_complete(struct rvt_qp *qp, struct rvt_swqe *wqe,
			enum ib_wc_status status)
{
	u32 old_last, last;
	unsigned i;

	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_OR_FLUSH_SEND))
		return;

	last = qp->s_last;
	old_last = last;
	if (++last >= qp->s_size)
		last = 0;
	qp->s_last = last;
	/* See post_send() */
	barrier();
	for (i = 0; i < wqe->wr.num_sge; i++) {
		struct rvt_sge *sge = &wqe->sg_list[i];

		rvt_put_mr(sge->mr);
	}
	if (qp->ibqp.qp_type == IB_QPT_UD ||
	    qp->ibqp.qp_type == IB_QPT_SMI ||
	    qp->ibqp.qp_type == IB_QPT_GSI)
		atomic_dec(&ibah_to_rvtah(wqe->ud_wr.ah)->refcount);

	/* See ch. 11.2.4.1 and 10.7.3.1 */
	if (!(qp->s_flags & RVT_S_SIGNAL_REQ_WR) ||
	    (wqe->wr.send_flags & IB_SEND_SIGNALED) ||
	    status != IB_WC_SUCCESS) {
		struct ib_wc wc;

		memset(&wc, 0, sizeof(wc));
		wc.wr_id = wqe->wr.wr_id;
		wc.status = status;
		wc.opcode = ib_hfi1_wc_opcode[wqe->wr.opcode];
		wc.qp = &qp->ibqp;
		if (status == IB_WC_SUCCESS)
			wc.byte_len = wqe->length;
		rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.send_cq), &wc,
			     status != IB_WC_SUCCESS);
	}

	if (qp->s_acked == old_last)
		qp->s_acked = last;
	if (qp->s_cur == old_last)
		qp->s_cur = last;
	if (qp->s_tail == old_last)
		qp->s_tail = last;
	if (qp->state == IB_QPS_SQD && last == qp->s_cur)
		qp->s_draining = 0;
}

/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/srq.h>
#include <linux/slab.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>

#include "mlx5_ib.h"

/* not supported currently */
static int srq_signature;

static void *get_wqe(struct mlx5_ib_srq *srq, int n)
{
	return mlx5_buf_offset(&srq->buf, n << srq->msrq.wqe_shift);
}

static void mlx5_ib_srq_event(struct mlx5_core_srq *srq, enum mlx5_event type)
{
	struct ib_event event;
	struct ib_srq *ibsrq = &to_mibsrq(srq)->ibsrq;

	if (ibsrq->event_handler) {
		event.device      = ibsrq->device;
		event.element.srq = ibsrq;
		switch (type) {
		case MLX5_EVENT_TYPE_SRQ_RQ_LIMIT:
			event.event = IB_EVENT_SRQ_LIMIT_REACHED;
			break;
		case MLX5_EVENT_TYPE_SRQ_CATAS_ERROR:
			event.event = IB_EVENT_SRQ_ERR;
			break;
		default:
			pr_warn("mlx5_ib: Unexpected event type %d on SRQ %06x\n",
				type, srq->srqn);
			return;
		}

		ibsrq->event_handler(&event, ibsrq->srq_context);
	}
}

static int create_srq_user(struct ib_pd *pd, struct mlx5_ib_srq *srq,
			   struct mlx5_srq_attr *in,
			   struct ib_udata *udata, int buf_size)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_create_srq ucmd = {};
	size_t ucmdlen;
	int err;
	int npages;
	int page_shift;
	int ncont;
	u32 offset;
	u32 uidx = MLX5_IB_DEFAULT_UIDX;

	ucmdlen = min(udata->inlen, sizeof(ucmd));

	if (ib_copy_from_udata(&ucmd, udata, ucmdlen)) {
		mlx5_ib_dbg(dev, "failed copy udata\n");
		return -EFAULT;
	}

	if (ucmd.reserved0 || ucmd.reserved1)
		return -EINVAL;

	if (udata->inlen > sizeof(ucmd) &&
	    !ib_is_udata_cleared(udata, sizeof(ucmd),
				 udata->inlen - sizeof(ucmd)))
		return -EINVAL;

	if (in->type == IB_SRQT_XRC) {
		err = get_srq_user_index(to_mucontext(pd->uobject->context),
					 &ucmd, udata->inlen, &uidx);
		if (err)
			return err;
	}

	srq->wq_sig = !!(ucmd.flags & MLX5_SRQ_FLAG_SIGNATURE);

	srq->umem = ib_umem_get(pd->uobject->context, ucmd.buf_addr, buf_size,
				0, 0);
	if (IS_ERR(srq->umem)) {
		mlx5_ib_dbg(dev, "failed umem get, size %d\n", buf_size);
		err = PTR_ERR(srq->umem);
		return err;
	}

	mlx5_ib_cont_pages(srq->umem, ucmd.buf_addr, &npages,
			   &page_shift, &ncont, NULL);
	err = mlx5_ib_get_buf_offset(ucmd.buf_addr, page_shift,
				     &offset);
	if (err) {
		mlx5_ib_warn(dev, "bad offset\n");
		goto err_umem;
	}

	in->pas = mlx5_vzalloc(sizeof(*in->pas) * ncont);
	if (!in->pas) {
		err = -ENOMEM;
		goto err_umem;
	}

	mlx5_ib_populate_pas(dev, srq->umem, page_shift, in->pas, 0);

	err = mlx5_ib_db_map_user(to_mucontext(pd->uobject->context),
				  ucmd.db_addr, &srq->db);
	if (err) {
		mlx5_ib_dbg(dev, "map doorbell failed\n");
		goto err_in;
	}

	in->log_page_size = page_shift - MLX5_ADAPTER_PAGE_SHIFT;
	in->page_offset = offset;
	if (MLX5_CAP_GEN(dev->mdev, cqe_version) == MLX5_CQE_VERSION_V1 &&
	    in->type == IB_SRQT_XRC)
		in->user_index = uidx;

	return 0;

err_in:
	kvfree(in->pas);

err_umem:
	ib_umem_release(srq->umem);

	return err;
}

static int create_srq_kernel(struct mlx5_ib_dev *dev, struct mlx5_ib_srq *srq,
			     struct mlx5_srq_attr *in, int buf_size)
{
	int err;
	int i;
	struct mlx5_wqe_srq_next_seg *next;
	int page_shift;
	int npages;

	err = mlx5_db_alloc(dev->mdev, &srq->db);
	if (err) {
		mlx5_ib_warn(dev, "alloc dbell rec failed\n");
		return err;
	}

	if (mlx5_buf_alloc(dev->mdev, buf_size, &srq->buf)) {
		mlx5_ib_dbg(dev, "buf alloc failed\n");
		err = -ENOMEM;
		goto err_db;
	}
	page_shift = srq->buf.page_shift;

	srq->head    = 0;
	srq->tail    = srq->msrq.max - 1;
	srq->wqe_ctr = 0;

	for (i = 0; i < srq->msrq.max; i++) {
		next = get_wqe(srq, i);
		next->next_wqe_index =
			cpu_to_be16((i + 1) & (srq->msrq.max - 1));
	}

	npages = DIV_ROUND_UP(srq->buf.npages, 1 << (page_shift - PAGE_SHIFT));
	mlx5_ib_dbg(dev, "buf_size %d, page_shift %d, npages %d, calc npages %d\n",
		    buf_size, page_shift, srq->buf.npages, npages);
	in->pas = mlx5_vzalloc(sizeof(*in->pas) * npages);
	if (!in->pas) {
		err = -ENOMEM;
		goto err_buf;
	}
	mlx5_fill_page_array(&srq->buf, in->pas);

	srq->wrid = kmalloc(srq->msrq.max * sizeof(u64), GFP_KERNEL);
	if (!srq->wrid) {
		mlx5_ib_dbg(dev, "kmalloc failed %lu\n",
			    (unsigned long)(srq->msrq.max * sizeof(u64)));
		err = -ENOMEM;
		goto err_in;
	}
	srq->wq_sig = !!srq_signature;

	in->log_page_size = page_shift - MLX5_ADAPTER_PAGE_SHIFT;
	if (MLX5_CAP_GEN(dev->mdev, cqe_version) == MLX5_CQE_VERSION_V1 &&
	    in->type == IB_SRQT_XRC)
		in->user_index = MLX5_IB_DEFAULT_UIDX;

	return 0;

err_in:
	kvfree(in->pas);

err_buf:
	mlx5_buf_free(dev->mdev, &srq->buf);

err_db:
	mlx5_db_free(dev->mdev, &srq->db);
	return err;
}

static void destroy_srq_user(struct ib_pd *pd, struct mlx5_ib_srq *srq)
{
	mlx5_ib_db_unmap_user(to_mucontext(pd->uobject->context), &srq->db);
	ib_umem_release(srq->umem);
}


static void destroy_srq_kernel(struct mlx5_ib_dev *dev, struct mlx5_ib_srq *srq)
{
	kfree(srq->wrid);
	mlx5_buf_free(dev->mdev, &srq->buf);
	mlx5_db_free(dev->mdev, &srq->db);
}

struct ib_srq *mlx5_ib_create_srq(struct ib_pd *pd,
				  struct ib_srq_init_attr *init_attr,
				  struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_srq *srq;
	int desc_size;
	int buf_size;
	int err;
	struct mlx5_srq_attr in = {0};
	__u32 max_srq_wqes = 1 << MLX5_CAP_GEN(dev->mdev, log_max_srq_sz);

	/* Sanity check SRQ size before proceeding */
	if (init_attr->attr.max_wr >= max_srq_wqes) {
		mlx5_ib_dbg(dev, "max_wr %d, cap %d\n",
			    init_attr->attr.max_wr,
			    max_srq_wqes);
		return ERR_PTR(-EINVAL);
	}

	srq = kmalloc(sizeof(*srq), GFP_KERNEL);
	if (!srq)
		return ERR_PTR(-ENOMEM);

	mutex_init(&srq->mutex);
	spin_lock_init(&srq->lock);
	srq->msrq.max    = roundup_pow_of_two(init_attr->attr.max_wr + 1);
	srq->msrq.max_gs = init_attr->attr.max_sge;

	desc_size = sizeof(struct mlx5_wqe_srq_next_seg) +
		    srq->msrq.max_gs * sizeof(struct mlx5_wqe_data_seg);
	desc_size = roundup_pow_of_two(desc_size);
	desc_size = max_t(int, 32, desc_size);
	srq->msrq.max_avail_gather = (desc_size - sizeof(struct mlx5_wqe_srq_next_seg)) /
		sizeof(struct mlx5_wqe_data_seg);
	srq->msrq.wqe_shift = ilog2(desc_size);
	buf_size = srq->msrq.max * desc_size;
	mlx5_ib_dbg(dev, "desc_size 0x%x, req wr 0x%x, srq size 0x%x, max_gs 0x%x, max_avail_gather 0x%x\n",
		    desc_size, init_attr->attr.max_wr, srq->msrq.max, srq->msrq.max_gs,
		    srq->msrq.max_avail_gather);

	if (pd->uobject)
		err = create_srq_user(pd, srq, &in, udata, buf_size);
	else
		err = create_srq_kernel(dev, srq, &in, buf_size);

	if (err) {
		mlx5_ib_warn(dev, "create srq %s failed, err %d\n",
			     pd->uobject ? "user" : "kernel", err);
		goto err_srq;
	}

	in.type = init_attr->srq_type;
	in.log_size = ilog2(srq->msrq.max);
	in.wqe_shift = srq->msrq.wqe_shift - 4;
	if (srq->wq_sig)
		in.flags |= MLX5_SRQ_FLAG_WQ_SIG;
	if (init_attr->srq_type == IB_SRQT_XRC) {
		in.xrcd = to_mxrcd(init_attr->ext.xrc.xrcd)->xrcdn;
		in.cqn = to_mcq(init_attr->ext.xrc.cq)->mcq.cqn;
	} else if (init_attr->srq_type == IB_SRQT_BASIC) {
		in.xrcd = to_mxrcd(dev->devr.x0)->xrcdn;
		in.cqn = to_mcq(dev->devr.c0)->mcq.cqn;
	}

	in.pd = to_mpd(pd)->pdn;
	in.db_record = srq->db.dma;
	err = mlx5_core_create_srq(dev->mdev, &srq->msrq, &in);
	kvfree(in.pas);
	if (err) {
		mlx5_ib_dbg(dev, "create SRQ failed, err %d\n", err);
		goto err_usr_kern_srq;
	}

	mlx5_ib_dbg(dev, "create SRQ with srqn 0x%x\n", srq->msrq.srqn);

	srq->msrq.event = mlx5_ib_srq_event;
	srq->ibsrq.ext.xrc.srq_num = srq->msrq.srqn;

	if (pd->uobject)
		if (ib_copy_to_udata(udata, &srq->msrq.srqn, sizeof(__u32))) {
			mlx5_ib_dbg(dev, "copy to user failed\n");
			err = -EFAULT;
			goto err_core;
		}

	init_attr->attr.max_wr = srq->msrq.max - 1;

	return &srq->ibsrq;

err_core:
	mlx5_core_destroy_srq(dev->mdev, &srq->msrq);

err_usr_kern_srq:
	if (pd->uobject)
		destroy_srq_user(pd, srq);
	else
		destroy_srq_kernel(dev, srq);

err_srq:
	kfree(srq);

	return ERR_PTR(err);
}

int mlx5_ib_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		       enum ib_srq_attr_mask attr_mask, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(ibsrq->device);
	struct mlx5_ib_srq *srq = to_msrq(ibsrq);
	int ret;

	/* We don't support resizing SRQs yet */
	if (attr_mask & IB_SRQ_MAX_WR)
		return -EINVAL;

	if (attr_mask & IB_SRQ_LIMIT) {
		if (attr->srq_limit >= srq->msrq.max)
			return -EINVAL;

		mutex_lock(&srq->mutex);
		ret = mlx5_core_arm_srq(dev->mdev, &srq->msrq, attr->srq_limit, 1);
		mutex_unlock(&srq->mutex);

		if (ret)
			return ret;
	}

	return 0;
}

int mlx5_ib_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr)
{
	struct mlx5_ib_dev *dev = to_mdev(ibsrq->device);
	struct mlx5_ib_srq *srq = to_msrq(ibsrq);
	int ret;
	struct mlx5_srq_attr *out;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	ret = mlx5_core_query_srq(dev->mdev, &srq->msrq, out);
	if (ret)
		goto out_box;

	srq_attr->srq_limit = out->lwm;
	srq_attr->max_wr    = srq->msrq.max - 1;
	srq_attr->max_sge   = srq->msrq.max_gs;

out_box:
	kfree(out);
	return ret;
}

int mlx5_ib_destroy_srq(struct ib_srq *srq)
{
	struct mlx5_ib_dev *dev = to_mdev(srq->device);
	struct mlx5_ib_srq *msrq = to_msrq(srq);

	mlx5_core_destroy_srq(dev->mdev, &msrq->msrq);

	if (srq->uobject) {
		mlx5_ib_db_unmap_user(to_mucontext(srq->uobject->context), &msrq->db);
		ib_umem_release(msrq->umem);
	} else {
		destroy_srq_kernel(dev, msrq);
	}

	kfree(srq);
	return 0;
}

void mlx5_ib_free_srq_wqe(struct mlx5_ib_srq *srq, int wqe_index)
{
	struct mlx5_wqe_srq_next_seg *next;

	/* always called with interrupts disabled. */
	spin_lock(&srq->lock);

	next = get_wqe(srq, srq->tail);
	next->next_wqe_index = cpu_to_be16(wqe_index);
	srq->tail = wqe_index;

	spin_unlock(&srq->lock);
}

int mlx5_ib_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			  struct ib_recv_wr **bad_wr)
{
	struct mlx5_ib_srq *srq = to_msrq(ibsrq);
	struct mlx5_wqe_srq_next_seg *next;
	struct mlx5_wqe_data_seg *scat;
	struct mlx5_ib_dev *dev = to_mdev(ibsrq->device);
	struct mlx5_core_dev *mdev = dev->mdev;
	unsigned long flags;
	int err = 0;
	int nreq;
	int i;

	spin_lock_irqsave(&srq->lock, flags);

	if (mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		err = -EIO;
		*bad_wr = wr;
		goto out;
	}

	for (nreq = 0; wr; nreq++, wr = wr->next) {
		if (unlikely(wr->num_sge > srq->msrq.max_gs)) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}

		if (unlikely(srq->head == srq->tail)) {
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		srq->wrid[srq->head] = wr->wr_id;

		next      = get_wqe(srq, srq->head);
		srq->head = be16_to_cpu(next->next_wqe_index);
		scat      = (struct mlx5_wqe_data_seg *)(next + 1);

		for (i = 0; i < wr->num_sge; i++) {
			scat[i].byte_count = cpu_to_be32(wr->sg_list[i].length);
			scat[i].lkey       = cpu_to_be32(wr->sg_list[i].lkey);
			scat[i].addr       = cpu_to_be64(wr->sg_list[i].addr);
		}

		if (i < srq->msrq.max_avail_gather) {
			scat[i].byte_count = 0;
			scat[i].lkey       = cpu_to_be32(MLX5_INVALID_LKEY);
			scat[i].addr       = 0;
		}
	}

	if (likely(nreq)) {
		srq->wqe_ctr += nreq;

		/* Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*srq->db.db = cpu_to_be32(srq->wqe_ctr);
	}
out:
	spin_unlock_irqrestore(&srq->lock, flags);

	return err;
}

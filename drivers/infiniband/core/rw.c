/*
 * Copyright (c) 2016 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <rdma/mr_pool.h>
#include <rdma/rw.h>

enum {
	RDMA_RW_SINGLE_WR,
	RDMA_RW_MULTI_WR,
	RDMA_RW_MR,
	RDMA_RW_SIG_MR,
};

static bool rdma_rw_force_mr;
module_param_named(force_mr, rdma_rw_force_mr, bool, 0);
MODULE_PARM_DESC(force_mr, "Force usage of MRs for RDMA READ/WRITE operations");

/*
 * Check if the device might use memory registration.  This is currently only
 * true for iWarp devices. In the future we can hopefully fine tune this based
 * on HCA driver input.
 */
static inline bool rdma_rw_can_use_mr(struct ib_device *dev, u8 port_num)
{
	if (rdma_protocol_iwarp(dev, port_num))
		return true;
	if (unlikely(rdma_rw_force_mr))
		return true;
	return false;
}

/*
 * Check if the device will use memory registration for this RW operation.
 * We currently always use memory registrations for iWarp RDMA READs, and
 * have a debug option to force usage of MRs.
 *
 * XXX: In the future we can hopefully fine tune this based on HCA driver
 * input.
 */
static inline bool rdma_rw_io_needs_mr(struct ib_device *dev, u8 port_num,
		enum dma_data_direction dir, int dma_nents)
{
	if (rdma_protocol_iwarp(dev, port_num) && dir == DMA_FROM_DEVICE)
		return true;
	if (unlikely(rdma_rw_force_mr))
		return true;
	return false;
}

static inline u32 rdma_rw_fr_page_list_len(struct ib_device *dev)
{
	/* arbitrary limit to avoid allocating gigantic resources */
	return min_t(u32, dev->attrs.max_fast_reg_page_list_len, 256);
}

/* Caller must have zero-initialized *reg. */
static int rdma_rw_init_one_mr(struct ib_qp *qp, u8 port_num,
		struct rdma_rw_reg_ctx *reg, struct scatterlist *sg,
		u32 sg_cnt, u32 offset)
{
	u32 pages_per_mr = rdma_rw_fr_page_list_len(qp->pd->device);
	u32 nents = min(sg_cnt, pages_per_mr);
	int count = 0, ret;

	reg->mr = ib_mr_pool_get(qp, &qp->rdma_mrs);
	if (!reg->mr)
		return -EAGAIN;

	if (reg->mr->need_inval) {
		reg->inv_wr.opcode = IB_WR_LOCAL_INV;
		reg->inv_wr.ex.invalidate_rkey = reg->mr->lkey;
		reg->inv_wr.next = &reg->reg_wr.wr;
		count++;
	} else {
		reg->inv_wr.next = NULL;
	}

	ret = ib_map_mr_sg(reg->mr, sg, nents, &offset, PAGE_SIZE);
	if (ret < nents) {
		ib_mr_pool_put(qp, &qp->rdma_mrs, reg->mr);
		return -EINVAL;
	}

	reg->reg_wr.wr.opcode = IB_WR_REG_MR;
	reg->reg_wr.mr = reg->mr;
	reg->reg_wr.access = IB_ACCESS_LOCAL_WRITE;
	if (rdma_protocol_iwarp(qp->device, port_num))
		reg->reg_wr.access |= IB_ACCESS_REMOTE_WRITE;
	count++;

	reg->sge.addr = reg->mr->iova;
	reg->sge.length = reg->mr->length;
	return count;
}

static int rdma_rw_init_mr_wrs(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u8 port_num, struct scatterlist *sg, u32 sg_cnt, u32 offset,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir)
{
	struct rdma_rw_reg_ctx *prev = NULL;
	u32 pages_per_mr = rdma_rw_fr_page_list_len(qp->pd->device);
	int i, j, ret = 0, count = 0;

	ctx->nr_ops = (sg_cnt + pages_per_mr - 1) / pages_per_mr;
	ctx->reg = kcalloc(ctx->nr_ops, sizeof(*ctx->reg), GFP_KERNEL);
	if (!ctx->reg) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ctx->nr_ops; i++) {
		struct rdma_rw_reg_ctx *reg = &ctx->reg[i];
		u32 nents = min(sg_cnt, pages_per_mr);

		ret = rdma_rw_init_one_mr(qp, port_num, reg, sg, sg_cnt,
				offset);
		if (ret < 0)
			goto out_free;
		count += ret;

		if (prev) {
			if (reg->mr->need_inval)
				prev->wr.wr.next = &reg->inv_wr;
			else
				prev->wr.wr.next = &reg->reg_wr.wr;
		}

		reg->reg_wr.wr.next = &reg->wr.wr;

		reg->wr.wr.sg_list = &reg->sge;
		reg->wr.wr.num_sge = 1;
		reg->wr.remote_addr = remote_addr;
		reg->wr.rkey = rkey;
		if (dir == DMA_TO_DEVICE) {
			reg->wr.wr.opcode = IB_WR_RDMA_WRITE;
		} else if (!rdma_cap_read_inv(qp->device, port_num)) {
			reg->wr.wr.opcode = IB_WR_RDMA_READ;
		} else {
			reg->wr.wr.opcode = IB_WR_RDMA_READ_WITH_INV;
			reg->wr.wr.ex.invalidate_rkey = reg->mr->lkey;
		}
		count++;

		remote_addr += reg->sge.length;
		sg_cnt -= nents;
		for (j = 0; j < nents; j++)
			sg = sg_next(sg);
		prev = reg;
		offset = 0;
	}

	if (prev)
		prev->wr.wr.next = NULL;

	ctx->type = RDMA_RW_MR;
	return count;

out_free:
	while (--i >= 0)
		ib_mr_pool_put(qp, &qp->rdma_mrs, ctx->reg[i].mr);
	kfree(ctx->reg);
out:
	return ret;
}

static int rdma_rw_init_map_wrs(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		struct scatterlist *sg, u32 sg_cnt, u32 offset,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir)
{
	struct ib_device *dev = qp->pd->device;
	u32 max_sge = dir == DMA_TO_DEVICE ? qp->max_write_sge :
		      qp->max_read_sge;
	struct ib_sge *sge;
	u32 total_len = 0, i, j;

	ctx->nr_ops = DIV_ROUND_UP(sg_cnt, max_sge);

	ctx->map.sges = sge = kcalloc(sg_cnt, sizeof(*sge), GFP_KERNEL);
	if (!ctx->map.sges)
		goto out;

	ctx->map.wrs = kcalloc(ctx->nr_ops, sizeof(*ctx->map.wrs), GFP_KERNEL);
	if (!ctx->map.wrs)
		goto out_free_sges;

	for (i = 0; i < ctx->nr_ops; i++) {
		struct ib_rdma_wr *rdma_wr = &ctx->map.wrs[i];
		u32 nr_sge = min(sg_cnt, max_sge);

		if (dir == DMA_TO_DEVICE)
			rdma_wr->wr.opcode = IB_WR_RDMA_WRITE;
		else
			rdma_wr->wr.opcode = IB_WR_RDMA_READ;
		rdma_wr->remote_addr = remote_addr + total_len;
		rdma_wr->rkey = rkey;
		rdma_wr->wr.num_sge = nr_sge;
		rdma_wr->wr.sg_list = sge;

		for (j = 0; j < nr_sge; j++, sg = sg_next(sg)) {
			sge->addr = ib_sg_dma_address(dev, sg) + offset;
			sge->length = ib_sg_dma_len(dev, sg) - offset;
			sge->lkey = qp->pd->local_dma_lkey;

			total_len += sge->length;
			sge++;
			sg_cnt--;
			offset = 0;
		}

		rdma_wr->wr.next = i + 1 < ctx->nr_ops ?
			&ctx->map.wrs[i + 1].wr : NULL;
	}

	ctx->type = RDMA_RW_MULTI_WR;
	return ctx->nr_ops;

out_free_sges:
	kfree(ctx->map.sges);
out:
	return -ENOMEM;
}

static int rdma_rw_init_single_wr(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		struct scatterlist *sg, u32 offset, u64 remote_addr, u32 rkey,
		enum dma_data_direction dir)
{
	struct ib_device *dev = qp->pd->device;
	struct ib_rdma_wr *rdma_wr = &ctx->single.wr;

	ctx->nr_ops = 1;

	ctx->single.sge.lkey = qp->pd->local_dma_lkey;
	ctx->single.sge.addr = ib_sg_dma_address(dev, sg) + offset;
	ctx->single.sge.length = ib_sg_dma_len(dev, sg) - offset;

	memset(rdma_wr, 0, sizeof(*rdma_wr));
	if (dir == DMA_TO_DEVICE)
		rdma_wr->wr.opcode = IB_WR_RDMA_WRITE;
	else
		rdma_wr->wr.opcode = IB_WR_RDMA_READ;
	rdma_wr->wr.sg_list = &ctx->single.sge;
	rdma_wr->wr.num_sge = 1;
	rdma_wr->remote_addr = remote_addr;
	rdma_wr->rkey = rkey;

	ctx->type = RDMA_RW_SINGLE_WR;
	return 1;
}

/**
 * rdma_rw_ctx_init - initialize a RDMA READ/WRITE context
 * @ctx:	context to initialize
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist to READ/WRITE from/to
 * @sg_cnt:	number of entries in @sg
 * @sg_offset:	current byte offset into @sg
 * @remote_addr:remote address to read/write (relative to @rkey)
 * @rkey:	remote key to operate on
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 *
 * Returns the number of WQEs that will be needed on the workqueue if
 * successful, or a negative error code.
 */
int rdma_rw_ctx_init(struct rdma_rw_ctx *ctx, struct ib_qp *qp, u8 port_num,
		struct scatterlist *sg, u32 sg_cnt, u32 sg_offset,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir)
{
	struct ib_device *dev = qp->pd->device;
	int ret;

	ret = ib_dma_map_sg(dev, sg, sg_cnt, dir);
	if (!ret)
		return -ENOMEM;
	sg_cnt = ret;

	/*
	 * Skip to the S/G entry that sg_offset falls into:
	 */
	for (;;) {
		u32 len = ib_sg_dma_len(dev, sg);

		if (sg_offset < len)
			break;

		sg = sg_next(sg);
		sg_offset -= len;
		sg_cnt--;
	}

	ret = -EIO;
	if (WARN_ON_ONCE(sg_cnt == 0))
		goto out_unmap_sg;

	if (rdma_rw_io_needs_mr(qp->device, port_num, dir, sg_cnt)) {
		ret = rdma_rw_init_mr_wrs(ctx, qp, port_num, sg, sg_cnt,
				sg_offset, remote_addr, rkey, dir);
	} else if (sg_cnt > 1) {
		ret = rdma_rw_init_map_wrs(ctx, qp, sg, sg_cnt, sg_offset,
				remote_addr, rkey, dir);
	} else {
		ret = rdma_rw_init_single_wr(ctx, qp, sg, sg_offset,
				remote_addr, rkey, dir);
	}

	if (ret < 0)
		goto out_unmap_sg;
	return ret;

out_unmap_sg:
	ib_dma_unmap_sg(dev, sg, sg_cnt, dir);
	return ret;
}
EXPORT_SYMBOL(rdma_rw_ctx_init);

/**
 * rdma_rw_ctx_signature init - initialize a RW context with signature offload
 * @ctx:	context to initialize
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist to READ/WRITE from/to
 * @sg_cnt:	number of entries in @sg
 * @prot_sg:	scatterlist to READ/WRITE protection information from/to
 * @prot_sg_cnt: number of entries in @prot_sg
 * @sig_attrs:	signature offloading algorithms
 * @remote_addr:remote address to read/write (relative to @rkey)
 * @rkey:	remote key to operate on
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 *
 * Returns the number of WQEs that will be needed on the workqueue if
 * successful, or a negative error code.
 */
int rdma_rw_ctx_signature_init(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u8 port_num, struct scatterlist *sg, u32 sg_cnt,
		struct scatterlist *prot_sg, u32 prot_sg_cnt,
		struct ib_sig_attrs *sig_attrs,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir)
{
	struct ib_device *dev = qp->pd->device;
	u32 pages_per_mr = rdma_rw_fr_page_list_len(qp->pd->device);
	struct ib_rdma_wr *rdma_wr;
	struct ib_send_wr *prev_wr = NULL;
	int count = 0, ret;

	if (sg_cnt > pages_per_mr || prot_sg_cnt > pages_per_mr) {
		pr_err("SG count too large\n");
		return -EINVAL;
	}

	ret = ib_dma_map_sg(dev, sg, sg_cnt, dir);
	if (!ret)
		return -ENOMEM;
	sg_cnt = ret;

	ret = ib_dma_map_sg(dev, prot_sg, prot_sg_cnt, dir);
	if (!ret) {
		ret = -ENOMEM;
		goto out_unmap_sg;
	}
	prot_sg_cnt = ret;

	ctx->type = RDMA_RW_SIG_MR;
	ctx->nr_ops = 1;
	ctx->sig = kcalloc(1, sizeof(*ctx->sig), GFP_KERNEL);
	if (!ctx->sig) {
		ret = -ENOMEM;
		goto out_unmap_prot_sg;
	}

	ret = rdma_rw_init_one_mr(qp, port_num, &ctx->sig->data, sg, sg_cnt, 0);
	if (ret < 0)
		goto out_free_ctx;
	count += ret;
	prev_wr = &ctx->sig->data.reg_wr.wr;

	if (prot_sg_cnt) {
		ret = rdma_rw_init_one_mr(qp, port_num, &ctx->sig->prot,
				prot_sg, prot_sg_cnt, 0);
		if (ret < 0)
			goto out_destroy_data_mr;
		count += ret;

		if (ctx->sig->prot.inv_wr.next)
			prev_wr->next = &ctx->sig->prot.inv_wr;
		else
			prev_wr->next = &ctx->sig->prot.reg_wr.wr;
		prev_wr = &ctx->sig->prot.reg_wr.wr;
	} else {
		ctx->sig->prot.mr = NULL;
	}

	ctx->sig->sig_mr = ib_mr_pool_get(qp, &qp->sig_mrs);
	if (!ctx->sig->sig_mr) {
		ret = -EAGAIN;
		goto out_destroy_prot_mr;
	}

	if (ctx->sig->sig_mr->need_inval) {
		memset(&ctx->sig->sig_inv_wr, 0, sizeof(ctx->sig->sig_inv_wr));

		ctx->sig->sig_inv_wr.opcode = IB_WR_LOCAL_INV;
		ctx->sig->sig_inv_wr.ex.invalidate_rkey = ctx->sig->sig_mr->rkey;

		prev_wr->next = &ctx->sig->sig_inv_wr;
		prev_wr = &ctx->sig->sig_inv_wr;
	}

	ctx->sig->sig_wr.wr.opcode = IB_WR_REG_SIG_MR;
	ctx->sig->sig_wr.wr.wr_cqe = NULL;
	ctx->sig->sig_wr.wr.sg_list = &ctx->sig->data.sge;
	ctx->sig->sig_wr.wr.num_sge = 1;
	ctx->sig->sig_wr.access_flags = IB_ACCESS_LOCAL_WRITE;
	ctx->sig->sig_wr.sig_attrs = sig_attrs;
	ctx->sig->sig_wr.sig_mr = ctx->sig->sig_mr;
	if (prot_sg_cnt)
		ctx->sig->sig_wr.prot = &ctx->sig->prot.sge;
	prev_wr->next = &ctx->sig->sig_wr.wr;
	prev_wr = &ctx->sig->sig_wr.wr;
	count++;

	ctx->sig->sig_sge.addr = 0;
	ctx->sig->sig_sge.length = ctx->sig->data.sge.length;
	if (sig_attrs->wire.sig_type != IB_SIG_TYPE_NONE)
		ctx->sig->sig_sge.length += ctx->sig->prot.sge.length;

	rdma_wr = &ctx->sig->data.wr;
	rdma_wr->wr.sg_list = &ctx->sig->sig_sge;
	rdma_wr->wr.num_sge = 1;
	rdma_wr->remote_addr = remote_addr;
	rdma_wr->rkey = rkey;
	if (dir == DMA_TO_DEVICE)
		rdma_wr->wr.opcode = IB_WR_RDMA_WRITE;
	else
		rdma_wr->wr.opcode = IB_WR_RDMA_READ;
	prev_wr->next = &rdma_wr->wr;
	prev_wr = &rdma_wr->wr;
	count++;

	return count;

out_destroy_prot_mr:
	if (prot_sg_cnt)
		ib_mr_pool_put(qp, &qp->rdma_mrs, ctx->sig->prot.mr);
out_destroy_data_mr:
	ib_mr_pool_put(qp, &qp->rdma_mrs, ctx->sig->data.mr);
out_free_ctx:
	kfree(ctx->sig);
out_unmap_prot_sg:
	ib_dma_unmap_sg(dev, prot_sg, prot_sg_cnt, dir);
out_unmap_sg:
	ib_dma_unmap_sg(dev, sg, sg_cnt, dir);
	return ret;
}
EXPORT_SYMBOL(rdma_rw_ctx_signature_init);

/*
 * Now that we are going to post the WRs we can update the lkey and need_inval
 * state on the MRs.  If we were doing this at init time, we would get double
 * or missing invalidations if a context was initialized but not actually
 * posted.
 */
static void rdma_rw_update_lkey(struct rdma_rw_reg_ctx *reg, bool need_inval)
{
	reg->mr->need_inval = need_inval;
	ib_update_fast_reg_key(reg->mr, ib_inc_rkey(reg->mr->lkey));
	reg->reg_wr.key = reg->mr->lkey;
	reg->sge.lkey = reg->mr->lkey;
}

/**
 * rdma_rw_ctx_wrs - return chain of WRs for a RDMA READ or WRITE operation
 * @ctx:	context to operate on
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @cqe:	completion queue entry for the last WR
 * @chain_wr:	WR to append to the posted chain
 *
 * Return the WR chain for the set of RDMA READ/WRITE operations described by
 * @ctx, as well as any memory registration operations needed.  If @chain_wr
 * is non-NULL the WR it points to will be appended to the chain of WRs posted.
 * If @chain_wr is not set @cqe must be set so that the caller gets a
 * completion notification.
 */
struct ib_send_wr *rdma_rw_ctx_wrs(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u8 port_num, struct ib_cqe *cqe, struct ib_send_wr *chain_wr)
{
	struct ib_send_wr *first_wr, *last_wr;
	int i;

	switch (ctx->type) {
	case RDMA_RW_SIG_MR:
		rdma_rw_update_lkey(&ctx->sig->data, true);
		if (ctx->sig->prot.mr)
			rdma_rw_update_lkey(&ctx->sig->prot, true);
	
		ctx->sig->sig_mr->need_inval = true;
		ib_update_fast_reg_key(ctx->sig->sig_mr,
			ib_inc_rkey(ctx->sig->sig_mr->lkey));
		ctx->sig->sig_sge.lkey = ctx->sig->sig_mr->lkey;

		if (ctx->sig->data.inv_wr.next)
			first_wr = &ctx->sig->data.inv_wr;
		else
			first_wr = &ctx->sig->data.reg_wr.wr;
		last_wr = &ctx->sig->data.wr.wr;
		break;
	case RDMA_RW_MR:
		for (i = 0; i < ctx->nr_ops; i++) {
			rdma_rw_update_lkey(&ctx->reg[i],
				ctx->reg[i].wr.wr.opcode !=
					IB_WR_RDMA_READ_WITH_INV);
		}

		if (ctx->reg[0].inv_wr.next)
			first_wr = &ctx->reg[0].inv_wr;
		else
			first_wr = &ctx->reg[0].reg_wr.wr;
		last_wr = &ctx->reg[ctx->nr_ops - 1].wr.wr;
		break;
	case RDMA_RW_MULTI_WR:
		first_wr = &ctx->map.wrs[0].wr;
		last_wr = &ctx->map.wrs[ctx->nr_ops - 1].wr;
		break;
	case RDMA_RW_SINGLE_WR:
		first_wr = &ctx->single.wr.wr;
		last_wr = &ctx->single.wr.wr;
		break;
	default:
		BUG();
	}

	if (chain_wr) {
		last_wr->next = chain_wr;
	} else {
		last_wr->wr_cqe = cqe;
		last_wr->send_flags |= IB_SEND_SIGNALED;
	}

	return first_wr;
}
EXPORT_SYMBOL(rdma_rw_ctx_wrs);

/**
 * rdma_rw_ctx_post - post a RDMA READ or RDMA WRITE operation
 * @ctx:	context to operate on
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @cqe:	completion queue entry for the last WR
 * @chain_wr:	WR to append to the posted chain
 *
 * Post the set of RDMA READ/WRITE operations described by @ctx, as well as
 * any memory registration operations needed.  If @chain_wr is non-NULL the
 * WR it points to will be appended to the chain of WRs posted.  If @chain_wr
 * is not set @cqe must be set so that the caller gets a completion
 * notification.
 */
int rdma_rw_ctx_post(struct rdma_rw_ctx *ctx, struct ib_qp *qp, u8 port_num,
		struct ib_cqe *cqe, struct ib_send_wr *chain_wr)
{
	struct ib_send_wr *first_wr, *bad_wr;

	first_wr = rdma_rw_ctx_wrs(ctx, qp, port_num, cqe, chain_wr);
	return ib_post_send(qp, first_wr, &bad_wr);
}
EXPORT_SYMBOL(rdma_rw_ctx_post);

/**
 * rdma_rw_ctx_destroy - release all resources allocated by rdma_rw_ctx_init
 * @ctx:	context to release
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist that was used for the READ/WRITE
 * @sg_cnt:	number of entries in @sg
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 */
void rdma_rw_ctx_destroy(struct rdma_rw_ctx *ctx, struct ib_qp *qp, u8 port_num,
		struct scatterlist *sg, u32 sg_cnt, enum dma_data_direction dir)
{
	int i;

	switch (ctx->type) {
	case RDMA_RW_MR:
		for (i = 0; i < ctx->nr_ops; i++)
			ib_mr_pool_put(qp, &qp->rdma_mrs, ctx->reg[i].mr);
		kfree(ctx->reg);
		break;
	case RDMA_RW_MULTI_WR:
		kfree(ctx->map.wrs);
		kfree(ctx->map.sges);
		break;
	case RDMA_RW_SINGLE_WR:
		break;
	default:
		BUG();
		break;
	}

	ib_dma_unmap_sg(qp->pd->device, sg, sg_cnt, dir);
}
EXPORT_SYMBOL(rdma_rw_ctx_destroy);

/**
 * rdma_rw_ctx_destroy_signature - release all resources allocated by
 *	rdma_rw_ctx_init_signature
 * @ctx:	context to release
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist that was used for the READ/WRITE
 * @sg_cnt:	number of entries in @sg
 * @prot_sg:	scatterlist that was used for the READ/WRITE of the PI
 * @prot_sg_cnt: number of entries in @prot_sg
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 */
void rdma_rw_ctx_destroy_signature(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u8 port_num, struct scatterlist *sg, u32 sg_cnt,
		struct scatterlist *prot_sg, u32 prot_sg_cnt,
		enum dma_data_direction dir)
{
	if (WARN_ON_ONCE(ctx->type != RDMA_RW_SIG_MR))
		return;

	ib_mr_pool_put(qp, &qp->rdma_mrs, ctx->sig->data.mr);
	ib_dma_unmap_sg(qp->pd->device, sg, sg_cnt, dir);

	if (ctx->sig->prot.mr) {
		ib_mr_pool_put(qp, &qp->rdma_mrs, ctx->sig->prot.mr);
		ib_dma_unmap_sg(qp->pd->device, prot_sg, prot_sg_cnt, dir);
	}

	ib_mr_pool_put(qp, &qp->sig_mrs, ctx->sig->sig_mr);
	kfree(ctx->sig);
}
EXPORT_SYMBOL(rdma_rw_ctx_destroy_signature);

void rdma_rw_init_qp(struct ib_device *dev, struct ib_qp_init_attr *attr)
{
	u32 factor;

	WARN_ON_ONCE(attr->port_num == 0);

	/*
	 * Each context needs at least one RDMA READ or WRITE WR.
	 *
	 * For some hardware we might need more, eventually we should ask the
	 * HCA driver for a multiplier here.
	 */
	factor = 1;

	/*
	 * If the devices needs MRs to perform RDMA READ or WRITE operations,
	 * we'll need two additional MRs for the registrations and the
	 * invalidation.
	 */
	if (attr->create_flags & IB_QP_CREATE_SIGNATURE_EN)
		factor += 6;	/* (inv + reg) * (data + prot + sig) */
	else if (rdma_rw_can_use_mr(dev, attr->port_num))
		factor += 2;	/* inv + reg */

	attr->cap.max_send_wr += factor * attr->cap.max_rdma_ctxs;

	/*
	 * But maybe we were just too high in the sky and the device doesn't
	 * even support all we need, and we'll have to live with what we get..
	 */
	attr->cap.max_send_wr =
		min_t(u32, attr->cap.max_send_wr, dev->attrs.max_qp_wr);
}

int rdma_rw_init_mrs(struct ib_qp *qp, struct ib_qp_init_attr *attr)
{
	struct ib_device *dev = qp->pd->device;
	u32 nr_mrs = 0, nr_sig_mrs = 0;
	int ret = 0;

	if (attr->create_flags & IB_QP_CREATE_SIGNATURE_EN) {
		nr_sig_mrs = attr->cap.max_rdma_ctxs;
		nr_mrs = attr->cap.max_rdma_ctxs * 2;
	} else if (rdma_rw_can_use_mr(dev, attr->port_num)) {
		nr_mrs = attr->cap.max_rdma_ctxs;
	}

	if (nr_mrs) {
		ret = ib_mr_pool_init(qp, &qp->rdma_mrs, nr_mrs,
				IB_MR_TYPE_MEM_REG,
				rdma_rw_fr_page_list_len(dev));
		if (ret) {
			pr_err("%s: failed to allocated %d MRs\n",
				__func__, nr_mrs);
			return ret;
		}
	}

	if (nr_sig_mrs) {
		ret = ib_mr_pool_init(qp, &qp->sig_mrs, nr_sig_mrs,
				IB_MR_TYPE_SIGNATURE, 2);
		if (ret) {
			pr_err("%s: failed to allocated %d SIG MRs\n",
				__func__, nr_mrs);
			goto out_free_rdma_mrs;
		}
	}

	return 0;

out_free_rdma_mrs:
	ib_mr_pool_destroy(qp, &qp->rdma_mrs);
	return ret;
}

void rdma_rw_cleanup_mrs(struct ib_qp *qp)
{
	ib_mr_pool_destroy(qp, &qp->sig_mrs);
	ib_mr_pool_destroy(qp, &qp->rdma_mrs);
}

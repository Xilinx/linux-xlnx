/*
 * NVMe I/O command implementation.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/blkdev.h>
#include <linux/module.h>
#include "nvmet.h"

static void nvmet_bio_done(struct bio *bio)
{
	struct nvmet_req *req = bio->bi_private;

	nvmet_req_complete(req,
		bio->bi_error ? NVME_SC_INTERNAL | NVME_SC_DNR : 0);

	if (bio != &req->inline_bio)
		bio_put(bio);
}

static inline u32 nvmet_rw_len(struct nvmet_req *req)
{
	return ((u32)le16_to_cpu(req->cmd->rw.length) + 1) <<
			req->ns->blksize_shift;
}

static void nvmet_inline_bio_init(struct nvmet_req *req)
{
	struct bio *bio = &req->inline_bio;

	bio_init(bio);
	bio->bi_max_vecs = NVMET_MAX_INLINE_BIOVEC;
	bio->bi_io_vec = req->inline_bvec;
}

static void nvmet_execute_rw(struct nvmet_req *req)
{
	int sg_cnt = req->sg_cnt;
	struct scatterlist *sg;
	struct bio *bio;
	sector_t sector;
	blk_qc_t cookie;
	int op, op_flags = 0, i;

	if (!req->sg_cnt) {
		nvmet_req_complete(req, 0);
		return;
	}

	if (req->cmd->rw.opcode == nvme_cmd_write) {
		op = REQ_OP_WRITE;
		op_flags = WRITE_ODIRECT;
		if (req->cmd->rw.control & cpu_to_le16(NVME_RW_FUA))
			op_flags |= REQ_FUA;
	} else {
		op = REQ_OP_READ;
	}

	sector = le64_to_cpu(req->cmd->rw.slba);
	sector <<= (req->ns->blksize_shift - 9);

	nvmet_inline_bio_init(req);
	bio = &req->inline_bio;
	bio->bi_bdev = req->ns->bdev;
	bio->bi_iter.bi_sector = sector;
	bio->bi_private = req;
	bio->bi_end_io = nvmet_bio_done;
	bio_set_op_attrs(bio, op, op_flags);

	for_each_sg(req->sg, sg, req->sg_cnt, i) {
		while (bio_add_page(bio, sg_page(sg), sg->length, sg->offset)
				!= sg->length) {
			struct bio *prev = bio;

			bio = bio_alloc(GFP_KERNEL, min(sg_cnt, BIO_MAX_PAGES));
			bio->bi_bdev = req->ns->bdev;
			bio->bi_iter.bi_sector = sector;
			bio_set_op_attrs(bio, op, op_flags);

			bio_chain(bio, prev);
			cookie = submit_bio(prev);
		}

		sector += sg->length >> 9;
		sg_cnt--;
	}

	cookie = submit_bio(bio);

	blk_poll(bdev_get_queue(req->ns->bdev), cookie);
}

static void nvmet_execute_flush(struct nvmet_req *req)
{
	struct bio *bio;

	nvmet_inline_bio_init(req);
	bio = &req->inline_bio;

	bio->bi_bdev = req->ns->bdev;
	bio->bi_private = req;
	bio->bi_end_io = nvmet_bio_done;
	bio_set_op_attrs(bio, REQ_OP_WRITE, WRITE_FLUSH);

	submit_bio(bio);
}

static u16 nvmet_discard_range(struct nvmet_ns *ns,
		struct nvme_dsm_range *range, struct bio **bio)
{
	if (__blkdev_issue_discard(ns->bdev,
			le64_to_cpu(range->slba) << (ns->blksize_shift - 9),
			le32_to_cpu(range->nlb) << (ns->blksize_shift - 9),
			GFP_KERNEL, 0, bio))
		return NVME_SC_INTERNAL | NVME_SC_DNR;
	return 0;
}

static void nvmet_execute_discard(struct nvmet_req *req)
{
	struct nvme_dsm_range range;
	struct bio *bio = NULL;
	int i;
	u16 status;

	for (i = 0; i <= le32_to_cpu(req->cmd->dsm.nr); i++) {
		status = nvmet_copy_from_sgl(req, i * sizeof(range), &range,
				sizeof(range));
		if (status)
			break;

		status = nvmet_discard_range(req->ns, &range, &bio);
		if (status)
			break;
	}

	if (bio) {
		bio->bi_private = req;
		bio->bi_end_io = nvmet_bio_done;
		if (status) {
			bio->bi_error = -EIO;
			bio_endio(bio);
		} else {
			submit_bio(bio);
		}
	} else {
		nvmet_req_complete(req, status);
	}
}

static void nvmet_execute_dsm(struct nvmet_req *req)
{
	switch (le32_to_cpu(req->cmd->dsm.attributes)) {
	case NVME_DSMGMT_AD:
		nvmet_execute_discard(req);
		return;
	case NVME_DSMGMT_IDR:
	case NVME_DSMGMT_IDW:
	default:
		/* Not supported yet */
		nvmet_req_complete(req, 0);
		return;
	}
}

int nvmet_parse_io_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	if (unlikely(!(req->sq->ctrl->cc & NVME_CC_ENABLE))) {
		pr_err("nvmet: got io cmd %d while CC.EN == 0\n",
				cmd->common.opcode);
		req->ns = NULL;
		return NVME_SC_CMD_SEQ_ERROR | NVME_SC_DNR;
	}

	if (unlikely(!(req->sq->ctrl->csts & NVME_CSTS_RDY))) {
		pr_err("nvmet: got io cmd %d while CSTS.RDY == 0\n",
				cmd->common.opcode);
		req->ns = NULL;
		return NVME_SC_CMD_SEQ_ERROR | NVME_SC_DNR;
	}

	req->ns = nvmet_find_namespace(req->sq->ctrl, cmd->rw.nsid);
	if (!req->ns)
		return NVME_SC_INVALID_NS | NVME_SC_DNR;

	switch (cmd->common.opcode) {
	case nvme_cmd_read:
	case nvme_cmd_write:
		req->execute = nvmet_execute_rw;
		req->data_len = nvmet_rw_len(req);
		return 0;
	case nvme_cmd_flush:
		req->execute = nvmet_execute_flush;
		req->data_len = 0;
		return 0;
	case nvme_cmd_dsm:
		req->execute = nvmet_execute_dsm;
		req->data_len = le32_to_cpu(cmd->dsm.nr + 1) *
			sizeof(struct nvme_dsm_range);
		return 0;
	default:
		pr_err("nvmet: unhandled cmd %d\n", cmd->common.opcode);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}
}

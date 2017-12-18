/*
 * NVMe Fabrics command implementation.
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
#include "nvmet.h"

static void nvmet_execute_prop_set(struct nvmet_req *req)
{
	u16 status = 0;

	if (!(req->cmd->prop_set.attrib & 1)) {
		u64 val = le64_to_cpu(req->cmd->prop_set.value);

		switch (le32_to_cpu(req->cmd->prop_set.offset)) {
		case NVME_REG_CC:
			nvmet_update_cc(req->sq->ctrl, val);
			break;
		default:
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			break;
		}
	} else {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	nvmet_req_complete(req, status);
}

static void nvmet_execute_prop_get(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	u16 status = 0;
	u64 val = 0;

	if (req->cmd->prop_get.attrib & 1) {
		switch (le32_to_cpu(req->cmd->prop_get.offset)) {
		case NVME_REG_CAP:
			val = ctrl->cap;
			break;
		default:
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			break;
		}
	} else {
		switch (le32_to_cpu(req->cmd->prop_get.offset)) {
		case NVME_REG_VS:
			val = ctrl->subsys->ver;
			break;
		case NVME_REG_CC:
			val = ctrl->cc;
			break;
		case NVME_REG_CSTS:
			val = ctrl->csts;
			break;
		default:
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			break;
		}
	}

	req->rsp->result64 = cpu_to_le64(val);
	nvmet_req_complete(req, status);
}

int nvmet_parse_fabrics_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	req->ns = NULL;

	switch (cmd->fabrics.fctype) {
	case nvme_fabrics_type_property_set:
		req->data_len = 0;
		req->execute = nvmet_execute_prop_set;
		break;
	case nvme_fabrics_type_property_get:
		req->data_len = 0;
		req->execute = nvmet_execute_prop_get;
		break;
	default:
		pr_err("received unknown capsule type 0x%x\n",
			cmd->fabrics.fctype);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

	return 0;
}

static u16 nvmet_install_queue(struct nvmet_ctrl *ctrl, struct nvmet_req *req)
{
	struct nvmf_connect_command *c = &req->cmd->connect;
	u16 qid = le16_to_cpu(c->qid);
	u16 sqsize = le16_to_cpu(c->sqsize);
	struct nvmet_ctrl *old;

	old = cmpxchg(&req->sq->ctrl, NULL, ctrl);
	if (old) {
		pr_warn("queue already connected!\n");
		return NVME_SC_CONNECT_CTRL_BUSY | NVME_SC_DNR;
	}

	nvmet_cq_setup(ctrl, req->cq, qid, sqsize);
	nvmet_sq_setup(ctrl, req->sq, qid, sqsize);
	return 0;
}

static void nvmet_execute_admin_connect(struct nvmet_req *req)
{
	struct nvmf_connect_command *c = &req->cmd->connect;
	struct nvmf_connect_data *d;
	struct nvmet_ctrl *ctrl = NULL;
	u16 status = 0;

	d = kmap(sg_page(req->sg)) + req->sg->offset;

	/* zero out initial completion result, assign values as needed */
	req->rsp->result = 0;

	if (c->recfmt != 0) {
		pr_warn("invalid connect version (%d).\n",
			le16_to_cpu(c->recfmt));
		status = NVME_SC_CONNECT_FORMAT | NVME_SC_DNR;
		goto out;
	}

	if (unlikely(d->cntlid != cpu_to_le16(0xffff))) {
		pr_warn("connect attempt for invalid controller ID %#x\n",
			d->cntlid);
		status = NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;
		req->rsp->result = IPO_IATTR_CONNECT_DATA(cntlid);
		goto out;
	}

	status = nvmet_alloc_ctrl(d->subsysnqn, d->hostnqn, req,
			le32_to_cpu(c->kato), &ctrl);
	if (status)
		goto out;

	status = nvmet_install_queue(ctrl, req);
	if (status) {
		nvmet_ctrl_put(ctrl);
		goto out;
	}

	pr_info("creating controller %d for NQN %s.\n",
			ctrl->cntlid, ctrl->hostnqn);
	req->rsp->result16 = cpu_to_le16(ctrl->cntlid);

out:
	kunmap(sg_page(req->sg));
	nvmet_req_complete(req, status);
}

static void nvmet_execute_io_connect(struct nvmet_req *req)
{
	struct nvmf_connect_command *c = &req->cmd->connect;
	struct nvmf_connect_data *d;
	struct nvmet_ctrl *ctrl = NULL;
	u16 qid = le16_to_cpu(c->qid);
	u16 status = 0;

	d = kmap(sg_page(req->sg)) + req->sg->offset;

	/* zero out initial completion result, assign values as needed */
	req->rsp->result = 0;

	if (c->recfmt != 0) {
		pr_warn("invalid connect version (%d).\n",
			le16_to_cpu(c->recfmt));
		status = NVME_SC_CONNECT_FORMAT | NVME_SC_DNR;
		goto out;
	}

	status = nvmet_ctrl_find_get(d->subsysnqn, d->hostnqn,
			le16_to_cpu(d->cntlid),
			req, &ctrl);
	if (status)
		goto out;

	if (unlikely(qid > ctrl->subsys->max_qid)) {
		pr_warn("invalid queue id (%d)\n", qid);
		status = NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;
		req->rsp->result = IPO_IATTR_CONNECT_SQE(qid);
		goto out_ctrl_put;
	}

	status = nvmet_install_queue(ctrl, req);
	if (status) {
		/* pass back cntlid that had the issue of installing queue */
		req->rsp->result16 = cpu_to_le16(ctrl->cntlid);
		goto out_ctrl_put;
	}

	pr_info("adding queue %d to ctrl %d.\n", qid, ctrl->cntlid);

out:
	kunmap(sg_page(req->sg));
	nvmet_req_complete(req, status);
	return;

out_ctrl_put:
	nvmet_ctrl_put(ctrl);
	goto out;
}

int nvmet_parse_connect_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	req->ns = NULL;

	if (req->cmd->common.opcode != nvme_fabrics_command) {
		pr_err("invalid command 0x%x on unconnected queue.\n",
			cmd->fabrics.opcode);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}
	if (cmd->fabrics.fctype != nvme_fabrics_type_connect) {
		pr_err("invalid capsule type 0x%x on unconnected queue.\n",
			cmd->fabrics.fctype);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

	req->data_len = sizeof(struct nvmf_connect_data);
	if (cmd->connect.qid == 0)
		req->execute = nvmet_execute_admin_connect;
	else
		req->execute = nvmet_execute_io_connect;
	return 0;
}

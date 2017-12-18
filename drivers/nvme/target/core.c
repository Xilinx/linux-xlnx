/*
 * Common code for the NVMe target.
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
#include <linux/module.h>
#include <linux/random.h>
#include "nvmet.h"

static struct nvmet_fabrics_ops *nvmet_transports[NVMF_TRTYPE_MAX];

/*
 * This read/write semaphore is used to synchronize access to configuration
 * information on a target system that will result in discovery log page
 * information change for at least one host.
 * The full list of resources to protected by this semaphore is:
 *
 *  - subsystems list
 *  - per-subsystem allowed hosts list
 *  - allow_any_host subsystem attribute
 *  - nvmet_genctr
 *  - the nvmet_transports array
 *
 * When updating any of those lists/structures write lock should be obtained,
 * while when reading (popolating discovery log page or checking host-subsystem
 * link) read lock is obtained to allow concurrent reads.
 */
DECLARE_RWSEM(nvmet_config_sem);

static struct nvmet_subsys *nvmet_find_get_subsys(struct nvmet_port *port,
		const char *subsysnqn);

u16 nvmet_copy_to_sgl(struct nvmet_req *req, off_t off, const void *buf,
		size_t len)
{
	if (sg_pcopy_from_buffer(req->sg, req->sg_cnt, buf, len, off) != len)
		return NVME_SC_SGL_INVALID_DATA | NVME_SC_DNR;
	return 0;
}

u16 nvmet_copy_from_sgl(struct nvmet_req *req, off_t off, void *buf, size_t len)
{
	if (sg_pcopy_to_buffer(req->sg, req->sg_cnt, buf, len, off) != len)
		return NVME_SC_SGL_INVALID_DATA | NVME_SC_DNR;
	return 0;
}

static u32 nvmet_async_event_result(struct nvmet_async_event *aen)
{
	return aen->event_type | (aen->event_info << 8) | (aen->log_page << 16);
}

static void nvmet_async_events_free(struct nvmet_ctrl *ctrl)
{
	struct nvmet_req *req;

	while (1) {
		mutex_lock(&ctrl->lock);
		if (!ctrl->nr_async_event_cmds) {
			mutex_unlock(&ctrl->lock);
			return;
		}

		req = ctrl->async_event_cmds[--ctrl->nr_async_event_cmds];
		mutex_unlock(&ctrl->lock);
		nvmet_req_complete(req, NVME_SC_INTERNAL | NVME_SC_DNR);
	}
}

static void nvmet_async_event_work(struct work_struct *work)
{
	struct nvmet_ctrl *ctrl =
		container_of(work, struct nvmet_ctrl, async_event_work);
	struct nvmet_async_event *aen;
	struct nvmet_req *req;

	while (1) {
		mutex_lock(&ctrl->lock);
		aen = list_first_entry_or_null(&ctrl->async_events,
				struct nvmet_async_event, entry);
		if (!aen || !ctrl->nr_async_event_cmds) {
			mutex_unlock(&ctrl->lock);
			return;
		}

		req = ctrl->async_event_cmds[--ctrl->nr_async_event_cmds];
		nvmet_set_result(req, nvmet_async_event_result(aen));

		list_del(&aen->entry);
		kfree(aen);

		mutex_unlock(&ctrl->lock);
		nvmet_req_complete(req, 0);
	}
}

static void nvmet_add_async_event(struct nvmet_ctrl *ctrl, u8 event_type,
		u8 event_info, u8 log_page)
{
	struct nvmet_async_event *aen;

	aen = kmalloc(sizeof(*aen), GFP_KERNEL);
	if (!aen)
		return;

	aen->event_type = event_type;
	aen->event_info = event_info;
	aen->log_page = log_page;

	mutex_lock(&ctrl->lock);
	list_add_tail(&aen->entry, &ctrl->async_events);
	mutex_unlock(&ctrl->lock);

	schedule_work(&ctrl->async_event_work);
}

int nvmet_register_transport(struct nvmet_fabrics_ops *ops)
{
	int ret = 0;

	down_write(&nvmet_config_sem);
	if (nvmet_transports[ops->type])
		ret = -EINVAL;
	else
		nvmet_transports[ops->type] = ops;
	up_write(&nvmet_config_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(nvmet_register_transport);

void nvmet_unregister_transport(struct nvmet_fabrics_ops *ops)
{
	down_write(&nvmet_config_sem);
	nvmet_transports[ops->type] = NULL;
	up_write(&nvmet_config_sem);
}
EXPORT_SYMBOL_GPL(nvmet_unregister_transport);

int nvmet_enable_port(struct nvmet_port *port)
{
	struct nvmet_fabrics_ops *ops;
	int ret;

	lockdep_assert_held(&nvmet_config_sem);

	ops = nvmet_transports[port->disc_addr.trtype];
	if (!ops) {
		up_write(&nvmet_config_sem);
		request_module("nvmet-transport-%d", port->disc_addr.trtype);
		down_write(&nvmet_config_sem);
		ops = nvmet_transports[port->disc_addr.trtype];
		if (!ops) {
			pr_err("transport type %d not supported\n",
				port->disc_addr.trtype);
			return -EINVAL;
		}
	}

	if (!try_module_get(ops->owner))
		return -EINVAL;

	ret = ops->add_port(port);
	if (ret) {
		module_put(ops->owner);
		return ret;
	}

	port->enabled = true;
	return 0;
}

void nvmet_disable_port(struct nvmet_port *port)
{
	struct nvmet_fabrics_ops *ops;

	lockdep_assert_held(&nvmet_config_sem);

	port->enabled = false;

	ops = nvmet_transports[port->disc_addr.trtype];
	ops->remove_port(port);
	module_put(ops->owner);
}

static void nvmet_keep_alive_timer(struct work_struct *work)
{
	struct nvmet_ctrl *ctrl = container_of(to_delayed_work(work),
			struct nvmet_ctrl, ka_work);

	pr_err("ctrl %d keep-alive timer (%d seconds) expired!\n",
		ctrl->cntlid, ctrl->kato);

	ctrl->ops->delete_ctrl(ctrl);
}

static void nvmet_start_keep_alive_timer(struct nvmet_ctrl *ctrl)
{
	pr_debug("ctrl %d start keep-alive timer for %d secs\n",
		ctrl->cntlid, ctrl->kato);

	INIT_DELAYED_WORK(&ctrl->ka_work, nvmet_keep_alive_timer);
	schedule_delayed_work(&ctrl->ka_work, ctrl->kato * HZ);
}

static void nvmet_stop_keep_alive_timer(struct nvmet_ctrl *ctrl)
{
	pr_debug("ctrl %d stop keep-alive\n", ctrl->cntlid);

	cancel_delayed_work_sync(&ctrl->ka_work);
}

static struct nvmet_ns *__nvmet_find_namespace(struct nvmet_ctrl *ctrl,
		__le32 nsid)
{
	struct nvmet_ns *ns;

	list_for_each_entry_rcu(ns, &ctrl->subsys->namespaces, dev_link) {
		if (ns->nsid == le32_to_cpu(nsid))
			return ns;
	}

	return NULL;
}

struct nvmet_ns *nvmet_find_namespace(struct nvmet_ctrl *ctrl, __le32 nsid)
{
	struct nvmet_ns *ns;

	rcu_read_lock();
	ns = __nvmet_find_namespace(ctrl, nsid);
	if (ns)
		percpu_ref_get(&ns->ref);
	rcu_read_unlock();

	return ns;
}

static void nvmet_destroy_namespace(struct percpu_ref *ref)
{
	struct nvmet_ns *ns = container_of(ref, struct nvmet_ns, ref);

	complete(&ns->disable_done);
}

void nvmet_put_namespace(struct nvmet_ns *ns)
{
	percpu_ref_put(&ns->ref);
}

int nvmet_ns_enable(struct nvmet_ns *ns)
{
	struct nvmet_subsys *subsys = ns->subsys;
	struct nvmet_ctrl *ctrl;
	int ret = 0;

	mutex_lock(&subsys->lock);
	if (!list_empty(&ns->dev_link))
		goto out_unlock;

	ns->bdev = blkdev_get_by_path(ns->device_path, FMODE_READ | FMODE_WRITE,
			NULL);
	if (IS_ERR(ns->bdev)) {
		pr_err("nvmet: failed to open block device %s: (%ld)\n",
			ns->device_path, PTR_ERR(ns->bdev));
		ret = PTR_ERR(ns->bdev);
		ns->bdev = NULL;
		goto out_unlock;
	}

	ns->size = i_size_read(ns->bdev->bd_inode);
	ns->blksize_shift = blksize_bits(bdev_logical_block_size(ns->bdev));

	ret = percpu_ref_init(&ns->ref, nvmet_destroy_namespace,
				0, GFP_KERNEL);
	if (ret)
		goto out_blkdev_put;

	if (ns->nsid > subsys->max_nsid)
		subsys->max_nsid = ns->nsid;

	/*
	 * The namespaces list needs to be sorted to simplify the implementation
	 * of the Identify Namepace List subcommand.
	 */
	if (list_empty(&subsys->namespaces)) {
		list_add_tail_rcu(&ns->dev_link, &subsys->namespaces);
	} else {
		struct nvmet_ns *old;

		list_for_each_entry_rcu(old, &subsys->namespaces, dev_link) {
			BUG_ON(ns->nsid == old->nsid);
			if (ns->nsid < old->nsid)
				break;
		}

		list_add_tail_rcu(&ns->dev_link, &old->dev_link);
	}

	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry)
		nvmet_add_async_event(ctrl, NVME_AER_TYPE_NOTICE, 0, 0);

	ret = 0;
out_unlock:
	mutex_unlock(&subsys->lock);
	return ret;
out_blkdev_put:
	blkdev_put(ns->bdev, FMODE_WRITE|FMODE_READ);
	ns->bdev = NULL;
	goto out_unlock;
}

void nvmet_ns_disable(struct nvmet_ns *ns)
{
	struct nvmet_subsys *subsys = ns->subsys;
	struct nvmet_ctrl *ctrl;

	mutex_lock(&subsys->lock);
	if (list_empty(&ns->dev_link)) {
		mutex_unlock(&subsys->lock);
		return;
	}
	list_del_init(&ns->dev_link);
	mutex_unlock(&subsys->lock);

	/*
	 * Now that we removed the namespaces from the lookup list, we
	 * can kill the per_cpu ref and wait for any remaining references
	 * to be dropped, as well as a RCU grace period for anyone only
	 * using the namepace under rcu_read_lock().  Note that we can't
	 * use call_rcu here as we need to ensure the namespaces have
	 * been fully destroyed before unloading the module.
	 */
	percpu_ref_kill(&ns->ref);
	synchronize_rcu();
	wait_for_completion(&ns->disable_done);
	percpu_ref_exit(&ns->ref);

	mutex_lock(&subsys->lock);
	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry)
		nvmet_add_async_event(ctrl, NVME_AER_TYPE_NOTICE, 0, 0);

	if (ns->bdev)
		blkdev_put(ns->bdev, FMODE_WRITE|FMODE_READ);
	mutex_unlock(&subsys->lock);
}

void nvmet_ns_free(struct nvmet_ns *ns)
{
	nvmet_ns_disable(ns);

	kfree(ns->device_path);
	kfree(ns);
}

struct nvmet_ns *nvmet_ns_alloc(struct nvmet_subsys *subsys, u32 nsid)
{
	struct nvmet_ns *ns;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return NULL;

	INIT_LIST_HEAD(&ns->dev_link);
	init_completion(&ns->disable_done);

	ns->nsid = nsid;
	ns->subsys = subsys;

	return ns;
}

static void __nvmet_req_complete(struct nvmet_req *req, u16 status)
{
	if (status)
		nvmet_set_status(req, status);

	/* XXX: need to fill in something useful for sq_head */
	req->rsp->sq_head = 0;
	if (likely(req->sq)) /* may happen during early failure */
		req->rsp->sq_id = cpu_to_le16(req->sq->qid);
	req->rsp->command_id = req->cmd->common.command_id;

	if (req->ns)
		nvmet_put_namespace(req->ns);
	req->ops->queue_response(req);
}

void nvmet_req_complete(struct nvmet_req *req, u16 status)
{
	__nvmet_req_complete(req, status);
	percpu_ref_put(&req->sq->ref);
}
EXPORT_SYMBOL_GPL(nvmet_req_complete);

void nvmet_cq_setup(struct nvmet_ctrl *ctrl, struct nvmet_cq *cq,
		u16 qid, u16 size)
{
	cq->qid = qid;
	cq->size = size;

	ctrl->cqs[qid] = cq;
}

void nvmet_sq_setup(struct nvmet_ctrl *ctrl, struct nvmet_sq *sq,
		u16 qid, u16 size)
{
	sq->qid = qid;
	sq->size = size;

	ctrl->sqs[qid] = sq;
}

void nvmet_sq_destroy(struct nvmet_sq *sq)
{
	/*
	 * If this is the admin queue, complete all AERs so that our
	 * queue doesn't have outstanding requests on it.
	 */
	if (sq->ctrl && sq->ctrl->sqs && sq->ctrl->sqs[0] == sq)
		nvmet_async_events_free(sq->ctrl);
	percpu_ref_kill(&sq->ref);
	wait_for_completion(&sq->free_done);
	percpu_ref_exit(&sq->ref);

	if (sq->ctrl) {
		nvmet_ctrl_put(sq->ctrl);
		sq->ctrl = NULL; /* allows reusing the queue later */
	}
}
EXPORT_SYMBOL_GPL(nvmet_sq_destroy);

static void nvmet_sq_free(struct percpu_ref *ref)
{
	struct nvmet_sq *sq = container_of(ref, struct nvmet_sq, ref);

	complete(&sq->free_done);
}

int nvmet_sq_init(struct nvmet_sq *sq)
{
	int ret;

	ret = percpu_ref_init(&sq->ref, nvmet_sq_free, 0, GFP_KERNEL);
	if (ret) {
		pr_err("percpu_ref init failed!\n");
		return ret;
	}
	init_completion(&sq->free_done);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmet_sq_init);

bool nvmet_req_init(struct nvmet_req *req, struct nvmet_cq *cq,
		struct nvmet_sq *sq, struct nvmet_fabrics_ops *ops)
{
	u8 flags = req->cmd->common.flags;
	u16 status;

	req->cq = cq;
	req->sq = sq;
	req->ops = ops;
	req->sg = NULL;
	req->sg_cnt = 0;
	req->rsp->status = 0;

	/* no support for fused commands yet */
	if (unlikely(flags & (NVME_CMD_FUSE_FIRST | NVME_CMD_FUSE_SECOND))) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto fail;
	}

	/* either variant of SGLs is fine, as we don't support metadata */
	if (unlikely((flags & NVME_CMD_SGL_ALL) != NVME_CMD_SGL_METABUF &&
		     (flags & NVME_CMD_SGL_ALL) != NVME_CMD_SGL_METASEG)) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto fail;
	}

	if (unlikely(!req->sq->ctrl))
		/* will return an error for any Non-connect command: */
		status = nvmet_parse_connect_cmd(req);
	else if (likely(req->sq->qid != 0))
		status = nvmet_parse_io_cmd(req);
	else if (req->cmd->common.opcode == nvme_fabrics_command)
		status = nvmet_parse_fabrics_cmd(req);
	else if (req->sq->ctrl->subsys->type == NVME_NQN_DISC)
		status = nvmet_parse_discovery_cmd(req);
	else
		status = nvmet_parse_admin_cmd(req);

	if (status)
		goto fail;

	if (unlikely(!percpu_ref_tryget_live(&sq->ref))) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto fail;
	}

	return true;

fail:
	__nvmet_req_complete(req, status);
	return false;
}
EXPORT_SYMBOL_GPL(nvmet_req_init);

static inline bool nvmet_cc_en(u32 cc)
{
	return cc & 0x1;
}

static inline u8 nvmet_cc_css(u32 cc)
{
	return (cc >> 4) & 0x7;
}

static inline u8 nvmet_cc_mps(u32 cc)
{
	return (cc >> 7) & 0xf;
}

static inline u8 nvmet_cc_ams(u32 cc)
{
	return (cc >> 11) & 0x7;
}

static inline u8 nvmet_cc_shn(u32 cc)
{
	return (cc >> 14) & 0x3;
}

static inline u8 nvmet_cc_iosqes(u32 cc)
{
	return (cc >> 16) & 0xf;
}

static inline u8 nvmet_cc_iocqes(u32 cc)
{
	return (cc >> 20) & 0xf;
}

static void nvmet_start_ctrl(struct nvmet_ctrl *ctrl)
{
	lockdep_assert_held(&ctrl->lock);

	if (nvmet_cc_iosqes(ctrl->cc) != NVME_NVM_IOSQES ||
	    nvmet_cc_iocqes(ctrl->cc) != NVME_NVM_IOCQES ||
	    nvmet_cc_mps(ctrl->cc) != 0 ||
	    nvmet_cc_ams(ctrl->cc) != 0 ||
	    nvmet_cc_css(ctrl->cc) != 0) {
		ctrl->csts = NVME_CSTS_CFS;
		return;
	}

	ctrl->csts = NVME_CSTS_RDY;
}

static void nvmet_clear_ctrl(struct nvmet_ctrl *ctrl)
{
	lockdep_assert_held(&ctrl->lock);

	/* XXX: tear down queues? */
	ctrl->csts &= ~NVME_CSTS_RDY;
	ctrl->cc = 0;
}

void nvmet_update_cc(struct nvmet_ctrl *ctrl, u32 new)
{
	u32 old;

	mutex_lock(&ctrl->lock);
	old = ctrl->cc;
	ctrl->cc = new;

	if (nvmet_cc_en(new) && !nvmet_cc_en(old))
		nvmet_start_ctrl(ctrl);
	if (!nvmet_cc_en(new) && nvmet_cc_en(old))
		nvmet_clear_ctrl(ctrl);
	if (nvmet_cc_shn(new) && !nvmet_cc_shn(old)) {
		nvmet_clear_ctrl(ctrl);
		ctrl->csts |= NVME_CSTS_SHST_CMPLT;
	}
	if (!nvmet_cc_shn(new) && nvmet_cc_shn(old))
		ctrl->csts &= ~NVME_CSTS_SHST_CMPLT;
	mutex_unlock(&ctrl->lock);
}

static void nvmet_init_cap(struct nvmet_ctrl *ctrl)
{
	/* command sets supported: NVMe command set: */
	ctrl->cap = (1ULL << 37);
	/* CC.EN timeout in 500msec units: */
	ctrl->cap |= (15ULL << 24);
	/* maximum queue entries supported: */
	ctrl->cap |= NVMET_QUEUE_SIZE - 1;
}

u16 nvmet_ctrl_find_get(const char *subsysnqn, const char *hostnqn, u16 cntlid,
		struct nvmet_req *req, struct nvmet_ctrl **ret)
{
	struct nvmet_subsys *subsys;
	struct nvmet_ctrl *ctrl;
	u16 status = 0;

	subsys = nvmet_find_get_subsys(req->port, subsysnqn);
	if (!subsys) {
		pr_warn("connect request for invalid subsystem %s!\n",
			subsysnqn);
		req->rsp->result = IPO_IATTR_CONNECT_DATA(subsysnqn);
		return NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;
	}

	mutex_lock(&subsys->lock);
	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry) {
		if (ctrl->cntlid == cntlid) {
			if (strncmp(hostnqn, ctrl->hostnqn, NVMF_NQN_SIZE)) {
				pr_warn("hostnqn mismatch.\n");
				continue;
			}
			if (!kref_get_unless_zero(&ctrl->ref))
				continue;

			*ret = ctrl;
			goto out;
		}
	}

	pr_warn("could not find controller %d for subsys %s / host %s\n",
		cntlid, subsysnqn, hostnqn);
	req->rsp->result = IPO_IATTR_CONNECT_DATA(cntlid);
	status = NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;

out:
	mutex_unlock(&subsys->lock);
	nvmet_subsys_put(subsys);
	return status;
}

static bool __nvmet_host_allowed(struct nvmet_subsys *subsys,
		const char *hostnqn)
{
	struct nvmet_host_link *p;

	if (subsys->allow_any_host)
		return true;

	list_for_each_entry(p, &subsys->hosts, entry) {
		if (!strcmp(nvmet_host_name(p->host), hostnqn))
			return true;
	}

	return false;
}

static bool nvmet_host_discovery_allowed(struct nvmet_req *req,
		const char *hostnqn)
{
	struct nvmet_subsys_link *s;

	list_for_each_entry(s, &req->port->subsystems, entry) {
		if (__nvmet_host_allowed(s->subsys, hostnqn))
			return true;
	}

	return false;
}

bool nvmet_host_allowed(struct nvmet_req *req, struct nvmet_subsys *subsys,
		const char *hostnqn)
{
	lockdep_assert_held(&nvmet_config_sem);

	if (subsys->type == NVME_NQN_DISC)
		return nvmet_host_discovery_allowed(req, hostnqn);
	else
		return __nvmet_host_allowed(subsys, hostnqn);
}

u16 nvmet_alloc_ctrl(const char *subsysnqn, const char *hostnqn,
		struct nvmet_req *req, u32 kato, struct nvmet_ctrl **ctrlp)
{
	struct nvmet_subsys *subsys;
	struct nvmet_ctrl *ctrl;
	int ret;
	u16 status;

	status = NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;
	subsys = nvmet_find_get_subsys(req->port, subsysnqn);
	if (!subsys) {
		pr_warn("connect request for invalid subsystem %s!\n",
			subsysnqn);
		req->rsp->result = IPO_IATTR_CONNECT_DATA(subsysnqn);
		goto out;
	}

	status = NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;
	down_read(&nvmet_config_sem);
	if (!nvmet_host_allowed(req, subsys, hostnqn)) {
		pr_info("connect by host %s for subsystem %s not allowed\n",
			hostnqn, subsysnqn);
		req->rsp->result = IPO_IATTR_CONNECT_DATA(hostnqn);
		up_read(&nvmet_config_sem);
		goto out_put_subsystem;
	}
	up_read(&nvmet_config_sem);

	status = NVME_SC_INTERNAL;
	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		goto out_put_subsystem;
	mutex_init(&ctrl->lock);

	nvmet_init_cap(ctrl);

	INIT_WORK(&ctrl->async_event_work, nvmet_async_event_work);
	INIT_LIST_HEAD(&ctrl->async_events);

	memcpy(ctrl->subsysnqn, subsysnqn, NVMF_NQN_SIZE);
	memcpy(ctrl->hostnqn, hostnqn, NVMF_NQN_SIZE);

	/* generate a random serial number as our controllers are ephemeral: */
	get_random_bytes(&ctrl->serial, sizeof(ctrl->serial));

	kref_init(&ctrl->ref);
	ctrl->subsys = subsys;

	ctrl->cqs = kcalloc(subsys->max_qid + 1,
			sizeof(struct nvmet_cq *),
			GFP_KERNEL);
	if (!ctrl->cqs)
		goto out_free_ctrl;

	ctrl->sqs = kcalloc(subsys->max_qid + 1,
			sizeof(struct nvmet_sq *),
			GFP_KERNEL);
	if (!ctrl->sqs)
		goto out_free_cqs;

	ret = ida_simple_get(&subsys->cntlid_ida,
			     NVME_CNTLID_MIN, NVME_CNTLID_MAX,
			     GFP_KERNEL);
	if (ret < 0) {
		status = NVME_SC_CONNECT_CTRL_BUSY | NVME_SC_DNR;
		goto out_free_sqs;
	}
	ctrl->cntlid = ret;

	ctrl->ops = req->ops;
	if (ctrl->subsys->type == NVME_NQN_DISC) {
		/* Don't accept keep-alive timeout for discovery controllers */
		if (kato) {
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			goto out_free_sqs;
		}

		/*
		 * Discovery controllers use some arbitrary high value in order
		 * to cleanup stale discovery sessions
		 *
		 * From the latest base diff RC:
		 * "The Keep Alive command is not supported by
		 * Discovery controllers. A transport may specify a
		 * fixed Discovery controller activity timeout value
		 * (e.g., 2 minutes).  If no commands are received
		 * by a Discovery controller within that time
		 * period, the controller may perform the
		 * actions for Keep Alive Timer expiration".
		 */
		ctrl->kato = NVMET_DISC_KATO;
	} else {
		/* keep-alive timeout in seconds */
		ctrl->kato = DIV_ROUND_UP(kato, 1000);
	}
	nvmet_start_keep_alive_timer(ctrl);

	mutex_lock(&subsys->lock);
	list_add_tail(&ctrl->subsys_entry, &subsys->ctrls);
	mutex_unlock(&subsys->lock);

	*ctrlp = ctrl;
	return 0;

out_free_sqs:
	kfree(ctrl->sqs);
out_free_cqs:
	kfree(ctrl->cqs);
out_free_ctrl:
	kfree(ctrl);
out_put_subsystem:
	nvmet_subsys_put(subsys);
out:
	return status;
}

static void nvmet_ctrl_free(struct kref *ref)
{
	struct nvmet_ctrl *ctrl = container_of(ref, struct nvmet_ctrl, ref);
	struct nvmet_subsys *subsys = ctrl->subsys;

	nvmet_stop_keep_alive_timer(ctrl);

	mutex_lock(&subsys->lock);
	list_del(&ctrl->subsys_entry);
	mutex_unlock(&subsys->lock);

	ida_simple_remove(&subsys->cntlid_ida, ctrl->cntlid);
	nvmet_subsys_put(subsys);

	kfree(ctrl->sqs);
	kfree(ctrl->cqs);
	kfree(ctrl);
}

void nvmet_ctrl_put(struct nvmet_ctrl *ctrl)
{
	kref_put(&ctrl->ref, nvmet_ctrl_free);
}

static void nvmet_fatal_error_handler(struct work_struct *work)
{
	struct nvmet_ctrl *ctrl =
			container_of(work, struct nvmet_ctrl, fatal_err_work);

	pr_err("ctrl %d fatal error occurred!\n", ctrl->cntlid);
	ctrl->ops->delete_ctrl(ctrl);
}

void nvmet_ctrl_fatal_error(struct nvmet_ctrl *ctrl)
{
	mutex_lock(&ctrl->lock);
	if (!(ctrl->csts & NVME_CSTS_CFS)) {
		ctrl->csts |= NVME_CSTS_CFS;
		INIT_WORK(&ctrl->fatal_err_work, nvmet_fatal_error_handler);
		schedule_work(&ctrl->fatal_err_work);
	}
	mutex_unlock(&ctrl->lock);
}
EXPORT_SYMBOL_GPL(nvmet_ctrl_fatal_error);

static struct nvmet_subsys *nvmet_find_get_subsys(struct nvmet_port *port,
		const char *subsysnqn)
{
	struct nvmet_subsys_link *p;

	if (!port)
		return NULL;

	if (!strncmp(NVME_DISC_SUBSYS_NAME, subsysnqn,
			NVMF_NQN_SIZE)) {
		if (!kref_get_unless_zero(&nvmet_disc_subsys->ref))
			return NULL;
		return nvmet_disc_subsys;
	}

	down_read(&nvmet_config_sem);
	list_for_each_entry(p, &port->subsystems, entry) {
		if (!strncmp(p->subsys->subsysnqn, subsysnqn,
				NVMF_NQN_SIZE)) {
			if (!kref_get_unless_zero(&p->subsys->ref))
				break;
			up_read(&nvmet_config_sem);
			return p->subsys;
		}
	}
	up_read(&nvmet_config_sem);
	return NULL;
}

struct nvmet_subsys *nvmet_subsys_alloc(const char *subsysnqn,
		enum nvme_subsys_type type)
{
	struct nvmet_subsys *subsys;

	subsys = kzalloc(sizeof(*subsys), GFP_KERNEL);
	if (!subsys)
		return NULL;

	subsys->ver = NVME_VS(1, 2, 1); /* NVMe 1.2.1 */

	switch (type) {
	case NVME_NQN_NVME:
		subsys->max_qid = NVMET_NR_QUEUES;
		break;
	case NVME_NQN_DISC:
		subsys->max_qid = 0;
		break;
	default:
		pr_err("%s: Unknown Subsystem type - %d\n", __func__, type);
		kfree(subsys);
		return NULL;
	}
	subsys->type = type;
	subsys->subsysnqn = kstrndup(subsysnqn, NVMF_NQN_SIZE,
			GFP_KERNEL);
	if (!subsys->subsysnqn) {
		kfree(subsys);
		return NULL;
	}

	kref_init(&subsys->ref);

	mutex_init(&subsys->lock);
	INIT_LIST_HEAD(&subsys->namespaces);
	INIT_LIST_HEAD(&subsys->ctrls);

	ida_init(&subsys->cntlid_ida);

	INIT_LIST_HEAD(&subsys->hosts);

	return subsys;
}

static void nvmet_subsys_free(struct kref *ref)
{
	struct nvmet_subsys *subsys =
		container_of(ref, struct nvmet_subsys, ref);

	WARN_ON_ONCE(!list_empty(&subsys->namespaces));

	ida_destroy(&subsys->cntlid_ida);
	kfree(subsys->subsysnqn);
	kfree(subsys);
}

void nvmet_subsys_put(struct nvmet_subsys *subsys)
{
	kref_put(&subsys->ref, nvmet_subsys_free);
}

static int __init nvmet_init(void)
{
	int error;

	error = nvmet_init_discovery();
	if (error)
		goto out;

	error = nvmet_init_configfs();
	if (error)
		goto out_exit_discovery;
	return 0;

out_exit_discovery:
	nvmet_exit_discovery();
out:
	return error;
}

static void __exit nvmet_exit(void)
{
	nvmet_exit_configfs();
	nvmet_exit_discovery();

	BUILD_BUG_ON(sizeof(struct nvmf_disc_rsp_page_entry) != 1024);
	BUILD_BUG_ON(sizeof(struct nvmf_disc_rsp_page_hdr) != 1024);
}

module_init(nvmet_init);
module_exit(nvmet_exit);

MODULE_LICENSE("GPL v2");

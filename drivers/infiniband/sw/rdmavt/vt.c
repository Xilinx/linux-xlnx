/*
 * Copyright(c) 2016 Intel Corporation.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include "vt.h"
#include "trace.h"

#define RVT_UVERBS_ABI_VERSION 2

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("RDMA Verbs Transport Library");

static int rvt_init(void)
{
	/*
	 * rdmavt does not need to do anything special when it starts up. All it
	 * needs to do is sit and wait until a driver attempts registration.
	 */
	return 0;
}
module_init(rvt_init);

static void rvt_cleanup(void)
{
	/*
	 * Nothing to do at exit time either. The module won't be able to be
	 * removed until all drivers are gone which means all the dev structs
	 * are gone so there is really nothing to do.
	 */
}
module_exit(rvt_cleanup);

/**
 * rvt_alloc_device - allocate rdi
 * @size: how big of a structure to allocate
 * @nports: number of ports to allocate array slots for
 *
 * Use IB core device alloc to allocate space for the rdi which is assumed to be
 * inside of the ib_device. Any extra space that drivers require should be
 * included in size.
 *
 * We also allocate a port array based on the number of ports.
 *
 * Return: pointer to allocated rdi
 */
struct rvt_dev_info *rvt_alloc_device(size_t size, int nports)
{
	struct rvt_dev_info *rdi = ERR_PTR(-ENOMEM);

	rdi = (struct rvt_dev_info *)ib_alloc_device(size);
	if (!rdi)
		return rdi;

	rdi->ports = kcalloc(nports,
			     sizeof(struct rvt_ibport **),
			     GFP_KERNEL);
	if (!rdi->ports)
		ib_dealloc_device(&rdi->ibdev);

	return rdi;
}
EXPORT_SYMBOL(rvt_alloc_device);

/**
 * rvt_dealloc_device - deallocate rdi
 * @rdi: structure to free
 *
 * Free a structure allocated with rvt_alloc_device()
 */
void rvt_dealloc_device(struct rvt_dev_info *rdi)
{
	kfree(rdi->ports);
	ib_dealloc_device(&rdi->ibdev);
}
EXPORT_SYMBOL(rvt_dealloc_device);

static int rvt_query_device(struct ib_device *ibdev,
			    struct ib_device_attr *props,
			    struct ib_udata *uhw)
{
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);

	if (uhw->inlen || uhw->outlen)
		return -EINVAL;
	/*
	 * Return rvt_dev_info.dparms.props contents
	 */
	*props = rdi->dparms.props;
	return 0;
}

static int rvt_modify_device(struct ib_device *device,
			     int device_modify_mask,
			     struct ib_device_modify *device_modify)
{
	/*
	 * There is currently no need to supply this based on qib and hfi1.
	 * Future drivers may need to implement this though.
	 */

	return -EOPNOTSUPP;
}

/**
 * rvt_query_port: Passes the query port call to the driver
 * @ibdev: Verbs IB dev
 * @port_num: port number, 1 based from ib core
 * @props: structure to hold returned properties
 *
 * Return: 0 on success
 */
static int rvt_query_port(struct ib_device *ibdev, u8 port_num,
			  struct ib_port_attr *props)
{
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	struct rvt_ibport *rvp;
	int port_index = ibport_num_to_idx(ibdev, port_num);

	if (port_index < 0)
		return -EINVAL;

	rvp = rdi->ports[port_index];
	memset(props, 0, sizeof(*props));
	props->sm_lid = rvp->sm_lid;
	props->sm_sl = rvp->sm_sl;
	props->port_cap_flags = rvp->port_cap_flags;
	props->max_msg_sz = 0x80000000;
	props->pkey_tbl_len = rvt_get_npkeys(rdi);
	props->bad_pkey_cntr = rvp->pkey_violations;
	props->qkey_viol_cntr = rvp->qkey_violations;
	props->subnet_timeout = rvp->subnet_timeout;
	props->init_type_reply = 0;

	/* Populate the remaining ib_port_attr elements */
	return rdi->driver_f.query_port_state(rdi, port_num, props);
}

/**
 * rvt_modify_port
 * @ibdev: Verbs IB dev
 * @port_num: Port number, 1 based from ib core
 * @port_modify_mask: How to change the port
 * @props: Structure to fill in
 *
 * Return: 0 on success
 */
static int rvt_modify_port(struct ib_device *ibdev, u8 port_num,
			   int port_modify_mask, struct ib_port_modify *props)
{
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	struct rvt_ibport *rvp;
	int ret = 0;
	int port_index = ibport_num_to_idx(ibdev, port_num);

	if (port_index < 0)
		return -EINVAL;

	rvp = rdi->ports[port_index];
	rvp->port_cap_flags |= props->set_port_cap_mask;
	rvp->port_cap_flags &= ~props->clr_port_cap_mask;

	if (props->set_port_cap_mask || props->clr_port_cap_mask)
		rdi->driver_f.cap_mask_chg(rdi, port_num);
	if (port_modify_mask & IB_PORT_SHUTDOWN)
		ret = rdi->driver_f.shut_down_port(rdi, port_num);
	if (port_modify_mask & IB_PORT_RESET_QKEY_CNTR)
		rvp->qkey_violations = 0;

	return ret;
}

/**
 * rvt_query_pkey - Return a pkey from the table at a given index
 * @ibdev: Verbs IB dev
 * @port_num: Port number, 1 based from ib core
 * @intex: Index into pkey table
 *
 * Return: 0 on failure pkey otherwise
 */
static int rvt_query_pkey(struct ib_device *ibdev, u8 port_num, u16 index,
			  u16 *pkey)
{
	/*
	 * Driver will be responsible for keeping rvt_dev_info.pkey_table up to
	 * date. This function will just return that value. There is no need to
	 * lock, if a stale value is read and sent to the user so be it there is
	 * no way to protect against that anyway.
	 */
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	int port_index;

	port_index = ibport_num_to_idx(ibdev, port_num);
	if (port_index < 0)
		return -EINVAL;

	if (index >= rvt_get_npkeys(rdi))
		return -EINVAL;

	*pkey = rvt_get_pkey(rdi, port_index, index);
	return 0;
}

/**
 * rvt_query_gid - Return a gid from the table
 * @ibdev: Verbs IB dev
 * @port_num: Port number, 1 based from ib core
 * @index: = Index in table
 * @gid: Gid to return
 *
 * Return: 0 on success
 */
static int rvt_query_gid(struct ib_device *ibdev, u8 port_num,
			 int guid_index, union ib_gid *gid)
{
	struct rvt_dev_info *rdi;
	struct rvt_ibport *rvp;
	int port_index;

	/*
	 * Driver is responsible for updating the guid table. Which will be used
	 * to craft the return value. This will work similar to how query_pkey()
	 * is being done.
	 */
	port_index = ibport_num_to_idx(ibdev, port_num);
	if (port_index < 0)
		return -EINVAL;

	rdi = ib_to_rvt(ibdev);
	rvp = rdi->ports[port_index];

	gid->global.subnet_prefix = rvp->gid_prefix;

	return rdi->driver_f.get_guid_be(rdi, rvp, guid_index,
					 &gid->global.interface_id);
}

struct rvt_ucontext {
	struct ib_ucontext ibucontext;
};

static inline struct rvt_ucontext *to_iucontext(struct ib_ucontext
						*ibucontext)
{
	return container_of(ibucontext, struct rvt_ucontext, ibucontext);
}

/**
 * rvt_alloc_ucontext - Allocate a user context
 * @ibdev: Vers IB dev
 * @data: User data allocated
 */
static struct ib_ucontext *rvt_alloc_ucontext(struct ib_device *ibdev,
					      struct ib_udata *udata)
{
	struct rvt_ucontext *context;

	context = kmalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return ERR_PTR(-ENOMEM);
	return &context->ibucontext;
}

/**
 *rvt_dealloc_ucontext - Free a user context
 *@context - Free this
 */
static int rvt_dealloc_ucontext(struct ib_ucontext *context)
{
	kfree(to_iucontext(context));
	return 0;
}

static int rvt_get_port_immutable(struct ib_device *ibdev, u8 port_num,
				  struct ib_port_immutable *immutable)
{
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	struct ib_port_attr attr;
	int err, port_index;

	port_index = ibport_num_to_idx(ibdev, port_num);
	if (port_index < 0)
		return -EINVAL;

	err = rvt_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->core_cap_flags = rdi->dparms.core_cap_flags;
	immutable->max_mad_size = rdi->dparms.max_mad_size;

	return 0;
}

enum {
	MISC,
	QUERY_DEVICE,
	MODIFY_DEVICE,
	QUERY_PORT,
	MODIFY_PORT,
	QUERY_PKEY,
	QUERY_GID,
	ALLOC_UCONTEXT,
	DEALLOC_UCONTEXT,
	GET_PORT_IMMUTABLE,
	CREATE_QP,
	MODIFY_QP,
	DESTROY_QP,
	QUERY_QP,
	POST_SEND,
	POST_RECV,
	POST_SRQ_RECV,
	CREATE_AH,
	DESTROY_AH,
	MODIFY_AH,
	QUERY_AH,
	CREATE_SRQ,
	MODIFY_SRQ,
	DESTROY_SRQ,
	QUERY_SRQ,
	ATTACH_MCAST,
	DETACH_MCAST,
	GET_DMA_MR,
	REG_USER_MR,
	DEREG_MR,
	ALLOC_MR,
	MAP_MR_SG,
	ALLOC_FMR,
	MAP_PHYS_FMR,
	UNMAP_FMR,
	DEALLOC_FMR,
	MMAP,
	CREATE_CQ,
	DESTROY_CQ,
	POLL_CQ,
	REQ_NOTFIY_CQ,
	RESIZE_CQ,
	ALLOC_PD,
	DEALLOC_PD,
	_VERB_IDX_MAX /* Must always be last! */
};

static inline int check_driver_override(struct rvt_dev_info *rdi,
					size_t offset, void *func)
{
	if (!*(void **)((void *)&rdi->ibdev + offset)) {
		*(void **)((void *)&rdi->ibdev + offset) = func;
		return 0;
	}

	return 1;
}

static noinline int check_support(struct rvt_dev_info *rdi, int verb)
{
	switch (verb) {
	case MISC:
		/*
		 * These functions are not part of verbs specifically but are
		 * required for rdmavt to function.
		 */
		if ((!rdi->driver_f.port_callback) ||
		    (!rdi->driver_f.get_card_name) ||
		    (!rdi->driver_f.get_pci_dev))
			return -EINVAL;
		break;

	case QUERY_DEVICE:
		check_driver_override(rdi, offsetof(struct ib_device,
						    query_device),
						    rvt_query_device);
		break;

	case MODIFY_DEVICE:
		/*
		 * rdmavt does not support modify device currently drivers must
		 * provide.
		 */
		if (!check_driver_override(rdi, offsetof(struct ib_device,
							 modify_device),
					   rvt_modify_device))
			return -EOPNOTSUPP;
		break;

	case QUERY_PORT:
		if (!check_driver_override(rdi, offsetof(struct ib_device,
							 query_port),
					   rvt_query_port))
			if (!rdi->driver_f.query_port_state)
				return -EINVAL;
		break;

	case MODIFY_PORT:
		if (!check_driver_override(rdi, offsetof(struct ib_device,
							 modify_port),
					   rvt_modify_port))
			if (!rdi->driver_f.cap_mask_chg ||
			    !rdi->driver_f.shut_down_port)
				return -EINVAL;
		break;

	case QUERY_PKEY:
		check_driver_override(rdi, offsetof(struct ib_device,
						    query_pkey),
				      rvt_query_pkey);
		break;

	case QUERY_GID:
		if (!check_driver_override(rdi, offsetof(struct ib_device,
							 query_gid),
					   rvt_query_gid))
			if (!rdi->driver_f.get_guid_be)
				return -EINVAL;
		break;

	case ALLOC_UCONTEXT:
		check_driver_override(rdi, offsetof(struct ib_device,
						    alloc_ucontext),
				      rvt_alloc_ucontext);
		break;

	case DEALLOC_UCONTEXT:
		check_driver_override(rdi, offsetof(struct ib_device,
						    dealloc_ucontext),
				      rvt_dealloc_ucontext);
		break;

	case GET_PORT_IMMUTABLE:
		check_driver_override(rdi, offsetof(struct ib_device,
						    get_port_immutable),
				      rvt_get_port_immutable);
		break;

	case CREATE_QP:
		if (!check_driver_override(rdi, offsetof(struct ib_device,
							 create_qp),
					   rvt_create_qp))
			if (!rdi->driver_f.qp_priv_alloc ||
			    !rdi->driver_f.qp_priv_free ||
			    !rdi->driver_f.notify_qp_reset ||
			    !rdi->driver_f.flush_qp_waiters ||
			    !rdi->driver_f.stop_send_queue ||
			    !rdi->driver_f.quiesce_qp)
				return -EINVAL;
		break;

	case MODIFY_QP:
		if (!check_driver_override(rdi, offsetof(struct ib_device,
							 modify_qp),
					   rvt_modify_qp))
			if (!rdi->driver_f.notify_qp_reset ||
			    !rdi->driver_f.schedule_send ||
			    !rdi->driver_f.get_pmtu_from_attr ||
			    !rdi->driver_f.flush_qp_waiters ||
			    !rdi->driver_f.stop_send_queue ||
			    !rdi->driver_f.quiesce_qp ||
			    !rdi->driver_f.notify_error_qp ||
			    !rdi->driver_f.mtu_from_qp ||
			    !rdi->driver_f.mtu_to_path_mtu)
				return -EINVAL;
		break;

	case DESTROY_QP:
		if (!check_driver_override(rdi, offsetof(struct ib_device,
							 destroy_qp),
					   rvt_destroy_qp))
			if (!rdi->driver_f.qp_priv_free ||
			    !rdi->driver_f.notify_qp_reset ||
			    !rdi->driver_f.flush_qp_waiters ||
			    !rdi->driver_f.stop_send_queue ||
			    !rdi->driver_f.quiesce_qp)
				return -EINVAL;
		break;

	case QUERY_QP:
		check_driver_override(rdi, offsetof(struct ib_device,
						    query_qp),
						    rvt_query_qp);
		break;

	case POST_SEND:
		if (!check_driver_override(rdi, offsetof(struct ib_device,
							 post_send),
					   rvt_post_send))
			if (!rdi->driver_f.schedule_send ||
			    !rdi->driver_f.do_send ||
			    !rdi->post_parms)
				return -EINVAL;
		break;

	case POST_RECV:
		check_driver_override(rdi, offsetof(struct ib_device,
						    post_recv),
				      rvt_post_recv);
		break;
	case POST_SRQ_RECV:
		check_driver_override(rdi, offsetof(struct ib_device,
						    post_srq_recv),
				      rvt_post_srq_recv);
		break;

	case CREATE_AH:
		check_driver_override(rdi, offsetof(struct ib_device,
						    create_ah),
				      rvt_create_ah);
		break;

	case DESTROY_AH:
		check_driver_override(rdi, offsetof(struct ib_device,
						    destroy_ah),
				      rvt_destroy_ah);
		break;

	case MODIFY_AH:
		check_driver_override(rdi, offsetof(struct ib_device,
						    modify_ah),
				      rvt_modify_ah);
		break;

	case QUERY_AH:
		check_driver_override(rdi, offsetof(struct ib_device,
						    query_ah),
				      rvt_query_ah);
		break;

	case CREATE_SRQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    create_srq),
				      rvt_create_srq);
		break;

	case MODIFY_SRQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    modify_srq),
				      rvt_modify_srq);
		break;

	case DESTROY_SRQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    destroy_srq),
				      rvt_destroy_srq);
		break;

	case QUERY_SRQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    query_srq),
				      rvt_query_srq);
		break;

	case ATTACH_MCAST:
		check_driver_override(rdi, offsetof(struct ib_device,
						    attach_mcast),
				      rvt_attach_mcast);
		break;

	case DETACH_MCAST:
		check_driver_override(rdi, offsetof(struct ib_device,
						    detach_mcast),
				      rvt_detach_mcast);
		break;

	case GET_DMA_MR:
		check_driver_override(rdi, offsetof(struct ib_device,
						    get_dma_mr),
				      rvt_get_dma_mr);
		break;

	case REG_USER_MR:
		check_driver_override(rdi, offsetof(struct ib_device,
						    reg_user_mr),
				      rvt_reg_user_mr);
		break;

	case DEREG_MR:
		check_driver_override(rdi, offsetof(struct ib_device,
						    dereg_mr),
				      rvt_dereg_mr);
		break;

	case ALLOC_FMR:
		check_driver_override(rdi, offsetof(struct ib_device,
						    alloc_fmr),
				      rvt_alloc_fmr);
		break;

	case ALLOC_MR:
		check_driver_override(rdi, offsetof(struct ib_device,
						    alloc_mr),
				      rvt_alloc_mr);
		break;

	case MAP_MR_SG:
		check_driver_override(rdi, offsetof(struct ib_device,
						    map_mr_sg),
				      rvt_map_mr_sg);
		break;

	case MAP_PHYS_FMR:
		check_driver_override(rdi, offsetof(struct ib_device,
						    map_phys_fmr),
				      rvt_map_phys_fmr);
		break;

	case UNMAP_FMR:
		check_driver_override(rdi, offsetof(struct ib_device,
						    unmap_fmr),
				      rvt_unmap_fmr);
		break;

	case DEALLOC_FMR:
		check_driver_override(rdi, offsetof(struct ib_device,
						    dealloc_fmr),
				      rvt_dealloc_fmr);
		break;

	case MMAP:
		check_driver_override(rdi, offsetof(struct ib_device,
						    mmap),
				      rvt_mmap);
		break;

	case CREATE_CQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    create_cq),
				      rvt_create_cq);
		break;

	case DESTROY_CQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    destroy_cq),
				      rvt_destroy_cq);
		break;

	case POLL_CQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    poll_cq),
				      rvt_poll_cq);
		break;

	case REQ_NOTFIY_CQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    req_notify_cq),
				      rvt_req_notify_cq);
		break;

	case RESIZE_CQ:
		check_driver_override(rdi, offsetof(struct ib_device,
						    resize_cq),
				      rvt_resize_cq);
		break;

	case ALLOC_PD:
		check_driver_override(rdi, offsetof(struct ib_device,
						    alloc_pd),
				      rvt_alloc_pd);
		break;

	case DEALLOC_PD:
		check_driver_override(rdi, offsetof(struct ib_device,
						    dealloc_pd),
				      rvt_dealloc_pd);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * rvt_register_device - register a driver
 * @rdi: main dev structure for all of rdmavt operations
 *
 * It is up to drivers to allocate the rdi and fill in the appropriate
 * information.
 *
 * Return: 0 on success otherwise an errno.
 */
int rvt_register_device(struct rvt_dev_info *rdi)
{
	int ret = 0, i;

	if (!rdi)
		return -EINVAL;

	/*
	 * Check to ensure drivers have setup the required helpers for the verbs
	 * they want rdmavt to handle
	 */
	for (i = 0; i < _VERB_IDX_MAX; i++)
		if (check_support(rdi, i)) {
			pr_err("Driver support req not met at %d\n", i);
			return -EINVAL;
		}


	/* Once we get past here we can use rvt_pr macros and tracepoints */
	trace_rvt_dbg(rdi, "Driver attempting registration");
	rvt_mmap_init(rdi);

	/* Queue Pairs */
	ret = rvt_driver_qp_init(rdi);
	if (ret) {
		pr_err("Error in driver QP init.\n");
		return -EINVAL;
	}

	/* Address Handle */
	spin_lock_init(&rdi->n_ahs_lock);
	rdi->n_ahs_allocated = 0;

	/* Shared Receive Queue */
	rvt_driver_srq_init(rdi);

	/* Multicast */
	rvt_driver_mcast_init(rdi);

	/* Mem Region */
	ret = rvt_driver_mr_init(rdi);
	if (ret) {
		pr_err("Error in driver MR init.\n");
		goto bail_no_mr;
	}

	/* Completion queues */
	ret = rvt_driver_cq_init(rdi);
	if (ret) {
		pr_err("Error in driver CQ init.\n");
		goto bail_mr;
	}

	/* DMA Operations */
	rdi->ibdev.dma_ops =
		rdi->ibdev.dma_ops ? : &rvt_default_dma_mapping_ops;

	/* Protection Domain */
	spin_lock_init(&rdi->n_pds_lock);
	rdi->n_pds_allocated = 0;

	/*
	 * There are some things which could be set by underlying drivers but
	 * really should be up to rdmavt to set. For instance drivers can't know
	 * exactly which functions rdmavt supports, nor do they know the ABI
	 * version, so we do all of this sort of stuff here.
	 */
	rdi->ibdev.uverbs_abi_ver = RVT_UVERBS_ABI_VERSION;
	rdi->ibdev.uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT)         |
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)        |
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)          |
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)            |
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD)          |
		(1ull << IB_USER_VERBS_CMD_CREATE_AH)           |
		(1ull << IB_USER_VERBS_CMD_MODIFY_AH)           |
		(1ull << IB_USER_VERBS_CMD_QUERY_AH)            |
		(1ull << IB_USER_VERBS_CMD_DESTROY_AH)          |
		(1ull << IB_USER_VERBS_CMD_REG_MR)              |
		(1ull << IB_USER_VERBS_CMD_DEREG_MR)            |
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)           |
		(1ull << IB_USER_VERBS_CMD_RESIZE_CQ)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)          |
		(1ull << IB_USER_VERBS_CMD_POLL_CQ)             |
		(1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ)       |
		(1ull << IB_USER_VERBS_CMD_CREATE_QP)           |
		(1ull << IB_USER_VERBS_CMD_QUERY_QP)            |
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP)          |
		(1ull << IB_USER_VERBS_CMD_POST_SEND)           |
		(1ull << IB_USER_VERBS_CMD_POST_RECV)           |
		(1ull << IB_USER_VERBS_CMD_ATTACH_MCAST)        |
		(1ull << IB_USER_VERBS_CMD_DETACH_MCAST)        |
		(1ull << IB_USER_VERBS_CMD_CREATE_SRQ)          |
		(1ull << IB_USER_VERBS_CMD_MODIFY_SRQ)          |
		(1ull << IB_USER_VERBS_CMD_QUERY_SRQ)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_SRQ)         |
		(1ull << IB_USER_VERBS_CMD_POST_SRQ_RECV);
	rdi->ibdev.node_type = RDMA_NODE_IB_CA;
	rdi->ibdev.num_comp_vectors = 1;

	/* We are now good to announce we exist */
	ret =  ib_register_device(&rdi->ibdev, rdi->driver_f.port_callback);
	if (ret) {
		rvt_pr_err(rdi, "Failed to register driver with ib core.\n");
		goto bail_cq;
	}

	rvt_create_mad_agents(rdi);

	rvt_pr_info(rdi, "Registration with rdmavt done.\n");
	return ret;

bail_cq:
	rvt_cq_exit(rdi);

bail_mr:
	rvt_mr_exit(rdi);

bail_no_mr:
	rvt_qp_exit(rdi);

	return ret;
}
EXPORT_SYMBOL(rvt_register_device);

/**
 * rvt_unregister_device - remove a driver
 * @rdi: rvt dev struct
 */
void rvt_unregister_device(struct rvt_dev_info *rdi)
{
	trace_rvt_dbg(rdi, "Driver is unregistering.");
	if (!rdi)
		return;

	rvt_free_mad_agents(rdi);

	ib_unregister_device(&rdi->ibdev);
	rvt_cq_exit(rdi);
	rvt_mr_exit(rdi);
	rvt_qp_exit(rdi);
}
EXPORT_SYMBOL(rvt_unregister_device);

/**
 * rvt_init_port - init internal data for driver port
 * @rdi: rvt dev strut
 * @port: rvt port
 * @port_index: 0 based index of ports, different from IB core port num
 *
 * Keep track of a list of ports. No need to have a detach port.
 * They persist until the driver goes away.
 *
 * Return: always 0
 */
int rvt_init_port(struct rvt_dev_info *rdi, struct rvt_ibport *port,
		  int port_index, u16 *pkey_table)
{

	rdi->ports[port_index] = port;
	rdi->ports[port_index]->pkey_table = pkey_table;

	return 0;
}
EXPORT_SYMBOL(rvt_init_port);

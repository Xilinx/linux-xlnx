/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#include "rxe.h"
#include "rxe_loc.h"

MODULE_AUTHOR("Bob Pearson, Frank Zago, John Groves, Kamal Heib");
MODULE_DESCRIPTION("Soft RDMA transport");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("0.2");

/* free resources for all ports on a device */
static void rxe_cleanup_ports(struct rxe_dev *rxe)
{
	kfree(rxe->port.pkey_tbl);
	rxe->port.pkey_tbl = NULL;

}

/* free resources for a rxe device all objects created for this device must
 * have been destroyed
 */
static void rxe_cleanup(struct rxe_dev *rxe)
{
	rxe_pool_cleanup(&rxe->uc_pool);
	rxe_pool_cleanup(&rxe->pd_pool);
	rxe_pool_cleanup(&rxe->ah_pool);
	rxe_pool_cleanup(&rxe->srq_pool);
	rxe_pool_cleanup(&rxe->qp_pool);
	rxe_pool_cleanup(&rxe->cq_pool);
	rxe_pool_cleanup(&rxe->mr_pool);
	rxe_pool_cleanup(&rxe->mw_pool);
	rxe_pool_cleanup(&rxe->mc_grp_pool);
	rxe_pool_cleanup(&rxe->mc_elem_pool);

	rxe_cleanup_ports(rxe);
}

/* called when all references have been dropped */
void rxe_release(struct kref *kref)
{
	struct rxe_dev *rxe = container_of(kref, struct rxe_dev, ref_cnt);

	rxe_cleanup(rxe);
	ib_dealloc_device(&rxe->ib_dev);
}

void rxe_dev_put(struct rxe_dev *rxe)
{
	kref_put(&rxe->ref_cnt, rxe_release);
}
EXPORT_SYMBOL_GPL(rxe_dev_put);

/* initialize rxe device parameters */
static int rxe_init_device_param(struct rxe_dev *rxe)
{
	rxe->max_inline_data			= RXE_MAX_INLINE_DATA;

	rxe->attr.fw_ver			= RXE_FW_VER;
	rxe->attr.max_mr_size			= RXE_MAX_MR_SIZE;
	rxe->attr.page_size_cap			= RXE_PAGE_SIZE_CAP;
	rxe->attr.vendor_id			= RXE_VENDOR_ID;
	rxe->attr.vendor_part_id		= RXE_VENDOR_PART_ID;
	rxe->attr.hw_ver			= RXE_HW_VER;
	rxe->attr.max_qp			= RXE_MAX_QP;
	rxe->attr.max_qp_wr			= RXE_MAX_QP_WR;
	rxe->attr.device_cap_flags		= RXE_DEVICE_CAP_FLAGS;
	rxe->attr.max_sge			= RXE_MAX_SGE;
	rxe->attr.max_sge_rd			= RXE_MAX_SGE_RD;
	rxe->attr.max_cq			= RXE_MAX_CQ;
	rxe->attr.max_cqe			= (1 << RXE_MAX_LOG_CQE) - 1;
	rxe->attr.max_mr			= RXE_MAX_MR;
	rxe->attr.max_pd			= RXE_MAX_PD;
	rxe->attr.max_qp_rd_atom		= RXE_MAX_QP_RD_ATOM;
	rxe->attr.max_ee_rd_atom		= RXE_MAX_EE_RD_ATOM;
	rxe->attr.max_res_rd_atom		= RXE_MAX_RES_RD_ATOM;
	rxe->attr.max_qp_init_rd_atom		= RXE_MAX_QP_INIT_RD_ATOM;
	rxe->attr.max_ee_init_rd_atom		= RXE_MAX_EE_INIT_RD_ATOM;
	rxe->attr.atomic_cap			= RXE_ATOMIC_CAP;
	rxe->attr.max_ee			= RXE_MAX_EE;
	rxe->attr.max_rdd			= RXE_MAX_RDD;
	rxe->attr.max_mw			= RXE_MAX_MW;
	rxe->attr.max_raw_ipv6_qp		= RXE_MAX_RAW_IPV6_QP;
	rxe->attr.max_raw_ethy_qp		= RXE_MAX_RAW_ETHY_QP;
	rxe->attr.max_mcast_grp			= RXE_MAX_MCAST_GRP;
	rxe->attr.max_mcast_qp_attach		= RXE_MAX_MCAST_QP_ATTACH;
	rxe->attr.max_total_mcast_qp_attach	= RXE_MAX_TOT_MCAST_QP_ATTACH;
	rxe->attr.max_ah			= RXE_MAX_AH;
	rxe->attr.max_fmr			= RXE_MAX_FMR;
	rxe->attr.max_map_per_fmr		= RXE_MAX_MAP_PER_FMR;
	rxe->attr.max_srq			= RXE_MAX_SRQ;
	rxe->attr.max_srq_wr			= RXE_MAX_SRQ_WR;
	rxe->attr.max_srq_sge			= RXE_MAX_SRQ_SGE;
	rxe->attr.max_fast_reg_page_list_len	= RXE_MAX_FMR_PAGE_LIST_LEN;
	rxe->attr.max_pkeys			= RXE_MAX_PKEYS;
	rxe->attr.local_ca_ack_delay		= RXE_LOCAL_CA_ACK_DELAY;

	rxe->max_ucontext			= RXE_MAX_UCONTEXT;

	return 0;
}

/* initialize port attributes */
static int rxe_init_port_param(struct rxe_port *port)
{
	port->attr.state		= RXE_PORT_STATE;
	port->attr.max_mtu		= RXE_PORT_MAX_MTU;
	port->attr.active_mtu		= RXE_PORT_ACTIVE_MTU;
	port->attr.gid_tbl_len		= RXE_PORT_GID_TBL_LEN;
	port->attr.port_cap_flags	= RXE_PORT_PORT_CAP_FLAGS;
	port->attr.max_msg_sz		= RXE_PORT_MAX_MSG_SZ;
	port->attr.bad_pkey_cntr	= RXE_PORT_BAD_PKEY_CNTR;
	port->attr.qkey_viol_cntr	= RXE_PORT_QKEY_VIOL_CNTR;
	port->attr.pkey_tbl_len		= RXE_PORT_PKEY_TBL_LEN;
	port->attr.lid			= RXE_PORT_LID;
	port->attr.sm_lid		= RXE_PORT_SM_LID;
	port->attr.lmc			= RXE_PORT_LMC;
	port->attr.max_vl_num		= RXE_PORT_MAX_VL_NUM;
	port->attr.sm_sl		= RXE_PORT_SM_SL;
	port->attr.subnet_timeout	= RXE_PORT_SUBNET_TIMEOUT;
	port->attr.init_type_reply	= RXE_PORT_INIT_TYPE_REPLY;
	port->attr.active_width		= RXE_PORT_ACTIVE_WIDTH;
	port->attr.active_speed		= RXE_PORT_ACTIVE_SPEED;
	port->attr.phys_state		= RXE_PORT_PHYS_STATE;
	port->mtu_cap			=
				ib_mtu_enum_to_int(RXE_PORT_ACTIVE_MTU);
	port->subnet_prefix		= cpu_to_be64(RXE_PORT_SUBNET_PREFIX);

	return 0;
}

/* initialize port state, note IB convention that HCA ports are always
 * numbered from 1
 */
static int rxe_init_ports(struct rxe_dev *rxe)
{
	struct rxe_port *port = &rxe->port;

	rxe_init_port_param(port);

	if (!port->attr.pkey_tbl_len || !port->attr.gid_tbl_len)
		return -EINVAL;

	port->pkey_tbl = kcalloc(port->attr.pkey_tbl_len,
			sizeof(*port->pkey_tbl), GFP_KERNEL);

	if (!port->pkey_tbl)
		return -ENOMEM;

	port->pkey_tbl[0] = 0xffff;
	port->port_guid = rxe->ifc_ops->port_guid(rxe);

	spin_lock_init(&port->port_lock);

	return 0;
}

/* init pools of managed objects */
static int rxe_init_pools(struct rxe_dev *rxe)
{
	int err;

	err = rxe_pool_init(rxe, &rxe->uc_pool, RXE_TYPE_UC,
			    rxe->max_ucontext);
	if (err)
		goto err1;

	err = rxe_pool_init(rxe, &rxe->pd_pool, RXE_TYPE_PD,
			    rxe->attr.max_pd);
	if (err)
		goto err2;

	err = rxe_pool_init(rxe, &rxe->ah_pool, RXE_TYPE_AH,
			    rxe->attr.max_ah);
	if (err)
		goto err3;

	err = rxe_pool_init(rxe, &rxe->srq_pool, RXE_TYPE_SRQ,
			    rxe->attr.max_srq);
	if (err)
		goto err4;

	err = rxe_pool_init(rxe, &rxe->qp_pool, RXE_TYPE_QP,
			    rxe->attr.max_qp);
	if (err)
		goto err5;

	err = rxe_pool_init(rxe, &rxe->cq_pool, RXE_TYPE_CQ,
			    rxe->attr.max_cq);
	if (err)
		goto err6;

	err = rxe_pool_init(rxe, &rxe->mr_pool, RXE_TYPE_MR,
			    rxe->attr.max_mr);
	if (err)
		goto err7;

	err = rxe_pool_init(rxe, &rxe->mw_pool, RXE_TYPE_MW,
			    rxe->attr.max_mw);
	if (err)
		goto err8;

	err = rxe_pool_init(rxe, &rxe->mc_grp_pool, RXE_TYPE_MC_GRP,
			    rxe->attr.max_mcast_grp);
	if (err)
		goto err9;

	err = rxe_pool_init(rxe, &rxe->mc_elem_pool, RXE_TYPE_MC_ELEM,
			    rxe->attr.max_total_mcast_qp_attach);
	if (err)
		goto err10;

	return 0;

err10:
	rxe_pool_cleanup(&rxe->mc_grp_pool);
err9:
	rxe_pool_cleanup(&rxe->mw_pool);
err8:
	rxe_pool_cleanup(&rxe->mr_pool);
err7:
	rxe_pool_cleanup(&rxe->cq_pool);
err6:
	rxe_pool_cleanup(&rxe->qp_pool);
err5:
	rxe_pool_cleanup(&rxe->srq_pool);
err4:
	rxe_pool_cleanup(&rxe->ah_pool);
err3:
	rxe_pool_cleanup(&rxe->pd_pool);
err2:
	rxe_pool_cleanup(&rxe->uc_pool);
err1:
	return err;
}

/* initialize rxe device state */
static int rxe_init(struct rxe_dev *rxe)
{
	int err;

	/* init default device parameters */
	rxe_init_device_param(rxe);

	err = rxe_init_ports(rxe);
	if (err)
		goto err1;

	err = rxe_init_pools(rxe);
	if (err)
		goto err2;

	/* init pending mmap list */
	spin_lock_init(&rxe->mmap_offset_lock);
	spin_lock_init(&rxe->pending_lock);
	INIT_LIST_HEAD(&rxe->pending_mmaps);
	INIT_LIST_HEAD(&rxe->list);

	mutex_init(&rxe->usdev_lock);

	return 0;

err2:
	rxe_cleanup_ports(rxe);
err1:
	return err;
}

int rxe_set_mtu(struct rxe_dev *rxe, unsigned int ndev_mtu)
{
	struct rxe_port *port = &rxe->port;
	enum ib_mtu mtu;

	mtu = eth_mtu_int_to_enum(ndev_mtu);

	/* Make sure that new MTU in range */
	mtu = mtu ? min_t(enum ib_mtu, mtu, RXE_PORT_MAX_MTU) : IB_MTU_256;

	port->attr.active_mtu = mtu;
	port->mtu_cap = ib_mtu_enum_to_int(mtu);

	return 0;
}
EXPORT_SYMBOL(rxe_set_mtu);

/* called by ifc layer to create new rxe device.
 * The caller should allocate memory for rxe by calling ib_alloc_device.
 */
int rxe_add(struct rxe_dev *rxe, unsigned int mtu)
{
	int err;

	kref_init(&rxe->ref_cnt);

	err = rxe_init(rxe);
	if (err)
		goto err1;

	err = rxe_set_mtu(rxe, mtu);
	if (err)
		goto err1;

	err = rxe_register_device(rxe);
	if (err)
		goto err1;

	return 0;

err1:
	rxe_dev_put(rxe);
	return err;
}
EXPORT_SYMBOL(rxe_add);

/* called by the ifc layer to remove a device */
void rxe_remove(struct rxe_dev *rxe)
{
	rxe_unregister_device(rxe);

	rxe_dev_put(rxe);
}
EXPORT_SYMBOL(rxe_remove);

static int __init rxe_module_init(void)
{
	int err;

	/* initialize slab caches for managed objects */
	err = rxe_cache_init();
	if (err) {
		pr_err("unable to init object pools\n");
		return err;
	}

	err = rxe_net_init();
	if (err)
		return err;

	pr_info("loaded\n");
	return 0;
}

static void __exit rxe_module_exit(void)
{
	rxe_remove_all();
	rxe_net_exit();
	rxe_cache_exit();

	pr_info("unloaded\n");
}

late_initcall(rxe_module_init);
module_exit(rxe_module_exit);

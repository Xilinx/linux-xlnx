/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/etherdevice.h>
#include <linux/mlx4/cmd.h>
#include <linux/module.h>
#include <linux/cache.h>

#include "fw.h"
#include "icm.h"

enum {
	MLX4_COMMAND_INTERFACE_MIN_REV		= 2,
	MLX4_COMMAND_INTERFACE_MAX_REV		= 3,
	MLX4_COMMAND_INTERFACE_NEW_PORT_CMDS	= 3,
};

extern void __buggy_use_of_MLX4_GET(void);
extern void __buggy_use_of_MLX4_PUT(void);

static bool enable_qos;
module_param(enable_qos, bool, 0444);
MODULE_PARM_DESC(enable_qos, "Enable Quality of Service support in the HCA (default: off)");

#define MLX4_GET(dest, source, offset)				      \
	do {							      \
		void *__p = (char *) (source) + (offset);	      \
		switch (sizeof (dest)) {			      \
		case 1: (dest) = *(u8 *) __p;	    break;	      \
		case 2: (dest) = be16_to_cpup(__p); break;	      \
		case 4: (dest) = be32_to_cpup(__p); break;	      \
		case 8: (dest) = be64_to_cpup(__p); break;	      \
		default: __buggy_use_of_MLX4_GET();		      \
		}						      \
	} while (0)

#define MLX4_PUT(dest, source, offset)				      \
	do {							      \
		void *__d = ((char *) (dest) + (offset));	      \
		switch (sizeof(source)) {			      \
		case 1: *(u8 *) __d = (source);		       break; \
		case 2:	*(__be16 *) __d = cpu_to_be16(source); break; \
		case 4:	*(__be32 *) __d = cpu_to_be32(source); break; \
		case 8:	*(__be64 *) __d = cpu_to_be64(source); break; \
		default: __buggy_use_of_MLX4_PUT();		      \
		}						      \
	} while (0)

static void dump_dev_cap_flags(struct mlx4_dev *dev, u64 flags)
{
	static const char *fname[] = {
		[ 0] = "RC transport",
		[ 1] = "UC transport",
		[ 2] = "UD transport",
		[ 3] = "XRC transport",
		[ 4] = "reliable multicast",
		[ 5] = "FCoIB support",
		[ 6] = "SRQ support",
		[ 7] = "IPoIB checksum offload",
		[ 8] = "P_Key violation counter",
		[ 9] = "Q_Key violation counter",
		[10] = "VMM",
		[12] = "Dual Port Different Protocol (DPDP) support",
		[15] = "Big LSO headers",
		[16] = "MW support",
		[17] = "APM support",
		[18] = "Atomic ops support",
		[19] = "Raw multicast support",
		[20] = "Address vector port checking support",
		[21] = "UD multicast support",
		[24] = "Demand paging support",
		[25] = "Router support",
		[30] = "IBoE support",
		[32] = "Unicast loopback support",
		[34] = "FCS header control",
		[38] = "Wake On LAN support",
		[40] = "UDP RSS support",
		[41] = "Unicast VEP steering support",
		[42] = "Multicast VEP steering support",
		[48] = "Counters support",
		[53] = "Port ETS Scheduler support",
		[55] = "Port link type sensing support",
		[59] = "Port management change event support",
		[61] = "64 byte EQE support",
		[62] = "64 byte CQE support",
	};
	int i;

	mlx4_dbg(dev, "DEV_CAP flags:\n");
	for (i = 0; i < ARRAY_SIZE(fname); ++i)
		if (fname[i] && (flags & (1LL << i)))
			mlx4_dbg(dev, "    %s\n", fname[i]);
}

static void dump_dev_cap_flags2(struct mlx4_dev *dev, u64 flags)
{
	static const char * const fname[] = {
		[0] = "RSS support",
		[1] = "RSS Toeplitz Hash Function support",
		[2] = "RSS XOR Hash Function support",
		[3] = "Device manage flow steering support",
		[4] = "Automatic MAC reassignment support",
		[5] = "Time stamping support",
		[6] = "VST (control vlan insertion/stripping) support",
		[7] = "FSM (MAC anti-spoofing) support",
		[8] = "Dynamic QP updates support"
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(fname); ++i)
		if (fname[i] && (flags & (1LL << i)))
			mlx4_dbg(dev, "    %s\n", fname[i]);
}

int mlx4_MOD_STAT_CFG(struct mlx4_dev *dev, struct mlx4_mod_stat_cfg *cfg)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *inbox;
	int err = 0;

#define MOD_STAT_CFG_IN_SIZE		0x100

#define MOD_STAT_CFG_PG_SZ_M_OFFSET	0x002
#define MOD_STAT_CFG_PG_SZ_OFFSET	0x003

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	inbox = mailbox->buf;

	MLX4_PUT(inbox, cfg->log_pg_sz, MOD_STAT_CFG_PG_SZ_OFFSET);
	MLX4_PUT(inbox, cfg->log_pg_sz_m, MOD_STAT_CFG_PG_SZ_M_OFFSET);

	err = mlx4_cmd(dev, mailbox->dma, 0, 0, MLX4_CMD_MOD_STAT_CFG,
			MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_QUERY_FUNC_CAP_wrapper(struct mlx4_dev *dev, int slave,
				struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox,
				struct mlx4_cmd_info *cmd)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u8	field;
	u32	size;
	int	err = 0;

#define QUERY_FUNC_CAP_FLAGS_OFFSET		0x0
#define QUERY_FUNC_CAP_NUM_PORTS_OFFSET		0x1
#define QUERY_FUNC_CAP_PF_BHVR_OFFSET		0x4
#define QUERY_FUNC_CAP_FMR_OFFSET		0x8
#define QUERY_FUNC_CAP_QP_QUOTA_OFFSET_DEP	0x10
#define QUERY_FUNC_CAP_CQ_QUOTA_OFFSET_DEP	0x14
#define QUERY_FUNC_CAP_SRQ_QUOTA_OFFSET_DEP	0x18
#define QUERY_FUNC_CAP_MPT_QUOTA_OFFSET_DEP	0x20
#define QUERY_FUNC_CAP_MTT_QUOTA_OFFSET_DEP	0x24
#define QUERY_FUNC_CAP_MCG_QUOTA_OFFSET_DEP	0x28
#define QUERY_FUNC_CAP_MAX_EQ_OFFSET		0x2c
#define QUERY_FUNC_CAP_RESERVED_EQ_OFFSET	0x30

#define QUERY_FUNC_CAP_QP_QUOTA_OFFSET		0x50
#define QUERY_FUNC_CAP_CQ_QUOTA_OFFSET		0x54
#define QUERY_FUNC_CAP_SRQ_QUOTA_OFFSET		0x58
#define QUERY_FUNC_CAP_MPT_QUOTA_OFFSET		0x60
#define QUERY_FUNC_CAP_MTT_QUOTA_OFFSET		0x64
#define QUERY_FUNC_CAP_MCG_QUOTA_OFFSET		0x68

#define QUERY_FUNC_CAP_FMR_FLAG			0x80
#define QUERY_FUNC_CAP_FLAG_RDMA		0x40
#define QUERY_FUNC_CAP_FLAG_ETH			0x80
#define QUERY_FUNC_CAP_FLAG_QUOTAS		0x10

/* when opcode modifier = 1 */
#define QUERY_FUNC_CAP_PHYS_PORT_OFFSET		0x3
#define QUERY_FUNC_CAP_RDMA_PROPS_OFFSET	0x8
#define QUERY_FUNC_CAP_ETH_PROPS_OFFSET		0xc

#define QUERY_FUNC_CAP_QP0_TUNNEL		0x10
#define QUERY_FUNC_CAP_QP0_PROXY		0x14
#define QUERY_FUNC_CAP_QP1_TUNNEL		0x18
#define QUERY_FUNC_CAP_QP1_PROXY		0x1c

#define QUERY_FUNC_CAP_ETH_PROPS_FORCE_MAC	0x40
#define QUERY_FUNC_CAP_ETH_PROPS_FORCE_VLAN	0x80

#define QUERY_FUNC_CAP_RDMA_PROPS_FORCE_PHY_WQE_GID 0x80

	if (vhcr->op_modifier == 1) {
		field = 0;
		/* ensure force vlan and force mac bits are not set */
		MLX4_PUT(outbox->buf, field, QUERY_FUNC_CAP_ETH_PROPS_OFFSET);
		/* ensure that phy_wqe_gid bit is not set */
		MLX4_PUT(outbox->buf, field, QUERY_FUNC_CAP_RDMA_PROPS_OFFSET);

		field = vhcr->in_modifier; /* phys-port = logical-port */
		MLX4_PUT(outbox->buf, field, QUERY_FUNC_CAP_PHYS_PORT_OFFSET);

		/* size is now the QP number */
		size = dev->phys_caps.base_tunnel_sqpn + 8 * slave + field - 1;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_QP0_TUNNEL);

		size += 2;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_QP1_TUNNEL);

		size = dev->phys_caps.base_proxy_sqpn + 8 * slave + field - 1;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_QP0_PROXY);

		size += 2;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_QP1_PROXY);

	} else if (vhcr->op_modifier == 0) {
		/* enable rdma and ethernet interfaces, and new quota locations */
		field = (QUERY_FUNC_CAP_FLAG_ETH | QUERY_FUNC_CAP_FLAG_RDMA |
			 QUERY_FUNC_CAP_FLAG_QUOTAS);
		MLX4_PUT(outbox->buf, field, QUERY_FUNC_CAP_FLAGS_OFFSET);

		field = dev->caps.num_ports;
		MLX4_PUT(outbox->buf, field, QUERY_FUNC_CAP_NUM_PORTS_OFFSET);

		size = dev->caps.function_caps; /* set PF behaviours */
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_PF_BHVR_OFFSET);

		field = 0; /* protected FMR support not available as yet */
		MLX4_PUT(outbox->buf, field, QUERY_FUNC_CAP_FMR_OFFSET);

		size = priv->mfunc.master.res_tracker.res_alloc[RES_QP].quota[slave];
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_QP_QUOTA_OFFSET);
		size = dev->caps.num_qps;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_QP_QUOTA_OFFSET_DEP);

		size = priv->mfunc.master.res_tracker.res_alloc[RES_SRQ].quota[slave];
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_SRQ_QUOTA_OFFSET);
		size = dev->caps.num_srqs;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_SRQ_QUOTA_OFFSET_DEP);

		size = priv->mfunc.master.res_tracker.res_alloc[RES_CQ].quota[slave];
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_CQ_QUOTA_OFFSET);
		size = dev->caps.num_cqs;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_CQ_QUOTA_OFFSET_DEP);

		size = dev->caps.num_eqs;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_MAX_EQ_OFFSET);

		size = dev->caps.reserved_eqs;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_RESERVED_EQ_OFFSET);

		size = priv->mfunc.master.res_tracker.res_alloc[RES_MPT].quota[slave];
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_MPT_QUOTA_OFFSET);
		size = dev->caps.num_mpts;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_MPT_QUOTA_OFFSET_DEP);

		size = priv->mfunc.master.res_tracker.res_alloc[RES_MTT].quota[slave];
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_MTT_QUOTA_OFFSET);
		size = dev->caps.num_mtts;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_MTT_QUOTA_OFFSET_DEP);

		size = dev->caps.num_mgms + dev->caps.num_amgms;
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_MCG_QUOTA_OFFSET);
		MLX4_PUT(outbox->buf, size, QUERY_FUNC_CAP_MCG_QUOTA_OFFSET_DEP);

	} else
		err = -EINVAL;

	return err;
}

int mlx4_QUERY_FUNC_CAP(struct mlx4_dev *dev, u32 gen_or_port,
			struct mlx4_func_cap *func_cap)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32			*outbox;
	u8			field, op_modifier;
	u32			size;
	int			err = 0, quotas = 0;

	op_modifier = !!gen_or_port; /* 0 = general, 1 = logical port */

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err = mlx4_cmd_box(dev, 0, mailbox->dma, gen_or_port, op_modifier,
			   MLX4_CMD_QUERY_FUNC_CAP,
			   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
	if (err)
		goto out;

	outbox = mailbox->buf;

	if (!op_modifier) {
		MLX4_GET(field, outbox, QUERY_FUNC_CAP_FLAGS_OFFSET);
		if (!(field & (QUERY_FUNC_CAP_FLAG_ETH | QUERY_FUNC_CAP_FLAG_RDMA))) {
			mlx4_err(dev, "The host supports neither eth nor rdma interfaces\n");
			err = -EPROTONOSUPPORT;
			goto out;
		}
		func_cap->flags = field;
		quotas = !!(func_cap->flags & QUERY_FUNC_CAP_FLAG_QUOTAS);

		MLX4_GET(field, outbox, QUERY_FUNC_CAP_NUM_PORTS_OFFSET);
		func_cap->num_ports = field;

		MLX4_GET(size, outbox, QUERY_FUNC_CAP_PF_BHVR_OFFSET);
		func_cap->pf_context_behaviour = size;

		if (quotas) {
			MLX4_GET(size, outbox, QUERY_FUNC_CAP_QP_QUOTA_OFFSET);
			func_cap->qp_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_SRQ_QUOTA_OFFSET);
			func_cap->srq_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_CQ_QUOTA_OFFSET);
			func_cap->cq_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_MPT_QUOTA_OFFSET);
			func_cap->mpt_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_MTT_QUOTA_OFFSET);
			func_cap->mtt_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_MCG_QUOTA_OFFSET);
			func_cap->mcg_quota = size & 0xFFFFFF;

		} else {
			MLX4_GET(size, outbox, QUERY_FUNC_CAP_QP_QUOTA_OFFSET_DEP);
			func_cap->qp_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_SRQ_QUOTA_OFFSET_DEP);
			func_cap->srq_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_CQ_QUOTA_OFFSET_DEP);
			func_cap->cq_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_MPT_QUOTA_OFFSET_DEP);
			func_cap->mpt_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_MTT_QUOTA_OFFSET_DEP);
			func_cap->mtt_quota = size & 0xFFFFFF;

			MLX4_GET(size, outbox, QUERY_FUNC_CAP_MCG_QUOTA_OFFSET_DEP);
			func_cap->mcg_quota = size & 0xFFFFFF;
		}
		MLX4_GET(size, outbox, QUERY_FUNC_CAP_MAX_EQ_OFFSET);
		func_cap->max_eq = size & 0xFFFFFF;

		MLX4_GET(size, outbox, QUERY_FUNC_CAP_RESERVED_EQ_OFFSET);
		func_cap->reserved_eq = size & 0xFFFFFF;

		goto out;
	}

	/* logical port query */
	if (gen_or_port > dev->caps.num_ports) {
		err = -EINVAL;
		goto out;
	}

	if (dev->caps.port_type[gen_or_port] == MLX4_PORT_TYPE_ETH) {
		MLX4_GET(field, outbox, QUERY_FUNC_CAP_ETH_PROPS_OFFSET);
		if (field & QUERY_FUNC_CAP_ETH_PROPS_FORCE_VLAN) {
			mlx4_err(dev, "VLAN is enforced on this port\n");
			err = -EPROTONOSUPPORT;
			goto out;
		}

		if (field & QUERY_FUNC_CAP_ETH_PROPS_FORCE_MAC) {
			mlx4_err(dev, "Force mac is enabled on this port\n");
			err = -EPROTONOSUPPORT;
			goto out;
		}
	} else if (dev->caps.port_type[gen_or_port] == MLX4_PORT_TYPE_IB) {
		MLX4_GET(field, outbox, QUERY_FUNC_CAP_RDMA_PROPS_OFFSET);
		if (field & QUERY_FUNC_CAP_RDMA_PROPS_FORCE_PHY_WQE_GID) {
			mlx4_err(dev, "phy_wqe_gid is "
				 "enforced on this ib port\n");
			err = -EPROTONOSUPPORT;
			goto out;
		}
	}

	MLX4_GET(field, outbox, QUERY_FUNC_CAP_PHYS_PORT_OFFSET);
	func_cap->physical_port = field;
	if (func_cap->physical_port != gen_or_port) {
		err = -ENOSYS;
		goto out;
	}

	MLX4_GET(size, outbox, QUERY_FUNC_CAP_QP0_TUNNEL);
	func_cap->qp0_tunnel_qpn = size & 0xFFFFFF;

	MLX4_GET(size, outbox, QUERY_FUNC_CAP_QP0_PROXY);
	func_cap->qp0_proxy_qpn = size & 0xFFFFFF;

	MLX4_GET(size, outbox, QUERY_FUNC_CAP_QP1_TUNNEL);
	func_cap->qp1_tunnel_qpn = size & 0xFFFFFF;

	MLX4_GET(size, outbox, QUERY_FUNC_CAP_QP1_PROXY);
	func_cap->qp1_proxy_qpn = size & 0xFFFFFF;

	/* All other resources are allocated by the master, but we still report
	 * 'num' and 'reserved' capabilities as follows:
	 * - num remains the maximum resource index
	 * - 'num - reserved' is the total available objects of a resource, but
	 *   resource indices may be less than 'reserved'
	 * TODO: set per-resource quotas */

out:
	mlx4_free_cmd_mailbox(dev, mailbox);

	return err;
}

int mlx4_QUERY_DEV_CAP(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *outbox;
	u8 field;
	u32 field32, flags, ext_flags;
	u16 size;
	u16 stat_rate;
	int err;
	int i;

#define QUERY_DEV_CAP_OUT_SIZE		       0x100
#define QUERY_DEV_CAP_MAX_SRQ_SZ_OFFSET		0x10
#define QUERY_DEV_CAP_MAX_QP_SZ_OFFSET		0x11
#define QUERY_DEV_CAP_RSVD_QP_OFFSET		0x12
#define QUERY_DEV_CAP_MAX_QP_OFFSET		0x13
#define QUERY_DEV_CAP_RSVD_SRQ_OFFSET		0x14
#define QUERY_DEV_CAP_MAX_SRQ_OFFSET		0x15
#define QUERY_DEV_CAP_RSVD_EEC_OFFSET		0x16
#define QUERY_DEV_CAP_MAX_EEC_OFFSET		0x17
#define QUERY_DEV_CAP_MAX_CQ_SZ_OFFSET		0x19
#define QUERY_DEV_CAP_RSVD_CQ_OFFSET		0x1a
#define QUERY_DEV_CAP_MAX_CQ_OFFSET		0x1b
#define QUERY_DEV_CAP_MAX_MPT_OFFSET		0x1d
#define QUERY_DEV_CAP_RSVD_EQ_OFFSET		0x1e
#define QUERY_DEV_CAP_MAX_EQ_OFFSET		0x1f
#define QUERY_DEV_CAP_RSVD_MTT_OFFSET		0x20
#define QUERY_DEV_CAP_MAX_MRW_SZ_OFFSET		0x21
#define QUERY_DEV_CAP_RSVD_MRW_OFFSET		0x22
#define QUERY_DEV_CAP_MAX_MTT_SEG_OFFSET	0x23
#define QUERY_DEV_CAP_MAX_AV_OFFSET		0x27
#define QUERY_DEV_CAP_MAX_REQ_QP_OFFSET		0x29
#define QUERY_DEV_CAP_MAX_RES_QP_OFFSET		0x2b
#define QUERY_DEV_CAP_MAX_GSO_OFFSET		0x2d
#define QUERY_DEV_CAP_RSS_OFFSET		0x2e
#define QUERY_DEV_CAP_MAX_RDMA_OFFSET		0x2f
#define QUERY_DEV_CAP_RSZ_SRQ_OFFSET		0x33
#define QUERY_DEV_CAP_ACK_DELAY_OFFSET		0x35
#define QUERY_DEV_CAP_MTU_WIDTH_OFFSET		0x36
#define QUERY_DEV_CAP_VL_PORT_OFFSET		0x37
#define QUERY_DEV_CAP_MAX_MSG_SZ_OFFSET		0x38
#define QUERY_DEV_CAP_MAX_GID_OFFSET		0x3b
#define QUERY_DEV_CAP_RATE_SUPPORT_OFFSET	0x3c
#define QUERY_DEV_CAP_CQ_TS_SUPPORT_OFFSET	0x3e
#define QUERY_DEV_CAP_MAX_PKEY_OFFSET		0x3f
#define QUERY_DEV_CAP_EXT_FLAGS_OFFSET		0x40
#define QUERY_DEV_CAP_FLAGS_OFFSET		0x44
#define QUERY_DEV_CAP_RSVD_UAR_OFFSET		0x48
#define QUERY_DEV_CAP_UAR_SZ_OFFSET		0x49
#define QUERY_DEV_CAP_PAGE_SZ_OFFSET		0x4b
#define QUERY_DEV_CAP_BF_OFFSET			0x4c
#define QUERY_DEV_CAP_LOG_BF_REG_SZ_OFFSET	0x4d
#define QUERY_DEV_CAP_LOG_MAX_BF_REGS_PER_PAGE_OFFSET	0x4e
#define QUERY_DEV_CAP_LOG_MAX_BF_PAGES_OFFSET	0x4f
#define QUERY_DEV_CAP_MAX_SG_SQ_OFFSET		0x51
#define QUERY_DEV_CAP_MAX_DESC_SZ_SQ_OFFSET	0x52
#define QUERY_DEV_CAP_MAX_SG_RQ_OFFSET		0x55
#define QUERY_DEV_CAP_MAX_DESC_SZ_RQ_OFFSET	0x56
#define QUERY_DEV_CAP_MAX_QP_MCG_OFFSET		0x61
#define QUERY_DEV_CAP_RSVD_MCG_OFFSET		0x62
#define QUERY_DEV_CAP_MAX_MCG_OFFSET		0x63
#define QUERY_DEV_CAP_RSVD_PD_OFFSET		0x64
#define QUERY_DEV_CAP_MAX_PD_OFFSET		0x65
#define QUERY_DEV_CAP_RSVD_XRC_OFFSET		0x66
#define QUERY_DEV_CAP_MAX_XRC_OFFSET		0x67
#define QUERY_DEV_CAP_MAX_COUNTERS_OFFSET	0x68
#define QUERY_DEV_CAP_EXT_2_FLAGS_OFFSET	0x70
#define QUERY_DEV_CAP_FLOW_STEERING_RANGE_EN_OFFSET	0x76
#define QUERY_DEV_CAP_FLOW_STEERING_MAX_QP_OFFSET	0x77
#define QUERY_DEV_CAP_RDMARC_ENTRY_SZ_OFFSET	0x80
#define QUERY_DEV_CAP_QPC_ENTRY_SZ_OFFSET	0x82
#define QUERY_DEV_CAP_AUX_ENTRY_SZ_OFFSET	0x84
#define QUERY_DEV_CAP_ALTC_ENTRY_SZ_OFFSET	0x86
#define QUERY_DEV_CAP_EQC_ENTRY_SZ_OFFSET	0x88
#define QUERY_DEV_CAP_CQC_ENTRY_SZ_OFFSET	0x8a
#define QUERY_DEV_CAP_SRQ_ENTRY_SZ_OFFSET	0x8c
#define QUERY_DEV_CAP_C_MPT_ENTRY_SZ_OFFSET	0x8e
#define QUERY_DEV_CAP_MTT_ENTRY_SZ_OFFSET	0x90
#define QUERY_DEV_CAP_D_MPT_ENTRY_SZ_OFFSET	0x92
#define QUERY_DEV_CAP_BMME_FLAGS_OFFSET		0x94
#define QUERY_DEV_CAP_RSVD_LKEY_OFFSET		0x98
#define QUERY_DEV_CAP_MAX_ICM_SZ_OFFSET		0xa0
#define QUERY_DEV_CAP_FW_REASSIGN_MAC		0x9d

	dev_cap->flags2 = 0;
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma, 0, 0, MLX4_CMD_QUERY_DEV_CAP,
			   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
	if (err)
		goto out;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_QP_OFFSET);
	dev_cap->reserved_qps = 1 << (field & 0xf);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_QP_OFFSET);
	dev_cap->max_qps = 1 << (field & 0x1f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_SRQ_OFFSET);
	dev_cap->reserved_srqs = 1 << (field >> 4);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_SRQ_OFFSET);
	dev_cap->max_srqs = 1 << (field & 0x1f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_CQ_SZ_OFFSET);
	dev_cap->max_cq_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_CQ_OFFSET);
	dev_cap->reserved_cqs = 1 << (field & 0xf);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_CQ_OFFSET);
	dev_cap->max_cqs = 1 << (field & 0x1f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MPT_OFFSET);
	dev_cap->max_mpts = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_EQ_OFFSET);
	dev_cap->reserved_eqs = field & 0xf;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_EQ_OFFSET);
	dev_cap->max_eqs = 1 << (field & 0xf);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_MTT_OFFSET);
	dev_cap->reserved_mtts = 1 << (field >> 4);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MRW_SZ_OFFSET);
	dev_cap->max_mrw_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_MRW_OFFSET);
	dev_cap->reserved_mrws = 1 << (field & 0xf);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MTT_SEG_OFFSET);
	dev_cap->max_mtt_seg = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_REQ_QP_OFFSET);
	dev_cap->max_requester_per_qp = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_RES_QP_OFFSET);
	dev_cap->max_responder_per_qp = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_GSO_OFFSET);
	field &= 0x1f;
	if (!field)
		dev_cap->max_gso_sz = 0;
	else
		dev_cap->max_gso_sz = 1 << field;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSS_OFFSET);
	if (field & 0x20)
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_RSS_XOR;
	if (field & 0x10)
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_RSS_TOP;
	field &= 0xf;
	if (field) {
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_RSS;
		dev_cap->max_rss_tbl_sz = 1 << field;
	} else
		dev_cap->max_rss_tbl_sz = 0;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_RDMA_OFFSET);
	dev_cap->max_rdma_global = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_ACK_DELAY_OFFSET);
	dev_cap->local_ca_ack_delay = field & 0x1f;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_VL_PORT_OFFSET);
	dev_cap->num_ports = field & 0xf;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MSG_SZ_OFFSET);
	dev_cap->max_msg_sz = 1 << (field & 0x1f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_FLOW_STEERING_RANGE_EN_OFFSET);
	if (field & 0x80)
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_FS_EN;
	dev_cap->fs_log_max_ucast_qp_range_size = field & 0x1f;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_FLOW_STEERING_MAX_QP_OFFSET);
	dev_cap->fs_max_num_qp_per_entry = field;
	MLX4_GET(stat_rate, outbox, QUERY_DEV_CAP_RATE_SUPPORT_OFFSET);
	dev_cap->stat_rate_support = stat_rate;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_CQ_TS_SUPPORT_OFFSET);
	if (field & 0x80)
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_TS;
	MLX4_GET(ext_flags, outbox, QUERY_DEV_CAP_EXT_FLAGS_OFFSET);
	MLX4_GET(flags, outbox, QUERY_DEV_CAP_FLAGS_OFFSET);
	dev_cap->flags = flags | (u64)ext_flags << 32;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_UAR_OFFSET);
	dev_cap->reserved_uars = field >> 4;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_UAR_SZ_OFFSET);
	dev_cap->uar_size = 1 << ((field & 0x3f) + 20);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_PAGE_SZ_OFFSET);
	dev_cap->min_page_sz = 1 << field;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_BF_OFFSET);
	if (field & 0x80) {
		MLX4_GET(field, outbox, QUERY_DEV_CAP_LOG_BF_REG_SZ_OFFSET);
		dev_cap->bf_reg_size = 1 << (field & 0x1f);
		MLX4_GET(field, outbox, QUERY_DEV_CAP_LOG_MAX_BF_REGS_PER_PAGE_OFFSET);
		if ((1 << (field & 0x3f)) > (PAGE_SIZE / dev_cap->bf_reg_size))
			field = 3;
		dev_cap->bf_regs_per_page = 1 << (field & 0x3f);
		mlx4_dbg(dev, "BlueFlame available (reg size %d, regs/page %d)\n",
			 dev_cap->bf_reg_size, dev_cap->bf_regs_per_page);
	} else {
		dev_cap->bf_reg_size = 0;
		mlx4_dbg(dev, "BlueFlame not available\n");
	}

	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_SG_SQ_OFFSET);
	dev_cap->max_sq_sg = field;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_MAX_DESC_SZ_SQ_OFFSET);
	dev_cap->max_sq_desc_sz = size;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_QP_MCG_OFFSET);
	dev_cap->max_qp_per_mcg = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_MCG_OFFSET);
	dev_cap->reserved_mgms = field & 0xf;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MCG_OFFSET);
	dev_cap->max_mcgs = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_PD_OFFSET);
	dev_cap->reserved_pds = field >> 4;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_PD_OFFSET);
	dev_cap->max_pds = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_XRC_OFFSET);
	dev_cap->reserved_xrcds = field >> 4;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_XRC_OFFSET);
	dev_cap->max_xrcds = 1 << (field & 0x1f);

	MLX4_GET(size, outbox, QUERY_DEV_CAP_RDMARC_ENTRY_SZ_OFFSET);
	dev_cap->rdmarc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_QPC_ENTRY_SZ_OFFSET);
	dev_cap->qpc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_AUX_ENTRY_SZ_OFFSET);
	dev_cap->aux_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_ALTC_ENTRY_SZ_OFFSET);
	dev_cap->altc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_EQC_ENTRY_SZ_OFFSET);
	dev_cap->eqc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_CQC_ENTRY_SZ_OFFSET);
	dev_cap->cqc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_SRQ_ENTRY_SZ_OFFSET);
	dev_cap->srq_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_C_MPT_ENTRY_SZ_OFFSET);
	dev_cap->cmpt_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_MTT_ENTRY_SZ_OFFSET);
	dev_cap->mtt_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_D_MPT_ENTRY_SZ_OFFSET);
	dev_cap->dmpt_entry_sz = size;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_SRQ_SZ_OFFSET);
	dev_cap->max_srq_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_QP_SZ_OFFSET);
	dev_cap->max_qp_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSZ_SRQ_OFFSET);
	dev_cap->resize_srq = field & 1;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_SG_RQ_OFFSET);
	dev_cap->max_rq_sg = field;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_MAX_DESC_SZ_RQ_OFFSET);
	dev_cap->max_rq_desc_sz = size;

	MLX4_GET(dev_cap->bmme_flags, outbox,
		 QUERY_DEV_CAP_BMME_FLAGS_OFFSET);
	MLX4_GET(dev_cap->reserved_lkey, outbox,
		 QUERY_DEV_CAP_RSVD_LKEY_OFFSET);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_FW_REASSIGN_MAC);
	if (field & 1<<6)
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_REASSIGN_MAC_EN;
	MLX4_GET(dev_cap->max_icm_sz, outbox,
		 QUERY_DEV_CAP_MAX_ICM_SZ_OFFSET);
	if (dev_cap->flags & MLX4_DEV_CAP_FLAG_COUNTERS)
		MLX4_GET(dev_cap->max_counters, outbox,
			 QUERY_DEV_CAP_MAX_COUNTERS_OFFSET);

	MLX4_GET(field32, outbox, QUERY_DEV_CAP_EXT_2_FLAGS_OFFSET);
	if (field32 & (1 << 16))
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_UPDATE_QP;
	if (field32 & (1 << 26))
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_VLAN_CONTROL;
	if (field32 & (1 << 20))
		dev_cap->flags2 |= MLX4_DEV_CAP_FLAG2_FSM;

	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
		for (i = 1; i <= dev_cap->num_ports; ++i) {
			MLX4_GET(field, outbox, QUERY_DEV_CAP_VL_PORT_OFFSET);
			dev_cap->max_vl[i]	   = field >> 4;
			MLX4_GET(field, outbox, QUERY_DEV_CAP_MTU_WIDTH_OFFSET);
			dev_cap->ib_mtu[i]	   = field >> 4;
			dev_cap->max_port_width[i] = field & 0xf;
			MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_GID_OFFSET);
			dev_cap->max_gids[i]	   = 1 << (field & 0xf);
			MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_PKEY_OFFSET);
			dev_cap->max_pkeys[i]	   = 1 << (field & 0xf);
		}
	} else {
#define QUERY_PORT_SUPPORTED_TYPE_OFFSET	0x00
#define QUERY_PORT_MTU_OFFSET			0x01
#define QUERY_PORT_ETH_MTU_OFFSET		0x02
#define QUERY_PORT_WIDTH_OFFSET			0x06
#define QUERY_PORT_MAX_GID_PKEY_OFFSET		0x07
#define QUERY_PORT_MAX_MACVLAN_OFFSET		0x0a
#define QUERY_PORT_MAX_VL_OFFSET		0x0b
#define QUERY_PORT_MAC_OFFSET			0x10
#define QUERY_PORT_TRANS_VENDOR_OFFSET		0x18
#define QUERY_PORT_WAVELENGTH_OFFSET		0x1c
#define QUERY_PORT_TRANS_CODE_OFFSET		0x20

		for (i = 1; i <= dev_cap->num_ports; ++i) {
			err = mlx4_cmd_box(dev, 0, mailbox->dma, i, 0, MLX4_CMD_QUERY_PORT,
					   MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);
			if (err)
				goto out;

			MLX4_GET(field, outbox, QUERY_PORT_SUPPORTED_TYPE_OFFSET);
			dev_cap->supported_port_types[i] = field & 3;
			dev_cap->suggested_type[i] = (field >> 3) & 1;
			dev_cap->default_sense[i] = (field >> 4) & 1;
			MLX4_GET(field, outbox, QUERY_PORT_MTU_OFFSET);
			dev_cap->ib_mtu[i]	   = field & 0xf;
			MLX4_GET(field, outbox, QUERY_PORT_WIDTH_OFFSET);
			dev_cap->max_port_width[i] = field & 0xf;
			MLX4_GET(field, outbox, QUERY_PORT_MAX_GID_PKEY_OFFSET);
			dev_cap->max_gids[i]	   = 1 << (field >> 4);
			dev_cap->max_pkeys[i]	   = 1 << (field & 0xf);
			MLX4_GET(field, outbox, QUERY_PORT_MAX_VL_OFFSET);
			dev_cap->max_vl[i]	   = field & 0xf;
			MLX4_GET(field, outbox, QUERY_PORT_MAX_MACVLAN_OFFSET);
			dev_cap->log_max_macs[i]  = field & 0xf;
			dev_cap->log_max_vlans[i] = field >> 4;
			MLX4_GET(dev_cap->eth_mtu[i], outbox, QUERY_PORT_ETH_MTU_OFFSET);
			MLX4_GET(dev_cap->def_mac[i], outbox, QUERY_PORT_MAC_OFFSET);
			MLX4_GET(field32, outbox, QUERY_PORT_TRANS_VENDOR_OFFSET);
			dev_cap->trans_type[i] = field32 >> 24;
			dev_cap->vendor_oui[i] = field32 & 0xffffff;
			MLX4_GET(dev_cap->wavelength[i], outbox, QUERY_PORT_WAVELENGTH_OFFSET);
			MLX4_GET(dev_cap->trans_code[i], outbox, QUERY_PORT_TRANS_CODE_OFFSET);
		}
	}

	mlx4_dbg(dev, "Base MM extensions: flags %08x, rsvd L_Key %08x\n",
		 dev_cap->bmme_flags, dev_cap->reserved_lkey);

	/*
	 * Each UAR has 4 EQ doorbells; so if a UAR is reserved, then
	 * we can't use any EQs whose doorbell falls on that page,
	 * even if the EQ itself isn't reserved.
	 */
	dev_cap->reserved_eqs = max(dev_cap->reserved_uars * 4,
				    dev_cap->reserved_eqs);

	mlx4_dbg(dev, "Max ICM size %lld MB\n",
		 (unsigned long long) dev_cap->max_icm_sz >> 20);
	mlx4_dbg(dev, "Max QPs: %d, reserved QPs: %d, entry size: %d\n",
		 dev_cap->max_qps, dev_cap->reserved_qps, dev_cap->qpc_entry_sz);
	mlx4_dbg(dev, "Max SRQs: %d, reserved SRQs: %d, entry size: %d\n",
		 dev_cap->max_srqs, dev_cap->reserved_srqs, dev_cap->srq_entry_sz);
	mlx4_dbg(dev, "Max CQs: %d, reserved CQs: %d, entry size: %d\n",
		 dev_cap->max_cqs, dev_cap->reserved_cqs, dev_cap->cqc_entry_sz);
	mlx4_dbg(dev, "Max EQs: %d, reserved EQs: %d, entry size: %d\n",
		 dev_cap->max_eqs, dev_cap->reserved_eqs, dev_cap->eqc_entry_sz);
	mlx4_dbg(dev, "reserved MPTs: %d, reserved MTTs: %d\n",
		 dev_cap->reserved_mrws, dev_cap->reserved_mtts);
	mlx4_dbg(dev, "Max PDs: %d, reserved PDs: %d, reserved UARs: %d\n",
		 dev_cap->max_pds, dev_cap->reserved_pds, dev_cap->reserved_uars);
	mlx4_dbg(dev, "Max QP/MCG: %d, reserved MGMs: %d\n",
		 dev_cap->max_pds, dev_cap->reserved_mgms);
	mlx4_dbg(dev, "Max CQEs: %d, max WQEs: %d, max SRQ WQEs: %d\n",
		 dev_cap->max_cq_sz, dev_cap->max_qp_sz, dev_cap->max_srq_sz);
	mlx4_dbg(dev, "Local CA ACK delay: %d, max MTU: %d, port width cap: %d\n",
		 dev_cap->local_ca_ack_delay, 128 << dev_cap->ib_mtu[1],
		 dev_cap->max_port_width[1]);
	mlx4_dbg(dev, "Max SQ desc size: %d, max SQ S/G: %d\n",
		 dev_cap->max_sq_desc_sz, dev_cap->max_sq_sg);
	mlx4_dbg(dev, "Max RQ desc size: %d, max RQ S/G: %d\n",
		 dev_cap->max_rq_desc_sz, dev_cap->max_rq_sg);
	mlx4_dbg(dev, "Max GSO size: %d\n", dev_cap->max_gso_sz);
	mlx4_dbg(dev, "Max counters: %d\n", dev_cap->max_counters);
	mlx4_dbg(dev, "Max RSS Table size: %d\n", dev_cap->max_rss_tbl_sz);

	dump_dev_cap_flags(dev, dev_cap->flags);
	dump_dev_cap_flags2(dev, dev_cap->flags2);

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_QUERY_DEV_CAP_wrapper(struct mlx4_dev *dev, int slave,
			       struct mlx4_vhcr *vhcr,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox,
			       struct mlx4_cmd_info *cmd)
{
	u64	flags;
	int	err = 0;
	u8	field;
	u32	bmme_flags;

	err = mlx4_cmd_box(dev, 0, outbox->dma, 0, 0, MLX4_CMD_QUERY_DEV_CAP,
			   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
	if (err)
		return err;

	/* add port mng change event capability and disable mw type 1
	 * unconditionally to slaves
	 */
	MLX4_GET(flags, outbox->buf, QUERY_DEV_CAP_EXT_FLAGS_OFFSET);
	flags |= MLX4_DEV_CAP_FLAG_PORT_MNG_CHG_EV;
	flags &= ~MLX4_DEV_CAP_FLAG_MEM_WINDOW;
	MLX4_PUT(outbox->buf, flags, QUERY_DEV_CAP_EXT_FLAGS_OFFSET);

	/* For guests, disable timestamp */
	MLX4_GET(field, outbox->buf, QUERY_DEV_CAP_CQ_TS_SUPPORT_OFFSET);
	field &= 0x7f;
	MLX4_PUT(outbox->buf, field, QUERY_DEV_CAP_CQ_TS_SUPPORT_OFFSET);

	/* For guests, report Blueflame disabled */
	MLX4_GET(field, outbox->buf, QUERY_DEV_CAP_BF_OFFSET);
	field &= 0x7f;
	MLX4_PUT(outbox->buf, field, QUERY_DEV_CAP_BF_OFFSET);

	/* For guests, disable mw type 2 */
	MLX4_GET(bmme_flags, outbox, QUERY_DEV_CAP_BMME_FLAGS_OFFSET);
	bmme_flags &= ~MLX4_BMME_FLAG_TYPE_2_WIN;
	MLX4_PUT(outbox->buf, bmme_flags, QUERY_DEV_CAP_BMME_FLAGS_OFFSET);

	/* turn off device-managed steering capability if not enabled */
	if (dev->caps.steering_mode != MLX4_STEERING_MODE_DEVICE_MANAGED) {
		MLX4_GET(field, outbox->buf,
			 QUERY_DEV_CAP_FLOW_STEERING_RANGE_EN_OFFSET);
		field &= 0x7f;
		MLX4_PUT(outbox->buf, field,
			 QUERY_DEV_CAP_FLOW_STEERING_RANGE_EN_OFFSET);
	}
	return 0;
}

int mlx4_QUERY_PORT_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 def_mac;
	u8 port_type;
	u16 short_field;
	int err;
	int admin_link_state;

#define MLX4_VF_PORT_NO_LINK_SENSE_MASK	0xE0
#define MLX4_PORT_LINK_UP_MASK		0x80
#define QUERY_PORT_CUR_MAX_PKEY_OFFSET	0x0c
#define QUERY_PORT_CUR_MAX_GID_OFFSET	0x0e

	err = mlx4_cmd_box(dev, 0, outbox->dma, vhcr->in_modifier, 0,
			   MLX4_CMD_QUERY_PORT, MLX4_CMD_TIME_CLASS_B,
			   MLX4_CMD_NATIVE);

	if (!err && dev->caps.function != slave) {
		def_mac = priv->mfunc.master.vf_oper[slave].vport[vhcr->in_modifier].state.mac;
		MLX4_PUT(outbox->buf, def_mac, QUERY_PORT_MAC_OFFSET);

		/* get port type - currently only eth is enabled */
		MLX4_GET(port_type, outbox->buf,
			 QUERY_PORT_SUPPORTED_TYPE_OFFSET);

		/* No link sensing allowed */
		port_type &= MLX4_VF_PORT_NO_LINK_SENSE_MASK;
		/* set port type to currently operating port type */
		port_type |= (dev->caps.port_type[vhcr->in_modifier] & 0x3);

		admin_link_state = priv->mfunc.master.vf_oper[slave].vport[vhcr->in_modifier].state.link_state;
		if (IFLA_VF_LINK_STATE_ENABLE == admin_link_state)
			port_type |= MLX4_PORT_LINK_UP_MASK;
		else if (IFLA_VF_LINK_STATE_DISABLE == admin_link_state)
			port_type &= ~MLX4_PORT_LINK_UP_MASK;

		MLX4_PUT(outbox->buf, port_type,
			 QUERY_PORT_SUPPORTED_TYPE_OFFSET);

		short_field = 1; /* slave max gids */
		MLX4_PUT(outbox->buf, short_field,
			 QUERY_PORT_CUR_MAX_GID_OFFSET);

		short_field = dev->caps.pkey_table_len[vhcr->in_modifier];
		MLX4_PUT(outbox->buf, short_field,
			 QUERY_PORT_CUR_MAX_PKEY_OFFSET);
	}

	return err;
}

int mlx4_get_slave_pkey_gid_tbl_len(struct mlx4_dev *dev, u8 port,
				    int *gid_tbl_len, int *pkey_tbl_len)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32			*outbox;
	u16			field;
	int			err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err =  mlx4_cmd_box(dev, 0, mailbox->dma, port, 0,
			    MLX4_CMD_QUERY_PORT, MLX4_CMD_TIME_CLASS_B,
			    MLX4_CMD_WRAPPED);
	if (err)
		goto out;

	outbox = mailbox->buf;

	MLX4_GET(field, outbox, QUERY_PORT_CUR_MAX_GID_OFFSET);
	*gid_tbl_len = field;

	MLX4_GET(field, outbox, QUERY_PORT_CUR_MAX_PKEY_OFFSET);
	*pkey_tbl_len = field;

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_get_slave_pkey_gid_tbl_len);

int mlx4_map_cmd(struct mlx4_dev *dev, u16 op, struct mlx4_icm *icm, u64 virt)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_icm_iter iter;
	__be64 *pages;
	int lg;
	int nent = 0;
	int i;
	int err = 0;
	int ts = 0, tc = 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	pages = mailbox->buf;

	for (mlx4_icm_first(icm, &iter);
	     !mlx4_icm_last(&iter);
	     mlx4_icm_next(&iter)) {
		/*
		 * We have to pass pages that are aligned to their
		 * size, so find the least significant 1 in the
		 * address or size and use that as our log2 size.
		 */
		lg = ffs(mlx4_icm_addr(&iter) | mlx4_icm_size(&iter)) - 1;
		if (lg < MLX4_ICM_PAGE_SHIFT) {
			mlx4_warn(dev, "Got FW area not aligned to %d (%llx/%lx).\n",
				   MLX4_ICM_PAGE_SIZE,
				   (unsigned long long) mlx4_icm_addr(&iter),
				   mlx4_icm_size(&iter));
			err = -EINVAL;
			goto out;
		}

		for (i = 0; i < mlx4_icm_size(&iter) >> lg; ++i) {
			if (virt != -1) {
				pages[nent * 2] = cpu_to_be64(virt);
				virt += 1 << lg;
			}

			pages[nent * 2 + 1] =
				cpu_to_be64((mlx4_icm_addr(&iter) + (i << lg)) |
					    (lg - MLX4_ICM_PAGE_SHIFT));
			ts += 1 << (lg - 10);
			++tc;

			if (++nent == MLX4_MAILBOX_SIZE / 16) {
				err = mlx4_cmd(dev, mailbox->dma, nent, 0, op,
						MLX4_CMD_TIME_CLASS_B,
						MLX4_CMD_NATIVE);
				if (err)
					goto out;
				nent = 0;
			}
		}
	}

	if (nent)
		err = mlx4_cmd(dev, mailbox->dma, nent, 0, op,
			       MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);
	if (err)
		goto out;

	switch (op) {
	case MLX4_CMD_MAP_FA:
		mlx4_dbg(dev, "Mapped %d chunks/%d KB for FW.\n", tc, ts);
		break;
	case MLX4_CMD_MAP_ICM_AUX:
		mlx4_dbg(dev, "Mapped %d chunks/%d KB for ICM aux.\n", tc, ts);
		break;
	case MLX4_CMD_MAP_ICM:
		mlx4_dbg(dev, "Mapped %d chunks/%d KB at %llx for ICM.\n",
			  tc, ts, (unsigned long long) virt - (ts << 10));
		break;
	}

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_MAP_FA(struct mlx4_dev *dev, struct mlx4_icm *icm)
{
	return mlx4_map_cmd(dev, MLX4_CMD_MAP_FA, icm, -1);
}

int mlx4_UNMAP_FA(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_UNMAP_FA,
			MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);
}


int mlx4_RUN_FW(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_RUN_FW,
			MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
}

int mlx4_QUERY_FW(struct mlx4_dev *dev)
{
	struct mlx4_fw  *fw  = &mlx4_priv(dev)->fw;
	struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
	struct mlx4_cmd_mailbox *mailbox;
	u32 *outbox;
	int err = 0;
	u64 fw_ver;
	u16 cmd_if_rev;
	u8 lg;

#define QUERY_FW_OUT_SIZE             0x100
#define QUERY_FW_VER_OFFSET            0x00
#define QUERY_FW_PPF_ID		       0x09
#define QUERY_FW_CMD_IF_REV_OFFSET     0x0a
#define QUERY_FW_MAX_CMD_OFFSET        0x0f
#define QUERY_FW_ERR_START_OFFSET      0x30
#define QUERY_FW_ERR_SIZE_OFFSET       0x38
#define QUERY_FW_ERR_BAR_OFFSET        0x3c

#define QUERY_FW_SIZE_OFFSET           0x00
#define QUERY_FW_CLR_INT_BASE_OFFSET   0x20
#define QUERY_FW_CLR_INT_BAR_OFFSET    0x28

#define QUERY_FW_COMM_BASE_OFFSET      0x40
#define QUERY_FW_COMM_BAR_OFFSET       0x48

#define QUERY_FW_CLOCK_OFFSET	       0x50
#define QUERY_FW_CLOCK_BAR	       0x58

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma, 0, 0, MLX4_CMD_QUERY_FW,
			    MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
	if (err)
		goto out;

	MLX4_GET(fw_ver, outbox, QUERY_FW_VER_OFFSET);
	/*
	 * FW subminor version is at more significant bits than minor
	 * version, so swap here.
	 */
	dev->caps.fw_ver = (fw_ver & 0xffff00000000ull) |
		((fw_ver & 0xffff0000ull) >> 16) |
		((fw_ver & 0x0000ffffull) << 16);

	MLX4_GET(lg, outbox, QUERY_FW_PPF_ID);
	dev->caps.function = lg;

	if (mlx4_is_slave(dev))
		goto out;


	MLX4_GET(cmd_if_rev, outbox, QUERY_FW_CMD_IF_REV_OFFSET);
	if (cmd_if_rev < MLX4_COMMAND_INTERFACE_MIN_REV ||
	    cmd_if_rev > MLX4_COMMAND_INTERFACE_MAX_REV) {
		mlx4_err(dev, "Installed FW has unsupported "
			 "command interface revision %d.\n",
			 cmd_if_rev);
		mlx4_err(dev, "(Installed FW version is %d.%d.%03d)\n",
			 (int) (dev->caps.fw_ver >> 32),
			 (int) (dev->caps.fw_ver >> 16) & 0xffff,
			 (int) dev->caps.fw_ver & 0xffff);
		mlx4_err(dev, "This driver version supports only revisions %d to %d.\n",
			 MLX4_COMMAND_INTERFACE_MIN_REV, MLX4_COMMAND_INTERFACE_MAX_REV);
		err = -ENODEV;
		goto out;
	}

	if (cmd_if_rev < MLX4_COMMAND_INTERFACE_NEW_PORT_CMDS)
		dev->flags |= MLX4_FLAG_OLD_PORT_CMDS;

	MLX4_GET(lg, outbox, QUERY_FW_MAX_CMD_OFFSET);
	cmd->max_cmds = 1 << lg;

	mlx4_dbg(dev, "FW version %d.%d.%03d (cmd intf rev %d), max commands %d\n",
		 (int) (dev->caps.fw_ver >> 32),
		 (int) (dev->caps.fw_ver >> 16) & 0xffff,
		 (int) dev->caps.fw_ver & 0xffff,
		 cmd_if_rev, cmd->max_cmds);

	MLX4_GET(fw->catas_offset, outbox, QUERY_FW_ERR_START_OFFSET);
	MLX4_GET(fw->catas_size,   outbox, QUERY_FW_ERR_SIZE_OFFSET);
	MLX4_GET(fw->catas_bar,    outbox, QUERY_FW_ERR_BAR_OFFSET);
	fw->catas_bar = (fw->catas_bar >> 6) * 2;

	mlx4_dbg(dev, "Catastrophic error buffer at 0x%llx, size 0x%x, BAR %d\n",
		 (unsigned long long) fw->catas_offset, fw->catas_size, fw->catas_bar);

	MLX4_GET(fw->fw_pages,     outbox, QUERY_FW_SIZE_OFFSET);
	MLX4_GET(fw->clr_int_base, outbox, QUERY_FW_CLR_INT_BASE_OFFSET);
	MLX4_GET(fw->clr_int_bar,  outbox, QUERY_FW_CLR_INT_BAR_OFFSET);
	fw->clr_int_bar = (fw->clr_int_bar >> 6) * 2;

	MLX4_GET(fw->comm_base, outbox, QUERY_FW_COMM_BASE_OFFSET);
	MLX4_GET(fw->comm_bar,  outbox, QUERY_FW_COMM_BAR_OFFSET);
	fw->comm_bar = (fw->comm_bar >> 6) * 2;
	mlx4_dbg(dev, "Communication vector bar:%d offset:0x%llx\n",
		 fw->comm_bar, fw->comm_base);
	mlx4_dbg(dev, "FW size %d KB\n", fw->fw_pages >> 2);

	MLX4_GET(fw->clock_offset, outbox, QUERY_FW_CLOCK_OFFSET);
	MLX4_GET(fw->clock_bar,    outbox, QUERY_FW_CLOCK_BAR);
	fw->clock_bar = (fw->clock_bar >> 6) * 2;
	mlx4_dbg(dev, "Internal clock bar:%d offset:0x%llx\n",
		 fw->clock_bar, fw->clock_offset);

	/*
	 * Round up number of system pages needed in case
	 * MLX4_ICM_PAGE_SIZE < PAGE_SIZE.
	 */
	fw->fw_pages =
		ALIGN(fw->fw_pages, PAGE_SIZE / MLX4_ICM_PAGE_SIZE) >>
		(PAGE_SHIFT - MLX4_ICM_PAGE_SHIFT);

	mlx4_dbg(dev, "Clear int @ %llx, BAR %d\n",
		 (unsigned long long) fw->clr_int_base, fw->clr_int_bar);

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_QUERY_FW_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	u8 *outbuf;
	int err;

	outbuf = outbox->buf;
	err = mlx4_cmd_box(dev, 0, outbox->dma, 0, 0, MLX4_CMD_QUERY_FW,
			    MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
	if (err)
		return err;

	/* for slaves, set pci PPF ID to invalid and zero out everything
	 * else except FW version */
	outbuf[0] = outbuf[1] = 0;
	memset(&outbuf[8], 0, QUERY_FW_OUT_SIZE - 8);
	outbuf[QUERY_FW_PPF_ID] = MLX4_INVALID_SLAVE_ID;

	return 0;
}

static void get_board_id(void *vsd, char *board_id)
{
	int i;

#define VSD_OFFSET_SIG1		0x00
#define VSD_OFFSET_SIG2		0xde
#define VSD_OFFSET_MLX_BOARD_ID	0xd0
#define VSD_OFFSET_TS_BOARD_ID	0x20

#define VSD_SIGNATURE_TOPSPIN	0x5ad

	memset(board_id, 0, MLX4_BOARD_ID_LEN);

	if (be16_to_cpup(vsd + VSD_OFFSET_SIG1) == VSD_SIGNATURE_TOPSPIN &&
	    be16_to_cpup(vsd + VSD_OFFSET_SIG2) == VSD_SIGNATURE_TOPSPIN) {
		strlcpy(board_id, vsd + VSD_OFFSET_TS_BOARD_ID, MLX4_BOARD_ID_LEN);
	} else {
		/*
		 * The board ID is a string but the firmware byte
		 * swaps each 4-byte word before passing it back to
		 * us.  Therefore we need to swab it before printing.
		 */
		for (i = 0; i < 4; ++i)
			((u32 *) board_id)[i] =
				swab32(*(u32 *) (vsd + VSD_OFFSET_MLX_BOARD_ID + i * 4));
	}
}

int mlx4_QUERY_ADAPTER(struct mlx4_dev *dev, struct mlx4_adapter *adapter)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *outbox;
	int err;

#define QUERY_ADAPTER_OUT_SIZE             0x100
#define QUERY_ADAPTER_INTA_PIN_OFFSET      0x10
#define QUERY_ADAPTER_VSD_OFFSET           0x20

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma, 0, 0, MLX4_CMD_QUERY_ADAPTER,
			   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
	if (err)
		goto out;

	MLX4_GET(adapter->inta_pin, outbox,    QUERY_ADAPTER_INTA_PIN_OFFSET);

	get_board_id(outbox + QUERY_ADAPTER_VSD_OFFSET / 4,
		     adapter->board_id);

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_INIT_HCA(struct mlx4_dev *dev, struct mlx4_init_hca_param *param)
{
	struct mlx4_cmd_mailbox *mailbox;
	__be32 *inbox;
	int err;

#define INIT_HCA_IN_SIZE		 0x200
#define INIT_HCA_VERSION_OFFSET		 0x000
#define	 INIT_HCA_VERSION		 2
#define INIT_HCA_CACHELINE_SZ_OFFSET	 0x0e
#define INIT_HCA_FLAGS_OFFSET		 0x014
#define INIT_HCA_QPC_OFFSET		 0x020
#define	 INIT_HCA_QPC_BASE_OFFSET	 (INIT_HCA_QPC_OFFSET + 0x10)
#define	 INIT_HCA_LOG_QP_OFFSET		 (INIT_HCA_QPC_OFFSET + 0x17)
#define	 INIT_HCA_SRQC_BASE_OFFSET	 (INIT_HCA_QPC_OFFSET + 0x28)
#define	 INIT_HCA_LOG_SRQ_OFFSET	 (INIT_HCA_QPC_OFFSET + 0x2f)
#define	 INIT_HCA_CQC_BASE_OFFSET	 (INIT_HCA_QPC_OFFSET + 0x30)
#define	 INIT_HCA_LOG_CQ_OFFSET		 (INIT_HCA_QPC_OFFSET + 0x37)
#define	 INIT_HCA_EQE_CQE_OFFSETS	 (INIT_HCA_QPC_OFFSET + 0x38)
#define	 INIT_HCA_ALTC_BASE_OFFSET	 (INIT_HCA_QPC_OFFSET + 0x40)
#define	 INIT_HCA_AUXC_BASE_OFFSET	 (INIT_HCA_QPC_OFFSET + 0x50)
#define	 INIT_HCA_EQC_BASE_OFFSET	 (INIT_HCA_QPC_OFFSET + 0x60)
#define	 INIT_HCA_LOG_EQ_OFFSET		 (INIT_HCA_QPC_OFFSET + 0x67)
#define	 INIT_HCA_RDMARC_BASE_OFFSET	 (INIT_HCA_QPC_OFFSET + 0x70)
#define	 INIT_HCA_LOG_RD_OFFSET		 (INIT_HCA_QPC_OFFSET + 0x77)
#define INIT_HCA_MCAST_OFFSET		 0x0c0
#define	 INIT_HCA_MC_BASE_OFFSET	 (INIT_HCA_MCAST_OFFSET + 0x00)
#define	 INIT_HCA_LOG_MC_ENTRY_SZ_OFFSET (INIT_HCA_MCAST_OFFSET + 0x12)
#define	 INIT_HCA_LOG_MC_HASH_SZ_OFFSET	 (INIT_HCA_MCAST_OFFSET + 0x16)
#define  INIT_HCA_UC_STEERING_OFFSET	 (INIT_HCA_MCAST_OFFSET + 0x18)
#define	 INIT_HCA_LOG_MC_TABLE_SZ_OFFSET (INIT_HCA_MCAST_OFFSET + 0x1b)
#define  INIT_HCA_DEVICE_MANAGED_FLOW_STEERING_EN	0x6
#define  INIT_HCA_FS_PARAM_OFFSET         0x1d0
#define  INIT_HCA_FS_BASE_OFFSET          (INIT_HCA_FS_PARAM_OFFSET + 0x00)
#define  INIT_HCA_FS_LOG_ENTRY_SZ_OFFSET  (INIT_HCA_FS_PARAM_OFFSET + 0x12)
#define  INIT_HCA_FS_LOG_TABLE_SZ_OFFSET  (INIT_HCA_FS_PARAM_OFFSET + 0x1b)
#define  INIT_HCA_FS_ETH_BITS_OFFSET      (INIT_HCA_FS_PARAM_OFFSET + 0x21)
#define  INIT_HCA_FS_ETH_NUM_ADDRS_OFFSET (INIT_HCA_FS_PARAM_OFFSET + 0x22)
#define  INIT_HCA_FS_IB_BITS_OFFSET       (INIT_HCA_FS_PARAM_OFFSET + 0x25)
#define  INIT_HCA_FS_IB_NUM_ADDRS_OFFSET  (INIT_HCA_FS_PARAM_OFFSET + 0x26)
#define INIT_HCA_TPT_OFFSET		 0x0f0
#define	 INIT_HCA_DMPT_BASE_OFFSET	 (INIT_HCA_TPT_OFFSET + 0x00)
#define  INIT_HCA_TPT_MW_OFFSET		 (INIT_HCA_TPT_OFFSET + 0x08)
#define	 INIT_HCA_LOG_MPT_SZ_OFFSET	 (INIT_HCA_TPT_OFFSET + 0x0b)
#define	 INIT_HCA_MTT_BASE_OFFSET	 (INIT_HCA_TPT_OFFSET + 0x10)
#define	 INIT_HCA_CMPT_BASE_OFFSET	 (INIT_HCA_TPT_OFFSET + 0x18)
#define INIT_HCA_UAR_OFFSET		 0x120
#define	 INIT_HCA_LOG_UAR_SZ_OFFSET	 (INIT_HCA_UAR_OFFSET + 0x0a)
#define  INIT_HCA_UAR_PAGE_SZ_OFFSET     (INIT_HCA_UAR_OFFSET + 0x0b)

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	inbox = mailbox->buf;

	*((u8 *) mailbox->buf + INIT_HCA_VERSION_OFFSET) = INIT_HCA_VERSION;

	*((u8 *) mailbox->buf + INIT_HCA_CACHELINE_SZ_OFFSET) =
		(ilog2(cache_line_size()) - 4) << 5;

#if defined(__LITTLE_ENDIAN)
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) &= ~cpu_to_be32(1 << 1);
#elif defined(__BIG_ENDIAN)
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 1);
#else
#error Host endianness not defined
#endif
	/* Check port for UD address vector: */
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1);

	/* Enable IPoIB checksumming if we can: */
	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_IPOIB_CSUM)
		*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 3);

	/* Enable QoS support if module parameter set */
	if (enable_qos)
		*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 2);

	/* enable counters */
	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_COUNTERS)
		*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 4);

	/* CX3 is capable of extending CQEs/EQEs from 32 to 64 bytes */
	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_64B_EQE) {
		*(inbox + INIT_HCA_EQE_CQE_OFFSETS / 4) |= cpu_to_be32(1 << 29);
		dev->caps.eqe_size   = 64;
		dev->caps.eqe_factor = 1;
	} else {
		dev->caps.eqe_size   = 32;
		dev->caps.eqe_factor = 0;
	}

	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_64B_CQE) {
		*(inbox + INIT_HCA_EQE_CQE_OFFSETS / 4) |= cpu_to_be32(1 << 30);
		dev->caps.cqe_size   = 64;
		dev->caps.userspace_caps |= MLX4_USER_DEV_CAP_64B_CQE;
	} else {
		dev->caps.cqe_size   = 32;
	}

	/* QPC/EEC/CQC/EQC/RDMARC attributes */

	MLX4_PUT(inbox, param->qpc_base,      INIT_HCA_QPC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_num_qps,   INIT_HCA_LOG_QP_OFFSET);
	MLX4_PUT(inbox, param->srqc_base,     INIT_HCA_SRQC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_num_srqs,  INIT_HCA_LOG_SRQ_OFFSET);
	MLX4_PUT(inbox, param->cqc_base,      INIT_HCA_CQC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_num_cqs,   INIT_HCA_LOG_CQ_OFFSET);
	MLX4_PUT(inbox, param->altc_base,     INIT_HCA_ALTC_BASE_OFFSET);
	MLX4_PUT(inbox, param->auxc_base,     INIT_HCA_AUXC_BASE_OFFSET);
	MLX4_PUT(inbox, param->eqc_base,      INIT_HCA_EQC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_num_eqs,   INIT_HCA_LOG_EQ_OFFSET);
	MLX4_PUT(inbox, param->rdmarc_base,   INIT_HCA_RDMARC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_rd_per_qp, INIT_HCA_LOG_RD_OFFSET);

	/* steering attributes */
	if (dev->caps.steering_mode ==
	    MLX4_STEERING_MODE_DEVICE_MANAGED) {
		*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |=
			cpu_to_be32(1 <<
				    INIT_HCA_DEVICE_MANAGED_FLOW_STEERING_EN);

		MLX4_PUT(inbox, param->mc_base, INIT_HCA_FS_BASE_OFFSET);
		MLX4_PUT(inbox, param->log_mc_entry_sz,
			 INIT_HCA_FS_LOG_ENTRY_SZ_OFFSET);
		MLX4_PUT(inbox, param->log_mc_table_sz,
			 INIT_HCA_FS_LOG_TABLE_SZ_OFFSET);
		/* Enable Ethernet flow steering
		 * with udp unicast and tcp unicast
		 */
		MLX4_PUT(inbox, (u8) (MLX4_FS_UDP_UC_EN | MLX4_FS_TCP_UC_EN),
			 INIT_HCA_FS_ETH_BITS_OFFSET);
		MLX4_PUT(inbox, (u16) MLX4_FS_NUM_OF_L2_ADDR,
			 INIT_HCA_FS_ETH_NUM_ADDRS_OFFSET);
		/* Enable IPoIB flow steering
		 * with udp unicast and tcp unicast
		 */
		MLX4_PUT(inbox, (u8) (MLX4_FS_UDP_UC_EN | MLX4_FS_TCP_UC_EN),
			 INIT_HCA_FS_IB_BITS_OFFSET);
		MLX4_PUT(inbox, (u16) MLX4_FS_NUM_OF_L2_ADDR,
			 INIT_HCA_FS_IB_NUM_ADDRS_OFFSET);
	} else {
		MLX4_PUT(inbox, param->mc_base,	INIT_HCA_MC_BASE_OFFSET);
		MLX4_PUT(inbox, param->log_mc_entry_sz,
			 INIT_HCA_LOG_MC_ENTRY_SZ_OFFSET);
		MLX4_PUT(inbox, param->log_mc_hash_sz,
			 INIT_HCA_LOG_MC_HASH_SZ_OFFSET);
		MLX4_PUT(inbox, param->log_mc_table_sz,
			 INIT_HCA_LOG_MC_TABLE_SZ_OFFSET);
		if (dev->caps.steering_mode == MLX4_STEERING_MODE_B0)
			MLX4_PUT(inbox, (u8) (1 << 3),
				 INIT_HCA_UC_STEERING_OFFSET);
	}

	/* TPT attributes */

	MLX4_PUT(inbox, param->dmpt_base,  INIT_HCA_DMPT_BASE_OFFSET);
	MLX4_PUT(inbox, param->mw_enabled, INIT_HCA_TPT_MW_OFFSET);
	MLX4_PUT(inbox, param->log_mpt_sz, INIT_HCA_LOG_MPT_SZ_OFFSET);
	MLX4_PUT(inbox, param->mtt_base,   INIT_HCA_MTT_BASE_OFFSET);
	MLX4_PUT(inbox, param->cmpt_base,  INIT_HCA_CMPT_BASE_OFFSET);

	/* UAR attributes */

	MLX4_PUT(inbox, param->uar_page_sz,	INIT_HCA_UAR_PAGE_SZ_OFFSET);
	MLX4_PUT(inbox, param->log_uar_sz,      INIT_HCA_LOG_UAR_SZ_OFFSET);

	err = mlx4_cmd(dev, mailbox->dma, 0, 0, MLX4_CMD_INIT_HCA, 10000,
		       MLX4_CMD_NATIVE);

	if (err)
		mlx4_err(dev, "INIT_HCA returns %d\n", err);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_QUERY_HCA(struct mlx4_dev *dev,
		   struct mlx4_init_hca_param *param)
{
	struct mlx4_cmd_mailbox *mailbox;
	__be32 *outbox;
	u32 dword_field;
	int err;
	u8 byte_field;

#define QUERY_HCA_GLOBAL_CAPS_OFFSET	0x04
#define QUERY_HCA_CORE_CLOCK_OFFSET	0x0c

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma, 0, 0,
			   MLX4_CMD_QUERY_HCA,
			   MLX4_CMD_TIME_CLASS_B,
			   !mlx4_is_slave(dev));
	if (err)
		goto out;

	MLX4_GET(param->global_caps, outbox, QUERY_HCA_GLOBAL_CAPS_OFFSET);
	MLX4_GET(param->hca_core_clock, outbox, QUERY_HCA_CORE_CLOCK_OFFSET);

	/* QPC/EEC/CQC/EQC/RDMARC attributes */

	MLX4_GET(param->qpc_base,      outbox, INIT_HCA_QPC_BASE_OFFSET);
	MLX4_GET(param->log_num_qps,   outbox, INIT_HCA_LOG_QP_OFFSET);
	MLX4_GET(param->srqc_base,     outbox, INIT_HCA_SRQC_BASE_OFFSET);
	MLX4_GET(param->log_num_srqs,  outbox, INIT_HCA_LOG_SRQ_OFFSET);
	MLX4_GET(param->cqc_base,      outbox, INIT_HCA_CQC_BASE_OFFSET);
	MLX4_GET(param->log_num_cqs,   outbox, INIT_HCA_LOG_CQ_OFFSET);
	MLX4_GET(param->altc_base,     outbox, INIT_HCA_ALTC_BASE_OFFSET);
	MLX4_GET(param->auxc_base,     outbox, INIT_HCA_AUXC_BASE_OFFSET);
	MLX4_GET(param->eqc_base,      outbox, INIT_HCA_EQC_BASE_OFFSET);
	MLX4_GET(param->log_num_eqs,   outbox, INIT_HCA_LOG_EQ_OFFSET);
	MLX4_GET(param->rdmarc_base,   outbox, INIT_HCA_RDMARC_BASE_OFFSET);
	MLX4_GET(param->log_rd_per_qp, outbox, INIT_HCA_LOG_RD_OFFSET);

	MLX4_GET(dword_field, outbox, INIT_HCA_FLAGS_OFFSET);
	if (dword_field & (1 << INIT_HCA_DEVICE_MANAGED_FLOW_STEERING_EN)) {
		param->steering_mode = MLX4_STEERING_MODE_DEVICE_MANAGED;
	} else {
		MLX4_GET(byte_field, outbox, INIT_HCA_UC_STEERING_OFFSET);
		if (byte_field & 0x8)
			param->steering_mode = MLX4_STEERING_MODE_B0;
		else
			param->steering_mode = MLX4_STEERING_MODE_A0;
	}
	/* steering attributes */
	if (param->steering_mode == MLX4_STEERING_MODE_DEVICE_MANAGED) {
		MLX4_GET(param->mc_base, outbox, INIT_HCA_FS_BASE_OFFSET);
		MLX4_GET(param->log_mc_entry_sz, outbox,
			 INIT_HCA_FS_LOG_ENTRY_SZ_OFFSET);
		MLX4_GET(param->log_mc_table_sz, outbox,
			 INIT_HCA_FS_LOG_TABLE_SZ_OFFSET);
	} else {
		MLX4_GET(param->mc_base, outbox, INIT_HCA_MC_BASE_OFFSET);
		MLX4_GET(param->log_mc_entry_sz, outbox,
			 INIT_HCA_LOG_MC_ENTRY_SZ_OFFSET);
		MLX4_GET(param->log_mc_hash_sz,  outbox,
			 INIT_HCA_LOG_MC_HASH_SZ_OFFSET);
		MLX4_GET(param->log_mc_table_sz, outbox,
			 INIT_HCA_LOG_MC_TABLE_SZ_OFFSET);
	}

	/* CX3 is capable of extending CQEs/EQEs from 32 to 64 bytes */
	MLX4_GET(byte_field, outbox, INIT_HCA_EQE_CQE_OFFSETS);
	if (byte_field & 0x20) /* 64-bytes eqe enabled */
		param->dev_cap_enabled |= MLX4_DEV_CAP_64B_EQE_ENABLED;
	if (byte_field & 0x40) /* 64-bytes cqe enabled */
		param->dev_cap_enabled |= MLX4_DEV_CAP_64B_CQE_ENABLED;

	/* TPT attributes */

	MLX4_GET(param->dmpt_base,  outbox, INIT_HCA_DMPT_BASE_OFFSET);
	MLX4_GET(param->mw_enabled, outbox, INIT_HCA_TPT_MW_OFFSET);
	MLX4_GET(param->log_mpt_sz, outbox, INIT_HCA_LOG_MPT_SZ_OFFSET);
	MLX4_GET(param->mtt_base,   outbox, INIT_HCA_MTT_BASE_OFFSET);
	MLX4_GET(param->cmpt_base,  outbox, INIT_HCA_CMPT_BASE_OFFSET);

	/* UAR attributes */

	MLX4_GET(param->uar_page_sz, outbox, INIT_HCA_UAR_PAGE_SZ_OFFSET);
	MLX4_GET(param->log_uar_sz, outbox, INIT_HCA_LOG_UAR_SZ_OFFSET);

out:
	mlx4_free_cmd_mailbox(dev, mailbox);

	return err;
}

/* for IB-type ports only in SRIOV mode. Checks that both proxy QP0
 * and real QP0 are active, so that the paravirtualized QP0 is ready
 * to operate */
static int check_qp0_state(struct mlx4_dev *dev, int function, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	/* irrelevant if not infiniband */
	if (priv->mfunc.master.qp0_state[port].proxy_qp0_active &&
	    priv->mfunc.master.qp0_state[port].qp0_active)
		return 1;
	return 0;
}

int mlx4_INIT_PORT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int port = vhcr->in_modifier;
	int err;

	if (priv->mfunc.master.slave_state[slave].init_port_mask & (1 << port))
		return 0;

	if (dev->caps.port_mask[port] != MLX4_PORT_TYPE_IB) {
		/* Enable port only if it was previously disabled */
		if (!priv->mfunc.master.init_port_ref[port]) {
			err = mlx4_cmd(dev, 0, port, 0, MLX4_CMD_INIT_PORT,
				       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
			if (err)
				return err;
		}
		priv->mfunc.master.slave_state[slave].init_port_mask |= (1 << port);
	} else {
		if (slave == mlx4_master_func_num(dev)) {
			if (check_qp0_state(dev, slave, port) &&
			    !priv->mfunc.master.qp0_state[port].port_active) {
				err = mlx4_cmd(dev, 0, port, 0, MLX4_CMD_INIT_PORT,
					       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
				if (err)
					return err;
				priv->mfunc.master.qp0_state[port].port_active = 1;
				priv->mfunc.master.slave_state[slave].init_port_mask |= (1 << port);
			}
		} else
			priv->mfunc.master.slave_state[slave].init_port_mask |= (1 << port);
	}
	++priv->mfunc.master.init_port_ref[port];
	return 0;
}

int mlx4_INIT_PORT(struct mlx4_dev *dev, int port)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *inbox;
	int err;
	u32 flags;
	u16 field;

	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
#define INIT_PORT_IN_SIZE          256
#define INIT_PORT_FLAGS_OFFSET     0x00
#define INIT_PORT_FLAG_SIG         (1 << 18)
#define INIT_PORT_FLAG_NG          (1 << 17)
#define INIT_PORT_FLAG_G0          (1 << 16)
#define INIT_PORT_VL_SHIFT         4
#define INIT_PORT_PORT_WIDTH_SHIFT 8
#define INIT_PORT_MTU_OFFSET       0x04
#define INIT_PORT_MAX_GID_OFFSET   0x06
#define INIT_PORT_MAX_PKEY_OFFSET  0x0a
#define INIT_PORT_GUID0_OFFSET     0x10
#define INIT_PORT_NODE_GUID_OFFSET 0x18
#define INIT_PORT_SI_GUID_OFFSET   0x20

		mailbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(mailbox))
			return PTR_ERR(mailbox);
		inbox = mailbox->buf;

		flags = 0;
		flags |= (dev->caps.vl_cap[port] & 0xf) << INIT_PORT_VL_SHIFT;
		flags |= (dev->caps.port_width_cap[port] & 0xf) << INIT_PORT_PORT_WIDTH_SHIFT;
		MLX4_PUT(inbox, flags,		  INIT_PORT_FLAGS_OFFSET);

		field = 128 << dev->caps.ib_mtu_cap[port];
		MLX4_PUT(inbox, field, INIT_PORT_MTU_OFFSET);
		field = dev->caps.gid_table_len[port];
		MLX4_PUT(inbox, field, INIT_PORT_MAX_GID_OFFSET);
		field = dev->caps.pkey_table_len[port];
		MLX4_PUT(inbox, field, INIT_PORT_MAX_PKEY_OFFSET);

		err = mlx4_cmd(dev, mailbox->dma, port, 0, MLX4_CMD_INIT_PORT,
			       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);

		mlx4_free_cmd_mailbox(dev, mailbox);
	} else
		err = mlx4_cmd(dev, 0, port, 0, MLX4_CMD_INIT_PORT,
			       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);

	return err;
}
EXPORT_SYMBOL_GPL(mlx4_INIT_PORT);

int mlx4_CLOSE_PORT_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int port = vhcr->in_modifier;
	int err;

	if (!(priv->mfunc.master.slave_state[slave].init_port_mask &
	    (1 << port)))
		return 0;

	if (dev->caps.port_mask[port] != MLX4_PORT_TYPE_IB) {
		if (priv->mfunc.master.init_port_ref[port] == 1) {
			err = mlx4_cmd(dev, 0, port, 0, MLX4_CMD_CLOSE_PORT,
				       1000, MLX4_CMD_NATIVE);
			if (err)
				return err;
		}
		priv->mfunc.master.slave_state[slave].init_port_mask &= ~(1 << port);
	} else {
		/* infiniband port */
		if (slave == mlx4_master_func_num(dev)) {
			if (!priv->mfunc.master.qp0_state[port].qp0_active &&
			    priv->mfunc.master.qp0_state[port].port_active) {
				err = mlx4_cmd(dev, 0, port, 0, MLX4_CMD_CLOSE_PORT,
					       1000, MLX4_CMD_NATIVE);
				if (err)
					return err;
				priv->mfunc.master.slave_state[slave].init_port_mask &= ~(1 << port);
				priv->mfunc.master.qp0_state[port].port_active = 0;
			}
		} else
			priv->mfunc.master.slave_state[slave].init_port_mask &= ~(1 << port);
	}
	--priv->mfunc.master.init_port_ref[port];
	return 0;
}

int mlx4_CLOSE_PORT(struct mlx4_dev *dev, int port)
{
	return mlx4_cmd(dev, 0, port, 0, MLX4_CMD_CLOSE_PORT, 1000,
			MLX4_CMD_WRAPPED);
}
EXPORT_SYMBOL_GPL(mlx4_CLOSE_PORT);

int mlx4_CLOSE_HCA(struct mlx4_dev *dev, int panic)
{
	return mlx4_cmd(dev, 0, 0, panic, MLX4_CMD_CLOSE_HCA, 1000,
			MLX4_CMD_NATIVE);
}

int mlx4_SET_ICM_SIZE(struct mlx4_dev *dev, u64 icm_size, u64 *aux_pages)
{
	int ret = mlx4_cmd_imm(dev, icm_size, aux_pages, 0, 0,
			       MLX4_CMD_SET_ICM_SIZE,
			       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
	if (ret)
		return ret;

	/*
	 * Round up number of system pages needed in case
	 * MLX4_ICM_PAGE_SIZE < PAGE_SIZE.
	 */
	*aux_pages = ALIGN(*aux_pages, PAGE_SIZE / MLX4_ICM_PAGE_SIZE) >>
		(PAGE_SHIFT - MLX4_ICM_PAGE_SHIFT);

	return 0;
}

int mlx4_NOP(struct mlx4_dev *dev)
{
	/* Input modifier of 0x1f means "finish as soon as possible." */
	return mlx4_cmd(dev, 0, 0x1f, 0, MLX4_CMD_NOP, 100, MLX4_CMD_NATIVE);
}

#define MLX4_WOL_SETUP_MODE (5 << 28)
int mlx4_wol_read(struct mlx4_dev *dev, u64 *config, int port)
{
	u32 in_mod = MLX4_WOL_SETUP_MODE | port << 8;

	return mlx4_cmd_imm(dev, 0, config, in_mod, 0x3,
			    MLX4_CMD_MOD_STAT_CFG, MLX4_CMD_TIME_CLASS_A,
			    MLX4_CMD_NATIVE);
}
EXPORT_SYMBOL_GPL(mlx4_wol_read);

int mlx4_wol_write(struct mlx4_dev *dev, u64 config, int port)
{
	u32 in_mod = MLX4_WOL_SETUP_MODE | port << 8;

	return mlx4_cmd(dev, config, in_mod, 0x1, MLX4_CMD_MOD_STAT_CFG,
			MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
}
EXPORT_SYMBOL_GPL(mlx4_wol_write);

enum {
	ADD_TO_MCG = 0x26,
};


void mlx4_opreq_action(struct work_struct *work)
{
	struct mlx4_priv *priv = container_of(work, struct mlx4_priv,
					      opreq_task);
	struct mlx4_dev *dev = &priv->dev;
	int num_tasks = atomic_read(&priv->opreq_count);
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mgm *mgm;
	u32 *outbox;
	u32 modifier;
	u16 token;
	u16 type;
	int err;
	u32 num_qps;
	struct mlx4_qp qp;
	int i;
	u8 rem_mcg;
	u8 prot;

#define GET_OP_REQ_MODIFIER_OFFSET	0x08
#define GET_OP_REQ_TOKEN_OFFSET		0x14
#define GET_OP_REQ_TYPE_OFFSET		0x1a
#define GET_OP_REQ_DATA_OFFSET		0x20

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		mlx4_err(dev, "Failed to allocate mailbox for GET_OP_REQ\n");
		return;
	}
	outbox = mailbox->buf;

	while (num_tasks) {
		err = mlx4_cmd_box(dev, 0, mailbox->dma, 0, 0,
				   MLX4_CMD_GET_OP_REQ, MLX4_CMD_TIME_CLASS_A,
				   MLX4_CMD_NATIVE);
		if (err) {
			mlx4_err(dev, "Failed to retrieve required operation: %d\n",
				 err);
			return;
		}
		MLX4_GET(modifier, outbox, GET_OP_REQ_MODIFIER_OFFSET);
		MLX4_GET(token, outbox, GET_OP_REQ_TOKEN_OFFSET);
		MLX4_GET(type, outbox, GET_OP_REQ_TYPE_OFFSET);
		type &= 0xfff;

		switch (type) {
		case ADD_TO_MCG:
			if (dev->caps.steering_mode ==
			    MLX4_STEERING_MODE_DEVICE_MANAGED) {
				mlx4_warn(dev, "ADD MCG operation is not supported in DEVICE_MANAGED steering mode\n");
				err = EPERM;
				break;
			}
			mgm = (struct mlx4_mgm *)((u8 *)(outbox) +
						  GET_OP_REQ_DATA_OFFSET);
			num_qps = be32_to_cpu(mgm->members_count) &
				  MGM_QPN_MASK;
			rem_mcg = ((u8 *)(&mgm->members_count))[0] & 1;
			prot = ((u8 *)(&mgm->members_count))[0] >> 6;

			for (i = 0; i < num_qps; i++) {
				qp.qpn = be32_to_cpu(mgm->qp[i]);
				if (rem_mcg)
					err = mlx4_multicast_detach(dev, &qp,
								    mgm->gid,
								    prot, 0);
				else
					err = mlx4_multicast_attach(dev, &qp,
								    mgm->gid,
								    mgm->gid[5]
								    , 0, prot,
								    NULL);
				if (err)
					break;
			}
			break;
		default:
			mlx4_warn(dev, "Bad type for required operation\n");
			err = EINVAL;
			break;
		}
		err = mlx4_cmd(dev, 0, ((u32) err | cpu_to_be32(token) << 16),
			       1, MLX4_CMD_GET_OP_REQ, MLX4_CMD_TIME_CLASS_A,
			       MLX4_CMD_NATIVE);
		if (err) {
			mlx4_err(dev, "Failed to acknowledge required request: %d\n",
				 err);
			goto out;
		}
		memset(outbox, 0, 0xffc);
		num_tasks = atomic_dec_return(&priv->opreq_count);
	}

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
}

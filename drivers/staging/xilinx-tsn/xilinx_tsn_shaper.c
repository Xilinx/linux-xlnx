// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx FPGA Xilinx TSN QBV sheduler module.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Author: Syed S <syeds@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "xilinx_axienet_tsn.h"
#include "xilinx_tsn_shaper.h"
#include <net/pkt_sched.h>

/* Total number of TAS GCL entries */
#define XLNX_TAPRIO_NUM_GCL			256

/* Maximum supported cycle time in nanoseconds */
#define XLNX_TAPRIO_MAX_CYCLE_TIME_NS		(BIT(30) - 1)

int axienet_qbv_enabled(struct axienet_local *lp)
{
	return axienet_qbv_ior(lp, CONFIG_CHANGE) & CC_ADMIN_GATE_ENABLE_BIT;
}

static inline int axienet_map_gs_to_hw(struct axienet_local *lp, u32 gs)
{
	u8 be_queue = 0;
	u8 re_queue = 1;
	u8 st_queue = 2;
	unsigned int acl_bit_map = 0;

	if (lp->num_tc == XAE_MIN_LEGACY_TSN_TC)
		st_queue = 1;

	if (gs & GS_BE_OPEN)
		acl_bit_map |= (1 << be_queue);
	if (gs & GS_ST_OPEN)
		acl_bit_map |= (1 << st_queue);
	if (lp->num_tc == XAE_MAX_LEGACY_TSN_TC && (gs & GS_RE_OPEN))
		acl_bit_map |= (1 << re_queue);

	return acl_bit_map;
}

static int validate_tsn_preemption(struct net_device *ndev,
				   struct tc_mqprio_qopt_offload *mqprio)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (!mqprio->preemptible_tcs)
		return 0;

	if (!(lp->abl_reg & TSN_FRAME_PREEMPTION_EN)) {
		netdev_err(ndev, "Preemption not enabled in hardware ability 0x%x\n",
			   lp->abl_reg);
		return -EOPNOTSUPP;
	}

	if (mqprio->qopt.num_tc <= XAE_MAX_LEGACY_TSN_TC) {
		netdev_err(ndev, "Preemption needs num_tc > %d\n",
			   XAE_MAX_LEGACY_TSN_TC);
		return -EOPNOTSUPP;
	}

	if (!xlnx_switch_per_mac_preemption_enabled()) {
		netdev_err(ndev, "Per-MAC preemption not supported\n");
		return -EOPNOTSUPP;
	}

	if (mqprio->qopt.num_tc > lp->num_tc) {
		netdev_err(ndev, "Requested number of TCs exceeds supported limit %d\n",
			   lp->num_tc);
		return -EINVAL;
	}

	if (mqprio->preemptible_tcs & ~GENMASK(lp->num_tc - 1, 0)) {
		netdev_err(ndev, "preemptible_tcs has bits set above num_tc\n");
		return -EINVAL;
	}

	return 0;
}

static void tsn_reset_tc_mqprio(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);

	netdev_reset_tc(ndev);

	if (lp->tsn_has_preemption) {
		axienet_iow(lp, XAE_QBU_USER_QUEUE_MAP_OFFSET, XAE_QBU_RESET_VALUE);
		lp->tsn_has_preemption = false;
	}
}

static int tsn_setup_shaper_tc_mqprio(struct net_device *ndev,
				      void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio = type_data;
	struct axienet_local *lp = netdev_priv(ndev);
	int err;
	u8 i;

	if (!mqprio->qopt.num_tc) {
		tsn_reset_tc_mqprio(ndev);
		return 0;
	}

	err = validate_tsn_preemption(ndev, mqprio);
	if (err)
		return err;

	err = netdev_set_num_tc(ndev, mqprio->qopt.num_tc);
	if (err)
		return err;

	for (i = 0; i < mqprio->qopt.num_tc; ++i) {
		err = netdev_set_tc_queue(ndev, i, 1, i);
		if (err) {
			netdev_reset_tc(ndev);
			return err;
		}
	}

	if (mqprio->preemptible_tcs) {
		axienet_iow(lp, XAE_QBU_USER_QUEUE_MAP_OFFSET,
			    (u32)mqprio->preemptible_tcs);
		lp->tsn_has_preemption = true;
	}

	return 0;
}

static int validate_taprio_qopt(struct net_device *ndev,
				struct tc_taprio_qopt_offload *qopt)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u32 i = 0, max_tc = 0;
	u64 total_time = 0;

	if (qopt->cycle_time_extension)
		return -EOPNOTSUPP;

	if (qopt->num_entries > XLNX_TAPRIO_NUM_GCL)
		return -EOPNOTSUPP;

	if (!qopt->cycle_time || qopt->cycle_time > XLNX_TAPRIO_MAX_CYCLE_TIME_NS)
		return -ERANGE;

	for (i = 0; i < qopt->num_entries; ++i) {
		struct tc_taprio_sched_entry *entry = &qopt->entries[i];

		if (entry->interval > XLNX_TAPRIO_MAX_CYCLE_TIME_NS)
			return -EOPNOTSUPP;

		max_tc = fls(entry->gate_mask);
		if (max_tc > lp->num_tc) {
			netdev_err(ndev, "Invalid gate_mask 0x%x at off %d\n",
				   entry->gate_mask, i);
			return -EINVAL;
		}

		if (entry->command != TC_TAPRIO_CMD_SET_GATES)
			return -EINVAL;

		total_time += entry->interval;
	}

	if (total_time > XLNX_TAPRIO_MAX_CYCLE_TIME_NS)
		return -EINVAL;

	/* The cycle time to be at least as big as sum of each interval of gcl */
	if (qopt->cycle_time < total_time)
		return -EINVAL;

	if (qopt->base_time <= 0) {
		netdev_err(ndev, "Invalid base_time: must be greater than 0, got %lld\n",
			   qopt->base_time);
		return -ERANGE;
	}

	return 0;
}

static int xlnx_disable_queues(struct net_device *ndev,
			       struct tc_taprio_qopt_offload *offload)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_local *master_lp;
	struct net_device *master;
	int i, j, err;

	master = lp->master ? lp->master : ndev;
	master_lp = netdev_priv(master);

	lp->qbv_enabled = 0;
	for (i = 0; i < offload->num_entries; i++)
		lp->qbv_enabled |= offload->entries[i].gate_mask;

	for (i = 0; i < lp->num_tc; i++) {
		if (master_lp->txqs[i].is_tadma)
			continue;

		if (lp->qbv_enabled & BIT(i))
			continue;

		if (!master_lp->txqs[i].disable_cnt) {
			err = axienet_mcdma_disable_tx_q(master, i);
			if (err)
				goto q_disable_err;
		}

		master_lp->txqs[i].disable_cnt++;
	}

	return 0;

q_disable_err:
	for (j = 0; j < i; j++) {
		if (lp->qbv_enabled & BIT(j))
			continue;

		master_lp->txqs[j].disable_cnt--;
		if (!master_lp->txqs[j].disable_cnt)
			axienet_mcdma_enable_tx_q(master, i);
	}

	return err;
}

static int xlnx_taprio_replace(struct net_device *ndev,
			       struct tc_taprio_qopt_offload *offload)
{
	struct axienet_local *lp = netdev_priv(ndev);
	unsigned int u_config_change = 0;
	struct timespec64 ts;
	int err = 0;
	u16 i;

	err = validate_taprio_qopt(ndev, offload);
	if (err)
		return err;

	err = tsn_setup_shaper_tc_mqprio(ndev, &offload->mqprio);
	if (err)
		return err;

	err = xlnx_disable_queues(ndev, offload);
	if (err) {
		dev_err(&ndev->dev, "Failed to disable unused queues\n");
		goto mqprio_destroy;
	}

	/* write admin cycle time */
	axienet_qbv_iow(lp, ADMIN_CYCLE_TIME_DENOMINATOR,
			offload->cycle_time & CYCLE_TIME_DENOMINATOR_MASK);

	/* write admin base time */
	ts = ktime_to_timespec64(offload->base_time);
	axienet_qbv_iow(lp, ADMIN_BASE_TIME_SEC, lower_32_bits(ts.tv_sec));
	axienet_qbv_iow(lp, ADMIN_BASE_TIME_SECS, upper_32_bits(ts.tv_sec));
	axienet_qbv_iow(lp, ADMIN_BASE_TIME_NS, ts.tv_nsec);

	u_config_change = axienet_qbv_ior(lp, CONFIG_CHANGE);

	u_config_change &= ~(CC_ADMIN_CTRL_LIST_LENGTH_MASK <<
			     CC_ADMIN_CTRL_LIST_LENGTH_SHIFT);
	u_config_change |= (offload->num_entries & CC_ADMIN_CTRL_LIST_LENGTH_MASK)
			   << CC_ADMIN_CTRL_LIST_LENGTH_SHIFT;

	/* program each list */
	for (i = 0; i < offload->num_entries; i++) {
		axienet_qbv_iow(lp,  ADMIN_CTRL_LIST(i),
				(offload->entries[i].gate_mask &
				ACL_GATE_STATE_MASK) << ACL_GATE_STATE_SHIFT);

		/* set the time for each entry */
		axienet_qbv_iow(lp, ADMIN_CTRL_LIST_TIME(i),
				(offload->entries[i].interval / 8) &
				CTRL_LIST_TIME_INTERVAL_MASK);
	}

	/* clear interrupt status */
	axienet_qbv_iow(lp, INT_STATUS, 0);

	/* kick in new config change */
	u_config_change |= CC_ADMIN_CONFIG_CHANGE_BIT;

	/* enable gate */
	u_config_change |= CC_ADMIN_GATE_ENABLE_BIT;

	/* start */
	axienet_qbv_iow(lp, CONFIG_CHANGE, u_config_change);

	return 0;

mqprio_destroy:
	tsn_reset_tc_mqprio(ndev);

	return err;
}

static void xlnx_enable_queues(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_local *master_lp;
	struct net_device *master;
	int i;

	master = lp->master ? lp->master : ndev;
	master_lp = netdev_priv(master);

	for (i = 0; i < lp->num_tc; i++) {
		if (master_lp->txqs[i].is_tadma)
			continue;

		if (lp->qbv_enabled & BIT(i))
			continue;

		master_lp->txqs[i].disable_cnt--;
		if (!master_lp->txqs[i].disable_cnt)
			axienet_mcdma_enable_tx_q(master, i);
	}

	lp->qbv_enabled = 0;
}

static void xlnx_taprio_destroy(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u32 u_config_change = 0;

	u_config_change &= ~CC_ADMIN_GATE_ENABLE_BIT;
	/* open all the gates */
	u_config_change |= CC_ADMIN_GATE_STATE_MASK;
	axienet_qbv_iow(lp, CONFIG_CHANGE, u_config_change);
	xlnx_enable_queues(ndev);
	tsn_reset_tc_mqprio(ndev);
}

static int tsn_setup_shaper_tc_taprio(struct net_device *ndev, void *type_data)
{
	struct tc_taprio_qopt_offload *offload = type_data;
	int ret = 0;

	switch (offload->cmd) {
	case TAPRIO_CMD_REPLACE:
		ret = xlnx_taprio_replace(ndev, offload);
		break;
	case TAPRIO_CMD_DESTROY:
		xlnx_taprio_destroy(ndev);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/**
 * axienet_cbs_init - Initialize CBS queue tracking by reading hardware config
 * @ndev: Pointer to net_device structure
 *
 * Reads the hardware queue configuration register (Q_TYPE) to detect
 * which queues have CBS (Credit-Based Shaper) enabled. Each queue uses 4 bits:
 *   0000 (0) = BE  - Best Effort
 *   0001 (1) = RES - Reserved/CBS (Credit-Based Shaper)
 *   0010 (2) = ST  - Scheduled Traffic (Time-aware Shaper)
 *
 * Builds the CBS queue mapping where CBS enabled queues are assigned
 * sequential CBS hardware instance indices (0, 1, 2...).
 * Should be called during driver probe/initialization.
 */
void axienet_cbs_init(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int queue, cbs_index = 0;
	u32 queue_config;
	u8 queue_type;

	for (queue = 0; queue < XAE_MAX_TSN_TC; queue++)
		lp->cbs_queue_map[queue] = -1;

	/* CBS is not supported in the 2-queue design */
	if (lp->num_tc <= XAE_MIN_LEGACY_TSN_TC)
		goto out;
	/* In the 3-queue design, CBS is supported only on Queue 1 */
	if (lp->num_tc == XAE_MAX_LEGACY_TSN_TC) {
		lp->cbs_queue_map[1] = cbs_index;
		cbs_index++;
		goto out;
	}

	queue_config = axienet_ior(lp, XAE_Q_TYPE_OFFSET);
	/* Parse queue configuration - 4 bits per queue */
	for (queue = 0; queue < lp->num_tc; queue++) {
		/* Extract 4-bit queue type for this queue */
		queue_type = (queue_config >> (queue * 4)) & 0xF;
		if (queue_type == HW_QUEUE_TYPE_RES) {
			lp->cbs_queue_map[queue] = cbs_index;
			cbs_index++;
		}
	}
out:
	dev_dbg(&ndev->dev, "CBS: Initialized %d CBS-enabled queues from hardware\n",
		cbs_index);
}

static int xlnx_cbs_add(struct net_device *ndev,
			struct tc_cbs_qopt_offload *qopt)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	u32 idleslope_offset, sendslope_offset;
	u32 hw_idleslope, hw_sendslope;
	int queue = qopt->queue;
	u32 link_speed_kbps;
	int cbs_index;

	if (queue < 0 || queue >= XAE_MAX_TSN_TC)
		return -EINVAL;

	/* Check if this queue has CBS enabled in hardware */
	if (lp->cbs_queue_map[queue] < 0) {
		netdev_err(ndev, "Queue %d does not have CBS enabled in hardware\n", queue);
		return -EINVAL;
	}

	/* Get current link speed */
	if (!phydev || phydev->speed == SPEED_UNKNOWN) {
		netdev_err(ndev, "Link speed unknown, cannot configure CBS\n");
		return -EINVAL;
	}

	if (phydev->speed != SPEED_1000 && phydev->speed != SPEED_100) {
		netdev_err(ndev, "CBS unsupported link speed %d Mbps\n",
			   phydev->speed);
		return -EOPNOTSUPP;
	}

	/* Convert link speed from Mbps to kbps */
	link_speed_kbps = phydev->speed * 1000;

	/* Check for invalid CBS parameters */
	if ((qopt->idleslope <= 0 || qopt->sendslope >= 0) ||
	    ((u32)(qopt->idleslope - qopt->sendslope) != link_speed_kbps)) {
		netdev_err(ndev, "Invalid CBS parameters: idleslope=%d sendslope=%d\n",
			   qopt->idleslope, qopt->sendslope);
		return -EINVAL;
	}

	/*
	 * Check minimum idleslope based on link speed
	 * 1 Gbps: minimum CBS_MIN_IDLESLOPE_1G kbps
	 * 100 Mbps: minimum CBS_MIN_IDLESLOPE_100M kbps
	 */
	if (phydev->speed == SPEED_1000 && qopt->idleslope < CBS_MIN_IDLESLOPE_1G) {
		netdev_err(ndev, "CBS Q%d: idleslope %d kbps is less than minimum %d kbps for 1 Gbps link\n",
			   queue, qopt->idleslope, CBS_MIN_IDLESLOPE_1G);
		return -EOPNOTSUPP;
	} else if (phydev->speed == SPEED_100 && qopt->idleslope < CBS_MIN_IDLESLOPE_100M) {
		/* Using CBS_MIN_IDLESLOPE_100M kbps as minimum for 100 Mbps (12.5 rounded up) */
		netdev_err(ndev, "CBS Q%d: idleslope %d kbps is less than minimum %d kbps for 100 Mbps link\n",
			   queue, qopt->idleslope, CBS_MIN_IDLESLOPE_100M);
		return -EOPNOTSUPP;
	}

	/*
	 * Hardware uses CBS_HW_BW_MAX (8192) as 100% bandwidth representation
	 * Calculate hardware values based on link speed
	 */
	hw_idleslope = ((u64)qopt->idleslope * CBS_HW_BW_MAX) / link_speed_kbps;
	hw_sendslope = CBS_HW_BW_MAX - hw_idleslope;

	/* Get CBS index from hardware-detected mapping */
	cbs_index = lp->cbs_queue_map[queue];

	/* Calculate hardware register offsets based on CBS index */
	sendslope_offset = CBS_SENDSLOPE_OFFSET(cbs_index);
	idleslope_offset = CBS_IDLESLOPE_OFFSET(cbs_index);

	/* Configure hardware CBS registers */
	axienet_iow(lp, idleslope_offset, hw_idleslope);
	axienet_iow(lp, sendslope_offset, hw_sendslope);
	netdev_dbg(ndev, "CBS enabled on Q%d\n", queue);

	return 0;
}

static int xlnx_cbs_del(struct net_device *ndev,
			struct tc_cbs_qopt_offload *qopt)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u32 idleslope_offset, sendslope_offset;
	int queue = qopt->queue;
	int cbs_index;

	cbs_index = lp->cbs_queue_map[queue];

	/* Calculate hardware register offsets */
	sendslope_offset = CBS_SENDSLOPE_OFFSET(cbs_index);
	idleslope_offset = CBS_IDLESLOPE_OFFSET(cbs_index);

	/* Clear hardware CBS registers (set to default) */
	axienet_iow(lp, sendslope_offset, CBS_SENDSLOPE_DEF_VALUE);
	axienet_iow(lp, idleslope_offset, CBS_IDLESLOPE_DEF_VALUE);
	netdev_dbg(ndev, "CBS disabled on Q%d\n", queue);

	return 0;
}

static int tsn_setup_shaper_tc_cbs(struct net_device *ndev, void *type_data)
{
	struct tc_cbs_qopt_offload *qopt = type_data;

	return qopt->enable ? xlnx_cbs_add(ndev, qopt) :
			      xlnx_cbs_del(ndev, qopt);
}

int axienet_tsn_shaper_tc(struct net_device *dev, enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_TAPRIO:
		return tsn_setup_shaper_tc_taprio(dev, type_data);
	case TC_SETUP_QDISC_MQPRIO:
		return tsn_setup_shaper_tc_mqprio(dev, type_data);
	case TC_SETUP_QDISC_CBS:
		return tsn_setup_shaper_tc_cbs(dev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int __axienet_set_schedule(struct net_device *ndev, struct qbv_info *qbv)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u16 i;
	unsigned int acl_bit_map = 0;
	u32 u_config_change = 0;

	if (qbv->cycle_time == 0) {
		/* clear the gate enable bit */
		u_config_change &= ~CC_ADMIN_GATE_ENABLE_BIT;
		/* open all the gates */
		u_config_change |= CC_ADMIN_GATE_STATE_MASK;

		axienet_qbv_iow(lp, CONFIG_CHANGE, u_config_change);

		return 0;
	}

	if (axienet_qbv_ior(lp, PORT_STATUS) & 1) {
		if (qbv->force) {
			u_config_change &= ~CC_ADMIN_GATE_ENABLE_BIT;
			axienet_qbv_iow(lp, CONFIG_CHANGE, u_config_change);
		} else {
			return -EALREADY;
		}
	}
	/* write admin time */
	axienet_qbv_iow(lp, ADMIN_CYCLE_TIME_DENOMINATOR,
			qbv->cycle_time & CYCLE_TIME_DENOMINATOR_MASK);

	axienet_qbv_iow(lp, ADMIN_BASE_TIME_NS, qbv->ptp_time_ns);

	axienet_qbv_iow(lp, ADMIN_BASE_TIME_SEC,
			qbv->ptp_time_sec & 0xFFFFFFFF);
	axienet_qbv_iow(lp, ADMIN_BASE_TIME_SECS,
			(qbv->ptp_time_sec >> 32) & BASE_TIME_SECS_MASK);

	u_config_change = axienet_qbv_ior(lp, CONFIG_CHANGE);

	u_config_change &= ~(CC_ADMIN_CTRL_LIST_LENGTH_MASK <<
				CC_ADMIN_CTRL_LIST_LENGTH_SHIFT);
	u_config_change |= (qbv->list_length & CC_ADMIN_CTRL_LIST_LENGTH_MASK)
					<< CC_ADMIN_CTRL_LIST_LENGTH_SHIFT;

	/* program each list */
	for (i = 0; i < qbv->list_length; i++) {
		acl_bit_map = axienet_map_gs_to_hw(lp, qbv->acl_gate_state[i]);
		axienet_qbv_iow(lp,  ADMIN_CTRL_LIST(i),
				(acl_bit_map & (ACL_GATE_STATE_MASK)) <<
				ACL_GATE_STATE_SHIFT);

	    /* set the time for each entry */
	    axienet_qbv_iow(lp, ADMIN_CTRL_LIST_TIME(i),
			    qbv->acl_gate_time[i] &
			    CTRL_LIST_TIME_INTERVAL_MASK);
	}

	/* clear interrupt status */
	axienet_qbv_iow(lp, INT_STATUS, 0);

	/* kick in new config change */
	u_config_change |= CC_ADMIN_CONFIG_CHANGE_BIT;

	/* enable gate */
	u_config_change |= CC_ADMIN_GATE_ENABLE_BIT;

	/* start */
	axienet_qbv_iow(lp, CONFIG_CHANGE, u_config_change);

	return 0;
}

int axienet_set_schedule(struct net_device *ndev, void __user *useraddr)
{
	struct qbv_info *config;
	int ret;

	config = kmalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	if (copy_from_user(config, useraddr, sizeof(struct qbv_info))) {
		ret = -EFAULT;
		goto out;
	}

	pr_debug("setting new schedule\n");

	ret = __axienet_set_schedule(ndev, config);
out:
	kfree(config);
	return ret;
}

static int __axienet_get_schedule(struct net_device *ndev, struct qbv_info *qbv)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u16 i = 0;
	u32 u_value = 0;

	if (!(axienet_qbv_ior(lp, CONFIG_CHANGE) &
			CC_ADMIN_GATE_ENABLE_BIT)) {
		qbv->cycle_time = 0;
		return 0;
	}

	u_value = axienet_qbv_ior(lp, GATE_STATE);
	qbv->list_length = (u_value >> CC_ADMIN_CTRL_LIST_LENGTH_SHIFT) &
				CC_ADMIN_CTRL_LIST_LENGTH_MASK;

	u_value = axienet_qbv_ior(lp, OPER_CYCLE_TIME_DENOMINATOR);
	qbv->cycle_time = u_value & CYCLE_TIME_DENOMINATOR_MASK;

	u_value = axienet_qbv_ior(lp, OPER_BASE_TIME_NS);
	qbv->ptp_time_ns = u_value & OPER_BASE_TIME_NS_MASK;

	qbv->ptp_time_sec = axienet_qbv_ior(lp, OPER_BASE_TIME_SEC);
	u_value = axienet_qbv_ior(lp, OPER_BASE_TIME_SECS);
	qbv->ptp_time_sec |= (u64)(u_value & BASE_TIME_SECS_MASK) << 32;

	for (i = 0; i < qbv->list_length; i++) {
		u_value = axienet_qbv_ior(lp, OPER_CTRL_LIST(i));
		qbv->acl_gate_state[i] = (u_value >> ACL_GATE_STATE_SHIFT) &
					ACL_GATE_STATE_MASK;
		/**
		 * In 2Q system, the actual ST Gate state value is 2,
		 * for user the ST Gate state value is always 4.
		 */
		if (lp->num_tc == 2 && qbv->acl_gate_state[i] == 2)
			qbv->acl_gate_state[i] = 4;

		u_value = axienet_qbv_ior(lp, OPER_CTRL_LIST_TIME(i));
		qbv->acl_gate_time[i] = u_value & CTRL_LIST_TIME_INTERVAL_MASK;
	}
	return 0;
}

int axienet_get_schedule(struct net_device *ndev, void __user *useraddr)
{
	struct qbv_info *qbv;
	int ret = 0;

	qbv = kmalloc(sizeof(*qbv), GFP_KERNEL);
	if (!qbv)
		return -ENOMEM;

	if (copy_from_user(qbv, useraddr, sizeof(struct qbv_info))) {
		ret = -EFAULT;
		goto out;
	}

	__axienet_get_schedule(ndev, qbv);

	if (copy_to_user(useraddr, qbv, sizeof(struct qbv_info)))
		ret = -EFAULT;
out:
	kfree(qbv);
	return ret;
}

static irqreturn_t axienet_qbv_irq(int irq, void *_ndev)
{
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);

	/* clear status */
	axienet_qbv_iow(lp, INT_CLEAR, 0);

	return IRQ_HANDLED;
}

int axienet_qbv_init(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int rc = 0;
	static char irq_name[24];

	if (lp->qbv_irq > 0) {
		sprintf(irq_name, "%s_qbv", ndev->name);
		rc = devm_request_irq(lp->dev, lp->qbv_irq, axienet_qbv_irq,
				      0, irq_name, ndev);
		if (rc)
			dev_err(&ndev->dev, "Failed to request qbv_irq: %d\n", rc);
	}
	return rc;
}

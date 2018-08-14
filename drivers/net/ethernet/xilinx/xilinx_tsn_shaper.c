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

#include "xilinx_axienet.h"
#include "xilinx_tsn_shaper.h"

static inline int axienet_map_gs_to_hw(struct axienet_local *lp, u32 gs)
{
	u8 be_queue = 0;
	u8 re_queue = 1;
	u8 st_queue = 2;
	unsigned int acl_bit_map = 0;

	if (lp->num_tc == 2)
		st_queue = 1;

	if (gs & GS_BE_OPEN)
		acl_bit_map |= (1 << be_queue);
	if (gs & GS_ST_OPEN)
		acl_bit_map |= (1 << st_queue);
	if (lp->num_tc == 3 && (gs & GS_RE_OPEN))
		acl_bit_map |= (1 << re_queue);

	return acl_bit_map;
}

static int __axienet_set_schedule(struct net_device *ndev, struct qbv_info *qbv)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u16 i;
	unsigned int acl_bit_map = 0;
	u32 u_config_change = 0;
	u8 port = qbv->port;

	if (qbv->cycle_time == 0) {
		/* clear the gate enable bit */
		u_config_change &= ~CC_ADMIN_GATE_ENABLE_BIT;
		/* open all the gates */
		u_config_change |= CC_ADMIN_GATE_STATE_SHIFT;

		axienet_iow(lp, CONFIG_CHANGE(port), u_config_change);

		return 0;
	}

	if (axienet_ior(lp, PORT_STATUS(port)) & 1) {
		if (qbv->force) {
			u_config_change &= ~CC_ADMIN_GATE_ENABLE_BIT;
			axienet_iow(lp, CONFIG_CHANGE(port), u_config_change);
		} else {
			return -EALREADY;
		}
	}
	/* write admin time */
	axienet_iow(lp, ADMIN_CYCLE_TIME_DENOMINATOR(port),
		    qbv->cycle_time & CYCLE_TIME_DENOMINATOR_MASK);

	axienet_iow(lp, ADMIN_BASE_TIME_NS(port), qbv->ptp_time_ns);

	axienet_iow(lp, ADMIN_BASE_TIME_SEC(port),
		    qbv->ptp_time_sec & 0xFFFFFFFF);
	axienet_iow(lp, ADMIN_BASE_TIME_SECS(port),
		    (qbv->ptp_time_sec >> 32) & BASE_TIME_SECS_MASK);

	u_config_change = axienet_ior(lp, CONFIG_CHANGE(port));

	u_config_change &= ~(CC_ADMIN_CTRL_LIST_LENGTH_MASK <<
				CC_ADMIN_CTRL_LIST_LENGTH_SHIFT);
	u_config_change |= (qbv->list_length & CC_ADMIN_CTRL_LIST_LENGTH_MASK)
					<< CC_ADMIN_CTRL_LIST_LENGTH_SHIFT;

	/* program each list */
	for (i = 0; i < qbv->list_length; i++) {
		acl_bit_map = axienet_map_gs_to_hw(lp, qbv->acl_gate_state[i]);
		axienet_iow(lp,  ADMIN_CTRL_LIST(port, i),
			    (acl_bit_map & (ACL_GATE_STATE_MASK)) <<
			    ACL_GATE_STATE_SHIFT);

	    /* set the time for each entry */
	    axienet_iow(lp, ADMIN_CTRL_LIST_TIME(port, i),
			qbv->acl_gate_time[i] & CTRL_LIST_TIME_INTERVAL_MASK);
	}

	/* clear interrupt status */
	axienet_iow(lp, INT_STATUS(port), 0);

	/* kick in new config change */
	u_config_change |= CC_ADMIN_CONFIG_CHANGE_BIT;

	/* enable gate */
	u_config_change |= CC_ADMIN_GATE_ENABLE_BIT;

	/* start */
	axienet_iow(lp, CONFIG_CHANGE(port), u_config_change);

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
	u8 port = qbv->port;

	if (!(axienet_ior(lp, CONFIG_CHANGE(port)) &
			CC_ADMIN_GATE_ENABLE_BIT)) {
		qbv->cycle_time = 0;
		return 0;
	}

	u_value = axienet_ior(lp, GATE_STATE(port));
	qbv->list_length = (u_value >> CC_ADMIN_CTRL_LIST_LENGTH_SHIFT) &
				CC_ADMIN_CTRL_LIST_LENGTH_MASK;

	u_value = axienet_ior(lp, OPER_CYCLE_TIME_DENOMINATOR(port));
	qbv->cycle_time = u_value & CYCLE_TIME_DENOMINATOR_MASK;

	u_value = axienet_ior(lp, OPER_BASE_TIME_NS(port));
	qbv->ptp_time_ns = u_value & OPER_BASE_TIME_NS_MASK;

	qbv->ptp_time_sec = axienet_ior(lp, OPER_BASE_TIME_SEC(port));
	u_value = axienet_ior(lp, OPER_BASE_TIME_SECS(port));
	qbv->ptp_time_sec |= (u64)(u_value & BASE_TIME_SECS_MASK) << 32;

	for (i = 0; i < qbv->list_length; i++) {
		u_value = axienet_ior(lp, OPER_CTRL_LIST(port, i));
		qbv->acl_gate_state[i] = (u_value >> ACL_GATE_STATE_SHIFT) &
					ACL_GATE_STATE_MASK;
		/**
		 * In 2Q system, the actual ST Gate state value is 2,
		 * for user the ST Gate state value is always 4.
		 */
		if (lp->num_tc == 2 && qbv->acl_gate_state[i] == 2)
			qbv->acl_gate_state[i] = 4;

		u_value = axienet_ior(lp, OPER_CTRL_LIST_TIME(port, i));
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
	u8  port = 0; /* TODO */

	/* clear status */
	axienet_iow(lp, INT_CLEAR(port), 0);

	return IRQ_HANDLED;
}

int axienet_qbv_init(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int rc;

	rc = request_irq(lp->qbv_irq, axienet_qbv_irq, 0, ndev->name, ndev);
	if (rc)
		goto err_qbv_irq;

err_qbv_irq:
	return rc;
}

void axienet_qbv_remove(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);

	free_irq(lp->qbv_irq, ndev);
}

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

static int __axienet_set_schedule(struct net_device *ndev, struct qbv_info *qbv)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int i;
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

	/* write admin time */
	axienet_iow(lp, ADMIN_CYCLE_TIME_DENOMINATOR(port), qbv->cycle_time);

	axienet_iow(lp, ADMIN_BASE_TIME_NS(port), qbv->ptp_time_ns);

	axienet_iow(lp, ADMIN_BASE_TIME_SEC(port), qbv->ptp_time_sec);

	u_config_change = axienet_ior(lp, CONFIG_CHANGE(port));

	/* write control list length */
	u_config_change |= (qbv->list_length & CC_ADMIN_CTRL_LIST_LENGTH_MASK)
					<< CC_ADMIN_CTRL_LIST_LENGTH_SHIFT;

	/* program each list */
	for (i = 0; i < qbv->list_length; i++) {
		axienet_iow(lp,  ADMIN_CTRL_LIST(port, i),
			    (qbv->acl_gate_state[i] & (ACL_GATE_STATE_MASK)) <<
			    ACL_GATE_STATE_SHIFT);

	    /* set the time for each entry */
	    axienet_iow(lp, ADMIN_CTRL_LIST_TIME(port, i),
			qbv->acl_gate_time[i]);
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
	struct qbv_info config;

	if (copy_from_user(&config, useraddr, sizeof(struct qbv_info)))
		return -EFAULT;

	pr_debug("setting new schedule\n");

	return __axienet_set_schedule(ndev, &config);
}

int axienet_get_schedule(struct net_device *ndev, u8 port, struct qbv_info *qbv)
{
	return 0;
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

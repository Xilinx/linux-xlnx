// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx FPGA Xilinx TSN QBU/QBR - Frame Preemption module.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Author: Priyadarshini Babu <priyadar@xilinx.com>
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
#include "xilinx_tsn_preemption.h"

/**
 * axienet_preemption -  Configure Frame Preemption
 * @ndev: Pointer to the net_device structure
 * @useraddr: Value to be programmed
 * Return: 0 on success, Non-zero error value on failure
 */
int axienet_preemption(struct net_device *ndev, void __user *useraddr)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u8 preemp;

	if (copy_from_user(&preemp, useraddr, sizeof(preemp)))
		return -EFAULT;

	axienet_iow(lp, PREEMPTION_ENABLE_REG, preemp & PREEMPTION_ENABLE);
	return 0;
}

/**
 * axienet_preemption_ctrl -  Configure Frame Preemption Control register
 * @ndev: Pointer to the net_device structure
 * @useraddr: Value to be programmed
 * Return: 0 on success, Non-zero error value on failure
 */
int axienet_preemption_ctrl(struct net_device *ndev, void __user *useraddr)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct preempt_ctrl_sts data;
	u32 value;

	if (copy_from_user(&data, useraddr, sizeof(struct preempt_ctrl_sts)))
		return -EFAULT;
	value = axienet_ior(lp, PREEMPTION_CTRL_STS_REG);

	value &= ~(VERIFY_TIMER_VALUE_MASK << VERIFY_TIMER_VALUE_SHIFT);
	value |= (data.verify_timer_value << VERIFY_TIMER_VALUE_SHIFT);
	value &= ~(ADDITIONAL_FRAG_SIZE_MASK << ADDITIONAL_FRAG_SIZE_SHIFT);
	value |= (data.additional_frag_size << ADDITIONAL_FRAG_SIZE_SHIFT);
	value &= ~(DISABLE_PREEMPTION_VERIFY);
	value |= (data.disable_preemp_verify);

	axienet_iow(lp, PREEMPTION_CTRL_STS_REG, value);
	return 0;
}

/**
 * axienet_preemption_sts -  Get Frame Preemption Status
 * @ndev: Pointer to the net_device structure
 * @useraddr: return value, containing Frame Preemption status
 * Return: 0 on success, Non-zero error value on failure
 */
int axienet_preemption_sts(struct net_device *ndev, void __user *useraddr)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct preempt_status status;
	u32 value;

	value = axienet_ior(lp, PREEMPTION_ENABLE_REG);
	status.preemp_en = value & PREEMPTION_ENABLE;
	value = axienet_ior(lp, XAE_TSN_ABL_OFFSET);
	status.preemp_sup = (value & PREEMPTION_SUPPORT) ? 1 : 0;

	value = axienet_ior(lp, PREEMPTION_CTRL_STS_REG);
	status.ctrl.tx_preemp_sts = (value & TX_PREEMPTION_STS) ? 1 : 0;
	status.ctrl.mac_tx_verify_sts = (value >> MAC_MERGE_TX_VERIFY_STS_SHIFT)
					& MAC_MERGE_TX_VERIFY_STS_MASK;
	status.ctrl.verify_timer_value = (value >> VERIFY_TIMER_VALUE_SHIFT) &
					VERIFY_TIMER_VALUE_MASK;
	status.ctrl.additional_frag_size = (value >> ADDITIONAL_FRAG_SIZE_SHIFT)
						& ADDITIONAL_FRAG_SIZE_MASK;
	status.ctrl.disable_preemp_verify = value & DISABLE_PREEMPTION_VERIFY;

	if (copy_to_user(useraddr, &status, sizeof(struct preempt_status)))
		return -EFAULT;
	return 0;
}

int axienet_preemption_receive(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	u32 value;
	u8 preemption_support;

	value = axienet_ior(lp, XAE_TSN_ABL_OFFSET);
	preemption_support = (value & PREEMPTION_SUPPORT) ? 1 : 0;

	if (!preemption_support)
		return -EOPNOTSUPP;

	value = axienet_ior(lp, PREEMPTION_ENABLE_REG);
	value = value | PREEMPTION_ENABLE;
	axienet_iow(lp, PREEMPTION_ENABLE_REG, value);
	return 0;
}

/**
 * statistic_cnts -  Read statistics counter registers
 * @ndev: Pointer to the net_device structure
 * @ptr: Buffer addr to fill the counter values
 * @count: read #count number of registers
 * @addr_off: Register address to be read
 */
static void statistic_cnts(struct net_device *ndev, void *ptr,
			   unsigned int count, unsigned int addr_off)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int *buf = (int *)ptr;
	int i = 0;

	for (i = 0; i < count; i++) {
		buf[i] = axienet_ior(lp, addr_off);
		addr_off += 4;
	}
}

/**
 * axienet_preemption_cnt -  Get Frame Preemption Statistics counter
 * @ndev: Pointer to the net_device structure
 * @useraddr: return value, containing counters value
 * Return: 0 on success, Non-zero error value on failure
 */
int axienet_preemption_cnt(struct net_device *ndev, void __user *useraddr)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct emac_pmac_stats stats;

	statistic_cnts(ndev, &stats.emac,
		       sizeof(struct statistics_counters) / 4,
		       RX_BYTES_EMAC_REG);

	stats.preemp_en = axienet_ior(lp, PREEMPTION_ENABLE_REG);
	if (stats.preemp_en) {
		statistic_cnts(ndev, &stats.pmac.sts,
			       sizeof(struct statistics_counters) / 4,
			       RX_BYTES_PMAC_REG);
		statistic_cnts(ndev, &stats.pmac.merge,
			       sizeof(struct mac_merge_counters) / 4,
			       TX_HOLD_REG);
	}

	if (copy_to_user(useraddr, &stats, sizeof(struct emac_pmac_stats)))
		return -EFAULT;
	return 0;
}

/**
 * axienet_qbu_user_override -  Configure QBU user override register
 * @ndev: Pointer to the net_device structure
 * @useraddr: Value to be programmed
 * Return: 0 on success, Non-zero error value on failure
 */
int axienet_qbu_user_override(struct net_device *ndev, void __user *useraddr)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct qbu_prog data;
	u32 value;

	if (copy_from_user(&data, useraddr, sizeof(struct qbu_prog)))
		return -EFAULT;

	value = axienet_ior(lp, QBU_USER_OVERRIDE_REG);

	if (data.set & QBU_WINDOW) {
		if (data.user.hold_rel_window) {
			value |= USER_HOLD_REL_ENABLE_VALUE;
			value |= HOLD_REL_WINDOW_OVERRIDE;
		} else {
			value &= ~(USER_HOLD_REL_ENABLE_VALUE);
			value &= ~(HOLD_REL_WINDOW_OVERRIDE);
		}
	}
	if (data.set & QBU_GUARD_BAND) {
		if (data.user.guard_band)
			value |= GUARD_BAND_OVERRUN_CNT_INC_OVERRIDE;
		else
			value &= ~(GUARD_BAND_OVERRUN_CNT_INC_OVERRIDE);
	}
	if (data.set & QBU_HOLD_TIME) {
		if (data.user.hold_time_override) {
			value |= HOLD_TIME_OVERRIDE;
			value &= ~(USER_HOLD_TIME_MASK << USER_HOLD_TIME_SHIFT);
			value |= data.user.user_hold_time <<
					USER_HOLD_TIME_SHIFT;
		} else {
			value &= ~(HOLD_TIME_OVERRIDE);
			value &= ~(USER_HOLD_TIME_MASK << USER_HOLD_TIME_SHIFT);
		}
	}
	if (data.set & QBU_REL_TIME) {
		if (data.user.rel_time_override) {
			value |= REL_TIME_OVERRIDE;
			value &= ~(USER_REL_TIME_MASK << USER_REL_TIME_SHIFT);
			value |= data.user.user_rel_time << USER_REL_TIME_SHIFT;
		} else {
			value &= ~(REL_TIME_OVERRIDE);
			value &= ~(USER_REL_TIME_MASK << USER_REL_TIME_SHIFT);
		}
	}

	axienet_iow(lp, QBU_USER_OVERRIDE_REG, value);
	return 0;
}

/**
 * axienet_qbu_sts -  Get QBU Core status
 * @ndev: Pointer to the net_device structure
 * @useraddr: return value, containing QBU core status value
 * Return: 0 on success, Non-zero error value on failure
 */
int axienet_qbu_sts(struct net_device *ndev, void __user *useraddr)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct qbu_all_status status;
	u32 value = 0;

	value = axienet_ior(lp, QBU_USER_OVERRIDE_REG);
	status.prog.hold_rel_window = (value & USER_HOLD_REL_ENABLE_VALUE)
					? 1 : 0;
	status.prog.guard_band = (value & GUARD_BAND_OVERRUN_CNT_INC_OVERRIDE)
					? 1 : 0;
	status.prog.user_hold_time = (value >> USER_HOLD_TIME_SHIFT) &
					USER_HOLD_TIME_MASK;
	status.prog.user_rel_time = (value >> USER_REL_TIME_SHIFT) &
					USER_REL_TIME_MASK;

	value = axienet_ior(lp, QBU_CORE_STS_REG);
	status.core.hold_time = (value >> HOLD_TIME_STS_SHIFT) &
					HOLD_TIME_STS_MASK;
	status.core.rel_time = (value >> REL_TIME_STS_SHIFT) &
					REL_TIME_STS_MASK;
	status.core.hold_rel_en = (value & HOLD_REL_ENABLE_STS) ? 1 : 0;
	status.core.pmac_hold_req = value & PMAC_HOLD_REQ_STS;

	if (copy_to_user(useraddr, &status, sizeof(struct qbu_all_status)))
		return -EFAULT;
	return 0;
}

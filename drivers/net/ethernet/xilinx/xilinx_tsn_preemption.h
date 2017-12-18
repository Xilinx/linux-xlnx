/**
 * Xilinx TSN QBU/QBR - Frame Preemption header
 *
 * Copyright (C) 2017 Xilinx, Inc.
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

#ifndef XILINX_TSN_PREEMPTION_H
#define XILINX_TSN_PREEMPTION_H

#define PREEMPTION_ENABLE_REG			0x00000440
#define PREEMPTION_CTRL_STS_REG			0x00000444
#define QBU_USER_OVERRIDE_REG			0x00000448
#define QBU_CORE_STS_REG			0x0000044c
#define TX_HOLD_REG				0x00000910
#define RX_BYTES_EMAC_REG			0x00000200
#define RX_BYTES_PMAC_REG			0x00000800

#define PREEMPTION_ENABLE			BIT(0)

#define TX_PREEMPTION_STS			BIT(31)
#define MAC_MERGE_TX_VERIFY_STS_MASK		0x7
#define MAC_MERGE_TX_VERIFY_STS_SHIFT		24
#define VERIFY_TIMER_VALUE_MASK			0x7F
#define VERIFY_TIMER_VALUE_SHIFT		8
#define ADDITIONAL_FRAG_SIZE_MASK		0x3
#define ADDITIONAL_FRAG_SIZE_SHIFT		4
#define DISABLE_PREEMPTION_VERIFY		BIT(0)

#define USER_HOLD_REL_ENABLE_VALUE		BIT(31)
#define USER_HOLD_TIME_MASK			0x1FF
#define USER_HOLD_TIME_SHIFT			16
#define USER_REL_TIME_MASK			0x3F
#define USER_REL_TIME_SHIFT			8
#define GUARD_BAND_OVERRUN_CNT_INC_OVERRIDE	BIT(3)
#define HOLD_REL_WINDOW_OVERRIDE		BIT(2)
#define HOLD_TIME_OVERRIDE			BIT(1)
#define REL_TIME_OVERRIDE			BIT(0)

#define HOLD_REL_ENABLE_STS			BIT(31)
#define HOLD_TIME_STS_MASK			0x1FF
#define HOLD_TIME_STS_SHIFT			16
#define REL_TIME_STS_MASK			0x3F
#define REL_TIME_STS_SHIFT			8
#define PMAC_HOLD_REQ_STS			BIT(0)

struct preempt_ctrl_sts {
	u8 tx_preemp_sts:1;
	u8 mac_tx_verify_sts:3;
	u8 verify_timer_value:7;
	u8 additional_frag_size:2;
	u8 disable_preemp_verify:1;
} __packed;

struct qbu_user_override {
	u8 enable_value:1;
	u16 user_hold_time:9;
	u8 user_rel_time:6;
	u8 guard_band:1;
	u8 hold_rel_window:1;
	u8 hold_time_override:1;
	u8 rel_time_override:1;
} __packed;

struct qbu_user {
	struct qbu_user_override user;
	u8 set;
};

#define QBU_WINDOW BIT(0)
#define QBU_GUARD_BAND BIT(1)
#define QBU_HOLD_TIME BIT(2)
#define QBU_REL_TIME BIT(3)

struct qbu_core_status {
	u16 hold_time;
	u8 rel_time;
	u8 hold_rel_en:1;
	u8 pmac_hold_req:1;
} __packed;

struct cnt_64 {
	unsigned int msb;
	unsigned int lsb;
};

union static_cntr {
	u64 cnt;
	struct cnt_64 word;
};

struct mac_merge_counters {
	union static_cntr tx_hold_cnt;
	union static_cntr tx_frag_cnt;
	union static_cntr rx_assembly_ok_cnt;
	union static_cntr rx_assembly_err_cnt;
	union static_cntr rx_smd_err_cnt;
	union static_cntr rx_frag_cnt;
};

struct statistics_counters {
	union static_cntr rx_bytes_cnt;
	union static_cntr tx_bytes_cnt;
	union static_cntr undersize_frames_cnt;
	union static_cntr frag_frames_cnt;
	union static_cntr rx_64_bytes_frames_cnt;
	union static_cntr rx_65_127_bytes_frames_cnt;
	union static_cntr rx_128_255_bytes_frames_cnt;
	union static_cntr rx_256_511_bytes_frames_cnt;
	union static_cntr rx_512_1023_bytes_frames_cnt;
	union static_cntr rx_1024_max_frames_cnt;
	union static_cntr rx_oversize_frames_cnt;
	union static_cntr tx_64_bytes_frames_cnt;
	union static_cntr tx_65_127_bytes_frames_cnt;
	union static_cntr tx_128_255_bytes_frames_cnt;
	union static_cntr tx_256_511_bytes_frames_cnt;
	union static_cntr tx_512_1023_bytes_frames_cnt;
	union static_cntr tx_1024_max_frames_cnt;
	union static_cntr tx_oversize_frames_cnt;
	union static_cntr rx_good_frames_cnt;
	union static_cntr rx_fcs_err_cnt;
	union static_cntr rx_good_broadcast_frames_cnt;
	union static_cntr rx_good_multicast_frames_cnt;
	union static_cntr rx_good_control_frames_cnt;
	union static_cntr rx_out_of_range_err_cnt;
	union static_cntr rx_good_vlan_frames_cnt;
	union static_cntr rx_good_pause_frames_cnt;
	union static_cntr rx_bad_opcode_frames_cnt;
	union static_cntr tx_good_frames_cnt;
	union static_cntr tx_good_broadcast_frames_cnt;
	union static_cntr tx_good_multicast_frames_cnt;
	union static_cntr tx_underrun_err_cnt;
	union static_cntr tx_good_control_frames_cnt;
	union static_cntr tx_good_vlan_frames_cnt;
	union static_cntr tx_good_pause_frames_cnt;
};

struct pmac_counters {
	struct statistics_counters sts;
	struct mac_merge_counters merge;
};

struct emac_pmac_stats {
	u8 preemp_en;
	struct statistics_counters emac;
	struct pmac_counters pmac;
};

#endif /* XILINX_TSN_PREEMPTION_H */

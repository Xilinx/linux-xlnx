/*
 * Xilinx TSN QBV scheduler header
 *
 * Copyright (C) 2017 Xilinx, Inc.
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

#ifndef XILINX_TSN_SHAPER_H
#define XILINX_TSN_SHAPER_H

/* 0x0		CONFIG_CHANGE
 * 0x8		GATE_STATE
 * 0x10		ADMIN_CTRL_LIST_LENGTH
 * 0x18	ADMIN_CYCLE_TIME_DENOMINATOR
 * 0x20         ADMIN_BASE_TIME_NS
 * 0x24		ADMIN_BASE_TIME_SEC
 * 0x28		ADMIN_BASE_TIME_SECS
 * 0x30	INT_STAT
 * 0x34	INT_EN
 * 0x38		INT_CLR
 * 0x3c		STATUS
 * 0x40		CONFIG_CHANGE_TIME_NS
 * 0x44		CONFIG_CHANGE_TIME_SEC
 * 0x48		CONFIG_CHANGE_TIME_SECS
 * 0x50		OPER_CTRL_LIST_LENGTH
 * 0x58	OPER_CYCLE_TIME_DENOMINATOR
 * 0x60		OPER_BASE_TIME_NS
 * 0x64		OPER_BASE_TIME_SEC
 * 0x68		OPER_BASE_TIME_SECS
 * 0x6c	BE_XMIT_OVRRUN_CNT
 * 0x74		RES_XMIT_OVRRUN_CNT
 * 0x7c		ST_XMIT_OVRRUN_CNT
 */

enum hw_port {
	PORT_EP = 0,
	PORT_TEMAC_1,
	PORT_TEMAC_2,
};

			     /* EP */ /* TEMAC1 */ /* TEMAC2*/
static u32 qbv_reg_map[3] = { 0x0,   0x14000,     0x14000 };

/* 0x14000	0x14FFC	Time Schedule Registers (Control & Status)
 * 0x15000	0x15FFF	Time Schedule Control List Entries
 */

#define TIME_SCHED_BASE(port)  qbv_reg_map[(port)]

#define CTRL_LIST_BASE(port)  (TIME_SCHED_BASE(port) + 0x1000)

/* control list entries
 * admin control list 0 : 31
 * "Time interval between two gate entries" must be greater than
 * "time required to transmit biggest supported frame" on that queue when
 * the gate for the queue is going from open to close state.
 */
#define ADMIN_CTRL_LIST(port, n)  (CTRL_LIST_BASE(port) + ((n) * 8))
#define ACL_GATE_STATE_SHIFT	8
#define ACL_GATE_STATE_MASK	0x7
#define ADMIN_CTRL_LIST_TIME(port, n)  (ADMIN_CTRL_LIST((port), n) + 4)

#define OPER_CTRL_LIST(port, n)	(CTRL_LIST_BASE(port) + 0x800 + ((n) * 8))
#define OPER_CTRL_LIST_TIME(port, n)  (OPER_CTRL_LIST(port, n) + 4)
#define CTRL_LIST_TIME_INTERVAL_MASK	0xFFFFF

#define CONFIG_CHANGE(port)    (TIME_SCHED_BASE(port) + 0x0)
#define CC_ADMIN_GATE_STATE_SHIFT            0x7
#define CC_ADMIN_GATE_STATE_MASK             (7)
#define CC_ADMIN_CTRL_LIST_LENGTH_SHIFT      (8)
#define CC_ADMIN_CTRL_LIST_LENGTH_MASK		(0x1FF)
/* This request bit is set when all the related Admin* filelds are populated.
 * This bit is set by S/W and clear by core when core start with new schedule.
 * Once set it can only be cleared by core or hard/soft reset.
 */
#define CC_ADMIN_CONFIG_CHANGE_BIT           BIT(30)
#define CC_ADMIN_GATE_ENABLE_BIT             BIT(31)

#define GATE_STATE(port)      (TIME_SCHED_BASE(port) + 0x8)
#define GS_OPER_GATE_STATE_SHIFT		(0)
#define GS_OPER_GATE_STATE_MASK		(0x7)
#define GS_OPER_CTRL_LIST_LENGTH_SHIFT	(8)
#define GS_OPER_CTRL_LIST_LENGTH_MASK		(0x3F)
#define GS_SUP_MAX_LIST_LENGTH_SHIFT		(16)
#define GS_SUP_MAX_LIST_LENGTH_MASK		(0x3F)
#define GS_TICK_GRANULARITY_SHIFT		(24)
#define GS_TICK_GRANULARITY_MASK		(0x3F)

#define ADMIN_CYCLE_TIME_DENOMINATOR(port)	(TIME_SCHED_BASE(port) + 0x18)
#define ADMIN_BASE_TIME_NS(port)		(TIME_SCHED_BASE(port) + 0x20)
#define ADMIN_BASE_TIME_SEC(port)		(TIME_SCHED_BASE(port) + 0x24)
#define ADMIN_BASE_TIME_SECS(port)		(TIME_SCHED_BASE(port) + 0x28)

#define INT_STATUS(port)			(TIME_SCHED_BASE(port) + 0x30)
#define INT_ENABLE(port)			(TIME_SCHED_BASE(port) + 0x34)
#define INT_CLEAR(port)				(TIME_SCHED_BASE(port) + 0x38)
#define PORT_STATUS(port)			(TIME_SCHED_BASE(port) + 0x3c)

/* Config Change time is valid after Config Pending bit is set. */
#define CONFIG_CHANGE_TIME_NS(port)		(TIME_SCHED_BASE((port)) + 0x40)
#define CONFIG_CHANGE_TIME_SEC(port)		(TIME_SCHED_BASE((port)) + 0x44)
#define CONFIG_CHANGE_TIME_SECS(port)		(TIME_SCHED_BASE((port)) + 0x48)

#define OPER_CONTROL_LIST_LENGTH(port)		(TIME_SCHED_BASE(port) + 0x50)
#define OPER_CYCLE_TIME_DENOMINATOR(port)	(TIME_SCHED_BASE(port) + 0x58)
#define CYCLE_TIME_DENOMINATOR_MASK		(0x3FFFFFFF)

#define OPER_BASE_TIME_NS(port)			(TIME_SCHED_BASE(port) + 0x60)
#define OPER_BASE_TIME_NS_MASK			(0x3FFFFFFF)
#define OPER_BASE_TIME_SEC(port)		(TIME_SCHED_BASE(port) + 0x64)
#define OPER_BASE_TIME_SECS(port)		(TIME_SCHED_BASE(port) + 0x68)
#define BASE_TIME_SECS_MASK			(0xFFFF)

#define BE_XMIT_OVERRUN_COUNT(port)		(TIME_SCHED_BASE(port) + 0x6c)
#define RES_XMIT_OVERRUN_COUNT(port)		(TIME_SCHED_BASE(port) + 0x74)
#define ST_XMIT_OVERRUN_COUNT(port)		(TIME_SCHED_BASE(port) + 0x7c)

/* internally hw deals with queues only,
 * in 3q system ST acl bitmap would be would 1 << 2
 * in 2q system ST acl bitmap would be 1 << 1
 * But this is confusing to users.
 * so use the following fixed gate state and internally
 * map them to hw
 */
#define GS_BE_OPEN   BIT(0)
#define GS_RE_OPEN   BIT(1)
#define GS_ST_OPEN   BIT(2)
#define QBV_MAX_ENTRIES	256

struct qbv_info {
	u8 port;
	u8 force;
	u32 cycle_time;
	u64 ptp_time_sec;
	u32 ptp_time_ns;
	u32 list_length;
	u32 acl_gate_state[QBV_MAX_ENTRIES];
	u32 acl_gate_time[QBV_MAX_ENTRIES];
};

#endif /* XILINX_TSN_SHAPER_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The Xilinx timer/counter component. This component supports the Xilinx
 * timer/counter which supports the following features:
 *  - Polled mode.
 *  - Interrupt driven mode
 *  - enabling and disabling specific timers
 *  - PWM operation
 *  - Cascade Operation
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 */

#ifndef _XLNX_TIMER_H_
#define _XLNX_TIMER_H_

#include <linux/types.h>

/**
 * struct xlnx_hdcp_timer_hw - This structure contains hardware subcore configuration
 * information about AXI Timer.
 * @coreaddress: AXI Timer core address
 * @sys_clock_freq: System Clock Frequency
 */
struct xlnx_hdcp_timer_hw {
	void __iomem *coreaddress;
	u32 sys_clock_freq;
};

/*
 * Detailed register descriptions available in
 * Programming Guide PG079.
 * https://docs.xilinx.com/v/u/en-US/pg079-axi-timer
 */
#define XTC_DEVICE_TIMER_COUNT		2
#define XTC_TIMER_COUNTER_OFFSET	16
#define XTC_CASCADE_MODE_OPTION		BIT(7)
#define XTC_ENABLE_ALL_OPTION		BIT(6)
#define XTC_DOWN_COUNT_OPTION		BIT(5)
#define XTC_CAPTURE_MODE_OPTION		BIT(4)
#define XTC_INT_MODE_OPTION		BIT(3)
#define XTC_AUTO_RELOAD_OPTION		BIT(2)
#define XTC_EXT_COMPARE_OPTION		BIT(1)
#define XTC_TIMER_0			0
#define XTC_TIMER_1			1

#define XTC_TCSR_OFFSET			0
#define XTC_TLR_OFFSET			4
#define XTC_TCR_OFFSET			8
#define XTC_CSR_CASC_MASK		BIT(11)
#define XTC_CSR_ENABLE_ALL_MASK		BIT(10)
#define XTC_CSR_ENABLE_PWM_MASK		BIT(9)
#define XTC_CSR_INT_OCCURED_MASK	BIT(8)
#define XTC_CSR_ENABLE_TMR_MASK		BIT(7)
#define XTC_CSR_ENABLE_INT_MASK		BIT(6)
#define XTC_CSR_LOAD_MASK		BIT(5)
#define XTC_CSR_AUTO_RELOAD_MASK	BIT(4)
#define XTC_CSR_EXT_CAPTURE_MASK	BIT(3)
#define XTC_CSR_EXT_GENERATE_MASK	BIT(2)
#define XTC_CSR_DOWN_COUNT_MASK		BIT(1)
#define XTC_CSR_CAPTURE_MODE_MASK	BIT(0)
#define XTC_MAX_LOAD_VALUE		GENMASK(31, 0)
#define XTC_COMPONENT_IS_READY		BIT(0)
#define XTC_COMPONENT_IS_STARTED	BIT(1)

typedef void (*xlnx_timer_cntr_handler) (void *callbackref, u8 tmr_cntr_number);

/**
 * struct xlnx_hdcp_timer_config - The user is required to allocate a
 * variable of this type for every timer/counter device in the system.
 * @hw_config: Configuration of timer hardware core
 * @handler: Timer callback handler
 * @callbackref: Timer callback reference
 * @is_tmrcntr0_started: Timercnt0 is initialized and started
 * @is_tmrcntr1_started: Timercnt1 is initialized and started
 */
struct xlnx_hdcp_timer_config {
	struct xlnx_hdcp_timer_hw hw_config;
	xlnx_timer_cntr_handler handler;
	void *callbackref;
	u32 is_tmrcntr0_started;
	u32 is_tmrcntr1_started;
};

u32 xlnx_hdcp_tmrcntr_get_value(struct xlnx_hdcp_timer_config *xtimercntr,
				u8 tmr_cntr_number);
int xlnx_hdcp_tmrcntr_init(struct xlnx_hdcp_timer_config *xtimercntr);
void xlnx_hdcp_tmrcntr_stop(struct xlnx_hdcp_timer_config *xtimercntr,
			    u8 tmr_cntr_number);
void xlnx_hdcp_tmrcntr_start(struct xlnx_hdcp_timer_config *xtimercntr,
			     u8 tmr_cntr_number);
void xlnx_hdcp_tmrcntr_cfg_init(struct xlnx_hdcp_timer_config *xtimercntr);
void xlnx_hdcp_tmrcntr_start(struct xlnx_hdcp_timer_config *xtimercntr,
			     u8 tmr_cntr_number);
void xlnx_hdcp_tmrcntr_set_reset_value(struct xlnx_hdcp_timer_config *xtimercntr,
				       u8 tmr_cntr_number, u32 reset_value);
void xlnx_hdcp_tmrcntr_set_options(struct xlnx_hdcp_timer_config *xtimercntr,
				   u8 tmr_cntr_number, u32 options);
void xlnx_hdcp_tmrcntr_set_handler(struct xlnx_hdcp_timer_config *xtimercntr,
				   xlnx_timer_cntr_handler funcptr,
				   void *callbackref);
void xlnx_hdcp_tmrcntr_interrupt_handler(struct xlnx_hdcp_timer_config *xtimercntr);
void xlnx_hdcp_tmrcntr_reset(struct xlnx_hdcp_timer_config *xtimercntr,
			     u8 tmr_cntr_number);

#endif

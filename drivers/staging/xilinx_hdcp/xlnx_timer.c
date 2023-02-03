// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AXI Timer driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 *
 * This driver initializes Xilinx timer/counter component.
 */

#include <linux/io.h>
#include <linux/xlnx/xlnx_timer.h>

/*
 * The following data type maps an option to a register mask such that getting
 * and setting the options may be table driven.
 */
struct mapping {
	u32 option;
	u32 mask;
};

/*
 * Create the table which contains options which are to be processed to get/set
 * the options. These options are table driven to allow easy maintenance and
 * expansion of the options.
 */
static struct mapping options_table[] = {
	{XTC_CASCADE_MODE_OPTION, XTC_CSR_CASC_MASK},
	{XTC_ENABLE_ALL_OPTION, XTC_CSR_ENABLE_ALL_MASK},
	{XTC_DOWN_COUNT_OPTION, XTC_CSR_DOWN_COUNT_MASK},
	{XTC_CAPTURE_MODE_OPTION, XTC_CSR_CAPTURE_MODE_MASK |
	 XTC_CSR_EXT_CAPTURE_MASK},
	{XTC_INT_MODE_OPTION, XTC_CSR_ENABLE_INT_MASK},
	{XTC_AUTO_RELOAD_OPTION, XTC_CSR_AUTO_RELOAD_MASK},
	{XTC_EXT_COMPARE_OPTION, XTC_CSR_EXT_GENERATE_MASK}
};

#define XTC_NUM_OPTIONS   ARRAY_SIZE(options_table)

static const u8 xtmrctr_offset[] = { 0, XTC_TIMER_COUNTER_OFFSET };

static void xlnx_hdcp_tmrcntr_write_reg(void __iomem *coreaddress,
					u32 tmrctr_number, u32 offset, u32 value)
{
	writel(value, coreaddress + xtmrctr_offset[tmrctr_number] + offset);
}

static u32 xlnx_hdcp_tmrcntr_read_reg(void __iomem *coreaddress, u32 tmrctr_number,
				      u32 offset)
{
	return readl(coreaddress + xtmrctr_offset[tmrctr_number] + offset);
}

void xlnx_hdcp_tmrcntr_set_handler(struct xlnx_hdcp_timer_config *xtimercntr,
				   xlnx_timer_cntr_handler funcptr,
				   void *callbackref)
{
	xtimercntr->handler = funcptr;
	xtimercntr->callbackref = callbackref;
}
EXPORT_SYMBOL_GPL(xlnx_hdcp_tmrcntr_set_handler);

void xlnx_hdcp_tmrcntr_cfg_init(struct xlnx_hdcp_timer_config *xtimercntr)
{
	xtimercntr->callbackref = xtimercntr;
}

static int xlnx_hdcp_timer_init(struct xlnx_hdcp_timer_config *xtimercntr)
{
	struct xlnx_hdcp_timer_hw *hw_config = &xtimercntr->hw_config;
	u32 tmr_cntr_started[XTC_DEVICE_TIMER_COUNT];
	int status = -EINVAL;
	u8 tmr_index;

	tmr_cntr_started[0] = xtimercntr->is_tmrcntr0_started;
	tmr_cntr_started[1] = xtimercntr->is_tmrcntr1_started;

	for (tmr_index = 0; tmr_index < XTC_DEVICE_TIMER_COUNT; tmr_index++) {
		if (tmr_cntr_started[tmr_index] == XTC_COMPONENT_IS_STARTED)
			continue;
		xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_index,
					    XTC_TLR_OFFSET, 0);
		xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_index,
					    XTC_TCSR_OFFSET,
					    XTC_CSR_INT_OCCURED_MASK | XTC_CSR_LOAD_MASK);
		xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_index,
					    XTC_TCSR_OFFSET, 0);
		status = 0;
	}

	return status;
}

int xlnx_hdcp_tmrcntr_init(struct xlnx_hdcp_timer_config *xtimercntr)
{
	if (xtimercntr->is_tmrcntr0_started == XTC_COMPONENT_IS_STARTED &&
	    xtimercntr->is_tmrcntr1_started == XTC_COMPONENT_IS_STARTED)
		return 0;

	xlnx_hdcp_tmrcntr_cfg_init(xtimercntr);

	return xlnx_hdcp_timer_init(xtimercntr);
}

void xlnx_hdcp_tmrcntr_start(struct xlnx_hdcp_timer_config *xtimercntr,
			     u8 tmr_cntr_number)
{
	struct xlnx_hdcp_timer_hw *hw_config = &xtimercntr->hw_config;
	u32 cntrl_statusreg;

	cntrl_statusreg = xlnx_hdcp_tmrcntr_read_reg(hw_config->coreaddress,
						     tmr_cntr_number,
						     XTC_TCSR_OFFSET);
	xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_cntr_number,
				    XTC_TCSR_OFFSET,
				    XTC_CSR_LOAD_MASK);

	if (tmr_cntr_number == XTC_TIMER_0)
		xtimercntr->is_tmrcntr0_started = XTC_COMPONENT_IS_STARTED;
	else
		xtimercntr->is_tmrcntr1_started = XTC_COMPONENT_IS_STARTED;
	xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_cntr_number,
				    XTC_TCSR_OFFSET,
				    cntrl_statusreg | XTC_CSR_ENABLE_TMR_MASK);
	cntrl_statusreg = xlnx_hdcp_tmrcntr_read_reg(hw_config->coreaddress,
						     tmr_cntr_number,
						     XTC_TCSR_OFFSET);
}

void xlnx_hdcp_tmrcntr_stop(struct xlnx_hdcp_timer_config *xtimercntr,
			    u8 tmr_cntr_number)
{
	struct xlnx_hdcp_timer_hw *hw_config = &xtimercntr->hw_config;
	u32 cntrl_statusreg;

	cntrl_statusreg = xlnx_hdcp_tmrcntr_read_reg(hw_config->coreaddress,
						     tmr_cntr_number, XTC_TCSR_OFFSET);

	cntrl_statusreg &= (u32)~(XTC_CSR_ENABLE_TMR_MASK);

	xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_cntr_number,
				    XTC_TCSR_OFFSET, cntrl_statusreg);

	if (tmr_cntr_number == XTC_TIMER_0)
		xtimercntr->is_tmrcntr0_started = 0;
	else
		xtimercntr->is_tmrcntr1_started = 0;
}

u32 xlnx_hdcp_tmrcntr_get_value(struct xlnx_hdcp_timer_config *xtimercntr,
				u8 tmr_cntr_number)
{
	struct xlnx_hdcp_timer_hw *hw_config = &xtimercntr->hw_config;

	return xlnx_hdcp_tmrcntr_read_reg(hw_config->coreaddress,
					  tmr_cntr_number,
					  XTC_TCR_OFFSET);
}

void xlnx_hdcp_tmrcntr_set_reset_value(struct xlnx_hdcp_timer_config *xtimercntr,
				       u8 tmr_cntr_number, u32 reset_value)
{
	struct xlnx_hdcp_timer_hw *hw_config = &xtimercntr->hw_config;

	xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_cntr_number,
				    XTC_TLR_OFFSET, reset_value);
}

void xlnx_hdcp_tmrcntr_reset(struct xlnx_hdcp_timer_config *xtimercntr,
			     u8 tmr_cntr_number)
{
	struct xlnx_hdcp_timer_hw *hw_config = &xtimercntr->hw_config;
	u32 counter_cntrl_reg;

	counter_cntrl_reg = xlnx_hdcp_tmrcntr_read_reg(hw_config->coreaddress,
						       tmr_cntr_number,
						       XTC_TCSR_OFFSET);
	xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_cntr_number,
				    XTC_TCSR_OFFSET,
				    counter_cntrl_reg | XTC_CSR_LOAD_MASK);

	xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_cntr_number,
				    XTC_TCSR_OFFSET, counter_cntrl_reg);
}

void xlnx_hdcp_tmrcntr_set_options(struct xlnx_hdcp_timer_config *xtimercntr,
				   u8 tmr_cntr_number, u32 options)
{
	struct xlnx_hdcp_timer_hw *hw_config = &xtimercntr->hw_config;
	u32 counter_cntrl_reg = 0;
	u32 index;

	for (index = 0; index < XTC_NUM_OPTIONS; index++) {
		if (options & options_table[index].option)
			counter_cntrl_reg |= options_table[index].mask;
		else
			counter_cntrl_reg &= ~options_table[index].mask;
	}
	xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress, tmr_cntr_number,
				    XTC_TCSR_OFFSET, counter_cntrl_reg);
}

/**
 * xlnx_hdcp_tmrcntr_interrupt_handler - HDCP timercntr interrupt handler
 * @xtimercntr: timercounter configuration structure
 */
void xlnx_hdcp_tmrcntr_interrupt_handler(struct xlnx_hdcp_timer_config *xtimercntr)
{
	struct xlnx_hdcp_timer_hw *hw_config = &xtimercntr->hw_config;
	u32 control_status_reg;
	u8 tmr_cntr_number;

	for (tmr_cntr_number = 0;
		tmr_cntr_number < XTC_DEVICE_TIMER_COUNT; tmr_cntr_number++) {
		control_status_reg = xlnx_hdcp_tmrcntr_read_reg(hw_config->coreaddress,
								tmr_cntr_number,
								XTC_TCSR_OFFSET);
		if (control_status_reg & XTC_CSR_ENABLE_INT_MASK) {
			if (control_status_reg & XTC_CSR_INT_OCCURED_MASK) {
				xtimercntr->handler(xtimercntr->callbackref, tmr_cntr_number);
				control_status_reg =
					xlnx_hdcp_tmrcntr_read_reg(hw_config->coreaddress,
								   tmr_cntr_number,
								   XTC_TCSR_OFFSET);
				if (((control_status_reg & XTC_CSR_AUTO_RELOAD_MASK) == 0) &&
				    ((control_status_reg & XTC_CSR_CAPTURE_MODE_MASK)
				    == 0)) {
					control_status_reg &=
						(u32)~XTC_CSR_ENABLE_TMR_MASK;
					xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress,
								    tmr_cntr_number,
								    XTC_TCSR_OFFSET,
								    control_status_reg |
								    XTC_CSR_LOAD_MASK);
					xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress,
								    tmr_cntr_number,
								    XTC_TCSR_OFFSET,
								    control_status_reg);
				}
				xlnx_hdcp_tmrcntr_write_reg(hw_config->coreaddress,
							    tmr_cntr_number, XTC_TCSR_OFFSET,
							    control_status_reg |
							    XTC_CSR_INT_OCCURED_MASK);
			}
		}
	}
}
EXPORT_SYMBOL_GPL(xlnx_hdcp_tmrcntr_interrupt_handler);

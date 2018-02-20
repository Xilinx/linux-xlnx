/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright (C) 2016-2018 Xilinx
 */

#ifndef __LINUX_CLK_ZYNQMP_H_
#define __LINUX_CLK_ZYNQMP_H_

#include <linux/spinlock.h>
#include <linux/firmware/xilinx/zynqmp/firmware.h>

#define CLK_FRAC	BIT(13) /* has a fractional parent */

struct device;

struct clk *clk_register_zynqmp_pll(const char *name, u32 clk_id,
				    const char * const *parent, u8 num_parents,
				    unsigned long flag);

struct clk *zynqmp_clk_register_gate(struct device *dev, const char *name,
				     u32 clk_id,
				     const char * const *parent_name,
				     u8 num_parents, unsigned long flags,
				     u8 clk_gate_flags);

struct clk *zynqmp_clk_register_divider(struct device *dev, const char *name,
					u32 clk_id, u32 div_type,
					const char * const *parent_name,
					u8 num_parents,
					unsigned long flags,
					u8 clk_divider_flags);

struct clk *zynqmp_clk_register_mux(struct device *dev, const char *name,
				    u32 clk_id,
				    const char **parent_names,
				    u8 num_parents, unsigned long flags,
				    u8 clk_mux_flags);

struct clk *zynqmp_clk_register_mux_table(struct device *dev, const char *name,
					  u32 clk_id,
					  const char * const *parent_names,
					  u8 num_parents, unsigned long flags,
					  u8 clk_mux_flags);

#endif

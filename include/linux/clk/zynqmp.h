/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright (C) 2016-2018 Xilinx
 */

#ifndef __LINUX_CLK_ZYNQMP_H_
#define __LINUX_CLK_ZYNQMP_H_

#include <linux/spinlock.h>
#include <linux/soc/xilinx/zynqmp/firmware.h>

#define CLK_FRAC	BIT(13) /* has a fractional parent */

struct device;

static inline u32 zynqmp_pm_mmio_readl(void __iomem *reg)
{
	u32 val;
	int ret;

	ret = zynqmp_pm_mmio_read((u32)(ulong)reg, &val);
	if (ret)
		pr_err("Read failed address: %x\n", (u32)(ulong)reg);
	return val;
}

static inline int zynqmp_pm_mmio_writel(u32 val, void __iomem *reg)
{

	return zynqmp_pm_mmio_write((u32)(ulong)reg, 0xffffffff, val);
}

struct clk *clk_register_zynqmp_pll(const char *name, const char *parent,
		unsigned long flag, resource_size_t *pll_ctrl,
		resource_size_t *pll_status, u8 lock_index);

struct clk *zynqmp_clk_register_gate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		resource_size_t *reg, u8 bit_idx,
		u8 clk_gate_flags);

struct clk *zynqmp_clk_register_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		resource_size_t *reg, u8 shift, u8 width,
		u8 clk_divider_flags);

struct clk *zynqmp_clk_register_mux(struct device *dev, const char *name,
		const char  **parent_names, u8 num_parents,
		unsigned long flags,
		resource_size_t *reg, u8 shift, u8 width,
		u8 clk_mux_flags);

struct clk *zynqmp_clk_register_mux_table(struct device *dev, const char *name,
		const char * const *parent_names, u8 num_parents,
		unsigned long flags,
		resource_size_t *reg, u8 shift, u32 mask,
		u8 clk_mux_flags, u32 *table);

#endif

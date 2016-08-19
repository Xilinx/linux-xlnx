/*
 * Copyright (C) 2016 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_CLK_ZYNQMP_H_
#define __LINUX_CLK_ZYNQMP_H_

#include <linux/spinlock.h>
#include <linux/soc/xilinx/zynqmp/pm.h>

static inline u32 zynqmp_pm_mmio_readl(void __iomem *reg)
{
	u32 val;
	int ret;

	ret = zynqmp_pm_mmio_read((u32)(ulong)reg, &val);
	if (ret)
		pr_err("Read failed\n");
	return val;
}

static inline int zynqmp_pm_mmio_writel(u32 val, void __iomem *reg)
{
	int ret;

	ret = zynqmp_pm_mmio_write((u32)(ulong)reg, 0xffffffff, val);
	if (ret)
		pr_err("Write failed\n");
	return ret;
}
#endif

/*
 *  Copyright (C) 2012 - 2013 Xilinx
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

#ifndef __LINUX_CLK_ZYNQ_H_
#define __LINUX_CLK_ZYNQ_H_

#include <linux/spinlock.h>

extern unsigned int zynq_clk_suspended;

void zynq_clock_init(void __iomem *slcr);
int zynq_clk_suspend_early(void);
void zynq_clk_resume_late(void);

struct clk *clk_register_zynq_pll(const char *name, const char *parent,
		void __iomem *pll_ctrl, void __iomem *pll_status, u8 lock_index,
		spinlock_t *lock);

#endif

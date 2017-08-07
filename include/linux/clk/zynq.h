/*
 * Copyright (C) 2013 Xilinx Inc.
 * Copyright (C) 2012 National Instruments
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_CLK_ZYNQ_H_
#define __LINUX_CLK_ZYNQ_H_

#include <linux/spinlock.h>

int zynq_clk_suspend_early(void);
void zynq_clk_resume_late(void);
void zynq_clk_topswitch_enable(void);
void zynq_clk_topswitch_disable(void);
void zynq_clock_init(void);

struct clk *clk_register_zynq_pll(const char *name, const char *parent,
		void __iomem *pll_ctrl, void __iomem *pll_status, u8 lock_index,
		spinlock_t *lock);
#endif

/*
 *  Copyright (C) 2012 Xilinx
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

#ifndef __MACH_CLK__H__
#define __MACH_CLK__H__

void zynq_clock_init(void);

struct clk *clk_register_zynq_gd1m(const char *name,
		void __iomem *clkctrl, const char **pnames,
		spinlock_t *lock);
struct clk *clk_register_zynq_gd2m(const char *name,
		void __iomem *clkctrl, const char **pnames, u8 num_parents,
		spinlock_t *lock);
struct clk *clk_register_zynq_d2m(const char *name,
		void __iomem *clkctrl, const char **pnames, spinlock_t *lock);
struct clk *clk_register_zynq_d1m(const char *name,
		void __iomem *clkctrl, const char **pnames, u8 num_parents,
		spinlock_t *lock);

struct clk *clk_register_zynq_pll(const char *name, void __iomem *pllctrl,
		void __iomem *pllcfg, void __iomem *pllstatus, u8 lockbit);

struct clk *clk_register_zynq_clk621(const char *name,
		void __iomem *clkctrl, void __iomem *clk621,
		unsigned int basediv,
		unsigned int divadd, const char **pnames, u8 num_parents,
		spinlock_t *lock);
#endif

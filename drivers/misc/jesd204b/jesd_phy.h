/*
 * Copyright (C) 2014 - 2015 Xilinx, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef JESD_PHY_H_
#define JESD_PHY_H_

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

struct jesd204b_phy_state {
	struct device	*dev;
	void __iomem	*phy;
	struct clk	*clk;
	u32		vers_id;
	u32		addr;
	u32		lanes;
	u32		band;
	u32		pll;
	unsigned long	rate;
};

int jesd204_phy_set_loop(struct jesd204b_phy_state *st, u32 loopval);

#endif /* JESD_PHY_H_ */

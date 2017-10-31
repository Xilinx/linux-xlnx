/*
 * Xilinx VPHY header
 *
 * Copyright (C) 2016-2017 Xilinx, Inc.
 *
 * Author: Leon Woestenberg <leon@sidebranch.com>
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

#ifndef _PHY_VPHY_H_
#define _PHY_VPHY_H_

/* @TODO change directory name on production release */
#include "xvphy.h"

struct phy;

/* VPHY is built (either as module or built-in) */
extern XVphy *xvphy_get_xvphy(struct phy *phy);
extern void xvphy_mutex_lock(struct phy *phy);
extern void xvphy_mutex_unlock(struct phy *phy);
extern int xvphy_do_something(struct phy *phy);

#endif /* _PHY_VPHY_H_ */

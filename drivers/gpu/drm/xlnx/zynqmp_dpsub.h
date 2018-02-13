// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP DPSUB Subsystem Driver
 *
 *  Copyright (C) 2017 - 2018 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
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

#ifndef _ZYNQMP_DPSUB_H_
#define _ZYNQMP_DPSUB_H_

struct zynqmp_dpsub {
	struct zynqmp_dp *dp;
	struct zynqmp_disp *disp;
	struct platform_device *master;
};

#endif /* _ZYNQMP_DPSUB_H_ */

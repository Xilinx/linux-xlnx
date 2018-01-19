// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP Display Driver
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

#ifndef _ZYNQMP_DISP_H_
#define _ZYNQMP_DISP_H_

struct zynqmp_disp;

void zynqmp_disp_handle_vblank(struct zynqmp_disp *disp);
unsigned int zynqmp_disp_get_apb_clk_rate(struct zynqmp_disp *disp);
bool zynqmp_disp_aud_enabled(struct zynqmp_disp *disp);
unsigned int zynqmp_disp_get_aud_clk_rate(struct zynqmp_disp *disp);
uint32_t zynqmp_disp_get_crtc_mask(struct zynqmp_disp *disp);

int zynqmp_disp_bind(struct device *dev, struct device *master, void *data);
void zynqmp_disp_unbind(struct device *dev, struct device *master, void *data);

int zynqmp_disp_probe(struct platform_device *pdev);
int zynqmp_disp_remove(struct platform_device *pdev);

#endif /* _ZYNQMP_DISP_H_ */

/*
 * Video Timing Controller Header for Xilinx DRM KMS
 *
 *  Copyright (C) 2013 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyunk@xilinx.com>
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

#ifndef _XILINX_VTC_H_
#define _XILINX_VTC_H_

struct xilinx_vtc;

struct videomode;

void xlnx_vtc_config_sig(struct xilinx_vtc *vtc,
			   struct videomode *vm);
void xlnx_vtc_enable_vblank_intr(struct xilinx_vtc *vtc,
				   void (*fn)(void *), void *data);
void xlnx_vtc_disable_vblank_intr(struct xilinx_vtc *vtc);
void xlnx_vtc_reset(struct xilinx_vtc *vtc);
void xlnx_vtc_enable(struct xilinx_vtc *vtc);
void xlnx_vtc_disable(struct xilinx_vtc *vtc);

struct device;
struct device_node;

struct xilinx_vtc *xlnx_vtc_probe(struct device *dev,
				    struct device_node *node);
void xilinx_vtc_remove(struct xilinx_vtc *vtc);

#endif /* _XILINX_VTC_H_ */

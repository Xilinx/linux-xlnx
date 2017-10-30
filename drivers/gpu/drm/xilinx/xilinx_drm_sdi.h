/*
 * SDI subsystem header for Xilinx DRM KMS
 *
 *  Copyright (C) 2017 Xilinx, Inc.
 *
 *  Author: Saurabh Sengar <saurabhs@xilinx.com>
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

#ifndef _XILINX_DRM_SDI_H_
#define _XILINX_DRM_SDI_H_

struct xilinx_sdi;
struct device_node;

struct xilinx_sdi *xilinx_drm_sdi_of_get(struct device_node *np);
void xilinx_drm_sdi_enable_vblank(struct xilinx_sdi *sdi,
				  void (*vblank_fn)(void *),
				  void *vblank_data);
void xilinx_drm_sdi_disable_vblank(struct xilinx_sdi *sdi);
#endif /* _XILINX_DRM_SDI_H_ */

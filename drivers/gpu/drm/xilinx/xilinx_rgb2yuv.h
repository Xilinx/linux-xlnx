/*
 * Color Space Converter Header for Xilinx DRM KMS
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

#ifndef _XILINX_RGB2YUV_H_
#define _XILINX_RGB2YUV_H_

struct xilinx_rgb2yuv;

void xilinx_rgb2yuv_configure(struct xilinx_rgb2yuv *rgb2yuv,
			      int hactive, int vactive);
void xilinx_rgb2yuv_reset(struct xilinx_rgb2yuv *rgb2yuv);
void xilinx_rgb2yuv_enable(struct xilinx_rgb2yuv *rgb2yuv);
void xilinx_rgb2yuv_disable(struct xilinx_rgb2yuv *rgb2yuv);

struct device;
struct device_node;

struct xilinx_rgb2yuv *xilinx_rgb2yuv_probe(struct device *dev,
					    struct device_node *node);

#endif /* _XILINX_RGB2YUV_H_ */

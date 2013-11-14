/*
 * Xilinx Chroma Resampler Header for Xilinx DRM KMS
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

#ifndef _XILINX_CRESAMPLE_H_
#define _XILINX_CRESAMPLE_H_

struct xilinx_cresample;

void xilinx_cresample_configure(struct xilinx_cresample *cresample,
				int hactive, int vactive);
void xilinx_cresample_reset(struct xilinx_cresample *cresample);
void xilinx_cresample_enable(struct xilinx_cresample *cresample);
void xilinx_cresample_disable(struct xilinx_cresample *cresample);

const char *
xilinx_cresample_get_input_format_name(struct xilinx_cresample *cresample);
const char *
xilinx_cresample_get_output_format_name(struct xilinx_cresample *cresample);

struct device;
struct device_node;

struct xilinx_cresample *xilinx_cresample_probe(struct device *dev,
						struct device_node *node);

#endif /* _XILINX_CRESAMPLE_H_ */

/*
 * Xilinx OSD Header for Xilinx DRM KMS
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

#ifndef _XILINX_OSD_H_
#define _XILINX_OSD_H_

/* TODO: use the fixed max alpha value for 8 bit component width for now. */
#define OSD_MAX_ALPHA	0x100

struct xilinx_osd;
struct xilinx_osd_layer;

/* osd layer configuration */
void xilinx_osd_layer_set_alpha(struct xilinx_osd_layer *layer, u32 enable,
				u32 alpha);
void xilinx_osd_layer_set_priority(struct xilinx_osd_layer *layer, u32 prio);
void xilinx_osd_layer_set_dimension(struct xilinx_osd_layer *layer,
				    u16 xstart, u16 ystart,
				    u16 xsize, u16 ysize);

/* osd layer operation */
void xilinx_osd_layer_enable(struct xilinx_osd_layer *layer);
void xilinx_osd_layer_disable(struct xilinx_osd_layer *layer);
struct xilinx_osd_layer *xilinx_osd_layer_get(struct xilinx_osd *osd);
void xilinx_osd_layer_put(struct xilinx_osd_layer *layer);

/* osd configuration */
void xilinx_osd_set_color(struct xilinx_osd *osd, u8 r, u8 g, u8 b);
void xilinx_osd_set_dimension(struct xilinx_osd *osd, u32 width, u32 height);

unsigned int xilinx_osd_get_num_layers(struct xilinx_osd *osd);
unsigned int xilinx_osd_get_max_width(struct xilinx_osd *osd);
unsigned int xilinx_osd_get_format(struct xilinx_osd *osd);

/* osd operation */
void xilinx_osd_reset(struct xilinx_osd *osd);
void xilinx_osd_enable(struct xilinx_osd *osd);
void xilinx_osd_disable(struct xilinx_osd *osd);
void xilinx_osd_enable_rue(struct xilinx_osd *osd);
void xilinx_osd_disable_rue(struct xilinx_osd *osd);

struct device;
struct device_node;

struct xilinx_osd *xilinx_osd_probe(struct device *dev,
				    struct device_node *node);

#endif /* _XILINX_OSD_H_ */

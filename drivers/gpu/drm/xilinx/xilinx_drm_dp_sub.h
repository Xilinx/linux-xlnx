/*
 * DisplayPort subsystem header for Xilinx DRM KMS
 *
 *  Copyright (C) 2014 Xilinx, Inc.
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

#ifndef _XILINX_DRM_DP_SUB_H_
#define _XILINX_DRM_DP_SUB_H_

#define XILINX_DRM_DP_SUB_NUM_LAYERS	2
#define XILINX_DRM_DP_SUB_MAX_WIDTH	4096
#define XILINX_DRM_DP_SUB_MAX_ALPHA	255

struct drm_device;
struct xilinx_drm_dp_sub;
struct xilinx_drm_dp_sub_layer;

int xilinx_drm_dp_sub_layer_check_size(struct xilinx_drm_dp_sub *dp_sub,
				       struct xilinx_drm_dp_sub_layer *layer,
				       uint32_t width, uint32_t height);
int xilinx_drm_dp_sub_layer_set_fmt(struct xilinx_drm_dp_sub *dp_sub,
				    struct xilinx_drm_dp_sub_layer *layer,
				    uint32_t drm_fmt);
uint32_t xilinx_drm_dp_sub_layer_get_fmt(struct xilinx_drm_dp_sub *dp_sub,
					 struct xilinx_drm_dp_sub_layer *layer);
void xilinx_drm_dp_sub_layer_enable(struct xilinx_drm_dp_sub *dp_sub,
				    struct xilinx_drm_dp_sub_layer *layer);
void xilinx_drm_dp_sub_layer_disable(struct xilinx_drm_dp_sub *dp_sub,
				     struct xilinx_drm_dp_sub_layer *layer);
struct xilinx_drm_dp_sub_layer *
xilinx_drm_dp_sub_layer_get(struct xilinx_drm_dp_sub *dp_sub, bool priv);
void xilinx_drm_dp_sub_layer_put(struct xilinx_drm_dp_sub *dp_sub,
				 struct xilinx_drm_dp_sub_layer *layer);

int xilinx_drm_dp_sub_set_output_fmt(struct xilinx_drm_dp_sub *dp_sub,
				     uint32_t drm_fmt);
void xilinx_drm_dp_sub_set_bg_color(struct xilinx_drm_dp_sub *dp_sub,
				    u32 c0, u32 c1, u32 c2);
void xilinx_drm_dp_sub_set_alpha(struct xilinx_drm_dp_sub *dp_sub, u32 alpha);
void
xilinx_drm_dp_sub_enable_alpha(struct xilinx_drm_dp_sub *dp_sub, bool enable);

void xilinx_drm_dp_sub_enable_vblank(struct xilinx_drm_dp_sub *dp_sub,
				     void (*vblank_fn)(void *),
				     void *vblank_data);
void xilinx_drm_dp_sub_disable_vblank(struct xilinx_drm_dp_sub *dp_sub);
void xilinx_drm_dp_sub_handle_vblank(struct xilinx_drm_dp_sub *dp_sub);
void xilinx_drm_dp_sub_enable(struct xilinx_drm_dp_sub *dp_sub);
void xilinx_drm_dp_sub_disable(struct xilinx_drm_dp_sub *dp_sub);

struct xilinx_drm_dp_sub *xilinx_drm_dp_sub_of_get(struct device_node *np);
void xilinx_drm_dp_sub_put(struct xilinx_drm_dp_sub *dp_sub);

#endif /* _XILINX_DRM_DP_SUB_H_ */

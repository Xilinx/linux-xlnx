/*
 * Xilinx DRM Mixer plane driver
 *
 *  Copyright (C) 2017 Xilinx, Inc.
 *
 *  Author: Jeffrey Mouroux <jmouroux@xilinx.com>
 *
 *  Based on Xilinx plane driver by Hyun Kwon <hyunk@xilinx.com>
 *  Copyright (C) 2013
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

#ifndef _XILINX_DRM_MIXER_CRTC_H_
#define _XILINX_DRM_MIXER_CRTC_H_
#include "xilinx_mixer_data.h"

struct drm_device;
struct drm_crtc;
struct xv_mixer;
struct xilinx_drm_mixer;
struct xilinx_mixer_crtc;

/** JPM TODO update
 * struct xilnx_drm_mixer - Container for interfacing DRM driver to mixer
 * @mixer_hw: Object representing actual hardware state of mixer
 * @plane_manager: Xilinx DRM driver crtc object
 * @drm_primary_layer: Hardware layer serving as logical DRM primary layer
 * @hw_master_layer: Base video streaming layer
 * @hw_logo_layer: Hardware logo layer
 * @alpha_prop: Global layer alpha property
 * @scale_prop: Layer scale property (1x, 2x or 4x)
 * @bg_color: Background color property for primary layer
 *
 * Contains pointers to logical constructions such as the DRM plane manager as
 * well as pointers to distinquish the mixer layer serving as the DRM "primary"
 * plane from the actual mixer layer which serves as the background layer in
 * hardware.
 *
 */

struct xilinx_drm_mixer {
	struct xv_mixer mixer_hw;
	struct xilinx_mixer_crtc *crtc;
	struct xilinx_drm_plane *drm_primary_layer;
	struct xilinx_drm_plane *hw_master_layer;
	struct xilinx_drm_plane *hw_logo_layer;
	struct xilinx_drm_plane *planes;
	u32 num_planes;
	u32 max_width;
	u32 max_height;
	u32 max_cursor_width;
	u32 max_cursor_height;
	struct drm_property *alpha_prop;
	struct drm_property *scale_prop;
	struct drm_property *bg_color;
};

struct xilinx_mixer_crtc {
	struct drm_crtc base;
	struct xilinx_drm_mixer mixer;
	struct drm_device *drm;
	struct clk *pixel_clock;
	bool pixel_clock_enabled;
	int dpms;
	struct drm_pending_vblank_event *event;
};

#define to_xilinx_crtc(x)	container_of(x, struct xilinx_mixer_crtc, base)

void xvmixer_drm_crtc_enable_vblank(struct drm_crtc *base_crtc);
void xvmixer_drm_crtc_disable_vblank(struct drm_crtc *base_crtc);
void xvmixer_drm_crtc_cancel_page_flip(struct drm_crtc *base_crtc,
				      struct drm_file *file);

void xvmixer_drm_crtc_restore(struct drm_crtc *base_crtc);

unsigned int xvmixer_drm_crtc_get_max_width(struct drm_crtc *base_crtc);
unsigned int xvmixer_drm_crtc_get_max_height(struct drm_crtc *base_crtc);
unsigned int xvmixer_drm_crtc_get_max_cursor_width(struct drm_crtc *base_crtc);
unsigned int xvmixer_drm_crtc_get_max_cursor_height(struct drm_crtc *base_crtc);
bool xvmixer_drm_crtc_check_format(struct drm_crtc *base_crtc, uint32_t fourcc);
uint32_t xvmixer_drm_crtc_get_format(struct drm_crtc *base_crtc);
unsigned int xvmixer_drm_crtc_get_align(struct drm_crtc *base_crtc);

struct drm_crtc *xvmixer_drm_crtc_create(struct drm_device *drm);
void xvmixer_drm_crtc_destroy(struct drm_crtc *base_crtc);

#endif /* _XILINX_DRM_MIXER_CRTC_H_ */

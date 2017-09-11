/*
 * Xilinx DRM Mixer plane header
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

#ifndef _XILINX_DRM_MIXER_PLANE_H_
#define _XILINX_DRM_MIXER_PLANE_H_
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <linux/of_dma.h>

struct drm_crtc;
struct drm_plane;

#define MAX_NUM_SUB_PLANES	4

/**
 * struct xilinx_drm_plane_dma - Xilinx drm plane VDMA object
 *
 * @chan: dma channel
 * @xt: dma interleaved configuration template
 * @sgl: data chunk for dma_interleaved_template
 * @is_active: flag if the DMA is active
 */
struct xilinx_drm_plane_dma {
	struct dma_chan *chan;
	struct dma_interleaved_template xt;
	struct data_chunk sgl[1];
	bool is_active;
};

/**
 * struct xilinx_drm_plane - Xilinx drm plane object
 *
 * @base: base drm plane object
 * @mixer_layer: video mixer hardware layer data instance
 * @mixer: mixer DRM object
 * @dma: dma object
 * @id: plane id
 * @dpms: current dpms level
 * @primary: flag for primary plane
 * @format: pixel format
 */
struct xilinx_drm_plane {
	struct drm_plane base;
	struct xv_mixer_layer_data *mixer_layer;
	struct xilinx_drm_mixer *mixer;
	struct xilinx_drm_plane_dma dma[MAX_NUM_SUB_PLANES];
	int id;
	int dpms;
	bool primary;
	uint32_t format;
};

#define to_xilinx_plane(x)	container_of(x, struct xilinx_drm_plane, base)

/* plane operations */
void xvmixer_drm_plane_dpms(struct drm_plane *base_plane, int dpms);

void xvmixer_drm_plane_commit(struct drm_plane *base_plane);

int xvmixer_drm_plane_mode_set(struct drm_plane *base_plane,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      unsigned int crtc_w, unsigned int crtc_h,
			      uint32_t src_x, uint32_t src_y,
			      uint32_t src_w, uint32_t src_h);

int xvmixer_drm_plane_get_max_width(struct drm_plane *base_plane);

int xvmixer_drm_plane_get_max_height(struct drm_plane *base_plane);

int xvmixer_drm_plane_get_max_cursor_width(struct drm_plane *base_plane);

int xvmixer_drm_plane_get_max_cursor_height(struct drm_plane *base_plane);

uint32_t xvmixer_drm_plane_get_format(struct drm_plane *base_plane);

unsigned int xvmixer_drm_plane_get_align(struct drm_plane *base_plane);

int xvmixer_drm_mixer_init_plane(struct xilinx_drm_plane *plane,
				unsigned int poss_crtcs,
				struct device_node *layer_node);

bool xvmixer_drm_plane_check_format(struct xilinx_drm_mixer *mixer,
				   uint32_t format);

u32 xvmixer_drm_plane_get_num_planes(struct xilinx_drm_mixer *mixer);

void xvmixer_drm_plane_restore(struct xilinx_drm_mixer *mixer);

#endif /* _XILINX_DRM_MIXER_PLANE_H_ */

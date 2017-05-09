/*
 * Xilinx DRM plane header for Xilinx
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

#ifndef _XILINX_DRM_PLANE_H_
#define _XILINX_DRM_PLANE_H_
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
 * @id: plane id
 * @dpms: current dpms level
 * @zpos: user requested z-position value
 * @prio: actual layer priority
 * @alpha: alpha value
 * @alpha_enable: alpha enable value
 * @primary: flag for primary plane
 * @format: pixel format
 * @dma: dma object
 * @rgb2yuv: rgb2yuv instance
 * @cresample: cresample instance
 * @osd_layer: osd layer
 * @mixer_layer: video mixer hardware layer data instance
 * @dp_layer: DisplayPort subsystem layer
 * @manager: plane manager
 */
struct xilinx_drm_plane {
	struct drm_plane base;
	int id;
	int dpms;
	unsigned int zpos;
	unsigned int prio;
	unsigned int alpha;
	unsigned int alpha_enable;
	bool primary;
	uint32_t format;
	struct xilinx_drm_plane_dma dma[MAX_NUM_SUB_PLANES];
	struct xilinx_rgb2yuv *rgb2yuv;
	struct xilinx_cresample *cresample;
	struct xilinx_osd_layer *osd_layer;
	struct xv_mixer_layer_data *mixer_layer;
	struct xilinx_drm_dp_sub_layer *dp_layer;
	struct xilinx_drm_plane_manager *manager;
};

#ifdef __XLNX_DRM_MIXER__
#define MAX_PLANES XVMIX_MAX_SUPPORTED_LAYERS
#else
#define MAX_PLANES 8
#endif

/**
 * struct xilinx_drm_plane_manager - Xilinx drm plane manager object
 *
 * @drm: drm device
 * @node: plane device node
 * @osd: osd instance
 * @mixer: mixer IP instance
 * @dp_sub: DisplayPort subsystem instance
 * @num_planes: number of available planes
 * @format: video format
 * @max_width: maximum crtc primary layer width
 * @max_height: maximum crtc primary layer height
 * @max_cursor_width: maximum pixel size for cursor layer width
 * @max_cursor_height: maximum pixel size for cursor layer height
 * @zpos_prop: z-position(priority) property
 * @alpha_prop: alpha value property
 * @default_alpha: default alpha value
 * @planes: xilinx drm planes
 */
struct xilinx_drm_plane_manager {
	struct drm_device *drm;
	struct device_node *node;
	struct xilinx_osd *osd;
	struct xilinx_drm_mixer *mixer;
	struct xilinx_drm_dp_sub *dp_sub;
	int num_planes;
	int max_planes;
	uint32_t format;
	int max_width;
	int max_height;
	int max_cursor_width;
	int max_cursor_height;
	struct drm_property *zpos_prop;
	struct drm_property *alpha_prop;
	struct drm_property *scale_prop;
	struct drm_property *alpha_enable_prop;
	unsigned int default_alpha;
	struct xilinx_drm_plane *planes[MAX_PLANES];
};

#define to_xilinx_plane(x)	container_of(x, struct xilinx_drm_plane, base)

/* plane operations */
void xilinx_drm_plane_dpms(struct drm_plane *base_plane, int dpms);

void xilinx_drm_plane_commit(struct drm_plane *base_plane);

int xilinx_drm_plane_mode_set(struct drm_plane *base_plane,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      unsigned int crtc_w, unsigned int crtc_h,
			      uint32_t src_x, uint32_t src_y,
			      uint32_t src_w, uint32_t src_h);
int xilinx_drm_plane_get_max_width(struct drm_plane *base_plane);

int xilinx_drm_plane_get_max_height(struct drm_plane *base_plane);

int xilinx_drm_plane_get_max_cursor_width(struct drm_plane *base_plane);

int xilinx_drm_plane_get_max_cursor_height(struct drm_plane *base_plane);

uint32_t xilinx_drm_plane_get_format(struct drm_plane *base_plane);

unsigned int xilinx_drm_plane_get_align(struct drm_plane *base_plane);

/* plane manager operations */
struct xilinx_drm_plane_manager;

void
xilinx_drm_plane_manager_mode_set(struct xilinx_drm_plane_manager *manager,
				  unsigned int crtc_w, unsigned int crtc_h);
void xilinx_drm_plane_manager_dpms(struct xilinx_drm_plane_manager *manager,
				   int dpms);
struct drm_plane *
xilinx_drm_plane_create_primary(struct xilinx_drm_plane_manager *manager,
				unsigned int possible_crtcs);
int xilinx_drm_plane_create_planes(struct xilinx_drm_plane_manager *manager,
				   unsigned int possible_crtcs);

bool xilinx_drm_plane_check_format(struct xilinx_drm_plane_manager *manager,
				   uint32_t format);
int xilinx_drm_plane_get_num_planes(struct xilinx_drm_plane_manager *manager);

void xilinx_drm_plane_restore(struct xilinx_drm_plane_manager *manager);

struct xilinx_drm_plane_manager *
xilinx_drm_plane_probe_manager(struct drm_device *drm);

void xilinx_drm_plane_remove_manager(struct xilinx_drm_plane_manager *manager);

#endif /* _XILINX_DRM_PLANE_H_ */

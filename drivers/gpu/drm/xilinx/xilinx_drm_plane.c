/*
 * Xilinx DRM plane driver for Xilinx
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

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>

#include "xilinx_drm_dp_sub.h"
#include "xilinx_drm_drv.h"
#include "xilinx_drm_fb.h"
#include "xilinx_drm_plane.h"

#include "xilinx_cresample.h"
#include "xilinx_osd.h"
#include "xilinx_rgb2yuv.h"

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
	u32 format;
	struct xilinx_drm_plane_dma dma[MAX_NUM_SUB_PLANES];
	struct xilinx_rgb2yuv *rgb2yuv;
	struct xilinx_cresample *cresample;
	struct xilinx_osd_layer *osd_layer;
	struct xilinx_drm_dp_sub_layer *dp_layer;
	struct xilinx_drm_plane_manager *manager;
};

#define MAX_PLANES 8

/**
 * struct xilinx_drm_plane_manager - Xilinx drm plane manager object
 *
 * @drm: drm device
 * @node: plane device node
 * @osd: osd instance
 * @dp_sub: DisplayPort subsystem instance
 * @num_planes: number of available planes
 * @format: video format
 * @max_width: maximum width
 * @zpos_prop: z-position(priority) property
 * @alpha_prop: alpha value property
 * @alpha_enable_prop: alpha enable property
 * @default_alpha: default alpha value
 * @planes: xilinx drm planes
 */
struct xilinx_drm_plane_manager {
	struct drm_device *drm;
	struct device_node *node;
	struct xilinx_osd *osd;
	struct xilinx_drm_dp_sub *dp_sub;
	int num_planes;
	u32 format;
	int max_width;
	struct drm_property *zpos_prop;
	struct drm_property *alpha_prop;
	struct drm_property *alpha_enable_prop;
	unsigned int default_alpha;
	struct xilinx_drm_plane *planes[MAX_PLANES];
};

#define to_xilinx_plane(x)	container_of(x, struct xilinx_drm_plane, base)

void xilinx_drm_plane_commit(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct dma_async_tx_descriptor *desc;
	enum dma_ctrl_flags flags;
	unsigned int i;

	/* for xilinx video framebuffer dma, if used */
	xilinx_xdma_drm_config(plane->dma[0].chan, plane->format);

	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);

	for (i = 0; i < MAX_NUM_SUB_PLANES; i++) {
		struct xilinx_drm_plane_dma *dma = &plane->dma[i];

		if (dma->chan && dma->is_active) {
			flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
			desc = dmaengine_prep_interleaved_dma(dma->chan,
							      &dma->xt,
							      flags);
			if (!desc) {
				DRM_ERROR("failed to prepare DMA descriptor\n");
				return;
			}

			dmaengine_submit(desc);

			dma_async_issue_pending(dma->chan);
		}
	}
}

/* set plane dpms */
void xilinx_drm_plane_dpms(struct drm_plane *base_plane, int dpms)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_plane_manager *manager = plane->manager;
	unsigned int i;

	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);
	DRM_DEBUG_KMS("dpms: %d -> %d\n", plane->dpms, dpms);

	if (plane->dpms == dpms)
		return;

	plane->dpms = dpms;
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		if (manager->dp_sub) {
			if (plane->primary) {
				xilinx_drm_dp_sub_enable_alpha(manager->dp_sub,
							       plane->alpha_enable);
				xilinx_drm_dp_sub_set_alpha(manager->dp_sub,
							    plane->alpha);
			}
			xilinx_drm_dp_sub_layer_enable(manager->dp_sub,
						       plane->dp_layer);
		}

		if (plane->rgb2yuv)
			xilinx_rgb2yuv_enable(plane->rgb2yuv);

		if (plane->cresample)
			xilinx_cresample_enable(plane->cresample);

		/* enable osd */
		if (manager->osd) {
			xilinx_osd_disable_rue(manager->osd);

			xilinx_osd_layer_set_priority(plane->osd_layer,
						      plane->prio);
			xilinx_osd_layer_enable_alpha(plane->osd_layer,
						      plane->alpha_enable);
			xilinx_osd_layer_set_alpha(plane->osd_layer,
						   plane->alpha);
			xilinx_osd_layer_enable(plane->osd_layer);

			xilinx_osd_enable_rue(manager->osd);
		}

		xilinx_drm_plane_commit(base_plane);
		break;
	default:
		/* disable/reset osd */
		if (manager->osd) {
			xilinx_osd_disable_rue(manager->osd);

			xilinx_osd_layer_set_dimension(plane->osd_layer,
						       0, 0, 0, 0);
			xilinx_osd_layer_disable(plane->osd_layer);

			xilinx_osd_enable_rue(manager->osd);
		}

		if (plane->cresample) {
			xilinx_cresample_disable(plane->cresample);
			xilinx_cresample_reset(plane->cresample);
		}

		if (plane->rgb2yuv) {
			xilinx_rgb2yuv_disable(plane->rgb2yuv);
			xilinx_rgb2yuv_reset(plane->rgb2yuv);
		}

		/* stop dma engine and release descriptors */
		for (i = 0; i < MAX_NUM_SUB_PLANES; i++) {
			if (plane->dma[i].chan && plane->dma[i].is_active)
				dmaengine_terminate_all(plane->dma[i].chan);
		}

		if (manager->dp_sub)
			xilinx_drm_dp_sub_layer_disable(manager->dp_sub,
							plane->dp_layer);

		break;
	}
}

/* mode set a plane */
int xilinx_drm_plane_mode_set(struct drm_plane *base_plane,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      unsigned int crtc_w, unsigned int crtc_h,
			      u32 src_x, u32 src_y,
			      u32 src_w, u32 src_h)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct drm_gem_cma_object *obj;
	size_t offset;
	unsigned int hsub, vsub, i;

	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);

	/* configure cresample */
	if (plane->cresample)
		xilinx_cresample_configure(plane->cresample, crtc_w, crtc_h);

	/* configure rgb2yuv */
	if (plane->rgb2yuv)
		xilinx_rgb2yuv_configure(plane->rgb2yuv, crtc_w, crtc_h);

	DRM_DEBUG_KMS("h: %d(%d), v: %d(%d)\n",
		      src_w, crtc_x, src_h, crtc_y);
	DRM_DEBUG_KMS("bpp: %d\n", fb->bits_per_pixel / 8);

	hsub = drm_format_horz_chroma_subsampling(fb->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(fb->pixel_format);

	for (i = 0; i < drm_format_num_planes(fb->pixel_format); i++) {
		unsigned int width = src_w / (i ? hsub : 1);
		unsigned int height = src_h / (i ? vsub : 1);
		unsigned int cpp = drm_format_plane_cpp(fb->pixel_format, i);

		if (!cpp)
			cpp = xilinx_drm_format_bpp(fb->pixel_format) >> 3;

		obj = xilinx_drm_fb_get_gem_obj(fb, i);
		if (!obj) {
			DRM_ERROR("failed to get a gem obj for fb\n");
			return -EINVAL;
		}

		plane->dma[i].xt.numf = height;
		plane->dma[i].sgl[0].size = width * cpp;
		plane->dma[i].sgl[0].icg = fb->pitches[i] -
					   plane->dma[i].sgl[0].size;
		offset = src_x * cpp + src_y * fb->pitches[i];
		offset += fb->offsets[i];
		plane->dma[i].xt.src_start = obj->paddr + offset;
		plane->dma[i].xt.frame_size = 1;
		plane->dma[i].xt.dir = DMA_MEM_TO_DEV;
		plane->dma[i].xt.src_sgl = true;
		plane->dma[i].xt.dst_sgl = false;
		plane->dma[i].is_active = true;
	}

	for (; i < MAX_NUM_SUB_PLANES; i++)
		plane->dma[i].is_active = false;

	/* set OSD dimensions */
	if (plane->manager->osd) {
		xilinx_osd_disable_rue(plane->manager->osd);

		xilinx_osd_layer_set_dimension(plane->osd_layer, crtc_x, crtc_y,
					       src_w, src_h);

		xilinx_osd_enable_rue(plane->manager->osd);
	}

	if (plane->manager->dp_sub) {
		int ret;

		ret = xilinx_drm_dp_sub_layer_check_size(plane->manager->dp_sub,
							 plane->dp_layer,
							 src_w, src_h);
		if (ret)
			return ret;

		ret = xilinx_drm_dp_sub_layer_set_fmt(plane->manager->dp_sub,
						      plane->dp_layer,
						      fb->pixel_format);
		if (ret) {
			DRM_ERROR("failed to set dp_sub layer fmt\n");
			return ret;
		}
	}

	return 0;
}

/* update a plane. just call mode_set() with bit-shifted values */
static int xilinx_drm_plane_update(struct drm_plane *base_plane,
				   struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   int crtc_x, int crtc_y,
				   unsigned int crtc_w, unsigned int crtc_h,
				   u32 src_x, u32 src_y,
				   u32 src_w, u32 src_h)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	int ret;

	ret = xilinx_drm_plane_mode_set(base_plane, fb,
					crtc_x, crtc_y, crtc_w, crtc_h,
					src_x >> 16, src_y >> 16,
					src_w >> 16, src_h >> 16);
	if (ret) {
		DRM_ERROR("failed to mode-set a plane\n");
		return ret;
	}

	/* make sure a plane is on */
	if (plane->dpms != DRM_MODE_DPMS_ON)
		xilinx_drm_plane_dpms(base_plane, DRM_MODE_DPMS_ON);
	else
		xilinx_drm_plane_commit(base_plane);

	return 0;
}

/* disable a plane */
static int xilinx_drm_plane_disable(struct drm_plane *base_plane)
{
	xilinx_drm_plane_dpms(base_plane, DRM_MODE_DPMS_OFF);

	return 0;
}

/* destroy a plane */
static void xilinx_drm_plane_destroy(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	unsigned int i;

	xilinx_drm_plane_dpms(base_plane, DRM_MODE_DPMS_OFF);

	plane->manager->planes[plane->id] = NULL;

	drm_plane_cleanup(base_plane);

	for (i = 0; i < MAX_NUM_SUB_PLANES; i++)
		if (plane->dma[i].chan)
			dma_release_channel(plane->dma[i].chan);

	if (plane->manager->osd) {
		xilinx_osd_layer_disable(plane->osd_layer);
		xilinx_osd_layer_put(plane->osd_layer);
	}

	if (plane->manager->dp_sub) {
		xilinx_drm_dp_sub_layer_disable(plane->manager->dp_sub,
						plane->dp_layer);
		xilinx_drm_dp_sub_layer_put(plane->manager->dp_sub,
					    plane->dp_layer);
	}
}

/**
 * xilinx_drm_plane_update_prio - Configure plane priorities based on zpos
 * @manager: the plane manager
 *
 * Z-position values are user requested position of planes. The priority is
 * the actual position of planes in hardware. Some hardware doesn't allow
 * any duplicate priority, so this function needs to be called when a duplicate
 * priority is found. Then planes are sorted by zpos value, and the priorities
 * are reconfigured. A plane with lower plane ID gets assigned to the lower
 * priority when planes have the same zpos value.
 */
static void
xilinx_drm_plane_update_prio(struct xilinx_drm_plane_manager *manager)
{
	struct xilinx_drm_plane *planes[MAX_PLANES];
	struct xilinx_drm_plane *plane;
	unsigned int i, j;

	/* sort planes by zpos */
	for (i = 0; i < manager->num_planes; i++) {
		plane = manager->planes[i];

		for (j = i; j > 0; --j) {
			if (planes[j - 1]->zpos <= plane->zpos)
				break;
			planes[j] = planes[j - 1];
		}

		planes[j] = plane;
	}

	xilinx_osd_disable_rue(manager->osd);

	/* remove duplicates by reassigning priority */
	for (i = 0; i < manager->num_planes; i++) {
		planes[i]->prio = i;
		xilinx_osd_layer_set_priority(planes[i]->osd_layer,
					      planes[i]->prio);
	}

	xilinx_osd_enable_rue(manager->osd);
}

static void xilinx_drm_plane_set_zpos(struct drm_plane *base_plane,
				      unsigned int zpos)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_plane_manager *manager = plane->manager;
	bool update = false;
	int i;

	for (i = 0; i < manager->num_planes; i++) {
		if (manager->planes[i] != plane &&
		    manager->planes[i]->prio == zpos) {
			update = true;
			break;
		}
	}

	plane->zpos = zpos;

	if (update) {
		xilinx_drm_plane_update_prio(manager);
	} else {
		plane->prio = zpos;
		xilinx_osd_layer_set_priority(plane->osd_layer, plane->prio);
	}
}

static void xilinx_drm_plane_set_alpha(struct drm_plane *base_plane,
				       unsigned int alpha)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_plane_manager *manager = plane->manager;

	plane->alpha = alpha;

	if (plane->osd_layer)
		xilinx_osd_layer_set_alpha(plane->osd_layer, plane->alpha);
	else if (manager->dp_sub)
		xilinx_drm_dp_sub_set_alpha(manager->dp_sub, plane->alpha);
}

static void xilinx_drm_plane_enable_alpha(struct drm_plane *base_plane,
					  bool enable)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_plane_manager *manager = plane->manager;

	plane->alpha_enable = enable;

	if (plane->osd_layer)
		xilinx_osd_layer_enable_alpha(plane->osd_layer, enable);
	else if (manager->dp_sub)
		xilinx_drm_dp_sub_enable_alpha(manager->dp_sub, enable);
}

/* set property of a plane */
static int xilinx_drm_plane_set_property(struct drm_plane *base_plane,
					 struct drm_property *property,
					 uint64_t val)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_plane_manager *manager = plane->manager;

	if (property == manager->zpos_prop)
		xilinx_drm_plane_set_zpos(base_plane, val);
	else if (property == manager->alpha_prop)
		xilinx_drm_plane_set_alpha(base_plane, val);
	else if (property == manager->alpha_enable_prop)
		xilinx_drm_plane_enable_alpha(base_plane, val);
	else
		return -EINVAL;

	drm_object_property_set_value(&base_plane->base, property, val);

	return 0;
}

static struct drm_plane_funcs xilinx_drm_plane_funcs = {
	.update_plane	= xilinx_drm_plane_update,
	.disable_plane	= xilinx_drm_plane_disable,
	.destroy	= xilinx_drm_plane_destroy,
	.set_property	= xilinx_drm_plane_set_property,
};

/* get a plane max width */
int xilinx_drm_plane_get_max_width(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->manager->max_width;
}

/* check if format is supported */
bool xilinx_drm_plane_check_format(struct xilinx_drm_plane_manager *manager,
				   u32 format)
{
	int i;

	for (i = 0; i < MAX_PLANES; i++)
		if (manager->planes[i] &&
		    (manager->planes[i]->format == format))
			return true;

	return false;
}

/* get the number of planes */
int xilinx_drm_plane_get_num_planes(struct xilinx_drm_plane_manager *manager)
{
	return manager->num_planes;
}

/**
 * xilinx_drm_plane_restore - Restore the plane states
 * @manager: the plane manager
 *
 * Restore the plane states to the default ones. Any state that needs to be
 * restored should be here. This improves consistency as applications see
 * the same default values, and removes mismatch between software and hardware
 * values as software values are updated as hardware values are reset.
 */
void xilinx_drm_plane_restore(struct xilinx_drm_plane_manager *manager)
{
	struct xilinx_drm_plane *plane;
	unsigned int i;

	/*
	 * Reinitialize property default values as they get reset by DPMS OFF
	 * operation. User will read the correct default values later, and
	 * planes will be initialized with default values.
	 */
	for (i = 0; i < manager->num_planes; i++) {
		plane = manager->planes[i];

		plane->prio = plane->id;
		plane->zpos = plane->id;
		if (manager->zpos_prop)
			drm_object_property_set_value(&plane->base.base,
						      manager->zpos_prop,
						      plane->prio);

		plane->alpha = manager->default_alpha;
		if (manager->alpha_prop)
			drm_object_property_set_value(&plane->base.base,
						      manager->alpha_prop,
						      plane->alpha);

		plane->alpha_enable = true;
		if (manager->alpha_enable_prop)
			drm_object_property_set_value(&plane->base.base,
						      manager->alpha_enable_prop, true);
	}
}

/* get the plane format */
u32 xilinx_drm_plane_get_format(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->format;
}

/**
 * xilinx_drm_plane_get_align - Get the alignment value for pitch
 * @base_plane: Base drm plane object
 *
 * Get the alignment value for pitch from the dma device
 *
 * Return: The alignment value if successful, or the error code.
 */
unsigned int xilinx_drm_plane_get_align(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return 1 << plane->dma[0].chan->device->copy_align;
}

/* create plane properties */
static void
xilinx_drm_plane_create_property(struct xilinx_drm_plane_manager *manager)
{
	if (manager->osd)
		manager->zpos_prop = drm_property_create_range(manager->drm, 0,
				"zpos", 0, manager->num_planes - 1);

	if (manager->osd || manager->dp_sub) {
		manager->alpha_prop = drm_property_create_range(manager->drm, 0,
				"alpha", 0, manager->default_alpha);
		manager->alpha_enable_prop =
			drm_property_create_bool(manager->drm, 0,
						 "global alpha enable");
	}
}

/* attach plane properties */
static void xilinx_drm_plane_attach_property(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_plane_manager *manager = plane->manager;

	if (manager->zpos_prop)
		drm_object_attach_property(&base_plane->base,
					   manager->zpos_prop,
					   plane->id);

	if (manager->alpha_prop) {
		if (manager->dp_sub && !plane->primary)
			return;

		drm_object_attach_property(&base_plane->base,
					   manager->alpha_prop,
					   manager->default_alpha);
		drm_object_attach_property(&base_plane->base,
					   manager->alpha_enable_prop, false);
	}

	plane->alpha_enable = true;
}

/**
 * xilinx_drm_plane_manager_dpms - Set DPMS for the Xilinx plane manager
 * @manager: Xilinx plane manager object
 * @dpms: requested DPMS
 *
 * Set the Xilinx plane manager to the given DPMS state. This function is
 * usually called from the CRTC driver with calling xilinx_drm_plane_dpms().
 */
void xilinx_drm_plane_manager_dpms(struct xilinx_drm_plane_manager *manager,
				   int dpms)
{
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		if (manager->dp_sub) {
			xilinx_drm_dp_sub_set_bg_color(manager->dp_sub,
						       0, 0, 0);
			xilinx_drm_dp_sub_enable(manager->dp_sub);
		}

		if (manager->osd) {
			xilinx_osd_disable_rue(manager->osd);
			xilinx_osd_enable(manager->osd);
			xilinx_osd_enable_rue(manager->osd);
		}

		break;
	default:
		if (manager->osd)
			xilinx_osd_reset(manager->osd);

		if (manager->dp_sub)
			xilinx_drm_dp_sub_disable(manager->dp_sub);

		break;
	}
}

/**
 * xilinx_drm_plane_manager_mode_set - Set the mode to the Xilinx plane manager
 * @manager: Xilinx plane manager object
 * @crtc_w: CRTC width
 * @crtc_h: CRTC height
 *
 * Set the width and height of the Xilinx plane manager. This function is uaully
 * called from the CRTC driver before calling the xilinx_drm_plane_mode_set().
 */
void xilinx_drm_plane_manager_mode_set(struct xilinx_drm_plane_manager *manager,
				       unsigned int crtc_w, unsigned int crtc_h)
{
	if (manager->osd)
		xilinx_osd_set_dimension(manager->osd, crtc_w, crtc_h);
}

/* create a plane */
static struct xilinx_drm_plane *
xilinx_drm_plane_create(struct xilinx_drm_plane_manager *manager,
			unsigned int possible_crtcs, bool primary)
{
	struct xilinx_drm_plane *plane;
	struct device *dev = manager->drm->dev;
	char plane_name[16];
	struct device_node *plane_node;
	struct device_node *sub_node;
	struct property *prop;
	const char *dma_name;
	enum drm_plane_type type;
	u32 fmt_in = 0;
	u32 fmt_out = 0;
	const char *fmt;
	int i;
	int ret;
	u32 *fmts = NULL;
	unsigned int num_fmts = 0;

	for (i = 0; i < manager->num_planes; i++)
		if (!manager->planes[i])
			break;

	if (i >= manager->num_planes) {
		DRM_ERROR("failed to allocate plane\n");
		return ERR_PTR(-ENODEV);
	}

	snprintf(plane_name, sizeof(plane_name), "plane%d", i);
	plane_node = of_get_child_by_name(manager->node, plane_name);
	if (!plane_node) {
		DRM_ERROR("failed to find a plane node\n");
		return ERR_PTR(-ENODEV);
	}

	plane = devm_kzalloc(dev, sizeof(*plane), GFP_KERNEL);
	if (!plane) {
		ret = -ENOMEM;
		goto err_out;
	}

	plane->primary = primary;
	plane->id = i;
	plane->prio = i;
	plane->zpos = i;
	plane->alpha = manager->default_alpha;
	plane->dpms = DRM_MODE_DPMS_OFF;
	plane->format = 0;
	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);

	i = 0;
	of_property_for_each_string(plane_node, "dma-names", prop, dma_name) {
		if (i >= MAX_NUM_SUB_PLANES) {
			DRM_WARN("%s contains too many sub-planes (dma-names), indexes %d and above ignored\n",
				 of_node_full_name(plane_node),
				 MAX_NUM_SUB_PLANES);
			break;
		}
		plane->dma[i].chan = of_dma_request_slave_channel(plane_node,
								  dma_name);
		if (IS_ERR(plane->dma[i].chan)) {
			ret = PTR_ERR(plane->dma[i].chan);
			DRM_ERROR("failed to request dma channel \"%s\" for plane %s (err:%d)\n",
				  dma_name, of_node_full_name(plane_node), ret);
			plane->dma[i].chan = NULL;
			goto err_dma;
		}
		++i;
	}

	if (i == 0) {
		DRM_ERROR("plane \"%s\" doesn't have any dma channels (dma-names)\n",
			  of_node_full_name(plane_node));
		ret = -EINVAL;
		goto err_out;
	}

	/* probe color space converter */
	sub_node = of_parse_phandle(plane_node, "xlnx,rgb2yuv", i);
	if (sub_node) {
		plane->rgb2yuv = xilinx_rgb2yuv_probe(dev, sub_node);
		of_node_put(sub_node);
		if (IS_ERR(plane->rgb2yuv)) {
			DRM_ERROR("failed to probe a rgb2yuv\n");
			ret = PTR_ERR(plane->rgb2yuv);
			goto err_dma;
		}

		/* rgb2yuv input format */
		plane->format = DRM_FORMAT_XRGB8888;

		/* rgb2yuv output format */
		fmt_out = DRM_FORMAT_YUV444;
	}

	/* probe chroma resampler */
	sub_node = of_parse_phandle(plane_node, "xlnx,cresample", i);
	if (sub_node) {
		plane->cresample = xilinx_cresample_probe(dev, sub_node);
		of_node_put(sub_node);
		if (IS_ERR(plane->cresample)) {
			DRM_ERROR("failed to probe a cresample\n");
			ret = PTR_ERR(plane->cresample);
			goto err_dma;
		}

		/* cresample input format */
		fmt = xilinx_cresample_get_input_format_name(plane->cresample);
		ret = xilinx_drm_format_by_name(fmt, &fmt_in);
		if (ret)
			goto err_dma;

		/* format sanity check */
		if ((fmt_out != 0) && (fmt_out != fmt_in)) {
			DRM_ERROR("input/output format mismatch\n");
			ret = -EINVAL;
			goto err_dma;
		}

		if (plane->format == 0)
			plane->format = fmt_in;

		/* cresample output format */
		fmt = xilinx_cresample_get_output_format_name(plane->cresample);
		ret = xilinx_drm_format_by_name(fmt, &fmt_out);
		if (ret)
			goto err_dma;
	}

	/* create an OSD layer when OSD is available */
	if (manager->osd) {
		/* format sanity check */
		if ((fmt_out != 0) && (fmt_out != manager->format)) {
			DRM_ERROR("input/output format mismatch\n");
			ret = -EINVAL;
			goto err_dma;
		}

		/* create an osd layer */
		plane->osd_layer = xilinx_osd_layer_get(manager->osd);
		if (IS_ERR(plane->osd_layer)) {
			DRM_ERROR("failed to create a osd layer\n");
			ret = PTR_ERR(plane->osd_layer);
			plane->osd_layer = NULL;
			goto err_dma;
		}

		if (plane->format == 0)
			plane->format = manager->format;
	}

	if (manager->dp_sub) {
		plane->dp_layer = xilinx_drm_dp_sub_layer_get(manager->dp_sub,
							      primary);
		if (IS_ERR(plane->dp_layer)) {
			DRM_ERROR("failed to create a dp_sub layer\n");
			ret = PTR_ERR(plane->dp_layer);
			plane->dp_layer = NULL;
			goto err_dma;
		}

		if (primary) {
			ret = xilinx_drm_dp_sub_layer_set_fmt(manager->dp_sub,
							      plane->dp_layer,
							      manager->format);
			if (ret) {
				DRM_ERROR("failed to set dp_sub layer fmt\n");
				goto err_dma;
			}
		}

		plane->format =
			xilinx_drm_dp_sub_layer_get_fmt(manager->dp_sub,
							plane->dp_layer);
		xilinx_drm_dp_sub_layer_get_fmts(manager->dp_sub,
						 plane->dp_layer, &fmts,
						 &num_fmts);
	}

	/* If there's no IP other than VDMA, pick the manager's format */
	if (plane->format == 0)
		plane->format = manager->format;

	/* initialize drm plane */
	type = primary ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;
	ret = drm_universal_plane_init(manager->drm, &plane->base,
				       possible_crtcs, &xilinx_drm_plane_funcs,
				       fmts ? fmts : &plane->format,
				       num_fmts ? num_fmts : 1, type, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize plane\n");
		goto err_init;
	}
	plane->manager = manager;
	manager->planes[plane->id] = plane;

	xilinx_drm_plane_attach_property(&plane->base);

	of_node_put(plane_node);

	return plane;

err_init:
	if (manager->dp_sub) {
		xilinx_drm_dp_sub_layer_disable(manager->dp_sub,
						plane->dp_layer);
		xilinx_drm_dp_sub_layer_put(plane->manager->dp_sub,
					    plane->dp_layer);
	}
	if (manager->osd) {
		xilinx_osd_layer_disable(plane->osd_layer);
		xilinx_osd_layer_put(plane->osd_layer);
	}
err_dma:
	for (i = 0; i < MAX_NUM_SUB_PLANES; i++)
		if (plane->dma[i].chan)
			dma_release_channel(plane->dma[i].chan);
err_out:
	of_node_put(plane_node);
	return ERR_PTR(ret);
}

/* create a primary plane */
struct drm_plane *
xilinx_drm_plane_create_primary(struct xilinx_drm_plane_manager *manager,
				unsigned int possible_crtcs)
{
	struct xilinx_drm_plane *plane;

	plane = xilinx_drm_plane_create(manager, possible_crtcs, true);
	if (IS_ERR(plane)) {
		DRM_ERROR("failed to allocate a primary plane\n");
		return ERR_CAST(plane);
	}

	return &plane->base;
}

/* create extra planes */
int xilinx_drm_plane_create_planes(struct xilinx_drm_plane_manager *manager,
				   unsigned int possible_crtcs)
{
	struct xilinx_drm_plane *plane;
	int i;

	/* find if there any available plane, and create if available */
	for (i = 0; i < manager->num_planes; i++) {
		if (manager->planes[i])
			continue;

		plane = xilinx_drm_plane_create(manager, possible_crtcs, false);
		if (IS_ERR(plane)) {
			DRM_ERROR("failed to allocate a plane\n");
			return PTR_ERR(plane);
		}

		manager->planes[i] = plane;
	}

	return 0;
}

/* initialize a plane manager: num_planes, format, max_width */
static int
xilinx_drm_plane_init_manager(struct xilinx_drm_plane_manager *manager)
{
	unsigned int format;
	u32 drm_format;
	int ret = 0;

	if (manager->osd) {
		manager->num_planes = xilinx_osd_get_num_layers(manager->osd);
		manager->max_width = xilinx_osd_get_max_width(manager->osd);

		format = xilinx_osd_get_format(manager->osd);
		ret = xilinx_drm_format_by_code(format, &drm_format);
		if (drm_format != manager->format)
			ret = -EINVAL;
	} else if (manager->dp_sub) {
		manager->num_planes = XILINX_DRM_DP_SUB_NUM_LAYERS;
		manager->max_width = XILINX_DRM_DP_SUB_MAX_WIDTH;
	} else {
		/* without osd, only one plane is supported */
		manager->num_planes = 1;
		manager->max_width = 4096;
	}

	return ret;
}

struct xilinx_drm_plane_manager *
xilinx_drm_plane_probe_manager(struct drm_device *drm)
{
	struct xilinx_drm_plane_manager *manager;
	struct device *dev = drm->dev;
	struct device_node *sub_node;
	const char *format;
	int ret;

	manager = devm_kzalloc(dev, sizeof(*manager), GFP_KERNEL);
	if (!manager)
		return ERR_PTR(-ENOMEM);

	/* this node is used to create a plane */
	manager->node = of_get_child_by_name(dev->of_node, "planes");
	if (!manager->node) {
		DRM_ERROR("failed to get a planes node\n");
		return ERR_PTR(-EINVAL);
	}

	/* check the base pixel format of plane manager */
	ret = of_property_read_string(manager->node, "xlnx,pixel-format",
				      &format);
	if (ret < 0) {
		DRM_ERROR("failed to get a plane manager format\n");
		return ERR_PTR(ret);
	}

	ret = xilinx_drm_format_by_name(format, &manager->format);
	if (ret < 0) {
		DRM_ERROR("invalid plane manager format\n");
		return ERR_PTR(ret);
	}

	manager->drm = drm;

	/* probe an OSD. proceed even if there's no OSD */
	sub_node = of_parse_phandle(dev->of_node, "xlnx,osd", 0);
	if (sub_node) {
		manager->osd = xilinx_osd_probe(dev, sub_node);
		of_node_put(sub_node);
		if (IS_ERR(manager->osd)) {
			of_node_put(manager->node);
			DRM_ERROR("failed to probe an osd\n");
			return ERR_CAST(manager->osd);
		}
		manager->default_alpha = OSD_MAX_ALPHA;
	}

	manager->dp_sub = xilinx_drm_dp_sub_of_get(drm->dev->of_node);
	if (IS_ERR(manager->dp_sub)) {
		DRM_DEBUG_KMS("failed to get a dp_sub\n");
		return ERR_CAST(manager->dp_sub);
	} else if (manager->dp_sub) {
		manager->default_alpha = XILINX_DRM_DP_SUB_MAX_ALPHA;
	}

	ret = xilinx_drm_plane_init_manager(manager);
	if (ret) {
		DRM_ERROR("failed to init a plane manager\n");
		return ERR_PTR(ret);
	}

	xilinx_drm_plane_create_property(manager);

	return manager;
}

void xilinx_drm_plane_remove_manager(struct xilinx_drm_plane_manager *manager)
{
	xilinx_drm_dp_sub_put(manager->dp_sub);
	of_node_put(manager->node);
}

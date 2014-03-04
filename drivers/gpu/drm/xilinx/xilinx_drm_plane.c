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

#include <linux/amba/xilinx_dma.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>

#include "xilinx_drm_drv.h"
#include "xilinx_drm_plane.h"

#include "xilinx_cresample.h"
#include "xilinx_osd.h"
#include "xilinx_rgb2yuv.h"

/**
 * struct xilinx_drm_plane_vdma - Xilinx drm plane VDMA object
 *
 * @chan: dma channel
 * @dma_config: vdma config
 */
struct xilinx_drm_plane_vdma {
	struct dma_chan *chan;
	struct xilinx_vdma_config dma_config;
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
 * @priv: flag for private plane
 * @x: x position
 * @y: y position
 * @paddr: physical address of current plane buffer
 * @bpp: bytes per pixel
 * @format: pixel format
 * @vdma: vdma object
 * @rgb2yuv: rgb2yuv instance
 * @cresample: cresample instance
 * @osd_layer: osd layer
 * @manager: plane manager
 */
struct xilinx_drm_plane {
	struct drm_plane base;
	int id;
	int dpms;
	unsigned int zpos;
	unsigned int prio;
	unsigned int alpha;
	bool priv;
	uint32_t x;
	uint32_t y;
	dma_addr_t paddr;
	int bpp;
	uint32_t format;
	struct xilinx_drm_plane_vdma vdma;
	struct xilinx_rgb2yuv *rgb2yuv;
	struct xilinx_cresample *cresample;
	struct xilinx_osd_layer *osd_layer;
	struct xilinx_drm_plane_manager *manager;
};

#define MAX_PLANES 8

/**
 * struct xilinx_drm_plane_manager - Xilinx drm plane manager object
 *
 * @drm: drm device
 * @node: plane device node
 * @osd: osd instance
 * @num_planes: number of available planes
 * @format: video format
 * @max_width: maximum width
 * @zpos_prop: z-position(priority) property
 * @alpha_prop: alpha value property
 * @default_alpha: default alpha value
 * @planes: xilinx drm planes
 */
struct xilinx_drm_plane_manager {
	struct drm_device *drm;
	struct device_node *node;
	struct xilinx_osd *osd;
	int num_planes;
	uint32_t format;
	int max_width;
	struct drm_property *zpos_prop;
	struct drm_property *alpha_prop;
	unsigned int default_alpha;
	struct xilinx_drm_plane *planes[MAX_PLANES];
};

#define to_xilinx_plane(x)	container_of(x, struct xilinx_drm_plane, base)

/* set plane dpms */
void xilinx_drm_plane_dpms(struct drm_plane *base_plane, int dpms)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_plane_manager *manager = plane->manager;
	struct xilinx_vdma_config dma_config;

	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);
	DRM_DEBUG_KMS("dpms: %d -> %d\n", plane->dpms, dpms);

	if (plane->dpms == dpms)
		return;

	plane->dpms = dpms;
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		/* start vdma engine */
		dma_async_issue_pending(plane->vdma.chan);

		if (plane->rgb2yuv)
			xilinx_rgb2yuv_enable(plane->rgb2yuv);

		if (plane->cresample)
			xilinx_cresample_enable(plane->cresample);

		/* enable osd */
		if (manager->osd) {
			xilinx_osd_disable_rue(manager->osd);

			xilinx_osd_layer_set_priority(plane->osd_layer,
						      plane->prio);
			xilinx_osd_layer_set_alpha(plane->osd_layer, 1,
						   plane->alpha);
			xilinx_osd_layer_enable(plane->osd_layer);
			if (plane->priv) {
				/* set background color as black */
				xilinx_osd_set_color(manager->osd, 0x0, 0x0,
						     0x0);
				xilinx_osd_enable(manager->osd);
			}

			xilinx_osd_enable_rue(manager->osd);
		}

		break;
	default:
		/* disable/reset osd */
		if (manager->osd) {
			xilinx_osd_disable_rue(manager->osd);

			xilinx_osd_layer_set_dimension(plane->osd_layer,
						       0, 0, 0, 0);
			xilinx_osd_layer_disable(plane->osd_layer);
			if (plane->priv)
				xilinx_osd_reset(manager->osd);

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

		/* reset vdma */
		dma_config.reset = 1;
		dmaengine_device_control(plane->vdma.chan, DMA_SLAVE_CONFIG,
					 (unsigned long)&dma_config);

		/* stop vdma engine and release descriptors */
		dmaengine_terminate_all(plane->vdma.chan);
		break;
	}
}

/* apply mode to plane pipe */
void xilinx_drm_plane_commit(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct dma_async_tx_descriptor *desc;
	uint32_t height = plane->vdma.dma_config.hsize;
	int pitch = plane->vdma.dma_config.stride;
	size_t offset;

	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);

	offset = plane->x * plane->bpp + plane->y * pitch;
	desc = dmaengine_prep_slave_single(plane->vdma.chan,
					   plane->paddr + offset,
					   height * pitch, DMA_MEM_TO_DEV, 0);
	if (!desc) {
		DRM_ERROR("failed to prepare DMA descriptor\n");
		return;
	}

	/* submit vdma desc */
	dmaengine_submit(desc);

	/* start vdma with new mode */
	dma_async_issue_pending(plane->vdma.chan);
}

/* mode set a plane */
int xilinx_drm_plane_mode_set(struct drm_plane *base_plane,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      unsigned int crtc_w, unsigned int crtc_h,
			      uint32_t src_x, uint32_t src_y,
			      uint32_t src_w, uint32_t src_h)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct drm_gem_cma_object *obj;

	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);

	if (fb->pixel_format != plane->format) {
		DRM_ERROR("unsupported pixel format %08x\n", fb->pixel_format);
		return -EINVAL;
	}

	/* configure cresample */
	if (plane->cresample)
		xilinx_cresample_configure(plane->cresample, crtc_w, crtc_h);

	/* configure rgb2yuv */
	if (plane->rgb2yuv)
		xilinx_rgb2yuv_configure(plane->rgb2yuv, crtc_w, crtc_h);

	obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!obj) {
		DRM_ERROR("failed to get a gem obj for fb\n");
		return -EINVAL;
	}

	plane->x = src_x;
	plane->y = src_y;
	plane->bpp = fb->bits_per_pixel / 8;
	plane->paddr = obj->paddr;

	DRM_DEBUG_KMS("h: %d(%d), v: %d(%d), paddr: %p\n",
		      src_w, crtc_x, src_h, crtc_y, (void *)obj->paddr);
	DRM_DEBUG_KMS("bpp: %d\n", plane->bpp);

	/* configure vdma desc */
	plane->vdma.dma_config.hsize = src_w * plane->bpp;
	plane->vdma.dma_config.vsize = src_h;
	plane->vdma.dma_config.stride = fb->pitches[0];
	plane->vdma.dma_config.park = 1;
	plane->vdma.dma_config.park_frm = 0;

	dmaengine_device_control(plane->vdma.chan, DMA_SLAVE_CONFIG,
				 (unsigned long)&plane->vdma.dma_config);

	/* set OSD dimensions */
	if (plane->manager->osd) {
		xilinx_osd_disable_rue(plane->manager->osd);

		/* if a plane is private, it's for crtc */
		if (plane->priv)
			xilinx_osd_set_dimension(plane->manager->osd,
						 crtc_w, crtc_h);

		xilinx_osd_layer_set_dimension(plane->osd_layer, crtc_x, crtc_y,
					       src_w, src_h);

		xilinx_osd_enable_rue(plane->manager->osd);
	}

	return 0;
}

/* update a plane. just call mode_set() with bit-shifted values */
static int xilinx_drm_plane_update(struct drm_plane *base_plane,
				   struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   int crtc_x, int crtc_y,
				   unsigned int crtc_w, unsigned int crtc_h,
				   uint32_t src_x, uint32_t src_y,
				   uint32_t src_w, uint32_t src_h)
{
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
	xilinx_drm_plane_dpms(base_plane, DRM_MODE_DPMS_ON);
	/* apply the new fb addr */
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

	xilinx_drm_plane_dpms(base_plane, DRM_MODE_DPMS_OFF);

	plane->manager->planes[plane->id] = NULL;

	drm_plane_cleanup(base_plane);

	dma_release_channel(plane->vdma.chan);

	if (plane->manager->osd) {
		xilinx_osd_layer_disable(plane->osd_layer);
		xilinx_osd_layer_put(plane->osd_layer);
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

void xilinx_drm_plane_set_zpos(struct drm_plane *base_plane, unsigned int zpos)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_plane_manager *manager = plane->manager;
	bool update = false;
	int i;

	if (plane->zpos == zpos)
		return;

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

void xilinx_drm_plane_set_alpha(struct drm_plane *base_plane,
				unsigned int alpha)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	if (plane->alpha == alpha)
		return;

	plane->alpha = alpha;

	/* FIXME: use global alpha for now */
	xilinx_osd_layer_set_alpha(plane->osd_layer, 1, plane->alpha);
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

/* get the max alpha value */
unsigned int xilinx_drm_plane_get_max_alpha(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->manager->default_alpha;
}

/* get the default z-position value which is the plane id */
unsigned int xilinx_drm_plane_get_default_zpos(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->id;
}

/* check if format is supported */
bool xilinx_drm_plane_check_format(struct xilinx_drm_plane_manager *manager,
				   uint32_t format)
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

		plane->prio = plane->zpos = plane->id;
		if (manager->zpos_prop)
			drm_object_property_set_value(&plane->base.base,
						      manager->zpos_prop,
						      plane->prio);

		plane->alpha = manager->default_alpha;
		if (manager->alpha_prop)
			drm_object_property_set_value(&plane->base.base,
						      manager->alpha_prop,
						      plane->alpha);
	}
}

/* get the plane format */
uint32_t xilinx_drm_plane_get_format(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	return plane->format;
}

/* create plane properties */
static void
xilinx_drm_plane_create_property(struct xilinx_drm_plane_manager *manager)
{
	if (!manager->osd)
		return;

	manager->zpos_prop = drm_property_create_range(manager->drm, 0,
						       "zpos", 0,
						       manager->num_planes - 1);

	manager->alpha_prop = drm_property_create_range(manager->drm, 0,
							"alpha", 0,
							manager->default_alpha);
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

	if (manager->alpha_prop)
		drm_object_attach_property(&base_plane->base,
					   manager->alpha_prop,
					   manager->default_alpha);
}

/* create a plane */
static struct xilinx_drm_plane *
xilinx_drm_plane_create(struct xilinx_drm_plane_manager *manager,
			unsigned int possible_crtcs, bool priv)
{
	struct xilinx_drm_plane *plane;
	struct device *dev = manager->drm->dev;
	char plane_name[16];
	struct device_node *plane_node;
	struct device_node *sub_node;
	uint32_t fmt_in = -1;
	uint32_t fmt_out = -1;
	const char *fmt;
	int i;
	int ret;

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

	plane->priv = priv;
	plane->id = i;
	plane->prio = i;
	plane->zpos = i;
	plane->alpha = manager->default_alpha;
	plane->dpms = DRM_MODE_DPMS_OFF;
	plane->format = -1;
	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);

	plane->vdma.chan = of_dma_request_slave_channel(plane_node, "vdma");
	if (!plane->vdma.chan) {
		DRM_ERROR("failed to request dma channel\n");
		ret = -ENODEV;
		goto err_out;
	}

	/* probe color space converter */
	sub_node = of_parse_phandle(plane_node, "rgb2yuv", i);
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
	sub_node = of_parse_phandle(plane_node, "cresample", i);
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
		if ((fmt_out != -1) && (fmt_out != fmt_in)) {
			DRM_ERROR("input/output format mismatch\n");
			ret = -EINVAL;
			goto err_dma;
		}

		if (plane->format == -1)
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
		if ((fmt_out != -1) && (fmt_out != manager->format)) {
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

		if (plane->format == -1)
			plane->format = manager->format;
	}

	/* If there's no IP other than VDMA, pick the manager's format */
	if (plane->format == -1)
		plane->format = manager->format;

	/* initialize drm plane */
	ret = drm_plane_init(manager->drm, &plane->base, possible_crtcs,
			     &xilinx_drm_plane_funcs, &plane->format, 1, priv);
	if (ret) {
		DRM_ERROR("failed to initialize plane\n");
		goto err_init;
	}
	plane->manager = manager;
	manager->planes[i] = plane;

	of_node_put(plane_node);

	return plane;

err_init:
	if (manager->osd) {
		xilinx_osd_layer_disable(plane->osd_layer);
		xilinx_osd_layer_put(plane->osd_layer);
	}
err_dma:
	dma_release_channel(plane->vdma.chan);
err_out:
	of_node_put(plane_node);
	return ERR_PTR(ret);
}

/* create a private plane */
struct drm_plane *
xilinx_drm_plane_create_private(struct xilinx_drm_plane_manager *manager,
				unsigned int possible_crtcs)
{
	struct xilinx_drm_plane *plane;

	plane = xilinx_drm_plane_create(manager, possible_crtcs, true);
	if (IS_ERR(plane)) {
		DRM_ERROR("failed to allocate a private plane\n");
		return ERR_CAST(plane);
	}

	return &plane->base;
}

void xilinx_drm_plane_destroy_private(struct xilinx_drm_plane_manager *manager,
				      struct drm_plane *base_plane)
{
	xilinx_drm_plane_destroy(base_plane);
}

/* destroy planes */
void xilinx_drm_plane_destroy_planes(struct xilinx_drm_plane_manager *manager)
{
	struct xilinx_drm_plane *plane;
	int i;

	for (i = 0; i < manager->num_planes; i++) {
		plane = manager->planes[i];
		if (plane && !plane->priv) {
			xilinx_drm_plane_destroy(&plane->base);
			manager->planes[i] = NULL;
		}
	}
}

/* create extra planes */
int xilinx_drm_plane_create_planes(struct xilinx_drm_plane_manager *manager,
				   unsigned int possible_crtcs)
{
	struct xilinx_drm_plane *plane;
	int i;
	int err_ret;

	xilinx_drm_plane_create_property(manager);

	/* find if there any available plane, and create if available */
	for (i = 0; i < manager->num_planes; i++) {
		if (manager->planes[i])
			continue;

		plane = xilinx_drm_plane_create(manager, possible_crtcs, false);
		if (IS_ERR(plane)) {
			DRM_ERROR("failed to allocate a plane\n");
			err_ret = PTR_ERR(plane);
			goto err_out;
		}

		xilinx_drm_plane_attach_property(&plane->base);

		manager->planes[i] = plane;
	}

	return 0;

err_out:
	xilinx_drm_plane_destroy_planes(manager);
	return err_ret;
}

/* initialize a plane manager: num_planes, format, max_width */
static int
xilinx_drm_plane_init_manager(struct xilinx_drm_plane_manager *manager)
{
	unsigned int format;
	int ret = 0;

	if (manager->osd) {
		manager->num_planes = xilinx_osd_get_num_layers(manager->osd);
		manager->max_width = xilinx_osd_get_max_width(manager->osd);

		format = xilinx_osd_get_format(manager->osd);
		ret = xilinx_drm_format_by_code(format, &manager->format);
	} else {
		/* without osd, only one plane is supported */
		manager->num_planes = 1;
		/* YUV422 based on the current pipeline design without osd */
		manager->format = DRM_FORMAT_YUV422;
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

	manager->drm = drm;

	/* probe an OSD. proceed even if there's no OSD */
	sub_node = of_parse_phandle(dev->of_node, "osd", 0);
	if (sub_node) {
		manager->osd = xilinx_osd_probe(dev, sub_node);
		of_node_put(sub_node);
		if (IS_ERR(manager->osd)) {
			of_node_put(manager->node);
			DRM_ERROR("failed to probe an osd\n");
			return ERR_CAST(manager->osd);
		}
	}

	ret = xilinx_drm_plane_init_manager(manager);
	if (ret) {
		DRM_ERROR("failed to init a plane manager\n");
		return ERR_PTR(ret);
	}

	manager->default_alpha = OSD_MAX_ALPHA;

	return manager;
}

void xilinx_drm_plane_remove_manager(struct xilinx_drm_plane_manager *manager)
{
	int i;

	for (i = 0; i < manager->num_planes; i++) {
		if (manager->planes[i] && !manager->planes[i]->priv) {
			xilinx_drm_plane_dpms(&manager->planes[i]->base,
					      DRM_MODE_DPMS_OFF);
			xilinx_drm_plane_destroy(&manager->planes[i]->base);
			manager->planes[i] = NULL;
		}
	}

	of_node_put(manager->node);
}

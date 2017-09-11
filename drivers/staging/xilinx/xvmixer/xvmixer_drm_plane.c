/*
 * Xilinx DRM Mixer plane driver for Xilinx
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

#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drmP.h>

#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>

/* drm component libs */
#include "xvmixer_drm_crtc.h"
#include "xvmixer_drm_drv.h"
#include "xvmixer_drm_fb.h"
#include "xvmixer_drm_plane.h"

/* hardware layer libs */
#include "xilinx_drm_mixer.h"

/* set plane dpms */
void xvmixer_drm_plane_dpms(struct drm_plane *base_plane, int dpms)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	unsigned int i;

	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);
	DRM_DEBUG_KMS("dpms: %d -> %d\n", plane->dpms, dpms);

	if (plane->dpms == dpms)
		return;

	plane->dpms = dpms;
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		/* start dma engine */
		for (i = 0; i < MAX_NUM_SUB_PLANES; i++)
			if (plane->dma[i].chan && plane->dma[i].is_active)
				dma_async_issue_pending(plane->dma[i].chan);

		xilinx_drm_mixer_plane_dpms(plane, dpms);

		break;
	default:
		xilinx_drm_mixer_plane_dpms(plane, dpms);

		/* stop dma engine and release descriptors */
		for (i = 0; i < MAX_NUM_SUB_PLANES; i++) {
			if (plane->dma[i].chan && plane->dma[i].is_active) {
				dmaengine_terminate_all(plane->dma[i].chan);
				plane->dma[i].is_active = false;
			}
		}

		break;
	}
}

/* apply mode to plane pipe */
void xvmixer_drm_plane_commit(struct drm_plane *base_plane)
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

/* mode set a plane */
int xvmixer_drm_plane_mode_set(struct drm_plane *base_plane,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      unsigned int crtc_w, unsigned int crtc_h,
			      uint32_t src_x, uint32_t src_y,
			      uint32_t src_w, uint32_t src_h)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct drm_gem_cma_object *obj;
	size_t offset = 0;
	unsigned int hsub, vsub, fb_plane_cnt, i = 0;
	u32 padding_factor_nume, padding_factor_deno, cpp_nume, cpp_deno;
	int ret;


	/* JPM TODO begin start of code to extract into prep-interleaved*/
	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);
	DRM_DEBUG_KMS("h: %d(%d), v: %d(%d)\n",
		      src_w, crtc_x, src_h, crtc_y);
	DRM_DEBUG_KMS("bpp: %d\n", fb->bits_per_pixel / 8);

	hsub = drm_format_horz_chroma_subsampling(fb->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(fb->pixel_format);
	fb_plane_cnt = drm_format_num_planes(fb->pixel_format);
	drm_format_width_padding_factor(fb->pixel_format, &padding_factor_nume,
					&padding_factor_deno);
	drm_format_cpp_scaling_factor(fb->pixel_format, &cpp_nume, &cpp_deno);

	/* We have multiple dma channels.  Set each per video plane */
	for (; i < fb_plane_cnt; i++) {
		unsigned int width = src_w / (i ? hsub : 1);
		unsigned int height = src_h / (i ? vsub : 1);
		unsigned int cpp = drm_format_plane_cpp(fb->pixel_format, i);

		obj = xvmixer_drm_fb_get_gem_obj(fb, i);
		if (!obj) {
			DRM_ERROR("failed to get a gem obj for fb\n");
			return -EINVAL;
		}

		plane->dma[i].xt.numf = height;
		plane->dma[i].sgl[0].size = width * cpp;
		plane->dma[i].sgl[0].size =
				(width * cpp * cpp_nume * padding_factor_nume) /
				(cpp_deno * padding_factor_deno);
		offset = src_x * cpp + src_y * fb->pitches[i]; /* JPM fix */
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

	/* Do we have a video format aware dma channel?
	 * If so, modify descriptor accordingly
	 */
	if (plane->dma[0].chan && !plane->dma[1].chan &&
	    fb_plane_cnt > 1) {
		u32 stride = plane->dma[0].sgl[0].size +
				plane->dma[0].sgl[0].icg;

		plane->dma[0].sgl[0].src_icg =
				plane->dma[1].xt.src_start -
				plane->dma[0].xt.src_start -
				(plane->dma[0].xt.numf * stride);
				
		plane->dma[0].xt.frame_size = fb_plane_cnt;
	} 

	ret = xilinx_drm_mixer_set_plane(plane, fb, crtc_x, crtc_y,
					 src_x, src_y, src_w, src_h);
	if (ret)
		return ret;

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

	ret = xvmixer_drm_plane_mode_set(base_plane, fb,
					crtc_x, crtc_y, crtc_w, crtc_h,
					src_x >> 16, src_y >> 16,
					src_w >> 16, src_h >> 16);
	if (ret) {
		DRM_ERROR("failed to mode-set a plane\n");
		return ret;
	}

	/* apply the new fb addr */
	xvmixer_drm_plane_commit(base_plane);

	/* make sure a plane is on */
	xvmixer_drm_plane_dpms(base_plane, DRM_MODE_DPMS_ON);

	return 0;
}

/* disable a plane */
static int xilinx_drm_plane_disable(struct drm_plane *base_plane)
{
	xvmixer_drm_plane_dpms(base_plane, DRM_MODE_DPMS_OFF);

	return 0;
}

/* destroy a plane */
static void xilinx_drm_plane_destroy(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	unsigned int i;
	xvmixer_drm_plane_dpms(base_plane, DRM_MODE_DPMS_OFF);

	drm_plane_cleanup(base_plane);

	for (i = 0; i < MAX_NUM_SUB_PLANES; i++)
		if (plane->dma[i].chan)
			dma_release_channel(plane->dma[i].chan);

	xilinx_drm_mixer_layer_disable(plane);

}

/* set property of a plane */
static int xilinx_drm_plane_set_property(struct drm_plane *base_plane,
					 struct drm_property *property,
					 uint64_t val)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	int ret = 0;

	ret = xilinx_drm_mixer_set_plane_property(plane,
						  property, val);
	return ret;
}

/* JPM TODO: xilinx_drm_plane_funcs not used in this file.  Why not?*/
static struct drm_plane_funcs xilinx_drm_plane_funcs = {
	.update_plane	= xilinx_drm_plane_update,
	.disable_plane	= xilinx_drm_plane_disable,
	.destroy	= xilinx_drm_plane_destroy,
	.set_property	= xilinx_drm_plane_set_property,
};

/* get a plane max width */
int xvmixer_drm_plane_get_max_width(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->mixer->max_width;
}

/* get a plane max height */
int xvmixer_drm_plane_get_max_height(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->mixer->max_height;
}

/* get a plane max width */
int xvmixer_drm_plane_get_max_cursor_width(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->mixer->max_cursor_width;
}

/* get a plane max height */
int xvmixer_drm_plane_get_max_cursor_height(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->mixer->max_cursor_height;
}

/* check if format is supported */
bool xvmixer_drm_plane_check_format(struct xilinx_drm_mixer *mixer,
				   uint32_t format)
{
	int i;

	for (i = 0; i < mixer->num_planes; i++)
		if (&mixer->planes[i] &&
		    (mixer->planes[i].format == format))
			return true;

	return false;
}

/* get the number of planes */
u32 xvmixer_drm_plane_get_num_planes(struct xilinx_drm_mixer *mixer)
{
	return get_num_mixer_planes(mixer);
}

/**
 * xvmixer_drm_plane_restore - Restore the plane states
 * @manager: the plane manager
 *
 * Restore the plane states to the default ones. Any state that needs to be
 * restored should be here. This improves consistency as applications see
 * the same default values, and removes mismatch between software and hardware
 * values as software values are updated as hardware values are reset.
 */
void xvmixer_drm_plane_restore(struct xilinx_drm_mixer *mixer)
{
	struct xilinx_drm_plane *plane;
	unsigned int i;

	if (!mixer)
		return;
	/*
	 * Reinitialize property default values as they get reset by DPMS OFF
	 * operation. User will read the correct default values later, and
	 * planes will be initialized with default values.
	 */
	for (i = 0; i < mixer->num_planes; i++) {
		plane = &mixer->planes[i];

		if (!plane)
			continue;

		xilinx_drm_mixer_plane_dpms(plane, DRM_MODE_DPMS_OFF);

	}
}

/* get the plane format */
uint32_t xvmixer_drm_plane_get_format(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);

	return plane->format;
}

/**
 * xvmixer_drm_plane_get_align - Get the alignment value for pitch
 * @base_plane: Base drm plane object
 *
 * Get the alignment value for pitch from the dma device
 *
 * Return: The alignment value if successful, or the error code.
 */
unsigned int xvmixer_drm_plane_get_align(struct drm_plane *base_plane)
{
	struct xilinx_drm_plane *plane = to_xilinx_plane(base_plane);
	struct xilinx_drm_mixer *m = plane->mixer;

	return get_xilinx_mixer_mem_align(m);
}

int xvmixer_drm_mixer_init_plane(struct xilinx_drm_plane *plane,
				unsigned int poss_crtcs,
				struct device_node *layer_node)
{
	struct xilinx_drm_mixer *mixer = plane->mixer;
	char name[16];
	enum drm_plane_type type;
	int ret, i;

        plane->dpms = DRM_MODE_DPMS_OFF;
        type = DRM_PLANE_TYPE_OVERLAY;

        for (i = 0; i < MAX_NUM_SUB_PLANES; i++) {
                snprintf(name, sizeof(name), "dma%d", i);
                plane->dma[i].chan = of_dma_request_slave_channel(layer_node,
                                                                  name);
                if (PTR_ERR(plane->dma[i].chan) == -ENODEV) {
                        plane->dma[i].chan = NULL;
                        continue;
                }

                if (IS_ERR(plane->dma[i].chan)) {
                        DRM_ERROR("failed to request dma channel\n");
                        ret = PTR_ERR(plane->dma[i].chan);
                        plane->dma[i].chan = NULL;
                        goto err_dma;
                }
        }

	ret = xilinx_drm_mixer_fmt_to_drm_fmt
				(mixer_layer_fmt(plane->mixer_layer),
				 &plane->format);
        if (ret) {
                DRM_ERROR("failed to initialize plane\n");
                goto err_init;
        }

        if (plane == mixer->hw_logo_layer)
                type = DRM_PLANE_TYPE_CURSOR;

        if (plane == mixer->drm_primary_layer)
                type = DRM_PLANE_TYPE_PRIMARY;

        plane->primary = type == DRM_PLANE_TYPE_PRIMARY ? true : false;

        /* initialize drm plane */
        ret = drm_universal_plane_init(mixer->crtc->drm, &plane->base,
                                       poss_crtcs, &xilinx_drm_plane_funcs,
					/*JPM TODO need array for streaming layer*/
                                       &plane->format, 
                                       1, type, NULL);


        if (ret) {
                DRM_ERROR("failed to initialize plane\n");
                goto err_init;
        }

        of_node_put(layer_node);

        return 0;

err_init:
        xilinx_drm_mixer_layer_disable(plane);

err_dma:
        for (i = 0; i < MAX_NUM_SUB_PLANES; i++)
                if (plane->dma[i].chan)
                        dma_release_channel(plane->dma[i].chan);

        of_node_put(layer_node);
        return (ret);
}

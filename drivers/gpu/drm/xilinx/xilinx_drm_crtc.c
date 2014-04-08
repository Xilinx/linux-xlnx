/*
 * Xilinx DRM crtc driver for Xilinx
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
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>

#include <video/videomode.h>

#include "xilinx_drm_crtc.h"
#include "xilinx_drm_drv.h"
#include "xilinx_drm_plane.h"

#include "xilinx_cresample.h"
#include "xilinx_rgb2yuv.h"
#include "xilinx_vtc.h"

struct xilinx_drm_crtc {
	struct drm_crtc base;
	struct drm_plane *priv_plane;
	struct xilinx_cresample *cresample;
	struct xilinx_rgb2yuv *rgb2yuv;
	struct clk *pixel_clock;
	struct xilinx_vtc *vtc;
	struct xilinx_drm_plane_manager *plane_manager;
	int dpms;
	unsigned int default_zpos;
	unsigned int default_alpha;
	unsigned int alpha;
	struct drm_property *zpos_prop;
	struct drm_property *alpha_prop;
	struct drm_pending_vblank_event *event;
};

#define to_xilinx_crtc(x)	container_of(x, struct xilinx_drm_crtc, base)

/* set crtc dpms */
static void xilinx_drm_crtc_dpms(struct drm_crtc *base_crtc, int dpms)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	DRM_DEBUG_KMS("dpms: %d -> %d\n", crtc->dpms, dpms);

	if (crtc->dpms == dpms)
		return;

	crtc->dpms = dpms;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		xilinx_drm_plane_dpms(crtc->priv_plane, dpms);
		if (crtc->rgb2yuv)
			xilinx_rgb2yuv_enable(crtc->rgb2yuv);
		if (crtc->cresample)
			xilinx_cresample_enable(crtc->cresample);
		xilinx_vtc_enable(crtc->vtc);
		break;
	default:
		xilinx_vtc_disable(crtc->vtc);
		xilinx_vtc_reset(crtc->vtc);
		if (crtc->cresample) {
			xilinx_cresample_disable(crtc->cresample);
			xilinx_cresample_reset(crtc->cresample);
		}
		if (crtc->rgb2yuv) {
			xilinx_rgb2yuv_disable(crtc->rgb2yuv);
			xilinx_rgb2yuv_reset(crtc->rgb2yuv);
		}
		xilinx_drm_plane_dpms(crtc->priv_plane, dpms);
		break;
	}
}

/* prepare crtc */
static void xilinx_drm_crtc_prepare(struct drm_crtc *base_crtc)
{
	xilinx_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);
}

/* apply mode to crtc pipe */
static void xilinx_drm_crtc_commit(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	xilinx_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_ON);
	xilinx_drm_plane_commit(crtc->priv_plane);
}

/* fix mode */
static bool xilinx_drm_crtc_mode_fixup(struct drm_crtc *base_crtc,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	/* no op */
	return true;
}

/* set new mode in crtc pipe */
static int xilinx_drm_crtc_mode_set(struct drm_crtc *base_crtc,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode,
				    int x, int y,
				    struct drm_framebuffer *old_fb)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct videomode vm;
	long diff;
	int ret;

	/* set pixel clock */
	ret = clk_set_rate(crtc->pixel_clock, adjusted_mode->clock * 1000);
	if (ret) {
		DRM_ERROR("failed to set a pixel clock\n");
		return ret;
	}

	diff = clk_get_rate(crtc->pixel_clock) - adjusted_mode->clock * 1000;
	if (abs(diff) > (adjusted_mode->clock * 1000) / 20)
		DRM_INFO("actual pixel clock rate(%d) is off by %ld\n",
				adjusted_mode->clock, diff);

	/* set video timing */
	vm.hactive = adjusted_mode->hdisplay;
	vm.hfront_porch = adjusted_mode->hsync_start - adjusted_mode->hdisplay;
	vm.hback_porch = adjusted_mode->htotal - adjusted_mode->hsync_end;
	vm.hsync_len = adjusted_mode->hsync_end - adjusted_mode->hsync_start;

	vm.vactive = adjusted_mode->vdisplay;
	vm.vfront_porch = adjusted_mode->vsync_start - adjusted_mode->vdisplay;
	vm.vback_porch = adjusted_mode->vtotal - adjusted_mode->vsync_end;
	vm.vsync_len = adjusted_mode->vsync_end - adjusted_mode->vsync_start;

	xilinx_vtc_config_sig(crtc->vtc, &vm);

	/* configure cresample and rgb2yuv */
	if (crtc->cresample)
		xilinx_cresample_configure(crtc->cresample,
					   adjusted_mode->hdisplay,
					   adjusted_mode->vdisplay);
	if (crtc->rgb2yuv)
		xilinx_rgb2yuv_configure(crtc->rgb2yuv,
					 adjusted_mode->hdisplay,
					 adjusted_mode->vdisplay);

	/* configure a plane: vdma and osd layer */
	ret = xilinx_drm_plane_mode_set(crtc->priv_plane,
					base_crtc->fb, 0, 0,
					adjusted_mode->hdisplay,
					adjusted_mode->vdisplay,
					x, y,
					adjusted_mode->hdisplay,
					adjusted_mode->vdisplay);
	if (ret) {
		DRM_ERROR("failed to mode set a plane\n");
		return ret;
	}

	return 0;
}

static int _xilinx_drm_crtc_mode_set_base(struct drm_crtc *base_crtc,
					  struct drm_framebuffer *fb,
					  int x, int y)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	int ret;

	/* configure a plane */
	ret = xilinx_drm_plane_mode_set(crtc->priv_plane,
					fb, 0, 0,
					base_crtc->hwmode.hdisplay,
					base_crtc->hwmode.vdisplay,
					x, y,
					base_crtc->hwmode.hdisplay,
					base_crtc->hwmode.vdisplay);
	if (ret) {
		DRM_ERROR("failed to mode set a plane\n");
		return ret;
	}

	/* apply the new fb addr */
	xilinx_drm_crtc_commit(base_crtc);

	return 0;
}

/* update address and information from fb */
static int xilinx_drm_crtc_mode_set_base(struct drm_crtc *base_crtc,
					 int x, int y,
					 struct drm_framebuffer *old_fb)
{
	/* configure a plane */
	return _xilinx_drm_crtc_mode_set_base(base_crtc, base_crtc->fb, x, y);
}

/* load rgb LUT for crtc */
static void xilinx_drm_crtc_load_lut(struct drm_crtc *base_crtc)
{
	/* no op */
}

static struct drm_crtc_helper_funcs xilinx_drm_crtc_helper_funcs = {
	.dpms		= xilinx_drm_crtc_dpms,
	.prepare	= xilinx_drm_crtc_prepare,
	.commit		= xilinx_drm_crtc_commit,
	.mode_fixup	= xilinx_drm_crtc_mode_fixup,
	.mode_set	= xilinx_drm_crtc_mode_set,
	.mode_set_base	= xilinx_drm_crtc_mode_set_base,
	.load_lut	= xilinx_drm_crtc_load_lut,
};

/* destroy crtc */
void xilinx_drm_crtc_destroy(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	/* make sure crtc is off */
	xilinx_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);

	drm_crtc_cleanup(base_crtc);

	clk_disable_unprepare(crtc->pixel_clock);

	xilinx_drm_plane_destroy_planes(crtc->plane_manager);
	xilinx_drm_plane_destroy_private(crtc->plane_manager, crtc->priv_plane);
	xilinx_drm_plane_remove_manager(crtc->plane_manager);
}

/* cancel page flip functions */
void xilinx_drm_crtc_cancel_page_flip(struct drm_crtc *base_crtc,
				      struct drm_file *file)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	event = crtc->event;
	if (event && (event->base.file_priv == file)) {
		crtc->event = NULL;
		event->base.destroy(&event->base);
		drm_vblank_put(drm, 0);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

/* finish page flip functions */
static void xilinx_drm_crtc_finish_page_flip(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	event = crtc->event;
	crtc->event = NULL;
	if (event) {
		drm_send_vblank_event(drm, 0, event);
		drm_vblank_put(drm, 0);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

/* page flip functions */
static int xilinx_drm_crtc_page_flip(struct drm_crtc *base_crtc,
				     struct drm_framebuffer *fb,
				     struct drm_pending_vblank_event *event,
				     uint32_t page_flip_flags)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&drm->event_lock, flags);
	if (crtc->event != NULL) {
		spin_unlock_irqrestore(&drm->event_lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);

	/* configure a plane */
	ret = _xilinx_drm_crtc_mode_set_base(base_crtc, fb,
					     base_crtc->x, base_crtc->y);
	if (ret) {
		DRM_ERROR("failed to mode set a plane\n");
		return ret;
	}

	base_crtc->fb = fb;

	if (event) {
		event->pipe = 0;
		drm_vblank_get(drm, 0);
		spin_lock_irqsave(&drm->event_lock, flags);
		crtc->event = event;
		spin_unlock_irqrestore(&drm->event_lock, flags);
	}

	return 0;
}

/* set property of a plane */
static int xilinx_drm_crtc_set_property(struct drm_crtc *base_crtc,
					struct drm_property *property,
					uint64_t val)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	if (property == crtc->zpos_prop)
		xilinx_drm_plane_set_zpos(crtc->priv_plane, val);
	else if (property == crtc->alpha_prop)
		xilinx_drm_plane_set_alpha(crtc->priv_plane, val);
	else
		return -EINVAL;

	drm_object_property_set_value(&base_crtc->base, property, val);

	return 0;
}

/* vblank interrupt handler */
static void xilinx_drm_crtc_vblank_handler(void *data)
{
	struct drm_crtc *base_crtc = data;
	struct drm_device *drm;

	if (!base_crtc)
		return;

	drm = base_crtc->dev;

	drm_handle_vblank(drm, 0);
	xilinx_drm_crtc_finish_page_flip(base_crtc);
}

/* enable vblank interrupt */
void xilinx_drm_crtc_enable_vblank(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	xilinx_vtc_enable_vblank_intr(crtc->vtc, xilinx_drm_crtc_vblank_handler,
				      base_crtc);
}

/* disable vblank interrupt */
void xilinx_drm_crtc_disable_vblank(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	xilinx_vtc_disable_vblank_intr(crtc->vtc);
}

/**
 * xilinx_drm_crtc_restore - Restore the crtc states
 * @base_crtc: base crtc object
 *
 * Restore the crtc states to the default ones. The request is propagated
 * to the plane driver.
 */
void xilinx_drm_crtc_restore(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	/*
	 * Reinitialize the property values, so correct values are read
	 * for these properties.
	 */
	if (crtc->zpos_prop) {
		xilinx_drm_plane_set_zpos(crtc->priv_plane, crtc->default_zpos);
		drm_object_property_set_value(&base_crtc->base, crtc->zpos_prop,
					      crtc->default_zpos);
	}

	if (crtc->alpha_prop) {
		xilinx_drm_plane_set_alpha(crtc->priv_plane,
					   crtc->default_alpha);
		drm_object_property_set_value(&base_crtc->base,
					      crtc->alpha_prop,
					      crtc->default_alpha);
	}

	xilinx_drm_plane_restore(crtc->plane_manager);
}

/* check max width */
unsigned int xilinx_drm_crtc_get_max_width(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	return xilinx_drm_plane_get_max_width(crtc->priv_plane);
}

/* check format */
bool xilinx_drm_crtc_check_format(struct drm_crtc *base_crtc, uint32_t fourcc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	return xilinx_drm_plane_check_format(crtc->plane_manager, fourcc);
}

/* get format */
uint32_t xilinx_drm_crtc_get_format(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	return xilinx_drm_plane_get_format(crtc->priv_plane);
}

static struct drm_crtc_funcs xilinx_drm_crtc_funcs = {
	.destroy	= xilinx_drm_crtc_destroy,
	.set_config	= drm_crtc_helper_set_config,
	.page_flip	= xilinx_drm_crtc_page_flip,
	.set_property	= xilinx_drm_crtc_set_property,
};

/* attach crtc properties */
static void xilinx_drm_crtc_attach_property(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	int num_planes;

	num_planes = xilinx_drm_plane_get_num_planes(crtc->plane_manager);
	if (num_planes <= 1)
		return;

	crtc->zpos_prop = drm_property_create_range(base_crtc->dev, 0, "zpos",
						    0, num_planes - 1);
	if (crtc->zpos_prop) {
		crtc->default_zpos =
			xilinx_drm_plane_get_default_zpos(crtc->priv_plane);
		drm_object_attach_property(&base_crtc->base, crtc->zpos_prop,
					   crtc->default_zpos);
		xilinx_drm_plane_set_zpos(crtc->priv_plane, crtc->default_zpos);
	}

	crtc->default_alpha = xilinx_drm_plane_get_max_alpha(crtc->priv_plane);
	crtc->alpha_prop = drm_property_create_range(base_crtc->dev, 0,
						      "alpha", 0,
						      crtc->default_alpha);
	if (crtc->alpha_prop) {
		drm_object_attach_property(&base_crtc->base, crtc->alpha_prop,
					   crtc->default_alpha);
		xilinx_drm_plane_set_alpha(crtc->priv_plane,
					   crtc->default_alpha);
	}
}

/* create crtc */
struct drm_crtc *xilinx_drm_crtc_create(struct drm_device *drm)
{
	struct xilinx_drm_crtc *crtc;
	struct device_node *sub_node;
	int possible_crtcs = 1;
	int ret;

	crtc = devm_kzalloc(drm->dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return ERR_PTR(-ENOMEM);

	/* probe chroma resampler and enable */
	sub_node = of_parse_phandle(drm->dev->of_node, "cresample", 0);
	if (sub_node) {
		crtc->cresample = xilinx_cresample_probe(drm->dev, sub_node);
		of_node_put(sub_node);
		if (IS_ERR(crtc->cresample)) {
			DRM_ERROR("failed to probe a cresample\n");
			return ERR_CAST(crtc->cresample);
		}
	}

	/* probe color space converter and enable */
	sub_node = of_parse_phandle(drm->dev->of_node, "rgb2yuv", 0);
	if (sub_node) {
		crtc->rgb2yuv = xilinx_rgb2yuv_probe(drm->dev, sub_node);
		of_node_put(sub_node);
		if (IS_ERR(crtc->rgb2yuv)) {
			DRM_ERROR("failed to probe a rgb2yuv\n");
			return ERR_CAST(crtc->rgb2yuv);
		}
	}

	/* probe a plane manager */
	crtc->plane_manager = xilinx_drm_plane_probe_manager(drm);
	if (IS_ERR(crtc->plane_manager)) {
		DRM_ERROR("failed to probe a plane manager\n");
		return ERR_CAST(crtc->plane_manager);
	}

	/* create a private plane. there's only one crtc now */
	crtc->priv_plane = xilinx_drm_plane_create_private(crtc->plane_manager,
							   possible_crtcs);
	if (IS_ERR(crtc->priv_plane)) {
		DRM_ERROR("failed to create a private plane for crtc\n");
		ret = PTR_ERR(crtc->priv_plane);
		goto err_plane;
	}

	/* create extra planes */
	xilinx_drm_plane_create_planes(crtc->plane_manager, possible_crtcs);

	crtc->pixel_clock = devm_clk_get(drm->dev, NULL);
	if (IS_ERR(crtc->pixel_clock)) {
		DRM_DEBUG_KMS("failed to get pixel clock\n");
		ret = -EPROBE_DEFER;
		goto err_out;
	}

	ret = clk_prepare_enable(crtc->pixel_clock);
	if (ret) {
		DRM_DEBUG_KMS("failed to prepare/enable clock\n");
		goto err_out;
	}

	sub_node = of_parse_phandle(drm->dev->of_node, "vtc", 0);
	if (!sub_node) {
		DRM_ERROR("failed to get a video timing controller node\n");
		ret = -ENODEV;
		goto err_out;
	}

	crtc->vtc = xilinx_vtc_probe(drm->dev, sub_node);
	of_node_put(sub_node);
	if (IS_ERR(crtc->vtc)) {
		DRM_ERROR("failed to probe video timing controller\n");
		ret = PTR_ERR(crtc->vtc);
		goto err_out;
	}

	crtc->dpms = DRM_MODE_DPMS_OFF;

	/* initialize drm crtc */
	ret = drm_crtc_init(drm, &crtc->base, &xilinx_drm_crtc_funcs);
	if (ret) {
		DRM_ERROR("failed to initialize crtc\n");
		goto err_out;
	}
	drm_crtc_helper_add(&crtc->base, &xilinx_drm_crtc_helper_funcs);

	xilinx_drm_crtc_attach_property(&crtc->base);

	return &crtc->base;

err_out:
	xilinx_drm_plane_destroy_planes(crtc->plane_manager);
	xilinx_drm_plane_destroy_private(crtc->plane_manager, crtc->priv_plane);
err_plane:
	xilinx_drm_plane_remove_manager(crtc->plane_manager);
	return ERR_PTR(ret);
}

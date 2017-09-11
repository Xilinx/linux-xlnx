/*
 * Xilinx DRM Mixer crtc driver
 *
 *  Copyright (C) 2017 Xilinx, Inc.
 *
 *  Author: Jeffrey Mouroux <jmouroux@xilinx.com>
 *
 *  Based on Xilinx crtc driver by Hyun Kwon <hyunk@xilinx.com>
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

#include "xvmixer_drm_crtc.h"
#include "xvmixer_drm_drv.h"
#include "xvmixer_drm_plane.h"
#include "xilinx_drm_mixer.h"

/* set crtc dpms */
static void xilinx_drm_crtc_dpms(struct drm_crtc *base_crtc, int dpms)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);
	int ret;

	DRM_DEBUG_KMS("dpms: %d -> %d\n", crtc->dpms, dpms);

	if (crtc->dpms == dpms)
		return;

	crtc->dpms = dpms;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		if (!crtc->pixel_clock_enabled) {
			ret = clk_prepare_enable(crtc->pixel_clock);
			if (ret) {
				DRM_ERROR("failed to enable a pixel clock\n");
				crtc->pixel_clock_enabled = false;
			}
		}
		crtc->pixel_clock_enabled = true;

		xilinx_drm_mixer_dpms(&crtc->mixer, dpms);
		/* JPM TODO revise call below */
		xvmixer_drm_plane_dpms(base_crtc->primary, dpms);
		break;
	default:
		/* JPM TODO revise call below */
		xvmixer_drm_plane_dpms(base_crtc->primary, dpms);
		xilinx_drm_mixer_dpms(&crtc->mixer, dpms);
		if (crtc->pixel_clock_enabled) {
			clk_disable_unprepare(crtc->pixel_clock);
			crtc->pixel_clock_enabled = false;
		}
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
	xvmixer_drm_plane_commit(base_crtc->primary);
	xilinx_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_ON);
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
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);
	long diff;
	int ret;

	if (crtc->pixel_clock_enabled) {
		clk_disable_unprepare(crtc->pixel_clock);
		crtc->pixel_clock_enabled = false;
	}

	/* set pixel clock */
	ret = clk_set_rate(crtc->pixel_clock, adjusted_mode->clock * 1000);
	if (ret) {
		DRM_ERROR("failed to set a pixel clock.  ret code = %d\n", ret);
		return ret;
	}

	diff = clk_get_rate(crtc->pixel_clock) - adjusted_mode->clock * 1000;
	if (abs(diff) > (adjusted_mode->clock * 1000) / 20)
		DRM_DEBUG_KMS("actual pixel clock rate(%d) is off by %ld\n",
			      adjusted_mode->clock, diff);

	ret = xvmixer_drm_plane_mode_set(base_crtc->primary,
					base_crtc->primary->fb, 0, 0,
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
	int ret;

	ret = xvmixer_drm_plane_mode_set(base_crtc->primary,
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
	return _xilinx_drm_crtc_mode_set_base(base_crtc, base_crtc->primary->fb,
	       x, y);
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
void xvmixer_drm_crtc_destroy(struct drm_crtc *base_crtc)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);

	/* make sure crtc is off */
	crtc->mixer.alpha_prop = NULL;
	crtc->mixer.scale_prop = NULL;
	crtc->mixer.bg_color = NULL;

	xilinx_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);

	drm_crtc_cleanup(base_crtc);

	if (crtc->pixel_clock_enabled) {
		clk_disable_unprepare(crtc->pixel_clock);
		crtc->pixel_clock_enabled = false;
	}
}

/* crtc set config helper */
int xilinx_drm_crtc_helper_set_config(struct drm_mode_set *set)
{
	struct drm_device *drm = set->crtc->dev;

	xvmixer_drm_set_config(drm, set);

	return drm_crtc_helper_set_config(set);
}

/* cancel page flip functions */
void xvmixer_drm_crtc_cancel_page_flip(struct drm_crtc *base_crtc,
				      struct drm_file *file)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	event = crtc->event;
	if (event && (event->base.file_priv == file)) {
		crtc->event = NULL;
		kfree(&event->base);
		drm_crtc_vblank_put(base_crtc);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

/* finish page flip functions */
static void xilinx_drm_crtc_finish_page_flip(struct drm_crtc *base_crtc)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	event = crtc->event;
	crtc->event = NULL;
	if (event) {
		drm_crtc_send_vblank_event(base_crtc, event);
		drm_crtc_vblank_put(base_crtc);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

/* page flip functions */
static int xilinx_drm_crtc_page_flip(struct drm_crtc *base_crtc,
				     struct drm_framebuffer *fb,
				     struct drm_pending_vblank_event *event,
				     uint32_t page_flip_flags)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&drm->event_lock, flags);
	if (crtc->event) {
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

	base_crtc->primary->fb = fb;

	if (event) {
		event->pipe = 0;
		drm_crtc_vblank_get(base_crtc);
		spin_lock_irqsave(&drm->event_lock, flags);
		crtc->event = event;
		spin_unlock_irqrestore(&drm->event_lock, flags);
	}

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
void xvmixer_drm_crtc_enable_vblank(struct drm_crtc *base_crtc)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct xilinx_drm_mixer *mixer = &crtc->mixer;

	if (!mixer->mixer_hw.intrpts_enabled)
		return;

	xilinx_drm_mixer_set_intr_handler(mixer,
					  xilinx_drm_crtc_vblank_handler,
					  base_crtc);

	xilinx_drm_mixer_set_intrpts(mixer, true);
}

/* disable vblank interrupt */
void xvmixer_drm_crtc_disable_vblank(struct drm_crtc *base_crtc)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct xilinx_drm_mixer *mixer = &crtc->mixer;

	if (!mixer->mixer_hw.intrpts_enabled)
		return;

	xilinx_drm_mixer_set_intrpts(mixer, false);
}

/**
 * xvmixer_drm_crtc_restore - Restore the crtc states
 * @base_crtc: base crtc object
 *
 * Restore the crtc states to the default ones. The request is propagated
 * to the plane driver.
 */
void xvmixer_drm_crtc_restore(struct drm_crtc *base_crtc)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);

	xvmixer_drm_plane_restore(&crtc->mixer);
}

/* check max width */
unsigned int xvmixer_drm_crtc_get_max_width(struct drm_crtc *base_crtc)
{
	return xvmixer_drm_plane_get_max_width(base_crtc->primary);
}

/* check max height */
unsigned int xvmixer_drm_crtc_get_max_height(struct drm_crtc *base_crtc)
{
	return xvmixer_drm_plane_get_max_height(base_crtc->primary);
}

/* check max cursor width */
unsigned int xvmixer_drm_crtc_get_max_cursor_width(struct drm_crtc *base_crtc)
{
	return xvmixer_drm_plane_get_max_cursor_width(base_crtc->primary);
}

/* check max cursor height */
unsigned int xvmixer_drm_crtc_get_max_cursor_height(struct drm_crtc *base_crtc)
{
	return xvmixer_drm_plane_get_max_cursor_height(base_crtc->primary);
}

/* check format */
bool xvmixer_drm_crtc_check_format(struct drm_crtc *base_crtc, uint32_t fourcc)
{
	struct xilinx_mixer_crtc *crtc = to_xilinx_crtc(base_crtc);

	return xvmixer_drm_plane_check_format(&crtc->mixer, fourcc);
}

/* get format */
uint32_t xvmixer_drm_crtc_get_format(struct drm_crtc *base_crtc)
{
	return xvmixer_drm_plane_get_format(base_crtc->primary);
}

/**
 * xvmixer_drm_crtc_get_align - Get the alignment value for pitch
 * @base_crtc: Base crtc object
 *
 * Get the alignment value for pitch from the plane
 *
 * Return: The alignment value if successful, or the error code.
 */
unsigned int xvmixer_drm_crtc_get_align(struct drm_crtc *base_crtc)
{
	return xvmixer_drm_plane_get_align(base_crtc->primary);
}

static struct drm_crtc_funcs xilinx_drm_crtc_funcs = {
	.destroy	= xvmixer_drm_crtc_destroy,
	.set_config	= xilinx_drm_crtc_helper_set_config,
	.page_flip	= xilinx_drm_crtc_page_flip,
};

/* create crtc */
struct drm_crtc *xvmixer_drm_crtc_create(struct drm_device *drm)
{
	struct xilinx_mixer_crtc *crtc;
	struct xilinx_drm_mixer *mixer;
	struct drm_plane *primary_plane = NULL;
	struct drm_plane *cursor_plane = NULL;
	int ret, i;

	crtc = devm_kzalloc(drm->dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return ERR_PTR(-ENOMEM);

	crtc->drm = drm;

	ret = xilinx_drm_mixer_probe(drm->dev, crtc);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			DRM_ERROR("failed to probe mixer\n");
		return ERR_PTR(ret);
	}

	mixer = &crtc->mixer;
	primary_plane = &mixer->drm_primary_layer->base;
	cursor_plane = &mixer->hw_logo_layer->base;

	for (i = 0; i < mixer->num_planes; i++)
		xilinx_drm_mixer_attach_plane_prop(&mixer->planes[i]);

	crtc->pixel_clock = devm_clk_get(drm->dev, NULL);
	if (IS_ERR(crtc->pixel_clock)) {
		if (PTR_ERR(crtc->pixel_clock) == -EPROBE_DEFER) {
			ret = PTR_ERR(crtc->pixel_clock);
			goto err_plane;
		} else {
			DRM_DEBUG_KMS("failed to get pixel clock\n");
			crtc->pixel_clock = NULL;
		}
	}

	ret = clk_prepare_enable(crtc->pixel_clock);
	if (ret) {
		DRM_ERROR("failed to enable a pixel clock\n");
		crtc->pixel_clock_enabled = false;
		goto err_plane;
	}
	crtc->pixel_clock_enabled = true;

	crtc->dpms = DRM_MODE_DPMS_OFF;

	/* initialize drm crtc */
	ret = drm_crtc_init_with_planes(drm, &crtc->base,
					&mixer->drm_primary_layer->base,
					&mixer->hw_logo_layer->base,
					&xilinx_drm_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize crtc\n");
		goto err_pixel_clk;
	}
	drm_crtc_helper_add(&crtc->base, &xilinx_drm_crtc_helper_funcs);

	return &crtc->base;

err_pixel_clk:
	if (crtc->pixel_clock_enabled) {
		clk_disable_unprepare(crtc->pixel_clock);
		crtc->pixel_clock_enabled = false;
	}
err_plane:
	return ERR_PTR(ret);
}

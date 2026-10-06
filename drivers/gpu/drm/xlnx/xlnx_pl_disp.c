// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DRM CRTC DMA engine driver
 *
 * Copyright (C) 2017 - 2018 Xilinx, Inc.
 *
 * Author : Saurabh Sengar <saurabhs@xilinx.com>
 *        : Hyun Woo Kwon <hyun.kwon@xilinx.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_vblank.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <video/videomode.h>
#include "xlnx_bridge.h"
#include "xlnx_crtc.h"
#include "xlnx_drv.h"

#define XLNX_PL_DISP_MAX_NUM_PLANES	3

/*
 * Overview
 * --------
 *
 * This driver intends to support the display pipeline with DMA engine
 * driver by initializing DRM crtc and plane objects. The driver makes
 * an assumption that it's single plane pipeline, as multi-plane pipeline
 * would require programing beyond the DMA engine interface.
 */

/**
 * struct xlnx_dma_chan - struct for DMA engine
 * @dma_chan: DMA channel
 * @xt: Interleaved desc config container
 * @sgl: Data chunk for dma_interleaved_template
 */
struct xlnx_dma_chan {
	struct dma_chan *dma_chan;
	struct dma_interleaved_template xt;
	struct data_chunk sgl[1];
};

/**
 * struct xlnx_pl_disp - struct for display subsystem
 * @dev: device structure
 * @master: logical master device from xlnx drm
 * @xlnx_crtc: Xilinx DRM driver crtc object
 * @plane: base drm plane object
 * @chan: struct for DMA engine
 * @event: vblank pending event
 * @callback: callback for registering DMA callback function
 * @callback_param: parameter for passing  to DMA callback function
 * @drm: core drm object
 * @fmt: drm color format
 * @ppc: pixels per clock produced by the source video pipeline
 * @output_bus_format: media bus format presented on the CRTC output
 * @vid_clk: PL video (pixel) clock, or NULL if managed elsewhere
 * @vid_clk_enabled: this instance holds a prepare/enable reference on @vid_clk
 * @vtc_bridge: vtc_bridge structure
 * @fid: field id
 * @fid_err_prop: field id error property
 * @fid_err_val: field id error value
 * @fid_out_prop: field id out property
 * @fid_out_val: field out value
 */
struct xlnx_pl_disp {
	struct device *dev;
	struct platform_device *master;
	struct xlnx_crtc xlnx_crtc;
	struct drm_plane plane;
	struct xlnx_dma_chan *chan;
	struct drm_pending_vblank_event *event;
	dma_async_tx_callback callback;
	void *callback_param;
	struct drm_device *drm;
	u32 fmt;
	u32 ppc;
	u32 output_bus_format;
	struct clk *vid_clk;
	bool vid_clk_enabled;
	struct xlnx_bridge *vtc_bridge;
	u32 fid;
	struct drm_property *fid_err_prop;
	u32 fid_err_val;
	struct drm_property *fid_out_prop;
	u32 fid_out_val;
};

/*
 * Xlnx crtc functions
 */
static inline struct xlnx_pl_disp *crtc_to_dma(struct xlnx_crtc *xlnx_crtc)
{
	return container_of(xlnx_crtc, struct xlnx_pl_disp, xlnx_crtc);
}

/**
 * xlnx_pl_disp_complete - vblank handler
 * @param: parameter to vblank handler
 *
 * The frame buffer read DMA raises a start-of-frame interrupt every frame,
 * so drive vblank directly from the DMA completion callback and send the
 * pending event to the CRTC object.
 */
static void xlnx_pl_disp_complete(void *param)
{
	struct xlnx_pl_disp *xlnx_pl_disp = param;
	struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan;
	int ret;

	drm_crtc_handle_vblank(&xlnx_pl_disp->xlnx_crtc.crtc);

	/* Get fid err flag and fid out val */
	ret = xilinx_xdma_get_fid_err_flag(xlnx_dma_chan->dma_chan,
					   &xlnx_pl_disp->fid_err_val);
	if (ret)
		dev_dbg(xlnx_pl_disp->dev, "failed to get fid_err info\n");

	ret = xilinx_xdma_get_fid_out(xlnx_dma_chan->dma_chan,
				      &xlnx_pl_disp->fid_out_val);
	if (ret)
		dev_dbg(xlnx_pl_disp->dev, "failed to get fid_out info\n");
}

/**
 * xlnx_pl_disp_get_format - Get the current display pipeline format
 * @xlnx_crtc: xlnx crtc object
 *
 * Get the current format of pipeline
 *
 * Return: the corresponding DRM_FORMAT_XXX
 */
static uint32_t xlnx_pl_disp_get_format(struct xlnx_crtc *xlnx_crtc)
{
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);

	return xlnx_pl_disp->fmt;
}

/**
 * xlnx_pl_disp_get_align - Get the alignment value for pitch
 * @xlnx_crtc: xlnx crtc object
 *
 * Get the alignment value for pitch from the plane
 *
 * Return: The alignment value if successful, or the error code.
 */
static unsigned int xlnx_pl_disp_get_align(struct xlnx_crtc *xlnx_crtc)
{
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);

	return 1 << xlnx_pl_disp->chan->dma_chan->device->copy_align;
}

/*
 * DRM plane functions
 */
static inline struct xlnx_pl_disp *plane_to_dma(struct drm_plane *plane)
{
	return container_of(plane, struct xlnx_pl_disp, plane);
}

/**
 * xlnx_pl_disp_plane_disable - Disables DRM plane
 * @plane: DRM plane object
 *
 * Disable the DRM plane, by stopping the corrosponding DMA
 */
static void xlnx_pl_disp_plane_disable(struct drm_plane *plane)
{
	struct xlnx_pl_disp *xlnx_pl_disp = plane_to_dma(plane);
	struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan;

	dmaengine_terminate_sync(xlnx_dma_chan->dma_chan);
}

/**
 * xlnx_pl_disp_plane_enable - Enables DRM plane
 * @plane: DRM plane object
 *
 * Enable the DRM plane, by enabling the corresponding DMA
 */
static void xlnx_pl_disp_plane_enable(struct drm_plane *plane)
{
	struct xlnx_pl_disp *xlnx_pl_disp = plane_to_dma(plane);
	struct dma_async_tx_descriptor *desc;
	unsigned long flags;
	struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan;
	struct dma_chan *dma_chan = xlnx_dma_chan->dma_chan;
	struct dma_interleaved_template *xt = &xlnx_dma_chan->xt;

	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	desc = dmaengine_prep_interleaved_dma(dma_chan, xt, flags);
	if (!desc) {
		dev_err(xlnx_pl_disp->dev,
			"failed to prepare DMA descriptor\n");
		return;
	}
	desc->callback = xlnx_pl_disp->callback;
	desc->callback_param = xlnx_pl_disp->callback_param;
	xilinx_xdma_set_earlycb(xlnx_dma_chan->dma_chan, desc, EARLY_CALLBACK);

	if (plane->state->crtc->state->adjusted_mode.flags &
			DRM_MODE_FLAG_INTERLACE) {
		/*
		 * Framebuffer DMA Reader sends the first field twice, which
		 * causes the following fields out of order. The fid is
		 * reverted to restore the order
		 */
		if (plane->state->fb->flags == DRM_MODE_FB_ALTERNATE_TOP) {
			xlnx_pl_disp->fid = 0;
		} else if (plane->state->fb->flags ==
				DRM_MODE_FB_ALTERNATE_BOTTOM) {
			xlnx_pl_disp->fid = 1;
		} else {
			/*
			 * FIXME: for interlace mode, application may send
			 * dummy packets before the video field, need to set
			 * the fid correctly to avoid display distortion
			 */
			xlnx_pl_disp->fid = 0;
		}

		xilinx_xdma_set_fid(xlnx_dma_chan->dma_chan, desc,
				    xlnx_pl_disp->fid);
	}

	dmaengine_submit(desc);
	dma_async_issue_pending(xlnx_dma_chan->dma_chan);
}

static void xlnx_pl_disp_plane_atomic_disable(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	xlnx_pl_disp_plane_disable(plane);
}

static int xlnx_pl_disp_plane_mode_set(struct drm_plane *plane,
				       struct drm_framebuffer *fb,
				       int crtc_x, int crtc_y,
				       unsigned int crtc_w, unsigned int crtc_h,
				       u32 src_x, uint32_t src_y,
				       u32 src_w, uint32_t src_h)
{
	struct xlnx_pl_disp *xlnx_pl_disp = plane_to_dma(plane);
	const struct drm_format_info *info = fb->format;
	dma_addr_t luma_paddr, chroma_paddr;
	size_t stride;
	struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan;

	if (info->num_planes > XLNX_PL_DISP_MAX_NUM_PLANES) {
		dev_err(xlnx_pl_disp->dev, "Color format not supported\n");
		return -EINVAL;
	}
	luma_paddr = drm_fb_dma_get_gem_addr(fb, plane->state, 0);
	if (!luma_paddr) {
		dev_err(xlnx_pl_disp->dev, "failed to get luma paddr\n");
		return -EINVAL;
	}

	dev_dbg(xlnx_pl_disp->dev, "num planes = %d\n", info->num_planes);
	xlnx_dma_chan->xt.numf = src_h;
	xlnx_dma_chan->sgl[0].size = drm_format_plane_width_bytes(info,
								  0, src_w);
	xlnx_dma_chan->sgl[0].icg = fb->pitches[0] - xlnx_dma_chan->sgl[0].size;
	xlnx_dma_chan->xt.src_start = luma_paddr;
	xlnx_dma_chan->xt.frame_size = info->num_planes;
	xlnx_dma_chan->xt.dir = DMA_MEM_TO_DEV;
	xlnx_dma_chan->xt.src_sgl = true;
	xlnx_dma_chan->xt.dst_sgl = false;

	/* Do we have a video format aware dma channel?
	 * so, modify descriptor accordingly. Hueristic test:
	 * we have a multi-plane format but only one dma channel
	 */
	if (info->num_planes > 1) {
		chroma_paddr = drm_fb_dma_get_gem_addr(fb, plane->state, 1);
		if (!chroma_paddr) {
			dev_err(xlnx_pl_disp->dev,
				"failed to get chroma paddr\n");
			return -EINVAL;
		}
		stride = xlnx_dma_chan->sgl[0].size +
			xlnx_dma_chan->sgl[0].icg;
		xlnx_dma_chan->sgl[0].src_icg = chroma_paddr -
			xlnx_dma_chan->xt.src_start -
			(xlnx_dma_chan->xt.numf * stride);
	}

	return 0;
}

static void xlnx_pl_disp_plane_atomic_update(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	int ret;
	struct xlnx_pl_disp *xlnx_pl_disp = plane_to_dma(plane);

	ret = xlnx_pl_disp_plane_mode_set(plane,
					  plane->state->fb,
					  plane->state->crtc_x,
					  plane->state->crtc_y,
					  plane->state->crtc_w,
					  plane->state->crtc_h,
					  plane->state->src_x >> 16,
					  plane->state->src_y >> 16,
					  plane->state->src_w >> 16,
					  plane->state->src_h >> 16);
	if (ret) {
		dev_err(xlnx_pl_disp->dev, "failed to mode set a plane\n");
		return;
	}
	/* in case frame buffer is used set the color format */
	xilinx_xdma_drm_config(xlnx_pl_disp->chan->dma_chan,
			       xlnx_pl_disp->plane.state->fb->format->format);
	/* apply the new fb addr and enable */
	xlnx_pl_disp_plane_enable(plane);
}

static int
xlnx_pl_disp_plane_atomic_check(struct drm_plane *plane,
				struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	const struct drm_plane_state *old_plane_state =
		drm_atomic_get_old_plane_state(state, plane);
	struct drm_crtc *crtc = new_plane_state->crtc ?: old_plane_state->crtc;
	struct drm_crtc_state *new_crtc_state;

	if (!crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	/* plane must be enabled when state is active */
	if (new_crtc_state->active && !new_plane_state->crtc)
		return -EINVAL;

	/*
	 * This check is required to call modeset if there is a change in color
	 * format
	 */
	if (new_plane_state->fb && old_plane_state->fb &&
	    new_plane_state->fb->format->format !=
	    old_plane_state->fb->format->format)
		new_crtc_state->mode_changed = true;

	return 0;
}

static int
xlnx_pl_disp_plane_atomic_get_property(struct drm_plane *plane,
				       const struct drm_plane_state *state,
				       struct drm_property *property,
				       uint64_t *val)
{
	struct xlnx_pl_disp *xlnx_pl_disp = plane_to_dma(plane);

	if (property == xlnx_pl_disp->fid_err_prop)
		*val = xlnx_pl_disp->fid_err_val;
	else if (property == xlnx_pl_disp->fid_out_prop)
		*val = xlnx_pl_disp->fid_out_val;
	else
		return -EINVAL;

	return 0;
}

static const struct drm_plane_helper_funcs xlnx_pl_disp_plane_helper_funcs = {
	.atomic_update = xlnx_pl_disp_plane_atomic_update,
	.atomic_disable = xlnx_pl_disp_plane_atomic_disable,
	.atomic_check = xlnx_pl_disp_plane_atomic_check,
};

static struct drm_plane_funcs xlnx_pl_disp_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.atomic_get_property = xlnx_pl_disp_plane_atomic_get_property,
};

static void xlnx_pl_disp_crtc_atomic_begin(struct drm_crtc *crtc,
					   struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		/* Consume the flip_done event from atomic helper */
		crtc->state->event->pipe = drm_crtc_index(crtc);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		drm_crtc_arm_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void xlnx_pl_disp_clear_event(struct drm_crtc *crtc)
{
	if (crtc->state->event) {
		complete_all(crtc->state->event->base.completion);
		crtc->state->event = NULL;
	}
}

static void xlnx_pl_disp_crtc_atomic_enable(struct drm_crtc *crtc,
					    struct drm_atomic_state *state)
{
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	int vrefresh;
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);
	struct videomode vm;
	unsigned long pixel_clk;
	int ret;

	/*
	 * Bring up the PL video (pixel) clock for this pipeline. The source
	 * emits @ppc pixels per clock, and the clock wizard output is a /2
	 * ("2x") clock, so the required rate matches AVPG's handling: for
	 * ppc > 1 divide the pixel rate by ppc/2, otherwise double it. The
	 * clock is optional; clk_* on NULL is a no-op for boards that manage
	 * the video clock elsewhere.
	 */
	pixel_clk = adjusted_mode->clock * 1000;
	ret = clk_prepare_enable(xlnx_pl_disp->vid_clk);
	if (ret < 0) {
		dev_err(xlnx_pl_disp->dev,
			"failed to enable video clock: %d\n", ret);
		return;
	}
	xlnx_pl_disp->vid_clk_enabled = true;
	if (xlnx_pl_disp->ppc > 1)
		pixel_clk /= xlnx_pl_disp->ppc >> 1;
	else
		pixel_clk *= 2;
	ret = clk_set_rate(xlnx_pl_disp->vid_clk, pixel_clk);
	if (ret < 0) {
		dev_err(xlnx_pl_disp->dev,
			"failed to set video clock rate of %lu: %d\n",
			pixel_clk, ret);
		clk_disable_unprepare(xlnx_pl_disp->vid_clk);
		xlnx_pl_disp->vid_clk_enabled = false;
		return;
	}

	if (xlnx_pl_disp->vtc_bridge) {
		/* set video timing */
		drm_display_mode_to_videomode(adjusted_mode, &vm);
		/*
		 * When the source pipeline emits more than one pixel per
		 * clock the horizontal timing domain is expressed in clock
		 * beats (pixels / ppc), so scale the VTC timing accordingly.
		 * For ppc == 1 this is a no-op.
		 */
		if (xlnx_pl_disp->ppc > 1) {
			vm.pixelclock /= xlnx_pl_disp->ppc;
			vm.hactive /= xlnx_pl_disp->ppc;
			vm.hfront_porch /= xlnx_pl_disp->ppc;
			vm.hback_porch /= xlnx_pl_disp->ppc;
			vm.hsync_len /= xlnx_pl_disp->ppc;
		}
		xlnx_bridge_set_timing(xlnx_pl_disp->vtc_bridge, &vm);
		xlnx_bridge_enable(xlnx_pl_disp->vtc_bridge);
	}

	/* Delay of 1 vblank interval for timing gen to be stable */
	vrefresh = (adjusted_mode->clock * 1000) /
		   (adjusted_mode->vtotal * adjusted_mode->htotal);
	msleep(1 * 1000 / vrefresh);
}

static void xlnx_pl_disp_crtc_atomic_disable(struct drm_crtc *crtc,
					     struct drm_atomic_state *state)
{
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);

	xlnx_pl_disp_clear_event(crtc);
	drm_crtc_vblank_off(crtc);
	if (xlnx_pl_disp->vtc_bridge)
		xlnx_bridge_disable(xlnx_pl_disp->vtc_bridge);
	if (xlnx_pl_disp->vid_clk_enabled) {
		clk_disable_unprepare(xlnx_pl_disp->vid_clk);
		xlnx_pl_disp->vid_clk_enabled = false;
	}
}

static int xlnx_pl_disp_crtc_atomic_check(struct drm_crtc *crtc,
					  struct drm_atomic_state *state)
{
	return drm_atomic_add_affected_planes(state, crtc);
}

/**
 * xlnx_pl_disp_crtc_select_output_bus_format - Select the CRTC output bus format
 * @crtc: The CRTC to query
 * @crtc_state: The new CRTC state
 * @in_bus_fmts: List of media bus formats the encoder can accept
 * @num_in_bus_fmts: Number of entries in @in_bus_fmts
 *
 * The downstream encoder (e.g. the DP MST controller) uses this to negotiate
 * the pixels-per-clock the CRTC feeds it. Without this callback the DRM helper
 * falls back to the first entry in @in_bus_fmts, which is the single-pixel
 * format, and a multi-pixel source pipeline then starves the encoder. Return
 * the format matching the source pipeline's pixels-per-clock instead.
 *
 * Return: the selected media bus format, or 0 if none is supported.
 */
static u32
xlnx_pl_disp_crtc_select_output_bus_format(struct drm_crtc *crtc,
					   struct drm_crtc_state *crtc_state,
					   const u32 *in_bus_fmts,
					   unsigned int num_in_bus_fmts)
{
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);
	unsigned int i;

	for (i = 0; i < num_in_bus_fmts; ++i) {
		if (in_bus_fmts[i] == xlnx_pl_disp->output_bus_format)
			return xlnx_pl_disp->output_bus_format;
	}

	return 0;
}

static struct drm_crtc_helper_funcs xlnx_pl_disp_crtc_helper_funcs = {
	.atomic_enable = xlnx_pl_disp_crtc_atomic_enable,
	.atomic_disable = xlnx_pl_disp_crtc_atomic_disable,
	.atomic_check = xlnx_pl_disp_crtc_atomic_check,
	.atomic_begin = xlnx_pl_disp_crtc_atomic_begin,
	.select_output_bus_format = xlnx_pl_disp_crtc_select_output_bus_format,
};

static void xlnx_pl_disp_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
}

static int xlnx_pl_disp_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);

	/* Drive vblank from the frame buffer read DMA completion callback. */
	xlnx_pl_disp->callback = xlnx_pl_disp_complete;
	xlnx_pl_disp->callback_param = xlnx_pl_disp;

	return 0;
}

static void xlnx_pl_disp_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);

	xlnx_pl_disp->callback = NULL;
	xlnx_pl_disp->callback_param = NULL;
}

static struct drm_crtc_funcs xlnx_pl_disp_crtc_funcs = {
	.destroy = xlnx_pl_disp_crtc_destroy,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = xlnx_pl_disp_crtc_enable_vblank,
	.disable_vblank = xlnx_pl_disp_crtc_disable_vblank,
};

/**
 * xlnx_pl_disp_ppc_to_bus_format - Map pixels-per-clock to an RGB888 bus format
 * @ppc: pixels per clock (1, 2 or 4)
 *
 * The source pipeline emits 8bpc RGB (matching the XR24 plane format) at @ppc
 * pixels per clock. Map that to the corresponding DP controller input media
 * bus format so the encoder is programmed for the same pixels-per-clock.
 *
 * Return: the matching media bus format, or 0 if @ppc is unsupported.
 */
static u32 xlnx_pl_disp_ppc_to_bus_format(u32 ppc)
{
	switch (ppc) {
	case 1:
		return MEDIA_BUS_FMT_RGB888_1X24;
	case 2:
		return MEDIA_BUS_FMT_RGB888_0_5X48;
	case 4:
		return MEDIA_BUS_FMT_RGB888_0_25X96;
	default:
		return 0;
	}
}

static int xlnx_pl_disp_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct drm_device *drm = data;
	struct xlnx_pl_disp *xlnx_pl_disp = dev_get_drvdata(dev);
	struct drm_mode_object *obj = &xlnx_pl_disp->plane.base;
	struct device_node *fb_node;
	int ret;
	u32 *fmts = NULL;
	unsigned int num_fmts = 0;

	/* in case of fb IP query the supported formats and there count */
	xilinx_xdma_get_drm_vid_fmts(xlnx_pl_disp->chan->dma_chan,
				     &num_fmts, &fmts);
	ret = drm_universal_plane_init(drm, &xlnx_pl_disp->plane, 0,
				       &xlnx_pl_disp_plane_funcs,
				       fmts ? fmts : &xlnx_pl_disp->fmt,
				       num_fmts ? num_fmts : 1,
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_helper_add(&xlnx_pl_disp->plane,
			     &xlnx_pl_disp_plane_helper_funcs);

	ret = drm_crtc_init_with_planes(drm, &xlnx_pl_disp->xlnx_crtc.crtc,
					&xlnx_pl_disp->plane, NULL,
					&xlnx_pl_disp_crtc_funcs, NULL);
	if (ret) {
		drm_plane_cleanup(&xlnx_pl_disp->plane);
		return ret;
	}

	drm_crtc_helper_add(&xlnx_pl_disp->xlnx_crtc.crtc,
			    &xlnx_pl_disp_crtc_helper_funcs);
	xlnx_pl_disp->xlnx_crtc.get_format = &xlnx_pl_disp_get_format;
	xlnx_pl_disp->xlnx_crtc.get_align = &xlnx_pl_disp_get_align;
	xlnx_pl_disp->drm = drm;

	/*
	 * Determine the pixels-per-clock the source video pipeline emits by
	 * querying the DMA (frame buffer) IP node. This drives both the VTC
	 * timing scaling and the CRTC output bus format so the downstream
	 * encoder is programmed for the same pixels-per-clock.
	 */
	xlnx_pl_disp->ppc = 1;
	fb_node = xlnx_pl_disp->chan->dma_chan->device->dev->of_node;
	if (fb_node)
		of_property_read_u32(fb_node, "xlnx,pixels-per-clock",
				     &xlnx_pl_disp->ppc);
	xlnx_pl_disp->output_bus_format =
		xlnx_pl_disp_ppc_to_bus_format(xlnx_pl_disp->ppc);
	if (!xlnx_pl_disp->output_bus_format)
		dev_warn(dev, "unsupported ppc %u, output bus format unset\n",
			 xlnx_pl_disp->ppc);

	xlnx_pl_disp->fid_err_prop = drm_property_create_bool(xlnx_pl_disp->drm,
							      0, "fid_err");
	xlnx_pl_disp->fid_out_prop = drm_property_create_bool(xlnx_pl_disp->drm,
							      0, "fid_out");
	drm_object_attach_property(obj, xlnx_pl_disp->fid_err_prop, 0);
	drm_object_attach_property(obj, xlnx_pl_disp->fid_out_prop, 0);

	ret = xlnx_crtc_create_stream_property(&xlnx_pl_disp->xlnx_crtc.crtc,
					       dev->of_node);
	if (ret) {
		dev_err(dev, "failed to create stream property: %d\n", ret);
		goto err_properties;
	}

	xlnx_crtc_register(xlnx_pl_disp->drm, &xlnx_pl_disp->xlnx_crtc);

	return 0;

err_properties:
	drm_property_destroy(xlnx_pl_disp->drm, xlnx_pl_disp->fid_out_prop);
	drm_property_destroy(xlnx_pl_disp->drm, xlnx_pl_disp->fid_err_prop);
	drm_crtc_cleanup(&xlnx_pl_disp->xlnx_crtc.crtc);
	drm_plane_cleanup(&xlnx_pl_disp->plane);
	return ret;
}

static void xlnx_pl_disp_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct xlnx_pl_disp *xlnx_pl_disp = dev_get_drvdata(dev);

	drm_property_destroy(xlnx_pl_disp->drm, xlnx_pl_disp->fid_out_prop);
	drm_property_destroy(xlnx_pl_disp->drm, xlnx_pl_disp->fid_err_prop);
	xlnx_crtc_unregister(xlnx_pl_disp->drm, &xlnx_pl_disp->xlnx_crtc);
	drm_plane_cleanup(&xlnx_pl_disp->plane);
	drm_crtc_cleanup(&xlnx_pl_disp->xlnx_crtc.crtc);
}

static const struct component_ops xlnx_pl_disp_component_ops = {
	.bind	= xlnx_pl_disp_bind,
	.unbind	= xlnx_pl_disp_unbind,
};

static int xlnx_pl_disp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *vtc_node;
	struct xlnx_pl_disp *xlnx_pl_disp;
	int ret;
	const char *vformat;
	struct dma_chan *dma_chan;
	struct xlnx_dma_chan *xlnx_dma_chan;
	const struct drm_format_info *info;

	xlnx_pl_disp = devm_kzalloc(dev, sizeof(*xlnx_pl_disp), GFP_KERNEL);
	if (!xlnx_pl_disp)
		return -ENOMEM;

	dma_chan = of_dma_request_slave_channel(dev->of_node, "dma0");
	if (IS_ERR_OR_NULL(dma_chan)) {
		dev_err(dev, "failed to request dma channel\n");
		return PTR_ERR(dma_chan);
	}

	xlnx_dma_chan = devm_kzalloc(dev, sizeof(*xlnx_dma_chan), GFP_KERNEL);
	if (!xlnx_dma_chan)
		return -ENOMEM;

	xlnx_dma_chan->dma_chan = dma_chan;
	xlnx_pl_disp->chan = xlnx_dma_chan;
	ret = of_property_read_string(dev->of_node, "xlnx,vformat", &vformat);
	if (ret) {
		dev_err(dev, "No xlnx,vformat value in dts\n");
		goto err_dma;
	}

	xlnx_pl_disp->fmt = fourcc_code(vformat[0], vformat[1],
					vformat[2], vformat[3]);
	info = drm_format_info(xlnx_pl_disp->fmt);
	if (!info) {
		dev_err(dev, "Invalid video format in dts\n");
		ret = -EINVAL;
		goto err_dma;
	}

	/*
	 * Optional PL video (pixel) clock. When present the CRTC enables it and
	 * sets its rate per mode; when absent (NULL) the clock is assumed to be
	 * managed elsewhere and the clk_* calls become no-ops.
	 */
	xlnx_pl_disp->vid_clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(xlnx_pl_disp->vid_clk)) {
		ret = PTR_ERR(xlnx_pl_disp->vid_clk);
		dev_err(dev, "failed to get video clock: %d\n", ret);
		goto err_dma;
	}

	/* VTC Bridge support */
	vtc_node = of_parse_phandle(dev->of_node, "xlnx,bridge", 0);
	if (vtc_node) {
		xlnx_pl_disp->vtc_bridge = of_xlnx_bridge_get(vtc_node);
		if (!xlnx_pl_disp->vtc_bridge) {
			dev_info(dev, "Didn't get vtc bridge instance\n");
			ret = -EPROBE_DEFER;
			goto err_dma;
		}
	} else {
		dev_info(dev, "vtc bridge property not present\n");
	}

	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret)
		dev_dbg(&pdev->dev, "of_reserved_mem_device_init: %d\n", ret);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_mask_and_coherent: %d\n", ret);
		goto err_dma;
	}

	xlnx_pl_disp->dev = dev;
	platform_set_drvdata(pdev, xlnx_pl_disp);

	ret = component_add(dev, &xlnx_pl_disp_component_ops);
	if (ret)
		goto err_dma;

	xlnx_pl_disp->master = xlnx_drm_pipeline_init(pdev);
	if (IS_ERR(xlnx_pl_disp->master)) {
		ret = PTR_ERR(xlnx_pl_disp->master);
		xlnx_pl_disp->master = NULL;
		dev_err(dev, "failed to initialize the drm pipeline\n");
		goto err_component;
	}

	dev_info(&pdev->dev, "Xlnx PL display driver probed\n");

	return 0;

err_component:
	component_del(dev, &xlnx_pl_disp_component_ops);
err_dma:
	of_reserved_mem_device_release(&pdev->dev);
	dma_release_channel(xlnx_pl_disp->chan->dma_chan);

	return ret;
}

static void xlnx_pl_disp_remove(struct platform_device *pdev)
{
	struct xlnx_pl_disp *xlnx_pl_disp = platform_get_drvdata(pdev);
	struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan;

	if (xlnx_pl_disp->vtc_bridge)
		of_xlnx_bridge_put(xlnx_pl_disp->vtc_bridge);
	if (xlnx_pl_disp->master)
		xlnx_drm_pipeline_exit(xlnx_pl_disp->master);
	component_del(&pdev->dev, &xlnx_pl_disp_component_ops);

	/* Make sure the channel is terminated before release */
	dmaengine_terminate_sync(xlnx_dma_chan->dma_chan);
	dma_release_channel(xlnx_dma_chan->dma_chan);
	of_reserved_mem_device_release(&pdev->dev);
}

static const struct of_device_id xlnx_pl_disp_of_match[] = {
	{ .compatible = "xlnx,pl-disp"},
	{ }
};
MODULE_DEVICE_TABLE(of, xlnx_pl_disp_of_match);

static struct platform_driver xlnx_pl_disp_driver = {
	.probe = xlnx_pl_disp_probe,
	.remove = xlnx_pl_disp_remove,
	.driver = {
		.name = "xlnx-pl-disp",
		.of_match_table = xlnx_pl_disp_of_match,
	},
};

module_platform_driver(xlnx_pl_disp_driver);

MODULE_AUTHOR("Saurabh Sengar");
MODULE_DESCRIPTION("Xilinx DRM Display Driver for PL IPs");
MODULE_LICENSE("GPL");

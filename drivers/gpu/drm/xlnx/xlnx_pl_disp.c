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
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <video/videomode.h>
#include "xlnx_bridge.h"
#include "xlnx_crtc.h"
#include "xlnx_drv.h"

#define XLNX_PL_DISP_MAX_NUM_PLANES	3

static bool overclock_2x;
module_param(overclock_2x, bool, 0444);
MODULE_PARM_DESC(overclock_2x,
		 "Double the pixel clock rate for some legacy designs");

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
 * @gpio_en_live: live-input (stream converter) clock-enable GPIO, or NULL
 * @gpio_en_vtc: VTC clock-enable GPIO, or NULL
 * @vtc_bridge: vtc_bridge structure
 * @encoder: DRM encoder driving the downstream bridge chain
 * @connector: DRM connector created from the downstream bridge chain
 * @bridge: downstream display bridge (mmi-dc bypass bridge)
 * @vtc_vid_clk: VTC bridge's video clock
 * @dp_native: native-video VTC path selected by xlnx,dp-native in DT
 * @primary: this instance owns the downstream encoder/bridge/connector
 * @fid: field id
 * @fid_err_prop: field id error property
 * @fid_err_val: field id error value
 * @fid_out_prop: field id out property
 * @fid_out_val: field out value
 * @legacy_mode: legacy (xlnx-drm) mode flag
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
	struct gpio_desc *gpio_en_live;
	struct gpio_desc *gpio_en_vtc;
	struct xlnx_bridge *vtc_bridge;
	struct clk *vtc_vid_clk;
	bool dp_native;
	struct drm_encoder encoder;
	struct drm_connector *connector;
	struct drm_bridge *bridge;
	bool primary;
	u32 fid;
	struct drm_property *fid_err_prop;
	u32 fid_err_val;
	struct drm_property *fid_out_prop;
	u32 fid_out_val;
	bool legacy_mode;
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
	struct xlnx_pl_disp *xlnx_pl_disp = plane_to_dma(plane);

	/* Gate the live-input converter before the stream stops. */
	gpiod_set_value_cansleep(xlnx_pl_disp->gpio_en_live, 0);
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

	/*
	 * Release the live-input (stream converter) clock gate only now that the
	 * frmbuf DMA has been issued and is pushing pixels. The AXI4-Stream to
	 * Video Out converter locks onto an already-live stream; asserting this
	 * earlier (e.g. in the CRTC enable, before the plane update kicks the
	 * DMA) lets the converter start against an empty stream and latch into a
	 * stalled under-run. Mirrors the ordering the AVPG source uses.
	 */
	gpiod_set_value_cansleep(xlnx_pl_disp->gpio_en_live, 1);
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
	unsigned long pixel_clk, cur_rate;
	u32 user_calc_ppc = 1;
	int ret;

	pixel_clk = adjusted_mode->clock * 1000 / xlnx_pl_disp->ppc;
	if (overclock_2x)
		pixel_clk *= 2;
	/*
	 * Several pipelines may share a single clocking-wizard VCO. Calling
	 * clk_set_rate() triggers a dynamic reconfiguration that re-locks the
	 * whole wizard, momentarily dropping its "locked" output and resetting
	 * the timing generators of the other, already-running pipelines. Only
	 * reprogram the rate when the wizard is not already producing it, so a
	 * second display coming up does not blank the first.
	 *
	 * Compare against the current *actual* rate (clk_get_rate()) with a
	 * small tolerance rather than clk_round_rate(): on the Xilinx clocking
	 * wizard the round/determine path and the recalc/readback path round
	 * differently, so clk_round_rate() rarely matches clk_get_rate() to the
	 * Hz even when the hardware is already at the requested rate, which
	 * would defeat the guard and reconfigure the wizard on every modeset.
	 */
	cur_rate = clk_get_rate(xlnx_pl_disp->vid_clk);
	if (abs((long)(cur_rate - pixel_clk)) > pixel_clk / 1000) {
		ret = clk_set_rate(xlnx_pl_disp->vid_clk, pixel_clk);
		if (ret < 0) {
			dev_err(xlnx_pl_disp->dev,
				"failed to set video clock rate of %lu: %d\n",
				pixel_clk, ret);
			return;
		}
	}

	ret = clk_prepare_enable(xlnx_pl_disp->vid_clk);
	if (ret < 0) {
		dev_err(xlnx_pl_disp->dev,
			"failed to enable video clock: %d\n", ret);
		return;
	}
	xlnx_pl_disp->vid_clk_enabled = true;

	if (xlnx_pl_disp->vtc_bridge && !xlnx_pl_disp->dp_native) {
		/* Ungate the VTC clock before programming/enabling it. */
		gpiod_set_value_cansleep(xlnx_pl_disp->gpio_en_vtc, 1);
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
	} else if (xlnx_pl_disp->vtc_bridge && xlnx_pl_disp->dp_native) {
	/* PL DP does not use actual PPC of design. It uses USERPIXELWIDTH
	 * of DPTX as PPC for VTC timing calculations. This custom implementation
	 * is due to minimum frequency constraints in Clock Wizard.
	 *
	 * In Native mode also, DPTX configures video clock based on DPTX
	 * USERPIXELWIDTH. In pl-disp.c, USERPIXELWIDTH register value of DPTX is
	 * indirectly derived from video clock set by DPTX. This derived value is
	 * used as PPC.
	 */
		drm_display_mode_to_videomode(adjusted_mode, &vm);
		if (xlnx_pl_disp->vtc_vid_clk) {
			unsigned long vid_rate =
				clk_get_rate(xlnx_pl_disp->vtc_vid_clk);

			if (vid_rate)
				user_calc_ppc = DIV_ROUND_CLOSEST(vm.pixelclock,
								  vid_rate);
			if (user_calc_ppc < 1)
				user_calc_ppc = 1;
			if (xlnx_pl_disp->ppc &&
			    user_calc_ppc > xlnx_pl_disp->ppc)
				user_calc_ppc = xlnx_pl_disp->ppc;
		}

		dev_info(xlnx_pl_disp->dev,
			 "driving external VTC bridge: user_calc_ppc=%u (max=%u) hactive=%u pixelclock=%lu\n",
			 user_calc_ppc, xlnx_pl_disp->ppc, vm.hactive,
			 vm.pixelclock);
		if (user_calc_ppc > 1) {
			vm.hactive /= user_calc_ppc;
			vm.hfront_porch /= user_calc_ppc;
			vm.hsync_len /= user_calc_ppc;
			vm.hback_porch /= user_calc_ppc;
			vm.pixelclock /= user_calc_ppc;
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
	/* Gate the VTC clock after the VTC has been disabled. */
	gpiod_set_value_cansleep(xlnx_pl_disp->gpio_en_vtc, 0);
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

/**
 * xlnx_pl_disp_remote_port - Resolve the downstream port this CRTC feeds
 * @dev: The pl-disp device
 *
 * The output endpoint 0 of this CRTC connects to a live-video input port of
 * the MMI DC. Parse the remote port number so the pipeline can elect a single
 * CRTC (the one wired to DC port 0) to own the downstream encoder/bridge chain.
 *
 * Return: the remote port number on success or a negative error code.
 */
static int xlnx_pl_disp_remote_port(struct device *dev)
{
	struct device_node *endpoint, *remote;
	struct of_endpoint ep;
	int ret;

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 0, 0);
	if (!endpoint)
		return -ENODEV;

	remote = of_graph_get_remote_endpoint(endpoint);
	of_node_put(endpoint);
	if (!remote)
		return -ENODEV;

	ret = of_graph_parse_endpoint(remote, &ep);
	of_node_put(remote);
	if (ret < 0)
		return ret;

	return ep.port;
}

/**
 * xlnx_pl_disp_find_bridge - Find the downstream display bridge
 * @dev: The pl-disp device
 *
 * Resolve the bridge connected to output endpoint 0 (the MMI DC bypass
 * bridge). Prefer the generic OF graph lookup through devm_drm_of_get_bridge(),
 * which covers the standard case where the remote bridge is registered against
 * its device node. If that lookup defers probe, retry against the exact remote
 * port node, as some Xilinx DRM bridges register themselves against a DT port
 * node instead.
 *
 * Return: A valid DRM bridge pointer on success or an ERR_PTR() on failure.
 */
static struct drm_bridge *xlnx_pl_disp_find_bridge(struct device *dev)
{
	struct device_node *endpoint, *port;
	struct drm_bridge *bridge;

	bridge = devm_drm_of_get_bridge(dev, dev->of_node, 0, 0);
	if (!IS_ERR(bridge) || PTR_ERR(bridge) != -EPROBE_DEFER)
		return bridge;

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 0, 0);
	if (!endpoint)
		return bridge;

	port = of_graph_get_remote_port(endpoint);
	of_node_put(endpoint);
	if (!port)
		return bridge;

	bridge = of_drm_find_bridge(port);
	of_node_put(port);

	return bridge ? bridge : ERR_PTR(-EPROBE_DEFER);
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
	/* Lets a DP TX map this CRTC to the stream its output port is wired to. */
	xlnx_pl_disp->xlnx_crtc.crtc.port = of_graph_get_port_by_id(dev->of_node, 0);
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

	if (xlnx_pl_disp->primary) {
		struct drm_encoder *encoder = &xlnx_pl_disp->encoder;
		struct drm_connector *connector;

		encoder->possible_crtcs =
			drm_crtc_mask(&xlnx_pl_disp->xlnx_crtc.crtc);
		ret = drm_simple_encoder_init(drm, encoder,
					      DRM_MODE_ENCODER_NONE);
		if (ret) {
			dev_err(dev, "failed to initialize encoder: %d\n", ret);
			goto err_properties;
		}

		ret = drm_bridge_attach(encoder, xlnx_pl_disp->bridge, NULL,
					DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (ret) {
			dev_err(dev, "failed to attach bridge: %d\n", ret);
			goto err_encoder;
		}

		connector = drm_bridge_connector_init(drm, encoder);
		if (IS_ERR(connector)) {
			ret = PTR_ERR(connector);
			dev_err(dev, "failed to create connector: %d\n", ret);
			goto err_encoder;
		}

		ret = drm_connector_attach_encoder(connector, encoder);
		if (ret) {
			dev_err(dev, "failed to attach connector: %d\n", ret);
			goto err_encoder;
		}

		xlnx_pl_disp->connector = connector;
	}

	if (xlnx_pl_disp->legacy_mode)
		xlnx_crtc_register(xlnx_pl_disp->drm, &xlnx_pl_disp->xlnx_crtc);

	return 0;

err_encoder:
	drm_encoder_cleanup(&xlnx_pl_disp->encoder);
err_properties:
	drm_property_destroy(xlnx_pl_disp->drm, xlnx_pl_disp->fid_out_prop);
	drm_property_destroy(xlnx_pl_disp->drm, xlnx_pl_disp->fid_err_prop);
	of_node_put(xlnx_pl_disp->xlnx_crtc.crtc.port);
	xlnx_pl_disp->xlnx_crtc.crtc.port = NULL;
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
	if (xlnx_pl_disp->legacy_mode)
		xlnx_crtc_unregister(xlnx_pl_disp->drm, &xlnx_pl_disp->xlnx_crtc);
	if (xlnx_pl_disp->primary)
		drm_encoder_cleanup(&xlnx_pl_disp->encoder);
	of_node_put(xlnx_pl_disp->xlnx_crtc.crtc.port);
	xlnx_pl_disp->xlnx_crtc.crtc.port = NULL;
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
	struct device_node *tx_node;
	struct device_node *vtc_node;
	struct xlnx_pl_disp *xlnx_pl_disp;
	int ret;
	const char *vformat;
	struct dma_chan *dma_chan;
	struct xlnx_dma_chan *xlnx_dma_chan;
	const struct drm_format_info *info;
	u32 num_streams = 1;
	u32 dp_native;

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

	/*
	 * Optional "clk-enable" GPIOs gating the live-input stream converter
	 * (index 0) and the VTC (index 1), mirroring the AVPG source. When
	 * present the CRTC ungates the VTC around VTC enable and the plane
	 * ungates the live-input converter only after the frmbuf DMA is live,
	 * so the converter locks onto an already-running stream. Absent on
	 * boards where the clocks are always enabled.
	 */
	xlnx_pl_disp->gpio_en_live = devm_gpiod_get_index_optional(dev,
								   "clk-enable",
								   0, GPIOD_ASIS);
	if (IS_ERR(xlnx_pl_disp->gpio_en_live)) {
		ret = PTR_ERR(xlnx_pl_disp->gpio_en_live);
		dev_err(dev, "failed to get live-input enable gpio: %d\n", ret);
		goto err_dma;
	}
	ret = gpiod_direction_output(xlnx_pl_disp->gpio_en_live, 0);
	if (xlnx_pl_disp->gpio_en_live && ret < 0) {
		dev_err(dev, "failed to set live-input gpio direction: %d\n",
			ret);
		goto err_dma;
	}

	xlnx_pl_disp->gpio_en_vtc = devm_gpiod_get_index_optional(dev,
								  "clk-enable",
								  1, GPIOD_ASIS);
	if (IS_ERR(xlnx_pl_disp->gpio_en_vtc)) {
		ret = PTR_ERR(xlnx_pl_disp->gpio_en_vtc);
		dev_err(dev, "failed to get vtc enable gpio: %d\n", ret);
		goto err_dma;
	}
	ret = gpiod_direction_output(xlnx_pl_disp->gpio_en_vtc, 0);
	if (xlnx_pl_disp->gpio_en_vtc && ret < 0) {
		dev_err(dev, "failed to set vtc gpio direction: %d\n", ret);
		goto err_dma;
	}

	/* VTC Bridge support */
	dp_native = 0;
	of_property_read_u32(dev->of_node, "xlnx,dp-native", &dp_native);
	xlnx_pl_disp->dp_native = dp_native ? true : false;

	vtc_node = of_parse_phandle(dev->of_node, "xlnx,bridge", 0);
	if (vtc_node) {
		xlnx_pl_disp->vtc_bridge = of_xlnx_bridge_get(vtc_node);
		if (!xlnx_pl_disp->vtc_bridge) {
			dev_info(dev, "Didn't get vtc bridge instance\n");
			ret = -EPROBE_DEFER;
			goto err_dma;
		}

		xlnx_pl_disp->vtc_vid_clk = of_clk_get_by_name(vtc_node, "clk");
		if (IS_ERR(xlnx_pl_disp->vtc_vid_clk)) {
			dev_info(dev, "no vtc video clk; using DT vtc_ppc as-is\n");
			xlnx_pl_disp->vtc_vid_clk = NULL;
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

	/*
	 * Decide the DRM pipeline master from the connected DP TX node's
	 * xlnx,num-streams. For a single stream (SST) this pl-disp node owns
	 * the pipeline; for multi-stream (MST) the DP TX node is the master
	 * because its OF-graph fans out to every per-stream pl-disp CRTC.
	 * num-streams lives only on the TX node, reached over the OF-graph.
	 */
	tx_node = of_graph_get_remote_node(dev->of_node, 0, 0);
	if (tx_node) {
		of_property_read_u32(tx_node, "xlnx,num-streams", &num_streams);
		of_node_put(tx_node);
	}

	/*
	 * When this CRTC is a child of an "xlnx,pl-drm-1.0" aggregate node the
	 * parent frame driver owns the DRM device and drives the component
	 * master. In that case there is nothing to elect here: just expose the
	 * component and let the aggregate master bind the whole pipeline once.
	 *
	 * Set legacy_mode before component_add() as that call may synchronously
	 * bind a waiting legacy master, and bind/unbind must observe the same
	 * mode to stay consistent.
	 */
	xlnx_pl_disp->legacy_mode =
		!of_device_is_compatible(dev->of_node->parent,
					 "xlnx,pl-drm-1.0");

	/*
	 * Downstream bridge election only applies to the xlnx-pl aggregate
	 * topology, where CRTCs feed the DC live-input ports and the CRTC wired
	 * to port 0 owns the encoder/bridge chain (the DC bypass bridge into
	 * the DisplayPort TX). Legacy PL display pipelines (e.g. SDI/HDMI)
	 * register components rather than DRM bridges and may legitimately use
	 * remote port 0, so skip the bridge lookup for them to keep the legacy
	 * component path available.
	 */
	if (!xlnx_pl_disp->legacy_mode) {
		ret = xlnx_pl_disp_remote_port(dev);
		if (ret == 0) {
			struct drm_bridge *bridge =
				xlnx_pl_disp_find_bridge(dev);

			if (IS_ERR(bridge)) {
				ret = dev_err_probe(dev, PTR_ERR(bridge),
						    "failed to find downstream bridge\n");
				goto err_dma;
			}
			xlnx_pl_disp->bridge = bridge;
			xlnx_pl_disp->primary = true;
		} else if (ret < 0) {
			dev_dbg(dev, "failed to resolve remote port: %d\n", ret);
		}
	}

	ret = component_add(dev, &xlnx_pl_disp_component_ops);
	if (ret)
		goto err_dma;

	if (!xlnx_pl_disp->legacy_mode) {
		dev_info(&pdev->dev, "Xlnx PL display driver probed\n");
		return 0;
	}

	if (num_streams == 1) {
		xlnx_pl_disp->master = xlnx_drm_pipeline_init(pdev);
		if (IS_ERR(xlnx_pl_disp->master)) {
			ret = PTR_ERR(xlnx_pl_disp->master);
			dev_err(dev, "failed to initialize the drm pipeline\n");
			goto err_component;
		}
	} else {
		dev_info(dev, "DP TX owns the DRM pipeline (MST)\n");
		xlnx_pl_disp->master = NULL;
	}

	dev_info(&pdev->dev, "Xlnx PL display driver probed\n");

	return 0;

err_component:
	component_del(dev, &xlnx_pl_disp_component_ops);
err_dma:
	if (xlnx_pl_disp->vtc_bridge)
		of_xlnx_bridge_put(xlnx_pl_disp->vtc_bridge);
	of_reserved_mem_device_release(&pdev->dev);
	dma_release_channel(xlnx_pl_disp->chan->dma_chan);

	return ret;
}

static void xlnx_pl_disp_remove(struct platform_device *pdev)
{
	struct xlnx_pl_disp *xlnx_pl_disp = platform_get_drvdata(pdev);
	struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan;

	if (xlnx_pl_disp->vtc_vid_clk)
		clk_put(xlnx_pl_disp->vtc_vid_clk);
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

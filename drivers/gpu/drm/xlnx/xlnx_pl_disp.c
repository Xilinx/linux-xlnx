// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DRM CRTC DMA engine driver
 *
 * Copyright (C) 2017 - 2018 Xilinx, Inc.
 *
 * Author : Saurabh Sengar <saurabhs@xilinx.com>
 *        : Hyun Woo Kwon <hyun.kwon@xilinx.com>
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <video/videomode.h>
#include "xlnx_bridge.h"
#include "xlnx_crtc.h"
#include "xlnx_drv.h"

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
 * @vtc_bridge: vtc_bridge structure
 * @fid: field id
 * @prev_fid: previous field id
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
	struct xlnx_bridge *vtc_bridge;
	u32 fid;
	u32 prev_fid;
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
 * This function handles the vblank interrupt, and sends an event to
 * CRTC object.
 */
static void xlnx_pl_disp_complete(void *param)
{
	struct xlnx_pl_disp *xlnx_pl_disp = param;
	struct drm_device *drm = xlnx_pl_disp->drm;

	drm_handle_vblank(drm, 0);
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
			xlnx_pl_disp->fid = !xlnx_pl_disp->prev_fid;
		}

		if (xlnx_pl_disp->fid == xlnx_pl_disp->prev_fid) {
			xlnx_pl_disp_complete(xlnx_pl_disp);
			return;
		}

		xilinx_xdma_set_fid(xlnx_dma_chan->dma_chan, desc,
				    xlnx_pl_disp->fid);
		xlnx_pl_disp->prev_fid = xlnx_pl_disp->fid;
	}

	dmaengine_submit(desc);
	dma_async_issue_pending(xlnx_dma_chan->dma_chan);
}

static void xlnx_pl_disp_plane_atomic_disable(struct drm_plane *plane,
					      struct drm_plane_state *old_state)
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

	if (info->num_planes > 2) {
		dev_err(xlnx_pl_disp->dev, "Color format not supported\n");
		return -EINVAL;
	}
	luma_paddr = drm_fb_cma_get_gem_addr(fb, plane->state, 0);
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
		chroma_paddr = drm_fb_cma_get_gem_addr(fb, plane->state, 1);
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
					     struct drm_plane_state *old_state)
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
				struct drm_plane_state *new_plane_state)
{
	struct drm_atomic_state *state = new_plane_state->state;
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
};

static inline struct xlnx_pl_disp *drm_crtc_to_dma(struct drm_crtc *crtc)
{
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);

	return crtc_to_dma(xlnx_crtc);
}

static void xlnx_pl_disp_crtc_atomic_begin(struct drm_crtc *crtc,
					   struct drm_crtc_state *old_state)
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
					    struct drm_crtc_state *old_state)
{
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	int vrefresh;
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);
	struct videomode vm;

	if (xlnx_pl_disp->vtc_bridge) {
		/* set video timing */
		drm_display_mode_to_videomode(adjusted_mode, &vm);
		xlnx_bridge_set_timing(xlnx_pl_disp->vtc_bridge, &vm);
		xlnx_bridge_enable(xlnx_pl_disp->vtc_bridge);
	}

	xlnx_pl_disp_plane_enable(crtc->primary);

	/* Delay of 1 vblank interval for timing gen to be stable */
	vrefresh = (adjusted_mode->clock * 1000) /
		   (adjusted_mode->vtotal * adjusted_mode->htotal);
	msleep(1 * 1000 / vrefresh);
}

static void xlnx_pl_disp_crtc_atomic_disable(struct drm_crtc *crtc,
					     struct drm_crtc_state *old_state)
{
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);

	xlnx_pl_disp_plane_disable(crtc->primary);
	xlnx_pl_disp_clear_event(crtc);
	drm_crtc_vblank_off(crtc);
	xlnx_bridge_disable(xlnx_pl_disp->vtc_bridge);

	/* first field is expected to be bottom so init previous field to top */
	xlnx_pl_disp->prev_fid = 1;
}

static int xlnx_pl_disp_crtc_atomic_check(struct drm_crtc *crtc,
					  struct drm_crtc_state *state)
{
	return drm_atomic_add_affected_planes(state->state, crtc);
}

static struct drm_crtc_helper_funcs xlnx_pl_disp_crtc_helper_funcs = {
	.atomic_enable = xlnx_pl_disp_crtc_atomic_enable,
	.atomic_disable = xlnx_pl_disp_crtc_atomic_disable,
	.atomic_check = xlnx_pl_disp_crtc_atomic_check,
	.atomic_begin = xlnx_pl_disp_crtc_atomic_begin,
};

static void xlnx_pl_disp_crtc_destroy(struct drm_crtc *crtc)
{
	xlnx_pl_disp_plane_disable(crtc->primary);
	drm_crtc_cleanup(crtc);
}

static int xlnx_pl_disp_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);
	struct xlnx_pl_disp *xlnx_pl_disp = crtc_to_dma(xlnx_crtc);

	/*
	 * Use the complete callback for vblank event assuming the dma engine
	 * starts on the next descriptor upon this event. This may not be safe
	 * assumption for some dma engines.
	 */
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

static int xlnx_pl_disp_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct drm_device *drm = data;
	struct xlnx_pl_disp *xlnx_pl_disp = dev_get_drvdata(dev);
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
	xlnx_crtc_register(xlnx_pl_disp->drm, &xlnx_pl_disp->xlnx_crtc);

	return 0;
}

static void xlnx_pl_disp_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct xlnx_pl_disp *xlnx_pl_disp = dev_get_drvdata(dev);

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

	strcpy((char *)&xlnx_pl_disp->fmt, vformat);

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

	xlnx_pl_disp->dev = dev;
	platform_set_drvdata(pdev, xlnx_pl_disp);

	ret = component_add(dev, &xlnx_pl_disp_component_ops);
	if (ret)
		goto err_dma;

	xlnx_pl_disp->master = xlnx_drm_pipeline_init(pdev);
	if (IS_ERR(xlnx_pl_disp->master)) {
		ret = PTR_ERR(xlnx_pl_disp->master);
		dev_err(dev, "failed to initialize the drm pipeline\n");
		goto err_component;
	}

	/* first field is expected to be bottom so init previous field to top */
	xlnx_pl_disp->prev_fid = 1;

	dev_info(&pdev->dev, "Xlnx PL display driver probed\n");

	return 0;

err_component:
	component_del(dev, &xlnx_pl_disp_component_ops);
err_dma:
	dma_release_channel(xlnx_pl_disp->chan->dma_chan);

	return ret;
}

static int xlnx_pl_disp_remove(struct platform_device *pdev)
{
	struct xlnx_pl_disp *xlnx_pl_disp = platform_get_drvdata(pdev);
	struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan;

	of_xlnx_bridge_put(xlnx_pl_disp->vtc_bridge);
	xlnx_drm_pipeline_exit(xlnx_pl_disp->master);
	component_del(&pdev->dev, &xlnx_pl_disp_component_ops);

	/* Make sure the channel is terminated before release */
	dmaengine_terminate_sync(xlnx_dma_chan->dma_chan);
	dma_release_channel(xlnx_dma_chan->dma_chan);

	return 0;
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
MODULE_LICENSE("GPL v2");

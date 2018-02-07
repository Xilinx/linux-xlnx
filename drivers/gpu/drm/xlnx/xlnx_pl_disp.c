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
#include "xlnx_crtc.h"

/*
 * Overview
 * --------
 *
 * This driver intends to support the display pipeline with DMA engine
 * driver by initializing DRM crtc and plane objects. The driver makes
 * an assumption that it's single plane pipeline, as multi-plane pipeline
 * would require programing beyond the DMA engine interface. Each plane
 * can have up to XLNX_DMA_MAX_CHAN DMA channels to support multi-planar
 * formats.
 */

#define XLNX_DMA_MAX_CHAN	1

/**
 * struct xlnx_dma_chan - struct for DMA engine
 * @dma_chan: DMA channel
 * @is_active: flag if the DMA is active
 * @xt: Interleaved desc config container
 * @sgl: Data chunk for dma_interleaved_template
 */
struct xlnx_dma_chan {
	struct dma_chan *dma_chan;
	bool is_active;
	struct dma_interleaved_template xt;
	struct data_chunk sgl[1];
};

/**
 * struct xlnx_pl_disp - struct for display subsystem
 * @dev: device structure
 * @xlnx_crtc: Xilinx DRM driver crtc object
 * @plane: base drm plane object
 * @chan: struct for DMA engine
 * @event: vblank pending event
 * @callback: callback for registering DMA callback function
 * @callback_param: parameter for passing  to DMA callback function
 * @drm: core drm object
 * @fmt: drm color format
 */
struct xlnx_pl_disp {
	struct device *dev;
	struct xlnx_crtc xlnx_crtc;
	struct drm_plane plane;
	struct xlnx_dma_chan *chan[XLNX_DMA_MAX_CHAN];
	struct drm_pending_vblank_event *event;
	dma_async_tx_callback callback;
	void *callback_param;
	struct drm_device *drm;
	u32 fmt;
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

	return 1 << xlnx_pl_disp->chan[0]->dma_chan->device->copy_align;
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
	unsigned int i;

	for (i = 0; i < XLNX_DMA_MAX_CHAN; i++) {
		struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan[i];

		if (xlnx_dma_chan->dma_chan)
			dmaengine_terminate_sync(xlnx_dma_chan->dma_chan);
	}
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
	enum dma_ctrl_flags flags;
	unsigned int i;

	for (i = 0; i < XLNX_DMA_MAX_CHAN; i++) {
		struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan[i];
		struct dma_chan *dma_chan = xlnx_dma_chan->dma_chan;
		struct dma_interleaved_template *xt = &xlnx_dma_chan->xt;

		if (xlnx_dma_chan && xlnx_dma_chan->is_active) {
			flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
			desc = dmaengine_prep_interleaved_dma(dma_chan, xt,
							      flags);
			if (!desc) {
				dev_err(xlnx_pl_disp->dev,
					"failed to prepare DMA descriptor\n");
				return;
			}
			desc->callback = xlnx_pl_disp->callback;
			desc->callback_param = xlnx_pl_disp->callback_param;

			dmaengine_submit(desc);
			dma_async_issue_pending(xlnx_dma_chan->dma_chan);
		}
	}
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
	struct drm_gem_cma_object *obj;
	size_t offset;
	unsigned int i;

	for (i = 0; i < info->num_planes; i++) {
		struct xlnx_dma_chan *xlnx_dma_chan = xlnx_pl_disp->chan[i];
		unsigned int width = src_w / (i ? info->hsub : 1);
		unsigned int height = src_h / (i ? info->vsub : 1);
		unsigned int cpp = drm_format_plane_cpp(info->format, i);

		obj = drm_fb_cma_get_gem_obj(fb, i);
		if (!obj) {
			dev_err(xlnx_pl_disp->dev, "failed to get fb obj\n");
			return -EINVAL;
		}

		xlnx_dma_chan->xt.numf = height;
		xlnx_dma_chan->sgl[0].size = width * cpp;
		xlnx_dma_chan->sgl[0].icg = fb->pitches[i] -
					   xlnx_dma_chan->sgl[0].size;
		offset = src_x * cpp + src_y * fb->pitches[i];
		offset += fb->offsets[i];
		xlnx_dma_chan->xt.src_start = obj->paddr + offset;
		xlnx_dma_chan->xt.frame_size = 1;
		xlnx_dma_chan->xt.dir = DMA_MEM_TO_DEV;
		xlnx_dma_chan->xt.src_sgl = true;
		xlnx_dma_chan->xt.dst_sgl = false;
		xlnx_dma_chan->is_active = true;
	}

	for (; i < XLNX_DMA_MAX_CHAN; i++)
		xlnx_pl_disp->chan[i]->is_active = false;

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
	xilinx_xdma_drm_config(xlnx_pl_disp->chan[0]->dma_chan,
			       xlnx_pl_disp->fmt);
	/* apply the new fb addr and enable */
	xlnx_pl_disp_plane_enable(plane);
}

static const struct drm_plane_helper_funcs xlnx_pl_disp_plane_helper_funcs = {
	.atomic_update = xlnx_pl_disp_plane_atomic_update,
	.atomic_disable = xlnx_pl_disp_plane_atomic_disable,
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
	xlnx_pl_disp_plane_enable(crtc->primary);
}

static void xlnx_pl_disp_crtc_atomic_disable(struct drm_crtc *crtc,
					     struct drm_crtc_state *old_state)
{
	xlnx_pl_disp_plane_disable(crtc->primary);
	xlnx_pl_disp_clear_event(crtc);
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

	ret = drm_universal_plane_init(drm, &xlnx_pl_disp->plane, 0,
				       &xlnx_pl_disp_plane_funcs,
				       &xlnx_pl_disp->fmt, 1,
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
	struct xlnx_pl_disp *xlnx_pl_disp;
	unsigned int i;
	int ret;
	const char *vformat;

	xlnx_pl_disp = devm_kzalloc(dev, sizeof(*xlnx_pl_disp), GFP_KERNEL);
	if (!xlnx_pl_disp)
		return -ENOMEM;

	for (i = 0; i < XLNX_DMA_MAX_CHAN; i++) {
		struct dma_chan *dma_chan;
		struct xlnx_dma_chan *xlnx_dma_chan;
		char temp[16];

		snprintf(temp, sizeof(temp), "dma%d", i);
		dma_chan = of_dma_request_slave_channel(dev->of_node, temp);
		if (IS_ERR(dma_chan)) {
			dev_err(dev, "failed to request dma channel\n");
			return PTR_ERR(dma_chan);
		}

		xlnx_dma_chan = devm_kzalloc(dev, sizeof(*xlnx_dma_chan),
					     GFP_KERNEL);
		if (!xlnx_dma_chan)
			return -ENOMEM;

		xlnx_dma_chan->dma_chan = dma_chan;
		xlnx_pl_disp->chan[i] = xlnx_dma_chan;
	}
	ret = of_property_read_string(dev->of_node, "xlnx,vformat", &vformat);
	if (ret) {
		dev_err(dev, "No xlnx,vformat value in dts\n");
		return ret;
	}

	strcpy((char *)&xlnx_pl_disp->fmt, vformat);
	xlnx_pl_disp->dev = dev;
	platform_set_drvdata(pdev, xlnx_pl_disp);

	return component_add(dev, &xlnx_pl_disp_component_ops);
}

static int xlnx_pl_disp_remove(struct platform_device *pdev)
{
	struct xlnx_pl_disp *xlnx_pl_disp = platform_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < XLNX_DMA_MAX_CHAN; i++) {
		if (xlnx_pl_disp->chan[i] && xlnx_pl_disp->chan[i]->is_active) {
			/* Make sure the channel is terminated before release */
			dmaengine_terminate_all(xlnx_pl_disp->chan[i]->dma_chan);
			dma_release_channel(xlnx_pl_disp->chan[i]->dma_chan);
		}
	}

	component_del(&pdev->dev, &xlnx_pl_disp_component_ops);

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

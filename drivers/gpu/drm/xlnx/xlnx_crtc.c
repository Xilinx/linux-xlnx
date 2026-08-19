// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DRM crtc driver
 *
 *  Copyright (C) 2017 - 2018 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
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

#include <drm/drm_drv.h>
#include <drm/drm_crtc.h>
#include <drm/drm_property.h>

#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include "xlnx_crtc.h"
#include "xlnx_drv.h"

/*
 * Overview
 * --------
 *
 * The Xilinx CRTC layer is to enable the custom interface to CRTC drivers.
 * The interface is used by Xilinx DRM driver where it needs CRTC
 * functionailty. CRTC drivers should attach the desired callbacks
 * to struct xlnx_crtc and register the xlnx_crtc with correcsponding
 * drm_device. It's highly recommended CRTC drivers register all callbacks
 * even though many of them are optional.
 * The CRTC helper simply walks through the registered CRTC device,
 * and call the callbacks.
 */

/**
 * struct xlnx_crtc_helper - Xilinx CRTC helper
 * @xlnx_crtcs: list of Xilinx CRTC devices
 * @lock: lock to protect @xlnx_crtcs
 * @drm: back pointer to DRM core
 */
struct xlnx_crtc_helper {
	struct list_head xlnx_crtcs;
	struct mutex lock; /* lock for @xlnx_crtcs */
	struct drm_device *drm;
};

#define XLNX_CRTC_MAX_HEIGHT_WIDTH	INT_MAX

unsigned int xlnx_crtc_helper_get_align(struct xlnx_crtc_helper *helper)
{
	struct xlnx_crtc *crtc;
	unsigned int align = 1, tmp;

	list_for_each_entry(crtc, &helper->xlnx_crtcs, list) {
		if (crtc->get_align) {
			tmp = crtc->get_align(crtc);
			align = ALIGN(align, tmp);
		}
	}

	return align;
}

u64 xlnx_crtc_helper_get_dma_mask(struct xlnx_crtc_helper *helper)
{
	struct xlnx_crtc *crtc;
	u64 mask = DMA_BIT_MASK(sizeof(dma_addr_t) * 8), tmp;

	list_for_each_entry(crtc, &helper->xlnx_crtcs, list) {
		if (crtc->get_dma_mask) {
			tmp = crtc->get_dma_mask(crtc);
			mask = min(mask, tmp);
		}
	}

	return mask;
}

int xlnx_crtc_helper_get_max_width(struct xlnx_crtc_helper *helper)
{
	struct xlnx_crtc *crtc;
	int width = XLNX_CRTC_MAX_HEIGHT_WIDTH, tmp;

	list_for_each_entry(crtc, &helper->xlnx_crtcs, list) {
		if (crtc->get_max_width) {
			tmp = crtc->get_max_width(crtc);
			width = min(width, tmp);
		}
	}

	return width;
}

int xlnx_crtc_helper_get_max_height(struct xlnx_crtc_helper *helper)
{
	struct xlnx_crtc *crtc;
	int height = XLNX_CRTC_MAX_HEIGHT_WIDTH, tmp;

	list_for_each_entry(crtc, &helper->xlnx_crtcs, list) {
		if (crtc->get_max_height) {
			tmp = crtc->get_max_height(crtc);
			height = min(height, tmp);
		}
	}

	return height;
}

uint32_t xlnx_crtc_helper_get_format(struct xlnx_crtc_helper *helper)
{
	struct xlnx_crtc *crtc;
	u32 format = 0, tmp;

	list_for_each_entry(crtc, &helper->xlnx_crtcs, list) {
		if (crtc->get_format) {
			tmp = crtc->get_format(crtc);
			if (format && format != tmp)
				return 0;
			format = tmp;
		}
	}

	return format;
}

u32 xlnx_crtc_helper_get_cursor_width(struct xlnx_crtc_helper *helper)
{
	struct xlnx_crtc *crtc;
	u32 width = XLNX_CRTC_MAX_HEIGHT_WIDTH, tmp;

	list_for_each_entry(crtc, &helper->xlnx_crtcs, list) {
		if (crtc->get_cursor_width) {
			tmp = crtc->get_cursor_width(crtc);
			width = min(width, tmp);
		}
	}

	return width;
}

u32 xlnx_crtc_helper_get_cursor_height(struct xlnx_crtc_helper *helper)
{
	struct xlnx_crtc *crtc;
	u32 height = XLNX_CRTC_MAX_HEIGHT_WIDTH, tmp;

	list_for_each_entry(crtc, &helper->xlnx_crtcs, list) {
		if (crtc->get_cursor_height) {
			tmp = crtc->get_cursor_height(crtc);
			height = min(height, tmp);
		}
	}

	return height;
}
struct xlnx_crtc_helper *xlnx_crtc_helper_init(struct drm_device *drm)
{
	struct xlnx_crtc_helper *helper;

	helper = devm_kzalloc(drm->dev, sizeof(*helper), GFP_KERNEL);
	if (!helper)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&helper->xlnx_crtcs);
	mutex_init(&helper->lock);
	helper->drm = drm;

	return helper;
}

void xlnx_crtc_helper_fini(struct drm_device *drm,
			   struct xlnx_crtc_helper *helper)
{
	if (WARN_ON(helper->drm != drm))
		return;

	if (WARN_ON(!list_empty(&helper->xlnx_crtcs)))
		return;

	mutex_destroy(&helper->lock);
	devm_kfree(drm->dev, helper);
}

void xlnx_crtc_register(struct drm_device *drm, struct xlnx_crtc *crtc)
{
	struct xlnx_crtc_helper *helper = xlnx_get_crtc_helper(drm);

	mutex_lock(&helper->lock);
	list_add_tail(&crtc->list, &helper->xlnx_crtcs);
	mutex_unlock(&helper->lock);
}
EXPORT_SYMBOL_GPL(xlnx_crtc_register);

void xlnx_crtc_unregister(struct drm_device *drm, struct xlnx_crtc *crtc)
{
	struct xlnx_crtc_helper *helper = xlnx_get_crtc_helper(drm);

	mutex_lock(&helper->lock);
	list_del(&crtc->list);
	mutex_unlock(&helper->lock);
}
EXPORT_SYMBOL_GPL(xlnx_crtc_unregister);

/* Maximum DP Tx MST stream index a CRTC may be routed to. */
#define XLNX_CRTC_STREAM_MAX	3

/**
 * xlnx_crtc_create_stream_property - Attach the immutable "stream" property
 * @drm_crtc: DRM CRTC to attach the property to
 * @of_node: Device node of the CRTC's source, used to resolve its OF graph
 *
 * In an MST pipeline several source CRTCs feed a single DRM bridge chain. The
 * DP Tx stream a CRTC drives is fixed by the hardware wiring: it is the reg of
 * the downstream live-video input port that this source's OF graph output
 * endpoint connects to (port@0 -> stream 0, port@1 -> stream 1, ...). Expose
 * that index as an immutable CRTC property so the DP Tx MST encoder selection
 * can map a CRTC to its stream deterministically, independent of DRM CRTC
 * creation order. Defaults to stream 0 when the OF graph cannot be resolved.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int xlnx_crtc_create_stream_property(struct drm_crtc *drm_crtc,
				     struct device_node *of_node)
{
	struct device_node *ep, *remote_ep;
	struct of_endpoint endpoint;
	struct drm_property *prop;
	u32 stream = 0;

	ep = of_graph_get_endpoint_by_regs(of_node, 0, 0);
	if (ep) {
		remote_ep = of_graph_get_remote_endpoint(ep);
		of_node_put(ep);
		if (remote_ep) {
			if (!of_graph_parse_endpoint(remote_ep, &endpoint))
				stream = endpoint.port;
			of_node_put(remote_ep);
		}
	}

	if (stream > XLNX_CRTC_STREAM_MAX) {
		dev_err(drm_crtc->dev->dev,
			"CRTC %s: stream index %u exceeds max %u\n",
			drm_crtc->name, stream, XLNX_CRTC_STREAM_MAX);
		return -EINVAL;
	}

	prop = drm_property_create_range(drm_crtc->dev, DRM_MODE_PROP_IMMUTABLE,
					 "stream", 0, XLNX_CRTC_STREAM_MAX);
	if (!prop)
		return -ENOMEM;

	drm_object_attach_property(&drm_crtc->base, prop, stream);

	return 0;
}
EXPORT_SYMBOL_GPL(xlnx_crtc_create_stream_property);

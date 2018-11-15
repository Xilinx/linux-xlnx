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

#include <drm/drmP.h>

#include <linux/list.h>

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

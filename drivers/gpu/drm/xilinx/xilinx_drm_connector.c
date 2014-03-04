/*
 * Xilinx DRM connector driver for Xilinx
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
#include <drm/drm_encoder_slave.h>

#include <linux/device.h>

#include "xilinx_drm_drv.h"
#include "xilinx_drm_connector.h"

struct xilinx_drm_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
};

#define to_xilinx_connector(x)	\
	container_of(x, struct xilinx_drm_connector, base)

/* get mode list */
static int xilinx_drm_connector_get_modes(struct drm_connector *base_connector)
{
	struct xilinx_drm_connector *connector =
		to_xilinx_connector(base_connector);
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_slave *encoder_slave = to_encoder_slave(encoder);
	struct drm_encoder_slave_funcs *encoder_sfuncs =
		encoder_slave->slave_funcs;
	int count = 0;

	if (encoder_sfuncs->get_modes)
		count = encoder_sfuncs->get_modes(encoder, base_connector);

	return count;
}

/* check if mode is valid */
static int xilinx_drm_connector_mode_valid(struct drm_connector *base_connector,
					   struct drm_display_mode *mode)
{
	if (mode->clock > 165000)
		return MODE_CLOCK_HIGH;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	return MODE_OK;
}

/* find best encoder: return stored encoder */
static struct drm_encoder *
xilinx_drm_connector_best_encoder(struct drm_connector *base_connector)
{
	struct xilinx_drm_connector *connector =
		to_xilinx_connector(base_connector);

	return connector->encoder;
}

static struct drm_connector_helper_funcs xilinx_drm_connector_helper_funcs = {
	.get_modes	= xilinx_drm_connector_get_modes,
	.mode_valid	= xilinx_drm_connector_mode_valid,
	.best_encoder	= xilinx_drm_connector_best_encoder,
};

static enum drm_connector_status
xilinx_drm_connector_detect(struct drm_connector *base_connector, bool force)
{
	struct xilinx_drm_connector *connector =
		to_xilinx_connector(base_connector);
	enum drm_connector_status status = connector_status_unknown;
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_slave *encoder_slave = to_encoder_slave(encoder);
	struct drm_encoder_slave_funcs *encoder_sfuncs =
		encoder_slave->slave_funcs;

	if (encoder_sfuncs->detect)
		status = encoder_sfuncs->detect(encoder, base_connector);

	/* some connector ignores the first hpd, so try again if forced */
	if (force && (status != connector_status_connected))
		status = encoder_sfuncs->detect(encoder, base_connector);

	DRM_DEBUG_KMS("status: %d\n", status);

	return status;
}

/* destroy connector */
void xilinx_drm_connector_destroy(struct drm_connector *base_connector)
{
	drm_sysfs_connector_remove(base_connector);
	drm_connector_cleanup(base_connector);
}

static struct drm_connector_funcs xilinx_drm_connector_funcs = {
	.dpms		= drm_helper_connector_dpms,
	.fill_modes	= drm_helper_probe_single_connector_modes,
	.detect		= xilinx_drm_connector_detect,
	.destroy	= xilinx_drm_connector_destroy,
};

/* create connector */
struct drm_connector *
xilinx_drm_connector_create(struct drm_device *drm,
			    struct drm_encoder *base_encoder)
{
	struct xilinx_drm_connector *connector;
	int ret;

	connector = devm_kzalloc(drm->dev, sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return ERR_PTR(-ENOMEM);

	connector->base.polled = DRM_CONNECTOR_POLL_CONNECT |
				 DRM_CONNECTOR_POLL_DISCONNECT;

	ret = drm_connector_init(drm, &connector->base,
				 &xilinx_drm_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		DRM_ERROR("failed to initialize connector\n");
		return ERR_PTR(ret);
	}

	drm_connector_helper_add(&connector->base,
				 &xilinx_drm_connector_helper_funcs);

	/* add sysfs entry for connector */
	ret = drm_sysfs_connector_add(&connector->base);
	if (ret) {
		DRM_ERROR("failed to add to sysfs\n");
		goto err_sysfs;
	}

	/* connect connector and encoder */
	connector->base.encoder = base_encoder;
	ret = drm_mode_connector_attach_encoder(&connector->base, base_encoder);
	if (ret) {
		DRM_ERROR("failed to attach connector to encoder\n");
		goto err_attach;
	}
	connector->encoder = base_encoder;

	return &connector->base;

err_attach:
	drm_sysfs_connector_remove(&connector->base);
err_sysfs:
	drm_connector_cleanup(&connector->base);
	return ERR_PTR(ret);
}

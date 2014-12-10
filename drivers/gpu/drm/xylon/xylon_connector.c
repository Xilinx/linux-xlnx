/*
 * Xylon DRM connector functions
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * Reused Xilinx DRM connector driver.
 * Copyright (C) 2013 Xilinx, Inc.
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

#include "xylon_connector.h"
#include "xylon_drv.h"

struct xylon_drm_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
};

#define to_xylon_connector(x) container_of(x, struct xylon_drm_connector, base)

static int xylon_drm_connector_get_modes(struct drm_connector *base_connector)
{
	struct xylon_drm_connector *connector =
		to_xylon_connector(base_connector);
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_slave *encoder_slave = to_encoder_slave(encoder);
	struct drm_encoder_slave_funcs *encoder_sfuncs =
		encoder_slave->slave_funcs;
	int count = 0;

	if (encoder_sfuncs->get_modes)
		count = encoder_sfuncs->get_modes(encoder, base_connector);

	return count;
}

static int xylon_drm_connector_mode_valid(struct drm_connector *base_connector,
					  struct drm_display_mode *mode)
{
	struct xylon_drm_connector *connector =
		to_xylon_connector(base_connector);
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_slave *encoder_slave = to_encoder_slave(encoder);
	struct drm_encoder_slave_funcs *encoder_sfuncs =
		encoder_slave->slave_funcs;
	int ret = MODE_OK;

	if (encoder_sfuncs->mode_valid)
		ret = encoder_sfuncs->mode_valid(encoder, mode);

	return ret;
}

static struct drm_encoder *
xylon_drm_connector_best_encoder(struct drm_connector *base_connector)
{
	struct xylon_drm_connector *connector =
		to_xylon_connector(base_connector);

	return connector->encoder;
}

static struct drm_connector_helper_funcs xylon_drm_connector_helper_funcs = {
	.get_modes = xylon_drm_connector_get_modes,
	.mode_valid = xylon_drm_connector_mode_valid,
	.best_encoder = xylon_drm_connector_best_encoder,
};

static enum drm_connector_status
xylon_drm_connector_detect(struct drm_connector *base_connector, bool force)
{
	struct xylon_drm_connector *connector =
		to_xylon_connector(base_connector);
	enum drm_connector_status status = connector_status_unknown;
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_slave *encoder_slave = to_encoder_slave(encoder);
	struct drm_encoder_slave_funcs *encoder_sfuncs =
		encoder_slave->slave_funcs;

	if (encoder_sfuncs->detect)
		status = encoder_sfuncs->detect(encoder, base_connector);

	if (force && (status != connector_status_connected))
		status = encoder_sfuncs->detect(encoder, base_connector);

	return status;
}

static void xylon_drm_connector_destroy(struct drm_connector *base_connector)
{
	drm_connector_unregister(base_connector);
	drm_connector_cleanup(base_connector);
}

static struct drm_connector_funcs xylon_drm_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = xylon_drm_connector_detect,
	.destroy = xylon_drm_connector_destroy,
};

struct drm_connector *
xylon_drm_connector_create(struct drm_device *dev,
			   struct drm_encoder *base_encoder)
{
	struct xylon_drm_connector *connector;
	int ret;

	connector = devm_kzalloc(dev->dev, sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return ERR_PTR(-ENOMEM);

	connector->base.encoder = base_encoder;
	connector->base.polled = DRM_CONNECTOR_POLL_CONNECT |
				 DRM_CONNECTOR_POLL_DISCONNECT;

	ret = drm_connector_init(dev, &connector->base,
				 &xylon_drm_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		DRM_ERROR("failed initialize connector\n");
		return ERR_PTR(ret);
	}

	drm_connector_helper_add(&connector->base,
				 &xylon_drm_connector_helper_funcs);

	ret = drm_connector_register(&connector->base);
	if (ret) {
		DRM_ERROR("failed register encoder connector\n");
		goto err_register;
	}

	ret = drm_mode_connector_attach_encoder(&connector->base, base_encoder);
	if (ret) {
		DRM_ERROR("failed attach encoder connector\n");
		goto err_attach;
	}
	connector->encoder = base_encoder;

	return &connector->base;

err_attach:
	drm_connector_unregister(&connector->base);
err_register:
	drm_connector_cleanup(&connector->base);
	return ERR_PTR(ret);
}

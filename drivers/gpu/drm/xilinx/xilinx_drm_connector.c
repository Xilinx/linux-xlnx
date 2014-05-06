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

struct xilinx_drm_connector_type {
	const char *name;
	const int type;
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
	struct xilinx_drm_connector *connector =
		to_xilinx_connector(base_connector);
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_slave *encoder_slave = to_encoder_slave(encoder);
	struct drm_encoder_slave_funcs *encoder_sfuncs =
		encoder_slave->slave_funcs;
	int ret = MODE_OK;

	if (encoder_sfuncs->mode_valid)
		ret = encoder_sfuncs->mode_valid(encoder, mode);

	return ret;
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

static const struct xilinx_drm_connector_type connector_types[] = {
	{ "HDMIA", DRM_MODE_CONNECTOR_HDMIA },
	{ "DisplayPort", DRM_MODE_CONNECTOR_DisplayPort },
};

/* create connector */
struct drm_connector *
xilinx_drm_connector_create(struct drm_device *drm,
			    struct drm_encoder *base_encoder)
{
	struct xilinx_drm_connector *connector;
	const char *string;
	int type = DRM_MODE_CONNECTOR_Unknown;
	int i, ret;

	connector = devm_kzalloc(drm->dev, sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return ERR_PTR(-ENOMEM);

	connector->base.polled = DRM_CONNECTOR_POLL_CONNECT |
				 DRM_CONNECTOR_POLL_DISCONNECT;

	ret = of_property_read_string(drm->dev->of_node, "xlnx,connector-type",
				      &string);
	if (ret < 0) {
		dev_err(drm->dev, "No connector type in DT\n");
		return ERR_PTR(ret);
	}

	for (i = 0; i < ARRAY_SIZE(connector_types); i++)
		if (strcmp(connector_types[i].name, string) == 0) {
			type = connector_types[i].type;
			break;
		}

	if (type == DRM_MODE_CONNECTOR_Unknown) {
		dev_err(drm->dev, "Unknown connector type in DT\n");
		return ERR_PTR(-EINVAL);
	}

	ret = drm_connector_init(drm, &connector->base,
				 &xilinx_drm_connector_funcs, type);
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

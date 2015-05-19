/*
 * Xylon DRM encoder functions
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * Reused Xilinx DRM encoder driver.
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

#include <linux/hdmi.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "xylon_drv.h"
#include "xylon_encoder.h"

struct xylon_drm_encoder {
	struct drm_encoder_slave slave;
	struct i2c_client *client;
	int dpms;
};

#define to_xylon_encoder(x) container_of(x, struct xylon_drm_encoder, slave)

static void xylon_drm_encoder_dpms(struct drm_encoder *base_encoder, int dpms)
{
	struct xylon_drm_encoder *encoder;
	struct drm_encoder_slave *encoder_slave;
	struct drm_encoder_slave_funcs *encoder_sfuncs;

	encoder_slave = to_encoder_slave(base_encoder);
	encoder_sfuncs = encoder_slave->slave_funcs;
	encoder = to_xylon_encoder(encoder_slave);

	if (encoder->dpms == dpms)
		return;

	encoder->dpms = dpms;
	if (encoder_sfuncs->dpms)
		encoder_sfuncs->dpms(base_encoder, dpms);
}

static bool
xylon_drm_encoder_mode_fixup(struct drm_encoder *base_encoder,
			     const struct drm_display_mode *mode,
			     struct drm_display_mode *adjusted_mode)
{
	struct drm_encoder_slave *encoder_slave;
	struct drm_encoder_slave_funcs *encoder_sfuncs;
	bool ret = true;

	encoder_slave = to_encoder_slave(base_encoder);
	encoder_sfuncs = encoder_slave->slave_funcs;
	if (encoder_sfuncs->mode_fixup)
		ret = encoder_sfuncs->mode_fixup(base_encoder, mode,
						 adjusted_mode);

	return ret;
}

static void xylon_drm_encoder_mode_set(struct drm_encoder *base_encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	struct xylon_drm_encoder *encoder;
	struct drm_device *dev = base_encoder->dev;
	struct drm_encoder_slave *encoder_slave;
	struct drm_encoder_slave_funcs *encoder_sfuncs;
	struct drm_connector *iter;
	struct drm_connector *connector = NULL;

	DRM_DEBUG("h: %d, v: %d\n",
		  adjusted_mode->hdisplay, adjusted_mode->vdisplay);
	DRM_DEBUG("refresh: %d, pclock: %d khz\n",
		  adjusted_mode->vrefresh, adjusted_mode->clock);

	encoder_slave = to_encoder_slave(base_encoder);
	encoder = to_xylon_encoder(encoder_slave);

	list_for_each_entry(iter, &dev->mode_config.connector_list, head) {
		if (iter->encoder == base_encoder) {
			connector = iter;
			break;
		}
	}
	if (!connector) {
		DRM_ERROR("failed find a connector\n");
		return;
	}

	encoder_sfuncs = encoder_slave->slave_funcs;
	if (encoder_sfuncs->mode_set)
		encoder_sfuncs->mode_set(base_encoder, mode, adjusted_mode);
}

static void xylon_drm_encoder_commit(struct drm_encoder *base_encoder)
{
	xylon_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_ON);
}

static void xylon_drm_encoder_prepare(struct drm_encoder *base_encoder)
{
	xylon_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_OFF);
}

static struct drm_crtc *
xylon_drm_encoder_get_crtc(struct drm_encoder *base_encoder)
{
	return base_encoder->crtc;
}

static struct drm_encoder_helper_funcs xylon_drm_encoder_helper_funcs = {
	.dpms = xylon_drm_encoder_dpms,
	.mode_fixup = xylon_drm_encoder_mode_fixup,
	.mode_set = xylon_drm_encoder_mode_set,
	.prepare = xylon_drm_encoder_prepare,
	.commit = xylon_drm_encoder_commit,
	.get_crtc = xylon_drm_encoder_get_crtc,
};

static void xylon_drm_encoder_destroy(struct drm_encoder *base_encoder)
{
	struct xylon_drm_encoder *encoder;
	struct drm_encoder_slave *encoder_slave;

	encoder_slave = to_encoder_slave(base_encoder);
	encoder = to_xylon_encoder(encoder_slave);

	xylon_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_OFF);

	drm_encoder_cleanup(base_encoder);
	put_device(&encoder->client->dev);
}

static struct drm_encoder_funcs xylon_drm_encoder_funcs = {
	.destroy = xylon_drm_encoder_destroy,
};

struct drm_encoder *xylon_drm_encoder_create(struct drm_device *dev)
{
	struct xylon_drm_encoder *encoder;
	struct device_node *sub_node;
	struct i2c_driver *i2c_driver;
	struct drm_i2c_encoder_driver *drm_i2c_driver;
	int ret;

	encoder = devm_kzalloc(dev->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return ERR_PTR(-ENOMEM);

	encoder->dpms = DRM_MODE_DPMS_OFF;

	encoder->slave.base.possible_crtcs = 1;
	ret = drm_encoder_init(dev, &encoder->slave.base,
			       &xylon_drm_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS);
	if (ret) {
		DRM_ERROR("failed initialize encoder\n");
		return ERR_PTR(ret);
	}

	drm_encoder_helper_add(&encoder->slave.base,
			       &xylon_drm_encoder_helper_funcs);

	sub_node = of_parse_phandle(dev->dev->of_node, "encoder", 0);
	if (!sub_node) {
		DRM_ERROR("failed get encoder\n");
		return ERR_PTR(-ENODEV);
	}

	encoder->client = of_find_i2c_device_by_node(sub_node);
	of_node_put(sub_node);
	if (!encoder->client || !encoder->client->dev.driver) {
		DRM_INFO("failed find encoder\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	i2c_driver = to_i2c_driver(encoder->client->dev.driver);
	drm_i2c_driver = to_drm_i2c_encoder_driver(i2c_driver);
	if (!drm_i2c_driver) {
		DRM_ERROR("failed initialize encoder driver\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	ret = drm_i2c_driver->encoder_init(encoder->client, dev,
					   &encoder->slave);
	if (ret) {
		DRM_ERROR("failed initialize encoder\n");
		return ERR_PTR(ret);
	}

	if (!encoder->slave.slave_funcs) {
		DRM_ERROR("failed check encoder function\n");
		return ERR_PTR(-ENODEV);
	}

	return &encoder->slave.base;
}

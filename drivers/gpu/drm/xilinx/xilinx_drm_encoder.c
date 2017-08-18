/*
 * Xilinx DRM encoder driver for Xilinx
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

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "xilinx_drm_drv.h"
#include "xilinx_drm_encoder.h"

struct xilinx_drm_encoder {
	struct drm_encoder_slave slave;
	struct device *dev;
	int dpms;
};

#define to_xilinx_encoder(x)	\
	container_of(x, struct xilinx_drm_encoder, slave)

/* set encoder dpms */
static void xilinx_drm_encoder_dpms(struct drm_encoder *base_encoder, int dpms)
{
	struct xilinx_drm_encoder *encoder;
	struct drm_encoder_slave *encoder_slave;
	const struct drm_encoder_slave_funcs *encoder_sfuncs;

	encoder_slave = to_encoder_slave(base_encoder);
	encoder_sfuncs = encoder_slave->slave_funcs;
	encoder = to_xilinx_encoder(encoder_slave);

	DRM_DEBUG_KMS("dpms: %d -> %d\n", encoder->dpms, dpms);

	if (encoder->dpms == dpms)
		return;

	encoder->dpms = dpms;
	if (encoder_sfuncs->dpms)
		encoder_sfuncs->dpms(base_encoder, dpms);
}

/* adjust a mode if needed */
static bool
xilinx_drm_encoder_mode_fixup(struct drm_encoder *base_encoder,
			      const struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode)
{
	struct drm_encoder_slave *encoder_slave;
	const struct drm_encoder_slave_funcs *encoder_sfuncs = NULL;
	bool ret = true;

	encoder_slave = to_encoder_slave(base_encoder);
	encoder_sfuncs = encoder_slave->slave_funcs;
	if (encoder_sfuncs->mode_fixup)
		ret = encoder_sfuncs->mode_fixup(base_encoder, mode,
						 adjusted_mode);

	return ret;
}

/* set mode to xilinx encoder */
static void xilinx_drm_encoder_mode_set(struct drm_encoder *base_encoder,
					struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	struct drm_encoder_slave *encoder_slave;
	const struct drm_encoder_slave_funcs *encoder_sfuncs;

	DRM_DEBUG_KMS("h: %d, v: %d\n",
		      adjusted_mode->hdisplay, adjusted_mode->vdisplay);
	DRM_DEBUG_KMS("refresh: %d, pclock: %d khz\n",
		      adjusted_mode->vrefresh, adjusted_mode->clock);

	encoder_slave = to_encoder_slave(base_encoder);
	encoder_sfuncs = encoder_slave->slave_funcs;
	if (encoder_sfuncs->mode_set)
		encoder_sfuncs->mode_set(base_encoder, mode, adjusted_mode);
}

/* apply mode to encoder pipe */
static void xilinx_drm_encoder_commit(struct drm_encoder *base_encoder)
{
	/* start encoder with new mode */
	xilinx_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_ON);
}

/* prepare encoder */
static void xilinx_drm_encoder_prepare(struct drm_encoder *base_encoder)
{
	xilinx_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_OFF);
}

/* get crtc */
static struct drm_crtc *
xilinx_drm_encoder_get_crtc(struct drm_encoder *base_encoder)
{
	return base_encoder->crtc;
}

static const struct drm_encoder_helper_funcs xilinx_drm_encoder_helper_funcs = {
	.dpms		= xilinx_drm_encoder_dpms,
	.mode_fixup	= xilinx_drm_encoder_mode_fixup,
	.mode_set	= xilinx_drm_encoder_mode_set,
	.prepare	= xilinx_drm_encoder_prepare,
	.commit		= xilinx_drm_encoder_commit,
	.get_crtc	= xilinx_drm_encoder_get_crtc,
};

/* destroy encoder */
void xilinx_drm_encoder_destroy(struct drm_encoder *base_encoder)
{
	struct xilinx_drm_encoder *encoder;
	struct drm_encoder_slave *encoder_slave;

	encoder_slave = to_encoder_slave(base_encoder);
	encoder = to_xilinx_encoder(encoder_slave);

	/* make sure encoder is off */
	xilinx_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_OFF);

	drm_encoder_cleanup(base_encoder);
	put_device(encoder->dev);
}

static const struct drm_encoder_funcs xilinx_drm_encoder_funcs = {
	.destroy	= xilinx_drm_encoder_destroy,
};

/* create encoder */
struct drm_encoder *xilinx_drm_encoder_create(struct drm_device *drm,
					      struct device_node *node)
{
	struct xilinx_drm_encoder *encoder;
	struct i2c_client *i2c_slv;
	struct i2c_driver *i2c_driver;
	struct drm_i2c_encoder_driver *drm_i2c_driver;
	struct device_driver *device_driver;
	struct platform_device *platform_slv;
	struct platform_driver *platform_driver;
	struct drm_platform_encoder_driver *drm_platform_driver;
	int ret = 0;

	encoder = devm_kzalloc(drm->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return ERR_PTR(-ENOMEM);

	encoder->dpms = DRM_MODE_DPMS_OFF;

	/* FIXME: Use DT to figure out crtcs / clones */
	encoder->slave.base.possible_crtcs = 1;
	encoder->slave.base.possible_clones = ~0;
	ret = drm_encoder_init(drm, &encoder->slave.base,
			       &xilinx_drm_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize drm encoder\n");
		return ERR_PTR(ret);
	}

	drm_encoder_helper_add(&encoder->slave.base,
			       &xilinx_drm_encoder_helper_funcs);

	/* initialize slave encoder */
	i2c_slv = of_find_i2c_device_by_node(node);
	if (i2c_slv && i2c_slv->dev.driver) {
		i2c_driver = to_i2c_driver(i2c_slv->dev.driver);
		drm_i2c_driver = to_drm_i2c_encoder_driver(i2c_driver);
		if (!drm_i2c_driver || !drm_i2c_driver->encoder_init) {
			DRM_DEBUG_KMS("failed to initialize i2c slave\n");
			ret = -EPROBE_DEFER;
			goto err_out;
		}

		encoder->dev = &i2c_slv->dev;
		ret = drm_i2c_driver->encoder_init(i2c_slv, drm,
						   &encoder->slave);
	} else {
		platform_slv = of_find_device_by_node(node);
		if (!platform_slv) {
			DRM_DEBUG_KMS("failed to get an encoder slv\n");
			return ERR_PTR(-EPROBE_DEFER);
		}

		device_driver = platform_slv->dev.driver;
		if (!device_driver) {
			DRM_DEBUG_KMS("failed to get device driver\n");
			return ERR_PTR(-EPROBE_DEFER);
		}

		platform_driver = to_platform_driver(device_driver);
		drm_platform_driver =
			to_drm_platform_encoder_driver(platform_driver);
		if (!drm_platform_driver ||
		    !drm_platform_driver->encoder_init) {
			DRM_DEBUG_KMS("failed to initialize platform slave\n");
			ret = -EPROBE_DEFER;
			goto err_out;
		}

		encoder->dev = &platform_slv->dev;
		ret = drm_platform_driver->encoder_init(platform_slv, drm,
							&encoder->slave);
	}

	if (ret) {
		DRM_ERROR("failed to initialize encoder slave\n");
		goto err_out;
	}

	if (!encoder->slave.slave_funcs) {
		DRM_ERROR("there's no encoder slave function\n");
		ret = -ENODEV;
		goto err_out;
	}

	return &encoder->slave.base;

err_out:
	return ERR_PTR(ret);
}

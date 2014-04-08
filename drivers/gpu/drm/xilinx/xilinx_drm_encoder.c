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
#include <drm/i2c/adv7511.h>

#include <linux/hdmi.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "xilinx_drm_drv.h"
#include "xilinx_drm_encoder.h"

struct xilinx_drm_encoder {
	struct drm_encoder_slave slave;
	struct i2c_client *i2c_slave;
	bool rgb;
	int dpms;
};

#define to_xilinx_encoder(x)	\
	container_of(x, struct xilinx_drm_encoder, slave)

/* coefficients for adv7511 color space conversion */
static const uint16_t adv7511_csc_ycbcr_to_rgb[] = {
	0x0734, 0x04ad, 0x0000, 0x1c1b,
	0x1ddc, 0x04ad, 0x1f24, 0x0135,
	0x0000, 0x04ad, 0x087c, 0x1b77,
};

/* set encoder dpms */
static void xilinx_drm_encoder_dpms(struct drm_encoder *base_encoder, int dpms)
{
	struct xilinx_drm_encoder *encoder;
	struct drm_encoder_slave *encoder_slave;
	struct drm_encoder_slave_funcs *encoder_sfuncs;

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
	struct drm_encoder_slave_funcs *encoder_sfuncs = NULL;
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
	struct xilinx_drm_encoder *encoder;
	struct drm_device *dev = base_encoder->dev;
	struct drm_encoder_slave *encoder_slave;
	struct drm_encoder_slave_funcs *encoder_sfuncs;
	struct drm_connector *iter;
	struct drm_connector *connector = NULL;
	struct adv7511_video_config config;
	struct edid *edid;

	DRM_DEBUG_KMS("h: %d, v: %d\n",
		      adjusted_mode->hdisplay, adjusted_mode->vdisplay);
	DRM_DEBUG_KMS("refresh: %d, pclock: %d khz\n",
		      adjusted_mode->vrefresh, adjusted_mode->clock);

	encoder_slave = to_encoder_slave(base_encoder);
	encoder = to_xilinx_encoder(encoder_slave);

	/* search for a connector for this encoder.
	 * assume there's only one connector for this encoder
	 */
	list_for_each_entry(iter, &dev->mode_config.connector_list, head) {
		if (iter->encoder == base_encoder) {
			connector = iter;
			break;
		}
	}
	if (!connector) {
		DRM_ERROR("failed to find a connector\n");
		return;
	}

	edid = adv7511_get_edid(base_encoder);
	if (edid) {
		config.hdmi_mode = drm_detect_hdmi_monitor(edid);
		kfree(edid);
	} else
		config.hdmi_mode = false;

	hdmi_avi_infoframe_init(&config.avi_infoframe);

	config.avi_infoframe.scan_mode = HDMI_SCAN_MODE_UNDERSCAN;

	if (encoder->rgb) {
		config.csc_enable = false;
		config.avi_infoframe.colorspace = HDMI_COLORSPACE_RGB;
	} else {
		config.csc_scaling_factor = ADV7511_CSC_SCALING_4;
		config.csc_coefficents = adv7511_csc_ycbcr_to_rgb;

		if ((connector->display_info.color_formats &
		     DRM_COLOR_FORMAT_YCRCB422) &&
		    config.hdmi_mode) {
			config.csc_enable = false;
			config.avi_infoframe.colorspace =
				HDMI_COLORSPACE_YUV422;
		} else {
			config.csc_enable = true;
			config.avi_infoframe.colorspace = HDMI_COLORSPACE_RGB;
		}
	}

	encoder_sfuncs = encoder_slave->slave_funcs;
	if (encoder_sfuncs->set_config)
		encoder_sfuncs->set_config(base_encoder, &config);

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

static struct drm_encoder_helper_funcs xilinx_drm_encoder_helper_funcs = {
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
	put_device(&encoder->i2c_slave->dev);
}

static struct drm_encoder_funcs xilinx_drm_encoder_funcs = {
	.destroy	= xilinx_drm_encoder_destroy,
};

/* create encoder */
struct drm_encoder *xilinx_drm_encoder_create(struct drm_device *drm)
{
	struct xilinx_drm_encoder *encoder;
	struct device_node *sub_node;
	struct i2c_driver *i2c_driver;
	struct drm_i2c_encoder_driver *drm_i2c_driver;
	int ret;

	encoder = devm_kzalloc(drm->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return ERR_PTR(-ENOMEM);

	encoder->dpms = DRM_MODE_DPMS_OFF;

	/* get slave encoder */
	sub_node = of_parse_phandle(drm->dev->of_node, "encoder-slave", 0);
	if (!sub_node) {
		DRM_ERROR("failed to get an encoder slave node\n");
		return ERR_PTR(-ENODEV);
	}

	encoder->i2c_slave = of_find_i2c_device_by_node(sub_node);
	of_node_put(sub_node);
	if (!encoder->i2c_slave) {
		DRM_DEBUG_KMS("failed to get an encoder slv\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	/* initialize slave encoder */
	i2c_driver = to_i2c_driver(encoder->i2c_slave->dev.driver);
	drm_i2c_driver = to_drm_i2c_encoder_driver(i2c_driver);
	if (!drm_i2c_driver) {
		DRM_ERROR("failed to initialize encoder slave\n");
		ret = -EPROBE_DEFER;
		goto err_out;
	}

	ret = drm_i2c_driver->encoder_init(encoder->i2c_slave, drm,
					   &encoder->slave);
	if (ret) {
		DRM_ERROR("failed to initialize encoder slave\n");
		goto err_out;
	}

	if (!encoder->slave.slave_funcs) {
		DRM_ERROR("there's no encoder slave function\n");
		ret = -ENODEV;
		goto err_out;
	}

	encoder->rgb = of_property_read_bool(drm->dev->of_node, "adi,is-rgb");

	/* initialize encoder */
	encoder->slave.base.possible_crtcs = 1;
	ret = drm_encoder_init(drm, &encoder->slave.base,
			       &xilinx_drm_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS);
	if (ret) {
		DRM_ERROR("failed to initialize drm encoder\n");
		goto err_out;
	}

	drm_encoder_helper_add(&encoder->slave.base,
			       &xilinx_drm_encoder_helper_funcs);

	return &encoder->slave.base;

err_out:
	put_device(&encoder->i2c_slave->dev);
	return ERR_PTR(ret);
}

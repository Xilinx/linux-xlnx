/*
 * Analog Devices ADV7511 HDMI transmitter driver
 *
 * Copyright 2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>

#include <drm/i2c/adv7511.h>

#define MAX_CLOCK	165000

static const uint8_t adv7511_register_defaults[] = {
	0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 00 */
	0x00, 0x00, 0x01, 0x0e, 0xbc, 0x18, 0x01, 0x13,
	0x25, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 10 */
	0x46, 0x62, 0x04, 0xa8, 0x00, 0x00, 0x1c, 0x84,
	0x1c, 0xbf, 0x04, 0xa8, 0x1e, 0x70, 0x02, 0x1e, /* 20 */
	0x00, 0x00, 0x04, 0xa8, 0x08, 0x12, 0x1b, 0xac,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 30 */
	0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xb0,
	0x00, 0x50, 0x90, 0x7e, 0x79, 0x70, 0x00, 0x00, /* 40 */
	0x00, 0xa8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x02, 0x0d, 0x00, 0x00, 0x00, 0x00, /* 50 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 60 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 70 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 80 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, /* 90 */
	0x0b, 0x02, 0x00, 0x18, 0x5a, 0x60, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x80, 0x08, 0x04, 0x00, 0x00, /* a0 */
	0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x14,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* b0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* c0 */
	0x00, 0x03, 0x00, 0x00, 0x02, 0x00, 0x01, 0x04,
	0x30, 0xff, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, /* d0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x01,
	0x80, 0x75, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, /* e0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x11, 0x00, /* f0 */
	0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ADI recommanded values for proper operation. */
static const struct reg_default adv7511_fixed_registers[] = {
	{ 0x98, 0x03 },
	{ 0x9a, 0xe0 },
	{ 0x9c, 0x30 },
	{ 0x9d, 0x61 },
	{ 0xa2, 0xa4 },
	{ 0xa3, 0xa4 },
	{ 0xe0, 0xd0 },
	{ 0xf9, 0x00 },
	{ 0x55, 0x02 },
};

/* Coefficients for color space conversion */
static const uint16_t adv7511_csc_ycbcr_to_rgb[] = {
	0x0734, 0x04ad, 0x0000, 0x1c1b,
	0x1ddc, 0x04ad, 0x1f24, 0x0135,
	0x0000, 0x04ad, 0x087c, 0x1b77,
};

static struct adv7511 *encoder_to_adv7511(struct drm_encoder *encoder)
{
	return to_encoder_slave(encoder)->slave_priv;
}

static void adv7511_set_colormap(struct adv7511 *adv7511, bool enable,
				 const uint16_t *coeff,
				 unsigned int scaling_factor)
{
	unsigned int i;

	regmap_update_bits(adv7511->regmap, ADV7511_REG_CSC_UPPER(1),
			   ADV7511_CSC_UPDATE_MODE, ADV7511_CSC_UPDATE_MODE);

	if (enable) {
		for (i = 0; i < 12; ++i) {
			regmap_update_bits(adv7511->regmap,
					   ADV7511_REG_CSC_UPPER(i),
					   0x1f, coeff[i] >> 8);
			regmap_write(adv7511->regmap,
				     ADV7511_REG_CSC_LOWER(i),
				     coeff[i] & 0xff);
		}
	}

	if (enable)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_CSC_UPPER(0),
				   0xe0, 0x80 | (scaling_factor << 5));
	else
		regmap_update_bits(adv7511->regmap, ADV7511_REG_CSC_UPPER(0),
				   0x80, 0x00);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_CSC_UPPER(1),
			   ADV7511_CSC_UPDATE_MODE, 0);
}

#define ADV7511_HDMI_CFG_MODE_MASK 0x2
#define ADV7511_HDMI_CFG_MODE_DVI 0x0
#define ADV7511_HDMI_CFG_MODE_HDMI 0x2

#define ADV7511_PACKET_MEM_SPD		0
#define ADV7511_PACKET_MEM_MPEG		1
#define ADV7511_PACKET_MEM_ACP		2
#define ADV7511_PACKET_MEM_ISRC1	3
#define ADV7511_PACKET_MEM_ISRC2	4
#define ADV7511_PACKET_MEM_GM		5
#define ADV7511_PACKET_MEM_SPARE1	6
#define ADV7511_PACKET_MEM_SPARE2	7

#define ADV7511_PACKET_MEM_DATA_REG(x) ((x) * 0x20)
#define ADV7511_PACKET_MEM_UPDATE_REG(x) ((x) * 0x20 + 0x1f)
#define ADV7511_PACKET_MEM_UPDATE_ENABLE BIT(7)

static void adv7511_set_config(struct drm_encoder *encoder, void *c)
{
	struct adv7511 *adv7511 = encoder_to_adv7511(encoder);
	struct adv7511_video_config *config = c;
	bool output_format_422, output_format_ycbcr;
	unsigned int mode;
	uint8_t infoframe[17];

	if (config->hdmi_mode) {
		mode = ADV7511_HDMI_CFG_MODE_HDMI;

		switch (config->avi_infoframe.colorspace) {
		case HDMI_COLORSPACE_YUV444:
			output_format_422 = false;
			output_format_ycbcr = true;
			break;
		case HDMI_COLORSPACE_YUV422:
			output_format_422 = true;
			output_format_ycbcr = true;
			break;
		default:
			output_format_422 = false;
			output_format_ycbcr = false;
			break;
		}
	} else {
		mode = ADV7511_HDMI_CFG_MODE_DVI;
		output_format_422 = false;
		output_format_ycbcr = false;
	}

	adv7511_packet_disable(adv7511, ADV7511_PACKET_ENABLE_AVI_INFOFRAME);

	adv7511_set_colormap(adv7511, config->csc_enable,
			     config->csc_coefficents,
			     config->csc_scaling_factor);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_VIDEO_INPUT_CFG1, 0x81,
			   (output_format_422 << 7) | output_format_ycbcr);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_HDCP_HDMI_CFG,
			   ADV7511_HDMI_CFG_MODE_MASK, mode);

	hdmi_avi_infoframe_pack(&config->avi_infoframe, infoframe,
				sizeof(infoframe));

	/* The AVI infoframe id is not configurable */
	regmap_bulk_write(adv7511->regmap, ADV7511_REG_AVI_INFOFRAME_VERSION,
			  infoframe + 1, sizeof(infoframe) - 1);

	adv7511_packet_enable(adv7511, ADV7511_PACKET_ENABLE_AVI_INFOFRAME);
}

static void adv7511_set_link_config(struct adv7511 *adv7511,
				    const struct adv7511_link_config *config)
{
	enum adv7511_input_sync_pulse sync_pulse;

	switch (config->id) {
	case ADV7511_INPUT_ID_12_15_16BIT_RGB444_YCbCr444:
		sync_pulse = ADV7511_INPUT_SYNC_PULSE_NONE;
		break;
	default:
		sync_pulse = config->sync_pulse;
		break;
	}

	switch (config->id) {
	case ADV7511_INPUT_ID_16_20_24BIT_YCbCr422_EMBEDDED_SYNC:
	case ADV7511_INPUT_ID_8_10_12BIT_YCbCr422_EMBEDDED_SYNC:
		adv7511->embedded_sync = true;
		break;
	default:
		adv7511->embedded_sync = false;
		break;
	}

	regmap_update_bits(adv7511->regmap, ADV7511_REG_I2C_FREQ_ID_CFG, 0xf,
			   config->id);
	regmap_update_bits(adv7511->regmap, ADV7511_REG_VIDEO_INPUT_CFG1, 0x7e,
			   (config->input_color_depth << 4) |
			   (config->input_style << 2));
	regmap_write(adv7511->regmap, ADV7511_REG_VIDEO_INPUT_CFG2,
		     (config->reverse_bitorder << 6) |
		     (config->bit_justification << 3));
	regmap_write(adv7511->regmap, ADV7511_REG_TIMING_GEN_SEQ,
		     (sync_pulse << 2) | (config->timing_gen_seq << 1));
	regmap_write(adv7511->regmap, 0xba, (config->clock_delay << 5));

	regmap_update_bits(adv7511->regmap, ADV7511_REG_TMDS_CLOCK_INV, 0x08,
			   config->tmds_clock_inversion << 3);

	adv7511->hsync_polarity = config->hsync_polarity;
	adv7511->vsync_polarity = config->vsync_polarity;
	adv7511->rgb = config->rgb;
}

int adv7511_packet_enable(struct adv7511 *adv7511, unsigned int packet)
{
	if (packet & 0xff)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE0,
				   packet, 0xff);

	if (packet & 0xff00) {
		packet >>= 8;
		regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE1,
				   packet, 0xff);
	}

	return 0;
}

int adv7511_packet_disable(struct adv7511 *adv7511, unsigned int packet)
{
	if (packet & 0xff)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE0,
				   packet, 0x00);

	if (packet & 0xff00) {
		packet >>= 8;
		regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE1,
				   packet, 0x00);
	}

	return 0;
}

static bool adv7511_register_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADV7511_REG_SPDIF_FREQ:
	case ADV7511_REG_CTS_AUTOMATIC1:
	case ADV7511_REG_CTS_AUTOMATIC2:
	case ADV7511_REG_VIC_DETECTED:
	case ADV7511_REG_VIC_SEND:
	case ADV7511_REG_AUX_VIC_DETECTED:
	case ADV7511_REG_STATUS:
	case ADV7511_REG_GC(1):
	case ADV7511_REG_INT(0):
	case ADV7511_REG_INT(1):
	case ADV7511_REG_PLL_STATUS:
	case ADV7511_REG_AN(0):
	case ADV7511_REG_AN(1):
	case ADV7511_REG_AN(2):
	case ADV7511_REG_AN(3):
	case ADV7511_REG_AN(4):
	case ADV7511_REG_AN(5):
	case ADV7511_REG_AN(6):
	case ADV7511_REG_AN(7):
	case ADV7511_REG_HDCP_STATUS:
	case ADV7511_REG_BCAPS:
	case ADV7511_REG_BKSV(0):
	case ADV7511_REG_BKSV(1):
	case ADV7511_REG_BKSV(2):
	case ADV7511_REG_BKSV(3):
	case ADV7511_REG_BKSV(4):
	case ADV7511_REG_DDC_STATUS:
	case ADV7511_REG_BSTATUS(0):
	case ADV7511_REG_BSTATUS(1):
	case ADV7511_REG_CHIP_ID_HIGH:
	case ADV7511_REG_CHIP_ID_LOW:
		return true;
	}

	return false;
}

static bool adv7511_hpd(struct adv7511 *adv7511)
{
	unsigned int irq0;
	int ret;

	ret = regmap_read(adv7511->regmap, ADV7511_REG_INT(0), &irq0);
	if (ret < 0)
		return false;

	if (irq0 & ADV7511_INT0_HDP) {
		regmap_write(adv7511->regmap, ADV7511_REG_INT(0),
			     ADV7511_INT0_HDP);
		return true;
	}

	return false;
}

static irqreturn_t adv7511_irq_handler(int irq, void *devid)
{
	struct adv7511 *adv7511 = devid;

	if (adv7511_hpd(adv7511))
		drm_helper_hpd_irq_event(adv7511->encoder->dev);

	wake_up_all(&adv7511->wq);

	return IRQ_HANDLED;
}

static unsigned int adv7511_is_interrupt_pending(struct adv7511 *adv7511,
						 unsigned int irq)
{
	unsigned int irq0, irq1;
	unsigned int pending;
	int ret;

	ret = regmap_read(adv7511->regmap, ADV7511_REG_INT(0), &irq0);
	if (ret < 0)
		return 0;
	ret = regmap_read(adv7511->regmap, ADV7511_REG_INT(1), &irq1);
	if (ret < 0)
		return 0;

	pending = (irq1 << 8) | irq0;

	return pending & irq;
}

static int adv7511_wait_for_interrupt(struct adv7511 *adv7511, int irq,
				      int timeout)
{
	unsigned int pending = 0;
	int ret;

	if (adv7511->i2c_main->irq) {
		ret = wait_event_interruptible_timeout(adv7511->wq,
				adv7511_is_interrupt_pending(adv7511, irq),
				msecs_to_jiffies(timeout));
		if (ret <= 0)
			return 0;
		pending = adv7511_is_interrupt_pending(adv7511, irq);
	} else {
		if (timeout < 25)
			timeout = 25;
		do {
			pending = adv7511_is_interrupt_pending(adv7511, irq);
			if (pending)
				break;
			msleep(25);
			timeout -= 25;
		} while (timeout >= 25);
	}

	return pending;
}

static int adv7511_get_edid_block(void *data, unsigned char *buf, int block,
				  int len)
{
	struct drm_encoder *encoder = data;
	struct adv7511 *adv7511 = encoder_to_adv7511(encoder);
	struct i2c_msg xfer[2];
	uint8_t offset;
	int i;
	int ret;

	if (len > 128)
		return -EINVAL;

	if (adv7511->current_edid_segment != block / 2) {
		unsigned int status;

		ret = regmap_read(adv7511->regmap, ADV7511_REG_DDC_STATUS,
				  &status);
		if (ret < 0)
			return ret;

		if (status != 2) {
			regmap_write(adv7511->regmap, ADV7511_REG_EDID_SEGMENT,
				     block);
			ret = adv7511_wait_for_interrupt(adv7511,
					ADV7511_INT0_EDID_READY |
					ADV7511_INT1_DDC_ERROR, 200);

			if (!(ret & ADV7511_INT0_EDID_READY))
				return -EIO;
		}

		regmap_write(adv7511->regmap, ADV7511_REG_INT(0),
			     ADV7511_INT0_EDID_READY | ADV7511_INT1_DDC_ERROR);

		/* Break this apart, hopefully more I2C controllers will
		 * support 64 byte transfers than 256 byte transfers
		 */

		xfer[0].addr = adv7511->i2c_edid->addr;
		xfer[0].flags = 0;
		xfer[0].len = 1;
		xfer[0].buf = &offset;
		xfer[1].addr = adv7511->i2c_edid->addr;
		xfer[1].flags = I2C_M_RD;
		xfer[1].len = 64;
		xfer[1].buf = adv7511->edid_buf;

		offset = 0;

		for (i = 0; i < 4; ++i) {
			ret = i2c_transfer(adv7511->i2c_edid->adapter, xfer,
					   ARRAY_SIZE(xfer));
			if (ret < 0)
				return ret;
			else if (ret != 2)
				return -EIO;

			xfer[1].buf += 64;
			offset += 64;
		}

		adv7511->current_edid_segment = block / 2;
	}

	if (block % 2 == 0)
		memcpy(buf, adv7511->edid_buf, len);
	else
		memcpy(buf, adv7511->edid_buf + 128, len);

	return 0;
}

/**
 * adv7511_set_default_config - Set adv7511 with default config
 * @encoder: drm encoder
 * @connector: drm connector
 * @edid: edid from the connected display device
 *
 * Configure adv7511 with default values. This configuration is overwritten
 * when client driver re-configures the adv7511 using adv7511_set_config().
 */
static void adv7511_set_default_config(struct drm_encoder *encoder,
				       struct drm_connector *connector,
				       struct edid *edid)
{
	struct adv7511 *adv7511 = encoder_to_adv7511(encoder);
	struct adv7511_video_config config;

	if (edid)
		config.hdmi_mode = drm_detect_hdmi_monitor(edid);
	else
		config.hdmi_mode = false;

	hdmi_avi_infoframe_init(&config.avi_infoframe);

	config.avi_infoframe.scan_mode = HDMI_SCAN_MODE_UNDERSCAN;

	if (adv7511->rgb) {
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

	adv7511_set_config(encoder, &config);
}

static int adv7511_get_modes(struct drm_encoder *encoder,
			     struct drm_connector *connector)
{
	struct adv7511 *adv7511 = encoder_to_adv7511(encoder);
	struct edid *edid;
	unsigned int count;

	/* Reading the EDID only works if the device is powered */
	if (adv7511->dpms_mode != DRM_MODE_DPMS_ON) {
		regmap_write(adv7511->regmap, ADV7511_REG_INT(0),
			     ADV7511_INT0_EDID_READY | ADV7511_INT1_DDC_ERROR);
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER,
				   ADV7511_POWER_POWER_DOWN, 0);
		adv7511->current_edid_segment = -1;
	}

	edid = drm_do_get_edid(connector, adv7511_get_edid_block, encoder);

	if (adv7511->dpms_mode != DRM_MODE_DPMS_ON)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER,
				   ADV7511_POWER_POWER_DOWN,
				   ADV7511_POWER_POWER_DOWN);

	kfree(adv7511->edid);
	adv7511->edid = edid;
	if (!edid)
		return 0;

	drm_mode_connector_update_edid_property(connector, edid);
	count = drm_add_edid_modes(connector, edid);

	adv7511_set_default_config(encoder, connector, edid);

	return count;
}

struct edid *adv7511_get_edid(struct drm_encoder *encoder)
{
	struct adv7511 *adv7511 = encoder_to_adv7511(encoder);

	if (!adv7511->edid)
		return NULL;

	return kmemdup(adv7511->edid, sizeof(*adv7511->edid) +
		       adv7511->edid->extensions * 128, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(adv7511_get_edid);

static void adv7511_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct adv7511 *adv7511 = encoder_to_adv7511(encoder);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		adv7511->current_edid_segment = -1;

		regmap_write(adv7511->regmap, ADV7511_REG_INT(0),
			     ADV7511_INT0_EDID_READY | ADV7511_INT1_DDC_ERROR);
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER,
				   ADV7511_POWER_POWER_DOWN, 0);
		/*
		 * Per spec it is allowed to pulse the HDP signal to indicate
		 * that the EDID information has changed. Some monitors do this
		 * when they wakeup from standby or are enabled. When the HDP
		 * goes low the adv7511 is reset and the outputs are disabled
		 * which might cause the monitor to go to standby again. To
		 * avoid this we ignore the HDP pin for the first few seconds
		 * after enabeling the output.
		 */
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER2,
				   ADV7511_REG_POWER2_HDP_SRC_MASK,
				   ADV7511_REG_POWER2_HDP_SRC_NONE);
		/* Most of the registers are reset during power down or
		 * when HPD is low
		 */
		regcache_sync(adv7511->regmap);
		break;
	default:
		/* TODO: setup additional power down modes */
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER,
				   ADV7511_POWER_POWER_DOWN,
				   ADV7511_POWER_POWER_DOWN);
		regcache_mark_dirty(adv7511->regmap);
		break;
	}

	adv7511->dpms_mode = mode;
}

static enum drm_connector_status
adv7511_encoder_detect(struct drm_encoder *encoder,
		       struct drm_connector *connector)
{
	struct adv7511 *adv7511 = encoder_to_adv7511(encoder);
	enum drm_connector_status status;
	unsigned int val;
	bool hpd;
	int ret;

	ret = regmap_read(adv7511->regmap, ADV7511_REG_STATUS, &val);
	if (ret < 0)
		return connector_status_disconnected;

	if (val & ADV7511_STATUS_HPD)
		status = connector_status_connected;
	else
		status = connector_status_disconnected;

	hpd = adv7511_hpd(adv7511);

	/* The chip resets itself when the cable is disconnected, so in case
	 * there is a pending HPD interrupt and the cable is connected there was
	 * at least one transition from disconnected to connected and the chip
	 * has to be reinitialized. */
	if (status == connector_status_connected && hpd &&
	    adv7511->dpms_mode == DRM_MODE_DPMS_ON) {
		regcache_mark_dirty(adv7511->regmap);
		adv7511_encoder_dpms(encoder, adv7511->dpms_mode);
		adv7511_get_modes(encoder, connector);
		if (adv7511->status == connector_status_connected)
			status = connector_status_disconnected;
	} else {
		/* Renable HDP sensing */
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER2,
				   ADV7511_REG_POWER2_HDP_SRC_MASK,
				   ADV7511_REG_POWER2_HDP_SRC_BOTH);
	}

	adv7511->status = status;
	return status;
}

static int adv7511_encoder_mode_valid(struct drm_encoder *encoder,
				      struct drm_display_mode *mode)
{
	if (mode->clock > MAX_CLOCK)
		return MODE_CLOCK_HIGH;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	return MODE_OK;
}

static void adv7511_encoder_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adj_mode)
{
	struct adv7511 *adv7511 = encoder_to_adv7511(encoder);
	unsigned int low_refresh_rate;
	unsigned int hsync_polarity = 0;
	unsigned int vsync_polarity = 0;

	if (adv7511->embedded_sync) {
		unsigned int hsync_offset, hsync_len;
		unsigned int vsync_offset, vsync_len;

		hsync_offset = adj_mode->crtc_hsync_start -
			       adj_mode->crtc_hdisplay;
		vsync_offset = adj_mode->crtc_vsync_start -
			       adj_mode->crtc_vdisplay;
		hsync_len = adj_mode->crtc_hsync_end -
			    adj_mode->crtc_hsync_start;
		vsync_len = adj_mode->crtc_vsync_end -
			    adj_mode->crtc_vsync_start;

		/* The hardware vsync generator has a off-by-one bug */
		vsync_offset += 1;

		regmap_write(adv7511->regmap, ADV7511_REG_HSYNC_PLACEMENT_MSB,
			     ((hsync_offset >> 10) & 0x7) << 5);
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(0),
			     (hsync_offset >> 2) & 0xff);
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(1),
			     ((hsync_offset & 0x3) << 6) |
			     ((hsync_len >> 4) & 0x3f));
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(2),
			     ((hsync_len & 0xf) << 4) |
			     ((vsync_offset >> 6) & 0xf));
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(3),
			     ((vsync_offset & 0x3f) << 2) |
			     ((vsync_len >> 8) & 0x3));
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(4),
			     vsync_len & 0xff);

		hsync_polarity = !(adj_mode->flags & DRM_MODE_FLAG_PHSYNC);
		vsync_polarity = !(adj_mode->flags & DRM_MODE_FLAG_PVSYNC);
	} else {
		enum adv7511_sync_polarity mode_hsync_polarity;
		enum adv7511_sync_polarity mode_vsync_polarity;

		/**
		 * If the input signal is always low or always high we want to
		 * invert or let it passthrough depending on the polarity of the
		 * current mode.
		 **/
		if (adj_mode->flags & DRM_MODE_FLAG_NHSYNC)
			mode_hsync_polarity = ADV7511_SYNC_POLARITY_LOW;
		else
			mode_hsync_polarity = ADV7511_SYNC_POLARITY_HIGH;

		if (adj_mode->flags & DRM_MODE_FLAG_NVSYNC)
			mode_vsync_polarity = ADV7511_SYNC_POLARITY_LOW;
		else
			mode_vsync_polarity = ADV7511_SYNC_POLARITY_HIGH;

		if (adv7511->hsync_polarity != mode_hsync_polarity &&
		    adv7511->hsync_polarity !=
		    ADV7511_SYNC_POLARITY_PASSTHROUGH)
			hsync_polarity = 1;

		if (adv7511->vsync_polarity != mode_vsync_polarity &&
		    adv7511->vsync_polarity !=
		    ADV7511_SYNC_POLARITY_PASSTHROUGH)
			vsync_polarity = 1;
	}

	if (mode->vrefresh <= 24000)
		low_refresh_rate = ADV7511_LOW_REFRESH_RATE_24HZ;
	else if (mode->vrefresh <= 25000)
		low_refresh_rate = ADV7511_LOW_REFRESH_RATE_25HZ;
	else if (mode->vrefresh <= 30000)
		low_refresh_rate = ADV7511_LOW_REFRESH_RATE_30HZ;
	else
		low_refresh_rate = ADV7511_LOW_REFRESH_RATE_NONE;

	regmap_update_bits(adv7511->regmap, 0xfb,
		0x6, low_refresh_rate << 1);
	regmap_update_bits(adv7511->regmap, 0x17,
		0x60, (vsync_polarity << 6) | (hsync_polarity << 5));

	adv7511->f_tmds = mode->clock;
}

static struct drm_encoder_slave_funcs adv7511_encoder_funcs = {
	.set_config = adv7511_set_config,
	.dpms = adv7511_encoder_dpms,
	/* .destroy = adv7511_encoder_destroy,*/
	.mode_valid = adv7511_encoder_mode_valid,
	.mode_set = adv7511_encoder_mode_set,
	.detect = adv7511_encoder_detect,
	.get_modes = adv7511_get_modes,
};

static const struct regmap_config adv7511_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0xff,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults_raw = adv7511_register_defaults,
	.num_reg_defaults_raw = ARRAY_SIZE(adv7511_register_defaults),

	.volatile_reg = adv7511_register_volatile,
};

/*
	adi,input-id -
		0x00:
		0x01:
		0x02:
		0x03:
		0x04:
		0x05:
	adi,sync-pulse - Selects the sync pulse
		0x00: Use the DE signal as sync pulse
		0x01: Use the HSYNC signal as sync pulse
		0x02: Use the VSYNC signal as sync pulse
		0x03: No external sync pulse
	adi,bit-justification -
		0x00: Evently
		0x01: Right
		0x02: Left
	adi,up-conversion -
		0x00: zero-order up conversion
		0x01: first-order up conversion
	adi,timing-generation-sequence -
		0x00: Sync adjustment first, then DE generation
		0x01: DE generation first then sync adjustment
	adi,vsync-polarity - Polarity of the vsync signal
		0x00: Passthrough
		0x01: Active low
		0x02: Active high
	adi,hsync-polarity - Polarity of the hsync signal
		0x00: Passthrough
		0x01: Active low
		0x02: Active high
	adi,reverse-bitorder - If set the bitorder is reveresed
	adi,tmds-clock-inversion - If set use tdms clock inversion
	adi,clock-delay - Clock delay for the video data clock
		0x00: -1200 ps
		0x01:  -800 ps
		0x02:  -400 ps
		0x03: no dealy
		0x04:   400 ps
		0x05:   800 ps
		0x06:  1200 ps
		0x07:  1600 ps
	adi,input-style - Specifies the input style used
		0x02: Use input style 1
		0x01: Use input style 2
		0x03: Use Input style 3
	adi,input-color-depth - Selects the input format color depth
		0x03: 8-bit per channel
		0x01: 10-bit per channel
		0x02: 12-bit per channel
*/

static int adv7511_parse_dt(struct device_node *np,
			    struct adv7511_link_config *config)
{
	int ret;

	ret = of_property_read_u32(np, "adi,input-id", &config->id);
	if (ret < 0)
		return ret;

	config->sync_pulse = ADV7511_INPUT_SYNC_PULSE_NONE;
	of_property_read_u32(np, "adi,sync-pulse", &config->sync_pulse);

	ret = of_property_read_u32(np, "adi,bit-justification",
				   &config->bit_justification);
	if (ret < 0)
		return ret;

	config->up_conversion = ADV7511_UP_CONVERSION_ZERO_ORDER;
	of_property_read_u32(np, "adi,up-conversion", &config->up_conversion);

	ret = of_property_read_u32(np, "adi,timing-generation-sequence",
				   &config->timing_gen_seq);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(np, "adi,vsync-polarity",
				   &config->vsync_polarity);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(np, "adi,hsync-polarity",
				   &config->hsync_polarity);
	if (ret < 0)
		return ret;

	config->reverse_bitorder = of_property_read_bool(np,
		"adi,reverse-bitorder");
	config->tmds_clock_inversion = of_property_read_bool(np,
		"adi,tmds-clock-inversion");

	ret = of_property_read_u32(np, "adi,clock-delay",
				   &config->clock_delay);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "adi,input-style",
				   &config->input_style);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "adi,input-color-depth",
				   &config->input_color_depth);
	if (ret)
		return ret;

	config->gpio_pd = of_get_gpio(np, 0);
	if (config->gpio_pd == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	config->rgb = of_property_read_bool(np, "adi,is-rgb");

	return 0;
}

static const int edid_i2c_addr = 0x7e;
static const int packet_i2c_addr = 0x70;
static const int cec_i2c_addr = 0x78;

static int adv7511_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct adv7511_link_config link_config;
	struct adv7511 *adv7511;
	struct device *dev = &i2c->dev;
	unsigned int val;
	int ret;

	if (dev->of_node) {
		ret = adv7511_parse_dt(dev->of_node, &link_config);
		if (ret)
			return ret;
	} else {
		if (!dev->platform_data)
			return -EINVAL;
		link_config = *(struct adv7511_link_config *)dev->platform_data;
	}

	adv7511 = devm_kzalloc(dev, sizeof(*adv7511), GFP_KERNEL);
	if (!adv7511)
		return -ENOMEM;

	adv7511->dpms_mode = DRM_MODE_DPMS_OFF;
	adv7511->status = connector_status_disconnected;

	adv7511->gpio_pd = link_config.gpio_pd;

	if (gpio_is_valid(adv7511->gpio_pd)) {
		ret = devm_gpio_request_one(dev, adv7511->gpio_pd,
					    GPIOF_OUT_INIT_HIGH, "PD");
		if (ret)
			return ret;
		mdelay(5);
		gpio_set_value_cansleep(adv7511->gpio_pd, 0);
	}

	adv7511->regmap = devm_regmap_init_i2c(i2c, &adv7511_regmap_config);
	if (IS_ERR(adv7511->regmap))
		return PTR_ERR(adv7511->regmap);

	ret = regmap_read(adv7511->regmap, ADV7511_REG_CHIP_REVISION, &val);
	if (ret)
		return ret;
	dev_dbg(dev, "Rev. %d\n", val);

	ret = regmap_register_patch(adv7511->regmap, adv7511_fixed_registers,
				    ARRAY_SIZE(adv7511_fixed_registers));
	if (ret)
		return ret;

	regmap_write(adv7511->regmap, ADV7511_REG_EDID_I2C_ADDR, edid_i2c_addr);
	regmap_write(adv7511->regmap, ADV7511_REG_PACKET_I2C_ADDR,
		     packet_i2c_addr);
	regmap_write(adv7511->regmap, ADV7511_REG_CEC_I2C_ADDR, cec_i2c_addr);
	adv7511_packet_disable(adv7511, 0xffff);

	adv7511->i2c_main = i2c;
	adv7511->i2c_edid = i2c_new_dummy(i2c->adapter, edid_i2c_addr >> 1);
	adv7511->i2c_packet = i2c_new_dummy(i2c->adapter, packet_i2c_addr >> 1);
	if (!adv7511->i2c_edid)
		return -ENOMEM;

	if (i2c->irq) {
		ret = request_threaded_irq(i2c->irq, NULL, adv7511_irq_handler,
					   IRQF_ONESHOT, dev_name(dev),
					   adv7511);
		if (ret)
			goto err_i2c_unregister_device;

		init_waitqueue_head(&adv7511->wq);
	}

	/* CEC is unused for now */
	regmap_write(adv7511->regmap, ADV7511_REG_CEC_CTRL,
		     ADV7511_CEC_CTRL_POWER_DOWN);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER,
			   ADV7511_POWER_POWER_DOWN, ADV7511_POWER_POWER_DOWN);

	adv7511->dpms_mode = DRM_MODE_DPMS_OFF;
	adv7511->current_edid_segment = -1;

	i2c_set_clientdata(i2c, adv7511);
	adv7511_audio_init(dev);

	adv7511_set_link_config(adv7511, &link_config);

	regcache_mark_dirty(adv7511->regmap);

	return 0;

err_i2c_unregister_device:
	i2c_unregister_device(adv7511->i2c_edid);

	return ret;
}

static int adv7511_remove(struct i2c_client *i2c)
{
	struct adv7511 *adv7511 = i2c_get_clientdata(i2c);

	i2c_unregister_device(adv7511->i2c_edid);

	if (i2c->irq)
		free_irq(i2c->irq, adv7511);
	kfree(adv7511->edid);

	return 0;
}

static int adv7511_encoder_init(struct i2c_client *i2c, struct drm_device *dev,
				struct drm_encoder_slave *encoder)
{

	struct adv7511 *adv7511 = i2c_get_clientdata(i2c);

	encoder->slave_priv = adv7511;
	encoder->slave_funcs = &adv7511_encoder_funcs;

	adv7511->encoder = &encoder->base;

	return 0;
}

static const struct i2c_device_id adv7511_ids[] = {
	{ "adv7511", 0 },
	{}
};

static struct drm_i2c_encoder_driver adv7511_driver = {
	.i2c_driver = {
		.driver = {
			.name = "adv7511",
		},
		.id_table = adv7511_ids,
		.probe = adv7511_probe,
		.remove = adv7511_remove,
	},

	.encoder_init = adv7511_encoder_init,
};

static int adv7511_init(void)
{
	return drm_i2c_encoder_register(THIS_MODULE, &adv7511_driver);
}
module_init(adv7511_init);

static void adv7511_exit(void)
{
	drm_i2c_encoder_unregister(&adv7511_driver);
}
module_exit(adv7511_exit);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ADV7511 HDMI transmitter driver");
MODULE_LICENSE("GPL");

/*
 * Analog Devices ADV7511 HDMI Transmitter Device Driver
 *
 * Copyright 2012 Cisco Systems, Inc. and/or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/adv7511.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

MODULE_DESCRIPTION("Analog Devices ADV7511 HDMI Transmitter Device Driver");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");

#define MASK_ADV7511_EDID_RDY_INT   0x04
#define MASK_ADV7511_MSEN_INT       0x40
#define MASK_ADV7511_HPD_INT        0x80

#define MASK_ADV7511_HPD_DETECT     0x40
#define MASK_ADV7511_MSEN_DETECT    0x20
#define MASK_ADV7511_EDID_RDY       0x10

#define EDID_MAX_RETRIES (8)
#define EDID_DELAY 10
#define EDID_MAX_SEGM 8

/*
**********************************************************************
*
*  Arrays with configuration parameters for the ADV7511
*
**********************************************************************
*/

struct i2c_reg_value {
	unsigned char reg;
	unsigned char value;
};

struct adv7511_state_edid {
	/* total number of blocks */
	u32 blocks;
	/* Number of segments read */
	u32 segments;
	uint8_t data[EDID_MAX_SEGM * 256];
	/* Number of EDID read retries left */
	unsigned read_retries;
};

#ifdef CONFIG_OF
struct adv7511_in_params {
	uint8_t input_id;
	uint8_t input_style;
	uint8_t input_color_depth;
	uint8_t bit_justification;
	uint8_t hsync_polarity;
	uint8_t vsync_polarity;
	uint8_t clock_delay;
};

struct adv7511_csc_coeff {
	uint16_t a1;
	uint16_t a2;
	uint16_t a3;
	uint16_t a4;
	uint16_t b1;
	uint16_t b2;
	uint16_t b3;
	uint16_t b4;
	uint16_t c1;
	uint16_t c2;
	uint16_t c3;
	uint16_t c4;
};

struct adv7511_out_params {
	bool hdmi_mode;
	uint8_t output_format;
	uint8_t output_color_space;
	uint8_t up_conversion;
	uint8_t csc_enable;
	uint8_t csc_scaling_factor;
	struct adv7511_csc_coeff csc_coeff;
};
#endif

struct adv7511_config {
#ifdef CONFIG_OF
	struct adv7511_in_params in_params;
	struct adv7511_out_params out_params;
#endif
	bool embedded_sync;
	bool loaded;
};

struct adv7511_state {
	struct adv7511_config cfg;
	struct adv7511_platform_data pdata;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler hdl;
	int chip_revision;
	uint8_t edid_addr;
	/* Is the adv7511 powered on? */
	bool power_on;
	/* Did we receive hotplug and rx-sense signals? */
	bool have_monitor;
	/* timings from s_dv_timings */
	struct v4l2_dv_timings dv_timings;
	/* controls */
	struct v4l2_ctrl *hdmi_mode_ctrl;
	struct v4l2_ctrl *audio_sample_freq_ctrl;
	struct v4l2_ctrl *audio_word_length_ctrl;
	struct v4l2_ctrl *audio_channel_count_ctrl;
	struct v4l2_ctrl *audio_channel_map_ctrl;
	struct v4l2_ctrl *audio_i2s_format_ctrl;
	struct v4l2_ctrl *hotplug_ctrl;
	struct v4l2_ctrl *rx_sense_ctrl;
	struct v4l2_ctrl *have_edid0_ctrl;
	struct v4l2_ctrl *rgb_quantization_range_ctrl;
	struct i2c_client *edid_i2c_client;
	struct adv7511_state_edid edid;
	/* Running counter of the number of detected EDIDs (for debugging) */
	unsigned edid_detect_counter;
	struct workqueue_struct *work_queue;
	struct delayed_work edid_handler; /* work entry */
};

static void adv7511_check_monitor_present_status(struct v4l2_subdev *sd);
static bool adv7511_check_edid_status(struct v4l2_subdev *sd);
static void adv7511_setup(struct v4l2_subdev *sd);
static int adv7511_s_i2s_clock_freq(struct v4l2_subdev *sd, u32 freq);
static int adv7511_s_clock_freq(struct v4l2_subdev *sd, u32 freq);
#if 0
static int adv7511_s_audio_word_length(struct v4l2_subdev *sd, int length);
static int adv7511_s_audio_channel_count(struct v4l2_subdev *sd, int count);
static int adv7511_s_audio_channel_map(struct v4l2_subdev *sd, int map);
static int adv7511_s_audio_i2s_format(struct v4l2_subdev *sd, int format);
#endif

static inline struct adv7511_state *get_adv7511_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7511_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct adv7511_state, hdl)->sd;
}

/* ---------------------------- I2C ---------------------------- */

static int adv7511_rd(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static int adv7511_wr(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	int i;

	for (i = 0; i < 3; i++) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (ret == 0)
			return 0;
	}
	v4l2_err(sd, "I2C Write Problem\n");
	return ret;
}

/* To set specific bits in the register, a clear-mask is given (to be AND-ed),
   and then the value-mask (to be OR-ed). */
static inline void adv7511_wr_and_or(struct v4l2_subdev *sd,
	u8 reg, uint8_t clr_mask, uint8_t val_mask)
{
	adv7511_wr(sd, reg, (adv7511_rd(sd, reg) & clr_mask) | val_mask);
}

static inline void adv7511_edid_rd(struct v4l2_subdev *sd,
	uint16_t len, uint8_t *buf)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	int i;

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	for (i = 0; i < len; i++)
		buf[i] = i2c_smbus_read_byte_data(state->edid_i2c_client, i);
}

static inline bool adv7511_have_hotplug(struct v4l2_subdev *sd)
{
	return adv7511_rd(sd, 0x42) & MASK_ADV7511_HPD_DETECT;
}

static inline bool adv7511_have_rx_sense(struct v4l2_subdev *sd)
{
	return adv7511_rd(sd, 0x42) & MASK_ADV7511_MSEN_DETECT;
}

static void adv7511_csc_conversion_mode(struct v4l2_subdev *sd, uint8_t mode)
{
	adv7511_wr_and_or(sd, 0x18, 0x9f, (mode & 0x3)<<5);
}

static void adv7511_csc_coeff(struct v4l2_subdev *sd,
			      u16 A1, u16 A2, u16 A3, u16 A4,
			      u16 B1, u16 B2, u16 B3, u16 B4,
			      u16 C1, u16 C2, u16 C3, u16 C4)
{
	/* A */
	adv7511_wr_and_or(sd, 0x18, 0xe0, A1>>8);
	adv7511_wr(sd, 0x19, A1);
	adv7511_wr_and_or(sd, 0x1A, 0xe0, A2>>8);
	adv7511_wr(sd, 0x1B, A2);
	adv7511_wr_and_or(sd, 0x1c, 0xe0, A3>>8);
	adv7511_wr(sd, 0x1d, A3);
	adv7511_wr_and_or(sd, 0x1e, 0xe0, A4>>8);
	adv7511_wr(sd, 0x1f, A4);

	/* B */
	adv7511_wr_and_or(sd, 0x20, 0xe0, B1>>8);
	adv7511_wr(sd, 0x21, B1);
	adv7511_wr_and_or(sd, 0x22, 0xe0, B2>>8);
	adv7511_wr(sd, 0x23, B2);
	adv7511_wr_and_or(sd, 0x24, 0xe0, B3>>8);
	adv7511_wr(sd, 0x25, B3);
	adv7511_wr_and_or(sd, 0x26, 0xe0, B4>>8);
	adv7511_wr(sd, 0x27, B4);

	/* C */
	adv7511_wr_and_or(sd, 0x28, 0xe0, C1>>8);
	adv7511_wr(sd, 0x29, C1);
	adv7511_wr_and_or(sd, 0x2A, 0xe0, C2>>8);
	adv7511_wr(sd, 0x2B, C2);
	adv7511_wr_and_or(sd, 0x2C, 0xe0, C3>>8);
	adv7511_wr(sd, 0x2D, C3);
	adv7511_wr_and_or(sd, 0x2E, 0xe0, C4>>8);
	adv7511_wr(sd, 0x2F, C4);
}
#if 0
static void adv7511_csc_rgb_full2limit(struct v4l2_subdev *sd, bool enable)
{
	if (enable) {
		uint8_t csc_mode = 0;
		adv7511_csc_conversion_mode(sd, csc_mode);
		adv7511_csc_coeff(sd,
				  4096-564, 0, 0, 256,
				  0, 4096-564, 0, 256,
				  0, 0, 4096-564, 256);
		/* enable CSC */
		adv7511_wr_and_or(sd, 0x18, 0x7f, 0x80);
		/* AVI infoframe: Limited range RGB (16-235) */
		adv7511_wr_and_or(sd, 0x57, 0xf3, 0x04);
	} else {
		/* disable CSC */
		adv7511_wr_and_or(sd, 0x18, 0x7f, 0x0);
		/* AVI infoframe: Full range RGB (0-255) */
		adv7511_wr_and_or(sd, 0x57, 0xf3, 0x08);
	}
}
#endif
static void adv7511_set_IT_content_AVI_InfoFrame(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	if (state->dv_timings.bt.standards & V4L2_DV_BT_STD_CEA861) {
		/* CEA format, not IT  */
		adv7511_wr_and_or(sd, 0x57, 0x7f, 0x00);
	} else {
		/* IT format */
		adv7511_wr_and_or(sd, 0x57, 0x7f, 0x80);
	}
}
#if 0
static int adv7511_set_rgb_quantization_mode(struct v4l2_subdev *sd,
	struct v4l2_ctrl *ctrl)
{
	switch (ctrl->val) {
	default:
		return -EINVAL;
		break;
	case V4L2_DV_RANGE_AUTO: {
		/* automatic */
		struct adv7511_state *state = get_adv7511_state(sd);

		if (state->dv_timings.bt.standards & V4L2_DV_BT_STD_CEA861) {
			/* cea format, RGB limited range (16-235) */
			adv7511_csc_rgb_full2limit(sd, true);
		} else {
			/* not cea format, RGB full range (0-255) */
			adv7511_csc_rgb_full2limit(sd, false);
		}
	}
		break;
	case V4L2_DV_RANGE_LIMITED:
		/* RGB limited range (16-235) */
		adv7511_csc_rgb_full2limit(sd, true);
		break;
	case V4L2_DV_RANGE_FULL:
		/* RGB full range (0-255) */
		adv7511_csc_rgb_full2limit(sd, false);
		break;
	}

	return 0;
}
#endif
/* ---------------------------- CTRL OPS ---------------------------- */

static int adv7511_s_ctrl(struct v4l2_ctrl *ctrl)
{
	return 0;
#if 0
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct adv7511_state *state = get_adv7511_state(sd);

	v4l2_dbg(1, debug, sd, "%s: ctrl id: %d, ctrl->val %d\n",
		__func__, ctrl->id, ctrl->val);

	if (state->hdmi_mode_ctrl == ctrl) {
		/* Set HDMI or DVI-D */
		adv7511_wr_and_or(sd, 0xaf, 0xfd,
			ctrl->val == V4L2_DV_TX_MODE_HDMI ? 0x02 : 0x00);
		return 0;
	}
	if (state->audio_sample_freq_ctrl == ctrl) {
		adv7511_s_i2s_clock_freq(sd, ctrl->val);
		return adv7511_s_clock_freq(sd, ctrl->val);
	}
	if (state->audio_word_length_ctrl == ctrl)
		return adv7511_s_audio_word_length(sd, ctrl->val);
	if (state->audio_channel_count_ctrl == ctrl)
		return adv7511_s_audio_channel_count(sd, ctrl->val);
	if (state->audio_channel_map_ctrl == ctrl)
		return adv7511_s_audio_channel_map(sd, ctrl->val);
	if (state->audio_i2s_format_ctrl == ctrl)
		return adv7511_s_audio_i2s_format(sd, ctrl->val);
	if (state->hotplug_ctrl == ctrl)
		return 0;
	if (state->rx_sense_ctrl == ctrl)
		return 0;
	if (state->have_edid0_ctrl == ctrl)
		return 0;
	if (state->rgb_quantization_range_ctrl == ctrl)
		return adv7511_set_rgb_quantization_mode(sd, ctrl);

	return -EINVAL;
#endif
}

static const struct v4l2_ctrl_ops adv7511_ctrl_ops = {
	.s_ctrl = adv7511_s_ctrl,
};

#if 0
static const char * const hdmi_dvi_mode_menu[] = {
	"DVI-D",
	"HDMI",
	NULL
};

static const struct v4l2_ctrl_config adv7511_ctrl_hdmi_mode = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_MODE,
	.name = "hdmi dvi mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = V4L2_DV_TX_MODE_DVI_D,
	.max = V4L2_DV_TX_MODE_HDMI,
	.step = 0,
	.def = V4L2_DV_TX_MODE_DVI_D,
	.qmenu = hdmi_dvi_mode_menu,
};

static const struct v4l2_ctrl_config adv7511_ctrl_audio_sample_freq = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_AUDIO_SAMPLE_FREQ,
	.name = "audio sample freq",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 32000,
	.max = 192000,
	.step = 100,
	.def = 48000,
};

static const struct v4l2_ctrl_config adv7511_ctrl_audio_word_length = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_AUDIO_WORD_LEN,
	.name = "audio word length",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 16,
	.max = 24,
	.step = 1,
	.def = 16,
};

static const struct v4l2_ctrl_config adv7511_ctrl_audio_channel_count = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_AUDIO_CH_COUNT,
	.name = "audio channel count",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 8,
	.step = 1,
	.def = 2,
};

static const struct v4l2_ctrl_config adv7511_ctrl_audio_channel_map = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_AUDIO_CH_MAP,
	.name = "audio channel mapping",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 0x1f,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config adv7511_ctrl_audio_i2s_format = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_AUDIO_I2S_FORMAT,
	.name = "audio i2s format",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 2,
	.step = 1,
	.def = 0,
};


static const struct v4l2_ctrl_config adv7511_ctrl_hotplug = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_HOTPLUG,
	.name = "hotplug",
	.type = V4L2_CTRL_TYPE_BITMASK,
	.min = 0,
	.max = 1,
	.step = 0,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config adv7511_ctrl_rx_sense = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_RXSENSE,
	.name = "rx sense",
	.type = V4L2_CTRL_TYPE_BITMASK,
	.min = 0,
	.max = 1,
	.step = 0,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config adv7511_ctrl_edid_segment0 = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_EDID_PRESENT,
	.name = "edid present",
	.type = V4L2_CTRL_TYPE_BITMASK,
	.min = 0,
	.max = 1,
	.step = 0,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const char * const rgb_quantization_range_menu[] = {
	"Automatic",
	"RGB limited range (16-235)",
	"RGB full range (0-255)",
	NULL
};

static const struct v4l2_ctrl_config adv7511_ctrl_rgb_quantization_range = {
	.ops = &adv7511_ctrl_ops,
	.id = V4L2_CID_DV_TX_RGB_RANGE,
	.name = "RGB quantization range",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = V4L2_DV_RANGE_AUTO,
	.max = V4L2_DV_RANGE_FULL,
	.step = 0,
	.def = V4L2_DV_RANGE_AUTO,
	.qmenu = rgb_quantization_range_menu,
};
#endif
/* ---------------------------- CORE OPS ---------------------------- */

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int adv7511_g_register(struct v4l2_subdev *sd,
	struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	reg->val = adv7511_rd(sd, reg->reg & 0xff);
	reg->size = 1;

	return 0;
}

static int adv7511_s_register(struct v4l2_subdev *sd,
	struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	adv7511_wr(sd, reg->reg & 0xff, reg->val & 0xff);

	return 0;
}
#endif

static int adv7511_g_chip_ident(struct v4l2_subdev *sd,
	struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_ADV7511, 0);
}

static int adv7511_log_status(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct adv7511_state_edid *edid = &state->edid;

	static const char * const states[] = {
		"in reset",
		"reading EDID",
		"idle",
		"initializing HDCP",
		"HDCP enabled",
		"initializing HDCP repeater",
		"6", "7", "8", "9", "A", "B", "C", "D", "E", "F"
	};
	static const char * const errors[] = {
		"no error",
		"bad receiver BKSV",
		"Ri mismatch",
		"Pj mismatch",
		"i2c error",
		"timed out",
		"max repeater cascade exceeded",
		"hash check failed",
		"too many devices",
		"9", "A", "B", "C", "D", "E", "F"
	};

	v4l2_info(sd, "power %s\n", state->power_on ? "on" : "off");
	v4l2_info(sd, "%s hotplug, %s Rx Sense, %s EDID (%d block(s))\n",
		(adv7511_rd(sd, 0x42) & MASK_ADV7511_HPD_DETECT) ?
			"detected" : "no",
		(adv7511_rd(sd, 0x42) & MASK_ADV7511_MSEN_DETECT) ?
			"detected" : "no",
		edid->segments ? "found" : "no",
		edid->blocks);
	if (state->have_monitor) {
		v4l2_info(sd, "%s output %s\n",
			(adv7511_rd(sd, 0xaf) & 0x02) ?
			"HDMI" : "DVI-D",
			(adv7511_rd(sd, 0xa1) & 0x3c) ?
			"disabled" : "enabled");
	}
	v4l2_info(sd, "state: %s, error: %s, detect count: %u, msk/irq: %02x/%02x\n",
		states[adv7511_rd(sd, 0xc8) & 0xf],
		errors[adv7511_rd(sd, 0xc8) >> 4],
		state->edid_detect_counter,
		adv7511_rd(sd, 0x94), adv7511_rd(sd, 0x96));
	v4l2_info(sd, "RGB quantization: %s range\n",
		adv7511_rd(sd, 0x18) & 0x80 ? "limited" : "full");
	if (state->dv_timings.type == V4L2_DV_BT_656_1120) {
		struct v4l2_bt_timings *bt = bt = &state->dv_timings.bt;
		u32 frame_width =
			bt->width + bt->hfrontporch +
			bt->hsync + bt->hbackporch;
		u32 frame_height =
			bt->height + bt->vfrontporch +
			bt->vsync + bt->vbackporch;
		v4l2_info(sd, "timings: %dx%d%s%d (%dx%d). Pix freq. = %d Hz. Polarities = 0x%x\n",
			bt->width, bt->height,
			bt->interlaced ? "i" : "p",
			(frame_height*frame_width) > 0 ?
				(int)bt->pixelclock /
				(frame_height*frame_width) : 0,
			frame_width, frame_height,
			(int)bt->pixelclock, bt->polarities);
	} else {
		v4l2_info(sd, "no timings set\n");
	}
	v4l2_info(sd, "edid_i2_addr: 0x%x\n", state->edid_addr);

	return 0;
}

/* Power up/down adv7511 */
static int adv7511_s_power(struct v4l2_subdev *sd, int on)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	const int retries = 20;
	int i;

	v4l2_dbg(1, debug, sd,
		"%s: power %s\n", __func__, on ? "on" : "off");

	state->power_on = on;

	if (!on) {
		/* Power down */
		adv7511_wr_and_or(sd, 0x41, 0xbf, 0x40);
		return true;
	}

	/* Power up */
	/* The adv7511 does not always come up immediately.
	   Retry multiple times. */
	for (i = 0; i < retries; i++) {
		adv7511_wr_and_or(sd, 0x41, 0xbf, 0x0);
		if ((adv7511_rd(sd, 0x41) & 0x40) == 0)
			break;
		adv7511_wr_and_or(sd, 0x41, 0xbf, 0x40);
		msleep(20); /* TODO Or msleep_interruptible */
	}
	if (i == retries) {
		v4l2_dbg(1, debug, sd,
			"%s: failed to powerup the adv7511!\n", __func__);
		adv7511_s_power(sd, 0);
		return false;
	}
	if (i > 1)
		v4l2_dbg(1, debug, sd,
			"%s: needed %d retries to powerup the adv7511\n",
			__func__, i);

	/* Reserved registers that must be set */
	adv7511_wr(sd, 0x98, 0x03);
	adv7511_wr_and_or(sd, 0x9a, 0xfe, 0x70);
	adv7511_wr(sd, 0x9c, 0x30);
	adv7511_wr_and_or(sd, 0x9d, 0xfc, 0x61);
	adv7511_wr(sd, 0xa2, 0xa4);
	adv7511_wr(sd, 0xa3, 0xa4);
	adv7511_wr(sd, 0xde, 0x9c);
	adv7511_wr(sd, 0xe0, 0xd0);
	adv7511_wr(sd, 0xf9, 0x00);

	adv7511_wr(sd, 0x43, state->edid_addr);

	/* Set number of attempts to read the EDID */
	adv7511_wr(sd, 0xc9, 0xf);
	return true;
}

/* Enable interrupts */
static void adv7511_set_isr(struct v4l2_subdev *sd, bool enable)
{
	uint8_t irqs = MASK_ADV7511_HPD_INT | MASK_ADV7511_MSEN_INT;
	uint8_t irqs_rd;
	int retries = 100;

	/* The datasheet says that the EDID ready interrupt should be
	   disabled if there is no hotplug. */
	if (!enable)
		irqs = 0;
	else if (adv7511_have_hotplug(sd))
		irqs |= MASK_ADV7511_EDID_RDY_INT;

	/*
	 * This i2c write can fail (approx. 1 in 1000 writes). But it
	 * is essential that this register is correct, so retry it
	 * multiple times.
	 *
	 * Note that the i2c write does not report an error, but the readback
	 * clearly shows the wrong value.
	 */
	do {
		adv7511_wr(sd, 0x94, irqs);
		irqs_rd = adv7511_rd(sd, 0x94);
	} while (retries-- && irqs_rd != irqs);

	if (irqs_rd == irqs)
		return;
	v4l2_err(sd, "Could not set interrupts: hw failure?\n");
}

/* Interrupt handler */
static int adv7511_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct delayed_work *dwork = &state->edid_handler;
	uint8_t irq_status;

	/* disable interrupts to prevent a race condition */
	adv7511_set_isr(sd, false);
	irq_status = adv7511_rd(sd, 0x96);
	/* clear detected interrupts */
	adv7511_wr(sd, 0x96, irq_status);

	if (irq_status & (MASK_ADV7511_HPD_INT | MASK_ADV7511_MSEN_INT))
		adv7511_check_monitor_present_status(sd);
	if (irq_status & MASK_ADV7511_EDID_RDY_INT)
		if (!delayed_work_pending(dwork))
			queue_delayed_work(state->work_queue,
				&state->edid_handler, EDID_DELAY);

	/* enable interrupts */
	adv7511_set_isr(sd, true);
	if (handled)
		*handled = true;

	return 0;
}

static long adv7511_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	union {
		struct v4l2_subdev_edid *edid;
	} ioctl;

	switch (cmd) {
	case VIDIOC_SUBDEV_G_EDID:
		ioctl.edid = arg;

		if (ioctl.edid->pad != 0)
			return -EINVAL;
		if ((ioctl.edid->blocks == 0) || (ioctl.edid->blocks > 256))
			return -EINVAL;
		if (!ioctl.edid->edid)
			return -EINVAL;
		if (!state->edid.segments) {
			v4l2_dbg(1, debug, sd, "EDID segment 0 not found\n");
			return -ENODATA;
		}
		if (ioctl.edid->start_block >= state->edid.segments*2)
			return -E2BIG;
		if ((ioctl.edid->blocks + ioctl.edid->start_block) >=
			state->edid.segments*2) {
			ioctl.edid->blocks = (state->edid.segments * 2) -
				ioctl.edid->start_block;
		}
		memcpy(ioctl.edid->edid,
			&state->edid.data[ioctl.edid->start_block * 128],
			ioctl.edid->blocks * 128);
		break;

	default:
		v4l2_dbg(1, debug, sd, "unknown ioctl %08x\n", cmd);
		return -ENOTTY;
	}

	return 0;
}

static const struct v4l2_subdev_core_ops adv7511_core_ops = {
	.log_status = adv7511_log_status,
	.g_chip_ident = adv7511_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = adv7511_g_register,
	.s_register = adv7511_s_register,
#endif
	.s_power = adv7511_s_power,
	.interrupt_service_routine = adv7511_isr,
	.ioctl = adv7511_ioctl,
};

/* ---------------------------- VIDEO OPS ---------------------------- */

/* Enable/disable adv7511 output */
static int adv7511_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	v4l2_dbg(1, debug, sd,
		"%s: %sable\n", __func__, (enable ? "en" : "dis"));

	adv7511_wr_and_or(sd, 0xa1, ~0x3c, (enable ? 0 : 0x3c));
	if (enable) {
		adv7511_check_monitor_present_status(sd);
	} else {
		adv7511_s_power(sd, 0);
		state->have_monitor = false;
	}

	return 0;
}

static const struct v4l2_dv_timings adv7511_timings[] = {
	V4L2_DV_BT_CEA_720X480P59_94,
	V4L2_DV_BT_CEA_720X576P50,
	V4L2_DV_BT_CEA_1280X720P24,
	V4L2_DV_BT_CEA_1280X720P25,
	V4L2_DV_BT_CEA_1280X720P30,
	V4L2_DV_BT_CEA_1280X720P50,
	V4L2_DV_BT_CEA_1280X720P60,
	V4L2_DV_BT_CEA_1920X1080P24,
	V4L2_DV_BT_CEA_1920X1080P25,
	V4L2_DV_BT_CEA_1920X1080P30,
	V4L2_DV_BT_CEA_1920X1080P50,
	V4L2_DV_BT_CEA_1920X1080P60,

	V4L2_DV_BT_DMT_640X350P85,
	V4L2_DV_BT_DMT_640X400P85,
	V4L2_DV_BT_DMT_720X400P85,
	V4L2_DV_BT_DMT_640X480P60,
	V4L2_DV_BT_DMT_640X480P72,
	V4L2_DV_BT_DMT_640X480P75,
	V4L2_DV_BT_DMT_640X480P85,
	V4L2_DV_BT_DMT_800X600P56,
	V4L2_DV_BT_DMT_800X600P60,
	V4L2_DV_BT_DMT_800X600P72,
	V4L2_DV_BT_DMT_800X600P75,
	V4L2_DV_BT_DMT_800X600P85,
	V4L2_DV_BT_DMT_848X480P60,
	V4L2_DV_BT_DMT_1024X768P60,
	V4L2_DV_BT_DMT_1024X768P70,
	V4L2_DV_BT_DMT_1024X768P75,
	V4L2_DV_BT_DMT_1024X768P85,
	V4L2_DV_BT_DMT_1152X864P75,
	V4L2_DV_BT_DMT_1280X768P60_RB,
	V4L2_DV_BT_DMT_1280X768P60,
	V4L2_DV_BT_DMT_1280X768P75,
	V4L2_DV_BT_DMT_1280X768P85,
	V4L2_DV_BT_DMT_1280X800P60_RB,
	V4L2_DV_BT_DMT_1280X800P60,
	V4L2_DV_BT_DMT_1280X800P75,
	V4L2_DV_BT_DMT_1280X800P85,
	V4L2_DV_BT_DMT_1280X960P60,
	V4L2_DV_BT_DMT_1280X960P85,
	V4L2_DV_BT_DMT_1280X1024P60,
	V4L2_DV_BT_DMT_1280X1024P75,
	V4L2_DV_BT_DMT_1280X1024P85,
	V4L2_DV_BT_DMT_1360X768P60,
	V4L2_DV_BT_DMT_1400X1050P60_RB,
	V4L2_DV_BT_DMT_1400X1050P60,
	V4L2_DV_BT_DMT_1400X1050P75,
	V4L2_DV_BT_DMT_1400X1050P85,
	V4L2_DV_BT_DMT_1440X900P60_RB,
	V4L2_DV_BT_DMT_1440X900P60,
	V4L2_DV_BT_DMT_1600X1200P60,
	V4L2_DV_BT_DMT_1680X1050P60_RB,
	V4L2_DV_BT_DMT_1680X1050P60,
	V4L2_DV_BT_DMT_1792X1344P60,
	V4L2_DV_BT_DMT_1856X1392P60,
	V4L2_DV_BT_DMT_1920X1200P60_RB,
	V4L2_DV_BT_DMT_1366X768P60,
	V4L2_DV_BT_DMT_1920X1080P60,
	{}
};

static int adv7511_s_dv_timings(struct v4l2_subdev *sd,
			       struct v4l2_dv_timings *timings)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	int i;

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	/* quick sanity check */
	if (timings->type != V4L2_DV_BT_656_1120)
		return -EINVAL;

	if (timings->bt.interlaced)
		return -EINVAL;
	if (timings->bt.pixelclock < 27000000 ||
	    timings->bt.pixelclock > 170000000)
		return -EINVAL;

	/* Fill the optional fields .standards and .flags
	   in struct v4l2_dv_timings if the format is listed
	   in adv7511_timings[] */
	for (i = 0; adv7511_timings[i].bt.width; i++) {
		if (v4l_match_dv_timings(timings, &adv7511_timings[i], 0)) {
			*timings = adv7511_timings[i];
			break;
		}
	}

	timings->bt.flags &= ~V4L2_DV_FL_REDUCED_FPS;

	/* save timings */
	state->dv_timings = *timings;

	/* update quantization range based on new dv_timings */
	/* adv7511_set_rgb_quantization_mode(sd,
		state->rgb_quantization_range_ctrl); */

	/* update AVI infoframe */
	adv7511_set_IT_content_AVI_InfoFrame(sd);

	return 0;
}

static int adv7511_g_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (!timings)
		return -EINVAL;

	*timings = state->dv_timings;

	return 0;
}


static int adv7511_enum_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_enum_dv_timings *timings)
{
	if (timings->index >= ARRAY_SIZE(adv7511_timings))
		return -EINVAL;

	timings->timings = adv7511_timings[timings->index];

	return 0;
}

static int adv7511_dv_timings_cap(struct v4l2_subdev *sd,
				  struct v4l2_dv_timings_cap *cap)
{
	cap->type = V4L2_DV_BT_656_1120;
	cap->bt.max_width = 1920;
	cap->bt.max_height = 1200;
	cap->bt.min_pixelclock = 27000000;
	cap->bt.max_pixelclock = 170000000;
	cap->bt.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			 V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT;
	cap->bt.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
		V4L2_DV_BT_CAP_REDUCED_BLANKING | V4L2_DV_BT_CAP_CUSTOM;

	return 0;
}

static int adv7511_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned int index,
		enum v4l2_mbus_pixelcode *code)
{
	if (index > 0)
		return -EINVAL;

	*code = V4L2_MBUS_FMT_VYUY8_1X16;

	return 0;
}

static int adv7511_g_mbus_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct v4l2_bt_timings *bt = &state->dv_timings.bt;

	fmt->width = bt->width;
	fmt->height = bt->height;
	fmt->code = V4L2_MBUS_FMT_VYUY8_1X16;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_REC709;

	return 0;
}

static const struct v4l2_subdev_video_ops adv7511_video_ops = {
	.s_stream = adv7511_s_stream,
	.s_dv_timings = adv7511_s_dv_timings,
	.g_dv_timings = adv7511_g_dv_timings,
	.enum_mbus_fmt = adv7511_enum_mbus_fmt,
	.g_mbus_fmt = adv7511_g_mbus_fmt,
	.try_mbus_fmt = adv7511_g_mbus_fmt,
	.s_mbus_fmt = adv7511_g_mbus_fmt,
	.enum_dv_timings = adv7511_enum_dv_timings,
	.dv_timings_cap = adv7511_dv_timings_cap,
};

/* ---------------------------- AUDIO OPS ---------------------------- */

#if 0
static int adv7511_s_audio_i2s_format(struct v4l2_subdev *sd, int format)
{
	adv7511_wr_and_or(sd, 0x0c, 0xfc, (format & 0x03));

	return 0;
}

static int adv7511_s_audio_channel_count(struct v4l2_subdev *sd, int count)
{
	u32 val;

	if (count == 0)
		val = 0x00; /* refer to stream header */
	else if (count != 1 && count <= 8)
		val = count - 1;
	else
		return -EINVAL;
	adv7511_wr_and_or(sd, 0x73, 0xf8, (val & 0x03));

	return 0;
}

static int adv7511_s_audio_channel_map(struct v4l2_subdev *sd, int map)
{
	return adv7511_wr(sd, 0x76, map);
}

static int adv7511_s_audio_word_length(struct v4l2_subdev *sd, int length)
{
	u32 val;

	switch (length) {
	case 16:
		val = 0x02;
		break;
	case 17:
		val = 0x0c;
		break;
	case 18:
		val = 0x04;
		break;
	case 19:
		val = 0x08;
		break;
	case 20:
		val = 0x0a;
		break;
	case 21:
		val = 0x0d;
		break;
	case 22:
		val = 0x05;
		break;
	case 23:
		val = 0x09;
		break;
	case 24:
		val = 0x0b;
		break;
	default:
		return -EINVAL;
	}
	adv7511_wr_and_or(sd, 0x14, 0xf0, val);

	return 0;
}
#endif

static int adv7511_s_audio_stream(struct v4l2_subdev *sd, int enable)
{
	v4l2_dbg(1, debug, sd,
		"%s: %sable\n", __func__, (enable ? "en" : "dis"));

	if (enable)
		adv7511_wr_and_or(sd, 0x4b, 0x3f, 0x80);
	else
		adv7511_wr_and_or(sd, 0x4b, 0x3f, 0x40);

	return 0;
}

static int adv7511_s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	u32 N;

	switch (freq) {
	case 32000:
		N = 4096;
		break;
	case 44100:
		N = 6272;
		break;
	case 48000:
		N = 6144;
		break;
	case 88200:
		N = 12544;
		break;
	case 96000:
		N = 12288;
		break;
	case 176400:
		N = 25088;
		break;
	case 192000:
		N = 24576;
		break;
	default:
		return -EINVAL;
	}

	/* Set N (used with CTS to regenerate the audio clock) */
	adv7511_wr(sd, 0x01, (N >> 16) & 0xf);
	adv7511_wr(sd, 0x02, (N >> 8) & 0xff);
	adv7511_wr(sd, 0x03, N & 0xff);

	return 0;
}

static int adv7511_s_i2s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	u32 i2s_sf;

	switch (freq) {
	case 32000:
		i2s_sf = 0x30;
		break;
	case 44100:
		i2s_sf = 0x00;
		break;
	case 48000:
		i2s_sf = 0x20;
		break;
	case 88200:
		i2s_sf = 0x80;
		break;
	case 96000:
		i2s_sf = 0xa0;
		break;
	case 176400:
		i2s_sf = 0xc0;
		break;
	case 192000:
		i2s_sf = 0xe0;
		break;
	default:
		return -EINVAL;
	}

	/* Set sampling frequency for I2S audio to 48 kHz */
	adv7511_wr_and_or(sd, 0x15, 0xf, i2s_sf);

	return 0;
}

/* TODO! */
static int adv7511_s_routing(struct v4l2_subdev *sd,
	u32 input, u32 output, u32 config)
{
	/* TODO based on input/output/config */
	/* TODO See datasheet "Programmers guide" p. 39-40 */

	/* Only 2 channels in use for application */
	adv7511_wr_and_or(sd, 0x73, 0xf8, 0x1);
	/* Speaker mapping */
	adv7511_wr(sd, 0x76, 0x00);

	/* TODO Where should this be placed? */
	/* 16 bit audio word length */
	adv7511_wr_and_or(sd, 0x14, 0xf0, 0x02);

	return 0;
}

static const struct v4l2_subdev_audio_ops adv7511_audio_ops = {
	.s_stream = adv7511_s_audio_stream,
	.s_clock_freq = adv7511_s_clock_freq,
	.s_i2s_clock_freq = adv7511_s_i2s_clock_freq,
	.s_routing = adv7511_s_routing,
};

/* ---------------------------- SUBDEV OPS ---------------------------- */

static const struct v4l2_subdev_ops adv7511_ops = {
	.core  = &adv7511_core_ops,
	.video = &adv7511_video_ops,
	.audio = &adv7511_audio_ops,
};

/* -------------------------------------------------------- */

static void adv7511_dbg_dump_edid(int lvl, int debug, struct v4l2_subdev *sd,
	int segment, uint8_t *buf)
{
	if (debug >= lvl) {
		int i, j;
		v4l2_dbg(lvl, debug, sd, "edid segment %d\n", segment);
		for (i = 0; i < 256; i += 16) {
			u8 b[128];
			u8 *bp = b;
			if (i == 128)
				v4l2_dbg(lvl, debug, sd, "\n");
			for (j = i; j < i + 16; j++) {
				sprintf(bp, "0x%02x, ", buf[j]);
				bp += 6;
			}
			bp[0] = '\0';
			v4l2_dbg(lvl, debug, sd, "%s\n", b);
		}
	}
}

static void adv7511_edid_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct adv7511_state *state =
		container_of(dwork, struct adv7511_state, edid_handler);
	struct v4l2_subdev *sd = &state->sd;
	struct adv7511_edid_detect ed;

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (adv7511_check_edid_status(sd)) {
		/* Return if we received the EDID. */
		return;
	}

	if (adv7511_have_hotplug(sd)) {
		/* We must retry reading the EDID several times, it is possible
		 * that initially the EDID couldn't be read due to i2c errors
		 * (DVI connectors are particularly prone to this problem). */
		if (state->edid.read_retries) {
			state->edid.read_retries--;
			/* EDID read failed, trigger a retry */
			adv7511_wr(sd, 0xc9, 0xf);
			queue_delayed_work(state->work_queue,
				&state->edid_handler, EDID_DELAY);
			return;
		}
	}

	/* We failed to read the EDID, so send an event for this. */
	ed.present = false;
	ed.segment = adv7511_rd(sd, 0xc4);
	v4l2_subdev_notify(sd, ADV7511_EDID_DETECT, (void *)&ed);
	v4l2_dbg(1, debug, sd, "%s: no edid found\n", __func__);
}

static void adv7511_audio_setup(struct v4l2_subdev *sd)
{
	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	adv7511_s_i2s_clock_freq(sd, 48000);
	adv7511_s_clock_freq(sd, 48000);
	adv7511_s_routing(sd, 0, 0, 0);
}

#ifdef CONFIG_OF
static void adv7511_set_ofdt_config(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct adv7511_config *config = &state->cfg;
	uint8_t val_mask, val;
	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	/* Input format */
	val_mask = 0;
	switch (config->in_params.input_id) {
	default:
	case 0:
		val = 0x00;
		break;
	case 1:
		val = 0x01;
		break;
	case 2:
		val = 0x02;
		config->embedded_sync = true;
		break;
	case 3:
		val = 0x03;
		break;
	case 4:
		val = 0x04;
		config->embedded_sync = true;
		break;
	case 5:
		val = 0x05;
		break;
	case 6:
		val = 0x06;
		break;
	case 7:
		val = 0x07;
		break;
	case 8:
		val = 0x08;
		config->embedded_sync = true;
		break;
	}
	val_mask |= val;
	adv7511_wr(sd, 0x15, val_mask);

	/* Output format */
	val_mask = 0;
	switch (config->out_params.output_color_space) {
	default:
	case 0:
		val = 0x00;
		break;
	case 1:
		val = 0x01;
		break;
	}
	val_mask |= (val << 0);
	switch (config->in_params.input_style) {
	case 1:
		val = 0x02;
		break;
	case 2:
		val = 0x01;
		break;
	case 3:
		val = 0x03;
		break;
	default:
		val = 0x00;
		break;
	}
	val_mask |= (val << 2);
	switch (config->in_params.input_color_depth) {
	case 8:
		val = 0x03;
		break;
	case 10:
		val = 0x01;
		break;
	case 12:
		val = 0x02;
		break;
	default:
		val = 0x00;
		break;
	}
	val_mask |= (val << 4);
	switch (config->out_params.output_format) {
	default:
	case 0:
		val = 0x00;
		break;
	case 1:
		val = 0x01;
		break;
	}
	val_mask |= (val << 7);
	adv7511_wr(sd, 0x16, val_mask);

	/* H, V sync polarity, interpolation style */
	val_mask = 0;
	switch (config->out_params.up_conversion) {
	default:
	case 0:
		val = 0x00;
		break;
	case 1:
		val = 0x01;
		break;
	}
	val_mask |= (val << 2);
	switch (config->in_params.hsync_polarity) {
	default:
	case 0:
		val = 0x00;
		break;
	case 1:
		val = 0x01;
		break;
	}
	val_mask |= (val << 5);
	switch (config->in_params.vsync_polarity) {
	default:
	case 0:
		val = 0x00;
		break;
	case 1:
		val = 0x01;
		break;
	}
	val_mask |= (val << 6);
	adv7511_wr(sd, 0x17, val_mask);

	/* CSC mode, CSC coefficients */
	if (config->out_params.csc_enable) {
		switch (config->out_params.csc_scaling_factor) {
		case 1:
			val = 0x00;
			break;
		case 2:
			val = 0x01;
			break;
		case 4:
		default:
			val = 0x02;
			break;
		}
		adv7511_csc_conversion_mode(sd, val);
		adv7511_csc_coeff(sd,
				  config->out_params.csc_coeff.a1,
				  config->out_params.csc_coeff.a2,
				  config->out_params.csc_coeff.a3,
				  config->out_params.csc_coeff.a4,
				  config->out_params.csc_coeff.b1,
				  config->out_params.csc_coeff.b2,
				  config->out_params.csc_coeff.b3,
				  config->out_params.csc_coeff.b4,
				  config->out_params.csc_coeff.c1,
				  config->out_params.csc_coeff.c2,
				  config->out_params.csc_coeff.c3,
				  config->out_params.csc_coeff.c4);
		/* enable CSC */
		adv7511_wr_and_or(sd, 0x18, 0x7f, 0x80);
		/* AVI infoframe: Limited range RGB (16-235) */
		adv7511_wr_and_or(sd, 0x57, 0xf3, 0x04);
	}

	/* AVI Info, Audio Info */
	adv7511_wr_and_or(sd, 0x44, 0xe7, 0x10);

	/* Video input justification */
	val_mask = 0;
	switch (config->in_params.bit_justification) {
	default:
	case 0:
		val = 0x00;
		break;
	case 1:
		val = 0x01;
		break;
	case 2:
		val = 0x02;
		break;
	}
	val_mask |= (val << 3);
	adv7511_wr(sd, 0x48, val_mask);

	/* Output format */
	val_mask = 0x00;
	if (config->out_params.output_format == 1) {
		if (config->out_params.output_color_space == 0)
			val_mask = 0x02;
		else if (config->out_params.output_format == 1)
			val_mask = 0x01;
	}
	val_mask <<= 5;
	adv7511_wr(sd, 0x55, val_mask);

	/* Picture format aspect ratio */
	adv7511_wr(sd, 0x56, 0x28);

	/* HDCP, Frame encryption, HDMI/DVI */
	val_mask = 0x04;
	if (config->out_params.hdmi_mode)
		val_mask |= 0x02;
	adv7511_wr(sd, 0xaf, val_mask);

	/* Capture for input video clock */
	val_mask = 0;
	switch (config->in_params.clock_delay) {
	default:
	case 0:
		val = 0x00;
		break;
	case 1:
		val = 0x01;
		break;
	case 2:
		val = 0x02;
		break;
	case 3:
		val = 0x03;
		break;
	case 4:
		val = 0x04;
		break;
	case 5:
		val = 0x05;
		break;
	case 6:
		val = 0x06;
		break;
	case 7:
		val = 0x07;
		break;
	}
	val_mask |= (val << 5);
	adv7511_wr_and_or(sd, 0xba, 0x1f, val_mask);
}
#endif

/* Configure hdmi transmitter. */
static void adv7511_setup(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	if (!state->cfg.loaded) {
		/* Input format: RGB 4:4:4 */
		adv7511_wr_and_or(sd, 0x15, 0xf0, 0x0);
		/* Output format: RGB 4:4:4 */
		adv7511_wr_and_or(sd, 0x16, 0x7f, 0x0);
		/* 1st order interpolation 4:2:2 -> 4:4:4 up conversion,
		   Aspect ratio: 16:9 */
		adv7511_wr_and_or(sd, 0x17, 0xf9, 0x06);
		/* Disable pixel repetition */
		adv7511_wr_and_or(sd, 0x3b, 0x9f, 0x0);
		/* Disable CSC */
		adv7511_wr_and_or(sd, 0x18, 0x7f, 0x0);
		/* Output format: RGB 4:4:4, Active Format Information is valid,
		 * underscanned */
		adv7511_wr_and_or(sd, 0x55, 0x9c, 0x12);
		/* AVI Info frame packet enable, Audio Info frame disable */
		adv7511_wr_and_or(sd, 0x44, 0xe7, 0x10);
		/* RGB Quantization range: full range */
		/* FIXME: this may be halley specific...
		   we need to make a control value if we want to
		   set it from user space? */
		adv7511_wr(sd, 0x57, 0x08);
		/* Colorimetry, Active format aspect ratio: same as picure. */
		adv7511_wr(sd, 0x56, 0xa8);
		/* No encryption */
		adv7511_wr_and_or(sd, 0xaf, 0xed, 0x2);

		/* Positive clk edge capture for input video clock */
		adv7511_wr_and_or(sd, 0xba, 0x1f, 0x60);
	} else {
#ifdef CONFIG_OF
		adv7511_set_ofdt_config(sd);
#endif
	}

	adv7511_audio_setup(sd);

	v4l2_ctrl_handler_setup(&state->hdl);
}

static void adv7511_notify_monitor_detect(struct v4l2_subdev *sd)
{
	struct adv7511_monitor_detect mdt;
	struct adv7511_state *state = get_adv7511_state(sd);

	mdt.present = state->have_monitor;
	v4l2_subdev_notify(sd, ADV7511_MONITOR_DETECT, (void *)&mdt);
}

static void adv7511_check_monitor_present_status(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	/* read hotplug and rx-sense state */
	uint8_t status = adv7511_rd(sd, 0x42);

	v4l2_dbg(1, debug, sd, "%s: status: 0x%x%s%s\n",
			 __func__,
			 status,
			 status & MASK_ADV7511_HPD_DETECT ? ", hotplug" : "",
			 status & MASK_ADV7511_MSEN_DETECT ? ", rx-sense" : "");

	if ((status & MASK_ADV7511_HPD_DETECT) &&
		((status & MASK_ADV7511_MSEN_DETECT) || state->edid.segments)) {
		v4l2_dbg(1, debug, sd, "%s: hotplug and (rx-sense or edid)\n",
			__func__);
		if (!state->have_monitor) {
			v4l2_dbg(1, debug, sd,
				"%s: monitor detected\n", __func__);
			state->have_monitor = true;
			adv7511_set_isr(sd, true);
			if (!adv7511_s_power(sd, true)) {
				v4l2_dbg(1, debug, sd, "%s: monitor detected, powerup failed\n",
					__func__);
				return;
			}
			adv7511_setup(sd);
			adv7511_notify_monitor_detect(sd);
			state->edid.read_retries = EDID_MAX_RETRIES;
			queue_delayed_work(state->work_queue,
				&state->edid_handler, EDID_DELAY);
		}
	} else if (status & MASK_ADV7511_HPD_DETECT) {
		v4l2_dbg(1, debug, sd, "%s: hotplug detected\n", __func__);
		state->edid.read_retries = EDID_MAX_RETRIES;
		queue_delayed_work(
			state->work_queue, &state->edid_handler, EDID_DELAY);
	} else if (!(status & MASK_ADV7511_HPD_DETECT)) {
		v4l2_dbg(1, debug, sd, "%s: hotplug not detected\n", __func__);
		if (state->have_monitor) {
			v4l2_dbg(1, debug, sd,
				"%s: monitor not detected\n", __func__);
			state->have_monitor = false;
			adv7511_notify_monitor_detect(sd);
		}
		adv7511_s_power(sd, false);
		memset(&state->edid, 0, sizeof(struct adv7511_state_edid));
	}
#if 0
	/* update read only ctrls */
	v4l2_ctrl_s_ctrl(
		state->hotplug_ctrl, adv7511_have_hotplug(sd) ? 0x1 : 0x0);
	v4l2_ctrl_s_ctrl(
		state->rx_sense_ctrl, adv7511_have_rx_sense(sd) ? 0x1 : 0x0);
	v4l2_ctrl_s_ctrl(
		state->have_edid0_ctrl, state->edid.segments ? 0x1 : 0x0);
#endif
}

static bool edid_block_verify_crc(uint8_t *edid_block)
{
	int i;
	uint8_t sum = 0;

	for (i = 0; i < 127; i++)
		sum += *(edid_block + i);

	return ((255 - sum + 1) == edid_block[127]);
}

static bool edid_segment_verify_crc(struct v4l2_subdev *sd, u32 segment)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	u32 blocks = state->edid.blocks;
	uint8_t *data = state->edid.data;

	if (edid_block_verify_crc(&data[segment*256])) {
		if ((segment + 1)*2 <= blocks)
			return edid_block_verify_crc(&data[segment*256 + 128]);
		return true;
	}
	return false;
}

static void adv7511_embedded_sync(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	u8 val;

	val = (u8)(state->dv_timings.bt.hfrontporch >> 2);
	adv7511_wr(sd, 0x30, val);
	val = (u8)((state->dv_timings.bt.hfrontporch << 6) |
		(state->dv_timings.bt.hsync >> 4));
	adv7511_wr(sd, 0x31, val);
	val = (u8)((state->dv_timings.bt.hsync << 4) |
		(state->dv_timings.bt.vfrontporch >> 6));
	adv7511_wr(sd, 0x32, val);
	val = (u8)((state->dv_timings.bt.vfrontporch << 2) |
		(state->dv_timings.bt.vsync >> 8));
	adv7511_wr(sd, 0x33, val);
	val = (u8)state->dv_timings.bt.vsync;
	adv7511_wr(sd, 0x34, val);

	adv7511_wr_and_or(sd, 0x41, 0xFD, 0x02);

	val = 0;
	if (!(state->dv_timings.bt.polarities & V4L2_DV_VSYNC_POS_POL))
		val |= 0x40;
	if (!(state->dv_timings.bt.polarities & V4L2_DV_HSYNC_POS_POL))
		val |= 0x20;
	if (val)
		adv7511_wr_and_or(sd, 0x17, 0x9F, val);
}

static bool adv7511_check_edid_status(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	uint8_t edidRdy = adv7511_rd(sd, 0xc5);

	v4l2_dbg(1, debug, sd, "%s: edid ready (retries: %d)\n",
			 __func__, EDID_MAX_RETRIES - state->edid.read_retries);

	if (edidRdy & MASK_ADV7511_EDID_RDY) {
		int segment = adv7511_rd(sd, 0xc4);
		struct adv7511_edid_detect ed;
		if (segment >= EDID_MAX_SEGM) {
			v4l2_err(sd, "edid segment number too big\n");
			return false;
		}
		v4l2_dbg(1, debug, sd,
			"%s: got segment %d\n", __func__, segment);
		adv7511_edid_rd(sd, 256, &state->edid.data[segment * 256]);
		adv7511_dbg_dump_edid(2, debug, sd,
			segment, &state->edid.data[segment * 256]);
		if (segment == 0) {
			state->edid.blocks = state->edid.data[0x7e] + 1;
			v4l2_dbg(1, debug, sd, "%s: %d blocks in total\n",
				__func__, state->edid.blocks);
		}
		if (!edid_segment_verify_crc(sd, segment)) {
			/* edid crc error, force reread of edid segment */
			adv7511_s_power(sd, false);
			adv7511_s_power(sd, true);
			return false;
		} else {
			/* one more segment read ok */
			state->edid.segments = segment + 1;
		}
		if (((state->edid.data[0x7e]>>1) + 1) > state->edid.segments) {
			/* Request next EDID segment */
			v4l2_dbg(1, debug, sd, "%s: request segment %d\n",
				__func__, state->edid.segments);
			adv7511_wr(sd, 0xc9, 0xf);
			adv7511_wr(sd, 0xc4, state->edid.segments);
			state->edid.read_retries = EDID_MAX_RETRIES;
			queue_delayed_work(state->work_queue,
				&state->edid_handler, EDID_DELAY);
			return false;
		}

		/* report when we have all segments
		   but report only for segment 0
		 */
		ed.present = true;
		ed.segment = 0;
		v4l2_subdev_notify(sd, ADV7511_EDID_DETECT, (void *)&ed);
		state->edid_detect_counter++;

		if (state->cfg.embedded_sync)
			adv7511_embedded_sync(sd);
#if 0
		v4l2_ctrl_s_ctrl(state->have_edid0_ctrl,
			state->edid.segments ? 0x1 : 0x0);
#endif
		return ed.present;
	}

	return false;
}

/* -------------------------------------------------------- */

/* Setup ADV7511 */
static void adv7511_init_setup(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct adv7511_state_edid *edid = &state->edid;

	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	/* clear all interrupts */
	adv7511_wr(sd, 0x96, 0xff);
	memset(edid, 0, sizeof(struct adv7511_state_edid));
	state->have_monitor = false;
	adv7511_set_isr(sd, false);
	adv7511_s_stream(sd, false);
	adv7511_s_audio_stream(sd, false);
}

#ifdef CONFIG_OF
static void adv7511_get_ofdt_config(struct i2c_client *client,
	struct adv7511_state *state)
{
	struct device_node *dn = client->dev.of_node;
	struct device_node *np;
	struct adv7511_config *config = &state->cfg;
	u32 const *prop;
	int size;
	bool vin_loaded, vout_loaded;

	vin_loaded = vout_loaded = false;

	prop = of_get_property(dn, "edid-addr", &size);
	if (prop)
		state->pdata.edid_addr = (uint8_t)be32_to_cpup(prop);

	np = of_find_node_by_name(dn, "video-input");
	if (np) {
		prop = of_get_property(np, "input-id", &size);
		if (prop)
			config->in_params.input_id =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "input-style", &size);
		if (prop)
			config->in_params.input_style =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "input-color-depth", &size);
		if (prop)
			config->in_params.input_color_depth =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "bit-justification", &size);
		if (prop)
			config->in_params.bit_justification =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "hsync-polarity", &size);
		if (prop)
			config->in_params.hsync_polarity =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "vsync-polarity", &size);
		if (prop)
			config->in_params.vsync_polarity =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "clock-delay", &size);
		if (prop)
			config->in_params.clock_delay =
				(uint8_t)be32_to_cpup(prop);
		vin_loaded = true;
	} else {
		pr_info("No video input configuration, using device default\n");
	}

	np = of_find_node_by_name(dn, "video-output");
	if (np) {
		prop = of_get_property(np, "hdmi-mode", &size);
		if (prop) {
			if (be32_to_cpup(prop) == 1)
				config->out_params.hdmi_mode = true;
		}
		prop = of_get_property(np, "output-format", &size);
		if (prop)
			config->out_params.output_format =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "output-color-space", &size);
		if (prop)
			config->out_params.output_color_space =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "up-conversion", &size);
		if (prop)
			config->out_params.up_conversion =
				(uint8_t)be32_to_cpup(prop);
		prop = of_get_property(np, "csc-enable", &size);
		if (prop)
			config->out_params.csc_enable =
				(uint8_t)be32_to_cpup(prop);
		if (config->out_params.csc_enable) {
			prop = of_get_property(np, "csc-scaling-factor", &size);
			if (prop) {
				config->out_params.csc_scaling_factor =
					(uint8_t)be32_to_cpup(prop);
			}
			np = of_find_node_by_name(dn, "csc-coefficients");
			if (np) {
				prop = of_get_property(np, "a1", &size);
				if (prop) {
					config->out_params.csc_coeff.a1 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "a2", &size);
				if (prop) {
					config->out_params.csc_coeff.a2 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "a3", &size);
				if (prop) {
					config->out_params.csc_coeff.a3 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "a4", &size);
				if (prop) {
					config->out_params.csc_coeff.a4 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "b1", &size);
				if (prop) {
					config->out_params.csc_coeff.b1 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "b2", &size);
				if (prop) {
					config->out_params.csc_coeff.b2 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "b3", &size);
				if (prop) {
					config->out_params.csc_coeff.b3 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "b4", &size);
				if (prop) {
					config->out_params.csc_coeff.b4 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "c1", &size);
				if (prop) {
					config->out_params.csc_coeff.c1 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "c2", &size);
				if (prop) {
					config->out_params.csc_coeff.c2 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "c3", &size);
				if (prop) {
					config->out_params.csc_coeff.c3 =
						(uint16_t)be32_to_cpup(prop);
				}
				prop = of_get_property(np, "c4", &size);
				if (prop) {
					config->out_params.csc_coeff.c4 =
						(uint16_t)be32_to_cpup(prop);
				}
			} else {
				pr_info("No CSC coefficients, using default\n");
			}
		}
		vout_loaded = true;
	} else {
		pr_info("No video output configuration, using device default\n");
	}

	if (vin_loaded && vout_loaded)
		config->loaded = true;
}
#endif

struct v4l2_subdev *adv7511_subdev(struct v4l2_subdev *sd)
{
	static struct v4l2_subdev *subdev;

	if (sd)
		subdev = sd;

	return subdev;
}
EXPORT_SYMBOL(adv7511_subdev);

static int adv7511_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	const struct v4l2_dv_timings dv1080p60 = V4L2_DV_BT_CEA_1920X1080P60;
	struct adv7511_state *state;
	struct adv7511_platform_data *pdata = client->dev.platform_data;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_subdev *sd;
	u8 chip_id[2];
	int err = -EIO;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l2_dbg(1, debug, sd, "detecting adv7511 client on address 0x%x\n",
			 client->addr << 1);

	state = kzalloc(sizeof(struct adv7511_state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

#ifdef CONFIG_OF
	adv7511_get_ofdt_config(client, state);
#else
	if (pdata == NULL) {
		v4l_err(client, "No platform data!\n");
		err = -ENODEV;
		goto err_free;
	}
	memcpy(&state->pdata, pdata, sizeof(state->pdata));
#endif

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7511_ops);
	adv7511_subdev(sd);

	hdl = &state->hdl;
	v4l2_ctrl_handler_init(hdl, 10);
	/* add in ascending ID order */
#if 0
	state->hdmi_mode_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_hdmi_mode, NULL);
	state->audio_sample_freq_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_audio_sample_freq, NULL);
	state->audio_word_length_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_audio_word_length, NULL);
	state->audio_channel_count_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_audio_channel_count, NULL);
	state->audio_channel_map_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_audio_channel_map, NULL);
	state->audio_i2s_format_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_audio_i2s_format, NULL);
	state->hotplug_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_hotplug, NULL);
	state->rx_sense_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_rx_sense, NULL);
	state->have_edid0_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_edid_segment0, NULL);
	state->rgb_quantization_range_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7511_ctrl_rgb_quantization_range, NULL);
#endif
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		err = hdl->error;

		goto err_hdl;
	}
	state->pad.flags = MEDIA_PAD_FL_SINK;
	err = media_entity_init(&sd->entity, 1, &state->pad, 0);
	if (err)
		goto err_hdl;

	/* EDID i2c addr */
	state->edid_addr = state->pdata.edid_addr;

	state->chip_revision = adv7511_rd(sd, 0x0);
	chip_id[1] = adv7511_rd(sd, 0xf5);
	chip_id[0] = adv7511_rd(sd, 0xf6);
	if (chip_id[1] != 0x75  || chip_id[0] != 0x11) {
		v4l2_err(sd, "chip_id != 0x7511, read 0x%02x%02x\n",
			chip_id[1], chip_id[0]);
		err = -EIO;
		goto err_entity;
	}
	v4l2_dbg(1, debug, sd, "reg 0x41 0x%x, chip version (reg 0x00) 0x%x\n",
		adv7511_rd(sd, 0x41), state->chip_revision);

	state->edid_i2c_client =
		i2c_new_dummy(client->adapter, (state->edid_addr>>1));
	if (state->edid_i2c_client == NULL) {
		v4l2_err(sd, "failed to register edid i2c client\n");
		goto err_entity;
	}
	if (pdata && pdata->i2c_ex) {
		struct i2c_client *i2c_ex;
		i2c_ex = i2c_new_dummy(client->adapter, pdata->i2c_ex);
		/* enable 16-bit mode and sport */
		i2c_smbus_write_byte_data(i2c_ex, 0x14, 0x5b);
		i2c_smbus_write_byte_data(i2c_ex, 0x15, 0xff);
		i2c_smbus_write_byte_data(i2c_ex, 0x0, 0x0);
		i2c_smbus_write_byte_data(i2c_ex, 0x1, 0x0);
		i2c_unregister_device(i2c_ex);
	}

	state->work_queue = create_singlethread_workqueue(sd->name);
	if (state->work_queue == NULL) {
		v4l2_err(sd, "could not create workqueue\n");
		goto err_unreg;
	}

	INIT_DELAYED_WORK(&state->edid_handler, adv7511_edid_handler);

	state->dv_timings = dv1080p60;
	/* adv7511_init_setup(sd); */
	adv7511_set_isr(sd, true);

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
		client->addr << 1, client->adapter->name);
	return 0;

err_unreg:
	i2c_unregister_device(state->edid_i2c_client);
err_entity:
	media_entity_cleanup(&sd->entity);
err_hdl:
	v4l2_ctrl_handler_free(&state->hdl);
#ifndef CONFIG_OF
err_free:
#endif
	kfree(state);
	return err;
}

/* -------------------------------------------------------- */

static int adv7511_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7511_state *state = get_adv7511_state(sd);

	state->chip_revision = -1;

	v4l2_dbg(1, debug, sd, "%s removed @ 0x%x (%s)\n", client->name,
		 client->addr << 1, client->adapter->name);

	adv7511_init_setup(sd);
	cancel_delayed_work(&state->edid_handler);
	i2c_unregister_device(state->edid_i2c_client);
	destroy_workqueue(state->work_queue);
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	kfree(get_adv7511_state(sd));

	return 0;
}

/* -------------------------------------------------------- */

#ifdef CONFIG_OF
static struct of_device_id i2c_adv7511_of_match[] = {
	{ .compatible = "adv7511" },
	{ },
};
MODULE_DEVICE_TABLE(of, i2c_adv7511_of_match);
#endif

static struct i2c_device_id adv7511_id[] = {
	{ "adv7511", V4L2_IDENT_ADV7511 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7511_id);

static struct i2c_driver adv7511_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "adv7511",
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(i2c_adv7511_of_match),
#endif
	},
	.probe = adv7511_probe,
	.remove = adv7511_remove,
	.id_table = adv7511_id,
};

static int __init adv7511_init(void)
{
	return i2c_add_driver(&adv7511_driver);
}

static void __exit adv7511_exit(void)
{
	i2c_del_driver(&adv7511_driver);
}

module_init(adv7511_init);
module_exit(adv7511_exit);

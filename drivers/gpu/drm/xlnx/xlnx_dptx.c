// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA DisplayPort TX Subsystem Driver
 *
 *  Copyright (C) 2020 Xilinx, Inc.
 *
 * Author: Rajesh Gugulothu <gugulothu.rajesh@xilinx.com>
 *	 : Venkateshwar Rao G <vgannava.xilinx.com>
 *
 */
#include "xlnx_dptx.h"

#include <drm/drm_managed.h>
#include <sound/soc.h>
#include <sound/pcm_drm_eld.h>

#define I2S_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
			 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE |\
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |\
			 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |\
			 SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)
#define DP_RATES	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
			 SNDRV_PCM_RATE_192000)

static const struct xlnx_dp_feature xlnx_dp_cfg_v31 = {
	.flags = XDPTX_VTC_OFFSET_CHANGE,
};

static const struct xlnx_dp_feature xlnx_dp_cfg_v30 = {
	.flags = 0,
};

static const struct of_device_id xlnx_dp_of_match[] = {
	{ .compatible = "xlnx,v-dp-txss-3.0",
		.data = (void *)&xlnx_dp_cfg_v30},
	{ .compatible = "xlnx,v-dp-txss-3.1",
		.data = (void *)&xlnx_dp_cfg_v31},
	{ /* end of table */ }
};

/*
 * dp_codec_channel_alloc: speaker configuration available for CEA
 *
 * This is an ordered list that must match with dp_codec_8ch_chmaps struct
 * The preceding ones have better chances to be selected by
 * dp_codec_get_ch_alloc_table_idx().
 */
static const struct dp_codec_cea_spk_alloc dp_codec_channel_alloc[] = {
	{ .ca_id = 0x00, .n_ch = 2,
	  .mask = FL | FR},
	/* 2.1 */
	{ .ca_id = 0x01, .n_ch = 4,
	  .mask = FL | FR | LFE},
	/* Dolby Surround */
	{ .ca_id = 0x02, .n_ch = 4,
	  .mask = FL | FR | FC },
	/* surround51 */
	{ .ca_id = 0x0b, .n_ch = 6,
	  .mask = FL | FR | LFE | FC | RL | RR},
	/* surround40 */
	{ .ca_id = 0x08, .n_ch = 6,
	  .mask = FL | FR | RL | RR },
	/* surround41 */
	{ .ca_id = 0x09, .n_ch = 6,
	  .mask = FL | FR | LFE | RL | RR },
	/* surround50 */
	{ .ca_id = 0x0a, .n_ch = 6,
	  .mask = FL | FR | FC | RL | RR },
	/* 6.1 */
	{ .ca_id = 0x0f, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | RC },
	/* surround71 */
	{ .ca_id = 0x13, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | RLC | RRC },
	/* others */
	{ .ca_id = 0x03, .n_ch = 8,
	  .mask = FL | FR | LFE | FC },
	{ .ca_id = 0x04, .n_ch = 8,
	  .mask = FL | FR | RC},
	{ .ca_id = 0x05, .n_ch = 8,
	  .mask = FL | FR | LFE | RC },
	{ .ca_id = 0x06, .n_ch = 8,
	  .mask = FL | FR | FC | RC },
	{ .ca_id = 0x07, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RC },
	{ .ca_id = 0x0c, .n_ch = 8,
	  .mask = FL | FR | RC | RL | RR },
	{ .ca_id = 0x0d, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | RC },
	{ .ca_id = 0x0e, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | RC },
	{ .ca_id = 0x10, .n_ch = 8,
	  .mask = FL | FR | RL | RR | RLC | RRC },
	{ .ca_id = 0x11, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | RLC | RRC },
	{ .ca_id = 0x12, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | RLC | RRC },
	{ .ca_id = 0x14, .n_ch = 8,
	  .mask = FL | FR | FLC | FRC },
	{ .ca_id = 0x15, .n_ch = 8,
	  .mask = FL | FR | LFE | FLC | FRC },
	{ .ca_id = 0x16, .n_ch = 8,
	  .mask = FL | FR | FC | FLC | FRC },
	{ .ca_id = 0x17, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | FLC | FRC },
	{ .ca_id = 0x18, .n_ch = 8,
	  .mask = FL | FR | RC | FLC | FRC },
	{ .ca_id = 0x19, .n_ch = 8,
	  .mask = FL | FR | LFE | RC | FLC | FRC },
	{ .ca_id = 0x1a, .n_ch = 8,
	  .mask = FL | FR | RC | FC | FLC | FRC },
	{ .ca_id = 0x1b, .n_ch = 8,
	  .mask = FL | FR | LFE | RC | FC | FLC | FRC },
	{ .ca_id = 0x1c, .n_ch = 8,
	  .mask = FL | FR | RL | RR | FLC | FRC },
	{ .ca_id = 0x1d, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | FLC | FRC },
	{ .ca_id = 0x1e, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | FLC | FRC },
	{ .ca_id = 0x1f, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | FLC | FRC },
};

static void xlnx_dp_hpd_pulse_work_func(struct work_struct *work);
static int xlnx_dp_txconnected(struct xlnx_dp *dp);

static inline struct xlnx_dp *encoder_to_dp(struct drm_encoder *encoder)
{
	return container_of(encoder, struct xlnx_dp, encoder);
}

static inline struct xlnx_dp *bridge_to_dp(struct drm_bridge *bridge)
{
	return container_of(bridge, struct xlnx_dp, bridge);
}

void xlnx_dp_write(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

u32 xlnx_dp_read(void __iomem *base, int offset)
{
	return readl(base + offset);
}

void xlnx_dp_set(void __iomem *base, int offset, u32 set)
{
	xlnx_dp_write(base, offset, xlnx_dp_read(base, offset) | set);
}

void xlnx_dp_clr(void __iomem *base, int offset, u32 clr)
{
	xlnx_dp_write(base, offset, xlnx_dp_read(base, offset) & ~clr);
}

void xlnx_dp_vtc_set_timing(struct xlnx_dp *dp,
			    const struct drm_display_mode *mode, u32 vtc_off,
			    u32 ppc)
{
	u32 reg;
	u32 htotal, hactive, hsync_start, hbackporch_start;
	u32 vtotal, vactive, vsync_start, vbackporch_start;
	u32 hsync_len, hfront_porch, hback_porch;
	u32 vsync_len, vfront_porch, vback_porch;

	if (!ppc)
		ppc = 1;

	/*
	 * TODO : For now driver does not support interlace mode
	 * In future  driver may add interlace support
	 */

	/*
	 * Note that pixels-per-clock for video data and timing is non-existent
	 * in the Video Timing Controller. There is only a single set of timing
	 * signals for the video data bus. This means that horizontal (acive
	 * pixles, hsync, hblank) timing settings can be detected and generated
	 * only for a multiple of the pixels-per-clock configured in the system.
	 */
	hactive = mode->hdisplay / ppc;
	hfront_porch = (mode->hsync_start - mode->hdisplay) / ppc;
	hback_porch = (mode->htotal - mode->hsync_end) / ppc;
	hsync_len = (mode->hsync_end - mode->hsync_start) / ppc;
	htotal = hactive + hfront_porch + hsync_len + hback_porch;
	hsync_start = hactive + hfront_porch;
	hbackporch_start = hsync_start + hsync_len;

	vactive = mode->vdisplay;
	vfront_porch = mode->vsync_start - mode->vdisplay;
	vback_porch = mode->vtotal - mode->vsync_end;
	vsync_len = mode->vsync_end - mode->vsync_start;
	vtotal = vactive + vfront_porch + vsync_len + vback_porch;
	vsync_start = vactive + vfront_porch;
	vbackporch_start = vsync_start + vsync_len;

	reg = htotal & XDPTX_VTC_GHSIZE_FRAME_HSIZE;
	xlnx_dp_write(dp->dp_base, vtc_off + XDPTX_VTC_GHSIZE, reg);

	reg = vtotal & XDPTX_VTC_GVSIZE_FRAME_VSIZE;
	reg |= reg << XDPTX_VTC_FIELD1_VSIZE_SHIFT;
	xlnx_dp_write(dp->dp_base, vtc_off + XDPTX_VTC_GVSIZE, reg);

	reg = hactive & XDPTX_VTC_ACTIVE_SIZE_MASK;
	reg |= (vactive & XDPTX_VTC_ACTIVE_SIZE_MASK) <<
		XDPTX_VTC_FIELD1_VSIZE_SHIFT;
	xlnx_dp_write(dp->dp_base, vtc_off + XDPTX_VTC_GASIZE_F0, reg);

	reg = hsync_start & XDPTX_VTC_GHSYNC_START_MASK;
	reg |= (hbackporch_start << XDPTX_VTC_GH1BPSTART_SHIFT) &
		XDPTX_VTC_GHSYNC_END_MASK;
	xlnx_dp_write(dp->dp_base, vtc_off + XDPTX_VTC_GHSYNC, reg);

	reg = vsync_start & XDPTX_VTC_F0_VSYNC_VSTART_MASK;
	reg |= (vbackporch_start << XDPTX_VTC_FIELD1_VSIZE_SHIFT) &
		XDPTX_VTC_F0_VSYNC_VEND_MASK;
	xlnx_dp_write(dp->dp_base, vtc_off + XDPTX_VTC_GVSYNC, reg);
	xlnx_dp_clr(dp->dp_base, vtc_off + XDPTX_VTC_GFENC,
		    XDPTX_VTC_GFENC_MASK);

	/* Calculate and update Generator VBlank Hori field 0 */
	reg = hactive & XDPTX_VTC_F0VBLANK_HSTART_MASK;
	reg |= (hactive << XDPTX_VTC_F0VSYNC_HEND_SHIFT) &
		XDPTX_VTC_F0VBLANK_HEND_MASK;
	xlnx_dp_write(dp->dp_base, vtc_off + XDPTX_VTC_GVBHOFF, reg);

	/* Calculate and update Generator VSync Hori field 0 */
	reg = hsync_start & XDPTX_VTC_F0VBLANK_HSTART_MASK;
	reg |= (hsync_start << XDPTX_VTC_F0VSYNC_HEND_SHIFT) &
		XDPTX_VTC_F0VBLANK_HEND_MASK;
	xlnx_dp_write(dp->dp_base, vtc_off + XDPTX_VTC_GVSHOFF, reg);

	/* sets all polarities as active high */
	xlnx_dp_write(dp->dp_base, vtc_off + XDPTX_VTC_GPOL,
		      XDPTX_VTC_GPOL_MASK);

	/* configure timing source */
	xlnx_dp_set(dp->dp_base, vtc_off + XDPTX_VTC_CTL,
		    XDPTX_VTC_CTL_MASK);
	xlnx_dp_set(dp->dp_base, vtc_off + XDPTX_VTC_CTL,
		    XDPTX_VTC_CTL_RU);
}

/**
 * xlnx_dp_bpc_from_fourcc - derive bits-per-component from a DRM fourcc
 * @dp: DisplayPort IP core structure
 * @drm_fourcc: framebuffer pixel format
 *
 * Return: 8 or 10 for known formats, else the DT default (xlnx,bpc).
 */
u8 xlnx_dp_bpc_from_fourcc(struct xlnx_dp *dp, u32 drm_fourcc)
{
	u8 dt_bpc = dp->config.max_bpc ? dp->config.max_bpc : 8;

	switch (drm_fourcc) {
	/* 8 bits-per-component formats */
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_VUY888:
	case DRM_FORMAT_XVUY8888:
	case DRM_FORMAT_Y8:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_NV16:
		return 8;
	/* 10 bits-per-component formats */
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_XVUY2101010:
	case DRM_FORMAT_Y10:
	case DRM_FORMAT_XV20:
		return 10;
	default:
		dev_dbg(dp->dev,
			"unsupported fourcc 0x%08x, defaulting bpc to DT xlnx,bpc (%u)\n",
			drm_fourcc, dt_bpc);
		return dt_bpc;
	}
}

/**
 * xlnx_dp_fmt_update_bpp - recompute bits-per-pixel for a stream format
 * @stream_fmt: per-stream format state
 */
static void xlnx_dp_fmt_update_bpp(struct xlnx_dp_stream_fmt *stream_fmt)
{
	stream_fmt->bpp = stream_fmt->bpc * stream_fmt->num_colors;
}

/**
 * xlnx_dp_calc_color - derive colour-format programming from a DRM fourcc
 * @dp: DisplayPort IP core structure
 * @drm_fourcc: framebuffer pixel format
 * @stream_fmt: per-stream format state, updated in place
 *
 * Pure helper: only @stream_fmt is modified (misc0 format bits, num_colors,
 * fmt, bpp), so distinct formats can be computed for different MST streams
 * without touching shared driver state.
 */
void xlnx_dp_calc_color(struct xlnx_dp *dp, u32 drm_fourcc,
			struct xlnx_dp_stream_fmt *stream_fmt)
{
	stream_fmt->misc0 &= ~XDPTX_MISC0_FORMAT_MASK;

	switch (drm_fourcc) {
	case DRM_FORMAT_XBGR8888:
		fallthrough;
	case DRM_FORMAT_XRGB8888:
		fallthrough;
	case DRM_FORMAT_BGR888:
		fallthrough;
	case DRM_FORMAT_RGB888:
		fallthrough;
	case DRM_FORMAT_XBGR2101010:
		stream_fmt->misc0 |= XDPTX_MISC0_RGB_MASK;
		stream_fmt->num_colors = 3;
		stream_fmt->fmt = 0x0;
		break;
	case DRM_FORMAT_VUY888:
		fallthrough;
	case DRM_FORMAT_XVUY8888:
		fallthrough;
	case DRM_FORMAT_Y8:
		fallthrough;
	case DRM_FORMAT_XVUY2101010:
		fallthrough;
	case DRM_FORMAT_Y10:
		stream_fmt->misc0 |= XDPTX_MISC0_YCRCB444_MASK;
		stream_fmt->num_colors = 3;
		stream_fmt->fmt = 0x1;
		break;
	case DRM_FORMAT_YUYV:
		fallthrough;
	case DRM_FORMAT_UYVY:
		fallthrough;
	case DRM_FORMAT_NV16:
		fallthrough;
	case DRM_FORMAT_XV20:
		stream_fmt->misc0 |= XDPTX_MISC0_YCRCB422_MASK;
		stream_fmt->num_colors = 2;
		stream_fmt->fmt = 0x2;
		break;
	default:
		dev_dbg(dp->dev,
			"unsupported fourcc 0x%08x, defaulting color format to RGB\n",
			drm_fourcc);
		stream_fmt->misc0 |= XDPTX_MISC0_RGB_MASK;
		stream_fmt->num_colors = 3;
		stream_fmt->fmt = 0x0;
	}
	xlnx_dp_fmt_update_bpp(stream_fmt);
}

/**
 * xlnx_dp_set_color - Set the color format for the SST stream
 * @dp: DisplayPort IP core structure
 * @drm_fourcc: Color string, from xlnx_disp_color_enum
 */
void xlnx_dp_set_color(struct xlnx_dp *dp, u32 drm_fourcc)
{
	xlnx_dp_calc_color(dp, drm_fourcc, &dp->fmt);
}

/**
 * xlnx_dp_init_phy - Initialize the phy
 * @dp: DisplayPort IP core structure
 *
 * Initialize the phy.
 *
 * Return: 0 if the phy instances are initialized correctly, or the error code
 * returned from the callee functions.
 */
static int xlnx_dp_init_phy(struct xlnx_dp *dp)
{
	unsigned int i;
	int ret;

	xlnx_dp_clr(dp->dp_base, XDPTX_PHYCONFIG_REG,
		    XDPTX_PHYCONFIG_ALLRESET_MASK);

	for (i = 0; i < XDPTX_MAX_LANES; i++) {
		ret = phy_init(dp->phy[i]);
		if (ret) {
			dev_err(dp->dev,
				"failed to init phy lane %d\n", i);
			return ret;
		}
	}

	return ret;
}

/**
 * xlnx_dp_exit_phy - Exit the phy
 * @dp: DisplayPort IP core structure
 *
 * Exit the phy.
 */
static void xlnx_dp_exit_phy(struct xlnx_dp *dp)
{
	unsigned int i;
	int ret;

	for (i = 0; i < XDPTX_MAX_LANES; i++) {
		ret = phy_exit(dp->phy[i]);
		if (ret)
			dev_err(dp->dev, "fail to exit phy(%d) %d\n", i, ret);
		dp->phy[i] = NULL;
	}
}

/**
 * xlnx_dp_phy_ready - check if PHY is ready
 * @dp: DisplayPort IP core structure
 *
 * check if PHY is ready. If PHY is not ready, wait 1ms to check for 100 times.
 * This amount of delay was suggested by IP designer.
 *
 * Return: 0 if PHY is ready, or -ENODEV if PHY is not ready.
 */
static int xlnx_dp_phy_ready(struct xlnx_dp *dp)
{
	u32 i, reg, ready;

	if (dp->config.versal_gt_present) {
		/*
		 * Versal Gen 2 series GT Quad path: only bits[5:0] of
		 * PHYSTATUS are driven - bits[3:0] are the per-lane ready
		 * bits and bits[5:4] are GT Quad readiness flags. Bit[6]
		 * (FPGA PLL lock) is not connected on this topology.
		 */
		ready = XDPTX_PHYSTATUS_VERSAL_GT_LANE_MASK;
	} else {
		ready = (1 << XDPTX_MAX_LANES) - 1;
		ready |= XDPTX_PHYSTATUS_FPGAPLLLOCK_MASK;
	}

	/* Wait for 100ms. This should be enough time for PHY to be ready */
	for (i = 0; ; i++) {
		reg = xlnx_dp_read(dp->dp_base, XDPTX_PHYSTATUS_REG);
		if ((reg & ready) == ready)
			return 0;
		if (i == 100) {
			dev_err(dp->dev,
				"PHY isn't ready (PHYSTATUS=0x%08x want=0x%08x)\n",
				reg, ready);
			return -ENODEV;
		}
		usleep_range(1000, 1100);
	}

	return 0;
}

/**
 * xlnx_dp_tx_set_vswing_preemp - This function sets current voltage swing and
 * pre-emphasis level settings from the link_config structure to hardware.
 * @dp: DisplayPort IP core structure
 * @aux_data: Aux_data is a pointer to the array used for preparing a burst
 * write over the AUX channel.
 */
static void xlnx_dp_tx_set_vswing_preemp(struct xlnx_dp *dp, u8 *aux_data)
{
	static const u32 tx_pe_levels[] = { 0x00, 0x0e, 0x14, 0x1b };
	static const u8 tx_vs_levels[] = { 0x2, 0x5, 0x8, 0xf };
	u32 pe_level, vs_level;
	u8 data, i;

	u8 vs_level_rx = dp->tx_link_config.vs_level;
	u8 pe_level_rx = dp->tx_link_config.pe_level;

	pe_level = tx_pe_levels[pe_level_rx];
	vs_level = tx_vs_levels[vs_level_rx];

	/*
	 * Redriver in path requires different voltage-swing and pre-emphasis
	 * values. Below case assumes there is no redriver in the path so
	 * voltage-swing compensation offset is used when pre-emphasis is used.
	 * See the VESA DisplayPort v1.4 Specification, section 3.6.1.1.
	 */
	if (!pe_level_rx)
		vs_level += XDPTX_VS_LEVEL_OFFSET;

	data = (pe_level_rx << XDP_TRAIN_PRE_EMPHASIS_SHIFT) |
		vs_level_rx;

	if (vs_level_rx == XDPTX_VS_PE_LEVEL_MAXCOUNT)
		data |= XDP_TRAIN_MAX_SWING_REACHED;
	if (pe_level_rx == XDPTX_VS_PE_LEVEL_MAXCOUNT)
		data |= XDP_DPCD_TRAINING_LANEX_SET_MAX_PE_MASK;

	memset(aux_data, data, XDPTX_MAX_LANES);

	for (i = 0; i < dp->mode.lane_cnt; i++) {
		xlnx_dp_write(dp->dp_base,
			      XDPTX_PHYPRECURSOR_LANE0_REG + 4 * i, 0x0);
		xlnx_dp_write(dp->dp_base,
			      XDPTX_PHYVOLTAGE_DIFFLANE0_REG + 4 * i, vs_level);
		xlnx_dp_write(dp->dp_base,
			      XDPTX_PHYPOSTCURSOR_LANE0_REG + 4 * i, pe_level);
	}
}

static int config_gt_control_linerate(struct xlnx_dp *dp, int bw_code)
{
	u32 data, regval;

	switch (bw_code) {
	case DP_LINK_BW_1_62:
		data = XDPTX_GTCTL_LINE_RATE_162G;
		break;
	case DP_LINK_BW_2_7:
		data = XDPTX_GTCTL_LINE_RATE_270G;
		break;
	case DP_LINK_BW_5_4:
		data = XDPTX_GTCTL_LINE_RATE_540G;
		break;
	case DP_LINK_BW_8_1:
		data = XDPTX_GTCTL_LINE_RATE_810G;
		break;
	default:
		data = XDPTX_GTCTL_LINE_RATE_810G;
	}

	regval = xlnx_dp_read(dp->dp_base, XDPTX_GTCTL_REG);
	regval &= ~XDPTX_GTCTL_LINE_RATE_MASK;
	regval |= FIELD_PREP(XDPTX_GTCTL_LINE_RATE_MASK, data);
	xlnx_dp_write(dp->dp_base, XDPTX_GTCTL_REG, regval);

	/* Wait for PHY ready */
	return xlnx_dp_phy_ready(dp);
}

static int xlnx_dp_tx_gt_control_init(struct xlnx_dp *dp)
{
	u32 data;
	int ret;

	/*
	 * Versal Gen 2 series silicon: settle the GT before the initial
	 * vswing write.
	 */
	if (dp->config.versal_2ve_2vm)
		usleep_range(500, 600);

	/* Setting initial vswing */
	data = xlnx_dp_read(dp->dp_base, XDPTX_GTCTL_REG);
	data &= ~XDPTX_GTCTL_VSWING_MASK;
	data |= FIELD_PREP(XDPTX_GTCTL_VSWING_MASK, 0x05);
	xlnx_dp_write(dp->dp_base, XDPTX_GTCTL_REG, data);

	xlnx_dp_clr(dp->dp_base, XDPTX_GTCTL_REG, 0x01);
	ret = xlnx_dp_phy_ready(dp);
	if (ret < 0)
		return ret;

	/* Setting initial link rate */
	ret = config_gt_control_linerate(dp, DP_LINK_BW_5_4);
	if (ret) {
		dev_err(dp->dev, "Default Line Rate setting Failed\n");
		return ret;
	}

	return 0;
}

/**
 * xlnx_dp_set_train_patttern - This function sets the training pattern to be
 * used during link training for both the DisplayPort TX core and Rx core.
 * @dp: DisplayPort IP core structure
 * @pattern: pattern selects the pattern to be used. One of the following
 *	- DP_TRAINING_PATTERN_DISABLE
 *	- DP_TRAINING_PATTERN_1
 *	- DP_TRAINING_PATTERN_2
 *	- DP_TRAINING_PATTERN_3
 *	- DP_TRAINING_PATTERN_4
 *
 * Returns:
 *	- 0 on success or negative error code on failure
 */
static int xlnx_dp_set_train_patttern(struct xlnx_dp *dp, u32 pattern)
{
	int ret;
	u8 aux_data[5];

	xlnx_dp_write(dp->dp_base, XDPTX_TRNGPAT_SET_REG, pattern);
	aux_data[0] = pattern;

	/* write scrambler disable to DisplayPort TX core */
	switch (pattern) {
	case DP_TRAINING_PATTERN_DISABLE:
		xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLING_DIS_REG, 0);
		break;
	case DP_TRAINING_PATTERN_1:
	case DP_TRAINING_PATTERN_2:
	case DP_TRAINING_PATTERN_3:
		aux_data[0] |= DP_LINK_SCRAMBLING_DISABLE;
		xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLING_DIS_REG, 1);
		break;
	case DP_TRAINING_PATTERN_4:
		xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLING_DIS_REG, 0);
		break;
	default:
		break;
	}

	/* Make the adjustment to both the DisplayPort TX core and the RX */
	xlnx_dp_tx_set_vswing_preemp(dp, &aux_data[1]);

	if (pattern == DP_TRAINING_PATTERN_DISABLE)
		ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_PATTERN_SET,
					aux_data, 1);
	else
		ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_PATTERN_SET,
					aux_data, 5);

	return ret;
}

/**
 * xlnx_dp_tx_pe_vs_adjust_handler - Calculate and configure pe and vs values
 * @dp: DisplayPort IP core structure
 * @dp_phy_opts: phy configuration structure
 *
 * This function adjusts the pre emphasis and voltage swing values of phy.
 */
static void xlnx_dp_tx_pe_vs_adjust_handler(struct xlnx_dp *dp,
					    struct phy_configure_opts_dp *dp_phy_opts)
{
	u8 preemp = 0, diff_swing = 0;
	u32 data;

	switch (dp_phy_opts->pre[0]) {
	case 0:
		preemp = XVPHY_GTHE4_PREEMP_DP_L0;
		break;
	case 1:
		preemp = XVPHY_GTHE4_PREEMP_DP_L1;
		break;
	case 2:
		preemp = XVPHY_GTHE4_PREEMP_DP_L2;
		break;
	case 3:
		preemp = XVPHY_GTHE4_PREEMP_DP_L3;
		break;
	}

	switch (dp_phy_opts->voltage[0]) {
	case 0:
		switch (dp_phy_opts->pre[0]) {
		case 0:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V0P0;
			break;
		case 1:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V0P1;
			break;
		case 2:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V0P2;
			break;
		case 3:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V0P3;
			break;
		}
		break;
	case 1:
		switch (dp_phy_opts->pre[0]) {
		case 0:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V1P0;
			break;
		case 1:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V1P1;
			break;
		case 2:
			fallthrough;
		case 3:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V1P2;
			break;
		}
		break;
	case 2:
		switch (dp_phy_opts->pre[0]) {
		case 0:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V2P0;
			break;
		case 1:
			fallthrough;
		case 2:
			fallthrough;
		case 3:
			diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V2P1;
			break;
		}
		break;
	case 3:
		diff_swing = XVPHY_GTHE4_DIFF_SWING_DP_V3P0;
		break;
	}

	data = xlnx_dp_read(dp->dp_base, XDPTX_GTCTL_REG);
	data &= ~(XDPTX_GTCTL_POST_CUR_MASK | XDPTX_GTCTL_VSWING_MASK);
	data |= FIELD_PREP(XDPTX_GTCTL_VSWING_MASK, diff_swing) |
		FIELD_PREP(XDPTX_GTCTL_POST_CUR_MASK, preemp);
	xlnx_dp_write(dp->dp_base, XDPTX_GTCTL_REG, data);

	dp_phy_opts->set_voltages = 0;
}

/**
 * xlnx_dp_tx_adj_vswing_preemp - Sets voltage swing and pre-emphasis levels
 * using the adjustment requests obtained from the RX device
 * @dp:Pointer to xlnx_dp structure
 * @link_status: An array of link status register
 *
 * This function sets new voltage swing and pre-emphasis levels using the
 * adjustment requests obtained from the sink.
 *
 * Return: 0 if the new levels were written successfully.
 * error value on failure.
 */
static int xlnx_dp_tx_adj_vswing_preemp(struct xlnx_dp *dp, u8 link_status[6])
{
	struct phy_configure_opts_dp *phy_cfg = &dp->phy_opts.dp;
	int ret;
	u8 i, aux_data[4];
	u8 vs_level_adj_req[4];
	u8 pe_level_adj_req[4];
	u8 max_lanes = dp->link_config.max_lanes;

	/*
	 * Analyze the adjustment requests for changes in voltage swing and
	 * pre-emphasis levels.
	 */
	vs_level_adj_req[0] = FIELD_GET(DP_ADJUST_VOLTAGE_SWING_LANE0_MASK,
					link_status[4]);
	vs_level_adj_req[1] = FIELD_GET(DP_ADJUST_VOLTAGE_SWING_LANE1_MASK,
					link_status[4]);
	vs_level_adj_req[2] = FIELD_GET(DP_ADJUST_VOLTAGE_SWING_LANE0_MASK,
					link_status[5]);
	vs_level_adj_req[3] = FIELD_GET(DP_ADJUST_VOLTAGE_SWING_LANE1_MASK,
					link_status[5]);
	pe_level_adj_req[0] = FIELD_GET(DP_ADJUST_PRE_EMPHASIS_LANE0_MASK,
					link_status[4]);
	pe_level_adj_req[1] = FIELD_GET(DP_ADJUST_PRE_EMPHASIS_LANE1_MASK,
					link_status[4]);
	pe_level_adj_req[2] = FIELD_GET(DP_ADJUST_PRE_EMPHASIS_LANE0_MASK,
					link_status[5]);
	pe_level_adj_req[3] = FIELD_GET(DP_ADJUST_PRE_EMPHASIS_LANE1_MASK,
					link_status[5]);

	/*
	 * change the drive settings to match the adjustment requests. Use the
	 * greatest level requested.
	 */
	dp->tx_link_config.vs_level = 0;
	dp->tx_link_config.pe_level = 0;
	for (i = 0; i < dp->mode.lane_cnt ; i++) {
		if (vs_level_adj_req[i] > dp->tx_link_config.vs_level)
			dp->tx_link_config.vs_level = vs_level_adj_req[i];
		if (pe_level_adj_req[i] > dp->tx_link_config.pe_level)
			dp->tx_link_config.pe_level = pe_level_adj_req[i];
	}

	/*
	 * Verify that the voltage swing and pre-emphasis combination is
	 * allowed. Some combinations will result in differential peak-to-peak
	 * voltage that is outside the permissible range. See the VESA
	 * DisplayPort v1.4 Specification, section 3.1.5.2.
	 * The valid combinations are:
	 *      PE=0    PE=1    PE=2    PE=3
	 * VS=0 Valid   Valid   Valid   Valid
	 * VS=1 Valid   Valid   Valid
	 * VS=2 Valid   Valid
	 * VS=3 Valid
	 */
	if (dp->tx_link_config.pe_level > (4 - dp->tx_link_config.vs_level))
		dp->tx_link_config.pe_level = 4 - dp->tx_link_config.vs_level;

	/*
	 * Make the adjustments to both the DisplayPort TX core and the RX
	 * device.
	 */
	xlnx_dp_tx_set_vswing_preemp(dp, aux_data);
	/*
	 * Write the voltage swing and pre-emphasis levels for each lane to the
	 * RX device.
	 */
	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET,
				&aux_data[0], 4);
	if (ret < 0)
		return ret;

	phy_cfg->lanes = max_lanes;
	phy_cfg->pre[0] = dp->tx_link_config.pe_level;
	phy_cfg->voltage[0] = dp->tx_link_config.vs_level;
	phy_cfg->set_voltages = 1;

	if (!dp->config.versal_gt_present)
		phy_configure(dp->phy[0], &dp->phy_opts);
	else
		xlnx_dp_tx_pe_vs_adjust_handler(dp, &dp->phy_opts.dp);

	return 0;
}

/**
 * xlnx_dp_check_clock_recovery - This function checks if the RX device's DPCD
 * indicates that the clock recovery sequence during link training was
 * successful - the RX device's link clock and data recovery unit has realized
 * and maintained the frequency lock for all the lanes currently in use.
 * @dp: DisplayPort IP core structure
 * @lane_cnt: Number of lanes in use
 *
 * Return: 0 if the RX device's clock recovery PLL has achieved frequency lock
 * for all the lanes in use.Othewise returns error value.
 */
static int xlnx_dp_check_clock_recovery(struct xlnx_dp *dp, u8 lane_cnt)
{
	struct xlnx_dp_link_config *link_config = &dp->link_config;
	int ret;
	u8 link_status[DP_LINK_STATUS_SIZE];

	ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
	if (ret < 0)
		return ret;

	switch (lane_cnt) {
	case XDPTX_DP_LANE_COUNT_4:
		if (!(link_status[0] & XDPTX_DPCD_LANE02_CRDONE_MASK)) {
			link_config->cr_done_cnt = XDPTX_LANE0_CRDONE_MASK;
			return 1;
		}
		if (!(link_status[0] & XDPTX_DPCD_LANE13_CRDONE_MASK)) {
			link_config->cr_done_cnt = XDPTX_LANE1_CRDONE_MASK;
			return 1;
		}
		if (!(link_status[1] & XDPTX_DPCD_LANE02_CRDONE_MASK)) {
			link_config->cr_done_cnt = XDPTX_LANE2_CRDONE_MASK;
			return 1;
		}
		if (!(link_status[1] & XDPTX_DPCD_LANE13_CRDONE_MASK)) {
			link_config->cr_done_cnt = XDPTX_LANE3_CRDONE_MASK;
			return 1;
		}
		link_config->cr_done_cnt = 0x4;
		fallthrough;
	case XDPTX_DP_LANE_COUNT_2:
		if (!(link_status[0] & XDPTX_DPCD_LANE02_CRDONE_MASK)) {
			link_config->cr_done_cnt = 0x0;
			return 1;
		}
		if (!(link_status[0] & XDPTX_DPCD_LANE13_CRDONE_MASK)) {
			link_config->cr_done_cnt = 0x1;
			return 1;
		}
		link_config->cr_done_cnt = 0x2;
		fallthrough;
	case XDPTX_DP_LANE_COUNT_1:
		if (!(link_status[0] & XDPTX_DPCD_LANE02_CRDONE_MASK)) {
			link_config->cr_done_cnt = 0x0;
			return 1;
		}
		link_config->cr_done_cnt = 0x1;
		break;
	default:
		break;
	}

	return 0;
}

static int xlnx_dp_set_linkrate(struct xlnx_dp *dp, u8 bw_code)
{
	struct phy_configure_opts_dp *phy_cfg = &dp->phy_opts.dp;
	int ret;
	u32 lrate_val = 0, val;
	u8 lane_count = dp->mode.lane_cnt;

	if (!xlnx_dp_txconnected(dp)) {
		dev_dbg(dp->dev, "display is not connected");
		return connector_status_disconnected;
	}

	dp->mode.bw_code = bw_code;

	switch (bw_code) {
	case DP_LINK_BW_1_62:
		phy_cfg->link_rate = XDPTX_REDUCED_BIT_RATE / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_162G;
		break;
	case DP_LINK_BW_2_7:
		phy_cfg->link_rate = XDPTX_HIGH_BIT_RATE_1 / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_270G;
		break;
	case DP_LINK_BW_5_4:
		phy_cfg->link_rate = XDPTX_HIGH_BIT_RATE_2 / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_540G;
		break;
	case DP_LINK_BW_8_1:
		phy_cfg->link_rate = XDPTX_HIGH_BIT_RATE_3 / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_810G;
		break;
	default:
		phy_cfg->link_rate = XDPTX_HIGH_BIT_RATE_3 / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_810G;
	}

	/*
	 * set the clock frequency for the DisplayPort PHY corresponding
	 * to a desired link rate
	 */
	val = xlnx_dp_read(dp->dp_base, XDPTX_ENABLE_REG);
	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 0);
	if (val)
		xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 1);
	/* Wait for PHY ready */
	ret = xlnx_dp_phy_ready(dp);
	if (ret < 0)
		return ret;

	/* write new link rate to the DisplayPort TX core */
	xlnx_dp_write(dp->dp_base, XDPTX_LINKBW_SET_REG, bw_code);
	/* write new link rate to the RX device */
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LINK_BW_SET, bw_code);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set DP bandwidth\n");
		return ret;
	}
	/* configure video phy controller to new link rate */
	phy_cfg->set_rate = 1;
	phy_cfg->lanes = lane_count;
	if (!dp->config.versal_gt_present)
		phy_configure(dp->phy[0], &dp->phy_opts);

	if (dp->config.versal_gt_present) {
		val = xlnx_dp_read(dp->dp_base, XDPTX_GTCTL_REG);
		val &= ~XDPTX_GTCTL_LINE_RATE_MASK;
		val |= FIELD_PREP(XDPTX_GTCTL_LINE_RATE_MASK, lrate_val);
		xlnx_dp_write(dp->dp_base, XDPTX_GTCTL_REG, val);
	}

	return 0;
}

static int xlnx_dp_set_lanecount(struct xlnx_dp *dp, u8 lane_cnt)
{
	int ret;
	u8 data;

	dp->mode.lane_cnt = lane_cnt;
	xlnx_dp_write(dp->dp_base, XDPTX_LANECNT_SET_REG, lane_cnt);

	ret = drm_dp_dpcd_readb(&dp->aux, DP_LANE_COUNT_SET, &data);
	if (ret < 0) {
		dev_err(dp->dev, "DPCD read retry fails");
		return ret;
	}

	data &= ~XDPTX_DPCD_LANECNT_SET_MASK;
	data |= dp->mode.lane_cnt;
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LANE_COUNT_SET, data);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return ret;
	}

	return 0;
}

/**
 * xlnx_dp_check_link_status - This function checks if the receiver has
 * achieved and maintained clock recovery, channel equalization, symbol lock, and
 * interlane alignment for all lanes currently in use
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if link status is successfully.
 * error value on failure.
 */
static int xlnx_dp_check_link_status(struct xlnx_dp *dp)
{
	int ret;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 retry = 0;

	memset(link_status, 0, sizeof(link_status));

	if (!xlnx_dp_txconnected(dp)) {
		dev_dbg(dp->dev, "display is not connected");
		return -ENOTCONN;
	}

	for (retry = 0; retry < 5; retry++) {
		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		if (drm_dp_clock_recovery_ok(link_status, dp->mode.lane_cnt) ||
		    drm_dp_channel_eq_ok(link_status, dp->mode.lane_cnt))
			return 0;
	}

	return -EINVAL;
}

static int xlnx_dp_post_training(struct xlnx_dp *dp)
{
	int ret;
	u8 data;

	if (dp->dpcd[DP_DPCD_REV] == XDPTX_V1_4) {
		ret = drm_dp_dpcd_readb(&dp->aux, DP_LANE_COUNT_SET, &data);
		if (ret < 0) {
			dev_dbg(dp->dev, "DPCD read first try fails");
			ret = drm_dp_dpcd_readb(&dp->aux,
						DP_LANE_COUNT_SET, &data);
			if (ret < 0) {
				dev_err(dp->dev, "DPCD read retry fails");
				return ret;
			}
		}

		/*
		 * Post Link Training: bit5 (0x20) of LANE_COUNT_SET grants the
		 * POST_LT_ADJ_REQ sequence to the downstream DPRX.  Never grant
		 * it: granting obliges the source to run a post-LT adjust loop
		 * (poll ADJUST_REQUEST_POST_LT and re-drive VS/PE until the sink
		 * clears it), which this driver does not implement, so a
		 * granted-but-unserviced request leaves the sink main stream
		 * un-synced (RX_PORT0 never locks, MSA stays empty -> no signal).
		 */
		data &= ~0x20;
		ret = drm_dp_dpcd_writeb(&dp->aux, DP_LANE_COUNT_SET, data);
		if (ret < 0) {
			dev_dbg(dp->dev, "DPCD write first try fails");
			ret = drm_dp_dpcd_writeb(&dp->aux,
						 DP_LANE_COUNT_SET, data);
			if (ret < 0) {
				dev_err(dp->dev, "DPCD write retry fails");
				return ret;
			}
		}
	}

	ret = xlnx_dp_check_link_status(dp);
	if (ret < 0)
		return ret;

	ret = xlnx_dp_set_train_patttern(dp, DP_TRAINING_PATTERN_DISABLE);
	if (ret < 0) {
		dev_err(dp->dev, "failed to disable training pattern\n");
		return ret;
	}

	return 0;
}

/**
 * xlnx_dp_link_train_cr - Train clock recovery
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if clock recovery train is done successfully, or corresponding
 * error code.
 */
static int xlnx_dp_link_train_cr(struct xlnx_dp *dp)
{
	struct xlnx_dp_tx_link_config *link_config = &dp->tx_link_config;
	struct xlnx_dp_link_config *config = &dp->link_config;
	int ret;
	u16 max_tries;
	u8 prev_vs_level = 0, same_vs_level_count = 0;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	bool cr_done = 0;

	/* start from minimal vs and pe levels */
	dp->tx_link_config.vs_level = 0;
	dp->tx_link_config.pe_level = 0;

	ret = xlnx_dp_set_train_patttern(dp, DP_TRAINING_PATTERN_1);
	if (ret < 0)
		return XLNX_DP_TRAIN_FAILURE;

	/*
	 * 256 loops should be maximum iterations for 4 lanes and 4 values.
	 * So, This loop should exit before 512 iterations
	 */
	for (max_tries = 0; max_tries < 512; max_tries++) {
		/*
		 * Obtain the required delay for clock recovery as specified
		 * by the RX device.
		 * Wait delay specified in TRAINING_AUX_RD_INTERVAL(0x0E)
		 */
		drm_dp_link_train_clock_recovery_delay(&dp->aux, dp->dpcd);
		/*
		 * check if all lanes have realized and maintained the
		 * frequency lock and get adjustment requests.
		 */
		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return XLNX_DP_TRAIN_FAILURE;

		cr_done = xlnx_dp_check_clock_recovery(dp, lane_cnt);
		if (!cr_done)
			return XLNX_DP_TRAIN_CE;
		/*
		 * check if the same voltage swing for each lane has been
		 * used 5 consecutive times.
		 */
		if (prev_vs_level == link_config->vs_level) {
			same_vs_level_count++;
		} else {
			same_vs_level_count = 0;
			prev_vs_level = link_config->vs_level;
		}

		if (same_vs_level_count >= XDPTX_VS_LEVEL_MAXCOUNT)
			break;

		if (link_config->vs_level == XDPTX_VS_PE_LEVEL_MAXCOUNT)
			break;
		/* Adjust the drive settings as requested by the RX device */
		ret = xlnx_dp_tx_adj_vswing_preemp(dp, link_status);
		if (ret < 0)
			return XLNX_DP_TRAIN_FAILURE;
	}

	if (dp->mode.bw_code == DP_LINK_BW_1_62) {
		if (config->cr_done_cnt != 0x4 && config->cr_done_cnt != 0x0) {
			ret = xlnx_dp_set_train_patttern(dp, DP_TRAINING_PATTERN_DISABLE);
			if (ret < 0) {
				dev_err(dp->dev, "failed to disable training pattern\n");
				return ret;
			}

			ret = xlnx_dp_set_linkrate(dp, DP_LINK_BW_8_1);
			if (ret < 0) {
				dev_err(dp->dev, "failed to set link rate\n");
				return ret;
			}

			ret = xlnx_dp_set_lanecount(dp, config->cr_done_cnt);
			if (ret < 0) {
				dev_err(dp->dev, "failed to set lane count\n");
				return ret;
			}
			config->cr_done_oldstate = config->cr_done_cnt;

			return XLNX_DP_TRAIN_CR;
		}
	}

	return XLNX_DP_ADJUST_LINKRATE;
}

/**
 * xlnx_dp_adjust_linkrate - Adjust the link rate
 * @dp: DisplayPort core structure
 *
 * This function is reached if either the clock recovery or the channel
 * equalization process failed during training. As a result, the data rate will
 * be downshifted and training will be re-attempted at the reduced data rate. If
 * the data rate is already at 1.62 Gbps, a downshited in lane count will be
 * attempted.
 *
 * Return: The next training state:
 *	- XLNX_DP_ADJUST_LANECOUNT if the minimal data rate is already in use.
 *	re-attempt training at a reduced lane count.
 *	- XLNX_DP_TRAIN_CR otherwise. Re-attempt training.
 */
static int xlnx_dp_adjust_linkrate(struct xlnx_dp *dp)
{
	int ret;
	u8 bw_code;

	switch (dp->mode.bw_code) {
	case DP_LINK_BW_8_1:
		bw_code = DP_LINK_BW_5_4;
		break;
	case DP_LINK_BW_5_4:
		bw_code = DP_LINK_BW_2_7;
		break;
	case DP_LINK_BW_2_7:
		bw_code = DP_LINK_BW_1_62;
		break;
	default:
		/*
		 * Already at the lowest link rate. Try reducing the lane
		 * count next
		 */
		return XLNX_DP_ADJUST_LANECOUNT;
	}

	ret = xlnx_dp_set_linkrate(dp, bw_code);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set link rate\n");
		return XLNX_DP_TRAIN_FAILURE;
	}

	ret = xlnx_dp_set_lanecount(dp, dp->link_config.cr_done_oldstate);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return XLNX_DP_TRAIN_FAILURE;
	}

	return XLNX_DP_TRAIN_CR;
}

/**
 * xlnx_dp_adjust_lanecount - Adjust the lane count
 * @dp: DisplayPort core structure
 *
 * This function is reached if either the clock recovery or the channel
 * equalization process failed during training. As a result, the number of lanes
 * will be downshifted and training will be re-attempted at this lower lane
 * count.
 *
 * note: Training will be re-attempted with the maximum data rate being used
 * with the reduced lane count to train the main link at the maximum bandwidth
 * possible.
 *
 * Return: The next training state:
 *	- XLNX_DP_TRAIN_FAILURE if only one lane is already in use
 *	- XLNX_DP_TRAIN_CR otherwise. Re-attempt training
 */
static int xlnx_dp_adjust_lanecount(struct xlnx_dp *dp)
{
	int max_rate = dp->link_config.max_rate, ret;
	u8 bw_code = drm_dp_link_rate_to_bw_code(max_rate), lane_cnt;

	switch (dp->mode.lane_cnt) {
	case XDPTX_DP_LANE_COUNT_4:
		lane_cnt = XDPTX_DP_LANE_COUNT_2;
		break;
	case XDPTX_DP_LANE_COUNT_2:
		lane_cnt = XDPTX_DP_LANE_COUNT_1;
		break;
	default:
		dev_err(dp->dev, " Training failed at lowest linkrate and lane count\n");
		return XLNX_DP_TRAIN_FAILURE;
	}

	ret = xlnx_dp_set_linkrate(dp, bw_code);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set link rate\n");
		return XLNX_DP_TRAIN_FAILURE;
	}

	ret = xlnx_dp_set_lanecount(dp, lane_cnt);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return XLNX_DP_TRAIN_FAILURE;
	}

	return XLNX_DP_TRAIN_CR;
}

/**
 * xlnx_dp_link_train_ce - Train channel equalization
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if channel equalization train is done successfully, or
 * corresponding error code.
 */

static int xlnx_dp_link_train_ce(struct xlnx_dp *dp)
{
	struct xlnx_dp_link_config *config = &dp->link_config;
	struct xlnx_dp_mode *mode = &dp->mode;
	int ret;
	u32 i;
	u8 pat, link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	bool ce_done, cr_done;

	if (dp->dpcd[DP_DPCD_REV] == XDPTX_V1_4 &&
	    dp->dpcd[DP_MAX_DOWNSPREAD] & DP_TPS4_SUPPORTED) {
		pat = DP_TRAINING_PATTERN_4;
	} else if (dp->dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED) {
		pat = DP_TRAINING_PATTERN_3;
	} else {
		pat = DP_TRAINING_PATTERN_2;
	}

	ret = xlnx_dp_set_train_patttern(dp, pat);
	if (ret < 0)
		return XLNX_DP_TRAIN_FAILURE;

	for (i = 0; i < DP_MAX_TRAINING_TRIES; i++) {
		/*
		 * Obtain the required delay for channel equalization as
		 * specified by the RX device.
		 */
		drm_dp_link_train_channel_eq_delay(&dp->aux, dp->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return XLNX_DP_TRAIN_FAILURE;

		cr_done = drm_dp_clock_recovery_ok(link_status, lane_cnt);
		if (!cr_done)
			break;
		/*
		 * check if all lanes have accomplished channel equalization,
		 * symbol lock, and interlane alignment.
		 */
		ce_done = drm_dp_channel_eq_ok(link_status, lane_cnt);
		if (ce_done)
			return XLNX_DP_TRAIN_SUCCESS;

		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return XLNX_DP_TRAIN_FAILURE;

		ret = xlnx_dp_tx_adj_vswing_preemp(dp, link_status);
		if (ret != 0)
			return XLNX_DP_TRAIN_FAILURE;
	}
	/*
	 * Tried 5 times with no success. Try a reduced bitrate first, then
	 * reduce the number of lanes.
	 */

	if (!cr_done) {
		/* Down link on CR failure in EQ state */
		config->cr_done_oldstate = config->max_lanes;
		return XLNX_DP_ADJUST_LINKRATE;
	} else if ((mode->lane_cnt == 1) && !ce_done) {
		/* Need to set lanecount for next iter */
		mode->lane_cnt = config->max_lanes;
		config->cr_done_oldstate = config->max_lanes;
		return XLNX_DP_ADJUST_LINKRATE;
	} else if ((mode->lane_cnt > 1) && !ce_done) {
		/* For EQ failure downlink the lane count */
		return XLNX_DP_ADJUST_LANECOUNT;
	}

	config->cr_done_oldstate = config->max_lanes;

	return XLNX_DP_ADJUST_LINKRATE;
}

/**
 * xlnx_dp_run_training - Run the link training process
 * @dp: DisplayPort core structure
 *
 * This function is implemented as a state machine, with each state returning
 * the next state. First, the clock recovery sequence will be run; if successful,
 * the channel equalization sequence will run. If either the clock recovery or
 * channel equalization sequence failed, the link rate or the number of lanes
 * used will be reduced and training will be re-attempted. If training fails
 * at the minimal data rate, 1.62 Gbps with a single lane, training will no
 * longer re-attempt and fail.
 *
 * Return: 0 if the training process succeeded.
 * error value on training failure.
 */
static int xlnx_dp_run_training(struct xlnx_dp *dp)
{
	struct xlnx_dp_link_config *config = &dp->link_config;
	enum xlnx_dp_train_state state = XLNX_DP_TRAIN_CR;
	int ret;

	while (1) {
		switch (state) {
		case XLNX_DP_TRAIN_CR:
			state = xlnx_dp_link_train_cr(dp);
			break;
		case XLNX_DP_TRAIN_CE:
			state = xlnx_dp_link_train_ce(dp);
			break;
		case XLNX_DP_ADJUST_LINKRATE:
			state = xlnx_dp_adjust_linkrate(dp);
			break;
		case XLNX_DP_ADJUST_LANECOUNT:
			state = xlnx_dp_adjust_lanecount(dp);
			break;
		default:
			break;
		}

		if (state == XLNX_DP_TRAIN_SUCCESS) {
			config->cr_done_oldstate = config->max_lanes;
			config->cr_done_cnt = config->max_lanes;
			dev_dbg(dp->dev, "dp training is success !!");
			break;
		} else if (state == XLNX_DP_TRAIN_FAILURE) {
			config->cr_done_oldstate = config->max_lanes;
			config->cr_done_cnt = config->max_lanes;
			goto err_out;
		}

		if (state == XLNX_DP_ADJUST_LINKRATE ||
		    state == XLNX_DP_ADJUST_LANECOUNT) {
			ret = xlnx_dp_set_train_patttern(dp, DP_TRAINING_PATTERN_DISABLE);
			if (ret < 0) {
				dev_err(dp->dev,
					"failed to disable training pattern\n");
				goto err_out;
			}
		}
	}

	return 0;
err_out:
	dev_err(dp->dev, "failed to train the DP link\n");
	return -EIO;
}

static void xlnx_dp_phy_reset(struct xlnx_dp *dp, u32 reset)
{
	u32 phy_val, reg_val;

	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 0);

	/* Preserve the current PHY settings */
	phy_val = xlnx_dp_read(dp->dp_base, XDPTX_PHYCONFIG_REG);

	/* Apply reset */
	reg_val = phy_val | reset;
	xlnx_dp_write(dp->dp_base, XDPTX_PHYCONFIG_REG, reg_val);

	/* Remove reset */
	xlnx_dp_write(dp->dp_base, XDPTX_PHYCONFIG_REG, phy_val);

	/* Wait for the PHY to be ready */
	xlnx_dp_phy_ready(dp);

	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 1);
}

/**
 * xlnx_dp_aux_cmd_submit - Submit aux command
 * @dp: DisplayPort IP core structure
 * @cmd: aux command
 * @addr: aux address
 * @buf: buffer for command data
 * @bytes: number of bytes for @buf
 * @reply: reply code to be returned
 *
 * Submit an aux command. All aux related commands, native or i2c aux
 * read/write, are submitted through this function. The function is mapped to
 * the transfer function of struct drm_dp_aux. This function involves in
 * multiple register reads/writes, thus synchronization is needed, and it is
 * done by drm_dp_helper using @hw_mutex. The calling thread goes into sleep
 * if there's no immediate reply to the command submission. The reply code is
 * returned at @reply if @reply != NULL.
 *
 * Return: 0 if the command is submitted properly, or corresponding error code:
 * -EBUSY when there is any request already being processed
 * -ETIMEDOUT when receiving reply is timed out
 * -EIO when received bytes are less than requested
 */
static int xlnx_dp_aux_cmd_submit(struct xlnx_dp *dp, u32 cmd, u32 addr,
				  u8 *buf, u32 bytes, u8 *reply)
{
	bool is_read = (cmd & XDPTX_AUX_READ_BIT) ? true : false;
	void __iomem *dp_base = dp->dp_base;
	u32 reg, i, bytesleft, address;
	u8 *data;
	u8 no_of_bytes;

	bytesleft = bytes;
	address = addr;

	while (bytesleft > 0) {
		reg = xlnx_dp_read(dp_base, XDPTX_INTR_SIGSTATE_REG);
		if (reg & XDPTX_INTR_SIGREQSTATE)
			return -EBUSY;

		address = addr + (bytes - bytesleft);
		xlnx_dp_write(dp_base, XDPTX_AUX_ADDR_REG, address);

		/* Increment the pointer to the supplied data buffer. */
		data = &buf[bytes - bytesleft];

		if (bytesleft > 16)
			no_of_bytes = 16;
		else
			no_of_bytes = bytesleft;

		bytesleft -= no_of_bytes;
		if (!is_read) {
			for (i = 0; i < no_of_bytes; i++) {
				xlnx_dp_write(dp_base, XDPTX_AUX_WRITEFIFO_REG,
					      data[i]);
			}
		}

		reg = cmd << XDPTX_AUXCMD_SHIFT;
		if (!bytes)
			reg |= XDPTX_AUXCMD_ADDRONLY_MASK;
		else
			reg |= (no_of_bytes - 1) << XDPTX_AUXCMD_BYTES_SHIFT;
		xlnx_dp_write(dp_base, XDPTX_AUXCMD_REG, reg);

		/* Wait for reply to be delivered upto 2ms */
		for (i = 0; ; i++) {
			reg = xlnx_dp_read(dp_base, XDPTX_INTR_SIGSTATE_REG);
			if (reg & XDPTX_INTR_SIGRPLYSTATE)
				break;

			if (reg & XDPTX_INTR_RPLYTIMEOUT ||
			    i == 2)
				return -ETIMEDOUT;

			usleep_range(1000, 1100);
		}

		reg = xlnx_dp_read(dp_base, XDPTX_AUXREPLY_CODE_REG);
		if (reply)
			*reply = reg;

		if (is_read && !reg) {
			reg = xlnx_dp_read(dp_base, XDPTX_AUXREPLY_DATACNT_REG);
			if ((reg & XDPTX_AUXREPLY_DATACNT_MASK) != no_of_bytes)
				return -EIO;

			for (i = 0; i < no_of_bytes; i++) {
				data[i] = xlnx_dp_read(dp_base,
						       XDPTX_AUXREPLY_DATA_REG);
			}
		}
	}

	return 0;
}

static ssize_t
xlnx_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct xlnx_dp *dp = container_of(aux, struct xlnx_dp, aux);
	int ret;
	unsigned int i, iter;

	/* This is to iterate at least 50 msec */
	iter = 50 * 1000 / 400;

	for (i = 0; i < iter; i++) {
		ret = xlnx_dp_aux_cmd_submit(dp, msg->request, msg->address,
					     msg->buffer, msg->size,
					       &msg->reply);
		if (!ret) {
			dev_dbg(dp->dev, "aux %d retries\n", i);
			return msg->size;
		}

		if (!xlnx_dp_txconnected(dp)) {
			dev_info_ratelimited(dp->dev, "no connected aux device\n");
			return -ENODEV;
		}

		usleep_range(3200, 3300);
	}

	dev_info(dp->dev, "failed aux transfer\n");

	return ret;
}

/**
 * xlnx_dp_init_aux - Initialize the DP aux
 * @dp: DisplayPort IP core structure
 *
 * Initialize the DP aux. The aux clock is derived from the axi clock, so
 * this function gets the axi clock frequency and calculates the filter
 * value. Additionally, the interrupts and transmitter are enabled.
 *
 * Return: 0 on success, error value otherwise
 */
static int xlnx_dp_init_aux(struct xlnx_dp *dp)
{
	unsigned long rate;
	u32 reg, w;

	rate = clk_get_rate(dp->axi_lite_clk);
	if (rate < XDPTX_CLKDIV_MHZ) {
		dev_err(dp->dev, "aclk should be higher than 1MHz\n");
		return -EINVAL;
	}

	/* Allowable values for this register are: 8, 16, 24, 32, 40, 48 */
	for (w = 8; w <= 48; w += 8) {
		/* AUX pulse width should be between 0.4 to 0.6 usec */
		if (w >= (4 * rate / 10000000) &&
		    w <= (6 * rate / 10000000))
			break;
	}

	if (w > 48)
		w = 48;

	reg = w << XDPTX_CLKDIV_AUXFILTER_SHIFT;
	reg |= rate / XDPTX_CLKDIV_MHZ;
	xlnx_dp_write(dp->dp_base, XDPTX_CLKDIV_REG, reg);

	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 1);
	return 0;
}

/**
 * xlnx_dp_exit_aux - De-initialize the DP aux
 * @dp: DisplayPort IP core structure
 *
 * De-initialize the DP aux. Disable all interrupts which are enabled
 * through aux initialization, as well as the transmitter.
 */
static void xlnx_dp_exit_aux(struct xlnx_dp *dp)
{
	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 0);
	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, 0xfff);
}

/**
 * xlnx_dp_update_misc - Write the misc registers
 * @dp: DisplayPort IP core structure
 *
 * The misc register values are stored in the structure, and this
 * function applies the values into the registers.
 */
static void xlnx_dp_update_misc(struct xlnx_dp *dp)
{
	if (!dp->colorimetry_through_vsc) {
		xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_MISC0_REG,
			      dp->fmt.misc0);
		xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_MISC1_REG, 0x0);
	} else {
		xlnx_dp_set(dp->dp_base, XDPTX_MAINSTRM_MISC1_REG,
			    XDPTX_MAINSTRM_MISC1_TIMING_IGNORED_MASK);
	}
}

static void xlnx_dp_vsc_pkt_handler(struct xlnx_dp *dp)
{
	struct xlnx_dp_vscpkt *vscpkt = &dp->vscpkt;
	int i;

	if (dp->colorimetry_through_vsc) {
		xlnx_dp_write(dp->dp_base, XDPTX_AUDIO_EXT_DATA(1),
			      vscpkt->header);
		for (i = 0; i < XDPTX_AUDIO_EXT_DATA_2ND_TO_9TH_WORD; i++) {
			xlnx_dp_write(dp->dp_base, XDPTX_AUDIO_EXT_DATA(i + 2),
				      vscpkt->payload[i]);
		}
	}
}

static void xlnx_dp_prepare_vsc(struct xlnx_dp *dp)
{
	struct xlnx_dp_vscpkt *vscpkt = &dp->vscpkt;
	int i;
	u32 payload_data = 0, bpc;

	vscpkt->header = XDPTX_VSC_SDP_PIXELENC_HEADER_MASK;
	payload_data |= dp->fmt.fmt << XDPTX_VSC_SDP_FMT_SHIFT;

	switch (dp->fmt.bpc) {
	case 6:
		bpc = 0x0;
		break;
	case 8:
		bpc = 0x1;
		break;
	case 10:
		bpc = 0x2;
		break;
	case 12:
		bpc = 0x3;
		break;
	case 16:
		bpc = 0x4;
		break;
	default:
		dev_err(dp->dev, "Not supported bpc (%u). fall back to 8bpc\n",
			dp->fmt.bpc);
		bpc = 0x0;
	}

	bpc &= XDPTX_VSC_SDP_BPC_MASK;
	payload_data |= (bpc << XDPTX_VSC_SDP_BPC_SHIFT);
	/* TODO: it has to be dynamic */
	payload_data |= (0x1 << XDPTX_VSC_SDP_DYNAMIC_RANGE_SHIFT);

	dev_dbg(dp->dev, "payload_data 0x%x", payload_data);

	/* population vsc payload */
	for (i = 0; i < 8; i++) {
		if (i == 4) {
			vscpkt->payload[i] = payload_data;
			continue;
		}
		vscpkt->payload[i] = 0x0;
	}
}

/**
 * xlnx_dp_calc_bpc - derive bpc programming into a per-stream format
 * @dp: DisplayPort IP core structure
 * @bpc: requested bits per component
 * @info: display info of the sink this stream drives (NULL: sink not known yet)
 * @stream_fmt: per-stream format state, updated in place (misc0 bpc bits, bpc,
 *	bpp)
 *
 * Pure helper: caps @bpc to the DT ceiling (xlnx,bpc) then the sink limit; a
 * sink whose EDID reports no depth is capped at 8 bpc.
 *
 * Return: 0 on success, or the fallback bpc value on an unsupported request.
 */
u32 xlnx_dp_calc_bpc(struct xlnx_dp *dp, u8 bpc,
		     const struct drm_display_info *info,
		     struct xlnx_dp_stream_fmt *stream_fmt)
{
	unsigned int ret = 0;

	if (dp->config.max_bpc && bpc > dp->config.max_bpc)
		bpc = dp->config.max_bpc;

	if (info) {
		u8 sink_bpc = info->bpc ? info->bpc : 8;

		if (bpc > sink_bpc) {
			dev_dbg(dp->dev,
				"clamping requested bpc (%u) to sink limit (%u)\n",
				bpc, sink_bpc);
			bpc = sink_bpc;
		}
	}

	stream_fmt->misc0 &= ~XDPTX_MISC0_BPC_MASK;
	switch (bpc) {
	case 6:
		stream_fmt->misc0 |= XDPTX_MISC0_BPC6_MASK;
		break;
	case 8:
		stream_fmt->misc0 |= XDPTX_MISC0_BPC8_MASK;
		break;
	case 10:
		stream_fmt->misc0 |= XDPTX_MISC0_BPC10_MASK;
		break;
	case 12:
		stream_fmt->misc0 |= XDPTX_MISC0_BPC12_MASK;
		break;
	case 16:
		stream_fmt->misc0 |= XDPTX_MISC0_BPC16_MASK;
		break;
	default:
		dev_err(dp->dev, "Not supported bpc (%u). fall back to 8bpc\n",
			bpc);
		stream_fmt->misc0 |= XDPTX_MISC0_BPC8_MASK;
		ret = 8;
		bpc = 8;
	}
	stream_fmt->bpc = bpc;
	xlnx_dp_fmt_update_bpp(stream_fmt);

	return ret;
}

/**
 * xlnx_dp_set_bpc - Set bpc value for the SST stream
 * @dp: DisplayPort IP core structure
 * @bpc: bits per component
 *
 * Return: 0 on success, or the fallback bpc value
 */
u32 xlnx_dp_set_bpc(struct xlnx_dp *dp, u8 bpc)
{
	return xlnx_dp_calc_bpc(dp, bpc,
				dp->connector ? &dp->connector->display_info : NULL,
				&dp->fmt);
}

/**
 * xlnx_dp_encoder_mode_set_transfer_unit - Set the transfer unit values
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * Set the transfer unit, and calculate all transfer unit size related values.
 * Calculation is based on DP and IP core specification.
 */
static void
xlnx_dp_encoder_mode_set_transfer_unit(struct xlnx_dp *dp,
				       struct drm_display_mode *mode)
{
	u32 tu = XDPTX_DEF_TRANSFER_UNITSIZE, temp;
	u32 bw, vid_kbytes, avg_bytes_per_tu, init_wait, min_bytes_per_tu;

	/* Use the max transfer unit size (default) */
	xlnx_dp_write(dp->dp_base, XDPTX_TRANSFER_UNITSIZE_REG, tu);

	vid_kbytes = mode->clock * (dp->fmt.bpp / 8);
	bw = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	avg_bytes_per_tu = vid_kbytes * tu / (dp->mode.lane_cnt * (bw / 1000));
	min_bytes_per_tu = avg_bytes_per_tu / 1000;
	xlnx_dp_write(dp->dp_base, XDPTX_MINBYTES_PERTU_REG,
		      min_bytes_per_tu);

	temp = (avg_bytes_per_tu % 1000) * 1024 / 1000;
	xlnx_dp_write(dp->dp_base, XDPTX_FRACBYTES_PERTU_REG, temp);

	init_wait = tu - min_bytes_per_tu;

	/* Configure the initial wait cycle based on transfer unit size */
	if (min_bytes_per_tu <= 4)
		init_wait = tu;
	else if ((dp->fmt.misc0 & XDPTX_MISC0_YCRCB422_MASK) ==
		XDPTX_MISC0_YCRCB422_MASK)
		init_wait = init_wait / 2;

	xlnx_dp_write(dp->dp_base, XDPTX_INIT_WAIT_REG, init_wait);
}

/**
 * xlnx_dp_mode_set_stream_offset - Program main-stream attributes at a register offset
 * @dp: DisplayPort IP core structure
 * @mode: display mode to program
 * @offset: per-stream register offset (0 for the primary/SST stream)
 * @stream_fmt: per-stream pixel format (misc0/bpp); NULL selects the SST
 *	stream format (dp->fmt). MST passes each stream's own format.
 */
void xlnx_dp_mode_set_stream_offset(struct xlnx_dp *dp,
				    const struct drm_display_mode *mode, u32 offset,
				    const struct xlnx_dp_stream_fmt *stream_fmt)
{
	void __iomem *dp_base = dp->dp_base;
	u32 reg, wpl, ppc;
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 bpp = stream_fmt ? stream_fmt->bpp : dp->fmt.bpp;

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_HTOTAL_REG + offset, mode->htotal);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_VTOTAL_REG + offset, mode->vtotal);

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_POL_REG + offset,
		      (!!(mode->flags & DRM_MODE_FLAG_PVSYNC) <<
		      XDPTX_MAINSTRM_POLVSYNC_SHIFT) |
		      (!!(mode->flags & DRM_MODE_FLAG_PHSYNC) <<
		      XDPTX_MAINSTRM_POLHSYNC_SHIFT));

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_HSWIDTH_REG + offset,
		      mode->hsync_end - mode->hsync_start);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_VSWIDTH_REG + offset,
		      mode->vsync_end - mode->vsync_start);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_HRES_REG + offset, mode->hdisplay);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_VRES_REG + offset, mode->vdisplay);

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_HSTART_REG + offset,
		      mode->htotal - mode->hsync_start);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_VSTART_REG + offset,
		      mode->vtotal - mode->vsync_start);

	if (stream_fmt) {
		xlnx_dp_write(dp_base, XDPTX_MAINSTRM_MISC0_REG + offset,
			      stream_fmt->misc0);
		xlnx_dp_write(dp_base, XDPTX_MAINSTRM_MISC1_REG + offset, 0x0);
	} else {
		xlnx_dp_update_misc(dp);
	}

	/*
	 * SST derives pixels-per-clock from the pixel clock; MST uses the
	 * DT-configured pixel packing (xlnx,pixel-mode), which is identical for
	 * every stream.
	 */
	if (dp->mst_mgr.mst_state) {
		ppc = dp->config.pixel_mode ? dp->config.pixel_mode : 1;
	} else {
		if (mode->clock > 530000)
			ppc = 4;
		else if (mode->clock > 270000)
			ppc = 2;
		else
			ppc = 1;
		dp->config.ppc = ppc;
	}
	xlnx_dp_write(dp_base, XDPTX_USER_PIXELWIDTH_REG + offset, ppc);

	xlnx_dp_write(dp_base, XDPTX_M_VID_REG + offset, mode->clock);
	reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	xlnx_dp_write(dp_base, XDPTX_N_VID_REG + offset, reg);

	wpl = (mode->hdisplay * bpp + 15) / 16;
	reg = wpl + wpl % lane_cnt - lane_cnt;
	xlnx_dp_write(dp_base, XDPTX_USER_DATACNTPERLANE_REG + offset, reg);
}

/**
 * xlnx_dp_encoder_mode_set_stream - Configure the SST main stream
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * SST is the primary stream at register offset 0; program it through the
 * shared MSA path.
 */
static void xlnx_dp_encoder_mode_set_stream(struct xlnx_dp *dp,
					    struct drm_display_mode *mode)
{
	xlnx_dp_mode_set_stream_offset(dp, mode, 0, NULL);
}

static void xlnx_dp_mainlink_en(struct xlnx_dp *dp, u8 enable)
{
	xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLER_RESET,
		      XDPTX_SCRAMBLER_RESET_MASK);
	xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_ENABLE_REG, enable);
}

static int xlnx_dp_power_cycle(struct xlnx_dp *dp)
{
	int ret = 0, i;
	u8 value = 0;

	/* sink Power cycle */
	for (i = 0; i < XDPTX_SINK_PWR_CYCLES; i++) {
		ret = drm_dp_dpcd_readb(&dp->aux, DP_SET_POWER, &value);
		if (ret < 0) {
			usleep_range(3000, 4000);
			continue;
		}

		value &= ~DP_SET_POWER_MASK;
		value |= DP_SET_POWER_D3;
		ret = drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, value);

		usleep_range(300, 400);
		value &= ~DP_SET_POWER_MASK;
		value |= DP_SET_POWER_D0;
		ret = drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, value);
		usleep_range(300, 400);
		ret = drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, value);
		if (ret == 1)
			break;
		usleep_range(3000, 4000);
	}

	if (ret < 0) {
		dev_err(dp->dev, "DP aux failed\n");
		return ret;
	}

	return 0;
}

/**
 * xlnx_dp_start - start the DisplayPort core
 * @dp: DisplayPort IP core structure
 *
 * This function trains the DisplayPort link based on the sink capabilities and
 * enables the main link.
 *
 * Return: 0 on success, negative errno if the link could not be brought up.
 */
int xlnx_dp_start(struct xlnx_dp *dp)
{
	struct xlnx_dp_mode *mode = &dp->mode;
	struct xlnx_hdcptx *dptxhdcp = &dp->tx_hdcp;
	int link_rate = dp->link_config.link_rate;
	int ret = 0;
	u32 val, intr_mask;
	u8 data;
	bool enhanced;

	mode->bw_code = drm_dp_link_rate_to_bw_code(link_rate);
	mode->lane_cnt = dp->link_config.lane_count;
	dp->link_config.cr_done_oldstate = dp->link_config.max_lanes;

	xlnx_dp_init_aux(dp);
	if (dp->status != connector_status_connected) {
		dev_info(dp->dev, "Display not connected\n");
		return -ENODEV;
	}

	xlnx_dp_power_cycle(dp);
	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 0);
	/*
	 * Give a bit of time for DP IP after monitor came up and starting
	 * link training
	 */
	msleep(100);
	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 1);

	ret = xlnx_dp_set_linkrate(dp, mode->bw_code);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set link rate\n");
		return ret;
	}

	ret = xlnx_dp_set_lanecount(dp, mode->lane_cnt);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return ret;
	}

	/* Disable MST mode only when there is no active MST session. */
	if (!dp->mst_mgr.mst_state && !dp->mst_enabling) {
		int s;

		ret = drm_dp_dpcd_writeb(&dp->aux, DP_MSTM_CTRL, 0);
		if (ret < 0) {
			dev_dbg(dp->dev, "DPCD write failed");
			return ret;
		}
		xlnx_dp_write(dp->dp_base, XDPTX_MST_CONFIG, 0x0);

		for (s = 0; s < dp->config.num_streams; s++)
			xlnx_dp_write(dp->dp_base,
				      XDPTX_STREAM1_TS_REG +
				      s * XDPTX_STREAM_TS_STRIDE, 0);
	}

	/* Disable main link during training. */
	val = xlnx_dp_read(dp->dp_base, XDPTX_MAINSTRM_ENABLE_REG);
	if (val)
		xlnx_dp_mainlink_en(dp, 0x0);

	/* Disable HPD pulse interrupts during link training */
	intr_mask = xlnx_dp_read(dp->dp_base, XDPTX_INTR_MASK_REG);
	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG,
		      intr_mask | XDPTX_INTR_HPDPULSE_MASK);

	/* Enable clock spreading for both DP TX and RX device */
	drm_dp_dpcd_readb(&dp->aux, DP_DOWNSPREAD_CTRL, &data);
	if (dp->dpcd[DP_MAX_DOWNSPREAD] & 0x1) {
		xlnx_dp_write(dp->dp_base, XDPTX_DOWNSPREAD_CTL_REG, 1);
		data |=  XDPTX_DPCD_SPREAD_AMP_MASK;
	} else {
		xlnx_dp_write(dp->dp_base, XDPTX_DOWNSPREAD_CTL_REG, 0);
		data &= ~XDPTX_DPCD_SPREAD_AMP_MASK;
	}
	drm_dp_dpcd_writeb(&dp->aux, DP_DOWNSPREAD_CTRL, data);

	/* Enahanced framing model */
	drm_dp_dpcd_readb(&dp->aux, DP_LANE_COUNT_SET, &data);
	enhanced = drm_dp_enhanced_frame_cap(dp->dpcd);
	if (enhanced) {
		xlnx_dp_write(dp->dp_base, XDPTX_EFRAME_EN_REG, 1);
		data |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LANE_COUNT_SET, data);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return ret;
	}
	/* set channel encoding */
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				 DP_SET_ANSI_8B10B);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set ANSI 8B/10B encoding\n");
		return ret;
	}
	/* Reset PHY */
	xlnx_dp_phy_reset(dp, XDPTX_PHYCONFIG_RESET_MASK);

	/* Wait for PHY ready */
	ret = xlnx_dp_phy_ready(dp);
	if (ret < 0)
		return ret;

	memset(dp->train_set, 0, XDPTX_MAX_LANES);

	ret = xlnx_dp_run_training(dp);
	if (ret < 0) {
		dev_err(dp->dev, "DP Link Training Failed\n");
		return ret;
	}

	ret = xlnx_dp_post_training(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed post trining settings\n");
		return ret;
	}

	/* Enable HDP interrupts after link training */
	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, intr_mask);

	dev_dbg(dp->dev, "Training done:");

	/* reset the transmitter */
	xlnx_dp_write(dp->dp_base, XDPTX_SOFT_RST,
		      XDPTX_SOFT_RST_VIDEO_STREAM_ALL_MASK |
		      XDPTX_SOFT_RST_HDCP_MASK);
	xlnx_dp_write(dp->dp_base, XDPTX_SOFT_RST, 0x0);

	if (dp->mst_enabling || !dp->mst_mgr.mst_state) {
		if (dp->mst_enabling) {
			xlnx_dp_mainlink_en(dp, 0x1);
		} else {
			xlnx_dp_set(dp->dp_base, dp->vtc_off[0] + XDPTX_VTC_CTL,
				    XDPTX_VTC_CTL_GE);
			xlnx_dp_mainlink_en(dp, 0x1);
		}
	}

	ret = xlnx_dp_check_link_status(dp);
	if (ret < 0) {
		dev_err(dp->dev, "Link is DOWN after main link enabled!\n");
		return ret;
	}
	if (dp->config.hdcp2x_enable || dp->config.hdcp1x_enable) {
		if (dp->config.hdcp2x_enable) {
			xlnx_dp_set(dp->dp_base, XDP_TX_HDCP2x_ENABLE,
				    XDP_TX_HDCP2x_ENABLE_BYPASS_DISABLE_MASK);
		}
		if (dp->config.hdcp1x_enable) {
			val  = xlnx_dp_read(dp->dp_base, XDP_TX_HDCP1X_ENABLE);
			val |= XDP_TX_HDCP1X_ENABLE_BYPASS_DISABLE_MASK;
			xlnx_dp_write(dp->dp_base, XDP_TX_HDCP1X_ENABLE, val);
		}
		ret = xlnx_start_hdcp_engine(dptxhdcp, mode->lane_cnt);
		if (ret < 0) {
			dev_err(dp->dev, "Failed to Start HDCP\n");
			return ret;
		}
	}

	if (dp->colorimetry_through_vsc && !dp->mst_mgr.mst_state) {
		/* program the VSC extended packet */
		xlnx_dp_vsc_pkt_handler(dp);
		/* This ensures that VSC pkt is sent every frame */
		xlnx_dp_set(dp->dp_base, XDPTX_MAINSTRM_MISC0_REG,
			    XDPTX_MAINSTRM_MISC0_EXT_VSYNC_MASK);
	}
	/* enable audio */
	xlnx_dp_set(dp->dp_base, XDPTX_AUDIO_CTRL_REG, 0x1);
	/* EXTPKT_TXD is only serviced for SST VSC colorimetry; mask it otherwise. */
	if (dp->colorimetry_through_vsc && !dp->mst_mgr.mst_state)
		xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, 0);
	else
		xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG,
			      XDPTX_INTR_EXTPKT_TXD_MASK);

	return 0;
}

/**
 * xlnx_dp_hdcp_reset - Reset HDCP module
 * @dp: DisplayPort IP core structure
 *
 * This function resets HDCP cipher engine,
 * protocol state machine and its internal parameters.
 *
 * Return: 0 on success, or the error code returned
 * from the callee functions.
 */
static int xlnx_dp_hdcp_reset(struct xlnx_dp *dp)
{
	struct xlnx_hdcptx *dptxhdcp = &dp->tx_hdcp;
	int ret;

	cancel_delayed_work(&dp->hdcp_cp_irq_work);
	ret = xlnx_hdcp_tx_reset(dptxhdcp);
	if (ret < 0) {
		dev_dbg(dp->dev, "failed to reset HDCP Cipher Engine");
		return ret;
	}

	return 0;
}

/**
 * xlnx_dp_stop - stop the DisplayPort core
 * @dp: DisplayPort IP core structure
 *
 * This function disables the DisplayPort main link and VTC.
 */
static void xlnx_dp_stop(struct xlnx_dp *dp)
{
	struct phy_configure_opts_dp *phy_cfg = &dp->phy_opts.dp;

	xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_ENABLE_REG, 0);

	/* Disabling the audio in dp core */
	xlnx_dp_clr(dp->dp_base, XDPTX_AUDIO_CTRL_REG, 0);

	/* set Vs and Pe to 0, 0 on cable disconnect */
	phy_cfg->pre[0] = 0;
	phy_cfg->voltage[0] = 0;
	phy_cfg->set_voltages = 1;

	if (!dp->config.versal_gt_present)
		phy_configure(dp->phy[0], &dp->phy_opts);
	else
		xlnx_dp_tx_pe_vs_adjust_handler(dp, &dp->phy_opts.dp);

	/* Disable VTC */
	xlnx_dp_clr(dp->dp_base, dp->vtc_off[0] + XDPTX_VTC_CTL,
		    XDPTX_VTC_CTL_GE);
	/* Reset HDCP Engine. */
	if (dp->config.hdcp2x_enable || dp->config.hdcp1x_enable)
		xlnx_dp_hdcp_reset(dp);
}

static int xlnx_dp_txconnected(struct xlnx_dp *dp)
{
	u32 status;
	u8 retries = 0;

	do {
		status = xlnx_dp_read(dp->dp_base,
				      XDPTX_INTR_SIGSTATE_REG) & 0x1;
		if (retries > 5)
			return false;
		retries++;
		usleep_range(1000, 1100);
	} while (status == 0);

	return true;
}

/* -------------------------------------------------------------------------
 * SST (Single-Stream Transport) bridge path
 * -------------------------------------------------------------------------
 */
/* MST bring-up attempts per HPD event before hpd_work stops re-queueing. */
#define XDPTX_MST_BRINGUP_MAX_ATTEMPTS	3

static enum drm_connector_status
xlnx_dp_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct xlnx_dp *dp = bridge_to_dp(bridge);
	struct xlnx_dp_link_config *link_config = &dp->link_config;
	struct xlnx_dp_mode *mode = &dp->mode;
	int ret;
	u8 dpcd_ext[DP_RECEIVER_CAP_SIZE];
	u8 max_link_rate, ext_cap_rd = 0, data;

	if (!dp->drm)
		return connector_status_disconnected;

	if (!xlnx_dp_txconnected(dp)) {
		dev_dbg(dp->dev, "Display is not connected");
		goto disconnected;
	}

	/*
	 * While an MST topology is live the per-stream MST connectors carry the
	 * displays; report the SST bridge connector as disconnected so userspace
	 * does not try to light it.
	 */
	if (dp->mst_mgr_init && dp->mst_mgr.mst_state)
		return connector_status_disconnected;

	/* Reading the Ext capability for compliance */
	ret = drm_dp_dpcd_read(&dp->aux, DP_DP13_DPCD_REV, dpcd_ext,
			       sizeof(dpcd_ext));
	if ((dp->dpcd[6] & 0x1) == 0x1) {
		ret = drm_dp_dpcd_read(&dp->aux, DP_DOWNSTREAM_PORT_0,
				       dpcd_ext, sizeof(dpcd_ext));
	}

	ret = drm_dp_dpcd_read(&dp->aux, DP_DPCD_REV, dp->dpcd,
			       sizeof(dp->dpcd));
	if (ret < 0) {
		dev_dbg(dp->dev, "DPCD read first try fails");
		ret = drm_dp_dpcd_read(&dp->aux, DP_DPCD_REV, dp->dpcd,
				       sizeof(dp->dpcd));
		if (ret < 0) {
			dev_info(dp->dev, "DPCD read failes");
			goto disconnected;
		}
	}
	/* set MaxLinkRate to TX rate, if sink provides a non-standard value */
	if (dp->dpcd[DP_MAX_LINK_RATE] != DP_LINK_BW_8_1 &&
	    dp->dpcd[DP_MAX_LINK_RATE] != DP_LINK_BW_5_4 &&
	    dp->dpcd[DP_MAX_LINK_RATE] != DP_LINK_BW_2_7 &&
	    dp->dpcd[DP_MAX_LINK_RATE] != DP_LINK_BW_1_62) {
		dp->dpcd[DP_MAX_LINK_RATE] = DP_LINK_BW_8_1;
	}

	if (dp->dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
	    DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT) {
		ret = drm_dp_dpcd_read(&dp->aux, DP_DP13_MAX_LINK_RATE,
				       &max_link_rate, 1);
		if (ret < 0) {
			dev_dbg(dp->dev, "DPCD read failed");
			goto disconnected;
		}

		if (max_link_rate == DP_LINK_BW_8_1)
			dp->dpcd[DP_MAX_LINK_RATE] = DP_LINK_BW_8_1;

		/* compliance: UCD400 required reading these extended registers */
		ret = drm_dp_dpcd_read(&dp->aux, DP_DP13_MAX_LINK_RATE,
				       &ext_cap_rd, 1);
		ret = drm_dp_dpcd_read(&dp->aux, DP_SINK_COUNT_ESI,
				       &ext_cap_rd, 1);
		ret = drm_dp_dpcd_read(&dp->aux,
				       DP_DEVICE_SERVICE_IRQ_VECTOR_ESI0,
				       &ext_cap_rd, 1);
		ret = drm_dp_dpcd_read(&dp->aux, DP_LANE0_1_STATUS_ESI,
				       &ext_cap_rd, 1);
		ret = drm_dp_dpcd_read(&dp->aux, DP_LANE2_3_STATUS_ESI,
				       &ext_cap_rd, 1);
		ret = drm_dp_dpcd_read(&dp->aux,
				       DP_LANE_ALIGN_STATUS_UPDATED_ESI,
				       &ext_cap_rd, 1);
		ret = drm_dp_dpcd_read(&dp->aux, DP_SINK_STATUS_ESI,
				       &ext_cap_rd, 1);
		if (ret < 0)
			dev_dbg(dp->dev, "DPCD read fails");
	}

	link_config->max_rate = min_t(int,
				      drm_dp_max_link_rate(dp->dpcd),
				      dp->config.max_link_rate);
	link_config->max_lanes = min_t(u8,
				       drm_dp_max_lane_count(dp->dpcd),
				       dp->config.max_lanes);
	link_config->link_rate = link_config->max_rate;
	link_config->lane_count = link_config->max_lanes;
	mode->lane_cnt = link_config->max_lanes;
	mode->bw_code = drm_dp_link_rate_to_bw_code(link_config->link_rate);

	ret = drm_dp_dpcd_readb(&dp->aux, DP_DPRX_FEATURE_ENUMERATION_LIST,
				&data);
	if (ret < 0) {
		dev_dbg(dp->dev, "DPCD read failed");
		goto disconnected;
	}
	dp->status = connector_status_connected;

	if (data & DP_VSC_SDP_EXT_FOR_COLORIMETRY_SUPPORTED)
		dp->colorimetry_through_vsc = true;
	else
		dp->colorimetry_through_vsc = false;

	if (dp->mst_mode && dp->mst_mgr_init &&
	    !dp->mst_enabling) {
		u8 mstm_cap = 0;

		drm_dp_dpcd_readb(&dp->aux, DP_MSTM_CAP, &mstm_cap);
		if (mstm_cap & DP_MST_CAP) {
			if (dp->mst_bringup_failures < XDPTX_MST_BRINGUP_MAX_ATTEMPTS) {
				dev_dbg(dp->dev,
					"detect: MST sink, scheduling MST bring-up\n");
				schedule_delayed_work(&dp->hpd_work, 0);
			}
			return connector_status_disconnected;
		}
	}

	if (dp->enabled) {
		xlnx_dp_stop(dp);
		xlnx_dp_start(dp);
	}

	return connector_status_connected;
disconnected:
	dp->status = connector_status_disconnected;
	if (dp->enabled)
		xlnx_dp_stop(dp);

	return connector_status_disconnected;
}

static const struct drm_edid *
xlnx_dp_bridge_edid_read(struct drm_bridge *bridge,
			 struct drm_connector *connector)
{
	struct xlnx_dp *dp = bridge_to_dp(bridge);
	const struct drm_edid *edid;

	if (!dp->drm)
		return NULL;

	edid = drm_edid_read_ddc(connector, &dp->aux.ddc);
	dp->have_edid = !!edid;

	return edid;
}

static enum drm_mode_status
xlnx_dp_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	struct xlnx_dp *dp = bridge_to_dp(bridge);
	u8 max_lanes = dp->link_config.max_lanes;
	u8 bpp = dp->fmt.bpp;
	int max_rate = dp->link_config.max_rate;
	int rate;

	if (mode->clock > XDPTX_MAX_FREQ) {
		dev_info(dp->dev, "filtered the mode, %s,for high pixel rate\n",
			 mode->name);
		drm_mode_debug_printmodeline(mode);
		return MODE_CLOCK_HIGH;
	}

	/* check with link rate and lane count */
	rate = XDPTX_MAX_RATE(max_rate, max_lanes, bpp);
	if (mode->clock > rate) {
		dev_dbg(dp->dev, "filtered the mode, %s,for high pixel rate\n",
			mode->name);
		drm_mode_debug_printmodeline(mode);

		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

static int xlnx_dp_get_eld(struct device *dev, u8 *buf, size_t len)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);
	struct drm_connector *connector;
	size_t size;
	int ret = 0;

	if (!dp->have_edid)
		return -EIO;

	/* Keep unbind from clearing dp->connector while we read it. */
	mutex_lock(&dp->conn_lock);
	connector = dp->connector;
	if (!connector) {
		ret = -EIO;
		goto out;
	}

	mutex_lock(&connector->eld_mutex);
	size = drm_eld_size(connector->eld);
	if (size) {
		if (len < size)
			size = len;
		memcpy(buf, connector->eld, size);
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&connector->eld_mutex);
out:
	mutex_unlock(&dp->conn_lock);

	return ret;
}

static unsigned long dp_codec_spk_mask_from_alloc(int spk_alloc)
{
	int i;
	static const unsigned long dp_codec_eld_spk_alloc_bits[] = {
		[0] = FL | FR, [1] = LFE, [2] = FC, [3] = RL | RR,
		[4] = RC, [5] = FLC | FRC, [6] = RLC | RRC,
	};
	unsigned long spk_mask = 0;

	for (i = 0; i < ARRAY_SIZE(dp_codec_eld_spk_alloc_bits); i++) {
		if (spk_alloc & (1 << i))
			spk_mask |= dp_codec_eld_spk_alloc_bits[i];
	}

	return spk_mask;
}

static int dp_codec_get_ch_alloc_table_idx(u8 *eld, u8 channels)
{
	int i;
	u8 spk_alloc;
	unsigned long spk_mask;
	const struct dp_codec_cea_spk_alloc *cap = dp_codec_channel_alloc;

	spk_alloc = drm_eld_get_spk_alloc(eld);
	spk_mask = dp_codec_spk_mask_from_alloc(spk_alloc);

	for (i = 0; i < ARRAY_SIZE(dp_codec_channel_alloc); i++, cap++) {
		/* If spk_alloc == 0, DP is unplugged return stereo config */
		if (!spk_alloc && cap->ca_id == 0)
			return i;
		if (cap->n_ch != channels)
			continue;
		if (!(cap->mask == (spk_mask & cap->mask)))
			continue;
		return i;
	}

	return -EINVAL;
}

static int dp_codec_fill_cea_params(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai,
				    unsigned int channels,
				    struct hdmi_audio_infoframe *cea)
{
	int idx;
	int ret;
	u8 eld[MAX_ELD_BYTES];

	ret = xlnx_dp_get_eld(dai->dev, eld, sizeof(eld));
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_eld(substream->runtime, eld);
	if (ret)
		return ret;

	/* Select a channel allocation that matches with ELD and pcm channels */
	idx = dp_codec_get_ch_alloc_table_idx(eld, channels);
	if (idx < 0) {
		dev_err(dai->dev, "Not able to map channels to speakers (%d)\n",
			idx);
		return idx;
	}

	hdmi_audio_infoframe_init(cea);
	cea->channels = channels;
	cea->coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	cea->sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	cea->sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;
	cea->channel_allocation = dp_codec_channel_alloc[idx].ca_id;

	return 0;
}

/**
 * xlnx_tx_pcm_startup - initialize audio during audio usecase
 * @substream: PCM substream
 * @dai: runtime dai data
 *
 * This function is called by ALSA framework before audio
 * playback begins. This callback initializes audio.
 *
 * Return: 0 on success
 */
static int xlnx_tx_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct xlnx_dp *dp = dev_get_drvdata(dai->dev);

	/* Enabling the audio in dp core */
	xlnx_dp_clr(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_EN_MASK);

	xlnx_dp_set(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_EN_MASK);

	return 0;
}

/**
 * xlnx_tx_pcm_hw_params - sets the playback stream properties
 * @substream: PCM substream
 * @params: PCM stream hardware parameters
 * @dai: runtime dai data
 *
 * This function is called by ALSA framework after startup callback
 * packs the audio infoframe from stream paremters and programs ACR
 * block
 *
 * Return: 0 on success
 */
static int xlnx_tx_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct hdmi_audio_infoframe infoframe;
	struct xlnx_dp *dp = dev_get_drvdata(dai->dev);
	int ret;
	u8 infopckt[DP_INFOFRAME_SIZE(AUDIO)] = {0};
	u8 *ptr = (u8 *)dp->tx_audio_data->buffer;

	ret = dp_codec_fill_cea_params(substream, dai,
				       params_channels(params),
				       &infoframe);
	if (ret < 0)
		return ret;

	/* Setting audio channels */
	xlnx_dp_write(dp->dp_base, XDPTX_AUDIO_CHANNELS_REG,
		      infoframe.channels - 1);

	hdmi_audio_infoframe_pack(&infoframe, infopckt,
				  DP_INFOFRAME_SIZE(AUDIO));
	/* Setting audio infoframe packet header. Please refer to PG 299 */
	ptr[0] = 0x00;
	ptr[1] = 0x84;
	ptr[2] = 0x1B;
	ptr[3] = 0x44;
	memcpy((void *)(&ptr[4]), (void *)(&infopckt[4]),
	       (DP_INFOFRAME_SIZE(AUDIO) - DP_INFOFRAME_HEADER_SIZE));

	return 0;
}

/**
 * xlnx_tx_pcm_shutdown - Deinitialze audio when audio usecase is stopped
 * @substream: PCM substream
 * @dai: runtime dai data
 *
 * This function is called by ALSA framework before audio playback usecase
 * ends.
 */
static void xlnx_tx_pcm_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct xlnx_dp *dp = dev_get_drvdata(dai->dev);

	/* Disabling the audio in dp core */
	xlnx_dp_clr(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_EN_MASK);
}

/**
 * xlnx_tx_pcm_digital_mute - mute or unmute audio
 * @dai: runtime dai data
 * @enable: enable or disable mute
 * @direction: direction to enable mute (capture/playback)
 *
 * This function is called by ALSA framework before audio usecase
 * starts and before audio usecase ends
 *
 * Return: 0 on success
 */
static int xlnx_tx_pcm_digital_mute(struct snd_soc_dai *dai, int enable,
				    int direction)
{
	struct xlnx_dp *dp = dev_get_drvdata(dai->dev);

	if (enable)
		xlnx_dp_set(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_MUTE_MASK);
	else
		xlnx_dp_clr(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_MUTE_MASK);

	return 0;
}

static const struct snd_soc_dai_ops xlnx_dp_tx_dai_ops = {
	.startup = xlnx_tx_pcm_startup,
	.hw_params = xlnx_tx_pcm_hw_params,
	.shutdown = xlnx_tx_pcm_shutdown,
	.mute_stream = xlnx_tx_pcm_digital_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver xlnx_dp_tx_dai = {
	.name = "xlnx_dp_tx",
	.playback = {
		.stream_name = "I2S Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = DP_RATES,
		.formats = I2S_FORMATS,
		.sig_bits = 24,
	},
	.ops = &xlnx_dp_tx_dai_ops,
};

static int xlnx_tx_codec_probe(struct snd_soc_component *component)
{
	return 0;
}

static void xlnx_tx_codec_remove(struct snd_soc_component *component)
{
}

static const struct snd_soc_component_driver xlnx_dp_component = {
	.probe = xlnx_tx_codec_probe,
	.remove = xlnx_tx_codec_remove,
};

static int dptx_register_aud_dev(struct device *dev)
{
	return devm_snd_soc_register_component(dev, &xlnx_dp_component,
			&xlnx_dp_tx_dai, 1);
}

static void
xlnx_dp_encoder_atomic_mode_set(struct xlnx_dp *dp, struct drm_crtc *crtc,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	int rate, max_rate = dp->link_config.max_rate;
	u32 clock, drm_fourcc;
	u8 max_lanes = dp->link_config.max_lanes;
	u8 bpc, bpp;

	/*
	 * This assumes that there is no conversion  between framebuffer
	 * and DP Tx
	 */
	drm_fourcc = crtc->primary->state->fb->format->format;

	bpc = xlnx_dp_bpc_from_fourcc(dp, drm_fourcc);
	if (conn_state->max_requested_bpc && bpc > conn_state->max_requested_bpc)
		bpc = conn_state->max_requested_bpc;

	xlnx_dp_set_color(dp, drm_fourcc);
	xlnx_dp_set_bpc(dp, bpc);
	bpp = dp->fmt.bpp;

	rate = XDPTX_MAX_RATE(max_rate, max_lanes, bpp);
	if (mode->clock > rate) {
		dev_err(dp->dev, "the mode, %s,has too high pixel rate\n",
			mode->name);
		drm_mode_debug_printmodeline(mode);
	}

	/* The timing register should be programmed always */
	xlnx_dp_encoder_mode_set_stream(dp, adjusted_mode);
	xlnx_dp_encoder_mode_set_transfer_unit(dp, adjusted_mode);
	clock = adjusted_mode->clock * 1000;
	clk_set_rate(dp->tx_vid_clk[0], clock / dp->config.ppc);

	xlnx_dp_vtc_set_timing(dp, adjusted_mode, dp->vtc_off[0],
			       dp->config.ppc);
	/* prepare a vsc packet */
	xlnx_dp_prepare_vsc(dp);
}

/**
 * xlnx_dp_bridge_enable - Enable the DisplayPort bridge and start the stream
 * @bridge: DRM bridge
 * @state: atomic state being committed
 */
static void xlnx_dp_bridge_enable(struct drm_bridge *bridge,
				  struct drm_atomic_state *state)
{
	struct xlnx_dp *dp = bridge_to_dp(bridge);
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	if (!connector) {
		dev_err(dp->dev, "bridge_enable: no connector for encoder\n");
		return;
	}

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (!conn_state || !conn_state->crtc) {
		dev_err(dp->dev, "bridge_enable: no crtc for connector\n");
		return;
	}
	crtc = conn_state->crtc;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (!crtc_state) {
		dev_err(dp->dev, "bridge_enable: no crtc state for crtc\n");
		return;
	}
	xlnx_dp_encoder_atomic_mode_set(dp, crtc, crtc_state, conn_state);

	if (!dp->enabled) {
		pm_runtime_get_sync(dp->dev);
		dp->enabled = true;
		xlnx_dp_start(dp);
		if (dp->mst_to_sst_relink) {
			dp->mst_to_sst_relink = false;
			xlnx_dp_stop(dp);
			xlnx_dp_start(dp);
		}
	}
}

/**
 * xlnx_dp_bridge_disable - Disable the DisplayPort bridge and stop the stream
 * @bridge: DRM bridge
 * @state: atomic state being committed
 */
static void xlnx_dp_bridge_disable(struct drm_bridge *bridge,
				   struct drm_atomic_state *state)
{
	struct xlnx_dp *dp = bridge_to_dp(bridge);

	if (dp->enabled) {
		dp->enabled = false;
		/* No _sync: HPD work takes modeset locks this commit may hold. */
		cancel_delayed_work(&dp->hpd_work);
		cancel_delayed_work(&dp->hpd_pulse_work);
		xlnx_dp_stop(dp);
		pm_runtime_put_sync(dp->dev);
	}
}

static int xlnx_dp_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct xlnx_dp *dp = bridge_to_dp(bridge);
	int ret;

	if (dp->mst_mode && !dp->mst_mgr_init) {
		ret = xlnx_dp_mst_create_encoders(dp);
		if (ret) {
			dev_err(dp->dev, "MST: encoder creation failed (%d)\n",
				ret);
			return ret;
		}

		mutex_init(&dp->esi_lock);

		ret = drm_dp_mst_topology_mgr_init(&dp->mst_mgr, dp->drm,
						   &dp->aux, 16,
						   dp->config.num_streams, 0);
		if (ret) {
			dev_err(dp->dev, "MST: topology mgr init failed (%d)\n",
				ret);
			mutex_destroy(&dp->esi_lock);
			/* The encoders are DRM-managed; just drop our references. */
			memset(dp->mst_encoders, 0, sizeof(dp->mst_encoders));
			return ret;
		}

		dp->mst_mgr.cbs = &xlnx_dp_mst_cbs;
		dp->mst_mgr_init = true;
		dev_dbg(dp->dev, "MST: topology manager initialised\n");
	}

	return 0;
}

static void xlnx_dp_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct xlnx_dp *dp = bridge_to_dp(bridge);
	u32 intr_mask = xlnx_dp_read(dp->dp_base, XDPTX_INTR_MASK_REG);

	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG,
		      intr_mask & ~(XDPTX_INTR_HPDPULSE_MASK |
				    XDPTX_INTR_HPDEVENT_MASK));
}

static void xlnx_dp_bridge_hpd_disable(struct drm_bridge *bridge)
{
	struct xlnx_dp *dp = bridge_to_dp(bridge);
	u32 intr_mask = xlnx_dp_read(dp->dp_base, XDPTX_INTR_MASK_REG);

	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG,
		      intr_mask | XDPTX_INTR_HPDPULSE_MASK |
		      XDPTX_INTR_HPDEVENT_MASK);
}

static const struct drm_bridge_funcs xlnx_dp_bridge_funcs = {
	.atomic_enable		= xlnx_dp_bridge_enable,
	.atomic_disable		= xlnx_dp_bridge_disable,
	.mode_valid		= xlnx_dp_bridge_mode_valid,
	.edid_read		= xlnx_dp_bridge_edid_read,
	.detect			= xlnx_dp_bridge_detect,
	.attach			= xlnx_dp_bridge_attach,
	.hpd_enable		= xlnx_dp_bridge_hpd_enable,
	.hpd_disable		= xlnx_dp_bridge_hpd_disable,
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
};

static void xlnx_dp_hpd_work_func(struct work_struct *work)
{
	struct xlnx_dp *dp = container_of(work, struct xlnx_dp, hpd_work.work);
	u8 mstm_cap = 0, mstm_ctrl = 0;
	int ret, s;

	if (dp->drm) {
		enum drm_connector_status status;

		status = xlnx_dp_bridge_detect(&dp->bridge, dp->connector);
		drm_bridge_hpd_notify(&dp->bridge, status);
	}

	if (!dp->mst_mode || !dp->mst_mgr_init)
		return;

	/* Sink gone: tear down any active topology. */
	if (!xlnx_dp_txconnected(dp)) {
		if (dp->mst_mgr.mst_state)
			goto teardown;
		return;
	}

	if (drm_dp_dpcd_readb(&dp->aux, DP_MSTM_CAP, &mstm_cap) != 1) {
		dev_dbg(dp->dev, "MST: CAP read failed, keeping current state\n");
		return;
	}

	/* SST sink on an MST-capable port: drop back to SST. */
	if (!(mstm_cap & DP_MST_CAP)) {
		if (dp->mst_mgr.mst_state)
			goto teardown;
		return;
	}

	/* Bring-up retry budget spent: wait for the next HPD event. */
	if (dp->mst_bringup_failures >= XDPTX_MST_BRINGUP_MAX_ATTEMPTS)
		return;

	/* MST-capable sink already live: keep the existing topology. */
	if (dp->mst_mgr.mst_state) {
		drm_dp_dpcd_readb(&dp->aux, DP_MSTM_CTRL, &mstm_ctrl);
		if (mstm_ctrl & DP_MST_EN) {
			dev_dbg(dp->dev,
				"MST: HPD on live session, keeping topology\n");
			return;
		}
		/* Stale session on re-plug: rebuild from scratch. */
		drm_dp_mst_topology_mgr_set_mst(&dp->mst_mgr, false);
		dp->mst_link_trained = false;
		for (s = 0; s < dp->config.num_streams; s++)
			xlnx_dp_clr(dp->dp_base, dp->vtc_off[s] + XDPTX_VTC_CTL,
				    XDPTX_VTC_CTL_GE);
	}

	/* VSC colorimetry is SST-only; keep it disabled for MST. */
	dp->colorimetry_through_vsc = false;

	ret = xlnx_dp_start(dp);
	if (ret < 0) {
		dev_err(dp->dev, "MST: link training failed (%d)\n", ret);
		goto fail;
	}
	dp->mst_link_trained = true;
	dp->mst_fresh_train = true;
	dp->mst_enabling = true;
	dev_dbg(dp->dev, "MST: main link trained (bw=0x%x lanes=%d)\n",
		dp->mode.bw_code, dp->mode.lane_cnt);

	ret = xlnx_dp_mst_enable(dp);
	if (ret < 0) {
		dev_err(dp->dev, "MST: enable failed (%d)\n", ret);
		goto fail;
	}

	ret = xlnx_dp_mst_clear_payload_table(dp);
	if (ret < 0)
		goto fail;

	dp->status = connector_status_connected;

	ret = drm_dp_mst_topology_mgr_set_mst(&dp->mst_mgr, true);
	if (ret) {
		dev_err(dp->dev, "MST: set_mst(true) failed (%d)\n", ret);
		goto fail;
	}
	dev_dbg(dp->dev, "MST: topology discovery started\n");
	dp->mst_bringup_failures = 0;
	dp->mst_enabling = false;
	return;

fail:
	dp->mst_enabling = false;
	if (++dp->mst_bringup_failures == XDPTX_MST_BRINGUP_MAX_ATTEMPTS)
		dev_err(dp->dev, "MST: bring-up failed %d times, waiting for HPD\n",
			XDPTX_MST_BRINGUP_MAX_ATTEMPTS);
	return;

teardown:
	/* Stream count/mask stay with the commit path; disables drain them. */
	drm_dp_mst_topology_mgr_set_mst(&dp->mst_mgr, false);
	dp->mst_link_trained = false;
	for (s = 0; s < dp->config.num_streams; s++)
		xlnx_dp_clr(dp->dp_base, dp->vtc_off[s] + XDPTX_VTC_CTL,
			    XDPTX_VTC_CTL_GE);
	dp->mst_to_sst_relink = true;
	dev_dbg(dp->dev, "MST: topology torn down\n");
}

/**
 * xlnx_dp_hdcp_cp_irq_func - Checks for HDCP information
 * whenever CP IRQ is detected. HDCP transmitters must process this interrupt when
 * they are received from receivers/repeaters.
 * @work: work structure
 *
 * This function checks for HDCP authentication information via rxstatus register
 * as soon as interrupt triggers.
 */
static void xlnx_dp_hdcp_cp_irq_func(struct work_struct *work)
{
	struct xlnx_dp *dp;
	struct xlnx_hdcptx *dptxhdcp;

	dp = container_of(work, struct xlnx_dp, hdcp_cp_irq_work.work);
	dptxhdcp = &dp->tx_hdcp;

	xlnx_hdcp_tx_process_cp_irq(dptxhdcp);
}

/*
 * xlnx_dp_atomic_commit_tail - MST-aware atomic commit tail
 *
 * Waits for in-flight MST payload updates to retire before the standard commit
 * tail, so a new modeset does not race a concurrent stream's VC payload update.
 */
static void xlnx_dp_atomic_commit_tail(struct drm_atomic_state *state)
{
	drm_dp_mst_atomic_wait_for_dependencies(state);
	drm_atomic_helper_commit_tail(state);
}

/*
 * Registered from xlnx_dp_bind() so the DP-MST commit helpers stay confined
 * to the MST-capable component.
 */
static const struct drm_mode_config_helper_funcs xlnx_dp_mode_config_helper_funcs = {
	.atomic_commit_setup	= drm_dp_mst_atomic_setup_commit,
	.atomic_commit_tail	= xlnx_dp_atomic_commit_tail,
};

static void xlnx_dp_bridge_put(struct drm_device *drm, void *bridge)
{
	drm_bridge_put(bridge);
}

static int xlnx_dp_bind(struct device *dev, struct device *master, void *data)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dp->encoder;
	struct drm_connector *connector;
	struct drm_device *drm = data;
	int ret;

	dp->drm = drm;
	drm->mode_config.helper_private = &xlnx_dp_mode_config_helper_funcs;
	dp->aux.drm_dev = drm;
	ret = drm_dp_aux_register(&dp->aux);
	if (ret) {
		dev_err(dp->dev, "failed to register DP aux\n");
		dp->drm = NULL;
		return ret;
	}

	encoder->possible_crtcs = 1;
	/* dp->encoder lives in the bridge allocation; hold it past encoder release. */
	drm_bridge_get(&dp->bridge);
	ret = drmm_add_action_or_reset(drm, xlnx_dp_bridge_put, &dp->bridge);
	if (!ret)
		ret = drmm_encoder_init(drm, encoder, NULL,
					DRM_MODE_ENCODER_TMDS, NULL);
	if (ret) {
		dp->drm = NULL;
		drm_dp_aux_unregister(&dp->aux);
		return ret;
	}

	ret = drm_bridge_attach(encoder, &dp->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(dp->dev, "failed to attach bridge to encoder\n");
		goto error_aux;
	}

	connector = drm_bridge_connector_init(drm, encoder);
	if (IS_ERR(connector)) {
		dev_err(dp->dev, "failed to create bridge connector\n");
		ret = PTR_ERR(connector);
		goto error_aux;
	}
	mutex_lock(&dp->conn_lock);
	WRITE_ONCE(dp->connector, connector);
	mutex_unlock(&dp->conn_lock);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		dev_err(dp->dev, "failed to attach connector to encoder\n");
		goto error_aux;
	}

	drm_atomic_helper_connector_reset(connector);
	if (!connector->state) {
		ret = -ENOMEM;
		goto error_aux;
	}

	xlnx_dp_set_bpc(dp, 8);
	drm_connector_attach_max_bpc_property(connector, 8, 12);
	drm_object_attach_property(&connector->base,
				   connector->dev->mode_config.gen_hdr_output_metadata_property, 0);

	/* This enables interrupts, so should be called after DRM init */
	ret = xlnx_dp_init_aux(dp);
	if (ret) {
		dev_err(dp->dev, "failed to initialize DP aux");
		goto error_aux;
	}

	if (dp->mst_mode && dp->mst_mgr_init &&
	    xlnx_dp_txconnected(dp)) {
		dev_dbg(dp->dev,
			"MST: sink already connected at bind, scheduling hpd_work\n");
		schedule_delayed_work(&dp->hpd_work, 0);
	}

	return 0;

error_aux:
	/* No unbind follows a failed bind: undo what bridge attach set up. */
	if (dp->mst_mgr_init) {
		drm_dp_mst_topology_mgr_destroy(&dp->mst_mgr);
		mutex_destroy(&dp->esi_lock);
		memset(dp->mst_encoders, 0, sizeof(dp->mst_encoders));
		dp->mst_mgr_init = false;
	}
	/* The connector and encoder are drmm-managed; just drop our references. */
	mutex_lock(&dp->conn_lock);
	WRITE_ONCE(dp->connector, NULL);
	mutex_unlock(&dp->conn_lock);
	dp->drm = NULL;
	drm_dp_aux_unregister(&dp->aux);

	return ret;
}

static void xlnx_dp_unbind(struct device *dev,
			   struct device *master, void *data)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);
	struct drm_device *drm = data;

	/* Mask all interrupts so the live IRQ cannot re-queue the HPD work */
	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, ~0);
	/* Wait out any threaded handler already running before dropping the connector. */
	synchronize_irq(dp->irq);
	/* hpd_pulse_work can re-queue hpd_work, so cancel it first. */
	cancel_delayed_work_sync(&dp->hpd_pulse_work);
	cancel_delayed_work_sync(&dp->hpd_work);
	if (dp->mst_mode && dp->mst_mgr_init) {
		drm_dp_mst_topology_mgr_set_mst(&dp->mst_mgr, false);
		dp->mst_link_trained = false;
		dp->mst_stream_count = 0;
		dp->mst_active_mask = 0;
		drm_dp_mst_topology_mgr_destroy(&dp->mst_mgr);
		mutex_destroy(&dp->esi_lock);
		dp->mst_mgr_init = false;
	}

	/* The drmm connector/encoder outlive unbind; stop probes reaching the HW. */
	mutex_lock(&drm->mode_config.mutex);
	dp->drm = NULL;
	mutex_lock(&dp->conn_lock);
	WRITE_ONCE(dp->connector, NULL);
	mutex_unlock(&dp->conn_lock);
	mutex_unlock(&drm->mode_config.mutex);

	xlnx_dp_exit_aux(dp);
	drm_dp_aux_unregister(&dp->aux);
}

static void xlnx_dp_hpd_pulse_work_func(struct work_struct *work)
{
	struct xlnx_dp *dp;
	int ret;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 bw_set, lane_set;

	dp = container_of(work, struct xlnx_dp, hpd_pulse_work.work);

	/*
	 * On an MST-capable port that is not yet in MST, an HPD pulse from an
	 * MST sink is the trigger to bring up the topology; hand off to
	 * hpd_work and stop here.
	 */
	if (dp->mst_mode && dp->mst_mgr_init &&
	    !dp->mst_mgr.mst_state && !dp->mst_enabling &&
	    xlnx_dp_txconnected(dp)) {
		u8 mstm_cap = 0;

		drm_dp_dpcd_readb(&dp->aux, DP_MSTM_CAP, &mstm_cap);
		if (mstm_cap & DP_MST_CAP) {
			schedule_delayed_work(&dp->hpd_work, 0);
			return;
		}
	}

	if (dp->mst_mgr.mst_state || dp->mst_enabling) {
		if (!xlnx_dp_txconnected(dp))
			return;
		if (dp->mst_mgr.mst_state)
			xlnx_dp_mst_process_esi(dp);
		return;
	}

	if (!dp->enabled)
		return;

	if (!xlnx_dp_txconnected(dp)) {
		dev_err(dp->dev, "incorrect HPD pulse received\n");
		return;
	}

	/* Read Link information from downstream device */
	ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
	if (ret < 0)
		return;
	ret |= drm_dp_dpcd_read(&dp->aux, DP_LINK_BW_SET, &bw_set, 1);
	ret |= drm_dp_dpcd_read(&dp->aux, DP_LANE_COUNT_SET, &lane_set, 1);
	if (ret < 0)
		return;

	bw_set &= DP_LINK_BW_SET_MASK;
	if (bw_set != DP_LINK_BW_8_1 &&
	    bw_set != DP_LINK_BW_5_4 &&
	    bw_set != DP_LINK_BW_2_7 &&
	    bw_set != DP_LINK_BW_1_62)
		goto retrain_link;

	lane_set &= DP_LANE_COUNT_MASK;
	if (lane_set != 1 && lane_set != 2 && lane_set != 4)
		goto retrain_link;

	/* Verify the link status */
	ret = drm_dp_channel_eq_ok(link_status, lane_set);
	if (!ret)
		goto retrain_link;

	if (dp->config.hdcp2x_enable || dp->config.hdcp1x_enable)
		schedule_delayed_work(&dp->hdcp_cp_irq_work, 0);

	return;

retrain_link:
	xlnx_dp_stop(dp);
	xlnx_dp_start(dp);
}

static void xlnx_dp_gen_drmif_pkt(struct xlnx_dp *dp,
				  struct hdmi_drm_infoframe drmif)
{
	struct xlnx_dp_infoframe *iframe = &dp->infoframe;

	memset(iframe, 0, sizeof(struct xlnx_dp_infoframe));

	iframe->header.byte[0] = NON_AUDIOIF_PKT_ID;
	iframe->header.byte[1] = NON_AUDIOIF_DRM_TYPE;
	iframe->header.byte[2] = NON_AUDIOIF_LDATA_BYTECOUNT;
	iframe->header.byte[3] = NON_AUDIOIF_SDP_VERSION;
	iframe->payload.byte[0] = CTA_DRMIF_VERSION_NUMBER;
	iframe->payload.byte[1] = CTA_DRMIF_LENGHT;

	iframe->payload.byte[2] = drmif.eotf & 0x7;
	iframe->payload.byte[3] = drmif.metadata_type & 0x7;

	iframe->payload.byte[4] = drmif.display_primaries[0].x & 0xFF;
	iframe->payload.byte[5] = drmif.display_primaries[0].x >> 8;

	iframe->payload.byte[6] = drmif.display_primaries[0].y & 0xFF;
	iframe->payload.byte[7] = drmif.display_primaries[0].y >> 8;

	iframe->payload.byte[8] = drmif.display_primaries[1].x & 0xFF;
	iframe->payload.byte[9] = drmif.display_primaries[1].x >> 8;

	iframe->payload.byte[10] = drmif.display_primaries[1].y & 0xFF;
	iframe->payload.byte[11] = drmif.display_primaries[1].y >> 8;

	iframe->payload.byte[12] = drmif.display_primaries[2].x & 0xFF;
	iframe->payload.byte[13] = drmif.display_primaries[2].x >> 8;

	iframe->payload.byte[14] = drmif.display_primaries[2].y & 0xFF;
	iframe->payload.byte[15] = drmif.display_primaries[2].y >> 8;

	iframe->payload.byte[16] = drmif.white_point.x & 0xFF;
	iframe->payload.byte[17] = drmif.white_point.x >> 8;

	iframe->payload.byte[18] = drmif.white_point.y & 0xFF;
	iframe->payload.byte[19] = drmif.white_point.y >> 8;

	iframe->payload.byte[20] = drmif.max_display_mastering_luminance & 0xFF;
	iframe->payload.byte[21] = drmif.max_display_mastering_luminance >> 8;

	iframe->payload.byte[22] = drmif.min_display_mastering_luminance & 0xFF;
	iframe->payload.byte[23] = drmif.min_display_mastering_luminance >> 8;

	iframe->payload.byte[24] = drmif.max_cll & 0xFF;
	iframe->payload.byte[25] = drmif.max_cll >> 8;

	iframe->payload.byte[26] = drmif.max_fall & 0xFF;
	iframe->payload.byte[27] = drmif.max_fall >> 8;
}

static void xlnx_dp_vsync_handler(struct xlnx_dp *dp)
{
	struct drm_connector *connector = READ_ONCE(dp->connector);
	struct drm_connector_state *state = connector ? connector->state : NULL;
	struct hdmi_drm_infoframe frame;
	struct xlnx_dp_infoframe *iframe = &dp->infoframe;
	int i;
	u32 fifosts = xlnx_dp_read(dp->dp_base, XDPTX_AUDIO_INFO_BUFF_STATUS);

	if (!(fifosts & (XDPTX_AUDIO_INFO_BUFF_FULL |
					XDPTX_AUDIO_INFO_BUFF_OVERFLOW))) {
		/* Write new audio info packet */
		for (i = 0; i < DP_INFOFRAME_FIFO_SIZE_WORDS; i++) {
			xlnx_dp_write(dp->dp_base,
				      XDPTX_AUDIO_INFO_DATA_REG,
				      dp->tx_audio_data->buffer[i]);
		}

		if (state && state->gen_hdr_output_metadata) {
			if (drm_hdmi_infoframe_set_gen_hdr_metadata(&frame, state))
				return;

			xlnx_dp_gen_drmif_pkt(dp, frame);

			xlnx_dp_write(dp->dp_base, XDPTX_AUDIO_INFO_DATA_REG,
				      iframe->header.data);
			/* Write new hdr info packet */
			for (i = 0; i < (DP_INFOFRAME_FIFO_SIZE_WORDS - 1); i++) {
				xlnx_dp_write(dp->dp_base,
					      XDPTX_AUDIO_INFO_DATA_REG,
					      iframe->payload.data[i]);
			}
		}
	}
}

static irqreturn_t xlnx_dp_irq_handler(int irq, void *data)
{
	struct xlnx_dp *dp = (struct xlnx_dp *)data;
	u32 intrstatus;
	u32 hpdduration;

	/* Determine what kind of interrupt occurred. */
	intrstatus = xlnx_dp_read(dp->dp_base, XDPTX_INTR_STATUS_REG);

	if (!intrstatus)
		return IRQ_NONE;

	if (intrstatus & XDPTX_INTR_HPDEVENT_MASK) {
		dev_dbg_ratelimited(dp->dev, "hpdevent detected\n");
	} else if (intrstatus & XDPTX_INTR_HPDPULSE_MASK &&
		   xlnx_dp_txconnected(dp)) {
		/*
		 * Some monitors give HPD pulse repeatedly which cause
		 * HPD pulse function to be executed huge number of times.
		 * Hence HPD pulse interrupt is disabled if pulse duration
		 * is longer than 500 microseconds. In MST the HPD pulse
		 * carries the sideband/ESI events the modeset waits on, so
		 * never mask it while an MST session is active.
		 */
		if (!dp->mst_mgr.mst_state) {
			hpdduration = xlnx_dp_read(dp->dp_base,
						   XDPTX_HPD_DURATION_REG);
			if (hpdduration >= 500)
				xlnx_dp_write(dp->dp_base,
					      XDPTX_INTR_MASK_REG, 0x10);
		}
	}

	if (intrstatus &
	    (XDPTX_INTR_CHBUFUNDFW_MASK | XDPTX_INTR_CHBUFOVFW_MASK)) {
		u32 m = xlnx_dp_read(dp->dp_base, XDPTX_INTR_MASK_REG);

		m |= (XDPTX_INTR_CHBUFUNDFW_MASK | XDPTX_INTR_CHBUFOVFW_MASK);
		xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, m);
		dev_warn_ratelimited(dp->dev,
				     "CHBUF over/underflow - masking FIFO interrupts to stop storm (status=0x%x)\n",
				     intrstatus);
	}

	if (intrstatus & XDPTX_INTR_HPDEVENT_MASK) {
		/* A new HPD event re-arms MST bring-up. */
		dp->mst_bringup_failures = 0;
		schedule_delayed_work(&dp->hpd_work, 0);
	}
	if (intrstatus & XDPTX_INTR_HPDPULSE_MASK)
		schedule_delayed_work(&dp->hpd_pulse_work, 0);
	if (intrstatus & XDPTX_INTR_EXTPKT_TXD_MASK) {
		bool vsc_active = dp->colorimetry_through_vsc &&
				  !dp->mst_mgr.mst_state;

		if (vsc_active) {
			xlnx_dp_vsc_pkt_handler(dp);
		} else {
			u32 m = xlnx_dp_read(dp->dp_base, XDPTX_INTR_MASK_REG);

			m |= XDPTX_INTR_EXTPKT_TXD_MASK;
			xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, m);
		}
	}
	if (intrstatus & XDPTX_INTR_VBLANK_MASK)
		xlnx_dp_vsync_handler(dp);

	return IRQ_HANDLED;
}

static const struct component_ops xlnx_dp_component_ops = {
	.bind	= xlnx_dp_bind,
	.unbind	= xlnx_dp_unbind,
};

static ssize_t xlnx_hdcp_load_key(struct device *sysfs_dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret = 0;
	struct xlnx_dp *dp = (struct xlnx_dp *)dev_get_drvdata(sysfs_dev);
	struct xlnx_hdcptx *dptxhdcp = &dp->tx_hdcp;

	ret = xlnx_hdcp_tx_set_keys(dptxhdcp, (u8 *)buf, count);
	if (ret) {
		dev_dbg(dptxhdcp->dev, "failed to send HDCP key from Sysfs to wrapper\n");
		return ret;
	}
	return count;
}

static DEVICE_ATTR(hdcp_key, XHDCP_KEY_WRITE_PERMISSION, NULL, xlnx_hdcp_load_key);

static struct attribute *attrs[] = {
	&dev_attr_hdcp_key.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

/**
 * xlnx_dp_hdcp_dpcd_write - HDCP message write through dpcd interface
 * @ref: callback reference pointer
 * @offset: register offset
 * @buf: write buffer
 * @buf_size: number of bytes to write
 *
 * Return: buffer size on successful write, or the error code returned
 * from the callee functions.
 */
static int xlnx_dp_hdcp_dpcd_write(void *ref, u32 offset,
				   void *buf, u32 buf_size)
{
	struct xlnx_dp *dp = (struct xlnx_dp *)ref;
	u32 ret = 0;

	ret = drm_dp_dpcd_write(&dp->aux, offset, buf, buf_size);
	if (ret < 0) {
		dev_err(dp->dev, "dpcd write failed");
		return ret;
	}

	return ret;
}

/**
 * xlnx_dp_hdcp_dpcd_read - HDCP message read through dpcd interface
 * @ref: callback reference pointer
 * @offset: register offset
 * @buf: read buffer
 * @buf_size: number of bytes to read
 *
 * Return: buffer size on successful read, or the error code returned
 * from the callee functions.
 */
static int xlnx_dp_hdcp_dpcd_read(void *ref, u32 offset,
				  void *buf, u32 buf_size)
{
	struct xlnx_dp *dp = (struct xlnx_dp *)ref;
	u32 ret = 0;

	ret = drm_dp_dpcd_read(&dp->aux, offset, buf, buf_size);
	if (ret < 0) {
		dev_err(dp->dev, "dpcd read failed");
		return ret;
	}

	return ret;
}

/**
 * xlnx_dp_hdcp_status_update - HDCP status notification
 * @ref: callback reference pointer
 * @notification: HDCP notification
 */
static void xlnx_dp_hdcp_status_update(void *ref, u32 notification)
{
	struct xlnx_dp *dp = (struct xlnx_dp *)ref;

	switch (notification) {
	case XHDCPTX_INCOMPATIBLE_RX:
		dev_dbg(dp->dev, "HDCP TX compatible receiver is not found\n");
		break;
	case XHDCPTX_AUTHENTICATION_BUSY:
		dev_dbg(dp->dev, "HDCP TX Authentication Busy\n");
		break;
	case XHDCPTX_AUTHENTICATED:
		dev_dbg(dp->dev, "HDCP TX Authenticated\n");
		break;
	case XHDCPTX_REAUTHENTICATE_REQUESTED:
		dev_dbg(dp->dev, "HDCP TX Re-authentication Request received\n");
		break;
	case XHDCPTX_DEVICE_IS_REVOKED:
		dev_dbg(dp->dev, "HDCP TX , a device in the HDCP chain is revoked\n");
		break;
	case XHDCPTX_NO_SRM_LOADED:
		dev_dbg(dp->dev, "HDCP TX , no valid srm is loaded\n");
		break;
	case XHDCPTX_UNAUTHENTICATED:
		dev_dbg(dp->dev, "HDCP TX Unauthenticated\n");
		break;
	default:
		dev_dbg(dp->dev, "Error, HDCP is not initialized\n");
		break;
	}
}

/**
 * xlnx_dp_hdcp_exit - HDCP module de-initialization
 * @dp: displayPort IP core structure
 *
 * Return: 0 on success, or the status from called functions
 */
static int xlnx_dp_hdcp_exit(struct xlnx_dp *dp)
{
	struct xlnx_hdcptx *dptxhdcp = &dp->tx_hdcp;
	int ret;

	if (!(dptxhdcp->hdcp1xenable || dptxhdcp->hdcp2xenable)) {
		dev_info(dp->dev, "HDCP is not enabled in the system");
		return -EINVAL;
	}

	ret = xlnx_dp_hdcp_reset(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to reset HDCP");
		return ret;
	}

	xlnx_hdcp_tx_timer_exit(dptxhdcp);
	xlnx_hdcp_tx_exit(dptxhdcp);

	return 0;
}

/**
 * xlnx_timer_irq_handler - HDCP timer interrupt handler
 * @irq: IRQ number of the interrupt being handled
 * @data: Pointer to device structure
 *
 * Return: irq handler status
 */
static irqreturn_t xlnx_timer_irq_handler(int irq, void *data)
{
	struct xlnx_dp *dp = (struct xlnx_dp *)data;
	struct xlnx_hdcptx *dptxhdcp = &dp->tx_hdcp;

	xlnx_hdcp_tmrcntr_interrupt_handler(dptxhdcp->xhdcptmr);

	return IRQ_HANDLED;
}

/**
 * xlnx_hdcp_init - HDCP module initialization
 * @dp: displayPort IP core structure
 * @pdev: platform structure
 *
 * Return: 0 on success, or return the error code from the called functions.
 */
static int xlnx_hdcp_init(struct xlnx_dp *dp,
			  struct platform_device *pdev)
{
	struct xlnx_hdcptx *dptxhdcp = &dp->tx_hdcp;
	int ret;

	dptxhdcp->dev = dp->dev;
	dptxhdcp->hdcp2xenable = dp->config.hdcp2x_enable;
	dptxhdcp->hdcp1xenable = dp->config.hdcp1x_enable;

	if (dp->config.hdcp2x_enable) {
		xlnx_dp_set(dp->dp_base, XDP_TX_HDCP2x_ENABLE,
			    XDP_TX_HDCP2x_ENABLE_BYPASS_DISABLE_MASK);

		dptxhdcp->xhdcp2x = xlnx_hdcp_tx_init(&pdev->dev, dp, dptxhdcp,
						      dp->dp_base + XDPTX_HDCP2X_OFFSET,
						      0, XHDCPTX_HDCP_2X, dp->mode.lane_cnt,
						      XDPTX_HDCP, dp->hdcpx_keymgmt_base);

		if (IS_ERR(dptxhdcp->xhdcp2x)) {
			dev_err(dp->dev, "failed to initialize HDCP2X module\n");
			return PTR_ERR(dptxhdcp->xhdcp2x);
		}
	}
	dptxhdcp->xhdcptmr =
			xlnx_hdcp_timer_init(&pdev->dev, dp->dp_base + XDPTX_HDCP_TIMER_OFFSET);
	if (IS_ERR(dptxhdcp->xhdcptmr)) {
		dev_err(dp->dev, "failed to initialize HDCP timer\n");
		return PTR_ERR(dptxhdcp->xhdcptmr);
	}
	dp->hdcptx_timer_irq =
			 platform_get_irq_byname(pdev, "dptxss_timer_irq");
	if (dp->hdcptx_timer_irq < 0) {
		dev_err(dp->dev, "failed to get HDCP timer irq ");
		return -EINVAL;
	}
	ret = devm_request_threaded_irq(dp->dev, dp->hdcptx_timer_irq, NULL,
					xlnx_timer_irq_handler,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"dptxss_timer_irq", dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to register HDCP timer irq");
		return ret;
	}
	if (dp->config.hdcp1x_enable) {
		dptxhdcp->xhdcp1x = xlnx_hdcp_tx_init(&pdev->dev, dp, dptxhdcp,
						      dp->dp_base + XDPTX_HDCP1X_OFFSET,
						      0, XHDCPTX_HDCP_1X, dp->mode.lane_cnt,
						      XDPTX_HDCP, dp->hdcpx_keymgmt_base);
		if (IS_ERR(dptxhdcp->xhdcp1x)) {
			dev_err(dp->dev, "failed to initialize HDCP1X module\n");
			return PTR_ERR(dptxhdcp->xhdcp1x);
		}
	}

	ret = xlnx_hdcp_tx_set_callback(dptxhdcp, XDPTX_HDCP_DPCD_WRITE,
					xlnx_dp_hdcp_dpcd_write);
	if (ret < 0) {
		dev_err(dp->dev, "failed to register HDCP DPCD Write Callback");
		return ret;
	}

	ret = xlnx_hdcp_tx_set_callback(dptxhdcp, XDPTX_HDCP_DPCD_READ,
					xlnx_dp_hdcp_dpcd_read);
	if (ret < 0) {
		dev_err(dp->dev, "failed to register HDCP DPCD Read Callback");
		return ret;
	}

	ret = xlnx_hdcp_tx_set_callback(dptxhdcp, XDPTX_HDCP_STATUS,
					xlnx_dp_hdcp_status_update);
	if (ret < 0) {
		dev_err(dp->dev, "failed to register HDCP Status Update Callback");
		return ret;
	}

	INIT_DELAYED_WORK(&dp->hdcp_cp_irq_work, xlnx_dp_hdcp_cp_irq_func);

	return 0;
}

static int xlnx_dp_parse_of(struct xlnx_dp *dp)
{
	struct xlnx_dp_config *config = &dp->config;
	struct device_node *node = dp->dev->of_node;
	u32 bpc;
	u32 pixel_mode;
	int ret;

	/*
	 * xlnx,num-streams selects SST vs MST: 1 (or property absent) means
	 * SST; 2..XDPTX_MAX_STREAMS enables MST with exactly that many streams.
	 */
	ret = of_property_read_u32(node, "xlnx,num-streams",
				   &config->num_streams);
	if (ret < 0) {
		config->num_streams = 1;
	} else if (config->num_streams < 1 ||
		   config->num_streams > XDPTX_MAX_STREAMS) {
		dev_err(dp->dev, "xlnx,num-streams %u out of range (1..%d)\n",
			config->num_streams, XDPTX_MAX_STREAMS);
		return -EINVAL;
	}

	/* Configured for MST when more than one stream is present. */
	dp->mst_mode = config->num_streams > 1;

	/*
	 * Each stream drives its own VTC generator, so the DT must list one
	 * vtc-offset per stream.  Require exactly num_streams entries so that
	 * vtc_off[0..num_streams-1] are fully populated and no stream is left
	 * pointing at offset 0 (which would collide with stream 0 / the base).
	 */
	if (dp->cfg->flags & XDPTX_VTC_OFFSET_CHANGE) {
		ret = of_property_read_u32_array(node, "xlnx,vtc-offset",
						 dp->vtc_off,
						 config->num_streams);
		if (ret < 0) {
			dev_err(dp->dev,
				"xlnx,vtc-offset must list %u entries (xlnx,num-streams)\n",
				config->num_streams);
			return ret;
		}
	} else if (config->num_streams > 1) {
		dev_err(dp->dev, "MST requires per-stream VTC offsets (xlnx,vtc-offset)\n");
		return -EINVAL;
	} else {
		dp->vtc_off[0] = XDPTX_VTC_BASE;
	}

	/* PL pixel packing; MST pins ppc to this. Default quad-pixel. */
	if (of_property_read_u32(node, "xlnx,pixel-mode", &pixel_mode))
		pixel_mode = 4;
	if (pixel_mode != 1 && pixel_mode != 2 && pixel_mode != 4) {
		dev_err(dp->dev,
			"invalid xlnx,pixel-mode %u (must be 1, 2 or 4)\n",
			pixel_mode);
		return -EINVAL;
	}
	config->pixel_mode = pixel_mode;

	config->hdcp1x_enable = of_property_read_bool(node, "xlnx,hdcp-enable");
	config->hdcp2x_enable =
			of_property_read_bool(node, "xlnx,hdcp22-enable");

	ret = of_property_read_u32(node, "xlnx,max-lanes", &config->max_lanes);
	if (ret < 0) {
		dev_err(dp->dev, "No lane count in DT\n");
		return ret;
	}
	if (config->max_lanes != 1 && config->max_lanes != 2 &&
	    config->max_lanes != 4) {
		dev_err(dp->dev, "Invalid max lanes in DT\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,max-link-rate",
				   &config->max_link_rate);
	if (ret < 0) {
		dev_err(dp->dev, "No link rate in DT\n");
		return ret;
	}
	if (config->max_link_rate != XDPTX_REDUCED_BIT_RATE &&
	    config->max_link_rate != XDPTX_HIGH_BIT_RATE_1 &&
	    config->max_link_rate != XDPTX_HIGH_BIT_RATE_2 &&
	    config->max_link_rate != XDPTX_HIGH_BIT_RATE_3) {
		dev_err(dp->dev, "Invalid link rate in DT\n");
		return -EINVAL;
	}

	xlnx_dp_set_color(dp, DRM_FORMAT_RGB888);

	ret = of_property_read_u32(node, "xlnx,bpc", &bpc);
	if (ret < 0) {
		dev_err(dp->dev, "No color depth(bpc) in DT\n");
		return ret;
	}

	switch (bpc) {
	case 6:
		dp->fmt.misc0 |= XDPTX_MISC0_BPC6_MASK;
		break;
	case 8:
		dp->fmt.misc0 |= XDPTX_MISC0_BPC8_MASK;
		break;
	case 10:
		dp->fmt.misc0 |= XDPTX_MISC0_BPC10_MASK;
		break;
	case 12:
		dp->fmt.misc0 |= XDPTX_MISC0_BPC12_MASK;
		break;
	case 16:
		dp->fmt.misc0 |= XDPTX_MISC0_BPC16_MASK;
		break;
	default:
		dev_err(dp->dev, "Not supported color depth in DT\n");
		return -EINVAL;
	}
	config->max_bpc = bpc;

	config->audio_enabled =
		of_property_read_bool(node, "xlnx,audio-enable");

	config->versal_gt_present =
		of_property_read_bool(node, "xlnx,versal-gt");

	if (config->hdcp1x_enable) {
		dp->hdcpx_keymgmt_base = syscon_regmap_lookup_by_phandle(node,
									 "xlnx,hdcp1x-keymgmt");
		if (IS_ERR(dp->hdcpx_keymgmt_base)) {
			dev_err(dp->dev, "couldn't map hdcp1x Keymgmt registers\n");
			return -ENODEV;
		}
	}

	return 0;
}

static int xlnx_dp_probe(struct platform_device *pdev)
{
	struct device_node *pnode = pdev->dev.of_node;
	struct device_node *fnode;
	struct platform_device *iface_pdev;
	struct xlnx_dp *dp;
	struct resource *res;
	const struct of_device_id *match;
	u32 family = 0;
	unsigned int i;
	int irq, ret;

	dp = devm_drm_bridge_alloc(&pdev->dev, struct xlnx_dp, bridge,
				   &xlnx_dp_bridge_funcs);
	if (IS_ERR(dp))
		return PTR_ERR(dp);

	dp->tx_audio_data =
		devm_kzalloc(&pdev->dev, sizeof(struct xlnx_dptx_audio_data),
			     GFP_KERNEL);
	if (!dp->tx_audio_data)
		return -ENOMEM;

	dp->dpms = DRM_MODE_DPMS_OFF;
	dp->status = connector_status_disconnected;
	dp->dev = &pdev->dev;
	ret = devm_mutex_init(&pdev->dev, &dp->conn_lock);
	if (ret)
		return ret;

	match = of_match_node(xlnx_dp_of_match, pnode);
	if (!match)
		return -ENODEV;

	dp->cfg = match->data;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dp_base");
	dp->dp_base = devm_ioremap_resource(dp->dev, res);
	if (IS_ERR(dp->dp_base)) {
		dev_err(&pdev->dev, "couldn't map DisplayPort registers\n");
		return -ENODEV;
	}

	ret = xlnx_dp_parse_of(dp);
	if (ret < 0)
		return ret;

	/*
	 * Detect Versal Gen 2 series silicon via the ZynqMP firmware API
	 * rather than a board-specific DT property. The flag is consumed by
	 * xlnx_dp_tx_gt_control_init() to insert a small GT settling delay.
	 * On non-firmware platforms the API returns an error and the flag
	 * stays cleared, which is the safe default.
	 */
	if (zynqmp_pm_get_family_info(&family) == 0 &&
	    family == PM_VERSAL2_FAMILY_CODE)
		dp->config.versal_2ve_2vm = true;

	if (dp->config.versal_gt_present) {
		dp->phy[0] = devm_phy_get(dp->dev, "dp-gtquad");
		if (IS_ERR(dp->phy[0]))
			return dev_err_probe(dp->dev, PTR_ERR(dp->phy[0]),
					     "failed to get phy\n");

		ret = phy_init(dp->phy[0]);
		if (ret)
			return ret;

		fnode = of_parse_phandle(pnode, "xlnx,xilinx-vfmc", 0);
		if (fnode) {
			iface_pdev = of_find_device_by_node(fnode);
			of_node_put(fnode);
			if (!iface_pdev) {
				ret = -ENODEV;
				goto error_phy;
			}
			if (!dev_get_drvdata(&iface_pdev->dev)) {
				put_device(&iface_pdev->dev);
				ret = dev_err_probe(dp->dev, -EPROBE_DEFER,
						    "vfmc driver not ready\n");
				goto error_phy;
			}
			put_device(&iface_pdev->dev);
		} else {
			dev_dbg(&pdev->dev,
				"xlnx,xilinx-vfmc not present, skipping\n");
		}
	}

	dp->axi_lite_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(dp->axi_lite_clk)) {
		ret = dev_err_probe(dp->dev, PTR_ERR(dp->axi_lite_clk),
				    "failed to get axi_lite_clk\n");
		goto error_phy;
	}

	/*
	 * Video (pixel) clocks. Stream 1 always uses "tx_vid_clk" (the SST /
	 * legacy name). MST bitstreams may expose one Clocking Wizard output per
	 * additional stream ("tx_vid_clk_stream2".."tx_vid_clk_stream4"); when
	 * present, per_stream_vid_clk is set so each stream runs at its own pixel
	 * rate, otherwise all streams share tx_vid_clk[0].
	 */
	dp->tx_vid_clk[0] = devm_clk_get(&pdev->dev, "tx_vid_clk");
	if (IS_ERR(dp->tx_vid_clk[0])) {
		ret = dev_err_probe(dp->dev, PTR_ERR(dp->tx_vid_clk[0]),
				    "failed to get tx_vid_clk\n");
		goto error_phy;
	}
	for (i = 1; i < dp->config.num_streams; i++) {
		char clk_name[24];

		snprintf(clk_name, sizeof(clk_name), "tx_vid_clk_stream%u",
			 i + 1);
		dp->tx_vid_clk[i] = devm_clk_get_optional(&pdev->dev, clk_name);
		if (IS_ERR(dp->tx_vid_clk[i])) {
			ret = dev_err_probe(dp->dev, PTR_ERR(dp->tx_vid_clk[i]),
					    "failed to get %s\n", clk_name);
			goto error_phy;
		}
		if (dp->tx_vid_clk[i])
			dp->per_stream_vid_clk = true;
	}

	platform_set_drvdata(pdev, dp);
	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 0);
	xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_ENABLE_REG, 0);

	dp->tx_link_config.vs_level = 0;
	dp->tx_link_config.pe_level = 0;

	if (!dp->config.versal_gt_present) {
		/* acquire vphy lanes */
		for (i = 0; i < dp->config.max_lanes; i++) {
			char phy_name[16];

			snprintf(phy_name, sizeof(phy_name), "dp-phy%d", i);
			dp->phy[i] = devm_phy_get(dp->dev, phy_name);
			if (IS_ERR(dp->phy[i])) {
				ret = dev_err_probe(dp->dev, PTR_ERR(dp->phy[i]),
						    "failed to get phy lane %s i %d\n",
						    phy_name, i);
				dp->phy[i] = NULL;
				goto error_phy;
			}
		}

		ret = xlnx_dp_init_phy(dp);
		if (ret)
			goto error_phy;
	} else {
		ret = xlnx_dp_tx_gt_control_init(dp);
		if (ret)
			goto error_phy;
	}

	ret = clk_prepare_enable(dp->axi_lite_clk);
	if (ret) {
		dev_err(dp->dev, "failed to enable axi_lite_clk (%d)\n", ret);
		goto error_phy;
	}

	for (i = 0; i < dp->config.num_streams; i++) {
		if (!dp->tx_vid_clk[i])
			continue;
		ret = clk_prepare_enable(dp->tx_vid_clk[i]);
		if (ret) {
			dev_err(dp->dev,
				"failed to enable tx_vid_clk[%u] (%d)\n",
				i, ret);
			while (i-- > 0)
				if (dp->tx_vid_clk[i])
					clk_disable_unprepare(dp->tx_vid_clk[i]);
			goto error_clk_axi;
		}
	}

	dp->aux.name = "Xlnx DP AUX";
	dp->aux.dev = dp->dev;
	dp->aux.transfer = xlnx_dp_aux_transfer;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto error_clk_vid;
	}
	dp->irq = irq;
	/* The handler queues the HPD works, which must exist before it can run. */
	INIT_DELAYED_WORK(&dp->hpd_work, xlnx_dp_hpd_work_func);
	INIT_DELAYED_WORK(&dp->hpd_pulse_work, xlnx_dp_hpd_pulse_work_func);
	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, ~0);
	ret = devm_request_threaded_irq(dp->dev, irq, NULL,
					xlnx_dp_irq_handler, IRQF_ONESHOT,
					dev_name(dp->dev), dp);

	if (ret)
		goto error_clk_vid;

	ret = sysfs_create_group(&dp->dev->kobj, &attr_group);
	if (ret) {
		dev_err(dp->dev, "sysfs group creation failed\n");
		goto error_clk_vid;
	}

	if (dp->config.hdcp2x_enable || dp->config.hdcp1x_enable) {
		ret = xlnx_hdcp_init(dp, pdev);
		if (ret)
			goto error_sysfs;
	}

	if (dp->config.audio_enabled) {
		ret = dptx_register_aud_dev(dp->dev);
		if (ret) {
			dp->audio_init = false;
			dev_err(dp->dev, "dp tx audio init failed\n");
			goto error_hdcp;
		}
		dp->audio_init = true;
		dev_info(dp->dev, "dp tx audio initialized\n");
	}

	dp->bridge.of_node = dp->dev->of_node;
	dp->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;
	dp->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_HPD |
			 DRM_BRIDGE_OP_EDID;
	drm_bridge_add(&dp->bridge);

	/*
	 * Audio cleanup is handled automatically via devm on failure.
	 * component_add() has no devm variant and must be undone explicitly.
	 */
	ret = component_add(&pdev->dev, &xlnx_dp_component_ops);
	if (ret) {
		drm_bridge_remove(&dp->bridge);
		goto error_hdcp;
	}

	/*
	 * For MST (num_streams > 1) the DP TX node owns the DRM pipeline:
	 * its OF-graph fans out to every per-stream pl-disp CRTC, so create
	 * the logical master here. For SST (num_streams == 1) the single
	 * pl-disp node is the master instead.
	 */
	if (dp->mst_mode) {
		dp->drm_master = xlnx_drm_pipeline_init(pdev);
		if (IS_ERR(dp->drm_master)) {
			ret = PTR_ERR(dp->drm_master);
			dp->drm_master = NULL;
			dev_err(&pdev->dev, "failed to init DRM pipeline\n");
			component_del(&pdev->dev, &xlnx_dp_component_ops);
			drm_bridge_remove(&dp->bridge);
			goto error_hdcp;
		}
	}

	return 0;

error_hdcp:
	if (dp->config.hdcp2x_enable || dp->config.hdcp1x_enable)
		xlnx_dp_hdcp_exit(dp);
error_sysfs:
	sysfs_remove_group(&dp->dev->kobj, &attr_group);
error_clk_vid:
	for (i = 0; i < dp->config.num_streams; i++)
		if (dp->tx_vid_clk[i])
			clk_disable_unprepare(dp->tx_vid_clk[i]);
error_clk_axi:
	clk_disable_unprepare(dp->axi_lite_clk);
error_phy:
	if (!dp->config.versal_gt_present)
		xlnx_dp_exit_phy(dp);
	else
		phy_exit(dp->phy[0]);

	return ret;
}

static void xlnx_dp_remove(struct platform_device *pdev)
{
	struct xlnx_dp *dp = platform_get_drvdata(pdev);
	unsigned int i;

	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 0);
	if (dp->config.hdcp2x_enable || dp->config.hdcp1x_enable)
		xlnx_dp_hdcp_exit(dp);
	if (!dp->config.versal_gt_present)
		xlnx_dp_exit_phy(dp);
	else
		phy_exit(dp->phy[0]);
	if (dp->drm_master)
		xlnx_drm_pipeline_exit(dp->drm_master);
	component_del(&pdev->dev, &xlnx_dp_component_ops);
	drm_bridge_remove(&dp->bridge);
	sysfs_remove_group(&pdev->dev.kobj, &attr_group);
	for (i = 0; i < dp->config.num_streams; i++)
		if (dp->tx_vid_clk[i])
			clk_disable_unprepare(dp->tx_vid_clk[i]);
	clk_disable_unprepare(dp->axi_lite_clk);
}

MODULE_DEVICE_TABLE(of, xlnx_dp_of_match);

static struct platform_driver dp_tx_driver = {
	.probe = xlnx_dp_probe,
	.remove = xlnx_dp_remove,
	.driver = {
		.name = "xlnx-dp-tx",
		.of_match_table = xlnx_dp_of_match,
	},
};

module_platform_driver(dp_tx_driver);

MODULE_AUTHOR("Rajesh Gugulothu <gugulothu.rajesh@xilinx.com>");
MODULE_DESCRIPTION("Xilinx FPGA DisplayPort Tx Driver");
MODULE_LICENSE("GPL");

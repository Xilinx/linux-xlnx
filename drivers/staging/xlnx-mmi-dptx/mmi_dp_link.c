// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated DisplayPort Tx driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_fixed.h>

#include "mmi_dp.h"

/**
 * mmi_dp_set_video_format() - Set video format
 * @dptx: The dptx struct
 * @video_format: video format
 * Possible options: 0 - CEA, 1 - CVT, 2 - DMT
 *
 * Return: Returns 0 on success otherwise negative errno.
 */
static int mmi_dp_set_video_format(struct dptx *dptx, u8 video_format)
{
	struct video_params *vparams;

	if (video_format > DMT) {
		dptx_dbg(dptx, "%s: Invalid video format value %d\n",
			 __func__, video_format);
		return -EINVAL;
	}

	vparams = &dptx->vparams[0];
	vparams->video_format = video_format;
	return 0;
}

/**
 * mmi_dp_set_video_dynamic_range() - Set video dynamic range
 * @dptx: The dptx struct
 * @dynamic_range: video dynamic range
 * Possible options: 1 - CEA, 2 - VESA
 *
 * Return: Returns 0 on success otherwise negative errno.
 */
static int mmi_dp_set_video_dynamic_range(struct dptx *dptx, u8 dynamic_range)
{
	struct video_params *vparams;

	if (dynamic_range > VESA) {
		dptx_dbg(dptx, "%s: Invalid dynamic range value %d\n",
			 __func__, dynamic_range);
		return -EINVAL;
	}

	vparams = &dptx->vparams[0];
	vparams->dynamic_range = dynamic_range;

	return 0;
}

/**
 * mmi_dp_set_video_colorimetry() - Set video colorimetry
 * @dptx: The dptx struct
 * @video_col: Video colorimetry
 * Possible options: 1 - ITU-R BT.601, 2 - ITU-R BT.709
 *
 * Return: Returns 0 on success otherwise negative errno.
 */
static int mmi_dp_set_video_colorimetry(struct dptx *dptx, u8 video_col)
{
	struct video_params *vparams;

	if (video_col > ITU709) {
		dptx_dbg(dptx, "%s: Invalid video colorimetry value %d\n",
			 __func__, video_col);
		return -EINVAL;
	}

	vparams = &dptx->vparams[0];
	vparams->colorimetry = video_col;

	return 0;
}

/**
 * mmi_dp_set_pixel_enc() - Set pixel encoding
 * @dptx: The dptx struct
 * @pix_enc: Video pixel encoding
 * Possible options: RGB - 0, YCbCR420 - 1, YCbCR422 - 2
 *		     YCbCR444 - 3, YOnly - 4, RAW -5
 *
 * Return: Returns 0 on success otherwise negative errno.
 */
static int mmi_dp_set_pixel_enc(struct dptx *dptx, u8 pix_enc)
{
	struct video_params *vparams;
	struct dtd *mdtd;
	u32 hpdsts;

	hpdsts = mmi_dp_read_regfield(dptx->base, HPD_STATUS, HPD_STATUS_MASK);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return -ENODEV;
	}
	if (pix_enc > RAW) {
		dptx_dbg(dptx, "%s: Invalid pixel encoding value %d\n",
			 __func__, pix_enc);
		return -EINVAL;
	}
	vparams = &dptx->vparams[0];
	mdtd = &vparams->mdtd;
	mmi_dp_video_ts_calculate(dptx, dptx->link.lanes, dptx->link.rate,
				  vparams->bpc, pix_enc, mdtd->pixel_clock);

	vparams->pix_enc = pix_enc;

	mmi_dp_disable_default_video_stream(dptx, DEFAULT_STREAM);
	mmi_dp_video_bpc_change(dptx, DEFAULT_STREAM);
	mmi_dp_video_ts_change(dptx, DEFAULT_STREAM);
	mmi_dp_enable_default_video_stream(dptx, DEFAULT_STREAM);

	if (pix_enc == YCBCR420) {
		mmi_dp_vsd_ycbcr420_send(dptx, 1);
		dptx->ycbcr420 = true;
	} else {
		mmi_dp_vsd_ycbcr420_send(dptx, 0);
		dptx->ycbcr420 = false;
	}

	return 0;
}

void mmi_dp_video_ts_calculate(struct dptx *dptx, int lane_num, int rate,
			       int bpc, int encoding, int pixel_clock)
{
	struct video_params *vparams;
	struct dtd *mdtd;
	int link_rate, link_clk, tu, tu_frac, color_dep, numerator, denominator;
	int T1 = 0, T2 = 0;
	s64 fixp;

	vparams = &dptx->vparams[0];
	mdtd = &vparams->mdtd;
	link_rate = mmi_dp_get_link_rate(rate);
	color_dep = mmi_dp_get_color_depth_bpp(bpc, encoding);

	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		link_clk = 40500;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		link_clk = 67500;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		link_clk = 135000;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		link_clk = 202500;
		break;
	default:
		link_clk = 40500;
	}

	numerator = (dptx->selected_pixel_clock * color_dep) / 8;
	denominator = (link_rate) * 10 * lane_num * 100;
	fixp = drm_fixp_from_fraction(numerator * 64, denominator);
	tu = drm_fixp2int(fixp);

	fixp &= DRM_FIXED_DECIMAL_MASK;
	if (dptx->mst)
		fixp *= 64;
	else
		fixp *= 10;

	tu_frac = drm_fixp2int(fixp);

	/* Calculate init_threshold for non DSC mode */
	if (dptx->multipixel == DPTX_MP_SINGLE_PIXEL) {
		/* Single Pixel Mode */
		if (tu <= 16)
			vparams->init_threshold = 32;
		else if (mdtd->h_blanking <= 40 && encoding == YCBCR420)
			vparams->init_threshold = 3;
		else if (mdtd->h_blanking <= 80 && encoding != YCBCR420)
			vparams->init_threshold = 12;
		else
			vparams->init_threshold = 16;
	} else {
		/* Multiple Pixel Mode */
		int init_thrshld;

		switch (bpc) {
		case COLOR_DEPTH_6:
			T1 = (4 * 1000 / 9) * lane_num;
			break;
		case COLOR_DEPTH_8:
			if (encoding == YCBCR422)
				T1 = (1000 / 2) * lane_num;
			else if (encoding == YONLY)
				T1 = lane_num * 1000;
			else if (dptx->multipixel == DPTX_MP_DUAL_PIXEL)
				T1 = (1000 / 3) * lane_num;
			else
				T1 = (3000 / 16) * lane_num;
			break;
		case COLOR_DEPTH_10:
			if (encoding == YCBCR422)
				T1 = (2000 / 5) * lane_num;
			else if (encoding == YONLY)
				T1 = (4000 / 5) * lane_num;
			else
				T1 = (4000 / 15) * lane_num;
			break;
		case COLOR_DEPTH_12:
			if (encoding == YCBCR422) /* Nothing happens here */
				if (dptx->multipixel == DPTX_MP_DUAL_PIXEL)
					T1 = (1000 / 6) * lane_num;
				else
					T1 = (1000 / 3) * lane_num;
			else if (encoding == YONLY)
				T1 = (2000 / 3) * lane_num;
			else
				T1 = (2000 / 9) * lane_num;
			break;
		case COLOR_DEPTH_16:
			if (encoding == YONLY)
				T1 = (1000 / 2) * lane_num;
			else if ((encoding != YCBCR422) &&
				 (dptx->multipixel == DPTX_MP_DUAL_PIXEL))
				T1 = (1000 / 6) * lane_num;
			else
				T1 = (1000 / 4) * lane_num;
			break;
		default:
			dptx_dbg(dptx, "Invalid param BPC = %d\n", bpc);
		}

		T2 = (link_clk * 1000 / dptx->selected_pixel_clock);

		init_thrshld = T1 * T2 * tu / (1000 * 1000);
		if (init_thrshld <= 16 || tu < 10)
			vparams->init_threshold = 40;
		else
			vparams->init_threshold = init_thrshld;
	}

	vparams->aver_bytes_per_tu = tu;
	vparams->aver_bytes_per_tu_frac = tu_frac;
}

static int mmi_dp_config_ctrl_video_mode(struct dptx *dptx)
{
	struct video_params *vparams;
	struct dtd *mdtd;

	vparams = &dptx->vparams[0];
	mdtd = &vparams->mdtd;

	mmi_dp_disable_video_stream(dptx, 0);
	mmi_dp_vinput_polarity_ctrl(dptx, 0);
	mmi_dp_vsample_ctrl(dptx, 0);
	mmi_dp_video_config1(dptx, 0);
	mmi_dp_video_config2(dptx, 0);
	mmi_dp_video_config3(dptx, 0);
	mmi_dp_video_config4(dptx, 0);

	mmi_dp_video_ts_calculate(dptx, dptx->link.lanes, dptx->link.rate, vparams->bpc,
				  vparams->pix_enc, mdtd->pixel_clock);
	mmi_dp_write_mask(dptx, VIDEO_CONFIG5, AVERAGE_BYTES_PER_TU_MASK,
			  vparams->aver_bytes_per_tu);

	if (!dptx->mst)
		mmi_dp_write_mask(dptx, VIDEO_CONFIG5, AVERAGE_BYTES_PER_TU_FRAC_MASK,
				  vparams->aver_bytes_per_tu_frac << 2);
	else
		mmi_dp_write_mask(dptx, VIDEO_CONFIG5, AVERAGE_BYTES_PER_TU_FRAC_MASK,
				  vparams->aver_bytes_per_tu_frac);

	mmi_dp_write_mask(dptx, VIDEO_CONFIG5, INIT_THRESHOLD_MASK,
			  vparams->init_threshold);

	if (dptx->rx_caps.enhanced_frame_cap)
		mmi_dp_write_mask(dptx, CCTL, CCTL_ENHANCE_FRAMING_EN, 1);

	mmi_dp_video_msa1(dptx, 0);
	mmi_dp_video_msa2(dptx, 0);
	mmi_dp_video_msa3(dptx, 0);
	mmi_dp_video_hblank_interval(dptx, 0);

	return 0;
}

/**
 * mmi_dp_enable_audio() - Initializes SDP and AUD for audio
 * @dptx: The dptx struct
 *
 * Configure SDP and AUD for 16-bit 8 channel audio.
 */
static void mmi_dp_enable_audio(struct dptx *dptx)
{
	u32 aud_config, sdp_vert, sdp_hori;

	aud_config = AUD_CONFIG1_DATA_IN_EN_8CH |
		     AUD_CONFIG1_DATA_WIDTH_16 |
		     AUD_CONFIG1_NUM_CH_8 |
		     (AUD_CONFIG1_TIMESTAMP_VER << AUD_CONFIG1_TS_VER_SHIFT) |
		     (AUD_CONFIG1_AUDCLK_512FS << AUD_CONFIG1_AUDIO_CLK_MULT_FS_SHIFT);

	mmi_dp_write(dptx->base, AUD_CONFIG1, aud_config);

	sdp_vert = SDP_VER_CTRL_EN_TIMESTAMP |
		   SDP_VER_CTRL_EN_STREAM |
		   SDP_VER_CTRL_FIXED_PRIO_ARB;
	mmi_dp_write(dptx->base, SDP_VERTICAL_CTRL, sdp_vert);

	sdp_hori = SDP_HORI_CTRL_EN_TIMESTAMP |
		   SDP_HORI_CTRL_EN_STREAM |
		   SDP_HORI_CTRL_FIXED_PRIO_ARB;
	mmi_dp_write(dptx->base, SDP_HORIZONTAL_CTRL, sdp_hori);
}

int mmi_dp_sst_configuration(struct dptx *dptx)
{
	struct video_params *vparams;

	dptx_dbg(dptx, "Making SST Configuration");
	dptx->streams = 1;
	vparams = &dptx->vparams[0];

	/* Configure CTRL for Timing requested */
	mmi_dp_config_ctrl_video_mode(dptx);

	/* Configure SDP and AUD for 8 channel audio */
	mmi_dp_enable_audio(dptx);

	/* Enable Video Stream */
	mmi_dp_set(dptx->base, VSAMPLE_CTRL, VIDEO_STREAM_ENABLE_MASK);

	dptx_info(dptx, "Video Transmission: %dx%d @ %dHz", vparams->mdtd.h_active,
		  vparams->mdtd.v_active, ((vparams->refresh_rate + 500) / 1000));

	return 0;
}

int mmi_dp_dtd_fill(struct dtd *mdtd, struct display_mode_t *display_mode)
{
	struct dtd_t dtd;

	dtd = display_mode->dtd;
	mmi_dp_dtd_reset(mdtd);

	mdtd->h_image_size = dtd.m_h_image_size;
	mdtd->v_image_size = dtd.m_v_image_size;
	mdtd->h_active = dtd.m_h_active;
	mdtd->v_active = dtd.m_v_active;
	mdtd->h_border = dtd.m_h_border;
	mdtd->v_border = dtd.m_v_border;
	mdtd->h_blanking = dtd.m_h_blanking;
	mdtd->v_blanking = dtd.m_v_blanking;
	mdtd->h_sync_offset = dtd.m_h_sync_offset;
	mdtd->v_sync_offset = dtd.m_v_sync_offset;
	mdtd->h_sync_pulse_width = dtd.m_h_sync_pulse_width;
	mdtd->v_sync_pulse_width = dtd.m_v_sync_pulse_width;
	mdtd->interlaced = dtd.m_interlaced; /* (progressive_nI) */
	mdtd->pixel_clock = dtd.m_pixel_clock;
	mdtd->h_sync_polarity = 1; /* dtd.m_h_sync_polarity; */
	mdtd->v_sync_polarity = 1; /* dtd.m_v_sync_polarity; */

	return 0;
}

static int mmi_dp_fill_current_mode_1080(struct display_mode_t *cmode)
{
	cmode->refresh_rate = 60000;
	/* Pixel Clock */
	cmode->dtd.m_pixel_clock = 148500;
	/* Interlaced */
	cmode->dtd.m_interlaced = 0;

	/* Horizontal data */
	cmode->dtd.m_h_active = 1920;
	cmode->dtd.m_h_blanking = 280;
	cmode->dtd.m_h_border = 0;
	cmode->dtd.m_h_image_size = 16;
	cmode->dtd.m_h_sync_pulse_width = 44;
	cmode->dtd.m_h_sync_offset = 88;

	/* Vertical data */
	cmode->dtd.m_v_active = 1080;
	cmode->dtd.m_v_blanking = 45;
	cmode->dtd.m_v_border = 0;
	cmode->dtd.m_v_image_size = 9;
	cmode->dtd.m_v_sync_pulse_width = 5;
	cmode->dtd.m_v_sync_offset = 4;
	return 0;
}

static int mmi_dp_video_mode_change(struct dptx *dptx)
{
	struct video_params *vparams;
	struct display_mode_t current_mode;
	u16 peak_stream_bw, link_bw;
	struct dtd mdtd;
	u32 pixel_clk;
	int retval;
	s64 fixp;
	u16 rate;
	u8 bpp;

	vparams = &dptx->vparams[0];
	retval = 0;

	mmi_dp_fill_current_mode_1080(&current_mode);
	mmi_dp_dtd_fill(&mdtd, &current_mode);

	vparams->mdtd = mdtd;

	dptx->selected_pixel_clock = mdtd.pixel_clock;
	/* Check if the stablished link is enough for the payload requested */
	bpp = mmi_dp_get_color_depth_bpp(vparams->bpc, vparams->pix_enc);
	rate = mmi_dp_get_link_rate(dptx->link.rate);
	pixel_clk = mdtd.pixel_clock;
	fixp = drm_fixp_div(drm_int2fixp(bpp), drm_int2fixp(8));
	fixp = drm_fixp_mul(fixp, drm_int2fixp(pixel_clk));
	fixp = drm_fixp_div(fixp, drm_int2fixp(1000));
	peak_stream_bw = drm_fixp2int(fixp);
	link_bw = rate * dptx->link.lanes;

	if (peak_stream_bw > link_bw) {
		dptx_err(dptx, "ERROR: VIC chosen isn't suitable for Link Rate running\n");
		dptx_err(dptx, "refresh_rate: %d BPC: %d PixelClock: %d",
			 vparams->refresh_rate, vparams->bpc, mdtd.pixel_clock);
		dptx_err(dptx, "Rate: %d Lanes: %d", dptx->link.rate, dptx->link.lanes);
		return -EINVAL;
	}

	/* Disable Video Stream and Generator */
	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_CTRL_N(0), VIDEO_STREAM_ENABLE_MASK, 0);

	mmi_dp_sst_configuration(dptx);

	mmi_dp_clean_interrupts(dptx);

	return retval;
}

/**
 * mmi_dp_set_video_mode() - Set current video mode
 * @dptx: The dptx struct
 *
 * Return: Returns 0 on success otherwise negative errno.
 */
static int mmi_dp_set_video_mode(struct dptx *dptx)
{
	u32 hpdsts;
	int retval;

	retval = 0;

	hpdsts = mmi_dp_read_regfield(dptx->base, HPD_STATUS, HPD_STATUS_MASK);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return -ENODEV;
	}
	retval = mmi_dp_video_mode_change(dptx);
	if (retval < 0) {
		mmi_dp_write_mask(dptx, DPTX_VSAMPLE_CTRL_N(DEFAULT_STREAM),
				  VIDEO_STREAM_ENABLE_MASK, 0);
		mmi_dp_soft_reset(dptx, DPTX_SRST_VIDEO_RESET_ALL);
	}

	return retval;
}

/**
 * mmi_dp_set_bpc() - Set bits per component
 * @dptx: The dptx struct
 * @bpc: Bits per component value
 * Possible options: 6, 8, 10, 12, 16
 *
 * Return: Returns 0 on success otherwise negative errno.
 */
static int mmi_dp_set_bpc(struct dptx *dptx, u8 bpc)
{
	u32 hpdsts;
	struct video_params *vparams;

	hpdsts = mmi_dp_read_regfield(dptx->base, HPD_STATUS, HPD_STATUS_MASK);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return -ENODEV;
	}

	switch (bpc) {
	case (COLOR_DEPTH_6):
	case (COLOR_DEPTH_8):
	case (COLOR_DEPTH_10):
	case (COLOR_DEPTH_12):
	case (COLOR_DEPTH_16):
		break;
	default:
		dptx_dbg(dptx, "%s: Invalid bits per component value %d\n",
			 __func__, bpc);
		return -EINVAL;
	}

	vparams = &dptx->vparams[0];
	vparams->bpc = bpc;
	mmi_dp_disable_default_video_stream(dptx, DEFAULT_STREAM);
	mmi_dp_config_ctrl_video_mode(dptx);
	mmi_dp_enable_default_video_stream(dptx, DEFAULT_STREAM);

	return 0;
}

static int mmi_dp_check_phy_busy(struct dptx *dptx, u32 timeout)
{
	u32 count = 0, phy_busy;

	phy_busy = mmi_dp_read_regfield(dptx->base, PHYIF_CTRL, PHYIF_PHY_BUSY);
	while (phy_busy) {
		count++;
		if (count > timeout) {
			dptx_err(dptx, "%s: TIMEOUT - PHY BUSY", __func__);
			return -EBUSY;
		}
		msleep(20);
		phy_busy = mmi_dp_read_regfield(dptx->base, PHYIF_CTRL, PHYIF_PHY_BUSY);
	}
	return 0;
}

static int mmi_dp_power_up_phy(struct dptx *dptx)
{
	dptx_dbg(dptx, "PHY: Power Up Sequence\n");

	/* Set the initial input values */
	mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_POWER_DOWN, DPTX_PHY_POWER_DOWN);
	mmi_dp_clr(dptx->base, PHYIF_CTRL, DPTX_PHYIF_CTRL_XMIT_EN_ALL);

	if (mmi_dp_check_phy_busy(dptx, MAX_PHY_BUSY_WAIT_ITER))
		return -EAGAIN;

	return 0;
}

int mmi_dp_power_state_change_phy(struct dptx *dptx, u8 power_state)
{
	if (!(power_state == DPTX_PHY_POWER_ON ||
	      power_state == DPTX_PHY_INTER_P2_POWER ||
	      power_state == DPTX_PHY_POWER_DOWN ||
	      power_state == DPTX_PHY_P4_POWER_STATE))
		return -EINVAL;

	/* Select the power state to change into */
	mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_POWER_DOWN, power_state);

	if (mmi_dp_check_phy_busy(dptx, MAX_PHY_BUSY_WAIT_ITER))
		return -EAGAIN;

	return 0;
}

int mmi_dp_disable_datapath_phy(struct dptx *dptx)
{
	u32 phyifctrl;
	u32 mask;

	mask = 0xF00; /* XMIT_ENABLE bits (11-8) */
	phyifctrl = mmi_dp_read(dptx->base, PHYIF_CTRL);
	phyifctrl &= ~mask;
	mmi_dp_write(dptx->base, PHYIF_CTRL, phyifctrl);

	return 0;
}

static int mmi_dp_link_training_lanes_set(struct dptx *dptx)
{
	int retval;
	unsigned int i;
	u8 bytes[4] = { 0xff, 0xff, 0xff, 0xff };

	for (i = 0; i < dptx->link.lanes; i++) {
		u8 byte = 0;

		byte |= ((dptx->link.vswing_level[i] <<
			  DP_TRAIN_VOLTAGE_SWING_SHIFT) &
			 DP_TRAIN_VOLTAGE_SWING_MASK);

		if (dptx->link.vswing_level[i] == 3)
			byte |= DP_TRAIN_MAX_SWING_REACHED;

		byte |= ((dptx->link.preemp_level[i] <<
			  DP_TRAIN_PRE_EMPHASIS_SHIFT) &
			 DP_TRAIN_PRE_EMPHASIS_MASK);

		if (dptx->link.preemp_level[i] == 3)
			byte |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

		bytes[i] = byte;
	}

	retval = mmi_dp_write_bytes_to_dpcd(dptx, DP_TRAINING_LANE0_SET, bytes,
					    dptx->link.lanes);
	if (retval)
		return retval;

	return 0;
}

int mmi_dp_fast_link_training(struct dptx *dptx)
{
	int nr_lanes, link_rate;

	nr_lanes = dptx->max_lanes;
	link_rate = dptx->max_rate;
	mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_POWER_DOWN, DPTX_PHY_POWER_ON);
	mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_RATE, link_rate);

	switch (nr_lanes) {
	case (1):
		mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_LANES, 0);
		break;
	case (2):
		mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_LANES, 2);
		break;
	case (4):
		mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_LANES, 4);
		break;
	default:
		mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_LANES, 0);
	}

	if (mmi_dp_check_phy_busy(dptx, 1000))
		return -EBUSY;

	mmi_dp_phy_set_pattern(dptx, 1);
	mmi_dp_phy_enable_xmit(dptx, nr_lanes, true);

	/* Wait for 500us as per DP Tx controller programming guide */
	usleep_range(500, 600);

	switch (link_rate) {
	case (DPTX_PHYIF_CTRL_RATE_HBR):
		mmi_dp_phy_set_pattern(dptx, 2);
		break;
	case (DPTX_PHYIF_CTRL_RATE_HBR2):
		mmi_dp_phy_set_pattern(dptx, 3);
		break;
	case (DPTX_PHYIF_CTRL_RATE_HBR3):
		mmi_dp_phy_set_pattern(dptx, 4);
		break;
	default:
		mmi_dp_phy_set_pattern(dptx, 2);
		break;
	}

	/* Wait for 500us as per DP Tx controller programming guide */
	usleep_range(500, 600);

	mmi_dp_phy_set_pattern(dptx, 0);

	return 0;
}

static int mmi_dp_dpcd_link_configuration(struct dptx *dptx)
{
	u8 lanes, rate, byte;

	/* LINK_BW_SET - rate */
	rate = mmi_dp_phy_rate_to_bw(dptx->link.rate);
	mmi_dp_write_dpcd(dptx, DP_LINK_BW_SET, rate);

	/* LANE_COUNT_SET */
	lanes = dptx->link.lanes | DP_LANE_COUNT_ENHANCED_FRAME_EN;
	mmi_dp_write_dpcd(dptx, DP_LANE_COUNT_SET, lanes);

	/* DOWNSPREAD_CTRL */
	byte = 0x00; /* SPREAD_AMP must be set to 0 */
	mmi_dp_write_dpcd(dptx, DP_DOWNSPREAD_CTRL, byte);

	/* MAIN_LINK_CHANNEL_CODING_SET */
	byte = 0x01; /* 8b/10b encoding selected */
	mmi_dp_write_dpcd(dptx, DP_MAIN_LINK_CHANNEL_CODING_SET, byte);

	return 0;
}

static int mmi_dp_transmit_TPS1(struct dptx *dptx)
{
	int ret;

	mmi_dp_disable_datapath_phy(dptx);

	/* Configure PHY Link Rate */
	ret = mmi_dp_power_state_change_phy(dptx, DPTX_PHY_INTER_P2_POWER);
	if (ret)
		return ret;

	/* Configure PHY Lanes */
	mmi_dp_phy_set_lanes(dptx, dptx->link.lanes);
	mmi_dp_phy_set_rate(dptx, dptx->link.rate);

	/* wait for phy busy */
	if (mmi_dp_check_phy_busy(dptx, 1000))
		return -EBUSY;

	/* Force no Transmitted Pattern */
	mmi_dp_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);

	/* PHY Power On */
	ret = mmi_dp_power_state_change_phy(dptx, DPTX_PHY_POWER_ON);
	if (ret)
		return ret;

	/* Configure TPS1 transmission */
	mmi_dp_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_1);

	mmi_dp_phy_enable_xmit(dptx, dptx->link.lanes, true);

	return 0;
}

static int mmi_dp_set_training_set_regs(struct dptx *dptx, u8 pattern)
{
	u8 reg[5] = { 0 };
	u8 i;

	/* TRAINING_PATTERN_SET - DPCD 102h */
	reg[0] = mmi_dp_set8_field(reg[0], PATTERN_MASK, pattern);
	if (pattern == DP_TRAINING_PATTERN_4)
		reg[0] = mmi_dp_set8_field(reg[0], SCRAMBLING_DIS_MASK, 0);
	else
		reg[0] = mmi_dp_set8_field(reg[0], SCRAMBLING_DIS_MASK, 1);

	/* TRAINING_LANEx_SET */
	for (i = 0; i < dptx->link.lanes; i++) {
		reg[1 + i] = mmi_dp_set8_field(reg[1 + i], VSWING_MASK, dptx->link.vswing_level[i]);
		if (dptx->link.vswing_level[i] == 3)
			reg[1 + i] = mmi_dp_set8_field(reg[1 + i], MAX_VSWING_MASK, 1);
		else
			reg[1 + i] = mmi_dp_set8_field(reg[1 + i], MAX_VSWING_MASK, 0);

		reg[1 + i] = mmi_dp_set8_field(reg[1 + i], PREEMPH_MASK,
					       dptx->link.preemp_level[i]);
		if (dptx->link.preemp_level[i] == 3)
			reg[1 + i] = mmi_dp_set8_field(reg[1 + i], MAX_PREEMPH_MASK, 1);
		else
			reg[1 + i] = mmi_dp_set8_field(reg[1 + i], MAX_PREEMPH_MASK, 0);
	}

	mmi_dp_write_bytes_to_dpcd(dptx, DP_TRAINING_PATTERN_SET, reg, 5);

	return 0;
}

static int mmi_dp_adjust_drive_settings(struct dptx *dptx, bool *settings_changed)
{
	u8 bytes[2] = { 0 }, adj[4] = { 0 };
	u8 lanes;
	u8 i;

	*settings_changed = FALSE;
	lanes = dptx->link.lanes;
	bytes[0] = dptx->link.status[4];
	bytes[1] = dptx->link.status[5];

	switch (lanes) {
	case 4:
		adj[2] = bytes[1] & 0x0f;
		adj[3] = (bytes[1] & 0xf0) >> 4;
		fallthrough;
	case 2:
		adj[1] = (bytes[0] & 0xf0) >> 4;
		fallthrough;
	case 1:
		adj[0] = bytes[0] & 0x0f;
		break;
	default:
		dptx_warn(dptx, "Invalid number of lanes %d\n", lanes);
		return -EINVAL;
	}

	/* Save the drive settings */
	for (i = 0; i < lanes; i++) {
		u8 vs = adj[i] & 0x3;
		u8 pe = (adj[i] & 0xc) >> 2;

		if (dptx->link.vswing_level[i] != vs)
			*settings_changed = TRUE;

		dptx->link.vswing_level[i] = vs;
		dptx->link.preemp_level[i] = pe;
		dptx_dbg(dptx, "%s - SET PREEMP/VSWING VALUES [Lane %d]: vswing - %X preemp - %X",
			 __func__, i, vs, pe);
	}

	mmi_dp_adjust_vswing_and_preemphasis(dptx);

	return 0;
}

static int mmi_dp_cr_done_seq(struct dptx *dptx)
{
	bool settings_changed = FALSE;
	bool cr_fallback_required;
	bool max_vswing_achieved;
	u8 adj_req_ack_cnt = 1;
	u8 main_ack_cnt = 0;
	int ret = 1;

	/* Transmit TPS1 */
	if (mmi_dp_transmit_TPS1(dptx)) {
		/* Reset PHY */
		mmi_dp_power_up_phy(dptx);
		mmi_dp_power_state_change_phy(dptx, DPTX_PHY_INTER_P2_POWER);

		mmi_dp_transmit_TPS1(dptx);
	}

	/* Set TRAINING_PATTERN_SET and TRAINING_LANEx_SET registers */
	mmi_dp_set_training_set_regs(dptx, DP_TRAINING_PATTERN_1);

	do {
		/* Wait for 100us between training pattern set and reading Lane status */
		fsleep(100);

		/* Read LANEx_CR_DONE bits and ADJUST_REQUEST_LANEx_y regs */
		mmi_dp_read_bytes_from_dpcd(dptx, DP_LANE0_1_STATUS, dptx->link.status, 6);
		main_ack_cnt++;

		if (drm_dp_clock_recovery_ok(dptx->link.status, dptx->link.lanes)) {
			ret = CR_DONE;
		} else {
			dptx_err(dptx, "If Clock recovery is not ok");
			/* If one of the conditions achieved threshold */
			max_vswing_achieved = (dptx->link.vswing_level[0] == 3);
			cr_fallback_required = (max_vswing_achieved ||
						(adj_req_ack_cnt >= 5) ||
						(main_ack_cnt >= 10));

			if (cr_fallback_required) {
				ret = -CR_FAIL;
				break;
			}
			/* Adjust driver settings */
			mmi_dp_adjust_drive_settings(dptx, &settings_changed);
			/* Update TRAINING_LANE_SET regs */
			mmi_dp_link_training_lanes_set(dptx);
			settings_changed ? adj_req_ack_cnt = 0 : adj_req_ack_cnt++;
		}
	} while (ret != CR_DONE);

	return ret;
}

static int mmi_dp_reduce_link_rate(struct dptx *dptx)
{
	u8 rate = dptx->link.rate;

	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		return -ELOWESTRATE;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		rate = DPTX_PHYIF_CTRL_RATE_RBR;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		rate = DPTX_PHYIF_CTRL_RATE_HBR;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		rate = DPTX_PHYIF_CTRL_RATE_HBR2;
		break;
	}

	dptx->link.rate = rate;

	return RATE_REDUCTION;
}

static u8 dp_link_status(const u8 link_status[DP_LINK_STATUS_SIZE], int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static u8 dp_get_lane_status(const u8 link_status[DP_LINK_STATUS_SIZE],
			     int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	u8 l = dp_link_status(link_status, i);

	return (l >> s) & 0xf;
}

static bool mmi_dp_lane_cr_done(struct dptx *dptx, u8 lane)
{
	u8 lane_status;

	lane_status = dp_get_lane_status(dptx->link.status, lane);
	if ((lane_status & DP_LANE_CR_DONE) == 0)
		return FALSE;
	return TRUE;
}

static int reduce_link_lanes(struct dptx *dptx)
{
	u8 lanes;

	switch (dptx->link.lanes) {
	case 4:
		if (mmi_dp_lane_cr_done(dptx, 1) == FALSE) {
			fallthrough;
		} else {
			lanes = 2;
			break;
		}
	case 2:
		if (mmi_dp_lane_cr_done(dptx, 0) == FALSE) {
			fallthrough;
		} else {
			lanes = 1;
			break;
		}
	case 1:
	default:
		return -ELOWESTLANENR;
	}

	dptx->link.lanes = lanes;

	return 0;
}

static int mmi_dp_transmit_ch_eq_TPS(struct dptx *dptx)
{
	struct rx_capabilities rx_caps;
	u8 dp_pattern;
	u8 pattern;

	rx_caps = dptx->rx_caps;

	switch (dptx->max_rate) {
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		if (rx_caps.tps4_supported) {
			pattern = DPTX_PHYIF_CTRL_TPS_4;
			dp_pattern = DP_TRAINING_PATTERN_4;
			break;
		}
		fallthrough;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		if (rx_caps.tps3_supported) {
			pattern = DPTX_PHYIF_CTRL_TPS_3;
			dp_pattern = DP_TRAINING_PATTERN_3;
			break;
		}
		fallthrough;
	case DPTX_PHYIF_CTRL_RATE_HBR:
	case DPTX_PHYIF_CTRL_RATE_RBR:
		if (rx_caps.tps4_supported) {
			pattern = DPTX_PHYIF_CTRL_TPS_4;
			dp_pattern = DP_TRAINING_PATTERN_4;
		} else if (rx_caps.tps3_supported) {
			pattern = DPTX_PHYIF_CTRL_TPS_3;
			dp_pattern = DP_TRAINING_PATTERN_3;
		} else {
			pattern = DPTX_PHYIF_CTRL_TPS_2;
			dp_pattern = DP_TRAINING_PATTERN_2;
		}
		break;
	default:
		dptx_warn(dptx, "Invalid rate %d\n", dptx->link.rate);
		return -EINVAL;
	}

	mmi_dp_phy_set_pattern(dptx, pattern);

	/* Set TRAINING_PATTERN_SET and TRAINING_LANEx_SET registers */
	mmi_dp_set_training_set_regs(dptx, dp_pattern);

	return 0;
}

static int mmi_dp_wait_aux_rd_interval(struct dptx *dptx)
{
	u32 reg;
	u8 byte = 0;

	mmi_dp_read_dpcd(dptx, DP_TRAINING_AUX_RD_INTERVAL, &byte);

	/*
	 * DP_TRAINING_AUX_RD_INTERVAL contains the timeout values which can be
	 * 4ms, 8ms, 12ms, 16ms or 400 us as per DP 1.4 DPCD register
	 * TRAINING_AUX_RD_INTERVAL spec.
	 */
	reg = min_t(u32, (byte & 0x7f), 4);
	reg *= 4000;
	if (!reg)
		reg = 400;

	usleep_range(reg, reg + 100);

	return 0;
}

static bool mmi_dp_any_lane_cr_bit_done(struct dptx *dptx)
{
	u8 i;

	for (i = 0; i < dptx->link.lanes; i++) {
		if (mmi_dp_lane_cr_done(dptx, i))
			return TRUE;
	}

	return FALSE;
}

static int mmi_dp_ch_eq_done_seq(struct dptx *dptx)
{
	bool settings_changed;
	u8 main_ack_cnt = 0;
	int ret = 1;

	/* Transmit CH_EQ Pattern */
	dptx_dbg(dptx, "Transmit CH_EQ Pattern");
	mmi_dp_transmit_ch_eq_TPS(dptx);

	do {
		/* Wait specified Interval */
		dptx_dbg(dptx, "Wait specified Interval");
		mmi_dp_wait_aux_rd_interval(dptx);

		/* Read CR_DONE, CH_EQ_DONE, SYMBOL_LOCKED and ADJ_REQ */
		dptx_dbg(dptx, "Read CR_DONE, CH_EQ_DONE, SYMBOL_LOCKED and ADJ_REQ");
		mmi_dp_read_bytes_from_dpcd(dptx, DP_LANE0_1_STATUS, dptx->link.status, 6);

		/* Check CR Done remains */
		dptx_dbg(dptx, "Check if Clock Recovery is OK");
		if (!drm_dp_clock_recovery_ok(dptx->link.status, dptx->link.lanes)) {
			ret = -CH_EQ_FAIL;
			break;
		}

		dptx_dbg(dptx, "Check if Channel Equalization is OK");
		if (drm_dp_channel_eq_ok(dptx->link.status, dptx->link.lanes)) {
			ret = CH_EQ_DONE;
			break;
		}

		dptx_err(dptx, "Channel EQ bits not OK");
		main_ack_cnt++;
		if (main_ack_cnt > 5) {
			ret = -CH_EQ_FAIL;
			break;
		}

		mmi_dp_adjust_drive_settings(dptx, &settings_changed);
		mmi_dp_link_training_lanes_set(dptx);

		dptx_dbg(dptx, "Driver settings adjusted");
	} while (1);

	return ret;
}

static int mmi_dp_check_allowed_link_configs(struct dptx *dptx)
{
	u8 sink_max_rate;

	dptx->link.lanes = min_t(u8, dptx->link.lanes, dptx->rx_caps.max_lane_count);

	sink_max_rate = mmi_dp_bw_to_phy_rate(dptx->rx_caps.max_link_rate);
	dptx->link.rate = min_t(u8, dptx->link.rate, sink_max_rate);

	return 0;
}

int mmi_dp_full_link_training(struct dptx *dptx)
{
	int ret, retval;

	/* Guarantee lanes and rates are supported by Source and Sink */
	mmi_dp_check_allowed_link_configs(dptx);

	do {
		/* DPCD Link Configuration */
		mmi_dp_dpcd_link_configuration(dptx);

		/* CR Done Sequence */
		do {
			/* Reset Vswing and Preemph to minimum value */
			memset(dptx->link.preemp_level, 0, sizeof(u8) * 4);
			memset(dptx->link.vswing_level, 0, sizeof(u8) * 4);
			/* Clean Link Status Info */
			memset(dptx->link.status, 0, DP_LINK_STATUS_SIZE);

			mmi_dp_adjust_vswing_and_preemphasis(dptx);
			ret = mmi_dp_cr_done_seq(dptx);
			if (ret != CR_DONE) {
				/* Reduce Link Rate */
				ret = mmi_dp_reduce_link_rate(dptx);
				/* Already RBR? Reduce Lanes */
				if (ret == -ELOWESTRATE) {
					ret = reduce_link_lanes(dptx);
					dptx->link.rate = dptx->max_rate;
					mmi_dp_check_allowed_link_configs(dptx);

					/* Not achieved? Stop Link Training */
					if (ret == -ELOWESTLANENR) {
						ret = -LT_CR_FAIL;
						break;
					}
				}
				/* Force no Transmitted Pattern */
				mmi_dp_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);
				mmi_dp_write_dpcd(dptx, DP_TRAINING_PATTERN_SET,
						  DP_TRAINING_PATTERN_DISABLE);

				/* DPCD Link Configuration - There is a lane/rate change */
				mmi_dp_dpcd_link_configuration(dptx);
			}
		} while (ret != CR_DONE);

		/* Clock Recovery Process Failed, stop LT */
		if (ret == -LT_CR_FAIL)
			break;

		/* Channel EQ Done Sequence */
		do {
			ret = mmi_dp_ch_eq_done_seq(dptx);
			if (ret != CH_EQ_DONE) {
				if (mmi_dp_any_lane_cr_bit_done(dptx)) {
					ret = reduce_link_lanes(dptx);
					if (ret == 0) {
						ret = LANE_REDUCTION;
						break;
					}
				}

				ret = mmi_dp_reduce_link_rate(dptx);
				if (ret == -ELOWESTRATE) {
					ret = -LT_CH_EQ_FAIL;
					break;
				}
				dptx->link.lanes = dptx->max_lanes;
				mmi_dp_check_allowed_link_configs(dptx);
				break;
			}
		} while (ret != CH_EQ_DONE);

		if (ret == -LT_CH_EQ_FAIL)
			break;
		else if (ret == CH_EQ_DONE)
			ret = LT_DONE;

	} while (ret != LT_DONE);

	/* Clean Pattern and end LT */
	mmi_dp_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);
	mmi_dp_write_dpcd(dptx, DP_TRAINING_PATTERN_SET, DP_TRAINING_PATTERN_DISABLE);

	if (ret == LT_DONE) {
		/* dptx_phy_enable_xmit(dptx, dptx->link.lanes, true); */
		dptx->link.trained = true;
		dptx_info(dptx, "Successful Link Training - Rate: %d Lanes: %d",
			  dptx->link.rate, dptx->link.lanes);
		dptx->multipixel = DPTX_MP_SINGLE_PIXEL;
		mmi_dp_set_video_dynamic_range(dptx, 1);
		mmi_dp_set_video_colorimetry(dptx, 1); /* for 601 */

		retval = mmi_dp_set_bpc(dptx, COLOR_DEPTH_8);
		if (retval)
			dptx_info(dptx, "mmi_dp_set_bpc failed");

		retval = mmi_dp_set_video_format(dptx, VCEA);
		if (retval)
			dptx_info(dptx, "mmi_dp_set_video_format failed");

		retval = mmi_dp_set_pixel_enc(dptx, RGB);
		if (retval)
			dptx_info(dptx, "mmi_dp_set_pixel_enc failed");

		retval = mmi_dp_set_video_mode(dptx); /* 1920x1080@60 */
		if (retval)
			dptx_info(dptx, "mmi_dp_set_video_mode failed");
	} else {
		dptx_info(dptx, "Link Training Failed");
	}

	return ret;
}

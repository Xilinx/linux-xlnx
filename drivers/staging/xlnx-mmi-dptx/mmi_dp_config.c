// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated DisplayPort Tx driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include <drm/drm_fixed.h>
#include <linux/kernel.h>

#include "mmi_dp.h"
#include "mmi_dp_config.h"

/* Configuration Api's */

u8 mmi_dp_bit_field(const u16 data, u8 shift, u8 width)
{
	return ((data >> shift) & ((((u16)1) << width) - 1));
}

u16 mmi_dp_concat_bits(u8 bhi, u8 ohi, u8 nhi, u8 blo, u8 olo, u8 nlo)
{
	return (mmi_dp_bit_field(bhi, ohi, nhi) << nlo) |
	       mmi_dp_bit_field(blo, olo, nlo);
}

u16 mmi_dp_byte_to_word(const u8 hi, const u8 lo)
{
	return mmi_dp_concat_bits(hi, 0, 8, lo, 0, 8);
}

static u8 mmi_dp_get_bpc_mapping(u8 pix_enc, u8 bpc)
{
	u8 bpc_mapping = 0;

	switch (pix_enc) {
	case RGB:

		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 0;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR444:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR422:

		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YONLY:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case RAW:
		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 3;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 4;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 5;
		else if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 7;
		break;
	default:
		bpc_mapping = 0;
	}

	return bpc_mapping;
}

static u8 mmi_dp_get_color_mapping(u8 pix_enc, u8 dynamic_range, u8 colorimetry)
{
	u8 col_mapping = 0;

	/* According to Table 2-94 of DisplayPort spec 1.4 */
	switch (pix_enc) {
	case RGB:
		if (dynamic_range == CEA)
			col_mapping = 4;
		else if (dynamic_range == VESA)
			col_mapping = 0;
		break;
	case YCBCR422:
		if (colorimetry == ITU601)
			col_mapping = 5;
		else if (colorimetry == ITU709)
			col_mapping = 13;
		break;
	case YCBCR444:
		if (colorimetry == ITU601)
			col_mapping = 6;
		else if (colorimetry == ITU709)
			col_mapping = 14;
		break;
	case RAW:
		col_mapping = 1;
		break;
	case YCBCR420:
	case YONLY:
		col_mapping = 0;
		break;
	}

	return col_mapping;
}

static u8 get_video_mapping(u8 bpc, u8 pixel_encoding)
{
	u8 bpc_mapping = 1;

	switch (pixel_encoding) {
	case RGB:
		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 0;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR444:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 5;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 6;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 7;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 8;
		break;
	case YCBCR422:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 9;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 10;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 11;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 12;
		break;
	case YCBCR420:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 13;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 14;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 15;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 16;
		break;
	case YONLY:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 17;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 18;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 19;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 20;
		break;
	case RAW:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 23;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 24;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 25;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 27;
		break;
	}

	return bpc_mapping;
}

static void mmi_dp_disable_sdp(struct dptx *dptx, u32 *payload)
{
	int i;

	for (i = 0; i < DPTX_SDP_NUM; i++)
		if (!memcmp(dptx->sdp_list[i].payload, payload, DPTX_SDP_LEN))
			memset(dptx->sdp_list[i].payload, 0, DPTX_SDP_SIZE);
}

static void mmi_dp_enable_sdp(struct dptx *dptx, struct sdp_full_data *data)
{
	int i, reg_num, sdp_offset;
	u32 reg, header;

	header = (__force u32)cpu_to_be32(data->payload[0]);
	for (i = 0; i < DPTX_SDP_NUM; i++)
		if (dptx->sdp_list[i].payload[0] == 0) {
			dptx->sdp_list[i].payload[0] = header;
			sdp_offset = i * DPTX_SDP_SIZE;
			reg_num = 0;
			while (reg_num < DPTX_SDP_LEN) {
				mmi_dp_write(dptx->base, SDP_REGISTER_BANK_0 +
				sdp_offset + reg_num * 4,
				(__force u32)cpu_to_be32(data->payload[reg_num]));
				reg_num++;
			}
			switch (data->blanking) {
			case 0:
				reg = mmi_dp_read(dptx->base, SDP_VERTICAL_CTRL);
				reg |= (1 << (2 + i));
				mmi_dp_write(dptx->base, SDP_VERTICAL_CTRL, reg);
				break;
			case 1:
				reg = mmi_dp_read(dptx->base, SDP_HORIZONTAL_CTRL);
				reg |= (1 << (2 + i));
				mmi_dp_write(dptx->base, SDP_HORIZONTAL_CTRL, reg);
				break;
			case 2:
				reg = mmi_dp_read(dptx->base, SDP_VERTICAL_CTRL);
				reg |= (1 << (2 + i));
				mmi_dp_write(dptx->base, SDP_VERTICAL_CTRL, reg);
				reg = mmi_dp_read(dptx->base, SDP_HORIZONTAL_CTRL);
				reg |= (1 << (2 + i));
				mmi_dp_write(dptx->base, SDP_HORIZONTAL_CTRL, reg);
				break;
			}
			break;
		}
}

static void mmi_dp_fill_sdp(struct dptx *dptx, struct sdp_full_data *data)
{
	if (data->en == 1)
		mmi_dp_enable_sdp(dptx, data);
	else
		mmi_dp_disable_sdp(dptx, data->payload);
}

void mmi_dp_video_config1(struct dptx *dptx, u8 stream)
{
	struct dtd *mdtd;
	u16 h_blank;

	mdtd = &dptx->vparams[stream].mdtd;

	mmi_dp_write_mask(dptx, DPTX_VIDEO_CONFIG1_N(stream),
			  H_ACTIVE_MASK, mdtd->h_active);
	h_blank = mdtd->h_blanking + mdtd->h_border;
	mmi_dp_write_mask(dptx, DPTX_VIDEO_CONFIG1_N(stream), H_BLANK_MASK, h_blank);
	mmi_dp_write_mask(dptx, DPTX_VIDEO_CONFIG1_N(stream), I_P_MASK, mdtd->interlaced);
	mmi_dp_write_mask(dptx, DPTX_VIDEO_CONFIG1_N(stream), R_V_BLANK_IN_OSC_MASK, 0);
}

void mmi_dp_video_config2(struct dptx *dptx, u8 stream)
{
	struct dtd *mdtd;
	u16 v_blank;

	mdtd = &dptx->vparams[stream].mdtd;

	v_blank = mdtd->v_blanking + mdtd->v_border;
	mmi_dp_write_mask(dptx, DPTX_VIDEO_CONFIG2_N(stream), V_BLANK_MASK, v_blank);
	mmi_dp_write_mask(dptx, DPTX_VIDEO_CONFIG2_N(stream), V_ACTIVE_MASK, mdtd->v_active);
}

void mmi_dp_video_config3(struct dptx *dptx, u8 stream)
{
	struct dtd *mdtd;

	mdtd = &dptx->vparams[stream].mdtd;

	mmi_dp_write_mask(dptx, DPTX_VIDEO_CONFIG3_N(stream),
			  H_SYNC_WIDTH_MASK, mdtd->h_sync_pulse_width);
}

void mmi_dp_video_config4(struct dptx *dptx, u8 stream)
{
	struct dtd *mdtd;

	mdtd = &dptx->vparams[stream].mdtd;

	mmi_dp_write_mask(dptx, DPTX_VIDEO_CONFIG4_N(stream),
			  V_SYNC_WIDTH_MASK, mdtd->v_sync_pulse_width);
}

void mmi_dp_video_msa1(struct dptx *dptx, u8 stream)
{
	struct dtd *mdtd;
	u16 v_start, h_start;

	mdtd = &dptx->vparams[stream].mdtd;

	v_start = mdtd->v_blanking - mdtd->v_sync_offset + (mdtd->v_border / 2);
	mmi_dp_write_mask(dptx, DPTX_VIDEO_MSA1_N(stream), MSA_V_START_MASK, v_start);

	h_start = mdtd->h_blanking - mdtd->h_sync_offset + (mdtd->h_border / 2);
	mmi_dp_write_mask(dptx, DPTX_VIDEO_MSA1_N(stream), MSA_H_START_MASK, h_start);
}

void mmi_dp_video_msa2(struct dptx *dptx, u8 stream)
{
	u8 bpc_mapping, col_mapping, bpc, dynamic_range, colorimetry;
	struct video_params *vparams;
	enum pixel_enc_type pix_enc;

	vparams = &dptx->vparams[stream];
	pix_enc = vparams->pix_enc;
	bpc = vparams->bpc;
	dynamic_range = vparams->dynamic_range;
	colorimetry = vparams->colorimetry;

	mmi_dp_write_mask(dptx, DPTX_VIDEO_MSA2_N(stream), MSA_MISC0_SYNC_MODE_MASK, 0);
	col_mapping = mmi_dp_get_color_mapping(pix_enc, dynamic_range, colorimetry);
	mmi_dp_write_mask(dptx, DPTX_VIDEO_MSA2_N(stream),
			  MSA_MISC0_COLOR_MAP_MASK, col_mapping);
	bpc_mapping = mmi_dp_get_bpc_mapping(pix_enc, bpc);
	mmi_dp_write_mask(dptx, DPTX_VIDEO_MSA2_N(stream),
			  MSA_MISC0_BPC_MAP_MASK, bpc_mapping);
}

void mmi_dp_video_msa3(struct dptx *dptx, u8 stream)
{
	enum pixel_enc_type pix_enc;
	u8 pix_enc_map;

	pix_enc = dptx->vparams[stream].pix_enc;

	switch (pix_enc) {
	case YCBCR420:
		pix_enc_map = 1;
		break;
	case YONLY:
	case RAW:
		pix_enc_map = 2;
		break;
	default:
		pix_enc_map = 0;
		break;
	}

	mmi_dp_write_mask(dptx, DPTX_VIDEO_MSA3_N(stream),
			  MSA_MISC1_PIX_ENC_MASK, pix_enc_map);
}

static int mmi_dp_calculate_hblank_interval(struct dptx *dptx, u8 stream)
{
	struct video_params *vparams;
	int hblank_interval;
	int pixel_clk;
	u32 link_clk;
	u16 h_blank;
	u8 rate;
	s64 fixp;

	vparams = &dptx->vparams[stream];
	pixel_clk = vparams->mdtd.pixel_clock;
	h_blank = vparams->mdtd.h_blanking;
	rate = dptx->link.rate;

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
		dptx_warn(dptx, "Invalid rate 0x%x\n", rate);
		return -EINVAL;
	}

	fixp = drm_fixp_mul(drm_int2fixp(h_blank), drm_int2fixp(link_clk));
	fixp = drm_fixp_div(fixp, drm_int2fixp(pixel_clk));
	hblank_interval = drm_fixp2int(fixp);

	return hblank_interval;
}

void mmi_dp_video_hblank_interval(struct dptx *dptx, u8 stream)
{
	u16 hblank_interval;

	hblank_interval = mmi_dp_calculate_hblank_interval(dptx, stream);
	dptx_dbg(dptx, "HBLANK INTERVAL: %d", hblank_interval);
	mmi_dp_write_mask(dptx, DPTX_VIDEO_HBLANK_INTERVAL_N(stream),
			  H_BLANK_INTERVAL_MASK, hblank_interval);
}

void mmi_dp_vinput_polarity_ctrl(struct dptx *dptx, u8 stream)
{
	struct dtd *mdtd;

	mdtd = &dptx->vparams[stream].mdtd;

	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_POLARITY_CTRL_N(stream),
			  H_SYNC_IN_POLARITY_MASK, mdtd->h_sync_polarity);
	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_POLARITY_CTRL_N(stream),
			  V_SYNC_IN_POLARITY_MASK, mdtd->v_sync_polarity);
}

void mmi_dp_vsample_ctrl(struct dptx *dptx, u8 stream)
{
	u8 video_mapping;

	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_CTRL_N(stream),
			  VIDEO_MAPPING_IPI_EN_MASK, 0);
	video_mapping = get_video_mapping(dptx->vparams[stream].bpc,
					  dptx->vparams[stream].pix_enc);
	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_CTRL_N(stream),
			  VIDEO_MAPPING_MASK, video_mapping);
	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_CTRL_N(stream),
			  PIXEL_MODE_SELECT_MASK, dptx->multipixel);
}

void mmi_dp_disable_video_stream(struct dptx *dptx, u8 stream)
{
	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_CTRL_N(stream), VIDEO_STREAM_ENABLE_MASK, 0);
}

void mmi_dp_vsd_ycbcr420_send(struct dptx *dptx, u8 enable)
{
	struct sdp_full_data vsc_data;
	int i;

	struct video_params *vparams;

	vparams = &dptx->vparams[0];

	vsc_data.en = enable;
	for (i = 0; i < 9; i++) {
		if (i == 0)
			vsc_data.payload[i] = 0x00070513;
		else if (i == 5)
			switch (vparams->bpc) {
			case COLOR_DEPTH_8:
				vsc_data.payload[i] = 0x30010000;
				break;
			case COLOR_DEPTH_10:
				vsc_data.payload[i] = 0x30020000;
				break;
			case COLOR_DEPTH_12:
				vsc_data.payload[i] = 0x30030000;
				break;
			case COLOR_DEPTH_16:
				vsc_data.payload[i] = 0x30040000;
				break;
			}
		else
			vsc_data.payload[i] = 0x0;
	}
	vsc_data.blanking = 0;
	vsc_data.cont = 1;

	mmi_dp_fill_sdp(dptx, &vsc_data);
}

void mmi_dp_video_ts_change(struct dptx *dptx, int stream)
{
	u32 reg;
	struct video_params *vparams;

	vparams = &dptx->vparams[0];

	reg = mmi_dp_read(dptx->base, DPTX_VIDEO_CONFIG5_N(stream));
	reg = reg & (~DPTX_VIDEO_CONFIG5_TU_MASK);
	reg = reg | (vparams->aver_bytes_per_tu <<
			DPTX_VIDEO_CONFIG5_TU_SHIFT);
	reg = reg & (~DPTX_VIDEO_CONFIG5_TU_FRAC_MASK_SST);
	reg = reg | (vparams->aver_bytes_per_tu_frac <<
			     DPTX_VIDEO_CONFIG5_TU_FRAC_SHIFT_SST);
	reg = reg & (~DPTX_VIDEO_CONFIG5_INIT_THRESHOLD_MASK);
	reg = reg | (vparams->init_threshold <<
		      DPTX_VIDEO_CONFIG5_INIT_THRESHOLD_SHIFT);
	mmi_dp_write(dptx->base, DPTX_VIDEO_CONFIG5_N(stream), reg);
}

static void mmi_dp_video_set_core_bpc(struct dptx *dptx, int stream)
{
	u32 reg;
	u8 bpc_mapping = 0, bpc = 0;
	enum pixel_enc_type pix_enc;
	struct video_params *vparams;

	vparams = &dptx->vparams[0];
	bpc = vparams->bpc;
	pix_enc = vparams->pix_enc;

	reg = mmi_dp_read(dptx->base, DPTX_VSAMPLE_CTRL_N(stream));
	reg &= ~DPTX_VSAMPLE_CTRL_VMAP_BPC_MASK;

	bpc_mapping = get_video_mapping(bpc, pix_enc);
	reg |= (bpc_mapping << DPTX_VSAMPLE_CTRL_VMAP_BPC_SHIFT);
	mmi_dp_write(dptx->base, DPTX_VSAMPLE_CTRL_N(stream), reg);
}

static void mmi_dp_video_set_sink_col(struct dptx *dptx, int stream)
{
	u32 reg_msa2;
	u8 col_mapping;
	u8 colorimetry;
	u8 dynamic_range;
	struct video_params *vparams;
	enum pixel_enc_type pix_enc;

	vparams = &dptx->vparams[0];
	pix_enc = vparams->pix_enc;
	colorimetry = vparams->colorimetry;
	dynamic_range = vparams->dynamic_range;

	reg_msa2 = mmi_dp_read(dptx->base, DPTX_VIDEO_MSA2_N(stream));
	reg_msa2 &= ~DPTX_VIDEO_VMSA2_COL_MASK;

	col_mapping = 0;

	/* According to Table 2-94 of DisplayPort spec 1.3 */
	switch (pix_enc) {
	case RGB:
		if (dynamic_range == CEA)
			col_mapping = 4;
		else if (dynamic_range == VESA)
			col_mapping = 0;
		break;
	case YCBCR422:
		if (colorimetry == ITU601)
			col_mapping = 5;
		else if (colorimetry == ITU709)
			col_mapping = 13;
		break;
	case YCBCR444:
		if (colorimetry == ITU601)
			col_mapping = 6;
		else if (colorimetry == ITU709)
			col_mapping = 14;
		break;
	case RAW:
		col_mapping = 1;
		break;
	case YCBCR420:
	case YONLY:
		break;
	}

	reg_msa2 |= (col_mapping << DPTX_VIDEO_VMSA2_COL_SHIFT);
	mmi_dp_write(dptx->base, DPTX_VIDEO_MSA2_N(stream), reg_msa2);
}

static void mmi_dp_video_set_sink_bpc(struct dptx *dptx, int stream)
{
	u32 reg_msa2, reg_msa3;
	u8 bpc_mapping = 0, bpc = 0;
	struct video_params *vparams;
	enum pixel_enc_type pix_enc;

	vparams = &dptx->vparams[0];
	pix_enc = vparams->pix_enc;
	bpc = vparams->bpc;

	reg_msa2 = mmi_dp_read(dptx->base, DPTX_VIDEO_MSA2_N(stream));
	reg_msa3 = mmi_dp_read(dptx->base, DPTX_VIDEO_MSA3_N(stream));

	reg_msa2 &= ~DPTX_VIDEO_VMSA2_BPC_MASK;
	reg_msa3 &= ~DPTX_VIDEO_VMSA3_PIX_ENC_MASK;

	switch (pix_enc) {
	case RGB:
		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 0;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR444:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR422:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR420:
		reg_msa3 |= DPTX_VIDEO_VMSA3_PIX_ENC_YCBCR420;
		break;
	case YONLY:
		/* According to Table 2-94 of DisplayPort spec 1.3 */
		reg_msa3 |= DPTX_VIDEO_VMSA3_PIX_ENC;

		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case RAW:
		/* According to Table 2-94 of DisplayPort spec 1.3 */
		reg_msa3 |= DPTX_VIDEO_VMSA3_PIX_ENC;

		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 3;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 4;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 5;
		else if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 7;
		break;
	}

	reg_msa2 |= (bpc_mapping << DPTX_VIDEO_VMSA2_BPC_SHIFT);

	mmi_dp_write(dptx->base, DPTX_VIDEO_MSA2_N(stream), reg_msa2);
	mmi_dp_write(dptx->base, DPTX_VIDEO_MSA3_N(stream), reg_msa3);

	mmi_dp_video_set_sink_col(dptx, stream);
}

void mmi_dp_video_bpc_change(struct dptx *dptx, int stream)
{
	mmi_dp_video_set_core_bpc(dptx, stream);
	mmi_dp_video_set_sink_bpc(dptx, stream);
}

void mmi_dp_disable_default_video_stream(struct dptx *dptx, int stream)
{
	u32 vsamplectrl;

	vsamplectrl = mmi_dp_read(dptx->base, DPTX_VSAMPLE_CTRL_N(stream));
	vsamplectrl &= ~DPTX_VSAMPLE_CTRL_STREAM_EN;
	mmi_dp_write(dptx->base, DPTX_VSAMPLE_CTRL_N(stream), vsamplectrl);
}

void mmi_dp_enable_default_video_stream(struct dptx *dptx, int stream)
{
	u32 vsamplectrl;

	vsamplectrl = mmi_dp_read(dptx->base, DPTX_VSAMPLE_CTRL_N(stream));
	vsamplectrl |= DPTX_VSAMPLE_CTRL_STREAM_EN;
	mmi_dp_write(dptx->base, DPTX_VSAMPLE_CTRL_N(stream), vsamplectrl);
}

/*
 * DTD
 */
void mmi_dp_dtd_reset(struct dtd *mdtd)
{
	mdtd->pixel_repetition_input = 0;
	mdtd->pixel_clock = 0;
	mdtd->h_active = 0;
	mdtd->h_border = 0;
	mdtd->h_blanking = 0;
	mdtd->h_sync_offset = 0;
	mdtd->h_sync_pulse_width = 0;
	mdtd->h_image_size = 0;
	mdtd->v_active = 0;
	mdtd->v_border = 0;
	mdtd->v_blanking = 0;
	mdtd->v_sync_offset = 0;
	mdtd->v_sync_pulse_width = 0;
	mdtd->v_image_size = 0;
	mdtd->interlaced = 0;
	mdtd->v_sync_polarity = 0;
	mdtd->h_sync_polarity = 0;
}

u8 mmi_dp_get_color_depth_bpp(u8 bpc, u8 encoding)
{
	u8 color_dep;

	switch (bpc) {
	case COLOR_DEPTH_6:
		color_dep = 18;
		break;
	case COLOR_DEPTH_8:
		if (encoding == YCBCR420)
			color_dep = 12;
		else if (encoding == YCBCR422)
			color_dep = 16;
		else if (encoding == YONLY)
			color_dep = 8;
		else
			color_dep = 24;
		break;
	case COLOR_DEPTH_10:
		if (encoding == YCBCR420)
			color_dep = 15;
		else if (encoding == YCBCR422)
			color_dep = 20;
		else if (encoding == YONLY)
			color_dep = 10;
		else
			color_dep = 30;
		break;

	case COLOR_DEPTH_12:
		if (encoding == YCBCR420)
			color_dep = 18;
		else if (encoding == YCBCR422)
			color_dep = 24;
		else if (encoding == YONLY)
			color_dep = 12;
		else
			color_dep = 36;
		break;

	case COLOR_DEPTH_16:
		if (encoding == YCBCR420)
			color_dep = 24;
		else if (encoding == YCBCR422)
			color_dep = 32;
		else if (encoding == YONLY)
			color_dep = 16;
		else
			color_dep = 48;
		break;
	default:
		color_dep = 18;
		break;
	}

	return color_dep;
}

u16 mmi_dp_get_link_rate(u8 rate)
{
	u16 link_rate;

	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		link_rate = 162;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		link_rate = 270;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		link_rate = 540;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		link_rate = 810;
		break;
	default:
		link_rate = 162;
	}

	return link_rate;
}

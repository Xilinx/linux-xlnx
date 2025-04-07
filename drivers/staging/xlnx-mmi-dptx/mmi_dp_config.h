/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multimedia Integrated DisplayPort Tx driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __MMI_DP_CONFIG_H__
#define __MMI_DP_CONFIG_H__

struct dptx;

struct short_audio_desc_t {
	u8 m_format;
	u8 m_max_channels;
	u8 m_sample_rates;
	u8 m_byte3;
};

struct speaker_allocation_data_block_t {
	u8 m_byte1;
	u8 m_byte2;
	u8 m_byte3;
	int m_valid;
};

struct room_config_data_block_t {
	u8 m_speaker_count;
	u8 m_sld;
	u8 m_speaker;
	u8 m_display;
	u8 m_spm1;
	u8 m_spm2;
	u8 m_spm3;
	int m_valid;
};

enum pixel_enc_type {
	RGB = 0,
	YCBCR420 = 1,
	YCBCR422 = 2,
	YCBCR444 = 3,
	YONLY = 4,
	RAW = 5
};

enum color_depth {
	COLOR_DEPTH_INVALID = 0,
	COLOR_DEPTH_6 = 6,
	COLOR_DEPTH_8 = 8,
	COLOR_DEPTH_10 = 10,
	COLOR_DEPTH_12 = 12,
	COLOR_DEPTH_16 = 16
};

enum dynamic_range_type {
	CEA = 1,
	VESA = 2
};

enum colorimetry_type {
	ITU601 = 1,
	ITU709 = 2
};

enum video_format_type {
	VCEA = 0,
	CVT = 1,
	DMT = 2
};

struct dtd {
	u16 pixel_repetition_input;
	int pixel_clock;
	u8 interlaced; /* 1 for interlaced, 0 progressive */
	u16 h_active;
	u16 h_border;
	u16 h_blanking;
	u16 h_image_size;
	u16 h_sync_offset;
	u16 h_sync_pulse_width;
	u8 h_sync_polarity;
	u16 v_active;
	u16 v_border;
	u16 v_blanking;
	u16 v_image_size;
	u16 v_sync_offset;
	u16 v_sync_pulse_width;
	u8 v_sync_polarity;
};

struct dtd_t {
	/* VIC code */
	u32 m_code;

	/* Identifies modes that ONLY can be displayed in YCC 4:2:0 */
	u8 m_limited_to_ycc420;

	/* Identifies modes that can also be displayed in YCC 4:2:0 */
	u8 m_ycc420;

	u16 m_pixel_repetition_factor;

	/* In units of 1kHz */
	u32 m_pixel_clock;

	/* 1 for interlaced, 0 progressive */
	u8 m_interlaced;

	u16 m_h_active;

	u16 m_h_blanking;

	u16 m_h_border;

	u16 m_h_image_size;

	u16 m_h_sync_offset;

	u16 m_h_sync_pulse_width;

	/* 0 for Active low, 1 active high */
	u8 m_h_sync_polarity;

	u16 m_v_active;

	u16 m_v_blanking;

	u16 m_v_border;

	u16 m_v_image_size;

	u16 m_v_sync_offset;

	u16 m_v_sync_pulse_width;

	/* 0 for Active low, 1 active high */
	u8 m_v_sync_polarity;
};

struct display_mode_t {
	u32 refresh_rate;
	struct dtd_t dtd;
};

struct video_params {
	u8 pix_enc;
	u8 pattern_mode;
	struct dtd mdtd;
	u8 mode;
	u8 bpc;
	u8 colorimetry;
	u8 dynamic_range;
	u8 vc_payload;
	u16 pbn;
	u8 aver_bytes_per_tu;
	u8 aver_bytes_per_tu_frac;
	u8 init_threshold;
	u32 refresh_rate;
	u8 video_format;
};

struct short_video_desc_t {
	int m_native;
	unsigned int m_code;
	unsigned int m_limited_to_ycc420;
	unsigned int m_ycc420;
};

struct monitor_range_limits_t {
	u8 m_min_vertical_rate;
	u8 m_max_vertical_rate;
	u8 m_min_horizontal_rate;
	u8 m_max_horizontal_rate;
	u8 m_max_pixel_clock;
	int m_valid;
};

struct video_capability_data_block_t {
	int m_quantization_range_selectable;
	u8 m_preferred_timing_scan_info;
	u8 m_it_scan_info;
	u8 m_ce_scan_info;
	int m_valid;
};

struct colorimetry_data_block_t {
	u8 m_byte3;
	u8 m_byte4;
	int m_valid;
};

u8 mmi_dp_bit_field(const u16 data, u8 shift, u8 width);
u16 mmi_dp_concat_bits(u8 bhi, u8 ohi, u8 nhi, u8 blo, u8 olo, u8 nlo);
u16 mmi_dp_byte_to_word(const u8 hi, const u8 lo);
int mmi_dp_dtd_fill(struct dtd *mdtd, struct display_mode_t *display_mode);
void mmi_dp_dtd_reset(struct dtd *mdtd);
void mmi_dp_video_bpc_change(struct dptx *dptx, int stream);
void mmi_dp_video_ts_change(struct dptx *dptx, int stream);
void mmi_dp_video_ts_calculate(struct dptx *dptx, int lane_num, int rate,
			       int bpc, int encoding, int pixel_clock);
void mmi_dp_enable_default_video_stream(struct dptx *dptx, int stream);
void mmi_dp_disable_default_video_stream(struct dptx *dptx, int stream);
void mmi_dp_vsd_ycbcr420_send(struct dptx *dptx, u8 enable);
u16 mmi_dp_get_link_rate(u8 rate);
u8 mmi_dp_get_color_depth_bpp(u8 bpc, u8 encoding);
int mmi_dp_sst_configuration(struct dptx *dptx);

/* MST-Configuration */
void mmi_dp_video_config1(struct dptx *dptx, u8 stream);
void mmi_dp_video_config2(struct dptx *dptx, u8 stream);
void mmi_dp_video_config3(struct dptx *dptx, u8 stream);
void mmi_dp_video_config4(struct dptx *dptx, u8 stream);
void mmi_dp_video_msa1(struct dptx *dptx, u8 stream);
void mmi_dp_video_msa2(struct dptx *dptx, u8 stream);
void mmi_dp_video_msa3(struct dptx *dptx, u8 stream);
void mmi_dp_video_hblank_interval(struct dptx *dptx, u8 stream);
void mmi_dp_vinput_polarity_ctrl(struct dptx *dptx, u8 stream);
void mmi_dp_vsample_ctrl(struct dptx *dptx, u8 stream);
void mmi_dp_disable_video_stream(struct dptx *dptx, u8 stream);

#endif

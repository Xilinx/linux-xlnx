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
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/hdr-ctrls.h>
#include <uapi/linux/videodev2.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <linux/hdmi.h>
#include <sound/soc.h>
#include <sound/pcm_drm_eld.h>

/* Link configuration registers */
#define XDPTX_LINKBW_SET_REG			0x0
#define XDPTX_LANECNT_SET_REG			0x4
#define XDPTX_DPCD_LANECNT_SET_MASK		GENMASK(4, 0)
#define XDPTX_EFRAME_EN_REG			0x8
#define XDPTX_TRNGPAT_SET_REG			0xc
#define XDPTX_SCRAMBLING_DIS_REG		0x14
#define XDPTX_DOWNSPREAD_CTL_REG		0x18
#define XDPTX_DPCD_SPREAD_AMP_MASK		BIT(4)
#define XDPTX_SOFT_RST				0x1c
#define XDPTX_SOFT_RST_VIDEO_STREAM_ALL_MASK	GENMASK(3, 0)
#define XDPTX_SOFT_RST_HDCP_MASK		BIT(8)

/* GtCtrl Registers */
#define XDPTX_GTCTL_REG				0x4c
#define XDPTX_GTCTL_VSWING_MASK			GENMASK(12, 8)
#define XDPTX_GTCTL_POST_CUR_MASK		GENMASK(22, 18)
#define XDPTX_GTCTL_LINE_RATE_MASK		GENMASK(2, 1)
#define XDPTX_GTCTL_LINE_RATE_810G		0x03
#define XDPTX_GTCTL_LINE_RATE_540G		0x02
#define XDPTX_GTCTL_LINE_RATE_270G		0x01
#define XDPTX_GTCTL_LINE_RATE_162G		0x00

/* GTHE4 */
#define XVPHY_GTHE4_DIFF_SWING_DP_V0P0		0x01
#define XVPHY_GTHE4_DIFF_SWING_DP_V0P1		0x02
#define XVPHY_GTHE4_DIFF_SWING_DP_V0P2		0x05
#define XVPHY_GTHE4_DIFF_SWING_DP_V0P3		0x0b
#define XVPHY_GTHE4_DIFF_SWING_DP_V1P0		0x02
#define XVPHY_GTHE4_DIFF_SWING_DP_V1P1		0x05
#define XVPHY_GTHE4_DIFF_SWING_DP_V1P2		0x07
#define XVPHY_GTHE4_DIFF_SWING_DP_V2P0		0x04
#define XVPHY_GTHE4_DIFF_SWING_DP_V2P1		0x07
#define XVPHY_GTHE4_DIFF_SWING_DP_V3P0		0x08
#define XVPHY_GTHE4_PREEMP_DP_L0		0x03
#define XVPHY_GTHE4_PREEMP_DP_L1		0x0d
#define XVPHY_GTHE4_PREEMP_DP_L2		0x16
#define XVPHY_GTHE4_PREEMP_DP_L3		0x1d

/* DPTX Core enable registers */
#define XDPTX_ENABLE_REG			0x80
#define XDPTX_MAINSTRM_ENABLE_REG		0x84
#define XDPTX_SCRAMBLER_RESET			0xc0
#define XDPTX_SCRAMBLER_RESET_MASK		BIT(0)
#define XDPTX_MST_CONFIG			0xd0

/* AUX channel interface registers */
#define XDPTX_AUXCMD_REG			0x100
#define XDPTX_AUX_WRITEFIFO_REG			0x104
#define XDPTX_AUX_ADDR_REG			0x108
#define XDPTX_AUXCMD_ADDRONLY_MASK		BIT(12)
#define XDPTX_AUXCMD_SHIFT			0x8
#define XDPTX_AUXCMD_BYTES_SHIFT		0x0
#define XDPTX_AUX_READ_BIT				0x1

#define XDPTX_CLKDIV_REG			0x10c
#define XDPTX_CLKDIV_MHZ			1000000
#define XDPTX_CLKDIV_AUXFILTER_SHIFT		0x8

#define XDPTX_INTR_SIGSTATE_REG			0x130
#define XDPTX_INTR_SIGHPDSTATE			BIT(0)
#define XDPTX_INTR_SIGREQSTATE			BIT(1)
#define XDPTX_INTR_SIGRPLYSTATE			BIT(2)
#define XDPTX_INTR_RPLYTIMEOUT			BIT(3)

#define XDPTX_AUXREPLY_DATA_REG			0x134
#define XDPTX_AUXREPLY_CODE_REG			0x138
#define XDPTX_AUXREPLYCODE_AUXACK_MASK		(0)
#define XDPTX_AUXREPLYCODE_I2CACK_MASK		(0)

#define XDPTX_AUXREPLY_DATACNT_REG		0x148
#define XDPTX_AUXREPLY_DATACNT_MASK		GENMASK(7, 0)
#define XDPTX_INTR_STATUS_REG			0x140
#define XDPTX_INTR_MASK_REG			0x144
#define XDPTX_INTR_HPDEVENT_MASK		BIT(1)
#define XDPTX_INTR_HPDPULSE_MASK		BIT(4)
#define XDPTX_INTR_CHBUFUNDFW_MASK		GENMASK(21, 16)
#define XDPTX_INTR_CHBUFOVFW_MASK		GENMASK(27, 22)
#define XDPTX_INTR_VBLANK_MASK			BIT(10)
#define XDPTX_INTR_EXTPKT_TXD_MASK		BIT(5)
#define XDPTX_HPD_DURATION_REG			0x150

/* Main stream attribute registers */
#define XDPTX_MAINSTRM_HTOTAL_REG		0x180
#define XDPTX_MAINSTRM_VTOTAL_REG		0x184
#define XDPTX_MAINSTRM_POL_REG			0x188
#define XDPTX_MAINSTRM_POLHSYNC_SHIFT		0x0
#define XDPTX_MAINSTRM_POLVSYNC_SHIFT		0x1
#define XDPTX_MAINSTRM_HSWIDTH_REG		0x18c
#define XDPTX_MAINSTRM_VSWIDTH_REG		0x190
#define XDPTX_MAINSTRM_HRES_REG			0x194
#define XDPTX_MAINSTRM_VRES_REG			0x198
#define XDPTX_MAINSTRM_HSTART_REG		0x19c
#define XDPTX_MAINSTRM_VSTART_REG		0x1a0
#define XDPTX_MAINSTRM_MISC0_REG		0x1a4
#define XDPTX_MAINSTRM_MISC0_MASK		BIT(0)
#define XDPTX_MAINSTRM_MISC0_EXT_VSYNC_MASK	BIT(12)
#define XDPTX_MAINSTRM_MISC1_REG		0x1a8
#define XDPTX_MAINSTRM_MISC1_TIMING_IGNORED_MASK BIT(6)

#define XDPTX_M_VID_REG				0x1ac
#define XDPTX_TRANSFER_UNITSIZE_REG		0x1b0
#define XDPTX_DEF_TRANSFER_UNITSIZE		0x40
#define XDPTX_N_VID_REG				0x1b4
#define XDPTX_USER_PIXELWIDTH_REG		0x1b8
#define XDPTX_USER_DATACNTPERLANE_REG		0x1bc
#define XDPTX_MINBYTES_PERTU_REG		0x1c4
#define XDPTX_FRACBYTES_PERTU_REG		0x1c8
#define XDPTX_INIT_WAIT_REG			0x1cc

/* PHY configuration and status registers */
#define XDPTX_PHYCONFIG_REG			0x200
#define XDPTX_PHYCONFIG_RESET_MASK		BIT(0)
#define XDPTX_PHYCONFIG_GTTXRESET_MASK		BIT(1)
#define XDPTX_PHYCONFIG_PMARESET_MASK		BIT(8)
#define XDPTX_PHYCONFIG_PCSRESET_MASK		BIT(9)
#define XDPTX_PHYCONFIG_ALLRESET_MASK	(XDPTX_PHYCONFIG_RESET_MASK | \
					 XDPTX_PHYCONFIG_GTTXRESET_MASK | \
					 XDPTX_PHYCONFIG_PMARESET_MASK | \
					 XDPTX_PHYCONFIG_PCSRESET_MASK)

#define XDPTX_PHYCLOCK_FBSETTING_REG		0x234
#define XDPTX_PHYCLOCK_FBSETTING162_MASK	0x1
#define XDPTX_PHYCLOCK_FBSETTING270_MASK	0x3
#define XDPTX_PHYCLOCK_FBSETTING810_MASK	0x5

#define XDPTX_VS_PE_LEVEL_MAXCOUNT		3
#define XDPTX_VS_LEVEL_MAXCOUNT			0x5

#define XDPTX_PHYSTATUS_REG			0x280
#define XDPTX_PHYSTATUS_FPGAPLLLOCK_MASK	BIT(6)
#define XDPTX_MAX_RATE(bw, lanecnt, bpp)	((bw) * (lanecnt) * 8 / (bpp))

#define XDPTX_MISC0_RGB_MASK			(0)
#define XDPTX_MISC0_YCRCB422_MASK		(5 << 1)
#define XDPTX_MISC0_YCRCB444_MASK		GENMASK(3, 2)
#define XDPTX_MISC0_FORMAT_MASK			GENMASK(3, 1)
#define XDPTX_MISC0_BPC6_MASK			(0 << 5)
#define XDPTX_MISC0_BPC8_MASK			BIT(5)
#define XDPTX_MISC0_BPC10_MASK			BIT(6)
#define XDPTX_MISC0_BPC12_MASK			GENMASK(6, 5)
#define XDPTX_MISC0_BPC16_MASK			BIT(7)
#define XDPTX_MISC0_BPC_MASK			GENMASK(7, 5)
#define XDPTX_MISC1_YONLY_MASK			BIT(7)

#define XDPTX_MAX_LANES				0x4
#define XDPTX_MAX_FREQ				3000000
#define XDPTX_SINK_PWR_CYCLES			3

#define XDPTX_REDUCED_BIT_RATE			162000
#define XDPTX_HIGH_BIT_RATE_1			270000
#define XDPTX_HIGH_BIT_RATE_2			540000
#define XDPTX_HIGH_BIT_RATE_3			810000

#define XDPTX_V1_2				0x12
#define XDPTX_V1_4				0x14

#define XDP_TRAIN_MAX_SWING_REACHED			BIT(2)
#define XDP_TRAIN_PRE_EMPHASIS_SHIFT			GENMASK(1, 0)
#define XDP_DPCD_TRAINING_LANEX_SET_MAX_PE_MASK		BIT(5)

#define XDPTX_PHYPRECURSOR_LANE0_REG			0x23c
#define XDPTX_PHYPOSTCURSOR_LANE0_REG			0x24c

/* Transceiver PHY reset and Differential voltage swing */
#define XDPTX_PHYVOLTAGE_DIFFLANE0_REG			0x220
#define XDPTX_VS_LEVEL_OFFSET				0x4

#define XDPTX_VTC_BASE					0x1000

/* VTC register offsets and bit masks */
#define XDPTX_VTC_CTL					0x000
#define XDPTX_VTC_CTL_MASK				GENMASK(18, 8)
#define XDPTX_VTC_CTL_GE				BIT(2)
#define XDPTX_VTC_CTL_RU				BIT(1)

#define XDPTX_VTC_GASIZE_F0				0x060
#define XDPTX_VTC_ACTIVE_SIZE_MASK			GENMASK(12, 0)

#define XDPTX_VTC_GFENC					0x068
#define XDPTX_VTC_GFENC_MASK				BIT(6)

#define XDPTX_VTC_GPOL					0x06c
#define XDPTX_VTC_GPOL_FIELD_ID_POL			BIT(6)
#define XDPTX_VTC_ACTIVE_CHROMA_POL			BIT(5)
#define	XDPTX_VTC_ACTIVE_VIDEO_POL			BIT(4)
#define XDPTX_VTC_HSYNC_POL				BIT(3)
#define XDPTX_VTC_VSYNC_POL				BIT(2)
#define XDPTX_VTC_HBLANK_POL				BIT(1)
#define XDPTX_VTC_VBLANK_POL				BIT(0)
#define XDPTX_VTC_GPOL_MASK		(XDPTX_VTC_VBLANK_POL |\
					 XDPTX_VTC_HBLANK_POL |\
					 XDPTX_VTC_VSYNC_POL |\
					 XDPTX_VTC_HSYNC_POL |\
					 XDPTX_VTC_ACTIVE_VIDEO_POL |\
					 XDPTX_VTC_ACTIVE_CHROMA_POL)

#define XDPTX_VTC_INT_GPOL_MASK		(XDPTX_VTC_GPOL_FIELD_ID_POL |\
					 XDPTX_VTC_ACTIVE_CHROMA_POL |\
					 XDPTX_VTC_ACTIVE_VIDEO_POL)

#define XDPTX_VTC_GHSIZE				0x070
#define XDPTX_VTC_GHSIZE_FRAME_HSIZE			GENMASK(12, 0)

#define XDPTX_VTC_GVSIZE				0x074
#define XDPTX_VTC_FIELD1_VSIZE_SHIFT			16
#define XDPTX_VTC_GVSIZE_FRAME_VSIZE			GENMASK(12, 0)

#define XDPTX_VTC_GHSYNC				0x078
#define XDPTX_VTC_GH1BPSTART_SHIFT			16
#define XDPTX_VTC_GHSYNC_END_MASK			GENMASK(28, 16)
#define XDPTX_VTC_GHSYNC_START_MASK			GENMASK(12, 0)

#define XDPTX_VTC_GVBHOFF				0x07c
#define XDPTX_VTC_F0VSYNC_HEND_SHIFT			16
#define XDPTX_VTC_F0VBLANK_HEND_MASK			GENMASK(28, 16)
#define XDPTX_VTC_F0VBLANK_HSTART_MASK			GENMASK(12, 0)

#define XDPTX_VTC_GVSYNC				0x080
#define XDPTX_VTC_F0_VSYNC_VEND_MASK			GENMASK(28, 16)
#define XDPTX_VTC_F0_VSYNC_VSTART_MASK			GENMASK(12, 0)

#define XDPTX_VTC_GVSHOFF				0x084
#define XDPTX_VTC_GVBHOFF_F1				0x088
#define XDPTX_VTC_GVSYNC_F1				0x08c
#define XDPTX_VTC_GVSHOFF_F1				0x090
#define XDPTX_VTC_GASIZE_F1				0x094
/*
 * This is sleep time in milliseconds before start training and it can be
 * modified as per the monitor
 */
#define XDPTX_POWERON_DELAY_MS				4
#define XDPTX_AUDIO_CTRL_REG				0x300
#define XDPTX_AUDIO_EN_MASK				BIT(0)
#define XDPTX_AUDIO_MUTE_MASK				BIT(16)
#define XDPTX_AUDIO_CHANNELS_REG			0x304
#define XDPTX_AUDIO_INFO_DATA_REG			0x308
#define XDPTX_AUDIO_MAUD_REG				0x328
#define XDPTX_AUDIO_NAUD_REG				0x32C
#define XDPTX_AUDIO_INFO_BUFF_STATUS			0x6A0
#define XDPTX_AUDIO_INFO_BUFF_FULL			BIT(0)
#define XDPTX_AUDIO_INFO_BUFF_OVERFLOW			BIT(1)

#define DP_INFOFRAME_FIFO_SIZE_WORDS	8
#define DP_INFOFRAME_FIFO_SIZE		(DP_INFOFRAME_FIFO_SIZE_WORDS * 4)
#define DP_INFOFRAME_HEADER_SIZE	4
#define DP_AUDIO_INFOFRAME_SIZE		10
/* infoframe SDP header byte. Please refer section 2.2.5.1.2 in DP1.4 spec */
#define NON_AUDIOIF_PKT_ID		0x00
#define NON_AUDIOIF_TYPE		0x07
#define NON_AUDIOIF_LDATA_BYTECOUNT	0x1d
#define NON_AUDIOIF_SDP_VERSION		0x4c
#define NON_AUDIOIF_DRM_TYPE		(0x80 + NON_AUDIOIF_TYPE)
/* DRM infoframe. Please refer section 6.9 in CTA-861G */
#define CTA_DRMIF_VERSION_NUMBER	0x01
#define CTA_DRMIF_LENGHT		0x1a

#define DP_INFOFRAME_SIZE(type)	\
	(DP_INFOFRAME_HEADER_SIZE + DP_ ## type ## _INFOFRAME_SIZE)
#define XDPTX_AUDIO_EXT_DATA(NUM)	(0x330 + 4 * ((NUM) - 1))
#define XDPTX_AUDIO_EXT_DATA_2ND_TO_9TH_WORD		8
#define XDPTX_VSC_SDP_PIXELENC_HEADER_MASK		0x13050700
#define XDPTX_VSC_SDP_DYNAMIC_RANGE_SHIFT		15
#define XDPTX_VSC_SDP_BPC_SHIFT				8
#define XDPTX_VSC_SDP_BPC_MASK				GENMASK(2, 0)
#define XDPTX_VSC_SDP_FMT_SHIFT				4

#define DP_LINK_BW_SET_MASK			GENMASK(4, 0)
#define DP_MAX_TRAINING_TRIES				5

#define XDPTX_DP_LANE_COUNT_1				0x01
#define XDPTX_DP_LANE_COUNT_2				0x02
#define XDPTX_DP_LANE_COUNT_4				0x04

#define XDPTX_DPCD_LANE02_CRDONE_MASK			0x01
#define XDPTX_DPCD_LANE13_CRDONE_MASK			0x10

#define XDPTX_LANE0_CRDONE_MASK				0x0
#define XDPTX_LANE1_CRDONE_MASK				0x1
#define XDPTX_LANE2_CRDONE_MASK				0x2
#define XDPTX_LANE3_CRDONE_MASK				0x3

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
/*
 * CEA speaker placement
 *
 *  FL  FLC   FC   FRC   FR   FRW
 *
 *                                  LFE
 *
 *  RL  RLC   RC   RRC   RR
 */
enum dp_codec_cea_spk_placement {
	FL  = BIT(0),	/* Front Left           */
	FC  = BIT(1),	/* Front Center         */
	FR  = BIT(2),	/* Front Right          */
	FLC = BIT(3),	/* Front Left Center    */
	FRC = BIT(4),	/* Front Right Center   */
	RL  = BIT(5),	/* Rear Left            */
	RC  = BIT(6),	/* Rear Center          */
	RR  = BIT(7),	/* Rear Right           */
	RLC = BIT(8),	/* Rear Left Center     */
	RRC = BIT(9),	/* Rear Right Center    */
	LFE = BIT(10),	/* Low Frequency Effect */
};

/*
 * cea Speaker allocation structure
 */
struct dp_codec_cea_spk_alloc {
	const int ca_id;
	unsigned int n_ch;
	unsigned long mask;
};

/**
 * struct xlnx_dptx_audio_data - Audio data structure
 * @buffer: Audio infoframe data buffer
 */
struct xlnx_dptx_audio_data {
	u32 buffer[DP_INFOFRAME_FIFO_SIZE_WORDS];
};

union xlnx_dp_iframe_header {
	u32 data;
	u8 byte[4];
};

union xlnx_dp_iframe_payload {
	u32 data[8];
	u8 byte[32];
};

struct xlnx_dp_infoframe {
	union xlnx_dp_iframe_header header;
	union xlnx_dp_iframe_payload payload;
};

/**
 * struct xlnx_dp_vscpkt: VSC extended packet structure
 * @payload: VSC packet payload bytes from DB0 to DB28
 * @header: VSC packet header
 * @bpc: Number of bits per color component
 * @fmt: The color format currenctly in use by the video stream
 * @dynamic_range: The dynamic range colorimetry currenctly in use by te video stream
 * @ycbcr_colorimetry: The ycbcr colorimetry currently in use by te video stream
 */
struct xlnx_dp_vscpkt {
	u32 payload[8];
	u32 header;
	u32 bpc;
	u8 fmt;
	u8 dynamic_range;
	u8 ycbcr_colorimetry;
};

/*
 * struct xlnx_dp_link_config - Common link config between source and sink
 * @max_rate: Miaximum link rate
 * @max_lanes: Maximum number of lanes
 */
struct xlnx_dp_link_config {
	int max_rate;
	u8 max_lanes;
	int link_rate;
	u8 lane_count;
	u8 cr_done_cnt;
	u8 cr_done_oldstate;
};

/**
 * struct xlnx_dp_tx_link_config - configuration information of the source
 * @vs_level: voltage swing level
 * @pe_level: pre emphasis level
 */
struct xlnx_dp_tx_link_config {
	u8 vs_level;
	u8 pe_level;
};

/**
 * struct xlnx_dp_mode - Configured mode of DisplayPort
 * @pclock: pixel clock frequency of current mode
 * @bw_code: code for bandwidth(link rate)
 * @lane_cnt: number of lanes
 */
struct xlnx_dp_mode {
	int pclock;
	u8 bw_code;
	u8 lane_cnt;
};

/**
 * struct xlnx_dp_config - Configuration of DisplayPort from DTS
 * @max_lanes: Maximum number of lanes
 * @max_link_rate: Maximum supported link rate
 * @misc0: Misc0 configuration
 * @bpp: Bits per pixel
 * @bpc: Bits per component
 * @num_colors: Number of color components
 * @ppc: Pixels per component
 * @fmt: Color format
 * @audio_enabled: flag to indicate audio is enabled in device tree
 * @versal_gt_present: flag to indicate versal-gt property in device tree
 */
struct xlnx_dp_config {
	u32 max_lanes;
	u32 max_link_rate;
	u8 misc0;
	u8 bpp;
	u8 bpc;
	u8 num_colors;
	u8 ppc;
	u8 fmt;
	bool audio_enabled;
	bool versal_gt_present;
};

enum xlnx_dp_train_state {
	XLNX_DP_TRAIN_CR = 0,
	XLNX_DP_TRAIN_CE = 1,
	XLNX_DP_ADJUST_LINKRATE = 2,
	XLNX_DP_ADJUST_LANECOUNT = 3,
	XLNX_DP_TRAIN_FAILURE = 4,
	XLNX_DP_TRAIN_SUCCESS = 5
};

/**
 * struct xlnx_dp - Xilinx DisplayPort core
 * @dev: device structure
 * @encoder: the drm encoder structure
 * @connector: the drm connector structure
 * @sync_prop: synchronous mode property
 * @bpc_prop: bpc mode property
 * @aux: aux channel
 * @config: IP core configuration from DTS
 * @tx_link_config: source configuration
 * @rx_config: sink configuration
 * @link_config: common link configuration between IP core and sink device
 * @drm: DRM core
 * @mode: current mode between IP core and sink device
 * @phy: PHY handles for DP lanes
 * @axi_lite_clk: axi lite clock
 * @tx_vid_clk: tx video clock
 * @reset_gpio: reset gpio
 * @hpd_work: hot plug detection worker
 * @hpd_pulse_work: hot plug pulse detection worker
 * @tx_audio_data: audio data
 * @infoframe : IP infoframe data
 * @vscpkt: VSC extended packet data
 * @phy_opts: Opaque generic phy configuration
 * @status: connection status
 * @dp_base: Base address of DisplayPort Tx subsystem
 * @dpms: current dpms state
 * @dpcd: DP configuration data from currently connected sink device
 * @train_set: set of training data
 * @num_lanes: number of enabled phy lanes
 * @enabled: flag to indicate if the device is enabled
 * @audio_init: flag to indicate audio is initialized
 * @have_edid: flag to indicate if edid is available
 * @colorimetry_through_vsc: colorimetry information through vsc packets
 *
 */
struct xlnx_dp {
	struct device *dev;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_property *sync_prop;
	struct drm_property *bpc_prop;
	struct drm_dp_aux aux;
	struct xlnx_dp_config config;
	struct xlnx_dp_tx_link_config tx_link_config;
	struct xlnx_dp_link_config link_config;
	struct drm_device *drm;
	struct xlnx_dp_mode mode;
	struct phy *phy[XDPTX_MAX_LANES];
	struct clk *axi_lite_clk;
	struct clk *tx_vid_clk;
	struct gpio_desc *reset_gpio;
	struct delayed_work hpd_work;
	struct delayed_work hpd_pulse_work;
	struct xlnx_dptx_audio_data *tx_audio_data;
	struct xlnx_dp_infoframe infoframe;
	struct xlnx_dp_vscpkt vscpkt;
	union phy_configure_opts phy_opts;
	enum drm_connector_status status;
	void __iomem *dp_base;
	int dpms;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	u8 train_set[XDPTX_MAX_LANES];
	u8 num_lanes;
	unsigned int enabled : 1;
	bool audio_init;
	bool have_edid;
	unsigned int colorimetry_through_vsc : 1;
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

static inline struct xlnx_dp *connector_to_dp(struct drm_connector *connector)
{
	return container_of(connector, struct xlnx_dp, connector);
}

static void xlnx_dp_write(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

static u32 xlnx_dp_read(void __iomem *base, int offset)
{
	return readl(base + offset);
}

static void xlnx_dp_set(void __iomem *base, int offset, u32 set)
{
	xlnx_dp_write(base, offset, xlnx_dp_read(base, offset) | set);
}

static void xlnx_dp_clr(void __iomem *base, int offset, u32 clr)
{
	xlnx_dp_write(base, offset, xlnx_dp_read(base, offset) & ~clr);
}

static void xlnx_dp_vtc_set_timing(struct xlnx_dp *dp,
				   struct drm_display_mode *mode)
{
	u32 reg;
	u32 htotal, hactive, hsync_start, hbackporch_start;
	u32 vtotal, vactive, vsync_start, vbackporch_start;
	u32 hsync_len, hfront_porch, hback_porch;
	u32 vsync_len, vfront_porch, vback_porch;

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
	hactive = mode->hdisplay / dp->config.ppc;
	hfront_porch = (mode->hsync_start - mode->hdisplay) / dp->config.ppc;
	hback_porch = (mode->htotal - mode->hsync_end) / dp->config.ppc;
	hsync_len = (mode->hsync_end - mode->hsync_start) / dp->config.ppc;
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
	xlnx_dp_write(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GHSIZE, reg);

	reg = vtotal & XDPTX_VTC_GVSIZE_FRAME_VSIZE;
	reg |= reg << XDPTX_VTC_FIELD1_VSIZE_SHIFT;
	xlnx_dp_write(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GVSIZE, reg);

	reg = hactive & XDPTX_VTC_ACTIVE_SIZE_MASK;
	reg |= (vactive & XDPTX_VTC_ACTIVE_SIZE_MASK) <<
		XDPTX_VTC_FIELD1_VSIZE_SHIFT;
	xlnx_dp_write(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GASIZE_F0, reg);

	reg = hsync_start & XDPTX_VTC_GHSYNC_START_MASK;
	reg |= (hbackporch_start << XDPTX_VTC_GH1BPSTART_SHIFT) &
		XDPTX_VTC_GHSYNC_END_MASK;
	xlnx_dp_write(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GHSYNC, reg);

	reg = vsync_start & XDPTX_VTC_F0_VSYNC_VSTART_MASK;
	reg |= (vbackporch_start << XDPTX_VTC_FIELD1_VSIZE_SHIFT) &
		XDPTX_VTC_F0_VSYNC_VEND_MASK;
	xlnx_dp_write(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GVSYNC, reg);
	xlnx_dp_clr(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GFENC,
		    XDPTX_VTC_GFENC_MASK);

	/* Calculate and update Generator VBlank Hori field 0 */
	reg = hactive & XDPTX_VTC_F0VBLANK_HSTART_MASK;
	reg |= (hactive << XDPTX_VTC_F0VSYNC_HEND_SHIFT) &
		XDPTX_VTC_F0VBLANK_HEND_MASK;
	xlnx_dp_write(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GVBHOFF, reg);

	/* Calculate and update Generator VSync Hori field 0 */
	reg = hsync_start & XDPTX_VTC_F0VBLANK_HSTART_MASK;
	reg |= (hsync_start << XDPTX_VTC_F0VSYNC_HEND_SHIFT) &
		XDPTX_VTC_F0VBLANK_HEND_MASK;
	xlnx_dp_write(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GVSHOFF, reg);

	/* sets all polarities as active high */
	xlnx_dp_write(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_GPOL,
		      XDPTX_VTC_GPOL_MASK);

	/* configure timing source */
	xlnx_dp_set(dp->dp_base,
		    XDPTX_VTC_BASE + XDPTX_VTC_CTL, XDPTX_VTC_CTL_MASK);
	xlnx_dp_set(dp->dp_base,
		    XDPTX_VTC_BASE + XDPTX_VTC_CTL, XDPTX_VTC_CTL_RU);
}

/**
 * xlnx_dp_update_bpp - Update the current bpp config
 * @dp: DisplayPort IP core structure
 *
 * Update the current bpp based on the color format: bpc & num_colors.
 * Any function that changes bpc or num_colors should call this
 * to keep the bpp value in sync.
 */
static void xlnx_dp_update_bpp(struct xlnx_dp *dp)
{
	struct xlnx_dp_config *config = &dp->config;

	config->bpp = dp->config.bpc * dp->config.num_colors;
}

/**
 * xlnx_dp_set_color - Set the color format
 * @dp: DisplayPort IP core structure
 * @drm_fourcc: Color string, from xlnx_disp_color_enum
 *
 * This function updates misc register values based on color string.
 */
static void xlnx_dp_set_color(struct xlnx_dp *dp, u32 drm_fourcc)
{
	struct xlnx_dp_config *config = &dp->config;

	config->misc0 &= ~XDPTX_MISC0_FORMAT_MASK;

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
		config->misc0 |= XDPTX_MISC0_RGB_MASK;
		config->num_colors = 3;
		config->fmt = 0x0;
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
		config->misc0 |= XDPTX_MISC0_YCRCB444_MASK;
		config->num_colors = 3;
		config->fmt = 0x1;
		break;
	case DRM_FORMAT_YUYV:
		fallthrough;
	case DRM_FORMAT_UYVY:
		fallthrough;
	case DRM_FORMAT_NV16:
		fallthrough;
	case DRM_FORMAT_XV20:
		config->misc0 |= XDPTX_MISC0_YCRCB422_MASK;
		config->num_colors = 2;
		config->fmt = 0x2;
		break;
	default:
		dev_dbg(dp->dev, "Warning: Unknown drm_fourcc format :%d\n",
			drm_fourcc);
		config->misc0 |= XDPTX_MISC0_RGB_MASK;
	}
	xlnx_dp_update_bpp(dp);
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

	ready = (1 << XDPTX_MAX_LANES) - 1;
	ready |= XDPTX_PHYSTATUS_FPGAPLLLOCK_MASK;

	/* Wait for 100ms. This should be enough time for PHY to be ready */
	for (i = 0; ; i++) {
		reg = xlnx_dp_read(dp->dp_base, XDPTX_PHYSTATUS_REG);
		if ((reg & ready) == ready)
			return 0;
		if (i == 100) {
			dev_err(dp->dev, "PHY isn't ready\n");
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
	default:
		break;
	}

	return 0;
}

static int xlnx_dp_set_linkrate(struct xlnx_dp *dp, u8 bw_code)
{
	struct phy_configure_opts_dp *phy_cfg = &dp->phy_opts.dp;
	int ret;
	u32 reg, lrate_val = 0, val;
	u8 lane_count = dp->mode.lane_cnt;

	if (!xlnx_dp_txconnected(dp)) {
		dev_dbg(dp->dev, "display is not connected");
		return connector_status_disconnected;
	}

	dp->mode.bw_code = bw_code;

	switch (bw_code) {
	case DP_LINK_BW_1_62:
		reg = XDPTX_PHYCLOCK_FBSETTING162_MASK;
		phy_cfg->link_rate = XDPTX_REDUCED_BIT_RATE / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_162G;
		break;
	case DP_LINK_BW_2_7:
		reg = XDPTX_PHYCLOCK_FBSETTING270_MASK;
		phy_cfg->link_rate = XDPTX_HIGH_BIT_RATE_1 / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_270G;
		break;
	case DP_LINK_BW_5_4:
		reg = XDPTX_PHYCLOCK_FBSETTING810_MASK;
		phy_cfg->link_rate = XDPTX_HIGH_BIT_RATE_2 / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_540G;
		break;
	case DP_LINK_BW_8_1:
		reg = XDPTX_PHYCLOCK_FBSETTING810_MASK;
		phy_cfg->link_rate = XDPTX_HIGH_BIT_RATE_3 / 100;
		if (dp->config.versal_gt_present)
			lrate_val = XDPTX_GTCTL_LINE_RATE_810G;
		break;
	default:
		reg = XDPTX_PHYCLOCK_FBSETTING810_MASK;
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
	xlnx_dp_write(dp->dp_base, XDPTX_PHYCLOCK_FBSETTING_REG, reg);
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
		return connector_status_disconnected;
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
		 * Post Link Training ; An upstream device with a DPTX sets
		 * this bit to 1 to grant the POST_LT_ADJ_REQ sequence by the
		 * downstream DPRX if the downstream DPRX supports
		 * POST_LT_ADJ_REQ
		 */
		data |= 0x20;
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
static int xlnx_dp_aux_cmd_submit(struct xlnx_dp *dp, u32 cmd, u16 addr,
				  u8 *buf, u8 bytes, u8 *reply)
{
	bool is_read = (cmd & XDPTX_AUX_READ_BIT) ? true : false;
	void __iomem *dp_base = dp->dp_base;
	u32 reg, i;

	reg = xlnx_dp_read(dp_base, XDPTX_INTR_SIGSTATE_REG);
	if (reg & XDPTX_INTR_SIGREQSTATE)
		return -EBUSY;

	xlnx_dp_write(dp_base, XDPTX_AUX_ADDR_REG, addr);
	if (!is_read) {
		for (i = 0; i < bytes; i++) {
			xlnx_dp_write(dp_base, XDPTX_AUX_WRITEFIFO_REG,
				      buf[i]);
		}
	}

	reg = cmd << XDPTX_AUXCMD_SHIFT;
	if (!bytes)
		reg |= XDPTX_AUXCMD_ADDRONLY_MASK;
	else
		reg |= (bytes - 1) << XDPTX_AUXCMD_BYTES_SHIFT;
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
		if ((reg & XDPTX_AUXREPLY_DATACNT_MASK) != bytes)
			return -EIO;

		for (i = 0; i < bytes; i++) {
			buf[i] = xlnx_dp_read(dp_base,
					      XDPTX_AUXREPLY_DATA_REG);
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

		if (dp->status == connector_status_disconnected) {
			dev_info(dp->dev, "no connected aux device\n");
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
	struct xlnx_dp_config *config = &dp->config;

	if (!dp->colorimetry_through_vsc) {
		xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_MISC0_REG,
			      config->misc0);
		xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_MISC1_REG, 0x0);
	} else {
		xlnx_dp_set(dp->dp_base, XDPTX_MAINSTRM_MISC1_REG,
			    XDPTX_MAINSTRM_MISC1_TIMING_IGNORED_MASK);
	}
}

/**
 * xlnx_dp_set_sync_mode - Set the sync mode bit in the software misc state
 * @dp: DisplayPort IP core structure
 * @mode: flag if the sync mode should be on or off
 *
 * Set the bit in software misc state. To apply to hardware,
 * xlnx_dp_update_misc() should be called.
 */
static void xlnx_dp_set_sync_mode(struct xlnx_dp *dp, bool mode)
{
	struct xlnx_dp_config *config = &dp->config;

	if (mode)
		config->misc0 |= XDPTX_MAINSTRM_MISC0_MASK;
	else
		config->misc0 &= ~XDPTX_MAINSTRM_MISC0_MASK;
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
	struct xlnx_dp_config *config = &dp->config;
	struct xlnx_dp_vscpkt *vscpkt = &dp->vscpkt;
	int i;
	u32 payload_data = 0, bpc;

	vscpkt->header = XDPTX_VSC_SDP_PIXELENC_HEADER_MASK;
	payload_data |= config->fmt << XDPTX_VSC_SDP_FMT_SHIFT;

	switch (config->bpc) {
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
			config->bpc);
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
 * xlnx_dp_set_bpc - Set bpc value in software misc state
 * @dp: DisplayPort IP core structure
 * @bpc: bits per component
 *
 * Return: 0 on success, or the fallback bpc value
 */
static u32 xlnx_dp_set_bpc(struct xlnx_dp *dp, u8 bpc)
{
	struct xlnx_dp_config *config = &dp->config;
	unsigned int ret = 0;

	if (dp->connector.display_info.bpc &&
	    dp->connector.display_info.bpc != bpc) {
		dev_err(dp->dev, "requested bpc (%u) != display info (%u)\n",
			bpc, dp->connector.display_info.bpc);
		bpc = dp->connector.display_info.bpc;
	}

	config->misc0 &= ~XDPTX_MISC0_BPC_MASK;
	switch (bpc) {
	case 6:
		config->misc0 |= XDPTX_MISC0_BPC6_MASK;
		break;
	case 8:
		config->misc0 |= XDPTX_MISC0_BPC8_MASK;
		break;
	case 10:
		config->misc0 |= XDPTX_MISC0_BPC10_MASK;
		break;
	case 12:
		config->misc0 |= XDPTX_MISC0_BPC12_MASK;
		break;
	case 16:
		config->misc0 |= XDPTX_MISC0_BPC16_MASK;
		break;
	default:
		dev_err(dp->dev, "Not supported bpc (%u). fall back to 8bpc\n",
			bpc);
		config->misc0 |= XDPTX_MISC0_BPC8_MASK;
		ret = 8;
	}
	config->bpc = bpc;
	xlnx_dp_update_bpp(dp);

	return ret;
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
	struct xlnx_dp_config *config = &dp->config;
	u32 tu = XDPTX_DEF_TRANSFER_UNITSIZE, temp;
	u32 bw, vid_kbytes, avg_bytes_per_tu, init_wait, min_bytes_per_tu;

	/* Use the max transfer unit size (default) */
	xlnx_dp_write(dp->dp_base, XDPTX_TRANSFER_UNITSIZE_REG, tu);

	vid_kbytes = (mode->clock / 1000) * (dp->config.bpp / 8);
	bw = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	avg_bytes_per_tu = vid_kbytes * tu / (dp->mode.lane_cnt * bw);
	min_bytes_per_tu = avg_bytes_per_tu / 1000;
	xlnx_dp_write(dp->dp_base, XDPTX_MINBYTES_PERTU_REG,
		      min_bytes_per_tu);

	temp = (avg_bytes_per_tu % 1000) * 1024 / 1000;
	xlnx_dp_write(dp->dp_base, XDPTX_FRACBYTES_PERTU_REG, temp);

	init_wait = tu - min_bytes_per_tu;

	/* Configure the initial wait cycle based on transfer unit size */
	if (min_bytes_per_tu <= 4)
		init_wait = tu;
	else if ((config->misc0 & XDPTX_MISC0_YCRCB422_MASK) ==
		XDPTX_MISC0_YCRCB422_MASK)
		init_wait = init_wait / 2;

	xlnx_dp_write(dp->dp_base, XDPTX_INIT_WAIT_REG, init_wait);
}

/**
 * xlnx_dp_encoder_mode_set_stream - Configure the main stream
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * Configure the main stream based on the requested mode @mode. Calculation is
 * based on IP core specification.
 */
static void xlnx_dp_encoder_mode_set_stream(struct xlnx_dp *dp,
					    struct drm_display_mode *mode)
{
	void __iomem *dp_base = dp->dp_base;
	u32 reg, wpl, ppc;
	u8 lane_cnt = dp->mode.lane_cnt;

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_HTOTAL_REG, mode->htotal);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_VTOTAL_REG, mode->vtotal);

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_POL_REG,
		      (!!(mode->flags & DRM_MODE_FLAG_PVSYNC) <<
		      XDPTX_MAINSTRM_POLVSYNC_SHIFT) |
		      (!!(mode->flags & DRM_MODE_FLAG_PHSYNC) <<
		      XDPTX_MAINSTRM_POLHSYNC_SHIFT));

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_HSWIDTH_REG,
		      mode->hsync_end - mode->hsync_start);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_VSWIDTH_REG,
		      mode->vsync_end - mode->vsync_start);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_HRES_REG, mode->hdisplay);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_VRES_REG, mode->vdisplay);

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_HSTART_REG,
		      mode->htotal - mode->hsync_start);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_VSTART_REG,
		      mode->vtotal - mode->vsync_start);
	xlnx_dp_update_misc(dp);

	if (mode->clock > 530000)
		ppc = 4;
	else if (mode->clock > 270000)
		ppc = 2;
	else
		ppc = 1;

	xlnx_dp_write(dp_base, XDPTX_USER_PIXELWIDTH_REG, ppc);
	dp->config.ppc = ppc;

	xlnx_dp_write(dp_base, XDPTX_M_VID_REG, mode->clock);
	reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	xlnx_dp_write(dp_base, XDPTX_N_VID_REG, reg);

	/* In synchronous mode, set the dividers */
	if (dp->config.misc0 & XDPTX_MAINSTRM_MISC0_MASK) {
		reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
		xlnx_dp_write(dp_base, XDPTX_N_VID_REG, reg);
		xlnx_dp_write(dp_base, XDPTX_M_VID_REG, mode->clock);
	}

	wpl = (mode->hdisplay * dp->config.bpp + 15) / 16;
	reg = wpl + wpl % lane_cnt - lane_cnt;
	xlnx_dp_write(dp_base, XDPTX_USER_DATACNTPERLANE_REG, reg);
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
	u8 value;

	/* sink Power cycle */
	for (i = 0; i < XDPTX_SINK_PWR_CYCLES; i++) {
		ret = drm_dp_dpcd_readb(&dp->aux, DP_SET_POWER, &value);

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
 */
static void xlnx_dp_start(struct xlnx_dp *dp)
{
	struct xlnx_dp_mode *mode = &dp->mode;
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
		return;
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
		return;
	}

	ret = xlnx_dp_set_lanecount(dp, mode->lane_cnt);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return;
	}

	/* Disable MST mode in both the RX and TX */
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_MSTM_CTRL, 0);
	if (ret < 0) {
		dev_dbg(dp->dev, "DPCD write failed");
		return;
	}
	xlnx_dp_write(dp->dp_base, XDPTX_MST_CONFIG, 0x0);

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
		return;
	}
	/* set channel encoding */
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				 DP_SET_ANSI_8B10B);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set ANSI 8B/10B encoding\n");
		return;
	}
	/* Reset PHY */
	xlnx_dp_phy_reset(dp, XDPTX_PHYCONFIG_RESET_MASK);

	/* Wait for PHY ready */
	ret = xlnx_dp_phy_ready(dp);
	if (ret < 0)
		return;

	memset(dp->train_set, 0, XDPTX_MAX_LANES);

	ret = xlnx_dp_run_training(dp);
	if (ret < 0) {
		dev_err(dp->dev, "DP Link Training Failed\n");
		return;
	}

	ret = xlnx_dp_post_training(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed post trining settings\n");
		return;
	}

	/* re-enable main link after training if required */
	if (val)
		xlnx_dp_mainlink_en(dp, 0x1);
	/* Enable HDP interrupts after link training */
	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, intr_mask);

	dev_dbg(dp->dev, "Training done:");

	/* reset the transmitter */
	xlnx_dp_write(dp->dp_base, XDPTX_SOFT_RST,
		      XDPTX_SOFT_RST_VIDEO_STREAM_ALL_MASK |
		      XDPTX_SOFT_RST_HDCP_MASK);
	xlnx_dp_write(dp->dp_base, XDPTX_SOFT_RST, 0x0);

	/* Enable VTC and MainStream */
	xlnx_dp_set(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_CTL,
		    XDPTX_VTC_CTL_GE);
	xlnx_dp_mainlink_en(dp, 0x1);

	ret = xlnx_dp_check_link_status(dp);
	if (ret < 0) {
		dev_err(dp->dev, "Link is DOWN after main link enabled!\n");
		return;
	}
	if (dp->colorimetry_through_vsc) {
		/* program the VSC extended packet */
		xlnx_dp_vsc_pkt_handler(dp);
		/* This ensures that VSC pkt is sent every frame */
		xlnx_dp_set(dp->dp_base, XDPTX_MAINSTRM_MISC0_REG,
			    XDPTX_MAINSTRM_MISC0_EXT_VSYNC_MASK);
	}
	/* enable audio */
	xlnx_dp_set(dp->dp_base, XDPTX_AUDIO_CTRL_REG, 0x1);
	/* Enabling TX interrupts */
	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, 0);
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
	xlnx_dp_clr(dp->dp_base, XDPTX_VTC_BASE + XDPTX_VTC_CTL,
		    XDPTX_VTC_CTL_GE);
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

/*
 * DRM connector functions
 */
static enum drm_connector_status
xlnx_dp_connector_detect(struct drm_connector *connector, bool force)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	struct xlnx_dp_link_config *link_config = &dp->link_config;
	struct xlnx_dp_mode *mode = &dp->mode;
	int ret;
	u8 dpcd_ext[DP_RECEIVER_CAP_SIZE];
	u8 max_link_rate, ext_cap_rd = 0, data;

	if (!xlnx_dp_txconnected(dp)) {
		dev_dbg(dp->dev, "Display is not connected");
		goto disconnected;
	}

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

static int xlnx_dp_connector_get_modes(struct drm_connector *connector)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	struct edid *edid;
	int ret;

	edid = drm_get_edid(connector, &dp->aux.ddc);
	if (!edid) {
		drm_connector_update_edid_property(connector, NULL);
		dp->have_edid = false;
		return 0;
	}

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	dp->have_edid = true;
	kfree(edid);

	return ret;
}

static struct drm_encoder *
xlnx_dp_connector_best_encoder(struct drm_connector *connector)
{
	struct xlnx_dp *dp = connector_to_dp(connector);

	return &dp->encoder;
}

static int xlnx_dp_connector_mode_valid(struct drm_connector *connector,
					struct drm_display_mode *mode)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	u8 max_lanes = dp->link_config.max_lanes;
	u8 bpp = dp->config.bpp;
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

static void xlnx_dp_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int
xlnx_dp_connector_atomic_set_property(struct drm_connector *connector,
				      struct drm_connector_state *state,
				      struct drm_property *property,
				      uint64_t val)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	unsigned int bpc;

	if (property == dp->sync_prop) {
		xlnx_dp_set_sync_mode(dp, val);
	} else if (property == dp->bpc_prop) {
		bpc = xlnx_dp_set_bpc(dp, val);
		if (bpc) {
			drm_object_property_set_value(&connector->base,
						      property, bpc);
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int
xlnx_dp_connector_atomic_get_property(struct drm_connector *connector,
				      const struct drm_connector_state *state,
					struct drm_property *property,
					uint64_t *val)
{
	struct xlnx_dp *dp = connector_to_dp(connector);

	if (property == dp->sync_prop)
		*val = (dp->config.misc0 & XDPTX_MAINSTRM_MISC0_MASK);
	else if (property == dp->bpc_prop)
		*val = dp->config.bpc;
	else
		return -EINVAL;

	return 0;
}

static const struct drm_connector_funcs xlnx_dp_connector_funcs = {
	.detect			= xlnx_dp_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= xlnx_dp_connector_destroy,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_set_property	= xlnx_dp_connector_atomic_set_property,
	.atomic_get_property	= xlnx_dp_connector_atomic_get_property,
};

static struct drm_connector_helper_funcs xlnx_dp_connector_helper_funcs = {
	.get_modes	= xlnx_dp_connector_get_modes,
	.best_encoder	= xlnx_dp_connector_best_encoder,
	.mode_valid	= xlnx_dp_connector_mode_valid,
};

static int xlnx_dp_get_eld(struct device *dev, u8 *buf, size_t len)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);
	size_t size;

	if (!dp->have_edid)
		return -EIO;

	size = drm_eld_size(dp->connector.eld);
	if (!size)
		return -EINVAL;

	if (len < size)
		size = len;
	memcpy(buf, dp->connector.eld, size);

	return 0;
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

static void xlnx_dp_encoder_enable(struct drm_encoder *encoder)
{
	struct xlnx_dp *dp = encoder_to_dp(encoder);

	pm_runtime_get_sync(dp->dev);

	if (!dp->enabled) {
		dp->enabled = true;
		xlnx_dp_start(dp);
	}
}

static void xlnx_dp_encoder_disable(struct drm_encoder *encoder)
{
	struct xlnx_dp *dp = encoder_to_dp(encoder);

	if (dp->enabled) {
		dp->enabled = false;
		cancel_delayed_work(&dp->hpd_work);
		cancel_delayed_work(&dp->hpd_pulse_work);
		xlnx_dp_stop(dp);
	}
	pm_runtime_put_sync(dp->dev);
}

static void
xlnx_dp_encoder_atomic_mode_set(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *connector_state)
{
	struct xlnx_dp *dp = encoder_to_dp(encoder);
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	int rate, max_rate = dp->link_config.max_rate;
	u32 clock, drm_fourcc;
	u8 max_lanes = dp->link_config.max_lanes;
	u8 bpp = dp->config.bpp;

	/*
	 * This assumes that there is no conversion  between framebuffer
	 * and DP Tx
	 */
	drm_fourcc = encoder->crtc->primary->state->fb->format->format;

	xlnx_dp_set_color(dp, drm_fourcc);

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
	clk_set_rate(dp->tx_vid_clk, clock / dp->config.ppc);

	xlnx_dp_vtc_set_timing(dp, adjusted_mode);
	/* prepare a vsc packet */
	xlnx_dp_prepare_vsc(dp);
}

static const struct drm_encoder_funcs xlnx_dp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_encoder_helper_funcs xlnx_dp_encoder_helper_funcs = {
	.enable			= xlnx_dp_encoder_enable,
	.disable		= xlnx_dp_encoder_disable,
	.atomic_mode_set	= xlnx_dp_encoder_atomic_mode_set,
};

static void xlnx_dp_hpd_work_func(struct work_struct *work)
{
	struct xlnx_dp *dp;

	dp = container_of(work, struct xlnx_dp, hpd_work.work);

	if (dp->drm)
		drm_helper_hpd_irq_event(dp->drm);
}

static struct drm_prop_enum_list xlnx_dp_bpc_enum[] = {
	{ 6, "6BPC" },
	{ 8, "8BPC" },
	{ 10, "10BPC" },
	{ 12, "12BPC" },
};

static int xlnx_dp_bind(struct device *dev, struct device *master, void *data)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dp->encoder;
	struct drm_connector *connector = &dp->connector;
	struct drm_device *drm = data;
	unsigned int ret;

	encoder->possible_crtcs = 1;
	drm_encoder_init(drm, encoder, &xlnx_dp_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(encoder, &xlnx_dp_encoder_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_HPD;
	ret = drm_connector_init(encoder->dev, connector,
				 &xlnx_dp_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		dev_err(dp->dev, "failed to initialize the drm connector");
		goto error_encoder;
	}

	drm_connector_helper_add(connector, &xlnx_dp_connector_helper_funcs);
	drm_connector_register(connector);
	drm_connector_attach_encoder(connector, encoder);
	connector->dpms = DRM_MODE_DPMS_OFF;

	dp->drm = drm;
	dp->sync_prop = drm_property_create_bool(drm, 0, "sync");
	dp->bpc_prop = drm_property_create_enum(drm, 0, "bpc",
						xlnx_dp_bpc_enum,
						ARRAY_SIZE(xlnx_dp_bpc_enum));
	dp->config.misc0 &= ~XDPTX_MAINSTRM_MISC0_MASK;
	drm_object_attach_property(&connector->base, dp->sync_prop, false);
	ret = xlnx_dp_set_bpc(dp, 8);
	drm_object_attach_property(&connector->base, dp->bpc_prop,
				   ret ? ret : 8);
	xlnx_dp_update_bpp(dp);

	drm_object_attach_property(&connector->base,
				   connector->dev->mode_config.gen_hdr_output_metadata_property, 0);
	/* This enables interrupts, so should be called after DRM init */
	ret = xlnx_dp_init_aux(dp);
	if (ret) {
		dev_err(dp->dev, "failed to initialize DP aux");
		goto error_prop;
	}
	INIT_DELAYED_WORK(&dp->hpd_work, xlnx_dp_hpd_work_func);
	INIT_DELAYED_WORK(&dp->hpd_pulse_work, xlnx_dp_hpd_pulse_work_func);

	return 0;

error_prop:
	drm_property_destroy(dp->drm, dp->bpc_prop);
	drm_property_destroy(dp->drm, dp->sync_prop);
	xlnx_dp_connector_destroy(&dp->connector);
error_encoder:
	drm_encoder_cleanup(&dp->encoder);

	return ret;
}

static void xlnx_dp_unbind(struct device *dev,
			   struct device *master, void *data)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&dp->hpd_work);
	cancel_delayed_work_sync(&dp->hpd_pulse_work);
	xlnx_dp_exit_aux(dp);
	drm_property_destroy(dp->drm, dp->bpc_prop);
	drm_property_destroy(dp->drm, dp->sync_prop);
	xlnx_dp_connector_destroy(&dp->connector);
	drm_encoder_cleanup(&dp->encoder);
}

static void xlnx_dp_hpd_pulse_work_func(struct work_struct *work)
{
	struct xlnx_dp *dp;
	int ret;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 bw_set, lane_set;

	dp = container_of(work, struct xlnx_dp, hpd_pulse_work.work);

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
	struct drm_connector_state *state = dp->connector.state;
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

		if (state->gen_hdr_output_metadata) {
			drm_hdmi_infoframe_set_gen_hdr_metadata(&frame, state);
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
		 * is longer than 500 microseconds.
		 */
		hpdduration = xlnx_dp_read(dp->dp_base, XDPTX_HPD_DURATION_REG);
		if (hpdduration >= 500)
			xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, 0x10);
	}

	if (intrstatus & XDPTX_INTR_CHBUFUNDFW_MASK)
		dev_dbg_ratelimited(dp->dev, "underflow interrupt\n");
	if (intrstatus & XDPTX_INTR_CHBUFOVFW_MASK)
		dev_dbg_ratelimited(dp->dev, "overflow interrupt\n");

	if (intrstatus & XDPTX_INTR_HPDEVENT_MASK)
		schedule_delayed_work(&dp->hpd_work, 0);
	if (intrstatus & XDPTX_INTR_HPDPULSE_MASK)
		schedule_delayed_work(&dp->hpd_pulse_work, 0);
	if (intrstatus & XDPTX_INTR_EXTPKT_TXD_MASK)
		xlnx_dp_vsc_pkt_handler(dp);
	if (intrstatus & XDPTX_INTR_VBLANK_MASK)
		xlnx_dp_vsync_handler(dp);

	return IRQ_HANDLED;
}

static const struct component_ops xlnx_dp_component_ops = {
	.bind	= xlnx_dp_bind,
	.unbind	= xlnx_dp_unbind,
};

static int xlnx_dp_parse_of(struct xlnx_dp *dp)
{
	struct xlnx_dp_config *config = &dp->config;
	struct device_node *node = dp->dev->of_node;
	u32 bpc;
	int ret;

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
		config->misc0 |= XDPTX_MISC0_BPC6_MASK;
		break;
	case 8:
		config->misc0 |= XDPTX_MISC0_BPC8_MASK;
		break;
	case 10:
		config->misc0 |= XDPTX_MISC0_BPC10_MASK;
		break;
	case 12:
		config->misc0 |= XDPTX_MISC0_BPC12_MASK;
		break;
	case 16:
		config->misc0 |= XDPTX_MISC0_BPC16_MASK;
		break;
	default:
		dev_err(dp->dev, "Not supported color depth in DT\n");
		return -EINVAL;
	}

	config->audio_enabled =
		of_property_read_bool(node, "xlnx,audio-enable");

	config->versal_gt_present =
		of_property_read_bool(node, "xlnx,versal-gt");

	return 0;
}

static int xlnx_dp_probe(struct platform_device *pdev)
{
	struct device_node *pnode = pdev->dev.of_node;
	struct device_node *fnode;
	struct platform_device *iface_pdev;
	struct xlnx_dp *dp;
	struct resource *res;
	void *ptr;
	unsigned int i;
	int irq, ret;

	dp = devm_kzalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->tx_audio_data =
		devm_kzalloc(&pdev->dev, sizeof(struct xlnx_dptx_audio_data),
			     GFP_KERNEL);
	if (!dp->tx_audio_data)
		return -ENOMEM;

	dp->dpms = DRM_MODE_DPMS_OFF;
	dp->status = connector_status_disconnected;
	dp->dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dp_base");
	dp->dp_base = devm_ioremap_resource(dp->dev, res);
	if (IS_ERR(dp->dp_base)) {
		dev_err(&pdev->dev, "couldn't map DisplayPort registers\n");
		return -ENODEV;
	}

	ret = xlnx_dp_parse_of(dp);
	if (ret < 0)
		return ret;

	if (dp->config.versal_gt_present) {
		dp->phy[0] = devm_phy_get(dp->dev, "dp-gtquad");
		if (IS_ERR(dp->phy[0]))
			return dev_err_probe(dp->dev, ret, "failed to get phy\n");

		ret = phy_init(dp->phy[0]);
		if (ret)
			goto error_phy;

		fnode = of_parse_phandle(pnode, "xlnx,xilinx-vfmc", 0);
		if (!fnode) {
			dev_err(&pdev->dev, "platform node not found\n");
			of_node_put(fnode);
		} else {
			iface_pdev = of_find_device_by_node(fnode);
			if (!iface_pdev) {
				of_node_put(pnode);
				return -ENODEV;
			}

			ptr = dev_get_drvdata(&iface_pdev->dev);
			if (!ptr) {
				dev_info(&pdev->dev,
					 "platform device not found -EPROBE_DEFER\n");
				of_node_put(fnode);
				return -EPROBE_DEFER;
			}
			of_node_put(fnode);
		}
	}

	dp->axi_lite_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(dp->axi_lite_clk))
		return PTR_ERR(dp->axi_lite_clk);

	dp->tx_vid_clk = devm_clk_get(&pdev->dev, "tx_vid_clk");
	if (IS_ERR(dp->tx_vid_clk))
		dev_err(dp->dev, "failed to get vid clk stream1\n");

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
				ret = PTR_ERR(dp->phy[i]);
				dp->phy[i] = NULL;
				if (ret == -EPROBE_DEFER) {
					dev_info(dp->dev,
						 "xvphy not ready -EPROBE_DEFER\n");
					return ret;
				}
				if (ret != -EPROBE_DEFER)
					dev_err(dp->dev, "failed to get phy lane %s i %d, error %d\n",
						phy_name, i, ret);
				goto error_phy;
			}
		}

		ret = xlnx_dp_init_phy(dp);
		if (ret)
			goto error_phy;
	} else {
		ret = xlnx_dp_tx_gt_control_init(dp);
		if (ret < 0)
			return ret;
	}

	ret = clk_prepare_enable(dp->axi_lite_clk);
	if (ret) {
		dev_err(dp->dev, "failed to enable axi_lite_clk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dp->tx_vid_clk);
	if (ret) {
		dev_err(dp->dev, "failed to enable tx_vid_clk (%d)\n", ret);
		goto tx_vid_clk_err;
	}

	dp->aux.name = "Xlnx DP AUX";
	dp->aux.dev = dp->dev;
	dp->aux.transfer = xlnx_dp_aux_transfer;
	ret = drm_dp_aux_register(&dp->aux);
	if (ret < 0) {
		dev_err(dp->dev, "failed to initialize DP aux\n");
		goto error;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto error;
	}
	ret = devm_request_threaded_irq(dp->dev, irq, NULL,
					xlnx_dp_irq_handler, IRQF_ONESHOT,
					dev_name(dp->dev), dp);

	if (ret < 0)
		goto error;

	if (dp->config.audio_enabled) {
		if (dptx_register_aud_dev(dp->dev)) {
			dp->audio_init = false;
			dev_err(dp->dev, "dp tx audio init failed\n");
			goto error;
		} else {
			dp->audio_init = true;
			dev_info(dp->dev, "dp tx audio initialized\n");
		}
	}

	return component_add(&pdev->dev, &xlnx_dp_component_ops);

tx_vid_clk_err:
	clk_disable_unprepare(dp->axi_lite_clk);
error:
	drm_dp_aux_unregister(&dp->aux);
error_phy:
	if (!dp->config.versal_gt_present) {
		dev_dbg(&pdev->dev, "xdprxss_probe() error_phy:\n");
		xlnx_dp_exit_phy(dp);
	} else {
		phy_exit(dp->phy[0]);
	}

	return ret;
}

static int xlnx_dp_remove(struct platform_device *pdev)
{
	struct xlnx_dp *dp = platform_get_drvdata(pdev);

	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 0);
	drm_dp_aux_unregister(&dp->aux);
	if (!dp->config.versal_gt_present)
		xlnx_dp_exit_phy(dp);
	else
		phy_exit(dp->phy[0]);
	component_del(&pdev->dev, &xlnx_dp_component_ops);

	return 0;
}

static const struct of_device_id xlnx_dp_of_match[] = {
	{ .compatible = "xlnx,v-dp-txss-3.0", },
	{ /* end of table */ }
};
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
MODULE_LICENSE("GPL v2");

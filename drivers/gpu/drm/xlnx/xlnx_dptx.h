/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA DisplayPort TX Subsystem Driver
 *
 *  Copyright (C) 2020 Xilinx, Inc.
 *
 * Author: Rajesh Gugulothu <gugulothu.rajesh@xilinx.com>
 *	 : Venkateshwar Rao G <vgannava.xilinx.com>
 *
 */
#ifndef __XLNX_DPTX_H__
#define __XLNX_DPTX_H__

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/hdr-ctrls.h>
#include <uapi/linux/videodev2.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_eld.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_simple_kms_helper.h>

#include <linux/hdmi.h>

#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <drm/display/drm_dp_mst_helper.h>
#include "hdcp/xlnx_hdcp_tx.h"
#include "xlnx_drv.h"

#define XDPTX_HDCP			0
#define XDPTX_HDCP2X_OFFSET		0x4000
#define XDPTX_HDCP_TIMER_OFFSET		0x6000
#define XDPTX_HDCP1X_OFFSET		0x2000
#define XDP_TX_HDCP1X_ENABLE		0x400
#define XDP_TX_HDCP1X_ENABLE_BYPASS_DISABLE_MASK 0x0001
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
#define XDPTX_MAINSTRM_ENABLE_MASK		BIT(0)
#define XDPTX_ENABLE_SEC_STREAM_REG		0x88
#define XDPTX_SCRAMBLER_RESET			0xc0
#define XDPTX_SCRAMBLER_RESET_MASK		BIT(0)
#define XDPTX_MST_CONFIG			0xd0
#define XDPTX_MST_CONFIG_MST_EN_MASK		BIT(0)
#define XDPTX_MST_CONFIG_VCP_UPDATED_MASK	BIT(1)

/* MST: VC payload buffer (64 slots x 4 bytes = 0x800..0x8FC) */
#define XDPTX_VC_PAYLOAD_BUFF			0x800
#define XDPTX_VC_PAYLOAD_BUFF_NSLOTS		64

/* MST: per-stream average timeslot registers - (TsInt << 16) | TsFrac */
#define XDPTX_STREAM1_TS_REG			0x1d0
#define XDPTX_STREAM_TS_STRIDE			4

/* MST: MSA register bank byte offsets for streams 2-4 relative to stream 1 (0x180) */
#define XDPTX_MST_MSA_OFFSET_S2			0x380u
#define XDPTX_MST_MSA_OFFSET_S3			0x3d0u
#define XDPTX_MST_MSA_OFFSET_S4			0x420u

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

#define XDPTX_VS_PE_LEVEL_MAXCOUNT		3
#define XDPTX_VS_LEVEL_MAXCOUNT			0x5

#define XDPTX_PHYSTATUS_REG			0x280
#define XDPTX_PHYSTATUS_FPGAPLLLOCK_MASK	BIT(6)
/*
 * On Versal Gen 2 series silicon (e.g. VEK385 board) the DP Tx GT Quad
 * drives only bits[5:0] of PHYSTATUS: bits[3:0] are the per-lane ready
 * bits and bits[5:4] carry GT Quad readiness flags. Bit[6] (FPGA PLL
 * lock) is not connected on this topology, so the legacy
 * (BIT(MAX_LANES) - 1) | FPGAPLLLOCK_MASK never converges.
 */
#define XDPTX_PHYSTATUS_VERSAL_GT_LANE_MASK	GENMASK(5, 0)
/* Max pixel clock (kHz): link payload rate (kB/s) per 8b/10b or 128b/132b coding. */
#define XDPTX_MAX_RATE(bw, lanecnt, bpp) \
	(drm_dp_max_dprx_data_rate((bw), (lanecnt)) * 8 / (bpp))

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
#define XDPTX_MAX_STREAMS			4
#define XDPTX_MAX_FREQ				3000000
#define XDPTX_SINK_PWR_CYCLES			3
#define XDPTX_ACT_POLL_MS			3000

#define XDPTX_REDUCED_BIT_RATE			162000
#define XDPTX_HIGH_BIT_RATE_1			270000
#define XDPTX_HIGH_BIT_RATE_2			540000
#define XDPTX_HIGH_BIT_RATE_3			810000

/* DP2.1 128b/132b (UHBR) link rates, in 10 kbps units (DRM convention). */
#define XDPTX_UHBR10_BIT_RATE		1000000
#define XDPTX_UHBR13_5_BIT_RATE		1350000
#define XDPTX_UHBR20_BIT_RATE		2000000
/* uhbr_rate_ceiling below UHBR10: no UHBR rate is negotiated until disconnect. */
#define XDPTX_UHBR_CEILING_8B10B_ONLY	1
/* Main-link channel-coding select: 8b/10b (DP1.4) vs 128b/132b (DP2.1). */
#define XDPTX_ML_CH_CODING_SET_REG	0x6a4
#define XDPTX_ML_CH_CODING_8B10B	0x01
#define XDPTX_ML_CH_CODING_128B132B	0x02
#define XDPTX_V2_0_CONFIG_REG		0x6e0
#define XDPTX_V2_0_CONFIG_RESET_VAL	0x3ff
/* DP2.1 per-stream video-frequency (Vfreq) registers (HIGH = LOW + 4). */
#define XDPTX_VFREQ_STREAM1_LOW		0x6d0
#define XDPTX_VFREQ_STREAM2_LOW		0x6e4
#define XDPTX_VFREQ_STREAM3_LOW		0x6ec
#define XDPTX_VFREQ_STREAM4_LOW		0x6f4
/* DP2.1 per-stream end-of-line reset disable. */
#define XDPTX_LINE_RESET_DISABLE_REG	0x0f0

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
#define XDP_TX_HDCP2x_ENABLE				0x404
#define XDP_TX_HDCP2x_ENABLE_BYPASS_DISABLE_MASK	BIT(0)
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

#define XDPTX_HDCP_DPCD_READ				0x00
#define XDPTX_HDCP_DPCD_WRITE				BIT(0)
#define XDPTX_HDCP_STATUS				BIT(1)

/* Flag to get VTC offset from device tree */
#define XDPTX_VTC_OFFSET_CHANGE		BIT(0)
/* IP core is a DP2.1 (128b/132b) variant, per compatible match */
#define XDPTX_DP21_CORE			BIT(1)

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

/**
 * struct xlnx_dp_link_config - Common link config between source and sink
 * @max_rate: Maximum link rate
 * @max_lanes: Maximum number of lanes
 * @link_rate: Current link rate
 * @lane_count: Current lane count
 * @cr_done_cnt: Lane count that achieved clock recovery
 * @cr_done_oldstate: Lane count from the previous clock-recovery pass
 * @link_training_2x: true when the 128b/132b (UHBR) training sequence is used
 * @channel_coding: Main-link channel coding (XDPTX_ML_CH_CODING_*)
 * @ffe_preset: Per-lane 128b/132b TX FFE presets requested by the sink
 * @uhbr_rate_ceiling: Highest UHBR rate to negotiate after a training failure;
 *	0 means no cap. Reset on disconnect.
 */
struct xlnx_dp_link_config {
	int max_rate;
	u8 max_lanes;
	int link_rate;
	u8 lane_count;
	u8 cr_done_cnt;
	u8 cr_done_oldstate;
	bool link_training_2x;
	u8 channel_coding;
	u8 ffe_preset[XDPTX_MAX_LANES];
	int uhbr_rate_ceiling;
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
 * struct xlnx_dp_stream_fmt - derived per-stream pixel format programming
 * @misc0: MSA MISC0 value (colour format + bpc bits)
 * @bpc: bits per component
 * @bpp: bits per pixel (bpc * num_colors)
 * @num_colors: number of colour components
 * @fmt: colour-format selector
 *
 * Computed from a framebuffer fourcc + bpc by xlnx_dp_calc_color()/
 * xlnx_dp_calc_bpc(). SST keeps a single instance in struct xlnx_dp (dp->fmt);
 * MST keeps one per stream in the connector atomic state
 * (struct xlnx_dp_mst_conn_state) so streams never stomp each other's format.
 */
struct xlnx_dp_stream_fmt {
	u8 misc0;
	u8 bpc;
	u8 bpp;
	u8 num_colors;
	u8 fmt;
};

/**
 * struct xlnx_dp_config - Configuration of DisplayPort from DTS
 * @max_lanes: Maximum number of lanes
 * @max_link_rate: Maximum supported link rate
 * @max_bpc: Maximum bits per component from DT (xlnx,bpc); default/ceiling
 * @pixel_mode: PL pixel packing from DT (xlnx,pixel-mode); MST uses it for ppc
 * @audio_enabled: flag to indicate audio is enabled in device tree
 * @versal_gt_present: flag to indicate versal-gt property in device tree
 * @versal_2ve_2vm: flag set at probe when zynqmp_pm_get_family_info()
 *	reports the platform is on Versal Gen 2 series silicon (Versal AI
 *	Edge series Gen 2, e.g. VEK385). On this family the GT Quad needs
 *	a small settling delay before each line-rate change.
 * @hdcp2x_enable: flag to indicate hdcp22-enable property in device tree
 * @hdcp1x_enable: flag to indicate hdcp-enable property in device tree
 * @num_streams: Number of DP TX video streams (xlnx,num-streams); the sole
 *	SST-vs-MST selector (num_streams == 1 -> SST, num_streams > 1 -> MST)
 * @dp21_present: true on a DP2.1 (128b/132b UHBR) core, from the compatible match
 */
struct xlnx_dp_config {
	u32 max_lanes;
	u32 max_link_rate;
	u8 max_bpc;
	u8 pixel_mode;
	bool audio_enabled;
	bool versal_gt_present;
	bool versal_2ve_2vm;
	bool hdcp2x_enable;
	bool hdcp1x_enable;
	u32 num_streams;
	/* DP2.1 (128b/132b UHBR), parsed from DT */
	bool dp21_present;
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
 * struct xlnx_dp_feature - dt or IP property structure
 * @flags: Bitmask of properties enabled in IP or dt
 */
struct xlnx_dp_feature {
	u32 flags;
};

/**
 * struct xlnx_dp_mst_encoder - per-stream MST encoder
 * @encoder: DRM encoder
 * @dp: back-pointer to the main DP structure
 * @stream_id: zero-based stream index (0..XDPTX_MAX_STREAMS-1)
 */
struct xlnx_dp_mst_encoder {
	struct drm_encoder encoder;
	struct xlnx_dp *dp;
	int stream_id;
};

/**
 * struct xlnx_dp_mst_connector - MST virtual connector
 * @connector: DRM connector
 * @dp: back-pointer to the main DP structure
 * @port: MST port this connector represents
 */
struct xlnx_dp_mst_connector {
	struct drm_connector connector;
	struct xlnx_dp *dp;
	struct drm_dp_mst_port *port;
};

/**
 * struct xlnx_dp_mst_conn_state - MST connector atomic state
 * @base: base DRM connector state
 * @fmt: per-stream pixel format
 */
struct xlnx_dp_mst_conn_state {
	struct drm_connector_state base;
	struct xlnx_dp_stream_fmt fmt;
};

/**
 * struct xlnx_dp - Xilinx DisplayPort core
 * @dev: device structure
 * @encoder: the drm encoder structure
 * @bridge: the drm bridge structure
 * @connector: the drm bridge connector (created by drm_bridge_connector_init)
 * @conn_lock: keeps @connector valid for the ALSA ELD read across unbind
 * @aux: aux channel
 * @config: IP core configuration from DTS
 * @fmt: SST stream pixel format (derived at runtime from the framebuffer)
 * @tx_link_config: source configuration
 * @tx_hdcp: HDCP configuration
 * @hdcpx_keymgmt_base: HDCP key management base address
 * @link_config: common link configuration between IP core and sink device
 * @drm: DRM core
 * @mode: current mode between IP core and sink device
 * @phy: PHY handles for DP lanes
 * @axi_lite_clk: axi lite clock
 * @tx_vid_clk: tx video clock
 * @per_stream_vid_clk: true if each MST stream drives its own pixel clock
 * @reset_gpio: reset gpio
 * @hpd_work: hot plug detection worker
 * @hpd_pulse_work: hot plug pulse detection worker
 * @hdcp_cp_irq_work: HDCP content protection message indication worker
 * @tx_audio_data: audio data
 * @infoframe: DRM infoframe
 * @vscpkt: VSC extended packet
 * @cfg: Pointer to DP Feature config struct
 * @phy_opts: Opaque generic phy configuration
 * @status: connection status
 * @dp_base: Base address of DisplayPort Tx subsystem
 * @dpms: current dpms state
 * @hdcptx_timer_irq: HDCP TX timer interrupt
 * @irq: main DisplayPort interrupt number (used to synchronize unbind())
 * @vtc_off: VTC sub-core offset address
 * @dpcd: DP configuration data from currently connected sink device
 * @train_set: set of training data
 * @num_lanes: number of enabled phy lanes
 * @enabled: flag to indicate if the device is enabled
 * @audio_init: flag to indicate audio is initialized
 * @have_edid: flag to indicate if edid is available
 * @colorimetry_through_vsc: colorimetry information through vsc packets
 * @mst_mgr: MST topology manager - valid when config.num_streams > 1
 * @esi_lock: serializes the MST ESI sideband (xlnx_dp_mst_process_esi),
 *	driven from both the HPD-pulse work item and the topology manager's
 *	poll_hpd_irq callback
 * @mst_encoders: per-stream MST encoders (fixed pool, one per HW stream/CRTC)
 * @mst_modeset_retry_work: flags MST links BAD when a retrain invalidates a
 *	committed slot allocation
 * @mst_link_retrain_work: recovers a lost MST link with an internal modeset
 * @mst_retrain_attempts: in-kernel recoveries since the link was last good
 * @mst_link_trained: true once the MST main link has completed training
 * @mst_fresh_train: true for the first stream enable after a fresh train
 * @mst_enabling: true while (re)establishing the MST topology
 * @mst_bringup_failures: failed MST bring-ups since the last HPD event
 * @mst_link_lost: true once a live MST link dropped; the next enable retrains
 * @mst_to_sst_relink: true when a torn-down MST session must relink to SST
 * @mst_stream_count: number of currently active MST streams
 * @mst_active_mask: bitmask of active MST stream IDs
 * @drm_master: logical DRM pipeline master (set when this node owns the pipeline)
 * @mst_mgr_init: true once mst_mgr has been initialized
 * @mst_mode: true when configured for MST (num_streams > 1)
 * @cur_mode: last committed SST mode; lets xlnx_dp_start re-apply UHBR VC framing
 * @cur_mode_lock: protects cur_mode against concurrent atomic_mode_set() vs
 *	HPD-driven xlnx_dp_start()
 */
struct xlnx_dp {
	struct device *dev;
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_connector *connector;
	struct mutex conn_lock; /* serializes xlnx_dp_get_eld() with dp->connector bind/unbind */
	struct drm_dp_aux aux;
	struct xlnx_dp_config config;
	struct xlnx_dp_stream_fmt fmt;
	struct xlnx_dp_tx_link_config tx_link_config;
	struct xlnx_hdcptx tx_hdcp;
	struct regmap *hdcpx_keymgmt_base;
	struct xlnx_dp_link_config link_config;
	struct drm_device *drm;
	struct xlnx_dp_mode mode;
	struct phy *phy[XDPTX_MAX_LANES];
	struct clk *axi_lite_clk;
	struct clk *tx_vid_clk[XDPTX_MAX_STREAMS];
	bool per_stream_vid_clk;
	struct gpio_desc *reset_gpio;
	struct delayed_work hpd_work;
	struct delayed_work hpd_pulse_work;
	struct delayed_work hdcp_cp_irq_work;
	struct xlnx_dptx_audio_data *tx_audio_data;
	struct xlnx_dp_infoframe infoframe;
	struct xlnx_dp_vscpkt vscpkt;
	const struct xlnx_dp_feature *cfg;
	union phy_configure_opts phy_opts;
	enum drm_connector_status status;
	void __iomem *dp_base;
	int dpms;
	int hdcptx_timer_irq;
	int irq;
	u32 vtc_off[XDPTX_MAX_STREAMS];
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	u8 train_set[XDPTX_MAX_LANES];
	u8 num_lanes;
	unsigned int enabled : 1;
	bool audio_init;
	bool have_edid;
	unsigned int colorimetry_through_vsc : 1;
	/* Logical DRM pipeline master (set when this node owns the pipeline) */
	struct platform_device *drm_master;
	/* MST topology manager - valid when config.num_streams > 1 */
	struct drm_dp_mst_topology_mgr mst_mgr;
	bool mst_mgr_init;
	/* True when configured for MST (num_streams > 1) */
	bool mst_mode;
	/*
	 * Serializes the MST ESI sideband pump (xlnx_dp_mst_process_esi),
	 * driven from both the HPD-pulse work item and the topology manager's
	 * poll_hpd_irq callback.
	 */
	struct mutex esi_lock;
	/* Per-stream MST encoders (fixed pool, one per HW stream/CRTC) */
	struct xlnx_dp_mst_encoder *mst_encoders[XDPTX_MAX_STREAMS];
	struct work_struct mst_modeset_retry_work;
	struct work_struct mst_link_retrain_work;
	int mst_retrain_attempts;
	/* MST link / stream state */
	bool mst_link_trained;
	bool mst_fresh_train;
	bool mst_enabling;
	int mst_bringup_failures;
	bool mst_link_lost;
	bool mst_to_sst_relink;
	/* Last committed SST mode; lets xlnx_dp_start re-apply UHBR VC framing. */
	struct drm_display_mode cur_mode;
	/* Protects cur_mode against concurrent atomic_mode_set() vs HPD-driven xlnx_dp_start(). */
	struct mutex cur_mode_lock;
	int mst_stream_count;
	u32 mst_active_mask;
};

/* Shared helpers implemented in xlnx_dptx.c and used by xlnx_dptx_mst.c */
int xlnx_dp_start(struct xlnx_dp *dp);
void xlnx_dp_vtc_set_timing(struct xlnx_dp *dp,
			    const struct drm_display_mode *mode,
			    u32 vtc_off, u32 ppc);
void xlnx_dp_mode_set_stream_offset(struct xlnx_dp *dp,
				    const struct drm_display_mode *mode, u32 off,
				    const struct xlnx_dp_stream_fmt *stream_fmt);
void xlnx_dp_set_color(struct xlnx_dp *dp, u32 drm_fourcc);
u32 xlnx_dp_set_bpc(struct xlnx_dp *dp, u8 bpc);
void xlnx_dp_calc_color(struct xlnx_dp *dp, u32 drm_fourcc,
			struct xlnx_dp_stream_fmt *stream_fmt);
u32 xlnx_dp_calc_bpc(struct xlnx_dp *dp, u8 bpc,
		     const struct drm_display_info *info,
		     struct xlnx_dp_stream_fmt *stream_fmt);
u8 xlnx_dp_bpc_from_fourcc(struct xlnx_dp *dp, u32 drm_fourcc);
u32 xlnx_dp_calc_user_ppc(struct xlnx_dp *dp, int clock_khz, u8 lane_cnt);
bool xlnx_dp_uhbr_active(struct xlnx_dp *dp);

/* Low-level register accessors - implemented in xlnx_dptx.c */
void xlnx_dp_write(void __iomem *base, int offset, u32 val);
u32 xlnx_dp_read(void __iomem *base, int offset);
void xlnx_dp_set(void __iomem *base, int offset, u32 set);
void xlnx_dp_clr(void __iomem *base, int offset, u32 clr);

/* MST entry points implemented in xlnx_dptx_mst.c and used by xlnx_dptx.c */
int xlnx_dp_mst_enable(struct xlnx_dp *dp);
int xlnx_dp_mst_clear_payload_table(struct xlnx_dp *dp);
int xlnx_dp_uhbr_sst_setup_framing(struct xlnx_dp *dp,
				   const struct drm_display_mode *mode);
void xlnx_dp_mst_process_esi(struct xlnx_dp *dp);
int xlnx_dp_mst_create_encoders(struct xlnx_dp *dp);
extern const struct drm_dp_mst_topology_cbs xlnx_dp_mst_cbs;
#endif /* __XLNX_DPTX_H__ */

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

#include <linux/mfd/syscon.h>
#include "hdcp/xlnx_hdcp_tx.h"

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
#define XDPTX_ENABLE_SEC_STREAM_REG		0x88
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

#endif /* __XLNX_DPTX_H__ */

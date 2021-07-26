// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DP Rx Subsystem
 *
 * Copyright (C) 2020 Xilinx, Inc.
 *
 * Author: Rajesh Gugulothu <gugulothu.rajesh@xilinx.com>
 *
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/v4l2-subdev.h>

#include <drm/drm_dp_helper.h>
#include <dt-bindings/media/xilinx-vip.h>

#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include <sound/soc.h>

#include "xilinx-vip.h"

#define XV_AES_ENABLE			0x8
#define XDP_AUDIO_DETECT_TIMEOUT	500 /* milliseconds */
#define INFO_PCKT_SIZE_WORDS		8
#define INFO_PCKT_SIZE			(INFO_PCKT_SIZE_WORDS * 4)
#define INFO_PCKT_TYPE_AUDIO		0x84

/* DP Rx subsysetm register map, bitmask, and offsets. */
#define XDPRX_LINK_ENABLE_REG		0x000
#define XDPRX_AUX_CLKDIV_REG		0x004
#define XDPRX_AUX_DEFER_COUNT		6
#define XDPRX_AUX_DEFER_SHIFT		24
#define XDPRX_AUX_DEFER_MASK		GENMASK(27, 24)

#define XDPRX_LINERST_DIS_REG		0x008
#define XDPRX_DTG_REG			0x00c
#define XDPRX_DTG_DIS_MASK		GENMASK(31, 1)

#define XDPRX_PIXEL_WIDTH_REG		0x010
#define XDPRX_INTR_MASK_REG		0x014
#define XDPRX_INTR_POWER_MASK		BIT(1)
#define XDPRX_INTR_NOVID_MASK		BIT(2)
#define XDPRX_INTR_VBLANK_MASK		BIT(3)
#define XDPRX_INTR_TRLOST_MASK		BIT(4)
#define XDPRX_INTR_VID_MASK		BIT(6)
#define XDPRX_INTR_AUDIO_MASK		BIT(8)
#define XDPRX_INTR_TRDONE_MASK		BIT(14)
#define XDPRX_INTR_BWCHANGE_MASK	BIT(15)
#define XDPRX_INTR_TP1_MASK		BIT(16)
#define XDPRX_INTR_T2_MASK		BIT(17)
#define XDPRX_INTR_TP3_MASK		BIT(18)
#define XDPRX_INTR_LINKQUAL_MASK	BIT(29)
#define XDPRX_INTR_UNPLUG_MASK		BIT(31)
#define XDPRX_INTR_CRCTST_MASK		BIT(30)
#define XDPRX_INTR_TRNG_MASK		(XDPRX_INTR_TP1_MASK | \
					 XDPRX_INTR_T2_MASK |\
					 XDPRX_INTR_TP3_MASK | \
					 XDPRX_INTR_POWER_MASK |\
					 XDPRX_INTR_CRCTST_MASK |\
					 XDPRX_INTR_BWCHANGE_MASK)
#define XDPRX_INTR_ALL_MASK		0xffffffff

#define XDPRX_SOFT_RST_REG		0x01c
#define XDPRX_SOFT_VIDRST_MASK		BIT(0)
#define XDPRX_SOFT_AUXRST_MASK		BIT(7)

#define XDPRX_HPD_INTR_REG		0x02c
#define XDPRX_HPD_INTR_MASK		BIT(1)
#define XDPRX_HPD_PULSE_MASK		GENMASK(31, 16)

#define XDPRX_INTR_CAUSE_REG		0x040
#define XDPRX_INTR_CAUSE1_REG		0x048
#define XDPRX_CRC_CONFIG_REG		0x074
#define XDPRX_CRC_EN_MASK		BIT(5)

#define XDPRX_LOCAL_EDID_REG		0x084
#define XDPRX_VIDEO_UNSUPPORTED_REG	0x094
#define XDPRX_VRD_BWSET_REG		0x09c
#define XDPRX_LANE_CNT_REG		0x0a0
#define XDPRX_EFRAME_CAP_MASK		BIT(7)
#define XDPRX_LNCNT_TPS3_MASK		BIT(6)

#define XDPRX_TP_SET_REG		0x0a4
#define XDPRX_AUX_RDINT_SHIFT		8
#define XDPRX_AUX_RDINT_16MS		4
#define XDPRX_AUX_READINTRVL_REG	BIT(15)

#define XDPRX_CTRL_DPCD_REG		0x0b8
#define XDPRX_MST_CAP_REG		0x0d0
#define XDPRX_SINK_COUNT_REG		0x0d4

#define XDPRX_PHY_REG			0x200
#define XDPRX_PHY_GTPLLRST_MASK		BIT(0)
#define XDPRX_PHY_GTRXRST_MASK		BIT(1)
#define XDPRX_PHYRST_TRITER_MASK	BIT(23)
#define XDPRX_PHYRST_RATECHANGE_MASK	BIT(24)
#define XDPRX_PHYRST_TP1START_MASK	BIT(25)
#define XDPRX_PHYRST_ENBL_MASK		0x0
#define XDPRX_PHY_INIT_MASK		GENMASK(29, 27)

#define XDPRX_MINVOLT_SWING_REG		0x214
#define XDPRX_VS_PE_SHIFT		12
#define XDPRX_VS_SWEEP_CNTSHIFT		4
#define XDPRX_VS_CROPT_SHIFT		2
#define XDPRX_VS_CROPT_INC4CNT		1
#define XDPRX_MIN_VS_MASK		(1 | (XDPRX_VS_CROPT_INC4CNT << \
					 XDPRX_VS_CROPT_SHIFT) | \
					 (4 << XDPRX_VS_SWEEP_CNTSHIFT) | \
					 (1 << XDPRX_VS_PE_SHIFT))

#define XDPRX_CDRCTRL_CFG_REG		0x21c
/* CDR tDLOCK calibration value */
#define XDPRX_CDRCTRL_TDLOCK_VAL	0x1388
#define XDPRX_CDRCTRL_DIS_TIMEOUT	BIT(30)

#define XDPRX_BSIDLE_TIME_REG		0x220
#define XDPRX_BSIDLE_TMOUT_VAL		0x047868C0

#define XDPRX_AUDIO_CONTROL		0x300
#define XDPRX_AUDIO_EN_MASK		BIT(0)
#define XDPRX_AUDIO_INFO_DATA		0x304
#define XDPRX_AUDIO_MAUD		0x324
#define XDPRX_AUDIO_NAUD		0x328
#define XDPRX_AUDIO_STATUS		0x32C

#define XDPRX_LINK_BW_REG		0x400
#define XDPRX_LANE_COUNT_REG		0x404
#define XDPRX_MSA_HRES_REG		0x500
#define XDPRX_MSA_VHEIGHT_REG		0x514
#define XDPRX_MSA_HTOTAL_REG		0x510
#define XDPRX_MSA_VTOTAL_REG		0x524
#define XDPRX_MSA_MISC0_REG		0x528
#define XDPRX_MSA_FMT_MASK		GENMASK(2, 1)
#define XDPRX_MSA_BPC_MASK		GENMASK(7, 5)
#define XDPRX_COLOR_DEPTH_SHIFT		5
#define XDPRX_COLOR_FMT_SHIFT		1

#define XDPRX_MSA_MISC1_REG		0x52c
#define XDPRX_INTERLACE_MASK		BIT(0)

#define XDPRX_MSA_MVID_REG		0x530
#define XDPRX_MSA_NVID_REG		0x534
#define XDPRX_INTR_ERRORCNT_MASK	BIT(28)
#define XDPRX_INTR_LANESET_MASK		BIT(30)

#define XDPRX_COLOR_FORMAT_RGB		0x0
#define XDPRX_COLOR_FORMAT_422		0x1
#define XDPRX_COLOR_FORMAT_444		0x2
#define MHZ				1000000
#define XDPRX_MAX_LANE_COUNT		4

#define XDPRX_EDID_NUM_BLOCKS		3
#define XDPRX_EDID_BLOCK_SIZE		128
#define XDPRX_EDID_LENGTH		(XDPRX_EDID_BLOCK_SIZE * \
					 XDPRX_EDID_NUM_BLOCKS * 4)
/*
 * IRQ_HPD pulse for upstream device is 5ms as per
 * the VESA standard
 */
#define XDPRX_HPD_PLUSE_5000		5000
/*
 * low going IRQ_HPD generated for upstream device
 * as per the VESA standard
 */
#define XDPRX_HPD_PLUSE_750		750

/**
 * struct xlnx_dprx_audio_data - DP Rx Subsystem audio data structure
 * @infoframe: Audio infoframe that is received
 * @audio_detected: To indicate audio detection
 * @audio_update_q: wait queue for audio detection
 */
struct xlnx_dprx_audio_data {
	u32 infoframe[8];
	bool audio_detected;
	wait_queue_head_t audio_update_q;
};

/**
 * struct xdprxss_state - DP Rx Subsystem device structure
 * @dev: Platform structure
 * @subdev: The v4l2 subdev structure
 * @event: Holds the video unlock event
 * @detected_timings: Detected Video timings
 * @phy: pointer to phy instance
 * @pad: media pad
 * @axi_clk: Axi lite interface clock
 * @rx_lnk_clk: DP Rx GT clock
 * @rx_vid_clk: DP RX Video clock
 * @dp_base: Base address of DP Rx Subsystem
 * @edid_base: Bare Address of EDID block
 * @lock: Lock is used for width, height, framerate variables
 * @format: Active V4L2 format on each pad
 * @frame_interval: Captures the frame rate
 * @max_linkrate: Maximum supported link rate
 * @max_lanecount: Maximux supported lane count
 * @bpc: Bits per component
 * @hdcp_enable: To indicate hdcp enabled or not
 * @audio_enable: To indicate audio enabled or not
 * @audio_init: flag to indicate audio is initialized
 * @rx_audio_data: audio data
 * @valid_stream: To indicate valid video
 * @streaming: Flag for storing streaming state
 * This structure contains the device driver related parameters
 */
struct xdprxss_state {
	struct device *dev;
	struct v4l2_subdev subdev;
	struct v4l2_event event;
	struct v4l2_dv_timings detected_timings;
	struct phy *phy[XDPRX_MAX_LANE_COUNT];
	struct media_pad pad;
	struct clk *axi_clk;
	struct clk *rx_lnk_clk;
	struct clk *rx_vid_clk;
	void __iomem *dp_base;
	void __iomem *edid_base;
	/* protects width, height, framerate variables */
	spinlock_t lock;
	struct v4l2_mbus_framefmt format;
	unsigned int frame_interval;
	u32 max_linkrate;
	u32 max_lanecount;
	u32 bpc;
	bool hdcp_enable;
	bool audio_enable;
	bool audio_init;
	struct xlnx_dprx_audio_data *rx_audio_data;
	unsigned int valid_stream : 1;
	unsigned int streaming : 1;
};

/*
 * This is a default EDID data loaded to EDID memory. It allows the source
 * to get edid before application start on DP Rx.User can load their
 * custom EDID data using set_edid functions call
 */
static u8 xilinx_edid[384] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x10, 0xac, 0x47, 0x41,
	0x4c, 0x35, 0x37, 0x30, 0x20, 0x1b, 0x01, 0x04, 0xb5, 0x46, 0x27, 0x78,
	0x3a, 0x76, 0x45, 0xae, 0x51, 0x33, 0xba, 0x26, 0x0d, 0x50, 0x54, 0xa5,
	0x4b, 0x00, 0x81, 0x00, 0xb3, 0x00, 0xd1, 0x00, 0xa9, 0x40, 0x81, 0x80,
	0xd1, 0xc0, 0x01, 0x01, 0x01, 0x01, 0x4d, 0xd0, 0x00, 0xa0, 0xf0, 0x70,
	0x3e, 0x80, 0x30, 0x20, 0x35, 0x00, 0xba, 0x89, 0x21, 0x00, 0x00, 0x1a,
	0x00, 0x00, 0x00, 0xff, 0x00, 0x46, 0x46, 0x4e, 0x58, 0x4d, 0x37, 0x38,
	0x37, 0x30, 0x37, 0x35, 0x4c, 0x0a, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x44,
	0x45, 0x4c, 0x4c, 0x20, 0x55, 0x50, 0x33, 0x32, 0x31, 0x38, 0x4b, 0x0a,
	0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x4b, 0x1e, 0xb4, 0x6c, 0x01, 0x0a,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x02, 0x70, 0x02, 0x03, 0x1d, 0xf1,
	0x50, 0x10, 0x1f, 0x20, 0x05, 0x14, 0x04, 0x13, 0x12, 0x11, 0x03, 0x02,
	0x16, 0x15, 0x07, 0x06, 0x01, 0x23, 0x09, 0x1f, 0x07, 0x83, 0x01, 0x00,
	0x00, 0xa3, 0x66, 0x00, 0xa0, 0xf0, 0x70, 0x1f, 0x80, 0x30, 0x20, 0x35,
	0x00, 0xba, 0x89, 0x21, 0x00, 0x00, 0x1a, 0x56, 0x5e, 0x00, 0xa0, 0xa0,
	0xa0, 0x29, 0x50, 0x30, 0x20, 0x35, 0x00, 0xba, 0x89, 0x21, 0x00, 0x00,
	0x1a, 0x7c, 0x39, 0x00, 0xA0, 0x80, 0x38, 0x1f, 0x40, 0x30, 0x20, 0x3a,
	0x00, 0xba, 0x89, 0x21, 0x00, 0x00, 0x1a, 0xa8, 0x16, 0x00, 0xa0, 0x80,
	0x38, 0x13, 0x40, 0x30, 0x20, 0x3a, 0x00, 0xba, 0x89, 0x21, 0x00, 0x00,
	0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x47, 0x70, 0x12, 0x79, 0x00, 0x00, 0x12, 0x00, 0x16,
	0x82, 0x10, 0x10, 0x00, 0xff, 0x0e, 0xdf, 0x10, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x44, 0x45, 0x4c, 0x47, 0x41, 0x4c, 0x35, 0x37, 0x30, 0x03, 0x01,
	0x50, 0x70, 0x92, 0x01, 0x84, 0xff, 0x1d, 0xc7, 0x00, 0x1d, 0x80, 0x09,
	0x00, 0xdf, 0x10, 0x2f, 0x00, 0x02, 0x00, 0x04, 0x00, 0xc1, 0x42, 0x01,
	0x84, 0xff, 0x1d, 0xc7, 0x00, 0x2f, 0x80, 0x1f, 0x00, 0xdf, 0x10, 0x30,
	0x00, 0x02, 0x00, 0x04, 0x00, 0xa8, 0x4e, 0x01, 0x04, 0xff, 0x0e, 0xc7,
	0x00, 0x2f, 0x80, 0x1f, 0x00, 0xdf, 0x10, 0x61, 0x00, 0x02, 0x00, 0x09,
	0x00, 0x97, 0x9d, 0x01, 0x04, 0xff, 0x0e, 0xc7, 0x00, 0x2f, 0x80, 0x1f,
	0x00, 0xdf, 0x10, 0x2f, 0x00, 0x02, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x90,
};

static const u32 xdprxss_supported_mbus_fmts[] = {
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_VUY8_1X24,
	MEDIA_BUS_FMT_RBG888_1X24,
	MEDIA_BUS_FMT_UYVY10_1X20,
	MEDIA_BUS_FMT_VUY10_1X30,
	MEDIA_BUS_FMT_RBG101010_1X30,
};

#define XLNX_V4L2_DV_BT_2048X1080P60 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(2048, 1080, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		148500000, 88, 44, 20, 4, 5, 36, 0, 0, 0, \
		V4L2_DV_BT_STD_CEA861) \
}

#define XLNX_V4L2_DV_BT_2048X1080I50 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(2048, 1080, 1, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		74250000, 274, 44, 274, 2, 5, 15, 3, 5, 15, \
		V4L2_DV_BT_STD_CEA861) \
}

#define XLNX_V4L2_DV_BT_2048X1080I60 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(2048, 1080, 1, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		74250000, 66, 20, 66, 2, 5, 15, 3, 5, 15, \
		V4L2_DV_BT_STD_CEA861) \
}

#define XLNX_V4L2_DV_BT_2048X1080P50 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(2048, 1080, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		148500000, 400, 44, 148, 4, 5, 36, 0, 0, 0, \
		V4L2_DV_BT_STD_CEA861) \
}

#define XLNX_V4L2_DV_BT_7680X4320P30 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(7680, 4320, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		74250000, 552, 176, 592, 16, 20, 44, 0, 0, 0, \
		V4L2_DV_BT_STD_CEA861) \
}

static const struct v4l2_dv_timings fmt_cap[] = {
	V4L2_DV_BT_CEA_1280X720P25,
	V4L2_DV_BT_CEA_1280X720P30,
	V4L2_DV_BT_CEA_1280X720P50,
	V4L2_DV_BT_CEA_1280X720P60,
	V4L2_DV_BT_CEA_1920X1080P25,
	V4L2_DV_BT_CEA_1920X1080P30,
	V4L2_DV_BT_CEA_1920X1080P50,
	V4L2_DV_BT_CEA_1920X1080P60,
	V4L2_DV_BT_CEA_1920X1080I50,
	V4L2_DV_BT_CEA_1920X1080I60,
	V4L2_DV_BT_CEA_3840X2160P30,
	V4L2_DV_BT_CEA_3840X2160P50,
	V4L2_DV_BT_CEA_3840X2160P60,
	V4L2_DV_BT_CEA_4096X2160P25,
	V4L2_DV_BT_CEA_4096X2160P30,
	V4L2_DV_BT_CEA_4096X2160P50,
	V4L2_DV_BT_CEA_4096X2160P60,

	XLNX_V4L2_DV_BT_2048X1080I50,
	XLNX_V4L2_DV_BT_2048X1080I60,
	XLNX_V4L2_DV_BT_2048X1080P50,
	XLNX_V4L2_DV_BT_2048X1080P60,
	XLNX_V4L2_DV_BT_7680X4320P30,
};

struct xdprxss_dv_map {
	u32 width;
	u32 height;
	u32 fps;
	struct v4l2_dv_timings timing;
};

static const struct xdprxss_dv_map xdprxss_dv_timings[] = {
	/* HD - 1280x720p25 */
	{ 1280, 720, 25, V4L2_DV_BT_CEA_1280X720P25 },
	/* HD - 1280x720p30 */
	{ 1280, 720, 30, V4L2_DV_BT_CEA_1280X720P30 },
	/* HD - 1280x720p50 */
	{ 1280, 720, 50, V4L2_DV_BT_CEA_1280X720P50 },
	/* HD - 1280x720p60 */
	{ 1280, 720, 60, V4L2_DV_BT_CEA_1280X720P60 },
	/* HD - 1920x1080p25 */
	{ 1920, 1080, 25, V4L2_DV_BT_CEA_1920X1080P25 },
	/* HD - 1920x1080p30 */
	{ 1920, 1080, 30, V4L2_DV_BT_CEA_1920X1080P30 },
	/* HD - 1920x1080p50 */
	{ 1920, 1080, 50, V4L2_DV_BT_CEA_1920X1080P50 },
	/* HD - 1920x1080p60 */
	{ 1920, 1080, 60, V4L2_DV_BT_CEA_1920X1080P60 },
	/* HD - 1920x1080i50 */
	{ 1920, 540, 25, V4L2_DV_BT_CEA_1920X1080I50 },
	/* HD - 1920x1080i59.94 */
	/* HD - 1920x1080i60 */
	{ 1920, 540, 30, V4L2_DV_BT_CEA_1920X1080I60 },
	{ 3840, 2160, 30, V4L2_DV_BT_CEA_3840X2160P30 },
	{ 3840, 2160, 50, V4L2_DV_BT_CEA_3840X2160P50 },
	{ 3840, 2160, 60, V4L2_DV_BT_CEA_3840X2160P60 },
	{ 4096, 2160, 25, V4L2_DV_BT_CEA_4096X2160P25 },
	{ 4096, 2160, 30, V4L2_DV_BT_CEA_4096X2160P30 },
	{ 4096, 2160, 50, V4L2_DV_BT_CEA_4096X2160P50 },
	{ 4096, 2160, 60, V4L2_DV_BT_CEA_4096X2160P60 },
	/* HD - 2048x1080i50 */
	{ 2048, 540, 25, XLNX_V4L2_DV_BT_2048X1080I50 },
	/* HD - 2048x1080i59.94 */
	/* HD - 2048x1080i60 */
	{ 2048, 540, 30, XLNX_V4L2_DV_BT_2048X1080I60 },
	/* 3G - 2048x1080p50 */
	{ 2048, 1080, 50, XLNX_V4L2_DV_BT_2048X1080P50 },
	/* 3G - 2048x1080p59.94 */
	/* 3G - 2048x1080p60 */
	{ 2048, 1080, 60, XLNX_V4L2_DV_BT_2048X1080P60 },
	{ 7680, 4320, 30, XLNX_V4L2_DV_BT_7680X4320P30 }
};

static inline struct xdprxss_state *
to_xdprxssstate(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xdprxss_state, subdev);
}

/* Register related operations */
static inline u32 xdprxss_read(struct xdprxss_state *xdprxss, u32 addr)
{
	return ioread32(xdprxss->dp_base + addr);
}

static inline void xdprxss_write(struct xdprxss_state *xdprxss, u32 addr,
				 u32 value)
{
	iowrite32(value, xdprxss->dp_base + addr);
}

static inline void xdprxss_clr(struct xdprxss_state *xdprxss, u32 addr,
			       u32 clr)
{
	xdprxss_write(xdprxss, addr, xdprxss_read(xdprxss, addr) & ~clr);
}

static inline void xdprxss_set(struct xdprxss_state *xdprxss, u32 addr,
			       u32 set)
{
	xdprxss_write(xdprxss, addr, xdprxss_read(xdprxss, addr) | set);
}

static inline void xdprxss_dpcd_update_start(struct xdprxss_state *xdprxss)
{
	iowrite32(0x1, xdprxss->dp_base + XDPRX_CTRL_DPCD_REG);
}

static inline void xdprxss_dpcd_update_end(struct xdprxss_state *xdprxss)
{
	iowrite32(0x0, xdprxss->dp_base + XDPRX_CTRL_DPCD_REG);
}

/**
 * xdprxss_dpcd_update - Update the DPCD registers
 * @xdprxss: pointer to driver state
 * @addr: DPCD register address
 * @val: Value to be override
 * This function is used to override the DPCD registers set.
 * DPCD register set is ranges from 0x084-0x0f0.
 * Register 0x0B8(direct_dpcd_access) must be set to 1 to
 * override DPCD values
 */
static inline void xdprxss_dpcd_update(struct xdprxss_state *xdprxss,
				       u32 addr, u32 val)
{
	xdprxss_write(xdprxss, addr, val);
}

/**
 * xdprxss_get_stream_properties - Get DP Rx stream properties
 * @state: pointer to driver state
 * This function decodes the stream to get stream properties
 * like width, height, format, picture type (interlaced/progressive),etc.
 *
 * Return: 0 for success else errors
 */
static int xdprxss_get_stream_properties(struct xdprxss_state *state)
{
	struct v4l2_mbus_framefmt *format = &state->format;
	struct v4l2_bt_timings *bt = &state->detected_timings.bt;

	u32 rxmsa_mvid, rxmsa_nvid, rxmsa_misc, recv_clk_freq, linkrate;
	u16 vres_total, hres_total, framerate, lanecount;
	u8 pixel_width, fmt;
	u16 read_val;

	rxmsa_mvid = xdprxss_read(state, XDPRX_MSA_MVID_REG);
	rxmsa_nvid = xdprxss_read(state, XDPRX_MSA_NVID_REG);

	bt->width = xdprxss_read(state, XDPRX_MSA_HRES_REG);

	bt->height = xdprxss_read(state, XDPRX_MSA_VHEIGHT_REG);
	rxmsa_misc = xdprxss_read(state, XDPRX_MSA_MISC0_REG);

	vres_total = xdprxss_read(state, XDPRX_MSA_VTOTAL_REG);
	hres_total = xdprxss_read(state, XDPRX_MSA_HTOTAL_REG);
	linkrate = xdprxss_read(state, XDPRX_LINK_BW_REG);
	lanecount = xdprxss_read(state, XDPRX_LANE_COUNT_REG);

	recv_clk_freq = (linkrate * 27 * rxmsa_mvid) / rxmsa_nvid;

	if (recv_clk_freq > 540 && lanecount == 4)
		pixel_width = 0x4;
	else if (recv_clk_freq > 270 && (lanecount != 1))
		pixel_width = 0x2;
	else
		pixel_width = 0x1;

	framerate = (recv_clk_freq * MHZ) / (hres_total * vres_total);
	framerate = roundup(framerate, 5);
	xdprxss_write(state, XDPRX_LINERST_DIS_REG, 0x1);
	/* set pixel mode as per lane count and reset the DTG */
	read_val = xdprxss_read(state, XDPRX_DTG_REG);
	xdprxss_write(state, XDPRX_DTG_REG, (read_val & XDPRX_DTG_DIS_MASK));
	xdprxss_write(state, XDPRX_PIXEL_WIDTH_REG, pixel_width);
	read_val = xdprxss_read(state, XDPRX_DTG_REG);
	xdprxss_write(state, XDPRX_DTG_REG, (read_val | 0x1));
	fmt = FIELD_GET(XDPRX_MSA_FMT_MASK, rxmsa_misc);
	state->bpc = FIELD_GET(XDPRX_MSA_BPC_MASK, rxmsa_misc);

	switch (fmt) {
	case XDPRX_COLOR_FORMAT_422:
		if (state->bpc == 10)
			format->code = MEDIA_BUS_FMT_UYVY10_1X20;
		else
			format->code = MEDIA_BUS_FMT_UYVY8_1X16;
		break;
	case XDPRX_COLOR_FORMAT_444:
		if (state->bpc == 10)
			format->code = MEDIA_BUS_FMT_VUY10_1X30;
		else
			format->code = MEDIA_BUS_FMT_VUY8_1X24;
		break;
	case XDPRX_COLOR_FORMAT_RGB:
		if (state->bpc == 10)
			format->code = MEDIA_BUS_FMT_RBG101010_1X30;
		else
			format->code = MEDIA_BUS_FMT_RBG888_1X24;
		break;
	default:
		dev_err(state->dev, "Unsupported color format\n");

		return -EINVAL;
	}

	spin_lock(&state->lock);
	format->width = bt->width;
	format->height = bt->height;
	format->colorspace = V4L2_COLORSPACE_REC709;
	format->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_DEFAULT;
	format->field = V4L2_FIELD_NONE;
	state->frame_interval = framerate;
	spin_unlock(&state->lock);

	dev_dbg(state->dev, "detected properties : width %d height %d\n",
		bt->width, bt->height);

	return 0;
}

static void xdprxss_set_training_params(struct xdprxss_state *xdprxss)
{
	unsigned int offset;

	/*
	 * This register is used to set a minimum value which must be met
	 * As per the Display Port protocol.The internal logic forces training
	 * to fail until this value is met.Please refer to PG 300
	 * https://www.xilinx.com/support/documentation/ip_documentation/dp_rx_subsystem/v2_1/pg233-displayport-rx-subsystem.pdf
	 */
	xdprxss_write(xdprxss, XDPRX_MINVOLT_SWING_REG, XDPRX_MIN_VS_MASK);
	xdprxss_write(xdprxss, XDPRX_AUX_CLKDIV_REG,
		      xdprxss_read(xdprxss, XDPRX_AUX_CLKDIV_REG) |
		      FIELD_PREP(XDPRX_AUX_DEFER_MASK, XDPRX_AUX_DEFER_COUNT));

	xdprxss_dpcd_update_start(xdprxss);
	xdprxss_dpcd_update(xdprxss, XDPRX_TP_SET_REG,
			    (XDPRX_AUX_RDINT_16MS << XDPRX_AUX_RDINT_SHIFT) |
			    XDPRX_AUX_READINTRVL_REG);
	xdprxss_dpcd_update_end(xdprxss);

	xdprxss_clr(xdprxss, XDPRX_INTR_MASK_REG, XDPRX_INTR_ALL_MASK);

	/* Load edid data to EDID memory block */
	for (offset = 0; offset < XDPRX_EDID_LENGTH; offset = offset + 4) {
		iowrite32((uint32_t)xilinx_edid[offset / 4],
			  xdprxss->edid_base + offset);
	}
	xdprxss_write(xdprxss, XDPRX_LOCAL_EDID_REG, 0x1);

	/* Disable all the interrupts */
	xdprxss_set(xdprxss, XDPRX_INTR_MASK_REG, XDPRX_INTR_ALL_MASK);

	/* Enable trainng related interrupts */
	xdprxss_clr(xdprxss, XDPRX_INTR_MASK_REG, XDPRX_INTR_TRNG_MASK);
	xdprxss_write(xdprxss, XDPRX_AUX_CLKDIV_REG,
		      xdprxss_read(xdprxss, XDPRX_AUX_CLKDIV_REG) |
		      FIELD_PREP(XDPRX_AUX_DEFER_MASK, XDPRX_AUX_DEFER_COUNT));
	xdprxss_write(xdprxss, XDPRX_BSIDLE_TIME_REG, XDPRX_BSIDLE_TMOUT_VAL);
	xdprxss_clr(xdprxss, XDPRX_CRC_CONFIG_REG, XDPRX_CRC_EN_MASK);
	xdprxss_write(xdprxss, XDPRX_LINK_ENABLE_REG, 0x1);
}

static void xdprxss_core_init(struct xdprxss_state *xdprxss)
{
	unsigned long axi_clk;

	u32 max_lanecount = xdprxss->max_lanecount;

	xdprxss_dpcd_update_start(xdprxss);
	xdprxss_dpcd_update(xdprxss,
			    XDPRX_VRD_BWSET_REG, xdprxss->max_linkrate);
	max_lanecount |= (XDPRX_EFRAME_CAP_MASK | XDPRX_LNCNT_TPS3_MASK);
	xdprxss_dpcd_update(xdprxss, XDPRX_LANE_CNT_REG, max_lanecount);
	xdprxss_dpcd_update_end(xdprxss);
	xdprxss_write(xdprxss, XDPRX_LINK_ENABLE_REG, 0x0);
	axi_clk = clk_get_rate(xdprxss->axi_clk);
	xdprxss_write(xdprxss, XDPRX_AUX_CLKDIV_REG, axi_clk / MHZ);
	/* Put both GT RX/TX and CPLL into reset */
	xdprxss_write(xdprxss, XDPRX_PHY_REG, XDPRX_PHY_GTPLLRST_MASK |
		      XDPRX_PHY_GTRXRST_MASK);
	/* Release CPLL reset */
	xdprxss_write(xdprxss, XDPRX_PHY_REG, XDPRX_PHY_GTRXRST_MASK);
	/*
	 * Remove the reset from the PHY and configure to issue reset after
	 * every training iteration, link rate change, and start of training
	 * pattern
	 */
	xdprxss_write(xdprxss, XDPRX_PHY_REG,
		      XDPRX_PHYRST_ENBL_MASK |
		      XDPRX_PHYRST_TRITER_MASK |
		      XDPRX_PHYRST_RATECHANGE_MASK |
		      XDPRX_PHYRST_TP1START_MASK);
	xdprxss_write(xdprxss, XDPRX_MST_CAP_REG, 0x0);
	xdprxss_write(xdprxss, XDPRX_SINK_COUNT_REG, 1);
	xdprxss_set_training_params(xdprxss);
}

static void xdprxss_irq_unplug(struct xdprxss_state *state)
{
	dev_dbg(state->dev, "Asserted cable unplug interrupt\n");

	xdprxss_set(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_VIDRST_MASK);
	xdprxss_clr(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_VIDRST_MASK);

	xdprxss_set(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_ALL_MASK);
	xdprxss_clr(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_TRNG_MASK);
	/*
	 * In a scenario, where the cable is plugged-in but the training
	 * is lost, the software is expected to assert a HPD upon the
	 * occurrence of a TRAINING_LOST interrupt, so that the Source
	 * can retrain the link.
	 */
	xdprxss_write(state, XDPRX_HPD_INTR_REG,
		      FIELD_PREP(XDPRX_HPD_PULSE_MASK, XDPRX_HPD_PLUSE_5000) |
		      XDPRX_HPD_INTR_MASK);
}

static void xdprxss_irq_tp1(struct xdprxss_state *state)
{
	union phy_configure_opts phy_opts = { 0 };
	struct phy_configure_opts_dp *phy_cfg = &phy_opts.dp;
	u32 linkrate;
	unsigned int i;

	dev_dbg(state->dev, "Asserted traning pattern 1\n");

	linkrate = xdprxss_read(state, XDPRX_LINK_BW_REG);

	switch (linkrate) {
	case DP_LINK_BW_1_62:
	case DP_LINK_BW_2_7:
	case DP_LINK_BW_5_4:
	case DP_LINK_BW_8_1:
		phy_cfg->link_rate = linkrate * 270;
		break;
	default:
		dev_err(state->dev, "invalid link rate\n");
		break;
	}
	phy_cfg->set_rate = 1;
	for (i = 0; i < state->max_lanecount; i++)
		phy_configure(state->phy[i], &phy_opts);

	/* Initialize phy logic of DP-RX core */
	xdprxss_write(state, XDPRX_PHY_REG, XDPRX_PHY_INIT_MASK);
	phy_reset(state->phy[0]);
	xdprxss_clr(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_ALL_MASK);
}

static void xdprxss_training_failure(struct xdprxss_state *state)
{
	dev_dbg(state->dev, "Traning Lost !!\n");
	state->valid_stream = false;

	xdprxss_write(state, XDPRX_HPD_INTR_REG,
		      FIELD_PREP(XDPRX_HPD_PULSE_MASK, XDPRX_HPD_PLUSE_750) |
		      XDPRX_HPD_INTR_MASK);

	/* reset the aux logic */
	xdprxss_set(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_AUXRST_MASK);
	xdprxss_clr(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_AUXRST_MASK);
}

static void xdprxss_irq_no_video(struct xdprxss_state *state)
{
	dev_dbg(state->dev, "No Valid video received !!\n");

	xdprxss_write(state, XDPRX_VIDEO_UNSUPPORTED_REG, 0x1);
	xdprxss_clr(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_VBLANK_MASK);
	xdprxss_set(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_NOVID_MASK);
	/* reset the dtg core */
	xdprxss_set(state, XDPRX_DTG_REG, 0x0);
	xdprxss_set(state, XDPRX_DTG_REG, 0x1);

	/* reset the video logic */
	xdprxss_set(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_VIDRST_MASK);
	xdprxss_clr(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_VIDRST_MASK);

	/* notify source change event */
	memset(&state->event, 0, sizeof(state->event));
	state->event.type = V4L2_EVENT_SOURCE_CHANGE;
	state->event.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION;
	v4l2_subdev_notify_event(&state->subdev, &state->event);
	state->valid_stream = false;
}

static void xdprxss_irq_valid_video(struct xdprxss_state *state)
{
	dev_dbg(state->dev, "Valid Video received !!\n");
	xdprxss_write(state, XDPRX_VIDEO_UNSUPPORTED_REG, 0x0);

	if (!xdprxss_get_stream_properties(state)) {
		memset(&state->event, 0, sizeof(state->event));
		state->event.type = V4L2_EVENT_SOURCE_CHANGE;
		state->event.u.src_change.changes =
				V4L2_EVENT_SRC_CH_RESOLUTION;
		v4l2_subdev_notify_event(&state->subdev, &state->event);
		state->valid_stream = true;
	} else {
		dev_err(state->dev, "Unable to get stream properties!\n");
		state->valid_stream = false;
	}
}

static void xdprxss_irq_audio_detected(struct xdprxss_state *state)
{
	u32 buff[INFO_PCKT_SIZE_WORDS];
	u8 *buf_ptr;
	int i;

	for (i = 0; i < INFO_PCKT_SIZE_WORDS; i++)
		buff[i] = xdprxss_read(state, XDPRX_AUDIO_INFO_DATA);

	buf_ptr = (u8 *)buff;
	memcpy(state->rx_audio_data->infoframe, buff, INFO_PCKT_SIZE);

	if (buf_ptr[1] == INFO_PCKT_TYPE_AUDIO)
		state->rx_audio_data->audio_detected = true;
}

static irqreturn_t xdprxss_irq_handler(int irq, void *dev_id)
{
	struct xdprxss_state *state = (struct xdprxss_state *)dev_id;
	u32 status;

	status = xdprxss_read(state, XDPRX_INTR_CAUSE_REG);
	status &= ~xdprxss_read(state, XDPRX_INTR_MASK_REG);

	if (!status)
		return IRQ_NONE;

	if (status & XDPRX_INTR_UNPLUG_MASK)
		xdprxss_irq_unplug(state);
	if (status & XDPRX_INTR_TP1_MASK)
		xdprxss_irq_tp1(state);
	if (status & XDPRX_INTR_TRLOST_MASK)
		xdprxss_training_failure(state);
	if (status & XDPRX_INTR_NOVID_MASK)
		xdprxss_irq_no_video(state);
	if (status & XDPRX_INTR_VID_MASK)
		xdprxss_irq_valid_video(state);
	if (status & XDPRX_INTR_AUDIO_MASK)
		xdprxss_irq_audio_detected(state);
#ifdef DEBUG
	if (status & XDPRX_INTR_TRDONE_MASK)
		dev_dbg(state->dev, "DP Link training is done !!\n");
#endif

	return IRQ_HANDLED;
}

/**
 * xdprxss_subscribe_event - Subscribe to video source change event
 * @sd: V4L2 Sub device
 * @fh: V4L2 File Handle
 * @sub: Subcribe event structure
 *
 * Return: 0 on success, errors otherwise
 */
static int xdprxss_subscribe_event(struct v4l2_subdev *sd,
				   struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	int ret;
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);

	dev_dbg(xdprxss->dev, "Event subscribed : 0x%08x\n", sub->type);

	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		ret = v4l2_src_change_event_subscribe(fh, sub);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int xdprxss_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);

	/* DP does not need to be enabled when we start streaming */
	if (enable == xdprxss->streaming)
		return 0;

	if (enable && !xdprxss->valid_stream)
		return -EINVAL;

	xdprxss->streaming = enable;

	return 0;
}

/**
 * xdprxss_g_input_status - It is used to determine if the video signal
 * is present / locked onto or not.
 *
 * @sd: V4L2 Sub device
 * @status: status of signal locked
 *
 * This is used to determine if the valid video signal is present and
 * locked onto by the DP Rx xdprxss or not .
 *
 * Return: zero on success
 */
static int xdprxss_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);

	if (!xdprxss->valid_stream)
		*status = V4L2_IN_ST_NO_SYNC | V4L2_IN_ST_NO_SIGNAL;
	else
		*status = 0;

	return 0;
}

static struct v4l2_mbus_framefmt *
__xdprxss_get_pad_format(struct xdprxss_state *xdprxss,
			 struct v4l2_subdev_pad_config *cfg,
			 unsigned int pad, u32 which)
{
	struct v4l2_mbus_framefmt *format;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		format = v4l2_subdev_get_try_format(&xdprxss->subdev, cfg, pad);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		format = &xdprxss->format;
		break;
	default:
		format = NULL;
		break;
	}

	return format;
}

/**
 * xdprxss_init_cfg - Initialise the pad format config to default
 * @sd: Pointer to V4L2 Sub device structure
 * @cfg: Pointer to sub device pad information structure
 *
 * This function is used to initialize the pad format with the default
 * values.
 *
 * Return: 0 on success
 */
static int xdprxss_init_cfg(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(sd, cfg, 0);

	if (!xdprxss->valid_stream)
		*format = xdprxss->format;

	return 0;
}

/**
 * xdprxss_getset_format - This is used to set and get the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @cfg: Pointer to sub device pad information structure
 * @fmt: Pointer to pad level media bus format
 *
 * This function is used to set the pad format.
 * Since the pad format is fixed in hardware, it can't be
 * modified on run time.
 *
 * Return: 0 on success
 */
static int xdprxss_getset_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
	struct v4l2_mbus_framefmt *format;

	if (!xdprxss->valid_stream) {
		dev_err(xdprxss->dev, "Video not locked!\n");
		return -EINVAL;
	}

	dev_dbg(xdprxss->dev,
		"set width %d height %d code %d field %d colorspace %d\n",
		fmt->format.width, fmt->format.height,
		fmt->format.code, fmt->format.field,
		fmt->format.colorspace);
	format = __xdprxss_get_pad_format(xdprxss, cfg,
					  fmt->pad, fmt->which);
	if (!format)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

/**
 * xdprxss_enum_mbus_code - Handle pixel format enumeration
 * @sd: pointer to v4l2 subdev structure
 * @cfg: V4L2 subdev pad configuration
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 *
 * Return: -EINVAL or zero on success
 */
static int xdprxss_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
	u32 index = code->index;
	u32 base = 0;

	if (xdprxss->bpc == 8)
		base = 0;

	if (xdprxss->bpc == 10)
		base = 3;

	if (code->pad || index >= 3)
		return -EINVAL;

	code->code = xdprxss_supported_mbus_fmts[base + index];

	return 0;
}

/**
 * xdprxss_enum_dv_timings - Enumerate all the supported DV timings
 * @sd: pointer to v4l2 subdev structure
 * @timings: DV timings structure to be returned.
 *
 * Return: -EINVAL incase of invalid index and pad or zero on success
 */
static int xdprxss_enum_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_enum_dv_timings *timings)
{
	if (timings->index >= ARRAY_SIZE(fmt_cap))
		return -EINVAL;

	if (timings->pad != 0)
		return -EINVAL;

	timings->timings = fmt_cap[timings->index];

	return 0;
}

/**
 * xdprxss_get_dv_timings_cap - This is used to set the dv timing
 * capabilities
 * @subdev: Pointer to V4L2 Sub device structure
 * @cap: Pointer to dv timing capability structure
 *
 * Return: -EINVAL incase of invalid pad or zero on success
 */
static int xdprxss_get_dv_timings_cap(struct v4l2_subdev *subdev,
				      struct v4l2_dv_timings_cap *cap)
{
	struct v4l2_dv_timings_cap xdprxss_dv_timings_cap = {
		.type = V4L2_DV_BT_656_1120,
		.reserved = { 0 },
		V4L2_INIT_BT_TIMINGS
		(800, 7680,
		600, 4320,
		25000000, 297000000,
		V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
		V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
		V4L2_DV_BT_CAP_INTERLACED | V4L2_DV_BT_CAP_PROGRESSIVE |
		V4L2_DV_BT_CAP_REDUCED_BLANKING |
		V4L2_DV_BT_CAP_CUSTOM
		)
	};

	if (cap->pad != 0)
		return -EINVAL;

	*cap = xdprxss_dv_timings_cap;

	return 0;
}

static int xdprxss_query_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_dv_timings *timings)
{
	struct xdprxss_state *state = to_xdprxssstate(sd);
	unsigned int i;

	if (!timings)
		return -EINVAL;

	if (!state->valid_stream)
		return -ENOLCK;
	for (i = 0; i < ARRAY_SIZE(xdprxss_dv_timings); i++) {
		if (state->format.width == xdprxss_dv_timings[i].width &&
		    state->format.height == xdprxss_dv_timings[i].height &&
		    state->frame_interval == xdprxss_dv_timings[i].fps) {
			*timings = xdprxss_dv_timings[i].timing;
			return 0;
		}
	}

	return -ERANGE;
}

/* ------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xdprxss_media_ops = {
	.link_validate = v4l2_subdev_link_validate
};

static const struct v4l2_subdev_core_ops xdprxss_core_ops = {
	.subscribe_event	= xdprxss_subscribe_event,
	.unsubscribe_event	= v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops xdprxss_video_ops = {
	.query_dv_timings	= xdprxss_query_dv_timings,
	.s_stream		= xdprxss_s_stream,
	.g_input_status		= xdprxss_g_input_status,
};

static const struct v4l2_subdev_pad_ops xdprxss_pad_ops = {
	.init_cfg		= xdprxss_init_cfg,
	.enum_mbus_code		= xdprxss_enum_mbus_code,
	.get_fmt		= xdprxss_getset_format,
	.set_fmt		= xdprxss_getset_format,
	.enum_dv_timings	= xdprxss_enum_dv_timings,
	.dv_timings_cap         = xdprxss_get_dv_timings_cap,
};

static const struct v4l2_subdev_ops xdprxss_ops = {
	.core	= &xdprxss_core_ops,
	.video	= &xdprxss_video_ops,
	.pad	= &xdprxss_pad_ops
};

/* ----------------------------------------------------------------
 * DP audio operation
 */
/**
 * xlnx_rx_pcm_startup - initialize audio during audio usecase
 *
 * @substream: Pointer to sound pcm substream structure
 * @dai: Pointer to sound soc dai structure
 *
 * This function is called by ALSA framework before audio
 * capture begins.
 *
 * Return: -EIO if no audio is detected or 0 on success
 */
static int xlnx_rx_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	int err;
	struct xlnx_dprx_audio_data *adata;
	unsigned long jiffies = msecs_to_jiffies(XDP_AUDIO_DETECT_TIMEOUT);
	struct xdprxss_state *xdprxss = dev_get_drvdata(dai->dev);

	adata = xdprxss->rx_audio_data;

	xdprxss_clr(xdprxss, XDPRX_AUDIO_CONTROL, XDPRX_AUDIO_EN_MASK);
	xdprxss_set(xdprxss, XDPRX_AUDIO_CONTROL, XDPRX_AUDIO_EN_MASK);

	/*
	 * TODO: Currently the audio infoframe packet interrupts are not
	 * coming for the first time without the below msleep.
	 * Need to find out the root cause and should remove this msleep
	 */
	msleep(50);

	/* Enable DP Rx audio and interruts */
	xdprxss_set(xdprxss, XDPRX_INTR_MASK_REG, XDPRX_INTR_AUDIO_MASK);

	err = wait_event_interruptible_timeout(adata->audio_update_q,
					       adata->audio_detected,
					       jiffies);
	if (!err) {
		dev_err(dai->dev, "No audio detected in input stream\n");
		return -EIO;
	}

	dev_info(dai->dev, "Detected audio, starting capture\n");

	return 0;
}

/**
 * xlnx_rx_pcm_shutdown - Deinitialze audio when audio usecase is stopped
 *
 * @substream: Pointer to sound pcm substream structure
 * @dai: Pointer to sound soc dai structure
 *
 * This function is called by ALSA framework before audio capture usecase
 * ends.
 */
static void xlnx_rx_pcm_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct xdprxss_state *xdprxss = dev_get_drvdata(dai->dev);

	xdprxss_clr(xdprxss, XDPRX_AUDIO_CONTROL, XDPRX_AUDIO_EN_MASK);
	xdprxss_clr(xdprxss, XDPRX_INTR_MASK_REG, XDPRX_INTR_AUDIO_MASK);
}

static const struct snd_soc_dai_ops xlnx_rx_dai_ops = {
	.startup = xlnx_rx_pcm_startup,
	.shutdown = xlnx_rx_pcm_shutdown,
};

static struct snd_soc_dai_driver xlnx_rx_audio_dai = {
	.name = "xlnx_dp_rx",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			 SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &xlnx_rx_dai_ops,
};

static const struct snd_soc_component_driver xlnx_rx_dummy_codec_driver;

/**
 * dprx_register_aud_dev - register audio device
 *
 * @dev: Pointer to Platform structure
 *
 * This function registers codec DAI device as part of
 * ALSA SoC framework.
 *
 * Return: 0 on success, error value otherwise
 */
static int dprx_register_aud_dev(struct device *dev)
{
	return snd_soc_register_component(dev, &xlnx_rx_dummy_codec_driver,
			&xlnx_rx_audio_dai, 1);
}

/**
 * dprx_unregister_aud_dev - register audio device
 *
 * @dev: Pointer to Platform structure
 *
 * This functions unregisters codec DAI device
 */
static void dprx_unregister_aud_dev(struct device *dev)
{
	snd_soc_unregister_component(dev);
}

/* ----------------------------------------------------------------
 * Platform Device Driver
 */
static int xdprxss_parse_of(struct xdprxss_state *xdprxss)
{
	struct device_node *node = xdprxss->dev->of_node;
	u32 val = 0;
	int ret;

	ret = of_property_read_u32(node, "xlnx,bpc", &xdprxss->bpc);
	if (ret < 0) {
		if (ret != -EINVAL) {
			dev_err(xdprxss->dev, "failed to get xlnx,bpp\n");
			return ret;
		}
	}
	/*
	 * TODO : For now driver supports only 8, 10 bpc.
	 * In future, driver may add with other bpc support
	 */
	if (xdprxss->bpc != 8 && xdprxss->bpc != 10) {
		dev_err(xdprxss->dev, "unsupported bpc = %u\n", xdprxss->bpc);
		return -EINVAL;
	}

	xdprxss->hdcp_enable = of_property_read_bool(node, "xlnx,hdcp-enable");
	/* TODO : This driver does not support HDCP feature */
	if (xdprxss->hdcp_enable) {
		dev_err(xdprxss->dev, "hdcp unsupported\n");
		return -EINVAL;
	}

	xdprxss->audio_enable = of_property_read_bool(node,
						      "xlnx,audio-enable");
	if (!xdprxss->audio_enable)
		dev_info(xdprxss->dev, "audio not enabled\n");

	ret = of_property_read_u32(node, "xlnx,link-rate", &val);
	if (ret < 0) {
		dev_err(xdprxss->dev, "xlnx,link-rate property not found\n");
		return ret;
	}
	if (!(val == DP_LINK_BW_1_62 ||
	      val == DP_LINK_BW_2_7 ||
	      val == DP_LINK_BW_5_4 ||
	      val == DP_LINK_BW_8_1)) {
		dev_err(xdprxss->dev, "invalid link rate\n");
		return -EINVAL;
	}
	xdprxss->max_linkrate = val;

	ret = of_property_read_u32(node, "xlnx,lane-count", &val);
	if (ret < 0) {
		dev_err(xdprxss->dev, "xlnx,lane-count property not found\n");
		return ret;
	}
	if (val < 1 && val > 4) {
		dev_err(xdprxss->dev, "invalid lane count\n");
		return -EINVAL;
	}
	xdprxss->max_lanecount = val;

	ret = of_property_read_u32(node, "xlnx,mode", &val);
	if (ret < 0) {
		dev_err(xdprxss->dev, "xlnx,mode property not found\n");
		return ret;
	}
	if (val > 0) {
		dev_err(xdprxss->dev, "driver does't support MST mode\n");
		return -EINVAL;
	}

	return 0;
}

static int xdprxss_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xdprxss_state *xdprxss;
	struct device_node *node;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret, irq;
	unsigned int i = 0, j;
	struct xlnx_dprx_audio_data *adata;

	xdprxss = devm_kzalloc(dev, sizeof(*xdprxss), GFP_KERNEL);
	if (!xdprxss)
		return -ENOMEM;

	xdprxss->dev = &pdev->dev;
	node = xdprxss->dev->of_node;

	xdprxss->rx_audio_data =
		devm_kzalloc(&pdev->dev, sizeof(struct xlnx_dprx_audio_data),
			     GFP_KERNEL);
	if (!xdprxss->rx_audio_data)
		return -ENOMEM;

	adata = xdprxss->rx_audio_data;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dp_base");
	xdprxss->dp_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(xdprxss->dp_base)) {
		dev_err(dev, "couldn't map DisplayPort registers\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "edid_base");
	xdprxss->edid_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(xdprxss->edid_base)) {
		dev_err(dev, "Couldn't map EDID IP memory\n");
		return -ENOENT;
	}

	xdprxss->axi_clk = devm_clk_get(dev, "s_axi_aclk");
	if (IS_ERR(xdprxss->axi_clk)) {
		ret = PTR_ERR(xdprxss->axi_clk);
		dev_err(&pdev->dev, "failed to get s_axi_clk (%d)\n", ret);
		return ret;
	}

	xdprxss->rx_lnk_clk = devm_clk_get(dev, "rx_lnk_clk");
	if (IS_ERR(xdprxss->rx_lnk_clk)) {
		ret = PTR_ERR(xdprxss->rx_lnk_clk);
		dev_err(&pdev->dev, "failed to get rx_lnk_clk (%d)\n", ret);
		return ret;
	}

	xdprxss->rx_vid_clk = devm_clk_get(dev, "rx_vid_clk");
	if (IS_ERR(xdprxss->rx_vid_clk)) {
		ret = PTR_ERR(xdprxss->rx_vid_clk);
		dev_err(&pdev->dev, "failed to get rx_vid_clk (%d)\n", ret);
		return ret;
	}

	ret = xdprxss_parse_of(xdprxss);
	if (ret < 0)
		goto clk_err;

	/* acquire vphy lanes */
	for (i = 0; i < xdprxss->max_lanecount; i++) {
		char phy_name[16];

		snprintf(phy_name, sizeof(phy_name), "dp-phy%d", i);
		xdprxss->phy[i] = devm_phy_get(xdprxss->dev, phy_name);
		if (IS_ERR(xdprxss->phy[i])) {
			ret = PTR_ERR(xdprxss->phy[i]);
			xdprxss->phy[i] = NULL;
			if (ret == -EPROBE_DEFER)
				dev_info(dev, "phy not ready -EPROBE_DEFER\n");
			if (ret != -EPROBE_DEFER)
				dev_err(dev,
					"failed to get phy lane %s i %d\n",
					phy_name, i);
			goto error_phy;
		}
		ret = phy_init(xdprxss->phy[i]);
		if (ret) {
			dev_err(dev,
				"failed to init phy lane %d\n", i);
			goto error_phy;
		}
	}

	ret = clk_prepare_enable(xdprxss->axi_clk);
	if (ret) {
		dev_err(dev, "failed to enable axi_clk (%d)\n", ret);
		goto error_phy;
	}

	ret = clk_prepare_enable(xdprxss->rx_lnk_clk);
	if (ret) {
		dev_err(dev, "failed to enable rx_lnk_clk (%d)\n", ret);
		goto rx_lnk_clk_err;
	}

	ret = clk_prepare_enable(xdprxss->rx_vid_clk);
	if (ret) {
		dev_err(dev, "failed to enable rx_vid_clk (%d)\n", ret);
		goto rx_vid_clk_err;
	}

	spin_lock_init(&xdprxss->lock);

	/* Initialize the DP core */
	xdprxss_core_init(xdprxss);

	/* Initialize V4L2 subdevice and media entity */
	xdprxss->pad.flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xdprxss->subdev;
	v4l2_subdev_init(subdev, &xdprxss_ops);
	subdev->dev = &pdev->dev;
	strscpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));

	subdev->flags = V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;

	subdev->entity.ops = &xdprxss_media_ops;

	v4l2_set_subdevdata(subdev, xdprxss);
	ret = media_entity_pads_init(&subdev->entity, 1, &xdprxss->pad);
	if (ret < 0)
		goto error;

	/* Register interrupt handler */
	irq = irq_of_parse_and_map(node, 0);
	ret = devm_request_irq(xdprxss->dev, irq, xdprxss_irq_handler,
			       IRQF_SHARED, subdev->name, xdprxss);
	if (ret) {
		dev_err(dev, "Err = %d Interrupt handler reg failed!\n",
			ret);
		goto error;
	}

	platform_set_drvdata(pdev, xdprxss);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(dev, "failed to register subdev\n");
		goto error;
	}

	if (xdprxss->audio_enable) {
		ret = dprx_register_aud_dev(xdprxss->dev);
		if (ret < 0) {
			xdprxss->audio_init = false;
			dev_err(xdprxss->dev, "dp rx audio init failed\n");
			goto error;
		} else {
			xdprxss->audio_init = true;
			init_waitqueue_head(&adata->audio_update_q);
			dev_info(xdprxss->dev, "dp rx audio initialized\n");
		}
	}

	return 0;

error:
	media_entity_cleanup(&subdev->entity);
clk_err:
	clk_disable_unprepare(xdprxss->rx_vid_clk);
rx_vid_clk_err:
	clk_disable_unprepare(xdprxss->rx_lnk_clk);
rx_lnk_clk_err:
	clk_disable_unprepare(xdprxss->axi_clk);
error_phy:
	dev_dbg(dev, " %s error_phy:\n", __func__);
	/* release the lanes that we did get, if we did not get all lanes */
	for (j = 0; j < i; j++) {
		if (xdprxss->phy[j]) {
			dev_dbg(dev,
				"phy_exit() xdprxss->phy[%d] = %p\n",
				j, xdprxss->phy[j]);
			phy_exit(xdprxss->phy[j]);
			xdprxss->phy[j] = NULL;
		}
	}

	return ret;
}

static int xdprxss_remove(struct platform_device *pdev)
{
	struct xdprxss_state *xdprxss = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xdprxss->subdev;
	unsigned int i;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	clk_disable_unprepare(xdprxss->rx_vid_clk);
	clk_disable_unprepare(xdprxss->rx_lnk_clk);
	clk_disable_unprepare(xdprxss->axi_clk);
	for (i = 0; i < XDPRX_MAX_LANE_COUNT; i++) {
		phy_exit(xdprxss->phy[i]);
		xdprxss->phy[i] = NULL;
	}

	if (xdprxss->audio_init)
		dprx_unregister_aud_dev(&pdev->dev);

	return 0;
}

static const struct of_device_id xdprxss_of_id_table[] = {
	{ .compatible = "xlnx,v-dp-rxss-3.0", },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, xdprxss_of_id_table);

static struct platform_driver xdprxss_driver = {
	.driver = {
		.name		= "xilinx-dprxss",
		.of_match_table	= xdprxss_of_id_table,
	},
	.probe			= xdprxss_probe,
	.remove			= xdprxss_remove,
};

module_platform_driver(xdprxss_driver);

MODULE_AUTHOR("Rajesh Gugulothu <gugulothu.rajesh@xilinx.com");
MODULE_DESCRIPTION("Xilinx DP Rx Subsystem Driver");
MODULE_LICENSE("GPL v2");

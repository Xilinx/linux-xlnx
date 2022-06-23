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
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/v4l2-subdev.h>

#include <drm/drm_dp_helper.h>
#include <dt-bindings/media/xilinx-vip.h>

#include <media/hdr-ctrls.h>
#include <media/v4l2-ctrls.h>
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
/* Refer section 2.2.5.1.2 in DP spec and table 42 in CTA-861-G spec */
#define INFO_PCKT_TYPE_DRM		0x87

/* DP Rx subsysetm register map, bitmask, and offsets. */
#define XDPRX_LINK_ENABLE_REG		0x000
#define XDPRX_AUX_CLKDIV_REG		0x004
#define XDPRX_AUX_DEFER_COUNT		6
#define XDPRX_AUX_DEFER_SHIFT		24
#define XDPRX_AUX_DEFER_MASK		GENMASK(27, 24)

#define XDPRX_LINERST_DIS_REG		0x008
#define XDPRX_DTG_REG			0x00c
#define XDPRX_DTG_DIS_MASK		BIT(0)
#define XDPRX_VSCEXT_VESA_SDP_SUPPORTED	BIT(2)

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
#define XDPRX_INTR_TP2_MASK		BIT(17)
#define XDPRX_INTR_TP3_MASK		BIT(18)
#define XDPRX_INTR_LINKQUAL_MASK	BIT(29)
#define XDPRX_INTR_UNPLUG_MASK		BIT(31)
#define XDPRX_INTR_CRCTST_MASK		BIT(30)
#define XDPRX_INTR_TRNG_MASK		(XDPRX_INTR_TP1_MASK | \
					 XDPRX_INTR_TP2_MASK |\
					 XDPRX_INTR_TP3_MASK | \
					 XDPRX_INTR_POWER_MASK |\
					 XDPRX_INTR_CRCTST_MASK |\
					 XDPRX_INTR_BWCHANGE_MASK)
#define XDPRX_INTR_ACCESS_LANE_SET_MASK		BIT(30)
#define XDPRX_INTR_TP4_MASK			BIT(31)
#define XDPRX_INTR_ACCESS_LINK_QUAL_MASK	BIT(29)
#define XDPRX_INTR_ACCESS_ERR_CNT_MASK		BIT(28)
#define XDPRX_INTR_TRNG_MASK_1		(XDPRX_INTR_TP4_MASK | \
					 XDPRX_INTR_ACCESS_LANE_SET_MASK | \
					 XDPRX_INTR_ACCESS_LINK_QUAL_MASK | \
					 XDPRX_INTR_ACCESS_ERR_CNT_MASK)
#define XDPRX_INTR_ALL_MASK		0xffffffff
#define XDPRX_INTR_ALL_MASK_1		0xffffffff

#define XDPRX_SOFT_RST_REG		0x01c
#define XDPRX_SOFT_VIDRST_MASK		BIT(0)
#define XDPRX_SOFT_AUXRST_MASK		BIT(7)

#define XDPRX_HPD_INTR_REG		0x02c
#define XDPRX_HPD_INTR_MASK		BIT(1)
#define XDPRX_HPD_PULSE_MASK		GENMASK(31, 16)

#define XDPRX_INTR_CAUSE_REG		0x040
#define XDPRX_INTR_MASK_1_REG		0x044
#define XDPRX_INTR_CAUSE_1_REG		0x048
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

#define XDPRX_PHYSTATUS_REG			0x208
#define XDPRX_PHYSTATUS_ALL_LANES_GOOD_MASK	GENMASK(6, 0)
#define XDPRX_PHYSTATUS_READ_COUNT	100

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
/* default CDR tDLOCK calibration value */
#define XDPRX_CDRCTRL_TDLOCK_VAL	0x1388
#define XDPRX_CDRCTRL_TDLOCK_MASK	GENMASK(19, 0)
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
#define XDPRX_DPCD_TRAINING_PATTERN_SET	0x40c
#define XDPRX_DPCD_LANE01_STATUS	0x43c
#define XDPRX_LANE01_PEVS_MASK		GENMASK(15, 8)
#define XDPRX_DPC_LINK_QUAL_CONFIG	0x454
#define XDPRX_DPCD_LINK_QUAL_PRBS_MASK	GENMASK(1, 0)
#define XDPRX_LINK_QUAL_PRBS_MODE_MASK	GENMASK(2, 0)
#define XDPRX_MSA_HRES_REG		0x500
#define XDPRX_MSA_HSPOL_REG		0x504
#define XDPRX_MSA_HSPOL_MASK		BIT(0)
#define XDPRX_MSA_HSWIDTH_REG		0x508
#define XDPRX_MSA_HSTART_REG		0x50c
#define XDPRX_MSA_VHEIGHT_REG		0x514
#define XDPRX_MSA_HTOTAL_REG		0x510
#define XDPRX_MSA_VSPOL_REG		0x518
#define XDPRX_MSA_VSPOL_MASK		BIT(0)
#define XDPRX_MSA_VSWIDTH_REG		0x51c
#define XDPRX_MSA_VSTART_REG		0x520
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

#define XDPRX_EXT_VRD_BWSET_REG		0x7f0

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
#define XDPRX_HPD_PULSE_5000		5000
/*
 * low going IRQ_HPD generated for upstream device
 * as per the VESA standard
 */
#define XDPRX_HPD_PULSE_750		750

/* GtCtrl Registers */
#define XDPRX_GTCTL_REG			0x4C
#define XDPRX_GTCTL_EN			BIT(0)
#define XDPRX_GTCTL_VSWING_MASK		GENMASK(12, 8)
#define XDPRX_GTCTL_VSWING_INIT_VAL	0x05
#define XDPRX_GTCTL_LINE_RATE_MASK	GENMASK(2, 1)
#define XDPRX_GTCTL_LINE_RATE_810G	3
#define XDPRX_GTCTL_LINE_RATE_540G	2
#define XDPRX_GTCTL_LINE_RATE_270G	1
#define XDPRX_GTCTL_LINE_RATE_162G	0

#define DP_LINK_BW_1_62G	1620
#define DP_LINK_BW_2_7G		2700
#define DP_LINK_BW_5_4G		5400    /* 1.2 */
#define DP_LINK_BW_8_1G		8100    /* 1.4 */

#define XDPRXSS_MMCM_OFFSET		0x5000

/* Clock Wizard registers */
#define XDPRX_MMCM_SWRST_OFFSET		0x00000000
#define XDPRX_MMCM_SWRST_VAL		0xA
#define XDPRX_MMCM_STATUS_OFFSET	0x00000004
#define XDPRX_MMCM_ISR_OFFSET		0x0000000C
#define XDPRX_MMCM_IER_OFFSET		0x00000010
#define XDPRX_MMCM_RECONFIG_OFFSET	0x00000014
#define XDPRX_MMCM_REG1_OFFSET		0x00000330
#define XDPRX_MMCM_REG2_OFFSET		0x00000334
#define XDPRX_MMCM_REG3_OFFSET		0x00000338
#define XDPRX_MMCM_REG4_OFFSET		0x0000033C
#define XDPRX_MMCM_REG12_OFFSET		0x00000380
#define XDPRX_MMCM_REG13_OFFSET		0x00000384
#define XDPRX_MMCM_REG11_OFFSET		0x00000378
#define XDPRX_MMCM_REG11_VAL		0x2e
#define XDPRX_MMCM_REG14_OFFSET		0x00000398
#define XDPRX_MMCM_REG14_VAL		0xe80
#define XDPRX_MMCM_REG15_OFFSET		0x0000039C
#define XDPRX_MMCM_REG15_VAL		0x4271
#define XDPRX_MMCM_REG16_OFFSET		0x000003A0
#define XDPRX_MMCM_REG16_VAL		0x43e9
#define XDPRX_MMCM_REG17_OFFSET		0x000003A8
#define XDPRX_MMCM_REG17_VAL		0x1c
#define XDPRX_MMCM_REG19_OFFSET		0x000003CC
#define XDPRX_MMCM_REG25_OFFSET		0x000003F0
#define XDPRX_MMCM_REG26_OFFSET		0x000003FC
#define XDPRX_MMCM_REG26_VAL		1

#define XDPRX_MMCM_LOCK			BIT(0)
#define XDPRX_MMCM_REG3_PREDIV2		BIT(11)
#define XDPRX_MMCM_REG3_USED		BIT(12)
#define XDPRX_MMCM_REG3_MX		BIT(9)
#define XDPRX_MMCM_REG1_PREDIV2		BIT(12)
#define XDPRX_MMCM_REG1_EN		BIT(9)
#define XDPRX_MMCM_REG1_MX		BIT(10)
#define XDPRX_MMCM_RECONFIG_LOAD	BIT(0)
#define XDPRX_MMCM_RECONFIG_SADDR	BIT(1)
#define XDPRX_MMCM_REG1_EDGE_MASK	BIT(8)

#define XDPRX_MMCM_CLKOUT0_PREDIV2_SHIFT	11
#define XDPRX_MMCM_CLKOUT0_MX_SHIFT		9
#define XDPRX_MMCM_CLKOUT0_P5EN_SHIFT		13
#define XDPRX_MMCM_CLKOUT0_P5FEDGE_SHIFT	15
#define XDPRX_MMCM_REG12_EDGE_SHIFT		10

#define XDPRX_MMCM_M_VAL_405		28
#define XDPRX_MMCM_M_VAL_270		44
#define XDPRX_MMCM_M_VAL_135		88
#define XDPRX_MMCM_M_VAL_81		148
#define XDPRX_MMCM_D_VAL		5
#define XDPRX_MMCM_M_O_VAL_RATIO	4
#define XDPRX_MMCM_STATUS_RETRY		10000

#define MMCM_O_VAL_FEDGE_DIVIDER	2
#define MMCM_O_VAL_HIGHTIME_DIVIDER	4
#define MMCM_O_VAL_EDGE_DIVIDER		4
#define MMCM_D_VAL_EDGE_DIVIDER		2
#define MMCM_D_VAL_HIGHTIME_DIVIDER	2
#define MMCM_M_VAL_EDGE_DIVIDER		2
#define MMCM_M_VAL_HIGHTIME_DIVIDER	2
#define MMCM_MDO_VAL_HIGHTIME_SHIFT	8

#define xdprxss_generate_hpd_intr(state, duration) \
		xdprxss_write(state, XDPRX_HPD_INTR_REG, \
			      FIELD_PREP(XDPRX_HPD_PULSE_MASK, duration) |\
			      XDPRX_HPD_INTR_MASK)
#define xdprxss_disable_unplug_intr(state) \
		xdprxss_set(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_UNPLUG_MASK)
#define xdprxss_disable_audio(state) \
		xdprxss_clr(state, XDPRX_AUDIO_CONTROL, XDPRX_AUDIO_EN_MASK)
#define xdprxss_enable_audio(state) \
		xdprxss_set(state, XDPRX_AUDIO_CONTROL, XDPRX_AUDIO_EN_MASK)
#define xdprxss_dtg_enable(state)	xdprxss_set(state, XDPRX_DTG_REG, 1)
#define xdprxss_update_ext_rcv_cap(xdprxss, max_linkrate) \
		xdprxss_write(xdprxss, \
			      XDPRX_EXT_VRD_BWSET_REG, max_linkrate)
#define xdprxss_set_clk_data_recovery_timeout_val(xdprxss, value) \
		xdprxss_write(xdprxss, XDPRX_CDRCTRL_CFG_REG, \
				FIELD_PREP(XDPRX_CDRCTRL_TDLOCK_MASK, value))
#define xdprxss_enable_training_timeout(xdprxss) \
		xdprxss_clr(xdprxss, XDPRX_CDRCTRL_CFG_REG, \
			    XDPRX_CDRCTRL_DIS_TIMEOUT)
#define xdprxss_enable_training_intr(xdprxss) \
		xdprxss_clr(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_TRNG_MASK)
#define xdprxss_enable_training_intr_1(state) \
		xdprxss_clr(state, XDPRX_INTR_MASK_1_REG, XDPRX_INTR_TRNG_MASK_1)
#define xdprxss_disable_allintr(state) \
		xdprxss_set(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_ALL_MASK)
#define xdprxss_disable_allintr_1(state) \
		xdprxss_set(state, XDPRX_INTR_MASK_1_REG, XDPRX_INTR_ALL_MASK_1)
#define xdprxss_enable_audio_intr(state) \
		xdprxss_clr(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_AUDIO_MASK)

union xdprxss_iframe_header {
	u32 data;
	u8 byte[4];
};

union xdprxss_iframe_payload {
	u32 data[8];
	u8 byte[32];
};

struct xdprxss_infoframe {
	union xdprxss_iframe_header header;
	union xdprxss_iframe_payload payload;
};

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
 * struct retimer_cfg - Retimer configuration structure
 * @retimer_access_laneset: Function pointer to retimer access laneset function
 * @retimer_rst_cr_path: Function pointer to retimer reset cr path function
 * @retimer_rst_dp_path: Function pointer to retimer reset dp path function
 * @retimer_prbs_mode: Function pointer to prbs mode enable/disable function
 */
struct retimer_cfg {
	void (*retimer_access_laneset)(void);
	void (*retimer_rst_cr_path)(void);
	void (*retimer_rst_dp_path)(void);
	void (*retimer_prbs_mode)(u8 enable);
};

/**
 * struct vidphy_cfg - Video phy configuration structure
 * @vidphy_prbs_mode: Function pointer to prbs mode enable/disable function
 */
struct vidphy_cfg {
	void (*vidphy_prbs_mode)(u8 enable);
};

/**
 * struct xdprxss_state - DP Rx Subsystem device structure
 * @dev: Platform structure
 * @subdev: The v4l2 subdev structure
 * @ctrl_handler: control handler
 * @drm_infoframe: DRM infoframe data
 * @infoframe: IP infoframe data
 * @event: Holds the video unlock event
 * @detected_timings: Detected Video timings
 * @phy: pointer to phy instance
 * @pad: media pad
 * @axi_clk: Axi lite interface clock
 * @rx_lnk_clk: DP Rx GT clock
 * @rx_vid_clk: DP RX Video clock
 * @rx_dec_clk: DP Rx Decode clock
 * @dp_base: Base address of DP Rx Subsystem
 * @edid_base: Bare Address of EDID block
 * @prvdata: Pointer to device private data
 * @retimer_prvdata: Pointer to retimer private data structure
 * @vidphy_prvdata: Pointer to video phy private data structure
 * @tp1_work: training pattern 1 worker
 * @unplug_work: Unplug worker
 * @lock: Lock is used for width, height, framerate variables
 * @format: Active V4L2 format on each pad
 * @frame_interval: Captures the frame rate
 * @max_linkrate: Maximum supported link rate
 * @max_lanecount: Maximux supported lane count
 * @bpc: Bits per component
 * @ce_req_val: Variable for storing channel status
 * @versal_gt_present: flag to indicate versal-gt property in device tree
 * @hdcp_enable: To indicate hdcp enabled or not
 * @audio_enable: To indicate audio enabled or not
 * @audio_init: flag to indicate audio is initialized
 * @rx_audio_data: audio data
 * @valid_stream: To indicate valid video
 * @streaming: Flag for storing streaming state
 * @ltstate: Flag for storing link training state
 * This structure contains the device driver related parameters
 */
struct xdprxss_state {
	struct device *dev;
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_hdr10_payload drm_infoframe;
	struct xdprxss_infoframe infoframe;
	struct v4l2_event event;
	struct v4l2_dv_timings detected_timings;
	struct phy *phy[XDPRX_MAX_LANE_COUNT];
	struct media_pad pad;
	struct clk *axi_clk;
	struct clk *rx_lnk_clk;
	struct clk *rx_vid_clk;
	void __iomem *dp_base;
	void __iomem *edid_base;
	void *prvdata;
	struct retimer_cfg *retimer_prvdata;
	struct vidphy_cfg *vidphy_prvdata;
	struct delayed_work tp1_work;
	struct delayed_work unplug_work;
	/* protects width, height, framerate variables */
	spinlock_t lock;
	struct v4l2_mbus_framefmt format;
	unsigned int frame_interval;
	u32 max_linkrate;
	u32 max_lanecount;
	u32 bpc;
	u32 ce_req_val;
	bool versal_gt_present;
	bool hdcp_enable;
	bool audio_enable;
	bool audio_init;
	struct xlnx_dprx_audio_data *rx_audio_data;
	unsigned int valid_stream : 1;
	unsigned int streaming : 1;
	unsigned int ltstate : 2;
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

#define XLNX_V4L2_DV_BT_7680X4320P25 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(7680, 4320, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		74250000, 2552, 176, 592, 16, 20, 44, 0, 0, 0, \
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

static inline struct xdprxss_state *
to_xdprxssstate(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xdprxss_state, subdev);
}

/* Register related operations */
static inline u32 xdprxss_mmcm_read(struct xdprxss_state *xdprxss, u32 addr)
{
	return ioread32(xdprxss->dp_base + XDPRXSS_MMCM_OFFSET + addr);
}

static inline void xdprxss_mmcm_write(struct xdprxss_state *xdprxss, u32 addr,
				      u32 value)
{
	iowrite32(value, xdprxss->dp_base + XDPRXSS_MMCM_OFFSET + addr);
}

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

static void xdprxss_clrset(struct xdprxss_state *dp, u32 addr,
			   u32 clr_mask, u32 set_data)
{
	u32 regval;

	regval = xdprxss_read(dp, addr);
	regval &= ~clr_mask;
	regval |= set_data << __bf_shf(clr_mask);
	xdprxss_write(dp, addr, regval);
}

static inline void xdprxss_dpcd_update_start(struct xdprxss_state *xdprxss)
{
	iowrite32(0x1, xdprxss->dp_base + XDPRX_CTRL_DPCD_REG);
}

static inline void xdprxss_dpcd_update_end(struct xdprxss_state *xdprxss)
{
	iowrite32(0x0, xdprxss->dp_base + XDPRX_CTRL_DPCD_REG);
}

static inline int xdprxss_get_lane01_reqval(struct xdprxss_state *xdprxss)
{
	return xdprxss_read(xdprxss, XDPRX_DPCD_LANE01_STATUS) &
			    XDPRX_LANE01_PEVS_MASK;
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

static inline void xdprxss_soft_video_reset(struct xdprxss_state *xdprxss)
{
	xdprxss_write(xdprxss, XDPRX_SOFT_RST_REG, XDPRX_SOFT_VIDRST_MASK);
	xdprxss_write(xdprxss, XDPRX_SOFT_RST_REG, 0x0);
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
static int xlnx_dp_phy_ready(struct xdprxss_state *dp)
{
	u32 i, reg, ready;

	ready = XDPRX_PHYSTATUS_ALL_LANES_GOOD_MASK;

	/* Wait for 100ms. This should be enough time for PHY to be ready */
	for (i = 0; i < XDPRX_PHYSTATUS_READ_COUNT; i++) {
		reg = xdprxss_read(dp, XDPRX_PHYSTATUS_REG);
		if ((reg & ready) == ready)
			break;

		usleep_range(1000, 1100);
	}

	if (i == XDPRX_PHYSTATUS_READ_COUNT) {
		dev_err(dp->dev, "PHY isn't ready\n");
		return -ENODEV;
	}

	return 0;
}

static void config_rx_dec_clk(struct xdprxss_state *dp, int bw_code)
{
	u8 p5_fedge_en, o_val, d_val, m_val;
	u16 hightime, div_edge;
	u32 reg;

	/*
	 * Configuring MMCM to give a /20 clock output for /16 clk input.
	 *
	 * GT ch0outclk (/16) --> MMCM --> /20 clock
	 *
	 * Thus:
	 * 8.1G  : Input MMCM clock is 506.25, output is 405
	 * 5.4G  : Input MMCM clock is 337.5, output is 270
	 * 2.7G  : Input MMCM clock is 168.75, output is 135
	 * 1.62G : Input MMCM clock is 101.25, output is 81
	 */
	switch (bw_code) {
	case DP_LINK_BW_8_1:
		m_val = XDPRX_MMCM_M_VAL_405;
		break;
	case DP_LINK_BW_5_4:
		m_val = XDPRX_MMCM_M_VAL_270;
		break;
	case DP_LINK_BW_2_7:
		m_val = XDPRX_MMCM_M_VAL_135;
		break;
	default:
		m_val = XDPRX_MMCM_M_VAL_81;
	}
	d_val = XDPRX_MMCM_D_VAL;
	o_val = m_val / XDPRX_MMCM_M_O_VAL_RATIO;

	/*
	 * MMCM is dynamically programmed for the respective rate
	 * using the M, D, Div values
	 */
	hightime = o_val / MMCM_O_VAL_HIGHTIME_DIVIDER;
	reg = XDPRX_MMCM_REG3_PREDIV2 | XDPRX_MMCM_REG3_USED | XDPRX_MMCM_REG3_MX;
	if (o_val % MMCM_O_VAL_EDGE_DIVIDER > 1)
		reg |= BIT(8);

	p5_fedge_en = o_val % MMCM_O_VAL_FEDGE_DIVIDER;
	reg |= p5_fedge_en << XDPRX_MMCM_CLKOUT0_P5EN_SHIFT |
		p5_fedge_en << XDPRX_MMCM_CLKOUT0_P5FEDGE_SHIFT;
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG3_OFFSET, reg);
	reg = hightime | hightime << MMCM_MDO_VAL_HIGHTIME_SHIFT;
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG4_OFFSET, reg);

	/* Implement D */
	reg = 0;
	div_edge = d_val % MMCM_D_VAL_EDGE_DIVIDER;
	hightime = d_val / MMCM_D_VAL_HIGHTIME_DIVIDER;
	reg = reg | div_edge << XDPRX_MMCM_REG12_EDGE_SHIFT;
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG12_OFFSET, reg);
	reg = hightime | hightime << MMCM_MDO_VAL_HIGHTIME_SHIFT;
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG13_OFFSET, reg);

	/* Implement M */
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG25_OFFSET, 0);

	div_edge = m_val % MMCM_M_VAL_EDGE_DIVIDER;
	hightime = m_val / MMCM_M_VAL_HIGHTIME_DIVIDER;
	reg = hightime | hightime << MMCM_MDO_VAL_HIGHTIME_SHIFT;
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG2_OFFSET, reg);
	reg = XDPRX_MMCM_REG1_PREDIV2 | XDPRX_MMCM_REG1_EN | XDPRX_MMCM_REG1_MX;

	if (div_edge)
		reg = reg | XDPRX_MMCM_REG1_EDGE_MASK;
	else
		reg = reg & ~XDPRX_MMCM_REG1_EDGE_MASK;

	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG1_OFFSET, reg);
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG11_OFFSET, XDPRX_MMCM_REG11_VAL);
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG14_OFFSET, XDPRX_MMCM_REG14_VAL);
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG15_OFFSET, XDPRX_MMCM_REG15_VAL);
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG16_OFFSET, XDPRX_MMCM_REG16_VAL);
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG17_OFFSET, XDPRX_MMCM_REG17_VAL);
	xdprxss_mmcm_write(dp, XDPRX_MMCM_REG26_OFFSET, XDPRX_MMCM_REG26_VAL);
	xdprxss_mmcm_write(dp, XDPRX_MMCM_RECONFIG_OFFSET,
			   XDPRX_MMCM_RECONFIG_LOAD | XDPRX_MMCM_RECONFIG_SADDR);
}

static int get_rx_dec_clk_lock(struct xdprxss_state *dp)
{
	u32 retry = 0;

	/* MMCM issued a reset */
	xdprxss_mmcm_write(dp, XDPRX_MMCM_SWRST_OFFSET, XDPRX_MMCM_SWRST_VAL);
	while (!(xdprxss_mmcm_read(dp, XDPRX_MMCM_STATUS_OFFSET) & BIT(0))) {
		if (retry == XDPRX_MMCM_STATUS_RETRY)
			return -ENODEV;

		usleep_range(1000, 1100);
		retry++;
	}

	return 0;
}

static int config_gt_control_linerate(struct xdprxss_state *dp, int bw_code)
{
	u32 data;

	switch (bw_code) {
	case DP_LINK_BW_1_62:
		data = XDPRX_GTCTL_LINE_RATE_162G;
		break;
	case DP_LINK_BW_2_7:
		data = XDPRX_GTCTL_LINE_RATE_270G;
		break;
	case DP_LINK_BW_5_4:
		data = XDPRX_GTCTL_LINE_RATE_540G;
		break;
	case DP_LINK_BW_8_1:
		data = XDPRX_GTCTL_LINE_RATE_810G;
		break;
	default:
		data = XDPRX_GTCTL_LINE_RATE_810G;
	}

	xdprxss_clrset(dp, XDPRX_GTCTL_REG, XDPRX_GTCTL_LINE_RATE_MASK, data);

	return xlnx_dp_phy_ready(dp);
}

static int xlnx_dp_rx_gt_control_init(struct xdprxss_state *dp)
{
	int ret;

	/* setting initial vswing */
	xdprxss_clrset(dp, XDPRX_GTCTL_REG, XDPRX_GTCTL_VSWING_MASK,
		       XDPRX_GTCTL_VSWING_INIT_VAL);

	xdprxss_clr(dp, XDPRX_GTCTL_REG, XDPRX_GTCTL_EN);
	ret = xlnx_dp_phy_ready(dp);
	if (ret < 0)
		return ret;

	/* Setting initial link rate */
	ret = config_gt_control_linerate(dp, DP_LINK_BW_8_1);
	if (ret) {
		dev_err(dp->dev, "Default Line Rate setting Failed\n");
		return ret;
	}

	return 0;
}

static void xdprxss_dtg_disable(struct xdprxss_state *state)
{
	xdprxss_clr(state, XDPRX_DTG_REG, XDPRX_DTG_DIS_MASK);
	xdprxss_soft_video_reset(state);
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
	struct v4l2_dv_timings *dv_timings = &state->detected_timings;
	u32 rxmsa_mvid, rxmsa_nvid, rxmsa_misc, recv_clk_freq, linkrate, data;
	u16 vres_total, hres_total, framerate, lanecount;
	u16 hact, vact, hsw, vsw, hstart, vstart;
	u8 pixel_width, fmt;
	u16 read_val;

	rxmsa_mvid = xdprxss_read(state, XDPRX_MSA_MVID_REG);
	rxmsa_nvid = xdprxss_read(state, XDPRX_MSA_NVID_REG);

	hact = xdprxss_read(state, XDPRX_MSA_HRES_REG);

	vact = xdprxss_read(state, XDPRX_MSA_VHEIGHT_REG);
	rxmsa_misc = xdprxss_read(state, XDPRX_MSA_MISC0_REG);

	vres_total = xdprxss_read(state, XDPRX_MSA_VTOTAL_REG);
	hres_total = xdprxss_read(state, XDPRX_MSA_HTOTAL_REG);
	linkrate = xdprxss_read(state, XDPRX_LINK_BW_REG);
	lanecount = xdprxss_read(state, XDPRX_LANE_COUNT_REG);
	hstart = xdprxss_read(state, XDPRX_MSA_HSTART_REG);
	vstart = xdprxss_read(state, XDPRX_MSA_VSTART_REG);
	hsw = xdprxss_read(state, XDPRX_MSA_HSWIDTH_REG);
	vsw = xdprxss_read(state, XDPRX_MSA_VSWIDTH_REG);

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
	xdprxss_clr(state, XDPRX_DTG_REG, XDPRX_DTG_DIS_MASK);
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

	dv_timings->type = V4L2_DV_BT_656_1120;
	/*
	 * TODO : For now driver supports only progressive video.
	 * In future, driver may add with other interlace support
	 */
	dv_timings->bt.interlaced = false;
	dv_timings->bt.width = hact;
	dv_timings->bt.height = vact;
	dv_timings->bt.polarities = 0;

	data = xdprxss_read(state, XDPRX_MSA_HSPOL_REG);
	if (data & XDPRX_MSA_HSPOL_MASK)
		dv_timings->bt.polarities = V4L2_DV_HSYNC_POS_POL;

	data = xdprxss_read(state, XDPRX_MSA_VSPOL_REG);
	if (data & XDPRX_MSA_VSPOL_MASK)
		dv_timings->bt.polarities |= V4L2_DV_VSYNC_POS_POL;

	dv_timings->bt.pixelclock = vres_total * hres_total * framerate;
	dv_timings->bt.hsync = hsw;
	dv_timings->bt.hfrontporch = (hres_total - (hact + hstart));
	dv_timings->bt.hbackporch = hstart - hsw;
	dv_timings->bt.vsync = vsw;
	dv_timings->bt.vfrontporch = (vres_total - (vact + vstart));
	dv_timings->bt.vbackporch = vstart - vsw;

	spin_lock(&state->lock);
	format->width = dv_timings->bt.width;
	format->height = dv_timings->bt.height;
	format->colorspace = V4L2_COLORSPACE_REC709;
	format->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_DEFAULT;
	format->field = V4L2_FIELD_NONE;
	state->frame_interval = framerate;
	spin_unlock(&state->lock);

	dev_dbg(state->dev, "detected properties : width %d height %d\n",
		dv_timings->bt.width, dv_timings->bt.height);

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
	xdprxss_set(xdprxss, XDPRX_DTG_REG, XDPRX_VSCEXT_VESA_SDP_SUPPORTED);

	/* Disable all the interrupts */
	xdprxss_set(xdprxss, XDPRX_INTR_MASK_REG, XDPRX_INTR_ALL_MASK);
	xdprxss_disable_allintr_1(xdprxss);

	/* Enable trainng related interrupts */
	xdprxss_clr(xdprxss, XDPRX_INTR_MASK_REG, XDPRX_INTR_TRNG_MASK);
	xdprxss_enable_training_intr_1(xdprxss);

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
	xdprxss_update_ext_rcv_cap(xdprxss, xdprxss->max_linkrate);
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
	xdprxss_set_clk_data_recovery_timeout_val(xdprxss,
						  XDPRX_CDRCTRL_TDLOCK_VAL);
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
	xdprxss_enable_training_timeout(xdprxss);
	xdprxss_set_training_params(xdprxss);
}

static void xdprxss_irq_unplug(struct xdprxss_state *state)
{
	dev_dbg(state->dev, "Asserted cable unplug interrupt\n");

	xdprxss_set(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_VIDRST_MASK);
	xdprxss_clr(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_VIDRST_MASK);

	if (state->retimer_prvdata)
		state->retimer_prvdata->retimer_rst_dp_path();

	/*
	 * Disable unplug interrupt so that no unplug event when RX is
	 * disconnected
	 */
	xdprxss_disable_unplug_intr(state);
	xdprxss_generate_hpd_intr(state, XDPRX_HPD_PULSE_750);

	xdprxss_disable_allintr(state);
	xdprxss_disable_allintr_1(state);

	xdprxss_enable_training_intr(state);
	xdprxss_enable_training_intr_1(state);
	/*
	 * In a scenario, where the cable is plugged-in but the training
	 * is lost, the software is expected to assert a HPD upon the
	 * occurrence of a TRAINING_LOST interrupt, so that the Source
	 * can retrain the link.
	 */
	xdprxss_write(state, XDPRX_HPD_INTR_REG,
		      FIELD_PREP(XDPRX_HPD_PULSE_MASK, XDPRX_HPD_PULSE_5000) |
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

	if (state->retimer_prvdata) {
		state->retimer_prvdata->retimer_rst_cr_path();
		state->retimer_prvdata->retimer_access_laneset();
	}

	if (!state->versal_gt_present) {
		phy_cfg->set_rate = 1;
		for (i = 0; i < state->max_lanecount; i++)
			phy_configure(state->phy[i], &phy_opts);
		/* Initialize phy logic of DP-RX core */
		xdprxss_write(state, XDPRX_PHY_REG, XDPRX_PHY_INIT_MASK);
		phy_reset(state->phy[0]);
	} else {
		config_rx_dec_clk(state, linkrate);

		config_gt_control_linerate(state, linkrate);

		if (get_rx_dec_clk_lock(state))
			dev_info(state->dev, "rx decryption clock failed to lock\n");

		/* Initialize phy logic of DP-RX core */
		xdprxss_write(state, XDPRX_PHY_REG, XDPRX_PHY_INIT_MASK);
	}
	state->ltstate = 1;
	xdprxss_clr(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_ALL_MASK);
}

static void xdprxss_irq_tp2(struct xdprxss_state *state)
{
	dev_dbg(state->dev, "Asserted traning pattern 2\n");
	state->ltstate = 2;
}

static void xdprxss_training_failure(struct xdprxss_state *state)
{
	dev_dbg(state->dev, "Traning Lost !!\n");
	state->valid_stream = false;

	xdprxss_write(state, XDPRX_HPD_INTR_REG,
		      FIELD_PREP(XDPRX_HPD_PULSE_MASK, XDPRX_HPD_PULSE_750) |
		      XDPRX_HPD_INTR_MASK);

	/* reset the aux logic */
	xdprxss_set(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_AUXRST_MASK);
	xdprxss_clr(state, XDPRX_SOFT_RST_REG, XDPRX_SOFT_AUXRST_MASK);
	xdprxss_disable_audio(state);
}

static void xdprxss_irq_no_video(struct xdprxss_state *state)
{
	dev_dbg(state->dev, "No Valid video received !!\n");

	xdprxss_write(state, XDPRX_VIDEO_UNSUPPORTED_REG, 0x1);
	xdprxss_clr(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_VBLANK_MASK);
	xdprxss_set(state, XDPRX_INTR_MASK_REG, XDPRX_INTR_NOVID_MASK);

	xdprxss_dtg_disable(state);
	xdprxss_dtg_enable(state);

	xdprxss_enable_audio_intr(state);

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

	xdprxss_disable_audio(state);
	xdprxss_enable_audio(state);
}

/**
 * xdprxss_parse_drmif - Parse DRM infoframe from received infoframe packet
 * @state: pointer to driver state
 * @drm_infoframe: DRM infoframe structure member
 * This function parses DRM(Dynamic Range and Mastering InfoFrame) infoframe
 * from received infoframe packet. For more information please refer to the
 * section 6.9 in CTA-861-G
 *
 */
static void xdprxss_parse_drmif(struct xdprxss_state *state,
				struct v4l2_hdr10_payload *drm_infoframe)
{
	struct xdprxss_infoframe *iframe = &state->infoframe;

	drm_infoframe->eotf = iframe->payload.byte[2] & 0x7;
	drm_infoframe->metadata_type = iframe->payload.byte[3] & 0x7;
	drm_infoframe->display_primaries[0].x =
					(iframe->payload.byte[4] & 0xFF) |
					(iframe->payload.byte[5] << 8);
	drm_infoframe->display_primaries[0].y =
					(iframe->payload.byte[6] & 0xFF) |
					(iframe->payload.byte[7] << 8);
	drm_infoframe->display_primaries[1].x =
					(iframe->payload.byte[8] & 0xFF) |
					(iframe->payload.byte[9] << 8);
	drm_infoframe->display_primaries[1].y =
					(iframe->payload.byte[10] & 0xFF) |
					(iframe->payload.byte[11] << 8);
	drm_infoframe->display_primaries[2].x =
					(iframe->payload.byte[12] & 0xFF) |
					(iframe->payload.byte[13] << 8);
	drm_infoframe->display_primaries[2].y =
					(iframe->payload.byte[14] & 0xFF) |
					(iframe->payload.byte[15] << 8);
	drm_infoframe->white_point.x =
				(iframe->payload.byte[16] & 0xFF) |
				(iframe->payload.byte[17] << 8);
	drm_infoframe->white_point.y =
				(iframe->payload.byte[18] & 0xFF) |
				(iframe->payload.byte[19] << 8);
	drm_infoframe->max_mdl = (iframe->payload.byte[20] & 0xFF) |
				(iframe->payload.byte[21] << 8);
	drm_infoframe->min_mdl = (iframe->payload.byte[22] & 0xFF) |
				(iframe->payload.byte[23] << 8);
	drm_infoframe->max_cll = (iframe->payload.byte[24] & 0xFF) |
				(iframe->payload.byte[25] << 8);
	drm_infoframe->max_fall = (iframe->payload.byte[26] & 0xFF) |
				(iframe->payload.byte[27] << 8);
}

static void xdprxss_irq_audio_detected(struct xdprxss_state *state)
{
	struct xdprxss_infoframe *iframe = &state->infoframe;
	struct v4l2_hdr10_payload *drm_infoframe = &state->drm_infoframe;
	u32 buff[INFO_PCKT_SIZE_WORDS];
	u8 *buf_ptr;
	int i;

	iframe->header.data = xdprxss_read(state, XDPRX_AUDIO_INFO_DATA);
	buff[0] = iframe->header.data;
	for (i = 0; i < (INFO_PCKT_SIZE_WORDS - 1); i++) {
		iframe->payload.data[i] = xdprxss_read(state,
						       XDPRX_AUDIO_INFO_DATA);
		buff[i + 1] = iframe->payload.data[i];
	}

	buf_ptr = (u8 *)buff;
	memcpy(state->rx_audio_data->infoframe, buff, INFO_PCKT_SIZE);

	if (buf_ptr[1] == INFO_PCKT_TYPE_AUDIO)
		state->rx_audio_data->audio_detected = true;
	if (iframe->header.byte[1] == INFO_PCKT_TYPE_DRM) {
		memset((void *)drm_infoframe, 0,
		       sizeof(struct v4l2_hdr10_payload));
		xdprxss_parse_drmif(state, drm_infoframe);
	}
}

static void xdprxss_irq_access_laneset(struct xdprxss_state *state)
{
	u32 read_val;
	u8 training;

	training = xdprxss_read(state, XDPRX_DPCD_TRAINING_PATTERN_SET);

	if (state->ltstate == 2 && training != 1) {
		read_val = xdprxss_get_lane01_reqval(state);

		if (state->ce_req_val != read_val && state->retimer_prvdata)
			state->retimer_prvdata->retimer_access_laneset();

		/* Update the value to be used in next round */
		state->ce_req_val = xdprxss_get_lane01_reqval(state);
	}
}

static void xdprxss_irq_access_linkqual(struct xdprxss_state *state)
{
	u32 read_val;

	read_val = xdprxss_read(state, XDPRX_DPC_LINK_QUAL_CONFIG);

	if ((read_val & XDPRX_LINK_QUAL_PRBS_MODE_MASK) ==
	    XDPRX_DPCD_LINK_QUAL_PRBS_MASK) {
		/* enable PRBS mode in video phy */
		state->vidphy_prvdata->vidphy_prbs_mode(1);
		/* enable PRBS mode in retimer */
		state->retimer_prvdata->retimer_prbs_mode(1);
	} else {
		/* disable PRBS mode in video phy */
		state->vidphy_prvdata->vidphy_prbs_mode(0);
		/* disable PRBS mode in retimer */
		state->retimer_prvdata->retimer_prbs_mode(0);
	}
}

static irqreturn_t xdprxss_irq_handler(int irq, void *dev_id)
{
	struct xdprxss_state *state = (struct xdprxss_state *)dev_id;
	u32 status, status1;

	status = xdprxss_read(state, XDPRX_INTR_CAUSE_REG);
	status &= ~xdprxss_read(state, XDPRX_INTR_MASK_REG);

	status1 = xdprxss_read(state, XDPRX_INTR_CAUSE_1_REG);
	status1 &= ~xdprxss_read(state, XDPRX_INTR_MASK_1_REG);

	if (!status)
		return IRQ_NONE;

	if (status1 & XDPRX_INTR_ACCESS_LANE_SET_MASK)
		xdprxss_irq_access_laneset(state);
	if (status1 & XDPRX_INTR_LINKQUAL_MASK)
		xdprxss_irq_access_linkqual(state);
	if (status & XDPRX_INTR_UNPLUG_MASK)
		schedule_delayed_work(&state->unplug_work, 0);
	if (status & XDPRX_INTR_TP1_MASK)
		schedule_delayed_work(&state->tp1_work, 0);
	if (status & XDPRX_INTR_TP2_MASK)
		xdprxss_irq_tp2(state);
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
			 struct v4l2_subdev_state *sd_state,
			 unsigned int pad, u32 which)
{
	struct v4l2_mbus_framefmt *format;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		format = v4l2_subdev_get_try_format(&xdprxss->subdev,
						    sd_state, pad);
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
 * @sd_state: Pointer to sub device pad information structure
 *
 * This function is used to initialize the pad format with the default
 * values.
 *
 * Return: 0 on success
 */
static int xdprxss_init_cfg(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state)
{
	struct xdprxss_state *xdprxss = to_xdprxssstate(sd);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(sd, sd_state, 0);

	if (!xdprxss->valid_stream)
		*format = xdprxss->format;

	return 0;
}

/**
 * xdprxss_getset_format - This is used to set and get the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @sd_state: Pointer to sub device pad information structure
 * @fmt: Pointer to pad level media bus format
 *
 * This function is used to set the pad format.
 * Since the pad format is fixed in hardware, it can't be
 * modified on run time.
 *
 * Return: 0 on success
 */
static int xdprxss_getset_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
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
	format = __xdprxss_get_pad_format(xdprxss, sd_state,
					  fmt->pad, fmt->which);
	if (!format)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

/**
 * xdprxss_enum_mbus_code - Handle pixel format enumeration
 * @sd: pointer to v4l2 subdev structure
 * @sd_state: V4L2 subdev pad configuration
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 *
 * Return: -EINVAL or zero on success
 */
static int xdprxss_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
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

	if (!timings)
		return -EINVAL;

	if (!state->valid_stream)
		return -ENOLCK;

	*timings = state->detected_timings;

	return 0;
}

static int xdprxss_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct xdprxss_state *state = container_of(ctrl->handler,
						   struct xdprxss_state,
						   ctrl_handler);
	struct v4l2_metadata_hdr *hdr_ptr;

	switch (ctrl->id) {
	case V4L2_CID_METADATA_HDR:
		if (!state->valid_stream) {
			dev_err(state->dev, "Can't get values when video not locked!\n");
			return -EINVAL;
		}
		hdr_ptr = (struct v4l2_metadata_hdr *)ctrl->p_new.p;
		hdr_ptr->metadata_type = V4L2_HDR_TYPE_HDR10;
		hdr_ptr->size = sizeof(struct v4l2_hdr10_payload);
		memcpy(hdr_ptr->payload, &state->drm_infoframe,
		       hdr_ptr->size);
		break;
	default:
		dev_err(state->dev, "Get Invalid control id 0x%08x\n", ctrl->id);
		ret = -EINVAL;
	}

	dev_dbg(state->dev, "Get ctrl id = 0x%08x val = 0x%08x\n",
		ctrl->id, ctrl->val);
	return ret;
}

/* ------------------------------------------------------------
 * Media Operations
 */
static const struct v4l2_ctrl_ops xdprxss_ctrl_ops = {
	.g_volatile_ctrl = xdprxss_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config xdprxss_ctrls[] = {
	{
		.ops = &xdprxss_ctrl_ops,
		.id = V4L2_CID_METADATA_HDR,
		.name = "HDR Controls",
		.type = V4L2_CTRL_TYPE_HDR,
		.min = 0x8000000000000000,
		.max = 0x7FFFFFFFFFFFFFFF,
		.step = 1,
		.def = 0,
		.elem_size = sizeof(struct v4l2_metadata_hdr),
		.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_HAS_PAYLOAD,
	}
};

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

	xdprxss->versal_gt_present =
		of_property_read_bool(node, "xlnx,versal-gt");

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

static void xlnx_dp_tp1_work_func(struct work_struct *work)
{
	struct xdprxss_state *dp;

	dp = container_of(work, struct xdprxss_state, tp1_work.work);

	xdprxss_irq_tp1(dp);
}

static void xlnx_dp_unplug_work_func(struct work_struct *work)
{
	struct xdprxss_state *dp;

	dp = container_of(work, struct xdprxss_state, unplug_work.work);

	xdprxss_irq_unplug(dp);
}

static int xlnx_find_device(struct platform_device *pdev,
			    struct xdprxss_state *xdprxss, const char *name)
{
	struct device_node *pnode = pdev->dev.of_node;
	struct device_node *fnode;
	struct platform_device *iface_pdev;

	fnode = of_parse_phandle(pnode, name, 0);
	if (!fnode) {
		dev_err(&pdev->dev, "platform node %s not found\n", name);
		of_node_put(fnode);
	} else {
		iface_pdev = of_find_device_by_node(fnode);
		if (!iface_pdev) {
			of_node_put(pnode);
			return -ENODEV;
		}

		xdprxss->prvdata = dev_get_drvdata(&iface_pdev->dev);
		if (!xdprxss->prvdata) {
			dev_info(&pdev->dev,
				 "platform device(%s) not found -EPROBE_DEFER\n", name);
			of_node_put(fnode);
			return -EPROBE_DEFER;
		}
		of_node_put(fnode);
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

	ret = xlnx_find_device(pdev, xdprxss, "xlnx,dp-retimer");
	if (ret)
		return ret;
	xdprxss->retimer_prvdata = xdprxss->prvdata;

	ret = xlnx_find_device(pdev, xdprxss, "xlnx,vidphy");
	if (ret)
		return ret;
	xdprxss->vidphy_prvdata = xdprxss->prvdata;

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

	if (!xdprxss->versal_gt_present) {
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
				else
					dev_err(dev,
						"failed to get phy lane %s i %d, ret = %d\n",
						phy_name, i, ret);
				goto error_phy;
			}
			ret = phy_init(xdprxss->phy[i]);
			if (ret) {
				dev_err(dev,
					"failed to init phy lane %d\n", i);
				goto error_phy;
			}
		}
	} else {
		xdprxss->phy[0] = devm_phy_get(xdprxss->dev, "dp-gtquad");
		if (IS_ERR(xdprxss->phy[0]))
			return dev_err_probe(dev, PTR_ERR(xdprxss->phy[0]),
					"failed to get phy\n");

		ret = phy_init(xdprxss->phy[0]);
		if (ret) {
			dev_err(dev, "failed to init phy\n");
			goto error_phy;
		}

		ret = xlnx_find_device(pdev, xdprxss, "xlnx,xilinx-vfmc");
		if (ret)
			return ret;

		ret = xlnx_dp_rx_gt_control_init(xdprxss);
		if (ret < 0)
			return ret;

		if (get_rx_dec_clk_lock(xdprxss))
			dev_info(dev, "rx decryption clock failed to lock\n");
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

	ret = v4l2_ctrl_handler_init(&xdprxss->ctrl_handler,
				     ARRAY_SIZE(xdprxss_ctrls));
	if (ret < 0) {
		dev_err(xdprxss->dev, "failed to initialize V4L2 ctrl\n");
		goto error;
	}

	for (i = 0; i < ARRAY_SIZE(xdprxss_ctrls); i++) {
		struct v4l2_ctrl *ctrl;

		dev_dbg(xdprxss->dev, "%d ctrl = 0x%x\n", i,
			xdprxss_ctrls[i].id);
		ctrl = v4l2_ctrl_new_custom(&xdprxss->ctrl_handler,
					    &xdprxss_ctrls[i], NULL);
		if (!ctrl) {
			dev_err(xdprxss->dev, "Failed for %s ctrl\n",
				xdprxss_ctrls[i].name);
			v4l2_ctrl_handler_free(&xdprxss->ctrl_handler);
			goto error;
		}
	}

	if (xdprxss->ctrl_handler.error) {
		dev_err(xdprxss->dev, "failed to add controls\n");
		ret = xdprxss->ctrl_handler.error;
		v4l2_ctrl_handler_free(&xdprxss->ctrl_handler);
		goto error;
	}

	subdev->ctrl_handler = &xdprxss->ctrl_handler;
	ret = v4l2_ctrl_handler_setup(&xdprxss->ctrl_handler);
	if (ret < 0) {
		dev_err(xdprxss->dev, "failed to set controls\n");
		goto error;
	}

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

	INIT_DELAYED_WORK(&xdprxss->tp1_work, xlnx_dp_tp1_work_func);
	INIT_DELAYED_WORK(&xdprxss->unplug_work, xlnx_dp_unplug_work_func);

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
	if (!xdprxss->versal_gt_present) {
		for (j = 0; j < i; j++) {
			if (xdprxss->phy[j]) {
				dev_dbg(dev,
					"phy_exit() xdprxss->phy[%d] = %p\n",
					j, xdprxss->phy[j]);
				phy_exit(xdprxss->phy[j]);
			}
		}
	} else {
		phy_exit(xdprxss->phy[0]);
	}

	return ret;
}

static int xdprxss_remove(struct platform_device *pdev)
{
	struct xdprxss_state *xdprxss = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xdprxss->subdev;
	unsigned int i;

	cancel_delayed_work_sync(&xdprxss->tp1_work);
	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	clk_disable_unprepare(xdprxss->rx_vid_clk);
	clk_disable_unprepare(xdprxss->rx_lnk_clk);
	clk_disable_unprepare(xdprxss->axi_clk);
	if (!xdprxss->versal_gt_present)
		for (i = 0; i < XDPRX_MAX_LANE_COUNT; i++)
			phy_exit(xdprxss->phy[i]);
	else
		phy_exit(xdprxss->phy[0]);

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

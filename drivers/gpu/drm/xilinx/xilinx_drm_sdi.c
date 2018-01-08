/*
 * Xilinx FPGA SDI Tx Controller driver.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Contacts: Saurabh Sengar <saurabhs@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_crtc_helper.h>
#include <drm/drmP.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <video/videomode.h>
#include "xilinx_drm_sdi.h"
#include "xilinx_vtc.h"

/* SDI register offsets */
#define XSDI_TX_RST_CTRL				0x00
#define XSDI_TX_MDL_CTRL				0x04
#define XSDI_TX_GLBL_IER				0x0C
#define XSDI_TX_ISR_STAT				0x10
#define XSDI_TX_IER_STAT				0x14
#define XSDI_TX_ST352_LINE				0x18
#define XSDI_TX_ST352_DATA_CH0				0x1C
#define XSDI_TX_VER					0x3C
#define XSDI_TX_SYS_CFG					0x40
#define XSDI_TX_STS_SB_TDATA				0x60
#define XSDI_TX_AXI4S_STS1				0x68
#define XSDI_TX_AXI4S_STS2				0x6C

/* MODULE_CTRL register masks */
#define XSDI_TX_CTRL_MDL_EN_MASK			BIT(0)
#define XSDI_TX_CTRL_OUT_EN_MASK			BIT(1)
#define XSDI_TX_CTRL_M_MASK				BIT(7)
#define XSDI_TX_CTRL_INS_CRC_MASK			BIT(12)
#define XSDI_TX_CTRL_INS_ST352_MASK			BIT(13)
#define XSDI_TX_CTRL_OVR_ST352_MASK			BIT(14)
#define XSDI_TX_CTRL_INS_SYNC_BIT_MASK			BIT(16)
#define XSDI_TX_CTRL_SD_BITREP_BYPASS_MASK		BIT(17)
#define XSDI_TX_CTRL_USE_ANC_IN_MASK			BIT(18)
#define XSDI_TX_CTRL_INS_LN_MASK			BIT(19)
#define XSDI_TX_CTRL_INS_EDH_MASK			BIT(20)
#define XSDI_TX_CTRL_MODE_MASK				0x7
#define XSDI_TX_CTRL_MUX_MASK				0x7
#define XSDI_TX_CTRL_MODE_SHIFT				4
#define XSDI_TX_CTRL_M_SHIFT				7
#define XSDI_TX_CTRL_MUX_SHIFT				8
#define XSDI_TX_CTRL_INS_CRC_SHIFT			12
#define XSDI_TX_CTRL_INS_ST352_SHIFT			13
#define XSDI_TX_CTRL_OVR_ST352_SHIFT			14
#define XSDI_TX_CTRL_ST352_F2_EN_SHIFT			15
#define XSDI_TX_CTRL_INS_SYNC_BIT_SHIFT			16
#define XSDI_TX_CTRL_SD_BITREP_BYPASS_SHIFT		17
#define XSDI_TX_CTRL_USE_ANC_IN_SHIFT			18
#define XSDI_TX_CTRL_INS_LN_SHIFT			19
#define XSDI_TX_CTRL_INS_EDH_SHIFT			20

/* TX_ST352_LINE register masks */
#define XSDI_TX_ST352_LINE_MASK				GENMASK(10, 0)
#define XSDI_TX_ST352_LINE_F2_SHIFT			16

/* ISR STAT register masks */
#define XSDI_GTTX_RSTDONE_INTR_MASK			BIT(0)
#define XSDI_TX_CE_ALIGN_ERR_INTR_MASK			BIT(1)
#define XSDI_AXI4S_VID_LOCK_INTR_MASK			BIT(8)
#define XSDI_OVERFLOW_INTR_MASK				BIT(9)
#define XSDI_UNDERFLOW_INTR_MASK			BIT(10)
#define XSDI_IER_EN_MASK		(XSDI_GTTX_RSTDONE_INTR_MASK | \
					XSDI_TX_CE_ALIGN_ERR_INTR_MASK | \
					XSDI_OVERFLOW_INTR_MASK | \
					XSDI_UNDERFLOW_INTR_MASK)

/* RST_CTRL_OFFSET masks */
#define XSDI_TX_BRIDGE_CTRL_EN_MASK			BIT(8)
#define XSDI_TX_AXI4S_CTRL_EN_MASK			BIT(9)
#define XSDI_TX_CTRL_EN_MASK				BIT(0)

/* STS_SB_TX_TDATA masks */
#define XSDI_TX_TDATA_DONE_MASK				BIT(0)
#define XSDI_TX_TDATA_FAIL_MASK				BIT(1)
#define XSDI_TX_TDATA_GT_RESETDONE_MASK			BIT(2)
#define XSDI_TX_TDATA_SLEW_RATE_MASK			BIT(3)
#define XSDI_TX_TDATA_TXPLLCLKSEL_MASK			GENMASK(5, 4)
#define XSDI_TX_TDATA_GT_SYSCLKSEL_MASK			GENMASK(7, 6)
#define XSDI_TX_TDATA_FABRIC_RST_MASK			BIT(8)
#define XSDI_TX_TDATA_DRP_FAIL_MASK			BIT(9)
#define XSDI_TX_TDATA_FAIL_CODE_MASK			GENMASK(14, 12)
#define XSDI_TX_TDATA_DRP_FAIL_CNT_MASK			0xFF0000
#define XSDI_TX_TDATA_GT_QPLL0LOCK_MASK			BIT(24)
#define XSDI_TX_TDATA_GT_QPLL1LOCK_MASK			BIT(25)

#define SDI_MAX_DATASTREAM					8

#define XSDI_TX_MUX_SD_HD_3GA			0
#define	XSDI_TX_MUX_3GB				1
#define	XSDI_TX_MUX_8STREAM_6G_12G		2
#define	XSDI_TX_MUX_4STREAM_6G			3
#define	XSDI_TX_MUX_16STREAM_12G		4

#define PIXELS_PER_CLK				2
#define XSDI_CH_SHIFT				29
#define XST352_PROG_PIC_MASK			BIT(6)
#define XST352_PROG_TRANS_MASK			BIT(7)
#define XST352_2048_SHIFT			BIT(6)
#define ST352_BYTE3				0x00
#define ST352_BYTE4				0x01
#define INVALID_VALUE				-1
#define GT_TIMEOUT				500

static LIST_HEAD(xilinx_sdi_list);
static DEFINE_MUTEX(xilinx_sdi_lock);
/**
 * enum payload_line_1 - Payload Ids Line 1 number
 * @PAYLD_LN1_HD_3_6_12G:	line 1 HD,3G,6G or 12G mode value
 * @PAYLD_LN1_SDPAL:		line 1 SD PAL mode value
 * @PAYLD_LN1_SDNTSC:		line 1 SD NTSC mode value
 */
enum payload_line_1 {
	PAYLD_LN1_HD_3_6_12G = 10,
	PAYLD_LN1_SDPAL = 9,
	PAYLD_LN1_SDNTSC = 13
};

/**
 * enum payload_line_2 - Payload Ids Line 2 number
 * @PAYLD_LN2_HD_3_6_12G:	line 2 HD,3G,6G or 12G mode value
 * @PAYLD_LN2_SDPAL:		line 2 SD PAL mode value
 * @PAYLD_LN2_SDNTSC:		line 2 SD NTSC mode value
 */
enum payload_line_2 {
	PAYLD_LN2_HD_3_6_12G = 572,
	PAYLD_LN2_SDPAL = 322,
	PAYLD_LN2_SDNTSC = 276
};

/**
 * enum sdi_modes - SDI modes
 * @XSDI_MODE_HD:	HD mode
 * @XSDI_MODE_SD:	SD mode
 * @XSDI_MODE_3GA:	3GA mode
 * @XSDI_MODE_3GB:	3GB mode
 * @XSDI_MODE_6G:	6G mode
 * @XSDI_MODE_12G:	12G mode
 */
enum sdi_modes {
	XSDI_MODE_HD = 0,
	XSDI_MODE_SD,
	XSDI_MODE_3GA,
	XSDI_MODE_3GB,
	XSDI_MODE_6G,
	XSDI_MODE_12G
};

/**
 * struct xilinx_sdi - Core configuration SDI Tx subsystem device structure
 * @encoder: DRM encoder structure
 * @connector: DRM connector structure
 * @vtc: Pointer to VTC structure
 * @dev: device structure
 * @base: Base address of SDI subsystem
 * @mode_flags: SDI operation mode related flags
 * @wait_event: wait event
 * @event_received: wait event status
 * @list: entry in the global SDI subsystem list
 * @vblank_fn: vblank handler
 * @vblank_data: vblank data to be used in vblank_fn
 * @sdi_mode: configurable SDI mode parameter, supported values are:
 *		0 - HD
 *		1 - SD
 *		2 - 3GA
 *		3 - 3GB
 *		4 - 6G
 *		5 - 12G
 * @sdi_mod_prop_val: configurable SDI mode parameter value
 * @sdi_data_strm: configurable SDI data stream parameter
 * @sdi_data_strm_prop_val: configurable number of SDI data streams
 *			    value currently supported are 2, 4 and 8
 * @is_frac_prop: configurable SDI fractional fps parameter
 * @is_frac_prop_val: configurable SDI fractional fps parameter value
 */
struct xilinx_sdi {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct xilinx_vtc *vtc;
	struct device *dev;
	void __iomem *base;
	u32 mode_flags;
	wait_queue_head_t wait_event;
	bool event_received;
	struct list_head list;
	void (*vblank_fn)(void *);
	void *vblank_data;
	struct drm_property *sdi_mode;
	u32 sdi_mod_prop_val;
	struct drm_property *sdi_data_strm;
	u32 sdi_data_strm_prop_val;
	struct drm_property *is_frac_prop;
	bool is_frac_prop_val;
};

/**
 * struct xilinx_sdi_display_config - SDI supported modes structure
 * @mode: drm display mode
 * @st352_byt2: st352 byte 2 value
 *		index 0 : value for integral fps
 *		index 1 : value for fractional fps
 * @st352_byt1: st352 byte 1 value
 *		index 0 : value for HD mode
 *		index 1 : value for SD mode
 *		index 2 : value for 3GA
 *		index 3 : value for 3GB
 *		index 4 : value for 6G
 *		index 5 : value for 12G
 */
struct xlnx_sdi_display_config {
	struct drm_display_mode mode;
	u8 st352_byt2[2];
	u8 st352_byt1[6];
};

/*
 * xlnx_sdi_modes - SDI DRM modes
 */
static const struct xlnx_sdi_display_config xlnx_sdi_modes[] = {
	/* 0 - dummy, VICs start at 1 */
	{ },
	/* SD: 720x480i@60Hz */
	{{ DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500, 720, 739,
		   801, 858, 0, 240, 244, 247, 262, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   .vrefresh = 60, }, {0x7, 0x6},
		   {0x81, 0x81, 0x81, 0x81, 0x81, 0x81} },
	/* SD: 720x576i@50Hz */
	{{ DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER, 13500, 720, 732,
		   795, 864, 0, 288, 290, 293, 312, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   .vrefresh = 50, }, {0x9, 0x9},
		   {0x81, 0x81, 0x81, 0x81, 0x81, 0x81} },
	/* HD: 1280x720@25Hz */
	{{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 2250,
		   2990, 3960, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 25, }, {0x5, 0x5},
		   {0x84, 0x84, 0x88, 0x84, 0x84, 0x84} },
	/* HD: 1280x720@24Hz */
	{{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 2250,
		   3155, 4125, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 24, }, {0x3, 0x2},
		   {0x84, 0x84, 0x88, 0x84, 0x84, 0x84} },
	/* HD: 1280x720@30Hz */
	 {{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 2250,
		   2330, 3300, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 30, }, {0x7, 0x6},
		   {0x84, 0x84, 0x88, 0x84, 0x84, 0x84} },
	/* HD: 1280x720@50Hz */
	{{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 50, }, {0x9, 0x9},
		   {0x84, 0x84, 0x88, 0x84, 0x84, 0x84} },
	/* HD: 1280x720@60Hz */
	{{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 60, }, {0xB, 0xA},
		   {0x84, 0x84, 0x88, 0x84, 0x84, 0x84} },
	/* HD: 1920x1080@24Hz */
	{{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2558,
		   2602, 2750, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 24, }, {0x3, 0x2},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 1920x1080@25Hz */
	{{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 25, }, {0x5, 0x5},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 1920x1080@30Hz */
	{{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 30, }, {0x7, 0x6},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 1920x1080i@48Hz */
	{{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2291,
		   2379, 2750, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 48, }, {0x3, 0x2},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 1920x1080i@50Hz */
	{{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2448,
		   2492, 2640, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 50, }, {0x5, 0x5},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 1920x1080i@60Hz */
	{{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
		   2052, 2200, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 60, }, {0x7, 0x6},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 1920x1080sf@24Hz */
	{{ DRM_MODE("1920x1080sf", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2291,
		   2379, 2750, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN),
		   .vrefresh = 48, }, {0x3, 0x2},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 1920x1080sf@25Hz */
	{{ DRM_MODE("1920x1080sf", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2448,
		   2492, 2640, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN),
		   .vrefresh = 50, }, {0x5, 0x5},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 1920x1080sf@30Hz */
	{{ DRM_MODE("1920x1080sf", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
		   2052, 2200, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN),
		   .vrefresh = 60, }, {0x7, 0x6},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080i@48Hz */
	{{ DRM_MODE("2048x1080i", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2377,
		   2421, 2750, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 48, }, {0x3, 0x2},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080i@50Hz */
	{{ DRM_MODE("2048x1080i", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2322,
		   2366, 2640, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 50, }, {0x5, 0x5},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080i@60Hz */
	{{ DRM_MODE("2048x1080i", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2114,
		   2134, 2200, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 60, }, {0x7, 0x6},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080sf@24Hz */
	{{ DRM_MODE("2048x1080sf", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2377,
		   2421, 2750, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN),
		   .vrefresh = 48, }, {0x3, 0x2},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080sf@25Hz */
	{{ DRM_MODE("2048x1080sf", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2322,
		   2366, 2640, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN),
		   .vrefresh = 50, }, {0x5, 0x5},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080sf@30Hz */
	{{ DRM_MODE("2048x1080sf", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2114,
		   2134, 2200, 0, 540, 542, 547, 562, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN),
		   .vrefresh = 60, }, {0x7, 0x6},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080@30Hz */
	{{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2114,
		   2134, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 30, }, {0x7, 0x6},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080@25Hz */
	{{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 25, }, {0x5, 0x5},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* HD: 2048x1080@24Hz */
	{{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 74250, 2048, 2558,
		   2602, 2750, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 24, }, {0x3, 0x2},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G: 1920x1080@48Hz */
	{{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2558,
		   2602, 2750, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 48, }, {0x8, 0x4},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G: 1920x1080@50Hz */
	{{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 50, }, {0x9, 0x9},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G: 1920x1080@60Hz */
	{{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 60, }, {0xB, 0xA},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G: 2048x1080@60Hz */
	{{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 148500, 2048, 2136,
		   2180, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 60, }, {0xB, 0xA},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G: 2048x1080@50Hz */
	{{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 148500, 2048, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 50, }, {0x9, 0x9},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G: 2048x1080@48Hz */
	{{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 148500, 2048, 2558,
		   2602, 2750, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 48, }, {0x8, 0x4},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G-B: 1920x1080i@96Hz */
	{{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2291,
		   2379, 2750, 0, 1080, 1084, 1094, 1124, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 96, }, {0x8, 0x4},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G-B: 1920x1080i@100Hz */
	{{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1094, 1124, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 100, }, {0x9, 0x9},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G-B: 1920x1080i@120Hz */
	{{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1094, 1124, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 120, }, {0xB, 0xA},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G-B: 2048x1080i@96Hz */
	{{ DRM_MODE("2048x1080i", DRM_MODE_TYPE_DRIVER, 148500, 2048, 2377,
		   2421, 2750, 0, 1080, 1084, 1094, 1124, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 96, }, {0x8, 0x4},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G-B: 2048x1080i@100Hz */
	{{ DRM_MODE("2048x1080i", DRM_MODE_TYPE_DRIVER, 148500, 2048, 2322,
		   2366, 2640, 0, 1080, 1084, 1094, 1124, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 100, }, {0x9, 0x9},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 3G-B: 2048x1080i@120Hz */
	{{ DRM_MODE("2048x1080i", DRM_MODE_TYPE_DRIVER, 148500, 2048, 2114,
		   2134, 2200, 0, 1080, 1084, 1094, 1124, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
		   .vrefresh = 120, }, {0xB, 0xA},
		   {0x85, 0x85, 0x89, 0x8A, 0xC1, 0xC1} },
	/* 6G: 3840x2160@30Hz */
	{{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 30, }, {0x7, 0x6},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 6G: 3840x2160@25Hz */
	{{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4896,
		   4984, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 25, }, {0x5, 0x5},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 6G: 3840x2160@24Hz */
	{{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 5116,
		   5204, 5500, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 24, }, {0x3, 0x2},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 6G: 4096x2160@24Hz */
	{{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 296704, 4096, 5116,
		   5204, 5500, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 24, }, {0x3, 0x2},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 6G: 4096x2160@25Hz */
	{{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 297000, 4096, 5064,
		   5152, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 25, }, {0x5, 0x5},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 6G: 4096x2160@30Hz */
	{{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 296704, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 30, }, {0x7, 0x6},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 12G: 3840x2160@48Hz */
	{{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 5116,
		   5204, 5500, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 48, }, {0x8, 0x4},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 12G: 3840x2160@50Hz */
	{{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4896,
		   4984, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 50, }, {0x9, 0x9},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 12G: 3840x2160@60Hz */
	{{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 60, }, {0xB, 0xA},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 12G: 4096x2160@48Hz */
	{{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 5116,
		   5204, 5500, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 48, }, {0x8, 0x4},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 12G: 4096x2160@50Hz */
	{{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 5064,
		   5152, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 50, }, {0x9, 0x9},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
	/* 12G: 4096x2160@60Hz */
	{{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 593408, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
		   .vrefresh = 60, }, {0xB, 0xA},
		   {0x98, 0x98, 0x97, 0x98, 0xC0, 0xCE} },
};

#define connector_to_sdi(c) container_of(c, struct xilinx_sdi, connector)
#define encoder_to_sdi(e) container_of(e, struct xilinx_sdi, encoder)

/**
 * xilinx_sdi_writel - Memory mapped SDI Tx register write
 * @base:	Pointer to SDI Tx registers base
 * @offset:	Register offset
 * @val:	value to be written
 *
 * This function writes the value to SDI TX registers
 */
static inline void xilinx_sdi_writel(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

/**
 * xilinx_sdi_readl - Memory mapped SDI Tx register read
 * @base:	Pointer to SDI Tx registers base
 * @offset:	Register offset
 *
 * Return: The contents of the SDI Tx register
 *
 * This function returns the contents of the corresponding SDI Tx register.
 */
static inline u32 xilinx_sdi_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

/**
 * xilinx_en_axi4s - Enable SDI Tx AXI4S-to-Video core
 * @sdi:	Pointer to SDI Tx structure
 *
 * This function enables the SDI Tx AXI4S-to-Video core.
 */
static void xilinx_en_axi4s(struct xilinx_sdi *sdi)
{
	u32 data;

	data = xilinx_sdi_readl(sdi->base, XSDI_TX_RST_CTRL);
	data |= XSDI_TX_AXI4S_CTRL_EN_MASK;
	xilinx_sdi_writel(sdi->base, XSDI_TX_RST_CTRL, data);
}

/**
 * xilinx_en_bridge - Enable SDI Tx bridge
 * @sdi:	Pointer to SDI Tx structure
 *
 * This function enables the SDI Tx bridge.
 */
static void xilinx_en_bridge(struct xilinx_sdi *sdi)
{
	u32 data;

	data = xilinx_sdi_readl(sdi->base, XSDI_TX_RST_CTRL);
	data |= XSDI_TX_BRIDGE_CTRL_EN_MASK;
	xilinx_sdi_writel(sdi->base, XSDI_TX_RST_CTRL, data);
}

/**
 * xilinx_sdi_set_default_drm_properties - Configure SDI DRM
 * properties with their default values
 * @sdi: SDI structure having the updated user parameters
 */
static void
xilinx_sdi_set_default_drm_properties(struct xilinx_sdi *sdi)
{
	drm_object_property_set_value(&sdi->connector.base,
				      sdi->sdi_mode, 0);
	drm_object_property_set_value(&sdi->connector.base,
				      sdi->sdi_data_strm, 0);
	drm_object_property_set_value(&sdi->connector.base,
				      sdi->is_frac_prop, 0);
}

/**
 * xilinx_sdi_irq_handler - SDI Tx interrupt
 * @irq:	irq number
 * @data:	irq data
 *
 * Return: IRQ_HANDLED for all cases.
 *
 * This is the compact GT ready interrupt.
 */
static irqreturn_t xilinx_sdi_irq_handler(int irq, void *data)
{
	struct xilinx_sdi *sdi = (struct xilinx_sdi *)data;
	u32 reg;

	reg = xilinx_sdi_readl(sdi->base, XSDI_TX_ISR_STAT);

	if (reg & XSDI_GTTX_RSTDONE_INTR_MASK)
		dev_dbg(sdi->dev, "GT reset interrupt received\n");
	if (reg & XSDI_TX_CE_ALIGN_ERR_INTR_MASK)
		dev_err_ratelimited(sdi->dev, "SDI SD CE align error\n");
	if (reg & XSDI_OVERFLOW_INTR_MASK)
		dev_err_ratelimited(sdi->dev, "AXI-4 Stream Overflow error\n");
	if (reg & XSDI_UNDERFLOW_INTR_MASK)
		dev_err_ratelimited(sdi->dev, "AXI-4 Stream Underflow error\n");
	xilinx_sdi_writel(sdi->base, XSDI_TX_ISR_STAT,
			  reg & ~(XSDI_AXI4S_VID_LOCK_INTR_MASK));

	reg = xilinx_sdi_readl(sdi->base, XSDI_TX_STS_SB_TDATA);
	if (reg & XSDI_TX_TDATA_GT_RESETDONE_MASK) {
		sdi->event_received = true;
		wake_up_interruptible(&sdi->wait_event);
	}
	return IRQ_HANDLED;
}

/**
 * xilinx_sdi_set_payload_line - set ST352 packet line number
 * @sdi:	Pointer to SDI Tx structure
 * @line_1:	line number used to insert st352 packet for field 1.
 * @line_2:	line number used to insert st352 packet for field 2.
 *
 * This function set 352 packet line number.
 */
static void xilinx_sdi_set_payload_line(struct xilinx_sdi *sdi,
					u32 line_1, u32 line_2)
{
	u32 data;

	data = ((line_1 & XSDI_TX_ST352_LINE_MASK) |
		((line_2 & XSDI_TX_ST352_LINE_MASK) <<
		XSDI_TX_ST352_LINE_F2_SHIFT));

	xilinx_sdi_writel(sdi->base, XSDI_TX_ST352_LINE, data);

	data = xilinx_sdi_readl(sdi->base, XSDI_TX_MDL_CTRL);
	data |= (1 << XSDI_TX_CTRL_ST352_F2_EN_SHIFT);

	xilinx_sdi_writel(sdi->base, XSDI_TX_MDL_CTRL, data);
}

/**
 * xilinx_sdi_set_payload_data - set ST352 packet payload
 * @sdi:		Pointer to SDI Tx structure
 * @data_strm:		data stream number
 * @payload:		st352 packet payload
 *
 * This function set ST352 payload data to corresponding stream.
 */
static void xilinx_sdi_set_payload_data(struct xilinx_sdi *sdi,
					u32 data_strm, u32 payload)
{
	xilinx_sdi_writel(sdi->base,
			  (XSDI_TX_ST352_DATA_CH0 + (data_strm * 4)), payload);
}

/**
 * xilinx_sdi_set_display_disable - Disable the SDI Tx IP core enable
 * register bit
 * @sdi: SDI structure having the updated user parameters
 *
 * This function takes the SDI strucure and disables the core enable bit
 * of core configuration register.
 */
static void xilinx_sdi_set_display_disable(struct xilinx_sdi *sdi)
{
	u32 i;

	for (i = 0; i < SDI_MAX_DATASTREAM; i++)
		xilinx_sdi_set_payload_data(sdi, i, 0);

	xilinx_sdi_writel(sdi->base, XSDI_TX_GLBL_IER, 0);
	xilinx_sdi_writel(sdi->base, XSDI_TX_RST_CTRL, 0);
}

/**
 * xilinx_sdi_payload_config -  config the SDI payload parameters
 * @sdi:	pointer Xilinx SDI Tx structure
 * @mode:	display mode
 *
 * This function config the SDI st352 payload parameter.
 */
static void xilinx_sdi_payload_config(struct xilinx_sdi *sdi, u32 mode)
{
	u32 payload_1, payload_2;

	switch (mode) {
	case XSDI_MODE_SD:
		payload_1 = PAYLD_LN1_SDPAL;
		payload_2 = PAYLD_LN2_SDPAL;
		break;
	case XSDI_MODE_HD:
	case XSDI_MODE_3GA:
	case XSDI_MODE_3GB:
	case XSDI_MODE_6G:
	case XSDI_MODE_12G:
		payload_1 = PAYLD_LN1_HD_3_6_12G;
		payload_2 = PAYLD_LN2_HD_3_6_12G;
		break;
	default:
		payload_1 = 0;
		payload_2 = 0;
		break;
	}

	xilinx_sdi_set_payload_line(sdi, payload_1, payload_2);
}

/**
 * xilinx_set_sdi_mode -  Set mode parameters in SDI Tx
 * @sdi:	pointer Xilinx SDI Tx structure
 * @mode:	SDI Tx display mode
 * @is_frac:	0 - integer 1 - fractional
 * @mux_ptrn:	specifiy the data stream interleaving pattern to be used
 * This function config the SDI st352 payload parameter.
 */
static void xilinx_set_sdi_mode(struct xilinx_sdi *sdi, u32 mode,
				bool is_frac, u32 mux_ptrn)
{
	u32 data;

	xilinx_sdi_payload_config(sdi, mode);

	data = xilinx_sdi_readl(sdi->base, XSDI_TX_MDL_CTRL);
	data &= ~((XSDI_TX_CTRL_MODE_MASK << XSDI_TX_CTRL_MODE_SHIFT) |
		(XSDI_TX_CTRL_M_MASK) | (XSDI_TX_CTRL_MUX_MASK
		<< XSDI_TX_CTRL_MUX_SHIFT));

	data |= (((mode & XSDI_TX_CTRL_MODE_MASK)
		<< XSDI_TX_CTRL_MODE_SHIFT) |
		(is_frac  << XSDI_TX_CTRL_M_SHIFT) |
		((mux_ptrn & XSDI_TX_CTRL_MUX_MASK) << XSDI_TX_CTRL_MUX_SHIFT));

	xilinx_sdi_writel(sdi->base, XSDI_TX_MDL_CTRL, data);
}

/**
 * xilinx_sdi_set_config_parameters - Configure SDI Tx registers with parameters
 * given from user application.
 * @sdi: SDI structure having the updated user parameters
 *
 * This function takes the SDI structure having drm_property parameters
 * configured from  user application and writes them into SDI IP registers.
 */
static void xilinx_sdi_set_config_parameters(struct xilinx_sdi *sdi)
{
	u32 mode;
	int mux_ptrn = INVALID_VALUE;
	bool is_frac;

	mode = sdi->sdi_mod_prop_val;
	is_frac = sdi->is_frac_prop_val;

	switch (mode) {
	case XSDI_MODE_3GA:
		mux_ptrn = XSDI_TX_MUX_SD_HD_3GA;
		break;
	case XSDI_MODE_3GB:
		mux_ptrn = XSDI_TX_MUX_3GB;
		break;
	case XSDI_MODE_6G:
		if (sdi->sdi_data_strm_prop_val == 4)
			mux_ptrn = XSDI_TX_MUX_4STREAM_6G;
		else if (sdi->sdi_data_strm_prop_val == 8)
			mux_ptrn = XSDI_TX_MUX_8STREAM_6G_12G;
		break;
	case XSDI_MODE_12G:
		if (sdi->sdi_data_strm_prop_val == 8)
			mux_ptrn = XSDI_TX_MUX_8STREAM_6G_12G;
		break;
	default:
		mux_ptrn = 0;
		break;
	}
	if (mux_ptrn == INVALID_VALUE) {
		dev_err(sdi->dev, "%d data stream not supported for %d mode",
			sdi->sdi_data_strm_prop_val, mode);
		return;
	}
	xilinx_set_sdi_mode(sdi, mode, is_frac, mux_ptrn);
}

/**
 * xilinx_sdi_connector_set_property - implementation of drm_connector_funcs
 * set_property invoked by IOCTL call to DRM_IOCTL_MODE_OBJ_SETPROPERTY
 *
 * @base_connector: pointer Xilinx SDI connector
 * @property: pointer to the drm_property structure
 * @value: SDI parameter value that is configured from user application
 *
 * This function takes a drm_property name and value given from user application
 * and update the SDI structure property varabiles with the values.
 * These values are later used to configure the SDI Rx IP.
 *
 * Return: 0 on success OR -EINVAL if setting property fails
 */
static int
xilinx_sdi_connector_set_property(struct drm_connector *base_connector,
				  struct drm_property *property,
				  u64 value)
{
	struct xilinx_sdi *sdi = connector_to_sdi(base_connector);

	if (property == sdi->sdi_mode)
		sdi->sdi_mod_prop_val = (unsigned int)value;
	else if (property == sdi->sdi_data_strm)
		sdi->sdi_data_strm_prop_val = (unsigned int)value;
	else if (property == sdi->is_frac_prop)
		sdi->is_frac_prop_val = !!value;
	else
		return -EINVAL;
	return 0;
}

/**
 * xilinx_sdi_get_mode_id - Search for a video mode in the supported modes table
 *
 * @mode: mode being searched
 *
 * Return: true if mode is found
 */
static int xilinx_sdi_get_mode_id(struct drm_display_mode *mode)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xlnx_sdi_modes); i++)
		if (drm_mode_equal(&xlnx_sdi_modes[i].mode, mode))
			return i;
	return -EINVAL;
}

/**
 * xilinx_sdi_drm_add_modes - Adds SDI supported modes
 * @connector: pointer Xilinx SDI connector
 *
 * Return:	Count of modes added
 *
 * This function adds the SDI modes supported and returns its count
 */
static int xilinx_sdi_drm_add_modes(struct drm_connector *connector)
{
	int i, num_modes = 0;
	struct drm_display_mode *mode;
	struct drm_device *dev = connector->dev;

	for (i = 0; i < ARRAY_SIZE(xlnx_sdi_modes); i++) {
		const struct drm_display_mode *ptr = &xlnx_sdi_modes[i].mode;

		mode = drm_mode_duplicate(dev, ptr);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
	}
	return num_modes;
}

static int xilinx_sdi_connector_dpms(struct drm_connector *connector,
				     int mode)
{
	return drm_helper_connector_dpms(connector, mode);
}

static enum drm_connector_status
xilinx_sdi_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void xilinx_sdi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	connector->dev = NULL;
}

static const struct drm_connector_funcs xilinx_sdi_connector_funcs = {
	.dpms = xilinx_sdi_connector_dpms,
	.detect = xilinx_sdi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = xilinx_sdi_connector_destroy,
	.set_property = xilinx_sdi_connector_set_property,
};

static struct drm_encoder *
xilinx_sdi_best_encoder(struct drm_connector *connector)
{
	return &(connector_to_sdi(connector)->encoder);
}

static int xilinx_sdi_get_modes(struct drm_connector *connector)
{
	return xilinx_sdi_drm_add_modes(connector);
}

static struct drm_connector_helper_funcs xilinx_sdi_connector_helper_funcs = {
	.get_modes = xilinx_sdi_get_modes,
	.best_encoder = xilinx_sdi_best_encoder,
};

/**
 * xilinx_sdi_drm_connector_create_property -  create SDI connector properties
 *
 * @base_connector: pointer to Xilinx SDI connector
 *
 * This function takes the xilinx SDI connector component and defines
 * the drm_property variables with their default values.
 */
static void
xilinx_sdi_drm_connector_create_property(struct drm_connector *base_connector)
{
	struct drm_device *dev = base_connector->dev;
	struct xilinx_sdi *sdi  = connector_to_sdi(base_connector);

	sdi->is_frac_prop = drm_property_create_bool(dev, 1, "is_frac");
	sdi->sdi_mode = drm_property_create_range(dev, 0,
			"sdi_mode", 0, 5);
	sdi->sdi_data_strm = drm_property_create_range(dev, 0,
			"sdi_data_stream", 2, 8);
}

/**
 * xilinx_sdi_drm_connector_attach_property -  attach SDI connector
 * properties
 *
 * @base_connector: pointer to Xilinx SDI connector
 */
static void
xilinx_sdi_drm_connector_attach_property(struct drm_connector *base_connector)
{
	struct xilinx_sdi *sdi = connector_to_sdi(base_connector);
	struct drm_mode_object *obj = &base_connector->base;

	if (sdi->sdi_mode)
		drm_object_attach_property(obj, sdi->sdi_mode, 0);

	if (sdi->sdi_data_strm)
		drm_object_attach_property(obj, sdi->sdi_data_strm, 0);

	if (sdi->is_frac_prop)
		drm_object_attach_property(obj, sdi->is_frac_prop, 0);
}

static int xilinx_sdi_create_connector(struct drm_encoder *encoder)
{
	struct xilinx_sdi *sdi = encoder_to_sdi(encoder);
	struct drm_connector *connector = &sdi->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;
	connector->interlace_allowed = true;
	connector->doublescan_allowed = true;

	ret = drm_connector_init(encoder->dev, connector,
				 &xilinx_sdi_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret) {
		dev_err(sdi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &xilinx_sdi_connector_helper_funcs);
	drm_connector_register(connector);
	drm_connector_attach_encoder(connector, encoder);
	xilinx_sdi_drm_connector_create_property(connector);
	xilinx_sdi_drm_connector_attach_property(connector);

	return 0;
}

static bool xilinx_sdi_mode_fixup(struct drm_encoder *encoder,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

/**
 * xilinx_sdi_set_display_enable - Enables the SDI Tx IP core enable
 * register bit
 * @sdi: SDI structure having the updated user parameters
 *
 * This function takes the SDI strucure and enables the core enable bit
 * of core configuration register.
 */
static void xilinx_sdi_set_display_enable(struct xilinx_sdi *sdi)
{
	u32 data;

	data = xilinx_sdi_readl(sdi->base, XSDI_TX_RST_CTRL);
	data |= XSDI_TX_CTRL_EN_MASK;
	/* start sdi stream */
	xilinx_sdi_writel(sdi->base, XSDI_TX_RST_CTRL, data);
}

static void xilinx_sdi_encoder_dpms(struct drm_encoder *encoder,
				    int mode)
{
	struct xilinx_sdi *sdi = encoder_to_sdi(encoder);

	dev_dbg(sdi->dev, "encoder dpms state: %d\n", mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		xilinx_sdi_set_display_enable(sdi);
		return;
	default:
		xilinx_sdi_set_display_disable(sdi);
		xilinx_sdi_set_default_drm_properties(sdi);
	}
}

/**
 * xilinx_sdi_calc_st352_payld -  calculate the st352 payload
 *
 * @sdi: pointer to SDI Tx structure
 * @mode: DRM display mode
 *
 * This function calculates the st352 payload to be configured.
 * Please refer to SMPTE ST352 documents for it.
 * Return:	return st352 payload
 */
static u32 xilinx_sdi_calc_st352_payld(struct xilinx_sdi *sdi,
				       struct drm_display_mode *mode)
{
	u8 byt1, byt2;
	u16 is_p;
	u32 id, sdi_mode = sdi->sdi_mod_prop_val;
	bool is_frac = sdi->is_frac_prop_val;
	u32 byt3 = ST352_BYTE3;

	id = xilinx_sdi_get_mode_id(mode);
	dev_dbg(sdi->dev, "mode id: %d\n", id);
	if (mode->hdisplay == 2048 || mode->hdisplay == 4096)
		byt3 |= XST352_2048_SHIFT;
	/* byte 2 calculation */
	is_p = !(mode->flags & DRM_MODE_FLAG_INTERLACE);
	byt2 = xlnx_sdi_modes[id].st352_byt2[is_frac];
	if ((sdi_mode == XSDI_MODE_3GB) ||
	    (mode->flags & DRM_MODE_FLAG_DBLSCAN) || is_p)
		byt2 |= XST352_PROG_PIC_MASK;
	if (is_p && (mode->vtotal >= 1125))
		byt2 |= XST352_PROG_TRANS_MASK;

	/* byte 1 calculation */
	byt1 = xlnx_sdi_modes[id].st352_byt1[sdi_mode];

	return (ST352_BYTE4 << 24 | byt3 << 16 | byt2 << 8 | byt1);
}

/**
 * xilinx_sdi_mode_set -  drive the SDI timing parameters
 *
 * @encoder: pointer to Xilinx DRM encoder
 * @mode: DRM kernel-internal display mode structure
 * @adjusted_mode: SDI panel timing parameters
 *
 * This function derives the SDI IP timing parameters from the timing
 * values given by VTC driver.
 */
static void xilinx_sdi_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct xilinx_sdi *sdi = encoder_to_sdi(encoder);
	struct videomode vm;
	u32 payload, i;

	xilinx_sdi_set_config_parameters(sdi);

	/* set st352 payloads */
	payload = xilinx_sdi_calc_st352_payld(sdi, adjusted_mode);
	dev_dbg(sdi->dev, "payload : %0x\n", payload);

	for (i = 0; i < sdi->sdi_data_strm_prop_val / 2; i++) {
		if (sdi->sdi_mod_prop_val == XSDI_MODE_3GB)
			payload |= (i << 1) << XSDI_CH_SHIFT;
		xilinx_sdi_set_payload_data(sdi, i, payload);
	}

	/* UHDSDI is fixed 2 pixels per clock, horizontal timings div by 2 */
	vm.hactive = adjusted_mode->hdisplay / PIXELS_PER_CLK;
	vm.hfront_porch = (adjusted_mode->hsync_start -
			  adjusted_mode->hdisplay) / PIXELS_PER_CLK;
	vm.hback_porch = (adjusted_mode->htotal -
			 adjusted_mode->hsync_end) / PIXELS_PER_CLK;
	vm.hsync_len = (adjusted_mode->hsync_end -
		       adjusted_mode->hsync_start) / PIXELS_PER_CLK;

	vm.vactive = adjusted_mode->vdisplay;
	vm.vfront_porch = adjusted_mode->vsync_start -
			  adjusted_mode->vdisplay;
	vm.vback_porch = adjusted_mode->vtotal -
			 adjusted_mode->vsync_end;
	vm.vsync_len = adjusted_mode->vsync_end -
		       adjusted_mode->vsync_start;
	vm.flags = 0;
	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		vm.flags |= DISPLAY_FLAGS_INTERLACED;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		vm.flags |= DISPLAY_FLAGS_HSYNC_LOW;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		vm.flags |= DISPLAY_FLAGS_VSYNC_LOW;

	xilinx_vtc_config_sig(sdi->vtc, &vm);
}

static void xilinx_sdi_prepare(struct drm_encoder *encoder)
{
	struct xilinx_sdi *sdi = encoder_to_sdi(encoder);
	u32 reg;

	dev_dbg(sdi->dev, "%s\n", __func__);

	reg = xilinx_sdi_readl(sdi->base, XSDI_TX_MDL_CTRL);
	reg |= XSDI_TX_CTRL_INS_CRC_MASK | XSDI_TX_CTRL_INS_ST352_MASK |
		XSDI_TX_CTRL_OVR_ST352_MASK | XSDI_TX_CTRL_INS_SYNC_BIT_MASK |
		XSDI_TX_CTRL_INS_EDH_MASK;
	xilinx_sdi_writel(sdi->base, XSDI_TX_MDL_CTRL, reg);
	xilinx_sdi_writel(sdi->base, XSDI_TX_IER_STAT, XSDI_IER_EN_MASK);
	xilinx_sdi_writel(sdi->base, XSDI_TX_GLBL_IER, 1);
	xilinx_vtc_reset(sdi->vtc);
}

static void xilinx_sdi_commit(struct drm_encoder *encoder)
{
	struct xilinx_sdi *sdi = encoder_to_sdi(encoder);
	u32 ret;

	dev_dbg(sdi->dev, "%s\n", __func__);
	xilinx_sdi_encoder_dpms(encoder, DRM_MODE_DPMS_ON);

	ret = wait_event_interruptible_timeout(sdi->wait_event,
					       sdi->event_received,
					       usecs_to_jiffies(GT_TIMEOUT));
	if (!ret) {
		dev_err(sdi->dev, "Timeout: GT interrupt not received\n");
		return;
	}
	sdi->event_received = false;
	/* enable sdi bridge, vtc and Axi4s_vid_out_ctrl */
	xilinx_en_bridge(sdi);
	xilinx_vtc_enable(sdi->vtc);
	xilinx_en_axi4s(sdi);
}

static const struct drm_encoder_helper_funcs xilinx_sdi_encoder_helper_funcs = {
	.dpms = xilinx_sdi_encoder_dpms,
	.mode_fixup = xilinx_sdi_mode_fixup,
	.mode_set = xilinx_sdi_mode_set,
	.prepare = xilinx_sdi_prepare,
	.commit = xilinx_sdi_commit,
};

static const struct drm_encoder_funcs xilinx_sdi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int xilinx_sdi_bind(struct device *dev, struct device *master,
			   void *data)
{
	struct xilinx_sdi *sdi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &sdi->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	/*
	 * TODO: The possible CRTCs are 1 now as per current implementation of
	 * SDI tx drivers. DRM framework can support more than one CRTCs and
	 * SDI driver can be enhanced for that.
	 */
	encoder->possible_crtcs = 1;

	drm_encoder_init(drm_dev, encoder, &xilinx_sdi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	drm_encoder_helper_add(encoder, &xilinx_sdi_encoder_helper_funcs);

	ret = xilinx_sdi_create_connector(encoder);
	if (ret) {
		dev_err(sdi->dev, "fail creating connector, ret = %d\n", ret);
		drm_encoder_cleanup(encoder);
	}
	return ret;
}

static void xilinx_sdi_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct xilinx_sdi *sdi = dev_get_drvdata(dev);

	xilinx_sdi_encoder_dpms(&sdi->encoder, DRM_MODE_DPMS_OFF);
	drm_encoder_cleanup(&sdi->encoder);
	drm_connector_cleanup(&sdi->connector);
}

static const struct component_ops xilinx_sdi_component_ops = {
	.bind	= xilinx_sdi_bind,
	.unbind	= xilinx_sdi_unbind,
};

static irqreturn_t xilinx_sdi_vblank_handler(int irq, void *data)
{
	struct xilinx_sdi *sdi = (struct xilinx_sdi *)data;
	u32 intr = xilinx_vtc_intr_get(sdi->vtc);

	if (!intr)
		return IRQ_NONE;

	if (sdi->vblank_fn)
		sdi->vblank_fn(sdi->vblank_data);

	xilinx_vtc_intr_clear(sdi->vtc, intr);
	return IRQ_HANDLED;
}

/**
 * xilinx_drm_sdi_enable_vblank - Enable the vblank handling
 * @sdi: SDI subsystem
 * @vblank_fn: callback to be called on vblank event
 * @vblank_data: data to be used in @vblank_fn
 *
 * This function register the vblank handler, and the handler will be triggered
 * on vblank event after.
 */
void xilinx_drm_sdi_enable_vblank(struct xilinx_sdi *sdi,
				  void (*vblank_fn)(void *),
				  void *vblank_data)
{
	sdi->vblank_fn = vblank_fn;
	sdi->vblank_data = vblank_data;
	xilinx_vtc_vblank_enable(sdi->vtc);
}
EXPORT_SYMBOL_GPL(xilinx_drm_sdi_enable_vblank);

/**
 * xilinx_drm_sdi_disable_vblank - Disable the vblank handling
 * @sdi: SDI subsystem
 *
 * Disable the vblank handler. The vblank handler and data are unregistered.
 */
void xilinx_drm_sdi_disable_vblank(struct xilinx_sdi *sdi)
{
	sdi->vblank_fn = NULL;
	sdi->vblank_data = NULL;
	xilinx_vtc_vblank_disable(sdi->vtc);
}

/**
 * xilinx_sdi_register_device - Register the SDI subsystem to the global list
 * @sdi: SDI subsystem
 *
 * Register the SDI subsystem instance to the global list
 */
static void xilinx_sdi_register_device(struct xilinx_sdi *sdi)
{
	mutex_lock(&xilinx_sdi_lock);
	list_add_tail(&sdi->list, &xilinx_sdi_list);
	mutex_unlock(&xilinx_sdi_lock);
}

/**
 * xilinx_drm_sdi_of_get - Get the SDI subsystem instance
 * @np: parent device node
 *
 * This function searches and returns a SDI subsystem structure for
 * the parent device node, @np. The SDI subsystem node should be a child node
 * of @np, with 'xlnx,sdi' property pointing to the SDI device node.
 * An instance can be shared by multiple users.
 *
 * Return: corresponding SDI subsystem structure if found. NULL if
 * the device node doesn't have 'xlnx,sdi' property, or -EPROBE_DEFER error
 * pointer if the the SDI subsystem isn't found.
 */
struct xilinx_sdi *xilinx_drm_sdi_of_get(struct device_node *np)
{
	struct xilinx_sdi *found = NULL;
	struct xilinx_sdi *sdi;
	struct device_node *sdi_node;

	if (!of_find_property(np, "xlnx,sdi", NULL))
		return NULL;

	sdi_node = of_parse_phandle(np, "xlnx,sdi", 0);
	if (!sdi_node)
		return ERR_PTR(-EINVAL);

	mutex_lock(&xilinx_sdi_lock);
	list_for_each_entry(sdi, &xilinx_sdi_list, list) {
		if (sdi->dev->of_node == sdi_node) {
			found = sdi;
			break;
		}
	}
	mutex_unlock(&xilinx_sdi_lock);

	of_node_put(sdi_node);
	if (!found)
		return ERR_PTR(-EPROBE_DEFER);
	return found;
}

/**
 * xilinx_sdi_unregister_device - Unregister the SDI subsystem instance
 * @sdi: SDI subsystem
 *
 * Unregister the SDI subsystem instance from the global list
 */
static void xilinx_sdi_unregister_device(struct xilinx_sdi *sdi)
{
	mutex_lock(&xilinx_sdi_lock);
	list_del(&sdi->list);
	mutex_unlock(&xilinx_sdi_lock);
}

static int xilinx_sdi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct xilinx_sdi *sdi;
	struct device_node *vtc_node;
	int ret, irq;

	sdi = devm_kzalloc(dev, sizeof(*sdi), GFP_KERNEL);
	if (!sdi)
		return -ENOMEM;

	sdi->dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sdi->base = devm_ioremap_resource(dev, res);

	if (IS_ERR(sdi->base)) {
		dev_err(dev, "failed to remap io region\n");
		return PTR_ERR(sdi->base);
	}
	platform_set_drvdata(pdev, sdi);

	vtc_node = of_parse_phandle(sdi->dev->of_node, "xlnx,vtc", 0);
	if (!vtc_node) {
		dev_err(dev, "vtc node not present\n");
		return PTR_ERR(vtc_node);
	}
	sdi->vtc = xilinx_vtc_probe(sdi->dev, vtc_node);
	of_node_put(vtc_node);
	if (IS_ERR(sdi->vtc)) {
		dev_err(dev, "failed to probe VTC\n");
		return PTR_ERR(sdi->vtc);
	}

	/* disable interrupt */
	xilinx_sdi_writel(sdi->base, XSDI_TX_GLBL_IER, 0);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(sdi->dev, irq, NULL,
					xilinx_sdi_irq_handler, IRQF_ONESHOT,
					dev_name(sdi->dev), sdi);
	if (ret < 0)
		return ret;

	irq = platform_get_irq(pdev, 1); /* vblank interrupt */
	if (irq < 0)
		return irq;
	ret = devm_request_threaded_irq(sdi->dev, irq, NULL,
					xilinx_sdi_vblank_handler, IRQF_ONESHOT,
					"sdiTx-vblank", sdi);
	if (ret < 0)
		return ret;

	init_waitqueue_head(&sdi->wait_event);
	sdi->event_received = false;

	xilinx_sdi_register_device(sdi);
	return component_add(dev, &xilinx_sdi_component_ops);
}

static int xilinx_sdi_remove(struct platform_device *pdev)
{
	struct xilinx_sdi *sdi = platform_get_drvdata(pdev);

	xilinx_sdi_unregister_device(sdi);
	component_del(&pdev->dev, &xilinx_sdi_component_ops);

	return 0;
}

static const struct of_device_id xilinx_sdi_of_match[] = {
	{ .compatible = "xlnx,v-smpte-uhdsdi-tx-ss"},
	{ }
};
MODULE_DEVICE_TABLE(of, xilinx_sdi_of_match);

static struct platform_driver sdi_tx_driver = {
	.probe = xilinx_sdi_probe,
	.remove = xilinx_sdi_remove,
	.driver = {
		.name = "xlnx,uhdsdi-tx",
		.of_match_table = xilinx_sdi_of_match,
	},
};

module_platform_driver(sdi_tx_driver);

MODULE_AUTHOR("Saurabh Sengar <saurabhs@xilinx.com>");
MODULE_DESCRIPTION("Xilinx FPGA SDI Tx Driver");
MODULE_LICENSE("GPL v2");

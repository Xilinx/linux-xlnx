// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDMI 2.1 Rx Subsystem driver
 *
 * Copyright (c) 2021 Xilinx
 * Author: Vishal Sagar <vishal.sagar@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <media/media-entity.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>
#include "xilinx-hdmirx-hw.h"

#define XHDMI_MAX_LANES		(4)
#define XEDID_BLOCKS_MAX	(10)
#define XEDID_BLOCK_SIZE	(128)

#define AXILITE_FREQ		clk_get_rate(xhdmi->clks[0].clk)
#define TIME_10MS		(AXILITE_FREQ / 100)
#define TIME_16MS		(AXILITE_FREQ * 10 / 625)
#define TIME_200MS		(AXILITE_FREQ / 5)

#define MAX_VID_PROP_TRIES	7
#define MAX_FIELDS		2
#define COREPIXPERCLK		4

enum xhdmirx_stream_state {
	XSTREAM_IDLE = 0,
	XSTREAM_INIT = 1,
	XSTREAM_LOCK = 2,
	XSTREAM_ARM = 3,
	XSTREAM_UP = 4,
	XSTREAM_DOWN = 5,
	XSTREAM_MAX_STATE = 6,
};

enum xhdmirx_syncstatus {
	XSYNCSTAT_SYNC_LOSS = 0,
	XSYNCSTAT_SYNC_EST = 1,
};

enum xcolorspace {
	XCS_RGB = 0,
	XCS_YUV422 = 1,
	XCS_YUV444 = 2,
	XCS_YUV420 = 3,
};

enum xcolordepth {
	XCD_8 = 8,
	XCD_10 = 10,
	XCD_12 = 12,
	XCD_16 = 16,
};

/**
 * struct xtiming - Timing struct
 *
 * @hact: Horizontal Active
 * @htot: Horizontal Total
 * @hbp: Horizontal Backporch
 * @hfp: Horizontal Frontporch
 * @hsw: Horizontal Syncwidth
 * @vact: Vertical Active
 * @vtot: Vertical Total
 * @vfp: Vertical Frontporch
 * @vbp: Vertical Backporch
 * @vsw: Vertical Syncwidth
 * @vsyncpol: Vertical polarity
 * @hsyncpol: Horizontal polarity
 */
struct xtiming {
	u16 hact;
	u16 htot;
	u16 hbp;
	u16 hfp;
	u16 hsw;
	u16 vact;
	u16 vtot[MAX_FIELDS];
	u16 vfp[MAX_FIELDS];
	u16 vbp[MAX_FIELDS];
	u16 vsw[MAX_FIELDS];
	u8 vsyncpol;
	u8 hsyncpol;
};

/**
 * struct xvideostream - Video stream structure
 * @timing: stream timing struct
 * @colorspace: color space of incoming stream RGB/YUV 444/422/420
 * @colordepth: color depth 8/10/12/16 bpc
 * @framerate: Frame rate of stream
 * @isinterlaced: stream is interlaced or progressive
 */
struct xvideostream {
	struct xtiming timing;
	enum xcolorspace colorspace;
	enum xcolordepth colordepth;
	u32 framerate;
	bool isinterlaced;
};

/**
 * struct xstream - Stream structure
 * @video: video stream properties struct
 * @state: streaming state
 * @syncstatus: whether sync established or lost
 * @pixelclk: Pixel clock
 * @refclk: Reference clock from cable
 * @cable_connected: Flag if HDMI cable is connected
 * @isscrambled: Flag if stream is scrambled
 * @vic: AVI vic code
 * @getvidproptries: Number of tries to get video properties
 * @ishdmi: whether hdmi or dvi
 */
struct xstream {
	struct xvideostream video;
	enum xhdmirx_stream_state state;
	enum xhdmirx_syncstatus syncstatus;
	u32 pixelclk;
	u32 refclk;
	bool cable_connected;
	bool isscrambled;
	u8 vic;
	u8 getvidproptries;
	u8 ishdmi;
};

union xhdmi_auxheader {
	u32 data;
	u8 byte[4];
};

union xhdmi_auxdata {
	u32 data[8];
	u8 byte[32];
};

struct xhdmi_aux {
	union xhdmi_auxheader header;
	union xhdmi_auxdata data;
};

/**
 * struct xhdmirx_state - HDMI Rx driver state
 * @dev: Platform structure
 * @regs: Base address
 * @sd: V4L2 subdev structure
 * @pad: Media pad
 * @mbus_fmt: Detected media bus format
 * @dv_timings: Detected DV timings
 * @stream: struct to save stream properties
 * @aux: struct to save auxiliary packet
 * @xhdmi_mutex: Mutex to prevent concurrent access to state structure
 * @work_queue: Pointer to work queue for hot plug
 * @delayed_work_enable_hotplug: Delayed work to enable hotplug
 * @phy: array of phy structure pointers
 * @clks: array of clocks
 * @intrstatus: Array to save the interrupt status registers
 * @edid_user: User EDID
 * @edid_user_blocks: Number of blocks in user EDID
 * @edid_blocks_max: Max number of EDID blocks
 * @edid_ram_size: EDID RAM size in IP configuration
 * @max_ppc: Maximum input pixels per clock from IP configuration
 * @max_bpc: Maximum bit per component from IP configuration
 * @max_frl_rate: Maximum FRL rate supported from IP configuration
 * @hdmi_stream_up: hdmi stream is up or not
 * @isstreamup: flag whether stream is up
 */
struct xhdmirx_state {
	struct device *dev;
	void __iomem *regs;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt mbus_fmt;
	struct v4l2_dv_timings dv_timings;
	struct xstream stream;
	struct xhdmi_aux aux;
	/* mutex to prevent concurrent access to this structure */
	struct mutex xhdmi_mutex;
	struct workqueue_struct *work_queue;
	struct delayed_work delayed_work_enable_hotplug;
	struct phy *phy[XHDMI_MAX_LANES];
	struct clk_bulk_data *clks;
	u32 intrstatus[8];
	u8 *edid_user;
	int edid_user_blocks;
	int edid_blocks_max;
	u16 edid_ram_size;
	u8 max_ppc;
	u8 max_bpc;
	u8 max_frl_rate;
	u8 hdmi_stream_up;
	bool isstreamup;
};

static const char * const xhdmirx_clks[] = {
	"s_axi_cpu_aclk", "frl_clk", "s_axis_video_aclk",
};

/* Default Xilinx EDID */
static const u8 xilinx_edid[] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x61, 0x98, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
	0x0E, 0x1C, 0x01, 0x03, 0x80, 0xA0, 0x5A, 0x78,
	0x0A, 0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26,
	0x0F, 0x50, 0x54, 0x21, 0x08, 0x00, 0x71, 0x4F,
	0x81, 0xC0, 0x81, 0x00, 0x81, 0x80, 0x95, 0x00,
	0xA9, 0xC0, 0xB3, 0x00, 0x01, 0x01, 0x08, 0xE8,
	0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80, 0xB0, 0x58,
	0x8A, 0x00, 0x40, 0x84, 0x63, 0x00, 0x00, 0x1E,
	0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40,
	0x58, 0x2C, 0x45, 0x00, 0x40, 0x84, 0x63, 0x00,
	0x00, 0x1E, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x18,
	0x4B, 0x0F, 0x8C, 0x3C, 0x00, 0x0A, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC,
	0x00, 0x58, 0x49, 0x4C, 0x49, 0x4E, 0x58, 0x20,
	0x48, 0x44, 0x4D, 0x49, 0x0A, 0x20, 0x01, 0x85,

	0x02, 0x03, 0x3F, 0xF5, 0x57, 0x61, 0x10, 0x1F,
	0x04, 0x13, 0x05, 0x14, 0x20, 0x21, 0x22, 0x5D,
	0x5E, 0x5F, 0x60, 0x65, 0x66, 0x62, 0x63, 0x64,
	0x07, 0x16, 0x03, 0x12, 0x23, 0x0F, 0x07, 0x07,
	0x83, 0x6F, 0x00, 0x00, 0x6B, 0x03, 0x0C, 0x00,
	0x10, 0x00, 0x78, 0x3C, 0x20, 0x00, 0x20, 0x03,
	0x67, 0xD8, 0x5D, 0xC4, 0x01, 0x78, 0x80, 0x07,
	0xE3, 0x0F, 0x01, 0xE0, 0xE2, 0x00, 0xCF, 0x02,
	0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58,
	0x2C, 0x45, 0x00, 0x20, 0xC2, 0x31, 0x00, 0x00,
	0x1E, 0x08, 0xE8, 0x00, 0x30, 0xF2, 0x70, 0x5A,
	0x80, 0xB0, 0x58, 0x8A, 0x00, 0x20, 0xC2, 0x31,
	0x00, 0x00, 0x1E, 0x04, 0x74, 0x00, 0x30, 0xF2,
	0x70, 0x5A, 0x80, 0xB0, 0x58, 0x8A, 0x00, 0x20,
	0x52, 0x31, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDC
};

/* Event struct */
static const struct v4l2_event xhdmi_ev_fmt = {
	.type = V4L2_EVENT_SOURCE_CHANGE,
	.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
};

#define xhdmirx_piointr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_CTRL_CLR_OFFSET,\
		    HDMIRX_PIO_CTRL_IE_MASK)
#define xhdmirx_piointr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_CTRL_SET_OFFSET,\
		    HDMIRX_PIO_CTRL_IE_MASK)

#define xhdmirx_pio_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_CTRL_CLR_OFFSET,\
		    HDMIRX_PIO_CTRL_RUN_MASK)
#define xhdmirx_pio_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_CTRL_SET_OFFSET,\
		    HDMIRX_PIO_CTRL_RUN_MASK)

#define xhdmirx_tmr1_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_SET_OFFSET, \
		    HDMIRX_TMR1_CTRL_RUN_MASK)
#define xhdmirx_tmr1_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_CLR_OFFSET, \
		    HDMIRX_TMR1_CTRL_RUN_MASK)
#define xhdmirx_tmr1intr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_SET_OFFSET, \
		    HDMIRX_TMR1_CTRL_IE_MASK)
#define xhdmirx_tmr1intr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_CLR_OFFSET, \
		    HDMIRX_TMR1_CTRL_IE_MASK)
#define xhdmirx_tmr1_start(xhdmi, value) \
	xhdmi_write(xhdmi, HDMIRX_TMR_1_CNT_OFFSET, \
		    value)

#define xhdmirx_tmr2_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_SET_OFFSET, \
		    HDMIRX_TMR2_CTRL_RUN_MASK)
#define xhdmirx_tmr2_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_CLR_OFFSET, \
		    HDMIRX_TMR2_CTRL_RUN_MASK)
#define xhdmirx_tmr2intr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_SET_OFFSET, \
		    HDMIRX_TMR2_CTRL_IE_MASK)
#define xhdmirx_tmr2intr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_CLR_OFFSET, \
		    HDMIRX_TMR2_CTRL_IE_MASK)
#define xhdmirx_tmr2_start(xhdmi, value) \
	xhdmi_write(xhdmi, HDMIRX_TMR_2_CNT_OFFSET, \
		    value)

#define xhdmirx_tmr3_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_SET_OFFSET, \
		    HDMIRX_TMR3_CTRL_RUN_MASK)
#define xhdmirx_tmr3_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_CLR_OFFSET, \
		    HDMIRX_TMR3_CTRL_RUN_MASK)
#define xhdmirx_tmr3intr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_SET_OFFSET, \
		    HDMIRX_TMR3_CTRL_IE_MASK)
#define xhdmirx_tmr3intr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_CLR_OFFSET, \
		    HDMIRX_TMR3_CTRL_IE_MASK)
#define xhdmirx_tmr3_start(xhdmi, value) \
	xhdmi_write(xhdmi, HDMIRX_TMR_3_CNT_OFFSET, \
		    value)

#define xhdmirx_tmr4_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_SET_OFFSET, \
		    HDMIRX_TMR4_CTRL_RUN_MASK)
#define xhdmirx_tmr4_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_CLR_OFFSET, \
		    HDMIRX_TMR4_CTRL_RUN_MASK)
#define xhdmirx_tmr4intr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_SET_OFFSET, \
		    HDMIRX_TMR4_CTRL_IE_MASK)
#define xhdmirx_tmr4intr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_TMR_CTRL_CLR_OFFSET, \
		    HDMIRX_TMR4_CTRL_IE_MASK)
#define xhdmirx_tmr4_start(xhdmi, value) \
	xhdmi_write(xhdmi, HDMIRX_TMR_4_CNT_OFFSET, \
		    value)

#define xhdmirx_vtdintr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_VTD_CTRL_CLR_OFFSET, \
		    HDMIRX_VTD_CTRL_IE_MASK)
#define xhdmirx_vtdintr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_VTD_CTRL_SET_OFFSET, \
		    HDMIRX_VTD_CTRL_IE_MASK)
#define xhdmirx_vtd_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_VTD_CTRL_CLR_OFFSET, \
		    HDMIRX_VTD_CTRL_RUN_MASK)
#define xhdmirx_vtd_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_VTD_CTRL_SET_OFFSET, \
		    HDMIRX_VTD_CTRL_RUN_MASK)

#define xhdmirx_ddcintr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_DDC_CTRL_CLR_OFFSET, \
		    HDMIRX_DDC_CTRL_IE_MASK)
#define xhdmirx_ddcintr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_DDC_CTRL_SET_OFFSET, \
		    HDMIRX_DDC_CTRL_IE_MASK)
#define xhdmirx_ddc_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_DDC_CTRL_CLR_OFFSET, \
		    HDMIRX_DDC_CTRL_RUN_MASK)
#define xhdmirx_ddc_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_DDC_CTRL_SET_OFFSET, \
		    HDMIRX_DDC_CTRL_RUN_MASK)

#define xhdmirx_auxintr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_AUX_CTRL_CLR_OFFSET, \
		    HDMIRX_AUX_CTRL_IE_MASK)
#define xhdmirx_auxintr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_AUX_CTRL_SET_OFFSET, \
		    HDMIRX_AUX_CTRL_IE_MASK)
#define xhdmirx_aux_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_AUX_CTRL_CLR_OFFSET, \
		    HDMIRX_AUX_CTRL_RUN_MASK)
#define xhdmirx_aux_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_AUX_CTRL_SET_OFFSET, \
		    HDMIRX_AUX_CTRL_RUN_MASK)

#define xhdmirx_audintr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_AUD_CTRL_CLR_OFFSET, \
		    HDMIRX_AUD_CTRL_IE_MASK)
#define xhdmirx_audintr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_AUD_CTRL_SET_OFFSET, \
		    HDMIRX_AUD_CTRL_IE_MASK)
#define xhdmirx_aud_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_AUD_CTRL_CLR_OFFSET, \
		    HDMIRX_AUD_CTRL_RUN_MASK)
#define xhdmirx_aud_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_AUD_CTRL_SET_OFFSET, \
		    HDMIRX_AUD_CTRL_RUN_MASK)

#define xhdmirx_lnkstaintr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_LNKSTA_CTRL_CLR_OFFSET, \
		    HDMIRX_LNKSTA_CTRL_IE_MASK)
#define xhdmirx_lnkstaintr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_LNKSTA_CTRL_SET_OFFSET, \
		    HDMIRX_LNKSTA_CTRL_IE_MASK)
#define xhdmirx_lnksta_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_LNKSTA_CTRL_CLR_OFFSET, \
		    HDMIRX_LNKSTA_CTRL_RUN_MASK)
#define xhdmirx_lnksta_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_LNKSTA_CTRL_SET_OFFSET, \
		    HDMIRX_LNKSTA_CTRL_RUN_MASK)

#define xhdmirx_frlintr_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_FRL_CTRL_CLR_OFFSET, \
		    HDMIRX_FRL_CTRL_IE_MASK)
#define xhdmirx_frlintr_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_FRL_CTRL_SET_OFFSET, \
		    HDMIRX_FRL_CTRL_IE_MASK)
#define xhdmirx_frl_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_FRL_CTRL_SET_OFFSET, \
		    HDMIRX_FRL_CTRL_RSTN_MASK)
#define xhdmirx_frl_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_FRL_CTRL_CLR_OFFSET, \
		    HDMIRX_FRL_CTRL_RSTN_MASK)

#define xhdmirx_setfrl_vclkvckeratio(xhdmi, val) \
	xhdmi_write(xhdmi, HDMIRX_FRL_VCLK_VCKE_RATIO_OFFSET, val)

#define xhdmirx_skewlockevt_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_FRL_CTRL_SET_OFFSET, \
		    HDMIRX_FRL_CTRL_SKEW_EVT_EN_MASK)

#define xhdmirx_skewlockevt_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_FRL_CTRL_CLR_OFFSET, \
		    HDMIRX_FRL_CTRL_SKEW_EVT_EN_MASK)

#define xhdmirx_ddcscdc_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_DDC_CTRL_SET_OFFSET, \
		    HDMIRX_DDC_CTRL_SCDC_EN_MASK)

#define xhdmirx_rxcore_vrst_assert(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET, \
		    HDMIRX_PIO_OUT_INT_VRST_MASK)
#define xhdmirx_rxcore_vrst_deassert(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET, \
		    HDMIRX_PIO_OUT_INT_VRST_MASK)

#define xhdmirx_rxcore_lrst_assert(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET, \
		    HDMIRX_PIO_OUT_INT_LRST_MASK)
#define xhdmirx_rxcore_lrst_deassert(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET, \
		    HDMIRX_PIO_OUT_INT_LRST_MASK)

#define xhdmirx_sysrst_assert(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET, \
		    HDMIRX_PIO_OUT_EXT_SYSRST_MASK)
#define xhdmirx_sysrst_deassert(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET, \
		    HDMIRX_PIO_OUT_EXT_SYSRST_MASK)

#define xhdmirx_ext_vrst_assert(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET, \
		    HDMIRX_PIO_OUT_EXT_VRST_MASK)
#define xhdmirx_ext_vrst_deassert(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET, \
		    HDMIRX_PIO_OUT_EXT_VRST_MASK)

#define xhdmirx_link_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET, \
		    HDMIRX_PIO_OUT_LNK_EN_MASK)

#define xhdmirx_link_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET, \
		    HDMIRX_PIO_OUT_LNK_EN_MASK)

#define xhdmirx_video_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET, \
		    HDMIRX_PIO_OUT_VID_EN_MASK)

#define xhdmirx_video_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET, \
		    HDMIRX_PIO_OUT_VID_EN_MASK)

#define xhdmirx_axi4s_enable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET, \
		    HDMIRX_PIO_OUT_AXIS_EN_MASK)

#define xhdmirx_axi4s_disable(xhdmi) \
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET, \
		    HDMIRX_PIO_OUT_AXIS_EN_MASK)

static inline u32 xhdmi_read(struct xhdmirx_state *xhdmi, u32 addr)
{
	return ioread32(xhdmi->regs + addr);
}

static inline void xhdmi_write(struct xhdmirx_state *xhdmi,
			       u32 addr, u32 val)
{
	iowrite32(val, xhdmi->regs + addr);
}

static inline struct xhdmirx_state *to_xhdmirx_state(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xhdmirx_state, sd);
}

/**
 * xhdmirx_vtd_settimebase - Set the Video Timing Detector
 *
 * @xhdmi: pointer to driver state
 * @count: count value to set the timer to.
 */
static inline void xhdmirx_vtd_settimebase(struct xhdmirx_state *xhdmi, u32 count)
{
	u32 val = xhdmi_read(xhdmi, HDMIRX_VTD_CTRL_OFFSET);

	val &= ~HDMIRX_VTD_CTRL_TIMERBASE_MASK;
	val |= FIELD_PREP(HDMIRX_VTD_CTRL_TIMERBASE_MASK, count);
	xhdmi_write(xhdmi, HDMIRX_VTD_CTRL_OFFSET, val);
}

static inline void xhdmirx_scrambler_enable(struct xhdmirx_state *xhdmi)
{
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET,
		    HDMIRX_PIO_OUT_SCRM_MASK);
	xhdmi->stream.isscrambled = true;
}

static inline void xhdmirx_scrambler_disable(struct xhdmirx_state *xhdmi)
{
	xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET,
		    HDMIRX_PIO_OUT_SCRM_MASK);
	xhdmi->stream.isscrambled = false;
}

static inline void xhdmirx_ddcscdc_clear(struct xhdmirx_state *xhdmi)
{
	xhdmi_write(xhdmi, HDMIRX_DDC_CTRL_SET_OFFSET,
		    HDMIRX_DDC_CTRL_SCDC_CLR_MASK);
	/* add a usleep(50) here */
	xhdmi_write(xhdmi, HDMIRX_DDC_CTRL_CLR_OFFSET,
		    HDMIRX_DDC_CTRL_SCDC_CLR_MASK);
}

/**
 * xhdmirx_disable_allintr - Disable all the interrupt sources
 *
 * @xhdmi: pointer to driver state
 */
static void xhdmirx_disable_allintr(struct xhdmirx_state *xhdmi)
{
	xhdmirx_piointr_disable(xhdmi);
	xhdmirx_vtdintr_disable(xhdmi);
	xhdmirx_ddcintr_disable(xhdmi);
	xhdmirx_tmr1intr_disable(xhdmi);
	xhdmirx_tmr2intr_disable(xhdmi);
	xhdmirx_tmr3intr_disable(xhdmi);
	xhdmirx_tmr4intr_disable(xhdmi);
	xhdmirx_auxintr_disable(xhdmi);
	xhdmirx_audintr_disable(xhdmi);
	xhdmirx_frlintr_disable(xhdmi);
	xhdmirx_lnkstaintr_disable(xhdmi);
}

/**
 * xhdmirx_enable_allintr - Enable all the interrupt sources
 *
 * @xhdmi: pointer to driver state
 */
static void xhdmirx_enable_allintr(struct xhdmirx_state *xhdmi)
{
	xhdmirx_piointr_enable(xhdmi);
	xhdmirx_vtdintr_enable(xhdmi);
	xhdmirx_ddcintr_enable(xhdmi);
	xhdmirx_tmr1intr_enable(xhdmi);
	xhdmirx_tmr2intr_enable(xhdmi);
	xhdmirx_tmr3intr_enable(xhdmi);
	xhdmirx_tmr4intr_enable(xhdmi);
	xhdmirx_auxintr_enable(xhdmi);
	xhdmirx_frlintr_enable(xhdmi);
	xhdmirx_lnkstaintr_enable(xhdmi);
}

/**
 * xhdmirx1_bridgeyuv420 - Enable/disable YUV 420 in bridge
 *
 * @xhdmi: pointer to driver state
 * @flag: Flag to set or clear the bit
 */
static void xhdmirx1_bridgeyuv420(struct xhdmirx_state *xhdmi, bool flag)
{
	if (flag)
		xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET,
			    HDMIRX_PIO_OUT_BRIDGE_YUV420_MASK);
	else
		xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET,
			    HDMIRX_PIO_OUT_BRIDGE_YUV420_MASK);
}

/**
 * xhdmirx1_bridgepixeldrop - Enable/Disable pixel drop in bridge
 *
 * @xhdmi: pointer to driver state
 * @flag: Flag to set or clear
 */
static void xhdmirx1_bridgepixeldrop(struct xhdmirx_state *xhdmi, bool flag)
{
	if (flag)
		xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET,
			    HDMIRX_PIO_OUT_BRIDGE_PIXEL_MASK);
	else
		xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET,
			    HDMIRX_PIO_OUT_BRIDGE_PIXEL_MASK);
}

/**
 * xhdmirx1_start - Start the HDMI Rx by enabling the PIO
 *
 * @xhdmi: pointer to driver state
 */
static void xhdmirx1_start(struct xhdmirx_state *xhdmi)
{
	xhdmirx_pio_enable(xhdmi);
	xhdmirx_piointr_enable(xhdmi);
}

static void xhdmirx1_clearlinkstatus(struct xhdmirx_state *xhdmi)
{
	xhdmi_write(xhdmi, HDMIRX_LNKSTA_CTRL_SET_OFFSET, HDMIRX_LNKSTA_CTRL_ERR_CLR_MASK);
	xhdmi_write(xhdmi, HDMIRX_LNKSTA_CTRL_CLR_OFFSET, HDMIRX_LNKSTA_CTRL_ERR_CLR_MASK);
}

/**
 * xhdmirx_set_hpd - Toggle hot plug detect line
 * @xhdmi: pointer to driver state
 * @enable: Flag to assert or deassert the HPD line
 *
 * This function is used to toggle hot plug detect line to indicate to the
 * HDMI Tx about change in HDMI Rx.
 */
static void xhdmirx_set_hpd(struct xhdmirx_state *xhdmi, int enable)
{
	if (enable)
		xhdmi_write(xhdmi, HDMIRX_PIO_OUT_SET_OFFSET,
			    HDMIRX_PIO_OUT_HPD_MASK);
	else
		xhdmi_write(xhdmi, HDMIRX_PIO_OUT_CLR_OFFSET,
			    HDMIRX_PIO_OUT_HPD_MASK);
}

static inline bool xhdmirx1_isstreamconnected(struct xhdmirx_state *xhdmi)
{
	/* return the stream cable connected */
	return xhdmi->stream.cable_connected;
}

/**
 * xhdmirx1_gettmdsclkratio - Get the TMDS clock ratio
 * @xhdmi: pointer to the driver state
 *
 * Return: TMDS clock ratio 0 or 1
 */
static u32 xhdmirx1_gettmdsclkratio(struct xhdmirx_state *xhdmi)
{
	u32 val;

	/* Get TMDS clock ratio */
	val = xhdmi_read(xhdmi, HDMIRX_PIO_IN_OFFSET);
	val = FIELD_GET(HDMIRX_PIO_IN_SCDC_TMDS_CLOCK_RATIO_MASK, val);

	dev_dbg(xhdmi->dev, "Get TMDS Clk Ratio = %u\n", val);
	return val;
}

/**
 * xhdmirx1_getavi_vic - Get the HDMI VIC id
 * @xhdmi: pointer to driver state
 *
 * Return: Returns HDMI VIC id
 */
static u8 xhdmirx1_getavi_vic(struct xhdmirx_state *xhdmi)
{
	u32 val;

	val = xhdmi_read(xhdmi, HDMIRX_AUX_STA_OFFSET);
	val = FIELD_GET(HDMIRX_AUX_STA_AVI_VIC_MASK, val);

	dev_dbg_ratelimited(xhdmi->dev, "Get AVI VIC = %u\n", val);
	return (u8)val;
}

/**
 * xhdmirx1_getavi_colorspace - Get the colorspace of the incoming stream
 *
 * @xhdmi: pointer to driver state
 *
 * Returns: Colorspace of stream
 */
static enum xcolorspace xhdmirx1_getavi_colorspace(struct xhdmirx_state *xhdmi)
{
	u32 val;
	enum xcolorspace cs;

	dev_dbg_ratelimited(xhdmi->dev, "Get avi colorspace ");
	val = xhdmi_read(xhdmi, HDMIRX_AUX_STA_OFFSET);
	switch (FIELD_GET(HDMIRX_AUX_STA_AVI_CS_MASK, val)) {
	case 1:
		cs = XCS_YUV422;
		dev_dbg_ratelimited(xhdmi->dev, "YUV 422\n");
		break;
	case 2:
		cs = XCS_YUV444;
		dev_dbg_ratelimited(xhdmi->dev, "YUV 444\n");
		break;
	case 3:
		cs = XCS_YUV420;
		dev_dbg_ratelimited(xhdmi->dev, "YUV 420\n");
		break;
	default:
		cs = XCS_RGB;
		dev_dbg_ratelimited(xhdmi->dev, "RGB\n");
		break;
	}

	return cs;
}

/**
 * xhdmirx1_getgcp_colordepth - Get the color depth of the stream
 *
 * @xhdmi: pointer to the driver state
 *
 * Returns: colordepth or bits per component of incoming stream
 */
static enum xcolordepth xhdmirx1_getgcp_colordepth(struct xhdmirx_state *xhdmi)
{
	u32 val;
	enum xcolordepth ret;

	val = xhdmi_read(xhdmi, HDMIRX_AUX_STA_OFFSET);

	switch (FIELD_GET(HDMIRX_AUX_STA_GCP_CD_MASK, val)) {
	case 1:
		ret = XCD_10;
		break;
	case 2:
		ret = XCD_12;
		break;
	case 3:
		ret = XCD_16;
		break;
	default:
		ret = XCD_8;
		break;
	}

	dev_dbg_ratelimited(xhdmi->dev, "get GCP colordepth %u\n", ret);
	return ret;
}

/**
 * xhdmirx1_get_video_properties - Get the incoming video stream properties
 *
 * @xhdmi: pointer to the driver state
 *
 * This function populates the video structure with color space and depth.
 * If getvidproptries > MAX_VID_PROP_TRIES means incoming stream is DVI
 *
 * Returns: 0 on success or -1 on fail
 */
static int xhdmirx1_get_video_properties(struct xhdmirx_state *xhdmi)
{
	u32 status;

	status = xhdmi_read(xhdmi, HDMIRX_AUX_STA_OFFSET);

	if (status & HDMIRX_AUX_STA_AVI_MASK) {
		xhdmi->stream.video.colorspace = xhdmirx1_getavi_colorspace(xhdmi);
		xhdmi->stream.vic = xhdmirx1_getavi_vic(xhdmi);

		if (xhdmi->stream.video.colorspace == XCS_YUV422)
			xhdmi->stream.video.colordepth = XCD_12;
		else
			xhdmi->stream.video.colordepth =
				xhdmirx1_getgcp_colordepth(xhdmi);
		return 0;
	}

	if (xhdmi->stream.getvidproptries > MAX_VID_PROP_TRIES) {
		xhdmi->stream.video.colorspace = XCS_RGB;
		xhdmi->stream.vic = 0;
		xhdmi->stream.video.colordepth = XCD_8;
		return 0;
	}

	xhdmi->stream.getvidproptries++;
	return -1;
}

/**
 * xhdmirx1_get_vid_timing - Get the video timings of incoming stream
 *
 * @xhdmi: pointer to driver state
 *
 * This function gets the timing information from the IP and checks it
 * against the older value. If mismatch, it updates the video timing
 * structure in the driver state.
 *
 * Returns: 0 on success or -1 on fail
 */
static int xhdmirx1_get_vid_timing(struct xhdmirx_state *xhdmi)
{
	u32 data;
	u16 hact, hfp, hsw, hbp, htot;
	u16 vact, vfp[MAX_FIELDS], vsw[MAX_FIELDS], vbp[MAX_FIELDS];
	u16 vtot[MAX_FIELDS];
	u8 match, yuv420_correction, isinterlaced;

	if (xhdmi->stream.video.colorspace == XCS_YUV420)
		yuv420_correction = 2;
	else
		yuv420_correction = 1;

	htot = xhdmi_read(xhdmi, HDMIRX_VTD_TOT_PIX_OFFSET) * yuv420_correction;
	hact = xhdmi_read(xhdmi, HDMIRX_VTD_ACT_PIX_OFFSET) * yuv420_correction;
	hsw = xhdmi_read(xhdmi, HDMIRX_VTD_HSW_OFFSET) * yuv420_correction;
	hfp = xhdmi_read(xhdmi, HDMIRX_VTD_HFP_OFFSET) * yuv420_correction;
	hbp = xhdmi_read(xhdmi, HDMIRX_VTD_HBP_OFFSET) * yuv420_correction;

	data = xhdmi_read(xhdmi, HDMIRX_VTD_TOT_LIN_OFFSET);
	vtot[0] = FIELD_GET(HDMIRX_VTD_VF0_MASK, data);
	vtot[1] = FIELD_GET(HDMIRX_VTD_VF1_MASK, data);

	vact = xhdmi_read(xhdmi, HDMIRX_VTD_ACT_LIN_OFFSET);

	data = xhdmi_read(xhdmi, HDMIRX_VTD_VFP_OFFSET);
	vfp[0] = FIELD_GET(HDMIRX_VTD_VF0_MASK, data);
	vfp[1] = FIELD_GET(HDMIRX_VTD_VF1_MASK, data);

	data = xhdmi_read(xhdmi, HDMIRX_VTD_VSW_OFFSET);
	vsw[0] = FIELD_GET(HDMIRX_VTD_VF0_MASK, data);
	vsw[1] = FIELD_GET(HDMIRX_VTD_VF1_MASK, data);

	data = xhdmi_read(xhdmi, HDMIRX_VTD_VBP_OFFSET);
	vbp[0] = FIELD_GET(HDMIRX_VTD_VF0_MASK, data);
	vbp[1] = FIELD_GET(HDMIRX_VTD_VF1_MASK, data);

	data = xhdmi_read(xhdmi, HDMIRX_VTD_STA_OFFSET);
	if (data & HDMIRX_VTD_STA_FMT_MASK)
		isinterlaced = 1;
	else
		isinterlaced = 0;

	match = 1;

	if (!hact || !hfp || !hsw || !hbp || !htot || !vact ||
	    !vtot[0] || !vfp[0] || !vbp[0] || !vsw[0])
		match = 0;

	if (isinterlaced && (!vfp[1] || !vsw[1] || !vbp[1] || !vtot[1]))
		match = 0;

	if (hact != xhdmi->stream.video.timing.hact ||
	    htot != xhdmi->stream.video.timing.htot ||
	    hfp != xhdmi->stream.video.timing.hfp ||
	    hbp != xhdmi->stream.video.timing.hbp ||
	    hsw != xhdmi->stream.video.timing.hsw ||
	    vact != xhdmi->stream.video.timing.vact ||
	    vtot[0] != xhdmi->stream.video.timing.vtot[0] ||
	    vtot[1] != xhdmi->stream.video.timing.vtot[1] ||
	    vfp[0] != xhdmi->stream.video.timing.vfp[0] ||
	    vfp[1] != xhdmi->stream.video.timing.vfp[1] ||
	    vbp[0] != xhdmi->stream.video.timing.vbp[0] ||
	    vbp[1] != xhdmi->stream.video.timing.vbp[1] ||
	    vsw[0] != xhdmi->stream.video.timing.vsw[0] ||
	    vsw[1] != xhdmi->stream.video.timing.vsw[1])
		match = 0;

	if (vtot[0] != (vact + vfp[0] + vsw[0] + vbp[0]))
		match = 0;

	if (isinterlaced) {
		if (vtot[1] != (vact + vfp[1] + vsw[1] + vbp[1]))
			match = 0;
	} else {
		/* if field 1 is populated for progessive video */
		if (vfp[1] | vbp[1] | vsw[1])
			match = 0;
	}

	xhdmi->stream.video.timing.hact = hact;
	xhdmi->stream.video.timing.htot = htot;
	xhdmi->stream.video.timing.hfp = hfp;
	xhdmi->stream.video.timing.hsw = hsw;
	xhdmi->stream.video.timing.hbp = hbp;

	xhdmi->stream.video.timing.vtot[0] = vtot[0];
	xhdmi->stream.video.timing.vtot[1] = vtot[1];

	xhdmi->stream.video.timing.vact = vact;
	xhdmi->stream.video.timing.vfp[0] = vfp[0];
	xhdmi->stream.video.timing.vfp[1] = vfp[1];
	xhdmi->stream.video.timing.vsw[0] = vsw[0];
	xhdmi->stream.video.timing.vsw[1] = vsw[1];
	xhdmi->stream.video.timing.vbp[0] = vbp[0];
	xhdmi->stream.video.timing.vbp[1] = vbp[1];

	if (match) {
		data = xhdmi_read(xhdmi, HDMIRX_VTD_STA_OFFSET);

		if (data & HDMIRX_VTD_STA_FMT_MASK)
			xhdmi->stream.video.isinterlaced = true;
		else
			xhdmi->stream.video.isinterlaced = false;

		if (data & HDMIRX_VTD_STA_VS_POL_MASK)
			xhdmi->stream.video.timing.vsyncpol = 1;
		else
			xhdmi->stream.video.timing.vsyncpol = 0;

		if (data & HDMIRX_VTD_STA_HS_POL_MASK)
			xhdmi->stream.video.timing.hsyncpol = 1;
		else
			xhdmi->stream.video.timing.hsyncpol = 0;

		return 0;
	}

	return -1;
}

/**
 * xhdmirx1_setpixelclk - Calculate and save the pixel clock
 *
 * @xhdmi: pointer to driver state
 *
 * This function calculates the pixel clock based on incoming stream
 * reference clock and bits per component / color depth
 */
static void xhdmirx1_setpixelclk(struct xhdmirx_state *xhdmi)
{
	switch (xhdmi->stream.video.colordepth) {
	case XCD_10:
		xhdmi->stream.pixelclk = (xhdmi->stream.refclk << 2) / 5;
		break;
	case XCD_12:
		xhdmi->stream.pixelclk = (xhdmi->stream.refclk << 1) / 3;
		break;
	case XCD_16:
		xhdmi->stream.pixelclk = xhdmi->stream.refclk >> 1;
		break;
	default:
		xhdmi->stream.pixelclk = xhdmi->stream.refclk;
		break;
	}

	if (xhdmi->stream.video.colorspace == XCS_YUV422)
		xhdmi->stream.pixelclk = xhdmi->stream.refclk;

	dev_dbg(xhdmi->dev, "pixel clk = %u Hz ref clk = %u Hz\n",
		xhdmi->stream.pixelclk, xhdmi->stream.refclk);
}

static int xhdmirx_phy_configure(struct xhdmirx_state *xhdmi,
				 union phy_configure_opts *opts)
{
	int ret;

	ret = phy_configure(xhdmi->phy[0], opts);
	if (ret)
		dev_err(xhdmi->dev, "phy_configure error %d\n", ret);

	return ret;
}

static void phy_rxinit_cb(void *param)
{
	struct xhdmirx_state *xhdmi = (struct xhdmirx_state *)param;
	union phy_configure_opts opts = {0};
	u32 val;

	/* Get TMDS clock ratio */
	val = xhdmirx1_gettmdsclkratio(xhdmi);

	opts.hdmi.tmdsclock_ratio_flag = 1;
	opts.hdmi.tmdsclock_ratio = val;

	/* set the TMDS clock ratio in phy */
	xhdmirx_phy_configure(xhdmi, &opts);
	dev_dbg(xhdmi->dev, "Phy RxInitCallback tmds clk ratio = %u\n", val);
}

static void phy_rxready_cb(void *param)
{
	struct xhdmirx_state *xhdmi = (struct xhdmirx_state *)param;
	union phy_configure_opts opts = {0};
	int ret;

	opts.hdmi.rx_get_refclk = 1;
	ret = xhdmirx_phy_configure(xhdmi, &opts);
	if (ret) {
		dev_err(xhdmi->dev, "Unable to get ref clk from phy %d\n", ret);
		return;
	}

	xhdmi->stream.refclk = opts.hdmi.rx_refclk_hz;
	dev_dbg(xhdmi->dev, "Phy RxReadyCallback refclk = %u Hz\n", xhdmi->stream.refclk);
}

/**
 * xhdmirx1_configbridgemode - Configure the bridge
 *
 * @xhdmi: pointer to driver state
 *
 * This function configure the bridge for YUV420 and pixel drop
 * based on whether the stream in interlaced, hdmi and colorspace is YUV420
 */
static void xhdmirx1_configbridgemode(struct xhdmirx_state *xhdmi)
{
	if (!xhdmi->stream.ishdmi && xhdmi->stream.video.isinterlaced) {
		if (xhdmi->stream.video.timing.hact == 1440 &&
		    (xhdmi->stream.video.timing.vact == 288 ||
		     xhdmi->stream.video.timing.vact == 240)) {
			xhdmirx1_bridgeyuv420(xhdmi, false);
			xhdmirx1_bridgepixeldrop(xhdmi, true);
		}
	}

	/* check aux info frame for pixel repetition and return */
	if (xhdmi->stream.video.colorspace == XCS_YUV420) {
		xhdmirx1_bridgepixeldrop(xhdmi, false);
		xhdmirx1_bridgeyuv420(xhdmi, true);
	} else {
		/*
		 * check if pixel repetition factor is 2
		 * for ntsc pal support
		 */
		xhdmirx1_bridgeyuv420(xhdmi, false);
		xhdmirx1_bridgepixeldrop(xhdmi, false);
	}
}

/**
 * xhdmirx1_get_mbusfmtcode - Update the media bus format
 *
 * @xhdmi: pointer to driver state
 *
 * This function populates the media bus format in the mbus_fmt struct
 * based on the colorspace and colordepth of the incoming stream.
 */
static void xhdmirx1_get_mbusfmtcode(struct xhdmirx_state *xhdmi)
{
	struct xvideostream *stream = &xhdmi->stream.video;

	/* decode AVI Info frame and fill up above correctly */
	switch (stream->colorspace) {
	case XCS_YUV422:
		switch (xhdmi->max_bpc) {
		case XCD_8:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_UYVY8_1X16;
			break;
		case XCD_10:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_UYVY10_1X20;
			break;
		case XCD_12:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_UYVY12_1X24;
			break;
		case XCD_16:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_UYVY16_2X32;
			break;
		}
		break;
	case XCS_YUV444:
		switch (xhdmi->max_bpc) {
		case XCD_8:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_VUY8_1X24;
			break;
		case XCD_10:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_VUY10_1X30;
			break;
		case XCD_12:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_VUY12_1X36;
			break;
		case XCD_16:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_VUY16_1X48;
			break;
		}
		break;
	case XCS_YUV420:
		switch (xhdmi->max_bpc) {
		case XCD_8:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_VYYUYY8_1X24;
			break;
		case XCD_10:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_VYYUYY10_4X20;
			break;
		case XCD_12:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_UYYVYY12_4X24;
			break;
		case XCD_16:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_UYYVYY16_4X32;
			break;
		}
		break;
	case XCS_RGB:
		switch (xhdmi->max_bpc) {
		case XCD_8:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_RBG888_1X24;
			break;
		case XCD_10:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_RBG101010_1X30;
			break;
		case XCD_12:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_RBG121212_1X36;
			break;
		case XCD_16:
			xhdmi->mbus_fmt.code = MEDIA_BUS_FMT_RBG161616_1X48;
			break;
		}
		break;
	}
	dev_dbg_ratelimited(xhdmi->dev, "mbus_fmt code = 0x%08x\n",
			    xhdmi->mbus_fmt.code);
}

/**
 * rxstreamup - Update the dv timings and media bus format structs
 *
 * @xhdmi: pointer to driver state
 *
 * This function is called when the stream is found to be up.
 * This configures the bridge mode, media bus format struct, detected
 * dv timings and generates source change event to user space.
 */
static void rxstreamup(struct xhdmirx_state *xhdmi)
{
	struct xvideostream *stream;

	dev_dbg_ratelimited(xhdmi->dev, "%s - enter\n", __func__);

	xhdmirx1_clearlinkstatus(xhdmi);
	xhdmi->isstreamup = true;

	xhdmirx1_configbridgemode(xhdmi);

	/* enable clock forwarding */

	stream = &xhdmi->stream.video;

	/*
	 * use v4l2_find_dv_timings_cea861_vic with avi info frame vic id
	 * to get the std timings. For now we will get timing from IP.
	 */
	xhdmi->mbus_fmt.width = stream->timing.hact;
	xhdmi->mbus_fmt.height = stream->timing.vact;

	if ((stream->timing.hact == 1440 &&
	     ((stream->timing.vact == 240 && stream->framerate == 60) ||
	      (stream->timing.vact == 288 && stream->framerate == 50))) &&
	    !!stream->isinterlaced)
		xhdmi->mbus_fmt.width /= 2;

	if (stream->isinterlaced)
		xhdmi->mbus_fmt.field = V4L2_FIELD_ALTERNATE;
	else
		xhdmi->mbus_fmt.field = V4L2_FIELD_NONE;

	xhdmi->mbus_fmt.colorspace = V4L2_COLORSPACE_REC709;
	xhdmi->mbus_fmt.ycbcr_enc = V4L2_YCBCR_ENC_709;
	xhdmi->mbus_fmt.xfer_func = V4L2_XFER_FUNC_709;
	xhdmi->mbus_fmt.quantization = V4L2_QUANTIZATION_DEFAULT;

	xhdmirx1_get_mbusfmtcode(xhdmi);

	xhdmi->dv_timings.type		= V4L2_DV_BT_656_1120;
	xhdmi->dv_timings.bt.width	= stream->timing.hact;
	xhdmi->dv_timings.bt.height	= stream->timing.vact;
	xhdmi->dv_timings.bt.interlaced = !!stream->isinterlaced;
	xhdmi->dv_timings.bt.polarities = stream->timing.vsyncpol ?
					  V4L2_DV_VSYNC_POS_POL : 0;
	xhdmi->dv_timings.bt.polarities |= stream->timing.hsyncpol ?
					   V4L2_DV_HSYNC_POS_POL : 0;
	/* determine pixel clock */
	if (stream->isinterlaced) {
		xhdmi->dv_timings.bt.pixelclock = stream->timing.vtot[0] +
						stream->timing.vtot[1];
		xhdmi->dv_timings.bt.pixelclock *= stream->framerate / 2;
	} else {
		xhdmi->dv_timings.bt.pixelclock = stream->timing.vtot[0] *
						  stream->framerate;
	}
	xhdmi->dv_timings.bt.pixelclock *= stream->timing.htot;

	if ((stream->timing.hact == 1440 &&
	     ((stream->timing.vact == 240 && stream->framerate == 60) ||
	      (stream->timing.vact == 288 && stream->framerate == 50))) &&
	    !!stream->isinterlaced) {
		xhdmi->dv_timings.bt.width /= 2;
		xhdmirx1_bridgeyuv420(xhdmi, false);
		xhdmirx1_bridgepixeldrop(xhdmi, true);
	}

	xhdmi->dv_timings.bt.hfrontporch = stream->timing.hfp;
	xhdmi->dv_timings.bt.hsync = stream->timing.hsw;
	xhdmi->dv_timings.bt.hbackporch = stream->timing.hbp;
	xhdmi->dv_timings.bt.vfrontporch = stream->timing.vfp[0];
	xhdmi->dv_timings.bt.vsync = stream->timing.vsw[0];
	xhdmi->dv_timings.bt.vbackporch = stream->timing.vbp[0];
	xhdmi->dv_timings.bt.il_vfrontporch = stream->timing.vfp[1];
	xhdmi->dv_timings.bt.il_vsync = stream->timing.vsw[1];
	xhdmi->dv_timings.bt.il_vbackporch = stream->timing.vbp[1];
	xhdmi->dv_timings.bt.standards = V4L2_DV_BT_STD_CEA861;
	xhdmi->dv_timings.bt.flags = V4L2_DV_FL_IS_CE_VIDEO;

	xhdmi->isstreamup = true;

	v4l2_subdev_notify_event(&xhdmi->sd, &xhdmi_ev_fmt);
	/* can enable audio now */

	dev_dbg_ratelimited(xhdmi->dev, "mbus fmt width = %u height = %u code = 0x%08x\n",
			    xhdmi->mbus_fmt.width, xhdmi->mbus_fmt.height, xhdmi->mbus_fmt.code);
#ifdef DEBUG
	v4l2_print_dv_timings("xilinx-hdmi-rx", "", &xhdmi->dv_timings, 1);
#endif
}

/**
 * rxstreaminit - Initialise the stream
 *
 * @xhdmi: pointer to driver state
 *
 * This function is called to initialized the video phy
 */
static void rxstreaminit(struct xhdmirx_state *xhdmi)
{
	union phy_configure_opts cfg = {0};
	struct xvideostream *vidstream = &xhdmi->stream.video;
	u8 colordepth = (u8)xhdmi->stream.video.colordepth;

	dev_dbg_ratelimited(xhdmi->dev, "%s - enter\n", __func__);

	if (vidstream->colorspace == XCS_YUV422)
		colordepth = (u8)XCD_8;

	cfg.hdmi.ppc = COREPIXPERCLK;
	cfg.hdmi.bpc = colordepth;
	cfg.hdmi.cal_mmcm_param = 1;
	xhdmirx_phy_configure(xhdmi, &cfg);
}

/**
 * rxconnect - function called back in connect state
 *
 * @xhdmi: pointer to driver state
 *
 * This function is called when driver state is in connect state.
 * When the cable is connected / disconnected this function is called.
 */
static void rxconnect(struct xhdmirx_state *xhdmi)
{
	union phy_configure_opts phy_cfg = {0};

	dev_dbg_ratelimited(xhdmi->dev, "%s - enter cable %s\n",
			    __func__, xhdmi->stream.cable_connected ?
			    "connected" : "disconnected");

	if (xhdmirx1_isstreamconnected(xhdmi)) {
		xhdmirx_set_hpd(xhdmi, 1);
		phy_cfg.hdmi.ibufds = 1;
		phy_cfg.hdmi.ibufds_en = 1;
		xhdmirx_phy_configure(xhdmi, &phy_cfg);
		xhdmirx_ext_vrst_assert(xhdmi);
	} else {
		xhdmirx_set_hpd(xhdmi, 0);
		xhdmirx_scrambler_disable(xhdmi);
		/* frl ddc write scrambler stat as 0 */

		phy_cfg.hdmi.tmdsclock_ratio_flag = 1;
		phy_cfg.hdmi.tmdsclock_ratio = 0;
		xhdmirx_phy_configure(xhdmi, &phy_cfg);

		phy_cfg.hdmi.ibufds = 1;
		phy_cfg.hdmi.ibufds_en = 0;
		xhdmirx_phy_configure(xhdmi, &phy_cfg);
	}
}

/**
 * tmdsconfig - Function to config the TMDS in the phy
 *
 * @xhdmi: pointer to driver state
 *
 * Function is used to configure the Phy in TMDS 2.0 or HDMI 2.1 config
 */
static void tmdsconfig(struct xhdmirx_state *xhdmi)
{
	union phy_configure_opts phy_cfg = {0};

	dev_dbg_ratelimited(xhdmi->dev, "%s - enter\n", __func__);

	phy_cfg.hdmi.config_hdmi20 = 1;
	xhdmirx_phy_configure(xhdmi, &phy_cfg);
	dev_dbg(xhdmi->dev, "set phy to hdmi20\n");

	/* XV_Rx_HdmiTrigCb_VfmcRxClkSel */
	xhdmirx_setfrl_vclkvckeratio(xhdmi, 0);
}

/**
 * streamdown - called on stream down event
 *
 * @xhdmi: pointer to driver state
 *
 * This function is called on stream down event
 */
static void streamdown(struct xhdmirx_state *xhdmi)
{
	dev_dbg_ratelimited(xhdmi->dev, "%s - enter\n", __func__);

	/* In TMDS mode */
	xhdmirx_rxcore_vrst_assert(xhdmi);
	xhdmirx_rxcore_lrst_assert(xhdmi);
	xhdmirx_sysrst_assert(xhdmi);
	xhdmi->isstreamup = false;
}

static void xhdmirx1_clear(struct xhdmirx_state *xhdmi)
{
	/*
	 * reset state to XSTREAM_DOWN
	 * reset colorspace, depth, timing, ishdmi, isfrl, isinterlaced, vic
	 * reset getvidpropcount, set frl state as lts_l
	 * clear aux packet data
	 * reset audio properties
	 */
	xhdmi->stream.state = XSTREAM_DOWN;
	xhdmi->stream.ishdmi = false;
	xhdmi->isstreamup = false;

	xhdmi->stream.video.colorspace = XCS_RGB;
	xhdmi->stream.video.isinterlaced = false;
	xhdmi->stream.video.colordepth = XCD_8;
	memset(&xhdmi->stream.video.timing, 0, sizeof(xhdmi->stream.video.timing));
	xhdmi->stream.vic = 0;
	xhdmi->stream.getvidproptries = 0;
	memset(&xhdmi->dv_timings, 0, sizeof(xhdmi->dv_timings));
	memset(&xhdmi->mbus_fmt, 0, sizeof(xhdmi->mbus_fmt));

	streamdown(xhdmi);
}

/**
 * xhdmirx_pioint_handler - Function to handle the PIO interrupt
 *
 * @xhdmi: pointer to driver state
 *
 * Function to handle the PIO interrupt
 */
static void xhdmirx_pioint_handler(struct xhdmirx_state *xhdmi)
{
	u32 event, data;

	event = xhdmi_read(xhdmi, HDMIRX_PIO_IN_EVT_OFFSET);
	/* clear the PIO interrupts */
	xhdmi_write(xhdmi, HDMIRX_PIO_IN_EVT_OFFSET, event);
	data = xhdmi_read(xhdmi, HDMIRX_PIO_IN_OFFSET);

	dev_dbg_ratelimited(xhdmi->dev, "pio int handler PIO IN - 0x%08x\n",
			    data);

	/* handle cable connect / disconnect */
	if (event & HDMIRX_PIO_IN_DET_MASK) {
		if (data & HDMIRX_PIO_IN_DET_MASK) {
			/* cable connected */
			xhdmi->stream.cable_connected = true;
			/* reset frl as false */
			xhdmi->stream.ishdmi = false;
			rxconnect(xhdmi);
			tmdsconfig(xhdmi);
		} else {
			xhdmi->stream.cable_connected = false;
			xhdmirx_ddcscdc_clear(xhdmi);
			/* reset frl as true */
			rxconnect(xhdmi);
		}
	}

	if (event & HDMIRX_PIO_IN_LNK_RDY_MASK) {
		xhdmi->stream.state = XSTREAM_IDLE;
		dev_dbg_ratelimited(xhdmi->dev, "pio lnk rdy state = XSTREAM_IDLE\n");
		/* start 10 ms timer */
		xhdmirx_tmr1_start(xhdmi, TIME_10MS);
	}

	if (event & HDMIRX_PIO_IN_VID_RDY_MASK) {
		if (data & HDMIRX_PIO_IN_VID_RDY_MASK) {
			if (xhdmi->stream.state == XSTREAM_INIT) {
				dev_dbg_ratelimited(xhdmi->dev, "pio vid rdy state = XSTREAM_INIT\n");
				/* Toggle Rx Core reset */
				xhdmirx_rxcore_vrst_assert(xhdmi);
				xhdmirx_rxcore_vrst_deassert(xhdmi);

				/* Toggle bridge reset */
				xhdmirx_ext_vrst_assert(xhdmi);
				xhdmirx_sysrst_assert(xhdmi);
				xhdmirx_ext_vrst_deassert(xhdmi);
				xhdmirx_sysrst_deassert(xhdmi);

				xhdmi->stream.state = XSTREAM_ARM;
				/* start 200 ms timer */
				xhdmirx_tmr1_start(xhdmi, TIME_200MS);
			}
		} else {
			/* Stream Down */
			xhdmirx_rxcore_vrst_assert(xhdmi);
			xhdmirx_rxcore_lrst_assert(xhdmi);

			xhdmirx1_clear(xhdmi);

			xhdmirx_aux_disable(xhdmi);
			xhdmirx_aud_disable(xhdmi);
			xhdmirx_vtd_disable(xhdmi);
			xhdmirx_link_disable(xhdmi);
			xhdmirx_video_enable(xhdmi);
			xhdmirx_axi4s_disable(xhdmi);

			xhdmi->stream.state = XSTREAM_DOWN;
			dev_dbg_ratelimited(xhdmi->dev, "pio vid rdy state = XSTREAM_DOWN\n");

			xhdmi_write(xhdmi, HDMIRX_VTD_CTRL_CLR_OFFSET,
				    HDMIRX_VTD_CTRL_SYNC_LOSS_MASK);

			streamdown(xhdmi);
		}
	}

	if (event & HDMIRX_PIO_IN_SCDC_SCRAMBLER_ENABLE_MASK) {
		dev_dbg_ratelimited(xhdmi->dev, "scrambler intr\n");
		if (data & HDMIRX_PIO_IN_SCDC_SCRAMBLER_ENABLE_MASK)
			xhdmirx_scrambler_enable(xhdmi);
		else
			xhdmirx_scrambler_disable(xhdmi);
	}

	if (event & HDMIRX_PIO_IN_MODE_MASK) {
		if (data & HDMIRX_PIO_IN_MODE_MASK)
			xhdmi->stream.ishdmi = true;
		else /* DVI */
			xhdmi->stream.ishdmi = false;

		if (xhdmi->stream.state == XSTREAM_UP ||
		    xhdmi->stream.state == XSTREAM_LOCK) {
			/* up or lock state */
			xhdmirx1_clear(xhdmi);
			xhdmi->stream.state = XSTREAM_IDLE;
			dev_dbg_ratelimited(xhdmi->dev, "state = XSTREAM_UP or LOCK\n");
			/* 10 ms timer */
			xhdmirx_tmr1_start(xhdmi, TIME_10MS);
		}
		/* modecall back does nothing */
	}

	if (event & HDMIRX_PIO_IN_SCDC_TMDS_CLOCK_RATIO_MASK)
		dev_dbg_ratelimited(xhdmi->dev, "scdc tmds clock ratio interrupt\n");

	if (event & HDMIRX_PIO_IN_BRDG_OVERFLOW_MASK)
		dev_dbg_ratelimited(xhdmi->dev, "bridge overflow interrupt\n");
}

/**
 * xhdmirx_tmrint_handler - Function to handle the timer interrupt
 *
 * @xhdmi: pointer to driver state
 *
 * Function to handle the timer interrupt
 */
static void xhdmirx_tmrint_handler(struct xhdmirx_state *xhdmi)
{
	u32 status;

	status = xhdmi_read(xhdmi, HDMIRX_TMR_STA_OFFSET);

	dev_dbg_ratelimited(xhdmi->dev, "%s - timer int status reg = 0x%08x\n",
			    __func__, status);

	if (status & HDMIRX_TMR1_STA_CNT_EVT_MASK) {
		xhdmi_write(xhdmi, HDMIRX_TMR_STA_OFFSET,
			    HDMIRX_TMR1_STA_CNT_EVT_MASK);

		/* TODO Add FRL related code here */

		if (xhdmi->stream.state == XSTREAM_IDLE) {
			dev_dbg_ratelimited(xhdmi->dev, "state = XSTREAM_IDLE\n");

			xhdmirx_aux_enable(xhdmi);
			/* enable audio */
			/* release the internal vrst & lrst */
			xhdmirx_rxcore_vrst_deassert(xhdmi);
			xhdmirx_rxcore_lrst_deassert(xhdmi);
			xhdmirx_link_enable(xhdmi);

			xhdmi->stream.state = XSTREAM_INIT;
			xhdmi->stream.getvidproptries = 0;
			xhdmirx_tmr1_start(xhdmi, TIME_200MS);

		} else if (xhdmi->stream.state == XSTREAM_INIT) {
			dev_dbg_ratelimited(xhdmi->dev, "state = XSTREAM_INIT\n");
			/* get video properties */
			if (xhdmirx1_get_video_properties(xhdmi)) {
				/* failed to get video properties */
				xhdmirx_tmr1_start(xhdmi, TIME_200MS);
			} else {
				xhdmirx1_setpixelclk(xhdmi);
				rxstreaminit(xhdmi);
			}

		} else if (xhdmi->stream.state == XSTREAM_ARM) {
			dev_dbg(xhdmi->dev, "%s - state = XSTREAM_ARM\n", __func__);
			xhdmirx_vtd_enable(xhdmi);
			xhdmirx_vtdintr_enable(xhdmi);

			/* 16 ms timer count is already loaded in VTD */
			xhdmi->stream.state = XSTREAM_LOCK;
		}
	}

	if (status & HDMIRX_TMR2_STA_CNT_EVT_MASK) {
		xhdmi_write(xhdmi, HDMIRX_TMR_STA_OFFSET,
			    HDMIRX_TMR2_STA_CNT_EVT_MASK);
	}

	if (status & HDMIRX_TMR3_STA_CNT_EVT_MASK) {
		xhdmi_write(xhdmi, HDMIRX_TMR_STA_OFFSET,
			    HDMIRX_TMR3_STA_CNT_EVT_MASK);
		/* TODO add HdmiRx1_PhyResetPoll() */
	}

	if (status & HDMIRX_TMR4_STA_CNT_EVT_MASK) {
		xhdmi_write(xhdmi, HDMIRX_TMR_STA_OFFSET,
			    HDMIRX_TMR4_STA_CNT_EVT_MASK);
	}
}

/**
 * xhdmirx_vtdint_handler - Function to handle the VTD interrupt
 *
 * @xhdmi: pointer to driver state
 *
 * Function to handle the video timing detector interrupt
 */
static void xhdmirx_vtdint_handler(struct xhdmirx_state *xhdmi)
{
	u32 status;

	status = xhdmi_read(xhdmi, HDMIRX_VTD_STA_OFFSET);

	if (status & HDMIRX_VTD_STA_TIMEBASE_EVT_MASK) {
		xhdmi_write(xhdmi, HDMIRX_VTD_STA_OFFSET,
			    HDMIRX_VTD_STA_TIMEBASE_EVT_MASK);
		dev_dbg_ratelimited(xhdmi->dev, "vtd intr\n");

		if (xhdmi->stream.state == XSTREAM_LOCK) {
			int ret;
			u32 divisor, dividend;

			dev_dbg_ratelimited(xhdmi->dev, "%s - state = XSTREAM_LOCK\n", __func__);
			/* Get video timings */
			ret = xhdmirx1_get_vid_timing(xhdmi);

			if (!ret) {
				xhdmirx1_setpixelclk(xhdmi);
				/* calculate framerate */
				divisor = xhdmi->stream.video.timing.vtot[0];
				divisor *= xhdmi->stream.video.timing.htot;
				dividend = xhdmi->stream.pixelclk;
				if (xhdmi->stream.video.colorspace == XCS_YUV420)
					dividend <<= 1;
				xhdmi->stream.video.framerate =
					DIV_ROUND_CLOSEST(dividend, divisor);

				/* enable AXI stream output */
				xhdmirx_axi4s_enable(xhdmi);

				xhdmi->stream.state = XSTREAM_UP;
				xhdmi->stream.syncstatus = XSYNCSTAT_SYNC_EST;

				rxstreamup(xhdmi);

				xhdmi->hdmi_stream_up = 1;
			}
		} else if (xhdmi->stream.state == XSTREAM_UP) {
			int ret;

			dev_dbg_ratelimited(xhdmi->dev, "%s - state = XSTREAM_UP\n", __func__);
			ret = xhdmirx1_get_vid_timing(xhdmi);
			if (!ret) {
				if (xhdmi->stream.syncstatus == XSYNCSTAT_SYNC_LOSS) {
					xhdmi->stream.syncstatus = XSYNCSTAT_SYNC_EST;
					/* call syncloss callback */
				}
			} else {
				/* in tmds mode just set state to lock */
				xhdmi->stream.state = XSTREAM_LOCK;
				/* need to do frl mode */
			}
		}
	} else if (status & HDMIRX_VTD_STA_SYNC_LOSS_EVT_MASK) {
		xhdmi_write(xhdmi, HDMIRX_VTD_STA_OFFSET,
			    HDMIRX_VTD_STA_SYNC_LOSS_EVT_MASK);

		if (xhdmi->stream.state == XSTREAM_UP)
			xhdmi->stream.syncstatus = XSYNCSTAT_SYNC_LOSS;
		dev_dbg(xhdmi->dev, "%s - Sync Loss event\n", __func__);
	}
}

/**
 * xhdmirx_auxint_handler - Function to handle the AUX interrupt
 *
 * @xhdmi: pointer to driver state
 *
 * Function to handle the AUX packets interrupt
 */
static void xhdmirx_auxint_handler(struct xhdmirx_state *xhdmi)
{
	u32 status;

	status = xhdmi_read(xhdmi, HDMIRX_AUX_STA_OFFSET);
	dev_dbg_ratelimited(xhdmi->dev, "aux intr\n");

	if (status & HDMIRX_AUX_STA_DYN_HDR_EVT_MASK) {
		dev_dbg_ratelimited(xhdmi->dev, "aux dyn intr\n");
		xhdmi_write(xhdmi, HDMIRX_AUX_STA_OFFSET,
			    HDMIRX_AUX_STA_DYN_HDR_EVT_MASK);
	}

	if (status & HDMIRX_AUX_STA_VRR_CD_EVT_MASK) {
		dev_dbg_ratelimited(xhdmi->dev, "aux VRR intr\n");
		xhdmi_write(xhdmi, HDMIRX_AUX_STA_OFFSET,
			    HDMIRX_AUX_STA_VRR_CD_EVT_MASK);
	}

	if (status & HDMIRX_AUX_STA_FSYNC_CD_EVT_MASK) {
		dev_dbg_ratelimited(xhdmi->dev, "aux fsync intr\n");
		xhdmi_write(xhdmi, HDMIRX_AUX_STA_OFFSET,
			    HDMIRX_AUX_STA_FSYNC_CD_EVT_MASK);
	}

	if (status & HDMIRX_AUX_STA_GCP_CD_EVT_MASK) {
		dev_dbg_ratelimited(xhdmi->dev, "aux gcp intr\n");
		xhdmi_write(xhdmi, HDMIRX_AUX_STA_OFFSET,
			    HDMIRX_AUX_STA_GCP_CD_EVT_MASK);
		if (status & HDMIRX_AUX_STA_GCP_MASK)
			xhdmi->stream.video.colordepth = xhdmirx1_getgcp_colordepth(xhdmi);
	}

	if (status & HDMIRX_AUX_STA_NEW_MASK) {
		int i;

		dev_dbg_ratelimited(xhdmi->dev, "aux new packet intr\n");
		xhdmi_write(xhdmi, HDMIRX_AUX_STA_OFFSET,
			    HDMIRX_AUX_STA_NEW_MASK);

		xhdmi->stream.ishdmi = true;
		xhdmi->aux.header.data = xhdmi_read(xhdmi,
						    HDMIRX_AUX_DAT_OFFSET);
		for (i = 0; i < 8; i++)
			xhdmi->aux.data.data[i] =
				xhdmi_read(xhdmi, HDMIRX_AUX_DAT_OFFSET);
		/* aux call back */
	}

	if (status & HDMIRX_AUX_STA_ERR_MASK) {
		dev_dbg_ratelimited(xhdmi->dev, "aux err intr\n");
		xhdmi_write(xhdmi, HDMIRX_AUX_STA_OFFSET,  HDMIRX_AUX_STA_ERR_MASK);
		/* link error call back */
	}
}

static irqreturn_t xhdmirx_irq_handler(int irq, void *param)
{
	struct xhdmirx_state *xhdmi = (struct xhdmirx_state *)param;

	/* read status registers */
	xhdmi->intrstatus[0] = xhdmi_read(xhdmi, HDMIRX_PIO_STA_OFFSET) &
				HDMIRX_PIO_STA_IRQ_MASK;
	xhdmi->intrstatus[1] = xhdmi_read(xhdmi, HDMIRX_TMR_STA_OFFSET) &
				HDMIRX_TMR_STA_IRQ_MASK;
	xhdmi->intrstatus[2] = xhdmi_read(xhdmi, HDMIRX_VTD_STA_OFFSET) &
				HDMIRX_VTD_STA_IRQ_MASK;
	xhdmi->intrstatus[3] = xhdmi_read(xhdmi, HDMIRX_DDC_STA_OFFSET) &
				HDMIRX_DDC_STA_IRQ_MASK;
	xhdmi->intrstatus[4] = xhdmi_read(xhdmi, HDMIRX_AUX_STA_OFFSET) &
				HDMIRX_AUX_STA_IRQ_MASK;
	xhdmi->intrstatus[5] = xhdmi_read(xhdmi, HDMIRX_AUD_STA_OFFSET) &
				HDMIRX_AUD_STA_IRQ_MASK;
	xhdmi->intrstatus[6] = xhdmi_read(xhdmi, HDMIRX_LNKSTA_STA_OFFSET) &
				HDMIRX_LNKSTA_STA_IRQ_MASK;
	xhdmi->intrstatus[7] = xhdmi_read(xhdmi, HDMIRX_FRL_STA_OFFSET) &
				HDMIRX_FRL_STA_IRQ_MASK;

	/* mask interrupt request */
	xhdmirx_disable_allintr(xhdmi);

	/* call bottom-half */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t xhdmirx_irq_thread(int irq, void *param)
{
	struct xhdmirx_state *xhdmi = (struct xhdmirx_state *)param;

	if (xhdmi->intrstatus[0])
		xhdmirx_pioint_handler(xhdmi);
	if (xhdmi->intrstatus[1])
		xhdmirx_tmrint_handler(xhdmi);
	if (xhdmi->intrstatus[2])
		xhdmirx_vtdint_handler(xhdmi);
	if (xhdmi->intrstatus[3])
		xhdmi_write(xhdmi, HDMIRX_DDC_STA_OFFSET, xhdmi->intrstatus[3]);
	if (xhdmi->intrstatus[4])
		xhdmirx_auxint_handler(xhdmi);
	if (xhdmi->intrstatus[5])
		xhdmi_write(xhdmi, HDMIRX_AUD_STA_OFFSET, xhdmi->intrstatus[5]);
	if (xhdmi->intrstatus[6])
		xhdmi_write(xhdmi, HDMIRX_LNKSTA_STA_OFFSET, xhdmi->intrstatus[6]);
	if (xhdmi->intrstatus[7])
		xhdmi_write(xhdmi, HDMIRX_FRL_STA_OFFSET, xhdmi->intrstatus[7]);

	xhdmirx_enable_allintr(xhdmi);

	return IRQ_HANDLED;
}

/**
 * xhdmirx_load_edid - Function to load the user EDID
 *
 * @xhdmi: pointer to driver state
 * @edid: buffer pointer to user EDID
 * @length: Length of buffer
 *
 * Returns: 0 on success else -EINVAL
 */
static int xhdmirx_load_edid(struct xhdmirx_state *xhdmi, u8 *edid, int length)
{
	u32 wordcount;
	int i;

	wordcount = xhdmi_read(xhdmi, HDMIRX_DDC_EDID_STA_OFFSET);
	wordcount &= 0xFFFF;

	if (wordcount < length) {
		dev_err(xhdmi->dev, "fail as length > edid wc!\n");
		return -EINVAL;
	}

	xhdmi_write(xhdmi, HDMIRX_DDC_EDID_WP_OFFSET, 0);

	for (i = 0; i < length; i++)
		xhdmi_write(xhdmi, HDMIRX_DDC_EDID_DATA_OFFSET, edid[i]);

	xhdmi_write(xhdmi, HDMIRX_DDC_CTRL_SET_OFFSET,
		    HDMIRX_DDC_CTRL_EDID_EN_MASK);

	return 0;
}

static void xhdmirx_reset(struct xhdmirx_state *xhdmi)
{
	/* assert resets */
	xhdmirx_rxcore_vrst_assert(xhdmi);
	xhdmirx_rxcore_lrst_assert(xhdmi);
	xhdmirx_sysrst_assert(xhdmi);

	/* deassert resets */
	xhdmirx_sysrst_deassert(xhdmi);
	xhdmirx_rxcore_lrst_deassert(xhdmi);
	xhdmirx_rxcore_vrst_deassert(xhdmi);
}

static void xhdmirx_init(struct xhdmirx_state *xhdmi)
{
	u32 mask;

	xhdmirx_pio_disable(xhdmi);
	xhdmirx_tmr1_disable(xhdmi);
	xhdmirx_tmr2_disable(xhdmi);
	xhdmirx_tmr2_disable(xhdmi);
	xhdmirx_tmr2_disable(xhdmi);
	xhdmirx_vtd_disable(xhdmi);
	xhdmirx_ddc_disable(xhdmi);
	xhdmirx_aux_disable(xhdmi);
	xhdmirx_aud_disable(xhdmi);
	xhdmirx_lnksta_disable(xhdmi);
	/* keeps HDMI 2.0 mode initially */
	xhdmirx_frl_disable(xhdmi);

	xhdmirx_piointr_disable(xhdmi);
	xhdmirx_tmr1intr_disable(xhdmi);
	xhdmirx_tmr2intr_disable(xhdmi);
	xhdmirx_tmr3intr_disable(xhdmi);
	xhdmirx_tmr4intr_disable(xhdmi);
	xhdmirx_vtdintr_disable(xhdmi);
	xhdmirx_ddcintr_disable(xhdmi);

	xhdmirx_ddcscdc_clear(xhdmi);
	xhdmirx_set_hpd(xhdmi, 0);

	/* Rising edge mask */
	mask = 0;
	mask |= HDMIRX_PIO_IN_BRDG_OVERFLOW_MASK;
	mask |= HDMIRX_PIO_IN_DET_MASK;
	mask |= HDMIRX_PIO_IN_LNK_RDY_MASK;
	mask |= HDMIRX_PIO_IN_VID_RDY_MASK;
	mask |= HDMIRX_PIO_IN_MODE_MASK;
	mask |= HDMIRX_PIO_IN_SCDC_SCRAMBLER_ENABLE_MASK;
	mask |= HDMIRX_PIO_IN_SCDC_TMDS_CLOCK_RATIO_MASK;
	xhdmi_write(xhdmi, HDMIRX_PIO_IN_EVT_RE_OFFSET, mask);

	mask = 0;
	mask |= HDMIRX_PIO_IN_DET_MASK;
	mask |= HDMIRX_PIO_IN_VID_RDY_MASK;
	mask |= HDMIRX_PIO_IN_MODE_MASK;
	mask |= HDMIRX_PIO_IN_SCDC_SCRAMBLER_ENABLE_MASK;
	mask |= HDMIRX_PIO_IN_SCDC_TMDS_CLOCK_RATIO_MASK;
	xhdmi_write(xhdmi, HDMIRX_PIO_IN_EVT_FE_OFFSET, mask);

	xhdmirx_tmr1_enable(xhdmi);
	xhdmirx_tmr2_enable(xhdmi);
	xhdmirx_tmr3_enable(xhdmi);
	xhdmirx_tmr4_enable(xhdmi);
	xhdmirx_tmr1intr_enable(xhdmi);
	xhdmirx_tmr2intr_enable(xhdmi);
	xhdmirx_tmr3intr_enable(xhdmi);
	xhdmirx_tmr4intr_enable(xhdmi);

	/* set VTD for 200 ms different from bare metal's 16ms */
	xhdmirx_vtd_settimebase(xhdmi, TIME_200MS);

	xhdmirx_ddc_enable(xhdmi);
	xhdmirx_ddcscdc_enable(xhdmi);
	xhdmirx_auxintr_enable(xhdmi);
	xhdmirx_lnksta_enable(xhdmi);

	xhdmirx_load_edid(xhdmi, (u8 *)&xilinx_edid,
			  sizeof(xilinx_edid));

	xhdmirx_reset(xhdmi);
}

static int xhdmirx_parse_of(struct xhdmirx_state *xhdmi)
{
	struct device_node *node = xhdmi->dev->of_node;
	struct device *dev = xhdmi->dev;
	int ret;

	ret = of_property_read_u16(node, "xlnx,edid-ram-size",
				   &xhdmi->edid_ram_size);
	if (ret) {
		dev_err(dev, "xlnx,edid-ram-size property not found.\n");
		return ret;
	}

	if (xhdmi->edid_ram_size != 256 && xhdmi->edid_ram_size != 512 &&
	    xhdmi->edid_ram_size != 1024 && xhdmi->edid_ram_size != 4096) {
		dev_err(dev, "invalid edid ram size %d in dt\n",
			xhdmi->edid_ram_size);
		return -EINVAL;
	}

	xhdmi->edid_blocks_max = xhdmi->edid_ram_size / XEDID_BLOCK_SIZE;

	ret = of_property_read_u8(node, "xlnx,input-pixels-per-clock",
				  &xhdmi->max_ppc);
	if (ret) {
		dev_err(dev, "xlnx,input-pixels-per-clock property not found.\n");
		return ret;
	}

	if (xhdmi->max_ppc != 4 && xhdmi->max_ppc != 8) {
		dev_err(dev, "dt pixels per clock %d  is invalid.\n",
			xhdmi->max_ppc);
		return -EINVAL;
	}

	ret = of_property_read_u8(node, "xlnx,max-bits-per-component",
				  &xhdmi->max_bpc);
	if (ret) {
		dev_err(dev, "xlnx,max-bit-per-component property not found.\n");
		return ret;
	}

	if (xhdmi->max_bpc != 8 && xhdmi->max_bpc != 10 &&
	    xhdmi->max_bpc != 12 && xhdmi->max_bpc != 16) {
		dev_err(dev, "dt max bits per component %d is invalid.\n",
			xhdmi->max_bpc);
		return -EINVAL;
	}

	ret = of_property_read_u8(node, "xlnx,max-frl-rate",
				  &xhdmi->max_frl_rate);
	if (ret) {
		dev_err(dev, "xlnx,max-frl-rate property not found.\n");
		return ret;
	}

	if (xhdmi->max_frl_rate != 4 && xhdmi->max_frl_rate != 5 &&
	    xhdmi->max_frl_rate != 6) {
		dev_err(dev, "dt max frl rate %d is invalid.\n", xhdmi->max_frl_rate);
		return -EINVAL;
	}

	return 0;
}

static void xhdmirx_phy_release(struct xhdmirx_state *xhdmi)
{
	int i, ret;

	for (i = 0; i < XHDMI_MAX_LANES; i++) {
		ret = phy_exit(xhdmi->phy[i]);
		if (ret)
			dev_err(xhdmi->dev, "fail to exit phy(%d) %d\n", i, ret);

		xhdmi->phy[i] = NULL;
	}
}

/**
 * xhdmirx_dv_timings_cap - function to get the dv timings capabilities
 *
 * @subdev: pointer to v4l2 subdev
 * @cap: Pointer to capable DV timings
 *
 * Returns: 0 on success else -EINVAL
 */
static int xhdmirx_dv_timings_cap(struct v4l2_subdev *subdev,
				  struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	cap->type = V4L2_DV_BT_656_1120;
	cap->bt.max_width = 4096;
	cap->bt.max_height = 2160;
	cap->bt.min_pixelclock = 25000000;
	cap->bt.max_pixelclock = 297000000;
	cap->bt.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
		V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT;
	cap->bt.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
		V4L2_DV_BT_CAP_INTERLACED | V4L2_DV_BT_CAP_REDUCED_BLANKING |
		V4L2_DV_BT_CAP_CUSTOM;

	return 0;
}

/**
 * xhdmirx_get_edid - function to get the EDID set currently
 *
 * @subdev: pointer to v4l2 subdev structure
 * @edid: pointer to v4l2 edid structure to be filled to return
 *
 * This function returns the current EDID set in the HDMI Rx
 *
 * Returns: 0 on success else -EINVAL
 */
static int xhdmirx_get_edid(struct v4l2_subdev *subdev, struct v4l2_edid *edid)
{
	struct xhdmirx_state *xhdmi = to_xhdmirx_state(subdev);
	int do_copy = 1;

	if (edid->pad > 0)
		return -EINVAL;

	if (edid->start_block != 0)
		return -EINVAL;

	/* caller is only interested in the size of the EDID? */
	if (edid->start_block == 0 && edid->blocks == 0)
		do_copy = 0;

	mutex_lock(&xhdmi->xhdmi_mutex);
	/* user EDID active? */
	if (xhdmi->edid_user_blocks) {
		if (do_copy)
			memcpy(edid->edid, xhdmi->edid_user,
			       128 * (u16)xhdmi->edid_user_blocks);
		edid->blocks = xhdmi->edid_user_blocks;
	} else {
		if (do_copy)
			memcpy(edid->edid, &xilinx_edid[0], sizeof(xilinx_edid));
		edid->blocks = sizeof(xilinx_edid) / 128;
	}
	mutex_unlock(&xhdmi->xhdmi_mutex);

	return 0;
}

static void xhdmirx_delayed_work_enable_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct xhdmirx_state *xhdmi = container_of(dwork,
						       struct xhdmirx_state,
						       delayed_work_enable_hotplug);

	xhdmirx_set_hpd(xhdmi, 1);
}

/**
 * xhdmirx_set_edid - function to set the user EDID
 *
 * @subdev: pointer to v4l2 subdev structure
 * @edid: pointer to v4l2 edid structure to be set
 *
 * This function sets the user EDID in the HDMI Rx
 *
 * Returns: 0 on success else -EINVAL or -E2BIG
 */
static int xhdmirx_set_edid(struct v4l2_subdev *subdev, struct v4l2_edid *edid)
{
	struct xhdmirx_state *xhdmi = to_xhdmirx_state(subdev);
	int ret = 0;

	if (edid->pad > 0)
		return -EINVAL;

	if (edid->start_block != 0)
		return -EINVAL;

	if (edid->blocks > xhdmi->edid_blocks_max) {
		/* notify caller of how many EDID blocks this driver supports */
		edid->blocks = xhdmi->edid_blocks_max;
		return -E2BIG;
	}

	mutex_lock(&xhdmi->xhdmi_mutex);

	xhdmi->edid_user_blocks = edid->blocks;

	/* Disable hotplug and I2C access to EDID RAM from DDC port */
	cancel_delayed_work_sync(&xhdmi->delayed_work_enable_hotplug);
	xhdmirx_set_hpd(xhdmi, 0);

	if (edid->blocks) {
		memcpy(xhdmi->edid_user, edid->edid, 128 * edid->blocks);
		ret = xhdmirx_load_edid(xhdmi, (u8 *)&xhdmi->edid_user,
					128 * xhdmi->edid_user_blocks);
		if (!ret)
			/* enable hotplug after 100 ms */
			queue_delayed_work(xhdmi->work_queue,
					   &xhdmi->delayed_work_enable_hotplug,
					   HZ / 10);
	} else {
		dev_dbg(xhdmi->dev, "edid->blocks = 0\n");
	}

	mutex_unlock(&xhdmi->xhdmi_mutex);

	return ret;
}

static int xhdmirx_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xhdmirx_state *xhdmi = to_xhdmirx_state(subdev);

	dev_dbg(xhdmi->dev, "s_stream : enable %d\n", enable);
	return 0;
}

/**
 * xhdmirx_g_input_status - Gets the current link status
 *
 * @sd: pointer to v4l2 subdev struct
 * @status: Pointer to status to be returned
 *
 * This function returns the link status. This is called and checked for
 * before querying the dv timings.
 *
 * Returns: 0 on success else Link status
 */
static int xhdmirx_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct xhdmirx_state *xhdmi = to_xhdmirx_state(sd);

	if (!xhdmi->hdmi_stream_up)
		*status = V4L2_IN_ST_NO_SYNC | V4L2_IN_ST_NO_SIGNAL;
	else
		*status = 0;

	dev_dbg_ratelimited(xhdmi->dev, "g_input_statue = 0x%08x\n", *status);

	return 0;
}

/**
 * xhdmirx_query_dv_timings - Gets the current incoming dv timings
 *
 * @subdev: pointer to v4l2 subdev
 * @timings: pointer to the dv timings to be filled and returned
 *
 * This function returns the incoming stream's dv timings
 *
 * Returns: 0 on success else -ENOLINK
 */
static int xhdmirx_query_dv_timings(struct v4l2_subdev *subdev,
				    struct v4l2_dv_timings *timings)
{
	struct xhdmirx_state *xhdmi = to_xhdmirx_state(subdev);

	if (!xhdmi->hdmi_stream_up) {
		dev_dbg(xhdmi->dev, "failed as no link\n");
		return -ENOLINK;
	}

	v4l2_print_dv_timings(xhdmi->sd.name, "xhdmirx_query_dv_timing: ",
			      &xhdmi->dv_timings, true);

	*timings = xhdmi->dv_timings;

	return 0;
}

static struct v4l2_mbus_framefmt *
__xhdmirx_get_pad_format_ptr(struct xhdmirx_state *xhdmi,
			     struct v4l2_subdev_pad_config *cfg,
			     unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		dev_dbg(xhdmi->dev, "%s V4L2_SUBDEV_FORMAT_TRY\n", __func__);
		return v4l2_subdev_get_try_format(&xhdmi->sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		dev_dbg(xhdmi->dev, "%s V4L2_SUBDEV_FORMAT_ACTIVE\n", __func__);
		return &xhdmi->mbus_fmt;
	default:
		return NULL;
	}
}

/**
 * xhdmirx_set_format - Set the format to the pad
 *
 * @subdev: pointer to the v4l2 subdev struct
 * @cfg: pointer to pad configuration to be set
 * @fmt: pointer to format structure
 *
 * This function will update the fmt structure passed to
 * the current incoming stream format.
 *
 * Returns: 0 on success else -EINVAL
 */
static int xhdmirx_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct xhdmirx_state *xhdmi = to_xhdmirx_state(subdev);

	if (fmt->pad > 0)
		return -EINVAL;

	fmt->format = xhdmi->mbus_fmt;
	return 0;
}

/**
 * xhdmirx_get_format - Function to get pad format
 *
 * @subdev: pointer to v4l2 subdev struct
 * @cfg: pointer to the pad configuration
 * @fmt: pointer to the subdev format structure
 *
 * The fmt structure is updated based on incoming stream format.
 *
 * Returns: 0 on success else -EINVAL
 */
static int xhdmirx_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct xhdmirx_state *xhdmi = to_xhdmirx_state(subdev);
	struct v4l2_mbus_framefmt *gfmt;

	if (fmt->pad > 0)
		return -EINVAL;

	/* copy either try or currently-active (i.e. detected) format to caller */
	gfmt = __xhdmirx_get_pad_format_ptr(xhdmi, cfg, fmt->pad, fmt->which);
	if (!gfmt)
		return -EINVAL;

	dev_dbg(xhdmi->dev, "width %d height %d code %d\n",
		gfmt->width, gfmt->height, gfmt->code);

	fmt->format = *gfmt;
	return 0;
}

static int xhdmirx_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	struct xhdmirx_state *xhdmi = to_xhdmirx_state(sd);
	int rc = 0;

	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		rc = v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
		dev_dbg(xhdmi->dev, "subscribed to V4L2_EVENT_SOURCE_CHANGE = %d\n", rc);
		break;
	default:
		dev_dbg(xhdmi->dev, "subscribe_event() default: -EINVAL\n");
		rc = -EINVAL;
		break;
	}

	return rc;
}

static const struct v4l2_subdev_video_ops xvideo_ops = {
	.s_stream		= xhdmirx_s_stream,
	.query_dv_timings	= xhdmirx_query_dv_timings,
	.g_input_status		= xhdmirx_g_input_status,
};

static const struct v4l2_subdev_core_ops xcore_ops = {
	.subscribe_event	= xhdmirx_subscribe_event,
	.unsubscribe_event	= v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_pad_ops xpad_ops = {
	.get_edid		= xhdmirx_get_edid,
	.set_edid		= xhdmirx_set_edid,
	.dv_timings_cap		= xhdmirx_dv_timings_cap,
	.get_fmt		= xhdmirx_get_format,
	.set_fmt		= xhdmirx_set_format,
};

static const struct v4l2_subdev_ops xhdmirx_ops = {
	.pad = &xpad_ops,
	.video = &xvideo_ops,
	.core = &xcore_ops,
};

static const struct media_entity_operations xmedia_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int xhdmirx_probe_load_edid(struct xhdmirx_state *xhdmi)
{
	const struct firmware *fw_edid;
	const char *fw_edid_name = "xilinx/xilinx-hdmi-rx-edid.bin";
	u8 *edidbufptr = (u8 *)&xilinx_edid;
	int edidsize = sizeof(xilinx_edid);

	/* retrieve EDID */
	if (!request_firmware(&fw_edid, fw_edid_name, xhdmi->dev)) {
		int blocks = fw_edid->size / 128;

		if (blocks == 0 || blocks > xhdmi->edid_blocks_max ||
		    (fw_edid->size % 128)) {
			dev_err(xhdmi->dev, "%s must be n * 128 bytes, with 1 <= n <= %d, using Xilinx built-in EDID instead.\n",
				fw_edid_name, xhdmi->edid_blocks_max);
		} else {
			memcpy(xhdmi->edid_user, fw_edid->data, 128 * blocks);
			xhdmi->edid_user_blocks = blocks;
			edidbufptr = xhdmi->edid_user;
			edidsize = xhdmi->edid_user_blocks * 128;
		}
		release_firmware(fw_edid);
	}

	if (edidbufptr == xhdmi->edid_user)
		dev_info(xhdmi->dev, "Loading firmware edid\n");
	else
		dev_info(xhdmi->dev, "Loading Xilinx default edid\n");

	return xhdmirx_load_edid(xhdmi, edidbufptr, edidsize);
}

static int xhdmirx_probe(struct platform_device *pdev)
{
	struct xhdmirx_state *xhdmi;
	struct v4l2_subdev *sd;
	struct resource *res;
	union phy_configure_opts phy_cfg = {0};
	int i, ret, irq, num_clks;

	xhdmi = devm_kzalloc(&pdev->dev, sizeof(*xhdmi), GFP_KERNEL);
	if (!xhdmi)
		return -ENOMEM;

	xhdmi->dev = &pdev->dev;

	platform_set_drvdata(pdev, xhdmi);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xhdmi->regs = devm_ioremap_resource(xhdmi->dev, res);
	if (IS_ERR(xhdmi->regs))
		return PTR_ERR(xhdmi->regs);

	xhdmi->edid_user = devm_kzalloc(xhdmi->dev,
					XEDID_BLOCKS_MAX * XEDID_BLOCK_SIZE,
					GFP_KERNEL);
	if (!xhdmi->edid_user)
		return -ENOMEM;

	num_clks = ARRAY_SIZE(xhdmirx_clks);
	xhdmi->clks = devm_kcalloc(xhdmi->dev, num_clks,
				   sizeof(*xhdmi->clks), GFP_KERNEL);
	if (!xhdmi->clks)
		return -ENOMEM;

	for (i = 0; i < num_clks; i++)
		xhdmi->clks[i].id = xhdmirx_clks[i];

	ret = devm_clk_bulk_get(xhdmi->dev, num_clks, xhdmi->clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(num_clks, xhdmi->clks);
	if (ret)
		return ret;

	mutex_init(&xhdmi->xhdmi_mutex);
	xhdmi->work_queue = create_singlethread_workqueue("xilinx-hdmi-rx-wq");
	if (!xhdmi->work_queue) {
		dev_err(xhdmi->dev, "fail to create work queue!\n");
		ret = -EINVAL;
		goto mutex_err;
	}
	INIT_DELAYED_WORK(&xhdmi->delayed_work_enable_hotplug,
			  xhdmirx_delayed_work_enable_hotplug);

	xhdmirx_init(xhdmi);
	xhdmirx_disable_allintr(xhdmi);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(xhdmi->dev, "get irq failed %d\n", irq);
		ret = -EINVAL;
		goto wrkq_err;
	}

	ret = devm_request_threaded_irq(xhdmi->dev, irq, xhdmirx_irq_handler,
					xhdmirx_irq_thread, IRQF_ONESHOT,
					dev_name(xhdmi->dev), xhdmi);
	if (ret) {
		dev_err(xhdmi->dev, "failed to register irq handler %d\n", ret);
		goto wrkq_err;
	}

	ret = xhdmirx_parse_of(xhdmi);
	if (ret)
		goto wrkq_err;

	for (i = 0; i < XHDMI_MAX_LANES; i++) {
		char phy_name[16];

		snprintf(phy_name, sizeof(phy_name), "hdmi-phy%d", i);
		xhdmi->phy[i] = devm_phy_get(xhdmi->dev, phy_name);
		if (IS_ERR(xhdmi->phy[i])) {
			ret = PTR_ERR(xhdmi->phy[i]);
			xhdmi->phy[i] = NULL;
			dev_err_probe(xhdmi->dev, ret, "failed to get phy lane %s index %d\n",
				      phy_name, i);
			goto phy_err;
		}

		ret = phy_init(xhdmi->phy[i]);
		if (ret) {
			dev_err(xhdmi->dev, "failed to init phy lane %d\n", i);
			goto phy_err;
		}
	}

	sd = &xhdmi->sd;
	v4l2_subdev_init(sd, &xhdmirx_ops);
	sd->dev = xhdmi->dev;
	strscpy(sd->name, dev_name(xhdmi->dev), sizeof(sd->name));
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.ops = &xmedia_ops;
	v4l2_set_subdevdata(sd, xhdmi);
	xhdmi->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &xhdmi->pad);
	if (ret < 0) {
		dev_err(xhdmi->dev, "failed to init media %d\n", ret);
		goto phy_err;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(xhdmi->dev, "failed to register v4l subdev %d\n", ret);
		goto media_err;
	}

	ret = xhdmirx_probe_load_edid(xhdmi);
	if (ret) {
		dev_err(xhdmi->dev, "failed to load edid\n");
		goto v4lsd_reg_err;
	}

	/* register phy callbacks */
	phy_cfg.hdmi.phycb = 1;
	phy_cfg.hdmi.hdmiphycb.cb = phy_rxinit_cb;
	phy_cfg.hdmi.hdmiphycb.data = (void *)xhdmi;
	phy_cfg.hdmi.hdmiphycb.type = RX_INIT_CB;
	dev_dbg(xhdmi->dev, "config phy rxinit cb\n");
	xhdmirx_phy_configure(xhdmi, &phy_cfg);

	phy_cfg.hdmi.phycb = 1;
	phy_cfg.hdmi.hdmiphycb.cb = phy_rxready_cb;
	phy_cfg.hdmi.hdmiphycb.data = (void *)xhdmi;
	phy_cfg.hdmi.hdmiphycb.type = RX_READY_CB;
	dev_dbg(xhdmi->dev, "config phy rxready cb\n");
	xhdmirx_phy_configure(xhdmi, &phy_cfg);

	phy_cfg.hdmi.config_hdmi20 = 1;
	dev_dbg(xhdmi->dev, "set phy to hdmi20\n");
	xhdmirx_phy_configure(xhdmi, &phy_cfg);

	xhdmirx_enable_allintr(xhdmi);

	xhdmirx1_start(xhdmi);

	dev_info(xhdmi->dev, "driver probe successful\n");

	return 0;

v4lsd_reg_err:
	v4l2_async_unregister_subdev(sd);
media_err:
	media_entity_cleanup(&sd->entity);
phy_err:
	xhdmirx_phy_release(xhdmi);
wrkq_err:
	cancel_delayed_work(&xhdmi->delayed_work_enable_hotplug);
	destroy_workqueue(xhdmi->work_queue);
mutex_err:
	mutex_destroy(&xhdmi->xhdmi_mutex);
	clk_bulk_disable_unprepare(num_clks, xhdmi->clks);

	return ret;
}

static int xhdmirx_remove(struct platform_device *pdev)
{
	struct xhdmirx_state *xhdmi = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = &xhdmi->sd;
	int num_clks = ARRAY_SIZE(xhdmirx_clks);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	xhdmirx_phy_release(xhdmi);
	cancel_delayed_work(&xhdmi->delayed_work_enable_hotplug);
	destroy_workqueue(xhdmi->work_queue);
	mutex_destroy(&xhdmi->xhdmi_mutex);
	clk_bulk_disable_unprepare(num_clks, xhdmi->clks);

	dev_info(xhdmi->dev, "driver removed successfully\n");
	return 0;
}

static const struct of_device_id xhdmirx_of_id_table[] = {
	{ .compatible = "xlnx,v-hdmi-rxss1-1.1" },
	{ }
};

MODULE_DEVICE_TABLE(of, xhdmirx_of_id_table);

static struct platform_driver xhdmirx_driver = {
	.driver = {
		.name		= "xlnx-hdmi21rxss",
		.of_match_table	= xhdmirx_of_id_table,
	},
	.probe			= xhdmirx_probe,
	.remove			= xhdmirx_remove,
};

module_platform_driver(xhdmirx_driver);

MODULE_AUTHOR("Vishal Sagar <vishal.sagar@xilinx.com>");
MODULE_DESCRIPTION("Xilinx HDMI 2.1 Rx Subsystem Driver");
MODULE_LICENSE("GPL v2");

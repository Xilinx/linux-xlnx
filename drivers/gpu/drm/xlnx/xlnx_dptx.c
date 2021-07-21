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
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <sound/hdmi-codec.h>

/* Link configuration registers */
#define XDPTX_LINKBW_SET_REG			0x0
#define XDPTX_LANECNT_SET_REG			0x4
#define XDPTX_EFRAME_EN_REG			0x8
#define XDPTX_TRNGPAT_SET_REG			0xc
#define XDPTX_SCRAMBLING_DIS_REG		0x14
#define XDPTX_DOWNSPREAD_CTL_REG		0x18

/* DPTX Core enable registers */
#define XDPTX_ENABLE_REG			0x80
#define XDPTX_MAINSTRM_ENABLE_REG		0x84

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
#define XDPTX_MAINSTRM_MISC1_REG		0x1a8

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

#define XDPTX_VS_PE_LEVEL_MAXCOUNT		0x3
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

#define DP_INFOFRAME_SIZE(type)	\
	(DP_INFOFRAME_HEADER_SIZE + DP_ ## type ## _INFOFRAME_SIZE)

/**
 * struct xlnx_dptx_audio_data - Audio data structure
 * @buffer: Audio infoframe data buffer
 */
struct xlnx_dptx_audio_data {
	u32 buffer[DP_INFOFRAME_FIFO_SIZE_WORDS];
};

/*
 * struct xlnx_dp_link_config - Common link config between source and sink
 * @max_rate: Miaximum link rate
 * @max_lanes: Maximum number of lanes
 */
struct xlnx_dp_link_config {
	int max_rate;
	u8 max_lanes;
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
 * @tx_audio_data: audio data
 * @audio_pdev: audio platform device
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
	struct xlnx_dptx_audio_data *tx_audio_data;
	struct platform_device *audio_pdev;
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
};

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
		/* fall-through */
	case DRM_FORMAT_XRGB8888:
		/* fall-through */
	case DRM_FORMAT_BGR888:
		/* fall-through */
	case DRM_FORMAT_RGB888:
		/* fall-through */
	case DRM_FORMAT_XBGR2101010:
		config->misc0 |= XDPTX_MISC0_RGB_MASK;
		config->num_colors = 3;
		config->fmt = 0x0;
		break;
	case DRM_FORMAT_VUY888:
		/* fall-through */
	case DRM_FORMAT_XVUY8888:
		/* fall-through */
	case DRM_FORMAT_Y8:
		/* fall-through */
	case DRM_FORMAT_XVUY2101010:
		/* fall-through */
	case DRM_FORMAT_Y10:
		config->misc0 |= XDPTX_MISC0_YCRCB444_MASK;
		config->num_colors = 3;
		config->fmt = 0x1;
		break;
	case DRM_FORMAT_YUYV:
		/* fall-through */
	case DRM_FORMAT_UYVY:
		/* fall-through */
	case DRM_FORMAT_NV16:
		/* fall-through */
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
 * xlnx_dp_mode_configure - Configure the link values
 * @dp: DisplayPort IP core structure
 * @pclock: pixel clock for requested display mode
 * @current_bw: current link rate
 *
 * Find the link configuration values, rate and lane count for requested pixel
 * clock @pclock. The @pclock is stored in the mode to be used in other
 * functions later. The returned rate is downshifted from the current rate
 * @current_bw.
 *
 * Return: Current link rate code, or -EINVAL.
 */
static int xlnx_dp_mode_configure(struct xlnx_dp *dp, int pclock,
				  u8 current_bw)
{
	int max_rate = dp->link_config.max_rate;
	u8 bw_code;
	u8 max_lanes = dp->link_config.max_lanes;
	u8 max_link_rate_code = drm_dp_link_rate_to_bw_code(max_rate);
	u8 bpp = dp->config.bpp;
	u8 lane_cnt;

	/* Downshift from current bandwidth */
	switch (current_bw) {
	case DP_LINK_BW_8_1:
		bw_code = DP_LINK_BW_5_4;
		break;
	case DP_LINK_BW_5_4:
		bw_code = DP_LINK_BW_2_7;
		break;
	case DP_LINK_BW_2_7:
		bw_code = DP_LINK_BW_1_62;
		break;
	case DP_LINK_BW_1_62:
		dev_err(dp->dev, "can't downshift. already lowest link rate\n");
		return -EINVAL;
	default:
		/* If not given, start with max supported */
		bw_code = max_link_rate_code;
		break;
	}

	for (lane_cnt = max_lanes; lane_cnt >= 1; lane_cnt >>= 1) {
		int bw;
		u32 rate;

		bw = drm_dp_bw_code_to_link_rate(bw_code);
		rate = XDPTX_MAX_RATE(bw, lane_cnt, bpp);
		if (pclock <= rate) {
			dp->mode.bw_code = bw_code;
			dp->mode.lane_cnt = lane_cnt;
			dp->mode.pclock = pclock;
			return dp->mode.bw_code;
		}
	}

	dev_err(dp->dev, "failed to configure link values\n");

	return -EINVAL;
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

/**
 * xlnx_dp_tx_adj_vswing_preemp - Sets voltage swing and pre-emphasis levels
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

	phy_configure(dp->phy[0], &dp->phy_opts);

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
	u8 prev_vs_level = 0;
	u8 same_vs_level_count = 0;
	u8 aux_data;
	u16 max_tries;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	bool cr_done = 0;
	int ret;
	struct xlnx_dp_tx_link_config *link_config = &dp->tx_link_config;

	dp->tx_link_config.vs_level = 0;
	dp->tx_link_config.pe_level = 0;

	xlnx_dp_write(dp->dp_base, XDPTX_TRNGPAT_SET_REG,
		      DP_TRAINING_PATTERN_1);
	xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLING_DIS_REG, 1);
	aux_data = DP_TRAINING_PATTERN_1 | DP_LINK_SCRAMBLING_DISABLE;
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET, aux_data);
	if (ret < 0)
		return ret;

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
		drm_dp_link_train_clock_recovery_delay(dp->dpcd);
		/*
		 * check if all lanes have realized and maintained the
		 * frequency lock and get adjustment requests.
		 */
		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;
		cr_done = drm_dp_clock_recovery_ok(link_status, lane_cnt);
		if (cr_done)
			break;
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

		if (same_vs_level_count == XDPTX_VS_LEVEL_MAXCOUNT)
			break;

		if (link_config->vs_level == XDPTX_VS_PE_LEVEL_MAXCOUNT)
			break;

		ret = xlnx_dp_tx_adj_vswing_preemp(dp, link_status);
		if (ret < 0)
			return ret;
	}
	if (!cr_done) {
		dev_err(dp->dev, "training cr failed\n");
		return -ETIMEDOUT;
	}

	return 0;
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
	int ret;
	u8 pat;
	u32 i;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 aux_data[5];
	bool ce_done;

	if (dp->dpcd[DP_DPCD_REV] == XDPTX_V1_4 &&
	    dp->dpcd[DP_MAX_DOWNSPREAD] & DP_TPS4_SUPPORTED) {
		pat = DP_TRAINING_PATTERN_4;
	} else if (dp->dpcd[DP_DPCD_REV] >= XDPTX_V1_2 &&
		dp->dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED) {
		pat = DP_TRAINING_PATTERN_3;
	} else {
		pat = DP_TRAINING_PATTERN_2;
	}

	xlnx_dp_write(dp->dp_base, XDPTX_TRNGPAT_SET_REG, pat);

	if (dp->dpcd[DP_DPCD_REV] == XDPTX_V1_4) {
		xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLING_DIS_REG, 0);
		aux_data[0] = DP_TRAINING_PATTERN_4;
	} else {
		xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLING_DIS_REG, 1);
		aux_data[0] = pat | DP_LINK_SCRAMBLING_DISABLE;
	}
	xlnx_dp_tx_set_vswing_preemp(dp, &aux_data[1]);
	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_PATTERN_SET,
				&aux_data[0], 5);
	if (ret < 0)
		return ret;

	for (i = 0; i < 8; i++) {
		/*
		 * Obtain the required delay for channel equalization as
		 * specified by the RX device.
		 */
		drm_dp_link_train_channel_eq_delay(dp->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		/*
		 * check if all lanes have accomplished channel equalization,
		 * symbol lock, and interlane alignment.
		 */
		ce_done = drm_dp_channel_eq_ok(link_status, lane_cnt);
		if (ce_done)
			break;

		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		ret = xlnx_dp_tx_adj_vswing_preemp(dp, link_status);
		if (ret != 0)
			return ret;
	}
	/*
	 * Tried 8 times with no success. Try a reduced bitrate first, then
	 * reduce the number of lanes.
	 */
	if (!ce_done) {
		dev_err(dp->dev, "training ce failed\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * xlnx_dp_train - Train the link
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if all trains are done successfully, or corresponding error code.
 */
static int xlnx_dp_train(struct xlnx_dp *dp)
{
	u32 reg;
	u8 bw_code = dp->mode.bw_code;
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 aux_lane_cnt = lane_cnt;
	u8 data;
	bool enhanced;
	int ret;

	xlnx_dp_write(dp->dp_base, XDPTX_LANECNT_SET_REG, lane_cnt);
	enhanced = drm_dp_enhanced_frame_cap(dp->dpcd);
	if (enhanced) {
		xlnx_dp_write(dp->dp_base, XDPTX_EFRAME_EN_REG, 1);
		aux_lane_cnt |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	}

	if (dp->dpcd[3] & 0x1) {
		xlnx_dp_write(dp->dp_base, XDPTX_DOWNSPREAD_CTL_REG, 1);
		drm_dp_dpcd_writeb(&dp->aux, DP_MAX_DOWNSPREAD,
				   DP_MAX_DOWNSPREAD_0_5);
	} else {
		xlnx_dp_write(dp->dp_base, XDPTX_DOWNSPREAD_CTL_REG, 0);
		drm_dp_dpcd_writeb(&dp->aux, DP_DOWNSPREAD_CTRL, 0);
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LANE_COUNT_SET, aux_lane_cnt);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return ret;
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				 DP_SET_ANSI_8B10B);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set ANSI 8B/10B encoding\n");
		return ret;
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LINK_BW_SET, bw_code);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set DP bandwidth\n");
		return ret;
	}
	xlnx_dp_write(dp->dp_base, XDPTX_LINKBW_SET_REG, bw_code);

	switch (bw_code) {
	case DP_LINK_BW_1_62:
		reg = XDPTX_PHYCLOCK_FBSETTING162_MASK;
		break;
	case DP_LINK_BW_2_7:
		reg = XDPTX_PHYCLOCK_FBSETTING270_MASK;
		break;
	case DP_LINK_BW_5_4:
		/* fall-through */
	case DP_LINK_BW_8_1:
		reg = XDPTX_PHYCLOCK_FBSETTING810_MASK;
		break;
	default:
		reg = XDPTX_PHYCLOCK_FBSETTING810_MASK;
	}
	xlnx_dp_write(dp->dp_base, XDPTX_PHYCLOCK_FBSETTING_REG,
		      reg);
	ret = xlnx_dp_phy_ready(dp);
	if (ret < 0)
		return ret;

	memset(dp->train_set, 0, XDPTX_MAX_LANES);
	ret = xlnx_dp_link_train_cr(dp);
	if (ret)
		return ret;

	ret = xlnx_dp_link_train_ce(dp);
	if (ret)
		return ret;

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
	xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLING_DIS_REG, 0);

	xlnx_dp_write(dp->dp_base, XDPTX_TRNGPAT_SET_REG,
		      DP_TRAINING_PATTERN_DISABLE);
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 DP_TRAINING_PATTERN_DISABLE);
	if (ret < 0) {
		dev_err(dp->dev, "failed to disable training pattern\n");
		return ret;
	}
	/* Disable the scrambler.*/
	xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLING_DIS_REG, 0);

	return 0;
}

/**
 * xlnx_dp_train_loop - Downshift the link rate during training
 * @dp: DisplayPort IP core structure
 *
 * Train the link by downshifting the link rate if training is not successful.
 */
static void xlnx_dp_train_loop(struct xlnx_dp *dp)
{
	struct xlnx_dp_mode *mode = &dp->mode;
	u8 bw = mode->bw_code;
	int ret;

	do {
		if (dp->status == connector_status_disconnected ||
		    !dp->enabled)
			return;
		ret = xlnx_dp_train(dp);
		if (!ret)
			return;
		ret = xlnx_dp_mode_configure(dp, mode->pclock, bw);
		if (ret < 0)
			goto err_out;

		bw = ret;
	} while (bw >= DP_LINK_BW_1_62);

err_out:
	dev_err(dp->dev, "failed to train the DP link\n");
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
	if (!buf || !bytes)
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

		usleep_range(400, 500);
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

	if (w > 48) {
		dev_err(dp->dev, "aclk frequency too high\n");
		return -EINVAL;
	}
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

	xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_MISC0_REG, config->misc0);
	xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_MISC1_REG, 0x0);
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
	u32 tu = XDPTX_DEF_TRANSFER_UNITSIZE, temp;
	u32 bw, vid_kbytes, avg_bytes_per_tu, init_wait;

	/* Use the max transfer unit size (default) */
	xlnx_dp_write(dp->dp_base, XDPTX_TRANSFER_UNITSIZE_REG, tu);

	vid_kbytes = (mode->clock / 1000) * (dp->config.bpp / 8);
	bw = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	avg_bytes_per_tu = vid_kbytes * tu / (dp->mode.lane_cnt * bw);

	xlnx_dp_write(dp->dp_base, XDPTX_MINBYTES_PERTU_REG,
		      avg_bytes_per_tu / 1000);

	temp = (avg_bytes_per_tu % 1000) * 1024 / 1000;
	xlnx_dp_write(dp->dp_base, XDPTX_FRACBYTES_PERTU_REG, temp);

	/* Configure the initial wait cycle based on transfer unit size */
	if (tu < (avg_bytes_per_tu / 1000))
		init_wait = 0;
	else if ((avg_bytes_per_tu / 1000) <= 4)
		init_wait = tu;
	else
		init_wait = tu - avg_bytes_per_tu / 1000;

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
	u8 lane_cnt = dp->mode.lane_cnt;
	u32 reg, wpl;
	unsigned int ppc;

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

	reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	xlnx_dp_write(dp_base, XDPTX_N_VID_REG, reg);
	xlnx_dp_write(dp_base, XDPTX_M_VID_REG, mode->clock);

	/* In synchronous mode, set the dividers */
	if (dp->config.misc0 & XDPTX_MAINSTRM_MISC0_MASK) {
		reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
		xlnx_dp_write(dp_base, XDPTX_N_VID_REG, reg);
		xlnx_dp_write(dp_base, XDPTX_M_VID_REG, mode->clock);
	}

	if (mode->clock > 530000)
		ppc = 4;
	else if (mode->clock > 270000)
		ppc = 2;
	else
		ppc = 1;

	xlnx_dp_write(dp_base, XDPTX_USER_PIXELWIDTH_REG, ppc);
	dp->config.ppc = ppc;

	wpl = (mode->hdisplay * dp->config.bpp + 15) / 16;
	reg = wpl + wpl % lane_cnt - lane_cnt;
	xlnx_dp_write(dp_base, XDPTX_USER_DATACNTPERLANE_REG, reg);
	xlnx_dp_write(dp_base, XDPTX_TRANSFER_UNITSIZE_REG, 0x40);
}

/*
 * DRM connector functions
 */
static enum drm_connector_status
xlnx_dp_connector_detect(struct drm_connector *connector, bool force)
{
	struct xlnx_dp *dp = connector_to_dp(connector);
	struct xlnx_dp_link_config *link_config = &dp->link_config;
	struct phy_configure_opts_dp *phy_cfg = &dp->phy_opts.dp;
	u32 state, i;
	int ret;

	/*
	 * This is from heuristic. It takes some delay (ex, 100 ~ 500 msec) to
	 * get the HPD signal with some monitors.
	 */
	for (i = 0; i < 10; i++) {
		state = xlnx_dp_read(dp->dp_base, XDPTX_INTR_SIGSTATE_REG);
		if (state & XDPTX_INTR_SIGHPDSTATE)
			break;
		msleep(100);
	}
	if (state & XDPTX_INTR_SIGHPDSTATE) {
		ret = drm_dp_dpcd_read(&dp->aux, 0x0, dp->dpcd,
				       sizeof(dp->dpcd));
		if (ret < 0) {
			dev_info(dp->dev, "DPCD read failes");
			goto disconnected;
		}
		dp->dpcd[1] = 0x1e;
		link_config->max_rate = min_t(int,
					      drm_dp_max_link_rate(dp->dpcd),
					      dp->config.max_link_rate);
		link_config->max_lanes = min_t(u8,
					       drm_dp_max_lane_count(dp->dpcd),
					       dp->config.max_lanes);
		dp->status = connector_status_connected;

		switch (dp->dpcd[1]) {
		case DP_LINK_BW_1_62:
			phy_cfg->link_rate = 1620;
			break;
		case DP_LINK_BW_2_7:
			phy_cfg->link_rate = 2700;
			break;
		case DP_LINK_BW_5_4:
			phy_cfg->link_rate = 5400;
			break;
		case DP_LINK_BW_8_1:
			phy_cfg->link_rate = 8100;
			break;
		default:
			dev_err(dp->dev, "invalid link rate\n");
			break;
		}

		phy_cfg->set_rate = 1;
		phy_cfg->lanes = link_config->max_lanes;
		phy_configure(dp->phy[0], &dp->phy_opts);

		return connector_status_connected;
	}
disconnected:
	dp->status = connector_status_disconnected;

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

/**
 * audio_codec_startup - initialize audio during audio usecase
 * @dev: device
 * @data: optional data set during registration
 *
 * This function is called by ALSA framework before audio
 * playback begins. This callback initializes audio.
 *
 * Return: 0 on success
 */
static int audio_codec_startup(struct device *dev, void *data)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);

	/* Enabling the audio in dp core */
	xlnx_dp_clr(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_EN_MASK);

	xlnx_dp_set(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_EN_MASK);

	return 0;
}

/**
 * audio_codec_hw_params - sets the playback stream properties
 * @dev: device
 * @data: optional data set during registration
 * @fmt: Protocol between ASoC cpu-dai and HDMI-encoder
 * @hparams: stream parameters
 *
 * This function is called by ALSA framework after startup callback
 * packs the audio infoframe from stream paremters and programs ACR
 * block
 *
 * Return: 0 on success
 */
static int audio_codec_hw_params(struct device *dev, void *data,
				 struct hdmi_codec_daifmt *fmt,
				 struct hdmi_codec_params *hparams)
{
	struct hdmi_audio_infoframe *infoframe = &hparams->cea;
	struct xlnx_dp *dp = dev_get_drvdata(dev);
	u8 infopckt[DP_INFOFRAME_SIZE(AUDIO)] = {0};
	u8 *ptr = (u8 *)dp->tx_audio_data->buffer;

	/* Setting audio channels */
	xlnx_dp_write(dp->dp_base, XDPTX_AUDIO_CHANNELS_REG,
		      infoframe->channels - 1);

	hdmi_audio_infoframe_pack(infoframe, infopckt,
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
 * audio_codec_shutdown - Deinitialze audio when audio usecase is stopped
 * @dev: device
 * @data: optional data set during registration
 *
 * This function is called by ALSA framework before audio playback usecase
 * ends.
 */
static void audio_codec_shutdown(struct device *dev, void *data)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);

	/* Disabling the audio in dp core */
	xlnx_dp_clr(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_EN_MASK);
}

/**
 * audio_codec_digital_mute - mute or unmute audio
 * @dev: device
 * @data: optional data set during registration
 * @enable: enable or disable mute
 * @direction: direction to enable mute (capture/playback)
 *
 * This function is called by ALSA framework before audio usecase
 * starts and before audio usecase ends
 *
 * Return: 0 on success
 */
static int audio_codec_digital_mute(struct device *dev, void *data,
				    bool enable, int direction)
{
	struct xlnx_dp *dp = dev_get_drvdata(dev);

	if (enable)
		xlnx_dp_set(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_MUTE_MASK);
	else
		xlnx_dp_clr(dp->dp_base, XDPTX_AUDIO_CTRL_REG, XDPTX_AUDIO_MUTE_MASK);

	return 0;
}

static int audio_codec_get_eld(struct device *dev, void *data,
			       u8 *buf, size_t len)
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

static const struct hdmi_codec_ops audio_ops = {
	.audio_startup = audio_codec_startup,
	.hw_params = audio_codec_hw_params,
	.audio_shutdown = audio_codec_shutdown,
	.mute_stream = audio_codec_digital_mute,
	.get_eld = audio_codec_get_eld,
	.no_capture_mute = 1,
};

/**
 * dptx_register_aud_dev - register audio device
 * @dev: device
 *
 * This functions registers a new platform device and a corresponding
 * module is loaded which registers a audio codec device and
 * calls the registered callbacks
 *
 * Return: platform device
 */
static struct platform_device *dptx_register_aud_dev(struct device *dev)
{
	struct platform_device *audio_pdev;
	struct hdmi_codec_pdata codec_pdata = {
		.ops = &audio_ops,
		.i2s = 1,
		.max_i2s_channels = 8,
	};

	audio_pdev = platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
						   0, &codec_pdata,
						   sizeof(codec_pdata));

	return audio_pdev;
}

static void xlnx_dp_encoder_enable(struct drm_encoder *encoder)
{
	struct xlnx_dp *dp = encoder_to_dp(encoder);
	void __iomem *dp_base = dp->dp_base;
	unsigned int i;
	int ret = 0;
	unsigned int xlnx_dp_power_on_delay_ms = 4;

	pm_runtime_get_sync(dp->dev);
	dp->enabled = true;
	xlnx_dp_init_aux(dp);
	if (dp->status == connector_status_connected) {
		for (i = 0; i < 3; i++) {
			u8 value;

			ret = drm_dp_dpcd_readb(&dp->aux, DP_SET_POWER, &value);
			if (ret < 0)
				break;

			value &= ~DP_SET_POWER_MASK;
			value |= DP_SET_POWER_D3;
			ret = drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, value);
			if (ret < 0)
				break;

			value &= ~DP_SET_POWER_MASK;
			value |= DP_SET_POWER_D0;
			ret = drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, value);
			if (ret == 0) {
				/* Per DP spec, the sink exits within 1 msec */
				usleep_range(1000, 2000);
				break;
			}
			usleep_range(300, 500);
		}
		/* Some monitors take time to wake up properly */
		msleep(xlnx_dp_power_on_delay_ms);
	}
	if (ret < 0)
		dev_err(dp->dev, "DP aux failed\n");
	else
		xlnx_dp_train_loop(dp);

	/* Enable VTC */
	xlnx_dp_set(dp->dp_base,
		    XDPTX_VTC_BASE + XDPTX_VTC_CTL, XDPTX_VTC_CTL_GE);
	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_ENABLE_REG, 1);
}

static void xlnx_dp_encoder_disable(struct drm_encoder *encoder)
{
	struct xlnx_dp *dp = encoder_to_dp(encoder);
	void __iomem *dp_base = dp->dp_base;

	xlnx_dp_write(dp_base, XDPTX_MAINSTRM_ENABLE_REG, 0);
	dp->enabled = false;
	cancel_delayed_work(&dp->hpd_work);
	/* Disable VTC */
	xlnx_dp_clr(dp->dp_base,
		    XDPTX_VTC_BASE + XDPTX_VTC_CTL, XDPTX_VTC_CTL_GE);
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
	u8 max_lanes = dp->link_config.max_lanes;
	u8 bpp = dp->config.bpp;
	int rate, max_rate = dp->link_config.max_rate;
	int ret;
	u32 clock;
	u32 drm_fourcc;

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
	ret = xlnx_dp_mode_configure(dp, adjusted_mode->clock, 0);
	if (ret < 0)
		return;

	/* The timing register should be programmed always */
	xlnx_dp_encoder_mode_set_stream(dp, adjusted_mode);
	xlnx_dp_encoder_mode_set_transfer_unit(dp, adjusted_mode);
	clock = adjusted_mode->clock * 1000;
	clk_set_rate(dp->tx_vid_clk, clock / dp->config.ppc);

	xlnx_dp_vtc_set_timing(dp, adjusted_mode);
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

	/* This enables interrupts, so should be called after DRM init */
	ret = xlnx_dp_init_aux(dp);
	if (ret) {
		dev_err(dp->dev, "failed to initialize DP aux");
		goto error_prop;
	}
	INIT_DELAYED_WORK(&dp->hpd_work, xlnx_dp_hpd_work_func);

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
	xlnx_dp_exit_aux(dp);
	drm_property_destroy(dp->drm, dp->bpc_prop);
	drm_property_destroy(dp->drm, dp->sync_prop);
	xlnx_dp_connector_destroy(&dp->connector);
	drm_encoder_cleanup(&dp->encoder);
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

static void xlnx_dp_vsync_handler(struct xlnx_dp *dp)
{
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

	return 0;
}

static int xlnx_dp_probe(struct platform_device *pdev)
{
	struct xlnx_dp *dp;
	struct resource *res;
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
		dp->audio_pdev = dptx_register_aud_dev(dp->dev);
		if (IS_ERR(dp->audio_pdev)) {
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
	dev_dbg(&pdev->dev, "xdprxss_probe() error_phy:\n");
	xlnx_dp_exit_phy(dp);

	return ret;
}

static int xlnx_dp_remove(struct platform_device *pdev)
{
	struct xlnx_dp *dp = platform_get_drvdata(pdev);

	xlnx_dp_write(dp->dp_base, XDPTX_ENABLE_REG, 0);
	drm_dp_aux_unregister(&dp->aux);
	xlnx_dp_exit_phy(dp);
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

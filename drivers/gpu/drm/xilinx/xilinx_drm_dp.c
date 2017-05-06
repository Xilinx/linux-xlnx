/*
 * Xilinx DRM DisplayPort encoder driver for Xilinx
 *
 *  Copyright (C) 2014 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyunk@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_encoder_slave.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-zynqmp.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "xilinx_drm_dp_sub.h"
#include "xilinx_drm_drv.h"

static uint xilinx_drm_dp_aux_timeout_ms = 50;
module_param_named(aux_timeout_ms, xilinx_drm_dp_aux_timeout_ms, uint, 0444);
MODULE_PARM_DESC(aux_timeout_ms,
		 "DP aux timeout value in msec (default: 50)");

/* Link configuration registers */
#define XILINX_DP_TX_LINK_BW_SET			0x0
#define XILINX_DP_TX_LANE_CNT_SET			0x4
#define XILINX_DP_TX_ENHANCED_FRAME_EN			0x8
#define XILINX_DP_TX_TRAINING_PATTERN_SET		0xc
#define XILINX_DP_TX_SCRAMBLING_DISABLE			0x14
#define XILINX_DP_TX_DOWNSPREAD_CTL			0x18
#define XILINX_DP_TX_SW_RESET				0x1c
#define XILINX_DP_TX_SW_RESET_STREAM1			(1 << 0)
#define XILINX_DP_TX_SW_RESET_STREAM2			(1 << 1)
#define XILINX_DP_TX_SW_RESET_STREAM3			(1 << 2)
#define XILINX_DP_TX_SW_RESET_STREAM4			(1 << 3)
#define XILINX_DP_TX_SW_RESET_AUX			(1 << 7)
#define XILINX_DP_TX_SW_RESET_ALL			(XILINX_DP_TX_SW_RESET_STREAM1 | \
							 XILINX_DP_TX_SW_RESET_STREAM2 | \
							 XILINX_DP_TX_SW_RESET_STREAM3 | \
							 XILINX_DP_TX_SW_RESET_STREAM4 | \
							 XILINX_DP_TX_SW_RESET_AUX)

/* Core enable registers */
#define XILINX_DP_TX_ENABLE				0x80
#define XILINX_DP_TX_ENABLE_MAIN_STREAM			0x84
#define XILINX_DP_TX_FORCE_SCRAMBLER_RESET		0xc0
#define XILINX_DP_TX_VERSION				0xf8
#define XILINX_DP_TX_VERSION_MAJOR_MASK			(0xff << 24)
#define XILINX_DP_TX_VERSION_MAJOR_SHIFT		24
#define XILINX_DP_TX_VERSION_MINOR_MASK			(0xff << 16)
#define XILINX_DP_TX_VERSION_MINOR_SHIFT		16
#define XILINX_DP_TX_VERSION_REVISION_MASK		(0xf << 12)
#define XILINX_DP_TX_VERSION_REVISION_SHIFT		12
#define XILINX_DP_TX_VERSION_PATCH_MASK			(0xf << 8)
#define XILINX_DP_TX_VERSION_PATCH_SHIFT		8
#define XILINX_DP_TX_VERSION_INTERNAL_MASK		(0xff << 0)
#define XILINX_DP_TX_VERSION_INTERNAL_SHIFT		0

/* Core ID registers */
#define XILINX_DP_TX_CORE_ID				0xfc
#define XILINX_DP_TX_CORE_ID_MAJOR_MASK			(0xff << 24)
#define XILINX_DP_TX_CORE_ID_MAJOR_SHIFT		24
#define XILINX_DP_TX_CORE_ID_MINOR_MASK			(0xff << 16)
#define XILINX_DP_TX_CORE_ID_MINOR_SHIFT		16
#define XILINX_DP_TX_CORE_ID_REVISION_MASK		(0xff << 8)
#define XILINX_DP_TX_CORE_ID_REVISION_SHIFT		8
#define XILINX_DP_TX_CORE_ID_DIRECTION			(1 << 0)

/* AUX channel interface registers */
#define XILINX_DP_TX_AUX_COMMAND			0x100
#define XILINX_DP_TX_AUX_COMMAND_CMD_SHIFT		8
#define XILINX_DP_TX_AUX_COMMAND_ADDRESS_ONLY		BIT(12)
#define XILINX_DP_TX_AUX_COMMAND_BYTES_SHIFT		0
#define XILINX_DP_TX_AUX_WRITE_FIFO			0x104
#define XILINX_DP_TX_AUX_ADDRESS			0x108
#define XILINX_DP_TX_CLK_DIVIDER			0x10c
#define XILINX_DP_TX_CLK_DIVIDER_MHZ			1000000
#define XILINX_DP_TX_CLK_DIVIDER_AUX_FILTER_SHIFT	8
#define XILINX_DP_TX_INTR_SIGNAL_STATE			0x130
#define XILINX_DP_TX_INTR_SIGNAL_STATE_HPD		(1 << 0)
#define XILINX_DP_TX_INTR_SIGNAL_STATE_REQUEST		(1 << 1)
#define XILINX_DP_TX_INTR_SIGNAL_STATE_REPLY		(1 << 2)
#define XILINX_DP_TX_INTR_SIGNAL_STATE_REPLY_TIMEOUT	(1 << 3)
#define XILINX_DP_TX_AUX_REPLY_DATA			0x134
#define XILINX_DP_TX_AUX_REPLY_CODE			0x138
#define XILINX_DP_TX_AUX_REPLY_CODE_AUX_ACK		(0)
#define XILINX_DP_TX_AUX_REPLY_CODE_AUX_NACK		(1 << 0)
#define XILINX_DP_TX_AUX_REPLY_CODE_AUX_DEFER		(1 << 1)
#define XILINX_DP_TX_AUX_REPLY_CODE_I2C_ACK		(0)
#define XILINX_DP_TX_AUX_REPLY_CODE_I2C_NACK		(1 << 2)
#define XILINX_DP_TX_AUX_REPLY_CODE_I2C_DEFER		(1 << 3)
#define XILINX_DP_TX_AUX_REPLY_CNT			0x13c
#define XILINX_DP_TX_AUX_REPLY_CNT_MASK			0xff
#define XILINX_DP_TX_INTR_STATUS			0x140
#define XILINX_DP_TX_INTR_MASK				0x144
#define XILINX_DP_TX_INTR_HPD_IRQ			(1 << 0)
#define XILINX_DP_TX_INTR_HPD_EVENT			(1 << 1)
#define XILINX_DP_TX_INTR_REPLY_RECV			(1 << 2)
#define XILINX_DP_TX_INTR_REPLY_TIMEOUT			(1 << 3)
#define XILINX_DP_TX_INTR_HPD_PULSE			(1 << 4)
#define XILINX_DP_TX_INTR_EXT_PKT_TXD			(1 << 5)
#define XILINX_DP_TX_INTR_LIV_ABUF_UNDRFLW		(1 << 12)
#define XILINX_DP_TX_INTR_VBLANK_START			(1 << 13)
#define XILINX_DP_TX_INTR_PIXEL0_MATCH			(1 << 14)
#define XILINX_DP_TX_INTR_PIXEL1_MATCH			(1 << 15)
#define XILINX_DP_TX_INTR_CHBUF_UNDERFLW_MASK		0x3f0000
#define XILINX_DP_TX_INTR_CHBUF_OVERFLW_MASK		0xfc00000
#define XILINX_DP_TX_INTR_CUST_TS_2			(1 << 28)
#define XILINX_DP_TX_INTR_CUST_TS			(1 << 29)
#define XILINX_DP_TX_INTR_EXT_VSYNC_TS			(1 << 30)
#define XILINX_DP_TX_INTR_VSYNC_TS			(1 << 31)
#define XILINX_DP_TX_INTR_ALL				(XILINX_DP_TX_INTR_HPD_IRQ | \
							 XILINX_DP_TX_INTR_HPD_EVENT | \
							 XILINX_DP_TX_INTR_REPLY_RECV | \
							 XILINX_DP_TX_INTR_REPLY_TIMEOUT | \
							 XILINX_DP_TX_INTR_HPD_PULSE | \
							 XILINX_DP_TX_INTR_EXT_PKT_TXD | \
							 XILINX_DP_TX_INTR_LIV_ABUF_UNDRFLW | \
							 XILINX_DP_TX_INTR_VBLANK_START | \
							 XILINX_DP_TX_INTR_CHBUF_UNDERFLW_MASK | \
							 XILINX_DP_TX_INTR_CHBUF_OVERFLW_MASK)
#define XILINX_DP_TX_REPLY_DATA_CNT			0x148
#define XILINX_DP_SUB_TX_INTR_STATUS			0x3a0
#define XILINX_DP_SUB_TX_INTR_MASK			0x3a4
#define XILINX_DP_SUB_TX_INTR_EN			0x3a8
#define XILINX_DP_SUB_TX_INTR_DS			0x3ac

/* Main stream attribute registers */
#define XILINX_DP_TX_MAIN_STREAM_HTOTAL			0x180
#define XILINX_DP_TX_MAIN_STREAM_VTOTAL			0x184
#define XILINX_DP_TX_MAIN_STREAM_POLARITY		0x188
#define XILINX_DP_TX_MAIN_STREAM_POLARITY_HSYNC_SHIFT	0
#define XILINX_DP_TX_MAIN_STREAM_POLARITY_VSYNC_SHIFT	1
#define XILINX_DP_TX_MAIN_STREAM_HSWIDTH		0x18c
#define XILINX_DP_TX_MAIN_STREAM_VSWIDTH		0x190
#define XILINX_DP_TX_MAIN_STREAM_HRES			0x194
#define XILINX_DP_TX_MAIN_STREAM_VRES			0x198
#define XILINX_DP_TX_MAIN_STREAM_HSTART			0x19c
#define XILINX_DP_TX_MAIN_STREAM_VSTART			0x1a0
#define XILINX_DP_TX_MAIN_STREAM_MISC0			0x1a4
#define XILINX_DP_TX_MAIN_STREAM_MISC0_SYNC		(1 << 0)
#define XILINX_DP_TX_MAIN_STREAM_MISC0_FORMAT_SHIFT	1
#define XILINX_DP_TX_MAIN_STREAM_MISC0_DYNAMIC_RANGE	(1 << 3)
#define XILINX_DP_TX_MAIN_STREAM_MISC0_YCBCR_COLRIMETRY	(1 << 4)
#define XILINX_DP_TX_MAIN_STREAM_MISC0_BPC_SHIFT	5
#define XILINX_DP_TX_MAIN_STREAM_MISC1			0x1a8
#define XILINX_DP_TX_MAIN_STREAM_MISC0_INTERLACED_VERT	(1 << 0)
#define XILINX_DP_TX_MAIN_STREAM_MISC0_STEREO_VID_SHIFT	1
#define XILINX_DP_TX_M_VID				0x1ac
#define XILINX_DP_TX_TRANSFER_UNIT_SIZE			0x1b0
#define XILINX_DP_TX_DEF_TRANSFER_UNIT_SIZE		64
#define XILINX_DP_TX_N_VID				0x1b4
#define XILINX_DP_TX_USER_PIXEL_WIDTH			0x1b8
#define XILINX_DP_TX_USER_DATA_CNT_PER_LANE		0x1bc
#define XILINX_DP_TX_MIN_BYTES_PER_TU			0x1c4
#define XILINX_DP_TX_FRAC_BYTES_PER_TU			0x1c8
#define XILINX_DP_TX_INIT_WAIT				0x1cc

/* PHY configuration and status registers */
#define XILINX_DP_TX_PHY_CONFIG				0x200
#define XILINX_DP_TX_PHY_CONFIG_PHY_RESET		(1 << 0)
#define XILINX_DP_TX_PHY_CONFIG_GTTX_RESET		(1 << 1)
#define XILINX_DP_TX_PHY_CONFIG_PHY_PMA_RESET		(1 << 8)
#define XILINX_DP_TX_PHY_CONFIG_PHY_PCS_RESET		(1 << 9)
#define XILINX_DP_TX_PHY_CONFIG_ALL_RESET		(XILINX_DP_TX_PHY_CONFIG_PHY_RESET | \
							 XILINX_DP_TX_PHY_CONFIG_GTTX_RESET | \
							 XILINX_DP_TX_PHY_CONFIG_PHY_PMA_RESET | \
							 XILINX_DP_TX_PHY_CONFIG_PHY_PCS_RESET)
#define XILINX_DP_TX_PHY_PREEMPHASIS_LANE_0		0x210
#define XILINX_DP_TX_PHY_PREEMPHASIS_LANE_1		0x214
#define XILINX_DP_TX_PHY_PREEMPHASIS_LANE_2		0x218
#define XILINX_DP_TX_PHY_PREEMPHASIS_LANE_3		0x21c
#define XILINX_DP_TX_PHY_VOLTAGE_DIFF_LANE_0		0x220
#define XILINX_DP_TX_PHY_VOLTAGE_DIFF_LANE_1		0x224
#define XILINX_DP_TX_PHY_VOLTAGE_DIFF_LANE_2		0x228
#define XILINX_DP_TX_PHY_VOLTAGE_DIFF_LANE_3		0x22c
#define XILINX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING		0x234
#define XILINX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_162	0x1
#define XILINX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_270	0x3
#define XILINX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_540	0x5
#define XILINX_DP_TX_PHY_POWER_DOWN			0x238
#define XILINX_DP_TX_PHY_POWER_DOWN_LANE_0		(1 << 0)
#define XILINX_DP_TX_PHY_POWER_DOWN_LANE_1		(1 << 1)
#define XILINX_DP_TX_PHY_POWER_DOWN_LANE_2		(1 << 2)
#define XILINX_DP_TX_PHY_POWER_DOWN_LANE_3		(1 << 3)
#define XILINX_DP_TX_PHY_POWER_DOWN_ALL			0xf
#define XILINX_DP_TX_PHY_PRECURSOR_LANE_0		0x23c
#define XILINX_DP_TX_PHY_PRECURSOR_LANE_1		0x240
#define XILINX_DP_TX_PHY_PRECURSOR_LANE_2		0x244
#define XILINX_DP_TX_PHY_PRECURSOR_LANE_3		0x248
#define XILINX_DP_TX_PHY_POSTCURSOR_LANE_0		0x24c
#define XILINX_DP_TX_PHY_POSTCURSOR_LANE_1		0x250
#define XILINX_DP_TX_PHY_POSTCURSOR_LANE_2		0x254
#define XILINX_DP_TX_PHY_POSTCURSOR_LANE_3		0x258
#define XILINX_DP_SUB_TX_PHY_PRECURSOR_LANE_0		0x24c
#define XILINX_DP_SUB_TX_PHY_PRECURSOR_LANE_1		0x250
#define XILINX_DP_TX_PHY_STATUS				0x280
#define XILINX_DP_TX_PHY_STATUS_PLL_LOCKED_SHIFT	4
#define XILINX_DP_TX_PHY_STATUS_FPGA_PLL_LOCKED		(1 << 6)

/* Audio registers */
#define XILINX_DP_TX_AUDIO_CONTROL			0x300
#define XILINX_DP_TX_AUDIO_CHANNELS			0x304
#define XILINX_DP_TX_AUDIO_INFO_DATA			0x308
#define XILINX_DP_TX_AUDIO_M_AUD			0x328
#define XILINX_DP_TX_AUDIO_N_AUD			0x32c
#define XILINX_DP_TX_AUDIO_EXT_DATA			0x330

#define XILINX_DP_MISC0_RGB				(0)
#define XILINX_DP_MISC0_YCRCB_422			(5 << 1)
#define XILINX_DP_MISC0_YCRCB_444			(6 << 1)
#define XILINX_DP_MISC0_BPC_6				(0 << 5)
#define XILINX_DP_MISC0_BPC_8				(1 << 5)
#define XILINX_DP_MISC0_BPC_10				(2 << 5)
#define XILINX_DP_MISC0_BPC_12				(3 << 5)
#define XILINX_DP_MISC0_BPC_16				(4 << 5)
#define XILINX_DP_MISC1_Y_ONLY				(1 << 7)

#define DP_REDUCED_BIT_RATE				162000
#define DP_HIGH_BIT_RATE				270000
#define DP_HIGH_BIT_RATE2				540000
#define DP_MAX_TRAINING_TRIES				5
#define DP_MAX_LANES					4

enum dp_version {
	DP_V1_1A = 0x11,
	DP_V1_2 = 0x12
};

/**
 * struct xilinx_drm_dp_link_config - Common link config between source and sink
 * @max_rate: maximum link rate
 * @max_lanes: maximum number of lanes
 */
struct xilinx_drm_dp_link_config {
	int max_rate;
	u8 max_lanes;
};

/**
 * struct xilinx_drm_dp_mode - Configured mode of DisplayPort
 * @bw_code: code for bandwidth(link rate)
 * @lane_cnt: number of lanes
 * @pclock: pixel clock frequency of current mode
 */
struct xilinx_drm_dp_mode {
	u8 bw_code;
	u8 lane_cnt;
	int pclock;
};

/**
 * struct xilinx_drm_dp_config - Configuration of DisplayPort from DTS
 * @dp_version: DisplayPort protocol version
 * @max_lanes: max number of lanes
 * @max_link_rate: max link rate
 * @max_bpc: maximum bits-per-color
 * @max_pclock: maximum pixel clock rate
 * @enable_yonly: enable yonly color space logic
 * @enable_ycrcb: enable ycrcb color space logic
 * @misc0: misc0 configuration (per DP v1.2 spec)
 * @misc1: misc1 configuration (per DP v1.2 spec)
 * @bpp: bits per pixel
 */
struct xilinx_drm_dp_config {
	enum dp_version dp_version;
	u32 max_lanes;
	u32 max_link_rate;
	u32 max_bpc;
	u32 max_pclock;
	bool enable_yonly;
	bool enable_ycrcb;

	u8 misc0;
	u8 misc1;
	u8 bpp;
};

/**
 * struct xilinx_drm_dp - Xilinx DisplayPort core
 * @encoder: pointer to the drm encoder structure
 * @dev: device structure
 * @iomem: device I/O memory for register access
 * @config: IP core configuration from DTS
 * @aux: aux channel
 * @dp_sub: DisplayPort subsystem
 * @phy: PHY handles for DP lanes
 * @aclk: clock source device for internal axi4-lite clock
 * @aud_clk: clock source device for audio clock
 * @dpms: current dpms state
 * @dpcd: DP configuration data from currently connected sink device
 * @link_config: common link configuration between IP core and sink device
 * @mode: current mode between IP core and sink device
 * @train_set: set of training data
 */
struct xilinx_drm_dp {
	struct drm_encoder *encoder;
	struct device *dev;
	void __iomem *iomem;

	struct xilinx_drm_dp_config config;
	struct drm_dp_aux aux;
	struct xilinx_drm_dp_sub *dp_sub;
	struct phy *phy[DP_MAX_LANES];
	struct clk *aclk;
	struct clk *aud_clk;

	int dpms;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	struct xilinx_drm_dp_link_config link_config;
	struct xilinx_drm_dp_mode mode;
	u8 train_set[DP_MAX_LANES];
};

static inline struct xilinx_drm_dp *to_dp(struct drm_encoder *encoder)
{
	return to_encoder_slave(encoder)->slave_priv;
}

#define AUX_READ_BIT	0x1

/**
 * xilinx_drm_dp_aux_cmd_submit - Submit aux command
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
static int xilinx_drm_dp_aux_cmd_submit(struct xilinx_drm_dp *dp, u32 cmd,
					u16 addr, u8 *buf, u8 bytes, u8 *reply)
{
	bool is_read = (cmd & AUX_READ_BIT) ? true : false;
	void __iomem *iomem = dp->iomem;
	u32 reg, i;

	reg = xilinx_drm_readl(iomem, XILINX_DP_TX_INTR_SIGNAL_STATE);
	if (reg & XILINX_DP_TX_INTR_SIGNAL_STATE_REQUEST)
		return -EBUSY;

	xilinx_drm_writel(iomem, XILINX_DP_TX_AUX_ADDRESS, addr);

	if (!is_read)
		for (i = 0; i < bytes; i++)
			xilinx_drm_writel(iomem, XILINX_DP_TX_AUX_WRITE_FIFO,
					  buf[i]);

	reg = cmd << XILINX_DP_TX_AUX_COMMAND_CMD_SHIFT;
	if (!buf || !bytes)
		reg |= XILINX_DP_TX_AUX_COMMAND_ADDRESS_ONLY;
	else
		reg |= (bytes - 1) << XILINX_DP_TX_AUX_COMMAND_BYTES_SHIFT;
	xilinx_drm_writel(iomem, XILINX_DP_TX_AUX_COMMAND, reg);

	/* Wait for reply to be delivered upto 2ms */
	for (i = 0; ; i++) {
		reg = xilinx_drm_readl(iomem, XILINX_DP_TX_INTR_SIGNAL_STATE);

		if (reg & XILINX_DP_TX_INTR_SIGNAL_STATE_REPLY)
			break;

		if (reg & XILINX_DP_TX_INTR_SIGNAL_STATE_REPLY_TIMEOUT ||
		    i == 2)
			return -ETIMEDOUT;

		usleep_range(1000, 1100);
	}

	reg = xilinx_drm_readl(iomem, XILINX_DP_TX_AUX_REPLY_CODE);
	if (reply)
		*reply = reg;

	if (is_read &&
	    (reg == XILINX_DP_TX_AUX_REPLY_CODE_AUX_ACK ||
	     reg == XILINX_DP_TX_AUX_REPLY_CODE_I2C_ACK)) {
		reg = xilinx_drm_readl(iomem, XILINX_DP_TX_REPLY_DATA_CNT);
		if ((reg & XILINX_DP_TX_AUX_REPLY_CNT_MASK) != bytes)
			return -EIO;

		for (i = 0; i < bytes; i++)
			buf[i] = xilinx_drm_readl(iomem,
						  XILINX_DP_TX_AUX_REPLY_DATA);
	}

	return 0;
}

/**
 * xilinx_drm_dp_phy_ready - Check if PHY is ready
 * @dp: DisplayPort IP core structure
 *
 * Check if PHY is ready. If PHY is not ready, wait 1ms to check for 100 times.
 * This amount of delay was suggested by IP designer.
 *
 * Return: 0 if PHY is ready, or -ENODEV if PHY is not ready.
 */
static int xilinx_drm_dp_phy_ready(struct xilinx_drm_dp *dp)
{
	u32 i, reg, ready, lane;

	lane = dp->config.max_lanes;
	ready = (1 << lane) - 1;
	if (!dp->dp_sub)
		ready |= XILINX_DP_TX_PHY_STATUS_FPGA_PLL_LOCKED;

	/* Wait for 100 * 1ms. This should be enough time for PHY to be ready */
	for (i = 0; ; i++) {
		reg = xilinx_drm_readl(dp->iomem, XILINX_DP_TX_PHY_STATUS);
		if ((reg & ready) == ready)
			return 0;

		if (i == 100) {
			DRM_ERROR("PHY isn't ready\n");
			return -ENODEV;
		}

		usleep_range(1000, 1100);
	}

	return 0;
}

/**
 * xilinx_drm_dp_max_rate - Calculate and return available max pixel clock
 * @link_rate: link rate (Kilo-bytes / sec)
 * @lane_num: number of lanes
 * @bpp: bits per pixel
 *
 * Return: max pixel clock (KHz) supported by current link config.
 */
static inline int xilinx_drm_dp_max_rate(int link_rate, u8 lane_num, u8 bpp)
{
	return link_rate * lane_num * 8 / bpp;
}

/**
 * xilinx_drm_dp_mode_configure - Configure the link values
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
static int xilinx_drm_dp_mode_configure(struct xilinx_drm_dp *dp, int pclock,
					u8 current_bw)
{
	int max_rate = dp->link_config.max_rate;
	u8 bws[3] = { DP_LINK_BW_1_62, DP_LINK_BW_2_7, DP_LINK_BW_5_4 };
	u8 max_lanes = dp->link_config.max_lanes;
	u8 max_link_rate_code = drm_dp_link_rate_to_bw_code(max_rate);
	u8 bpp = dp->config.bpp;
	u8 lane_cnt;
	s8 clock, i;

	for (i = ARRAY_SIZE(bws) - 1; i >= 0; i--) {
		if (current_bw && bws[i] >= current_bw)
			continue;

		if (bws[i] <= max_link_rate_code)
			break;
	}

	for (lane_cnt = 1; lane_cnt <= max_lanes; lane_cnt <<= 1) {
		for (clock = i; clock >= 0; clock--) {
			int bw;
			u32 rate;

			bw = drm_dp_bw_code_to_link_rate(bws[clock]);
			rate = xilinx_drm_dp_max_rate(bw, lane_cnt, bpp);
			if (pclock <= rate) {
				dp->mode.bw_code = bws[clock];
				dp->mode.lane_cnt = lane_cnt;
				dp->mode.pclock = pclock;
				return bws[clock];
			}
		}
	}

	DRM_ERROR("failed to configure link values\n");

	return -EINVAL;
}

/**
 * xilinx_drm_dp_adjust_train - Adjust train values
 * @dp: DisplayPort IP core structure
 * @link_status: link status from sink which contains requested training values
 */
static void xilinx_drm_dp_adjust_train(struct xilinx_drm_dp *dp,
				       u8 link_status[DP_LINK_STATUS_SIZE])
{
	u8 *train_set = dp->train_set;
	u8 voltage = 0, preemphasis = 0;
	u8 max_preemphasis;
	u8 i;

	for (i = 0; i < dp->mode.lane_cnt; i++) {
		u8 v = drm_dp_get_adjust_request_voltage(link_status, i);
		u8 p = drm_dp_get_adjust_request_pre_emphasis(link_status, i);

		if (v > voltage)
			voltage = v;

		if (p > preemphasis)
			preemphasis = p;
	}

	if (voltage >= DP_TRAIN_VOLTAGE_SWING_LEVEL_3)
		voltage |= DP_TRAIN_MAX_SWING_REACHED;

	max_preemphasis = (dp->dp_sub) ? DP_TRAIN_PRE_EMPH_LEVEL_2 :
					 DP_TRAIN_PRE_EMPH_LEVEL_3;

	if (preemphasis >= max_preemphasis)
		preemphasis |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	for (i = 0; i < dp->mode.lane_cnt; i++)
		train_set[i] = voltage | preemphasis;
}

/**
 * xilinx_drm_dp_update_vs_emph - Update the training values
 * @dp: DisplayPort IP core structure
 *
 * Update the training values based on the request from sink. The mapped values
 * are predefined, and values(vs, pe, pc) are from the device manual.
 *
 * Return: 0 if vs and emph are updated successfully, or the error code returned
 * by drm_dp_dpcd_write().
 */
static int xilinx_drm_dp_update_vs_emph(struct xilinx_drm_dp *dp)
{
	u8 *train_set = dp->train_set;
	u8 i, v_level, p_level;
	int ret;
	static u8 vs[4][4] = { { 0x2a, 0x27, 0x24, 0x20 },
			       { 0x27, 0x23, 0x20, 0xff },
			       { 0x24, 0x20, 0xff, 0xff },
			       { 0xff, 0xff, 0xff, 0xff } };
	static u8 pe[4][4] = { { 0x2, 0x2, 0x2, 0x2 },
			       { 0x1, 0x1, 0x1, 0xff },
			       { 0x0, 0x0, 0xff, 0xff },
			       { 0xff, 0xff, 0xff, 0xff } };

	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET, train_set,
				dp->mode.lane_cnt);
	if (ret < 0)
		return ret;

	for (i = 0; i < dp->mode.lane_cnt; i++) {
		v_level = (train_set[i] & DP_TRAIN_VOLTAGE_SWING_MASK) >>
			  DP_TRAIN_VOLTAGE_SWING_SHIFT;
		p_level = (train_set[i] & DP_TRAIN_PRE_EMPHASIS_MASK) >>
			  DP_TRAIN_PRE_EMPHASIS_SHIFT;

		if (dp->phy[i]) {
			u32 reg = XILINX_DP_SUB_TX_PHY_PRECURSOR_LANE_0 + i * 4;

			xpsgtr_margining_factor(dp->phy[i], p_level, v_level);
			xpsgtr_override_deemph(dp->phy[i], p_level, v_level);
			xilinx_drm_writel(dp->iomem, reg, 0x2);
		} else {
			u32 reg;

			reg = XILINX_DP_TX_PHY_VOLTAGE_DIFF_LANE_0 + i + 4;
			xilinx_drm_writel(dp->iomem, reg, vs[p_level][v_level]);
			reg = XILINX_DP_TX_PHY_PRECURSOR_LANE_0 + i + 4;
			xilinx_drm_writel(dp->iomem, reg, pe[p_level][v_level]);
			reg = XILINX_DP_TX_PHY_POSTCURSOR_LANE_0 + i + 4;
			xilinx_drm_writel(dp->iomem, reg, 0);
		}
	}

	return 0;
}

/**
 * xilinx_drm_dp_link_train_cr - Train clock recovery
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if clock recovery train is done successfully, or corresponding
 * error code.
 */
static int xilinx_drm_dp_link_train_cr(struct xilinx_drm_dp *dp)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 vs = 0, tries = 0;
	u16 max_tries, i;
	bool cr_done;
	int ret;

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 DP_TRAINING_PATTERN_1 |
				 DP_LINK_SCRAMBLING_DISABLE);
	if (ret < 0)
		return ret;

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_TRAINING_PATTERN_SET,
			  DP_TRAINING_PATTERN_1);

	/* 256 loops should be maximum iterations for 4 lanes and 4 values.
	 * So, This loop should exit before 512 iterations
	 */
	for (max_tries = 0; max_tries < 512; max_tries++) {
		ret = xilinx_drm_dp_update_vs_emph(dp);
		if (ret)
			return ret;

		drm_dp_link_train_clock_recovery_delay(dp->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		cr_done = drm_dp_clock_recovery_ok(link_status, lane_cnt);
		if (cr_done)
			break;

		for (i = 0; i < lane_cnt; i++)
			if (!(dp->train_set[i] & DP_TRAIN_MAX_SWING_REACHED))
				break;

		if (i == lane_cnt)
			break;

		if ((dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) == vs)
			tries++;
		else
			tries = 0;

		if (tries == DP_MAX_TRAINING_TRIES)
			break;

		vs = dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		xilinx_drm_dp_adjust_train(dp, link_status);
	}

	if (!cr_done)
		return -EIO;

	return 0;
}

/**
 * xilinx_drm_dp_link_train_ce - Train channel equalization
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if channel equalization train is done successfully, or
 * corresponding error code.
 */
static int xilinx_drm_dp_link_train_ce(struct xilinx_drm_dp *dp)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	u32 pat, tries;
	int ret;
	bool ce_done;

	if (dp->config.dp_version == DP_V1_2 &&
	    dp->dpcd[DP_DPCD_REV] >= DP_V1_2 &&
	    dp->dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED)
		pat = DP_TRAINING_PATTERN_3;
	else
		pat = DP_TRAINING_PATTERN_2;

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 pat | DP_LINK_SCRAMBLING_DISABLE);
	if (ret < 0)
		return ret;

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_TRAINING_PATTERN_SET, pat);

	for (tries = 0; tries < DP_MAX_TRAINING_TRIES; tries++) {
		ret = xilinx_drm_dp_update_vs_emph(dp);
		if (ret)
			return ret;

		drm_dp_link_train_channel_eq_delay(dp->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		ce_done = drm_dp_channel_eq_ok(link_status, lane_cnt);
		if (ce_done)
			break;

		xilinx_drm_dp_adjust_train(dp, link_status);
	}

	if (!ce_done)
		return -EIO;

	return 0;
}

/**
 * xilinx_drm_dp_link_train - Train the link
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if all trains are done successfully, or corresponding error code.
 */
static int xilinx_drm_dp_train(struct xilinx_drm_dp *dp)
{
	u32 reg;
	u8 bw_code = dp->mode.bw_code;
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 aux_lane_cnt = lane_cnt;
	bool enhanced;
	int ret;

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_LANE_CNT_SET, lane_cnt);

	enhanced = drm_dp_enhanced_frame_cap(dp->dpcd);
	if (enhanced) {
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_ENHANCED_FRAME_EN, 1);
		aux_lane_cnt |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	}

	if (dp->dpcd[3] & 0x1) {
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_DOWNSPREAD_CTL, 1);
		drm_dp_dpcd_writeb(&dp->aux, DP_DOWNSPREAD_CTRL,
				   DP_SPREAD_AMP_0_5);
	} else {
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_DOWNSPREAD_CTL, 0);
		drm_dp_dpcd_writeb(&dp->aux, DP_DOWNSPREAD_CTRL, 0);
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LANE_COUNT_SET, aux_lane_cnt);
	if (ret < 0) {
		DRM_ERROR("failed to set lane count\n");
		return ret;
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				 DP_SET_ANSI_8B10B);
	if (ret < 0) {
		DRM_ERROR("failed to set ANSI 8B/10B encoding\n");
		return ret;
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LINK_BW_SET, bw_code);
	if (ret < 0) {
		DRM_ERROR("failed to set DP bandwidth\n");
		return ret;
	}

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_LINK_BW_SET, bw_code);

	switch (bw_code) {
	case DP_LINK_BW_1_62:
		reg = XILINX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_162;
		break;
	case DP_LINK_BW_2_7:
		reg = XILINX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_270;
		break;
	case DP_LINK_BW_5_4:
	default:
		reg = XILINX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING_540;
		break;
	}

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_PHY_CLOCK_FEEDBACK_SETTING,
			  reg);
	ret = xilinx_drm_dp_phy_ready(dp);
	if (ret < 0)
		return ret;

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_SCRAMBLING_DISABLE, 1);

	memset(dp->train_set, 0, 4);

	ret = xilinx_drm_dp_link_train_cr(dp);
	if (ret)
		return ret;

	ret = xilinx_drm_dp_link_train_ce(dp);
	if (ret)
		return ret;

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_TRAINING_PATTERN_SET,
			  DP_TRAINING_PATTERN_DISABLE);
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 DP_TRAINING_PATTERN_DISABLE);
	if (ret < 0) {
		DRM_ERROR("failed to disable training pattern\n");
		return ret;
	}

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_SCRAMBLING_DISABLE, 0);

	return 0;
}

/**
 * xilinx_drm_dp_train_loop - Downshift the link rate during training
 * @dp: DisplayPort IP core structure
 *
 * Train the link by downshifting the link rate if training is not successful.
 */
static void xilinx_drm_dp_train_loop(struct xilinx_drm_dp *dp)
{
	struct xilinx_drm_dp_mode *mode = &dp->mode;
	u8 bw = mode->bw_code;
	int ret;

	do {
		ret = xilinx_drm_dp_train(dp);
		if (!ret)
			return;

		ret = xilinx_drm_dp_mode_configure(dp, mode->pclock, bw);
		if (ret < 0)
			return;
		bw = ret;
	} while (bw >= DP_LINK_BW_1_62);

	DRM_ERROR("failed to train the DP link\n");
}

/**
 * xilinx_drm_dp_init_aux - Initialize the DP aux
 * @dp: DisplayPort IP core structure
 *
 * Initialize the DP aux. The aux clock is derived from the axi clock, so
 * this function gets the axi clock frequency and calculates the filter
 * value. Additionally, the interrupts and transmitter are enabled.
 *
 * Return: 0 on success, error value otherwise
 */
static int xilinx_drm_dp_init_aux(struct xilinx_drm_dp *dp)
{
	int clock_rate;
	u32 reg, w;

	clock_rate = clk_get_rate(dp->aclk);
	if (clock_rate < XILINX_DP_TX_CLK_DIVIDER_MHZ) {
		DRM_ERROR("aclk should be higher than 1MHz\n");
		return -EINVAL;
	}

	/* Allowable values for this register are: 8, 16, 24, 32, 40, 48 */
	for (w = 8; w <= 48; w += 8) {
		/* AUX pulse width should be between 0.4 to 0.6 usec */
		if (w >= (4 * clock_rate / 10000000) &&
		    w <= (6 * clock_rate / 10000000))
			break;
	}

	if (w > 48) {
		DRM_ERROR("aclk frequency too high\n");
		return -EINVAL;
	}
	reg = w << XILINX_DP_TX_CLK_DIVIDER_AUX_FILTER_SHIFT;

	reg |= clock_rate / XILINX_DP_TX_CLK_DIVIDER_MHZ;
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_CLK_DIVIDER, reg);

	if (dp->dp_sub)
		xilinx_drm_writel(dp->iomem, XILINX_DP_SUB_TX_INTR_EN,
				  XILINX_DP_TX_INTR_ALL);
	else
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_INTR_MASK,
				  ~XILINX_DP_TX_INTR_ALL);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_ENABLE, 1);

	return 0;
}

/**
 * xilinx_drm_dp_init_phy - Initialize the phy
 * @dp: DisplayPort IP core structure
 *
 * Initialize the phy.
 *
 * Return: 0 if the phy instances are initialized correctly, or the error code
 * returned from the callee functions.
 */
static int xilinx_drm_dp_init_phy(struct xilinx_drm_dp *dp)
{
	unsigned int i;
	int ret;

	for (i = 0; i < dp->config.max_lanes; i++) {
		ret = phy_init(dp->phy[i]);
		if (ret) {
			dev_err(dp->dev, "failed to init phy lane %d\n", i);
			return ret;
		}
	}

	if (dp->dp_sub)
		xilinx_drm_writel(dp->iomem, XILINX_DP_SUB_TX_INTR_DS,
				  XILINX_DP_TX_INTR_ALL);
	else
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_INTR_MASK,
				  XILINX_DP_TX_INTR_ALL);

	xilinx_drm_clr(dp->iomem, XILINX_DP_TX_PHY_CONFIG,
		       XILINX_DP_TX_PHY_CONFIG_ALL_RESET);

	/* Wait for PLL to be locked for the primary (1st) */
	if (dp->phy[0]) {
		ret = xpsgtr_wait_pll_lock(dp->phy[0]);
		if (ret) {
			dev_err(dp->dev, "failed to lock pll\n");
			return ret;
		}
	}

	return 0;
}

/**
 * xilinx_drm_dp_exit_phy - Exit the phy
 * @dp: DisplayPort IP core structure
 *
 * Exit the phy.
 */
static void xilinx_drm_dp_exit_phy(struct xilinx_drm_dp *dp)
{
	unsigned int i;
	int ret;

	for (i = 0; i < dp->config.max_lanes; i++) {
		ret = phy_exit(dp->phy[i]);
		if (ret) {
			dev_err(dp->dev,
				"failed to exit phy (%d) %d\n", i, ret);
		}
	}
}

static void xilinx_drm_dp_dpms(struct drm_encoder *encoder, int dpms)
{
	struct xilinx_drm_dp *dp = to_dp(encoder);
	void __iomem *iomem = dp->iomem;
	unsigned int i;
	int ret;

	if (dp->dpms == dpms)
		return;

	dp->dpms = dpms;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		pm_runtime_get_sync(dp->dev);

		if (dp->aud_clk)
			xilinx_drm_writel(iomem, XILINX_DP_TX_AUDIO_CONTROL, 1);
		xilinx_drm_writel(iomem, XILINX_DP_TX_PHY_POWER_DOWN, 0);

		for (i = 0; i < 3; i++) {
			ret = drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER,
						 DP_SET_POWER_D0);
			if (ret == 1)
				break;
			usleep_range(300, 500);
		}

		if (ret != 1)
			dev_dbg(dp->dev, "DP aux failed\n");
		else
			xilinx_drm_dp_train_loop(dp);
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_SW_RESET,
				  XILINX_DP_TX_SW_RESET_ALL);
		xilinx_drm_writel(iomem, XILINX_DP_TX_ENABLE_MAIN_STREAM, 1);

		return;
	default:
		xilinx_drm_writel(iomem, XILINX_DP_TX_ENABLE_MAIN_STREAM, 0);
		drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, DP_SET_POWER_D3);
		xilinx_drm_writel(iomem, XILINX_DP_TX_PHY_POWER_DOWN,
				  XILINX_DP_TX_PHY_POWER_DOWN_ALL);
		if (dp->aud_clk)
			xilinx_drm_writel(iomem, XILINX_DP_TX_AUDIO_CONTROL, 0);

		pm_runtime_put_sync(dp->dev);

		return;
	}
}

static void xilinx_drm_dp_save(struct drm_encoder *encoder)
{
	/* no op */
}

static void xilinx_drm_dp_restore(struct drm_encoder *encoder)
{
	/* no op */
}

#define XILINX_DP_SUB_TX_MIN_H_BACKPORCH	20

static bool xilinx_drm_dp_mode_fixup(struct drm_encoder *encoder,
				     const struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct xilinx_drm_dp *dp = to_dp(encoder);
	int diff = mode->htotal - mode->hsync_end;

	/*
	 * ZynqMP DP requires horizontal backporch to be greater than 12.
	 * This limitation may conflict with the sink device.
	 */
	if (dp->dp_sub && diff < XILINX_DP_SUB_TX_MIN_H_BACKPORCH) {
		int vrefresh = (adjusted_mode->clock * 1000) /
			       (adjusted_mode->vtotal * adjusted_mode->htotal);

		diff = XILINX_DP_SUB_TX_MIN_H_BACKPORCH - diff;
		adjusted_mode->htotal += diff;
		adjusted_mode->clock = adjusted_mode->vtotal *
				       adjusted_mode->htotal * vrefresh / 1000;
	}

	return true;
}

static int xilinx_drm_dp_mode_valid(struct drm_encoder *encoder,
				    struct drm_display_mode *mode)
{
	struct xilinx_drm_dp *dp = to_dp(encoder);
	u8 max_lanes = dp->link_config.max_lanes;
	u8 bpp = dp->config.bpp;
	u32 max_pclock = dp->config.max_pclock;
	int max_rate = dp->link_config.max_rate;
	int rate;

	if (max_pclock && mode->clock > max_pclock)
		return MODE_CLOCK_HIGH;

	rate = xilinx_drm_dp_max_rate(max_rate, max_lanes, bpp);
	if (mode->clock > rate)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

/**
 * xilinx_drm_dp_mode_set_transfer_unit - Set the transfer unit values
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * Set the transfer unit, and caculate all transfer unit size related values.
 * Calculation is based on DP and IP core specification.
 */
static void xilinx_drm_dp_mode_set_transfer_unit(struct xilinx_drm_dp *dp,
						 struct drm_display_mode *mode)
{
	u32 tu = XILINX_DP_TX_DEF_TRANSFER_UNIT_SIZE;
	u32 bw, vid_kbytes, avg_bytes_per_tu, init_wait;

	/* Use the max transfer unit size (default) */
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_TRANSFER_UNIT_SIZE, tu);

	vid_kbytes = mode->clock * (dp->config.bpp / 8);
	bw = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	avg_bytes_per_tu = vid_kbytes * tu / (dp->mode.lane_cnt * bw / 1000);

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MIN_BYTES_PER_TU,
			  avg_bytes_per_tu / 1000);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_FRAC_BYTES_PER_TU,
			  avg_bytes_per_tu % 1000);

	/* Configure the initial wait cycle based on transfer unit size */
	if (tu < (avg_bytes_per_tu / 1000))
		init_wait = 0;
	else if ((avg_bytes_per_tu / 1000) <= 4)
		init_wait = tu;
	else
		init_wait = tu - avg_bytes_per_tu / 1000;

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_INIT_WAIT, init_wait);
}

/**
 * xilinx_drm_dp_mode_set_stream - Configure the main stream
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * Configure the main stream based on the requested mode @mode. Calculation is
 * based on IP core specification.
 */
static void xilinx_drm_dp_mode_set_stream(struct xilinx_drm_dp *dp,
					  struct drm_display_mode *mode)
{
	u8 lane_cnt = dp->mode.lane_cnt;
	u32 reg, wpl;

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_HTOTAL,
			  mode->htotal);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_VTOTAL,
			  mode->vtotal);

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_POLARITY,
			  (!!(mode->flags & DRM_MODE_FLAG_PVSYNC) <<
			   XILINX_DP_TX_MAIN_STREAM_POLARITY_VSYNC_SHIFT) |
			  (!!(mode->flags & DRM_MODE_FLAG_PHSYNC) <<
			   XILINX_DP_TX_MAIN_STREAM_POLARITY_HSYNC_SHIFT));

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_HSWIDTH,
			  mode->hsync_end - mode->hsync_start);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_VSWIDTH,
			  mode->vsync_end - mode->vsync_start);

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_HRES,
			  mode->hdisplay);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_VRES,
			  mode->vdisplay);

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_HSTART,
			  mode->htotal - mode->hsync_start);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_VSTART,
			  mode->vtotal - mode->vsync_start);

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_MISC0,
			  dp->config.misc0);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_MAIN_STREAM_MISC1,
			  dp->config.misc1);

	/* In synchronous mode, set the diviers */
	if (dp->config.misc0 & XILINX_DP_TX_MAIN_STREAM_MISC0_SYNC) {
		reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_N_VID, reg);
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_M_VID, mode->clock);
		if (dp->aud_clk) {
			int aud_rate = clk_get_rate(dp->aud_clk);

			dev_dbg(dp->dev, "Audio rate: %d\n", aud_rate / 512);

			xilinx_drm_writel(dp->iomem, XILINX_DP_TX_AUDIO_N_AUD,
					  reg);
			xilinx_drm_writel(dp->iomem, XILINX_DP_TX_AUDIO_M_AUD,
					  aud_rate / 1000);
		}
	}

	/* Only 2 channel is supported now */
	if (dp->aud_clk)
		xilinx_drm_writel(dp->iomem, XILINX_DP_TX_AUDIO_CHANNELS, 1);

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_USER_PIXEL_WIDTH, 1);

	/* Translate to the native 16 bit datapath based on IP core spec */
	wpl = (mode->hdisplay * dp->config.bpp + 15) / 16;
	reg = wpl + wpl % lane_cnt - lane_cnt;
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_USER_DATA_CNT_PER_LANE, reg);
}


static void xilinx_drm_dp_mode_set(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct xilinx_drm_dp *dp = to_dp(encoder);
	int ret;

	ret = xilinx_drm_dp_mode_configure(dp, adjusted_mode->clock, 0);
	if (ret < 0)
		return;

	xilinx_drm_dp_mode_set_stream(dp, adjusted_mode);
	xilinx_drm_dp_mode_set_transfer_unit(dp, adjusted_mode);
}

static enum drm_connector_status
xilinx_drm_dp_detect(struct drm_encoder *encoder,
		     struct drm_connector *connector)
{
	struct xilinx_drm_dp *dp = to_dp(encoder);
	struct xilinx_drm_dp_link_config *link_config = &dp->link_config;
	u32 state;
	int ret;

	state = xilinx_drm_readl(dp->iomem, XILINX_DP_TX_INTR_SIGNAL_STATE);
	if (state & XILINX_DP_TX_INTR_SIGNAL_STATE_HPD) {
		ret = drm_dp_dpcd_read(&dp->aux, 0x0, dp->dpcd,
				       sizeof(dp->dpcd));
		if (ret < 0)
			return connector_status_disconnected;

		link_config->max_rate = min_t(int,
					      drm_dp_max_link_rate(dp->dpcd),
					      dp->config.max_link_rate);
		link_config->max_lanes = min_t(u8,
					       drm_dp_max_lane_count(dp->dpcd),
					       dp->config.max_lanes);

		return connector_status_connected;
	}

	return connector_status_disconnected;
}

static int xilinx_drm_dp_get_modes(struct drm_encoder *encoder,
				   struct drm_connector *connector)
{
	struct xilinx_drm_dp *dp = to_dp(encoder);
	struct edid *edid;
	int ret;

	edid = drm_get_edid(connector, &dp->aux.ddc);
	if (!edid)
		return 0;

	drm_mode_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);

	kfree(edid);

	return ret;
}

static struct drm_encoder_slave_funcs xilinx_drm_dp_encoder_funcs = {
	.dpms			= xilinx_drm_dp_dpms,
	.save			= xilinx_drm_dp_save,
	.restore		= xilinx_drm_dp_restore,
	.mode_fixup		= xilinx_drm_dp_mode_fixup,
	.mode_valid		= xilinx_drm_dp_mode_valid,
	.mode_set		= xilinx_drm_dp_mode_set,
	.detect			= xilinx_drm_dp_detect,
	.get_modes		= xilinx_drm_dp_get_modes,
};

static int xilinx_drm_dp_encoder_init(struct platform_device *pdev,
				      struct drm_device *dev,
				      struct drm_encoder_slave *encoder)
{
	struct xilinx_drm_dp *dp = platform_get_drvdata(pdev);

	encoder->slave_priv = dp;
	encoder->slave_funcs = &xilinx_drm_dp_encoder_funcs;

	dp->encoder = &encoder->base;

	return xilinx_drm_dp_init_aux(dp);
}

static irqreturn_t xilinx_drm_dp_irq_handler(int irq, void *data)
{
	struct xilinx_drm_dp *dp = (struct xilinx_drm_dp *)data;
	u32 reg, status;

	reg = dp->dp_sub ?
	      XILINX_DP_SUB_TX_INTR_STATUS : XILINX_DP_TX_INTR_STATUS;
	status = xilinx_drm_readl(dp->iomem, reg);
	if (!status)
		return IRQ_NONE;

	if (status & XILINX_DP_TX_INTR_CHBUF_UNDERFLW_MASK)
		dev_dbg(dp->dev, "underflow interrupt\n");
	if (status & XILINX_DP_TX_INTR_CHBUF_OVERFLW_MASK)
		dev_dbg(dp->dev, "overflow interrupt\n");

	xilinx_drm_writel(dp->iomem, reg, status);

	if (status & XILINX_DP_TX_INTR_VBLANK_START)
		xilinx_drm_dp_sub_handle_vblank(dp->dp_sub);

	if (status & XILINX_DP_TX_INTR_HPD_EVENT)
		drm_helper_hpd_irq_event(dp->encoder->dev);

	if (status & XILINX_DP_TX_INTR_HPD_IRQ) {
		u8 status[DP_LINK_STATUS_SIZE + 2];

		drm_dp_dpcd_read(&dp->aux, DP_SINK_COUNT, status,
				 DP_LINK_STATUS_SIZE + 2);

		if (status[4] & DP_LINK_STATUS_UPDATED ||
		    !drm_dp_clock_recovery_ok(&status[2], dp->mode.lane_cnt) ||
		    !drm_dp_channel_eq_ok(&status[2], dp->mode.lane_cnt))
			xilinx_drm_dp_train_loop(dp);
	}

	return IRQ_HANDLED;
}

static ssize_t
xilinx_drm_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct xilinx_drm_dp *dp = container_of(aux, struct xilinx_drm_dp, aux);
	int ret;
	unsigned int i, iter;

	/* Number of loops = timeout in msec / aux delay (400 usec) */
	iter = xilinx_drm_dp_aux_timeout_ms * 1000 / 400;
	iter = iter ? iter : 1;

	for (i = 0; i < iter; i++) {
		ret = xilinx_drm_dp_aux_cmd_submit(dp, msg->request,
						   msg->address, msg->buffer,
						   msg->size, &msg->reply);
		if (!ret) {
			dev_dbg(dp->dev, "aux %d retries\n", i);
			return msg->size;
		}

		usleep_range(400, 500);
	}

	dev_dbg(dp->dev, "failed to do aux transfer (%d)\n", ret);

	return ret;
}

static int xilinx_drm_dp_parse_of(struct xilinx_drm_dp *dp)
{
	struct device_node *node = dp->dev->of_node;
	struct xilinx_drm_dp_config *config = &dp->config;
	const char *string;
	u32 num_colors, bpc;
	bool sync;
	int ret;

	ret = of_property_read_string(node, "xlnx,dp-version", &string);
	if (ret < 0) {
		dev_err(dp->dev, "No DP version in DT\n");
		return ret;
	}

	if (strcmp(string, "v1.1a") == 0) {
		config->dp_version = DP_V1_1A;
	} else if (strcmp(string, "v1.2") == 0) {
		config->dp_version = DP_V1_2;
	} else {
		dev_err(dp->dev, "Invalid DP version in DT\n");
		return -EINVAL;
	}

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

	if (config->max_link_rate != DP_REDUCED_BIT_RATE &&
	    config->max_link_rate != DP_HIGH_BIT_RATE &&
	    config->max_link_rate != DP_HIGH_BIT_RATE2) {
		dev_err(dp->dev, "Invalid link rate in DT\n");
		return -EINVAL;
	}

	config->enable_yonly = of_property_read_bool(node, "xlnx,enable-yonly");
	config->enable_ycrcb = of_property_read_bool(node, "xlnx,enable-ycrcb");

	sync = of_property_read_bool(node, "xlnx,sync");
	if (sync)
		config->misc0 |= XILINX_DP_TX_MAIN_STREAM_MISC0_SYNC;

	ret = of_property_read_string(node, "xlnx,colormetry", &string);
	if (ret < 0) {
		dev_err(dp->dev, "No colormetry in DT\n");
		return ret;
	}

	if (strcmp(string, "rgb") == 0) {
		config->misc0 |= XILINX_DP_MISC0_RGB;
		num_colors = 3;
	} else if (config->enable_ycrcb && strcmp(string, "ycrcb422") == 0) {
		config->misc0 |= XILINX_DP_MISC0_YCRCB_422;
		num_colors = 2;
	} else if (config->enable_ycrcb && strcmp(string, "ycrcb444") == 0) {
		config->misc0 |= XILINX_DP_MISC0_YCRCB_444;
		num_colors = 3;
	} else if (config->enable_yonly && strcmp(string, "yonly") == 0) {
		config->misc1 |= XILINX_DP_MISC1_Y_ONLY;
		num_colors = 1;
	} else {
		dev_err(dp->dev, "Invalid colormetry in DT\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,max-bpc", &config->max_bpc);
	if (ret < 0) {
		dev_err(dp->dev, "No max bpc in DT\n");
		return ret;
	}

	if (config->max_bpc != 8 && config->max_bpc != 10 &&
	    config->max_bpc != 12 && config->max_bpc != 16) {
		dev_err(dp->dev, "Invalid max bpc in DT\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,bpc", &bpc);
	if (ret < 0) {
		dev_err(dp->dev, "No color depth(bpc) in DT\n");
		return ret;
	}

	if (bpc > config->max_bpc) {
		dev_err(dp->dev, "Invalid color depth(bpc) in DT\n");
		return -EINVAL;
	}

	switch (bpc) {
	case 6:
		config->misc0 |= XILINX_DP_MISC0_BPC_6;
		break;
	case 8:
		config->misc0 |= XILINX_DP_MISC0_BPC_8;
		break;
	case 10:
		config->misc0 |= XILINX_DP_MISC0_BPC_10;
		break;
	case 12:
		config->misc0 |= XILINX_DP_MISC0_BPC_12;
		break;
	case 16:
		config->misc0 |= XILINX_DP_MISC0_BPC_16;
		break;
	default:
		dev_err(dp->dev, "Not supported color depth in DT\n");
		return -EINVAL;
	}

	config->bpp = num_colors * bpc;

	of_property_read_u32(node, "xlnx,max-pclock-frequency",
			     &config->max_pclock);

	return 0;
}

static int __maybe_unused xilinx_drm_dp_pm_suspend(struct device *dev)
{
	struct xilinx_drm_dp *dp = dev_get_drvdata(dev);

	xilinx_drm_dp_exit_phy(dp);

	return 0;
}

static int __maybe_unused xilinx_drm_dp_pm_resume(struct device *dev)
{
	struct xilinx_drm_dp *dp = dev_get_drvdata(dev);

	xilinx_drm_dp_init_phy(dp);
	xilinx_drm_dp_init_aux(dp);
	drm_helper_hpd_irq_event(dp->encoder->dev);

	return 0;
}

static const struct dev_pm_ops xilinx_drm_dp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xilinx_drm_dp_pm_suspend,
				xilinx_drm_dp_pm_resume)
};

static int xilinx_drm_dp_probe(struct platform_device *pdev)
{
	struct xilinx_drm_dp *dp;
	struct resource *res;
	u32 version, i;
	int irq, ret;

	dp = devm_kzalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->dpms = DRM_MODE_DPMS_OFF;
	dp->dev = &pdev->dev;

	ret = xilinx_drm_dp_parse_of(dp);
	if (ret < 0)
		return ret;

	dp->aclk = devm_clk_get(dp->dev, "aclk");
	if (IS_ERR(dp->aclk))
		return PTR_ERR(dp->aclk);

	ret = clk_prepare_enable(dp->aclk);
	if (ret) {
		dev_err(dp->dev, "failed to enable the aclk\n");
		return ret;
	}

	dp->aud_clk = devm_clk_get(dp->dev, "aud_clk");
	if (IS_ERR(dp->aud_clk)) {
		ret = PTR_ERR(dp->aud_clk);
		if (ret == -EPROBE_DEFER)
			goto error_aclk;
		dp->aud_clk = NULL;
		dev_dbg(dp->dev, "failed to get the aud_clk:\n");
	} else {
		ret = clk_prepare_enable(dp->aud_clk);
		if (ret) {
			dev_err(dp->dev, "failed to enable aud_clk\n");
			goto error_aclk;
		}
	}

	dp->dp_sub = xilinx_drm_dp_sub_of_get(pdev->dev.of_node);
	if (IS_ERR(dp->dp_sub)) {
		ret = PTR_ERR(dp->dp_sub);
		goto error_aud_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dp->iomem = devm_ioremap_resource(dp->dev, res);
	if (IS_ERR(dp->iomem)) {
		ret = PTR_ERR(dp->iomem);
		goto error_dp_sub;
	}

	platform_set_drvdata(pdev, dp);

	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_PHY_POWER_DOWN,
			  XILINX_DP_TX_PHY_POWER_DOWN_ALL);
	xilinx_drm_set(dp->iomem, XILINX_DP_TX_PHY_CONFIG,
		       XILINX_DP_TX_PHY_CONFIG_ALL_RESET);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_FORCE_SCRAMBLER_RESET, 1);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_ENABLE, 0);

	if (dp->dp_sub) {
		for (i = 0; i < dp->config.max_lanes; i++) {
			char phy_name[16];

			snprintf(phy_name, sizeof(phy_name), "dp-phy%d", i);
			dp->phy[i] = devm_phy_get(dp->dev, phy_name);
			if (IS_ERR(dp->phy[i])) {
				dev_err(dp->dev, "failed to get phy lane\n");
				ret = PTR_ERR(dp->phy[i]);
				dp->phy[i] = NULL;
				goto error_dp_sub;
			}
		}
	}

	ret = xilinx_drm_dp_init_phy(dp);
	if (ret)
		goto error_phy;

	dp->aux.name = "Xilinx DP AUX";
	dp->aux.dev = dp->dev;
	dp->aux.transfer = xilinx_drm_dp_aux_transfer;
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
					xilinx_drm_dp_irq_handler, IRQF_ONESHOT,
					dev_name(dp->dev), dp);
	if (ret < 0)
		goto error;

	version = xilinx_drm_readl(dp->iomem, XILINX_DP_TX_VERSION);

	dev_info(dp->dev, "device found, version %u.%02x%x\n",
		 ((version & XILINX_DP_TX_VERSION_MAJOR_MASK) >>
		  XILINX_DP_TX_VERSION_MAJOR_SHIFT),
		 ((version & XILINX_DP_TX_VERSION_MINOR_MASK) >>
		  XILINX_DP_TX_VERSION_MINOR_SHIFT),
		 ((version & XILINX_DP_TX_VERSION_REVISION_MASK) >>
		  XILINX_DP_TX_VERSION_REVISION_SHIFT));

	version = xilinx_drm_readl(dp->iomem, XILINX_DP_TX_CORE_ID);
	if (version & XILINX_DP_TX_CORE_ID_DIRECTION) {
		dev_err(dp->dev, "Receiver is not supported\n");
		ret = -ENODEV;
		goto error;
	}

	dev_info(dp->dev, "Display Port, version %u.%02x%02x (tx)\n",
		 ((version & XILINX_DP_TX_CORE_ID_MAJOR_MASK) >>
		  XILINX_DP_TX_CORE_ID_MAJOR_SHIFT),
		 ((version & XILINX_DP_TX_CORE_ID_MINOR_MASK) >>
		  XILINX_DP_TX_CORE_ID_MINOR_SHIFT),
		 ((version & XILINX_DP_TX_CORE_ID_REVISION_MASK) >>
		  XILINX_DP_TX_CORE_ID_REVISION_SHIFT));

	pm_runtime_enable(dp->dev);

	return 0;

error:
	drm_dp_aux_unregister(&dp->aux);
error_dp_sub:
	xilinx_drm_dp_sub_put(dp->dp_sub);
error_phy:
	xilinx_drm_dp_exit_phy(dp);
error_aud_clk:
	if (dp->aud_clk)
		clk_disable_unprepare(dp->aud_clk);
error_aclk:
	clk_disable_unprepare(dp->aclk);
	return ret;
}

static int xilinx_drm_dp_remove(struct platform_device *pdev)
{
	struct xilinx_drm_dp *dp = platform_get_drvdata(pdev);

	pm_runtime_disable(dp->dev);
	xilinx_drm_writel(dp->iomem, XILINX_DP_TX_ENABLE, 0);

	drm_dp_aux_unregister(&dp->aux);
	xilinx_drm_dp_exit_phy(dp);
	xilinx_drm_dp_sub_put(dp->dp_sub);

	if (dp->aud_clk)
		clk_disable_unprepare(dp->aud_clk);
	clk_disable_unprepare(dp->aclk);

	return 0;
}

static const struct of_device_id xilinx_drm_dp_of_match[] = {
	{ .compatible = "xlnx,v-dp", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_drm_dp_of_match);

static struct drm_platform_encoder_driver xilinx_drm_dp_driver = {
	.platform_driver = {
		.probe			= xilinx_drm_dp_probe,
		.remove			= xilinx_drm_dp_remove,
		.driver			= {
			.owner		= THIS_MODULE,
			.name		= "xilinx-drm-dp",
			.of_match_table	= xilinx_drm_dp_of_match,
			.pm		= &xilinx_drm_dp_pm_ops,
		},
	},

	.encoder_init = xilinx_drm_dp_encoder_init,
};

static int __init xilinx_drm_dp_init(void)
{
	return platform_driver_register(&xilinx_drm_dp_driver.platform_driver);
}

static void __exit xilinx_drm_dp_exit(void)
{
	platform_driver_unregister(&xilinx_drm_dp_driver.platform_driver);
}

module_init(xilinx_drm_dp_init);
module_exit(xilinx_drm_dp_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx DRM KMS DiplayPort Driver");
MODULE_LICENSE("GPL v2");

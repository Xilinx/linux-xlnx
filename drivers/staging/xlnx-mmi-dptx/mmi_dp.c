// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated DisplayPort Tx driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fixed.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "mmi_dp.h"
#include "mmi_dp_reg.h"

#define MMI_DPTX_MAX_AUX_MSG_LEN	(16)

/**
 * mmi_dp_tx_first_bit_set - Find first (least significant) bit set
 * @data: word to search
 * Return: bit position or 32 if none is set
 */
static u32 mmi_dp_tx_first_bit_set(u32 data)
{
	u32 n = 0;

	if (data != 0)
		n = __ffs(data);

	return n;
}

/**
 * mmi_dp_set_field - Set bit field
 * @data: raw data
 * @mask: bit field mask
 * @value: new value
 * Return: new raw data
 */
u32 mmi_dp_set_field(u32 data, u32 mask, u32 value)
{
	return (((value << mmi_dp_tx_first_bit_set(mask)) & mask) | (data & ~mask));
}

/**
 * mmi_dp_set8_field - Set bit field
 * @data: raw data
 * @mask: bit field mask
 * @value: new value
 * Return: new raw data
 */
u8 mmi_dp_set8_field(u8 data, u8 mask, u8 value)
{
	return (((value << mmi_dp_tx_first_bit_set(mask)) & mask) | (data & ~mask));
}

void mmi_dp_write_mask(struct dptx *dptx, u32 addr, u32 mask, u32 data)
{
	u32 temp;

	temp = mmi_dp_set_field(mmi_dp_read(dptx->base, addr), mask, data);
	mmi_dp_write(dptx->base, addr, temp);
}

/* AUX channel */

/* Reset the AUX payload registers. */
static void mmi_dp_aux_clear_data(struct dptx *dptx)
{
	mmi_dp_write(dptx->base, AUX_DATA0, 0);
	mmi_dp_write(dptx->base, AUX_DATA1, 0);
	mmi_dp_write(dptx->base, AUX_DATA2, 0);
	mmi_dp_write(dptx->base, AUX_DATA3, 0);
}

/* Pack up to 16 payload bytes into the AUX registers ahead of a write. */
static void mmi_dp_aux_write_data(struct dptx *dptx, const u8 *bytes,
				  unsigned int len)
{
	u32 data[4] = { 0 };
	unsigned int i;

	for (i = 0; i < len; i++)
		data[i / 4] |= bytes[i] << ((i % 4) * 8);

	mmi_dp_write(dptx->base, AUX_DATA0, data[0]);
	mmi_dp_write(dptx->base, AUX_DATA1, data[1]);
	mmi_dp_write(dptx->base, AUX_DATA2, data[2]);
	mmi_dp_write(dptx->base, AUX_DATA3, data[3]);
}

/* Unpack up to 16 payload bytes from the AUX registers after a read. */
static void mmi_dp_aux_read_data(struct dptx *dptx, u8 *bytes, unsigned int len)
{
	u32 data[4];

	data[0] = mmi_dp_read(dptx->base, AUX_DATA0);
	data[1] = mmi_dp_read(dptx->base, AUX_DATA1);
	data[2] = mmi_dp_read(dptx->base, AUX_DATA2);
	data[3] = mmi_dp_read(dptx->base, AUX_DATA3);

	memcpy(bytes, data, len);
}

/*
 * mmi_dp_aux_wait_reply() - Wait for the controller to finish a transaction.
 * @dptx: The dptx struct
 * @status: Returns the raw AUX reply status field
 * @bytes: Returns the number of payload bytes in the reply
 *
 * Return: 0 once the sink replied, -ETIMEDOUT if it never did.
 */
static int mmi_dp_aux_wait_reply(struct dptx *dptx, u32 *status,
				 unsigned int *bytes)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(50);
	u32 auxsts;

	while (1) {
		auxsts = mmi_dp_read(dptx->base, AUX_STATUS);

		if (auxsts & AUX_TIMEOUT_MASK)
			return -ETIMEDOUT;

		if (!(auxsts & AUX_REPLY_MASK))
			break;

		if (time_after_eq(jiffies, deadline))
			return -ETIMEDOUT;

		fsleep(1);
	}

	*status = (auxsts & AUX_STATUS_MASK) >> DPTX_AUX_STS_STATUS_SHIFT;
	*bytes = FIELD_GET(AUX_BYTES_READ, auxsts);
	/* The controller includes the AUX reply status byte in this count. */
	if (*bytes)
		(*bytes)--;

	return 0;
}

/*
 * mmi_dp_aux_xfer_one() - Issue a single (<= 16 byte) AUX transaction.
 * @dptx: The dptx struct
 * @request: DRM AUX request type
 * @addr: DPCD address (native) or I2C address
 * @buf: Payload buffer (NULL for an address-only transaction)
 * @len: Payload length, 0 for an address-only transaction
 * @reply: Returns the DP AUX reply code (ACK/NACK/DEFER)
 *
 * Return: number of payload bytes transferred, or a negative errno.
 */
static int mmi_dp_aux_xfer_one(struct dptx *dptx, u8 request, u32 addr,
			       u8 *buf, size_t len,
			       u8 *reply)
{
	bool addr_only = len == 0;
	bool native = request & DP_AUX_NATIVE_WRITE;
	bool read = request & DP_AUX_I2C_READ;
	unsigned int br = 0;
	u32 auxcmd, type, status;
	int ret;

	switch (request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
		type = DPTX_AUX_CMD_TYPE_WRITE | DPTX_AUX_CMD_TYPE_NATIVE;
		break;
	case DP_AUX_NATIVE_READ:
		type = DPTX_AUX_CMD_TYPE_READ | DPTX_AUX_CMD_TYPE_NATIVE;
		break;
	case DP_AUX_I2C_WRITE:
		type = DPTX_AUX_CMD_TYPE_WRITE;
		break;
	case DP_AUX_I2C_READ:
		type = DPTX_AUX_CMD_TYPE_READ;
		break;
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		type = DPTX_AUX_CMD_TYPE_WSU;
		break;
	default:
		return -EINVAL;
	}

	if (request & DP_AUX_I2C_MOT) {
		if (native)
			return -EINVAL;
		type |= DPTX_AUX_CMD_TYPE_MOT;
	}

	/* Space consecutive transactions out for the controller. */
	mdelay(1);

	mmi_dp_aux_clear_data(dptx);
	if (!read && !addr_only)
		mmi_dp_aux_write_data(dptx, buf, len);

	auxcmd = type << DPTX_AUX_CMD_TYPE_SHIFT |
		 addr << DPTX_AUX_CMD_ADDR_SHIFT;
	if (addr_only)
		auxcmd |= DPTX_AUX_CMD_I2C_ADDR_ONLY;
	else
		auxcmd |= (len - 1) << DPTX_AUX_CMD_REQ_LEN_SHIFT;

	mmi_dp_write(dptx->base, AUX_CMD, auxcmd);

	ret = mmi_dp_aux_wait_reply(dptx, &status, &br);
	if (ret < 0) {
		/* Hardware timeout - let the DRM core retry. */
		dev_dbg_ratelimited(dptx->dev, "AUX timed out\n");
		return ret;
	}

	switch (status) {
	case DPTX_AUX_STS_STATUS_ACK:
		*reply = native ? DP_AUX_NATIVE_REPLY_ACK : DP_AUX_I2C_REPLY_ACK;
		break;
	case DPTX_AUX_STS_STATUS_NACK:
		*reply = DP_AUX_NATIVE_REPLY_NACK;
		return 0;
	case DPTX_AUX_STS_STATUS_DEFER:
		*reply = DP_AUX_NATIVE_REPLY_DEFER;
		return 0;
	case DPTX_AUX_STS_STATUS_I2C_NACK:
		*reply = DP_AUX_I2C_REPLY_NACK;
		return 0;
	case DPTX_AUX_STS_STATUS_I2C_DEFER:
		*reply = DP_AUX_I2C_REPLY_DEFER;
		return 0;
	default:
		dev_err_ratelimited(dptx->dev, "invalid AUX status 0x%x\n",
				    status);
		mmi_dp_soft_reset(dptx, DPTX_SRST_CTRL_AUX);
		return -EBUSY;
	}

	/* Transaction was ACKed. */
	if (addr_only)
		return 0;

	if (read) {
		if (!br) {
			/* Empty reply - reset and let the core retry. */
			dev_err_ratelimited(dptx->dev, "AUX empty reply\n");
			mmi_dp_soft_reset(dptx, DPTX_SRST_CTRL_AUX);
			return -EBUSY;
		}

		br = min_t(unsigned int, br, len);
		mmi_dp_aux_read_data(dptx, buf, br);

		return br;
	}

	return len;
}

/*
 * mmi_dp_aux_transfer() - DP AUX transfer back-end for the DRM core.
 *
 * DRM-compliant AUX implementation: reports the sink reply through
 * @msg->reply and returns the number of payload bytes transferred (or a
 * negative errno), leaving the DRM core to drive the DEFER/NACK retry logic.
 *
 * A single AUX transaction carries at most 16 bytes, so larger native DPCD
 * accesses are split into chunks here (I2C-over-AUX messages already arrive
 * pre-chunked from the DRM I2C helper).
 */
static ssize_t mmi_dp_aux_transfer(struct drm_dp_aux *aux,
				   struct drm_dp_aux_msg *msg)
{
	struct dptx *dptx = container_of(aux, struct dptx, dp_aux);
	bool native = msg->request & DP_AUX_NATIVE_WRITE;
	size_t done = 0;
	int ret;

	/*
	 * Bail out if a hotplug/unplug event asked us to abort and we are not
	 * currently servicing one - the sink is going away.
	 */
	if (atomic_read(&dptx->aux.abort) && !atomic_read(&dptx->aux.serving))
		return -ETIMEDOUT;

	/* Address-only I2C transaction (start/stop). */
	if (msg->size == 0) {
		ret = mmi_dp_aux_xfer_one(dptx, msg->request,
					  msg->address, NULL, 0, &msg->reply);
		return ret < 0 ? ret : 0;
	}

	while (done < msg->size) {
		size_t chunk = min_t(size_t, msg->size - done,
				     MMI_DPTX_MAX_AUX_MSG_LEN);
		/* Native accesses address consecutive DPCD registers. */
		u32 addr = native ? msg->address + done : msg->address;

		ret = mmi_dp_aux_xfer_one(dptx, msg->request, addr,
					  msg->buffer + done, chunk,
					  &msg->reply);
		if (ret < 0)
			return ret;

		/*
		 * The sink did not ACK this chunk. Report what we have; the
		 * DRM core inspects msg->reply and retries as needed.
		 */
		if (msg->reply != DP_AUX_NATIVE_REPLY_ACK &&
		    msg->reply != DP_AUX_I2C_REPLY_ACK)
			return done;

		done += ret;

		/* Short reply - stop and let the caller cope. */
		if (ret < chunk)
			break;
	}

	return done;
}

/* Core Related api's */
/*
 * Core Access Layer
 *
 * Provides low-level register access to the DPTX core.
 */

/**
 * mmi_dp_intr_en() - Enables interrupts
 * @dptx: The dptx struct
 * @bits: The interrupts to enable
 *
 * This function enables (unmasks) all interrupts in the INTERRUPT
 * register specified by @bits.
 */
static void mmi_dp_intr_en(struct dptx *dptx, u32 bits)
{
	u32 ien;

	ien = mmi_dp_read(dptx->base, GENERAL_INTERRUPT_ENABLE);
	ien |= bits;
	mmi_dp_write(dptx->base, GENERAL_INTERRUPT_ENABLE, ien);
}

/**
 * mmi_dp_intr_dis() - Disables interrupts
 * @dptx: The dptx struct
 * @bits: The interrupts to disable
 *
 * This function disables (masks) all interrupts in the INTERRUPT
 * register specified by @bits.
 */
static void mmi_dp_intr_dis(struct dptx *dptx, u32 bits)
{
	u32 ien;

	ien = mmi_dp_read(dptx->base, GENERAL_INTERRUPT_ENABLE);
	ien &= ~bits;
	mmi_dp_write(dptx->base, GENERAL_INTERRUPT_ENABLE, ien);
}

/**
 * mmi_dp_global_intr_en() - Enables top-level interrupts
 * @dptx: The dptx struct
 *
 * Enables (unmasks) all top-level interrupts.
 */

void mmi_dp_global_intr_en(struct dptx *dptx)
{
	mmi_dp_intr_en(dptx, DPTX_IEN_ALL_INTR &
		       ~(DPTX_ISTS_AUX_REPLY | DPTX_ISTS_AUX_CMD_INVALID));
}

/**
 * mmi_dp_global_intr_dis() - Disables top-level interrupts
 * @dptx: The dptx struct
 *
 * Disables (masks) all top-level interrupts.
 */
void mmi_dp_global_intr_dis(struct dptx *dptx)
{
	mmi_dp_intr_dis(dptx, DPTX_IEN_ALL_INTR);
}

/**
 * mmi_dp_video_intr_dis() - Disables video interrupts
 * @dptx: The dptx struct
 *
 * Disables (masks) all video interrupts.
 */
void mmi_dp_video_intr_dis(struct dptx *dptx)
{
	mmi_dp_intr_dis(dptx, DPTX_IEN_VIDEO_FIFO_OVERFLOW |
			DPTX_IEN_VIDEO_FIFO_UNDERFLOW);
}

/**
 * mmi_dp_enable_hpd_intr() - Enables HPD interrupts
 * @dptx: The dptx struct
 *
 * Enables (unmasks) HPD interrupts.
 */
static void mmi_dp_enable_hpd_intr(struct dptx *dptx)
{
	mmi_dp_intr_en(dptx, DPTX_ISTS_HPD);
}

void mmi_dp_clean_interrupts(struct dptx *dptx)
{
	u32 intr;

	/* Set reset bits */
	intr = mmi_dp_read(dptx->base, GENERAL_INTERRUPT);
	intr |= GEN_INTR_AUX_REPLY_EVENT;
	intr |= GEN_INTR_AUDIO_FIFO_OVERFLOW_STREAM0;
	intr |= GEN_INTR_VIDEO_FIFO_OVERFLOW_STREAM0;
	intr |= GEN_INTR_VIDEO_FIFO_UNDERFLOW_STREAM0;
	mmi_dp_write(dptx->base, GENERAL_INTERRUPT, intr);
}

/**
 * mmi_dp_soft_reset() - Performs a core soft reset
 * @dptx: The dptx struct
 * @bits: The components to reset
 *
 * Resets specified parts of the core by writing @bits into the core
 * soft reset control register and clearing them 10-20 microseconds
 * later.
 */
void mmi_dp_soft_reset(struct dptx *dptx, u32 bits)
{
	u32 rst;

	bits &= DPTX_SRST_CTRL_ALL;

	/* Set reset bits */
	rst = mmi_dp_read(dptx->base, SOFT_RESET_CTRL);
	rst |= bits;
	mmi_dp_write(dptx->base, SOFT_RESET_CTRL, rst);

	usleep_range(10, 20);

	/* Clear reset bits */
	rst = mmi_dp_read(dptx->base, SOFT_RESET_CTRL);
	rst &= ~bits;
	mmi_dp_write(dptx->base, SOFT_RESET_CTRL, rst);
}

/**
 * mmi_dp_soft_reset_all() - Reset all core modules
 * @dptx: The dptx struct
 */
static void mmi_dp_soft_reset_all(struct dptx *dptx)
{
	mmi_dp_soft_reset(dptx, DPTX_SRST_CTRL_ALL);
}

/**
 * mmi_dp_core_init_phy() - Initializes the DP TX PHY module
 * @dptx: The dptx struct
 *
 * Initializes the PHY layer of the core. This needs to be called
 * whenever the PHY layer is reset.
 */
void mmi_dp_core_init_phy(struct dptx *dptx)
{
	mmi_dp_clr(dptx->base, PHYIF_CTRL, PHYIF_PHY_WIDTH);
}

/**
 * mmi_dp_check_dptx_id_n_ver() - Check value of DPTX_ID register
 * @dptx: The dptx struct
 *
 * Return: True if DPTX core correctly identified.
 */
static bool mmi_dp_check_dptx_id_n_ver(struct dptx *dptx)
{
	u32 dptx_id, version;

	dptx_id = mmi_dp_read(dptx->base, DPTX_ID);
	version = mmi_dp_read(dptx->base, DPTX_VERSION_NUMBER);
	if ((dptx_id != ((DPTX_ID_DEVICE_ID << DPTX_ID_DEVICE_ID_SHIFT) |
			DPTX_ID_VENDOR_ID)) || version != DPTX_VERSION)
		return false;

	return true;
}

static void mmi_dp_init_hwparams(struct dptx *dptx)
{
	/* Combo PHY */
	dptx->hwparams.gen2_phy = mmi_dp_read_regfield(dptx->base,
						       DPTX_CONFIG_REG1,
						       DPTX_GEN2_PHY_MASK);

	/* Forward Error Correction (FEC) */
	dptx->hwparams.fec = mmi_dp_read_regfield(dptx->base,
						  DPTX_CONFIG_REG1,
						  DPTX_FEC_EN_MASK);

	/* Embedded DisplayPort (eDP) */
	dptx->hwparams.edp = mmi_dp_read_regfield(dptx->base,
						  DPTX_CONFIG_REG1,
						  DPTX_EDP_EN_MASK);

	/* Display Stream Compression (DSC) */
	dptx->hwparams.dsc = mmi_dp_read_regfield(dptx->base,
						  DPTX_CONFIG_REG1,
						  DPTX_DSC_EN_MASK);

	/* Multi pixel mode */
	dptx->hwparams.mp_mode = mmi_dp_read_regfield(dptx->base,
						      DPTX_CONFIG_REG1,
						      DPTX_MP_MODE_MASK);

	/* Max Number MST streams */
	dptx->hwparams.num_streams = mmi_dp_read_regfield(dptx->base,
							  DPTX_CONFIG_REG1,
							  DPTX_NUM_STREAMS_MASK);

	/* Sync Depth - 2 or 3 Stages */
	dptx->hwparams.sync_depth = mmi_dp_read_regfield(dptx->base,
							 DPTX_CONFIG_REG1,
							 DPTX_SYNC_DEPTH_MASK);

	/* FPGA - Internal Video and Audio Generators Instantiation */
	dptx->hwparams.fpga = mmi_dp_read_regfield(dptx->base,
						   DPTX_CONFIG_REG1,
						   DPTX_FPGA_EN_MASK);

	/* SDP Register Bank Size */
	dptx->hwparams.sdp_reg_bank_size = mmi_dp_read_regfield(dptx->base,
								DPTX_CONFIG_REG1,
								DPTX_SDP_REG_BANK_SZ_MASK);

	/* Audio Selected */
	dptx->hwparams.audio_select = mmi_dp_read_regfield(dptx->base,
							   DPTX_CONFIG_REG1,
							   DPTX_AUDIO_SELECT_MASK);

	/* HDCP */
	dptx->hwparams.hdcp = mmi_dp_read_regfield(dptx->base,
						   DPTX_CONFIG_REG1,
						   DPTX_HDCP_SELECT_MASK);

	/* Panel Self Refresh (PSR) Version */
	dptx->hwparams.psr_version = mmi_dp_read_regfield(dptx->base,
							  DPTX_CONFIG_REG3,
							  DPTX_PSR_VER_MASK);

	/* Adaptive Sync */
	dptx->hwparams.adsync = mmi_dp_read_regfield(dptx->base,
						     DPTX_CONFIG_REG3,
						     DPTX_ADSYNC_EN_MASK);

	/* PHY Type */
	dptx->hwparams.phy_type = mmi_dp_read_regfield(dptx->base,
						       DPTX_CONFIG_REG3,
						       DPTX_PHY_TYPE_MASK);
}

/**
 * mmi_dp_core_init() - Initializes the DP TX core
 * @dptx: The dptx struct
 *
 * Initialize the DP TX core and put it in a known state.
 * Return: returns 0 on success
 */
static void mmi_dp_core_init(struct dptx *dptx)
{
	u32 hpd_ien;

	/* Reset the core */
	mmi_dp_soft_reset_all(dptx);

	/* Enable MST */
	mmi_dp_write(dptx->base, CCTL,
		     (dptx->mst ? DPTX_CCTL_ENABLE_MST_MODE : 0));

	mmi_dp_core_init_phy(dptx);

	/* Enable all HPD interrupts */
	hpd_ien = mmi_dp_read(dptx->base, HPD_INTERRUPT_ENABLE);
	hpd_ien |= (DPTX_HPD_IEN_IRQ_EN |
		    DPTX_HPD_IEN_HOT_PLUG_EN |
		    DPTX_HPD_IEN_HOT_UNPLUG_EN);
	mmi_dp_write(dptx->base, HPD_INTERRUPT_ENABLE, hpd_ien);
}

/**
 * mmi_dp_core_deinit() - Deinitialize the core
 * @dptx: The dptx struct
 *
 * Disable the core in preparation for module shutdown.
 */
static void mmi_dp_core_deinit(struct dptx *dptx)
{
	mmi_dp_global_intr_dis(dptx);
	mmi_dp_soft_reset_all(dptx);
}

void mmi_dp_phy_set_lanes(struct dptx *dptx, unsigned int lanes)
{
	u32 val;

	dptx_dbg(dptx, "%s: lanes=%d\n", __func__, lanes);

	switch (lanes) {
	case 1:
		val = 0;
		break;
	case 2:
		val = 1;
		break;
	case 4:
		val = 2;
		break;
	default:
		dptx_warn(dptx, "Invalid number of lanes %d\n - will set to 4", lanes);
		val = 2;
		break;
	}

	mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_LANES, val);
}

void mmi_dp_phy_set_rate(struct dptx *dptx, unsigned int rate)
{
	dptx_dbg(dptx, "%s: rate=%d\n", __func__, rate);

	mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_PHY_RATE, rate);
}

void mmi_dp_phy_set_pre_emphasis(struct dptx *dptx,
				 unsigned int lane,
				 unsigned int level)
{
	u32 phytxeq;

	dptx_dbg(dptx, "%s: lane=%d, level=0x%x\n", __func__, lane, level);

	if (lane > 3) {
		dptx_warn(dptx, "Invalid Lane %d", lane);
		return;
	}

	if (level > 3) {
		dptx_warn(dptx, "Invalid pre-emphasis level %d, using 3", level);
		level = 3;
	}

	phytxeq = mmi_dp_read(dptx->base, PHY_TX_EQ);
	phytxeq &= ~DPTX_PHY_TX_EQ_PREEMP_MASK(lane);
	phytxeq |= (level << DPTX_PHY_TX_EQ_PREEMP_SHIFT(lane)) &
		   DPTX_PHY_TX_EQ_PREEMP_MASK(lane);

	mmi_dp_write(dptx->base, PHY_TX_EQ, phytxeq);
}

void mmi_dp_phy_set_vswing(struct dptx *dptx,
			   unsigned int lane,
			   unsigned int level)
{
	u32 phytxeq;

	dptx_dbg(dptx, "%s: lane=%d, level=0x%x\n", __func__, lane, level);

	if (lane > 3) {
		dptx_warn(dptx, "Invalid Lane %d", lane);
		return;
	}

	if (level > DPTX_PHY_TX_EQ_VSWING_LVL_3) {
		dptx_warn(dptx, "Invalid pre-emphasis level %d, using 3", level);
		level = DPTX_PHY_TX_EQ_VSWING_LVL_3;
	}

	phytxeq = mmi_dp_read(dptx->base, PHY_TX_EQ);
	phytxeq &= ~DPTX_PHY_TX_EQ_VSWING_MASK(lane);
	phytxeq |= (level << DPTX_PHY_TX_EQ_VSWING_SHIFT(lane)) &
		   DPTX_PHY_TX_EQ_VSWING_MASK(lane);

	mmi_dp_write(dptx->base, PHY_TX_EQ, phytxeq);
}

void mmi_dp_phy_set_pattern(struct dptx *dptx, unsigned int pattern)
{
	mmi_dp_write_mask(dptx, PHYIF_CTRL, PHYIF_TPS_SEL, pattern);
}

void mmi_dp_phy_enable_xmit(struct dptx *dptx, unsigned int lanes, bool enable)
{
	u32 phyifctrl;
	u32 mask = 0;

	phyifctrl = mmi_dp_read(dptx->base, PHYIF_CTRL);

	switch (lanes) {
	case 4:
		mask |= DPTX_PHYIF_CTRL_XMIT_EN(3);
		mask |= DPTX_PHYIF_CTRL_XMIT_EN(2);
		fallthrough;
	case 2:
		mask |= DPTX_PHYIF_CTRL_XMIT_EN(1);
		fallthrough;
	case 1:
		mask |= DPTX_PHYIF_CTRL_XMIT_EN(0);
		break;
	default:
		dptx_warn(dptx, "Invalid number of lanes %d\n", lanes);
		break;
	}

	if (enable)
		phyifctrl |= mask;
	else
		phyifctrl &= ~mask;

	mmi_dp_write(dptx->base, PHYIF_CTRL, phyifctrl);
}

int mmi_dp_phy_rate_to_bw(unsigned int rate)
{
	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		return DP_LINK_BW_1_62;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		return DP_LINK_BW_2_7;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		return DP_LINK_BW_5_4;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		return DP_LINK_BW_8_1;
	default:
		return -EINVAL;
	}
}

int mmi_dp_bw_to_phy_rate(unsigned int bw)
{
	switch (bw) {
	case DP_LINK_BW_1_62:
		return DPTX_PHYIF_CTRL_RATE_RBR;
	case DP_LINK_BW_2_7:
		return DPTX_PHYIF_CTRL_RATE_HBR;
	case DP_LINK_BW_5_4:
		return DPTX_PHYIF_CTRL_RATE_HBR2;
	case DP_LINK_BW_8_1:
		return DPTX_PHYIF_CTRL_RATE_HBR3;
	default:
		return DPTX_MAX_LINK_RATE;
	}
}

static __maybe_unused struct dptx *to_dptx(struct drm_bridge *bridge)
{
	return container_of(bridge, struct dptx, bridge);
}

void mmi_dp_notify(struct dptx *dptx)
{
	wake_up_interruptible(&dptx->waitq);
}

static void mmi_dp_notify_shutdown(struct dptx *dptx)
{
	atomic_set(&dptx->shutdown, 1);
	mmi_dp_notify(dptx);
}

/**
 * mmi_dp_max_rate - Calculate and return available max pixel clock
 * @link_rate: Link rate
 * @lane_num: Number of lanes
 * @bpp: bits per pixel
 *
 * Return: max pixel clock (Khz) supported by current link training
 */
static inline int mmi_dp_max_rate(int link_rate, u8 lane_num, u8 bpp)
{
	return link_rate * lane_num * 8 / bpp;
}

static void mmi_dp_aux_init(struct dptx *dptx)
{
	dptx->dp_aux.name = "MMI DPTx aux";
	dptx->dp_aux.dev = dptx->dev;
	dptx->dp_aux.transfer = mmi_dp_aux_transfer;
	drm_dp_aux_init(&dptx->dp_aux);
}

static int mmi_dp_aux_register(struct dptx *dptx)
{
	int ret;

	dptx->dp_aux.drm_dev = dptx->bridge.dev;

	ret = drm_dp_aux_register(&dptx->dp_aux);
	if (ret) {
		dptx_err(dptx, "%s: Failed to register drm_dp_aux %d\n",
			 __func__, ret);
		dptx->dp_aux.drm_dev = NULL;
		return ret;
	}

	return 0;
}

static int mmi_dp_bridge_attach(struct drm_bridge *bridge,
				struct drm_encoder *encoder,
				enum drm_bridge_attach_flags flags)
{
	struct dptx *dptx = (struct dptx *)bridge->driver_private;
	int ret;

	if (flags == DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		dptx_err(dptx, "%s : DRM_BRIDGE_ATTACH_NO_CONNECTOR\n", __func__);

	/* Register AUX with the DRM device. */
	ret = mmi_dp_aux_register(dptx);
	if (ret) {
		dptx_err(dptx, "%s: Failed to register DP AUX\n", __func__);
		return ret;
	}

	/* Initialize the MST topology manager */
	if (dptx->mst) {
		ret = mmi_dp_mst_init(dptx);
		if (ret) {
			dptx_err(dptx, "%s: Failed to initialize MST\n",
				 __func__);
			drm_dp_aux_unregister(&dptx->dp_aux);
			dptx->dp_aux.drm_dev = NULL;
			return ret;
		}
	}

	/* Process a sink that was connected while HPD remained masked. */
	if (mmi_dp_read_regfield(dptx->base, HPD_STATUS,
				 HPD_STATUS_MASK)) {
		atomic_set(&dptx->aux.abort, 0);
		atomic_set(&dptx->aux.serving, 1);
		ret = mmi_dp_handle_hotplug(dptx);
		atomic_set(&dptx->aux.serving, 0);
		if (ret)
			dptx_warn(dptx, "Failed to process initial HPD state: %d\n",
				  ret);
	}

	mmi_dp_enable_hpd_intr(dptx);

	return 0;
}

static void mmi_dp_bridge_detach(struct drm_bridge *bridge)
{
	struct dptx *dptx = (struct dptx *)bridge->driver_private;

	if (!dptx)
		return;

	/*
	 * Quiesce the HPD path before tearing the MST topology down. The
	 * threaded IRQ handler queues the MST link-probe work (via
	 * mmi_dp_handle_hotplug()/mmi_dp_mst_handle_hpd_irq()), which touches
	 * the DRM device. If it runs while the bridge is being detached the
	 * work can dereference a DRM device that is going away. Mask the
	 * interrupts, abort any in-flight AUX transfer, then wait for the
	 * hard/threaded handler to finish so no new MST work can be queued.
	 * mmi_dp_mst_deinit() below then drains the already-queued work via
	 * drm_dp_mst_topology_mgr_destroy().
	 */
	mmi_dp_global_intr_dis(dptx);
	atomic_set(&dptx->aux.abort, 1);
	synchronize_irq(dptx->irq);

	/* Tear down the MST topology manager */
	mmi_dp_mst_deinit(dptx);

	/* Unregister the aux */
	drm_dp_aux_unregister(&dptx->dp_aux);
	dptx->dp_aux.drm_dev = NULL;
}

static enum drm_connector_status mmi_dp_bridge_detect(struct drm_bridge *bridge,
						      struct drm_connector *connector)
{
	const struct dptx *dptx = (struct dptx *)bridge->driver_private;

	return dptx->conn_status;
}

static u32 *mmi_dp_bridge_get_output_bus_fmts(struct drm_bridge *bridge,
					      struct drm_bridge_state *bridge_state,
					      struct drm_crtc_state *crtc_state,
					      struct drm_connector_state *conn_state,
					      unsigned int *num_output_formats)
{
	u32 *out_bus_formats;

	out_bus_formats = kmalloc(sizeof(*out_bus_formats), GFP_KERNEL);
	if (!out_bus_formats) {
		*num_output_formats = 0;
		return NULL;
	}

	*num_output_formats = 1;
	out_bus_formats[0] = MEDIA_BUS_FMT_FIXED;

	return out_bus_formats;
}

static const struct dptx_format_map dptx_input_format_map[] = {
	{
		.media_bus_format	= MEDIA_BUS_FMT_RGB888_1X24,
		.bits_per_component	= 8,
		.pixels_per_sample	= 1,
	},
	{
		.media_bus_format	= MEDIA_BUS_FMT_RGB888_0_5X48,
		.bits_per_component	= 8,
		.pixels_per_sample	= 2,
	},
	{
		.media_bus_format	= MEDIA_BUS_FMT_RGB888_0_25X96,
		.bits_per_component	= 8,
		.pixels_per_sample	= 4,
	},
	{
		.media_bus_format	= MEDIA_BUS_FMT_RGB101010_1X30,
		.bits_per_component	= 10,
		.pixels_per_sample	= 1,
	},
	{
		.media_bus_format	= MEDIA_BUS_FMT_RGB101010_0_5X60,
		.bits_per_component	= 10,
		.pixels_per_sample	= 2,
	},
	{
		.media_bus_format	= MEDIA_BUS_FMT_RGB101010_0_25X120,
		.bits_per_component	= 10,
		.pixels_per_sample	= 4,
	},
		{
		.media_bus_format	= MEDIA_BUS_FMT_RGB121212_1X36,
		.bits_per_component	= 12,
		.pixels_per_sample	= 1,
	},
	{
		.media_bus_format	= MEDIA_BUS_FMT_RGB121212_0_5X72,
		.bits_per_component	= 12,
		.pixels_per_sample	= 2,
	},
	{
		.media_bus_format	= MEDIA_BUS_FMT_RGB121212_0_25X144,
		.bits_per_component	= 12,
		.pixels_per_sample	= 4,
	},
};

const struct dptx_format_map *mmi_dp_get_input_format(u32 media_bus_format)
{
	u32 i = 0;

	for (i = 0; i < ARRAY_SIZE(dptx_input_format_map); i++) {
		if (dptx_input_format_map[i].media_bus_format ==
		    media_bus_format)
			return &dptx_input_format_map[i];
	}

	return NULL;
}

/**
 * mmi_dp_select_crtc_output_bus_format - Negotiate the CRTC output bus format
 * @crtc: CRTC feeding the DP controller
 * @crtc_state: new CRTC state to update
 *
 * In SST mode drm_atomic_bridge_chain_select_bus_fmts() walks the DP bridge
 * chain and asks the CRTC to select one of the controller's supported input
 * media bus formats, storing the result in crtc_state->output_bus_format. The
 * MST virtual encoders have no bridge attached, so that negotiation never runs
 * and the field is left at 0, which the CRTC atomic_check then rejects.
 * Replicate the negotiation here using the same input format list the physical
 * bridge advertises.
 *
 * Return: the selected media bus format, or 0 if the CRTC supports none.
 */
u32 mmi_dp_select_crtc_output_bus_format(struct drm_crtc *crtc,
					 struct drm_crtc_state *crtc_state)
{
	u32 fmts[ARRAY_SIZE(dptx_input_format_map)];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dptx_input_format_map); ++i)
		fmts[i] = dptx_input_format_map[i].media_bus_format;

	return drm_helper_crtc_select_output_bus_format(crtc, crtc_state, fmts,
							ARRAY_SIZE(fmts));
}

static u32 *mmi_dp_bridge_get_input_bus_fmts(struct drm_bridge *bridge,
					     struct drm_bridge_state *bridge_state,
					     struct drm_crtc_state *crtc_state,
					     struct drm_connector_state *conn_state,
					     u32 output_format,
					     unsigned int *num_input_formats)
{
	u32 *in_bus_formats, i;

	in_bus_formats = kcalloc(ARRAY_SIZE(dptx_input_format_map),
				 sizeof(*in_bus_formats), GFP_KERNEL);
	if (!in_bus_formats) {
		*num_input_formats = 0;
		return NULL;
	}

	for (i = 0; i < ARRAY_SIZE(dptx_input_format_map); ++i)
		in_bus_formats[i] = dptx_input_format_map[i].media_bus_format;
	*num_input_formats = ARRAY_SIZE(dptx_input_format_map);

	return in_bus_formats;
}

static const struct drm_edid *mmi_dp_bridge_edid_read(struct drm_bridge *bridge,
						      struct drm_connector *connector)
{
	struct dptx *dptx = container_of(bridge, struct dptx, bridge);

	return drm_edid_read_ddc(connector, &dptx->dp_aux.ddc);
}

static enum drm_mode_status
mmi_dp_bridge_mode_valid(struct drm_bridge *bridge,
			 const struct drm_display_info *info,
			 const struct drm_display_mode *mode)
{
	struct dptx *dptx = container_of(bridge, struct dptx, bridge);
	u32 max_pxl_clk = 0;
	u32 link_rate = 0;

	dptx->bpp = mmi_dp_get_color_depth_bpp(dptx->vparams[0].bpc,
					       dptx->vparams[0].pix_enc);

	link_rate = mmi_dp_get_link_rate(dptx->link.rate);
	link_rate *= 1000;

	max_pxl_clk = mmi_dp_max_rate(link_rate, dptx->link.lanes, dptx->bpp);

	dptx_dbg(dptx, "%s Bpp %d, link_rate %d pixel clock set %d\n", __func__,
		 dptx->bpp, link_rate, max_pxl_clk);

	if (mode->clock > max_pxl_clk) {
		dptx_dbg(dptx, "filtered mode %s for high pixel rate\n",
			 mode->name);
		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

/* Function to fill the dptx display mode */
static void mmi_dp_configure_params(struct drm_display_mode *mode,
				    struct display_mode_t *cmode)

{
	/* FPS */
	cmode->refresh_rate = drm_mode_vrefresh(mode) * 1000;
	/* Pixel Clock */
	cmode->dtd.m_pixel_clock = mode->clock;
	/* Interlaced */
	cmode->dtd.m_interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE;

	/* Horizontal data */
	cmode->dtd.m_h_active = mode->hdisplay;
	cmode->dtd.m_h_blanking = mode->htotal - mode->hdisplay;
	cmode->dtd.m_h_border = 0;
	cmode->dtd.m_h_image_size = mode->hdisplay * mode->width_mm;
	cmode->dtd.m_h_sync_pulse_width = mode->hsync_end - mode->hsync_start;
	cmode->dtd.m_h_sync_offset = mode->hsync_start - mode->hdisplay;

	/* Vertical data */
	cmode->dtd.m_v_active = mode->vdisplay;
	cmode->dtd.m_v_blanking = mode->vtotal - mode->vdisplay;
	cmode->dtd.m_v_border = 0;
	cmode->dtd.m_v_image_size = mode->vdisplay * mode->height_mm;
	cmode->dtd.m_v_sync_pulse_width = mode->vsync_end - mode->vsync_start;
	cmode->dtd.m_v_sync_offset = mode->vsync_start - mode->vdisplay;
}

/* Configure the video mode */
static int mmi_dp_configure_video(struct dptx *dptx,
				  struct drm_display_mode *mode)
{
	struct video_params *vparams;
	struct display_mode_t current_vmode;
	struct dtd mdtd;
	u32 pixel_clk;
	u16 rate, peak_stream_bw, link_bw;
	u8 bpp;
	s64 fixp;
	int retval;

	/*
	 * See sec 5.1.5 of DP 1.4 spec for guidance on downstream device
	 * power management by source device.
	 */
	retval = drm_dp_dpcd_write_byte(&dptx->dp_aux, DP_SET_POWER,
					DP_SET_POWER_D3);
	if (retval)
		return retval;

	mdelay(10);
	retval = drm_dp_dpcd_write_byte(&dptx->dp_aux, DP_SET_POWER,
					DP_SET_POWER_D0);
	if (retval)
		return retval;

	mdelay(30);
	retval = drm_dp_dpcd_write_byte(&dptx->dp_aux, DP_SET_POWER,
					DP_SET_POWER_D0);
	if (retval)
		return retval;

	mdelay(10);

	dptx->link.rate = dptx->max_rate;
	dptx->link.lanes = dptx->max_lanes;
	retval = mmi_dp_full_link_training(dptx);
	if (retval < 0)
		return retval;
	if (retval)
		return -EINVAL;

	vparams = &dptx->vparams[0];

	/* Reset the dtd structure and fill it */
	mmi_dp_configure_params(mode, &current_vmode);
	mmi_dp_dtd_fill(&mdtd, &current_vmode);

	vparams->mdtd = mdtd;
	dptx->selected_pixel_clock = mode->clock;

	/* check if the link is enough for the payload requested */
	bpp = mmi_dp_get_color_depth_bpp(vparams->bpc, vparams->pix_enc);
	rate = mmi_dp_get_link_rate(dptx->link.rate);
	pixel_clk = vparams->mdtd.pixel_clock;
	fixp = drm_fixp_div(drm_int2fixp(bpp), drm_int2fixp(8));
	fixp = drm_fixp_mul(fixp, drm_int2fixp(pixel_clk));
	fixp = drm_fixp_div(fixp, drm_int2fixp(1000));
	peak_stream_bw = drm_fixp2int(fixp);
	link_bw = rate * dptx->link.lanes;

	if (peak_stream_bw > link_bw) {
		dptx_err(dptx, "ERROR: Mode chosen isn't suitable for Link Rate running\n");
		return -EINVAL;
	}

	/* Disable video stream */
	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_CTRL_N(0), VIDEO_STREAM_ENABLE_MASK, 0);
	/* As of now do sst configuration */
	retval = mmi_dp_sst_configuration(dptx);
	if (retval < 0) {
		dptx_err(dptx, "Failed sst configuration\n");
		return retval;
	}
	mmi_dp_clean_interrupts(dptx);
	return 0;
}

/* Atomic enable function */
static void mmi_dp_bridge_atomic_enable(struct drm_bridge *bridge,
					struct drm_atomic_state *old_bridge_state)
{
	struct dptx *dptx = container_of(bridge, struct dptx, bridge);
	struct drm_atomic_state *state = old_bridge_state;
	struct drm_crtc_state *crtc_state;
	struct drm_display_mode *adjusted_mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	int retval;

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);

	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	adjusted_mode = &crtc_state->adjusted_mode;

	/* Configure the video */
	retval = mmi_dp_configure_video(dptx, adjusted_mode);
	if (retval < 0) {
		dptx_err(dptx, "Failed to configure video mode\n");
		return;
	}
	mmi_dp_intr_en(dptx, DPTX_IEN_VIDEO_FIFO_OVERFLOW |
		       DPTX_IEN_AUDIO_FIFO_OVERFLOW);
	dptx->enabled = true;
}

static void mmi_dp_bridge_atomic_disable(struct drm_bridge *bridge,
					 struct drm_atomic_state *old_bridge_state)
{
	struct dptx *dptx = container_of(bridge, struct dptx, bridge);

	mmi_dp_intr_dis(dptx, DPTX_IEN_VIDEO_FIFO_UNDERFLOW |
			DPTX_IEN_VIDEO_FIFO_OVERFLOW |
			DPTX_IEN_AUDIO_FIFO_OVERFLOW);
	mmi_dp_write_mask(dptx, DPTX_VSAMPLE_CTRL_N(0), VIDEO_STREAM_ENABLE_MASK, 0);
	dptx->enabled = false;
}

static const struct drm_bridge_funcs mmi_dp_bridge_funcs = {
	.attach = mmi_dp_bridge_attach,
	.detach = mmi_dp_bridge_detach,
	.detect = mmi_dp_bridge_detect,
	.atomic_get_output_bus_fmts = mmi_dp_bridge_get_output_bus_fmts,
	.atomic_get_input_bus_fmts = mmi_dp_bridge_get_input_bus_fmts,

	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,

	.edid_read = mmi_dp_bridge_edid_read,
	.mode_valid = mmi_dp_bridge_mode_valid,
	.atomic_enable = mmi_dp_bridge_atomic_enable,
	.atomic_disable = mmi_dp_bridge_atomic_disable,
};

static int mmi_dp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev;
	struct dptx *dptx;
	u32 max_lanes;
	int retval;

	dev = &pdev->dev;

	dptx = devm_drm_bridge_alloc(dev, struct dptx, bridge,
				     &mmi_dp_bridge_funcs);
	if (IS_ERR(dptx))
		return PTR_ERR(dptx);

	/* Update the device node */
	dptx->dev = dev;

	/* Get MEM resources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dp");
	dptx->base = devm_ioremap_resource(dptx->dev, res);
	if (IS_ERR(dptx->base)) {
		dev_err(dev, "Failed to get and map memory resource\n");
		return PTR_ERR(dptx->base);
	}

	if (!mmi_dp_check_dptx_id_n_ver(dptx)) {
		dev_err(dev, "DPTX_ID or DPTX_VERSION_NUMBER not match to 0x%04x:0x%04x & 0x%08x\n",
			DPTX_ID_DEVICE_ID, DPTX_ID_VENDOR_ID, DPTX_VERSION);
		return -ENODEV;
	}

	/* Get IRQ numbers from device */
	dptx->irq = platform_get_irq_byname(pdev, "dptx");
	if (dptx->irq < 0)
		return dptx->irq;

	dev_info(dev, "IRQ number %d.\n", dptx->irq);

	retval = of_property_read_u32(dev->of_node, "xlnx,dp-lanes", &max_lanes);
	if (retval < 0 || (max_lanes != 1 && max_lanes != 2 && max_lanes != 4)) {
		max_lanes = 1;
		dev_warn(dev, "no lanes/invalid lane count, defaulting to 1 lane\n");
	}

	dptx->max_lanes = max_lanes;

	dptx->cr_fail = false;
	dptx->mst = of_property_read_bool(dev->of_node, "xlnx,mst-mode-en");
	dptx->ssc_en = false;
	dptx->streams = 1;
	dptx->multipixel = DPTX_MP_QUAD_PIXEL;

	mutex_init(&dptx->mutex);
	init_waitqueue_head(&dptx->waitq);
	atomic_set(&dptx->sink_request, 0);
	atomic_set(&dptx->shutdown, 0);
	atomic_set(&dptx->c_connect, 0);

	dptx->max_rate = DPTX_MAX_LINK_RATE;

	platform_set_drvdata(pdev, dptx);

	/* update connector status */
	dptx->bridge.driver_private = dptx;
	dptx->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID;
	dptx->bridge.interlace_allowed = true;
	dptx->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;
	dptx->bridge.of_node = pdev->dev.of_node;
	dptx->conn_status = connector_status_disconnected;

	/* Initialize AUX before enabling HPD interrupts. */
	mmi_dp_aux_init(dptx);

	/* Get next bridge in chain using drm_of_find_panel_or_bridge */
	devm_drm_bridge_add(dev, &dptx->bridge);

	mmi_dp_global_intr_dis(dptx);

	mmi_dp_core_init(dptx);

	mmi_dp_init_hwparams(dptx);

	retval = devm_request_threaded_irq(dptx->dev,
					   dptx->irq,
					   mmi_dp_irq,
					   mmi_dp_threaded_irq,
					   IRQF_SHARED | IRQ_LEVEL,
					   "dptx_main_handler",
					   dptx);
	if (retval) {
		dev_err(dev, "Request for irq %d failed\n", dptx->irq);
		return retval;
	}

	dev_dbg(dev, "MMI DP Tx Driver probed\n");
	return 0;
}

static void mmi_dp_remove(struct platform_device *plat)
{
	struct dptx *dptx = platform_get_drvdata(plat);

	mmi_dp_notify_shutdown(dptx);

	/* wait for completing outstanding transmission on phy */
	msleep(20);
	mmi_dp_core_deinit(dptx);

	dev_dbg(dptx->dev, "MMI DP Tx Driver removed\n");
}

static const struct of_device_id mmi_dptx_of_match[] = {
	{ .compatible = "amd,mmi-dptx-1.0", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, mmi_dptx_of_match);

static struct platform_driver mmi_dptx_driver = {
	.probe		= mmi_dp_probe,
	.remove		= mmi_dp_remove,
	.driver		= {
		.name	= "mmi_dptx",
		.of_match_table	= mmi_dptx_of_match,
	},
};

module_platform_driver(mmi_dptx_driver);

MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD MMI DisplayPort TX Driver");

// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated DisplayPort Tx driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_bridge.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>

#include "mmi_dp.h"

static void mmi_dp_parse_rx_capabilities(struct dptx *dptx, const u8 *rx_caps)
{
	/* DPCD_REV - 00000h */
	dptx->rx_caps.minor_rev_num = rx_caps[DPCD_REV] & 0x0F;
	dptx->rx_caps.major_rev_num = (rx_caps[DPCD_REV] & 0xF0) >> 4;

	/* MAX_LINK_RATE - 00001h */
	dptx->rx_caps.max_link_rate = rx_caps[MAX_LINK_RATE];

	/* MAX_LANE_COUNT - 00002h */
	dptx->rx_caps.max_lane_count = rx_caps[MAX_LANE_COUNT] & 0x0F;
	dptx->rx_caps.post_lt_adj_req_supported = (rx_caps[MAX_LANE_COUNT] & BIT(5)) >> 5;
	dptx->rx_caps.tps3_supported = (rx_caps[MAX_LANE_COUNT] & BIT(6)) >> 6;
	dptx->rx_caps.enhanced_frame_cap = (rx_caps[MAX_LANE_COUNT] & BIT(7)) >> 7;

	/* MAX_DOWNSPREAD - 00003h */
	dptx->rx_caps.max_downspread = rx_caps[MAX_DOWNSPREAD] & BIT(0);
	dptx->rx_caps.no_aux_transaction_link_training = (rx_caps[MAX_DOWNSPREAD] & BIT(6)) >> 6;
	dptx->rx_caps.tps4_supported = (rx_caps[MAX_DOWNSPREAD] & BIT(7)) >> 7;

	/* NORP & DP_PWR_VOLTAGE_CAP - 00004h */
	dptx->rx_caps.norp = rx_caps[NORP_DP_PWR_VOLTAGE_CAP] & BIT(0);
	dptx->rx_caps.crc_3d_option_supported = (rx_caps[MAX_DOWNSPREAD] & BIT(1)) >> 1;
	dptx->rx_caps.dp_pwer_cap_5v = (rx_caps[NORP_DP_PWR_VOLTAGE_CAP] & BIT(5)) >> 5;
	dptx->rx_caps.dp_pwer_cap_12v = (rx_caps[NORP_DP_PWR_VOLTAGE_CAP] & BIT(6)) >> 6;
	dptx->rx_caps.dp_pwer_cap_18v = (rx_caps[NORP_DP_PWR_VOLTAGE_CAP] & BIT(7)) >> 7;

	/* DOWN_STREAM_PORT_PRESENT - 00005h */
	dptx->rx_caps.dfp_present = rx_caps[DOWN_STREAM_PORT_PRESENT] & BIT(0);
	dptx->rx_caps.dfp_type = (rx_caps[DOWN_STREAM_PORT_PRESENT] & 0x06) >> 1;
	dptx->rx_caps.format_conversion = (rx_caps[DOWN_STREAM_PORT_PRESENT] & BIT(3)) >> 3;
	dptx->rx_caps.detailed_cap_info_available =
	(rx_caps[DOWN_STREAM_PORT_PRESENT] & BIT(4)) >> 4;

	/* MAIN_LINK_CHANNEL_CODING - 00006h */
	dptx->rx_caps.channel_coding_8b10b_supported = rx_caps[MAIN_LINK_CHANNEL_CODING] & BIT(0);

	/* DOWN_STREAM_PORT_COUNT - 00007h */
	dptx->rx_caps.dfp_count = rx_caps[DOWN_STREAM_PORT_COUNT] & 0x0F;
	dptx->rx_caps.msa_timing_par_ignored = (rx_caps[DOWN_STREAM_PORT_COUNT] & BIT(6)) >> 6;
	dptx->rx_caps.oui_support = (rx_caps[DOWN_STREAM_PORT_COUNT] & BIT(7)) >> 7;

	/* RECEIVE_PORT0_CAP_0 - 00008h */
	dptx->rx_caps.port0_local_edid_present = (rx_caps[RECEIVE_PORT0_CAP_0] & BIT(1)) >> 1;
	dptx->rx_caps.port0_associated_to_preceding_port = (rx_caps[RECEIVE_PORT0_CAP_0] &
	BIT(2)) >> 2;
	dptx->rx_caps.port0_hblank_expansion_capable = (rx_caps[RECEIVE_PORT0_CAP_0] & BIT(3)) >> 3;
	dptx->rx_caps.port0_buffer_size_unit = (rx_caps[RECEIVE_PORT0_CAP_0] & BIT(4)) >> 4;
	dptx->rx_caps.port0_buffer_size_per_port = (rx_caps[RECEIVE_PORT0_CAP_0] & BIT(5)) >> 5;

	/* RECEIVE_PORT0_CAP_1 - 00009h */
	dptx->rx_caps.port0_buffer_size = rx_caps[RECEIVE_PORT0_CAP_1];

	/* RECEIVE_PORT1_CAP_0 - 0000Ah */
	dptx->rx_caps.port1_local_edid_present = (rx_caps[RECEIVE_PORT1_CAP_0] & BIT(1)) >> 1;
	dptx->rx_caps.port1_associated_to_preceding_port =
	(rx_caps[RECEIVE_PORT1_CAP_0] & BIT(2)) >> 2;
	dptx->rx_caps.port1_hblank_expansion_capable = (rx_caps[RECEIVE_PORT1_CAP_0] & BIT(3)) >> 3;
	dptx->rx_caps.port1_buffer_size_unit = (rx_caps[RECEIVE_PORT1_CAP_0] & BIT(4)) >> 4;
	dptx->rx_caps.port1_buffer_size_per_port = (rx_caps[RECEIVE_PORT1_CAP_0] & BIT(5)) >> 5;

	/* RECEIVE_PORT1_CAP_1 - 0000Bh */
	dptx->rx_caps.port1_buffer_size = rx_caps[RECEIVE_PORT1_CAP_1];

	/* I2C_SPEED_CONTROL - 0000Ch */
	dptx->rx_caps.i2c_speed = rx_caps[I2C_SPEED_CONTROL];

	/* TRAINING_AUX_RD_INTERVAL - 0000Eh */
	dptx->rx_caps.training_aux_rd_interval = rx_caps[TRAINING_AUX_RD_INTERVAL] & 0x7F;
	dptx->rx_caps.extended_receiver_cap_present = (rx_caps[TRAINING_AUX_RD_INTERVAL] &
	BIT(7)) >> 7;

	/* ADAPTER_CAP - 0000Fh */
	dptx->rx_caps.force_load_sense_cap = rx_caps[ADAPTER_CAP] & BIT(0);
	dptx->rx_caps.alternate_i2c_pattern_cap = (rx_caps[ADAPTER_CAP] & BIT(1)) >> 1;
}

int mmi_dp_adjust_vswing_and_preemphasis(struct dptx *dptx)
{
	u8 lane_01 = 0, lane_23 = 0;
	int retval, i;

	retval = mmi_dp_read_dpcd(dptx, DP_ADJUST_REQUEST_LANE0_1, &lane_01);
	if (retval)
		return retval;

	retval = mmi_dp_read_dpcd(dptx, DP_ADJUST_REQUEST_LANE2_3, &lane_23);
	if (retval)
		return retval;

	for (i = 0; i < dptx->link.lanes; i++) {
		u8 pe = 0, vs = 0;

		switch (i) {
		case 0:
			pe = (lane_01 & DP_ADJUST_PRE_EMPHASIS_LANE0_MASK)
				>> DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT;
			vs = (lane_01 & DP_ADJUST_VOLTAGE_SWING_LANE0_MASK)
				>> DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT;
			break;
		case 1:
			pe = (lane_01 & DP_ADJUST_PRE_EMPHASIS_LANE1_MASK)
				>> DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT;
			vs = (lane_01 & DP_ADJUST_VOLTAGE_SWING_LANE1_MASK)
				>> DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT;
			break;
		case 2:
			pe = (lane_23 & DP_ADJUST_PRE_EMPHASIS_LANE0_MASK)
				>> DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT;
			vs = (lane_23 & DP_ADJUST_VOLTAGE_SWING_LANE0_MASK)
				>> DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT;
			break;
		case 3:
			pe = (lane_23 & DP_ADJUST_PRE_EMPHASIS_LANE1_MASK)
				>> DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT;
			vs = (lane_23 & DP_ADJUST_VOLTAGE_SWING_LANE1_MASK)
				>> DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT;
			break;
		default:
			break;
		}

		mmi_dp_phy_set_pre_emphasis(dptx, i, pe);
		mmi_dp_phy_set_vswing(dptx, i, vs);
	}

	return 0;
}

static int mmi_dp_handle_hotunplug(struct dptx *dptx)
{
	u32 hpd_ien;

	dptx_info(dptx, "DPTX - Hotunplug Detected");

	atomic_set(&dptx->sink_request, 0);
	dptx->link.trained = false;

	/* PHY Standby */
	mmi_dp_disable_datapath_phy(dptx);
	mmi_dp_power_state_change_phy(dptx, DPTX_PHY_POWER_DOWN);

	hpd_ien = mmi_dp_read(dptx->base, HPD_INTERRUPT_ENABLE);
	hpd_ien |= (DPTX_HPD_IEN_IRQ_EN |
		    DPTX_HPD_IEN_HOT_PLUG_EN |
		    DPTX_HPD_IEN_HOT_UNPLUG_EN);
	mmi_dp_write(dptx->base, HPD_INTERRUPT_ENABLE, hpd_ien);

	dptx->conn_status = connector_status_disconnected;

	return 0;
}

static int mmi_dp_alpm_is_available(struct dptx *dptx)
{
	u8 alpm_cap = 0;
	int retval;

	retval = mmi_dp_read_dpcd(dptx, RECEIVER_ALPM_CAPABILITIES, &alpm_cap);
	if (retval)
		return retval;
	dptx_dbg(dptx, "ALPM Availability: %lu\n", alpm_cap & BIT(0));
	return (alpm_cap & BIT(0));
}

static int mmi_dp_handle_hotplug(struct dptx *dptx)
{
	u8 rx_caps[DPTX_RECEIVER_CAP_SIZE];
	int alpm_availability, retval;
	struct edp_alpm *alpm;
	u32 hpd_ien;
	u8 sink_cnt;
	u8 byte;

	dptx_info(dptx, "DPTX - Hotplug Detected");

	mmi_dp_video_intr_dis(dptx);
	hpd_ien = mmi_dp_read(dptx->base, HPD_INTERRUPT_ENABLE);
	hpd_ien |= (DPTX_HPD_IEN_IRQ_EN |
		    DPTX_HPD_IEN_HOT_UNPLUG_EN);
	mmi_dp_write(dptx->base, HPD_INTERRUPT_ENABLE, hpd_ien);
	mmi_dp_enable_hpd_intr(dptx);

	mmi_dp_core_init_phy(dptx);
	mmi_dp_clr(dptx->base, CCTL, CCTL_DEFAULT_FAST_LINK_TRAIN_EN);

	/* HDCP Soft Reset */
	mmi_dp_set(dptx->base, SOFT_RESET_CTRL, HDCP_MODULE_RESET);
	usleep_range(10, 20);
	mmi_dp_clr(dptx->base, SOFT_RESET_CTRL, HDCP_MODULE_RESET);
	msleep(100);

	/* Read Sink DPCD registers - Receiver Capability */
	memset(rx_caps, 0, DPTX_RECEIVER_CAP_SIZE);
	retval = mmi_dp_read_bytes_from_dpcd(dptx, DP_DPCD_REV,
					     rx_caps, DPTX_RECEIVER_CAP_SIZE);
	if (retval) {
		dptx_err(dptx, "DPCD Sink Capabilities: Unable to retrieve. retval:%d\n", retval);
		return retval;
	}
	mmi_dp_parse_rx_capabilities(dptx, rx_caps);
	dptx_dbg(dptx, "DP Revision %x.%x\n", dptx->rx_caps.major_rev_num,
		 dptx->rx_caps.minor_rev_num);

	/* Read Sink DPCD registers - Extended Receiver Capability */
	if (dptx->rx_caps.extended_receiver_cap_present) {
		retval = mmi_dp_read_bytes_from_dpcd(dptx, 0x2200, rx_caps,
						     DPTX_RECEIVER_CAP_SIZE);
		if (retval) {
			dptx_err(dptx, "DPCD Extended Sink Capabilities: Unable to retrieve\n");
			return retval;
		}

		mmi_dp_parse_rx_capabilities(dptx, rx_caps);
		dptx_dbg(dptx, "Extended DP Revision %x.%x\n", dptx->rx_caps.major_rev_num,
			 dptx->rx_caps.minor_rev_num);
	}

	mmi_dp_write_dpcd(dptx, DP_SET_POWER, 0);
	msleep(100);
	mmi_dp_write_dpcd(dptx, DP_SET_POWER, 1);
	msleep(50);

	if (dptx->rx_caps.enhanced_frame_cap) {
		u8 val = 0;

		mmi_dp_read_dpcd(dptx, 0x00101, &val);
		val |= BIT(7);
		mmi_dp_write_dpcd(dptx, 0x00101, val);
		dptx_dbg(dptx, "ENHANCED FRAME CAPABILITY ACTIVATED");
	}

	mmi_dp_read_dpcd(dptx, DP_SINK_COUNT, &sink_cnt);
	sink_cnt &= 0x3F;
	if (sink_cnt == 0) {
		dptx_dbg(dptx, "ZERO SINKS CONNECTED");
		return 0;
	}

	/* Initialize ALPM variables */
	alpm = &dptx->alpm;
	alpm_availability = mmi_dp_alpm_is_available(dptx);
	if (alpm_availability)
		alpm->status = DISABLED;
	else
		alpm->status = NOT_AVAILABLE;

	/* Define Stream Mode */
	mmi_dp_write_mask(dptx, CCTL, CCTL_ENABLE_MST_MODE, dptx->mst);
	mmi_dp_read_dpcd(dptx, DP_MSTM_CAP, &byte);
	if (dptx->mst && byte) {
		mmi_dp_write_dpcd(dptx, DP_MSTM_CTRL, 0x7);
		dptx_dbg(dptx, "ENABLING MST ON SINK");
		mmi_dp_write_dpcd(dptx, DP_BRANCH_DEVICE_CTRL, 0x1);
	} else {
		mmi_dp_write_dpcd(dptx, DP_MSTM_CTRL, 0x0);
	}

	dptx->link.rate = dptx->max_rate;
	dptx->link.lanes = dptx->max_lanes;

	/* Initiate link training */
	if (dptx->fec) {
		mmi_dp_set(dptx->base, CCTL, CCTL_ENHANCE_FRAMING_WITH_FEC_EN);

		/* Set FEC_READY on the sink side */
		retval = mmi_dp_write_dpcd(dptx, DP_FEC_CONFIGURATION, DP_FEC_READY);
		if (retval)
			return retval;
	}

	if (dptx->rx_caps.no_aux_transaction_link_training) {
		mmi_dp_fast_link_training(dptx);
	} else {
		retval = mmi_dp_full_link_training(dptx);
		if (retval)
			return retval;
	}

	dptx->conn_status = connector_status_connected;

	/* Clean interrupts */
	mmi_dp_clean_interrupts(dptx);

	return 0;
}

irqreturn_t mmi_dp_threaded_irq(int irq, void *dev)
{
	u32 hpdsts, hpd_ien;
	struct dptx *dptx = dev;

	mutex_lock(&dptx->mutex);

	/*
	 * TODO this should be set after all AUX transactions that are
	 * queued are aborted. Currently we don't queue AUX and AUX is
	 * only started from this function.
	 */
	atomic_set(&dptx->aux.abort, 0);
	atomic_set(&dptx->aux.serving, 1);

	if (atomic_read(&dptx->c_connect)) {
		atomic_set(&dptx->c_connect, 0);

		if (mmi_dp_read_regfield(dptx->base, HPD_STATUS, HPD_STATUS_MASK))
			mmi_dp_handle_hotplug(dptx);
		else
			mmi_dp_handle_hotunplug(dptx);
		hpd_ien = mmi_dp_read(dptx->base, HPD_INTERRUPT_ENABLE);
		hpd_ien |= DPTX_HPD_IEN_IRQ_EN;
		mmi_dp_write(dptx->base, HPD_INTERRUPT_ENABLE, hpd_ien);
		mmi_dp_global_intr_en(dptx);
	}

	if (atomic_read(&dptx->sink_request)) {
		atomic_set(&dptx->sink_request, 0);
		hpdsts = 0x1;
		mmi_dp_write(dptx->base, HPD_STATUS, hpdsts);
		mmi_dp_global_intr_en(dptx);
	}

	atomic_set(&dptx->aux.serving, 0);

	mutex_unlock(&dptx->mutex);

	return IRQ_HANDLED;
}

static void mmi_dp_handle_hpd_irq(struct dptx *dptx)
{
	dptx_dbg(dptx, "%s: HPD_IRQ\n", __func__);
	mmi_dp_notify(dptx);
}

irqreturn_t mmi_dp_irq(int irq, void *dev)
{
	irqreturn_t retval = IRQ_HANDLED;
	struct dptx *dptx = dev;
	u32 ists;

	ists = mmi_dp_read(dptx->base, GENERAL_INTERRUPT);

	if (!(ists & DPTX_ISTS_ALL_INTR)) {
		dptx_dbg(dptx, "%s: IRQ_NONE\n", __func__);
		return IRQ_NONE;
	}

	if (FIELD_GET(GEN_INTR_SDP_EVENT_STREAM0, ists)) {
		dptx_dbg(dptx, "%s: DPTX_ISTS_SDP\n", __func__);
		/* TODO Handle and clear */
	}

	if (FIELD_GET(GEN_INTR_AUDIO_FIFO_OVERFLOW_STREAM0, ists)) {
		dptx_dbg(dptx, "%s: DPTX_ISTS_AUDIO_FIFO_OVERFLOW\n", __func__);
		mmi_dp_set(dptx->base, GENERAL_INTERRUPT,
			   GEN_INTR_AUDIO_FIFO_OVERFLOW_STREAM0);
	}

	if (FIELD_GET(GEN_INTR_VIDEO_FIFO_OVERFLOW_STREAM0, ists)) {
		dptx_dbg(dptx, "%s: DPTX_ISTS_VIDEO_FIFO_OVERFLOW\n", __func__);
		ists |= BIT(6);
		mmi_dp_write(dptx->base, GENERAL_INTERRUPT, ists);
	}

	if (FIELD_GET(GEN_INTR_VIDEO_FIFO_UNDERFLOW_STREAM0, ists)) {
		dptx_dbg(dptx, "%s: DPTX_ISTS_VIDEO_FIFO_UNDERFLOW\n", __func__);
		ists |= BIT(8);
		mmi_dp_write(dptx->base, GENERAL_INTERRUPT, ists);
	}

	if (FIELD_GET(GEN_INTR_HPD_EVENT, ists)) {
		u32 hpdsts;

		mmi_dp_global_intr_dis(dptx);

		if (mmi_dp_read_regfield(dptx->base, HPD_STATUS, HPD_IRQ)) {
			hpdsts = mmi_dp_read(dptx->base, HPD_STATUS);
			hpdsts |= BIT(0);
			mmi_dp_write(dptx->base, HPD_STATUS, hpdsts);
			mmi_dp_handle_hpd_irq(dptx);
			retval = IRQ_WAKE_THREAD;
		}

		if (mmi_dp_read_regfield(dptx->base, HPD_STATUS, HPD_HOT_PLUG)) {
			hpdsts = mmi_dp_read(dptx->base, HPD_STATUS);

			hpdsts |= BIT(1);
			mmi_dp_write(dptx->base, HPD_STATUS, hpdsts);

			atomic_set(&dptx->aux.abort, 1);
			atomic_set(&dptx->c_connect, 1);
			mmi_dp_notify(dptx);
			retval = IRQ_WAKE_THREAD;
		}

		if (mmi_dp_read_regfield(dptx->base, HPD_STATUS, HPD_HOT_UNPLUG)) {
			hpdsts = mmi_dp_read(dptx->base, HPD_STATUS);

			hpdsts |= BIT(2);
			mmi_dp_write(dptx->base, HPD_STATUS, hpdsts);

			atomic_set(&dptx->aux.abort, 1);
			atomic_set(&dptx->c_connect, 1);
			mmi_dp_notify(dptx);
			retval = IRQ_WAKE_THREAD;
		}
	}

	return retval;
}

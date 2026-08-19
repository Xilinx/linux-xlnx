// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated DisplayPort Tx driver - MST topology glue
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Integrate MMI DP TX driver with the DRM DisplayPort MST topology manager.
 * This provides the topology-manager lifecycle, sideband HPD IRQ plumbing,
 * dynamic creation of DRM connectors for discovered downstream sinks (with
 * detection and EDID/mode enumeration) and the virtual per-stream encoders
 * used to route each MST stream. Basic VCPI payload allocation/release,
 * virtual channel table programming, and per-stream video setup are wired
 * through atomic MST helpers.
 */

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "mmi_dp.h"
#include "mmi_dp_reg.h"

/* MST sideband AUX transactions carry at most 16 bytes of payload. */
#define MMI_DP_MST_MAX_DPCD_TRANSACTION_BYTES	16

/* Assumed pixel depth used for MST bandwidth (PBN) budgeting. */
#define MMI_DP_MST_DEFAULT_BPP			24

/* Bound the number of ESI batches handled for a single HPD IRQ. */
#define MMI_DP_MST_MAX_ESI_ITERATIONS		30

#define MMI_DP_MST_VCP_TABLE_REGS		8
#define MMI_DP_MST_SLOTS_PER_VCP_REG		8
#define MMI_DP_MST_VCP_TABLE_SLOTS		\
	(MMI_DP_MST_VCP_TABLE_REGS * MMI_DP_MST_SLOTS_PER_VCP_REG)

/**
 * struct mmi_dp_mst_encoder - A virtual per-stream MST encoder
 * @base: The DRM encoder
 * @dptx: Back-pointer to the owning DP TX core
 * @stream_id: Index of the MST stream this encoder drives
 */
struct mmi_dp_mst_encoder {
	struct drm_encoder base;
	struct dptx *dptx;
	int stream_id;
};

#define to_mmi_dp_mst_encoder(x) \
	container_of(x, struct mmi_dp_mst_encoder, base)

/**
 * struct mmi_dp_mst_connector - A dynamically created MST sink connector
 * @base: The DRM connector
 * @dptx: Back-pointer to the owning DP TX core
 * @port: The MST topology port this connector represents
 * @stream_id: Zero-based DP TX stream routed to this connector
 */
struct mmi_dp_mst_connector {
	struct drm_connector base;
	struct dptx *dptx;
	struct drm_dp_mst_port *port;
	int stream_id;
};

static inline struct mmi_dp_mst_connector *
to_mmi_dp_mst_connector(struct drm_connector *x)
{
	return container_of(x, struct mmi_dp_mst_connector, base);
}

static int mmi_dp_mst_trigger_act(struct dptx *dptx)
{
	u32 val;
	int ret;

	mmi_dp_write_mask(dptx, CCTL, DPTX_CCTL_INITIATE_MST_ACT_SEQ, 1);

	/*
	 * The controller clears INITIATE_MST_ACT_SEQ once the ACT sequence
	 * has completed on the main link. Software must ensure the bit is back
	 * to 0 before triggering it again, so wait for the self-clear here.
	 */
	ret = readl_poll_timeout(dptx->base + CCTL, val,
				 !(val & DPTX_CCTL_INITIATE_MST_ACT_SEQ),
				 200, 50000);
	if (ret)
		dptx_err(dptx, "MST: timed out waiting for ACT sequence\n");

	return ret;
}

static void
mmi_dp_mst_program_vcp_table(struct dptx *dptx,
			     struct drm_dp_mst_topology_state *mst_state)
{
	struct drm_dp_mst_atomic_payload *payload;
	u32 table[MMI_DP_MST_VCP_TABLE_REGS] = { 0 };
	unsigned int i;

	list_for_each_entry(payload, &mst_state->payloads, next) {
		struct mmi_dp_mst_connector *mst_conn;
		unsigned int shift;
		unsigned int start_slot, slot;
		unsigned int stream_id;

		if (payload->delete || payload->time_slots <= 0)
			continue;

		if (!payload->port || !payload->port->connector)
			continue;

		if (payload->vc_start_slot < mst_state->start_slot)
			continue;

		mst_conn = to_mmi_dp_mst_connector(payload->port->connector);
		if (mst_conn->stream_id < 0 ||
		    mst_conn->stream_id >= DPTX_MAX_STREAM_NUMBER)
			continue;

		stream_id = mst_conn->stream_id + 1;
		start_slot = payload->vc_start_slot;

		for (slot = start_slot;
		     slot < start_slot + payload->time_slots &&
		     slot < MMI_DP_MST_VCP_TABLE_SLOTS; slot++) {
			shift = (slot % MMI_DP_MST_SLOTS_PER_VCP_REG) * 4;
			table[slot / MMI_DP_MST_SLOTS_PER_VCP_REG] |=
				stream_id << shift;
		}
	}

	for (i = 0; i < MMI_DP_MST_VCP_TABLE_REGS; i++)
		mmi_dp_write(dptx->base, DPTX_MST_VCP_TABLE_REG_N(i), table[i]);
}

static struct mmi_dp_mst_connector *
mmi_dp_mst_connector_get_for_encoder(struct drm_atomic_state *state,
				     struct drm_encoder *encoder)
{
	struct drm_connector *connector;

	connector = drm_atomic_get_new_connector_for_encoder(state, encoder);
	if (!connector)
		connector = drm_atomic_get_old_connector_for_encoder(state,
								     encoder);
	if (!connector)
		return NULL;

	return to_mmi_dp_mst_connector(connector);
}

static void mmi_dp_mst_fill_dtd(struct dtd *mdtd,
				const struct drm_display_mode *mode)
{
	mmi_dp_dtd_reset(mdtd);

	mdtd->pixel_clock = mode->clock;
	mdtd->interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE;
	mdtd->h_active = mode->hdisplay;
	mdtd->h_blanking = mode->htotal - mode->hdisplay;
	mdtd->h_border = 0;
	mdtd->h_image_size = mode->hdisplay * mode->width_mm;
	mdtd->h_sync_pulse_width = mode->hsync_end - mode->hsync_start;
	mdtd->h_sync_offset = mode->hsync_start - mode->hdisplay;
	mdtd->h_sync_polarity = 1;
	mdtd->v_active = mode->vdisplay;
	mdtd->v_blanking = mode->vtotal - mode->vdisplay;
	mdtd->v_border = 0;
	mdtd->v_image_size = mode->vdisplay * mode->height_mm;
	mdtd->v_sync_pulse_width = mode->vsync_end - mode->vsync_start;
	mdtd->v_sync_offset = mode->vsync_start - mode->vdisplay;
	mdtd->v_sync_polarity = 1;
}

static int mmi_dp_mst_program_stream(struct dptx *dptx, int stream,
				     const struct drm_display_mode *mode,
				     u32 bus_format)
{
	const struct dptx_format_map *input_format;
	struct video_params *vparams;
	struct dtd *mdtd;
	int ret;

	if (stream < 0 || stream >= DPTX_MAX_STREAM_NUMBER)
		return -EINVAL;

	if (!dptx->link.trained) {
		mmi_dp_mst_fill_dtd(&dptx->vparams[0].mdtd, mode);
		dptx->vparams[0].refresh_rate = drm_mode_vrefresh(mode) * 1000;
		dptx->selected_pixel_clock = mode->clock;

		drm_dp_dpcd_writeb(&dptx->dp_aux, DP_SET_POWER, 2);
		mdelay(10);
		drm_dp_dpcd_writeb(&dptx->dp_aux, DP_SET_POWER, 1);
		mdelay(30);
		drm_dp_dpcd_writeb(&dptx->dp_aux, DP_SET_POWER, 1);
		mdelay(10);

		dptx->link.rate = dptx->max_rate;
		dptx->link.lanes = dptx->max_lanes;
		ret = mmi_dp_full_link_training(dptx);
		if (ret)
			return ret;

		if (stream != DEFAULT_STREAM)
			mmi_dp_disable_video_stream(dptx, DEFAULT_STREAM);
	}

	dptx->vparams[stream] = dptx->vparams[0];
	vparams = &dptx->vparams[stream];
	mdtd = &vparams->mdtd;
	mmi_dp_mst_fill_dtd(mdtd, mode);
	vparams->refresh_rate = drm_mode_vrefresh(mode) * 1000;
	dptx->selected_pixel_clock = mode->clock;

	/*
	 * Derive the pixel mode from the negotiated CRTC output bus format.
	 * In SST this is done by mmi_dp_full_link_training() from the physical
	 * bridge's input_bus_cfg, but in MST the physical dptx bridge is not
	 * part of the atomic commit (the virtual encoder has no bridge chain),
	 * so full_link_training() falls back to a single-pixel default. Using
	 * the format the MST atomic_check negotiated keeps the controller's
	 * pixels-per-clock in sync with what the CRTC actually feeds.
	 */
	input_format = mmi_dp_get_input_format(bus_format);
	if (input_format)
		dptx->multipixel = input_format->pixels_per_sample >> 1;

	mmi_dp_disable_video_stream(dptx, stream);
	mmi_dp_vinput_polarity_ctrl(dptx, stream);
	mmi_dp_vsample_ctrl(dptx, stream);
	mmi_dp_video_config1(dptx, stream);
	mmi_dp_video_config2(dptx, stream);
	mmi_dp_video_config3(dptx, stream);
	mmi_dp_video_config4(dptx, stream);
	mmi_dp_video_ts_calculate_stream(dptx, stream, dptx->link.lanes,
					 dptx->link.rate, vparams->bpc,
					 vparams->pix_enc, mdtd->pixel_clock);
	mmi_dp_video_ts_change(dptx, stream);

	if (dptx->rx_caps.enhanced_frame_cap)
		mmi_dp_write_mask(dptx, CCTL, CCTL_ENHANCE_FRAMING_EN, 1);

	mmi_dp_video_msa1(dptx, stream);
	mmi_dp_video_msa2(dptx, stream);
	mmi_dp_video_msa3(dptx, stream);
	mmi_dp_video_hblank_interval(dptx, stream);
	mmi_dp_enable_default_video_stream(dptx, stream);

	dptx_info(dptx, "MST stream %d: %dx%d @ %dHz\n", stream,
		  mdtd->h_active, mdtd->v_active,
		  (vparams->refresh_rate + 500) / 1000);

	return 0;
}

static int
mmi_dp_mst_encoder_atomic_check(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct mmi_dp_mst_encoder *mst_enc = to_mmi_dp_mst_encoder(encoder);
	struct drm_dp_mst_topology_state *mst_state;
	struct mmi_dp_mst_connector *mst_conn;
	struct dptx *dptx = mst_enc->dptx;
	int bpc, pbn, ret;

	mst_conn = to_mmi_dp_mst_connector(conn_state->connector);

	mst_state = drm_atomic_get_mst_topology_state(crtc_state->state,
						      &dptx->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	/*
	 * Select the CRTC output (pixel-pipeline) bus format. In SST mode the
	 * bridge chain negotiates this; the MST encoders have no bridge, so do
	 * it here or the CRTC atomic_check rejects the commit with -EINVAL.
	 */
	if (!mmi_dp_select_crtc_output_bus_format(crtc_state->crtc, crtc_state))
		return -EINVAL;

	/*
	 * Tell the topology manager the per-timeslot bandwidth so it can
	 * translate PBN into time slots. The link is trained at the
	 * controller's maximum rate and lane count (see
	 * mmi_dp_mst_program_stream()), so budget the slots accordingly.
	 */
	mst_state->pbn_div =
		drm_dp_get_vc_payload_bw(mmi_dp_get_link_rate(dptx->max_rate) *
					 1000, dptx->max_lanes);

	bpc = conn_state->connector->display_info.bpc;
	if (!bpc)
		bpc = 8;

	pbn = drm_dp_calc_pbn_mode(crtc_state->adjusted_mode.clock,
				   bpc * 3 << 4);
	ret = drm_dp_atomic_find_time_slots(crtc_state->state, &dptx->mst_mgr,
					    mst_conn->port, pbn);
	if (ret < 0)
		return ret;

	return drm_dp_mst_atomic_check(crtc_state->state);
}

static void mmi_dp_mst_encoder_atomic_enable(struct drm_encoder *encoder,
					     struct drm_atomic_state *state)
{
	struct mmi_dp_mst_encoder *mst_enc = to_mmi_dp_mst_encoder(encoder);
	struct dptx *dptx = mst_enc->dptx;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct mmi_dp_mst_connector *mst_conn;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;
	int ret;

	mst_conn = mmi_dp_mst_connector_get_for_encoder(state, encoder);
	if (!mst_conn)
		return;

	conn_state = drm_atomic_get_new_connector_state(state, &mst_conn->base);
	if (!conn_state || !conn_state->crtc)
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (!crtc_state)
		return;

	mst_state = drm_atomic_get_new_mst_topology_state(state,
							  &dptx->mst_mgr);
	if (!mst_state)
		return;

	payload = drm_atomic_get_mst_payload_state(mst_state, mst_conn->port);
	if (!payload)
		return;

	mst_conn->stream_id = mst_enc->stream_id;
	ret = mmi_dp_mst_program_stream(dptx, mst_enc->stream_id,
					&crtc_state->adjusted_mode,
					crtc_state->output_bus_format);
	if (ret) {
		dptx_err(dptx, "MST: stream %d programming failed: %d\n",
			 mst_enc->stream_id, ret);
		mst_conn->stream_id = -1;
		return;
	}

	ret = drm_dp_add_payload_part1(&dptx->mst_mgr, mst_state, payload);
	if (ret < 0) {
		dptx_err(dptx, "MST: add payload part1 failed: %d\n", ret);
		mmi_dp_disable_video_stream(dptx, mst_enc->stream_id);
		mst_conn->stream_id = -1;
		return;
	}

	mmi_dp_mst_program_vcp_table(dptx, mst_state);

	ret = mmi_dp_mst_trigger_act(dptx);
	if (ret)
		return;

	/* Wait for the sink to adopt the new VC payload table. */
	ret = drm_dp_check_act_status(&dptx->mst_mgr);
	if (ret < 0) {
		dptx_err(dptx, "MST: ACT not handled by sink: %d\n", ret);
		return;
	}

	ret = drm_dp_add_payload_part2(&dptx->mst_mgr, payload);
	if (ret < 0) {
		dptx_err(dptx, "MST: add payload part2 failed: %d\n", ret);
		return;
	}
}

static void mmi_dp_mst_encoder_atomic_disable(struct drm_encoder *encoder,
					      struct drm_atomic_state *state)
{
	struct mmi_dp_mst_encoder *mst_enc = to_mmi_dp_mst_encoder(encoder);
	struct dptx *dptx = mst_enc->dptx;
	struct mmi_dp_mst_connector *mst_conn;
	struct drm_dp_mst_topology_state *old_mst_state;
	struct drm_dp_mst_topology_state *new_mst_state;
	const struct drm_dp_mst_atomic_payload *old_payload;
	struct drm_dp_mst_atomic_payload *new_payload;

	mst_conn = mmi_dp_mst_connector_get_for_encoder(state, encoder);
	if (!mst_conn)
		return;

	old_mst_state = drm_atomic_get_old_mst_topology_state(state,
							      &dptx->mst_mgr);
	if (!old_mst_state)
		return;

	if (IS_ERR(old_mst_state)) {
		dptx_err(dptx, "MST: failed to get old topology state: %ld\n",
			 PTR_ERR(old_mst_state));
		return;
	}
	new_mst_state = drm_atomic_get_new_mst_topology_state(state,
							      &dptx->mst_mgr);
	if (!new_mst_state)
		return;

	if (IS_ERR(new_mst_state)) {
		dptx_err(dptx, "MST: failed to get new topology state: %ld\n",
			 PTR_ERR(new_mst_state));
		return;
	}

	old_payload = drm_atomic_get_mst_payload_state(old_mst_state,
						       mst_conn->port);
	new_payload = drm_atomic_get_mst_payload_state(new_mst_state,
						       mst_conn->port);
	if (!old_payload || !new_payload)
		return;

	/*
	 * Follow the payload-teardown contract documented by
	 * drm_dp_remove_payload_part1(): deallocate the payload along the
	 * virtual channel (part1), then reprogram the source VC payload table
	 * and trigger the ACT sequence so the branch/sink adopt the new table,
	 * and only afterwards finalize the local time-slot accounting (part2).
	 * Doing part2 before the ACT (as before) invalidated vc_start_slot
	 * while the hardware table was still being programmed.
	 */
	drm_dp_remove_payload_part1(&dptx->mst_mgr, new_mst_state, new_payload);

	if (mst_conn->stream_id >= 0)
		mmi_dp_disable_video_stream(dptx, mst_conn->stream_id);
	mst_conn->stream_id = -1;

	mmi_dp_mst_program_vcp_table(dptx, new_mst_state);
	mmi_dp_mst_trigger_act(dptx);

	drm_dp_remove_payload_part2(&dptx->mst_mgr, new_mst_state,
				    old_payload, new_payload);
}

static const struct drm_encoder_helper_funcs mmi_dp_mst_encoder_helper_funcs = {
	.atomic_check = mmi_dp_mst_encoder_atomic_check,
	.atomic_enable = mmi_dp_mst_encoder_atomic_enable,
	.atomic_disable = mmi_dp_mst_encoder_atomic_disable,
};

static int mmi_dp_mst_connector_get_modes(struct drm_connector *connector)
{
	struct mmi_dp_mst_connector *mst_conn =
		to_mmi_dp_mst_connector(connector);
	struct dptx *dptx = mst_conn->dptx;
	const struct drm_edid *drm_edid;
	int ret;

	if (drm_connector_is_unregistered(connector))
		return 0;

	drm_edid = drm_dp_mst_edid_read(connector, &dptx->mst_mgr,
					mst_conn->port);
	drm_edid_connector_update(connector, drm_edid);
	ret = drm_edid_connector_add_modes(connector);
	drm_edid_free(drm_edid);

	return ret;
}

static enum drm_mode_status
mmi_dp_mst_connector_mode_valid(struct drm_connector *connector,
				const struct drm_display_mode *mode)
{
	struct mmi_dp_mst_connector *mst_conn =
		to_mmi_dp_mst_connector(connector);
	int pbn;

	if (drm_connector_is_unregistered(connector))
		return MODE_ERROR;

	/* Reject modes that exceed the sink's available MST bandwidth. */
	pbn = drm_dp_calc_pbn_mode(mode->clock,
				   MMI_DP_MST_DEFAULT_BPP << 4);
	if (pbn > mst_conn->port->full_pbn)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int mmi_dp_mst_connector_detect_ctx(struct drm_connector *connector,
					   struct drm_modeset_acquire_ctx *ctx,
					   bool force)
{
	struct mmi_dp_mst_connector *mst_conn =
		to_mmi_dp_mst_connector(connector);
	struct dptx *dptx = mst_conn->dptx;

	if (drm_connector_is_unregistered(connector))
		return connector_status_disconnected;

	return drm_dp_mst_detect_port(connector, ctx, &dptx->mst_mgr,
				      mst_conn->port);
}

static int
mmi_dp_mst_connector_atomic_check(struct drm_connector *connector,
				  struct drm_atomic_state *state)
{
	struct mmi_dp_mst_connector *mst_conn =
		to_mmi_dp_mst_connector(connector);
	struct dptx *dptx = mst_conn->dptx;
	int ret;

	/*
	 * Release the connector's MST time slots when it is being disabled.
	 * This must live in the connector (not encoder) atomic_check: the DRM
	 * atomic helper only invokes the encoder atomic_check for connectors
	 * that still have a CRTC in the new state (see mode_fixup() in
	 * drm_atomic_helper.c), so a plain display-off commit would otherwise
	 * never pull the MST topology state into the commit. Without it,
	 * drm_dp_remove_payload_part1/2 are skipped in atomic_disable and the
	 * topology manager's time-slot accounting leaks across modesets.
	 *
	 * drm_dp_atomic_release_time_slots() self-gates (it is a no-op unless
	 * this connector is actually losing its CRTC), so it is safe to call
	 * on every connector check.
	 */
	ret = drm_dp_atomic_release_time_slots(state, &dptx->mst_mgr,
					       mst_conn->port);
	if (ret < 0)
		return ret;

	return drm_dp_mst_atomic_check(state);
}

static struct drm_encoder *
mmi_dp_mst_connector_atomic_best_encoder(struct drm_connector *connector,
					 struct drm_atomic_state *state)
{
	struct mmi_dp_mst_connector *mst_conn =
		to_mmi_dp_mst_connector(connector);
	struct dptx *dptx = mst_conn->dptx;
	struct drm_connector_state *conn_state =
		drm_atomic_get_new_connector_state(state, connector);

	if (!conn_state->crtc)
		return NULL;

	/* Map the target CRTC to one of the controller's virtual MST encoders. */
	return dptx->mst_encoders[drm_crtc_index(conn_state->crtc) %
				  DPTX_MAX_STREAM_NUMBER];
}

static const struct drm_connector_helper_funcs
mmi_dp_mst_connector_helper_funcs = {
	.get_modes = mmi_dp_mst_connector_get_modes,
	.mode_valid = mmi_dp_mst_connector_mode_valid,
	.detect_ctx = mmi_dp_mst_connector_detect_ctx,
	.atomic_check = mmi_dp_mst_connector_atomic_check,
	.atomic_best_encoder = mmi_dp_mst_connector_atomic_best_encoder,
};

static int mmi_dp_mst_root_connector_id(struct dptx *dptx)
{
	struct drm_connector_list_iter iter;
	struct drm_connector *connector;
	int root_id = -ENODEV;

	drm_connector_list_iter_begin(dptx->bridge.dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		if (!drm_connector_has_possible_encoder(connector,
							dptx->bridge.encoder))
			continue;

		root_id = connector->base.id;
		break;
	}
	drm_connector_list_iter_end(&iter);

	return root_id;
}

static int
mmi_dp_mst_connector_update_path(struct mmi_dp_mst_connector *mst_conn)
{
	struct drm_connector *connector = &mst_conn->base;
	struct dptx *dptx = mst_conn->dptx;
	const char *old_path, *port_path;
	char *path;
	int root_id;
	int ret;

	root_id = mmi_dp_mst_root_connector_id(dptx);
	if (root_id < 0)
		return root_id;

	if (!connector->path_blob_ptr)
		return -EINVAL;

	old_path = connector->path_blob_ptr->data;
	port_path = strchr(old_path, '-');
	if (!port_path)
		return -EINVAL;

	path = kasprintf(GFP_KERNEL, "mst:%d%s", root_id, port_path);
	if (!path)
		return -ENOMEM;

	ret = drm_connector_set_path_property(connector, path);
	kfree(path);
	if (ret)
		return ret;

	dptx->mst_mgr.conn_base_id = root_id;

	return 0;
}

static int mmi_dp_mst_connector_late_register(struct drm_connector *connector)
{
	struct mmi_dp_mst_connector *mst_conn =
		to_mmi_dp_mst_connector(connector);
	int ret;

	ret = mmi_dp_mst_connector_update_path(mst_conn);
	if (ret) {
		dptx_err(mst_conn->dptx,
			 "MST: failed to set connector root path: %d\n", ret);
		return ret;
	}

	return drm_dp_mst_connector_late_register(connector, mst_conn->port);
}

static void
mmi_dp_mst_connector_early_unregister(struct drm_connector *connector)
{
	struct mmi_dp_mst_connector *mst_conn =
		to_mmi_dp_mst_connector(connector);

	drm_dp_mst_connector_early_unregister(connector, mst_conn->port);
}

static void mmi_dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct mmi_dp_mst_connector *mst_conn =
		to_mmi_dp_mst_connector(connector);

	drm_dp_mst_put_port_malloc(mst_conn->port);
	drm_connector_cleanup(connector);
	kfree(mst_conn);
}

static const struct drm_connector_funcs mmi_dp_mst_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.late_register = mmi_dp_mst_connector_late_register,
	.early_unregister = mmi_dp_mst_connector_early_unregister,
	.destroy = mmi_dp_mst_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_connector *
mmi_dp_mst_add_connector(struct drm_dp_mst_topology_mgr *mgr,
			 struct drm_dp_mst_port *port, const char *path)
{
	struct dptx *dptx = container_of(mgr, struct dptx, mst_mgr);
	struct drm_device *drm = dptx->bridge.dev;
	struct mmi_dp_mst_connector *mst_conn;
	struct drm_connector *connector;
	unsigned int i;
	int ret;

	mst_conn = kzalloc(sizeof(*mst_conn), GFP_KERNEL);
	if (!mst_conn)
		return NULL;

	mst_conn->dptx = dptx;
	mst_conn->port = port;
	mst_conn->stream_id = -1;
	connector = &mst_conn->base;

	/* Keep the port allocation alive for the lifetime of the connector. */
	drm_dp_mst_get_port_malloc(port);

	ret = drm_connector_dynamic_init(drm, connector,
					 &mmi_dp_mst_connector_funcs,
					 DRM_MODE_CONNECTOR_DisplayPort, NULL);
	if (ret) {
		dptx_err(dptx, "MST: failed to init connector: %d\n", ret);
		drm_dp_mst_put_port_malloc(port);
		kfree(mst_conn);
		return NULL;
	}

	drm_connector_helper_add(connector, &mmi_dp_mst_connector_helper_funcs);

	/* Initialize the connector's atomic state before attaching props. */
	connector->funcs->reset(connector);

	/* Any of the virtual MST stream encoders may drive this connector. */
	for (i = 0; i < DPTX_MAX_STREAM_NUMBER; i++) {
		if (!dptx->mst_encoders[i]) {
			dptx_err(dptx, "MST: encoder %u not initialized\n", i);
			ret = -EINVAL;
			goto err_cleanup;
		}

		ret = drm_connector_attach_encoder(connector,
						   dptx->mst_encoders[i]);
		if (ret) {
			dptx_err(dptx, "MST: failed to attach encoder: %d\n",
				 ret);
			goto err_cleanup;
		}
	}

	drm_object_attach_property(&connector->base,
				   drm->mode_config.path_property, 0);
	drm_object_attach_property(&connector->base,
				   drm->mode_config.tile_property, 0);

	ret = drm_connector_set_path_property(connector, path);
	if (ret) {
		dptx_err(dptx, "MST: failed to set path property: %d\n", ret);
		goto err_cleanup;
	}

	dptx_dbg(dptx, "MST: created connector for path %s\n", path);

	return connector;

err_cleanup:
	drm_connector_cleanup(connector);
	drm_dp_mst_put_port_malloc(port);
	kfree(mst_conn);

	return NULL;
}

static const struct drm_dp_mst_topology_cbs mmi_dp_mst_cbs = {
	.add_connector = mmi_dp_mst_add_connector,
};

/**
 * mmi_dp_mst_encoders_init - Create the virtual per-stream MST encoders
 * @dptx: The dptx struct
 *
 * MST can carry several independent streams over a single physical link, so
 * each potential payload needs its own DRM encoder. Create one virtual
 * DRM_MODE_ENCODER_DPMST encoder per stream, sharing the CRTC routing of the
 * primary bridge encoder. The encoders are managed by the DRM device and are
 * cleaned up automatically on teardown.
 *
 * Return: 0 on success, or a negative error code otherwise
 */
static int mmi_dp_mst_encoders_init(struct dptx *dptx)
{
	struct drm_device *drm = dptx->bridge.dev;
	unsigned int i;

	for (i = 0; i < DPTX_MAX_STREAM_NUMBER; i++) {
		struct mmi_dp_mst_encoder *mst_enc;

		mst_enc = drmm_encoder_alloc(drm, struct mmi_dp_mst_encoder,
					     base, NULL,
					     DRM_MODE_ENCODER_DPMST,
					     "mmi-dp-mst-%u", i);
		if (IS_ERR(mst_enc)) {
			dptx_err(dptx, "MST: failed to create encoder %u: %ld\n",
				 i, PTR_ERR(mst_enc));
			return PTR_ERR(mst_enc);
		}

		mst_enc->dptx = dptx;
		mst_enc->stream_id = i;
		drm_encoder_helper_add(&mst_enc->base,
				       &mmi_dp_mst_encoder_helper_funcs);
		/* Route through the same CRTCs as the primary bridge encoder */
		mst_enc->base.possible_crtcs =
			dptx->bridge.encoder->possible_crtcs;

		dptx->mst_encoders[i] = &mst_enc->base;
	}

	return 0;
}

/**
 * mmi_dp_mst_init - Initialize the MST topology manager
 * @dptx: The dptx struct
 *
 * Create the virtual per-stream encoders and bind the DRM DP MST topology
 * manager to the DP AUX channel and the DRM device backing the bridge. Must be
 * called after the bridge is attached and the AUX channel is registered.
 *
 * Return: 0 on success, or a negative error code otherwise
 */
int mmi_dp_mst_init(struct dptx *dptx)
{
	struct drm_device *drm = dptx->bridge.dev;
	int max_dpcd_transaction_bytes = MMI_DP_MST_MAX_DPCD_TRANSACTION_BYTES;
	int conn_base_id = dptx->bridge.encoder->base.id;
	int ret;

	/* If MST is not enabled, skip the topology manager initialization. */
	if (!dptx->mst)
		return 0;

	ret = mmi_dp_mst_encoders_init(dptx);
	if (ret)
		return ret;

	dptx->mst_mgr.cbs = &mmi_dp_mst_cbs;

	/*
	 * The bridge connector is created after the bridge attach callback
	 * returns. Use the physical encoder ID as a temporary unique root; the
	 * connector late-register callback replaces it with the connector ID
	 * before the MST PATH property becomes visible to userspace.
	 */
	ret = drm_dp_mst_topology_mgr_init(&dptx->mst_mgr, drm, &dptx->dp_aux,
					   max_dpcd_transaction_bytes,
					   DPTX_MAX_STREAM_NUMBER,
					   conn_base_id);
	if (ret) {
		dptx_err(dptx, "Failed to init MST topology manager: %d\n",
			 ret);
		return ret;
	}

	dptx_dbg(dptx, "MST topology manager initialized\n");

	return 0;
}

/**
 * mmi_dp_mst_deinit - Tear down the MST topology manager
 * @dptx: The dptx struct
 */
void mmi_dp_mst_deinit(struct dptx *dptx)
{
	if (dptx->mst)
		drm_dp_mst_topology_mgr_destroy(&dptx->mst_mgr);
}

/**
 * mmi_dp_mst_set_state - Enable or disable MST mode
 * @dptx: The dptx struct
 * @enable: True to switch to MST, false to switch to SST
 *
 * Put the controller core into the requested stream mode and update the MST
 * topology manager, which performs the sink-side DPCD programming and, when
 * enabling, kicks off downstream topology discovery.
 *
 * Return: 0 on success, or a negative error code otherwise
 */
int mmi_dp_mst_set_state(struct dptx *dptx, bool enable)
{
	int ret, root_id;

	if (!dptx->mst)
		return 0;

	if (enable) {
		root_id = mmi_dp_mst_root_connector_id(dptx);
		if (root_id >= 0)
			dptx->mst_mgr.conn_base_id = root_id;
	}

	mmi_dp_write_mask(dptx, CCTL, CCTL_ENABLE_MST_MODE, enable);

	ret = drm_dp_mst_topology_mgr_set_mst(&dptx->mst_mgr, enable);
	if (ret) {
		dptx_err(dptx, "Failed to %s MST topology: %d\n",
			 enable ? "enable" : "disable", ret);
		return ret;
	}

	dptx_info(dptx, "MST topology %s\n", enable ? "enabled" : "disabled");

	return 0;
}

/**
 * mmi_dp_mst_handle_hpd_irq - Forward an HPD IRQ to the MST topology manager
 * @dptx: The dptx struct
 *
 * On a short HPD pulse, read the ESI block and let the topology manager
 * process any pending sideband messages. No-op while running in SST mode.
 * Must be called from the threaded IRQ context, where AUX transactions are
 * permitted.
 */
void mmi_dp_mst_handle_hpd_irq(struct dptx *dptx)
{
	unsigned int i;
	int ret;

	if (!dptx->mst || !dptx->mst_mgr.mst_state)
		return;

	for (i = 0; i < MMI_DP_MST_MAX_ESI_ITERATIONS; i++) {
		u8 esi[4] = {};
		u8 ack[4] = {};
		bool handled;

		ret = drm_dp_dpcd_read_data(&dptx->dp_aux,
					    DP_SINK_COUNT_ESI, esi,
					    sizeof(esi));
		if (ret) {
			dptx_dbg(dptx, "MST: failed to read ESI: %d\n", ret);
			return;
		}

		drm_dp_mst_hpd_irq_handle_event(&dptx->mst_mgr, esi, ack,
						&handled);
		if (!handled)
			return;

		ret = drm_dp_dpcd_write_data(&dptx->dp_aux,
					     DP_SINK_COUNT_ESI + 1,
					     &ack[1], sizeof(ack) - 1);
		if (ret) {
			dptx_err(dptx, "MST: failed to acknowledge ESI: %d\n",
				 ret);
			return;
		}

		drm_dp_mst_hpd_irq_send_new_request(&dptx->mst_mgr);
	}

	dptx_warn(dptx, "MST: ESI processing limit reached\n");
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated DisplayPort Tx driver - MST topology glue
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Boilerplate integration with the DRM DisplayPort MST topology manager.
 * This provides the topology-manager lifecycle, sideband HPD IRQ plumbing,
 * dynamic creation of DRM connectors for discovered downstream sinks (with
 * detection and EDID/mode enumeration) and the virtual per-stream encoders
 * used to route each MST stream. Basic VCPI payload allocation/release is
 * wired through atomic MST helpers; virtual channel table programming and the
 * associated per-stream MST hardware setup remain future work.
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
 */
struct mmi_dp_mst_connector {
	struct drm_connector base;
	struct dptx *dptx;
	struct drm_dp_mst_port *port;
};

#define to_mmi_dp_mst_connector(x) \
	container_of(x, struct mmi_dp_mst_connector, base)

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

	if (!conn_state->crtc || !crtc_state->enable) {
		ret = drm_dp_atomic_release_time_slots(crtc_state->state,
						       &dptx->mst_mgr,
						       mst_conn->port);
		if (ret < 0)
			return ret;

		return drm_dp_mst_atomic_check(crtc_state->state);
	}

	mst_state = drm_atomic_get_mst_topology_state(crtc_state->state,
						      &dptx->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

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

static const struct drm_encoder_helper_funcs mmi_dp_mst_encoder_helper_funcs = {
	.atomic_check = mmi_dp_mst_encoder_atomic_check,
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

	/* TODO: Program the DP TX MST/SST mode bit in the hardware commit. */
	if (enable) {
		root_id = mmi_dp_mst_root_connector_id(dptx);
		if (root_id >= 0)
			dptx->mst_mgr.conn_base_id = root_id;
	}

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

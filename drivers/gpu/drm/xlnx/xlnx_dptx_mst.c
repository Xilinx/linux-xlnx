// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DP TX - MST (Multi-Stream Transport) code (DP1.4 / 8b10b).
 */
#include <drm/drm_fixed.h>
#include <drm/drm_print.h>
#include <linux/of_graph.h>

#include "xlnx_dptx.h"

/*
 * Set to 1 to enable verbose MST debug logging (per-register MST access trace
 * and MST bring-up/teardown status messages). These emit many dev_info() lines
 * per modeset; keep 0 for normal operation.
 */
#ifndef XLNX_DP_MST_DBG
#define XLNX_DP_MST_DBG 0
#endif

#if XLNX_DP_MST_DBG
#define xlnx_dp_mst_dbg(dp, fmt, ...) \
	dev_info((dp)->dev, fmt, ##__VA_ARGS__)
#else
#define xlnx_dp_mst_dbg(dp, fmt, ...) \
	do { if (0) dev_info((dp)->dev, fmt, ##__VA_ARGS__); } while (0)
#endif

/* Upper bound on back-to-back ESI events serviced per HPD IRQ. */
#define XDPTX_MST_ESI_MAX_ITER	10

/* MST-aware register helpers - every access is logged under XLNX_DP_MST_DBG */
static inline void xlnx_dp_mst_write(struct xlnx_dp *dp, u32 offset, u32 val)
{
	xlnx_dp_mst_dbg(dp, "MST: reg: 0x%03x val: 0x%x\n", offset, val);
	xlnx_dp_write(dp->dp_base, offset, val);
}

static inline u32 xlnx_dp_mst_read(struct xlnx_dp *dp, u32 offset)
{
	u32 val = xlnx_dp_read(dp->dp_base, offset);

	xlnx_dp_mst_dbg(dp, "MST: reg read: 0x%03x val: 0x%x\n", offset, val);
	return val;
}

static inline void xlnx_dp_mst_set(struct xlnx_dp *dp, u32 offset, u32 set)
{
	u32 val = xlnx_dp_read(dp->dp_base, offset) | set;

	xlnx_dp_mst_dbg(dp, "MST: reg: 0x%03x val: 0x%x (set mask 0x%x)\n",
			offset, val, set);
	xlnx_dp_write(dp->dp_base, offset, val);
}

/**
 * xlnx_dp_mst_write_sec_stream - program ENABLE_SEC_STREAM (8b/10b)
 * @dp: DisplayPort IP core structure
 * @on: whether the secondary stream should be enabled for this write
 */
static inline void xlnx_dp_mst_write_sec_stream(struct xlnx_dp *dp, bool on)
{
	xlnx_dp_mst_write(dp, XDPTX_ENABLE_SEC_STREAM_REG, on ? 0x1 : 0x0);
}

/**
 * xlnx_dp_mst_commit_vcp - assert the VC payload update (8b/10b)
 * @dp: DisplayPort IP core structure
 *
 * Holds MST_EN set alongside VCP_UPDATED to trigger a VC payload table update.
 */
static inline void xlnx_dp_mst_commit_vcp(struct xlnx_dp *dp)
{
	xlnx_dp_mst_write(dp, XDPTX_MST_CONFIG,
			  XDPTX_MST_CONFIG_MST_EN_MASK |
			  XDPTX_MST_CONFIG_VCP_UPDATED_MASK);
}

/**
 * xlnx_dp_mst_enable - enable MST mode in the DP TX and the downstream sink
 * @dp: DisplayPort IP core structure
 *
 * Must be called after successful link training. Writes DP_MSTM_CTRL =
 * MST_EN | UP_REQ_EN | UP_IS_SRC via AUX and sets MST_EN in the
 * XDPTX_MST_CONFIG register.
 *
 * Return: 0 on success, negative errno on AUX failure.
 */
int xlnx_dp_mst_enable(struct xlnx_dp *dp)
{
	int ret;

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_MSTM_CTRL,
				 DP_MST_EN | DP_UP_REQ_EN | DP_UPSTREAM_IS_SRC);
	if (ret < 0) {
		dev_err(dp->dev, "failed to write MSTM_CTRL (%d)\n", ret);
		return ret;
	}

	xlnx_dp_write(dp->dp_base, XDPTX_MST_CONFIG,
		      XDPTX_MST_CONFIG_MST_EN_MASK);

	return 0;
}

/**
 * xlnx_dp_mst_clear_payload_table - clear the VC payload ID table
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 on success, negative errno on AUX failure.
 */
int xlnx_dp_mst_clear_payload_table(struct xlnx_dp *dp)
{
	int i, ret;

	for (i = 0; i < XDPTX_VC_PAYLOAD_BUFF_NSLOTS; i++)
		xlnx_dp_write(dp->dp_base, XDPTX_VC_PAYLOAD_BUFF + 4 * i, 0);

	ret = drm_dp_dpcd_clear_payload(&dp->aux);
	if (ret < 0) {
		dev_err(dp->dev,
			"DPCD payload clear timed out (%d)\n", ret);
		return ret;
	}

	return 0;
}

/**
 * xlnx_dp_mst_clear_hpd_pulse_mask - clear the HPD-pulse interrupt mask bit
 * @dp: DisplayPort IP core structure
 */
static void xlnx_dp_mst_clear_hpd_pulse_mask(struct xlnx_dp *dp)
{
	u32 mask = xlnx_dp_read(dp->dp_base, XDPTX_INTR_MASK_REG);

	if (mask & XDPTX_INTR_HPDPULSE_MASK)
		xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG,
			      mask & ~XDPTX_INTR_HPDPULSE_MASK);
}

/**
 * xlnx_dp_mst_process_esi - read ESI regs and forward MST sideband events
 * @dp: DisplayPort IP core structure
 *
 * Holds dp->esi_lock for the duration so the two callers (the HPD-pulse work
 * item and the poll_hpd_irq callback) never reassemble a sideband message
 * concurrently.
 */
void xlnx_dp_mst_process_esi(struct xlnx_dp *dp)
{
	bool kick_hotplug = false;
	int iter;

	mutex_lock(&dp->esi_lock);

	for (iter = 0; iter < XDPTX_MST_ESI_MAX_ITER; iter++) {
		u8 esi[8] = {0};
		u8 esi_ack[8] = {0};
		bool handled = false;
		int ret;

		ret = drm_dp_dpcd_read(&dp->aux, DP_SINK_COUNT_ESI, esi,
				       sizeof(esi));
		if (ret < 0) {
			xlnx_dp_mst_dbg(dp,
					"MST: ESI read failed (%d)\n", ret);
			break;
		}

		drm_dp_mst_hpd_irq_handle_event(&dp->mst_mgr, esi, esi_ack,
						&handled);
		if (!handled)
			break;

		ret = drm_dp_dpcd_write(&dp->aux, DP_SINK_COUNT_ESI + 1,
					&esi_ack[1], 1);
		if (ret < 0) {
			dev_warn_ratelimited(dp->dev,
					     "MST: ESI ack write failed (%d)\n",
					     ret);
			break;
		}

		if (esi[1] & DP_UP_REQ_MSG_RDY)
			kick_hotplug = true;
	}

	drm_dp_mst_hpd_irq_send_new_request(&dp->mst_mgr);

	mutex_unlock(&dp->esi_lock);

	if (kick_hotplug)
		drm_kms_helper_hotplug_event(dp->drm);

	xlnx_dp_mst_clear_hpd_pulse_mask(dp);
}

/**
 * xlnx_dp_mst_poll_hpd_irq - poll_hpd_irq callback for the MST topology manager
 * @mgr: MST topology manager embedded in struct xlnx_dp
 */
static void xlnx_dp_mst_poll_hpd_irq(struct drm_dp_mst_topology_mgr *mgr)
{
	struct xlnx_dp *dp = container_of(mgr, struct xlnx_dp, mst_mgr);

	xlnx_dp_mst_process_esi(dp);
}

/* ---- MST encoder/connector helpers ---- */

static inline struct xlnx_dp_mst_encoder *
encoder_to_mst_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct xlnx_dp_mst_encoder, encoder);
}

static inline struct xlnx_dp_mst_connector *
connector_to_mst_connector(struct drm_connector *connector)
{
	return container_of(connector, struct xlnx_dp_mst_connector, connector);
}

static inline struct xlnx_dp_mst_conn_state *
to_mst_conn_state(struct drm_connector_state *state)
{
	return container_of(state, struct xlnx_dp_mst_conn_state, base);
}

/* ---- MST connector funcs ---- */

/**
 * xlnx_dp_mst_connector_get_modes - read and add EDID modes for a connector
 * @connector: MST connector
 *
 * Return: the number of modes added.
 */
static int xlnx_dp_mst_connector_get_modes(struct drm_connector *connector)
{
	struct xlnx_dp_mst_connector *mst_conn =
		connector_to_mst_connector(connector);
	const struct drm_edid *drm_edid;
	int num_modes = 0;

	if (!mst_conn->port->aux.ddc.algo)
		return 0;

	drm_edid = drm_dp_mst_edid_read(connector, &mst_conn->dp->mst_mgr,
					mst_conn->port);
	if (drm_edid) {
		drm_edid_connector_update(connector, drm_edid);
		num_modes = drm_edid_connector_add_modes(connector);
		drm_edid_free(drm_edid);
	}

	return num_modes;
}

/**
 * xlnx_dp_mst_connector_mode_valid - check a mode against the link budget
 * @connector: MST connector
 * @mode:      display mode to validate
 *
 * Return: MODE_OK or MODE_CLOCK_HIGH.
 */
static enum drm_mode_status
xlnx_dp_mst_connector_mode_valid(struct drm_connector *connector,
				 const struct drm_display_mode *mode)
{
	struct xlnx_dp_mst_connector *mst_conn =
		connector_to_mst_connector(connector);
	struct xlnx_dp *dp = mst_conn->dp;
	u32 bpp = dp->fmt.bpp;
	u32 max_pclock;

	if (connector->state) {
		struct xlnx_dp_mst_conn_state *mst_conn_state =
			to_mst_conn_state(connector->state);

		if (mst_conn_state->fmt.bpp)
			bpp = mst_conn_state->fmt.bpp;
	}

	max_pclock = XDPTX_MAX_RATE(dp->link_config.max_rate,
				    dp->link_config.max_lanes, bpp);

	if (mode->clock > max_pclock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

/**
 * xlnx_dp_mst_connector_detect_ctx - detect connection status of an MST port
 * @connector: MST connector
 * @ctx:       modeset acquire context
 * @force:     whether to force a fresh detection
 *
 * Return: the drm_connector_status of the port.
 */
static int
xlnx_dp_mst_connector_detect_ctx(struct drm_connector *connector,
				 struct drm_modeset_acquire_ctx *ctx,
				 bool force)
{
	struct xlnx_dp_mst_connector *mst_conn =
		connector_to_mst_connector(connector);
	struct xlnx_dp *dp = mst_conn->dp;

	return drm_dp_mst_detect_port(connector, ctx, &dp->mst_mgr,
				      mst_conn->port);
}

/**
 * xlnx_dp_mst_connector_destroy - release an MST connector
 * @connector: MST connector
 */
static void xlnx_dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct xlnx_dp_mst_connector *mst_conn =
		connector_to_mst_connector(connector);

	drm_dp_mst_put_port_malloc(mst_conn->port);
	drm_connector_cleanup(connector);
	kfree(mst_conn);
}

/**
 * xlnx_dp_mst_conn_reset - reset the MST connector to a fresh atomic state
 * @connector: MST connector
 *
 * Allocates the subclassed connector state and seeds a default format
 * (RGB888/8bpc) so PBN/slot math is valid before the first framebuffer.
 */
static void xlnx_dp_mst_conn_reset(struct drm_connector *connector)
{
	struct xlnx_dp_mst_connector *mst_conn =
		connector_to_mst_connector(connector);
	struct xlnx_dp_mst_conn_state *mst_conn_state;

	if (connector->state)
		connector->funcs->atomic_destroy_state(connector,
						       connector->state);

	mst_conn_state = kzalloc(sizeof(*mst_conn_state), GFP_KERNEL);
	__drm_atomic_helper_connector_reset(connector,
					    mst_conn_state ?
					    &mst_conn_state->base : NULL);

	if (mst_conn_state && mst_conn->dp) {
		xlnx_dp_calc_color(mst_conn->dp, DRM_FORMAT_RGB888,
				   &mst_conn_state->fmt);
		xlnx_dp_calc_bpc(mst_conn->dp, 8, NULL, &mst_conn_state->fmt);
	}
}

/**
 * xlnx_dp_mst_conn_duplicate_state - copy the MST connector atomic state
 * @connector: MST connector
 *
 * Return: the duplicated state, or NULL on allocation failure.
 */
static struct drm_connector_state *
xlnx_dp_mst_conn_duplicate_state(struct drm_connector *connector)
{
	struct xlnx_dp_mst_conn_state *mst_conn_state;

	if (WARN_ON(!connector->state))
		return NULL;

	mst_conn_state = kmemdup(connector->state, sizeof(*mst_conn_state),
				 GFP_KERNEL);
	if (!mst_conn_state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector,
						      &mst_conn_state->base);

	return &mst_conn_state->base;
}

/**
 * xlnx_dp_mst_conn_destroy_state - free an MST connector atomic state
 * @connector: MST connector
 * @state:     state to free
 */
static void xlnx_dp_mst_conn_destroy_state(struct drm_connector *connector,
					   struct drm_connector_state *state)
{
	__drm_atomic_helper_connector_destroy_state(state);
	kfree(to_mst_conn_state(state));
}

static const struct drm_connector_funcs xlnx_dp_mst_connector_funcs = {
	.fill_modes          = drm_helper_probe_single_connector_modes,
	.destroy             = xlnx_dp_mst_connector_destroy,
	.reset               = xlnx_dp_mst_conn_reset,
	.atomic_duplicate_state = xlnx_dp_mst_conn_duplicate_state,
	.atomic_destroy_state   = xlnx_dp_mst_conn_destroy_state,
};

/**
 * xlnx_dp_mst_connector_atomic_check - release VC time slots on disable
 * @connector: MST connector being checked
 * @state:     global atomic state
 *
 * Return: 0 on success or when there is nothing to release, negative errno on
 * failure.
 */
static int xlnx_dp_mst_connector_atomic_check(struct drm_connector *connector,
					      struct drm_atomic_state *state)
{
	struct xlnx_dp_mst_connector *mst_conn =
		connector_to_mst_connector(connector);
	struct xlnx_dp *dp = mst_conn->dp;
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(state, connector);
	struct drm_connector_state *new_conn_state =
		drm_atomic_get_new_connector_state(state, connector);
	struct drm_dp_mst_topology_state *mst_state;
	int ret;

	if (!old_conn_state || !old_conn_state->crtc)
		return 0;
	if (new_conn_state && new_conn_state->crtc)
		return 0;

	mst_state = drm_atomic_get_mst_topology_state(state, &dp->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	ret = drm_dp_atomic_release_time_slots(state, &dp->mst_mgr,
					       mst_conn->port);
	if (ret < 0) {
		dev_err(dp->dev,
			"MST: connector atomic_check: release_time_slots failed (%d)\n",
			ret);
		return ret;
	}

	xlnx_dp_mst_dbg(dp,
			"MST: connector atomic_check: released time slots (disable)\n");

	return drm_dp_mst_atomic_check(state);
}

/**
 * xlnx_dp_mst_crtc_stream - DP TX stream a CRTC is wired to
 * @dp:   DisplayPort IP core structure
 * @crtc: CRTC feeding the DP TX
 *
 * Return: the index N of the DP input port@N whose remote port is @crtc's OF
 * port, or -ENODEV if @crtc is not wired to any stream.
 */
static int xlnx_dp_mst_crtc_stream(struct xlnx_dp *dp, struct drm_crtc *crtc)
{
	struct device_node *ep, *remote;
	bool match;
	int i;

	if (!crtc->port)
		return -ENODEV;

	for (i = 0; i < dp->config.num_streams; i++) {
		ep = of_graph_get_endpoint_by_regs(dp->dev->of_node, i, -1);
		if (!ep)
			continue;

		remote = of_graph_get_remote_port(ep);
		of_node_put(ep);
		match = remote == crtc->port;
		of_node_put(remote);
		if (match)
			return i;
	}

	return -ENODEV;
}

/**
 * xlnx_dp_mst_connector_atomic_best_encoder - pick the encoder for the CRTC
 * @connector: MST connector being modeset
 * @state:     global atomic state
 *
 * Every per-stream encoder is attached to every MST connector. The stream a
 * CRTC drives is fixed by the OF graph (the DP input port its output port is
 * wired to), so the encoder is picked from that wiring, not from CRTC order.
 *
 * Return: the matching encoder, or NULL if the connector has no CRTC or the
 * CRTC is not wired to a DP stream.
 */
static struct drm_encoder *
xlnx_dp_mst_connector_atomic_best_encoder(struct drm_connector *connector,
					  struct drm_atomic_state *state)
{
	struct xlnx_dp_mst_connector *mst_conn =
		connector_to_mst_connector(connector);
	struct xlnx_dp *dp = mst_conn->dp;
	struct drm_connector_state *conn_state =
		drm_atomic_get_new_connector_state(state, connector);
	int stream;

	if (!conn_state || !conn_state->crtc)
		return NULL;

	stream = xlnx_dp_mst_crtc_stream(dp, conn_state->crtc);
	if (stream < 0 || !dp->mst_encoders[stream]) {
		drm_dbg_kms(dp->drm, "MST: [CRTC:%d] is not wired to a DP stream\n",
			    conn_state->crtc->base.id);
		return NULL;
	}

	return &dp->mst_encoders[stream]->encoder;
}

static const struct drm_connector_helper_funcs xlnx_dp_mst_connector_helper_funcs = {
	.get_modes           = xlnx_dp_mst_connector_get_modes,
	.mode_valid          = xlnx_dp_mst_connector_mode_valid,
	.atomic_check        = xlnx_dp_mst_connector_atomic_check,
	.atomic_best_encoder = xlnx_dp_mst_connector_atomic_best_encoder,
	.detect_ctx          = xlnx_dp_mst_connector_detect_ctx,
};

/* ---- MST topology add_connector callback ---- */

/**
 * xlnx_dp_mst_add_connector - create a DRM connector for a new MST port
 * @mgr:      MST topology manager
 * @port:     the newly discovered MST port
 * @pathprop: path property string for the port
 *
 * Return: the new connector, or NULL on failure.
 */
static struct drm_connector *
xlnx_dp_mst_add_connector(struct drm_dp_mst_topology_mgr *mgr,
			  struct drm_dp_mst_port *port, const char *pathprop)
{
	struct xlnx_dp *dp = container_of(mgr, struct xlnx_dp, mst_mgr);
	struct xlnx_dp_mst_connector *mst_conn;
	struct drm_connector *connector;
	int i, ret;

	mst_conn = kzalloc(sizeof(*mst_conn), GFP_KERNEL);
	if (!mst_conn)
		return NULL;

	mst_conn->dp   = dp;
	mst_conn->port = port;
	drm_dp_mst_get_port_malloc(port);
	connector = &mst_conn->connector;

	ret = drm_connector_dynamic_init(dp->drm, connector,
					 &xlnx_dp_mst_connector_funcs,
					 DRM_MODE_CONNECTOR_DisplayPort, NULL);
	if (ret) {
		dev_err(dp->dev, "MST: connector_dynamic_init failed (%d)\n", ret);
		drm_dp_mst_put_port_malloc(port);
		kfree(mst_conn);
		return NULL;
	}

	drm_object_attach_property(&connector->base,
				   dp->drm->mode_config.path_property, 0);
	drm_connector_set_path_property(connector, pathprop);

	xlnx_dp_mst_conn_reset(connector);
	if (!connector->state) {
		dev_err(dp->dev, "MST: connector state allocation failed\n");
		drm_dp_mst_put_port_malloc(port);
		drm_property_replace_blob(&connector->path_blob_ptr, NULL);
		drm_connector_cleanup(connector);
		kfree(mst_conn);
		return NULL;
	}

	drm_connector_helper_add(connector, &xlnx_dp_mst_connector_helper_funcs);

	/*
	 * Attach every per-stream encoder to this connector. Encoders are a
	 * fixed pool (one per HW stream / CRTC); the specific one is selected at
	 * modeset time by ->atomic_best_encoder from the target CRTC. Decoupling
	 * connectors from encoders lets the full topology be enumerated (more
	 * connectors than streams) while at most num_streams displays are lit.
	 */
	for (i = 0; i < dp->config.num_streams; i++) {
		if (!dp->mst_encoders[i])
			continue;
		ret = drm_connector_attach_encoder(connector,
						   &dp->mst_encoders[i]->encoder);
		if (ret) {
			dev_err(dp->dev,
				"MST: attach encoder %d failed (%d)\n", i, ret);
			drm_dp_mst_put_port_malloc(port);
			drm_property_replace_blob(&connector->path_blob_ptr, NULL);
			drm_connector_cleanup(connector);
			kfree(mst_conn);
			return NULL;
		}
	}

	dev_info(dp->dev, "MST: connector '%s' created for path '%s'\n",
		 connector->name, pathprop ? pathprop : "(null)");

	return connector;
}

const struct drm_dp_mst_topology_cbs xlnx_dp_mst_cbs = {
	.add_connector = xlnx_dp_mst_add_connector,
	.poll_hpd_irq  = xlnx_dp_mst_poll_hpd_irq,
};

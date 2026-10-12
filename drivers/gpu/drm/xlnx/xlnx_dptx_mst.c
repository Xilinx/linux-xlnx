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

/* ---- MST stream attribute helper ---- */

/**
 * xlnx_dp_mst_avg_tu_1000 - average bytes-per-TU x 1000 for one MST stream
 * @dp:        DisplayPort IP core structure
 * @clock_khz: pixel clock of the stream in kHz
 * @bpp:       bits-per-pixel of the stream (8b/10b path)
 * @pbn:       payload bandwidth in PBN units (UHBR path)
 * @pbn_div:   link bandwidth in PBN-per-slot, fixed20_12 (UHBR path)
 *
 * For 8b/10b: floor(clock x bpp x 64 / (lanes x link_rate)) x 125.
 * For 128b/132b (UHBR): pbn x 4096000 / pbn_div (per-MTP slot fraction).
 *
 * Return: average bytes per TU (8b/10b) or per-MTP slot fraction (128b/132b),
 * scaled by 1000; 0 if the lane count, link rate or @pbn_div is unavailable.
 */
static u64 xlnx_dp_mst_avg_tu_1000(struct xlnx_dp *dp, int clock_khz, u32 bpp,
				   int pbn, fixed20_12 pbn_div)
{
	u32 link_rate = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	u32 lanes = dp->mode.lane_cnt;

	if (drm_dp_is_uhbr_rate(link_rate)) {
		if (!pbn_div.full)
			return 0;
		return div64_u64((u64)pbn * 4096000ULL, pbn_div.full);
	}

	if (!lanes || !link_rate)
		return 0;

	return div64_u64((u64)clock_khz * bpp * 64ULL,
			 (u64)lanes * link_rate) * 125;
}

/**
 * xlnx_dp_mst_set_stream_attributes - Program HW registers for one MST stream
 * @dp:         DP controller
 * @stream_id:  0-based stream index (0..XDPTX_MAX_STREAMS-1)
 * @mode:       Display mode for this stream
 * @time_slots: Number of VC payload time slots allocated
 * @stream_fmt:  per-stream pixel format (from the connector atomic state)
 * @pbn:         allocated PBN for this stream (128b/132b UHBR path)
 * @pbn_div:     link bandwidth in PBN-per-slot, fixed20_12 (UHBR path)
 *
 * Programs the per-stream MSA timing registers, TU size, average-bytes-per-TU,
 * and the STREAM1..4 TS index registers. InitWait is forced to 0 for MST.
 */
static void xlnx_dp_mst_set_stream_attributes(struct xlnx_dp *dp,
					      int stream_id,
					      const struct drm_display_mode *mode,
					      int time_slots,
					      const struct xlnx_dp_stream_fmt *stream_fmt,
					      int pbn, fixed20_12 pbn_div)
{
	static const u32 msa_offsets[XDPTX_MAX_STREAMS] = {
		0,
		XDPTX_MST_MSA_OFFSET_S2,
		XDPTX_MST_MSA_OFFSET_S3,
		XDPTX_MST_MSA_OFFSET_S4,
	};
	u32 off;
	u64 avg_tu_1000;
	u32 ts_int, ts_frac;
	u32 bpp, user_calc_ppc;

	if (stream_id < 0 || stream_id >= XDPTX_MAX_STREAMS)
		return;

	off = msa_offsets[stream_id];
	bpp = stream_fmt ? stream_fmt->bpp : dp->fmt.bpp;
	user_calc_ppc = xlnx_dp_calc_user_ppc(dp, mode->clock, dp->mode.lane_cnt);

	if (stream_fmt)
		xlnx_dp_mst_dbg(dp,
				"MST: stream %d format misc0=0x%02x bpc=%u bpp=%u\n",
				stream_id, stream_fmt->misc0, stream_fmt->bpc,
				stream_fmt->bpp);

	avg_tu_1000 = xlnx_dp_mst_avg_tu_1000(dp, mode->clock, bpp, pbn, pbn_div);
	ts_int  = (u32)(avg_tu_1000 / 1000);
	ts_frac = (u32)(avg_tu_1000 % 1000);

	xlnx_dp_mst_dbg(dp,
			"MST: stream%d TS: slots=%d avg_tu_1000=%llu ts_int=%u ts_frac=%u ts_reg=0x%08x\n",
			stream_id, time_slots, avg_tu_1000, ts_int, ts_frac,
			(ts_int << 16) | ts_frac);
	xlnx_dp_mst_dbg(dp,
			"MST: stream%d mode: %dx%d clock=%d htotal=%d vtotal=%d msa_off=0x%x\n",
			stream_id, mode->hdisplay, mode->vdisplay, mode->clock,
			mode->htotal, mode->vtotal, off);

	if (dp->per_stream_vid_clk || dp->mst_stream_count == 0) {
		struct clk *vclk = dp->per_stream_vid_clk ?
				   dp->tx_vid_clk[stream_id] :
				   dp->tx_vid_clk[0];
		unsigned long pixel_hz = (unsigned long)mode->clock * 1000;
		unsigned long vid_clk_hz = pixel_hz / user_calc_ppc;

		if (vclk) {
			int cret = clk_set_rate(vclk, vid_clk_hz);

			if (cret) {
				dev_err(dp->dev,
					"MST: stream%d tx_vid_clk set_rate(%lu Hz) failed (%d)\n",
					stream_id, vid_clk_hz, cret);
			} else {
				usleep_range(2000, 4000);
				xlnx_dp_mst_dbg(dp,
						"MST: stream%d tx_vid_clk set to %lu Hz (pixel=%lu ppc=%u measured=%lu)\n",
						stream_id, vid_clk_hz, pixel_hz,
						user_calc_ppc, clk_get_rate(vclk));
			}
		}
	}

	xlnx_dp_mode_set_stream_offset(dp, mode, off, stream_fmt);

	xlnx_dp_mst_write(dp, XDPTX_TRANSFER_UNITSIZE_REG + off,
			  XDPTX_DEF_TRANSFER_UNITSIZE);

	xlnx_dp_mst_write(dp, XDPTX_MINBYTES_PERTU_REG + off, ts_int);

	xlnx_dp_mst_write(dp, XDPTX_FRACBYTES_PERTU_REG + off,
			  (ts_frac * 1024) / 1000);

	xlnx_dp_mst_write(dp, XDPTX_INIT_WAIT_REG + off, 0);

	xlnx_dp_mst_write(dp,
			  XDPTX_STREAM1_TS_REG + stream_id * XDPTX_STREAM_TS_STRIDE,
			  (ts_int << 16) | ts_frac);

	xlnx_dp_vtc_set_timing(dp, mode, dp->vtc_off[stream_id], user_calc_ppc);

	xlnx_dp_mst_set(dp, dp->vtc_off[stream_id] + XDPTX_VTC_CTL,
			XDPTX_VTC_CTL_GE);
}

/**
 * xlnx_dp_mst_relabel_vcpi - force a payload's VCPI to match its stream index
 * @dp:        DisplayPort IP core structure
 * @mst_state: MST topology state holding the payload
 * @port:      MST port whose payload to relabel
 * @stream_id: stream index the payload must map to
 */
static void xlnx_dp_mst_relabel_vcpi(struct xlnx_dp *dp,
				     struct drm_dp_mst_topology_state *mst_state,
				     struct drm_dp_mst_port *port,
				     int stream_id)
{
	struct drm_dp_mst_atomic_payload *p =
		drm_atomic_get_mst_payload_state(mst_state, port);
	struct drm_dp_mst_atomic_payload *pos;
	u32 want_vcpi = stream_id + 1;

	/*
	 * Xilinx HW pins each VC payload to a fixed HW stream, so the VCPI
	 * must equal stream_id + 1. Unique stream_ids mean streams never
	 * collide on a VCPI once every payload is relabelled.
	 */
	if (p && p->vcpi != want_vcpi) {
		xlnx_dp_mst_dbg(dp,
				"MST: relabel vcpi %d -> %d (stream_id=%d), mask 0x%x\n",
				p->vcpi, want_vcpi, stream_id, mst_state->payload_mask);
		p->vcpi = want_vcpi;
	}

	/* Rebuild: a payload not yet relabelled may still hold this VCPI. */
	mst_state->payload_mask = 0;
	list_for_each_entry(pos, &mst_state->payloads, next)
		if (pos->vcpi && !pos->delete)
			mst_state->payload_mask |= BIT(pos->vcpi - 1);
}

/**
 * xlnx_dp_mst_compute_fmt - derive per-stream pixel format from a framebuffer
 * @dp:  DisplayPort IP core structure
 * @fb:  framebuffer whose fourcc drives the format (may be NULL)
 * @info: display info of this stream's downstream sink
 * @stream_fmt:  per-stream format state to fill
 *
 * Fills @stream_fmt with the misc0/bpc/bpp for this stream using the pure
 * xlnx_dp_calc_*() helpers. Called from atomic_check so the enable path only
 * consumes immutable per-stream state.
 */
static void xlnx_dp_mst_compute_fmt(struct xlnx_dp *dp,
				    struct drm_framebuffer *fb,
				    const struct drm_display_info *info,
				    struct xlnx_dp_stream_fmt *stream_fmt)
{
	u32 fourcc;
	u8 bpc;

	if (!fb)
		return;

	fourcc = fb->format->format;
	/* DT ceiling and sink limit are applied by xlnx_dp_calc_bpc() */
	bpc = xlnx_dp_bpc_from_fourcc(dp, fourcc);

	stream_fmt->misc0 = 0;
	stream_fmt->num_colors = 0;
	xlnx_dp_calc_color(dp, fourcc, stream_fmt);
	xlnx_dp_calc_bpc(dp, bpc, info, stream_fmt);
}

/**
 * xlnx_dp_mst_encoder_atomic_check - validate and reserve VC slots for a stream
 * @encoder:    MST stream encoder
 * @crtc_state: new CRTC state for the stream
 * @conn_state: new connector state for the stream
 *
 * Return: 0 on success, negative errno on failure.
 */
static int xlnx_dp_mst_encoder_atomic_check(struct drm_encoder *encoder,
					    struct drm_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state)
{
	struct xlnx_dp_mst_encoder *mst_enc = encoder_to_mst_encoder(encoder);
	struct xlnx_dp *dp = mst_enc->dp;
	struct xlnx_dp_mst_connector *mst_conn =
		connector_to_mst_connector(conn_state->connector);
	struct xlnx_dp_mst_conn_state *mst_conn_state = to_mst_conn_state(conn_state);
	struct drm_atomic_state *state = conn_state->state;
	struct drm_plane *primary = crtc_state->crtc->primary;
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, primary);
	struct drm_framebuffer *fb = plane_state ? plane_state->fb :
		(primary->state ? primary->state->fb : NULL);
	struct drm_dp_mst_topology_state *mst_state;
	int pbn, slots, ret;
	bool uhbr = xlnx_dp_uhbr_active(dp);

	xlnx_dp_mst_compute_fmt(dp, fb, &conn_state->connector->display_info,
				&mst_conn_state->fmt);

	mst_state = drm_atomic_get_mst_topology_state(state, &dp->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	mst_state->pbn_div =
		drm_dp_get_vc_payload_bw(drm_dp_bw_code_to_link_rate(dp->mode.bw_code),
					 dp->mode.lane_cnt);

	if (uhbr)
		drm_dp_mst_update_slots(mst_state, DP_CAP_ANSI_128B132B);
	else
		drm_dp_mst_update_slots(mst_state, DP_CAP_ANSI_8B10B);

	pbn = drm_dp_calc_pbn_mode(crtc_state->adjusted_mode.clock,
				   mst_conn_state->fmt.bpp << 4);

	/*
	 * 8b/10b ties the slot count to the HW TU size; 128b/132b allocates slots
	 * straight from the natural pbn, so skip the bump for UHBR.
	 */
	if (!uhbr) {
		u64 avg_tu_1000 =
			xlnx_dp_mst_avg_tu_1000(dp,
						crtc_state->adjusted_mode.clock,
						mst_conn_state->fmt.bpp,
						pbn, mst_state->pbn_div);
		u32 ts_int  = (u32)(avg_tu_1000 / 1000);
		u32 ts_frac = (u32)(avg_tu_1000 % 1000);
		u32 tu_size = ts_int + (ts_frac ? 1 : 0);
		int pbn_div_int;

		if (dp->mode.lane_cnt == 4 && tu_size % 4 != 0)
			tu_size += 4 - tu_size % 4;
		else if (tu_size % 2 != 0)
			tu_size++;

		pbn_div_int = dfixed_trunc(mst_state->pbn_div);
		if (pbn_div_int > 0) {
			int min_pbn = (int)tu_size * pbn_div_int;

			if (pbn < min_pbn) {
				xlnx_dp_mst_dbg(dp,
						"MST: atomic_check: bumping pbn %d -> %d (tu_size=%u, pbn_div=%d)\n",
						pbn, min_pbn, tu_size, pbn_div_int);
				pbn = min_pbn;
			}
		}
	}

	slots = drm_dp_atomic_find_time_slots(state, &dp->mst_mgr,
					      mst_conn->port, pbn);
	if (slots < 0) {
		dev_err(dp->dev,
			"MST: atomic_find_time_slots failed (%d)\n", slots);
		return slots;
	}

	xlnx_dp_mst_dbg(dp,
			"MST-DIAG: clk=%d bpp=%u pbn=%d slots=%d pbn_div=%d\n",
			crtc_state->adjusted_mode.clock, mst_conn_state->fmt.bpp, pbn, slots,
			dfixed_trunc(mst_state->pbn_div));

	ret = drm_dp_mst_atomic_check(state);
	if (ret)
		return ret;

	xlnx_dp_mst_relabel_vcpi(dp, mst_state, mst_conn->port,
				 mst_enc->stream_id);

	return 0;
}

/**
 * xlnx_dp_mst_wait_for_act_handled - wait for the sink to confirm the ACT
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 once ACT_HANDLED is set, or -ETIMEDOUT on timeout.
 */
static int xlnx_dp_mst_wait_for_act_handled(struct xlnx_dp *dp)
{
	int ret, err;
	u8 status;

	/* 3s bound mirrors the DRM core's own ACT timeout (drm_dp_check_act_status). */
	err = read_poll_timeout(drm_dp_dpcd_readb, ret,
				ret == 1 && (status & DP_PAYLOAD_ACT_HANDLED),
				5000, XDPTX_ACT_POLL_MS * 1000, false,
				&dp->aux, DP_PAYLOAD_TABLE_UPDATE_STATUS,
				&status);

	return err;
}

/**
 * xlnx_dp_mst_relink_if_degraded - retrain the main link if channel-EQ degraded
 * @dp: DisplayPort IP core structure
 *
 * Retrains only when clock recovery is still locked but channel equalization
 * has been lost, and only when no stream is active.
 */
static void xlnx_dp_mst_relink_if_degraded(struct xlnx_dp *dp)
{
	u8 link_status[DP_LINK_STATUS_SIZE] = { 0 };
	u8 lane_cnt = dp->mode.lane_cnt;
	bool cr_ok, eq_ok;
	int ret;

	if (!lane_cnt)
		return;

	ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
	if (ret < 0)
		return;

	cr_ok = drm_dp_clock_recovery_ok(link_status, lane_cnt);
	eq_ok = drm_dp_channel_eq_ok(link_status, lane_cnt);

	if (eq_ok || !cr_ok)
		return;

	dev_err(dp->dev,
		"MST: main link EQ degraded while idle (CR locked, EQ lost) - retraining before modeset\n");

	dp->mst_enabling = true;
	if (xlnx_dp_start(dp) >= 0)
		xlnx_dp_mst_enable(dp);
	dp->mst_enabling = false;
}

/**
 * xlnx_dp_mst_soft_reset - pulse the video-stream soft reset
 * @dp:          DisplayPort IP core structure
 * @stream_mask: bitmask of streams to reset
 */
static void xlnx_dp_mst_soft_reset(struct xlnx_dp *dp, u32 stream_mask)
{
	xlnx_dp_mst_write(dp, XDPTX_SOFT_RST,
			  stream_mask & XDPTX_SOFT_RST_VIDEO_STREAM_ALL_MASK);
	usleep_range(10, 20);
	xlnx_dp_mst_write(dp, XDPTX_SOFT_RST, 0x0);
}

/**
 * xlnx_dp_mst_clear_chbuf_intr - clear the channel-buffer IRQ mask bits
 * @dp: DisplayPort IP core structure
 */
static void xlnx_dp_mst_clear_chbuf_intr(struct xlnx_dp *dp)
{
	u32 m;

	(void)xlnx_dp_read(dp->dp_base, XDPTX_INTR_STATUS_REG);
	m = xlnx_dp_read(dp->dp_base, XDPTX_INTR_MASK_REG);
	m &= ~(XDPTX_INTR_CHBUFUNDFW_MASK | XDPTX_INTR_CHBUFOVFW_MASK);
	xlnx_dp_write(dp->dp_base, XDPTX_INTR_MASK_REG, m);
}

/**
 * xlnx_dp_mst_write_vc_table - populate the HW VC payload buffer from the state
 * @dp:        DisplayPort IP core structure
 * @mst_state: MST topology state holding the allocated payloads
 */
static void xlnx_dp_mst_write_vc_table(struct xlnx_dp *dp,
				       struct drm_dp_mst_topology_state *mst_state)
{
	struct drm_dp_mst_atomic_payload *rp;
	int s;

	list_for_each_entry(rp, &mst_state->payloads, next) {
		if (rp->payload_allocation_status ==
		    DRM_DP_MST_PAYLOAD_ALLOCATION_NONE)
			continue;
		xlnx_dp_mst_dbg(dp,
				"MST: VCTABLE vcpi=%d start_slot=%d time_slots=%d\n",
				rp->vcpi, rp->vc_start_slot, rp->time_slots);
		for (s = rp->vc_start_slot;
		     s < rp->vc_start_slot + rp->time_slots; s++)
			xlnx_dp_mst_write(dp, XDPTX_VC_PAYLOAD_BUFF + s * 4,
					  rp->vcpi);
	}
}

/**
 * xlnx_dp_mst_commit_and_act - commit the payload table and run the ACT
 * @dp:        DisplayPort IP core structure
 * @mgr:       MST topology manager
 * @mst_state: MST topology state holding the allocated payloads
 * @payload:   payload for the stream that was just enabled
 *
 * Writes the VC payload table, triggers the ACT for this stream, and completes
 * payload part 2 for @payload.
 */
static void xlnx_dp_mst_commit_and_act(struct xlnx_dp *dp,
				       struct drm_dp_mst_topology_mgr *mgr,
				       struct drm_dp_mst_topology_state *mst_state,
				       struct drm_dp_mst_atomic_payload *payload)
{
	int ret;

	usleep_range(4000, 6000);

	xlnx_dp_mst_write_vc_table(dp, mst_state);

	xlnx_dp_mst_commit_vcp(dp);
	ret = xlnx_dp_mst_wait_for_act_handled(dp);
	if (ret)
		dev_err(dp->dev, "MST: ACT timeout (%d) - continuing\n", ret);

	ret = drm_dp_add_payload_part2(mgr, payload);
	if (ret)
		dev_err(dp->dev,
			"MST: add_payload_part2 failed (vcpi=%d, %d)\n",
			payload->vcpi, ret);
}

/**
 * xlnx_dp_mst_warm_reenable - retrain an idle but already-trained link
 * @dp: DisplayPort IP core structure
 */
static void xlnx_dp_mst_warm_reenable(struct xlnx_dp *dp)
{
	xlnx_dp_mst_dbg(dp,
			"MST: warm re-enable on already-trained link - retraining (SST-framed) so the framer re-latches the new MSA\n");
	drm_dp_dpcd_writeb(&dp->aux, DP_MSTM_CTRL, 0);
	xlnx_dp_write(dp->dp_base, XDPTX_MST_CONFIG, 0x0);
	dp->mst_enabling = true;
	if (xlnx_dp_start(dp) >= 0)
		xlnx_dp_mst_enable(dp);
	dp->mst_enabling = false;
	usleep_range(10000, 11000);

	xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_ENABLE_REG, 0x0);

	xlnx_dp_mst_clear_payload_table(dp);
}

/**
 * xlnx_dp_mst_encoder_atomic_enable - enable one MST video stream
 * @encoder: MST stream encoder
 * @state:   global atomic state
 */
static void xlnx_dp_mst_encoder_atomic_enable(struct drm_encoder *encoder,
					      struct drm_atomic_state *state)
{
	struct xlnx_dp_mst_encoder *mst_enc = encoder_to_mst_encoder(encoder);
	struct xlnx_dp *dp = mst_enc->dp;
	struct drm_dp_mst_topology_mgr *mgr = &dp->mst_mgr;
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;
	struct xlnx_dp_mst_connector *mst_conn = NULL;
	const struct drm_display_mode *mode = NULL;
	int i, ret, slot;

	for_each_new_connector_in_state(state, connector, new_conn_state, i) {
		if (new_conn_state->best_encoder == encoder) {
			struct drm_crtc_state *crtc_state =
				drm_atomic_get_new_crtc_state(state,
							      new_conn_state->crtc);

			mst_conn = connector_to_mst_connector(connector);
			if (crtc_state)
				mode = &crtc_state->adjusted_mode;
			break;
		}
	}
	if (!mst_conn || !mode) {
		dev_err(dp->dev, "MST: enable: could not find connector\n");
		return;
	}

	mst_state = drm_atomic_get_new_mst_topology_state(state, mgr);
	if (!mst_state) {
		dev_err(dp->dev, "MST: enable: no mst topology state\n");
		return;
	}

	payload = drm_atomic_get_mst_payload_state(mst_state, mst_conn->port);
	if (!payload) {
		dev_err(dp->dev, "MST: enable: no payload for port\n");
		return;
	}

	if (!dp->mst_link_trained) {
		dev_err(dp->dev,
			"MST: link not trained at atomic_enable -- hpd_work may not have run yet; skipping stream %d\n",
			mst_enc->stream_id);
		return;
	}

	if (dp->mst_stream_count == 0) {
		if (dp->mst_fresh_train) {
			xlnx_dp_mst_relink_if_degraded(dp);
			dp->mst_fresh_train = false;
		} else {
			xlnx_dp_mst_warm_reenable(dp);
			xlnx_dp_write(dp->dp_base, XDPTX_SCRAMBLER_RESET,
				      XDPTX_SCRAMBLER_RESET_MASK);
			xlnx_dp_write(dp->dp_base, XDPTX_MAINSTRM_ENABLE_REG,
				      XDPTX_MAINSTRM_ENABLE_MASK);
		}
	}

	xlnx_dp_mst_set_stream_attributes(dp, mst_enc->stream_id, mode,
					  payload->time_slots,
					  &to_mst_conn_state(new_conn_state)->fmt,
					  payload->pbn, mst_state->pbn_div);

	/* HW requires VCPI == stream_id + 1 (fixed payload-to-stream map). */
	if (payload->vcpi != mst_enc->stream_id + 1) {
		xlnx_dp_mst_dbg(dp,
				"MST: vcpi=%d != stream_id+1=%d at enable - forcing\n",
				payload->vcpi, mst_enc->stream_id + 1);
		payload->vcpi = mst_enc->stream_id + 1;
	}

	if (payload->payload_allocation_status ==
	    DRM_DP_MST_PAYLOAD_ALLOCATION_NONE) {
		ret = drm_dp_add_payload_part1(mgr, mst_state, payload);
		if (ret) {
			dev_err(dp->dev,
				"MST: add_payload_part1 failed (%d)\n", ret);
			return;
		}
	}

	xlnx_dp_mst_dbg(dp,
			"MST: VCSLOT[s%d] vc_start_slot=%d time_slots=%d vcpi=%d\n",
			mst_enc->stream_id, payload->vc_start_slot,
			payload->time_slots, payload->vcpi);
	for (slot = payload->vc_start_slot;
	     slot < payload->vc_start_slot + payload->time_slots;
	     slot++) {
		xlnx_dp_mst_write(dp,
				  XDPTX_VC_PAYLOAD_BUFF + slot * 4,
				  payload->vcpi);
	}

	xlnx_dp_mst_soft_reset(dp, BIT(mst_enc->stream_id));

	usleep_range(4000, 6000);

	dp->mst_active_mask |= BIT(mst_enc->stream_id);
	dp->mst_stream_count++;
	dev_info(dp->dev, "MST: stream %d enabled (active streams=%d)\n",
		 mst_enc->stream_id, dp->mst_stream_count);

	xlnx_dp_mst_commit_and_act(dp, mgr, mst_state, payload);

	/*
	 * Re-enable the channel-buffer under/overflow interrupt only after the
	 * stream is scanning out (MAINSTRM_ENABLE + the ACT epoch have settled).
	 * While the frmbuf DMA feeds a stream whose video timing is not yet
	 * running, the channel buffer can transiently under/overflow; deferring
	 * the unmask avoids servicing those spurious events during bring-up.
	 */
	xlnx_dp_mst_clear_chbuf_intr(dp);
}

/**
 * xlnx_dp_mst_encoder_atomic_disable - disable one MST video stream
 * @encoder: MST stream encoder
 * @state:   global atomic state
 */
static void xlnx_dp_mst_encoder_atomic_disable(struct drm_encoder *encoder,
					       struct drm_atomic_state *state)
{
	struct xlnx_dp_mst_encoder *mst_enc = encoder_to_mst_encoder(encoder);
	struct xlnx_dp *dp = mst_enc->dp;
	struct drm_dp_mst_topology_mgr *mgr = &dp->mst_mgr;
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state;
	struct drm_dp_mst_topology_state *old_mst_state, *new_mst_state;
	struct drm_dp_mst_atomic_payload *old_payload, *new_payload;
	struct xlnx_dp_mst_connector *mst_conn = NULL;
	bool was_active;
	int i, ret, slot, new_vcpi;

	for_each_old_connector_in_state(state, connector, old_conn_state, i) {
		if (old_conn_state->best_encoder == encoder) {
			mst_conn = connector_to_mst_connector(connector);
			break;
		}
	}
	if (!mst_conn) {
		dev_err(dp->dev, "MST: disable: could not find connector\n");
		return;
	}

	old_mst_state = drm_atomic_get_old_mst_topology_state(state, mgr);
	new_mst_state = drm_atomic_get_new_mst_topology_state(state, mgr);
	if (!old_mst_state || !new_mst_state) {
		dev_err(dp->dev,
			"MST: disable stream %d: no MST topology state - skipping\n",
			mst_enc->stream_id);
		return;
	}

	old_payload = drm_atomic_get_mst_payload_state(old_mst_state, mst_conn->port);
	new_payload = drm_atomic_get_mst_payload_state(new_mst_state, mst_conn->port);
	if (!old_payload || !new_payload) {
		dev_err(dp->dev, "MST: disable: no payload for port\n");
		return;
	}

	was_active = dp->mst_active_mask & BIT(mst_enc->stream_id);
	dp->mst_active_mask &= ~BIT(mst_enc->stream_id);

	xlnx_dp_mst_dbg(dp,
			"MST: disable: pre-remove old(vcs=%d,ts=%d,st=%d) new(vcs=%d,ts=%d,st=%d) port=%p primary_present=%d\n",
			old_payload->vc_start_slot, old_payload->time_slots,
			old_payload->payload_allocation_status,
			new_payload->vc_start_slot, new_payload->time_slots,
			new_payload->payload_allocation_status,
			mst_conn->port, mgr->mst_primary ? 1 : 0);

	/* A CRTC move relabels new_payload; remove the VC under its allocated ID. */
	new_vcpi = new_payload->vcpi;
	new_payload->vcpi = old_payload->vcpi;
	drm_dp_remove_payload_part1(mgr, new_mst_state, new_payload);
	xlnx_dp_mst_dbg(dp,
			"MST: disable: after part1 new(vcs=%d,st=%d)\n",
			new_payload->vc_start_slot,
			new_payload->payload_allocation_status);

	drm_dp_remove_payload_part2(mgr, new_mst_state, old_payload, new_payload);
	new_payload->vcpi = new_vcpi;

	/* 128b/132b allocates from slot 0, so clear it too. */
	for (slot = 0; slot < XDPTX_VC_PAYLOAD_BUFF_NSLOTS; slot++)
		xlnx_dp_mst_write(dp, XDPTX_VC_PAYLOAD_BUFF + slot * 4, 0);

	if (!dp->mst_active_mask) {
		ret = drm_dp_dpcd_clear_payload(&dp->aux);
		if (ret < 0)
			dev_err(dp->dev,
				"MST: disable: clear payload table failed (%d)\n",
				ret);
		else
			xlnx_dp_mst_dbg(dp,
					"MST: disable: cleared full hub payload table (last stream)\n");
	} else {
		struct drm_dp_mst_atomic_payload *rp;
		int s;

		list_for_each_entry(rp, &new_mst_state->payloads, next) {
			if (rp->vc_start_slot < 0 || rp->time_slots <= 0)
				continue;

			for (s = rp->vc_start_slot;
			     s < rp->vc_start_slot + rp->time_slots; s++)
				xlnx_dp_mst_write(dp,
						  XDPTX_VC_PAYLOAD_BUFF + s * 4,
						  rp->vcpi);

			xlnx_dp_mst_dbg(dp,
					"MST: disable: survivor vcpi=%d compacted to slots %d..%d\n",
					rp->vcpi, rp->vc_start_slot,
					rp->vc_start_slot + rp->time_slots - 1);
		}
	}

	xlnx_dp_mst_write(dp, XDPTX_MST_CONFIG,
			  XDPTX_MST_CONFIG_MST_EN_MASK |
			  XDPTX_MST_CONFIG_VCP_UPDATED_MASK);

	if (dp->mst_active_mask) {
		ret = xlnx_dp_mst_wait_for_act_handled(dp);
		if (ret)
			dev_err(dp->dev, "MST: disable ACT timeout (%d)\n",
				ret);
		else
			xlnx_dp_mst_dbg(dp,
					"MST: disable: post-removal table committed (active_mask=0x%x)\n",
					dp->mst_active_mask);
	} else {
		xlnx_dp_mst_dbg(dp,
				"MST: disable: last stream removed, empty table latched (ACT poll skipped)\n");
	}

	if (!dp->mst_active_mask) {
		xlnx_dp_mst_write(dp, XDPTX_MAINSTRM_ENABLE_REG, 0x0);
		xlnx_dp_mst_write(dp, XDPTX_ENABLE_SEC_STREAM_REG, 0x0);
		for (i = 0; i < dp->config.num_streams; i++)
			xlnx_dp_clr(dp->dp_base,
				    dp->vtc_off[i] + XDPTX_VTC_CTL,
				    XDPTX_VTC_CTL_GE);
		xlnx_dp_mst_dbg(dp,
				"MST: disable: last stream removed, main stream blanked after commit\n");

		dp->mst_to_sst_relink = true;
	}

	/* A stream whose enable failed was never counted. */
	if (was_active && dp->mst_stream_count > 0)
		dp->mst_stream_count--;

	dev_info(dp->dev, "MST: stream %d disabled (active streams=%d)\n",
		 mst_enc->stream_id, dp->mst_stream_count);
}

static const struct drm_encoder_helper_funcs xlnx_dp_mst_encoder_helper_funcs = {
	.atomic_check   = xlnx_dp_mst_encoder_atomic_check,
	.atomic_enable  = xlnx_dp_mst_encoder_atomic_enable,
	.atomic_disable = xlnx_dp_mst_encoder_atomic_disable,
};

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

	mst_conn_state = kzalloc(sizeof(*mst_conn_state), GFP_KERNEL);
	if (!mst_conn_state)
		return;

	if (connector->state)
		connector->funcs->atomic_destroy_state(connector,
						       connector->state);

	__drm_atomic_helper_connector_reset(connector, &mst_conn_state->base);

	if (mst_conn->dp) {
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

/* ---- MST encoder creation ---- */

/**
 * xlnx_dp_mst_create_encoders - allocate one DRM encoder per MST stream
 * @dp: DisplayPort IP core structure
 *
 * The encoders are DRM-managed and released with the DRM device.
 *
 * Return: 0 on success, negative errno on failure.
 */
int xlnx_dp_mst_create_encoders(struct xlnx_dp *dp)
{
	int i;

	/* Every stream's DP input port must be wired to an available source. */
	for (i = 0; i < dp->config.num_streams; i++) {
		struct device_node *ep, *remote;
		bool ok;

		ep = of_graph_get_endpoint_by_regs(dp->dev->of_node, i, -1);
		remote = ep ? of_graph_get_remote_port_parent(ep) : NULL;
		of_node_put(ep);
		ok = remote && of_device_is_available(remote);
		of_node_put(remote);
		if (!ok) {
			dev_err(dp->dev,
				"MST: port@%d has no available video source (xlnx,num-streams = %u)\n",
				i, dp->config.num_streams);
			return -EINVAL;
		}
	}

	for (i = 0; i < dp->config.num_streams; i++) {
		struct xlnx_dp_mst_encoder *mst_enc;

		mst_enc = drmm_encoder_alloc(dp->drm, struct xlnx_dp_mst_encoder,
					     encoder, NULL,
					     DRM_MODE_ENCODER_DPMST,
					     "DP-MST-Stream%d", i);
		if (IS_ERR(mst_enc)) {
			dev_err(dp->dev, "MST: encoder %d init failed (%ld)\n",
				i, PTR_ERR(mst_enc));
			return PTR_ERR(mst_enc);
		}

		mst_enc->dp = dp;
		mst_enc->stream_id = i;
		/* The wired stream CRTC is picked in atomic_best_encoder(). */
		mst_enc->encoder.possible_crtcs =
			GENMASK(dp->config.num_streams - 1, 0);
		drm_encoder_helper_add(&mst_enc->encoder,
				       &xlnx_dp_mst_encoder_helper_funcs);
		dp->mst_encoders[i] = mst_enc;

		xlnx_dp_mst_dbg(dp, "MST: encoder %d created\n", i);
	}

	return 0;
}

const struct drm_dp_mst_topology_cbs xlnx_dp_mst_cbs = {
	.add_connector = xlnx_dp_mst_add_connector,
	.poll_hpd_irq  = xlnx_dp_mst_poll_hpd_irq,
};

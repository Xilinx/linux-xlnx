/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Jesse Barnes <jbarnes@virtuousgeek.org>
 *
 * New plane/sprite handling.
 *
 * The older chips had a separate interface for programming plane related
 * registers; newer ones are much simpler and we can use the new DRM plane
 * support.
 */
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>
#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include "intel_drv.h"
#include "intel_frontbuffer.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

static bool
format_is_yuv(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_YVYU:
		return true;
	default:
		return false;
	}
}

int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode,
			     int usecs)
{
	/* paranoia */
	if (!adjusted_mode->crtc_htotal)
		return 1;

	return DIV_ROUND_UP(usecs * adjusted_mode->crtc_clock,
			    1000 * adjusted_mode->crtc_htotal);
}

/**
 * intel_pipe_update_start() - start update of a set of display registers
 * @crtc: the crtc of which the registers are going to be updated
 * @start_vbl_count: vblank counter return pointer used for error checking
 *
 * Mark the start of an update to pipe registers that should be updated
 * atomically regarding vblank. If the next vblank will happens within
 * the next 100 us, this function waits until the vblank passes.
 *
 * After a successful call to this function, interrupts will be disabled
 * until a subsequent call to intel_pipe_update_end(). That is done to
 * avoid random delays. The value written to @start_vbl_count should be
 * supplied to intel_pipe_update_end() for error checking.
 */
void intel_pipe_update_start(struct intel_crtc *crtc)
{
	const struct drm_display_mode *adjusted_mode = &crtc->config->base.adjusted_mode;
	long timeout = msecs_to_jiffies_timeout(1);
	int scanline, min, max, vblank_start;
	wait_queue_head_t *wq = drm_crtc_vblank_waitqueue(&crtc->base);
	DEFINE_WAIT(wait);

	vblank_start = adjusted_mode->crtc_vblank_start;
	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		vblank_start = DIV_ROUND_UP(vblank_start, 2);

	/* FIXME needs to be calibrated sensibly */
	min = vblank_start - intel_usecs_to_scanlines(adjusted_mode, 100);
	max = vblank_start - 1;

	local_irq_disable();

	if (min <= 0 || max <= 0)
		return;

	if (WARN_ON(drm_crtc_vblank_get(&crtc->base)))
		return;

	crtc->debug.min_vbl = min;
	crtc->debug.max_vbl = max;
	trace_i915_pipe_update_start(crtc);

	for (;;) {
		/*
		 * prepare_to_wait() has a memory barrier, which guarantees
		 * other CPUs can see the task state update by the time we
		 * read the scanline.
		 */
		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);

		scanline = intel_get_crtc_scanline(crtc);
		if (scanline < min || scanline > max)
			break;

		if (timeout <= 0) {
			DRM_ERROR("Potential atomic update failure on pipe %c\n",
				  pipe_name(crtc->pipe));
			break;
		}

		local_irq_enable();

		timeout = schedule_timeout(timeout);

		local_irq_disable();
	}

	finish_wait(wq, &wait);

	drm_crtc_vblank_put(&crtc->base);

	crtc->debug.scanline_start = scanline;
	crtc->debug.start_vbl_time = ktime_get();
	crtc->debug.start_vbl_count = intel_crtc_get_vblank_counter(crtc);

	trace_i915_pipe_update_vblank_evaded(crtc);
}

/**
 * intel_pipe_update_end() - end update of a set of display registers
 * @crtc: the crtc of which the registers were updated
 * @start_vbl_count: start vblank counter (used for error checking)
 *
 * Mark the end of an update started with intel_pipe_update_start(). This
 * re-enables interrupts and verifies the update was actually completed
 * before a vblank using the value of @start_vbl_count.
 */
void intel_pipe_update_end(struct intel_crtc *crtc, struct intel_flip_work *work)
{
	enum pipe pipe = crtc->pipe;
	int scanline_end = intel_get_crtc_scanline(crtc);
	u32 end_vbl_count = intel_crtc_get_vblank_counter(crtc);
	ktime_t end_vbl_time = ktime_get();

	if (work) {
		work->flip_queued_vblank = end_vbl_count;
		smp_mb__before_atomic();
		atomic_set(&work->pending, 1);
	}

	trace_i915_pipe_update_end(crtc, end_vbl_count, scanline_end);

	/* We're still in the vblank-evade critical section, this can't race.
	 * Would be slightly nice to just grab the vblank count and arm the
	 * event outside of the critical section - the spinlock might spin for a
	 * while ... */
	if (crtc->base.state->event) {
		WARN_ON(drm_crtc_vblank_get(&crtc->base) != 0);

		spin_lock(&crtc->base.dev->event_lock);
		drm_crtc_arm_vblank_event(&crtc->base, crtc->base.state->event);
		spin_unlock(&crtc->base.dev->event_lock);

		crtc->base.state->event = NULL;
	}

	local_irq_enable();

	if (crtc->debug.start_vbl_count &&
	    crtc->debug.start_vbl_count != end_vbl_count) {
		DRM_ERROR("Atomic update failure on pipe %c (start=%u end=%u) time %lld us, min %d, max %d, scanline start %d, end %d\n",
			  pipe_name(pipe), crtc->debug.start_vbl_count,
			  end_vbl_count,
			  ktime_us_delta(end_vbl_time, crtc->debug.start_vbl_time),
			  crtc->debug.min_vbl, crtc->debug.max_vbl,
			  crtc->debug.scanline_start, scanline_end);
	}
}

static void
skl_update_plane(struct drm_plane *drm_plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = drm_plane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane = to_intel_plane(drm_plane);
	struct drm_framebuffer *fb = plane_state->base.fb;
	const struct skl_wm_values *wm = &dev_priv->wm.skl_results;
	struct drm_crtc *crtc = crtc_state->base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	const int pipe = intel_plane->pipe;
	const int plane = intel_plane->plane + 1;
	u32 plane_ctl;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 surf_addr = plane_state->main.offset;
	unsigned int rotation = plane_state->base.rotation;
	u32 stride = skl_plane_stride(fb, 0, rotation);
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t crtc_w = drm_rect_width(&plane_state->base.dst);
	uint32_t crtc_h = drm_rect_height(&plane_state->base.dst);
	uint32_t x = plane_state->main.x;
	uint32_t y = plane_state->main.y;
	uint32_t src_w = drm_rect_width(&plane_state->base.src) >> 16;
	uint32_t src_h = drm_rect_height(&plane_state->base.src) >> 16;

	plane_ctl = PLANE_CTL_ENABLE |
		PLANE_CTL_PIPE_GAMMA_ENABLE |
		PLANE_CTL_PIPE_CSC_ENABLE;

	plane_ctl |= skl_plane_ctl_format(fb->pixel_format);
	plane_ctl |= skl_plane_ctl_tiling(fb->modifier[0]);

	plane_ctl |= skl_plane_ctl_rotation(rotation);

	if (wm->dirty_pipes & drm_crtc_mask(crtc))
		skl_write_plane_wm(intel_crtc, wm, plane);

	if (key->flags) {
		I915_WRITE(PLANE_KEYVAL(pipe, plane), key->min_value);
		I915_WRITE(PLANE_KEYMAX(pipe, plane), key->max_value);
		I915_WRITE(PLANE_KEYMSK(pipe, plane), key->channel_mask);
	}

	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		plane_ctl |= PLANE_CTL_KEY_ENABLE_DESTINATION;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		plane_ctl |= PLANE_CTL_KEY_ENABLE_SOURCE;

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	I915_WRITE(PLANE_OFFSET(pipe, plane), (y << 16) | x);
	I915_WRITE(PLANE_STRIDE(pipe, plane), stride);
	I915_WRITE(PLANE_SIZE(pipe, plane), (src_h << 16) | src_w);

	/* program plane scaler */
	if (plane_state->scaler_id >= 0) {
		int scaler_id = plane_state->scaler_id;
		const struct intel_scaler *scaler;

		DRM_DEBUG_KMS("plane = %d PS_PLANE_SEL(plane) = 0x%x\n", plane,
			PS_PLANE_SEL(plane));

		scaler = &crtc_state->scaler_state.scalers[scaler_id];

		I915_WRITE(SKL_PS_CTRL(pipe, scaler_id),
			   PS_SCALER_EN | PS_PLANE_SEL(plane) | scaler->mode);
		I915_WRITE(SKL_PS_PWR_GATE(pipe, scaler_id), 0);
		I915_WRITE(SKL_PS_WIN_POS(pipe, scaler_id), (crtc_x << 16) | crtc_y);
		I915_WRITE(SKL_PS_WIN_SZ(pipe, scaler_id),
			((crtc_w + 1) << 16)|(crtc_h + 1));

		I915_WRITE(PLANE_POS(pipe, plane), 0);
	} else {
		I915_WRITE(PLANE_POS(pipe, plane), (crtc_y << 16) | crtc_x);
	}

	I915_WRITE(PLANE_CTL(pipe, plane), plane_ctl);
	I915_WRITE(PLANE_SURF(pipe, plane),
		   intel_fb_gtt_offset(fb, rotation) + surf_addr);
	POSTING_READ(PLANE_SURF(pipe, plane));
}

static void
skl_disable_plane(struct drm_plane *dplane, struct drm_crtc *crtc)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	const int pipe = intel_plane->pipe;
	const int plane = intel_plane->plane + 1;

	/*
	 * We only populate skl_results on watermark updates, and if the
	 * plane's visiblity isn't actually changing neither is its watermarks.
	 */
	if (!dplane->state->visible)
		skl_write_plane_wm(to_intel_crtc(crtc),
				   &dev_priv->wm.skl_results, plane);

	I915_WRITE(PLANE_CTL(pipe, plane), 0);

	I915_WRITE(PLANE_SURF(pipe, plane), 0);
	POSTING_READ(PLANE_SURF(pipe, plane));
}

static void
chv_update_csc(struct intel_plane *intel_plane, uint32_t format)
{
	struct drm_i915_private *dev_priv = to_i915(intel_plane->base.dev);
	int plane = intel_plane->plane;

	/* Seems RGB data bypasses the CSC always */
	if (!format_is_yuv(format))
		return;

	/*
	 * BT.601 limited range YCbCr -> full range RGB
	 *
	 * |r|   | 6537 4769     0|   |cr  |
	 * |g| = |-3330 4769 -1605| x |y-64|
	 * |b|   |    0 4769  8263|   |cb  |
	 *
	 * Cb and Cr apparently come in as signed already, so no
	 * need for any offset. For Y we need to remove the offset.
	 */
	I915_WRITE(SPCSCYGOFF(plane), SPCSC_OOFF(0) | SPCSC_IOFF(-64));
	I915_WRITE(SPCSCCBOFF(plane), SPCSC_OOFF(0) | SPCSC_IOFF(0));
	I915_WRITE(SPCSCCROFF(plane), SPCSC_OOFF(0) | SPCSC_IOFF(0));

	I915_WRITE(SPCSCC01(plane), SPCSC_C1(4769) | SPCSC_C0(6537));
	I915_WRITE(SPCSCC23(plane), SPCSC_C1(-3330) | SPCSC_C0(0));
	I915_WRITE(SPCSCC45(plane), SPCSC_C1(-1605) | SPCSC_C0(4769));
	I915_WRITE(SPCSCC67(plane), SPCSC_C1(4769) | SPCSC_C0(0));
	I915_WRITE(SPCSCC8(plane), SPCSC_C0(8263));

	I915_WRITE(SPCSCYGICLAMP(plane), SPCSC_IMAX(940) | SPCSC_IMIN(64));
	I915_WRITE(SPCSCCBICLAMP(plane), SPCSC_IMAX(448) | SPCSC_IMIN(-448));
	I915_WRITE(SPCSCCRICLAMP(plane), SPCSC_IMAX(448) | SPCSC_IMIN(-448));

	I915_WRITE(SPCSCYGOCLAMP(plane), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
	I915_WRITE(SPCSCCBOCLAMP(plane), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
	I915_WRITE(SPCSCCROCLAMP(plane), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
}

static void
vlv_update_plane(struct drm_plane *dplane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	struct drm_framebuffer *fb = plane_state->base.fb;
	int pipe = intel_plane->pipe;
	int plane = intel_plane->plane;
	u32 sprctl;
	u32 sprsurf_offset, linear_offset;
	unsigned int rotation = plane_state->base.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t crtc_w = drm_rect_width(&plane_state->base.dst);
	uint32_t crtc_h = drm_rect_height(&plane_state->base.dst);
	uint32_t x = plane_state->base.src.x1 >> 16;
	uint32_t y = plane_state->base.src.y1 >> 16;
	uint32_t src_w = drm_rect_width(&plane_state->base.src) >> 16;
	uint32_t src_h = drm_rect_height(&plane_state->base.src) >> 16;

	sprctl = SP_ENABLE;

	switch (fb->pixel_format) {
	case DRM_FORMAT_YUYV:
		sprctl |= SP_FORMAT_YUV422 | SP_YUV_ORDER_YUYV;
		break;
	case DRM_FORMAT_YVYU:
		sprctl |= SP_FORMAT_YUV422 | SP_YUV_ORDER_YVYU;
		break;
	case DRM_FORMAT_UYVY:
		sprctl |= SP_FORMAT_YUV422 | SP_YUV_ORDER_UYVY;
		break;
	case DRM_FORMAT_VYUY:
		sprctl |= SP_FORMAT_YUV422 | SP_YUV_ORDER_VYUY;
		break;
	case DRM_FORMAT_RGB565:
		sprctl |= SP_FORMAT_BGR565;
		break;
	case DRM_FORMAT_XRGB8888:
		sprctl |= SP_FORMAT_BGRX8888;
		break;
	case DRM_FORMAT_ARGB8888:
		sprctl |= SP_FORMAT_BGRA8888;
		break;
	case DRM_FORMAT_XBGR2101010:
		sprctl |= SP_FORMAT_RGBX1010102;
		break;
	case DRM_FORMAT_ABGR2101010:
		sprctl |= SP_FORMAT_RGBA1010102;
		break;
	case DRM_FORMAT_XBGR8888:
		sprctl |= SP_FORMAT_RGBX8888;
		break;
	case DRM_FORMAT_ABGR8888:
		sprctl |= SP_FORMAT_RGBA8888;
		break;
	default:
		/*
		 * If we get here one of the upper layers failed to filter
		 * out the unsupported plane formats
		 */
		BUG();
		break;
	}

	/*
	 * Enable gamma to match primary/cursor plane behaviour.
	 * FIXME should be user controllable via propertiesa.
	 */
	sprctl |= SP_GAMMA_ENABLE;

	if (fb->modifier[0] == I915_FORMAT_MOD_X_TILED)
		sprctl |= SP_TILED;

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	intel_add_fb_offsets(&x, &y, plane_state, 0);
	sprsurf_offset = intel_compute_tile_offset(&x, &y, plane_state, 0);

	if (rotation == DRM_ROTATE_180) {
		sprctl |= SP_ROTATE_180;

		x += src_w;
		y += src_h;
	}

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	if (key->flags) {
		I915_WRITE(SPKEYMINVAL(pipe, plane), key->min_value);
		I915_WRITE(SPKEYMAXVAL(pipe, plane), key->max_value);
		I915_WRITE(SPKEYMSK(pipe, plane), key->channel_mask);
	}

	if (key->flags & I915_SET_COLORKEY_SOURCE)
		sprctl |= SP_SOURCE_KEY;

	if (IS_CHERRYVIEW(dev) && pipe == PIPE_B)
		chv_update_csc(intel_plane, fb->pixel_format);

	I915_WRITE(SPSTRIDE(pipe, plane), fb->pitches[0]);
	I915_WRITE(SPPOS(pipe, plane), (crtc_y << 16) | crtc_x);

	if (fb->modifier[0] == I915_FORMAT_MOD_X_TILED)
		I915_WRITE(SPTILEOFF(pipe, plane), (y << 16) | x);
	else
		I915_WRITE(SPLINOFF(pipe, plane), linear_offset);

	I915_WRITE(SPCONSTALPHA(pipe, plane), 0);

	I915_WRITE(SPSIZE(pipe, plane), (crtc_h << 16) | crtc_w);
	I915_WRITE(SPCNTR(pipe, plane), sprctl);
	I915_WRITE(SPSURF(pipe, plane),
		   intel_fb_gtt_offset(fb, rotation) + sprsurf_offset);
	POSTING_READ(SPSURF(pipe, plane));
}

static void
vlv_disable_plane(struct drm_plane *dplane, struct drm_crtc *crtc)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	int pipe = intel_plane->pipe;
	int plane = intel_plane->plane;

	I915_WRITE(SPCNTR(pipe, plane), 0);

	I915_WRITE(SPSURF(pipe, plane), 0);
	POSTING_READ(SPSURF(pipe, plane));
}

static void
ivb_update_plane(struct drm_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_framebuffer *fb = plane_state->base.fb;
	enum pipe pipe = intel_plane->pipe;
	u32 sprctl, sprscale = 0;
	u32 sprsurf_offset, linear_offset;
	unsigned int rotation = plane_state->base.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t crtc_w = drm_rect_width(&plane_state->base.dst);
	uint32_t crtc_h = drm_rect_height(&plane_state->base.dst);
	uint32_t x = plane_state->base.src.x1 >> 16;
	uint32_t y = plane_state->base.src.y1 >> 16;
	uint32_t src_w = drm_rect_width(&plane_state->base.src) >> 16;
	uint32_t src_h = drm_rect_height(&plane_state->base.src) >> 16;

	sprctl = SPRITE_ENABLE;

	switch (fb->pixel_format) {
	case DRM_FORMAT_XBGR8888:
		sprctl |= SPRITE_FORMAT_RGBX888 | SPRITE_RGB_ORDER_RGBX;
		break;
	case DRM_FORMAT_XRGB8888:
		sprctl |= SPRITE_FORMAT_RGBX888;
		break;
	case DRM_FORMAT_YUYV:
		sprctl |= SPRITE_FORMAT_YUV422 | SPRITE_YUV_ORDER_YUYV;
		break;
	case DRM_FORMAT_YVYU:
		sprctl |= SPRITE_FORMAT_YUV422 | SPRITE_YUV_ORDER_YVYU;
		break;
	case DRM_FORMAT_UYVY:
		sprctl |= SPRITE_FORMAT_YUV422 | SPRITE_YUV_ORDER_UYVY;
		break;
	case DRM_FORMAT_VYUY:
		sprctl |= SPRITE_FORMAT_YUV422 | SPRITE_YUV_ORDER_VYUY;
		break;
	default:
		BUG();
	}

	/*
	 * Enable gamma to match primary/cursor plane behaviour.
	 * FIXME should be user controllable via propertiesa.
	 */
	sprctl |= SPRITE_GAMMA_ENABLE;

	if (fb->modifier[0] == I915_FORMAT_MOD_X_TILED)
		sprctl |= SPRITE_TILED;

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		sprctl &= ~SPRITE_TRICKLE_FEED_DISABLE;
	else
		sprctl |= SPRITE_TRICKLE_FEED_DISABLE;

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		sprctl |= SPRITE_PIPE_CSC_ENABLE;

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	if (crtc_w != src_w || crtc_h != src_h)
		sprscale = SPRITE_SCALE_ENABLE | (src_w << 16) | src_h;

	intel_add_fb_offsets(&x, &y, plane_state, 0);
	sprsurf_offset = intel_compute_tile_offset(&x, &y, plane_state, 0);

	if (rotation == DRM_ROTATE_180) {
		sprctl |= SPRITE_ROTATE_180;

		/* HSW and BDW does this automagically in hardware */
		if (!IS_HASWELL(dev) && !IS_BROADWELL(dev)) {
			x += src_w;
			y += src_h;
		}
	}

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	if (key->flags) {
		I915_WRITE(SPRKEYVAL(pipe), key->min_value);
		I915_WRITE(SPRKEYMAX(pipe), key->max_value);
		I915_WRITE(SPRKEYMSK(pipe), key->channel_mask);
	}

	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		sprctl |= SPRITE_DEST_KEY;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		sprctl |= SPRITE_SOURCE_KEY;

	I915_WRITE(SPRSTRIDE(pipe), fb->pitches[0]);
	I915_WRITE(SPRPOS(pipe), (crtc_y << 16) | crtc_x);

	/* HSW consolidates SPRTILEOFF and SPRLINOFF into a single SPROFFSET
	 * register */
	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		I915_WRITE(SPROFFSET(pipe), (y << 16) | x);
	else if (fb->modifier[0] == I915_FORMAT_MOD_X_TILED)
		I915_WRITE(SPRTILEOFF(pipe), (y << 16) | x);
	else
		I915_WRITE(SPRLINOFF(pipe), linear_offset);

	I915_WRITE(SPRSIZE(pipe), (crtc_h << 16) | crtc_w);
	if (intel_plane->can_scale)
		I915_WRITE(SPRSCALE(pipe), sprscale);
	I915_WRITE(SPRCTL(pipe), sprctl);
	I915_WRITE(SPRSURF(pipe),
		   intel_fb_gtt_offset(fb, rotation) + sprsurf_offset);
	POSTING_READ(SPRSURF(pipe));
}

static void
ivb_disable_plane(struct drm_plane *plane, struct drm_crtc *crtc)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int pipe = intel_plane->pipe;

	I915_WRITE(SPRCTL(pipe), 0);
	/* Can't leave the scaler enabled... */
	if (intel_plane->can_scale)
		I915_WRITE(SPRSCALE(pipe), 0);

	I915_WRITE(SPRSURF(pipe), 0);
	POSTING_READ(SPRSURF(pipe));
}

static void
ilk_update_plane(struct drm_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_framebuffer *fb = plane_state->base.fb;
	int pipe = intel_plane->pipe;
	u32 dvscntr, dvsscale;
	u32 dvssurf_offset, linear_offset;
	unsigned int rotation = plane_state->base.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t crtc_w = drm_rect_width(&plane_state->base.dst);
	uint32_t crtc_h = drm_rect_height(&plane_state->base.dst);
	uint32_t x = plane_state->base.src.x1 >> 16;
	uint32_t y = plane_state->base.src.y1 >> 16;
	uint32_t src_w = drm_rect_width(&plane_state->base.src) >> 16;
	uint32_t src_h = drm_rect_height(&plane_state->base.src) >> 16;

	dvscntr = DVS_ENABLE;

	switch (fb->pixel_format) {
	case DRM_FORMAT_XBGR8888:
		dvscntr |= DVS_FORMAT_RGBX888 | DVS_RGB_ORDER_XBGR;
		break;
	case DRM_FORMAT_XRGB8888:
		dvscntr |= DVS_FORMAT_RGBX888;
		break;
	case DRM_FORMAT_YUYV:
		dvscntr |= DVS_FORMAT_YUV422 | DVS_YUV_ORDER_YUYV;
		break;
	case DRM_FORMAT_YVYU:
		dvscntr |= DVS_FORMAT_YUV422 | DVS_YUV_ORDER_YVYU;
		break;
	case DRM_FORMAT_UYVY:
		dvscntr |= DVS_FORMAT_YUV422 | DVS_YUV_ORDER_UYVY;
		break;
	case DRM_FORMAT_VYUY:
		dvscntr |= DVS_FORMAT_YUV422 | DVS_YUV_ORDER_VYUY;
		break;
	default:
		BUG();
	}

	/*
	 * Enable gamma to match primary/cursor plane behaviour.
	 * FIXME should be user controllable via propertiesa.
	 */
	dvscntr |= DVS_GAMMA_ENABLE;

	if (fb->modifier[0] == I915_FORMAT_MOD_X_TILED)
		dvscntr |= DVS_TILED;

	if (IS_GEN6(dev))
		dvscntr |= DVS_TRICKLE_FEED_DISABLE; /* must disable */

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	dvsscale = 0;
	if (crtc_w != src_w || crtc_h != src_h)
		dvsscale = DVS_SCALE_ENABLE | (src_w << 16) | src_h;

	intel_add_fb_offsets(&x, &y, plane_state, 0);
	dvssurf_offset = intel_compute_tile_offset(&x, &y, plane_state, 0);

	if (rotation == DRM_ROTATE_180) {
		dvscntr |= DVS_ROTATE_180;

		x += src_w;
		y += src_h;
	}

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	if (key->flags) {
		I915_WRITE(DVSKEYVAL(pipe), key->min_value);
		I915_WRITE(DVSKEYMAX(pipe), key->max_value);
		I915_WRITE(DVSKEYMSK(pipe), key->channel_mask);
	}

	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		dvscntr |= DVS_DEST_KEY;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		dvscntr |= DVS_SOURCE_KEY;

	I915_WRITE(DVSSTRIDE(pipe), fb->pitches[0]);
	I915_WRITE(DVSPOS(pipe), (crtc_y << 16) | crtc_x);

	if (fb->modifier[0] == I915_FORMAT_MOD_X_TILED)
		I915_WRITE(DVSTILEOFF(pipe), (y << 16) | x);
	else
		I915_WRITE(DVSLINOFF(pipe), linear_offset);

	I915_WRITE(DVSSIZE(pipe), (crtc_h << 16) | crtc_w);
	I915_WRITE(DVSSCALE(pipe), dvsscale);
	I915_WRITE(DVSCNTR(pipe), dvscntr);
	I915_WRITE(DVSSURF(pipe),
		   intel_fb_gtt_offset(fb, rotation) + dvssurf_offset);
	POSTING_READ(DVSSURF(pipe));
}

static void
ilk_disable_plane(struct drm_plane *plane, struct drm_crtc *crtc)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int pipe = intel_plane->pipe;

	I915_WRITE(DVSCNTR(pipe), 0);
	/* Disable the scaler */
	I915_WRITE(DVSSCALE(pipe), 0);

	I915_WRITE(DVSSURF(pipe), 0);
	POSTING_READ(DVSSURF(pipe));
}

static int
intel_check_sprite_plane(struct drm_plane *plane,
			 struct intel_crtc_state *crtc_state,
			 struct intel_plane_state *state)
{
	struct drm_device *dev = plane->dev;
	struct drm_crtc *crtc = state->base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_framebuffer *fb = state->base.fb;
	int crtc_x, crtc_y;
	unsigned int crtc_w, crtc_h;
	uint32_t src_x, src_y, src_w, src_h;
	struct drm_rect *src = &state->base.src;
	struct drm_rect *dst = &state->base.dst;
	const struct drm_rect *clip = &state->clip;
	int hscale, vscale;
	int max_scale, min_scale;
	bool can_scale;
	int ret;

	src->x1 = state->base.src_x;
	src->y1 = state->base.src_y;
	src->x2 = state->base.src_x + state->base.src_w;
	src->y2 = state->base.src_y + state->base.src_h;

	dst->x1 = state->base.crtc_x;
	dst->y1 = state->base.crtc_y;
	dst->x2 = state->base.crtc_x + state->base.crtc_w;
	dst->y2 = state->base.crtc_y + state->base.crtc_h;

	if (!fb) {
		state->base.visible = false;
		return 0;
	}

	/* Don't modify another pipe's plane */
	if (intel_plane->pipe != intel_crtc->pipe) {
		DRM_DEBUG_KMS("Wrong plane <-> crtc mapping\n");
		return -EINVAL;
	}

	/* FIXME check all gen limits */
	if (fb->width < 3 || fb->height < 3 || fb->pitches[0] > 16384) {
		DRM_DEBUG_KMS("Unsuitable framebuffer for plane\n");
		return -EINVAL;
	}

	/* setup can_scale, min_scale, max_scale */
	if (INTEL_INFO(dev)->gen >= 9) {
		/* use scaler when colorkey is not required */
		if (state->ckey.flags == I915_SET_COLORKEY_NONE) {
			can_scale = 1;
			min_scale = 1;
			max_scale = skl_max_scale(intel_crtc, crtc_state);
		} else {
			can_scale = 0;
			min_scale = DRM_PLANE_HELPER_NO_SCALING;
			max_scale = DRM_PLANE_HELPER_NO_SCALING;
		}
	} else {
		can_scale = intel_plane->can_scale;
		max_scale = intel_plane->max_downscale << 16;
		min_scale = intel_plane->can_scale ? 1 : (1 << 16);
	}

	/*
	 * FIXME the following code does a bunch of fuzzy adjustments to the
	 * coordinates and sizes. We probably need some way to decide whether
	 * more strict checking should be done instead.
	 */
	drm_rect_rotate(src, fb->width << 16, fb->height << 16,
			state->base.rotation);

	hscale = drm_rect_calc_hscale_relaxed(src, dst, min_scale, max_scale);
	BUG_ON(hscale < 0);

	vscale = drm_rect_calc_vscale_relaxed(src, dst, min_scale, max_scale);
	BUG_ON(vscale < 0);

	state->base.visible = drm_rect_clip_scaled(src, dst, clip, hscale, vscale);

	crtc_x = dst->x1;
	crtc_y = dst->y1;
	crtc_w = drm_rect_width(dst);
	crtc_h = drm_rect_height(dst);

	if (state->base.visible) {
		/* check again in case clipping clamped the results */
		hscale = drm_rect_calc_hscale(src, dst, min_scale, max_scale);
		if (hscale < 0) {
			DRM_DEBUG_KMS("Horizontal scaling factor out of limits\n");
			drm_rect_debug_print("src: ", src, true);
			drm_rect_debug_print("dst: ", dst, false);

			return hscale;
		}

		vscale = drm_rect_calc_vscale(src, dst, min_scale, max_scale);
		if (vscale < 0) {
			DRM_DEBUG_KMS("Vertical scaling factor out of limits\n");
			drm_rect_debug_print("src: ", src, true);
			drm_rect_debug_print("dst: ", dst, false);

			return vscale;
		}

		/* Make the source viewport size an exact multiple of the scaling factors. */
		drm_rect_adjust_size(src,
				     drm_rect_width(dst) * hscale - drm_rect_width(src),
				     drm_rect_height(dst) * vscale - drm_rect_height(src));

		drm_rect_rotate_inv(src, fb->width << 16, fb->height << 16,
				    state->base.rotation);

		/* sanity check to make sure the src viewport wasn't enlarged */
		WARN_ON(src->x1 < (int) state->base.src_x ||
			src->y1 < (int) state->base.src_y ||
			src->x2 > (int) state->base.src_x + state->base.src_w ||
			src->y2 > (int) state->base.src_y + state->base.src_h);

		/*
		 * Hardware doesn't handle subpixel coordinates.
		 * Adjust to (macro)pixel boundary, but be careful not to
		 * increase the source viewport size, because that could
		 * push the downscaling factor out of bounds.
		 */
		src_x = src->x1 >> 16;
		src_w = drm_rect_width(src) >> 16;
		src_y = src->y1 >> 16;
		src_h = drm_rect_height(src) >> 16;

		if (format_is_yuv(fb->pixel_format)) {
			src_x &= ~1;
			src_w &= ~1;

			/*
			 * Must keep src and dst the
			 * same if we can't scale.
			 */
			if (!can_scale)
				crtc_w &= ~1;

			if (crtc_w == 0)
				state->base.visible = false;
		}
	}

	/* Check size restrictions when scaling */
	if (state->base.visible && (src_w != crtc_w || src_h != crtc_h)) {
		unsigned int width_bytes;
		int cpp = drm_format_plane_cpp(fb->pixel_format, 0);

		WARN_ON(!can_scale);

		/* FIXME interlacing min height is 6 */

		if (crtc_w < 3 || crtc_h < 3)
			state->base.visible = false;

		if (src_w < 3 || src_h < 3)
			state->base.visible = false;

		width_bytes = ((src_x * cpp) & 63) + src_w * cpp;

		if (INTEL_INFO(dev)->gen < 9 && (src_w > 2048 || src_h > 2048 ||
		    width_bytes > 4096 || fb->pitches[0] > 4096)) {
			DRM_DEBUG_KMS("Source dimensions exceed hardware limits\n");
			return -EINVAL;
		}
	}

	if (state->base.visible) {
		src->x1 = src_x << 16;
		src->x2 = (src_x + src_w) << 16;
		src->y1 = src_y << 16;
		src->y2 = (src_y + src_h) << 16;
	}

	dst->x1 = crtc_x;
	dst->x2 = crtc_x + crtc_w;
	dst->y1 = crtc_y;
	dst->y2 = crtc_y + crtc_h;

	if (INTEL_GEN(dev) >= 9) {
		ret = skl_check_plane_surface(state);
		if (ret)
			return ret;
	}

	return 0;
}

int intel_sprite_set_colorkey(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_intel_sprite_colorkey *set = data;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	int ret = 0;

	/* Make sure we don't try to enable both src & dest simultaneously */
	if ((set->flags & (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE)) == (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE))
		return -EINVAL;

	if ((IS_VALLEYVIEW(dev) || IS_CHERRYVIEW(dev)) &&
	    set->flags & I915_SET_COLORKEY_DESTINATION)
		return -EINVAL;

	plane = drm_plane_find(dev, set->plane_id);
	if (!plane || plane->type != DRM_PLANE_TYPE_OVERLAY)
		return -ENOENT;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(plane->dev);
	if (!state) {
		ret = -ENOMEM;
		goto out;
	}
	state->acquire_ctx = &ctx;

	while (1) {
		plane_state = drm_atomic_get_plane_state(state, plane);
		ret = PTR_ERR_OR_ZERO(plane_state);
		if (!ret) {
			to_intel_plane_state(plane_state)->ckey = *set;
			ret = drm_atomic_commit(state);
		}

		if (ret != -EDEADLK)
			break;

		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
	}

	if (ret)
		drm_atomic_state_free(state);

out:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	return ret;
}

static const uint32_t ilk_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static const uint32_t snb_plane_formats[] = {
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static const uint32_t vlv_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static uint32_t skl_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

int
intel_plane_init(struct drm_device *dev, enum pipe pipe, int plane)
{
	struct intel_plane *intel_plane = NULL;
	struct intel_plane_state *state = NULL;
	unsigned long possible_crtcs;
	const uint32_t *plane_formats;
	int num_plane_formats;
	int ret;

	if (INTEL_INFO(dev)->gen < 5)
		return -ENODEV;

	intel_plane = kzalloc(sizeof(*intel_plane), GFP_KERNEL);
	if (!intel_plane) {
		ret = -ENOMEM;
		goto fail;
	}

	state = intel_create_plane_state(&intel_plane->base);
	if (!state) {
		ret = -ENOMEM;
		goto fail;
	}
	intel_plane->base.state = &state->base;

	switch (INTEL_INFO(dev)->gen) {
	case 5:
	case 6:
		intel_plane->can_scale = true;
		intel_plane->max_downscale = 16;
		intel_plane->update_plane = ilk_update_plane;
		intel_plane->disable_plane = ilk_disable_plane;

		if (IS_GEN6(dev)) {
			plane_formats = snb_plane_formats;
			num_plane_formats = ARRAY_SIZE(snb_plane_formats);
		} else {
			plane_formats = ilk_plane_formats;
			num_plane_formats = ARRAY_SIZE(ilk_plane_formats);
		}
		break;

	case 7:
	case 8:
		if (IS_IVYBRIDGE(dev)) {
			intel_plane->can_scale = true;
			intel_plane->max_downscale = 2;
		} else {
			intel_plane->can_scale = false;
			intel_plane->max_downscale = 1;
		}

		if (IS_VALLEYVIEW(dev) || IS_CHERRYVIEW(dev)) {
			intel_plane->update_plane = vlv_update_plane;
			intel_plane->disable_plane = vlv_disable_plane;

			plane_formats = vlv_plane_formats;
			num_plane_formats = ARRAY_SIZE(vlv_plane_formats);
		} else {
			intel_plane->update_plane = ivb_update_plane;
			intel_plane->disable_plane = ivb_disable_plane;

			plane_formats = snb_plane_formats;
			num_plane_formats = ARRAY_SIZE(snb_plane_formats);
		}
		break;
	case 9:
		intel_plane->can_scale = true;
		intel_plane->update_plane = skl_update_plane;
		intel_plane->disable_plane = skl_disable_plane;
		state->scaler_id = -1;

		plane_formats = skl_plane_formats;
		num_plane_formats = ARRAY_SIZE(skl_plane_formats);
		break;
	default:
		MISSING_CASE(INTEL_INFO(dev)->gen);
		ret = -ENODEV;
		goto fail;
	}

	intel_plane->pipe = pipe;
	intel_plane->plane = plane;
	intel_plane->frontbuffer_bit = INTEL_FRONTBUFFER_SPRITE(pipe, plane);
	intel_plane->check_plane = intel_check_sprite_plane;

	possible_crtcs = (1 << pipe);

	if (INTEL_INFO(dev)->gen >= 9)
		ret = drm_universal_plane_init(dev, &intel_plane->base, possible_crtcs,
					       &intel_plane_funcs,
					       plane_formats, num_plane_formats,
					       DRM_PLANE_TYPE_OVERLAY,
					       "plane %d%c", plane + 2, pipe_name(pipe));
	else
		ret = drm_universal_plane_init(dev, &intel_plane->base, possible_crtcs,
					       &intel_plane_funcs,
					       plane_formats, num_plane_formats,
					       DRM_PLANE_TYPE_OVERLAY,
					       "sprite %c", sprite_name(pipe, plane));
	if (ret)
		goto fail;

	intel_create_rotation_property(dev, intel_plane);

	drm_plane_helper_add(&intel_plane->base, &intel_plane_helper_funcs);

	return 0;

fail:
	kfree(state);
	kfree(intel_plane);

	return ret;
}

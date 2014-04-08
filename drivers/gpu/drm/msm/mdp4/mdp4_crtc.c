/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdp4_kms.h"

#include <drm/drm_mode.h>
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "drm_flip_work.h"

struct mdp4_crtc {
	struct drm_crtc base;
	char name[8];
	struct drm_plane *plane;
	struct drm_plane *planes[8];
	int id;
	int ovlp;
	enum mdp4_dma dma;
	bool enabled;

	/* which mixer/encoder we route output to: */
	int mixer;

	struct {
		spinlock_t lock;
		bool stale;
		uint32_t width, height;

		/* next cursor to scan-out: */
		uint32_t next_iova;
		struct drm_gem_object *next_bo;

		/* current cursor being scanned out: */
		struct drm_gem_object *scanout_bo;
	} cursor;


	/* if there is a pending flip, these will be non-null: */
	struct drm_pending_vblank_event *event;
	struct msm_fence_cb pageflip_cb;

#define PENDING_CURSOR 0x1
#define PENDING_FLIP   0x2
	atomic_t pending;

	/* the fb that we currently hold a scanout ref to: */
	struct drm_framebuffer *fb;

	/* for unref'ing framebuffers after scanout completes: */
	struct drm_flip_work unref_fb_work;

	/* for unref'ing cursor bo's after scanout completes: */
	struct drm_flip_work unref_cursor_work;

	struct mdp4_irq vblank;
	struct mdp4_irq err;
};
#define to_mdp4_crtc(x) container_of(x, struct mdp4_crtc, base)

static struct mdp4_kms *get_kms(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv = crtc->dev->dev_private;
	return to_mdp4_kms(priv->kms);
}

static void update_fb(struct drm_crtc *crtc, bool async,
		struct drm_framebuffer *new_fb)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct drm_framebuffer *old_fb = mdp4_crtc->fb;

	if (old_fb)
		drm_flip_work_queue(&mdp4_crtc->unref_fb_work, old_fb);

	/* grab reference to incoming scanout fb: */
	drm_framebuffer_reference(new_fb);
	mdp4_crtc->base.fb = new_fb;
	mdp4_crtc->fb = new_fb;

	if (!async) {
		/* enable vblank to pick up the old_fb */
		mdp4_irq_register(get_kms(crtc), &mdp4_crtc->vblank);
	}
}

/* if file!=NULL, this is preclose potential cancel-flip path */
static void complete_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = mdp4_crtc->event;
	if (event) {
		/* if regular vblank case (!file) or if cancel-flip from
		 * preclose on file that requested flip, then send the
		 * event:
		 */
		if (!file || (event->base.file_priv == file)) {
			mdp4_crtc->event = NULL;
			drm_send_vblank_event(dev, mdp4_crtc->id, event);
		}
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void crtc_flush(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	uint32_t i, flush = 0;

	for (i = 0; i < ARRAY_SIZE(mdp4_crtc->planes); i++) {
		struct drm_plane *plane = mdp4_crtc->planes[i];
		if (plane) {
			enum mdp4_pipe pipe_id = mdp4_plane_pipe(plane);
			flush |= pipe2flush(pipe_id);
		}
	}
	flush |= ovlp2flush(mdp4_crtc->ovlp);

	DBG("%s: flush=%08x", mdp4_crtc->name, flush);

	mdp4_write(mdp4_kms, REG_MDP4_OVERLAY_FLUSH, flush);
}

static void request_pending(struct drm_crtc *crtc, uint32_t pending)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);

	atomic_or(pending, &mdp4_crtc->pending);
	mdp4_irq_register(get_kms(crtc), &mdp4_crtc->vblank);
}

static void pageflip_cb(struct msm_fence_cb *cb)
{
	struct mdp4_crtc *mdp4_crtc =
		container_of(cb, struct mdp4_crtc, pageflip_cb);
	struct drm_crtc *crtc = &mdp4_crtc->base;
	struct drm_framebuffer *fb = crtc->fb;

	if (!fb)
		return;

	mdp4_plane_set_scanout(mdp4_crtc->plane, fb);
	crtc_flush(crtc);

	/* enable vblank to complete flip: */
	request_pending(crtc, PENDING_FLIP);
}

static void unref_fb_worker(struct drm_flip_work *work, void *val)
{
	struct mdp4_crtc *mdp4_crtc =
		container_of(work, struct mdp4_crtc, unref_fb_work);
	struct drm_device *dev = mdp4_crtc->base.dev;

	mutex_lock(&dev->mode_config.mutex);
	drm_framebuffer_unreference(val);
	mutex_unlock(&dev->mode_config.mutex);
}

static void unref_cursor_worker(struct drm_flip_work *work, void *val)
{
	struct mdp4_crtc *mdp4_crtc =
		container_of(work, struct mdp4_crtc, unref_cursor_work);
	struct mdp4_kms *mdp4_kms = get_kms(&mdp4_crtc->base);

	msm_gem_put_iova(val, mdp4_kms->id);
	drm_gem_object_unreference_unlocked(val);
}

static void mdp4_crtc_destroy(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);

	mdp4_crtc->plane->funcs->destroy(mdp4_crtc->plane);

	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&mdp4_crtc->unref_fb_work);
	drm_flip_work_cleanup(&mdp4_crtc->unref_cursor_work);

	kfree(mdp4_crtc);
}

static void mdp4_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	bool enabled = (mode == DRM_MODE_DPMS_ON);

	DBG("%s: mode=%d", mdp4_crtc->name, mode);

	if (enabled != mdp4_crtc->enabled) {
		if (enabled) {
			mdp4_enable(mdp4_kms);
			mdp4_irq_register(mdp4_kms, &mdp4_crtc->err);
		} else {
			mdp4_irq_unregister(mdp4_kms, &mdp4_crtc->err);
			mdp4_disable(mdp4_kms);
		}
		mdp4_crtc->enabled = enabled;
	}
}

static bool mdp4_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void blend_setup(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	int i, ovlp = mdp4_crtc->ovlp;
	uint32_t mixer_cfg = 0;
	static const enum mdp4_mixer_stage_id stages[] = {
			STAGE_BASE, STAGE0, STAGE1, STAGE2, STAGE3,
	};
	/* statically (for now) map planes to mixer stage (z-order): */
	static const int idxs[] = {
			[VG1]  = 1,
			[VG2]  = 2,
			[RGB1] = 0,
			[RGB2] = 0,
			[RGB3] = 0,
			[VG3]  = 3,
			[VG4]  = 4,

	};
	bool alpha[4]= { false, false, false, false };

	mdp4_write(mdp4_kms, REG_MDP4_OVLP_TRANSP_LOW0(ovlp), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_TRANSP_LOW1(ovlp), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_TRANSP_HIGH0(ovlp), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_TRANSP_HIGH1(ovlp), 0);

	/* TODO single register for all CRTCs, so this won't work properly
	 * when multiple CRTCs are active..
	 */
	for (i = 0; i < ARRAY_SIZE(mdp4_crtc->planes); i++) {
		struct drm_plane *plane = mdp4_crtc->planes[i];
		if (plane) {
			enum mdp4_pipe pipe_id = mdp4_plane_pipe(plane);
			int idx = idxs[pipe_id];
			if (idx > 0) {
				const struct mdp4_format *format =
					to_mdp4_format(msm_framebuffer_format(plane->fb));
				alpha[idx-1] = format->alpha_enable;
			}
			mixer_cfg |= mixercfg(mdp4_crtc->mixer, pipe_id, stages[idx]);
		}
	}

	/* this shouldn't happen.. and seems to cause underflow: */
	WARN_ON(!mixer_cfg);

	for (i = 0; i < 4; i++) {
		uint32_t op;

		if (alpha[i]) {
			op = MDP4_OVLP_STAGE_OP_FG_ALPHA(FG_PIXEL) |
					MDP4_OVLP_STAGE_OP_BG_ALPHA(FG_PIXEL) |
					MDP4_OVLP_STAGE_OP_BG_INV_ALPHA;
		} else {
			op = MDP4_OVLP_STAGE_OP_FG_ALPHA(FG_CONST) |
					MDP4_OVLP_STAGE_OP_BG_ALPHA(BG_CONST);
		}

		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_FG_ALPHA(ovlp, i), 0xff);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_BG_ALPHA(ovlp, i), 0x00);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_OP(ovlp, i), op);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_CO3(ovlp, i), 1);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_TRANSP_LOW0(ovlp, i), 0);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_TRANSP_LOW1(ovlp, i), 0);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_TRANSP_HIGH0(ovlp, i), 0);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_TRANSP_HIGH1(ovlp, i), 0);
	}

	mdp4_write(mdp4_kms, REG_MDP4_LAYERMIXER_IN_CFG, mixer_cfg);
}

static int mdp4_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	enum mdp4_dma dma = mdp4_crtc->dma;
	int ret, ovlp = mdp4_crtc->ovlp;

	mode = adjusted_mode;

	DBG("%s: set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mdp4_crtc->name, mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	mdp4_write(mdp4_kms, REG_MDP4_DMA_SRC_SIZE(dma),
			MDP4_DMA_SRC_SIZE_WIDTH(mode->hdisplay) |
			MDP4_DMA_SRC_SIZE_HEIGHT(mode->vdisplay));

	/* take data from pipe: */
	mdp4_write(mdp4_kms, REG_MDP4_DMA_SRC_BASE(dma), 0);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_SRC_STRIDE(dma),
			crtc->fb->pitches[0]);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_DST_SIZE(dma),
			MDP4_DMA_DST_SIZE_WIDTH(0) |
			MDP4_DMA_DST_SIZE_HEIGHT(0));

	mdp4_write(mdp4_kms, REG_MDP4_OVLP_BASE(ovlp), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_SIZE(ovlp),
			MDP4_OVLP_SIZE_WIDTH(mode->hdisplay) |
			MDP4_OVLP_SIZE_HEIGHT(mode->vdisplay));
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_STRIDE(ovlp),
			crtc->fb->pitches[0]);

	mdp4_write(mdp4_kms, REG_MDP4_OVLP_CFG(ovlp), 1);

	update_fb(crtc, false, crtc->fb);

	ret = mdp4_plane_mode_set(mdp4_crtc->plane, crtc, crtc->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16);
	if (ret) {
		dev_err(crtc->dev->dev, "%s: failed to set mode on plane: %d\n",
				mdp4_crtc->name, ret);
		return ret;
	}

	if (dma == DMA_E) {
		mdp4_write(mdp4_kms, REG_MDP4_DMA_E_QUANT(0), 0x00ff0000);
		mdp4_write(mdp4_kms, REG_MDP4_DMA_E_QUANT(1), 0x00ff0000);
		mdp4_write(mdp4_kms, REG_MDP4_DMA_E_QUANT(2), 0x00ff0000);
	}

	return 0;
}

static void mdp4_crtc_prepare(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	DBG("%s", mdp4_crtc->name);
	/* make sure we hold a ref to mdp clks while setting up mode: */
	mdp4_enable(get_kms(crtc));
	mdp4_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void mdp4_crtc_commit(struct drm_crtc *crtc)
{
	mdp4_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
	crtc_flush(crtc);
	/* drop the ref to mdp clk's that we got in prepare: */
	mdp4_disable(get_kms(crtc));
}

static int mdp4_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct drm_plane *plane = mdp4_crtc->plane;
	struct drm_display_mode *mode = &crtc->mode;

	update_fb(crtc, false, crtc->fb);

	return mdp4_plane_mode_set(plane, crtc, crtc->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16);
}

static void mdp4_crtc_load_lut(struct drm_crtc *crtc)
{
}

static int mdp4_crtc_page_flip(struct drm_crtc *crtc,
		struct drm_framebuffer *new_fb,
		struct drm_pending_vblank_event *event,
		uint32_t page_flip_flags)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *obj;
	unsigned long flags;

	if (mdp4_crtc->event) {
		dev_err(dev->dev, "already pending flip!\n");
		return -EBUSY;
	}

	obj = msm_framebuffer_bo(new_fb, 0);

	spin_lock_irqsave(&dev->event_lock, flags);
	mdp4_crtc->event = event;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	update_fb(crtc, true, new_fb);

	return msm_gem_queue_inactive_cb(obj, &mdp4_crtc->pageflip_cb);
}

static int mdp4_crtc_set_property(struct drm_crtc *crtc,
		struct drm_property *property, uint64_t val)
{
	// XXX
	return -EINVAL;
}

#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64

/* called from IRQ to update cursor related registers (if needed).  The
 * cursor registers, other than x/y position, appear not to be double
 * buffered, and changing them other than from vblank seems to trigger
 * underflow.
 */
static void update_cursor(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	enum mdp4_dma dma = mdp4_crtc->dma;
	unsigned long flags;

	spin_lock_irqsave(&mdp4_crtc->cursor.lock, flags);
	if (mdp4_crtc->cursor.stale) {
		struct mdp4_kms *mdp4_kms = get_kms(crtc);
		struct drm_gem_object *next_bo = mdp4_crtc->cursor.next_bo;
		struct drm_gem_object *prev_bo = mdp4_crtc->cursor.scanout_bo;
		uint32_t iova = mdp4_crtc->cursor.next_iova;

		if (next_bo) {
			/* take a obj ref + iova ref when we start scanning out: */
			drm_gem_object_reference(next_bo);
			msm_gem_get_iova_locked(next_bo, mdp4_kms->id, &iova);

			/* enable cursor: */
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_SIZE(dma),
					MDP4_DMA_CURSOR_SIZE_WIDTH(mdp4_crtc->cursor.width) |
					MDP4_DMA_CURSOR_SIZE_HEIGHT(mdp4_crtc->cursor.height));
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_BASE(dma), iova);
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_BLEND_CONFIG(dma),
					MDP4_DMA_CURSOR_BLEND_CONFIG_FORMAT(CURSOR_ARGB) |
					MDP4_DMA_CURSOR_BLEND_CONFIG_CURSOR_EN);
		} else {
			/* disable cursor: */
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_BASE(dma), 0);
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_BLEND_CONFIG(dma),
					MDP4_DMA_CURSOR_BLEND_CONFIG_FORMAT(CURSOR_ARGB));
		}

		/* and drop the iova ref + obj rev when done scanning out: */
		if (prev_bo)
			drm_flip_work_queue(&mdp4_crtc->unref_cursor_work, prev_bo);

		mdp4_crtc->cursor.scanout_bo = next_bo;
		mdp4_crtc->cursor.stale = false;
	}
	spin_unlock_irqrestore(&mdp4_crtc->cursor.lock, flags);
}

static int mdp4_crtc_cursor_set(struct drm_crtc *crtc,
		struct drm_file *file_priv, uint32_t handle,
		uint32_t width, uint32_t height)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *cursor_bo, *old_bo;
	unsigned long flags;
	uint32_t iova;
	int ret;

	if ((width > CURSOR_WIDTH) || (height > CURSOR_HEIGHT)) {
		dev_err(dev->dev, "bad cursor size: %dx%d\n", width, height);
		return -EINVAL;
	}

	if (handle) {
		cursor_bo = drm_gem_object_lookup(dev, file_priv, handle);
		if (!cursor_bo)
			return -ENOENT;
	} else {
		cursor_bo = NULL;
	}

	if (cursor_bo) {
		ret = msm_gem_get_iova(cursor_bo, mdp4_kms->id, &iova);
		if (ret)
			goto fail;
	} else {
		iova = 0;
	}

	spin_lock_irqsave(&mdp4_crtc->cursor.lock, flags);
	old_bo = mdp4_crtc->cursor.next_bo;
	mdp4_crtc->cursor.next_bo   = cursor_bo;
	mdp4_crtc->cursor.next_iova = iova;
	mdp4_crtc->cursor.width     = width;
	mdp4_crtc->cursor.height    = height;
	mdp4_crtc->cursor.stale     = true;
	spin_unlock_irqrestore(&mdp4_crtc->cursor.lock, flags);

	if (old_bo) {
		/* drop our previous reference: */
		msm_gem_put_iova(old_bo, mdp4_kms->id);
		drm_gem_object_unreference_unlocked(old_bo);
	}

	request_pending(crtc, PENDING_CURSOR);

	return 0;

fail:
	drm_gem_object_unreference_unlocked(cursor_bo);
	return ret;
}

static int mdp4_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	enum mdp4_dma dma = mdp4_crtc->dma;

	mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_POS(dma),
			MDP4_DMA_CURSOR_POS_X(x) |
			MDP4_DMA_CURSOR_POS_Y(y));

	return 0;
}

static const struct drm_crtc_funcs mdp4_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = mdp4_crtc_destroy,
	.page_flip = mdp4_crtc_page_flip,
	.set_property = mdp4_crtc_set_property,
	.cursor_set = mdp4_crtc_cursor_set,
	.cursor_move = mdp4_crtc_cursor_move,
};

static const struct drm_crtc_helper_funcs mdp4_crtc_helper_funcs = {
	.dpms = mdp4_crtc_dpms,
	.mode_fixup = mdp4_crtc_mode_fixup,
	.mode_set = mdp4_crtc_mode_set,
	.prepare = mdp4_crtc_prepare,
	.commit = mdp4_crtc_commit,
	.mode_set_base = mdp4_crtc_mode_set_base,
	.load_lut = mdp4_crtc_load_lut,
};

static void mdp4_crtc_vblank_irq(struct mdp4_irq *irq, uint32_t irqstatus)
{
	struct mdp4_crtc *mdp4_crtc = container_of(irq, struct mdp4_crtc, vblank);
	struct drm_crtc *crtc = &mdp4_crtc->base;
	struct msm_drm_private *priv = crtc->dev->dev_private;
	unsigned pending;

	mdp4_irq_unregister(get_kms(crtc), &mdp4_crtc->vblank);

	pending = atomic_xchg(&mdp4_crtc->pending, 0);

	if (pending & PENDING_FLIP) {
		complete_flip(crtc, NULL);
		drm_flip_work_commit(&mdp4_crtc->unref_fb_work, priv->wq);
	}

	if (pending & PENDING_CURSOR) {
		update_cursor(crtc);
		drm_flip_work_commit(&mdp4_crtc->unref_cursor_work, priv->wq);
	}
}

static void mdp4_crtc_err_irq(struct mdp4_irq *irq, uint32_t irqstatus)
{
	struct mdp4_crtc *mdp4_crtc = container_of(irq, struct mdp4_crtc, err);
	struct drm_crtc *crtc = &mdp4_crtc->base;
	DBG("%s: error: %08x", mdp4_crtc->name, irqstatus);
	crtc_flush(crtc);
}

uint32_t mdp4_crtc_vblank(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	return mdp4_crtc->vblank.irqmask;
}

void mdp4_crtc_cancel_pending_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	DBG("cancel: %p", file);
	complete_flip(crtc, file);
}

/* set dma config, ie. the format the encoder wants. */
void mdp4_crtc_set_config(struct drm_crtc *crtc, uint32_t config)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);

	mdp4_write(mdp4_kms, REG_MDP4_DMA_CONFIG(mdp4_crtc->dma), config);
}

/* set interface for routing crtc->encoder: */
void mdp4_crtc_set_intf(struct drm_crtc *crtc, enum mdp4_intf intf)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	uint32_t intf_sel;

	intf_sel = mdp4_read(mdp4_kms, REG_MDP4_DISP_INTF_SEL);

	switch (mdp4_crtc->dma) {
	case DMA_P:
		intf_sel &= ~MDP4_DISP_INTF_SEL_PRIM__MASK;
		intf_sel |= MDP4_DISP_INTF_SEL_PRIM(intf);
		break;
	case DMA_S:
		intf_sel &= ~MDP4_DISP_INTF_SEL_SEC__MASK;
		intf_sel |= MDP4_DISP_INTF_SEL_SEC(intf);
		break;
	case DMA_E:
		intf_sel &= ~MDP4_DISP_INTF_SEL_EXT__MASK;
		intf_sel |= MDP4_DISP_INTF_SEL_EXT(intf);
		break;
	}

	if (intf == INTF_DSI_VIDEO) {
		intf_sel &= ~MDP4_DISP_INTF_SEL_DSI_CMD;
		intf_sel |= MDP4_DISP_INTF_SEL_DSI_VIDEO;
		mdp4_crtc->mixer = 0;
	} else if (intf == INTF_DSI_CMD) {
		intf_sel &= ~MDP4_DISP_INTF_SEL_DSI_VIDEO;
		intf_sel |= MDP4_DISP_INTF_SEL_DSI_CMD;
		mdp4_crtc->mixer = 0;
	} else if (intf == INTF_LCDC_DTV){
		mdp4_crtc->mixer = 1;
	}

	blend_setup(crtc);

	DBG("%s: intf_sel=%08x", mdp4_crtc->name, intf_sel);

	mdp4_write(mdp4_kms, REG_MDP4_DISP_INTF_SEL, intf_sel);
}

static void set_attach(struct drm_crtc *crtc, enum mdp4_pipe pipe_id,
		struct drm_plane *plane)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);

	BUG_ON(pipe_id >= ARRAY_SIZE(mdp4_crtc->planes));

	if (mdp4_crtc->planes[pipe_id] == plane)
		return;

	mdp4_crtc->planes[pipe_id] = plane;
	blend_setup(crtc);
	if (mdp4_crtc->enabled && (plane != mdp4_crtc->plane))
		crtc_flush(crtc);
}

void mdp4_crtc_attach(struct drm_crtc *crtc, struct drm_plane *plane)
{
	set_attach(crtc, mdp4_plane_pipe(plane), plane);
}

void mdp4_crtc_detach(struct drm_crtc *crtc, struct drm_plane *plane)
{
	set_attach(crtc, mdp4_plane_pipe(plane), NULL);
}

static const char *dma_names[] = {
		"DMA_P", "DMA_S", "DMA_E",
};

/* initialize crtc */
struct drm_crtc *mdp4_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, int id, int ovlp_id,
		enum mdp4_dma dma_id)
{
	struct drm_crtc *crtc = NULL;
	struct mdp4_crtc *mdp4_crtc;
	int ret;

	mdp4_crtc = kzalloc(sizeof(*mdp4_crtc), GFP_KERNEL);
	if (!mdp4_crtc) {
		ret = -ENOMEM;
		goto fail;
	}

	crtc = &mdp4_crtc->base;

	mdp4_crtc->plane = plane;

	mdp4_crtc->ovlp = ovlp_id;
	mdp4_crtc->dma = dma_id;

	mdp4_crtc->vblank.irqmask = dma2irq(mdp4_crtc->dma);
	mdp4_crtc->vblank.irq = mdp4_crtc_vblank_irq;

	mdp4_crtc->err.irqmask = dma2err(mdp4_crtc->dma);
	mdp4_crtc->err.irq = mdp4_crtc_err_irq;

	snprintf(mdp4_crtc->name, sizeof(mdp4_crtc->name), "%s:%d",
			dma_names[dma_id], ovlp_id);

	spin_lock_init(&mdp4_crtc->cursor.lock);

	ret = drm_flip_work_init(&mdp4_crtc->unref_fb_work, 16,
			"unref fb", unref_fb_worker);
	if (ret)
		goto fail;

	ret = drm_flip_work_init(&mdp4_crtc->unref_cursor_work, 64,
			"unref cursor", unref_cursor_worker);

	INIT_FENCE_CB(&mdp4_crtc->pageflip_cb, pageflip_cb);

	drm_crtc_init(dev, crtc, &mdp4_crtc_funcs);
	drm_crtc_helper_add(crtc, &mdp4_crtc_helper_funcs);

	mdp4_plane_install_properties(mdp4_crtc->plane, &crtc->base);

	return crtc;

fail:
	if (crtc)
		mdp4_crtc_destroy(crtc);

	return ERR_PTR(ret);
}

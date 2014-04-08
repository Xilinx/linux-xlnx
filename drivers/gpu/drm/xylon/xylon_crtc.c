/*
 * Xylon DRM driver CRTC functions
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * Based on Xilinx DRM crtc driver.
 * Copyright (C) 2013 Xilinx, Inc.
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
#include <drm/drm_gem_cma_helper.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>

#include <video/videomode.h>

#include "xylon_crtc.h"
#include "xylon_drv.h"
#include "xylon_logicvc.h"
#include "xylon_logicvc_hw.h"
#include "xylon_logicvc_helper.h"
#include "xylon_plane.h"

#define XYLON_DRM_CRTC_ALPHA_MIN 0
#define XYLON_DRM_CRTC_ALPHA_MAX 255
#define XYLON_DRM_CRTC_COLOR_MIN 0
#define XYLON_DRM_CRTC_COLOR_MAX 0xFFFFFFFF

struct xylon_drm_crtc_properties {
	struct drm_property *ctrl;
	struct drm_property *transparency;
	struct drm_property *transparent_color;
	struct drm_property *bg_color;
};

struct xylon_drm_crtc {
	struct drm_crtc base;
	struct drm_pending_vblank_event *event;
	struct drm_plane *private;
	struct xylon_drm_crtc_properties properties;
	struct xylon_cvc *cvc;
	struct xylon_drm_plane_manager *manager;
	struct clk *pixel_clock;
	struct xylon_cvc_fix fix;
	struct videomode vmode;
	u32 priv_plane_id;
	int dpms;
};

#define to_xylon_crtc(x) container_of(x, struct xylon_drm_crtc, base)

enum xylon_drm_crtc_ctrl {
	XYLON_DRM_CRTC_CTRL_DISABLE = 0,
	XYLON_DRM_CRTC_CTRL_ENABLE,
	XYLON_DRM_CRTC_CTRL_TRANSPARENT_COLOR_DISABLE,
	XYLON_DRM_CRTC_CTRL_TRANSPARENT_COLOR_ENABLE,
	XYLON_DRM_CRTC_CTRL_COLOR_FORMAT_NORMAL,
	XYLON_DRM_CRTC_CTRL_COLOR_FORMAT_ANDROID
};

static const struct drm_prop_enum_list xylon_drm_crtc_ctrls[] = {
	{
		XYLON_DRM_CRTC_CTRL_DISABLE,
		"Disable"
	},
	{
		XYLON_DRM_CRTC_CTRL_ENABLE,
		"Enable"
	},
	{
		XYLON_DRM_CRTC_CTRL_TRANSPARENT_COLOR_DISABLE,
		"Color Transparency Disable"
	},
	{
		XYLON_DRM_CRTC_CTRL_TRANSPARENT_COLOR_ENABLE,
		"Color Transparency Enable"
	},
	{
		XYLON_DRM_CRTC_CTRL_COLOR_FORMAT_NORMAL,
		"Color Format Normal"
	},
	{
		XYLON_DRM_CRTC_CTRL_COLOR_FORMAT_ANDROID,
		"Color Format Android"
	},
};

static int xylon_drm_crtc_clk_set(struct xylon_drm_crtc *crtc)
{
	int ret;

	ret = clk_set_rate(crtc->pixel_clock, crtc->vmode.pixelclock);
	if (ret) {
		DRM_ERROR("failed set pixel clock\n");
		return ret;
	}
	DRM_DEBUG("pixel clock %ld -> %ld\n", crtc->vmode.pixelclock,
		 clk_get_rate(crtc->pixel_clock));

	return 0;
}

static void xylon_drm_crtc_dpms(struct drm_crtc *base_crtc, int dpms)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	if (crtc->dpms == dpms)
		return;

	crtc->dpms = dpms;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
		xylon_drm_plane_dpms(crtc->private, dpms);
		break;
	default:
		xylon_cvc_disable(crtc->cvc);
		break;
	}
}

static void xylon_drm_crtc_prepare(struct drm_crtc *base_crtc)
{
	xylon_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_STANDBY);
}

static void xylon_drm_crtc_commit(struct drm_crtc *base_crtc)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	xylon_drm_crtc_clk_set(crtc);

	xylon_drm_plane_commit(crtc->private);

	xylon_cvc_enable(crtc->cvc, &crtc->vmode);

	xylon_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_ON);
}

static bool xylon_drm_crtc_mode_fixup(struct drm_crtc *base_crtc,
				      const struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	if ((mode->hdisplay >= crtc->fix.hres_min &&
	     mode->hdisplay <= crtc->fix.hres_max) &&
	    (mode->vdisplay >= crtc->fix.vres_min &&
	     mode->vdisplay <= crtc->fix.vres_max))
		return true;

	return false;
}

static int xylon_drm_crtc_mode_set(struct drm_crtc *base_crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode,
				   int x, int y,
				   struct drm_framebuffer *old_fb)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);
	struct drm_display_mode *dm = adjusted_mode;
	int ret;

	crtc->vmode.pixelclock = dm->clock * KHZ;
	crtc->vmode.hactive = dm->hdisplay;
	crtc->vmode.hfront_porch = dm->hsync_start - dm->hdisplay;
	crtc->vmode.hback_porch = dm->htotal - dm->hsync_end;
	crtc->vmode.hsync_len = dm->hsync_end - dm->hsync_start;
	crtc->vmode.vactive = dm->vdisplay;
	crtc->vmode.vfront_porch = dm->vsync_start - dm->vdisplay;
	crtc->vmode.vback_porch = dm->vtotal - dm->vsync_end;
	crtc->vmode.vsync_len = dm->vsync_end - dm->vsync_start;

	ret = xylon_drm_plane_fb_set(crtc->private, base_crtc->fb,
				     0, 0, dm->hdisplay, dm->vdisplay,
				     x, y, dm->hdisplay, dm->vdisplay);
	if (ret) {
		DRM_ERROR("failed set plane mode\n");
		return ret;
	}

	return 0;
}

static int xylon_drm_crtc_mode_set_base(struct drm_crtc *base_crtc,
					int x, int y,
					struct drm_framebuffer *old_fb)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);
	int ret;

	ret = xylon_drm_plane_fb_set(crtc->private, base_crtc->fb,
				     0, 0,
				     base_crtc->hwmode.hdisplay,
				     base_crtc->hwmode.vdisplay,
				     x, y,
				     base_crtc->hwmode.hdisplay,
				     base_crtc->hwmode.vdisplay);
	if (ret) {
		DRM_ERROR("failed set plane mode\n");
		return ret;
	}

	xylon_drm_plane_commit(crtc->private);

	xylon_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_ON);

	return 0;
}

static void xylon_drm_crtc_load_lut(struct drm_crtc *base_crtc)
{
}

static struct drm_crtc_helper_funcs xylon_drm_crtc_helper_funcs = {
	.dpms = xylon_drm_crtc_dpms,
	.prepare = xylon_drm_crtc_prepare,
	.commit = xylon_drm_crtc_commit,
	.mode_fixup = xylon_drm_crtc_mode_fixup,
	.mode_set = xylon_drm_crtc_mode_set,
	.mode_set_base = xylon_drm_crtc_mode_set_base,
	.load_lut = xylon_drm_crtc_load_lut,
};

void xylon_drm_crtc_destroy(struct drm_crtc *base_crtc)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	xylon_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);

	drm_crtc_cleanup(base_crtc);

	clk_disable_unprepare(crtc->pixel_clock);

	xylon_drm_plane_destroy_all(crtc->manager);
	xylon_drm_plane_destroy(crtc->private);
	xylon_drm_plane_remove_manager(crtc->manager);
}

void xylon_drm_crtc_cancel_page_flip(struct drm_crtc *base_crtc,
				     struct drm_file *file)
{
	struct drm_device *dev = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = crtc->event;
	if (event && (event->base.file_priv == file)) {
		crtc->event = NULL;
		event->base.destroy(&event->base);
		drm_vblank_put(dev, 0);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static int xylon_drm_crtc_page_flip(struct drm_crtc *base_crtc,
				    struct drm_framebuffer *fb,
				    struct drm_pending_vblank_event *event,
				    u32 page_flip_flags)
{
	struct drm_device *dev = base_crtc->dev;
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (crtc->event != NULL) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	ret = xylon_drm_plane_fb_set(crtc->private, fb,
				     0, 0,
				     base_crtc->hwmode.hdisplay,
				     base_crtc->hwmode.vdisplay,
				     base_crtc->x, base_crtc->y,
				     base_crtc->hwmode.hdisplay,
				     base_crtc->hwmode.vdisplay);
	if (ret) {
		DRM_ERROR("failed mode set plane\n");
		return ret;
	}

	xylon_drm_plane_commit(crtc->private);

	base_crtc->fb = fb;

	if (event) {
		event->pipe = 0;
		drm_vblank_get(dev, 0);
		spin_lock_irqsave(&dev->event_lock, flags);
		crtc->event = event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	return 0;
}

static int xylon_drm_crtc_set_property(struct drm_crtc *base_crtc,
				       struct drm_property *property,
				       u64 val)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);
	struct xylon_drm_plane_op op;

	if (property == crtc->properties.ctrl) {
		enum xylon_drm_crtc_ctrl ctrl = val;

		switch (ctrl) {
		case XYLON_DRM_CRTC_CTRL_DISABLE:
			xylon_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);
			return 0;
		case XYLON_DRM_CRTC_CTRL_ENABLE:
			xylon_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_ON);
			return 0;
		case XYLON_DRM_CRTC_CTRL_TRANSPARENT_COLOR_DISABLE:
			op.id = XYLON_DRM_PLANE_OP_ID_CTRL;
			op.sid = XYLON_DRM_PLANE_OP_SID_CTRL_COLOR_TRANSPARENCY,
			op.param = XYLON_DRM_PLANE_OP_DISABLE;
			break;
		case XYLON_DRM_CRTC_CTRL_TRANSPARENT_COLOR_ENABLE:
			op.id = XYLON_DRM_PLANE_OP_ID_CTRL;
			op.sid = XYLON_DRM_PLANE_OP_SID_CTRL_COLOR_TRANSPARENCY,
			op.param = XYLON_DRM_PLANE_OP_ENABLE;
			break;
		case XYLON_DRM_CRTC_CTRL_COLOR_FORMAT_NORMAL:
			op.id = XYLON_DRM_PLANE_OP_ID_CTRL;
			op.sid = XYLON_DRM_PLANE_OP_SID_CTRL_PIXEL_FORMAT,
			op.param = XYLON_DRM_PLANE_OP_PIXEL_FORMAT_NORMAL;
			break;
		case XYLON_DRM_CRTC_CTRL_COLOR_FORMAT_ANDROID:
			op.id = XYLON_DRM_PLANE_OP_ID_CTRL;
			op.sid = XYLON_DRM_PLANE_OP_SID_CTRL_PIXEL_FORMAT,
			op.param = XYLON_DRM_PLANE_OP_PIXEL_FORMAT_ANDROID;
			break;
		default:
			return -EINVAL;
		}
	} else if (property == crtc->properties.transparency) {
		op.id = XYLON_DRM_PLANE_OP_ID_TRANSPARENCY;
		op.sid = XYLON_DRM_PLANE_OP_SID_NONE,
		op.param = (u32)val;
	} else if (property == crtc->properties.transparent_color) {
		op.id = XYLON_DRM_PLANE_OP_ID_TRANSPARENT_COLOR;
		op.sid = XYLON_DRM_PLANE_OP_SID_NONE,
		op.param = (u32)val;
	} else if (property == crtc->properties.bg_color) {
		op.id = XYLON_DRM_PLANE_OP_ID_BACKGORUND_COLOR;
		op.sid = XYLON_DRM_PLANE_OP_SID_NONE,
		op.param = (u32)val;
	} else {
		return -EINVAL;
	}

	xylon_drm_plane_op(crtc->private, &op);

	return 0;
}

static struct drm_crtc_funcs xylon_drm_crtc_funcs = {
	.destroy = xylon_drm_crtc_destroy,
	.set_config = drm_crtc_helper_set_config,
	.page_flip = xylon_drm_crtc_page_flip,
	.set_property = xylon_drm_crtc_set_property,
};

static void xylon_drm_crtc_vblank_handler(struct drm_crtc *base_crtc)
{
	struct drm_device *dev = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);
	unsigned long flags;

	drm_handle_vblank(dev, 0);

	spin_lock_irqsave(&dev->event_lock, flags);
	event = crtc->event;
	crtc->event = NULL;
	if (event) {
		drm_send_vblank_event(dev, 0, event);
		drm_vblank_put(dev, 0);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

void xylon_drm_crtc_vblank(struct drm_crtc *base_crtc, bool enabled)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	xylon_cvc_int_state(crtc->cvc, LOGICVC_INT_V_SYNC, enabled);
}

void xylon_drm_crtc_int_handle(struct drm_crtc *base_crtc)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);
	u32 active = xylon_cvc_int_get_active(crtc->cvc);
	u32 handled = 0;

	if (active & LOGICVC_INT_V_SYNC) {
		xylon_drm_crtc_vblank_handler(base_crtc);
		handled |= LOGICVC_INT_V_SYNC;
	}

	xylon_cvc_int_clear_active(crtc->cvc, handled);
}

void xylon_drm_crtc_int_hw_enable(struct drm_crtc *base_crtc)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	xylon_cvc_int_hw_enable(crtc->cvc);
}

void xylon_drm_crtc_int_hw_disable(struct drm_crtc *base_crtc)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	xylon_cvc_int_hw_disable(crtc->cvc);
}

int xylon_drm_crtc_int_request(struct drm_crtc *base_crtc, unsigned long flags,
			       irq_handler_t handler, void *dev)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	return xylon_cvc_int_request(crtc->cvc, flags, handler, dev);
}

void xylon_drm_crtc_int_free(struct drm_crtc *base_crtc, void *dev)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	xylon_cvc_int_free(crtc->cvc, dev);
}

void xylon_drm_crtc_get_fix_parameters(struct drm_crtc *base_crtc)
{
	struct drm_device *dev = base_crtc->dev;
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	xylon_cvc_get_fix_parameters(crtc->cvc, &crtc->fix);

	dev->mode_config.min_width = crtc->fix.x_min;
	dev->mode_config.min_height = crtc->fix.y_min;
	dev->mode_config.max_width = crtc->fix.x_max;
	dev->mode_config.max_height = crtc->fix.y_max;
}

int xylon_drm_crtc_get_bits_per_pixel(struct drm_crtc *base_crtc)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	return xylon_drm_plane_get_bits_per_pixel(crtc->private);
}

bool xylon_drm_crtc_check_format(struct drm_crtc *base_crtc, u32 fourcc)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	return xylon_drm_plane_check_format(crtc->manager, fourcc);
}

int xylon_drm_crtc_get_param(struct drm_crtc *base_crtc, unsigned int *p,
			     int cmd)
{
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);

	if (crtc->fix.x_max == 0)
		return -ENODEV;

	switch (cmd) {
	case XYLON_DRM_CRTC_BUFF_WIDTH:
		*p = crtc->fix.x_max;
		break;
	case XYLON_DRM_CRTC_BUFF_HEIGHT:
		*p = crtc->fix.y_max;
		break;
	}

	return 0;
}

static int xylon_drm_crtc_create_properties(struct drm_crtc *base_crtc)
{
	struct drm_device *dev = base_crtc->dev;
	struct drm_property *prop;
	struct xylon_drm_crtc *crtc = to_xylon_crtc(base_crtc);
	int size;

	prop = crtc->properties.ctrl;
	if (!prop) {
		size = ARRAY_SIZE(xylon_drm_crtc_ctrls);
		prop = drm_property_create_enum(dev, 0, "control",
						xylon_drm_crtc_ctrls, size);
		if (!prop)
			return -EINVAL;

		drm_object_attach_property(&base_crtc->base, prop, 0);

		crtc->properties.ctrl = prop;
	}
	prop = crtc->properties.transparency;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "transparency",
						 XYLON_DRM_CRTC_ALPHA_MIN,
						 XYLON_DRM_CRTC_ALPHA_MAX);
		if (!prop)
			goto err_property;

		drm_object_attach_property(&base_crtc->base, prop,
					   XYLON_DRM_CRTC_ALPHA_MAX);

		crtc->properties.transparency = prop;
	}
	prop = crtc->properties.transparent_color;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "transparent_color",
						 XYLON_DRM_CRTC_COLOR_MIN,
						 XYLON_DRM_CRTC_COLOR_MAX);
		if (!prop)
			goto err_property;

		drm_object_attach_property(&base_crtc->base, prop,
					   XYLON_DRM_CRTC_COLOR_MIN);

		crtc->properties.transparent_color = prop;
	}
	prop = crtc->properties.bg_color;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "background_color",
						 XYLON_DRM_CRTC_COLOR_MIN,
						 XYLON_DRM_CRTC_COLOR_MAX);
		if (!prop)
			goto err_property;

		drm_object_attach_property(&base_crtc->base, prop,
					   XYLON_DRM_CRTC_COLOR_MIN);

		crtc->properties.bg_color = prop;
	}

	return 0;

err_property:
	if (crtc->properties.transparent_color)
		drm_property_destroy(dev, crtc->properties.transparent_color);
	if (crtc->properties.transparency)
		drm_property_destroy(dev, crtc->properties.transparency);
	if (crtc->properties.ctrl)
		drm_property_destroy(dev, crtc->properties.ctrl);

	return -EINVAL;
}

struct drm_crtc *xylon_drm_crtc_create(struct drm_device *dev)
{
	struct device_node *sub_node;
	struct xylon_drm_crtc *crtc;
	int ret;

	sub_node = of_parse_phandle(dev->dev->of_node, "device", 0);
	if (!sub_node) {
		DRM_ERROR("failed get logicvc\n");
		return ERR_PTR(-ENODEV);
	}

	crtc = devm_kzalloc(dev->dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return ERR_PTR(-ENOMEM);

	crtc->cvc = xylon_cvc_probe(dev->dev, sub_node);
	of_node_put(sub_node);
	if (IS_ERR(crtc->cvc)) {
		DRM_ERROR("failed probe logicvc\n");
		return ERR_CAST(crtc->cvc);
	}

	crtc->manager = xylon_drm_plane_probe_manager(dev, crtc->cvc);
	if (IS_ERR(crtc->manager)) {
		DRM_ERROR("failed probe plane manager\n");
		return ERR_CAST(crtc->manager);
	}

	ret = of_property_read_u32(dev->dev->of_node, "private-plane",
				   &crtc->priv_plane_id);
	if (ret)
		DRM_INFO("no private-plane property\n");

	crtc->private = xylon_drm_plane_create(crtc->manager, 1, true,
					       crtc->priv_plane_id);
	if (IS_ERR(crtc->private)) {
		DRM_ERROR("failed create private plane for crtc\n");
		ret = PTR_ERR(crtc->private);
		goto err_plane;
	}

	xylon_drm_plane_create_all(crtc->manager, 1);

	crtc->pixel_clock = devm_clk_get(dev->dev, NULL);
	if (IS_ERR(crtc->pixel_clock)) {
		DRM_ERROR("failed get pixel clock\n");
		ret = -EPROBE_DEFER;
		goto err_out;
	}

	ret = clk_prepare_enable(crtc->pixel_clock);
	if (ret) {
		DRM_ERROR("failed prepare/enable clock\n");
		goto err_out;
	}

	ret = drm_crtc_init(dev, &crtc->base, &xylon_drm_crtc_funcs);
	if (ret) {
		DRM_ERROR("failed initialize crtc\n");
		goto err_out;
	}
	drm_crtc_helper_add(&crtc->base, &xylon_drm_crtc_helper_funcs);

	ret = xylon_drm_crtc_create_properties(&crtc->base);
	if (ret) {
		DRM_ERROR("failed initialize crtc properties\n");
		goto err_out;
	}

	return &crtc->base;

err_out:
	xylon_drm_plane_destroy_all(crtc->manager);
	xylon_drm_plane_destroy(crtc->private);
err_plane:
	xylon_drm_plane_remove_manager(crtc->manager);

	return ERR_PTR(ret);
}

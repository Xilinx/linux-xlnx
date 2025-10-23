// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx logicore audio / video pattern generator driver
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 *
 * This driver introduces support for the test CRTC based on AMD/Xilinx
 * Audio / Video Test Pattern Generator IP. The main goal of the driver is to
 * enable simplistic FPGA design that could be used to test FPGA CRTC to
 * external encoder IP connectivity.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/gpio/consumer.h>
#include <linux/hrtimer.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <video/videomode.h>

#include "xlnx_bridge.h"

#define DRIVER_NAME	"xlnx-avpg"
#define DRIVER_DESC	"Xilinx AV Pattern Generator DRM KMS Driver"
#define DRIVER_DATE	"20251009"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#define XLNX_AVPG_ENABLE		0x0000
#define XLNX_AVPG_VRES			0x001c
#define XLNX_AVPG_HRES			0x002c
#define XLNX_AVPG_MISC0			0x0300
#define XLNX_AVPG_MISC2			0x0308

#define XLNX_AVPG_START			BIT(0)
#define XLNX_AVPG_STOP			(0)
#define XLNX_AVPG_FORMAT_MASK		GENMASK(2, 1)
#define XLNX_AVPG_BPC_MASK		GENMASK(7, 5)
#define XLNX_AVPG_PATTERN_MASK		GENMASK(2, 0)
#define XLNX_AVPG_PPC_MASK		GENMASK(9, 8)

enum xlnx_avpg_pixel_format {
	XLNX_AVPG_FMT_RGB = 0,
	XLNX_AVPG_FMT_YUV_422,
	XLNX_AVPG_FMT_INVALID,
};

enum xlnx_avpg_bpc {
	XLNX_AVPG_6BPC = 0,
	XLNX_AVPG_8BPC,
	XLNX_AVPG_10BPC,
	XLNX_AVPG_12BPC,
	XLNX_AVPG_16BPC,
};

enum xlnx_avpg_pattern {
	XLNX_AVPG_PAT_COLOR_RAMP = 1,
	XLNX_AVPG_PAT_BW_VERT_LINES,
	XLNX_AVPG_PAT_COLOR_SQUARE,
	XLNX_AVPG_PAT_SOLID_RED,
	XLNX_AVPG_PAT_SOLID_GREEN,
	XLNX_AVPG_PAT_SOLID_BLUE,
	XLNX_AVPG_PAT_SOLID_YELLOW,
};

static const struct drm_prop_enum_list xlnx_avpg_pattern_list[] = {
	{ XLNX_AVPG_PAT_COLOR_RAMP, "color-ramp" },
	{ XLNX_AVPG_PAT_BW_VERT_LINES, "lines" },
	{ XLNX_AVPG_PAT_COLOR_SQUARE, "color-square" },
	{ XLNX_AVPG_PAT_SOLID_RED, "red" },
	{ XLNX_AVPG_PAT_SOLID_GREEN, "green"},
	{ XLNX_AVPG_PAT_SOLID_BLUE, "blue" },
	{ XLNX_AVPG_PAT_SOLID_YELLOW, "yellow" },
};

enum xlnx_avpg_ppc {
	XLNX_AVPG_1PPC = 0,
	XLNX_AVPG_2PPC,
	XLNX_AVPG_4PPC,
};

struct xlnx_avpg;

/**
 * struct xlnx_avpg_drm - AVPG CRTC DRM/KMS data
 * @avpg: Back pointer to parent AVPG
 * @dev: DRM device
 * @crtc: DRM CRTC
 * @plane: DRM primary plane
 * @encoder: DRM encoder
 * @connector: DRM connector
 * @pattern_prop: DRM property representing TPG video pattern
 * @event: Pending DRM VBLANK event
 */
struct xlnx_avpg_drm {
	struct xlnx_avpg		*avpg;
	struct drm_device		dev;
	struct drm_crtc			crtc;
	struct drm_plane		plane;
	struct drm_encoder		encoder;
	struct drm_connector		*connector;
	struct drm_property		*pattern_prop;
	struct drm_pending_vblank_event	*event;
};

/**
 * struct xlnx_avpg - AV Pattern Generator data
 * @pdev: Platform device
 * @drm: AVPG DRM data
 * @vtc: Video timing controller interface
 * @disp_bridge: DRM display bridge
 * @regs: Mapped TPG IP register space
 * @gpio_en_avpg: AVPG enable GPIO
 * @gpio_en_vtc: VTC enable GPIO
 * @axi_clk: AXI bus clock
 * @video_clk: Video output clock
 * @output_bus_format: Chosen AVPG output bus format
 * @pixel_format: AVPG pixel format
 * @pixels_per_clock: pixels per clock
 * @bits_per_component: bits per color component
 * @pattern: video pattern to generate
 * @timer: vsync timer
 * @period: vsync timer period
 */
struct xlnx_avpg {
	struct platform_device		*pdev;
	struct xlnx_avpg_drm		*drm;
	struct xlnx_bridge		*vtc;
	struct drm_bridge		*disp_bridge;
	void __iomem			*regs;
	struct gpio_desc		*gpio_en_avpg;
	struct gpio_desc		*gpio_en_vtc;
	struct clk			*axi_clk;
	struct clk			*video_clk;
	u32				output_bus_format;
	enum xlnx_avpg_pixel_format	pixel_format;
	enum xlnx_avpg_ppc		pixels_per_clock;
	enum xlnx_avpg_bpc		bits_per_component;
	enum xlnx_avpg_pattern		pattern;
	/* Poor man's VBLANK */
	struct hrtimer			timer;
	ktime_t				period;
};

static inline struct xlnx_avpg *timer_to_avpg(struct hrtimer *timer)
{
	return container_of(timer, struct xlnx_avpg, timer);
}

static inline struct xlnx_avpg *crtc_to_avpg(struct drm_crtc *crtc)
{
	return container_of(crtc, struct xlnx_avpg_drm, crtc)->avpg;
}

static inline struct xlnx_avpg *plane_to_avpg(struct drm_plane *plane)
{
	return container_of(plane, struct xlnx_avpg_drm, plane)->avpg;
}

static inline struct xlnx_avpg *encoder_to_avpg(struct drm_encoder *encoder)
{
	return container_of(encoder, struct xlnx_avpg_drm, encoder)->avpg;
}

/* -----------------------------------------------------------------------------
 * VSYNC Timer
 */

static enum hrtimer_restart xlnx_avpg_timer_cb(struct hrtimer *timer)
{
	struct xlnx_avpg *avpg = timer_to_avpg(timer);

	drm_crtc_handle_vblank(&avpg->drm->crtc);
	hrtimer_forward_now(&avpg->timer, avpg->period);

	return HRTIMER_RESTART;
}

/* -----------------------------------------------------------------------------
 * Video format mapping
 */

struct xlnx_avpg_format_map {
	u32 bus_format;
	enum xlnx_avpg_pixel_format pixel_format;
	enum xlnx_avpg_bpc bpc;
};

/**
 * xlnx_avpg_find_bus_format - Find media bus format based on AVPG pixel format
 * and bits per color
 *
 * @pixel_format: AVPG pixel format
 * @bpc: bits per color component
 *
 * Return: Media bus format that matches @pixel_format and @bpc or 0 if given
 * pixel format and bpc combo is not supported
 */
static u32 xlnx_avpg_find_bus_format(enum xlnx_avpg_pixel_format pixel_format,
				     enum xlnx_avpg_bpc bpc)
{
	static const struct xlnx_avpg_format_map format_map[] = {
		{
			.bus_format = MEDIA_BUS_FMT_RGB121212_1X36,
			.pixel_format = XLNX_AVPG_FMT_RGB,
			.bpc = XLNX_AVPG_12BPC,
		},
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(format_map); ++i) {
		if (format_map[i].pixel_format == pixel_format &&
		    format_map[i].bpc == bpc)
			return format_map[i].bus_format;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * TPG IP ops
 */

static void xlnx_avpg_write(struct xlnx_avpg *avpg, int offset, u32 val)
{
	writel(val, avpg->regs + offset);
}

static u32 xlnx_avpg_read(struct xlnx_avpg *avpg, int offset)
{
	return readl(avpg->regs + offset);
}

/**
 * xlnx_avpg_set_mode - Set AVPG output signal dimensions and timing
 * @avpg: The AVPG
 * @vm: video mode to set
 */
static void xlnx_avpg_set_mode(struct xlnx_avpg *avpg,
			       const struct videomode *vm)
{
	xlnx_avpg_write(avpg, XLNX_AVPG_VRES, vm->vactive);
	xlnx_avpg_write(avpg, XLNX_AVPG_HRES, vm->hactive);
}

/**
 * xlnx_avpg_set_pattern - Set AVPG output video pattern
 * @avpg: The AVPG
 */
static void xlnx_avpg_set_pattern(struct xlnx_avpg *avpg)
{
	u32 reg_val = xlnx_avpg_read(avpg, XLNX_AVPG_MISC2);

	reg_val &= ~XLNX_AVPG_PATTERN_MASK;
	reg_val |= FIELD_PREP(XLNX_AVPG_PATTERN_MASK, avpg->pattern);
	xlnx_avpg_write(avpg, XLNX_AVPG_MISC2, reg_val);
}

/**
 * xlnx_avpg_set_format - Set AVPG output video color format
 * @avpg: The AVPG
 */
static void xlnx_avpg_set_format(struct xlnx_avpg *avpg)
{
	u32 reg_val = FIELD_PREP(XLNX_AVPG_FORMAT_MASK, avpg->pixel_format) |
		      FIELD_PREP(XLNX_AVPG_BPC_MASK, avpg->bits_per_component);
	xlnx_avpg_write(avpg, XLNX_AVPG_MISC0, reg_val);

	reg_val = xlnx_avpg_read(avpg, XLNX_AVPG_MISC2);
	reg_val &= ~XLNX_AVPG_PPC_MASK;
	reg_val |= FIELD_PREP(XLNX_AVPG_PPC_MASK, avpg->pixels_per_clock);
	xlnx_avpg_write(avpg, XLNX_AVPG_MISC2, reg_val);
}

/**
 * xlnx_avpg_start - Start generation of the video signal
 * @avpg: The AVPG
 */
static void xlnx_avpg_start(struct xlnx_avpg *avpg)
{
	xlnx_avpg_write(avpg, XLNX_AVPG_ENABLE, XLNX_AVPG_START);
}

/**
 * xlnx_avpg_stop - Stop generation of the video signal
 * @avpg: The AVPG
 */
static void xlnx_avpg_stop(struct xlnx_avpg *avpg)
{
	xlnx_avpg_write(avpg, XLNX_AVPG_ENABLE, XLNX_AVPG_STOP);
}

/**
 * xlnx_avpg_map_resources - Map AVPG register space
 * @avpg: The AVPG
 *
 * Return: 0 on success or error code
 */
static int xlnx_avpg_map_resources(struct xlnx_avpg *avpg)
{
	int ret;

	avpg->regs = devm_platform_ioremap_resource(avpg->pdev, 0);
	if (IS_ERR(avpg->regs)) {
		ret = PTR_ERR(avpg->regs);
		dev_err(&avpg->pdev->dev, "failed to map register space\n");
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * DRM plane
 */

static int xlnx_avpg_plane_atomic_check(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct xlnx_avpg *avpg = plane_to_avpg(plane);
	struct drm_crtc_state *crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(state, &avpg->drm->crtc);

	/* Force CRTC shutdown when the plane is detached */
	if (crtc_state->enable && !plane_state->crtc)
		return -EINVAL;

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static void xlnx_avpg_plane_atomic_update(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	/* Nothing to do here but the callback is mandatory */
}

static const struct drm_plane_helper_funcs xlnx_avpg_plane_helper_funcs = {
	.prepare_fb	= drm_gem_plane_helper_prepare_fb,
	.atomic_check	= xlnx_avpg_plane_atomic_check,
	.atomic_update	= xlnx_avpg_plane_atomic_update,
};

static bool xlnx_avpg_format_mod_supported(struct drm_plane *plane,
					   u32 format, u64 modifier)
{
	return modifier == DRM_FORMAT_MOD_LINEAR;
}

static int xlnx_avpg_plane_set_property(struct drm_plane *plane,
					struct drm_plane_state *state,
					struct drm_property *property,
					u64 val)
{
	struct xlnx_avpg *avpg = plane_to_avpg(plane);

	if (property == avpg->drm->pattern_prop) {
		avpg->pattern = val;
		xlnx_avpg_set_pattern(avpg);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int xlnx_avpg_plane_get_property(struct drm_plane *plane,
					const struct drm_plane_state *state,
					struct drm_property *property,
					uint64_t *val)
{
	struct xlnx_avpg *avpg = plane_to_avpg(plane);

	if (property == avpg->drm->pattern_prop)
		*val = avpg->pattern;
	else
		return -EINVAL;

	return 0;
}

static const struct drm_plane_funcs xlnx_avpg_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.format_mod_supported	= xlnx_avpg_format_mod_supported,
	.atomic_set_property	= xlnx_avpg_plane_set_property,
	.atomic_get_property	= xlnx_avpg_plane_get_property,
};

/**
 * xlnx_avpg_create_properties - Create AVPG DRM properties
 * @avpg: The AVPG
 */
static void xlnx_avpg_create_properties(struct xlnx_avpg *avpg)
{
	struct drm_device *drm = &avpg->drm->dev;
	struct drm_mode_object *obj = &avpg->drm->plane.base;

	avpg->drm->pattern_prop =
		drm_property_create_enum(drm, 0, "pattern",
					 xlnx_avpg_pattern_list,
					 ARRAY_SIZE(xlnx_avpg_pattern_list));
	drm_object_attach_property(obj, avpg->drm->pattern_prop,
				   XLNX_AVPG_PAT_COLOR_RAMP);
	avpg->pattern = XLNX_AVPG_PAT_COLOR_RAMP;
}

/* -----------------------------------------------------------------------------
 * DRM CRTC
 */

static enum drm_mode_status
xlnx_avpg_crtc_mode_valid(struct drm_crtc *crtc,
			  const struct drm_display_mode *mode)
{
	return MODE_OK;
}

static void xlnx_avpg_crtc_begin(struct drm_crtc *crtc,
				 struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
}

static int xlnx_avpg_crtc_check(struct drm_crtc *crtc,
				struct drm_atomic_state *state)
{
	struct xlnx_avpg *avpg = crtc_to_avpg(crtc);
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);

	if (crtc_state->enable &&
	    avpg->output_bus_format != crtc_state->output_bus_format)
		return -EINVAL;

	return drm_atomic_add_affected_planes(state, crtc);
}

static void xlnx_avpg_crtc_enable(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct videomode vm;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct xlnx_avpg *avpg = crtc_to_avpg(crtc);
	int ret;

	pm_runtime_get_sync(&avpg->pdev->dev);

	ret = clk_prepare_enable(avpg->video_clk);
	if (ret < 0) {
		dev_err(&avpg->pdev->dev,
			"failed to enable video clock: %d\n", ret);
		return;
	}

	drm_display_mode_to_videomode(mode, &vm);

	gpiod_set_value_cansleep(avpg->gpio_en_vtc, 1);
	if (avpg->vtc) {
		xlnx_bridge_set_timing(avpg->vtc, &vm);
		xlnx_bridge_enable(avpg->vtc);
	}

	xlnx_avpg_set_mode(avpg, &vm);
	xlnx_avpg_set_format(avpg);
	xlnx_avpg_set_pattern(avpg);
	xlnx_avpg_start(avpg);
	gpiod_set_value_cansleep(avpg->gpio_en_avpg, 1);
}

static void xlnx_avpg_crtc_disable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct xlnx_avpg *avpg = crtc_to_avpg(crtc);

	xlnx_avpg_stop(avpg);

	if (avpg->vtc)
		xlnx_bridge_disable(avpg->vtc);

	gpiod_set_value_cansleep(avpg->gpio_en_vtc, 0);
	gpiod_set_value_cansleep(avpg->gpio_en_avpg, 0);

	drm_crtc_vblank_off(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	pm_runtime_put_sync(&avpg->pdev->dev);
}

static void xlnx_avpg_crtc_flush(struct drm_crtc *crtc,
				 struct drm_atomic_state *state)
{
	struct drm_pending_vblank_event *vblank;

	if (!crtc->state->event)
		return;

	vblank = crtc->state->event;
	crtc->state->event = NULL;

	vblank->pipe = drm_crtc_index(crtc);

	WARN_ON(drm_crtc_vblank_get(crtc) != 0);

	spin_lock_irq(&crtc->dev->event_lock);
	drm_crtc_arm_vblank_event(crtc, vblank);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static u32
xlnx_avpg_crtc_select_output_bus_format(struct drm_crtc *crtc,
					struct drm_crtc_state *crtc_state,
					const u32 *in_bus_fmts,
					unsigned int num_in_bus_fmts)
{
	struct xlnx_avpg *avpg = crtc_to_avpg(crtc);
	unsigned int i;

	for (i = 0; i < num_in_bus_fmts; ++i) {
		if (in_bus_fmts[i] == avpg->output_bus_format)
			return avpg->output_bus_format;
	}

	return 0;
}

static const struct drm_crtc_helper_funcs xlnx_avpg_crtc_helper_funcs = {
	.mode_valid = xlnx_avpg_crtc_mode_valid,
	.atomic_begin = xlnx_avpg_crtc_begin,
	.atomic_check = xlnx_avpg_crtc_check,
	.atomic_enable = xlnx_avpg_crtc_enable,
	.atomic_disable = xlnx_avpg_crtc_disable,
	.atomic_flush = xlnx_avpg_crtc_flush,
	.select_output_bus_format = xlnx_avpg_crtc_select_output_bus_format,
};

static int xlnx_avpg_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct xlnx_avpg *avpg = crtc_to_avpg(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	int vrefresh = drm_mode_vrefresh(mode);

	avpg->period = ktime_set(0, NSEC_PER_SEC / vrefresh);
	hrtimer_start(&avpg->timer, avpg->period, HRTIMER_MODE_REL);

	return 0;
}

static void xlnx_avpg_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct xlnx_avpg *avpg = crtc_to_avpg(crtc);

	hrtimer_cancel(&avpg->timer);
}

static const struct drm_crtc_funcs xlnx_avpg_crtc_funcs = {
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= xlnx_avpg_crtc_enable_vblank,
	.disable_vblank		= xlnx_avpg_crtc_disable_vblank,
};

/* -----------------------------------------------------------------------------
 * Setup & Init
 */

/**
 * xlnx_avpg_pipeline_init - Initialize DRM pipeline
 * @drm: DRM device
 *
 * Create and link CRTC, plane, and encoder. Attach external DRM bridge.
 *
 * Return: 0 on success, or a negative error code otherwise
 */
static int xlnx_avpg_pipeline_init(struct drm_device *drm)
{
	static const u32 xlnx_avpg_formats[] = {
		DRM_FORMAT_XRGB8888,
	};
	static const u64 xlnx_avpg_modifiers[] = {
		DRM_FORMAT_MOD_LINEAR,
		DRM_FORMAT_MOD_INVALID,
	};
	struct xlnx_avpg *avpg = dev_get_drvdata(drm->dev);
	struct drm_connector *connector;
	struct drm_encoder *encoder = &avpg->drm->encoder;
	struct drm_plane *plane = &avpg->drm->plane;
	struct drm_crtc *crtc = &avpg->drm->crtc;
	int ret;

	ret = xlnx_avpg_map_resources(avpg);
	if (ret < 0)
		return ret;

	drm_plane_helper_add(plane, &xlnx_avpg_plane_helper_funcs);
	ret = drm_universal_plane_init(drm, plane, 0,
				       &xlnx_avpg_plane_funcs,
				       xlnx_avpg_formats,
				       ARRAY_SIZE(xlnx_avpg_formats),
				       xlnx_avpg_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		dev_err(drm->dev, "failed to init plane: %d\n", ret);
		return ret;
	}

	drm_crtc_helper_add(crtc, &xlnx_avpg_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(drm, crtc, plane, NULL,
					&xlnx_avpg_crtc_funcs, NULL);
	if (ret) {
		dev_err(drm->dev, "failed to init crtc: %d\n", ret);
		return ret;
	}
	drm_crtc_vblank_off(crtc);

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	ret = drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_NONE);
	if (ret) {
		dev_err(drm->dev, "failed to init encoder: %d\n", ret);
		return ret;
	}

	ret = drm_bridge_attach(encoder, avpg->disp_bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret < 0) {
		dev_err(drm->dev, "failed to attach bridge to encoder: %d\n",
			ret);
		return ret;
	}

	connector = drm_bridge_connector_init(drm, encoder);
	if (IS_ERR(connector)) {
		ret = PTR_ERR(connector);
		dev_err(drm->dev, "failed to init connector: %d\n", ret);
		return ret;
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		dev_err(drm->dev, "failed to attach encoder: %d\n", ret);
		return ret;
	}

	xlnx_avpg_create_properties(avpg);

	return 0;
}

static const struct drm_mode_config_funcs xlnx_avpg_mode_config_funcs = {
	.fb_create	= drm_gem_fb_create,
	.atomic_check	= drm_atomic_helper_check,
	.atomic_commit	= drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(xlnx_avpg_gem_fops);
static struct drm_driver xlnx_avpg_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops			= &xlnx_avpg_gem_fops,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
	DRM_GEM_SHMEM_DRIVER_OPS,
};

/**
 * xlnx_avpg_drm_init - Initialize DRM device
 * @dev: The device
 *
 * Allocate and initialize DRM device. Configure mode config and initialize
 * AVPG DRM pipeline.
 *
 * Return: 0 on success, or a negative error code otherwise
 */
static int xlnx_avpg_drm_init(struct device *dev)
{
	struct xlnx_avpg *avpg = dev_get_drvdata(dev);
	struct drm_device *drm;
	int ret;

	avpg->drm = devm_drm_dev_alloc(dev, &xlnx_avpg_drm_driver,
				       struct xlnx_avpg_drm, dev);
	if (IS_ERR(avpg->drm))
		return PTR_ERR(avpg->drm);

	avpg->drm->avpg = avpg;
	drm = &avpg->drm->dev;

	ret = drm_mode_config_init(drm);
	if (ret < 0)
		return ret;

	avpg->drm->dev.mode_config.funcs = &xlnx_avpg_mode_config_funcs;
	avpg->drm->dev.mode_config.min_width = 0;
	avpg->drm->dev.mode_config.min_height = 0;
	avpg->drm->dev.mode_config.max_width = 8192;
	avpg->drm->dev.mode_config.max_height = 8192;

	ret = drm_vblank_init(drm, 1);
	if (ret < 0)
		return ret;

	drm_kms_helper_poll_init(drm);

	ret = xlnx_avpg_pipeline_init(drm);
	if (ret < 0)
		goto err_poll_fini;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_poll_fini;

	return ret;

err_poll_fini:
	drm_kms_helper_poll_fini(drm);

	return ret;
}

/**
 * xlnx_avpg_drm_fini - Finalize DRM device
 * @dev: The device
 */
static void xlnx_avpg_drm_fini(struct device *dev)
{
	struct xlnx_avpg *avpg = dev_get_drvdata(dev);
	struct xlnx_avpg_drm *drm = avpg->drm;

	drm_dev_unregister(&drm->dev);
	drm_atomic_helper_shutdown(&drm->dev);
	drm_encoder_cleanup(&drm->encoder);
	drm_kms_helper_poll_fini(&drm->dev);
	of_xlnx_bridge_put(avpg->vtc);
}

static int xlnx_avpg_probe(struct platform_device *pdev)
{
	struct xlnx_avpg *avpg;
	struct device_node *node, *vtc_node;
	u32 ppc, bpc;
	int ret;

	avpg = devm_kzalloc(&pdev->dev, sizeof(*avpg), GFP_KERNEL);
	if (!avpg)
		return -ENOMEM;

	avpg->pdev = pdev;
	platform_set_drvdata(pdev, avpg);
	node = pdev->dev.of_node;

	avpg->axi_clk = devm_clk_get_enabled(&pdev->dev, "av_axi_aclk");
	if (IS_ERR(avpg->axi_clk)) {
		dev_err(&pdev->dev, "failed to get axi clock\n");
		return PTR_ERR(avpg->axi_clk);
	}

	avpg->video_clk = devm_clk_get(&pdev->dev, "vid_out_axi4s_aclk");
	if (IS_ERR(avpg->video_clk)) {
		dev_err(&pdev->dev, "failed to get video clock\n");
		return PTR_ERR(avpg->video_clk);
	}

	avpg->disp_bridge = devm_drm_of_get_bridge(&pdev->dev, node, 0, 0);
	if (IS_ERR(avpg->disp_bridge)) {
		ret = PTR_ERR(avpg->disp_bridge);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to discover display bridge\n");
		return ret;
	}

	avpg->gpio_en_avpg = devm_gpiod_get_index(&pdev->dev, "clk-enable", 0,
						  GPIOD_ASIS);
	if (IS_ERR(avpg->gpio_en_avpg)) {
		ret = PTR_ERR(avpg->gpio_en_avpg);
		dev_err(&pdev->dev, "failed to get avpg en gpio: %d\n", ret);
		return ret;
	}

	avpg->gpio_en_vtc = devm_gpiod_get_index(&pdev->dev, "clk-enable", 1,
						 GPIOD_ASIS);
	if (IS_ERR(avpg->gpio_en_vtc)) {
		ret = PTR_ERR(avpg->gpio_en_vtc);
		dev_err(&pdev->dev, "failed to get vtc en gpio: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,ppc", &ppc);
	if (ret < 0) {
		dev_err(&pdev->dev, "required ppc property is missing\n");
		return ret;
	}
	switch (ppc) {
	case 1:
		avpg->pixels_per_clock = XLNX_AVPG_1PPC;
		break;
	case 2:
		avpg->pixels_per_clock = XLNX_AVPG_2PPC;
		break;
	case 4:
		avpg->pixels_per_clock = XLNX_AVPG_4PPC;
		break;
	default:
		dev_err(&pdev->dev, "%d ppc not supported\n", ppc);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,bpc", &bpc);
	if (ret < 0) {
		dev_err(&pdev->dev, "required bpc property is missing\n");
		return ret;
	}
	switch (bpc) {
	case 6:
		avpg->bits_per_component = XLNX_AVPG_6BPC;
		break;
	case 8:
		avpg->bits_per_component = XLNX_AVPG_8BPC;
		break;
	case 10:
		avpg->bits_per_component = XLNX_AVPG_10BPC;
		break;
	case 12:
		avpg->bits_per_component = XLNX_AVPG_12BPC;
		break;
	case 16:
		avpg->bits_per_component = XLNX_AVPG_16BPC;
		break;
	default:
		dev_err(&pdev->dev, "%d bpc not supported\n", bpc);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,video-format",
				   &avpg->pixel_format);
	if (ret < 0) {
		dev_err(&pdev->dev, "video-format property is missing\n");
		return ret;
	}
	avpg->output_bus_format =
		xlnx_avpg_find_bus_format(avpg->pixel_format,
					  avpg->bits_per_component);
	if (!avpg->output_bus_format) {
		dev_err(&pdev->dev, "unsupported format / bpc combo\n");
		return -EINVAL;
	}

	ret = xlnx_avpg_drm_init(&pdev->dev);
	if (ret < 0)
		return ret;

	vtc_node = of_parse_phandle(node, "xlnx,bridge", 0);
	if (!vtc_node) {
		dev_err(&pdev->dev, "required vtc node is missing\n");
		return -EINVAL;
	}
	avpg->vtc = of_xlnx_bridge_get(vtc_node);
	of_node_put(vtc_node);
	if (!avpg->vtc) {
		dev_dbg(&pdev->dev, "didn't get vtc bridge instance\n");
		return -EPROBE_DEFER;
	}

	hrtimer_init(&avpg->timer, CLOCK_REALTIME, HRTIMER_MODE_REL);
	avpg->timer.function = xlnx_avpg_timer_cb;

	return 0;
}

static void xlnx_avpg_remove(struct platform_device *pdev)
{
	xlnx_avpg_drm_fini(&pdev->dev);
}

static const struct of_device_id xlnx_avpg_of_match[] = {
	{ .compatible = "xlnx,psdpdc-av-pat-gen-2.0", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xlnx_avpg_of_match);

static struct platform_driver xlnx_avpg_driver = {
	.probe			= xlnx_avpg_probe,
	.remove			= xlnx_avpg_remove,
	.driver			= {
		.name		= "xlnx-avpg",
		.of_match_table	= xlnx_avpg_of_match,
	},
};

module_platform_driver(xlnx_avpg_driver);

MODULE_AUTHOR("Anatoliy Klymenko");
MODULE_DESCRIPTION("Xilinx AVPG CRTC Driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP Display Controller Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <video/videomode.h>

#include "xlnx_bridge.h"
#include "xlnx_crtc.h"
#include "xlnx_fb.h"
#include "zynqmp_disp.h"
#include "zynqmp_disp_regs.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"

/*
 * Overview
 * --------
 *
 * The display part of ZynqMP DP subsystem. Internally, the device
 * is partitioned into 3 blocks: AV buffer manager, Blender, Audio.
 * The driver creates the DRM crtc and plane objectes and maps the DRM
 * interface into those 3 blocks. In high level, the driver is layered
 * in the following way:
 *
 * zynqmp_disp_crtc & zynqmp_disp_plane
 * |->zynqmp_disp
 *	|->zynqmp_disp_aud
 *	|->zynqmp_disp_blend
 *	|->zynqmp_disp_av_buf
 *
 * The driver APIs are used externally by
 * - zynqmp_dpsub: Top level ZynqMP DP subsystem driver
 * - zynqmp_dp: ZynqMP DP driver
 * - xlnx_crtc: Xilinx DRM specific crtc functions
 */

/* The default value is ZYNQMP_DISP_AV_BUF_GFX_FMT_RGB565 */
static uint zynqmp_disp_gfx_init_fmt;
module_param_named(gfx_init_fmt, zynqmp_disp_gfx_init_fmt, uint, 0444);
MODULE_PARM_DESC(gfx_init_fmt, "The initial format of the graphics layer\n"
			       "\t\t0 = rgb565 (default)\n"
			       "\t\t1 = rgb888\n"
			       "\t\t2 = argb8888\n");
/* These value should be mapped to index of av_buf_gfx_fmts[] */
#define ZYNQMP_DISP_AV_BUF_GFX_FMT_RGB565		10
#define ZYNQMP_DISP_AV_BUF_GFX_FMT_RGB888		5
#define ZYNQMP_DISP_AV_BUF_GFX_FMT_ARGB8888		1
static const u32 zynqmp_disp_gfx_init_fmts[] = {
	ZYNQMP_DISP_AV_BUF_GFX_FMT_RGB565,
	ZYNQMP_DISP_AV_BUF_GFX_FMT_RGB888,
	ZYNQMP_DISP_AV_BUF_GFX_FMT_ARGB8888,
};

#define ZYNQMP_DISP_AV_BUF_NUM_VID_GFX_BUFFERS		4
#define ZYNQMP_DISP_AV_BUF_NUM_BUFFERS			6

#define ZYNQMP_DISP_NUM_LAYERS				2
#define ZYNQMP_DISP_MAX_NUM_SUB_PLANES			3
/*
 * 3840x2160 is advertised max resolution, but almost any resolutions under
 * 300Mhz pixel rate would work. Thus put 4096 as maximum width and height.
 */
#define ZYNQMP_DISP_MAX_WIDTH				4096
#define ZYNQMP_DISP_MAX_HEIGHT				4096
/* 44 bit addressing. This is actually DPDMA limitation */
#define ZYNQMP_DISP_MAX_DMA_BIT				44

/**
 * enum zynqmp_disp_layer_type - Layer type (can be used for hw ID)
 * @ZYNQMP_DISP_LAYER_VID: Video layer
 * @ZYNQMP_DISP_LAYER_GFX: Graphics layer
 */
enum zynqmp_disp_layer_type {
	ZYNQMP_DISP_LAYER_VID,
	ZYNQMP_DISP_LAYER_GFX
};

/**
 * enum zynqmp_disp_layer_mode - Layer mode
 * @ZYNQMP_DISP_LAYER_NONLIVE: non-live (memory) mode
 * @ZYNQMP_DISP_LAYER_LIVE: live (stream) mode
 */
enum zynqmp_disp_layer_mode {
	ZYNQMP_DISP_LAYER_NONLIVE,
	ZYNQMP_DISP_LAYER_LIVE
};

/**
 * struct zynqmp_disp_layer_dma - struct for DMA engine
 * @chan: DMA channel
 * @is_active: flag if the DMA is active
 * @xt: Interleaved desc config container
 * @sgl: Data chunk for dma_interleaved_template
 */
struct zynqmp_disp_layer_dma {
	struct dma_chan *chan;
	bool is_active;
	struct dma_interleaved_template xt;
	struct data_chunk sgl[1];
};

/**
 * struct zynqmp_disp_layer - Display subsystem layer
 * @plane: DRM plane
 * @bridge: Xlnx bridge
 * @dma: struct for DMA engine
 * @num_chan: Number of DMA channel
 * @id: Layer ID
 * @offset: Layer offset in the register space
 * @enabled: flag if enabled
 * @fmt: Current format descriptor
 * @drm_fmts: Array of supported DRM formats
 * @num_fmts: Number of supported DRM formats
 * @bus_fmts: Array of supported bus formats
 * @num_bus_fmts: Number of supported bus formats
 * @w: Width
 * @h: Height
 * @mode: the operation mode
 * @other: other layer
 * @disp: back pointer to struct zynqmp_disp
 */
struct zynqmp_disp_layer {
	struct drm_plane plane;
	struct xlnx_bridge bridge;
	struct zynqmp_disp_layer_dma dma[ZYNQMP_DISP_MAX_NUM_SUB_PLANES];
	unsigned int num_chan;
	enum zynqmp_disp_layer_type id;
	u32 offset;
	u8 enabled;
	const struct zynqmp_disp_fmt *fmt;
	u32 *drm_fmts;
	unsigned int num_fmts;
	u32 *bus_fmts;
	unsigned int num_bus_fmts;
	u32 w;
	u32 h;
	enum zynqmp_disp_layer_mode mode;
	struct zynqmp_disp_layer *other;
	struct zynqmp_disp *disp;
};

/**
 * struct zynqmp_disp_blend - Blender
 * @base: Base address offset
 */
struct zynqmp_disp_blend {
	void __iomem *base;
};

/**
 * struct zynqmp_disp_av_buf - AV buffer manager
 * @base: Base address offset
 */
struct zynqmp_disp_av_buf {
	void __iomem *base;
};

/**
 * struct zynqmp_disp_aud - Audio
 * @base: Base address offset
 */
struct zynqmp_disp_aud {
	void __iomem *base;
};

/**
 * struct zynqmp_disp - Display subsystem
 * @xlnx_crtc: Xilinx DRM crtc
 * @dev: device structure
 * @dpsub: Display subsystem
 * @drm: DRM core
 * @enabled: flag if enabled
 * @blend: Blender block
 * @av_buf: AV buffer manager block
 * @aud:Audio block
 * @layers: layers
 * @g_alpha_prop: global alpha property
 * @alpha: current global alpha value
 * @g_alpha_en_prop: the global alpha enable property
 * @alpha_en: flag if the global alpha is enabled
 * @color_prop: output color format property
 * @color: current output color value
 * @bg_c0_prop: 1st component of background color property
 * @bg_c0: current value of 1st background color component
 * @bg_c1_prop: 2nd component of background color property
 * @bg_c1: current value of 2nd background color component
 * @bg_c2_prop: 3rd component of background color property
 * @bg_c2: current value of 3rd background color component
 * @tpg_prop: Test Pattern Generation mode property
 * @tpg_on: current TPG mode state
 * @event: pending vblank event request
 * @_ps_pclk: Pixel clock from PS
 * @_pl_pclk: Pixel clock from PL
 * @pclk: Pixel clock
 * @pclk_en: Flag if the pixel clock is enabled
 * @_ps_audclk: Audio clock from PS
 * @_pl_audclk: Audio clock from PL
 * @audclk: Audio clock
 * @audclk_en: Flag if the audio clock is enabled
 * @aclk: APB clock
 * @aclk_en: Flag if the APB clock is enabled
 * @vtc_bridge: vtc_bridge structure
 */
struct zynqmp_disp {
	struct xlnx_crtc xlnx_crtc;
	struct device *dev;
	struct zynqmp_dpsub *dpsub;
	struct drm_device *drm;
	bool enabled;
	struct zynqmp_disp_blend blend;
	struct zynqmp_disp_av_buf av_buf;
	struct zynqmp_disp_aud aud;
	struct zynqmp_disp_layer layers[ZYNQMP_DISP_NUM_LAYERS];
	struct drm_property *g_alpha_prop;
	u32 alpha;
	struct drm_property *g_alpha_en_prop;
	bool alpha_en;
	struct drm_property *color_prop;
	unsigned int color;
	struct drm_property *bg_c0_prop;
	u32 bg_c0;
	struct drm_property *bg_c1_prop;
	u32 bg_c1;
	struct drm_property *bg_c2_prop;
	u32 bg_c2;
	struct drm_property *tpg_prop;
	bool tpg_on;
	struct drm_pending_vblank_event *event;
	/* Don't operate directly on _ps_ */
	struct clk *_ps_pclk;
	struct clk *_pl_pclk;
	struct clk *pclk;
	bool pclk_en;
	struct clk *_ps_audclk;
	struct clk *_pl_audclk;
	struct clk *audclk;
	bool audclk_en;
	struct clk *aclk;
	bool aclk_en;
	struct xlnx_bridge *vtc_bridge;
};

/**
 * struct zynqmp_disp_fmt - Display subsystem format mapping
 * @drm_fmt: drm format
 * @disp_fmt: Display subsystem format
 * @bus_fmt: Bus formats (live formats)
 * @rgb: flag for RGB formats
 * @swap: flag to swap r & b for rgb formats, and u & v for yuv formats
 * @chroma_sub: flag for chroma subsampled formats
 * @sf: scaling factors for up to 3 color components
 */
struct zynqmp_disp_fmt {
	u32 drm_fmt;
	u32 disp_fmt;
	u32 bus_fmt;
	bool rgb;
	bool swap;
	bool chroma_sub;
	u32 sf[3];
};

static void zynqmp_disp_write(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

static u32 zynqmp_disp_read(void __iomem *base, int offset)
{
	return readl(base + offset);
}

static void zynqmp_disp_clr(void __iomem *base, int offset, u32 clr)
{
	zynqmp_disp_write(base, offset, zynqmp_disp_read(base, offset) & ~clr);
}

static void zynqmp_disp_set(void __iomem *base, int offset, u32 set)
{
	zynqmp_disp_write(base, offset, zynqmp_disp_read(base, offset) | set);
}

/*
 * Clock functions
 */

/**
 * zynqmp_disp_clk_enable - Enable the clock if needed
 * @clk: clk device
 * @flag: flag if the clock is enabled
 *
 * Enable the clock only if it's not enabled @flag.
 *
 * Return: value from clk_prepare_enable().
 */
static int zynqmp_disp_clk_enable(struct clk *clk, bool *flag)
{
	int ret = 0;

	if (!*flag) {
		ret = clk_prepare_enable(clk);
		if (!ret)
			*flag = true;
	}

	return ret;
}

/**
 * zynqmp_disp_clk_disable - Disable the clock if needed
 * @clk: clk device
 * @flag: flag if the clock is enabled
 *
 * Disable the clock only if it's enabled @flag.
 */
static void zynqmp_disp_clk_disable(struct clk *clk, bool *flag)
{
	if (*flag) {
		clk_disable_unprepare(clk);
		*flag = false;
	}
}

/**
 * zynqmp_disp_clk_enable_disable - Enable and disable the clock
 * @clk: clk device
 * @flag: flag if the clock is enabled
 *
 * This is to ensure the clock is disabled. The initial hardware state is
 * unknown, and this makes sure that the clock is disabled.
 *
 * Return: value from clk_prepare_enable().
 */
static int zynqmp_disp_clk_enable_disable(struct clk *clk, bool *flag)
{
	int ret = 0;

	if (!*flag) {
		ret = clk_prepare_enable(clk);
		clk_disable_unprepare(clk);
	}

	return ret;
}

/*
 * Blender functions
 */

/**
 * zynqmp_disp_blend_set_output_fmt - Set the output format of the blend
 * @blend: blend object
 * @fmt: output format
 *
 * Set the output format to @fmt.
 */
static void
zynqmp_disp_blend_set_output_fmt(struct zynqmp_disp_blend *blend, u32 fmt)
{
	u16 reset_coeffs[] = { 0x1000, 0x0, 0x0,
			       0x0, 0x1000, 0x0,
			       0x0, 0x0, 0x1000 };
	u32 reset_offsets[] = { 0x0, 0x0, 0x0 };
	u16 sdtv_coeffs[] = { 0x4c9, 0x864, 0x1d3,
			      0x7d4d, 0x7ab3, 0x800,
			      0x800, 0x794d, 0x7eb3 };
	u32 full_range_offsets[] = { 0x0, 0x8000000, 0x8000000 };
	u16 *coeffs;
	u32 *offsets;
	u32 offset, i;

	zynqmp_disp_write(blend->base, ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT, fmt);
	if (fmt == ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_RGB) {
		coeffs = reset_coeffs;
		offsets = reset_offsets;
	} else {
		/* Hardcode Full-range SDTV values. Can be runtime config */
		coeffs = sdtv_coeffs;
		offsets = full_range_offsets;
	}

	offset = ZYNQMP_DISP_V_BLEND_RGB2YCBCR_COEFF0;
	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_COEFF; i++)
		zynqmp_disp_write(blend->base, offset + i * 4, coeffs[i]);

	offset = ZYNQMP_DISP_V_BLEND_LUMA_OUTCSC_OFFSET;
	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_OFFSET; i++)
		zynqmp_disp_write(blend->base, offset + i * 4, offsets[i]);
}

/**
 * zynqmp_disp_blend_layer_coeff - Set the coefficients for @layer
 * @blend: blend object
 * @layer: layer to set the coefficients for
 * @on: if layer is on / off
 *
 * Depending on the format (rgb / yuv and swap), and the status (on / off),
 * this function sets the coefficients for the given layer @layer accordingly.
 */
static void zynqmp_disp_blend_layer_coeff(struct zynqmp_disp_blend *blend,
					  struct zynqmp_disp_layer *layer,
					  bool on)
{
	u32 offset, i, s0, s1;
	u16 sdtv_coeffs[] = { 0x1000, 0x166f, 0x0,
			      0x1000, 0x7483, 0x7a7f,
			      0x1000, 0x0, 0x1c5a };
	u16 sdtv_coeffs_yonly[] = { 0x0, 0x0, 0x1000,
				    0x0, 0x0, 0x1000,
				    0x0, 0x0, 0x1000 };
	u16 swap_coeffs[] = { 0x1000, 0x0, 0x0,
			      0x0, 0x1000, 0x0,
			      0x0, 0x0, 0x1000 };
	u16 null_coeffs[] = { 0x0, 0x0, 0x0,
			      0x0, 0x0, 0x0,
			      0x0, 0x0, 0x0 };
	u16 *coeffs;
	u32 sdtv_offsets[] = { 0x0, 0x1800, 0x1800 };
	u32 sdtv_offsets_yonly[] = { 0x1800, 0x1800, 0x0 };
	u32 null_offsets[] = { 0x0, 0x0, 0x0 };
	u32 *offsets;
	struct zynqmp_disp *display = layer->disp;

	if (layer->id == ZYNQMP_DISP_LAYER_VID)
		offset = ZYNQMP_DISP_V_BLEND_IN1CSC_COEFF0;
	else
		offset = ZYNQMP_DISP_V_BLEND_IN2CSC_COEFF0;

	if (!on) {
		coeffs = null_coeffs;
		offsets = null_offsets;
	} else {
		if ((!layer->fmt->rgb) && (!display->tpg_on)) {
			/*
			 * In case of Y_ONLY formats, pixels are unpacked
			 * differently compared to YCbCr
			 */
			if (layer->fmt->drm_fmt == DRM_FORMAT_Y8 ||
			    layer->fmt->drm_fmt == DRM_FORMAT_Y10) {
				coeffs = sdtv_coeffs_yonly;
				offsets = sdtv_offsets_yonly;
			} else {
				coeffs = sdtv_coeffs;
				offsets = sdtv_offsets;
			}

			s0 = 1;
			s1 = 2;
		} else {
			coeffs = swap_coeffs;
			s0 = 0;
			s1 = 2;

			/* No offset for RGB formats */
			offsets = null_offsets;
		}

		if (layer->fmt->swap) {
			for (i = 0; i < 3; i++) {
				coeffs[i * 3 + s0] ^= coeffs[i * 3 + s1];
				coeffs[i * 3 + s1] ^= coeffs[i * 3 + s0];
				coeffs[i * 3 + s0] ^= coeffs[i * 3 + s1];
			}
		}
	}

	/* Program coefficients. Can be runtime configurable */
	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_COEFF; i++)
		zynqmp_disp_write(blend->base, offset + i * 4, coeffs[i]);

	if (layer->id == ZYNQMP_DISP_LAYER_VID)
		offset = ZYNQMP_DISP_V_BLEND_LUMA_IN1CSC_OFFSET;
	else
		offset = ZYNQMP_DISP_V_BLEND_LUMA_IN2CSC_OFFSET;

	/* Program offsets. Can be runtime configurable */
	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_OFFSET; i++)
		zynqmp_disp_write(blend->base, offset + i * 4, offsets[i]);
}

/**
 * zynqmp_disp_blend_layer_enable - Enable a layer
 * @blend: blend object
 * @layer: layer to enable
 *
 * Enable a layer @layer.
 */
static void zynqmp_disp_blend_layer_enable(struct zynqmp_disp_blend *blend,
					   struct zynqmp_disp_layer *layer)
{
	u32 reg;

	reg = layer->fmt->rgb ? ZYNQMP_DISP_V_BLEND_LAYER_CONTROL_RGB : 0;
	reg |= layer->fmt->chroma_sub ?
	       ZYNQMP_DISP_V_BLEND_LAYER_CONTROL_EN_US : 0;

	zynqmp_disp_write(blend->base,
			  ZYNQMP_DISP_V_BLEND_LAYER_CONTROL + layer->offset,
			  reg);

	zynqmp_disp_blend_layer_coeff(blend, layer, true);
}

/**
 * zynqmp_disp_blend_layer_disable - Disable a layer
 * @blend: blend object
 * @layer: layer to disable
 *
 * Disable a layer @layer.
 */
static void zynqmp_disp_blend_layer_disable(struct zynqmp_disp_blend *blend,
					    struct zynqmp_disp_layer *layer)
{
	zynqmp_disp_write(blend->base,
			  ZYNQMP_DISP_V_BLEND_LAYER_CONTROL + layer->offset, 0);

	zynqmp_disp_blend_layer_coeff(blend, layer, false);
}

/**
 * zynqmp_disp_blend_set_bg_color - Set the background color
 * @blend: blend object
 * @c0: color component 0
 * @c1: color component 1
 * @c2: color component 2
 *
 * Set the background color.
 */
static void zynqmp_disp_blend_set_bg_color(struct zynqmp_disp_blend *blend,
					   u32 c0, u32 c1, u32 c2)
{
	zynqmp_disp_write(blend->base, ZYNQMP_DISP_V_BLEND_BG_CLR_0, c0);
	zynqmp_disp_write(blend->base, ZYNQMP_DISP_V_BLEND_BG_CLR_1, c1);
	zynqmp_disp_write(blend->base, ZYNQMP_DISP_V_BLEND_BG_CLR_2, c2);
}

/**
 * zynqmp_disp_blend_set_alpha - Set the alpha for blending
 * @blend: blend object
 * @alpha: alpha value to be used
 *
 * Set the alpha for blending.
 */
static void
zynqmp_disp_blend_set_alpha(struct zynqmp_disp_blend *blend, u32 alpha)
{
	u32 reg;

	reg = zynqmp_disp_read(blend->base,
			       ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA);
	reg &= ~ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA_MASK;
	reg |= alpha << 1;
	zynqmp_disp_write(blend->base, ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA,
			  reg);
}

/**
 * zynqmp_disp_blend_enable_alpha - Enable/disable the global alpha
 * @blend: blend object
 * @enable: flag to enable or disable alpha blending
 *
 * Enable/disable the global alpha blending based on @enable.
 */
static void
zynqmp_disp_blend_enable_alpha(struct zynqmp_disp_blend *blend, bool enable)
{
	if (enable)
		zynqmp_disp_set(blend->base,
				ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA, BIT(0));
	else
		zynqmp_disp_clr(blend->base,
				ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA, BIT(0));
}

/* List of blend output formats */
/* The id / order should be aligned with zynqmp_disp_color_enum */
static const struct zynqmp_disp_fmt blend_output_fmts[] = {
	{
		.disp_fmt	= ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_RGB,
	}, {
		.disp_fmt	= ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_YCBCR444,
	}, {
		.disp_fmt	= ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_YCBCR422,
	}, {
		.disp_fmt	= ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_YONLY,
	}
};

/*
 * AV buffer manager functions
 */

/* List of video layer formats */
#define ZYNQMP_DISP_AV_BUF_VID_FMT_YUYV	2
static const struct zynqmp_disp_fmt av_buf_vid_fmts[] = {
	{
		.drm_fmt	= DRM_FORMAT_VYUY,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_VYUY,
		.rgb		= false,
		.swap		= true,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_UYVY,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_VYUY,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_YUYV,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YUYV,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_YVYU,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YUYV,
		.rgb		= false,
		.swap		= true,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_YUV422,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_YVU422,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16,
		.rgb		= false,
		.swap		= true,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_YUV444,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV24,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_YVU444,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV24,
		.rgb		= false,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_NV16,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_NV61,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI,
		.rgb		= false,
		.swap		= true,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_Y8,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_MONO,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_Y10,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YONLY_10,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_BGR888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGB888,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_RGB888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGB888,
		.rgb		= true,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_XBGR8888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGBA8880,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_XRGB8888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGBA8880,
		.rgb		= true,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_XBGR2101010,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGB888_10,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_XRGB2101010,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGB888_10,
		.rgb		= true,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_YUV420,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16_420,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_YVU420,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16_420,
		.rgb		= false,
		.swap		= true,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_NV12,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI_420,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_NV21,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI_420,
		.rgb		= false,
		.swap		= true,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_XV15,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI_420_10,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_XV20,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI_10,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
	}
};

/* List of graphics layer formats */
static const struct zynqmp_disp_fmt av_buf_gfx_fmts[] = {
	{
		.drm_fmt	= DRM_FORMAT_ABGR8888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA8888,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_ARGB8888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA8888,
		.rgb		= true,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_RGBA8888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_ABGR8888,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_BGRA8888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_ABGR8888,
		.rgb		= true,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_BGR888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGB888,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_RGB888,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_BGR888,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_RGBA5551,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA5551,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_BGRA5551,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA5551,
		.rgb		= true,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_RGBA4444,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA4444,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_4BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_4BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_4BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_BGRA4444,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA4444,
		.rgb		= true,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_4BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_4BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_4BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_RGB565,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGB565,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_6BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
	}, {
		.drm_fmt	= DRM_FORMAT_BGR565,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGB565,
		.rgb		= true,
		.swap		= true,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_6BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_5BIT_SF,
	}
};

/* List of live formats */
/* Format can be combination of color, bpc, and cb-cr order.
 * - Color: RGB / YUV444 / YUV422 / Y only
 * - BPC: 6, 8, 10, 12
 * - Swap: Cb and Cr swap
 * which can be 32 bus formats. Only list the subset of those for now.
 */
static const struct zynqmp_disp_fmt av_buf_live_fmts[] = {
	{
		.bus_fmt	= MEDIA_BUS_FMT_RGB666_1X18,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_BPC_6 |
				  ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_FMT_RGB,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_6BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_6BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_6BIT_SF,
	}, {
		.bus_fmt	= MEDIA_BUS_FMT_RBG888_1X24,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_BPC_8 |
				  ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_FMT_RGB,
		.rgb		= true,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.bus_fmt	= MEDIA_BUS_FMT_UYVY8_1X16,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_BPC_8 |
				  ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_FMT_YUV422,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.bus_fmt	= MEDIA_BUS_FMT_VUY8_1X24,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_BPC_8 |
				  ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_FMT_YUV444,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= false,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_8BIT_SF,
	}, {
		.bus_fmt	= MEDIA_BUS_FMT_UYVY10_1X20,
		.disp_fmt	= ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_BPC_10 |
				  ZYNQMP_DISP_AV_BUF_LIVE_CONFIG_FMT_YUV422,
		.rgb		= false,
		.swap		= false,
		.chroma_sub	= true,
		.sf[0]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[1]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
		.sf[2]		= ZYNQMP_DISP_AV_BUF_10BIT_SF,
	}
};

/**
 * zynqmp_disp_av_buf_set_fmt - Set the input formats
 * @av_buf: av buffer manager
 * @fmt: formats
 *
 * Set the av buffer manager format to @fmt. @fmt should have valid values
 * for both video and graphics layer.
 */
static void
zynqmp_disp_av_buf_set_fmt(struct zynqmp_disp_av_buf *av_buf, u32 fmt)
{
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_FMT, fmt);
}

/**
 * zynqmp_disp_av_buf_get_fmt - Get the input formats
 * @av_buf: av buffer manager
 *
 * Get the input formats (which include video and graphics) of
 * av buffer manager.
 *
 * Return: value of ZYNQMP_DISP_AV_BUF_FMT register.
 */
static u32
zynqmp_disp_av_buf_get_fmt(struct zynqmp_disp_av_buf *av_buf)
{
	return zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_FMT);
}

/**
 * zynqmp_disp_av_buf_set_live_fmt - Set the live_input format
 * @av_buf: av buffer manager
 * @fmt: format
 * @is_vid: if it's for video layer
 *
 * Set the live input format to @fmt. @fmt should have valid values.
 * @vid will determine if it's for video layer or graphics layer
 * @fmt should be a valid hardware value.
 */
static void zynqmp_disp_av_buf_set_live_fmt(struct zynqmp_disp_av_buf *av_buf,
					    u32 fmt, bool is_vid)
{
	u32 offset;

	if (is_vid)
		offset = ZYNQMP_DISP_AV_BUF_LIVE_VID_CONFIG;
	else
		offset = ZYNQMP_DISP_AV_BUF_LIVE_GFX_CONFIG;

	zynqmp_disp_write(av_buf->base, offset, fmt);
}

/**
 * zynqmp_disp_av_buf_set_vid_clock_src - Set the video clock source
 * @av_buf: av buffer manager
 * @from_ps: flag if the video clock is from ps
 *
 * Set the video clock source based on @from_ps. It can come from either PS or
 * PL.
 */
static void
zynqmp_disp_av_buf_set_vid_clock_src(struct zynqmp_disp_av_buf *av_buf,
				     bool from_ps)
{
	u32 reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_CLK_SRC);

	if (from_ps)
		reg |= ZYNQMP_DISP_AV_BUF_CLK_SRC_VID_FROM_PS;
	else
		reg &= ~ZYNQMP_DISP_AV_BUF_CLK_SRC_VID_FROM_PS;
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_CLK_SRC, reg);
}

/**
 * zynqmp_disp_av_buf_vid_clock_src_is_ps - if ps clock is used
 * @av_buf: av buffer manager
 *
 * Return: if ps clock is used
 */
static bool
zynqmp_disp_av_buf_vid_clock_src_is_ps(struct zynqmp_disp_av_buf *av_buf)
{
	u32 reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_CLK_SRC);

	return !!(reg & ZYNQMP_DISP_AV_BUF_CLK_SRC_VID_FROM_PS);
}

/**
 * zynqmp_disp_av_buf_set_vid_timing_src - Set the video timing source
 * @av_buf: av buffer manager
 * @internal: flag if the video timing is generated internally
 *
 * Set the video timing source based on @internal. It can come externally or
 * be generated internally.
 */
static void
zynqmp_disp_av_buf_set_vid_timing_src(struct zynqmp_disp_av_buf *av_buf,
				      bool internal)
{
	u32 reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_CLK_SRC);

	if (internal)
		reg |= ZYNQMP_DISP_AV_BUF_CLK_SRC_VID_INTERNAL_TIMING;
	else
		reg &= ~ZYNQMP_DISP_AV_BUF_CLK_SRC_VID_INTERNAL_TIMING;
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_CLK_SRC, reg);
}

/**
 * zynqmp_disp_av_buf_vid_timing_src_is_int - if internal timing is used
 * @av_buf: av buffer manager
 *
 * Return: if the internal timing is used
 */
static bool
zynqmp_disp_av_buf_vid_timing_src_is_int(struct zynqmp_disp_av_buf *av_buf)
{
	u32 reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_CLK_SRC);

	return !!(reg & ZYNQMP_DISP_AV_BUF_CLK_SRC_VID_INTERNAL_TIMING);
}

/**
 * zynqmp_disp_av_buf_set_aud_clock_src - Set the audio clock source
 * @av_buf: av buffer manager
 * @from_ps: flag if the video clock is from ps
 *
 * Set the audio clock source based on @from_ps. It can come from either PS or
 * PL.
 */
static void
zynqmp_disp_av_buf_set_aud_clock_src(struct zynqmp_disp_av_buf *av_buf,
				     bool from_ps)
{
	u32 reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_CLK_SRC);

	if (from_ps)
		reg |= ZYNQMP_DISP_AV_BUF_CLK_SRC_AUD_FROM_PS;
	else
		reg &= ~ZYNQMP_DISP_AV_BUF_CLK_SRC_AUD_FROM_PS;
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_CLK_SRC, reg);
}

/**
 * zynqmp_disp_av_buf_enable_buf - Enable buffers
 * @av_buf: av buffer manager
 *
 * Enable all (video and audio) buffers.
 */
static void
zynqmp_disp_av_buf_enable_buf(struct zynqmp_disp_av_buf *av_buf)
{
	u32 reg, i;

	reg = ZYNQMP_DISP_AV_BUF_CHBUF_EN;
	reg |= ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_MAX <<
	       ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_SHIFT;

	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_VID_GFX_BUFFERS; i++)
		zynqmp_disp_write(av_buf->base,
				  ZYNQMP_DISP_AV_BUF_CHBUF + i * 4, reg);

	reg = ZYNQMP_DISP_AV_BUF_CHBUF_EN;
	reg |= ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_AUD_MAX <<
	       ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_SHIFT;

	for (; i < ZYNQMP_DISP_AV_BUF_NUM_BUFFERS; i++)
		zynqmp_disp_write(av_buf->base,
				  ZYNQMP_DISP_AV_BUF_CHBUF + i * 4, reg);
}

/**
 * zynqmp_disp_av_buf_disable_buf - Disable buffers
 * @av_buf: av buffer manager
 *
 * Disable all (video and audio) buffers.
 */
static void
zynqmp_disp_av_buf_disable_buf(struct zynqmp_disp_av_buf *av_buf)
{
	u32 reg, i;

	reg = ZYNQMP_DISP_AV_BUF_CHBUF_FLUSH & ~ZYNQMP_DISP_AV_BUF_CHBUF_EN;
	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_BUFFERS; i++)
		zynqmp_disp_write(av_buf->base,
				  ZYNQMP_DISP_AV_BUF_CHBUF + i * 4, reg);
}

/**
 * zynqmp_disp_av_buf_enable_aud - Enable audio
 * @av_buf: av buffer manager
 *
 * Enable all audio buffers.
 */
static void
zynqmp_disp_av_buf_enable_aud(struct zynqmp_disp_av_buf *av_buf)
{
	u32 reg;

	reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT);
	reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MASK;
	reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MEM;
	reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD2_EN;
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT, reg);
}

/**
 * zynqmp_disp_av_buf_enable - Enable the video pipe
 * @av_buf: av buffer manager
 *
 * De-assert the video pipe reset
 */
static void
zynqmp_disp_av_buf_enable(struct zynqmp_disp_av_buf *av_buf)
{
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_SRST_REG, 0);
}

/**
 * zynqmp_disp_av_buf_disable - Disable the video pipe
 * @av_buf: av buffer manager
 *
 * Assert the video pipe reset
 */
static void
zynqmp_disp_av_buf_disable(struct zynqmp_disp_av_buf *av_buf)
{
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_SRST_REG,
			  ZYNQMP_DISP_AV_BUF_SRST_REG_VID_RST);
}

/**
 * zynqmp_disp_av_buf_disable_aud - Disable audio
 * @av_buf: av buffer manager
 *
 * Disable all audio buffers.
 */
static void
zynqmp_disp_av_buf_disable_aud(struct zynqmp_disp_av_buf *av_buf)
{
	u32 reg;

	reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT);
	reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MASK;
	reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_DISABLE;
	reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD2_EN;
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT, reg);
}

/**
 * zynqmp_disp_av_buf_set_tpg - Set TPG mode
 * @av_buf: av buffer manager
 * @tpg_on: if TPG should be on
 *
 * Set the TPG mode based on @tpg_on.
 */
static void zynqmp_disp_av_buf_set_tpg(struct zynqmp_disp_av_buf *av_buf,
				       bool tpg_on)
{
	u32 reg;

	reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT);
	reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MASK;
	if (tpg_on)
		reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_PATTERN;
	else
		reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_PATTERN;
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT, reg);
}

/**
 * zynqmp_disp_av_buf_enable_vid - Enable the video layer buffer
 * @av_buf: av buffer manager
 * @layer: layer to enable
 * @mode: operation mode of layer
 *
 * Enable the video/graphics buffer for @layer.
 */
static void zynqmp_disp_av_buf_enable_vid(struct zynqmp_disp_av_buf *av_buf,
					  struct zynqmp_disp_layer *layer,
					  enum zynqmp_disp_layer_mode mode)
{
	u32 reg;

	reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT);
	if (layer->id == ZYNQMP_DISP_LAYER_VID) {
		reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MASK;
		if (mode == ZYNQMP_DISP_LAYER_NONLIVE)
			reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MEM;
		else
			reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_LIVE;
	} else {
		reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MASK;
		reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MEM;
		if (mode == ZYNQMP_DISP_LAYER_NONLIVE)
			reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MEM;
		else
			reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_LIVE;
	}
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT, reg);
}

/**
 * zynqmp_disp_av_buf_disable_vid - Disable the video layer buffer
 * @av_buf: av buffer manager
 * @layer: layer to disable
 *
 * Disable the video/graphics buffer for @layer.
 */
static void
zynqmp_disp_av_buf_disable_vid(struct zynqmp_disp_av_buf *av_buf,
			       struct zynqmp_disp_layer *layer)
{
	u32 reg;

	reg = zynqmp_disp_read(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT);
	if (layer->id == ZYNQMP_DISP_LAYER_VID) {
		reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MASK;
		reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_NONE;
	} else {
		reg &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MASK;
		reg |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_DISABLE;
	}
	zynqmp_disp_write(av_buf->base, ZYNQMP_DISP_AV_BUF_OUTPUT, reg);
}

/**
 * zynqmp_disp_av_buf_init_sf - Initialize scaling factors
 * @av_buf: av buffer manager
 * @vid_fmt: video format descriptor
 * @gfx_fmt: graphics format descriptor
 *
 * Initialize scaling factors for both video and graphics layers.
 * If the format descriptor is NULL, the function skips the programming.
 */
static void zynqmp_disp_av_buf_init_sf(struct zynqmp_disp_av_buf *av_buf,
				       const struct zynqmp_disp_fmt *vid_fmt,
				       const struct zynqmp_disp_fmt *gfx_fmt)
{
	unsigned int i;
	u32 offset;

	if (gfx_fmt) {
		offset = ZYNQMP_DISP_AV_BUF_GFX_COMP0_SF;
		for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_SF; i++)
			zynqmp_disp_write(av_buf->base, offset + i * 4,
					  gfx_fmt->sf[i]);
	}

	if (vid_fmt) {
		offset = ZYNQMP_DISP_AV_BUF_VID_COMP0_SF;
		for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_SF; i++)
			zynqmp_disp_write(av_buf->base, offset + i * 4,
					  vid_fmt->sf[i]);
	}
}

/**
 * zynqmp_disp_av_buf_init_live_sf - Initialize scaling factors for live source
 * @av_buf: av buffer manager
 * @fmt: format descriptor
 * @is_vid: flag if this is for video layer
 *
 * Initialize scaling factors for live source.
 */
static void zynqmp_disp_av_buf_init_live_sf(struct zynqmp_disp_av_buf *av_buf,
					    const struct zynqmp_disp_fmt *fmt,
					    bool is_vid)
{
	unsigned int i;
	u32 offset;

	if (is_vid)
		offset = ZYNQMP_DISP_AV_BUF_LIVE_VID_COMP0_SF;
	else
		offset = ZYNQMP_DISP_AV_BUF_LIVE_GFX_COMP0_SF;

	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_SF; i++)
		zynqmp_disp_write(av_buf->base, offset + i * 4,
				  fmt->sf[i]);
}

/*
 * Audio functions
 */

/**
 * zynqmp_disp_aud_init - Initialize the audio
 * @aud: audio
 *
 * Initialize the audio with default mixer volume. The de-assertion will
 * initialize the audio states.
 */
static void zynqmp_disp_aud_init(struct zynqmp_disp_aud *aud)
{
	/* Clear the audio soft reset register as it's an non-reset flop */
	zynqmp_disp_write(aud->base, ZYNQMP_DISP_AUD_SOFT_RESET, 0);
	zynqmp_disp_write(aud->base, ZYNQMP_DISP_AUD_MIXER_VOLUME,
			  ZYNQMP_DISP_AUD_MIXER_VOLUME_NO_SCALE);
}

/**
 * zynqmp_disp_aud_deinit - De-initialize the audio
 * @aud: audio
 *
 * Put the audio in reset.
 */
static void zynqmp_disp_aud_deinit(struct zynqmp_disp_aud *aud)
{
	zynqmp_disp_set(aud->base, ZYNQMP_DISP_AUD_SOFT_RESET,
			ZYNQMP_DISP_AUD_SOFT_RESET_AUD_SRST);
}

/*
 * ZynqMP Display layer functions
 */

/**
 * zynqmp_disp_layer_check_size - Verify width and height for the layer
 * @disp: Display subsystem
 * @layer: layer
 * @width: width
 * @height: height
 *
 * The Display subsystem has the limitation that both layers should have
 * identical size. This function stores width and height of @layer, and verifies
 * if the size (width and height) is valid.
 *
 * Return: 0 on success, or -EINVAL if width or/and height is invalid.
 */
static int zynqmp_disp_layer_check_size(struct zynqmp_disp *disp,
					struct zynqmp_disp_layer *layer,
					u32 width, u32 height)
{
	struct zynqmp_disp_layer *other = layer->other;

	if (other->enabled && (other->w != width || other->h != height)) {
		dev_err(disp->dev, "Layer width:height must be %d:%d\n",
			other->w, other->h);
		return -EINVAL;
	}

	layer->w = width;
	layer->h = height;

	return 0;
}

/**
 * zynqmp_disp_map_fmt - Find the Display subsystem format for given drm format
 * @fmts: format table to look up
 * @size: size of the table @fmts
 * @drm_fmt: DRM format to search
 *
 * Search a Display subsystem format corresponding to the given DRM format
 * @drm_fmt, and return the format descriptor which contains the Display
 * subsystem format value.
 *
 * Return: a Display subsystem format descriptor on success, or NULL.
 */
static const struct zynqmp_disp_fmt *
zynqmp_disp_map_fmt(const struct zynqmp_disp_fmt fmts[],
		    unsigned int size, uint32_t drm_fmt)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		if (fmts[i].drm_fmt == drm_fmt)
			return &fmts[i];

	return NULL;
}

/**
 * zynqmp_disp_layer_set_fmt - Set the format of the layer
 * @disp: Display subsystem
 * @layer: layer to set the format
 * @drm_fmt: DRM format to set
 *
 * Set the format of the given layer to @drm_fmt.
 *
 * Return: 0 on success. -EINVAL if @drm_fmt is not supported by the layer.
 */
static int zynqmp_disp_layer_set_fmt(struct zynqmp_disp *disp,
				     struct zynqmp_disp_layer *layer,
				     uint32_t drm_fmt)
{
	const struct zynqmp_disp_fmt *fmt;
	const struct zynqmp_disp_fmt *vid_fmt = NULL, *gfx_fmt = NULL;
	u32 size, fmts, mask;

	if (layer->id == ZYNQMP_DISP_LAYER_VID) {
		size = ARRAY_SIZE(av_buf_vid_fmts);
		mask = ~ZYNQMP_DISP_AV_BUF_FMT_NL_VID_MASK;
		fmt = zynqmp_disp_map_fmt(av_buf_vid_fmts, size, drm_fmt);
		vid_fmt = fmt;
	} else {
		size = ARRAY_SIZE(av_buf_gfx_fmts);
		mask = ~ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_MASK;
		fmt = zynqmp_disp_map_fmt(av_buf_gfx_fmts, size, drm_fmt);
		gfx_fmt = fmt;
	}

	if (!fmt)
		return -EINVAL;

	fmts = zynqmp_disp_av_buf_get_fmt(&disp->av_buf);
	fmts &= mask;
	fmts |= fmt->disp_fmt;
	zynqmp_disp_av_buf_set_fmt(&disp->av_buf, fmts);
	zynqmp_disp_av_buf_init_sf(&disp->av_buf, vid_fmt, gfx_fmt);
	layer->fmt = fmt;

	return 0;
}

/**
 * zynqmp_disp_map_live_fmt - Find the hardware format for given bus format
 * @fmts: format table to look up
 * @size: size of the table @fmts
 * @bus_fmt: bus format to search
 *
 * Search a Display subsystem format corresponding to the given bus format
 * @bus_fmt, and return the format descriptor which contains the Display
 * subsystem format value.
 *
 * Return: a Display subsystem format descriptor on success, or NULL.
 */
static const struct zynqmp_disp_fmt *
zynqmp_disp_map_live_fmt(const struct zynqmp_disp_fmt fmts[],
			 unsigned int size, uint32_t bus_fmt)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		if (fmts[i].bus_fmt == bus_fmt)
			return &fmts[i];

	return NULL;
}

/**
 * zynqmp_disp_layer_set_live_fmt - Set the live format of the layer
 * @disp: Display subsystem
 * @layer: layer to set the format
 * @bus_fmt: bus format to set
 *
 * Set the live format of the given layer to @live_fmt.
 *
 * Return: 0 on success. -EINVAL if @bus_fmt is not supported by the layer.
 */
static int zynqmp_disp_layer_set_live_fmt(struct zynqmp_disp *disp,
					  struct zynqmp_disp_layer *layer,
					  uint32_t bus_fmt)
{
	const struct zynqmp_disp_fmt *fmt;
	u32 size;
	bool is_vid = layer->id == ZYNQMP_DISP_LAYER_VID;

	size = ARRAY_SIZE(av_buf_live_fmts);
	fmt = zynqmp_disp_map_live_fmt(av_buf_live_fmts, size, bus_fmt);
	if (!fmt)
		return -EINVAL;

	zynqmp_disp_av_buf_set_live_fmt(&disp->av_buf, fmt->disp_fmt, is_vid);
	zynqmp_disp_av_buf_init_live_sf(&disp->av_buf, fmt, is_vid);
	layer->fmt = fmt;

	return 0;
}

/**
 * zynqmp_disp_layer_set_tpg - Enable or disable TPG
 * @disp: Display subsystem
 * @layer: Video layer
 * @tpg_on: flag if TPG needs to be enabled or disabled
 *
 * Enable / disable the TPG mode on the video layer @layer depending on
 * @tpg_on. The video layer should be disabled prior to enable request.
 *
 * Return: 0 on success. -ENODEV if it's not video layer. -EIO if
 * the video layer is enabled.
 */
static int zynqmp_disp_layer_set_tpg(struct zynqmp_disp *disp,
				     struct zynqmp_disp_layer *layer,
				     bool tpg_on)
{
	if (layer->id != ZYNQMP_DISP_LAYER_VID) {
		dev_err(disp->dev,
			"only the video layer has the tpg mode\n");
		return -ENODEV;
	}

	if (layer->enabled) {
		dev_err(disp->dev,
			"the video layer should be disabled for tpg mode\n");
		return -EIO;
	}

	disp->tpg_on = tpg_on;
	zynqmp_disp_av_buf_set_tpg(&disp->av_buf, tpg_on);

	return 0;
}

/**
 * zynqmp_disp_layer_get_tpg - Get the TPG mode status
 * @disp: Display subsystem
 * @layer: Video layer
 *
 * Return if the TPG is enabled or not.
 *
 * Return: true if TPG is on, otherwise false
 */
static bool zynqmp_disp_layer_get_tpg(struct zynqmp_disp *disp,
				      struct zynqmp_disp_layer *layer)
{
	return disp->tpg_on;
}

/**
 * zynqmp_disp_layer_get_fmts - Get the supported DRM formats of the layer
 * @disp: Display subsystem
 * @layer: layer to get the formats
 * @drm_fmts: pointer to array of DRM format strings
 * @num_fmts: pointer to number of returned DRM formats
 *
 * Get the supported DRM formats of the given layer.
 */
static void zynqmp_disp_layer_get_fmts(struct zynqmp_disp *disp,
				       struct zynqmp_disp_layer *layer,
				       u32 **drm_fmts, unsigned int *num_fmts)
{
	*drm_fmts = layer->drm_fmts;
	*num_fmts = layer->num_fmts;
}

/**
 * zynqmp_disp_layer_enable - Enable the layer
 * @disp: Display subsystem
 * @layer: layer to esable
 * @mode: operation mode
 *
 * Enable the layer @layer.
 *
 * Return: 0 on success, otherwise error code.
 */
static int zynqmp_disp_layer_enable(struct zynqmp_disp *disp,
				    struct zynqmp_disp_layer *layer,
				    enum zynqmp_disp_layer_mode mode)
{
	struct device *dev = disp->dev;
	struct dma_async_tx_descriptor *desc;
	unsigned long flags;
	unsigned int i;

	if (layer->enabled && layer->mode != mode) {
		dev_err(dev, "layer is already enabled in different mode\n");
		return -EBUSY;
	}

	zynqmp_disp_av_buf_enable_vid(&disp->av_buf, layer, mode);
	zynqmp_disp_blend_layer_enable(&disp->blend, layer);

	layer->enabled = true;
	layer->mode = mode;

	if (mode == ZYNQMP_DISP_LAYER_LIVE)
		return 0;

	for (i = 0; i < ZYNQMP_DISP_MAX_NUM_SUB_PLANES; i++) {
		struct zynqmp_disp_layer_dma *dma = &layer->dma[i];

		if (dma->chan && dma->is_active) {
			flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT | DMA_PREP_REPEAT |
				DMA_PREP_LOAD_EOT;
			desc = dmaengine_prep_interleaved_dma(dma->chan,
							      &dma->xt, flags);
			if (!desc) {
				dev_err(dev, "failed to prep DMA descriptor\n");
				return -ENOMEM;
			}

			dmaengine_submit(desc);
			dma_async_issue_pending(dma->chan);
		}
	}

	return 0;
}

/**
 * zynqmp_disp_layer_disable - Disable the layer
 * @disp: Display subsystem
 * @layer: layer to disable
 * @mode: operation mode
 *
 * Disable the layer @layer.
 *
 * Return: 0 on success, or -EBUSY if the layer is in different mode.
 */
static int zynqmp_disp_layer_disable(struct zynqmp_disp *disp,
				     struct zynqmp_disp_layer *layer,
				     enum zynqmp_disp_layer_mode mode)
{
	struct device *dev = disp->dev;
	unsigned int i;

	if (layer->mode != mode) {
		dev_err(dev, "the layer is operating in different mode\n");
		return -EBUSY;
	}

	for (i = 0; i < ZYNQMP_DISP_MAX_NUM_SUB_PLANES; i++)
		if (layer->dma[i].chan && layer->dma[i].is_active)
			dmaengine_terminate_sync(layer->dma[i].chan);

	zynqmp_disp_av_buf_disable_vid(&disp->av_buf, layer);
	zynqmp_disp_blend_layer_disable(&disp->blend, layer);
	layer->enabled = false;

	return 0;
}

/**
 * zynqmp_disp_layer_request_dma - Request DMA channels for a layer
 * @disp: Display subsystem
 * @layer: layer to request DMA channels
 * @name: identifier string for layer type
 *
 * Request DMA engine channels for corresponding layer.
 *
 * Return: 0 on success, or err value from of_dma_request_slave_channel().
 */
static int
zynqmp_disp_layer_request_dma(struct zynqmp_disp *disp,
			      struct zynqmp_disp_layer *layer, const char *name)
{
	struct zynqmp_disp_layer_dma *dma;
	unsigned int i;
	int ret;

	for (i = 0; i < layer->num_chan; i++) {
		char temp[16];

		dma = &layer->dma[i];
		snprintf(temp, sizeof(temp), "%s%d", name, i);
		dma->chan = of_dma_request_slave_channel(disp->dev->of_node,
							 temp);
		if (IS_ERR(dma->chan)) {
			dev_err(disp->dev, "failed to request dma channel\n");
			ret = PTR_ERR(dma->chan);
			dma->chan = NULL;
			return ret;
		}
	}

	return 0;
}

/**
 * zynqmp_disp_layer_release_dma - Release DMA channels for a layer
 * @disp: Display subsystem
 * @layer: layer to release DMA channels
 *
 * Release the dma channels associated with @layer.
 */
static void zynqmp_disp_layer_release_dma(struct zynqmp_disp *disp,
					  struct zynqmp_disp_layer *layer)
{
	unsigned int i;

	for (i = 0; i < layer->num_chan; i++) {
		if (layer->dma[i].chan) {
			/* Make sure the channel is terminated before release */
			dmaengine_terminate_all(layer->dma[i].chan);
			dma_release_channel(layer->dma[i].chan);
		}
	}
}

/**
 * zynqmp_disp_layer_is_live - if any layer is live
 * @disp: Display subsystem
 *
 * Return: true if any layer is live
 */
static bool zynqmp_disp_layer_is_live(struct zynqmp_disp *disp)
{
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++) {
		if (disp->layers[i].enabled &&
		    disp->layers[i].mode == ZYNQMP_DISP_LAYER_LIVE)
			return true;
	}

	return false;
}

/**
 * zynqmp_disp_layer_is_enabled - if any layer is enabled
 * @disp: Display subsystem
 *
 * Return: true if any layer is enabled
 */
static bool zynqmp_disp_layer_is_enabled(struct zynqmp_disp *disp)
{
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++)
		if (disp->layers[i].enabled)
			return true;

	return false;
}

/**
 * zynqmp_disp_layer_destroy - Destroy all layers
 * @disp: Display subsystem
 *
 * Destroy all layers.
 */
static void zynqmp_disp_layer_destroy(struct zynqmp_disp *disp)
{
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++)
		zynqmp_disp_layer_release_dma(disp, &disp->layers[i]);
}

/**
 * zynqmp_disp_layer_create - Create all layers
 * @disp: Display subsystem
 *
 * Create all layers.
 *
 * Return: 0 on success, otherwise error code from failed function
 */
static int zynqmp_disp_layer_create(struct zynqmp_disp *disp)
{
	struct zynqmp_disp_layer *layer;
	unsigned int i;
	int num_chans[ZYNQMP_DISP_NUM_LAYERS] = { 3, 1 };
	const char * const dma_name[] = { "vid", "gfx" };
	int ret = -EINVAL;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++) {
		layer = &disp->layers[i];
		layer->id = (enum zynqmp_disp_layer_type)i;
		layer->offset = i * 4;
		layer->other = &disp->layers[!i];
		layer->num_chan = num_chans[i];
		ret = zynqmp_disp_layer_request_dma(disp, layer, dma_name[i]);
		if (ret)
			goto err;
		layer->disp = disp;
	}

	return 0;

err:
	zynqmp_disp_layer_destroy(disp);
	return ret;
}

/*
 * ZynqMP Display internal functions
 */

/*
 * Output format enumeration.
 * The ID should be aligned with blend_output_fmts.
 * The string should be aligned with how zynqmp_dp_set_color() decodes.
 */
static struct drm_prop_enum_list zynqmp_disp_color_enum[] = {
	{ 0, "rgb" },
	{ 1, "ycrcb444" },
	{ 2, "ycrcb422" },
	{ 3, "yonly" },
};

/**
 * zynqmp_disp_set_output_fmt - Set the output format
 * @disp: Display subsystem
 * @id: the format ID. Refer to zynqmp_disp_color_enum[].
 *
 * This function sets the output format of the display / blender as well as
 * the format of DP controller. The @id should be aligned with
 * zynqmp_disp_color_enum.
 */
static void
zynqmp_disp_set_output_fmt(struct zynqmp_disp *disp, unsigned int id)
{
	const struct zynqmp_disp_fmt *fmt = &blend_output_fmts[id];

	zynqmp_dp_set_color(disp->dpsub->dp, zynqmp_disp_color_enum[id].name);
	zynqmp_disp_blend_set_output_fmt(&disp->blend, fmt->disp_fmt);
}

/**
 * zynqmp_disp_set_bg_color - Set the background color
 * @disp: Display subsystem
 * @c0: color component 0
 * @c1: color component 1
 * @c2: color component 2
 *
 * Set the background color with given color components (@c0, @c1, @c2).
 */
static void zynqmp_disp_set_bg_color(struct zynqmp_disp *disp,
				     u32 c0, u32 c1, u32 c2)
{
	zynqmp_disp_blend_set_bg_color(&disp->blend, c0, c1, c2);
}

/**
 * zynqmp_disp_set_alpha - Set the alpha value
 * @disp: Display subsystem
 * @alpha: alpha value to set
 *
 * Set the alpha value for blending.
 */
static void zynqmp_disp_set_alpha(struct zynqmp_disp *disp, u32 alpha)
{
	disp->alpha = alpha;
	zynqmp_disp_blend_set_alpha(&disp->blend, alpha);
}

/**
 * zynqmp_disp_get_alpha - Get the alpha value
 * @disp: Display subsystem
 *
 * Get the alpha value for blending.
 *
 * Return: current alpha value.
 */
static u32 zynqmp_disp_get_alpha(struct zynqmp_disp *disp)
{
	return disp->alpha;
}

/**
 * zynqmp_disp_set_g_alpha - Enable/disable the global alpha blending
 * @disp: Display subsystem
 * @enable: flag to enable or disable alpha blending
 *
 * Set the alpha value for blending.
 */
static void zynqmp_disp_set_g_alpha(struct zynqmp_disp *disp, bool enable)
{
	disp->alpha_en = enable;
	zynqmp_disp_blend_enable_alpha(&disp->blend, enable);
}

/**
 * zynqmp_disp_get_g_alpha - Get the global alpha status
 * @disp: Display subsystem
 *
 * Get the global alpha statue.
 *
 * Return: true if global alpha is enabled, or false.
 */
static bool zynqmp_disp_get_g_alpha(struct zynqmp_disp *disp)
{
	return disp->alpha_en;
}

/**
 * zynqmp_disp_enable - Enable the Display subsystem
 * @disp: Display subsystem
 *
 * Enable the Display subsystem.
 */
static void zynqmp_disp_enable(struct zynqmp_disp *disp)
{
	bool live;

	if (disp->enabled)
		return;

	zynqmp_disp_av_buf_enable(&disp->av_buf);
	/* Choose clock source based on the DT clock handle */
	zynqmp_disp_av_buf_set_vid_clock_src(&disp->av_buf, !!disp->_ps_pclk);
	zynqmp_disp_av_buf_set_aud_clock_src(&disp->av_buf, !!disp->_ps_audclk);
	live = zynqmp_disp_layer_is_live(disp);
	zynqmp_disp_av_buf_set_vid_timing_src(&disp->av_buf, !live);
	zynqmp_disp_av_buf_enable_buf(&disp->av_buf);
	zynqmp_disp_av_buf_enable_aud(&disp->av_buf);
	zynqmp_disp_aud_init(&disp->aud);
	disp->enabled = true;
}

/**
 * zynqmp_disp_disable - Disable the Display subsystem
 * @disp: Display subsystem
 * @force: flag to disable forcefully
 *
 * Disable the Display subsystem.
 */
static void zynqmp_disp_disable(struct zynqmp_disp *disp, bool force)
{
	struct drm_crtc *crtc = &disp->xlnx_crtc.crtc;

	if (!force && (!disp->enabled || zynqmp_disp_layer_is_enabled(disp)))
		return;

	zynqmp_disp_aud_deinit(&disp->aud);
	zynqmp_disp_av_buf_disable_aud(&disp->av_buf);
	zynqmp_disp_av_buf_disable_buf(&disp->av_buf);
	zynqmp_disp_av_buf_disable(&disp->av_buf);

	/* Mark the flip is done as crtc is disabled anyway */
	if (crtc->state->event) {
		complete_all(crtc->state->event->base.completion);
		crtc->state->event = NULL;
	}

	disp->enabled = false;
}

/**
 * zynqmp_disp_init - Initialize the Display subsystem states
 * @disp: Display subsystem
 *
 * Some states are not initialized as desired. For example, the output select
 * register resets to the live source. This function is to initialize
 * some register states as desired.
 */
static void zynqmp_disp_init(struct zynqmp_disp *disp)
{
	struct zynqmp_disp_layer *layer;
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++) {
		layer = &disp->layers[i];
		zynqmp_disp_av_buf_disable_vid(&disp->av_buf, layer);
	}
}

/*
 * ZynqMP Display external functions for zynqmp_dp
 */

/**
 * zynqmp_disp_handle_vblank - Handle the vblank event
 * @disp: Display subsystem
 *
 * This function handles the vblank interrupt, and sends an event to
 * CRTC object. This will be called by the DP vblank interrupt handler.
 */
void zynqmp_disp_handle_vblank(struct zynqmp_disp *disp)
{
	struct drm_crtc *crtc = &disp->xlnx_crtc.crtc;

	drm_crtc_handle_vblank(crtc);
}

/**
 * zynqmp_disp_get_apb_clk_rate - Get the current APB clock rate
 * @disp: Display subsystem
 *
 * Return: the current APB clock rate.
 */
unsigned int zynqmp_disp_get_apb_clk_rate(struct zynqmp_disp *disp)
{
	return clk_get_rate(disp->aclk);
}

/**
 * zynqmp_disp_aud_enabled - If the audio is enabled
 * @disp: Display subsystem
 *
 * Return if the audio is enabled depending on the audio clock.
 *
 * Return: true if audio is enabled, or false.
 */
bool zynqmp_disp_aud_enabled(struct zynqmp_disp *disp)
{
	return !!disp->audclk;
}

/**
 * zynqmp_disp_get_aud_clk_rate - Get the current audio clock rate
 * @disp: Display subsystem
 *
 * Return: the current audio clock rate.
 */
unsigned int zynqmp_disp_get_aud_clk_rate(struct zynqmp_disp *disp)
{
	if (zynqmp_disp_aud_enabled(disp))
		return 0;
	return clk_get_rate(disp->aclk);
}

/**
 * zynqmp_disp_get_crtc_mask - Return the CRTC bit mask
 * @disp: Display subsystem
 *
 * Return: the crtc mask of the zyqnmp_disp CRTC.
 */
uint32_t zynqmp_disp_get_crtc_mask(struct zynqmp_disp *disp)
{
	return drm_crtc_mask(&disp->xlnx_crtc.crtc);
}

/*
 * DRM plane functions
 */

static inline struct zynqmp_disp_layer *plane_to_layer(struct drm_plane *plane)
{
	return container_of(plane, struct zynqmp_disp_layer, plane);
}

static int zynqmp_disp_plane_enable(struct drm_plane *plane)
{
	struct zynqmp_disp_layer *layer = plane_to_layer(plane);
	struct zynqmp_disp *disp = layer->disp;
	int ret;

	zynqmp_disp_set_g_alpha(disp, disp->alpha_en);
	zynqmp_disp_set_alpha(disp, disp->alpha);
	ret = zynqmp_disp_layer_enable(layer->disp, layer,
				       ZYNQMP_DISP_LAYER_NONLIVE);
	if (ret)
		return ret;

	if (layer->id == ZYNQMP_DISP_LAYER_GFX && disp->tpg_on) {
		layer = &disp->layers[ZYNQMP_DISP_LAYER_VID];
		zynqmp_disp_layer_set_tpg(disp, layer, disp->tpg_on);
	}

	return 0;
}

static int zynqmp_disp_plane_disable(struct drm_plane *plane)
{
	struct zynqmp_disp_layer *layer = plane_to_layer(plane);
	struct zynqmp_disp *disp = layer->disp;

	zynqmp_disp_layer_disable(disp, layer, ZYNQMP_DISP_LAYER_NONLIVE);
	if (layer->id == ZYNQMP_DISP_LAYER_VID && disp->tpg_on)
		zynqmp_disp_layer_set_tpg(disp, layer, disp->tpg_on);

	return 0;
}

static int zynqmp_disp_plane_mode_set(struct drm_plane *plane,
				      struct drm_framebuffer *fb,
				      int crtc_x, int crtc_y,
				      unsigned int crtc_w, unsigned int crtc_h,
				      u32 src_x, u32 src_y,
				      u32 src_w, u32 src_h)
{
	struct zynqmp_disp_layer *layer = plane_to_layer(plane);
	const struct drm_format_info *info = fb->format;
	struct device *dev = layer->disp->dev;
	dma_addr_t paddr;
	unsigned int i;
	int ret;

	if (!info) {
		dev_err(dev, "No format info found\n");
		return -EINVAL;
	}

	ret = zynqmp_disp_layer_check_size(layer->disp, layer, src_w, src_h);
	if (ret)
		return ret;

	for (i = 0; i < info->num_planes; i++) {
		unsigned int width = src_w / (i ? info->hsub : 1);
		unsigned int height = src_h / (i ? info->vsub : 1);
		int width_bytes;

		paddr = drm_fb_cma_get_gem_addr(fb, plane->state, i);
		if (!paddr) {
			dev_err(dev, "failed to get a paddr\n");
			return -EINVAL;
		}

		layer->dma[i].xt.numf = height;
		width_bytes = drm_format_plane_width_bytes(info, i, width);
		layer->dma[i].sgl[0].size = width_bytes;
		layer->dma[i].sgl[0].icg = fb->pitches[i] -
					   layer->dma[i].sgl[0].size;
		layer->dma[i].xt.src_start = paddr;
		layer->dma[i].xt.frame_size = 1;
		layer->dma[i].xt.dir = DMA_MEM_TO_DEV;
		layer->dma[i].xt.src_sgl = true;
		layer->dma[i].xt.dst_sgl = false;
		layer->dma[i].is_active = true;
	}

	for (; i < ZYNQMP_DISP_MAX_NUM_SUB_PLANES; i++)
		layer->dma[i].is_active = false;

	ret = zynqmp_disp_layer_set_fmt(layer->disp,  layer, info->format);
	if (ret)
		dev_err(dev, "failed to set dp_sub layer fmt\n");

	return ret;
}

static void zynqmp_disp_plane_destroy(struct drm_plane *plane)
{
	struct zynqmp_disp_layer *layer = plane_to_layer(plane);

	xlnx_bridge_unregister(&layer->bridge);
	drm_plane_cleanup(plane);
}

static int
zynqmp_disp_plane_atomic_set_property(struct drm_plane *plane,
				      struct drm_plane_state *state,
				      struct drm_property *property, u64 val)
{
	struct zynqmp_disp_layer *layer = plane_to_layer(plane);
	struct zynqmp_disp *disp = layer->disp;
	int ret = 0;

	if (property == disp->g_alpha_prop)
		zynqmp_disp_set_alpha(disp, val);
	else if (property == disp->g_alpha_en_prop)
		zynqmp_disp_set_g_alpha(disp, val);
	else if (property == disp->tpg_prop)
		ret = zynqmp_disp_layer_set_tpg(disp, layer, val);
	else
		return -EINVAL;

	return ret;
}

static int
zynqmp_disp_plane_atomic_get_property(struct drm_plane *plane,
				      const struct drm_plane_state *state,
				      struct drm_property *property,
				      uint64_t *val)
{
	struct zynqmp_disp_layer *layer = plane_to_layer(plane);
	struct zynqmp_disp *disp = layer->disp;
	int ret = 0;

	if (property == disp->g_alpha_prop)
		*val = zynqmp_disp_get_alpha(disp);
	else if (property == disp->g_alpha_en_prop)
		*val = zynqmp_disp_get_g_alpha(disp);
	else if (property == disp->tpg_prop)
		*val = zynqmp_disp_layer_get_tpg(disp, layer);
	else
		return -EINVAL;

	return ret;
}

static int
zynqmp_disp_plane_atomic_update_plane(struct drm_plane *plane,
				      struct drm_crtc *crtc,
				      struct drm_framebuffer *fb,
				      int crtc_x, int crtc_y,
				      unsigned int crtc_w, unsigned int crtc_h,
				      u32 src_x, u32 src_y,
				      u32 src_w, u32 src_h,
				      struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	int ret;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;
	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state)) {
		ret = PTR_ERR(plane_state);
		goto fail;
	}

	ret = drm_atomic_set_crtc_for_plane(plane_state, crtc);
	if (ret)
		goto fail;
	drm_atomic_set_fb_for_plane(plane_state, fb);
	plane_state->crtc_x = crtc_x;
	plane_state->crtc_y = crtc_y;
	plane_state->crtc_w = crtc_w;
	plane_state->crtc_h = crtc_h;
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;
	plane_state->src_w = src_w;
	plane_state->src_h = src_h;

	if (plane == crtc->cursor)
		state->legacy_cursor_update = true;

	/* Do async-update if possible */
	state->async_update = !drm_atomic_helper_async_check(plane->dev, state);
	ret = drm_atomic_commit(state);
fail:
	drm_atomic_state_put(state);
	return ret;
}

static struct drm_plane_funcs zynqmp_disp_plane_funcs = {
	.update_plane		= zynqmp_disp_plane_atomic_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.atomic_set_property	= zynqmp_disp_plane_atomic_set_property,
	.atomic_get_property	= zynqmp_disp_plane_atomic_get_property,
	.destroy		= zynqmp_disp_plane_destroy,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static void
zynqmp_disp_plane_atomic_update(struct drm_plane *plane,
				struct drm_plane_state *old_state)
{
	int ret;

	if (!plane->state->crtc || !plane->state->fb)
		return;

	if (plane->state->fb == old_state->fb &&
	    plane->state->crtc_x == old_state->crtc_x &&
	    plane->state->crtc_y == old_state->crtc_y &&
	    plane->state->crtc_w == old_state->crtc_w &&
	    plane->state->crtc_h == old_state->crtc_h &&
	    plane->state->src_x == old_state->src_x &&
	    plane->state->src_y == old_state->src_y &&
	    plane->state->src_w == old_state->src_w &&
	    plane->state->src_h == old_state->src_h)
		return;

	if (old_state->fb &&
	    old_state->fb->format->format != plane->state->fb->format->format)
		zynqmp_disp_plane_disable(plane);

	ret = zynqmp_disp_plane_mode_set(plane, plane->state->fb,
					 plane->state->crtc_x,
					 plane->state->crtc_y,
					 plane->state->crtc_w,
					 plane->state->crtc_h,
					 plane->state->src_x >> 16,
					 plane->state->src_y >> 16,
					 plane->state->src_w >> 16,
					 plane->state->src_h >> 16);
	if (ret)
		return;

	zynqmp_disp_plane_enable(plane);
}

static void
zynqmp_disp_plane_atomic_disable(struct drm_plane *plane,
				 struct drm_plane_state *old_state)
{
	zynqmp_disp_plane_disable(plane);
}

static int zynqmp_disp_plane_atomic_async_check(struct drm_plane *plane,
						struct drm_plane_state *state)
{
	return 0;
}

static void
zynqmp_disp_plane_atomic_async_update(struct drm_plane *plane,
				      struct drm_plane_state *new_state)
{
	int ret;

	if (plane->state->fb == new_state->fb)
		return;

	if (plane->state->fb &&
	    plane->state->fb->format->format != new_state->fb->format->format)
		zynqmp_disp_plane_disable(plane);

	 /* Update the current state with new configurations */
	swap(plane->state->fb, new_state->fb);
	plane->state->crtc = new_state->crtc;
	plane->state->crtc_x = new_state->crtc_x;
	plane->state->crtc_y = new_state->crtc_y;
	plane->state->crtc_w = new_state->crtc_w;
	plane->state->crtc_h = new_state->crtc_h;
	plane->state->src_x = new_state->src_x;
	plane->state->src_y = new_state->src_y;
	plane->state->src_w = new_state->src_w;
	plane->state->src_h = new_state->src_h;
	plane->state->state = new_state->state;

	ret = zynqmp_disp_plane_mode_set(plane, plane->state->fb,
					 plane->state->crtc_x,
					 plane->state->crtc_y,
					 plane->state->crtc_w,
					 plane->state->crtc_h,
					 plane->state->src_x >> 16,
					 plane->state->src_y >> 16,
					 plane->state->src_w >> 16,
					 plane->state->src_h >> 16);
	if (ret)
		return;

	zynqmp_disp_plane_enable(plane);
}

static const struct drm_plane_helper_funcs zynqmp_disp_plane_helper_funcs = {
	.atomic_update		= zynqmp_disp_plane_atomic_update,
	.atomic_disable		= zynqmp_disp_plane_atomic_disable,
	.atomic_async_check	= zynqmp_disp_plane_atomic_async_check,
	.atomic_async_update	= zynqmp_disp_plane_atomic_async_update,
};

static int zynqmp_disp_create_plane(struct zynqmp_disp *disp)
{
	struct zynqmp_disp_layer *layer;
	unsigned int i;
	u32 *fmts = NULL;
	unsigned int num_fmts = 0;
	enum drm_plane_type type;
	int ret;

	/* graphics layer is primary, and video layer is overaly */
	type = DRM_PLANE_TYPE_OVERLAY;
	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++) {
		layer = &disp->layers[i];
		zynqmp_disp_layer_get_fmts(disp, layer, &fmts, &num_fmts);
		ret = drm_universal_plane_init(disp->drm, &layer->plane, 0,
					       &zynqmp_disp_plane_funcs, fmts,
					       num_fmts, NULL, type, NULL);
		if (ret)
			goto err_plane;
		drm_plane_helper_add(&layer->plane,
				     &zynqmp_disp_plane_helper_funcs);
		type = DRM_PLANE_TYPE_PRIMARY;
	}

	/* Attach properties to each layers */
	drm_object_attach_property(&layer->plane.base, disp->g_alpha_prop,
				   ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA_MAX);
	disp->alpha = ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA_MAX;
	/* Enable the global alpha as default */
	drm_object_attach_property(&layer->plane.base, disp->g_alpha_en_prop,
				   true);
	disp->alpha_en = true;

	layer = &disp->layers[ZYNQMP_DISP_LAYER_VID];
	drm_object_attach_property(&layer->plane.base, disp->tpg_prop, false);

	return ret;

err_plane:
	if (i)
		drm_plane_cleanup(&disp->layers[0].plane);
	return ret;
}

static void zynqmp_disp_destroy_plane(struct zynqmp_disp *disp)
{
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++)
		zynqmp_disp_plane_destroy(&disp->layers[i].plane);
}

/*
 * Xlnx crtc functions
 */

static inline struct zynqmp_disp *xlnx_crtc_to_disp(struct xlnx_crtc *xlnx_crtc)
{
	return container_of(xlnx_crtc, struct zynqmp_disp, xlnx_crtc);
}

static int zynqmp_disp_get_max_width(struct xlnx_crtc *xlnx_crtc)
{
	return ZYNQMP_DISP_MAX_WIDTH;
}

static int zynqmp_disp_get_max_height(struct xlnx_crtc *xlnx_crtc)
{
	return ZYNQMP_DISP_MAX_HEIGHT;
}

static uint32_t zynqmp_disp_get_format(struct xlnx_crtc *xlnx_crtc)
{
	struct zynqmp_disp *disp = xlnx_crtc_to_disp(xlnx_crtc);

	return disp->layers[ZYNQMP_DISP_LAYER_GFX].fmt->drm_fmt;
}

static unsigned int zynqmp_disp_get_align(struct xlnx_crtc *xlnx_crtc)
{
	struct zynqmp_disp *disp = xlnx_crtc_to_disp(xlnx_crtc);
	struct zynqmp_disp_layer *layer = &disp->layers[ZYNQMP_DISP_LAYER_VID];

	return 1 << layer->dma->chan->device->copy_align;
}

static u64 zynqmp_disp_get_dma_mask(struct xlnx_crtc *xlnx_crtc)
{
	return DMA_BIT_MASK(ZYNQMP_DISP_MAX_DMA_BIT);
}

/*
 * DRM crtc functions
 */

static inline struct zynqmp_disp *crtc_to_disp(struct drm_crtc *crtc)
{
	struct xlnx_crtc *xlnx_crtc = to_xlnx_crtc(crtc);

	return xlnx_crtc_to_disp(xlnx_crtc);
}

static int zynqmp_disp_crtc_mode_set(struct drm_crtc *crtc,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode,
				     int x, int y,
				     struct drm_framebuffer *old_fb)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);
	unsigned long rate;
	long diff;
	int ret;

	zynqmp_disp_clk_disable(disp->pclk, &disp->pclk_en);
	ret = clk_set_rate(disp->pclk,
			   (unsigned long)adjusted_mode->clock * 1000);
	if (ret) {
		dev_err(disp->dev, "failed to set a pixel clock\n");
		return ret;
	}

	rate = clk_get_rate(disp->pclk);
	diff = rate - (unsigned long)adjusted_mode->clock * 1000;
	if (abs(diff) > ((long)adjusted_mode->clock * 1000) / 20) {
		dev_info(disp->dev, "request pixel rate: %d actual rate: %lu\n",
			 adjusted_mode->clock, rate);
	} else {
		dev_dbg(disp->dev, "request pixel rate: %d actual rate: %lu\n",
			adjusted_mode->clock, rate);
	}

	/* The timing register should be programmed always */
	zynqmp_dp_encoder_mode_set_stream(disp->dpsub->dp, adjusted_mode);

	return 0;
}

static void
zynqmp_disp_crtc_atomic_enable(struct drm_crtc *crtc,
			       struct drm_crtc_state *old_crtc_state)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	int ret, vrefresh;

	zynqmp_disp_crtc_mode_set(crtc, &crtc->state->mode,
				  adjusted_mode, crtc->x, crtc->y, NULL);

	ret = pm_runtime_get_sync(disp->dev);
	if (ret < 0) {
		dev_err(disp->dev, "IRQ sync failed to resume: %d\n", ret);
		return;
	}

	ret = zynqmp_disp_clk_enable(disp->pclk, &disp->pclk_en);
	if (ret) {
		dev_err(disp->dev, "failed to enable a pixel clock\n");
		return;
	}
	zynqmp_disp_set_output_fmt(disp, disp->color);
	zynqmp_disp_set_bg_color(disp, disp->bg_c0, disp->bg_c1, disp->bg_c2);
	zynqmp_disp_enable(disp);
	/* Delay of 3 vblank intervals for timing gen to be stable */
	vrefresh = (adjusted_mode->clock * 1000) /
		   (adjusted_mode->vtotal * adjusted_mode->htotal);
	msleep(3 * 1000 / vrefresh);
}

static void
zynqmp_disp_crtc_atomic_disable(struct drm_crtc *crtc,
				struct drm_crtc_state *old_crtc_state)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);

	zynqmp_disp_clk_disable(disp->pclk, &disp->pclk_en);
	zynqmp_disp_plane_disable(crtc->primary);
	zynqmp_disp_disable(disp, true);
	if (!disp->dpsub->external_crtc_attached)
		drm_crtc_vblank_off(crtc);
	pm_runtime_put_sync(disp->dev);
}

static int zynqmp_disp_crtc_atomic_check(struct drm_crtc *crtc,
					 struct drm_crtc_state *state)
{
	return drm_atomic_add_affected_planes(state->state, crtc);
}

static void
zynqmp_disp_crtc_atomic_begin(struct drm_crtc *crtc,
			      struct drm_crtc_state *old_crtc_state)
{
	drm_crtc_vblank_on(crtc);
	/* Don't rely on vblank when disabling crtc */
	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		/* Consume the flip_done event from atomic helper */
		crtc->state->event->pipe = drm_crtc_index(crtc);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		drm_crtc_arm_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static struct drm_crtc_helper_funcs zynqmp_disp_crtc_helper_funcs = {
	.atomic_enable	= zynqmp_disp_crtc_atomic_enable,
	.atomic_disable	= zynqmp_disp_crtc_atomic_disable,
	.atomic_check	= zynqmp_disp_crtc_atomic_check,
	.atomic_begin	= zynqmp_disp_crtc_atomic_begin,
};

static void zynqmp_disp_crtc_destroy(struct drm_crtc *crtc)
{
	zynqmp_disp_crtc_atomic_disable(crtc, NULL);
	drm_crtc_cleanup(crtc);
}

static int zynqmp_disp_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);

	zynqmp_dp_enable_vblank(disp->dpsub->dp);

	return 0;
}

static void zynqmp_disp_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);

	zynqmp_dp_disable_vblank(disp->dpsub->dp);
}

static int
zynqmp_disp_crtc_atomic_set_property(struct drm_crtc *crtc,
				     struct drm_crtc_state *state,
				     struct drm_property *property,
				     uint64_t val)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);

	/*
	 * CRTC prop values are just stored here and applied when CRTC gets
	 * enabled
	 */
	if (property == disp->color_prop)
		disp->color = val;
	else if (property == disp->bg_c0_prop)
		disp->bg_c0 = val;
	else if (property == disp->bg_c1_prop)
		disp->bg_c1 = val;
	else if (property == disp->bg_c2_prop)
		disp->bg_c2 = val;
	else
		return -EINVAL;

	return 0;
}

static int
zynqmp_disp_crtc_atomic_get_property(struct drm_crtc *crtc,
				     const struct drm_crtc_state *state,
				     struct drm_property *property,
				     uint64_t *val)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);

	if (property == disp->color_prop)
		*val = disp->color;
	else if (property == disp->bg_c0_prop)
		*val = disp->bg_c0;
	else if (property == disp->bg_c1_prop)
		*val = disp->bg_c1;
	else if (property == disp->bg_c2_prop)
		*val = disp->bg_c2;
	else
		return -EINVAL;

	return 0;
}

static struct drm_crtc_funcs zynqmp_disp_crtc_funcs = {
	.destroy		= zynqmp_disp_crtc_destroy,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_set_property	= zynqmp_disp_crtc_atomic_set_property,
	.atomic_get_property	= zynqmp_disp_crtc_atomic_get_property,
	.reset			= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= zynqmp_disp_crtc_enable_vblank,
	.disable_vblank		= zynqmp_disp_crtc_disable_vblank,
};

static int zynqmp_disp_create_crtc(struct zynqmp_disp *disp)
{
	struct drm_plane *plane = &disp->layers[ZYNQMP_DISP_LAYER_GFX].plane;
	struct drm_mode_object *obj = &disp->xlnx_crtc.crtc.base;
	int ret;

	ret = drm_crtc_init_with_planes(disp->drm, &disp->xlnx_crtc.crtc,
					plane, NULL, &zynqmp_disp_crtc_funcs,
					NULL);
	if (ret < 0) {
		dev_err(disp->dev, "failed to initialize disp CRTC: %d\n",
			ret);
		return ret;
	}

	drm_crtc_helper_add(&disp->xlnx_crtc.crtc,
			    &zynqmp_disp_crtc_helper_funcs);
	drm_object_attach_property(obj, disp->color_prop, 0);
	zynqmp_dp_set_color(disp->dpsub->dp, zynqmp_disp_color_enum[0].name);
	drm_object_attach_property(obj, disp->bg_c0_prop, 0);
	drm_object_attach_property(obj, disp->bg_c1_prop, 0);
	drm_object_attach_property(obj, disp->bg_c2_prop, 0);

	disp->xlnx_crtc.get_max_width = &zynqmp_disp_get_max_width;
	disp->xlnx_crtc.get_max_height = &zynqmp_disp_get_max_height;
	disp->xlnx_crtc.get_format = &zynqmp_disp_get_format;
	disp->xlnx_crtc.get_align = &zynqmp_disp_get_align;
	disp->xlnx_crtc.get_dma_mask = &zynqmp_disp_get_dma_mask;
	/* Only register the PS DP CRTC if there is no external port/CRTC */
	if (!disp->dpsub->external_crtc_attached)
		xlnx_crtc_register(disp->drm, &disp->xlnx_crtc);

	return 0;
}

static void zynqmp_disp_destroy_crtc(struct zynqmp_disp *disp)
{
	if (!disp->dpsub->external_crtc_attached)
		xlnx_crtc_unregister(disp->drm, &disp->xlnx_crtc);
	zynqmp_disp_crtc_destroy(&disp->xlnx_crtc.crtc);
}

static void zynqmp_disp_map_crtc_to_plane(struct zynqmp_disp *disp)
{
	u32 possible_crtcs = drm_crtc_mask(&disp->xlnx_crtc.crtc);
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++)
		disp->layers[i].plane.possible_crtcs = possible_crtcs;
}

/*
 * Xlnx bridge functions
 */

static inline struct zynqmp_disp_layer
*bridge_to_layer(struct xlnx_bridge *bridge)
{
	return container_of(bridge, struct zynqmp_disp_layer, bridge);
}

static int zynqmp_disp_bridge_enable(struct xlnx_bridge *bridge)
{
	struct zynqmp_disp_layer *layer = bridge_to_layer(bridge);
	struct zynqmp_disp *disp = layer->disp;
	struct drm_crtc *crtc = &disp->xlnx_crtc.crtc;
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	struct videomode vm;
	int ret;

	if (!disp->_pl_pclk) {
		dev_err(disp->dev, "PL clock is required for live\n");
		return -ENODEV;
	}

	ret = zynqmp_disp_layer_check_size(disp, layer, layer->w, layer->h);
	if (ret)
		return ret;

	/* Enable DP encoder if external CRTC attached */
	if (disp->dpsub->external_crtc_attached)
		zynqmp_disp_crtc_atomic_enable(crtc, NULL);

	if (disp->vtc_bridge) {
		drm_display_mode_to_videomode(adjusted_mode, &vm);
		xlnx_bridge_set_timing(disp->vtc_bridge, &vm);
		xlnx_bridge_enable(disp->vtc_bridge);
	}

	/* If external CRTC is connected through video layer, set alpha to 0 */
	if (disp->dpsub->external_crtc_attached && layer->id == ZYNQMP_DISP_LAYER_VID)
		disp->alpha = 0;

	zynqmp_disp_set_g_alpha(disp, disp->alpha_en);
	zynqmp_disp_set_alpha(disp, disp->alpha);
	ret = zynqmp_disp_layer_enable(layer->disp, layer,
				       ZYNQMP_DISP_LAYER_LIVE);
	if (ret)
		return ret;

	if (layer->id == ZYNQMP_DISP_LAYER_GFX && disp->tpg_on) {
		layer = &disp->layers[ZYNQMP_DISP_LAYER_VID];
		zynqmp_disp_layer_set_tpg(disp, layer, disp->tpg_on);
	}

	if (zynqmp_disp_av_buf_vid_timing_src_is_int(&disp->av_buf) ||
	    zynqmp_disp_av_buf_vid_clock_src_is_ps(&disp->av_buf)) {
		dev_info(disp->dev,
			 "Disabling the pipeline to change the clk/timing src");
		zynqmp_disp_disable(disp, true);
		zynqmp_disp_av_buf_set_vid_clock_src(&disp->av_buf, false);
		zynqmp_disp_av_buf_set_vid_timing_src(&disp->av_buf, false);
	}

	zynqmp_disp_enable(disp);

	return 0;
}

static void zynqmp_disp_bridge_disable(struct xlnx_bridge *bridge)
{
	struct zynqmp_disp_layer *layer = bridge_to_layer(bridge);
	struct zynqmp_disp *disp = layer->disp;

	zynqmp_disp_disable(disp, false);

	zynqmp_disp_layer_disable(disp, layer, ZYNQMP_DISP_LAYER_LIVE);
	if (layer->id == ZYNQMP_DISP_LAYER_VID && disp->tpg_on)
		zynqmp_disp_layer_set_tpg(disp, layer, disp->tpg_on);

	if (!zynqmp_disp_layer_is_live(disp)) {
		dev_info(disp->dev,
			 "Disabling the pipeline to change the clk/timing src");
		zynqmp_disp_disable(disp, true);
		zynqmp_disp_av_buf_set_vid_clock_src(&disp->av_buf, true);
		zynqmp_disp_av_buf_set_vid_timing_src(&disp->av_buf, true);
		if (zynqmp_disp_layer_is_enabled(disp))
			zynqmp_disp_enable(disp);
	}
}

static int zynqmp_disp_bridge_set_input(struct xlnx_bridge *bridge,
					u32 width, u32 height, u32 bus_fmt)
{
	struct zynqmp_disp_layer *layer = bridge_to_layer(bridge);
	int ret;

	ret = zynqmp_disp_layer_check_size(layer->disp, layer, width, height);
	if (ret)
		return ret;

	ret = zynqmp_disp_layer_set_live_fmt(layer->disp,  layer, bus_fmt);
	if (ret)
		dev_err(layer->disp->dev, "failed to set live fmt\n");

	return ret;
}

static int zynqmp_disp_bridge_get_input_fmts(struct xlnx_bridge *bridge,
					     const u32 **fmts, u32 *count)
{
	struct zynqmp_disp_layer *layer = bridge_to_layer(bridge);

	*fmts = layer->bus_fmts;
	*count = layer->num_bus_fmts;

	return 0;
}

static int zynqmp_disp_bridge_set_timing(struct xlnx_bridge *bridge,
					 struct videomode *vm)
{
	struct zynqmp_disp_layer *layer = bridge_to_layer(bridge);
	struct zynqmp_disp *disp = layer->disp;
	struct drm_crtc *crtc = &disp->xlnx_crtc.crtc;
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;

	drm_display_mode_from_videomode(vm, adjusted_mode);

	return 0;
}

/*
 * Component functions
 */

int zynqmp_disp_bind(struct device *dev, struct device *master, void *data)
{
	struct zynqmp_dpsub *dpsub = dev_get_drvdata(dev);
	struct zynqmp_disp *disp = dpsub->disp;
	struct drm_device *drm = data;
	int num;
	u64 max;
	int ret;

	disp->drm = drm;

	max = ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA_MAX;
	disp->g_alpha_prop = drm_property_create_range(drm, 0, "alpha", 0, max);
	disp->g_alpha_en_prop = drm_property_create_bool(drm, 0,
							 "g_alpha_en");
	num = ARRAY_SIZE(zynqmp_disp_color_enum);
	disp->color_prop = drm_property_create_enum(drm, 0,
						    "output_color",
						    zynqmp_disp_color_enum,
						    num);
	max = ZYNQMP_DISP_V_BLEND_BG_MAX;
	disp->bg_c0_prop = drm_property_create_range(drm, 0, "bg_c0", 0, max);
	disp->bg_c1_prop = drm_property_create_range(drm, 0, "bg_c1", 0, max);
	disp->bg_c2_prop = drm_property_create_range(drm, 0, "bg_c2", 0, max);
	disp->tpg_prop = drm_property_create_bool(drm, 0, "tpg");

	ret = zynqmp_disp_create_plane(disp);
	if (ret)
		return ret;
	ret = zynqmp_disp_create_crtc(disp);
	if (ret)
		return ret;
	zynqmp_disp_map_crtc_to_plane(disp);

	return 0;
}

void zynqmp_disp_unbind(struct device *dev, struct device *master, void *data)
{
	struct zynqmp_dpsub *dpsub = dev_get_drvdata(dev);
	struct zynqmp_disp *disp = dpsub->disp;

	zynqmp_disp_destroy_crtc(disp);
	zynqmp_disp_destroy_plane(disp);
	drm_property_destroy(disp->drm, disp->bg_c2_prop);
	drm_property_destroy(disp->drm, disp->bg_c1_prop);
	drm_property_destroy(disp->drm, disp->bg_c0_prop);
	drm_property_destroy(disp->drm, disp->color_prop);
	drm_property_destroy(disp->drm, disp->g_alpha_en_prop);
	drm_property_destroy(disp->drm, disp->g_alpha_prop);
}

/*
 * Platform initialization functions
 */

static int zynqmp_disp_enumerate_fmts(struct zynqmp_disp *disp)
{
	struct zynqmp_disp_layer *layer;
	u32 *bus_fmts;
	u32 i, size, num_bus_fmts;
	u32 gfx_fmt = ZYNQMP_DISP_AV_BUF_GFX_FMT_RGB565;

	num_bus_fmts = ARRAY_SIZE(av_buf_live_fmts);
	bus_fmts = devm_kzalloc(disp->dev, sizeof(*bus_fmts) * num_bus_fmts,
				GFP_KERNEL);
	if (!bus_fmts)
		return -ENOMEM;
	for (i = 0; i < num_bus_fmts; i++)
		bus_fmts[i] = av_buf_live_fmts[i].bus_fmt;

	layer = &disp->layers[ZYNQMP_DISP_LAYER_VID];
	layer->num_bus_fmts = num_bus_fmts;
	layer->bus_fmts = bus_fmts;
	size = ARRAY_SIZE(av_buf_vid_fmts);
	layer->num_fmts = size;
	layer->drm_fmts = devm_kzalloc(disp->dev,
				       sizeof(*layer->drm_fmts) * size,
				       GFP_KERNEL);
	if (!layer->drm_fmts)
		return -ENOMEM;
	for (i = 0; i < layer->num_fmts; i++)
		layer->drm_fmts[i] = av_buf_vid_fmts[i].drm_fmt;
	layer->fmt = &av_buf_vid_fmts[ZYNQMP_DISP_AV_BUF_VID_FMT_YUYV];

	layer = &disp->layers[ZYNQMP_DISP_LAYER_GFX];
	layer->num_bus_fmts = num_bus_fmts;
	layer->bus_fmts = bus_fmts;
	size = ARRAY_SIZE(av_buf_gfx_fmts);
	layer->num_fmts = size;
	layer->drm_fmts = devm_kzalloc(disp->dev,
				       sizeof(*layer->drm_fmts) * size,
				       GFP_KERNEL);
	if (!layer->drm_fmts)
		return -ENOMEM;

	for (i = 0; i < layer->num_fmts; i++)
		layer->drm_fmts[i] = av_buf_gfx_fmts[i].drm_fmt;
	if (zynqmp_disp_gfx_init_fmt < ARRAY_SIZE(zynqmp_disp_gfx_init_fmts))
		gfx_fmt = zynqmp_disp_gfx_init_fmts[zynqmp_disp_gfx_init_fmt];
	layer->fmt = &av_buf_gfx_fmts[gfx_fmt];

	return 0;
}

int zynqmp_disp_probe(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub;
	struct zynqmp_disp *disp;
	struct resource *res;
	int ret;
	struct zynqmp_disp_layer *layer;
	unsigned int i, j;
	struct device_node *vtc_node;

	disp = devm_kzalloc(&pdev->dev, sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;
	disp->dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "blend");
	disp->blend.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(disp->blend.base))
		return PTR_ERR(disp->blend.base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "av_buf");
	disp->av_buf.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(disp->av_buf.base))
		return PTR_ERR(disp->av_buf.base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aud");
	disp->aud.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(disp->aud.base))
		return PTR_ERR(disp->aud.base);

	dpsub = platform_get_drvdata(pdev);
	dpsub->disp = disp;
	disp->dpsub = dpsub;

	ret = zynqmp_disp_enumerate_fmts(disp);
	if (ret)
		return ret;

	/* Try the live PL video clock */
	disp->_pl_pclk = devm_clk_get(disp->dev, "dp_live_video_in_clk");
	if (!IS_ERR(disp->_pl_pclk)) {
		disp->pclk = disp->_pl_pclk;
		ret = zynqmp_disp_clk_enable_disable(disp->pclk,
						     &disp->pclk_en);
		if (ret)
			disp->pclk = NULL;
	} else if (PTR_ERR(disp->_pl_pclk) == -EPROBE_DEFER) {
		return PTR_ERR(disp->_pl_pclk);
	}

	/* If the live PL video clock is not valid, fall back to PS clock */
	if (!disp->pclk) {
		disp->_ps_pclk = devm_clk_get(disp->dev, "dp_vtc_pixel_clk_in");
		if (IS_ERR(disp->_ps_pclk)) {
			dev_err(disp->dev, "failed to init any video clock\n");
			return PTR_ERR(disp->_ps_pclk);
		}
		disp->pclk = disp->_ps_pclk;
		ret = zynqmp_disp_clk_enable_disable(disp->pclk,
						     &disp->pclk_en);
		if (ret) {
			dev_err(disp->dev, "failed to init any video clock\n");
			return ret;
		}
	}

	disp->aclk = devm_clk_get(disp->dev, "dp_apb_clk");
	if (IS_ERR(disp->aclk))
		return PTR_ERR(disp->aclk);
	ret = zynqmp_disp_clk_enable(disp->aclk, &disp->aclk_en);
	if (ret) {
		dev_err(disp->dev, "failed to enable the APB clk\n");
		return ret;
	}

	/* Try the live PL audio clock */
	disp->_pl_audclk = devm_clk_get(disp->dev, "dp_live_audio_aclk");
	if (!IS_ERR(disp->_pl_audclk)) {
		disp->audclk = disp->_pl_audclk;
		ret = zynqmp_disp_clk_enable_disable(disp->audclk,
						     &disp->audclk_en);
		if (ret)
			disp->audclk = NULL;
	}

	/* If the live PL audio clock is not valid, fall back to PS clock */
	if (!disp->audclk) {
		disp->_ps_audclk = devm_clk_get(disp->dev, "dp_aud_clk");
		if (!IS_ERR(disp->_ps_audclk)) {
			disp->audclk = disp->_ps_audclk;
			ret = zynqmp_disp_clk_enable_disable(disp->audclk,
							     &disp->audclk_en);
			if (ret)
				disp->audclk = NULL;
		}

		if (!disp->audclk) {
			dev_err(disp->dev,
				"audio is disabled due to clock failure\n");
		}
	}

	/* VTC Bridge support */
	vtc_node = of_parse_phandle(disp->dev->of_node, "xlnx,bridge", 0);
	if (vtc_node) {
		disp->vtc_bridge = of_xlnx_bridge_get(vtc_node);
		if (!disp->vtc_bridge) {
			dev_info(disp->dev, "Didn't get vtc bridge instance\n");
			return -EPROBE_DEFER;
		}
	} else {
		dev_info(disp->dev, "vtc bridge property not present\n");
	}

	ret = zynqmp_disp_layer_create(disp);
	if (ret)
		goto error_aclk;

	zynqmp_disp_init(disp);

	/*
	 * Register live bridges so external CRTCs will be able probe
	 * successfully
	 */
	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++) {
		layer = &disp->layers[i];
		layer->bridge.enable = &zynqmp_disp_bridge_enable;
		layer->bridge.disable = &zynqmp_disp_bridge_disable;
		layer->bridge.set_input = &zynqmp_disp_bridge_set_input;
		layer->bridge.get_input_fmts =
			&zynqmp_disp_bridge_get_input_fmts;
		layer->bridge.set_timing = &zynqmp_disp_bridge_set_timing;
		layer->bridge.of_node = disp->dev->of_node;
		layer->bridge.extra_name = ((i == 0) ? ".vid" : ".gfx");
		ret = xlnx_bridge_register(&layer->bridge);
		if (ret) {
			dev_info(disp->dev, "Bridge registration failed\n");
			for (j = 0; j < i; j++)
				xlnx_bridge_unregister(&disp->layers[j].bridge);
			goto error_aclk;
		}
	}

	return 0;

error_aclk:
	zynqmp_disp_clk_disable(disp->aclk, &disp->aclk_en);
	return ret;
}

int zynqmp_disp_remove(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub = platform_get_drvdata(pdev);
	struct zynqmp_disp *disp = dpsub->disp;

	zynqmp_disp_layer_destroy(disp);
	if (disp->audclk)
		zynqmp_disp_clk_disable(disp->audclk, &disp->audclk_en);
	if (disp->vtc_bridge)
		of_xlnx_bridge_put(disp->vtc_bridge);
	zynqmp_disp_clk_disable(disp->aclk, &disp->aclk_en);
	zynqmp_disp_clk_disable(disp->pclk, &disp->pclk_en);
	dpsub->disp = NULL;

	return 0;
}

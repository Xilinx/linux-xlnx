// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated Display Controller Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/reset.h>

#include "mmi_dc.h"
#include "mmi_dc_plane.h"
#include "mmi_dc_audio.h"

/* DC DP Stream Registers */
#define MMI_DC_DP_MAIN_STREAM_HTOTAL	(0x0000)
#define MMI_DC_DP_MAIN_STREAM_VTOTAL	(0x0004)
#define MMI_DC_DP_MAIN_STREAM_HSWIDTH	(0x000c)
#define MMI_DC_DP_MAIN_STREAM_VSWIDTH	(0x0010)
#define MMI_DC_DP_MAIN_STREAM_HRES	(0x0014)
#define MMI_DC_DP_MAIN_STREAM_VRES	(0x0018)
#define MMI_DC_DP_MAIN_STREAM_HSTART	(0x001c)
#define MMI_DC_DP_MAIN_STREAM_VSTART	(0x0020)
#define MMI_DC_DP_MAIN_STREAM_MISC0	(0x0024)

#define MMI_DC_DP_MAIN_STREAM_BPC_MASK	GENMASK(7, 5)
#define MMI_DC_DP_MAIN_STREAM_BPC_SHIFT	(5)
#define MMI_DC_DP_MAIN_STREAM_BPC_12	(3 << MMI_DC_DP_MAIN_STREAM_BPC_SHIFT)

/* Blender Registers */
#define MMI_DC_V_BLEND_BG_CLR(cc)		(0x0000 + 4 * (cc))
#define MMI_BG_CLR_MIN				(0)
#define MMI_BG_CLR_MAX				GENMASK(11, 0)
#define MMI_DC_V_BLEND_GLOBAL_ALPHA		(0x000c)
#define MMI_DC_V_BLEND_OUTPUT_VID_FORMAT	(0x0014)
#define MMI_DC_V_BLEND_RGB2YCBCR_COEFF(coeff)	(0x0020 + 4 * (coeff))
#define MMI_DC_V_BLEND_CC_OUTCSC_OFFSET(cc)	(0x0074 + 4 * (cc))

#define MMI_DC_V_BLEND_ALPHA_VALUE(alpha)	((u32)(alpha) << 1)
#define MMI_DC_V_BLEND_EN_DOWNSAMPLE		BIT(4)

/* AV Buffer Registers */
#define MMI_DC_AV_BUF_NON_LIVE_LATENCY		(0x0008)
#define MMI_DC_AV_BUF_NON_LIVE_LATENCY_VAL	(0x20138)
#define MMI_DC_AV_BUF_SRST			(0x0124)

#define MMI_DC_AV_BUF_RESET_SHIFT		(1)
#define MMI_DC_AV_BUF_AUD_VID_CLK_SOURCE	(0x0120)
#define MMI_DC_AV_BUF_AUD_VID_TIMING_SRC_INT	BIT(2)

/* Misc Registers */
#define MMI_DC_MISC_VID_CLK			(0x0c5c)
#define MMI_DC_MISC_WPROTS			(0x0c70)
#define MMI_DC_VIDEO_FRAME_SWITCH		(0x0d80)
#define MMI_DC_VIDEO_FRAME_SWITCH_DP_VID0_IMM	BIT(5)
#define MMI_DC_VIDEO_FRAME_SWITCH_DP_VID0_EN	BIT(4)
#define MMI_DC_VIDEO_FRAME_SWITCH_PL_VID1_IMM	BIT(3)
#define MMI_DC_VIDEO_FRAME_SWITCH_PL_VID1_EN	BIT(2)
#define MMI_DC_VIDEO_FRAME_SWITCH_PL_VID0_IMM	BIT(1)
#define MMI_DC_VIDEO_FRAME_SWITCH_PL_VID0_EN	BIT(0)
#define MMI_DC_VIDEO_FRAME_SWITCH_EN_ALL	(MMI_DC_VIDEO_FRAME_SWITCH_DP_VID0_IMM	| \
						MMI_DC_VIDEO_FRAME_SWITCH_DP_VID0_EN	| \
						MMI_DC_VIDEO_FRAME_SWITCH_PL_VID1_IMM	| \
						MMI_DC_VIDEO_FRAME_SWITCH_PL_VID1_EN	| \
						MMI_DC_VIDEO_FRAME_SWITCH_PL_VID0_IMM	| \
						MMI_DC_VIDEO_FRAME_SWITCH_PL_VID0_EN)

#define MMI_DC_MISC_VID_CLK_PS			BIT(1)
#define MMI_DC_MISC_VID_CLK_PL			0

/* IRQ Registers */
#define MMI_DC_INT_STATUS			(0x0000)
#define MMI_DC_INT_MASK				(0x0004)
#define MMI_DC_INT_EN				(0x0008)
#define MMI_DC_INT_DS				(0x000c)

#define MMI_DC_INT_VBLANK			BIT(3)
#define MMI_DC_INT_PIXEL_MATCH			(BIT(4) | BIT(5))
#define MMI_DC_MSLEEP_50MS			(50)

/* ----------------------------------------------------------------------------
 * CSC Data
 */

const u16 csc_zero_matrix[MMI_DC_CSC_NUM_COEFFS] = {
	0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000,
};

const u16 csc_identity_matrix[MMI_DC_CSC_NUM_COEFFS] = {
	0x1000, 0x0000, 0x0000,
	0x0000, 0x1000, 0x0000,
	0x0000, 0x0000, 0x1000,
};

const u16 csc_rgb_to_sdtv_matrix[MMI_DC_CSC_NUM_COEFFS] = {
	0x04c9, 0x0864, 0x01d3,
	0x7d4d, 0x7ab3, 0x0800,
	0x0800, 0x794d, 0x7eb3,
};

const u16 csc_sdtv_to_rgb_matrix[MMI_DC_CSC_NUM_COEFFS] = {
	0x1000, 0x166f, 0x0000,
	0x1000, 0x7483, 0x7a7f,
	0x1000, 0x0000, 0x1c5a,
};

const u32 csc_zero_offsets[MMI_DC_CSC_NUM_OFFSETS] = {
	0x00000000, 0x00000000, 0x00000000,
};

const u32 csc_rgb_to_sdtv_offsets[MMI_DC_CSC_NUM_OFFSETS] = {
	0x00000000, 0x08000000, 0x08000000,
};

const u32 csc_sdtv_to_rgb_offsets[MMI_DC_CSC_NUM_OFFSETS] = {
	0x00000000, 0x00001800, 0x00001800,
};

/**
 * mmi_dc_set_stream - Set DC output video stream
 * @dc: MMI DC device
 * @mode: requested DRM display mode or NULL to disable output to DP Tx
 */
static void mmi_dc_set_stream(struct mmi_dc *dc,
			      struct drm_display_mode *mode)
{
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_HTOTAL, mode ? mode->htotal : 0);
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_VTOTAL, mode ? mode->vtotal : 0);
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_HSWIDTH,
		    mode ? mode->hsync_end - mode->hsync_start : 0);
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_VSWIDTH,
		    mode ? mode->vsync_end - mode->vsync_start : 0);
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_HRES, mode ? mode->hdisplay : 0);
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_VRES, mode ? mode->vdisplay : 0);
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_HSTART,
		    mode ? mode->htotal - mode->hsync_start : 0);
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_VSTART,
		    mode ? mode->vtotal - mode->vsync_start : 0);
	dc_write_dp(dc, MMI_DC_DP_MAIN_STREAM_MISC0,
		    mode ? MMI_DC_DP_MAIN_STREAM_BPC_12 &
		    MMI_DC_DP_MAIN_STREAM_BPC_MASK : 0);
}

/**
 * mmi_dc_set_global_alpha - Set DC global alpha
 * @dc: MMI DC device
 * @alpha: requested alpha value
 * @enable: enable alpha blending
 */
void mmi_dc_set_global_alpha(struct mmi_dc *dc, u8 alpha, bool enable)
{
	dc_write_blend(dc, MMI_DC_V_BLEND_GLOBAL_ALPHA,
		       MMI_DC_V_BLEND_ALPHA_VALUE(alpha) | enable);
}

/**
 * mmi_dc_blend_set_bg_color - Set blender background color
 * @dc: MMI DC device
 * @rcr: R/Cr component value (12 bit)
 * @gy: G/Y component value (12 bit)
 * @bcb: B/Cb component value (12 bit)
 */
static void mmi_dc_blend_set_bg_color(struct mmi_dc *dc,
				      u32 rcr, u32 gy, u32 bcb)
{
	dc_write_blend(dc, MMI_DC_V_BLEND_BG_CLR(0), rcr);
	dc_write_blend(dc, MMI_DC_V_BLEND_BG_CLR(1), gy);
	dc_write_blend(dc, MMI_DC_V_BLEND_BG_CLR(2), bcb);
}

/**
 * mmi_dc_blend_set_output_format - Set blender output format
 * @dc: MMI DC device
 * @format: requested blender output format
 */
static void mmi_dc_blend_set_output_format(struct mmi_dc *dc,
					   enum mmi_dc_out_format format)
{
	u32 blend_format = format;
	const u16 *coeffs;
	const u32 *offsets;
	unsigned int i;

	if (blend_format == MMI_DC_FORMAT_YCBCR422)
		blend_format |= MMI_DC_V_BLEND_EN_DOWNSAMPLE;

	dc_write_blend(dc, MMI_DC_V_BLEND_OUTPUT_VID_FORMAT, blend_format);
	if (blend_format == MMI_DC_FORMAT_RGB) {
		coeffs = csc_identity_matrix;
		offsets = csc_zero_offsets;
	} else {
		coeffs = csc_rgb_to_sdtv_matrix;
		offsets = csc_rgb_to_sdtv_offsets;
	}

	for (i = 0; i < MMI_DC_CSC_NUM_COEFFS; ++i)
		dc_write_blend(dc, MMI_DC_V_BLEND_RGB2YCBCR_COEFF(i),
			       coeffs[i]);

	for (i = 0; i < MMI_DC_CSC_NUM_OFFSETS; ++i)
		dc_write_blend(dc, MMI_DC_V_BLEND_CC_OUTCSC_OFFSET(i),
			       offsets[i]);
}

/**
 * mmi_dc_blend_enable - Enable DC blender
 * @dc: MMI DC device
 */
static void mmi_dc_blend_enable(struct mmi_dc *dc)
{
	/* Set background color as blue */
	mmi_dc_blend_set_bg_color(dc, MMI_BG_CLR_MIN, MMI_BG_CLR_MIN,
				  MMI_BG_CLR_MAX);
	/* TODO: Support YUV formats */
	mmi_dc_blend_set_output_format(dc, MMI_DC_FORMAT_RGB);
}

/**
 * mmi_dc_blend_disable - Disable DC blender
 * @dc: MMI DC device
 */
static void mmi_dc_blend_disable(struct mmi_dc *dc)
{
	/* TODO: probably make sense to reset blender to default state */
}

/**
 * mmi_dc_reset - Soft reset DC hardware
 * @dc: MMI DC device
 * @reset: assert or deassert
 */
static void mmi_dc_reset(struct mmi_dc *dc, bool reset)
{
	dc_write_avbuf(dc, MMI_DC_AV_BUF_SRST,
		       reset << MMI_DC_AV_BUF_RESET_SHIFT);
}

/**
 * mmi_dc_reset_hw - Reset DC hardware with external reset
 * @dc: MMI DC device
 */
void mmi_dc_reset_hw(struct mmi_dc *dc)
{
	reset_control_assert(dc->rst);
	reset_control_deassert(dc->rst);
	mmi_dc_reset_planes(dc);
}

/**
 * mmi_dc_avbuf_enable - Enable AV buffer manager
 * @dc: MMI DC device
 */
static void mmi_dc_avbuf_enable(struct mmi_dc *dc)
{
	/* TODO: check if any global state need to be initialized */
}

/**
 * mmi_dc_avbuf_disable - Disable AV buffer manager
 * @dc: MMI DC device
 */
static void mmi_dc_avbuf_disable(struct mmi_dc *dc)
{
	/* TODO: reset AV buffer to default state */
}

/**
 * mmi_dc_enable - Enable MMI DC
 * @dc: MMI DC device
 * @mode: the display mode requested
 */
void mmi_dc_enable(struct mmi_dc *dc, struct drm_display_mode *mode)
{
	mmi_dc_blend_enable(dc);
	mmi_dc_avbuf_enable(dc);
	mmi_dc_set_stream(dc, mode);
}

/**
 * mmi_dc_disable - Disable MMI DC
 * @dc: MMI DC device
 */
void mmi_dc_disable(struct mmi_dc *dc)
{
	mmi_dc_avbuf_disable(dc);
	mmi_dc_blend_disable(dc);
	mmi_dc_set_stream(dc, NULL);
	mmi_dc_reset_hw(dc);
}

/**
 * mmi_dc_set_dma_align - Set DC DMA align
 * @dc: MMI DC device
 */
static void mmi_dc_set_dma_align(struct mmi_dc *dc)
{
	dc->dma_align = mmi_dc_planes_get_dma_align(dc);
}

/**
 * mmi_dc_enable_vblank - Enable VBLANK notifications
 * @dc: MMI DC device
 */
void mmi_dc_enable_vblank(struct mmi_dc *dc)
{
	dc_write_irq(dc, MMI_DC_INT_EN, MMI_DC_INT_VBLANK);
}

/**
 * mmi_dc_disable_vblank - Disable VBLANK notifications
 * @dc: MMI DC device
 */
void mmi_dc_disable_vblank(struct mmi_dc *dc)
{
	dc_write_irq(dc, MMI_DC_INT_DS, MMI_DC_INT_VBLANK);
}

/**
 * mmi_dc_irq_handler - MMI DC interrupt handler
 * @irq: IRQ lane number
 * @data: struct mmi_dc pointer bound to this handler
 *
 * Return: IRQ handling result.
 */
static irqreturn_t mmi_dc_irq_handler(int irq, void *data)
{
	struct mmi_dc *dc = data;
	u32 status, mask;

	status = dc_read_irq(dc, MMI_DC_INT_STATUS);
	/* clear status register as soon as we read it */
	dc_write_irq(dc, MMI_DC_INT_STATUS, status & ~MMI_DC_INT_PIXEL_MATCH);
	mask = dc_read_irq(dc, MMI_DC_INT_MASK);

	/*
	 * Status register may report some events, which corresponding
	 * interrupts have been disabled. Filter out those events against
	 * interrupts' mask.
	 */
	status &= ~mask;

	if (!status)
		return IRQ_NONE;

	/* TODO: handle errors */

	if (status & MMI_DC_INT_VBLANK)
		mmi_dc_drm_handle_vblank(dc->drm);

	return IRQ_HANDLED;
}

int mmi_dc_set_vid_clk_src(struct mmi_dc *dc, enum mmi_dc_vid_clk_src vidclksrc)
{
	u32 val = 0;

	if (vidclksrc == MMIDC_AUX0_REF_CLK)
		val = MMI_DC_MISC_VID_CLK_PS;
	else if (vidclksrc == MMIDC_PL_CLK)
		val = MMI_DC_MISC_VID_CLK_PL;

	dc_write_misc(dc, MMI_DC_MISC_VID_CLK, val);

	return 0;
}

enum mmi_dc_vid_clk_src mmi_dc_get_vid_clk_src(struct mmi_dc *dc)
{
	u32 val;
	enum mmi_dc_vid_clk_src ret = MMIDC_AUX0_REF_CLK;

	val = dc_read_misc(dc, MMI_DC_MISC_VID_CLK);

	if (val == MMI_DC_MISC_VID_CLK_PL)
		ret = MMIDC_PL_CLK;
	if (val == MMI_DC_MISC_VID_CLK_PS)
		ret = MMIDC_AUX0_REF_CLK;

	return ret;
}

static struct clk *mmi_dc_init_clk(struct mmi_dc *dc, const char *clk_name)
{
	struct clk *dc_clk = devm_clk_get(dc->dev, clk_name);

	if (IS_ERR(dc_clk)) {
		dev_dbg(dc->dev, "failed to get %s %ld\n",
			clk_name, PTR_ERR(dc_clk));
		dc_clk = NULL;
	}

	return dc_clk;
}

/**
 * mmi_dc_init - Initialize MMI DC hardware
 * @dc: MMI DC device
 * @drm: DRM device
 *
 * Return: 0 on success or error code otherwise.
 */
int mmi_dc_init(struct mmi_dc *dc, struct drm_device *drm)
{
	struct platform_device *pdev = to_platform_device(dc->dev);
	int ret;

	dc->dp = devm_platform_ioremap_resource_byname(pdev, "dp");
	if (IS_ERR(dc->dp))
		return PTR_ERR(dc->dp);

	dc->blend = devm_platform_ioremap_resource_byname(pdev, "blend");
	if (IS_ERR(dc->blend))
		return PTR_ERR(dc->blend);

	dc->avbuf = devm_platform_ioremap_resource_byname(pdev, "avbuf");
	if (IS_ERR(dc->avbuf))
		return PTR_ERR(dc->avbuf);

	dc->misc = devm_platform_ioremap_resource_byname(pdev, "misc");
	if (IS_ERR(dc->misc))
		return PTR_ERR(dc->misc);

	dc->irq = devm_platform_ioremap_resource_byname(pdev, "irq");
	if (IS_ERR(dc->irq))
		return PTR_ERR(dc->irq);

	dc->rst = devm_reset_control_get(dc->dev, NULL);
	if (IS_ERR(dc->rst))
		return dev_err_probe(dc->dev, PTR_ERR(dc->rst),
				     "failed to get reset control\n");

	/* Get all the video clocks */
	dc->pl_pixel_clk = mmi_dc_init_clk(dc, "pl_vid_func_clk");
	dc->ps_pixel_clk = mmi_dc_init_clk(dc, "ps_vid_clk");

	if (!dc->ps_pixel_clk && !dc->pl_pixel_clk) {
		dev_err(dc->dev, "at least one pixel clock is needed!\n");
		return -EINVAL;
	}

	dc->mmi_pll_clk = mmi_dc_init_clk(dc, "mmi_pll");
	dc->stc_ref_clk = mmi_dc_init_clk(dc, "stc_ref_clk");

	mmi_dc_reset_hw(dc);

	dc_write_misc(dc, MMI_DC_MISC_WPROTS, 0);
	dc_write_misc(dc, MMI_DC_VIDEO_FRAME_SWITCH,
		      MMI_DC_VIDEO_FRAME_SWITCH_EN_ALL);

	dc->irq_num = platform_get_irq(pdev, 0);
	if (dc->irq_num < 0)
		return dc->irq_num;

	ret = mmi_dc_create_planes(dc, drm);
	if (ret < 0) {
		mmi_dc_destroy_planes(dc);
		return ret;
	}

	mmi_dc_set_dma_align(dc);

	/* Set video clock source */
	if (dc->pl_pixel_clk)
		dc_write_misc(dc, MMI_DC_MISC_VID_CLK, MMI_DC_MISC_VID_CLK_PL);
	else
		dc_write_misc(dc, MMI_DC_MISC_VID_CLK, MMI_DC_MISC_VID_CLK_PS);

	mmi_dc_reset(dc, true);
	msleep(MMI_DC_MSLEEP_50MS);
	mmi_dc_reset(dc, false);

	/* Set another video clock source */
	dc_write_avbuf(dc, MMI_DC_AV_BUF_AUD_VID_CLK_SOURCE, MMI_DC_AV_BUF_AUD_VID_TIMING_SRC_INT);

	/* Set non live video latency */
	dc_write_avbuf(dc, MMI_DC_AV_BUF_NON_LIVE_LATENCY, MMI_DC_AV_BUF_NON_LIVE_LATENCY_VAL);

	/* Set blender background and alpha */
	mmi_dc_set_global_alpha(dc, 0, true);
	mmi_dc_blend_set_bg_color(dc, MMI_BG_CLR_MIN, MMI_BG_CLR_MIN,
				  MMI_BG_CLR_MAX);
	/*
	 * TODO: Audio driver initialization and audio clock to be handled separately
	 * ensuring that if audio driver fails, video pipeline shouldn't be affected.
	 */

	/* Set the aud_clk and initialize the audio driver */
	dc->aud_clk = devm_clk_get(dc->dev, "pl_aud_clk");
	if (IS_ERR(dc->aud_clk)) {
		dev_warn(dc->dev, "PL audio clock is unavailable\n");
	} else {
		ret = mmi_dc_audio_init(dc);
		if (ret < 0) {
			dev_err(dc->dev, "failed to initialize Audio Driver: %d\n", ret);
			return ret;
		}
	}

	ret = devm_request_threaded_irq(dc->dev, dc->irq_num, NULL,
					mmi_dc_irq_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					dev_name(dc->dev), dc);
	if (ret < 0) {
		dev_err(dc->dev, "failed to setup irq handler: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * mmi_dc_fini - Deinit MMI DC device
 * @dc: MMI DC device
 */
void mmi_dc_fini(struct mmi_dc *dc)
{
	mmi_dc_destroy_planes(dc);
	mmi_dc_audio_uninit(dc);
	mmi_dc_reset(dc, true);
	dc_write_misc(dc, MMI_DC_MISC_WPROTS, 1);
}

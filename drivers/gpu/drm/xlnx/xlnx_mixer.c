// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx logicore video mixer driver
 *
 * Copyright (C) 2017 - 2018 Xilinx, Inc.
 *
 * Author: Saurabh Sengar <saurabhs@xilinx.com>
 *       : Jeffrey Mouroux <jmouroux@xilinx.com>
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/dmaengine.h>
#include <video/videomode.h>
#include "xlnx_bridge.h"
#include "xlnx_crtc.h"
#include "xlnx_drv.h"

/**************************** Register Data **********************************/
#define XVMIX_AP_CTRL			0x00000
#define XVMIX_GIE			0x00004
#define XVMIX_IER			0x00008
#define XVMIX_ISR			0x0000c
#define XVMIX_WIDTH_DATA		0x00010
#define XVMIX_HEIGHT_DATA		0x00018
#define XVMIX_BACKGROUND_Y_R_DATA	0x00028
#define XVMIX_BACKGROUND_U_G_DATA	0x00030
#define XVMIX_BACKGROUND_V_B_DATA	0x00038
#define XVMIX_LAYERENABLE_DATA		0x00040
#define XVMIX_K00_1			0x00048
#define XVMIX_K01_1			0x00050
#define XVMIX_K02_1			0x00058
#define XVMIX_K10_1			0x00060
#define XVMIX_K11_1			0x00068
#define XVMIX_K12_1			0x00070
#define XVMIX_K20_1			0x00078
#define XVMIX_K21_1			0x00080
#define XVMIX_K22_1			0x00088
#define XVMIX_Y_DATA			0x00090
#define XVMIX_U_DATA			0x00098
#define XVMIX_V_DATA			0x000A0
#define XVMIX_LAYERALPHA_0_DATA		0x00100
#define XVMIX_LAYERSTARTX_0_DATA	0x00108
#define XVMIX_LAYERSTARTY_0_DATA	0x00110
#define XVMIX_LAYERWIDTH_0_DATA		0x00118
#define XVMIX_LAYERSTRIDE_0_DATA	0x00120
#define XVMIX_LAYERHEIGHT_0_DATA	0x00128
#define XVMIX_LAYERSCALE_0_DATA		0x00130
#define XVMIX_LAYERVIDEOFORMAT_0_DATA	0x00138
#define XVMIX_K00_2			0x00140
#define XVMIX_K01_2			0x00148
#define XVMIX_K02_2			0x00150
#define XVMIX_K10_2			0x00158
#define XVMIX_K11_2			0x00160
#define XVMIX_K12_2			0x00168
#define XVMIX_K20_2			0x00170
#define XVMIX_K21_2			0x00178
#define XVMIX_K22_2			0x00180
#define XVMIX_R_DATA			0x00188
#define XVMIX_G_DATA			0x00190
#define XVMIX_B_DATA			0x00198
#define XVMIX_LAYER1_BUF1_V_DATA	0x00240
#define XVMIX_LAYER1_BUF2_V_DATA	0x0024c
#define XVMIX_LOGOSTARTX_DATA		0x01000
#define XVMIX_LOGOSTARTY_DATA		0x01008
#define XVMIX_LOGOWIDTH_DATA		0x01010
#define XVMIX_LOGOHEIGHT_DATA		0x01018
#define XVMIX_LOGOSCALEFACTOR_DATA	0x01020
#define XVMIX_LOGOALPHA_DATA		0x01028
#define XVMIX_LOGOCLRKEYMIN_R_DATA	0x01030
#define XVMIX_LOGOCLRKEYMIN_G_DATA	0x01038
#define XVMIX_LOGOCLRKEYMIN_B_DATA	0x01040
#define XVMIX_LOGOCLRKEYMAX_R_DATA	0x01048
#define XVMIX_LOGOCLRKEYMAX_G_DATA	0x01050
#define XVMIX_LOGOCLRKEYMAX_B_DATA	0x01058
#define XVMIX_LOGOR_V_BASE		0x10000
#define XVMIX_LOGOR_V_HIGH		0x10fff
#define XVMIX_LOGOG_V_BASE		0x20000
#define XVMIX_LOGOG_V_HIGH		0x20fff
#define XVMIX_LOGOB_V_BASE		0x30000
#define XVMIX_LOGOB_V_HIGH		0x30fff
#define XVMIX_LOGOA_V_BASE		0x40000
#define XVMIX_LOGOA_V_HIGH		0x40fff

/************************** Constant Definitions *****************************/
#define XVMIX_LOGO_OFFSET		0x1000
#define XVMIX_MASK_DISABLE_ALL_LAYERS   0x0
#define XVMIX_REG_OFFSET                0x100
#define XVMIX_MASTER_LAYER_IDX		0x0
#define XVMIX_LOGO_LAYER_IDX		0x1
#define XVMIX_DISP_MAX_WIDTH		8192
#define XVMIX_DISP_MAX_HEIGHT		4320
#define XVMIX_DISP_MIN_WIDTH		64
#define XVMIX_DISP_MIN_HEIGHT		64
#define XVMIX_MAX_OVERLAY_LAYERS	16
#define XVMIX_MAX_BPC			16
#define XVMIX_ALPHA_MIN			0
#define XVMIX_ALPHA_MAX			256
#define XVMIX_LAYER_WIDTH_MIN		64
#define XVMIX_LAYER_HEIGHT_MIN		64
#define XVMIX_LOGO_LAYER_WIDTH_MIN	32
#define XVMIX_LOGO_LAYER_HEIGHT_MIN	32
#define XVMIX_LOGO_LAYER_WIDTH_MAX	256
#define XVMIX_LOGO_LAYER_HEIGHT_MAX	256
#define XVMIX_IRQ_DONE_MASK		BIT(0)
#define XVMIX_GIE_EN_MASK		BIT(0)
#define XVMIX_AP_EN_MASK		BIT(0)
#define XVMIX_AP_RST_MASK		BIT(7)
#define XVMIX_MAX_NUM_SUB_PLANES	4
#define XVMIX_SCALE_FACTOR_1X		0
#define	XVMIX_SCALE_FACTOR_2X		1
#define	XVMIX_SCALE_FACTOR_4X		2
#define	XVMIX_SCALE_FACTOR_INVALID	3
#define	XVMIX_BASE_ALIGN		8
#define XVMIX_CSC_MAX_ROWS		(3)
#define XVMIX_CSC_MAX_COLS		(3)
#define XVMIX_CSC_MATRIX_SIZE	(XVMIX_CSC_MAX_ROWS * XVMIX_CSC_MAX_COLS)
#define XVMIX_CSC_COEFF_SIZE		(12)
#define XVMIX_CSC_SCALE_FACTOR		(4096)
#define XVMIX_CSC_DIVISOR		(10000)

/*************************** STATIC DATA  ************************************/
static const s16
xlnx_mix_yuv2rgb_coeffs[][DRM_COLOR_ENCODING_MAX][XVMIX_CSC_COEFF_SIZE] = {
	[DRM_COLOR_YCBCR_BT601][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		10000, 0, 13669,
		10000, -3367, -6986,
		10000, 17335, 0,
		-175, 132, -222
	},
	[DRM_COLOR_YCBCR_BT601][DRM_COLOR_YCBCR_FULL_RANGE] = {
		10479, 0, 13979,
		10479, -3443, -7145,
		10479, 17729, 0,
		-179, 136, -227
	},
	[DRM_COLOR_YCBCR_BT709][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		10000, 0, 15406,
		10000, -1832, -4579,
		10000, 18153, 0,
		-197, 82, -232
	},
	[DRM_COLOR_YCBCR_BT709][DRM_COLOR_YCBCR_FULL_RANGE] = {
		10233, 0, 15756,
		10233, -1873, -4683,
		10233, 18566, 0,
		-202, 84, -238
	},
	[DRM_COLOR_YCBCR_BT2020][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		10000, 0, 14426,
		10000, -1609, -5589,
		10000, 18406, 0,
		-185, 92, -236
	},
	[DRM_COLOR_YCBCR_BT2020][DRM_COLOR_YCBCR_FULL_RANGE] = {
		10233, 0, 14754,
		10233, -1646, -5716,
		10233, 18824, 0,
		-189, 94, -241
	}
};

static const s16
xlnx_mix_rgb2yuv_coeffs[][DRM_COLOR_ENCODING_MAX][XVMIX_CSC_COEFF_SIZE] = {
	[DRM_COLOR_YCBCR_BT601][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		2990, 5870, 1440,
		-1720, -3390, 5110,
		5110, -4280, -830,
		0, 128, 128
	},
	[DRM_COLOR_YCBCR_BT601][DRM_COLOR_YCBCR_FULL_RANGE] = {
		2921, 5735, 1113,
		-1686, -3310, 4393,
		4393, -4184, -812,
		0, 128, 128
	},
	[DRM_COLOR_YCBCR_BT709][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		2120, 7150, 720,
		-1170, -3940, 5110,
		5110, -4640, -470,
		0, 128, 128
	},
	[DRM_COLOR_YCBCR_BT709][DRM_COLOR_YCBCR_FULL_RANGE] = {
		2077, 6988, 705,
		-1144, -3582, 4997,
		4997, -4538, -458,
		0, 128, 128
	},
	[DRM_COLOR_YCBCR_BT2020][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		2625, 6775, 592,
		-1427, -3684, 5110,
		5110, -4699, -410,
		0, 128, 128
	},
	[DRM_COLOR_YCBCR_BT2020][DRM_COLOR_YCBCR_FULL_RANGE] = {
		2566, 6625, 579,
		-1396, -3602, 4997,
		4997, -4595, -401,
		0, 128, 128
	}
};

static const u32 color_table[] = {
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_AYUV,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_Y8,
	DRM_FORMAT_Y10,
	DRM_FORMAT_XVUY2101010,
	DRM_FORMAT_VUY888,
	DRM_FORMAT_XVUY8888,
	DRM_FORMAT_XV15,
	DRM_FORMAT_XV20,
};

/*********************** Inline Functions/Macros *****************************/
#define to_mixer_hw(p) (&((p)->mixer->mixer_hw))
#define to_xlnx_crtc(x)	container_of(x, struct xlnx_crtc, crtc)
#define to_xlnx_plane(x)	container_of(x, struct xlnx_mix_plane, base)
#define to_xlnx_mixer(x)	container_of(x, struct xlnx_mix, crtc)

/**
 * enum xlnx_mix_layer_id - Describes the layer by index to be acted upon
 * @XVMIX_LAYER_MASTER: Master layer
 * @XVMIX_LAYER_1: Layer 1
 * @XVMIX_LAYER_2: Layer 2
 * @XVMIX_LAYER_3: Layer 3
 * @XVMIX_LAYER_4: Layer 4
 * @XVMIX_LAYER_5: Layer 5
 * @XVMIX_LAYER_6: Layer 6
 * @XVMIX_LAYER_7: Layer 7
 * @XVMIX_LAYER_8: Layer 8
 * @XVMIX_LAYER_9: Layer 9
 * @XVMIX_LAYER_10: Layer 10
 * @XVMIX_LAYER_11: Layer 11
 * @XVMIX_LAYER_12: Layer 12
 * @XVMIX_LAYER_13: Layer 13
 * @XVMIX_LAYER_14: Layer 14
 * @XVMIX_LAYER_15: Layer 15
 * @XVMIX_LAYER_16: Layer 16
 */
enum xlnx_mix_layer_id {
	XVMIX_LAYER_MASTER = 0,
	XVMIX_LAYER_1,
	XVMIX_LAYER_2,
	XVMIX_LAYER_3,
	XVMIX_LAYER_4,
	XVMIX_LAYER_5,
	XVMIX_LAYER_6,
	XVMIX_LAYER_7,
	XVMIX_LAYER_8,
	XVMIX_LAYER_9,
	XVMIX_LAYER_10,
	XVMIX_LAYER_11,
	XVMIX_LAYER_12,
	XVMIX_LAYER_13,
	XVMIX_LAYER_14,
	XVMIX_LAYER_15,
	XVMIX_LAYER_16
};

/**
 * struct xlnx_mix_layer_data - Describes the hardware configuration of a given
 * mixer layer
 * @hw_config: struct specifying the IP hardware constraints for this layer
 * @vid_fmt: DRM format for this layer
 * @can_alpha: Indicates that layer alpha is enabled for this layer
 * @can_scale: Indicates that layer scaling is enabled for this layer
 * @is_streaming: Indicates layer is not using mixer DMA but streaming from
 *  external DMA
 * @max_width: Max possible pixel width
 * @max_height: Max possible pixel height
 * @min_width: Min possible pixel width
 * @min_height: Min possible pixel height
 * @layer_regs: struct containing current cached register values
 * @buff_addr: Current physical address of image buffer
 * @x_pos: Current CRTC x offset
 * @y_pos: Current CRTC y offset
 * @width: Current width in pixels
 * @height: Current hight in pixels
 * @stride: Current stride (when Mixer is performing DMA)
 * @alpha: Current alpha setting
 * @is_active: Logical flag indicating layer in use.  If false, calls to
 *  enable layer will be ignored.
 * @scale_fact: Current scaling factor applied to layer
 * @id: The logical layer id identifies which layer this struct describes
 *  (e.g. 0 = master, 1-15 = overlay).
 *
 * All mixer layers are reprsented by an instance of this struct:
 * output streaming, overlay, logo.
 * Current layer-specific register state is stored in the layer_regs struct.
 * The hardware configuration is stored in struct hw_config.
 *
 * Note:
 * Some properties of the logo layer are unique and not described in this
 * struct.  Those properites are part of the xlnx_mix struct as global
 * properties.
 */
struct xlnx_mix_layer_data {
	struct {
		u32     vid_fmt;
		bool    can_alpha;
		bool    can_scale;
		bool    is_streaming;
		u32     max_width;
		u32     max_height;
		u32     min_width;
		u32     min_height;
	} hw_config;

	struct {
		u64     buff_addr1;
		u64     buff_addr2;
		u32     x_pos;
		u32     y_pos;
		u32     width;
		u32     height;
		u32     stride;
		u32     alpha;
		bool	is_active;
		u32	scale_fact;
	} layer_regs;

	enum xlnx_mix_layer_id id;
};

/**
 * struct xlnx_mix_hw - Describes a mixer IP block instance within the design
 * @base: Base physical address of Mixer IP in memory map
 * @logo_layer_en: Indicates logo layer is enabled in hardware
 * @logo_pixel_alpha_enabled: Indicates that per-pixel alpha supported for logo
 *  layer
 * @csc_enabled: Indicates that colorimetry coefficients are programmable
 * @max_layer_width: Max possible width for any layer on this Mixer
 * @max_layer_height: Max possible height for any layer on this Mixer
 * @max_logo_layer_width: Min possible width for any layer on this Mixer
 * @max_logo_layer_height: Min possible height for any layer on this Mixer
 * @num_layers: Max number of layers (excl: logo)
 * @bg_layer_bpc: Bits per component for the background streaming layer
 * @dma_addr_size: dma address size in bits
 * @ppc: Pixels per component
 * @irq: Interrupt request number assigned
 * @bg_color: Current RGB color value for internal background color generator
 * @layer_data: Array of layer data
 * @layer_cnt: Layer data array count
 * @max_layers: Maximum number of layers supported by hardware
 * @logo_layer_id: Index of logo layer
 * @logo_en_mask: Mask used to enable logo layer
 * @enable_all_mask: Mask used to enable all layers
 * @reset_gpio: GPIO line used to reset IP between modesetting operations
 * @intrpt_handler_fn: Interrupt handler function called when frame is completed
 * @intrpt_data: Data pointer passed to interrupt handler
 *
 * Used as the primary data structure for many L2 driver functions. Logo layer
 * data, if enabled within the IP, is described in this structure.  All other
 * layers are described by an instance of xlnx_mix_layer_data referenced by this
 * struct.
 *
 */
struct xlnx_mix_hw {
	void __iomem        *base;
	bool                logo_layer_en;
	bool                logo_pixel_alpha_enabled;
	u32		    csc_enabled;
	u32                 max_layer_width;
	u32                 max_layer_height;
	u32                 max_logo_layer_width;
	u32                 max_logo_layer_height;
	u32                 num_layers;
	u32                 bg_layer_bpc;
	u32		    dma_addr_size;
	u32                 ppc;
	int		    irq;
	u64		    bg_color;
	struct xlnx_mix_layer_data *layer_data;
	u32 layer_cnt;
	u32 max_layers;
	u32 logo_layer_id;
	u32 logo_en_mask;
	u32 enable_all_mask;
	struct gpio_desc *reset_gpio;
	void (*intrpt_handler_fn)(void *);
	void *intrpt_data;
};

/**
 * struct xlnx_mix - Container for interfacing DRM driver to mixer
 * @mixer_hw: Object representing actual hardware state of mixer
 * @master: Logical master device from xlnx drm
 * @crtc: Xilinx DRM driver crtc object
 * @drm_primary_layer: Hardware layer serving as logical DRM primary layer
 * @hw_master_layer: Base video streaming layer
 * @hw_logo_layer: Hardware logo layer
 * @planes: Mixer overlay layers
 * @num_planes : number of planes
 * @max_width : maximum width of plane
 * @max_height : maximum height of plane
 * @max_cursor_width : maximum cursor width
 * @max_cursor_height: maximum cursor height
 * @alpha_prop: Global layer alpha property
 * @scale_prop: Layer scale property (1x, 2x or 4x)
 * @bg_color: Background color property for primary layer
 * @drm: core drm object
 * @pixel_clock: pixel clock for mixer
 * @pixel_clock_enabled: pixel clock status
 * @dpms: mixer drm state
 * @event: vblank pending event
 * @vtc_bridge: vtc_bridge structure
 *
 * Contains pointers to logical constructions such as the DRM plane manager as
 * well as pointers to distinquish the mixer layer serving as the DRM "primary"
 * plane from the actual mixer layer which serves as the background layer in
 * hardware.
 *
 */
struct xlnx_mix {
	struct xlnx_mix_hw mixer_hw;
	struct platform_device *master;
	struct xlnx_crtc crtc;
	struct xlnx_mix_plane *drm_primary_layer;
	struct xlnx_mix_plane *hw_master_layer;
	struct xlnx_mix_plane *hw_logo_layer;
	struct xlnx_mix_plane *planes;
	u32 num_planes;
	u32 max_width;
	u32 max_height;
	u32 max_cursor_width;
	u32 max_cursor_height;
	struct drm_property *alpha_prop;
	struct drm_property *scale_prop;
	struct drm_property *bg_color;
	struct drm_device *drm;
	struct clk *pixel_clock;
	bool pixel_clock_enabled;
	int dpms;
	struct drm_pending_vblank_event *event;
	struct xlnx_bridge *vtc_bridge;
};

/**
 * struct xlnx_mix_plane_dma - Xilinx drm plane VDMA object
 *
 * @chan: dma channel
 * @xt: dma interleaved configuration template
 * @sgl: data chunk for dma_interleaved_template
 * @is_active: flag if the DMA is active
 */
struct xlnx_mix_plane_dma {
	struct dma_chan *chan;
	struct dma_interleaved_template xt;
	struct data_chunk sgl[1];
	bool is_active;
};

/**
 * struct xlnx_mix_plane - Xilinx drm plane object
 *
 * @base: base drm plane object
 * @mixer_layer: video mixer hardware layer data instance
 * @mixer: mixer DRM object
 * @dma: dma object
 * @id: plane id
 * @dpms: current dpms level
 * @format: pixel format
 */
struct xlnx_mix_plane {
	struct drm_plane base;
	struct xlnx_mix_layer_data *mixer_layer;
	struct xlnx_mix *mixer;
	struct xlnx_mix_plane_dma dma[XVMIX_MAX_NUM_SUB_PLANES];
	int id;
	int dpms;
	u32 format;
};

static inline void reg_writel(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

static inline void reg_writeq(void __iomem *base, int offset, u64 val)
{
	writel(lower_32_bits(val), base + offset);
	writel(upper_32_bits(val), base + offset + 4);
}

static inline u32 reg_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

/**
 * xlnx_mix_intrpt_enable_done - Enables interrupts
 * @mixer: instance of mixer IP core
 *
 * Enables interrupts in the mixer core
 */
static void xlnx_mix_intrpt_enable_done(struct xlnx_mix_hw *mixer)
{
	u32 curr_val = reg_readl(mixer->base, XVMIX_IER);

	/* Enable Interrupts */
	reg_writel(mixer->base, XVMIX_IER, curr_val | XVMIX_IRQ_DONE_MASK);
	reg_writel(mixer->base, XVMIX_GIE, XVMIX_GIE_EN_MASK);
}

/**
 * xlnx_mix_intrpt_disable - Disable interrupts
 * @mixer: instance of mixer IP core
 *
 * Disables interrupts in the mixer core
 */
static void xlnx_mix_intrpt_disable(struct xlnx_mix_hw *mixer)
{
	u32 curr_val =  reg_readl(mixer->base, XVMIX_IER);

	reg_writel(mixer->base, XVMIX_IER, curr_val & (~XVMIX_IRQ_DONE_MASK));
	reg_writel(mixer->base, XVMIX_GIE, 0);
}

/**
 * xlnx_mix_start - Start the mixer core video generator
 * @mixer: Mixer core instance for which to start video output
 *
 * Starts the core to generate a video frame.
 */
static void xlnx_mix_start(struct xlnx_mix_hw *mixer)
{
	u32 val;

	val = XVMIX_AP_RST_MASK | XVMIX_AP_EN_MASK;
	reg_writel(mixer->base, XVMIX_AP_CTRL, val);
}

/**
 * xlnx_mix_stop - Stop the mixer core video generator
 * @mixer: Mixer core instance for which to stop video output
 *
 * Starts the core to generate a video frame.
 */
static void xlnx_mix_stop(struct xlnx_mix_hw *mixer)
{
	reg_writel(mixer->base, XVMIX_AP_CTRL, 0);
}

static inline uint32_t xlnx_mix_get_intr_status(struct xlnx_mix_hw *mixer)
{
	return reg_readl(mixer->base, XVMIX_ISR) & XVMIX_IRQ_DONE_MASK;
}

static inline void xlnx_mix_clear_intr_status(struct xlnx_mix_hw *mixer,
					      uint32_t intr)
{
	reg_writel(mixer->base, XVMIX_ISR, intr);
}

/**
 * xlnx_mix_set_yuv2_rgb_coeff - Programs yuv to rgb coeffiecients
 * @plane: Xilinx drm plane object
 * @enc: Colorimetry encoding scheme
 * @range: Colorimetry range
 * Programs the colorimetry coefficients required for yuv to rgb
 * conversion.
 */
static void xlnx_mix_set_yuv2_rgb_coeff(struct xlnx_mix_plane *plane,
					enum drm_color_encoding enc,
					enum drm_color_range range)
{
	struct xlnx_mix *mixer = plane->mixer;
	u32 i;
	u32 bpc_scale = 1 << (mixer->mixer_hw.bg_layer_bpc - 8);

	for (i = 0; i < XVMIX_CSC_MATRIX_SIZE; i++)
		reg_writel(mixer->mixer_hw.base, XVMIX_K00_1 + i * 8,
			   xlnx_mix_yuv2rgb_coeffs[enc][range][i] *
			   XVMIX_CSC_SCALE_FACTOR / XVMIX_CSC_DIVISOR);

	for (i = XVMIX_CSC_MATRIX_SIZE; i < XVMIX_CSC_COEFF_SIZE; i++)
		reg_writel(mixer->mixer_hw.base, XVMIX_K00_1 + i * 8,
			   (xlnx_mix_yuv2rgb_coeffs[enc][range][i] *
			    bpc_scale));
}

/**
 * xlnx_mix_set_rgb2_yuv_coeff - Programs rgb to yuv coeffiecients
 * @plane: Xilinx drm plane object
 * @enc: Colorimetry encoding scheme
 * @range: Colorimetry range
 * Programs the colorimetry coefficients required for rgb to yuv
 * conversion.
 */
static void xlnx_mix_set_rgb2_yuv_coeff(struct xlnx_mix_plane *plane,
					enum drm_color_encoding enc,
					enum drm_color_range range)
{
	struct xlnx_mix *mixer = plane->mixer;
	u32 i;
	u32 bpc_scale = 1 << (mixer->mixer_hw.bg_layer_bpc - 8);

	for (i = 0; i < XVMIX_CSC_MATRIX_SIZE; i++)
		reg_writel(mixer->mixer_hw.base, XVMIX_K00_2 + i * 8,
			   xlnx_mix_rgb2yuv_coeffs[enc][range][i] *
			   XVMIX_CSC_SCALE_FACTOR / XVMIX_CSC_DIVISOR);

	for (i = XVMIX_CSC_MATRIX_SIZE; i < XVMIX_CSC_COEFF_SIZE; i++)
		reg_writel(mixer->mixer_hw.base, XVMIX_K00_2 + i * 8,
			   (xlnx_mix_rgb2yuv_coeffs[enc][range][i] *
			    bpc_scale));
}

/**
 * xlnx_mix_get_layer_data - Retrieve current hardware and register
 * values for a logical video layer
 * @mixer: Mixer instance to interrogate
 * @id: Id of layer for which data is requested
 *
 * Return:
 * Structure containing layer-specific data; NULL upon failure
 */
static struct xlnx_mix_layer_data *
xlnx_mix_get_layer_data(struct xlnx_mix_hw *mixer, enum xlnx_mix_layer_id id)
{
	u32 i;
	struct xlnx_mix_layer_data *layer_data;

	for (i = 0; i <= (mixer->layer_cnt - 1); i++) {
		layer_data = &mixer->layer_data[i];
		if (layer_data->id == id)
			return layer_data;
	}
	return NULL;
}

/**
 * xlnx_mix_set_active_area - Sets the number of active horizontal and
 * vertical scan lines for the mixer background layer.
 * @mixer: Mixer instance for which to set a new viewable area
 * @hactive: Width of new background image dimension
 * @vactive: Height of new background image dimension
 *
 * Minimum values are 64x64 with maximum values determined by the IP hardware
 * design.
 *
 * Return:
 * Zero on success, -EINVAL on failure
 */
static int xlnx_mix_set_active_area(struct xlnx_mix_hw *mixer,
				    u32 hactive, u32 vactive)
{
	struct xlnx_mix_layer_data *ld =
		xlnx_mix_get_layer_data(mixer, XVMIX_LAYER_MASTER);

	if (hactive > ld->hw_config.max_width ||
	    vactive > ld->hw_config.max_height) {
		DRM_ERROR("Invalid layer dimention\n");
		return -EINVAL;
	}
	/* set resolution */
	reg_writel(mixer->base, XVMIX_HEIGHT_DATA, vactive);
	reg_writel(mixer->base, XVMIX_WIDTH_DATA, hactive);
	ld->layer_regs.width  = hactive;
	ld->layer_regs.height = vactive;

	return 0;
}

/**
 * is_window_valid - Validate requested plane dimensions
 * @mixer: Mixer core instance for which to stop video output
 * @x_pos: x position requested for start of plane
 * @y_pos: y position requested for start of plane
 * @width: width of plane
 * @height: height of plane
 * @scale: scale factor of plane
 *
 * Validates if the requested window is within the frame boundary
 *
 * Return:
 * true on success, false on failure
 */
static bool is_window_valid(struct xlnx_mix_hw *mixer, u32 x_pos, u32 y_pos,
			    u32 width, u32 height, u32 scale)
{
	struct xlnx_mix_layer_data *master_layer;
	int scale_factor[3] = {1, 2, 4};

	master_layer = xlnx_mix_get_layer_data(mixer, XVMIX_LAYER_MASTER);

	/* Check if window scale factor is set */
	if (scale < XVMIX_SCALE_FACTOR_INVALID) {
		width  *= scale_factor[scale];
		height *= scale_factor[scale];
	}

	/* verify overlay falls within currently active background area */
	if (((x_pos + width)  <= master_layer->layer_regs.width) &&
	    ((y_pos + height) <= master_layer->layer_regs.height))
		return true;

	DRM_ERROR("Requested plane dimensions can't be set\n");
	return false;
}

/**
 *  xlnx_mix_layer_enable - Enables the requested layers
 * @mixer: Mixer instance in which to enable a video layer
 * @id: Logical id (e.g. 16 = logo layer) to enable
 *
 * Enables (permit video output) for layers in mixer
 * Enables the layer denoted by id in the IP core.
 * Layer 0 will indicate the background layer and layer 8 the logo
 * layer. Passing max layers value will enable all
 */
static void xlnx_mix_layer_enable(struct xlnx_mix_hw *mixer,
				  enum xlnx_mix_layer_id id)
{
	struct xlnx_mix_layer_data *layer_data;
	u32 curr_state;

	/* Ensure layer is marked as 'active' by application before
	 * turning on in hardware.  In some cases, layer register data
	 * may be written to otherwise inactive layers in lieu of, eventually,
	 * turning them on.
	 */
	layer_data = xlnx_mix_get_layer_data(mixer, id);
	if (!layer_data) {
		DRM_ERROR("Invalid layer id %d\n", id);
		return;
	}
	if (!layer_data->layer_regs.is_active)
		return; /* for inactive layers silently return */

	/* Check if request is to enable all layers or single layer */
	if (id == mixer->max_layers) {
		reg_writel(mixer->base, XVMIX_LAYERENABLE_DATA,
			   mixer->enable_all_mask);

	} else if ((id < mixer->layer_cnt) || ((id == mixer->logo_layer_id) &&
		   mixer->logo_layer_en)) {
		curr_state = reg_readl(mixer->base, XVMIX_LAYERENABLE_DATA);
		if (id == mixer->logo_layer_id)
			curr_state |= mixer->logo_en_mask;
		else
			curr_state |= BIT(id);
		reg_writel(mixer->base, XVMIX_LAYERENABLE_DATA, curr_state);
	} else {
		DRM_ERROR("Can't enable requested layer %d\n", id);
	}
}

/**
 * xlnx_mix_disp_layer_enable - Enables video output represented by the
 * plane object
 * @plane: Drm plane object describing video layer to enable
 *
 */
static void xlnx_mix_disp_layer_enable(struct xlnx_mix_plane *plane)
{
	struct xlnx_mix_hw *mixer_hw;
	struct xlnx_mix_layer_data *l_data;
	u32 id;

	if (!plane)
		return;
	mixer_hw = to_mixer_hw(plane);
	l_data = plane->mixer_layer;
	id = l_data->id;
	if (id > mixer_hw->logo_layer_id) {
		DRM_DEBUG_KMS("Attempt to activate invalid layer: %d\n", id);
		return;
	}
	if (id == XVMIX_LAYER_MASTER && !l_data->hw_config.is_streaming)
		return;

	xlnx_mix_layer_enable(mixer_hw, id);
}

/**
 * xlnx_mix_layer_disable - Disables the requested layer
 * @mixer:  Mixer for which the layer will be disabled
 * @id: Logical id of the layer to be disabled (0-16)
 *
 * Disables the layer denoted by layer_id in the IP core.
 * Layer 0 will indicate the background layer and layer 16 the logo
 * layer. Passing the value of max layers will disable all
 * layers.
 */
static void xlnx_mix_layer_disable(struct xlnx_mix_hw *mixer,
				   enum xlnx_mix_layer_id id)
{
	u32 num_layers, curr_state;

	num_layers = mixer->layer_cnt;

	if (id == mixer->max_layers) {
		reg_writel(mixer->base, XVMIX_LAYERENABLE_DATA,
			   XVMIX_MASK_DISABLE_ALL_LAYERS);
	} else if ((id < num_layers) ||
		   ((id == mixer->logo_layer_id) && (mixer->logo_layer_en))) {
		curr_state = reg_readl(mixer->base, XVMIX_LAYERENABLE_DATA);
		if (id == mixer->logo_layer_id)
			curr_state &= ~(mixer->logo_en_mask);
		else
			curr_state &= ~(BIT(id));
		reg_writel(mixer->base, XVMIX_LAYERENABLE_DATA, curr_state);
	} else {
		DRM_ERROR("Can't disable requested layer %d\n", id);
	}
}

/**
 * xlnx_mix_disp_layer_disable - Disables video output represented by the
 * plane object
 * @plane: Drm plane object describing video layer to disable
 *
 */
static void xlnx_mix_disp_layer_disable(struct xlnx_mix_plane *plane)
{
	struct xlnx_mix_hw *mixer_hw;
	u32 layer_id;

	if (plane)
		mixer_hw = to_mixer_hw(plane);
	else
		return;
	layer_id = plane->mixer_layer->id;
	if (layer_id > mixer_hw->logo_layer_id)
		return;

	xlnx_mix_layer_disable(mixer_hw, layer_id);
}

static int xlnx_mix_mark_layer_inactive(struct xlnx_mix_plane *plane)
{
	if (!plane || !plane->mixer_layer)
		return -ENODEV;

	plane->mixer_layer->layer_regs.is_active = false;

	return 0;
}

/* apply mode to plane pipe */
static void xlnx_mix_plane_commit(struct drm_plane *base_plane)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);
	struct dma_async_tx_descriptor *desc;
	enum dma_ctrl_flags flags;
	unsigned int i;

	/* for xlnx video framebuffer dma, if used */
	xilinx_xdma_drm_config(plane->dma[0].chan, plane->format);
	for (i = 0; i < XVMIX_MAX_NUM_SUB_PLANES; i++) {
		struct xlnx_mix_plane_dma *dma = &plane->dma[i];

		if (dma->chan && dma->is_active) {
			flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
			desc = dmaengine_prep_interleaved_dma(dma->chan,
							      &dma->xt,
							      flags);
			if (!desc) {
				DRM_ERROR("failed to prepare DMA descriptor\n");
				return;
			}
			dmaengine_submit(desc);
			dma_async_issue_pending(dma->chan);
		}
	}
}

static int xlnx_mix_plane_get_max_width(struct drm_plane *base_plane)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);

	return plane->mixer->max_width;
}

static int xlnx_mix_plane_get_max_height(struct drm_plane *base_plane)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);

	return plane->mixer->max_height;
}

static int xlnx_mix_plane_get_max_cursor_width(struct drm_plane *base_plane)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);

	return plane->mixer->max_cursor_width;
}

static int xlnx_mix_plane_get_max_cursor_height(struct drm_plane *base_plane)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);

	return plane->mixer->max_cursor_height;
}

static int xlnx_mix_crtc_get_max_width(struct xlnx_crtc *crtc)
{
	return xlnx_mix_plane_get_max_width(crtc->crtc.primary);
}

static int xlnx_mix_crtc_get_max_height(struct xlnx_crtc *crtc)
{
	return xlnx_mix_plane_get_max_height(crtc->crtc.primary);
}

static unsigned int xlnx_mix_crtc_get_max_cursor_width(struct xlnx_crtc *crtc)
{
	return xlnx_mix_plane_get_max_cursor_width(crtc->crtc.primary);
}

static unsigned int xlnx_mix_crtc_get_max_cursor_height(struct xlnx_crtc *crtc)
{
	return xlnx_mix_plane_get_max_cursor_height(crtc->crtc.primary);
}

/**
 * xlnx_mix_crtc_get_format - Get the current device format
 * @crtc: xlnx crtc object
 *
 * Get the current format of pipeline
 *
 * Return: the corresponding DRM_FORMAT_XXX
 */
static uint32_t xlnx_mix_crtc_get_format(struct xlnx_crtc *crtc)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(crtc->crtc.primary);

	return plane->format;
}

/**
 * xlnx_mix_crtc_get_align - Get the alignment value for pitch
 * @crtc: xlnx crtc object
 *
 * Get the alignment value for pitch from the plane
 *
 * Return: The alignment value if successful, or the error code.
 */
static unsigned int xlnx_mix_crtc_get_align(struct xlnx_crtc *crtc)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(crtc->crtc.primary);
	struct xlnx_mix *m = plane->mixer;

	return XVMIX_BASE_ALIGN * m->mixer_hw.ppc;
}

/**
 * xlnx_mix_attach_plane_prop - Attach mixer-specific drm property to
 * the given plane
 * @plane: Xilinx drm plane object to inspect and attach appropriate
 *  properties to
 *
 * The linked mixer layer will be inspected to see what capabilities it offers
 * (e.g. global layer alpha; scaling) and drm property objects that indicate
 * those capabilities will then be attached and initialized to default values.
 */
static void xlnx_mix_attach_plane_prop(struct xlnx_mix_plane *plane)
{
	struct drm_mode_object *base = &plane->base.base;
	struct xlnx_mix *mixer = plane->mixer;

	if (plane->mixer_layer->hw_config.can_scale)
		drm_object_attach_property(base, mixer->scale_prop,
					   XVMIX_SCALE_FACTOR_1X);
	if (plane->mixer_layer->hw_config.can_alpha)
		drm_object_attach_property(base, mixer->alpha_prop,
					   XVMIX_ALPHA_MAX);
	if (mixer->mixer_hw.csc_enabled) {
		u32 supported_encodings = BIT(DRM_COLOR_YCBCR_BT601) |
					  BIT(DRM_COLOR_YCBCR_BT709) |
					  BIT(DRM_COLOR_YCBCR_BT2020);
		u32 supported_ranges = BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
				       BIT(DRM_COLOR_YCBCR_FULL_RANGE);
		enum drm_color_encoding encoding = DRM_COLOR_YCBCR_BT709;
		enum drm_color_range range = DRM_COLOR_YCBCR_LIMITED_RANGE;

		drm_plane_create_color_properties(&plane->base,
						  supported_encodings,
						  supported_ranges,
						  encoding, range);
	}
}

static int xlnx_mix_mark_layer_active(struct xlnx_mix_plane *plane)
{
	if (!plane->mixer_layer)
		return -ENODEV;
	plane->mixer_layer->layer_regs.is_active = true;

	return 0;
}

static bool xlnx_mix_isfmt_support(u32 format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(color_table); i++) {
		if (format == color_table[i])
			return true;
	}
	return false;
}

/*************** DISPLAY ************/

/**
 * xlnx_mix_get_layer_scaling - Get layer scaling factor
 * @mixer: Mixer instance to program with new background color
 * @id: Plane id
 *
 * Applicable only for overlay layers
 *
 * Return:
 * scaling factor of the specified layer
 */
static int xlnx_mix_get_layer_scaling(struct xlnx_mix_hw *mixer,
				      enum xlnx_mix_layer_id id)
{
	int scale_factor = 0;
	u32 reg;
	struct xlnx_mix_layer_data *l_data = xlnx_mix_get_layer_data(mixer, id);

	if (id == mixer->logo_layer_id) {
		if (mixer->logo_layer_en) {
			if (mixer->max_layers > XVMIX_MAX_OVERLAY_LAYERS)
				reg = XVMIX_LOGOSCALEFACTOR_DATA +
					XVMIX_LOGO_OFFSET;
			else
				reg = XVMIX_LOGOSCALEFACTOR_DATA;
			scale_factor = reg_readl(mixer->base, reg);
			l_data->layer_regs.scale_fact = scale_factor;
		}
	} else {
		/*Layer0-Layer15*/
		if (id < mixer->logo_layer_id && l_data->hw_config.can_scale) {
			reg = XVMIX_LAYERSCALE_0_DATA + (id * XVMIX_REG_OFFSET);
			scale_factor = reg_readl(mixer->base, reg);
			l_data->layer_regs.scale_fact = scale_factor;
		}
	}
	return scale_factor;
}

/**
 * xlnx_mix_set_layer_window - Sets the position of an overlay layer
 * @mixer: Specific mixer object instance controlling the video
 * @id: Logical layer id (1-15) to be positioned
 * @x_pos: new: Column to start display of overlay layer
 * @y_pos: new: Row to start display of overlay layer
 * @width: Number of active columns to dislay for overlay layer
 * @height: Number of active columns to display for overlay layer
 * @stride: Width in bytes of overaly memory buffer (memory layer only)
 *
 * Sets the position of an overlay layer over the background layer (layer 0)
 * Applicable only for layers 1-15 or the logo layer
 *
 * Return:
 * Zero on success, -EINVAL if position is invalid or -ENODEV if layer
 */
static int xlnx_mix_set_layer_window(struct xlnx_mix_hw *mixer,
				     enum xlnx_mix_layer_id id, u32 x_pos,
				     u32 y_pos, u32 width, u32 height,
				     u32 stride)
{
	struct xlnx_mix_layer_data *l_data;
	u32 scale = 0;
	int status = -EINVAL;
	u32 x_reg, y_reg, w_reg, h_reg, s_reg;
	u32 off;

	l_data = xlnx_mix_get_layer_data(mixer, id);
	if (!l_data)
		return status;

	scale = xlnx_mix_get_layer_scaling(mixer, id);
	if (!is_window_valid(mixer, x_pos, y_pos, width, height, scale))
		return status;

	if (id == mixer->logo_layer_id) {
		if (!(mixer->logo_layer_en &&
		      width <= l_data->hw_config.max_width &&
		      height <= l_data->hw_config.max_height &&
		      height >= l_data->hw_config.min_height &&
		      width >= l_data->hw_config.min_width))
			return status;

		if (mixer->max_layers > XVMIX_MAX_OVERLAY_LAYERS) {
			x_reg = XVMIX_LOGOSTARTX_DATA + XVMIX_LOGO_OFFSET;
			y_reg = XVMIX_LOGOSTARTY_DATA + XVMIX_LOGO_OFFSET;
			w_reg = XVMIX_LOGOWIDTH_DATA + XVMIX_LOGO_OFFSET;
			h_reg = XVMIX_LOGOHEIGHT_DATA + XVMIX_LOGO_OFFSET;
		} else {
			x_reg = XVMIX_LOGOSTARTX_DATA;
			y_reg = XVMIX_LOGOSTARTY_DATA;
			w_reg = XVMIX_LOGOWIDTH_DATA;
			h_reg = XVMIX_LOGOHEIGHT_DATA;
		}
		reg_writel(mixer->base, x_reg, x_pos);
		reg_writel(mixer->base, y_reg, y_pos);
		reg_writel(mixer->base, w_reg, width);
		reg_writel(mixer->base, h_reg, height);
		l_data->layer_regs.x_pos = x_pos;
		l_data->layer_regs.y_pos = y_pos;
		l_data->layer_regs.width = width;
		l_data->layer_regs.height = height;
		status = 0;
	} else {
		 /*Layer1-Layer15*/

		if (!(id < mixer->layer_cnt &&
		      width <= l_data->hw_config.max_width &&
		      width >= l_data->hw_config.min_width))
			return status;
		x_reg = XVMIX_LAYERSTARTX_0_DATA;
		y_reg = XVMIX_LAYERSTARTY_0_DATA;
		w_reg = XVMIX_LAYERWIDTH_0_DATA;
		h_reg = XVMIX_LAYERHEIGHT_0_DATA;
		s_reg = XVMIX_LAYERSTRIDE_0_DATA;

		off = id * XVMIX_REG_OFFSET;
		reg_writel(mixer->base, (x_reg + off), x_pos);
		reg_writel(mixer->base, (y_reg + off), y_pos);
		reg_writel(mixer->base, (w_reg + off), width);
		reg_writel(mixer->base, (h_reg + off), height);
		l_data->layer_regs.x_pos = x_pos;
		l_data->layer_regs.y_pos = y_pos;
		l_data->layer_regs.width = width;
		l_data->layer_regs.height = height;

		if (!l_data->hw_config.is_streaming)
			reg_writel(mixer->base, (s_reg + off), stride);
		status = 0;
	}
	return status;
}

/**
 * xlnx_mix_set_layer_dimensions - Set layer dimensions
 * @plane: Drm plane object desribing video layer to reposition
 * @crtc_x: New horizontal anchor postion from which to begin rendering
 * @crtc_y: New vertical anchor position from which to begin rendering
 * @width: Width, in pixels, to render from stream or memory buffer
 * @height: Height, in pixels, to render from stream or memory buffer
 * @stride: Width, in bytes, of a memory buffer.  Used only for
 *  memory layers.  Use 0 for streaming layers.
 *
 * Establishes new coordinates and dimensions for a video plane layer
 * New size and coordinates of window must fit within the currently active
 * area of the crtc (e.g. the background resolution)
 *
 * Return: 0 if successful; Either -EINVAL if coordindate data is invalid
 * or -ENODEV if layer data not present
 */
static int xlnx_mix_set_layer_dimensions(struct xlnx_mix_plane *plane,
					 u32 crtc_x, u32 crtc_y,
					  u32 width, u32 height, u32 stride)
{
	struct xlnx_mix *mixer = plane->mixer;
	struct xlnx_mix_hw *mixer_hw = to_mixer_hw(plane);
	struct xlnx_mix_layer_data *layer_data;
	enum xlnx_mix_layer_id layer_id;
	int ret = 0;

	layer_data = plane->mixer_layer;
	layer_id = layer_data->id;
	if (layer_data->layer_regs.height != height ||
	    layer_data->layer_regs.width != width) {
		if (mixer->drm_primary_layer == plane)
			xlnx_mix_layer_disable(mixer_hw, XVMIX_LAYER_MASTER);

		xlnx_mix_layer_disable(mixer_hw, layer_id);
	}
	if (mixer->drm_primary_layer == plane) {
		crtc_x = 0;
		crtc_y = 0;
		ret = xlnx_mix_set_active_area(mixer_hw, width, height);
		if (ret)
			return ret;
		xlnx_mix_layer_enable(mixer_hw, XVMIX_LAYER_MASTER);
	}
	if (layer_id != XVMIX_LAYER_MASTER && layer_id < mixer_hw->max_layers) {
		ret = xlnx_mix_set_layer_window(mixer_hw, layer_id, crtc_x,
						crtc_y, width, height, stride);
		if (ret)
			return ret;
		xlnx_mix_disp_layer_enable(plane);
	}
	return ret;
}

/**
 * xlnx_mix_set_layer_scaling - Sets scaling factor
 * @mixer: Instance of mixer to be subject of scaling request
 * @id: Logical id of video layer subject to new scale setting
 * @scale: scale Factor (1x, 2x or 4x) for horiz. and vert. dimensions
 *
 * Sets the scaling factor for the specified video layer
 * Not applicable to background stream layer (layer 0)
 *
 * Return:
 * Zero on success, -EINVAL on failure to set scale for layer (likely
 * returned if resulting size of layer exceeds dimensions of active
 * display area
 */
static int xlnx_mix_set_layer_scaling(struct xlnx_mix_hw *mixer,
				      enum xlnx_mix_layer_id id, u32 scale)
{
	void __iomem *reg = mixer->base;
	struct xlnx_mix_layer_data *l_data;
	int status = 0;
	u32 x_pos, y_pos, width, height, offset;

	l_data = xlnx_mix_get_layer_data(mixer, id);
	x_pos = l_data->layer_regs.x_pos;
	y_pos = l_data->layer_regs.y_pos;
	width  = l_data->layer_regs.width;
	height = l_data->layer_regs.height;

	if (!is_window_valid(mixer, x_pos, y_pos, width, height, scale))
		return -EINVAL;

	if (id == mixer->logo_layer_id) {
		if (mixer->logo_layer_en) {
			if (mixer->max_layers > XVMIX_MAX_OVERLAY_LAYERS)
				reg_writel(reg, XVMIX_LOGOSCALEFACTOR_DATA +
					   XVMIX_LOGO_OFFSET, scale);
			else
				reg_writel(reg, XVMIX_LOGOSCALEFACTOR_DATA,
					   scale);
			l_data->layer_regs.scale_fact = scale;
			status = 0;
		}
	} else {
		 /* Layer0-Layer15 */
		if (id < mixer->layer_cnt && l_data->hw_config.can_scale) {
			offset = id * XVMIX_REG_OFFSET;

			reg_writel(reg, (XVMIX_LAYERSCALE_0_DATA + offset),
				   scale);
			l_data->layer_regs.scale_fact = scale;
			status = 0;
		}
	}
	return status;
}

/**
 * xlnx_mix_set_layer_scale - Change video scale factor for video plane
 * @plane: Drm plane object describing layer to be modified
 * @val: Index of scale factor to use:
 *		0 = 1x
 *		1 = 2x
 *		2 = 4x
 *
 * Return:
 * Zero on success, either -EINVAL if scale value is illegal or
 * -ENODEV if layer does not exist (null)
 */
static int xlnx_mix_set_layer_scale(struct xlnx_mix_plane *plane,
				    uint64_t val)
{
	struct xlnx_mix_hw *mixer_hw = to_mixer_hw(plane);
	struct xlnx_mix_layer_data *layer = plane->mixer_layer;
	int ret;

	if (!layer || !layer->hw_config.can_scale)
		return -ENODEV;
	if (val > XVMIX_SCALE_FACTOR_4X) {
		DRM_ERROR("Mixer layer scale value illegal.\n");
		return -EINVAL;
	}
	xlnx_mix_disp_layer_disable(plane);
	msleep(50);
	ret = xlnx_mix_set_layer_scaling(mixer_hw, layer->id, val);
	xlnx_mix_disp_layer_enable(plane);

	return ret;
}

/**
 * xlnx_mix_set_layer_alpha - Set the alpha value
 * @mixer: Instance of mixer controlling layer to modify
 * @layer_id: Logical id of video overlay to adjust alpha setting
 * @alpha: Desired alpha setting (0-255) for layer specified
 *            255 = completely opaque
 *            0 = fully transparent
 *
 * Set the layer global transparency for a video overlay
 * Not applicable to background streaming layer
 *
 * Return:
 * Zero on success, -EINVAL on failure
 */
static int xlnx_mix_set_layer_alpha(struct xlnx_mix_hw *mixer,
				    enum xlnx_mix_layer_id layer_id, u32 alpha)
{
	struct xlnx_mix_layer_data *layer_data;
	u32 reg;
	int status = -EINVAL;

	layer_data = xlnx_mix_get_layer_data(mixer, layer_id);

	if (layer_id == mixer->logo_layer_id) {
		if (mixer->logo_layer_en) {
			if (mixer->max_layers > XVMIX_MAX_OVERLAY_LAYERS)
				reg = XVMIX_LOGOALPHA_DATA + XVMIX_LOGO_OFFSET;
			else
				reg = XVMIX_LOGOALPHA_DATA;
			reg_writel(mixer->base, reg, alpha);
			layer_data->layer_regs.alpha = alpha;
			status = 0;
		}
	} else {
		 /*Layer1-Layer15*/
		if (layer_id < mixer->layer_cnt &&
		    layer_data->hw_config.can_alpha) {
			u32 offset =  layer_id * XVMIX_REG_OFFSET;

			reg = XVMIX_LAYERALPHA_0_DATA;
			reg_writel(mixer->base, (reg + offset), alpha);
			layer_data->layer_regs.alpha = alpha;
			status = 0;
		}
	}
	return status;
}

/**
 * xlnx_mix_disp_set_layer_alpha - Change the transparency of an entire plane
 * @plane: Video layer affected by new alpha setting
 * @val: Value of transparency setting (0-255) with 255 being opaque
 *  0 being fully transparent
 *
 * Return:
 * Zero on success, -EINVAL on failure
 */
static int xlnx_mix_disp_set_layer_alpha(struct xlnx_mix_plane *plane,
					 uint64_t val)
{
	struct xlnx_mix_hw *mixer_hw = to_mixer_hw(plane);
	struct xlnx_mix_layer_data *layer = plane->mixer_layer;

	if (!layer || !layer->hw_config.can_alpha)
		return -ENODEV;
	if (val > XVMIX_ALPHA_MAX) {
		DRM_ERROR("Mixer layer alpha dts value illegal.\n");
		return -EINVAL;
	}
	return xlnx_mix_set_layer_alpha(mixer_hw, layer->id, val);
}

/**
 * xlnx_mix_set_layer_buff_addr - Set buff addr for layer
 * @mixer: Instance of mixer controlling layer to modify
 * @id: Logical id of video overlay to adjust alpha setting
 * @luma_addr: Start address of plane 1 of frame buffer for layer 1
 * @chroma_addr: Start address of plane 2 of frame buffer for layer 1
 *
 * Sets the buffer address of the specified layer
 * Return:
 * Zero on success, -EINVAL on failure
 */
static int xlnx_mix_set_layer_buff_addr(struct xlnx_mix_hw *mixer,
					enum xlnx_mix_layer_id id,
					dma_addr_t luma_addr,
					dma_addr_t chroma_addr)
{
	struct xlnx_mix_layer_data *layer_data;
	u32 align, offset;
	u32 reg1, reg2;

	if (id >= mixer->layer_cnt)
		return -EINVAL;

	/* Check if addr is aligned to aximm width (PPC * 64-bits) */
	align = mixer->ppc * 8;
	if ((luma_addr % align) != 0 || (chroma_addr % align) != 0)
		return -EINVAL;

	offset = (id - 1) * XVMIX_REG_OFFSET;
	reg1 = XVMIX_LAYER1_BUF1_V_DATA + offset;
	reg2 = XVMIX_LAYER1_BUF2_V_DATA + offset;
	layer_data = &mixer->layer_data[id];
	if (mixer->dma_addr_size == 64 && sizeof(dma_addr_t) == 8) {
		reg_writeq(mixer->base, reg1, luma_addr);
		reg_writeq(mixer->base, reg2, chroma_addr);
	} else {
		reg_writel(mixer->base, reg1, (u32)luma_addr);
		reg_writel(mixer->base, reg2, (u32)chroma_addr);
	}
	layer_data->layer_regs.buff_addr1 = luma_addr;
	layer_data->layer_regs.buff_addr2 = chroma_addr;

	return 0;
}

/**
 * xlnx_mix_hw_plane_dpms - Implementation of display power management
 * system call (dpms).
 * @plane: Plane/mixer layer to enable/disable (based on dpms value)
 * @dpms: Display power management state to act upon
 *
 * Designed to disable and turn off a plane and restore all attached drm
 * properities to their initial values.  Alterntively, if dpms is "on", will
 * enable a layer.
 */

static void
xlnx_mix_hw_plane_dpms(struct xlnx_mix_plane *plane, int dpms)
{
	struct xlnx_mix *mixer;

	if (!plane->mixer)
		return;
	mixer = plane->mixer;
	plane->dpms = dpms;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		xlnx_mix_disp_layer_enable(plane);
		break;
	default:
		xlnx_mix_mark_layer_inactive(plane);
		xlnx_mix_disp_layer_disable(plane);
		/* restore to default property values */
		if (mixer->alpha_prop)
			xlnx_mix_disp_set_layer_alpha(plane, XVMIX_ALPHA_MAX);
		if (mixer->scale_prop)
			xlnx_mix_set_layer_scale(plane, XVMIX_SCALE_FACTOR_1X);
	}
}

static void xlnx_mix_plane_dpms(struct drm_plane *base_plane, int dpms)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);
	unsigned int i;

	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);
	DRM_DEBUG_KMS("dpms: %d -> %d\n", plane->dpms, dpms);

	if (plane->dpms == dpms)
		return;
	plane->dpms = dpms;
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		/* start dma engine */
		for (i = 0; i < XVMIX_MAX_NUM_SUB_PLANES; i++)
			if (plane->dma[i].chan && plane->dma[i].is_active)
				dma_async_issue_pending(plane->dma[i].chan);
		xlnx_mix_hw_plane_dpms(plane, dpms);
		break;
	default:
		xlnx_mix_hw_plane_dpms(plane, dpms);
		/* stop dma engine and release descriptors */
		for (i = 0; i < XVMIX_MAX_NUM_SUB_PLANES; i++) {
			if (plane->dma[i].chan && plane->dma[i].is_active) {
				dmaengine_terminate_sync(plane->dma[i].chan);
				plane->dma[i].is_active = false;
			}
		}
		break;
	}
}

static int
xlnx_mix_disp_plane_atomic_set_property(struct drm_plane *base_plane,
					struct drm_plane_state *state,
				      struct drm_property *property, u64 val)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);
	struct xlnx_mix *mixer = plane->mixer;

	if (property == mixer->alpha_prop)
		return xlnx_mix_disp_set_layer_alpha(plane, val);
	else if (property == mixer->scale_prop)
		return xlnx_mix_set_layer_scale(plane, val);
	else
		return -EINVAL;
	return 0;
}

static int
xlnx_mix_disp_plane_atomic_get_property(struct drm_plane *base_plane,
					const struct drm_plane_state *state,
				      struct drm_property *property,
				      uint64_t *val)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);
	struct xlnx_mix *mixer = plane->mixer;
	struct xlnx_mix_hw *mixer_hw = to_mixer_hw(plane);
	u32 layer_id = plane->mixer_layer->id;

	if (property == mixer->alpha_prop)
		*val = mixer_hw->layer_data[layer_id].layer_regs.alpha;
	else if (property == mixer->scale_prop)
		*val = mixer_hw->layer_data[layer_id].layer_regs.scale_fact;
	else
		return -EINVAL;

	return 0;
}

/**
 * xlnx_mix_disp_plane_atomic_update_plane - plane update using atomic
 * @plane: plane object to update
 * @crtc: owning CRTC of owning plane
 * @fb: framebuffer to flip onto plane
 * @crtc_x: x offset of primary plane on crtc
 * @crtc_y: y offset of primary plane on crtc
 * @crtc_w: width of primary plane rectangle on crtc
 * @crtc_h: height of primary plane rectangle on crtc
 * @src_x: x offset of @fb for panning
 * @src_y: y offset of @fb for panning
 * @src_w: width of source rectangle in @fb
 * @src_h: height of source rectangle in @fb
 * @ctx: lock acquire context
 *
 * Provides a default plane update handler using the atomic driver interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int
xlnx_mix_disp_plane_atomic_update_plane(struct drm_plane *plane,
					struct drm_crtc *crtc,
					struct drm_framebuffer *fb,
					int crtc_x, int crtc_y,
					unsigned int crtc_w,
					unsigned int crtc_h,
					uint32_t src_x, uint32_t src_y,
					uint32_t src_w, uint32_t src_h,
					struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	int ret = 0;

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
	if (ret != 0)
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

static struct drm_plane_funcs xlnx_mix_plane_funcs = {
	.update_plane	= xlnx_mix_disp_plane_atomic_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.atomic_set_property	= xlnx_mix_disp_plane_atomic_set_property,
	.atomic_get_property	= xlnx_mix_disp_plane_atomic_get_property,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

/**
 * xlnx_mix_logo_load - Loads mixer's internal bram
 * @mixer: Mixer instance to act upon
 * @logo_w: Width of logo in pixels
 * @logo_h: Height of logo in pixels
 * @r_buf: Pointer to byte buffer array of R data values
 * @g_buf: Pointer to byte buffer array of G data values
 * @b_buf: Pointer to byte buffer array of B data values
 * @a_buf: Pointer to byte buffer array of A data values
 *
 * Loads mixer's internal bram with planar R, G, B and A data
 *
 * Return:
 * Zero on success, -ENODEV if logo layer not enabled; -EINVAL otherwise
 */
static int xlnx_mix_logo_load(struct xlnx_mix_hw *mixer, u32 logo_w, u32 logo_h,
			      u8 *r_buf, u8 *g_buf, u8 *b_buf, u8 *a_buf)
{
	void __iomem *reg = mixer->base;
	struct xlnx_mix_layer_data *layer_data;

	int x;
	u32 shift;
	u32 rword, gword, bword, aword;
	u32 pixel_cnt = logo_w * logo_h;
	u32 unaligned_pix_cnt = pixel_cnt % 4;
	u32 curr_x_pos, curr_y_pos;
	u32 rbase_addr, gbase_addr, bbase_addr, abase_addr;

	layer_data = xlnx_mix_get_layer_data(mixer, mixer->logo_layer_id);
	rword = 0;
	gword = 0;
	bword = 0;
	aword = 0;

	if (!layer_data)
		return -ENODEV;

	/* RGBA data should be 32-bit word aligned */
	if (unaligned_pix_cnt && mixer->logo_pixel_alpha_enabled)
		return -EINVAL;

	if (!(mixer->logo_layer_en &&
	      logo_w <= layer_data->hw_config.max_width &&
	    logo_h <= layer_data->hw_config.max_height))
		return -EINVAL;

	rbase_addr = XVMIX_LOGOR_V_BASE;
	gbase_addr = XVMIX_LOGOG_V_BASE;
	bbase_addr = XVMIX_LOGOB_V_BASE;
	abase_addr = XVMIX_LOGOA_V_BASE;

	for (x = 0; x < pixel_cnt; x++) {
		shift = (x % 4) * 8;
		rword |= r_buf[x] << shift;
		gword |= g_buf[x] << shift;
		bword |= b_buf[x] << shift;
		if (mixer->logo_pixel_alpha_enabled)
			aword |= a_buf[x] << shift;

		if (x % 4 == 3) {
			reg_writel(reg, (rbase_addr + (x - 3)), rword);
			reg_writel(reg, (gbase_addr + (x - 3)), gword);
			reg_writel(reg, (bbase_addr + (x - 3)), bword);
			if (mixer->logo_pixel_alpha_enabled)
				reg_writel(reg, (abase_addr + (x - 3)), aword);
		}
	}

	curr_x_pos = layer_data->layer_regs.x_pos;
	curr_y_pos = layer_data->layer_regs.y_pos;
	return xlnx_mix_set_layer_window(mixer, mixer->logo_layer_id,
					 curr_x_pos, curr_y_pos,
					 logo_w, logo_h, 0);
}

static int xlnx_mix_update_logo_img(struct xlnx_mix_plane *plane,
				    struct drm_gem_cma_object *buffer,
				     u32 src_w, u32 src_h)
{
	struct xlnx_mix_layer_data *logo_layer = plane->mixer_layer;
	struct xlnx_mix_hw *mixer = to_mixer_hw(plane);
	size_t pixel_cnt = src_h * src_w;
	bool per_pixel_alpha = false;
	u32 max_width = logo_layer->hw_config.max_width;
	u32 max_height = logo_layer->hw_config.max_height;
	u32 min_width = logo_layer->hw_config.min_width;
	u32 min_height = logo_layer->hw_config.min_height;
	u8 *r_data = NULL;
	u8 *g_data = NULL;
	u8 *b_data = NULL;
	u8 *a_data = NULL;
	size_t el_size = sizeof(u8);
	u8 *pixel_mem_data;
	int ret, i, j;

	/* ensure valid conditions for update */
	if (logo_layer->id != mixer->logo_layer_id)
		return 0;

	if (src_h > max_height || src_w > max_width ||
	    src_h < min_height || src_w < min_width) {
		DRM_ERROR("Mixer logo/cursor layer dimensions illegal.\n");
		return -EINVAL;
	}

	if (!xlnx_mix_isfmt_support(plane->mixer_layer->hw_config.vid_fmt)) {
		DRM_ERROR("DRM color format not supported for logo layer\n");
		return -EINVAL;
	}
	per_pixel_alpha = (logo_layer->hw_config.vid_fmt ==
			   DRM_FORMAT_RGBA8888) ? true : false;
	r_data = kcalloc(pixel_cnt, el_size, GFP_KERNEL);
	g_data = kcalloc(pixel_cnt, el_size, GFP_KERNEL);
	b_data = kcalloc(pixel_cnt, el_size, GFP_KERNEL);
	if (per_pixel_alpha)
		a_data = kcalloc(pixel_cnt, el_size, GFP_KERNEL);

	if (!r_data || !g_data || !b_data || (per_pixel_alpha && !a_data)) {
		DRM_ERROR("Unable to allocate memory for logo layer data\n");
		ret = -ENOMEM;
		goto free;
	}
	/* ensure buffer attributes have changed to indicate new logo
	 * has been created
	 */
	if ((phys_addr_t)buffer->vaddr == logo_layer->layer_regs.buff_addr1 &&
	    src_w == logo_layer->layer_regs.width &&
	    src_h == logo_layer->layer_regs.height)
		return 0;

	/* cache buffer address for future comparison */
	logo_layer->layer_regs.buff_addr1 = (phys_addr_t)buffer->vaddr;
	pixel_mem_data = (u8 *)(buffer->vaddr);
	for (i = 0, j = 0; j < pixel_cnt; j++) {
		if (per_pixel_alpha && a_data)
			a_data[j] = pixel_mem_data[i++];

		b_data[j] = pixel_mem_data[i++];
		g_data[j] = pixel_mem_data[i++];
		r_data[j] = pixel_mem_data[i++];
	}
	ret = xlnx_mix_logo_load(to_mixer_hw(plane), src_w, src_h, r_data,
				 g_data, b_data,
				 per_pixel_alpha ? a_data : NULL);
free:
	kfree(r_data);
	kfree(g_data);
	kfree(b_data);
	kfree(a_data);

	return ret;
}

/**
 * xlnx_mix_set_plane - Implementation of DRM plane_update callback
 * @plane: xlnx_mix_plane object containing references to
 *  the base plane and mixer
 * @fb: Framebuffer descriptor
 * @crtc_x: X position of layer on crtc.  Note, if the plane represents either
 *  the master hardware layer (video0) or the layer representing the DRM primary
 *  layer, the crtc x/y coordinates are either ignored and/or set to 0/0
 *  respectively.
 * @crtc_y: Y position of layer.  See description of crtc_x handling
 * for more inforation.
 * @src_x: x-offset in memory buffer from which to start reading
 * @src_y: y-offset in memory buffer from which to start reading
 * @src_w: Number of horizontal pixels to read from memory per row
 * @src_h: Number of rows of video data to read from memory
 *
 * Configures a mixer layer to comply with user space SET_PLANE icotl
 * call.
 *
 * Return:
 * Zero on success, non-zero linux error code otherwise.
 */
static int xlnx_mix_set_plane(struct xlnx_mix_plane *plane,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      u32 src_x, u32 src_y,
			      u32 src_w, u32 src_h)
{
	struct xlnx_mix_hw *mixer_hw;
	struct xlnx_mix *mixer;
	struct drm_gem_cma_object *luma_buffer;
	u32 luma_stride = fb->pitches[0];
	dma_addr_t luma_addr, chroma_addr = 0;
	u32 active_area_width;
	u32 active_area_height;
	enum xlnx_mix_layer_id layer_id;
	int ret;
	const struct drm_format_info *info = fb->format;

	mixer = plane->mixer;
	mixer_hw = &mixer->mixer_hw;
	layer_id = plane->mixer_layer->id;
	active_area_width =
		mixer->drm_primary_layer->mixer_layer->layer_regs.width;
	active_area_height =
		mixer->drm_primary_layer->mixer_layer->layer_regs.height;
	/* compute memory data */
	luma_buffer = drm_fb_cma_get_gem_obj(fb, 0);
	luma_addr = drm_fb_cma_get_gem_addr(fb, plane->base.state, 0);
	if (!luma_addr) {
		DRM_ERROR("%s failed to get luma paddr\n", __func__);
		return -EINVAL;
	}

	if (info->num_planes > 1) {
		chroma_addr = drm_fb_cma_get_gem_addr(fb, plane->base.state, 1);
		if (!chroma_addr) {
			DRM_ERROR("failed to get chroma paddr\n");
			return -EINVAL;
		}
	}
	ret = xlnx_mix_mark_layer_active(plane);
	if (ret)
		return ret;

	switch (layer_id) {
	case XVMIX_LAYER_MASTER:
		if (!plane->mixer_layer->hw_config.is_streaming)
			xlnx_mix_mark_layer_inactive(plane);
		if (mixer->drm_primary_layer == mixer->hw_master_layer) {
			xlnx_mix_layer_disable(mixer_hw, layer_id);
			ret = xlnx_mix_set_active_area(mixer_hw, src_w, src_h);
			if (ret)
				return ret;
			xlnx_mix_layer_enable(mixer_hw, layer_id);

		} else if (src_w != active_area_width ||
			   src_h != active_area_height) {
			DRM_ERROR("Invalid dimensions for mixer layer 0.\n");
			return -EINVAL;
		}
		break;

	default:
		ret = xlnx_mix_set_layer_dimensions(plane, crtc_x, crtc_y,
						    src_w, src_h, luma_stride);
		if (ret)
			break;
		if (layer_id == mixer_hw->logo_layer_id) {
			ret = xlnx_mix_update_logo_img(plane, luma_buffer,
						       src_w, src_h);
		} else {
			if (!plane->mixer_layer->hw_config.is_streaming)
				ret = xlnx_mix_set_layer_buff_addr
					(mixer_hw, plane->mixer_layer->id,
					 luma_addr, chroma_addr);
		}
	}
	return ret;
}

/* mode set a plane */
static int xlnx_mix_plane_mode_set(struct drm_plane *base_plane,
				   struct drm_framebuffer *fb,
				   int crtc_x, int crtc_y,
				   unsigned int crtc_w, unsigned int crtc_h,
				   u32 src_x, uint32_t src_y,
				   u32 src_w, uint32_t src_h)
{
	struct xlnx_mix_plane *plane = to_xlnx_plane(base_plane);
	struct xlnx_mix_hw *mixer_hw = to_mixer_hw(plane);
	const struct drm_format_info *info = fb->format;
	size_t i = 0;
	dma_addr_t luma_paddr;
	int ret;
	u32 stride;

	/* JPM TODO begin start of code to extract into prep-interleaved*/
	DRM_DEBUG_KMS("plane->id: %d\n", plane->id);
	DRM_DEBUG_KMS("h: %d(%d), v: %d(%d)\n", src_w, crtc_x, src_h, crtc_y);

	/* We have multiple dma channels.  Set each per video plane */
	for (; i < info->num_planes; i++) {
		unsigned int width = src_w / (i ? info->hsub : 1);
		unsigned int height = src_h / (i ? info->vsub : 1);

		luma_paddr = drm_fb_cma_get_gem_addr(fb, base_plane->state, i);
		if (!luma_paddr) {
			DRM_ERROR("%s failed to get luma paddr\n", __func__);
			return -EINVAL;
		}

		plane->dma[i].xt.numf = height;
		plane->dma[i].sgl[0].size =
			drm_format_plane_width_bytes(info, 0, width);
		plane->dma[i].sgl[0].icg = fb->pitches[0] -
						plane->dma[i].sgl[0].size;
		plane->dma[i].xt.src_start = luma_paddr;
		plane->dma[i].xt.frame_size = info->num_planes;
		plane->dma[i].xt.dir = DMA_MEM_TO_DEV;
		plane->dma[i].xt.src_sgl = true;
		plane->dma[i].xt.dst_sgl = false;
		plane->dma[i].is_active = true;
	}

	for (; i < XVMIX_MAX_NUM_SUB_PLANES; i++)
		plane->dma[i].is_active = false;
	/* Do we have a video format aware dma channel?
	 * If so, modify descriptor accordingly
	 */
	if (plane->dma[0].chan && !plane->dma[1].chan && info->num_planes > 1) {
		stride = plane->dma[0].sgl[0].size + plane->dma[0].sgl[0].icg;
		plane->dma[0].sgl[0].src_icg = plane->dma[1].xt.src_start -
				plane->dma[0].xt.src_start -
				(plane->dma[0].xt.numf * stride);
	}

	if (mixer_hw->csc_enabled) {
		/**
		 * magic numbers of coefficient table for colorimetry
		 * and range are derived from the following references:
		 * [1] Rec. ITU-R BT.601-6
		 * [2] Rec. ITU-R BT.709-5
		 * [3] Rec. ITU-R BT.2020
		 * [4] http://en.wikipedia.org/wiki/YCbCr
		 * coefficient table supports BT601 / BT709 / BT2020 encoding
		 * schemes and 16-235(limited) / 16-240(full) range.
		 */
		xlnx_mix_set_yuv2_rgb_coeff(plane,
					    base_plane->state->color_encoding,
					    base_plane->state->color_range);
		xlnx_mix_set_rgb2_yuv_coeff(plane,
					    base_plane->state->color_encoding,
					    base_plane->state->color_range);
	}

	ret = xlnx_mix_set_plane(plane, fb, crtc_x, crtc_y, src_x, src_y,
				 src_w, src_h);
	return ret;
}

static int xlnx_mix_plane_prepare_fb(struct drm_plane *plane,
				     struct drm_plane_state *new_state)
{
	return 0;
}

static void xlnx_mix_plane_cleanup_fb(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
}

static int xlnx_mix_plane_atomic_check(struct drm_plane *plane,
				       struct drm_plane_state *state)
{
	int scale;
	struct xlnx_mix_plane *mix_plane = to_xlnx_plane(plane);
	struct xlnx_mix_hw *mixer_hw = to_mixer_hw(mix_plane);
	struct xlnx_mix *mix;

	/* No check required for the drm_primary_plane */
	mix = container_of(mixer_hw, struct xlnx_mix, mixer_hw);
	if (mix->drm_primary_layer == mix_plane)
		return 0;

	scale = xlnx_mix_get_layer_scaling(mixer_hw,
					   mix_plane->mixer_layer->id);
	if (is_window_valid(mixer_hw, state->crtc_x, state->crtc_y,
			    state->src_w >> 16, state->src_h >> 16, scale))
		return 0;

	return -EINVAL;
}

static void xlnx_mix_plane_atomic_update(struct drm_plane *plane,
					 struct drm_plane_state *old_state)
{
	int ret;

	if (!plane->state->crtc || !plane->state->fb)
		return;

	if (old_state->fb &&
	    old_state->fb->format->format != plane->state->fb->format->format)
		xlnx_mix_plane_dpms(plane, DRM_MODE_DPMS_OFF);

	ret = xlnx_mix_plane_mode_set(plane, plane->state->fb,
				      plane->state->crtc_x,
				      plane->state->crtc_y,
				      plane->state->crtc_w,
				      plane->state->crtc_h,
				      plane->state->src_x >> 16,
				      plane->state->src_y >> 16,
				      plane->state->src_w >> 16,
				      plane->state->src_h >> 16);
	if (ret) {
		DRM_ERROR("failed to mode-set a plane\n");
		return;
	}
	/* apply the new fb addr */
	xlnx_mix_plane_commit(plane);
	/* make sure a plane is on */
	xlnx_mix_plane_dpms(plane, DRM_MODE_DPMS_ON);
}

static void xlnx_mix_plane_atomic_disable(struct drm_plane *plane,
					  struct drm_plane_state *old_state)
{
	xlnx_mix_plane_dpms(plane, DRM_MODE_DPMS_OFF);
}

static int xlnx_mix_plane_atomic_async_check(struct drm_plane *plane,
					     struct drm_plane_state *state)
{
	return 0;
}

static void
xlnx_mix_plane_atomic_async_update(struct drm_plane *plane,
				   struct drm_plane_state *new_state)
{
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(new_state->state, plane);

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

	xlnx_mix_plane_atomic_update(plane, old_state);
}

static const struct drm_plane_helper_funcs xlnx_mix_plane_helper_funcs = {
	.prepare_fb	= xlnx_mix_plane_prepare_fb,
	.cleanup_fb	= xlnx_mix_plane_cleanup_fb,
	.atomic_check	= xlnx_mix_plane_atomic_check,
	.atomic_update	= xlnx_mix_plane_atomic_update,
	.atomic_disable	= xlnx_mix_plane_atomic_disable,
	.atomic_async_check = xlnx_mix_plane_atomic_async_check,
	.atomic_async_update = xlnx_mix_plane_atomic_async_update,
};

static int xlnx_mix_init_plane(struct xlnx_mix_plane *plane,
			       unsigned int poss_crtcs,
			       struct device_node *layer_node)
{
	struct xlnx_mix *mixer = plane->mixer;
	char name[16];
	enum drm_plane_type type;
	int ret, i;

	plane->dpms = DRM_MODE_DPMS_OFF;
	type = DRM_PLANE_TYPE_OVERLAY;

	for (i = 0; i < XVMIX_MAX_NUM_SUB_PLANES; i++) {
		snprintf(name, sizeof(name), "dma%d", i);
		plane->dma[i].chan = of_dma_request_slave_channel(layer_node,
								  name);
		if (PTR_ERR(plane->dma[i].chan) == -ENODEV) {
			plane->dma[i].chan = NULL;
			continue;
		}
		if (IS_ERR(plane->dma[i].chan)) {
			DRM_ERROR("failed to request dma channel\n");
			ret = PTR_ERR(plane->dma[i].chan);
			plane->dma[i].chan = NULL;
			goto err_dma;
		}
	}
	if (!xlnx_mix_isfmt_support(plane->mixer_layer->hw_config.vid_fmt)) {
		DRM_ERROR("DRM color format not supported by mixer\n");
		ret = -ENODEV;
		goto err_init;
	}
	plane->format = plane->mixer_layer->hw_config.vid_fmt;
	if (plane == mixer->hw_logo_layer)
		type = DRM_PLANE_TYPE_CURSOR;
	if (plane == mixer->drm_primary_layer)
		type = DRM_PLANE_TYPE_PRIMARY;

	/* initialize drm plane */
	ret = drm_universal_plane_init(mixer->drm, &plane->base,
				       poss_crtcs, &xlnx_mix_plane_funcs,
				       &plane->format,
				       1, NULL, type, NULL);

	if (ret) {
		DRM_ERROR("failed to initialize plane\n");
		goto err_init;
	}
	drm_plane_helper_add(&plane->base, &xlnx_mix_plane_helper_funcs);
	of_node_put(layer_node);

	return 0;

err_init:
	xlnx_mix_disp_layer_disable(plane);
err_dma:
	for (i = 0; i < XVMIX_MAX_NUM_SUB_PLANES; i++)
		if (plane->dma[i].chan)
			dma_release_channel(plane->dma[i].chan);

	of_node_put(layer_node);
	return ret;
}

static int xlnx_mix_parse_dt_bg_video_fmt(struct device_node *node,
					  struct xlnx_mix_hw *mixer_hw)
{
	struct device_node *layer_node;
	struct xlnx_mix_layer_data *layer;
	const char *vformat;

	layer_node = of_get_child_by_name(node, "layer_0");
	layer = &mixer_hw->layer_data[XVMIX_MASTER_LAYER_IDX];

	/* Set default values */
	layer->hw_config.can_alpha = false;
	layer->hw_config.can_scale = false;
	layer->hw_config.min_width = XVMIX_LAYER_WIDTH_MIN;
	layer->hw_config.min_height = XVMIX_LAYER_HEIGHT_MIN;

	if (of_property_read_string(layer_node, "xlnx,vformat",
				    &vformat)) {
		DRM_ERROR("No xlnx,vformat value for layer 0 in dts\n");
		return -EINVAL;
	}
	strcpy((char *)&layer->hw_config.vid_fmt, vformat);
	layer->hw_config.is_streaming =
		of_property_read_bool(layer_node, "xlnx,layer-streaming");
	if (of_property_read_u32(node, "xlnx,bpc", &mixer_hw->bg_layer_bpc)) {
		DRM_ERROR("Failed to get bits per component (bpc) prop\n");
		return -EINVAL;
	}
	if (of_property_read_u32(layer_node, "xlnx,layer-max-width",
				 &layer->hw_config.max_width)) {
		DRM_ERROR("Failed to get screen width prop\n");
		return -EINVAL;
	} else if (layer->hw_config.max_width > XVMIX_DISP_MAX_WIDTH ||
		   layer->hw_config.max_width < XVMIX_DISP_MIN_WIDTH) {
		DRM_ERROR("Invalid width in dt");
		return -EINVAL;
	}

	mixer_hw->max_layer_width = layer->hw_config.max_width;
	if (of_property_read_u32(layer_node, "xlnx,layer-max-height",
				 &layer->hw_config.max_height)) {
		DRM_ERROR("Failed to get screen height prop\n");
		return -EINVAL;
	} else if (layer->hw_config.max_height > XVMIX_DISP_MAX_HEIGHT ||
		   layer->hw_config.max_height < XVMIX_DISP_MIN_HEIGHT) {
		DRM_ERROR("Invalid height in dt");
		return -EINVAL;
	}

	mixer_hw->max_layer_height = layer->hw_config.max_height;
	layer->id = XVMIX_LAYER_MASTER;

	return 0;
}

static int xlnx_mix_parse_dt_logo_data(struct device_node *node,
				       struct xlnx_mix_hw *mixer_hw)
{
	struct xlnx_mix_layer_data *layer_data;
	struct device_node *logo_node;
	u32 max_width, max_height;

	logo_node = of_get_child_by_name(node, "logo");
	if (!logo_node) {
		DRM_ERROR("No logo node specified in device tree.\n");
		return -EINVAL;
	}

	layer_data = &mixer_hw->layer_data[XVMIX_LOGO_LAYER_IDX];

	/* set defaults for logo layer */
	layer_data->hw_config.min_height = XVMIX_LOGO_LAYER_HEIGHT_MIN;
	layer_data->hw_config.min_width = XVMIX_LOGO_LAYER_WIDTH_MIN;
	layer_data->hw_config.is_streaming = false;
	layer_data->hw_config.vid_fmt = DRM_FORMAT_RGB888;
	layer_data->hw_config.can_alpha = true;
	layer_data->hw_config.can_scale = true;
	layer_data->layer_regs.buff_addr1 = 0;
	layer_data->layer_regs.buff_addr2 = 0;
	layer_data->id = mixer_hw->logo_layer_id;

	if (of_property_read_u32(logo_node, "xlnx,logo-width", &max_width)) {
		DRM_ERROR("Failed to get logo width prop\n");
		return -EINVAL;
	}
	if (max_width > XVMIX_LOGO_LAYER_WIDTH_MAX ||
	    max_width < XVMIX_LOGO_LAYER_WIDTH_MIN) {
		DRM_ERROR("Illegal mixer logo layer width.\n");
		return -EINVAL;
	}
	layer_data->hw_config.max_width = max_width;
	mixer_hw->max_logo_layer_width = layer_data->hw_config.max_width;

	if (of_property_read_u32(logo_node, "xlnx,logo-height", &max_height)) {
		DRM_ERROR("Failed to get logo height prop\n");
		return -EINVAL;
	}
	if (max_height > XVMIX_LOGO_LAYER_HEIGHT_MAX ||
	    max_height < XVMIX_LOGO_LAYER_HEIGHT_MIN) {
		DRM_ERROR("Illegal mixer logo layer height.\n");
		return -EINVAL;
	}
	layer_data->hw_config.max_height = max_height;
	mixer_hw->max_logo_layer_height = layer_data->hw_config.max_height;
	mixer_hw->logo_pixel_alpha_enabled =
		of_property_read_bool(logo_node, "xlnx,logo-pixel-alpha");
	if (mixer_hw->logo_pixel_alpha_enabled)
		layer_data->hw_config.vid_fmt = DRM_FORMAT_RGBA8888;

	return 0;
}

static int xlnx_mix_dt_parse(struct device *dev, struct xlnx_mix *mixer)
{
	struct xlnx_mix_plane *planes;
	struct xlnx_mix_hw *mixer_hw;
	struct device_node *node, *vtc_node;
	struct xlnx_mix_layer_data *l_data;
	struct resource	res;
	int ret, l_cnt, i;

	node = dev->of_node;
	mixer_hw = &mixer->mixer_hw;
	mixer->dpms = DRM_MODE_DPMS_OFF;

	mixer_hw->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(mixer_hw->reset_gpio)) {
		ret = PTR_ERR(mixer_hw->reset_gpio);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "No gpio probed for mixer. Deferring\n");
		else
			dev_err(dev, "No reset gpio info from dts for mixer\n");
		return ret;
	}
	gpiod_set_raw_value(mixer_hw->reset_gpio, 0);
	gpiod_set_raw_value(mixer_hw->reset_gpio, 1);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "Invalid memory address for mixer %d\n", ret);
		return ret;
	}
	/* Read in mandatory global dts properties */
	mixer_hw->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(mixer_hw->base)) {
		dev_err(dev, "Failed to map io mem space for mixer\n");
		return PTR_ERR(mixer_hw->base);
	}
	if (of_device_is_compatible(dev->of_node, "xlnx,mixer-4.0") ||
	    of_device_is_compatible(dev->of_node, "xlnx,mixer-5.0")) {
		mixer_hw->max_layers = 18;
		mixer_hw->logo_en_mask = BIT(23);
		mixer_hw->enable_all_mask = (GENMASK(16, 0) |
						mixer_hw->logo_en_mask);
	} else {
		mixer_hw->max_layers = 10;
		mixer_hw->logo_en_mask = BIT(15);
		mixer_hw->enable_all_mask = (GENMASK(8, 0) |
						mixer_hw->logo_en_mask);
	}
	if (of_device_is_compatible(dev->of_node, "xlnx,mixer-5.0")) {
		const char *prop_name = "xlnx,enable-csc-coefficient-register";

		mixer_hw->csc_enabled = of_property_read_bool(node, prop_name);
	}

	ret = of_property_read_u32(node, "xlnx,num-layers",
				   &mixer_hw->num_layers);
	if (ret) {
		dev_err(dev, "No xlnx,num-layers dts prop for mixer node\n");
		return ret;
	}
	mixer_hw->logo_layer_id = mixer_hw->max_layers - 1;
	if (mixer_hw->num_layers > mixer_hw->max_layers) {
		dev_err(dev, "Num layer nodes in device tree > mixer max\n");
		return -EINVAL;
	}
	ret = of_property_read_u32(node, "xlnx,dma-addr-width",
				   &mixer_hw->dma_addr_size);
	if (ret) {
		dev_err(dev, "missing addr-width dts prop\n");
		return ret;
	}
	if (mixer_hw->dma_addr_size != 32 && mixer_hw->dma_addr_size != 64) {
		dev_err(dev, "invalid addr-width dts prop\n");
		return -EINVAL;
	}

	/* VTC Bridge support */
	vtc_node = of_parse_phandle(node, "xlnx,bridge", 0);
	if (vtc_node) {
		mixer->vtc_bridge = of_xlnx_bridge_get(vtc_node);
		if (!mixer->vtc_bridge) {
			dev_info(dev, "Didn't get vtc bridge instance\n");
			return -EPROBE_DEFER;
		}
	} else {
		dev_info(dev, "vtc bridge property not present\n");
	}

	mixer_hw->logo_layer_en = of_property_read_bool(node,
							"xlnx,logo-layer");
	l_cnt = mixer_hw->num_layers + (mixer_hw->logo_layer_en ? 1 : 0);
	mixer_hw->layer_cnt = l_cnt;

	l_data = devm_kzalloc(dev, sizeof(*l_data) * l_cnt, GFP_KERNEL);
	if (!l_data)
		return -ENOMEM;
	mixer_hw->layer_data = l_data;
	/* init DRM planes */
	planes = devm_kzalloc(dev, sizeof(*planes) * l_cnt, GFP_KERNEL);
	if (!planes)
		return -ENOMEM;
	mixer->planes = planes;
	mixer->num_planes = l_cnt;
	for (i = 0; i < mixer->num_planes; i++)
		mixer->planes[i].mixer = mixer;

	/* establish background layer video properties from dts */
	ret = xlnx_mix_parse_dt_bg_video_fmt(node, mixer_hw);
	if (ret)
		return ret;
	if (mixer_hw->logo_layer_en) {
		/* read logo data from dts */
		ret = xlnx_mix_parse_dt_logo_data(node, mixer_hw);
		return ret;
	}
	return 0;
}

static int xlnx_mix_of_init_layer(struct device *dev, struct device_node *node,
				  char *name, struct xlnx_mix_layer_data *layer,
				  u32 max_width, struct xlnx_mix *mixer, int id)
{
	struct device_node *layer_node;
	const char *vformat;
	int ret;

	layer_node = of_get_child_by_name(node, name);
	if (!layer_node)
		return -EINVAL;

	/* Set default values */
	layer->hw_config.can_alpha = false;
	layer->hw_config.can_scale = false;
	layer->hw_config.is_streaming = false;
	layer->hw_config.max_width = max_width;
	layer->hw_config.min_width = XVMIX_LAYER_WIDTH_MIN;
	layer->hw_config.min_height = XVMIX_LAYER_HEIGHT_MIN;
	layer->hw_config.vid_fmt = 0;
	layer->id = 0;
	mixer->planes[id].mixer_layer = layer;

	ret = of_property_read_u32(layer_node, "xlnx,layer-id", &layer->id);
	if (ret) {
		dev_err(dev, "xlnx,layer-id property not found\n");
		return ret;
	}
	if (layer->id < 1 || layer->id >= mixer->mixer_hw.max_layers) {
		dev_err(dev, "Mixer layer id %u in dts is out of legal range\n",
			layer->id);
		return -EINVAL;
	}
	ret = of_property_read_string(layer_node, "xlnx,vformat", &vformat);
	if (ret) {
		dev_err(dev, "No mixer layer vformat in dts for layer id %d\n",
			layer->id);
		return ret;
	}

	strcpy((char *)&layer->hw_config.vid_fmt, vformat);
	layer->hw_config.can_scale =
		    of_property_read_bool(layer_node, "xlnx,layer-scale");
	if (layer->hw_config.can_scale) {
		ret = of_property_read_u32(layer_node, "xlnx,layer-max-width",
					   &layer->hw_config.max_width);
		if (ret) {
			dev_err(dev, "Mixer layer %d dts missing width prop.\n",
				layer->id);
			return ret;
		}

		if (layer->hw_config.max_width > max_width) {
			dev_err(dev, "Illlegal Mixer layer %d width %d\n",
				layer->id, layer->hw_config.max_width);
			return -EINVAL;
		}
	}
	layer->hw_config.can_alpha =
		    of_property_read_bool(layer_node, "xlnx,layer-alpha");
	layer->hw_config.is_streaming =
		    of_property_read_bool(layer_node, "xlnx,layer-streaming");
	if (of_property_read_bool(layer_node, "xlnx,layer-primary")) {
		if (mixer->drm_primary_layer) {
			dev_err(dev,
				"More than one primary layer in mixer dts\n");
			return -EINVAL;
		}
		mixer->drm_primary_layer = &mixer->planes[id];
	}
	ret = xlnx_mix_init_plane(&mixer->planes[id], 1, layer_node);
	if (ret)
		dev_err(dev, "Unable to init drm mixer plane id = %u", id);

	return ret;
}

static irqreturn_t xlnx_mix_intr_handler(int irq, void *data)
{
	struct xlnx_mix_hw *mixer = data;
	u32 intr = xlnx_mix_get_intr_status(mixer);

	if (!intr)
		return IRQ_NONE;
	if (mixer->intrpt_handler_fn)
		mixer->intrpt_handler_fn(mixer->intrpt_data);
	xlnx_mix_clear_intr_status(mixer, intr);

	return IRQ_HANDLED;
}

static void xlnx_mix_create_plane_properties(struct xlnx_mix *mixer)
{
	mixer->scale_prop = drm_property_create_range(mixer->drm, 0, "scale",
						      XVMIX_SCALE_FACTOR_1X,
						      XVMIX_SCALE_FACTOR_4X);
	mixer->alpha_prop = drm_property_create_range(mixer->drm, 0, "alpha",
						      XVMIX_ALPHA_MIN,
						      XVMIX_ALPHA_MAX);
}

static int xlnx_mix_plane_create(struct device *dev, struct xlnx_mix *mixer)
{
	struct xlnx_mix_hw		*mixer_hw;
	struct device_node		*node, *layer_node;
	char				name[20];
	struct xlnx_mix_layer_data	*layer_data;
	int				ret, i;
	int				layer_idx;

	node = dev->of_node;
	mixer_hw = &mixer->mixer_hw;
	xlnx_mix_create_plane_properties(mixer);

	mixer->planes[XVMIX_MASTER_LAYER_IDX].mixer_layer =
				&mixer_hw->layer_data[XVMIX_MASTER_LAYER_IDX];
	mixer->planes[XVMIX_MASTER_LAYER_IDX].id = XVMIX_MASTER_LAYER_IDX;
	mixer->hw_master_layer = &mixer->planes[XVMIX_MASTER_LAYER_IDX];

	if (mixer_hw->logo_layer_en) {
		mixer->planes[XVMIX_LOGO_LAYER_IDX].mixer_layer =
				&mixer_hw->layer_data[XVMIX_LOGO_LAYER_IDX];
		mixer->planes[XVMIX_LOGO_LAYER_IDX].id = XVMIX_LOGO_LAYER_IDX;
		mixer->hw_logo_layer = &mixer->planes[XVMIX_LOGO_LAYER_IDX];
		layer_node = of_get_child_by_name(node, "logo");
		ret = xlnx_mix_init_plane(&mixer->planes[XVMIX_LOGO_LAYER_IDX],
					  1, layer_node);
		if (ret)
			return ret;
	}
	layer_idx = mixer_hw->logo_layer_en ? 2 : 1;
	for (i = 1; i < mixer_hw->num_layers; i++, layer_idx++) {
		snprintf(name, sizeof(name), "layer_%d", i);
		ret = xlnx_mix_of_init_layer(dev, node, name,
					     &mixer_hw->layer_data[layer_idx],
					     mixer_hw->max_layer_width,
					     mixer, layer_idx);
		if (ret)
			return ret;
	}
	/* If none of the overlay layers were designated as the drm
	 * primary layer, default to the mixer's video0 layer as drm primary
	 */
	if (!mixer->drm_primary_layer)
		mixer->drm_primary_layer = mixer->hw_master_layer;
	layer_node = of_get_child_by_name(node, "layer_0");
	ret = xlnx_mix_init_plane(&mixer->planes[XVMIX_MASTER_LAYER_IDX], 1,
				  layer_node);
	/* request irq and obtain pixels-per-clock (ppc) property */
	mixer_hw->irq = irq_of_parse_and_map(node, 0);
	if (mixer_hw->irq > 0) {
		ret = devm_request_irq(dev, mixer_hw->irq,
				       xlnx_mix_intr_handler,
				       IRQF_SHARED, "xlnx-mixer", mixer_hw);
		if (ret) {
			dev_err(dev, "Failed to request irq\n");
			return ret;
		}
	}
	ret = of_property_read_u32(node, "xlnx,ppc", &mixer_hw->ppc);
	if (ret) {
		dev_err(dev, "No xlnx,ppc property for mixer dts\n");
		return ret;
	}

	mixer->max_width = mixer_hw->max_layer_width;
	mixer->max_height = mixer_hw->max_layer_height;

	if (mixer->hw_logo_layer) {
		layer_data = &mixer_hw->layer_data[XVMIX_LOGO_LAYER_IDX];
		mixer->max_cursor_width = layer_data->hw_config.max_width;
		mixer->max_cursor_height = layer_data->hw_config.max_height;
	}
	return 0;
}

/**
 * xlnx_mix_plane_restore - Restore the plane states
 * @mixer: mixer device core structure
 *
 * Restore the plane states to the default ones. Any state that needs to be
 * restored should be here. This improves consistency as applications see
 * the same default values, and removes mismatch between software and hardware
 * values as software values are updated as hardware values are reset.
 */
static void xlnx_mix_plane_restore(struct xlnx_mix *mixer)
{
	struct xlnx_mix_plane *plane;
	unsigned int i;

	if (!mixer)
		return;
	/*
	 * Reinitialize property default values as they get reset by DPMS OFF
	 * operation. User will read the correct default values later, and
	 * planes will be initialized with default values.
	 */
	for (i = 0; i < mixer->num_planes; i++) {
		plane = &mixer->planes[i];
		if (!plane)
			continue;
		xlnx_mix_hw_plane_dpms(plane, DRM_MODE_DPMS_OFF);
	}
}

/**
 * xlnx_mix_set_bkg_col - Set background color
 * @mixer: Mixer instance to program with new background color
 * @rgb_value: RGB encoded as 32-bit integer in little-endian format
 *
 * Set the color to be output as background color when background stream layer
 */
static void xlnx_mix_set_bkg_col(struct xlnx_mix_hw *mixer, u64 rgb_value)
{
	u32 bg_bpc = mixer->bg_layer_bpc;
	u32 bpc_mask_shift = XVMIX_MAX_BPC - bg_bpc;
	u32 val_mask = (GENMASK(15, 0) >> bpc_mask_shift);
	u16 b_val = (rgb_value >> (bg_bpc * 2)) & val_mask;
	u16 g_val = (rgb_value >> bg_bpc) & val_mask;
	u16 r_val = (rgb_value >> 0) &  val_mask;

	/* Set Background Color */
	reg_writel(mixer->base, XVMIX_BACKGROUND_Y_R_DATA, r_val);
	reg_writel(mixer->base, XVMIX_BACKGROUND_U_G_DATA, g_val);
	reg_writel(mixer->base, XVMIX_BACKGROUND_V_B_DATA, b_val);
	mixer->bg_color = rgb_value;
}

/**
 * xlnx_mix_reset - Reset the mixer core video generator
 * @mixer: Mixer core instance for which to start video output
 *
 * Toggle the reset gpio and restores the bg color, plane and interrupt mask.
 */
static void xlnx_mix_reset(struct xlnx_mix *mixer)
{
	struct xlnx_mix_hw *mixer_hw = &mixer->mixer_hw;

	gpiod_set_raw_value(mixer_hw->reset_gpio, 0);
	gpiod_set_raw_value(mixer_hw->reset_gpio, 1);
	/* restore layer properties and bg color after reset */
	xlnx_mix_set_bkg_col(mixer_hw, mixer_hw->bg_color);
	xlnx_mix_plane_restore(mixer);
	xlnx_mix_intrpt_enable_done(&mixer->mixer_hw);
}

static void xlnx_mix_dpms(struct xlnx_mix *mixer, int dpms)
{
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		xlnx_mix_start(&mixer->mixer_hw);
		break;
	default:
		xlnx_mix_stop(&mixer->mixer_hw);
		mdelay(50); /* let IP shut down */
		xlnx_mix_reset(mixer);
	}
}

/* set crtc dpms */
static void xlnx_mix_crtc_dpms(struct drm_crtc *base_crtc, int dpms)
{
	struct xlnx_crtc *crtc = to_xlnx_crtc(base_crtc);
	struct xlnx_mix *mixer = to_xlnx_mixer(crtc);
	int ret;
	struct videomode vm;
	struct drm_display_mode *mode = &base_crtc->mode;

	DRM_DEBUG_KMS("dpms: %d\n", dpms);
	if (mixer->dpms == dpms)
		return;
	mixer->dpms = dpms;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		if (!mixer->pixel_clock_enabled) {
			ret = clk_prepare_enable(mixer->pixel_clock);
			if (ret) {
				DRM_ERROR("failed to enable a pixel clock\n");
				mixer->pixel_clock_enabled = false;
			}
		}
		mixer->pixel_clock_enabled = true;

		if (mixer->vtc_bridge) {
			drm_display_mode_to_videomode(mode, &vm);
			xlnx_bridge_set_timing(mixer->vtc_bridge, &vm);
			xlnx_bridge_enable(mixer->vtc_bridge);
		}

		xlnx_mix_dpms(mixer, dpms);
		xlnx_mix_plane_dpms(base_crtc->primary, dpms);
		break;
	default:
		xlnx_mix_plane_dpms(base_crtc->primary, dpms);
		xlnx_mix_dpms(mixer, dpms);
		xlnx_bridge_disable(mixer->vtc_bridge);
		if (mixer->pixel_clock_enabled) {
			clk_disable_unprepare(mixer->pixel_clock);
			mixer->pixel_clock_enabled = false;
		}
		break;
	}
}

static void xlnx_mix_set_intr_handler(struct xlnx_mix *mixer,
				      void (*intr_handler_fn)(void *),
				       void *data)
{
	mixer->mixer_hw.intrpt_handler_fn = intr_handler_fn;
	mixer->mixer_hw.intrpt_data = data;
}

static void xlnx_mix_crtc_vblank_handler(void *data)
{
	struct drm_crtc *base_crtc = data;
	struct xlnx_crtc *crtc = to_xlnx_crtc(base_crtc);
	struct xlnx_mix *mixer = to_xlnx_mixer(crtc);
	struct drm_device *drm = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	drm_crtc_handle_vblank(base_crtc);
	/* Finish page flip */
	spin_lock_irqsave(&drm->event_lock, flags);
	event = mixer->event;
	mixer->event = NULL;
	if (event) {
		drm_crtc_send_vblank_event(base_crtc, event);
		drm_crtc_vblank_put(base_crtc);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

static int xlnx_mix_crtc_enable_vblank(struct drm_crtc *base_crtc)
{
	struct xlnx_crtc *crtc = to_xlnx_crtc(base_crtc);
	struct xlnx_mix *mixer = to_xlnx_mixer(crtc);

	xlnx_mix_set_intr_handler(mixer, xlnx_mix_crtc_vblank_handler,
				  base_crtc);
	return 0;
}

static void xlnx_mix_crtc_disable_vblank(struct drm_crtc *base_crtc)
{
	struct xlnx_crtc *crtc = to_xlnx_crtc(base_crtc);
	struct xlnx_mix *mixer = to_xlnx_mixer(crtc);

	mixer->mixer_hw.intrpt_handler_fn = NULL;
	mixer->mixer_hw.intrpt_data = NULL;
}

static void xlnx_mix_crtc_destroy(struct drm_crtc *base_crtc)
{
	struct xlnx_crtc *crtc = to_xlnx_crtc(base_crtc);
	struct xlnx_mix *mixer = to_xlnx_mixer(crtc);

	/* make sure crtc is off */
	mixer->alpha_prop = NULL;
	mixer->scale_prop = NULL;
	mixer->bg_color = NULL;
	xlnx_mix_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);

	if (mixer->pixel_clock_enabled) {
		clk_disable_unprepare(mixer->pixel_clock);
		mixer->pixel_clock_enabled = false;
	}
	drm_crtc_cleanup(base_crtc);
}

static int
xlnx_mix_disp_crtc_atomic_set_property(struct drm_crtc *crtc,
				       struct drm_crtc_state *state,
				     struct drm_property *property,
				     uint64_t val)
{
	return 0;
}

static int
xlnx_mix_disp_crtc_atomic_get_property(struct drm_crtc *crtc,
				       const struct drm_crtc_state *state,
				     struct drm_property *property,
				     uint64_t *val)
{
	return 0;
}

static struct drm_crtc_funcs xlnx_mix_crtc_funcs = {
	.destroy		= xlnx_mix_crtc_destroy,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_set_property	= xlnx_mix_disp_crtc_atomic_set_property,
	.atomic_get_property	= xlnx_mix_disp_crtc_atomic_get_property,
	.reset			= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= xlnx_mix_crtc_enable_vblank,
	.disable_vblank		= xlnx_mix_crtc_disable_vblank,
};

static void
xlnx_mix_crtc_atomic_enable(struct drm_crtc *crtc,
			    struct drm_crtc_state *old_crtc_state)
{
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	int vrefresh;

	xlnx_mix_crtc_dpms(crtc, DRM_MODE_DPMS_ON);

	/* Delay of 3 vblank interval for timing gen to be stable */
	vrefresh = ((adjusted_mode->clock * 1000) /
		    (adjusted_mode->vtotal * adjusted_mode->htotal));
	msleep(3 * 1000 / vrefresh);
}

/**
 * xlnx_mix_clear_event - Clear any event if pending
 * @crtc: DRM crtc object
 *
 */
static void xlnx_mix_clear_event(struct drm_crtc *crtc)
{
	if (crtc->state->event) {
		complete_all(crtc->state->event->base.completion);
		crtc->state->event = NULL;
	}
}

static void
xlnx_mix_crtc_atomic_disable(struct drm_crtc *crtc,
			     struct drm_crtc_state *old_crtc_state)
{
	xlnx_mix_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
	xlnx_mix_clear_event(crtc);
	drm_crtc_vblank_off(crtc);
}

static void xlnx_mix_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
}

static int xlnx_mix_crtc_atomic_check(struct drm_crtc *crtc,
				      struct drm_crtc_state *state)
{
	return drm_atomic_add_affected_planes(state->state, crtc);
}

static void
xlnx_mix_crtc_atomic_begin(struct drm_crtc *crtc,
			   struct drm_crtc_state *old_crtc_state)
{
	drm_crtc_vblank_on(crtc);
	/* Don't rely on vblank when disabling crtc */
	if (crtc->state->event) {
		struct xlnx_crtc *xcrtc = to_xlnx_crtc(crtc);
		struct xlnx_mix *mixer = to_xlnx_mixer(xcrtc);

		/* Consume the flip_done event from atomic helper */
		crtc->state->event->pipe = drm_crtc_index(crtc);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		mixer->event = crtc->state->event;
		crtc->state->event = NULL;
	}
}

static struct drm_crtc_helper_funcs xlnx_mix_crtc_helper_funcs = {
	.atomic_enable	= xlnx_mix_crtc_atomic_enable,
	.atomic_disable	= xlnx_mix_crtc_atomic_disable,
	.mode_set_nofb	= xlnx_mix_crtc_mode_set_nofb,
	.atomic_check	= xlnx_mix_crtc_atomic_check,
	.atomic_begin	= xlnx_mix_crtc_atomic_begin,
};

/**
 * xlnx_mix_crtc_create - create crtc for mixer
 * @mixer: xilinx video mixer object
 *
 * Return:
 * Zero on success, error on failure
 *
 */
static int xlnx_mix_crtc_create(struct xlnx_mix *mixer)
{
	struct xlnx_crtc *crtc;
	int ret, i;

	crtc = &mixer->crtc;

	for (i = 0; i < mixer->num_planes; i++)
		xlnx_mix_attach_plane_prop(&mixer->planes[i]);
	mixer->pixel_clock = devm_clk_get(mixer->drm->dev, NULL);
	if (IS_ERR(mixer->pixel_clock)) {
		DRM_DEBUG_KMS("failed to get pixel clock\n");
		mixer->pixel_clock = NULL;
	}
	ret = clk_prepare_enable(mixer->pixel_clock);
	if (ret) {
		DRM_ERROR("failed to enable a pixel clock\n");
		mixer->pixel_clock_enabled = false;
		goto err_plane;
	}
	mixer->pixel_clock_enabled = true;
	/* initialize drm crtc */
	ret = drm_crtc_init_with_planes(mixer->drm, &crtc->crtc,
					&mixer->drm_primary_layer->base,
					&mixer->hw_logo_layer->base,
					&xlnx_mix_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize mixer crtc\n");
		goto err_pixel_clk;
	}
	drm_crtc_helper_add(&crtc->crtc, &xlnx_mix_crtc_helper_funcs);
	crtc->get_max_width = &xlnx_mix_crtc_get_max_width;
	crtc->get_max_height = &xlnx_mix_crtc_get_max_height;
	crtc->get_align = &xlnx_mix_crtc_get_align;
	crtc->get_format = &xlnx_mix_crtc_get_format;
	crtc->get_cursor_height = &xlnx_mix_crtc_get_max_cursor_height;
	crtc->get_cursor_width = &xlnx_mix_crtc_get_max_cursor_width;
	xlnx_crtc_register(mixer->drm, crtc);

	return 0;

err_pixel_clk:
	if (mixer->pixel_clock_enabled) {
		clk_disable_unprepare(mixer->pixel_clock);
		mixer->pixel_clock_enabled = false;
	}
err_plane:
	return ret;
}

/**
 * xlnx_mix_init - Establishes a default power-on state for the mixer IP
 * core
 * @mixer: instance of IP core to initialize to a default state
 *
 * Background layer initialized to maximum height and width settings based on
 * device tree properties and all overlay layers set to minimum height and width
 * sizes and positioned to 0,0 in the crtc.   All layers are inactive (resulting
 * in video output being generated by the background color generator).
 * Interrupts are disabled and the IP is started (with auto-restart enabled).
 */
static void xlnx_mix_init(struct xlnx_mix_hw *mixer)
{
	u32 i;
	u32 bg_bpc = mixer->bg_layer_bpc;
	u64 rgb_bg_clr = (0xFFFF >> (XVMIX_MAX_BPC - bg_bpc)) << (bg_bpc * 2);
	enum xlnx_mix_layer_id layer_id;
	struct xlnx_mix_layer_data *layer_data;

	layer_data = xlnx_mix_get_layer_data(mixer, XVMIX_LAYER_MASTER);
	xlnx_mix_layer_disable(mixer, mixer->max_layers);
	xlnx_mix_set_active_area(mixer, layer_data->hw_config.max_width,
				 layer_data->hw_config.max_height);
	/* default to blue */
	xlnx_mix_set_bkg_col(mixer, rgb_bg_clr);

	for (i = 0; i < mixer->layer_cnt; i++) {
		layer_id = mixer->layer_data[i].id;
		layer_data = &mixer->layer_data[i];
		if (layer_id == XVMIX_LAYER_MASTER)
			continue;
		xlnx_mix_set_layer_window(mixer, layer_id, 0, 0,
					  XVMIX_LAYER_WIDTH_MIN,
					  XVMIX_LAYER_HEIGHT_MIN, 0);
		if (layer_data->hw_config.can_scale)
			xlnx_mix_set_layer_scaling(mixer, layer_id, 0);
		if (layer_data->hw_config.can_alpha)
			xlnx_mix_set_layer_alpha(mixer, layer_id,
						 XVMIX_ALPHA_MAX);
	}
	xlnx_mix_intrpt_enable_done(mixer);
}

static int xlnx_mix_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct xlnx_mix *mixer = dev_get_drvdata(dev);
	struct drm_device *drm = data;
	u32 ret;

	mixer->drm = drm;
	ret = xlnx_mix_plane_create(dev, mixer);
	if (ret)
		return ret;
	ret = xlnx_mix_crtc_create(mixer);
	if (ret)
		return ret;
	xlnx_mix_init(&mixer->mixer_hw);

	return ret;
}

static void xlnx_mix_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct xlnx_mix *mixer = dev_get_drvdata(dev);

	dev_set_drvdata(dev, NULL);
	xlnx_mix_intrpt_disable(&mixer->mixer_hw);
	xlnx_crtc_unregister(mixer->drm, &mixer->crtc);
}

static const struct component_ops xlnx_mix_component_ops = {
	.bind	= xlnx_mix_bind,
	.unbind	= xlnx_mix_unbind,
};

static int xlnx_mix_probe(struct platform_device *pdev)
{
	struct xlnx_mix *mixer;
	int ret;

	mixer = devm_kzalloc(&pdev->dev, sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return -ENOMEM;

	/* Sub-driver will access mixer from drvdata */
	platform_set_drvdata(pdev, mixer);
	ret = xlnx_mix_dt_parse(&pdev->dev, mixer);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to probe mixer\n");
		return ret;
	}

	ret = component_add(&pdev->dev, &xlnx_mix_component_ops);
	if (ret)
		goto err;

	mixer->master = xlnx_drm_pipeline_init(pdev);
	if (IS_ERR(mixer->master)) {
		dev_err(&pdev->dev, "Failed to initialize the drm pipeline\n");
		goto err_component;
	}

	dev_info(&pdev->dev, "Xilinx Mixer driver probed success\n");
	return ret;

err_component:
	component_del(&pdev->dev, &xlnx_mix_component_ops);
err:
	return ret;
}

static int xlnx_mix_remove(struct platform_device *pdev)
{
	struct xlnx_mix *mixer = platform_get_drvdata(pdev);

	if (mixer->vtc_bridge)
		of_xlnx_bridge_put(mixer->vtc_bridge);
	xlnx_drm_pipeline_exit(mixer->master);
	component_del(&pdev->dev, &xlnx_mix_component_ops);
	return 0;
}

/*
 * TODO:
 * In Mixer IP core version 4.0, layer enable bits and logo layer offsets
 * have been changed. To provide backward compatibility number of max layers
 * field has been taken to differentiate IP versions.
 * This logic will have to be changed properly using the IP core version.
 */

static const struct of_device_id xlnx_mix_of_match[] = {
	{ .compatible = "xlnx,mixer-3.0", },
	{ .compatible = "xlnx,mixer-4.0", },
	{ .compatible = "xlnx,mixer-5.0", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xlnx_mix_of_match);

static struct platform_driver xlnx_mix_driver = {
	.probe			= xlnx_mix_probe,
	.remove			= xlnx_mix_remove,
	.driver			= {
		.name		= "xlnx-mixer",
		.of_match_table	= xlnx_mix_of_match,
	},
};

module_platform_driver(xlnx_mix_driver);

MODULE_AUTHOR("Saurabh Sengar");
MODULE_DESCRIPTION("Xilinx Mixer Driver");
MODULE_LICENSE("GPL v2");

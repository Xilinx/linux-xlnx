/*
 * (C) Copyright 2016 - 2017, Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * Defines all of the enums and data structures necessary to utilize the
 * mixer hardware accessor functions.
 */
#ifndef __XV_VIDEO_MIXER_DATA__
#define __XV_VIDEO_MIXER_DATA__

#include <linux/types.h>

#include "crtc/mixer/hw/xilinx_mixer_regs.h"

/*********************** Inline Functions/Macros *****************************/
#define mixer_layer_x_pos(l)	    ((l)->layer_regs.x_pos)
#define mixer_layer_y_pos(l)	    ((l)->layer_regs.y_pos)
#define mixer_layer_width(l)	    ((l)->layer_regs.width)
#define mixer_layer_height(l)       ((l)->layer_regs.height)
#define mixer_layer_active(l)       ((l)->layer_regs.is_active)
#define mixer_layer_can_scale(l)    ((l)->hw_config.can_scale)
#define mixer_layer_can_alpha(l)    ((l)->hw_config.can_alpha)
#define mixer_layer_is_streaming(l) ((l)->hw_config.is_streaming)
#define mixer_layer_fmt(l)          ((l)->hw_config.vid_fmt)

#define mixer_video_fmt(m)                               \
	((xilinx_mixer_get_layer_data(m, XVMIX_LAYER_MASTER))->\
	hw_config.vid_fmt)

/************************** Enums ********************************************/
/*
 * enum xv_mixer_layer_id - Describes the layer by index to be acted upon
 */
enum xv_mixer_layer_id {
	XVMIX_LAYER_MASTER = 0,
	XVMIX_LAYER_1,
	XVMIX_LAYER_2,
	XVMIX_LAYER_3,
	XVMIX_LAYER_4,
	XVMIX_LAYER_5,
	XVMIX_LAYER_6,
	XVMIX_LAYER_7,
	XVMIX_LAYER_LOGO,
	XVMIX_LAYER_ALL,
	XVMIX_LAYER_LAST
};

/*
 * enum xv_mixer_scale_factor - Legal scaling factors for layers which support
 * scaling.
 */
enum xv_mixer_scale_factor {
	XVMIX_SCALE_FACTOR_1X = 0,
	XVMIX_SCALE_FACTOR_2X,
	XVMIX_SCALE_FACTOR_4X,
	XVMIX_SCALE_FACTOR_NOT_SUPPORTED
};

/*
 * enum xv_comm_colordepth - Bits per color component.
 */
enum xv_comm_colordepth {
	XVIDC_BPC_6 = 6,
	XVIDC_BPC_8 = 8,
	XVIDC_BPC_10 = 10,
	XVIDC_BPC_12 = 12,
	XVIDC_BPC_14 = 14,
	XVIDC_BPC_16 = 16,
	XVIDC_BPC_NUM_SUPPORTED = 6,
	XVIDC_BPC_UNKNOWN
};

/*
 * enum xv_comm_color_fmt_id - Color space format.
 */
enum xv_comm_color_fmt_id {
	XVIDC_CSF_RGB = 0,
	XVIDC_CSF_BGR,
	XVIDC_CSF_BGR565,
	XVIDC_CSF_RGBA8,
	XVIDC_CSF_ABGR8,
	XVIDC_CSF_ARGB8,
	XVIDC_CSF_XBGR8,
	XVIDC_CSF_YCBCR_444,
	XVIDC_CSF_XYCBCR_444,
	XVIDC_CSF_YCBCR_422,
	XVIDC_CSF_AYCBCR_444,
	XVIDC_CSF_YCRCB_420,
	XVIDC_CSF_YCRCB8,
	XVIDC_CSF_Y_CBCR8_420,
	XVIDC_CSF_Y_CBCR8,
	XVIDC_CSF_YONLY,
	XVIDC_CSF_NUM_SUPPORTED,
	XVIDC_CSF_UNKNOWN
};

/************************** Data Model ***************************************/

/**
 * struct xv_mixer_layer_data - Describes the hardware configuration of a given
 * mixer layer
 * @hw_config: struct specifying the IP hardware constraints for this layer
 * @vid_fmt: Current video format for this layer
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
 *  (e.g. 0 = master, 1-7 = overlay).
 *
 * All mixer layers are reprsented by an instance of this struct:
 * output streaming, overlay, logo.
 * Current layer-specific register state is stored in the layer_regs struct.
 * The hardware configuration is stored in struct hw_config.
 *
 * Note:
 * Some properties of the logo layer are unique and not described in this
 * struct.  Those properites are part of the xv_mixer struct as global
 * properties.
 */
struct xv_mixer_layer_data {
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
		u64     buff_addr; /* JPM TODO mem layer not implemented yet */
		u32     x_pos;
		u32     y_pos;
		u32     width;
		u32     height;
		u32     stride; /* JPM TODO mem layer not implemented yet */
		u32     alpha;
		bool	is_active;
		enum xv_mixer_scale_factor scale_fact;
	} layer_regs;

	enum xv_mixer_layer_id id;
};

/**
 * struct xv_mixer - Describes a mixer IP block instance within the design
 * @dn: of device node reference for the Mixer
 * @reg_base_addr: Base physical address of Mixer IP in memory map
 * @logo_layer_enabled: Indicates logo layer is enabled in hardware
 * @logo_color_key_enabled: Not supported/used at this time
 * @logo_pixel_alpha_enabled: Indicates that per-pixel alpha supported for logo
 *  layer
 * @intrpts_enabled: Flag indicating interrupt generation is enabled/disabled
 * @max_layer_width: Max possible width for any layer on this Mixer
 * @max_layer_height: Max possible height for any layer on this Mixer
 * @max_logo_layer_width: Min possible width for any layer on this Mixer
 * @max_logo_layer_height: Min possible height for any layer on this Mixer
 * @max_layers: Max number of layers (excl: logo)
 * @bg_layer_bpc: Bits per component for the background streaming layer
 * @ppc: Pixels per component
 * @irq: Interrupt request number assigned
 * @bg_color: Current RGB color value for internal background color generator
 * @layer_data: Array of layer data
 * @layer_cnt: Layer data array count
 * @logo_color_key: not supported/used at this time
 * @reset_gpio: GPIO line used to reset IP between modesetting operations
 * @intrpt_handler_fn: Interrupt handler function called when frame is completed
 * @intrpt_data: Data pointer passed to interrupt handler
 * @private: Private data for use by higher level drivers if needed
 *
 * Used as the primary data structure for many L2 driver functions. Logo layer
 * data, if enabled within the IP, is described in this structure.  All other
 * layers are described by an instance of xv_mixer_layer_data referenced by this
 * struct.
 *
 */
struct xv_mixer {
	struct device_node  *dn;
	void __iomem        *reg_base_addr;
	bool                logo_layer_enabled;
	bool                logo_color_key_enabled;
	bool                logo_pixel_alpha_enabled;
	bool                intrpts_enabled;
	u32                 max_layer_width;
	u32                 max_layer_height;
	u32                 max_logo_layer_width;
	u32                 max_logo_layer_height;
	u32                 max_layers;
	u32                 bg_layer_bpc;
	u32                 ppc;
	int		    irq;
	u64		    bg_color;

	struct xv_mixer_layer_data *layer_data;
	u32 layer_cnt;

	/* JPM TODO color key feature not yet implemented */
	struct {
		u8 rgb_min[3];
		u8 rgb_max[3];
	} logo_color_key;

	struct gpio_desc *reset_gpio;

	void (*intrpt_handler_fn)(void *);
	void *intrpt_data;

	void *private;
};

/************************** Function Prototypes ******************************/

/**
 * xilinx_mixer_set_layer_window - Sets the position of an overlay layer over
 * the background layer (layer 0)
 * @mixer: Specific mixer object instance controlling the video
 * @layer_id: Logical layer id (1-7) to be positioned
 * @x_pos: new: Column to start display of overlay layer
 * @y_pos: new: Row to start display of overlay layer
 * @win_width: Number of active columns to dislay for overlay layer
 * @win_height: Number of active columns to display for overlay layer
 * @stride_bytes: Width in bytes of overaly memory buffer (memory layer only)
 *
 * Return:
 * Zero on success, -EINVAL if position is invalid or -ENODEV if layer
 * data is not found
 *
 * Note:
 * Applicable only for layers 1-7 or the logo layer
 */
int
xilinx_mixer_set_layer_window(struct xv_mixer *mixer,
			      enum xv_mixer_layer_id layer_id,
			      u32 x_pos, u32 y_pos,
			      u32 win_width, u32 win_height,
			      u32 stride_bytes);

/**
 * xilinx_mixer_set_active_area - Sets the number of active horizontal and
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
int
xilinx_mixer_set_active_area(struct xv_mixer *mixer,
			     u32 hactive, u32 vactive);

/**
 * xilinx_mixer_set_bkg_col - Set the color to be output as background color
 * when background stream layer
 * is disabled
 *
 * @mixer: Mixer instance to program with new background color
 * @rgb_value: RGB encoded as 32-bit integer in little-endian format
 */
void
xilinx_mixer_set_bkg_col(struct xv_mixer *mixer, u64 rgb_value);

/**
 *  xilinx_mixer_layer_enable - Enables (permit video output) for layer in mixer
 * @mixer: Mixer instance in which to enable a video layer
 * @layer_id: Logical id (e.g. 8 = logo layer) to enable
 */
void
xilinx_mixer_layer_enable(struct xv_mixer *mixer,
			  enum xv_mixer_layer_id layer_id);

/**
 * xilinx_mixer_layer_disable - Disables the layer denoted by layer_id in the
 * IP core.
 * @mixer:  Mixer for which the layer will be disabled
 * @layer_id: Logical id of the layer to be disabled (0-8)
 *
 * Note:
 * Layer 0 will indicate the background layer and layer 8 the logo
 * layer. Passing in the enum value XVMIX_LAYER_ALL will disable all
 * layers.
 */
void
xilinx_mixer_layer_disable(struct xv_mixer *mixer,
			   enum xv_mixer_layer_id layer_id);

static inline uint32_t
xilinx_mixer_get_intr_status(struct xv_mixer *mixer)
{
	return (reg_readl(mixer->reg_base_addr, XV_MIX_CTRL_ADDR_ISR) &
			XVMIX_IRQ_DONE_MASK);
}

static inline void
xilinx_mixer_clear_intr_status(struct xv_mixer *mixer, uint32_t intr)
{
	reg_writel(mixer->reg_base_addr, XV_MIX_CTRL_ADDR_ISR, intr);
}

static inline bool
xilinx_mixer_g_intrpt_enabled(struct xv_mixer *mixer)
{
	return (reg_readl(mixer->reg_base_addr, XV_MIX_CTRL_ADDR_GIE) &
			XVMIX_IRQ_DONE_MASK);
}

/**
 * xilinx_mixer_intrpt_enable - Enables interrupts in the mixer IP core
 * @mixer: Instance in which to enable interrupts
 */
void
xilinx_mixer_intrpt_enable(struct xv_mixer *mixer);

/**
 * xilinx_mixer_intrpt_disable - Disables all interrupts in the mixer IP core
 * @mixer: Instance in which to disable interrupts
 */
void
xilinx_mixer_intrpt_disable(struct xv_mixer *mixer);

/**
 * xilinx_mixer_get_layer_scaling - Return the current degree of scaling for
 * the layer specified
 * @mixer: Mixer instance for which layer information is requested
 * @layer_id: Logical id of layer for which scale setting is requested
 *
 * Return:
 * current layer scaling setting (defaults to 0 if scaling no enabled)
 *			0 = no scaling (or scaling not permitted)
 *			1 = 2x scaling (both horiz. and vert.)
 *			2 = 4x scaling (both horiz. and vert.)
 *
 * Note:
 * Only applicable to layers 1-7 and logo layer
 */
int
xilinx_mixer_get_layer_scaling(struct xv_mixer *mixer,
			       enum xv_mixer_layer_id layer_id);

/**
 * xilinx_mixer_get_layer_data - Retrieve current hardware and register
 * values for a logical video layer
 * @mixer: Mixer instance to interrogate
 * @layer_id: Logical id of layer for which data is requested
 *
 * Return
 * Structure containing layer-specific data; NULL upon failure
 */
struct xv_mixer_layer_data*
xilinx_mixer_get_layer_data(struct xv_mixer *mixer,
			    enum xv_mixer_layer_id layer_id);

/**
 * xilinx_mixer_set_layer_scaling - Sets the scaling factor for the specified
 * video layer
 * @mixer: Instance of mixer to be subject of scaling request
 * @layer_id: Logical id of video layer subject to new scale setting
 * @scale: scale Factor (1x, 2x or 4x) for horiz. and vert. dimensions
 *
 * Return:
 * Zero on success, -EINVAL on failure to set scale for layer (likely
 * returned if resulting size of layer exceeds dimensions of active
 * display area
 *
 * Note:
 * Not applicable to background stream layer (layer 0)
 */
int
xilinx_mixer_set_layer_scaling(struct xv_mixer *mixer,
			       enum xv_mixer_layer_id layer_id,
			       enum xv_mixer_scale_factor scale);

/**
 * xilinx_mixer_set_layer_alpha - Set the layer global transparency for a
 * video overlay
 * @mixer: Instance of mixer controlling layer to modify
 * @layer_id: Logical id of video overlay to adjust alpha setting
 * @alpha: Desired alpha setting (0-255) for layer specified
 *            255 = completely opaque
 *            0 = fully transparent
 *
 * Return:
 * Zero on success, -EINVAL on failure
 *
 * Note:
 * Not applicable to background streaming layer
 */
int
xilinx_mixer_set_layer_alpha(struct xv_mixer *mixer,
			     enum xv_mixer_layer_id layer_id,
			     u32 alpha);

/**
 * xilinx_mixer_start - Start the mixer core video generator
 * @mixer: Mixer core instance for which to begin video output
 */
void
xilinx_mixer_start(struct xv_mixer *mixer);

/**
 * xilinx_mixer_stop - Stop the mixer core video generator
 * @mixer: Mixer core instance for which to stop video output
 */
void
xilinx_mixer_stop(struct xv_mixer *mixer);

/**
 * xilinx_mixer_init - Establishes a default power-on state for the mixer IP
 * core
 * @mixer: instance of IP core to initialize to a default state
 *
 * Background layer initialized to maximum height and width settings based on
 * device tree properties and all overlay layers set to minimum height and width
 * sizes and positioned to 0,0 in the crtc.   All layers are inactive (resulting
 * in video output being generated by the background color generator).
 * Interrupts are disabled and the IP is started (with auto-restart enabled).
 */
void
xilinx_mixer_init(struct xv_mixer *mixer);

/**
 * xilinx_mixer_logo_load - Loads mixer's internal bram with planar R, G, B
 * and A data
 * @mixer: Mixer instance to act upon
 * @logo_w: Width of logo in pixels
 * @logo_h: Height of logo in pixels
 * @r_buf: Pointer to byte buffer array of R data values
 * @g_buf: Pointer to byte buffer array of G data values
 * @b_buf: Pointer to byte buffer array of B data values
 * @a_buf: Pointer to byte buffer array of A data values
 *
 * Return:
 * Zero on success, -ENODEV if logo layer not enabled; -EINVAL otherwise
 */
int
xilinx_mixer_logo_load(struct xv_mixer *mixer, u32 logo_w, u32 logo_h,
		       u8 *r_buf, u8 *g_buf, u8 *b_buf, u8 *a_buf);

int
xilinx_mixer_set_layer_buff_addr(struct xv_mixer *mixer,
				 enum xv_mixer_layer_id layer_id,
				 u32 buff_addr);

int
xilinx_mixer_get_layer_buff_addr(struct xv_mixer *mixer,
				 enum xv_mixer_layer_id layer_id,
				u32 *buff_addr);

#endif /* __XV_VIDEO_MIXER_DATA__ */

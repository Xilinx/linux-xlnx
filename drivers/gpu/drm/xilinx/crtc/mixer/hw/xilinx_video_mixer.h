/******************************************************************************
 *
 * Copyright (C) 2016 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Use of the Software is limited solely to applications:
 * (a) running on a Xilinx device, or
 * (b) that interact with a Xilinx device through a bus or interconnect.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 *
******************************************************************************/

/**
* @file xilinx_video_mixer.h
* Defines all of the enums and data structures necessary to utilize the
* mixer hardware accessor functions.  
*/
#ifndef __XV_VIDEO_MIXER__
#define __XV_VIDEO_MIXER__

#include <linux/types.h>
#include "crtc/mixer/hw/xilinx_mixer_hw.h"

/************************** Inline Functions *********************************/
#define mixer_layer_x_pos(l)	    l->layer_regs.x_pos
#define mixer_layer_y_pos(l)	    l->layer_regs.y_pos
#define mixer_layer_width(l)	    l->layer_regs.width
#define mixer_layer_height(l)       l->layer_regs.height
#define mixer_layer_active(l)       l->layer_regs.is_active
#define mixer_layer_can_scale(l)    l->hw_config.can_scale
#define mixer_layer_can_alpha(l)    l->hw_config.can_alpha
#define mixer_layer_is_streaming(l) l->hw_config.is_streaming
#define mixer_layer_fmt(l)          l->hw_config.vid_fmt

#define mixer_video_fmt(m)                               \
    (xilinx_mixer_get_layer_data(m,XVMIX_LAYER_MASTER))->\
    hw_config.vid_fmt

/************************** Enums ********************************************/
/**
 * @enum xv_mixer_layer_id
 * typedef used to describe the layer by index to be acted upon
*/
typedef enum {
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
} xv_mixer_layer_id;


/**
 * @enum xv_mixer_bkg_color_id
 * Used to select a set of values used to program mixer internal background 
 * color generator to generate the selected color
*/
typedef enum {
	XVMIX_BKGND_BLACK = 0,
	XVMIX_BKGND_WHITE,
	XVMIX_BKGND_RED,
	XVMIX_BKGND_GREEN,
	XVMIX_BKGND_BLUE,
	XVMIX_BKGND_LAST
} xv_mixer_bkg_color_id;

/**
 * @enum xv_mixer_scale_factor
 * Selection of legal scaling factors for layers which support scaling.
*/
typedef enum
{
	XVMIX_SCALE_FACTOR_1X = 0,
	XVMIX_SCALE_FACTOR_2X,
	XVMIX_SCALE_FACTOR_4X,
	XVMIX_SCALE_FACTOR_NOT_SUPPORTED
} xv_mixer_scale_factor;


/**
 * @enum xv_comm_colordepth
 * Color depth - bits per color component.
 */
typedef enum {
	XVIDC_BPC_6 = 6,
	XVIDC_BPC_8 = 8,
	XVIDC_BPC_10 = 10,
	XVIDC_BPC_12 = 12,
	XVIDC_BPC_14 = 14,
	XVIDC_BPC_16 = 16,
	XVIDC_BPC_NUM_SUPPORTED = 6,
	XVIDC_BPC_UNKNOWN
} xv_comm_colordepth;


/**
 * @enum xv_comm_color_fmt_id
 * Color space format.
 */
typedef enum {
	XVIDC_CSF_RGB = 0,
	XVIDC_CSF_YCRCB_444,
	XVIDC_CSF_YCRCB_422,
	XVIDC_CSF_YCRCB_420,
	XVIDC_CSF_YONLY,
	XVIDC_CSF_NUM_SUPPORTED,
	XVIDC_CSF_UNKNOWN
} xv_comm_color_fmt_id;

/************************** Data Model ***************************************/

/**
 * @struct xv_mixer_layer_data
 * Describes the hardware configuration of a give mixer layer (background or
 * overaly) in the struct hw_config.  Current layer-specific register state
 * is stored in the layer_regs struct.  The logical layer id identifies which
 * layer this struct describes (e.g. 0 = master, 1-7 = overlay).
 *
 * @note Some properties of the logo layer are unique and not described in this
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
	} hw_config;

	struct {
		u64     buff_addr; /* JPM TODO not implemented yet */
		u32     x_pos;
		u32     y_pos;
		u32     width;
		u32     height;
		u32     stride; /* JPM TODO not implemented yet */
		u32     alpha;
		bool	is_active;
		xv_mixer_scale_factor scale_fact;
	} layer_regs;

	xv_mixer_layer_id id;
};


/**
 * @struct xv_mixer
 * Describes a mixer IP block instance within the desgin.  Used as the
 * primary data structure for many L2 driver functions. Logo layer data, if
 * enabled within the IP, is described in this structure.  All other layers
 * are described by an instance of xv_mixer_layer_data referenced by this
 * struct. 
*/
struct xv_mixer {

	struct device_node  *dn;
	void __iomem        *reg_base_addr;
	bool                logo_layer_enabled;
	bool                logo_color_key_enabled;
	u32        	    max_layer_width;
	u32        	    max_layer_height;
	u32        	    max_logo_layer_width;
	u32        	    max_logo_layer_height;
	u32        	    max_layers;
	u32        	    bg_layer_bpc;
	u32        	    ppc; /* JPM not intialized in driver yet.  
				    For memory interfaces */

	xv_mixer_bkg_color_id      bg_color;

	struct xv_mixer_layer_data *layer_data; 
	u32 layer_cnt;

	struct {
		u8 rgb_min[3];
		u8 rgb_max[3];
	} logo_color_key;

	struct {
		u8 *r_buffer;
		u8 *g_buffer;
		u8 *b_buffer;
	} logo_rgb_buffers;

	struct gpio_desc *reset_gpio;

	void *		private;
};



/************************** Function Prototypes ******************************/

/**
 * Sets the position of an overlay layer over the background layer (layer 0)
 *
 * @param[in] mixer        Specific mixer object instance controlling the video
 * @param[in] layer_id     Logical layer id (1-7) to be positioned
 * @param[in] x_pos new    Column to start display of overlay layer
 * @param[in] y_pos new    Row to start display of overlay layer
 * @param[in] win_width    Number of active columns to dislay for overlay layer
 * @param[in] win_height   Number of active columns to display for overlay layer
 * @param[in] stride_bytes Width in bytes of overaly memory buffer 
 * 			  (memory layer only) 
 *                         yuv422 colorspace requires 2 bytes/pixel
 * 			  yu444 colorspace requires 4 bytes/pixel
 *			  Equation to ocmpute stride is as follows:
 *   		          stride = (win_width * (YUV422 ? 2 : 4))
 *		          Only applicable when layer is of type memory
 *
 * @return 0 on success; -1 if position is invalid or otherwise illegal
 *
 * @note Applicable only for layers 1-7 or the logo layer
*/
int 
xilinx_mixer_set_layer_window(struct xv_mixer *mixer,
                              xv_mixer_layer_id layer_id,
                              u32 x_pos, u32 y_pos,
                              u32 win_width, u32 win_height,
                              u32 stride_bytes);


/**
 * Sets the number of active horizontal and vertical scan lines for the
 * mixer background layer.  Minimum values are 64x64 with maximum values
 * determined by the IP hardware design.
 *
 * @param[in] mixer Mixer instance for which to set a new viewable area
 * @param[in] hactive Width of new background image dimension
 * @param[in] vactive Height of new background image dimension
 * 
 * @return 0 on success; -1 on failure
*/
int 
xilinx_mixer_set_active_area(struct xv_mixer *mixer,
                             u32 hactive, u32 vactive);

/**
 * Set the color to be output as background color when background stream layer
 * is disabled 
 *
 * @param[in] mixer Mixer instance to program with new background color
 * @param[in] col_id Logical id of the background color to output (see enum)
 * @param[in] bpc  Data width per color component (e.g. 8, 10, 12 or 16)
*/
void 
xilinx_mixer_set_bkg_col(struct xv_mixer *mixer,
			 xv_mixer_bkg_color_id col_id,
			 xv_comm_colordepth bpc);

/**
 *  Enables (permit video output) for layer in mixer
 *
 * @param[in] mixer Mixer instance in which to enable a video layer
 * @param[in] layer_id Logical id (e.g. 8 = logo layer) to enable
*/
void 
xilinx_mixer_layer_enable(struct xv_mixer *mixer,
                          xv_mixer_layer_id layer_id);

/**
 * Disables the layer denoted by layer_id in the IP core.
 *
 * @param[in] mixer  Mixer for which the layer will be disabled
 * @param[in] layer_id Logical id of the layer to be disabled (0-8)
 *
 * @note Layer 0 will indicate the background layer and layer 8 the logo
 * layer.  Passing in the enum value XVMIX_LAYER_ALL will disable all 
 * layers.
*/
void 
xilinx_mixer_layer_disable(struct xv_mixer *mixer,
                           xv_mixer_layer_id layer_id);

/**
 * Disables all interrupts in the mixer IP core
 *
 * @param[in] mixer instance in which to disable interrupts
*/
void 
xilinx_mixer_intrpt_disable(struct xv_mixer *mixer);

/**
 * Return the current degree of scaling for the layer specified
 * 
 * @param[in] mixer Mixer instance for which layer information is requested
 * @param[in] layer_id Logical id of layer for which scale setting is requested
 *
 * @returns current layer scaling setting (defaults to 0 if scaling no enabled)
 * 			0 = no scaling (or scaling not permitted)
 * 			1 = 2x scaling (both horiz. and vert.) 
 * 			2 = 4x scaling (both horiz. and vert.) 
 *
 * @note Only applicable to layers 1-7 and logo layer
*/
int 
xilinx_mixer_get_layer_scaling(struct xv_mixer *mixer,
                               xv_mixer_layer_id layer_id);

/**
 * Retrieve pointer to data structure containing hardware and current register
 * values for a logical video layer
 *
 * @param[in] mixer Mixer instance to interrogate
 * @param[in] layer_id Logical id of layer for which data is requested
 * 
 * @returns Structure containing layer-specific data; NULL upon failure
*/
struct xv_mixer_layer_data*
xilinx_mixer_get_layer_data(struct xv_mixer *mixer,
                            xv_mixer_layer_id);

/**
 * Sets the scaling factor for the specified video layer
 * 
 * @param[in] mixer instance of mixer to be subject of scaling request 
 * @param[in] layer_id logical id of video layer subject to new scale setting 
 * @param[in] scale scale factor (1x, 2x or 4x) for horiz. and vert. dimensions
 *
 * @return 0 on success; -1 on failure to set scale for layer
 *
 * @note Not applicable to background stream layer (layer 0)
*/
int
xilinx_mixer_set_layer_scaling(struct xv_mixer *mixer,
                              xv_mixer_layer_id layer_id,
                              xv_mixer_scale_factor scale);

/**
 * Set the layer global transparency for a video overlay 
 * 
 * @param[in] mixer instance of mixer controlling layer to modify
 * @param[in] layer_id logical id of video overlay to adjust alpha setting
 * @param[in] alpha desired alpha setting (0-255) for layer specified
 *            255 = completely opaque
 *            0 = fully transparent
 * 
 * @returns 0 on success; -1 on failure
 * 
 * @note not applicable to background streaming layer 
*/
int
xilinx_mixer_set_layer_alpha(struct xv_mixer *mixer,
			     xv_mixer_layer_id layer_id,
                             u32 alpha);

/**
 * Start the mixer core video generator
 * 
 * @param[in] mixer mixer core instance for which to begin video output 
*/
void
xilinx_mixer_start(struct xv_mixer *mixer);

/**
 * Stop the mixer core video generator
 * 
 * @param[in] mixer mixer core instance for which to stop video output
*/
void
xilinx_mixer_stop(struct xv_mixer *mixer);

/**
 * Establishes a default power-on state for the mixer IP core.  Background
 * Layer initialized to maximum height and width settings based on device
 * tree properties and all overlay layers set to minimum height and width
 * sizes and positioned to 0,0 in the crtc.   All layers are inactive (resulting
 * in video output being generated by the background color generator).
 * Interrupts are disabled and the IP is started (with auto-restart enabled).
 *
 * @param[in] mixer instance of IP core to initialize to a default state
*/
void
xilinx_mixer_init(struct xv_mixer *mixer);


int
xilinx_mixer_logo_load(struct xv_mixer *mixer, u32 logo_w, u32 logo_h,
		       u8 *r_buf, u8 *g_buf, u8 *b_buf);
#endif /* __XV_VIDEO_MIXER */

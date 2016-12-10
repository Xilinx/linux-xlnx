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

#ifndef __XLNX_DRM_MIXER__
#define __XLNX_DRM_MIXER__
#include "crtc/mixer/hw/xilinx_video_mixer.h"
#include "xilinx_drm_plane.h"

#define XLNX_MIXER	"DRM XV_MIXER: "

/*JPM for temp debug -- remove asap */
#define XLNX_MIXER_INFO(dev, fmt, args...) dev_info(dev, XLNX_MIXER fmt, ##args)

/**
 * Used to parse device tree for mixer node and initialize the mixer IP core
 * to a default state wherein a background color is generated and all layers
 * are initially disabled.  
 *
 * @param[in] dev Device member of drm device
 * @param[in] node Open firmware(of) device tree node describing the mixer IP
 *
 * @returns reference to mixer instance struct; err pointer otherwise
 *
*/ 
struct xv_mixer *
xilinx_drm_mixer_probe(struct device *dev, 
		       struct device_node *node);

/**
 * Hold the reset line for the IP core low for 300 nano seconds and then
 * brings line high to pull out of reset.  The core can then be reprogrammed with
 * new mode settings and subsequently started to begin generating video
 *
 * @param[in] mixer IP core instance to reset
 *
*/
void
xilinx_drm_mixer_reset(struct xv_mixer *mixer);

/*JPM TODO implement start with auto-restart */
void
xilinx_drm_mixer_start(struct xv_mixer *mixer);

/**
 * Internal method used to look-up color format index based on device tree
 * string.  
 * 
 * @param[in] color_fmt String value representing color format found in device
 *                      tree (e.g. "rgb", "yuv422", "yuv444")
 * @param[out] output Enum value of video format id
 *
 * @returns 0 on success; -1 if no entry was found in table
 *
 * @note Should not be used outside of DRM driver.
 */  
int 
xilinx_drm_mixer_string_to_fmt(const char *color_fmt, u32 *output);

/**
 * Internal method used to use Xilinx color id and match to DRM-based fourcc
 * color code.
 * 
 * @param[in] id Xilinx enum value for a color space type (e.g. YUV422)
 * @param[out] output DRM fourcc value for corresponding Xilinx color space id
 *
 * @returns 0 on success; -1 if no matching entry found
 *
 * @note Should not be used outside of DRM driver.
*/
int 
xilinx_drm_mixer_fmt_to_drm_fmt(xv_comm_color_fmt_id id, u32 *output);


/**
 * Change video scale factor for video plane
 *
 * @param[in] plane Drm plane object describing layer to be modified
 * @param[in] val Index of scale factor to use:
 * 		  0 = 1x
 * 		  1 = 2x
 * 		  2 = 4x 
 *
 * @returns 0 on success; -1 on failure
*/
int
xilinx_drm_mixer_set_layer_scale(struct xilinx_drm_plane *plane,
                                 uint64_t val);

/**
 * Change the transparency of an entire plane
 * 
 * @param[in] plane Video layer affected by new alpha setting
 * @param[in] val Value of transparency setting (0-255) with 255 being opaque
 * 		  0 being fully transparent
 * 
 * @returns 0 on success; -1 on failure
*/
int
xilinx_drm_mixer_set_layer_alpha(struct xilinx_drm_plane *plane,
                                 uint64_t val);

/**
 * Disables video output represented by the plane object
 * 
 * @param[in] plane Drm plane object describing video layer to disable
 *
*/
void
xilinx_drm_mixer_layer_disable(struct xilinx_drm_plane *plane);

/**
 * Enables video output represented by the plane object
 * 
 * @param[in] plane Drm plane object describing video layer to enable
 *
*/
void
xilinx_drm_mixer_layer_enable(struct xilinx_drm_plane *plane);


/**
 * Enables video output represented by the plane object
 * 
 * @param[in] plane Drm plane object describing video layer to mark
 * 		as active.  Only layers marked 'active' will be
 * 		enabled when size or scale registeres are update.
 * 		In-active layers can be updated but will not be
 * 		enabled in hardware.
*/
int
xilinx_drm_mixer_mark_layer_active(struct xilinx_drm_plane *plane);

/**
 * Enables video output represented by the plane object
 * 
 * @param[in] plane Drm plane object describing video layer to mark
 * 		as inactive.  Only layers marked 'active' will be
 * 		enabled when size or scale registeres are update.
 * 		In-active layers can be updated but will not be
 * 		enabled in hardware.
*/
int
xilinx_drm_mixer_mark_layer_inactive(struct xilinx_drm_plane *plane);

/**
 * Establishes new coordinates and dimensions for a video plane layer
 *
 * @param[in] plane Drm plane object desribing video layer to reposition
 * @param[in] crtc_x New horizontal anchor postion from which to begin rendering
 * @param[in] crtc_y New vertical anchor position from which to begin rendering
 * @param[in] width Width, in pixels, to render from stream or memory buffer
 * @param[in] height Height, in pixels, to render from stream or memory buffer
 * 
 * @returns 0 if successful; -1 if corrdinates and/or height are invalid
 *
 * @note New size and coordinates of window must fit within the currently active
 * area of the crtc (e.g. the background resolution)
*/
int
xilinx_drm_mixer_set_layer_dimensions(struct xilinx_drm_plane *plane,
                                      u32 crtc_x, u32 crtc_y,
                                      u32 width, u32 height);

/**
 * Obtains a pointer to a struct containing layer-specific data for the mixer IP
 * 
 * @param[in] mixer Instance of mixer for which to obtain layer data
 * @param[in] id logical layer id (e.g. 0=background, 1=overlay) for which to obtain
 * 	         layer information
 *
 * @returns pointer to struct xv_mixer_layer_data for layer specified by id; NULL on
 *          failure.
 *
 * @note Does not apply to logo layer.  Logo layer data is contained within the 
 *       struct xv_mixer instance.
*/
struct xv_mixer_layer_data *
xilinx_drm_mixer_get_layer(struct xv_mixer *mixer, xv_mixer_layer_id id);

#endif /* end __XLNX_DRM_MIXER__ */

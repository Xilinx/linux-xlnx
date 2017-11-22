/*
 * Xilinx DRM Mixer header
 *
 * (C) Copyright 2017, Xilinx, Inc.
 *
 *  Author: Jeffrey Mouroux <jmouroux@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __XLNX_DRM_MIXER__
#define __XLNX_DRM_MIXER__
#include "xvmixer_drm_crtc.h"
#include "xvmixer_drm_plane.h"
#include "xilinx_mixer_data.h"

#define get_mixer_max_height(m)      mixer_layer_height((m)->hw_master_layer->mixer_layer)
#define get_mixer_max_width(m)       mixer_layer_width((m)->hw_master_layer->mixer_layer)
#define get_mixer_max_logo_height(m) mixer_layer_height((m)->hw_logo_layer->mixer_layer)
#define get_mixer_max_logo_width(m)  mixer_layer_width((m)->hw_logo_layer->mixer_layer)
#define get_num_mixer_planes(m)      ((m)->mixer_hw.layer_cnt)
#define get_mixer_vid_out_fmt(m)     mixer_video_fmt(&(m)->mixer_hw)

#define to_xv_mixer_hw(p) (&((p)->mixer->mixer_hw))

#define get_xilinx_mixer_mem_align(m)  \
	sizeof((m)->mixer_hw.layer_data[0].layer_regs.buff_addr1) *\
	(m)->mixer_hw.ppc

/**
 * xilinx_drm_mixer_probe - Parse device tree and init mixer node
 * @dev: Device member of drm device
 * @crtc: Mixer hardware instance
 *
 * Initialize the mixer IP core to a default state wherein a background color
 * is generated and all layers are initially disabled.
 *
 * Returns:
 * zero if successful, error code otherwise
 *
 */
int xilinx_drm_mixer_probe(struct device *dev,
			   struct xilinx_mixer_crtc *crtc);

/**
 * xilinx_drm_mixer_set_intrpts - enable/disable hw interrupts on mixer
 * @mixer: DRM mixer instance
 * @enabled: boolean indicating whether to enable (true) or disable (false)
 *           interrupts
 *
 * Returns: none
*/
void xilinx_drm_mixer_set_intrpts(struct xilinx_drm_mixer *mixer,
				  bool enabled);

/**
 * xilinx_drm_mixer_set_plane - Implementation of DRM plane_update callback
 * @plane: Xilinx_drm_plane object containing references to
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
 * Returns:
 * Zero on success, non-zero linux error code otherwise.
 */
int xilinx_drm_mixer_set_plane(struct xilinx_drm_plane *plane,
			       struct drm_framebuffer *fb,
			       int crtc_x, int crtc_y,
			       u32 src_x, u32 src_y,
			       u32 src_w, u32 src_h);

/**
 * xilinx_drm_create_mixer_plane_properties - Creates Mixer-specific drm
 * property objects
 * @mixer: Drm mixer object
 */
void xilinx_drm_create_mixer_plane_properties(struct xilinx_drm_mixer *mixer);

/**
 * xilinx_drm_mixer_set_plane_property - Sets the current value for a
 * particular plane property in the corresponding mixer layer hardware
 * @plane: Xilinx drm plane object containing references to the mixer
 * @property: Drm property passed in by user space for update
 * @value: New value used to set mixer layer hardware for register
 *  mapped to the drm property
 *
 * Returns:
 * Zero on success, EINVAL otherwise
 */
int xilinx_drm_mixer_set_plane_property(struct xilinx_drm_plane *plane,
					struct drm_property *property,
					uint64_t value);

/**
 * xilinx_drm_mixer_attach_plane_prop - Attach mixer-specific drm property to
 * the given plane
 * @plane: Xilinx drm plane object to inspect and attach appropriate
 *  properties to
 *
 * The linked mixer layer will be inspected to see what capabilities it offers
 * (e.g. global layer alpha; scaling) and drm property objects that indicate
 * those capabilities will then be attached and initialized to default values.
 */
void xilinx_drm_mixer_attach_plane_prop(struct xilinx_drm_plane *plane);

/**
 * xilinx_drm_mixer_reset - Used to reset Mixer between mode changes
 * @mixer: IP core instance to reset

 * Hold the reset line for the IP core low for 1 micro second and then
 * brings line high to pull out of reset.  The core can then be reprogrammed
 * with new mode settings and subsequently started to begin generating video
 */
void xilinx_drm_mixer_reset(struct xilinx_drm_mixer *mixer);

/**
 * xilinx_drm_mixer_start - Start generation of video stream from mixer
 * @mixer: IP core instance to reset
 *
 * Note:
 * Sets the mixer to auto-restart so that video will be streamed
 * continuously
 */
void xilinx_drm_mixer_start(struct xv_mixer *mixer);

/**
 * xilinx_drm_mixer_string_to_fmt - Used to look-up color format index based
 * on device tree string.
 * @color_fmt: String value representing color format found in device
 *  tree (e.g. "rgb", "yuv422", "yuv444")
 * @output: Enum value of video format id
 *
 * Returns:
 * Zero on success, -EINVAL if no entry was found in table
 *
 * Note:
 * Should not be used outside of DRM driver.
 */
int xilinx_drm_mixer_string_to_fmt(const char *color_fmt, u32 *output);

/**
 * xilinx_drm_mixer_fmt_to_drm_fmt - Internal method used to use Xilinx color
 * id and match to DRM-based fourcc color code.
 * @id: Xilinx enum value for a color space type (e.g. YUV422)
 * @output: DRM fourcc value for corresponding Xilinx color space id
 *
 * Returns:
 * Zero on success, -EINVAL if no matching entry found
 *
 * Note:
 * Should not be used outside of DRM driver.
 */
int xilinx_drm_mixer_fmt_to_drm_fmt(enum xv_comm_color_fmt_id id, u32 *output);

/**
 * xilinx_drm_mixer_set_layer_scale - Change video scale factor for video plane
 * @plane: Drm plane object describing layer to be modified
 * @val: Index of scale factor to use:
 *		0 = 1x
 *		1 = 2x
 *		2 = 4x
 *
 * Returns:
 * Zero on success, either -EINVAL if scale value is illegal or
 * -ENODEV if layer does not exist (null)
 */
int xilinx_drm_mixer_set_layer_scale(struct xilinx_drm_plane *plane,
				     uint64_t val);

/**
 * xilinx_drm_mixer_set_layer_alpha - Change the transparency of an entire plane
 * @plane: Video layer affected by new alpha setting
 * @val: Value of transparency setting (0-255) with 255 being opaque
 *  0 being fully transparent
 *
 * Returns:
 * Zero on success, -EINVAL on failure
 */
int xilinx_drm_mixer_set_layer_alpha(struct xilinx_drm_plane *plane,
				     uint64_t val);

/**
 * xilinx_drm_mixer_layer_disable - Disables video output represented by the
 * plane object
 * @plane: Drm plane object describing video layer to disable
 *
 */
void xilinx_drm_mixer_layer_disable(struct xilinx_drm_plane *plane);

/**
 * xilinx_drm_mixer_layer_enable - Enables video output represented by the
 * plane object
 * @plane: Drm plane object describing video layer to enable
 *
 */
void xilinx_drm_mixer_layer_enable(struct xilinx_drm_plane *plane);

/**
 * xilinx_drm_mixer_mark_layer_active - Enables video output represented by
 * the plane object
 * @plane: Drm plane object describing video layer to mark
 *  as active.  Only layers marked 'active' will be enabled when size or scale
 *  registeres are update. In-active layers can be updated but will not be
 *  enabled in hardware.
 *
 * Returns: Zero on success, -ENODEV if mixer layer does not exist
 */
int xilinx_drm_mixer_mark_layer_active(struct xilinx_drm_plane *plane);

/**
 * xilinx_drm_mixer_mark_layer_inactive - Enables video output represented by
 * the plane object
 * @plane: Drm plane object describing video layer to mark as inactive.
 *  Only layers marked 'active' will be enabled when size or scale registeres
 *  are update. In-active layers can be updated but will not be enabled in
 *  hardware.
 *
 * Returns: Zero on success, -ENODEV if mixer layer does not exist
 */
int xilinx_drm_mixer_mark_layer_inactive(struct xilinx_drm_plane *plane);

/**
 * xilinx_drm_mixer_set_layer_dimensions - Establishes new coordinates and
 * dimensions for a video plane layer
 * @plane: Drm plane object desribing video layer to reposition
 * @crtc_x: New horizontal anchor postion from which to begin rendering
 * @crtc_y: New vertical anchor position from which to begin rendering
 * @width: Width, in pixels, to render from stream or memory buffer
 * @height: Height, in pixels, to render from stream or memory buffer
 * @stride: Width, in bytes, of a memory buffer.  Used only for
 *  memory layers.  Use 0 for streaming layers.
 *
 * Returns: 0 if successful; Either -EINVAL if coordindate data is invalid
 * or -ENODEV if layer data not present
 *
 * Note:
 * New size and coordinates of window must fit within the currently active
 * area of the crtc (e.g. the background resolution)
 */
int xilinx_drm_mixer_set_layer_dimensions(struct xilinx_drm_plane *plane,
					  u32 crtc_x, u32 crtc_y,
				u32 width, u32 height, u32 stride);

/**
 * xv_mixer_layer_data - Obtains a pointer to a struct containing
 * layer-specific data for the mixer IP
 * @mixer: Instance of mixer for which to obtain layer data
 * @id: logical layer id (e.g. 0=background, 1=overlay) for which to
 *		obtain layer information
 *
 * Returns: pointer to struct xv_mixer_layer_data for layer specified by id;
 * NULL on failure.
 *
 * Note:
 * Does not apply to logo layer.  Logo layer data is contained within the
 * struct xv_mixer instance.
 */
struct
xv_mixer_layer_data * xilinx_drm_mixer_get_layer(struct xv_mixer *mixer,
						 enum xv_mixer_layer_id id);

/**
 * xilinx_drm_mixer_set_intr_handler - Sets and interrupt handler
 * @mixer: Mixer object upon which to run handler function when mixer
 *		generates an "done" interrupt for a frame
 * @intr_handler_fn: Function pointer for interrupt handler.  Typically
 *		a drm vertical blank event generation function.
 * @data: Pointer to crtc object
 *
 * Function to run when the mixer generates and ap_done interrupt event (when
 * frame processing has completed)
 */
void xilinx_drm_mixer_set_intr_handler(struct xilinx_drm_mixer *mixer,
				       void (*intr_handler_fn)(void *),
				       void *data);

/**
 * xilinx_drm_mixer_plane_dpms - Implementation of display power management
 * system call (dpms).
 * @plane: Plane/mixer layer to enable/disable (based on dpms value)
 * @dpms: Display power management state to act upon
 *
 * Designed to disable and turn off a plane and restore all attached drm
 * properities to their initial values.  Alterntively, if dpms is "on", will
 * enable a layer.
 */
void xilinx_drm_mixer_plane_dpms(struct xilinx_drm_plane *plane, int dpms);

/**
 * xilinx_drm_mixer_dpms - Implement drm dpms semantics for video mixer IP
 *
 * @mixer: Device instance representing mixer IP
 * @dpms:  Display power management state to act upon
 */
void xilinx_drm_mixer_dpms(struct xilinx_drm_mixer *mixer, int dpms);

/**
 * xilinx_drm_mixer_update_logo_img - Updates internal R, G and B buffer array
 * of mixer from kernel framebuffer
 * @plane: Xilinx drm plane object with current video format info
 * @buffer: GEM object with address references for logo image data
 * @src_w: Width of buffer to read RGB888 or RGBA8888 data
 * @src_h: Height of buffer to read RGB888 or RGBA8888 data
 *
 * Returns:
 * Zero on success, -EINVAL if format and/or size of buffer is invalid
 *
 * Note:
 * Initial call caches buffer kernel virtual address.  Subsequent calls
 * will only re-load buffer if virtual address and/or size changes.
 */
int xilinx_drm_mixer_update_logo_img(struct xilinx_drm_plane *plane,
				     struct drm_gem_cma_object *buffer,
				     u32 src_w, u32 src_h);
#endif /* end __XLNX_DRM_MIXER__ */

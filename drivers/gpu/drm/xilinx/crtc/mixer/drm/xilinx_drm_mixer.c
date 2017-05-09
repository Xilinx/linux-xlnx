/*
 * (C) Copyright 2016 - 2017, Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drm_crtc.h>
#include <drm/drm_gem_cma_helper.h>

#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "xilinx_drm_drv.h"
#include "xilinx_drm_fb.h"

#include "crtc/mixer/drm/xilinx_drm_mixer.h"
#include "crtc/mixer/hw/xilinx_mixer_regs.h"
#include "crtc/mixer/hw/xilinx_mixer_data.h"

#define COLOR_NAME_SIZE 10
#define MASTER_LAYER_IDX 0
#define LOGO_LAYER_IDX 1

struct color_fmt_tbl {
	char				name[COLOR_NAME_SIZE + 1];
	enum xv_comm_color_fmt_id	fmt_id;
	u32				drm_format;
};

/*************************** STATIC DATA  ************************************/
static const struct color_fmt_tbl color_table[] = {
	{"bgr888",    XVIDC_CSF_BGR,         DRM_FORMAT_BGR888},
	{"rgb888",    XVIDC_CSF_RGB,         DRM_FORMAT_RGB888},
	{"bgr565",    XVIDC_CSF_BGR565,      DRM_FORMAT_BGR565},
	{"yuv422",    XVIDC_CSF_YCBCR_422,   DRM_FORMAT_YUYV},
	{"ayuv",      XVIDC_CSF_AYCBCR_444,  DRM_FORMAT_AYUV},
	{"nv12",      XVIDC_CSF_Y_CBCR8_420, DRM_FORMAT_NV12},
	{"nv16",      XVIDC_CSF_Y_CBCR8,     DRM_FORMAT_NV16},
	{"rgba8888",  XVIDC_CSF_RGBA8,       DRM_FORMAT_RGBA8888},
	{"abgr8888",  XVIDC_CSF_ABGR8,       DRM_FORMAT_ABGR8888},
	{"argb8888",  XVIDC_CSF_ARGB8,       DRM_FORMAT_ARGB8888},
	{"xbgr8888",  XVIDC_CSF_XBGR8,       DRM_FORMAT_XBGR8888},
};

static const struct of_device_id xv_mixer_match[] = {
	{.compatible = "xlnx,v-mix-1.00.a"},
	{/*end of table*/},
};

/*************************** PROTOTYPES **************************************/

static int
xilinx_drm_mixer_of_init_layer_data
			       (struct device *dev,
				struct device_node *dev_node,
				char *layer_name,
				struct xv_mixer_layer_data *layer,
				u32 max_layer_width,
				struct xv_mixer_layer_data **drm_primary_layer);

static int xilinx_drm_mixer_parse_dt_logo_data(struct device_node *node,
					       struct xv_mixer *mixer_hw);

static int
xilinx_drm_mixer_parse_dt_bg_video_fmt(struct device_node *layer_node,
				       struct xv_mixer *mixer_hw);

static irqreturn_t xilinx_drm_mixer_intr_handler(int irq, void *data);

/************************* IMPLEMENTATIONS ***********************************/

struct
xilinx_drm_mixer *xilinx_drm_mixer_probe
				      (struct device *dev,
				       struct device_node *node,
				       struct xilinx_drm_plane_manager *manager)
{
	struct xilinx_drm_mixer		*mixer;
	struct xv_mixer			*mixer_hw;
	char				layer_node_name[20] = {0};
	struct xv_mixer_layer_data	*layer_data;
	const struct of_device_id	*match;
	struct resource			res;
	int				ret;
	int				layer_idx;
	int				layer_cnt;
	int				i;

	match = of_match_node(xv_mixer_match, node);

	if (!match) {
		dev_err(dev, "Failed to match device node for mixer\n");
		return ERR_PTR(-ENODEV);
	}

	mixer = devm_kzalloc(dev, sizeof(struct xilinx_drm_mixer), GFP_KERNEL);
	if (!mixer)
		return ERR_PTR(-ENOMEM);

	mixer_hw = &mixer->mixer_hw;

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "Invalid io memory address in dts for mixer\n");
		return ERR_PTR(ret);
	}

	mixer_hw->reg_base_addr = devm_ioremap_resource(dev, &res);
	if (IS_ERR(mixer_hw->reg_base_addr)) {
		dev_err(dev, "Failed to map io mem space for mixer\n");
		return ERR_CAST(mixer_hw->reg_base_addr);
	}

	ret = of_property_read_u32(node, "xlnx,num-layers",
				   &mixer_hw->max_layers);
	if (ret) {
		dev_err(dev, "No xlnx,num-layers dts prop for mixer node\n");
		return ERR_PTR(-EINVAL);
	}

	if (mixer_hw->max_layers > XVMIX_MAX_SUPPORTED_LAYERS) {
		dev_err(dev, "Num layer nodes in device tree > mixer max\n");
		return ERR_PTR(-EINVAL);
	}

	/* establish some global defaults subject to override via dts */
	mixer_hw->intrpts_enabled = false;
	mixer_hw->logo_pixel_alpha_enabled = false;
	mixer_hw->logo_layer_enabled = of_property_read_bool(node,
							  "xlnx,logo-layer");

	/* Alloc num_layers + 1 for logo layer if enabled in dt */
	layer_cnt = mixer_hw->max_layers +
			(mixer_hw->logo_layer_enabled ? 1 : 0);

	layer_data =
		devm_kzalloc(dev,
			     sizeof(struct xv_mixer_layer_data) * layer_cnt,
			     GFP_KERNEL);

	if (layer_data) {
		mixer_hw->layer_cnt = layer_cnt;
	} else {
		dev_err(dev, "Out of mem for mixer layer data\n");
		return ERR_PTR(-ENOMEM);
	}

	mixer_hw->layer_data = layer_data;

	/* establish background layer video properties from dts */
	ret = xilinx_drm_mixer_parse_dt_bg_video_fmt(node, mixer_hw);
	if (ret)
		return ERR_PTR(ret);

	/* read logo data from dts */
	ret = xilinx_drm_mixer_parse_dt_logo_data(node, mixer_hw);
	if (ret)
		return ERR_PTR(ret);

	mixer->plane_manager = manager;
	mixer->drm_primary_layer = NULL;
	mixer->hw_logo_layer = NULL;
	mixer->hw_master_layer = &mixer_hw->layer_data[MASTER_LAYER_IDX];

	if (mixer_hw->logo_layer_enabled)
		mixer->hw_logo_layer = &mixer_hw->layer_data[LOGO_LAYER_IDX];

	layer_idx = mixer_hw->logo_layer_enabled ? 2 : 1;
	for (i = 1; i <= (mixer_hw->max_layers - 1); i++, layer_idx++) {
		snprintf(layer_node_name, sizeof(layer_node_name),
			 "layer_%d", i);

		ret = xilinx_drm_mixer_of_init_layer_data
					    (dev,
					     node,
					     layer_node_name,
					     &mixer_hw->layer_data[layer_idx],
					     mixer_hw->max_layer_width,
					     &mixer->drm_primary_layer);

		if (ret)
			return ERR_PTR(ret);

		if (!mixer_hw->layer_data[layer_idx].hw_config.is_streaming &&
		    !mixer_hw->intrpts_enabled)
			mixer_hw->intrpts_enabled = true;
	}

	/* If none of the overlay layers were designated as the drm
	 * primary layer, default to the mixer's video0 layer as drm primary
	 */
	if (!mixer->drm_primary_layer)
		mixer->drm_primary_layer = mixer->hw_master_layer;

	/* request irq and obtain pixels-per-clock (ppc) property */
	if (mixer_hw->intrpts_enabled) {
		mixer_hw->irq = irq_of_parse_and_map(node, 0);

		if (mixer_hw->irq > 0) {
			ret = devm_request_irq(dev, mixer_hw->irq,
					       xilinx_drm_mixer_intr_handler,
					       IRQF_SHARED, "xilinx_mixer",
					       mixer_hw);

			if (ret) {
				dev_err(dev,
					"Failed to request irq for mixer\n");
				return ERR_PTR(ret);
			}
		}

		ret = of_property_read_u32(node, "xlnx,ppc", &mixer_hw->ppc);

		if (ret) {
			dev_err(dev, "No xlnx,ppc property for mixer dts\n");
			return ERR_PTR(ret);
		}
	}

	mixer_hw->reset_gpio = devm_gpiod_get_optional(dev,
						"xlnx,mixer-reset",
						GPIOD_OUT_LOW);
	if (IS_ERR(mixer_hw->reset_gpio)) {
		ret = PTR_ERR(mixer_hw->reset_gpio);

		if (ret == -EPROBE_DEFER) {
			dev_info(dev, "No gpio probed for mixer. Deferring\n");
			return ERR_PTR(ret);
		}

		dev_err(dev, "No reset gpio info from dts for mixer\n");
		return ERR_PTR(ret);
	}

	gpiod_set_raw_value(mixer_hw->reset_gpio, 0x1);

	if (mixer_hw->intrpts_enabled)
		xilinx_mixer_intrpt_enable(mixer_hw);
	else
		xilinx_mixer_intrpt_disable(mixer_hw);

	/* Init all layers to inactive state in software. An update_plane()
	 * call to our drm driver will change this to 'active' and permit the
	 * layer to be enabled in hardware
	 */
	for (i = 0; i < mixer_hw->layer_cnt; i++) {
		layer_data = &mixer_hw->layer_data[i];
		mixer_layer_active(layer_data) = false;
	}

	xilinx_drm_create_mixer_plane_properties(mixer);

	xilinx_mixer_init(mixer_hw);

	return mixer;
}

int xilinx_drm_mixer_set_plane(struct xilinx_drm_plane *plane,
			       struct drm_framebuffer *fb,
			       int crtc_x, int crtc_y,
			       u32 src_x, u32 src_y,
			       u32 src_w, u32 src_h)
{
	struct xv_mixer *mixer_hw;
	struct xilinx_drm_mixer *mixer;
	struct drm_gem_cma_object *buffer;
	u32 stride = fb->pitches[0];
	u32 offset = 0;
	u32 active_area_width;
	u32 active_area_height;
	enum xv_mixer_layer_id layer_id;
	int ret;

	mixer = plane->manager->mixer;
	mixer_hw = &mixer->mixer_hw;
	layer_id = plane->mixer_layer->id;
	active_area_width = mixer_layer_width(mixer->drm_primary_layer);
	active_area_height = mixer_layer_height(mixer->drm_primary_layer);

	/* compute memory data */
	buffer = xilinx_drm_fb_get_gem_obj(fb, 0);
	stride = fb->pitches[0];
	offset = src_x * drm_format_plane_cpp(fb->pixel_format, 0) +
						src_y * stride;
	offset += fb->offsets[0];

	ret = xilinx_drm_mixer_mark_layer_active(plane);
	if (ret)
		return ret;

	switch (layer_id) {
	case XVMIX_LAYER_LOGO:
		ret = xilinx_drm_mixer_update_logo_img(plane, buffer,
						       src_w, src_h);
		if (ret)
			break;

		ret = xilinx_drm_mixer_set_layer_dimensions(plane,
							    crtc_x, crtc_y,
							    src_w, src_h,
							    stride);
		break;

	case XVMIX_LAYER_MASTER:
		if (!mixer_layer_is_streaming(plane->mixer_layer))
			xilinx_drm_mixer_mark_layer_inactive(plane);

		if (mixer->drm_primary_layer == mixer->hw_master_layer) {
			xilinx_mixer_layer_disable(mixer_hw, layer_id);
			msleep(50);

			ret = xilinx_mixer_set_active_area(mixer_hw,
							   src_w, src_h);

			xilinx_mixer_layer_enable(mixer_hw, layer_id);

		} else if (src_w != active_area_width ||
			   src_h != active_area_height) {
			DRM_ERROR("Invalid dimensions for mixer layer 0.\n");
			return -EINVAL;
		}

		break;

	default:
		ret = xilinx_drm_mixer_set_layer_dimensions(plane,
							    crtc_x, crtc_y,
							    src_w, src_h,
							    stride);
		if (ret)
			break;

		if (!mixer_layer_is_streaming(plane->mixer_layer))
			ret = xilinx_mixer_set_layer_buff_addr
						(mixer_hw,
						 plane->mixer_layer->id,
						 buffer->paddr + offset);
	}

	return ret;
}

int xilinx_drm_mixer_set_plane_property(struct xilinx_drm_plane *plane,
					struct drm_property *property,
					uint64_t value)
{
	struct xilinx_drm_mixer *mixer = plane->manager->mixer;

	if (property == mixer->alpha_prop)
		return xilinx_drm_mixer_set_layer_alpha(plane, value);

	if (property == mixer->scale_prop)
		return xilinx_drm_mixer_set_layer_scale(plane, value);

	if (property == mixer->bg_color) {
		xilinx_mixer_set_bkg_col(&mixer->mixer_hw, value);
		return 0;
	}

	return -EINVAL;
}

void
xilinx_drm_mixer_plane_dpms(struct xilinx_drm_plane *plane, int dpms)
{
	struct xilinx_drm_mixer *mixer = plane->manager->mixer;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		xilinx_drm_mixer_layer_enable(plane);
		break;

	default:
		xilinx_drm_mixer_mark_layer_inactive(plane);
		xilinx_drm_mixer_layer_disable(plane);

		/* restore to default property values */
		if (mixer->alpha_prop) {
			drm_object_property_set_value(&plane->base.base,
						      mixer->alpha_prop,
						      XVMIX_ALPHA_MAX);
			xilinx_drm_mixer_set_layer_alpha(plane,
							 XVMIX_ALPHA_MAX);
		}

		if (mixer->scale_prop) {
			drm_object_property_set_value(&plane->base.base,
						      mixer->scale_prop,
						      XVMIX_SCALE_FACTOR_1X);
			xilinx_drm_mixer_set_layer_scale(plane,
							 XVMIX_SCALE_FACTOR_1X);
		}
	}
}

void xilinx_drm_mixer_dpms(struct xilinx_drm_mixer *mixer, int dpms)
{
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		xilinx_mixer_start(&mixer->mixer_hw);
		break;

	default:
		xilinx_drm_mixer_reset(mixer);
	}
}

int xilinx_drm_mixer_string_to_fmt(const char *color_fmt, u32 *output)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(color_table); i++) {
		if (strcmp(color_fmt, (const char *)color_table[i].name) == 0) {
			*output = color_table[i].fmt_id;
			return 0;
		}
	}

	return -EINVAL;
}

int xilinx_drm_mixer_fmt_to_drm_fmt(enum xv_comm_color_fmt_id id, u32 *output)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(color_table); i++) {
		if (id == color_table[i].fmt_id)
			*output = color_table[i].drm_format;
	}

	if (output)
		return 0;

	return -EINVAL;
}

int xilinx_drm_mixer_set_layer_scale(struct xilinx_drm_plane *plane,
				     uint64_t val)
{
	struct xv_mixer *mixer_hw = to_xv_mixer_hw(plane);
	struct xv_mixer_layer_data *layer = plane->mixer_layer;
	int ret;

	if (!layer || !layer->hw_config.can_scale)
		return -ENODEV;

	if (val > XVMIX_SCALE_FACTOR_4X ||
	    val < XVMIX_SCALE_FACTOR_1X) {
		DRM_ERROR("Mixer layer scale value illegal.\n");
		return -EINVAL;
	}
	xilinx_drm_mixer_layer_disable(plane);
	msleep(50);
	ret = xilinx_mixer_set_layer_scaling(mixer_hw, layer->id, val);
	xilinx_drm_mixer_layer_enable(plane);

	return ret;
}

int xilinx_drm_mixer_set_layer_alpha(struct xilinx_drm_plane *plane,
				     uint64_t val)
{
	struct xv_mixer *mixer_hw = to_xv_mixer_hw(plane);
	struct xv_mixer_layer_data *layer = plane->mixer_layer;
	int ret;

	if (!layer || !layer->hw_config.can_alpha)
		return -EINVAL;

	if (val > XVMIX_ALPHA_MAX || val < XVMIX_ALPHA_MIN) {
		DRM_ERROR("Mixer layer alpha dts value illegal.\n");
		return -EINVAL;
	}
	ret = xilinx_mixer_set_layer_alpha(mixer_hw, layer->id, val);
	if (ret)
		return ret;

	return 0;
}

void xilinx_drm_mixer_layer_disable(struct xilinx_drm_plane *plane)
{
	struct xv_mixer *mixer_hw;
	u32 layer_id;

	if (plane)
		mixer_hw = to_xv_mixer_hw(plane);
	else
		return;

	layer_id = plane->mixer_layer->id;
	if (layer_id < XVMIX_LAYER_MASTER  || layer_id > XVMIX_LAYER_LOGO)
		return;

	xilinx_mixer_layer_disable(mixer_hw, layer_id);
}

void xilinx_drm_mixer_layer_enable(struct xilinx_drm_plane *plane)
{
	struct xv_mixer *mixer_hw;
	struct xv_mixer_layer_data *layer_data;
	u32 layer_id;

	if (!plane)
		return;

	mixer_hw = to_xv_mixer_hw(plane);
	layer_data = plane->mixer_layer;
	layer_id = layer_data->id;

	if (layer_id < XVMIX_LAYER_MASTER  || layer_id > XVMIX_LAYER_LOGO) {
		DRM_DEBUG_KMS("Attempt to activate invalid layer: %d\n",
			      layer_id);
		return;
	}

	if (layer_id == XVMIX_LAYER_MASTER &&
	    !mixer_layer_is_streaming(layer_data)) {
		return;
	}

	xilinx_mixer_layer_enable(mixer_hw, layer_id);
}

int xilinx_drm_mixer_set_layer_dimensions(struct xilinx_drm_plane *plane,
					  u32 crtc_x, u32 crtc_y,
					  u32 width, u32 height, u32 stride)
{
	struct xilinx_drm_mixer *mixer = plane->manager->mixer;
	struct xv_mixer *mixer_hw = to_xv_mixer_hw(plane);
	struct xv_mixer_layer_data *layer_data;
	enum xv_mixer_layer_id layer_id;
	bool disable_req = false;
	int ret = 0;

	layer_data = plane->mixer_layer;
	layer_id = layer_data->id;

	if (mixer_layer_height(layer_data) != height ||
	    mixer_layer_width(layer_data) != width)
		disable_req = true;

	/* disable any layers necessary */
	if (disable_req) {
		if (mixer->drm_primary_layer == layer_data)
			xilinx_mixer_layer_disable(mixer_hw,
						   XVMIX_LAYER_MASTER);

		if (layer_id != XVMIX_LAYER_MASTER &&
		    layer_id < XVMIX_LAYER_ALL) {
			xilinx_mixer_layer_disable(mixer_hw, layer_id);
		} else {
			DRM_DEBUG_KMS("Invalid mixer layer id %u\n", layer_id);
			return -EINVAL;
		}
		msleep(50);
	}

	if (mixer->drm_primary_layer == layer_data) {
		/* likely unneeded but, just to be sure...*/
		crtc_x = 0;
		crtc_y = 0;

		ret = xilinx_mixer_set_active_area(mixer_hw,
						   width, height);

		if (ret)
			return ret;

		xilinx_mixer_layer_enable(mixer_hw, XVMIX_LAYER_MASTER);
	}

	if (layer_id != XVMIX_LAYER_MASTER && layer_id < XVMIX_LAYER_ALL) {
		ret = xilinx_mixer_set_layer_window(mixer_hw, layer_id,
						    crtc_x, crtc_y,
						    width, height, stride);

		if (ret)
			return ret;

		xilinx_drm_mixer_layer_enable(plane);
	}

	return ret;
}

struct
xv_mixer_layer_data *xilinx_drm_mixer_get_layer(struct xv_mixer *mixer_hw,
						enum xv_mixer_layer_id layer_id)
{
	return xilinx_mixer_get_layer_data(mixer_hw, layer_id);
}

void xilinx_drm_mixer_reset(struct xilinx_drm_mixer *mixer)
{
	struct xv_mixer *mixer_hw = &mixer->mixer_hw;

	gpiod_set_raw_value(mixer_hw->reset_gpio, 0x0);
	udelay(1);
	gpiod_set_raw_value(mixer_hw->reset_gpio, 0x1);

	/* restore layer properties and bg color after reset */
	xilinx_mixer_set_bkg_col(mixer_hw, mixer_hw->bg_color);

	if (mixer_hw->intrpts_enabled)
		xilinx_mixer_intrpt_enable(mixer_hw);

	xilinx_drm_plane_restore(mixer->plane_manager);
}

int xilinx_drm_mixer_mark_layer_active(struct xilinx_drm_plane *plane)
{
	if (!plane->mixer_layer)
		return -ENODEV;

	mixer_layer_active(plane->mixer_layer) = true;

	return 0;
}

int xilinx_drm_mixer_mark_layer_inactive(struct xilinx_drm_plane *plane)
{
	if (!plane || !plane->mixer_layer)
		return -ENODEV;

	mixer_layer_active(plane->mixer_layer) = false;

	return 0;
}

int xilinx_drm_mixer_update_logo_img(struct xilinx_drm_plane *plane,
				     struct drm_gem_cma_object *buffer,
				     u32 src_w, u32 src_h)
{
	struct xv_mixer_layer_data *logo_layer = plane->mixer_layer;
	size_t pixel_cnt = src_h * src_w;

	/* color comp defaults to offset in RG24 buffer */
	u32 pix_cmp_cnt;
	u32 logo_cmp_cnt;
	enum xv_comm_color_fmt_id layer_pixel_fmt = 0;
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
	if (logo_layer->id != XVMIX_LAYER_LOGO)
		return 0;

	if (src_h > max_height || src_w > max_width ||
	    src_h < min_height || src_w < min_width) {
		DRM_ERROR("Mixer logo/cursor layer dimensions illegal.\n");

		return -EINVAL;
	}

	ret = xilinx_drm_mixer_fmt_to_drm_fmt(logo_layer->hw_config.vid_fmt,
					      &layer_pixel_fmt);

	per_pixel_alpha =
		(mixer_layer_fmt(logo_layer) == XVIDC_CSF_RGBA8) ? true : false;

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

	pix_cmp_cnt = per_pixel_alpha ? 4 : 3;
	logo_cmp_cnt = pixel_cnt * pix_cmp_cnt;

	if (ret)
		return ret;

	/* ensure buffer attributes have changed to indicate new logo
	 * has been created
	 */
	if ((phys_addr_t)buffer->vaddr == logo_layer->layer_regs.buff_addr &&
	    src_w == logo_layer->layer_regs.width &&
	    src_h == logo_layer->layer_regs.height)
		return 0;

	/* cache buffer address for future comparison */
	logo_layer->layer_regs.buff_addr = (phys_addr_t)buffer->vaddr;

	pixel_mem_data = (u8 *)(buffer->vaddr);

	for (i = 0, j = 0; j < pixel_cnt; j++) {
		if (per_pixel_alpha && a_data)
			a_data[j] = pixel_mem_data[i++];

		b_data[j] = pixel_mem_data[i++];
		g_data[j] = pixel_mem_data[i++];
		r_data[j] = pixel_mem_data[i++];
	}

	ret = xilinx_mixer_logo_load(to_xv_mixer_hw(plane),
				     src_w, src_h,
				     r_data, g_data, b_data,
				     per_pixel_alpha ? a_data : NULL);

free:
	kfree(r_data);
	kfree(g_data);
	kfree(b_data);
	kfree(a_data);

	return ret;
}

void xilinx_drm_mixer_set_intr_handler(struct xilinx_drm_mixer *mixer,
				       void (*intr_handler_fn)(void *),
				       void *data)
{
	mixer->mixer_hw.intrpt_handler_fn = intr_handler_fn;
	mixer->mixer_hw.intrpt_data = data;
}

void xilinx_drm_create_mixer_plane_properties(struct xilinx_drm_mixer *mixer)
{
	u64 bit_shift = (XVMIX_MAX_BPC - mixer->mixer_hw.bg_layer_bpc) * 3;
	u64 max_scale_range = (XVMIX_MAX_BG_COLOR_BITS >> bit_shift);

	mixer->scale_prop =
		drm_property_create_range(mixer->plane_manager->drm, 0,
					  "scale",
					  XVMIX_SCALE_FACTOR_1X,
					  XVMIX_SCALE_FACTOR_4X);

	mixer->alpha_prop =
		drm_property_create_range(mixer->plane_manager->drm, 0,
					  "alpha",
					  XVMIX_ALPHA_MIN,
					  XVMIX_ALPHA_MAX);

	mixer->bg_color =
		drm_property_create_range(mixer->plane_manager->drm,
					  0, "bg_color", 0,
					  max_scale_range);
}

void xilinx_drm_mixer_attach_plane_prop(struct xilinx_drm_plane *plane)
{
	struct xilinx_drm_plane_manager *manager = plane->manager;
	struct xilinx_drm_mixer *mixer = manager->mixer;
	struct drm_mode_object *base = &plane->base.base;

	if (plane->mixer_layer->hw_config.can_scale)
		drm_object_attach_property(base,
					   mixer->scale_prop,
					   XVMIX_SCALE_FACTOR_1X);

	if (plane->mixer_layer->hw_config.can_alpha)
		drm_object_attach_property(base,
					   mixer->alpha_prop,
					   XVMIX_ALPHA_MAX);

	if (mixer->drm_primary_layer == plane->mixer_layer)
		drm_object_attach_property(base,
					   mixer->bg_color,
					   mixer->mixer_hw.bg_color);
}

int
xilinx_drm_create_mixer_layer_plane(struct xilinx_drm_plane_manager *manager,
				    struct xilinx_drm_plane *plane,
				    struct device_node *node)
{
	struct xv_mixer_layer_data *layer_data;
	struct xilinx_drm_mixer *mixer = manager->mixer;
	u32 layer_id;
	int ret;

	ret = of_property_read_u32(node, "xlnx,layer-id", &layer_id);

	if (ret) {
		DRM_ERROR("Missing xlnx,layer-id parameter in mixer dts\n");
		ret = -EINVAL;
	}

	layer_data =
		xilinx_drm_mixer_get_layer(&mixer->mixer_hw, layer_id);

	if (!layer_data)
		return -ENODEV;

	of_node_put(node);

	plane->mixer_layer = layer_data;

	ret = xilinx_drm_mixer_fmt_to_drm_fmt
					(mixer_layer_fmt(plane->mixer_layer),
					 &plane->format);

	if (ret)
		DRM_ERROR("Missing video format in dts for drm plane id %d\n",
			  plane->id);
	return ret;
}

static int xilinx_drm_mixer_parse_dt_logo_data(struct device_node *node,
					       struct xv_mixer *mixer_hw)
{
	struct xv_mixer_layer_data *layer_data;
	struct device_node *logo_node;
	u32 max_width, max_height;
	int ret;

	/* read in logo data */
	if (!mixer_hw->logo_layer_enabled)
		return 0;

	logo_node = of_get_child_by_name(node, "logo");
	if (!logo_node) {
		DRM_ERROR("No logo node specified in device tree.\n");
		return -EINVAL;
	}

	layer_data = &mixer_hw->layer_data[LOGO_LAYER_IDX];

	/* set defaults for logo layer */
	layer_data->hw_config.min_height = XVMIX_LOGO_LAYER_HEIGHT_MIN;
	layer_data->hw_config.min_width = XVMIX_LOGO_LAYER_WIDTH_MIN;
	layer_data->hw_config.is_streaming = false;
	layer_data->hw_config.vid_fmt = XVIDC_CSF_RGB;
	layer_data->hw_config.can_alpha = true;
	layer_data->hw_config.can_scale = true;
	layer_data->layer_regs.buff_addr = 0;
	layer_data->id = XVMIX_LAYER_LOGO;

	ret  = of_property_read_u32(logo_node, "xlnx,logo-width", &max_width);

	if (ret) {
		DRM_ERROR("Failed to get logo width prop\n");
		return -EINVAL;
	}

	if (max_width > XVMIX_LOGO_LAYER_WIDTH_MAX ||
	    max_width < XVMIX_LOGO_LAYER_WIDTH_MIN) {
		DRM_ERROR("Illegal mixer logo layer width.\n");
		return -EINVAL;
	}

	layer_data->hw_config.max_width = max_width;
	mixer_hw->max_logo_layer_width =
		layer_data->hw_config.max_width;

	ret = of_property_read_u32(logo_node, "xlnx,logo-height", &max_height);

	if (ret) {
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

	mixer_hw->logo_color_key_enabled =
			of_property_read_bool(logo_node,
					      "xlnx,logo-transp");

	mixer_hw->logo_pixel_alpha_enabled =
		of_property_read_bool(logo_node,
				      "xlnx,logo-pixel-alpha");

	if (mixer_hw->logo_pixel_alpha_enabled)
		layer_data->hw_config.vid_fmt = XVIDC_CSF_RGBA8;

	return ret;
}

static int xilinx_drm_mixer_parse_dt_bg_video_fmt(struct device_node *node,
						  struct xv_mixer *mixer_hw)
{
	struct device_node *layer_node;
	const char *vformat;
	struct xv_mixer_layer_data *layer;
	u32 *l_width_ptr;
	u32 *l_height_ptr;
	u32 *l_vid_fmt_ptr;
	int ret;

	layer_node = of_get_child_by_name(node, "layer_0");
	layer = &mixer_hw->layer_data[MASTER_LAYER_IDX];
	l_width_ptr = &layer->hw_config.max_width;
	l_height_ptr = &layer->hw_config.max_height;
	l_vid_fmt_ptr = &layer->hw_config.vid_fmt;

	/* Set default values */
	layer->hw_config.can_alpha = false;
	layer->hw_config.can_scale = false;
	layer->hw_config.is_streaming = false;
	layer->hw_config.min_width = XVMIX_LAYER_WIDTH_MIN;
	layer->hw_config.min_height = XVMIX_LAYER_HEIGHT_MIN;

	ret = of_property_read_string(layer_node, "xlnx,vformat", &vformat);

	if (ret) {
		DRM_ERROR("No xlnx,vformat value for layer_0 in dts.\n");
		return -EINVAL;
	}

	mixer_layer_is_streaming(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-streaming");

	ret = of_property_read_u32(node, "xlnx,bpc",
				   &mixer_hw->bg_layer_bpc);
	if (ret) {
		DRM_ERROR("Failed to get bits per component (bpc) prop\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(layer_node, "xlnx,layer-width", l_width_ptr);
	if (ret) {
		DRM_ERROR("Failed to get screen width prop\n");
		return -EINVAL;
	}

	/* set global max width for mixer which will, ultimately, set the
	 *  limit for the crtc
	 */
	mixer_hw->max_layer_width = *l_width_ptr;

	ret = of_property_read_u32(layer_node, "xlnx,layer-height",
				   l_height_ptr);
	if (ret) {
		DRM_ERROR("Failed to get screen height prop\n");
		return -EINVAL;
	}

	mixer_hw->max_layer_height = *l_height_ptr;

	/*We'll use the first layer instance to store data of the master layer*/
	layer->id = XVMIX_LAYER_MASTER;

	ret = xilinx_drm_mixer_string_to_fmt(vformat, l_vid_fmt_ptr);
	if (ret < 0) {
		DRM_ERROR("Invalid mixer video format in dts\n");
		return -EINVAL;
	}

	return ret;
}

static irqreturn_t xilinx_drm_mixer_intr_handler(int irq, void *data)
{
	struct xv_mixer *mixer = data;

	u32 intr = xilinx_mixer_get_intr_status(mixer);

	if (!intr)
		return IRQ_NONE;

	else if (mixer->intrpt_handler_fn)
		mixer->intrpt_handler_fn(mixer->intrpt_data);

	xilinx_mixer_clear_intr_status(mixer, intr);

	return IRQ_HANDLED;
}

static int
xilinx_drm_mixer_of_init_layer_data(struct device *dev,
				    struct device_node *node,
				    char *layer_name,
				    struct xv_mixer_layer_data *layer,
				    u32 max_layer_width,
				    struct xv_mixer_layer_data **drm_pri_layer)
{
	struct device_node *layer_node;
	const char *vformat;
	int ret;

	layer_node = of_get_child_by_name(node, layer_name);

	if (!layer_node)
		return -1;

	/* Set default values */
	layer->hw_config.can_alpha = false;
	layer->hw_config.can_scale = false;
	layer->hw_config.is_streaming = false;
	layer->hw_config.max_width = max_layer_width;
	layer->hw_config.min_width = XVMIX_LAYER_WIDTH_MIN;
	layer->hw_config.min_height = XVMIX_LAYER_HEIGHT_MIN;
	layer->hw_config.vid_fmt = 0;
	layer->id = 0;

	ret = of_property_read_u32(layer_node, "xlnx,layer-id", &layer->id);

	if (ret || layer->id < 1 ||
	    layer->id > (XVMIX_MAX_SUPPORTED_LAYERS - 1)) {
		dev_err(dev,
			"Mixer layer id %u in dts is out of legal range\n",
			layer->id);
		ret = -EINVAL;
	}

	ret = of_property_read_string(layer_node, "xlnx,vformat", &vformat);
	if (ret) {
		dev_err(dev,
			"No mixer layer video format in dts for layer id %d\n",
			layer->id);
		return -EINVAL;
	}

	ret = xilinx_drm_mixer_string_to_fmt(vformat,
					     &layer->hw_config.vid_fmt);
	if (ret < 0) {
		dev_err(dev,
			"No matching video format for mixer layer %d in dts\n",
			layer->id);
		return -EINVAL;
	}

	mixer_layer_can_scale(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-scale");

	if (mixer_layer_can_scale(layer)) {
		ret = of_property_read_u32(layer_node, "xlnx,layer-width",
					   &layer->hw_config.max_width);
		if (ret) {
			dev_err(dev, "Mixer layer %d dts missing width prop.\n",
				layer->id);
			return ret;
		}

		if (layer->hw_config.max_width > max_layer_width) {
			dev_err(dev,
				"Mixer layer %d width in dts > max width\n",
				layer->id);
			return -EINVAL;
		}
	}

	mixer_layer_can_alpha(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-alpha");

	mixer_layer_is_streaming(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-streaming");

	if (of_property_read_bool(layer_node, "xlnx,layer-primary")) {
		if (*drm_pri_layer) {
			dev_err(dev,
				"More than one primary layer in mixer dts\n");
			return -EINVAL;
		}
		mixer_layer_can_scale(layer) = false;
		*drm_pri_layer = layer;
	}
	return 0;
}

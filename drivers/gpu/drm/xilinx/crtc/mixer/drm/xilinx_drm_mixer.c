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

#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio/consumer.h>

#include "xilinx_drm_drv.h"

#include "crtc/mixer/drm/xilinx_drm_mixer.h"

#include "crtc/mixer/hw/xilinx_mixer_hw.h"
#include "crtc/mixer/hw/xilinx_video_mixer.h"

#define COLOR_NAME_SIZE 10


struct color_fmt_tbl {
    char 		 name[COLOR_NAME_SIZE+1];
    xv_comm_color_fmt_id fmt_id;
    u32 		 drm_format;
};

/*************************** STATIC DATA  ************************************/
static struct color_fmt_tbl color_table[] = {
    {"rgb",    XVIDC_CSF_RGB,       DRM_FORMAT_RGB888},
    {"yuv444", XVIDC_CSF_YCRCB_444, DRM_FORMAT_YUV444},
    {"yuv422", XVIDC_CSF_YCRCB_422, DRM_FORMAT_YUYV},
    {"yuv420", XVIDC_CSF_YCRCB_420, DRM_FORMAT_YUV420},
};

static const struct of_device_id xv_mixer_match[] = {
    {.compatible = "xlnx,v-mix-1.0"},
    {/*end of table*/},
};

/*************************** PROTOTYPES **************************************/

static int 
xilinx_drm_mixer_of_init_layer_data(struct device_node *dev_node,
                                    char *layer_name,
                                    struct xv_mixer_layer_data *layer,
				    uint32_t max_layer_width);

static int 
xilinx_drm_mixer_parse_dt_logo_data (struct device_node *node,
				     struct xv_mixer *mixer);

static int 
xilinx_drm_mixer_parse_dt_bg_video_fmt (struct device_node *layer_node,
			   	        struct xv_mixer *mixer);


/************************* IMPLEMENTATIONS ***********************************/
struct xv_mixer * xilinx_drm_mixer_probe(struct device *dev, 
                                        struct device_node *node,
					struct xilinx_drm_plane_manager *manager)
{

	struct xv_mixer			*mixer;
	char 				layer_node_name[20] = {0};
	struct xv_mixer_layer_data 	*layer_data;
	const struct of_device_id 	*match;
	struct resource			res;
	int 				ret;
	int 				layer_idx;
	int				layer_cnt;
	int				i;

    	match = of_match_node(xv_mixer_match, node);
	if(!match) {
		dev_err(dev, "Failed to match device node for mixer\n"); 
		return ERR_PTR(-ENODEV);
	}

	mixer = devm_kzalloc(dev, sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return ERR_PTR(-ENOMEM);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "Failed to parse node memory address from dts\n");
		return ERR_PTR(ret);
	}

	mixer->reg_base_addr = devm_ioremap_resource(dev, &res);
	if (IS_ERR(mixer->reg_base_addr)) {
		dev_err(dev, "Failed to map io space into virtual memory for "
			"mixer\n");
		return ERR_CAST(mixer->reg_base_addr);
	}

	ret = of_property_read_u32(node, "xlnx,num-layers", 
				   &(mixer->max_layers));
	if (ret) {
		dev_err(dev, "Failed to get num of layers prop\n");
		return ERR_PTR(-EINVAL);
	}

    	if(mixer->max_layers > XVMIX_MAX_SUPPORTED_LAYERS) {
			dev_err(dev, "Number of layers specified in device "
				"tree exceeds mixer capabilities\n");
			return ERR_PTR(-EINVAL);

    	} 

	mixer->logo_layer_enabled = of_property_read_bool(node,
							  "xlnx,logo-layer");

    	/* Alloc num_layers + 1 for logo layer if enabled in dt */
	layer_cnt = mixer->max_layers + (mixer->logo_layer_enabled ? 1 : 0);	

	layer_data = devm_kzalloc(dev, 
				sizeof(struct xv_mixer_layer_data) * layer_cnt, 
                                GFP_KERNEL);

	if(layer_data) { 
		mixer->layer_cnt = layer_cnt;
	} else {
		dev_err(dev, "Failed to to allocate memory to store mixer layer"
			" data\n");
		return ERR_PTR(-ENOMEM);
    	}

    	mixer->layer_data = layer_data;


   	/* establish background layer video properties */
	ret = xilinx_drm_mixer_parse_dt_bg_video_fmt(node, mixer);
	if(ret) {
		dev_err(dev, "Incomplete mixer video format in dt\n");
		return ERR_PTR(-EINVAL);
	}
	mixer->private = (void *)manager;


	/* Parse out logo data from device tree */
	ret = xilinx_drm_mixer_parse_dt_logo_data(node, mixer);
	if(ret) {
		dev_err(dev, "Failed to parse all required logo properties "
			"from dt\n");
		return ERR_PTR(-EINVAL);
	}

	layer_idx = mixer->logo_layer_enabled ? 2 : 1;
	for(i=1; i <= (mixer->max_layers -1); i++, layer_idx++) {

		snprintf(layer_node_name, sizeof(layer_node_name), 
			"layer_%d", i);

		ret = 
		     xilinx_drm_mixer_of_init_layer_data(node, layer_node_name,
		  				      &(mixer->layer_data[layer_idx]),
							mixer->max_layer_width);

		if(ret) {
		    dev_err(dev, "Failed to obtain required parameter(s) for "
				 "mixer layer %d and/or invalid parameter values"
				 " supplied\n",i);
		    return ERR_PTR(-EINVAL);
		}

	}

	/*Pull device out of reset */
	mixer->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if(IS_ERR(mixer->reset_gpio))
		return(ERR_PTR(mixer->reset_gpio));

	gpiod_set_raw_value(mixer->reset_gpio, 0x1);

	/* init the hardware and establish default register values */
	xilinx_mixer_init(mixer); 

	/* initialize all layers to inactive state in software. An update_plane()
	 * call to our drm driver will change this to 'active' and permit the
	 * layer to be enabled in hardware */	
	for(i=0; i<mixer->layer_cnt; i++){
		layer_data = &(mixer->layer_data[i]);
		mixer_layer_active(layer_data) = false;
	}

	return mixer;    
}



static int xilinx_drm_mixer_of_init_layer_data(struct device_node *node, 
                                             char *layer_name,
                                             struct xv_mixer_layer_data *layer,
					     uint32_t max_layer_width)
{
	struct device_node *layer_node;
	const char *vformat;
	int ret = 0;

	layer_node = of_get_child_by_name(node, layer_name);


	if(!layer_node) 
		return -1;

	/* Set default values */ 
	layer->hw_config.can_alpha = false;
	layer->hw_config.can_scale = false;
	layer->hw_config.is_streaming = false;
	layer->hw_config.max_width = max_layer_width; 
	layer->hw_config.vid_fmt = 0; 
	layer->id = 0; 

	ret = of_property_read_string(layer_node, "xlnx,vformat", &vformat);
	if(ret) {
		return -1;
	}

	ret = of_property_read_u32(layer_node,"xlnx,layer-id", &layer->id);
	if(ret || layer->id < 1 || layer->id > (XVMIX_MAX_SUPPORTED_LAYERS - 1)) {
		ret = -1;
	}

	ret = xilinx_drm_mixer_string_to_fmt(vformat, 
					     &(layer->hw_config.vid_fmt));
	if(ret < 0) {
		return -1;
	}

	if(mixer_layer_can_scale(layer)) {
	    ret = of_property_read_u32(layer_node, "xlnx,layer-width",
				&(layer->hw_config.max_width));
		if(ret) {
			return ret;
		}

		if(layer->hw_config.max_width > max_layer_width)
			return -EINVAL;
	}

	mixer_layer_can_scale(layer) = 
		    of_property_read_bool(layer_node, "xlnx,layer-scale");

	mixer_layer_can_alpha(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-alpha");

	mixer_layer_is_streaming(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-streaming");

	return 0;
}



int xilinx_drm_mixer_string_to_fmt(const char *color_fmt, u32 *output)
{
	int i,l;

	l = strlen(color_fmt);

	for(i=0; i < ARRAY_SIZE(color_table); i++) {
		if(0==strncmp(color_fmt, (const char *)color_table[i].name, l))
		    *output = color_table[i].fmt_id;
	}

	if(output)
		return 0;

	return -1;
}

int xilinx_drm_mixer_fmt_to_drm_fmt(xv_comm_color_fmt_id id, u32 *output) 
{
	int i;

	for(i=0; i < ARRAY_SIZE(color_table); i++) {
		if(id == color_table[i].fmt_id)
		    *output = color_table[i].drm_format;
	}

	if(output)
		return 0;

	return -1;
} 



int
xilinx_drm_mixer_set_layer_scale(struct xilinx_drm_plane *plane,
                                 uint64_t val) 
{
	struct xv_mixer *mixer = plane->manager->mixer;
	struct xv_mixer_layer_data *layer = plane->mixer_layer;
	int ret;

	if(layer && layer->hw_config.can_scale){
		if(val > XVMIX_SCALE_FACTOR_4X || val < XVMIX_SCALE_FACTOR_1X){
			DRM_ERROR("Property setting for mixer layer scale "
				  "exceeds legal values\n");
			return -EINVAL;
		}
		xilinx_drm_mixer_layer_disable(plane);
		ret = xilinx_mixer_set_layer_scaling(mixer, layer->id ,val);
		if(ret)
			return ret;

		xilinx_drm_mixer_layer_enable(plane);

		return 0; 
	}

	return -ENODEV;
}

int
xilinx_drm_mixer_set_layer_alpha(struct xilinx_drm_plane *plane,
                                 uint64_t val)
{
	struct xv_mixer *mixer = plane->manager->mixer;
	struct xv_mixer_layer_data *layer = plane->mixer_layer;
	int ret;

	if(layer && layer->hw_config.can_alpha){
		if(val > XVMIX_ALPHA_MAX || val < XVMIX_ALPHA_MIN) {
		    DRM_ERROR("Property setting for mixer layer alpha exceeds "
			      "legal values\n");
		    return -1;
		}
		ret = xilinx_mixer_set_layer_alpha(mixer, layer->id ,val);
		if(ret)
		    return ret;

		return 0;
	}
	return -1;
}



void
xilinx_drm_mixer_layer_disable(struct xilinx_drm_plane *plane)
{
	struct xv_mixer *mixer = plane->manager->mixer;
	u32 layer_id = plane->mixer_layer->id;
	if(layer_id < XVMIX_LAYER_MASTER  || layer_id > XVMIX_LAYER_LOGO)
		return;

	xilinx_mixer_layer_disable(mixer, layer_id);

}

void
xilinx_drm_mixer_layer_enable(struct xilinx_drm_plane *plane)
{
	struct xv_mixer *mixer = plane->manager->mixer;
	u32 layer_id = plane->mixer_layer->id;

	if(layer_id < XVMIX_LAYER_MASTER  || layer_id > XVMIX_LAYER_LOGO) {
		DRM_DEBUG_KMS("Attempt to activate invalid layer: %d\n", layer_id);
		return;
	}

	xilinx_mixer_layer_enable(mixer, layer_id);
}



int
xilinx_drm_mixer_set_layer_dimensions(struct xilinx_drm_plane *plane,
                                      u32 crtc_x, u32 crtc_y,
                                      u32 width, u32 height)
{
	/* JPM TODO Need to update this call to compute stride which is needed
	* for memory mapped layers
	*/
	int ret = 0;
	struct xv_mixer *mixer = plane->manager->mixer;
	xv_mixer_layer_id layer_id = plane->mixer_layer->id;

	if(layer_id != XVMIX_LAYER_MASTER && layer_id < XVMIX_LAYER_ALL) {
		xilinx_drm_mixer_layer_disable(plane);
		ret = xilinx_mixer_set_layer_window(mixer, layer_id,
						     crtc_x, crtc_y, 
						     width, height, 0);

		/*JPM TODO update l2 driver code to use linux error codes*/
		if(ret)
			return -EINVAL;

	}

	if(layer_id == XVMIX_LAYER_MASTER) { 
		xilinx_drm_mixer_layer_disable(plane);

		/*JPM TODO update l2 driver code to use linux error codes*/
		ret = xilinx_mixer_set_active_area(mixer, width, height);
		if(ret)
			return -EINVAL;

		xilinx_drm_mixer_layer_enable(plane);
	}
	
	return ret;
}



struct xv_mixer_layer_data *
xilinx_drm_mixer_get_layer(struct xv_mixer *mixer, xv_mixer_layer_id layer_id)
{
        return xilinx_mixer_get_layer_data(mixer, layer_id);
}

void xilinx_drm_mixer_reset(struct xv_mixer *mixer) {

	struct xv_mixer_layer_data layer;
	struct xilinx_drm_plane_manager *manager =
		(struct xilinx_drm_plane_manager *)mixer->private;
	int i;
	int ret;
	
	gpiod_set_raw_value(mixer->reset_gpio, 0x0);

	udelay(1);

	gpiod_set_raw_value(mixer->reset_gpio, 0x1);

	/* restore layer properties and bg color after reset */
	xilinx_mixer_set_bkg_col(mixer, mixer->bg_color, mixer->bg_layer_bpc);
#if 0
	for(i = 0; i <= mixer->layer_cnt; i++) {

		layer = mixer->layer_data[i];
		/* all layers disabled so no need to explicitly disable/enable */
		/* JPM TODO determine if we should permit scale to be zero'd. 
		   restoring from cache may not be the right thing to do during
		   a modeset to a lower resolution.  Would risk layer exceeding
		   active area */
		ret = xilinx_mixer_set_layer_scaling(mixer, layer.id,
						layer.layer_regs.scale_fact);
		if(ret)
			DRM_ERROR("Problem restoring scale property for mixer"
				  " layer %u\n", layer.id); 

		ret = xilinx_mixer_set_layer_alpha(mixer, layer.id,
						layer.layer_regs.alpha);
		if(ret)
			DRM_ERROR("Problem restoring alpha property for mixer"
				  " layer %u\n", layer.id); 
	}
#endif
	xilinx_drm_plane_restore(manager);	

	/* JPM TODO remove this.  Just a temporary measure to test logo layer
	   after resets.  Need to update logo buffer in response to
	   plane_update() calls via a check to see if a new buffer has been
	   provided */
	xilinx_mixer_logo_load(mixer,64,64,NULL,NULL,NULL);
} 

static int xilinx_drm_mixer_parse_dt_logo_data (struct device_node *node,
						struct xv_mixer *mixer) {

	int ret = 0;
	struct xv_mixer_layer_data *layer_data; 
	struct device_node *logo_node;

	/* read in logo data */
	if(mixer->logo_layer_enabled) {
		
		logo_node = of_get_child_by_name(node, "logo");
		if(!logo_node) {
			DRM_ERROR("No logo node specified in device tree.\n");
			return -1;
		}

		layer_data = &(mixer->layer_data[1]);

		/* set defaults for logo layer */
		layer_data->hw_config.is_streaming = false;
		layer_data->hw_config.vid_fmt = XVIDC_CSF_RGB;
		layer_data->id = XVMIX_LAYER_LOGO;

		ret  = of_property_read_u32(logo_node, "xlnx,logo-width", 
					&(layer_data->hw_config.max_width));

		if (ret) {
		    DRM_ERROR("Failed to get logo width prop\n");
		    return -1; 
		}
		mixer->max_logo_layer_width = layer_data->hw_config.max_width; 	

		ret = of_property_read_u32(logo_node, "xlnx,logo-height",
					   &(layer_data->hw_config.max_height));

		if (ret) {
		    DRM_ERROR("Failed to get logo height prop\n");
		    return -1;
		}

		mixer->max_logo_layer_height = layer_data->hw_config.max_height; 	

		mixer->logo_color_key_enabled = 
				of_property_read_bool(logo_node, 
						      "xlnx,logo-transparency");

		layer_data->hw_config.can_alpha = 
			of_property_read_bool(logo_node, "xlnx,logo-alpha");

		layer_data->hw_config.can_scale = 
			of_property_read_bool(logo_node, "xlnx,logo-scale");
		
	}
	return ret;
}



static int xilinx_drm_mixer_parse_dt_bg_video_fmt (struct device_node *node,
						   struct xv_mixer *mixer) {

	struct device_node *layer_node;
	const char *vformat;
	int ret = 0;

   	layer_node = of_get_child_by_name(node, "layer_0");


	ret = of_property_read_string(layer_node, "xlnx,vformat", &vformat);


	if (ret) {
		DRM_ERROR("Failed to get mixer video format. Read %s from "
			"dt\n", vformat);
		return -1;
	}

	ret = of_property_read_u32(node, "xlnx,bpc", 
				   &(mixer->bg_layer_bpc));
	if (ret) {
		DRM_ERROR("Failed to get bits per component (bpc) prop\n");
		return -1;
	}

	ret = of_property_read_u32(layer_node, "xlnx,layer-width", 
                               &(mixer->layer_data[0].hw_config.max_width));
	if (ret) {
		DRM_ERROR("Failed to get screen width prop\n");
		return -1;
	}

	/* set global max width for mixer which will, ultimately, set the limit for the crtc */
	mixer->max_layer_width = mixer->layer_data[0].hw_config.max_width;


	ret = of_property_read_u32(layer_node, "xlnx,layer-height", 
                               &(mixer->layer_data[0].hw_config.max_height));
	if (ret) {
		DRM_ERROR("Failed to get screen height prop\n");
		return -1;
	}

	mixer->max_layer_height = mixer->layer_data[0].hw_config.max_height;

	/*We'll use the first layer instance to store data of the master layer*/
	mixer->layer_data[0].id = XVMIX_LAYER_MASTER;

	ret = xilinx_drm_mixer_string_to_fmt(vformat,
                                    &(mixer->layer_data[0].hw_config.vid_fmt));
	if(ret < 0) {
		DRM_ERROR("Invalid mixer video format in dt\n");
		return -1;
	}

	return ret;
}


int
xilinx_drm_mixer_mark_layer_active(struct xilinx_drm_plane *plane) {

	if(!plane->mixer_layer)
		return -ENODEV;

	mixer_layer_active(plane->mixer_layer) = true;

	return 0;
}


int
xilinx_drm_mixer_mark_layer_inactive(struct xilinx_drm_plane *plane) {

	if(!plane->mixer_layer)
		return -ENODEV;

	mixer_layer_active(plane->mixer_layer) = false;


	return 0;
}

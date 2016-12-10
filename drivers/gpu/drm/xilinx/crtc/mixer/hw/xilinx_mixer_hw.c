#include <linux/types.h>

#include "crtc/mixer/hw/xilinx_mixer_hw.h"
#include "crtc/mixer/hw/xilinx_video_mixer.h"


/************************** Constant Definitions *****************************/
#define XVMIX_MASK_ENABLE_ALL_LAYERS    (0x01FF)
#define XVMIX_MASK_DISABLE_ALL_LAYERS   (0)
#define XVMIX_REG_OFFSET                (8)


/* Pixel values in 8 bit resolution in YUV color space*/
static const u8 bkgndColorYUV[XVMIX_BKGND_LAST][3] =
{
	{  0, 128, 128}, //Black
	{255, 128, 128}, //White
	{ 76,  85, 255}, //Red
	{149,  43,  21}, //Green
	{ 29, 255, 107}  //Blue
};

/* Pixel values in RGB color space*/
static const u8 bkgndColorRGB[XVMIX_BKGND_LAST][3] =
{
	{0, 0, 0}, //Black
	{1, 1, 1}, //White
	{1, 0, 0}, //Red
	{0, 1, 0}, //Green
	{0, 0, 1}  //Blue
};

/************************** Function Prototypes ******************************/
static int is_window_valid(struct xv_mixer *mixer,
                            u32 new_x_pos, u32 new_y_pos,
                            u32 width, u32 height,	
			    xv_mixer_scale_factor scale);

/*****************************************************************************/
/**
* This function initializes the core instance.
* Sets initial state of mixer primary video layer and max height/width
* settings which should be retrieved from the device tree.
******************************************************************************/

void xilinx_mixer_init(struct xv_mixer *mixer)
{

	int i;
	xv_mixer_layer_id layer_id;
	struct xv_mixer_layer_data *layer_data;

	layer_data = xilinx_mixer_get_layer_data(mixer, XVMIX_LAYER_MASTER);

	xilinx_mixer_layer_disable(mixer, XVMIX_LAYER_ALL);

	xilinx_mixer_set_active_area(mixer,
				     layer_data->hw_config.max_width,
				     layer_data->hw_config.max_height);

  	reg_writel(mixer->reg_base_addr, 
		XV_MIX_CTRL_ADDR_HWREG_VIDEO_FORMAT_DATA, 2);

	xilinx_mixer_set_bkg_col(mixer, XVMIX_BKGND_BLUE, mixer->bg_layer_bpc);
	mixer->bg_color = XVMIX_BKGND_BLUE;

	for(i=0; i <= mixer->layer_cnt; i++) {

		layer_id = mixer->layer_data[i].id;
		layer_data = &(mixer->layer_data[i]);

		if (layer_id == XVMIX_LAYER_MASTER)
		    continue;

		xilinx_mixer_set_layer_window(mixer, layer_id, 0, 0, 
					      XVMIX_LAYER_WIDTH_MIN, 
					      XVMIX_LAYER_HEIGHT_MIN, 
					      0);

		if(mixer_layer_can_scale(layer_data)) 
		    xilinx_mixer_set_layer_scaling(mixer, layer_id, 0);

		if(mixer_layer_can_alpha(layer_data)) 
		    xilinx_mixer_set_layer_alpha(mixer, 
						layer_id,
						XVMIX_ALPHA_MAX);

	}

	xilinx_mixer_intrpt_disable(mixer);

	/* JPM TODO remove logo hack.  For testing */
	xilinx_mixer_logo_load(mixer,64,64,NULL,NULL,NULL);
	xilinx_mixer_start(mixer);

}


/*****************************************************************************/
/**
* This function enables interrupts in the core
*
*
******************************************************************************/
void xilinx_mixer_intrpt_enable(struct xv_mixer *mixer)
{

	void __iomem *reg_base_addr = mixer->reg_base_addr;
	u32 curr_val =  reg_readl(reg_base_addr, XV_MIX_CTRL_ADDR_IER);

	/* Enable Interrupts */
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_IER, 
	curr_val | XVMIX_IRQ_DONE_MASK);

	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_GIE, 0x1);

	/* Disable autostart bit */
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_AP_CTRL, 0x0);
}

/*****************************************************************************/
/**
* This function disables interrupts in the core
*
* @param  InstancePtr is a pointer to core instance to be worked upon
*
* @return none
*
******************************************************************************/
void xilinx_mixer_intrpt_disable(struct xv_mixer *mixer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	u32 curr_val =  reg_readl(reg_base_addr, XV_MIX_CTRL_ADDR_IER);

	/* Disable Interrupts */
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_IER, 
	curr_val & (~XVMIX_IRQ_DONE_MASK));

	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_GIE, 0);

	/* Set autostart bit */
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_AP_CTRL, 0x80);
}

/*****************************************************************************/
/**
* This function starts the core instance
*
* @param  InstancePtr is a pointer to core instance to be worked upon
*
* @return none
*
******************************************************************************/
/* JPM TODO consider adding boolean param to indicate if free-running mode is
 * desired.  Right now, I'm defaulting to free running mode */
void xilinx_mixer_start(struct xv_mixer *mixer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	u32 curr_val;

	curr_val = reg_readl(reg_base_addr, XV_MIX_CTRL_ADDR_AP_CTRL) & 0x80;
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_AP_CTRL, curr_val | 0x81);
}

/*****************************************************************************/
/**
* This function stops the core instance
*
******************************************************************************/
void xilinx_mixer_stop(struct xv_mixer *mixer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;

	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_AP_CTRL, 0);
}

/*****************************************************************************/
/**
* This function validates if the requested window is within the frame boundary
*
******************************************************************************/
static int is_window_valid(struct xv_mixer *mixer,
			u32 new_x_pos, u32 new_y_pos, u32 width, u32 height,	
			xv_mixer_scale_factor scale)
{
	struct xv_mixer_layer_data *ld;
	int scale_factor[3] = {1,2,4};

	ld = xilinx_mixer_get_layer_data(mixer, XVMIX_LAYER_MASTER);

	/* Check if window scale factor is set */
	if(scale < XVMIX_SCALE_FACTOR_NOT_SUPPORTED) {
		/* update window per scale factor before validating */
		  width  *= scale_factor[scale];
		  height *= scale_factor[scale];
	  }

	if((new_x_pos >= 0) && (new_y_pos >= 0) && 
		((new_x_pos + width)  <= ld->layer_regs.width) &&
		 ((new_y_pos + height) <= ld->layer_regs.height)) {

		return 0;
	} 
	return -1;
}

/*****************************************************************************/
/**
* This function configures the mixer input stream
*
******************************************************************************/
int xilinx_mixer_set_active_area(struct xv_mixer *mixer, 
                                  u32 hactive, u32 vactive)
{
	struct xv_mixer_layer_data *ld = 
		    xilinx_mixer_get_layer_data(mixer, XVMIX_LAYER_MASTER);

	void __iomem *reg_base_addr = mixer->reg_base_addr;

	if(hactive > ld->hw_config.max_width || 
	   vactive > ld->hw_config.max_height)
	return -1;

	/* set resolution */
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_HWREG_HEIGHT_DATA, vactive);
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_HWREG_WIDTH_DATA, hactive);

	ld->layer_regs.width  = hactive;
	ld->layer_regs.height = vactive;

	return 0;
}

/*****************************************************************************/
/**
* This function enables the specified layer of the core instance
*
******************************************************************************/
void xilinx_mixer_layer_enable(struct xv_mixer *mixer, 
                               xv_mixer_layer_id layer_id)
{
	u32 num_layers = mixer->layer_cnt;
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	u32 curr_state;

	/* Ensure layer is marked as 'active' by application before
	 * turning on in hardware.  In some cases, layer register data
	 * may be written to otherwise inactive layes in lieu of, eventually,
	 * turning them on.
	*/
	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	if(layer_data) {
		if(!mixer_layer_active(layer_data))
			return;
	} else {
		return;
	}

	  /*Check if request is to enable all layers or single layer*/
	if(layer_id == XVMIX_LAYER_ALL) {
		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA,
			XVMIX_MASK_ENABLE_ALL_LAYERS);
	  
	} else if((layer_id < num_layers) ||
		  ((layer_id == XVMIX_LAYER_LOGO) && 
						mixer->logo_layer_enabled)) {

			curr_state = reg_readl(reg_base_addr,
					XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA);

		curr_state |= (1<<layer_id);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA, 
			curr_state);
	  }

}

/*****************************************************************************/
/**
* This function disables the specified layer of the core instance
*
******************************************************************************/
void xilinx_mixer_layer_disable(struct xv_mixer *mixer, 
                                xv_mixer_layer_id layer_id)
{
	u32 num_layers, curr_state;
	void __iomem *reg_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int i;

	num_layers = mixer->layer_cnt;

	if(layer_id == XVMIX_LAYER_ALL) {

		reg_writel(reg_addr, 
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA,
			XVMIX_MASK_DISABLE_ALL_LAYERS);

  	}
  	else if((layer_id < num_layers) || 
		((layer_id == XVMIX_LAYER_LOGO) && 
						(mixer->logo_layer_enabled))) {

    		curr_state = reg_readl(reg_addr, 
                                   XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA);

    		curr_state &= ~(1 << layer_id);

		reg_writel(reg_addr, 
                       XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA,
                       curr_state);
  	}
}

/*****************************************************************************/
/**
*
* This function returns state of the specified layer [enabled or disabled]
*
* @param    InstancePtr is a pointer to the core instance.
* @param    LayerId is the layer index for which information is requested
*
* @return   Enabled(1)/Disabled(0)
*
******************************************************************************/
int xilinx_mixer_is_layer_enabled(struct xv_mixer *mixer, 
				xv_mixer_layer_id layer_id)
{
	u32 state, mask;
	mask = (1<<layer_id);
	state = reg_readl(mixer->reg_base_addr, 
			    XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA);
	return ((state & mask) ? 1 : 0);
}

/*****************************************************************************/
/**
* This function sets the background color to be displayed when stream layer is
* disabled
*
******************************************************************************/
void xilinx_mixer_set_bkg_col(struct xv_mixer *mixer, 
				xv_mixer_bkg_color_id col_id,
				xv_comm_colordepth bpc)
{
	u16 y_r_val;
	u16 u_g_val;
	u16 v_b_val;
	u16 scale;

#if 0   /* JPM mixer is always in RGB mode for bg layer data */
	xv_comm_color_fmt_id col_fmt;
	col_fmt = reg_readl(mixer->reg_base_addr, 
		XV_MIX_CTRL_ADDR_HWREG_VIDEO_FORMAT_DATA);

	if(col_fmt == XVIDC_CSF_RGB) {
		scale = ((1<<bpc)-1);
		y_r_val = bkgndColorRGB[col_id][0] * scale;
		u_g_val = bkgndColorRGB[col_id][1] * scale;
		v_b_val = bkgndColorRGB[col_id][2] * scale;
	}
	else {/*YUV*/
		scale =  (1<<(bpc-XVIDC_BPC_8));
		y_r_val = bkgndColorYUV[col_id][0] * scale;
		u_g_val = bkgndColorYUV[col_id][1] * scale;
		v_b_val = bkgndColorYUV[col_id][2] * scale;
	}
#endif
	scale = ((1<<bpc)-1);
	y_r_val = bkgndColorRGB[col_id][0] * scale;
	u_g_val = bkgndColorRGB[col_id][1] * scale;
	v_b_val = bkgndColorRGB[col_id][2] * scale;

	/* Set Background Color */
	reg_writel(mixer->reg_base_addr, 
		XV_MIX_CTRL_ADDR_HWREG_BACKGROUND_Y_R_DATA, y_r_val);
	reg_writel(mixer->reg_base_addr,
		XV_MIX_CTRL_ADDR_HWREG_BACKGROUND_U_G_DATA, u_g_val);
	reg_writel(mixer->reg_base_addr,
		XV_MIX_CTRL_ADDR_HWREG_BACKGROUND_V_B_DATA, v_b_val);
}

/*****************************************************************************/
/**
* This function configures the window coordinates of the specified layer
*
* @param  InstancePtr is a pointer to core instance to be worked upon
* @param  LayerId is the layer for which window coordinates are to be set
* @param  Win is the window coordinates in pixels
* @param  StrideInBytes is the stride of the requested window
*           yuv422 Color space requires 2 Bytes/Pixel
*           yuv444 Color space requires 4 Bytes/Pixel
*           Equation to compute stride is as follows
*              Stride = (Window_Width * (YUV422 ? 2 : 4))
*           (Applicable only when layer type is Memory)
*
* @note   Applicable only for Layer1-7 and Logo Layer
*
******************************************************************************/
int xilinx_mixer_set_layer_window(struct xv_mixer *mixer,
                                  xv_mixer_layer_id layer_id,
                                  u32 x_pos, u32 y_pos, 
                                  u32 win_width, u32 win_height,
                                  u32 stride_bytes)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	xv_mixer_scale_factor scale = 0;
	int status = 0;
	bool win_valid = false;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	if(!layer_data)
		return (-1);

	/* Check window coordinates */
	scale = xilinx_mixer_get_layer_scaling(mixer, layer_id);

	if(is_window_valid(mixer, x_pos, y_pos, win_width, win_height, scale)){
		return(-1);
	}

	switch(layer_id) {
		case XVMIX_LAYER_LOGO:
		if(mixer->logo_layer_enabled && 
		   win_width <= layer_data->hw_config.max_width &&
	 	   win_height <= layer_data->hw_config.max_height) {

			reg_writel(reg_base_addr,
			    XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTX_DATA, x_pos);

			reg_writel(reg_base_addr,
			    XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTY_DATA, y_pos);

			reg_writel(reg_base_addr,
			    XV_MIX_CTRL_ADDR_HWREG_LOGOWIDTH_DATA, win_width);

			reg_writel(reg_base_addr,
			    XV_MIX_CTRL_ADDR_HWREG_LOGOHEIGHT_DATA, win_height);

			layer_data->layer_regs.x_pos = x_pos;
			layer_data->layer_regs.y_pos = y_pos;

			layer_data->layer_regs.width = win_width;
			layer_data->layer_regs.height = win_height;

		} else {
			status = -1;
		}
		break;

		default: //Layer1-Layer7
		if(layer_id < mixer->layer_cnt) {

			if(win_width <= layer_data->hw_config.max_width) {

			/* Check layer interface is Stream or Memory */
			if(layer_data->hw_config.is_streaming) {

			/* Stride is not required for stream layer */
				win_valid = true;
			} else {
			/* Check if stride is aligned to aximm width (2*PPC*32-bits) */
				u32 align = 2 * mixer->ppc * 4;
				if((stride_bytes % align) != 0) {
					win_valid = false;
					status   = -1;
				} else {
					win_valid = true;
				}
			}

			if(win_valid) {

				u32 offset = layer_id * XVMIX_REG_OFFSET;

				reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTX_0_DATA + offset), 
				x_pos);

				reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTY_0_DATA + offset),
				y_pos);

				reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERWIDTH_0_DATA + offset),
				win_width);

				reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERHEIGHT_0_DATA + offset),
				win_height);


				layer_data->layer_regs.x_pos = x_pos;
				layer_data->layer_regs.y_pos = y_pos;

				layer_data->layer_regs.width = win_width;
				layer_data->layer_regs.height = win_height;

				if(!(layer_data->hw_config.is_streaming)) {

					reg_writel(reg_base_addr,
					(XV_MIX_CTRL_ADDR_HWREG_LAYERSTRIDE_0_DATA + offset),
					stride_bytes);
				}

				status = 0;
			}
		}
		} else {
			status = -1;
		}

		break;
	}

	return(status);
}

/*****************************************************************************/
/**
* This function read the window coordinates of the specified layer
*
******************************************************************************/
int xilinx_mixer_get_layer_window(struct xv_mixer *mixer,
	                              xv_mixer_layer_id layer_id)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int status = 0;
	u32 *x_pos, *y_pos, *win_width, *win_height;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	x_pos 	= &(mixer_layer_x_pos(layer_data));
	y_pos	= &(mixer_layer_y_pos(layer_data));

	win_width  = &(mixer_layer_width(layer_data));
	win_height = &(mixer_layer_height(layer_data));


	switch(layer_id) {
	case XVMIX_LAYER_LOGO:
		if(mixer->logo_layer_enabled) {

			*x_pos = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTX_DATA);
			*y_pos = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTY_DATA);
			*win_width = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOWIDTH_DATA);
			*win_height = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOHEIGHT_DATA);

			status = 0;
		} else {
			status = -1;
		}
		break;

	default: //Layer1-Layer7
		if(layer_id < mixer->layer_cnt) {

			u32 offset = layer_id * XVMIX_REG_OFFSET;

			*x_pos = reg_readl(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTX_0_DATA + offset));
			*y_pos = reg_readl(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTY_0_DATA + offset));
			*win_width  = reg_readl(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERWIDTH_0_DATA + offset));
			*win_height = reg_readl(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERHEIGHT_0_DATA + offset));

			status = 0;
		} else {
			status = -1;
		}
		break;
	}

	return(status);
}

/*****************************************************************************/
/**
* This function moved the window position of the specified layer to new
* corrdinates
*
* @param  InstancePtr is a pointer to core instance to be worked upon
* @param  LayerId is the layer for which window position is to be set
* @param  StartX is the new X position
* @param  StartY is the new Y position
*
* @return XST_SUCCESS if command is successful else error code with reason
*
* @note   Applicable only for Layer1-7 and Logo Layer
*
******************************************************************************/
int xilinx_mixer_move_layer_window(struct xv_mixer *mixer,
                          	xv_mixer_layer_id layer_id,
                    	  	u32 new_x_pos,
                     	  	u32 new_y_pos)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int status = 0;
	u32 win_status = 0;
	u32 scale_val = 0;
	u32 *org_y, *org_x, *win_width, *win_height;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);
	/* Current window settings for layer */ 

	org_x = &(mixer_layer_x_pos(layer_data));
	org_y = &(mixer_layer_y_pos(layer_data));

	win_width = &(mixer_layer_width(layer_data));
	win_height = &(mixer_layer_height(layer_data));

	/* Update window settings in cache from hardware */
	win_status = xilinx_mixer_get_layer_window(mixer, layer_id);
	if(win_status != 0) {
		return(win_status);
	}

	/* Get scale factor */
	scale_val = xilinx_mixer_get_layer_scaling(mixer, layer_id);
	/* Validate new start position will not cause the layer window
	* to go out of scope
	*/
	if(is_window_valid(mixer, new_x_pos, new_y_pos, 
		*win_width, *win_height, scale_val)) {
		return(-1);
	}

	switch(layer_id) {
	case XVMIX_LAYER_LOGO:
		if(mixer->logo_layer_enabled) {

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTX_DATA,
				new_x_pos);

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTY_DATA,
				new_y_pos);

			*org_x = new_x_pos;
			*org_y = new_y_pos;

			status = 0;
		}
		break;

	default: //Layer1-Layer7
		if(layer_id < mixer->layer_cnt) {
			u32 offset;

			offset = layer_id * XVMIX_REG_OFFSET;

			reg_writel(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTX_0_DATA + offset),
			new_x_pos);

			reg_writel(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTY_0_DATA + offset),
			new_y_pos);

			*org_x = new_x_pos;
			*org_y = new_y_pos;

			status = 0;
		}
		break;
	}
	return(status);
}

/*****************************************************************************/
/**
* This function configures the scaling factor of the specified layer
*
* Applicable only for Layer1-7 and Logo Layer
*
******************************************************************************/
int xilinx_mixer_set_layer_scaling(struct xv_mixer *mixer,
                                   xv_mixer_layer_id layer_id,
                                   xv_mixer_scale_factor scale)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data; 
	
	int status = 0;
	int win_status = 0;
	u32 layer_x_pos, layer_y_pos, layer_width, layer_height;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	/* Validate if scaling will cause the layer window to go out of scope */
	win_status = xilinx_mixer_get_layer_window(mixer, layer_id);
	if(win_status != 0) {
		return(win_status);
	}
  
	layer_x_pos = mixer_layer_x_pos(layer_data);
	layer_y_pos = mixer_layer_y_pos(layer_data);

	layer_width  = mixer_layer_width(layer_data);
	layer_height = mixer_layer_height(layer_data);

	if(is_window_valid(mixer, layer_x_pos, layer_y_pos,
		      layer_width, layer_height, scale)) {
		return(-1);
	}

	switch(layer_id) {
	case XVMIX_LAYER_LOGO:
		if(mixer->logo_layer_enabled) {
			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSCALEFACTOR_DATA,
				scale);

			layer_data->layer_regs.scale_fact = scale;

			status = 0;
		}
		break;

	default: //Layer0-Layer7
		if(layer_id < mixer->layer_cnt && 
			mixer_layer_can_scale(layer_data)) {
		
			u32 offset = layer_id * XVMIX_REG_OFFSET;
			reg_writel(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERSCALEFACTOR_0_DATA+offset),
			scale);

			layer_data->layer_regs.scale_fact = scale;

			status = 0;
		}
		break;
	}
	return(status);
}

/*****************************************************************************/
/**
* This function returns the scaling factor of the specified layer
*
* Applicable only for Layer1-7 and Logo Layer
*
******************************************************************************/
int xilinx_mixer_get_layer_scaling(struct xv_mixer *mixer, 
				   xv_mixer_layer_id layer_id)
{
	int scale_factor = 0;
	struct xv_mixer_layer_data *layer_data = 
		xilinx_mixer_get_layer_data(mixer, layer_id);

	if(layer_id == XVMIX_LAYER_LOGO)

	switch(layer_id) {
	case XVMIX_LAYER_LOGO:
		if(mixer->logo_layer_enabled) {
			scale_factor = reg_readl(mixer->reg_base_addr, 
				XV_MIX_CTRL_ADDR_HWREG_LOGOSCALEFACTOR_DATA);
			layer_data->layer_regs.scale_fact = scale_factor; 
		}
		break;

	default: //Layer0-Layer7
		if((layer_id < XVMIX_LAYER_LOGO) && 
					mixer_layer_can_scale(layer_data)){

			u32 base_reg;

			base_reg = XV_MIX_CTRL_ADDR_HWREG_LAYERSCALEFACTOR_0_DATA;
			scale_factor = reg_readl(mixer->reg_base_addr,
				(base_reg + (layer_id * XVMIX_REG_OFFSET)));
			layer_data->layer_regs.scale_fact = scale_factor;
		}
		break;
	}
	return scale_factor;
}

/*****************************************************************************/
/**
* This function configures the Alpha level of the specified layer
*
* Applicable only for Layer1-7 and Logo Layer
*
******************************************************************************/
int xilinx_mixer_set_layer_alpha(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id,
                             	u32 alpha)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int status = 0;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	switch(layer_id) {
	case XVMIX_LAYER_LOGO:
		if(mixer->logo_layer_enabled) {

			reg_writel(reg_base_addr,
			    XV_MIX_CTRL_ADDR_HWREG_LOGOALPHA_DATA, alpha);

			layer_data->layer_regs.alpha = alpha;

			status = 0;

		} else {
			status = -1;
		}
		break;

	default: //Layer1-Layer7
		if((layer_id < mixer->layer_cnt) &&
					mixer_layer_can_alpha(layer_data)){

			u32 offset =  layer_id * XVMIX_REG_OFFSET;
			reg_writel(reg_base_addr,
			    (XV_MIX_CTRL_ADDR_HWREG_LAYERALPHA_0_DATA + offset),
			    alpha);

			layer_data->layer_regs.alpha = alpha;

			status = 0;
		} else {
			status = -1;
		}
		break;
	}
	return(status);
}

/*****************************************************************************/
/**
* This function returns the alpha of the specified layer
*
*
******************************************************************************/
int xilinx_mixer_get_layer_alpha(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id, u32 *reg_val)
{ 

	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int status = -1;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	switch(layer_id) {
	case XVMIX_LAYER_LOGO:
		if(mixer->logo_layer_enabled) {

			*reg_val = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOALPHA_DATA);
			status = 0;
			layer_data->layer_regs.alpha = *reg_val;
		}
		break;

	default: //Layer1-Layer7
		if((layer_id < mixer->layer_cnt) &&
					mixer_layer_can_alpha(layer_data)){

			u32 offset = layer_id * XVMIX_REG_OFFSET;
			*reg_val = reg_readl(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERALPHA_0_DATA + offset));
			layer_data->layer_regs.alpha = *reg_val;
			status = 0;
		}
		break;
	}
	return(status);
}

/*****************************************************************************/
/**
* This function reads the color format of the specified layer
*
******************************************************************************/
int xilinx_mixer_get_layer_colorspace_fmt(struct xv_mixer *mixer,
                              xv_mixer_layer_id layer_id,
                              xv_comm_color_fmt_id *Cfmt)
{
	struct xv_mixer_layer_data *layer_data;
	int status = -1;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	if(layer_id <= mixer->layer_cnt) {

		*Cfmt = layer_data->hw_config.vid_fmt;
		status = 0;
	}

	return(status);
}

/*****************************************************************************/
/**
* This function sets the buffer address of the specified layer
*
* @param  InstancePtr is a pointer to core instance to be worked upon
* @param  LayerId is the layer to be updated
* @param  Addr is the absolute addrees of buffer in memory
*
* @return XST_SUCCESS or XST_FAILURE
*
* @note   Applicable only for Layer1-7
*
******************************************************************************/
int xilinx_mixer_set_layer_buff_addr(struct xv_mixer *mixer,
				     xv_mixer_layer_id layer_id,
				     u32 buff_addr)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	int status = 0;
	u32 align;
	u32 win_valid = 0;

	if(layer_id < mixer->layer_cnt) {
	/* Check if addr is aligned to aximm width (2*PPC*32-bits (4Bytes)) */
		align = 2 * mixer->ppc * 4;
		if((buff_addr % align) != 0) {
			 win_valid = 0;
			 status   = -1;
		} else {
			win_valid = 1;
		}
		if(win_valid) {

			u32 offset = (layer_id-1) * XVMIX_REG_OFFSET;

			reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYER1_V_DATA + offset),
				buff_addr);

			mixer->layer_data[layer_id].layer_regs.buff_addr = buff_addr;
		}
	}
	return(status);
}

/*****************************************************************************/
/**
* This function reads the buffer address of the specified layer
*
*
******************************************************************************/
int xilinx_mixer_get_layer_buff_addr(struct xv_mixer *mixer,
                             xv_mixer_layer_id layer_id, u32 buff_addr)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	int status = -1;

	if(layer_id < mixer->layer_cnt) {

		u32 offset = (layer_id-1) * XVMIX_REG_OFFSET; 

		buff_addr = reg_readl(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYER1_V_DATA + offset));
		status = 0;
	}
	return(status);
}

/*****************************************************************************/
/**
* This function sets the logo layer color key data
*
******************************************************************************/
int xilinx_mixer_set_logo_color_key(struct xv_mixer *mixer)
{

	int status = -1;

	if(mixer->logo_layer_enabled && mixer->logo_color_key_enabled) {

		void __iomem *reg_base_addr = mixer->reg_base_addr;
		u8 *rgb_min = mixer->logo_color_key.rgb_min;
		u8 *rgb_max = mixer->logo_color_key.rgb_max;

		reg_writel(reg_base_addr, 
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_R_DATA,
			rgb_min[0]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_G_DATA,
			rgb_min[1]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_B_DATA,
			rgb_min[2]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_R_DATA,
			rgb_max[0]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_G_DATA,
			rgb_max[1]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_B_DATA,
			rgb_max[2]);

		status = 0;
	}

	return status;
}

/*****************************************************************************/
/**
* This function reads the logo layer color key data
*
* @param  InstancePtr is a pointer to core instance to be worked upon
* @param  ColorKeyData is the structure that holds return min/max values
*
* @return XST_SUCCESS or XST_FAILURE
*
* @note   none
*
******************************************************************************/
int xilinx_mixer_get_logo_color_key(struct xv_mixer *mixer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	u8 *rgb_min = mixer->logo_color_key.rgb_min;
	u8 *rgb_max = mixer->logo_color_key.rgb_max;
	int status = -1;

	if(mixer->logo_layer_enabled && mixer->logo_color_key_enabled) {

		rgb_min[0] = reg_readl(reg_base_addr, 
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_R_DATA);

		rgb_min[1] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_G_DATA);

		rgb_min[2] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_B_DATA);

		rgb_max[0] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_R_DATA);

		rgb_max[1] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_G_DATA);

		rgb_max[2] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_B_DATA);

		status = 0;
	}
	return status;
}

/*****************************************************************************/
/**
* This function loads the logo data into core BRAM
*
* @param  InstancePtr is a pointer to core instance to be worked upon
* @param  Win is logo window (logo width must be multiple of 4 bytes)
* @param  RBuffer is the pointer to Red buffer
* @param  GBuffer is the pointer to Green buffer
* @param  BBuffer is the pointer to Blue buffer
*
* @return XST_SUCCESS or XST_FAILURE
*
* @note   none
*
******************************************************************************/

/* JPM TODO REMOVE THIS HACK INCLUDE ASAP */
#include "logo_img.c"


int xilinx_mixer_logo_load(struct xv_mixer *mixer,
                   	u32 logo_w, u32 logo_h,
                   	u8 *r_buffer,
                   	u8 *g_buffer,
                   	u8 *b_buffer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;

	/* JPM TODO REMOVE THIS HACK ASAP */
	r_buffer = (u8*)&Logo_R;
	g_buffer = (u8*)&Logo_G;
	b_buffer = (u8*)&Logo_B;
	/* END HACK */

	int status = 0;
	int x,y;
	u32 rword, gword, bword;
	u32 width, height, curr_x_pos, curr_y_pos;
	u32 rbase_addr, gbase_addr, bbase_addr;

	layer_data = xilinx_mixer_get_layer_data(mixer, XVMIX_LAYER_LOGO);

	if(!layer_data)
		return -1;

	if(mixer->logo_layer_enabled && 
		logo_w <= layer_data->hw_config.max_width &&
		logo_h <= layer_data->hw_config.max_height) {

		width  = logo_w;
		height = logo_h; 

		rbase_addr = XV_MIX_CTRL_ADDR_HWREG_LOGOR_V_BASE;
		gbase_addr = XV_MIX_CTRL_ADDR_HWREG_LOGOG_V_BASE;
		bbase_addr = XV_MIX_CTRL_ADDR_HWREG_LOGOB_V_BASE;

		for (y=0; y<height; y++) {
			for (x=0; x<width; x+=4) {
				rword = (u32)r_buffer[y*width+x] |
				(((u32)r_buffer[y*width+x+1])<<8) |
				(((u32)r_buffer[y*width+x+2])<<16) |
				(((u32)r_buffer[y*width+x+3])<<24);

				gword = (u32)g_buffer[y*width+x] |
				(((u32)g_buffer[y*width+x+1])<<8) |
				(((u32)g_buffer[y*width+x+2])<<16) |
				(((u32)g_buffer[y*width+x+3])<<24);

				bword = (u32)b_buffer[y*width+x] |
				(((u32)b_buffer[y*width+x+1])<<8) |
				(((u32)b_buffer[y*width+x+2])<<16) |
				(((u32)b_buffer[y*width+x+3])<<24);

				reg_writel(reg_base_addr,
					(rbase_addr+(y*width+x)), rword);
				reg_writel(reg_base_addr,
					(gbase_addr+(y*width+x)), gword);
				reg_writel(reg_base_addr,
					(bbase_addr+(y*width+x)), bword);
			}
		}

		mixer->logo_rgb_buffers.r_buffer = r_buffer;
		mixer->logo_rgb_buffers.g_buffer = g_buffer;
		mixer->logo_rgb_buffers.b_buffer = b_buffer;
 
		curr_x_pos = mixer_layer_x_pos(layer_data);
		curr_y_pos = mixer_layer_y_pos(layer_data);

		status = xilinx_mixer_set_layer_window(mixer, XVMIX_LAYER_LOGO,
				curr_x_pos, curr_y_pos, logo_w, logo_h, 0);
	}
	else {
		status = -1;
	}
	return(status);
}


struct xv_mixer_layer_data* xilinx_mixer_get_layer_data(struct xv_mixer *mixer,
                                                        xv_mixer_layer_id id)
{
	int i;
	struct xv_mixer_layer_data *layer_data;
	for(i=0; i <= (mixer->layer_cnt -1); i++) {
		layer_data = &(mixer->layer_data[i]);
		if(layer_data->id == id)
		return layer_data;
	}
	return NULL;
}
#if 0
/*****************************************************************************/
/**
* This function reports the mixer status
*
* @param  InstancePtr is a pointer to core instance to be worked upon
*
* @return none
*
* @note   none
*
******************************************************************************/
void XVMix_DbgReportStatus(XV_Mix_l2 *InstancePtr)
{
  XV_mix *MixPtr;
  u32 index, IsEnabled, ctrl;
  const char *Status[2] = {"Disabled", "Enabled"};

  Xil_AssertVoid(InstancePtr != NULL);

  xil_printf("\r\n\r\n----->MIXER STATUS<----\r\n");
  MixPtr = &InstancePtr->Mix;

  ctrl  = XV_mix_ReadReg(MixPtr->Config.BaseAddress, XV_MIX_CTRL_ADDR_AP_CTRL);

  xil_printf("Pixels Per Clock: %d\r\n", InstancePtr->Mix.Config.PixPerClk);
  xil_printf("Color Depth:      %d\r\n", InstancePtr->Mix.Config.MaxDataWidth);
  xil_printf("Number of Layers: %d\r\n",XVMix_GetNumLayers(InstancePtr));
  xil_printf("Control Reg:      0x%x\r\n", ctrl);
  xil_printf("Layer Enable Reg: 0x%x\r\n\r\n",XV_mix_Get_HwReg_layerEnable(MixPtr));

  IsEnabled = XVMix_IsLayerEnabled(InstancePtr, XVMIX_LAYER_MASTER);
  xil_printf("Layer Master: %s\r\n", Status[IsEnabled]);
  for(index = XVMIX_LAYER_1; index<XVMIX_LAYER_LOGO; ++index) {
      xil_printf("Layer %d     : %s\r\n" ,index,
              Status[(XVMix_IsLayerEnabled(InstancePtr, index))]);
  }
  IsEnabled = XVMix_IsLayerEnabled(InstancePtr, XVMIX_LAYER_LOGO);
  xil_printf("Layer Logo  : %s\r\n\r\n", Status[IsEnabled]);

  xil_printf("Background Color Y/R: %d\r\n", XV_mix_Get_HwReg_background_Y_R(MixPtr));
  xil_printf("Background Color U/G: %d\r\n", XV_mix_Get_HwReg_background_U_G(MixPtr));
  xil_printf("Background Color V/B: %d\r\n\r\n", XV_mix_Get_HwReg_background_V_B(MixPtr));
}

/*****************************************************************************/
/**
* This function reports the mixer status of the specified layer
*
* @param  InstancePtr is a pointer to core instance to be worked upon
* @param  LayerId is the layer to be updated
*
* @return none
*
* @note   none
*
******************************************************************************/
void XVMix_DbgLayerInfo(XV_Mix_l2 *InstancePtr, XVMix_LayerId LayerId)
{
  XV_mix *MixPtr;
  u32 index, IsEnabled;
  u32 ReadVal;
  XVidC_VideoWindow Win;
  XVidC_ColorFormat ColFormat;
  XVMix_LayerType LayerType;
  char *Status[2] = {"Disabled", "Enabled"};
  char *ScaleFactor[3] = {"1x", "2x", "4x"};
  char *IntfType[2] = {"Memory", "Stream"};

  Xil_AssertVoid(InstancePtr != NULL);
  Xil_AssertVoid((LayerId >= XVMIX_LAYER_MASTER) &&
                 (LayerId <= XVMIX_LAYER_LOGO));

  MixPtr = &InstancePtr->Mix;
  IsEnabled = XVMix_IsLayerEnabled(InstancePtr, LayerId);

  switch(LayerId) {
    case XVMIX_LAYER_MASTER:
        xil_printf("\r\n\r\n----->Master Layer Status<----\r\n");
        xil_printf("State: %s\r\n", Status[IsEnabled]);
        if(IsEnabled) {
		  u32 width, height;

		  XVMix_GetLayerColorFormat(InstancePtr, LayerId, &ColFormat);
          width  = XV_mix_Get_HwReg_width(&InstancePtr->Mix);
          height = XV_mix_Get_HwReg_height(&InstancePtr->Mix);
          xil_printf("Color Format: %s\r\n\r\n",
                     XVidC_GetColorFormatStr(ColFormat));
          xil_printf("Resolution: %d x %d\r\n", width, height);
          xil_printf("Stream Info->\r\n");
          XVidC_ReportStreamInfo(&InstancePtr->Stream);
        }
	    break;

    case XVMIX_LAYER_LOGO:
	    xil_printf("\r\n\r\n----->Layer LOGO Status<----\r\n");
        xil_printf("State: %s\r\n", Status[IsEnabled]);
        if(IsEnabled) {

          ReadVal = XVMix_GetLayerAlpha(InstancePtr, LayerId);
          xil_printf("Alpha: %d\r\n", ReadVal);
          ReadVal = XVMix_GetLayerScaleFactor(InstancePtr, LayerId);
          xil_printf("Scale: %s\r\n\r\n", ScaleFactor[ReadVal]);
          xil_printf("Window Data: \r\n");
          XVMix_GetLayerWindow(InstancePtr, LayerId, &Win);
          xil_printf("   Start X    = %d\r\n", Win.StartX);
          xil_printf("   Start Y    = %d\r\n", Win.StartY);
          xil_printf("   Win Width  = %d\r\n", Win.Width);
          xil_printf("   Win Height = %d\r\n", Win.Height);

		  IsEnabled = XVMix_IsLogoColorKeyEnabled(InstancePtr);
		  if(IsEnabled) {
		    XVMix_LogoColorKey Data;

		    XVMix_GetLogoColorKey(InstancePtr, &Data);
            xil_printf("\r\nColor Key Data: \r\n");
            xil_printf("     Min    Max\r\n");
            xil_printf("    -----  -----\r\n");
            xil_printf("  R: %3d    %3d\r\n", Data.RGB_Min[0],Data.RGB_Max[0]);
            xil_printf("  G: %3d    %3d\r\n", Data.RGB_Min[1],Data.RGB_Max[1]);
            xil_printf("  B: %3d    %3d\r\n", Data.RGB_Min[2],Data.RGB_Max[2]);
		  } else {
            xil_printf("Color Key: %s\r\n", Status[IsEnabled]);
		  }
        }
	    break;

    default: //Layer1-7
        LayerType = XVMix_GetLayerInterfaceType(InstancePtr, LayerId);
        xil_printf("\r\n\r\n----->Layer %d Status<----\r\n", LayerId);
        xil_printf("State: %s\r\n", Status[IsEnabled]);
        xil_printf("Type : %s\r\n", IntfType[LayerType]);
        if(IsEnabled) {
		  u32 Stride, Reg, Offset;

          xil_printf("Addr : 0x%x\r\n",
                       XVMix_GetLayerBufferAddr(InstancePtr, LayerId));

          IsEnabled = XVMix_IsAlphaEnabled(InstancePtr, LayerId);
          if (IsEnabled) {
              ReadVal = XVMix_GetLayerAlpha(InstancePtr, LayerId);
              xil_printf("Alpha: %d\r\n", ReadVal);
          } else {
              xil_printf("Alpha: %s\r\n", Status[IsEnabled]);
          }

          IsEnabled = XVMix_IsScalingEnabled(InstancePtr, LayerId);
          if (IsEnabled) {
              ReadVal = XVMix_GetLayerScaleFactor(InstancePtr, LayerId);
              xil_printf("Scale: %s\r\n", ScaleFactor[ReadVal]);
          } else {
              xil_printf("Scale: %s\r\n", Status[IsEnabled]);
          }

		  XVMix_GetLayerColorFormat(InstancePtr, LayerId, &ColFormat);
          xil_printf("Color Format: %s\r\n\r\n",
                          XVidC_GetColorFormatStr(ColFormat));

          xil_printf("Window Data: \r\n");
          Reg = XV_MIX_CTRL_ADDR_HWREG_LAYERSTRIDE_0_DATA;
          Offset = LayerId*XVMIX_REG_OFFSET;
          Stride = XV_mix_ReadReg(MixPtr->Config.BaseAddress,
                                  (Reg+Offset));

          XVMix_GetLayerWindow(InstancePtr, LayerId, &Win);
          xil_printf("   Start X    = %d\r\n", Win.StartX);
          xil_printf("   Start Y    = %d\r\n", Win.StartY);
          xil_printf("   Win Width  = %d\r\n", Win.Width);
          xil_printf("   Win Height = %d\r\n", Win.Height);
          xil_printf("   Win Stride = %d\r\n", Stride);
        } //Layer State
	    break;
  }
}

/** @} */
#endif

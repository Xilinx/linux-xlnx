/*
 * Xylon logiCVC frame buffer driver platform data structures
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * 2012 (c) Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef	__XYLON_FB_PLATFORM_H__
#define __XYLON_FB_PLATFORM_H__


#include <linux/types.h>


/* Framebuffer driver platform layer structure */
struct xylonfb_platform_layer_params
{
	/* Layer memory offset in lines */
	unsigned int offset;
	/* Layer buffer memory offset in lines */
	unsigned short buffer_offset;
	/* Layer bits per pixel */
	unsigned char bpp;
	/* Layer alpha mode */
	unsigned char alpha_mode;
	/* Layer control register value */
	unsigned char ctrl;
};

/* Framebuffer driver platform data structure */
struct xylonfb_platform_data
{
	struct xylonfb_platform_layer_params *layer_params;
	/* logiCVC Control Register value */
	u32 ctrl_reg;
	/* Physical starting address of the video memory */
	unsigned long vmem_base_addr;
	/* Physical ending address of the video memory */
	unsigned long vmem_high_addr;
	/* Layer row stride in pixels */
	unsigned short row_stride;
	/* Number of logiCVC layers */
	unsigned char num_layers;
	/* logiCVC layer ID for FB console */
	unsigned char active_layer;
	/* Background layer bits per pixel */
	unsigned char bg_layer_bpp;
	/* Background layer alpha mode */
	unsigned char bg_layer_alpha_mode;
};

#endif /* __XYLON_FB_PLATFORM_H__ */

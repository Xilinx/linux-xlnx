/*
 * Xylon logiCVC frame buffer driver
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

#ifndef __XYLON_FB_H__
#define __XYLON_FB_H__


/* Framebuffer driver platform layer structure */
struct xylonfb_platform_layer_params
{
	unsigned long offset;	/* Layer memory offset in lines */
	unsigned char bpp;		/* Layer bits per pixel */
};

/* Framebuffer driver platform data structure */
struct xylonfb_platform_data
{
	unsigned long vmem_base_addr;	/* Physical starting address of the video memory */
	unsigned long vmem_high_addr;	/* Physical ending address of the video memory */
	unsigned int row_stride;		/* Layer row stride in pixels */
	struct xylonfb_platform_layer_params *layer_params;
	unsigned char num_layers;
	unsigned char active_layer;
};

#endif /* __XYLON_FB_H__ */

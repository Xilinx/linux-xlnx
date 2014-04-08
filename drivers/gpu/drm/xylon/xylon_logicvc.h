/*
 * Xylon DRM driver logiCVC header
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _XYLON_LOGICVC_H_
#define _XYLON_LOGICVC_H_

#define LOGICVC_LAYER_CTRL_NONE                 0
#define LOGICVC_LAYER_CTRL_COLOR_TRANSP_DISABLE 1
#define LOGICVC_LAYER_CTRL_COLOR_TRANSP_ENABLE  2
#define LOGICVC_LAYER_CTRL_PIXEL_FORMAT_NORMAL  3
#define LOGICVC_LAYER_CTRL_PIXEL_FORMAT_ANDROID 4

struct xylon_cvc;

unsigned int xylon_cvc_get_layers_num(struct xylon_cvc *cvc);
unsigned int xylon_cvc_get_layers_max_width(struct xylon_cvc *cvc);

u32 xylon_cvc_layer_get_format(struct xylon_cvc *cvc, int id);
int xylon_cvc_layer_get_bits_per_pixel(struct xylon_cvc *cvc, int id);
void xylon_cvc_layer_set_alpha(struct xylon_cvc *cvc, int id, u8 alpha);
int xylon_cvc_layer_set_size_position(struct xylon_cvc *cvc, int id,
				      int src_x, int src_y,
				      unsigned int src_x_size,
				      unsigned int src_y_size,
				      int dst_x, int dst_y,
				      unsigned int dst_x_size,
				      unsigned int dst_y_size);
void xylon_cvc_layer_set_address(struct xylon_cvc *cvc, int id,
				 dma_addr_t paddr, u32 x, u32 y);

void xylon_cvc_layer_enable(struct xylon_cvc *cvc, int id);
void xylon_cvc_layer_disable(struct xylon_cvc *cvc, int id);
void xylon_cvc_layer_update(struct xylon_cvc *cvc, int id);
void xylon_cvc_layer_ctrl(struct xylon_cvc *cvc, int id, int op);

void xylon_cvc_set_hw_color(struct xylon_cvc *cvc, int id, u32 color);

void xylon_cvc_int_state(struct xylon_cvc *cvc, unsigned int type,
			 bool enabled);
u32 xylon_cvc_int_get_active(struct xylon_cvc *cvc);
void xylon_cvc_int_clear_active(struct xylon_cvc *cvc, u32 active);
void xylon_cvc_int_hw_enable(struct xylon_cvc *cvc);
void xylon_cvc_int_hw_disable(struct xylon_cvc *cvc);
int xylon_cvc_int_request(struct xylon_cvc *cvc, unsigned long flags,
			  irq_handler_t handler, void *dev);
void xylon_cvc_int_free(struct xylon_cvc *cvc, void *dev);

void xylon_cvc_reset(struct xylon_cvc *cvc);
void xylon_cvc_enable(struct xylon_cvc *cvc, struct videomode *vmode);
void xylon_cvc_disable(struct xylon_cvc *cvc);

struct xylon_cvc *xylon_cvc_probe(struct device *dev, struct device_node *node);

#endif /* _XYLON_LOGICVC_H_ */

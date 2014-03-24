/*
 * Xylon DRM driver logiCVC layer header
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

#ifndef _XYLON_LOGICVC_LAYER_H_
#define _XYLON_LOGICVC_LAYER_H_

enum xylon_cvc_layer_control {
	LOGICVC_LAYER_COLOR_TRANSPARENCY_DISABLE,
	LOGICVC_LAYER_COLOR_TRANSPARENCY_ENABLE,
	LOGICVC_LAYER_INTERLACE_DISABLE,
	LOGICVC_LAYER_INTERLACE_ENABLE
};

struct xylon_cvc;

unsigned int xylon_cvc_layer_get_total_count(struct xylon_cvc *cvc);
u32 xylon_cvc_layer_get_format(struct xylon_cvc *cvc, int id);
unsigned int xylon_cvc_layer_get_bits_per_pixel(struct xylon_cvc *cvc, int id);
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
void xylon_cvc_layer_ctrl(struct xylon_cvc *cvc, int id,
			  enum xylon_cvc_layer_control op);

void xylon_cvc_layer_set_color_reg(struct xylon_cvc *cvc, int id, u32 color);

#endif /* _XYLON_LOGICVC_LAYER_H_ */

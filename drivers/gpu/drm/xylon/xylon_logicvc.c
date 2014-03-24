/*
 * Xylon DRM driver logiCVC functions
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

#include <drm/drmP.h>

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#include <video/videomode.h>

#include "xylon_drv.h"
#include "xylon_logicvc_helper.h"
#include "xylon_logicvc_hw.h"
#include "xylon_logicvc_layer.h"

/*
 * All logiCVC registers are only 32-bit accessible.
 * All logiCVC registers are aligned to 8 byte boundary.
 */
#define LOGICVC_REG_STRIDE		8
#define LOGICVC_HSYNC_FRONT_PORCH_ROFF	(0  * LOGICVC_REG_STRIDE)
#define LOGICVC_HSYNC_ROFF		(1  * LOGICVC_REG_STRIDE)
#define LOGICVC_HSYNC_BACK_PORCH_ROFF	(2  * LOGICVC_REG_STRIDE)
#define LOGICVC_HRES_ROFF		(3  * LOGICVC_REG_STRIDE)
#define LOGICVC_VSYNC_FRONT_PORCH_ROFF	(4  * LOGICVC_REG_STRIDE)
#define LOGICVC_VSYNC_ROFF		(5  * LOGICVC_REG_STRIDE)
#define LOGICVC_VSYNC_BACK_PORCH_ROFF	(6  * LOGICVC_REG_STRIDE)
#define LOGICVC_VRES_ROFF		(7  * LOGICVC_REG_STRIDE)
#define LOGICVC_CTRL_ROFF		(8  * LOGICVC_REG_STRIDE)
#define LOGICVC_DTYPE_ROFF		(9  * LOGICVC_REG_STRIDE)
#define LOGICVC_BACKGROUND_COLOR_ROFF	(10 * LOGICVC_REG_STRIDE)
#define LOGICVC_DOUBLE_CLUT_ROFF	(12 * LOGICVC_REG_STRIDE)
#define LOGICVC_INT_STAT_ROFF		(13 * LOGICVC_REG_STRIDE)
#define LOGICVC_INT_MASK_ROFF		(14 * LOGICVC_REG_STRIDE)
#define LOGICVC_POWER_CTRL_ROFF		(15 * LOGICVC_REG_STRIDE)
#define LOGICVC_IP_VERSION_ROFF		(31 * LOGICVC_REG_STRIDE)

/*
 * logiCVC layer registers offsets (common for each layer)
 * Last possible logiCVC layer (No.4) implements only "Layer memory address"
 * and "Layer control" registers.
 */
#define LOGICVC_LAYER_ADDR_ROFF		(0 * LOGICVC_REG_STRIDE)
#define LOGICVC_LAYER_HPOS_ROFF		(2 * LOGICVC_REG_STRIDE)
#define LOGICVC_LAYER_VPOS_ROFF		(3 * LOGICVC_REG_STRIDE)
#define LOGICVC_LAYER_HSIZE_ROFF	(4 * LOGICVC_REG_STRIDE)
#define LOGICVC_LAYER_VSIZE_ROFF	(5 * LOGICVC_REG_STRIDE)
#define LOGICVC_LAYER_ALPHA_ROFF	(6 * LOGICVC_REG_STRIDE)
#define LOGICVC_LAYER_CTRL_ROFF		(7 * LOGICVC_REG_STRIDE)
#define LOGICVC_LAYER_TRANSP_COLOR_ROFF	(8 * LOGICVC_REG_STRIDE)

/* logiCVC interrupt bits */
#define LOGICVC_INT_ALL \
		(LOGICVC_INT_L0_UPDATED | LOGICVC_INT_L1_UPDATED | \
		 LOGICVC_INT_L2_UPDATED | LOGICVC_INT_L3_UPDATED | \
		 LOGICVC_INT_L4_UPDATED | LOGICVC_INT_V_SYNC | \
		 LOGICVC_INT_E_VIDEO_VALID | LOGICVC_INT_FIFO_UNDERRUN | \
		 LOGICVC_INT_L0_CLUT_SW | LOGICVC_INT_L1_CLUT_SW | \
		 LOGICVC_INT_L2_CLUT_SW | LOGICVC_INT_L3_CLUT_SW | \
		 LOGICVC_INT_L4_CLUT_SW)
#define LOGICVC_INT_GENERAL \
		(LOGICVC_INT_L0_UPDATED | LOGICVC_INT_L1_UPDATED | \
		 LOGICVC_INT_L2_UPDATED | LOGICVC_INT_L3_UPDATED | \
		 LOGICVC_INT_L4_UPDATED | LOGICVC_INT_FIFO_UNDERRUN)

/* logiCVC layer base offsets */
#define LOGICVC_LAYER_OFFSET		0x80
#define LOGICVC_LAYER_BASE_OFFSET	0x100
#define LOGICVC_LAYER_0_OFFSET		(0 * LOGICVC_LAYER_OFFSET)
#define LOGICVC_LAYER_1_OFFSET		(1 * LOGICVC_LAYER_OFFSET)
#define LOGICVC_LAYER_2_OFFSET		(2 * LOGICVC_LAYER_OFFSET)
#define LOGICVC_LAYER_3_OFFSET		(3 * LOGICVC_LAYER_OFFSET)
#define LOGICVC_LAYER_4_OFFSET		(4 * LOGICVC_LAYER_OFFSET)

/*
 * logiCVC layer CLUT base offsets
 */
#define LOGICVC_CLUT_OFFSET		0x800
#define LOGICVC_CLUT_BASE_OFFSET	0x1000
#define LOGICVC_CLUT_L0_CLUT_0_OFFSET	(0 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L0_CLUT_1_OFFSET	(1 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L1_CLUT_0_OFFSET	(2 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L1_CLUT_1_OFFSET	(3 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L2_CLUT_0_OFFSET	(4 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L2_CLUT_1_OFFSET	(5 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L3_CLUT_0_OFFSET	(6 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L3_CLUT_1_OFFSET	(7 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L4_CLUT_0_OFFSET	(8 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_L4_CLUT_1_OFFSET	(9 * LOGICVC_CLUT_OFFSET)
#define LOGICVC_CLUT_REGISTER_SIZE	8
#define LOGICVC_CLUT_0_INDEX_OFFSET	2
#define LOGICVC_CLUT_1_INDEX_OFFSET	1

/* logiCVC control register bits */
#define LOGICVC_CTRL_HSYNC			(1 << 0)
#define LOGICVC_CTRL_HSYNC_INVERT		(1 << 1)
#define LOGICVC_CTRL_VSYNC			(1 << 2)
#define LOGICVC_CTRL_VSYNC_INVERT		(1 << 3)
#define LOGICVC_CTRL_DATA_ENABLE		(1 << 4)
#define LOGICVC_CTRL_DATA_ENABLE_INVERT		(1 << 5)
#define LOGICVC_CTRL_PIXEL_DATA_INVERT		(1 << 7)
#define LOGICVC_CTRL_PIXEL_DATA_TRIGGER_INVERT	(1 << 8)
#define LOGICVC_CTRL_DISABLE_LAYER_UPDATE	(1 << 9)

/* logiCVC layer control register bits */
#define LOGICVC_LAYER_CTRL_ENABLE			(1 << 0)
#define LOGICVC_LAYER_CTRL_COLOR_TRANSPARENCY_BIT	(1 << 1)
#define LOGICVC_LAYER_CTRL_INTERLACE_BIT		(1 << 3)

/* logiCVC control registers initial values */
#define LOGICVC_DTYPE_REG_INIT 0

/* logiCVC various definitions */
#define LOGICVC_MAJOR_REVISION_SHIFT	11
#define LOGICVC_MAJOR_REVISION_MASK	0x3F
#define LOGICVC_MINOR_REVISION_SHIFT	5
#define LOGICVC_MINOR_REVISION_MASK	0x3F
#define LOGICVC_PATCH_LEVEL_MASK	0x1F

#define LOGICVC_MIN_HRES	64
#define LOGICVC_MIN_VRES	1
#define LOGICVC_MAX_HRES	2048
#define LOGICVC_MAX_VRES	2048
#define LOGICVC_MAX_LINES	4096
#define LOGICVC_MAX_LAYERS	5

#define LOGICVC_FLAGS_READABLE_REGS		(1 << 0)
#define LOGICVC_FLAGS_SIZE_POSITION		(1 << 1)
#define LOGICVC_FLAGS_BACKGROUND_LAYER		(1 << 2)
#define LOGICVC_FLAGS_BACKGROUND_LAYER_RGB	(1 << 3)
#define LOGICVC_FLAGS_BACKGROUND_LAYER_YUV	(1 << 4)

#define LOGICVC_COLOR_RGB_BLACK		0
#define LOGICVC_COLOR_RGB_WHITE		0xFFFFFF
#define LOGICVC_COLOR_RGB565_WHITE	0xFFFF
#define LOGICVC_COLOR_YUV888_BLACK	0x8080
#define LOGICVC_COLOR_YUV888_WHITE	0xFF8080

enum xylon_cvc_layer_type {
	LOGICVC_LAYER_RGB,
	LOGICVC_LAYER_YUV
};

enum xylon_cvc_layer_alpha_type {
	LOGICVC_ALPHA_LAYER,
	LOGICVC_ALPHA_PIXEL,
	LOGICVC_ALPHA_CLUT_16BPP,
	LOGICVC_ALPHA_CLUT_32BPP
};

enum xylon_cvc_display_color_space {
	LOGICVC_DCS_RGB,
	LOGICVC_DCS_YUV422,
	LOGICVC_DCS_YUV444
};

struct xylon_cvc_layer_data;

struct xylon_cvc_register_access {
	u32 (*xylon_cvc_get_reg_val)(void __iomem *reg_base_virt,
				     unsigned int offset,
				     struct xylon_cvc_layer_data *layer_data);
	void (*xylon_cvc_set_reg_val)(u32 value, void __iomem *reg_base_virt,
				      unsigned int offset,
				      struct xylon_cvc_layer_data *layer_data);
};

struct xylon_cvc_registers {
	u32 ctrl;
	u32 dtype;
	u32 bg;
	u32 unused[3];
	u32 imr;
};

struct xylon_cvc_layer_fix_data {
	unsigned int id;
	u32 address;
	u32 bpp;
	u32 type;
	u32 transparency;
	u32 width;
};

struct xylon_cvc_layer_registers {
	u32 addr;
	u32 unused;
	u32 hpos;
	u32 vpos;
	u32 hsize;
	u32 vsize;
	u32 alpha;
	u32 ctrl;
	u32 transp;
};

struct xylon_cvc_layer_data {
	struct xylon_cvc_layer_fix_data fix_data;
	struct xylon_cvc_layer_registers regs;
	void __iomem *base;
	void __iomem *clut_base;
	dma_addr_t vmem_pbase;
	struct xylon_cvc *cvc;
};

struct xylon_cvc {
	struct device_node *dn;
	void __iomem *base;
	struct videomode *vmode;
	struct xylon_cvc_register_access reg_access;
	struct xylon_cvc_registers regs;
	struct xylon_cvc_layer_data *layer_data[LOGICVC_MAX_LAYERS];
	unsigned int flags;
	unsigned int irq;
	unsigned int layers;
	unsigned int power_on_delay;
	unsigned int signal_on_delay;
	u32 bg_layer_bpp;
	u32 ctrl;
	u32 pixel_stride;
};

static u32 xylon_cvc_get_reg(void __iomem *base,
			     unsigned int offset,
			     struct xylon_cvc_layer_data *layer_data)
{
	return readl(base + offset);
}

static void xylon_cvc_set_reg(u32 value, void __iomem *base,
			      unsigned int offset,
			      struct xylon_cvc_layer_data *layer_data)
{
	writel(value, base + offset);
}

static unsigned long
xylon_cvc_get_reg_mem_addr(void __iomem *base, unsigned int offset,
		           struct xylon_cvc_layer_data *layer_data)
{
	unsigned int ordinal = offset / LOGICVC_REG_STRIDE;

	if ((unsigned long)base - (unsigned long)layer_data->cvc->base) {
		return (unsigned long)(&layer_data->regs) +
			(ordinal * sizeof(u32));
	} else {
		ordinal -= (LOGICVC_CTRL_ROFF / LOGICVC_REG_STRIDE);
		return (unsigned long)(&layer_data->cvc->regs) +
				       (ordinal * sizeof(u32));
	}
}

static u32 xylon_cvc_get_reg_mem(void __iomem *base, unsigned int offset,
				 struct xylon_cvc_layer_data *layer_data)
{
	return *((unsigned long *)xylon_cvc_get_reg_mem_addr(base, offset,
							     layer_data));
}

static void xylon_cvc_set_reg_mem(u32 value, void __iomem *base,
				  unsigned int offset,
				  struct xylon_cvc_layer_data *layer_data)
{
	unsigned long *reg_mem_addr =
		(unsigned long *)xylon_cvc_get_reg_mem_addr(base, offset,
							    layer_data);
	*reg_mem_addr = value;
	writel((*reg_mem_addr), (base + offset));
}

unsigned int xylon_cvc_layer_get_total_count(struct xylon_cvc *cvc)
{
	return cvc->layers;
}

u32 xylon_cvc_layer_get_format(struct xylon_cvc *cvc, int id)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];
	u32 drm_format = 0;
	u32 bpp = layer_data->fix_data.bpp;
	u32 transp = layer_data->fix_data.transparency;

	switch (layer_data->fix_data.type) {
	case LOGICVC_LAYER_RGB:
		if (bpp == 16 && transp == LOGICVC_ALPHA_LAYER)
			drm_format = DRM_FORMAT_RGB565;
		else if (bpp == 32 && transp == LOGICVC_ALPHA_LAYER)
			drm_format = DRM_FORMAT_XRGB8888;
		else if (bpp == 32 && transp == LOGICVC_ALPHA_PIXEL)
			drm_format = DRM_FORMAT_ARGB8888;
		break;
	case LOGICVC_LAYER_YUV:
		if (bpp == 16 && transp == LOGICVC_ALPHA_LAYER)
			drm_format = DRM_FORMAT_YUYV;
		else if (bpp == 32 && transp == LOGICVC_ALPHA_LAYER)
			drm_format = DRM_FORMAT_YUYV;
		break;
	default:
		DRM_ERROR("unsupported layer format\n");
	}

	return drm_format;
}

unsigned int xylon_cvc_layer_get_bits_per_pixel(struct xylon_cvc *cvc, int id)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];

	return layer_data->fix_data.bpp;
}

void xylon_cvc_layer_set_alpha(struct xylon_cvc *cvc, int id, u8 alpha)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];
	struct xylon_cvc_layer_fix_data *fix_data = &layer_data->fix_data;
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;
	u32 alpha32 = alpha;

	if (fix_data->transparency == LOGICVC_ALPHA_LAYER)
		reg_access->xylon_cvc_set_reg_val(alpha32, layer_data->base,
						  LOGICVC_LAYER_ALPHA_ROFF,
						  layer_data);
}

int xylon_cvc_layer_set_size_position(struct xylon_cvc *cvc, int id,
				      int src_x, int src_y,
				      unsigned int src_x_size,
				      unsigned int src_y_size,
				      int dst_x, int dst_y,
				      unsigned int dst_x_size,
				      unsigned int dst_y_size)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;
	void __iomem *base = layer_data->base;
	u32 hres, vres;

	DRM_DEBUG("%d-%d(%d-%d), %d-%d(%d-%d)\n",
		  src_x, dst_x, src_x_size, dst_x_size,
		  src_y, dst_y, src_y_size, dst_y_size);

	if (src_x_size != dst_x_size || src_y_size != dst_y_size) {
		DRM_ERROR("invalid source coordinates\n");
		return -EINVAL;
	}

	if (cvc->vmode) {
		hres = cvc->vmode->hactive;
		vres = cvc->vmode->vactive;

		if ((dst_x + dst_x_size) > hres ||
		    (dst_y + dst_y_size) > vres) {
			DRM_ERROR("invalid rectangle width\n");
			return -EINVAL;
		}

		reg_access->xylon_cvc_set_reg_val(hres - dst_x - 1,
						  base,
						  LOGICVC_LAYER_HPOS_ROFF,
						  layer_data);
		reg_access->xylon_cvc_set_reg_val(vres - dst_y - 1,
						  base,
						  LOGICVC_LAYER_VPOS_ROFF,
						  layer_data);
		reg_access->xylon_cvc_set_reg_val(dst_x_size - 1,
						  base,
						  LOGICVC_LAYER_HSIZE_ROFF,
						  layer_data);
		reg_access->xylon_cvc_set_reg_val(dst_y_size - 1,
						  base,
						  LOGICVC_LAYER_VSIZE_ROFF,
						  layer_data);
	}

	return 0;
}

void xylon_cvc_layer_set_address(struct xylon_cvc *cvc, int id,
				 dma_addr_t paddr, u32 x, u32 y)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];
	u32 vmem_offset;

	vmem_offset = (x * (layer_data->fix_data.bpp / 8)) +
		      (y * layer_data->fix_data.width *
		      (layer_data->fix_data.bpp / 8));

	layer_data->vmem_pbase = paddr + vmem_offset;
}

void xylon_cvc_layer_enable(struct xylon_cvc *cvc, int id)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;
	u32 regval = reg_access->xylon_cvc_get_reg_val(layer_data->base,
						       LOGICVC_LAYER_CTRL_ROFF,
						       layer_data);

	regval |= LOGICVC_LAYER_CTRL_ENABLE;
	reg_access->xylon_cvc_set_reg_val(regval,
					  layer_data->base,
					  LOGICVC_LAYER_CTRL_ROFF,
					  layer_data);
}

void xylon_cvc_layer_disable(struct xylon_cvc *cvc, int id)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;
	u32 regval = reg_access->xylon_cvc_get_reg_val(layer_data->base,
						       LOGICVC_LAYER_CTRL_ROFF,
						       layer_data);

	regval &= (~LOGICVC_LAYER_CTRL_ENABLE);
	reg_access->xylon_cvc_set_reg_val(regval,
					  layer_data->base,
					  LOGICVC_LAYER_CTRL_ROFF,
					  layer_data);
}

void xylon_cvc_layer_update(struct xylon_cvc *cvc, int id)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;

	reg_access->xylon_cvc_set_reg_val(layer_data->vmem_pbase,
					  layer_data->base,
					  LOGICVC_LAYER_ADDR_ROFF,
					  layer_data);
}

void xylon_cvc_layer_ctrl(struct xylon_cvc *cvc, int id,
			  enum xylon_cvc_layer_control op)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[id];
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;
	u32 regval = reg_access->xylon_cvc_get_reg_val(layer_data->base,
						       LOGICVC_LAYER_CTRL_ROFF,
						       layer_data);

	switch (op) {
	case LOGICVC_LAYER_COLOR_TRANSPARENCY_DISABLE:
		regval |= LOGICVC_LAYER_CTRL_COLOR_TRANSPARENCY_BIT;
		break;
	case LOGICVC_LAYER_COLOR_TRANSPARENCY_ENABLE:
		regval &= ~LOGICVC_LAYER_CTRL_COLOR_TRANSPARENCY_BIT;
		break;
	case LOGICVC_LAYER_INTERLACE_DISABLE:
		regval |= LOGICVC_LAYER_CTRL_INTERLACE_BIT;
		break;
	case LOGICVC_LAYER_INTERLACE_ENABLE:
		regval &= ~LOGICVC_LAYER_CTRL_INTERLACE_BIT;
		break;
	default:
		return;
	}

	reg_access->xylon_cvc_set_reg_val(regval,
					  layer_data->base,
					  LOGICVC_LAYER_CTRL_ROFF,
					  layer_data);
}

void xylon_cvc_layer_set_color_reg(struct xylon_cvc *cvc, int id, u32 color)
{
	struct xylon_cvc_layer_data *layer_data;
	void __iomem *base;
	unsigned int layer_bpp;
	unsigned long offset;
	u8 r, g, b;
	bool bg = false;

	if (id == BACKGROUND_LAYER_ID)
		bg = true;

	if (bg) {
		if (!(cvc->flags & LOGICVC_FLAGS_BACKGROUND_LAYER))
			return;
		layer_data = cvc->layer_data[0];
		layer_bpp = cvc->bg_layer_bpp;
	} else {
		layer_data = cvc->layer_data[id];
		layer_bpp = layer_data->fix_data.bpp;
	}

	switch (layer_bpp) {
	case 16:
		color &= LOGICVC_COLOR_RGB_WHITE;
		if (cvc->flags & LOGICVC_FLAGS_BACKGROUND_LAYER_RGB &&
		    (color != LOGICVC_COLOR_RGB_BLACK)) {
			if (color != LOGICVC_COLOR_RGB_WHITE) {
				r = color >> 16;
				g = color >> 8;
				b = color;

				color = ((r & 0xF8) << 8) |
					((g & 0xFC) << 3) |
					((b & 0xF8) >> 3);
			} else {
				color = LOGICVC_COLOR_RGB565_WHITE;
			}
		}
		break;
	case 32:
		if (cvc->flags & LOGICVC_FLAGS_BACKGROUND_LAYER_YUV) {
			if (color == LOGICVC_COLOR_RGB_BLACK)
				color = LOGICVC_COLOR_YUV888_BLACK;
			else if (color == LOGICVC_COLOR_RGB_WHITE)
				color = LOGICVC_COLOR_YUV888_WHITE;
		}
		break;
	default:
		if (cvc->flags & LOGICVC_FLAGS_BACKGROUND_LAYER_YUV)
			color = LOGICVC_COLOR_YUV888_BLACK;
		else
			color = LOGICVC_COLOR_RGB_BLACK;
		DRM_INFO("unsupported bg layer bpp\n");
		return;
	}

	if (bg) {
		base = cvc->base;
		offset = LOGICVC_BACKGROUND_COLOR_ROFF;
	} else {
		base = layer_data->base;
		offset = LOGICVC_LAYER_TRANSP_COLOR_ROFF;
	}
	cvc->reg_access.xylon_cvc_set_reg_val(color,
					      base, offset,
					      layer_data);
}

void xylon_cvc_int_state(struct xylon_cvc *cvc, unsigned int type,
			 bool enabled)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[0];
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;
	void __iomem *base = cvc->base;
	u32 imr = reg_access->xylon_cvc_get_reg_val(base,
						    LOGICVC_INT_MASK_ROFF,
						    layer_data);
	if (enabled)
		imr &= ~type;
	else
		imr |= type;

	reg_access->xylon_cvc_set_reg_val(imr, base,
					  LOGICVC_INT_MASK_ROFF,
					  layer_data);
}

u32 xylon_cvc_int_get_active(struct xylon_cvc *cvc)
{
	return readl(cvc->base + LOGICVC_INT_STAT_ROFF);
}

void xylon_cvc_int_clear_active(struct xylon_cvc *cvc, u32 active)
{
	writel(active, cvc->base + LOGICVC_INT_STAT_ROFF);
}

void xylon_cvc_int_hw_enable(struct xylon_cvc *cvc)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[0];
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;
	void __iomem *base = cvc->base;

	reg_access->xylon_cvc_set_reg_val(LOGICVC_INT_GENERAL, base,
					  LOGICVC_INT_MASK_ROFF,
					  layer_data);
	writel(LOGICVC_INT_ALL, base + LOGICVC_INT_STAT_ROFF);
}

void xylon_cvc_int_hw_disable(struct xylon_cvc *cvc)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[0];
	struct xylon_cvc_register_access *reg_access = &cvc->reg_access;
	void __iomem *base = cvc->base;

	reg_access->xylon_cvc_set_reg_val(LOGICVC_INT_ALL, base,
					  LOGICVC_INT_MASK_ROFF,
					  layer_data);
	writel(LOGICVC_INT_ALL, base + LOGICVC_INT_STAT_ROFF);
}

int xylon_cvc_int_request(struct xylon_cvc *cvc, unsigned long flags,
			  irq_handler_t handler, void *dev)
{
	struct device_node *dn = cvc->dn;
	int irq;

	irq = of_irq_to_resource(dn, 0, NULL);
	if (irq < 0) {
		DRM_ERROR("failed get irq resource\n");
		return irq;
	}

	cvc->irq = irq;

	return request_irq(irq, handler, flags, dn->name, dev);
}

void xylon_cvc_int_free(struct xylon_cvc *cvc, void *dev)
{
	free_irq(cvc->irq, dev);
}

void xylon_cvc_ctrl(struct xylon_cvc *cvc, enum xylon_cvc_control ctrl,
		    bool val)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[0];

	switch (ctrl) {
	case LOGICVC_LAYER_UPDATE:
		if (val)
			cvc->ctrl &= ~LOGICVC_CTRL_DISABLE_LAYER_UPDATE;
		else
			cvc->ctrl |= LOGICVC_CTRL_DISABLE_LAYER_UPDATE;
		break;
	case LOGICVC_PIXEL_DATA_TRIGGER_INVERT:
		if (val)
			cvc->ctrl |= LOGICVC_CTRL_PIXEL_DATA_TRIGGER_INVERT;
		else
			cvc->ctrl &= ~LOGICVC_CTRL_PIXEL_DATA_TRIGGER_INVERT;
		break;
	case LOGICVC_PIXEL_DATA_INVERT:
		if (val)
			cvc->ctrl |= LOGICVC_CTRL_PIXEL_DATA_INVERT;
		else
			cvc->ctrl &= ~LOGICVC_CTRL_PIXEL_DATA_INVERT;
		break;
	}

	cvc->reg_access.xylon_cvc_set_reg_val(cvc->ctrl, cvc->base,
					      LOGICVC_CTRL_ROFF,
					      layer_data);
}

void xylon_cvc_enable(struct xylon_cvc *cvc, struct videomode *vmode)
{
	void __iomem *base = cvc->base;
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[0];
	struct videomode *vm;

	if (vmode) {
		cvc->vmode = vmode;
		vm = vmode;
	} else {
		vm = cvc->vmode;
	}

	writel(vm->hfront_porch - 1, base + LOGICVC_HSYNC_FRONT_PORCH_ROFF);
	writel(vm->hsync_len - 1, base + LOGICVC_HSYNC_ROFF);
	writel(vm->hback_porch - 1, base + LOGICVC_HSYNC_BACK_PORCH_ROFF);
	writel(vm->hactive - 1, base + LOGICVC_HRES_ROFF);
	writel(vm->vfront_porch - 1, base + LOGICVC_VSYNC_FRONT_PORCH_ROFF);
	writel(vm->vsync_len - 1, base + LOGICVC_VSYNC_ROFF);
	writel(vm->vback_porch - 1, base + LOGICVC_VSYNC_BACK_PORCH_ROFF);
	writel(vm->vactive - 1, base + LOGICVC_VRES_ROFF);

	cvc->reg_access.xylon_cvc_set_reg_val(cvc->ctrl, base,
					      LOGICVC_CTRL_ROFF,
					      layer_data);

	writel(LOGICVC_DTYPE_REG_INIT, base + LOGICVC_DTYPE_ROFF);
}

void xylon_cvc_disable(struct xylon_cvc *cvc)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[0];
	int i;

	if (layer_data)
		for (i = 0; i < cvc->layers; i++)
			xylon_cvc_layer_disable(cvc, i);
}

static int xylon_parse_hw_info(struct device_node *dn, struct xylon_cvc *cvc)
{
	int ret;
	const char *string;

	if (of_property_read_bool(dn, "background-layer-bits-per-pixel")) {
		ret = of_property_read_u32(dn,
					   "background-layer-bits-per-pixel",
					   &cvc->bg_layer_bpp);
		if (ret) {
			DRM_ERROR("failed get bg-layer-bits-per-pixel\n");
			return ret;
		}
		cvc->flags |= LOGICVC_FLAGS_BACKGROUND_LAYER;

		ret = of_property_read_string(dn, "background-layer-type",
					      &string);
		if (ret) {
			DRM_ERROR("failed get bg-layer-type\n");
			return ret;
		}
		if (!strcmp(string, "rgb")) {
			cvc->flags |= LOGICVC_FLAGS_BACKGROUND_LAYER_RGB;
		} else if (!strcmp(string, "yuv")) {
			cvc->flags |= LOGICVC_FLAGS_BACKGROUND_LAYER_YUV;
		} else {
			DRM_ERROR("unsupported bg layer type\n");
			return -EINVAL;
		}
	}

	if (of_property_read_bool(dn, "readable-regs"))
		cvc->flags |= LOGICVC_FLAGS_READABLE_REGS;
	else
		DRM_INFO("logicvc registers not readable\n");

	if (of_property_read_bool(dn, "size-position"))
		cvc->flags |= LOGICVC_FLAGS_SIZE_POSITION;
	else
		DRM_INFO("logicvc size-position disabled\n");

	ret = of_property_read_u32(dn, "pixel-stride", &cvc->pixel_stride);
	if (ret) {
		DRM_ERROR("failed get pixel-stride\n");
		return ret;
	}

	return 0;
}

static int xylonfb_parse_layer_info(struct device *dev,
				    struct device_node *parent_dn,
				    struct xylon_cvc *cvc, int id)
{
	struct device_node *dn;
	struct xylon_cvc_layer_data *layer_data;
	int ret;
	char layer_name[10];
	const char *string;

	snprintf(layer_name, sizeof(layer_name), "layer_%d", id);
	dn = of_get_child_by_name(parent_dn, layer_name);
	if (!dn)
		return 0;

	cvc->layers++;

	layer_data = devm_kzalloc(dev, sizeof(*layer_data), GFP_KERNEL);
	if (!layer_data) {
		DRM_ERROR("failed allocate layer data id %d\n", id);
		return -ENOMEM;
	}
	layer_data->cvc = cvc;
	layer_data->fix_data.id = id;

	cvc->layer_data[id] = layer_data;

	if (of_property_read_bool(dn, "address")) {
		ret = of_property_read_u32(dn, "address",
					   &layer_data->fix_data.address);
		if (ret) {
			DRM_ERROR("failed get address\n");
			return ret;
		}
	}

	ret = of_property_read_u32(dn, "bits-per-pixel",
				   &layer_data->fix_data.bpp);
	if (ret) {
		DRM_ERROR("failed get bits-per-pixel\n");
		return ret;
	}

	ret = of_property_read_string(dn, "type", &string);
	if (ret) {
		DRM_ERROR("failed get type\n");
		return ret;
	}
	if (!strcmp(string, "rgb")) {
		layer_data->fix_data.type = LOGICVC_LAYER_RGB;
	} else if (!strcmp(string, "yuv")) {
		layer_data->fix_data.type = LOGICVC_LAYER_YUV;
	} else {
		DRM_ERROR("unsupported layer type\n");
		return -EINVAL;
	}

	ret = of_property_read_string(dn, "transparency", &string);
	if (ret) {
		DRM_ERROR("failed get transparency\n");
		return ret;
	}
	if (!strcmp(string, "layer")) {
		layer_data->fix_data.transparency = LOGICVC_ALPHA_LAYER;
	} else if (!strcmp(string, "pixel")) {
		layer_data->fix_data.transparency = LOGICVC_ALPHA_PIXEL;
	} else {
		DRM_ERROR("unsupported layer transparency\n");
		return -EINVAL;
	}

	layer_data->fix_data.width = cvc->pixel_stride;

	return id + 1;
}

static void xylon_cvc_init_ctrl(struct device_node *dn, u32 *ctrl)
{
	u32 ctrl_reg = (LOGICVC_CTRL_HSYNC | LOGICVC_CTRL_VSYNC |
			LOGICVC_CTRL_DATA_ENABLE);

	if (of_property_read_bool(dn, "hsync-active-low"))
		ctrl_reg |= LOGICVC_CTRL_HSYNC_INVERT;
	if (of_property_read_bool(dn, "vsync-active-low"))
		ctrl_reg |= LOGICVC_CTRL_HSYNC_INVERT;
	if (of_property_read_bool(dn, "pixel-data-invert"))
		ctrl_reg |= LOGICVC_CTRL_PIXEL_DATA_INVERT;
	if (of_property_read_bool(dn, "pixel-data-output-trigger-high"))
		ctrl_reg |= LOGICVC_CTRL_PIXEL_DATA_TRIGGER_INVERT;

	*ctrl = ctrl_reg;
}

bool xylon_cvc_get_info(struct xylon_cvc *cvc, enum xylon_cvc_info info,
			unsigned int param)
{
	struct xylon_cvc_layer_data *layer_data;
	struct xylon_cvc_register_access *reg_access;

	switch (info) {
	case LOGICVC_INFO_BACKGROUND_LAYER:
		if (cvc->flags & LOGICVC_FLAGS_BACKGROUND_LAYER)
			return true;
		break;
	case LOGICVC_INFO_LAST_LAYER:
		if (param == (cvc->layers - 1))
			return true;
		break;
	case LOGICVC_INFO_LAYER_COLOR_TRANSPARENCY:
		layer_data = cvc->layer_data[param];
		reg_access = &cvc->reg_access;
		if (!(reg_access->xylon_cvc_get_reg_val(layer_data->base,
							LOGICVC_LAYER_CTRL_ROFF,
							layer_data) &
		    LOGICVC_LAYER_CTRL_COLOR_TRANSPARENCY_BIT))
			return true;
		break;
	case LOGICVC_INFO_LAYER_UPDATE:
		if (!(cvc->ctrl & LOGICVC_CTRL_DISABLE_LAYER_UPDATE))
			return true;
		break;
	case LOGICVC_INFO_PIXEL_DATA_INVERT:
		if (cvc->ctrl & LOGICVC_CTRL_PIXEL_DATA_INVERT)
			return true;
		break;
	case LOGICVC_INFO_PIXEL_DATA_TRIGGER_INVERT:
		if (cvc->ctrl & LOGICVC_CTRL_PIXEL_DATA_TRIGGER_INVERT)
			return true;
		break;
	case LOGICVC_INFO_SIZE_POSITION:
		if (cvc->flags & LOGICVC_FLAGS_SIZE_POSITION)
			return true;
		break;
	}

	return false;
}

void xylon_cvc_get_fix_parameters(struct xylon_cvc *cvc,
				  struct xylon_cvc_fix *cvc_fix)
{
	struct xylon_cvc_layer_data *layer_data = cvc->layer_data[0];
	struct xylon_cvc_layer_fix_data *fix_data = &layer_data->fix_data;

	cvc_fix->hres_min = LOGICVC_MIN_HRES;
	cvc_fix->vres_min = LOGICVC_MIN_VRES;
	cvc_fix->hres_max = LOGICVC_MAX_HRES;
	cvc_fix->vres_max = LOGICVC_MAX_VRES;
	cvc_fix->x_min = LOGICVC_MIN_HRES;
	cvc_fix->y_min = LOGICVC_MIN_VRES;
	cvc_fix->x_max = fix_data->width;
	cvc_fix->y_max = LOGICVC_MAX_LINES;
}

static const struct of_device_id cvc_of_match[] = {
	{ .compatible = "xylon,logicvc-4.00.a" },
	{ .compatible = "xylon,logicvc-5.0" },
	{/* end of table */}
};

struct xylon_cvc *xylon_cvc_probe(struct device *dev, struct device_node *dn)
{
	struct xylon_cvc *cvc;
	const struct of_device_id *match;
	__force const void *err;
	struct resource res;
	u32 ip_ver;
	int ret, i;
	unsigned short xylon_cvc_layer_base_off[] = {
		(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_0_OFFSET),
		(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_1_OFFSET),
		(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_2_OFFSET),
		(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_3_OFFSET),
		(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_4_OFFSET)
	};
	unsigned short xylon_cvc_clut_base_off[] = {
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L0_CLUT_0_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L0_CLUT_1_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L1_CLUT_0_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L1_CLUT_1_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L2_CLUT_0_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L2_CLUT_1_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L3_CLUT_0_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L3_CLUT_1_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L4_CLUT_0_OFFSET),
		(LOGICVC_CLUT_BASE_OFFSET + LOGICVC_CLUT_L4_CLUT_1_OFFSET)
	};

	match = of_match_node(cvc_of_match, dn);
	if (!match) {
		DRM_ERROR("failed match cvc\n");
		return ERR_PTR(-ENODEV);
	}

	cvc = devm_kzalloc(dev, sizeof(*cvc), GFP_KERNEL);
	if (!cvc) {
		DRM_ERROR("failed allocate cvc\n");
		return ERR_PTR(-ENOMEM);
	}
	cvc->dn = dn;

	ret = of_address_to_resource(dn, 0, &res);
	if (ret) {
		DRM_ERROR("failed get mem resource\n");
		return ERR_PTR(ret);
	}

	cvc->base = devm_ioremap_resource(dev, &res);
	err = (__force const void *)cvc->base;
	if (IS_ERR(err)) {
		DRM_ERROR("failed remap resource\n");
		return ERR_CAST(err);
	}

	ip_ver = readl(cvc->base + LOGICVC_IP_VERSION_ROFF);
	DRM_INFO("logiCVC IP core %d.%02d.%c\n",
		 ((ip_ver >> LOGICVC_MAJOR_REVISION_SHIFT) &
		 LOGICVC_MAJOR_REVISION_MASK),
		 ((ip_ver >> LOGICVC_MINOR_REVISION_SHIFT) &
		 LOGICVC_MINOR_REVISION_MASK),
		 ((ip_ver & LOGICVC_PATCH_LEVEL_MASK) + 'a'));

	ret = xylon_parse_hw_info(dn, cvc);
	if (ret)
		return ERR_PTR(ret);

	for (i = 0; i < LOGICVC_MAX_LAYERS; i++) {
		ret = xylonfb_parse_layer_info(dev, dn, cvc, i);
		if (ret < 0)
			return ERR_PTR(ret);
		if (ret == 0)
			break;

		cvc->layer_data[i]->base =
			cvc->base + xylon_cvc_layer_base_off[i];
		cvc->layer_data[i]->clut_base =
			cvc->base + xylon_cvc_clut_base_off[i];
	}

	xylon_cvc_init_ctrl(dn, &cvc->ctrl);

	if (cvc->flags & LOGICVC_FLAGS_READABLE_REGS) {
		cvc->reg_access.xylon_cvc_get_reg_val = xylon_cvc_get_reg;
		cvc->reg_access.xylon_cvc_set_reg_val = xylon_cvc_set_reg;
	} else {
		cvc->reg_access.xylon_cvc_get_reg_val = xylon_cvc_get_reg_mem;
		cvc->reg_access.xylon_cvc_set_reg_val = xylon_cvc_set_reg_mem;
	}

	if (cvc->flags & LOGICVC_FLAGS_BACKGROUND_LAYER)
		xylon_cvc_layer_set_color_reg(cvc, BACKGROUND_LAYER_ID,
					      LOGICVC_COLOR_RGB_BLACK);

	return cvc;
}

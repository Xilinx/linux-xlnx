/*
 * Xilinx OSD support
 *
 *  Copyright (C) 2013 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyunk@xilinx.com>
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
#include <linux/of_irq.h>
#include <linux/slab.h>

#include "xilinx_drm_drv.h"

#include "xilinx_osd.h"

/* registers */
#define OSD_CTL	0x000	/* control */
#define OSD_SS	0x020	/* screen size */
#define OSD_ENC	0x028	/* encoding register */
#define OSD_BC0	0x100	/* background color channel 0 */
#define OSD_BC1	0x104	/* background color channel 1 */
#define OSD_BC2	0x108	/* background color channel 2 */

#define OSD_L0C	0x110	/* layer 0 control */

/* register offset of layers */
#define OSD_LAYER_SIZE	0x10
#define OSD_LXC		0x00	/* layer control */
#define OSD_LXP		0x04	/* layer position */
#define OSD_LXS		0x08	/* layer size */

/*osd control register bit definition */
#define OSD_CTL_RUE		(1 << 1)	/* osd reg update enable */
#define OSD_CTL_EN		(1 << 0)	/* osd enable */

/* osd screen size register bit definition */
#define OSD_SS_YSIZE_MASK   0x0fff0000	/* vertical height of OSD output */
#define OSD_SS_YSIZE_SHIFT  16		/* bit shift of OSD_SS_YSIZE_MASK */
#define OSD_SS_XSIZE_MASK   0x00000fff	/* horizontal width of OSD output */

/* osd vidoe format mask */
#define OSD_VIDEO_FORMAT_MASK	0x0000000f	/* video format */

/* osd background color channel 0 */
#define OSD_BC0_YG_MASK		0x000000ff	/* Y (luma) or Green */

/* osd background color channel 1 */
#define OSD_BC1_UCBB_MASK	0x000000ff	/* U (Cb) or Blue */

/* osd background color channel 2 */
#define OSD_BC2_VCRR_MASK	0x000000ff	/* V(Cr) or Red */

/* maximum number of the layers */
#define OSD_MAX_NUM_OF_LAYERS	8

/* osd layer control (layer 0 through (OSD_MAX_NUM_OF_LAYERS - 1)) */
#define OSD_LXC_ALPHA_MASK	0x0fff0000	/* global alpha value */
#define OSD_LXC_ALPHA_SHIFT	16		/* bit shift of alpha value */
#define OSD_LXC_PRIORITY_MASK	0x00000700	/* layer priority */
#define OSD_LXC_PRIORITY_SHIFT	8		/* bit shift of priority */
#define OSD_LXC_GALPHAEN	(1 << 1)	/* global alpha enable */
#define OSD_LXC_EN		(1 << 0)	/* layer enable */

/* osd layer position (layer 0 through (OSD_MAX_NUM_OF_LAYERS - 1)) */
#define OSD_LXP_YSTART_MASK	0x0fff0000	/* vert start line */
#define OSD_LXP_YSTART_SHIFT	16		/* vert start line bit shift */
#define OSD_LXP_XSTART_MASK	0x00000fff	/* horizontal start pixel */

/* osd layer size (layer 0 through (OSD_MAX_NUM_OF_LAYERS - 1)) */
#define OSD_LXS_YSIZE_MASK	0x0fff0000	/* vert size */
#define OSD_LXS_YSIZE_SHIFT	16		/* vertical size bit shift */
#define OSD_LXS_XSIZE_MASK	0x00000fff	/* horizontal size of layer */

/* osd software reset */
#define OSD_RST_RESET	(1 << 31)

/**
 * struct xilinx_osd_layer - Xilinx OSD layer object
 *
 * @base: base address
 * @id: id
 * @avail: available flag
 * @osd: osd
 */
struct xilinx_osd_layer {
	void __iomem *base;
	int id;
	bool avail;
	struct xilinx_osd *osd;
};

/**
 * struct xilinx_osd - Xilinx OSD object
 *
 * @base: base address
 * @layers: layers
 * @num_layers: number of layers
 * @max_width: maximum width
 * @format: video format
 */
struct xilinx_osd {
	void __iomem *base;
	struct xilinx_osd_layer *layers[OSD_MAX_NUM_OF_LAYERS];
	unsigned int num_layers;
	unsigned int max_width;
	unsigned int format;
};

/* osd layer operation */
/* set layer alpha */
void xilinx_osd_layer_set_alpha(struct xilinx_osd_layer *layer, u32 alpha)
{
	u32 value;

	DRM_DEBUG_DRIVER("layer->id: %d\n", layer->id);
	DRM_DEBUG_DRIVER("alpha: 0x%08x\n", alpha);

	value = xilinx_drm_readl(layer->base, OSD_LXC);
	value &= ~OSD_LXC_ALPHA_MASK;
	value |= (alpha << OSD_LXC_ALPHA_SHIFT) & OSD_LXC_ALPHA_MASK;
	xilinx_drm_writel(layer->base, OSD_LXC, value);
}

void xilinx_osd_layer_enable_alpha(struct xilinx_osd_layer *layer, bool enable)
{
	u32 value;

	DRM_DEBUG_DRIVER("layer->id: %d\n", layer->id);
	DRM_DEBUG_DRIVER("enable: %d\n", enable);

	value = xilinx_drm_readl(layer->base, OSD_LXC);
	value = enable ? (value | OSD_LXC_GALPHAEN) :
		(value & ~OSD_LXC_GALPHAEN);
	xilinx_drm_writel(layer->base, OSD_LXC, value);
}

/* set layer priority */
void xilinx_osd_layer_set_priority(struct xilinx_osd_layer *layer, u32 prio)
{
	u32 value;

	DRM_DEBUG_DRIVER("layer->id: %d\n", layer->id);
	DRM_DEBUG_DRIVER("prio: %d\n", prio);

	value = xilinx_drm_readl(layer->base, OSD_LXC);
	value &= ~OSD_LXC_PRIORITY_MASK;
	value |= (prio << OSD_LXC_PRIORITY_SHIFT) & OSD_LXC_PRIORITY_MASK;
	xilinx_drm_writel(layer->base, OSD_LXC, value);
}

/* set layer dimension */
void xilinx_osd_layer_set_dimension(struct xilinx_osd_layer *layer,
				    u16 xstart, u16 ystart,
				    u16 xsize, u16 ysize)
{
	u32 value;

	DRM_DEBUG_DRIVER("layer->id: %d\n", layer->id);
	DRM_DEBUG_DRIVER("w: %d(%d), h: %d(%d)\n",
			 xsize, xstart, ysize, ystart);

	value = xstart & OSD_LXP_XSTART_MASK;
	value |= (ystart << OSD_LXP_YSTART_SHIFT) & OSD_LXP_YSTART_MASK;

	xilinx_drm_writel(layer->base, OSD_LXP, value);

	value = xsize & OSD_LXS_XSIZE_MASK;
	value |= (ysize << OSD_LXS_YSIZE_SHIFT) & OSD_LXS_YSIZE_MASK;

	xilinx_drm_writel(layer->base, OSD_LXS, value);
}

/* enable layer */
void xilinx_osd_layer_enable(struct xilinx_osd_layer *layer)
{
	u32 value;

	DRM_DEBUG_DRIVER("layer->id: %d\n", layer->id);

	value = xilinx_drm_readl(layer->base, OSD_LXC);
	value |= OSD_LXC_EN;
	xilinx_drm_writel(layer->base, OSD_LXC, value);
}

/* disable layer */
void xilinx_osd_layer_disable(struct xilinx_osd_layer *layer)
{
	u32 value;

	DRM_DEBUG_DRIVER("layer->id: %d\n", layer->id);

	value = xilinx_drm_readl(layer->base, OSD_LXC);
	value &= ~OSD_LXC_EN;
	xilinx_drm_writel(layer->base, OSD_LXC, value);
}

/* get an available layer */
struct xilinx_osd_layer *xilinx_osd_layer_get(struct xilinx_osd *osd)
{
	struct xilinx_osd_layer *layer = NULL;
	int i;

	for (i = 0; i < osd->num_layers; i++) {
		if (osd->layers[i]->avail) {
			layer = osd->layers[i];
			layer->avail = false;
			break;
		}
	}

	if (!layer)
		return ERR_PTR(-ENODEV);

	DRM_DEBUG_DRIVER("layer id: %d\n", i);

	return layer;
}

/* put a layer */
void xilinx_osd_layer_put(struct xilinx_osd_layer *layer)
{
	layer->avail = true;
}

/* osd operations */
/* set osd color */
void xilinx_osd_set_color(struct xilinx_osd *osd, u8 r, u8 g, u8 b)
{
	u32 value;

	value = g;
	xilinx_drm_writel(osd->base, OSD_BC0, value);
	value = b;
	xilinx_drm_writel(osd->base, OSD_BC1, value);
	value = r;
	xilinx_drm_writel(osd->base, OSD_BC2, value);
}

/* set osd dimension */
void xilinx_osd_set_dimension(struct xilinx_osd *osd, u32 width, u32 height)
{
	u32 value;

	DRM_DEBUG_DRIVER("w: %d, h: %d\n", width, height);

	value = width | ((height << OSD_SS_YSIZE_SHIFT) & OSD_SS_YSIZE_MASK);
	xilinx_drm_writel(osd->base, OSD_SS, value);
}

/* get osd number of layers */
unsigned int xilinx_osd_get_num_layers(struct xilinx_osd *osd)
{
	return osd->num_layers;
}

/* get osd max width */
unsigned int xilinx_osd_get_max_width(struct xilinx_osd *osd)
{
	return osd->max_width;
}

/* get osd color format */
unsigned int xilinx_osd_get_format(struct xilinx_osd *osd)
{
	return osd->format;
}

/* reset osd */
void xilinx_osd_reset(struct xilinx_osd *osd)
{
	xilinx_drm_writel(osd->base, OSD_CTL, OSD_RST_RESET);
}

/* enable osd */
void xilinx_osd_enable(struct xilinx_osd *osd)
{
	xilinx_drm_writel(osd->base, OSD_CTL,
			  xilinx_drm_readl(osd->base, OSD_CTL) | OSD_CTL_EN);
}

/* disable osd */
void xilinx_osd_disable(struct xilinx_osd *osd)
{
	xilinx_drm_writel(osd->base, OSD_CTL,
			  xilinx_drm_readl(osd->base, OSD_CTL) & ~OSD_CTL_EN);
}

/* register-update-enable osd */
void xilinx_osd_enable_rue(struct xilinx_osd *osd)
{
	xilinx_drm_writel(osd->base, OSD_CTL,
			  xilinx_drm_readl(osd->base, OSD_CTL) | OSD_CTL_RUE);
}

/* register-update-enable osd */
void xilinx_osd_disable_rue(struct xilinx_osd *osd)
{
	xilinx_drm_writel(osd->base, OSD_CTL,
			  xilinx_drm_readl(osd->base, OSD_CTL) & ~OSD_CTL_RUE);
}

static const struct of_device_id xilinx_osd_of_match[] = {
	{ .compatible = "xlnx,v-osd-5.01.a" },
	{ /* end of table */ },
};

struct xilinx_osd *xilinx_osd_probe(struct device *dev,
				    struct device_node *node)
{
	struct xilinx_osd *osd;
	struct xilinx_osd_layer *layer;
	const struct of_device_id *match;
	struct resource res;
	int i;
	int ret;

	match = of_match_node(xilinx_osd_of_match, node);
	if (!match) {
		dev_err(dev, "failed to match the device node\n");
		return ERR_PTR(-ENODEV);
	}

	osd = devm_kzalloc(dev, sizeof(*osd), GFP_KERNEL);
	if (!osd)
		return ERR_PTR(-ENOMEM);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "failed to of_address_to_resource\n");
		return ERR_PTR(ret);
	}

	osd->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(osd->base))
		return ERR_CAST(osd->base);

	ret = of_property_read_u32(node, "xlnx,num-layers", &osd->num_layers);
	if (ret) {
		dev_warn(dev, "failed to get num of layers prop\n");
		return ERR_PTR(ret);
	}

	ret = of_property_read_u32(node, "xlnx,screen-width", &osd->max_width);
	if (ret) {
		dev_warn(dev, "failed to get screen width prop\n");
		return ERR_PTR(ret);
	}

	/* read the video format set by a user */
	osd->format = xilinx_drm_readl(osd->base, OSD_ENC) &
		      OSD_VIDEO_FORMAT_MASK;

	for (i = 0; i < osd->num_layers; i++) {
		layer = devm_kzalloc(dev, sizeof(*layer), GFP_KERNEL);
		if (!layer)
			return ERR_PTR(-ENOMEM);

		layer->base = osd->base + OSD_L0C + OSD_LAYER_SIZE * i;
		layer->id = i;
		layer->osd = osd;
		layer->avail = true;
		osd->layers[i] = layer;
	}

	return osd;
}

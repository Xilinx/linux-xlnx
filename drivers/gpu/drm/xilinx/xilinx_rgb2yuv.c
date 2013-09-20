/*
 * Xilinx rgb to yuv converter support for Xilinx DRM KMS
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

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "xilinx_drm_drv.h"

#include "xilinx_rgb2yuv.h"

/* registers */
/* control register */
#define RGB_CONTROL	0x000
/* active size v,h */
#define RGB_ACTIVE_SIZE	0x020

/* control register bit definition */
#define RGB_CTL_EN	(1 << 0)	/* enable */
#define RGB_CTL_RUE	(1 << 1)	/* register update enable */
#define RGB_RST_RESET	(1 << 31)	/* instant reset */

struct xilinx_rgb2yuv {
	void __iomem *base;
};

/* enable rgb2yuv */
void xilinx_rgb2yuv_enable(struct xilinx_rgb2yuv *rgb2yuv)
{
	u32 reg;

	reg = xilinx_drm_readl(rgb2yuv->base, RGB_CONTROL);
	xilinx_drm_writel(rgb2yuv->base, RGB_CONTROL, reg | RGB_CTL_EN);
}

/* disable rgb2yuv */
void xilinx_rgb2yuv_disable(struct xilinx_rgb2yuv *rgb2yuv)
{
	u32 reg;

	reg = xilinx_drm_readl(rgb2yuv->base, RGB_CONTROL);
	xilinx_drm_writel(rgb2yuv->base, RGB_CONTROL, reg & ~RGB_CTL_EN);
}

/* configure rgb2yuv */
void xilinx_rgb2yuv_configure(struct xilinx_rgb2yuv *rgb2yuv,
			      int hactive, int vactive)
{
	xilinx_drm_writel(rgb2yuv->base, RGB_ACTIVE_SIZE,
			  (vactive << 16) | hactive);
}

/* reset rgb2yuv */
void xilinx_rgb2yuv_reset(struct xilinx_rgb2yuv *rgb2yuv)
{
	u32 reg;

	xilinx_drm_writel(rgb2yuv->base, RGB_CONTROL, RGB_RST_RESET);

	/* enable register update */
	reg = xilinx_drm_readl(rgb2yuv->base, RGB_CONTROL);
	xilinx_drm_writel(rgb2yuv->base, RGB_CONTROL, reg | RGB_CTL_RUE);
}

static const struct of_device_id xilinx_rgb2yuv_of_match[] = {
	{ .compatible = "xlnx,v-rgb2ycrcb-6.01.a" },
	{ /* end of table */ },
};

/* probe rgb2yuv */
struct xilinx_rgb2yuv *xilinx_rgb2yuv_probe(struct device *dev,
					    struct device_node *node)
{
	struct xilinx_rgb2yuv *rgb2yuv;
	const struct of_device_id *match;
	struct resource res;
	int ret;

	match = of_match_node(xilinx_rgb2yuv_of_match, node);
	if (!match) {
		dev_err(dev, "failed to match the device node\n");
		return ERR_PTR(-ENODEV);
	}

	rgb2yuv = devm_kzalloc(dev, sizeof(*rgb2yuv), GFP_KERNEL);
	if (!rgb2yuv)
		return ERR_PTR(-ENOMEM);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "failed to of_address_to_resource\n");
		return ERR_PTR(ret);
	}

	rgb2yuv->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(rgb2yuv->base))
		return ERR_CAST(rgb2yuv->base);

	xilinx_rgb2yuv_reset(rgb2yuv);

	return rgb2yuv;
}

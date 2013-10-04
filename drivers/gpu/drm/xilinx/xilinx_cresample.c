/*
 * Xilinx Chroma Resampler support for Xilinx DRM KMS
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

/* registers */
/* general control registers */
#define CRESAMPLE_CONTROL		0x0000

/* horizontal and vertical active frame size */
#define CRESAMPLE_ACTIVE_SIZE		0x0020

/* control register bit definition */
#define CRESAMPLE_CTL_EN		(1 << 0)	/* enable */
#define CRESAMPLE_CTL_RU		(1 << 1)	/* reg update */
#define CRESAMPLE_CTL_RESET		(1 << 31)	/* instant reset */

struct xilinx_cresample {
	void __iomem *base;
};

/* enable cresample */
void xilinx_cresample_enable(struct xilinx_cresample *cresample)
{
	u32 reg;

	reg = xilinx_drm_readl(cresample->base, CRESAMPLE_CONTROL);
	xilinx_drm_writel(cresample->base, CRESAMPLE_CONTROL,
			  reg | CRESAMPLE_CTL_EN);
}

/* disable cresample */
void xilinx_cresample_disable(struct xilinx_cresample *cresample)
{
	u32 reg;

	reg = xilinx_drm_readl(cresample->base, CRESAMPLE_CONTROL);
	xilinx_drm_writel(cresample->base, CRESAMPLE_CONTROL,
			  reg & ~CRESAMPLE_CTL_EN);
}

/* configure cresample */
void xilinx_cresample_configure(struct xilinx_cresample *cresample,
				int hactive, int vactive)
{
	/* configure hsize and vsize */
	xilinx_drm_writel(cresample->base, CRESAMPLE_ACTIVE_SIZE,
			  (vactive << 16) | hactive);
}

/* reset cresample */
void xilinx_cresample_reset(struct xilinx_cresample *cresample)
{
	u32 reg;

	xilinx_drm_writel(cresample->base, CRESAMPLE_CONTROL,
			  CRESAMPLE_CTL_RESET);

	/* enable register update */
	reg = xilinx_drm_readl(cresample->base, CRESAMPLE_CONTROL);
	xilinx_drm_writel(cresample->base, CRESAMPLE_CONTROL,
			  reg | CRESAMPLE_CTL_RU);
}

struct xilinx_cresample *xilinx_cresample_probe(struct device *dev,
						struct device_node *node)
{
	struct xilinx_cresample *cresample;
	struct resource res;
	int ret;

	cresample = devm_kzalloc(dev, sizeof(*cresample), GFP_KERNEL);
	if (!cresample)
		return ERR_PTR(-ENOMEM);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "failed to of_address_to_resource\n");
		return ERR_PTR(ret);
	}

	cresample->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(cresample->base))
		return ERR_CAST(cresample->base);

	xilinx_cresample_reset(cresample);

	return cresample;
}

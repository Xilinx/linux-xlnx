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

#include "xilinx_cresample.h"

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
	const char *input_format_name;
	const char *output_format_name;
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

/* get an input format */
const char *
xilinx_cresample_get_input_format_name(struct xilinx_cresample *cresample)
{
	return cresample->input_format_name;
}

/* get an output format */
const char *
xilinx_cresample_get_output_format_name(struct xilinx_cresample *cresample)
{
	return cresample->output_format_name;
}

static const struct of_device_id xilinx_cresample_of_match[] = {
	{ .compatible = "xlnx,v-cresample-3.01.a" },
	{ /* end of table */ },
};

struct xilinx_cresample *xilinx_cresample_probe(struct device *dev,
						struct device_node *node)
{
	struct xilinx_cresample *cresample;
	const struct of_device_id *match;
	struct resource res;
	int ret;

	match = of_match_node(xilinx_cresample_of_match, node);
	if (!match) {
		dev_err(dev, "failed to match the device node\n");
		return ERR_PTR(-ENODEV);
	}

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

	ret = of_property_read_string(node, "xlnx,input-format",
				      &cresample->input_format_name);
	if (ret) {
		dev_warn(dev, "failed to get an input format prop\n");
		return ERR_PTR(ret);
	}

	ret = of_property_read_string(node, "xlnx,output-format",
				      &cresample->output_format_name);
	if (ret) {
		dev_warn(dev, "failed to get an output format prop\n");
		return ERR_PTR(ret);
	}

	xilinx_cresample_reset(cresample);

	return cresample;
}

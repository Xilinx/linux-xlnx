// SPDX-License-Identifier: GPL-2.0
/*
 * Video Timing Controller support for Xilinx DRM KMS
 *
 * Copyright (C) 2013 - 2018 Xilinx, Inc.
 *
 * Author: Hyun Woo Kwon <hyunk@xilinx.com>
 *	   Saurabh Sengar <saurabhs@xilinx.com>
 *	   Vishal Sagar <vishal.sagar@xilinx.com>
 *
 * This driver adds support to control the Xilinx Video Timing
 * Controller connected to the CRTC.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <video/videomode.h>
#include "xlnx_bridge.h"

/* register offsets */
#define XVTC_CTL		0x000
#define XVTC_VER		0x010
#define XVTC_GASIZE		0x060
#define XVTC_GENC		0x068
#define XVTC_GPOL		0x06c
#define XVTC_GHSIZE		0x070
#define XVTC_GVSIZE		0x074
#define XVTC_GHSYNC		0x078
#define XVTC_GVBHOFF_F0		0x07c
#define XVTC_GVSYNC_F0		0x080
#define XVTC_GVSHOFF_F0		0x084
#define XVTC_GVBHOFF_F1		0x088
#define XVTC_GVSYNC_F1		0x08C
#define XVTC_GVSHOFF_F1		0x090
#define XVTC_GASIZE_F1		0x094

/* vtc control register bits */
#define XVTC_CTL_SWRESET	BIT(31)
#define XVTC_CTL_FIPSS		BIT(26)
#define XVTC_CTL_ACPSS		BIT(25)
#define XVTC_CTL_AVPSS		BIT(24)
#define XVTC_CTL_HSPSS		BIT(23)
#define XVTC_CTL_VSPSS		BIT(22)
#define XVTC_CTL_HBPSS		BIT(21)
#define XVTC_CTL_VBPSS		BIT(20)
#define XVTC_CTL_VCSS		BIT(18)
#define XVTC_CTL_VASS		BIT(17)
#define XVTC_CTL_VBSS		BIT(16)
#define XVTC_CTL_VSSS		BIT(15)
#define XVTC_CTL_VFSS		BIT(14)
#define XVTC_CTL_VTSS		BIT(13)
#define XVTC_CTL_HBSS		BIT(11)
#define XVTC_CTL_HSSS		BIT(10)
#define XVTC_CTL_HFSS		BIT(9)
#define XVTC_CTL_HTSS		BIT(8)
#define XVTC_CTL_GE		BIT(2)
#define XVTC_CTL_RU		BIT(1)

/* vtc generator polarity register bits */
#define XVTC_GPOL_FIP		BIT(6)
#define XVTC_GPOL_ACP		BIT(5)
#define XVTC_GPOL_AVP		BIT(4)
#define XVTC_GPOL_HSP		BIT(3)
#define XVTC_GPOL_VSP		BIT(2)
#define XVTC_GPOL_HBP		BIT(1)
#define XVTC_GPOL_VBP		BIT(0)

/* vtc generator horizontal 1 */
#define XVTC_GH1_BPSTART_MASK	GENMASK(28, 16)
#define XVTC_GH1_BPSTART_SHIFT	16
#define XVTC_GH1_SYNCSTART_MASK GENMASK(12, 0)
/* vtc generator vertical 1 (field 0) */
#define XVTC_GV1_BPSTART_MASK	GENMASK(28, 16)
#define XVTC_GV1_BPSTART_SHIFT	16
#define XVTC_GV1_SYNCSTART_MASK	GENMASK(12, 0)
/* vtc generator/detector vblank/vsync horizontal offset registers */
#define XVTC_XVXHOX_HEND_MASK	GENMASK(28, 16)
#define XVTC_XVXHOX_HEND_SHIFT	16
#define XVTC_XVXHOX_HSTART_MASK	GENMASK(12, 0)

#define XVTC_GHFRAME_HSIZE	GENMASK(12, 0)
#define XVTC_GVFRAME_HSIZE_F1	GENMASK(12, 0)
#define XVTC_GA_ACTSIZE_MASK	GENMASK(12, 0)

/* vtc generator encoding register bits */
#define XVTC_GENC_INTERL	BIT(6)

/**
 * struct xlnx_vtc - Xilinx VTC object
 *
 * @bridge: xilinx bridge structure
 * @dev: device structure
 * @base: base addr
 * @ppc: pixels per clock
 * @axi_clk: AXI Lite clock
 * @vid_clk: Video clock
 */
struct xlnx_vtc {
	struct xlnx_bridge bridge;
	struct device *dev;
	void __iomem *base;
	u32 ppc;
	struct clk *axi_clk;
	struct clk *vid_clk;
};

static inline void xlnx_vtc_writel(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

static inline u32 xlnx_vtc_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

static inline struct xlnx_vtc *bridge_to_vtc(struct xlnx_bridge *bridge)
{
	return container_of(bridge, struct xlnx_vtc, bridge);
}

static void xlnx_vtc_reset(struct xlnx_vtc *vtc)
{
	u32 reg;

	xlnx_vtc_writel(vtc->base, XVTC_CTL, XVTC_CTL_SWRESET);

	/* enable register update */
	reg = xlnx_vtc_readl(vtc->base, XVTC_CTL);
	xlnx_vtc_writel(vtc->base, XVTC_CTL, reg | XVTC_CTL_RU);
}

/**
 * xlnx_vtc_enable - Enable the VTC
 * @bridge: xilinx bridge structure pointer
 *
 * Return:
 * Zero on success.
 *
 * This function enables the VTC
 */
static int xlnx_vtc_enable(struct xlnx_bridge *bridge)
{
	u32 reg;
	struct xlnx_vtc *vtc = bridge_to_vtc(bridge);

	/* enable generator */
	reg = xlnx_vtc_readl(vtc->base, XVTC_CTL);
	xlnx_vtc_writel(vtc->base, XVTC_CTL, reg | XVTC_CTL_GE);
	dev_dbg(vtc->dev, "enabled\n");
	return 0;
}

/**
 * xlnx_vtc_disable - Disable the VTC
 * @bridge: xilinx bridge structure pointer
 *
 * This function disables and resets the VTC.
 */
static void xlnx_vtc_disable(struct xlnx_bridge *bridge)
{
	u32 reg;
	struct xlnx_vtc *vtc = bridge_to_vtc(bridge);

	/* disable generator and reset */
	reg = xlnx_vtc_readl(vtc->base, XVTC_CTL);
	xlnx_vtc_writel(vtc->base, XVTC_CTL, reg & ~XVTC_CTL_GE);
	xlnx_vtc_reset(vtc);
	dev_dbg(vtc->dev, "disabled\n");
}

/**
 * xlnx_vtc_set_timing - Configures the VTC
 * @bridge: xilinx bridge structure pointer
 * @vm: video mode requested
 *
 * Return:
 * Zero on success.
 *
 * This function calculates the timing values from the video mode
 * structure passed from the CRTC and configures the VTC.
 */
static int xlnx_vtc_set_timing(struct xlnx_bridge *bridge,
			       struct videomode *vm)
{
	u32 reg;
	u32 htotal, hactive, hsync_start, hbackporch_start;
	u32 vtotal, vactive, vsync_start, vbackporch_start;
	struct xlnx_vtc *vtc = bridge_to_vtc(bridge);

	reg = xlnx_vtc_readl(vtc->base, XVTC_CTL);
	xlnx_vtc_writel(vtc->base, XVTC_CTL, reg & ~XVTC_CTL_RU);

	vm->hactive /= vtc->ppc;
	vm->hfront_porch /= vtc->ppc;
	vm->hback_porch /= vtc->ppc;
	vm->hsync_len /= vtc->ppc;

	htotal = vm->hactive + vm->hfront_porch + vm->hsync_len +
		 vm->hback_porch;
	vtotal = vm->vactive + vm->vfront_porch + vm->vsync_len +
		 vm->vback_porch;

	hactive = vm->hactive;
	vactive = vm->vactive;

	hsync_start = vm->hactive + vm->hfront_porch;
	vsync_start = vm->vactive + vm->vfront_porch;

	hbackporch_start = hsync_start + vm->hsync_len;
	vbackporch_start = vsync_start + vm->vsync_len;

	dev_dbg(vtc->dev, "ha: %d, va: %d\n", hactive, vactive);
	dev_dbg(vtc->dev, "ht: %d, vt: %d\n", htotal, vtotal);
	dev_dbg(vtc->dev, "hs: %d, hb: %d\n", hsync_start, hbackporch_start);
	dev_dbg(vtc->dev, "vs: %d, vb: %d\n", vsync_start, vbackporch_start);

	reg = htotal & XVTC_GHFRAME_HSIZE;
	xlnx_vtc_writel(vtc->base, XVTC_GHSIZE, reg);

	reg = vtotal & XVTC_GVFRAME_HSIZE_F1;
	reg |= reg << XVTC_GV1_BPSTART_SHIFT;
	xlnx_vtc_writel(vtc->base, XVTC_GVSIZE, reg);

	reg = hactive & XVTC_GA_ACTSIZE_MASK;
	reg |= (vactive & XVTC_GA_ACTSIZE_MASK) << 16;
	xlnx_vtc_writel(vtc->base, XVTC_GASIZE, reg);

	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		xlnx_vtc_writel(vtc->base, XVTC_GASIZE_F1, reg);

	reg = hsync_start & XVTC_GH1_SYNCSTART_MASK;
	reg |= (hbackporch_start << XVTC_GH1_BPSTART_SHIFT) &
	       XVTC_GH1_BPSTART_MASK;
	xlnx_vtc_writel(vtc->base, XVTC_GHSYNC, reg);

	reg = vsync_start & XVTC_GV1_SYNCSTART_MASK;
	reg |= (vbackporch_start << XVTC_GV1_BPSTART_SHIFT) &
	       XVTC_GV1_BPSTART_MASK;
	xlnx_vtc_writel(vtc->base, XVTC_GVSYNC_F0, reg);

	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		xlnx_vtc_writel(vtc->base, XVTC_GVSYNC_F1, reg);
		reg = xlnx_vtc_readl(vtc->base, XVTC_GENC) | XVTC_GENC_INTERL;
		xlnx_vtc_writel(vtc->base, XVTC_GENC, reg);
	} else {
		reg = xlnx_vtc_readl(vtc->base, XVTC_GENC) & ~XVTC_GENC_INTERL;
		xlnx_vtc_writel(vtc->base, XVTC_GENC, reg);
	}

	/* configure horizontal offset */
	/* Calculate and update Generator VBlank Hori field 0 */
	reg = hactive & XVTC_XVXHOX_HSTART_MASK;
	reg |= (hactive << XVTC_XVXHOX_HEND_SHIFT) &
		XVTC_XVXHOX_HEND_MASK;
	xlnx_vtc_writel(vtc->base, XVTC_GVBHOFF_F0, reg);

	/* Calculate and update Generator VSync Hori field 0 */
	reg = hsync_start & XVTC_XVXHOX_HSTART_MASK;
	reg |= (hsync_start << XVTC_XVXHOX_HEND_SHIFT) &
		XVTC_XVXHOX_HEND_MASK;
	xlnx_vtc_writel(vtc->base, XVTC_GVSHOFF_F0, reg);

	/* Calculate and update Generator VBlank Hori field 1 */
	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		reg = hactive & XVTC_XVXHOX_HSTART_MASK;
		reg |= (hactive << XVTC_XVXHOX_HEND_SHIFT) &
			XVTC_XVXHOX_HEND_MASK;
		xlnx_vtc_writel(vtc->base, XVTC_GVBHOFF_F1, reg);
	}

	/* Calculate and update Generator VBlank Hori field 1 */
	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		reg =  (hsync_start - (htotal / 2)) & XVTC_XVXHOX_HSTART_MASK;
		reg |= ((hsync_start - (htotal / 2)) <<
			XVTC_XVXHOX_HEND_SHIFT) & XVTC_XVXHOX_HEND_MASK;
	} else {
		reg =  hsync_start & XVTC_XVXHOX_HSTART_MASK;
		reg |= (hsync_start << XVTC_XVXHOX_HEND_SHIFT) &
			XVTC_XVXHOX_HEND_MASK;
	}

	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		xlnx_vtc_writel(vtc->base, XVTC_GVSHOFF_F1, reg);

	/* configure polarity of signals */
	reg = 0;
	reg |= XVTC_GPOL_ACP;
	reg |= XVTC_GPOL_AVP;
	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		reg |= XVTC_GPOL_FIP;
	if (vm->flags & DISPLAY_FLAGS_VSYNC_HIGH) {
		reg |= XVTC_GPOL_VBP;
		reg |= XVTC_GPOL_VSP;
	}
	if (vm->flags & DISPLAY_FLAGS_HSYNC_HIGH) {
		reg |= XVTC_GPOL_HBP;
		reg |= XVTC_GPOL_HSP;
	}
	xlnx_vtc_writel(vtc->base, XVTC_GPOL, reg);

	/* configure timing source */
	reg = xlnx_vtc_readl(vtc->base, XVTC_CTL);
	reg |= XVTC_CTL_VCSS;
	reg |= XVTC_CTL_VASS;
	reg |= XVTC_CTL_VBSS;
	reg |= XVTC_CTL_VSSS;
	reg |= XVTC_CTL_VFSS;
	reg |= XVTC_CTL_VTSS;
	reg |= XVTC_CTL_HBSS;
	reg |= XVTC_CTL_HSSS;
	reg |= XVTC_CTL_HFSS;
	reg |= XVTC_CTL_HTSS;
	xlnx_vtc_writel(vtc->base, XVTC_CTL, reg);

	reg = xlnx_vtc_readl(vtc->base, XVTC_CTL);
	xlnx_vtc_writel(vtc->base, XVTC_CTL, reg | XVTC_CTL_RU);
	dev_dbg(vtc->dev, "set timing done\n");

	return 0;
}

static int xlnx_vtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct xlnx_vtc *vtc;
	struct resource *res;
	int ret;

	vtc = devm_kzalloc(dev, sizeof(*vtc), GFP_KERNEL);
	if (!vtc)
		return -ENOMEM;

	vtc->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get resource for device\n");
		return -EFAULT;
	}

	vtc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(vtc->base)) {
		dev_err(dev, "failed to remap io region\n");
		return PTR_ERR(vtc->base);
	}

	platform_set_drvdata(pdev, vtc);

	ret = of_property_read_u32(dev->of_node, "xlnx,pixels-per-clock",
				   &vtc->ppc);
	if (ret || (vtc->ppc != 1 && vtc->ppc != 2 && vtc->ppc != 4)) {
		dev_err(dev, "failed to get ppc\n");
		return ret;
	}
	dev_info(dev, "vtc ppc = %d\n", vtc->ppc);

	vtc->axi_clk = devm_clk_get(vtc->dev, "s_axi_aclk");
	if (IS_ERR(vtc->axi_clk)) {
		ret = PTR_ERR(vtc->axi_clk);
		dev_err(dev, "failed to get axi lite clk %d\n", ret);
		return ret;
	}

	vtc->vid_clk = devm_clk_get(vtc->dev, "clk");
	if (IS_ERR(vtc->vid_clk)) {
		ret = PTR_ERR(vtc->vid_clk);
		dev_err(dev, "failed to get video clk %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vtc->axi_clk);
	if (ret) {
		dev_err(vtc->dev, "unable to enable axilite clk %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vtc->vid_clk);
	if (ret) {
		dev_err(vtc->dev, "unable to enable video clk %d\n", ret);
		goto err_axi_clk;
	}

	xlnx_vtc_reset(vtc);

	vtc->bridge.enable = &xlnx_vtc_enable;
	vtc->bridge.disable = &xlnx_vtc_disable;
	vtc->bridge.set_timing = &xlnx_vtc_set_timing;
	vtc->bridge.of_node = dev->of_node;
	ret = xlnx_bridge_register(&vtc->bridge);
	if (ret) {
		dev_err(dev, "Bridge registration failed\n");
		goto err_vid_clk;
	}

	dev_info(dev, "Xilinx VTC IP version : 0x%08x\n",
		 xlnx_vtc_readl(vtc->base, XVTC_VER));
	dev_info(dev, "Xilinx VTC DRM Bridge driver probed\n");
	return 0;

err_vid_clk:
	clk_disable_unprepare(vtc->vid_clk);
err_axi_clk:
	clk_disable_unprepare(vtc->axi_clk);
	return ret;
}

static int xlnx_vtc_remove(struct platform_device *pdev)
{
	struct xlnx_vtc *vtc = platform_get_drvdata(pdev);

	xlnx_bridge_unregister(&vtc->bridge);
	clk_disable_unprepare(vtc->vid_clk);
	clk_disable_unprepare(vtc->axi_clk);

	return 0;
}

static const struct of_device_id xlnx_vtc_of_match[] = {
	{ .compatible = "xlnx,bridge-v-tc-6.1" },
	{ /* end of table */ },
};

MODULE_DEVICE_TABLE(of, xlnx_vtc_of_match);

static struct platform_driver xlnx_vtc_bridge_driver = {
	.probe = xlnx_vtc_probe,
	.remove = xlnx_vtc_remove,
	.driver = {
		.name = "xlnx,bridge-vtc",
		.of_match_table = xlnx_vtc_of_match,
	},
};

module_platform_driver(xlnx_vtc_bridge_driver);

MODULE_AUTHOR("Vishal Sagar");
MODULE_DESCRIPTION("Xilinx VTC Bridge Driver");
MODULE_LICENSE("GPL v2");


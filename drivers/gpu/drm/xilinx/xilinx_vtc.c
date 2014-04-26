/*
 * Video Timing Controller support for Xilinx DRM KMS
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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#include <video/videomode.h>

#include "xilinx_drm_drv.h"
#include "xilinx_vtc.h"

/* register offsets */
#define VTC_CTL		0x000	/* control */
#define VTC_STATS	0x004	/* status */
#define VTC_ERROR	0x008	/* error */

#define VTC_GASIZE	0x060	/* generator active size */
#define VTC_GPOL	0x06c	/* generator polarity */
#define VTC_GHSIZE	0x070	/* generator frame horizontal size */
#define VTC_GVSIZE	0x074	/* generator frame vertical size */
#define VTC_GHSYNC	0x078	/* generator horizontal sync */
#define VTC_GVBHOFF	0x07c	/* generator vblank horizontal offset */
#define VTC_GVSYNC	0x080	/* generator vertical sync */
#define VTC_GVSHOFF	0x084	/* generator vsync horizontal offset */

#define VTC_RESET	0x000	/* reset register */
#define VTC_ISR		0x004	/* interrupt status register */
#define VTC_IER		0x00c	/* interrupt enable register */

/* control register bit */
#define VTC_CTL_FIP	(1 << 6)	/* field id output polarity */
#define VTC_CTL_ACP	(1 << 5)	/* active chroma output polarity */
#define VTC_CTL_AVP	(1 << 4)	/* active video output polarity */
#define VTC_CTL_HSP	(1 << 3)	/* hori sync output polarity */
#define VTC_CTL_VSP	(1 << 2)	/* vert sync output polarity */
#define VTC_CTL_HBP	(1 << 1)	/* hori blank output polarity */
#define VTC_CTL_VBP	(1 << 0)	/* vert blank output polarity */

#define VTC_CTL_FIPSS	(1 << 26)	/* field id output polarity source */
#define VTC_CTL_ACPSS	(1 << 25)	/* active chroma out polarity source */
#define VTC_CTL_AVPSS	(1 << 24)	/* active video out polarity source */
#define VTC_CTL_HSPSS	(1 << 23)	/* hori sync out polarity source */
#define VTC_CTL_VSPSS	(1 << 22)	/* vert sync out polarity source */
#define VTC_CTL_HBPSS	(1 << 21)	/* hori blank out polarity source */
#define VTC_CTL_VBPSS	(1 << 20)	/* vert blank out polarity source */

#define VTC_CTL_VCSS	(1 << 18)	/* chroma source select */
#define VTC_CTL_VASS	(1 << 17)	/* vertical offset source select */
#define VTC_CTL_VBSS	(1 << 16)	/* vertical sync end source select */
#define VTC_CTL_VSSS	(1 << 15)	/* vertical sync start source select */
#define VTC_CTL_VFSS	(1 << 14)	/* vertical active size source select */
#define VTC_CTL_VTSS	(1 << 13)	/* vertical frame size source select */

#define VTC_CTL_HBSS	(1 << 11)	/* horiz sync end source select */
#define VTC_CTL_HSSS	(1 << 10)	/* horiz sync start source select */
#define VTC_CTL_HFSS	(1 << 9)	/* horiz active size source select */
#define VTC_CTL_HTSS	(1 << 8)	/* horiz frame size source select */

#define VTC_CTL_GE	(1 << 2)	/* vtc generator enable */
#define VTC_CTL_RU	(1 << 1)	/* vtc register update */

/* vtc generator horizontal 1 */
#define VTC_GH1_BPSTART_MASK   0x1fff0000	/* horiz back porch start */
#define VTC_GH1_BPSTART_SHIFT  16
#define VTC_GH1_SYNCSTART_MASK 0x00001fff

/* vtc generator vertical 1 (filed 0) */
#define VTC_GV1_BPSTART_MASK   0x1fff0000	/* vertical back porch start */
#define VTC_GV1_BPSTART_SHIFT  16
#define VTC_GV1_SYNCSTART_MASK 0x00001fff

/* vtc generator/detector vblank/vsync horizontal offset registers */
#define VTC_XVXHOX_HEND_MASK	0x1fff0000	/* horizontal offset end */
#define VTC_XVXHOX_HEND_SHIFT	16		/* horizontal offset end
						   shift */
#define VTC_XVXHOX_HSTART_MASK	0x00001fff	/* horizontal offset start */

/* reset register bit definition */
#define VTC_RESET_RESET		(1 << 31)	/* Software Reset */

/* interrupt status/enable register bit definition */
#define VTC_IXR_FSYNC15		(1 << 31)	/* frame sync interrupt 15 */
#define VTC_IXR_FSYNC14		(1 << 30)	/* frame sync interrupt 14 */
#define VTC_IXR_FSYNC13		(1 << 29)	/* frame sync interrupt 13 */
#define VTC_IXR_FSYNC12		(1 << 28)	/* frame sync interrupt 12 */
#define VTC_IXR_FSYNC11		(1 << 27)	/* frame sync interrupt 11 */
#define VTC_IXR_FSYNC10		(1 << 26)	/* frame sync interrupt 10 */
#define VTC_IXR_FSYNC09		(1 << 25)	/* frame sync interrupt 09 */
#define VTC_IXR_FSYNC08		(1 << 24)	/* frame sync interrupt 08 */
#define VTC_IXR_FSYNC07		(1 << 23)	/* frame sync interrupt 07 */
#define VTC_IXR_FSYNC06		(1 << 22)	/* frame sync interrupt 06 */
#define VTC_IXR_FSYNC05		(1 << 21)	/* frame sync interrupt 05 */
#define VTC_IXR_FSYNC04		(1 << 20)	/* frame sync interrupt 04 */
#define VTC_IXR_FSYNC03		(1 << 19)	/* frame sync interrupt 03 */
#define VTC_IXR_FSYNC02		(1 << 18)	/* frame sync interrupt 02 */
#define VTC_IXR_FSYNC01		(1 << 17)	/* frame sync interrupt 01 */
#define VTC_IXR_FSYNC00		(1 << 16)	/* frame sync interrupt 00 */
#define VTC_IXR_FSYNCALL_MASK	(VTC_IXR_FSYNC00 |	\
				VTC_IXR_FSYNC01 |	\
				VTC_IXR_FSYNC02 |	\
				VTC_IXR_FSYNC03 |	\
				VTC_IXR_FSYNC04 |	\
				VTC_IXR_FSYNC05 |	\
				VTC_IXR_FSYNC06 |	\
				VTC_IXR_FSYNC07 |	\
				VTC_IXR_FSYNC08 |	\
				VTC_IXR_FSYNC09 |	\
				VTC_IXR_FSYNC10 |	\
				VTC_IXR_FSYNC11 |	\
				VTC_IXR_FSYNC12 |	\
				VTC_IXR_FSYNC13 |	\
				VTC_IXR_FSYNC14 |	\
				VTC_IXR_FSYNC15)

#define VTC_IXR_G_AV		(1 << 13)	/* generator actv video intr */
#define VTC_IXR_G_VBLANK	(1 << 12)	/* generator vblank interrupt */
#define VTC_IXR_G_ALL_MASK	(VTC_IXR_G_AV |	\
				 VTC_IXR_G_VBLANK)	/* all generator intr */

#define VTC_IXR_D_AV		(1 << 11)	/* detector active video intr */
#define VTC_IXR_D_VBLANK	(1 << 10)	/* detector vblank interrupt */
#define VTC_IXR_D_ALL_MASK	(VTC_IXR_D_AV |	\
				VTC_IXR_D_VBLANK)	/* all detector intr */

#define VTC_IXR_LOL		(1 << 9)	/* lock loss */
#define VTC_IXR_LO		(1 << 8)	/* lock  */
#define VTC_IXR_LOCKALL_MASK	(VTC_IXR_LOL |	\
				VTC_IXR_LO)	/* all signal lock intr */

#define VTC_IXR_ACL	(1 << 21)	/* active chroma signal lock */
#define VTC_IXR_AVL	(1 << 20)	/* active video signal lock */
#define VTC_IXR_HSL	(1 << 19)	/* horizontal sync signal
						   lock */
#define VTC_IXR_VSL	(1 << 18)	/* vertical sync signal lock */
#define VTC_IXR_HBL	(1 << 17)	/* horizontal blank signal
						   lock */
#define VTC_IXR_VBL	(1 << 16)	/* vertical blank signal lock */

/* mask for all interrupts */
#define VTC_IXR_ALLINTR_MASK	(VTC_IXR_FSYNCALL_MASK |	\
				VTC_IXR_G_ALL_MASK |		\
				VTC_IXR_D_ALL_MASK |		\
				VTC_IXR_LOCKALL_MASK)
/**
 * struct xilinx_vtc - Xilinx VTC object
 *
 * @base: base addr
 * @irq: irq
 * @vblank_fn: vblank handler func
 * @vblank_data: vblank handler private data
 */
struct xilinx_vtc {
	void __iomem *base;
	int irq;
	void (*vblank_fn)(void *);
	void *vblank_data;
};

/**
 * struct xilinx_vtc_polarity - vtc polarity config
 *
 * @active_chroma: active chroma polarity
 * @active_video: active video polarity
 * @field_id: field ID polarity
 * @vblank: vblank polarity
 * @vsync: vsync polarity
 * @hblank: hblank polarity
 * @hsync: hsync polarity
 */
struct xilinx_vtc_polarity {
	u8 active_chroma;
	u8 active_video;
	u8 field_id;
	u8 vblank;
	u8 vsync;
	u8 hblank;
	u8 hsync;
};

/**
 * struct xilinx_vtc_hori_offset - vtc horizontal offset config
 *
 * @vblank_hori_start: vblank horizontal start
 * @vblank_hori_end: vblank horizontal end
 * @vsync_hori_start: vsync horizontal start
 * @vsync_hori_end: vsync horizontal end
 */
struct xilinx_vtc_hori_offset {
	u16 vblank_hori_start;
	u16 vblank_hori_end;
	u16 vsync_hori_start;
	u16 vsync_hori_end;
};

/**
 * struct xilinx_vtc_src_config - vtc source config
 *
 * @field_id_pol: filed id polarity source
 * @active_chroma_pol: active chroma polarity source
 * @active_video_pol: active video polarity source
 * @hsync_pol: hsync polarity source
 * @vsync_pol: vsync polarity source
 * @hblank_pol: hblnak polarity source
 * @vblank_pol: vblank polarity source
 * @vchroma: vchroma polarity start source
 * @vactive: vactive size source
 * @vbackporch: vbackporch start source
 * @vsync: vsync start source
 * @vfrontporch: vfrontporch start source
 * @vtotal: vtotal size source
 * @hactive: hactive start source
 * @hbackporch: hbackporch start source
 * @hsync: hsync start source
 * @hfrontporch: hfrontporch start source
 * @htotal: htotal size source
 */
struct xilinx_vtc_src_config {
	u8 field_id_pol;
	u8 active_chroma_pol;
	u8 active_video_pol;
	u8 hsync_pol;
	u8 vsync_pol;
	u8 hblank_pol;
	u8 vblank_pol;

	u8 vchroma;
	u8 vactive;
	u8 vbackporch;
	u8 vsync;
	u8 vfrontporch;
	u8 vtotal;

	u8 hactive;
	u8 hbackporch;
	u8 hsync;
	u8 hfrontporch;
	u8 htotal;
};

/* configure polarity of signals */
static void xilinx_vtc_config_polarity(struct xilinx_vtc *vtc,
				       struct xilinx_vtc_polarity *polarity)
{
	u32 reg;

	reg = xilinx_drm_readl(vtc->base, VTC_GPOL);

	if (polarity->active_chroma)
		reg |= VTC_CTL_ACP;
	if (polarity->active_video)
		reg |= VTC_CTL_AVP;
	if (polarity->field_id)
		reg |= VTC_CTL_FIP;
	if (polarity->vblank)
		reg |= VTC_CTL_VBP;
	if (polarity->vsync)
		reg |= VTC_CTL_VSP;
	if (polarity->hblank)
		reg |= VTC_CTL_HBP;
	if (polarity->hsync)
		reg |= VTC_CTL_HSP;

	xilinx_drm_writel(vtc->base, VTC_GPOL, reg);
}

/* configure horizontal offset */
static void
xilinx_vtc_config_hori_offset(struct xilinx_vtc *vtc,
			      struct xilinx_vtc_hori_offset *hori_offset)
{
	u32 reg;

	reg = hori_offset->vblank_hori_start & VTC_XVXHOX_HSTART_MASK;
	reg |= (hori_offset->vblank_hori_end << VTC_XVXHOX_HEND_SHIFT) &
	       VTC_XVXHOX_HEND_MASK;
	xilinx_drm_writel(vtc->base, VTC_GVBHOFF, reg);

	reg = hori_offset->vsync_hori_start & VTC_XVXHOX_HSTART_MASK;
	reg |= (hori_offset->vsync_hori_end << VTC_XVXHOX_HEND_SHIFT) &
	       VTC_XVXHOX_HEND_MASK;
	xilinx_drm_writel(vtc->base, VTC_GVSHOFF, reg);
}

/* configure source */
static void xilinx_vtc_config_src(struct xilinx_vtc *vtc,
				  struct xilinx_vtc_src_config *src_config)
{
	u32 reg;

	reg = xilinx_drm_readl(vtc->base, VTC_CTL);

	if (src_config->field_id_pol)
		reg |= VTC_CTL_FIPSS;
	if (src_config->active_chroma_pol)
		reg |= VTC_CTL_ACPSS;
	if (src_config->active_video_pol)
		reg |= VTC_CTL_AVPSS;
	if (src_config->hsync_pol)
		reg |= VTC_CTL_HSPSS;
	if (src_config->vsync_pol)
		reg |= VTC_CTL_VSPSS;
	if (src_config->hblank_pol)
		reg |= VTC_CTL_HBPSS;
	if (src_config->vblank_pol)
		reg |= VTC_CTL_VBPSS;

	if (src_config->vchroma)
		reg |= VTC_CTL_VCSS;
	if (src_config->vactive)
		reg |= VTC_CTL_VASS;
	if (src_config->vbackporch)
		reg |= VTC_CTL_VBSS;
	if (src_config->vsync)
		reg |= VTC_CTL_VSSS;
	if (src_config->vfrontporch)
		reg |= VTC_CTL_VFSS;
	if (src_config->vtotal)
		reg |= VTC_CTL_VTSS;

	if (src_config->hbackporch)
		reg |= VTC_CTL_HBSS;
	if (src_config->hsync)
		reg |= VTC_CTL_HSSS;
	if (src_config->hfrontporch)
		reg |= VTC_CTL_HFSS;
	if (src_config->htotal)
		reg |= VTC_CTL_HTSS;

	xilinx_drm_writel(vtc->base, VTC_CTL, reg);
}

/* enable vtc */
void xilinx_vtc_enable(struct xilinx_vtc *vtc)
{
	u32 reg;

	/* enable a generator only for now */
	reg = xilinx_drm_readl(vtc->base, VTC_CTL);
	xilinx_drm_writel(vtc->base, VTC_CTL, reg | VTC_CTL_GE);
}

/* disable vtc */
void xilinx_vtc_disable(struct xilinx_vtc *vtc)
{
	u32 reg;

	/* disable a generator only for now */
	reg = xilinx_drm_readl(vtc->base, VTC_CTL);
	xilinx_drm_writel(vtc->base, VTC_CTL, reg & ~VTC_CTL_GE);
}

/* configure vtc signals */
void xilinx_vtc_config_sig(struct xilinx_vtc *vtc,
			   struct videomode *vm)
{
	u32 reg;
	u32 htotal, hactive, hsync_start, hbackporch_start;
	u32 vtotal, vactive, vsync_start, vbackporch_start;
	struct xilinx_vtc_hori_offset hori_offset;
	struct xilinx_vtc_polarity polarity;
	struct xilinx_vtc_src_config src;

	reg = xilinx_drm_readl(vtc->base, VTC_CTL);
	xilinx_drm_writel(vtc->base, VTC_CTL, reg & ~VTC_CTL_RU);

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

	reg = htotal & 0x1fff;
	xilinx_drm_writel(vtc->base, VTC_GHSIZE, reg);

	reg = vtotal & 0x1fff;
	xilinx_drm_writel(vtc->base, VTC_GVSIZE, reg);

	DRM_DEBUG_DRIVER("ht: %d, vt: %d\n", htotal, vtotal);

	reg = hactive & 0x1fff;
	reg |= (vactive & 0x1fff) << 16;
	xilinx_drm_writel(vtc->base, VTC_GASIZE, reg);

	DRM_DEBUG_DRIVER("ha: %d, va: %d\n", hactive, vactive);

	reg = hsync_start & VTC_GH1_SYNCSTART_MASK;
	reg |= (hbackporch_start << VTC_GH1_BPSTART_SHIFT) &
	       VTC_GH1_BPSTART_MASK;
	xilinx_drm_writel(vtc->base, VTC_GHSYNC, reg);

	DRM_DEBUG_DRIVER("hs: %d, hb: %d\n", hsync_start, hbackporch_start);

	reg = vsync_start & VTC_GV1_SYNCSTART_MASK;
	reg |= (vbackporch_start << VTC_GV1_BPSTART_SHIFT) &
	       VTC_GV1_BPSTART_MASK;
	xilinx_drm_writel(vtc->base, VTC_GVSYNC, reg);

	DRM_DEBUG_DRIVER("vs: %d, vb: %d\n", vsync_start, vbackporch_start);

	hori_offset.vblank_hori_start = hactive;
	hori_offset.vblank_hori_end = hactive;
	hori_offset.vsync_hori_start = hactive;
	hori_offset.vsync_hori_end = hactive;

	xilinx_vtc_config_hori_offset(vtc, &hori_offset);

	/* set up polarity */
	memset(&polarity, 0x0, sizeof(polarity));
	polarity.hsync = 1;
	polarity.vsync = 1;
	polarity.hblank = 1;
	polarity.vblank = 1;
	polarity.active_video = 1;
	polarity.active_chroma = 1;
	polarity.field_id = 1;
	xilinx_vtc_config_polarity(vtc, &polarity);

	/* set up src config */
	memset(&src, 0x0, sizeof(src));
	src.vchroma = 1;
	src.vactive = 1;
	src.vbackporch = 1;
	src.vsync = 1;
	src.vfrontporch = 1;
	src.vtotal = 1;
	src.hactive = 1;
	src.hbackporch = 1;
	src.hsync = 1;
	src.hfrontporch = 1;
	src.htotal = 1;
	xilinx_vtc_config_src(vtc, &src);

	reg = xilinx_drm_readl(vtc->base, VTC_CTL);
	xilinx_drm_writel(vtc->base, VTC_CTL, reg | VTC_CTL_RU);
}

/* reset vtc */
void xilinx_vtc_reset(struct xilinx_vtc *vtc)
{
	u32 reg;

	xilinx_drm_writel(vtc->base, VTC_RESET, VTC_RESET_RESET);

	/* enable register update */
	reg = xilinx_drm_readl(vtc->base, VTC_CTL);
	xilinx_drm_writel(vtc->base, VTC_CTL, reg | VTC_CTL_RU);
}

/* enable interrupt */
static inline void xilinx_vtc_intr_enable(struct xilinx_vtc *vtc, u32 intr)
{
	xilinx_drm_writel(vtc->base, VTC_IER, (intr & VTC_IXR_ALLINTR_MASK) |
			  xilinx_drm_readl(vtc->base, VTC_IER));
}

/* disable interrupt */
static inline void xilinx_vtc_intr_disable(struct xilinx_vtc *vtc, u32 intr)
{
	xilinx_drm_writel(vtc->base, VTC_IER, ~(intr & VTC_IXR_ALLINTR_MASK) &
			  xilinx_drm_readl(vtc->base, VTC_IER));
}

/* get interrupt */
static inline u32 xilinx_vtc_intr_get(struct xilinx_vtc *vtc)
{
	return xilinx_drm_readl(vtc->base, VTC_IER) &
	       xilinx_drm_readl(vtc->base, VTC_ISR) & VTC_IXR_ALLINTR_MASK;
}

/* clear interrupt */
static inline void xilinx_vtc_intr_clear(struct xilinx_vtc *vtc, u32 intr)
{
	xilinx_drm_writel(vtc->base, VTC_ISR, intr & VTC_IXR_ALLINTR_MASK);
}

/* interrupt handler */
static irqreturn_t xilinx_vtc_intr_handler(int irq, void *data)
{
	struct xilinx_vtc *vtc = data;

	u32 intr = xilinx_vtc_intr_get(vtc);

	if (!intr)
		return IRQ_NONE;

	if ((intr & VTC_IXR_G_VBLANK) && (vtc->vblank_fn))
		vtc->vblank_fn(vtc->vblank_data);

	xilinx_vtc_intr_clear(vtc, intr);

	return IRQ_HANDLED;
}

/* enable vblank interrupt */
void xilinx_vtc_enable_vblank_intr(struct xilinx_vtc *vtc,
				   void (*vblank_fn)(void *),
				   void *vblank_priv)
{
	vtc->vblank_fn = vblank_fn;
	vtc->vblank_data = vblank_priv;
	xilinx_vtc_intr_enable(vtc, VTC_IXR_G_VBLANK);
}

/* disable vblank interrupt */
void xilinx_vtc_disable_vblank_intr(struct xilinx_vtc *vtc)
{
	xilinx_vtc_intr_disable(vtc, VTC_IXR_G_VBLANK);
	vtc->vblank_data = NULL;
	vtc->vblank_fn = NULL;
}

static const struct of_device_id xilinx_vtc_of_match[] = {
	{ .compatible = "xlnx,v-tc-5.01.a" },
	{ /* end of table */ },
};

/* probe vtc */
struct xilinx_vtc *xilinx_vtc_probe(struct device *dev,
				    struct device_node *node)
{
	struct xilinx_vtc *vtc;
	const struct of_device_id *match;
	struct resource res;
	int ret;

	match = of_match_node(xilinx_vtc_of_match, node);
	if (!match) {
		dev_err(dev, "failed to match the device node\n");
		return ERR_PTR(-ENODEV);
	}

	vtc = devm_kzalloc(dev, sizeof(*vtc), GFP_KERNEL);
	if (!vtc)
		return ERR_PTR(-ENOMEM);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "failed to of_address_to_resource\n");
		return ERR_PTR(ret);
	}

	vtc->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(vtc->base))
		return ERR_CAST(vtc->base);

	xilinx_vtc_intr_disable(vtc, VTC_IXR_ALLINTR_MASK);
	vtc->irq = irq_of_parse_and_map(node, 0);
	if (vtc->irq > 0) {
		ret = devm_request_irq(dev, vtc->irq, xilinx_vtc_intr_handler,
				       IRQF_SHARED, "xilinx_vtc", vtc);
		if (ret) {
			dev_warn(dev, "failed to requet_irq() for vtc\n");
			return ERR_PTR(ret);
		}
	}

	xilinx_vtc_reset(vtc);

	return vtc;
}

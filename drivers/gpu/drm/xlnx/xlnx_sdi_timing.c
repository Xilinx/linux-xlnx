// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA SDI Tx timing controller driver
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Contacts: Saurabh Sengar <saurabhs@xilinx.com>
 */

#include <drm/drmP.h>
#include <linux/device.h>
#include <video/videomode.h>
#include "xlnx_sdi_timing.h"

/* timing controller register offsets */
#define XSTC_CTL	0x00
#define XSTC_STATS	0x04
#define XSTC_ERROR	0x08
#define XSTC_GASIZE	0x60
#define XSTC_GENC	0x68
#define XSTC_GPOL	0x6c
#define XSTC_GHSIZE	0x70
#define XSTC_GVSIZE	0x74
#define XSTC_GHSYNC	0x78
#define XSTC_GVBH_F0	0x7c
#define XSTC_GVSYNC_F0	0x80
#define XSTC_GVSH_F0	0x84
#define XSTC_GVBH_F1	0x88
#define XSTC_GVSYNC_F1	0x8C
#define XSTC_GVSH_F1	0x90
#define XSTC_GASIZE_F1	0x94
#define XSTC_OFFSET	0x10000

/* timing controller register bit */
#define XSTC_CTL_FIP	BIT(6)	/* field id polarity */
#define XSTC_CTL_ACP	BIT(5)	/* active chroma polarity */
#define XSTC_CTL_AVP	BIT(4)	/* active video polarity */
#define XSTC_CTL_HSP	BIT(3)	/* hori sync polarity */
#define XSTC_CTL_VSP	BIT(2)	/* vert sync polarity */
#define XSTC_CTL_HBP	BIT(1)	/* hori blank polarity */
#define XSTC_CTL_VBP	BIT(0)	/* vert blank polarity */
#define XSTC_CTL_FIPSS	BIT(26)	/* field id polarity source */
#define XSTC_CTL_ACPSS	BIT(25)	/* active chroma polarity src */
#define XSTC_CTL_AVPSS	BIT(24)	/* active video polarity src */
#define XSTC_CTL_HSPSS	BIT(23)	/* hori sync polarity src */
#define XSTC_CTL_VSPSS	BIT(22)	/* vert sync polarity src */
#define XSTC_CTL_HBPSS	BIT(21)	/* hori blank polarity src */
#define XSTC_CTL_VBPSS	BIT(20)	/* vert blank polarity src */
#define XSTC_CTL_VCSS	BIT(18)	/* chroma src */
#define XSTC_CTL_VASS	BIT(17)	/* vertical offset src */
#define XSTC_CTL_VBSS	BIT(16)	/* vertical sync end src */
#define XSTC_CTL_VSSS	BIT(15)	/* vertical sync start src */
#define XSTC_CTL_VFSS	BIT(14)	/* vertical active size src */
#define XSTC_CTL_VTSS	BIT(13)	/* vertical frame size src */
#define XSTC_CTL_HBSS	BIT(11)	/* horiz sync end src */
#define XSTC_CTL_HSSS	BIT(10)	/* horiz sync start src */
#define XSTC_CTL_HFSS	BIT(9)	/* horiz active size src */
#define XSTC_CTL_HTSS	BIT(8)	/* horiz frame size src */
#define XSTC_CTL_GE	BIT(2)	/* timing generator enable */
#define XSTC_CTL_RU	BIT(1)	/* timing register update */

/* timing generator horizontal 1 */
#define XSTC_GH1_BPSTART_MASK	GENMASK(28, 16)
#define XSTC_GH1_BPSTART_SHIFT	16
#define XSTC_GH1_SYNCSTART_MASK	GENMASK(12, 0)
/* timing generator vertical 1 (filed 0) */
#define XSTC_GV1_BPSTART_MASK	GENMASK(28, 16)
#define XSTC_GV1_BPSTART_SHIFT	16
#define XSTC_GV1_SYNCSTART_MASK	GENMASK(12, 0)
/* timing generator/detector vblank/vsync horizontal offset registers */
#define XSTC_XVXHOX_HEND_MASK	GENMASK(28, 16)
#define XSTC_XVXHOX_HEND_SHIFT	16
#define XSTC_XVXHOX_HSTART_MASK	GENMASK(12, 0)

#define XSTC_GHFRAME_HSIZE	GENMASK(12, 0)
#define XSTC_GVFRAME_HSIZE_F1	GENMASK(12, 0)
#define XSTC_GA_ACTSIZE_MASK	GENMASK(12, 0)
/* reset register bit definition */
#define XSTC_RST		BIT(31)
/* Interlaced bit in XSTC_GENC */
#define XSTC_GENC_INTERL	BIT(6)

/**
 * struct xlnx_stc_polarity - timing signal polarity
 *
 * @field_id: field ID polarity
 * @vblank: vblank polarity
 * @vsync: vsync polarity
 * @hblank: hblank polarity
 * @hsync: hsync polarity
 */
struct xlnx_stc_polarity {
	u8 field_id;
	u8 vblank;
	u8 vsync;
	u8 hblank;
	u8 hsync;
};

/**
 * struct xlnx_stc_hori_off - timing signal horizontal offset
 *
 * @v0blank_hori_start: vblank horizontal start (field 0)
 * @v0blank_hori_end: vblank horizontal end (field 0)
 * @v0sync_hori_start: vsync horizontal start (field 0)
 * @v0sync_hori_end: vsync horizontal end (field 0)
 * @v1blank_hori_start: vblank horizontal start (field 1)
 * @v1blank_hori_end: vblank horizontal end (field 1)
 * @v1sync_hori_start: vsync horizontal start (field 1)
 * @v1sync_hori_end: vsync horizontal end (field 1)
 */
struct xlnx_stc_hori_off {
	u16 v0blank_hori_start;
	u16 v0blank_hori_end;
	u16 v0sync_hori_start;
	u16 v0sync_hori_end;
	u16 v1blank_hori_start;
	u16 v1blank_hori_end;
	u16 v1sync_hori_start;
	u16 v1sync_hori_end;
};

/**
 * xlnx_stc_writel - Memory mapped SDI Tx timing controller write
 * @base:	Pointer to SDI Tx registers base
 * @offset:	Register offset
 * @val:	value to be written
 *
 * This function writes the value to SDI TX timing controller registers
 */
static inline void xlnx_stc_writel(void __iomem *base, int offset, u32 val)
{
	writel(val, base + XSTC_OFFSET + offset);
}

/**
 * xlnx_stc_readl - Memory mapped timing controllerregister read
 * @base:	Pointer to SDI Tx registers base
 * @offset:	Register offset
 *
 * Return: The contents of the SDI Tx timing controller register
 *
 * This function returns the contents of the corresponding SDI Tx register.
 */
static inline u32 xlnx_stc_readl(void __iomem *base, int offset)
{
	return readl(base + XSTC_OFFSET + offset);
}

/**
 * xlnx_stc_enable - Enable timing controller
 * @base:	Base address of SDI Tx subsystem
 *
 * This function enables the SDI Tx subsystem's timing controller
 */
void xlnx_stc_enable(void __iomem *base)
{
	u32 reg;

	reg = xlnx_stc_readl(base, XSTC_CTL);
	xlnx_stc_writel(base, XSTC_CTL, reg | XSTC_CTL_GE);
}

/**
 * xlnx_stc_disable - Disable timing controller
 * @base:	Base address of SDI Tx subsystem
 *
 * This function disables the SDI Tx subsystem's timing controller
 */
void xlnx_stc_disable(void __iomem *base)
{
	u32 reg;

	reg = xlnx_stc_readl(base, XSTC_CTL);
	xlnx_stc_writel(base, XSTC_CTL, reg & ~XSTC_CTL_GE);
}

/**
 * xlnx_stc_reset - Reset timing controller
 * @base:	Base address of SDI Tx subsystem
 *
 * This function resets the SDI Tx subsystem's timing controller
 */
void xlnx_stc_reset(void __iomem *base)
{
	u32 reg;

	xlnx_stc_writel(base, XSTC_CTL, XSTC_RST);

	/* enable register update */
	reg = xlnx_stc_readl(base, XSTC_CTL);
	xlnx_stc_writel(base, XSTC_CTL, reg | XSTC_CTL_RU);
}

/**
 * xlnx_stc_polarity - Configure timing signal polarity
 * @base:	Base address of SDI Tx subsystem
 * @polarity:	timing signal polarity data
 *
 * This function configure timing signal polarity
 */
static void xlnx_stc_polarity(void __iomem *base,
			      struct xlnx_stc_polarity *polarity)
{
	u32 reg = 0;

	reg = XSTC_CTL_ACP;
	reg |= XSTC_CTL_AVP;
	if (polarity->field_id)
		reg |= XSTC_CTL_FIP;
	if (polarity->vblank)
		reg |= XSTC_CTL_VBP;
	if (polarity->vsync)
		reg |= XSTC_CTL_VSP;
	if (polarity->hblank)
		reg |= XSTC_CTL_HBP;
	if (polarity->hsync)
		reg |= XSTC_CTL_HSP;

	xlnx_stc_writel(base, XSTC_GPOL, reg);
}

/**
 * xlnx_stc_hori_off - Configure horzontal timing offset
 * @base:	Base address of SDI Tx subsystem
 * @hori_off:	horizontal offset configuration data
 * @flags:	Display flags
 *
 * This function configure horizontal offset
 */
static void xlnx_stc_hori_off(void __iomem *base,
			      struct xlnx_stc_hori_off *hori_off,
			      enum display_flags flags)
{
	u32 reg;

	/* Calculate and update Generator VBlank Hori field 0 */
	reg = hori_off->v0blank_hori_start & XSTC_XVXHOX_HSTART_MASK;
	reg |= (hori_off->v0blank_hori_end << XSTC_XVXHOX_HEND_SHIFT) &
		XSTC_XVXHOX_HEND_MASK;
	xlnx_stc_writel(base, XSTC_GVBH_F0, reg);

	/* Calculate and update Generator VSync Hori field 0 */
	reg = hori_off->v0sync_hori_start & XSTC_XVXHOX_HSTART_MASK;
	reg |= (hori_off->v0sync_hori_end << XSTC_XVXHOX_HEND_SHIFT) &
		XSTC_XVXHOX_HEND_MASK;
	xlnx_stc_writel(base, XSTC_GVSH_F0, reg);

	/* Calculate and update Generator VBlank Hori field 1 */
	if (flags & DISPLAY_FLAGS_INTERLACED) {
		reg = hori_off->v1blank_hori_start & XSTC_XVXHOX_HSTART_MASK;
		reg |= (hori_off->v1blank_hori_end << XSTC_XVXHOX_HEND_SHIFT) &
			XSTC_XVXHOX_HEND_MASK;
		xlnx_stc_writel(base, XSTC_GVBH_F1, reg);
	}

	/* Calculate and update Generator VBlank Hori field 1 */
	if (flags & DISPLAY_FLAGS_INTERLACED) {
		reg = hori_off->v1sync_hori_start & XSTC_XVXHOX_HSTART_MASK;
		reg |= (hori_off->v1sync_hori_end << XSTC_XVXHOX_HEND_SHIFT) &
			XSTC_XVXHOX_HEND_MASK;
		xlnx_stc_writel(base, XSTC_GVSH_F1, reg);
	}
}

/**
 * xlnx_stc_src - Configure timing source
 * @base:	Base address of SDI Tx subsystem
 *
 * This function configure timing source
 */
static void xlnx_stc_src(void __iomem *base)
{
	u32 reg;

	reg = xlnx_stc_readl(base, XSTC_CTL);
	reg |= XSTC_CTL_VCSS;
	reg |= XSTC_CTL_VASS;
	reg |= XSTC_CTL_VBSS;
	reg |= XSTC_CTL_VSSS;
	reg |= XSTC_CTL_VFSS;
	reg |= XSTC_CTL_VTSS;
	reg |= XSTC_CTL_HBSS;
	reg |= XSTC_CTL_HSSS;
	reg |= XSTC_CTL_HFSS;
	reg |= XSTC_CTL_HTSS;
	xlnx_stc_writel(base, XSTC_CTL, reg);
}

/**
 * xlnx_stc_sig - Generates timing signal
 * @base:	Base address of SDI Tx subsystem
 * @vm:		video mode
 *
 * This function generated the timing for given vide mode
 */
void xlnx_stc_sig(void __iomem *base, struct videomode *vm)
{
	u32 reg;
	u32 htotal, hactive, hsync_start, hbackporch_start;
	u32 vtotal, vactive, vsync_start, vbackporch_start;
	struct xlnx_stc_hori_off hori_off;
	struct xlnx_stc_polarity polarity;

	reg = xlnx_stc_readl(base, XSTC_CTL);
	xlnx_stc_writel(base, XSTC_CTL, reg & ~XSTC_CTL_RU);

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

	DRM_DEBUG_DRIVER("ha: %d, va: %d\n", hactive, vactive);
	DRM_DEBUG_DRIVER("hs: %d, hb: %d\n", hsync_start, hbackporch_start);
	DRM_DEBUG_DRIVER("vs: %d, vb: %d\n", vsync_start, vbackporch_start);
	DRM_DEBUG_DRIVER("ht: %d, vt: %d\n", htotal, vtotal);

	reg = htotal & XSTC_GHFRAME_HSIZE;
	xlnx_stc_writel(base, XSTC_GHSIZE, reg);
	reg = vtotal & XSTC_GVFRAME_HSIZE_F1;
	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		if (vm->pixelclock == 148500000)
			reg |= (reg + 2) <<
				XSTC_GV1_BPSTART_SHIFT;
		else
			reg |= (reg + 1) <<
				XSTC_GV1_BPSTART_SHIFT;
	} else {
		reg |= reg << XSTC_GV1_BPSTART_SHIFT;
	}
	xlnx_stc_writel(base, XSTC_GVSIZE, reg);
	reg = hactive & XSTC_GA_ACTSIZE_MASK;
	reg |= (vactive & XSTC_GA_ACTSIZE_MASK) << 16;
	xlnx_stc_writel(base, XSTC_GASIZE, reg);

	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		if (vactive == 243)
			reg = ((vactive + 1) & XSTC_GA_ACTSIZE_MASK) << 16;
		else
			reg = (vactive & XSTC_GA_ACTSIZE_MASK) << 16;
		xlnx_stc_writel(base, XSTC_GASIZE_F1, reg);
	}

	reg = hsync_start & XSTC_GH1_SYNCSTART_MASK;
	reg |= (hbackporch_start << XSTC_GH1_BPSTART_SHIFT) &
	       XSTC_GH1_BPSTART_MASK;
	xlnx_stc_writel(base, XSTC_GHSYNC, reg);
	reg = vsync_start & XSTC_GV1_SYNCSTART_MASK;
	reg |= (vbackporch_start << XSTC_GV1_BPSTART_SHIFT) &
	       XSTC_GV1_BPSTART_MASK;

	/*
	 * Fix the Vsync_vstart and vsync_vend of Field 0
	 * for all interlaced modes including 3GB.
	 */
	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		reg = ((((reg & XSTC_GV1_BPSTART_MASK) >>
			XSTC_GV1_BPSTART_SHIFT) - 1) <<
			XSTC_GV1_BPSTART_SHIFT) |
			((reg & XSTC_GV1_SYNCSTART_MASK) - 1);

	xlnx_stc_writel(base, XSTC_GVSYNC_F0, reg);

	/*
	 * Fix the Vsync_vstart and vsync_vend of Field 1
	 * for interlaced and 3GB modes.
	 */
	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		if (vm->pixelclock == 148500000)
			/* Revert and increase by 1 for 3GB mode */
			reg = ((((reg & XSTC_GV1_BPSTART_MASK) >>
				XSTC_GV1_BPSTART_SHIFT) + 2) <<
				XSTC_GV1_BPSTART_SHIFT) |
				((reg & XSTC_GV1_SYNCSTART_MASK) + 2);
		else
			/* Only revert the reduction */
			reg = ((((reg & XSTC_GV1_BPSTART_MASK) >>
				XSTC_GV1_BPSTART_SHIFT) + 1) <<
				XSTC_GV1_BPSTART_SHIFT) |
				((reg & XSTC_GV1_SYNCSTART_MASK) + 1);
	}

	hori_off.v0blank_hori_start = hactive;
	hori_off.v0blank_hori_end = hactive;
	hori_off.v0sync_hori_start = hsync_start;
	hori_off.v0sync_hori_end = hsync_start;
	hori_off.v1blank_hori_start = hactive;
	hori_off.v1blank_hori_end = hactive;

	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		hori_off.v1sync_hori_start = hsync_start - (htotal / 2);
		hori_off.v1sync_hori_end = hsync_start - (htotal / 2);
		xlnx_stc_writel(base, XSTC_GVSYNC_F1, reg);
		reg = xlnx_stc_readl(base, XSTC_GENC)
				     | XSTC_GENC_INTERL;
		xlnx_stc_writel(base, XSTC_GENC, reg);
	} else {
		hori_off.v1sync_hori_start = hsync_start;
		hori_off.v1sync_hori_end = hsync_start;
		reg = xlnx_stc_readl(base, XSTC_GENC)
				     & ~XSTC_GENC_INTERL;
		xlnx_stc_writel(base, XSTC_GENC, reg);
	}

	xlnx_stc_hori_off(base, &hori_off, vm->flags);
	/* set up polarity */
	memset(&polarity, 0x0, sizeof(polarity));
	polarity.hsync = !!(vm->flags & DISPLAY_FLAGS_HSYNC_LOW);
	polarity.vsync = !!(vm->flags & DISPLAY_FLAGS_VSYNC_LOW);
	polarity.hblank = !!(vm->flags & DISPLAY_FLAGS_HSYNC_LOW);
	polarity.vblank = !!(vm->flags & DISPLAY_FLAGS_VSYNC_LOW);
	polarity.field_id = !!(vm->flags & DISPLAY_FLAGS_INTERLACED);
	xlnx_stc_polarity(base, &polarity);

	xlnx_stc_src(base);

	reg = xlnx_stc_readl(base, XSTC_CTL);
	xlnx_stc_writel(base, XSTC_CTL, reg | XSTC_CTL_RU);
}

/*
 *  drivers/video/dm270fb.c
 *
 *  Copyright (C) 2004 Chee Tim Loh <lohct@pacific.net.sg>
 *
 *  Based on drivers/video/cyber2000fb.c
 *  Copyright (C) 1998-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Integraphics CyberPro 2000, 2010 and 5000 frame buffer device
 *
 * Based on cyberfb.c.
 *
 * Note that we now use the new fbcon fix, var and cmap scheme.  We do
 * still have to check which console is the currently displayed one
 * however, especially for the colourmap stuff.
 *
 * We also use the new hotplug PCI subsystem.  I'm not sure if there
 * are any such cards, but I'm erring on the side of caution.  We don't
 * want to go pop just because someone does have one.
 *
 * Note that this doesn't work fully in the case of multiple CyberPro
 * cards with grabbers.  We currently can only attach to the first
 * CyberPro card found.
 *
 * When we're in truecolour mode, we power down the LUT RAM as a power
 * saving feature.  Also, when we enter any of the powersaving modes
 * (except soft blanking) we power down the RAMDACs.  This saves about
 * 1W, which is roughly 8% of the power consumption of a NetWinder
 * (which, incidentally, is about the same saving as a 2.5in hard disk
 * entering standby mode.)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/page-flags.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dm270-id.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include "dm270fb.h"

/* -------------------- Global Variables ----------------------------------- */

static struct fb_info dm270fbinfo[DM270FB_NR_FB];
static char *dm270fb_options __initdata = NULL;

static struct dm270fb_cfg dm270fb_bootcfg = {
	.noaccel		= 1,
	.nopan			= 1,
	.nowrap			= 1,
	.nohwcursor		= 1,
	.cmap_inverse		= 0,
	.cmap_static		= 1,
	.fontname		= {'\0'},
	.disp_type		= DM270FB_DEFAULT_DISPTYPE,	/* composite */
	.vidout_std		= DM270FB_DEFAULT_VIDFMT,	/* NTSC */
};

static struct fb_var_screeninfo dm270fb_bootvar = {
	.xres			= DM270FB_DEFAULT_XRES,		/* 640 */
	.yres			= DM270FB_DEFAULT_YRES,		/* 480 */
	.xres_virtual		= DM270FB_DEFAULT_XRES,
	.yres_virtual		= DM270FB_DEFAULT_YRES,
	.xoffset		= 0,
	.yoffset		= 0,
	.bits_per_pixel		= DM270FB_DEFAULT_BPP,		/* 8 */
	.grayscale		= 0,
	/* for bpp <= 8, length of red = length of green = length of blue = bpp */
	.red			= {0, DM270FB_DEFAULT_BPP, 0},
	.green			= {0, DM270FB_DEFAULT_BPP, 0},
	.blue			= {0, DM270FB_DEFAULT_BPP, 0},
	.transp			= {0, 0, 0},
	.nonstd			= 0,
	.activate		= FB_ACTIVATE_NOW,
	.height			= -1,
	.width			= -1,
	.accel_flags		= 0,
	.pixclock		= DM270FB_DEFAULT_PIXCLOCK,	/* 0 */
	.left_margin		= DM270FB_DEFAULT_LEFT_MARGIN,	/* 0 */
	.right_margin		= DM270FB_DEFAULT_RIGHT_MARGIN,	/* 0 */
	.upper_margin		= DM270FB_DEFAULT_UPPER_MARGIN,	/* 0 */
	.lower_margin		= DM270FB_DEFAULT_LOWER_MARGIN,	/* 0 */
	.hsync_len		= DM270FB_DEFAULT_HSYNC_LEN,	/* 0 */
	.vsync_len		= DM270FB_DEFAULT_VSYNC_LEN,	/* 0 */
	.sync			= DM270FB_DEFAULT_SYNC,		/* csync */
	.vmode			= DM270FB_DEFAULT_VMODE,	/* interlaced */
	.rotate			= 0,
};

/* -------------------- ROM Color Lookup Table ----------------------------- */

static u16 dm270fb_romclut_red[] = {
	0x0000, 0xa400, 0x0000, 0x8600, 0x0000, 0x9e00, 0x0000, 0xc000, 
	0xb900, 0x9900, 0xfb00, 0x0800, 0x1000, 0x1800, 0x2100, 0x2900, 
	0x3100, 0x4a00, 0x5a00, 0x7300, 0x7b00, 0x9400, 0xa500, 0xbd00, 
	0x4c00, 0x7f00, 0x7700, 0x6700, 0xa500, 0x8d00, 0x8400, 0x8600, 
	0x8800, 0xae00, 0xa600, 0xa800, 0xc800, 0xf200, 0xae00, 0x8f00, 
	0xec00, 0xa600, 0x8d00, 0xdd00, 0x6400, 0xb700, 0xd700, 0x6700, 
	0xc500, 0xce00, 0xfb00, 0x5200, 0xa800, 0xff00, 0xca00, 0x7800, 
	0x7600, 0xd700, 0xe800, 0xff00, 0xff00, 0x6000, 0x5800, 0xff00, 
	0xff00, 0x6600, 0x2400, 0x6000, 0x5800, 0x3c00, 0x9800, 0x5e00, 
	0x5600, 0x9400, 0x8c00, 0x7b00, 0x3500, 0x6200, 0xea00, 0x4700, 
	0x8900, 0xdf00, 0x6800, 0xbf00, 0x9d00, 0xe400, 0xaf00, 0xdc00, 
	0xe500, 0xe800, 0xd900, 0xe100, 0xe900, 0xda00, 0xc000, 0x4f00, 
	0xef00, 0xac00, 0xe200, 0xeb00, 0xe300, 0xf500, 0xff00, 0xe600, 
	0x8000, 0x8900, 0x7600, 0x7600, 0x6300, 0x5100, 0x4500, 0x3d00, 
	0x3c00, 0x2a00, 0x4500, 0x0800, 0x1600, 0x0e00, 0x3600, 0x6d00, 
	0x1600, 0x0600, 0x2600, 0x0d00, 0x1700, 0x1f00, 0x1700, 0x0f00, 
	0x1100, 0x0100, 0x0200, 0x0000, 0x0000, 0x0500, 0x8100, 0x3f00, 
	0x2600, 0x1e00, 0x1500, 0x4c00, 0x2e00, 0x2900, 0x8f00, 0x0500, 
	0x6800, 0x5800, 0x1400, 0x0000, 0x0000, 0xa200, 0x6d00, 0x0000, 
	0x7e00, 0x7400, 0x6200, 0x6000, 0x0000, 0xaf00, 0x8e00, 0x4c00, 
	0x2b00, 0x1200, 0x0000, 0x0000, 0xe400, 0xc800, 0xb400, 0x9c00, 
	0x8b00, 0x1e00, 0x7000, 0x5100, 0x6000, 0x7600, 0x6500, 0x5d00, 
	0x3f00, 0x4400, 0x5800, 0x0000, 0x2500, 0x5700, 0x5500, 0x4400, 
	0x3600, 0x1200, 0x9300, 0x8300, 0x7200, 0x6200, 0x2800, 0x1a00, 
	0x0d00, 0x7400, 0x3200, 0x0600, 0x0000, 0x0000, 0x0000, 0xa700, 
	0x4400, 0x3c00, 0x2300, 0x2800, 0x2a00, 0xda00, 0x7700, 0x3900, 
	0x4300, 0x1e00, 0x3b00, 0x3200, 0x2a00, 0x0000, 0x0000, 0xa900, 
	0x8800, 0x6f00, 0x2d00, 0x2500, 0x0600, 0x0100, 0x5600, 0x4600, 
	0x4500, 0xac00, 0x9b00, 0x6a00, 0x6200, 0x5100, 0x5900, 0x4900, 
	0x4100, 0x6a00, 0x6200, 0x2000, 0x4800, 0x1700, 0x0f00, 0x4c00, 
	0x2300, 0x8a00, 0x5e00, 0x6700, 0x8a00, 0x0000, 0xff00, 0xa000, 
	0x8000, 0xfe00, 0x0000, 0xff00, 0x0000, 0xff00, 0x0000, 0xff00, 
};
static u16 dm270fb_romclut_green[] = {
	0x0000, 0x0000, 0x9a00, 0x8d00, 0x0000, 0x0000, 0x8d00, 0xc000, 
	0xe000, 0xca00, 0xfb00, 0x0800, 0x1000, 0x1800, 0x2100, 0x2900, 
	0x3100, 0x4a00, 0x5a00, 0x7300, 0x7b00, 0x9400, 0xa500, 0xbd00, 
	0x4000, 0x6900, 0x6100, 0x5000, 0x7800, 0x6000, 0x5700, 0x4d00, 
	0x4500, 0x5400, 0x4c00, 0x4200, 0x4100, 0x0000, 0x3000, 0x0700, 
	0x0a00, 0x4c00, 0x3300, 0x1d00, 0x0a00, 0x0c00, 0x0b00, 0x0100, 
	0x7500, 0x2f00, 0x2400, 0x0200, 0x2100, 0x1400, 0x6500, 0x1300, 
	0x1c00, 0x5c00, 0x4a00, 0x2e00, 0x2400, 0x1e00, 0x1400, 0x3800, 
	0x4000, 0x5b00, 0x1900, 0x4d00, 0x4500, 0x3400, 0x7b00, 0x5600, 
	0x4e00, 0x8200, 0x7b00, 0x6900, 0x2d00, 0x4f00, 0xd100, 0x3d00, 
	0x8000, 0xcf00, 0x5f00, 0xb100, 0x8d00, 0xc400, 0xa000, 0xbc00, 
	0xc000, 0xdb00, 0xca00, 0xd200, 0xd800, 0xc600, 0xaf00, 0x4800, 
	0xec00, 0xa500, 0xcc00, 0xd100, 0xc900, 0xd700, 0xe300, 0xe200, 
	0xd600, 0xd500, 0xd900, 0xd800, 0xd100, 0xc800, 0xcc00, 0xc400, 
	0xcc00, 0xc600, 0x9900, 0x3700, 0xbf00, 0xb700, 0x7e00, 0xc100, 
	0xbe00, 0xab00, 0x3300, 0x4b00, 0x8100, 0xba00, 0xb200, 0x7900, 
	0xa000, 0x2600, 0x8500, 0x2f00, 0x6800, 0x4000, 0x8d00, 0x4b00, 
	0x3200, 0x2a00, 0x2100, 0x6400, 0xa800, 0xba00, 0xc700, 0x9f00, 
	0xb500, 0xa500, 0x8d00, 0x8d00, 0x7d00, 0xce00, 0xa500, 0x7300, 
	0xb600, 0xb500, 0xa600, 0xac00, 0x7a00, 0xc600, 0xa500, 0x6300, 
	0x4200, 0x2900, 0x6200, 0x6b00, 0xef00, 0xdd00, 0xd400, 0xbd00, 
	0xab00, 0x2900, 0x9b00, 0x7100, 0x8b00, 0xab00, 0x9c00, 0x9200, 
	0x6a00, 0x7b00, 0xa400, 0x7100, 0x7100, 0x8300, 0x8a00, 0x7800, 
	0x6200, 0x6900, 0xb300, 0xa300, 0x9200, 0x8200, 0x4800, 0x4e00, 
	0x5800, 0x8b00, 0x4900, 0x4500, 0x4e00, 0x3500, 0x3e00, 0xbb00, 
	0x5800, 0x5000, 0x3700, 0x4700, 0x5f00, 0xe500, 0x8200, 0x5800, 
	0x5800, 0x2700, 0x5000, 0x4800, 0x4000, 0x1d00, 0x1d00, 0xb400, 
	0x9300, 0x7a00, 0x3800, 0x3000, 0x2400, 0x1400, 0x6100, 0x4f00, 
	0x4d00, 0xac00, 0x9a00, 0x6a00, 0x6200, 0x5100, 0x5800, 0x4900, 
	0x4100, 0x6900, 0x6100, 0x2000, 0x4700, 0x1600, 0x0f00, 0x3f00, 
	0x1600, 0x6600, 0x4800, 0x4f00, 0x6600, 0x0000, 0xfb00, 0x9e00, 
	0x8000, 0x0b00, 0xff00, 0xf900, 0x0600, 0x0000, 0xf400, 0xff00, 
};
static u16 dm270fb_romclut_blue[] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0xd700, 0xb900, 0x9d00, 0xc000, 
	0xb300, 0xff00, 0xfb00, 0x0800, 0x1000, 0x1800, 0x2100, 0x2900, 
	0x3100, 0x4a00, 0x5a00, 0x7300, 0x7b00, 0x9400, 0xa500, 0xbd00, 
	0x4000, 0x6700, 0x5f00, 0x4e00, 0x7300, 0x5b00, 0x5200, 0x4800, 
	0x3e00, 0x4a00, 0x4200, 0x3900, 0x3200, 0x0000, 0x1600, 0x0000, 
	0x0000, 0x3500, 0x1c00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
	0x6000, 0x0300, 0x0000, 0x0000, 0x0000, 0x0000, 0x3d00, 0x0000, 
	0x0000, 0x2400, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
	0x0000, 0x3c00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0a00, 
	0x0200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
	0x0900, 0x0c00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0300, 
	0x1d00, 0x3800, 0x1100, 0x1f00, 0x0c00, 0x0600, 0x0000, 0x0000, 
	0x0000, 0x0000, 0x1c00, 0x0000, 0x0000, 0x0000, 0x1c00, 0x5300, 
	0x0000, 0x0000, 0x2500, 0x0400, 0x0700, 0x0800, 0x0000, 0x0000, 
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0a00, 0x8d00, 0x4b00, 
	0x3200, 0x2a00, 0x2300, 0x7400, 0xff00, 0xff00, 0xf800, 0xff00, 
	0xff00, 0xf700, 0xff00, 0xff00, 0xff00, 0xff00, 0xe600, 0xff00, 
	0xf500, 0xff00, 0xf500, 0xff00, 0xff00, 0xe400, 0xc300, 0x8100, 
	0x6000, 0x4700, 0xff00, 0xff00, 0xfd00, 0xfe00, 0xff00, 0xec00, 
	0xdd00, 0x3700, 0xdb00, 0xa300, 0xcb00, 0xfb00, 0xeb00, 0xe200, 
	0xaa00, 0xca00, 0xff00, 0xff00, 0xef00, 0xd000, 0xe800, 0xd900, 
	0xaf00, 0xff00, 0xf200, 0xe200, 0xd100, 0xc100, 0x8700, 0xbd00, 
	0xe500, 0xb900, 0x7700, 0xd200, 0xff00, 0xc200, 0xe600, 0xea00, 
	0x8700, 0x7f00, 0x6600, 0x9400, 0xdb00, 0xff00, 0xa100, 0xa500, 
	0x9500, 0x4600, 0x8d00, 0x8500, 0x7d00, 0x7900, 0x9700, 0xd300, 
	0xb200, 0x9900, 0x5700, 0x4f00, 0x9000, 0x6200, 0x8d00, 0x7e00, 
	0x9900, 0xba00, 0xab00, 0x7800, 0x7000, 0x5f00, 0x6900, 0x5700, 
	0x4f00, 0x8500, 0x7d00, 0x2e00, 0x7300, 0x2700, 0x1d00, 0x4d00, 
	0x2400, 0x8000, 0x5300, 0x5b00, 0x7300, 0x0000, 0xe700, 0xa700, 
	0x8000, 0x0000, 0x0000, 0x0000, 0xfe00, 0xff00, 0xff00, 0xff00, 
};

static struct fb_cmap dm270fb_romclut_cmap = {
	.start		= 0,
	.len		= 256,
	.red		= dm270fb_romclut_red,
	.green		= dm270fb_romclut_green,
	.blue		= dm270fb_romclut_blue,
	.transp		= NULL,
};

/* -------------------- Hardware specific routines ------------------------- */

/*
 * Initialise the DM270 hardware
 */
static void __init
dm270fb_init_hw(struct fb_info *fbinfo)
{
	struct dm270fb_par *dm270fbpar = (struct dm270fb_par *)fbinfo->par;
	unsigned int bmpwin_addr;
	unsigned int osdmode;
	unsigned int vid01 = 0;
	unsigned int vid02 = 0;
	unsigned int basepx = 0;
	unsigned int basepy = 0;
	int ii;

	if (!dm270fbpar->cfg.noinit) {
		/* Disable VENC & DAC */
		outw(0, DM270_VENC_VID01);

		/* Disable clock to OSD, VENC & DAC. */
		outw((inw(DM270_CLKC_MOD1) &
				~(DM270_CLKC_MOD1_COSD | DM270_CLKC_MOD1_CVENC |
				  DM270_CLKC_MOD1_CDAC)),
				DM270_CLKC_MOD1);

		/* Select MXI as VENC clock source, CLK_VENC as OSD clock source */
		outw((inw(DM270_CLKC_CLKC) &
				~(DM270_CLKC_CLKC_CENS0 | DM270_CLKC_CLKC_CENS1 |
				  DM270_CLKC_CLKC_COSDS | DM270_CLKC_CLKC_CENIV)),
				DM270_CLKC_CLKC);

		/* Enable clock to OSD, VENC & DAC. */
		outw((inw(DM270_CLKC_MOD1) |
				(DM270_CLKC_MOD1_COSD | DM270_CLKC_MOD1_CVENC |
				 DM270_CLKC_MOD1_CDAC)),
				DM270_CLKC_MOD1);

		// Disable VENC & DAC
		outw(0, DM270_VENC_VID01);

		/* Initialize OSD and VENC */
		bmpwin_addr = (fbinfo->fix.smem_start - CONFIG_DRAM_BASE) >> 5;
		outw((bmpwin_addr >> 16), DM270_OSD_BMPWINADH);	/* XXX potential contention between bmpwin0 & bmpwin1 */
		outw((bmpwin_addr & 0xffff), dm270fbpar->regaddr.bmpwinadl);

		osdmode = (DM270_OSD_OSDMODE_CS_CBCR |
			   DM270_OSD_OSDMODE_BCLUT_ROM |
			   DM270_OSD_OSDMODE_CABG_BLACK);

		if (dm270fbpar->cfg.disp_type == DISP_TYPE_COMP) {
			vid01 = (DM270_VENC_VID01_CRCUT_1_5MHZ |
				 DM270_VENC_VID01_SETUP_0 |
				 DM270_VENC_VID01_RGBFLT_OFF |
				 DM270_VENC_VID01_YFLT_OFF |
				 DM270_VENC_VID01_COUTEN_ENABLE |
				 DM270_VENC_VID01_BLANK_NORMAL);

			vid02 = (DM270_VENC_VID02_SSMD_NTSCPAL |
				 DM270_VENC_VID02_SCMP_YES |
				 DM270_VENC_VID02_SYSW_DISABLE |
				 DM270_VENC_VID02_VSSW_CSYNC |	/* XXX */
				 DM270_VENC_VID02_SYNE_ENABLE |
				 DM270_VENC_VID02_BREN_DISABLE |
				 DM270_VENC_VID02_BRPL_ACTIVELOW |
				 DM270_VENC_VID02_BRWDTH_0);
		} else if (dm270fbpar->cfg.disp_type == DISP_TYPE_LCD ||
			   dm270fbpar->cfg.disp_type == DISP_TYPE_TFT ||
			   dm270fbpar->cfg.disp_type == DISP_TYPE_CRT) {
		} else if (dm270fbpar->cfg.disp_type == DISP_TYPE_EPSON) {
		} else if (dm270fbpar->cfg.disp_type == DISP_TYPE_CASIO) {
		} else {
		}

		if (dm270fbpar->cfg.vidout_std == VID_FMT_NTSC) {
			vid01 |= DM270_VENC_VID01_NTPLS_NTSC;

			osdmode |= (DM270_OSD_OSDMODE_ORSZ_X1 |
					 DM270_OSD_OSDMODE_FSINV_NORMAL);

			basepx = DM270FB_OSD_BASEPX_NTSC;	/* 120 */
			basepy = DM270FB_OSD_BASEPY_NTSC;	/*  18 */
		} else if (dm270fbpar->cfg.vidout_std == VID_FMT_PAL) {
			vid01 |= DM270_VENC_VID01_NTPLS_PAL;

			osdmode |= (DM270_OSD_OSDMODE_ORSZ_X6_5 |
					 DM270_OSD_OSDMODE_FSINV_INVERTED);

			basepx = DM270FB_OSD_BASEPX_PAL;	/* 144 */
			basepy = DM270FB_OSD_BASEPY_PAL;	/*  22 */
		} else {
		}

		for (ii = 0; ii < 16; ii += 2) {
			outw(((ii + 1) << 8) | ii,
					dm270fbpar->regaddr.wbmp + ii);
		}

		outw(basepx, DM270_OSD_BASEPX);
		outw(basepy, DM270_OSD_BASEPY);
		outw(osdmode, DM270_OSD_OSDMODE);
		outw(vid02, DM270_VENC_VID02);
		outw(vid01, DM270_VENC_VID01);
	} else {
		bmpwin_addr = ((unsigned int)inw(DM270_OSD_BMPWINADH) << 16);
		bmpwin_addr |= inw(dm270fbpar->regaddr.bmpwinadl);
		fbinfo->fix.smem_start = (bmpwin_addr << 5) + CONFIG_DRAM_BASE;
		fbinfo->screen_base = phys_to_virt(fbinfo->fix.smem_start);
		DPRINTK("phys=0x%016lx virt=0x%08x len=%u\n",
			fbinfo->fix.smem_start,
			(unsigned int)fbinfo->screen_base,
			fbinfo->fix.smem_len);
	}

	DPRINTK("OSDMODE=0x%04x BASEPX=0x%04x BASEPY=0x%04x\n",
		inw(DM270_OSD_OSDMODE), inw(DM270_OSD_BASEPX),
		inw(DM270_OSD_BASEPY));
	DPRINTK("BMPWINADH=0x%04x BMPWINADL=0x%04x\n",
		inw(DM270_OSD_BMPWINADH), inw(dm270fbpar->regaddr.bmpwinadl));
	DPRINTK("VID01=0x%04x VID02=0x%04x\n",
		inw(DM270_VENC_VID01), inw(DM270_VENC_VID02));
}

static void
dm270fb_blank_display(struct fb_info *fbinfo)
{
	outw(inw(DM270_VENC_VID01) | DM270_VENC_VID01_BLANK, DM270_VENC_VID01);
	DPRINTK("Blank: VID01=0x%04x\n", inw(DM270_VENC_VID01));
}

static void
dm270fb_unblank_display(struct fb_info *fbinfo)
{
	outw(inw(DM270_VENC_VID01) & ~DM270_VENC_VID01_BLANK, DM270_VENC_VID01);
	DPRINTK("Unblank: VID01=0x%04x\n", inw(DM270_VENC_VID01));
}

/*
 * FIXME: move LCD power stuff into dm270fb_dac_powerup()
 * Also, I'm expecting that the backlight stuff should
 * be handled differently.
 */
static void
dm270fb_backlight_on(struct fb_info *fbinfo)
{
	DPRINTK("Backlight on\n");
#ifdef CONFIG_BOARD_IMPLDM270VP4
	outw(DM270_GIO_GIO06_BIT, DM270_GIO_BITSET0);
#endif
}

/*
 * FIXME: move LCD power stuff into dm270fb_dac_powerdown()
 * Also, I'm expecting that the backlight stuff should
 * be handled differently.
 */
static void
dm270fb_backlight_off(struct fb_info *fbinfo)
{
	DPRINTK("Backlight off\n");
#ifdef CONFIG_BOARD_IMPLDM270VP4
	outw(DM270_GIO_GIO06_BIT, DM270_GIO_BITCLR0);
#endif
}

static void
dm270fb_dac_powerup(struct fb_info *fbinfo)
{
	outw(inw(DM270_VENC_VID01) | DM270_VENC_VID01_DAPD, DM270_VENC_VID01);
	DPRINTK("DAC poweron: VID01=0x%04x\n", inw(DM270_VENC_VID01));
}

static void
dm270fb_dac_powerdown(struct fb_info *fbinfo)
{
	outw(inw(DM270_VENC_VID01) & ~DM270_VENC_VID01_DAPD, DM270_VENC_VID01);
	DPRINTK("DAC poweroff: VID01=0x%04x\n", inw(DM270_VENC_VID01));
}

static void
dm270fb_osd_enable(struct fb_info *fbinfo)
{
	struct dm270fb_par *dm270fbpar = (struct dm270fb_par *)fbinfo->par;

	outw(inw(dm270fbpar->regaddr.bmpwinmd) | DM270_OSD_BMPWINMD_OACT,
			dm270fbpar->regaddr.bmpwinmd);

	DPRINTK("OSD enable: BMPWINMD=0x%04x\n",
			inw(dm270fbpar->regaddr.bmpwinmd));
}

static void
dm270fb_osd_disable(struct fb_info *fbinfo)
{
	struct dm270fb_par *dm270fbpar = (struct dm270fb_par *)fbinfo->par;

	outw(inw(dm270fbpar->regaddr.bmpwinmd) & ~DM270_OSD_BMPWINMD_OACT,
			dm270fbpar->regaddr.bmpwinmd);

	DPRINTK("OSD disable: BMPWINMD=0x%04x\n",
			inw(dm270fbpar->regaddr.bmpwinmd));
}

static void
dm270fb_venc_enable(struct fb_info *fbinfo)
{
	outw(inw(DM270_VENC_VID01) | (DM270_VENC_VID01_DAOE |
			DM270_VENC_VID01_VENC), DM270_VENC_VID01);

	DPRINTK("VENC enable: VID01=0x%04x\n", inw(DM270_VENC_VID01));
}

static void
dm270fb_venc_disable(struct fb_info *fbinfo)
{
	outw(inw(DM270_VENC_VID01) & ~(DM270_VENC_VID01_DAOE |
			DM270_VENC_VID01_VENC), DM270_VENC_VID01);

	DPRINTK("VENC disable: VID01=0x%04x\n", inw(DM270_VENC_VID01));
}

static int
dm270fb_set_palettereg(unsigned int regno, unsigned int red, unsigned int green,
		unsigned int blue, unsigned int transp, struct fb_info *fbinfo)
{
	unsigned int ccir601_y;
	unsigned int ccir601_cb;
	unsigned int ccir601_cr;
	int too_long;

	if (regno >= DM270FB_NR_PALETTE) {
		WPRINTK("regno %u exceed %u CLUT entries\n",
				regno, DM270FB_NR_PALETTE);
		return -EINVAL;
	}

	/*
	 * CCIR-601 YCbCr Color Space Conversion Equation
	 *
	 * Y	= ( 77R + 150G +  29B)/256		Range: 16 ~ 235
	 * Cb	= (-44R -  87G + 131B)/256 + 128	Range: 16 ~ 240
	 * Cr	= (131R - 110G -  21B)/256 + 128	Range: 16 ~ 240
	 *
	 * R	= Y + 1.371(Cr - 128)
	 * G	= Y - 0.698(Cr - 128) - 0.336(Cb - 128)
	 * B	= Y + 1.732(Cb - 128)
	 *
	 * where R, G and B are gamma-corrected values with a nominal range of 16 to 235 
	 *
	 * Y	=  0.257R + 0.504G + 0.098B + 16	Range: 16 ~ 235
	 * Cb	= -0.148R - 0.291G + 0.439B + 128	Range: 16 ~ 240
	 * Cr	=  0.439R - 0.368G - 0.071B + 128	Range: 16 ~ 240
	 *
	 * R	= 1.164(Y - 16) + 1.596(Cr - 128)
	 * G	= 1.164(Y - 16) - 0.813(Cr - 128) - 0.392(Cb - 128)
	 * B	= 1.164(Y - 16) + 2.017(Cb - 128)
	 *
	 * where R, G and B are gamma-corrected values with a range of 0 to 255
	 */

	ccir601_y	= ((16843*red + 33030*green +  6423*blue)/65536 +  16) & 0xff;
	ccir601_cb	= ((-9699*red - 19071*green + 28770*blue)/65536 + 128) & 0xff;
	ccir601_cr	= ((28770*red - 24117*green -  4653*blue)/65536 + 128) & 0xff;

	too_long = 100000;
	while ((inw(DM270_OSD_MISCCTL) & DM270_OSD_MISCCTL_CPBSY) &&
			(too_long-- > 0));

	if (too_long <= 0) {
		WPRINTK("timeout (MISCCTL=0x%04x)\n", inw(DM270_OSD_MISCCTL));
		return -ETIMEDOUT;
	}
	
	outw((ccir601_y << 8) | ccir601_cb , DM270_OSD_CLUTRAMYCB);

#if 0
	/*
	 * XXX
	 * discrepancy between description and code in
	 * DM270 Techincal Reference Manual Ver 1.2 Sect 12.7.1.2 Pg 274
	 */
	too_long = 100000;
	while ((inw(DM270_OSD_MISCCTL) & DM270_OSD_MISCCTL_CPBSY) &&
			(too_long-- > 0));
#endif

	outw((ccir601_cr << 8) | (regno & 0xff), DM270_OSD_CLUTRAMCR);
	return 0;
}

/* -------------------- Helper routines ------------------------- */

static void
dm270fb_display_powerup(struct fb_info *fbinfo)
{
	DPRINTK("Display poweron\n");
	dm270fb_dac_powerup(fbinfo);
	dm270fb_backlight_on(fbinfo);
}

static void
dm270fb_display_powerdown(struct fb_info *fbinfo)
{
	DPRINTK("Display poweroff\n");
	dm270fb_backlight_off(fbinfo);
	dm270fb_dac_powerdown(fbinfo);
}

static void
dm270fb_display_enable(struct fb_info *fbinfo)
{
	DPRINTK("Display enable\n");
	dm270fb_osd_enable(fbinfo);
	dm270fb_venc_enable(fbinfo);
	dm270fb_display_powerup(fbinfo);
}

static void
dm270fb_display_disable(struct fb_info *fbinfo)
{
	DPRINTK("Display disable\n");
	dm270fb_display_powerdown(fbinfo);
	dm270fb_venc_disable(fbinfo);
	dm270fb_osd_disable(fbinfo);
}

static unsigned int
dm270fb_calc_linelength(struct fb_info *fbinfo)
{
	unsigned int linelength;

	/*
	 * Where width of data in SDRAM is not multiple of 32 bytes, padding
	 * must be done to make it multiple of 32 bytes
	 */
	linelength = ((((fbinfo->var.xres_virtual * fbinfo->var.bits_per_pixel)
			+ 255) >> 8) << 5);
	fbinfo->var.xres_virtual = (linelength << 3) /
			fbinfo->var.bits_per_pixel;
	return linelength;
}

/*
 * dm270fb_decode_var - Get the hardware video params out of 'var'.
 * @info: frame buffer structure that represents a single frame buffer
 * @regval: hardware register values obtained from 'var'
 */
static int
dm270fb_decode_var(struct fb_info *fbinfo, struct dm270fb_regval *regval)
{
	fbinfo->fix.line_length = dm270fb_calc_linelength(fbinfo);
	regval->bmpwinofst = (fbinfo->fix.line_length >> 5);
	regval->bmpwinxl = fbinfo->var.xres;
	regval->bmpwinxp = fbinfo->var.xoffset;

	regval->bmpwinmd = (DM270_OSD_BMPWINMD_CLUT_ROM |
			DM270_OSD_BMPWINMD_OHZ_X1 |
			DM270_OSD_BMPWINMD_OVZ_X1 |
			DM270_OSD_BMPWINMD_BLND_0_8 |
			DM270_OSD_BMPWINMD_TE_ENABLE);

	regval->vid01 = 0;
	if (fbinfo->var.vmode & FB_VMODE_INTERLACED) {
		regval->vid01 |= DM270_VENC_VID01_SCMD_INTERLACE;
	} else if (fbinfo->var.vmode & FB_VMODE_NONINTERLACED) {
		regval->vid01 |= DM270_VENC_VID01_SCMD_NONINTERLACE;
	}

	regval->vid02 = 0;
	if (fbinfo->var.sync & FB_SYNC_COMP_HIGH_ACT) {
		regval->vid02 |= DM270_VENC_VID02_VSSW_CSYNC;
	}

	if (fbinfo->var.vmode & FB_VMODE_DOUBLE) {
		regval->bmpwinyl = fbinfo->var.yres;
		regval->bmpwinyp = fbinfo->var.yoffset;
		regval->bmpwinmd |= DM270_OSD_BMPWINMD_OFF_FIELD;
	} else {
		regval->bmpwinyl = (fbinfo->var.yres >> 1);
		regval->bmpwinyp = (fbinfo->var.yoffset >> 1);
		regval->bmpwinmd |= DM270_OSD_BMPWINMD_OFF_FRAME;
	}

	switch (fbinfo->var.bits_per_pixel) {
	case 1: regval->bmpwinmd |= DM270_OSD_BMPWINMD_BMW_1BPP; break;
	case 2: regval->bmpwinmd |= DM270_OSD_BMPWINMD_BMW_2BPP; break;
	case 4: regval->bmpwinmd |= DM270_OSD_BMPWINMD_BMW_4BPP; break;
	case 8: regval->bmpwinmd |= DM270_OSD_BMPWINMD_BMW_8BPP; break;
	default:
		WPRINTK("depth %u bpp not supported???\n",
				fbinfo->var.bits_per_pixel);
		return -EINVAL;
	}

	return 0;
}

/*
 * dm270fb_map_graphics_memory - Allocates DRAM memory for frame buffer.
 * @info: frame buffer structure that represents a single frame buffer
 *
 * This memory is remapped into a non-cached, non-buffered, memory region
 * to allow pixel writes to occur without flushing the cache.  Once this
 * area is remapped, all virtual memory access to the graphics memory
 * should occur at the new region.
 */
static int __init
dm270fb_map_graphics_memory(struct fb_info *fbinfo)
{
	unsigned long adrs;
	unsigned long size;

	/*
	 * We reserve size of the framebuffer.
	 */
	size = PAGE_ALIGN(fbinfo->fix.smem_len);
	if (0 == size) {
		WPRINTK("size=%u\n", fbinfo->fix.smem_len);
		return -EINVAL;
	}
	if (size > (PAGE_SIZE << MAX_ORDER)) {
		WPRINTK("size %u exceed %lu\n", fbinfo->fix.smem_len,
				(PAGE_SIZE << MAX_ORDER));
		return -EINVAL;
	}
	fbinfo->screen_base = (void *)__get_free_pages(GFP_KERNEL,
			get_order(size));
	if (NULL == fbinfo->screen_base) {
		WPRINTK("alloc failed: virt=0x%08x size=%u "
				"PAGESIZE=%lu MAX_ORDER=%u\n",
				(unsigned int)fbinfo->screen_base, fbinfo->fix.smem_len,
				PAGE_SIZE, MAX_ORDER);
		return -ENOMEM;
	}
	fbinfo->fix.smem_len = size;
	adrs = (unsigned long)fbinfo->screen_base;
	while (size > 0) {
		SetPageReserved(virt_to_page(adrs));
		adrs += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	fbinfo->fix.smem_start = virt_to_phys(fbinfo->screen_base);
	DPRINTK("phys=0x%016lx virt=0x%08x len=%u\n", fbinfo->fix.smem_start,
			(unsigned int)fbinfo->screen_base,
			fbinfo->fix.smem_len);
	memset(fbinfo->screen_base, 0, fbinfo->fix.smem_len);
	return 0;
}

/*
 * dm270fb_unmap_graphics_memory - Frees DRAM memory of frame buffer.
 * @info: frame buffer structure that represents a single frame buffer
 */
static void
dm270fb_unmap_graphics_memory(struct fb_info *fbinfo)
{
	unsigned long adrs;
	unsigned long size;

	DPRINTK("phys=0x%016lx virt=0x%08x len=%u\n",
			fbinfo->fix.smem_start,
			(unsigned int)fbinfo->screen_base,
			fbinfo->fix.smem_len);

	if (fbinfo->screen_base) {
		size = fbinfo->fix.smem_len;
		adrs = (unsigned long)fbinfo->screen_base;
		while (size > 0) {
			ClearPageReserved(virt_to_page(adrs));
			adrs += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
		free_pages((unsigned long)fbinfo->screen_base, get_order(fbinfo->fix.smem_len));
		fbinfo->fix.smem_len = 0;
		fbinfo->fix.smem_start = 0;
		fbinfo->screen_base = NULL;
	}
}

/*
 * ===========================================================================
 */

/*
 * dm270fb_check_var - Optional function. Validates a var passed in.
 * @var: frame buffer variable screen structure
 * @info: frame buffer structure that represents a single frame buffer
 *
 * Checks to see if the hardware supports the state requested by
 * var passed in. This function does not alter the hardware state!!!
 * This means the data stored in struct fb_info and struct dm270fb_par do
 * not change. This includes the var inside of struct fb_info.
 * Do NOT change these. This function can be called on its own if we
 * intent to only test a mode and not actually set it. The stuff in
 * modedb.c is a example of this. If the var passed in is slightly
 * off by what the hardware can support then we alter the var PASSED in
 * to what we can do. If the hardware doesn't support mode change
 * a -EINVAL will be returned by the upper layers. You don't need to
 * implement this function then. If you hardware doesn't support
 * changing the resolution then this function is not needed. In this
 * case the driver woudl just provide a var that represents the static
 * state the screen is in.
 *
 * If a value doesn't fit, round it up, if it's too big, return -EINVAL.
 *
 * Suggestion: Round up in the following order: bits_per_pixel, xres,
 * yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
 * bitfields, horizontal timing, vertical timing.
 *
 * Returns negative errno on error, or zero on success.
 */
static int
dm270fb_check_var(struct fb_var_screeninfo *fbvar, struct fb_info *fbinfo)
{
	switch (fbvar->bits_per_pixel) {
	case 1:
	case 2:
	case 4:
	case 8:
		/* for bpp <= 8, length of red = length of green = length of blue = bpp */
		fbvar->red.offset	= 0;
		fbvar->red.length	= fbvar->bits_per_pixel;
		fbvar->red.msb_right	= 0;

		fbvar->green.offset	= 0;
		fbvar->green.length	= fbvar->bits_per_pixel;
		fbvar->green.msb_right	= 0;

		fbvar->blue.offset	= 0;
		fbvar->blue.length	= fbvar->bits_per_pixel;
		fbvar->blue.msb_right	= 0;

		fbvar->transp.offset	= 0;
		fbvar->transp.length	= 0;
		fbvar->transp.msb_right	= 0;
		break;
	default:
		WPRINTK("unsupported depth: %u bpp\n", fbvar->bits_per_pixel);
		return -EINVAL;
	}

	if (fbvar->xres < DM270FB_XRES_MIN) {
		WPRINTK("width %u round up to %u\n",
				fbvar->xres, DM270FB_XRES_MIN);
		fbvar->xres = DM270FB_XRES_MIN;
	}
	if (fbvar->yres < DM270FB_YRES_MIN) {
		WPRINTK("height %u round up to %u\n",
				fbvar->yres, DM270FB_YRES_MIN);
		fbvar->yres = DM270FB_YRES_MIN;
	}
	if (fbvar->xres > DM270FB_XRES_MAX) {
		WPRINTK("width %u round down to %u\n",
				fbvar->xres, DM270FB_XRES_MAX);
		fbvar->xres = DM270FB_XRES_MAX;
	}
	if (fbvar->yres > DM270FB_YRES_MAX) {
		WPRINTK("height %u round down to %u\n",
				fbvar->yres, DM270FB_YRES_MAX);
		fbvar->yres = DM270FB_YRES_MAX;
	}

	if (fbvar->xres_virtual < fbvar->xres) {
		WPRINTK("virtual x resolution %u round up to "
				"physical x resolution %u\n",
				fbvar->xres_virtual, fbvar->xres);
		fbvar->xres_virtual = fbvar->xres;
	}
	if (fbvar->yres_virtual < fbvar->yres) {
		WPRINTK("virtual y resolution %u round up to "
				"physical y resolution %u\n",
				fbvar->yres_virtual, fbvar->yres);
		fbvar->yres_virtual = fbvar->yres;
	}

	if (((fbvar->xres_virtual * fbvar->yres_virtual * fbvar->bits_per_pixel)
			>> 3) > fbinfo->fix.smem_len) {
		WPRINTK("insufficient memory for virtual screen (%u, %u, %u)\n",
				fbvar->xres_virtual, fbvar->yres_virtual,
				fbvar->bits_per_pixel);
		return -ENOMEM;
	}

	fbvar->nonstd = 0;
	fbvar->height = -1;
	fbvar->width = -1;
	return 0;
}

/*
 * dm270fb_set_par - Optional function. Alters the hardware state.
 * @info: frame buffer structure that represents a single frame buffer
 *
 * Using the fb_var_screeninfo in fb_info we set the resolution of the
 * this particular framebuffer. This function alters the par AND the
 * fb_fix_screeninfo stored in fb_info. It doesn't alter var in
 * fb_info since we are using that data. This means we depend on the
 * data in var inside fb_info to be supported by the hardware.
 * dm270fb_check_var is always called before dm270fb_set_par to ensure this.
 * Again if you can't change the resolution you don't need this function.
 *
 * Configures OSD based on entries in var parameter.  Settings are
 * only written to the controller if changes were made.
 */
static int
dm270fb_set_par(struct fb_info *fbinfo)
{
	struct dm270fb_par *dm270fbpar = (struct dm270fb_par *)fbinfo->par;
	struct dm270fb_regval regval;
	int retval;

	DPRINTK("Configuring TI TMS320DM270 OSD\n");

	if ((retval = dm270fb_decode_var(fbinfo, &regval)))
		return retval;

	/*
	 * XXX
	 * Only DM270_VENC_VID01_SCMD & DM270_VENC_VID02_VSSW are tracked.
	 * DM270_OSD_BMPWINMD_OACT is not tracked.
	 */
	dm270fbpar->regval = regval;	/* struct copy */

	outw((inw(DM270_VENC_VID02) & ~(DM270_VENC_VID02_VSSW)) |
			regval.vid02, DM270_VENC_VID02);
	outw((inw(DM270_VENC_VID01) & ~(DM270_VENC_VID01_SCMD)) |
			regval.vid01, DM270_VENC_VID01);

	outw((inw(dm270fbpar->regaddr.bmpwinmd) & DM270_OSD_BMPWINMD_OACT) |
			regval.bmpwinmd, dm270fbpar->regaddr.bmpwinmd);
	outw(regval.bmpwinofst, dm270fbpar->regaddr.bmpwinofst);
	outw(regval.bmpwinxl, dm270fbpar->regaddr.bmpwinxl);
	outw(regval.bmpwinyl, dm270fbpar->regaddr.bmpwinyl);
	outw(regval.bmpwinxp, dm270fbpar->regaddr.bmpwinxp);
	outw(regval.bmpwinyp, dm270fbpar->regaddr.bmpwinyp);

	DPRINTK("VID01=0x%04x VID02=0x%04x\n",
			inw(DM270_VENC_VID01), inw(DM270_VENC_VID02));
	DPRINTK("BMPWINMD=0x%04x BMPWINOFST=0x%04x\n",
			inw(dm270fbpar->regaddr.bmpwinmd),
			inw(dm270fbpar->regaddr.bmpwinofst));
	DPRINTK("BMPWINXL=0x%04x BMPWINYL=0x%04x\n",
			inw(dm270fbpar->regaddr.bmpwinxl),
			inw(dm270fbpar->regaddr.bmpwinyl));
	DPRINTK("BMPWINXP=0x%04x BMPWINYP=0x%04x\n",
			inw(dm270fbpar->regaddr.bmpwinxp),
			inw(dm270fbpar->regaddr.bmpwinyp));
	return 0;
}

/*
 * dm270fb_setcolreg - Optional function. Sets a color register.
 * @regno: Which register in the CLUT we are programming
 * @red: The red value which can be up to 16 bits wide
 * @green: The green value which can be up to 16 bits wide
 * @blue:  The blue value which can be up to 16 bits wide
 * @transp: If supported the alpha value which can be up to 16 bits wide.
 * @info: frame buffer info structure
 * 
 * Set a single color register. The values supplied have a 16 bit
 * magnitude which needs to be scaled in this function for the hardware.
 * Things to take into consideration are how many color registers, if
 * any, are supported with the current color visual. With truecolor mode
 * no color palettes are supported. Here a psuedo palette is created
 * which we store the value in pseudo_palette in struct fb_info. For
 * pseudocolor mode we have a limited color palette. To deal with this
 * we can program what color is displayed for a particular pixel value.
 * DirectColor is similar in that we can program each color field. If
 * we have a static colormap we don't need to implement this function.
 * 
 * Returns negative errno on error, or zero on success.
 */
static int
dm270fb_setcolreg(unsigned int  regno, unsigned int red, unsigned int green,
		unsigned int blue, unsigned int transp, struct fb_info *fbinfo)
{
	DPRINTK("regno=%u red=%u green=%u blue=%u transp=%u\n",
			regno, red, green, blue, transp);

	/*
	 * If grayscale is true, we convert the RGB value to
	 * grayscale regardless of what visual we are using.
	 */
	if (fbinfo->var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue =
			(19595 * red + 38470 * green + 7471 * blue) >> 16;
	}

	switch (fbinfo->fix.visual) {
	case FB_VISUAL_PSEUDOCOLOR:
		/*
		 * Pseudocolour:
		 *         8     8
		 * pixel --/--+--/-->  red lut  --> red dac
		 *            |  8
		 *            +--/--> green lut --> green dac
		 *            |  8
		 *            +--/-->  blue lut --> blue dac
		 */
		if (regno >= fbinfo->cmap.len || regno >= DM270FB_NR_PALETTE) {
			WPRINTK("regno %u exceed cmap length %u (max %u)\n",
					regno, fbinfo->cmap.len,
					DM270FB_NR_PALETTE);
			return -EINVAL;
		}

		return dm270fb_set_palettereg(regno, red, green, blue,
				transp, fbinfo);
	default:
		WPRINTK("invalid visual %u\n", fbinfo->fix.visual);
		return -EINVAL;
	}

	return 0;
}

/*
 * dm270fb_blank - NOT a required function. Blanks the display.
 * @blank: the blank mode we want. 
 * @info: frame buffer structure that represents a single frame buffer
 *
 * Blank the screen if blank != 0, else unblank. Return 0 if
 * blanking succeeded, != 0 if un-/blanking failed due to e.g. a 
 * video mode which doesn't support it. Implements VESA suspend
 * and powerdown modes on hardware that supports disabling hsync/vsync:
 * blank == 2: suspend vsync
 * blank == 3: suspend hsync
 * blank == 4: powerdown
 *
 * Returns negative errno on error, or zero on success.
 */
/*
 * Formal definition of the VESA spec:
 *  On
 *  	This refers to the state of the display when it is in full operation
 *  Stand-By
 *  	This defines an optional operating state of minimal power reduction with
 *  	the shortest recovery time
 *  Suspend
 *  	This refers to a level of power management in which substantial power
 *  	reduction is achieved by the display.  The display can have a longer 
 *  	recovery time from this state than from the Stand-by state
 *  Off
 *  	This indicates that the display is consuming the lowest level of power
 *  	and is non-operational. Recovery from this state may optionally require
 *  	the user to manually power on the monitor
 *
 *  Now, the fbdev driver adds an additional state, (blank), where they
 *  turn off the video (maybe by colormap tricks), but don't mess with the
 *  video itself: think of it semantically between on and Stand-By.
 *
 *  So here's what we should do in our fbdev blank routine:
 *
 *  	VESA_NO_BLANKING (mode 0)	Video on,  front/back light on
 *  	VESA_VSYNC_SUSPEND (mode 1)  	Video on,  front/back light off
 *  	VESA_HSYNC_SUSPEND (mode 2)  	Video on,  front/back light off
 *  	VESA_POWERDOWN (mode 3)		Video off, front/back light off
 *
 *  This will match the matrox implementation.
 */
static int
dm270fb_blank(int blank, struct fb_info *fbinfo)
{
	DPRINTK("blank=%d\n", blank);

	if (blank) {
		dm270fb_blank_display(fbinfo);
	} else {
		dm270fb_unblank_display(fbinfo);
		dm270fb_dac_powerup(fbinfo);
	}
	if (blank > 0) {
		switch (blank - 1) {
		case VESA_NO_BLANKING:
			dm270fb_display_powerup(fbinfo);
			break;
		case VESA_VSYNC_SUSPEND:
		case VESA_HSYNC_SUSPEND:
			dm270fb_dac_powerdown(fbinfo);
			break;
		case VESA_POWERDOWN:
			dm270fb_display_powerdown(fbinfo);
			break;
		default:
			WPRINTK("invalid VESA blanking level %d\n", blank);
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * dm270fb_pan_display - NOT a required function. Pans the display.
 * @var: frame buffer variable screen structure
 * @info: frame buffer structure that represents a single frame buffer
 *
 * Pan (or wrap, depending on the `vmode' field) the display using the
 * `xoffset' and `yoffset' fields of the `var' structure.
 * If the values don't fit, return -EINVAL.
 *
 * Returns negative errno on error, or zero on success.
 */
static int
dm270fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	return -EINVAL;
}

static int
dm270fb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		unsigned long arg, struct fb_info *fbinfo)
{
	int ii;
	int jj;
	int retval = 0;
    
	switch (cmd) {
#ifdef DM270FB_DEBUG
	case FBIOPUT_DM270_COLORIMG:
		DPRINTK("FBIOPUT_DM270_COLORIMG\n");
		for (ii = 0; ii < fbinfo->var.yres; ii++) {
			for (jj = 0; jj < fbinfo->var.xres; jj++) {
				*((unsigned char *)fbinfo->screen_base +
						ii*fbinfo->var.xres + jj) = arg;
			}
		}
		break;

	case FBCMD_DM270_PRINT_FBUF:
		DPRINTK("FBCMD_DM270_PRINT_FBUF\n");
		if (arg < 0 || arg >= fbinfo->var.yres) {
			WPRINTK("arg=%ld (%u)\n", arg, fbinfo->var.yres);
			return -EINVAL;
		}
		for (ii = 0; ii < fbinfo->var.xres; ii++) {
			unsigned char *fbaddr =
					(unsigned char *)fbinfo->screen_base +
					arg*fbinfo->var.xres + ii;
			if (!(ii % 16)) {
				printk("\n%p: %02x ", fbaddr, *fbaddr);
			} else {
				printk("%02x ", *fbaddr);
			}
		}
		printk("\n");
		break;

	case FBCMD_DM270_PRINT_REG:
		DPRINTK("FBCMD_DM270_PRINT_REG\n");

		DPRINTK("Clock Controller\n" );
		DPRINTK("================\n" );
		DPRINTK("PLLA        = 0x%08x\n", inw(DM270_CLKC_PLLA));
		DPRINTK("PLLB        = 0x%08x\n", inw(DM270_CLKC_PLLB));
		DPRINTK("CLKC        = 0x%08x\n", inw(DM270_CLKC_CLKC));
		DPRINTK("SEL         = 0x%08x\n", inw(DM270_CLKC_SEL));
		DPRINTK("DIV         = 0x%08x\n", inw(DM270_CLKC_DIV));
		DPRINTK("BYP         = 0x%08x\n", inw(DM270_CLKC_BYP));
		DPRINTK("MMCCLK      = 0x%08x\n", inw(DM270_CLKC_MMCCLK));
		DPRINTK("MOD0        = 0x%08x\n", inw(DM270_CLKC_MOD0));
		DPRINTK("MOD1        = 0x%08x\n", inw(DM270_CLKC_MOD1));
		DPRINTK("MOD2        = 0x%08x\n", inw(DM270_CLKC_MOD2));
		DPRINTK("LPCTL0      = 0x%08x\n", inw(DM270_CLKC_LPCTL0));
		DPRINTK("LPCTL1      = 0x%08x\n", inw(DM270_CLKC_LPCTL1));
		DPRINTK("OSEL        = 0x%08x\n", inw(DM270_CLKC_OSEL));
		DPRINTK("O0DIV       = 0x%08x\n", inw(DM270_CLKC_O0DIV));
		DPRINTK("O1DIV       = 0x%08x\n", inw(DM270_CLKC_O1DIV));
		DPRINTK("O2DIV       = 0x%08x\n", inw(DM270_CLKC_O2DIV));
		DPRINTK("PWM0C       = 0x%08x\n", inw(DM270_CLKC_PWM0C));
		DPRINTK("PWM0H       = 0x%08x\n", inw(DM270_CLKC_PWM0H));
		DPRINTK("PWM1C       = 0x%08x\n", inw(DM270_CLKC_PWM1C));
		DPRINTK("PWM1H       = 0x%08x\n", inw(DM270_CLKC_PWM1H));
		DPRINTK("\n");

		DPRINTK("OSD - On-Screen Display\n" );
		DPRINTK("=======================\n" );
		DPRINTK("OSDMODE     = 0x%04x\n", inw(DM270_OSD_OSDMODE));
		DPRINTK("VIDWINMD    = 0x%04x\n", inw(DM270_OSD_VIDWINMD));
		DPRINTK("BMPWIN0MD   = 0x%04x\n", inw(DM270_OSD_BMPWIN0MD));
		DPRINTK("ATRMD       = 0x%04x\n", inw(DM270_OSD_ATRMD));
		DPRINTK("RECTCUR     = 0x%04x\n", inw(DM270_OSD_RECTCUR));
		DPRINTK("VIDWIN0OFST = 0x%04x\n", inw(DM270_OSD_VIDWIN0OFST));
		DPRINTK("VIDWIN1OFST = 0x%04x\n", inw(DM270_OSD_VIDWIN1OFST));
		DPRINTK("BMPWIN0OFST = 0x%04x\n", inw(DM270_OSD_BMPWIN0OFST));
		DPRINTK("BMPWIN1OFST = 0x%04x\n", inw(DM270_OSD_BMPWIN1OFST));
		DPRINTK("VIDWINADH   = 0x%04x\n", inw(DM270_OSD_VIDWINADH));
		DPRINTK("VIDWIN0ADL  = 0x%04x\n", inw(DM270_OSD_VIDWIN0ADL));
		DPRINTK("VIDWIN1ADL  = 0x%04x\n", inw(DM270_OSD_VIDWIN1ADL));
		DPRINTK("BMPWINADH   = 0x%04x\n", inw(DM270_OSD_BMPWINADH));
		DPRINTK("BMPWIN0ADL  = 0x%04x\n", inw(DM270_OSD_BMPWIN0ADL));
		DPRINTK("BMPWIN1ADL  = 0x%04x\n", inw(DM270_OSD_BMPWIN1ADL));
		DPRINTK("BASEPX      = 0x%04x\n", inw(DM270_OSD_BASEPX));
		DPRINTK("BASEPY      = 0x%04x\n", inw(DM270_OSD_BASEPY));
		DPRINTK("VIDWIN0XP   = 0x%04x\n", inw(DM270_OSD_VIDWIN0XP));
		DPRINTK("VIDWIN0YP   = 0x%04x\n", inw(DM270_OSD_VIDWIN0YP));
		DPRINTK("VIDWIN0XL   = 0x%04x\n", inw(DM270_OSD_VIDWIN0XL));
		DPRINTK("VIDWIN0YL   = 0x%04x\n", inw(DM270_OSD_VIDWIN0YL));
		DPRINTK("VIDWIN1XP   = 0x%04x\n", inw(DM270_OSD_VIDWIN1XP));
		DPRINTK("VIDWIN1YP   = 0x%04x\n", inw(DM270_OSD_VIDWIN1YP));
		DPRINTK("VIDWIN1XL   = 0x%04x\n", inw(DM270_OSD_VIDWIN1XL));
		DPRINTK("VIDWIN1YL   = 0x%04x\n", inw(DM270_OSD_VIDWIN1YL));
		DPRINTK("BMPWIN0XP   = 0x%04x\n", inw(DM270_OSD_BMPWIN0XP));
		DPRINTK("BMPWIN0YP   = 0x%04x\n", inw(DM270_OSD_BMPWIN0YP));
		DPRINTK("BMPWIN0XL   = 0x%04x\n", inw(DM270_OSD_BMPWIN0XL));
		DPRINTK("BMPWIN0YL   = 0x%04x\n", inw(DM270_OSD_BMPWIN0YL));
		DPRINTK("BMPWIN1XP   = 0x%04x\n", inw(DM270_OSD_BMPWIN1XP));
		DPRINTK("BMPWIN1YP   = 0x%04x\n", inw(DM270_OSD_BMPWIN1YP));
		DPRINTK("BMPWIN1XL   = 0x%04x\n", inw(DM270_OSD_BMPWIN1XL));
		DPRINTK("BMPWIN1YL   = 0x%04x\n", inw(DM270_OSD_BMPWIN1YL));
		DPRINTK("CURXP       = 0x%04x\n", inw(DM270_OSD_CURXP));
		DPRINTK("CURYP       = 0x%04x\n", inw(DM270_OSD_CURYP));
		DPRINTK("CURXL       = 0x%04x\n", inw(DM270_OSD_CURXL));
		DPRINTK("CURYL       = 0x%04x\n", inw(DM270_OSD_CURYL));
		DPRINTK("W0BMP01     = 0x%04x\n", inw(DM270_OSD_W0BMP01));
		DPRINTK("W0BMP23     = 0x%04x\n", inw(DM270_OSD_W0BMP23));
		DPRINTK("W0BMP45     = 0x%04x\n", inw(DM270_OSD_W0BMP45));
		DPRINTK("W0BMP67     = 0x%04x\n", inw(DM270_OSD_W0BMP67));
		DPRINTK("W0BMP89     = 0x%04x\n", inw(DM270_OSD_W0BMP89));
		DPRINTK("W0BMPAB     = 0x%04x\n", inw(DM270_OSD_W0BMPAB));
		DPRINTK("W0BMPCD     = 0x%04x\n", inw(DM270_OSD_W0BMPCD));
		DPRINTK("W0BMPEF     = 0x%04x\n", inw(DM270_OSD_W0BMPEF));
		DPRINTK("W1BMP01     = 0x%04x\n", inw(DM270_OSD_W1BMP01));
		DPRINTK("W1BMP23     = 0x%04x\n", inw(DM270_OSD_W1BMP23));
		DPRINTK("W1BMP45     = 0x%04x\n", inw(DM270_OSD_W1BMP45));
		DPRINTK("W1BMP67     = 0x%04x\n", inw(DM270_OSD_W1BMP67));
		DPRINTK("W1BMP89     = 0x%04x\n", inw(DM270_OSD_W1BMP89));
		DPRINTK("W1BMPAB     = 0x%04x\n", inw(DM270_OSD_W1BMPAB));
		DPRINTK("W1BMPCD     = 0x%04x\n", inw(DM270_OSD_W1BMPCD));
		DPRINTK("W1BMPEF     = 0x%04x\n", inw(DM270_OSD_W1BMPEF));
		DPRINTK("MISCCTL     = 0x%04x\n", inw(DM270_OSD_MISCCTL));
		DPRINTK("CLUTRAMYCB  = 0x%04x\n", inw(DM270_OSD_CLUTRAMYCB));
		DPRINTK("CLUTRAMCR   = 0x%04x\n", inw(DM270_OSD_CLUTRAMCR));
		DPRINTK("PPVWIN0ADH  = 0x%04x\n", inw(DM270_OSD_PPVWIN0ADH));
		DPRINTK("PPVWIN0ADL  = 0x%04x\n", inw(DM270_OSD_PPVWIN0ADL));

		DPRINTK("Video Encoder\n" );
		DPRINTK("=============\n" );
		DPRINTK("VID01       = 0x%04x\n", inw(DM270_VENC_VID01));
		DPRINTK("VID02       = 0x%04x\n", inw(DM270_VENC_VID02));
		DPRINTK("DLCD1       = 0x%04x\n", inw(DM270_VENC_DLCD1));
		DPRINTK("DLCD2       = 0x%04x\n", inw(DM270_VENC_DLCD2));
		DPRINTK("DCLKPTN0E   = 0x%04x\n", inw(DM270_VENC_DCLKPTN0E));
		DPRINTK("DCLKPTN1E   = 0x%04x\n", inw(DM270_VENC_DCLKPTN1E));
		DPRINTK("DCLKPTN2E   = 0x%04x\n", inw(DM270_VENC_DCLKPTN2E));
		DPRINTK("DCLKPTN3E   = 0x%04x\n", inw(DM270_VENC_DCLKPTN3E));
		DPRINTK("DCLKPTN0O   = 0x%04x\n", inw(DM270_VENC_DCLKPTN0O));
		DPRINTK("DCLKPTN1O   = 0x%04x\n", inw(DM270_VENC_DCLKPTN1O));
		DPRINTK("DCLKPTN2O   = 0x%04x\n", inw(DM270_VENC_DCLKPTN2O));
		DPRINTK("DCLKPTN3O   = 0x%04x\n", inw(DM270_VENC_DCLKPTN3O));
		DPRINTK("DCLKSTPHE   = 0x%04x\n", inw(DM270_VENC_DCLKSTPHE));
		DPRINTK("DCLKSTPHO   = 0x%04x\n", inw(DM270_VENC_DCLKSTPHO));
		DPRINTK("DCLKVLDH    = 0x%04x\n", inw(DM270_VENC_DCLKVLDH));
		DPRINTK("DCLKSTPV    = 0x%04x\n", inw(DM270_VENC_DCLKSTPV));
		DPRINTK("DCLKVLDV    = 0x%04x\n", inw(DM270_VENC_DCLKVLDV));
		DPRINTK("HVPWIDTH    = 0x%04x\n", inw(DM270_VENC_HVPWIDTH));
		DPRINTK("HINTERVL    = 0x%04x\n", inw(DM270_VENC_HINTERVL));
		DPRINTK("HSTART      = 0x%04x\n", inw(DM270_VENC_HSTART));
		DPRINTK("HVALID      = 0x%04x\n", inw(DM270_VENC_HVALID));
		DPRINTK("VINTERVL    = 0x%04x\n", inw(DM270_VENC_VINTERVL));
		DPRINTK("VSTART      = 0x%04x\n", inw(DM270_VENC_VSTART));
		DPRINTK("VVALID      = 0x%04x\n", inw(DM270_VENC_VVALID));
		DPRINTK("HDELAY      = 0x%04x\n", inw(DM270_VENC_HDELAY));
		DPRINTK("VDELAY      = 0x%04x\n", inw(DM270_VENC_VDELAY));
		DPRINTK("CULLLINE    = 0x%04x\n", inw(DM270_VENC_CULLLINE));
		DPRINTK("PWMCTRL     = 0x%04x\n", inw(DM270_VENC_PWMCTRL));
		DPRINTK("PWMHPRD     = 0x%04x\n", inw(DM270_VENC_PWMHPRD));
		DPRINTK("RGBLEVEL    = 0x%04x\n", inw(DM270_VENC_RGBLEVEL));
		DPRINTK("ATR0        = 0x%04x\n", inw(DM270_VENC_ATR0));
		DPRINTK("ATR1        = 0x%04x\n", inw(DM270_VENC_ATR1));
		DPRINTK("ATR2        = 0x%04x\n", inw(DM270_VENC_ATR2));
		DPRINTK("REC656      = 0x%04x\n", inw(DM270_VENC_REC656));
		DPRINTK("EPSON_LCD   = 0x%04x\n", inw(DM270_VENC_EPSON_LCD));
		DPRINTK("GCPDATA     = 0x%04x\n", inw(DM270_VENC_GCPDATA));
		DPRINTK("CASIO       = 0x%04x\n", inw(DM270_VENC_CASIO));
		DPRINTK("DOUTCTL     = 0x%04x\n", inw(DM270_VENC_DOUTCTL));
		break;
#endif

	default:
		WPRINTK("cmd=0x%08x\n", cmd);
		return -EINVAL;
	}

	return retval;
}

static struct fb_ops dm270fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= dm270fb_check_var,
	.fb_set_par	= dm270fb_set_par,
	.fb_setcolreg	= dm270fb_setcolreg,
	.fb_blank	= dm270fb_blank,
	.fb_pan_display	= dm270fb_pan_display,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
	.fb_ioctl	= dm270fb_ioctl,
};

/*
 * ===========================================================================
 */

#if 0
/*
 * These parameters give
 * 640x480, hsync 31.5kHz, vsync 60Hz
 */
static struct fb_videomode __devinitdata dm270fb_default_mode = {
	.refresh	= 60,
	.xres		= 640,
	.yres		= 480,
	.pixclock	= 39722,
	.left_margin	= 56,
	.right_margin	= 16,
	.upper_margin	= 34,
	.lower_margin	= 9,
	.hsync_len	= 88,
	.vsync_len	= 2,
	.sync		= FB_SYNC_COMP_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode		= FB_VMODE_NONINTERLACED
};
#endif

/*
 * Parse dm270fb options.
 * Usage: video=dm270:<options>
 */
static int __init
dm270fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options) {
		DPRINTK("options=%p\n", options);
		return 0;
	}

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "noaccel", 7)) {
			dm270fb_bootcfg.noaccel = 1;
		} else if (!strncmp(this_opt, "nopan", 5)) {
			dm270fb_bootcfg.nopan = 1;
		} else if (!strncmp(this_opt, "nowrap", 6)) {
			dm270fb_bootcfg.nowrap = 1;
		} else if (!strncmp(this_opt, "nohwcursor", 10)) {
			dm270fb_bootcfg.nohwcursor = 1;
		} else if (!strncmp(this_opt, "noinit", 6)) {
			dm270fb_bootcfg.noinit = 1;
		} else if (!strncmp(this_opt, "romclut", 7)) {
			dm270fb_bootcfg.cmap_static = 1;
		} else if (!strncmp(this_opt, "vsync", 5)) {
			dm270fb_bootvar.sync &= ~FB_SYNC_COMP_HIGH_ACT;
		} else if (!strncmp(this_opt, "grayscale", 9)) {
			dm270fb_bootvar.grayscale = 1;
		} else if (!strncmp(this_opt, "doublescan", 10)) {
			dm270fb_bootvar.vmode |= FB_VMODE_DOUBLE;
		} else if (!strncmp(this_opt, "font:", 5)) {
			strncpy(dm270fb_bootcfg.fontname, this_opt+5,
				sizeof(dm270fb_bootcfg.fontname)-1);
		} else if (!strncmp(this_opt, "display:", 8)) {
			if (!strncmp(this_opt+8, "comp", 4))
				dm270fb_bootcfg.disp_type = DISP_TYPE_COMP;
			else if (!strncmp(this_opt+8, "lcd", 3))
				dm270fb_bootcfg.disp_type = DISP_TYPE_LCD;
			else if (!strncmp(this_opt+8, "tft", 3))
				dm270fb_bootcfg.disp_type = DISP_TYPE_TFT;
			else if (!strncmp(this_opt+8, "crt", 3))
				dm270fb_bootcfg.disp_type = DISP_TYPE_CRT;
			else if (!strncmp(this_opt+8, "epson", 5))
				dm270fb_bootcfg.disp_type = DISP_TYPE_EPSON;
			else if (!strncmp(this_opt+8, "casio", 5))
				dm270fb_bootcfg.disp_type = DISP_TYPE_CASIO;
		} else if (!strncmp(this_opt, "vidfmt:", 7)) {
			if (!strncmp(this_opt+7, "ntsc", 4))
				dm270fb_bootcfg.vidout_std = VID_FMT_NTSC;
			else if (!strncmp(this_opt+7, "pal", 3))
				dm270fb_bootcfg.vidout_std = VID_FMT_PAL;
		} else if (!strncmp(this_opt, "vidscan:", 8)) {
			if (!strncmp(this_opt+7, "interlace", 9))
				dm270fb_bootvar.vmode |= FB_VMODE_INTERLACED;
			else if (!strncmp(this_opt+7, "noninterlace", 12))
				dm270fb_bootvar.vmode &= ~FB_VMODE_INTERLACED;
		} else if (!strncmp(this_opt, "width:", 6)) {
			dm270fb_bootvar.xres = simple_strtoul(this_opt+6, NULL, 0);
		} else if (!strncmp(this_opt, "height:", 7)) {
			dm270fb_bootvar.yres = simple_strtoul(this_opt+7, NULL, 0);
		} else if (!strncmp(this_opt, "bpp:", 4)) {
			dm270fb_bootvar.bits_per_pixel = simple_strtoul(this_opt+4, NULL, 0);
		} else if (!strncmp(this_opt, "hswidth:", 8)) {
			dm270fb_bootvar.hsync_len = simple_strtoul(this_opt+8,
							      NULL, 0);
		} else if (!strncmp(this_opt, "vswidth:", 8)) {
			dm270fb_bootvar.vsync_len = simple_strtoul(this_opt+8,
							      NULL, 0);
		} else {
			dm270fb_bootcfg.mode_option = this_opt;
		}
	}

	return 0;
}

static int __init
dm270fb_init_fbinfo(struct fb_info *fbinfo, char *name)
{
	struct dm270fb_par *dm270fbpar;
	int maxlen;
	int retval = 0;

	if (!fbinfo) {
		WPRINTK("NULL\n");
		return -EINVAL;
	}

	dm270fbpar = kmalloc(sizeof(struct dm270fb_par), GFP_KERNEL);
	if (!dm270fbpar) {
		WPRINTK("par alloc failed\n");
		return -ENOMEM;
	}

	memset(dm270fbpar, 0, sizeof(struct dm270fb_par));
	memset(fbinfo, 0, sizeof(struct fb_info));
	
	/* copy boot options */
	dm270fbpar->cfg = dm270fb_bootcfg;	/* struct copy */
	fbinfo->var = dm270fb_bootvar;		/* struct copy */

	maxlen = sizeof(fbinfo->fix.id) - 1;
	strncpy(fbinfo->fix.id, name, maxlen);	/* max length 15 */
	fbinfo->fix.id[maxlen]	= 0;
	fbinfo->fix.type	= FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.type_aux	= 0;
	fbinfo->fix.visual	= (dm270fbpar->cfg.cmap_static) ?
			FB_VISUAL_STATIC_PSEUDOCOLOR : FB_VISUAL_PSEUDOCOLOR;
	fbinfo->fix.xpanstep	= (dm270fbpar->cfg.nopan) ? 0 : 1;
	fbinfo->fix.ypanstep	= (dm270fbpar->cfg.nopan) ? 0 : 1;
	fbinfo->fix.ywrapstep	= (dm270fbpar->cfg.nowrap) ? 0 : 1;
	fbinfo->fix.line_length	= dm270fb_calc_linelength(fbinfo);
	if (dm270fbpar->cfg.noaccel) {
		fbinfo->fix.accel	= FB_ACCEL_NONE;
	} else {
		fbinfo->fix.accel	= FB_ACCEL_DM270;
	}

	/* initialize frame-buffer */
	fbinfo->fix.smem_len = (DM270FB_XRES_MAX * DM270FB_YRES_MAX *
			DM270FB_BPP_MAX) >> 3;	/* frame-buffer size */

	if (!dm270fbpar->cfg.noinit) {
		if ((retval = dm270fb_map_graphics_memory(fbinfo))) {
			WPRINTK("frame buffer alloc failed\n");
			retval = -ENOMEM;
			goto ret_free_par;
		}
	}

	/* initialize register pointers */
	dm270fbpar->regaddr.bmpwinmd	= DM270_OSD_BMPWIN0MD;
	dm270fbpar->regaddr.bmpwinofst	= DM270_OSD_BMPWIN0OFST;
	dm270fbpar->regaddr.bmpwinadl	= DM270_OSD_BMPWIN0ADL;
	dm270fbpar->regaddr.bmpwinxp	= DM270_OSD_BMPWIN0XP;
	dm270fbpar->regaddr.bmpwinyp	= DM270_OSD_BMPWIN0YP;
	dm270fbpar->regaddr.bmpwinxl	= DM270_OSD_BMPWIN0XL;
	dm270fbpar->regaddr.bmpwinyl	= DM270_OSD_BMPWIN0YL;
	dm270fbpar->regaddr.wbmp	= DM270_OSD_W0BMP01;

	/* enable clock to VENC & OSD */
	fbinfo->par = dm270fbpar;
	dm270fb_init_hw(fbinfo);

	if (!dm270fbpar->cfg.noinit) {
		/* disable video encoder while initializing */
		dm270fb_display_disable(fbinfo);
	}

#if 0
	if (!dm270fbpar->cfg.nohwcursor)
		dm270fb_hwcursor_init(fbinfo);
#endif

	fbinfo->node	= -1;
	if (dm270fbpar->cfg.noaccel) {
		fbinfo->flags = FBINFO_DEFAULT;
	} else {
		fbinfo->flags = (FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN);
	}
	fbinfo->fbops	= &dm270fb_ops;
	fbinfo->currcon = -1;

	/*
	 * If mode_option wasn't given at boot, assume all the boot
	 * option timing parameters were specified individually, in
	 * which case we do not need to call fb_find_mode as it has
	 * already been copied from the boot options above.
	 */
#if 0
	if (dm270fbpar->cfg.mode_option) {
		struct fb_videomode* modedb, *defmode;
		int dbsize = dm270fb_get_mode(fbinfo, DM270FB_DEFAULT_XRES, DM270FB_DEFAULT_YRES, &modedb, &defmode);

		/* first try the generic modedb */
		if (!fb_find_mode(&fbinfo->var, fbinfo, dm270fbpar->cfg.mode_option,
				  NULL, 0, NULL, fbinfo->var.bits_per_pixel)) {
			WPRINTK("mode %s failed, trying dm270 modedb\n",
			       dm270fbpar->cfg.mode_option);
			/* didn't work in generic modedb, try ours */
			if (!fb_find_mode(&fbinfo->var, fbinfo,
					  dm270fbpar->cfg.mode_option, modedb, dbsize,
					  defmode, fbinfo->var.bits_per_pixel)) {
				WPRINTK("mode %s failed dm270 modedb too, sorry\n",
				       dm270fbpar->cfg.mode_option);
				retval = -ENXIO;
				goto ret_unmap_fbuf;
			}
		}

		fbinfo->var.xres_virtual = dm270fb_bootvar.xres_virtual ?
				dm270fb_bootvar.xres_virtual : fbinfo->var.xres;
		fbinfo->var.yres_virtual = dm270fb_bootvar.yres_virtual ?
				dm270fb_bootvar.yres_virtual : fbinfo->var.yres;
	}
#endif

	if ((retval = fb_alloc_cmap(&fbinfo->cmap, DM270FB_NR_PALETTE, 0))) {
		WPRINTK("error %d allocating cmap\n", retval);
		goto ret_unmap_fbuf;
	}

	if (fbinfo->fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR) {
		fb_copy_cmap(&dm270fb_romclut_cmap, &fbinfo->cmap);
	}

	return 0;

ret_unmap_fbuf:
	if (!dm270fbpar->cfg.noinit) {
		dm270fb_unmap_graphics_memory(fbinfo);
	}
	fbinfo->par = NULL;

ret_free_par:
	kfree(dm270fbpar);
	return retval;
}

static int __init
dm270fb_init(void)
{
	struct dm270fb_par *dm270fbpar;
	int retval = -1;

#ifndef MODULE
	if (fb_get_options("dm270fb", &dm270fb_options))
		return -ENODEV;
#endif
	dm270fb_setup(dm270fb_options);

	if ((retval = dm270fb_init_fbinfo(&dm270fbinfo[0], DM270FB_NAME))) {
		printk(KERN_ERR "dm270fb: error %d initializing framebuffer\n",
				retval);
		goto ret_failed;
	}

	dm270fbpar = (struct dm270fb_par *)dm270fbinfo[0].par;
	if (!dm270fbpar->cfg.noinit) {
		if ((retval = dm270fb_set_par(&dm270fbinfo[0]))) {
			printk(KERN_ERR "dm270fb: error %d initializing hardware\n",
					retval);
			goto ret_free_resource;
		}
		dm270fb_display_enable(&dm270fbinfo[0]);
	}

	if ((retval = register_framebuffer(&dm270fbinfo[0])) < 0) {
		printk(KERN_ERR "dm270fb: error %d registering framebuffer\n",
				retval);
		goto ret_free_resource;
	}

	printk("fb%d: %s frame buffer device\n",
			dm270fbinfo[0].node, dm270fbinfo[0].fix.id);
	return 0;

ret_free_resource:
	if (!dm270fbpar->cfg.noinit) {
		dm270fb_unmap_graphics_memory(&dm270fbinfo[0]);
	}
	if (dm270fbinfo[0].par) {
		kfree(dm270fbinfo[0].par);
		dm270fbinfo[0].par = NULL;
	}

ret_failed:
	return retval;
}

static void __exit
dm270fb_exit(void)
{
	struct dm270fb_par *dm270fbpar = (struct dm270fb_par *)dm270fbinfo[0].par;
	int retval;

	if ((retval = unregister_framebuffer(&dm270fbinfo[0]))) {
		WPRINTK("error %d unregistering framebuffer\n", retval);
	}
	fb_dealloc_cmap(&dm270fbinfo[0].cmap);
	if (!dm270fbpar->cfg.noinit) {
		dm270fb_unmap_graphics_memory(&dm270fbinfo[0]);
	}
	if (dm270fbinfo[0].par) {
		kfree(dm270fbinfo[0].par);
		dm270fbinfo[0].par = NULL;
	}
}

module_init(dm270fb_init);
module_exit(dm270fb_exit);

MODULE_AUTHOR("Chee Tim Loh <lohct@pacific.net.sg>");
MODULE_DESCRIPTION("TI TMS320DM270 on-chip OSD framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_PARM(dm270fb_options, "s");
MODULE_PARM_DESC(dm270fb_options, "Options to pass to dm270fb");

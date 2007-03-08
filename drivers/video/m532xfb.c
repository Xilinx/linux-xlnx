/*
 *  linux/drivers/video/m532xfb.c -- Coldfire MCF5329 frame buffer driver
 *
 *  Copyright (c) 2006, emlix, Thomas Brinker <tb@emlix.com>
 *  Yaroslav Vinogradov yaroslav.vinogradov@freescale.com
 *  Copyright Freescale Semiconductor, Inc 2006
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Modified for Coldfire M5329 frame buffer by yaroslav.vinogradov@freescale.com
 *  Modified to new api Jan 2001 by James Simmons (jsimmons@transvirtual.com)
 *  Created 28 Dec 1997 by Geert Uytterhoeven
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/mcfsim.h>

#define FB_HWAIT1 0
#define FB_HWIDTH 1
#define FB_HWAIT2 2
#define FB_VWAIT1 3
#define FB_VWIDTH 4
#define FB_VWAIT2 5

static u32  fb_wait_params[][6]  = {
/* HWAIT1, HWIDTH, HWAIT2, VWAIT1, VWIDTH, VWAIT2  */
/* 640x480 */ {48,  14, 102, 32,  1, 35},
/* 800x600 */ {110, 59, 85,  42,  3, 24},
/* 240x320 */ {85,  10, 75,  32, 10, 10}
};

#if defined(CONFIG_LCD_640x480)
#define MODE_OPTION "640x480@60"
#define MODE_BPP 32
#define MODE_WIDTH 640
#define MODE_HEIGHT 480
#define MODE_VPW MODE_WIDTH
#define FB_WAIT_PARAMS(p) (fb_wait_params[0][(p)])
#define PIX_CLK_DIV 12
#define LCDC_LDCR_VALUE (MCF_LCDC_LDCR_TM(8) | MCF_LCDC_LDCR_HM(4))

#elif defined(CONFIG_LCD_800x600)
#define MODE_OPTION "800x600@60"
#define MODE_BPP 32 /* Default is 32 bits, 16 bits mode is also aviable */
#define MODE_WIDTH 800
#define MODE_HEIGHT 600
#define MODE_VPW (MODE_WIDTH >> (MODE_BPP == 16 ? 1 : 0))
#define FB_WAIT_PARAMS(p) (fb_wait_params[1][(p)])
#define PIX_CLK_DIV 3
#define LCDC_LDCR_VALUE (MCF_LCDC_LDCR_TM(8) | MCF_LCDC_LDCR_HM(4))

#elif defined(CONFIG_LCD_240x320)
#define MODE_OPTION "240x320@60"
#define MODE_BPP 32
#define MODE_WIDTH 240
#define MODE_HEIGHT 320
#define MODE_VPW (MODE_WIDTH >> (MODE_BPP == 16 ? 1 : 0))
#define FB_WAIT_PARAMS(p) (fb_wait_params[2][(p)])
#define PIX_CLK_DIV 12
#define LCDC_LDCR_VALUE (MCF_LCDC_LDCR_TM(4) | MCF_LCDC_LDCR_HM(8) | MCF_LCDC_LDCR_BURST)

#else
#error "LCD display resolution is not specified!"
#endif

extern int soft_cursor(struct fb_info* info, struct fb_cursor* cursor);

/*
 * This structure defines the hardware state of the graphics card. Normally
 * you place this in a header file in linux/include/video. This file usually
 * also includes register information. That allows other driver subsystems
 * and userland applications the ability to use the same header file to
 * avoid duplicate work and easy porting of software.
 */
struct m532x_par {
	char mode_option[40];
/* ... */
	unsigned dump;
};

/*
 * Here we define the default structs fb_fix_screeninfo and fb_var_screeninfo
 * if we don't use modedb. If we do use modedb see xxxfb_init how to use it
 * to get a fb_var_screeninfo. Otherwise define a default var as well.
 */
static struct fb_fix_screeninfo m532xfb_fix = {
	.id =		"M532x FB",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.xpanstep =	0,
	.ypanstep =	0,
	.ywrapstep =	0,
	.accel =	FB_ACCEL_NONE,
};

    /*
     * 	Modern graphical hardware not only supports pipelines but some
     *  also support multiple monitors where each display can have its
     *  its own unique data. In this case each display could be
     *  represented by a separate framebuffer device thus a separate
     *  struct fb_info. Now the struct xxx_par represents the graphics
     *  hardware state thus only one exist per card. In this case the
     *  struct xxx_par for each graphics card would be shared between
     *  every struct fb_info that represents a framebuffer on that card.
     *  This allows when one display changes it video resolution (info->var)
     *  the other displays know instantly. Each display can always be
     *  aware of the entire hardware state that affects it because they share
     *  the same xxx_par struct. The other side of the coin is multiple
     *  graphics cards that pass data around until it is finally displayed
     *  on one monitor. Such examples are the voodoo 1 cards and high end
     *  NUMA graphics servers. For this case we have a bunch of pars, each
     *  one that represents a graphics state, that belong to one struct
     *  fb_info. Their you would want to have *par point to a array of device
     *  states and have each struct fb_ops function deal with all those
     *  states. I hope this covers every possible hardware design. If not
     *  feel free to send your ideas at jsimmons@users.sf.net
     */

    /*
     *  If your driver supports multiple boards or it supports multiple
     *  framebuffers, you should make these arrays, or allocate them
     *  dynamically (using kmalloc()).
     */
static struct fb_info info;

    /*
     * Each one represents the state of the hardware. Most hardware have
     * just one hardware state. These here represent the default state(s).
     */

#define DUMP_OPTIONS 0x0 /* nothing */
//#define DUMP_OPTIONS 0xffffffff /* everything */

static struct m532x_par current_par = {
	.mode_option	= MODE_OPTION,
	.dump 			= DUMP_OPTIONS,
};

static u32 pseudo_palette[256];

int m532xfb_init(void);
int m532xfb_setup(char*);

/* ----- DUMP start ----- */

void m532xfb_dump_var(struct fb_info *info)
{
	printk("*** FB var: ***\n");
	printk("resolution: %d x %d\n", info->var.xres, info->var.yres);
	printk("virtual:    %d x %d\n", info->var.xres_virtual, info->var.yres_virtual);
	printk("offsets:    %d x %d\n", info->var.xoffset, info->var.yoffset);
	printk("bpp:        %d\n", info->var.bits_per_pixel);
	printk("grey:       %d\n", info->var.grayscale);

	printk("red:   off: %d len %d msb %d\n", info->var.red.offset, info->var.red.length, info->var.red.msb_right);
	printk("green: off: %d len %d msb %d\n", info->var.green.offset, info->var.green.length, info->var.green.msb_right);
	printk("blue:  off: %d len %d msb %d\n", info->var.blue.offset, info->var.blue.length, info->var.blue.msb_right);
	printk("transp:off: %d len %d msb %d\n", info->var.transp.offset, info->var.transp.length, info->var.transp.msb_right);

	printk("pixelformat:%d\n", info->var.nonstd);
	printk("activate:   %d\n", info->var.activate);
	printk("dimension:  %d x %d\n", info->var.height, info->var.width);

	printk("pixclock:   %lu\n", PICOS2KHZ(info->var.pixclock));
	printk("margins:    %d - %d - %d - %d\n", info->var.left_margin, info->var.right_margin, info->var.upper_margin, info->var.lower_margin);
	printk("synclen:    %d - %d\n", info->var.hsync_len, info->var.vsync_len);
	printk("sync:       %d\n", info->var.sync);
	printk("vmode:      %d\n", info->var.vmode);
	printk("rotate:     %d\n\n", info->var.rotate);
}

void m532xfb_dump_fix(struct fb_info *info)
{
	printk("*** FB fix: ***\n");
	printk("id          %s\n", info->fix.id);
	printk("smem_start  0x%08lx\n", info->fix.smem_start);
	printk("smem_len    %d\n", info->fix.smem_len);
	printk("type:       %d\n", info->fix.type);
	printk("type_aux:   %d\n", info->fix.type_aux);
	printk("visual:     %d\n", info->fix.visual);
	printk("xpanstep    %d\n", info->fix.xpanstep);
	printk("ypanstep    %d\n", info->fix.ypanstep);
	printk("ywrapstep   %d\n", info->fix.ywrapstep);
	printk("line_length %d\n", info->fix.line_length);
	printk("accel       %d\n\n", info->fix.accel);
}

void m532xfb_dump_par(struct fb_info *info)
{
	struct m532x_par *par = (struct m532x_par *) info->par;
	printk("*** FB par: ***\n");
	printk("dump:      %d\n\n", par->dump);
}

void m532xfb_dump_colors(void)
{
}

void m532xfb_dump_info(struct fb_info *info)
{
	int dump = ((struct m532x_par *) info->par)->dump;
	if (!dump) return;
	printk("-------------------------------------------------------------------\n");
	printk("*** FB info DUMP ***\n");
	printk("node:        %d\n", info->node);
	printk("flags:       %d\n\n", info->flags);
	printk("screenbase:  0x%p\n", info->screen_base);
	printk("screen_size: 0x%08lx\n", info->screen_size);
	printk("state:       %d\n\n", info->state);

	if (dump & 0x02)
		m532xfb_dump_fix(info);
	if (dump & 0x04)
		m532xfb_dump_var(info);
	if (dump & 0x08)
		m532xfb_dump_par(info);
	if (dump & 0x10)
		m532xfb_dump_colors();

    printk("*** LCD-Registers ***\n");
    printk("MCF_LCDC_LSSAR 0x%08lx\n",MCF_LCDC_LSSAR);
    printk("MCF_LCDC_LSR 0x%08lx\n",MCF_LCDC_LSR);
    printk("MCF_LCDC_LVPWR 0x%08lx\n",MCF_LCDC_LVPWR);
    printk("MCF_LCDC_LCPR 0x%08lx\n",MCF_LCDC_LCPR);
    printk("MCF_LCDC_LCWHBR 0x%08lx\n",MCF_LCDC_LCWHBR);
    printk("MCF_LCDC_LCCMR 0x%08lx\n",MCF_LCDC_LCCMR);
    printk("MCF_LCDC_LPCR 0x%08lx\n",MCF_LCDC_LPCR);
    printk("MCF_LCDC_LHCR 0x%08lx\n",MCF_LCDC_LHCR);
    printk("MCF_LCDC_LVCR 0x%08lx\n",MCF_LCDC_LVCR);
    printk("MCF_LCDC_LPOR 0x%08lx\n",MCF_LCDC_LPOR);
    printk("MCF_LCDC_LSCR 0x%08lx\n",MCF_LCDC_LSCR);
    printk("MCF_LCDC_LPCCR 0x%08lx\n",MCF_LCDC_LPCCR);
    printk("MCF_LCDC_LDCR 0x%08lx\n",MCF_LCDC_LDCR);
    printk("MCF_LCDC_LRMCR 0x%08lx\n",MCF_LCDC_LRMCR);
    printk("MCF_LCDC_LICR 0x%08lx\n",MCF_LCDC_LICR);
    printk("MCF_LCDC_LIER 0x%08lx\n",MCF_LCDC_LIER);
    printk("MCF_LCDC_LISR 0x%08lx\n",MCF_LCDC_LISR);
    printk("MCF_LCDC_LGWSAR 0x%08lx\n",MCF_LCDC_LGWSAR);
    printk("MCF_LCDC_LGWSR 0x%08lx\n",MCF_LCDC_LGWSR);
    printk("MCF_LCDC_LGWVPWR 0x%08lx\n",MCF_LCDC_LGWVPWR);
    printk("MCF_LCDC_LGWPOR 0x%08lx\n",MCF_LCDC_LGWPOR);
    printk("MCF_LCDC_LGWPR 0x%08lx\n",MCF_LCDC_LGWPR);
    printk("MCF_LCDC_LGWCR 0x%08lx\n",MCF_LCDC_LGWCR);
    printk("MCF_LCDC_LGWDCR 0x%08lx\n",MCF_LCDC_LGWDCR);
    printk("MCF_LCDC_BPLUT_BASE 0x%08lx\n",MCF_LCDC_BPLUT_BASE);
    printk("MCF_LCDC_GWLUT_BASE 0x%08lx\n",MCF_LCDC_GWLUT_BASE);
    printk("-------------------------------------------------------------------\n");
}

/* ----- DUMP end ----- */

/**
 *  	xxxfb_setcolreg - Optional function. Sets a color register.
 *      @regno: Which register in the CLUT we are programming
 *      @red: The red value which can be up to 16 bits wide
 *	@green: The green value which can be up to 16 bits wide
 *	@blue:  The blue value which can be up to 16 bits wide.
 *	@transp: If supported, the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 *
 *  	Set a single color register. The values supplied have a 16 bit
 *  	magnitude which needs to be scaled in this function for the hardware.
 *	Things to take into consideration are how many color registers, if
 *	any, are supported with the current color visual. With truecolor mode
 *	no color palettes are supported. Here a pseudo palette is created
 *	which we store the value in pseudo_palette in struct fb_info. For
 *	pseudocolor mode we have a limited color palette. To deal with this
 *	we can program what color is displayed for a particular pixel value.
 *	DirectColor is similar in that we can program each color field. If
 *	we have a static colormap we don't need to implement this function.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int m532xfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   const struct fb_info *info)
{
    if (regno >= 256)  /* no. of hw registers */
       return -EINVAL;
    /*
     * Program hardware... do anything you want with transp
     */

    /* grayscale works only partially under directcolor */
    if (info->var.grayscale) {
       /* grayscale = 0.30*R + 0.59*G + 0.11*B */
       red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
    }

    /* Directcolor:
     *   var->{color}.offset contains start of bitfield
     *   var->{color}.length contains length of bitfield
     *   {hardwarespecific} contains width of DAC
     *   cmap[X] is programmed to (X << red.offset) | (X << green.offset) | (X << blue.offset)
     *   RAMDAC[X] is programmed to (red, green, blue)
     *
     * Pseudocolor:
     *    uses offset = 0 && length = DAC register width.
     *    var->{color}.offset is 0
     *    var->{color}.length contains widht of DAC
     *    cmap is not used
     *    DAC[X] is programmed to (red, green, blue)
     * Truecolor:
     *    does not use RAMDAC (usually has 3 of them).
     *    var->{color}.offset contains start of bitfield
     *    var->{color}.length contains length of bitfield
     *    cmap is programmed to (red << red.offset) | (green << green.offset) |
     *                      (blue << blue.offset) | (transp << transp.offset)
     *    RAMDAC does not exist
     */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
    switch (info->fix.visual) {
       case FB_VISUAL_TRUECOLOR:
       case FB_VISUAL_PSEUDOCOLOR:
               red = CNVT_TOHW(red, info->var.red.length);
               green = CNVT_TOHW(green, info->var.green.length);
               blue = CNVT_TOHW(blue, info->var.blue.length);
               transp = CNVT_TOHW(transp, info->var.transp.length);
               break;
       case FB_VISUAL_DIRECTCOLOR:
	       /* example here assumes 8 bit DAC. Might be different
		* for your hardware */
               red = CNVT_TOHW(red, 8);
               green = CNVT_TOHW(green, 8);
               blue = CNVT_TOHW(blue, 8);
               /* hey, there is bug in transp handling... */
               transp = CNVT_TOHW(transp, 8);
               break;
    }
#undef CNVT_TOHW
    /* Truecolor has hardware independent palette */
    if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
       u32 v;

       if (regno >= 16)
           return -EINVAL;

       v = (red << info->var.red.offset) |
           (green << info->var.green.offset) |
           (blue << info->var.blue.offset) |
           (transp << info->var.transp.offset);

       switch (info->var.bits_per_pixel) {
		case 8:
			/* Yes some hand held devices have this. */
           		((u8*)(info->pseudo_palette))[regno] = v;
			break;
   		case 16:
           		((u16*)(info->pseudo_palette))[regno] = v;
			break;
		case 24:
		case 32:
           		((u32*)(info->pseudo_palette))[regno] = v;
			break;
       }
       return 0;
    }
    /* ... */
printk("do something with color palette!\n");
    return 0;
}
/* ------------------------------------------------------------------------- */

/*
 *  Frame buffer operations
 */

static struct fb_ops m532xfb_ops = {
	.owner		= THIS_MODULE,
	.fb_fillrect	= cfb_fillrect, 	/* Needed !!! */
	.fb_copyarea	= cfb_copyarea,		/* Needed !!! */
	.fb_imageblit	= cfb_imageblit,	/* Needed !!! */
	.fb_cursor	= soft_cursor,		/* Needed !!! */
};

/*
 *  Initialization
 */
int __init m532xfb_init(void)
{
	int cmap_len=256, retval;
	char* mode_option=NULL;

	/*
	*  For kernel boot options (in 'video=xxxfb:<options>' format)
	*/
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("m532xfb", &option)) {
		printk("No fb on command line specified\n");
		return -ENODEV;
	}
	m532xfb_setup(option);
#endif

	printk("Initing M532x Framebuffer\n");

	info.fbops = &m532xfb_ops;
	info.fix = m532xfb_fix;
	info.pseudo_palette = pseudo_palette;

	/*
	* Set up flags to indicate what sort of acceleration your
	* driver can provide (pan/wrap/copyarea/etc.) and whether it
	* is a module -- see FBINFO_* in include/linux/fb.h
	*/
	info.flags = FBINFO_DEFAULT + FBINFO_HWACCEL_DISABLED;
	info.par = &current_par;

	/*
	* This should give a reasonable default video mode. The following is
	* done when we can set a video mode.
	*/
	if (!mode_option)
		mode_option = MODE_OPTION;

	retval = fb_find_mode(&info.var, &info, mode_option, NULL, 0, NULL, MODE_BPP);

	if (!retval || retval == 4)
		return -EINVAL;

	info.screen_size = (info.var.xres * info.var.yres * info.var.bits_per_pixel) / 8;
	info.var.xres_virtual = info.var.xres;
	info.var.yres_virtual = info.var.yres;

#if MODE_BPP == 32
	info.var.red.offset = 18;
	info.var.red.length = 6;
	info.var.red.msb_right = 0;

	info.var.green.offset = 10;
	info.var.green.length = 6;
	info.var.green.msb_right = 0;

	info.var.blue.offset = 2;
	info.var.blue.length = 6;
	info.var.blue.msb_right = 0;

	info.var.transp.offset = 0;
	info.var.transp.length = 0;
	info.var.transp.msb_right = 0;
#else
	info.var.red.offset = 11;
	info.var.red.length = 5;

	info.var.green.offset = 5;
	info.var.green.length = 6;

	info.var.blue.offset = 0;
	info.var.blue.length = 5;
#endif

	/*
	* Here we set the screen_base to the virtual memory address
	* for the framebuffer. Usually we obtain the resource address
	* from the bus layer and then translate it to virtual memory
	* space via ioremap. Consult ioport.h.
	*/
	info.screen_base = (unsigned char *)__get_free_pages(GFP_KERNEL, get_order(info.screen_size));
	if (!info.screen_base) {
		printk("Unable to allocate %d PAGEs(%ld Bytes) fb memory\n",get_order(info.screen_size),info.screen_size);
		return -ENOMEM;
	}

	info.fix.smem_start = virt_to_phys((void *)info.screen_base);
	info.fix.smem_len = info.screen_size;
	info.fix.line_length = info.var.xres * info.var.bits_per_pixel / 8;

	/*
	* Set page reserved so that mmap will work. This is necessary
	* since we'll be remapping normal memory.
	*/
    {
		unsigned char * page;
		for (page = info.screen_base;
			(unsigned long)page < (PAGE_ALIGN((unsigned long)info.screen_base + info.screen_size));
			page += PAGE_SIZE)
		{
			SetPageReserved(virt_to_page(page));
		}
    }
	memset((void *)info.screen_base, 0, info.screen_size);

	/* set gpios */
	MCF_GPIO_PAR_LCDDATA = 0xff; /* switch all to display */
	MCF_GPIO_PAR_LCDCTL  = 0x1ff;

	/* burst mode */
	MCF_SCM_BCR = 0x3ff;

	MCF_LCDC_LSSAR     = (unsigned int)info.screen_base;
	MCF_LCDC_LSR       = MCF_LCDC_LSR_XMAX(MODE_WIDTH/16) | MCF_LCDC_LSR_YMAX(MODE_HEIGHT);
	MCF_LCDC_LVPWR     = MCF_LCDC_LVPWR_VPW(MODE_VPW);

#if defined(CONFIG_LCD_640x480)
	MCF_LCDC_LPCR      = MCF_LCDC_LPCR_TFT
			| MCF_LCDC_LPCR_COLOR
			| MCF_LCDC_LPCR_BPIX_18bpp
			| MCF_LCDC_LPCR_FLM
			| MCF_LCDC_LPCR_LPPOL
			| MCF_LCDC_LPCR_OEPOL
			| MCF_LCDC_LPCR_CLKPOL
			| MCF_LCDC_LPCR_SCLKSEL
			| MCF_LCDC_LPCR_ACDSEL
			| MCF_LCDC_LPCR_ENDSEL
			| MCF_LCDC_LPCR_PCD(PIX_CLK_DIV);
#elif defined(CONFIG_LCD_800x600)
	MCF_LCDC_LPCR      = MCF_LCDC_LPCR_MODE_TFT
#if MODE_BPP == 32
			| MCF_LCDC_LPCR_BPIX_18bpp
#else
			| MCF_LCDC_LPCR_BPIX_16bpp
#endif
			| MCF_LCDC_LPCR_FLM
			| MCF_LCDC_LPCR_LPPOL
			| MCF_LCDC_LPCR_CLKPOL
			| MCF_LCDC_LPCR_OEPOL
			| MCF_LCDC_LPCR_ACDSEL
			| MCF_LCDC_LPCR_SCLKSEL
			| MCF_LCDC_LPCR_ENDSEL
			| MCF_LCDC_LPCR_PCD(PIX_CLK_DIV);
#elif defined(CONFIG_LCD_240x320)
	MCF_LCDC_LPCR      = MCF_LCDC_LPCR_TFT
			  | MCF_LCDC_LPCR_COLOR
		      | MCF_LCDC_LPCR_BPIX_18bpp
		      | MCF_LCDC_LPCR_FLM
		      | MCF_LCDC_LPCR_LPPOL
			  | MCF_LCDC_LPCR_OEPOL
		      | MCF_LCDC_LPCR_CLKPOL
			  | MCF_LCDC_LPCR_SCLKSEL
			  | MCF_LCDC_LPCR_ACDSEL
		      | MCF_LCDC_LPCR_ENDSEL
		      | MCF_LCDC_LPCR_PCD(PIX_CLK_DIV);
#endif

	MCF_LCDC_LHCR = MCF_LCDC_LHCR_H_WIDTH(FB_WAIT_PARAMS(FB_HWIDTH))
			| MCF_LCDC_LHCR_H_WAIT_1(FB_WAIT_PARAMS(FB_HWAIT1))
			| MCF_LCDC_LHCR_H_WAIT_2(FB_WAIT_PARAMS(FB_HWAIT2));

	MCF_LCDC_LVCR = MCF_LCDC_LVCR_V_WIDTH(FB_WAIT_PARAMS(FB_VWIDTH))
			| MCF_LCDC_LVCR_V_WAIT_1(FB_WAIT_PARAMS(FB_VWAIT1))
			| MCF_LCDC_LVCR_V_WAIT_2(FB_WAIT_PARAMS(FB_VWAIT2));

	MCF_LCDC_LPOR      = MCF_LCDC_LPOR_POS(0);
	MCF_LCDC_LDCR      = LCDC_LDCR_VALUE;

	/* connect ldc controller to clock */
	MCF_CCM_MISCCR    |= MCF_CCM_MISCCR_LCD_CHEN;

	/* This has to been done !!! */
	fb_alloc_cmap(&info.cmap, cmap_len, 0);

	/*
	* The following is done in the case of having hardware with a static
	* mode. If we are setting the mode ourselves we don't call this.
	*/
    if (register_framebuffer(&info) < 0)
		return -EINVAL;
	printk(KERN_INFO "fb%d: %s frame buffer device\n", info.node, info.fix.id);

    m532xfb_dump_info(&info);

    return 0;
}

/*
*  Cleanup
*/
static void __exit m532xfb_cleanup(void)
{
	/*
	*  If your driver supports multiple boards, you should unregister and
	*  clean up all instances.
	*/
    unregister_framebuffer(&info);
    fb_dealloc_cmap(&info.cmap);
    /* ... */
}

/*
*  Setup
*/

/*
 * Only necessary if your driver takes special options,
 * otherwise we fall back on the generic fb_setup().
 */
int __init m532xfb_setup(char *options)
{
    /* Parse user speficied options (`video=xxxfb:') */
    return 0;
}
/* ------------------------------------------------------------------------- */
/*
*  Modularization
*/
module_init(m532xfb_init);
module_exit(m532xfb_cleanup);

MODULE_AUTHOR("Thomas Brinker <tb@emlix.com>");
MODULE_DESCRIPTION("MCF532x Framebuffer");
MODULE_LICENSE("GPL");

/*
 * Xylon logiCVC frame buffer driver core functions
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * This driver was based on skeletonfb.c and other framebuffer video drivers.
 * 2013 Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/console.h>
#include <linux/videodev2.h>
#include "xylonfb.h"
#if defined(CONFIG_FB_XYLON_MISC)
#include "../misc/xylonfb-misc.h"
#endif


#define XYLONFB_PSEUDO_PALETTE_SZ 256

#define LOGICVC_PIX_FMT_AYUV  v4l2_fourcc('A', 'Y', 'U', 'V')
#define LOGICVC_PIX_FMT_AVUY  v4l2_fourcc('A', 'V', 'U', 'Y')
#define LOGICVC_PIX_FMT_ALPHA v4l2_fourcc('A', '8', ' ', ' ')


static struct xylonfb_vmode_data xylonfb_vmode = {
	.fb_vmode = {
		.refresh = 60,
		.xres = 1024,
		.yres = 768,
		.pixclock = KHZ2PICOS(65000),
		.left_margin = 160,
		.right_margin = 24,
		.upper_margin = 29,
		.lower_margin = 3,
		.hsync_len = 136,
		.vsync_len = 6,
		.vmode = FB_VMODE_NONINTERLACED
	},
	.fb_vmode_name = "1024x768"
};

static unsigned short logicvc_layer_reg_offset[] = {
	(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_0_OFFSET),
	(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_1_OFFSET),
	(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_2_OFFSET),
	(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_3_OFFSET),
	(LOGICVC_LAYER_BASE_OFFSET + LOGICVC_LAYER_4_OFFSET)
};

static unsigned short logicvc_clut_reg_offset[] = {
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

static char *xylonfb_mode_option;

/* Function declarations */
static int xylonfb_set_timings(struct fb_info *fbi, int bpp);
static void xylonfb_logicvc_disp_ctrl(struct fb_info *fbi, bool enable);
static void xylonfb_enable_logicvc_output(struct fb_info *fbi);
static void xylonfb_disable_logicvc_output(struct fb_info *fbi);
static void xylonfb_enable_logicvc_layer(struct fb_info *fbi);
static void xylonfb_disable_logicvc_layer(struct fb_info *fbi);
static void xylonfb_fbi_update(struct fb_info *fbi);

/******************************************************************************/

static u32 xylonfb_get_reg(void *base_virt, unsigned long offset,
	struct xylonfb_layer_data *ld)
{
	return readl(base_virt + offset);
}

static void xylonfb_set_reg(u32 value,
	void *base_virt, unsigned long offset,
	struct xylonfb_layer_data *ld)
{
	writel(value, (base_virt + offset));
}

static unsigned long xylonfb_get_reg_mem_addr(
	void *base_virt, unsigned long offset,
	struct xylonfb_layer_data *ld)
{
	unsigned long ordinal = offset >> 3;

	if ((unsigned long)base_virt - (unsigned long)ld->reg_base_virt) {
		return (unsigned long)(&ld->layer_reg_list->hpos_reg) +
			(ordinal * sizeof(unsigned long));
	} else {
		return (unsigned long)(&ld->xylonfb_cd->reg_list->dtype_reg) +
			(ordinal * sizeof(unsigned long));
	}
}

static u32 xylonfb_get_reg_mem(
	void *base_virt, unsigned long offset,
	struct xylonfb_layer_data *ld)
{
	return *((unsigned long *)xylonfb_get_reg_mem_addr(
		base_virt, offset, ld));
}

static void xylonfb_set_reg_mem(u32 value,
	void *base_virt, unsigned long offset,
	struct xylonfb_layer_data *ld)
{
	unsigned long *reg_mem_addr =
		(unsigned long *)xylonfb_get_reg_mem_addr(
			base_virt, offset, ld);
	*reg_mem_addr = value;
	writel((*reg_mem_addr), (base_virt + offset));
}

/******************************************************************************/

static irqreturn_t xylonfb_isr(int irq, void *dev_id)
{
	struct fb_info **afbi = dev_get_drvdata(dev_id);
	struct fb_info *fbi = afbi[0];
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	u32 isr;

	driver_devel("%s IRQ %d\n", __func__, irq);

	isr = readl(ld->reg_base_virt + LOGICVC_INT_STAT_ROFF);
	if (isr & LOGICVC_V_SYNC_INT) {
		writel(LOGICVC_V_SYNC_INT,
			ld->reg_base_virt + LOGICVC_INT_STAT_ROFF);
		cd->vsync.cnt++;
		wake_up_interruptible(&cd->vsync.wait);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

/******************************************************************************/

static int xylonfb_open(struct fb_info *fbi, int user)
{
	struct xylonfb_layer_data *ld = fbi->par;

	driver_devel("%s\n", __func__);

	if (ld->layer_use_ref == 0) {
		/* turn on layer */
		xylonfb_enable_logicvc_layer(fbi);
	}
	ld->layer_use_ref++;
	ld->xylonfb_cd->xylonfb_use_ref++;

	return 0;
}

static int xylonfb_release(struct fb_info *fbi, int user)
{
	struct xylonfb_layer_data *ld = fbi->par;

	driver_devel("%s\n", __func__);

	ld->layer_use_ref--;
	if (ld->layer_use_ref == 0) {
		/* turn off layer */
		xylonfb_disable_logicvc_layer(fbi);
	}
	ld->xylonfb_cd->xylonfb_use_ref--;

	return 0;
}

/******************************************************************************/

static int xylonfb_check_var(struct fb_var_screeninfo *var,
	struct fb_info *fbi)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_layer_fix_data *lfdata = &ld->layer_fix;

	driver_devel("%s\n", __func__);

	if (var->xres < LOGICVC_MIN_XRES)
		var->xres = LOGICVC_MIN_XRES;
	if (var->xres > LOGICVC_MAX_XRES)
		var->xres = LOGICVC_MAX_XRES;
	if (var->yres < LOGICVC_MIN_VRES)
		var->yres = LOGICVC_MIN_VRES;
	if (var->yres > LOGICVC_MAX_VRES)
		var->yres = LOGICVC_MAX_VRES;

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->xres_virtual > lfdata->width)
		var->xres_virtual = lfdata->width;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;
	if (var->yres_virtual > lfdata->height)
		var->yres_virtual = lfdata->height;

	if ((var->xoffset + var->xres) >= var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres - 1;
	if ((var->yoffset + var->yres) >= var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres - 1;

	if (var->bits_per_pixel != fbi->var.bits_per_pixel) {
		if (var->bits_per_pixel == 24)
			var->bits_per_pixel = 32;
		else
			var->bits_per_pixel = fbi->var.bits_per_pixel;
	}

	var->grayscale = fbi->var.grayscale;

	var->transp.offset = fbi->var.transp.offset;
	var->transp.length = fbi->var.transp.length;
	var->transp.msb_right = fbi->var.transp.msb_right;
	var->red.offset = fbi->var.red.offset;
	var->red.length = fbi->var.red.length;
	var->red.msb_right = fbi->var.red.msb_right;
	var->green.offset = fbi->var.green.offset;
	var->green.length = fbi->var.green.length;
	var->green.msb_right = fbi->var.green.msb_right;
	var->blue.offset = fbi->var.blue.offset;
	var->blue.length = fbi->var.blue.length;
	var->blue.msb_right = fbi->var.blue.msb_right;
	var->height = fbi->var.height;
	var->width = fbi->var.width;
	var->sync = fbi->var.sync;
	var->rotate = fbi->var.rotate;

	return 0;
}

static int xylonfb_set_par(struct fb_info *fbi)
{
	struct fb_info **afbi = NULL;
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	int rc = 0;
	int i;
	char vmode_opt[VMODE_NAME_SZ];
	bool resolution_change, layer_on[LOGICVC_MAX_LAYERS];

	driver_devel("%s\n", __func__);

	if (cd->xylonfb_flags & XYLONFB_FLAG_VMODE_SET)
		return 0;

	if (!(cd->xylonfb_flags & XYLONFB_FLAG_EDID_VMODE) &&
		((fbi->var.xres ==
			cd->vmode_data_current.fb_vmode.xres) ||
		(fbi->var.yres ==
			cd->vmode_data_current.fb_vmode.yres))) {
		resolution_change = false;
	} else {
		resolution_change = true;
	}

	if (resolution_change ||
		(cd->xylonfb_flags & XYLONFB_FLAG_VMODE_INIT)) {

		if (!(cd->xylonfb_flags & XYLONFB_FLAG_VMODE_INIT)) {
			struct xylonfb_layer_data *ld;
			/* store id's of enabled layers */
			afbi = dev_get_drvdata(fbi->device);
			for (i = 0; i < cd->xylonfb_layers; i++) {
				ld = afbi[i]->par;
				if (ld->layer_ctrl_flags & LOGICVC_LAYER_ON)
					layer_on[i] = true;
				else
					layer_on[i] = false;
			}
		}

		xylonfb_disable_logicvc_output(fbi);
		xylonfb_logicvc_disp_ctrl(fbi, false);

		if (!(cd->xylonfb_flags & XYLONFB_FLAG_VMODE_INIT)) {
			/* we want 60Hz refresh rate */
			cd->vmode_data_current.fb_vmode.refresh = 60;
			sprintf(vmode_opt, "%dx%d%s-%d@%d%s",
				fbi->var.xres, fbi->var.yres,
				cd->vmode_data_current.fb_vmode_opts_cvt,
				fbi->var.bits_per_pixel,
				cd->vmode_data_current.fb_vmode.refresh,
				cd->vmode_data_current.fb_vmode_opts_ext);
			if (!strcmp(cd->vmode_data.fb_vmode_name, vmode_opt)) {
				cd->vmode_data_current = cd->vmode_data;
			} else {
				xylonfb_mode_option = vmode_opt;
				rc = xylonfb_set_timings(
					fbi, fbi->var.bits_per_pixel);
				xylonfb_mode_option = NULL;
			}
		}
		if (!rc) {
			if (cd->xylonfb_flags & XYLONFB_FLAG_PIXCLK_VALID) {
				rc = xylonfb_hw_pixclk_set(
					cd->xylonfb_pixclk_src_id,
					PICOS2KHZ(cd->vmode_data_current.fb_vmode.pixclock));
				if (rc)
					pr_err("Error xylonfb changing pixel clock\n");
			}
			xylonfb_fbi_update(fbi);
			pr_info("xylonfb video mode: %dx%d%s-%d@%d%s\n",
				fbi->var.xres, fbi->var.yres,
				cd->vmode_data_current.fb_vmode_opts_cvt,
				fbi->var.bits_per_pixel,
				cd->vmode_data_current.fb_vmode.refresh,
				cd->vmode_data_current.fb_vmode_opts_ext);
		}

		xylonfb_enable_logicvc_output(fbi);
		xylonfb_logicvc_disp_ctrl(fbi, true);

		/* set flag used for finding video mode only once */
		if (cd->xylonfb_flags & XYLONFB_FLAG_VMODE_INIT)
			cd->xylonfb_flags |= XYLONFB_FLAG_VMODE_SET;
		/* used only when resolution is changed */
		if (!(cd->xylonfb_flags & XYLONFB_FLAG_VMODE_SET)) {
			if (afbi) {
				for (i = 0; i < cd->xylonfb_layers; i++)
					if (layer_on[i])
						xylonfb_enable_logicvc_layer(afbi[i]);
			} else {
				xylonfb_enable_logicvc_layer(fbi);
			}
		}
	}

	return rc;
}

static int xylonfb_set_color_hw_rgb_to_yuv(
	u16 *transp, u16 *red, u16 *green, u16 *blue, int len, int idx,
	struct xylonfb_layer_data *ld)
{
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	u32 yuv_pixel;
	u32 y, cb, cr;
	u32 ykr, ykg, ykb, yk;
	u32 crkr, crkg, crkb;
	u32 cbkr, cbkg, cbkb;

	driver_devel("%s\n", __func__);

	if (idx > (LOGICVC_CLUT_SIZE-1) || len > LOGICVC_CLUT_SIZE)
		return -EINVAL;

	if ((cd->xylonfb_display_interface_type >> 4) == LOGICVC_DI_ITU656) {
		ykr  = 29900;
		ykg  = 58700;
		ykb  = 11400;
		yk   = 1600000;
		crkr = 51138;
		crkg = 42820;
		crkb = 8316;
		cbkr = 17258;
		cbkg = 33881;
		cbkb = 51140;
	} else {
		ykr  = 29900;
		ykg  = 58700;
		ykb  = 11400;
		yk   = 0;
		crkr = 49980;
		crkg = 41850;
		crkb = 8128;
		cbkr = 16868;
		cbkg = 33107;
		cbkb = 49970;
	}

	while (len > 0) {
		y = (
				(ykr * (red[idx] & 0xFF))
					+
				(ykg * (green[idx] & 0xFF))
					+
				(ykb * (blue[idx] & 0xFF))
					+
				 yk
			)
				/
			100000;
		cr = (
				(crkr * (red[idx] & 0xFF))
					-
				(crkg * (green[idx] & 0xFF))
					-
				(crkb * (blue[idx] & 0xFF))
					+
				 12800000
			 )
				/
			100000;
		cb = (
				(-cbkr * (red[idx] & 0xFF))
					-
				(cbkg * (green[idx] & 0xFF))
					+
				(cbkb * (blue[idx] & 0xFF))
					+
				12800000
			 )
				/
			100000;
		if (transp) {
			yuv_pixel = (((u32)transp[idx] & 0xFF) << 24) |
				(y << 16) | (cb << 8) | cr;
		} else {
			yuv_pixel =
				(0xFF << 24) | (y << 16) | (cb << 8) | cr;
		}
		writel(yuv_pixel, ld->layer_clut_base_virt +
			(idx*LOGICVC_CLUT_REGISTER_SIZE));
		len--;
		idx++;
	}

	return 0;
}

static int xylonfb_set_color_hw(u16 *transp, u16 *red, u16 *green, u16 *blue,
	int len, int idx, struct fb_info *fbi)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_layer_fix_data *lfdata = &ld->layer_fix;
	u32 pixel;
	int bpp_virt, toff, roff, goff, boff;

	driver_devel("%s\n", __func__);

	if ((fbi->fix.visual == FB_VISUAL_FOURCC) &&
		(fbi->var.grayscale == LOGICVC_PIX_FMT_AYUV)) {
		return xylonfb_set_color_hw_rgb_to_yuv(
			transp, red, green, blue, len, idx, ld);
	}

	bpp_virt = lfdata->bpp_virt;

	toff = fbi->var.transp.offset;
	roff = fbi->var.red.offset;
	goff = fbi->var.green.offset;
	boff = fbi->var.blue.offset;

	if (fbi->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		u32 clut_value;

		if (idx > (LOGICVC_CLUT_SIZE-1) || len > LOGICVC_CLUT_SIZE)
			return -EINVAL;

		if (lfdata->alpha_mode == LOGICVC_CLUT_16BPP_ALPHA) {
			if (transp) {
				while (len > 0) {
					clut_value =
						((((transp[idx] & 0xFC) >> 2) << toff) |
						(((red[idx] & 0xF8) >> 3) << roff) |
						(((green[idx] & 0xFC) >> 2) << goff) |
						(((blue[idx] & 0xF8) >> 3) << boff));
					writel(clut_value,
						ld->layer_clut_base_virt +
						(idx*LOGICVC_CLUT_REGISTER_SIZE));
					len--;
					idx++;
				}
			} else {
				while (len > 0) {
					clut_value =
						((0x3F << toff) |
						(((red[idx] & 0xF8) >> 3) << roff) |
						(((green[idx] & 0xFC) >> 2) << goff) |
						(((blue[idx] & 0xF8) >> 3) << boff));
					writel(clut_value,
						ld->layer_clut_base_virt +
						(idx*LOGICVC_CLUT_REGISTER_SIZE));
					len--;
					idx++;
				}
			}
		} else if (lfdata->alpha_mode == LOGICVC_CLUT_32BPP_ALPHA) {
			if (transp) {
				while (len > 0) {
					clut_value =
						(((transp[idx] & 0xFF) << toff) |
						((red[idx] & 0xFF) << roff) |
						((green[idx] & 0xFF) << goff) |
						((blue[idx] & 0xFF) << boff));
					writel(clut_value,
						ld->layer_clut_base_virt +
						(idx*LOGICVC_CLUT_REGISTER_SIZE));
					len--;
					idx++;
				}
			} else {
				while (len > 0) {
					clut_value =
						((0xFF << toff) |
						((red[idx] & 0xFF) << roff) |
						((green[idx] & 0xFF) << goff) |
						((blue[idx] & 0xFF) << boff));
					writel(clut_value,
						ld->layer_clut_base_virt +
						(idx*LOGICVC_CLUT_REGISTER_SIZE));
					len--;
					idx++;
				}
			}
		}
	} else if (fbi->fix.visual == FB_VISUAL_TRUECOLOR) {
		if (bpp_virt == 8) {
			if (lfdata->alpha_mode == LOGICVC_LAYER_ALPHA) {
				while (len > 0) {
					pixel = ((((red[idx] & 0xE0) >> 5) << roff) |
						(((green[idx] & 0xE0) >> 5) << goff) |
						(((blue[idx] & 0xC0) >> 6) << boff));
					((u32 *)(fbi->pseudo_palette))[idx] =
						(pixel << 24) | (pixel << 16) | (pixel << 8) | pixel;
					len--;
					idx++;
				}
			} else if (lfdata->alpha_mode == LOGICVC_PIXEL_ALPHA) {
				if (transp) {
					while (len > 0) {
						pixel = ((((transp[idx] & 0xE0) >> 5) << toff) |
							(((red[idx] & 0xE0) >> 5) << roff) |
							(((green[idx] & 0xE0) >> 5) << goff) |
							(((blue[idx] & 0xC0) >> 6) << boff));
						((u32 *)(fbi->pseudo_palette))[idx] =
							(pixel << 16) | pixel;
						len--;
						idx++;
					}
				} else {
					while (len > 0) {
						pixel = ((0x07 << toff) |
							(((red[idx] & 0xE0) >> 5) << roff) |
							(((green[idx] & 0xE0) >> 5) << goff) |
							(((blue[idx] & 0xC0) >> 6) << boff));
						((u32 *)(fbi->pseudo_palette))[idx] =
							(pixel << 16) | pixel;
						len--;
						idx++;
					}
				}
			}
		} else if (bpp_virt == 16) {
			if (lfdata->alpha_mode == LOGICVC_LAYER_ALPHA) {
				while (len > 0) {
					pixel = ((((red[idx] & 0xF8) >> 3) << roff) |
						(((green[idx] & 0xFC) >> 2) << goff) |
						(((blue[idx] & 0xF8) >> 3) << boff));
					((u32 *)(fbi->pseudo_palette))[idx] =
						(pixel << 16) | pixel;
					len--;
					idx++;
				}
			} else if (lfdata->alpha_mode == LOGICVC_PIXEL_ALPHA) {
				if (transp) {
					while (len > 0) {
						((u32 *)(fbi->pseudo_palette))[idx] =
							((((transp[idx] & 0xFC) >> 2) << toff) |
							(((red[idx] & 0xF8) >> 3) << roff) |
							(((green[idx] & 0xFC) >> 2) << goff) |
							(((blue[idx] & 0xF8) >> 3) << boff));
						len--;
						idx++;
					}
				} else {
					while (len > 0) {
						((u32 *)(fbi->pseudo_palette))[idx] =
							((0x3F << toff) |
							(((red[idx] & 0xF8) >> 3) << roff) |
							(((green[idx] & 0xFC) >> 2) << goff) |
							(((blue[idx] & 0xF8) >> 3) << boff));
						len--;
						idx++;
					}
				}
			}
		} else if (bpp_virt == 32) {
			if (lfdata->alpha_mode == LOGICVC_LAYER_ALPHA) {
				while (len > 0) {
					((u32 *)(fbi->pseudo_palette))[idx] =
						(((red[idx] & 0xFF) << roff) |
						((green[idx] & 0xFF) << goff) |
						((blue[idx] & 0xFF) << boff));
					len--;
					idx++;
				}
			} else if (lfdata->alpha_mode == LOGICVC_PIXEL_ALPHA) {
				if (transp) {
					while (len > 0) {
						((u32 *)(fbi->pseudo_palette))[idx] =
							(((transp[idx] & 0xFF) << toff) |
							((red[idx] & 0xFF) << roff) |
							((green[idx] & 0xFF) << goff) |
							((blue[idx] & 0xFF) << boff));
						len--;
						idx++;
					}
				} else {
					while (len > 0) {
						((u32 *)(fbi->pseudo_palette))[idx] =
							((0xFF << toff) |
							((red[idx] & 0xFF) << roff) |
							((green[idx] & 0xFF) << goff) |
							((blue[idx] & 0xFF) << boff));
						len--;
						idx++;
					}
				}
			}
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int xylonfb_set_color_reg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *fbi)
{
	driver_devel("%s\n", __func__);

	return xylonfb_set_color_hw(
		(u16 *)&transp, (u16 *)&red, (u16 *)&green, (u16 *)&blue,
		1, regno, fbi);
}

static int xylonfb_set_cmap(struct fb_cmap *cmap, struct fb_info *fbi)
{
	driver_devel("%s\n", __func__);

	return xylonfb_set_color_hw(
		cmap->transp, cmap->red, cmap->green, cmap->blue,
		cmap->len, cmap->start, fbi);
}

static void xylonfb_set_pixels(struct fb_info *fbi,
	struct xylonfb_layer_data *ld, int bpp, unsigned int pix)
{
	u32 *vmem;
	u8 *vmem8;
	u16 *vmem16;
	u32 *vmem32;
	int x, y, pix_off;

	driver_devel("%s\n", __func__);

	vmem = ld->fb_virt +
		(fbi->var.xoffset * (fbi->var.bits_per_pixel/4)) +
		(fbi->var.yoffset * fbi->var.xres_virtual *
		(fbi->var.bits_per_pixel/4));

	switch (bpp) {
	case 8:
		vmem8 = (u8 *)vmem;
		for (y = fbi->var.yoffset; y < fbi->var.yres; y++) {
			pix_off = (y * fbi->var.xres_virtual);
			for (x = fbi->var.xoffset; x < fbi->var.xres; x++)
				vmem8[pix_off+x] = pix;
		}
		break;
	case 16:
		vmem16 = (u16 *)vmem;
		for (y = fbi->var.yoffset; y < fbi->var.yres; y++) {
			pix_off = (y * fbi->var.xres_virtual);
			for (x = fbi->var.xoffset; x < fbi->var.xres; x++)
				vmem16[pix_off+x] = pix;
		}
		break;
	case 32:
		vmem32 = (u32 *)vmem;
		for (y = fbi->var.yoffset; y < fbi->var.yres; y++) {
			pix_off = (y * fbi->var.xres_virtual);
			for (x = fbi->var.xoffset; x < fbi->var.xres; x++)
				vmem32[pix_off+x] = pix;
		}
		break;
	}
}

static int xylonfb_blank(int blank_mode, struct fb_info *fbi)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_layer_fix_data *lfdata = &ld->layer_fix;
	u32 reg;

	driver_devel("%s\n", __func__);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		driver_devel("FB_BLANK_UNBLANK\n");
		reg = readl(ld->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		reg |= LOGICVC_V_EN_MSK;
		writel(reg, ld->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		mdelay(50);
		break;

	case FB_BLANK_NORMAL:
		driver_devel("FB_BLANK_NORMAL\n");
		switch (lfdata->bpp_virt) {
		case 8:
			switch (lfdata->alpha_mode) {
			case LOGICVC_LAYER_ALPHA:
				xylonfb_set_pixels(fbi, ld, 8, 0x00);
				break;
			case LOGICVC_PIXEL_ALPHA:
				xylonfb_set_pixels(fbi, ld, 16, 0xFF00);
				break;
			case LOGICVC_CLUT_16BPP_ALPHA:
			case LOGICVC_CLUT_32BPP_ALPHA:
				xylonfb_set_color_reg(0, 0, 0, 0, 0xFF, fbi);
				xylonfb_set_pixels(fbi, ld, 8, 0);
				break;
			}
			break;
		case 16:
			switch (lfdata->alpha_mode) {
			case LOGICVC_LAYER_ALPHA:
				xylonfb_set_pixels(fbi, ld, 16, 0x0000);
				break;
			case LOGICVC_PIXEL_ALPHA:
				xylonfb_set_pixels(fbi, ld, 32, 0xFF000000);
				break;
			}
			break;
		case 32:
			xylonfb_set_pixels(fbi, ld, 32, 0xFF000000);
			break;
		}
		break;

	case FB_BLANK_POWERDOWN:
		driver_devel("FB_BLANK_POWERDOWN\n");
		reg = readl(ld->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		reg &= ~LOGICVC_V_EN_MSK;
		writel(reg, ld->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		mdelay(50);
		break;

	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	default:
		driver_devel("FB_BLANK_ not supported!\n");
		return -EINVAL;
	}

	return 0;
}

static int xylonfb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *fbi)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;

	driver_devel("%s\n", __func__);

	if (fbi->var.xoffset == var->xoffset &&
		fbi->var.yoffset == var->yoffset)
		return 0;

	if (fbi->var.vmode & FB_VMODE_YWRAP) {
		return -EINVAL;
	} else {
		if (var->xoffset + fbi->var.xres > fbi->var.xres_virtual ||
			var->yoffset + fbi->var.yres > fbi->var.yres_virtual) {
			/* if smaller then physical layer video memory
			   allow panning */
			if ((var->xoffset + fbi->var.xres > ld->layer_fix.width)
					||
				(var->yoffset + fbi->var.yres > ld->layer_fix.height)) {
				return -EINVAL;
			}
		}
	}
	/* YCbCr 4:2:2 layer type can only have even layer xoffset */
	if (ld->layer_fix.layer_type == LOGICVC_YCBCR_LAYER &&
		ld->layer_fix.bpp_virt == 16) {
		var->xoffset &= ~1;
	}

	fbi->var.xoffset = var->xoffset;
	fbi->var.yoffset = var->yoffset;
	/* set layer memory X offset */
	cd->reg_access.xylonfb_set_reg_val(var->xoffset,
		ld->layer_reg_base_virt, LOGICVC_LAYER_HOR_OFF_ROFF,
		ld);
	/* set layer memory Y offset */
	cd->reg_access.xylonfb_set_reg_val(var->yoffset,
		ld->layer_reg_base_virt, LOGICVC_LAYER_VER_OFF_ROFF,
		ld);
	cd->reg_access.xylonfb_set_reg_val((fbi->var.xres-1),
		ld->layer_reg_base_virt, LOGICVC_LAYER_HOR_POS_ROFF,
		ld);
	/* apply changes in logiCVC */
	cd->reg_access.xylonfb_set_reg_val((fbi->var.yres-1),
		ld->layer_reg_base_virt, LOGICVC_LAYER_VER_POS_ROFF,
		ld);

	return 0;
}


static struct fb_ops xylonfb_ops = {
	.owner = THIS_MODULE,
	.fb_open = xylonfb_open,
	.fb_release = xylonfb_release,
	.fb_check_var = xylonfb_check_var,
	.fb_set_par = xylonfb_set_par,
	.fb_setcolreg = xylonfb_set_color_reg,
	.fb_setcmap = xylonfb_set_cmap,
	.fb_blank = xylonfb_blank,
	.fb_pan_display = xylonfb_pan_display,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_cursor = NULL,
	.fb_rotate = NULL,
	.fb_sync = NULL,
	.fb_ioctl = xylonfb_ioctl,
	.fb_mmap = NULL,
	.fb_get_caps = NULL,
	.fb_destroy = NULL,
};

/******************************************************************************/

static int xylonfb_find_next_layer(struct xylonfb_layer_fix_data *lfdata,
	int layers, int curr)
{
	unsigned long address, temp_address, loop_address;
	int i, next;

	driver_devel("%s\n", __func__);

	address = lfdata[curr].offset * lfdata[curr].width * lfdata[curr].bpp;
	temp_address = ~0;
	next = -1;

	for (i = 0; i < layers; i++) {
		loop_address =
			lfdata[i].offset * lfdata[i].width * lfdata[i].bpp;
		if (address < loop_address
				&&
			loop_address < temp_address) {
			next = i;
			temp_address = loop_address;
		}
	}

	return next;
}

static void xylonfb_set_yvirt(struct xylonfb_init_data *init_data,
	int layers, int curr)
{
	struct xylonfb_layer_fix_data *lfdata;
	unsigned long vmem_base_addr, vmem_high_addr;
	int next;

	driver_devel("%s\n", __func__);

	lfdata = init_data->lfdata;
	vmem_base_addr = init_data->vmem_base_addr;
	vmem_high_addr = init_data->vmem_high_addr;

	next = xylonfb_find_next_layer(lfdata, layers, curr);

	if (next != -1) {
		lfdata[curr].height =
			((lfdata[next].width * (lfdata[next].bpp/8) *
			lfdata[next].offset)
				-
			(lfdata[curr].width * (lfdata[curr].bpp/8) *
			lfdata[curr].offset))
				/
			(lfdata[curr].width * (lfdata[curr].bpp/8));
	} else { /* last physical logiCVC layer */
		lfdata[curr].height = LOGICVC_MAX_LINES + 1;
		while (1) {
			if (((lfdata[curr].width * (lfdata[curr].bpp/8) *
				lfdata[curr].height)
					+
				(lfdata[curr].width * (lfdata[curr].bpp/8) *
				lfdata[curr].offset))
					<=
				(vmem_high_addr - vmem_base_addr))
				break;
			/* FIXME - magic decrease step */
			lfdata[curr].height -= 64;
		}
	}

	if (lfdata[curr].height >
		(lfdata[curr].buffer_offset * LOGICVC_MAX_LAYER_BUFFERS)) {
		lfdata[curr].height =
			lfdata[curr].buffer_offset * LOGICVC_MAX_LAYER_BUFFERS;
	}

	lfdata[curr].layer_fix_info |=
		((lfdata[curr].height / lfdata[curr].buffer_offset) << 4);
}

static int xylonfb_map(int id, int layers, struct device *dev,
	struct xylonfb_layer_data *ld,
	unsigned long vmem_base_addr,
	unsigned long reg_base_phys, void *reg_base_virt,
	int memmap)
{
	struct xylonfb_layer_fix_data *lfdata = &ld->layer_fix;

	driver_devel("%s\n", __func__);

	/* logiCVC register mapping */
	ld->reg_base_phys = reg_base_phys;
	ld->reg_base_virt = reg_base_virt;
	/* check register mappings */
	if (!ld->reg_base_virt) {
		pr_err("Error xylonfb registers mapping\n");
		return -ENOMEM;
	}
	/* Video memory mapping */
	ld->fb_phys = vmem_base_addr +
		(lfdata->width * (lfdata->bpp/8) * lfdata->offset);
	ld->fb_size =
		lfdata->width * (lfdata->bpp/8) * lfdata->height;

	if (memmap) {
		if (ld->xylonfb_cd->xylonfb_flags & XYLONFB_FLAG_DMA_BUFFER) {
			/* NOT USED FOR NOW! */
			ld->fb_virt = dma_alloc_coherent(NULL,
				PAGE_ALIGN(ld->fb_size),
				&ld->fb_phys, GFP_KERNEL);
		} else {
			ld->fb_virt =
				ioremap_wc(ld->fb_phys, ld->fb_size);
		}
		/* check memory mappings */
		if (!ld->fb_virt) {
			pr_err("Error xylonfb vmem mapping\n");
			return -ENOMEM;
		}
	}
	ld->layer_reg_base_virt = ld->reg_base_virt +
		logicvc_layer_reg_offset[id];
	ld->layer_clut_base_virt = ld->reg_base_virt +
		logicvc_clut_reg_offset[id*LOGICVC_CLUT_0_INDEX_OFFSET];
	ld->layer_use_ref = 0;
	ld->layer_ctrl_flags = 0;

	return 0;
}

static void xylonfb_set_fbi_var_screeninfo(struct fb_var_screeninfo *var,
	struct xylonfb_common_data *cd)
{
	driver_devel("%s\n", __func__);

	var->xres = cd->vmode_data_current.fb_vmode.xres;
	var->yres = cd->vmode_data_current.fb_vmode.yres;
	var->pixclock = cd->vmode_data_current.fb_vmode.pixclock;
	var->left_margin = cd->vmode_data_current.fb_vmode.left_margin;
	var->right_margin = cd->vmode_data_current.fb_vmode.right_margin;
	var->upper_margin = cd->vmode_data_current.fb_vmode.upper_margin;
	var->lower_margin = cd->vmode_data_current.fb_vmode.lower_margin;
	var->hsync_len = cd->vmode_data_current.fb_vmode.hsync_len;
	var->vsync_len = cd->vmode_data_current.fb_vmode.vsync_len;
	var->sync = cd->vmode_data_current.fb_vmode.sync;
	var->vmode = cd->vmode_data_current.fb_vmode.vmode;
}

static void xylonfb_fbi_update(struct fb_info *fbi)
{
	struct fb_info **afbi = dev_get_drvdata(fbi->device);
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	int i, layers, layer_id;

	driver_devel("%s\n", __func__);

	if (!(cd->xylonfb_flags & XYLONFB_FLAG_EDID_VMODE) ||
		!(cd->xylonfb_flags & XYLONFB_FLAG_EDID_RDY) ||
		!afbi)
		return;

	layers = cd->xylonfb_layers;
	layer_id = ld->layer_fix.layer_fix_info & 0x0F;

	for (i = 0; i < layers; i++) {
		if (i == layer_id)
			continue;
		xylonfb_set_fbi_var_screeninfo(&afbi[i]->var, cd);
		afbi[i]->monspecs = afbi[layer_id]->monspecs;
	}
}

static void xylonfb_set_hw_specifics(struct fb_info *fbi,
	struct xylonfb_layer_data *ld,
	struct xylonfb_layer_fix_data *lfdata,
	unsigned long reg_base_phys)
{
	driver_devel("%s\n", __func__);

	fbi->fix.smem_start = ld->fb_phys;
	fbi->fix.smem_len = ld->fb_size;
	if (lfdata->layer_type == LOGICVC_RGB_LAYER)
		fbi->fix.type = FB_TYPE_PACKED_PIXELS;
	else if (lfdata->layer_type == LOGICVC_YCBCR_LAYER)
		fbi->fix.type = FB_TYPE_FOURCC;
	if ((lfdata->layer_type == LOGICVC_YCBCR_LAYER) ||
		(lfdata->layer_type == LOGICVC_ALPHA_LAYER)) {
		fbi->fix.visual = FB_VISUAL_FOURCC;
	} else if ((lfdata->layer_type == LOGICVC_RGB_LAYER) &&
		(lfdata->bpp == 8) &&
		((lfdata->alpha_mode == LOGICVC_CLUT_16BPP_ALPHA) ||
		(lfdata->alpha_mode == LOGICVC_CLUT_32BPP_ALPHA))) {
		fbi->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	} else {
		/*
			Other logiCVC layer pixel formats:
			- 8 bpp: LAYER or PIXEL alpha
			  It is not true color, RGB triplet is stored in 8 bits.
			- 16 bpp:
			  LAYER alpha: RGB triplet is stored in 16 bits
			  PIXEL alpha: ARGB quadriplet is stored in 32 bits
			- 32 bpp: LAYER or PIXEL alpha
			  True color, RGB triplet or ARGB quadriplet
			  is stored in 32 bits.
		*/
		fbi->fix.visual = FB_VISUAL_TRUECOLOR;
	}
	/* sanity check */
	if ((lfdata->bpp != 8) &&
		((lfdata->alpha_mode == LOGICVC_CLUT_16BPP_ALPHA) ||
		(lfdata->alpha_mode == LOGICVC_CLUT_32BPP_ALPHA))) {
		pr_warn("xylonfb invalid layer alpha!\n");
		lfdata->alpha_mode = LOGICVC_LAYER_ALPHA;
	}
	fbi->fix.xpanstep = 1;
	fbi->fix.ypanstep = 1;
	fbi->fix.ywrapstep = 0;
	fbi->fix.line_length = lfdata->width * (lfdata->bpp/8);
	fbi->fix.mmio_start = reg_base_phys;
	fbi->fix.mmio_len = LOGICVC_REGISTERS_RANGE;
	fbi->fix.accel = FB_ACCEL_NONE;

	fbi->var.xres_virtual = lfdata->width;
	if (lfdata->height <= LOGICVC_MAX_LINES)
		fbi->var.yres_virtual = lfdata->height;
	else
		fbi->var.yres_virtual = LOGICVC_MAX_LINES;
	fbi->var.bits_per_pixel = lfdata->bpp;
	switch (lfdata->layer_type) {
	case LOGICVC_RGB_LAYER:
		fbi->var.grayscale = 0;
		break;
	case LOGICVC_YCBCR_LAYER:
		if (lfdata->bpp == 8) {
			fbi->var.grayscale = LOGICVC_PIX_FMT_AYUV;
		} else if (lfdata->bpp == 16) {
			if (ld->layer_ctrl_flags & LOGICVC_SWAP_RB)
				fbi->var.grayscale = V4L2_PIX_FMT_YVYU;
			else
				fbi->var.grayscale = V4L2_PIX_FMT_VYUY;
		} else if (lfdata->bpp == 32) {
			if (ld->layer_ctrl_flags & LOGICVC_SWAP_RB)
				fbi->var.grayscale = LOGICVC_PIX_FMT_AVUY;
			else
				fbi->var.grayscale = LOGICVC_PIX_FMT_AYUV;
		}
		break;
	case LOGICVC_ALPHA_LAYER:
		/* logiCVC Alpha layer 8bpp */
		fbi->var.grayscale = LOGICVC_PIX_FMT_ALPHA;
		break;
	}

	/*
		Set values according to logiCVC layer data width configuration:
		- layer data width can be 1, 2, 4 bytes
		- layer data width for 16 bpp can be 2 or 4 bytes
	*/
	if (lfdata->alpha_mode == LOGICVC_LAYER_ALPHA) {
		fbi->var.transp.offset = 0;
		fbi->var.transp.length = 0;
	}
	switch (lfdata->bpp_virt) {
	case 8:
		switch (lfdata->alpha_mode) {
		case LOGICVC_PIXEL_ALPHA:
			fbi->var.transp.offset = 8;
			fbi->var.transp.length = 3;

		case LOGICVC_LAYER_ALPHA:
			fbi->var.red.offset = 5;
			fbi->var.red.length = 3;
			fbi->var.green.offset = 2;
			fbi->var.green.length = 3;
			fbi->var.blue.offset = 0;
			fbi->var.blue.length = 2;
			break;

		case LOGICVC_CLUT_16BPP_ALPHA:
			fbi->var.transp.offset = 24;
			fbi->var.transp.length = 6;
			fbi->var.red.offset = 19;
			fbi->var.red.length = 5;
			fbi->var.green.offset = 10;
			fbi->var.green.length = 6;
			fbi->var.blue.offset = 3;
			fbi->var.blue.length = 5;
			break;

		case LOGICVC_CLUT_32BPP_ALPHA:
			fbi->var.transp.offset = 24;
			fbi->var.transp.length = 8;
			fbi->var.red.offset = 16;
			fbi->var.red.length = 8;
			fbi->var.green.offset = 8;
			fbi->var.green.length = 8;
			fbi->var.blue.offset = 0;
			fbi->var.blue.length = 8;
			break;
		}
		break;
	case 16:
		switch (lfdata->alpha_mode) {
		case LOGICVC_PIXEL_ALPHA:
			fbi->var.transp.offset = 24;
			fbi->var.transp.length = 6;

		case LOGICVC_LAYER_ALPHA:
			fbi->var.red.offset = 11;
			fbi->var.red.length = 5;
			fbi->var.green.offset = 5;
			fbi->var.green.length = 6;
			fbi->var.blue.offset = 0;
			fbi->var.blue.length = 5;
			break;
		}
		break;
	case 32:
		switch (lfdata->alpha_mode) {
		case LOGICVC_PIXEL_ALPHA:
			fbi->var.transp.offset = 24;
			fbi->var.transp.length = 8;

		case LOGICVC_LAYER_ALPHA:
			fbi->var.red.offset = 16;
			fbi->var.red.length = 8;
			fbi->var.green.offset = 8;
			fbi->var.green.length = 8;
			fbi->var.blue.offset = 0;
			fbi->var.blue.length = 8;
			break;
		}
		break;
	}
	fbi->var.transp.msb_right = 0;
	fbi->var.red.msb_right = 0;
	fbi->var.green.msb_right = 0;
	fbi->var.blue.msb_right = 0;
	fbi->var.activate = FB_ACTIVATE_NOW;
	fbi->var.height = 0;
	fbi->var.width = 0;
	fbi->var.sync = 0;
	fbi->var.rotate = 0;
}

static int xylonfb_set_timings(struct fb_info *fbi, int bpp)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	struct fb_var_screeninfo fb_var;
	int rc;

	driver_devel("%s\n", __func__);

	if ((cd->xylonfb_flags & XYLONFB_FLAG_VMODE_INIT) &&
		(!(cd->xylonfb_flags & XYLONFB_FLAG_EDID_RDY)) &&
		memchr(cd->vmode_data.fb_vmode_name, 'x', 10)) {
		cd->vmode_data_current = cd->vmode_data;
		return 0;
	}

	/* switch-case to default */
	rc = 255;
	if ((cd->xylonfb_flags & XYLONFB_FLAG_EDID_VMODE) &&
		(cd->xylonfb_flags & XYLONFB_FLAG_EDID_RDY)) {
		if (cd->xylonfb_flags & XYLONFB_FLAG_VMODE_INIT) {
#if defined(CONFIG_FB_XYLON_MISC)
			fb_var = *(cd->xylonfb_misc->var_screeninfo);
#endif
		} else {
			rc = fb_find_mode(&fb_var, fbi, xylonfb_mode_option,
				fbi->monspecs.modedb, fbi->monspecs.modedb_len,
				&xylonfb_vmode.fb_vmode, bpp);
			if ((rc != 1) && (rc != 2))
				return -EINVAL;
#if defined(CONFIG_FB_XYLON_MISC)
			if (fbi->monspecs.modedb &&
				cd->xylonfb_misc->monspecs->misc & FB_MISC_1ST_DETAIL)
				if ((fbi->var.xres == fbi->monspecs.modedb[0].xres) &&
					(fbi->var.yres == fbi->monspecs.modedb[0].yres)) {
					fb_videomode_to_var(&fb_var, &fbi->monspecs.modedb[0]);
				}
#endif
		}
	} else {
		rc = fb_find_mode(&fb_var, fbi, xylonfb_mode_option, NULL, 0,
			&xylonfb_vmode.fb_vmode, bpp);
	}
#ifdef DEBUG
	switch (rc) {
	case 0:
		pr_err("Error xylonfb video mode\n"
			"using driver default mode %dx%dM-%d@%d\n",
			xylonfb_vmode.fb_vmode.xres,
			xylonfb_vmode.fb_vmode.yres,
			bpp,
			xylonfb_vmode.fb_vmode.refresh);
		break;
	case 1:
		driver_devel("xylonfb video mode %s\n", xylonfb_mode_option);
		break;
	case 2:
		pr_notice("xylonfb video mode %s with ignored refresh rate\n",
			xylonfb_mode_option);
		break;
	case 3:
		pr_notice("xylonfb default video mode %dx%dM-%d@%d\n",
			xylonfb_vmode.fb_vmode.xres,
			xylonfb_vmode.fb_vmode.yres,
			bpp,
			xylonfb_vmode.fb_vmode.refresh);
		break;
	case 4:
		pr_notice("xylonfb video mode fallback\n");
		break;
	default:
		break;
	}
#endif

	cd->vmode_data_current.ctrl_reg = cd->vmode_data.ctrl_reg;
	cd->vmode_data_current.fb_vmode.xres = fb_var.xres;
	cd->vmode_data_current.fb_vmode.yres = fb_var.yres;
	cd->vmode_data_current.fb_vmode.pixclock = fb_var.pixclock;
	cd->vmode_data_current.fb_vmode.left_margin = fb_var.left_margin;
	cd->vmode_data_current.fb_vmode.right_margin = fb_var.right_margin;
	cd->vmode_data_current.fb_vmode.upper_margin = fb_var.upper_margin;
	cd->vmode_data_current.fb_vmode.lower_margin = fb_var.lower_margin;
	cd->vmode_data_current.fb_vmode.hsync_len = fb_var.hsync_len;
	cd->vmode_data_current.fb_vmode.vsync_len = fb_var.vsync_len;
	cd->vmode_data_current.fb_vmode.sync = fb_var.sync;
	cd->vmode_data_current.fb_vmode.vmode = fb_var.vmode;
	cd->vmode_data_current.fb_vmode.refresh =
		DIV_ROUND_CLOSEST(
			(PICOS2KHZ(fb_var.pixclock) * 1000),
			((fb_var.xres + fb_var.left_margin +
			fb_var.right_margin + fb_var.hsync_len)
				*
			(fb_var.yres + fb_var.upper_margin +
			fb_var.lower_margin + fb_var.vsync_len)));
	strcpy(cd->vmode_data_current.fb_vmode_opts_cvt,
		cd->vmode_data.fb_vmode_opts_cvt);
	strcpy(cd->vmode_data_current.fb_vmode_opts_ext,
		cd->vmode_data.fb_vmode_opts_ext);
	sprintf(cd->vmode_data_current.fb_vmode_name,
		"%dx%d%s-%d@%d%s",
		fb_var.xres, fb_var.yres,
		cd->vmode_data_current.fb_vmode_opts_cvt,
		fb_var.bits_per_pixel,
		cd->vmode_data_current.fb_vmode.refresh,
		cd->vmode_data_current.fb_vmode_opts_ext);

	if ((cd->xylonfb_flags & XYLONFB_FLAG_EDID_RDY) ||
		!memchr(cd->vmode_data.fb_vmode_name, 'x', 10)) {
		cd->vmode_data = cd->vmode_data_current;
	}

	return 0;
}

static int xylonfb_register_fb(struct fb_info *fbi,
	struct xylonfb_layer_data *ld,
	unsigned long reg_base_phys, int id, int *regfb)
{
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	struct xylonfb_layer_fix_data *lfdata = &ld->layer_fix;
	int alpha;

	driver_devel("%s\n", __func__);

	fbi->flags = FBINFO_DEFAULT;
	fbi->screen_base = (char __iomem *)ld->fb_virt;
	fbi->screen_size = ld->fb_size;
	fbi->pseudo_palette =
		kzalloc(sizeof(u32) * XYLONFB_PSEUDO_PALETTE_SZ, GFP_KERNEL);
	fbi->fbops = &xylonfb_ops;

	sprintf(fbi->fix.id, "Xylon FB%d", id);
	xylonfb_set_hw_specifics(fbi, ld, lfdata, reg_base_phys);
	if (!(cd->xylonfb_flags & XYLONFB_FLAG_DEFAULT_VMODE_SET)) {
		xylonfb_set_timings(fbi, fbi->var.bits_per_pixel);
		cd->xylonfb_flags |= XYLONFB_FLAG_DEFAULT_VMODE_SET;
	}
	xylonfb_set_fbi_var_screeninfo(&fbi->var, cd);
	fbi->mode = &cd->vmode_data_current.fb_vmode;
	fbi->mode->name = cd->vmode_data_current.fb_vmode_name;

	if (lfdata->alpha_mode == LOGICVC_LAYER_ALPHA)
		alpha = 0;
	else
		alpha = 1;
	if (fb_alloc_cmap(&fbi->cmap, XYLONFB_PSEUDO_PALETTE_SZ, alpha))
		return -ENOMEM;

	*regfb = register_framebuffer(fbi);
	if (*regfb) {
		pr_err("Error xylonfb registering xylonfb %d\n", id);
		return -EINVAL;
	}
	pr_info("xylonfb %d registered\n", id);
	/* after fb driver registration, values in struct fb_info
		must not be changed anywhere else except in xylonfb_set_par */

	return 0;
}

static void xylonfb_init_layer_regs(struct xylonfb_layer_data *ld)
{
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	u32 reg_val;

	switch (ld->layer_fix.bpp_virt) {
	case 8:
		switch (ld->layer_fix.alpha_mode) {
		case LOGICVC_CLUT_16BPP_ALPHA:
			reg_val = TRANSPARENT_COLOR_8BPP_CLUT_16;
			break;
		case LOGICVC_CLUT_32BPP_ALPHA:
			reg_val = TRANSPARENT_COLOR_8BPP_CLUT_24;
			break;
		default:
			reg_val = TRANSPARENT_COLOR_8BPP;
			break;
		}
		break;
	case 16:
		reg_val = TRANSPARENT_COLOR_16BPP;
		break;
	case 32:
		reg_val = TRANSPARENT_COLOR_24BPP;
		break;
	default:
		reg_val = TRANSPARENT_COLOR_24BPP;
		break;
	}
	cd->reg_access.xylonfb_set_reg_val(reg_val,
		ld->layer_reg_base_virt, LOGICVC_LAYER_TRANSP_ROFF,
		ld);

	if (!(cd->xylonfb_flags & LOGICVC_READABLE_REGS))
		cd->reg_access.xylonfb_set_reg_val(0xFF,
			ld->layer_reg_base_virt, LOGICVC_LAYER_ALPHA_ROFF,
			ld);

	reg_val = ld->layer_ctrl_flags;
	cd->reg_access.xylonfb_set_reg_val(reg_val,
		ld->layer_reg_base_virt, LOGICVC_LAYER_CTRL_ROFF,
		ld);
}

static void xylonfb_logicvc_disp_ctrl(struct fb_info *fbi, bool enable)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	u32 val;

	driver_devel("%s\n", __func__);

	if (enable) {
		val = LOGICVC_EN_VDD_MSK;
		writel(val, ld->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		mdelay(cd->power_on_delay);
		val |= LOGICVC_V_EN_MSK;
		writel(val, ld->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		mdelay(cd->signal_on_delay);
		val |= LOGICVC_EN_BLIGHT_MSK;
		writel(val, ld->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
	} else {
		writel(0, ld->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
	}
}

static void xylonfb_enable_logicvc_layer(struct fb_info *fbi)
{
	struct xylonfb_layer_data *ld = fbi->par;

	driver_devel("%s\n", __func__);

	ld->layer_ctrl_flags |= LOGICVC_LAYER_ON;
	ld->xylonfb_cd->reg_access.xylonfb_set_reg_val(
		ld->layer_ctrl_flags,
		ld->layer_reg_base_virt, LOGICVC_LAYER_CTRL_ROFF,
		ld);
}

static void xylonfb_disable_logicvc_layer(struct fb_info *fbi)
{
	struct xylonfb_layer_data *ld = fbi->par;

	driver_devel("%s\n", __func__);

	ld->layer_ctrl_flags &= (~LOGICVC_LAYER_ON);
	ld->xylonfb_cd->reg_access.xylonfb_set_reg_val(
		ld->layer_ctrl_flags,
		ld->layer_reg_base_virt, LOGICVC_LAYER_CTRL_ROFF,
		ld);
}

static void xylonfb_enable_logicvc_output(struct fb_info *fbi)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;

	driver_devel("%s\n", __func__);

	writel(cd->vmode_data_current.fb_vmode.right_margin-1,
		ld->reg_base_virt + LOGICVC_SHSY_FP_ROFF);
	writel(cd->vmode_data_current.fb_vmode.hsync_len-1,
		ld->reg_base_virt + LOGICVC_SHSY_ROFF);
	writel(cd->vmode_data_current.fb_vmode.left_margin-1,
		ld->reg_base_virt + LOGICVC_SHSY_BP_ROFF);
	writel(cd->vmode_data_current.fb_vmode.xres-1,
		ld->reg_base_virt + LOGICVC_SHSY_RES_ROFF);
	writel(cd->vmode_data_current.fb_vmode.lower_margin-1,
		ld->reg_base_virt + LOGICVC_SVSY_FP_ROFF);
	writel(cd->vmode_data_current.fb_vmode.vsync_len-1,
		ld->reg_base_virt + LOGICVC_SVSY_ROFF);
	writel(cd->vmode_data_current.fb_vmode.upper_margin-1,
		ld->reg_base_virt + LOGICVC_SVSY_BP_ROFF);
	writel(cd->vmode_data_current.fb_vmode.yres-1,
		ld->reg_base_virt + LOGICVC_SVSY_RES_ROFF);
	cd->reg_access.xylonfb_set_reg_val(
		cd->vmode_data_current.ctrl_reg,
		ld->reg_base_virt,
		LOGICVC_SCTRL_ROFF,
		ld);
	writel(SD_REG_INIT, ld->reg_base_virt + LOGICVC_SDTYPE_ROFF);

	driver_devel("\n" \
		"logiCVC HW parameters:\n" \
		"    Horizontal Front Porch: %d pixclks\n" \
		"    Horizontal Sync:        %d pixclks\n" \
		"    Horizontal Back Porch:  %d pixclks\n" \
		"    Vertical Front Porch:   %d pixclks\n" \
		"    Vertical Sync:          %d pixclks\n" \
		"    Vertical Back Porch:    %d pixclks\n" \
		"    Pixel Clock:            %d ps\n" \
		"    Horizontal Res:         %d\n" \
		"    Vertical Res:           %d\n" \
		"\n", \
		cd->vmode_data_current.fb_vmode.right_margin,
		cd->vmode_data_current.fb_vmode.hsync_len,
		cd->vmode_data_current.fb_vmode.left_margin,
		cd->vmode_data_current.fb_vmode.lower_margin,
		cd->vmode_data_current.fb_vmode.vsync_len,
		cd->vmode_data_current.fb_vmode.upper_margin,
		cd->vmode_data_current.fb_vmode.pixclock,
		cd->vmode_data_current.fb_vmode.xres,
		cd->vmode_data_current.fb_vmode.yres);
}

static void xylonfb_disable_logicvc_output(struct fb_info *fbi)
{
	struct fb_info **afbi = dev_get_drvdata(fbi->device);
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	int i;

	driver_devel("%s\n", __func__);

	if (afbi) {
		for (i = 0; i < cd->xylonfb_layers; i++)
			xylonfb_disable_logicvc_layer(afbi[i]);
	}
}

static void xylonfb_start(struct fb_info **afbi, int layers)
{
	struct xylonfb_layer_data *ld;
	int i;

	driver_devel("%s\n", __func__);

	/* turn OFF all layers except already used ones */
	for (i = 0; i < layers; i++) {
		ld = afbi[i]->par;
		if (ld->layer_ctrl_flags & LOGICVC_LAYER_ON)
			continue;
		/* turn off layer */
		xylonfb_disable_logicvc_layer(afbi[i]);
	}
	/* print layer parameters */
	for (i = 0; i < layers; i++) {
		ld = afbi[i]->par;
		driver_devel("logiCVC layer %d\n" \
			"    Registers Base Address:     0x%lX\n" \
			"    Layer Video Memory Address: 0x%lX\n" \
			"    X resolution:               %d\n" \
			"    Y resolution:               %d\n" \
			"    X resolution (virtual):     %d\n" \
			"    Y resolution (virtual):     %d\n" \
			"    Line length (bytes):        %d\n" \
			"    Bits per Pixel:             %d\n" \
			"\n", \
			i,
			(unsigned long)ld->reg_base_phys,
			(unsigned long)ld->fb_phys,
			afbi[i]->var.xres,
			afbi[i]->var.yres,
			afbi[i]->var.xres_virtual,
			afbi[i]->var.yres_virtual,
			afbi[i]->fix.line_length,
			afbi[i]->var.bits_per_pixel);
	}
}

/******************************************************************************/

static int xylonfb_event_notify(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct fb_event *fbe = data;
	struct fb_info *fbi = fbe->info;
	int ret = 0;

	driver_devel("%s\n", __func__);

	switch (event) {
	case XYLONFB_EVENT_FBI_UPDATE:
		xylonfb_fbi_update(fbi);
		break;
	}

	return ret;
}

/******************************************************************************/

static void xylonfb_get_vmode_opts(
	struct xylonfb_init_data *init_data,
	struct xylonfb_common_data *cd)
{
	char cvt_opt[VMODE_OPTS_SZ] = "MR";
	char ext_opt[VMODE_OPTS_SZ] = "im";
	char *s, *opt, *ext, *c, *pco, *peo;

	if (cd->xylonfb_flags & XYLONFB_FLAG_EDID_VMODE)
		return;

	s = init_data->vmode_data.fb_vmode_name;
	opt = cd->vmode_data.fb_vmode_opts_cvt;
	ext = cd->vmode_data.fb_vmode_opts_ext;
	pco = cvt_opt;
	peo = ext_opt;

	while (*pco) {
		c = strchr(s, (int)(*pco));
		if (c)
			*opt++ = *c;
		pco++;
	}
	while (*peo) {
		c = strchr(s, (int)(*peo));
		if (c)
			*ext++ = *c;
		peo++;
	}
}

int xylonfb_init_driver(struct xylonfb_init_data *init_data)
{
	struct device *dev;
	struct fb_info **afbi;
	struct fb_info *fbi;
	struct xylonfb_common_data *cd;
	struct xylonfb_layer_data *ld;
	struct resource *reg_res, *irq_res;
	void *reg_base_virt;
	unsigned long reg_base_phys;
	int reg_range, layers, active_layer;
	int i, rc, memmap;
	int regfb[LOGICVC_MAX_LAYERS];

	driver_devel("%s\n", __func__);

	dev = &init_data->pdev->dev;

	reg_res = platform_get_resource(init_data->pdev, IORESOURCE_MEM, 0);
	irq_res = platform_get_resource(init_data->pdev, IORESOURCE_IRQ, 0);
	if ((!reg_res) || (!irq_res)) {
		pr_err("Error xylonfb resources\n");
		return -ENODEV;
	}

	layers = init_data->layers;
	if (layers == 0) {
		pr_err("Error xylonfb zero layers\n");
		return -ENODEV;
	}
	active_layer = init_data->active_layer;
	if (active_layer >= layers) {
		pr_err("Error xylonfb default layer: set to 0\n");
		active_layer = 0;
	}

	afbi = kzalloc(sizeof(struct fb_info *) * layers, GFP_KERNEL);
	cd = kzalloc(sizeof(struct xylonfb_common_data), GFP_KERNEL);
	if (!afbi || !cd) {
		pr_err("Error xylonfb allocating internal data\n");
		rc = -ENOMEM;
		goto err_mem;
	}

	BLOCKING_INIT_NOTIFIER_HEAD(&cd->xylonfb_notifier_list);
	cd->xylonfb_nb.notifier_call = xylonfb_event_notify;
	blocking_notifier_chain_register(
		&cd->xylonfb_notifier_list, &cd->xylonfb_nb);

	cd->xylonfb_display_interface_type =
		init_data->display_interface_type;
	cd->xylonfb_layers = layers;
	cd->xylonfb_flags |= XYLONFB_FLAG_VMODE_INIT;
	cd->xylonfb_console_layer = active_layer;
	if (init_data->flags & XYLONFB_FLAG_ADV7511_SKIP) {
		cd->xylonfb_flags |= XYLONFB_FLAG_ADV7511_SKIP;
	} else {
		if (init_data->flags & XYLONFB_FLAG_EDID_VMODE)
			cd->xylonfb_flags |= XYLONFB_FLAG_EDID_VMODE;
		if (init_data->flags & XYLONFB_FLAG_EDID_PRINT)
			cd->xylonfb_flags |= XYLONFB_FLAG_EDID_PRINT;
	}
	if (init_data->flags & LOGICVC_READABLE_REGS) {
		cd->xylonfb_flags |= LOGICVC_READABLE_REGS;
		cd->reg_access.xylonfb_get_reg_val = xylonfb_get_reg;
		cd->reg_access.xylonfb_set_reg_val = xylonfb_set_reg;
	} else {
		cd->reg_list =
			kzalloc(sizeof(struct xylonfb_registers), GFP_KERNEL);
		cd->reg_access.xylonfb_get_reg_val = xylonfb_get_reg_mem;
		cd->reg_access.xylonfb_set_reg_val = xylonfb_set_reg_mem;
	}

	sprintf(init_data->vmode_data.fb_vmode_name, "%s-%d@%d",
		init_data->vmode_data.fb_vmode_name,
		init_data->lfdata[active_layer].bpp,
		init_data->vmode_data.fb_vmode.refresh);
	if (init_data->vmode_params_set) {
		cd->vmode_data = init_data->vmode_data;
	} else {
		xylonfb_mode_option = init_data->vmode_data.fb_vmode_name;
		cd->vmode_data.ctrl_reg = init_data->vmode_data.ctrl_reg;
		cd->vmode_data.fb_vmode.refresh =
			init_data->vmode_data.fb_vmode.refresh;
	}
	xylonfb_get_vmode_opts(init_data, cd);

	if (init_data->pixclk_src_id) {
		if (xylonfb_hw_pixclk_supported(init_data->pixclk_src_id)) {
			cd->xylonfb_pixclk_src_id = init_data->pixclk_src_id;
			cd->xylonfb_flags |= XYLONFB_FLAG_PIXCLK_VALID;
		} else {
			pr_info("xylonfb pixel clock not supported\n");
		}
	} else {
		pr_info("xylonfb external pixel clock\n");
	}

	ld = NULL;

	reg_base_phys = reg_res->start;
	reg_range = resource_size(reg_res);
	reg_base_virt = ioremap_nocache(reg_base_phys, reg_range);

	/* load layer parameters for all layers */
	for (i = 0; i < layers; i++)
		regfb[i] = -1;
	memmap = 1;

	/* make /dev/fb0 to be default active layer
	   regardless how logiCVC layers are organized */
	for (i = active_layer; i < layers; i++) {
		if (regfb[i] != -1)
			continue;

		fbi = framebuffer_alloc(sizeof(struct xylonfb_layer_data), dev);
		if (!fbi) {
			pr_err("Error xylonfb allocate info\n");
			rc = -ENOMEM;
			goto err_fb;
		}
		afbi[i] = fbi;
		ld = fbi->par;
		ld->xylonfb_cd = cd;

#if defined(CONFIG_FB_XYLON_MISC)
		if (!cd->xylonfb_misc) {
			cd->xylonfb_misc = kzalloc(
				sizeof(struct xylonfb_misc_data), GFP_KERNEL);
			if (cd->xylonfb_misc) {
				xylonfb_misc_init(fbi);
			} else {
				pr_err("Error xylonfb allocating misc internal data\n");
				goto err_fb;
			}
		}
#endif

		xylonfb_set_yvirt(init_data, layers, i);

		ld->layer_fix = init_data->lfdata[i];
		if (!(cd->xylonfb_flags & LOGICVC_READABLE_REGS)) {
			ld->layer_reg_list =
				kzalloc(sizeof(struct xylonfb_layer_registers), GFP_KERNEL);
		}

		rc = xylonfb_map(i, layers, dev, ld, init_data->vmem_base_addr,
			reg_base_phys, reg_base_virt, memmap);
		if (rc)
			goto err_fb;
		memmap = 0;

		ld->layer_ctrl_flags = init_data->layer_ctrl_flags[i];
		xylonfb_init_layer_regs(ld);

		rc = xylonfb_register_fb(fbi, ld, reg_base_phys, i, &regfb[i]);
		if (rc)
			goto err_fb;

		fbi->monspecs = afbi[cd->xylonfb_console_layer]->monspecs;

		mutex_init(&ld->layer_mutex);

		/* register following layers in HW configuration order */
		if (active_layer > 0) {
			i = -1; /* after for loop increment i will be zero */
			active_layer = -1;
		}

		driver_devel( \
			"    Layer ID %d\n" \
			"    Layer offset %u\n" \
			"    Layer buffer offset %hd\n" \
			"    Layer buffers %d\n" \
			"    Layer width %d pixels\n" \
			"    Layer height %d lines\n" \
			"    Layer bits per pixel %d\n" \
			"    Layer bits per pixel (virtual) %d\n" \
			"    Layer FB size %ld bytes\n", \
			(ld->layer_fix.layer_fix_info & 0x0F),
			ld->layer_fix.offset,
			ld->layer_fix.buffer_offset,
			(ld->layer_fix.layer_fix_info >> 4),
			ld->layer_fix.width,
			ld->layer_fix.height,
			ld->layer_fix.bpp,
			ld->layer_fix.bpp_virt,
			ld->fb_size);
	}

	if (ld) {
		if (!(cd->xylonfb_flags & LOGICVC_READABLE_REGS))
			cd->reg_access.xylonfb_set_reg_val(0xFFFF,
				ld->reg_base_virt, LOGICVC_INT_MASK_ROFF,
				ld);
	} else {
		pr_warn("Warning xylonfb initialization not completed\n");
	}

	cd->xylonfb_bg_layer_bpp = init_data->bg_layer_bpp;
	cd->xylonfb_bg_layer_alpha_mode = init_data->bg_layer_alpha_mode;
	driver_devel("BG layer %dbpp\n", init_data->bg_layer_bpp);

	cd->xylonfb_irq = irq_res->start;
	rc = request_irq(cd->xylonfb_irq, xylonfb_isr,
		IRQF_TRIGGER_HIGH, DEVICE_NAME, dev);
	if (rc) {
		cd->xylonfb_irq = 0;
		goto err_fb;
	}

#if defined(__LITTLE_ENDIAN)
	cd->xylonfb_flags |= XYLONFB_FLAG_MEMORY_LE;
#endif
	mutex_init(&cd->irq_mutex);
	init_waitqueue_head(&cd->vsync.wait);
	cd->xylonfb_use_ref = 0;

	dev_set_drvdata(dev, (void *)afbi);

	cd->xylonfb_flags &= ~(XYLONFB_FLAG_VMODE_INIT |
		XYLONFB_FLAG_DEFAULT_VMODE_SET | XYLONFB_FLAG_VMODE_SET);
	xylonfb_mode_option = NULL;

	/* start HW */
	xylonfb_start(afbi, layers);

	return 0;

err_fb:
	if (cd->xylonfb_irq != 0)
		free_irq(cd->xylonfb_irq, dev);
	for (i = layers-1; i >= 0; i--) {
		fbi = afbi[i];
		if (!fbi)
			continue;
		ld = fbi->par;
		if (regfb[i] == 0)
			unregister_framebuffer(fbi);
		else
			regfb[i] = 0;
		if (fbi->cmap.red)
			fb_dealloc_cmap(&fbi->cmap);
		if (ld) {
			if (cd->xylonfb_flags & XYLONFB_FLAG_DMA_BUFFER) {
				/* NOT USED FOR NOW! */
				dma_free_coherent(dev,
					PAGE_ALIGN(fbi->fix.smem_len),
					ld->fb_virt, ld->fb_phys);
			} else {
				if (ld->fb_virt)
					iounmap(ld->fb_virt);
			}
			kfree(ld->layer_reg_list);
			kfree(fbi->pseudo_palette);
			framebuffer_release(fbi);
		}
	}
	if (reg_base_virt)
		iounmap(reg_base_virt);

err_mem:
	if (cd) {
		kfree(cd->reg_list);
#if defined(CONFIG_FB_XYLON_MISC)
		kfree(cd->xylonfb_misc);
#endif
		kfree(cd);
	}
	kfree(afbi);

	dev_set_drvdata(dev, NULL);

	return rc;
}

int xylonfb_deinit_driver(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fb_info **afbi = dev_get_drvdata(dev);
	struct fb_info *fbi = afbi[0];
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	void *reg_base_virt = NULL;
	int i;
	bool logicvc_unmap = false;

	driver_devel("%s\n", __func__);

	if (cd->xylonfb_use_ref) {
		pr_err("Error xylonfb in use\n");
		return -EINVAL;
	}

	xylonfb_disable_logicvc_output(fbi);

#if defined(CONFIG_FB_XYLON_MISC)
	xylonfb_misc_deinit(fbi);
	kfree(cd->xylonfb_misc);
#endif

	free_irq(cd->xylonfb_irq, dev);
	for (i = cd->xylonfb_layers-1; i >= 0; i--) {
		fbi = afbi[i];
		ld = fbi->par;

		if (!logicvc_unmap) {
			reg_base_virt = ld->reg_base_virt;
			logicvc_unmap = true;
		}
		unregister_framebuffer(fbi);
		fb_dealloc_cmap(&fbi->cmap);
		if (cd->xylonfb_flags & XYLONFB_FLAG_DMA_BUFFER) {
			dma_free_coherent(dev, PAGE_ALIGN(fbi->fix.smem_len),
				ld->fb_virt, ld->fb_phys);
		} else {
			iounmap(ld->fb_virt);
		}
		if (!(cd->xylonfb_flags & LOGICVC_READABLE_REGS))
			kfree(ld->layer_reg_list);
		kfree(fbi->pseudo_palette);
		framebuffer_release(fbi);
	}

	if (reg_base_virt)
		iounmap(reg_base_virt);

	if (!(cd->xylonfb_flags & LOGICVC_READABLE_REGS))
		kfree(cd->reg_list);
	kfree(cd);
	kfree(afbi);

	dev_set_drvdata(dev, NULL);

	return 0;
}

#ifndef MODULE
int xylonfb_get_params(char *options)
{
	char *this_opt;

	driver_devel("%s\n", __func__);

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		xylonfb_mode_option = this_opt;
	}
	return 0;
}
#endif

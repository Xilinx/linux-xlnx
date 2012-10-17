/*
 * Xylon logiCVC frame buffer driver core functions
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * This driver was based on skeletonfb.c and other framebuffer video drivers.
 * 2012 Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/*
	Usefull driver information:
	- driver does not support multiple instances of logiCVC-ML
	- logiCVC-ML background layer is recomended
	- platform driver default resolution is set with defines in xylonfb-vmode.h
 */


#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/console.h>
#include "xylonfb.h"


#define XYLONFB_PSEUDO_PALETTE_SZ 256


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
static void xylonfb_start_logicvc(struct fb_info *fbi);
static void xylonfb_stop_logicvc(struct fb_info *fbi);
static void xylonfb_enable_logicvc_layer(struct fb_info *fbi);
static void xylonfb_disable_logicvc_layer(struct fb_info *fbi);


extern int xylonfb_hw_pixclk_set(unsigned long pixclk_khz);
extern bool xylonfb_hw_pixclk_change(void);


static irqreturn_t xylonfb_isr(int irq, void *dev_id)
{
	struct fb_info **afbi = (struct fb_info **)dev_id;
	struct fb_info *fbi = afbi[0];
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;
	u32 isr;

	driver_devel("%s IRQ %d\n", __func__, irq);

	isr = readl(layer_data->reg_base_virt + LOGICVC_INT_ROFF);
	if (isr & LOGICVC_V_SYNC_INT) {
		writel(LOGICVC_V_SYNC_INT,
			layer_data->reg_base_virt + LOGICVC_INT_ROFF);
		common_data->xylonfb_vsync.cnt++;
		wake_up_interruptible(&common_data->xylonfb_vsync.wait);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static int xylonfb_open(struct fb_info *fbi, int user)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	driver_devel("%s\n", __func__);

	if (layer_data->layer_use_ref == 0) {
		/* turn on layer */
		xylonfb_enable_logicvc_layer(fbi);
		/* set layer ON flag */
		layer_data->layer_flags |= LOGICVC_LAYER_ON;
	}
	layer_data->layer_use_ref++;
	layer_data->xylonfb_cd->xylonfb_use_ref++;

	return 0;
}

static int xylonfb_release(struct fb_info *fbi, int user)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	driver_devel("%s\n", __func__);

	layer_data->layer_use_ref--;
	if (layer_data->layer_use_ref == 0) {
		/* turn off layer */
		xylonfb_disable_logicvc_layer(fbi);
		/* set layer OFF flag */
		layer_data->layer_flags &= (~LOGICVC_LAYER_ON);
	}
	layer_data->xylonfb_cd->xylonfb_use_ref--;

	return 0;
}

static int xylonfb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	driver_devel("%s\n", __func__);

	/* HW layer bpp value can not be changed */
	if (var->bits_per_pixel != fbi->var.bits_per_pixel) {
		if (var->bits_per_pixel == 24)
			var->bits_per_pixel = 32;
		else
			return -EINVAL;
	}

	if (var->xres > LOGICVC_MAX_XRES)
		var->xres = LOGICVC_MAX_XRES;
	if (var->yres > LOGICVC_MAX_VRES)
		var->yres = LOGICVC_MAX_VRES;

	if (var->xres_virtual > fbi->var.xres_virtual)
		var->xres_virtual = fbi->var.xres_virtual;
	if (var->yres_virtual > fbi->var.yres_virtual)
		var->yres_virtual = fbi->var.yres_virtual;

	if (fbi->var.xres != 0)
		if ((var->xoffset + fbi->var.xres) >= fbi->var.xres_virtual)
			var->xoffset = fbi->var.xres_virtual - fbi->var.xres - 1;
	if (fbi->var.yres != 0)
		if ((var->yoffset + fbi->var.yres) >= fbi->var.yres_virtual)
			var->yoffset = fbi->var.yres_virtual - fbi->var.yres - 1;

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
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;
	int rc;
	char vmode_opt[20+1];

	driver_devel("%s\n", __func__);

	if ((fbi->var.xres > LOGICVC_MAX_XRES) ||
		(fbi->var.yres > LOGICVC_MAX_VRES)) {
		return -EINVAL;
	}

	if (common_data->xylonfb_flags & FB_VMODE_SET)
		return 0;

	xylonfb_stop_logicvc(fbi);
	xylonfb_logicvc_disp_ctrl(fbi, false);

	if (xylonfb_hw_pixclk_change()) {
		if (!(common_data->xylonfb_flags & FB_VMODE_INIT)) {
			sprintf(vmode_opt, "%dx%dM-%d@%d",
				fbi->var.xres, fbi->var.yres,
				fbi->var.bits_per_pixel,
				common_data->vmode_data_current.fb_vmode.refresh);
			if (!strcmp(common_data->vmode_data.fb_vmode_name, vmode_opt)) {
				common_data->vmode_data_current = common_data->vmode_data;
			} else {
				xylonfb_mode_option = vmode_opt;
				xylonfb_set_timings(fbi, fbi->var.bits_per_pixel);
				xylonfb_mode_option = NULL;
			}
		}

		rc = xylonfb_hw_pixclk_set(
			PICOS2KHZ(common_data->vmode_data_current.fb_vmode.pixclock));
		if (rc) {
			pr_err("Error xylonfb changing pixel clock\n");
			return rc;
		}
	}

	xylonfb_start_logicvc(fbi);
	xylonfb_logicvc_disp_ctrl(fbi, true);

	pr_info("xylonfb video mode: %dx%d-%d@%d\n",
		fbi->var.xres, fbi->var.yres, fbi->var.bits_per_pixel,
		common_data->vmode_data_current.fb_vmode.refresh);

	/* set flag used for finding video mode only once */
	if (common_data->xylonfb_flags & FB_VMODE_INIT)
		common_data->xylonfb_flags |= FB_VMODE_SET;
	/* used only when resolution is changed */
	if (!(common_data->xylonfb_flags & FB_VMODE_SET))
		xylonfb_enable_logicvc_layer(fbi);

	return 0;
}

static int xylonfb_set_color_hw(u16 *transp, u16 *red, u16 *green, u16 *blue,
	int len, int idx, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct layer_fix_data *lfdata = &layer_data->layer_fix;
	u32 pixel;
	int bpp_virt, toff, roff, goff, boff;

	driver_devel("%s\n", __func__);

	bpp_virt = lfdata->bpp_virt;

	toff = fbi->var.transp.offset;
	roff = fbi->var.red.offset;
	goff = fbi->var.green.offset;
	boff = fbi->var.blue.offset;

	if (fbi->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		u32 clut_value;

		if (idx > 255 || len > 256)
			return -EINVAL;

		if (lfdata->alpha_mode == LOGICVC_CLUT_16BPP_ALPHA) {
			if (transp) {
				while (len > 0) {
					clut_value =
						((((transp[idx] & 0xFC) >> 2) << toff) |
						(((red[idx] & 0xF8) >> 3) << roff) |
						(((green[idx] & 0xFC) >> 2) << goff) |
						(((blue[idx] & 0xF8) >> 3) << boff));
					writel(clut_value, layer_data->layer_clut_base_virt +
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
					writel(clut_value, layer_data->layer_clut_base_virt +
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
					writel(clut_value, layer_data->layer_clut_base_virt +
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
					writel(clut_value, layer_data->layer_clut_base_virt +
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
			(u16 *)&transp,
			(u16 *)&red,
			(u16 *)&green,
			(u16 *)&blue,
			1, regno, fbi);
}

static int xylonfb_set_cmap(struct fb_cmap *cmap, struct fb_info *fbi)
{
	driver_devel("%s\n", __func__);

	return
		xylonfb_set_color_hw(cmap->transp, cmap->red, cmap->green, cmap->blue,
			cmap->len, cmap->start, fbi);
}

static void xylonfb_set_pixels(struct fb_info *fbi,
	struct xylonfb_layer_data *layer_data, int bpp, unsigned int pix)
{
	u32* vmem;
	u8 *vmem8;
	u16 *vmem16;
	u32 *vmem32;
	int x, y, pix_off;

	driver_devel("%s\n", __func__);

	vmem = layer_data->fb_virt +
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
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct layer_fix_data *lfdata = &layer_data->layer_fix;
	u32 pix, reg;
	int i;

	driver_devel("%s\n", __func__);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		driver_devel("FB_BLANK_UNBLANK\n");
		reg = readl(layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		reg |= LOGICVC_V_EN_MSK;
		writel(reg, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		mdelay(50);
		break;

	case FB_BLANK_NORMAL:
		driver_devel("FB_BLANK_NORMAL\n");
		switch (lfdata->bpp_virt) {
		case 8:
			switch (lfdata->alpha_mode) {
			case LOGICVC_LAYER_ALPHA:
				xylonfb_set_pixels(fbi, layer_data, 8, 0x00);
				break;
			case LOGICVC_PIXEL_ALPHA:
				xylonfb_set_pixels(fbi, layer_data, 16, 0xFF00);
				break;
			case LOGICVC_CLUT_16BPP_ALPHA:
			case LOGICVC_CLUT_32BPP_ALPHA:
				for (i = 0; i < 256; i++) {
					pix = readl(layer_data->layer_clut_base_virt +
						(i*LOGICVC_CLUT_REGISTER_SIZE));
					pix &= 0x00FFFFFF;
					if (pix == 0)
						break;
				}
				xylonfb_set_pixels(fbi, layer_data, 8, i);
				break;
			}
			break;
		case 16:
			switch (lfdata->alpha_mode) {
			case LOGICVC_LAYER_ALPHA:
				xylonfb_set_pixels(fbi, layer_data, 16, 0x0000);
				break;
			case LOGICVC_PIXEL_ALPHA:
				xylonfb_set_pixels(fbi, layer_data, 32, 0xFF000000);
				break;
			}
			break;
		case 32:
			xylonfb_set_pixels(fbi, layer_data, 32, 0xFF000000);
			break;
		}
		break;

	case FB_BLANK_POWERDOWN:
		driver_devel("FB_BLANK_POWERDOWN\n");
		reg = readl(layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		reg &= ~LOGICVC_V_EN_MSK;
		writel(reg, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
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
	struct xylonfb_layer_data *layer_data = fbi->par;

	driver_devel("%s\n", __func__);

	if (fbi->var.xoffset == var->xoffset && fbi->var.yoffset == var->yoffset)
		return 0;

	/* check for negative values */
	if (var->xoffset < 0)
		var->xoffset += var->xres;
	if (var->yoffset < 0)
		var->yoffset += var->yres;

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset > fbi->var.yres_virtual ||
			var->xoffset) {
			return -EINVAL;
		}
	} else {
		if (var->xoffset + var->xres > fbi->var.xres_virtual ||
			var->yoffset + var->yres > fbi->var.yres_virtual) {
			/* if smaller then physical layer video memory allow panning */
			if ((var->xoffset + var->xres > layer_data->layer_fix.width)
					||
				(var->yoffset + var->yres > layer_data->layer_fix.height)) {
				return -EINVAL;
			}
		}
	}
	fbi->var.xoffset = var->xoffset;
	fbi->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		fbi->var.vmode |= FB_VMODE_YWRAP;
	else
		fbi->var.vmode &= ~FB_VMODE_YWRAP;
	/* set layer memory X offset */
	writel(var->xoffset,
		(layer_data->layer_reg_base_virt + LOGICVC_LAYER_HOR_OFF_ROFF));
	/* set layer memory Y offset */
	writel(var->yoffset,
		(layer_data->layer_reg_base_virt + LOGICVC_LAYER_VER_OFF_ROFF));
	/* apply changes in logiCVC */
	writel((var->yres-1),
		(layer_data->layer_reg_base_virt + LOGICVC_LAYER_VER_POS_ROFF));

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


static int xylonfb_find_next_layer(struct layer_fix_data *lfdata,
	int layers, int curr)
{
	u32 address, temp_address, loop_address;
	int i, next;

	driver_devel("%s\n", __func__);

	address = lfdata[curr].offset * lfdata[curr].width * lfdata[curr].bpp;
	temp_address = 0xFFFFFFFF;
	next = -1;

	for (i = 0; i < layers; i++) {
		loop_address = lfdata[i].offset * lfdata[i].width * lfdata[i].bpp;
		if (address < loop_address
				&&
			loop_address < temp_address) {
			next = i;
			temp_address = loop_address;
		}
	}

	return next;
}

static void xylonfb_set_yvirt(struct layer_fix_data *lfdata,
	unsigned long vmem_base_addr, unsigned long vmem_high_addr,
	int layers, int curr)
{
	int next;

	driver_devel("%s\n", __func__);

	next = xylonfb_find_next_layer(lfdata, layers, curr);

	if (next != -1) {
		lfdata[curr].height =
			((lfdata[next].width * (lfdata[next].bpp/8) *
			lfdata[next].offset)
				-
			(lfdata[curr].width * (lfdata[curr].bpp/8) *
			lfdata[curr].offset)) /
			(lfdata[curr].width * (lfdata[curr].bpp/8));
	} else { /* last physical logiCVC layer */
		/* FIXME - this is fixed for 1920x1080 tripple buffering,
			but it should be read from somewhere */
		lfdata[curr].height = 3240;
		while (1) {
			if (((lfdata[curr].width * (lfdata[curr].bpp/8) *
				lfdata[curr].height)
					+
				(lfdata[curr].width * (lfdata[curr].bpp/8) *
				lfdata[curr].offset))
					<=
				(vmem_high_addr - vmem_base_addr))
				break;
			lfdata[curr].height -= 64; /* FIXME - magic number? */
		}
	}

	lfdata[curr].layer_fix_info |=
		((lfdata[curr].height / lfdata[curr].buffer_offset) << 4);
}

static int xylonfb_map(int id, int layers, struct device *dev,
	struct xylonfb_layer_data *layer_data,
	unsigned long vmem_base_addr, u32 reg_base_phys, void *reg_base_virt)
{
	struct layer_fix_data *lfdata = &layer_data->layer_fix;

	driver_devel("%s\n", __func__);

	/* logiCVC register mapping */
	layer_data->reg_base_phys = reg_base_phys;
	layer_data->reg_base_virt = reg_base_virt;
	/* Video memory mapping */
	layer_data->fb_phys = vmem_base_addr +
		(lfdata->width * (lfdata->bpp/8) * lfdata->offset);
	layer_data->fb_size =
		lfdata->width * (lfdata->bpp/8) * lfdata->height;

	if (layer_data->xylonfb_cd->xylonfb_flags & FB_DMA_BUFFER) {
		/* NOT USED FOR NOW! */
		layer_data->fb_virt = dma_alloc_writecombine(dev,
			PAGE_ALIGN(layer_data->fb_size),
			&layer_data->fb_phys, GFP_KERNEL);
	} else {
		layer_data->fb_virt =
			ioremap_wc(layer_data->fb_phys, layer_data->fb_size);
	}
	/* check memory mappings */
	if (!layer_data->reg_base_virt || !layer_data->fb_virt) {
		pr_err("Error xylonfb ioremap REGS 0x%X FB 0x%X\n",
			(unsigned int)layer_data->reg_base_virt,
			(unsigned int)layer_data->fb_virt);
		return -ENOMEM;
	}
	//memset_io((void __iomem *)layer_data->fb_virt, 0, layer_data->fb_size);
	layer_data->layer_reg_base_virt =
		layer_data->reg_base_virt + logicvc_layer_reg_offset[id];
	layer_data->layer_clut_base_virt =
		layer_data->reg_base_virt +
		logicvc_clut_reg_offset[id*LOGICVC_CLUT_0_INDEX_OFFSET];
	layer_data->layer_use_ref = 0;
	layer_data->layer_flags = 0;

	return 0;
}

static void xylonfb_set_fbi_timings(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;
	struct fb_var_screeninfo *var = &fbi->var;

	driver_devel("%s\n", __func__);

	var->xres = common_data->vmode_data_current.fb_vmode.xres;
	var->yres = common_data->vmode_data_current.fb_vmode.yres;
	var->pixclock = common_data->vmode_data_current.fb_vmode.pixclock;
	var->left_margin = common_data->vmode_data_current.fb_vmode.left_margin;
	var->right_margin = common_data->vmode_data_current.fb_vmode.right_margin;
	var->upper_margin = common_data->vmode_data_current.fb_vmode.upper_margin;
	var->lower_margin = common_data->vmode_data_current.fb_vmode.lower_margin;
	var->hsync_len = common_data->vmode_data_current.fb_vmode.hsync_len;
	var->vsync_len = common_data->vmode_data_current.fb_vmode.vsync_len;
	var->sync = common_data->vmode_data_current.fb_vmode.sync;
	var->vmode = common_data->vmode_data_current.fb_vmode.vmode;
}

static void xylonfb_set_hw_specifics(struct fb_info *fbi,
	struct xylonfb_layer_data *layer_data, struct layer_fix_data *lfdata,
	u32 reg_base_phys)
{
	driver_devel("%s\n", __func__);

	fbi->fix.smem_start = layer_data->fb_phys;
	fbi->fix.smem_len = layer_data->fb_size;
	fbi->fix.type = FB_TYPE_PACKED_PIXELS;
	if ((lfdata->bpp == 8) &&
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
			  True color, RGB triplet or ARGB quadriplet is stored in 32 bits.
		*/
		fbi->fix.visual = FB_VISUAL_TRUECOLOR;
	}
	/* sanity check */
	if ((lfdata->bpp != 8) &&
		((lfdata->alpha_mode == LOGICVC_CLUT_16BPP_ALPHA) ||
		(lfdata->alpha_mode == LOGICVC_CLUT_32BPP_ALPHA))) {
		pr_warning("xylonfb invalid layer alpha!\n");
		lfdata->alpha_mode = LOGICVC_LAYER_ALPHA;
	}

	fbi->fix.xpanstep = 1;
	fbi->fix.ypanstep = 1;
	fbi->fix.ywrapstep = LOGICVC_MAX_VRES;
	fbi->fix.line_length = lfdata->width * (lfdata->bpp/8);
	fbi->fix.mmio_start = reg_base_phys;
	fbi->fix.mmio_len = LOGICVC_REGISTERS_RANGE;
	fbi->fix.accel = FB_ACCEL_NONE;

	fbi->var.xres_virtual = lfdata->width;
	if (lfdata->height <= LOGICVC_MAX_VRES)
		fbi->var.yres_virtual = lfdata->height;
	else
		fbi->var.yres_virtual = LOGICVC_MAX_VRES;
	fbi->var.bits_per_pixel = lfdata->bpp;

	/*	Set values according to logiCVC layer data width configuration:
		- layer data width can be 1, 2, 4 bytes
		- layer data width for 16 bpp can be 2 or 4 bytes */
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
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;
	struct fb_var_screeninfo fb_var;
	int rc;

	driver_devel("%s\n", __func__);

	if ((common_data->xylonfb_flags & FB_VMODE_INIT) &&
		memchr(common_data->vmode_data.fb_vmode_name, 'x', 10)) {
		common_data->vmode_data_current = common_data->vmode_data;
		return 0;
	}

	rc = fb_find_mode(&fb_var, fbi, xylonfb_mode_option, NULL, 0,
		&xylonfb_vmode.fb_vmode, bpp);
#ifdef DEBUG
	switch (rc) {
	case 0:
		pr_err("Error xylonfb video mode\n"
			"using driver default mode %dx%d-%d@%d\n",
			xylonfb_vmode.fb_vmode.xres, xylonfb_vmode.fb_vmode.yres,
			bpp, xylonfb_vmode.fb_vmode.refresh);
		break;
	case 1:
		driver_devel("xylonfb video mode %s\n", xylonfb_mode_option);
		break;
	case 2:
		pr_notice("xylonfb video mode %s with ignored refresh rate\n",
			xylonfb_mode_option);
		break;
	case 3:
		pr_notice("xylonfb default video mode %dx%d-%d@%d\n",
			xylonfb_vmode.fb_vmode.xres,
			xylonfb_vmode.fb_vmode.yres,
			bpp, xylonfb_vmode.fb_vmode.refresh);
		break;
	case 4:
		pr_notice("xylonfb video mode fallback\n");
		break;
	}
#endif

	common_data->vmode_data_current.ctrl_reg =
		common_data->vmode_data.ctrl_reg;
	common_data->vmode_data_current.fb_vmode.refresh =
		common_data->vmode_data.fb_vmode.refresh;
	sprintf(common_data->vmode_data_current.fb_vmode_name,
		"%dx%dM-%d@%d",
		fb_var.xres, fb_var.yres, fb_var.bits_per_pixel,
		common_data->vmode_data_current.fb_vmode.refresh);
	common_data->vmode_data_current.fb_vmode.xres = fb_var.xres;
	common_data->vmode_data_current.fb_vmode.yres = fb_var.yres;
	common_data->vmode_data_current.fb_vmode.pixclock = fb_var.pixclock;
	common_data->vmode_data_current.fb_vmode.left_margin = fb_var.left_margin;
	common_data->vmode_data_current.fb_vmode.right_margin = fb_var.right_margin;
	common_data->vmode_data_current.fb_vmode.upper_margin = fb_var.upper_margin;
	common_data->vmode_data_current.fb_vmode.lower_margin = fb_var.lower_margin;
	common_data->vmode_data_current.fb_vmode.hsync_len = fb_var.hsync_len;
	common_data->vmode_data_current.fb_vmode.vsync_len = fb_var.vsync_len;
	common_data->vmode_data_current.fb_vmode.sync = fb_var.sync;
	common_data->vmode_data_current.fb_vmode.vmode = fb_var.vmode;

	if (!memchr(common_data->vmode_data.fb_vmode_name, 'x', 10))
		common_data->vmode_data = common_data->vmode_data_current;

	return rc;
}

static int xylonfb_register_fb(struct fb_info *fbi,
	struct xylonfb_layer_data *layer_data,
	u32 reg_base_phys, int id, int *regfb)
{
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;
	struct layer_fix_data *lfdata = &layer_data->layer_fix;
	int alpha;

	driver_devel("%s\n", __func__);

	fbi->flags = FBINFO_DEFAULT;
	fbi->screen_base = (char __iomem *)layer_data->fb_virt;
	fbi->screen_size = layer_data->fb_size;
	fbi->pseudo_palette = kzalloc(sizeof(u32) * XYLONFB_PSEUDO_PALETTE_SZ,
		GFP_KERNEL);
	fbi->fbops = &xylonfb_ops;

	sprintf(fbi->fix.id, "Xylon FB%d", id);
	xylonfb_set_hw_specifics(fbi, layer_data, lfdata, reg_base_phys);
	if (!(common_data->xylonfb_flags & FB_DEFAULT_VMODE_SET)) {
		xylonfb_set_timings(fbi, fbi->var.bits_per_pixel);
		common_data->xylonfb_flags |= FB_DEFAULT_VMODE_SET;
	}
	xylonfb_set_fbi_timings(fbi);

	if (lfdata->alpha_mode == LOGICVC_LAYER_ALPHA)
		alpha = 0;
	else
		alpha = 1;
	if (fb_alloc_cmap(&fbi->cmap, 256, alpha))
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

static void xylonfb_init_layer_regs(struct xylonfb_layer_data *layer_data)
{
	u32 reg_val;

	switch (layer_data->layer_fix.bpp_virt) {
	case 8:
		switch (layer_data->layer_fix.alpha_mode) {
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
	writel(reg_val,
		(layer_data->layer_reg_base_virt + LOGICVC_LAYER_TRANSP_ROFF));

	reg_val = layer_data->layer_ctrl;
	writel(reg_val,
		(layer_data->layer_reg_base_virt + LOGICVC_LAYER_CTRL_ROFF));
}

static void xylonfb_logicvc_disp_ctrl(struct fb_info *fbi, bool enable)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;
	u32 val;

	driver_devel("%s\n", __func__);

	if (enable) {
		val = LOGICVC_EN_VDD_MSK;
		writel(val, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		mdelay(common_data->power_on_delay);
		val |= LOGICVC_V_EN_MSK;
		writel(val, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
		mdelay(common_data->signal_on_delay);
		val |= LOGICVC_EN_BLIGHT_MSK;
		writel(val, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
	} else {
		writel(0, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
	}
}

static void xylonfb_enable_logicvc_layer(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	u32 reg;

	driver_devel("%s\n", __func__);

	reg = readl(layer_data->layer_reg_base_virt + LOGICVC_LAYER_CTRL_ROFF);
	reg |= 0x01;
	writel(reg, (layer_data->layer_reg_base_virt + LOGICVC_LAYER_CTRL_ROFF));
}

static void xylonfb_disable_logicvc_layer(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	u32 reg;

	driver_devel("%s\n", __func__);

	reg = readl(layer_data->layer_reg_base_virt + LOGICVC_LAYER_CTRL_ROFF);
	reg &= ~0x01;
	writel(reg, (layer_data->layer_reg_base_virt + LOGICVC_LAYER_CTRL_ROFF));
}

static void xylonfb_start_logicvc(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;

	driver_devel("%s\n", __func__);

	writel(common_data->vmode_data_current.fb_vmode.right_margin-1,
		layer_data->reg_base_virt + LOGICVC_SHSY_FP_ROFF);
	writel(common_data->vmode_data_current.fb_vmode.hsync_len-1,
		layer_data->reg_base_virt + LOGICVC_SHSY_ROFF);
	writel(common_data->vmode_data_current.fb_vmode.left_margin-1,
		layer_data->reg_base_virt + LOGICVC_SHSY_BP_ROFF);
	writel(common_data->vmode_data_current.fb_vmode.xres-1,
		layer_data->reg_base_virt + LOGICVC_SHSY_RES_ROFF);
	writel(common_data->vmode_data_current.fb_vmode.lower_margin-1,
		layer_data->reg_base_virt + LOGICVC_SVSY_FP_ROFF);
	writel(common_data->vmode_data_current.fb_vmode.vsync_len-1,
		layer_data->reg_base_virt + LOGICVC_SVSY_ROFF);
	writel(common_data->vmode_data_current.fb_vmode.upper_margin-1,
		layer_data->reg_base_virt + LOGICVC_SVSY_BP_ROFF);
	writel(common_data->vmode_data_current.fb_vmode.yres-1,
		layer_data->reg_base_virt + LOGICVC_SVSY_RES_ROFF);
	writel(common_data->vmode_data_current.ctrl_reg,
		layer_data->reg_base_virt + LOGICVC_SCTRL_ROFF);
	writel(SD_REG_INIT, layer_data->reg_base_virt + LOGICVC_SDTYPE_ROFF);

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
		common_data->vmode_data_current.fb_vmode.right_margin,
		common_data->vmode_data_current.fb_vmode.hsync_len,
		common_data->vmode_data_current.fb_vmode.left_margin,
		common_data->vmode_data_current.fb_vmode.lower_margin,
		common_data->vmode_data_current.fb_vmode.vsync_len,
		common_data->vmode_data_current.fb_vmode.upper_margin,
		common_data->vmode_data_current.fb_vmode.pixclock,
		common_data->vmode_data_current.fb_vmode.xres,
		common_data->vmode_data_current.fb_vmode.yres);
}

static void xylonfb_stop_logicvc(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;
	struct fb_info **afbi =
		(struct fb_info **)dev_get_drvdata(common_data->dev);
	int i;

	driver_devel("%s\n", __func__);

	if (afbi) {
		for (i = 0; i < common_data->layers; i++)
			xylonfb_disable_logicvc_layer(afbi[i]);
	}
}

static void xylonfb_start(struct fb_info **afbi, int layers)
{
	struct xylonfb_layer_data *layer_data;
	int i;

	driver_devel("%s\n", __func__);

	/* turn OFF all layers except already used ones */
	for (i = 0; i < layers; i++) {
		layer_data = (struct xylonfb_layer_data *)afbi[i]->par;
		if (layer_data->layer_flags & LOGICVC_LAYER_ON)
			continue;
		/* turn off layer */
		xylonfb_disable_logicvc_layer(afbi[i]);
	}
	/* print layer parameters */
	for (i = 0; i < layers; i++) {
		layer_data = (struct xylonfb_layer_data *)afbi[i]->par;
		driver_devel("logiCVC layer %d\n" \
			"    Registers Base Address:     0x%X\n" \
			"    Layer Video Memory Address: 0x%X\n" \
			"    X resolution:               %d\n" \
			"    Y resolution:               %d\n" \
			"    X resolution (virtual):     %d\n" \
			"    Y resolution (virtual):     %d\n" \
			"    Line length (bytes):        %d\n" \
			"    Bits per Pixel:             %d\n" \
			"\n", \
			i,
			(unsigned int)layer_data->reg_base_phys,
			(unsigned int)layer_data->fb_phys,
			afbi[i]->var.xres,
			afbi[i]->var.yres,
			afbi[i]->var.xres_virtual,
			afbi[i]->var.yres_virtual,
			afbi[i]->fix.line_length,
			afbi[i]->var.bits_per_pixel);
	}
}

int xylonfb_init_driver(struct xylonfb_init_data *init_data)
{
	struct device *dev;
	struct fb_info **afbi;
	struct fb_info *fbi;
	struct xylonfb_common_data *common_data;
	struct xylonfb_layer_data *layer_data;
	struct resource *reg_res, *irq_res;
	void *reg_base_virt;
	u32 reg_base_phys;
	int reg_range, layers, active_layer;
	int i, rc;
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
	active_layer = init_data->active_layer;
	if (active_layer >= layers) {
		pr_err("Error xylonfb default layer (set 0)\n");
		active_layer = 0;
	}

	afbi = kzalloc(sizeof(struct fb_info *) * layers, GFP_KERNEL);
	common_data = kzalloc(sizeof(struct xylonfb_common_data), GFP_KERNEL);
	if (!afbi || !common_data) {
		pr_err("Error xylonfb allocating internal data\n");
		rc = -ENOMEM;
		goto err_mem;
	}

	common_data->layers = layers;
	common_data->xylonfb_flags |= FB_VMODE_INIT;

	sprintf(init_data->vmode_data.fb_vmode_name, "%sM-%d@%d",
		init_data->vmode_data.fb_vmode_name,
		init_data->lfdata[active_layer].bpp,
		init_data->vmode_data.fb_vmode.refresh);
	if (init_data->vmode_params_set) {
		common_data->vmode_data = init_data->vmode_data;
	} else {
		xylonfb_mode_option = init_data->vmode_data.fb_vmode_name;
		common_data->vmode_data.ctrl_reg = init_data->vmode_data.ctrl_reg;
		common_data->vmode_data.fb_vmode.refresh =
			init_data->vmode_data.fb_vmode.refresh;
	}

	layer_data = NULL;

	reg_base_phys = reg_res->start;
	reg_range = reg_res->end - reg_res->start;
	reg_base_virt = ioremap_nocache(reg_base_phys, reg_range);

	/* load layer parameters for all layers */
	for (i = 0; i < layers; i++)
		regfb[i] = -1;

	/* make /dev/fb0 to be default active layer
	   no matter how hw layers are organized */
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
		layer_data = fbi->par;
		layer_data->xylonfb_cd = common_data;

		xylonfb_set_yvirt(init_data->lfdata,
			init_data->vmem_base_addr, init_data->vmem_high_addr, layers, i);

		layer_data->layer_fix = init_data->lfdata[i];

		rc = xylonfb_map(i, layers, dev, layer_data,
			init_data->vmem_base_addr, reg_base_phys, reg_base_virt);
		if (rc)
			goto err_fb;

		layer_data->layer_ctrl = init_data->layer_ctrl[i];
		xylonfb_init_layer_regs(layer_data);

		rc = xylonfb_register_fb(fbi, layer_data, reg_base_phys, i, &regfb[i]);
		if (rc)
			goto err_fb;

		mutex_init(&layer_data->layer_mutex);

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
			(layer_data->layer_fix.layer_fix_info & 0x0F),
			layer_data->layer_fix.offset,
			layer_data->layer_fix.buffer_offset,
			(layer_data->layer_fix.layer_fix_info >> 4),
			layer_data->layer_fix.width,
			layer_data->layer_fix.height,
			layer_data->layer_fix.bpp,
			layer_data->layer_fix.bpp_virt,
			layer_data->fb_size);
	}

	common_data->bg_layer_bpp = init_data->bg_layer_bpp;
	common_data->bg_layer_alpha_mode = init_data->bg_layer_alpha_mode;
	driver_devel("BG layer %dbpp\n", init_data->bg_layer_bpp);

	common_data->xylonfb_irq = irq_res->start;
	rc = request_irq(common_data->xylonfb_irq, xylonfb_isr,
			IRQF_TRIGGER_HIGH, DEVICE_NAME, afbi);
	if (rc) {
		common_data->xylonfb_irq = 0;
		goto err_fb;
	}

#if defined(__LITTLE_ENDIAN)
	common_data->xylonfb_flags |= FB_MEMORY_LE;
#endif
	mutex_init(&common_data->irq_mutex);
	init_waitqueue_head(&common_data->xylonfb_vsync.wait);
	common_data->xylonfb_use_ref = 0;

	common_data->xylonfb_flags &=
		~(FB_VMODE_INIT | FB_DEFAULT_VMODE_SET | FB_VMODE_SET);
	xylonfb_mode_option = NULL;

	common_data->dev = dev;
	dev_set_drvdata(dev, (void *)afbi);

	/* start HW */
	xylonfb_start(afbi, layers);

	return 0;

err_fb:
	if (common_data->xylonfb_irq != 0)
		free_irq(common_data->xylonfb_irq, afbi);
	if (layer_data->reg_base_virt)
		iounmap(layer_data->reg_base_virt);
	for (i = layers-1; i >= 0; i--) {
		fbi = afbi[i];
		if (!fbi)
			continue;
		layer_data = fbi->par;
		if (regfb[i] == 0)
			unregister_framebuffer(fbi);
		else
			regfb[i] = 0;
		if (fbi->cmap.red)
			fb_dealloc_cmap(&fbi->cmap);
		if (layer_data) {
			if (common_data->xylonfb_flags & FB_DMA_BUFFER) {
				/* NOT USED FOR NOW! */
				dma_free_coherent(dev, PAGE_ALIGN(fbi->fix.smem_len),
					layer_data->fb_virt, layer_data->fb_phys);
			} else {
				if (layer_data->fb_virt)
					iounmap(layer_data->fb_virt);
			}
			kfree(fbi->pseudo_palette);
			framebuffer_release(fbi);
		}
	}

err_mem:
	if (common_data)
		kfree(common_data);
	if (afbi)
		kfree(afbi);

	dev_set_drvdata(dev, NULL);

	return rc;
}

int xylonfb_deinit_driver(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fb_info **afbi = (struct fb_info **)dev_get_drvdata(dev);
	struct fb_info *fbi = afbi[0];
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data = layer_data->xylonfb_cd;
	int i;
	bool logicvc_unmap;

	driver_devel("%s\n", __func__);

	if (common_data->xylonfb_use_ref) {
		pr_err("Error xylonfb in use\n");
		return -EINVAL;
	}

	logicvc_unmap = false;

	free_irq(common_data->xylonfb_irq, afbi);
	for (i = common_data->layers-1; i >= 0; i--) {
		fbi = afbi[i];
		layer_data = fbi->par;
		xylonfb_disable_logicvc_layer(fbi);
		if (!logicvc_unmap) {
			iounmap(layer_data->reg_base_virt);
			logicvc_unmap = true;
		}
		unregister_framebuffer(fbi);
		fb_dealloc_cmap(&fbi->cmap);
		if (common_data->xylonfb_flags & FB_DMA_BUFFER) {
			dma_free_coherent(dev, PAGE_ALIGN(fbi->fix.smem_len),
				layer_data->fb_virt, layer_data->fb_phys);
		} else {
			iounmap(layer_data->fb_virt);
		}
		kfree(fbi->pseudo_palette);
		framebuffer_release(fbi);
	}

	kfree(common_data);
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

/*
 * Xylon logiCVC frame buffer driver
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * This driver was based on skeletonfb.c and other fb video drivers.
 * 2012 Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/*
	Driver information:
	logiCVC must have background layer for proper functioning of screen
	blanking functions.
	logiCVC layers should be in order such that layer index and video memory
	addresses increases. e.g. L0 VRAM addr 0x1000, L1 VRAM addr 0x2000, etc...
 */

#include <asm/io.h>
#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/xylonfb.h>
#include "logicvc.h"
#include "xylonfb-data.h"
#include "xylonfb-vmode.h"
#include "xylonfb-pixclk.h"

#if defined (CONFIG_OF) && !defined (CONFIG_FB_XYLON_PLATFORM)
#define FB_XYLON_CONFIG_OF
#endif


#define dbg(...) //printk(KERN_INFO __VA_ARGS__)

#define DRIVER_NAME "xylonfb"
#define PLATFORM_DRIVER_NAME "logicvc"
#define DRIVER_DESCRIPTION "Xylon logiCVC frame buffer driver"

#define XYLONFB_IOC_MAGIC   'x'
#define XYLONFB_IOC_GETVRAM _IO(XYLONFB_IOC_MAGIC, 0)

#define XYLONFB_PSEUDO_PALETTE_SZ 256

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

static struct fb_videomode drv_vmode;
static char *mode_option __devinitdata;

/* Function declarations */
static inline void xylonfb_set_fbi_timings(struct fb_var_screeninfo *var);
static int xylonfb_set_timings(struct fb_info *fbi, int bpp, bool change_res);
static void xylonfb_start_logicvc(struct fb_info *fbi);
static void xylonfb_stop_logicvc(struct fb_info *fbi);

extern struct xylonfb_vmode_params xylonfb_vmode;


static int xylonfb_set_pixelclock(struct fb_info *fbi)
{
	dbg("%s\n", __func__);

	return pixclk_set(fbi);
}

static irqreturn_t xylonfb_isr(int irq, void *dev_id)
{
	struct fb_info **afbi = (struct fb_info **)dev_id;
	struct fb_info *fbi = afbi[0];
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data =
		layer_data->xylonfb_cd;
	u32 isr;

	dbg("%s\n", __func__);

	isr = readl(layer_data->reg_base_virt + LOGICVC_INT_ROFF);
	if (isr & LOGICVC_V_SYNC_INT) {
		writel(LOGICVC_V_SYNC_INT,
			layer_data->reg_base_virt + LOGICVC_INT_ROFF);
		common_data->xylonfb_vsync.cnt++;
		wake_up_interruptible(&common_data->xylonfb_vsync.wait);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
}

static int xylonfb_open(struct fb_info *fbi, int user)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	dbg("%s\n", __func__);

	spin_lock(&layer_data->layer_lock);

	if (layer_data->layer_use_ref == 0) {
		/* turn on layer */
		writel(1, (layer_data->layer_reg_base_virt + LOGICVC_LAYER_CTRL_ROFF));
		/* set layer ON flag */
		layer_data->layer_info |= LOGICVC_LAYER_ON;
	}
	layer_data->layer_use_ref++;
	layer_data->xylonfb_cd->xylonfb_use_ref++;

	spin_unlock(&layer_data->layer_lock);

	return 0;
}

static int xylonfb_release(struct fb_info *fbi, int user)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	dbg("%s\n", __func__);

	spin_lock(&layer_data->layer_lock);

	layer_data->layer_use_ref--;
	if (layer_data->layer_use_ref == 0) {
		/* turn off layer */
		writel(0, (layer_data->layer_reg_base_virt + LOGICVC_LAYER_CTRL_ROFF));
		/* set layer OFF flag */
		layer_data->layer_info &= (~LOGICVC_LAYER_ON);
	}
	layer_data->xylonfb_cd->xylonfb_use_ref--;

	spin_unlock(&layer_data->layer_lock);

	return 0;
}

static int xylonfb_check_var(struct fb_var_screeninfo *var,
	struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	int ret;
	bool denied = 0;
	char vmode_opt[20+1];

	dbg("%s\n", __func__);

	/* HW layer bpp value can not be changed */
	if (var->bits_per_pixel != fbi->var.bits_per_pixel) {
		if (var->bits_per_pixel == 24)
			var->bits_per_pixel = 32;
		else
			return -EINVAL;
	}

	if ((var->xres != fbi->var.xres) || (var->yres != fbi->var.yres)) {
		sprintf(vmode_opt, "%dx%dM-%d@60",
			var->xres, var->yres, var->bits_per_pixel);
		mode_option = vmode_opt;
		printk(KERN_INFO "xylonfb requested new video mode %s\n", mode_option);
		ret = xylonfb_set_timings(fbi, var->bits_per_pixel, RES_CHANGE_ALLOWED);
		if (ret == 1 || ret == 2)
			layer_data->xylonfb_cd->xylonfb_flags |= FB_CHANGE_RES;
		else
			denied = 1;
		mode_option = NULL;
	}

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
	var->activate = fbi->var.activate;
	var->height = fbi->var.height;
	var->width = fbi->var.width;
	var->sync = fbi->var.sync;
	var->rotate = fbi->var.rotate;

	if (denied) {
		printk(KERN_ERR "Error xylonfb res change not allowed\n");
		return -EPERM;
	}

	return 0;
}

static int xylonfb_set_par(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data =
		layer_data->xylonfb_cd;

	dbg("%s\n", __func__);

	if (common_data->xylonfb_flags & FB_CHANGE_RES) {
		xylonfb_set_fbi_timings(&fbi->var);
		xylonfb_stop_logicvc(fbi);
		if (xylonfb_set_pixelclock(fbi)) {
			printk(KERN_ERR "Error xylonfb changing pixel clock\n");
			return -EACCES;
		}
		xylonfb_start_logicvc(fbi);
		common_data->xylonfb_flags &= (~FB_CHANGE_RES);
		printk(KERN_INFO
			"xylonfb new video mode: %dx%d-%dbpp@60\n",
			fbi->var.xres, fbi->var.yres, fbi->var.bits_per_pixel);
	}

	return 0;
}

static int xylonfb_set_color_hw(u16 *transp, u16 *red, u16 *green, u16 *blue,
	int len, int idx, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct layer_fix_data *lfdata = &layer_data->layer_fix;
	u32 pixel;
	int bpp_virt, toff, roff, goff, boff;

	dbg("%s\n", __func__);

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
	} else
		return -EINVAL;

	return 0;
}

static int xylonfb_set_color_reg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *fbi)
{
	dbg("%s\n", __func__);

	return xylonfb_set_color_hw(
			(u16 *)&transp,
			(u16 *)&red,
			(u16 *)&green,
			(u16 *)&blue,
			1, regno, fbi);
}

static int xylonfb_set_cmap(struct fb_cmap *cmap, struct fb_info *fbi)
{
	dbg("%s\n", __func__);

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

	dbg("%s\n", __func__);

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

	dbg("%s\n", __func__);

	switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			dbg("FB_BLANK_UNBLANK\n");
			reg = readl(layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
			reg |= LOGICVC_V_EN_MSK;
			writel(reg, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
			mdelay(50);
			break;

		case FB_BLANK_NORMAL:
			dbg("FB_BLANK_NORMAL\n");
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
			dbg("FB_BLANK_POWERDOWN\n");
			reg = readl(layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
			reg &= ~LOGICVC_V_EN_MSK;
			writel(reg, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
			mdelay(50);
			break;

		case FB_BLANK_VSYNC_SUSPEND:
		case FB_BLANK_HSYNC_SUSPEND:
		default:
			dbg("FB_BLANK_ not supported!\n");
			return -EINVAL;
	}

	return 0;
}

static int xylonfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	dbg("%s\n", __func__);

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
	/* set HW memory X offset */
	writel(var->xoffset, (layer_data->layer_reg_base_virt + LOGICVC_LAYER_HOR_OFF_ROFF));
	/* set HW memory Y offset */
	writel(var->yoffset, (layer_data->layer_reg_base_virt + LOGICVC_LAYER_VER_OFF_ROFF));
	/* Apply changes */
	writel((var->yres-1), (layer_data->layer_reg_base_virt + LOGICVC_LAYER_VER_POS_ROFF));

	return 0;
}

static int xylonfb_get_vblank(struct fb_vblank *vblank, struct fb_info *fbi)
{
	dbg("%s\n", __func__);

	vblank->flags |= FB_VBLANK_HAVE_VSYNC;

	return 0;
}

static int xylonfb_wait_for_vsync(u32 crt, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data =
		layer_data->xylonfb_cd;
	u32 imr;
	int ret, cnt;

	dbg("%s\n", __func__);

	mutex_lock(&common_data->irq_mutex);

	cnt = common_data->xylonfb_vsync.cnt;

	/* prepare LOGICVC V-sync interrupt */
	imr = readl(layer_data->reg_base_virt + LOGICVC_INT_MASK_ROFF);
	imr &= (~LOGICVC_V_SYNC_INT);
	/* clear LOGICVC V-sync interrupt */
	writel(LOGICVC_V_SYNC_INT, layer_data->reg_base_virt + LOGICVC_INT_ROFF);
	/* enable LOGICVC V-sync interrupt */
	writel(imr, layer_data->reg_base_virt + LOGICVC_INT_MASK_ROFF);

	ret = wait_event_interruptible_timeout(
			common_data->xylonfb_vsync.wait,
			(cnt != common_data->xylonfb_vsync.cnt), HZ/10);

	/* disable LOGICVC V-sync interrupt */
	imr |= LOGICVC_V_SYNC_INT;
	writel(imr, layer_data->reg_base_virt + LOGICVC_INT_MASK_ROFF);

	mutex_unlock(&common_data->irq_mutex);

	if (ret < 0)
		return ret;
	else if (ret == 0) {
		dbg("xylonfb timeout waiting for V-sync\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int xylonfb_ioctl(struct fb_info *fbi, unsigned int cmd,
	unsigned long arg)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct fb_vblank vblank;
	void __user *argp = (void __user *)arg;
	u32 crt;
	long ret = 0;

	dbg("%s\n", __func__);

	switch (cmd) {
	case FBIOGET_VSCREENINFO:
		dbg("FBIOGET_VSCREENINFO\n");
		if (!lock_fb_info(fbi))
			return -ENODEV;
		var = fbi->var;
		unlock_fb_info(fbi);

		ret = copy_to_user(argp, &var, sizeof(var)) ? -EFAULT : 0;
		break;

	case FBIOPUT_VSCREENINFO:
		dbg("FBIOPUT_VSCREENINFO\n");
		if (copy_from_user(&var, argp, sizeof(var)))
			return -EFAULT;
		if (!lock_fb_info(fbi))
			return -ENODEV;
		console_lock();
		fbi->flags |= FBINFO_MISC_USEREVENT;
		ret = fb_set_var(fbi, &var);
		fbi->flags &= ~FBINFO_MISC_USEREVENT;
		console_unlock();
		unlock_fb_info(fbi);
		if (!ret && copy_to_user(argp, &var, sizeof(var)))
			ret = -EFAULT;
		break;

	case FBIOGET_FSCREENINFO:
		dbg("FBIOGET_FSCREENINFO\n");
		if (!lock_fb_info(fbi))
			return -ENODEV;
		fix = fbi->fix;
		unlock_fb_info(fbi);

		ret = copy_to_user(argp, &fix, sizeof(fix)) ? -EFAULT : 0;
		break;

	case FBIOPAN_DISPLAY:
		dbg("FBIOPAN_DISPLAY\n");
		if (copy_from_user(&var, argp, sizeof(var)))
			return -EFAULT;
		if (!lock_fb_info(fbi))
			return -ENODEV;
		console_lock();
		ret = fb_pan_display(fbi, &var);
		console_unlock();
		unlock_fb_info(fbi);
		if (ret == 0 && copy_to_user(argp, &var, sizeof(var)))
			return -EFAULT;
		break;

	case FBIO_CURSOR:
		dbg("FBIO_CURSOR\n");
		ret = -EINVAL;
		break;

	case FBIOBLANK:
		dbg("FBIOBLANK\n");
		if (!lock_fb_info(fbi))
			return -ENODEV;
		console_lock();
		fbi->flags |= FBINFO_MISC_USEREVENT;
		ret = fb_blank(fbi, arg);
		fbi->flags &= ~FBINFO_MISC_USEREVENT;
		console_unlock();
		unlock_fb_info(fbi);
		break;

	case FBIOGET_VBLANK:
		dbg("FBIOGET_VBLANK\n");
		if (copy_from_user(&vblank, argp, sizeof(vblank)))
			return -EFAULT;
		ret = xylonfb_get_vblank(&vblank, fbi);
		if (!ret)
			if (copy_to_user(argp, &vblank, sizeof(vblank)))
				ret = -EFAULT;
		break;

	case FBIO_WAITFORVSYNC:
		dbg("FBIO_WAITFORVSYNC\n");
		if (get_user(crt, (u32 __user *) arg))
			break;
		ret = xylonfb_wait_for_vsync(crt, fbi);
		break;

	default:
		dbg("FBIO_DEFAULT\n");
		ret = -EINVAL;
	}

	return ret;
}

/*
 * Framebuffer operations structure.
 */
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


static inline void set_ctrl_reg(unsigned long pix_data_invert,
	unsigned long pix_clk_act_edge)
{
	u32 sync = xylonfb_vmode.fb_vmode.sync;
	u32 ctrl = CTRL_REG_INIT;

	if (sync & (1<<0)) {	//FB_SYNC_HOR_HIGH_ACT
		ctrl &= (~(1<<1));
	}
	if (sync & (1<<1)) {	// FB_SYNC_VERT_HIGH_ACT
		ctrl &= (~(1<<3));
	}
	if (pix_data_invert) {
		ctrl |= (1<<7);
	}
	if (pix_clk_act_edge) {
		ctrl |= (1<<8);
	}

	xylonfb_vmode.ctrl_reg = ctrl;
}

#ifdef FB_XYLON_CONFIG_OF
static int xylonfb_parse_vram_info(struct platform_device *pdev,
	unsigned long *vmem_base_addr, unsigned long *vmem_high_addr)
{
	u32 const *prop;
	int size;

	dbg("%s\n", __func__);

	prop =
		of_get_property(pdev->dev.of_node, "xlnx,vmem-baseaddr", &size);
	if (!prop) {
		printk(KERN_ERR "Error xylonfb getting VRAM address begin\n");
		return -EINVAL;
	}
	*vmem_base_addr = be32_to_cpup(prop);

	prop =
		of_get_property(pdev->dev.of_node, "xlnx,vmem-highaddr", &size);
	if (!prop) {
		printk(KERN_ERR "Error xylonfb getting VRAM address end\n");
		return -EINVAL;
	}
	*vmem_high_addr = be32_to_cpup(prop);

	return 0;
}

static int xylonfb_parse_layer_info(struct platform_device *pdev,
	int *layers)
{
	u32 const *prop;
	int size;

	dbg("%s\n", __func__);

	prop = of_get_property(pdev->dev.of_node, "xlnx,num-of-layers", &size);
	if (!prop) {
		printk(KERN_ERR "Error getting number of layers\n");
		return -EINVAL;
	}
	*layers = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "xlnx,use-background", &size);
	if (!prop) {
		printk(KERN_ERR "Error getting use background\n");
		return -EINVAL;
	}
	/* if background layer is present decrease number of layers */
	if (be32_to_cpup(prop) == 1)
		(*layers)--;
	else
		dbg("xylonfb no BG layer\n");

	return 0;
}

static int xylonfb_parse_vmode_info(struct platform_device *pdev,
	int *active_layer)
{
	struct device_node *dn, *vmode_dn;
	u32 const *prop;
	unsigned long pix_data_invert, pix_clk_act_edge;
	int i, size, vmode_id;

	dbg("%s\n", __func__);

	*active_layer = 0;

	dn = of_find_node_by_name(NULL, "xylon-videomode-params");
	if (dn == NULL) {
		printk(KERN_ERR "Error getting video mode parameters\n");
		return -1;
	}

	pix_data_invert = 0;
	prop = of_get_property(dn, "pixel-data-invert", &size);
	if (!prop)
		printk(KERN_ERR "Error getting pixel data invert\n");
	else
		pix_data_invert = be32_to_cpup(prop);
	pix_clk_act_edge = 0;
	prop = of_get_property(dn, "pixel-clock-active-edge", &size);
	if (!prop)
		printk(KERN_ERR "Error getting pixel active edge\n");
	else
		pix_clk_act_edge = be32_to_cpup(prop);

	prop = of_get_property(dn, "default-active-layer-idx", &size);
	if (prop)
		*active_layer = be32_to_cpup(prop);
	else
		printk(KERN_INFO "xylonfb setting default layer to %d\n",
			*active_layer);

	prop = of_get_property(dn, "default-videomode-idx", &size);
	if (prop)
		vmode_id = be32_to_cpup(prop);
	else {
		vmode_id = 0;
		printk(KERN_INFO "xylonfb setting default video mode to %d\n",
			vmode_id);
	}
	for (i = 0, vmode_dn = NULL; i <= vmode_id; i++)
		vmode_dn = of_get_next_child(dn, vmode_dn);

	prop = of_get_property(vmode_dn, "mode-name", &size);
	if (!prop)
		printk(KERN_ERR "Error getting video mode name\n");
	else
		strcpy(xylonfb_vmode.name, (char *)prop);

	prop = of_get_property(vmode_dn, "refresh", &size);
	if (!prop)
		printk(KERN_ERR "Error getting refresh rate\n");
	else
		xylonfb_vmode.fb_vmode.refresh = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "xres", &size);
	if (!prop)
		printk(KERN_ERR "Error getting xres\n");
	else
		xylonfb_vmode.fb_vmode.xres = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "yres", &size);
	if (!prop)
		printk(KERN_ERR "Error getting yres\n");
	else
		xylonfb_vmode.fb_vmode.yres = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "pixclock-khz", &size);
	if (!prop)
		printk(KERN_ERR "Error getting pixclock-khz\n");
	else
		xylonfb_vmode.fb_vmode.pixclock = KHZ2PICOS(be32_to_cpup(prop));

	prop = of_get_property(vmode_dn, "left-margin", &size);
	if (!prop)
		printk(KERN_ERR "Error getting left-margin\n");
	else
		xylonfb_vmode.fb_vmode.left_margin = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "right-margin", &size);
	if (!prop)
		printk(KERN_ERR "Error getting right-margin\n");
	else
		xylonfb_vmode.fb_vmode.right_margin = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "upper-margin", &size);
	if (!prop)
		printk(KERN_ERR "Error getting upper-margin\n");
	else
		xylonfb_vmode.fb_vmode.upper_margin = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "lower-margin", &size);
	if (!prop)
		printk(KERN_ERR "Error getting lower-margin\n");
	else
		xylonfb_vmode.fb_vmode.lower_margin = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "hsync-len", &size);
	if (!prop)
		printk(KERN_ERR "Error getting hsync-len\n");
	else
		xylonfb_vmode.fb_vmode.hsync_len = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "vsync-len", &size);
	if (!prop)
		printk(KERN_ERR "Error getting vsync-len\n");
	else
		xylonfb_vmode.fb_vmode.vsync_len = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "sync", &size);
	if (!prop)
		printk(KERN_ERR "Error getting sync\n");
	else
		xylonfb_vmode.fb_vmode.sync = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "vmode", &size);
	if (!prop)
		printk(KERN_ERR "Error getting vmode\n");
	else
		xylonfb_vmode.fb_vmode.vmode = be32_to_cpup(prop);

	set_ctrl_reg(pix_data_invert, pix_clk_act_edge);

	return 0;
}

static int xylonfb_parse_layer_params(struct platform_device *pdev,
	int id, struct layer_fix_data *lfdata)
{
	u32 const *prop;
	int size;
	char layer_property_name[25];

	dbg("%s\n", __func__);

	sprintf(layer_property_name, "xlnx,layer-%d-offset", id);
	prop = of_get_property(pdev->dev.of_node, layer_property_name, &size);
	if (!prop) {
		printk(KERN_ERR "Error getting layer offset\n");
		return -EINVAL;
	} else {
		lfdata->offset = be32_to_cpup(prop);
	}

	prop = of_get_property(pdev->dev.of_node, "xlnx,row-stride", &size);
	if (!prop)
		lfdata->width = 1024;
	else
		lfdata->width = be32_to_cpup(prop);

	sprintf(layer_property_name, "xlnx,layer-%d-alpha-mode", id);
	prop = of_get_property(pdev->dev.of_node, layer_property_name, &size);
	if (!prop) {
		printk(KERN_ERR "Error getting layer alpha mode\n");
		return -EINVAL;
	} else {
		lfdata->alpha_mode = be32_to_cpup(prop);
	}

	sprintf(layer_property_name, "xlnx,layer-%d-data-width", id);
	prop = of_get_property(pdev->dev.of_node, layer_property_name, &size);
	if (!prop)
		lfdata->bpp = 16;
	else
		lfdata->bpp = be32_to_cpup(prop);
	if (lfdata->bpp == 24)
		lfdata->bpp = 32;

	lfdata->bpp_virt = lfdata->bpp;

	switch (lfdata->bpp) {
		case 8:
			if (lfdata->alpha_mode == LOGICVC_PIXEL_ALPHA)
				lfdata->bpp = 16;
			break;
		case 16:
			if (lfdata->alpha_mode == LOGICVC_PIXEL_ALPHA)
				lfdata->bpp = 32;
			break;
	}

	return 0;
}
#endif

static int xylonfb_find_next_layer(struct layer_fix_data *lfdata,
	int layers, int curr)
{
	u32 address, temp_address, loop_address;
	int i, next;

	dbg("%s\n", __func__);

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

	dbg("%s\n", __func__);

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
		/* FIXME - this is set for 1920x1080 tripple buffering,
			but it should be read from dt parameters */
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
}

static int xylonfb_map(int id, int layers, struct device *dev,
	struct xylonfb_layer_data *layer_data, struct layer_fix_data *lfdata,
	unsigned long vmem_base_addr, u32 reg_base_phys, void *reg_base_virt)
{
	dbg("%s\n", __func__);

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
		printk(KERN_ERR "Error xylonfb ioremap REGS 0x%X FB 0x%X\n",
			(unsigned int)layer_data->reg_base_virt,
			(unsigned int)layer_data->fb_virt);
		return -ENOMEM;
	}
	//memset_io((void __iomem *)layer_data->fb_virt, 0, layer_data->fb_size);
	layer_data->layer_reg_base_virt =
		layer_data->reg_base_virt + logicvc_layer_reg_offset[id];
	layer_data->layer_clut_base_virt =
		layer_data->reg_base_virt + logicvc_clut_reg_offset[id];
	layer_data->layer_use_ref = 0;
	layer_data->layer_info = id;
	layer_data->layers = layers;

	return 0;
}

static inline void xylonfb_set_drv_vmode(void)
{
	dbg("%s\n", __func__);

	drv_vmode.xres = xylonfb_vmode.fb_vmode.xres;
	drv_vmode.yres = xylonfb_vmode.fb_vmode.yres;
	drv_vmode.pixclock = xylonfb_vmode.fb_vmode.pixclock;
	drv_vmode.left_margin = xylonfb_vmode.fb_vmode.left_margin;
	drv_vmode.right_margin = xylonfb_vmode.fb_vmode.right_margin;
	drv_vmode.upper_margin = xylonfb_vmode.fb_vmode.upper_margin;
	drv_vmode.lower_margin = xylonfb_vmode.fb_vmode.lower_margin;
	drv_vmode.hsync_len = xylonfb_vmode.fb_vmode.hsync_len;
	drv_vmode.vsync_len = xylonfb_vmode.fb_vmode.vsync_len;
	drv_vmode.vmode = xylonfb_vmode.fb_vmode.vmode;
}

static inline void xylonfb_set_fbi_timings(struct fb_var_screeninfo *var)
{
	dbg("%s\n", __func__);

	var->xres = drv_vmode.xres;
	var->yres = drv_vmode.yres;
	var->pixclock = drv_vmode.pixclock;
	var->left_margin = drv_vmode.left_margin;
	var->right_margin = drv_vmode.right_margin;
	var->upper_margin = drv_vmode.upper_margin;
	var->lower_margin = drv_vmode.lower_margin;
	var->hsync_len = drv_vmode.hsync_len;
	var->vsync_len = drv_vmode.vsync_len;
	var->sync = drv_vmode.sync;
	var->vmode = drv_vmode.vmode;
}

static inline void xylonfb_set_hw_specifics(struct fb_info *fbi,
	struct xylonfb_layer_data *layer_data, struct layer_fix_data *lfdata,
	u32 reg_base_phys)
{
	dbg("%s\n", __func__);

	fbi->fix.smem_start = layer_data->fb_phys;
	fbi->fix.smem_len = layer_data->fb_size;
	fbi->fix.type = FB_TYPE_PACKED_PIXELS;
	if ((lfdata->bpp == 8) &&
		((lfdata->alpha_mode == LOGICVC_CLUT_16BPP_ALPHA) ||
		(lfdata->alpha_mode == LOGICVC_CLUT_32BPP_ALPHA)))
		fbi->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else
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

static int xylonfb_set_timings(struct fb_info *fbi,
	int bpp, bool change_res)
{
	struct fb_var_screeninfo fb_var;
	int rc;
	bool set = 0;

	dbg("%s\n", __func__);

	if (change_res)
		if (!pixclk_change(fbi))
			return 0;

	rc = fb_find_mode(&fb_var, fbi, mode_option, NULL, 0,
		&xylonfb_vmode.fb_vmode, bpp);
	switch (rc) {
		case 0:
			printk(KERN_ERR "Error xylonfb video mode option\n"
				"using driver default mode %s\n", xylonfb_vmode.name);
			break;

		case 1 ... 4:
			if (rc == 1) {
				dbg("xylonfb using video mode option %s\n",
					mode_option);
					set = 1;
			}
			else if (rc == 2) {
				printk(KERN_INFO "xylonfb using video mode option, "
					"with ignored refresh rate %s\n", mode_option);
					set = 1;
			}
			else if (rc == 3) {
				printk(KERN_INFO "xylonfb using default video mode %s\n",
					xylonfb_vmode.name);
				if (!change_res)
					set = 1;
			}
			else if (rc == 4) {
				printk(KERN_INFO "xylonfb video mode fallback\n");
				if (!change_res)
					set = 1;
			}

			if (set) {
				dbg("set!\n");
				drv_vmode.xres = fb_var.xres;
				drv_vmode.yres = fb_var.yres;
				drv_vmode.pixclock = fb_var.pixclock;
				drv_vmode.left_margin = fb_var.left_margin;
				drv_vmode.right_margin = fb_var.right_margin;
				drv_vmode.upper_margin = fb_var.upper_margin;
				drv_vmode.lower_margin = fb_var.lower_margin;
				drv_vmode.hsync_len = fb_var.hsync_len;
				drv_vmode.vsync_len = fb_var.vsync_len;
				drv_vmode.sync = fb_var.sync;
				drv_vmode.vmode = fb_var.vmode;
			}

			break;
	}

	return rc;
}

static int xylonfb_register_fb(struct fb_info *fbi,
	struct xylonfb_layer_data *layer_data, struct layer_fix_data *lfdata,
	u32 reg_base_phys, int id, int *regfb)
{
	int alpha;
	dbg("%s\n", __func__);

	fbi->flags = FBINFO_DEFAULT;
	fbi->screen_base = (char __iomem *)layer_data->fb_virt;
	fbi->screen_size = layer_data->fb_size;
	fbi->pseudo_palette = kzalloc(sizeof(u32) * XYLONFB_PSEUDO_PALETTE_SZ,
		GFP_KERNEL);
	fbi->fbops = &xylonfb_ops;

	sprintf(fbi->fix.id, "Xylon FB%d", id);
	xylonfb_set_hw_specifics(fbi, layer_data, lfdata, reg_base_phys);

	/* if mode_option is set, find mode will be done only once */
	if (mode_option) {
		xylonfb_set_timings(fbi, lfdata->bpp, RES_CHANGE_DENIED);
		mode_option = NULL;
	}

	xylonfb_set_fbi_timings(&fbi->var);

	if (lfdata->alpha_mode == LOGICVC_LAYER_ALPHA)
		alpha = 0;
	else
		alpha = 1;
	if (fb_alloc_cmap(&fbi->cmap, 256, alpha))
		return -ENOMEM;

	*regfb = register_framebuffer(fbi);
	if (*regfb) {
		printk(KERN_ERR "Error xylonfb registering xylonfb %d\n", id);
		return -EINVAL;
	}
	printk(KERN_INFO "xylonfb %d registered\n", id);
	/* after driver registration values in struct fb_info
		must not be changed anywhere else except in xylonfb_set_par */

	return 0;
}

static void xylonfb_logicvc_disp_ctrl(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data =
		layer_data->xylonfb_cd;
	u32 val;

	dbg("%s\n", __func__);

	val = LOGICVC_EN_VDD_MSK;
	writel(val, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
	mdelay(common_data->power_on_delay);
	val |= LOGICVC_V_EN_MSK;
	writel(val, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
	mdelay(common_data->signal_on_delay);
	val |= LOGICVC_EN_BLIGHT_MSK;
	writel(val, layer_data->reg_base_virt + LOGICVC_SPWRCTRL_ROFF);
}

static void xylonfb_start_logicvc(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	dbg("%s\n", __func__);

	writel(fbi->var.right_margin-1,
		layer_data->reg_base_virt + LOGICVC_SHSY_FP_ROFF);
	writel(fbi->var.hsync_len-1,
		layer_data->reg_base_virt + LOGICVC_SHSY_ROFF);
	writel(fbi->var.left_margin-1,
		layer_data->reg_base_virt + LOGICVC_SHSY_BP_ROFF);
	writel(fbi->var.xres-1,
		layer_data->reg_base_virt + LOGICVC_SHSY_RES_ROFF);
	writel(fbi->var.lower_margin-1,
		layer_data->reg_base_virt + LOGICVC_SVSY_FP_ROFF);
	writel(fbi->var.vsync_len-1,
		layer_data->reg_base_virt + LOGICVC_SVSY_ROFF);
	writel(fbi->var.upper_margin-1,
		layer_data->reg_base_virt + LOGICVC_SVSY_BP_ROFF);
	writel(fbi->var.yres-1,
		layer_data->reg_base_virt + LOGICVC_SVSY_RES_ROFF);
	writel(xylonfb_vmode.ctrl_reg,
		layer_data->reg_base_virt + LOGICVC_SCTRL_ROFF);
	writel(SD_REG_INIT, layer_data->reg_base_virt + LOGICVC_SDTYPE_ROFF);
	writel(BACKGROUND_COLOR, layer_data->reg_base_virt + LOGICVC_BACKCOL_ROFF);
//	writel(0x00, layer_data->reg_base_virt + LOGICVC_DOUBLE_VBUFF_ROFF);
//	writel(0x00, layer_data->reg_base_virt + LOGICVC_DOUBLE_CLUT_ROFF);
	writel(0xFFFF, layer_data->reg_base_virt + LOGICVC_INT_ROFF);
	writel(0xFFFF, layer_data->reg_base_virt + LOGICVC_INT_MASK_ROFF);
	writel(TRANSPARENT_COLOR_24BPP,
		(layer_data->layer_reg_base_virt + LOGICVC_LAYER_TRANSP_ROFF));

	dbg("\n");
	dbg("logiCVC HW parameters:\n");
	dbg("    Horizontal Front Porch: %d pixclks\n",
		fbi->var.right_margin);
	dbg("    Horizontal Sync:        %d pixclks\n",
		fbi->var.hsync_len);
	dbg("    Horizontal Back Porch:  %d pixclks\n",
		fbi->var.left_margin);
	dbg("    Vertical Front Porch:   %d pixclks\n",
		fbi->var.lower_margin);
	dbg("    Vertical Sync:          %d pixclks\n",
		fbi->var.vsync_len);
	dbg("    Vertical Back Porch:    %d pixclks\n",
		fbi->var.upper_margin);
	dbg("    Pixel Clock (ps):       %d\n",
		fbi->var.pixclock);
	dbg("    Bits per Pixel:         %d\n",
		fbi->var.bits_per_pixel);
	dbg("    Horizontal Res:         %d\n",
		fbi->var.xres);
	dbg("    Vertical Res:           %d\n",
		fbi->var.yres);
	dbg("\n");
}

static void xylonfb_stop_logicvc(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	dbg("%s\n", __func__);

	writel(0, layer_data->reg_base_virt + LOGICVC_SCTRL_ROFF);
}

static int xylonfb_start(struct fb_info **afbi, int layers)
{
	struct xylonfb_layer_data *layer_data;
	int i;

	dbg("%s\n", __func__);

	if (xylonfb_set_pixelclock(afbi[0]))
		return -EACCES;
	/* start logiCVC and enable primary layer */
	xylonfb_start_logicvc(afbi[0]);
	/* display power control */
	xylonfb_logicvc_disp_ctrl(afbi[0]);
	/* turn OFF all layers except already used ones */
	for (i = 0; i < layers; i++) {
		layer_data = (struct xylonfb_layer_data *)afbi[i]->par;
		if (layer_data->layer_info & LOGICVC_LAYER_ON)
			continue;
		/* turn off layer */
		writel(0, (layer_data->layer_reg_base_virt + LOGICVC_LAYER_CTRL_ROFF));
	}
	/* print layer parameters */
	for (i = 0; i < layers; i++) {
		layer_data = (struct xylonfb_layer_data *)afbi[i]->par;
		dbg("logiCVC layer %d\n", i);
		dbg("    Registers Base Address:     0x%X\n",
			(unsigned int)layer_data->reg_base_phys);
		dbg("    Layer Video Memory Address: 0x%X\n",
			(unsigned int)layer_data->fb_phys);
		dbg("    X resolution:               %d\n",
			afbi[i]->var.xres);
		dbg("    Y resolution:               %d\n",
			afbi[i]->var.yres);
		dbg("    X resolution (virtual):     %d\n",
			afbi[i]->var.xres_virtual);
		dbg("    Y resolution (virtual):     %d\n",
			afbi[i]->var.yres_virtual);
		dbg("    Line length (bytes):        %d\n",
			afbi[i]->fix.line_length);
		dbg("    Bits per Pixel:             %d\n",
			afbi[i]->var.bits_per_pixel);
		dbg("\n");
	}

	return 0;
}

static int xylonfb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fb_info **afbi;
	struct fb_info *fbi;
	struct xylonfb_common_data *common_data;
	struct xylonfb_layer_data *layer_data;
	struct resource *reg_res, *irq_res;
#ifndef FB_XYLON_CONFIG_OF
	struct xylonfb_platform_data *pdata;
#endif
	struct layer_fix_data lfdata[LOGICVC_MAX_LAYERS];
	void *reg_base_virt;
	u32 reg_base_phys;
	unsigned long vmem_base_addr, vmem_high_addr;
	int reg_range, layers, active_layer;
	int i, rc;
	int regfb[LOGICVC_MAX_LAYERS];

	dbg("%s\n", __func__);

	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if ((!reg_res) || (!irq_res)) {
		printk(KERN_ERR "Error xylonfb resources: MEM 0x%X IRQ 0x%X\n",
			(unsigned int)reg_res, (unsigned int)irq_res);
		return -ENODEV;
	}

#ifdef FB_XYLON_CONFIG_OF
	rc = xylonfb_parse_vram_info(pdev, &vmem_base_addr, &vmem_high_addr);
	if (rc)
		return rc;
	rc = xylonfb_parse_layer_info(pdev, &layers);
	if (rc)
		return rc;
	if (xylonfb_parse_vmode_info(pdev, &active_layer) == 0) {
		/* if DT contains video mode options do not use
		   kernel command line video mode options */
		mode_option = NULL;
	}
#else
	pdata = (struct xylonfb_platform_data *)pdev->dev.platform_data;
	vmem_base_addr = pdata->vmem_base_addr;
	vmem_high_addr = pdata->vmem_high_addr;
	layers = pdata->num_layers;
	active_layer = pdata->active_layer;
#endif
	xylonfb_set_drv_vmode();

#if CONFIG_FB_XYLON_NUM_FBS > 0
	layers = CONFIG_FB_XYLON_NUM_FBS;
#endif

	afbi = kzalloc(sizeof(struct fb_info *) * layers, GFP_KERNEL);
	common_data = kzalloc(sizeof(struct xylonfb_common_data), GFP_KERNEL);
	if (!afbi || !common_data) {
		printk(KERN_ERR "Error xylonfb allocating internal data\n");
		rc = -ENOMEM;
		goto err_mem;
	}

	layer_data = NULL;

	reg_base_phys = reg_res->start;
	reg_range = reg_res->end - reg_res->start;
	reg_base_virt = ioremap_nocache(reg_base_phys, reg_range);

	/* load layer parameters for all layers */
	for (i = 0; i < layers; i++) {
#ifdef FB_XYLON_CONFIG_OF
		xylonfb_parse_layer_params(pdev, i, &lfdata[i]);
#else
		lfdata[i].offset = pdata->layer_params[i].offset;
		lfdata[i].bpp = pdata->layer_params[i].bpp;
		lfdata[i].width = pdata->row_stride;
#endif
		regfb[i] = -1;
	}

	/* make /dev/fb0 to be default active layer
	   no matter how hw layers are organized */
	for (i = active_layer; i < layers; i++) {
		if (regfb[i] != -1)
			continue;

		fbi = framebuffer_alloc(sizeof(struct xylonfb_layer_data), dev);
		if (!fbi) {
			printk(KERN_ERR "Error xylonfb allocate info\n");
			rc = -ENOMEM;
			goto err_fb;
		}
		afbi[i] = fbi;
		layer_data = fbi->par;
		layer_data->xylonfb_cd = common_data;

		spin_lock_init(&layer_data->layer_lock);

		xylonfb_set_yvirt(lfdata, vmem_base_addr, vmem_high_addr, layers, i);

		layer_data->layer_fix = lfdata[i];

		rc = xylonfb_map(i, layers, dev, layer_data, &lfdata[i],
			vmem_base_addr, reg_base_phys, reg_base_virt);
		if (rc)
			goto err_fb;

		rc = xylonfb_register_fb(fbi, layer_data, &lfdata[i],
			reg_base_phys, i, &regfb[i]);
		if (rc)
			goto err_fb;



		/* register following layers in HW configuration order */
		if (active_layer > 0) {
			i = -1; /* after for loop increment i will be zero */
			active_layer = -1;
		}

		dbg( \
			"    Layer ID %d\n" \
			"    Layer offset %d\n" \
			"    Layer width %d pixels\n" \
			"    Layer height %d lines\n" \
			"    Layer bits per pixel %d\n" \
			"    Layer bits per pixel (virtual) %d\n" \
			"    Layer FB size %ld bytes\n", \
			(layer_data->layer_info & 0x0F),
			layer_data->layer_fix.offset,
			layer_data->layer_fix.width,
			layer_data->layer_fix.height,
			layer_data->layer_fix.bpp,
			layer_data->layer_fix.bpp_virt,
			layer_data->fb_size);
	}

	common_data->xylonfb_irq = irq_res->start;
	rc = request_irq(common_data->xylonfb_irq, xylonfb_isr,
			IRQF_TRIGGER_HIGH, PLATFORM_DRIVER_NAME, afbi);
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

	dev_set_drvdata(dev, (void *)afbi);

	/* start HW */
	rc = xylonfb_start(afbi, layers);
	if (rc)
		goto err_fb;

	printk(KERN_INFO
		"xylonfb video mode: %dx%d-%dbpp@60\n",
		afbi[0]->var.xres, afbi[0]->var.yres, afbi[0]->var.bits_per_pixel);

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

static int xylonfb_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fb_info **afbi = (struct fb_info **)dev_get_drvdata(dev);
	struct fb_info *fbi = afbi[0];
	struct xylonfb_layer_data *layer_data = fbi->par;
	struct xylonfb_common_data *common_data =
		layer_data->xylonfb_cd;
	int i;
	bool logicvc_off;

	dbg("%s\n", __func__);

	if (common_data->xylonfb_use_ref) {
		printk(KERN_ERR "Error xylonfb in use\n");
		return -EINVAL;
	}

	logicvc_off = 0;

	free_irq(common_data->xylonfb_irq, afbi);
	for (i = layer_data->layers-1; i >= 0; i--) {
		fbi = afbi[i];
		layer_data = fbi->par;
		if (!logicvc_off) {
			xylonfb_stop_logicvc(fbi);
			iounmap(layer_data->reg_base_virt);
			logicvc_off = 1;
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

/* Match table for of_platform binding */
#ifdef FB_XYLON_CONFIG_OF
static struct of_device_id xylonfb_of_match[] __devinitdata = {
	{ .compatible = "xylon,logicvc-2.04.a" },
	{ .compatible = "xylon,logicvc-2.05.b" },
	{ .compatible = "xlnx,logicvc-2.05.c" },
	{/* end of table */},
};
MODULE_DEVICE_TABLE(of, xylonfb_of_match);
#else
#define xylonfb_of_match NULL
#endif

static struct platform_driver xylonfb_driver = {
	.probe = xylonfb_probe,
	.remove = xylonfb_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = PLATFORM_DRIVER_NAME,
		.of_match_table = xylonfb_of_match,
	},
};


#ifndef MODULE
static int __init xylonfb_setup(char *options)
{
	char *this_opt;

	dbg("%s\n", __func__);

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		mode_option = this_opt;
	}
	return 0;
}
#endif

static int __init xylonfb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	dbg("%s\n", __func__);

	/*
	 *  For kernel boot options (in 'video=xxxfb:<options>' format)
	 */
	if (fb_get_options(DRIVER_NAME, &option))
		return -ENODEV;
	/* Set internal module parameters */
	xylonfb_setup(option);
#endif

	if (platform_driver_register(&xylonfb_driver)) {
		printk(KERN_ERR "Error xylonfb driver registration\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit xylonfb_exit(void)
{
	dbg("%s\n", __func__);

	platform_driver_unregister(&xylonfb_driver);
}


#ifndef MODULE
late_initcall(xylonfb_init);
#else
module_init(xylonfb_init);
module_exit(xylonfb_exit);
#endif

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);

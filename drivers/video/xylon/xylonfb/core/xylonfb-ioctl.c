/*
 * Xylon logiCVC frame buffer driver IOCTL functionality
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * 2013 Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


#include <linux/uaccess.h>
#include "logicvc.h"
#include "xylonfb.h"
#if defined(CONFIG_FB_XYLON_MISC)
#include "../misc/xylonfb-misc.h"
#endif


static int xylonfb_get_vblank(struct fb_vblank *vblank, struct fb_info *fbi)
{
	vblank->flags |= FB_VBLANK_HAVE_VSYNC;

	return 0;
}

static int xylonfb_wait_for_vsync(u32 crt, struct fb_info *fbi)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	u32 imr;
	int ret, cnt;

	mutex_lock(&cd->irq_mutex);

	cnt = cd->vsync.cnt;

	/* prepare LOGICVC V-sync interrupt */
	imr = cd->reg_access.xylonfb_get_reg_val(
		ld->reg_base_virt, LOGICVC_INT_MASK_ROFF, ld);
	imr &= (~LOGICVC_V_SYNC_INT);
	/* clear LOGICVC V-sync interrupt */
	writel(LOGICVC_V_SYNC_INT,
		ld->reg_base_virt + LOGICVC_INT_STAT_ROFF);
	/* enable LOGICVC V-sync interrupt */
	cd->reg_access.xylonfb_set_reg_val(imr,
		ld->reg_base_virt, LOGICVC_INT_MASK_ROFF, ld);

	ret = wait_event_interruptible_timeout(cd->vsync.wait,
		(cnt != cd->vsync.cnt), HZ/10);

	/* disable LOGICVC V-sync interrupt */
	imr |= LOGICVC_V_SYNC_INT;
	cd->reg_access.xylonfb_set_reg_val(imr,
		ld->reg_base_virt, LOGICVC_INT_MASK_ROFF, ld);

	mutex_unlock(&cd->irq_mutex);

	if (ret < 0)
		return ret;
	else if (ret == 0)
		return -ETIMEDOUT;

	return 0;
}

static unsigned int alpha_normalized(unsigned int alpha,
	unsigned int used_bits, bool get)
{
	if (get)
		return (((255 << 16) / ((1 << used_bits)-1)) * alpha) >> 16;
	else
		return alpha / (255 / ((1 << used_bits)-1));
}

static int xylonfb_layer_alpha(struct xylonfb_layer_data *ld,
	unsigned int *alpha, bool get)
{
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	unsigned int used_bits;

	if (ld->layer_fix.alpha_mode != LOGICVC_LAYER_ALPHA)
		return -EPERM;

	switch (ld->layer_fix.layer_type) {
	case LOGICVC_YCBCR_LAYER:
		used_bits = 8;
		break;
	case LOGICVC_RGB_LAYER:
		switch (ld->layer_fix.bpp_virt) {
		case 8:
			used_bits = 3;
			break;
		case 16:
			used_bits = 6;
			break;
		case 32:
			used_bits = 8;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	if (get) {
		*alpha = cd->reg_access.xylonfb_get_reg_val(
			ld->layer_reg_base_virt, LOGICVC_LAYER_ALPHA_ROFF, ld);
		*alpha &= (0xFF >> (8-used_bits));
	}

	/* get/set normalized alpha value */
	*alpha = alpha_normalized(*alpha, used_bits, get);

	if (!get)
		cd->reg_access.xylonfb_set_reg_val(*alpha,
			ld->layer_reg_base_virt, LOGICVC_LAYER_ALPHA_ROFF, ld);

	return 0;
}

static int xylonfb_layer_color_rgb(struct xylonfb_layer_data *ld,
	struct xylonfb_layer_color *layer_color, unsigned int reg_offset,
	bool get)
{
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	void *base;
	u32 raw_rgb, r, g, b;
	int bpp, alpha_mode;

	if (reg_offset == LOGICVC_LAYER_TRANSP_ROFF) {
		base = ld->layer_reg_base_virt;
		bpp = ld->layer_fix.bpp_virt;
		alpha_mode = ld->layer_fix.alpha_mode;
	} else /* if (reg_offset == LOGICVC_BACKCOL_ROFF) */ {
		base = ld->reg_base_virt;
		bpp = ld->xylonfb_cd->xylonfb_bg_layer_bpp;
		alpha_mode = ld->xylonfb_cd->xylonfb_bg_layer_alpha_mode;
	}

	if (get) {
		raw_rgb = cd->reg_access.xylonfb_get_reg_val(
			base, reg_offset, ld);
check_bpp_get:
		/* convert HW color format to RGB-888 */
		switch (bpp) {
		case 8:
			switch (alpha_mode) {
			case LOGICVC_CLUT_16BPP_ALPHA:
				/* RGB-565 */
				bpp = 16;
				goto check_bpp_get;
				break;
			case LOGICVC_CLUT_32BPP_ALPHA:
				/* RGB-888 */
				bpp = 32;
				goto check_bpp_get;
				break;
			default:
				/* RGB-332 */
				r = raw_rgb >> 5;
				r = (((r << 3) | r) << 2) | (r >> 1);
				g = (raw_rgb >> 2) & 0x07;
				g = (((g << 3) | g) << 2) | (g >> 1);
				b = raw_rgb & 0x03;
				b = (b << 6) | (b << 4) | (b << 2) | b;
				break;
			}
			break;
		case 16:
			/* RGB-565 */
			r = raw_rgb >> 11;
			r = (r << 3) | (r >> 2);
			g = (raw_rgb >> 5) & 0x3F;
			g = (g << 2) | (g >> 4);
			b = raw_rgb & 0x1F;
			b = (b << 3) | (b >> 2);
			break;
		case 32:
			/* RGB-888 */
			r = raw_rgb >> 16;
			g = (raw_rgb >> 8) & 0xFF;
			b = raw_rgb & 0xFF;
			break;
		default:
			raw_rgb = r = g = b = 0;
		}
		layer_color->raw_rgb = raw_rgb;
		layer_color->r = (u8)r;
		layer_color->g = (u8)g;
		layer_color->b = (u8)b;
	} else {
		if (layer_color->use_raw) {
			raw_rgb = layer_color->raw_rgb;
		} else {
			r = layer_color->r;
			g = layer_color->g;
			b = layer_color->b;
check_bpp_set:
			/* convert RGB-888 to HW color format */
			switch (bpp) {
			case 8:
				switch (alpha_mode) {
				case LOGICVC_CLUT_16BPP_ALPHA:
					/* RGB-565 */
					bpp = 16;
					goto check_bpp_set;
					break;
				case LOGICVC_CLUT_32BPP_ALPHA:
					/* RGB-888 */
					bpp = 32;
					goto check_bpp_set;
					break;
				default:
					raw_rgb =
						(r & 0xE0) |
						((g & 0xE0) >> 3) |
						((b & 0xC0) >> 6);
					break;
				}
				break;
			case 16:
				raw_rgb =
					((r & 0xF8) << 8) |
					((g & 0xFC) << 3) |
					((b & 0xF8) >> 3);
				break;
			case 32:
				raw_rgb =
					(r << 16) |
					(g << 8) |
					b;
				break;
			default:
				raw_rgb = 0;
			}
		}
		cd->reg_access.xylonfb_set_reg_val(raw_rgb,
			base, reg_offset, ld);
	}

	return 0;
}

static int xylonfb_layer_pos_sz(struct fb_info *fbi,
	struct xylonfb_layer_pos_size *layer_pos_sz, bool get)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	u32 x, y, width, height, xres, yres;

	xres = fbi->var.xres;
	yres = fbi->var.yres;

	if (get) {
		x = cd->reg_access.xylonfb_get_reg_val(
			ld->layer_reg_base_virt, LOGICVC_LAYER_HOR_POS_ROFF, ld);
		layer_pos_sz->x = xres - (x + 1);
		y = cd->reg_access.xylonfb_get_reg_val(
			ld->layer_reg_base_virt, LOGICVC_LAYER_VER_POS_ROFF, ld);
		layer_pos_sz->y = yres - (y + 1);
		layer_pos_sz->width = cd->reg_access.xylonfb_get_reg_val(
			ld->layer_reg_base_virt, LOGICVC_LAYER_WIDTH_ROFF, ld);
		layer_pos_sz->width += 1;
		layer_pos_sz->height = cd->reg_access.xylonfb_get_reg_val(
			ld->layer_reg_base_virt, LOGICVC_LAYER_HEIGHT_ROFF, ld);
		layer_pos_sz->height += 1;
	} else {
		x = layer_pos_sz->x;
		y = layer_pos_sz->y;
		width = layer_pos_sz->width;
		height = layer_pos_sz->height;

		if ((x > xres) || (y > yres))
			return -EINVAL;

		if ((width == 0) || (height == 0))
			return -EINVAL;

		if ((x + width) > xres) {
			width = xres - x;
			layer_pos_sz->width = width;
		}
		if ((y + height) > yres) {
			height = yres - y;
			layer_pos_sz->height = height;
		}
		/* YCbCr 4:2:2 layer type can only have even layer width */
		if ((width > 2)
				&&
			(ld->layer_fix.layer_type == LOGICVC_YCBCR_LAYER)
				&&
			(ld->layer_fix.bpp_virt == 16)) {
			width &= ~1;
		}

		cd->reg_access.xylonfb_set_reg_val((width - 1),
			ld->layer_reg_base_virt, LOGICVC_LAYER_WIDTH_ROFF, ld);
		cd->reg_access.xylonfb_set_reg_val((height - 1),
			ld->layer_reg_base_virt, LOGICVC_LAYER_HEIGHT_ROFF, ld);
		cd->reg_access.xylonfb_set_reg_val((xres - (x + 1)),
			ld->layer_reg_base_virt, LOGICVC_LAYER_HOR_POS_ROFF, ld);
		cd->reg_access.xylonfb_set_reg_val((yres - (y + 1)),
			ld->layer_reg_base_virt, LOGICVC_LAYER_VER_POS_ROFF, ld);
	}

	return 0;
}

static int xylonfb_layer_reg_access(
	struct xylonfb_layer_data *ld,
	struct xylonfb_common_data *cd,
	struct xylonfb_hw_access *hw_access,
	bool read)
{
	u32 rel_offset;

	if ((hw_access->offset < LOGICVC_LAYER_BASE_OFFSET) ||
		(hw_access->offset > LOGICVC_LAYER_BASE_END))
		return -EPERM;

	rel_offset = hw_access->offset -
		((ld->layer_fix.layer_fix_info & 0x0F) * 0x80) -
		LOGICVC_LAYER_BASE_OFFSET;

	if (read) {
		hw_access->value = cd->reg_access.xylonfb_get_reg_val(
			ld->layer_reg_base_virt, rel_offset, ld);
	} else {
		cd->reg_access.xylonfb_set_reg_val(hw_access->value,
			ld->layer_reg_base_virt, rel_offset, ld);
	}

	return 0;
}

int xylonfb_ioctl(struct fb_info *fbi, unsigned int cmd, unsigned long arg)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	union {
		struct fb_vblank vblank;
		struct xylonfb_layer_color layer_color;
		struct xylonfb_layer_pos_size layer_pos_sz;
		struct xylonfb_hw_access hw_access;
	} ioctl;
	void __user *argp = (void __user *)arg;
	u32 var32;
	unsigned long val, layer_buffs, layer_id;
	int ret = 0;

	switch (cmd) {
	case FBIOGET_VBLANK:
		driver_devel("FBIOGET_VBLANK\n");
		if (copy_from_user(&ioctl.vblank, argp, sizeof(ioctl.vblank)))
			return -EFAULT;
		ret = xylonfb_get_vblank(&ioctl.vblank, fbi);
		if (!ret && copy_to_user(argp, &ioctl.vblank, sizeof(ioctl.vblank)))
			ret = -EFAULT;
		break;

	case FBIO_WAITFORVSYNC:
		driver_devel("FBIO_WAITFORVSYNC\n");
		if (get_user(var32, (u32 __user *)arg))
			return -EFAULT;
		ret = xylonfb_wait_for_vsync(var32, fbi);
		break;

	case XYLONFB_GET_LAYER_IDX:
		driver_devel("XYLONFB_GET_LAYER_IDX\n");
		val = ld->layer_fix.layer_fix_info & 0x0F;
		put_user(val, (unsigned long __user *)arg);
		break;

	case XYLONFB_GET_LAYER_ALPHA:
		driver_devel("XYLONFB_GET_LAYER_ALPHA\n");
		ret = xylonfb_layer_alpha(ld, (unsigned int *)&val, true);
		if (!ret)
			put_user(val, (unsigned long __user *)arg);
		break;

	case XYLONFB_SET_LAYER_ALPHA:
		driver_devel("XYLONFB_SET_LAYER_ALPHA\n");
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		mutex_lock(&ld->layer_mutex);
		ret = xylonfb_layer_alpha(ld, (unsigned int *)&val, false);
		mutex_unlock(&ld->layer_mutex);
		break;

	case XYLONFB_LAYER_COLOR_TRANSP:
		driver_devel("XYLONFB_LAYER_COLOR_TRANSP\n");
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		mutex_lock(&ld->layer_mutex);
		var32 = cd->reg_access.xylonfb_get_reg_val(
			ld->layer_reg_base_virt, LOGICVC_LAYER_CTRL_ROFF, ld);
		if (val)
			var32 |= (1 << 1); /* transparency disabled */
		else
			var32 &= ~(1 << 1); /* transparency enabled */
		cd->reg_access.xylonfb_set_reg_val(var32,
			ld->layer_reg_base_virt, LOGICVC_LAYER_CTRL_ROFF, ld);
		mutex_unlock(&ld->layer_mutex);
		break;

	case XYLONFB_GET_LAYER_COLOR_TRANSP:
		driver_devel("XYLONFB_GET_LAYER_COLOR_TRANSP\n");
		if (copy_from_user(&ioctl.layer_color, argp,
			sizeof(ioctl.layer_color)))
			return -EFAULT;
		ret = xylonfb_layer_color_rgb(ld, &ioctl.layer_color,
			LOGICVC_LAYER_TRANSP_ROFF, true);
		if (!ret)
			if (copy_to_user(argp, &ioctl.layer_color,
				sizeof(ioctl.layer_color)))
				ret = -EFAULT;
		break;

	case XYLONFB_SET_LAYER_COLOR_TRANSP:
		driver_devel("XYLONFB_SET_LAYER_COLOR_TRANSP\n");
		if (copy_from_user(&ioctl.layer_color, argp,
			sizeof(ioctl.layer_color)))
			return -EFAULT;
		mutex_lock(&ld->layer_mutex);
		ret = xylonfb_layer_color_rgb(ld, &ioctl.layer_color,
			LOGICVC_LAYER_TRANSP_ROFF, false);
		mutex_unlock(&ld->layer_mutex);
		break;

	case XYLONFB_GET_LAYER_SIZE_POS:
		driver_devel("XYLONFB_GET_LAYER_SIZE_POS\n");
		if (copy_from_user(&ioctl.layer_pos_sz, argp,
			sizeof(ioctl.layer_pos_sz)))
			return -EFAULT;
		ret = xylonfb_layer_pos_sz(fbi, &ioctl.layer_pos_sz, true);
		if (!ret)
			if (copy_to_user(argp, &ioctl.layer_pos_sz,
				sizeof(ioctl.layer_pos_sz)))
				ret = -EFAULT;
		break;

	case XYLONFB_SET_LAYER_SIZE_POS:
		driver_devel("XYLONFB_SET_LAYER_SIZE_POS\n");
		if (copy_from_user(&ioctl.layer_pos_sz, argp,
			sizeof(ioctl.layer_pos_sz)))
			return -EFAULT;
		mutex_lock(&ld->layer_mutex);
		ret = xylonfb_layer_pos_sz(fbi, &ioctl.layer_pos_sz, false);
		if (!ret)
			if (copy_to_user(argp, &ioctl.layer_pos_sz,
				sizeof(ioctl.layer_pos_sz)))
				ret = -EFAULT;
		mutex_unlock(&ld->layer_mutex);
		break;

	case XYLONFB_GET_LAYER_BUFFER:
		driver_devel("XYLONFB_GET_LAYER_BUFFER\n");
		layer_id = ld->layer_fix.layer_fix_info & 0x0F;
		var32 = readl(ld->reg_base_virt + LOGICVC_DOUBLE_VBUFF_ROFF);
		var32 >>= ((layer_id << 1)); /* get buffer */
		val = var32 & 0x03;
		put_user(val, (unsigned long __user *)arg);
		break;

	case XYLONFB_SET_LAYER_BUFFER:
		driver_devel("XYLONFB_SET_LAYER_BUFFER\n");
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		layer_buffs = ld->layer_fix.layer_fix_info >> 4;
		if (val >= layer_buffs)
			return -EINVAL;
		layer_id = ld->layer_fix.layer_fix_info & 0x0F;
		mutex_lock(&ld->layer_mutex);
		var32 = readl(ld->reg_base_virt + LOGICVC_DOUBLE_VBUFF_ROFF);
		var32 |= (1 << (10 + layer_id)); /* set layer */
		var32 &= ~(0x03 << (layer_id << 1)); /* clear previous buffer */
		var32 |= (val << (layer_id << 1)); /* set buffer */
		writel(var32, ld->reg_base_virt + LOGICVC_DOUBLE_VBUFF_ROFF);
		ret = xylonfb_wait_for_vsync(var32, fbi);
		mutex_unlock(&ld->layer_mutex);
		break;

	case XYLONFB_GET_LAYER_BUFFER_OFFSET:
		driver_devel("XYLONFB_GET_LAYER_BUFFER_OFFSET\n");
		layer_id = ld->layer_fix.layer_fix_info & 0x0F;
		var32 = readl(ld->reg_base_virt + LOGICVC_DOUBLE_VBUFF_ROFF);
		var32 >>= ((layer_id << 1)); /* get buffer */
		var32 &= 0x03;
		val = ld->layer_fix.buffer_offset;
		val *= var32;
		put_user(val, (unsigned long __user *)arg);
		break;

	case XYLONFB_GET_LAYER_BUFFERS_NUM:
		driver_devel("XYLONFB_GET_LAYER_BUFFERS_NUM\n");
		layer_buffs = ld->layer_fix.layer_fix_info >> 4;
		put_user(layer_buffs, (unsigned long __user *)arg);
		break;

	case XYLONFB_GET_BACKGROUND_COLOR:
		driver_devel("XYLONFB_GET_BACKGROUND_COLOR\n");
		if (ld->xylonfb_cd->xylonfb_bg_layer_bpp == 0)
			return -EPERM;
		if (copy_from_user(&ioctl.layer_color, argp,
			sizeof(ioctl.layer_color)))
			return -EFAULT;
		ret = xylonfb_layer_color_rgb(ld, &ioctl.layer_color,
			LOGICVC_BACKCOL_ROFF, true);
		if (!ret)
			if (copy_to_user(argp, &ioctl.layer_color,
				sizeof(ioctl.layer_color)))
				ret = -EFAULT;
		break;

	case XYLONFB_SET_BACKGROUND_COLOR:
		driver_devel("XYLONFB_SET_BACKGROUND_COLOR\n");
		if (ld->xylonfb_cd->xylonfb_bg_layer_bpp == 0)
			return -EPERM;
		if (copy_from_user(&ioctl.layer_color, argp,
			sizeof(ioctl.layer_color)))
			return -EFAULT;
		mutex_lock(&ld->layer_mutex);
		ret = xylonfb_layer_color_rgb(ld, &ioctl.layer_color,
			LOGICVC_BACKCOL_ROFF, false);
		mutex_unlock(&ld->layer_mutex);
		break;

	case XYLONFB_LAYER_EXT_BUFF_SWITCH:
		driver_devel("XYLONFB_LAYER_EXT_BUFF_SWITCH\n");
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		mutex_lock(&ld->layer_mutex);
		var32 = cd->reg_access.xylonfb_get_reg_val(
			ld->layer_reg_base_virt, LOGICVC_LAYER_CTRL_ROFF, ld);
		if (val)
			var32 |= (1 << 2);
		else
			var32 &= ~(1 << 2);
		cd->reg_access.xylonfb_set_reg_val(var32,
			ld->layer_reg_base_virt, LOGICVC_LAYER_CTRL_ROFF, ld);
		mutex_unlock(&ld->layer_mutex);
		break;

	case XYLONFB_READ_HW_REG:
		driver_devel("XYLONFB_READ_HW_REG\n");
		if (copy_from_user(&ioctl.hw_access, argp,
			sizeof(ioctl.hw_access)))
			return -EFAULT;
		if (cd->xylonfb_flags & LOGICVC_READABLE_REGS) {
			ioctl.hw_access.value =
				cd->reg_access.xylonfb_get_reg_val(
					ld->reg_base_virt, ioctl.hw_access.offset, ld);
		} else {
			ret = xylonfb_layer_reg_access(ld, cd, &ioctl.hw_access, true);
			if (ret)
				break;
		}
		if (copy_to_user(argp, &ioctl.hw_access,
			sizeof(ioctl.hw_access)))
			ret = -EFAULT;
		break;

	case XYLONFB_WRITE_HW_REG:
		driver_devel("XYLONFB_WRITE_HW_REG\n");
		if (copy_from_user(&ioctl.hw_access, argp,
			sizeof(ioctl.hw_access)))
			return -EFAULT;
		if (cd->xylonfb_flags & LOGICVC_READABLE_REGS) {
			cd->reg_access.xylonfb_set_reg_val(ioctl.hw_access.value,
				ld->reg_base_virt, ioctl.hw_access.offset, ld);
		} else {
			ret = xylonfb_layer_reg_access(ld, cd, &ioctl.hw_access, false);
			if (ret)
				break;
		}
		if (copy_to_user(argp, &ioctl.hw_access,
			sizeof(ioctl.hw_access)))
			ret = -EFAULT;
		break;

	case XYLONFB_WAIT_EDID:
		driver_devel("XYLONFB_WAIT_EDID\n");
#if defined(CONFIG_FB_XYLON_MISC)
		if (cd->xylonfb_flags & XYLONFB_FLAG_EDID_RDY)
			break;
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		if ((val == 0) || (val < 0))
			val = XYLONFB_EDID_WAIT_TOUT;
		ret = wait_event_interruptible_timeout(cd->xylonfb_misc->wait,
			(cd->xylonfb_flags & XYLONFB_FLAG_EDID_RDY), (val * HZ));
		if (ret == 0)
			return -ETIMEDOUT;
		else
			ret = 0;
#else
			return -EPERM;
#endif
		break;

	case XYLONFB_GET_EDID:
		driver_devel("XYLONFB_GET_EDID\n");
#if defined(CONFIG_FB_XYLON_MISC)
		if (cd->xylonfb_flags & XYLONFB_FLAG_EDID_RDY) {
			if (cd->xylonfb_misc->edid) {
				if (copy_to_user(argp, cd->xylonfb_misc->edid,
					XYLONFB_EDID_SIZE))
					ret = -EFAULT;
			} else {
				return -EPERM;
			}
		} else {
			return -EPERM;
		}
#else
		return -EPERM;
#endif
		break;

	default:
		driver_devel("UNKNOWN_IOCTL\n");
		ret = -EINVAL;
	}

	return ret;
}

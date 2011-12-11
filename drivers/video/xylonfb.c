/*
 * XYLON logiCVC frame buffer driver
 *
 * Author: Xylon d.o.o.
 *
 * 2002-2007 (c) MontaVista Software, Inc.
 * 2007 (c) Secret Lab Technologies, Ltd.
 * 2009 (c) Xilinx Inc.
 * 2011 (c) Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/*
 * This driver was based on skeletonfb.c and other various fb video drivers.
 */

/*
 * logiCVC frame buffer driver supports tripple buffering system per video layer.
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <asm/io.h>
#include <linux/stat.h>
#include "xylonfb.h"


/* DJ: will be removed (used only for development on non-PEP platform) */
//#define ARM8_BOARD
#define dbg(...) //printk(KERN_ERR __VA_ARGS__)

#define DRIVER_NAME "xylonfb"
#define DRIVER_DESCRIPTION "Xylon logiCVC frame buffer driver"

#define LOGICVC_USER_CONFIGURATION 0xFFFF


#define TRANSP_COLOR_8BPP_CLUT_16 0xF813
#define TRANSP_COLOR_8BPP_CLUT_24 0x00FF009C
#define TRANSP_COLOR_16BPP 0xF813
#define TRANSP_COLOR_24BPP 0x00FF009C
#define BACKGROUND_COLOR_24BPP 0x00000000

/* All logiCVC registers are 32 bit registers, at distance of 64 bit */
#define CVC_REG_DIST_USED      8                       /*  All logicvc registers are spaced at 8 bytes */
#define CVC_SHSY_FP_ROFF      (0  * CVC_REG_DIST_USED) /* R_HSY_FP */
#define CVC_SHSY_ROFF         (1  * CVC_REG_DIST_USED) /* R_HSY */
#define CVC_SHSY_BP_ROFF      (2  * CVC_REG_DIST_USED) /* R_HSY_BP */
#define CVC_SHSY_RES_ROFF     (3  * CVC_REG_DIST_USED) /* R_HSY_RES */
#define CVC_SVSY_FP_ROFF      (4  * CVC_REG_DIST_USED) /* R_VSY_FP */
#define CVC_SVSY_ROFF         (5  * CVC_REG_DIST_USED) /* R_VSY */
#define CVC_SVSY_BP_ROFF      (6  * CVC_REG_DIST_USED) /* R_VSY_BP */
#define CVC_SVSY_RES_ROFF     (7  * CVC_REG_DIST_USED) /* R_VSY_RES */
#define CVC_SCTRL_ROFF        (8  * CVC_REG_DIST_USED) /* R_CTRL */
#define CVC_SDTYPE_ROFF       (9  * CVC_REG_DIST_USED) /* R_DTYPE */
#define CVC_BACKCOL_ROFF      (10 * CVC_REG_DIST_USED) /* R_BACKGROUND */
#define CVC_DOUBLE_VBUFF_ROFF (11 * CVC_REG_DIST_USED) /* R_DOUBLE_VBUFF */
#define CVC_DOUBLE_CLUT_ROFF  (12 * CVC_REG_DIST_USED) /* R_DOUBLE_CLUT */
#define CVC_INT_ROFF          (13 * CVC_REG_DIST_USED) /* R_INT */
#define CVC_INT_MASK_ROFF     (14 * CVC_REG_DIST_USED) /* R_INT_MASK */
#define CVC_SPWRCTRL_ROFF     (15 * CVC_REG_DIST_USED) /* R_PWRCTRL */

/* CVC layer registers base and distance between the layers */
//#define CVC_LAYER_DISTANCE   (16  * CVC_REG_DIST_USED)                       /* distance between groups of layer registers */
//#define CVC_LAYER0_BASE_ROFF (32  * CVC_REG_DIST_USED)                       /* offset to the beginning of layer 0 registers */
//#define CVC_LAYER1_BASE_ROFF (CVC_LAYER0_BASE_ROFF + CVC_LAYER_DISTANCE * 1) /* offset to the beginning of layer 1 registers */
//#define CVC_LAYER2_BASE_ROFF (CVC_LAYER0_BASE_ROFF + CVC_LAYER_DISTANCE * 2) /* offset to the beginning of layer 2 registers */
//#define CVC_LAYER3_BASE_ROFF (CVC_LAYER0_BASE_ROFF + CVC_LAYER_DISTANCE * 3) /* offset to the beginning of layer 3 registers */
//#define CVC_LAYER4_BASE_ROFF (CVC_LAYER0_BASE_ROFF + CVC_LAYER_DISTANCE * 4) /* offset to the beginning of layer 4 registers */
/* CVC layer registers offsets (common for each layer) */
#define CVC_LAYER_HOR_OFF_ROFF (0 * CVC_REG_DIST_USED) /*  LH_OFFSET   */
#define CVC_LAYER_VER_OFF_ROFF (1 * CVC_REG_DIST_USED) /*  LV_OFFSET   */
#define CVC_LAYER_HOR_POS_ROFF (2 * CVC_REG_DIST_USED) /*  LH_POSITION */
#define CVC_LAYER_VER_POS_ROFF (3 * CVC_REG_DIST_USED) /*  LV_POSITION */
#define CVC_LAYER_WIDTH_ROFF   (4 * CVC_REG_DIST_USED) /*  LH_WIDTH    */
#define CVC_LAYER_HEIGHT_ROFF  (5 * CVC_REG_DIST_USED) /*  LV_HEIGHT   */
#define CVC_LAYER_ALPHA_ROFF   (6 * CVC_REG_DIST_USED) /*  ALPHA       */
#define CVC_LAYER_CTRL_ROFF    (7 * CVC_REG_DIST_USED) /*  CTRL        */
#define CVC_LAYER_TRANSP_ROFF  (8 * CVC_REG_DIST_USED) /*  TRANSPARENT */

/* CVC interrupt bits */
#define CVC_L0_VBUFF_SW_INT   0x01
#define CVC_L1_VBUFF_SW_INT   0x02
#define CVC_L2_VBUFF_SW_INT   0x04
#define CVC_L3_VBUFF_SW_INT   0x08
#define CVC_L4_VBUFF_SW_INT   0x10
#define CVC_V_SYNC_INT        0x20
#define CVC_E_VIDEO_VALID_INT 0x40
#define CVC_L0_CLUT_SW_INT    0x100
#define CVC_L1_CLUT_SW_INT    0x200
#define CVC_L2_CLUT_SW_INT    0x400
#define CVC_L3_CLUT_SW_INT    0x800
#define CVC_L4_CLUT_SW_INT    0x1000

/* CVC layer base offsets */
#define CVC_LAYER_BASE_OFFSET 0x100
#define CVC_LAYER_0_OFFSET    0
#define CVC_LAYER_1_OFFSET    0x80
#define CVC_LAYER_2_OFFSET    0x100
#define CVC_LAYER_3_OFFSET    0x180
#define CVC_LAYER_4_OFFSET    0x200

/* CVC layer CLUT base offsets */
#define CVC_CLUT_BASE_OFFSET      0x1000
#define CVC_CLUT_L0_CLUT_0_OFFSET 0
#define CVC_CLUT_L0_CLUT_1_OFFSET 0x800
#define CVC_CLUT_L1_CLUT_0_OFFSET 0x1000
#define CVC_CLUT_L1_CLUT_1_OFFSET 0x1800
#define CVC_CLUT_L2_CLUT_0_OFFSET 0x2000
#define CVC_CLUT_L2_CLUT_1_OFFSET 0x2800
#define CVC_CLUT_L3_CLUT_0_OFFSET 0x3000
#define CVC_CLUT_L3_CLUT_1_OFFSET 0x3800
#define CVC_CLUT_L4_CLUT_0_OFFSET 0x4000
#define CVC_CLUT_L4_CLUT_1_OFFSET 0x4800
#define CVC_CLUT_REGISTER_SIZE    4

/* CVC register and CLUT base offsets */
#define CVC_GENERAL_REGISTERS_RANGE 0x100
#define CVC_REGISTERS_RANGE         0x6000

/* CVC register initial values */
#define CTRL_REG_INIT 0x001F
#define TYPE_REG_INIT 0x001F

/* CVC display power signals */
#define CVC_EN_BLIGHT_MSK 0x01
#define CVC_EN_VDD_MSK    0x02
#define CVC_EN_VEE_MSK    0x04
#define CVC_V_EN_MSK      0x08

/* FB driver flags */
#define FB_DMA_BUFFER 0x01
#define FB_VSYNC_INT  0x02




struct xylonfb_vsync {
	wait_queue_head_t wait;
	unsigned int cnt;
};

struct xylonfb_layer_data {
	struct xylonfb_vsync vsync;	/* FB driver V-sync structure */
	dma_addr_t reg_base_phys;	/* Physical base address of the logiCVC registers */
	void *reg_base_virt;		/* Virtual base address of the logiCVC registers */
	unsigned long reg_range;	/* Size of the logiCVC registers area */
	dma_addr_t fb_phys; 		/* Physical base address of the frame buffer video memory */
	void *fb_virt;				/* Virtual base address of the frame buffer video memory */
	unsigned long fb_size;		/* Size of the frame buffer video memory */
	void *layer_reg_base_virt;	/* Virtual base address of the logiCVC layer registers */
	void *layer_clut_base_virt;	/* Virtual base address of the logiCVC layer CLUT registers */
	unsigned char layer_byte_pp;/* logiCVC layer bytes per pixel */
	unsigned char layer_id;		/* logiCVC layer ID */
	unsigned char layers;		/* logiCVC number of layers */
	unsigned char fb_flags;		/* FB driver flags */
};

static unsigned short cvc_layer_reg_offset[] = {
	(CVC_LAYER_BASE_OFFSET + CVC_LAYER_0_OFFSET),
	(CVC_LAYER_BASE_OFFSET + CVC_LAYER_1_OFFSET),
	(CVC_LAYER_BASE_OFFSET + CVC_LAYER_2_OFFSET),
	(CVC_LAYER_BASE_OFFSET + CVC_LAYER_3_OFFSET),
	(CVC_LAYER_BASE_OFFSET + CVC_LAYER_4_OFFSET)
};
static unsigned short cvc_clut_reg_offset[] = {
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L0_CLUT_0_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L0_CLUT_1_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L1_CLUT_0_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L1_CLUT_1_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L2_CLUT_0_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L2_CLUT_1_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L3_CLUT_0_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L3_CLUT_1_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L4_CLUT_0_OFFSET),
	(CVC_CLUT_BASE_OFFSET + CVC_CLUT_L4_CLUT_1_OFFSET)
};


/* Framebuffer driver platform data struct */
struct xylonfb_hw_platform_data
{
	unsigned long regs_baseaddr;	/* Physical address of the logiCVC hardware registers */
	unsigned long vmem_baseaddr;	/* Physical address of the layer framebuffer */
	unsigned long xres;				/* Layer resolution of screen in pixels */
	unsigned long yres;
	unsigned long xvirt;			/* Layer resolution of memory buffer in pixels */
	unsigned long yvirt;
	unsigned long row_stride;		/* Layer row stride in virtual memory. (Should be the same as xvirt) */
	unsigned char bpp;				/* Layer bits per pixel */
};

/* Default logiCVC HW platform configuration */
#ifdef ARM8_BOARD
static struct xylonfb_hw_platform_data logiCVC_platform_data[] = {
	{
		.regs_baseaddr = 0x18008000,
		.vmem_baseaddr = 0x10000000,
		.xres = 0,
		.yres = 0,
		.xvirt = 1024,
		.yvirt = 2048,
		.row_stride = 1024,
		.bpp = 8,
	},
	{
		.regs_baseaddr = 0x18008000,
		.vmem_baseaddr = 0x10200000,
		.xres = 0,
		.yres = 0,
		.xvirt = 1024,
		.yvirt = 3072,
		.row_stride = 1024,
		.bpp = 16,
	},
	{
		.regs_baseaddr = 0x18008000,
		.vmem_baseaddr = 0x10800000,
		.xres = 0,
		.yres = 0,
		.xvirt = 1024,
		.yvirt = 3072,
		.row_stride = 1024,
		.bpp = 32,
	},
};
#else /* #ifdef ARM8_BOARD (PEP HW) */
static struct xylonfb_hw_platform_data logiCVC_platform_data[] = {
	{
		.regs_baseaddr = 0x40030000,
		.vmem_baseaddr = 0x0F000000,
		.xres = 0,
		.yres = 0,
		.xvirt = 2048, // 1024,
		.yvirt = 1080, // 1024,
		.row_stride = 2048, // 1024,
		.bpp = 32, // 8,
	},
	{
		.regs_baseaddr = 0x40030000,
		.vmem_baseaddr = 0x0F100000,
		.xres = 0,
		.yres = 0,
		.xvirt = 1024,
		.yvirt = 1536,
		.row_stride = 1024,
		.bpp = 16,
	},
	{
		.regs_baseaddr = 0x40030000,
		.vmem_baseaddr = 0x0F400000,
		.xres = 0,
		.yres = 0,
		.xvirt = 1024,
		.yvirt = 1536,
		.row_stride = 1024,
		.bpp = 32,
	},
};
#endif /* #ifdef ARM8_BOARD */


/* Supported_video_modes */
#define VESA_640_480 "640x480@60"
#define VESA_800_600 "800x600@60"
#define VESA_1024_768 "1024x768@60"
#define VESA_1280_1024 "1280x1024@60"

/**
 * Structure that contains detailed data about the particular display or standard VGA resolution type. 
 */
static struct fb_videomode videomode_640x480 = {
	.refresh      = 60,
	.xres         = 640,
	.yres         = 480,
	.pixclock     = KHZ2PICOS(25152),
	.left_margin  = 48,
	.right_margin = 16,
	.upper_margin = 31,
	.lower_margin = 11,
	.hsync_len    = 96,
	.vsync_len    = 2,
	.sync         = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode        = FB_VMODE_NONINTERLACED
};

/* Xenarc 800x480 60Hz VGA monitor (used on logiTAP, running PetaLinux) */
//static struct fb_videomode videomode_800x480 = {
//	.xres           = 800,
//	.yres           = 480,
//	.pixclock       = KHZ2PICOS(31500),
//	.left_margin    = 56,
//	.right_margin   = 64,
//	.upper_margin   = 14,
//	.lower_margin   = 28,
//	.hsync_len      = 80,
//	.vsync_len      = 3,
//	.sync           = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
//	.vmode          = FB_VMODE_NONINTERLACED
//};

static struct fb_videomode videomode_800x600 = {
	.refresh      = 60,
	.xres         = 800,
	.yres         = 600,
	.pixclock     = KHZ2PICOS(39790),
	.left_margin  = 88,
	.right_margin = 40,
	.upper_margin = 23,
	.lower_margin = 1,
	.hsync_len    = 128,
	.vsync_len    = 4,
	.sync         = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode        = FB_VMODE_NONINTERLACED
};

static struct fb_videomode videomode_1024x768 = {
	.refresh      = 60,
	.xres         = 1024,
	.yres         = 768,
	.pixclock     = KHZ2PICOS(65076),
	.left_margin  = 160,
	.right_margin = 24,
	.upper_margin = 29,
	.lower_margin = 3,
	.hsync_len    = 136,
	.vsync_len    = 6,
	.sync         = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode        = FB_VMODE_NONINTERLACED
};

static struct fb_videomode videomode_1280x1024 = {
	.refresh      = 60,
	.xres         = 1024,
	.yres         = 768,
	.pixclock     = KHZ2PICOS(108065),
	.left_margin  = 160,
	.right_margin = 24,
	.upper_margin = 29,
	.lower_margin = 3,
	.hsync_len    = 136,
	.vsync_len    = 6,
	.sync         = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode        = FB_VMODE_NONINTERLACED
};


struct video_mode_parameters
{
	unsigned int VESA_code; /* VESA code. 0 if display isn't in VESA standard */
	unsigned int bpp; /* Bits per pixel(8, 16, 24 supported) */
	struct fb_videomode *vmode_data; /* Video mode parameters */
};

struct active_video_mode_parameters
{
	unsigned int bpp; /* Bits per pixel(8, 16, 24 supported) */
	unsigned int power_on_delay; /* Delay after applying display power and before applying display signals */
	unsigned int signal_on_delay; /* Delay after applying display signal and before applying display backlight power supply */
	struct fb_videomode vmode_data; /* Video mode parameters */
};

enum supported_video_modes {
	VESA_640_480_8 = 0,
	VESA_640_480_16,
	VESA_640_480_32,
	VESA_800_600_8,
	VESA_800_600_16,
	VESA_800_600_32,
	VESA_1024_768_8,
	VESA_1024_768_16,
	VESA_1024_768_32,
	VESA_1280_1024_8,
	VESA_1280_1024_16,
	VESA_1280_1024_32,
	NUM_OF_VIDEO_MODES,
	DEFAULT_VIDEO_MODE = VESA_640_480_32
};

static struct video_mode_parameters const video_modes[] = {
	/* 640 x 480 @ 60Hz */
	{
		XYLONFB_VM_VESA_640_480_8,
		8,
		&videomode_640x480,
	},
	{
		XYLONFB_VM_VESA_640_480_16,
		16,
		&videomode_640x480,
	},
	{
		XYLONFB_VM_VESA_640_480_32,
		32,
		&videomode_640x480,
	},
	/* 800 x 600 @ 60Hz */
	{
		XYLONFB_VM_VESA_800_600_8,
		8,
		&videomode_800x600,
	},
	{
		XYLONFB_VM_VESA_800_600_16,
		16,
		&videomode_800x600,
	},
	{
		XYLONFB_VM_VESA_800_600_32,
		32,
		&videomode_800x600,
	},
	/* 1024 x 768 @ 60Hz */
	{
		XYLONFB_VM_VESA_1024_768_8,
		8,
		&videomode_1024x768,
	},
	{
		XYLONFB_VM_VESA_1024_768_16,
		16,
		&videomode_1024x768,
	},
	{
		XYLONFB_VM_VESA_1024_768_32,
		32,
		&videomode_1024x768,
	},
	/* 1280 x 1024 @ 60Hz */
	{
		XYLONFB_VM_VESA_1280_1024_8,
		8,
		&videomode_1280x1024,
	},
	{
		XYLONFB_VM_VESA_1280_1024_16,
		16,
		&videomode_1280x1024,
	},
	{
		XYLONFB_VM_VESA_1280_1024_32,
		32,
		&videomode_1280x1024,
	}
};


/* Active video mode (parameters are changeable) */
static struct active_video_mode_parameters active_video_mode;

/* Platform init input parameters */
static unsigned long regs_baseaddr;
static unsigned long vmem_baseaddr;
static unsigned long virt_hres;
static unsigned long virt_vres;
static unsigned long row_stride;
/* Video mode init input parameters */
static unsigned long video_mode_code;
static unsigned long hfp;
static unsigned long hsync;
static unsigned long hbp;
static unsigned long hres;
static unsigned long vfp;
static unsigned long vsync;
static unsigned long vbp;
static unsigned long vres;
static unsigned long pix_clk;
static unsigned long bpp;
static unsigned long power_on_delay;
static unsigned long signal_on_delay;
static unsigned long startup_layer;

/* Xylon platform parameters */
module_param(regs_baseaddr, ulong, S_IRUGO | S_IWUSR);
module_param(vmem_baseaddr, ulong, S_IRUGO | S_IWUSR);
module_param(virt_hres, ulong, S_IRUGO | S_IWUSR);
module_param(virt_vres, ulong, S_IRUGO | S_IWUSR);
module_param(row_stride, ulong, S_IRUGO | S_IWUSR);
/* Video mode parameters */
module_param(video_mode_code, ulong, S_IRUGO | S_IWUSR);
module_param(hfp, ulong, S_IRUGO | S_IWUSR);
module_param(hsync, ulong, S_IRUGO | S_IWUSR);
module_param(hbp, ulong, S_IRUGO | S_IWUSR);
module_param(hres, ulong, S_IRUGO | S_IWUSR);
module_param(vfp, ulong, S_IRUGO | S_IWUSR);
module_param(vsync, ulong, S_IRUGO | S_IWUSR);
module_param(vbp, ulong, S_IRUGO | S_IWUSR);
module_param(vres, ulong, S_IRUGO | S_IWUSR);
module_param(pix_clk, ulong, S_IRUGO | S_IWUSR);
module_param(bpp, ulong, S_IRUGO | S_IWUSR);
module_param(power_on_delay, ulong, S_IRUGO | S_IWUSR);
module_param(signal_on_delay, ulong, S_IRUGO | S_IWUSR);
module_param(startup_layer, ulong, S_IRUGO | S_IWUSR);

static u32 xylonfb_pseudo_palette[16];
atomic_t xylonfb_use_ref;



//static irqreturn_t xylonfb_irq(int irq, void *dev_id)
//{
//	struct fb_info *fbi = (struct fb_info *)dev_id;
//	struct xylonfb_layer_data *layer_data = fbi->par;
//	u32 isr;
//
//	isr = readl(layer_data->reg_base_virt + CVC_INT_ROFF);
//
//	if (isr & CVC_V_SYNC_INT) {
//		writel(layer_data->reg_base_virt + CVC_INT_ROFF, CVC_V_SYNC_INT);
//		layer_data->vsync.cnt++;
//		layer_data->fb_flags |= FB_VSYNC_INT;
//		wake_up_interruptible(&layer_data->vsync.wait);
//	}
//
//	return IRQ_HANDLED;
//}


static int xylonfb_open(struct fb_info *fbi, int user)
{
	dbg("%s", __func__);

	atomic_inc(&xylonfb_use_ref);

	return 0;
}

static int xylonfb_release(struct fb_info *fbi, int user)
{
	dbg("%s", __func__);

	atomic_dec(&xylonfb_use_ref);

	return 0;
}

static int xylonfb_set_color_reg(unsigned regno, unsigned red, unsigned green,
			unsigned blue, unsigned transp, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	u32 clut_value = 0;

	dbg("%s", __func__);

	if (fbi->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		if (regno >= 256)
			return -EINVAL;

		/* for now supported only 32bpp CLUT */
		if (fbi->var.bits_per_pixel == 8)
			clut_value =
				(((u8)transp<<24) | ((u8)red<<16) | ((u8)green<<8) | (u8)blue);
		/* logiCVC supports 16bpp CLUT also */
		else if (fbi->var.bits_per_pixel == 16)
			clut_value =
				(((u8)(transp&0x3F)<<24) | ((u8)(red&0xF8)<<16) |
				((u8)(green&0xFC)<<8) | (u8)(blue&0xF8));
		writel(clut_value,
			layer_data->layer_clut_base_virt +
				(regno * CVC_CLUT_REGISTER_SIZE));
	} else {
		if (regno >= 16)
			return -EINVAL;

		((u32 *)(fbi->pseudo_palette))[regno] =
			(red & 0xf800) | ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
	}

	return 0;
}

static int xylonfb_set_cmap(struct fb_cmap *cmap, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	u32 clut_value;
	int i;

	dbg("%s", __func__);

	if (fbi->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		if (cmap->start >= 256 || cmap->len >= 256)
			return -EINVAL;

		if (fbi->var.bits_per_pixel == 8) {
			/* for now supported only 32bpp CLUT */
			for (i = cmap->start; i < cmap->len; i++) {
				clut_value =
					(((u8)cmap->transp[i]<<24) | ((u8)cmap->red[i]<<16) |
					((u8)cmap->green[i]<<8) | (u8)cmap->blue[i]);
				writel(clut_value,
					layer_data->layer_clut_base_virt + (i * CVC_CLUT_REGISTER_SIZE));
			}
		} else if (fbi->var.bits_per_pixel == 16) {
			/* logiCVC supports 16bpp CLUT also */
			for (i = cmap->start; i < cmap->len; i++) {
				clut_value = (((u8)(cmap->transp[i]&0x3F)<<24) |
					((u8)(cmap->red[i]&0xF8)<<16) |
					((u8)(cmap->green[i]&0xFC)<<8) |
					(u8)(cmap->blue[i]&0xF8));
				writel(clut_value,
					layer_data->layer_clut_base_virt + (i*CVC_CLUT_REGISTER_SIZE));
			}
		}
	} else {
		if (cmap->start > 16 || cmap->len > 16)
			return -EINVAL;

		for (i = cmap->start; i < cmap->len; i++) {
			((u32 *)(fbi->pseudo_palette))[i] =	
				0xFF000000 | 
				((cmap->red[i] & 0x00FF) << 16) | 
				((cmap->green[i] & 0x00FF) << 8) | 
				((cmap->blue[i] & 0x00FF));
		}
	}

	return 0;
}

static int xylonfb_blank(int blank_mode, struct fb_info *fbi)
{
	dbg("%s", __func__);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
	case FB_BLANK_NORMAL:
//		memset_io((void __iomem *)fbi->screen_base, 0, fbi->screen_size);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		break;
	default:
		return -EINVAL;
	}

	/* let fbcon do a soft blank for us */
	return (blank_mode == FB_BLANK_NORMAL) ? 1 : 0;
}

static int xylonfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	dbg("%s", __func__);

	if (fbi->var.xoffset == var->xoffset && fbi->var.yoffset == var->yoffset)
		return 0;

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0 ||
			var->yoffset >= fbi->var.yres_virtual ||
			var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset + var->xres > fbi->var.xres_virtual ||
			var->yoffset + var->yres > fbi->var.yres_virtual)
			return -EINVAL;
	}
	fbi->var.xoffset = var->xoffset;
	fbi->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		fbi->var.vmode |= FB_VMODE_YWRAP;
	else
		fbi->var.vmode &= ~FB_VMODE_YWRAP;
	/* set HW memory X offset */
	writel(var->xoffset, (layer_data->layer_reg_base_virt + CVC_LAYER_HOR_OFF_ROFF));
	/* set HW memory Y offset */
	writel(var->yoffset, (layer_data->layer_reg_base_virt + CVC_LAYER_VER_OFF_ROFF));
	/* Apply changes */
	writel((var->yres-1), (layer_data->layer_reg_base_virt + CVC_LAYER_VER_POS_ROFF));

	return 0;
}

static int xylonfb_get_vblank(struct fb_vblank *vblank, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	u32 isr;

	dbg("%s", __func__);

	isr = readl(layer_data->reg_base_virt + CVC_INT_ROFF);
	if (isr & CVC_V_SYNC_INT)
		vblank->flags |= (FB_VBLANK_VSYNCING | FB_VBLANK_HAVE_VSYNC);
	else
		vblank->flags |= FB_VBLANK_HAVE_VSYNC;

	return 0;
}

static int xylonfb_wait_for_vsync(u32 crt, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	u32 imr;
	int ret;

	dbg("%s", __func__);

	/* enable CVC V-sync interrupt */
	imr = readl(layer_data->reg_base_virt + CVC_INT_MASK_ROFF);
	imr &= (~CVC_V_SYNC_INT);
	writel(layer_data->reg_base_virt + CVC_INT_MASK_ROFF, imr);

	ret = wait_event_interruptible_timeout(layer_data->vsync.wait,
			(layer_data->fb_flags & FB_VSYNC_INT), HZ/10);

	/* disable CVC V-sync interrupt */
	imr |= CVC_V_SYNC_INT;
	writel(layer_data->reg_base_virt + CVC_INT_MASK_ROFF, imr);

	if (ret < 0)
		return ret;
	else if (ret == 0) {
		return -ETIMEDOUT;
	} else
		layer_data->fb_flags &= (~FB_VSYNC_INT);

	return 0;
}

static int xylonfb_ioctl(struct fb_info *fbi, unsigned int cmd,
			unsigned long arg)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct fb_vblank vblank;
	struct fb_con2fbmap con2fb;
	struct fb_cmap cmap_from;
	struct fb_cmap cmap;
	struct fb_event event;
	void __user *argp = (void __user *)arg;
	u32 crt;
	long ret = 0;

	dbg("%s", __func__);

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

	case FBIOPUTCMAP:
		dbg("FBIOPUTCMAP\n");
		if (fbi->var.bits_per_pixel != 8)
			return -EINVAL;

		if (copy_from_user(&cmap, argp, sizeof(cmap)))
			return -EFAULT;
		ret = fb_set_cmap(&cmap, fbi);
		break;

	case FBIOGETCMAP:
		dbg("FBIOGETCMAP\n");
		if (fbi->var.bits_per_pixel != 8)
			return -EINVAL;

		if (copy_from_user(&cmap, argp, sizeof(cmap)))
			return -EFAULT;
		if (!lock_fb_info(fbi))
			return -ENODEV;
		cmap_from = fbi->cmap;
		unlock_fb_info(fbi);
		ret = fb_copy_cmap(&cmap_from, &cmap);
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
		ret = -EINVAL;
		break;

	case FBIOGET_CON2FBMAP:
		dbg("FBIOGET_CON2FBMAP\n");
		if (copy_from_user(&con2fb, argp, sizeof(con2fb)))
			return -EFAULT;
		if (con2fb.console < 1 || con2fb.console > MAX_NR_CONSOLES)
			return -EINVAL;
		con2fb.framebuffer = -1;
		event.data = &con2fb;
		if (!lock_fb_info(fbi))
			return -ENODEV;
		event.info = fbi;
		fb_notifier_call_chain(FB_EVENT_GET_CONSOLE_MAP, &event);
		unlock_fb_info(fbi);
		ret = copy_to_user(argp, &con2fb, sizeof(con2fb)) ? -EFAULT : 0;
		break;

	case FBIOPUT_CON2FBMAP:
		dbg("FBIOPUT_CON2FBMAP\n");
		if (copy_from_user(&con2fb, argp, sizeof(con2fb)))
			return -EFAULT;
		if (con2fb.console < 1 || con2fb.console > MAX_NR_CONSOLES)
			return -EINVAL;
		if (con2fb.framebuffer < 0 || con2fb.framebuffer >= FB_MAX)
			return -EINVAL;
		if (!registered_fb[con2fb.framebuffer])
			request_module("fb%d", con2fb.framebuffer);
		if (!registered_fb[con2fb.framebuffer]) {
			ret = -EINVAL;
			break;
		}
		event.data = &con2fb;
		if (!lock_fb_info(fbi))
			return -ENODEV;
		event.info = fbi;
		ret = fb_notifier_call_chain(FB_EVENT_SET_CONSOLE_MAP, &event);
		unlock_fb_info(fbi);
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
	.fb_check_var = NULL,
	.fb_set_par = NULL,
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


static void xylonfb_hw_start(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	u32 val;
	int i;

	dbg("%s", __func__);

	writel(active_video_mode.vmode_data.right_margin-1,
		layer_data->reg_base_virt + CVC_SHSY_FP_ROFF);
	writel(active_video_mode.vmode_data.hsync_len-1,
		layer_data->reg_base_virt + CVC_SHSY_ROFF);
	writel(active_video_mode.vmode_data.left_margin-1,
		layer_data->reg_base_virt + CVC_SHSY_BP_ROFF);
	writel(active_video_mode.vmode_data.xres-1,
		layer_data->reg_base_virt + CVC_SHSY_RES_ROFF);
	writel(active_video_mode.vmode_data.lower_margin-1,
		layer_data->reg_base_virt + CVC_SVSY_FP_ROFF);
	writel(active_video_mode.vmode_data.vsync_len-1,
		layer_data->reg_base_virt + CVC_SVSY_ROFF);
	writel(active_video_mode.vmode_data.upper_margin-1,
		layer_data->reg_base_virt + CVC_SVSY_BP_ROFF);
	writel(active_video_mode.vmode_data.yres-1,
		layer_data->reg_base_virt + CVC_SVSY_RES_ROFF);
#ifdef ARM8_BOARD
	val = readl(layer_data->reg_base_virt + CVC_SCTRL_ROFF);
	val |= CTRL_REG_INIT;
	writel(val, layer_data->reg_base_virt + CVC_SCTRL_ROFF);
#else /* #ifdef ARM8_BOARD */
	writel(CTRL_REG_INIT, layer_data->reg_base_virt + CVC_SCTRL_ROFF);
#endif /* #ifdef ARM8_BOARD */
	writel(TYPE_REG_INIT, layer_data->reg_base_virt + CVC_SDTYPE_ROFF);
	writel(0xFFFFFFFF, layer_data->reg_base_virt + CVC_BACKCOL_ROFF);
//	writel(0x00, layer_data->reg_base_virt + CVC_DOUBLE_VBUFF_ROFF);
//	writel(0x00, layer_data->reg_base_virt + CVC_DOUBLE_CLUT_ROFF);
	writel(0xFFFF, layer_data->reg_base_virt + CVC_INT_ROFF);
	writel(0xFFFF, layer_data->reg_base_virt + CVC_INT_MASK_ROFF);
	writel(TRANSP_COLOR_24BPP,
		(layer_data->layer_reg_base_virt + CVC_LAYER_TRANSP_ROFF));
	/* Display power control */
	val = CVC_EN_VDD_MSK;
	writel(val, layer_data->reg_base_virt + CVC_SPWRCTRL_ROFF);
	mdelay(active_video_mode.power_on_delay);
	val |= CVC_V_EN_MSK;
	writel(val, layer_data->reg_base_virt + CVC_SPWRCTRL_ROFF);
	mdelay(active_video_mode.signal_on_delay);
	val |= CVC_EN_BLIGHT_MSK;
	writel(val, layer_data->reg_base_virt + CVC_SPWRCTRL_ROFF);
	/* Turn logiCVC ON - make layer visible on screen */
	writel(1, (layer_data->layer_reg_base_virt + CVC_LAYER_CTRL_ROFF));

	printk(KERN_INFO "logiCVC HW parameters:\n");
	printk(KERN_INFO "    Horizontal Front Porch: %d pixclks\n",
		active_video_mode.vmode_data.right_margin);
	printk(KERN_INFO "    Horizontal Sync:        %d pixclks\n",
		active_video_mode.vmode_data.hsync_len);
	printk(KERN_INFO "    Horizontal Back Porch:  %d pixclks\n",
		active_video_mode.vmode_data.left_margin);
	printk(KERN_INFO "    Vertical Front Porch:   %d pixclks\n",
		active_video_mode.vmode_data.lower_margin);
	printk(KERN_INFO "    Vertical Sync:          %d pixclks\n",
		active_video_mode.vmode_data.vsync_len);
	printk(KERN_INFO "    Vertical Back Porch:    %d pixclks\n",
		active_video_mode.vmode_data.upper_margin);
	printk(KERN_INFO "    Pixel Clock:            %d\n",
		active_video_mode.vmode_data.pixclock);
	printk(KERN_INFO "    Bits per Pixel:         %d\n",
		active_video_mode.bpp);
	printk(KERN_INFO "    Horizontal Res:         %d\n",
		active_video_mode.vmode_data.xres);
	printk(KERN_INFO "    Vertical Res:           %d\n",
		active_video_mode.vmode_data.yres);
	printk(KERN_INFO "\n");
	printk(KERN_INFO "logiCVC layer parameters:\n");
	for (i = 0; i < ARRAY_SIZE(logiCVC_platform_data); i++) {
		printk(KERN_INFO "logiCVC layer %d\n", i);
		printk(KERN_INFO "    Registers Base Address:     0x%X\n",
			(unsigned int)logiCVC_platform_data[i].regs_baseaddr);
		printk(KERN_INFO "    Layer Video Memory Address: 0x%X\n",
			(unsigned int)logiCVC_platform_data[i].vmem_baseaddr);
		printk(KERN_INFO "    X resolution:               %ld\n",
			logiCVC_platform_data[i].xres);
		printk(KERN_INFO "    Y resolution:               %ld\n",
			logiCVC_platform_data[i].yres);
		printk(KERN_INFO "    X resolution (virtual):     %ld\n",
			logiCVC_platform_data[i].xvirt);
		printk(KERN_INFO "    Y resolution (virtual):     %ld\n",
			logiCVC_platform_data[i].yvirt);
		printk(KERN_INFO "    Row stride:                 %ld\n",
			logiCVC_platform_data[i].row_stride);
		printk(KERN_INFO "    Bits per Pixel:             %d\n",
			logiCVC_platform_data[i].bpp);
		printk(KERN_INFO "\n");
	}
}

static int xylonfb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct xylonfb_hw_platform_data *pdata;
	struct fb_info **afbi;
	struct fb_info *fbi;
	struct xylonfb_layer_data *layer_data;
	int layers;
	int i, rc, regfb, on;

	dbg("%s", __func__);

	layers = 1; // ARRAY_SIZE(logiCVC_platform_data);

	afbi = kzalloc(sizeof(struct fb_info *) * layers, GFP_KERNEL);
	if (!afbi) {
		dev_err(dev, "Error allocate xylonfb internals\n");
		return -ENOMEM;
	}

	layer_data = NULL;

	for (i = layers-1; i >= 0; i--) {
		/* set register flag to invalid value */
		regfb = -1;

		fbi = framebuffer_alloc(sizeof(struct xylonfb_layer_data), dev);
		if (!fbi) {
			dev_err(dev, "Error allocate xylonfb info\n");
			rc = -ENOMEM;
			goto err_fb;
		}
		afbi[i] = fbi;
		layer_data = fbi->par;

		if (pdev->dev.platform_data)
			pdata = pdev->dev.platform_data;
		else
			pdata = &logiCVC_platform_data[i];

		/* logiCVC register mapping */
		layer_data->reg_base_phys = pdata->regs_baseaddr;
		layer_data->reg_range = CVC_REGISTERS_RANGE;
		layer_data->reg_base_virt =
			ioremap_nocache(layer_data->reg_base_phys, layer_data->reg_range);
		/* VMEM mapping */
		layer_data->fb_phys = pdata->vmem_baseaddr;
		layer_data->fb_size = pdata->xvirt * (pdata->bpp / 8) * pdata->yvirt;
		if (layer_data->fb_flags & FB_DMA_BUFFER) {
			/* NOT USED FOR NOW! */
			layer_data->fb_virt = dma_alloc_writecombine(dev,
				PAGE_ALIGN(layer_data->fb_size),
				&layer_data->fb_phys, GFP_KERNEL);
		} else {
			layer_data->fb_virt =
				ioremap_wc(layer_data->fb_phys, layer_data->fb_size);
		}
		/* check IO mappings */
		if (!layer_data->reg_base_virt || !layer_data->fb_virt) {
			dev_err(dev, "Error xylonfb ioremap REGS 0x%X FB 0x%X\n",
				(unsigned int)layer_data->reg_base_virt,
				(unsigned int)layer_data->fb_virt);
			rc = -ENOMEM;
			goto err_fb;
		}
//		memset_io((void __iomem *)layer_data->fb_virt, 0, layer_data->fb_size);
		layer_data->layer_reg_base_virt =
			layer_data->reg_base_virt + cvc_layer_reg_offset[i];
		layer_data->layer_clut_base_virt =
			layer_data->reg_base_virt + cvc_clut_reg_offset[i];
		layer_data->layer_byte_pp = pdata->bpp / 8;
		layer_data->layer_id = i;
		layer_data->layers = layers;

		printk(KERN_INFO \
			"Registers base address 0x%X\n" \
			"Registers range 0x%X\n" \
			"Layer registers base address 0x%X\n" \
			"Layer CLUT registers base address 0x%X\n" \
			"Layer bytes per Pixel %d\n" \
			"Layer ID %d\n" \
			"FB address 0x%X\n" \
			"FB size %ld\n", \
			(unsigned int)layer_data->reg_base_virt, (unsigned int)layer_data->reg_range,
			(unsigned int)layer_data->layer_reg_base_virt, (unsigned int)layer_data->layer_clut_base_virt,
			layer_data->layer_byte_pp, layer_data->layer_id,
			(unsigned int)layer_data->fb_virt, layer_data->fb_size);

		fbi->flags = FBINFO_DEFAULT;
		fbi->screen_base = (char __iomem *)layer_data->fb_virt;
		fbi->screen_size = layer_data->fb_size;
		fbi->pseudo_palette = xylonfb_pseudo_palette;
		fbi->fbops = &xylonfb_ops;

		sprintf(fbi->fix.id, "Xylon FB%d", i);
		fbi->fix.smem_start = layer_data->fb_phys;
		fbi->fix.smem_len = layer_data->fb_size;
		fbi->fix.type = FB_TYPE_PACKED_PIXELS;
		if (pdata->bpp == 8 || pdata->bpp == 16)
			fbi->fix.visual = FB_VISUAL_DIRECTCOLOR;
		else
			fbi->fix.visual = FB_VISUAL_TRUECOLOR;
		fbi->fix.xpanstep = 1;
		fbi->fix.ypanstep = 1;
		fbi->fix.ywrapstep = 2048;
		fbi->fix.line_length = pdata->xvirt * (pdata->bpp / 8);
		fbi->fix.mmio_start = layer_data->reg_base_phys;
		fbi->fix.mmio_len = CVC_REGISTERS_RANGE;
		fbi->fix.accel = FB_ACCEL_NONE;

		fbi->var.xres = pdata->xres;
		fbi->var.yres = pdata->yres;
		fbi->var.xres_virtual = pdata->xvirt;
		fbi->var.yres_virtual = pdata->yvirt;
		fbi->var.bits_per_pixel = pdata->bpp;
		fbi->var.transp.offset = 24;
		fbi->var.transp.length = 8;
		fbi->var.transp.msb_right = 0;
		fbi->var.red.offset = 16;
		fbi->var.red.length = 8;
		fbi->var.red.msb_right = 0;
		fbi->var.green.offset = 8;
		fbi->var.green.length = 8;
		fbi->var.green.msb_right = 0;
		fbi->var.blue.offset = 0;
		fbi->var.blue.length = 8;
		fbi->var.blue.msb_right = 0;
		fbi->var.activate = FB_ACTIVATE_NOW;
		fbi->var.height = 0;
		fbi->var.width = 0;
		fbi->var.pixclock = active_video_mode.vmode_data.pixclock;
		fbi->var.left_margin = active_video_mode.vmode_data.left_margin;
		fbi->var.right_margin = active_video_mode.vmode_data.right_margin;
		fbi->var.upper_margin = active_video_mode.vmode_data.upper_margin;
		fbi->var.lower_margin = active_video_mode.vmode_data.lower_margin;
		fbi->var.hsync_len = active_video_mode.vmode_data.hsync_len;
		fbi->var.vsync_len = active_video_mode.vmode_data.vsync_len;
		fbi->var.sync = 0;
		fbi->var.vmode = FB_VMODE_NONINTERLACED;
		fbi->var.rotate = 0;

		if (fb_alloc_cmap(&fbi->cmap, 256, 1)) {
			rc = -ENOMEM;
			goto err_fb;
		}

		regfb = register_framebuffer(fbi);
		if (regfb) {
			printk(KERN_ERR "Error registering xylonfb %d\n", i);
			goto err_fb;
		} else
			printk(KERN_INFO "xylonfb %d registered\n", i);

		init_waitqueue_head(&layer_data->vsync.wait);
	}

//	if (request_irq(irq, xylonfb_irq, IRQF_SHARED, "xylonfb", fbi))
//		goto err_fb;

	atomic_set(&xylonfb_use_ref, 0);
	dev_set_drvdata(dev, (void *)afbi);

	/* start logiCVC HW */
	if (startup_layer == 0) {
		for (i = 0; i < layers; i++)
			if (afbi[i]->fix.visual == FB_VISUAL_TRUECOLOR)
				break;
	} else {
		i = startup_layer - 1;
	}
	if (i < layers) {
		/* start 32-bit layer */
		xylonfb_hw_start(afbi[i]);
		/* Turn OFF unused layers */
		on = i;
		for (i = 0; i < layers; i++) {
			if (i == on)
				continue;
			fbi = afbi[i];
			layer_data = (struct xylonfb_layer_data *)fbi->par;
			writel(0, (layer_data->layer_reg_base_virt + CVC_LAYER_CTRL_ROFF));
		}
	} else
		printk(KERN_ERR "No 32-bit logiCVC layer found!\nxylonfb disabled\n");

	return 0;

err_fb:
	for (; i < layers; i++) {
		fbi = afbi[i];
		if (fbi)
			layer_data = fbi->par;

		if (regfb == 0)
			unregister_framebuffer(fbi);
		else
			regfb = 0;
		if (fbi->cmap.red)
			fb_dealloc_cmap(&fbi->cmap);
		if (layer_data) {
			if (layer_data->fb_flags & FB_DMA_BUFFER) {
				/* NOT USED FOR NOW! */
		 		dma_free_coherent(dev, PAGE_ALIGN(fbi->fix.smem_len),
					layer_data->fb_virt, layer_data->fb_phys);
			} else {
				if (layer_data->fb_virt)
					iounmap(layer_data->fb_virt);
			}
			if (layer_data->reg_base_virt)
				iounmap(layer_data->reg_base_virt);
			framebuffer_release(fbi);
		}
	}

	dev_set_drvdata(dev, NULL);

	return rc;
}

static int xylonfb_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fb_info **afbi = (struct fb_info **)dev_get_drvdata(dev);
	struct fb_info *fbi;
	struct xylonfb_layer_data *layer_data;
	int i, use;
	bool cvc_off = 0;

	dbg("%s", __func__);

	use = atomic_read(&xylonfb_use_ref);
	if (use) {
		printk(KERN_ERR "xylonfb driver is in use\n");
		return -EINVAL;
	}

	for (i = ARRAY_SIZE(logiCVC_platform_data)-1; i >= 0; i--) {
		fbi = afbi[i];
		layer_data = fbi->par;
		if (!cvc_off) {
			/* disable logiCVC */
			writel(0, layer_data->reg_base_virt + CVC_SCTRL_ROFF);
			cvc_off = 1;
		}
		unregister_framebuffer(fbi);
		fb_dealloc_cmap(&fbi->cmap);
		if (layer_data->fb_flags & FB_DMA_BUFFER) {
	 		dma_free_coherent(dev, PAGE_ALIGN(fbi->fix.smem_len),
				layer_data->fb_virt, layer_data->fb_phys);
		} else {
			iounmap(layer_data->fb_virt);
		}
		iounmap(layer_data->reg_base_virt);
		framebuffer_release(fbi);
	}

	dev_set_drvdata(dev, NULL);

	return 0;
}

static struct platform_driver xylonfb_driver = {
	.probe = xylonfb_probe,
	.remove = xylonfb_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
	},
};


static struct platform_device xylonfb_device = {
	.name = DRIVER_NAME,
	.id = -1,
};


/* Set initial display parameters.
   First check if video mode (input parameter) is set, if it's not, then set it to default mode.
   After the video mode is set, check if any of the input parameters is set and change that parameter
   in already selected mode. This alows us to set new mode without need to set all parameters for it,
   just choose similar mode and change parameters that needs to be changed to get desired mode.  
*/
static void __init xylonfb_set_params(void)
{
	int i;
	bool video_mode_set = 0;

	dbg("%s", __func__);

	/* Set video mode */
	if (0 != video_mode_code) {
		for (i = 0; i < ARRAY_SIZE(video_modes); i++) {
			if (video_modes[i].VESA_code == video_mode_code) {
				active_video_mode.bpp = video_modes[i].bpp;
				active_video_mode.vmode_data = *video_modes[i].vmode_data;
				video_mode_set = 1;
				break;
			}
		}
	}

	if (0 == video_mode_set) {
		if (0 == video_mode_code) {
			/* Set default video mode */
			active_video_mode.bpp = video_modes[DEFAULT_VIDEO_MODE].bpp;
			active_video_mode.vmode_data =
				*video_modes[DEFAULT_VIDEO_MODE].vmode_data;
		} else if (LOGICVC_USER_CONFIGURATION == video_mode_code) {
			/* Set video mode with input parameters */
			if(0 != hfp)
				active_video_mode.vmode_data.right_margin = hfp;
			if(0 != hsync)
				active_video_mode.vmode_data.hsync_len = hsync;
			if(0 != hbp)
				active_video_mode.vmode_data.left_margin = hbp;
			if(0 != hres)
				active_video_mode.vmode_data.xres = hres;
			if(0 != vfp)
				active_video_mode.vmode_data.lower_margin = vfp;
			if(0 != vsync)
				active_video_mode.vmode_data.vsync_len = vsync;
			if(0 != vbp)
				active_video_mode.vmode_data.upper_margin = vbp;
			if(0 != vres)
				active_video_mode.vmode_data.yres = vres;
			if(0 != pix_clk)
				active_video_mode.vmode_data.pixclock = pix_clk;
			if(0 != bpp)
				active_video_mode.bpp = bpp;
		}
	}

	/* Set logiCVC HW platform parameters */
	for (i = 0; i < ARRAY_SIZE(logiCVC_platform_data); i++) {
		if(0 != hres)
			logiCVC_platform_data[i].xres = hres;
		else
			logiCVC_platform_data[i].xres = active_video_mode.vmode_data.xres;
		if(0 != vres)
			logiCVC_platform_data[i].yres = vres;
		else
			logiCVC_platform_data[i].yres = active_video_mode.vmode_data.yres;
		if(0 != virt_hres)
			logiCVC_platform_data[i].xvirt = virt_hres;
		if(0 != virt_vres)
			logiCVC_platform_data[i].yvirt = virt_vres;
		if(0 != row_stride)
			logiCVC_platform_data[i].row_stride = row_stride;
		if(0 != regs_baseaddr)
			logiCVC_platform_data[i].regs_baseaddr = regs_baseaddr;
		if(0 != vmem_baseaddr)
			logiCVC_platform_data[i].vmem_baseaddr = vmem_baseaddr;
	}
}

#ifdef ARM8_BOARD
static void __init config_clk(void)
{
#define CLOCK_REGISTERS_BASEADDR 0x18007000
#define CLOCK_REGISTERS_RANGE 0x18
#define GPOUT_REG_OFF 0x10
#define PWM_REG_OFF 0x14
#define PWR_REG_DISPLAY_POWER_MSK 1
#define PWR_REG_BACKLIGHT_POWER_MSK 2
#define BACKLIGHT_MAX_VALUE 255
	void *clk_reg_base_virt;
	void *cvc_reg_base_virt;
	u32 val;

	dbg("%s", __func__);

	clk_reg_base_virt =
		ioremap_nocache(CLOCK_REGISTERS_BASEADDR, CLOCK_REGISTERS_RANGE);
	cvc_reg_base_virt =
		ioremap_nocache(logiCVC_platform_data[0].regs_baseaddr, CVC_GENERAL_REGISTERS_RANGE);

	/*
		Available display resolutions:
		0x0000 - 640x480
		0x2000 - 800x600
		0x4000 - 1024x768
		0x6000 - 1280x1024
	*/
	/* reset FPGA */
	writel(0x01000000, clk_reg_base_virt + GPOUT_REG_OFF);
	udelay(100);
	/* set logiCVC input clock divider */
	writel(0x2000, cvc_reg_base_virt + CVC_SCTRL_ROFF);
	udelay(100);
	val = readl(cvc_reg_base_virt + CVC_SCTRL_ROFF);
	val |= 0x8000;
	/* set VCLKSEL2 bit */
	writel(val, cvc_reg_base_virt + CVC_SCTRL_ROFF);
	val &= ~0x8000;
	/* set video PLL reset */
	writel(0x01000000, clk_reg_base_virt);
	udelay(10);
	/* clear VCLKSEL2 bit */
	writel(val, cvc_reg_base_virt + CVC_SCTRL_ROFF);
	udelay(10);
	/* release video PLL reset */
	writel(0, clk_reg_base_virt);
	udelay(10);

	writel(PWR_REG_DISPLAY_POWER_MSK, clk_reg_base_virt + GPOUT_REG_OFF);

	val = readl(clk_reg_base_virt + GPOUT_REG_OFF);
	writel((val | PWR_REG_BACKLIGHT_POWER_MSK),
		clk_reg_base_virt + GPOUT_REG_OFF);
	writel((BACKLIGHT_MAX_VALUE / 2), clk_reg_base_virt + PWM_REG_OFF);

	iounmap(cvc_reg_base_virt);
	iounmap(clk_reg_base_virt);
}
#endif /* #ifdef ARM8_BOARD */

static int __init xylonfb_setup(char *options)
{
	char *this_opt;

	dbg("%s", __func__);

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
	}

	return 0;
}

static void xylonfb_dev_release(struct device *dev)
{
	dbg("%s", __func__);
}

static int __init xylonfb_init(void)
{
	char *option = NULL;
	int ret;

	dbg("%s", __func__);

	/*
	 *  For kernel boot options (in 'video=xxxfb:<options>' format)
	 */
	if (fb_get_options(DRIVER_NAME, &option))
		return -ENODEV;

	/* Set internal module parameters */
	xylonfb_setup(option);
	/* Check input parameters */
	xylonfb_set_params();

#ifdef ARM8_BOARD
	config_clk();
#endif

	ret = platform_driver_register(&xylonfb_driver);
	if (!ret) {
		xylonfb_device.dev.release = xylonfb_dev_release;
		ret = platform_device_register(&xylonfb_device);
		if (ret) {
			platform_driver_unregister(&xylonfb_driver);
			printk(KERN_ERR "xylonfb device registration failed\n");
		}
	}

	return ret;
}

static void __exit xylonfb_exit(void)
{
	dbg("%s", __func__);

	platform_device_unregister(&xylonfb_device);
	platform_driver_unregister(&xylonfb_driver);
}


module_init(xylonfb_init);
module_exit(xylonfb_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);

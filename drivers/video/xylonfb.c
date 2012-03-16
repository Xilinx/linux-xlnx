/*
 * Xylon logiCVC frame buffer driver
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
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
 
#include <asm/io.h>
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
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/xylonfb.h>


#define dbg(...) //printk(KERN_INFO __VA_ARGS__)

#define DRIVER_NAME "xylonfb"
#define PLATFORM_DRIVER_NAME "logicvc"
#define DRIVER_DESCRIPTION "Xylon logiCVC frame buffer driver"

#define CVC_MAX_LAYERS            5
#define CVC_LAYER_ON              0x10
#define CVC_MAX_VRES              4096 /* this value must be 2048! for now it's hacked */
#define TRANSP_COLOR_8BPP_CLUT_16 0xF813
#define TRANSP_COLOR_8BPP_CLUT_24 0x00FF009C
#define TRANSP_COLOR_16BPP 0xF813
#define TRANSP_COLOR_24BPP 0x00FF009C
#define BACKGROUND_COLOR          0x00000000

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
#define SD_REG_INIT   0

/* CVC display power signals */
#define CVC_EN_BLIGHT_MSK 0x01
#define CVC_EN_VDD_MSK    0x02
#define CVC_EN_VEE_MSK    0x04
#define CVC_V_EN_MSK      0x08

/* FB driver flags */
#define FB_DMA_BUFFER 0x01
#define FB_VSYNC_INT  0x02
#define FB_CHANGE_RES 0x04

#define XYLONFB_IOC_MAGIC   'x'
#define XYLONFB_IOC_GETVRAM _IO(XYLONFB_IOC_MAGIC, 0)

/* Supported_video_modes */
#define XYLONFB_VM_VESA_640x480_8    (0x101+0x200)
#define XYLONFB_VM_VESA_640x480_16   (0x111+0x200)
#define XYLONFB_VM_VESA_640x480_32   (0x112+0x200)
#define XYLONFB_VM_VESA_800x600_8    (0x103+0x200)
#define XYLONFB_VM_VESA_800x600_16   (0x114+0x200)
#define XYLONFB_VM_VESA_800x600_32   (0x115+0x200)
#define XYLONFB_VM_VESA_1024x768_8   (0x105+0x200)
#define XYLONFB_VM_VESA_1024x768_16  (0x117+0x200)
#define XYLONFB_VM_VESA_1024x768_32  (0x118+0x200)
#define XYLONFB_VM_VESA_1280x720_8    0
#define XYLONFB_VM_VESA_1280x720_16   0
#define XYLONFB_VM_VESA_1280x720_32   0
#define XYLONFB_VM_VESA_1280x1024_8  (0x107+0x200)
#define XYLONFB_VM_VESA_1280x1024_16 (0x11A+0x200)
#define XYLONFB_VM_VESA_1280x1024_32 (0x11B+0x200)
#define XYLONFB_VM_VESA_1680x1050_8   0
#define XYLONFB_VM_VESA_1680x1050_16  0
#define XYLONFB_VM_VESA_1680x1050_32  0
#define XYLONFB_VM_VESA_1920x1080_8   0
#define XYLONFB_VM_VESA_1920x1080_16  0
#define XYLONFB_VM_VESA_1920x1080_32  0

#define VESA_640x480_8    1
#define VESA_640x480_16   2
#define VESA_640x480_32   3
#define VESA_800x600_8    4
#define VESA_800x600_16   5
#define VESA_800x600_32   6
#define VESA_1024x768_8   7
#define VESA_1024x768_16  8
#define VESA_1024x768_32  9
#define VESA_1280x720_8   10
#define VESA_1280x720_16  11
#define VESA_1280x720_32  12
#define VESA_1280x1024_8  13
#define VESA_1280x1024_16 14
#define VESA_1280x1024_32 15
#define VESA_1680x1050_8  16
#define VESA_1680x1050_16 17
#define VESA_1680x1050_32 18
#define VESA_1920x1080_8  19
#define VESA_1920x1080_16 20
#define VESA_1920x1080_32 21

/* Choose FB driver default video resolution
   which will be set at driver initialization */
//#define VIDEO_MODE VESA_640x480_8
//#define VIDEO_MODE VESA_640x480_16
#define VIDEO_MODE VESA_640x480_32
//#define VIDEO_MODE VESA_800x600_8
//#define VIDEO_MODE VESA_800x600_16
//#define VIDEO_MODE VESA_800x600_32
//#define VIDEO_MODE VESA_1024x768_8
//#define VIDEO_MODE VESA_1024x768_16
//#define VIDEO_MODE VESA_1024x768_32
//#define VIDEO_MODE VESA_1280x720_8
//#define VIDEO_MODE VESA_1280x720_16
//#define VIDEO_MODE VESA_1280x720_32
//#define VIDEO_MODE VESA_1280x1024_8
//#define VIDEO_MODE VESA_1280x1024_16
//#define VIDEO_MODE VESA_1280x1024_32
//#define VIDEO_MODE VESA_1680x1050_8
//#define VIDEO_MODE VESA_1680x1050_16
//#define VIDEO_MODE VESA_1680x1050_32
//#define VIDEO_MODE VESA_1920x1080_8
//#define VIDEO_MODE VESA_1920x1080_16
//#define VIDEO_MODE VESA_1920x1080_32




struct layer_fix_data {
	unsigned int offset;
	unsigned int bpp;
	unsigned int width;
	unsigned int height;
};

struct xylonfb_vsync {
	wait_queue_head_t wait;
	unsigned int cnt;
};

struct xylonfb_layer_data {
	dma_addr_t reg_base_phys;	/* Physical base address of the logiCVC registers */
	void *reg_base_virt;		/* Virtual base address of the logiCVC registers */
	dma_addr_t fb_phys; 		/* Physical base address of the frame buffer video memory */
	void *fb_virt;				/* Virtual base address of the frame buffer video memory */
	unsigned long fb_size;		/* Size of the frame buffer video memory */
	void *layer_reg_base_virt;	/* Virtual base address of the logiCVC layer registers */
	void *layer_clut_base_virt;	/* Virtual base address of the logiCVC layer CLUT registers */
	atomic_t layer_use_ref;				/* logiCVC layer reference usage counter */
	struct xylonfb_vsync vsync;			/* FB driver V-sync structure */
	struct layer_fix_data layer_fix;	/* logiCVC layer fixed parameters */
	unsigned char layer_info;			/* bit 4: logiCVC layer ON/OFF    bits 3-0: logiCVC layer ID */
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


/**
 * Structure contains detailed data about
   the particular display or standard VGA resolution type.
 */
struct video_mode_parameters
	{
	u32 ctrl_reg;
	struct fb_videomode fb_vmode;	/* Video mode parameters */
	char name[10];					/* Video mode name */
};

static struct video_mode_parameters video_mode = {
#if ((VIDEO_MODE == VESA_640x480_8) || \
	 (VIDEO_MODE == VESA_640x480_16) || \
	 (VIDEO_MODE == VESA_640x480_32))
	.fb_vmode = {
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
	.vmode        = FB_VMODE_NONINTERLACED
	},
	.name = "VGA",
#endif /* #if (VIDEO_MODE == VESA_640x480_ ...) */

#if ((VIDEO_MODE == VESA_800x600_8) || \
	 (VIDEO_MODE == VESA_800x600_16) || \
	 (VIDEO_MODE == VESA_800x600_32))
	.fb_vmode = {
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
	.vmode        = FB_VMODE_NONINTERLACED
	},
	.name = "SVGA",
#endif /* #if (VIDEO_MODE == VESA_800x600_ ...) */

#if ((VIDEO_MODE == VESA_1024x768_8) || \
	 (VIDEO_MODE == VESA_1024x768_16) || \
	 (VIDEO_MODE == VESA_1024x768_32))
	.fb_vmode = {
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
	.vmode        = FB_VMODE_NONINTERLACED
	},
	.name = "XGA",
#endif /* #if (VIDEO_MODE == VESA_1024x768_ ...) */

#if ((VIDEO_MODE == VESA_1280x720_8) || \
	 (VIDEO_MODE == VESA_1280x720_16) || \
	 (VIDEO_MODE == VESA_1280x720_32))
	.fb_vmode = {
	.refresh      = 60,
		.xres         = 1280,
		.yres         = 720,
		.pixclock     = KHZ2PICOS(74250),
		.left_margin  = 220,
		.right_margin = 110,
		.upper_margin = 20,
		.lower_margin = 5,
		.hsync_len    = 40,
		.vsync_len    = 5,
		.vmode        = FB_VMODE_NONINTERLACED
	},
	.name = "HD720",
#endif /* #if (VIDEO_MODE == VESA_1280x720_ ...) */

#if ((VIDEO_MODE == VESA_1280x1024_8) || \
	 (VIDEO_MODE == VESA_1280x1024_16) || \
	 (VIDEO_MODE == VESA_1280x1024_32))
	.fb_vmode = {
		.refresh      = 60,
		.xres         = 1280,
		.yres         = 1024,
		.pixclock     = KHZ2PICOS(107964),
		.left_margin  = 248,
		.right_margin = 48,
		.upper_margin = 38,
		.lower_margin = 1,
		.hsync_len    = 112,
		.vsync_len    = 3,
		.vmode        = FB_VMODE_NONINTERLACED
	},
	.name = "SXGA",
#endif /* #if (VIDEO_MODE == VESA_1280x1024_ ...) */

#if ((VIDEO_MODE == VESA_1680x1050_8) || \
	 (VIDEO_MODE == VESA_1680x1050_16) || \
	 (VIDEO_MODE == VESA_1680x1050_32))
	.fb_vmode = {
		.refresh      = 60,
		.xres         = 1680,
		.yres         = 1050,
		.pixclock     = KHZ2PICOS(146361),
		.left_margin  = 280,
		.right_margin = 104,
		.upper_margin = 30,
	.lower_margin = 3,
		.hsync_len    = 176,
	.vsync_len    = 6,
	.vmode        = FB_VMODE_NONINTERLACED
	},
	.name = "WSXVGA+",
#endif /* #if (VIDEO_MODE == VESA_1680x1050_ ...) */

#if ((VIDEO_MODE == VESA_1920x1080_8) || \
	 (VIDEO_MODE == VESA_1920x1080_16) || \
	 (VIDEO_MODE == VESA_1920x1080_32))
	.fb_vmode = {
		.refresh      = 60,
		.xres         = 1920,
		.yres         = 1080,
		.pixclock     = KHZ2PICOS(148500),
		.left_margin  = 148,
		.right_margin = 88,
		.upper_margin = 36,
		.lower_margin = 4,
		.hsync_len    = 44,
		.vsync_len    = 5,
		.vmode        = FB_VMODE_NONINTERLACED
	},
	.name = "HD1080",
#endif /* #if (VIDEO_MODE == VESA_1920x1080_ ...) */
};


static struct fb_videomode drv_vmode;
static u32 xylonfb_pseudo_palette[16];
static atomic_t xylonfb_use_ref;
/* Delay after applying display power and
   before applying display signals */
static unsigned int power_on_delay;
/* Delay after applying display signal and
   before applying display backlight power supply */
static unsigned int signal_on_delay;
static char *mode_option __devinitdata;

/* function declarations */
static inline void xylonfb_set_fbi_timings(struct fb_var_screeninfo *var);
static int xylonfb_set_timings(struct fb_info *fbi, int bpp, bool change_res);
static void xylonfb_start_logicvc(struct fb_info *fbi);
static void xylonfb_stop_logicvc(struct fb_info *fbi);


/*
This function is completly dependent on pixelclock hardware.
Code in function must be reimplemented for every HW platform if pixelclock
wants to be changed dynamically while system is up and running.

The implementation below assume that the pixelclock is derived from
the pllclk running at 1000Mhz and computes the divisor needed
to generate the pixel clock accordingly.

Subsequent TDP design will use the zc702 on-board clock synthesizer
to generate the pixel clock.  This function will be updated accordingly.
*/
static int xylonfb_set_pixelclock(struct fb_info *fbi)
	{
	unsigned long pllclk, sysclk, pixclk;
	unsigned long div, delta, delta_dec, delta_inc;
	void *slcr_regs, *clk_regs, *rst_reg;

	dbg("%s\n", __func__);

	/* all clock values are in kHz */
	pllclk = 1000000;
	sysclk = 100000;
	pixclk = PICOS2KHZ(fbi->var.pixclock);

	slcr_regs = ioremap_nocache(0xF8000004, 8);
	if (!slcr_regs) {
		printk(KERN_ERR
			"Error mapping SLCR\n");
		return -EBUSY;
	}
	clk_regs = ioremap_nocache(0xF8000170, 32);
	if (!clk_regs) {
		printk(KERN_ERR
			"Error setting xylonfb pixelclock\n");
		iounmap(slcr_regs);
		return -EBUSY;
	}
	rst_reg = ioremap_nocache(0xF8000240, 4);
	if (!rst_reg) {
		printk(KERN_ERR
			"Error setting xylonfb pixelclock\n");
		iounmap(clk_regs);
		iounmap(slcr_regs);
		return -EBUSY;
	}

	/* unlock register access */
	writel(0xDF0D, (slcr_regs+4));
//	/* calculate system clock divisor */
//	div = pllclk / sysclk;
//	/* prepare for register writting */
//	div = (div + 0x1000) << 8;
//	/* set system clock */
//	writel(div, clk_regs);
	/* calculate video clock divisor */
	div = pllclk / pixclk;
	delta = (pllclk / div) - pixclk;
	if (delta != 0) {
		delta_inc = pixclk - (pllclk / (div+1));
		delta_dec = (pllclk / (div-1)) - pixclk;
		if (delta < delta_inc) {
			if (delta > delta_dec)
				div--;
			//else
			//	div = div;
		} else {
			if (delta > delta_dec) {
				if (delta_inc > delta_dec)
					div--;
				else
					div++;
			} else {
				div++;
			}
		}
	}
	/* prepare for register writting */
	div = (div + 0x1000) << 8;
	/* set video clock */
	writel(div, (clk_regs+0x10));
//	/* reset FPGA */
//	writel(0, rst_reg);
//	writel(0x1, rst_reg);
//	writel(0, rst_reg);
	/* lock register access */
	writel(0x767B, slcr_regs);

	iounmap(rst_reg);
	iounmap(clk_regs);
	iounmap(slcr_regs);

	return 0;
}

//static irqreturn_t xylonfb_irq(int irq, void *dev_id)
//{
//	struct fb_info *fbi = (struct fb_info *)dev_id;
//	struct xylonfb_layer_data *layer_data = fbi->par;
//	u32 isr;
//
//	isr = readl(layer_data->reg_base_virt + CVC_INT_ROFF);
//
//	if (isr & CVC_V_SYNC_INT) {
//		writel(CVC_V_SYNC_INT, layer_data->reg_base_virt + CVC_INT_ROFF);
//		layer_data->vsync.cnt++;
//		layer_data->fb_flags |= FB_VSYNC_INT;
//		wake_up_interruptible(&layer_data->vsync.wait);
//	}
//
//	return IRQ_HANDLED;
//}


static int xylonfb_open(struct fb_info *fbi, int user)
{
	struct xylonfb_layer_data *layer_data;
	int use;

	dbg("%s\n", __func__);

	layer_data = (struct xylonfb_layer_data *)fbi->par;
	use = atomic_read(&layer_data->layer_use_ref);
	if (use == 0) {
		/* turn on layer */
		writel(1, (layer_data->layer_reg_base_virt + CVC_LAYER_CTRL_ROFF));
		/* set layer ON flag */
		layer_data->layer_info |= CVC_LAYER_ON;
	}

	atomic_inc(&layer_data->layer_use_ref);
	atomic_inc(&xylonfb_use_ref);

	return 0;
}

static int xylonfb_release(struct fb_info *fbi, int user)
{
	struct xylonfb_layer_data *layer_data;
	int use;

	dbg("%s\n", __func__);

	layer_data = (struct xylonfb_layer_data *)fbi->par;
	atomic_dec(&layer_data->layer_use_ref);
	use = atomic_read(&layer_data->layer_use_ref);
	if (use == 0) {
		/* turn off layer */
		writel(0, (layer_data->layer_reg_base_virt + CVC_LAYER_CTRL_ROFF));
		/* set layer OFF flag */
		layer_data->layer_info &= (~CVC_LAYER_ON);
	}

	atomic_dec(&xylonfb_use_ref);

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
		printk(KERN_INFO "Requested new video mode %s\n", mode_option);
		ret = xylonfb_set_timings(fbi, var->bits_per_pixel, 1);
		if (ret == 1 || ret == 2)
			layer_data->fb_flags |= FB_CHANGE_RES;
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

	if (denied)
		return -EPERM;

	return 0;
}

static int xylonfb_set_par(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;

	dbg("%s\n", __func__);

	if (layer_data->fb_flags & FB_CHANGE_RES) {
		xylonfb_set_fbi_timings(&fbi->var);
		xylonfb_stop_logicvc(fbi);
		if (xylonfb_set_pixelclock(fbi))
			return -EACCES;
		xylonfb_start_logicvc(fbi);
		layer_data->fb_flags &= (~FB_CHANGE_RES);
		printk(KERN_INFO
			"xylonfb new video mode: %dx%d-%dbpp@60\n",
			fbi->var.xres, fbi->var.yres, fbi->var.bits_per_pixel);
	}

	return 0;
}

static int xylonfb_set_color_reg(unsigned regno, unsigned red, unsigned green,
			unsigned blue, unsigned transp, struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data = fbi->par;
	u32 clut_value = 0;

	dbg("%s\n", __func__);

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

	dbg("%s\n", __func__);

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
	dbg("%s\n", __func__);

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

	dbg("%s\n", __func__);

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

	dbg("%s\n", __func__);

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


static inline void set_ctrl_reg(void)
{
//	u32 sync = video_mode.fb_vmode.sync;
	u32 ctrl = CTRL_REG_INIT;
	/*	- [bit 0,1] hsync active and inverted (active low)
		- [bit 2,3] vsync active and inverted (active low)
		- [bit   4] de active */

//	if (sync & (1<<0)) {	//FB_SYNC_HOR_HIGH_ACT
//		ctrl &= (~(1<<1));	// invert hsync (active high)
//	}
//	if (sync & (1<<1)) {	// FB_SYNC_VERT_HIGH_ACT
//		ctrl &= (~(1<<3));	// invert vsync (active high)
//	}
//	if (sync & (1<<7)) {	// added new bit for pixel clock inversion
//		ctrl &= (~(1<<8));	// invert pixel clock (data active on high to low)
//	}

	video_mode.ctrl_reg = ctrl;
}

#ifdef CONFIG_OF
static int xylonfb_parse_vram_addr(struct platform_device *pdev,
	unsigned long *vmem_base_addr, unsigned long *vmem_high_addr)
{
	u32 const *prop;
	int size;

	dbg("%s\n", __func__);

	prop =
		of_get_property(pdev->dev.of_node, "vmem-baseaddr", &size);
	if (!prop) {
		printk(KERN_ERR "Error getting xylonfb VRAM address begin\n");
		return -EINVAL;
	}
	*vmem_base_addr = be32_to_cpup(prop);

	prop =
		of_get_property(pdev->dev.of_node, "vmem-highaddr", &size);
	if (!prop) {
		printk(KERN_ERR "Error getting xylonfb VRAM address end\n");
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

	prop = of_get_property(pdev->dev.of_node, "num-of-layers", &size);
	if (!prop) {
		printk(KERN_ERR "Error getting number of layers\n");
		return -EINVAL;
	}
	*layers = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "use-background", &size);
	if (!prop) {
		printk(KERN_ERR "Error getting number of layers\n");
		return -EINVAL;
	}
	/* if background layer is present decrease number of layers */
	if (be32_to_cpup(prop) == 1)
		(*layers)--;

	return 0;
}

static int xylonfb_parse_vmode_params(struct platform_device *pdev,
	int *active_layer)
{
	struct device_node *dn, *vmode_dn;
	u32 const *prop;
	int i, size, vmode_id;

	dbg("%s\n", __func__);

	*active_layer = 0;

	dn = of_find_node_by_name(NULL, "xylon-videomode-params");
	if (dn == NULL) {
		printk(KERN_ERR "Error getting xylonfb video mode parameters\n");
		return -1;
	}

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
		printk(KERN_ERR "Error getting xylonfb video mode name\n");
	else
		strcpy(video_mode.name, (char *)prop);

	prop = of_get_property(vmode_dn, "refresh", &size);
	if (!prop)
		printk(KERN_ERR "Error getting refresh rate\n");
	else
		video_mode.fb_vmode.refresh = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "xres", &size);
	if (!prop)
		printk(KERN_ERR "Error getting xres\n");
	else
		video_mode.fb_vmode.xres = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "yres", &size);
	if (!prop)
		printk(KERN_ERR "Error getting yres\n");
	else
		video_mode.fb_vmode.yres = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "pixclock-khz", &size);
	if (!prop)
		printk(KERN_ERR "Error getting pixclock-khz\n");
	else
		video_mode.fb_vmode.pixclock = KHZ2PICOS(be32_to_cpup(prop));

	prop = of_get_property(vmode_dn, "left-margin", &size);
	if (!prop)
		printk(KERN_ERR "Error getting left-margin\n");
	else
		video_mode.fb_vmode.left_margin = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "right-margin", &size);
	if (!prop)
		printk(KERN_ERR "Error getting right-margin\n");
	else
		video_mode.fb_vmode.right_margin = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "upper-margin", &size);
	if (!prop)
		printk(KERN_ERR "Error getting upper-margin\n");
	else
		video_mode.fb_vmode.upper_margin = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "lower-margin", &size);
	if (!prop)
		printk(KERN_ERR "Error getting lower-margin\n");
	else
		video_mode.fb_vmode.lower_margin = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "hsync-len", &size);
	if (!prop)
		printk(KERN_ERR "Error getting hsync-len\n");
	else
		video_mode.fb_vmode.hsync_len = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "vsync-len", &size);
	if (!prop)
		printk(KERN_ERR "Error getting vsync-len\n");
	else
		video_mode.fb_vmode.vsync_len = be32_to_cpup(prop);

	prop = of_get_property(vmode_dn, "sync", &size);
	if (!prop)
		printk(KERN_ERR "Error getting sync\n");
	else
		video_mode.fb_vmode.sync = be32_to_cpup(prop);
	set_ctrl_reg();

	prop = of_get_property(vmode_dn, "vmode", &size);
	if (!prop)
		printk(KERN_ERR "Error getting vmode\n");
	else
		video_mode.fb_vmode.vmode = be32_to_cpup(prop);

	return 0;
		}

static int xylonfb_parse_layer_params(struct platform_device *pdev,
	int id, struct layer_fix_data *lfdata)
{
	u32 const *prop;
	int size;
	char layer_property_name[25];

	dbg("%s\n", __func__);

	sprintf(layer_property_name, "layer-%d-offset", id);
	prop = of_get_property(pdev->dev.of_node, layer_property_name, &size);
	if (!prop) {
		printk(KERN_ERR "Error getting xylonfb layer offset\n");
		return -EINVAL;
	} else {
		lfdata->offset = be32_to_cpup(prop);
	}

	sprintf(layer_property_name, "layer-%d-data-width", id);
	prop = of_get_property(pdev->dev.of_node, layer_property_name, &size);
	if (!prop)
		lfdata->bpp = 16;
		else
		lfdata->bpp = be32_to_cpup(prop);
	if (lfdata->bpp == 24)
		lfdata->bpp = 32;

	prop = of_get_property(pdev->dev.of_node, "row-stride", &size);
	if (!prop)
		lfdata->width = 1024;
	else
		lfdata->width = be32_to_cpup(prop);

	return 0;
}
#endif

static void xylonfb_set_yvirt(int id, int layers,
	struct layer_fix_data *lfdata,
	unsigned long vmem_base_addr, unsigned long vmem_high_addr)
{
	dbg("%s\n", __func__);

	if (id < (layers-1)) {
		lfdata[id].height =
			((lfdata[id+1].width * (lfdata[id+1].bpp/8) *
			lfdata[id+1].offset)
				-
			(lfdata[id].width * (lfdata[id].bpp/8) *
			lfdata[id].offset)) /
			(lfdata[id].width * (lfdata[id].bpp/8));
	} else {
		/* FIXME - this is set for 1920x1080 tripple buffering,
			and it should be read from dt parameters */
		lfdata[id].height = 3240;
		while (1) {
			if ((lfdata[id].width * (lfdata[id].bpp/8) * lfdata[id].height +
				lfdata[id].width * (lfdata[id].bpp/8) *
				lfdata[id].offset)
					<=
				(vmem_high_addr - vmem_base_addr))
				break;
			lfdata[id].height -= 64; /* FIXME - magic number? */
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
		if (layer_data->fb_flags & FB_DMA_BUFFER) {
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
//		memset_io((void __iomem *)layer_data->fb_virt, 0, layer_data->fb_size);
		layer_data->layer_reg_base_virt =
		layer_data->reg_base_virt + cvc_layer_reg_offset[id];
		layer_data->layer_clut_base_virt =
		layer_data->reg_base_virt + cvc_clut_reg_offset[id];
	atomic_set(&layer_data->layer_use_ref, 0);
	layer_data->layer_info = id;
		layer_data->layers = layers;

	return 0;
}

static inline void xylonfb_set_drv_vmode(void)
{
	dbg("%s\n", __func__);

	drv_vmode.xres = video_mode.fb_vmode.xres;
	drv_vmode.yres = video_mode.fb_vmode.yres;
	drv_vmode.pixclock = video_mode.fb_vmode.pixclock;
	drv_vmode.left_margin = video_mode.fb_vmode.left_margin;
	drv_vmode.right_margin = video_mode.fb_vmode.right_margin;
	drv_vmode.upper_margin = video_mode.fb_vmode.upper_margin;
	drv_vmode.lower_margin = video_mode.fb_vmode.lower_margin;
	drv_vmode.hsync_len = video_mode.fb_vmode.hsync_len;
	drv_vmode.vsync_len = video_mode.fb_vmode.vsync_len;
	drv_vmode.vmode = video_mode.fb_vmode.vmode;
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
	if (lfdata->bpp == 8 || lfdata->bpp == 16)
			fbi->fix.visual = FB_VISUAL_DIRECTCOLOR;
		else
			fbi->fix.visual = FB_VISUAL_TRUECOLOR;
		fbi->fix.xpanstep = 1;
		fbi->fix.ypanstep = 1;
	fbi->fix.ywrapstep = CVC_MAX_VRES;
	fbi->fix.line_length = lfdata->width * (lfdata->bpp/8);
	fbi->fix.mmio_start = reg_base_phys;
		fbi->fix.mmio_len = CVC_REGISTERS_RANGE;
		fbi->fix.accel = FB_ACCEL_NONE;

	fbi->var.xres_virtual = lfdata->width;
	if (lfdata->height <= CVC_MAX_VRES)
		fbi->var.yres_virtual = lfdata->height;
	else
		fbi->var.yres_virtual = CVC_MAX_VRES;
	fbi->var.bits_per_pixel = lfdata->bpp;

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

	rc = fb_find_mode(&fb_var, fbi, mode_option, NULL, 0,
		&video_mode.fb_vmode, bpp);
	switch (rc) {
		case 0:
			printk(KERN_ERR "xylonfb video mode option error\n"
				"using driver default mode %s\n", video_mode.name);
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
					video_mode.name);
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
	dbg("%s\n", __func__);

	fbi->flags = FBINFO_DEFAULT;
	fbi->screen_base = (char __iomem *)layer_data->fb_virt;
	fbi->screen_size = layer_data->fb_size;
	fbi->pseudo_palette = xylonfb_pseudo_palette;
	fbi->fbops = &xylonfb_ops;

	sprintf(fbi->fix.id, "Xylon FB%d", id);
	xylonfb_set_hw_specifics(fbi, layer_data, lfdata, reg_base_phys);

	/* if mode_option is set, find mode will be done only once */
	if (mode_option) {
		xylonfb_set_timings(fbi, lfdata->bpp, 0);
		mode_option = NULL;
	}

	xylonfb_set_fbi_timings(&fbi->var);

	if (fb_alloc_cmap(&fbi->cmap, 256, 1))
		return -ENOMEM;

	*regfb = register_framebuffer(fbi);
	if (*regfb) {
		printk(KERN_ERR "Error registering xylonfb %d\n", id);
		return -EINVAL;
	}
	printk(KERN_INFO "xylonfb %d registered\n", id);
	/* after driver registration values in struct fb_info
		must not be changed anywhere else except in xylonfb_set_par */

	return 0;
}

static void xylonfb_logicvc_disp_ctrl(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data =
		(struct xylonfb_layer_data *)fbi->par;
	u32 val;

	dbg("%s\n", __func__);

	val = CVC_EN_VDD_MSK;
	writel(val, layer_data->reg_base_virt + CVC_SPWRCTRL_ROFF);
	mdelay(power_on_delay);
	val |= CVC_V_EN_MSK;
	writel(val, layer_data->reg_base_virt + CVC_SPWRCTRL_ROFF);
	mdelay(signal_on_delay);
	val |= CVC_EN_BLIGHT_MSK;
	writel(val, layer_data->reg_base_virt + CVC_SPWRCTRL_ROFF);
}

static void xylonfb_start_logicvc(struct fb_info *fbi)
{
	struct xylonfb_layer_data *layer_data =
		(struct xylonfb_layer_data *)fbi->par;

	dbg("%s\n", __func__);

	writel(fbi->var.right_margin-1,
		layer_data->reg_base_virt + CVC_SHSY_FP_ROFF);
	writel(fbi->var.hsync_len-1,
		layer_data->reg_base_virt + CVC_SHSY_ROFF);
	writel(fbi->var.left_margin-1,
		layer_data->reg_base_virt + CVC_SHSY_BP_ROFF);
	writel(fbi->var.xres-1,
		layer_data->reg_base_virt + CVC_SHSY_RES_ROFF);
	writel(fbi->var.lower_margin-1,
		layer_data->reg_base_virt + CVC_SVSY_FP_ROFF);
	writel(fbi->var.vsync_len-1,
		layer_data->reg_base_virt + CVC_SVSY_ROFF);
	writel(fbi->var.upper_margin-1,
		layer_data->reg_base_virt + CVC_SVSY_BP_ROFF);
	writel(fbi->var.yres-1,
		layer_data->reg_base_virt + CVC_SVSY_RES_ROFF);
	writel(video_mode.ctrl_reg, layer_data->reg_base_virt + CVC_SCTRL_ROFF);
	writel(SD_REG_INIT, layer_data->reg_base_virt + CVC_SDTYPE_ROFF);
	writel(BACKGROUND_COLOR, layer_data->reg_base_virt + CVC_BACKCOL_ROFF);
//	writel(0x00, layer_data->reg_base_virt + CVC_DOUBLE_VBUFF_ROFF);
//	writel(0x00, layer_data->reg_base_virt + CVC_DOUBLE_CLUT_ROFF);
	writel(0xFFFF, layer_data->reg_base_virt + CVC_INT_ROFF);
	writel(0xFFFF, layer_data->reg_base_virt + CVC_INT_MASK_ROFF);
	writel(TRANSP_COLOR_24BPP,
		(layer_data->layer_reg_base_virt + CVC_LAYER_TRANSP_ROFF));

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
	struct xylonfb_layer_data *layer_data =
		(struct xylonfb_layer_data *)fbi->par;

	dbg("%s\n", __func__);

	writel(0, layer_data->reg_base_virt + CVC_SCTRL_ROFF);
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
		if (layer_data->layer_info & CVC_LAYER_ON)
			continue;
		/* turn off layer */
		writel(0, (layer_data->layer_reg_base_virt + CVC_LAYER_CTRL_ROFF));
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
	struct xylonfb_layer_data *layer_data;
	struct resource *reg_res, *irq_res;
#ifndef CONFIG_OF
	struct xylonfb_platform_data *pdata;
#endif
	struct layer_fix_data lfdata[CVC_MAX_LAYERS];
	void *reg_base_virt;
	u32 reg_base_phys;
	unsigned long vmem_base_addr, vmem_high_addr;
	int reg_range, layers, active_layer;
	int i, rc;
	int regfb[CVC_MAX_LAYERS];

	dbg("%s\n", __func__);

	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if ((!reg_res) || (!irq_res)) {
		printk(KERN_ERR "Error xylonfb resources: MEM 0x%X IRQ 0x%X\n",
			(unsigned int)reg_res, (unsigned int)irq_res);
		return -ENODEV;
	}

#ifdef CONFIG_OF
	rc = xylonfb_parse_vram_addr(pdev, &vmem_base_addr, &vmem_high_addr);
	if (rc)
		return rc;
	rc = xylonfb_parse_layer_info(pdev, &layers);
	if (rc)
		return rc;
	if (xylonfb_parse_vmode_params(pdev, &active_layer) == 0) {
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

	afbi = kzalloc(sizeof(struct fb_info *) * layers, GFP_KERNEL);
	if (!afbi) {
		printk(KERN_ERR "Error allocating xylonfb internal data\n");
		return -ENOMEM;
	}

	layer_data = NULL;

	reg_base_phys = reg_res->start;
	reg_range = reg_res->end - reg_res->start;
	reg_base_virt = ioremap_nocache(reg_base_phys, reg_range);

	/* load layer parameters for all layers */
	for (i = 0; i < layers; i++) {
#ifdef CONFIG_OF
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
			printk(KERN_ERR "Error allocate xylonfb info\n");
			rc = -ENOMEM;
			goto err_fb;
		}
		afbi[i] = fbi;
		layer_data = fbi->par;

		xylonfb_set_yvirt(i, layers, lfdata,
			vmem_base_addr, vmem_high_addr);
		rc = xylonfb_map(i, layers, dev, layer_data, &lfdata[i],
			vmem_base_addr, reg_base_phys, reg_base_virt);
		if (rc)
			goto err_fb;

		rc = xylonfb_register_fb(fbi, layer_data, &lfdata[i],
			reg_base_phys, i, &regfb[i]);
		if (rc)
			goto err_fb;

		layer_data->layer_fix = lfdata[i];

		init_waitqueue_head(&layer_data->vsync.wait);

		/* register following layers in HW configuration order */
		if (active_layer > 0) {
			i = -1; /* after for loop increment i will be zero */
			active_layer = -1;
		}

		dbg( \
			"    Layer ID %d\n" \
			"    Layer offset %d\n" \
			"    Layer bits per pixel %d\n" \
			"    Layer width %d pixels\n" \
			"    Layer height %d lines\n" \
			"    Layer FB size %ld bytes\n", \
			(layer_data->layer_info & 0x0F),
			layer_data->layer_fix.offset,
			layer_data->layer_fix.bpp,
			layer_data->layer_fix.width,
			layer_data->layer_fix.height,
			layer_data->fb_size);
	}

//	if (request_irq(irq, xylonfb_irq, IRQF_SHARED, "xylonfb", fbi))
//		goto err_fb;

	atomic_set(&xylonfb_use_ref, 0);
	dev_set_drvdata(dev, (void *)afbi);

	/* start HW */
	rc = xylonfb_start(afbi, layers);
	if (rc)
		goto err_fb;

	printk(KERN_INFO
		"xylonfb video mode: %dx%d-%dbpp@60\n",
		afbi[0]->var.xres, afbi[0]->var.yres, afbi[0]->var.bits_per_pixel);

	kfree(afbi);

	return 0;

err_fb:
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
	kfree(afbi);

	dev_set_drvdata(dev, NULL);

	return rc;
}

static int xylonfb_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fb_info **afbi = (struct fb_info **)dev_get_drvdata(dev);
	struct fb_info *fbi;
	struct xylonfb_layer_data *layer_data;
	int i, use, layers;
	bool cvc_off = 0;

	dbg("%s\n", __func__);

	use = atomic_read(&xylonfb_use_ref);
	if (use) {
		printk(KERN_ERR "xylonfb driver is in use\n");
		return -EINVAL;
	}

	/* get information about number of layers (framebuffer devices) */
	fbi = afbi[0];
	layer_data = fbi->par;
	layers = layer_data->layers;

	for (i = layers-1; i >= 0; i--) {
		fbi = afbi[i];
		layer_data = fbi->par;
		if (!cvc_off) {
			xylonfb_stop_logicvc(fbi);
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
	kfree(afbi);

	dev_set_drvdata(dev, NULL);

	return 0;
}

/* Match table for of_platform binding */
#ifdef CONFIG_OF
static struct of_device_id xylonfb_of_match[] __devinitdata = {
	{ .compatible = "xylon,logicvc-2.01.d" },
	{ .compatible = "xylon,logicvc-2.04.a" },
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
		printk(KERN_ERR "xylonfb driver registration failed\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit xylonfb_exit(void)
{
	dbg("%s\n", __func__);

	platform_driver_unregister(&xylonfb_driver);
}


module_init(xylonfb_init);
module_exit(xylonfb_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);

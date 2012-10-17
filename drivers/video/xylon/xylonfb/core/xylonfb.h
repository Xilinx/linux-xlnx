/*
 * Xylon logiCVC frame buffer driver internal data structures
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * 2012 (c) Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __XYLON_FB_DATA_H__
#define __XYLON_FB_DATA_H__


#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/fb.h>
#include "logicvc.h"


#define DRIVER_NAME "xylonfb"
#define DEVICE_NAME "logicvc"
#define DRIVER_DESCRIPTION "Xylon logiCVC frame buffer driver"
#define DRIVER_VERSION "1.1"

/* FB driver flags */
#define FB_DMA_BUFFER        0x01
#define FB_MEMORY_LE         0x02
#define FB_VMODE_INIT        0x10
#define FB_DEFAULT_VMODE_SET 0x20
#define FB_VMODE_SET         0x40


#ifdef DEBUG
#define driver_devel(format, ...) \
	do { \
		printk(KERN_INFO format, ## __VA_ARGS__); \
	} while (0)
#else
#define driver_devel(format, ...)
#endif


#define VMODE_NAME_SZ 20
struct xylonfb_vmode_data {
	u32 ctrl_reg;
	struct fb_videomode fb_vmode;
	char fb_vmode_name[VMODE_NAME_SZ+1];
};

struct layer_fix_data {
	unsigned int offset;
	unsigned short buffer_offset;
	unsigned short width;
	unsigned short height;
	unsigned char bpp;
	unsigned char bpp_virt;
	unsigned char alpha_mode;
	unsigned char layer_fix_info;	/* higher 4 bits: number of layer buffers */
									/* lower 4 bits: layer ID */
};

struct xylonfb_sync {
	wait_queue_head_t wait;
	unsigned int cnt;
};

struct xylonfb_common_data {
	struct device *dev;
	struct mutex irq_mutex;
	struct xylonfb_sync xylonfb_vsync;
	struct xylonfb_vmode_data vmode_data;
	struct xylonfb_vmode_data vmode_data_current;
	/* Delay after applying display power and
		before applying display signals */
	unsigned int power_on_delay;
	/* Delay after applying display signal and
		before applying display backlight power supply */
	unsigned int signal_on_delay;
	unsigned char layers;
	unsigned char xylonfb_irq;
	unsigned char xylonfb_use_ref;
	unsigned char xylonfb_flags;
	unsigned char xylonfb_used_layer;
	unsigned char bg_layer_bpp;
	unsigned char bg_layer_alpha_mode;
};

struct xylonfb_layer_data {
	struct xylonfb_common_data *xylonfb_cd;
	struct mutex layer_mutex;
	dma_addr_t reg_base_phys;
	dma_addr_t fb_phys;
	void *reg_base_virt;
	void *fb_virt;
	unsigned long fb_size;
	void *layer_reg_base_virt;
	void *layer_clut_base_virt;
	struct layer_fix_data layer_fix;
	unsigned char layer_ctrl;
	unsigned char layer_flags;
	unsigned char layer_use_ref;
};

struct xylonfb_init_data {
	struct platform_device *pdev;
	struct xylonfb_vmode_data vmode_data;
	struct layer_fix_data lfdata[LOGICVC_MAX_LAYERS];
	unsigned long vmem_base_addr;
	unsigned long vmem_high_addr;
	unsigned char layer_ctrl[LOGICVC_MAX_LAYERS];
	unsigned char layers;
	unsigned char active_layer;
	unsigned char bg_layer_bpp;
	unsigned char bg_layer_alpha_mode;
	bool vmode_params_set;
};


/* xylonfb core interface functions */
extern int xylonfb_get_params(char *options);
extern int xylonfb_init_driver(struct xylonfb_init_data *init_data);
extern int xylonfb_deinit_driver(struct platform_device *pdev);
extern int xylonfb_ioctl(struct fb_info *fbi,
	unsigned int cmd, unsigned long arg);

#endif /* __XYLON_FB_DATA_H__ */

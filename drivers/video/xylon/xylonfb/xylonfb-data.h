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

#ifndef	__XYLON_FB_DATA_H__
#define __XYLON_FB_DATA_H__


/* FB driver flags */
#define RES_CHANGE_DENIED  0
#define RES_CHANGE_ALLOWED 1

#define FB_DMA_BUFFER 0x01
#define FB_MEMORY_LE  0x02
#define FB_CHANGE_RES 0x10


struct layer_fix_data {
	unsigned short offset;
	unsigned short width;
	unsigned short height;
	unsigned char bpp;
	unsigned char bpp_virt;
	unsigned char alpha_mode;
};

struct xylonfb_sync {
	wait_queue_head_t wait;
	unsigned int cnt;
};

struct xylonfb_common_data {
	struct mutex irq_mutex;
	struct xylonfb_sync xylonfb_vsync;
	/* Delay after applying display power and
		before applying display signals */
	unsigned int power_on_delay;
	/* Delay after applying display signal and
		before applying display backlight power supply */
	unsigned int signal_on_delay;
	unsigned char xylonfb_irq;
	unsigned char xylonfb_use_ref;
	unsigned char xylonfb_flags;
	unsigned char xylonfb_used_layer;
};

struct xylonfb_layer_data {
	struct xylonfb_common_data *xylonfb_cd;
	spinlock_t layer_lock;
	dma_addr_t reg_base_phys;
	dma_addr_t fb_phys;
	void *reg_base_virt;
	void *fb_virt;
	unsigned long fb_size;
	void *layer_reg_base_virt;
	void *layer_clut_base_virt;
	struct layer_fix_data layer_fix;
	unsigned char layer_info;
	unsigned char layer_use_ref;
	unsigned char layers;
};

#endif /* __XYLON_FB_DATA_H__ */

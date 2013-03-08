/*
 * Xylon logiCVC frame buffer driver IOCTL parameters
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

#ifndef __XYLON_FB_H__
#define __XYLON_FB_H__


#include <linux/types.h>


struct xylonfb_layer_color {
	__u32 raw_rgb;
	__u8 use_raw;
	__u8 r;
	__u8 g;
	__u8 b;
};

struct xylonfb_layer_pos_size {
	__u16 x;
	__u16 y;
	__u16 width;
	__u16 height;
};

struct xylonfb_hw_access {
	__u32 offset;
	__u32 value;
};

/* XylonFB events */
#define XYLONFB_EVENT_FBI_UPDATE 0x01

/* XylonFB IOCTL's */
#define XYLONFB_IOW(num, dtype)  _IOW('x', num, dtype)
#define XYLONFB_IOR(num, dtype)  _IOR('x', num, dtype)
#define XYLONFB_IOWR(num, dtype) _IOWR('x', num, dtype)
#define XYLONFB_IO(num)          _IO('x', num)

#define XYLONFB_GET_LAYER_IDX           XYLONFB_IOR(30, unsigned int)
#define XYLONFB_GET_LAYER_ALPHA         XYLONFB_IOR(31, unsigned int)
#define XYLONFB_SET_LAYER_ALPHA         XYLONFB_IOW(32, unsigned int)
#define XYLONFB_LAYER_COLOR_TRANSP      XYLONFB_IOW(33, unsigned int)
#define XYLONFB_GET_LAYER_COLOR_TRANSP \
	XYLONFB_IOR(34, struct xylonfb_layer_color)
#define XYLONFB_SET_LAYER_COLOR_TRANSP \
	XYLONFB_IOW(35, struct xylonfb_layer_color)
#define XYLONFB_GET_LAYER_SIZE_POS \
	XYLONFB_IOR(36, struct xylonfb_layer_pos_size)
#define XYLONFB_SET_LAYER_SIZE_POS \
	XYLONFB_IOW(37, struct xylonfb_layer_pos_size)
#define XYLONFB_GET_LAYER_BUFFER        XYLONFB_IOR(38, unsigned int)
#define XYLONFB_SET_LAYER_BUFFER        XYLONFB_IOW(39, unsigned int)
#define XYLONFB_GET_LAYER_BUFFER_OFFSET XYLONFB_IOR(40, unsigned int)
#define XYLONFB_GET_LAYER_BUFFERS_NUM   XYLONFB_IOR(41, unsigned int)
#define XYLONFB_GET_BACKGROUND_COLOR \
	XYLONFB_IOR(42, struct xylonfb_layer_color)
#define XYLONFB_SET_BACKGROUND_COLOR \
	XYLONFB_IOW(43, struct xylonfb_layer_color)
#define XYLONFB_LAYER_EXT_BUFF_SWITCH   XYLONFB_IOW(43, unsigned int)
#define XYLONFB_READ_HW_REG \
	XYLONFB_IOR(44, struct xylonfb_hw_access)
#define XYLONFB_WRITE_HW_REG \
	XYLONFB_IOW(45, struct xylonfb_hw_access)
#define XYLONFB_WAIT_EDID               XYLONFB_IOW(46, unsigned int)
#define XYLONFB_GET_EDID                XYLONFB_IOR(47, char)

#endif /* __XYLON_FB_H__ */

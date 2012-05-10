/*
 * Xylon logiCVC supported video modes definitions
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

#ifndef	__VIDEOMODES_H__
#define __VIDEOMODES_H__


#include <linux/types.h>


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

/*
    Structure contains detailed data about
    the particular display or standard VGA resolution type.
 */
struct xylonfb_vmode_params
{
	u32 ctrl_reg;
	struct fb_videomode fb_vmode;	/* Video mode parameters */
	char name[10];					/* Video mode name */
};

#endif /* __VIDEOMODES_H__ */

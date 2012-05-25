/*
 * Xylon logiCVC supported video modes
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

/*
 * This file implements hw dependent functionality for controlling pixel clock generation.
 */


#include <linux/fb.h>
#include "xylonfb-vmode.h"


struct xylonfb_vmode_params xylonfb_vmode = {
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

/*
 * Xylon logiCVC frame buffer driver miscellaneous interface functionality
 * header file
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


#ifndef __XYLON_FB_MISC__
#define __XYLON_FB_MISC__


#include "../core/xylonfb.h"


struct xylonfb_misc_data {
	wait_queue_head_t wait;
	struct fb_var_screeninfo *var_screeninfo;
	struct fb_monspecs *monspecs;
	u8 *edid;
};


void xylonfb_misc_init(struct fb_info *fbi);
void xylonfb_misc_deinit(struct fb_info *fbi);

#endif /* #ifndef __XYLON_FB_MISC__ */

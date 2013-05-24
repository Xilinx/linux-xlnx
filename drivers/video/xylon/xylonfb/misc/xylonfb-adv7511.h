/*
 * Xylon logiCVC frame buffer driver miscellaneous ADV7511 functionality
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


#ifndef __XYLON_FB_MISC_ADV7511_H__
#define __XYLON_FB_MISC_ADV7511_H__


#include <linux/types.h>


int xylonfb_adv7511_register(struct fb_info *fbi);
void xylonfb_adv7511_unregister(struct fb_info *fbi);


#endif /* #ifndef __XYLON_FB_MISC_ADV7511_H__ */

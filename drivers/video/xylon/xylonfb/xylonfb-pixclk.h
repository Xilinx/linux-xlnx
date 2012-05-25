/*
 * Xylon logiCVC frame buffer driver pixel clock generation declarations
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

#ifndef	__XYLON_FB_PIXCLK_H__
#define __XYLON_FB_PIXCLK_H__


int pixclk_set(struct fb_info *fbi);
inline int pixclk_change(struct fb_info *fbi);

#endif /* __XYLON_FB_PIXCLK_H__ */

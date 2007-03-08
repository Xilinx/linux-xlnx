/***********************************************************************
 * drivers/video/dm270fb.h
 *
 *   TI TMS320DM270 Frame Buffer Driver
 *
 *   Derived from driver/video/sa1100fb.h
 *
 *   Copyright (C) 2004 InnoMedia Pte Ltd. All rights reserved.
 *   cheetim_loh@innomedia.com.sg  <www.innomedia.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ***********************************************************************/

#define DM270FB_DEBUG

#define DM270FB_NR_FB			1

/* maximum length of fb_fix_screeninfo.id is 15 characters */
#define DM270FB_NAME			"DM270FB"

#define DISP_TYPE_COMP			0
#define DISP_TYPE_LCD			1
#define DISP_TYPE_TFT			2
#define DISP_TYPE_CRT			3
#define DISP_TYPE_EPSON			4
#define DISP_TYPE_CASIO			5

#define VID_FMT_NTSC			0
#define VID_FMT_PAL			1

#define FB_ACCEL_DM270			FB_ACCEL_NONE

#define DM270FB_NR_PALETTE		256

#define DM270FB_XRES_MIN		320
#define DM270FB_YRES_MIN		200
#define DM270FB_XRES_MAX		1024
#define DM270FB_YRES_MAX		768
#define DM270FB_BPP_MAX			8

#define DM270FB_DEFAULT_DISPTYPE	DISP_TYPE_COMP
#define DM270FB_DEFAULT_VIDFMT		VID_FMT_NTSC
#define DM270FB_DEFAULT_SYNC		FB_SYNC_COMP_HIGH_ACT
#define DM270FB_DEFAULT_VMODE		FB_VMODE_INTERLACED
#define DM270FB_DEFAULT_XRES		640
#define DM270FB_DEFAULT_YRES		480
#define DM270FB_DEFAULT_BPP		8
#define DM270FB_DEFAULT_PIXCLOCK	0
#define DM270FB_DEFAULT_LEFT_MARGIN	0
#define DM270FB_DEFAULT_RIGHT_MARGIN	0
#define DM270FB_DEFAULT_UPPER_MARGIN	0
#define DM270FB_DEFAULT_LOWER_MARGIN	0
#define DM270FB_DEFAULT_HSYNC_LEN	0
#define DM270FB_DEFAULT_VSYNC_LEN	0

#define DM270FB_OSD_BASEPX_NTSC		(120 + 32)
#define DM270FB_OSD_BASEPY_NTSC		18
#define DM270FB_OSD_BASEPX_PAL		(144 + 32)
#define DM270FB_OSD_BASEPY_PAL		22

struct dm270fb_cfg {
	int			noaccel;
	int			nopan;
	int			nowrap;
	int			nohwcursor;
	int			noinit;
	int			cmap_inverse;
	int			cmap_static;
	int			disp_type;
	int			vidout_std;
	char			fontname[40];		/* follow length of fb_info.fontname */
	char			*mode_option;
};

struct dm270fb_regaddr {
	/* address dependent on bmpwin0 or bmpwin1 */
	unsigned int		bmpwinmd;
	unsigned int		bmpwinofst;
	unsigned int		bmpwinadl;
	unsigned int		bmpwinxp;
	unsigned int		bmpwinyp;
	unsigned int		bmpwinxl;
	unsigned int		bmpwinyl;
	unsigned int		wbmp;
};

struct dm270fb_regval {
	unsigned short		vid01;
	unsigned short		vid02;
	unsigned short		bmpwinmd;
	unsigned short		rectcur;
	unsigned short		bmpwinofst;
	unsigned short		bmpwinxp;
	unsigned short		bmpwinyp;
	unsigned short		bmpwinxl;
	unsigned short		bmpwinyl;
};

struct dm270fb_cursor {
	int			type;
	int			state;
	int			w;
	int			h;
	int			u;
	int			x;
	int			y;
	int			redraw;
	unsigned long		enable;
	unsigned long		disable;
	struct timer_list	timer;
	spinlock_t		lock; 
};

struct dm270fb_par {
	struct dm270fb_cfg	cfg;
	struct dm270fb_regaddr	regaddr;
	struct dm270fb_regval	regval;
	struct dm270fb_cursor	cursor;
};

/*
 *  Debug macros 
 */
#ifdef DM270FB_DEBUG
#  define WPRINTK(fmt, args...)	printk(KERN_WARNING "dm270fb: %s: " fmt, __FUNCTION__ , ## args)
#  define DPRINTK(fmt, args...)	printk(KERN_DEBUG "dm270fb: %s: " fmt, __FUNCTION__ , ## args)
#else
#  define WPRINTK(fmt, args...)
#  define DPRINTK(fmt, args...)
#endif

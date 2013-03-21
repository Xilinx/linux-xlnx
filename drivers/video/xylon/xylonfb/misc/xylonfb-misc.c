/*
 * Xylon logiCVC frame buffer driver miscellaneous interface functionality
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


#include "xylonfb-misc.h"

#if defined(CONFIG_FB_XYLON_MISC_ADV7511)

#include "../misc/xylonfb-adv7511.h"

static void xylonfb_misc_adv7511(struct fb_info *fbi, bool init)
{
	struct xylonfb_layer_data *ld = fbi->par;
	struct xylonfb_common_data *cd = ld->xylonfb_cd;
	struct xylonfb_misc_data *misc_data = cd->xylonfb_misc;

	driver_devel("%s\n", __func__);

	if (init) {
		if (cd->xylonfb_flags & XYLONFB_FLAG_MISC_ADV7511)
			return;

		if (!xylonfb_adv7511_register(fbi)) {
			fbi->monspecs = *(misc_data->monspecs);
			cd->xylonfb_flags |= XYLONFB_FLAG_MISC_ADV7511;
		}
	} else {
		xylonfb_adv7511_unregister(fbi);
		cd->xylonfb_flags &= ~XYLONFB_FLAG_MISC_ADV7511;
	}
}
#endif

void xylonfb_misc_init(struct fb_info *fbi)
{
#if defined(CONFIG_FB_XYLON_MISC_ADV7511)
	xylonfb_misc_adv7511(fbi, true);
#endif
}

void xylonfb_misc_deinit(struct fb_info *fbi)
{
#if defined(CONFIG_FB_XYLON_MISC_ADV7511)
	xylonfb_misc_adv7511(fbi, false);
#endif
}

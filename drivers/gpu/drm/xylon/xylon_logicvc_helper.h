/*
 * Xylon DRM driver logiCVC helper functions header
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _XYLON_LOGICVC_HELPER_H_
#define _XYLON_LOGICVC_HELPER_H_

#define CVC_BACKGROUND_LAYER 5

struct xylon_cvc;

struct xylon_cvc_fix {
	unsigned int hres_min;
	unsigned int vres_min;
	unsigned int hres_max;
	unsigned int vres_max;
	unsigned int x_min;
	unsigned int y_min;
	unsigned int x_max;
	unsigned int y_max;
};

void xylon_cvc_get_fix_parameters(struct xylon_cvc *cvc,
				  struct xylon_cvc_fix *cvc_fix);

#endif /* _XYLON_LOGICVC_HELPER_H_ */

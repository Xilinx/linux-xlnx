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

#define BACKGROUND_LAYER_ID 5

enum xylon_cvc_info {
	LOGICVC_INFO_BACKGROUND_LAYER,
	LOGICVC_INFO_LAST_LAYER,
	LOGICVC_INFO_LAYER_COLOR_TRANSPARENCY,
	LOGICVC_INFO_LAYER_UPDATE,
	LOGICVC_INFO_PIXEL_DATA_INVERT,
	LOGICVC_INFO_PIXEL_DATA_TRIGGER_INVERT,
	LOGICVC_INFO_SIZE_POSITION
};

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

bool xylon_cvc_get_info(struct xylon_cvc *cvc, enum xylon_cvc_info info,
			unsigned int param);

void xylon_cvc_get_fix_parameters(struct xylon_cvc *cvc,
				  struct xylon_cvc_fix *cvc_fix);

#endif /* _XYLON_LOGICVC_HELPER_H_ */

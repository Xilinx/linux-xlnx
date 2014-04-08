/*
 * Xylon DRM driver logiCVC header for HW constants
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

#ifndef _XYLON_LOGICVC_HW_H_
#define _XYLON_LOGICVC_HW_H_

#define LOGICVC_INT_L0_UPDATED    (1 << 0)
#define LOGICVC_INT_L1_UPDATED    (1 << 1)
#define LOGICVC_INT_L2_UPDATED    (1 << 2)
#define LOGICVC_INT_L3_UPDATED    (1 << 3)
#define LOGICVC_INT_L4_UPDATED    (1 << 4)
#define LOGICVC_INT_V_SYNC        (1 << 5)
#define LOGICVC_INT_E_VIDEO_VALID (1 << 6)
#define LOGICVC_INT_FIFO_UNDERRUN (1 << 7)
#define LOGICVC_INT_L0_CLUT_SW    (1 << 8)
#define LOGICVC_INT_L1_CLUT_SW    (1 << 9)
#define LOGICVC_INT_L2_CLUT_SW    (1 << 10)
#define LOGICVC_INT_L3_CLUT_SW    (1 << 11)
#define LOGICVC_INT_L4_CLUT_SW    (1 << 12)

#endif /* _XYLON_LOGICVC_HW_H_ */

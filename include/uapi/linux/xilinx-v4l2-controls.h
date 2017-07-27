/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Xilinx Controls Header
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
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

#ifndef __UAPI_XILINX_V4L2_CONTROLS_H__
#define __UAPI_XILINX_V4L2_CONTROLS_H__

#include <linux/v4l2-controls.h>

#define V4L2_CID_XILINX_OFFSET	0xc000
#define V4L2_CID_XILINX_BASE	(V4L2_CID_USER_BASE + V4L2_CID_XILINX_OFFSET)

/*
 * Private Controls for Xilinx Video IPs
 */

/*
 * Xilinx TPG Video IP
 */

#define V4L2_CID_XILINX_TPG			(V4L2_CID_USER_BASE + 0xc000)

/* Draw cross hairs */
#define V4L2_CID_XILINX_TPG_CROSS_HAIRS		(V4L2_CID_XILINX_TPG + 1)
/* Enable a moving box */
#define V4L2_CID_XILINX_TPG_MOVING_BOX		(V4L2_CID_XILINX_TPG + 2)
/* Mask out a color component */
#define V4L2_CID_XILINX_TPG_COLOR_MASK		(V4L2_CID_XILINX_TPG + 3)
/* Enable a stuck pixel feature */
#define V4L2_CID_XILINX_TPG_STUCK_PIXEL		(V4L2_CID_XILINX_TPG + 4)
/* Enable a noisy output */
#define V4L2_CID_XILINX_TPG_NOISE		(V4L2_CID_XILINX_TPG + 5)
/* Enable the motion feature */
#define V4L2_CID_XILINX_TPG_MOTION		(V4L2_CID_XILINX_TPG + 6)
/* Configure the motion speed of moving patterns */
#define V4L2_CID_XILINX_TPG_MOTION_SPEED	(V4L2_CID_XILINX_TPG + 7)
/* The row of horizontal cross hair location */
#define V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW	(V4L2_CID_XILINX_TPG + 8)
/* The colum of vertical cross hair location */
#define V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN	(V4L2_CID_XILINX_TPG + 9)
/* Set starting point of sine wave for horizontal component */
#define V4L2_CID_XILINX_TPG_ZPLATE_HOR_START	(V4L2_CID_XILINX_TPG + 10)
/* Set speed of the horizontal component */
#define V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED	(V4L2_CID_XILINX_TPG + 11)
/* Set starting point of sine wave for vertical component */
#define V4L2_CID_XILINX_TPG_ZPLATE_VER_START	(V4L2_CID_XILINX_TPG + 12)
/* Set speed of the vertical component */
#define V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED	(V4L2_CID_XILINX_TPG + 13)
/* Moving box size */
#define V4L2_CID_XILINX_TPG_BOX_SIZE		(V4L2_CID_XILINX_TPG + 14)
/* Moving box color */
#define V4L2_CID_XILINX_TPG_BOX_COLOR		(V4L2_CID_XILINX_TPG + 15)
/* Upper limit count of generated stuck pixels */
#define V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH	(V4L2_CID_XILINX_TPG + 16)
/* Noise level */
#define V4L2_CID_XILINX_TPG_NOISE_GAIN		(V4L2_CID_XILINX_TPG + 17)
/* Foreground pattern (HLS)*/
#define V4L2_CID_XILINX_TPG_HLS_FG_PATTERN     (V4L2_CID_XILINX_TPG + 18)

/*
 * Xilinx HLS Video IP
 */

#define V4L2_CID_XILINX_HLS			(V4L2_CID_USER_BASE + 0xc060)

/* The IP model */
#define V4L2_CID_XILINX_HLS_MODEL		(V4L2_CID_XILINX_HLS + 1)

/*
 * Xilinx Gamma Correction IP
 */

/* Base ID */
#define V4L2_CID_XILINX_GAMMA_CORR		(V4L2_CID_USER_BASE + 0xc0c0)
/* Adjust Red Gamma */
#define V4L2_CID_XILINX_GAMMA_CORR_RED_GAMMA	(V4L2_CID_XILINX_GAMMA_CORR + 1)
/* Adjust Blue Gamma */
#define V4L2_CID_XILINX_GAMMA_CORR_BLUE_GAMMA	(V4L2_CID_XILINX_GAMMA_CORR + 2)
/* Adjust Green Gamma */
#define V4L2_CID_XILINX_GAMMA_CORR_GREEN_GAMMA	(V4L2_CID_XILINX_GAMMA_CORR + 3)

/*
 * Xilinx Color Space Converter (CSC) VPSS
 */

/* Base ID */
#define V4L2_CID_XILINX_CSC			(V4L2_CID_USER_BASE + 0xc0a0)
/* Adjust Brightness */
#define V4L2_CID_XILINX_CSC_BRIGHTNESS		(V4L2_CID_XILINX_CSC + 1)
/* Adjust Contrast */
#define V4L2_CID_XILINX_CSC_CONTRAST		(V4L2_CID_XILINX_CSC + 2)
/* Adjust Red Gain */
#define V4L2_CID_XILINX_CSC_RED_GAIN		(V4L2_CID_XILINX_CSC + 3)
/* Adjust Green Gain */
#define V4L2_CID_XILINX_CSC_GREEN_GAIN		(V4L2_CID_XILINX_CSC + 4)
/* Adjust Blue Gain */
#define V4L2_CID_XILINX_CSC_BLUE_GAIN		(V4L2_CID_XILINX_CSC + 5)

/*
 * Xilinx SDI Rx Subsystem
 */

/* Base ID */
#define V4L2_CID_XILINX_SDIRX			(V4L2_CID_USER_BASE + 0xc100)

/* Framer Control */
#define V4L2_CID_XILINX_SDIRX_FRAMER		(V4L2_CID_XILINX_SDIRX + 1)
/* Video Lock Window Control */
#define V4L2_CID_XILINX_SDIRX_VIDLOCK_WINDOW	(V4L2_CID_XILINX_SDIRX + 2)
/* EDH Error Mask Control */
#define V4L2_CID_XILINX_SDIRX_EDH_ERRCNT_ENABLE	(V4L2_CID_XILINX_SDIRX + 3)
/* Mode search Control */
#define V4L2_CID_XILINX_SDIRX_SEARCH_MODES	(V4L2_CID_XILINX_SDIRX + 4)
/* Get Detected Mode control */
#define V4L2_CID_XILINX_SDIRX_MODE_DETECT	(V4L2_CID_XILINX_SDIRX + 5)
/* Get CRC error status */
#define V4L2_CID_XILINX_SDIRX_CRC		(V4L2_CID_XILINX_SDIRX + 6)
/* Get EDH error count control */
#define V4L2_CID_XILINX_SDIRX_EDH_ERRCNT	(V4L2_CID_XILINX_SDIRX + 7)
/* Get EDH status control */
#define V4L2_CID_XILINX_SDIRX_EDH_STATUS	(V4L2_CID_XILINX_SDIRX + 8)
/* Get Transport Interlaced status */
#define V4L2_CID_XILINX_SDIRX_TS_IS_INTERLACED	(V4L2_CID_XILINX_SDIRX + 9)
/* Get Active Streams count */
#define V4L2_CID_XILINX_SDIRX_ACTIVE_STREAMS	(V4L2_CID_XILINX_SDIRX + 10)
/* Is Mode 3GB */
#define V4L2_CID_XILINX_SDIRX_IS_3GB		(V4L2_CID_XILINX_SDIRX + 11)
#endif /* __UAPI_XILINX_V4L2_CONTROLS_H__ */

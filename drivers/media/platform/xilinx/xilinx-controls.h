/*
 * Xilinx Controls Header
 *
 * Copyright (C) 2013 - 2014 Xilinx, Inc.
 *
 * Author: Hyun Woo Kwon <hyunk@xilinx.com>
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

#ifndef __XILINX_CONTROLS_H__
#define __XILINX_CONTROLS_H__

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

/*
 * Xilinx CRESAMPLE Video IP
 */

#define V4L2_CID_XILINX_CRESAMPLE		(V4L2_CID_USER_BASE + 0xc020)

/* The field parity for interlaced video */
#define V4L2_CID_XILINX_CRESAMPLE_FIELD_PARITY	(V4L2_CID_XILINX_CRESAMPLE + 1)
/* Specify if the first line of video contains the Chroma infomation */
#define V4L2_CID_XILINX_CRESAMPLE_CHROMA_PARITY	(V4L2_CID_XILINX_CRESAMPLE + 2)

/*
 * Xilinx RGB2YUV Video IPs
 */

#define V4L2_CID_XILINX_RGB2YUV			(V4L2_CID_USER_BASE + 0xc040)

/* Maximum Luma(Y) value */
#define V4L2_CID_XILINX_RGB2YUV_YMAX		(V4L2_CID_XILINX_RGB2YUV + 1)
/* Minimum Luma(Y) value */
#define V4L2_CID_XILINX_RGB2YUV_YMIN		(V4L2_CID_XILINX_RGB2YUV + 2)
/* Maximum Cb Chroma value */
#define V4L2_CID_XILINX_RGB2YUV_CBMAX		(V4L2_CID_XILINX_RGB2YUV + 3)
/* Minimum Cb Chroma value */
#define V4L2_CID_XILINX_RGB2YUV_CBMIN		(V4L2_CID_XILINX_RGB2YUV + 4)
/* Maximum Cr Chroma value */
#define V4L2_CID_XILINX_RGB2YUV_CRMAX		(V4L2_CID_XILINX_RGB2YUV + 5)
/* Minimum Cr Chroma value */
#define V4L2_CID_XILINX_RGB2YUV_CRMIN		(V4L2_CID_XILINX_RGB2YUV + 6)
/* The offset compensation value for Luma(Y) */
#define V4L2_CID_XILINX_RGB2YUV_YOFFSET		(V4L2_CID_XILINX_RGB2YUV + 7)
/* The offset compensation value for Cb Chroma */
#define V4L2_CID_XILINX_RGB2YUV_CBOFFSET	(V4L2_CID_XILINX_RGB2YUV + 8)
/* The offset compensation value for Cr Chroma */
#define V4L2_CID_XILINX_RGB2YUV_CROFFSET	(V4L2_CID_XILINX_RGB2YUV + 9)

/* Y = CA * R + (1 - CA - CB) * G + CB * B */

/* CA coefficient */
#define V4L2_CID_XILINX_RGB2YUV_ACOEF		(V4L2_CID_XILINX_RGB2YUV + 10)
/* CB coefficient */
#define V4L2_CID_XILINX_RGB2YUV_BCOEF		(V4L2_CID_XILINX_RGB2YUV + 11)
/* CC coefficient */
#define V4L2_CID_XILINX_RGB2YUV_CCOEF		(V4L2_CID_XILINX_RGB2YUV + 12)
/* CD coefficient */
#define V4L2_CID_XILINX_RGB2YUV_DCOEF		(V4L2_CID_XILINX_RGB2YUV + 13)

#endif /* __XILINX_CONTROLS_H__ */

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
 * Xilinx CRESAMPLE Video IP
 */

#define V4L2_CID_XILINX_CRESAMPLE		(V4L2_CID_USER_BASE + 0xc020)

/* The field parity for interlaced video */
#define V4L2_CID_XILINX_CRESAMPLE_FIELD_PARITY	(V4L2_CID_XILINX_CRESAMPLE + 1)
/* Specify if the first line of video contains the Chroma information */
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

/*
 * Xilinx VIP
 */

/* Base ID */
#define V4L2_CID_XILINX_VIP			(V4L2_CID_USER_BASE + 0xc120)

/* Low latency mode */
#define V4L2_CID_XILINX_LOW_LATENCY		(V4L2_CID_XILINX_VIP + 1)

/* Control values to enable/disable low latency capture mode */
#define XVIP_LOW_LATENCY_ENABLE		BIT(1)
#define XVIP_LOW_LATENCY_DISABLE	BIT(2)

/* Control value to start DMA */
#define XVIP_START_DMA			BIT(3)

/*
 * Xilinx SCD
 */

/* Base ID */
#define V4L2_CID_XILINX_SCD			(V4L2_CID_USER_BASE + 0xc140)

/*
 * SCD Threshold
 * User can pass percentage as an integer to tune threshold value
 */
#define V4L2_CID_XILINX_SCD_THRESHOLD		(V4L2_CID_XILINX_SCD + 1)

/*
 * Xilinx Video ISP Pipeline IP
 */

/* Base ID */
#define V4L2_CID_XILINX_ISP			(V4L2_CID_USER_BASE + 0xc160)
/* Adjust Red Gain */
#define V4L2_CID_XILINX_ISP_RED_GAIN		(V4L2_CID_XILINX_ISP + 1)
/* Adjust Blue Gain */
#define V4L2_CID_XILINX_ISP_BLUE_GAIN		(V4L2_CID_XILINX_ISP + 2)
/* Adjust AWB */
#define V4L2_CID_XILINX_ISP_AWB			(V4L2_CID_XILINX_ISP + 3)
/* Adjust Threshold */
#define V4L2_CID_XILINX_ISP_THRESHOLD		(V4L2_CID_XILINX_ISP + 4)
/* Adjust Red Gamma */
#define V4L2_CID_XILINX_ISP_RED_GAMMA		(V4L2_CID_XILINX_ISP + 5)
/* Adjust Green Gamma */
#define V4L2_CID_XILINX_ISP_GREEN_GAMMA		(V4L2_CID_XILINX_ISP + 6)
/* Adjust Blue Gamma */
#define V4L2_CID_XILINX_ISP_BLUE_GAMMA		(V4L2_CID_XILINX_ISP + 7)
/* Enable/Disable AEC */
#define V4L2_CID_XILINX_ISP_AEC_EN		(V4L2_CID_XILINX_ISP + 8)
/* Adjust Threshold */
#define V4L2_CID_XILINX_ISP_AEC_THRESHOLD		(V4L2_CID_XILINX_ISP + 9)
/* Enable/Disable BLC */
#define V4L2_CID_XILINX_ISP_BLC_EN		(V4L2_CID_XILINX_ISP + 10)
/* Adjust Multiplication Factor */
#define V4L2_CID_XILINX_ISP_MULTI_FACTOR	(V4L2_CID_XILINX_ISP + 11)
/* Enable/Disable AWB */
#define V4L2_CID_XILINX_ISP_AWB_EN		(V4L2_CID_XILINX_ISP + 12)
/* Adjust AWB */
#define V4L2_CID_XILINX_ISP_AWB_THRESHOLD	(V4L2_CID_XILINX_ISP + 13)
/* Enable/Disable BPC */
#define V4L2_CID_XILINX_ISP_BPC_EN		(V4L2_CID_XILINX_ISP + 14)
/* Enable/Disable Degamma */
#define V4L2_CID_XILINX_ISP_DEGAMMA_EN		(V4L2_CID_XILINX_ISP + 15)
/* Adjust Degamma values */
#define V4L2_CID_XILINX_ISP_DEGAMMA_PARAMS		(V4L2_CID_XILINX_ISP + 16)
/* Enable/Disable RGBIR */
#define V4L2_CID_XILINX_ISP_RGBIR_EN		(V4L2_CID_XILINX_ISP + 17)
/* Enable/Disable LSC */
#define V4L2_CID_XILINX_ISP_LSC_EN		(V4L2_CID_XILINX_ISP + 18)
/* Enable/Disable Demosaic */
#define V4L2_CID_XILINX_ISP_DEMOSAIC_EN		(V4L2_CID_XILINX_ISP + 39)
/* Enable/Disable CCM */
#define V4L2_CID_XILINX_ISP_CCM_EN		(V4L2_CID_XILINX_ISP + 20)
/* Adjust CCM values */
#define V4L2_CID_XILINX_ISP_CCM_PARAMS		(V4L2_CID_XILINX_ISP + 21)
/* Enable/Disable Gain Control */
#define V4L2_CID_XILINX_ISP_GAIN_EN		(V4L2_CID_XILINX_ISP + 22)
/* Adjust Gain control Red Gain */
#define V4L2_CID_XILINX_ISP_GAIN_CONTROL_RED_GAIN		(V4L2_CID_XILINX_ISP + 23)
/* Adjust Gain control Blue Gain */
#define V4L2_CID_XILINX_ISP_GAIN_CONTROL_BLUE_GAIN		(V4L2_CID_XILINX_ISP + 24)
/* Adjust Gain control Green Gain */
#define V4L2_CID_XILINX_ISP_GAIN_CONTROL_GREEN_GAIN		(V4L2_CID_XILINX_ISP + 52)
/* Adjust Luminance Gain */
#define V4L2_CID_XILINX_ISP_LUMA_GAIN		(V4L2_CID_XILINX_ISP + 25)
/* Enable/Disable Gamma */
#define V4L2_CID_XILINX_ISP_GAMMA_EN		(V4L2_CID_XILINX_ISP + 26)
/* Enable/Disable HDR */
#define V4L2_CID_XILINX_ISP_HDR_EN			(V4L2_CID_XILINX_ISP + 27)
/* Adjust Decompand values */
#define V4L2_CID_XILINX_ISP_DECOMPAND_PARAMS		(V4L2_CID_XILINX_ISP + 28)
/* Adjust HDR Merge Alpha */
#define V4L2_CID_XILINX_ISP_ALPHA		(V4L2_CID_XILINX_ISP + 29)
/* Adjust HDR Merge Optical Black Value */
#define V4L2_CID_XILINX_ISP_OPTICAL_BLACK_VALUE	(V4L2_CID_XILINX_ISP + 30)
/* Adjust HDR Merge Intersec */
#define V4L2_CID_XILINX_ISP_INTERSEC		(V4L2_CID_XILINX_ISP + 31)
/* Adjust HDR Merge Rho */
#define V4L2_CID_XILINX_ISP_RHO			(V4L2_CID_XILINX_ISP + 32)
/* Enable/Disable TM */
#define V4L2_CID_XILINX_ISP_TM_EN		(V4L2_CID_XILINX_ISP + 33)
/* Adjust GTM C1 */
#define V4L2_CID_XILINX_ISP_GTM_C1		(V4L2_CID_XILINX_ISP + 34)
/* Adjust GTM C2 */
#define V4L2_CID_XILINX_ISP_GTM_C2		(V4L2_CID_XILINX_ISP + 35)
/* Adjust LTM Block Rows */
#define V4L2_CID_XILINX_ISP_BLOCK_ROWS		(V4L2_CID_XILINX_ISP + 36)
/* Adjust LTM Block Cols */
#define V4L2_CID_XILINX_ISP_BLOCK_COLS		(V4L2_CID_XILINX_ISP + 37)
/* Enable/Disable 3dLut */
#define V4L2_CID_XILINX_ISP_3DLUT_EN		(V4L2_CID_XILINX_ISP + 38)
/* Adjust 3dLut dimension */
#define V4L2_CID_XILINX_ISP_3DLUT_DIM		(V4L2_CID_XILINX_ISP + 40)
/* Enable/Disable CSC */
#define V4L2_CID_XILINX_ISP_CSC_EN		(V4L2_CID_XILINX_ISP + 41)
/* Enable/Disable Bayer stats */
#define V4L2_CID_XILINX_ISP_BAYER_STATS_EN		(V4L2_CID_XILINX_ISP + 42)
/* Enable/Disable Luma stats */
#define V4L2_CID_XILINX_ISP_LUMA_STATS_EN		(V4L2_CID_XILINX_ISP + 43)
/* Enable/Disable RGB stats */
#define V4L2_CID_XILINX_ISP_RGB_STATS_EN		(V4L2_CID_XILINX_ISP + 44)
/* Enable/Disable Clahe */
#define V4L2_CID_XILINX_ISP_CLAHE_EN		(V4L2_CID_XILINX_ISP + 45)
/* Adjust CLAHE Clip */
#define V4L2_CID_XILINX_ISP_CLIP		(V4L2_CID_XILINX_ISP + 46)
/* Adjust CLAHE TilesY */
#define V4L2_CID_XILINX_ISP_TILESY		(V4L2_CID_XILINX_ISP + 47)
/* Adjust CLAHE TilesX */
#define V4L2_CID_XILINX_ISP_TILESX		(V4L2_CID_XILINX_ISP + 48)
/* Enable/Disable Median Blur */
#define V4L2_CID_XILINX_ISP_MEDIAN_EN		(V4L2_CID_XILINX_ISP + 49)
/* Enable/Disable Resize */
#define V4L2_CID_XILINX_ISP_RESIZE_EN		(V4L2_CID_XILINX_ISP + 50)
/* Enable/Disable ISP */
#define V4L2_CID_XILINX_ISP_EN			(V4L2_CID_XILINX_ISP + 51)

#endif /* __UAPI_XILINX_V4L2_CONTROLS_H__ */

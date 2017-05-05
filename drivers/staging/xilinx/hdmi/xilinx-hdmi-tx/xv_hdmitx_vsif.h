/******************************************************************************
*
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xv_hdmitx_vsif.h
*
* This is the main header file for Vendor Specific InfoFrames used in HDMI.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  yh     15/01/15 Initial release for 3D video support
* </pre>
*
******************************************************************************/

#ifndef XV_HDMITX_VSIF_H_
/* Prevent circular inclusions by using protection macros. */
#define XV_HDMITX_VSIF_H_

#ifdef __cplusplus
extern "C" {
#endif

/******************************* Include Files ********************************/
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"

#include "xvidc.h"

/************************** Constant Definitions ******************************/

/** @name Vendor Specific InfoFrame Field Masks and Shifts.
 * @{
 */
#define XV_HDMITX_VSIF_VIDEO_FORMAT_SHIFT 5
#define XV_HDMITX_VSIF_VIDEO_FORMAT_MASK (0x7 << XV_HDMITX_VSIF_VIDEO_FORMAT_SHIFT)

#define XV_HDMITX_3D_STRUCT_SHIFT 4
#define XV_HDMITX_3D_STRUCT_MASK (0xF << XV_HDMITX_3D_STRUCT_SHIFT)

#define XV_HDMITX_3D_SAMP_METHOD_SHIFT 6
#define XV_HDMITX_3D_SAMP_METHOD_MASK (0x3 << XV_HDMITX_3D_SAMP_METHOD_SHIFT)

#define XV_HDMITX_3D_SAMP_POS_SHIFT 4
#define XV_HDMITX_3D_SAMP_POS_MASK (0x3 << XV_HDMITX_3D_SAMP_POS_SHIFT)

#define XV_HDMITX_3D_META_PRESENT_SHIFT 3
#define XV_HDMITX_3D_META_PRESENT_MASK (0x1 << XV_HDMITX_3D_META_PRESENT_SHIFT)

#define XV_HDMITX_3D_META_TYPE_SHIFT 5
#define XV_HDMITX_3D_META_TYPE_MASK (0x7 << XV_HDMITX_3D_META_TYPE_SHIFT)

#define XV_HDMITX_3D_META_LENGTH_SHIFT 0
#define XV_HDMITX_3D_META_LENGTH_MASK (0x1F << XV_HDMITX_3D_META_LENGTH_SHIFT)
/* @} */

/**************************** Type Definitions *******************************/

/**
 * HDMI Video Format
 */
typedef enum {
    XV_HDMITX_VSIF_VF_NOINFO = 0, /**<No additional HDMI video format is presented */
    XV_HDMITX_VSIF_VF_EXTRES = 1, /**<Extended resolution format present */
    XV_HDMITX_VSIF_VF_3D     = 2, /**<3D format indication present */
    XV_HDMITX_VSIF_VF_UNKNOWN
} XV_HdmiTx_VSIF_Video_Format;

/**
 * 3D structure definitions as defined in the HDMI 1.4a specification
 */
typedef enum {
    XV_HDMITX_3D_STRUCT_FRAME_PACKING        = 0, /**<Frame packing */
    XV_HDMITX_3D_STRUCT_FIELD_ALTERNATIVE    = 1, /**<Field alternative */
    XV_HDMITX_3D_STRUCT_LINE_ALTERNATIVE     = 2, /**<Line alternative */
    XV_HDMITX_3D_STRUCT_SIDE_BY_SIDE_FULL    = 3, /**<Side-by-side (full) */
    XV_HDMITX_3D_STRUCT_L_DEPTH              = 4, /**<L + depth */
    XV_HDMITX_3D_STRUCT_L_DEPTH_GRAPH_GDEPTH = 5, /**<L + depth + graphics + graphics-depth */
    XV_HDMITX_3D_STRUCT_TOP_AND_BOTTOM       = 6, /**<Top-and-bottom */
    // 7 is reserved for future use
    XV_HDMITX_3D_STRUCT_SIDE_BY_SIDE_HALF    = 8, /**<Side-by-side (half) */
    XV_HDMITX_3D_STRUCT_UNKNOWN
} XV_HdmiTx_3D_Struct_Field;

/**
 * Sub-sampling methods for Side-by-side(half)
 */
typedef enum {
    XV_HDMITX_3D_SAMPLING_HORIZONTAL = 0, /**<Horizontal sub-sampling */
    XV_HDMITX_3D_SAMPLING_QUINCUNX   = 1, /**<Quincunx matrix */
    XV_HDMITX_3D_SAMPLING_UNKNOWN
} XV_HdmiTx_3D_Sampling_Method;

/**
 * Sub-sampling positions for the sub-sampling methods
 */
typedef enum {
    XV_HDMITX_3D_SAMPPOS_OLOR = 0, /**<Odd/Left, Odd/Right */
    XV_HDMITX_3D_SAMPPOS_OLER = 1, /**<Odd/Left, Even/Right */
    XV_HDMITX_3D_SAMPPOS_ELOR = 2, /**<Even/Left, Odd/Right */
    XV_HDMITX_3D_SAMPPOS_ELER = 3, /**<Even/Left, Even/Right */
    XV_HDMITX_3D_SAMPPOS_UNKNOWN
} XV_HdmiTx_3D_Sampling_Position;

/**
 * 3D Metadata types
 */
typedef enum {
    XV_HDMITX_3D_META_PARALLAX = 0, /**<Parallax information */
    XV_HDMITX_3D_META_UNKNOWN
} XV_HdmiTx_3D_MetaData_Type;


// 8 is the maximum size for currently defined meta types (HDMI 1.4a)
#define XV_HDMITX_3D_META_MAX_SIZE 8 /**<Maximum 3D Metadata size in bytes */

/**
 * Structure for 3D meta data
 */
typedef struct {
    u8                     IsPresent;                    /**<Indicates 3D metadata presence */
    XV_HdmiTx_3D_MetaData_Type Type;                         /**<Type */
    u8                     Length;                       /**<Length in bytes */
    u8                     Data[XV_HDMITX_3D_META_MAX_SIZE]; /**<Data */
} XV_HdmiTx_3D_MetaData;

/**
 * Structure containing 3D information
 */
typedef struct {
    XVidC_3DInfo               Stream;
    XV_HdmiTx_3D_MetaData          MetaData;   /**<3D Metadata */
} XV_HdmiTx_3D_Info;

/**
 * Structure for holding the VSIF.
 * Format indicates the used union member.
 */
typedef struct {
    u8                      Version; /**<Version */
    u32                     IEEE_ID; /**<IEEE Registration Identifier */
    XV_HdmiTx_VSIF_Video_Format Format;  /**<HDMI Video Format */

    union {
        u8            HDMI_VIC; /**<XV_HDMITX_VSIF_VF_EXTRES: HDMI Video Format Identification Code */
        XV_HdmiTx_3D_Info Info_3D;  /**<XV_HDMITX_VSIF_VF_3D: 3D Information */
    };
} XV_HdmiTx_VSIF;

/***************** Macros (Inline Functions) Definitions *********************/

#ifdef __cplusplus
}
#endif

#endif /* End of protection macro */

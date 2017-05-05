/*
 * Xilinx VPSS Color Space Converter
 *
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __XILINX_CSC_H__
#define __XILINX_CSC_H__

/* CTRL
 * 0x000 : Control signals
 *         bit 0  - ap_start (Read/Write/COH)
 *         bit 1  - ap_done (Read/COR)
 *         bit 2  - ap_idle (Read)
 *         bit 3  - ap_ready (Read)
 *         bit 7  - auto_restart (Read/Write)
 *         others - reserved
 * 0x004 : Global Interrupt Enable Register
 *         bit 0  - Global Interrupt Enable (Read/Write)
 *         others - reserved
 * 0x008 : IP Interrupt Enable Register (Read/Write)
 *         bit 0  - Channel 0 (ap_done)
 *         bit 1  - Channel 1 (ap_ready)
 *         others - reserved
 * 0x00c : IP Interrupt Status Register (Read/TOW)
 *         bit 0  - Channel 0 (ap_done)
 *         bit 1  - Channel 1 (ap_ready)
 *         others - reserved
 * 0x010 : Data signal of HwReg_InVideoFormat
 *         bit 7~0 - HwReg_InVideoFormat[7:0] (Read/Write)
 *         others  - reserved
 * 0x014 : reserved
 * 0x018 : Data signal of HwReg_OutVideoFormat
 *         bit 7~0 - HwReg_OutVideoFormat[7:0] (Read/Write)
 *         others  - reserved
 * 0x01c : reserved
 * 0x020 : Data signal of HwReg_width
 *         bit 15~0 - HwReg_width[15:0] (Read/Write)
 *         others   - reserved
 * 0x024 : reserved
 * 0x028 : Data signal of HwReg_height
 *         bit 15~0 - HwReg_height[15:0] (Read/Write)
 *         others   - reserved
 * 0x02c : reserved
 * 0x030 : Data signal of HwReg_ColStart
 *         bit 15~0 - HwReg_ColStart[15:0] (Read/Write)
 *         others   - reserved
 * 0x034 : reserved
 * 0x038 : Data signal of HwReg_ColEnd
 *         bit 15~0 - HwReg_ColEnd[15:0] (Read/Write)
 *         others   - reserved
 * 0x03c : reserved
 * 0x040 : Data signal of HwReg_RowStart
 *         bit 15~0 - HwReg_RowStart[15:0] (Read/Write)
 *         others   - reserved
 * 0x044 : reserved
 * 0x048 : Data signal of HwReg_RowEnd
 *         bit 15~0 - HwReg_RowEnd[15:0] (Read/Write)
 *         others   - reserved
 * 0x04c : reserved
 * 0x050 : Data signal of HwReg_K11
 *         bit 15~0 - HwReg_K11[15:0] (Read/Write)
 *         others   - reserved
 * 0x054 : reserved
 * 0x058 : Data signal of HwReg_K12
 *         bit 15~0 - HwReg_K12[15:0] (Read/Write)
 *         others   - reserved
 * 0x05c : reserved
 * 0x060 : Data signal of HwReg_K13
 *         bit 15~0 - HwReg_K13[15:0] (Read/Write)
 *         others   - reserved
 * 0x064 : reserved
 * 0x068 : Data signal of HwReg_K21
 *         bit 15~0 - HwReg_K21[15:0] (Read/Write)
 *         others   - reserved
 * 0x06c : reserved
 * 0x070 : Data signal of HwReg_K22
 *         bit 15~0 - HwReg_K22[15:0] (Read/Write)
 *         others   - reserved
 * 0x074 : reserved
 * 0x078 : Data signal of HwReg_K23
 *         bit 15~0 - HwReg_K23[15:0] (Read/Write)
 *         others   - reserved
 * 0x07c : reserved
 * 0x080 : Data signal of HwReg_K31
 *         bit 15~0 - HwReg_K31[15:0] (Read/Write)
 *         others   - reserved
 * 0x084 : reserved
 * 0x088 : Data signal of HwReg_K32
 *         bit 15~0 - HwReg_K32[15:0] (Read/Write)
 *         others   - reserved
 * 0x08c : reserved
 * 0x090 : Data signal of HwReg_K33
 *         bit 15~0 - HwReg_K33[15:0] (Read/Write)
 *         others   - reserved
 * 0x094 : reserved
 * 0x098 : Data signal of HwReg_ROffset_V
 *         bit 11~0 - HwReg_ROffset_V[11:0] (Read/Write)
 *         others   - reserved
 * 0x09c : reserved
 * 0x0a0 : Data signal of HwReg_GOffset_V
 *         bit 11~0 - HwReg_GOffset_V[11:0] (Read/Write)
 *         others   - reserved
 * 0x0a4 : reserved
 * 0x0a8 : Data signal of HwReg_BOffset_V
 *         bit 11~0 - HwReg_BOffset_V[11:0] (Read/Write)
 *         others   - reserved
 * 0x0ac : reserved
 * 0x0b0 : Data signal of HwReg_ClampMin_V
 *         bit 9~0 - HwReg_ClampMin_V[9:0] (Read/Write)
 *         others  - reserved
 * 0x0b4 : reserved
 * 0x0b8 : Data signal of HwReg_ClipMax_V
 *         bit 9~0 - HwReg_ClipMax_V[9:0] (Read/Write)
 *         others  - reserved
 * 0x0bc : reserved
 * 0x0c0 : Data signal of HwReg_K11_2
 *         bit 15~0 - HwReg_K11_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x0c4 : reserved
 * 0x0c8 : Data signal of HwReg_K12_2
 *         bit 15~0 - HwReg_K12_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x0cc : reserved
 * 0x0d0 : Data signal of HwReg_K13_2
 *         bit 15~0 - HwReg_K13_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x0d4 : reserved
 * 0x0d8 : Data signal of HwReg_K21_2
 *         bit 15~0 - HwReg_K21_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x0dc : reserved
 * 0x0e0 : Data signal of HwReg_K22_2
 *         bit 15~0 - HwReg_K22_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x0e4 : reserved
 * 0x0e8 : Data signal of HwReg_K23_2
 *         bit 15~0 - HwReg_K23_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x0ec : reserved
 * 0x0f0 : Data signal of HwReg_K31_2
 *         bit 15~0 - HwReg_K31_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x0f4 : reserved
 * 0x0f8 : Data signal of HwReg_K32_2
 *         bit 15~0 - HwReg_K32_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x0fc : reserved
 * 0x100 : Data signal of HwReg_K33_2
 *         bit 15~0 - HwReg_K33_2[15:0] (Read/Write)
 *         others   - reserved
 * 0x104 : reserved
 * 0x108 : Data signal of HwReg_ROffset_2_V
 *         bit 11~0 - HwReg_ROffset_2_V[11:0] (Read/Write)
 *         others   - reserved
 * 0x10c : reserved
 * 0x110 : Data signal of HwReg_GOffset_2_V
 *         bit 11~0 - HwReg_GOffset_2_V[11:0] (Read/Write)
 *         others   - reserved
 * 0x114 : reserved
 * 0x118 : Data signal of HwReg_BOffset_2_V
 *         bit 11~0 - HwReg_BOffset_2_V[11:0] (Read/Write)
 *         others   - reserved
 * 0x11c : reserved
 * 0x120 : Data signal of HwReg_ClampMin_2_V
 *         bit 9~0 - HwReg_ClampMin_2_V[9:0] (Read/Write)
 *         others  - reserved
 * 0x124 : reserved
 * 0x128 : Data signal of HwReg_ClipMax_2_V
 *         bit 9~0 - HwReg_ClipMax_2_V[9:0] (Read/Write)
 *         others  - reserved
 * 0x12c : reserved
 * (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write,
 * COH = Clear on Handshake)
 */

#include <linux/bitops.h>

#define XV_CSC_AP_CTRL					(0x000)
#define XV_CSC_INVIDEOFORMAT				(0x010)
#define XV_CSC_OUTVIDEOFORMAT				(0x018)
#define XV_CSC_WIDTH					(0x020)
#define XV_CSC_HEIGHT					(0x028)
#define XV_CSC_K11					(0x050)
#define XV_CSC_K12					(0x058)
#define XV_CSC_K13					(0x060)
#define XV_CSC_K21					(0x068)
#define XV_CSC_K22					(0x070)
#define XV_CSC_K23					(0x078)
#define XV_CSC_K31					(0x080)
#define XV_CSC_K32					(0x088)
#define XV_CSC_K33					(0x090)
#define XV_CSC_ROFFSET					(0x098)
#define XV_CSC_GOFFSET					(0x0a0)
#define XV_CSC_BOFFSET					(0x0a8)
#define XV_CSC_CLAMPMIN					(0x0b0)
#define XV_CSC_CLIPMAX					(0x0b8)

#define XV_CSC_FRACTIONAL_BITS	(12)
#define XV_CSC_SCALE_FACTOR	(4096)
#define XV_CSC_RGB_OFFSET_WR(x)	(((x) >> 12) & 0x3FF)
#define XV_CSC_DIVISOR		(10000)
#define XV_CSC_DEFAULT_HEIGHT	(720)
#define XV_CSC_DEFAULT_WIDTH	(1280)
#define XV_CSC_K_MAX_ROWS	(3)
#define XV_CSC_K_MAX_COLUMNS	(3)

#endif /* __XILINX_CSC_H__ */

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
* @file xhdcp22_mmult_hw.h
* @addtogroup hdcp22_mmult_v1_1
* @{
* @details
*
* This header file contains identifiers and register-level core functions (or
* macros) that can be used to access the Xilinx HDCP22 Montgomery Multipler
* (Mmult) core.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  MH     12/07/15 Initial release.
* </pre>
*
******************************************************************************/

// CTRL
// 0x000 : Control signals
//         bit 0  - ap_start (Read/Write/COH)
//         bit 1  - ap_done (Read/COR)
//         bit 2  - ap_idle (Read)
//         bit 3  - ap_ready (Read)
//         bit 7  - auto_restart (Read/Write)
//         others - reserved
// 0x004 : Global Interrupt Enable Register
//         bit 0  - Global Interrupt Enable (Read/Write)
//         others - reserved
// 0x008 : IP Interrupt Enable Register (Read/Write)
//         bit 0  - Channel 0 (ap_done)
//         bit 1  - Channel 1 (ap_ready)
//         others - reserved
// 0x00c : IP Interrupt Status Register (Read/TOW)
//         bit 0  - Channel 0 (ap_done)
//         bit 1  - Channel 1 (ap_ready)
//         others - reserved
// 0x040 ~
// 0x07f : Memory 'U' (16 * 32b)
//         Word n : bit [31:0] - U[n]
// 0x080 ~
// 0x0bf : Memory 'A' (16 * 32b)
//         Word n : bit [31:0] - A[n]
// 0x0c0 ~
// 0x0ff : Memory 'B' (16 * 32b)
//         Word n : bit [31:0] - B[n]
// 0x100 ~
// 0x13f : Memory 'N' (16 * 32b)
//         Word n : bit [31:0] - N[n]
// 0x140 ~
// 0x17f : Memory 'NPrime' (16 * 32b)
//         Word n : bit [31:0] - NPrime[n]
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XHDCP22_MMULT_CTRL_ADDR_AP_CTRL     0x000
#define XHDCP22_MMULT_CTRL_ADDR_GIE         0x004
#define XHDCP22_MMULT_CTRL_ADDR_IER         0x008
#define XHDCP22_MMULT_CTRL_ADDR_ISR         0x00c
#define XHDCP22_MMULT_CTRL_ADDR_U_BASE      0x040
#define XHDCP22_MMULT_CTRL_ADDR_U_HIGH      0x07f
#define XHDCP22_MMULT_CTRL_WIDTH_U          32
#define XHDCP22_MMULT_CTRL_DEPTH_U          16
#define XHDCP22_MMULT_CTRL_ADDR_A_BASE      0x080
#define XHDCP22_MMULT_CTRL_ADDR_A_HIGH      0x0bf
#define XHDCP22_MMULT_CTRL_WIDTH_A          32
#define XHDCP22_MMULT_CTRL_DEPTH_A          16
#define XHDCP22_MMULT_CTRL_ADDR_B_BASE      0x0c0
#define XHDCP22_MMULT_CTRL_ADDR_B_HIGH      0x0ff
#define XHDCP22_MMULT_CTRL_WIDTH_B          32
#define XHDCP22_MMULT_CTRL_DEPTH_B          16
#define XHDCP22_MMULT_CTRL_ADDR_N_BASE      0x100
#define XHDCP22_MMULT_CTRL_ADDR_N_HIGH      0x13f
#define XHDCP22_MMULT_CTRL_WIDTH_N          32
#define XHDCP22_MMULT_CTRL_DEPTH_N          16
#define XHDCP22_MMULT_CTRL_ADDR_NPRIME_BASE 0x140
#define XHDCP22_MMULT_CTRL_ADDR_NPRIME_HIGH 0x17f
#define XHDCP22_MMULT_CTRL_WIDTH_NPRIME     32
#define XHDCP22_MMULT_CTRL_DEPTH_NPRIME     16

/** @} */

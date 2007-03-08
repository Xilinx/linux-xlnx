/* $Id: xpacket_fifo_l_v2_00_a.h,v 1.1 2006/12/13 14:23:01 imanuilov Exp $ */
/******************************************************************************
*
*       XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
*       AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
*       SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
*       OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
*       APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
*       THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
*       AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
*       FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
*       WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
*       IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
*       REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
*       INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*       FOR A PARTICULAR PURPOSE.
*
*       (c) Copyright 2003-2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xpacket_fifo_l_v2_00_a.h
*
* This header file contains identifiers and low-level (Level 0) driver
* functions (or macros) that can be used to access the FIFO.  High-level driver
* (Level 1) functions are defined in xpacket_fifo_v2_00_a.h.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- ------------------------------------------------------
* 2.00a rpm  10/22/03  First release. Moved most of Level 1 driver functions
*                      into this layer.
* 2.00a rmm  02/24/04  Added L0WriteDre function.
* 2.00a xd   10/27/04  Changed comments to support doxygen for API
*                      documentation.
* </pre>
*
*****************************************************************************/
#ifndef XPACKET_FIFO_L_V200A_H	/* prevent circular inclusions */
#define XPACKET_FIFO_L_V200A_H	/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xstatus.h"

/************************** Constant Definitions *****************************/

/** @name FIFO types
 *
 * These constants specify the FIFO type and are mutually exclusive
 * @{
 */
#define XPF_V200A_READ_FIFO_TYPE      0	    /**< a read FIFO */
#define XPF_V200A_WRITE_FIFO_TYPE     1	    /**< a write FIFO */
/* @} */

/** @name Register offsets
 *
 * These constants define the offsets to each of the registers from the
 * register base address, each of the constants are a number of bytes
 * @{
 */
#define XPF_V200A_RESET_REG_OFFSET            0UL /**< Reset register */
#define XPF_V200A_MODULE_INFO_REG_OFFSET      0UL /**< MIR register */
#define XPF_V200A_COUNT_STATUS_REG_OFFSET     4UL /**< Count/Status register */
/* @} */

/**
 * This constant is used with the Reset Register
 */
#define XPF_V200A_RESET_FIFO_MASK             0x0000000A

/** @name Occupancy/Vacancy Count Register constants
 * @{
 */
/** Constant used with the Occupancy/Vacancy Count Register. This
 *  register also contains FIFO status
 */
#define XPF_V200A_COUNT_MASK                  0x00FFFFFF
#define XPF_V200A_DEADLOCK_MASK               0x20000000
#define XPF_V200A_ALMOST_EMPTY_FULL_MASK      0x40000000
#define XPF_V200A_EMPTY_FULL_MASK             0x80000000
#define XPF_V200A_VACANCY_SCALED_MASK         0x10000000
/* @} */

/**
 * This constant is used to mask the Width field
 */
#define XPF_V200A_FIFO_WIDTH_MASK             0x0E000000

/** @name Width field
 * @{
 */
/** Constant used with the Width field */
#define XPF_V200A_FIFO_WIDTH_LEGACY_TYPE      0x00000000
#define XPF_V200A_FIFO_WIDTH_8BITS_TYPE       0x02000000
#define XPF_V200A_FIFO_WIDTH_16BITS_TYPE      0x04000000
#define XPF_V200A_FIFO_WIDTH_32BITS_TYPE      0x06000000
#define XPF_V200A_FIFO_WIDTH_64BITS_TYPE      0x08000000
#define XPF_V200A_FIFO_WIDTH_128BITS_TYPE     0x0A000000
#define XPF_V200A_FIFO_WIDTH_256BITS_TYPE     0x0C000000
#define XPF_V200A_FIFO_WIDTH_512BITS_TYPE     0x0E000000
/* @} */

/** @name FIFO word width
 * @{
 */
/** Width of a FIFO word */
#define XPF_V200A_32BIT_FIFO_WIDTH_BYTE_COUNT       4
#define XPF_V200A_64BIT_FIFO_WIDTH_BYTE_COUNT       8
/* @} */

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

int XPacketFifoV200a_L0Read(u32 RegBaseAddress,
			    u32 DataBaseAddress,
			    u8 *ReadBufferPtr, u32 ByteCount);

int XPacketFifoV200a_L0Write(u32 RegBaseAddress,
			     u32 DataBaseAddress,
			     u8 *WriteBufferPtr, u32 ByteCount);

int XPacketFifoV200a_L0WriteDre(u32 RegBaseAddress,
				u32 DataBaseAddress,
				u8 *BufferPtr, u32 ByteCount);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

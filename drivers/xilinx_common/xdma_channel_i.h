/* $Id: xdma_channel_i.h,v 1.1 2006/12/13 14:22:04 imanuilov Exp $ */
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
*       (c) Copyright 2001-2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xdma_channel_i.h
*
* <b>Description</b>
*
* This file contains data which is shared internal data for the DMA channel
* component. It is also shared with the buffer descriptor component which is
* very tightly coupled with the DMA channel component.
*
* @note
*
* The last buffer descriptor constants must be located here to prevent a
* circular dependency between the DMA channel component and the buffer
* descriptor component.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a xd   10/27/04 Doxygenated for inclusion in API documentation
* 1.00b ecm  10/31/05 Updated for the check sum offload changes.
* </pre>
*
******************************************************************************/

#ifndef XDMA_CHANNEL_I_H	/* prevent circular inclusions */
#define XDMA_CHANNEL_I_H	/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif


/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xstatus.h"
#include "xversion.h"

/************************** Constant Definitions *****************************/

#define XDC_DMA_CHANNEL_V1_00_B     "1.00b"

/** @name DMA control register bit fields
 *
 * the following constant provides access to the bit fields of the DMA control
 * register (DMACR) which must be shared between the DMA channel component
 * and the buffer descriptor component
 * @{
 */
#define XDC_CONTROL_LAST_BD_MASK    0x02000000UL /**< last buffer descriptor */
/* @} */

/** @name DMA status register bit fields
 *
 * the following constant provides access to the bit fields of the DMA status
 * register (DMASR) which must be shared between the DMA channel component
 * and the buffer descriptor component
 * @{
 */
#define XDC_STATUS_LAST_BD_MASK     0x10000000UL /**< last buffer descriptor */

#define XDC_DMASR_RX_CS_RAW_MASK    0xFFFF0000UL /**< RAW CS value for RX data */
/* @} */

/** @name DMA Channel register offsets
 *
 * the following constants provide access to each of the registers of a DMA
 * channel
 * @{
 */
#define XDC_RST_REG_OFFSET  0	/**< reset register */
#define XDC_MI_REG_OFFSET   0	/**< module information register */
#define XDC_DMAC_REG_OFFSET 4	/**< DMA control register */
#define XDC_SA_REG_OFFSET   8	/**< source address register */
#define XDC_DA_REG_OFFSET   12	/**< destination address register */
#define XDC_LEN_REG_OFFSET  16	/**< length register */
#define XDC_DMAS_REG_OFFSET 20	/**< DMA status register */
#define XDC_BDA_REG_OFFSET  24	/**< buffer descriptor address register */
#define XDC_SWCR_REG_OFFSET 28	/**< software control register */
#define XDC_UPC_REG_OFFSET  32	/**< unserviced packet count register */
#define XDC_PCT_REG_OFFSET  36	/**< packet count threshold register */
#define XDC_PWB_REG_OFFSET  40	/**< packet wait bound register */
#define XDC_IS_REG_OFFSET   44	/**< interrupt status register */
#define XDC_IE_REG_OFFSET   48	/**< interrupt enable register */
/* @} */

/**
 * the following constant is written to the reset register to reset the
 * DMA channel
 */
#define XDC_RESET_MASK              0x0000000AUL

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

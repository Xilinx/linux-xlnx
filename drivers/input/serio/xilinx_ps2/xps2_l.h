/******************************************************************************
*
*     Author: Xilinx, Inc.
*
*
*     This program is free software; you can redistribute it and/or modify it
*     under the terms of the GNU General Public License as published by the
*     Free Software Foundation; either version 2 of the License, or (at your
*     option) any later version.
*
*
*     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A
*     COURTESY TO YOU. BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS
*     ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE, APPLICATION OR STANDARD,
*     XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION IS FREE
*     FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE FOR OBTAINING
*     ANY THIRD PARTY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.
*     XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO
*     THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY
*     WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM
*     CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND
*     FITNESS FOR A PARTICULAR PURPOSE.
*
*
*     Xilinx hardware products are not intended for use in life support
*     appliances, devices, or systems. Use in such applications is
*     expressly prohibited.
*
*
*     (c) Copyright 2002 Xilinx Inc.
*     All rights reserved.
*
*
*     You should have received a copy of the GNU General Public License along
*     with this program; if not, write to the Free Software Foundation, Inc.,
*     675 Mass Ave, Cambridge, MA 02139, USA.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xps2_l.h
*
* This header file contains identifiers and low-level driver functions (or
* macros) that can be used to access the device.  The user should refer to the
* hardware device specification for more details of the device operation.
* High-level driver functions are defined in xps2.h.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a ch   06/18/02 First release
* </pre>
*
******************************************************************************/

#ifndef XPS2_L_H		/* prevent circular inclusions */
#define XPS2_L_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files ********************************/

#include "xbasic_types.h"
#include "xio.h"

/************************** Constant Definitions ****************************/

/* PS/2 register offsets */
#define XPS2_RESET_OFFSET            0	/* reset register, write only */
#define XPS2_STATUS_OFFSET           4	/* status register, read only */
#define XPS2_RX_REG_OFFSET           8	/* receive register, read only */
#define XPS2_TX_REG_OFFSET           12	/* transmit register, write only */
#define XPS2_INTSTA_REG_OFFSET       16	/* int status register, read only */
#define XPS2_INTCLR_REG_OFFSET       20	/* int clear register, write only */
#define XPS2_INTMSET_REG_OFFSET      24	/* mask set register, read/write */
#define XPS2_INTMCLR_REG_OFFSET      28	/* mask clear register, write only */

/* reset register bit positions */
#define XPS2_CLEAR_RESET             0x00
#define XPS2_RESET                   0x01

/* status register bit positions */
#define XPS2_ST_RX_FULL              0x01
#define XPS2_ST_TX_FULL              0x02

/* interrupt register bit positions */
/* used for the INTSTA, INTCLR, INTMSET, INTMCLR register */
#define XPS2_INT_WDT_TOUT            0x01
#define XPS2_INT_TX_NOACK            0x02
#define XPS2_INT_TX_ACK              0x04
#define XPS2_INT_TX_ALL              0x06
#define XPS2_INT_RX_OVF              0x08
#define XPS2_INT_RX_ERR              0x10
#define XPS2_INT_RX_FULL             0x20
#define XPS2_INT_RX_ALL              0x38
#define XPS2_INT_ALL                 0x3f

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/

/*****************************************************************************
*
* Low-level driver macros.  The list below provides signatures to help the
* user use the macros.
*
* void XPs2_mReset(u32 BaseAddress)
* u8 XPs2_mGetStatus(u32 BaseAddress)
*
* u8 XPs2_mGetIntrStatus(u32 BaseAddress)
* void XPs2_mClearIntr(u32 BaseAddress, u8 ClearMask)
* u32 XPs2_mIsIntrEnabled(u32 BaseAddress, u8 EnabledMask)
* void XPs2_mEnableIntr(u32 BaseAddress, u8 EnableMask)
* void XPs2_mDisableIntr(u32 BaseAddress, u8 DisableMask)
*
* u32 XPs2_mIsReceiveEmpty(u32 BaseAddress)
* u32 XPs2_mIsTransmitFull(u32 BaseAddress)
*
*****************************************************************************/

/****************************************************************************/
/**
* Reset the PS/2 port.
*
* @param    BaseAddress contains the base address of the device.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mReset(BaseAddress) \
            XIo_Out8(((BaseAddress) + XPS2_RESET_OFFSET), XPS2_RESET); \
            XIo_Out8(((BaseAddress) + XPS2_RESET_OFFSET), XPS2_CLEAR_RESET)

/****************************************************************************/
/**
* Read the PS/2 status register.
*
* @param    BaseAddress contains the base address of the device.
*
* @return   The value read from the register.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mGetStatus(BaseAddress) \
            (XIo_In8((BaseAddress) + XPS2_STATUS_OFFSET))

/****************************************************************************/
/**
* Read the interrupt status register.
*
* @param    BaseAddress contains the base address of the device.
*
* @return   The value read from the register.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mGetIntrStatus(BaseAddress) \
            (XIo_In8((BaseAddress) + XPS2_INTSTA_REG_OFFSET))

/****************************************************************************/
/**
* Clear pending interrupts.
*
* @param    BaseAddress contains the base address of the device.
*           Bitmask for interrupts to be cleared. A "1" clears the interrupt.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mClearIntr(BaseAddress, ClearMask) \
            XIo_Out8((BaseAddress) + XPS2_INTCLR_REG_OFFSET, (ClearMask))

/****************************************************************************/
/**
* Check for enabled interrupts.
*
* @param    BaseAddress contains the base address of the device.
*           Bitmask for interrupts to be checked.
*
* @return   TRUE if the interrupt is enabled, FALSE otherwise.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mIsIntrEnabled(BaseAddress, EnabledMask) \
            (XIo_In8((BaseAddress) + XPS2_INTMSET_REG_OFFSET) & (EnabledMask))

/****************************************************************************/
/**
* Enable Interrupts.
*
* @param    BaseAddress contains the base address of the device.
*           Bitmask for interrupts to be enabled.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mEnableIntr(BaseAddress, EnableMask) \
            XIo_Out8((BaseAddress) + XPS2_INTMSET_REG_OFFSET, (EnableMask))

/****************************************************************************/
/**
* Disable Interrupts.
*
* @param    BaseAddress contains the base address of the device.
*           Bitmask for interrupts to be disabled.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mDisableIntr(BaseAddress, DisableMask) \
            XIo_Out8((BaseAddress) + XPS2_INTMCLR_REG_OFFSET, (DisableMask))

/****************************************************************************/
/**
* Determine if there is receive data in the receiver.
*
* @param    BaseAddress contains the base address of the device.
*
* @return   TRUE if there is receive data, FALSE otherwise.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mIsReceiveEmpty(BaseAddress) \
            (!(XPs2_mGetStatus(BaseAddress) & XPS2_ST_RX_FULL))

/****************************************************************************/
/**
* Determine if a byte of data can be sent with the transmitter.
*
* @param    BaseAddress contains the base address of the device.
*
* @return   TRUE if a byte can be sent, FALSE otherwise.
*
* @note     None.
*
******************************************************************************/
#define XPs2_mIsTransmitFull(BaseAddress) \
            (XPs2_mGetStatus(BaseAddress) & XPS2_ST_TX_FULL)

/************************** Variable Definitions ****************************/

/************************** Function Prototypes *****************************/

	void XPs2_SendByte(u32 BaseAddress, u8 Data);
	u8 XPs2_RecvByte(u32 BaseAddress);

/****************************************************************************/

#ifdef __cplusplus
}
#endif
#endif

/* $Id: xgpio_l.h,v 1.1.2.1 2007/02/16 10:03:29 imanuilov Exp $ */
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
*       (c) Copyright 2002 - 2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xgpio_l.h
*
* This header file contains identifiers and low-level driver functions (or
* macros) that can be used to access the device.  The user should refer to the
* hardware device specification for more details of the device operation.
* High-level driver functions are defined in xgpio.h.
*
* The macros that are available in this file use a multiply to calculate the
* addresses of registers. The user can control whether that multiply is done
* at run time or at compile time. A constant passed as the channel parameter
* will cause the multiply to be done at compile time. A variable passed as the
* channel parameter will cause it to occur at run time.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a jhl  04/24/02 First release of low level driver
* 2.00a jhl  11/26/03 Added support for dual channels and interrupts. This
*                     change required the functions to be changed such that
*                     the interface is not compatible with previous versions.
*                     See the examples in the example directory for macros
*                     to help compile an application that was designed for
*                     previous versions of the driver. The interrupt registers
*                     are accessible using the ReadReg and WriteReg macros and
*                     a channel parameter was added to the other macros.
* </pre>
*
******************************************************************************/

#ifndef XGPIO_L_H		/* prevent circular inclusions */
#define XGPIO_L_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xio.h"

/************************** Constant Definitions *****************************/

/** @name Registers
 *
 * Register offsets for this device. This device utilizes IPIF interrupt
 * registers.
 * @{
 */
#define XGPIO_DATA_OFFSET  0x0	/**< Data register for 1st channel */
#define XGPIO_TRI_OFFSET   0x4	/**< I/O direction register for 1st channel */
#define XGPIO_DATA2_OFFSET 0x8	/**< Data register for 2nd channel */
#define XGPIO_TRI2_OFFSET  0xC	/**< I/O direction register for 2nd channel */

#define XGPIO_GIER_OFFSET  0x11C /**< Glogal interrupt enable register */
#define XGPIO_ISR_OFFSET   0x120 /**< Interrupt status register */
#define XGPIO_IER_OFFSET   0x128 /**< Interrupt enable register */

/* @} */

/* The following constant describes the offset of each channels data and
 * tristate register from the base address.
 */
#define XGPIO_CHAN_OFFSET  8

/** @name Interrupt Status and Enable Register bitmaps and masks
 *
 * Bit definitions for the interrupt status register and interrupt enable
 * registers.
 * @{
 */
#define XGPIO_IR_MASK        0x3 /**< Mask of all bits */
#define XGPIO_IR_CH1_MASK    0x1 /**< Mask for the 1st channel */
#define XGPIO_IR_CH2_MASK    0x2 /**< Mask for the 2nd channel */
/*@}*/

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/****************************************************************************/
/**
*
* Write a value to a GPIO register. A 32 bit write is performed. If the
* GPIO component is implemented in a smaller width, only the least
* significant data is written.
*
* @param   BaseAddress is the base address of the GPIO device.
* @param   RegOffset is the register offset from the base to write to.
* @param   Data is the data written to the register.
*
* @return  None.
*
* @note    None.
*
* C-style signature:
*    void XGpio_mWriteReg(u32 BaseAddress, unsigned RegOffset,
*                         u32 Data)
*
****************************************************************************/
#define XGpio_mWriteReg(BaseAddress, RegOffset, Data) \
    XIo_Out32((BaseAddress) + (RegOffset), (u32)(Data))

/****************************************************************************/
/**
*
* Read a value from a GPIO register. A 32 bit read is performed. If the
* GPIO component is implemented in a smaller width, only the least
* significant data is read from the register. The most significant data
* will be read as 0.
*
* @param   BaseAddress is the base address of the GPIO device.
* @param   Register is the register offset from the base to read from.
* @param   Data is the data from the register.
*
* @return  None.
*
* @note    None.
*
* C-style signature:
*    u32 XGpio_mReadReg(u32 BaseAddress, unsigned RegOffset)
*
****************************************************************************/
#define XGpio_mReadReg(BaseAddress, RegOffset) \
    XIo_In32((BaseAddress) + (RegOffset))

/*****************************************************************************
*
* Set the input/output direction of the signals of the specified GPIO channel.
*
* @param    BaseAddress contains the base address of the GPIO device.
* @param    Channel contains the channel (1 or 2) to operate on.
* @param    DirectionMask is a bitmask specifying which discretes are input and
*           which are output. Bits set to 0 are output and bits set to 1 are
*           input.
*
* @return   None.
*
* @note     None.
*
* C-style signature:
*    void XGpio_mSetDataDirection(u32 BaseAddress, unsigned Channel,
*                                 u32 DirectionMask)
*
******************************************************************************/
#define XGpio_mSetDataDirection(BaseAddress, Channel, DirectionMask) \
    XGpio_mWriteReg((BaseAddress), \
                    (((Channel) - 1) * XGPIO_CHAN_OFFSET) + XGPIO_TRI_OFFSET, \
                    (DirectionMask))

/****************************************************************************/
/**
* Get the data register of the specified GPIO channel.
*
* @param    BaseAddress contains the base address of the GPIO device.
* @param    Channel contains the channel (1 or 2) to operate on.
*
* @return   The contents of the data register.
*
* @note     None.
*
* C-style signature:
*    u32 XGpio_mGetDataReg(u32 BaseAddress, unsigned Channel)
*
*****************************************************************************/
#define XGpio_mGetDataReg(BaseAddress, Channel) \
    XGpio_mReadReg((BaseAddress), \
                   (((Channel) - 1) * XGPIO_CHAN_OFFSET) + XGPIO_DATA_OFFSET)

/****************************************************************************/
/**
* Set the data register of the specified GPIO channel.
*
* @param    BaseAddress contains the base address of the GPIO device.
* @param    Channel contains the channel (1 or 2) to operate on.
* @param    Data is the value to be written to the data register.
*
* @return   None.
*
* @note     None.
*
* C-style signature:
*    void XGpio_mSetDataReg(u32 BaseAddress, unsigned Channel,
*                           u32 Data)
*
*****************************************************************************/
#define XGpio_mSetDataReg(BaseAddress, Channel, Data) \
    XGpio_mWriteReg((BaseAddress), \
                    (((Channel) - 1) * XGPIO_CHAN_OFFSET) + XGPIO_DATA_OFFSET,\
                    (Data))

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

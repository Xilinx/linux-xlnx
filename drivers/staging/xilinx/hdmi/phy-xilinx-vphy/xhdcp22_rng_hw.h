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
* @file xhdcp22_rng_hw.h
* @addtogroup hdcp22_rng_v1_1
* @{
* @details
*
* This header file contains identifiers and register-level core functions (or
* macros) that can be used to access the Xilinx HDCP 2.2 RNG core.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  JO     10/01/15 Initial release.
* </pre>
*
******************************************************************************/

#ifndef XHDCP22_RNG_HW_H
/**< Prevent circular inclusions by using protection macros */
#define XHDCP22_RNG_HW_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include "xil_io.h"

/************************** Constant Definitions *****************************/
/* HDCP22 RNG Version Interface base */
#define XHDCP22_RNG_VER_BASE                (0*64)
/** VER Identification register * register offset */
#define XHDCP22_RNG_VER_ID_OFFSET           ((XHDCP22_RNG_VER_BASE)+(0*4))
/** VER Version register * offset */
#define XHDCP22_RNG_VER_VERSION_OFFSET      ((XHDCP22_RNG_VER_BASE)+(1*4))

/* HDCP22 RNG Register peripheral interface base */
#define XHDCP22_RNG_REG_BASE                (1*64)
/** Control register * register offset */
#define XHDCP22_RNG_REG_CTRL_OFFSET         ((XHDCP22_RNG_REG_BASE)+(0*4))
/** Control set register * offset */
#define XHDCP22_RNG_REG_CTRL_SET_OFFSET     ((XHDCP22_RNG_REG_BASE)+(1*4))
/** Control clear register * offset */
#define XHDCP22_RNG_REG_CTRL_CLR_OFFSET     ((XHDCP22_RNG_REG_BASE)+(2*4))
/** Status register * offset */
#define XHDCP22_RNG_REG_STA_OFFSET          ((XHDCP22_RNG_REG_BASE)+(3*4))
/** Random number register 1 * offset */
#define XHDCP22_RNG_REG_RN_1_OFFSET         ((XHDCP22_RNG_REG_BASE)+(4*4))
/** Random number 2 * offset */
#define XHDCP22_RNG_REG_RN_2_OFFSET         ((XHDCP22_RNG_REG_BASE)+(5*4))
/** Random number 3 * offset */
#define XHDCP22_RNG_REG_RN_3_OFFSET         ((XHDCP22_RNG_REG_BASE)+(6*4))
/** Random number 4 * offset */
#define XHDCP22_RNG_REG_RN_4_OFFSET         ((XHDCP22_RNG_REG_BASE)+(7*4))

/* HDCP22 RNG Control register masks */
/** Control register Run mask */
#define XHDCP22_RNG_REG_CTRL_RUN_MASK       (1<<0)
/** Control register Interrupt Enable mask. Reserved for future use. */
#define XHDCP22_RNG_REG_CTRL_IE_MASK        (1<<1)

/* HDCP22 RNG Status register masks */
/** Status register interrupt mask. Reserved for future use.*/
#define XHDCP22_RN_REG_STA_IRQ_MASK         (1<<0)
/** Status register event mask. Reserved for future use.*/
#define XHDCP22_RN_REG_STA_EVT_MASK         (1<<1)

#define XHDCP22_RNG_SHIFT_16                16      /**< 16 shift value */
#define XHDCP22_RNG_MASK_16                 0xFFFF  /**< 16 bit mask value */
#define XHDCP22_RNG_VER_ID                  0x2200  /**< Version ID */


/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/** @name Register access macro definition
* @{
*/
#define XHdcp22Rng_In32        Xil_In32        /**< Input Operations */
#define XHdcp22Rng_Out32       Xil_Out32       /**< Output Operations */

/*****************************************************************************/
/**
*
* This macro reads a value from a HDCP22 RNG register.
* A 32 bit read is performed.
* If the component is implemented in a smaller width, only the least
* significant data is read from the register. The most significant data
* will be read as 0.
*
* @param  BaseAddress is the base address of the HDCP22 RNG core instance.
* @param  RegOffset is the register offset of the register (defined at
*         the top of this file).
*
* @return The 32-bit value of the register.
*
* @note   C-style signature:
*         u32 XHdcp22Rng_ReadReg(u32 BaseAddress, u32 RegOffset)
*
******************************************************************************/
#define XHdcp22Rng_ReadReg(BaseAddress, RegOffset) \
        XHdcp22Rng_In32((BaseAddress) + ((u32)RegOffset))

/*****************************************************************************/
/**
*
* This macro writes a value to a HDCP22 RNG register.
* A 32 bit write is performed. If the component is implemented in a smaller
* width, only the least significant data is written.
*
* @param  BaseAddress is the base address of the HDCP22 RNG core instance.
* @param  RegOffset is the register offset of the register (defined at
*         the top of this file) to be written.
* @param  Data is the 32-bit value to write into the register.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Rng_WriteReg(u32 BaseAddress, u32 RegOffset, u32 Data)
*
******************************************************************************/
#define XHdcp22Rng_WriteReg(BaseAddress, RegOffset, Data) \
        XHdcp22Rng_Out32((BaseAddress) + ((u32)RegOffset), (u32)(Data))

/*****************************************************************************/
/**
*
* This macro reads the status register from the HDCP22 RNG.
*
* @param  BaseAddress is the base address of the HDCP22 RNG core instance.
*
* @return A 32-bit value representing the contents of the status register.
*
* @note   C-style signature:
*         u32 XHdcp22Rng_GetStatusReg(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Rng_GetStatusReg(BaseAddress) \
        XHdcp22Rng_ReadReg(BaseAddress, XHDCP22_RNG_REG_STA_OFFSET)

/*****************************************************************************/
/**
*
* This macro reads the control register from the HDCP22 RNG.
*
* @param  BaseAddress is the base address of the HDCP22 RNG core instance.
*
* @return A 32-bit value representing the contents of the control register.
*
* @note   C-style signature:
*         u32 XHdcp22Rng_GetStatusReg(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Rng_GetControlReg(BaseAddress) \
        XHdcp22Rng_ReadReg(BaseAddress, XHDCP22_RNG_REG_CTRL_OFFSET)
/*@}*/

/************************** Function Prototypes ******************************/

/************************** Variable Declarations ****************************/

#ifdef __cplusplus
}
#endif

#endif /* XHDCP2_RNG_HW_H */

/** @} */

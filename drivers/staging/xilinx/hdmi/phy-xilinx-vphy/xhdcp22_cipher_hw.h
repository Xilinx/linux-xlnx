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
* @file xhdcp22_cipher_hw.h
* @addtogroup hdcp22_cipher_v1_1
* @{
* @details
*
* This header file contains identifiers and register-level core functions (or
* macros) that can be used to access the Xilinx HDCP 2.2 Cipher core.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  JO     10/01/15 Initial release.
* 1.01  MG     10/28/15 Added InputCtr register
* </pre>
*
******************************************************************************/

#ifndef XHDCP22_CIPHER_HW_H
/**< Prevent circular inclusions by using protection macros */
#define XHDCP22_CIPHER_HW_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include "xil_types.h"
#include "xil_io.h"

/************************** Constant Definitions *****************************/
/* HDCP22 Cipher Version Interface base */
#define XHDCP22_CIPHER_VER_BASE             (0*64)
/** VER Identification register * register offset */
#define XHDCP22_CIPHER_VER_ID_OFFSET        ((XHDCP22_CIPHER_VER_BASE)+(0*4))
/** VER Version register * offset */
#define XHDCP22_CIPHER_VER_VERSION_OFFSET   ((XHDCP22_CIPHER_VER_BASE)+(1*4))

/* HDCP22 Cipher Register peripheral interface base */
#define XHDCP22_CIPHER_REG_BASE             (1*64)
/** Control register * register offset */
#define XHDCP22_CIPHER_REG_CTRL_OFFSET      ((XHDCP22_CIPHER_REG_BASE)+(0*4))
/** Control set register * offset */
#define XHDCP22_CIPHER_REG_CTRL_SET_OFFSET  ((XHDCP22_CIPHER_REG_BASE)+(1*4))
/** Control clear register * offset */
#define XHDCP22_CIPHER_REG_CTRL_CLR_OFFSET  ((XHDCP22_CIPHER_REG_BASE)+(2*4))
/** Status register * offset */
#define XHDCP22_CIPHER_REG_STA_OFFSET       ((XHDCP22_CIPHER_REG_BASE)+(3*4))
/** Ks register 1 * offset */
#define XHDCP22_CIPHER_REG_KS_1_OFFSET      ((XHDCP22_CIPHER_REG_BASE)+(4*4))
/** Ks register 2 * offset */
#define XHDCP22_CIPHER_REG_KS_2_OFFSET      ((XHDCP22_CIPHER_REG_BASE)+(5*4))
/** Ks register 3 * offset */
#define XHDCP22_CIPHER_REG_KS_3_OFFSET      ((XHDCP22_CIPHER_REG_BASE)+(6*4))
/** Ks register 4 * offset */
#define XHDCP22_CIPHER_REG_KS_4_OFFSET      ((XHDCP22_CIPHER_REG_BASE)+(7*4))
/** Lc128 register 1 * offset */
#define XHDCP22_CIPHER_REG_LC128_1_OFFSET   ((XHDCP22_CIPHER_REG_BASE)+(8*4))
/** Lc128 register 2 * offset */
#define XHDCP22_CIPHER_REG_LC128_2_OFFSET   ((XHDCP22_CIPHER_REG_BASE)+(9*4))
/** Lc128 register 3 * offset */
#define XHDCP22_CIPHER_REG_LC128_3_OFFSET   ((XHDCP22_CIPHER_REG_BASE)+(10*4))
/** Lc128 register 4 * offset */
#define XHDCP22_CIPHER_REG_LC128_4_OFFSET   ((XHDCP22_CIPHER_REG_BASE)+(11*4))
/** Riv register 1 * offset */
#define XHDCP22_CIPHER_REG_RIV_1_OFFSET     ((XHDCP22_CIPHER_REG_BASE)+(12*4))
/** Riv register 2 * offset */
#define XHDCP22_CIPHER_REG_RIV_2_OFFSET     ((XHDCP22_CIPHER_REG_BASE)+(13*4))
/** InputCtr register 1 * offset */
#define XHDCP22_CIPHER_REG_INPUTCTR_1_OFFSET    ((XHDCP22_CIPHER_REG_BASE)+(14*4))
/** InputCtr register 2 * offset */
#define XHDCP22_CIPHER_REG_INPUTCTR_2_OFFSET    ((XHDCP22_CIPHER_REG_BASE)+(15*4))

/* HDCP22 Cipher Control register masks */
/** Control register Run mask */
#define XHDCP22_CIPHER_REG_CTRL_RUN_MASK        (1<<0)
/** Control register Interrupt Enable mask. Reserved for future use. */
#define XHDCP22_CIPHER_REG_CTRL_IE_MASK         (1<<1)
/** Control register Mode mask */
#define XHDCP22_CIPHER_REG_CTRL_MODE_MASK       (1<<2)
/** Control register Encrypt mask */
#define XHDCP22_CIPHER_REG_CTRL_ENCRYPT_MASK    (1<<3)
/** Control register blank mask */
#define XHDCP22_CIPHER_REG_CTRL_BLANK_MASK    	(1<<4)
/** Control register noise mask */
#define XHDCP22_CIPHER_REG_CTRL_NOISE_MASK    	(1<<5)

/* HDCP22 Cipher Status register masks */
/** Status register interrupt mask. Reserved for future use.*/
#define XHDCP22_CIPHER_REG_STA_IRQ_MASK         (1<<0)
/** Status register event mask. Reserved for future use.*/
#define XHDCP22_CIPHER_REG_STA_EVT_MASK         (1<<1)
/** Status register encrypted mask. */
#define XHDCP22_CIPHER_REG_STA_ENCRYPTED_MASK   (1<<2)

/* Peripheral ID and General shift values. */
#define XHDCP22_CIPHER_SHIFT_16               16      /**< 16 shift value */
#define XHDCP22_CIPHER_MASK_16                0xFFFF  /**< 16 bit mask value */
#define XHDCP22_CIPHER_VER_ID                 0x2200  /**< Version ID */


/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/** @name Register access macro definition
* @{
*/
#define XHdcp22Cipher_In32        Xil_In32        /**< Input Operations */
#define XHdcp22Cipher_Out32       Xil_Out32       /**< Output Operations */

/*****************************************************************************/
/**
*
* This macro reads a value from a HDCP22 Cipher register.
* A 32 bit read is performed.
* If the component is implemented in a smaller width, only the least
* significant data is read from the register. The most significant data
* will be read as 0.
*
* @param  BaseAddress is the base address of the HDCP22 Cipher core instance.
* @param  RegOffset is the register offset of the register (defined at
*         the top of this file).
*
* @return The 32-bit value of the register.
*
* @note   C-style signature:
*         u32 XHdcp22Cipher_ReadReg(u32 BaseAddress, u32 RegOffset)
*
******************************************************************************/
#define XHdcp22Cipher_ReadReg(BaseAddress, RegOffset) \
        XHdcp22Cipher_In32((BaseAddress) + ((u32)RegOffset))

/*****************************************************************************/
/**
*
* This macro writes a value to a HDCP22 Cipher register.
* A 32 bit write is performed. If the component is implemented in a smaller
* width, only the least significant data is written.
*
* @param  BaseAddress is the base address of the HDCP22 Cipher core instance.
* @param  RegOffset is the register offset of the register (defined at
*         the top of this file) to be written.
* @param  Data is the 32-bit value to write into the register.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Cipher_WriteReg(u32 BaseAddress, u32 RegOffset, u32 Data)
*
******************************************************************************/
#define XHdcp22Cipher_WriteReg(BaseAddress, RegOffset, Data) \
        XHdcp22Cipher_Out32((BaseAddress) + ((u32)RegOffset), (u32)(Data))
/*****************************************************************************/
/**
*
* This macro reads the status register from the HDCP22 Cipher.
*
* @param  IBaseAddress is the base address of the HDCP22 Cipher core instance.
*
* @return A 32-bit value representing the contents of the status register.
*
* @note   C-style signature:
*         u32 XHdcp22Cipher_GetStatusReg(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_GetStatusReg(BaseAddress) \
        XHdcp22Cipher_ReadReg(BaseAddress, XHDCP22_CIPHER_REG_STA_OFFSET)

/*****************************************************************************/
/**
*
* This macro reads the control register from the HDCP22 Cipher.
*
* @param  BaseAddress is the base address of the HDCP22 Cipher core instance.
*
* @return A 32-bit value representing the contents of the control register.
*
* @note   C-style signature:
*         u32 XHdcp22Cipher_GetStatusReg(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_GetControlReg(BaseAddress) \
        XHdcp22Cipher_ReadReg(BaseAddress, XHDCP22_CIPHER_REG_CTRL_OFFSET)
/*@}*/

/************************** Function Prototypes ******************************/

/************************** Variable Declarations ****************************/

#ifdef __cplusplus
}
#endif

#endif /* XHDCP2_CIPHER_HW_H */

/** @} */

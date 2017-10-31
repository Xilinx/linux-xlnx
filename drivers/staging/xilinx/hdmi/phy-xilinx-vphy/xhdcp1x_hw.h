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
* @file xhdcp1x_hw.h
* @addtogroup hdcp1x_v4_0
* @{
*
* This header file contains identifiers and register-level core functions (or
* macros) that can be used to access the Xilinx HDCP cipher core.
*
* For more information about the operation of this core see the hardware
* specification and documentation in the higher level driver xhdcp1x_ciper.h
* file.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 3.10  yas    06/14/16 Added cipher registers XHDCP1X_CIPHER_REG_BLANK_VALUE
*                       and XHDCP1X_CIPHER_REG_BLANK_SEL
* </pre>
*
******************************************************************************/

#ifndef XHDCP1X_HW_H
/**< Prevent circular inclusions by using protection macros */
#define XHDCP1X_HW_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include "xil_io.h"

/************************** Constant Definitions *****************************/

/* HDCP Cipher register offsets */
#define XHDCP1X_CIPHER_REG_VERSION	(0x0000u)  /**< Version register
						offset */
#define XHDCP1X_CIPHER_REG_TYPE		(0x0004u)  /**< Type register offset */
#define XHDCP1X_CIPHER_REG_SCRATCH	(0x0008u)  /**< Scratch pad register
						offset */
#define XHDCP1X_CIPHER_REG_CONTROL	(0x000Cu)  /**< Control register
						offset */
#define XHDCP1X_CIPHER_REG_STATUS	(0x0010u)  /**< Status register
						offset */
#define XHDCP1X_CIPHER_REG_INTERRUPT_MASK (0x0014u)  /**< Interrupt Mask
						register offset */
#define XHDCP1X_CIPHER_REG_INTERRUPT_STATUS \
					(0x0018u)  /**< Interrupt Status
						register offset */
#define XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H \
					(0x0020u)  /**< Encryption Enable (High)
						register offset */
#define XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L \
					(0x0024u)  /**< Encryption Enable (Low)
						register offset */

#define XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL \
					(0x002Cu)  /**< Key Management Control
						register offset */
#define XHDCP1X_CIPHER_REG_KEYMGMT_STATUS (0x0030u)  /**< Key Management Status
						register offset */
#define XHDCP1X_CIPHER_REG_KSV_LOCAL_H	(0x0038u)  /**< Local KSV (High)
						register offset */
#define XHDCP1X_CIPHER_REG_KSV_LOCAL_L	(0x003Cu)  /**< Local KSV (Low) register
						offset */
#define XHDCP1X_CIPHER_REG_KSV_REMOTE_H	(0x0040u)  /**< Remote KSV (High)
						offset */
#define XHDCP1X_CIPHER_REG_KSV_REMOTE_L	(0x0044u)  /**< Remote KSV (Low)
						register offset */
#define XHDCP1X_CIPHER_REG_Km_H		(0x0048u)  /**< Km (High) register
						offset */
#define XHDCP1X_CIPHER_REG_Km_L		(0x004Cu)  /**< Km (Low) register
						offset */

#define XHDCP1X_CIPHER_REG_CIPHER_CONTROL \
					(0x0050u)  /**< Cipher Control register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_STATUS (0x0054u)  /**< Cipher Status register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Bx	(0x0058u)  /**< Cipher Bx register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_By	(0x005Cu)  /**< Cipher By register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Bz	(0x0060u)  /**< Cipher Bz register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Kx	(0x0064u)  /**< Cipher Kx register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Ky	(0x0068u)  /**< Cipher Ky register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Kz	(0x006Cu)  /**< Cipher Kz register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Mi_H	(0x0070u)  /**< Cipher Mi (High)
						register offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Mi_L	(0x0074u)  /**< Cipher Mi (Low) register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Ri	(0x0078u)  /**< Cipher Ri register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Ro	(0x007Cu)  /**< Cipher Ro register
						offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Mo_H	(0x0080u)  /**< Cipher Mo (High)
						register offset */
#define XHDCP1X_CIPHER_REG_CIPHER_Mo_L	(0x0084u)  /**< Cipher Mo (Low) register
						offset */
#define XHDCP1X_CIPHER_REG_BLANK_VALUE 	(0x00BCu)  /**< Cipher blank value
						register */
#define XHDCP1X_CIPHER_REG_BLANK_SEL (0x00C0u)  /**< Cipher blank select
						register */

/* HDCP Cipher register bit mask definitions */
#define XHDCP1X_CIPHER_BITMASK_TYPE_PROTOCOL \
					(0x03u <<  0)	/**< Protocol bitmask in
						Type register */
#define XHDCP1X_CIPHER_BITMASK_TYPE_DIRECTION \
					(0x01u <<  2)  /**< Direction bitmask in
						Type register */

#define XHDCP1X_CIPHER_BITMASK_CONTROL_ENABLE \
					(0x01u <<  0)  /**< Enable bitmask in
						Control register */
#define XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE \
					(0x01u <<  1)  /**< Update bitmask in
						Control register */
#define XHDCP1X_CIPHER_BITMASK_CONTROL_NUM_LANES \
					(0x07u <<  4)  /**< Num Lanes bitmask in
						Control register */
#define XHDCP1X_CIPHER_BITMASK_CONTROL_RESET \
					(0x01u << 31)  /**< Reset bitmask in
						Control register */

#define XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL \
					(0x01u <<  0)  /**< Link Failure bitmask
						in Interrupt register(s) */
#define XHDCP1X_CIPHER_BITMASK_INTERRUPT_Ri_UPDATE \
					(0x01u <<  1)  /**< Ri bitmask in
						Interrupt register(s) */

#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_LOCAL_KSV \
					(0x01u <<  0)  /**< Read Local KSV
						bitmask in Key Management
						Control register */
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_BEGIN_Km \
					(0x01u <<  1)  /**< Being Km bitmask in
						Key Management Control
						register */
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_ABORT_Km \
					(0x01u <<  2)  /**< Abort Km bitmask in
						Key Management Control
						register */
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_SET_SELECT \
					(0x07u << 16)  /**< Key Set Select
						bitmask in Key Management
						Control register */

#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_KSV_READY \
					(0x01u <<  0)  /**< Local KSV ready
						bitmask in Key Management Status
						register */
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_Km_READY \
					(0x01u <<  1)  /**< Km Value ready
						bitmask in Key Management Status
						register */

#define XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE \
					(0x01u <<  0)  /**< XOR Enable bitmask
						in Cipher Control register */
#define XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_REQUEST \
					(0x07u <<  8)  /**< Request bitmask in
						Cipher Control register */

#define XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_XOR_IN_PROG \
					(0x01u <<  0)  /**< XOR In Progress
						bitmask in Cipher Status
						register */
#define XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_REQUEST_IN_PROG \
					(0x07u <<  8)  /**< Request In Progress
						bitmask in Cipher Status
						register */

#define XHDCP1X_CIPHER_BITMASK_BLANK_VALUE \
					(0x000000FF) /**< Cipher blank value
					bitmask, lower 24 bits */
#define XHDCP1X_CIPHER_BITMASK_BLANK_SEL \
					(0x1u)  /**< Cipher blank
						select bitmask */

/* HDCP Cipher register bit value definitions */
#define XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_DP \
					(0x00u <<  0)  /**< DP Protocol value in
						Type register */
#define XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_HDMI \
					(0x01u <<  0)  /**< HDMI Protocol value
						in Type register */

#define XHDCP1X_CIPHER_VALUE_TYPE_DIRECTION_RX \
					(0x00u <<  2)  /**< RX Direction value
						in Type register */
#define XHDCP1X_CIPHER_VALUE_TYPE_DIRECTION_TX \
					(0x01u <<  2)  /**< TX Direction value
						in Type register */

#define XHDCP1X_CIPHER_VALUE_CIPHER_CONTROL_REQUEST_BLOCK \
					(0x01u <<  8)  /**< Block Request value
						in Cipher Control register */
#define XHDCP1X_CIPHER_VALUE_CIPHER_CONTROL_REQUEST_REKEY \
					(0x01u <<  9)  /**< ReKey Request value
						in Cipher Control register */
#define XHDCP1X_CIPHER_VALUE_CIPHER_CONTROL_REQUEST_RNG \
					(0x01u << 10)  /**< RNG Request value in
						Cipher Control register */

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/* Register access macro definition */
#define XHdcp1x_In32		Xil_In32	/**< Input Operations */
#define XHdcp1x_Out32		Xil_Out32	/**< Output Operations */

/*****************************************************************************/
/**
*
* This macro reads a value from a HDCP cipher register. A 32 bit read is
* always performed.
*
* @param  BaseAddress is the base address of the HDCP cipher core instance.
* @param  RegOffset is the register offset of the register
*
* @return
*  The 32-bit value of the register.
*
* @note
* C-style: u32 XHdcp1x_ReadReg(UINTPTR BaseAddress, u32 RegOffset)
*
******************************************************************************/
#define XHdcp1x_ReadReg(BaseAddress, RegOffset) \
	XHdcp1x_In32((BaseAddress) + ((u32)RegOffset))

/*****************************************************************************/
/**
*
* This macro writes a value to a HDCP cipher register. A 32 bit write is
* always performed.
*
* @param  BaseAddress is the base address of the HDCP cipher core instance.
* @param  RegOffset is the register offset of the register
* @param  Data is the 32-bit value to write into the register.
*
* @return
*  None.
*
* @note
* C-style: void XHdcp1x_WriteReg(UINTPTR BaseAddress, u32 RegOffset, u32 Data)
*
******************************************************************************/
#define XHdcp1x_WriteReg(BaseAddress, RegOffset, Data) \
	XHdcp1x_Out32((BaseAddress) + ((u32)RegOffset), (u32)(Data))

/*****************************************************************************/
/**
* This queries a cipher to determine if it is enabled.
*
* @param	InstancePtr is the instance to query.
*
* @return	Truth value indicating transmitter (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
#define XHdcp1x_CipherIsEnabled(InstancePtr) \
	((XHdcp1x_ReadReg((InstancePtr)->Config.BaseAddress, \
	XHDCP1X_CIPHER_REG_CONTROL) & \
	XHDCP1X_CIPHER_BITMASK_CONTROL_ENABLE) != 0)

/*****************************************************************************/
/**
* This queries a cipher to determine if the XOR (encryption) function is
* currently in progress.
*
* @param	InstancePtr is the instance to query.
*
* @return	Truth value indicating in progress (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
#define XHdcp1x_CipherXorInProgress(InstancePtr) \
	((XHdcp1x_ReadReg((InstancePtr)->Config.BaseAddress, \
	XHDCP1X_CIPHER_REG_CIPHER_STATUS) & \
	XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_XOR_IN_PROG) != 0)

/*****************************************************************************/
/**
* This queries a cipher to determine if the local KSV is ready to read.
*
* @param	InstancePtr is the instance to query.
*
* @return	Truth value indicating ready (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
#define XHdcp1x_CipherLocalKsvReady(InstancePtr) \
	((XHdcp1x_ReadReg((InstancePtr)->Config.BaseAddress, \
	XHDCP1X_CIPHER_REG_KEYMGMT_STATUS) & \
	XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_KSV_READY) != 0)

/*****************************************************************************/
/**
* This queries a cipher to determine if the Km value is ready.
*
* @param	InstancePtr is the instance to query.
*
* @return	Truth value indicating ready (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
#define XHdcp1x_CipherKmReady(InstancePtr) \
	((XHdcp1x_ReadReg((InstancePtr)->Config.BaseAddress, \
	XHDCP1X_CIPHER_REG_KEYMGMT_STATUS) & \
	XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_Km_READY) != 0)

/*****************************************************************************/
/**
*
* This macro checks if a core supports the Display Port protocol
*
* @param	InstancePtr is a pointer to the XHdcp1x core instance.
*
* @return	Truth value indicating DP (TRUE) or not (FALSE)
*
******************************************************************************/
#define XHdcp1x_IsDP(InstancePtr) \
	((XHdcp1x_ReadReg((InstancePtr)->Config.BaseAddress, \
	XHDCP1X_CIPHER_REG_TYPE) & XHDCP1X_CIPHER_BITMASK_TYPE_PROTOCOL) \
	== XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_DP)

/*****************************************************************************/
/**
*
* This macro checks if a core supports the HDMI protocol
*
* @param	InstancePtr is a pointer to the XHdcp1x core instance.
*
* @return	Truth value indicating HDMI (TRUE) or not (FALSE)
*
******************************************************************************/
#define XHdcp1x_IsHDMI(InstancePtr) \
	((XHdcp1x_ReadReg((InstancePtr)->Config.BaseAddress, \
	XHDCP1X_CIPHER_REG_TYPE) & XHDCP1X_CIPHER_BITMASK_TYPE_PROTOCOL) \
	== XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_HDMI)

/*****************************************************************************/
/**
*
* This macro checks if a core supports the receive direction
*
* @param	InstancePtr is a pointer to the XHdcp1x core instance.
*
* @return	Truth value indicating receive (TRUE) or not (FALSE)
*
******************************************************************************/
#define XHdcp1x_IsRX(InstancePtr) \
	((XHdcp1x_ReadReg((InstancePtr)->Config.BaseAddress, \
	XHDCP1X_CIPHER_REG_TYPE) & XHDCP1X_CIPHER_BITMASK_TYPE_DIRECTION) \
	== XHDCP1X_CIPHER_VALUE_TYPE_DIRECTION_RX)

/*****************************************************************************/
/**
*
* This macro checks if a core supports the transmit direction
*
* @param	InstancePtr is a pointer to the XHdcp1x core instance.
*
* @return	Truth value indicating transmit (TRUE) or not (FALSE)
*
******************************************************************************/
#define XHdcp1x_IsTX(InstancePtr) \
	((XHdcp1x_ReadReg((InstancePtr)->Config.BaseAddress, \
	XHDCP1X_CIPHER_REG_TYPE) & XHDCP1X_CIPHER_BITMASK_TYPE_DIRECTION) \
	== XHDCP1X_CIPHER_VALUE_TYPE_DIRECTION_TX)

/************************** Function Prototypes ******************************/

/************************** Variable Declarations ****************************/

#ifdef __cplusplus
}
#endif

#endif /* XHDCP1X_HW_H */
/** @} */

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
* @file xhdcp1x_cipher.c
* @addtogroup hdcp1x_v4_0
* @{
*
* This file contains the main implementation of the driver associated with
* the Xilinx HDCP Cipher core.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 3.1   yas    06/14/16 Added new functions XHdcp1x_CipherEnableBlank
*                       and XHdcp1x_CipherDisableBlank
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

//#include <stdlib.h>
#include <linux/string.h>
#include "xhdcp1x.h"
#include "xhdcp1x_cipher.h"
#include "xil_assert.h"
#include "xil_types.h"
#include "xhdcp1x_debug.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/*************************** Function Prototypes *****************************/

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
* This function initializes an HDCP cipher.
*
* @param	InstancePtr is the device to initialize.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_CipherInit(XHdcp1x *InstancePtr)
{
	u32 Value = 0;

	/* Reset it */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_RESET;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_RESET;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Ensure all interrupts are disabled and cleared */
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_INTERRUPT_MASK, (u32)(-1));
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_INTERRUPT_STATUS, (u32)(-1));

	/* Check for DP */
	if (XHdcp1x_IsDP(InstancePtr)) {
		/* Configure for four lanes SST */
		Value  = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CONTROL);
		Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_NUM_LANES;
		Value |= (4u << 4);
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CONTROL, Value);
	}

	/* Ensure that the register update bit is set */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);
}

/*****************************************************************************/
/**
* This function queries the link state of a cipher device.
*
* @param	InstancePtr is	the device to query.
*
* @return	Truth value.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherIsLinkUp(const XHdcp1x *InstancePtr)
{
	u32 Value;
	int IsUp = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check for currently enabled */
	if (XHdcp1x_CipherIsEnabled(InstancePtr)) {
		Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_STATUS);
		if ((Value &
			XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL) != 0) {
			IsUp = TRUE;
		}
	}

	return (IsUp);
}

/*****************************************************************************/
/**
* This function enables a HDCP cipher.
*
* @param	InstancePtr is the device to enable.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherEnable(XHdcp1x *InstancePtr)
{
	u32 Value = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check for currently disabled */
	if (XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_FAILURE);
	}

	/* Clear the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Ensure that all encryption is disabled for now */
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H, 0x00ul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L, 0x00ul);

	/* Ensure that XOR is disabled on tx and enabled for rx to start */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
	if (XHdcp1x_IsRX(InstancePtr)) {
		Value |= XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
	}
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL, Value);

	/* Enable it */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_ENABLE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Ensure that the register update bit is set */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
*
* This function disables a HDCP cipher.
*
* @param	InstancePtr is the device to disable.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherDisable(XHdcp1x *InstancePtr)
{
	u32 Value = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Ensure all interrupts are disabled */
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_INTERRUPT_MASK, 0xFFFFFFFFul);

	/* Enable bypass operation */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_ENABLE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Ensure that all encryption is disabled for now */
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H, 0x00ul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L, 0x00ul);

	/* Ensure that XOR is disabled */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL, Value);

	/* Ensure that the register update bit is set */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function configures the key selection value.
*
* @param	InstancePtr is the device to configure.
* @param	KeySelect is the desired key select value.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherSetKeySelect(XHdcp1x *InstancePtr, u8 KeySelect)
{
	int Status = XST_SUCCESS;
	u32 Value = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(KeySelect < 8);

	/* Update the device */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_SET_SELECT;
	Value |= (KeySelect << 16);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL, Value);

	return (Status);
}

/*****************************************************************************/
/**
* This function initiates a request within the HDCP cipher.
*
* @param	InstancePtr is the device to submit the request to.
* @param	Request is the request to submit.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_NOT_ENABLE if the core is disabled.
*		- XST_DEVICE_BUSY if the core is busy.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherDoRequest(XHdcp1x *InstancePtr,
		XHdcp1x_CipherRequestType Request)
{
	u32 Value = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Request >= (XHDCP1X_CIPHER_REQUEST_BLOCK));

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Determine if there is a request in progress */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_STATUS);
	Value &= XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_REQUEST_IN_PROG;

	/* Check that it is not busy */
	if (Value != 0) {
		return (XST_DEVICE_BUSY);
	}

	/* Ensure that the register update bit is set */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Set the appropriate request bit
	 * and ensure that Km is always used
	 */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_REQUEST;
	Value |= (XHDCP1X_CIPHER_VALUE_CIPHER_CONTROL_REQUEST_BLOCK
								<< Request);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL, Value);

	/* Ensure that the request bit(s) get cleared for next time */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_REQUEST;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL, Value);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function queries the progress of the current request.
*
* @param	InstancePtr is the device to query.
*
* @return	Truth value.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherIsRequestComplete(const XHdcp1x *InstancePtr)
{
	u32 Value = 0;
	int IsComplete = TRUE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine Value */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_STATUS);
	Value &= XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_REQUEST_IN_PROG;

	/* Update IsComplete */
	if (Value != 0) {
		IsComplete = FALSE;
	}

	return (IsComplete);
}

/*****************************************************************************/
/**
* This function retrieves the current number of lanes of the HDCP cipher.
*
* @param	InstancePtr is the device to query.
*
* @return	The current number of lanes.
*
* @note		None.
*
******************************************************************************/
u32 XHdcp1x_CipherGetNumLanes(const XHdcp1x *InstancePtr)
{
	u32 NumLanes = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check for currently enabled */
	if (XHdcp1x_CipherIsEnabled(InstancePtr)) {
		/* Determine NumLanes */
		NumLanes = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CONTROL);
		NumLanes &= XHDCP1X_CIPHER_BITMASK_CONTROL_NUM_LANES;
		NumLanes >>= 4;
	}

	return (NumLanes);
}

/*****************************************************************************/
/**
* This function configures the number of lanes of the HDCP cipher.
*
* @param	InstancePtr is the device to configure.
* @param	NumLanes is the number of lanes to configure.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherSetNumLanes(XHdcp1x *InstancePtr, u32 NumLanes)
{
	int Status = XST_SUCCESS;
	u32 Value = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(NumLanes > 0);
	Xil_AssertNonvoid(NumLanes <= 4);

	/* Check for HDMI */
	if (XHdcp1x_IsHDMI(InstancePtr)) {
		/* Verify NumLanes (again) */
		Xil_AssertNonvoid(NumLanes == 1);

		/* Update the control register */
		Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CONTROL);
		Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_NUM_LANES;
		Value |= (NumLanes << 4);
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CONTROL, Value);
	}
	/* Otherwise - must be DP */
	else {
		/* Verify NumLanes (again) */
		Xil_AssertNonvoid(NumLanes != 3);

		/* Update the control register */
		Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CONTROL);
		Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_NUM_LANES;
		Value |= (NumLanes << 4);
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CONTROL, Value);
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function retrieves the current encryption stream map.
*
* @param	InstancePtr is the device to query.
*
* @return	The current encryption stream map.
*
* @note		In the case of the receiver version of this core, the XOR in
*		progress bit needs to be checked as well as the encryption map
*		to fully determine if encryption is enabled for the SST case.
*		This is the reason for the additional check in this code.
*
******************************************************************************/
u64 XHdcp1x_CipherGetEncryption(const XHdcp1x *InstancePtr)
{
	u64 StreamMap = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (StreamMap);
	}

	/* Determine StreamMap */
	StreamMap  = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H);
	StreamMap <<= 32;
	StreamMap |= XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L);

	/* Check for special case of just XOR in progress */
	if ((StreamMap == 0) &&
			(XHdcp1x_CipherXorInProgress(InstancePtr))) {
		StreamMap = 0x01ul;
	}

	return (StreamMap);
}

/*****************************************************************************/
/**
* This function enables encryption on a set of streams.
*
* @param	InstancePtr is the device to configure.
* @param	StreamMap is the bit map of streams to enable encryption on.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_NOT_ENABLE if the core is not enabled.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherEnableEncryption(XHdcp1x *InstancePtr, u64 StreamMap)
{
	u32 Value = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Check that it is not a receiver */
	if (XHdcp1x_IsRX(InstancePtr)) {
		return (XST_FAILURE);
	}

	/* Check for nothing to do */
	if (StreamMap == 0) {
		return (XST_SUCCESS);
	}

	/* Clear the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Update the LS 32-bits */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L);
	Value |= ((u32) (StreamMap & 0xFFFFFFFFul));
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L, Value);

	/* Write the MS 32-bits */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H);
	Value |= ((u32) ((StreamMap >> 32) & 0xFFFFFFFFul));
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H, Value);

	/* Ensure that the XOR is enabled */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_CONTROL, Value);

	/* Set the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Check if XORInProgress bit is set in the status register*/
	if (!XHdcp1x_CipherXorInProgress(InstancePtr)) {
		/* Do nothing for now. We can depend on the Cipher
		 * to set the XorInProgress in status register when
		 * we receive protected content. */
	}

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function disables encryption on a set of streams.
*
* @param	InstancePtr is the device to configure.
* @param	StreamMap is the bit map of streams to disable encryption on.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_NOT_ENABLE if the core is not enabled.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherDisableEncryption(XHdcp1x *InstancePtr, u64 StreamMap)
{
	u32 Val = 0;
	int DisableXor = TRUE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Check that it is not a receiver */
	if (XHdcp1x_IsRX(InstancePtr)) {
		return (XST_FAILURE);
	}

	/* Check for nothing to do */
	if (StreamMap == 0) {
		return (XST_SUCCESS);
	}

	/* Clear the register update bit */
	Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Val &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Val);

	/* Update the LS 32-bits */
	Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L);
	Val &= ~((u32) (StreamMap & 0xFFFFFFFFul));
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L, Val);
	if (Val != 0) {
		DisableXor = FALSE;
	}

	/* Write the MS 32-bits */
	Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H);
	Val &= ~((u32) ((StreamMap >> 32) & 0xFFFFFFFFul));
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H, Val);
	if (Val != 0) {
		DisableXor = FALSE;
	}

	/* Check HDMI special case */
	if (XHdcp1x_IsHDMI(InstancePtr)) {
		DisableXor = TRUE;
	}

	/* Check for XOR disable */
	if (DisableXor) {
		Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
		Val &= ~XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CIPHER_CONTROL, Val);
	}

	/* Set the register update bit */
	Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Val |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Val);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function reads the local KSV value from the cipher.
*
* @param	InstancePtr is the device to query.
*
* @return	The local KSV value.
*
* @note		None.
*
******************************************************************************/
u64 XHdcp1x_CipherGetLocalKsv(const XHdcp1x *InstancePtr)
{
	u32 Val = 0;
	u32 Guard = 0x400ul;
	u64 Ksv = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (Ksv);
	}

	/* Check if the local ksv is not available */
	Val  = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KEYMGMT_STATUS);
	Val &= XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_KSV_READY;
	if (Val == 0) {
		/* Abort any running Km calculation just in case */
		Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL);
		Val |= XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_ABORT_Km;
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL, Val);
		Val &= ~XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_ABORT_Km;
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL, Val);

		/* Load the local ksv */
		Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL);
		Val |= XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_LOCAL_KSV;
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL, Val);
		Val &= ~XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_LOCAL_KSV;
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL, Val);

		/* Wait until local KSV available */
		while ((!XHdcp1x_CipherLocalKsvReady(InstancePtr)) &&
			(--Guard > 0));
	}

	/* Confirm no timeout */
	if (Guard != 0) {
		/* Update Ksv */
		Ksv = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_KSV_LOCAL_H);
		Ksv &= 0xFFul;
		Ksv <<= 32;
		Ksv |= XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_KSV_LOCAL_L);
	}

	return (Ksv);
}

/*****************************************************************************/
/**
* This function reads the remote KSV value from the cipher.
*
* @param	InstancePtr is the device to query.
*
* @return	The remote KSV value.
*
* @note		None.
*
******************************************************************************/
u64 XHdcp1x_CipherGetRemoteKsv(const XHdcp1x *InstancePtr)
{
	u64 Ksv = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine Ksv */
	Ksv = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KSV_REMOTE_H);
	Ksv <<= 32;
	Ksv |= XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KSV_REMOTE_L);

	return (Ksv);
}

/*****************************************************************************/
/**
* This function writes the remote KSV value to the cipher.
*
* @param	InstancePtr is the device to write to.
* @param	Ksv is the remote KSV value to write.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_NOT_ENABLED otherwise.
*
* @note		Whenever this function is called, the underlying driver will
*		initiate the calculation of the Km value and wait for it to
*		complete.
*
******************************************************************************/
int XHdcp1x_CipherSetRemoteKsv(XHdcp1x *InstancePtr, u64 Ksv)
{
	u32 Value = 0;
	u32 Guard = 0x400ul;
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Read local ksv to put things into a known state */
	XHdcp1x_CipherGetLocalKsv(InstancePtr);

	/* Clear the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Write the LS 32-bits */
	Value = (u32)(Ksv & 0xFFFFFFFFul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KSV_REMOTE_L, Value);

	/* Write the MS 8-bits */
	Value = (u32)((Ksv >> 32) & 0xFFul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KSV_REMOTE_H, Value);

	/* Set the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Trigger the calculation of theKm */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL);
	Value &= 0xFFFFFFF0ul;
	Value |= XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_BEGIN_Km;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL, Value);
	Value &= ~XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_BEGIN_Km;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL, Value);

	/* Wait until Km is available */
	while ((!XHdcp1x_CipherKmReady(InstancePtr)) && (--Guard > 0));

	/* Check for timeout */
	if (Guard == 0) {
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function reads the contents of the B register in BM0.
*
* @param	InstancePtr is the device to query.
* @param	X is to be loaded with the contents of Bx.
* @param	Y is to be loaded with the contents of By.
* @param	Z is to be loaded with the contents of Bz.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_NOT_ENABLED otherwise.
*
* @note		A NULL pointer can be passed in any of X, Y and Z.  If so, then
*		this portion of the B register is not returned to the caller.
*
******************************************************************************/
int XHdcp1x_CipherGetB(const XHdcp1x *InstancePtr, u32 *X, u32 *Y, u32 *Z)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Get X if requested */
	if (X != NULL) {
		*X = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CIPHER_Bx);
		*X &= 0x0FFFFFFFul;
	}

	/* Get Y if requested */
	if (Y != NULL) {
		*Y = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CIPHER_By);
		*Y &= 0x0FFFFFFFul;
	}

	/* Get Z if requested */
	if (Z != NULL) {
		*Z = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CIPHER_Bz);
		*Z &= 0x0FFFFFFFul;
	}

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function writes the contents of the B register in BM0.
*
* @param	InstancePtr is the device to write to.
* @param	X is the value to be written to Bx.
* @param	Y is the value to be written to By.
* @param	Z is the value to be written to Bz.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_NOT_ENABLED otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherSetB(XHdcp1x *InstancePtr, u32 X, u32 Y, u32 Z)
{
	u32 Value = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Clear the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Update the Bx */
	Value = (X & 0x0FFFFFFFul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Bx, Value);

	/* Update the By */
	Value = (Y & 0x0FFFFFFFul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_By, Value);

	/* Update the Bz */
	Value = (Z & 0x0FFFFFFFul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Bz, Value);

	/* Set the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function reads the contents of the K register in BM0.
*
* @param	InstancePtr is the device to query.
* @param	X is to be loaded with the contents of Kx.
* @param	Y is to be loaded with the contents of Ky.
* @param	Z is to be loaded with the contents of Kz.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_NOT_ENABLED otherwise.
*
* @note		A NULL pointer can be passed in any of X, Y and Z. If so,
*		then this portion of the K register is not returned
*		to the caller.
*
******************************************************************************/
int XHdcp1x_CipherGetK(const XHdcp1x *InstancePtr, u32 *X, u32 *Y, u32 *Z)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Get X if requested */
	if (X != NULL) {
		*X = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CIPHER_Kx);
		*X &= 0x0FFFFFFFul;
	}

	/* Get Y if requested */
	if (Y != NULL) {
		*Y = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CIPHER_Ky);
		*Y &= 0x0FFFFFFFul;
	}

	/* Get Z if requested */
	if (Z != NULL) {
		*Z = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_CIPHER_Kz);
		*Z &= 0x0FFFFFFFul;
	}

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function writes the contents of the K register in BM0.
*
* @param	InstancePtr is the device to write to.
* @param	X is the value to be written to Kx.
* @param	Y is the value to be written to Ky.
* @param	Z is the value to be written to Kz.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_NOT_ENABLED otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherSetK(XHdcp1x *InstancePtr, u32 X, u32 Y, u32 Z)
{
	u32 Value = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Clear the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	/* Update the Kx */
	Value = (X & 0x0FFFFFFFul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Kx, Value);

	/* Update the Ky */
	Value = (Y & 0x0FFFFFFFul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Ky, Value);

	/* Update the Kz */
	Value = (Z & 0x0FFFFFFFul);
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Kz, Value);

	/* Set the register update bit */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL);
	Value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CONTROL, Value);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function reads the contents of the Mi/An register of BM0.
*
* @param	InstancePtr is the device to query.
*
* @return	The contents of the register.
*
* @note		None.
*
******************************************************************************/
u64 XHdcp1x_CipherGetMi(const XHdcp1x *InstancePtr)
{
	u64 Mi = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Update Mi */
	Mi = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Mi_H);
	Mi <<= 32;
	Mi |= XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Mi_L);

	return (Mi);
}

/*****************************************************************************/
/**
* This function reads the contents of the Ri register of BM0.
*
* @param	InstancePtr is the device to query.
*
* @return	The contents of the register.
*
* @note		None.
*
******************************************************************************/
u16 XHdcp1x_CipherGetRi(const XHdcp1x *InstancePtr)
{
	u16 Ri = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Determine Ri */
	Ri = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Ri);

	return (Ri);
}

/*****************************************************************************/
/**
* This function reads the contents of the Mo register of the device.
*
* @param	InstancePtr is the device to query.
*
* @return	The contents of the Mo register.
*
* @note		None.
*
******************************************************************************/
u64 XHdcp1x_CipherGetMo(const XHdcp1x *InstancePtr)
{
	u64 Mo = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Determine Mo */
	Mo = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Mo_H);
	Mo <<= 32;
	Mo |= XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Mo_L);

	return (Mo);
}

/*****************************************************************************/
/**
* This function reads the contents of the Ro register of the device.
*
* @param	InstancePtr is the device to query.
*
* @return	The contents of the Ro register.
*
* @note		None.
*
******************************************************************************/
u16 XHdcp1x_CipherGetRo(const XHdcp1x *InstancePtr)
{
	u16 Ro = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check that it is not disabled */
	if (!XHdcp1x_CipherIsEnabled(InstancePtr)) {
		return (XST_NOT_ENABLED);
	}

	/* Determine Ro */
	Ro = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_CIPHER_Ro);

	return (Ro);
}

/*****************************************************************************/
/**
* This function reads the version of the HDCP cipher core.
*
* @param	InstancePtr is the device to query.
*
* @return	The version of the HDCP cipher device.
*
* @note		None.
*
******************************************************************************/
u32 XHdcp1x_CipherGetVersion(const XHdcp1x *InstancePtr)
{
	u32 Version = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine Version */
	Version = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_VERSION);

	return (Version);
}

/*****************************************************************************/
/**
 * This function sets the cipher blank value to 0x0000FF (blue),
 * and sets the cipher blank select to TRUE
 *
 * @param	InstancePtr is the device to query.
 *
 * @return	None.
 *
 * @note	None.
 *
 *****************************************************************************/
void XHdcp1x_CipherEnableBlank(XHdcp1x *InstancePtr)
{
	u32 Value = 0;

	/* Verify Arguments */
	Xil_AssertVoid(InstancePtr);

	/* Set the cipher blank value */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_BLANK_VALUE);
	Value |= XHDCP1X_CIPHER_BITMASK_BLANK_VALUE;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_BLANK_VALUE, Value);

	/* Enable the cipher blank */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_BLANK_SEL);
	Value |= XHDCP1X_CIPHER_BITMASK_BLANK_SEL;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_BLANK_SEL, Value);
}

/*****************************************************************************/
/**
 *  This function sets the cipher blank select to FALSE
 *
 * @param	InstancePtr is the device to query.
 *
 * @return	None.
 *
 * @note	None.
 *
 *****************************************************************************/
void XHdcp1x_CipherDisableBlank(XHdcp1x *InstancePtr)
{
	u32 Value = 0;

	/* Verify Arguments */
	Xil_AssertVoid(InstancePtr);

	/* Disable the cipher blank */
	Value = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_BLANK_SEL);
	Value &= ~XHDCP1X_CIPHER_BITMASK_BLANK_SEL;
	XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_BLANK_SEL, Value);
}
/** @} */

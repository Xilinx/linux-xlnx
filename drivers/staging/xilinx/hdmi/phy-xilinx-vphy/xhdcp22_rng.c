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
* @file xhdcp22_rng.c
* @addtogroup hdcp22_rng_v1_1
* @{
* @details
*
* This file contains the main implementation of the Xilinx HDCP 2.2 RNG
* device driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  JO     10/01/15 Initial release.
* 1.01  MH     08/04/16 Added 64 bit address support.
* 1.02  MH     02/17/16 Fixed pointer alignment problem in function
*                       XHdcp22Rng_GetRandom
* </pre>
*
******************************************************************************/


/***************************** Include Files *********************************/
#include "xhdcp22_rng.h"
#include <linux/string.h>

/************************** Constant Definitions *****************************/

/***************** Macros (Inline Functions) Definitions *********************/

/**************************** Type Definitions *******************************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
*
* This function initializes the HDCP22 Rng core. This function must be called
* prior to using the HDCP22 Rng core. Initialization of the HDCP22 Rng includes
* setting up the instance data, and ensuring the hardware is in a quiescent
* state.
*
* @param  InstancePtr is a pointer to the XHdcp22_Rng core instance.
* @param  CfgPtr points to the configuration structure associated with
*         the HDCP22 Rng core core.
* @param  EffectiveAddr is the base address of the device. If address
*         translation is being used, then this parameter must reflect the
*         virtual base address. Otherwise, the physical address should be
*         used.
*
* @return
*   - XST_SUCCESS if XHdcp22Rng_CfgInitialize was successful.
*   - XST_FAILURE if HDCP22 Rng ID mismatched.
*
* @note		None.
*
******************************************************************************/
int XHdcp22Rng_CfgInitialize(XHdcp22_Rng *InstancePtr,
                                XHdcp22_Rng_Config *CfgPtr,
                                UINTPTR EffectiveAddr)
{
	u32 RegValue;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(CfgPtr != NULL);
	Xil_AssertNonvoid(EffectiveAddr != (UINTPTR)NULL);

	/* Setup the instance */
	(void)memset((void *)InstancePtr, 0, sizeof(XHdcp22_Rng));
	(void)memcpy((void *)&(InstancePtr->Config), (const void *)CfgPtr, sizeof(XHdcp22_Rng_Config));
	InstancePtr->Config.BaseAddress = EffectiveAddr;

	/* Check ID */
	RegValue = XHdcp22Rng_ReadReg(InstancePtr->Config.BaseAddress, (XHDCP22_RNG_VER_ID_OFFSET));
	RegValue = ((RegValue) >> (XHDCP22_RNG_SHIFT_16)) & (XHDCP22_RNG_MASK_16);
	if (RegValue != (XHDCP22_RNG_VER_ID)) {
		return (XST_FAILURE);
	}

	/* Reset the hardware and set the flag to indicate the driver is ready */
	InstancePtr->IsReady = (u32)(XIL_COMPONENT_IS_READY);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
*
* This function returns a random number.
*
* @param  InstancePtr is a pointer to the XHdcp22_Rng core instance.
* @param  BufferPtr points to the buffer that will contain a random number.
* @param  BufferLength is the length of the BufferPtr in bytes.
*         The length must be greater than or equal to RandomLength.
* @param  RandomLength is the requested length of the random number in bytes.
*         The length must be a multiple of 4
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Rng_GetRandom(XHdcp22_Rng *InstancePtr, u8 *BufferPtr, u16 BufferLength, u16 RandomLength)
{
	u32 i, j;
	u32 Offset = 0;
	u32 RandomWord;
	u8 *RandomPtr = (u8 *)&RandomWord;

	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(BufferPtr != NULL);
	Xil_AssertVoid(RandomLength%4 == 0);
	Xil_AssertVoid(BufferLength >= RandomLength);

	for (i=0; i<RandomLength; i+=4)
	{
		RandomWord = XHdcp22Rng_ReadReg(InstancePtr->Config.BaseAddress,
					XHDCP22_RNG_REG_RN_1_OFFSET + Offset);
		for (j=0; j<4; j++) {
			BufferPtr[i + j] = RandomPtr[j];
		}

		/* Increase offset to the next register and wrap after the last register
		   (RNG length is 16 bytes) */
		Offset = (Offset+4) % 16;
	}
}

/** @} */

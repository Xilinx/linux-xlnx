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
* @file hmac.c
*
* This file contains the implementation of the HMAC Hash Message
* Authentication Code.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00  MH   10/30/15 First Release
*</pre>
*
*****************************************************************************/

/***************************** Include Files *********************************/
#include <linux/types.h>
#include <linux/math64.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "xil_types.h"
#include "xstatus.h"

#include "xhdcp22_common.h"

/************************** Constant Definitions *****************************/
#define SHA256_SIZE		256/8	/**< SHA256 Hash size */

/***************** Macros (Inline Functions) Definitions *********************/

/**************************** Type Definitions *******************************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
*
* This function does a HMAC_SHA256 transform:
* SHA256(K XOR opad, SHA256(K XOR ipad, text))
*
* ipad is the byte 0x36 repeated 64 times
* opad is the byte 0x5c repeated 64 times
* and text is the data being protected
*
* @param	Data is the input data.
* @param	DataSize is the size of the data buffer.
* @param	Key is the hash-key to use.
* @param	KeySize is the size of the hash key.
* @param	HashedData is the output of this function.
*
* @return	- XST_SUCCESS if no errors occured
*			- XST_FAILURE if the datasize execeeds the size of the local buffer.
*
* @note		None.
*
******************************************************************************/
int XHdcp22Cmn_HmacSha256Hash(const u8 *Data, int DataSize, const u8 *Key, int KeySize, u8  *HashedData)
{
#if 0
	u8 Ipad[65];   /* inner padding-key XORd with ipad */
	u8 Opad[65];   /* outer padding-key XORd with opad */
	u8 Ktemp[SHA256_SIZE];
	u8 Ktemp2[SHA256_SIZE];
	u8 BufferIn[256];
	u8 BufferOut[256];
	int i;

	memset(BufferIn, 0x00, 256);
	memset(BufferOut, 0x00, 256);
#endif
	u8 *Ipad, *Opad, *Ktemp, *Ktemp2, *BufferIn, *BufferOut;
	Ipad = kzalloc(65, GFP_KERNEL);
	Opad = kzalloc(65, GFP_KERNEL);
	Ktemp = kzalloc(SHA256_SIZE, GFP_KERNEL);
	Ktemp2 = kzalloc(SHA256_SIZE, GFP_KERNEL);
	BufferIn = kzalloc(256, GFP_KERNEL);
	BufferOut = kzalloc(256, GFP_KERNEL);
	int i;

	/* If the input size exceeds the local buffers, return an error */
	if(DataSize + 64 >  256) {
		return XST_FAILURE;
	}

	/* If key is longer than 64 bytes reset it to Key=sha256(Key) */
	if(KeySize > 64) {
		XHdcp22Cmn_Sha256Hash(Key, KeySize, Ktemp );
		Key     = Ktemp;
		KeySize = SHA256_SIZE;
	}

	/* start out by storing Key in pads */
#if 0
	memset(Ipad, 0, sizeof Ipad );
	memset(Opad, 0, sizeof Opad );
#endif
	memcpy(Ipad, Key, KeySize );
	memcpy(Opad, Key, KeySize );

	/* XOR Key with Ipad and Opad values */
	for(i = 0; i < 64; i++) {
		Ipad[i] ^= 0x36;
		Opad[i] ^= 0x5c;
	}

	/* Execute inner SHA256 */
	memcpy(BufferIn, Ipad, 64 );
	memcpy(BufferIn + 64, Data, DataSize );
	XHdcp22Cmn_Sha256Hash(BufferIn, 64 + DataSize, Ktemp2 );

	/* Execute outer SHA256 */
	memcpy(BufferOut, Opad, 64);
	memcpy(BufferOut + 64, Ktemp2, SHA256_SIZE );
	XHdcp22Cmn_Sha256Hash(BufferOut, 64 + SHA256_SIZE,
						(u8 *)HashedData );

	kfree(BufferOut);
	kfree(BufferIn);
	kfree(Ktemp);
	kfree(Ktemp2);
	kfree(Opad);
	kfree(Ipad);

	return XST_SUCCESS;
}

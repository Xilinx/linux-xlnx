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
* @file xhdcp22_common.h
*
* This file contains common functions shared between HDCP22 drivers.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00  MH   10/30/15 First Release.
* 1.01  MH   01/15/16 Added prefix to function names.
* 2.00  MH   06/21/17 Changed DIGIT_T type to u32 for ARM support.
*</pre>
*
*****************************************************************************/

#ifndef XHDCP22_COMMON_H_
#define XHDCP22_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files ********************************/
#include "bigdigits.h"

/************************** Constant Definitions ****************************/

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Function Prototypes *****************************/

/* Cryptographic functions */
void XHdcp22Cmn_Sha256Hash(const u8 *Data, u32 DataSize, u8 *HashedData);
int  XHdcp22Cmn_HmacSha256Hash(const u8 *Data, int DataSize, const u8 *Key, int KeySize, u8  *HashedData);
void XHdcp22Cmn_Aes128Encrypt(const u8 *Data, const u8 *Key, u8 *Output);
void XHdcp22Cmn_Aes128Decrypt(const u8 *Data, const u8 *Key, u8 *Output);

#ifdef __cplusplus
}
#endif

#endif /* XHDCP22_COMMON_H_ */

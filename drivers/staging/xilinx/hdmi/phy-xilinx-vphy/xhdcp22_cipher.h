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
* @file xhdcp22_cipher.h
* @addtogroup hdcp22_cipher_v1_1
* @{
* @details
*
* This is the main header file of the Xilinx HDCP 2.2 Cipher device driver.
* The Cipher implements the AES-128 standard for encrypting and decrypting
* audiovisual content. The Cipher is required to be programmed with the
* global constant Lc128, random number Riv, and session key Ks before encryption
* is enabled. Internally, the cipher uses the Enhanced Encryption Signaling
* Status (EESS) to determine when to encrypt and decrypt frames. It also
* manages the data and frame counters to ensure the transmitter and receiver
* Ciphers are synchronized.
*
* <b>Software Initialization and Configuration</b>
*
* The application needs to do the following steps to run the Cipher.
* - Call XHdcp22Cipher_LookupConfig using the device ID to find the
*   core configuration instance.
* - Call XHdcp22Cipher_CfgInitialize to intitialize the device instance.
* - Call XHdcp22Cipher_SetTxMode or XHdcp22Cipher_SetRxMode to setup
*   the Cipher as either a transmitter or receiver.
* - Call XHdcp22Cipher_Enable to enable the cipher.
* - Call XHdcp22Cipher_SetLc128 to program the Lc128 constant.
* - Call XHdcp22Cipher_SetRiv to program the random number Riv.
* - Call XHdcp22Cipher_SetKs to program the session key Ks.
* - If operating as a transmitter call XHdcp22Cipher_EnableTxEncryption
*   to enable encryption or XHdcp22Cipher_DisableTxEncryption to
*   disable encryption.
*
* <b>Interrupts</b>
*
* None.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  JO     10/01/15 Initial release.
* 1.01  MG     10/28/15 Added Noise and blank macros
* 1.02  MG     02/25/15 Added GetVersion macro
* 1.03  MH     08/04/16 Added 64 bit address support.
* </pre>
*
******************************************************************************/

#ifndef XHDCP22_CIPHER_H
/**< Prevent circular inclusions by using protection macros */
#define XHDCP22_CIPHER_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include "xhdcp22_cipher_hw.h"
#include "xil_assert.h"
#include "xstatus.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/**
* This typedef contains configuration information for the HDCP22 Cipher core.
* Each HDCP22 Cipher device should have a configuration structure associated.
*/
typedef struct {
	u16 DeviceId;     /**< DeviceId is the unique ID of the HDCP22 Cipher core */
	UINTPTR BaseAddress;  /**< BaseAddress is the physical base address of the core's registers */
} XHdcp22_Cipher_Config;

/**
* The XHdcp22 Cipher driver instance data. An instance must be allocated for each
* HDCP22 Cipher core in use.
*/
typedef struct {
	XHdcp22_Cipher_Config Config; /**< Hardware Configuration */
	u32 IsReady;                  /**< Core and the driver instance are initialized */
} XHdcp22_Cipher;

/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
/**
*
* This macro enables the HDCP22 Cipher peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Cipher_Enable(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_Enable(InstancePtr) \
        XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, \
        (XHDCP22_CIPHER_REG_CTRL_SET_OFFSET),(XHDCP22_CIPHER_REG_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro disables the HDCP22 Cipher peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Cipher_Disable(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_Disable(InstancePtr) \
        XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, \
        (XHDCP22_CIPHER_REG_CTRL_CLR_OFFSET), (XHDCP22_CIPHER_REG_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro returns the encrypted enabled state of the HDCP22 Cipher core
* instance.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return TRUE if HDCP22 cipher is enabled, FALSE otherwise.
*
* @note   C-style signature:
*         u32 XHdcp22Cipher_IsEnabled(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_IsEnabled(InstancePtr) \
        ((XHdcp22Cipher_GetControlReg((InstancePtr)->Config.BaseAddress)\
        & XHDCP22_CIPHER_REG_CTRL_RUN_MASK) ==  XHDCP22_CIPHER_REG_CTRL_RUN_MASK)

/*****************************************************************************/
/**
*
* This macro sets the HDCP operation mode for the HDCP22 Cipher peripheral.
* The mode
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Cipher_SetTxMode(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_SetTxMode(InstancePtr) \
        XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, \
        (XHDCP22_CIPHER_REG_CTRL_CLR_OFFSET), (XHDCP22_CIPHER_REG_CTRL_MODE_MASK))

/*****************************************************************************/
/**
*
* This macro sets the HDCP RX operation mode for the HDCP22 Cipher peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Cipher_SetRxMode(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_SetRxMode(InstancePtr) \
        XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, \
        (XHDCP22_CIPHER_REG_CTRL_SET_OFFSET), (XHDCP22_CIPHER_REG_CTRL_MODE_MASK))

/*****************************************************************************/
/**
*
* This macro enables HDCP TX encryption for the HDCP22 Cipher peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Cipher_EnableTxEncryption(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_EnableTxEncryption(InstancePtr) \
        XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, \
        (XHDCP22_CIPHER_REG_CTRL_SET_OFFSET), (XHDCP22_CIPHER_REG_CTRL_ENCRYPT_MASK))

/*****************************************************************************/
/**
*
* This macro disables HDCP TX encryption for the HDCP22 Cipher peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Cipher_DisableTxEncryption(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_DisableTxEncryption(InstancePtr) \
        XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, \
        (XHDCP22_CIPHER_REG_CTRL_CLR_OFFSET), (XHDCP22_CIPHER_REG_CTRL_ENCRYPT_MASK))

/*****************************************************************************/
/**
*
* This macro returns the encrypted enabled state of HDCP TX encryption
* for the HDCP22 Cipher peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return TRUE if HDCP22 TX encryption is enabled, FALSE otherwise.
*
* @note   C-style signature:
*         u32 XHdcp22Cipher_IsTxEncryptionEnabled(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_IsTxEncryptionEnabled(InstancePtr) \
        ((XHdcp22Cipher_GetControlReg((InstancePtr)->Config.BaseAddress)\
        & XHDCP22_CIPHER_REG_CTRL_ENCRYPT_MASK) ==  XHDCP22_CIPHER_REG_CTRL_ENCRYPT_MASK)

/*****************************************************************************/
/**
*
* This macro returns the encrypted state for the HDCP22 Cipher peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return TRUE if the frame is encrypted, FALSE otherwise.
*
* @note   C-style signature:
*         void XHdcp22Cipher_DisableTxEncryption(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_IsEncrypted(InstancePtr) \
        ((XHdcp22Cipher_GetStatusReg((InstancePtr)->Config.BaseAddress) \
        & XHDCP22_CIPHER_REG_STA_ENCRYPTED_MASK) ==  XHDCP22_CIPHER_REG_STA_ENCRYPTED_MASK)

/*****************************************************************************/
/**
*
* This macro enables or disables noise output for the HDCP22 Cipher
* peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
* @param  Set specifies TRUE/FALSE either to enable/disable noise output.
*
* @return none.
*
* @note   C-style signature:
*         void XHdcp22Cipher_Noise(u32 BaseAddress, u8 Set)
*
******************************************************************************/
#define XHdcp22Cipher_Noise(InstancePtr, Set) \
{ \
        if (Set) { \
                XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, (XHDCP22_CIPHER_REG_CTRL_SET_OFFSET), (XHDCP22_CIPHER_REG_CTRL_NOISE_MASK)); \
        } \
        else { \
                XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, (XHDCP22_CIPHER_REG_CTRL_CLR_OFFSET), (XHDCP22_CIPHER_REG_CTRL_NOISE_MASK)); \
        } \
}

/*****************************************************************************/
/**
*
* This macro enables or disables blank screen for the HDCP22 Cipher
* peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
* @param  Set specifies TRUE/FALSE either to enable/disable blank screen.
*
* @return none.
*
* @note   C-style signature:
*         void XHdcp22Cipher_Blank(u32 BaseAddress, u8 Set)
*
******************************************************************************/
#define XHdcp22Cipher_Blank(InstancePtr, Set) \
{ \
        if (Set) { \
                XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, (XHDCP22_CIPHER_REG_CTRL_SET_OFFSET), (XHDCP22_CIPHER_REG_CTRL_BLANK_MASK)); \
        } \
        else { \
                XHdcp22Cipher_WriteReg((InstancePtr)->Config.BaseAddress, (XHDCP22_CIPHER_REG_CTRL_CLR_OFFSET), (XHDCP22_CIPHER_REG_CTRL_BLANK_MASK)); \
        } \
}

/*****************************************************************************/
/**
*
* This macro reads the version for the HDCP22 Cipher
* peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 Cipher core instance.
*
* @return version.
*
* @note   C-style signature:
*         void XHdcp22Cipher_GetVersion(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Cipher_GetVersion(InstancePtr) \
        XHdcp22Cipher_ReadReg((InstancePtr)->Config.BaseAddress, XHDCP22_CIPHER_VER_VERSION_OFFSET)

/************************** Function Prototypes ******************************/
/* Initialization function in xhdcp22_cipher_sinit.c */
XHdcp22_Cipher_Config *XHdcp22Cipher_LookupConfig(u16 DeviceId);

/* Initialization and control functions in xhdcp22_cipher.c */
int XHdcp22Cipher_CfgInitialize(XHdcp22_Cipher *InstancePtr, XHdcp22_Cipher_Config *CfgPtr, UINTPTR EffectiveAddr);

void XHdcp22Cipher_SetKs(XHdcp22_Cipher *InstancePtr, const u8 *KsPtr, u16 Length);
void XHdcp22Cipher_SetLc128(XHdcp22_Cipher *InstancePtr, const u8 *Lc128Ptr,  u16 Length);
void XHdcp22Cipher_SetRiv(XHdcp22_Cipher *InstancePtr, const u8 *RivPtr,  u16 Length);

/************************** Variable Declarations ****************************/

#ifdef __cplusplus
}
#endif

#endif /* XHDCP2_CIPHER_H */

/** @} */

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
* @file xhdcp22_rng.h
* @addtogroup hdcp22_rng_v1_1
* @{
* @details
*
* This is the main header file of the Xilinx HDCP 2.2 RNG device driver.
* The RNG is a random number generator, which is used to produce
* random numbers during the authentication and key exchange.
*
* <b>Software Initialization and Configuration</b>
*
* The application needs to do the following steps to run the RNG.
* - Call XHdcp22Rng_LookupConfig using the device ID to find the
*   core configuration instance.
* - Call XHdcp22Rng_CfgInitialize to intitialize the device instance.
* - Call XHdcp22Rng_Enable to enable the device.
* - Call XHdcp22Rng_GetRandom to get random words.
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
* 1.01  MH     08/04/16 Added 64 bit address support.
* </pre>
*
******************************************************************************/

#ifndef XHDCP22_RNG_H
/**< Prevent circular inclusions by using protection macros */
#define XHDCP22_RNG_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include "xhdcp22_rng_hw.h"
#include "xil_assert.h"
#include "xstatus.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/
/**
* This typedef contains configuration information for the HDCP22 Rng core.
* Each HDCP22 Rng device should have a configuration structure associated.
*/
typedef struct {
	u16 DeviceId;     /**< DeviceId is the unique ID of the HDCP22 Rng core */
	UINTPTR BaseAddress;  /**< BaseAddress is the physical base address of the core's registers */
} XHdcp22_Rng_Config;

/**
* The XHdcp22 Rng driver instance data. An instance must be allocated for each
* HDCP22 Rng core in use.
*/
typedef struct {
	XHdcp22_Rng_Config Config; /**< Hardware Configuration */
	u32 IsReady;               /**< Core and the driver instance are initialized */
} XHdcp22_Rng;

/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
/**
*
* This macro enables the HDCP22 RNG peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 RNG core instance.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Rng_Enable(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Rng_Enable(InstancePtr) \
        XHdcp22Rng_WriteReg((InstancePtr)->Config.BaseAddress, \
       (XHDCP22_RNG_REG_CTRL_SET_OFFSET), (XHDCP22_RNG_REG_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro disables the HDCP22 RNG peripheral.
*
* @param  InstancePtr is a pointer to the HDCP22 RNG core instance.
*
* @return None.
*
* @note   C-style signature:
*         void XHdcp22Rng_Disable(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Rng_Disable(InstancePtr) \
        XHdcp22Rng_WriteReg((InstancePtr)->Config.BaseAddress, \
        (XHDCP22_RNG_REG_CTRL_CLR_OFFSET), (XHDCP22_RNG_REG_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro returns the enabled state of HDCP22 RNG.
* for the HDCP22 RNG peripheral.
* @param  InstancePtr is a pointer to the HDCP22 RNG core instance.
*
* @return TRUE if HDCP22 RNG is enabled, FALSE otherwise.
*
* @note   C-style signature:
*         u32 XHdcp22Rng_IsEnabled(u32 BaseAddress)
*
******************************************************************************/
#define XHdcp22Rng_IsEnabled(InstancePtr) \
        ((XHdcp22Rng_GetControlReg((InstancePtr)->Config.BaseAddress)\
        & XHDCP22_RNG_REG_CTRL_RUN_MASK) ==  XHDCP22_RNG_REG_CTRL_RUN_MASK)


/************************** Function Prototypes ******************************/
/* Initialization function in xhdcp22_rng_sinit.c */
XHdcp22_Rng_Config *XHdcp22Rng_LookupConfig(u16 DeviceId);

/* Initialization and control functions in xhdcp22_rng.c */
int XHdcp22Rng_CfgInitialize(XHdcp22_Rng *InstancePtr, XHdcp22_Rng_Config *CfgPtr, UINTPTR EffectiveAddr);

/* Return a random number */
void XHdcp22Rng_GetRandom(XHdcp22_Rng *InstancePtr, u8 *BufferPtr, u16 BufferLength, u16 RandomLength);

/************************** Variable Declarations ****************************/

#ifdef __cplusplus
}
#endif

#endif /* XHDCP22_RNG_H */

/** @} */

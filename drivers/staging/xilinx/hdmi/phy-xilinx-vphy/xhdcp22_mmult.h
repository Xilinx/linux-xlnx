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
* @file xhdcp22_mmult.h
* @addtogroup hdcp22_mmult_v1_1
* @{
* @details
*
* This is the main header file for the Xilinx HDCP 2.2 Montgomery Multipler
* device driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  MH     12/07/15 Initial release.
* 1.01  MH     08/04/16 Added 64 bit address support.
* </pre>
*
******************************************************************************/

#ifndef XHDCP22_MMULT_H
#define XHDCP22_MMULT_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#if (defined(__KERNEL__) || (!defined(__linux__)))
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_io.h"
#else
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
//#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#endif
#include "xhdcp22_mmult_hw.h"

/**************************** Type Definitions ******************************/
#if (defined(__linux__) && (!defined(__KERNEL__)))
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#else
typedef struct {
    u16 DeviceId;
    UINTPTR BaseAddress;
} XHdcp22_mmult_Config;
#endif

typedef struct {
	XHdcp22_mmult_Config Config;
    u32 IsReady;
} XHdcp22_mmult;

/***************** Macros (Inline Functions) Definitions *********************/
#if (defined(__KERNEL__) || (!defined(__linux__)))
#define XHdcp22_mmult_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XHdcp22_mmult_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XHdcp22_mmult_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XHdcp22_mmult_ReadReg(BaseAddress, RegOffset) \
    *(volatile u32*)((BaseAddress) + (RegOffset))

#define Xil_AssertVoid(expr)    assert(expr)
#define Xil_AssertNonvoid(expr) assert(expr)

#define XST_SUCCESS             0
#define XST_DEVICE_NOT_FOUND    2
#define XST_OPEN_DEVICE_FAILED  3
#define XIL_COMPONENT_IS_READY  1
#endif

/************************** Function Prototypes *****************************/
#if (defined(__KERNEL__) || (!defined(__linux__)))
int XHdcp22_mmult_Initialize(XHdcp22_mmult *InstancePtr, u16 DeviceId);
XHdcp22_mmult_Config* XHdcp22_mmult_LookupConfig(u16 DeviceId);
int XHdcp22_mmult_CfgInitialize(XHdcp22_mmult *InstancePtr, XHdcp22_mmult_Config *ConfigPtr, UINTPTR EffectiveAddr);
#else
int XHdcp22_mmult_Initialize(XHdcp22_mmult *InstancePtr, const char* InstanceName);
int XHdcp22_mmult_Release(XHdcp22_mmult *InstancePtr);
#endif

void XHdcp22_mmult_Start(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_IsDone(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_IsIdle(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_IsReady(XHdcp22_mmult *InstancePtr);
void XHdcp22_mmult_EnableAutoRestart(XHdcp22_mmult *InstancePtr);
void XHdcp22_mmult_DisableAutoRestart(XHdcp22_mmult *InstancePtr);

u32 XHdcp22_mmult_Get_U_BaseAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_U_HighAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_U_TotalBytes(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_U_BitWidth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_U_Depth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Write_U_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Read_U_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Write_U_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Read_U_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Get_A_BaseAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_A_HighAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_A_TotalBytes(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_A_BitWidth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_A_Depth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Write_A_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Read_A_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Write_A_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Read_A_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Get_B_BaseAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_B_HighAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_B_TotalBytes(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_B_BitWidth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_B_Depth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Write_B_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Read_B_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Write_B_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Read_B_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Get_N_BaseAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_N_HighAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_N_TotalBytes(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_N_BitWidth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_N_Depth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Write_N_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Read_N_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Write_N_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Read_N_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Get_NPrime_BaseAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_NPrime_HighAddress(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_NPrime_TotalBytes(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_NPrime_BitWidth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Get_NPrime_Depth(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_Write_NPrime_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Read_NPrime_Words(XHdcp22_mmult *InstancePtr, int offset, u32 *data, int length);
u32 XHdcp22_mmult_Write_NPrime_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);
u32 XHdcp22_mmult_Read_NPrime_Bytes(XHdcp22_mmult *InstancePtr, int offset, u8 *data, int length);

void XHdcp22_mmult_InterruptGlobalEnable(XHdcp22_mmult *InstancePtr);
void XHdcp22_mmult_InterruptGlobalDisable(XHdcp22_mmult *InstancePtr);
void XHdcp22_mmult_InterruptEnable(XHdcp22_mmult *InstancePtr, u32 Mask);
void XHdcp22_mmult_InterruptDisable(XHdcp22_mmult *InstancePtr, u32 Mask);
void XHdcp22_mmult_InterruptClear(XHdcp22_mmult *InstancePtr, u32 Mask);
u32 XHdcp22_mmult_InterruptGetEnabled(XHdcp22_mmult *InstancePtr);
u32 XHdcp22_mmult_InterruptGetStatus(XHdcp22_mmult *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif

/** @} */

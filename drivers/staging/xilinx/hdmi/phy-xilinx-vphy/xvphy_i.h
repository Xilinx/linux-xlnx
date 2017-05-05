/*******************************************************************************
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
*******************************************************************************/
/******************************************************************************/
/**
 *
 * @file xvphy_i.h
 *
 * Contains generic APIs that are locally called or used within the
 * VPHY driver.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   gm,  11/09/16 Initial release.
 * 1.4   gm   11/24/16 Made debug log optional (can be disabled via makefile)
 * </pre>
 *
*******************************************************************************/

#ifndef XVPHY_I_H_
/* Prevent circular inclusions by using protection macros. */
#define XVPHY_I_H_

/******************************* Include Files ********************************/

#include "xil_assert.h"
#include "xvphy.h"
#include "xvphy_hw.h"
#include "xvidc.h"
//#include "xvphy_dp.h"

/****************************** Type Definitions ******************************/


/**************************** Function Prototypes *****************************/


void XVphy_Ch2Ids(XVphy *InstancePtr, XVphy_ChannelId ChId,
		u8 *Id0, u8 *Id1);
XVphy_SysClkDataSelType Pll2SysClkData(XVphy_PllType PllSelect);
XVphy_SysClkOutSelType Pll2SysClkOut(XVphy_PllType PllSelect);
u32 XVphy_PllCalculator(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, XVphy_DirectionType Dir,
		u32 PllClkInFreqHz);

/* xvphy.c: Voltage swing and preemphasis. */
void XVphy_SetRxLpm(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u8 Enable);
void XVphy_SetTxVoltageSwing(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, u8 Vs);
void XVphy_SetTxPreEmphasis(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		u8 Pe);

/* xvphy.c: Channel configuration functions - setters. */
u32 XVphy_WriteCfgRefClkSelReg(XVphy *InstancePtr, u8 QuadId);
void XVphy_CfgPllRefClkSel(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_PllRefClkSelType RefClkSel);
void XVphy_CfgSysClkDataSel(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_SysClkDataSelType SysClkDataSel);
void XVphy_CfgSysClkOutSel(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_SysClkOutSelType SysClkOutSel);

u32 XVphy_ClkCalcParams(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u32 PllClkInFreqHz);
u32 XVphy_OutDivReconfig(XVphy *InstancePtr, u8 QuadId,
				XVphy_ChannelId ChId, XVphy_DirectionType Dir);
u32 XVphy_DirReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir);
u32 XVphy_ClkReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId);

/* xvphy.c: Channel configuration functions - getters. */
XVphy_ChannelId XVphy_GetRcfgChId(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_PllType PllType);
u32 XVphy_GetQuadRefClkFreq(XVphy *InstancePtr, u8 QuadId,
		XVphy_PllRefClkSelType RefClkType);
XVphy_SysClkDataSelType XVphy_GetSysClkDataSel(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_ChannelId ChId);
XVphy_SysClkOutSelType XVphy_GetSysClkOutSel(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_ChannelId ChId);
u32 XVphy_IsPllLocked(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId);
u32 XVphy_GtUserRdyEnable(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u8 Hold);

/* xvphy.c: GT/MMCM DRP access. */
void XVphy_MmcmReset(XVphy *InstancePtr, u8 QuadId, XVphy_DirectionType Dir,
		u8 Hold);
void XVphy_MmcmLockedMaskEnable(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, u8 Enable);
void XVphy_SetBufgGtDiv(XVphy *InstancePtr, XVphy_DirectionType Dir, u8 Div);
/* xvphy.c Miscellaneous control. */
u32 XVphy_PowerDownGtPll(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		u8 Hold);


/* xvphy_intr.c: Interrupt handling functions. */
void XVphy_SetIntrHandler(XVphy *InstancePtr, XVphy_IntrHandlerType HandlerType,
		XVphy_IntrHandler CallbackFunc, void *CallbackRef);
void XVphy_IntrEnable(XVphy *InstancePtr, XVphy_IntrHandlerType Intr);
void XVphy_IntrDisable(XVphy *InstancePtr, XVphy_IntrHandlerType Intr);
void XVphy_CfgErrIntr(XVphy *InstancePtr, XVphy_ErrIrqType ErrIrq, u8 Set);

u64 XVphy_GetPllVcoFreqHz(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, XVphy_DirectionType Dir);

/******************* Macros (Inline Functions) Definitions ********************/

#endif /* XVPHY_I_H_ */

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
* @file xhdcp1x.c
* @addtogroup hdcp1x_v4_0
* @{
*
* This contains the implementation of the HDCP state machine module
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 2.00  als    09/30/15 Added EffectiveAddr argument to
*                       XHdcp1x_CfgInitialize.
* 2.10  MG     01/18/16 Added function XHdcp1x_IsEnabled.
* 2.20  MG     01/20/16 Added function XHdcp1x_GetHdcpCallback.
* 2.30  MG     02/25/16 Added function XHdcp1x_SetCallback and authenticated
*                       callback.
* 3.0   yas    02/13/16 Upgraded to support HDCP Repeater functionality.
*                       Added functions:
*                       XHdcp1x_DownstreamReady, XHdcp1x_GetRepeaterInfo,
*                       XHdcp1x_SetCallBack, XHdcp1x_ReadDownstream
* 4.0   yas    07/30/16 Addded function:
*                       XHdcp1x_SetRepeater, XHdcp1x_IsInComputations,
*                       XHdcp1x_IsInWaitforready, XHdcp1x_IsDwnstrmCapable,
*                       XHdcp1x_GetTopology, XHdcp1x_DisableBlank,
*                       XHdcp1x_EnableBlank, XHdcp1x_GetTopologyKSVList,
*                       XHdcp1x_GetTopologyBKSV, XHdcp1x_SetTopologyField,
*                       Hdcp1x_GetTopologyField, XHdcp1x_IsRepeater,
*                       XHdcp1x_SetTopology, XHdcp1x_SetTopologyKSVList,
*                       XHdcp1x_SetTopologyUpdate.
* 4.0   yas    08/16/16 Used UINTPTR instead of u32 for BaseAddress
*                       XHdcp1x_CfgInitialize
* 4.1   yas    11/10/16 Added function XHdcp1x_SetHdmiMode.
* 4.1   yas    08/03/17 Updated the initialization to memset the XHdcp1x
*                       structure to 0.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

//#include <stdlib.h>
#include <linux/string.h>
#include "xhdcp1x.h"
#include "xhdcp1x_cipher.h"
#include "xhdcp1x_debug.h"
#include "xhdcp1x_port.h"
#include "xhdcp1x_rx.h"
#include "xhdcp1x_tx.h"
#include "xil_types.h"
#include "xstatus.h"

/************************** Constant Definitions *****************************/

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
#define INCLUDE_TX
#endif
#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)
#define INCLUDE_RX
#endif
#if defined(XPAR_XDP_NUM_INSTANCES) && (XPAR_XDP_NUM_INSTANCES > 0)
#define INCLUDE_RX
#define INCLUDE_TX
#endif

/**
 * This defines the version of the software driver
 */
#define	DRIVER_VERSION			(0x00010023ul)

/**************************** Type Definitions *******************************/

/************************** Extern Declarations ******************************/

/************************** Global Declarations ******************************/

XHdcp1x_LogMsg XHdcp1xDebugLogMsg = NULL;	/**< Instance of function
						  *  interface used for debug
						  *  log message statement */
XHdcp1x_KsvRevokeCheck XHdcp1xKsvRevokeCheck = NULL; /**< Instance of function
						       *  interface used for
						       *  checking a specific
						       *  KSV against the
						       *  platforms revocation
						       *  list */

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
* This function retrieves the configuration for this HDCP instance and fills
* in the InstancePtr->Config structure.
*
* @param	InstancePtr is the device whose adaptor is to be determined.
* @param	CfgPtr is the configuration of the instance.
* @param	PhyIfPtr is pointer to the underlying physical interface.
* @param	EffectiveAddr is the device base address in the virtual
*		memory space. If the address translation is not used,
*		then the physical address is passed.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CfgInitialize(XHdcp1x *InstancePtr, const XHdcp1x_Config *CfgPtr,
		void *PhyIfPtr, UINTPTR EffectiveAddr)
{
	int Status;
	u32 RegVal;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(CfgPtr != NULL);
	Xil_AssertNonvoid(EffectiveAddr != (UINTPTR)NULL);

	/* clear instance */
	memset(InstancePtr, 0, sizeof(XHdcp1x));
	
	/* Initialize InstancePtr. */
	InstancePtr->Config = *CfgPtr;
	InstancePtr->Config.BaseAddress = EffectiveAddr;
	InstancePtr->Port.PhyIfPtr = PhyIfPtr;

	/* Update IsRx. */
	RegVal = XHdcp1x_ReadReg(EffectiveAddr, XHDCP1X_CIPHER_REG_TYPE);
	RegVal &= XHDCP1X_CIPHER_BITMASK_TYPE_DIRECTION;
	InstancePtr->Config.IsRx = (RegVal ==
		XHDCP1X_CIPHER_VALUE_TYPE_DIRECTION_RX) ? TRUE : FALSE;

	/* Update IsHDMI. */
	RegVal = XHdcp1x_ReadReg(EffectiveAddr, XHDCP1X_CIPHER_REG_TYPE);
	RegVal &= XHDCP1X_CIPHER_BITMASK_TYPE_PROTOCOL;
	InstancePtr->Config.IsHDMI = (RegVal ==
		XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_HDMI) ? TRUE : FALSE;

	InstancePtr->Port.Adaptor = XHdcp1x_PortDetermineAdaptor(InstancePtr);

	/* Ensure existence of an adaptor initialization function. */
	if (!InstancePtr->Port.Adaptor || !InstancePtr->Port.Adaptor->Init) {
		return (XST_NO_FEATURE);
	}
	/* Invoke adaptor initialization function. */
	Status = (*(InstancePtr->Port.Adaptor->Init))(InstancePtr);
	if (Status != XST_SUCCESS) {
		return (Status);
	}

	/* Initialize the cipher core. */
	XHdcp1x_CipherInit(InstancePtr);
	/* Initialize the transmitter/receiver state machine. */
	if (!InstancePtr->Config.IsRx) {
		/* Set all handlers to stub values, let user configure this
		 * data later. If any new callbacks are added to the HDCP
		 * RX interface they should be initialized to null. */
		InstancePtr->Tx.AuthenticatedCallback = NULL;
		InstancePtr->Tx.IsAuthenticatedCallbackSet = (FALSE);

		InstancePtr->Tx.DdcRead = NULL;
		InstancePtr->Tx.IsDdcReadSet = (FALSE);

		InstancePtr->Tx.DdcWrite = NULL;
		InstancePtr->Tx.IsDdcWriteSet = (FALSE);

		InstancePtr->Tx.RepeaterExchangeCallback = NULL;
		InstancePtr->Tx.IsRepeaterExchangeCallbackSet = (FALSE);

		InstancePtr->Tx.UnauthenticatedCallback = NULL;
		InstancePtr->Tx.IsUnauthenticatedCallbackSet = (FALSE);

		/* Initialize TX */
		XHdcp1x_TxInit(InstancePtr);
	}
	else {
		/* Set all the receiver handlers to stub value, let
		 * user configure this data later. If any new callbacks
		 * are added to the HDCP TX interface they should be
		 * initialized to null. */
		InstancePtr->Rx.DdcSetAddressCallback = NULL;
		InstancePtr->Rx.IsDdcSetAddressCallbackSet = (FALSE);

		InstancePtr->Rx.DdcSetDataCallback = NULL;
		InstancePtr->Rx.IsDdcSetDataCallbackSet = (FALSE);

		InstancePtr->Rx.DdcGetDataCallback = NULL;
		InstancePtr->Rx.IsDdcGetDataCallbackSet = (FALSE);

		InstancePtr->Rx.RepeaterDownstreamAuthCallback = NULL;
		InstancePtr->Rx.IsRepeaterDownstreamAuthCallbackSet = (FALSE);

		InstancePtr->Rx.AuthenticatedCallback = NULL;
		InstancePtr->Rx.IsAuthenticatedCallbackSet = (FALSE);

		InstancePtr->Rx.UnauthenticatedCallback = NULL;
		InstancePtr->Rx.IsUnauthenticatedCallbackSet = (FALSE);

		InstancePtr->Rx.TopologyUpdateCallback = NULL;
		InstancePtr->Rx.IsTopologyUpdateCallbackSet = (FALSE);

		InstancePtr->Rx.EncryptionUpdateCallback = NULL;
		InstancePtr->Rx.IsEncryptionUpdateCallbackSet = (FALSE);

		/* Initialize RX */
		XHdcp1x_RxInit(InstancePtr);
	}
	
	InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function polls an HDCP interface.
*
* @param	InstancePtr is the interface to poll.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_Poll(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxPoll(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxPoll(InstancePtr);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function posts a DOWNSTREAMREADY event to an HDCP interface.
*
* @param 	InstancePtr is the interface to reset.
*
* @return
*		- XST_SUCCESS if successful.
* 		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_DownstreamReady(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		/* No downstreamready for TX */
		Status = XST_FAILURE;
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxDownstreamReady(InstancePtr);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function copies the V'H0, V'H1, V'H2, V'H3, V'H4, KSVList and BInfo
* values in the HDCP RX HDCP Instance for Repeater validation .
*
* @param	InstancePtr is the receiver instance.
* @param	RepeaterInfoPtr is the Repeater information in the transmitter
* 		instance.
*
* @return
*		- XST_SUCCESS if successful.
* 		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_GetRepeaterInfo(XHdcp1x *InstancePtr,
		XHdcp1x_RepeaterExchange *RepeaterInfoPtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(RepeaterInfoPtr != NULL);

#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxGetRepeaterInfo(InstancePtr,
			RepeaterInfoPtr);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return Status;
}


/*****************************************************************************/
/**
* This function sets the Repeater functionality for an HDCP interface.
*
* @param	InstancePtr is the interface to disable.
* @param	State is etiher TRUE or FALSE
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_SetRepeater(XHdcp1x *InstancePtr, u8 State)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	InstancePtr->IsRepeater = State;

#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxSetRepeaterBcaps(InstancePtr,
			State);
	}
#endif

	return (Status);
}

/*****************************************************************************/
/**
* This function resets an HDCP interface.
*
* @param 	InstancePtr is the interface to reset.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_Reset(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxReset(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxReset(InstancePtr);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function enables an HDCP interface.
*
* @param	InstancePtr is the interface to enable.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_Enable(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxEnable(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxEnable(InstancePtr);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function disables an HDCP interface.
*
* @param	InstancePtr is the interface to disable.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_Disable(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxDisable(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxDisable(InstancePtr);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function updates the state of the underlying physical interface.
*
* @param	InstancePtr is the interface to update.
* @param	IsUp indicates the state of the underlying physical interface.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_SetPhysicalState(XHdcp1x *InstancePtr, int IsUp)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxSetPhysicalState(InstancePtr, IsUp);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxSetPhysicalState(InstancePtr, IsUp);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function sets the lane count of a hdcp interface
*
* @param	InstancePtr is the interface to update.
* @param	LaneCount is the lane count.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_SetLaneCount(XHdcp1x *InstancePtr, int LaneCount)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if ((!InstancePtr->Config.IsRx) && (!InstancePtr->Config.IsHDMI)) {
		Status = XHdcp1x_TxSetLaneCount(InstancePtr, LaneCount);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if ((InstancePtr->Config.IsRx) && (!InstancePtr->Config.IsHDMI)) {
		Status = XHdcp1x_RxSetLaneCount(InstancePtr, LaneCount);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function initiates authentication of an HDCP interface.
*
* @param	InstancePtr is the interface to initiate authentication on.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_Authenticate(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxAuthenticate(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxAuthenticate(InstancePtr);
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function initiates downstream read of READY bit and consequently the
* second part of Repeater authentication.
*
* @param	InstancePtr is the interface to initiate authentication on.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_ReadDownstream(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxReadDownstream(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		/* Does nothing for Rx state machine.*/
	}
	else
#endif
	{
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function queries an interface to determine if authentication is in
* progress.
*
* @param	InstancePtr is the interface to query.
*
* @return	Truth value indicating authentication in progress (TRUE)
*		or not (FALSE).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_IsInProgress(const XHdcp1x *InstancePtr)
{
	int IsInProgress = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		IsInProgress = XHdcp1x_TxIsInProgress(InstancePtr);
	}
#endif

	return (IsInProgress);
}

/*****************************************************************************/
/**
* This function queries an interface to determine if it has successfully
* completed authentication.
*
* @param	InstancePtr is the interface to query.
*
* @return	Truth value indicating authenticated (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_IsAuthenticated(const XHdcp1x *InstancePtr)
{
	int IsAuth = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		IsAuth = XHdcp1x_TxIsAuthenticated(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		IsAuth = XHdcp1x_RxIsAuthenticated(InstancePtr);
	}
	else
#endif
	{
		IsAuth = FALSE;
	}

	return (IsAuth);
}

/*****************************************************************************/
/**
* This function queries an interface to determine if it is in the state of
* computations or not.
*
* @param	InstancePtr is the interface to query.
*
* @return	Truth value indicating authenticated (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_IsInComputations(const XHdcp1x *InstancePtr)
{
	int IsInComp = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		IsInComp = XHdcp1x_TxIsInComputations(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		IsInComp = XHdcp1x_RxIsInComputations(InstancePtr);
	}
	else
#endif
	{
		IsInComp = FALSE;
	}

	return (IsInComp);
}

/*****************************************************************************/
/**
* This function queries an interface to determine if it is in the
* wait-for-ready state or not.
*
* @param	InstancePtr is the interface to query.
*
* @return	Truth value indicating authenticated (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_IsInWaitforready(const XHdcp1x *InstancePtr)
{
	int IsInWfr = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		IsInWfr = XHdcp1x_TxIsInWaitforready(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		IsInWfr = XHdcp1x_RxIsInWaitforready(InstancePtr);
	}
	else
#endif
	{
		IsInWfr = FALSE;
	}

	return (IsInWfr);
}

/*****************************************************************************/
/**
* This function queries the device connected to the downstream interface to
* determine if it supports hdcp or not.
*
* @param	InstancePtr is the interface to query.
*
* @return	Truth value indicating HDCP capability (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_IsDwnstrmCapable(const XHdcp1x *InstancePtr)
{
	int IsCapable = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		IsCapable = XHdcp1x_TxIsDownstrmCapable(InstancePtr);
	}
#endif

	return (IsCapable);
}

/*****************************************************************************/
/**
* This function queries an interface to determine if it is enabled.
*
* @param	InstancePtr is the interface to query.
*
* @return	Truth value indicating enabled (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_IsEnabled(const XHdcp1x *InstancePtr)
{
	int IsEnabled = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		IsEnabled = XHdcp1x_TxIsEnabled(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		IsEnabled = XHdcp1x_RxIsEnabled(InstancePtr);
	}
	else
#endif
	{
		IsEnabled = FALSE;
	}

	return (IsEnabled);
}

/*****************************************************************************/
/**
* This function retrieves the current encryption map of the video streams
* traversing an hdcp interface.
*
* @param 	InstancePtr is the interface to query.
*
* @return	The current encryption map.
*
* @note		None.
*
******************************************************************************/
u64 XHdcp1x_GetEncryption(const XHdcp1x *InstancePtr)
{
	u64 StreamMap = 0;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		StreamMap = XHdcp1x_TxGetEncryption(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		StreamMap = XHdcp1x_RxGetEncryption(InstancePtr);
	}
	else
#endif
	{
		StreamMap = 0;
	}

	return (StreamMap);
}

/*****************************************************************************/
/**
* This function determines if the video stream is encrypted.
* The traffic is encrypted if the encryption bit map is non-zero and the
* interface is authenticated.
*
* @param	InstancePtr is a pointer to the HDCP instance.
*
* @return	Truth value indicating encrypted (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_IsEncrypted(const XHdcp1x *InstancePtr)
{
	return (XHdcp1x_GetEncryption(InstancePtr) &&
		XHdcp1x_IsAuthenticated(InstancePtr));
}

/*****************************************************************************/
/**
* This function enables encryption on a series of streams within an HDCP
* interface.
*
* @param	InstancePtr is the interface to configure.
* @param	Map is the stream map to enable encryption on.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_EnableEncryption(XHdcp1x *InstancePtr, u64 Map)
{
	int Status = XST_FAILURE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxEnableEncryption(InstancePtr, Map);
	}
#else
	UNUSED(Map);
#endif

	return (Status);
}

/*****************************************************************************/
/**
* This function disables encryption on a series of streams within an HDCP
* interface.
*
* @param	InstancePtr is the interface to configure.
* @param	Map is the stream map to disable encryption on.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_DisableEncryption(XHdcp1x *InstancePtr, u64 Map)
{
	int Status = XST_FAILURE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxDisableEncryption(InstancePtr, Map);
	}
#else
	UNUSED(Map);
#endif

	return (Status);
}

/*****************************************************************************/
/**
* This function sets the key selection vector that is to be used by the HDCP
* cipher.
*
* @param	InstancePtr is the interface to configure.
* @param	KeySelect is the key selection vector.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_SetKeySelect(XHdcp1x *InstancePtr, u8 KeySelect)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	Status = XHdcp1x_CipherSetKeySelect(InstancePtr, KeySelect);

	return (Status);
}

/*****************************************************************************/
/**
* This function handles a timeout on an HDCP interface.
*
* @param	InstancePtr is the interface.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_HandleTimeout(void *InstancePtr)
{
	XHdcp1x *HdcpPtr = InstancePtr;

	/* Verify argument. */
	Xil_AssertVoid(HdcpPtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!HdcpPtr->Config.IsRx) {
		XHdcp1x_TxHandleTimeout(HdcpPtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	if (HdcpPtr->Config.IsRx) {
		XHdcp1x_RxHandleTimeout(HdcpPtr);
	}
	else
#endif
	{
		XDEBUG_PRINTF("unknown interface type\r\n");
	}
}

#if 0
void XHdcp1x_DebugBufPrintf(const char *fmt, ...)
{
	if(XHdcp1xDebugBuff != NULL)
	{
		va_list args;
		va_start(args, fmt);
		*XHdcp1xDebugBuffPos += vscnprintf(XHdcp1xDebugBuff + *XHdcp1xDebugBuffPos,
				XHdcp1xDebugBuffSize - *XHdcp1xDebugBuffPos, fmt, args);
		va_end(args);
	}
}

/*****************************************************************************/
/**
* This function sets the debug printf function for the module to print to the
* supplied buffer.
*
* @param	buff is the buffer to print to.
* @param	buff_size is the maximum size of the buffer
* @param	buff_pos is the current (and will be updated) position in the buffer
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_SetDebugBufPrintf(char *buff, int buff_size, int *buff_pos)
{
	if(buff)
	{
		XHdcp1xDebugBuff = buff;
		XHdcp1xDebugBuffSize = buff_size;
		XHdcp1xDebugBuffPos = buff_pos;
		XHdcp1x_SetDebugPrintf(XHdcp1x_DebugBufPrintf);
	} else {
		XHdcp1x_SetDebugPrintf(NULL);
		XHdcp1xDebugBuff = NULL;
		XHdcp1xDebugBuffSize = 0;
		XHdcp1xDebugBuffPos = NULL;
	}
}
#endif

/*****************************************************************************/
/**
* This function sets the debug log message function for the module.
*
* @param	LogFunc is the debug logging function.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_SetDebugLogMsg(XHdcp1x_LogMsg LogFunc)
{
	XHdcp1xDebugLogMsg = LogFunc;
}

/*****************************************************************************/
/**
* This function sets the KSV revocation list check function for the module.
*
* @param	RevokeCheckFunc is the KSV revocation list check function.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_SetKsvRevokeCheck(XHdcp1x_KsvRevokeCheck RevokeCheckFunc)
{
	XHdcp1xKsvRevokeCheck = RevokeCheckFunc;
}

/*****************************************************************************/
/**
* This function sets timer start function for the module.
*
* @param	InstancePtr is the pointer to the HDCP interface.
* @param	TimerStartFunc is the timer start function.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_SetTimerStart(XHdcp1x *InstancePtr, XHdcp1x_TimerStart TimerStartFunc)
{
	/* Verify Argument */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->XHdcp1xTimerStart = TimerStartFunc;
}

/*****************************************************************************/
/**
* This function sets timer stop function for the module.
*
* @param	InstancePtr is the pointer to the HDCP interface.
* @param	TimerStopFunc is the timer stop function.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_SetTimerStop(XHdcp1x *InstancePtr, XHdcp1x_TimerStop TimerStopFunc)
{
	/* Verify Argument */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->XHdcp1xTimerStop = TimerStopFunc;
}

/*****************************************************************************/
/**
* This function sets timer busy delay function for the module.
*
* @param	InstancePtr is the pointer to the HDCP interface.
* @param	TimerDelayFunc is the timer busy delay function.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_SetTimerDelay(XHdcp1x *InstancePtr, XHdcp1x_TimerDelay TimerDelayFunc)
{
	/* Verify Argument */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->XHdcp1xTimerDelay = TimerDelayFunc;
}

/*****************************************************************************/
/**
* This function retrieves the version of the HDCP driver software.
*
* @return	The software driver version.
*
* @note		None.
*
******************************************************************************/
u32 XHdcp1x_GetDriverVersion(void)
{
	return (DRIVER_VERSION);
}

/*****************************************************************************/
/**
* This function retrieves the cipher version of an HDCP interface.
*
* @param	InstancePtr is the interface to query.
*
* @return	The cipher version used by the interface
*
* @note		None.
*
******************************************************************************/
u32 XHdcp1x_GetVersion(const XHdcp1x *InstancePtr)
{
	u32 Version = 0;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	Version = XHdcp1x_CipherGetVersion(InstancePtr);

	return (Version);
}

/*****************************************************************************/
/**
* This function performs a debug display of an HDCP instance.
*
* @param	InstancePtr is the interface to display.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_Info(const XHdcp1x *InstancePtr)
{
	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		XHdcp1x_TxInfo(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		XHdcp1x_RxInfo(InstancePtr);
	}
	else
#endif
	{
		XDEBUG_PRINTF("unknown interface type\r\n");
	}
}

/*****************************************************************************/
/**
* This function processes the AKsv.
*
* @param	InstancePtr is the interface to display.
*
* @return	None
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_ProcessAKsv(XHdcp1x *InstancePtr)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);

#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		InstancePtr->Port.Adaptor->CallbackHandler(InstancePtr);
	}
#endif
}

/*****************************************************************************/
/**
*
* This function returns a pointer to the downstream Topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return   A pointer to the XHdcp14 Topology structure or NULL when
*           the topology info is invalid.
*
* @note     None.
*
******************************************************************************/
void *XHdcp1x_GetTopology(XHdcp1x *InstancePtr)
{
	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	void *TopologyPtr = NULL;

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		TopologyPtr = XHdcp1x_TxGetTopology(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		/* Not currently applicable */
	}
	else
#endif
	{
		XDEBUG_PRINTF("unknown interface type\r\n");
	}

	return TopologyPtr;
}

/*****************************************************************************/
/**
*
* This function disables the blank output for the cipher.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XHdcp1x_DisableBlank(XHdcp1x *InstancePtr)
{
	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		XHdcp1x_TxDisableBlank(InstancePtr);
	}
#endif
}

/*****************************************************************************/
/**
*
* This function enables the blank output for the cipher.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XHdcp1x_EnableBlank(XHdcp1x *InstancePtr)
{
	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		XHdcp1x_TxEnableBlank(InstancePtr);
	}
#endif
}

/*****************************************************************************/
/**
*
* This function returns the value of KSV List read in the downstream interface
* of the repeater topology.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return
*           - List of KSVs of the downstream devices
*
* @note     None.
*
******************************************************************************/
u8 *XHdcp1x_GetTopologyKSVList(XHdcp1x *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u8 *KSVList = NULL;

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		KSVList = XHdcp1x_TxGetTopologyKSVList(InstancePtr);
	}
#endif

	return (KSVList);
}

/*****************************************************************************/
/**
*
* This function returns the value of KSV of the device attached to the
* downstream interface of the repeater.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return
*           - TRUE if MAX_DEPTH_EXCEEDED
*
* @note     None.
*
******************************************************************************/
u8 *XHdcp1x_GetTopologyBKSV(XHdcp1x *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u8 *BKSV = NULL;

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		BKSV = XHdcp1x_TxGetTopologyBKSV(InstancePtr);
	}
#endif

	return (BKSV);
}

/*****************************************************************************/
/**
* This function is used to set various fields inside the topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp1x core instance.
* @param    Field indicates what field of the topology structure to update.
* @param    Value is the value assigned to the field of the topology structure.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_SetTopologyField(XHdcp1x *InstancePtr,
		XHdcp1x_TopologyField Field, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Field < XHDCP1X_TOPOLOGY_INVALID);

	switch(Field)
	{
	case XHDCP1X_TOPOLOGY_DEPTH :
		XHdcp1x_RxSetTopologyDepth(InstancePtr, Value);
		break;
	case XHDCP1X_TOPOLOGY_DEVICECNT :
		XHdcp1x_RxSetTopologyDeviceCnt(InstancePtr, Value);
		break;
	case XHDCP1X_TOPOLOGY_MAXDEVSEXCEEDED :
		XHdcp1x_RxSetTopologyMaxDevsExceeded(InstancePtr, Value);
		break;
	case XHDCP1X_TOPOLOGY_MAXCASCADEEXCEEDED :
		XHdcp1x_RxSetTopologyMaxCascadeExceeded(InstancePtr, Value);
		break;
	case XHDCP1X_TOPOLOGY_HDCP20REPEATERDOWNSTREAM :
		/* Not currently applicable */
		break;
	case XHDCP1X_TOPOLOGY_HDCP1DEVICEDOWNSTREAM :
		/* Not currently applicable */
		break;
	case XHDCP1X_TOPOLOGY_INVALID :
		/* Not currently applicable */
		break;
	}
}

/*****************************************************************************/
/**
* This function is used to get various fields inside the topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp1x core instance.
* @param    Field indicates what field of the topology structure to update.
*
* @return   None.
*
* @note     None.
******************************************************************************/
u32 XHdcp1x_GetTopologyField(XHdcp1x *InstancePtr, XHdcp1x_TopologyField Field)
{
	u32 Value = 0;

	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Field < XHDCP1X_TOPOLOGY_INVALID);

	switch(Field)
	{
	case XHDCP1X_TOPOLOGY_DEPTH :
		Value = XHdcp1x_TxGetTopologyDepth(InstancePtr);
		break;
	case XHDCP1X_TOPOLOGY_DEVICECNT :
		Value = XHdcp1x_TxGetTopologyDeviceCnt(InstancePtr);
		break;
	case XHDCP1X_TOPOLOGY_MAXDEVSEXCEEDED :
		Value = XHdcp1x_TxGetTopologyMaxDevsExceeded(InstancePtr);
		break;
	case XHDCP1X_TOPOLOGY_MAXCASCADEEXCEEDED :
		Value = XHdcp1x_TxGetTopologyMaxCascadeExceeded(InstancePtr);
		break;
	case XHDCP1X_TOPOLOGY_HDCP20REPEATERDOWNSTREAM :
		/* Not currently applicable */
		break;
	case XHDCP1X_TOPOLOGY_HDCP1DEVICEDOWNSTREAM :
		/* Not currently applicable */
		break;
	case XHDCP1X_TOPOLOGY_INVALID :
		/* Not currently applicable */
		break;
	}

	return Value;
}

/*****************************************************************************/
/**
* This function return if the HDCP interface is a repeater in case of Rx or
* is connected to a repeater in case of Tx
*
* @param	InstancePtr is the transmitter instance.
*
* @return
*		- TRUE if Repeater.
*		- FALSE if not Repeater,
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_IsRepeater(XHdcp1x *InstancePtr)
{
	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u8 Status = FALSE;

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxIsRepeater(InstancePtr);
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		Status = (InstancePtr->IsRepeater)? TRUE : FALSE;
	}
	else
#endif
	{
		XDEBUG_PRINTF("unknown interface type\r\n");
	}

	return Status;
}

/*****************************************************************************/
/**
* This function sets the RepeaterInfo value int the HDCP RX instance
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
* @param    TopologyPtr is a pointer to the Repeater Info value(s).
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_SetTopology(XHdcp1x *InstancePtr,
		const XHdcp1x_RepeaterExchange *TopologyPtr)
{
	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(TopologyPtr != NULL);

#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		 XHdcp1x_RxSetTopology(InstancePtr, TopologyPtr);
	}
	else
#endif
	{
		TopologyPtr = NULL;
	}
}

/*****************************************************************************/
/**
* This function sets the KSVList value(s) in the HDCP RX KSV Fifo register
* space for the upstream interface to read.
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
* @param    ListPtr is a pointer to the KSV list.
* @param    ListSize is the number of KSVs in the list.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_SetTopologyKSVList(XHdcp1x *InstancePtr, u8 *ListPtr,
		u32 ListSize)
{
	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(ListPtr != NULL);

#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		XHdcp1x_RxSetTopologyKSVList(InstancePtr, ListPtr,	ListSize);
	}
	else
#else
	UNUSED(ListSize);
#endif
	{
		ListPtr = NULL;
	}
}

/*****************************************************************************/
/**
* This function does the necessary actions to update HDCP
* after the topology has been set.
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_SetTopologyUpdate(XHdcp1x *InstancePtr)
{
	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		/* Not currently applicable */
	}
	else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		XHdcp1x_RxSetTopologyUpdate(InstancePtr);
	}
	else
#endif
	{
		XDEBUG_PRINTF("unknown interface type\r\n");
	}
}

/*****************************************************************************/
/**
* This function set the HDMI_MODE in the BStatus register of the HDMI DDC
* space.
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
* @param	Value is the truth-value.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_SetHdmiMode(XHdcp1x *InstancePtr, u8 Value)
{
	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

#if defined(INCLUDE_TX)
	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		XHdcp1x_TxSetHdmiMode(InstancePtr, Value);
	} else
#endif
#if defined(INCLUDE_RX)
	/* Check for RX */
	if (InstancePtr->Config.IsRx) {
		XHdcp1x_RxSetHdmiMode(InstancePtr, Value);
	} else
#endif
	{
		XDEBUG_PRINTF("unknown interface type\r\n");
	}
}
/** @} */

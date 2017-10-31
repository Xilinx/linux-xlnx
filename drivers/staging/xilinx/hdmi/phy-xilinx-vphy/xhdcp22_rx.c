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
* @file xhdcp22_rx.c
* @addtogroup hdcp22_rx_v2_0
* @{
* @details
*
* This file contains the main implementation of the Xilinx HDCP 2.2 Receiver
* device driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00  MH   10/30/15 First Release
* 1.01  MH   01/15/16 Updated function XHdcp22Rx_SetDdcReauthReq to remove
*                     static. Replaced function XHdcp22Rx_SetDdcHandles
*                     with XHdcp22Rx_SetCallback. Added callback for
*                     authenticated event.
* 1.02  MH   03/02/16 Updated to perform Montgomery NPrime calcuation
*                     when XHdcp22Rx_LoadPrivateKey is called.
*                     Added function XHDCP22Rx_GetVersion.
* 1.03  MH   03/15/16 Fixed XHdcp22Rx_SetLinkError and XHdcp22Rx_SetDdcError
*                     functions to update error flag.
* 2.00  MH   04/14/16 Updated for repeater upstream support.
* 2.01  MH   02/28/17 Fixed compiler warnings.
* 2.20  MH   06/08/17 Updated for 64 bit support.
*</pre>
*
*****************************************************************************/

/***************************** Include Files ********************************/
//#include "stdio.h"
#include <linux/string.h>
#include "xstatus.h"
#include "xdebug.h"
#include "xil_printf.h"

#include "xhdcp22_rx_i.h"
#include "xhdcp22_rx.h"
#include "xtmrctr.h"

/************************** Constant Definitions ****************************/

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Variable Definitions ****************************/

/************************** Function Prototypes *****************************/
/* Functions for initializing subcores */
static int  XHdcp22Rx_InitializeCipher(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_InitializeMmult(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_InitializeRng(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_InitializeTimer(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_ComputeBaseAddress(UINTPTR BaseAddress, UINTPTR SubcoreOffset, UINTPTR *SubcoreAddressPtr);

/* Functions for generating authentication parameters */
static int  XHdcp22Rx_GenerateRrx(XHdcp22_Rx *InstancePtr, u8 *RrxPtr);

/* Functions for performing various tasks during authentication */
static u8   XHdcp22Rx_IsWriteMessageAvailable(XHdcp22_Rx *InstancePtr);
static u8   XHdcp22Rx_IsReadMessageComplete(XHdcp22_Rx *InstancePtr);
static void XHdcp22Rx_SetRxStatus(XHdcp22_Rx *InstancePtr, u16 MessageSize,
              u8 ReauthReq, u8 TopologyReady);
static void	XHdcp22Rx_SetDdcReauthReq(XHdcp22_Rx *InstancePtr);
static void XHdcp22Rx_ResetDdc(XHdcp22_Rx *InstancePtr, u8 ClrWrBuffer,
              u8 ClrRdBuffer, u8 ClrReady, u8 ClrReauthReq);
static void XHdcp22Rx_ResetAfterError(XHdcp22_Rx *InstancePtr);
static void XHdcp22Rx_ResetParams(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_PollMessage(XHdcp22_Rx *InstancePtr);
static void XHdcp22Rx_TimerHandler(void *CallbackRef, u8 TmrCntNumber);
static void XHdcp22Rx_StartTimer(XHdcp22_Rx *InstancePtr, u32 TimeOut_mSec,
              u8 ReasonId);
static void XHdcp22Rx_StopTimer(XHdcp22_Rx *InstancePtr);

/* Functions for implementing the receiver state machine */
static void *XHdcp22Rx_StateB0(XHdcp22_Rx *InstancePtr);
static void *XHdcp22Rx_StateB1(XHdcp22_Rx *InstancePtr);
static void *XHdcp22Rx_StateB2(XHdcp22_Rx *InstancePtr);
static void *XHdcp22Rx_StateB3(XHdcp22_Rx *InstancePtr);
static void *XHdcp22Rx_StateB4(XHdcp22_Rx *InstancePtr);

/* Functions for implementing the repeater upstream state machine.
   Repeater states C0-C3 are identical to receiver states B0-B3. */
static void *XHdcp22Rx_StateC4(XHdcp22_Rx *InstancePtr);
static void *XHdcp22Rx_StateC5(XHdcp22_Rx *InstancePtr);
static void *XHdcp22Rx_StateC6(XHdcp22_Rx *InstancePtr);
static void *XHdcp22Rx_StateC7(XHdcp22_Rx *InstancePtr);
static void *XHdcp22Rx_StateC8(XHdcp22_Rx *InstancePtr);

/* Functions for processing received messages */
static int  XHdcp22Rx_ProcessMessageAKEInit(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_ProcessMessageAKENoStoredKm(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_ProcessMessageAKEStoredKm(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_ProcessMessageLCInit(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_ProcessMessageSKESendEks(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_ProcessMessageRepeaterAuthSendAck(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_ProcessMessageRepeaterAuthStreamManage(XHdcp22_Rx *InstancePtr);

/* Functions for generating and sending messages */
static int  XHdcp22Rx_SendMessageAKESendCert(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_SendMessageAKESendHPrime(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_SendMessageAKESendPairingInfo(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_SendMessageLCSendLPrime(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_SendMessageRepeaterAuthSendRxIdList(XHdcp22_Rx *InstancePtr);
static int  XHdcp22Rx_SendMessageRepeaterAuthStreamReady(XHdcp22_Rx *InstancePtr);

/* Function for setting fields inside the topology structure */
static void XHdcp22Rx_SetTopologyDepth(XHdcp22_Rx *InstancePtr, u8 Depth);
static void XHdcp22Rx_SetTopologyDeviceCnt(XHdcp22_Rx *InstancePtr, u8 DeviceCnt);
static void XHdcp22Rx_SetTopologyMaxDevsExceeded(XHdcp22_Rx *InstancePtr, u8 Value);
static void XHdcp22Rx_SetTopologyMaxCascadeExceeded(XHdcp22_Rx *InstancePtr, u8 Value);
static void XHdcp22Rx_SetTopologyHdcp20RepeaterDownstream(XHdcp22_Rx *InstancePtr, u8 Value);
static void XHdcp22Rx_SetTopologyHdcp1DeviceDownstream(XHdcp22_Rx *InstancePtr, u8 Value);

/* Functions for stub callbacks */
static void XHdcp22_Rx_StubRunHandler(void *HandlerRef);
static void XHdcp22_Rx_StubSetHandler(void *HandlerRef, u32 Data);
static u32  XHdcp22_Rx_StubGetHandler(void *HandlerRef);

/****************************************************************************/
/**
* Initialize the instance provided by the caller based on the given
* configuration data.
*
* @param 	InstancePtr is a pointer to an XHdcp22_Rx instance.
*			The memory the pointer references must be pre-allocated by
*			the caller. Further calls to manipulate the driver through
*			the HDCP22-RX API must be made with this pointer.
* @param	Config is a reference to a structure containing information
*			about a specific HDCP22-RX device. This function
*			initializes an InstancePtr object for a specific device
*			specified by the contents of Config. This function can
*			initialize multiple instance objects with the use of multiple
*			calls giving different Config information on each call.
* @param	EffectiveAddr is the base address of the device. If address
*			translation is being used, then this parameter must reflect the
*			virtual base address. Otherwise, the physical address should be
*			used.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
*****************************************************************************/
int XHdcp22Rx_CfgInitialize(XHdcp22_Rx *InstancePtr, XHdcp22_Rx_Config *ConfigPtr,
      UINTPTR EffectiveAddr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(ConfigPtr != NULL);
	Xil_AssertNonvoid(EffectiveAddr != (UINTPTR)NULL);

	int Status;

	/* Clear the instance */
	memset((void *)InstancePtr, 0, sizeof(XHdcp22_Rx));

	/* Copy configuration settings */
	memcpy((void *)&(InstancePtr->Config), (const void *)ConfigPtr, sizeof(XHdcp22_Rx_Config));

	/* Set default values */
	InstancePtr->Config.BaseAddress = EffectiveAddr;
	InstancePtr->StateFunc = (XHdcp22_Rx_StateFunc)(&XHdcp22Rx_StateB0);
	InstancePtr->Info.IsEnabled = FALSE;
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_UNAUTHENTICATED;
	InstancePtr->Info.IsNoStoredKm = FALSE;
	InstancePtr->Info.ReauthReq = FALSE;
	InstancePtr->Info.TopologyReady = FALSE;
	InstancePtr->Info.IsEncrypted = FALSE;
	InstancePtr->Info.LCInitAttempts = 0;
	InstancePtr->Info.AuthRequestCnt = 0;
	InstancePtr->Info.ReauthRequestCnt = 0;
	InstancePtr->Info.LinkErrorCnt = 0;
	InstancePtr->Info.DdcErrorCnt = 0;
	InstancePtr->Info.ErrorFlag = XHDCP22_RX_ERROR_FLAG_NONE;
	InstancePtr->Info.ErrorFlagSticky = XHDCP22_RX_ERROR_FLAG_NONE;
	InstancePtr->Info.CurrentState = XHDCP22_RX_STATE_B0_WAIT_AKEINIT;
	InstancePtr->Info.NextState = XHDCP22_RX_STATE_B0_WAIT_AKEINIT;

	/* Set default repeater values */
	InstancePtr->Info.IsTopologyValid = FALSE;
	InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
	InstancePtr->Info.SeqNumV = 0;
	InstancePtr->Info.HasStreamManagementInfo = FALSE;
	InstancePtr->Info.SkipRead = FALSE;

	/* Set the callback functions to stubs */
	InstancePtr->Handles.DdcSetAddressCallback = XHdcp22_Rx_StubSetHandler;
	InstancePtr->Handles.IsDdcSetAddressCallbackSet = (FALSE);

	InstancePtr->Handles.DdcSetDataCallback = XHdcp22_Rx_StubSetHandler;
	InstancePtr->Handles.IsDdcSetDataCallbackSet = (FALSE);

	InstancePtr->Handles.DdcGetDataCallback = XHdcp22_Rx_StubGetHandler;
	InstancePtr->Handles.IsDdcGetDataCallbackSet = (FALSE);

	InstancePtr->Handles.DdcGetWriteBufferSizeCallback = XHdcp22_Rx_StubGetHandler;
	InstancePtr->Handles.IsDdcGetWriteBufferSizeCallbackSet = (FALSE);

	InstancePtr->Handles.DdcGetReadBufferSizeCallback = XHdcp22_Rx_StubGetHandler;
	InstancePtr->Handles.IsDdcGetReadBufferSizeCallbackRefSet = (FALSE);

	InstancePtr->Handles.DdcIsWriteBufferEmptyCallback = XHdcp22_Rx_StubGetHandler;
	InstancePtr->Handles.IsDdcIsWriteBufferEmptyCallbackSet = (FALSE);

	InstancePtr->Handles.DdcIsReadBufferEmptyCallback = XHdcp22_Rx_StubGetHandler;
	InstancePtr->Handles.IsDdcIsReadBufferEmptyCallbackSet = (FALSE);

	InstancePtr->Handles.DdcClearReadBufferCallback = XHdcp22_Rx_StubRunHandler;
	InstancePtr->Handles.IsDdcClearReadBufferCallbackSet = (FALSE);

	InstancePtr->Handles.DdcClearWriteBufferCallback = XHdcp22_Rx_StubRunHandler;
	InstancePtr->Handles.IsDdcClearWriteBufferCallbackSet = (FALSE);

	InstancePtr->Handles.AuthenticatedCallback = XHdcp22_Rx_StubRunHandler;
	InstancePtr->Handles.IsAuthenticatedCallbackSet = (FALSE);

	InstancePtr->Handles.AuthenticationRequestCallback = XHdcp22_Rx_StubRunHandler;
	InstancePtr->Handles.IsAuthenticationRequestCallbackSet = (FALSE);

	InstancePtr->Handles.StreamManageRequestCallback = XHdcp22_Rx_StubRunHandler;
	InstancePtr->Handles.IsStreamManageRequestCallbackSet = (FALSE);

	InstancePtr->Handles.TopologyUpdateCallback = XHdcp22_Rx_StubRunHandler;
	InstancePtr->Handles.IsTopologyUpdateCallbackSet = (FALSE);

	InstancePtr->Handles.IsDdcAllCallbacksSet = (FALSE);

	/* Set RXCAPS repeater mode */
	InstancePtr->RxCaps[0] = 0x02;
	InstancePtr->RxCaps[1] = 0x00;
	InstancePtr->RxCaps[2] = (InstancePtr->Config.Mode == XHDCP22_RX_RECEIVER) ? 0x00 : 0x01;

	/* Reset stored parameters */
	XHdcp22Rx_ResetParams(InstancePtr);

	/* Configure Cipher Instance */
	Status = XHdcp22Rx_InitializeCipher(InstancePtr);
	if(Status == XST_FAILURE)
	{
		return Status;
	}

	/* Configure Mmult Instance */
	Status = XHdcp22Rx_InitializeMmult(InstancePtr);
	if(Status == XST_FAILURE)
	{
		return Status;
	}

	/* Configure Rng Instance */
	Status = XHdcp22Rx_InitializeRng(InstancePtr);
	if(Status == XST_FAILURE)
	{
		return Status;
	}

	/* Configure Timer Instance */
	Status = XHdcp22Rx_InitializeTimer(InstancePtr);
	if(Status == XST_FAILURE)
	{
		return Status;
	}

	/* Reset log */
	XHdcp22Rx_LogReset(InstancePtr, FALSE);

	/* Indicate component has been initialized */
	InstancePtr->IsReady = (XIL_COMPONENT_IS_READY);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function resets the HDCP22-RX system to the default state.
* The HDCP22-RX DDC registers are set to their default value
* and the message buffer is reset.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		The DDC message handles must be assigned by the
* 			XHdcp22Rx_SetDdcHandles function prior to calling
* 			this reset function.
******************************************************************************/
int XHdcp22Rx_Reset(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	/* Verify DDC handles assigned */
	Xil_AssertNonvoid(InstancePtr->Handles.IsDdcAllCallbacksSet == TRUE);

	int AuthenticationStatus = InstancePtr->Info.AuthenticationStatus;

	/* Log info event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO, XHDCP22_RX_LOG_INFO_RESET);

	/* Clear message buffer */
	memset(&InstancePtr->MessageBuffer, 0, sizeof(XHdcp22_Rx_Message));
	InstancePtr->MessageSize = 0;

	/* Set default values */
	InstancePtr->StateFunc = (XHdcp22_Rx_StateFunc)(&XHdcp22Rx_StateB0);
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_UNAUTHENTICATED;
	InstancePtr->Info.IsNoStoredKm = FALSE;
	InstancePtr->Info.ReauthReq = FALSE;
	InstancePtr->Info.TopologyReady = FALSE;
	InstancePtr->Info.IsEncrypted = FALSE;
	InstancePtr->Info.LCInitAttempts = 0;
	InstancePtr->Info.AuthRequestCnt = 0;
	InstancePtr->Info.ReauthRequestCnt = 0;
	InstancePtr->Info.LinkErrorCnt = 0;
	InstancePtr->Info.DdcErrorCnt = 0;
	InstancePtr->Info.ErrorFlag = XHDCP22_RX_ERROR_FLAG_NONE;
	InstancePtr->Info.ErrorFlagSticky = XHDCP22_RX_ERROR_FLAG_NONE;
	InstancePtr->Info.CurrentState = XHDCP22_RX_STATE_B0_WAIT_AKEINIT;
	InstancePtr->Info.NextState = XHDCP22_RX_STATE_B0_WAIT_AKEINIT;

	/* Reset repeater values */
	memset(&InstancePtr->Topology, 0, sizeof(XHdcp22_Rx_Topology));
	InstancePtr->Info.IsTopologyValid = FALSE;
	InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
	InstancePtr->Info.SeqNumV = 0;
	InstancePtr->Info.HasStreamManagementInfo = FALSE;
	InstancePtr->Info.SkipRead = FALSE;

	/* Reset stored parameters */
	XHdcp22Rx_ResetParams(InstancePtr);

	/* Reset DDC registers */
	XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);

	/* Disable timer */
	XHdcp22Rx_StopTimer(InstancePtr);

	/* Disable cipher */
	XHdcp22Cipher_Disable(&InstancePtr->CipherInst);

	/* Run callback function type: XHDCP22_RX_HANDLER_UNAUTHENTICATED */
	if((InstancePtr->Handles.IsUnauthenticatedCallbackSet) &&
		 (AuthenticationStatus == XHDCP22_RX_AUTHENTICATED))
	{
		InstancePtr->Handles.UnauthenticatedCallback(InstancePtr->Handles.UnauthenticatedCallbackRef);
	}

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function enables the HDCP22-RX state machine. The HDCP2Version
* register is set to active.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		Before enabling the state machine ensure that the instance has
*			been initialized, DDC message handles have been assigned, and keys
*			have been loaded.
******************************************************************************/
int XHdcp22Rx_Enable(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	/* Verify DDC handles assigned */
	Xil_AssertNonvoid(InstancePtr->Handles.IsDdcAllCallbacksSet == TRUE);
	/* Verify keys loaded */
	Xil_AssertNonvoid(InstancePtr->PublicCertPtr != NULL);
	Xil_AssertNonvoid(InstancePtr->PrivateKeyPtr != NULL);
	/* Verify devices ready */
	Xil_AssertNonvoid(InstancePtr->MmultInst.IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertNonvoid(InstancePtr->TimerInst.IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertNonvoid(InstancePtr->RngInst.IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertNonvoid(InstancePtr->CipherInst.IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

	/* Log info event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO, XHDCP22_RX_LOG_INFO_ENABLE);

	/* Enable RNG and Cipher */
	XHdcp22Rng_Enable(&InstancePtr->RngInst);
	XHdcp22Cipher_Enable(&InstancePtr->CipherInst);

	/* Assert enabled flag */
	InstancePtr->Info.IsEnabled = TRUE;

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function disables the HDCP22-RX state machine. The HDCP2Version
* register is cleared, and the ReauthReq bit is set in the
* RxStatus register to allow the transmitter to recover when it has
* already authenticated. Without setting the ReauthReq bit the
* transmitter will not know that HDCP 2.2 protocol has been disabled
* by the Receiver after it has already authenticated.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		The HDCP22-RX cipher is disabled.
******************************************************************************/
int XHdcp22Rx_Disable(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Log info event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO, XHDCP22_RX_LOG_INFO_DISABLE);

	/* Set ReauthReq for recovery when already authenticated */
	XHdcp22Rx_SetDdcReauthReq(InstancePtr);

	/* Disable, Rng, Cipher, and Timer */
	XHdcp22Rng_Disable(&InstancePtr->RngInst);
	XHdcp22Cipher_Disable(&InstancePtr->CipherInst);
	XHdcp22Rx_StopTimer(InstancePtr);

	/* Deassert device enable flag */
	InstancePtr->Info.IsEnabled = FALSE;

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
*
* This function installs callback functions for the given
* HandlerType:
*
* <pre>
* HandlerType                                           Callback Function Type
* -------------------------                             ---------------------------
* (XHDCP22_RX_HANDLER_DDC_SETREGADDR)                   DdcSetAddressCallback
* (XHDCP22_RX_HANDLER_DDC_SETREGDATA)                   DdcSetDataCallback
* (XHDCP22_RX_HANDLER_DDC_GETREGDATA)                   DdcGetDataCallback
* (XHDCP22_RX_HANDLER_DDC_GETWBUFSIZE)                  DdcGetWriteBufferSizeCallback
* (XHDCP22_RX_HANDLER_DDC_GETRBUFSIZE)                  DdcGetReadBufferSizeCallback
* (XHDCP22_RX_HANDLER_DDC_ISWBUFEMPTY)                  DdcIsWriteBufferEmptyCallback
* (XHDCP22_RX_HANDLER_DDC_ISRBUFEMPTY)                  DdcIsReadBufferEmptyCallback
* (XHDCP22_RX_HANDLER_DDC_CLEARRBUF)                    DdcClearReadBufferCallback
* (XHDCP22_RX_HANDLER_DDC_CLEARWBUF)                    DdcClearWriteBufferCallback
* (XHDCP22_RX_HANDLER_AUTHENTICATED)                    AuthenticatedCallback
* (XHDCP22_RX_HANDLER_UNAUTHENTICATED)                  UnauthenticatedCallback
* (XHDCP22_RX_HANDLER_AUTHENTICATION_REQUEST)           AuthenticationRequestCallback
* (XHDCP22_RX_HANDLER_STREAM_MANAGE_REQUEST)            StreamManageRequestCallback
* (XHDCP22_RX_HANDLER_TOPOLOGY_UPDATE)                  TopologyUpdateCallback
* </pre>
*
* @param	InstancePtr is a pointer to the HDMI RX core instance.
* @param	HandlerType specifies the type of handler.
* @param	CallbackFunc is the address of the callback function.
* @param	CallbackRef is a user data item that will be passed to the
*			callback function when it is invoked.
*
* @return
*			- XST_SUCCESS if callback function installed successfully.
*			- XST_INVALID_PARAM when HandlerType is invalid.
*
* @note		Invoking this function for a handler that already has been
*			installed replaces it with the new handler.
*
******************************************************************************/
int XHdcp22Rx_SetCallback(XHdcp22_Rx *InstancePtr, XHdcp22_Rx_HandlerType HandlerType, void *CallbackFunc, void *CallbackRef)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(HandlerType > (XHDCP22_RX_HANDLER_UNDEFINED));
	Xil_AssertNonvoid(HandlerType < (XHDCP22_RX_HANDLER_INVALID));
	Xil_AssertNonvoid(CallbackFunc != NULL);
	Xil_AssertNonvoid(CallbackRef != NULL);

	u32 Status;

	/* Check for handler type */
	switch(HandlerType)
	{
		// DDC Set Register Address
		case (XHDCP22_RX_HANDLER_DDC_SETREGADDR):
			InstancePtr->Handles.DdcSetAddressCallback = (XHdcp22_Rx_SetHandler)CallbackFunc;
			InstancePtr->Handles.DdcSetAddressCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcSetAddressCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC Set Register Data
		case (XHDCP22_RX_HANDLER_DDC_SETREGDATA):
			InstancePtr->Handles.DdcSetDataCallback = (XHdcp22_Rx_SetHandler)CallbackFunc;
			InstancePtr->Handles.DdcSetDataCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcSetDataCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC Get Register Data
		case (XHDCP22_RX_HANDLER_DDC_GETREGDATA):
			InstancePtr->Handles.DdcGetDataCallback = (XHdcp22_Rx_GetHandler)CallbackFunc;
			InstancePtr->Handles.DdcGetDataCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcGetDataCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC Get Write Buffer Size
		case (XHDCP22_RX_HANDLER_DDC_GETWBUFSIZE):
			InstancePtr->Handles.DdcGetWriteBufferSizeCallback = (XHdcp22_Rx_GetHandler)CallbackFunc;
			InstancePtr->Handles.DdcGetWriteBufferSizeCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcGetWriteBufferSizeCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC Get Read Buffer Size
		case (XHDCP22_RX_HANDLER_DDC_GETRBUFSIZE):
			InstancePtr->Handles.DdcGetReadBufferSizeCallback = (XHdcp22_Rx_GetHandler)CallbackFunc;
			InstancePtr->Handles.DdcGetReadBufferSizeCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcGetReadBufferSizeCallbackRefSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC Is Write Buffer Empty
		case (XHDCP22_RX_HANDLER_DDC_ISWBUFEMPTY):
			InstancePtr->Handles.DdcIsWriteBufferEmptyCallback = (XHdcp22_Rx_GetHandler)CallbackFunc;
			InstancePtr->Handles.DdcIsWriteBufferEmptyCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcIsWriteBufferEmptyCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC Is Read Buffer Empty
		case (XHDCP22_RX_HANDLER_DDC_ISRBUFEMPTY):
			InstancePtr->Handles.DdcIsReadBufferEmptyCallback = (XHdcp22_Rx_GetHandler)CallbackFunc;
			InstancePtr->Handles.DdcIsReadBufferEmptyCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcIsReadBufferEmptyCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC Clear Read Buffer
		case (XHDCP22_RX_HANDLER_DDC_CLEARRBUF):
			InstancePtr->Handles.DdcClearReadBufferCallback = (XHdcp22_Rx_RunHandler)CallbackFunc;
			InstancePtr->Handles.DdcClearReadBufferCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcClearReadBufferCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC Clear Write Buffer
		case (XHDCP22_RX_HANDLER_DDC_CLEARWBUF):
			InstancePtr->Handles.DdcClearWriteBufferCallback = (XHdcp22_Rx_RunHandler)CallbackFunc;
			InstancePtr->Handles.DdcClearWriteBufferCallbackRef = CallbackRef;
			InstancePtr->Handles.IsDdcClearWriteBufferCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Authenticated
		case (XHDCP22_RX_HANDLER_AUTHENTICATED):
			InstancePtr->Handles.AuthenticatedCallback = (XHdcp22_Rx_RunHandler)CallbackFunc;
			InstancePtr->Handles.AuthenticatedCallbackRef = CallbackRef;
			InstancePtr->Handles.IsAuthenticatedCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Unauthenticated
		case (XHDCP22_RX_HANDLER_UNAUTHENTICATED):
			InstancePtr->Handles.UnauthenticatedCallback = (XHdcp22_Rx_RunHandler)CallbackFunc;
			InstancePtr->Handles.UnauthenticatedCallbackRef = CallbackRef;
			InstancePtr->Handles.IsUnauthenticatedCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Authentication Request
		case (XHDCP22_RX_HANDLER_AUTHENTICATION_REQUEST):
			InstancePtr->Handles.AuthenticationRequestCallback = (XHdcp22_Rx_RunHandler)CallbackFunc;
			InstancePtr->Handles.AuthenticationRequestCallbackRef = CallbackRef;
			InstancePtr->Handles.IsAuthenticationRequestCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Management Request
		case (XHDCP22_RX_HANDLER_STREAM_MANAGE_REQUEST):
			InstancePtr->Handles.StreamManageRequestCallback = (XHdcp22_Rx_RunHandler)CallbackFunc;
			InstancePtr->Handles.StreamManageRequestCallbackRef = CallbackRef;
			InstancePtr->Handles.IsStreamManageRequestCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Topology Update
		case (XHDCP22_RX_HANDLER_TOPOLOGY_UPDATE):
			InstancePtr->Handles.TopologyUpdateCallback = (XHdcp22_Rx_RunHandler)CallbackFunc;
			InstancePtr->Handles.TopologyUpdateCallbackRef = CallbackRef;
			InstancePtr->Handles.IsTopologyUpdateCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Encryption Status Update
		case (XHDCP22_RX_HANDLER_ENCRYPTION_UPDATE):
			InstancePtr->Handles.EncryptionStatusCallback = (XHdcp22_Rx_RunHandler)CallbackFunc;
			InstancePtr->Handles.EncryptionStatusCallbackRef = CallbackRef;
			InstancePtr->Handles.IsEncryptionStatusCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		default:
			Status = (XST_INVALID_PARAM);
			break;
	}

	/* Reset DDC registers only when all handlers have been registered */
    if(InstancePtr->Handles.IsDdcSetAddressCallbackSet &&
		InstancePtr->Handles.IsDdcSetDataCallbackSet &&
		InstancePtr->Handles.IsDdcGetDataCallbackSet &&
		InstancePtr->Handles.IsDdcGetWriteBufferSizeCallbackSet &&
		InstancePtr->Handles.IsDdcGetReadBufferSizeCallbackRefSet &&
		InstancePtr->Handles.IsDdcIsWriteBufferEmptyCallbackSet &&
		InstancePtr->Handles.IsDdcIsReadBufferEmptyCallbackSet &&
		InstancePtr->Handles.IsDdcClearReadBufferCallbackSet &&
		InstancePtr->Handles.IsDdcClearWriteBufferCallbackSet &&
        InstancePtr->Handles.IsDdcAllCallbacksSet == (FALSE))
	{
		InstancePtr->Handles.IsDdcAllCallbacksSet = (TRUE);
		XHdcp22Rx_ResetDdc(InstancePtr, TRUE, TRUE, TRUE, TRUE);
	}

	return Status;
}

/*****************************************************************************/
/**
* This function executes the HDCP22-RX state machine.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	None.
*
* @note		State transitions are logged.
******************************************************************************/
int XHdcp22Rx_Poll(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Run state machine */
	if(InstancePtr->Info.IsEnabled == TRUE)
	{
		InstancePtr->StateFunc = (XHdcp22_Rx_StateFunc)(*InstancePtr->StateFunc)(InstancePtr);
	}

	/* Log state transitions */
	if(InstancePtr->Info.NextState != InstancePtr->Info.CurrentState)
	{
	    XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_STATE, InstancePtr->Info.NextState);
	}

	return (int)(InstancePtr->Info.AuthenticationStatus);
}

/*****************************************************************************/
/**
* This function checks if the HDCP22-RX state machine is enabled.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	TRUE or FALSE.
*
* @note		None.
******************************************************************************/
u8 XHdcp22Rx_IsEnabled(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return InstancePtr->Info.IsEnabled;
}

/*****************************************************************************/
/**
* This function checks if the HDCP22-RX cipher encryption is enabled.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	TRUE or FALSE.
*
* @note		None.
******************************************************************************/
u8 XHdcp22Rx_IsEncryptionEnabled(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u32 Status;

	Status = XHdcp22Cipher_IsEncrypted(&InstancePtr->CipherInst);

	return (Status) ? TRUE : FALSE;
}

/*****************************************************************************/
/**
* This function checks if the HDCP22-RX state machine is enabled but
* not yet in the Authenticated state.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	TRUE or FALSE.
*
* @note		None.
******************************************************************************/
u8 XHdcp22Rx_IsInProgress(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Info.AuthenticationStatus == XHDCP22_RX_AUTHENTICATION_BUSY) ? (TRUE) : (FALSE);
}

/*****************************************************************************/
/**
* This function checks if the HDCP22-RX state machine is in the
* Authenticated state.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	TRUE or FALSE.
*
* @note		None.
******************************************************************************/
u8 XHdcp22Rx_IsAuthenticated(XHdcp22_Rx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Info.AuthenticationStatus == XHDCP22_RX_AUTHENTICATED) ? (TRUE) : (FALSE);
}

/*****************************************************************************/
/**
* This function checks if the HDCP22-RX state machine has detected an error
* condition.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	TRUE or FALSE.
*
* @note		None.
******************************************************************************/
u8 XHdcp22Rx_IsError(XHdcp22_Rx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Info.ErrorFlagSticky != XHDCP22_RX_ERROR_FLAG_NONE) ? (TRUE) : (FALSE);
}

/*****************************************************************************/
/**
*
* This function returns the current repeater mode status.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return
*   - TRUE if the HDCP 2.2 instance is the upstream port of a repeater
*   - FALSE if the HDCP 2.2 instance is a receiver
*
******************************************************************************/
u8 XHdcp22Rx_IsRepeater(XHdcp22_Rx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Config.Mode != XHDCP22_RX_RECEIVER) ? (TRUE) : (FALSE);
}

/*****************************************************************************/
/**
*
* This function sets the repeater mode status.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param  Set is TRUE to enable repeater mode and FALSE to disable.
*
* @return None.
*
******************************************************************************/
void XHdcp22Rx_SetRepeater(XHdcp22_Rx *InstancePtr, u8 Set)
{
	Xil_AssertVoid(InstancePtr != NULL);

	/* Update mode */
	if (Set)
		InstancePtr->Config.Mode = XHDCP22_RX_REPEATER;
	else
		InstancePtr->Config.Mode = XHDCP22_RX_RECEIVER;

	/* Set RxCaps */
	InstancePtr->RxCaps[0] = 0x02;
	InstancePtr->RxCaps[1] = 0x00;
	InstancePtr->RxCaps[2] = (InstancePtr->Config.Mode == XHDCP22_RX_RECEIVER) ? 0x00 : 0x01;
}

/*****************************************************************************/
/**
* This function is called when 50 consecutive data island ECC errors are
* detected indicating a link integrity problem. The error flag is set
* indicating to the state machine that REAUTH_REQ bit in the RXSTATUS
* register be set. Setting this flag only takes effect when the
* authentication state machine is in the Authenticated state B4.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	None.
*
* @note		This function can be registered as an asynchronous callback.
******************************************************************************/
void XHdcp22Rx_SetLinkError(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Increment link error count */
	InstancePtr->Info.LinkErrorCnt++;

	/* Set Error Flag */
	InstancePtr->Info.ErrorFlag |= XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY;

	/* Log error event set flag */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY);
}

/*****************************************************************************/
/**
* This function is called when a DDC read/write burst stops prior to completing
* the expected message size. This will cause the message buffers to be flushed
* and state machine reset.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	None.
*
* @note		This function can be registered as an asynchronous callback.
******************************************************************************/
void XHdcp22Rx_SetDdcError(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Increment link error count */
	InstancePtr->Info.DdcErrorCnt++;

	/* Set Error Flag */
	InstancePtr->Info.ErrorFlag |= XHDCP22_RX_ERROR_FLAG_DDC_BURST;

	/* Log error event and set flag */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_DDC_BURST);
}

/*****************************************************************************/
/**
* This function is called when a complete message is available in the
* write message buffer.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	None.
*
* @note		This function can be registered as an asynchronous callback.
******************************************************************************/
void XHdcp22Rx_SetWriteMessageAvailable(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Log info event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_WRITE_MESSAGE_AVAILABLE);

	/* Update DDC flag */
	InstancePtr->Info.DdcFlag |= XHDCP22_RX_DDC_FLAG_WRITE_MESSAGE_READY;
}

/*****************************************************************************/
/**
* This function is called when a message has been read out of the read
* message buffer.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	None.
*
* @note		This function can be registered as an asynchronous callback.
******************************************************************************/
void XHdcp22Rx_SetReadMessageComplete(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Log info event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_READ_MESSAGE_COMPLETE);

	/* Update DDC flag */
	InstancePtr->Info.DdcFlag |= XHDCP22_RX_DDC_FLAG_READ_MESSAGE_READY;
}

/*****************************************************************************/
/**
* This function is used to load the Lc128 value by copying the contents
* of the array referenced by Lc128Ptr into the cipher.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param	Lc128Ptr is a pointer to an array.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp22Rx_LoadLc128(XHdcp22_Rx *InstancePtr, const u8 *Lc128Ptr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Lc128Ptr != NULL);

	XHdcp22Cipher_SetLc128(&InstancePtr->CipherInst, Lc128Ptr,  XHDCP22_RX_LC128_SIZE);
}

/*****************************************************************************/
/**
* This function is used to load the public certificate.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param	PublicCertPtr is a pointer to an array.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp22Rx_LoadPublicCert(XHdcp22_Rx *InstancePtr, const u8 *PublicCertPtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(PublicCertPtr != NULL);

	InstancePtr->PublicCertPtr = PublicCertPtr;
}

/*****************************************************************************/
/**
* This function is used to load the private key.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param	PrivateKeyPtr is a pointer to an array.
*
* @return	- XST_SUCCESS if MMULT keys are calcualted correctly.
* 			- XST_FAILURE if MMULT keys are not calculated correctly.
*
* @note		None.
******************************************************************************/
int XHdcp22Rx_LoadPrivateKey(XHdcp22_Rx *InstancePtr, const u8 *PrivateKeyPtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(PrivateKeyPtr != NULL);

	int Status = XST_SUCCESS;
	XHdcp22_Rx_KprivRx *PrivateKey = (XHdcp22_Rx_KprivRx *)PrivateKeyPtr;

	/* Load the Private Key */
	InstancePtr->PrivateKeyPtr = PrivateKeyPtr;

	/* Calculate Montgomery Multiplier NPrimeP */
	Status = XHdcp22Rx_CalcMontNPrime(InstancePtr->NPrimeP, (u8 *)PrivateKey->p, XHDCP22_RX_P_SIZE/4);
    if(Status != XST_SUCCESS)
	{
	    xil_printf("ERROR: HDCP22-RX MMult NPrimeP Generation Failed\n\r");
		return Status;
	}

	/* Calculate Montgomery Multiplier NPrimeQ */
	Status = XHdcp22Rx_CalcMontNPrime(InstancePtr->NPrimeQ, (u8 *)PrivateKey->q, XHDCP22_RX_P_SIZE/4);
	if(Status != XST_SUCCESS)
	{
	    xil_printf("ERROR: HDCP22-RX MMult NPrimeQ Generation Failed\n\r");
	    return Status;
	}

	return Status;
}

/*****************************************************************************/
/**
* This function reads the version.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Returns the version register of the cipher.
*
* @note     None.
******************************************************************************/
u32 XHdcp22Rx_GetVersion(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return XHdcp22Cipher_GetVersion(&InstancePtr->CipherInst);
}

/*****************************************************************************/
/**
* This function returns the pointer to the internal timer control instance
* needed for connecting the timer interrupt to an interrupt controller.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   A pointer to the internal timer control instance.
*
* @note     None.
******************************************************************************/
XTmrCtr* XHdcp22Rx_GetTimer(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return &InstancePtr->TimerInst;
}

/*****************************************************************************/
/**
* This function copies a complete repeater topology table into the instance
* repeater topology table.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    TopologyPtr is a pointer to the repeater topology table.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp22Rx_SetTopology(XHdcp22_Rx *InstancePtr, const XHdcp22_Rx_Topology *TopologyPtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(TopologyPtr != NULL);

	memcpy(&(InstancePtr->Topology), TopologyPtr, sizeof(XHdcp22_Rx_Topology));
}

/*****************************************************************************/
/**
* This function copies the RECEIVER_ID_LIST into the repeater topology table.
* Receiver ID list is constructed by appending Receiver IDs in big-endian order.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    ListPtr is a pointer to the Receiver ID list. The list consists
*           of a contiguous set of bytes stored in big-endian order, with
*           each Receiver ID occuping five bytes with a maximum of 31
*           Receiver IDs.
* @param    ListSize is the number of Receiver IDs in the list. The
*           list size cannot exceed 31 devices.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp22Rx_SetTopologyReceiverIdList(XHdcp22_Rx *InstancePtr, const u8 *ListPtr, u32 ListSize)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(ListPtr != NULL);
	Xil_AssertVoid(ListSize <= XHDCP22_RX_MAX_DEVICE_COUNT);

	memcpy(InstancePtr->Topology.ReceiverIdList, ListPtr, (ListSize*XHDCP22_RX_RCVID_SIZE));
}

/*****************************************************************************/
/**
* This function is used to set various fields inside the topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    Field indicates what field of the topology structure to update.
* @param    Value is the value assigned to the field of the topology structure.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp22Rx_SetTopologyField(XHdcp22_Rx *InstancePtr, XHdcp22_Rx_TopologyField Field, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Field < XHDCP22_RX_TOPOLOGY_INVALID);

	switch(Field)
	{
	case XHDCP22_RX_TOPOLOGY_DEPTH :
		XHdcp22Rx_SetTopologyDepth(InstancePtr, Value);
		break;
	case XHDCP22_RX_TOPOLOGY_DEVICECNT :
		XHdcp22Rx_SetTopologyDeviceCnt(InstancePtr, Value);
		break;
	case XHDCP22_RX_TOPOLOGY_MAXDEVSEXCEEDED :
		XHdcp22Rx_SetTopologyMaxDevsExceeded(InstancePtr, Value);
		break;
	case XHDCP22_RX_TOPOLOGY_MAXCASCADEEXCEEDED :
		XHdcp22Rx_SetTopologyMaxCascadeExceeded(InstancePtr, Value);
		break;
	case XHDCP22_RX_TOPOLOGY_HDCP20REPEATERDOWNSTREAM :
		XHdcp22Rx_SetTopologyHdcp20RepeaterDownstream(InstancePtr, Value);
		break;
	case XHDCP22_RX_TOPOLOGY_HDCP1DEVICEDOWNSTREAM :
		XHdcp22Rx_SetTopologyHdcp1DeviceDownstream(InstancePtr, Value);
		break;
	default:
		break;
	}
}

/*****************************************************************************/
/**
* This function is used to indicate that the topology table has been
* updated and is ready for upstream propagation. Before calling this
* function ensure that all the fields in the repeater topology table
* have been updated.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp22Rx_SetTopologyUpdate(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->Info.IsTopologyValid = TRUE;

	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO, XHDCP22_RX_LOG_INFO_TOPOLOGY_UPDATE);
}

/*****************************************************************************/
/**
* This function is gets the type information received from the
* RepeaterAuth_Stream_Manage message for downstream propagation
* of management information. This function should be called only
* after the RepeaterAuth_Stream_Manage message has been received,
* and can be called inside the user callback function type
* XHDCP22_RX_HANDLER_STREAM_MANAGE_REQUEST.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - 0x00: Type 0 Content Stream. May be transmitted by the
*             HDCP Repeater to all HDCP Devices.
*           - 0x01: Type 1 Content Stream. Must not be transmitted
*             by the HDCP Repeater to HDCP 1.x-compliant Devices and
*             HDCP 2.0-compliant Repeaters.
*           - 0x02-0xFF: Reserved for future use only. Content
*             Streams with reserved Type values must be treated
*             similar to Type 1 Content Streams.
*
* @note     None.
******************************************************************************/
u8 XHdcp22Rx_GetContentStreamType(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Params.StreamIdType[1]);
}

/*****************************************************************************/
/**
* This function is called to initialize the cipher.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
******************************************************************************/
static int XHdcp22Rx_InitializeCipher(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status = XST_SUCCESS;
	UINTPTR SubcoreBaseAddr;
	XHdcp22_Cipher_Config *CipherConfigPtr = NULL;

	CipherConfigPtr = XHdcp22Cipher_LookupConfig(InstancePtr->Config.CipherDeviceId);
	if(CipherConfigPtr == NULL)
	{
		return XST_FAILURE;
	}

	Status = XHdcp22Rx_ComputeBaseAddress(InstancePtr->Config.BaseAddress, CipherConfigPtr->BaseAddress, &SubcoreBaseAddr);
	Status |= XHdcp22Cipher_CfgInitialize(&InstancePtr->CipherInst, CipherConfigPtr, SubcoreBaseAddr);

	XHdcp22Cipher_SetRxMode(&InstancePtr->CipherInst);

	return Status;
}

/*****************************************************************************/
/**
* This function is called to initialize the modular multiplier.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
******************************************************************************/
static int XHdcp22Rx_InitializeMmult(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status = XST_SUCCESS;
	UINTPTR SubcoreBaseAddr;
	XHdcp22_mmult_Config *MmultConfigPtr;

	MmultConfigPtr = XHdcp22_mmult_LookupConfig(InstancePtr->Config.MontMultDeviceId);
	if(MmultConfigPtr == NULL)
	{
		return XST_FAILURE;
	}

	Status = XHdcp22Rx_ComputeBaseAddress(InstancePtr->Config.BaseAddress, MmultConfigPtr->BaseAddress, &SubcoreBaseAddr);
	Status |= XHdcp22_mmult_CfgInitialize(&InstancePtr->MmultInst, MmultConfigPtr, SubcoreBaseAddr);

	return Status;
}

/*****************************************************************************/
/**
* This function is called to initialize the random number generator.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
******************************************************************************/
static int XHdcp22Rx_InitializeRng(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status = XST_SUCCESS;
	UINTPTR SubcoreBaseAddr;
	XHdcp22_Rng_Config *RngConfigPtr;

	RngConfigPtr = XHdcp22Rng_LookupConfig(InstancePtr->Config.RngDeviceId);
	if(RngConfigPtr == NULL)
	{
		return XST_FAILURE;
	}

	Status |= XHdcp22Rx_ComputeBaseAddress(InstancePtr->Config.BaseAddress, RngConfigPtr->BaseAddress, &SubcoreBaseAddr);
	Status |= XHdcp22Rng_CfgInitialize(&InstancePtr->RngInst, RngConfigPtr, SubcoreBaseAddr);

	return Status;
}

/*****************************************************************************/
/**
* This function is called to initialize the timer.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
******************************************************************************/
static int XHdcp22Rx_InitializeTimer(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status = XST_SUCCESS;
	UINTPTR SubcoreBaseAddr;
	XTmrCtr_Config *TimerConfigPtr;

	TimerConfigPtr = XTmrCtr_LookupConfig(InstancePtr->Config.TimerDeviceId);
	if(TimerConfigPtr == NULL)
	{
		return XST_FAILURE;
	}

	Status = XHdcp22Rx_ComputeBaseAddress(InstancePtr->Config.BaseAddress, TimerConfigPtr->BaseAddress, &SubcoreBaseAddr);
	XTmrCtr_CfgInitialize(&InstancePtr->TimerInst, TimerConfigPtr, SubcoreBaseAddr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	XTmrCtr_SetOptions(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_0,
	                   XTC_AUTO_RELOAD_OPTION);
	XTmrCtr_SetOptions(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_1,
	                   XTC_INT_MODE_OPTION | XTC_DOWN_COUNT_OPTION);
	XTmrCtr_SetHandler(&InstancePtr->TimerInst, XHdcp22Rx_TimerHandler,
	                   InstancePtr);

	return Status;
}

/*****************************************************************************/
/**
*
* This function handles timer interrupts.
*
* @param  CallbackRef refers to a XHdcp22_Rx structure
* @param  TmrCntNumber the number of used timer.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Rx_TimerHandler(void *CallbackRef, u8 TmrCntNumber)
{
	XHdcp22_Rx *InstancePtr = (XHdcp22_Rx *)CallbackRef;

	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

	if (TmrCntNumber == XHDCP22_RX_TMR_CTR_0) {
		return;
	}

	/* Set timer expired signaling flag */
	InstancePtr->Info.TimerExpired = (TRUE);

	/* Log error event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_TIMER_EXPIRED);
}

/*****************************************************************************/
/**
*
* This function starts the count down timer needed for checking
* protocol timeouts.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    TimeOut_mSec is the timeout for the next status update.
*
* @return   XST_SUCCESS or XST_FAILURE.
*
* @note     None.
*
******************************************************************************/
static void XHdcp22Rx_StartTimer(XHdcp22_Rx *InstancePtr, u32 TimeOut_mSec,
                                u8 ReasonId)
{
	u32 Ticks = (u32)(InstancePtr->TimerInst.Config.SysClockFreqHz / 1000000) * TimeOut_mSec * 1000;

	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

	InstancePtr->Info.TimerExpired = (FALSE);
	InstancePtr->Info.TimerReasonId = ReasonId;
	InstancePtr->Info.TimerInitialTicks = Ticks;

#ifndef _XHDCP22_RX_DISABLE_TIMEOUT_CHECKING_
	XTmrCtr_SetResetValue(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_1, Ticks);
	XTmrCtr_Start(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_1);

	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG,
	                XHDCP22_RX_LOG_DEBUG_TIMER_START);
#endif /* _XHDCP22_RX_DISABLE_TIMEOUT_CHECKING_ */
}

/*****************************************************************************/
/**
*
* This function stops the count down timer used for protocol timeouts.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    TimeOut_mSec is the timeout for the next status update.
*
* @return   XST_SUCCESS or XST_FAILURE.
*
* @note     None.
*
******************************************************************************/
static void XHdcp22Rx_StopTimer(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

	InstancePtr->Info.TimerExpired = (FALSE);

	XTmrCtr_Stop(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_1);
}

/*****************************************************************************/
/**
* This function computes the subcore absolute address on axi-lite interface
* Subsystem is mapped at an absolute address and all included sub-cores are
* at pre-defined offset from the subsystem base address. To access the subcore
* register map from host CPU an absolute address is required.
*
* @param	BaseAddress is the base address of the subsystem instance
* @param	SubcoreOffset is the offset of the the subcore instance
* @param	SubcoreAddressPtr is the computed base address of the subcore instance
*
* @return	XST_SUCCESS if base address computation is successful and within
* 			subsystem address range else XST_FAILURE
*
******************************************************************************/
static int XHdcp22Rx_ComputeBaseAddress(UINTPTR BaseAddress, UINTPTR SubcoreOffset, UINTPTR *SubcoreAddressPtr)
{
	int Status;
	UINTPTR Address;

	Address = BaseAddress | SubcoreOffset;
	if((Address >= BaseAddress))
	{
		*SubcoreAddressPtr = Address;
		Status = XST_SUCCESS;
	}
	else
	{
		*SubcoreAddressPtr = 0;
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function is used to get a random value Rrx of 64bits for AKEInit.
* When the test mode is set to XHDCP22_RX_TESTMODE_NO_TX a preloaded test
* value is copied into the array with pointer RrxPtr. Otherwise, a
* random value is generated and copied.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param	RrxPtr is a pointer to an array where the 64bit Rrx is
* 			to be copied.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
******************************************************************************/
static int XHdcp22Rx_GenerateRrx(XHdcp22_Rx *InstancePtr, u8 *RrxPtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(RrxPtr != NULL);

	XHdcp22Rx_GenerateRandom(InstancePtr, XHDCP22_RX_RRX_SIZE, RrxPtr);

#ifdef _XHDCP22_RX_TEST_
	/* In test mode copy the test vector */
	XHdcp22Rx_TestGenerateRrx(InstancePtr, RrxPtr);
#endif

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function is used to check if a complete message is available in
* the write message buffer. The DDC flag is cleared when a message
* available is detected.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	TRUE or FALSE.
*
* @note		None.
******************************************************************************/
static u8 XHdcp22Rx_IsWriteMessageAvailable(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check if write message is available then clear flag */
	if(InstancePtr->Info.DdcFlag & XHDCP22_RX_DDC_FLAG_WRITE_MESSAGE_READY)
	{
		InstancePtr->Info.DdcFlag &= ~XHDCP22_RX_DDC_FLAG_WRITE_MESSAGE_READY;
		return (TRUE);
	}

	return (FALSE);
}

/*****************************************************************************/
/**
* This function is used to check if a complete message has been read
* out of the read message buffer, indicating that the buffer is empty.
* The DDC flag is cleared when a read message complete has been detected.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	TRUE or FALSE.
*
* @note		None.
******************************************************************************/
static u8 XHdcp22Rx_IsReadMessageComplete(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check if read message is complete then clear flag */
	if((InstancePtr->Info.DdcFlag & XHDCP22_RX_DDC_FLAG_READ_MESSAGE_READY))
		return (TRUE);
	else
		return (FALSE);
}

/*****************************************************************************/
/**
* This function sets the DDC RxStatus registers (0x70-0x71) MessageSize bits.
* The HDCP22-RX state machine calls this function immediately after writing
* a complete message into the read message buffer. The repeater READY bit
* can also be updated in conjunction to the message size.
*
* @param  InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param  MessageSize indicates the size in bytes of the message
*         available in the read message buffer.
* @param  ReauthReq is set to TRUE to assert the REAUTH_REQ bit.
* @param  TopologyReady is set to TRUE to assert the READY bit.
*
* @return None.
*
* @note	  None.
******************************************************************************/
static void XHdcp22Rx_SetRxStatus(XHdcp22_Rx *InstancePtr, u16 MessageSize, u8 ReauthReq, u8 TopologyReady)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(MessageSize <= 0x03FF);

	u8 RxStatus[2] = {0, 0};

	/* Update RxStatus[11:0] */
	RxStatus[1] &= (0x0C00 >> 8);                  // Clear fields except for RxStatus[11:10], REAUTH_REQ and READY
	RxStatus[0] = (MessageSize & 0x00FF);          // Update RxStatus[7:0], Message_Size
	RxStatus[1] |= ((MessageSize & 0x0300) >> 8);  // Update RxStatus[9:8], Message_Size
	if(TopologyReady)
		RxStatus[1] |= (0x0400 >> 8);                // Set RxStatus[10], READY bit
	if(ReauthReq)
		RxStatus[1] |= (0x0800 >> 8);                // Set RxStatus[11], REAUTH_REQ bit

	/* Set RxStatus */
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef, XHDCP22_RX_DDC_RXSTATUS0_REG);
	InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef, RxStatus[0]);
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef, XHDCP22_RX_DDC_RXSTATUS1_REG);
	InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef, RxStatus[1]);

	/* Clear DDC read message buffer ready */
	if (MessageSize > 0)
		InstancePtr->Info.DdcFlag &= ~XHDCP22_RX_DDC_FLAG_READ_MESSAGE_READY;
}

/*****************************************************************************/
/**
* This function sets the DDC RxStatus registers (0x70-0x71) ReauthReq bit
* and clears the link integrity error flag. Setting the ReauthReq bit
* signals the transmitter to re-initiate authentication.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	None.
*
* @note		None.
******************************************************************************/
static void XHdcp22Rx_SetDdcReauthReq(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

#ifndef _XHDCP22_RX_DISABLE_REAUTH_REQUEST_
	/* Log info event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO, XHDCP22_RX_LOG_INFO_REQAUTH_REQ);

	/* Increment re-authentication request count */
	InstancePtr->Info.ReauthRequestCnt++;

	/* Set the ReauthReq flag */
	InstancePtr->Info.ReauthReq = TRUE;

	/* Set the RxStatus register */
	XHdcp22Rx_SetRxStatus(InstancePtr, 0,
		InstancePtr->Info.ReauthReq, InstancePtr->Info.TopologyReady);

	/* Clear link integrity error flag */
	InstancePtr->Info.ErrorFlag &= ~XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY;
#endif /* _XHDCP22_RX_DISABLE_REAUTH_REQUEST_ */
}

/*****************************************************************************/
/**
* This function resets the HDCP22-RX DDC registers to their default values
* and clears the read/write message buffers. The DDC status flag is set to
* it's initial state and the DDC error flags are cleared.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param	ClrWrBuffer is TRUE to clear the write message buffer.
* @param	ClrRdBuffer is TRUE to clear the read message buffer.
* @param	ClrReady is TRUE to clear the READY bit.
* @param	ClrReauthReq is set to TRUE to clear the REAUTH_REQ bit.
*
* @return	None.
*
* @note		None.
******************************************************************************/
static void XHdcp22Rx_ResetDdc(XHdcp22_Rx *InstancePtr, u8 ClrWrBuffer,
	u8 ClrRdBuffer, u8 ClrReady, u8 ClrReauthReq)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->Handles.IsDdcAllCallbacksSet == TRUE);

	/* Clear READY flag */
	if(ClrReady) {
		InstancePtr->Info.TopologyReady = FALSE;
	}

	/* Clear REAUTH_REQ flag */
	if(ClrReauthReq) {
		InstancePtr->Info.ReauthReq = FALSE;
	}

	/* Update RxStatus register */
	XHdcp22Rx_SetRxStatus(InstancePtr, 0, InstancePtr->Info.ReauthReq, InstancePtr->Info.TopologyReady);

	/* Reset read message buffers */
	if(ClrRdBuffer)
	{
		InstancePtr->Handles.DdcClearReadBufferCallback(InstancePtr->Handles.DdcClearReadBufferCallbackRef);
		/* Initialize DDC status flag */
		InstancePtr->Info.DdcFlag = XHDCP22_RX_DDC_FLAG_READ_MESSAGE_READY;
	}

	/* Reset write message buffers */
	if(ClrWrBuffer) {
		InstancePtr->Handles.DdcClearWriteBufferCallback(InstancePtr->Handles.DdcClearWriteBufferCallbackRef);
	}

	/* Clear DDC error flags */
	InstancePtr->Info.ErrorFlag &= ~XHDCP22_RX_ERROR_FLAG_DDC_BURST;

	/* Set HDCP2Version register */
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef, XHDCP22_RX_DDC_VERSION_REG);
	InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef, 0x04);
}

/*****************************************************************************/
/**
* This function resets the HDCP22-RX system after an error event. The
* driver calls this function when recovering from error conditions.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	None.
*
* @note		None.
******************************************************************************/
static void XHdcp22Rx_ResetAfterError(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	int AuthenticationStatus = InstancePtr->Info.AuthenticationStatus;

	/* Log info event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_FORCE_RESET);

	/* Reset cipher */
	XHdcp22Cipher_Disable(&InstancePtr->CipherInst);
	XHdcp22Cipher_Enable(&InstancePtr->CipherInst);

	/* Clear message buffer */
	memset(&InstancePtr->MessageBuffer, 0, sizeof(XHdcp22_Rx_Message));
	InstancePtr->MessageSize = 0;

	/* Set default values */
	InstancePtr->StateFunc = (XHdcp22_Rx_StateFunc)(&XHdcp22Rx_StateB0);
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_UNAUTHENTICATED;
	InstancePtr->Info.IsNoStoredKm = FALSE;
	InstancePtr->Info.IsEncrypted = FALSE;
	InstancePtr->Info.LCInitAttempts = 0;
	InstancePtr->Info.CurrentState = XHDCP22_RX_STATE_B0_WAIT_AKEINIT;
	InstancePtr->Info.NextState = XHDCP22_RX_STATE_B0_WAIT_AKEINIT;

	/* Reset repeater values */
	memset(&InstancePtr->Topology, 0, sizeof(XHdcp22_Rx_Topology));
	InstancePtr->Info.IsTopologyValid = FALSE;
	InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
	InstancePtr->Info.SeqNumV = 0;
	InstancePtr->Info.HasStreamManagementInfo = FALSE;
	InstancePtr->Info.SkipRead = FALSE;

	/* Disable timer */
	XHdcp22Rx_StopTimer(InstancePtr);

	/* Reset parameters */
	XHdcp22Rx_ResetParams(InstancePtr);

	/* Run callback function type: XHDCP22_RX_HANDLER_UNAUTHENTICATED */
	if((InstancePtr->Handles.IsUnauthenticatedCallbackSet) &&
		 (AuthenticationStatus == XHDCP22_RX_AUTHENTICATED))
	{
		InstancePtr->Handles.UnauthenticatedCallback(InstancePtr->Handles.UnauthenticatedCallbackRef);
	}
}

/*****************************************************************************/
/**
* This function resets the HDCP22-RX parameters stored in memory during the
* authentication process.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	None.
*
* @note		This function is called each time an AKE_Init message is
*			received.
******************************************************************************/
static void XHdcp22Rx_ResetParams(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Clear stored parameters */
	memset(InstancePtr->Params.Km,           0, sizeof(InstancePtr->Params.Km));
	memset(InstancePtr->Params.Ks,           0, sizeof(InstancePtr->Params.Ks));
	memset(InstancePtr->Params.Rn,           0, sizeof(InstancePtr->Params.Rn));
	memset(InstancePtr->Params.EKh,          0, sizeof(InstancePtr->Params.EKh));
	memset(InstancePtr->Params.Riv,          0, sizeof(InstancePtr->Params.Riv));
	memset(InstancePtr->Params.Rrx,          0, sizeof(InstancePtr->Params.Rrx));
	memset(InstancePtr->Params.Rtx,          0, sizeof(InstancePtr->Params.Rtx));
	memset(InstancePtr->Params.RxCaps,       0, sizeof(InstancePtr->Params.RxCaps));
	memset(InstancePtr->Params.TxCaps,       0, sizeof(InstancePtr->Params.TxCaps));
	memset(InstancePtr->Params.HPrime,       0, sizeof(InstancePtr->Params.HPrime));
	memset(InstancePtr->Params.LPrime,       0, sizeof(InstancePtr->Params.LPrime));
	memset(InstancePtr->Params.VPrime,       0, sizeof(InstancePtr->Params.VPrime));
	memset(InstancePtr->Params.SeqNumM,      0, sizeof(InstancePtr->Params.SeqNumM));
	memset(InstancePtr->Params.StreamIdType, 0, sizeof(InstancePtr->Params.StreamIdType));
	memset(InstancePtr->Params.MPrime,       0, sizeof(InstancePtr->Params.MPrime));
}

/*****************************************************************************/
/**
* This function uses polling to read a complete message out of the read
* message buffer.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return	Size of message read out of read message buffer.
*
* @note		None.
******************************************************************************/
static int XHdcp22Rx_PollMessage(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u32 Size = 0;
	u32 Offset = 0;

	/* Get message */
	if(XHdcp22Rx_IsWriteMessageAvailable(InstancePtr) == TRUE)
	{
		Size = InstancePtr->Handles.DdcGetWriteBufferSizeCallback(InstancePtr->Handles.DdcGetWriteBufferSizeCallbackRef);

		InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef, XHDCP22_RX_DDC_WRITE_REG);

		for(Offset = 0; Offset < Size; Offset++)
		{
			InstancePtr->MessageBuffer[Offset] = InstancePtr->Handles.DdcGetDataCallback(InstancePtr->Handles.DdcGetDataCallbackRef);
		}
	}

	return Size;
}

/*****************************************************************************/
/**
* This function implements the Receiver State B0 (Unauthenticated). In this
* state the receiver is awaiting the reception of AKE_Init from the
* transmitter to trigger the authentication protocol.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     When an unexpected or malformed message is received
*           the system is reset to a known state to allow graceful
*           recovery from error.
******************************************************************************/
static void *XHdcp22Rx_StateB0(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_UNAUTHENTICATED;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}

	/* Check if message is available */
	InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);

	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT: /* Transition B0->B1 */
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);
			if(Status == XST_SUCCESS)
			{
			    InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
			    return (void *)XHdcp22Rx_StateB1;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	return (void *)XHdcp22Rx_StateB0;
}

/*****************************************************************************/
/**
* This function implements the Receiver State B1 (ComputeKm). In this
* state the receiver makes the AKESendCert message available for reading
* by the transmitter in response to AKEInit. If AKENoStoredKm is received,
* the receiver decrypts Km with KprivRx and calculates HPrime. If AKEStoredKm
* is received it decrypts Ekh(Km) to derive Km and calculate HPrime. It makes
* AKESendHPrime message available for reading immediately after computation
* of HPrime to ensure that the message is received by the transmitter within
* the specified 1s timeout at the transmitter for NoStoredKm and 200ms timeout
* for StoredKm. When AKENoStoredKm is received the AKESendPairingInfo message
* is made available to the transmitter for reading after within 200ms after
* AKESendHPrime message is received by the transmitter.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     When an unexpected or malformed message is received the
*           system is reset to the default state B0 to allow graceful
*           recovery from error.
******************************************************************************/
static void *XHdcp22Rx_StateB1(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATION_BUSY;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}

	/* Check if message is available */
	InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);

	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT:
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);
			if(Status == XST_SUCCESS)
			{
			    InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
			    break;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_AKENOSTOREDKM:
			if(InstancePtr->Info.CurrentState == XHDCP22_RX_STATE_B1_WAIT_AKEKM)
			{
				Status = XHdcp22Rx_ProcessMessageAKENoStoredKm(InstancePtr);
				if(Status == XST_SUCCESS)
				{
					InstancePtr->Info.IsNoStoredKm = TRUE;
					InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDHPRIME;
					break;
				}
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_AKENOSTOREDKM);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_AKESTOREDKM:
			if(InstancePtr->Info.CurrentState == XHDCP22_RX_STATE_B1_WAIT_AKEKM)
			{
				Status = XHdcp22Rx_ProcessMessageAKEStoredKm(InstancePtr);
				if(Status == XST_SUCCESS)
				{
					InstancePtr->Info.IsNoStoredKm = FALSE;
					InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDHPRIME;
					break;
				}
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_AKESTOREDKM);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	/* Message send */
	switch(InstancePtr->Info.NextState)
	{
	case XHDCP22_RX_STATE_B1_SEND_AKESENDCERT:
		if(XHdcp22Rx_IsReadMessageComplete(InstancePtr) == TRUE)
		{
			Status = XHdcp22Rx_SendMessageAKESendCert(InstancePtr);
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_WAIT_AKEKM;
		}
		break;
	case XHDCP22_RX_STATE_B1_SEND_AKESENDHPRIME:
		if(XHdcp22Rx_IsReadMessageComplete(InstancePtr) == TRUE)
		{
			Status = XHdcp22Rx_SendMessageAKESendHPrime(InstancePtr);
			if(InstancePtr->Info.IsNoStoredKm)
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDPAIRINGINFO;
			}
			else
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_WAIT_LCINIT;
				return (void *)XHdcp22Rx_StateB2; /* Transition B1->B2 */
			}
		}
		break;
	case XHDCP22_RX_STATE_B1_SEND_AKESENDPAIRINGINFO:
		if(XHdcp22Rx_IsReadMessageComplete(InstancePtr) == TRUE)
		{
			Status = XHdcp22Rx_SendMessageAKESendPairingInfo(InstancePtr);
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_WAIT_LCINIT;
			return (void *)XHdcp22Rx_StateB2;
		}
		break;
	default:
		break;
	}

	return (void *)XHdcp22Rx_StateB1;
}

/*****************************************************************************/
/**
* This function implements the Receiver State B2 (Compute_LPrime). The
* receiver computes LPrime required during locality check and makes
* LCSendLPrime message available for reading by the transmitter.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     When an unexpected or malformed message is received
*           the system is reset to the default state B0 to allow
*           graceful recovery from error.
******************************************************************************/
static void *XHdcp22Rx_StateB2(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATION_BUSY;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}

	/* Check if message is available */
	InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);

	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT: /* Transition B2->B1 */
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);
			if(Status == XST_SUCCESS)
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
				return (void *)XHdcp22Rx_StateB1;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_LCINIT:
			/* Maximum of 1024 locality check attempts allowed */
			if(InstancePtr->Info.CurrentState == XHDCP22_RX_STATE_B1_WAIT_LCINIT ||
				InstancePtr->Info.CurrentState == XHDCP22_RX_STATE_B2_WAIT_SKESENDEKS)
			{
				if(InstancePtr->Info.LCInitAttempts <= XHDCP22_RX_MAX_LCINIT)
				{
					Status = XHdcp22Rx_ProcessMessageLCInit(InstancePtr);
					if(Status == XST_SUCCESS)
					{
						InstancePtr->Info.NextState = XHDCP22_RX_STATE_B2_SEND_LCSENDLPRIME;
						break;
					}
				}
				else
				{
					XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_MAX_LCINIT_ATTEMPTS);
				}
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_LCINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_SKESENDEKS: /* Transition B2->B3 */
			if(InstancePtr->Info.CurrentState == XHDCP22_RX_STATE_B2_WAIT_SKESENDEKS)
			{
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_B3_COMPUTE_KS;
			return (void *)XHdcp22Rx_StateB3;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_SKESENDEKS);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	/* Message send */
	switch(InstancePtr->Info.NextState)
	{
	case XHDCP22_RX_STATE_B2_SEND_LCSENDLPRIME:
		if(XHdcp22Rx_IsReadMessageComplete(InstancePtr) == TRUE)
		{
			Status = XHdcp22Rx_SendMessageLCSendLPrime(InstancePtr);
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_B2_WAIT_SKESENDEKS;
		}
		break;
	default:
		break;
	}

	return (void *)XHdcp22Rx_StateB2; /* Transition B2->B2 */
}

/*****************************************************************************/
/**
* This function implements the Receiver State B3 (ComputeKs). The
* receiver decrypts Edkey(Ks) to derive Ks. The cipher is updated
* with the session key and enabled within 200ms after the encrypted
* session key is received.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     When an unexpected or malformed message is received the
*           system is reset to the default state B0 to allow graceful
*           recovery from error.
******************************************************************************/
static void *XHdcp22Rx_StateB3(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATION_BUSY;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}

	/* Compute Ks */
	XHdcp22Rx_ProcessMessageSKESendEks(InstancePtr);

	/* Transition to next state is conditioned on operating mode */
	if(InstancePtr->Config.Mode == XHDCP22_RX_RECEIVER)
	{
		InstancePtr->Info.NextState = XHDCP22_RX_STATE_B4_AUTHENTICATED;
		return (void *)XHdcp22Rx_StateB4; /* Transition B3->B4, Receiver */
	}
	else
	{
		InstancePtr->Info.NextState = XHDCP22_RX_STATE_C4_WAIT_FOR_DOWNSTREAM;
		return (void *)XHdcp22Rx_StateC4; /* Transition B3->C4, Repeater */
	}
}

/*****************************************************************************/
/**
* This function implements the Receiver State B4 (Authenticated). The
* receiver has completed the authentication protocol. This function
* executes the XHDCP22_RX_HANDLER_AUTHENTICATED user callback function
* on transition to this state. An ongoing link integrity check is performed
* external to the HDCP22-RX system to detect synchronization mismatches
* between transmitter and receiver. RxStatus REAUTH_REQ bit is set if
* 50 consecutive data island CRC errors are detected. Setting the REAUTH_REQ
* bit signals the transmitter to re-initiate authentication.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     When an unexpected or malformed message is received the
*           system is reset to the default state B0 to allow graceful
*           recovery from error.
******************************************************************************/
static void *XHdcp22Rx_StateB4(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Run callback function type: XHDCP22_RX_HANDLER_AUTHENTICATED */
	if(InstancePtr->Info.CurrentState != InstancePtr->Info.NextState)
	{
		if(InstancePtr->Handles.IsAuthenticatedCallbackSet)
		{
			InstancePtr->Handles.AuthenticatedCallback(InstancePtr->Handles.AuthenticatedCallbackRef);
		}

	  /* Start 1 second timer */
	  XHdcp22Rx_StartTimer(InstancePtr, XHDCP22_RX_ENCRYPTION_STATUS_INTERVAL, 0);
	}

	/* Run callback function type: XHDCP22_RX_HANDLER_ENCRYPTION_UPDATE */
	if(InstancePtr->Info.TimerExpired == TRUE)
	{
	  Status = XHdcp22Cipher_IsEncrypted(&InstancePtr->CipherInst);

	/* Encryption Enabled */
	  if((InstancePtr->Info.IsEncrypted != Status)) {
		  if(InstancePtr->Handles.IsEncryptionStatusCallbackSet)
		  {
			InstancePtr->Handles.EncryptionStatusCallback(InstancePtr->Handles.EncryptionStatusCallbackRef);
		  }
	}

	  InstancePtr->Info.IsEncrypted = Status;

	  /* Start 1 second timer */
	  XHdcp22Rx_StartTimer(InstancePtr, XHDCP22_RX_ENCRYPTION_STATUS_INTERVAL, 0);
	}

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATED;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}
	else if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY)
	{
		XHdcp22Rx_SetDdcReauthReq(InstancePtr);
		InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_REAUTHENTICATE_REQUESTED;
	}

	/* Check if message is available */
	InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);

	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT: /* Transition B4->B1 */
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);

			if(Status == XST_SUCCESS)
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
				return (void *)XHdcp22Rx_StateB1;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	return (void *)XHdcp22Rx_StateB4; /* Transition B4->B4 */
}

/*****************************************************************************/
/**
* This function implements the Repeater State C4 (WaitForDownstream). In this
* state the receiver upstream is waiting for all downstream HDCP-protected
* interface ports of the HDCP Repeater to enter one of the following states:
* Unconnected (State P0), Unauthenticated (State P1), Authenticated (State F5),
* or ReceiverIdListAck (State F8). This state checks if the topology table
* is available for upstream propogation based on the flag set in the function
* XHdcp22Rx_SetTopologyUpdate by the higher level repeater function.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     If a link integrity problem is detected during this state
*           then the REAUTH_REQ bit is set in the RxStatus register.
*           If the RepeaterAuth_Stream_Manage message is received then
*           transition to State C7 then return back to this state.
******************************************************************************/
static void *XHdcp22Rx_StateC4(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Run topology update callback */
	if((InstancePtr->Info.CurrentState != InstancePtr->Info.NextState) &&
	(InstancePtr->Handles.IsTopologyUpdateCallbackSet))
	{
		InstancePtr->Handles.TopologyUpdateCallback(
			InstancePtr->Handles.TopologyUpdateCallbackRef);
	}

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATION_BUSY;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}
	else if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY)
	{
		XHdcp22Rx_SetDdcReauthReq(InstancePtr);
		InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_REAUTHENTICATE_REQUESTED;
	}

	/* Check if message is available */
	InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);

	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT: /* Transition C4->B1 */
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);
			if(Status == XST_SUCCESS)
			{
			    InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
			    return (void *)XHdcp22Rx_StateB1;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
				XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMMANAGE: /* Transition C4->C7 */
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_C7_WAIT_STREAM_MANAGEMENT;
			InstancePtr->Info.ReturnState = InstancePtr->Info.CurrentState;
			InstancePtr->Info.SkipRead = TRUE;
			return (void *)XHdcp22Rx_StateC7;
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	/* Check the topology update flag */
	if(InstancePtr->Info.IsTopologyValid == TRUE)
	{
		InstancePtr->Info.IsTopologyValid = FALSE;
		InstancePtr->Info.NextState = XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST;
		return (void *)XHdcp22Rx_StateC5; /* Transition C4->C5 */
	}

	return (void *)XHdcp22Rx_StateC4; /* Transition C4->C4 */
}

/*****************************************************************************/
/**
* This function implements the Repeater State C5 (AssembleReceiverIdList).
* If MAX_DEVS_EXCEEDED or MAX_CASCADE_EXCEEDED is not set then this state
* calculates VPrime based on the Receiver ID List in the topology table.
* After VPrime is calculated the Repeaterauth_Send_ReceiverID_List message
* is populated based on the topology table, VPrime computation, and
* current seq_num_V counter value. seq_num_V is initialized to a value
* of zero after AKE_Init and is incremented by one after the transmission
* of every RepeaterAuth_Send_ReceiverID_List message. The upstream
* transmitter is responsible for detecting roll over of seq_num_V.
* The READY bit is asserted once the Repeaterauth_Send_ReceiverID_List
* message is made available to the upstream and a watchdog timer is set
* to detect if the acknowledgement arrives within a 2 second period.
* If the topology maximum flags are asserted in the topology table then
* the state machine transitions to StateC0 immediately after the
* Repeaterauth_Send_ReceiverID_List message is read, and the
* REAUTH_REQ bit is set the in the RxStatus register.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     If a link integrity problem is detected during this state
*           then the REAUTH_REQ bit is set in the RxStatus register.
*           If the RepeaterAuth_Stream_Manage message is received then
*           transition to State C7 then return back to this state.
******************************************************************************/
static void *XHdcp22Rx_StateC5(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATION_BUSY;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}
	else if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY)
	{
		XHdcp22Rx_SetDdcReauthReq(InstancePtr);
		InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_REAUTHENTICATE_REQUESTED;
	}

	/* Check if message is available */
	InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);

	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT: /* Transition C5->B1 */
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);
			if(Status == XST_SUCCESS)
			{
			    InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
			    return (void *)XHdcp22Rx_StateB1;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
				XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMMANAGE: /* Transition C5->C7 */
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_C7_WAIT_STREAM_MANAGEMENT;
			InstancePtr->Info.ReturnState = InstancePtr->Info.CurrentState;
			InstancePtr->Info.SkipRead = TRUE;
			return (void *)XHdcp22Rx_StateC7;
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	/* Message send */
	switch(InstancePtr->Info.NextState)
	{
	case XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST:
		if(XHdcp22Rx_IsReadMessageComplete(InstancePtr) == TRUE)
		{
			Status = XHdcp22Rx_SendMessageRepeaterAuthSendRxIdList(InstancePtr);
			if(Status == XST_SUCCESS)
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST_DONE;
			}
			else
			{
				XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
					XHDCP22_RX_ERROR_FLAG_EMPTY_REPEATER_TOPOLOGY);
				XHdcp22Rx_ResetAfterError(InstancePtr);
				XHdcp22Rx_SetDdcReauthReq(InstancePtr);
				return (void *)XHdcp22Rx_StateB0; /* Transition C5->B0 */
			}
		}
		break;
	case XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST_DONE:
		if(XHdcp22Rx_IsReadMessageComplete(InstancePtr) == TRUE)
		{
			XHdcp22Rx_ResetDdc(InstancePtr, FALSE, FALSE, TRUE, FALSE);

			if(InstancePtr->Topology.MaxDevsExceeded == TRUE ||
				InstancePtr->Topology.MaxCascadeExceeded == TRUE)
			{
				XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
					XHDCP22_RX_ERROR_FLAG_MAX_REPEATER_TOPOLOGY);
				XHdcp22Rx_ResetAfterError(InstancePtr);
				XHdcp22Rx_SetDdcReauthReq(InstancePtr);
				return (void *)XHdcp22Rx_StateB0; /* Transition C5->B0 */
			}
			else
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_C6_VERIFY_RECEIVERIDLISTACK;
				return (void *)XHdcp22Rx_StateC6; /* Transition C5->C6 */
			}
		}
		break;
	default:
		break;
	}

	return (void *)XHdcp22Rx_StateC5; /* Transition C5->C5 */
}

/*****************************************************************************/
/**
* This function implements the Repeater State C6 (VerifyReceiverIdListAck).
* The RepeaterAuth_Send_Ack message should be available for the receiver
* upstream interface within two seconds from when the READY bit in the
* RxStatus register was set in State C5. If the message is not
* available within the two second window then the state machine
* transitions back into the un-authenticated state. If the message
* is received in-time and the least significant 128-bits of V and VPrime
* match, this indicated successful upstream transmission of topology
* information and triggers a transition to State C7. If there is a
* mismatch then the state machine transitions back to the un-authenticated
* state, and the REAUTH_REQ bit is set in the RxStatus Register.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     If a link integrity problem is detected during this state
*           then the REAUTH_REQ bit is set in the RxStatus register.
*           If the RepeaterAuth_Stream_Manage message is received then
*           transition to State C7 then return back to this state.
******************************************************************************/
static void *XHdcp22Rx_StateC6(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATION_BUSY;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}
	else if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY)
	{
		XHdcp22Rx_SetDdcReauthReq(InstancePtr);
		InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_REAUTHENTICATE_REQUESTED;
	}

	/* Check timeout for RepeaterAuth_Send_Ack message */
	if(InstancePtr->Info.TimerExpired == TRUE)
	{
		XHdcp22Rx_ResetAfterError(InstancePtr);
		XHdcp22Rx_SetDdcReauthReq(InstancePtr);
		return (void *)XHdcp22Rx_StateB0; /* Transition C6->B0 */
	}

	/* Check if message is available.
	   When SkipRead flag is set, this indicates that a message has
	   already been read by another state and should get processed
	   by this state; therefore, skip the first read. */
	if(InstancePtr->Info.SkipRead != TRUE)
	{
		InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);
	}
	InstancePtr->Info.SkipRead = FALSE;


	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT: /* Transition C6->B1 */
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);
			if(Status == XST_SUCCESS)
			{
			    InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
			    return (void *)XHdcp22Rx_StateB1;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
				XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMMANAGE: /* Transition C6->C7 */
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_C7_WAIT_STREAM_MANAGEMENT;
			InstancePtr->Info.ReturnState = InstancePtr->Info.CurrentState;
			InstancePtr->Info.SkipRead = TRUE;
			return (void *)XHdcp22Rx_StateC7;
		case XHDCP22_RX_MSG_ID_REPEATERAUTHSENDACK:
			Status = XHdcp22Rx_ProcessMessageRepeaterAuthSendAck(InstancePtr);
			if(Status == XST_SUCCESS)
			{
				if(InstancePtr->Info.HasStreamManagementInfo == TRUE) /* Transition C6->C8 */
				{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_C8_AUTHENTICATED;
				return (void *)XHdcp22Rx_StateC8;
				}
				else /* Transition C6->C7 */
				{
					if(InstancePtr->Info.ReturnState == XHDCP22_RX_STATE_UNDEFINED)
					{
					InstancePtr->Info.NextState = XHDCP22_RX_STATE_C7_WAIT_STREAM_MANAGEMENT;
					return (void *)XHdcp22Rx_StateC7;
					}
					else
					{
						InstancePtr->Info.NextState = InstancePtr->Info.ReturnState;
						switch(InstancePtr->Info.ReturnState)
						{
						case XHDCP22_RX_STATE_C7_SEND_STREAM_READY:
							InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
							return (void *)XHdcp22Rx_StateC7;
						default:
							break;
						}
					}
				}
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
				XHDCP22_RX_ERROR_FLAG_PROCESSING_REPEATERAUTHSENDACK);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			XHdcp22Rx_SetDdcReauthReq(InstancePtr);
			return (void *)XHdcp22Rx_StateB0; /* Transition C6->B0 */
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	return (void *)XHdcp22Rx_StateC6; /* Transition C6->C6 */
}

/*****************************************************************************/
/**
* This function implements the Repeater State C7 (ContentStreamManagement).
* This state waits to receive the RepeaterAuth_Stream_Manage message from
* the upstream transmitter. Once the message is received the MPrime value is
* computed and made available to the upstream transmitter to read as part of
* the RepeaterAuth_Stream_Ready message. Note that state executes in parallel
* with the upstream propagation of topology information (State C4, State C5,
* and State C6). A transition in to this state may occur from State C4,
* State C5, or State C6 if content stream management information is received
* from the upstream transmitter. Also, the transition from State C7 may
* return to the appropriate state to allow undisrupted operation.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     If a link integrity problem is detected during this state
*           then the REAUTH_REQ bit is set in the RxStatus register.
*           If the RepeaterAuth_Stream_Manage message is received then
*           transition to State C7 then return back to this state.
******************************************************************************/
static void *XHdcp22Rx_StateC7(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATION_BUSY;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}
	else if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY)
	{
		XHdcp22Rx_SetDdcReauthReq(InstancePtr);
		InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_REAUTHENTICATE_REQUESTED;
	}

	/* Check if message is available.
	   When SkipRead flag is set, this indicates that a message has
	   already been read by another state and should get processed
	   by this state; therefore, skip the first read. */
	if(InstancePtr->Info.SkipRead != TRUE)
	{
		InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);
	}
	InstancePtr->Info.SkipRead = FALSE;

	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT: /* Transition C7->B1 */
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);
			if(Status == XST_SUCCESS)
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
				return (void *)XHdcp22Rx_StateB1;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
				XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_REPEATERAUTHSENDACK:
			InstancePtr->Info.NextState = InstancePtr->Info.ReturnState;
			InstancePtr->Info.ReturnState = InstancePtr->Info.CurrentState;
			InstancePtr->Info.SkipRead = TRUE;
			return (void *)XHdcp22Rx_StateC6; /* Transition C7->C6 */
		case XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMMANAGE:
			Status = XHdcp22Rx_ProcessMessageRepeaterAuthStreamManage(InstancePtr);
			if(Status == XST_SUCCESS)
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_C7_SEND_STREAM_READY;
				break;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
				XHDCP22_RX_ERROR_FLAG_PROCESSING_REPEATERAUTHSTREAMMANAGE);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			XHdcp22Rx_SetDdcReauthReq(InstancePtr);
			return (void *)XHdcp22Rx_StateB0; /* Transition C7->B0 */
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	/* Message send */
	switch(InstancePtr->Info.NextState)
	{
	case XHDCP22_RX_STATE_C7_SEND_STREAM_READY:
		if(XHdcp22Rx_IsReadMessageComplete(InstancePtr) == TRUE)
		{
			Status = XHdcp22Rx_SendMessageRepeaterAuthStreamReady(InstancePtr);
			InstancePtr->Info.HasStreamManagementInfo = TRUE;
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_C7_SEND_STREAM_READY_DONE;
		}
		break;
	case XHDCP22_RX_STATE_C7_SEND_STREAM_READY_DONE:
		if(XHdcp22Rx_IsReadMessageComplete(InstancePtr) == TRUE)
		{
			if(InstancePtr->Info.ReturnState == XHDCP22_RX_STATE_UNDEFINED)
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_C8_AUTHENTICATED;
				return (void *)XHdcp22Rx_StateC8; /* Transition C7->C8 */
			}
			else
			{
				InstancePtr->Info.NextState = InstancePtr->Info.ReturnState;
				switch(InstancePtr->Info.ReturnState)
				{
				case XHDCP22_RX_STATE_C4_WAIT_FOR_DOWNSTREAM:
					InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
					return (void *)XHdcp22Rx_StateC4; /* Transition C7->C4 */
				case XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST:
					InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
					return (void *)XHdcp22Rx_StateC5; /* Transition C7->C5 */
				case XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST_DONE:
					InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
					return (void *)XHdcp22Rx_StateC5; /* Transition C7->C5 */
				case XHDCP22_RX_STATE_C6_VERIFY_RECEIVERIDLISTACK:
					InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
					return (void *)XHdcp22Rx_StateC6; /* Transition C7->C6 */
				case XHDCP22_RX_STATE_C8_AUTHENTICATED:
					InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
					return (void *)XHdcp22Rx_StateC8; /* Transition C7->C8 */
				default:
					break;
				}
			}
		}
		break;
	default:
		break;
	}

	return (void *)XHdcp22Rx_StateC7; /* Transition C7->C7 */
}

/*****************************************************************************/
/**
* This function implements the Receiver State C8 (Authenticated). The
* repeater has completed the authentication protocol. Upon detection
* of any changes to the topology a transition back to State C5
* (AssembleReceiverIdList) occurs. Change to the topology occur when
* a repeater downstream port has transitioned to the following:
* authenticated, unauthenticated, or unconnected. This function
* executes the XHDCP22_RX_HANDLER_AUTHENTICATED user callback function
* on transition to this state. An ongoing link integrity check is performed
* external to the HDCP22-RX system to detect synchronization mismatches
* between transmitter and receiver. RxStatus REAUTH_REQ bit is set if
* 50 consecutive data island CRC errors are detected. Setting the REAUTH_REQ
* bit signals the transmitter to re-initiate authentication.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   Pointer to the function that implements the next state.
*
* @note     When an unexpected or malformed message is received the
*           system is reset to the default state B0 to allow graceful
*           recovery from error.
******************************************************************************/
static void *XHdcp22Rx_StateC8(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Run callback function type: XHDCP22_RX_HANDLER_AUTHENTICATED
	   Only run once and clear params */
	if(InstancePtr->Info.CurrentState != InstancePtr->Info.NextState)
	{
	    if(InstancePtr->Handles.IsAuthenticatedCallbackSet)
		{
			InstancePtr->Handles.AuthenticatedCallback(InstancePtr->Handles.AuthenticatedCallbackRef);
		}

	  /* Start 1 second timer */
	  XHdcp22Rx_StartTimer(InstancePtr, XHDCP22_RX_ENCRYPTION_STATUS_INTERVAL, 0);
	}

	/* Run callback function type: XHDCP22_RX_HANDLER_ENCRYPTION_UPDATE */
	if(InstancePtr->Info.TimerExpired == TRUE)
	{
	  Status = XHdcp22Cipher_IsEncrypted(&InstancePtr->CipherInst);

	/* Encryption Enabled */
	  if((InstancePtr->Info.IsEncrypted != Status)) {
		  if(InstancePtr->Handles.IsEncryptionStatusCallbackSet)
		  {
			InstancePtr->Handles.EncryptionStatusCallback(InstancePtr->Handles.EncryptionStatusCallbackRef);
		  }
	}

	  InstancePtr->Info.IsEncrypted = Status;

	  /* Start 1 second timer */
	  XHdcp22Rx_StartTimer(InstancePtr, XHDCP22_RX_ENCRYPTION_STATUS_INTERVAL, 0);
	}

	/* Update state */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_AUTHENTICATED;
	InstancePtr->Info.CurrentState = InstancePtr->Info.NextState;

	/* Check error condition */
	if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_DDC_BURST)
	{
		XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);
		XHdcp22Rx_ResetAfterError(InstancePtr);
		return (void *)XHdcp22Rx_StateB0;
	}
	else if(InstancePtr->Info.ErrorFlag & XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY)
	{
		XHdcp22Rx_SetDdcReauthReq(InstancePtr);
		InstancePtr->Info.AuthenticationStatus = XHDCP22_RX_REAUTHENTICATE_REQUESTED;
	}

	/* Check if message is available */
	InstancePtr->MessageSize = XHdcp22Rx_PollMessage(InstancePtr);

	/* Message handling */
	if(InstancePtr->MessageSize > 0)
	{
		switch(MsgPtr->MsgId)
		{
		case XHDCP22_RX_MSG_ID_AKEINIT: /* Transition C8->B1 */
			Status = XHdcp22Rx_ProcessMessageAKEInit(InstancePtr);
			if(Status == XST_SUCCESS)
			{
				InstancePtr->Info.NextState = XHDCP22_RX_STATE_B1_SEND_AKESENDCERT;
				return (void *)XHdcp22Rx_StateB1;
			}
			XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT);
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		case XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMMANAGE:
			InstancePtr->Info.NextState = XHDCP22_RX_STATE_C7_WAIT_STREAM_MANAGEMENT;
			InstancePtr->Info.ReturnState = InstancePtr->Info.CurrentState;
			InstancePtr->Info.SkipRead = TRUE;
			return (void *)XHdcp22Rx_StateC7;
		default:
			XHdcp22Rx_ResetAfterError(InstancePtr);
			return (void *)XHdcp22Rx_StateB0;
		}
	}

	/* Check the topology update flag */
	if(InstancePtr->Info.IsTopologyValid == TRUE)
	{
		InstancePtr->Info.IsTopologyValid = FALSE;
		InstancePtr->Info.NextState = XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST;
		return (void *)XHdcp22Rx_StateC5; /* Transition C8->C5 */
	}

	return (void *)XHdcp22Rx_StateC8; /* Transition C8->C8 */
}

/*****************************************************************************/
/**
* This function processes the AKE_Init message received from the transmitter.
* The stored authentication parameters are reset so that no state
* information is stored between authentication attempts. The DDC
* registers are reset to their default value including the REAUTH_REQ
* flag. The cipher is disabled.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS if message processed successfully.
*           - XST_FAILURE if message size is incorrect.
*
* @note     None.
******************************************************************************/
static int XHdcp22Rx_ProcessMessageAKEInit(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Log message read completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_AKEINIT);

	/* Increment authentication request count */
	InstancePtr->Info.AuthRequestCnt++;

	/* Reset cipher */
	XHdcp22Cipher_Disable(&InstancePtr->CipherInst);
	XHdcp22Cipher_Enable(&InstancePtr->CipherInst);

	/* Reset timer counter */
	XTmrCtr_Reset(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_0);
	XHdcp22Rx_StopTimer(InstancePtr);

	/* Reset repeater values */
	memset(&InstancePtr->Topology, 0, sizeof(XHdcp22_Rx_Topology));
	InstancePtr->Info.ReauthReq = FALSE;
	InstancePtr->Info.TopologyReady = FALSE;
	InstancePtr->Info.IsTopologyValid = FALSE;
	InstancePtr->Info.ReturnState = XHDCP22_RX_STATE_UNDEFINED;
	InstancePtr->Info.SeqNumV = 0;
	InstancePtr->Info.HasStreamManagementInfo = FALSE;
	InstancePtr->Info.SkipRead = FALSE;

	/* Check message size */
	if(InstancePtr->MessageSize != sizeof(XHdcp22_Rx_AKEInit))
	{
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE);
		return XST_FAILURE;
	}

	/* Reset state variables and DDC registers */
	XHdcp22Rx_ResetParams(InstancePtr);
	XHdcp22Rx_ResetDdc(InstancePtr, FALSE, TRUE, TRUE, TRUE);

	/* Record Rtx and TxCaps parameters */
	memcpy(InstancePtr->Params.Rtx, MsgPtr->AKEInit.Rtx, XHDCP22_RX_RTX_SIZE);
	memcpy(InstancePtr->Params.TxCaps, MsgPtr->AKEInit.TxCaps, XHDCP22_RX_TXCAPS_SIZE);

	/* Run callback function type: XHDCP22_RX_HANDLER_UNAUTHENTICATED */
	if((InstancePtr->Handles.IsUnauthenticatedCallbackSet) &&
		 (InstancePtr->Info.AuthenticationStatus == XHDCP22_RX_AUTHENTICATED))
	{
		InstancePtr->Handles.UnauthenticatedCallback(InstancePtr->Handles.UnauthenticatedCallbackRef);
	}

	/* Run callback function type: XHDCP22_RX_HANDLER_AUTHENTICATION_REQUEST */
	if(InstancePtr->Handles.IsAuthenticationRequestCallbackSet)
	{
		InstancePtr->Handles.AuthenticationRequestCallback(
			InstancePtr->Handles.AuthenticationRequestCallbackRef);
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function generates the AKE_Send_Cert message and writes it into the
* read message buffer. After the complete message has been written to the
* buffer the MessageSize is set in the DDC RxStatus register signaling
* the transmitter that the message is available for reading.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS if message written successfully.
*           - XST_FAILURE if generation of random integer Rrx failed.
*
* @note     This message must be available for the transmitter with 100ms
*           after receiving AKEInit.
******************************************************************************/
static int XHdcp22Rx_SendMessageAKESendCert(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u32 Offset = 0;
	int Status = XST_SUCCESS;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Generate AKE_Send_Cert message */
	MsgPtr->AKESendCert.MsgId = XHDCP22_RX_MSG_ID_AKESENDCERT;
	memcpy(MsgPtr->AKESendCert.RxCaps, InstancePtr->RxCaps, XHDCP22_RX_RXCAPS_SIZE);
	Status = XHdcp22Rx_GenerateRrx(InstancePtr, MsgPtr->AKESendCert.Rrx);
	memcpy(MsgPtr->AKESendCert.CertRx, InstancePtr->PublicCertPtr, XHDCP22_RX_CERT_SIZE);

	/* Write message to read buffer */
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef,
		XHDCP22_RX_DDC_READ_REG);
	for(Offset = 0; Offset < sizeof(XHdcp22_Rx_AKESendCert); Offset++)
	{
		InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef,
			InstancePtr->MessageBuffer[Offset]);
	}

	/* Write message size signaling completion */
	XHdcp22Rx_SetRxStatus(InstancePtr, sizeof(XHdcp22_Rx_AKESendCert),
		InstancePtr->Info.ReauthReq, InstancePtr->Info.TopologyReady);

	/* Record Rrx and RxCaps */
	memcpy(InstancePtr->Params.Rrx, MsgPtr->AKESendCert.Rrx, XHDCP22_RX_RRX_SIZE);
	memcpy(InstancePtr->Params.RxCaps, MsgPtr->AKESendCert.RxCaps, XHDCP22_RX_RXCAPS_SIZE);

	/* Log message write completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_AKESENDCERT);

	return Status;
}

/*****************************************************************************/
/**
* This function processes the AKE_No_Stored_km message received from the
* transmitter. The RSAES-OAEP operation decrypts Km with the receiver
* private key.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS if message processed successfully.
*           - XST_FAILURE if message size is incorrect or Km
*             decryption failed.
*
* @note     None.
******************************************************************************/
static int XHdcp22Rx_ProcessMessageAKENoStoredKm(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Size;
	int Status = XST_SUCCESS;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Log message read completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_AKENOSTOREDKM);

	/* Check message size */
	if(InstancePtr->MessageSize != sizeof(XHdcp22_Rx_AKENoStoredKm))
	{
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE);
		return XST_FAILURE;
	}

	/* Compute Km, Perform RSA decryption */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_KM);
	Status = XHdcp22Rx_RsaesOaepDecrypt(InstancePtr, (XHdcp22_Rx_KprivRx *)(InstancePtr->PrivateKeyPtr),
										MsgPtr->AKENoStoredKm.EKpubKm, InstancePtr->Params.Km, &Size);
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_KM_DONE);

	return (Status == XST_SUCCESS && Size == XHDCP22_RX_KM_SIZE) ? XST_SUCCESS : XST_FAILURE;
}

/*****************************************************************************/
/**
* This function processes the AKE_Stored_km message received from the
* transmitter. Decrypts Ekh(Km) using AES with the received m as
* input and Kh as key into the AES module.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS if message processed successfully.
*           - XST_FAILURE if message size is incorrect.
*
* @note     None.
******************************************************************************/
static int XHdcp22Rx_ProcessMessageAKEStoredKm(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status = XST_SUCCESS;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Log message read completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_AKESTOREDKM);

	/* Check message size */
	if(InstancePtr->MessageSize != sizeof(XHdcp22_Rx_AKEStoredKm))
	{
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE);
		return XST_FAILURE;
	}

	/* Compute Km */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_KM);
	XHdcp22Rx_ComputeEkh(InstancePtr->PrivateKeyPtr, MsgPtr->AKEStoredKm.EKhKm, MsgPtr->AKEStoredKm.M,
		InstancePtr->Params.Km);
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_KM_DONE);

	return Status;
}

/*****************************************************************************/
/**
* This function computes HPrime, generates AKE_Send_H_prime message, and
* writes it into the read message buffer. After the complete message has
* been written to the buffer the MessageSize is set in the DDC RxStatus
* register signaling the transmitter that the message is available for
* reading.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   XST_SUCCESS.
*
* @note     This message must be available for the transmitter within
*           1s after receiving AKENoStoredKm or 200ms after receiving
*           AKEStoredKm.
******************************************************************************/
static int XHdcp22Rx_SendMessageAKESendHPrime(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u32 Offset;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Compute H Prime */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_HPRIME);
	XHdcp22Rx_ComputeHPrime(InstancePtr->Params.Rrx, InstancePtr->Params.RxCaps,
			InstancePtr->Params.Rtx, InstancePtr->Params.TxCaps, InstancePtr->Params.Km,
			MsgPtr->AKESendHPrime.HPrime);
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_HPRIME_DONE);

	/* Generate AKE_Send_H_prime message */
	MsgPtr->AKESendHPrime.MsgId = XHDCP22_RX_MSG_ID_AKESENDHPRIME;

	/* Write message to buffer */
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef,
		XHDCP22_RX_DDC_READ_REG);
	for(Offset = 0; Offset < sizeof(XHdcp22_Rx_AKESendHPrime); Offset++)
	{
		InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef,
			InstancePtr->MessageBuffer[Offset]);
	}

	/* Write message size signaling completion */
	XHdcp22Rx_SetRxStatus(InstancePtr, sizeof(XHdcp22_Rx_AKESendHPrime),
		InstancePtr->Info.ReauthReq, InstancePtr->Info.TopologyReady);

	/* Record HPrime */
	memcpy(InstancePtr->Params.HPrime, MsgPtr->AKESendHPrime.HPrime, XHDCP22_RX_HPRIME_SIZE);

	/* Log message write completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_AKESENDHPRIME);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function computes Ekh(Km), generates AKE_Send_Pairing_Info message,
* and writes it into the read message buffer. After the complete message
* has been written to the buffer the MessageSize is set in the DDC RxStatus
* register signaling the transmitter that the message is available for
* reading.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   XST_SUCCESS.
*
* @note     The AKESendPairingInfo message is sent only in response to
*           receiving AKENoStoredKm message. This message must be
*           available for the transmitter within 200ms after sending
*           AKESendHPrime.
******************************************************************************/
static int XHdcp22Rx_SendMessageAKESendPairingInfo(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u8 M[XHDCP22_RX_RTX_SIZE+XHDCP22_RX_RRX_SIZE];
	u8 EKhKm[XHDCP22_RX_EKH_SIZE];
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;
	u32 Offset;

	/* Concatenate M = (Rtx || Rrx) */
	memcpy(M, InstancePtr->Params.Rtx, XHDCP22_RX_RTX_SIZE);
	memcpy(M+XHDCP22_RX_RTX_SIZE, InstancePtr->Params.Rrx, XHDCP22_RX_RRX_SIZE);

	/* Compute Ekh */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_EKH);
	XHdcp22Rx_ComputeEkh(InstancePtr->PrivateKeyPtr, InstancePtr->Params.Km, M, EKhKm);
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_EKH_DONE);

	/* Generate AKE_Send_Pairing_Info message */
	MsgPtr->AKESendPairingInfo.MsgId = XHDCP22_RX_MSG_ID_AKESENDPAIRINGINFO;
	memcpy(MsgPtr->AKESendPairingInfo.EKhKm, EKhKm, XHDCP22_RX_EKH_SIZE);

	/* Write message to buffer */
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef,
		XHDCP22_RX_DDC_READ_REG);
	for(Offset = 0; Offset < sizeof(XHdcp22_Rx_AKESendPairingInfo); Offset++)
	{
		InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef,
			InstancePtr->MessageBuffer[Offset]);
	}

	/* Write message size signaling completion */
	XHdcp22Rx_SetRxStatus(InstancePtr, sizeof(XHdcp22_Rx_AKESendPairingInfo),
		InstancePtr->Info.ReauthReq, InstancePtr->Info.TopologyReady);

	/* Record Ekh */
	memcpy(InstancePtr->Params.EKh, EKhKm, XHDCP22_RX_EKH_SIZE);

	/* Log message write completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_AKESENDPAIRINGINFO);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function processes the LC_Init message received from the
* transmitter. The locality check attempts is incremented for
* each LCInit message received. The random nonce Rn is recorded.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS if message processed successfully.
*           - XST_FAILURE if message size is incorrect.
*
* @note     None.
******************************************************************************/
static int XHdcp22Rx_ProcessMessageLCInit(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Log message read completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_LCINIT);

	/* Check message size */
	if(InstancePtr->MessageSize != sizeof(XHdcp22_Rx_LCInit))
	{
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE);
		return XST_FAILURE;
	}

	/* Update locality check attempts */
	InstancePtr->Info.LCInitAttempts++;

	/* Record Rn parameter */
	memcpy(InstancePtr->Params.Rn, MsgPtr->LCInit.Rn, XHDCP22_RX_RN_SIZE);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function computes LPrime, generates LC_Send_L_prime message,
* and writes it into the read message buffer. After the complete message
* has been written to the buffer the MessageSize is set in the DDC RxStatus
* register signaling the transmitter that the message is available for
* reading.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   XST_SUCCESS.
*
* @note     The LCSendLPrime message must be available for the transmitter
*           within 20ms after receiving LCInit.
******************************************************************************/
static int XHdcp22Rx_SendMessageLCSendLPrime(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;
	u32 Offset;

	/* Compute LPrime */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_LPRIME);
	XHdcp22Rx_ComputeLPrime(InstancePtr->Params.Rn, InstancePtr->Params.Km, InstancePtr->Params.Rrx,
		InstancePtr->Params.Rtx, MsgPtr->LCSendLPrime.LPrime);
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_LPRIME_DONE);

	/* Generate LC_Send_L_prime message */
	MsgPtr->LCSendLPrime.MsgId = XHDCP22_RX_MSG_ID_LCSENDLPRIME;

	/* Write message to buffer */
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef,
		XHDCP22_RX_DDC_READ_REG);
	for(Offset = 0; Offset < sizeof(XHdcp22_Rx_LCSendLPrime); Offset++)
	{
		InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef,
			InstancePtr->MessageBuffer[Offset]);
	}

	/* Write message size signaling completion */
	XHdcp22Rx_SetRxStatus(InstancePtr, sizeof(XHdcp22_Rx_LCSendLPrime),
		InstancePtr->Info.ReauthReq, InstancePtr->Info.TopologyReady);

	/* Record LPrime parameter */
	memcpy(InstancePtr->Params.LPrime, MsgPtr->LCSendLPrime.LPrime, XHDCP22_RX_LPRIME_SIZE);

	/* Log message write completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_LCSENDLPRIME);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function processes the SKE_Send_Eks message received from the
* transmitter. The session key Ks is decrypted and written to the
* cipher.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS if message processed successfully.
*           - XST_FAILURE if message size is incorrect.
*
* @note     The cipher must be enabled within 200ms after receiving
*           SKESendEks.
******************************************************************************/
static int XHdcp22Rx_ProcessMessageSKESendEks(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Log message read completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_SKESENDEKS);

	/* Check message size */
	if(InstancePtr->MessageSize != sizeof(XHdcp22_Rx_SKESendEks))
	{
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE);
		return XST_FAILURE;
	}

	/* Compute Ks */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_KS);
	XHdcp22Rx_ComputeKs(InstancePtr->Params.Rrx, InstancePtr->Params.Rtx, InstancePtr->Params.Km,
						InstancePtr->Params.Rn, MsgPtr->SKESendEks.EDkeyKs, InstancePtr->Params.Ks);
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_KS_DONE);

	/* Record Riv parameter */
	memcpy(InstancePtr->Params.Riv, MsgPtr->SKESendEks.Riv, XHDCP22_RX_RIV_SIZE);

	/* Load cipher Ks and Riv */
	XHdcp22Cipher_SetKs(&InstancePtr->CipherInst, InstancePtr->Params.Ks, XHDCP22_RX_KS_SIZE);
	XHdcp22Cipher_SetRiv(&InstancePtr->CipherInst, InstancePtr->Params.Riv, XHDCP22_RX_RIV_SIZE);

	/* Log info event */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO, XHDCP22_RX_LOG_INFO_ENCRYPTION_ENABLE);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function generates the RepeaterAuth_Send_ReceiverID_List message
* and writes it into the read message buffer. After the complete message
* has been written to the buffer the MessageSize and READY fields are set
* in the DDC RxStatus register signaling the transmitter that the message
* is available for reading. When the topology maximums are not exceeded
* VPrime is computed. seq_num_v is incremented after the transmission of
* each RepeaterAuth_Send_ReceiverID_List message. The READY bit in the
* RxStatus register is cleared automatically.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS if device count is equal to zero.
*           - XST_FAILURE if device count is greater than zero.
*
* @note     The RepeaterAuth_Send_ReceiverID_List must be available
*           and READY bit asserted in the RxStatus register for
*           the upstream transmitter within 3 seconds.
******************************************************************************/
static int XHdcp22Rx_SendMessageRepeaterAuthSendRxIdList(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;
	u32 Offset;
	u32 MessageSize;

	/* Set message ID */
	MsgPtr->RepeaterAuthSendRxIdList.MsgId      =
		XHDCP22_RX_MSG_ID_REPEATERAUTHSENDRXIDLIST;

	/* Set RxInfo bit fields */
	MsgPtr->RepeaterAuthSendRxIdList.RxInfo[0]  =
		(u8)((InstancePtr->Topology.Depth & 0x07) << 1);                    // RxInfo[11:9] = Depth[2:0]
	MsgPtr->RepeaterAuthSendRxIdList.RxInfo[0] |=
		(u8)((InstancePtr->Topology.DeviceCnt & 0x10) >> 4);                // RxInfo[8]    = DeviceCnt[4]
	MsgPtr->RepeaterAuthSendRxIdList.RxInfo[1]  =
		(u8)((InstancePtr->Topology.DeviceCnt & 0x0F) << 4);                // RxInfo[7:4]  = DeviceCnt[3:0]
	MsgPtr->RepeaterAuthSendRxIdList.RxInfo[1] |=
		(u8)((InstancePtr->Topology.MaxDevsExceeded & 0x01) << 3);          // RxInfo[3]    = MaxDevsExceeded
	MsgPtr->RepeaterAuthSendRxIdList.RxInfo[1] |=
		(u8)((InstancePtr->Topology.MaxCascadeExceeded & 0x01) << 2);       // RxInfo[2]    = MaxCascadeExceeded
	MsgPtr->RepeaterAuthSendRxIdList.RxInfo[1] |=
		(u8)((InstancePtr->Topology.Hdcp20RepeaterDownstream & 0x01) << 1); // RxInfo[1]    = Hdcp20RepeaterDownstream
	MsgPtr->RepeaterAuthSendRxIdList.RxInfo[1] |=
		(u8)(InstancePtr->Topology.Hdcp1DeviceDownstream & 0x01);           // RxInfo[0]    = Hdcp1DeviceDownstream

	/* Set seq_numb_V bytes */
	MsgPtr->RepeaterAuthSendRxIdList.SeqNumV[0] = (InstancePtr->Info.SeqNumV >> 16) & 0xFF;
	MsgPtr->RepeaterAuthSendRxIdList.SeqNumV[1] = (InstancePtr->Info.SeqNumV >> 8) & 0xFF;
	MsgPtr->RepeaterAuthSendRxIdList.SeqNumV[2] = (InstancePtr->Info.SeqNumV) & 0xFF;

	/* Set other message fields
	 * - When topology maximums are not exceeded send seq_num_V, VPrime, and Receiver ID List
	 * - Otherwise, only send RxInfo
	 */
	if(InstancePtr->Topology.MaxDevsExceeded != TRUE &&
		InstancePtr->Topology.MaxCascadeExceeded != TRUE)
	{
		/* Increment seq_num_V count with rollover */
		InstancePtr->Info.SeqNumV = (InstancePtr->Info.SeqNumV + 1) % XHDCP22_RX_MAX_SEQNUMV;

		/* Compute VPrime */
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_VPRIME);
		XHdcp22Rx_ComputeVPrime((u8 *)InstancePtr->Topology.ReceiverIdList,
			InstancePtr->Topology.DeviceCnt,
			MsgPtr->RepeaterAuthSendRxIdList.RxInfo,
			MsgPtr->RepeaterAuthSendRxIdList.SeqNumV,
			InstancePtr->Params.Km,
			InstancePtr->Params.Rrx,
			InstancePtr->Params.Rtx,
			InstancePtr->Params.VPrime);
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_VPRIME_DONE);

		/* Copy VPrime[0:16], most significant 128-bits */
		memcpy(MsgPtr->RepeaterAuthSendRxIdList.VPrime, InstancePtr->Params.VPrime, 16);

		/* Copy receiver ID list */
		memcpy(MsgPtr->RepeaterAuthSendRxIdList.ReceiverIdList,
				InstancePtr->Topology.ReceiverIdList,
				XHDCP22_RX_RCVID_SIZE*InstancePtr->Topology.DeviceCnt);

		/* Update message size */
		MessageSize = 22 + XHDCP22_RX_RCVID_SIZE*InstancePtr->Topology.DeviceCnt;
	}
	else
	{
		/* Update message size */
		MessageSize = 3;
	}

	/* Write message to buffer */
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef,
		XHDCP22_RX_DDC_READ_REG);
	for(Offset = 0; Offset < MessageSize; Offset++)
	{
		InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef,
			InstancePtr->MessageBuffer[Offset]);
	}

	/* Set the TopologyReady flag */
	InstancePtr->Info.TopologyReady = TRUE;

	/* Write message size and assert the RxStatus READY bit signaling completion */
	XHdcp22Rx_SetRxStatus(InstancePtr, MessageSize,
		InstancePtr->Info.ReauthReq, InstancePtr->Info.TopologyReady);

	/* Start 2 second timer */
	XHdcp22Rx_StartTimer(InstancePtr, XHDCP22_RX_REPEATERAUTH_ACK_INTERVAL, 0);

	/* Log message write completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_REPEATERAUTHSENDRXIDLIST);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function processes the RepeaterAuth_Send_Ack message received from
* the upstream transmitter. The least significant 128-bits of the value
* V are compared against VPrime. The timer is stopped.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS when V and VPrime match.
*           - XST_FAILURE when V and VPrime do not match.
*
* @note     The RepeaterAuth_Send_Ack message must be available for the
*           repeater upstream interface within 2 seconds after asserting
*           the READY bit in the RxStatus register.
******************************************************************************/
static int XHdcp22Rx_ProcessMessageRepeaterAuthSendAck(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Status;
	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Stop the timer */
	XHdcp22Rx_StopTimer(InstancePtr);

	/* Log message read completion */
	XHdcp22Rx_LogWr(InstancePtr,
		XHDCP22_RX_LOG_EVT_INFO_MESSAGE,
		XHDCP22_RX_MSG_ID_REPEATERAUTHSENDACK);

	/* Check message size */
	if(InstancePtr->MessageSize != sizeof(XHdcp22_Rx_RepeaterAuthSendAck))
	{
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR, XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE);
		return XST_FAILURE;
	}

	/* Compare LSb's of V and VPrime */
	Status = memcmp(MsgPtr->RepeaterAuthSendAck.V, InstancePtr->Params.VPrime+16, 16);

	return (Status == 0) ? XST_SUCCESS : XST_FAILURE;
}

/*****************************************************************************/
/**
* This function processes the RepeaterAuth_Repeater_Manage message.
* The message values seq_num_M and StreamID_Type are stored for
* later computation of MPrime. The user callback function
* type XHDCP22_RX_HANDLER_STREAM_MANAGE_REQUEST is executed.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   - XST_SUCCESS if message processed successfully.
*           - XST_FAILURE if message size is incorrect.
*
* @note     After receiving RepeaterAuth_Repeater_Manage message the
*           repeater upstream interface has 100ms to respond with the
*           RepeaterAuth_Stream_Ready message. Detection of seq_num_M
*           roll-over is the responsibility of the upstream transmitter.
******************************************************************************/
static int XHdcp22Rx_ProcessMessageRepeaterAuthStreamManage(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;

	/* Log message read completion */
	XHdcp22Rx_LogWr(InstancePtr,
		XHDCP22_RX_LOG_EVT_INFO_MESSAGE,
		XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMMANAGE);

	/* Check message size */
	if(InstancePtr->MessageSize != sizeof(XHdcp22_Rx_RepeaterAuthStreamManage))
	{
		XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_ERROR,
			XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE);
		return XST_FAILURE;
	}

	/* Record seq_num_M and StreamID_Type parameter */
	memcpy(InstancePtr->Params.SeqNumM,
		MsgPtr->RepeaterAuthStreamManage.SeqNumM,
		XHDCP22_RX_SEQNUMM_SIZE);
	memcpy(InstancePtr->Params.StreamIdType,
		MsgPtr->RepeaterAuthStreamManage.StreamIdType,
		XHDCP22_RX_STREAMID_SIZE);

	/* Run callback function type: XHDCP22_RX_HANDLER_STREAM_MANAGE_REQUEST */
	if(InstancePtr->Handles.IsStreamManageRequestCallbackSet)
	{
		InstancePtr->Handles.StreamManageRequestCallback(
			InstancePtr->Handles.StreamManageRequestCallbackRef);
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function computes MPrime and sends the RepeaterAuth_Stream_Ready
* message.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return   XST_SUCCESS.
*
* @note     None.
******************************************************************************/
static int XHdcp22Rx_SendMessageRepeaterAuthStreamReady(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	XHdcp22_Rx_Message *MsgPtr = (XHdcp22_Rx_Message*)InstancePtr->MessageBuffer;
	u32 Offset;

	/* Compute MPrime */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_MPRIME);
	XHdcp22Rx_ComputeMPrime(InstancePtr->Params.StreamIdType, InstancePtr->Params.SeqNumM,
		InstancePtr->Params.Km, InstancePtr->Params.Rrx, InstancePtr->Params.Rtx,
		MsgPtr->RepeaterAuthStreamReady.MPrime);
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_MPRIME_DONE);

	/* Generate RepeaterAuth_Stream_Ready message */
	MsgPtr->RepeaterAuthStreamReady.MsgId = XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMREADY;

	/* Write message to buffer */
	InstancePtr->Handles.DdcSetAddressCallback(InstancePtr->Handles.DdcSetAddressCallbackRef,
		XHDCP22_RX_DDC_READ_REG);
	for(Offset = 0; Offset < sizeof(XHdcp22_Rx_RepeaterAuthStreamReady); Offset++)
	{
		InstancePtr->Handles.DdcSetDataCallback(InstancePtr->Handles.DdcSetDataCallbackRef,
			InstancePtr->MessageBuffer[Offset]);
	}

	/* Write message size signaling completion */
	XHdcp22Rx_SetRxStatus(InstancePtr, sizeof(XHdcp22_Rx_RepeaterAuthStreamReady),
		InstancePtr->Info.ReauthReq, InstancePtr->Info.TopologyReady);

	/* Record MPrime parameter */
	memcpy(InstancePtr->Params.MPrime, MsgPtr->RepeaterAuthStreamReady.MPrime, XHDCP22_RX_MPRIME_SIZE);

	/* Log message write completion */
	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_INFO_MESSAGE, XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMREADY);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function sets DEPTH in the repeater topology table.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    Depth is the Repeater cascade depth. This value gives the
*           number of attached levels through the connection topology.
*           The depth cannot cannot exceed 4 levels.
*
* @return   None.
*
* @note     None.
******************************************************************************/
static void XHdcp22Rx_SetTopologyDepth(XHdcp22_Rx *InstancePtr, u8 Depth)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Depth <= XHDCP22_RX_MAX_DEPTH);

	InstancePtr->Topology.Depth = Depth;
}

/*****************************************************************************/
/**
* This function sets DEVICE_COUNT in the repeater topology table.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    DeviceCnt is the Total number of connected downstream devices.
*           Always zero for HDCP Receivers. The device count does not include
*           the HDCP Repeater itself, but only devices downstream from the
*           HDCP Repeater. The device count cannot exceed 31 devices.
*
* @return   None.
*
* @note     None.
******************************************************************************/
static void XHdcp22Rx_SetTopologyDeviceCnt(XHdcp22_Rx *InstancePtr, u8 DeviceCnt)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(DeviceCnt > 0);
	Xil_AssertVoid(DeviceCnt <= XHDCP22_RX_MAX_DEVICE_COUNT);

	InstancePtr->Topology.DeviceCnt = DeviceCnt;
}

/*****************************************************************************/
/**
* This function sets the MAX_DEVS_EXCEEDED error flag in the repeater
* topology table used to indicate a topology error. Setting the flag
* indicates that more than 31 downstream devices are attached.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    Value is either TRUE or FALSE.
*
* @return   None.
*
* @note     None.
******************************************************************************/
static void XHdcp22Rx_SetTopologyMaxDevsExceeded(XHdcp22_Rx *InstancePtr, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Value == FALSE || Value == TRUE);

	InstancePtr->Topology.MaxDevsExceeded = Value;
}

/*****************************************************************************/
/**
* This function sets the MAX_CASCADE_EXCEEDED flag in the repeater
* topology table used to indicate a topology error. Setting the error
* flag indicates that more than four levels of repeaters have been
* cascaded together.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    Value is either TRUE or FALSE.
*
* @return   None.
*
* @note     None.
******************************************************************************/
static void XHdcp22Rx_SetTopologyMaxCascadeExceeded(XHdcp22_Rx *InstancePtr, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Value == FALSE || Value == TRUE);

	InstancePtr->Topology.MaxCascadeExceeded = Value;
}

/*****************************************************************************/
/**
* This function sets the HDCP2_0_REPEATER_DOWNSTREAM flag in the repeater
* topology table used to indicate the presence of an HDCP2.0-compliant
* Repeater in the topology.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    Value is either TRUE or FALSE.
*
* @return   None.
*
* @note     None.
******************************************************************************/
static void XHdcp22Rx_SetTopologyHdcp20RepeaterDownstream(XHdcp22_Rx *InstancePtr, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Value == FALSE || Value == TRUE);

	InstancePtr->Topology.Hdcp20RepeaterDownstream = Value;
}

/*****************************************************************************/
/**
* This function sets the HDCP1_DEVICE_DOWNSTREAM flag in the repeater
* topology table used to indicate the presence of an HDCP1.x-compliant
* device in the topology.
*
* @param    InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param    Value is either TRUE or FALSE.
*
* @return   None.
*
* @note     None.
******************************************************************************/
static void XHdcp22Rx_SetTopologyHdcp1DeviceDownstream(XHdcp22_Rx *InstancePtr, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Value == FALSE || Value == TRUE);

	InstancePtr->Topology.Hdcp1DeviceDownstream = Value;
}

/*****************************************************************************/
/**
* This function clears the log pointers.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param	Verbose allows to add debug logging.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp22Rx_LogReset(XHdcp22_Rx *InstancePtr, u8 Verbose)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->Log.Head = 0;
	InstancePtr->Log.Tail = 0;
	InstancePtr->Log.Verbose = Verbose;

	/* Reset and start the logging timer. */
	/* Note: This timer increments continuously and will wrap at 42 second (100 Mhz clock) */
	if (InstancePtr->TimerInst.IsReady == XIL_COMPONENT_IS_READY) {
	  XTmrCtr_Reset(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_0);
	  XTmrCtr_Start(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_0);
	}
}


/*****************************************************************************/
/**
* This function returns the time expired since a log reset was called.
*
* @param  InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return The expired logging time in useconds.
*
* @note   None.
******************************************************************************/
u32 XHdcp22Rx_LogGetTimeUSecs(XHdcp22_Rx *InstancePtr)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u32 PeriodUsec = InstancePtr->TimerInst.Config.SysClockFreqHz / 1000000;
	return (XTmrCtr_GetValue(&InstancePtr->TimerInst, XHDCP22_RX_TMR_CTR_0) / PeriodUsec);
}


/*****************************************************************************/
/**
* This function writes HDCP22-RX log event into buffer. If the log event
* is of type error, the sticky error flag is set. The sticky error flag
* is used to keep history of error conditions.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param	Evt specifies an action to be carried out.
* @param	Data specifies the information that gets written into log
*			buffer.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp22Rx_LogWr(XHdcp22_Rx *InstancePtr, u16 Evt, u16 Data)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Evt < XHDCP22_RX_LOG_EVT_INVALID);

	/* When logging a debug event check if verbose is set to true */
	if ((InstancePtr->Log.Verbose == FALSE) &&
			((Evt == XHDCP22_RX_LOG_EVT_DEBUG) ||
			 (Evt == XHDCP22_RX_LOG_EVT_INFO_MESSAGE))) {
		return;
	}

	/* Write data and event into log buffer */
	InstancePtr->Log.LogItems[InstancePtr->Log.Head].Data = Data;
	InstancePtr->Log.LogItems[InstancePtr->Log.Head].LogEvent = Evt;
	InstancePtr->Log.LogItems[InstancePtr->Log.Head].TimeStamp =
			XHdcp22Rx_LogGetTimeUSecs(InstancePtr);

	/* Update head pointer if reached to end of the buffer */
	if (InstancePtr->Log.Head == XHDCP22_RX_LOG_BUFFER_SIZE - 1) {
		/* Clear pointer */
		InstancePtr->Log.Head = 0;
	} else {
		/* Increment pointer */
		InstancePtr->Log.Head++;
	}

	/* Check tail pointer. When the two pointer are equal, then the buffer
	* is full.In this case then increment the tail pointer as well to
	* remove the oldest entry from the buffer.
	*/
	if (InstancePtr->Log.Tail == InstancePtr->Log.Head) {
		if (InstancePtr->Log.Tail == XHDCP22_RX_LOG_BUFFER_SIZE - 1) {
			InstancePtr->Log.Tail = 0;
		} else {
			InstancePtr->Log.Tail++;
		}
	}

	/* Update sticky error flag */
	if(Evt == XHDCP22_RX_LOG_EVT_ERROR)
	{
		InstancePtr->Info.ErrorFlagSticky |= (u32)Data;
	}
}


/*****************************************************************************/
/**
* This function provides the log information from the log buffer.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return
*           - Content of log buffer if log pointers are not equal.
*           - Otherwise Zero.
*
* @note		None.
******************************************************************************/
XHdcp22_Rx_LogItem* XHdcp22Rx_LogRd(XHdcp22_Rx *InstancePtr)
{
	XHdcp22_Rx_LogItem* LogPtr;
	u8 Tail = 0;
	u8 Head = 0;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	Tail = InstancePtr->Log.Tail;
	Head = InstancePtr->Log.Head;

	/* Check if there is any data in the log and return a NONE defined log item */
	if (Tail == Head) {
		LogPtr = &InstancePtr->Log.LogItems[Tail];
		LogPtr->Data = 0;
		LogPtr->LogEvent = XHDCP22_RX_LOG_EVT_NONE;
		LogPtr->TimeStamp = 0;
		return LogPtr;
	}

	LogPtr = &InstancePtr->Log.LogItems[Tail];

	/* Increment tail pointer */
	if (Tail == XHDCP22_RX_LOG_BUFFER_SIZE - 1) {
		InstancePtr->Log.Tail = 0;
	}
	else {
		InstancePtr->Log.Tail++;
	}
	return LogPtr;
}


/*****************************************************************************/
/**
* This function prints the contents of the log buffer.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
* @param	buff is a pointer to the buffer to write to.
* @param	buff_size is the size of the passed buffer
*
* @return	Number of characters printed to the buffer.
*
* @note		None.
******************************************************************************/
int XHdcp22Rx_LogShow(XHdcp22_Rx *InstancePtr, char *buff, int buff_size)
{
	int strSize = 0;
	XHdcp22_Rx_LogItem* LogPtr;
	char str[255];
	u32 TimeStampPrev = 0;

	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

	strSize = scnprintf(buff+strSize, buff_size-strSize,
			"\r\n-------HDCP22 RX log start-------\r\n");
	strSize += scnprintf(buff+strSize, buff_size-strSize,
			"[Time(us):Delta(us)] <Event>\n\r");
	strcpy(str, "UNDEFINED");
	do {
		/* Read log data */
		LogPtr = XHdcp22Rx_LogRd(InstancePtr);

		/* Print timestamp */
		if(LogPtr->LogEvent != XHDCP22_RX_LOG_EVT_NONE)
		{
			if(LogPtr->TimeStamp < TimeStampPrev) TimeStampPrev = 0;
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"[%8u:", LogPtr->TimeStamp);
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"%8u] ", (LogPtr->TimeStamp - TimeStampPrev));
			TimeStampPrev = LogPtr->TimeStamp;
		}

		/* Print log event */
		switch(LogPtr->LogEvent) {
		case(XHDCP22_RX_LOG_EVT_NONE):
		strSize += scnprintf(buff+strSize, buff_size-strSize,
				"-------HDCP22 RX log end-------\r\n\r\n");
			break;
		case XHDCP22_RX_LOG_EVT_INFO:
			switch(LogPtr->Data)
			{
			/* Print General Log Data */
			case XHDCP22_RX_LOG_INFO_RESET:
				strcpy(str, "Asserted [RESET]"); break;
			case XHDCP22_RX_LOG_INFO_ENABLE:
				strcpy(str, "State machine [ENABLED]"); break;
			case XHDCP22_RX_LOG_INFO_DISABLE:
				strcpy(str, "State machine [DISABLED]"); break;
			case XHDCP22_RX_LOG_INFO_REQAUTH_REQ:
				strcpy(str, "Asserted [REAUTH_REQ]"); break;
			case XHDCP22_RX_LOG_INFO_ENCRYPTION_ENABLE:
				strcpy(str, "Asserted [ENCRYPTION_ENABLE]"); break;
			case XHDCP22_RX_LOG_INFO_TOPOLOGY_UPDATE:
				strcpy(str, "Asserted [TOPOLOGY_UPDATE]"); break;
			default:
				strcpy(str, "Unknown?"); break;
			}
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"%s\r\n", str);
			break;
		case XHDCP22_RX_LOG_EVT_INFO_STATE:
			switch(LogPtr->Data)
			{
			/* Print State Log Data */
			case XHDCP22_RX_STATE_B0_WAIT_AKEINIT:
				strcpy(str, "B0_WAIT_AKEINIT"); break;
			case XHDCP22_RX_STATE_B1_SEND_AKESENDCERT:
				strcpy(str, "B1_SEND_AKESENDCERT"); break;
			case XHDCP22_RX_STATE_B1_WAIT_AKEKM:
				strcpy(str, "B1_WAIT_AKEKM"); break;
			case XHDCP22_RX_STATE_B1_SEND_AKESENDHPRIME:
				strcpy(str, "B1_SEND_AKESENDHPRIME"); break;
			case XHDCP22_RX_STATE_B1_SEND_AKESENDPAIRINGINFO:
				strcpy(str, "B1_SEND_AKESENDPAIRINGINFO"); break;
			case XHDCP22_RX_STATE_B1_WAIT_LCINIT:
				strcpy(str, "B1_WAIT_LCINIT"); break;
			case XHDCP22_RX_STATE_B2_SEND_LCSENDLPRIME:
				strcpy(str, "B2_SEND_LCSENDLPRIME"); break;
			case XHDCP22_RX_STATE_B2_WAIT_SKESENDEKS:
				strcpy(str, "B2_WAIT_SKESENDEKS"); break;
			case XHDCP22_RX_STATE_B3_COMPUTE_KS:
				strcpy(str, "B3_COMPUTE_KS"); break;
			case XHDCP22_RX_STATE_B4_AUTHENTICATED:
				strcpy(str, "B4_AUTHENTICATED"); break;
			case XHDCP22_RX_STATE_C4_WAIT_FOR_DOWNSTREAM:
				strcpy(str, "C4_WAIT_FOR_DOWNSTREAM"); break;
			case XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST:
				strcpy(str, "C5_SEND_RECEIVERIDLIST"); break;
			case XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST_DONE:
				strcpy(str, "C5_SEND_RECEIVERIDLIST_DONE"); break;
			case XHDCP22_RX_STATE_C6_VERIFY_RECEIVERIDLISTACK:
				strcpy(str, "C6_WAIT_RECEIVERIDLISTACK"); break;
			case XHDCP22_RX_STATE_C7_WAIT_STREAM_MANAGEMENT:
				strcpy(str, "C7_WAIT_STREAMMANAGEMENT"); break;
			case XHDCP22_RX_STATE_C7_SEND_STREAM_READY:
				strcpy(str, "C7_SEND_STREAM_READY"); break;
			case XHDCP22_RX_STATE_C7_SEND_STREAM_READY_DONE:
				strcpy(str, "C7_SEND_STREAM_READY_DONE"); break;
			case XHDCP22_RX_STATE_C8_AUTHENTICATED:
				strcpy(str, "C8_AUTHENTICATED"); break;
			default:
				strcpy(str, "Unknown?"); break;
			}
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Current state [%s]\r\n", str);
			break;
		case XHDCP22_RX_LOG_EVT_INFO_MESSAGE:
			switch(LogPtr->Data)
			{
			/* Print Message Log Data */
			case XHDCP22_RX_MSG_ID_AKEINIT:
				strcpy(str, "Received message [AKEINIT]"); break;
			case XHDCP22_RX_MSG_ID_AKESENDCERT:
				strcpy(str, "Sent message [AKESENDCERT]"); break;
			case XHDCP22_RX_MSG_ID_AKENOSTOREDKM:
				strcpy(str, "Received message [AKENOSTOREDKM]"); break;
			case XHDCP22_RX_MSG_ID_AKESTOREDKM:
				strcpy(str, "Received message [AKESTOREDKM]"); break;
			case XHDCP22_RX_MSG_ID_AKESENDHPRIME:
				strcpy(str, "Sent message [AKESENDHPRIME]"); break;
			case XHDCP22_RX_MSG_ID_AKESENDPAIRINGINFO:
				strcpy(str, "Sent message [AKESENDPAIRINGINFO]"); break;
			case XHDCP22_RX_MSG_ID_LCINIT:
				strcpy(str, "Received message [LCINIT]"); break;
			case XHDCP22_RX_MSG_ID_LCSENDLPRIME:
				strcpy(str, "Sent message [LCSENDLPRIME]"); break;
			case XHDCP22_RX_MSG_ID_SKESENDEKS:
				strcpy(str, "Received message [SKESENDEKS]"); break;
			case XHDCP22_RX_MSG_ID_REPEATERAUTHSENDRXIDLIST:
				strcpy(str, "Sent message [REPEATERAUTHSENDRXIDLIST]"); break;
			case XHDCP22_RX_MSG_ID_REPEATERAUTHSENDACK:
				strcpy(str, "Received message [REPEATERAUTHSENDACK]"); break;
			case XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMMANAGE:
				strcpy(str, "Received message [REPEATERAUTHSTREAMMANAGE]"); break;
			case XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMREADY:
				strcpy(str, "Sent message [REPEATERAUTHSTREAMREADY]"); break;
			default:
				strcpy(str, "Unknown?"); break;
			}
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"%s\r\n", str);
			break;
		case XHDCP22_RX_LOG_EVT_DEBUG:
			switch(LogPtr->Data)
			{
			/* Print Debug Log Data */
			case XHDCP22_RX_LOG_DEBUG_WRITE_MESSAGE_AVAILABLE:
				strcpy(str, "Write message available"); break;
			case XHDCP22_RX_LOG_DEBUG_READ_MESSAGE_COMPLETE:
				strcpy(str, "Read message complete"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_RSA:
				strcpy(str, "COMPUTE_RSA"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_RSA_DONE:
				strcpy(str, "COMPUTE_RSA_DONE"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_KM:
				strcpy(str, "COMPUTE_KM"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_KM_DONE:
				strcpy(str, "COMPUTE_KM_DONE"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_HPRIME:
				strcpy(str, "COMPUTE_HPRIME"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_HPRIME_DONE:
				strcpy(str, "COMPUTE_HPRIME_DONE"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_EKH:
				strcpy(str, "COMPUTE_EKHKM"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_EKH_DONE:
				strcpy(str, "COMPUTE_EKHKM_DONE"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_LPRIME:
				strcpy(str, "COMPUTE_LPRIME"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_LPRIME_DONE:
				strcpy(str, "COMPUTE_LPRIME_DONE"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_KS:
				strcpy(str, "COMPUTE_KS"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_KS_DONE:
				strcpy(str, "COMPUTE_KS_DONE"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_VPRIME:
				strcpy(str, "COMPUTE_VPRIME"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_VPRIME_DONE:
				strcpy(str, "COMPUTE_VPRIME_DONE"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_MPRIME:
				strcpy(str, "COMPUTE_MPRIME"); break;
			case XHDCP22_RX_LOG_DEBUG_COMPUTE_MPRIME_DONE:
				strcpy(str, "COMPUTE_MPRIME_DONE"); break;
			case XHDCP22_RX_LOG_DEBUG_TIMER_START:
				strcpy(str, "TIMER_START"); break;
			case XHDCP22_RX_LOG_DEBUG_TIMER_EXPIRED:
				strcpy(str, "TIMER_EXPIRED"); break;
			default:
				strcpy(str, "Unknown?"); break;
			}
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Debug: Event [%s]\r\n", str);
			break;
		case XHDCP22_RX_LOG_EVT_ERROR:
			switch(LogPtr->Data)
			{
			/* Print Error Log Data */
			case XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE:
				strcpy(str, "Received message with unexpected size"); break;
			case XHDCP22_RX_ERROR_FLAG_FORCE_RESET:
				strcpy(str, "Forcing reset after error"); break;
			case XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT:
				strcpy(str, "Problem processing received message [AKEINIT]"); break;
			case XHDCP22_RX_ERROR_FLAG_PROCESSING_AKENOSTOREDKM:
				strcpy(str, "Problem processing received message [AKENOSTOREDKM]"); break;
			case XHDCP22_RX_ERROR_FLAG_PROCESSING_AKESTOREDKM:
				strcpy(str, "Problem processing received message [AKESTOREDKM]"); break;
			case XHDCP22_RX_ERROR_FLAG_PROCESSING_LCINIT:
				strcpy(str, "Problem processing received message [LCINIT]"); break;
			case XHDCP22_RX_ERROR_FLAG_PROCESSING_SKESENDEKS:
				strcpy(str, "Problem processing received message [SKESENDEKS]"); break;
			case XHDCP22_RX_ERROR_FLAG_PROCESSING_REPEATERAUTHSENDACK:
				strcpy(str, "Problem processing received message [REPEATERAUTHSENDACK]"); break;
			case XHDCP22_RX_ERROR_FLAG_PROCESSING_REPEATERAUTHSTREAMMANAGE:
				strcpy(str, "Problem processing received message [REPEATERAUTHSTREAMMANAGE]"); break;
			case XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY:
				strcpy(str, "Detected problem with link integrity"); break;
			case XHDCP22_RX_ERROR_FLAG_DDC_BURST:
				strcpy(str, "Detected problem with DDC burst read/write"); break;
			case XHDCP22_RX_ERROR_FLAG_MAX_LCINIT_ATTEMPTS:
				strcpy(str, "Exceeded maximum LCINIT attempts"); break;
			case XHDCP22_RX_ERROR_FLAG_EMPTY_REPEATER_TOPOLOGY:
				strcpy(str, "Empty repeater topology, device count is zero"); break;
			case XHDCP22_RX_ERROR_FLAG_MAX_REPEATER_TOPOLOGY:
				strcpy(str, "Exceeded repeater topology maximums"); break;
			default:
				strcpy(str, "Unknown?"); break;
			}
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: %s\r\n", str);
			break;
		case XHDCP22_RX_LOG_EVT_USER:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"User: %d\r\n", LogPtr->Data);
			break;
		default:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: Unknown log event\r\n");
			break;
		}
	} while (LogPtr->LogEvent != XHDCP22_RX_LOG_EVT_NONE);
	return strSize;
}

/*****************************************************************************/
/**
* This function prints the state machine information.
*
* @param	InstancePtr is a pointer to the XHdcp22_Rx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Rx_Info(XHdcp22_Rx *InstancePtr)
{
	XDEBUG_PRINTF("Status: ");
	if (XHdcp22Rx_IsEnabled(InstancePtr)) {
		switch (InstancePtr->Info.AuthenticationStatus) {
			case XHDCP22_RX_UNAUTHENTICATED :
				XDEBUG_PRINTF("Not Authenticated.\n\r");
			break;

			case XHDCP22_RX_AUTHENTICATION_BUSY :
				XDEBUG_PRINTF("Authentication Busy.\n\r");
			break;

			case XHDCP22_RX_AUTHENTICATED :
				XDEBUG_PRINTF("Authenticated.\n\r");
			break;

			case XHDCP22_RX_REAUTHENTICATE_REQUESTED :
				XDEBUG_PRINTF("Reauthentication Requested.\n\r");
			break;

			default :
				XDEBUG_PRINTF("Unknown?\n\r");
			break;
		}
	} else {
		XDEBUG_PRINTF("Core is disabled.\n\r");
	}

	XDEBUG_PRINTF("Encryption: ");
	if (XHdcp22Rx_IsEncryptionEnabled(InstancePtr)) {
		XDEBUG_PRINTF("Enabled.\n\r");
	} else {
		XDEBUG_PRINTF("Disabled.\n\r");
	}

	XDEBUG_PRINTF("Repeater: ");
	if (XHdcp22Rx_IsRepeater(InstancePtr)) {
		if (InstancePtr->Topology.MaxDevsExceeded)
			XDEBUG_PRINTF("MaxDevsExceeded, ");

		if (InstancePtr->Topology.MaxCascadeExceeded)
			XDEBUG_PRINTF("MaxCascadeExceeded, ");

		if (InstancePtr->Topology.Hdcp20RepeaterDownstream)
			XDEBUG_PRINTF("Hdcp20RepeaterDownstream, ");

		if (InstancePtr->Topology.Hdcp1DeviceDownstream)
			XDEBUG_PRINTF("Hdcp1DeviceDownstream, ");

		XDEBUG_PRINTF("Depth=%d, ", InstancePtr->Topology.Depth);
		XDEBUG_PRINTF("DeviceCnt=%d, ", InstancePtr->Topology.DeviceCnt);
		XDEBUG_PRINTF("StreamType=%d\n\r", XHdcp22Rx_GetContentStreamType(InstancePtr));
	} else {
		XDEBUG_PRINTF("Disabled.\n\r");
	}

	XDEBUG_PRINTF("Auth Requests: %d\n\r", InstancePtr->Info.AuthRequestCnt);
	XDEBUG_PRINTF("Reauth Requests: %d\n\r", InstancePtr->Info.ReauthRequestCnt);
	XDEBUG_PRINTF("Link Errors: %d\n\r", InstancePtr->Info.LinkErrorCnt);
	XDEBUG_PRINTF("DDC Errors: %d\n\r", InstancePtr->Info.DdcErrorCnt);
}

/*****************************************************************************/
/**
* This function is a stub for the run handler. The stub is here in
* case the upper layer forgot to set the handlers. On initialization, all
* handlers are set to this callback. It is considered an error for this
* handler to be invoked.
*
* @param	HandlerRef is a callback reference passed in by the upper
*			layer when setting the callback functions, and passed back to
*			the upper layer when the callback is invoked.
*
* @return	None.
*
* @note		None.
******************************************************************************/
static void XHdcp22_Rx_StubRunHandler(void *HandlerRef)
{
	Xil_AssertVoid(HandlerRef != NULL);
	Xil_AssertVoidAlways();
}

/*****************************************************************************/
/**
* This function is a stub for the set handler. The stub is here in
* case the upper layer forgot to set the handlers. On initialization, all
* handlers are set to this callback. It is considered an error for this
* handler to be invoked.
*
* @param	HandlerRef is a callback reference passed in by the upper
*			layer when setting the callback functions, and passed back to
*			the upper layer when the callback is invoked.
* @param	Data is a value to be set.
*
* @return	None.
*
* @note		None.
******************************************************************************/
static void XHdcp22_Rx_StubSetHandler(void *HandlerRef, u32 Data)
{
	Xil_AssertVoid(HandlerRef != NULL);
	Xil_AssertVoid(Data != 0);
	Xil_AssertVoidAlways();
}

/*****************************************************************************/
/**
* This function is a stub for the set handler. The stub is here in
* case the upper layer forgot to set the handlers. On initialization, all
* handlers are set to this callback. It is considered an error for this
* handler to be invoked.
*
* @param	HandlerRef is a callback reference passed in by the upper
*			layer when setting the callback functions, and passed back to
*			the upper layer when the callback is invoked.
*
* @return	Returns the get value.
*
* @note		None.
******************************************************************************/
static u32 XHdcp22_Rx_StubGetHandler(void *HandlerRef)
{
	Xil_AssertNonvoid(HandlerRef != NULL);
	Xil_AssertNonvoidAlways();

	return 0;
}

/** @} */

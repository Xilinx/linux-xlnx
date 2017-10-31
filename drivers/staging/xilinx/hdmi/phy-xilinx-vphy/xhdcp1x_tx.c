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
* @file xhdcp1x_tx.c
* @addtogroup hdcp1x_v4_0
* @{
*
* This contains the main implementation file for the Xilinx HDCP transmit
* state machine
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 1.10  MG     01/18/16 Added function XHdcp1x_TxIsEnabled.
* 1.20  MG     02/25/16 Added function XHdcp1x_TxSetCallback and
*                       authenticated callback.
* 3.0   yas    02/13/16 Upgraded to support HDCP Repeater functionality.
*                       Added enum XHDCP1X_EVENT_READDOWNSTREAM, of
*                       XHdcp1x_EventType.
*                       Added new functions:
*                       XHdcp1x_TxReadDownstream, XHdcp1x_TxSetCallBack,
*                       XHdcp1x_TxTriggerDownstreamAuth,
*                       XHdcp1x_TxSetRepeaterInfo.
* 3.1   yas    06/15/16 Repeater functionality extended to support HDMI.
*                       Added fucntions,
*                       XHdcp1x_TxIsInComputations,XHdcp1x_TxIsInWaitforready,
*                       XHdcp1x_TxIsDownstrmCapable, XHdcp1x_TxIsRepeater,
*                       XHdcp1x_TxEnableBlank, XHdcp1x_TxDisableBlank,
*                       XHdcp1x_TxGetTopologyKSVList,
*                       XHdcp1x_TxGetTopologyDepth,
*                       XHdcp1x_TxGetTopologyDeviceCnt,
*                       XHdcp1x_TxGetTopologyMaxCascadeExceeded,
*                       XHdcp1x_TxGetTopologyBKSV,
*                       XHdcp1x_TxGetTopologyMaxDevsExceeded,
*                       XHdcp1x_TxGetTopology
* 4.1   yas    03/07/17 Updated to remove compliance failures.
* 4.1   yas    22/04/17 Added function XHdcp1x_TxSetHdmiMode.
* 4.1   MH     11/05/17 Update to enable encryption immediately after Ro' check.
*                       Increase timeout for topology propagation.
* 4.1   yas    08/03/17 Updated the XHdcp1x_TxIsInProgress to track any
*                       pending authentication requests.
* </pre>
*
*****************************************************************************/

/***************************** Include Files *********************************/


#include "sha1.h"
//#include <stdio.h>
//#include <stdlib.h>
#include <linux/string.h>
#include "xhdcp1x.h"
#include "xhdcp1x_cipher.h"
#include "xhdcp1x_debug.h"
#include "xhdcp1x_platform.h"
#include "xhdcp1x_port.h"
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
#include "xhdcp1x_port_hdmi.h"
#else
#include "xhdcp1x_port_dp.h"
#endif
#include "xhdcp1x_tx.h"
#include "xil_types.h"
#include "xil_printf.h"

/************************** Constant Definitions *****************************/

#define XVPHY_FLAG_PHY_UP	(1u << 0)  /**< Flag to track physical state */
#define XVPHY_FLAG_IS_REPEATER	(1u << 1)  /**< Flag to track repeater state */

#define XVPHY_TMO_5MS		(5u)    /**< Timeout value for 5ms */
#define XVPHY_TMO_100MS		(100u)    /**< Timeout value for 100ms */
#define XVPHY_TMO_1SECOND	(1000u)    /**< Timeout value for 1s */

#define XHDCP1X_MAX_BCAPS_RDY_POLL_CNT	(55) /**< Max times to poll on BCaps
					  *  Ready bit at 100ms interval */

/**************************** Type Definitions *******************************/

/**
 * This enumerates the Event Types for HDCP Transmitter state machine.
 */
typedef enum {
	XHDCP1X_EVENT_NULL,
	XHDCP1X_EVENT_AUTHENTICATE,
	XHDCP1X_EVENT_CHECK,
	XHDCP1X_EVENT_DISABLE,
	XHDCP1X_EVENT_ENABLE,
	XHDCP1X_EVENT_LINKDOWN,
	XHDCP1X_EVENT_PHYDOWN,
	XHDCP1X_EVENT_PHYUP,
	XHDCP1X_EVENT_POLL,
	XHDCP1X_EVENT_TIMEOUT,
	XHDCP1X_EVENT_READDOWNSTREAM,
} XHdcp1x_EventType;

/**
 * This enumerates the Event Types for HDCP Transmitter state machine.
 */
typedef enum {
	XHDCP1X_STATE_DISABLED = XHDCP1X_TX_STATE_DISABLED,
	XHDCP1X_STATE_DETERMINERXCAPABLE = XHDCP1X_TX_STATE_DETERMINERXCAPABLE,
	XHDCP1X_STATE_EXCHANGEKSVS = XHDCP1X_TX_STATE_EXCHANGEKSVS,
	XHDCP1X_STATE_COMPUTATIONS = XHDCP1X_TX_STATE_COMPUTATIONS,
	XHDCP1X_STATE_VALIDATERX = XHDCP1X_TX_STATE_VALIDATERX,
	XHDCP1X_STATE_AUTHENTICATED = XHDCP1X_TX_STATE_AUTHENTICATED,
	XHDCP1X_STATE_LINKINTEGRITYCHECK = XHDCP1X_TX_STATE_LINKINTEGRITYCHECK,
	XHDCP1X_STATE_TESTFORREPEATER = XHDCP1X_TX_STATE_TESTFORREPEATER,
	XHDCP1X_STATE_WAITFORREADY = XHDCP1X_TX_STATE_WAITFORREADY,
	XHDCP1X_STATE_READKSVLIST = XHDCP1X_TX_STATE_READKSVLIST,
	XHDCP1X_STATE_UNAUTHENTICATED = XHDCP1X_TX_STATE_UNAUTHENTICATED,
	XHDCP1X_STATE_PHYDOWN = XHDCP1X_TX_STATE_PHYDOWN,
} XHdcp1x_StateType;

/***************** Macros (Inline Functions) Definitions *********************/

/*************************** Function Prototypes *****************************/

static void XHdcp1x_TxDebugLog(const XHdcp1x *InstancePtr, const char *LogMsg);
static void XHdcp1x_TxPostEvent(XHdcp1x *InstancePtr, XHdcp1x_EventType Event);
static void XHdcp1x_TxStartTimer(XHdcp1x *InstancePtr, u16 TimeoutInMs);
static void XHdcp1x_TxStopTimer(XHdcp1x *InstancePtr);
static void XHdcp1x_TxBusyDelay(XHdcp1x *InstancePtr, u16 DelayInMs);
static void XHdcp1x_TxReauthenticateCallback(void *Parameter);
static void XHdcp1x_TxCheckLinkCallback(void *Parameter);
static void XHdcp1x_TxSetCheckLinkState(XHdcp1x *InstancePtr, int IsEnabled);
static void XHdcp1x_TxEnableEncryptionState(XHdcp1x *InstancePtr);
static void XHdcp1x_TxDisableEncryptionState(XHdcp1x *InstancePtr);
static void XHdcp1x_TxEnableState(XHdcp1x *InstancePtr);
static void XHdcp1x_TxDisableState(XHdcp1x *InstancePtr);
static void XHdcp1x_TxCheckRxCapable(const XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static u64 XHdcp1x_TxGenerateAn(XHdcp1x *InstancePtr);
static int XHdcp1x_TxIsKsvValid(u64 Ksv);
static void XHdcp1x_TxExchangeKsvs(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxStartComputations(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxPollForComputations(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxValidateRx(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxCheckLinkIntegrity(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxTestForRepeater(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxPollForWaitForReady(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static int XHdcp1x_TxSetRepeaterInfo(XHdcp1x *InstancePtr);
static int XHdcp1x_TxValidateKsvList(XHdcp1x *InstancePtr, u16 RepeaterInfo);
static void XHdcp1x_TxReadKsvList(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunDisabledState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunDetermineRxCapableState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunExchangeKsvsState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunComputationsState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunValidateRxState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunAuthenticatedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunLinkIntegrityCheckState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunTestForRepeaterState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunWaitForReadyState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunReadKsvListState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunUnauthenticatedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxRunPhysicalLayerDownState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxEnterState(XHdcp1x *InstancePtr, XHdcp1x_StateType State,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxExitState(XHdcp1x *InstancePtr, XHdcp1x_StateType State,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_TxDoTheState(XHdcp1x *InstancePtr, XHdcp1x_EventType Event);
static void XHdcp1x_TxProcessPending(XHdcp1x *InstancePtr);
static const char *XHdcp1x_TxStateToString(XHdcp1x_StateType State);
#if XHDCP1X_ADDITIONAL_DEBUG
static const char *XHdcp1x_TxEventToString(XHdcp1x_EventType Event);
#endif

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
* This function installs callback functions for the given
* HandlerType:
*
* @param	InstancePtr is a pointer to the HDCP core instance.
* @param	HandlerType specifies the type of handler.
* @param	CallbackFunc is the address of the callback function.
* @param	CallbackRef is a user data item that will be passed to the
*		callback function when it is invoked.
*
* @return
*		- XST_SUCCESS if callback function installed successfully.
*		- XST_INVALID_PARAM when HandlerType is invalid.
*
* @note		Invoking this function for a handler that already has been
*			installed replaces it with the new handler.
*
******************************************************************************/
int XHdcp1x_TxSetCallback(XHdcp1x *InstancePtr,
	XHdcp1x_HandlerType HandlerType, void *CallbackFunc, void *CallbackRef)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(HandlerType > (XHDCP1X_HANDLER_UNDEFINED));
	Xil_AssertNonvoid(HandlerType < (XHDCP1X_HANDLER_INVALID));
	Xil_AssertNonvoid(CallbackFunc != NULL);
	Xil_AssertNonvoid(CallbackRef != NULL);

	u32 Status = XST_NO_DATA;

	/* Check for handler type */
	switch (HandlerType)
	{
		/* DDC write */
		case (XHDCP1X_HANDLER_DDC_WRITE):
			InstancePtr->Tx.DdcWrite
				= (XHdcp1x_RunDdcHandler)CallbackFunc;
			InstancePtr->Tx.DdcWriteRef = CallbackRef;
			InstancePtr->Tx.IsDdcWriteSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		/* DDC read */
		case (XHDCP1X_HANDLER_DDC_READ):
			InstancePtr->Tx.DdcRead
				= (XHdcp1x_RunDdcHandler)CallbackFunc;
			InstancePtr->Tx.DdcReadRef = CallbackRef;
			InstancePtr->Tx.IsDdcReadSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		/* Authenticated */
		case (XHDCP1X_HANDLER_AUTHENTICATED):
			InstancePtr->Tx.AuthenticatedCallback
				= (XHdcp1x_Callback)CallbackFunc;
			InstancePtr->Tx.AuthenticatedCallbackRef = CallbackRef;
			InstancePtr->Tx.IsAuthenticatedCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		/* Repeater - Repeater exchange (values) */
		/* Equivalent of case (XHDCP1X_RPTR_HDLR_REPEATER_EXCHANGE): */
		case (XHDCP1X_HANDLER_RPTR_RPTREXCHANGE):
			InstancePtr->Tx.RepeaterExchangeCallback
				= (XHdcp1x_Callback)CallbackFunc;
			InstancePtr->Tx.RepeaterExchangeRef = CallbackRef;
			InstancePtr->Tx.IsRepeaterExchangeCallbackSet = (TRUE);
			break;

		/* Un-authenticated */
		case (XHDCP1X_HANDLER_UNAUTHENTICATED):
			InstancePtr->Tx.UnauthenticatedCallback
				= (XHdcp1x_Callback)CallbackFunc;
			InstancePtr->Tx.UnauthenticatedCallbackRef
				= CallbackRef;
			InstancePtr->Tx.IsUnauthenticatedCallbackSet = (TRUE);
			break;

		default:
			Status = (XST_INVALID_PARAM);
			break;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function initializes a transmit state machine.
*
* @param	InstancePtr is the receiver instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_TxInit(XHdcp1x *InstancePtr)
{
	XHdcp1x_StateType DummyState = XHDCP1X_STATE_DISABLED;

	/* Update theHandler */
	InstancePtr->Tx.PendingEvents = 0;

	/* Kick the state machine */
	XHdcp1x_TxEnterState(InstancePtr, XHDCP1X_STATE_DISABLED, &DummyState);
}

/*****************************************************************************/
/**
* This function polls an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxPoll(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Process any pending events */
	XHdcp1x_TxProcessPending(InstancePtr);

	/* Poll it */
	XHdcp1x_TxDoTheState(InstancePtr, XHDCP1X_EVENT_POLL);

	return (Status);
}

/*****************************************************************************/
/**
* This function resets an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		This function disables and then re-enables the interface.
*
******************************************************************************/
int XHdcp1x_TxReset(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Reset it */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_DISABLE);
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_ENABLE);

	/* Reset the Authentication In Progress Flag */
	InstancePtr->Tx.IsAuthReqPending = (FALSE);
	return (Status);
}

/*****************************************************************************/
/**
* This function enables an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxEnable(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Post it */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_ENABLE);

	return (Status);
}

/*****************************************************************************/
/**
* This function disables an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxDisable(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Post it */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_DISABLE);

	return (Status);
}

/*****************************************************************************/
/**
* This function queries an interface to check if it is enabled.
*
* @param	InstancePtr is the transmitter instance.
*
* @return	Truth value indicating is enabled (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxIsEnabled(const XHdcp1x *InstancePtr)
{
	int IsEnabled = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Which state? */
	switch (InstancePtr->Tx.CurrentState) {
		case XHDCP1X_STATE_DISABLED:
			IsEnabled = FALSE;
			break;

		/* Otherwise */
		default:
			IsEnabled = TRUE;
			break;
	}

	return (IsEnabled);
}

/*****************************************************************************/
/**
* This function updates the physical state of an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
* @param	IsUp is truth value indicating the status of physical interface.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxSetPhysicalState(XHdcp1x *InstancePtr, int IsUp)
{
	int Status = XST_SUCCESS;
	XHdcp1x_EventType Event = XHDCP1X_EVENT_PHYDOWN;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine Event */
	if (IsUp) {
		Event = XHDCP1X_EVENT_PHYUP;
	}

	/* Post it */
	XHdcp1x_TxPostEvent(InstancePtr, Event);

	return (Status);
}

/*****************************************************************************/
/**
* This function set the lane count of an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
* @param	LaneCount is the number of lanes of the interface.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxSetLaneCount(XHdcp1x *InstancePtr, int LaneCount)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(LaneCount > 0);

	/* Set it */
	return (XHdcp1x_CipherSetNumLanes(InstancePtr, LaneCount));
}

/*****************************************************************************/
/**
* This function initiates authentication on an interface.
*
* @param	InstancePtr is the transmitter instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxAuthenticate(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Set auth request pending flag */
	InstancePtr->Tx.IsAuthReqPending = (TRUE);

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Post the re-authentication request */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_AUTHENTICATE);

	return (Status);
}

/*****************************************************************************/
/**
* This function initiates the transmitter to read READY bit from downstream and
* complete second part of authentication.
*
* @param	InstancePtr is the transmitter instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxReadDownstream(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Post the re-authentication request */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_READDOWNSTREAM);

	return (Status);
}

/*****************************************************************************/
/**
* This function queries an interface to check if authentication is still in
* progress.
*
* @param	InstancePtr is the transmitter instance.
*
* @return	Truth value indicating in progress (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxIsInProgress(const XHdcp1x *InstancePtr)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Tx.IsAuthReqPending);
}

/*****************************************************************************/
/**
* This function queries an interface to check if its been authenticated.
*
* @param	InstancePtr is the transmitter instance.
*
* @return	Truth value indicating authenticated (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxIsAuthenticated(const XHdcp1x *InstancePtr)
{
	int Authenticated = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Case-wise process the kind of state called */
	switch (InstancePtr->Tx.CurrentState) {
		/* For the authenticated and link integrity check states */
		case XHDCP1X_STATE_AUTHENTICATED:
		case XHDCP1X_STATE_LINKINTEGRITYCHECK:
			Authenticated = TRUE;
			break;

		/* Otherwise */
		default:
			Authenticated = FALSE;
			break;
	}

	return (Authenticated);
}

/*****************************************************************************/
/**
* This function queries an interface to check if its in the computations state.
*
* @param	InstancePtr is the transmitter instance.
*
* @return	Truth value indicating authenticated (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxIsInComputations(const XHdcp1x *InstancePtr)
{
	int IsInComp = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine IsInComputations */
	if (InstancePtr->Tx.CurrentState == XHDCP1X_STATE_COMPUTATIONS) {
		IsInComp = TRUE;
	}

	return (IsInComp);
}

/*****************************************************************************/
/**
* This function queries an interface to check if its in the
* wait-for-ready state.
*
* @param	InstancePtr is the transmitter instance.
*
* @return	Truth value indicating authenticated (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxIsInWaitforready(const XHdcp1x *InstancePtr)
{
	int IsInWfr = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine IsInWaitforready */
	if (InstancePtr->Tx.CurrentState == XHDCP1X_STATE_WAITFORREADY) {
		IsInWfr = TRUE;
	}

	return (IsInWfr);
}

/*****************************************************************************/
/**
* This function queries the downstream device to check if the downstream
* device is HDCP capable.
*
* @param	InstancePtr is the receiver instance.
*
* @return	Truth value indicating HDCP capability (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxIsDownstrmCapable(const XHdcp1x *InstancePtr)
{
	int IsCapable = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine IsCapable */
	if (XHdcp1x_PortIsCapable(InstancePtr)) {
		IsCapable = TRUE;
	}

	return (IsCapable);
}

/*****************************************************************************/
/**
* This function retrieves the current encryption stream map.
*
* @param	InstancePtr the transmitter instance.
*
* @return	The current encryption stream map.
*
* @note		None.
*
******************************************************************************/
u64 XHdcp1x_TxGetEncryption(const XHdcp1x *InstancePtr)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Tx.EncryptionMap);
}

/*****************************************************************************/
/**
* This function enables encryption on set of streams on an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
* @param	StreamMap is the bit map of streams to enable encryption on.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxEnableEncryption(XHdcp1x *InstancePtr, u64 StreamMap)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Update InstancePtr */
	InstancePtr->Tx.EncryptionMap |= StreamMap;

	/* Check for authenticated */
	if (XHdcp1x_TxIsAuthenticated(InstancePtr)) {
		XHdcp1x_TxEnableEncryptionState(InstancePtr);
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function disables encryption on set of streams on an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
* @param	StreamMap is the bit map of streams to disable encryption on.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxDisableEncryption(XHdcp1x *InstancePtr, u64 StreamMap)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Only disable encryption when the TX is enabled */
	if (XHdcp1x_TxIsEnabled(InstancePtr)) {

		/* Disable it */
		Status = XHdcp1x_CipherDisableEncryption(InstancePtr, StreamMap);

		/* Update InstancePtr */
		if (Status == XST_SUCCESS) {
			InstancePtr->Tx.EncryptionMap &= ~StreamMap;
		}
	}
	return (Status);
}

/*****************************************************************************/
/**
* This set a flag that allows the hdcp1x drivers to determine if the
* transmitter is HDMI or DVI.
*
* @param	InstancePtr is the transmitter instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_TxSetHdmiMode(XHdcp1x *InstancePtr, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Value == FALSE || Value == TRUE);

#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)
	InstancePtr->Tx.TxIsHdmi = Value;
#else
	UNUSED(Value);
#endif
}

/*****************************************************************************/
/**
* This function handles a timeout on an HDCP interface.
*
* @param	InstancePtr is the transmitter instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_TxHandleTimeout(XHdcp1x *InstancePtr)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Post the timeout */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_TIMEOUT);
}


/*****************************************************************************/
/**
* This function returns if HDCP TX interface is connected
* to a downstream repeater.
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
int XHdcp1x_TxIsRepeater(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check if downstream is Repeater */
	Status = XHdcp1x_PortIsRepeater(InstancePtr);

	return (Status);
}

/*****************************************************************************/
/**
* This function implements the debug display output for transmit instances.
*
* @param	InstancePtr is the receiver instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_TxInfo(const XHdcp1x *InstancePtr)
{
	u64 LocalKsv = 0;
	u32 Version = 0;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Display it */
	XDEBUG_PRINTF("Type:            ");
	if (InstancePtr->Config.IsHDMI) {
		XDEBUG_PRINTF("hdmi-tx\r\n");
	}
	else {
		XDEBUG_PRINTF("dp-tx\r\n");
	}
	XDEBUG_PRINTF("Current State:   %s\r\n",
			XHdcp1x_TxStateToString(InstancePtr->Tx.CurrentState));
	XDEBUG_PRINTF("Previous State:  %s\r\n",
			XHdcp1x_TxStateToString(InstancePtr->Tx.PreviousState));
	XDEBUG_PRINTF("Encrypted?:      %s\r\n",
			XHdcp1x_IsEncrypted(InstancePtr) ? "Yes" : "No");
	XDEBUG_PRINTF("State Helper:    %016llX\r\n",
			InstancePtr->Tx.StateHelper);
	XDEBUG_PRINTF("Flags:           %04X\r\n",
			InstancePtr->Tx.Flags);
	XDEBUG_PRINTF("Encryption Map:  %016llX\r\n",
			InstancePtr->Tx.EncryptionMap);
	Version = XHdcp1x_GetDriverVersion();
	XDEBUG_PRINTF("Driver Version:  %d.%02d.%02d\r\n",
			((Version >> 16) &0xFFFFu), ((Version >> 8) & 0xFFu),
			(Version & 0xFFu));
	Version = XHdcp1x_CipherGetVersion(InstancePtr);
	XDEBUG_PRINTF("Cipher Version:  %d.%02d.%02d\r\n",
			((Version >> 16) &0xFFFFu), ((Version >> 8) & 0xFFu),
			(Version & 0xFFu));
	LocalKsv = XHdcp1x_CipherGetLocalKsv(InstancePtr);
	XDEBUG_PRINTF("Local KSV:       %02lX", (LocalKsv >> 32));
	XDEBUG_PRINTF("%08lX\r\n", (LocalKsv & 0xFFFFFFFFu));

	XDEBUG_PRINTF("\r\n");
	XDEBUG_PRINTF("Tx Stats\r\n");
	XDEBUG_PRINTF("Auth Passed:     %d\r\n",
			InstancePtr->Tx.Stats.AuthPassed);
	XDEBUG_PRINTF("Auth Failed:     %d\r\n",
			InstancePtr->Tx.Stats.AuthFailed);
	XDEBUG_PRINTF("Reauth Requests: %d\r\n",
			InstancePtr->Tx.Stats.ReauthRequested);
	XDEBUG_PRINTF("Check Passed:    %d\r\n",
			InstancePtr->Tx.Stats.LinkCheckPassed);
	XDEBUG_PRINTF("Check Failed:    %d\r\n",
			InstancePtr->Tx.Stats.LinkCheckFailed);
	XDEBUG_PRINTF("Read Failures:   %d\r\n",
			InstancePtr->Tx.Stats.ReadFailures);

	XDEBUG_PRINTF("\r\n");
	XDEBUG_PRINTF("Cipher Stats\r\n");
	XDEBUG_PRINTF("Int Count:       %d\r\n",
			InstancePtr->Cipher.Stats.IntCount);

	XDEBUG_PRINTF("\r\n");
	XDEBUG_PRINTF("Port Stats\r\n");
	XDEBUG_PRINTF("Int Count:       %d\r\n",
			InstancePtr->Port.Stats.IntCount);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
*
* This function enables the blank output for the cipher.
*
* @param  InstancePtr is a pointer to the Hdcp1.4 core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp1x_TxEnableBlank(XHdcp1x *InstancePtr)
{
	XHdcp1x_CipherEnableBlank(InstancePtr);
}

/*****************************************************************************/
/**
*
* This function disables the blank output for the cipher.
*
* @param  InstancePtr is a pointer to the Hdcp1.4 core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp1x_TxDisableBlank(XHdcp1x *InstancePtr)
{
	XHdcp1x_CipherDisableBlank(InstancePtr);
}

/*****************************************************************************/
/**
* This function logs a debug message on behalf of a handler state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	LogMsg is the message to log.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxDebugLog(const XHdcp1x *InstancePtr, const char *LogMsg)
{
#if 0
	char Label[16];

	/* Format Label */
	snprintf(Label, 16, "hdcp-tx(%d) - ", InstancePtr->Config.DeviceId);

	/* Log it */
	XHDCP1X_DEBUG_LOGMSG(Label);
	XHDCP1X_DEBUG_LOGMSG(LogMsg);
	XHDCP1X_DEBUG_LOGMSG("\r\n");
#else
	//xil_printf("hdcp-tx(%d) - %s\n", InstancePtr->Config.DeviceId, LogMsg);
#endif
}

/*****************************************************************************/
/**
* This function posts an event to a state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to post.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxPostEvent(XHdcp1x *InstancePtr, XHdcp1x_EventType Event)
{
	/* Check for disable and clear any pending enable */
	if (Event == XHDCP1X_EVENT_DISABLE) {
		InstancePtr->Tx.PendingEvents &= ~(1u << XHDCP1X_EVENT_ENABLE);
	}
	/* Check for phy-down and clear any pending phy-up */
	else if (Event == XHDCP1X_EVENT_PHYDOWN) {
		InstancePtr->Tx.PendingEvents &= ~(1u << XHDCP1X_EVENT_PHYUP);
	}

	/* Post it */
	InstancePtr->Tx.PendingEvents |= (1u << Event);
}

/*****************************************************************************/
/**
* This function starts a state machine's timer.
*
* @param	InstancePtr is the state machine.
* @param	TimeoutInMs is the timeout in milli-seconds.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxStartTimer(XHdcp1x *InstancePtr, u16 TimeoutInMs)
{
	/* Start it */
	XHdcp1x_PlatformTimerStart(InstancePtr, TimeoutInMs);
}

/*****************************************************************************/
/**
* This function stops a state machine's timer.
*
* @param	InstancePtr is the state machine.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxStopTimer(XHdcp1x *InstancePtr)
{
	/* Stop it */
	XHdcp1x_PlatformTimerStop(InstancePtr);
}

/*****************************************************************************/
/**
* This function busy delays a state machine.
*
* @param	InstancePtr is the state machine.
* @param	DelayInMs is the delay time in milli-seconds.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxBusyDelay(XHdcp1x *InstancePtr, u16 DelayInMs)
{
	/* Busy wait */
	XHdcp1x_PlatformTimerBusy(InstancePtr, DelayInMs);
}

/*****************************************************************************/
/**
* This function acts as the reauthentication callback for a state machine.
*
* @param	Parameter is the parameter specified during registration.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxReauthenticateCallback(void *Parameter)
{
	XHdcp1x *InstancePtr = Parameter;

	/* Update statistics */
	InstancePtr->Tx.Stats.ReauthRequested++;

	/* Post the re-authentication request */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_AUTHENTICATE);
}

/*****************************************************************************/
/**
* This function acts as the downstream authentication trigger callback for a
* Repeater state machine, to start the second part of authentication.
*
* @param	Parameter is the parameter specified during registration.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_TxTriggerDownstreamAuth(void *Parameter)
{
	XHdcp1x *InstancePtr = Parameter;

	/* Post the re-authentication request */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_AUTHENTICATE);
}

/*****************************************************************************/
/**
* This function acts as the check link callback for a state machine.
*
* @param	Parameter is the parameter specified during registration.
*
* @return	Mpme/
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxCheckLinkCallback(void *Parameter)
{
	XHdcp1x *InstancePtr = Parameter;

	/* Post the check request */
	XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_CHECK);
}

/*****************************************************************************/
/**
* This function sets the check link state of the handler.
*
* @param	InstancePtr is the HDCP state machine.
* @param	IsEnabled is truth value indicating on/off.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxSetCheckLinkState(XHdcp1x *InstancePtr, int IsEnabled)
{
	/* Check for HDMI */
	if (InstancePtr->Config.IsHDMI) {
		/* Check for enabled */
		if (IsEnabled) {
			/* Register Callback */
			XHdcp1x_CipherSetCallback(InstancePtr,
				XHDCP1X_CIPHER_HANDLER_Ri_UPDATE,
				&XHdcp1x_TxCheckLinkCallback, InstancePtr);

			/* Enable it */
			XHdcp1x_CipherSetRiUpdate(InstancePtr, TRUE);
		}
		/* Otherwise */
		else {
			/* Disable it */
			XHdcp1x_CipherSetRiUpdate(InstancePtr, FALSE);
		}
	}
}

/*****************************************************************************/
/**
* This function enables encryption for a state machine.
*
* @param	InstancePtr is the HDCP state machine.
*
* @return	None.
*
* @note		This function inserts a 5ms delay for things to settle when
*		encryption is actually being disabled.
*
******************************************************************************/
static void XHdcp1x_TxEnableEncryptionState(XHdcp1x *InstancePtr)
{
	/* Check for encryption enabled */
	if (InstancePtr->Tx.EncryptionMap != 0) {
		u64 StreamMap = 0;

		/* Determine StreamMap */
		StreamMap =
			XHdcp1x_CipherGetEncryption(InstancePtr);

		/* Check if there is something to do */
		if (StreamMap != InstancePtr->Tx.EncryptionMap) {
			/* Wait a bit */
			XHdcp1x_TxBusyDelay(InstancePtr, XVPHY_TMO_5MS);

			/* Enable it */
			XHdcp1x_CipherEnableEncryption(InstancePtr,
					InstancePtr->Tx.EncryptionMap);
		}
	}
}

/*****************************************************************************/
/**
* This function disables encryption for a state machine.
*
* @param	InstancePtr is the HDCP state machine.
*
* @return	None.
*
* @note		This function inserts a 5ms delay for things to settle when
*		encryption is actually being disabled.
*
******************************************************************************/
static void XHdcp1x_TxDisableEncryptionState(XHdcp1x *InstancePtr)
{
	u64 StreamMap = XHdcp1x_CipherGetEncryption(InstancePtr);

	/* Check if encryption actually enabled */
	if (StreamMap != 0) {
		/* Update StreamMap for all stream */
		StreamMap = (u64)(-1);

		/* Disable it all */
		XHdcp1x_CipherDisableEncryption(InstancePtr, StreamMap);

		/* Wait at least a frame */
		XHdcp1x_TxBusyDelay(InstancePtr, XVPHY_TMO_5MS);
	}
}

/*****************************************************************************/
/**
* This function enables a state machine.
*
* @param	InstancePtr is the HDCP state machine.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxEnableState(XHdcp1x *InstancePtr)
{
	/* Clear statistics */
	memset(&(InstancePtr->Tx.Stats), 0, sizeof(InstancePtr->Tx.Stats));

	/* Enable the crypto engine */
	XHdcp1x_CipherEnable(InstancePtr);

	/* Register the re-authentication callback */
	XHdcp1x_PortSetCallback(InstancePtr,
			XHDCP1X_PORT_HANDLER_AUTHENTICATE,
			&XHdcp1x_TxReauthenticateCallback, InstancePtr);

	/* Enable the hdcp port */
	XHdcp1x_PortEnable(InstancePtr);
}

/*****************************************************************************/
/**
* This function disables a state machine.
*
* @param	InstancePtr is the HDCP state machine.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxDisableState(XHdcp1x *InstancePtr)
{
	/* Disable the hdcp port */
	XHdcp1x_PortDisable(InstancePtr);

	/* Disable the cryto engine */
	XHdcp1x_CipherDisable(InstancePtr);

	/* Disable the timer */
	XHdcp1x_TxStopTimer(InstancePtr);

	/* Update InstancePtr */
	InstancePtr->Tx.Flags &= ~XVPHY_FLAG_IS_REPEATER;
	InstancePtr->Tx.StateHelper = 0;
	InstancePtr->Tx.EncryptionMap = 0;
}

/*****************************************************************************/
/**
* This function checks to ensure that the remote end is HDCP capable.
*
* @param	InstancePtr is the HDCP state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxCheckRxCapable(const XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	/* Check for capable */
	if (XHdcp1x_PortIsCapable(InstancePtr)) {
		/* Log */
		XHdcp1x_TxDebugLog(InstancePtr, "rx hdcp capable");

		/* Update NextStatePtr */
		*NextStatePtr = XHDCP1X_STATE_EXCHANGEKSVS;
	}
	else {
		/* Log */
		XHdcp1x_TxDebugLog(InstancePtr, "rx not capable");

		/* Update NextStatePtr */
		*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
	}
}

/*****************************************************************************/
/**
* This function generates the An from a random number generator.
*
* @param	InstancePtr is the HDCP state machine.
*
* @return	A 64-bit pseudo random number (An).
*
* @note		None.
*
******************************************************************************/
static u64 XHdcp1x_TxGenerateAn(XHdcp1x *InstancePtr)
{
	u64 An = 0;

	/* Attempt to generate An */
	if (XHdcp1x_CipherDoRequest(InstancePtr,
		XHDCP1X_CIPHER_REQUEST_RNG) == XST_SUCCESS) {
		/* Wait until done */
		while (!XHdcp1x_CipherIsRequestComplete(InstancePtr));

		/* Update theAn */
		An = XHdcp1x_CipherGetMi(InstancePtr);
	}

	/* Check if zero */
	if (An == 0) {
		An = 0x351F7175406A74Dull;
	}

	return (An);
}

/*****************************************************************************/
/**
* This function validates a KSV value as having 20 1s and 20 0s.
*
* @param	Ksv is the value to validate.
*
* @return	Truth value indicating valid (TRUE) or not (FALSE).
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_TxIsKsvValid(u64 Ksv)
{
	int IsValid = FALSE;
	int NumOnes = 0;

	/* Determine NumOnes */
	while (Ksv != 0) {
		if ((Ksv & 1) != 0) {
			NumOnes++;
		}
		Ksv >>= 1;
	}

	/* Check for 20 1s */
	if (NumOnes == 20) {
		IsValid = TRUE;
	}

	return (IsValid);
}

/*****************************************************************************/
/**
* This function exchanges the ksvs between the two ends of the link.
*
* @param	InstancePtr is the HDCP state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxExchangeKsvs(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	u8 Buf[8];

	/* Initialize Buf */
	memset(Buf, 0, 8);

	/* Update NextStatePtr - assume failure */
	*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;

	/* Read the Bksv from remote end */
	if (XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BKSV,
			Buf, 5) > 0) {
		u64 RemoteKsv = 0;

		/* Determine theRemoteKsv */
		XHDCP1X_PORT_BUF_TO_UINT(RemoteKsv, Buf,
				XHDCP1X_PORT_SIZE_BKSV * 8);

		/* Check for invalid */
		if (!XHdcp1x_TxIsKsvValid(RemoteKsv)) {
			XHdcp1x_TxDebugLog(InstancePtr, "Bksv invalid");
		}
		/* Check for revoked */
		else if (XHdcp1x_PlatformIsKsvRevoked(InstancePtr,
				RemoteKsv)) {
			XHdcp1x_TxDebugLog(InstancePtr, "Bksv is revoked");
		}
		/* Otherwise we're good to go */
		else {
			u64 LocalKsv = 0;
			u64 An = 0;
			u8 Buf_AInfo[XHDCP1X_PORT_SIZE_AINFO];

			/* Check for repeater and update InstancePtr */
			if (XHdcp1x_PortIsRepeater(InstancePtr)) {
				InstancePtr->Tx.Flags |= XVPHY_FLAG_IS_REPEATER;
			}
			else {
				InstancePtr->Tx.Flags &=
						~XVPHY_FLAG_IS_REPEATER;
			}

			/* Generate theAn */
			An = XHdcp1x_TxGenerateAn(InstancePtr);

			/* Save theAn into the state helper for use later */
			InstancePtr->Tx.StateHelper = An;

			/* Determine theLocalKsv */
			LocalKsv = XHdcp1x_CipherGetLocalKsv(InstancePtr);

			/* Load the cipher with the remote ksv */
			XHdcp1x_CipherSetRemoteKsv(InstancePtr, RemoteKsv);

			/* Clear AINFO */
			memset(Buf_AInfo, 0, XHDCP1X_PORT_SIZE_AINFO);
			XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_AINFO,
					Buf_AInfo, XHDCP1X_PORT_SIZE_AINFO);

			/* Send An to remote */
			XHDCP1X_PORT_UINT_TO_BUF(Buf, An,
					XHDCP1X_PORT_SIZE_AN * 8);
			XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_AN,
					Buf, XHDCP1X_PORT_SIZE_AN);

			/* Send AKsv to remote */
			XHDCP1X_PORT_UINT_TO_BUF(Buf, LocalKsv,
					XHDCP1X_PORT_SIZE_AKSV * 8);
			XHdcp1x_PortWrite(InstancePtr,
					XHDCP1X_PORT_OFFSET_AKSV,
					Buf, XHDCP1X_PORT_SIZE_AKSV);

			/* Update NextStatePtr */
			*NextStatePtr = XHDCP1X_STATE_COMPUTATIONS;
		}
	}
	/* Otherwise */
	else {
		/* Update the statistics */
		InstancePtr->Tx.Stats.ReadFailures++;
	}
}

/*****************************************************************************/
/**
* This function initiates the computations for a state machine.
*
* @param	InstancePtr is the HDCP receiver state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxStartComputations(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	u64 Value = 0;
	u32 X = 0;
	u32 Y = 0;
	u32 Z = 0;

	/* Log */
	XHdcp1x_TxDebugLog(InstancePtr, "starting computations");

	/* Update Value with An */
	Value = InstancePtr->Tx.StateHelper;

	/* Load the cipher B registers with An */
	X = (u32)(Value & 0x0FFFFFFFul);
	Value >>= 28;
	Y = (u32)(Value & 0x0FFFFFFFul);
	Value >>= 28;
	Z = (u32)(Value & 0x000000FFul);
	if ((InstancePtr->Tx.Flags & XVPHY_FLAG_IS_REPEATER) != 0) {
		Z |= (1ul << 8);
	}
	XHdcp1x_CipherSetB(InstancePtr, X, Y, Z);

	/* Initiate the block cipher */
	XHdcp1x_CipherDoRequest(InstancePtr, XHDCP1X_CIPHER_REQUEST_BLOCK);

	/* Update NextStatePtr */
	*NextStatePtr = XHDCP1X_STATE_COMPUTATIONS;
}

/*****************************************************************************/
/**
* This function polls the progress of the computations for a state machine.
*
* @param	InstancePtr is the HDCP state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxPollForComputations(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	/* Check for done */
	if (XHdcp1x_CipherIsRequestComplete(InstancePtr)) {
		XHdcp1x_TxDebugLog(InstancePtr, "computations complete");
		*NextStatePtr = XHDCP1X_STATE_VALIDATERX;
	}
	else {
		XHdcp1x_TxDebugLog(InstancePtr, "waiting for computations");
	}
}

/*****************************************************************************/
/**
* This function validates the attached receiver.
*
* @param	InstancePtr is the HDCP state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxValidateRx(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	u8 Buf[2];
	int NumTries = 3;

	/* Update NextStatePtr */
	*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;

	/* Attempt to read Ro */
	do {
		/* Read the remote Ro' */
		if (XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_RO,
				Buf, 2) > 0) {
			char LogBuf[32];
			u16 RemoteRo = 0;
			u16 LocalRo = 0;

			/* Determine RemoteRo */
			XHDCP1X_PORT_BUF_TO_UINT(RemoteRo, Buf, (2 * 8));

			/* Determine theLLocalRoocalRo */
			LocalRo = XHdcp1x_CipherGetRo(InstancePtr);

			/* Compare the Ro values */
			if (LocalRo == RemoteRo) {

				/* Determine theLogBuf */
				snprintf(LogBuf, 32, "rx valid Ro/Ro' (%04X)",
						LocalRo);

				/* Update NextStatePtr */
				*NextStatePtr = XHDCP1X_STATE_TESTFORREPEATER;
			}
			/* Otherwise */
			else {
				/* Determine theLogBuf */
				snprintf(LogBuf, 32, "Ro/Ro' mismatch (%04X/"
						"%04X)", LocalRo, RemoteRo);

				/* Update statistics if the last attempt */
				if (NumTries == 1)
					InstancePtr->Tx.Stats.AuthFailed++;
			}

			/* Log */
			XHdcp1x_TxDebugLog(InstancePtr, LogBuf);
		}
		/* Otherwise */
		else {
			/* Log */
			XHdcp1x_TxDebugLog(InstancePtr, "Ro' read failure");

			/* Update the statistics */
			InstancePtr->Tx.Stats.ReadFailures++;
		}

		/* Update for loop */
		NumTries--;
	}
	while ((*NextStatePtr == XHDCP1X_STATE_UNAUTHENTICATED) &&
		(NumTries > 0));
}

/*****************************************************************************/
/**
* This function checks the integrity of a HDCP link.
*
* @param	InstancePtr is the hdcp state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxCheckLinkIntegrity(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	u8 Buf[2];
	int NumTries = 3;

	/* Update theNextState */
	*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;

	/* Iterate through the tries */
	do {
		/* Read the remote Ri' */
		if (XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_RO,
				Buf, 2) > 0) {

			char LogBuf[48];
			u16 RemoteRi = 0;
			u16 LocalRi = 0;

			/* Determine theRemoteRo */
			XHDCP1X_PORT_BUF_TO_UINT(RemoteRi, Buf, 16);

			/* Determine theLocalRi */
			LocalRi = XHdcp1x_CipherGetRi(InstancePtr);

			/* Compare the local and remote values */
			if (LocalRi == RemoteRi) {
				*NextStatePtr = XHDCP1X_STATE_AUTHENTICATED;
				snprintf(LogBuf, 48, "link check passed Ri/Ri'"
						"(%04X)", LocalRi);
			}
			/* Check for last attempt */
			else if (NumTries == 1) {
				snprintf(LogBuf, 48, "link check failed Ri/Ri'"
						"(%04X/%04X)", LocalRi,
						RemoteRi);
			}

			/* Log */
			XHdcp1x_TxDebugLog(InstancePtr, LogBuf);
		}
		else {
			XHdcp1x_TxDebugLog(InstancePtr, "Ri' read failure");
			InstancePtr->Tx.Stats.ReadFailures++;
		}

		/* Update for loop */
		NumTries--;
	} while ((*NextStatePtr != XHDCP1X_STATE_AUTHENTICATED) &&
		(NumTries > 0));

	/* Check for success */
	if (*NextStatePtr == XHDCP1X_STATE_AUTHENTICATED) {
		InstancePtr->Tx.Stats.LinkCheckPassed++;
	}
	else {
		InstancePtr->Tx.Stats.LinkCheckFailed++;
	}
}

/*****************************************************************************/
/**
* This function checks the remote end to see if its a repeater.
*
* @param	InstancePtr is the HDCP state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		The implementation of this function enables encryption when a
*		repeater is detected downstream. The standard is ambiguous
*		as to the handling of this specific case by this behaviour is
*		required in order to pass the Unigraf compliance test suite.
*
******************************************************************************/
static void XHdcp1x_TxTestForRepeater(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	/* Check for repeater */
	if (XHdcp1x_PortIsRepeater(InstancePtr)) {

		/* Update InstancePtr */
		InstancePtr->Tx.Flags |= XVPHY_FLAG_IS_REPEATER;

		/* Update NextStatePtr */
		*NextStatePtr = XHDCP1X_STATE_WAITFORREADY;

		/* Log */
		XHdcp1x_TxDebugLog(InstancePtr, "repeater detected");

	}
	else {
		/* Update InstancePtr */
		InstancePtr->Tx.Flags &= ~XVPHY_FLAG_IS_REPEATER;

		/* Set the Down Stream Ready flag - in case of repeater
		 * we are ready to send repeater values upstream */
		InstancePtr->Tx.DownstreamReady = 1;

		/* Update NextStatePtr */
		*NextStatePtr = XHDCP1X_STATE_AUTHENTICATED;

	}
}

/*****************************************************************************/
/**
* This function polls a state machine in the "wait for ready" state.
*
* @param	InstancePtr is the hdcp state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxPollForWaitForReady(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	u16 RepeaterInfo = 0;
	int Status = XST_SUCCESS;

	/* Attempt to read the repeater info */
	Status = XHdcp1x_PortGetRepeaterInfo(InstancePtr, &RepeaterInfo);
	if (Status == XST_SUCCESS) {
		/* Check that neither cascade or device numbers exceeded */
		if ((RepeaterInfo & 0x0880u) == 0) {
			/* Check for at least one attached device */
			if ((RepeaterInfo & 0x007Fu) != 0) {
				/* Update InstancePtr */
				InstancePtr->Tx.StateHelper = RepeaterInfo;

				/* Update NextStatePtr */
				*NextStatePtr = XHDCP1X_STATE_READKSVLIST;

				/* Log */
				XHdcp1x_TxDebugLog(InstancePtr,
					"devices attached: ksv list ready");
			}
			/* Otherwise */
			else {
				/* Update NextStatePtr */
				*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;

				/* Log */
				XHdcp1x_TxDebugLog(InstancePtr,
					"no attached devices");
			}
		}
		/* Check for cascade exceeded */
		else {
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
			/* Disable the hdcp encryption for both HDMI and DP.
			 * But currently only doing it for HDMI. */
			XHdcp1x_TxDisableEncryptionState(InstancePtr);
#endif

			/* Update NextStatePtr */
			*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;

			/* Log */
			if ((RepeaterInfo & 0x0800u) != 0) {
				XHdcp1x_TxDebugLog(InstancePtr,
					"max cascade exceeded");
			}
			else {
				XHdcp1x_TxDebugLog(InstancePtr,
					"max devices exceeded");
			}
		}
	}
}

/*****************************************************************************/
/**
* This function validates the ksv list from an attached repeater.
*
* @param	InstancePtr is the hdcp state machine.
* @param	RepeaterInfo is the repeater information.
*
* @return	Truth value indicating valid (TRUE) or invalid (FALSE).
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_TxValidateKsvList(XHdcp1x *InstancePtr, u16 RepeaterInfo)
{
	SHA1Context Sha1Context;
	u8 Buf[24];
	int NumToRead = 0;
	int KsvCount = 0;
	int IsValid = FALSE;

	u8 ksvListHolder[127*XHDCP1X_PORT_SIZE_BKSV];

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)

#else

	int NumOfKsvsToRead = 0;
	unsigned int KSVListSize = 0;
	unsigned int ksvListByteCount = 0;

#endif

	/* Initialize the local KSV fifo */
	memset(ksvListHolder, 0, (127*XHDCP1X_PORT_SIZE_BKSV));

	/* Initialize Buf */
	memset(Buf, 0, 24);

	/* Initialize Sha1Context */
	SHA1Reset(&Sha1Context);

	/* Assume success */
	IsValid = TRUE;

	/* Determine theNumToRead */
	NumToRead = (((RepeaterInfo & 0x7Fu) * 5));

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
	/* Read the ksv list */
	/* Read the entire KSV fifo list in one go */
	int ByteCount = 0;

	if(XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_KSVFIFO,
			ksvListHolder, NumToRead) ) {

		/* Update the calculation of V */
		SHA1Input(&Sha1Context, ksvListHolder, NumToRead);

		/* Update this value in the RepeaterExchange
		 * structure to be read later by RX */

		u64 Value = 0;
		u32 Index;
		while (ByteCount < NumToRead) {
			if((ByteCount + 1) % 5 == 0) {

				Index = (((ByteCount+1) / XHDCP1X_PORT_SIZE_BKSV) - 1) * XHDCP1X_PORT_SIZE_BKSV;

				XHDCP1X_PORT_BUF_TO_UINT(Value ,
						(ksvListHolder + ( (((ByteCount+1)
						/ XHDCP1X_PORT_SIZE_BKSV) - 1)
						* XHDCP1X_PORT_SIZE_BKSV)),
					XHDCP1X_PORT_SIZE_BKSV * 8);
				if (!(Value)) {
					XHdcp1x_TxDebugLog(InstancePtr ,
						"Error: Null KSV read "
						"from downstream KSV List");
				}
				InstancePtr->RepeaterValues.KsvList[KsvCount++] =
					(Value & 0xFFFFFFFFFFul);
				Value = 0;
			}

			ByteCount++;

		}

		UNUSED(Index);
	}
	else {
		/* Update the statistics */
		InstancePtr->Tx.Stats.ReadFailures++;

		/* Update IsValid */
		IsValid = FALSE;
	}

#else

	NumOfKsvsToRead = (NumToRead / 5);

	KSVListSize = NumOfKsvsToRead;

	/* Read the ksv list */
	do {
		/* Read 15 bytes at a go */
		int NumThisTime = XHDCP1X_PORT_SIZE_KSVFIFO;
		int NumOfKsvsThisTime = 3;

		/* Truncate if necessary */
		if (NumThisTime > NumToRead) {
			NumThisTime = NumToRead;
		}

		if (NumOfKsvsThisTime > NumOfKsvsToRead) {
			NumOfKsvsThisTime = NumOfKsvsToRead;
		}

		/* Read the next chunk of the list */
		if (XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_KSVFIFO,
				Buf, NumThisTime) > 0) {

			/* Update the calculation of V */
			SHA1Input(&Sha1Context, Buf, NumThisTime);

			/* Duplicate the buffer */
			int BufReadKsvCount=0;
			while(BufReadKsvCount < NumThisTime) {
				ksvListHolder[ksvListByteCount++] = Buf[BufReadKsvCount];
				BufReadKsvCount++;
			}

		}
		else {
			/* Update the statistics */
			InstancePtr->Tx.Stats.ReadFailures++;

			/* Update IsValid */
			IsValid = FALSE;
		}

		/* Update for loop */
		NumToRead -= NumThisTime;
		NumOfKsvsToRead -= NumOfKsvsThisTime;

	} while ((NumToRead > 0) && (IsValid));

#endif

	/* Check for success */
	if (IsValid) {
		u64 Mo = 0;
		u8 Sha1Result[SHA1HashSize];

		/* Insert RepeaterInfo into the SHA-1 transform */
		Buf[0] = (u8) (RepeaterInfo & 0xFFu);
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
		Buf[1] = (u8) ( ((RepeaterInfo |
			(XHDCP1X_PORT_BIT_BSTATUS_HDMI_MODE))
			>> XHDCP1X_PORT_BSTATUS_DEPTH_SHIFT) & 0xFFu);
#else
		Buf[1] = (u8) ((RepeaterInfo >> XHDCP1X_PORT_BINFO_DEPTH_SHIFT)
						& 0xFFu);
#endif
		SHA1Input(&Sha1Context, Buf, 2);

		/* Insert the Mo into the SHA-1 transform */
		Mo = XHdcp1x_CipherGetMo(InstancePtr);
		XHDCP1X_PORT_UINT_TO_BUF(Buf, Mo, 64);
		SHA1Input(&Sha1Context, Buf, 8);

		/* Finalize the SHA-1 result and confirm success */
		if (SHA1Result(&Sha1Context, Sha1Result) == shaSuccess) {

			u8 Offset = XHDCP1X_PORT_OFFSET_VH0;
			const u8 *Sha1Buf = Sha1Result;
			int NumIterations = (SHA1HashSize >> 2);

			/* Iterate through the SHA-1 chunks */
			do {
				u32 CalcValue = 0;
				u32 ReadValue = 0;

				/* Determine CalcValue */
				CalcValue = *Sha1Buf++;
				CalcValue <<= 8;
				CalcValue |= *Sha1Buf++;
				CalcValue <<= 8;
				CalcValue |= *Sha1Buf++;
				CalcValue <<= 8;
				CalcValue |= *Sha1Buf++;

				/* Read the value from the far end */
				if (XHdcp1x_PortRead(InstancePtr, Offset,
						Buf, 4) > 0) {
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
					/* Storing the V' prime values later */
#else
					memcpy(&InstancePtr->RepeaterValues.V[Offset -
								XHDCP1X_PORT_OFFSET_VH0], Buf, 4);
#endif

					/* Determine ReadValue */
					XHDCP1X_PORT_BUF_TO_UINT(ReadValue,
							Buf, 32);
				}
				else {
					/* Update ReadValue */
					ReadValue = 0;

					/* Update the statistics */
					InstancePtr->Tx.Stats.ReadFailures++;
				}

				/* Check for mismatch */
				if (CalcValue != ReadValue) {
					IsValid = FALSE;
				}

				/* Update for loop */
				Offset += 4;
				NumIterations--;
			}
			while (NumIterations > 0);
		}
		/* Otherwise */
		else {
			IsValid = FALSE;
		}
	}

	if (InstancePtr->IsRepeater) {

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
			/* Do nothing */
#else
		/* Update this value in the RepeaterExchange
		 * structure to be read later by RX
		 */
		u64 Value = 0;
		u32 thisKsv = 0;
		while(thisKsv < KSVListSize) {

			XHDCP1X_PORT_BUF_TO_UINT(Value ,
				(ksvListHolder +
				(thisKsv * XHDCP1X_PORT_SIZE_BKSV)) ,
				XHDCP1X_PORT_SIZE_BKSV * 8);
			if (!(Value)) {
				XHdcp1x_TxDebugLog(InstancePtr ,
					"Error: Null KSV read "
					"from downstream KSV List");
				thisKsv++;
				continue;
			}


			InstancePtr->RepeaterValues.KsvList[KsvCount++] =
				(Value & 0xFFFFFFFFFFul);

			Value = 0;
			thisKsv++;
		}
#endif

		{
			u8 Bksv[5];

			/* Clear Bksv read buffer */
			memset(Bksv, 0 , 5);

			/* Add the BKSV of the attached downstream repeater to
			 * the KSV List */
			XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BKSV,
					Bksv, 5);

			u64 RemoteKsv = 0;

			/* Determine theRemoteKsv */
			XHDCP1X_PORT_BUF_TO_UINT(RemoteKsv, Bksv,
					XHDCP1X_PORT_SIZE_BKSV * 8);

			/* Check for invalid */
			if (!XHdcp1x_TxIsKsvValid(RemoteKsv)) {
				XHdcp1x_TxDebugLog(InstancePtr, "Bksv invalid");
				return FALSE;
			}

			InstancePtr->RepeaterValues.KsvList[KsvCount] = RemoteKsv;
		}
	}

	return (IsValid);
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
*           - TRUE if MAX_DEPTH_EXCEEDED
*           - FALSE if not
*
* @note     None.
*
******************************************************************************/
u8 *XHdcp1x_TxGetTopologyKSVList(XHdcp1x *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	static u8  DeviceList[256][5];
	u32 DeviceCnt;
	u32 i;

	memset(DeviceList[0],0,(32*5));
	DeviceCnt = InstancePtr->RepeaterValues.DeviceCount;

	for(i=0;i<DeviceCnt;i++) {
		memcpy(&DeviceList[i][0],
			&InstancePtr->RepeaterValues.KsvList[i], 5);
	}

	return(DeviceList[0]);

}

/*****************************************************************************/
/**
*
* This function returns the value of Depth read in the downstream interface
* of the repeater topology.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return	Value of Depth.
*
* @note     None.
*
******************************************************************************/
u32 XHdcp1x_TxGetTopologyDepth(XHdcp1x *InstancePtr)
{
	return (InstancePtr->RepeaterValues.Depth);
}

/*****************************************************************************/
/**
*
* This function returns the value of Device Count read in the downstream
* interface of the repeater topology.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return
*           - TRUE if MAX_DEPTH_EXCEEDED
*           - FALSE if not
*
* @note     None.
*
******************************************************************************/
u32 XHdcp1x_TxGetTopologyDeviceCnt(XHdcp1x *InstancePtr)
{
	return (InstancePtr->RepeaterValues.DeviceCount);
}

/*****************************************************************************/
/**
*
* This function returns the MAX_DEPTH_EXCEEDED flag in the repeater
* topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return
*           - TRUE if MAX_DEPTH_EXCEEDED
*           - FALSE if not
*
* @note     None.
*
******************************************************************************/
u32 XHdcp1x_TxGetTopologyMaxCascadeExceeded(XHdcp1x *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u32 Buf;
	u32 Status;

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
	/* Read the BStatus Register */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
			&Buf, XHDCP1X_PORT_SIZE_BSTATUS);

	/* BInfo : Max_device_exceeded[7] */
	Status = (Buf & XHDCP1X_PORT_BSTATUS_BIT_DEPTH_ERR)? TRUE : FALSE;
#else
	/* Read the BInfo Register */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
			&Buf, XHDCP1X_PORT_SIZE_BINFO);

	/* BInfo : Max_device_exceeded[7] */
	Status = (Buf & XHDCP1X_PORT_BINFO_BIT_DEPTH_ERR)? TRUE : FALSE;
#endif

	return Status;
}

/*****************************************************************************/
/**
*
* This function returns the value of BKSV of the device connected to the
* repeater downstream interface.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return
*           - TRUE if MAX_DEPTH_EXCEEDED
*           - FALSE if not
*
* @note     None.
*
******************************************************************************/
u8 *XHdcp1x_TxGetTopologyBKSV(XHdcp1x *InstancePtr)
{
	u8 Buf[8];
	u64 RemoteKsv = 0;
	static u8 Bksv[5];

	/* Initialize Buf */
	memset(Buf, 0, 8);
	memset(Bksv, 0, 5);

	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BKSV,
				Buf, XHDCP1X_PORT_SIZE_BKSV);

	XHDCP1X_PORT_BUF_TO_UINT(RemoteKsv, Buf,
					XHDCP1X_PORT_SIZE_BKSV * 8);

	memcpy(&Bksv[0], &RemoteKsv, XHDCP1X_PORT_SIZE_BKSV);

	return(&Bksv[0]);
}

/*****************************************************************************/
/**
*
* This function returns the MAX_DEVICS_EXCEEDED flag in the repeater
* topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp14 core instance.
*
* @return
*           - TRUE if MAX_DEVICES_EXCEEDED
*           - FALSE if not
*
* @note     None.
*
******************************************************************************/
u32 XHdcp1x_TxGetTopologyMaxDevsExceeded(XHdcp1x *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	u32 Buf;
	u32 Status;

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
	/* Read the BStatus Register */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
			&Buf, XHDCP1X_PORT_SIZE_BSTATUS);

	/* BInfo : Max_device_exceeded[7] */
	Status = (Buf & XHDCP1X_PORT_BSTATUS_BIT_DEV_CNT_ERR)? TRUE : FALSE;
#else
	/* Read the BInfo Register */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
			&Buf, XHDCP1X_PORT_SIZE_BINFO);

	/* BInfo : Max_device_exceeded[7] */
	Status = (Buf & XHDCP1X_PORT_BINFO_BIT_DEV_CNT_ERR)? TRUE : FALSE;
#endif

	return Status;
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
XHdcp1x_RepeaterExchange *XHdcp1x_TxGetTopology(XHdcp1x *InstancePtr)
{

	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	if (InstancePtr->IsRepeater) {
		if (InstancePtr->Rx.CurrentState == XHDCP1X_STATE_AUTHENTICATED) {
			return &InstancePtr->RepeaterValues;
		}
	}

	return NULL;
}

/*****************************************************************************/
/**
* This function sets the V'H0, V'H1, V'H2, V'H3, V'H4, KSVList and BInfo values
* in the HDCP TX HDCP Instance for Repeater validation .
*
* @param	InstancePtr is the transmitter instance.
*
* @return
*		-XST_SUCCESS if successful.
* 		-XST_FAILURE if unsuccessful.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_TxSetRepeaterInfo(XHdcp1x *InstancePtr)
{
	u8 Bksv[8];
	u32 KsvCount = 0;

	/* Check for repeater */
	if (XHdcp1x_PortIsRepeater(InstancePtr)) {
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
		u32 Buf;
#else
		u16 RepeaterInfo;
#endif

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)

		/* Set the SHA1 Hash value */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_VH0,
				&Buf, XHDCP1X_PORT_SIZE_VH0);

		/* V'H0 */
		InstancePtr->RepeaterValues.V[0] = (Buf&0xFFFF);

		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_VH1,
				&Buf, XHDCP1X_PORT_SIZE_VH1);

		/* V'H1 */
		InstancePtr->RepeaterValues.V[1] = (Buf&0xFFFF);

		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_VH2,
				&Buf, XHDCP1X_PORT_SIZE_VH2);

		/* V'H2 */
		InstancePtr->RepeaterValues.V[2] = (Buf&0xFFFF);

		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_VH3,
				&Buf, XHDCP1X_PORT_SIZE_VH3);

		/* V'H3 */
		InstancePtr->RepeaterValues.V[3] = (Buf&0xFFFF);

		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_VH4,
				&Buf, XHDCP1X_PORT_SIZE_VH4);

		/* V'H4 */
		InstancePtr->RepeaterValues.V[4] = (Buf&0xFFFF);

#else
		/* Do nothing for DP */
#endif

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)

		/* Copy the Depth read from the downstream HDCP device */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
				&Buf, XHDCP1X_PORT_SIZE_BSTATUS);
		/* BInfo : Device_Count[6:0], Max_device_exceeded[7],
		 * Depth[10:8], Max_cascade_exceeded[11], Hdmi_Mode[12],
		 * Hdmi_Reserved_2[13], Rsvd[15:14].
		 */
		InstancePtr->RepeaterValues.Depth = ((Buf & 0x0700)>>8);

		/* Copy the device count read from
		 * the downstream HDCP device */
		InstancePtr->RepeaterValues.DeviceCount = (Buf & 0x007F);
		/* Increment the device count by 1 to account for
		 * the HDCP Repeater system itsef */
		InstancePtr->RepeaterValues.DeviceCount++;
#else
		/* Get the Depth read from the downstream HDCP device */
		RepeaterInfo = (u16)(InstancePtr->Tx.StateHelper & 0x0FFFu);

		/* BInfo : Device_Count[6:0], Max_device_exceeded[7],
		 * Depth[10:8], Max_cascade_exceeded[11]
		 */
		InstancePtr->RepeaterValues.Depth = ((RepeaterInfo & 0x0700)>>8);

		/* Copy the device count read from
		 * the downstream HDCP device */
		InstancePtr->RepeaterValues.DeviceCount = (RepeaterInfo & 0x007F);

		/* Increment the device count by 1 to account for
		 * the HDCP Repeater system itsef */
		InstancePtr->RepeaterValues.DeviceCount++;
#endif

	}/* end of (if (downstream is a Repeater)) */
	else {
		/* The downstream is not Repeater and we need to
		* add the downstream BKSV to the KSV FIFO
		*/

		/* Write the Depth to the RepeaterValues */
		InstancePtr->RepeaterValues.Depth = 0;

		/* Write the Device Count to the RepeaterValues */
		InstancePtr->RepeaterValues.DeviceCount = 1;

		/* Add the BKSV of the attached downstream receiver
		* to the KSV List */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BKSV,
				Bksv, 5);

		u64 RemoteKsv = 0;

		/* Determine theRemoteKsv */
		XHDCP1X_PORT_BUF_TO_UINT(RemoteKsv, Bksv,
				XHDCP1X_PORT_SIZE_BKSV * 8);

		/* Check for invalid */
		if (!XHdcp1x_TxIsKsvValid(RemoteKsv)) {
			XHdcp1x_TxDebugLog(InstancePtr, "Bksv invalid");
			return XST_FAILURE;
		}

		InstancePtr->RepeaterValues.KsvList[KsvCount++] = RemoteKsv;
	}/* end of (else (downstream is a Repeater)) */

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function reads the ksv list from an attached repeater.
*
* @param	InstancePtr is the hdcp state machine.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxReadKsvList(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	int NumAttempts = 3;
	int KsvListIsValid = FALSE;
	u16 RepeaterInfo = 0;

	/* Determine RepeaterInfo */
	RepeaterInfo = (u16)(InstancePtr->Tx.StateHelper & 0x1FFFu);

	/* Iterate through the attempts */
	do {
		/* Attempt to validate the ksv list */
		KsvListIsValid =
			XHdcp1x_TxValidateKsvList(InstancePtr, RepeaterInfo);

		/* Update for loop */
		NumAttempts--;
	}
	while ((NumAttempts > 0) && (!KsvListIsValid));

	/* Check for success */
	if (KsvListIsValid) {
		/* Log */
		XHdcp1x_TxDebugLog(InstancePtr, "ksv list validated");

		/* Check for Repeater */
		if (InstancePtr->IsRepeater) {
			/* Set the Repeater information to be
			* passes to HDCP RX setup */
			XHdcp1x_TxSetRepeaterInfo(InstancePtr);
		}

		/* Set the downstream ready variable to 1, as Tx is now
		* authenticated and call back the RX
		*/
		InstancePtr->Tx.DownstreamReady = 1;

		/* Update NextStatePtr */
		*NextStatePtr = XHDCP1X_STATE_AUTHENTICATED;
	}
	else {
		/* Log */
		XHdcp1x_TxDebugLog(InstancePtr, "ksv list invalid");

		/* Update NextStatePtr */
		*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
	}
}

/*****************************************************************************/
/**
* This function runs the "disabled" state of the transmit state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunDisabledState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For enable */
		case XHDCP1X_EVENT_ENABLE:
			*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
			if ((InstancePtr->Tx.Flags & XVPHY_FLAG_PHY_UP) == 0) {
				*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			}
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			InstancePtr->Tx.Flags &= ~XVPHY_FLAG_PHY_UP;
			break;

		/* For physical layer up */
		case XHDCP1X_EVENT_PHYUP:
			InstancePtr->Tx.Flags |= XVPHY_FLAG_PHY_UP;
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "determine rx capable" state of the transmit state
* machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunDetermineRxCapableState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* InstancePtr not being used */
	UNUSED(InstancePtr);

	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "exchange ksvs" state of the transmit state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunExchangeKsvsState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* InstancePtr not being used */
	UNUSED(InstancePtr);

	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "computations" state of the transmit state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunComputationsState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* For poll */
		case XHDCP1X_EVENT_POLL:
			XHdcp1x_TxPollForComputations(InstancePtr,
					NextStatePtr);
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "validate-rx" state of the transmit state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunValidateRxState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* For timeout */
		case XHDCP1X_EVENT_TIMEOUT:
			XHdcp1x_TxDebugLog(InstancePtr, "validate-rx timeout");
			XHdcp1x_TxValidateRx(InstancePtr, NextStatePtr);
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "authenticated" state of the transmit state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunAuthenticatedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* InstancePtr not being used */
	UNUSED(InstancePtr);

	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;
			break;

		/* For check */
		case XHDCP1X_EVENT_CHECK:
			*NextStatePtr = XHDCP1X_STATE_LINKINTEGRITYCHECK;
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "link-integrity check" state of the transmit state
* machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunLinkIntegrityCheckState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* For poll */
		case XHDCP1X_EVENT_POLL:
			XHdcp1x_TxCheckLinkIntegrity(InstancePtr,
						NextStatePtr);
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "test-for-repeater" state of the transmit state
* machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunTestForRepeaterState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* For poll */
		case XHDCP1X_EVENT_POLL:
			XHdcp1x_TxTestForRepeater(InstancePtr, NextStatePtr);
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "wait-for-ready" state of the transmit state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunWaitForReadyState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* For poll */
		case XHDCP1X_EVENT_POLL:
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
			/* Don't read the Repeater info for HDMI from a
			 * poll from application. We'll poll every 100ms. */
#else
			/* Attempt to read the repeater info */
			XHdcp1x_TxPollForWaitForReady(InstancePtr,
				NextStatePtr);
#endif
			break;

		/* For reading the READY bit in BStatus register of the
		 * attached downstream device.
		 */
		case XHDCP1X_EVENT_READDOWNSTREAM:
			XHdcp1x_TxPollForWaitForReady(InstancePtr,
				NextStatePtr);
			break;

		/* For timeout */
		case XHDCP1X_EVENT_TIMEOUT:
			XHdcp1x_TxDebugLog(InstancePtr,
					"wait-for-ready timeout");
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
			InstancePtr->Tx.WaitForReadyPollCntFlag++;
			XHdcp1x_TxStopTimer(InstancePtr);
			XHdcp1x_TxPollForWaitForReady(InstancePtr,
						      NextStatePtr);
			if (InstancePtr->Tx.WaitForReadyPollCntFlag >
					XHDCP1X_MAX_BCAPS_RDY_POLL_CNT) {
				*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
				InstancePtr->Tx.WaitForReadyPollCntFlag = 0;
			} else {
				if (*NextStatePtr == XHDCP1X_STATE_READKSVLIST ||
				    *NextStatePtr == XHDCP1X_STATE_UNAUTHENTICATED) {
					/* Do nothing, timer
					 * is already stopped */
				} else {
					XHdcp1x_TxStartTimer(InstancePtr, XVPHY_TMO_100MS);
				}
			}
#else
			/* Poll on DisplayPort to check
			 * if the READY bit is set. */
			XHdcp1x_TxPollForWaitForReady(InstancePtr,
						      NextStatePtr);
			if (*NextStatePtr == XHDCP1X_STATE_WAITFORREADY) {
				*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
			}
#endif
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "read-ksv-list" state of the transmit state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunReadKsvListState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* InstancePtr not being used */
	UNUSED(InstancePtr);

	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* For timeout event */
		case XHDCP1X_EVENT_TIMEOUT:
			/* Do nothing */
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "unauthenticated" state of the transmit state machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunUnauthenticatedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			XHdcp1x_TxStartTimer(InstancePtr, XVPHY_TMO_100MS);
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		case XHDCP1X_EVENT_TIMEOUT:
			*NextStatePtr = XHDCP1X_STATE_DETERMINERXCAPABLE;
			XHdcp1x_TxStopTimer(InstancePtr);
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "physical-layer-down" state of the transmit state
* machine.
*
* @param	InstancePtr is the transmitter instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxRunPhysicalLayerDownState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer up */
		case XHDCP1X_EVENT_PHYUP:
			*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
			if (InstancePtr->Tx.EncryptionMap != 0) {
				XHdcp1x_TxPostEvent(InstancePtr,
					XHDCP1X_EVENT_AUTHENTICATE);
			}
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function enters a state.
*
* @param	InstancePtr is the HDCP state machine.
* @param	State is the state to enter.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxEnterState(XHdcp1x *InstancePtr, XHdcp1x_StateType State,
		XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of state called */
	switch (State) {
		/* For the disabled state */
		case XHDCP1X_STATE_DISABLED:
			XHdcp1x_TxDisableState(InstancePtr);
			break;

		/* For determine rx capable */
		case XHDCP1X_STATE_DETERMINERXCAPABLE:
			InstancePtr->Tx.Flags |= XVPHY_FLAG_PHY_UP;
			XHdcp1x_TxSetCheckLinkState(InstancePtr, FALSE);
			XHdcp1x_TxDisableEncryptionState(InstancePtr);
			XHdcp1x_TxCheckRxCapable(InstancePtr, NextStatePtr);
			break;

		/* For the exchange ksvs state */
		case XHDCP1X_STATE_EXCHANGEKSVS:
			InstancePtr->Tx.StateHelper = 0;
			XHdcp1x_TxExchangeKsvs(InstancePtr, NextStatePtr);
			break;

		/* For the computations state */
		case XHDCP1X_STATE_COMPUTATIONS:
			XHdcp1x_TxStartComputations(InstancePtr,
						    NextStatePtr);
			break;

		/* For the validate rx state */
		case XHDCP1X_STATE_VALIDATERX:
			InstancePtr->Tx.StateHelper = 0;
			XHdcp1x_TxStartTimer(InstancePtr, XVPHY_TMO_100MS);
			break;

		/* For the validate rx state */
		case XHDCP1X_STATE_TESTFORREPEATER:
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
			/* Enable encryption for HDMI (this will come immediately
			 * after Ro' has been read and successfully compared). */
			InstancePtr->Tx.EncryptionMap = 0x1;
			XHdcp1x_TxEnableEncryptionState(InstancePtr);
#else
			/* Enable encryption for DP (this will come immediately
			 * after Ro' has been read and successfully compared). */
			XHdcp1x_TxEnableEncryptionState(InstancePtr);
#endif
			break;

		/* For the wait for ready state */
		case XHDCP1X_STATE_WAITFORREADY:
			InstancePtr->Tx.StateHelper = 0;

#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
			InstancePtr->Tx.WaitForReadyPollCntFlag = 0;
			/* Post the timeout */
			XHdcp1x_TxPostEvent(InstancePtr, XHDCP1X_EVENT_TIMEOUT);
#else
			XHdcp1x_TxStartTimer(InstancePtr,
					     (5 * XVPHY_TMO_1SECOND));
#endif

			break;

		/* For the read ksv list state */
		case XHDCP1X_STATE_READKSVLIST:
			XHdcp1x_TxReadKsvList(InstancePtr, NextStatePtr);
			break;

		/* For the authenticated state */
		case XHDCP1X_STATE_AUTHENTICATED:
			InstancePtr->Tx.StateHelper = 0;

			if (InstancePtr->Tx.PreviousState !=
					XHDCP1X_STATE_LINKINTEGRITYCHECK) {
				InstancePtr->Tx.Stats.AuthPassed++;
				XHdcp1x_TxSetCheckLinkState(InstancePtr, TRUE);
				XHdcp1x_TxDebugLog(InstancePtr,
					"authenticated");

				/* Authenticated callback */
				if (InstancePtr->Tx.IsAuthenticatedCallbackSet)
				{
					/* Clear auth request pending flag */
					InstancePtr->Tx.IsAuthReqPending = (FALSE);

					InstancePtr->Tx.AuthenticatedCallback
					(InstancePtr->Tx.AuthenticatedCallbackRef);
				}
			}

			if (InstancePtr->IsRepeater == 1) {
			  if(InstancePtr->Tx.DownstreamReady == 1) {
			    InstancePtr->Tx.DownstreamReady = 0;
#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
			    /* Do nothing for HDMI. */
#else
			    /* In case of DisplayPort , read the Downstream Repeater
			     * Configuration values, and update the StateHelper flag. */
			    u32 Buf;
				XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
							&Buf, XHDCP1X_PORT_SIZE_BINFO);
				InstancePtr->Tx.StateHelper = (Buf & 0x0000FFFFu);
#endif
			    XHdcp1x_TxSetRepeaterInfo(InstancePtr);
			    if (InstancePtr->Tx.IsRepeaterExchangeCallbackSet)
			    {
			      InstancePtr->Tx.RepeaterExchangeCallback(
			        InstancePtr->Tx.RepeaterExchangeRef);
			    }
			  }
			}
			break;

		/* For the link integrity check state */
		case XHDCP1X_STATE_LINKINTEGRITYCHECK:
			XHdcp1x_TxCheckLinkIntegrity(InstancePtr,
						NextStatePtr);
			break;

		/* For the unauthenticated state */
		case XHDCP1X_STATE_UNAUTHENTICATED:
			InstancePtr->Tx.Flags &= ~XVPHY_FLAG_IS_REPEATER;
			InstancePtr->Tx.Flags |= XVPHY_FLAG_PHY_UP;
			XHdcp1x_TxDisableEncryptionState(InstancePtr);
			break;

		/* For physical layer down */
		case XHDCP1X_STATE_PHYDOWN:
			InstancePtr->Tx.Flags &= ~XVPHY_FLAG_PHY_UP;
			XHdcp1x_TxDisableEncryptionState(InstancePtr);
			XHdcp1x_CipherDisable(InstancePtr);
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function exits a state.
*
* @param	InstancePtr is the HDCP state machine.
* @param	State is the state to exit.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxExitState(XHdcp1x *InstancePtr, XHdcp1x_StateType State,
		XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of state called */
	switch (State) {
		/* For the disabled state */
		case XHDCP1X_STATE_DISABLED:
			XHdcp1x_TxEnableState(InstancePtr);
			break;

		/* For the computations state */
		case XHDCP1X_STATE_COMPUTATIONS:
			InstancePtr->Tx.StateHelper = 0;
			break;

		/* For the validate rx state */
		case XHDCP1X_STATE_VALIDATERX:
			XHdcp1x_TxStopTimer(InstancePtr);
			break;

		/* For the wait for ready state */
		case XHDCP1X_STATE_WAITFORREADY:
			if (*NextStatePtr == XHDCP1X_STATE_READKSVLIST ||
			    *NextStatePtr == XHDCP1X_STATE_UNAUTHENTICATED) {
				/* Timer already stopped. */
			} else {
				XHdcp1x_TxStopTimer(InstancePtr);
			}
			break;

		/* For the read ksv list state */
		case XHDCP1X_STATE_READKSVLIST:
			InstancePtr->Tx.StateHelper = 0;
			break;

		/* For physical layer down */
		case XHDCP1X_STATE_PHYDOWN:
			XHdcp1x_CipherEnable(InstancePtr);
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function drives a transmit state machine.
*
* @param	InstancePtr is the HDCP state machine.
* @param	Event is the event to process.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxDoTheState(XHdcp1x *InstancePtr, XHdcp1x_EventType Event)
{
	XHdcp1x_StateType NextState = InstancePtr->Tx.CurrentState;

	/* Case-wise process the kind of state called */
	switch (InstancePtr->Tx.CurrentState) {
		/* For the disabled state */
		case XHDCP1X_STATE_DISABLED:
			XHdcp1x_TxRunDisabledState(InstancePtr, Event,
				&NextState);
			break;

		/* For determine rx capable state */
		case XHDCP1X_STATE_DETERMINERXCAPABLE:
			XHdcp1x_TxRunDetermineRxCapableState(InstancePtr,
				Event, &NextState);
			break;

		/* For exchange ksvs state */
		case XHDCP1X_STATE_EXCHANGEKSVS:
			XHdcp1x_TxRunExchangeKsvsState(InstancePtr, Event,
				&NextState);
			break;

		/* For the computations state */
		case XHDCP1X_STATE_COMPUTATIONS:
			XHdcp1x_TxRunComputationsState(InstancePtr, Event,
				&NextState);
			break;

		/* For the validate rx state */
		case XHDCP1X_STATE_VALIDATERX:
			XHdcp1x_TxRunValidateRxState(InstancePtr, Event,
				&NextState);
			break;

		/* For the authenticated state */
		case XHDCP1X_STATE_AUTHENTICATED:
			XHdcp1x_TxRunAuthenticatedState(InstancePtr, Event,
				&NextState);
			break;

		/* For the link integrity check state */
		case XHDCP1X_STATE_LINKINTEGRITYCHECK:
			XHdcp1x_TxRunLinkIntegrityCheckState(InstancePtr,
				Event, &NextState);
			break;

		/* For the test for repeater state */
		case XHDCP1X_STATE_TESTFORREPEATER:
			XHdcp1x_TxRunTestForRepeaterState(InstancePtr, Event,
				&NextState);
			break;

		/* For the wait for ready state */
		case XHDCP1X_STATE_WAITFORREADY:
			XHdcp1x_TxRunWaitForReadyState(InstancePtr, Event,
				&NextState);
			break;

		/* For the reads ksv list state */
		case XHDCP1X_STATE_READKSVLIST:
			XHdcp1x_TxRunReadKsvListState(InstancePtr, Event,
				&NextState);
			break;

		/* For the unauthenticated state */
		case XHDCP1X_STATE_UNAUTHENTICATED:
			XHdcp1x_TxRunUnauthenticatedState(InstancePtr,
				Event, &NextState);
			break;

		/* For the physical layer down state */
		case XHDCP1X_STATE_PHYDOWN:
			XHdcp1x_TxRunPhysicalLayerDownState(InstancePtr,
				Event, &NextState);
			break;

		/* Otherwise */
		default:
			break;
	}

	/* Check for state change */
	while (InstancePtr->Tx.CurrentState != NextState) {
		/* Perform the state transition */
		XHdcp1x_TxExitState(InstancePtr,
				InstancePtr->Tx.CurrentState, &NextState);
		InstancePtr->Tx.PreviousState = InstancePtr->Tx.CurrentState;
		InstancePtr->Tx.CurrentState = NextState;
		XHdcp1x_TxEnterState(InstancePtr, InstancePtr->Tx.CurrentState,
				&NextState);
		if( ((InstancePtr->Tx.PreviousState != XHDCP1X_STATE_DISABLED)
		&& (InstancePtr->Tx.PreviousState != XHDCP1X_STATE_PHYDOWN))
		&& (InstancePtr->Tx.CurrentState
			== XHDCP1X_STATE_UNAUTHENTICATED) ){
			/* Clear auth request pending flag */
			InstancePtr->Tx.IsAuthReqPending = (FALSE);

			if (InstancePtr->Tx.IsUnauthenticatedCallbackSet) {
				InstancePtr->Tx.UnauthenticatedCallback(
				InstancePtr->Tx.UnauthenticatedCallbackRef);
			}
		}
	}
}

/*****************************************************************************/
/**
* This function processes the events pending on a state machine.
*
* @param	InstancePtr is the receiver instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_TxProcessPending(XHdcp1x *InstancePtr)
{
	/* Check for any pending events */
	if (InstancePtr->Tx.PendingEvents != 0) {
		u16 Pending = InstancePtr->Tx.PendingEvents;
		XHdcp1x_EventType Event = XHDCP1X_EVENT_NULL;

		/* Update InstancePtr */
		InstancePtr->Tx.PendingEvents = 0;

		/* Iterate through thePending */
		do {
			/* Check for a pending event */
			if ((Pending & 1u) != 0) {
				XHdcp1x_TxDoTheState(InstancePtr, Event);
			}

			/* Update for loop */
			Pending >>= 1;
			Event++;
		}
		while (Pending != 0);
	}
}

/*****************************************************************************/
/**
* This function converts from a state to a display string.
*
* @param	State is the state to convert.
*
* @return	The corresponding display string.
*
* @note		None.
*
******************************************************************************/
static const char *XHdcp1x_TxStateToString(XHdcp1x_StateType State)
{
	const char *String = NULL;

	/* Case-wise process the kind of state called */
	switch (State) {
		case XHDCP1X_STATE_DISABLED:
			String = "disabled";
			break;

		case XHDCP1X_STATE_DETERMINERXCAPABLE:
			String = "determine-rx-capable";
			break;

		case XHDCP1X_STATE_EXCHANGEKSVS:
			String = "exchange-ksvs";
			break;

		case XHDCP1X_STATE_COMPUTATIONS:
			String = "computations";
			break;

		case XHDCP1X_STATE_VALIDATERX:
			String = "validate-rx";
			break;

		case XHDCP1X_STATE_AUTHENTICATED:
			String = "authenticated";
			break;

		case XHDCP1X_STATE_LINKINTEGRITYCHECK:
			String = "link-integrity-check";
			break;

		case XHDCP1X_STATE_TESTFORREPEATER:
			String = "test-for-repeater";
			break;

		case XHDCP1X_STATE_WAITFORREADY:
			String = "wait-for-ready";
			break;

		case XHDCP1X_STATE_READKSVLIST:
			String = "read-ksv-list";
			break;

		case XHDCP1X_STATE_UNAUTHENTICATED:
			String = "unauthenticated";
			break;

		case XHDCP1X_STATE_PHYDOWN:
			String = "physical-layer-down";
			break;

		default:
			String = "unknown?";
			break;
	}

	return (String);
}

#if XHDCP1X_ADDITIONAL_DEBUG
/*****************************************************************************/
/**
* This function converts from a state to a display string.
*
* @param	Event is the event to convert.
*
* @return	The corresponding display string.
*
* @note		None.
*
******************************************************************************/
static const char *XHdcp1x_TxEventToString(XHdcp1x_EventType Event)
{
	const char *String = NULL;

	/* Case-wise process the kind of event called */
	switch (Event) {
		case 	XHDCP1X_EVENT_NULL:
			String = "null";
			break;

		case 	XHDCP1X_EVENT_AUTHENTICATE:
			String = "authenticate";
			break;

		case	XHDCP1X_EVENT_CHECK:
			String = "check";
			break;

		case	XHDCP1X_EVENT_DISABLE:
			String = "disable";
			break;

		case	XHDCP1X_EVENT_ENABLE:
			String = "enable";
			break;

		case	XHDCP1X_EVENT_LINKDOWN:
			String = "link-down";
			break;

		case	XHDCP1X_EVENT_PHYDOWN:
			String = "phy-down";
			break;

		case	XHDCP1X_EVENT_PHYUP:
			String = "phy-up";
			break;

		case	XHDCP1X_EVENT_POLL:
			String = "poll";
			break;

		case	XHDCP1X_EVENT_TIMEOUT:
			String = "timeout";
			break;

		case	XHDCP1X_EVENT_READDOWNSTREAM:
			String = "read-downstream";
			break;

		default:
			String = "unknown?";
			break;
	}

	return (String);
}
#endif

/** @} */

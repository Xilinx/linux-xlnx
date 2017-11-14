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
* @file xhdcp1x_rx.c
* @addtogroup hdcp1x_v4_0
* @{
*
* This contains the main implementation file for the Xilinx HDCP receive state
* machine
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 1.10  MG     01/18/16 Added function XHdcp1x_RxIsEnabled and
*                       XHdcp1x_RxIsInProgress.
* 3.0   yas    02/13/16 Upgraded to support HDCP Repeater functionality.
*                       Added a new enum XHdcp1x_EventType value:
*                       XHDCP1X_EVENT_DOWNSTREAMREADY.
*                       Added new XHdcp1x_StateType values:
*                       XHDCP1X_STATE_WAITFORDOWNSTREAM,
*                       XHDCP1X_STATE_ASSEMBLEKSVLIST.
*                       Added a new function:
*                       XHdcp1x_RxDownstreamReady, XHdcp1x_RxGetRepeaterInfo,
*                       XHdcp1x_RxSetCallBack.
*                       Updated the following functions for Repeater:
*                       XHdcp1x_RxStartComputations,
*                       XHdcp1x_RxPollForComputations, XHdcp1x_RxEnterState,
*                       XHdcp1x_RxDoTheState.
* 3.1   yas    07/28/16 Repeater functionality extended to support HDMI.
*                       Removed the XHdcp1x_RxDownstreamReadyCallback.
*                       Added fucntions,
*                       XHdcp1x_RxSetRepeaterBcaps,XHdcp1x_RxIsInComputations,
*                       XHdcp1x_RxIsInWaitforready, XHdcp1x_RxHandleTimeout,
*                       XHdcp1x_RxStartTimer, XHdcp1x_RxStopTimer,
*                       XHdcp1x_RxBusyDelay, XHdcp1x_RxSetTopologyUpdate,
*                       XHdcp1x_RxSetTopology, XHdcp1x_RxSetTopologyKSVList,
*                       XHdcp1x_RxSetTopologyDepth,
*                       XHdcp1x_RxSetTopologyDeviceCnt,
*                       XHdcp1x_RxSetTopologyMaxCascadeExceeded,
*                       XHdcp1x_RxSetTopologyMaxDevsExceeded,
*                       XHdcp1x_RxCheckEncryptionChange.
* 4.1   yas    11/10/16 Added function XHdcp1x_RxSetHdmiMode.
* </pre>
*
*****************************************************************************/

/***************************** Include Files *********************************/


//#include <stdio.h>
//#include <stdlib.h>
#include <linux/string.h>
#include "xhdcp1x.h"
#include "xhdcp1x_cipher.h"
#include "xhdcp1x_debug.h"
#include "xhdcp1x_port.h"
#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)
#include "xhdcp1x_port_hdmi.h"
#else
#include "xhdcp1x_port_dp.h"
#endif
#include "xhdcp1x_rx.h"
#include "xhdcp1x_platform.h"
#include "sha1.h"
#include "xil_types.h"

/************************** Constant Definitions *****************************/

#define XVPHY_FLAG_PHY_UP	(1u << 0)  /**< Flag to track physical state */

#define XVPHY_TMO_5MS		(5u)    /**< Timeout value for 5ms */
#define XVPHY_TMO_100MS		(100u)    /**< Timeout value for 100ms */
#define XVPHY_TMO_1SECOND	(1000u)    /**< Timeout value for 1s */

/**************************** Type Definitions *******************************/

/**
 * This enumerates the Event Types for HDCP Receiver state machine.
 */
typedef enum {
	XHDCP1X_EVENT_NULL,
	XHDCP1X_EVENT_AUTHENTICATE,
	XHDCP1X_EVENT_CHECK,
	XHDCP1X_EVENT_DISABLE,
	XHDCP1X_EVENT_ENABLE,
	XHDCP1X_EVENT_PHYDOWN,
	XHDCP1X_EVENT_PHYUP,
	XHDCP1X_EVENT_POLL,
	XHDCP1X_EVENT_UPDATERi,
	XHDCP1X_EVENT_TIMEOUT,
	XHDCP1X_EVENT_DOWNSTREAMREADY,
} XHdcp1x_EventType;

/**
 * This enumerates the State Types for HDCP Receiver state machine.
 */
typedef enum {
	XHDCP1X_STATE_DISABLED = XHDCP1X_RX_STATE_DISABLED,
	XHDCP1X_STATE_UNAUTHENTICATED = XHDCP1X_RX_STATE_UNAUTHENTICATED,
	XHDCP1X_STATE_COMPUTATIONS = XHDCP1X_RX_STATE_COMPUTATIONS,
	XHDCP1X_STATE_WAITFORDOWNSTREAM = XHDCP1X_RX_STATE_WAITFORDOWNSTREAM,
	XHDCP1X_STATE_ASSEMBLEKSVLIST = XHDCP1X_RX_STATE_ASSEMBLEKSVLIST,
	XHDCP1X_STATE_AUTHENTICATED = XHDCP1X_RX_STATE_AUTHENTICATED,
	XHDCP1X_STATE_LINKINTEGRITYFAILED = XHDCP1X_RX_STATE_LINKINTEGRITYFAILED,
	XHDCP1X_STATE_PHYDOWN = XHDCP1X_RX_STATE_PHYDOWN,
} XHdcp1x_StateType;

/***************** Macros (Inline Functions) Definitions *********************/

/*************************** Function Prototypes *****************************/

static void XHdcp1x_RxDebugLog(const XHdcp1x *InstancePtr, const char *LogMsg);
static void XHdcp1x_RxPostEvent(XHdcp1x *InstancePtr, XHdcp1x_EventType Event);
static void XHdcp1x_RxStartTimer(XHdcp1x *InstancePtr, u16 TimeoutInMs);
static void XHdcp1x_RxStopTimer(XHdcp1x *InstancePtr);
#if XHDCP1X_ADDITIONAL_DEBUG
static void XHdcp1x_RxBusyDelay(XHdcp1x *InstancePtr, u16 DelayInMs);
#endif
static void XHdcp1x_RxAuthCallback(void *Parameter);
static void XHdcp1x_RxLinkFailCallback(void *Parameter);
static void XHdcp1x_RxRiUpdateCallback(void *Parameter);
static void XHdcp1x_RxSetCheckLinkState(XHdcp1x *InstancePtr, int IsEnabled);
static void XHdcp1x_RxEnableState(XHdcp1x *InstancePtr);
static void XHdcp1x_RxDisableState(XHdcp1x *InstancePtr);
static void XHdcp1x_RxStartComputations(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxPollForComputations(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static int XHdcp1x_RxCalculateSHA1Value(XHdcp1x *InstancePtr,
		u16 RepeaterInfo);
static void XHdcp1x_RxAssembleKSVList(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxUpdateRi(XHdcp1x *InstancePtr);
static void XHdcp1x_RxCheckLinkIntegrity(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxReportLinkIntegrityFailure(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxRunDisabledState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxRunUnauthenticatedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxRunComputationsState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxRunAuthenticatedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxRunLinkIntegrityFailedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxRunPhysicalLayerDownState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxEnterState(XHdcp1x *InstancePtr, XHdcp1x_StateType State,
		XHdcp1x_StateType *NextStatePtr);
static void XHdcp1x_RxExitState(XHdcp1x *InstancePtr, XHdcp1x_StateType State);
static void XHdcp1x_RxDoTheState(XHdcp1x *InstancePtr, XHdcp1x_EventType Event);
static void XHdcp1x_RxProcessPending(XHdcp1x *InstancePtr);
static const char *XHdcp1x_RxStateToString(XHdcp1x_StateType State);
#if XHDCP1X_ADDITIONAL_DEBUG
static const char *XHdcp1x_RxEventToString(XHdcp1x_EventType Event);
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
*			callback function when it is invoked.
*
* @return
*		- XST_SUCCESS if callback function installed successfully.
*		- XST_INVALID_PARAM when HandlerType is invalid.
*
* @note		Invoking this function for a handler that already has been
*			installed replaces it with the new handler.
*
******************************************************************************/
int XHdcp1x_RxSetCallback(XHdcp1x *InstancePtr,
	XHdcp1x_HandlerType HandlerType, void *CallbackFunc, void *CallbackRef)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(HandlerType > (XHDCP1X_HANDLER_UNDEFINED));
	Xil_AssertNonvoid(HandlerType < (XHDCP1X_HANDLER_INVALID));
	Xil_AssertNonvoid(CallbackFunc != NULL);
	Xil_AssertNonvoid(CallbackRef != NULL);

	u32 Status;

	/* Check for handler type */
	switch (HandlerType)
	{
		case (XHDCP1X_HANDLER_DDC_SETREGADDR):
			InstancePtr->Rx.DdcSetAddressCallback
				= (XHdcp1x_SetDdcHandler)CallbackFunc;
			InstancePtr->Rx.DdcSetAddressCallbackRef
				= CallbackRef;
			InstancePtr->Rx.IsDdcSetAddressCallbackSet = (TRUE);
			Status = XST_SUCCESS;
			break;

		case (XHDCP1X_HANDLER_DDC_SETREGDATA):
			InstancePtr->Rx.DdcSetDataCallback
				= (XHdcp1x_SetDdcHandler)CallbackFunc;
			InstancePtr->Rx.DdcSetDataCallbackRef
				= CallbackRef;
			InstancePtr->Rx.IsDdcSetDataCallbackSet = (TRUE);
			Status = XST_SUCCESS;
			break;

		case (XHDCP1X_HANDLER_DDC_GETREGDATA):
			InstancePtr->Rx.DdcGetDataCallback
				= (XHdcp1x_GetDdcHandler)CallbackFunc;
			InstancePtr->Rx.DdcGetDataCallbackRef
				= CallbackRef;
			InstancePtr->Rx.IsDdcGetDataCallbackSet = (TRUE);
			Status = XST_SUCCESS;
			break;

		/* Repeater - trigger Downstream Authentication */
		/* Eqilvalent of case
		 * (XHDCP1X_RPTR_HDLR_TRIG_DOWNSTREAM_AUTH):
		 */
		case (XHDCP1X_HANDLER_RPTR_TRIGDWNSTRMAUTH):
			InstancePtr->Rx.RepeaterDownstreamAuthCallback
				= (XHdcp1x_Callback)CallbackFunc;
			InstancePtr->Rx.RepeaterDownstreamAuthRef
				= CallbackRef;
			InstancePtr->Rx.IsRepeaterDownstreamAuthCallbackSet
				= (TRUE);
			Status = XST_SUCCESS;
			break;

		// authenticated
		case (XHDCP1X_HANDLER_AUTHENTICATED):
			InstancePtr->Rx.AuthenticatedCallback
				= (XHdcp1x_Callback)CallbackFunc;
			InstancePtr->Rx.AuthenticatedCallbackRef
				= CallbackRef;
			InstancePtr->Rx.IsAuthenticatedCallbackSet = (TRUE);
			Status = XST_SUCCESS;
			break;

		// unauthenticated
		case (XHDCP1X_HANDLER_UNAUTHENTICATED):
			InstancePtr->Rx.UnauthenticatedCallback
				= (XHdcp1x_Callback)CallbackFunc;
			InstancePtr->Rx.UnauthenticatedCallbackRef
				= CallbackRef;
			InstancePtr->Rx.IsUnauthenticatedCallbackSet = (TRUE);
			Status = XST_SUCCESS;
			break;

		// topology updated
		case (XHDCP1X_HANDLER_TOPOLOGY_UPDATE):
			InstancePtr->Rx.TopologyUpdateCallback
				= (XHdcp1x_Callback)CallbackFunc;
			InstancePtr->Rx.TopologyUpdateCallbackRef
				= CallbackRef;
			InstancePtr->Rx.IsTopologyUpdateCallbackSet = (TRUE);
			Status = XST_SUCCESS;
			break;

		// encryption updated
		case (XHDCP1X_HANDLER_ENCRYPTION_UPDATE):
			InstancePtr->Rx.EncryptionUpdateCallback
				= (XHdcp1x_Callback)CallbackFunc;
			InstancePtr->Rx.EncryptionUpdateCallbackRef
				= CallbackRef;
			InstancePtr->Rx.IsEncryptionUpdateCallbackSet = (TRUE);
			Status = XST_SUCCESS;
			break;

		default:
			Status = XST_INVALID_PARAM;
			break;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function initializes a HDCP receiver state machine.
*
* @param	InstancePtr is the receiver instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_RxInit(XHdcp1x *InstancePtr)
{
	XHdcp1x_StateType DummyState = XHDCP1X_STATE_DISABLED;

	/* Update theHandler */
	InstancePtr->Rx.PendingEvents = 0;

	/* Kick the state machine */
	XHdcp1x_RxEnterState(InstancePtr, XHDCP1X_STATE_DISABLED, &DummyState);
}

/*****************************************************************************/
/**
* This function polls the HDCP receiver module.
*
* @param	InstancePtr is the receiver instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
 ******************************************************************************/
int XHdcp1x_RxPoll(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Process any pending events */
	XHdcp1x_RxProcessPending(InstancePtr);

	/* Poll it */
	XHdcp1x_RxDoTheState(InstancePtr, XHDCP1X_EVENT_POLL);

	return (Status);
}

/*****************************************************************************/
/**
* This function set the REPEATER bit for the HDCP RX interface.
*
* @param	InstancePtr is the receiver instance.
* @param	IsRptr is the truth value to determine if the repeater bit
* 		in the port registers is to be set.
* @return
*		- XST_SUCCESS if successful.
*
* @note		This function disables and then re-enables the interface.
*
 ******************************************************************************/
int XHdcp1x_RxSetRepeaterBcaps(XHdcp1x *InstancePtr, u8 IsRptr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	Status = XHdcp1x_PortSetRepeater(InstancePtr, IsRptr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function resets an HDCP interface.
*
* @param	InstancePtr is the receiver instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		This function disables and then re-enables the interface.
*
 ******************************************************************************/
int XHdcp1x_RxReset(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Reset it */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_DISABLE);
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_ENABLE);

	return (Status);
}

/*****************************************************************************/
/**
* This function enables a HDCP receive interface.
*
* @param	InstancePtr is the receiver instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxEnable(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Post it */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_ENABLE);

	return (Status);
}

/*****************************************************************************/
/**
* This function disables a HDCP receive interface.
*
* @param	InstancePtr is the receiver instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxDisable(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Post it */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_DISABLE);

	return (Status);
}

/*****************************************************************************/
/**
* This function queries an interface to check if is enabled.
*
* @param	InstancePtr is the receiver instance.
*
* @return	Truth value indicating is enabled (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxIsEnabled(const XHdcp1x *InstancePtr)
{
	int IsEnabled = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Case-wise process the current state of Rx state machine */
	switch (InstancePtr->Rx.CurrentState) {
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
* @param	InstancePtr is the receiver instance.
* @param	IsUp is truth value indicating the status of physical interface.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxSetPhysicalState(XHdcp1x *InstancePtr, int IsUp)
{
	int Status = XST_SUCCESS;
	XHdcp1x_EventType Event = XHDCP1X_EVENT_PHYDOWN;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine Event */
	if (IsUp) {
		Event = XHDCP1X_EVENT_PHYUP;
	}

	/* Post it */
	XHdcp1x_RxPostEvent(InstancePtr, Event);

	return (Status);
}

/*****************************************************************************/
/**
* This function set the lane count of an hdcp interface.
*
* @param	InstancePtr is the receiver instance.
* @param	LaneCount is the number of lanes of the interface.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxSetLaneCount(XHdcp1x *InstancePtr, int LaneCount)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(LaneCount > 0);

	/* Set it */
	return (XHdcp1x_CipherSetNumLanes(InstancePtr, LaneCount));
}

/*****************************************************************************/
/**
* This function initiates downstream ready/ assemble ksv list on an interface.
*
* @param	InstancePtr is the receiver instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxDownstreamReady(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Post the re-authentication request */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_DOWNSTREAMREADY);

	return (Status);
}

/*****************************************************************************/
/**
* This function initiates authentication on an interface.
*
* @param	InstancePtr is the receiver instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxAuthenticate(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Post the re-authentication request */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_AUTHENTICATE);

	return (Status);
}

/*****************************************************************************/
/**
* This function queries an interface to check if authentication is in
* progress.
*
* @param	InstancePtr is the receiver instance.
*
* @return	Truth value indicating in progress (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxIsInProgress(const XHdcp1x *InstancePtr)
{
	int IsInProgress = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Case-wise process the current state of Rx state machine  */
	switch (InstancePtr->Rx.CurrentState) {
		case XHDCP1X_STATE_COMPUTATIONS:
			IsInProgress = TRUE;
			break;

		/* Otherwise */
		default:
			IsInProgress = FALSE;
			break;
	}

	return (IsInProgress);
}

/*****************************************************************************/
/**
* This function queries an interface to check if its been authenticated.
*
* @param	InstancePtr is the receiver instance.
*
* @return	Truth value indicating authenticated (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxIsAuthenticated(const XHdcp1x *InstancePtr)
{
	int IsAuthenticated = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine IsAuthenticated */
	if (InstancePtr->Rx.CurrentState == XHDCP1X_STATE_AUTHENTICATED) {
		IsAuthenticated = TRUE;
	}

	return (IsAuthenticated);
}

/*****************************************************************************/
/**
* This function queries an interface to check if its in the computations state.
*
* @param	InstancePtr is the receiver instance.
*
* @return	Truth value indicating authenticated (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxIsInComputations(const XHdcp1x *InstancePtr)
{
	int IsInComp = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine IsAuthenticated */
	if (InstancePtr->Rx.CurrentState == XHDCP1X_STATE_COMPUTATIONS) {
		IsInComp = TRUE;
	}

	return (IsInComp);
}

/*****************************************************************************/
/**
* This function queries an interface to check if its in the
* wait-for-downstream-ready state.
*
* @param	InstancePtr is the receiver instance.
*
* @return	Truth value indicating authenticated (true) or not (false).
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxIsInWaitforready(const XHdcp1x *InstancePtr)
{
	int IsInWfr = FALSE;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Determine IsAuthenticated */
	if (InstancePtr->Rx.CurrentState == XHDCP1X_STATE_WAITFORDOWNSTREAM) {
		IsInWfr = TRUE;
	}

	return (IsInWfr);
}

/*****************************************************************************/
/**
* This function retrieves the current encryption stream map.
*
* @param	InstancePtr is the receiver instance.
*
* @return	The current encryption stream map.
*
* @note		None.
*
******************************************************************************/
u64 XHdcp1x_RxGetEncryption(const XHdcp1x *InstancePtr)
{
	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Get it */
	return (XHdcp1x_CipherGetEncryption(InstancePtr));
}

/*****************************************************************************/
/**
* This function handles a timeout on an HDCP interface.
*
* @param	InstancePtr is the receiver instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_RxHandleTimeout(XHdcp1x *InstancePtr)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Post the timeout */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_TIMEOUT);
}

/*****************************************************************************/
/**
* This function implements the debug display output for receiver instances.
*
* @param	InstancePtr is the receiver instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxInfo(const XHdcp1x *InstancePtr)
{
	u64 LocalKsv = 0;
	u32 Version = 0;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Display it */
	XDEBUG_PRINTF("Type:            ");
	if (InstancePtr->Config.IsHDMI) {
		XDEBUG_PRINTF("hdmi-rx\r\n");
	}
	else {
		XDEBUG_PRINTF("dp-rx\r\n");
	}
	XDEBUG_PRINTF("Current State:   %s\r\n",
		XHdcp1x_RxStateToString(InstancePtr->Rx.CurrentState));
	XDEBUG_PRINTF("Previous State:  %s\r\n",
		XHdcp1x_RxStateToString(InstancePtr->Rx.PreviousState));
	XDEBUG_PRINTF("Encrypted?:      %s\r\n",
		XHdcp1x_IsEncrypted(InstancePtr) ? "Yes" : "No");
	XDEBUG_PRINTF("Flags:           %04X\r\n",
		InstancePtr->Rx.Flags);
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
	XDEBUG_PRINTF("Rx Stats\r\n");
	XDEBUG_PRINTF("Auth Attempts:   %d\r\n",
		InstancePtr->Rx.Stats.AuthAttempts);
	XDEBUG_PRINTF("Link Failures:   %d\r\n",
		InstancePtr->Rx.Stats.LinkFailures);
	XDEBUG_PRINTF("Ri Updates:      %d\r\n",
		InstancePtr->Rx.Stats.RiUpdates);

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
* This function copies the V'H0, V'H1, V'H2, V'H3, V'H4, KSVList and
* BInfo values in the HDCP RX HDCP Instance for Repeater validation .
*
* @param	InstancePtr is the receiver instance.
* @param	RepeaterInfoPtr is the Repeater information in the
* 		transmitter instance.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_RxGetRepeaterInfo(XHdcp1x *InstancePtr,
		XHdcp1x_RepeaterExchange *RepeaterInfoPtr)
{
	int Status = XST_SUCCESS;
	u32 ksvCount=0;
	u32 ksvsToCopy;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(RepeaterInfoPtr != NULL);

	/*
	 * Copy the Depth read from the downstream HDCP device and
	 * increment it by one to account for the Repeater itself
	 */
	InstancePtr->RepeaterValues.Depth = RepeaterInfoPtr->Depth + 1;

	/* Copy the device count read from the downstream HDCP device */
	InstancePtr->RepeaterValues.DeviceCount
			= RepeaterInfoPtr->DeviceCount;

	/* Copy the KSVList */
	ksvsToCopy = InstancePtr->RepeaterValues.DeviceCount;

	for(ksvCount=0 ; ksvCount < ksvsToCopy ; ksvCount++) {
		InstancePtr->RepeaterValues.KsvList[ksvCount]
			= RepeaterInfoPtr->KsvList[ksvCount];
	}

	/* Copy the SHA1 Hash value V'H0 */
	InstancePtr->RepeaterValues.V[0] = RepeaterInfoPtr->V[0];

	/* Copy the SHA1 Hash value V'H1 */
	InstancePtr->RepeaterValues.V[1] = RepeaterInfoPtr->V[1];

	/* Copy the SHA1 Hash value V'H2 */
	InstancePtr->RepeaterValues.V[2] = RepeaterInfoPtr->V[2];

	/* Copy the SHA1 Hash value V'H3 */
	InstancePtr->RepeaterValues.V[3] = RepeaterInfoPtr->V[3];

	/* Copy the SHA1 Hash value V'H4 */
	InstancePtr->RepeaterValues.V[4] = RepeaterInfoPtr->V[4];

	return (Status);
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
static void XHdcp1x_RxDebugLog(const XHdcp1x *InstancePtr, const char *LogMsg)
{
	char Label[16];

	/* Format Label */
	snprintf(Label, 16, "hdcp-rx(%d) - ", InstancePtr->Config.DeviceId);

	/* Log it */
	XHDCP1X_DEBUG_LOGMSG(Label);
	XHDCP1X_DEBUG_LOGMSG(LogMsg);
	XHDCP1X_DEBUG_LOGMSG("\n");
}

/*****************************************************************************/
/**
* This function posts an event to a state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to post.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxPostEvent(XHdcp1x *InstancePtr, XHdcp1x_EventType Event)
{
	/* Check for disable and clear any pending enable */
	if (Event == XHDCP1X_EVENT_DISABLE) {
		InstancePtr->Rx.PendingEvents &= ~(1u << XHDCP1X_EVENT_ENABLE);
	}
	/* Check for phy-down and clear any pending phy-up */
	else if (Event == XHDCP1X_EVENT_PHYDOWN) {
		InstancePtr->Rx.PendingEvents &= ~(1u << XHDCP1X_EVENT_PHYUP);
	}

	/* Post it */
	InstancePtr->Rx.PendingEvents |= (1u << Event);
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
static void XHdcp1x_RxStartTimer(XHdcp1x *InstancePtr, u16 TimeoutInMs)
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
static void XHdcp1x_RxStopTimer(XHdcp1x *InstancePtr)
{
	/* Stop it */
	XHdcp1x_PlatformTimerStop(InstancePtr);
}

#if XHDCP1X_ADDITIONAL_DEBUG
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
static void XHdcp1x_RxBusyDelay(XHdcp1x *InstancePtr, u16 DelayInMs)
{
	/* Busy wait */
	XHdcp1x_PlatformTimerBusy(InstancePtr, DelayInMs);
}
#endif

/*****************************************************************************/
/**
* This function acts as the re-authentication callback for a state machine.
*
* @param	Parameter is the parameter specified during registration.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxAuthCallback(void *Parameter)
{
	XHdcp1x *InstancePtr = (XHdcp1x *)Parameter;

	/* Post the re-authentication request */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_AUTHENTICATE);
}

/*****************************************************************************/
/**
* This function acts as the link failure callback for a state machine.
*
* @param	Parameter is the parameter specified during registration.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxLinkFailCallback(void *Parameter)
{
	XHdcp1x *InstancePtr = (XHdcp1x *)Parameter;

	/* Post the check request */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_CHECK);
}

/*****************************************************************************/
/**
* This function acts as the Ri register update callback for a state machine.
*
* @param	Parameter is the parameter specified during registration.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRiUpdateCallback(void *Parameter)
{
	XHdcp1x *InstancePtr = (XHdcp1x *)Parameter;

	if(InstancePtr->Rx.CurrentState == XHDCP1X_STATE_AUTHENTICATED) {
		/* Update the Ri value. */
		XHdcp1x_RxUpdateRi(InstancePtr);
	} else {
		/* Post the update Ri request. */
		XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_UPDATERi);
	}
}

/*****************************************************************************/
/**
* This function sets the check link state of the handler.
*
* @param	InstancePtr is the receiver instance.
* @param	IsEnabled is truth value indicating on/off.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxSetCheckLinkState(XHdcp1x *InstancePtr, int IsEnabled)
{
	/* Check for HDMI */
	if (InstancePtr->Config.IsHDMI) {
		XHdcp1x_CipherSetRiUpdate(InstancePtr, IsEnabled);
	}
	/* Check for DP */
	else {
		XHdcp1x_CipherSetLinkStateCheck(InstancePtr, IsEnabled);
	}
}

/*****************************************************************************/
/**
* This function enables a receiver state machine.
*
* @param	InstancePtr is the receiver instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxEnableState(XHdcp1x *InstancePtr)
{
	u64 MyKsv = 0;
	u8 Buf[8];

	/* Disable and register the link failure callback */
	XHdcp1x_CipherSetLinkStateCheck(InstancePtr, FALSE);
	XHdcp1x_CipherSetCallback(InstancePtr,
			XHDCP1X_CIPHER_HANDLER_LINK_FAILURE,
			&XHdcp1x_RxLinkFailCallback, InstancePtr);

	/* Disable and register the Ri callback */
	XHdcp1x_CipherSetRiUpdate(InstancePtr, FALSE);
	XHdcp1x_CipherSetCallback(InstancePtr,
			XHDCP1X_CIPHER_HANDLER_Ri_UPDATE,
			&XHdcp1x_RxRiUpdateCallback, InstancePtr);

	/* Enable the crypto engine */
	XHdcp1x_CipherEnable(InstancePtr);

	/* Read MyKsv */
	MyKsv = XHdcp1x_CipherGetLocalKsv(InstancePtr);

	/* If unknown - try again for good luck */
	if (MyKsv == 0) {
		MyKsv = XHdcp1x_CipherGetLocalKsv(InstancePtr);
	}

	/* Initialize Bksv */
	memset(Buf, 0, 8);
	XHDCP1X_PORT_UINT_TO_BUF(Buf, MyKsv,
			XHDCP1X_PORT_SIZE_BKSV * 8);
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BKSV, Buf,
			XHDCP1X_PORT_SIZE_BKSV);

	/* Register the re-authentication callback */
	XHdcp1x_PortSetCallback(InstancePtr,
			XHDCP1X_PORT_HANDLER_AUTHENTICATE,
			&XHdcp1x_RxAuthCallback, InstancePtr);

	/* Enable the hdcp port */
	XHdcp1x_PortEnable(InstancePtr);

	/* Update the hdcp encryption status */
	InstancePtr->Rx.XORState.CurrentState = FALSE;
}

/*****************************************************************************/
/**
* This function disables a receiver state machine.
*
* @param	InstancePtr is the receiver instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxDisableState(XHdcp1x *InstancePtr)
{
	/* Disable the hdcp cipher and port */
	XHdcp1x_PortDisable(InstancePtr);
	XHdcp1x_CipherDisable(InstancePtr);

	/* Clear statistics */
	memset(&(InstancePtr->Rx.Stats), 0, sizeof(InstancePtr->Rx.Stats));
}

/*****************************************************************************/
/**
* This function initiates the computations for a receiver state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxStartComputations(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	/* NextStatePtr not being used */
	UNUSED(NextStatePtr);

	u8 Buf[8];
	u64 Value = 0;
	u32 X = 0;
	u32 Y = 0;
	u32 Z = 0;

	/* Log */
	XHdcp1x_RxDebugLog(InstancePtr, "starting computations");

	/* Update statistics */
	InstancePtr->Rx.Stats.AuthAttempts++;

	/* Determine theAKsv */
	memset(Buf, 0, 8);
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_AKSV, Buf,
			XHDCP1X_PORT_SIZE_AKSV);
	XHDCP1X_PORT_BUF_TO_UINT(Value, Buf, XHDCP1X_PORT_SIZE_AKSV * 8);

	/* Load the cipher with the remote ksv */
	XHdcp1x_CipherSetRemoteKsv(InstancePtr, Value);

	/* Update theU64Value with An */
	memset(Buf, 0, 8);
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_AN, Buf,
			XHDCP1X_PORT_SIZE_AN);
	XHDCP1X_PORT_BUF_TO_UINT(Value, Buf, XHDCP1X_PORT_SIZE_AN * 8);

	/* Load the cipher B registers with An */
	X = (u32)(Value & 0x0FFFFFFFul);
	Value >>= 28;
	Y = (u32)(Value & 0x0FFFFFFFul);
	Value >>= 28;
	if (InstancePtr->IsRepeater) {
		Z = (u32)((Value | 0x00000100) & 0x000001FFul);
	}
	else {
		Z = (u32)(Value & 0x000000FFul);
	}
	XHdcp1x_CipherSetB(InstancePtr, X, Y, Z);

	/* Initiate the block cipher */
	XHdcp1x_CipherDoRequest(InstancePtr, XHDCP1X_CIPHER_REQUEST_BLOCK);
}

/*****************************************************************************/
/**
* This function polls the progress of the computations for a state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxPollForComputations(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	/* Check for done */
	if (XHdcp1x_CipherIsRequestComplete(InstancePtr)) {
		u8 Buf[4];
		u16 Ro = 0;

		/* Log */
		XHdcp1x_RxDebugLog(InstancePtr, "computations complete");

		/* Read theRo */
		Ro = XHdcp1x_CipherGetRo(InstancePtr);

		/* Initialize Buf */
		memset(Buf, 0, 4);
		XHDCP1X_PORT_UINT_TO_BUF(Buf, Ro, 16);

		/* Update the value of Ro' */
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_RO,
				Buf, 2);

#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)
		/* No reset required in the sofware for HDMI. KSV fifo
		 * pointer reset is implemented in the hardware. */

#else
		u32 KSVPtrReset = 0;

		/* Reset the KSV FIFO read pointer to ox6802C */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_HDCP_RESET_KSV,
				&KSVPtrReset, 4);
		KSVPtrReset |= XHDCP1X_PORT_HDCP_RESET_KSV_RST;
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_HDCP_RESET_KSV,
				&KSVPtrReset, 4);

		KSVPtrReset &= ~XHDCP1X_PORT_HDCP_RESET_KSV_RST;
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_HDCP_RESET_KSV,
				&KSVPtrReset, 4);

#if defined(XHDCP1X_PORT_BIT_BSTATUS_RO_AVAILABLE)
		/* Update the Bstatus to indicate Ro' available */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
				Buf, XHDCP1X_PORT_SIZE_BSTATUS);
		Buf[0] |= XHDCP1X_PORT_BIT_BSTATUS_RO_AVAILABLE;
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
				Buf, XHDCP1X_PORT_SIZE_BSTATUS);
#endif

#endif

		if (InstancePtr->IsRepeater) {
			*NextStatePtr = XHDCP1X_STATE_WAITFORDOWNSTREAM;
			if (InstancePtr->Rx.
				IsRepeaterDownstreamAuthCallbackSet) {
				if (InstancePtr->Rx.
					RepeaterDownstreamAuthCallback
							!= NULL) {
				InstancePtr->Rx.RepeaterDownstreamAuthCallback
				(InstancePtr->Rx.RepeaterDownstreamAuthRef);
				}
				else {
					XHdcp1x_RxDebugLog(InstancePtr,
					"Warning: Repeater Downstream"
					"interface not triggered,"
					"CallBack not Initialized !!!");
				}
			}
		}
		else {
			*NextStatePtr = XHDCP1X_STATE_AUTHENTICATED;
		}
	}
	else {
		XHdcp1x_RxDebugLog(InstancePtr, "waiting for computations");
	}
}

/*****************************************************************************/
/**
* This function calculates the SHA-1 Hash value for the Repeater Rx interface.
*
* @param	InstancePtr is the hdcp state machine.
* @param	RepeaterInfo is the repeater information.
*
* @return	Truth value indicating valid (TRUE) or invalid (FALSE).
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_RxCalculateSHA1Value(XHdcp1x *InstancePtr, u16 RepeaterInfo)
{
	SHA1Context Sha1Context;
	u8 Buf[24];
	u32 NumToRead = 0;
	int IsValid = FALSE;
	u32 KsvCount;
	u64 tempKsv;

	/* Initialize Buf */
	memset(Buf, 0, 24);

	/* Initialize Sha1Context */
	SHA1Reset(&Sha1Context);

	/* Assume success */
	IsValid = TRUE;

	/* Determine theNumToRead */
	NumToRead = ((RepeaterInfo & 0x7Fu));

	/* Read one ksv at a time from the
	 * Ksv List and send it to SHA1 Input
	 */
	KsvCount = 0;

	while (KsvCount < NumToRead) {
		if (InstancePtr->RepeaterValues.DeviceCount > 0) {
			tempKsv = InstancePtr->RepeaterValues.KsvList[KsvCount];
			XHDCP1X_PORT_UINT_TO_BUF(Buf , tempKsv ,
					(XHDCP1X_PORT_SIZE_BKSV*8));
			SHA1Input(&Sha1Context , Buf , XHDCP1X_PORT_SIZE_BKSV);
		}
		else {
			IsValid = FALSE;
		}
		KsvCount++;
	}

	/* Check for success */
	if (IsValid) {
		u64 Mo = 0;
		u8 Sha1Result[SHA1HashSize];

		/* Insert RepeaterInfo into the SHA-1 transform */
		Buf[0] = (u8) (RepeaterInfo & 0xFFu);
		Buf[1] = (u8) ((RepeaterInfo >> 8) & 0xFFu);
		SHA1Input(&Sha1Context, Buf, 2);

		/* Insert the Mo into the SHA-1 transform */
		Mo = XHdcp1x_CipherGetMo(InstancePtr);
		XHDCP1X_PORT_UINT_TO_BUF(Buf, Mo, 64);

		SHA1Input(&Sha1Context, Buf, 8);

		/* Finalize the SHA-1 result and confirm success */
		if (SHA1Result(&Sha1Context, Sha1Result) == shaSuccess) {
			/* Offset(XHDCP1X_PORT_OFFSET_VH0) = 0 */
			u8 Offset = 0;
			const u8 *Sha1Buf = Sha1Result;
			int NumIterations = (SHA1HashSize >> 2);

			/* Iterate through the SHA-1 chunks */
			do {
				u32 CalcValue = 0;

				/* Determine CalcValue */
				CalcValue = *Sha1Buf++;
				CalcValue <<= 8;
				CalcValue |= *Sha1Buf++;
				CalcValue <<= 8;
				CalcValue |= *Sha1Buf++;
				CalcValue <<= 8;
				CalcValue |= *Sha1Buf++;

				/* Update the V' value in the Instance
				 * Pointer for the HDCP state machine
				 */
				InstancePtr->RepeaterValues.V[Offset]
					= CalcValue;

				/* Update for loop */
				Offset++;
				NumIterations--;
			}
			while (NumIterations > 0);
		}
		/* Otherwise */
		else {
			IsValid = FALSE;
		}
	}

	return (IsValid);
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
void XHdcp1x_RxSetTopologyUpdate(XHdcp1x *InstancePtr)
{
	/* Post the re-authentication request */
	XHdcp1x_RxPostEvent(InstancePtr, XHDCP1X_EVENT_DOWNSTREAMREADY);
}

/*****************************************************************************/
/**
* This function sets the RepeaterInfo value int the HDCP RX instance
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
* @param    TopologyPtr is the pointer to topology information.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_RxSetTopology(XHdcp1x *InstancePtr,
		const XHdcp1x_RepeaterExchange *TopologyPtr)
{
	memcpy(&(InstancePtr->RepeaterValues), TopologyPtr,
			sizeof(XHdcp1x_RepeaterExchange));
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
void XHdcp1x_RxSetTopologyKSVList(XHdcp1x *InstancePtr, u8 *ListPtr,
		u32 ListSize)
{
	u32 i;

	for(i=0 ; i<ListSize; i++) {
		memcpy(&InstancePtr->RepeaterValues.KsvList[i],
				(ListPtr + (i*5)), XHDCP1X_PORT_SIZE_BKSV);
	}
}

/*****************************************************************************/
/**
* This function sets the Depth value in the HDCP RX BStatus/BInfo register
* space for the upstream interface to read.
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
* @param    Value is the Depth value.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_RxSetTopologyDepth(XHdcp1x *InstancePtr, u32 Value)
{
	InstancePtr->RepeaterValues.Depth = Value;
}

/*****************************************************************************/
/**
* This function sets the DEVICE_COUNT value in the HDCP RX register space
* for the upstream interface to read.
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
* @param    Value is the device count value.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_RxSetTopologyDeviceCnt(XHdcp1x *InstancePtr, u32 Value)
{
	InstancePtr->RepeaterValues.DeviceCount = Value;
}

/*****************************************************************************/
/**
* This function sets the MAX_CASCADE_EXCEEDED error flag in the HDCP
* BStatus/BInfo register to indicate a topology error. Setting the flag
* indicates a depth of more than (4 - 1).
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
* @param    Value is either TRUE or FALSE.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_RxSetTopologyMaxCascadeExceeded(XHdcp1x *InstancePtr, u8 Value)
{
#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)

	u32 BStatus;

	/* Update the value of Max Devices exceeded in BStatus */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
		&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);
	BStatus |= (u32)(Value << XHDCP1X_PORT_BSTATUS_DEPTH_ERR_SHIFT);
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
		&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);

#else

	u32 BInfo;

	/* Update the value of Depth in BInfo */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
		&BInfo, XHDCP1X_PORT_SIZE_BINFO);
	BInfo |= (Value << XHDCP1X_PORT_BINFO_DEPTH_ERR_SHIFT);
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
		&BInfo, XHDCP1X_PORT_SIZE_BINFO);

#endif
}

/*****************************************************************************/
/**
* This function sets the MAX_DEVS_EXCEEDED error flag in the HDCP
* BStatus register to indicate a topology error. Setting the flag
* indicates that more than 31 downstream devices are attached.
*
* @param    InstancePtr is a pointer to the Hdcp1x core instance.
* @param    Value is either TRUE or FALSE.
*
* @return   None.
*
* @note     None.
******************************************************************************/
void XHdcp1x_RxSetTopologyMaxDevsExceeded(XHdcp1x *InstancePtr, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Value == FALSE || Value == TRUE);

#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)

	u16 DevCntErr = (Value & 0xFFFF);
	u32 BStatus;

	/* Update the value of Max Devices exceeded in BStatus */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
		&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);
	BStatus |= (DevCntErr << XHDCP1X_PORT_BSTATUS_DEV_CNT_ERR_SHIFT);
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
		&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);

#else

	u32 BInfo;

	/* Update the value of Depth in BInfo */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
		&BInfo, XHDCP1X_PORT_SIZE_BINFO);
	BInfo |= (Value << XHDCP1X_PORT_BINFO_DEV_CNT_ERR_SHIFT);
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
		&BInfo, XHDCP1X_PORT_SIZE_BINFO);

#endif
}

/*****************************************************************************/
/**
* This function set the HDMI_MODE in the BStatus register of the HDMI RX DDC
* space.
*
* @param	InstancePtr is a pointer to the Hdcp1x core instance.
* @param	Value is the truth-value.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp1x_RxSetHdmiMode(XHdcp1x *InstancePtr, u8 Value)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)
	u32 BStatus;

	/* Update the value of HDMI_MODE bit in the BStatus Register */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
		&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);
	if (Value == TRUE) {
		BStatus |= XHDCP1X_PORT_BIT_BSTATUS_HDMI_MODE;
	} else {
		BStatus &= ~XHDCP1X_PORT_BIT_BSTATUS_HDMI_MODE;
	}
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
		&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);
#else
	UNUSED(Value);
#endif
}

/*****************************************************************************/
/**
* This function writes the KSV List and the BInfo values to the RX
* DPCD register space and sets the READY bit.
*
* @param	InstancePtr is the receiver instance.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxAssembleKSVList(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(NextStatePtr != NULL);

	/* Check if the max cascade and max depth is not exceeded */
	if (InstancePtr->RepeaterValues.Depth > XHDCP1X_RPTR_MAX_CASCADE) {
		XHdcp1x_RxDebugLog(InstancePtr, "Repeater maximum\
						cascade exceeded");
		*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
	}
	else if (InstancePtr->RepeaterValues.DeviceCount >
				XHDCP1X_RPTR_MAX_DEVS_COUNT) {
		XHdcp1x_RxDebugLog(InstancePtr, "Repeater maximum\
						Depth exceeded");
		*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
	}
	else {
#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)
		u32 BCaps;
#else
		u32 BInfo;
		u32 KSVPtrReset;
#endif

		u32 BStatus;
		u8 Buf[5];
		u32 sha1value;
		u32 ksvCount, ksvsToWrite;
		u16 RepeaterInfo = 0;

#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)

		/* Ensure that the READY bit is clear */
		/* Update the Ready bit in the BCaps Register */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
				&BCaps, XHDCP1X_PORT_SIZE_BCAPS);
		if(BCaps & XHDCP1X_PORT_BIT_BCAPS_READY) {
			BCaps &= ~XHDCP1X_PORT_BIT_BCAPS_READY;
			XHdcp1x_PortWrite(InstancePtr,
				XHDCP1X_PORT_OFFSET_BCAPS ,
				&BCaps, XHDCP1X_PORT_SIZE_BCAPS);
		}

		/* Update the value of Depth and Device count in BStatus */
		memset(Buf,0,5);
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
			&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);
		BStatus |= ((InstancePtr->RepeaterValues.Depth<<8) & 0x0700);
		BStatus |= ((InstancePtr->RepeaterValues.DeviceCount) & 0x007F);
		BStatus |= XHDCP1X_PORT_BIT_BSTATUS_HDMI_MODE;
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
			&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);

#else
		/* Update the value of Depth in BInfo */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
			&BInfo, XHDCP1X_PORT_SIZE_BINFO);
		BInfo |= ((InstancePtr->RepeaterValues.Depth<<8) & 0x0700);
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
			&BInfo, XHDCP1X_PORT_SIZE_BINFO);

		/*Update the value of Device Count in the BInfo register */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
			&BInfo, XHDCP1X_PORT_SIZE_BINFO);
		BInfo |= ((InstancePtr->RepeaterValues.DeviceCount) & 0x007F);
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
			&BInfo, XHDCP1X_PORT_SIZE_BINFO);

		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BINFO,
			&BInfo, XHDCP1X_PORT_SIZE_BINFO);
#endif
		/* Update the KSV List in the KSV Fifo */
		ksvsToWrite = InstancePtr->RepeaterValues.DeviceCount;
		ksvCount = 0;
		while(ksvsToWrite>0) {
			u64 tempKsv;
			memset(Buf,0,5);
			tempKsv = InstancePtr->RepeaterValues.KsvList[ksvCount];
			XHDCP1X_PORT_UINT_TO_BUF(Buf, tempKsv ,
					(XHDCP1X_PORT_SIZE_BKSV * 8));
#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)

			/* Write the KSV to the HDCP_DAT register each time,
			 * The KSV Fifo will auto increment
			 */
			XHdcp1x_PortWrite(InstancePtr,
					XHDCP1X_PORT_OFFSET_KSVFIFO,
					Buf, XHDCP1X_PORT_SIZE_BKSV);

#else
			XHdcp1x_PortWrite(InstancePtr,
					( XHDCP1X_PORT_OFFSET_KSVFIFO +
					(ksvCount * XHDCP1X_PORT_SIZE_BKSV) ),
					Buf, XHDCP1X_PORT_SIZE_BKSV);
#endif
			ksvCount++;
			ksvsToWrite -= 1;
		}

#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)
		RepeaterInfo = (XHDCP1X_PORT_BIT_BSTATUS_HDMI_MODE) |
				(XHDCP1X_PORT_BSTATUS_BIT_DEPTH_NO_ERR) |
			(InstancePtr->RepeaterValues.Depth <<
					XHDCP1X_PORT_BSTATUS_DEPTH_SHIFT) |
			(XHDCP1X_PORT_BSTATUS_BIT_DEV_CNT_NO_ERR) |
			(InstancePtr->RepeaterValues.DeviceCount &
					XHDCP1X_PORT_BSTATUS_DEV_CNT_MASK);
#else
		RepeaterInfo = (XHDCP1X_PORT_BINFO_BIT_DEPTH_NO_ERR) |
			(InstancePtr->RepeaterValues.Depth <<
					XHDCP1X_PORT_BINFO_DEPTH_SHIFT) |
			(XHDCP1X_PORT_BINFO_BIT_DEV_CNT_NO_ERR) |
			(InstancePtr->RepeaterValues.DeviceCount &
					XHDCP1X_PORT_BINFO_DEV_CNT_MASK);
#endif

		XHdcp1x_RxCalculateSHA1Value(InstancePtr,RepeaterInfo);

#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)

		/* Update the value of V'H0 */
		sha1value = 0;
		sha1value = InstancePtr->RepeaterValues.V[0];
		XHDCP1X_PORT_UINT_TO_BUF(Buf, sha1value ,
					(XHDCP1X_PORT_SIZE_VH0 * 8));
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH0,
				Buf, XHDCP1X_PORT_SIZE_VH0);

		/* Update the value of V'H1 */
		sha1value = InstancePtr->RepeaterValues.V[1];
		XHDCP1X_PORT_UINT_TO_BUF(Buf, sha1value ,
					(XHDCP1X_PORT_SIZE_VH1 * 8));
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH1,
				Buf, XHDCP1X_PORT_SIZE_VH1);

		/* Update the value of V'H2 */
		sha1value = InstancePtr->RepeaterValues.V[2];
		XHDCP1X_PORT_UINT_TO_BUF(Buf, sha1value ,
					(XHDCP1X_PORT_SIZE_VH2 * 8));
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH2,
				Buf , XHDCP1X_PORT_SIZE_VH2);

		/* Update the value of V'H3 */
		sha1value = InstancePtr->RepeaterValues.V[3];
		XHDCP1X_PORT_UINT_TO_BUF(Buf, sha1value ,
					(XHDCP1X_PORT_SIZE_VH3 * 8));
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH3,
				Buf , XHDCP1X_PORT_SIZE_VH3);

		/* Update the value of V'H4 */
		sha1value = InstancePtr->RepeaterValues.V[4];
		XHDCP1X_PORT_UINT_TO_BUF(Buf, sha1value ,
					(XHDCP1X_PORT_SIZE_VH4 * 8));
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH4,
				Buf, XHDCP1X_PORT_SIZE_VH4);

		/* Update the Ready bit in the BCaps Register */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
				&BCaps, XHDCP1X_PORT_SIZE_BCAPS);
		BCaps |= XHDCP1X_PORT_BIT_BCAPS_READY;
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS ,
				&BCaps, XHDCP1X_PORT_SIZE_BCAPS);

#else

		/* Update the value of V'H0 */
		sha1value = 0;
		sha1value = InstancePtr->RepeaterValues.V[0];
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH0,
				&sha1value, XHDCP1X_PORT_SIZE_VH0);

		/* Update the value of V'H1 */
		sha1value = InstancePtr->RepeaterValues.V[1];
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH1,
				&sha1value, XHDCP1X_PORT_SIZE_VH1);

		/* Update the value of V'H2 */
		sha1value = InstancePtr->RepeaterValues.V[2];
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH2,
				&sha1value , XHDCP1X_PORT_SIZE_VH2);

		/* Update the value of V'H3 */
		sha1value = InstancePtr->RepeaterValues.V[3];
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH3,
				&sha1value , XHDCP1X_PORT_SIZE_VH3);

		/* Update the value of V'H4 */
		sha1value = InstancePtr->RepeaterValues.V[4];
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_VH4,
				&sha1value, XHDCP1X_PORT_SIZE_VH4);

		/* Reset the KSV FIFO read pointer to ox6802C */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_HDCP_RESET_KSV,
			&KSVPtrReset, XHDCP1X_PORT_SIZE_HDCP_RESET_KSV);
		KSVPtrReset |= XHDCP1X_PORT_HDCP_RESET_KSV_RST;
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_HDCP_RESET_KSV,
			&KSVPtrReset, XHDCP1X_PORT_SIZE_HDCP_RESET_KSV);

		KSVPtrReset &= ~XHDCP1X_PORT_HDCP_RESET_KSV_RST;
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_HDCP_RESET_KSV,
			&KSVPtrReset, XHDCP1X_PORT_SIZE_HDCP_RESET_KSV);

		/* Update the Ready bit in the BStatus Register */
		XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
				&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);
		BStatus |= XHDCP1X_PORT_BIT_BSTATUS_READY;
		XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS ,
				&BStatus, XHDCP1X_PORT_SIZE_BSTATUS);
#endif

		*NextStatePtr = XHDCP1X_STATE_AUTHENTICATED;
	}
}

/*****************************************************************************/
/**
* This function updates the Ro'/Ri' register of the state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		This function has to save the value of Ri (in RememberRi) as
*		the macro that converts from a uint16 to a HDCP buffer
*		destroys the original value.
*
******************************************************************************/
static void XHdcp1x_RxUpdateRi(XHdcp1x *InstancePtr)
{
	char LogBuf[20];
	u8 Buf[4];
	u16 Ri = 0;
	u16 RememberRi = 0;

	/* Read Ri */
	Ri = XHdcp1x_CipherGetRi(InstancePtr);

	/* Update RememberRi */
	RememberRi = Ri;

	/* Initialize theBuf */
	memset(Buf, 0, 4);
	XHDCP1X_PORT_UINT_TO_BUF(Buf, Ri, XHDCP1X_PORT_SIZE_RO * 8);

	/* Update the value of Ro' */
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_RO,
			Buf, sizeof(Ri));

#if defined(XHDCP1X_PORT_BIT_BSTATUS_RO_AVAILABLE)
	/* Update the Bstatus to indicate Ro' available */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS, Buf,
			XHDCP1X_PORT_SIZE_BSTATUS);
	Buf[0] |= XHDCP1X_PORT_BIT_BSTATUS_RO_AVAILABLE;
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS, Buf,
			XHDCP1X_PORT_SIZE_BSTATUS);
#endif

	/* Update statistics */
	InstancePtr->Rx.Stats.RiUpdates++;

	/* Determine theLogBuf */
	snprintf(LogBuf, 20, "update Ri (%04X)", RememberRi);

	/* Log */
	XHdcp1x_RxDebugLog(InstancePtr, LogBuf);
}

/*****************************************************************************/
/**
* This functions handles check the integrity of the link.
*
* @param	InstancePtr is the receiver instance.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxCheckLinkIntegrity(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	if (XHdcp1x_CipherIsLinkUp(InstancePtr)) {
		*NextStatePtr = XHDCP1X_STATE_AUTHENTICATED;
	}
	else {
		*NextStatePtr = XHDCP1X_STATE_LINKINTEGRITYFAILED;
	}
}

/*****************************************************************************/
/**
* This functions handles check if the encryption status (enable/disable) of
* the HDCP cipher has changed.
*
* @param	InstancePtr is the receiver instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxCheckEncryptionChange(XHdcp1x *InstancePtr)
{
	InstancePtr->Rx.XORState.PreviousState =
			InstancePtr->Rx.XORState.CurrentState;

	InstancePtr->Rx.XORState.CurrentState =
			XHdcp1x_CipherXorInProgress(InstancePtr);

	/* Check if encrypted */
	if (InstancePtr->Rx.XORState.CurrentState !=
			InstancePtr->Rx.XORState.PreviousState) {
		/* Call encryption update callback */
		if (InstancePtr->Rx.IsEncryptionUpdateCallbackSet) {
			InstancePtr->Rx.EncryptionUpdateCallback(
			InstancePtr->Rx.EncryptionUpdateCallbackRef);
		}
	}

	/* Start a 2 second timer again */
	XHdcp1x_RxStartTimer(InstancePtr, (2 * XVPHY_TMO_1SECOND));
}

/*****************************************************************************/
/**
* This functions reports the failure of link integrity.
*
* @param	InstancePtr is the receiver instance.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxReportLinkIntegrityFailure(XHdcp1x *InstancePtr,
		XHdcp1x_StateType *NextStatePtr)
{
	/* NextStatePtr not being used */
	UNUSED(NextStatePtr);

#if defined(XHDCP1X_PORT_BIT_BSTATUS_LINK_FAILURE)
	u8 Buf[XHDCP1X_PORT_SIZE_BSTATUS];

	/* Update the Bstatus register */
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS, Buf,
			XHDCP1X_PORT_SIZE_BSTATUS);
	Buf[0] |= XHDCP1X_PORT_BIT_BSTATUS_LINK_FAILURE;
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS, Buf,
			XHDCP1X_PORT_SIZE_BSTATUS);
#endif

	/* Log */
	XHdcp1x_RxDebugLog(InstancePtr, "link integrity failed");
}

/*****************************************************************************/
/**
* This function runs the "disabled" state of the receiver state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRunDisabledState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For enable */
		case XHDCP1X_EVENT_ENABLE:
			*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
			if ((InstancePtr->Rx.Flags & XVPHY_FLAG_PHY_UP) == 0) {
				*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			}
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			InstancePtr->Rx.Flags &= ~XVPHY_FLAG_PHY_UP;
			break;

		/* For physical layer up */
		case XHDCP1X_EVENT_PHYUP:
			InstancePtr->Rx.Flags |= XVPHY_FLAG_PHY_UP;
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "unauthenticated" state of the receiver state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRunUnauthenticatedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* InstancePtr not being used */
	UNUSED(InstancePtr);

	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_COMPUTATIONS;
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
* This function runs the "computations" state of the receiver state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRunComputationsState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			XHdcp1x_RxStartComputations(InstancePtr,
					NextStatePtr);
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
			XHdcp1x_RxPollForComputations(InstancePtr,
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
* This function runs the "wait for downstream" state of the receiver state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRunWaitForDownstreamState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* InstancePtr not being used */
	UNUSED(InstancePtr);

	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_COMPUTATIONS;
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
			*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
			break;

		case XHDCP1X_EVENT_DOWNSTREAMREADY:
			*NextStatePtr = XHDCP1X_STATE_ASSEMBLEKSVLIST;
			break;

		case XHDCP1X_EVENT_NULL:
			/*Do nothing */
			break;

		case XHDCP1X_EVENT_CHECK:
			/*Do nothing */
			break;

		case XHDCP1X_EVENT_ENABLE:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_PHYUP:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_POLL:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_UPDATERi:
			/* Do nothing */
			break;

	}
}

/*****************************************************************************/
/**
* This function runs the "assemble KSV list" state of the receiver state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRunAssembleKsvListState(XHdcp1x *InstancePtr,
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

		case XHDCP1X_EVENT_NULL:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_AUTHENTICATE:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_CHECK:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_ENABLE:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_PHYUP:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_POLL:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_UPDATERi:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_TIMEOUT:
			/* Do nothing */
			break;

		case XHDCP1X_EVENT_DOWNSTREAMREADY:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "authenticated" state of the receiver state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRunAuthenticatedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_COMPUTATIONS;
			break;

		/* For check */
		case XHDCP1X_EVENT_CHECK:
			XHdcp1x_RxCheckLinkIntegrity(InstancePtr,
					NextStatePtr);
			break;

		/* For disable */
		case XHDCP1X_EVENT_DISABLE:
			*NextStatePtr = XHDCP1X_STATE_DISABLED;
			break;

		/* For physical layer down */
		case XHDCP1X_EVENT_PHYDOWN:
			*NextStatePtr = XHDCP1X_STATE_PHYDOWN;
			break;

		/* For update Ri */
		case XHDCP1X_EVENT_UPDATERi:
			XHdcp1x_RxUpdateRi(InstancePtr);
			break;

		/* In every 2 second priodically checks encryption status */
		case XHDCP1X_EVENT_TIMEOUT:
			XHdcp1x_RxCheckEncryptionChange(InstancePtr);
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function runs the "link integrity failed" state of the receiver state
* machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRunLinkIntegrityFailedState(XHdcp1x *InstancePtr,
		XHdcp1x_EventType Event, XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of event called */
	switch (Event) {
		/* For authenticate */
		case XHDCP1X_EVENT_AUTHENTICATE:
			*NextStatePtr = XHDCP1X_STATE_COMPUTATIONS;
			break;

		/* For check */
		case XHDCP1X_EVENT_CHECK:
			XHdcp1x_RxCheckLinkIntegrity(InstancePtr,
					NextStatePtr);
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
* This function runs the "physical layer down" state of the receiver state
* machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxRunPhysicalLayerDownState(XHdcp1x *InstancePtr,
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

		/* For physical layer up */
		case XHDCP1X_EVENT_PHYUP:
			*NextStatePtr = XHDCP1X_STATE_UNAUTHENTICATED;
			break;

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function enters a HDCP receiver state.
*
* @param	InstancePtr is the receiver instance.
* @param	State is the state to enter.
* @param	NextStatePtr is the next state.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxEnterState(XHdcp1x *InstancePtr, XHdcp1x_StateType State,
		XHdcp1x_StateType *NextStatePtr)
{
	/* Case-wise process the kind of state called */
	switch (State) {
		/* For the disabled state */
		case XHDCP1X_STATE_DISABLED:
			XHdcp1x_RxDisableState(InstancePtr);
			break;

		/* For the unauthenticated state */
		case XHDCP1X_STATE_UNAUTHENTICATED:
			XHdcp1x_RxSetCheckLinkState(InstancePtr, FALSE);
			InstancePtr->Rx.Flags |= XVPHY_FLAG_PHY_UP;
			if (InstancePtr->Rx.IsUnauthenticatedCallbackSet) {
				InstancePtr->Rx.UnauthenticatedCallback(
				InstancePtr->Rx.UnauthenticatedCallbackRef);
			}
			break;

		/* For the computations state */
		case XHDCP1X_STATE_COMPUTATIONS:
			XHdcp1x_RxStartComputations(InstancePtr,
						NextStatePtr);
			break;

		/* For the wait-for-downstream state */
		case XHDCP1X_STATE_WAITFORDOWNSTREAM:
			XHdcp1x_RxSetCheckLinkState(InstancePtr, TRUE);
			XHdcp1x_RxStartTimer(InstancePtr,
			((5 * XVPHY_TMO_1SECOND) + (5 * XVPHY_TMO_100MS)) );
			break;

		/* For assemble KSV list */
		case XHDCP1X_STATE_ASSEMBLEKSVLIST:
			XHdcp1x_RxAssembleKSVList(InstancePtr,
						NextStatePtr);
			break;

		/* For the authenticated state */
		case XHDCP1X_STATE_AUTHENTICATED:
			XHdcp1x_RxDebugLog(InstancePtr, "authenticated");
			XHdcp1x_RxSetCheckLinkState(InstancePtr, TRUE);
			if(InstancePtr->Rx.IsAuthenticatedCallbackSet) {
				InstancePtr->Rx.AuthenticatedCallback(
				InstancePtr->Rx.AuthenticatedCallbackRef);
			}
			XHdcp1x_RxStartTimer(InstancePtr,
					(2 * XVPHY_TMO_1SECOND));
			break;

		/* For the link integrity failed state */
		case XHDCP1X_STATE_LINKINTEGRITYFAILED:
			InstancePtr->Rx.Stats.LinkFailures++;
			XHdcp1x_RxReportLinkIntegrityFailure(InstancePtr,
					NextStatePtr);
			break;

		/* For physical layer down */
		case XHDCP1X_STATE_PHYDOWN:
			InstancePtr->Rx.Flags &= ~XVPHY_FLAG_PHY_UP;
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
* This function exits a HDCP receiver state.
*
* @param	InstancePtr is the receiver instance.
* @param	State is the state to exit.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxExitState(XHdcp1x *InstancePtr, XHdcp1x_StateType State)
{
	/* Case-wise process the kind of state called */
	switch (State) {
		/* For the disabled state */
		case XHDCP1X_STATE_DISABLED:
			XHdcp1x_RxEnableState(InstancePtr);
			break;

		/* For the authenticated state */
		case XHDCP1X_STATE_AUTHENTICATED:
			XHdcp1x_RxStopTimer(InstancePtr);
			XHdcp1x_RxSetCheckLinkState(InstancePtr, FALSE);
			break;

		/* For physical layer down */
		case XHDCP1X_STATE_PHYDOWN:
			XHdcp1x_CipherEnable(InstancePtr);
			break;

		/* For wait-for-downstream ready */
		case XHDCP1X_STATE_WAITFORDOWNSTREAM:
			XHdcp1x_RxStopTimer(InstancePtr);

		/* Otherwise */
		default:
			/* Do nothing */
			break;
	}
}

/*****************************************************************************/
/**
* This function drives a HDCP receiver state machine.
*
* @param	InstancePtr is the receiver instance.
* @param	Event is the event to process.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XHdcp1x_RxDoTheState(XHdcp1x *InstancePtr, XHdcp1x_EventType Event)
{
	XHdcp1x_StateType NextState = InstancePtr->Rx.CurrentState;

	/* Case-wise process the kind of state called */
	switch (InstancePtr->Rx.CurrentState) {
		/* For the disabled state */
		case XHDCP1X_STATE_DISABLED:
			XHdcp1x_RxRunDisabledState(InstancePtr,
					Event, &NextState);
			break;

		/* For the unauthenticated state */
		case XHDCP1X_STATE_UNAUTHENTICATED:
			XHdcp1x_RxRunUnauthenticatedState(InstancePtr,
					Event, &NextState);
			break;

		/* For the computations state */
		case XHDCP1X_STATE_COMPUTATIONS:
			XHdcp1x_RxRunComputationsState(InstancePtr,
					Event, &NextState);
			break;

		/* For the Wait For Downstream state */
		case XHDCP1X_STATE_WAITFORDOWNSTREAM:
			XHdcp1x_RxRunWaitForDownstreamState(InstancePtr,
					Event, &NextState);
			break;

		/* For the assemble ksv list state */
		case XHDCP1X_STATE_ASSEMBLEKSVLIST:
			XHdcp1x_RxRunAssembleKsvListState(InstancePtr,
					Event, &NextState);
			break;

		/* For the authenticated state */
		case XHDCP1X_STATE_AUTHENTICATED:
			XHdcp1x_RxRunAuthenticatedState(InstancePtr,
					Event, &NextState);
			break;

		/* For the link integrity failed state */
		case XHDCP1X_STATE_LINKINTEGRITYFAILED:
			XHdcp1x_RxRunLinkIntegrityFailedState(InstancePtr,
					Event, &NextState);
			break;

		/* For the physical layer down state */
		case XHDCP1X_STATE_PHYDOWN:
			XHdcp1x_RxRunPhysicalLayerDownState(InstancePtr,
					Event, &NextState);
			break;

		/* Otherwise */
		default:
			break;
	}

	/* Check for state change */
	while (InstancePtr->Rx.CurrentState != NextState) {
		/* Perform the state transition */
		XHdcp1x_RxExitState(InstancePtr,
				InstancePtr->Rx.CurrentState);
		InstancePtr->Rx.PreviousState = InstancePtr->Rx.CurrentState;
		InstancePtr->Rx.CurrentState = NextState;
		XHdcp1x_RxEnterState(InstancePtr,
				InstancePtr->Rx.CurrentState,
				&NextState);
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
static void XHdcp1x_RxProcessPending(XHdcp1x *InstancePtr)
{
	/* Check for any pending events */
	if (InstancePtr->Rx.PendingEvents != 0) {
		u16 Pending = InstancePtr->Rx.PendingEvents;
		XHdcp1x_EventType Event = XHDCP1X_EVENT_NULL;

		/* Update InstancePtr */
		InstancePtr->Rx.PendingEvents = 0;

		/* Iterate through thePending */
		do {
			/* Check for a pending event */
			if ((Pending & 1u) != 0) {
				XHdcp1x_RxDoTheState(InstancePtr, Event);
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
static const char *XHdcp1x_RxStateToString(XHdcp1x_StateType State)
{
	const char *String = NULL;

	/* Case-wise process the input state */
	switch (State) {
		case XHDCP1X_STATE_DISABLED:
			String = "disabled";
			break;

		case XHDCP1X_STATE_UNAUTHENTICATED:
			String = "unauthenticated";
			break;

		case XHDCP1X_STATE_COMPUTATIONS:
			String = "computations";
			break;

		case XHDCP1X_STATE_WAITFORDOWNSTREAM:
			String = "wait-for-downstream";
			break;

		case XHDCP1X_STATE_ASSEMBLEKSVLIST:
			String = "assemble-ksv-list";
			break;

		case XHDCP1X_STATE_AUTHENTICATED:
			String = "authenticated";
			break;

		case XHDCP1X_STATE_LINKINTEGRITYFAILED:
			String = "link-integrity-failed";
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
* This function converts from a event to a display string.
*
* @param	Event is the event to convert.
*
* @return	The corresponding display string.
*
* @note		None.
*
******************************************************************************/
static const char *XHdcp1x_RxEventToString(XHdcp1x_EventType Event)
{
	const char *String = NULL;

	/* Case-wise process the input event */
	switch (Event) {
		case XHDCP1X_EVENT_NULL:
			String = "null";
			break;

		case XHDCP1X_EVENT_AUTHENTICATE:
			String = "authenticate";
			break;

		case XHDCP1X_EVENT_CHECK:
			String = "check";
			break;

		case XHDCP1X_EVENT_DISABLE:
			String = "disable";
			break;

		case XHDCP1X_EVENT_ENABLE:
			String = "enable";
			break;

		case XHDCP1X_EVENT_PHYDOWN:
			String = "phy-down";
			break;

		case XHDCP1X_EVENT_PHYUP:
			String = "phy-up";
			break;

		case XHDCP1X_EVENT_POLL:
			String = "poll";
			break;

		case XHDCP1X_EVENT_UPDATERi:
			String = "update-ri";
			break;

		case XHDCP1X_EVENT_DOWNSTREAMREADY:
			String = "downstream-ready";
			break;

		default:
			String = "unknown?";
			break;
	}

	return (String);
}
#endif

/** @} */

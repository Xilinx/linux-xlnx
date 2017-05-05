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
* @file xv_hdmitxss_hdcp.c
*
* This is main code of Xilinx HDMI Transmitter Subsystem device driver.
* Please see xv_hdmitxss.h for more details of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00   MMO 19/12/16 Move HDCP Code from xv_hdmitxss.c to xv_hdmitxss_hdcp.c
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xv_hdmitxss.h"
/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/**************************** Local Global ***********************************/

/************************** Function Prototypes ******************************/
// HDCP specific
#ifdef USE_HDCP_TX
static XV_HdmiTxSs_HdcpEvent XV_HdmiTxSs_HdcpGetEvent(XV_HdmiTxSs *InstancePtr);
static int XV_HdmiTxSs_HdcpProcessEvents(XV_HdmiTxSs *InstancePtr);
static int XV_HdmiTxSs_HdcpReset(XV_HdmiTxSs *InstancePtr);
static u32 XV_HdmiTxSs_HdcpGetTopologyDepth(XV_HdmiTxSs *InstancePtr);
static u32 XV_HdmiTxSs_HdcpGetTopologyDeviceCnt(XV_HdmiTxSs *InstancePtr);
static u8 XV_HdmiTxSs_HdcpGetTopologyMaxDevsExceeded(XV_HdmiTxSs *InstancePtr);
static u8
        XV_HdmiTxSs_HdcpGetTopologyMaxCascadeExceeded(XV_HdmiTxSs *InstancePtr);
static u8
  XV_HdmiTxSs_HdcpGetTopologyHdcp20RepeaterDownstream(XV_HdmiTxSs *InstancePtr);
static u8
     XV_HdmiTxSs_HdcpGetTopologyHdcp1DeviceDownstream(XV_HdmiTxSs *InstancePtr);
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
static u32 XV_HdmiTxSs_HdcpTimerConvUsToTicks(u32 TimeoutInUs,
    u32 ClockFrequency);
#endif

/***************** Macros (Inline Functions) Definitions *********************/
/************************** Function Definition ******************************/

#ifdef XPAR_XHDCP_NUM_INSTANCES
/*****************************************************************************/
/**
 * This function calls the interrupt handler for HDCP
 *
 * @param  InstancePtr is a pointer to the HDMI TX Subsystem
 *
 *****************************************************************************/
void XV_HdmiTxSS_HdcpIntrHandler(XV_HdmiTxSs *InstancePtr)
{
    XHdcp1x_CipherIntrHandler(InstancePtr->Hdcp14Ptr);
}
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
/*****************************************************************************/
/**
 * This function calls the interrupt handler for HDCP Timer
 *
 * @param  InstancePtr is a pointer to the HDMI TX Subsystem
 *
 *****************************************************************************/
void XV_HdmiTxSS_HdcpTimerIntrHandler(XV_HdmiTxSs *InstancePtr)
{
    XTmrCtr_InterruptHandler(InstancePtr->HdcpTimerPtr);
}
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
/*****************************************************************************/
/**
 * This function calls the interrupt handler for HDCP 2.2 Timer
 *
 * @param  InstancePtr is a pointer to the HDMI TX Subsystem
 *
 *****************************************************************************/
void XV_HdmiTxSS_Hdcp22TimerIntrHandler(XV_HdmiTxSs *InstancePtr)
{
  XTmrCtr *XTmrCtrPtr;

  XTmrCtrPtr = XHdcp22Tx_GetTimer(InstancePtr->Hdcp22Ptr);

  XTmrCtr_InterruptHandler(XTmrCtrPtr);
}
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
/******************************************************************************/
/**
*
* This function converts from microseconds to timer ticks
*
* @param TimeoutInUs  the timeout to convert
* @param ClockFrequency  the clock frequency to use in the conversion
*
* @return
*   The number of "ticks"
*
* @note
*   None.
*
******************************************************************************/
static u32 XV_HdmiTxSs_HdcpTimerConvUsToTicks(u32 TimeoutInUs,
                                                    u32 ClockFrequency)
{
    u32 TimeoutFreq = 0;
    u32 NumTicks = 0;

    /* Check for greater than one second */
    if (TimeoutInUs > 1000000ul) {
        u32 NumSeconds = 0;

        /* Determine theNumSeconds */
        NumSeconds = (TimeoutInUs/1000000ul);

        /* Update theNumTicks */
        NumTicks = (NumSeconds*ClockFrequency);

        /* Adjust theTimeoutInUs */
        TimeoutInUs -= (NumSeconds*1000000ul);
    }

    /* Convert TimeoutFreq to a frequency */
    TimeoutFreq  = 1000;
    TimeoutFreq *= 1000;
    TimeoutFreq /= TimeoutInUs;

    /* Update NumTicks */
    NumTicks += ((ClockFrequency / TimeoutFreq) + 1);

    return (NumTicks);
}
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
/*****************************************************************************/
/**
*
* This function serves as the timer callback function
*
* @param CallBackRef  the callback reference value
* @param TimerChannel  the channel within the timer that expired
*
* @return
*   void
*
* @note
*   None
*
******************************************************************************/
void XV_HdmiTxSs_HdcpTimerCallback(void* CallBackRef, u8 TimerChannel)
{
    XHdcp1x* HdcpPtr = (XHdcp1x*) CallBackRef;

    TimerChannel = TimerChannel;
    XHdcp1x_HandleTimeout(HdcpPtr);
    return;
}
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
/******************************************************************************/
/**
*
* This function starts a timer on behalf of an hdcp interface
*
* @param InstancePtr  the hdcp interface
* @param TimeoutInMs  the timer duration in milliseconds
*
* @return
*   XST_SUCCESS if successful
*
* @note
*   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpTimerStart(void *InstancePtr, u16 TimeoutInMs)
{
    XHdcp1x *HdcpPtr = (XHdcp1x *)InstancePtr;
    XTmrCtr *TimerPtr = (XTmrCtr *)HdcpPtr->Hdcp1xRef;

    u8 TimerChannel = 0;
    u32 TimerOptions = 0;
    u32 NumTicks = 0;

    /* Verify argument. */
    Xil_AssertNonvoid(TimerPtr != NULL);


    /* Determine NumTicks */
    NumTicks = XV_HdmiTxSs_HdcpTimerConvUsToTicks((TimeoutInMs*1000ul),
                  TimerPtr->Config.SysClockFreqHz);


    /* Stop it */
    XTmrCtr_Stop(TimerPtr, TimerChannel);

    /* Configure the callback */
    XTmrCtr_SetHandler(TimerPtr, &XV_HdmiTxSs_HdcpTimerCallback,
                                                (void*) HdcpPtr);

    /* Configure the timer options */
    TimerOptions  = XTmrCtr_GetOptions(TimerPtr, TimerChannel);
    TimerOptions |=  XTC_DOWN_COUNT_OPTION;
    TimerOptions |=  XTC_INT_MODE_OPTION;
    TimerOptions &= ~XTC_AUTO_RELOAD_OPTION;
    XTmrCtr_SetOptions(TimerPtr, TimerChannel, TimerOptions);

    /* Set the timeout and start */
    XTmrCtr_SetResetValue(TimerPtr, TimerChannel, NumTicks);
    XTmrCtr_Start(TimerPtr, TimerChannel);

    return (XST_SUCCESS);
}
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
/******************************************************************************/
/**
*
* This function stops a timer on behalf of an hdcp interface
*
* @param InstancePtr  the hdcp interface
*
* @return
*   XST_SUCCESS if successful
*
* @note
*   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpTimerStop(void *InstancePtr)
{
    XHdcp1x *HdcpPtr = (XHdcp1x *)InstancePtr;
    XTmrCtr *TimerPtr = (XTmrCtr *)HdcpPtr->Hdcp1xRef;

    u8 TimerChannel = 0;

    /* Verify argument. */
    Xil_AssertNonvoid(TimerPtr != NULL);

    /* Stop it */
    XTmrCtr_Stop(TimerPtr, TimerChannel);

    return (XST_SUCCESS);
}
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
/******************************************************************************/
/**
*
* This function busy waits for an interval on behalf of an hdcp interface
*
* @param InstancePtr  the hdcp interface
* @param DelayInMs  the delay duration in milliseconds
*
* @return
*   XST_SUCCESS if successful
*
* @note
*   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpTimerBusyDelay(void *InstancePtr, u16 DelayInMs)
{
    XHdcp1x *HdcpPtr = (XHdcp1x *)InstancePtr;
    XTmrCtr *TimerPtr = (XTmrCtr *)HdcpPtr->Hdcp1xRef;

    u8 TimerChannel = 0;
    u32 TimerOptions = 0;
    u32 NumTicks = 0;

    /* Verify argument. */
    Xil_AssertNonvoid(TimerPtr != NULL);


    /* Determine NumTicks */
    NumTicks = XV_HdmiTxSs_HdcpTimerConvUsToTicks((DelayInMs*1000ul),
                  TimerPtr->Config.SysClockFreqHz);

    /* Stop it */
    XTmrCtr_Stop(TimerPtr, TimerChannel);

    /* Configure the timer options */
    TimerOptions  = XTmrCtr_GetOptions(TimerPtr, TimerChannel);
    TimerOptions |=  XTC_DOWN_COUNT_OPTION;
    TimerOptions &= ~XTC_INT_MODE_OPTION;
    TimerOptions &= ~XTC_AUTO_RELOAD_OPTION;
    XTmrCtr_SetOptions(TimerPtr, TimerChannel, TimerOptions);

    /* Set the timeout and start */
    XTmrCtr_SetResetValue(TimerPtr, TimerChannel, NumTicks);
    XTmrCtr_Start(TimerPtr, TimerChannel);

    /* Wait until done */
    while (!XTmrCtr_IsExpired(TimerPtr, TimerChannel));

    return (XST_SUCCESS);
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function pushes an event into the HDCP event queue.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
* @param Event is the event to be pushed in the queue.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpPushEvent(XV_HdmiTxSs *InstancePtr,
                              XV_HdmiTxSs_HdcpEvent Event)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);
  Xil_AssertNonvoid(Event < XV_HDMITXSS_HDCP_INVALID_EVT);

  /* Write event into the queue */
  InstancePtr->HdcpEventQueue.Queue[InstancePtr->HdcpEventQueue.Head] = Event;

  /* Update head pointer */
  if (InstancePtr->HdcpEventQueue.Head ==
                                        (XV_HDMITXSS_HDCP_MAX_QUEUE_SIZE - 1)) {
    InstancePtr->HdcpEventQueue.Head = 0;
  }
  else {
    InstancePtr->HdcpEventQueue.Head++;
  }

  /* Check tail pointer. When the two pointer are equal, then the buffer
   * is full. In this case then increment the tail pointer as well to
   * remove the oldest entry from the buffer.
   */
  if (InstancePtr->HdcpEventQueue.Tail == InstancePtr->HdcpEventQueue.Head) {
    if (InstancePtr->HdcpEventQueue.Tail ==
                                        (XV_HDMITXSS_HDCP_MAX_QUEUE_SIZE - 1)) {
      InstancePtr->HdcpEventQueue.Tail = 0;
    }
    else {
      InstancePtr->HdcpEventQueue.Tail++;
    }
  }

  return XST_SUCCESS;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets an event from the HDCP event queue.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return When the queue is filled, the next event is returned.
*         When the queue is empty, XV_HDMITXSS_HDCP_NO_EVT is returned.
*
* @note   None.
*
******************************************************************************/
static XV_HdmiTxSs_HdcpEvent XV_HdmiTxSs_HdcpGetEvent(XV_HdmiTxSs *InstancePtr)
{
  XV_HdmiTxSs_HdcpEvent Event;

  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  /* Check if there are any events in the queue */
  if (InstancePtr->HdcpEventQueue.Tail == InstancePtr->HdcpEventQueue.Head) {
    return XV_HDMITXSS_HDCP_NO_EVT;
  }

  Event = InstancePtr->HdcpEventQueue.Queue[InstancePtr->HdcpEventQueue.Tail];

  /* Update tail pointer */
  if (InstancePtr->HdcpEventQueue.Tail ==
                                        (XV_HDMITXSS_HDCP_MAX_QUEUE_SIZE - 1)) {
    InstancePtr->HdcpEventQueue.Tail = 0;
  }
  else {
    InstancePtr->HdcpEventQueue.Tail++;
  }

  return Event;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function clears all pending events from the HDCP event queue.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpClearEvents(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  InstancePtr->HdcpEventQueue.Head = 0;
  InstancePtr->HdcpEventQueue.Tail = 0;

  return XST_SUCCESS;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function processes pending events from the HDCP event queue.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
static int XV_HdmiTxSs_HdcpProcessEvents(XV_HdmiTxSs *InstancePtr)
{
  XV_HdmiTxSs_HdcpEvent Event;
  int Status = XST_SUCCESS;

  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  Event = XV_HdmiTxSs_HdcpGetEvent(InstancePtr);
  switch (Event) {

    // Stream up
    // Attempt authentication with downstream device
    case XV_HDMITXSS_HDCP_STREAMUP_EVT :
#ifdef XPAR_XHDCP_NUM_INSTANCES
      if (InstancePtr->Hdcp14Ptr) {
        // Set physical state
        XHdcp1x_SetPhysicalState(InstancePtr->Hdcp14Ptr, TRUE);
        // This is needed to ensure that the previous command is executed.
        XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
      }
#endif
      break;

    // Stream down
    case XV_HDMITXSS_HDCP_STREAMDOWN_EVT :
#ifdef XPAR_XHDCP_NUM_INSTANCES
      if (InstancePtr->Hdcp14Ptr) {
        // Set physical state
        XHdcp1x_SetPhysicalState(InstancePtr->Hdcp14Ptr, FALSE);
        // This is needed to ensure that the previous command is executed.
        XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
      }
#endif
      XV_HdmiTxSs_HdcpReset(InstancePtr);
      break;

    // Connect
    case XV_HDMITXSS_HDCP_CONNECT_EVT :
      break;

    // Disconnect
    // Reset both HDCP protocols
    case XV_HDMITXSS_HDCP_DISCONNECT_EVT :
      XV_HdmiTxSs_HdcpReset(InstancePtr);
      break;

    // Authenticate
    case XV_HDMITXSS_HDCP_AUTHENTICATE_EVT :
      XV_HdmiTxSs_HdcpAuthRequest(InstancePtr);
      break;

    default :
      break;
  }

  return Status;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function schedules the available HDCP cores. Only the active
* HDCP protocol poll function is executed. HDCP 1.4 and 2.2 poll
* functions should not execute in parallel.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpPoll(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  /* Only poll when the HDCP is ready */
  if (InstancePtr->HdcpIsReady) {

    /* Process any pending events from the TX event queue */
    XV_HdmiTxSs_HdcpProcessEvents(InstancePtr);

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    if (InstancePtr->Hdcp22Ptr) {
      if (XHdcp22Tx_IsEnabled(InstancePtr->Hdcp22Ptr)) {
       XHdcp22Tx_Poll(InstancePtr->Hdcp22Ptr);
      }
    }
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    if (InstancePtr->Hdcp14Ptr) {
      if (XHdcp1x_IsEnabled(InstancePtr->Hdcp14Ptr)) {
       XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
      }
    }
#endif
  }

  return XST_SUCCESS;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function sets the active HDCP protocol and enables it.
* The protocol can be set to either HDCP 1.4, 2.2, or None.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
* @param Protocol is the requested content protection scheme of type
*        XV_HdmiTxSs_HdcpProtocol.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpSetProtocol(XV_HdmiTxSs *InstancePtr,
                                XV_HdmiTxSs_HdcpProtocol Protocol)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);
  Xil_AssertNonvoid((Protocol == XV_HDMITXSS_HDCP_NONE)   ||
                    (Protocol == XV_HDMITXSS_HDCP_14) ||
                    (Protocol == XV_HDMITXSS_HDCP_22));

  int Status;

  // Set protocol
  InstancePtr->HdcpProtocol = Protocol;

  // Reset both protocols
  Status = XV_HdmiTxSs_HdcpReset(InstancePtr);
  if (Status != XST_SUCCESS) {
    InstancePtr->HdcpProtocol = XV_HDMITXSS_HDCP_NONE;
    return XST_FAILURE;
  }

  // Enable the requested protocol
  Status = XV_HdmiTxSs_HdcpEnable(InstancePtr);
  if (Status != XST_SUCCESS) {
    InstancePtr->HdcpProtocol = XV_HDMITXSS_HDCP_NONE;
    return XST_FAILURE;
  }

  return XST_SUCCESS;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the active HDCP content protection scheme.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  @RequestedScheme is the requested content protection scheme.
*
* @note   None.
*
******************************************************************************/
XV_HdmiTxSs_HdcpProtocol XV_HdmiTxSs_HdcpGetProtocol(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  return InstancePtr->HdcpProtocol;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function enables the requested HDCP protocol. This function
* ensures that the HDCP protocols are mutually exclusive such that
* either HDCP 1.4 or HDCP 2.2 is enabled and active at any given time.
* When the protocol is set to None, both HDCP protocols are disabled.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpEnable(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  int Status1 = XST_SUCCESS;
  int Status2 = XST_SUCCESS;

  switch (InstancePtr->HdcpProtocol) {

    /* Disable HDCP 1.4 and HDCP 2.2 */
    case XV_HDMITXSS_HDCP_NONE :
#ifdef XPAR_XHDCP_NUM_INSTANCES
      if (InstancePtr->Hdcp14Ptr) {
        Status1 = XHdcp1x_Disable(InstancePtr->Hdcp14Ptr);
        // This is needed to ensure that the previous command is executed.
        XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
      }
#endif
#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
      if (InstancePtr->Hdcp22Ptr) {
        Status2 = XHdcp22Tx_Disable(InstancePtr->Hdcp22Ptr);
      }
#endif
      break;

    /* Enable HDCP 1.4 and disable HDCP 2.2 */
    case XV_HDMITXSS_HDCP_14 :
#ifdef XPAR_XHDCP_NUM_INSTANCES
      if (InstancePtr->Hdcp14Ptr) {
        Status1 = XHdcp1x_Enable(InstancePtr->Hdcp14Ptr);
        // This is needed to ensure that the previous command is executed.
        XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
      }
      else {
        Status1 = XST_FAILURE;
      }
#else
      Status1 = XST_FAILURE;
#endif
#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
      if (InstancePtr->Hdcp22Ptr) {
        Status2 = XHdcp22Tx_Disable(InstancePtr->Hdcp22Ptr);
      }
#endif
      break;

    /* Enable HDCP 2.2 and disable HDCP 1.4 */
    case XV_HDMITXSS_HDCP_22 :
#ifdef XPAR_XHDCP_NUM_INSTANCES
      if (InstancePtr->Hdcp14Ptr) {
        Status1 = XHdcp1x_Disable(InstancePtr->Hdcp14Ptr);
        // This is needed to ensure that the previous command is executed.
        XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
      }
#endif
#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
      if (InstancePtr->Hdcp22Ptr) {
        Status2 = XHdcp22Tx_Enable(InstancePtr->Hdcp22Ptr);
      }
      else {
        Status2 = XST_FAILURE;
      }
#else
      Status2 = XST_FAILURE;
#endif
      break;

    default :
      return XST_FAILURE;
  }

  return (Status1 == XST_SUCCESS &&
          Status2 == XST_SUCCESS) ?
          XST_SUCCESS : XST_FAILURE;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function disables both HDCP 1.4 and 2.2 protocols.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpDisable(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  int Status = XST_SUCCESS;

#ifdef XPAR_XHDCP_NUM_INSTANCES
  // HDCP 1.4
  if (InstancePtr->Hdcp14Ptr) {
    Status = XHdcp1x_Disable(InstancePtr->Hdcp14Ptr);
    // This is needed to ensure that the previous command is executed.
    XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
    if (Status != XST_SUCCESS)
      return XST_FAILURE;
  }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  // HDCP 2.2
  if (InstancePtr->Hdcp22Ptr) {
    Status = XHdcp22Tx_Disable(InstancePtr->Hdcp22Ptr);
    if (Status != XST_SUCCESS)
      return XST_FAILURE;
  }
#endif

  return Status;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function resets both HDCP 1.4 and 2.2 protocols. This function
* also disables the both HDCP 1.4 and 2.2 protocols.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
static int XV_HdmiTxSs_HdcpReset(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  int Status = XST_SUCCESS;

#ifdef XPAR_XHDCP_NUM_INSTANCES
  // HDCP 1.4
  // Resetting HDCP 1.4 causes the state machine to be enabled, therefore
  // disable must be called immediately after reset is called.
  if (InstancePtr->Hdcp14Ptr) {
    Status = XHdcp1x_Reset(InstancePtr->Hdcp14Ptr);
    // This is needed to ensure that the previous command is executed.
    XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
    if (Status != XST_SUCCESS)
      return XST_FAILURE;

    Status = XHdcp1x_Disable(InstancePtr->Hdcp14Ptr);
    // This is needed to ensure that the previous command is executed.
    XHdcp1x_Poll(InstancePtr->Hdcp14Ptr);
    if (Status != XST_SUCCESS)
      return XST_FAILURE;
  }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  // HDCP 2.2
  if (InstancePtr->Hdcp22Ptr) {
    Status = XHdcp22Tx_Reset(InstancePtr->Hdcp22Ptr);
    if (Status != XST_SUCCESS)
      return XST_FAILURE;

    Status = XHdcp22Tx_Disable(InstancePtr->Hdcp22Ptr);
    if (Status != XST_SUCCESS)
      return XST_FAILURE;
  }
#endif

  // Set defaults
  XV_HdmiTxSs_HdcpDisableBlank(InstancePtr);
  XV_HdmiTxSs_HdcpDisableEncryption(InstancePtr);

  return Status;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function sends an authentication request to the connected receiver.
* The HDCP protocol is determined automatically by checking the capabilities
* of the connected device. When the connected device supports both HDCP 1.4
* and HDCP 2.2, then HDCP 2.2 is given priority.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if authentication started successfully
*  - XST_FAILURE if authentication did not start successfully
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpAuthRequest(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  int Status = XST_FAILURE;

  /* Always disable encryption */
  if (XV_HdmiTxSs_HdcpDisableEncryption(InstancePtr) != XST_SUCCESS) {
    XV_HdmiTxSs_HdcpSetProtocol(InstancePtr, XV_HDMITXSS_HDCP_NONE);
    return XST_FAILURE;
  }

  /* Verify if sink is attached */
  if (!XV_HdmiTx_IsStreamConnected(InstancePtr->HdmiTxPtr)) {
    xdbg_printf(XDBG_DEBUG_GENERAL, "No sink is attached\r\n");
    XV_HdmiTxSs_HdcpSetProtocol(InstancePtr, XV_HDMITXSS_HDCP_NONE);
    return XST_FAILURE;
  }

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  /* Authenticate HDCP 2.2, takes priority */
  if (InstancePtr->Hdcp22Ptr) {
    if (XV_HdmiTxSs_IsSinkHdcp22Capable(InstancePtr)) {
      xdbg_printf(XDBG_DEBUG_GENERAL, "Starting HDCP 2.2 authentication\r\n");
#ifdef XV_HDMITXSS_LOG_ENABLE
      XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_HDCP22_AUTHREQ, 0);
#endif
	  Status = XV_HdmiTxSs_HdcpSetProtocol(InstancePtr, XV_HDMITXSS_HDCP_22);
      Status |= XHdcp22Tx_Authenticate(InstancePtr->Hdcp22Ptr);
    }
    else {
      Status = XST_FAILURE;
      xdbg_printf(XDBG_DEBUG_GENERAL, "Sink is not HDCP 2.2 capable\r\n");
    }
  }
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
  /* Authenticate HDCP 1.4 */
  if ((InstancePtr->Hdcp14Ptr) && (Status == XST_FAILURE)) {
    if (XV_HdmiTxSs_IsSinkHdcp14Capable(InstancePtr)) {
      xdbg_printf(XDBG_DEBUG_GENERAL, "Starting HDCP 1.4 authentication\r\n");
#ifdef XV_HDMITXSS_LOG_ENABLE
      XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_HDCP14_AUTHREQ, 0);
#endif
      Status = XV_HdmiTxSs_HdcpSetProtocol(InstancePtr, XV_HDMITXSS_HDCP_14);
      Status |= XHdcp1x_Authenticate(InstancePtr->Hdcp14Ptr);
    }
    else {
      Status = XST_FAILURE;
      xdbg_printf(XDBG_DEBUG_GENERAL, "Sink is not HDCP 1.4 capable\r\n");
    }
  }
#endif

  /* Set protocol to None */
  if (Status == XST_FAILURE) {
    XV_HdmiTxSs_HdcpSetProtocol(InstancePtr, XV_HDMITXSS_HDCP_NONE);
  }

  return (Status == XST_SUCCESS) ? XST_SUCCESS : XST_FAILURE;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function enables encryption for the active HDCP protocol.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpEnableEncryption(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  int Status = XST_SUCCESS;

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
      break;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14 :
      if (InstancePtr->Hdcp14Ptr) {
        Status = XHdcp1x_EnableEncryption(InstancePtr->Hdcp14Ptr, 0x1);
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_22 :
      if (InstancePtr->Hdcp22Ptr) {
        Status = XHdcp22Tx_EnableEncryption(InstancePtr->Hdcp22Ptr);
      }
      break;
#endif

    default :
      Status = XST_FAILURE;
  }

  return Status;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function disables encryption for both HDCP protocols.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpDisableEncryption(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  int Status = XST_SUCCESS;

#ifdef XPAR_XHDCP_NUM_INSTANCES
  if (InstancePtr->Hdcp14Ptr) {
    Status = XHdcp1x_DisableEncryption(InstancePtr->Hdcp14Ptr, 0x1);

    if (Status != XST_SUCCESS) {
      return XST_FAILURE;
    }
  }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  if (InstancePtr->Hdcp22Ptr) {
    Status = XHdcp22Tx_DisableEncryption(InstancePtr->Hdcp22Ptr);

    if (Status != XST_SUCCESS) {
      return XST_FAILURE;
    }
  }
#endif

  return Status;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function enables cipher blank for the active HDCP protocol.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpEnableBlank(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
#ifdef XPAR_XHDCP_NUM_INSTANCES
      if (InstancePtr->Hdcp14Ptr) {
        XHdcp1x_Enable(InstancePtr->Hdcp14Ptr);
        XHdcp1x_EnableBlank(InstancePtr->Hdcp14Ptr);
        return XST_SUCCESS;
      }
#endif
#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
      if (InstancePtr->Hdcp22Ptr) {
        XHdcp22Tx_Enable(InstancePtr->Hdcp22Ptr);
        XHdcp22Tx_EnableBlank(InstancePtr->Hdcp22Ptr);
        return XST_SUCCESS;
      }
#endif
      break;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14 :
      if (InstancePtr->Hdcp14Ptr) {
        XHdcp1x_EnableBlank(InstancePtr->Hdcp14Ptr);
        return XST_SUCCESS;
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_22 :
      if (InstancePtr->Hdcp22Ptr) {
        XHdcp22Tx_EnableBlank(InstancePtr->Hdcp22Ptr);
        return XST_SUCCESS;
      }
      break;
#endif

    default :
      /* Do nothing */
      break;
  }

  return XST_FAILURE;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function disables cipher blank for both HDCP protocol.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpDisableBlank(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

#ifdef XPAR_XHDCP_NUM_INSTANCES
  if (InstancePtr->Hdcp14Ptr) {
    XHdcp1x_DisableBlank(InstancePtr->Hdcp14Ptr);
  }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  if (InstancePtr->Hdcp22Ptr) {
    XHdcp22Tx_DisableBlank(InstancePtr->Hdcp22Ptr);
  }
#endif

  return XST_SUCCESS;
}
#endif

/*****************************************************************************/
/**
*
* This function determines if the connected HDMI sink has HDCP 1.4 capabilities.
* The sink is determined to be HDCP 1.4 capable the BKSV indicates 20 ones and
* 20 zeros. If the sink is capable of HDCP 1.4, then this function checks if
* the Bcaps register indicates that the connected device a DVI or HDMI receiver.
* If the receiver is determined to be HDMI, then the function will return FALSE
* until the receiver has set the HDMI_MODE in the Bstatus register.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - TRUE if sink is HDCP 1.4 capable and ready to authenticate.
*  - FALSE if sink does not support HDCP 1.4 or is not ready.
*
******************************************************************************/
u8 XV_HdmiTxSs_IsSinkHdcp14Capable(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

#ifdef XPAR_XHDCP_NUM_INSTANCES
  int status;
  u8 buffer[5];
  u8 temp = 0;
  int zero_count = 0;
  int one_count = 0;
  int i,j;

  if (InstancePtr->Hdcp14Ptr) {
    buffer[0] = 0x0; // XHDCP14_BKSV_REG
    status = XV_HdmiTx_DdcWrite(InstancePtr->HdmiTxPtr,
                                0x3A,
                                1,
                                (u8*)&buffer,
                                FALSE);
    if (status != XST_SUCCESS)
      return FALSE;

    /* Read the receiver KSV and count the number of ones and zeros.
       A valid KSV has 20 ones and 20 zeros. */
    status = XV_HdmiTx_DdcRead(InstancePtr->HdmiTxPtr,
                               0x3A,
                               5,
                               (u8*)&buffer,
                               TRUE);
    if (status != XST_SUCCESS)
      return FALSE;

    for(i = 0; i < 5; i++) {
      temp = buffer[i];

      for(j = 0; j < 8; j++) {
        if(temp & 0x1)
          one_count++;
        else
          zero_count++;

        temp = temp >> 1;
      }
    }

    if (one_count != 20 || zero_count != 20)
      return FALSE;

    /* Check if the sink device is ready to authenticate */
    if (XHdcp1x_IsDwnstrmCapable(InstancePtr->Hdcp14Ptr)) {
      return TRUE;
    }
  }
  else {
    return FALSE;
  }
#endif

  return FALSE;
}

/*****************************************************************************/
/**
*
* This function determines if the connected HDMI sink has HDCP 2.2 capabilities.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - TRUE if sink is HDCP 2.2 capable
*  - FALSE if sink does not support HDCP 2.2 or HDCP 2.2 is not available.
*
******************************************************************************/
u8 XV_HdmiTxSs_IsSinkHdcp22Capable(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  int status;
  u8 data = 0x50; // XHDCP2_VERSION_REG

  if (InstancePtr->Hdcp22Ptr) {
    /* Write the register offset */
    status = XV_HdmiTx_DdcWrite(InstancePtr->HdmiTxPtr, 0x3A, 1, (u8*)&data, FALSE);
    if (status != XST_SUCCESS)
      return FALSE;

    /* Read the HDCP2 version */
    status = XV_HdmiTx_DdcRead(InstancePtr->HdmiTxPtr, 0x3A, 1, (u8*)&data, TRUE);
    if (status != XST_SUCCESS)
      return FALSE;

    /* Check the HDCP2.2 version */
    if(data & 0x4)
      return TRUE;
    else
      return FALSE;
  }
  else {
    return FALSE;
  }
#endif

  return FALSE;
}

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function checks if the active HDCP protocol is enabled.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - TRUE if active protocol is enabled
*  - FALSE if active protocol is disabled
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpIsEnabled(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
      return FALSE;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14 :
      return XHdcp1x_IsEnabled(InstancePtr->Hdcp14Ptr);
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_22 :
      return XHdcp22Tx_IsEnabled(InstancePtr->Hdcp22Ptr);
#endif

    default :
      return FALSE;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function checks if the active HDCP protocol is authenticated.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - TRUE if active protocol is authenticated
*  - FALSE if active protocol is not authenticated
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpIsAuthenticated(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
      return FALSE;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14 :
      return XHdcp1x_IsAuthenticated(InstancePtr->Hdcp14Ptr);
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_22 :
      return XHdcp22Tx_IsAuthenticated(InstancePtr->Hdcp22Ptr);
#endif

    default :
      return FALSE;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function checks if the active HDCP protocol has encryption enabled.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - TRUE if active protocol has encryption enabled
*  - FALSE if active protocol has encryption disabled
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpIsEncrypted(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
      return FALSE;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14 :
      return XHdcp1x_IsEncrypted(InstancePtr->Hdcp14Ptr);
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_22 :
      return XHdcp22Tx_IsEncryptionEnabled(InstancePtr->Hdcp22Ptr);
#endif

    default :
      return FALSE;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function checks if the active HDCP protocol is busy authenticating.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - TRUE if active protocol is busy authenticating
*  - FALSE if active protocol is not busy authenticating
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpIsInProgress(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
      return FALSE;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14 :
      return XHdcp1x_IsInProgress(InstancePtr->Hdcp14Ptr);
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_22 :
      return XHdcp22Tx_IsInProgress(InstancePtr->Hdcp22Ptr);
#endif

    default :
      return FALSE;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function checks if the active HDCP protocol is in computations state.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - TRUE if active protocol is in computations state
*  - FALSE if active protocol is not in computations state
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpIsInComputations(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
      return FALSE;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14 :
      return XHdcp1x_IsInComputations(InstancePtr->Hdcp14Ptr);
#endif

    case XV_HDMITXSS_HDCP_22 :
      return FALSE;

    default :
      return FALSE;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function checks if the active HDCP protocol is in wait-for-ready state.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*  - TRUE if active protocol is in computations state
*  - FALSE if active protocol is not in computations state
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpIsInWaitforready(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
      return FALSE;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14 :
      return XHdcp1x_IsInWaitforready(InstancePtr->Hdcp14Ptr);
#endif

    case XV_HDMITXSS_HDCP_22 :
      return FALSE;

    default :
      return FALSE;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function sets pointers to the HDCP 1.4 and HDCP 2.2 keys and
* System Renewability Message (SRM).
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_HdcpSetKey(XV_HdmiTxSs *InstancePtr,
                            XV_HdmiTxSs_HdcpKeyType KeyType,
                            u8 *KeyPtr)
{
  /* Verify argument. */
  Xil_AssertVoid(InstancePtr != NULL);
  Xil_AssertVoid(KeyType <= XV_HDMITXSS_KEY_INVALID);

  switch (KeyType) {

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2 LC128
    case XV_HDMITXSS_KEY_HDCP22_LC128 :
      InstancePtr->Hdcp22Lc128Ptr = KeyPtr;
      break;

    // HDCP 2.2 SRM
    case XV_HDMITXSS_KEY_HDCP22_SRM :
      InstancePtr->Hdcp22SrmPtr = KeyPtr;
      break;
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_KEY_HDCP14 :
      InstancePtr->Hdcp14KeyPtr = KeyPtr;
      break;

    // HDCP 1.4 SRM
    case XV_HDMITXSS_KEY_HDCP14_SRM :
      InstancePtr->Hdcp14SrmPtr = KeyPtr;
      break;
#endif

    default :
      break;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function reports the HDCP info
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_HdcpInfo(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertVoid(InstancePtr != NULL);

  switch (InstancePtr->HdcpProtocol) {
    case XV_HDMITXSS_HDCP_NONE :
      xil_printf("\r\nHDCP TX is disabled\r\n");
      break;

#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14 :
      if (InstancePtr->Hdcp14Ptr) {
        if (XHdcp1x_IsEnabled(InstancePtr->Hdcp14Ptr)) {
          xil_printf("\r\nHDCP 1.4 TX Info\r\n");

          // Route debug output to xil_printf
          XHdcp1x_SetDebugPrintf(xil_printf);

          // Display info
          XHdcp1x_Info(InstancePtr->Hdcp14Ptr);
        }
        else {
          xil_printf("\r\nHDCP 1.4 TX is disabled\r\n");
        }
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22 :
      if (InstancePtr->Hdcp22Ptr) {
        if (XHdcp22Tx_IsEnabled(InstancePtr->Hdcp22Ptr)) {
          XHdcp22Tx_LogDisplay(InstancePtr->Hdcp22Ptr);

          xil_printf("HDCP 2.2 TX Info\r\n");
          XHdcp22Tx_Info(InstancePtr->Hdcp22Ptr);
        }
        else {
          xil_printf("\r\nHDCP 2.2 TX is disabled\r\n");
        }
      }
      break;
#endif

    default:
      xil_printf("\r\nHDCP Info Unknown?\r\n");
      break;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function sets the logging level.
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_HdcpSetInfoDetail(XV_HdmiTxSs *InstancePtr, u8 Verbose)
{
  /* Verify argument. */
  Xil_AssertVoid(InstancePtr != NULL);

  if (Verbose) {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    if (InstancePtr->Hdcp14Ptr) {
      XHdcp1x_SetDebugLogMsg(xil_printf);
    }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    if (InstancePtr->Hdcp22Ptr) {
      XHdcp22Tx_LogReset(InstancePtr->Hdcp22Ptr, TRUE);
    }
#endif
  } else {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    if (InstancePtr->Hdcp14Ptr) {
      XHdcp1x_SetDebugLogMsg(NULL);
    }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    if (InstancePtr->Hdcp22Ptr) {
      XHdcp22Tx_LogReset(InstancePtr->Hdcp22Ptr, FALSE);
    }
#endif
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the HDCP repeater topology for the active protocol.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return Pointer to repeater topology structure.
*
* @note   None.
*
******************************************************************************/
void *XV_HdmiTxSs_HdcpGetTopology(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  void *RepeaterTopologyPtr = NULL;

  switch (InstancePtr->HdcpProtocol)
  {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        RepeaterTopologyPtr = XHdcp1x_GetTopology(InstancePtr->Hdcp14Ptr);
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        RepeaterTopologyPtr = XHdcp22Tx_GetTopology(InstancePtr->Hdcp22Ptr);
      }
      break;
#endif

    default:
      RepeaterTopologyPtr = NULL;
  }

  return RepeaterTopologyPtr;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the HDCP repeater topology list.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return Pointer to repeater topology list.
*
* @note   None.
*
******************************************************************************/
u8 *XV_HdmiTxSs_HdcpGetTopologyReceiverIdList(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  u8 *ListPtr = NULL;

  switch (InstancePtr->HdcpProtocol)
  {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        ListPtr = XHdcp1x_GetTopologyKSVList(InstancePtr->Hdcp14Ptr);
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        ListPtr = XHdcp22Tx_GetTopologyReceiverIdList(InstancePtr->Hdcp22Ptr);
      }
      break;
#endif

    default:
      ListPtr = NULL;
  }

  return ListPtr;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets various fields inside the HDCP repeater topology.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
* @param Field indicates what field of the topology structure to update.
*
* @return XST_SUCCESS or XST_FAILURE.
*
* @note   None.
*
******************************************************************************/
u32 XV_HdmiTxSs_HdcpGetTopologyField(XV_HdmiTxSs *InstancePtr,
                                     XV_HdmiTxSs_HdcpTopologyField Field)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);
  Xil_AssertNonvoid(Field < XV_HDMITXSS_HDCP_TOPOLOGY_INVALID);

  switch (Field)
  {
      case XV_HDMITXSS_HDCP_TOPOLOGY_DEPTH:
      return XV_HdmiTxSs_HdcpGetTopologyDepth(InstancePtr);
      case XV_HDMITXSS_HDCP_TOPOLOGY_DEVICECNT:
      return XV_HdmiTxSs_HdcpGetTopologyDeviceCnt(InstancePtr);
      case XV_HDMITXSS_HDCP_TOPOLOGY_MAXDEVSEXCEEDED:
      return XV_HdmiTxSs_HdcpGetTopologyMaxDevsExceeded(InstancePtr);
      case XV_HDMITXSS_HDCP_TOPOLOGY_MAXCASCADEEXCEEDED:
      return XV_HdmiTxSs_HdcpGetTopologyMaxCascadeExceeded(InstancePtr);
      case XV_HDMITXSS_HDCP_TOPOLOGY_HDCP20REPEATERDOWNSTREAM:
      return XV_HdmiTxSs_HdcpGetTopologyHdcp20RepeaterDownstream(InstancePtr);
      case XV_HDMITXSS_HDCP_TOPOLOGY_HDCP1DEVICEDOWNSTREAM:
      return XV_HdmiTxSs_HdcpGetTopologyHdcp1DeviceDownstream(InstancePtr);
    default:
      return 0;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the HDCP repeater topology depth.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return Depth from the repeater topology table.
*
* @note   None.
*
******************************************************************************/
static u32 XV_HdmiTxSs_HdcpGetTopologyDepth(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  u8 Depth = 0;

  switch (InstancePtr->HdcpProtocol)
  {
    // HDCP 1.4
#ifdef XPAR_XHDCP_NUM_INSTANCES
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        Depth = XHdcp1x_GetTopologyField(InstancePtr->Hdcp14Ptr,
                  XHDCP1X_TOPOLOGY_DEPTH);
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        Depth = XHdcp22Tx_GetTopologyField(InstancePtr->Hdcp22Ptr,
                  XHDCP22_TX_TOPOLOGY_DEPTH);
      }
      break;
#endif

    default :
      Depth = 0;
  }

  return Depth;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the HDCP repeater topology device count.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return Depth from the repeater topology table.
*
* @note   None.
*
******************************************************************************/
static u32 XV_HdmiTxSs_HdcpGetTopologyDeviceCnt(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  u8 DeviceCnt = 0;

  switch (InstancePtr->HdcpProtocol)
  {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        DeviceCnt = XHdcp1x_GetTopologyField(InstancePtr->Hdcp14Ptr,
                      XHDCP1X_TOPOLOGY_DEVICECNT);
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        DeviceCnt = XHdcp22Tx_GetTopologyField(InstancePtr->Hdcp22Ptr,
                      XHDCP22_TX_TOPOLOGY_DEVICECNT);
      }
      break;
#endif

    default:
      DeviceCnt = 0;
  }

  return DeviceCnt;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the HDCP repeater topology maximum devices exceeded
* flag.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*   - TRUE if maximum devices exceeded.
*   - FALSE if maximum devices not exceeded.
*
* @note   None.
*
******************************************************************************/
static u8 XV_HdmiTxSs_HdcpGetTopologyMaxDevsExceeded(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  u8 Flag = FALSE;

  switch (InstancePtr->HdcpProtocol)
  {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        Flag = XHdcp1x_GetTopologyField(InstancePtr->Hdcp14Ptr,
                 XHDCP1X_TOPOLOGY_MAXDEVSEXCEEDED);
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        Flag = XHdcp22Tx_GetTopologyField(InstancePtr->Hdcp22Ptr,
                 XHDCP22_TX_TOPOLOGY_MAXDEVSEXCEEDED);
      }
      break;
#endif

    default:
      Flag = FALSE;
  }

  return Flag;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the HDCP repeater topology maximum cascade exceeded
* flag.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*   - TRUE if maximum cascade exceeded.
*   - FALSE if maximum cascade not exceeded.
*
* @note   None.
*
******************************************************************************/
static
u8 XV_HdmiTxSs_HdcpGetTopologyMaxCascadeExceeded(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  u8 Flag = FALSE;

  switch (InstancePtr->HdcpProtocol)
  {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        Flag = XHdcp1x_GetTopologyField(InstancePtr->Hdcp14Ptr,
                 XHDCP1X_TOPOLOGY_MAXCASCADEEXCEEDED);
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        Flag = XHdcp22Tx_GetTopologyField(InstancePtr->Hdcp22Ptr,
                XHDCP22_TX_TOPOLOGY_MAXCASCADEEXCEEDED);
      }
      break;
#endif

    default:
      Flag = FALSE;
  }

  return Flag;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the HDCP repeater topology HDCP 2.0 repeater downstream
* flag.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*   - TRUE if HDCP 2.0 repeater is downstream.
*   - FALSE if HDCP 2.0 repeater is not downstream.
*
* @note   None.
*
******************************************************************************/
static
u8 XV_HdmiTxSs_HdcpGetTopologyHdcp20RepeaterDownstream(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  u8 Flag = FALSE;

  switch (InstancePtr->HdcpProtocol)
  {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        Flag = FALSE;
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        Flag = XHdcp22Tx_GetTopologyField(InstancePtr->Hdcp22Ptr,
                 XHDCP22_TX_TOPOLOGY_HDCP20REPEATERDOWNSTREAM);
      }
      break;
#endif

    default:
      Flag = FALSE;
  }

  return Flag;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function gets the HDCP repeater topology HDCP 1.x device downstream
* flag.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*   - TRUE if HDCP 1.x device is downstream.
*   - FALSE if HDCP 1.x device is not downstream.
*
* @note   None.
*
******************************************************************************/
static
   u8 XV_HdmiTxSs_HdcpGetTopologyHdcp1DeviceDownstream(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  u8 Flag = FALSE;

  switch (InstancePtr->HdcpProtocol)
  {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        Flag = TRUE;
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        Flag = XHdcp22Tx_GetTopologyField(InstancePtr->Hdcp22Ptr,
                 XHDCP22_TX_TOPOLOGY_HDCP1DEVICEDOWNSTREAM);
      }
      break;
#endif

    default:
      Flag = FALSE;
  }

  return Flag;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function sets the HDCP repeater content stream management type.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_HdcpSetContentStreamType(XV_HdmiTxSs *InstancePtr,
       XV_HdmiTxSs_HdcpContentStreamType StreamType)
{
  /* Verify argument. */
  Xil_AssertVoid(InstancePtr != NULL);
  Xil_AssertVoid(StreamType <= XV_HDMITXSS_HDCP_STREAMTYPE_1);

  switch (InstancePtr->HdcpProtocol)
  {
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      break;

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        XHdcp22Tx_SetContentStreamType(InstancePtr->Hdcp22Ptr,
                                       (XHdcp22_Tx_ContentStreamType) StreamType);
      }
      break;
#endif

    default:
      break;
  }
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function checks if the HDMI transmitter is an HDCP repeater
* downstream interface for the active protocol.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
*
* @return
*   - TRUE if repeater downstream interface.
*   - FALSE if not repeater downstream interface.
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpIsRepeater(XV_HdmiTxSs *InstancePtr)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  int Status = (int)FALSE;

  switch (InstancePtr->HdcpProtocol)
  {
#ifdef XPAR_XHDCP_NUM_INSTANCES
    // HDCP 1.4
    case XV_HDMITXSS_HDCP_14:
      if (InstancePtr->Hdcp14Ptr) {
        Status = XHdcp1x_IsRepeater(InstancePtr->Hdcp14Ptr);
      }
      break;
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    // HDCP 2.2
    case XV_HDMITXSS_HDCP_22:
      if (InstancePtr->Hdcp22Ptr) {
        Status = XHdcp22Tx_IsRepeater(InstancePtr->Hdcp22Ptr);
      }
      break;
#endif

    default:
      Status = (int)FALSE;
  }

  return Status;
}
#endif

#ifdef USE_HDCP_TX
/*****************************************************************************/
/**
*
* This function enables the Repeater functionality for the HDCP protocol.
*
* @param InstancePtr is a pointer to the XV_HdmiTxSs instance.
* @param Set is TRUE to enable and FALSE to disable repeater.
*
* @return
*  - XST_SUCCESS if action was successful
*  - XST_FAILURE if action was not successful
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_HdcpSetRepeater(XV_HdmiTxSs *InstancePtr, u8 Set)
{
  /* Verify argument. */
  Xil_AssertNonvoid(InstancePtr != NULL);

#ifdef XPAR_XHDCP_NUM_INSTANCES
  // HDCP 1.4
  if (InstancePtr->Hdcp14Ptr) {
    XHdcp1x_SetRepeater(InstancePtr->Hdcp14Ptr, Set);
  }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  // HDCP 2.2
  if (InstancePtr->Hdcp22Ptr) {
    XHdcp22Tx_SetRepeater(InstancePtr->Hdcp22Ptr, Set);
  }
#endif

  return XST_SUCCESS;
}
#endif

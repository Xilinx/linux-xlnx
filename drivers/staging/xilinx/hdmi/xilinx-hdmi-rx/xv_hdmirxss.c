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
* @file xv_hdmirxss.c
*
* This is main code of Xilinx HDMI Receiver Subsystem device driver.
* Please see xv_hdmirxss.h for more details of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00         10/07/15 Initial release.
* 1.1   yh     15/01/16 Added 3D Video support
* 1.2   yh     20/01/16 Added remapper support
* 1.3   yh     01/02/16 Added set_ppc api
* 1.4   yh     01/02/16 Removed xil_print "Cable (dis)connected"
* 1.5   yh     01/02/16 Removed xil_printf("Active audio channels...)
* 1.6   yh     15/02/16 Added default value to XV_HdmiRxSs_ConfigRemapper
* 1.7   MG     03/02/16 Added HDCP support
* 1.8   MG     10/02/16 Moved HDCP 2.2 reset from stream up/down callback
*                       to connect callback
* 1.9   MH     15/03/16 Added HDCP authenticated callback support
* 1.10  MH     23/04/16 1. HDCP 1.x driver now uses AXI timer 4.1, so updated
*                       to use AXI Timer config structure to determine timer
*                       clock frequency
*                       2. HDCP 1.x driver has fixed the problem where the
*                       reset for the receiver causes the entire DDC peripheral
*                       to get reset. Based on this change the driver has been
*                       updated to use XV_HdmiRxSs_HdcpReset and
*                       XV_HdmiRxSs_HdcpReset functions directly.
*                       3. Updated XV_HdmiRxSs_HdcpEnable and
*                       XV_HdmiRxSs_HdcpEnable functions to ensure that
*                       HDCP 1.4 and 2.2 are mutually exclusive.
*                       This fixes the problem where HDCP 1.4 and 2.2
*                       state machines are running simultaneously.
* 1.11  MG     13/05/16 Added DDC peripheral HDCP mode selection to XV_HdmiRxSs_HdcpEnable
* 1.12  MH     23/06/16 Added HDCP repeater support.
* 1.13  YH     18/07/16 1. Replace xil_print with xdbg_printf.
*                       2. Replace MB_Sleep() with usleep_range(,  + /10)
* 1.14  YH     25/07/16 Used UINTPTR instead of u32 for BaseAddress
*                       XV_HdmiRxSs_CfgInitialize
* 1.15  MH     26/07/16 Updates for automatic protocol switching
* 1.16  MH     05/08/16 Updates to optimize out HDCP when excluded
* 1.17  YH     17/08/16 Remove sleep in XV_HdmiRxSs_ResetRemapper
*                       squash unused variable compiler warning
*                       Added Event Log
* 1.18  MH     08/10/16 Improve HDCP 1.4 authentication
* 1.19  MG     31/10/16 Fixed issue with reference clock compensation in
*                           XV_HdmiRxSS_SetStream
* 1.20  YH     14/11/16 Added API to enable/disable YUV420/Pixel Drop Mode
*                       for video bridge
* 1.21  YH     14/11/16 Remove Remapper APIs
*                       Replace XV_HdmiRxSs_ConfigRemapper API with
*                       XV_HdmiRxSs_ConfigBridgeMode API as remapper feature is
*                           moved to video bridge and controlled by HDMI core
* 1.22  MMO    03/01/17 Add compiler option(XV_HDMIRXSS_LOG_ENABLE) to enable
*                           Log
*                       Move global variable XV_HdmiRx_VSIF VSIF to local
*                           XV_HdmiRxSs_RetrieveVSInfoframe API
*                       Move HDCP related API's to hdmirxss_hdcp.c
* 1.23  MMO    10/02/17 Added Sync Loss and HDMI/DVI Interrupt Support
*
******************************************************************************/

/***************************** Include Files *********************************/
#include <linux/delay.h>
#include "xv_hdmirxss.h"
#include "xv_hdmirxss_coreinit.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/
/**
 * This typedef declares the driver instances of all the cores in the subsystem
 */
typedef struct
{
#ifdef XPAR_XHDCP_NUM_INSTANCES
  XTmrCtr HdcpTimer;
  XHdcp1x Hdcp14;
#endif
#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
  XHdcp22_Rx Hdcp22;
#endif
  XV_HdmiRx HdmiRx;
} XV_HdmiRxSs_SubCores;

/**************************** Local Global ***********************************/
/** Define Driver instance of all sub-core included in the design */
XV_HdmiRxSs_SubCores XV_HdmiRxSs_SubCoreRepo[XPAR_XV_HDMIRXSS_NUM_INSTANCES];

/************************** Function Prototypes ******************************/
static void XV_HdmiRxSs_GetIncludedSubcores(XV_HdmiRxSs *HdmiRxSsPtr,
    u16 DevId);
static void XV_HdmiRxSs_WaitUs(XV_HdmiRxSs *InstancePtr, u32 MicroSeconds);
static void XV_HdmiRxSs_RetrieveVSInfoframe(XV_HdmiRx *HdmiRxPtr);
static int XV_HdmiRxSs_RegisterSubsysCallbacks(XV_HdmiRxSs *InstancePtr);
static void XV_HdmiRxSs_ConnectCallback(void *CallbackRef);
static void XV_HdmiRxSs_AuxCallback(void *CallbackRef);
static void XV_HdmiRxSs_AudCallback(void *CallbackRef);
static void XV_HdmiRxSs_LnkStaCallback(void *CallbackRef);
static void XV_HdmiRxSs_DdcCallback(void *CallbackRef);
static void XV_HdmiRxSs_StreamDownCallback(void *CallbackRef);
static void XV_HdmiRxSs_StreamInitCallback(void *CallbackRef);
static void XV_HdmiRxSs_StreamUpCallback(void *CallbackRef);
static void XV_HdmiRxSs_SyncLossCallback(void *CallbackRef);
static void XV_HdmiRxSs_ModeCallback(void *CallbackRef);

static void XV_HdmiRxSs_ReportCoreInfo(XV_HdmiRxSs *InstancePtr);
static void XV_HdmiRxSs_ReportTiming(XV_HdmiRxSs *InstancePtr);
static void XV_HdmiRxSs_ReportLinkQuality(XV_HdmiRxSs *InstancePtr);
static void XV_HdmiRxSs_ReportAudio(XV_HdmiRxSs *InstancePtr);
static void XV_HdmiRxSs_ReportInfoFrame(XV_HdmiRxSs *InstancePtr);
static void XV_HdmiRxSs_ReportSubcoreVersion(XV_HdmiRxSs *InstancePtr);

static void XV_HdmiRxSs_ConfigBridgeMode(XV_HdmiRxSs *InstancePtr);

/***************** Macros (Inline Functions) Definitions *********************/
/*****************************************************************************/
/**
* This macros selects the bridge YUV420 mode
*
* @param  InstancePtr is a pointer to the HDMI RX Subsystem
*
*****************************************************************************/
#define XV_HdmiRxSs_BridgeYuv420(InstancePtr,Enable) \
{ \
    XV_HdmiRx_Bridge_yuv420(InstancePtr->HdmiRxPtr, Enable); \
} \

/*****************************************************************************/
/**
* This macros selects the bridge pixel repeat mode
*
* @param  InstancePtr is a pointer to the HDMI TX Subsystem
*
*****************************************************************************/
#define XV_HdmiRxSs_BridgePixelDrop(InstancePtr,Enable) \
{ \
    XV_HdmiRx_Bridge_pixel(InstancePtr->HdmiRxPtr, Enable); \
}

/************************** Function Definition ******************************/

void XV_HdmiRxSs_ReportInfo(XV_HdmiRxSs *InstancePtr)
{
    xil_printf("------------\r\n");
    xil_printf("HDMI RX SubSystem\r\n");
    xil_printf("------------\r\n");
    XV_HdmiRxSs_ReportCoreInfo(InstancePtr);
    XV_HdmiRxSs_ReportSubcoreVersion(InstancePtr);
    xil_printf("\r\n");
    xil_printf("HDMI RX timing\r\n");
    xil_printf("------------\r\n");
    XV_HdmiRxSs_ReportTiming(InstancePtr);
    xil_printf("Link quality\r\n");
    xil_printf("---------\r\n");
    XV_HdmiRxSs_ReportLinkQuality(InstancePtr);
    xil_printf("Audio\r\n");
    xil_printf("---------\r\n");
    XV_HdmiRxSs_ReportAudio(InstancePtr);
    xil_printf("Infoframe\r\n");
    xil_printf("---------\r\n");
    XV_HdmiRxSs_ReportInfoFrame(InstancePtr);
    xil_printf("\r\n");
}

/*****************************************************************************/
/**
* This function reports list of cores included in Video Processing Subsystem
*
* @param  InstancePtr is a pointer to the Subsystem instance.
*
* @return None
*
******************************************************************************/
static void XV_HdmiRxSs_ReportCoreInfo(XV_HdmiRxSs *InstancePtr)
{
  Xil_AssertVoid(InstancePtr != NULL);

  xil_printf("\r\n  ->HDMI RX Subsystem Cores\r\n");

  /* Report all the included cores in the subsystem instance */
  if(InstancePtr->HdmiRxPtr)
  {
    xil_printf("    : HDMI RX \r\n");
  }

#ifdef XPAR_XHDCP_NUM_INSTANCES
  if(InstancePtr->Hdcp14Ptr)
  {
    xil_printf("    : HDCP 1.4 RX \r\n");
  }

  if(InstancePtr->HdcpTimerPtr)
  {
    xil_printf("    : HDCP: AXIS Timer\r\n");
  }
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
  if(InstancePtr->Hdcp22Ptr)
  {
    xil_printf("    : HDCP 2.2 RX \r\n");
  }
#endif
}

/******************************************************************************/
/**
 * This function installs a custom delay/sleep function to be used by the
 *  XV_HdmiRxSs driver.
 *
 * @param   InstancePtr is a pointer to the HdmiSsRx instance.
 * @param   CallbackFunc is the address to the callback function.
 * @param   CallbackRef is the user data item (microseconds to delay) that
 *      will be passed to the custom sleep/delay function when it is
 *      invoked.
 *
 * @return  None.
 *
 * @note    None.
 *
*******************************************************************************/
void XV_HdmiRxSs_SetUserTimerHandler(XV_HdmiRxSs *InstancePtr,
            XVidC_DelayHandler CallbackFunc, void *CallbackRef)
{
    /* Verify arguments. */
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(CallbackFunc != NULL);
    Xil_AssertVoid(CallbackRef != NULL);

    InstancePtr->UserTimerWaitUs = CallbackFunc;
    InstancePtr->UserTimerPtr = CallbackRef;
}

/******************************************************************************/
/**
 * This function is the delay/sleep function for the XV_HdmiRxSs driver. For the
 * Zynq family, there exists native sleep functionality. For MicroBlaze however,
 * there does not exist such functionality. In the MicroBlaze case, the default
 * method for delaying is to use a predetermined amount of loop iterations. This
 * method is prone to inaccuracy and dependent on system configuration; for
 * greater accuracy, the user may supply their own delay/sleep handler, pointed
 * to by InstancePtr->UserTimerWaitUs, which may have better accuracy if a
 * hardware timer is used.
 *
 * @param   InstancePtr is a pointer to the HdmiSsRx instance.
 * @param   MicroSeconds is the number of microseconds to delay/sleep for.
 *
 * @return  None.
 *
 * @note    None.
 *
*******************************************************************************/
static void XV_HdmiRxSs_WaitUs(XV_HdmiRxSs *InstancePtr, u32 MicroSeconds)
{
    /* Verify arguments. */
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    if (MicroSeconds == 0) {
        return;
    }

    if (InstancePtr->UserTimerWaitUs != NULL) {
        /* Use the timer handler specified by the user for better
         * accuracy. */
        InstancePtr->UserTimerWaitUs(InstancePtr, MicroSeconds);
    }
    else {
        usleep_range(MicroSeconds, MicroSeconds + MicroSeconds/10);
    }
}

/*****************************************************************************/
/**
 * This function calls the interrupt handler for HDMI RX
 *
 * @param  InstancePtr is a pointer to the HDMI RX Subsystem
 *
 *****************************************************************************/
void XV_HdmiRxSS_HdmiRxIntrHandler(XV_HdmiRxSs *InstancePtr)
{
    XV_HdmiRx_IntrHandler(InstancePtr->HdmiRxPtr);
}

/*****************************************************************************/
/**
 * This function register's all sub-core ISR's with interrupt controller and
 * any subsystem level call back function with requisite sub-core
 *
 * @param  InstancePtr is a pointer to the Subsystem instance to be
 *       worked on.
 *
 *****************************************************************************/
static int XV_HdmiRxSs_RegisterSubsysCallbacks(XV_HdmiRxSs *InstancePtr)
{
  XV_HdmiRxSs *HdmiRxSsPtr = InstancePtr;

  //Register HDMI callbacks
  if(HdmiRxSsPtr->HdmiRxPtr) {
    /*
     * Register call back for Rx Core Interrupts.
     */
    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_CONNECT,
                          (void *)XV_HdmiRxSs_ConnectCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_AUX,
						  (void *)XV_HdmiRxSs_AuxCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_AUD,
						  (void *)XV_HdmiRxSs_AudCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_LNKSTA,
						  (void *)XV_HdmiRxSs_LnkStaCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_DDC,
						  (void *)XV_HdmiRxSs_DdcCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_STREAM_DOWN,
						  (void *)XV_HdmiRxSs_StreamDownCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_STREAM_INIT,
						  (void *)XV_HdmiRxSs_StreamInitCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_STREAM_UP,
						  (void *)XV_HdmiRxSs_StreamUpCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_SYNC_LOSS,
						  (void *)XV_HdmiRxSs_SyncLossCallback,
						  (void *)InstancePtr);

    XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                          XV_HDMIRX_HANDLER_MODE,
						  (void *)XV_HdmiRxSs_ModeCallback,
						  (void *)InstancePtr);
  }

  return(XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function queries the subsystem instance configuration to determine
* the included sub-cores. For each sub-core that is present in the design
* the sub-core driver instance is binded with the subsystem sub-core driver
* handle
*
* @param  HdmiRxSsPtr is a pointer to the Subsystem instance to be worked on.
*
* @return None
*
******************************************************************************/
static void XV_HdmiRxSs_GetIncludedSubcores(XV_HdmiRxSs *HdmiRxSsPtr, u16 DevId)
{
  HdmiRxSsPtr->HdmiRxPtr   =((HdmiRxSsPtr->Config.HdmiRx.IsPresent) ?
                            (&XV_HdmiRxSs_SubCoreRepo[DevId].HdmiRx) : NULL);
#ifdef XPAR_XHDCP_NUM_INSTANCES
  HdmiRxSsPtr->Hdcp14Ptr   =((HdmiRxSsPtr->Config.Hdcp14.IsPresent) ?
                            (&XV_HdmiRxSs_SubCoreRepo[DevId].Hdcp14) : NULL);
  HdmiRxSsPtr->HdcpTimerPtr=((HdmiRxSsPtr->Config.HdcpTimer.IsPresent) ?
                            (&XV_HdmiRxSs_SubCoreRepo[DevId].HdcpTimer) : NULL);
#endif
#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
  HdmiRxSsPtr->Hdcp22Ptr   =((HdmiRxSsPtr->Config.Hdcp22.IsPresent) ?
                            (&XV_HdmiRxSs_SubCoreRepo[DevId].Hdcp22) : NULL);
#endif
}

/*****************************************************************************/
/**
* This function initializes the video subsystem and included sub-cores.
* This function must be called prior to using the subsystem. Initialization
* includes setting up the instance data for top level as well as all included
* sub-core therein, and ensuring the hardware is in a known stable state.
*
* @param  InstancePtr is a pointer to the Subsystem instance to be worked on.
* @param  CfgPtr points to the configuration structure associated with the
*         subsystem instance.
* @param  EffectiveAddr is the base address of the device. If address
*         translation is being used, then this parameter must reflect the
*         virtual base address. Otherwise, the physical address should be
*         used.
*
* @return XST_SUCCESS if initialization is successful else XST_FAILURE
*
******************************************************************************/
int XV_HdmiRxSs_CfgInitialize(XV_HdmiRxSs *InstancePtr,
    XV_HdmiRxSs_Config *CfgPtr,
    UINTPTR EffectiveAddr)
{
  XV_HdmiRxSs *HdmiRxSsPtr = InstancePtr;

  /* Verify arguments */
  Xil_AssertNonvoid(HdmiRxSsPtr != NULL);
  Xil_AssertNonvoid(CfgPtr != NULL);
  Xil_AssertNonvoid(EffectiveAddr != (UINTPTR)NULL);

  /* Setup the instance */
  memcpy((void *)&(HdmiRxSsPtr->Config), (const void *)CfgPtr,
    sizeof(XV_HdmiRxSs_Config));
  HdmiRxSsPtr->Config.BaseAddress = EffectiveAddr;

  /* Determine sub-cores included in the provided instance of subsystem */
  XV_HdmiRxSs_GetIncludedSubcores(HdmiRxSsPtr, CfgPtr->DeviceId);

  /* Initialize all included sub_cores */
  if(HdmiRxSsPtr->HdmiRxPtr)
  {
    if(XV_HdmiRxSs_SubcoreInitHdmiRx(HdmiRxSsPtr) != XST_SUCCESS)
    {
      return(XST_FAILURE);
    }
  }

#ifdef XPAR_XHDCP_NUM_INSTANCES
  // HDCP 1.4
  if(HdmiRxSsPtr->Hdcp14Ptr)
  {
    if(XV_HdmiRxSs_SubcoreInitHdcp14(HdmiRxSsPtr) != XST_SUCCESS)
    {
      return(XST_FAILURE);
    }
  }

  if(HdmiRxSsPtr->HdcpTimerPtr)
  {
    if(XV_HdmiRxSs_SubcoreInitHdcpTimer(HdmiRxSsPtr) != XST_SUCCESS)
    {
      return(XST_FAILURE);
    }
  }
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
  // HDCP 2.2
  if(HdmiRxSsPtr->Hdcp22Ptr)
  {
    if(XV_HdmiRxSs_SubcoreInitHdcp22(HdmiRxSsPtr) != XST_SUCCESS)
    {
      return(XST_FAILURE);
    }
  }
#endif

  /* Register Callbacks */
  XV_HdmiRxSs_RegisterSubsysCallbacks(HdmiRxSsPtr);

#ifdef USE_HDCP_RX
  /* Default value */
  HdmiRxSsPtr->HdcpIsReady = (FALSE);
#endif

#if defined(XPAR_XHDCP_NUM_INSTANCES) && defined(XPAR_XHDCP22_RX_NUM_INSTANCES)
  /* HDCP is ready when both HDCP cores are instantiated and all keys are
     loaded */
  if (HdmiRxSsPtr->Hdcp14Ptr && HdmiRxSsPtr->Hdcp22Ptr &&
      HdmiRxSsPtr->Hdcp22Lc128Ptr && HdmiRxSsPtr->Hdcp14KeyPtr &&
      HdmiRxSsPtr->Hdcp22PrivateKeyPtr) {
    HdmiRxSsPtr->HdcpIsReady = (TRUE);

    /* Set default HDCP content protection scheme */
    XV_HdmiRxSs_HdcpSetProtocol(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_14);
  }
#endif

#if defined(XPAR_XHDCP_NUM_INSTANCES)
  /* HDCP is ready when only the HDCP 1.4 core is instantiated and the key is
     loaded */
  if (!HdmiRxSsPtr->HdcpIsReady && HdmiRxSsPtr->Hdcp14Ptr &&
       HdmiRxSsPtr->Hdcp14KeyPtr) {
    HdmiRxSsPtr->HdcpIsReady = (TRUE);

    /* Set default HDCP content protection scheme */
    XV_HdmiRxSs_HdcpSetProtocol(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_14);
  }
#endif

#if defined(XPAR_XHDCP22_RX_NUM_INSTANCES)
  /* HDCP is ready when only the HDCP 2.2 core is instantiated and the keys are
     loaded */
  if (!HdmiRxSsPtr->HdcpIsReady && HdmiRxSsPtr->Hdcp22Ptr &&
       HdmiRxSsPtr->Hdcp22Lc128Ptr && HdmiRxSsPtr->Hdcp22PrivateKeyPtr) {
    HdmiRxSsPtr->HdcpIsReady = (TRUE);

    /* Set default HDCP content protection scheme */
    XV_HdmiRxSs_HdcpSetProtocol(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_22);
  }
#endif

  /* Reset the hardware and set the flag to indicate the
     subsystem is ready
   */
  XV_HdmiRxSs_Reset(HdmiRxSsPtr);
  HdmiRxSsPtr->IsReady = XIL_COMPONENT_IS_READY;

  return(XST_SUCCESS);
}

/****************************************************************************/
/**
* This function starts the HDMI RX subsystem including all sub-cores that are
* included in the processing pipeline for a given use-case. Video pipe is
* started from back to front
* @param  InstancePtr is a pointer to the Subsystem instance to be worked on.
*
* @return None
*
* @note Cores are started only if the corresponding start flag in the scratch
*       pad memory is set. This allows to selectively start only those cores
*       included in the processing chain
******************************************************************************/
void XV_HdmiRxSs_Start(XV_HdmiRxSs *InstancePtr)
{
  Xil_AssertVoid(InstancePtr != NULL);

#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(InstancePtr, XV_HDMIRXSS_LOG_EVT_START, 0);
#endif
  /* Set RX hot plug detect */
  XV_HdmiRx_SetHpd(InstancePtr->HdmiRxPtr, TRUE);

  /* Disable Audio Peripheral */
  XV_HdmiRx_AudioDisable(InstancePtr->HdmiRxPtr);
  XV_HdmiRx_AudioIntrDisable(InstancePtr->HdmiRxPtr);
}

/*****************************************************************************/
/**
* This function stops the HDMI RX subsystem including all sub-cores
* Stop the video pipe starting from front to back
*
* @param  InstancePtr is a pointer to the Subsystem instance to be worked on.
*
* @return None
*
******************************************************************************/
void XV_HdmiRxSs_Stop(XV_HdmiRxSs *InstancePtr)
{
  Xil_AssertVoid(InstancePtr != NULL);
#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(InstancePtr, XV_HDMIRXSS_LOG_EVT_STOP, 0);
#endif
}

/*****************************************************************************/
/**
* This function resets the video subsystem sub-cores. There are 2 reset
* networks within the subsystem
*  - For cores that are on AXIS interface
*  - For cores that are on AXI-MM interface
*
* @param  InstancePtr is a pointer to the Subsystem instance to be worked on.
*
* @return None
*
******************************************************************************/
void XV_HdmiRxSs_Reset(XV_HdmiRxSs *InstancePtr)
{
  Xil_AssertVoid(InstancePtr != NULL);
#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(InstancePtr, XV_HDMIRXSS_LOG_EVT_RESET, 0);
#endif
  /* Assert RX reset */
  XV_HdmiRx_Reset(InstancePtr->HdmiRxPtr, TRUE);

  /* Release RX reset */
  XV_HdmiRx_Reset(InstancePtr->HdmiRxPtr, FALSE);
}

/*****************************************************************************/
/**
*
* This function is called when a RX connect event has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_ConnectCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  // Is the cable connected?
  if (XV_HdmiRx_IsStreamConnected(HdmiRxSsPtr->HdmiRxPtr)) {
#ifdef XV_HDMIRXSS_LOG_ENABLE
    XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_CONNECT, 0);
#endif
    // Set RX hot plug detect
    XV_HdmiRx_SetHpd(HdmiRxSsPtr->HdmiRxPtr, TRUE);

    // Set stream connected flag
    HdmiRxSsPtr->IsStreamConnected = (TRUE);

#ifdef USE_HDCP_RX
    // Push connect event to HDCP event queue
    XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_CONNECT_EVT);
#endif
  }

  // RX cable is disconnected
  else {
#ifdef XV_HDMIRXSS_LOG_ENABLE
    XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_DISCONNECT, 0);
#endif
    // Clear RX hot plug detect
    XV_HdmiRx_SetHpd(HdmiRxSsPtr->HdmiRxPtr, FALSE);

    // Set stream connected flag
    HdmiRxSsPtr->IsStreamConnected = (FALSE);

#ifdef USE_HDCP_RX
    // Push disconnect event to HDCP event queue
    XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_DISCONNECT_EVT);
#endif

    XV_HdmiRx_SetScrambler(HdmiRxSsPtr->HdmiRxPtr, (FALSE)); //Disable scrambler
  }


  // Check if user callback has been registered
  if (HdmiRxSsPtr->ConnectCallback) {
    HdmiRxSsPtr->ConnectCallback(HdmiRxSsPtr->ConnectRef);
  }

}

/*****************************************************************************/
/**
*
* This function is called when a RX AUX IRQ has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_AuxCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  // Retrieve Vendor Specific Info Frame
  XV_HdmiRxSs_RetrieveVSInfoframe(HdmiRxSsPtr->HdmiRxPtr);

  // HDMI mode
  if (XV_HdmiRxSs_GetVideoStreamType(HdmiRxSsPtr )) {
#ifdef USE_HDCP_RX
    XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_HDMI_MODE_EVT);
#endif
  }

  // Check if user callback has been registered
  if (HdmiRxSsPtr->AuxCallback) {
      HdmiRxSsPtr->AuxCallback(HdmiRxSsPtr->AuxRef);
  }
}

/*****************************************************************************/
/**
*
* This function is called when a RX Sync Loss IRQ has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_SyncLossCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  // Push sync loss event to HDCP event queue
#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_SYNCLOSS, 0);
#endif

#ifdef USE_HDCP_RX
  XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_SYNC_LOSS_EVT);
#endif
}

/*****************************************************************************/
/**
*
* This function is called when the mode has transitioned from DVI to HDMI or
* vice versa.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_ModeCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  // HDMI mode
  if (XV_HdmiRxSs_GetVideoStreamType(HdmiRxSsPtr )) {
#ifdef XV_HDMIRXSS_LOG_ENABLE
    XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_HDMIMODE, 0);
#endif
#ifdef USE_HDCP_RX
    XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_HDMI_MODE_EVT);
#endif
  }

  // DVI mode
  else {
#ifdef XV_HDMIRXSS_LOG_ENABLE
    XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_DVIMODE, 0);
#endif
#ifdef USE_HDCP_RX
    XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_DVI_MODE_EVT);
#endif
  }
}

/*****************************************************************************/
/**
*
* This function retrieves the Vendor Specific Info Frame.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_RetrieveVSInfoframe(XV_HdmiRx *HdmiRx)
{
  /** Vendor-Specific InfoFrame structure */
  XV_HdmiRx_VSIF VSIF;

  if (HdmiRx->Aux.Header.Byte[0] == 0x81) {
      XV_HdmiRx_VSIF_ParsePacket(&HdmiRx->Aux, &VSIF);

      // Defaults
      HdmiRx->Stream.Video.Is3D = FALSE;
      HdmiRx->Stream.Video.Info_3D.Format = XVIDC_3D_UNKNOWN;

      if (VSIF.Format == XV_HDMIRX_VSIF_VF_3D) {
          HdmiRx->Stream.Video.Is3D = TRUE;
          HdmiRx->Stream.Video.Info_3D = VSIF.Info_3D.Stream;
      } else if (VSIF.Format == XV_HDMIRX_VSIF_VF_EXTRES) {
          switch(VSIF.HDMI_VIC) {
              case 1 :
                  HdmiRx->Stream.Vic = 95;
                  break;

              case 2 :
                  HdmiRx->Stream.Vic = 94;
                  break;

              case 3 :
                  HdmiRx->Stream.Vic = 93;
                  break;

              case 4 :
                  HdmiRx->Stream.Vic = 98;
                  break;

              default :
                  break;
          }
      }
  }
}

/*****************************************************************************/
/**
*
* This function is called when a RX Audio IRQ has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_AudCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  u8 Channels;

  if (XV_HdmiRx_IsAudioActive(HdmiRxSsPtr->HdmiRxPtr)) {

    // Get audio channels
    Channels = XV_HdmiRx_GetAudioChannels(HdmiRxSsPtr->HdmiRxPtr);
    HdmiRxSsPtr->AudioChannels = Channels;
  }

  // Check if user callback has been registered
  if (HdmiRxSsPtr->AudCallback) {
      HdmiRxSsPtr->AudCallback(HdmiRxSsPtr->AudRef);
  }
}

/*****************************************************************************/
/**
*
* This function is called when a RX Link Status IRQ has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_LnkStaCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  HdmiRxSsPtr->IsLinkStatusErrMax =
    XV_HdmiRx_IsLinkStatusErrMax(HdmiRxSsPtr->HdmiRxPtr);
#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_LINKSTATUS, 0);
#endif
  // Check if user callback has been registered
  if (HdmiRxSsPtr->LnkStaCallback) {
      HdmiRxSsPtr->LnkStaCallback(HdmiRxSsPtr->LnkStaRef);
  }
}

/*****************************************************************************/
/**
*
* This function is called when a RX DDC IRQ has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_DdcCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  // Check if user callback has been registered
  if (HdmiRxSsPtr->DdcCallback) {
      HdmiRxSsPtr->DdcCallback(HdmiRxSsPtr->DdcRef);
  }
}

/*****************************************************************************/
/**
*
* This function is called when the RX stream is down.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_StreamDownCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  // Assert HDMI RX reset
  XV_HdmiRx_Reset(HdmiRxSsPtr->HdmiRxPtr, TRUE);

  /* Set stream up flag */
  HdmiRxSsPtr->IsStreamUp = (FALSE);
#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_STREAMDOWN, 0);
#endif
#ifdef USE_HDCP_RX
  // Push stream-down event to HDCP event queue
  XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_STREAMDOWN_EVT);
#endif

  // Check if user callback has been registered
  if (HdmiRxSsPtr->StreamDownCallback) {
      HdmiRxSsPtr->StreamDownCallback(HdmiRxSsPtr->StreamDownRef);
  }

}

/*****************************************************************************/
/**
*
* This function is called when the RX stream init .
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_StreamInitCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;
#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_STREAMINIT, 0);
#endif
  // Check if user callback has been registered
  if (HdmiRxSsPtr->StreamInitCallback) {
      HdmiRxSsPtr->StreamInitCallback(HdmiRxSsPtr->StreamInitRef);
  }

}

/*****************************************************************************/
/**
*
* This function is called when the RX stream is up.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_StreamUpCallback(void *CallbackRef)
{
  XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)CallbackRef;

  /* Clear link Status error counters */
  XV_HdmiRx_ClearLinkStatus(HdmiRxSsPtr->HdmiRxPtr);

  /* Set stream up flag */
  HdmiRxSsPtr->IsStreamUp = (TRUE);
#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_STREAMUP, 0);
#endif
#ifdef USE_HDCP_RX
  // Push stream-up event to HDCP event queue
  XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_STREAMUP_EVT);
#endif

  /* Configure Remapper according to HW setting and video format */
  XV_HdmiRxSs_ConfigBridgeMode(HdmiRxSsPtr);

  // Check if user callback has been registered
  if (HdmiRxSsPtr->StreamUpCallback) {
      HdmiRxSsPtr->StreamUpCallback(HdmiRxSsPtr->StreamUpRef);
  }

}

/*****************************************************************************/
/**
*
* This function installs an asynchronous callback function for the given
* HandlerType:
*
* <pre>
* HandlerType                     Callback Function Type
* -----------------------         --------------------------------------------------
* (XV_HDMIRXSS_HANDLER_CONNECT)             HpdCallback
* (XV_HDMIRXSS_HANDLER_VS)                  VsCallback
* (XV_HDMIRXSS_HANDLER_STREAM_DOWN)         StreamDownCallback
* (XV_HDMIRXSS_HANDLER_STREAM_UP)           StreamUpCallback
* (XV_HDMIRXSS_HANDLER_HDCP_AUTHENTICATED)
* (XV_HDMIRXSS_HANDLER_HDCP_UNAUTHENTICATED)
* (XV_HDMIRXSS_HANDLER_HDCP_AUTHENTICATION_REQUEST)
* (XV_HDMIRXSS_HANDLER_HDCP_STREAM_MANAGE_REQUEST)
* (XV_HDMIRXSS_HANDLER_HDCP_TOPOLOGY_UPDATE)
* </pre>
*
* @param    InstancePtr is a pointer to the HDMI RX Subsystem instance.
* @param    HandlerType specifies the type of handler.
* @param    CallbackFunc is the address of the callback function.
* @param    CallbackRef is a user data item that will be passed to the
*       callback function when it is invoked.
*
* @return
*       - XST_SUCCESS if callback function installed successfully.
*       - XST_INVALID_PARAM when HandlerType is invalid.
*
* @note     Invoking this function for a handler that already has been
*       installed replaces it with the new handler.
*
******************************************************************************/
int XV_HdmiRxSs_SetCallback(XV_HdmiRxSs *InstancePtr, u32 HandlerType,
    void *CallbackFunc, void *CallbackRef)
{
    u32 Status;

    /* Verify arguments. */
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(HandlerType >= (XV_HDMIRXSS_HANDLER_CONNECT));
    Xil_AssertNonvoid(CallbackFunc != NULL);
    Xil_AssertNonvoid(CallbackRef != NULL);

    /* Check for handler type */
    switch (HandlerType) {

        case (XV_HDMIRXSS_HANDLER_CONNECT):
            InstancePtr->ConnectCallback = (XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->ConnectRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        case (XV_HDMIRXSS_HANDLER_AUX):
            InstancePtr->AuxCallback = (XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->AuxRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        case (XV_HDMIRXSS_HANDLER_AUD):
            InstancePtr->AudCallback = (XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->AudRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        case (XV_HDMIRXSS_HANDLER_LNKSTA):
            InstancePtr->LnkStaCallback = (XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->LnkStaRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // Ddc
        case (XV_HDMIRXSS_HANDLER_DDC):
            InstancePtr->DdcCallback =(XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->DdcRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // Stream down
        case (XV_HDMIRXSS_HANDLER_STREAM_DOWN):
            InstancePtr->StreamDownCallback =(XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->StreamDownRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // Stream Init
        case (XV_HDMIRXSS_HANDLER_STREAM_INIT):
            InstancePtr->StreamInitCallback =(XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->StreamInitRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // Stream up
        case (XV_HDMIRXSS_HANDLER_STREAM_UP):
            InstancePtr->StreamUpCallback = (XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->StreamUpRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // HDCP
        case (XV_HDMIRXSS_HANDLER_HDCP):
            InstancePtr->HdcpCallback = (XV_HdmiRxSs_Callback)CallbackFunc;
            InstancePtr->HdcpRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // HDCP authenticated
        case (XV_HDMIRXSS_HANDLER_HDCP_AUTHENTICATED):
#ifdef XPAR_XHDCP_NUM_INSTANCES
            // Register HDCP 1.4 callbacks
            if (InstancePtr->Hdcp14Ptr) {
              XHdcp1x_SetCallback(InstancePtr->Hdcp14Ptr,
                                  XHDCP1X_HANDLER_AUTHENTICATED,
								  (void *)(XHdcp1x_Callback)CallbackFunc,
								  (void *)CallbackRef);
            }
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
            // Register HDCP 2.2 callbacks
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Rx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_RX_HANDLER_AUTHENTICATED,
									(void *)(XHdcp22_Rx_RunHandler)CallbackFunc,
									(void *)CallbackRef);
            }
#endif
            Status = (XST_SUCCESS);
            break;

        // HDCP authenticated
        case (XV_HDMIRXSS_HANDLER_HDCP_UNAUTHENTICATED):
#ifdef XPAR_XHDCP_NUM_INSTANCES
            // Register HDCP 1.4 callbacks
            if (InstancePtr->Hdcp14Ptr) {
              XHdcp1x_SetCallback(InstancePtr->Hdcp14Ptr,
                                XHDCP1X_HANDLER_UNAUTHENTICATED,
								(void *)(XHdcp1x_Callback)CallbackFunc,
								(void *)CallbackRef);
            }
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
            // Register HDCP 2.2 callbacks
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Rx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_RX_HANDLER_UNAUTHENTICATED,
									(void *)(XHdcp22_Rx_RunHandler)CallbackFunc,
									(void *)CallbackRef);
            }
#endif
            Status = (XST_SUCCESS);
            break;

        // HDCP authentication request
        case (XV_HDMIRXSS_HANDLER_HDCP_AUTHENTICATION_REQUEST):
#ifdef XPAR_XHDCP_NUM_INSTANCES
            // Register HDCP 1.4 callbacks
            if (InstancePtr->Hdcp14Ptr) {
        //Register the hdcp trigger downstream authentication callback
               XHdcp1x_SetCallBack(InstancePtr->Hdcp14Ptr,
                                   (XHdcp1x_HandlerType) XHDCP1X_RPTR_HDLR_TRIG_DOWNSTREAM_AUTH,
								   (void *) (XHdcp1x_Callback)CallbackFunc,
								   (void *) CallbackRef);
            }
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
            // Register HDCP 2.2 callbacks
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Rx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_RX_HANDLER_AUTHENTICATION_REQUEST,
									(void *)(XHdcp22_Rx_RunHandler)CallbackFunc,
									(void *)CallbackRef);
            }
#endif
            Status = (XST_SUCCESS);
            break;

        // HDCP stream management request
        case (XV_HDMIRXSS_HANDLER_HDCP_STREAM_MANAGE_REQUEST):
#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
            // Register HDCP 2.2 callbacks
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Rx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_RX_HANDLER_STREAM_MANAGE_REQUEST,
									(void *)(XHdcp22_Rx_RunHandler)CallbackFunc,
									(void *)CallbackRef);
            }
#endif
            Status = (XST_SUCCESS);
            break;

        // HDCP topology update request
        case (XV_HDMIRXSS_HANDLER_HDCP_TOPOLOGY_UPDATE):
#ifdef XPAR_XHDCP_NUM_INSTANCES
            // Register HDCP 1.4 callbacks
            if (InstancePtr->Hdcp14Ptr) {
              XHdcp1x_SetCallback(InstancePtr->Hdcp14Ptr,
                                  XHDCP1X_HANDLER_TOPOLOGY_UPDATE,
								  (void *)(XHdcp1x_Callback)CallbackFunc,
								  (void *)CallbackRef);
            }
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
            // Register HDCP 2.2 callbacks
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Rx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_RX_HANDLER_TOPOLOGY_UPDATE,
									(void *)(XHdcp22_Rx_RunHandler)CallbackFunc,
									(void *)CallbackRef);
            }
#endif
            Status = (XST_SUCCESS);
            break;

        // HDCP encryption status update
        case (XV_HDMIRXSS_HANDLER_HDCP_ENCRYPTION_UPDATE):
#ifdef XPAR_XHDCP_NUM_INSTANCES
            // Register HDCP 1.4 callbacks
            if (InstancePtr->Hdcp14Ptr) {
        XHdcp1x_SetCallback(InstancePtr->Hdcp14Ptr,
                                 XHDCP1X_HANDLER_ENCRYPTION_UPDATE,
								 (void *)(XHdcp1x_Callback)CallbackFunc,
								 (void *)CallbackRef);
            }
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
            // Register HDCP 2.2 callbacks
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Rx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_RX_HANDLER_ENCRYPTION_UPDATE,
									(void *)(XHdcp22_Rx_RunHandler)CallbackFunc,
									(void *)CallbackRef);
            }
#endif
            Status = (XST_SUCCESS);
            break;

        default:
            Status = (XST_INVALID_PARAM);
            break;
    }

    return Status;
}

/*****************************************************************************/
/**
*
* This function Sets the EDID parameters in the HDMI RX SS struct
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiRxSs_SetEdidParam(XV_HdmiRxSs *InstancePtr, u8 *EdidDataPtr,
                                                                u16 Length)
{
    InstancePtr->EdidPtr = EdidDataPtr;
    InstancePtr->EdidLength = Length;
}

/*****************************************************************************/
/**
*
* This function loads the default EDID to the HDMI RX
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiRxSs_LoadDefaultEdid(XV_HdmiRxSs *InstancePtr)
{
    u32 Status;

    // Load new EDID
    Status = XV_HdmiRx_DdcLoadEdid(InstancePtr->HdmiRxPtr, InstancePtr->EdidPtr,
            InstancePtr->EdidLength);
    if (Status == XST_SUCCESS) {
        xil_printf("\r\nSuccessfully loaded edid.\r\n");
    }
    else {
        xil_printf("\r\nError loading edid.\r\n");
    }
}

/*****************************************************************************/
/**
*
* This function loads the default EDID to the HDMI RX
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiRxSs_LoadEdid(XV_HdmiRxSs *InstancePtr, u8 *EdidDataPtr,
                                                                u16 Length)
{
    u32 Status;

    // Load new EDID
    Status = XV_HdmiRx_DdcLoadEdid(InstancePtr->HdmiRxPtr, EdidDataPtr, Length);

    if (Status == XST_SUCCESS) {
        xil_printf("\r\nSuccessfully loaded edid.\r\n");
    }
    else {
        xil_printf("\r\nError loading edid.\r\n");
    }
}

/*****************************************************************************/
/**
*
* This function sets the HPD on the HDMI RX.
*
* @param  Value is a flag used to set the HPD.
*   - TRUE drives HPD high
*   - FALSE drives HPD low
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiRxSs_SetHpd(XV_HdmiRxSs *InstancePtr, u8 Value)
{
  /* Drive HPD low */
  XV_HdmiRx_SetHpd(InstancePtr->HdmiRxPtr, Value);
}

/*****************************************************************************/
/**
*
* This function toggles the HPD on the HDMI RX.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiRxSs_ToggleHpd(XV_HdmiRxSs *InstancePtr)
{
  /* Drive HPD low */
  XV_HdmiRx_SetHpd(InstancePtr->HdmiRxPtr, (FALSE));

  /* Wait 500 ms */
  XV_HdmiRxSs_WaitUs(InstancePtr, 500000);

  /* Drive HPD high */
  XV_HdmiRx_SetHpd(InstancePtr->HdmiRxPtr, (TRUE));
}

/*****************************************************************************/
/**
*
* This function returns the pointer to HDMI RX SS Aux structure
*
* @param  InstancePtr pointer to XV_HdmiRxSs instance
*
* @return XVidC_VideoStream pointer
*
* @note   None.
*
******************************************************************************/
XV_HdmiRx_Aux *XV_HdmiRxSs_GetAuxiliary(XV_HdmiRxSs *InstancePtr)
{
    return (&InstancePtr->HdmiRxPtr->Aux);
}

/*****************************************************************************/
/**
*
* This function set HDMI RX susbsystem stream parameters
*
* @param  None.
*
* @return Calculated TMDS Clock
*
* @note   None.
*
******************************************************************************/
u32 XV_HdmiRxSs_SetStream(XV_HdmiRxSs *InstancePtr,
        u32 Clock, u32 LineRate)
{

    LineRate = LineRate;

#ifdef XV_HDMIRXSS_LOG_ENABLE
  /* Write log */
  XV_HdmiRxSs_LogWrite(InstancePtr, XV_HDMIRXSS_LOG_EVT_SETSTREAM, 0);
#endif
  /* Set stream */
  XV_HdmiRx_SetStream(InstancePtr->HdmiRxPtr, InstancePtr->Config.Ppc, Clock);

  /* In case the TMDS clock ratio is 1/40 */
  /* The reference clock must be compensated */
  if (XV_HdmiRx_GetTmdsClockRatio(InstancePtr->HdmiRxPtr)) {
      InstancePtr->HdmiRxPtr->Stream.RefClk =
                   InstancePtr->HdmiRxPtr->Stream.RefClk * 4;
  }

  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
*
* This function returns the pointer to HDMI RX SS video stream
*
* @param  InstancePtr pointer to XV_HdmiRxSs instance
*
* @return XVidC_VideoStream pointer
*
* @note   None.
*
******************************************************************************/
XVidC_VideoStream *XV_HdmiRxSs_GetVideoStream(XV_HdmiRxSs *InstancePtr)
{
    return (&InstancePtr->HdmiRxPtr->Stream.Video);
}

/*****************************************************************************/
/**
*
* This function returns the pointer to HDMI RX SS video Identification code
*
* @param  InstancePtr pointer to XV_HdmiRxSs instance
*
* @return VIC
*
* @note   None.
*
******************************************************************************/
u8 XV_HdmiRxSs_GetVideoIDCode(XV_HdmiRxSs *InstancePtr)
{
    return (InstancePtr->HdmiRxPtr->Stream.Vic);
}

/*****************************************************************************/
/**
*
* This function returns the pointer to HDMI RX SS video stream type
*
* @param  InstancePtr pointer to XV_HdmiRxSs instance
*
* @return Stream Type  1:HDMI 0:DVI
*
* @note   None.
*
******************************************************************************/
u8 XV_HdmiRxSs_GetVideoStreamType(XV_HdmiRxSs *InstancePtr)
{
    return (InstancePtr->HdmiRxPtr->Stream.IsHdmi);
}

/*****************************************************************************/
/**
*
* This function returns the pointer to HDMI RX SS video stream type
*
* @param  InstancePtr pointer to XV_HdmiRxSs instance
*
* @return Stream Type  1:IsScrambled 0: not Scrambled
*
* @note   None.
*
******************************************************************************/
u8 XV_HdmiRxSs_GetVideoStreamScramblingFlag(XV_HdmiRxSs *InstancePtr)
{
    return (InstancePtr->HdmiRxPtr->Stream.IsScrambled);
}

/*****************************************************************************/
/**
*
* This function returns the HDMI RX SS number of active audio channels
*
* @param  InstancePtr pointer to XV_HdmiRxSs instance
*
* @return Channels
*
* @note   None.
*
******************************************************************************/
u8 XV_HdmiRxSs_GetAudioChannels(XV_HdmiRxSs *InstancePtr)
{
    return (InstancePtr->AudioChannels);
}

/*****************************************************************************/
/**
*
* This function is called when HDMI RX SS TMDS clock changes
*
* @param  None.
*
* @return None
*
* @note   None.
*
******************************************************************************/
void XV_HdmiRxSs_RefClockChangeInit(XV_HdmiRxSs *InstancePtr)
{
  // Set TMDS Clock ratio
  InstancePtr->TMDSClockRatio =
    XV_HdmiRx_GetTmdsClockRatio(InstancePtr->HdmiRxPtr);
#ifdef XV_HDMIRXSS_LOG_ENABLE
  XV_HdmiRxSs_LogWrite(InstancePtr, XV_HDMIRXSS_LOG_EVT_REFCLOCKCHANGE, 0);
#endif
}

/*****************************************************************************/
/**
*
* This function prints the HDMI RX SS timing information
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_ReportTiming(XV_HdmiRxSs *InstancePtr)
{
    // Check is the RX stream is up
    if (XV_HdmiRx_IsStreamUp(InstancePtr->HdmiRxPtr)) {
        XV_HdmiRx_DebugInfo(InstancePtr->HdmiRxPtr);
        xil_printf("VIC: %0d\r\n", InstancePtr->HdmiRxPtr->Stream.Vic);
        xil_printf("Scrambled: %0d\r\n",
            (XV_HdmiRx_IsStreamScrambled(InstancePtr->HdmiRxPtr)));
        xil_printf("Audio channels: %0d\r\n",
            (XV_HdmiRx_GetAudioChannels(InstancePtr->HdmiRxPtr)));
    }

    // No stream
    else {
      xil_printf("No HDMI RX stream\r\n");
    }
}

/*****************************************************************************/
/**
*
* This function reports the link quality based on the link error counter
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_ReportLinkQuality(XV_HdmiRxSs *InstancePtr)
{
  u8 Channel;
  u32 Errors;

  for (Channel = 0; Channel < 3; Channel++)
  {
      Errors = XV_HdmiRx_GetLinkStatus(InstancePtr->HdmiRxPtr, Channel);

      xil_printf("Link quality channel %0d : ", Channel);

      if (Errors == 0) {
        xil_printf("excellent");
      }

      else if ((Errors > 0) && (Errors < 1024)) {
        xil_printf("good");
      }

      else if ((Errors > 1024) && (Errors < 16384)) {
        xil_printf("average");
      }

      else {
        xil_printf("bad");
      }

      xil_printf(" (%0d)\r\n", Errors);
  }

  /* Clear link error counters */
  XV_HdmiRx_ClearLinkStatus(InstancePtr->HdmiRxPtr);
}

/*****************************************************************************/
/**
*
* This function prints the HDMI RX SS audio information
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_ReportAudio(XV_HdmiRxSs *InstancePtr)
{
  xil_printf("Channels : %d\r\n",
  XV_HdmiRx_GetAudioChannels(InstancePtr->HdmiRxPtr));
  xil_printf("ARC CTS : %d\r\n", XV_HdmiRx_GetAcrCts(InstancePtr->HdmiRxPtr));
  xil_printf("ARC N   : %d\r\n", XV_HdmiRx_GetAcrN(InstancePtr->HdmiRxPtr));
}

/*****************************************************************************/
/**
*
* This function prints the HDMI RX SS audio information
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiRxSs_ReportInfoFrame(XV_HdmiRxSs *InstancePtr)
{
  xil_printf("RX header: %0x\r\n", InstancePtr->HdmiRxPtr->Aux.Header.Data);
}

/*****************************************************************************/
/**
*
* This function prints the HDMI RX SS subcore versions
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiRxSs_ReportSubcoreVersion(XV_HdmiRxSs *InstancePtr)
{
  u32 Data;

  if(InstancePtr->HdmiRxPtr)
  {
     Data = XV_HdmiRx_GetVersion(InstancePtr->HdmiRxPtr);
     xil_printf("  HDMI RX version : %02d.%02d (%04x)\r\n",
     ((Data >> 24) & 0xFF), ((Data >> 16) & 0xFF), (Data & 0xFFFF));
  }

#ifdef XPAR_XHDCP_NUM_INSTANCES
  if (InstancePtr->Hdcp14Ptr){
     Data = XHdcp1x_GetVersion(InstancePtr->Hdcp14Ptr);
     xil_printf("  HDCP 1.4 RX version : %02d.%02d (%04x)\r\n",
        ((Data >> 24) & 0xFF), ((Data >> 16) & 0xFF), (Data & 0xFFFF));
  }
#endif

}

/*****************************************************************************/
/**
*
* This function checks if the video stream is up.
*
* @param  None.
*
* @return
*   - TRUE if stream is up.
*   - FALSE if stream is down.
*
* @note   None.
*
******************************************************************************/
int XV_HdmiRxSs_IsStreamUp(XV_HdmiRxSs *InstancePtr)
{
  return (InstancePtr->IsStreamUp);
}

/*****************************************************************************/
/**
*
* This function checks if the interface is connected.
*
* @param  None.
*
* @return
*   - TRUE if interface is connected.
*   - FALSE if interface is not connected.
*
* @note   None.
*
******************************************************************************/
int XV_HdmiRxSs_IsStreamConnected(XV_HdmiRxSs *InstancePtr)
{
  return (InstancePtr->IsStreamConnected);
}

/******************************************************************************/
/**
*
* This function configures the Bridge for YUV420 functionality and repeater
*
* @param InstancePtr  Instance Pointer to the main data structure
* @param None
*
* @return
*
* @note
*   None.
*
******************************************************************************/
static void XV_HdmiRxSs_ConfigBridgeMode(XV_HdmiRxSs *InstancePtr) {

    XVidC_ColorFormat ColorFormat;
    XVidC_VideoMode VideoMode;

    XVidC_VideoStream *HdmiRxSsVidStreamPtr;
    HdmiRxSsVidStreamPtr = XV_HdmiRxSs_GetVideoStream(InstancePtr);

    ColorFormat = HdmiRxSsVidStreamPtr->ColorFormatId;
    VideoMode = HdmiRxSsVidStreamPtr->VmId;

    if (ColorFormat == XVIDC_CSF_YCRCB_420) {
        /*********************************************************
         * 420 Support
         *********************************************************/
         XV_HdmiRxSs_BridgePixelDrop(InstancePtr,FALSE);
         XV_HdmiRxSs_BridgeYuv420(InstancePtr,TRUE);
    }
    else {
        if ((VideoMode == XVIDC_VM_1440x480_60_I) ||
            (VideoMode == XVIDC_VM_1440x576_50_I) )
        {
            /*********************************************************
             * NTSC/PAL Support
             *********************************************************/
             XV_HdmiRxSs_BridgeYuv420(InstancePtr,FALSE);
             XV_HdmiRxSs_BridgePixelDrop(InstancePtr,TRUE);
        }
        else {
            XV_HdmiRxSs_BridgeYuv420(InstancePtr,FALSE);
            XV_HdmiRxSs_BridgePixelDrop(InstancePtr,FALSE);
        }
    }
}

/*****************************************************************************/
/**
* This function will set the default in HDF.
*
* @param    InstancePtr is a pointer to the XV_HdmiRxSs core instance.
* @param    Id is the XV_HdmiRxSs ID to operate on.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XV_HdmiRxSs_SetDefaultPpc(XV_HdmiRxSs *InstancePtr, u8 Id) {
}

/*****************************************************************************/
/**
* This function will set PPC specified by user.
*
* @param    InstancePtr is a pointer to the XV_HdmiRxSs core instance.
* @param    Id is the XV_HdmiRxSs ID to operate on.
* @param    Ppc is the PPC to be set.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XV_HdmiRxSs_SetPpc(XV_HdmiRxSs *InstancePtr, u8 Id, u8 Ppc) {
    InstancePtr->Config.Ppc = (XVidC_PixelsPerClock) Ppc;
    Id = Id; //squash unused variable compiler warning
}

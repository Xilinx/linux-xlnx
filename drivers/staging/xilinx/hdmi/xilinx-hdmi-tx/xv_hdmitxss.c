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
* @file xv_hdmitxss.c
*
* This is main code of Xilinx HDMI Transmitter Subsystem device driver.
* Please see xv_hdmitxss.h for more details of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00         10/07/15 Initial release.
* 1.10  MG     17/12/16 Fixed issue in function SetAudioChannels
*                       Updated function XV_HdmiTxSs_SendAuxInfoframe
* 1.2   yh     12/01/16 Check vtc existance before configuring it
* 1.3   yh     15/01/16 Add 3D Support
* 1.4   yh     20/01/16 Added remapper support
* 1.5   yh     01/02/16 Added set_ppc api
* 1.6   yh     01/02/16 Removed xil_print "Cable (dis)connected"
* 1.7   yh     15/02/16 Added default value to XV_HdmiTxSs_ConfigRemapper
* 1.8   MG     03/02/16 Added HDCP support
* 1.9   MG     09/03/16 Added XV_HdmiTxSs_SetHdmiMode and XV_HdmiTxSs_SetDviMode
*                       Removed reduced blanking support
* 1.10  MH     03/15/16 Moved HDCP 2.2 reset from stream up/down callback to
*                       connect callback
* 1.11  YH     18/03/16 Add XV_HdmiTxSs_SendGenericAuxInfoframe function
* 1.12  MH     23/04/16 1. HDCP 1.x driver now uses AXI timer 4.1, so updated
*                       to use AXI Timer config structure to determine timer
*                       clock frequency
*                       2. HDCP 1.x driver has fixed the problem where the
*                       reset for the receiver causes the entire DDC peripheral
*                       to get reset. Based on this change the driver has been
*                       updated to use XV_HdmiTxSs_HdcpReset and
*                       XV_HdmiTxSs_HdcpReset functions directly.
*                       3. Updated XV_HdmiTxSs_HdcpEnable and
*                       XV_HdmiTxSs_HdcpEnable functions to ensure that
*                       HDCP 1.4 and 2.2 are mutually exclusive.
*                       This fixes the problem where HDCP 1.4 and 2.2
*                       state machines are running simultaneously.
* 1.13   MH    23/06/16 Added HDCP repeater support.
* 1.14   YH    18/07/16 1. Replace xil_print with xdbg_printf.
*                       2. XV_HdmiTx_VSIF VSIF global variable local to
*                        XV_HdmiTxSs_SendVSInfoframe
*                       3. Replace MB_Sleep() with usleep
*                       4. Remove checking VideoMode < XVIDC_VM_NUM_SUPPORTED in
*                       XV_HdmiTxSs_SetStream to support customized video format
* 1.15   YH    25/07/16 Used UINTPTR instead of u32 for BaseAddress
*                       XV_HdmiTxSs_CfgInitialize
* 1.16   YH    04/08/16 Remove unused functions
*                       XV_HdmiTxSs_GetSubSysStruct
* 1.17   MH    08/08/16 Updates to optimize out HDCP when excluded.
* 1.18   YH    17/08/16 Remove sleep in XV_HdmiTxSs_ResetRemapper
*                       Added Event Log
*                       Combine Report function into one ReportInfo
* 1.19   YH    27/08/16 Remove unused functions XV_HdmiTxSs_SetUserTimerHandler
*                       XV_HdmiTxSs_WaitUs
* 1.20   MH    08/10/16 Update function call sequence in
*                       XV_HdmiTxSs_StreamUpCallback
*
* 1.1x   mmo   04/11/16 Updated the XV_HdmiTxSs_SetAudioChannels API which
*                       currently calls XV_HdmiTx_SetAudioChannels driver,
*                       which sets the Audio Channels
*                       accordingly. This fixed is made during v1.2 (2016.1)
* 1.21  YH     14/11/16 Added API to enable/disable YUV420/Pixel Repeat Mode
*                       for video bridge
* 1.22  YH     14/11/16 Remove Remapper APIs as remapper feature is moved to
*                       video bridge and controlled by HDMI core
* 1.23  mmo    03/01/17 Move HDCP Related API to xv_hdmitxss_hdcp.c
*                       Remove inclusion of the xenv.h and sleep.h as it not
*                           used
*                       Replaced "print" with "xil_printf"
*                       Replace Carriage Return (\r) and Line Feed (\n) order,\
*                           where the Carriage Return + Line Feed order is used.
* 1.24  mmo    02/03/17 Added XV_HdmiTxSs_ReadEdidSegment API for Multiple
*                             Segment Support and HDMI Compliance Test
*                       Updated the XV_HdmiTxSs_ShowEdid API to have support
*                             multiple EDID.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xv_hdmitxss.h"
#include "xv_hdmitxss_coreinit.h"

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
#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  XHdcp22_Tx  Hdcp22;
#endif
  XV_HdmiTx HdmiTx;
  XVtc Vtc;
}XV_HdmiTxSs_SubCores;

/**************************** Local Global ***********************************/
XV_HdmiTxSs_SubCores XV_HdmiTxSs_SubCoreRepo[XPAR_XV_HDMITXSS_NUM_INSTANCES];
                /**< Define Driver instance of all sub-core
                                    included in the design */

/************************** Function Prototypes ******************************/
static void XV_HdmiTxSs_GetIncludedSubcores(XV_HdmiTxSs *HdmiTxSsPtr,
                                            u16 DevId);
static int XV_HdmiTxSs_RegisterSubsysCallbacks(XV_HdmiTxSs *InstancePtr);
static int XV_HdmiTxSs_VtcSetup(XVtc *XVtcPtr, XV_HdmiTx *HdmiTxPtr);
static void XV_HdmiTxSs_SendAviInfoframe(XV_HdmiTx *HdmiTxPtr);
static void XV_HdmiTxSs_SendGeneralControlPacket(XV_HdmiTx *HdmiTxPtr);
static void XV_HdmiTxSs_SendVSInfoframe(XV_HdmiTx *HdmiTxPtr);
static void XV_HdmiTxSs_ConnectCallback(void *CallbackRef);
static void XV_HdmiTxSs_ToggleCallback(void *CallbackRef);
static void XV_HdmiTxSs_VsCallback(void *CallbackRef);
static void XV_HdmiTxSs_StreamUpCallback(void *CallbackRef);
static void XV_HdmiTxSs_StreamDownCallback(void *CallbackRef);

static void XV_HdmiTxSs_ReportCoreInfo(XV_HdmiTxSs *InstancePtr);
static void XV_HdmiTxSs_ReportTiming(XV_HdmiTxSs *InstancePtr);
static void XV_HdmiTxSs_ReportSubcoreVersion(XV_HdmiTxSs *InstancePtr);

static void XV_HdmiTxSs_ConfigBridgeMode(XV_HdmiTxSs *InstancePtr);

/***************** Macros (Inline Functions) Definitions *********************/
/*****************************************************************************/
/**
* This macro selects the bridge YUV420 mode
*
* @param  InstancePtr is a pointer to the HDMI TX Subsystem
*
*****************************************************************************/
#define XV_HdmiTxSs_BridgeYuv420(InstancePtr, Enable) \
{ \
    XV_HdmiTx_Bridge_yuv420(InstancePtr->HdmiTxPtr, Enable); \
}

/*****************************************************************************/
/**
* This macro selects the bridge pixel repeat mode
*
* @param  InstancePtr is a pointer to the HDMI TX Subsystem
*
*****************************************************************************/
#define XV_HdmiTxSs_BridgePixelRepeat(InstancePtr, Enable) \
{ \
    XV_HdmiTx_Bridge_pixel(InstancePtr->HdmiTxPtr, Enable); \
}
/************************** Function Definition ******************************/

/*****************************************************************************/
/**
 * This function sets the core into HDMI mode
 *
 * @param  InstancePtr is a pointer to the HDMI TX Subsystem
 *
 *****************************************************************************/
void XV_HdmiTxSS_SetHdmiMode(XV_HdmiTxSs *InstancePtr)
{
    XV_HdmiTx_SetHdmiMode(InstancePtr->HdmiTxPtr);
}

/*****************************************************************************/
/**
 * This function sets the core into DVI mode
 *
 * @param  InstancePtr is a pointer to the HDMI TX Subsystem
 *
 *****************************************************************************/
void XV_HdmiTxSS_SetDviMode(XV_HdmiTxSs *InstancePtr)
{
    XV_HdmiTx_SetDviMode(InstancePtr->HdmiTxPtr);
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
static void XV_HdmiTxSs_ReportCoreInfo(XV_HdmiTxSs *InstancePtr)
{
  Xil_AssertVoid(InstancePtr != NULL);

  xil_printf("\r\n  ->HDMI TX Subsystem Cores\r\n");

  /* Report all the included cores in the subsystem instance */
  if (InstancePtr->HdmiTxPtr) {
    xil_printf("    : HDMI TX \r\n");
  }

  if (InstancePtr->VtcPtr) {
    xil_printf("    : VTC Core \r\n");
  }

#ifdef XPAR_XHDCP_NUM_INSTANCES
  if (InstancePtr->Hdcp14Ptr) {
    xil_printf("    : HDCP 1.4 TX \r\n");
  }

  if (InstancePtr->HdcpTimerPtr) {
    xil_printf("    : HDCP: AXIS Timer\r\n");
  }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  if (InstancePtr->Hdcp22Ptr) {
    xil_printf("    : HDCP 2.2 TX \r\n");
  }
#endif
}

/*****************************************************************************/
/**
 * This function calls the interrupt handler for HDMI TX
 *
 * @param  InstancePtr is a pointer to the HDMI TX Subsystem
 *
 *****************************************************************************/
void XV_HdmiTxSS_HdmiTxIntrHandler(XV_HdmiTxSs *InstancePtr)
{
    XV_HdmiTx_IntrHandler(InstancePtr->HdmiTxPtr);
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
static int XV_HdmiTxSs_RegisterSubsysCallbacks(XV_HdmiTxSs *InstancePtr)
{
  XV_HdmiTxSs *HdmiTxSsPtr = InstancePtr;

  /** Register HDMI callbacks */
  if (HdmiTxSsPtr->HdmiTxPtr) {
    /*
     * Register call back for Tx Core Interrupts.
     */
    XV_HdmiTx_SetCallback(HdmiTxSsPtr->HdmiTxPtr,
                          XV_HDMITX_HANDLER_CONNECT,
						  (void *)XV_HdmiTxSs_ConnectCallback,
						  (void *)InstancePtr);

    XV_HdmiTx_SetCallback(HdmiTxSsPtr->HdmiTxPtr,
                          XV_HDMITX_HANDLER_TOGGLE,
						  (void *)XV_HdmiTxSs_ToggleCallback,
						  (void *)InstancePtr);

    XV_HdmiTx_SetCallback(HdmiTxSsPtr->HdmiTxPtr,
                          XV_HDMITX_HANDLER_VS,
						  (void *)XV_HdmiTxSs_VsCallback,
						  (void *)InstancePtr);

    XV_HdmiTx_SetCallback(HdmiTxSsPtr->HdmiTxPtr,
                          XV_HDMITX_HANDLER_STREAM_UP,
						  (void *)XV_HdmiTxSs_StreamUpCallback,
						  (void *)InstancePtr);

    XV_HdmiTx_SetCallback(HdmiTxSsPtr->HdmiTxPtr,
                          XV_HDMITX_HANDLER_STREAM_DOWN,
						  (void *)XV_HdmiTxSs_StreamDownCallback,
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
* @param  HdmiTxSsPtr is a pointer to the Subsystem instance to be worked on.
*
* @return None
*
******************************************************************************/
static void XV_HdmiTxSs_GetIncludedSubcores(XV_HdmiTxSs *HdmiTxSsPtr, u16 DevId)
{
  HdmiTxSsPtr->HdmiTxPtr     = ((HdmiTxSsPtr->Config.HdmiTx.IsPresent)    \
                        ? (&XV_HdmiTxSs_SubCoreRepo[DevId].HdmiTx) : NULL);
  HdmiTxSsPtr->VtcPtr        = ((HdmiTxSsPtr->Config.Vtc.IsPresent)  \
                        ? (&XV_HdmiTxSs_SubCoreRepo[DevId].Vtc) : NULL);
#ifdef XPAR_XHDCP_NUM_INSTANCES
  // HDCP 1.4
  HdmiTxSsPtr->Hdcp14Ptr       = ((HdmiTxSsPtr->Config.Hdcp14.IsPresent) \
                        ? (&XV_HdmiTxSs_SubCoreRepo[DevId].Hdcp14) : NULL);
  HdmiTxSsPtr->HdcpTimerPtr  = ((HdmiTxSsPtr->Config.HdcpTimer.IsPresent) \
                        ? (&XV_HdmiTxSs_SubCoreRepo[DevId].HdcpTimer) : NULL);
#endif
#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  // HDCP 2.2
  HdmiTxSsPtr->Hdcp22Ptr       = ((HdmiTxSsPtr->Config.Hdcp22.IsPresent) \
                        ? (&XV_HdmiTxSs_SubCoreRepo[DevId].Hdcp22) : NULL);
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
int XV_HdmiTxSs_CfgInitialize(XV_HdmiTxSs *InstancePtr,
                              XV_HdmiTxSs_Config *CfgPtr,
                              UINTPTR EffectiveAddr)
{
  XV_HdmiTxSs *HdmiTxSsPtr = InstancePtr;

  /* Verify arguments */
  Xil_AssertNonvoid(HdmiTxSsPtr != NULL);
  Xil_AssertNonvoid(CfgPtr != NULL);
  Xil_AssertNonvoid(EffectiveAddr != (UINTPTR)NULL);

  /* Setup the instance */
  memcpy((void *)&(HdmiTxSsPtr->Config), (const void *)CfgPtr,
    sizeof(XV_HdmiTxSs_Config));
  HdmiTxSsPtr->Config.BaseAddress = EffectiveAddr;

  /* Determine sub-cores included in the provided instance of subsystem */
  XV_HdmiTxSs_GetIncludedSubcores(HdmiTxSsPtr, CfgPtr->DeviceId);

  /* Initialize all included sub_cores */

  // HDCP 1.4
#ifdef XPAR_XHDCP_NUM_INSTANCES
  if (HdmiTxSsPtr->HdcpTimerPtr) {
    if (XV_HdmiTxSs_SubcoreInitHdcpTimer(HdmiTxSsPtr) != XST_SUCCESS){
      return(XST_FAILURE);
    }
  }

  if (HdmiTxSsPtr->Hdcp14Ptr) {
    if (XV_HdmiTxSs_SubcoreInitHdcp14(HdmiTxSsPtr) != XST_SUCCESS){
      return(XST_FAILURE);
    }
  }
#endif

  if (HdmiTxSsPtr->HdmiTxPtr) {
    if (XV_HdmiTxSs_SubcoreInitHdmiTx(HdmiTxSsPtr) != XST_SUCCESS) {
      return(XST_FAILURE);
    }
    XV_HdmiTx_SetAxiClkFreq(HdmiTxSsPtr->HdmiTxPtr,
                            HdmiTxSsPtr->Config.AxiLiteClkFreq);
  }

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  // HDCP 2.2
  if (HdmiTxSsPtr->Hdcp22Ptr) {
    if (XV_HdmiTxSs_SubcoreInitHdcp22(HdmiTxSsPtr) != XST_SUCCESS){
      return(XST_FAILURE);
    }
  }
#endif

  if (HdmiTxSsPtr->VtcPtr) {
    if (XV_HdmiTxSs_SubcoreInitVtc(HdmiTxSsPtr) != XST_SUCCESS) {
      return(XST_FAILURE);
    }
  }

  /* Register Callbacks */
  XV_HdmiTxSs_RegisterSubsysCallbacks(HdmiTxSsPtr);

  /* Set default HDCP protocol */
  HdmiTxSsPtr->HdcpProtocol = XV_HDMITXSS_HDCP_NONE;

  /* HDCP ready flag */

#ifdef USE_HDCP_TX
  /* Default value */
  HdmiTxSsPtr->HdcpIsReady = (FALSE);
#endif

#if defined(XPAR_XHDCP_NUM_INSTANCES) && defined(XPAR_XHDCP22_TX_NUM_INSTANCES)
  /* HDCP is ready when both HDCP cores are instantiated and both keys
     are loaded */
  if (HdmiTxSsPtr->Hdcp14Ptr && HdmiTxSsPtr->Hdcp22Ptr &&
      HdmiTxSsPtr->Hdcp22Lc128Ptr && HdmiTxSsPtr->Hdcp22SrmPtr &&
      HdmiTxSsPtr->Hdcp14KeyPtr) {
    HdmiTxSsPtr->HdcpIsReady = (TRUE);
  }
#endif

#if defined(XPAR_XHDCP_NUM_INSTANCES)
  /* HDCP is ready when only the HDCP 1.4 core is instantiated and the key
     is loaded */
  if (!HdmiTxSsPtr->HdcpIsReady && HdmiTxSsPtr->Hdcp14Ptr &&
       HdmiTxSsPtr->Hdcp14KeyPtr) {
    HdmiTxSsPtr->HdcpIsReady = (TRUE);
  }
#endif

#if defined(XPAR_XHDCP22_TX_NUM_INSTANCES)
  /* HDCP is ready when only the HDCP 2.2 core is instantiated and the key
     is loaded */
  if (!HdmiTxSsPtr->HdcpIsReady && HdmiTxSsPtr->Hdcp22Ptr &&
       HdmiTxSsPtr->Hdcp22Lc128Ptr &&
      HdmiTxSsPtr->Hdcp22SrmPtr) {
    HdmiTxSsPtr->HdcpIsReady = (TRUE);
  }
#endif

  /* Set the flag to indicate the subsystem is ready */
  XV_HdmiTxSs_Reset(HdmiTxSsPtr);
  HdmiTxSsPtr->IsReady = XIL_COMPONENT_IS_READY;

  return(XST_SUCCESS);
}

/****************************************************************************/
/**
* This function starts the HDMI TX subsystem including all sub-cores that are
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
void XV_HdmiTxSs_Start(XV_HdmiTxSs *InstancePtr)
{
  Xil_AssertVoid(InstancePtr != NULL);
#ifdef XV_HDMITXSS_LOG_ENABLE
  XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_START, 0);
#endif
}

/*****************************************************************************/
/**
* This function stops the HDMI TX subsystem including all sub-cores
* Stop the video pipe starting from front to back
*
* @param  InstancePtr is a pointer to the Subsystem instance to be worked on.
*
* @return None
*
******************************************************************************/
void XV_HdmiTxSs_Stop(XV_HdmiTxSs *InstancePtr)
{
  Xil_AssertVoid(InstancePtr != NULL);
#ifdef XV_HDMITXSS_LOG_ENABLE
  XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_STOP, 0);
#endif
  if (InstancePtr->VtcPtr) {
    /* Disable VTC */
    XVtc_DisableGenerator(InstancePtr->VtcPtr);
  }
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
void XV_HdmiTxSs_Reset(XV_HdmiTxSs *InstancePtr)
{
  Xil_AssertVoid(InstancePtr != NULL);
#ifdef XV_HDMITXSS_LOG_ENABLE
  XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_RESET, 0);
#endif
  /* Assert TX reset */
  XV_HdmiTx_Reset(InstancePtr->HdmiTxPtr, TRUE);

  /* Release TX reset */
  XV_HdmiTx_Reset(InstancePtr->HdmiTxPtr, FALSE);
}

/*****************************************************************************/
/**
*
* This function configures Video Timing Controller (VTC).
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static int XV_HdmiTxSs_VtcSetup(XVtc *XVtcPtr, XV_HdmiTx *HdmiTxPtr)
{
  /* Polarity configuration */
  XVtc_Polarity Polarity;
  XVtc_SourceSelect SourceSelect;
  XVtc_Timing VideoTiming;
  u32 HdmiTx_Hblank;
  u32 Vtc_Hblank;

  /* Disable Generator */
  XVtc_Reset(XVtcPtr);
  XVtc_DisableGenerator(XVtcPtr);
  XVtc_Disable(XVtcPtr);

  /* Set up source select */
  memset((void *)&SourceSelect, 0, sizeof(SourceSelect));

  /* 1 = Generator registers, 0 = Detector registers */
  SourceSelect.VChromaSrc = 1;
  SourceSelect.VActiveSrc = 1;
  SourceSelect.VBackPorchSrc = 1;
  SourceSelect.VSyncSrc = 1;
  SourceSelect.VFrontPorchSrc = 1;
  SourceSelect.VTotalSrc = 1;
  SourceSelect.HActiveSrc = 1;
  SourceSelect.HBackPorchSrc = 1;
  SourceSelect.HSyncSrc = 1;
  SourceSelect.HFrontPorchSrc = 1;
  SourceSelect.HTotalSrc = 1;

  XVtc_SetSource(XVtcPtr, &SourceSelect);

  VideoTiming.HActiveVideo = HdmiTxPtr->Stream.Video.Timing.HActive;
  VideoTiming.HFrontPorch = HdmiTxPtr->Stream.Video.Timing.HFrontPorch;
  VideoTiming.HSyncWidth = HdmiTxPtr->Stream.Video.Timing.HSyncWidth;
  VideoTiming.HBackPorch = HdmiTxPtr->Stream.Video.Timing.HBackPorch;
  VideoTiming.HSyncPolarity = HdmiTxPtr->Stream.Video.Timing.HSyncPolarity;

  /* Vertical Timing */
  VideoTiming.VActiveVideo = HdmiTxPtr->Stream.Video.Timing.VActive;

  VideoTiming.V0FrontPorch = HdmiTxPtr->Stream.Video.Timing.F0PVFrontPorch;
  VideoTiming.V0BackPorch = HdmiTxPtr->Stream.Video.Timing.F0PVBackPorch;
  VideoTiming.V0SyncWidth = HdmiTxPtr->Stream.Video.Timing.F0PVSyncWidth;

  VideoTiming.V1FrontPorch = HdmiTxPtr->Stream.Video.Timing.F1VFrontPorch;
  VideoTiming.V1SyncWidth = HdmiTxPtr->Stream.Video.Timing.F1VSyncWidth;
  VideoTiming.V1BackPorch = HdmiTxPtr->Stream.Video.Timing.F1VBackPorch;

  VideoTiming.VSyncPolarity = HdmiTxPtr->Stream.Video.Timing.VSyncPolarity;

  VideoTiming.Interlaced = HdmiTxPtr->Stream.Video.IsInterlaced;

    /* 4 pixels per clock */
    if (HdmiTxPtr->Stream.Video.PixPerClk == XVIDC_PPC_4) {
      VideoTiming.HActiveVideo = VideoTiming.HActiveVideo/4;
      VideoTiming.HFrontPorch = VideoTiming.HFrontPorch/4;
      VideoTiming.HBackPorch = VideoTiming.HBackPorch/4;
      VideoTiming.HSyncWidth = VideoTiming.HSyncWidth/4;
    }

    /* 2 pixels per clock */
    else if (HdmiTxPtr->Stream.Video.PixPerClk == XVIDC_PPC_2) {
      VideoTiming.HActiveVideo = VideoTiming.HActiveVideo/2;
      VideoTiming.HFrontPorch = VideoTiming.HFrontPorch/2;
      VideoTiming.HBackPorch = VideoTiming.HBackPorch/2;
      VideoTiming.HSyncWidth = VideoTiming.HSyncWidth/2;
    }

    /* 1 pixels per clock */
    else {
      VideoTiming.HActiveVideo = VideoTiming.HActiveVideo;
      VideoTiming.HFrontPorch = VideoTiming.HFrontPorch;
      VideoTiming.HBackPorch = VideoTiming.HBackPorch;
      VideoTiming.HSyncWidth = VideoTiming.HSyncWidth;
    }

    /* For YUV420 the line width is double there for double the blanking */
    if (HdmiTxPtr->Stream.Video.ColorFormatId == XVIDC_CSF_YCRCB_420) {
      VideoTiming.HActiveVideo = VideoTiming.HActiveVideo/2;
      VideoTiming.HFrontPorch = VideoTiming.HFrontPorch/2;
      VideoTiming.HBackPorch = VideoTiming.HBackPorch/2;
      VideoTiming.HSyncWidth = VideoTiming.HSyncWidth/2;
    }

/** When compensating the vtc horizontal timing parameters for the pixel mode
* (quad or dual) rounding errors might be introduced (due to the divide)
* If this happens, the vtc total horizontal blanking is less than the hdmi tx
* horizontal blanking.
* As a result the hdmi tx vid out bridge is not able to lock to
* the incoming video stream.
* This process will check the horizontal blank timing and compensate
* for this condition.
* Calculate hdmi tx horizontal blanking */

  HdmiTx_Hblank = HdmiTxPtr->Stream.Video.Timing.HFrontPorch +
    HdmiTxPtr->Stream.Video.Timing.HSyncWidth +
    HdmiTxPtr->Stream.Video.Timing.HBackPorch;

  do {
    // Calculate vtc horizontal blanking
    Vtc_Hblank = VideoTiming.HFrontPorch +
        VideoTiming.HBackPorch +
        VideoTiming.HSyncWidth;

    // Quad pixel mode
    if (HdmiTxPtr->Stream.Video.PixPerClk == XVIDC_PPC_4) {
      Vtc_Hblank *= 4;
    }

    // Dual pixel mode
    else if (HdmiTxPtr->Stream.Video.PixPerClk == XVIDC_PPC_2) {
      Vtc_Hblank *= 2;
    }

    // Single pixel mode
    else {
      //Vtc_Hblank *= 1;
    }

    /* For YUV420 the line width is double there for double the blanking */
    if (HdmiTxPtr->Stream.Video.ColorFormatId == XVIDC_CSF_YCRCB_420) {
        Vtc_Hblank *= 2;
    }

    // If the horizontal total blanking differs,
    // then increment the Vtc horizontal front porch.
    if (Vtc_Hblank != HdmiTx_Hblank) {
      VideoTiming.HFrontPorch++;
    }

  } while (Vtc_Hblank < HdmiTx_Hblank);

  if (Vtc_Hblank != HdmiTx_Hblank) {
      xdbg_printf(XDBG_DEBUG_GENERAL,
                  "Error! Current format with total Hblank (%d) cannot \r\n",
                  HdmiTx_Hblank);
      xdbg_printf(XDBG_DEBUG_GENERAL,
                  "       be transmitted with pixels per clock = %d\r\n",
                  HdmiTxPtr->Stream.Video.PixPerClk);
      return (XST_FAILURE);
  }

  XVtc_SetGeneratorTiming(XVtcPtr, &VideoTiming);

  /* Set up Polarity of all outputs */
  memset((void *)&Polarity, 0, sizeof(XVtc_Polarity));
  Polarity.ActiveChromaPol = 1;
  Polarity.ActiveVideoPol = 1;

  //Polarity.FieldIdPol = 0;
  if (VideoTiming.Interlaced) {
    Polarity.FieldIdPol = 1;
  }
  else {
    Polarity.FieldIdPol = 0;
  }

  Polarity.VBlankPol = VideoTiming.VSyncPolarity;
  Polarity.VSyncPol = VideoTiming.VSyncPolarity;
  Polarity.HBlankPol = VideoTiming.HSyncPolarity;
  Polarity.HSyncPol = VideoTiming.HSyncPolarity;

  XVtc_SetPolarity(XVtcPtr, &Polarity);

  /* VTC driver does not take care of the setting of the VTC in
   * interlaced operation. As a work around the register
   * is set manually */
  if (VideoTiming.Interlaced) {
    /* Interlaced mode */
    XVtc_WriteReg(XVtcPtr->Config.BaseAddress, 0x68, 0x42);
  }
  else {
    /* Progressive mode */
    XVtc_WriteReg(XVtcPtr->Config.BaseAddress, 0x68, 0x2);
  }

  /* Enable generator module */
  XVtc_Enable(XVtcPtr);
  XVtc_EnableGenerator(XVtcPtr);
  XVtc_RegUpdateEnable(XVtcPtr);

  return (XST_SUCCESS);
}

/*****************************************************************************/
/**
*
* This function is called when a TX connect event has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_ConnectCallback(void *CallbackRef)
{
  XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)CallbackRef;

  /* Is the cable connected */
  if (XV_HdmiTx_IsStreamConnected(HdmiTxSsPtr->HdmiTxPtr)) {
#ifdef XV_HDMITXSS_LOG_ENABLE
    XV_HdmiTxSs_LogWrite(HdmiTxSsPtr, XV_HDMITXSS_LOG_EVT_CONNECT, 0);
#endif
    /* Set stream connected flag */
    HdmiTxSsPtr->IsStreamConnected = (TRUE);

#ifdef USE_HDCP_TX
    /* Push connect event to the HDCP event queue */
    XV_HdmiTxSs_HdcpPushEvent(HdmiTxSsPtr, XV_HDMITXSS_HDCP_CONNECT_EVT);
#endif
  }

  /* TX cable is disconnected */
  else {
#ifdef XV_HDMITXSS_LOG_ENABLE
    XV_HdmiTxSs_LogWrite(HdmiTxSsPtr, XV_HDMITXSS_LOG_EVT_DISCONNECT, 0);
#endif
    /* Set stream connected flag */
    HdmiTxSsPtr->IsStreamConnected = (FALSE);

#ifdef USE_HDCP_TX
    /* Push disconnect event to the HDCP event queue */
    XV_HdmiTxSs_HdcpPushEvent(HdmiTxSsPtr, XV_HDMITXSS_HDCP_DISCONNECT_EVT);
#endif
  }

  /* Check if user callback has been registered */
  if (HdmiTxSsPtr->ConnectCallback) {
    HdmiTxSsPtr->ConnectCallback(HdmiTxSsPtr->ConnectRef);
  }
}

/*****************************************************************************/
/**
*
* This function is called when a TX toggle event has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_ToggleCallback(void *CallbackRef)
{
  XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)CallbackRef;

  /* Set toggle flag */
  HdmiTxSsPtr->IsStreamToggled = TRUE;
#ifdef XV_HDMITXSS_LOG_ENABLE
  XV_HdmiTxSs_LogWrite(HdmiTxSsPtr, XV_HDMITXSS_LOG_EVT_TOGGLE, 0);
#endif
  /* Check if user callback has been registered */
  if (HdmiTxSsPtr->ToggleCallback) {
    HdmiTxSsPtr->ToggleCallback(HdmiTxSsPtr->ToggleRef);
  }

  /* Clear toggle flag */
  HdmiTxSsPtr->IsStreamToggled = FALSE;
}

/*****************************************************************************/
/**
*
* This function is called when a TX vsync has occurred.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_VsCallback(void *CallbackRef)
{
  XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)CallbackRef;

  // AVI infoframe
  XV_HdmiTxSs_SendAviInfoframe(HdmiTxSsPtr->HdmiTxPtr);

  // General control packet
  XV_HdmiTxSs_SendGeneralControlPacket(HdmiTxSsPtr->HdmiTxPtr);

  // Vendor-Specific InfoFrame
  XV_HdmiTxSs_SendVSInfoframe(HdmiTxSsPtr->HdmiTxPtr);

  // Check if user callback has been registered
  if (HdmiTxSsPtr->VsCallback) {
      HdmiTxSsPtr->VsCallback(HdmiTxSsPtr->VsRef);
  }
}

/*****************************************************************************/
/**
*
* This function sends AVI info frames.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_SendAviInfoframe(XV_HdmiTx *HdmiTx)
{
  u8 Index;
  u8 Data;
  u8 Crc;

  /* Header, Packet type*/
  HdmiTx->Aux.Header.Byte[0] = 0x82;

  /* Version */
  HdmiTx->Aux.Header.Byte[1] = 0x02;

  /* Length */
  HdmiTx->Aux.Header.Byte[2] = 13;

  /* Checksum (this will be calculated by the HDMI TX IP) */
  HdmiTx->Aux.Header.Byte[3] = 0;

  /* Data */
  switch (HdmiTx->Stream.Video.ColorFormatId) {
    case XVIDC_CSF_YCRCB_422:
      Data = 1 << 5;
      break;

    case XVIDC_CSF_YCRCB_444:
      Data = 2 << 5;
      break;

    case XVIDC_CSF_YCRCB_420:
      Data = 3 << 5;
      break;

    default:
      Data = 0;
      break;
  }

  HdmiTx->Aux.Data.Byte[1] = Data;

  HdmiTx->Aux.Data.Byte[2] = 0;
  HdmiTx->Aux.Data.Byte[3] = 0;

  if (!XVidC_IsStream3D(&HdmiTx->Stream.Video) &&
      (HdmiTx->Stream.Video.VmId == XVIDC_VM_3840x2160_24_P ||
       HdmiTx->Stream.Video.VmId == XVIDC_VM_3840x2160_25_P ||
       HdmiTx->Stream.Video.VmId == XVIDC_VM_3840x2160_30_P ||
       HdmiTx->Stream.Video.VmId == XVIDC_VM_4096x2160_24_P)) {
    HdmiTx->Aux.Data.Byte[4] = 0;
  }
  else {
      HdmiTx->Aux.Data.Byte[4] = HdmiTx->Stream.Vic;
  }

  for (Index = 5; Index < 32; Index++) {
    HdmiTx->Aux.Data.Byte[Index] = 0;
  }

  /* Calculate AVI infoframe checksum */
  Crc = 0;

  /* Header */
  for (Index = 0; Index < 3; Index++) {
    Crc += HdmiTx->Aux.Header.Byte[Index];
  }

  /* Data */
  for (Index = 1; Index < 5; Index++) {
    Crc += HdmiTx->Aux.Data.Byte[Index];
  }

  Crc = 256 - Crc;

  HdmiTx->Aux.Data.Byte[0] = Crc;

  XV_HdmiTx_AuxSend(HdmiTx);
}

/*****************************************************************************/
/**
*
* This function sends the general control packet.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_SendGeneralControlPacket(XV_HdmiTx *HdmiTx)
{
  u8 Index;
  u8 Data;

  // Pre-Process SB1 data
  // Pixel Packing Phase
  switch (XV_HdmiTx_GetPixelPackingPhase(HdmiTx)) {

    case 1 :
      Data = 1;
      break;

    case 2 :
      Data = 2;
      break;

    case 3 :
      Data = 3;
      break;

    default :
      Data = 0;
      break;
  }

  /**< Shift pixel packing phase to the upper nibble */
  Data <<= 4;

  /** In HDMI the colordepth in YUV422 is always 12 bits,  although on the
  * link itself it is being transmitted as 8-bits. Therefore if the colorspace
  * is YUV422, then force the colordepth to 8 bits. */
  if (HdmiTx->Stream.Video.ColorFormatId == XVIDC_CSF_YCRCB_422) {
    Data |= 0;
  }

  else {

    // Colordepth
    switch (HdmiTx->Stream.Video.ColorDepth) {

      // 10 bpc
      case XVIDC_BPC_10:
        // Color depth
        Data |= 5;
        break;

      // 12 bpc
      case XVIDC_BPC_12:
        // Color depth
        Data |= 6;
        break;

      // 16 bpc
      case XVIDC_BPC_16:
        // Color depth
        Data |= 7;
        break;

      // Not indicated
      default:
        Data = 0;
        break;
    }
  }

  // Packet type
  HdmiTx->Aux.Header.Byte[0] = 0x3;

  // Reserved
  HdmiTx->Aux.Header.Byte[1] = 0;

  // Reserved
  HdmiTx->Aux.Header.Byte[2] = 0;

  // Checksum (this will be calculated by the HDMI TX IP)
  HdmiTx->Aux.Header.Byte[3] = 0;

  // Data
  // The packet contains four identical subpackets
  for (Index = 0; Index < 4; Index++) {
    // SB0
    HdmiTx->Aux.Data.Byte[(Index*8)] = 0;

    // SB1
    HdmiTx->Aux.Data.Byte[(Index*8)+1] = Data;

    // SB2
    HdmiTx->Aux.Data.Byte[(Index*8)+2] = 0;

    // SB3
    HdmiTx->Aux.Data.Byte[(Index*8)+3] = 0;

    // SB4
    HdmiTx->Aux.Data.Byte[(Index*8)+4] = 0;

    // SB5
    HdmiTx->Aux.Data.Byte[(Index*8)+5] = 0;

    // SB6
    HdmiTx->Aux.Data.Byte[(Index*8)+6] = 0;

    // SB ECC
    HdmiTx->Aux.Data.Byte[(Index*8)+7] = 0;

  }

  XV_HdmiTx_AuxSend(HdmiTx);
}

/*****************************************************************************/
/**
*
* This function sends the Vendor Specific Info Frame.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_SendVSInfoframe(XV_HdmiTx *HdmiTx)
{
    XV_HdmiTx_VSIF VSIF;

    VSIF.Version = 0x1;
    VSIF.IEEE_ID = 0xC03;

    if (XVidC_IsStream3D(&HdmiTx->Stream.Video)) {
        VSIF.Format = XV_HDMITX_VSIF_VF_3D;
        VSIF.Info_3D.Stream = HdmiTx->Stream.Video.Info_3D;
        VSIF.Info_3D.MetaData.IsPresent = FALSE;
    }
    else if (HdmiTx->Stream.Video.VmId == XVIDC_VM_3840x2160_24_P ||
             HdmiTx->Stream.Video.VmId == XVIDC_VM_3840x2160_25_P ||
             HdmiTx->Stream.Video.VmId == XVIDC_VM_3840x2160_30_P ||
             HdmiTx->Stream.Video.VmId == XVIDC_VM_4096x2160_24_P) {
        VSIF.Format = XV_HDMITX_VSIF_VF_EXTRES;

        /* Set HDMI VIC */
        switch(HdmiTx->Stream.Video.VmId) {
            case XVIDC_VM_4096x2160_24_P :
                VSIF.HDMI_VIC = 4;
                break;
            case XVIDC_VM_3840x2160_24_P :
                VSIF.HDMI_VIC = 3;
                break;
            case XVIDC_VM_3840x2160_25_P :
                VSIF.HDMI_VIC = 2;
                break;
            case XVIDC_VM_3840x2160_30_P :
                VSIF.HDMI_VIC = 1;
                break;
            default :
                break;
        }
    }
    else {
        VSIF.Format = XV_HDMITX_VSIF_VF_NOINFO;
    }

    XV_HdmiTx_VSIF_GeneratePacket(&VSIF,(XV_HdmiTx_Aux *)&HdmiTx->Aux);

    XV_HdmiTx_AuxSend(HdmiTx);
}

/*****************************************************************************/
/**
*
* This function is called when the TX stream is up.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_StreamUpCallback(void *CallbackRef)
{
  XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)CallbackRef;

  /* Set stream up flag */
  HdmiTxSsPtr->IsStreamUp = (TRUE);

#ifdef USE_HDCP_TX
  /* Push the stream-up event to the HDCP event queue */
  XV_HdmiTxSs_HdcpPushEvent(HdmiTxSsPtr, XV_HDMITXSS_HDCP_STREAMUP_EVT);
#endif

  /* Check if user callback has been registered.
     User may change the video stream properties in the callback;
     therefore, execute the callback before changing stream settings. */
  if (HdmiTxSsPtr->StreamUpCallback) {
      HdmiTxSsPtr->StreamUpCallback(HdmiTxSsPtr->StreamUpRef);
  }

  /* Set TX sample rate */
  XV_HdmiTx_SetSampleRate(HdmiTxSsPtr->HdmiTxPtr, HdmiTxSsPtr->SamplingRate);

  /* Release HDMI TX reset */
  XV_HdmiTx_Reset(HdmiTxSsPtr->HdmiTxPtr, FALSE);

  if (HdmiTxSsPtr->VtcPtr) {
    /* Setup VTC */
    XV_HdmiTxSs_VtcSetup(HdmiTxSsPtr->VtcPtr, HdmiTxSsPtr->HdmiTxPtr);
  }

  if (HdmiTxSsPtr->AudioEnabled) {
      /* HDMI TX unmute audio */
      HdmiTxSsPtr->AudioMute = (FALSE);
      XV_HdmiTx_AudioUnmute(HdmiTxSsPtr->HdmiTxPtr);
  }

  /* Configure video bridge mode according to HW setting and video format */
  XV_HdmiTxSs_ConfigBridgeMode(HdmiTxSsPtr);
#ifdef XV_HDMITXSS_LOG_ENABLE
  XV_HdmiTxSs_LogWrite(HdmiTxSsPtr, XV_HDMITXSS_LOG_EVT_STREAMUP, 0);
#endif
}

/*****************************************************************************/
/**
*
* This function is called when the TX stream is down.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_StreamDownCallback(void *CallbackRef)
{
  XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)CallbackRef;

  /* Assert HDMI TX reset */
  XV_HdmiTx_Reset(HdmiTxSsPtr->HdmiTxPtr, TRUE);

  /* Set stream up flag */
  HdmiTxSsPtr->IsStreamUp = (FALSE);
#ifdef XV_HDMITXSS_LOG_ENABLE
  XV_HdmiTxSs_LogWrite(HdmiTxSsPtr, XV_HDMITXSS_LOG_EVT_STREAMDOWN, 0);
#endif
#ifdef USE_HDCP_TX
  /* Push the stream-down event to the HDCP event queue */
  XV_HdmiTxSs_HdcpPushEvent(HdmiTxSsPtr, XV_HDMITXSS_HDCP_STREAMDOWN_EVT);
#endif

  /* Check if user callback has been registered */
  if (HdmiTxSsPtr->StreamDownCallback) {
      HdmiTxSsPtr->StreamDownCallback(HdmiTxSsPtr->StreamDownRef);
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
* -----------------------         ---------------------------------------------
* (XV_HDMITXSS_HANDLER_CONNECT)       HpdCallback
* (XV_HDMITXSS_HANDLER_VS)            VsCallback
* (XV_HDMITXSS_HANDLER_STREAM_DOWN)   StreamDownCallback
* (XV_HDMITXSS_HANDLER_STREAM_UP)     StreamUpCallback
* (XV_HDMITXSS_HANDLER_HDCP_AUTHENTICATED)
* (XV_HDMITXSS_HANDLER_HDCP_DOWNSTREAM_TOPOLOGY_AVAILABLE)
* (XV_HDMITXSS_HANDLER_HDCP_UNAUTHENTICATED)
* </pre>
*
* @param    InstancePtr is a pointer to the HDMI TX Subsystem instance.
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
int XV_HdmiTxSs_SetCallback(XV_HdmiTxSs *InstancePtr,
    u32 HandlerType,
    void *CallbackFunc,
    void *CallbackRef)
{
    u32 Status;

    /* Verify arguments. */
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(HandlerType >= (XV_HDMITXSS_HANDLER_CONNECT));
    Xil_AssertNonvoid(CallbackFunc != NULL);
    Xil_AssertNonvoid(CallbackRef != NULL);

    /* Check for handler type */
    switch (HandlerType) {
        // Connect
        case (XV_HDMITXSS_HANDLER_CONNECT):
            InstancePtr->ConnectCallback = (XV_HdmiTxSs_Callback)CallbackFunc;
            InstancePtr->ConnectRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // Toggle
        case (XV_HDMITXSS_HANDLER_TOGGLE):
            InstancePtr->ToggleCallback = (XV_HdmiTxSs_Callback)CallbackFunc;
            InstancePtr->ToggleRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // Vsync
        case (XV_HDMITXSS_HANDLER_VS):
            InstancePtr->VsCallback = (XV_HdmiTxSs_Callback)CallbackFunc;
            InstancePtr->VsRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // Stream down
        case (XV_HDMITXSS_HANDLER_STREAM_DOWN):
            InstancePtr->StreamDownCallback =
                (XV_HdmiTxSs_Callback)CallbackFunc;
            InstancePtr->StreamDownRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // Stream up
        case (XV_HDMITXSS_HANDLER_STREAM_UP):
            InstancePtr->StreamUpCallback = (XV_HdmiTxSs_Callback)CallbackFunc;
            InstancePtr->StreamUpRef = CallbackRef;
            Status = (XST_SUCCESS);
            break;

        // HDCP authenticated
        case (XV_HDMITXSS_HANDLER_HDCP_AUTHENTICATED):
#ifdef XPAR_XHDCP_NUM_INSTANCES
            /* Register HDCP 1.4 callbacks */
            if (InstancePtr->Hdcp14Ptr) {
              XHdcp1x_SetCallback(InstancePtr->Hdcp14Ptr,
                                  XHDCP1X_HANDLER_AUTHENTICATED,
								  (void *)(XHdcp1x_Callback) CallbackFunc,
								  (void *)CallbackRef);
            }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
            /* Register HDCP 2.2 callbacks */
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Tx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_TX_HANDLER_AUTHENTICATED,
									(void *)(XHdcp22_Tx_Callback)CallbackFunc,
									(void *)CallbackRef);
            }
#endif
            Status = (XST_SUCCESS);
            break;

        // HDCP downstream topology available
        case (XV_HDMITXSS_HANDLER_HDCP_DOWNSTREAM_TOPOLOGY_AVAILABLE):
#ifdef XPAR_XHDCP_NUM_INSTANCES
            /** Register HDCP 1.4 callbacks */
            if (InstancePtr->Hdcp14Ptr) {
        XHdcp1x_SetCallBack(InstancePtr->Hdcp14Ptr,
                            (XHdcp1x_HandlerType) XHDCP1X_RPTR_HDLR_REPEATER_EXCHANGE,
							(void *) (XHdcp1x_Callback)CallbackFunc,
							(void *) CallbackRef);
            }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
            /** Register HDCP 2.2 callbacks */
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Tx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_TX_HANDLER_DOWNSTREAM_TOPOLOGY_AVAILABLE,
									(void *)(XHdcp22_Tx_Callback)CallbackFunc,
									(void *)CallbackRef);
            }
#endif
            Status = (XST_SUCCESS);
            break;

        // HDCP unauthenticated
        case (XV_HDMITXSS_HANDLER_HDCP_UNAUTHENTICATED):
#ifdef XPAR_XHDCP_NUM_INSTANCES
            /** Register HDCP 1.4 callbacks */
            if (InstancePtr->Hdcp14Ptr) {
        XHdcp1x_SetCallBack(InstancePtr->Hdcp14Ptr,
                            XHDCP1X_HANDLER_UNAUTHENTICATED,
							(void *) (XHdcp1x_Callback)CallbackFunc,
							(void *) CallbackRef);
            }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
            /** Register HDCP 2.2 callbacks */
            if (InstancePtr->Hdcp22Ptr) {
              XHdcp22Tx_SetCallback(InstancePtr->Hdcp22Ptr,
                                    XHDCP22_TX_HANDLER_UNAUTHENTICATED,
                                    (void *) (XHdcp22_Tx_Callback)CallbackFunc,
									(void *) CallbackRef);
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
* This function reads the HDMI Sink EDID.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_ReadEdid(XV_HdmiTxSs *InstancePtr, u8 *Buffer)
{
    u32 Status;

    // Default
    Status = (XST_FAILURE);

    // Check if a sink is connected
    if (InstancePtr->IsStreamConnected == (TRUE)) {

      *Buffer = 0x00;   // Offset zero
      Status = XV_HdmiTx_DdcWrite(InstancePtr->HdmiTxPtr, 0x50, 1, Buffer,
        (FALSE));

      // Check if write was successful
      if (Status == (XST_SUCCESS)) {
        // Read edid
        Status = XV_HdmiTx_DdcRead(InstancePtr->HdmiTxPtr, 0x50, 256, Buffer,
            (TRUE));
      }
    }
  return Status;
}

/*****************************************************************************/
/**
*
* This function reads one block from the HDMI Sink EDID.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_ReadEdidSegment(XV_HdmiTxSs *InstancePtr, u8 *Buffer, u8 segment)
{
    u32 Status;

    u8 dummy = 0;

    // Default
    Status = (XST_FAILURE);

    // Check if a sink is connected
    if (InstancePtr->IsStreamConnected == (TRUE)) {

	  // For multiple segment EDID read
	  // First, read the first block, then read address 0x7e to know how many
	  // blocks, if more than 2 blocks, then select segment after first 2 blocks
	  // Use the following code to select segment

	  if(segment != 0) {
        // Segment Pointer
        Status = XV_HdmiTx_DdcWrite(InstancePtr->HdmiTxPtr, 0x30, 1, &segment,
        (FALSE));
	  }

      // Read blocks
      dummy = 0x00;   // Offset zero
      Status = XV_HdmiTx_DdcWrite(InstancePtr->HdmiTxPtr, 0x50, 1, &dummy,
        (FALSE));

      // Check if write was successful
      if (Status == (XST_SUCCESS)) {
        // Read edid
        Status = XV_HdmiTx_DdcRead(InstancePtr->HdmiTxPtr, 0x50, 128, Buffer,
            (TRUE));
      }

	  if(segment != 0) {
        // Segment Pointer
        Status = XV_HdmiTx_DdcWrite(InstancePtr->HdmiTxPtr, 0x30, 1, &segment,
        (FALSE));
	  }

      // Read blocks
      dummy = 0x80;   // Offset 128
      Status = XV_HdmiTx_DdcWrite(InstancePtr->HdmiTxPtr, 0x50, 1, &dummy,
        (FALSE));

      // Check if write was successful
      if (Status == (XST_SUCCESS)) {
        // Read edid
        Status = XV_HdmiTx_DdcRead(InstancePtr->HdmiTxPtr, 0x50, 128, &Buffer[128],
            (TRUE));
      }
    }
    else {
      xil_printf("No sink is connected.\n\r");
      xil_printf("Please connect a HDMI sink.\n\r");
    }
  return Status;
}

/*****************************************************************************/
/**
*
* This function shows the HDMI source edid.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_ShowEdid(XV_HdmiTxSs *InstancePtr)
{
    u8 Buffer[256];
    u8 Row;
    u8 Column;
    u8 Valid;
    u32 Status;
    u8 EdidManName[4];
	u8 segment = 0;
    u8 ExtensionFlag = 0;

    // Check if a sink is connected
    if (InstancePtr->IsStreamConnected == (TRUE)) {

      // Default
      Valid = (FALSE);

      // Read Sink Edid Segment 0
      Status = XV_HdmiTxSs_ReadEdidSegment(InstancePtr, (u8*)&Buffer, segment);

      // Check if read was successful
      if (Status == (XST_SUCCESS)) {
        XVidC_EdidGetManName(&Buffer[0], (char *) EdidManName);
        xil_printf("\r\nMFG name : %s\r\n", EdidManName);

		ExtensionFlag = Buffer[126];
		ExtensionFlag = ExtensionFlag >> 1;
        xil_printf("Number of Segment : %d\n\r", ExtensionFlag+1);
        xil_printf("\r\nRaw data\r\n");
        xil_printf("----------------------------------------------------\r\n");
	  }

      segment = 0;
	  while (segment <= ExtensionFlag)
	  {
        // Check if read was successful
        if (Status == (XST_SUCCESS)) {
          xil_printf("\n\r---- Segment %d ----\n\r", segment);
          xil_printf("----------------------------------------------------\n\r");
          for (Row = 0; Row < 16; Row++) {
            xil_printf("%02X : ", (Row*16));
            for (Column = 0; Column < 16; Column++) {
              xil_printf("%02X ", Buffer[(Row*16)+Column]);
            }
        xil_printf("\r\n");
          }
          Valid = (TRUE);

          segment++;
		  if(segment <= ExtensionFlag) {
            Status = XV_HdmiTxSs_ReadEdidSegment(InstancePtr, (u8*)&Buffer, segment);
		  }
        }
	  }

      if (!Valid) {
        xil_printf("Error reading EDID\r\n");
      }
    }

    else {
      xil_printf("No sink is connected.\r\n");
    }
}


/*****************************************************************************/
/**
*
* This function starts the HDMI TX stream
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_StreamStart(XV_HdmiTxSs *InstancePtr)
{
  // Set TX pixel rate
  XV_HdmiTx_SetPixelRate(InstancePtr->HdmiTxPtr);

  // Set TX color depth
  XV_HdmiTx_SetColorDepth(InstancePtr->HdmiTxPtr);

  // Set TX color format
  XV_HdmiTx_SetColorFormat(InstancePtr->HdmiTxPtr);

  // Set TX scrambler
  XV_HdmiTx_Scrambler(InstancePtr->HdmiTxPtr);

  // Set TX clock ratio
  XV_HdmiTx_ClockRatio(InstancePtr->HdmiTxPtr);
#ifdef XV_HDMITXSS_LOG_ENABLE
  XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_STREAMSTART, 0);
#endif
}

/*****************************************************************************/
/**
*
* This function sends audio info frames.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SendAuxInfoframe(XV_HdmiTxSs *InstancePtr, void *Aux)
{
  u8 Index;
  u8 Crc;
  XV_HdmiTx_Aux *tx_aux = (XV_HdmiTx_Aux *)Aux;

  if (Aux == (NULL)) {
      /* Header, Packet type */
      InstancePtr->HdmiTxPtr->Aux.Header.Byte[0] = 0x84;

      /* Version */
      InstancePtr->HdmiTxPtr->Aux.Header.Byte[1] = 0x01;

      /* Length */
      InstancePtr->HdmiTxPtr->Aux.Header.Byte[2] = 10;

      /* Checksum (this will be calculated by the HDMI TX IP) */
      InstancePtr->HdmiTxPtr->Aux.Header.Byte[3] = 0;

      /* 2 Channel count. Audio coding type refer to stream */
      InstancePtr->HdmiTxPtr->Aux.Data.Byte[1] = 0x1;

      for (Index = 2; Index < 32; Index++) {
        InstancePtr->HdmiTxPtr->Aux.Data.Byte[Index] = 0;
      }

      /* Calculate AVI infoframe checksum */
      Crc = 0;

      /* Header */
      for (Index = 0; Index < 3; Index++) {
        Crc += InstancePtr->HdmiTxPtr->Aux.Header.Byte[Index];
      }

      /* Data */
      for (Index = 1; Index < 5; Index++) {
        Crc += InstancePtr->HdmiTxPtr->Aux.Data.Byte[Index];
      }

      Crc = 256 - Crc;
      InstancePtr->HdmiTxPtr->Aux.Data.Byte[0] = Crc;

      XV_HdmiTx_AuxSend(InstancePtr->HdmiTxPtr);

  }

  else {
      // Copy Audio Infoframe
      if (tx_aux->Header.Byte[0] == 0x84) {
        // Header
        InstancePtr->HdmiTxPtr->Aux.Header.Data = tx_aux->Header.Data;

        // Data
        for (Index = 0; Index < 8; Index++) {
          InstancePtr->HdmiTxPtr->Aux.Data.Data[Index] =
            tx_aux->Data.Data[Index];
        }
      }
  }

  /* Send packet */
  XV_HdmiTx_AuxSend(InstancePtr->HdmiTxPtr);
}

/*****************************************************************************/
/**
*
* This function sends generic info frames.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SendGenericAuxInfoframe(XV_HdmiTxSs *InstancePtr, void *Aux)
{
  u8 Index;
  XV_HdmiTx_Aux *tx_aux = (XV_HdmiTx_Aux *)Aux;

  // Header
  InstancePtr->HdmiTxPtr->Aux.Header.Data = tx_aux->Header.Data;

  // Data
  for (Index = 0; Index < 8; Index++) {
    InstancePtr->HdmiTxPtr->Aux.Data.Data[Index] =
    tx_aux->Data.Data[Index];
  }

  /* Send packet */
  XV_HdmiTx_AuxSend(InstancePtr->HdmiTxPtr);
}

/*****************************************************************************/
/**
*
* This function Sets the HDMI TX SS number of active audio channels
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
* @param  AudioChannels
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SetAudioChannels(XV_HdmiTxSs *InstancePtr, u8 AudioChannels)
{
    InstancePtr->AudioChannels = AudioChannels;
    XV_HdmiTx_SetAudioChannels(InstancePtr->HdmiTxPtr, AudioChannels);
#ifdef XV_HDMITXSS_LOG_ENABLE
    XV_HdmiTxSs_LogWrite(InstancePtr,
                         XV_HDMITXSS_LOG_EVT_SETAUDIOCHANNELS,
                         AudioChannels);
#endif
}

/*****************************************************************************/
/**
*
* This function set HDMI TX audio parameters
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_AudioMute(XV_HdmiTxSs *InstancePtr, u8 Enable)
{
  //Audio Mute Mode
  if (Enable){
    XV_HdmiTx_AudioMute(InstancePtr->HdmiTxPtr);
#ifdef XV_HDMITXSS_LOG_ENABLE
    XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_AUDIOMUTE, 0);
#endif
  }
  else{
    XV_HdmiTx_AudioUnmute(InstancePtr->HdmiTxPtr);
#ifdef XV_HDMITXSS_LOG_ENABLE
    XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_AUDIOUNMUTE, 0);
#endif
  }
}

/*****************************************************************************/
/**
*
* This function set HDMI TX susbsystem stream parameters
*
* @param  None.
*
* @return Calculated TMDS Clock
*
* @note   None.
*
******************************************************************************/
u32 XV_HdmiTxSs_SetStream(XV_HdmiTxSs *InstancePtr,
        XVidC_VideoMode VideoMode, XVidC_ColorFormat ColorFormat,
        XVidC_ColorDepth Bpc, XVidC_3DInfo *Info3D)
{
  u32 TmdsClock = 0;

  TmdsClock = XV_HdmiTx_SetStream(InstancePtr->HdmiTxPtr, VideoMode,
    ColorFormat, Bpc, InstancePtr->Config.Ppc, Info3D);
#ifdef XV_HDMITXSS_LOG_ENABLE
  XV_HdmiTxSs_LogWrite(InstancePtr, XV_HDMITXSS_LOG_EVT_SETSTREAM, TmdsClock);
#endif
  if(TmdsClock == 0) {
    xdbg_printf(XDBG_DEBUG_GENERAL,
                "\nWarning: Sink does not support HDMI 2.0\r\n");
    xdbg_printf(XDBG_DEBUG_GENERAL,
                "         Connect to HDMI 2.0 Sink or \r\n");
    xdbg_printf(XDBG_DEBUG_GENERAL,
                "         Change to HDMI 1.4 video format\r\n\n");
}

  return TmdsClock;
}

/*****************************************************************************/
/**
*
* This function returns the pointer to HDMI TX SS video stream
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
*
* @return XVidC_VideoStream pointer
*
* @note   None.
*
******************************************************************************/
XVidC_VideoStream *XV_HdmiTxSs_GetVideoStream(XV_HdmiTxSs *InstancePtr)
{
    return (&InstancePtr->HdmiTxPtr->Stream.Video);
}

/*****************************************************************************/
/**
*
* This function Sets the HDMI TX SS video stream
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
* @param
*
* @return XVidC_VideoStream pointer
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SetVideoStream(XV_HdmiTxSs *InstancePtr,
                                    XVidC_VideoStream VidStream)
{
    InstancePtr->HdmiTxPtr->Stream.Video = VidStream;
}

/*****************************************************************************/
/**
*
* This function Sets the HDMI TX SS video Identification code
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
* @param  SamplingRate Value
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SetSamplingRate(XV_HdmiTxSs *InstancePtr, u8 SamplingRate)
{
    InstancePtr->SamplingRate = SamplingRate;
}

/*****************************************************************************/
/**
*
* This function Sets the HDMI TX SS video Identification code
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
* @param  InstancePtr VIC Flag Value
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SetVideoIDCode(XV_HdmiTxSs *InstancePtr, u8 Vic)
{
    InstancePtr->HdmiTxPtr->Stream.Vic = Vic;
}

/*****************************************************************************/
/**
*
* This function Sets the HDMI TX SS video stream type
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
* @param  InstancePtr VIC Value 1:HDMI 0:DVI
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SetVideoStreamType(XV_HdmiTxSs *InstancePtr, u8 StreamType)
{
    InstancePtr->HdmiTxPtr->Stream.IsHdmi = StreamType;
}

/*****************************************************************************/
/**
*
* This function Sets the HDMI TX SS video stream type
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
* @param  IsScrambled 1:IsScrambled 0: not Scrambled
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SetVideoStreamScramblingFlag(XV_HdmiTxSs *InstancePtr,
                                                            u8 IsScrambled)
{
    InstancePtr->HdmiTxPtr->Stream.IsScrambled = IsScrambled;
}

/*****************************************************************************/
/**
*
* This function Sets the HDMI TX SS TMDS Cock Ratio
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
* @param  Ratio 0 - 1/10, 1 - 1/40
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_SetTmdsClockRatio(XV_HdmiTxSs *InstancePtr, u8 Ratio)
{
    InstancePtr->HdmiTxPtr->Stream.TMDSClockRatio = Ratio;
}

/*****************************************************************************/
/**
*
* This function Sets the HDMI TX SS video Identification code
*
* @param  InstancePtr pointer to XV_HdmiTxSs instance
* @param
*
* @return Stream Data Structure (TMDS Clock)
*
* @note   None.
*
******************************************************************************/
u32 XV_HdmiTxSs_GetTmdsClockFreqHz(XV_HdmiTxSs *InstancePtr)
{
    return (InstancePtr->HdmiTxPtr->Stream.TMDSClock);
}

/*****************************************************************************/
/**
*
* This function detects connected sink is a HDMI 2.0/HDMI 1.4 sink device
*
* @param    InstancePtr is a pointer to the XV_HdmiTxSs core instance.
*
* @return
*       - XST_SUCCESS if HDMI 2.0
*       - XST_FAILURE if HDMI 1.4
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_DetectHdmi20(XV_HdmiTxSs *InstancePtr)
{
      return (XV_HdmiTx_DetectHdmi20(InstancePtr->HdmiTxPtr));
}

/*****************************************************************************/
/**
*
* This function is called when HDMI TX SS TMDS clock changes
*
* @param  None.
*
* @return None
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_RefClockChangeInit(XV_HdmiTxSs *InstancePtr)
{
      /* Assert HDMI TX reset */
      XV_HdmiTx_Reset(InstancePtr->HdmiTxPtr, TRUE);

      /* Clear variables */
      XV_HdmiTx_Clear(InstancePtr->HdmiTxPtr);
}

/*****************************************************************************/
/**
*
* This function prints the HDMI TX SS timing information
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_ReportTiming(XV_HdmiTxSs *InstancePtr)
{
      XV_HdmiTx_DebugInfo(InstancePtr->HdmiTxPtr);
      xil_printf("Scrambled: %0d\r\n",
        (XV_HdmiTx_IsStreamScrambled(InstancePtr->HdmiTxPtr)));
      xil_printf("Sample rate: %0d\r\n",
        (XV_HdmiTx_GetSampleRate(InstancePtr->HdmiTxPtr)));
      xil_printf("Audio channels: %0d\r\n",
        (XV_HdmiTx_GetAudioChannels(InstancePtr->HdmiTxPtr)));
      xil_printf("\r\n");

}

/*****************************************************************************/
/**
*
* This function prints the HDMI TX SS subcore versions
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_ReportSubcoreVersion(XV_HdmiTxSs *InstancePtr)
{
  u32 Data;

  if (InstancePtr->HdmiTxPtr) {
     Data = XV_HdmiTx_GetVersion(InstancePtr->HdmiTxPtr);
     xil_printf("  HDMI TX version : %02d.%02d (%04x)\r\n",
        ((Data >> 24) & 0xFF), ((Data >> 16) & 0xFF), (Data & 0xFFFF));
  }

  if (InstancePtr->VtcPtr){
     Data = XVtc_GetVersion(InstancePtr->VtcPtr);
     xil_printf("  VTC version     : %02d.%02d (%04x)\r\n",
        ((Data >> 24) & 0xFF), ((Data >> 16) & 0xFF), (Data & 0xFFFF));
  }

#ifdef XPAR_XHDCP_NUM_INSTANCES
  // HDCP 1.4
  if (InstancePtr->Hdcp14Ptr){
     Data = XHdcp1x_GetVersion(InstancePtr->Hdcp14Ptr);
     xil_printf("  HDCP 1.4 TX version : %02d.%02d (%04x)\r\n",
        ((Data >> 24) & 0xFF), ((Data >> 16) & 0xFF), (Data & 0xFFFF));
  }
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
  // HDCP 2.2
  if (InstancePtr->Hdcp22Ptr) {
   Data = XHdcp22Tx_GetVersion(InstancePtr->Hdcp22Ptr);
   xil_printf("  HDCP 2.2 TX version : %02d.%02d (%04x)\r\n",
    ((Data >> 24) & 0xFF), ((Data >> 16) & 0xFF), (Data & 0xFFFF));
  }
#endif

}


/*****************************************************************************/
/**
*
* This function prints the HDMI TX SS subcore versions
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiTxSs_ReportInfo(XV_HdmiTxSs *InstancePtr)
{
    xil_printf("------------\r\n");
    xil_printf("HDMI TX SubSystem\r\n");
    xil_printf("------------\r\n");
    XV_HdmiTxSs_ReportCoreInfo(InstancePtr);
    XV_HdmiTxSs_ReportSubcoreVersion(InstancePtr);
    xil_printf("\r\n");
    xil_printf("HDMI TX timing\r\n");
    xil_printf("------------\r\n");
    XV_HdmiTxSs_ReportTiming(InstancePtr);
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
int XV_HdmiTxSs_IsStreamUp(XV_HdmiTxSs *InstancePtr)
{
  /* Verify arguments. */
  Xil_AssertNonvoid(InstancePtr != NULL);

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
*   - TRUE if the interface is connected.
*   - FALSE if the interface is not connected.
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_IsStreamConnected(XV_HdmiTxSs *InstancePtr)
{
  /* Verify arguments. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  return (InstancePtr->IsStreamConnected);
}

/*****************************************************************************/
/**
*
* This function checks if the interface has toggled.
*
* @param  None.
*
* @return
*   - TRUE if the interface HPD has toggled.
*   - FALSE if the interface HPD has not toggled.
*
* @note   None.
*
******************************************************************************/
int XV_HdmiTxSs_IsStreamToggled(XV_HdmiTxSs *InstancePtr)
{
  /* Verify arguments. */
  Xil_AssertNonvoid(InstancePtr != NULL);

  return (InstancePtr->IsStreamToggled);
}

/*****************************************************************************/
/**
*
* This function configures the Bridge for YUV420 and repeater functionality
*
* @param InstancePtr  Instance Pointer to the main data structure
* @param None
*
* @return
*
* @note   None.
*
******************************************************************************/
static void XV_HdmiTxSs_ConfigBridgeMode(XV_HdmiTxSs *InstancePtr) {

    XVidC_ColorFormat ColorFormat;
    XVidC_VideoMode VideoMode;

    XVidC_VideoStream *HdmiTxSsVidStreamPtr;
    HdmiTxSsVidStreamPtr = XV_HdmiTxSs_GetVideoStream(InstancePtr);

    ColorFormat = HdmiTxSsVidStreamPtr->ColorFormatId;
    VideoMode = HdmiTxSsVidStreamPtr->VmId;

    if (ColorFormat == XVIDC_CSF_YCRCB_420) {
        /*********************************************************
         * 420 Support
         *********************************************************/
         XV_HdmiTxSs_BridgePixelRepeat(InstancePtr,FALSE);
         XV_HdmiTxSs_BridgeYuv420(InstancePtr,TRUE);
    }
    else {
        if ((VideoMode == XVIDC_VM_1440x480_60_I) ||
            (VideoMode == XVIDC_VM_1440x576_50_I) )
        {
            /*********************************************************
             * NTSC/PAL Support
             *********************************************************/
             XV_HdmiTxSs_BridgeYuv420(InstancePtr,FALSE);
             XV_HdmiTxSs_BridgePixelRepeat(InstancePtr,TRUE);
        }
        else {
            XV_HdmiTxSs_BridgeYuv420(InstancePtr,FALSE);
            XV_HdmiTxSs_BridgePixelRepeat(InstancePtr,FALSE);
        }
    }
}

/*****************************************************************************/
/**
* This function will set the default in HDF.
*
* @param    InstancePtr is a pointer to the XV_HdmiTxSs core instance.
* @param    Id is the XV_HdmiTxSs ID to operate on.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XV_HdmiTxSs_SetDefaultPpc(XV_HdmiTxSs *InstancePtr, u8 Id) {
}

/*****************************************************************************/
/**
* This function will set PPC specified by user.
*
* @param    InstancePtr is a pointer to the XV_HdmiTxSs core instance.
* @param    Id is the XV_HdmiTxSs ID to operate on.
* @param    Ppc is the PPC to be set.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XV_HdmiTxSs_SetPpc(XV_HdmiTxSs *InstancePtr, u8 Id, u8 Ppc) {
    InstancePtr->Config.Ppc = (XVidC_PixelsPerClock) Ppc;
    Id = Id; //squash unused variable compiler warning
}

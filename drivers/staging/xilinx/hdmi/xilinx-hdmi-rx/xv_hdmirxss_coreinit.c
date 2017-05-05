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
* @file xv_hdmirxss_coreinit.c
* @addtogroup v_hdmirxss
* @{
* @details

* HDMI RX Subsystem Sub-Cores initialization
* The functions in this file provides an abstraction from the initialization
* sequence for included sub-cores. Subsystem is assigned an address and range
* on the axi-lite interface. This address space is condensed where-in each
* sub-core is at a fixed offset from the subsystem base address. For processor
* to be able to access the sub-core this offset needs to be transalted into a
* absolute address within the subsystems addressable range
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00         10/07/15 Initial release.
* 1.1   yh     20/01/16 Added remapper support
* 1.2   MG     20/01/16 Added HDCP support
* 1.3   MH     08/03/16 Added DDC read message not complete event to
*                       the function XV_HdmiRxSs_DdcHdcpCallback.
*                       Updated XV_HdmiRxSs_LinkErrorCallback
*                       function to set link error flag.
* 1.4   MH     23/04/16 Remove XV_HdmiRxSs_Reset from
*                       function XV_HdmiRxSs_SubcoreInitHdmiRx
* 1.5   MH     15/07/16 Added HDCP repeater support.
* 1.6   YH     18/07/16 Replace xil_print with xdbg_printf.
* 1.7   YH     25/07/16 Used UINTPTR instead of u32 for BaseAddr,HighAddr,Offset
*                       AbsAddr
* 1.8   MH     08/08/16 Updates to optimize out HDCP when excluded.
* 1.9   YH     14/11/16 Remove Remapper APIs as remapper feature is moved to
*                       video bridge and controlled by HDMI core
* 1.10  MMO    03/01/17 Remove XV_HdmiRxSs_ComputeSubcoreAbsAddr API, as it
*                       handles in the "_g" TCL generation
*                       Move XV_HdmiRx_DdcLoadEdid to xv_hdmirxss.h and call it
*                       in user application
*                       Add compiler option(XV_HDMIRXSS_LOG_ENABLE) to enable Log
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xv_hdmirxss_coreinit.h"
#include "xil_printf.h"

/************************** Constant Definitions *****************************/

/************************** Function Prototypes ******************************/
#ifdef USE_HDCP_RX
static void XV_HdmiRxSs_DdcSetRegAddrHandler(void *RefPtr, u32 Data);
static void XV_HdmiRxSs_DdcSetRegDataHandler(void *RefPtr, u32 Data);
static u32 XV_HdmiRxSs_DdcGetRegDataHandler(void *RefPtr);
static void XV_HdmiRxSs_DdcHdcpCallback(void *RefPtr, int Type);
#endif
#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
static u32 XV_HdmiRxSs_DdcGetWriteMessageBufferWordsHandler(void *RefPtr);
static u32 XV_HdmiRxSs_DdcGetReadMessageBufferWordsHandler(void *RefPtr);
static u32 XV_HdmiRxSs_DdcIsReadMessageBufferEmptyHandler(void *RefPtr);
static u32 XV_HdmiRxSs_DdcIsWriteMessageBufferEmptyHandler(void *RefPtr);
static void XV_HdmiRxSs_DdcClearReadMessageBufferHandler(void *RefPtr);
static void XV_HdmiRxSs_DdcClearWriteMessageBufferHandler(void *RefPtr);
static void XV_HdmiRxSs_LinkErrorCallback(void *RefPtr);
#endif

/*****************************************************************************/
/**
* This function initializes the included sub-core to it's static configuration
*
* @param  HdmiRxSsPtr is a pointer to the Subsystem instance to be worked on.
*
* @return XST_SUCCESS/XST_FAILURE
*
******************************************************************************/
int XV_HdmiRxSs_SubcoreInitHdmiRx(XV_HdmiRxSs *HdmiRxSsPtr)
{
  int Status;
  XV_HdmiRx_Config *ConfigPtr;

  if(HdmiRxSsPtr->HdmiRxPtr)
  {
    /* Get core configuration */
#ifdef XV_HDMIRXSS_LOG_ENABLE
    XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_HDMIRX_INIT, 0);
#endif
    ConfigPtr  = XV_HdmiRx_LookupConfig(HdmiRxSsPtr->Config.HdmiRx.DeviceId);
    if(ConfigPtr == NULL)
    {
      xdbg_printf(XDBG_DEBUG_GENERAL,"HDMIRXSS ERR:: HDMI RX device not found\r\n");
      return(XST_FAILURE);
    }

    /* Initialize core */
    Status = XV_HdmiRx_CfgInitialize(HdmiRxSsPtr->HdmiRxPtr,
                                    ConfigPtr,
                                    HdmiRxSsPtr->Config.HdmiRx.AbsAddr);

    if (Status != XST_SUCCESS)
    {
      xdbg_printf(XDBG_DEBUG_GENERAL,"HDMIRXSS ERR:: HDMI RX Initialization failed\r\n");
      return(XST_FAILURE);
    }

    // Load EDID
    XV_HdmiRx_DdcLoadEdid(HdmiRxSsPtr->HdmiRxPtr, HdmiRxSsPtr->EdidPtr,
        HdmiRxSsPtr->EdidLength);

  }
  return(XST_SUCCESS);
}

#ifdef XPAR_XHDCP_NUM_INSTANCES
/*****************************************************************************/
/**
* This function initializes the included sub-core to it's static configuration
*
* @param  HdmiRxSsPtr is a pointer to the Subsystem instance to be worked on.
*
* @return XST_SUCCESS/XST_FAILURE
*
******************************************************************************/
int XV_HdmiRxSs_SubcoreInitHdcpTimer(XV_HdmiRxSs *HdmiRxSsPtr)
{
  int Status;
  XTmrCtr_Config *ConfigPtr;

  if(HdmiRxSsPtr->HdcpTimerPtr)
  {
    /* Get core configuration */
#ifdef XV_HDMIRXSS_LOG_ENABLE
    XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_HDCPTIMER_INIT, 0);
#endif
    ConfigPtr  = XTmrCtr_LookupConfig(HdmiRxSsPtr->Config.HdcpTimer.DeviceId);
    if(ConfigPtr == NULL)
    {
      xdbg_printf(XDBG_DEBUG_GENERAL,"HDMIRXSS ERR:: AXIS Timer device not found\r\n");
      return(XST_FAILURE);
    }

    /* Setup the instance */
    memset(HdmiRxSsPtr->HdcpTimerPtr, 0, sizeof(XTmrCtr));

    /* Initialize core */
    XTmrCtr_CfgInitialize(HdmiRxSsPtr->HdcpTimerPtr,
                          ConfigPtr,
                          HdmiRxSsPtr->Config.HdcpTimer.AbsAddr);


    Status = XTmrCtr_InitHw(HdmiRxSsPtr->HdcpTimerPtr);

    /* Set Timer Counter instance in HDCP to the generic Hdcp1xRef
       that will be used in callbacks */
    HdmiRxSsPtr->Hdcp14Ptr->Hdcp1xRef = (void *)HdmiRxSsPtr->HdcpTimerPtr;

    /* Initialize the hdcp timer functions */
    XHdcp1x_SetTimerStart(HdmiRxSsPtr->Hdcp14Ptr,
      &XV_HdmiRxSs_HdcpTimerStart);
    XHdcp1x_SetTimerStop(HdmiRxSsPtr->Hdcp14Ptr,
      &XV_HdmiRxSs_HdcpTimerStop);
    XHdcp1x_SetTimerDelay(HdmiRxSsPtr->Hdcp14Ptr,
      &XV_HdmiRxSs_HdcpTimerBusyDelay);

    if(Status != XST_SUCCESS)
    {
      xdbg_printf(XDBG_DEBUG_GENERAL,"HDMIRXSS ERR:: AXI Timer Initialization failed\r\n");
      return(XST_FAILURE);
    }
  }
  return(XST_SUCCESS);
}
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
/*****************************************************************************/
/**
* This function initializes the included sub-core to it's static configuration
*
* @param  HdmiRxSsPtr is a pointer to the Subsystem instance to be worked on.
*
* @return XST_SUCCESS/XST_FAILURE
*
******************************************************************************/
int XV_HdmiRxSs_SubcoreInitHdcp14(XV_HdmiRxSs *HdmiRxSsPtr)
{
  int Status;
  XHdcp1x_Config *ConfigPtr;

  /* Is the HDCP 1.4 RX present? */
  if (HdmiRxSsPtr->Hdcp14Ptr) {

    /* Is the key loaded? */
    if (HdmiRxSsPtr->Hdcp14KeyPtr) {

      /* Get core configuration */
#ifdef XV_HDMIRXSS_LOG_ENABLE
      XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_HDCP14_INIT, 0);
#endif
      ConfigPtr  = XHdcp1x_LookupConfig(HdmiRxSsPtr->Config.Hdcp14.DeviceId);
      if(ConfigPtr == NULL)
      {
        xdbg_printf(XDBG_DEBUG_GENERAL,"HDMIRXSS ERR:: HDCP 1.4 device not found\r\n");
        return(XST_FAILURE);
      }

      /* Initialize core */
      void *PhyIfPtr = HdmiRxSsPtr->HdmiRxPtr;
      Status = XHdcp1x_CfgInitialize(HdmiRxSsPtr->Hdcp14Ptr,
                                        ConfigPtr,
                                        PhyIfPtr,
                                        HdmiRxSsPtr->Config.Hdcp14.AbsAddr);

      /* Self-test the hdcp interface */
      if (XHdcp1x_SelfTest(HdmiRxSsPtr->Hdcp14Ptr) != XST_SUCCESS) {
          Status = XST_FAILURE;
      }

      if(Status != XST_SUCCESS)
      {
        xdbg_printf(XDBG_DEBUG_GENERAL,"HDMIRXSS ERR:: HDCP 1.4 Initialization failed\r\n");
        return(XST_FAILURE);
      }

      /* Set-up the DDC Handlers */
      XHdcp1x_SetCallback(HdmiRxSsPtr->Hdcp14Ptr,
                          XHDCP1X_HANDLER_DDC_SETREGADDR,
						  (void *)(XHdcp1x_SetDdcHandler)XV_HdmiRxSs_DdcSetRegAddrHandler,
						  (void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp1x_SetCallback(HdmiRxSsPtr->Hdcp14Ptr,
                          XHDCP1X_HANDLER_DDC_SETREGDATA,
						  (void *)(XHdcp1x_SetDdcHandler)XV_HdmiRxSs_DdcSetRegDataHandler,
						  (void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp1x_SetCallback(HdmiRxSsPtr->Hdcp14Ptr,
                          XHDCP1X_HANDLER_DDC_GETREGDATA,
						  (void *)(XHdcp1x_GetDdcHandler)XV_HdmiRxSs_DdcGetRegDataHandler,
						  (void *)HdmiRxSsPtr->HdmiRxPtr);

      /* Select key */
      XHdcp1x_SetKeySelect(HdmiRxSsPtr->Hdcp14Ptr, XV_HDMIRXSS_HDCP_KEYSEL);

      /* Disable HDCP 1.4 repeater */
      HdmiRxSsPtr->Hdcp14Ptr->IsRepeater = 0;

      /* Set-up the HDMI RX HDCP Callback Handler */
      XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                XV_HDMIRX_HANDLER_HDCP,
				(void *)XV_HdmiRxSs_DdcHdcpCallback,
                (void *)HdmiRxSsPtr);

      /* Enable HDMI-RX DDC interrupts */
      XV_HdmiRx_DdcIntrEnable(HdmiRxSsPtr->HdmiRxPtr);

      /* Enable HDMI-RX HDCP */
      XV_HdmiRx_DdcHdcpEnable(HdmiRxSsPtr->HdmiRxPtr);

      /* Clear the HDCP KSV Fifo */
      XV_HdmiRx_DdcHdcpClearReadMessageBuffer(HdmiRxSsPtr->HdmiRxPtr);

      /* Clear the event queue */
      XV_HdmiRxSs_HdcpClearEvents(HdmiRxSsPtr);
    }
  }
  return(XST_SUCCESS);
}
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
/*****************************************************************************/
/**
* This function initializes the included sub-core to it's static configuration
*
* @param  HdmiRxSsPtr is a pointer to the Subsystem instance to be worked on.
*
* @return XST_SUCCESS/XST_FAILURE
*
******************************************************************************/
int XV_HdmiRxSs_SubcoreInitHdcp22(XV_HdmiRxSs *HdmiRxSsPtr)
{
  int Status;
  XHdcp22_Rx_Config *ConfigPtr;

  /* Is the HDCP 2.2 RX present? */
  if (HdmiRxSsPtr->Hdcp22Ptr) {

    /* Are the keys loaded? */
    if (HdmiRxSsPtr->Hdcp22Lc128Ptr && HdmiRxSsPtr->Hdcp22PrivateKeyPtr) {

      /* Get core configuration */
#ifdef XV_HDMIRXSS_LOG_ENABLE
      XV_HdmiRxSs_LogWrite(HdmiRxSsPtr, XV_HDMIRXSS_LOG_EVT_HDCP22_INIT, 0);
#endif
      ConfigPtr  = XHdcp22Rx_LookupConfig(HdmiRxSsPtr->Config.Hdcp22.DeviceId);
      if(ConfigPtr == NULL)
      {
        xdbg_printf(XDBG_DEBUG_GENERAL,"HDMIRXSS ERR:: HDCP 2.2 device not found\r\n");
        return (XST_FAILURE);
      }

      /* Initialize core */
      Status = XHdcp22Rx_CfgInitialize(HdmiRxSsPtr->Hdcp22Ptr,
                                       ConfigPtr,
                                       HdmiRxSsPtr->Config.Hdcp22.AbsAddr);
      if (Status != XST_SUCCESS)
      {
        xdbg_printf(XDBG_DEBUG_GENERAL,"HDMIRXSS ERR:: HDCP 2.2 Initialization failed\r\n");
        return(XST_FAILURE);
      }

      /* Set-up the DDC Handlers */
      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_SETREGADDR,
							(void *)(XHdcp22_Rx_SetHandler)XV_HdmiRxSs_DdcSetRegAddrHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_SETREGDATA,
							(void *)(XHdcp22_Rx_SetHandler)XV_HdmiRxSs_DdcSetRegDataHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_GETREGDATA,
							(void *)(XHdcp22_Rx_GetHandler)XV_HdmiRxSs_DdcGetRegDataHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_GETWBUFSIZE,
							(void *)(XHdcp22_Rx_GetHandler)XV_HdmiRxSs_DdcGetWriteMessageBufferWordsHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_GETRBUFSIZE,
							(void *)(XHdcp22_Rx_GetHandler)XV_HdmiRxSs_DdcGetReadMessageBufferWordsHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_ISWBUFEMPTY,
							(void *)(XHdcp22_Rx_GetHandler)XV_HdmiRxSs_DdcIsWriteMessageBufferEmptyHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_ISRBUFEMPTY,
							(void *)(XHdcp22_Rx_GetHandler)XV_HdmiRxSs_DdcIsReadMessageBufferEmptyHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_CLEARRBUF,
							(void *)(XHdcp22_Rx_RunHandler)XV_HdmiRxSs_DdcClearReadMessageBufferHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);

      XHdcp22Rx_SetCallback(HdmiRxSsPtr->Hdcp22Ptr,
                            XHDCP22_RX_HANDLER_DDC_CLEARWBUF,
							(void *)(XHdcp22_Rx_RunHandler)XV_HdmiRxSs_DdcClearWriteMessageBufferHandler,
							(void *)HdmiRxSsPtr->HdmiRxPtr);


      /* Set-up the HDMI RX HDCP Callback Handler */
      XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                XV_HDMIRX_HANDLER_HDCP,
				(void *)XV_HdmiRxSs_DdcHdcpCallback,
                (void *)HdmiRxSsPtr);

      /* Set-up the HDMI RX Link error Callback Handler */
      XV_HdmiRx_SetCallback(HdmiRxSsPtr->HdmiRxPtr,
                XV_HDMIRX_HANDLER_LINK_ERROR,
				(void *)XV_HdmiRxSs_LinkErrorCallback,
                (void *)HdmiRxSsPtr);

      /* Load Production Keys */
      XHdcp22Rx_LoadLc128(HdmiRxSsPtr->Hdcp22Ptr, HdmiRxSsPtr->Hdcp22Lc128Ptr);
      XHdcp22Rx_LoadPublicCert(HdmiRxSsPtr->Hdcp22Ptr, HdmiRxSsPtr->Hdcp22PrivateKeyPtr+40);
      XHdcp22Rx_LoadPrivateKey(HdmiRxSsPtr->Hdcp22Ptr, HdmiRxSsPtr->Hdcp22PrivateKeyPtr+562);
#ifdef XV_HDMIRXSS_LOG_ENABLE
      XHdcp22Rx_LogReset(HdmiRxSsPtr->Hdcp22Ptr, FALSE);
#endif
      /* Enable HDMI-RX DDC interrupts */
      XV_HdmiRx_DdcIntrEnable(HdmiRxSsPtr->HdmiRxPtr);

      /* Enable HDMI-RX HDCP */
      XV_HdmiRx_DdcHdcpEnable(HdmiRxSsPtr->HdmiRxPtr);

      /* Clear the event queue */
      XV_HdmiRxSs_HdcpClearEvents(HdmiRxSsPtr);
    }
  }

  return (XST_SUCCESS);
}
#endif

#ifdef USE_HDCP_RX
/*****************************************************************************/
/**
 *
 * This is the DDC set register address handler for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @param Data is the address to be written.
 *
 * @return None.
 *
 ******************************************************************************/
static void XV_HdmiRxSs_DdcSetRegAddrHandler(void *RefPtr, u32 Data)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  XV_HdmiRx_DdcHdcpSetAddress(InstancePtr, Data);
}
#endif

#ifdef USE_HDCP_RX
/*****************************************************************************/
/**
 *
 * This is the DDC set register data handler for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @param Data is the data to be written.
 *
 * @return None.
 *
 ******************************************************************************/
static void XV_HdmiRxSs_DdcSetRegDataHandler(void *RefPtr, u32 Data)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  XV_HdmiRx_DdcHdcpWriteData(InstancePtr, Data);
}
#endif

#ifdef USE_HDCP_RX
/*****************************************************************************/
/**
 *
 * This is the DDC get register data handler for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @return The read data.
 *
 ******************************************************************************/
static u32 XV_HdmiRxSs_DdcGetRegDataHandler(void *RefPtr)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  return XV_HdmiRx_DdcHdcpReadData(InstancePtr);
}
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
/*****************************************************************************/
/**
 *
 * This is the DDC get write message buffer words handler for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @return The number of words in the Write Message Buffer.
 *
 ******************************************************************************/
static u32 XV_HdmiRxSs_DdcGetWriteMessageBufferWordsHandler(void *RefPtr)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  return XV_HdmiRx_DdcGetHdcpWriteMessageBufferWords(InstancePtr);
}
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
/*****************************************************************************/
/**
 *
 * This is the DDC get read message buffer words handler for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @return The number of words in the Read Message Buffer.
 *
 ******************************************************************************/
static u32 XV_HdmiRxSs_DdcGetReadMessageBufferWordsHandler(void *RefPtr)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  return XV_HdmiRx_DdcGetHdcpReadMessageBufferWords(InstancePtr);
}
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
/*****************************************************************************/
/**
 *
 * This is the DDC get read message buffer is empty handler for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @return
 *  - TRUE if read message buffer is empty.
 *  - FALSE if read message buffer is not empty.
 *
 ******************************************************************************/
static u32 XV_HdmiRxSs_DdcIsReadMessageBufferEmptyHandler(void *RefPtr)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  return XV_HdmiRx_DdcIsHdcpReadMessageBufferEmpty(InstancePtr);
}
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
/*****************************************************************************/
/**
 *
 * This is the DDC get write message buffer is empty handler for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @return
 *  - TRUE if write message buffer is empty.
 *  - FALSE if write message buffer is not empty.
 *
 ******************************************************************************/
static u32 XV_HdmiRxSs_DdcIsWriteMessageBufferEmptyHandler(void *RefPtr)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  return XV_HdmiRx_DdcIsHdcpWriteMessageBufferEmpty(InstancePtr);
}
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
/*****************************************************************************/
/**
 *
 * This is the DDC clear read message buffer handler for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @return None
 *
 ******************************************************************************/
static void XV_HdmiRxSs_DdcClearReadMessageBufferHandler(void *RefPtr)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  XV_HdmiRx_DdcHdcpClearReadMessageBuffer(InstancePtr);
}
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
/*****************************************************************************/
/**
 *
 * This is the DDC clear write message buffer for the RX.
 *
 * @param RefPtr is a callback reference to the HDMI RX instance.
 *
 * @return None
 *
 ******************************************************************************/
static void XV_HdmiRxSs_DdcClearWriteMessageBufferHandler(void *RefPtr)
{
  XV_HdmiRx *InstancePtr = (XV_HdmiRx *)RefPtr;
  XV_HdmiRx_DdcHdcpClearWriteMessageBuffer(InstancePtr);
}
#endif

#ifdef USE_HDCP_RX
/*****************************************************************************/
/**
* This function is called when the HDMI-RX DDC HDCP interrupt has occurred.
*
* @param RefPtr is a callback reference to the HDCP22 RX instance.
* @param Type indicates the cause of the interrupt.
*
* @return None.
*
* @note   None.
******************************************************************************/
static void XV_HdmiRxSs_DdcHdcpCallback(void *RefPtr, int Type)
{
  XV_HdmiRxSs *HdmiRxSsPtr;
  HdmiRxSsPtr = (XV_HdmiRxSs*) RefPtr;

  switch (Type)
  {
    // HDCP 2.2. write message event
    case XV_HDMIRX_DDC_STA_HDCP_WMSG_NEW_EVT_MASK:
#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
      XHdcp22Rx_SetWriteMessageAvailable(HdmiRxSsPtr->Hdcp22Ptr);
#endif
      break;

    // HDCP 2.2 read message event
    case XV_HDMIRX_DDC_STA_HDCP_RMSG_END_EVT_MASK:
#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
      XHdcp22Rx_SetReadMessageComplete(HdmiRxSsPtr->Hdcp22Ptr);
#endif
      break;

    // HDCP 2.2 read not complete event
    case XV_HDMIRX_DDC_STA_HDCP_RMSG_NC_EVT_MASK:
#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
      XHdcp22Rx_SetDdcError(HdmiRxSsPtr->Hdcp22Ptr);
#endif
      break;

    // HDCP 1.4 Aksv event
    case XV_HDMIRX_DDC_STA_HDCP_AKSV_EVT_MASK:
#ifdef XPAR_XHDCP_NUM_INSTANCES
      XHdcp1x_ProcessAKsv(HdmiRxSsPtr->Hdcp14Ptr);
#endif
      break;

    // HDCP 1.4 protocol event
    case XV_HDMIRX_DDC_STA_HDCP_1_PROT_EVT_MASK:
#ifdef USE_HDCP_RX
      XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_1_PROT_EVT);
#endif
      break;

    // HDCP 2.2 protocol event
    case XV_HDMIRX_DDC_STA_HDCP_2_PROT_EVT_MASK:
#ifdef USE_HDCP_RX
      XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_2_PROT_EVT);
#endif
      break;

    default:
      break;
  }
}
#endif

#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
/*****************************************************************************/
/**
* This function is called when the HDMI-RX link error has occurred.
*
* @param RefPtr is a callback reference to the HDCP22 RX instance.
*
* @return None.
*
* @note   None.
******************************************************************************/
static void XV_HdmiRxSs_LinkErrorCallback(void *RefPtr)
{
  XV_HdmiRxSs *HdmiRxSsPtr;
  HdmiRxSsPtr = (XV_HdmiRxSs*) RefPtr;

  // HDCP 2.2
  if (HdmiRxSsPtr->HdcpProtocol == XV_HDMIRXSS_HDCP_22) {
    XHdcp22Rx_SetLinkError(HdmiRxSsPtr->Hdcp22Ptr);
  }
}
#endif

/** @} */

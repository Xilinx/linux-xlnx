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
 * @file xv_hdmirxss_log.c
 *
 *
 * @note    None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   YH   17/08/16 Initial release.
 * 1.01  MMO  03/01/17 Add compiler option(XV_HDMIRXSS_LOG_ENABLE) to enable Log
 * </pre>
 *
*******************************************************************************/

/******************************* Include Files ********************************/

#include "xv_hdmirxss.h"
#include "xil_printf.h"

/**************************** Function Prototypes *****************************/

/**************************** Function Definitions ****************************/
#ifdef XV_HDMIRXSS_LOG_ENABLE
/*****************************************************************************/
/**
* This function will reset the driver's logging mechanism.
*
* @param    InstancePtr is a pointer to the xv_hdmirxss core instance.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XV_HdmiRxSs_LogReset(XV_HdmiRxSs *InstancePtr)
{
    /* Verify arguments. */
    Xil_AssertVoid(InstancePtr != NULL);

    InstancePtr->Log.HeadIndex = 0;
    InstancePtr->Log.TailIndex = 0;
}

/*****************************************************************************/
/**
* This function will insert an event in the driver's logginc mechanism.
*
* @param    InstancePtr is a pointer to the XV_HdmiRxSs core instance.
* @param    Evt is the event type to log.
* @param    Data is the associated data for the event.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XV_HdmiRxSs_LogWrite(XV_HdmiRxSs *InstancePtr, XV_HdmiRxSs_LogEvent Evt, u8 Data)
{
    /* Verify arguments. */
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(Evt <= (XV_HDMIRXSS_LOG_EVT_DUMMY));
    Xil_AssertVoid(Data < 0xFF);

    /* Write data and event into log buffer */
    InstancePtr->Log.DataBuffer[InstancePtr->Log.HeadIndex] =
            (Data << 8) | Evt;

    /* Update head pointer if reached to end of the buffer */
    if (InstancePtr->Log.HeadIndex ==
            (u8)((sizeof(InstancePtr->Log.DataBuffer) / 2) - 1)) {
        /* Clear pointer */
        InstancePtr->Log.HeadIndex = 0;
    }
    else {
        /* Increment pointer */
        InstancePtr->Log.HeadIndex++;
    }

    /* Check tail pointer. When the two pointer are equal, then the buffer
     * is full. In this case then increment the tail pointer as well to
     * remove the oldest entry from the buffer. */
    if (InstancePtr->Log.TailIndex == InstancePtr->Log.HeadIndex) {
        if (InstancePtr->Log.TailIndex ==
            (u8)((sizeof(InstancePtr->Log.DataBuffer) / 2) - 1)) {
            InstancePtr->Log.TailIndex = 0;
        }
        else {
            InstancePtr->Log.TailIndex++;
        }
    }
}

/*****************************************************************************/
/**
* This function will read the last event from the log.
*
* @param    InstancePtr is a pointer to the XV_HdmiRxSs core instance.
*
* @return   The log data.
*
* @note     None.
*
******************************************************************************/
u16 XV_HdmiRxSs_LogRead(XV_HdmiRxSs *InstancePtr)
{
    u16 Log;

    /* Verify argument. */
    Xil_AssertNonvoid(InstancePtr != NULL);

    /* Check if there is any data in the log */
    if (InstancePtr->Log.TailIndex == InstancePtr->Log.HeadIndex) {
        Log = 0;
    }
    else {
        Log = InstancePtr->Log.DataBuffer[InstancePtr->Log.TailIndex];

        /* Increment tail pointer */
        if (InstancePtr->Log.TailIndex ==
            (u8)((sizeof(InstancePtr->Log.DataBuffer) / 2) - 1)) {
            InstancePtr->Log.TailIndex = 0;
        }
        else {
            InstancePtr->Log.TailIndex++;
        }
    }

    return Log;
}
#endif

/*****************************************************************************/
/**
* This function will print the entire log.
*
* @param    InstancePtr is a pointer to the XV_HdmiRxSs core instance.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XV_HdmiRxSs_LogDisplay(XV_HdmiRxSs *InstancePtr)
{
#ifdef XV_HDMIRXSS_LOG_ENABLE
    u16 Log;
    u8 Evt;
    u8 Data;

    /* Verify argument. */
    Xil_AssertVoid(InstancePtr != NULL);

    xil_printf("\r\n\n\nHDMI RX log\r\n");
    xil_printf("------\r\n");

    /* Read log data */
    Log = XV_HdmiRxSs_LogRead(InstancePtr);

    while (Log != 0) {
        /* Event */
        Evt = Log & 0xff;

        /* Data */
        Data = (Log >> 8) & 0xFF;
        Data = Data;

        switch (Evt) {
        case (XV_HDMIRXSS_LOG_EVT_NONE):
            xil_printf("HDMI RXSS log end\r\n-------\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDMIRX_INIT):
            xil_printf("Initializing HDMI RX core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_VTC_INIT):
            xil_printf("Initializing VTC core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDCPTIMER_INIT):
            xil_printf("Initializing AXI Timer core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDCP14_INIT):
            xil_printf("Initializing HDCP 1.4 core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDCP22_INIT):
            xil_printf("Initializing HDCP 2.2 core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_START):
            xil_printf("Start HDMI RX Subsystem....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_STOP):
            xil_printf("Stop HDMI RX Subsystem....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_RESET):
            xil_printf("Reset HDMI RX Subsystem....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_CONNECT):
            xil_printf("RX cable is connected....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_DISCONNECT):
            xil_printf("RX cable is disconnected....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_LINKSTATUS):
            xil_printf("RX Link Status Error....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_STREAMUP):
            xil_printf("RX Stream is Up\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_STREAMDOWN):
            xil_printf("RX Stream is Down\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_STREAMINIT):
            xil_printf("RX Stream Start\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_SETSTREAM):
            xil_printf("RX Stream Init\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_REFCLOCKCHANGE):
            xil_printf("RX TMDS reference clock change\r\n");
            break;
         case (XV_HDMIRXSS_LOG_EVT_HDCP14):
              if (Data) {
                xil_printf("RX HDCP 1.4 Enabled\r\n");
              } else {
                xil_printf("RX HDCP 1.4 Disabled\r\n");
              }
            break;
         case (XV_HDMIRXSS_LOG_EVT_HDCP22):
              if (Data) {
                xil_printf("RX HDCP 2.2 Enabled\r\n");
              } else {
                xil_printf("RX HDCP 2.2 Disabled\r\n");
              }
            break;
        case (XV_HDMIRXSS_LOG_EVT_DVIMODE):
            xil_printf("RX mode changed to DVI\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDMIMODE):
            xil_printf("RX mode changed to HDMI\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_SYNCLOSS):
            xil_printf("RX Sync Loss detected\r\n");
            break;
        default:
            xil_printf("Unknown event\r\n");
            break;
        }

        /* Read log data */
        Log = XV_HdmiRxSs_LogRead(InstancePtr);
    }
#else
    xil_printf("\r\n INFO:: HDMIRXSS Log Feature is Disabled \r\n");
#endif
}

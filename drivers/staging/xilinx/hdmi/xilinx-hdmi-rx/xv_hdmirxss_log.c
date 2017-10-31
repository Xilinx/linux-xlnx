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
 *
 * 1.4   YH   07/07/17 Add new log type XV_HDMIRXSS_LOG_EVT_SETSTREAM_ERR
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
* @param	buff Buffer to print to
* @param	buff_size size off buff passed
*
* @return   number of characters written to buff
*
* @note     None.
*
******************************************************************************/
int XV_HdmiRxSs_LogShow(XV_HdmiRxSs *InstancePtr, char *buff, int buff_size)
{
	int strSize = 0;
#ifdef XV_HDMIRXSS_LOG_ENABLE
    u16 Log;
    u8 Evt;
    u8 Data;

    /* Verify argument. */
    Xil_AssertVoid(InstancePtr != NULL);

	strSize = scnprintf(buff+strSize, buff_size-strSize,
			"\r\n\n\nHDMI RX log\r\n" \
			"------\r\n");

    /* Read log data */
    Log = XV_HdmiRxSs_LogRead(InstancePtr);

    while (Log != 0 && (buff_size - strSize) > 30 ) {
        /* Event */
        Evt = Log & 0xff;

        /* Data */
        Data = (Log >> 8) & 0xFF;

        switch (Evt) {
        case (XV_HDMIRXSS_LOG_EVT_NONE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"HDMI RXSS log end\r\n-------\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDMIRX_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing HDMI RX core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_VTC_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing VTC core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDCPTIMER_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing AXI Timer core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDCP14_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing HDCP 1.4 core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDCP22_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing HDCP 2.2 core....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_START):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Start HDMI RX Subsystem....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_STOP):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Stop HDMI RX Subsystem....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_RESET):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Reset HDMI RX Subsystem....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_CONNECT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX cable is connected....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_DISCONNECT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX cable is disconnected....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_LINKSTATUS):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX Link Status Error....\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_STREAMUP):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX Stream is Up\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_STREAMDOWN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX Stream is Down\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_STREAMINIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX Stream Start\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_SETSTREAM):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX Stream Init\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_SETSTREAM_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: RX Stream Reference Clock = 0\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_REFCLOCKCHANGE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX TMDS reference clock change\r\n");
            break;
         case (XV_HDMIRXSS_LOG_EVT_HDCP14):
              if (Data) {
            		strSize += scnprintf(buff+strSize, buff_size-strSize,
            				"RX HDCP 1.4 Enabled\r\n");
              } else {
            		strSize += scnprintf(buff+strSize, buff_size-strSize,
            				"RX HDCP 1.4 Disabled\r\n");
              }
            break;
         case (XV_HDMIRXSS_LOG_EVT_HDCP22):
              if (Data) {
            		strSize += scnprintf(buff+strSize, buff_size-strSize,
            				"RX HDCP 2.2 Enabled\r\n");
              } else {
            		strSize += scnprintf(buff+strSize, buff_size-strSize,
            				"RX HDCP 2.2 Disabled\r\n");
              }
            break;
        case (XV_HDMIRXSS_LOG_EVT_DVIMODE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX mode changed to DVI\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_HDMIMODE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX mode changed to HDMI\r\n");
            break;
        case (XV_HDMIRXSS_LOG_EVT_SYNCLOSS):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX Sync Loss detected\r\n");
            break;
        default:
        	strSize += scnprintf(buff+strSize, buff_size-strSize,
        			"Unknown event: %i\r\n", Evt);
            break;
        }

		if((buff_size - strSize) > 30) {
	        /* Read log data */
	        Log = XV_HdmiRxSs_LogRead(InstancePtr);
		} else {
			Log = 0;
		}
    }
#else
	strSize += scnprintf(buff+strSize, buff_size-strSize,
			"\r\n INFO:: HDMIRXSS Log Feature is Disabled \r\n");
#endif
    return strSize;
}

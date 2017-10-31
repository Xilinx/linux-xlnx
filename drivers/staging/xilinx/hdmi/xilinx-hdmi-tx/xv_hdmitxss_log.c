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
 * @file xv_hdmitxss_log.c
 *
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   YH   17/08/16 Initial release.
 * 1.01  MMO  03/01/17 Add compiler option(XV_HDMITXSS_LOG_ENABLE) to enable Log
 * </pre>
 *
*******************************************************************************/

/******************************* Include Files ********************************/

#include "xv_hdmitxss.h"
#include "xil_printf.h"

/**************************** Function Prototypes *****************************/

/**************************** Function Definitions ****************************/

/*****************************************************************************/
/**
* This function will reset the driver's logging mechanism.
*
* @param	InstancePtr is a pointer to the xv_hdmitxss core instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
#ifdef XV_HDMITXSS_LOG_ENABLE
void XV_HdmiTxSs_LogReset(XV_HdmiTxSs *InstancePtr)
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
* @param	InstancePtr is a pointer to the XV_HdmiTxSs core instance.
* @param	Evt is the event type to log.
* @param	Data is the associated data for the event.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XV_HdmiTxSs_LogWrite(XV_HdmiTxSs *InstancePtr, XV_HdmiTxSs_LogEvent Evt, u8 Data)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Evt <= (XV_HDMITXSS_LOG_EVT_DUMMY));
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
* @param	InstancePtr is a pointer to the XV_HdmiTxSs core instance.
*
* @return	The log data.
*
* @note		None.
*
******************************************************************************/
u16 XV_HdmiTxSs_LogRead(XV_HdmiTxSs *InstancePtr)
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
* This function will print the entire log to the passed buffer.
*
* @param	InstancePtr is a pointer to the XV_HdmiTxSs core instance.
* @param	buff Buffer to print to
* @param	buff_size size off buff passed
*
* @return	The number of characters written to buff.
*
* @note		None.
*
******************************************************************************/
int XV_HdmiTxSs_LogShow(XV_HdmiTxSs *InstancePtr, char *buff, int buff_size)
{
	int strSize = 0;
#ifdef XV_HDMITXSS_LOG_ENABLE
	u16 Log;
	u8 Evt;
	u8 Data;

	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

	strSize = scnprintf(buff+strSize, buff_size-strSize,
			"\r\n\n\nHDMI TX log\r\n" \
			"------\r\n");

	/* Read log data */
	Log = XV_HdmiTxSs_LogRead(InstancePtr);

	while (Log != 0 && (buff_size - strSize) > 30 ) {
		/* Event */
		Evt = Log & 0xff;

		/* Data */
		Data = (Log >> 8) & 0xFF;

		switch (Evt) {
		case (XV_HDMITXSS_LOG_EVT_NONE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"HDMI TXSS log end\r\n-------\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_HDMITX_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing HDMI TX core....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_VTC_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing VTC core....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_HDCPTIMER_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing AXI Timer core....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_HDCP14_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing HDCP 1.4 core....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_HDCP22_INIT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Initializing HDCP 2.2 core....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_START):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Start HDMI TX Subsystem....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_STOP):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Stop HDMI TX Subsystem....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_RESET):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Reset HDMI TX Subsystem....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_CONNECT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX cable is connected....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_DISCONNECT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX cable is disconnected....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_TOGGLE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX cable is toggled....\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_STREAMUP):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX Stream is Up\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_STREAMDOWN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX Stream is Down\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_STREAMSTART):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX Stream Start\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_SETAUDIOCHANNELS):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX Set Audio Channels (%0d)\r\n", Data);
			break;
	    case (XV_HDMITXSS_LOG_EVT_AUDIOMUTE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX Audio Muted\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_AUDIOUNMUTE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX Audio Unmuted\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_SETSTREAM):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX Set Stream, with TMDS (%0d)\r\n", Data);
			break;
	    case (XV_HDMITXSS_LOG_EVT_HDCP14_AUTHREQ):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX HDCP 1.4 authentication request\r\n");
			break;
	    case (XV_HDMITXSS_LOG_EVT_HDCP22_AUTHREQ):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX HDCP 2.2 authentication request\r\n");
			break;
		default:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Unknown event: %i\r\n", Data);
			break;
		}

		if((buff_size - strSize) > 30) {
	        /* Read log data */
	        Log = XV_HdmiTxSs_LogRead(InstancePtr);
		} else {
			Log = 0;
		}
	}
#else
	strSize += scnprintf(buff+strSize, buff_size-strSize,
			"\r\n INFO:: HDMITXSS Log Feature is Disabled \r\n");
#endif
    return strSize;
}

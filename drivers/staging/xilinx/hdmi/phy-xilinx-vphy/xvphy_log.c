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
 * @file xvphy.c
 *
 * Contains a minimal set of functions for the XVphy driver that allow access
 * to all of the Video PHY core's functionality. See xvphy.h for a detailed
 * description of the driver.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   als  10/19/15 Initial release.
 * 1.1   gm   02/01/16 Additional events for event log printout
 * 1.2   gm            Added log events for debugging
 * 1.4   gm   11/24/16 Made debug log optional (can be disabled via makefile)
 *                     Added XVPHY_LOG_EVT_TX_ALIGN_TMOUT log event
 * 1.6   gm   06/08/17 Added XVPHY_LOG_EVT_HDMI20_ERR log event
 *                     Added Red & Yellow printing for errors and warnings
 *                     Added XVPHY_LOG_EVT_NO_QPLL_ERR log event
 *                     Changed xil_printf new lines to \r\n
 *                     Added XVPHY_LOG_EVT_DRU_CLK_ERR log event
 * </pre>
 *
*******************************************************************************/

/******************************* Include Files ********************************/

#include "xvphy.h"
#include "xvphy_i.h"

/**************************** Function Prototypes *****************************/

/**************************** Function Definitions ****************************/

/*****************************************************************************/
/**
* This function will reset the driver's logginc mechanism.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_LogReset(XVphy *InstancePtr)
{
#ifdef XV_VPHY_LOG_ENABLE
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->Log.HeadIndex = 0;
	InstancePtr->Log.TailIndex = 0;
#endif
}

#ifdef XV_VPHY_LOG_ENABLE
/*****************************************************************************/
/**
* This function will insert an event in the driver's logginc mechanism.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	Evt is the event type to log.
* @param	Data is the associated data for the event.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_LogWrite(XVphy *InstancePtr, XVphy_LogEvent Evt, u8 Data)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Evt <= (XVPHY_LOG_EVT_DUMMY));
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
#endif

/*****************************************************************************/
/**
* This function will read the last event from the log.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
*
* @return	The log data.
*
* @note		None.
*
******************************************************************************/
u16 XVphy_LogRead(XVphy *InstancePtr)
{
#ifdef XV_VPHY_LOG_ENABLE
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
#endif
}

/*****************************************************************************/
/**
* This function will print the entire log to the passed buffer.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	buff Buffer to print to
* @param	buff_size size off buff passed
*
* @return	number of bytes written to buff.
*
* @note		None.
*
******************************************************************************/
int XVphy_LogShow(XVphy *InstancePtr, char *buff, int buff_size)
{
	int strSize = 0;
#ifdef XV_VPHY_LOG_ENABLE
	u16 Log;
	u8 Evt;
	u8 Data;

	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

	strSize = scnprintf(buff, buff_size,
			"\r\n\n\nVPHY log\r\n" \
			"------\r\n");

	/* Read log data */
	Log = XVphy_LogRead(InstancePtr);

	while (Log != 0 && (buff_size - strSize) > 30 ) {
		/* Event */
		Evt = Log & 0xff;

		/* Data */
		Data = (Log >> 8) & 0xFF;

		switch (Evt) {
		case (XVPHY_LOG_EVT_NONE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"GT log end\r\n-------\r\n");
			break;
		case (XVPHY_LOG_EVT_QPLL_EN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"QPLL enable (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_QPLL_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"QPLL reset (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_CPLL_EN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"CPLL enable (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_CPLL_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"CPLL reset (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_TXPLL_EN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"TX MMCM enable (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_TXPLL_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"TX MMCM reset (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_RXPLL_EN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"RX MMCM enable (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_RXPLL_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"RX MMCM reset (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_GTRX_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"GT RX reset (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_GTTX_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"GT TX reset (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_VID_TX_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Video TX reset (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_VID_RX_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Video RX reset (%0d)\r\n", Data);
			break;
		case (XVPHY_LOG_EVT_TX_ALIGN):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX alignment done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX alignment start.\r\n.");
			}
			break;
		case (XVPHY_LOG_EVT_TX_ALIGN_TMOUT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX alignment watchdog timed out.\r\n");
			break;
		case (XVPHY_LOG_EVT_TX_TMR):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX timer event\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX timer load\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_RX_TMR):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX timer event\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX timer load\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_CPLL_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"CPLL reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"CPLL reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_GT_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_GT_TX_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT TX reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT TX reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_GT_RX_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT RX reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT RX reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_QPLL_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"QPLL reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"QPLL reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_PLL0_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"PLL0 reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"PLL0 reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_PLL1_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"PLL1 reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"PLL1 reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_INIT):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT init done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT init start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_TXPLL_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX MMCM reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX MMCM reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_RXPLL_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX MMCM reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX MMCM reconfig start\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_QPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"QPLL lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"QPLL lost lock\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_PLL0_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"PLL0 lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"PLL0 lost lock\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_PLL1_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"PLL1 lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"PLL1 lost lock\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_CPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"CPLL lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"CPLL lost lock\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_RXPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX MMCM lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX MMCM lost lock\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_TXPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX MMCM lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX MMCM lost lock\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_TX_RST_DONE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX reset done\r\n");
			break;
		case (XVPHY_LOG_EVT_RX_RST_DONE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX reset done\r\n");
			break;
		case (XVPHY_LOG_EVT_TX_FREQ):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX frequency event\r\n");
			break;
		case (XVPHY_LOG_EVT_RX_FREQ):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX frequency event\r\n");
			break;
		case (XVPHY_LOG_EVT_DRU_EN):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"DRU enable\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"DRU disable\r\n");
			}
			break;
		case (XVPHY_LOG_EVT_GT_PLL_LAYOUT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: Couldn't find the correct GT "
					"parameters for this video resolution.\n\r");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Try another GT PLL layout.\n\r");
			break;
		case (XVPHY_LOG_EVT_GT_UNBONDED):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"WARNING: "
					"Transmitter cannot be used on\r\n");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"         "
					"bonded mode when DRU is enabled\r\n");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Switch to unbonded PLL layout\r\n");
			break;
		case (XVPHY_LOG_EVT_1PPC_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: The Video PHY cannot support this video ");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"format at PPC = 1\r\n");
			break;
		case (XVPHY_LOG_EVT_PPC_MSMTCH_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: HDMI TX SS PPC value, doesn't match with"
					" VPhy PPC value\r\n");
			break;
		case (XVPHY_LOG_EVT_VDCLK_HIGH_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: GTPE2 Video PHY cannot"
								"support resolutions");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"\r\n\twith video clock > 148.5 MHz.\r\n");
			break;
		case (XVPHY_LOG_EVT_NO_DRU):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Warning: No DRU instance found. ");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Low resolution video isn't supported in "
					"this version.\r\n");
			break;
		case (XVPHY_LOG_EVT_GT_QPLL_CFG_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: QPLL config not found!\r\n");
			break;
		case (XVPHY_LOG_EVT_GT_CPLL_CFG_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: CPLL config not found!\r\n");
			break;
		case (XVPHY_LOG_EVT_VD_NOT_SPRTD_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: This video format is not "
					"supported by this device\r\n");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"         "
					"Change to another format\r\n");
			break;
		case (XVPHY_LOG_EVT_MMCM_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: MMCM config not found!\r\n");
			break;
		case (XVPHY_LOG_EVT_HDMI20_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error!  The Video PHY doesn't "
					"support HDMI 2.0 line rates\r\n");
			break;
		case (XVPHY_LOG_EVT_NO_QPLL_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error!  There's no QPLL instance in the design\r\n");
			break;
		case (XVPHY_LOG_EVT_DRU_CLK_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error!  Wrong DRU REFCLK frequency detected\r\n");
			break;
		default:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Unknown event %i\r\n", Evt);
			break;
		}

		if((buff_size - strSize) > 30) {
			/* Read log data */
			Log = XVphy_LogRead(InstancePtr);
		} else {
			Log = 0;
		}
	}
#else
	strSize += scnprintf(buff+strSize, buff_size-strSize,
			"\r\nINFO:: VPHY Log Feature is Disabled \r\n");
#endif
    return strSize;
}

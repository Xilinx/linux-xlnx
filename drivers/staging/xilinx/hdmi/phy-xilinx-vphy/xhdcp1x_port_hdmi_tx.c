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
* @file xhdcp1x_port_hdmi_tx.c
* @addtogroup hdcp1x_v4_0
* @{
*
* This contains the implementation of the HDCP port driver for HDMI TX
* interfaces
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 2.00  MG     01/20/16 Assigned callback function in
*              XHdcp1x_PortHdmiTxAdaptor to NULL
* 2.10  MG     02/29/16 Added DDC write and read handlers
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/


#if defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0)
//#include <stdlib.h>
#include <linux/string.h>
#include "xhdcp1x_port.h"
#include "xhdcp1x_port_hdmi.h"
#include "xil_assert.h"
#include "xil_types.h"

/************************** Constant Definitions *****************************/

#define XHDCP1X_WRITE_CHUNK_SZ		(8)

/* Adaptor definition at the end of this file. */
const XHdcp1x_PortPhyIfAdaptor XHdcp1x_PortHdmiTxAdaptor;

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/*************************** Function Prototypes *****************************/

static int XHdcp1x_PortHdmiTxEnable(XHdcp1x *InstancePtr);
static int XHdcp1x_PortHdmiTxDisable(XHdcp1x *InstancePtr);
static int XHdcp1x_PortHdmiTxInit(XHdcp1x *InstancePtr);
static int XHdcp1x_PortHdmiTxIsCapable(const XHdcp1x *InstancePtr);
static int XHdcp1x_PortHdmiTxIsRepeater(const XHdcp1x *InstancePtr);
static int XHdcp1x_PortHdmiTxGetRepeaterInfo(const XHdcp1x *InstancePtr,
		u16 *Info);
static int XHdcp1x_PortHdmiTxRead(const XHdcp1x *InstancePtr, u8 Offset,
		void *Buf, u32 BufSize);
static int XHdcp1x_PortHdmiTxWrite(XHdcp1x *InstancePtr, u8 Offset,
		const void *Buf, u32 BufSize);

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
* This function enables a HDCP port device.
*
* @param	InstancePtr is the id of the device to enable.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_PortHdmiTxEnable(XHdcp1x *InstancePtr)
{
	u8 Value = 0;
	int Status = XST_NOT_ENABLED;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(InstancePtr->Port.PhyIfPtr != NULL);

	/* Read anything to ensure that the remote end is present */
	if ((XHdcp1x_PortHdmiTxRead(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
			&Value, 1)) > 0) {
		Status = XST_SUCCESS;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function disables a HDCP port device.
*
* @param	InstancePtr is the id of the device to disable.
*
* @return
*		- XST_SUCCESS if successful.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_PortHdmiTxDisable(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Nothing to do at this time */

	return (Status);
}

/*****************************************************************************/
/**
* This function initializes an HDCP port device.
*
* @param	InstancePtr is the device to initialize.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_PortHdmiTxInit(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(InstancePtr->Port.PhyIfPtr != NULL);

	/* Disable it */
	if (XHdcp1x_PortHdmiTxDisable(InstancePtr) != XST_SUCCESS) {
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function confirms the presence/capability of the remote HDCP device.
*
* @param	InstancePtr is the device to query.
*
* @return	Truth value.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_PortHdmiTxIsCapable(const XHdcp1x *InstancePtr)
{
	u8 Value[2] = {0, 0};
	u16 Bstatus = 0;
	int IsCapable = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check if the transmitter device is HDMI or DVI. */
	if(InstancePtr->Tx.TxIsHdmi == TRUE) {

		/* If an HDCP 1.x register is successfully read, then the
	        downstream device is ready to authenticate. */
		if (XHdcp1x_PortHdmiTxRead(InstancePtr,
			XHDCP1X_PORT_OFFSET_BCAPS, Value, 1)) {

			/* Check if connected device is DVI or HDMI
			 * capable in Bcaps. */
			if (Value[0] & 0x80) {

				if (XHdcp1x_PortHdmiTxRead(InstancePtr,
				     XHDCP1X_PORT_OFFSET_BSTATUS, Value, 2)) {
					/* If it is HDMI capable, check if
					 * HDMI_MODE in BStatus is true. */
					Bstatus = Value[0];
					Bstatus |= Value[1] << 8;

					/* Downstream receiver has
					 * transitioned to HDMI mode and is
					 * ready to authenticate. */
					if (Bstatus & 0x1000) {
						IsCapable = TRUE;
					}
					/* If the downstream receiver has
					 * not yet set the HDMI_MODE in
					 * BStatus it isn't ready yet. */
				}
			}
			/* The downstream device is DVI, but the transmitter is
			 * configured in HDMI mode. This is an error. In this
			 * case we keep the IsCapable value set to FALSE. */
			else {
				IsCapable = FALSE;
			}
		}
	}
	/* DVI */
	else if (InstancePtr->Tx.TxIsHdmi == FALSE) {
		/* If an HDCP 1.x register is successfully read, then the
	        downstream device is ready to authenticate. */
		if (XHdcp1x_PortHdmiTxRead(InstancePtr,
			XHDCP1X_PORT_OFFSET_BCAPS, Value, 1)) {
			IsCapable = TRUE;
		}
	}


	return (IsCapable);
}

/*****************************************************************************/
/**
* This function confirms if the remote HDCP device is a repeater.
*
* @param	InstancePtr is the device to query.
*
* @return	Truth value.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_PortHdmiTxIsRepeater(const XHdcp1x *InstancePtr)
{
	u8 Value = 0;
	int IsRepeater = FALSE;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check for repeater */
	if (XHdcp1x_PortHdmiTxRead(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
			&Value, 1) > 0) {
		if ((Value & XHDCP1X_PORT_BIT_BCAPS_REPEATER) != 0) {
			IsRepeater = TRUE;
		}
	}

	return (IsRepeater);
}

/*****************************************************************************/
/**
* This function retrieves the repeater information.
*
* @param	InstancePtr is the device to query.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_DEVICE_BUSY if the device is busy.
*		- XST_RECV_ERROR if receiver read failed.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_PortHdmiTxGetRepeaterInfo(const XHdcp1x *InstancePtr,
		u16 *Info)
{
	u8 Value = 0;
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Info != NULL);

	/* Read the remote capabilities */
	if (XHdcp1x_PortHdmiTxRead(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
			&Value, 1) > 0) {
		u8 ReadyMask = 0;

		/* Determine ReadyMask */
		ReadyMask  = XHDCP1X_PORT_BIT_BCAPS_REPEATER;
		ReadyMask |= XHDCP1X_PORT_BIT_BCAPS_READY;

		/* Check for repeater and ksv fifo ready */
		if ((Value & ReadyMask) == ReadyMask) {
			u8 Buf[2];
			u16 U16Value = 0;

			/* Read the Bstatus */
			XHdcp1x_PortHdmiTxRead(InstancePtr,
				XHDCP1X_PORT_OFFSET_BSTATUS, Buf, 2);

			/* Determine Value */
			XHDCP1X_PORT_BUF_TO_UINT(U16Value, Buf, 16);

			/* Update Info */
			*Info = (U16Value & 0x1FFFu);

		}
		else {
			Status = XST_DEVICE_BUSY;
		}
	}
	else {
		Status = XST_RECV_ERROR;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function reads a register from a HDCP port device.
*
* @param	InstancePtr is the device to read from.
* @param	Offset is the offset to start reading from.
* @param	Buf is the buffer to copy the data read.
* @param	BufSize is the size of the buffer.
*
* @return	The number of bytes read.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_PortHdmiTxRead(const XHdcp1x *InstancePtr, u8 Offset,
		void *Buf, u32 BufSize)
{
	u8 Slave = 0x3Au;
	int NumRead = 0;
	u8 *ReadBuf = Buf;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Buf != NULL);
	Xil_AssertNonvoid(InstancePtr->Tx.DdcRead != NULL);
	Xil_AssertNonvoid(InstancePtr->Tx.DdcWrite != NULL);

	/* Truncate if necessary */
	if ((BufSize + Offset) > 0x100u) {
		BufSize = (0x100u - Offset);
	}

	/* Write the address and check for failure */
	if (InstancePtr->Tx.DdcWrite(Slave, 1, &Offset, FALSE,
		InstancePtr->Tx.DdcWriteRef) != XST_SUCCESS) {
		NumRead = -1;
	}
	/* Read the data back and check for failure */
	else if (InstancePtr->Tx.DdcRead(Slave, BufSize, ReadBuf, TRUE,
		InstancePtr->Tx.DdcReadRef) != XST_SUCCESS) {
		NumRead = -2;
	}
	/* Success - just update NumRead */
	else {
		NumRead = (int)BufSize;
	}

	return (NumRead);
}

/*****************************************************************************/
/**
* This function writes a register from a HDCP port device.
*
* @param	InstancePtr is the device to write to.
* @param	Offset is the offset to start writing to.
* @param	Buf is the buffer containing the data to write.
* @param	BufSize is the size of the buffer.
*
* @return	The number of bytes written.
*
* @note		None.
*
******************************************************************************/
static int XHdcp1x_PortHdmiTxWrite(XHdcp1x *InstancePtr, u8 Offset,
		const void *Buf, u32 BufSize)
{
	u8 Slave = 0x3Au;
	u8 TxBuf[XHDCP1X_WRITE_CHUNK_SZ + 1];
	int NumWritten = 0;
	u32 ThisTime = 0;
	const u8 *WriteBuf = Buf;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Buf != NULL);
	Xil_AssertNonvoid(InstancePtr->Tx.DdcWrite != NULL);

	/* Truncate if necessary */
	if ((BufSize + Offset) > 0x100u) {
		BufSize = (0x100u - Offset);
	}

	/* Iterate through the buffer */
	do {
		/* Determine ThisTime */
		ThisTime = XHDCP1X_WRITE_CHUNK_SZ;
		if (ThisTime > BufSize) {
			ThisTime = BufSize;
		}

		/* Format TxBuf */
		TxBuf[0] = Offset;
		memcpy(&(TxBuf[1]), WriteBuf, ThisTime);

		/* Write the TxBuf */
		if (InstancePtr->Tx.DdcWrite(Slave, (ThisTime + 1), TxBuf,
			TRUE, InstancePtr->Tx.DdcWriteRef)
				!= XST_SUCCESS) {
			/* Update NumWritten and break */
			NumWritten = -1;
			break;
		}

		/* Update for loop */
		NumWritten += ThisTime;
		WriteBuf += ThisTime;
		BufSize -= ThisTime;
	} while ((BufSize != 0) && (NumWritten > 0));

	/* Return */
	return (NumWritten);
}

/*****************************************************************************/
/**
* This tables defines the adaptor for the HDMI TX HDCP port driver
*
******************************************************************************/
const XHdcp1x_PortPhyIfAdaptor XHdcp1x_PortHdmiTxAdaptor =
{
	&XHdcp1x_PortHdmiTxInit,
	&XHdcp1x_PortHdmiTxEnable,
	&XHdcp1x_PortHdmiTxDisable,
	&XHdcp1x_PortHdmiTxRead,
	&XHdcp1x_PortHdmiTxWrite,
	&XHdcp1x_PortHdmiTxIsCapable,
	&XHdcp1x_PortHdmiTxIsRepeater,
	NULL,
	&XHdcp1x_PortHdmiTxGetRepeaterInfo,
	NULL,
	NULL
};

#endif
/* defined(XPAR_XV_HDMITX_NUM_INSTANCES) && (XPAR_XV_HDMITX_NUM_INSTANCES > 0) */
/** @} */

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
* @file xhdcp1x_port_hdmi_rx.c
* @addtogroup hdcp1x_v4_0
* @{
*
* This contains the implementation of the HDCP port driver for HDMI RX
* interfaces
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 2.00  MG     01/20/16 Disabled hdcp call back in function
*                       XHdcp1x_PortHdmiRxEnable.
* 2.10  MG     02/29/16 Added DDC write and read handlers
* 3.0   yas    02/13/16 Upgraded function XHdcp1x_PortHdmiRxEnable to
*                       support HDCP Repeater functionality.
* 3.1   yas    07/28/16 Added function XHdcp1x_PortHdmiRxSetRepeater
* 3.2   yas    10/27/16 Updated the XHdcp1x_PortHdmiRxDisable function to not
*                       clear the AKSV, An and AInfo values in the DDC space.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/


#if defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0)
//#include <stdlib.h>
#include <linux/string.h>
#include "xhdcp1x_port.h"
#include "xhdcp1x_port_hdmi.h"
#include "xil_assert.h"
#include "xil_types.h"

/************************** Constant Definitions *****************************/

/* Adaptor definition at the end of this file. */
const XHdcp1x_PortPhyIfAdaptor XHdcp1x_PortHdmiRxAdaptor;

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/*************************** Function Prototypes *****************************/

static int XHdcp1x_PortHdmiRxEnable(XHdcp1x *InstancePtr);
static int XHdcp1x_PortHdmiRxDisable(XHdcp1x *InstancePtr);
static int XHdcp1x_PortHdmiRxInit(XHdcp1x *InstancePtr);
static int XHdcp1x_PortHdmiRxRead(const XHdcp1x *InstancePtr, u8 Offset,
		void *Buf, u32 BufSize);
static int XHdcp1x_PortHdmiRxWrite(XHdcp1x *InstancePtr, u8 Offset,
		const void *Buf, u32 BufSize);
static int XHdcp1x_PortHdmiRxSetRepeater(XHdcp1x *InstancePtr, u8 RptrConf);
static void XHdcp1x_ProcessAKsvWrite(void *CallbackRef);

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
static int XHdcp1x_PortHdmiRxEnable(XHdcp1x *InstancePtr)
{
	u8 Buf[4];
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(InstancePtr->Port.PhyIfPtr != NULL);

	/* Initialize the Bcaps register */
	memset(Buf, 0, 4);
	Buf[0] |= XHDCP1X_PORT_BIT_BCAPS_HDMI;
	Buf[0] |= XHDCP1X_PORT_BIT_BCAPS_FAST_REAUTH;
	if(InstancePtr->IsRepeater) {
		Buf[0] |= XHDCP1X_PORT_BIT_BCAPS_REPEATER;
	}
	XHdcp1x_PortHdmiRxWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS, Buf, 1);

	/* Initialize some debug registers */
	Buf[0] = 0xDE;
	Buf[1] = 0xAD;
	Buf[2] = 0xBE;
	Buf[3] = 0xEF;
	XHdcp1x_PortHdmiRxWrite(InstancePtr, XHDCP1X_PORT_OFFSET_DBG,
			Buf, 4);

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
static int XHdcp1x_PortHdmiRxDisable(XHdcp1x *InstancePtr)
{
	u8 Offset = 0;
	u8 Value = 0;
	int NumLeft = 0;
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(InstancePtr->Port.PhyIfPtr != NULL);

	/* Clear the hdcp registers */
	Value = 0;
	Offset = 0;
	/* Clear HDCP register space from BKSV (0x0) to AKSV (0x10) */
	NumLeft = 16;
	while (NumLeft-- > 0) {
		XHdcp1x_PortHdmiRxWrite(InstancePtr, Offset++, &Value, 1);
	}
	/* Clear the HDCP RSVD (0x16) register */
	Offset = 22;
	NumLeft = 2;
	while (NumLeft-- > 0) {
		XHdcp1x_PortHdmiRxWrite(InstancePtr, Offset++, &Value, 1);
	}
	/* Clear HDCP register space from VH0 (0x20) to RSVD (0x34) */
	Offset = 32;
	NumLeft = 20;
	while (NumLeft-- > 0) {
		XHdcp1x_PortHdmiRxWrite(InstancePtr, Offset++, &Value, 1);
	}
	/* Clear HDCP register space for Bcaps (0x40) */
	XHdcp1x_PortHdmiRxWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
					&Value, 1);

	/* Clear HDCP register space for Bstatus (0x41 and 0x42).
	 * Do not clear HDMI_MODE field. */
	XHdcp1x_PortHdmiRxWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BSTATUS,
					&Value, 1);
	XHdcp1x_PortHdmiRxRead(InstancePtr, (XHDCP1X_PORT_OFFSET_BSTATUS + 1),
					&Value, 1);
	Value &= (XHDCP1X_PORT_BIT_BSTATUS_HDMI_MODE >> 8);
	XHdcp1x_PortHdmiRxWrite(InstancePtr, (XHDCP1X_PORT_OFFSET_BSTATUS + 1),
					&Value, 1);

	/* Clear HDCP register space for KSV FIFO (0x43) */
	Value = 0;
	XHdcp1x_PortHdmiRxWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
					&Value, 1);

	return (Status);
}

/*****************************************************************************/
/**
* This function initializes a HDCP port device.
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
static int XHdcp1x_PortHdmiRxInit(XHdcp1x *InstancePtr)
{
	int Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(InstancePtr->Port.PhyIfPtr != NULL);

	/* Disable it */
	if (XHdcp1x_PortHdmiRxDisable(InstancePtr) != XST_SUCCESS) {
		Status = XST_FAILURE;
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
static int XHdcp1x_PortHdmiRxRead(const XHdcp1x *InstancePtr, u8 Offset,
		void *Buf, u32 BufSize)
{
	u32 NumLeft = BufSize;
	u8 *ReadBuf = Buf;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Buf != NULL);

	/* Truncate if necessary */
	if ((BufSize + Offset) > 0x100u) {
		BufSize = (0x100u - Offset);
	}

	/* Write the offset */
	if (InstancePtr->Rx.IsDdcSetAddressCallbackSet) {
		InstancePtr->Rx.DdcSetAddressCallback(
		    InstancePtr->Rx.DdcSetAddressCallbackRef, Offset);
	}

	/* Read the buffer */
	while (NumLeft-- > 0) {
		if (InstancePtr->Rx.IsDdcGetDataCallbackSet) {
			*ReadBuf++ = InstancePtr->Rx.DdcGetDataCallback(
			    InstancePtr->Rx.DdcGetDataCallbackRef);
		}
	}

	return ((int)BufSize);
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
static int XHdcp1x_PortHdmiRxWrite(XHdcp1x *InstancePtr, u8 Offset,
		const void *Buf, u32 BufSize)
{
	u32 NumLeft = BufSize;
	const u8 *WriteBuf = Buf;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Buf != NULL);

	/* Truncate if necessary */
	if ((BufSize + Offset) > 0x100u) {
		BufSize = (0x100u - Offset);
	}

	/* Write the offset */
	if (InstancePtr->Rx.IsDdcSetAddressCallbackSet) {
		InstancePtr->Rx.DdcSetAddressCallback(
		    InstancePtr->Rx.DdcSetAddressCallbackRef, Offset);
	}

	/* Write the buffer */
	while (NumLeft-- > 0) {
		if (InstancePtr->Rx.IsDdcSetDataCallbackSet) {
			InstancePtr->Rx.DdcSetDataCallback(
			    InstancePtr->Rx.DdcSetDataCallbackRef, *WriteBuf++);
		}
	}

	return ((int)BufSize);
}

/*****************************************************************************/
/**
* This function set the REPEATER bit in the BCaps of the device.
*
* @param	InstancePtr is the device to write to.
* @param	RptrConf is the repeater capability for the device.
*
* @return	XST_SUCCESS.
*
* @note		This function sets the REPEATER bit in the BCaps register for the
* 		upstream device to read. This can be used to update the device
* 		configuration if it changes in real time.
*
******************************************************************************/
static int XHdcp1x_PortHdmiRxSetRepeater(XHdcp1x *InstancePtr, u8 RptrConf)
{
	u8 Value = 0;

	/* Set the Ready bit in the BCaps Register */
	XHdcp1x_PortHdmiRxRead(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
			&Value, XHDCP1X_PORT_SIZE_BCAPS);
	if(RptrConf) {
		Value |= XHDCP1X_PORT_BIT_BCAPS_REPEATER;
	}
	else {
		Value &= ~XHDCP1X_PORT_BIT_BCAPS_REPEATER;
	}
	XHdcp1x_PortHdmiRxWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
			&Value, XHDCP1X_PORT_SIZE_BCAPS);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* This function process a write to the AKsv register from the tx device.
*
* @param	CallbackRef is the device to whose register was written.
*
* @return	None.
*
* @note		This function initiates the side effects of the tx device
*		writing the Aksv register. This is currently updates some status
*		bits as well as kick starts a re-authentication process.
*
******************************************************************************/
static void XHdcp1x_ProcessAKsvWrite(void *CallbackRef)
{
	XHdcp1x *InstancePtr = CallbackRef;
	u8 Value = 0;

	/* Update statistics */
	InstancePtr->Port.Stats.IntCount++;

	/* Clear bit 1 of the Ainfo register */
	XHdcp1x_PortHdmiRxRead(InstancePtr, XHDCP1X_PORT_OFFSET_AINFO,
			&Value, 1);
	Value &= 0xFDu;
	XHdcp1x_PortHdmiRxWrite(InstancePtr, XHDCP1X_PORT_OFFSET_AINFO,
			&Value, 1);

	/* Clear the Ready bit in the BCaps Register */
	Value = 0;
	XHdcp1x_PortRead(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
			&Value, XHDCP1X_PORT_SIZE_BCAPS);
	Value &= 0xDFu;
	XHdcp1x_PortWrite(InstancePtr, XHDCP1X_PORT_OFFSET_BCAPS,
			&Value, XHDCP1X_PORT_SIZE_BCAPS);

	/* Invoke authentication callback if set */
	if (InstancePtr->Port.IsAuthCallbackSet) {
		(*(InstancePtr->Port.AuthCallback))(InstancePtr->Port.AuthRef);
	}
}

/*****************************************************************************/
/**
* This tables defines the adaptor for the HDMI RX HDCP port driver
*
******************************************************************************/
const XHdcp1x_PortPhyIfAdaptor XHdcp1x_PortHdmiRxAdaptor =
{
	&XHdcp1x_PortHdmiRxInit,
	&XHdcp1x_PortHdmiRxEnable,
	&XHdcp1x_PortHdmiRxDisable,
	&XHdcp1x_PortHdmiRxRead,
	&XHdcp1x_PortHdmiRxWrite,
	NULL,
	NULL,
	&XHdcp1x_PortHdmiRxSetRepeater,
	NULL,
	NULL,
	&XHdcp1x_ProcessAKsvWrite
};

#endif
/* defined(XPAR_XV_HDMIRX_NUM_INSTANCES) && (XPAR_XV_HDMIRX_NUM_INSTANCES > 0) */
/** @} */

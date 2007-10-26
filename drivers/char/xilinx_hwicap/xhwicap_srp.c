/*****************************************************************************
 *
 *     Author: Xilinx, Inc.
 *
 *
 *     This program is free software; you can redistribute it and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *       XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
 *       AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
 *       SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
 *       OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
 *       APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
 *       THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
 *       AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
 *       FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
 *       WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
 *       IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
 *       REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
 *       INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *       FOR A PARTICULAR PURPOSE.
 *
 *       (c) Copyright 2003 Xilinx Inc.
 *       (c) Copyright 2007 Xilinx Inc.
 *       All rights reserved.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#include "xilinx_hwicap.h"

#define XHI_BUFFER_START 0

/****************************************************************************/
/**
 *
 * Stores data in the storage buffer at the specified address.
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked on.
 *
 * @param    Address - bram word address
 *
 * @param    Data - data to be stored at address
 *
 * @return   None.
 *
 * @note     None.
 *
*****************************************************************************/
void XHwIcap_StorageBufferWrite(struct xhwicap_drvdata *InstancePtr,
				u32 Address, u32 Data)
{
	/* Write data to storage buffer. */
	XHwIcap_mSetBram(InstancePtr->baseAddress, Address, Data);
}

/****************************************************************************/
/**
 *
 * Read data from the specified address in the storage buffer..
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked on.
 *
 * @param    Address - bram word address
 *
 * @return   Data.
 *
 * @note     None.
 *
*****************************************************************************/
u32 XHwIcap_StorageBufferRead(struct xhwicap_drvdata *InstancePtr, u32 Address)
{
	u32 Data;

	/* Read data from address. Multiply Address by 4 since 4 bytes per
	 * word.*/
	Data = XHwIcap_mGetBram(InstancePtr->baseAddress, Address);
	return Data;

}

/****************************************************************************/
/**
 *
 * Reads bytes from the device (ICAP) and puts it in the storage buffer.
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked on.
 *
 * @param    Offset - The storage buffer start address.
 *
 * @param    NumInts - The number of words (32 bit) to read from the
 *           device (ICAP).
 *
 *@return    int - 0 or -EBUSY or -EINVAL
 *
 * @note     None.
 *
*****************************************************************************/
int XHwIcap_DeviceRead(struct xhwicap_drvdata *InstancePtr, u32 Offset,
		       u32 NumInts)
{

	s32 Retries = 0;

	if (XHwIcap_mGetDoneReg(InstancePtr->baseAddress) == XHI_NOT_FINISHED) {
		return -EBUSY;
	}

	if ((Offset + NumInts) <= XHI_MAX_BUFFER_INTS) {
		/* setSize NumInts*4 to get bytes. */
		XHwIcap_mSetSizeReg((InstancePtr->baseAddress), (NumInts << 2));
		XHwIcap_mSetOffsetReg((InstancePtr->baseAddress), Offset);
		XHwIcap_mSetRncReg((InstancePtr->baseAddress), XHI_READBACK);

		while (XHwIcap_mGetDoneReg(InstancePtr->baseAddress) ==
		       XHI_NOT_FINISHED) {
			Retries++;
			if (Retries > XHI_MAX_RETRIES) {
				return -EBUSY;
			}
		}
	} else {
		return -EINVAL;
	}
	return 0;

};

/****************************************************************************/
/**
 *
 * Writes bytes from the storage buffer and puts it in the device (ICAP).
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked on.
 *
 * @param    Offset - The storage buffer start address.
 *
 * @param    NumInts - The number of words (32 bit) to read from the
 *           device (ICAP).
 *
 *@return    int - 0 or -EBUSY or -EINVAL
 *
 * @note     None.
 *
*****************************************************************************/
int XHwIcap_DeviceWrite(struct xhwicap_drvdata *InstancePtr, u32 Offset,
			u32 NumInts)
{

	s32 Retries = 0;

	if (XHwIcap_mGetDoneReg(InstancePtr->baseAddress) == XHI_NOT_FINISHED) {
		return -EBUSY;
	}

	if ((Offset + NumInts) <= XHI_MAX_BUFFER_INTS) {
		/* setSize NumInts*4 to get bytes.  */
		XHwIcap_mSetSizeReg((InstancePtr->baseAddress), NumInts << 2);
		XHwIcap_mSetOffsetReg((InstancePtr->baseAddress), Offset);
		XHwIcap_mSetRncReg((InstancePtr->baseAddress), XHI_CONFIGURE);

		while (XHwIcap_mGetDoneReg(InstancePtr->baseAddress) ==
		       XHI_NOT_FINISHED) {
			Retries++;
			if (Retries > XHI_MAX_RETRIES) {
				return -EBUSY;
			}
		}
	} else {
		return -EINVAL;
	}
	return 0;

};

/****************************************************************************/
/**
 *
 * Sends a DESYNC command to the ICAP port.
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked on.
 *
 *@return    int - 0 or -EBUSY or -EINVAL
 *
 * @note     None.
 *
*****************************************************************************/
int XHwIcap_CommandDesync(struct xhwicap_drvdata *InstancePtr)
{
	int status;

	XHwIcap_StorageBufferWrite(InstancePtr, 0,
				   (XHwIcap_Type1Write(XHI_CMD) | 1));
	XHwIcap_StorageBufferWrite(InstancePtr, 1, XHI_CMD_DESYNCH);
	XHwIcap_StorageBufferWrite(InstancePtr, 2, XHI_NOOP_PACKET);
	XHwIcap_StorageBufferWrite(InstancePtr, 3, XHI_NOOP_PACKET);

	status = XHwIcap_DeviceWrite(InstancePtr, 0, 4);	/* send four words */
	if (status) {
		return status;
	}

	return 0;
}

/****************************************************************************/
/**
 *
 * Sends a CAPTURE command to the ICAP port.  This command caputres all
 * of the flip flop states so they will be available during readback.
 * One can use this command instead of enabling the CAPTURE block in the
 * design.
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked on.
 *
 * @return    int - 0 or -EBUSY or -EINVAL
 *
 * @note     None.
 *
*****************************************************************************/
int XHwIcap_CommandCapture(struct xhwicap_drvdata *InstancePtr)
{
	int status;

	/* DUMMY and SYNC */
	XHwIcap_StorageBufferWrite(InstancePtr, 0, XHI_DUMMY_PACKET);
	XHwIcap_StorageBufferWrite(InstancePtr, 1, XHI_SYNC_PACKET);
	XHwIcap_StorageBufferWrite(InstancePtr, 2,
				   (XHwIcap_Type1Write(XHI_CMD) | 1));
	XHwIcap_StorageBufferWrite(InstancePtr, 3, XHI_CMD_GCAPTURE);
	XHwIcap_StorageBufferWrite(InstancePtr, 4, XHI_DUMMY_PACKET);
	XHwIcap_StorageBufferWrite(InstancePtr, 5, XHI_DUMMY_PACKET);

	status = XHwIcap_DeviceWrite(InstancePtr, 0, 6);	/* send six words */
	if (status) {		/* send six words */
		return status;
	}

	return 0;
}

/****************************************************************************/
/**
 * 
 * This function returns the value of the specified configuration
 * register.
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked
 * on.
 *
 * @param    ConfigReg  - A constant which represents the configuration
 * register value to be returned. Constants specified in xhwicap_i.h.  Examples:
 * XHI_IDCODE, XHI_FLR.
 *
 * @return   The value of the specified configuration register.
 *
 *
*****************************************************************************/

u32 XHwIcap_GetConfigReg(struct xhwicap_drvdata * InstancePtr, u32 ConfigReg)
{
	u32 Packet;
	int status;

	/* Write bitstream to bram */
	Packet = XHwIcap_Type1Read(ConfigReg) | 1;
	XHwIcap_StorageBufferWrite(InstancePtr, 0, XHI_DUMMY_PACKET);
	XHwIcap_StorageBufferWrite(InstancePtr, 1, XHI_SYNC_PACKET);
	XHwIcap_StorageBufferWrite(InstancePtr, 2, Packet);
	XHwIcap_StorageBufferWrite(InstancePtr, 3, XHI_NOOP_PACKET);
	XHwIcap_StorageBufferWrite(InstancePtr, 4, XHI_NOOP_PACKET);

	/* Transfer Bitstream from Bram to ICAP */
	if ((status = XHwIcap_DeviceWrite(InstancePtr, 0, 5))) {
		return status;
	}

	/* Now readback one word into bram position
	 * XHI_EX_BITSTREAM_LENGTH*/
	if ((status = XHwIcap_DeviceRead(InstancePtr, 5, 1))) {
		return status;
	}

	/* Return the Register value */
	return XHwIcap_StorageBufferRead(InstancePtr, 5);
}

/****************************************************************************
 *
 * Loads a partial bitstream from system memory.
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked on.
 *
 * @param    Data - Address of the data representing the partial bitstream
 *
 * @param    Size - the size of the partial bitstream in 32 bit words.
 *
 * @return   0, -EFBIG or -EINVAL.
 *
 * @note     None.
 *
*****************************************************************************/
int XHwIcap_SetConfiguration(struct xhwicap_drvdata *InstancePtr, u32 * Data,
			     u32 Size)
{
	int status;
	s32 BufferCount = 0;
	s32 NumWrites = 0;
	bool Dirty = 0;
	u32 I;

	/* Loop through all the data */
	for (I = 0, BufferCount = 0; I < Size; I++) {

		/* Copy data to bram */
		XHwIcap_StorageBufferWrite(InstancePtr, BufferCount, Data[I]);
		Dirty = 1;

		if (BufferCount == XHI_MAX_BUFFER_INTS - 1) {
			/* Write data to ICAP */
			status =
			    XHwIcap_DeviceWrite(InstancePtr, XHI_BUFFER_START,
						XHI_MAX_BUFFER_INTS);
			if (status != 0) {
				XHwIcap_mReset(InstancePtr->baseAddress);	// abort.
				return status;
			}

			BufferCount = 0;
			NumWrites++;
			Dirty = 0;
		} else {
			BufferCount++;
		}
	}

	/* Write unwritten data to ICAP */
	if (Dirty) {
		/* Write data to ICAP */
		status = XHwIcap_DeviceWrite(InstancePtr, XHI_BUFFER_START,
					     BufferCount);
		if (status != 0) {
			XHwIcap_mReset(InstancePtr->baseAddress);	// abort.
		}
		return status;
	}

	return 0;
};

/****************************************************************************
 *
 * Reads Configuration Data from the device.
 *
 * @param    InstancePtr - a pointer to the XHwIcap instance to be worked on.
 *
 * @param    Data - Address of the data representing the partial bitstream
 *
 * @param    Size - the size of the partial bitstream in 32 bit words.
 *
 * @return   0, -EFBIG or -EINVAL.
 *
 * @note     None.
 *
*****************************************************************************/
int XHwIcap_GetConfiguration(struct xhwicap_drvdata *InstancePtr, u32 * Data,
			     u32 Size)
{
	int status;
	s32 BufferCount = 0;
	s32 NumReads = 0;
	u32 I;

	/* Loop through all the data */
	for (I = 0, BufferCount = XHI_MAX_BUFFER_INTS; I < Size; I++) {
		if (BufferCount == XHI_MAX_BUFFER_INTS) {
			u32 intsRemaining = Size - I;
			u32 intsToRead =
			    intsRemaining <
			    XHI_MAX_BUFFER_INTS ? intsRemaining :
			    XHI_MAX_BUFFER_INTS;

			/* Read data from ICAP */

			status =
			    XHwIcap_DeviceRead(InstancePtr, XHI_BUFFER_START,
					       intsToRead);
			if (status != 0) {
				XHwIcap_mReset(InstancePtr->baseAddress);	// abort.
				return status;
			}

			BufferCount = 0;
			NumReads++;
		}

		/* Copy data from bram */
		Data[I] = XHwIcap_StorageBufferRead(InstancePtr, BufferCount);
		BufferCount++;
	}

	return 0;
};

 /*****************************************************************************
 *
 *     Author: Xilinx, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
 *     AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
 *     SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
 *     OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
 *     APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
 *     THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
 *     AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
 *     FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
 *     WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
 *     IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
 *     REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
 *     INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE.
 *
 *     Xilinx products are not intended for use in life support appliances,
 *     devices, or systems. Use in such applications is expressly prohibited.
 *
 *     (c) Copyright 2003-2007 Xilinx Inc.
 *     All rights reserved.
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
 * @parameter    drvdata - a pointer to the drvdata.
 *
 * @parameter    address - bram word address
 *
 * @parameter    Data - data to be stored at address
 *
 * @return   None.
 *
 * @note     None.
 *
*****************************************************************************/
void XHwIcap_StorageBufferWrite(struct hwicap_drvdata *drvdata,
				u32 address, u32 Data)
{
	/* Write data to storage buffer. */
	XHwIcap_mSetBram(drvdata->baseAddress, address, Data);
}

/****************************************************************************/
/**
 *
 * Read data from the specified address in the storage buffer..
 *
 * @parameter    drvdata - a pointer to the drvdata.
 *
 * @parameter    address - bram word address
 *
 * @return   Data.
 *
 * @note     None.
 *
*****************************************************************************/
u32 XHwIcap_StorageBufferRead(struct hwicap_drvdata *drvdata, u32 address)
{
	u32 Data;

	/* Read data from address. Multiply Address by 4 since 4 bytes per
	 * word.*/
	Data = XHwIcap_mGetBram(drvdata->baseAddress, address);
	return Data;

}

/**
 * XHwIcap_DeviceRead: Transfer bytes from ICAP to the storage buffer.
 * @parameter drvdata: a pointer to the drvdata.
 * @parameter offset: The storage buffer start address.
 * @parameter count: The number of words (32 bit) to read from the
 *           device (ICAP).
 **/
int XHwIcap_DeviceRead(struct hwicap_drvdata *drvdata, u32 offset, u32 count)
{

	s32 Retries = 0;
        void __iomem *base_address = drvdata->baseAddress;

	if (hwicap_busy(base_address)) {
		return -EBUSY;
	}

	if ((offset + count) <= XHI_MAX_BUFFER_INTS) {
		/* setSize count*4 to get bytes. */
		XHwIcap_mSetSizeReg(base_address, (count << 2));
		XHwIcap_mSetOffsetReg(base_address, offset);
		XHwIcap_mSetRncReg(base_address, XHI_READBACK);

		while (hwicap_busy(base_address)) {
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

/**
 * XHwIcap_DeviceWrite: Transfer bytes from ICAP to the storage buffer.
 * @parameter drvdata: a pointer to the drvdata.
 * @parameter offset: The storage buffer start address.
 * @parameter count: The number of words (32 bit) to read from the
 *           device (ICAP).
 **/
int XHwIcap_DeviceWrite(struct hwicap_drvdata *drvdata, u32 offset,
			u32 count)
{

	s32 Retries = 0;
        void __iomem *base_address = drvdata->baseAddress;

	if (hwicap_busy(base_address)) {
		return -EBUSY;
	}

	if ((offset + count) <= XHI_MAX_BUFFER_INTS) {
		/* setSize count*4 to get bytes.  */
		XHwIcap_mSetSizeReg(base_address, count << 2);
		XHwIcap_mSetOffsetReg(base_address, offset);
		XHwIcap_mSetRncReg(base_address, XHI_CONFIGURE);

		while (hwicap_busy(base_address)) {
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

/**
 * XHwIcap_CommandDesync: Send a DESYNC command to the ICAP port.
 * @parameter    drvdata - a pointer to the drvdata.
 **/
int XHwIcap_CommandDesync(struct hwicap_drvdata *drvdata)
{
	int status;
        void __iomem *base_address = drvdata->baseAddress;

	XHwIcap_mSetBram(base_address, 0,
				   (XHwIcap_Type1Write(XHI_CMD) | 1));
	XHwIcap_mSetBram(base_address, 1, XHI_CMD_DESYNCH);
	XHwIcap_mSetBram(base_address, 2, XHI_NOOP_PACKET);
	XHwIcap_mSetBram(base_address, 3, XHI_NOOP_PACKET);

	/* send four words */
	status = XHwIcap_DeviceWrite(drvdata, 0, 4);
	if (status) {
		return status;
	}

	return 0;
}

/**
 * XHwIcap_CommandCapture: Send a CAPTURE command to the ICAP port.
 * @parameter    drvdata - a pointer to the drvdata.
 *
 * This command caputres all of the flip flop states so they will be
 * available during readback.  One can use this command instead of
 * enabling the CAPTURE block in the design.
 **/
int XHwIcap_CommandCapture(struct hwicap_drvdata *drvdata)
{
	int status;
        void __iomem *base_address = drvdata->baseAddress;

	/* DUMMY and SYNC */
	XHwIcap_mSetBram(base_address, 0, XHI_DUMMY_PACKET);
	XHwIcap_mSetBram(base_address, 1, XHI_SYNC_PACKET);
	XHwIcap_mSetBram(base_address, 2,
				   (XHwIcap_Type1Write(XHI_CMD) | 1));
	XHwIcap_mSetBram(base_address, 3, XHI_CMD_GCAPTURE);
	XHwIcap_mSetBram(base_address, 4, XHI_DUMMY_PACKET);
	XHwIcap_mSetBram(base_address, 5, XHI_DUMMY_PACKET);

	/* send six words */
	status = XHwIcap_DeviceWrite(drvdata, 0, 6);
	if (status) {		/* send six words */
		return status;
	}

	return 0;
}

/**
 * XHwIcap_GetConfigReg: Return the value of a configuration register.
 *
 * @parameter drvdata: a pointer to the drvdata.
 * @parameter ConfigReg: A constant which represents the configuration
 * register value to be returned. Examples: XHI_IDCODE, XHI_FLR.
 **/
u32 XHwIcap_GetConfigReg(struct hwicap_drvdata *drvdata, u32 ConfigReg)
{
	u32 Packet;
	int status;
        void __iomem *base_address = drvdata->baseAddress;

	/* Write bitstream to bram */
	Packet = XHwIcap_Type1Read(ConfigReg) | 1;
	XHwIcap_mSetBram(base_address, 0, XHI_DUMMY_PACKET);
	XHwIcap_mSetBram(base_address, 1, XHI_SYNC_PACKET);
	XHwIcap_mSetBram(base_address, 2, Packet);
	XHwIcap_mSetBram(base_address, 3, XHI_NOOP_PACKET);
	XHwIcap_mSetBram(base_address, 4, XHI_NOOP_PACKET);

	/* Transfer Bitstream from Bram to ICAP */
	status = XHwIcap_DeviceWrite(drvdata, 0, 5);
	if (status) {
		return status;
	}

	/* Now readback one word into bram position
	 * XHI_EX_BITSTREAM_LENGTH*/
	status = XHwIcap_DeviceRead(drvdata, 5, 1);
	if (status) {
		return status;
	}

	/* Return the Register value */
	return XHwIcap_mGetBram(base_address, 5);
}

/**
 * XHwIcap_SetConfiguration: Load a partial bitstream from system memory.
 * @parameter drvdata: a pointer to the drvdata.
 * @parameter data: Kernel address of the partial bitstream.
 * @parameter size: the size of the partial bitstream in 32 bit words.
 **/
int XHwIcap_SetConfiguration(struct hwicap_drvdata *drvdata, u32 *data,
			     u32 size)
{
	int status;
	s32 buffer_count = 0;
	s32 NumWrites = 0;
	bool Dirty = 0;
	u32 i;
        void __iomem *base_address = drvdata->baseAddress;

	/* Loop through all the data */
	for (i = 0, buffer_count = 0; i < size; i++) {

		/* Copy data to bram */
		XHwIcap_mSetBram(base_address, buffer_count, data[i]);
		Dirty = 1;

		if (buffer_count == XHI_MAX_BUFFER_INTS - 1) {
			/* Write data to ICAP */
			status = XHwIcap_DeviceWrite(drvdata, XHI_BUFFER_START,
						XHI_MAX_BUFFER_INTS);
			if (status != 0) {
				/* abort. */
				XHwIcap_mReset(base_address);					return status;
			}

			buffer_count = 0;
			NumWrites++;
			Dirty = 0;
		} else {
			buffer_count++;
		}
	}

	/* Write unwritten data to ICAP */
	if (Dirty) {
		/* Write data to ICAP */
		status = XHwIcap_DeviceWrite(drvdata, XHI_BUFFER_START,
					     buffer_count);
		if (status != 0) {
			/* abort. */
			XHwIcap_mReset(base_address);
		}
		return status;
	}

	return 0;
};

/**
 * XHwIcap_GetConfiguration: Reads Configuration data from the device.
 * @parameter drvdata: a pointer to the drvdata.
 * @parameter data: Address of the data representing the partial bitstream
 * @parameter size: the size of the partial bitstream in 32 bit words.
 **/
int XHwIcap_GetConfiguration(struct hwicap_drvdata *drvdata, u32 *data,
			     u32 size)
{
	int status;
	s32 buffer_count = 0;
	s32 read_count = 0;
	u32 i;
        void __iomem *base_address = drvdata->baseAddress;

	/* Loop through all the data */
	for (i = 0, buffer_count = XHI_MAX_BUFFER_INTS; i < size; i++) {
		if (buffer_count == XHI_MAX_BUFFER_INTS) {
			u32 intsRemaining = size - i;
			u32 intsToRead =
			    intsRemaining <
			    XHI_MAX_BUFFER_INTS ? intsRemaining :
			    XHI_MAX_BUFFER_INTS;

			/* Read data from ICAP */
			status =
			    XHwIcap_DeviceRead(drvdata, XHI_BUFFER_START,
					       intsToRead);
			if (status != 0) {
				/* abort. */
				XHwIcap_mReset(base_address);
				return status;
			}

			buffer_count = 0;
			read_count++;
		}

		/* Copy data from bram */
		data[i] = XHwIcap_mGetBram(base_address, buffer_count);
		buffer_count++;
	}

	return 0;
};

/* $Id: xsysace_l.c,v 1.1 2006/02/17 21:52:36 moleres Exp $ */
/******************************************************************************
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
*       (c) Copyright 2002 Xilinx Inc.
*       All rights reserved.
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xsysace_l.c
*
* This file contains low-level functions to read and write CompactFlash
* sectors and ACE controller registers. These functions can be used when only
* the low-level functionality of the driver is desired. The user would
* typically use the high-level driver functions defined in xsysace.h.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  06/14/02 work in progress
* 1.00a rpm  09/16/03 Added include of xparameters.h in order to get
*                     the XPAR_XSYSACE_MEM_WIDTH definition.
* 1.00a rpm  02/17/04 Fixed WriteSector function command
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xsysace_l.h"

/************************** Constant Definitions *****************************/

/*
 * Set up the access width of the MPU registers based on compile-time constants.
 * If hardware requires 32-bit aligned addresses (XSA_ADDR_ALIGN=4) to access
 * the MPU registers, then access all of them as 32 bits. If hardware allows
 * 8-bit aligned addresses (XSA_ADDR_ALIGN=1, or not 4) to access the MPU
 * registers, access them as 8 or 16 bits depending on the bus mode of the ACE
 * controller.
 */
#if (XSA_ADDR_ALIGN == 4)

#define XIo_In   XIo_In32
#define XIo_Out  XIo_Out32

#else

#if (XPAR_XSYSACE_MEM_WIDTH == 16)
#define XIo_In   XIo_In16
#define XIo_Out  XIo_Out16
#else /* XPAR_XSYSACE_MEM_WIDTH */
#define XIo_In   XIo_In8
#define XIo_Out  XIo_Out8
#endif /* XPAR_XSYSACE_MEM_WIDTH */

#endif /* (XSA_ADDR_ALIGN == 4) */

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

/*****************************************************************************/
/**
*
* Read a 32-bit value from the given address. Based on a compile-time
* constant, do the read in two 16-bit reads or four 8-bit reads.
*
* @param    Address is the address to read from.
*
* @return   The 32-bit value of the address.
*
* @note     No need for endian conversion in 8-bit mode since this function
*           gets the bytes into their proper lanes in the 32-bit word.
*
******************************************************************************/
#if (XPAR_XSYSACE_MEM_WIDTH == 16)
u32 XSysAce_RegRead32(u32 Address)
{
	u16 Data;
	u16 ConvertedData;
	u32 Value = 0;

	/*
	 * Need to endian convert each 32-bit value. The ACE registers are little-
	 * endian, so we read the two LSBs first, endian convert, then put them
	 * in the LSB lanes of the 32-bit word. etc...
	 */
	Data = (u16) XIo_In(Address);
	XIo_FromLittleEndian16(Data, &ConvertedData);
	Value = (u32) ConvertedData;

	Data = (u16) XIo_In(Address + (2 * XSA_ADDR_ALIGN));
	XIo_FromLittleEndian16(Data, &ConvertedData);
	Value |= ((u32) ConvertedData << 16);

	return Value;
}
#else
u32 XSysAce_RegRead32(u32 Address)
{
	u32 Value = 0;

	/*
	 * The ACE registers are little-endian always. This code reads each 8-bit
	 * register value, in order from LSB to MSB, and shifts it to the correct
	 * byte lane of the 32-bit word. This code should work on both
	 * little-endian and big-endian processors.
	 */
	Value = (u32) XIo_In(Address);
	Value |= ((u32) XIo_In(Address + (1 * XSA_ADDR_ALIGN)) << 8);
	Value |= ((u32) XIo_In(Address + (2 * XSA_ADDR_ALIGN)) << 16);
	Value |= ((u32) XIo_In(Address + (3 * XSA_ADDR_ALIGN)) << 24);

	return Value;
}
#endif


/*****************************************************************************/
/**
*
* Read a 16-bit value from the given address. Based on a compile-time
* constant, do the read in one 16-bit read or two 8-bit reads.
*
* @param    Address is the address to read from.
*
* @return   The 16-bit value of the address.
*
* @note     No need for endian conversion in 8-bit mode since this function
*           gets the bytes into their proper lanes in the 16-bit word.
*
******************************************************************************/
#if (XPAR_XSYSACE_MEM_WIDTH == 16)
u16 XSysAce_RegRead16(u32 Address)
{
	u16 Data;
	u16 ConvertedData;

	/*
	 * Need to endian convert the 16-bit value. The ACE registers are little-
	 * endian.
	 */
	Data = (u16) XIo_In(Address);
	XIo_FromLittleEndian16(Data, &ConvertedData);
	return ConvertedData;
}
#else
u16 XSysAce_RegRead16(u32 Address)
{
	u16 Value = 0;

	/*
	 * The ACE registers are little-endian always. This code reads each 8-bit
	 * register value, in order from LSB to MSB, and shifts it to the correct
	 * byte lane of the 32-bit word. This code should work on both
	 * little-endian and big-endian processors.
	 */
	Value = (u16) XIo_In(Address);
	Value |= ((u16) XIo_In(Address + (1 * XSA_ADDR_ALIGN)) << 8);

	return Value;
}
#endif


/*****************************************************************************/
/**
*
* Write a 32-bit value to the given address. Based on a compile-time
* constant, do the write in two 16-bit writes or four 8-bit writes.
*
* @param    Address is the address to write to.
* @param    Data is the value to write
*
* @return   None.
*
* @note     No need for endian conversion in 8-bit mode since this function
*           writes the bytes into their proper lanes based on address.
*
******************************************************************************/
#if (XPAR_XSYSACE_MEM_WIDTH == 16)
void XSysAce_RegWrite32(u32 Address, u32 Data)
{
	u16 Hword;
	u16 ConvertedData;

	/*
	 * The ACE registers are little-endian always. This code takes each 16-bit
	 * value of the incoming 32-bit word and endian converts it, then writes it
	 * to the ACE register.
	 */
	Hword = (u16) Data;
	XIo_ToLittleEndian16(Hword, &ConvertedData);
	XIo_Out(Address, ConvertedData);

	Hword = (u16) (Data >> 16);
	XIo_ToLittleEndian16(Hword, &ConvertedData);
	XIo_Out(Address + (2 * XSA_ADDR_ALIGN), ConvertedData);
}
#else
void XSysAce_RegWrite32(u32 Address, u32 Data)
{
	/*
	 * The ACE registers are little-endian always. This code reads each 8-bit
	 * register value, in order from LSB to MSB, and shifts it to the correct
	 * byte lane of the 32-bit word. This code should work on both
	 * little-endian and big-endian processors.
	 */
	XIo_Out(Address, (u8) Data);
	XIo_Out(Address + (1 * XSA_ADDR_ALIGN), (u8) (Data >> 8));
	XIo_Out(Address + (2 * XSA_ADDR_ALIGN), (u8) (Data >> 16));
	XIo_Out(Address + (3 * XSA_ADDR_ALIGN), (u8) (Data >> 24));
}
#endif


/*****************************************************************************/
/**
*
* Write a 16-bit value to the given address. Based on a compile-time
* constant, do the write in one 16-bit write or two 8-bit writes.
*
* @param    Address is the address to write to.
* @param    Data is the value to write
*
* @return   None.
*
* @note     No need for endian conversion in 8-bit mode since this function
*           writes the bytes into their proper lanes based on address.
*
******************************************************************************/
#if (XPAR_XSYSACE_MEM_WIDTH == 16)
void XSysAce_RegWrite16(u32 Address, u16 Data)
{
	u16 ConvertedData;

	/*
	 * The ACE registers are little-endian always. This code takes the incoming
	 * 16-bit and endian converts it, then writes it to the ACE register.
	 */
	XIo_ToLittleEndian16(Data, &ConvertedData);
	XIo_Out(Address, ConvertedData);
}
#else
void XSysAce_RegWrite16(u32 Address, u16 Data)
{
	/*
	 * The ACE registers are little-endian always. This code reads each 8-bit
	 * register value, in order from LSB to MSB, and shifts it to the correct
	 * byte lane of the 32-bit word. This code should work on both
	 * little-endian and big-endian processors.
	 */
	XIo_Out(Address, (u8) Data);
	XIo_Out(Address + (1 * XSA_ADDR_ALIGN), (u8) (Data >> 8));
}
#endif


/*****************************************************************************/
/**
*
* Read a CompactFlash sector. This is a blocking, low-level function which
* does not return until the specified sector is read.
*
* @param BaseAddress is the base address of the device
* @param SectorId is the id of the sector to read
* @param BufferPtr is a pointer to a buffer where the data will be stored.
*
* @return
*
* The number of bytes read. If this number is not equal to the sector size,
* 512 bytes, then an error occurred.
*
* @note
*
* None.
*
******************************************************************************/
int XSysAce_ReadSector(u32 BaseAddress, u32 SectorId, u8 *BufferPtr)
{
	int NumRead;

	/* Request and wait for the lock */
	XSysAce_mWaitForLock(BaseAddress);

	/* See if the CF is ready for a command */
	if (!XSysAce_mIsReadyForCmd(BaseAddress)) {
		return 0;
	}

	/* Write the sector ID (LBA) */
	XSysAce_RegWrite32(BaseAddress + XSA_MLR_OFFSET, SectorId);

	/* Send a read command of one sector to the controller */
	XSysAce_RegWrite16(BaseAddress + XSA_SCCR_OFFSET,
			   XSA_SCCR_READDATA_MASK | 1);

	/* Reset configuration controller (be sure to keep the lock) */
	XSysAce_mOrControlReg(BaseAddress, XSA_CR_CFGRESET_MASK);

	/* Read a sector of data from the data buffer */
	NumRead = XSysAce_ReadDataBuffer(BaseAddress, BufferPtr,
					 XSA_CF_SECTOR_SIZE);

	/* Clear reset of configuration controller and locks */
	XSysAce_mAndControlReg(BaseAddress, ~(XSA_CR_CFGRESET_MASK |
					      XSA_CR_LOCKREQ_MASK));

	return NumRead;
}

/*****************************************************************************/
/**
*
* Write a CompactFlash sector. This is a blocking, low-level function which
* does not return until the specified sector is written in its entirety.
*
* @param BaseAddress is the base address of the device
* @param SectorId is the id of the sector to write
* @param BufferPtr is a pointer to a buffer used to write the sector.
*
* @return
*
* The number of bytes written. If this number is not equal to the sector size,
* 512 bytes, then an error occurred.
*
* @note
*
* None.
*
******************************************************************************/
int XSysAce_WriteSector(u32 BaseAddress, u32 SectorId, u8 *BufferPtr)
{
	int NumSent;

	/* Get the lock */
	XSysAce_mWaitForLock(BaseAddress);

	/* See if the CF is ready for a command */
	if (!XSysAce_mIsReadyForCmd(BaseAddress)) {
		return 0;
	}

	/* Write the sector ID (LBA) */
	XSysAce_RegWrite32(BaseAddress + XSA_MLR_OFFSET, SectorId);

	/* Send a write command of one sector to the controller */
	XSysAce_RegWrite16(BaseAddress + XSA_SCCR_OFFSET,
			   XSA_SCCR_WRITEDATA_MASK | 1);

	/* Reset configuration controller (be sure to keep the lock) */
	XSysAce_mOrControlReg(BaseAddress, XSA_CR_CFGRESET_MASK);

	/* Write a sector of data to the data buffer */
	NumSent = XSysAce_WriteDataBuffer(BaseAddress, BufferPtr,
					  XSA_CF_SECTOR_SIZE);

	/* Clear reset of configuration controller and locks */
	XSysAce_mAndControlReg(BaseAddress, ~(XSA_CR_CFGRESET_MASK |
					      XSA_CR_LOCKREQ_MASK));

	return NumSent;
}


/*****************************************************************************/
/**
*
* Read the specified number of bytes from the data buffer of the ACE
* controller. The data buffer, which is 32 bytes, can only be read two bytes
* at a time.  Once the data buffer is read, we wait for it to be filled again
* before reading the next buffer's worth of data.
*
* @param BaseAddress is the base address of the device
* @param BufferPtr is a pointer to a buffer in which to store data.
* @param Size is the number of bytes to read
*
* @return
*
* The total number of bytes read, or 0 if an error occurred.
*
* @note
*
* If Size is not aligned with the size of the data buffer (32 bytes), this
* function will read the entire data buffer, dropping the extra bytes on the
* floor since the user did not request them. This is necessary to get the
* data buffer to be ready again.
*
******************************************************************************/
int XSysAce_ReadDataBuffer(u32 BaseAddress, u8 *BufferPtr, int Size)
{
	int DataBytes;		/* number of data bytes written */
	int BufferBytes;
	u16 Data;

	/*
	 * Read data two bytes at a time. We need to wait for the data
	 * buffer to be ready before reading the buffer.
	 */
	BufferBytes = 0;
	for (DataBytes = 0; DataBytes < Size;) {
		/*
		 * If at any point during this read an error occurs, exit early
		 */
		if (XSysAce_mGetErrorReg(BaseAddress) != 0) {
			return 0;
		}

		if (BufferBytes == 0) {
			/*
			 * Wait for CF data buffer to ready, then reset buffer byte count
			 */
			while ((XSysAce_mGetStatusReg(BaseAddress)
				& XSA_SR_DATABUFRDY_MASK) == 0);

			BufferBytes = XSA_DATA_BUFFER_SIZE;
		}

		/*
		 * Need to read two bytes. Put the first one in the output buffer
		 * because if we're here we know one more is needed. Put the second one
		 * in the output buffer if there is still room, or just drop it on the
		 * floor if the requested number of bytes have already been read.
		 */
		Data = XSysAce_RegRead16(BaseAddress + XSA_DBR_OFFSET);
		*BufferPtr++ = (u8) Data;
		DataBytes++;

		if (DataBytes < Size) {
			/* Still more room in the output buffer */
			*BufferPtr++ = (u8) (Data >> 8);
			DataBytes++;
		}

		BufferBytes -= 2;
	}

	/*
	 * If a complete data buffer was not read, read and ignore the remaining
	 * bytes
	 */
	while (BufferBytes != 0) {
		/*
		 * If at any point during this read an error occurs, exit early
		 */
		if (XSysAce_mGetErrorReg(BaseAddress) != 0) {
			return 0;
		}

		(void) XSysAce_RegRead16(BaseAddress + XSA_DBR_OFFSET);
		BufferBytes -= 2;
	}

	return DataBytes;
}

/*****************************************************************************/
/**
*
* Write the specified number of bytes to the data buffer of the ACE controller.
* The data buffer, which is 32 bytes, can only be written two bytes at a time.
* Once the data buffer is written, we wait for it to be empty again before
* writing the next buffer's worth of data. If the size of the incoming buffer
* is not aligned with the System ACE data buffer size (32 bytes), then this
* routine pads out the data buffer with zeros so the entire data buffer is
* written. This is necessary for the ACE controller to process the data buffer.
*
* @param BaseAddress is the base address of the device
* @param BufferPtr is a pointer to a buffer used to write to the controller.
* @param Size is the number of bytes to write
*
* @return
*
* The total number of bytes written (not including pad bytes), or 0 if an
* error occurs.
*
* @note
*
* None.
*
******************************************************************************/
int XSysAce_WriteDataBuffer(u32 BaseAddress, u8 *BufferPtr, int Size)
{
	int DataBytes;		/* number of data bytes written */
	int BufferBytes;
	u16 Data;

	/*
	 * Write a sector two bytes at a time. We need to wait for the data
	 * buffer to be ready before writing the buffer.
	 */
	BufferBytes = 0;
	for (DataBytes = 0; DataBytes < Size;) {
		/*
		 * If at any point during this write an error occurs, exit early
		 */
		if (XSysAce_mGetErrorReg(BaseAddress) != 0) {
			return 0;
		}

		if (BufferBytes == 0) {
			/*
			 * Wait for CF read data buffer to ready, then reset buffer byte
			 * count
			 */
			while ((XSysAce_mGetStatusReg(BaseAddress)
				& XSA_SR_DATABUFRDY_MASK) == 0);

			BufferBytes = XSA_DATA_BUFFER_SIZE;
		}

		/*
		 * Need to send two bytes. Grab the first one from the incoming buffer
		 * because if we're here we know one more exists. Grab the second one
		 * from the incoming buffer if there are still any bytes remaining, or
		 * send a pad byte if the incoming buffer has been expired.
		 */
		Data = *BufferPtr++;
		DataBytes++;

		if (DataBytes < Size) {
			/* Still more data in the incoming buffer */
			Data |= ((u16) *BufferPtr++ << 8);
			DataBytes++;
		}
		else {
			/* No more data in the incoming buffer, send a pad byte of 0 */
			Data |= ((u16) 0 << 8);
		}

		XSysAce_RegWrite16(BaseAddress + XSA_DBR_OFFSET, Data);

		BufferBytes -= 2;
	}

	/*
	 * If a complete data buffer was not filled, fill it with pad bytes (zeros)
	 */
	while (BufferBytes != 0) {
		/*
		 * If at any point during this write an error occurs, exit early
		 */
		if (XSysAce_mGetErrorReg(BaseAddress) != 0) {
			return 0;
		}

		XSysAce_RegWrite16(BaseAddress + XSA_DBR_OFFSET, 0);
		BufferBytes -= 2;
	}

	return DataBytes;
}

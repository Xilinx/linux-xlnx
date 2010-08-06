/* $Id: xpacket_fifo_l_v2_00_a.c,v 1.1 2006/12/13 14:22:53 imanuilov Exp $ */
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
*       (c) Copyright 2003-2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xpacket_fifo_l_v2_00_a.c
*
* Contains low-level (Level 0) functions for the XPacketFifoV200a driver.
* See xpacket_fifo_v2_00_a.h for information about the high-level (Level 1)
* driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- ------------------------------------------------------
* 2.00a rpm  10/22/03  First release. Moved most of Level 1 driver functions
*                      into this layer.
* 2.00a rmm  02/24/04  Added L0WriteDRE function.
* 2.00a xd   10/27/04  Changed comments to support doxygen for API
*                      documentation.
* </pre>
*
*****************************************************************************/

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xio.h"
#include "xpacket_fifo_l_v2_00_a.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************* Variable Definitions ******************************/


/************************** Function Prototypes ******************************/

static int Write32(u32 RegBaseAddress, u32 DataBaseAddress,
		   u8 *BufferPtr, u32 ByteCount);

static int Write64(u32 RegBaseAddress, u32 DataBaseAddress,
		   u8 *BufferPtr, u32 ByteCount);

static int Read32(u32 RegBaseAddress, u32 DataBaseAddress,
		  u8 *BufferPtr, u32 ByteCount);

static int Read64(u32 RegBaseAddress, u32 DataBaseAddress,
		  u8 *BufferPtr, u32 ByteCount);


/*****************************************************************************/
/**
*
* Read data from a FIFO and puts it into a specified buffer. The packet FIFO is
* currently 32 or 64 bits wide such that an input buffer which is a series of
* bytes is filled from the FIFO a word at a time. If the requested byte count
* is not a multiple of 32/64 bit words, it is necessary for this function to
* format the remaining 32/64 bit word from the FIFO into a series of bytes in
* the buffer. There may be up to 3/7 extra bytes which must be extracted from
* the last word of the FIFO and put into the buffer.
*
* @param RegBaseAddress is the base address of the FIFO registers.
*
* @param DataBaseAddress is the base address of the FIFO keyhole.
*
* @param BufferPtr points to the memory buffer to write the data into. This
*        buffer must be 32 bit aligned or an alignment exception could be
*        generated. Since this buffer is a byte buffer, the data is assumed to
*        be endian independent.
*
* @param ByteCount contains the number of bytes to read from the FIFO. This
*        number of bytes must be present in the FIFO or an error will be
*        returned.
*
* @return
*
* XST_SUCCESS indicates the operation was successful.  If the number of
* bytes specified by the byte count is not present in the FIFO
* XST_PFIFO_LACK_OF_DATA is returned.
* <br><br>
* If the function was successful, the specified buffer is modified to contain
* the bytes which were removed from the FIFO.
*
* @note
*
* Note that the exact number of bytes which are present in the FIFO is
* not known by this function.  It can only check for a number of 32/64 bit
* words such that if the byte count specified is incorrect, but is still
* possible based on the number of words in the FIFO, up to 3/7 garbage bytes
* may be present at the end of the buffer.
* <br><br>
* This function assumes that if the device consuming data from the FIFO is
* a byte device, the order of the bytes to be consumed is from the most
* significant byte to the least significant byte of a 32/64 bit word removed
* from the FIFO.
*
******************************************************************************/
int XPacketFifoV200a_L0Read(u32 RegBaseAddress, u32 DataBaseAddress,
			    u8 *BufferPtr, u32 ByteCount)
{
	u32 Width;
	int Result = XST_FIFO_ERROR;

	/* determine the width of the FIFO
	 */
	Width = XIo_In32(RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) &
		XPF_V200A_FIFO_WIDTH_MASK;

	if ((Width == XPF_V200A_FIFO_WIDTH_LEGACY_TYPE) ||
	    (Width == XPF_V200A_FIFO_WIDTH_32BITS_TYPE)) {
		Result = Read32(RegBaseAddress, DataBaseAddress, BufferPtr,
				ByteCount);
	}
	else if (Width == XPF_V200A_FIFO_WIDTH_64BITS_TYPE) {
		Result = Read64(RegBaseAddress, DataBaseAddress, BufferPtr,
				ByteCount);
	}

	return Result;

}

/*****************************************************************************/
/**
*
* Write data into a packet FIFO. The packet FIFO is currently 32 or 64 bits
* wide such that an input buffer which is a series of bytes must be written
* into the FIFO a word at a time. If the buffer is not a multiple of 32 bit
* words, it is necessary for this function to format the remaining bytes into
* a single 32 bit word to be inserted into the FIFO. This is necessary to
* avoid any accesses past the end of the buffer.
*
* @param RegBaseAddress is the base address of the FIFO registers.
*
* @param DataBaseAddress is the base address of the FIFO keyhole.
*
* @param BufferPtr points to the memory buffer that data is to be read from
*        and written into the FIFO. Since this buffer is a byte buffer, the
*        data is assumed to be endian independent. This buffer must be 32 bit
*        aligned or an alignment exception could be generated.
*
* @param ByteCount contains the number of bytes to read from the buffer and to
*        write to the FIFO.
*
* @return
*
* XST_SUCCESS is returned if the operation succeeded.  If there is not enough
* room in the FIFO to hold the specified bytes, XST_PFIFO_NO_ROOM is
* returned.
*
* @note
*
* This function assumes that if the device inserting data into the FIFO is
* a byte device, the order of the bytes in each 32/64 bit word is from the most
* significant byte to the least significant byte.
*
******************************************************************************/
int XPacketFifoV200a_L0Write(u32 RegBaseAddress,
			     u32 DataBaseAddress, u8 *BufferPtr, u32 ByteCount)
{
	u32 Width;
	int Result = XST_FIFO_ERROR;


	/* determine the width of the FIFO
	 */
	Width = XIo_In32(RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) &
		XPF_V200A_FIFO_WIDTH_MASK;

	if ((Width == XPF_V200A_FIFO_WIDTH_LEGACY_TYPE) ||
	    (Width == XPF_V200A_FIFO_WIDTH_32BITS_TYPE)) {
		Result = Write32(RegBaseAddress, DataBaseAddress, BufferPtr,
				 ByteCount);
	}
	else if (Width == XPF_V200A_FIFO_WIDTH_64BITS_TYPE) {
		Result = Write64(RegBaseAddress, DataBaseAddress, BufferPtr,
				 ByteCount);
	}

	return Result;

}

/*****************************************************************************/
/**
*
* Write data into a packet FIFO configured for the Data Realignment Engine
* (DRE). A packet FIFO channel configured in this way will accept any
* combination of byte, half-word, or word writes. The DRE will shift the data
* into the correct byte lane.
*
* @param RegBaseAddress is the base address of the FIFO registers.
*
* @param DataBaseAddress is the base address of the FIFO keyhole.
*
* @param BufferPtr points to the memory buffer that data is to be read from
*        and written into the FIFO. Since this buffer is a byte buffer, the
*        data is assumed to be endian independent. There are no alignment
*        restrictions.
*
* @param ByteCount contains the number of bytes to read from the buffer and to
*        write to the FIFO.
*
* @return
*
* XST_SUCCESS is returned if the operation succeeded.  If there is not enough
* room in the FIFO to hold the specified bytes, XST_PFIFO_NO_ROOM is
* returned.
*
* @note
*
* This function assumes that if the device inserting data into the FIFO is
* a byte device, the order of the bytes in each 32/64 bit word is from the most
* significant byte to the least significant byte.
*
******************************************************************************/
int XPacketFifoV200a_L0WriteDre(u32 RegBaseAddress,
				u32 DataBaseAddress,
				u8 *BufferPtr, u32 ByteCount)
{
	u32 FifoRoomLeft;
	u32 BytesLeft;
	u32 Width;

	/* calculate how many slots are left in the FIFO
	 */
	FifoRoomLeft =
		XIo_In32(RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET)
		& XPF_V200A_COUNT_MASK;

	/* determine the width of the FIFO
	 */
	Width = XIo_In32(RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) &
		XPF_V200A_FIFO_WIDTH_MASK;

	/* from the width, determine how many bytes can be written to the FIFO
	 */
	if ((Width == XPF_V200A_FIFO_WIDTH_LEGACY_TYPE) ||
	    (Width == XPF_V200A_FIFO_WIDTH_32BITS_TYPE)) {
		FifoRoomLeft *= 4;
	}
	else if (Width == XPF_V200A_FIFO_WIDTH_64BITS_TYPE) {
		FifoRoomLeft *= 8;
	}

	/* Make sure there's enough room in the FIFO */
	if (FifoRoomLeft < ByteCount) {
		return XST_PFIFO_NO_ROOM;
	}

	/* Determine the number of bytes to write until 32 bit alignment is
	 * reached, then write those bytes to the FIFO one byte at a time
	 */
	BytesLeft = (unsigned) BufferPtr % sizeof(u32);
	ByteCount -= BytesLeft;
	while (BytesLeft--) {
		XIo_Out8(DataBaseAddress, *BufferPtr++);
	}

	/* Write as many 32 bit words as we can */
	BytesLeft = ByteCount;
	while (BytesLeft >= sizeof(u32)) {
		XIo_Out32(DataBaseAddress, *(u32 *) BufferPtr);
		BufferPtr += sizeof(u32);
		BytesLeft -= sizeof(u32);
	}

	/* Write remaining bytes */
	while (BytesLeft--) {
		XIo_Out8(DataBaseAddress, *BufferPtr++);
	}

	return XST_SUCCESS;

}

/*****************************************************************************/
/**
*
* Read data from a FIFO and puts it into a specified buffer. The packet FIFO
* is 32 bits wide such that an input buffer which is a series of bytes is
* filled from the FIFO a word at a time. If the requested byte count is not
* a multiple of 32 bit words, it is necessary for this function to format the
* remaining 32 bit word from the FIFO into a series of bytes in the buffer.
* There may be up to 3 extra bytes which must be extracted from the last word
* of the FIFO and put into the buffer.
*
* @param RegBaseAddress is the base address of the FIFO registers.
*
* @param DataBaseAddress is the base address of the FIFO keyhole.
*
* @param BufferPtr points to the memory buffer to write the data into. This
*        buffer must be 32 bit aligned or an alignment exception could be
*        generated. Since this buffer is a byte buffer, the data is assumed to
*        be endian independent.
*
* @param ByteCount contains the number of bytes to read from the FIFO. This
*        number of bytes must be present in the FIFO or an error will be
*        returned.
*
* @return
*
* XST_SUCCESS indicates the operation was successful.  If the number of
* bytes specified by the byte count is not present in the FIFO
* XST_PFIFO_LACK_OF_DATA is returned.
* <br><br>
* If the function was successful, the specified buffer is modified to contain
* the bytes which were removed from the FIFO.
*
* @note
*
* Note that the exact number of bytes which are present in the FIFO is
* not known by this function.  It can only check for a number of 32 bit
* words such that if the byte count specified is incorrect, but is still
* possible based on the number of words in the FIFO, up to 3 garbage bytes
* may be present at the end of the buffer.
* <br><br>
* This function assumes that if the device consuming data from the FIFO is
* a byte device, the order of the bytes to be consumed is from the most
* significant byte to the least significant byte of a 32 bit word removed
* from the FIFO.
*
******************************************************************************/
static int Read32(u32 RegBaseAddress, u32 DataBaseAddress,
		  u8 *BufferPtr, u32 ByteCount)
{
	u32 FifoCount;
	u32 WordCount;
	u32 ExtraByteCount;
	u32 *WordBuffer = (u32 *) BufferPtr;

	/* get the count of how many 32 bit words are in the FIFO, if there
	 * aren't enough words to satisfy the request, return an error
	 */

	FifoCount =
		XIo_In32(RegBaseAddress +
			 XPF_V200A_COUNT_STATUS_REG_OFFSET) &
		XPF_V200A_COUNT_MASK;

	if ((FifoCount * XPF_V200A_32BIT_FIFO_WIDTH_BYTE_COUNT) < ByteCount) {
		return XST_PFIFO_LACK_OF_DATA;
	}

	/* calculate the number of words to read from the FIFO before the word
	 * containing the extra bytes, and calculate the number of extra bytes
	 * the extra bytes are defined as those at the end of the buffer when
	 * the buffer does not end on a 32 bit boundary
	 */
	WordCount = ByteCount / XPF_V200A_32BIT_FIFO_WIDTH_BYTE_COUNT;
	ExtraByteCount = ByteCount % XPF_V200A_32BIT_FIFO_WIDTH_BYTE_COUNT;

	/* Read the 32 bit words from the FIFO for all the buffer except the
	 * last word which contains the extra bytes, the following code assumes
	 * that the buffer is 32 bit aligned, otherwise an alignment exception
	 * could be generated
	 */
	for (FifoCount = 0; FifoCount < WordCount; FifoCount++) {
		WordBuffer[FifoCount] = XIo_In32(DataBaseAddress);
	}

	/* if there are extra bytes to handle, read the last word from the FIFO
	 * and insert the extra bytes into the buffer
	 */
	if (ExtraByteCount > 0) {
		u32 LastWord;
		u8 *WordPtr;
		u8 *ExtraBytesBuffer = (u8 *) (WordBuffer + WordCount);

		/* get the last word from the FIFO for the extra bytes */

		LastWord = XIo_In32(DataBaseAddress);

		/* one extra byte in the last word, put the byte into the next
		 * location of the buffer, bytes in a word of the FIFO are ordered
		 * from most significant byte to least
		 */
		WordPtr = (u8 *) &LastWord;
		if (ExtraByteCount == 1) {
			ExtraBytesBuffer[0] = WordPtr[0];
		}

		/* two extra bytes in the last word, put each byte into the next
		 * two locations of the buffer
		 */
		else if (ExtraByteCount == 2) {
			ExtraBytesBuffer[0] = WordPtr[0];
			ExtraBytesBuffer[1] = WordPtr[1];
		}
		/* three extra bytes in the last word, put each byte into the next
		 * three locations of the buffer
		 */
		else if (ExtraByteCount == 3) {
			ExtraBytesBuffer[0] = WordPtr[0];
			ExtraBytesBuffer[1] = WordPtr[1];
			ExtraBytesBuffer[2] = WordPtr[2];
		}
	}

	return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* Read data from a FIFO and puts it into a specified buffer. The packet FIFO
* is 64 bits wide such that an input buffer which is a series of bytes is
* filled from the FIFO a word at a time. If the requested byte count is not
* a multiple of 64 bit words, it is necessary for this function to format the
* remaining 64 bit word from the FIFO into a series of bytes in the buffer.
* There may be up to 7 extra bytes which must be extracted from the last word
* of the FIFO and put into the buffer.
*
* @param RegBaseAddress is the base address of the FIFO registers.
*
* @param DataBaseAddress is the base address of the FIFO keyhole.
*
* @param BufferPtr points to the memory buffer to write the data into. This
*        buffer must be 32 bit aligned or an alignment exception could be
*        generated. Since this buffer is a byte buffer, the data is assumed to
*        be endian independent.
*
* @param ByteCount contains the number of bytes to read from the FIFO. This
*        number of bytes must be present in the FIFO or an error will be
*        returned.
*
* @return
*
* XST_SUCCESS indicates the operation was successful.  If the number of
* bytes specified by the byte count is not present in the FIFO
* XST_PFIFO_LACK_OF_DATA is returned.
* <br><br>
* If the function was successful, the specified buffer is modified to contain
* the bytes which were removed from the FIFO.
*
* @note
*
* Note that the exact number of bytes which are present in the FIFO is
* not known by this function.  It can only check for a number of 64 bit
* words such that if the byte count specified is incorrect, but is still
* possible based on the number of words in the FIFO, up to 7 garbage bytes
* may be present at the end of the buffer.
* <br><br>
* This function assumes that if the device consuming data from the FIFO is
* a byte device, the order of the bytes to be consumed is from the most
* significant byte to the least significant byte of a 64 bit word removed
* from the FIFO.
*
******************************************************************************/
static int Read64(u32 RegBaseAddress, u32 DataBaseAddress,
		  u8 *BufferPtr, u32 ByteCount)
{
	u32 FifoCount;
	u32 WordCount;
	u32 ExtraByteCount;
	u32 *WordBuffer = (u32 *) BufferPtr;

	/* get the count of how many 64 bit words are in the FIFO, if there
	 * aren't enough words to satisfy the request, return an error
	 */

	FifoCount =
		XIo_In32(RegBaseAddress +
			 XPF_V200A_COUNT_STATUS_REG_OFFSET) &
		XPF_V200A_COUNT_MASK;

	if ((FifoCount * XPF_V200A_64BIT_FIFO_WIDTH_BYTE_COUNT) < ByteCount) {
		return XST_PFIFO_LACK_OF_DATA;
	}

	/* calculate the number of words to read from the FIFO before the word
	 * containing the extra bytes, and calculate the number of extra bytes
	 * the extra bytes are defined as those at the end of the buffer when
	 * the buffer does not end on a 32 bit boundary
	 */
	WordCount = ByteCount / XPF_V200A_64BIT_FIFO_WIDTH_BYTE_COUNT;
	ExtraByteCount = ByteCount % XPF_V200A_64BIT_FIFO_WIDTH_BYTE_COUNT;

	/* Read the 64 bit words from the FIFO for all the buffer except the
	 * last word which contains the extra bytes, the following code assumes
	 * that the buffer is 32 bit aligned, otherwise an alignment exception
	 * could be generated. The MSWord must be read first followed by the
	 * LSWord
	 */
	for (FifoCount = 0; FifoCount < WordCount; FifoCount++) {
		WordBuffer[(FifoCount * 2)] = XIo_In32(DataBaseAddress);
		WordBuffer[(FifoCount * 2) + 1] = XIo_In32(DataBaseAddress + 4);
	}

	/* if there are extra bytes to handle, read the last word from the FIFO
	 * and insert the extra bytes into the buffer
	 */
	if (ExtraByteCount > 0) {
		u32 MSLastWord;
		u32 LSLastWord;
		u8 *WordPtr;
		u8 *ExtraBytesBuffer = (u8 *) (WordBuffer + (WordCount * 2));
		u8 Index = 0;

		/* get the last word from the FIFO for the extra bytes */

		MSLastWord = XIo_In32(DataBaseAddress);
		LSLastWord = XIo_In32(DataBaseAddress + 4);

		/* four or more extra bytes in the last word, put the byte into
		 * the next location of the buffer, bytes in a word of the FIFO
		 * are ordered from most significant byte to least
		 */
		WordPtr = (u8 *) &MSLastWord;
		if (ExtraByteCount >= 4) {
			ExtraBytesBuffer[Index] = WordPtr[0];
			ExtraBytesBuffer[Index + 1] = WordPtr[1];
			ExtraBytesBuffer[Index + 2] = WordPtr[2];
			ExtraBytesBuffer[Index + 3] = WordPtr[3];
			ExtraByteCount = ExtraByteCount - 4;
			MSLastWord = LSLastWord;
			Index = 4;
		}

		/* one extra byte in the last word, put the byte into the next
		 * location of the buffer, bytes in a word of the FIFO are
		 * ordered from most significant byte to least
		 */
		if (ExtraByteCount == 1) {
			ExtraBytesBuffer[Index] = WordPtr[0];
		}

		/* two extra bytes in the last word, put each byte into the next
		 * two locations of the buffer
		 */
		else if (ExtraByteCount == 2) {
			ExtraBytesBuffer[Index] = WordPtr[0];
			ExtraBytesBuffer[Index + 1] = WordPtr[1];
		}
		/* three extra bytes in the last word, put each byte into the next
		 * three locations of the buffer
		 */
		else if (ExtraByteCount == 3) {
			ExtraBytesBuffer[Index] = WordPtr[0];
			ExtraBytesBuffer[Index + 1] = WordPtr[1];
			ExtraBytesBuffer[Index + 2] = WordPtr[2];
		}
	}

	return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* Write data into a 32 bit packet FIFO. The packet FIFO is 32 bits wide in this
* function call such that an input buffer which is a series of bytes must be
* written into the FIFO a word at a time. If the buffer is not a multiple of
* 32 bit words, it is necessary for this function to format the remaining bytes
* into a single 32 bit word to be inserted into the FIFO. This is necessary to
* avoid any accesses past the end of the buffer.
*
* @param RegBaseAddress is the base address of the FIFO registers.
*
* @param DataBaseAddress is the base address of the FIFO keyhole.
*
* @param BufferPtr points to the memory buffer that data is to be read from
*        and written into the FIFO. Since this buffer is a byte buffer, the
*        data is assumed to be endian independent. This buffer must be 32 bit
*        aligned or an alignment exception could be generated.
* @param ByteCount contains the number of bytes to read from the buffer and to
*        write to the FIFO.
*
* @return
*
* XST_SUCCESS is returned if the operation succeeded.  If there is not enough
* room in the FIFO to hold the specified bytes, XST_PFIFO_NO_ROOM is
* returned.
*
* @note
*
* This function assumes that if the device inserting data into the FIFO is
* a byte device, the order of the bytes in each 32 bit word is from the most
* significant byte to the least significant byte.
*
******************************************************************************/
static int Write32(u32 RegBaseAddress, u32 DataBaseAddress,
		   u8 *BufferPtr, u32 ByteCount)
{
	u32 FifoCount;
	u32 WordCount;
	u32 ExtraByteCount;
	u32 *WordBuffer = (u32 *) BufferPtr;

	/* get the count of how many words may be inserted into the FIFO */

	FifoCount =
		XIo_In32(RegBaseAddress +
			 XPF_V200A_COUNT_STATUS_REG_OFFSET) &
		XPF_V200A_COUNT_MASK;

	/* Calculate the number of 32 bit words required to insert the
	 * specified number of bytes in the FIFO and determine the number
	 * of extra bytes if the buffer length is not a multiple of 32 bit
	 * words
	 */

	WordCount = ByteCount / XPF_V200A_32BIT_FIFO_WIDTH_BYTE_COUNT;
	ExtraByteCount = ByteCount % XPF_V200A_32BIT_FIFO_WIDTH_BYTE_COUNT;

	/* take into account the extra bytes in the total word count */

	if (ExtraByteCount > 0) {
		WordCount++;
	}

	/* if there's not enough room in the FIFO to hold the specified
	 * number of bytes, then indicate an error,
	 */
	if (FifoCount < WordCount) {
		return XST_PFIFO_NO_ROOM;
	}

	/* readjust the word count to not take into account the extra bytes */

	if (ExtraByteCount > 0) {
		WordCount--;
	}

	/* Write all the bytes of the buffer which can be written as 32 bit
	 * words into the FIFO, waiting to handle the extra bytes separately
	 */
	for (FifoCount = 0; FifoCount < WordCount; FifoCount++) {
		XIo_Out32(DataBaseAddress, WordBuffer[FifoCount]);
	}

	/* if there are extra bytes to handle, extract them from the buffer
	 * and create a 32 bit word and write it to the FIFO
	 */
	if (ExtraByteCount > 0) {
		u32 LastWord = 0;
		u8 *WordPtr;
		u8 *ExtraBytesBuffer = (u8 *) (WordBuffer + WordCount);

		/* one extra byte in the buffer, put the byte into the last word
		 * to be inserted into the FIFO, perform this processing inline
		 * rather than in a loop to help performance
		 */
		WordPtr = (u8 *) &LastWord;
		if (ExtraByteCount == 1) {
			WordPtr[0] = ExtraBytesBuffer[0];
		}

		/* two extra bytes in the buffer, put each byte into the last word
		 * to be inserted into the FIFO
		 */
		else if (ExtraByteCount == 2) {
			WordPtr[0] = ExtraBytesBuffer[0];
			WordPtr[1] = ExtraBytesBuffer[1];
		}

		/* three extra bytes in the buffer, put each byte into the last
		 * word to be inserted into the FIFO
		 */
		else if (ExtraByteCount == 3) {
			WordPtr[0] = ExtraBytesBuffer[0];
			WordPtr[1] = ExtraBytesBuffer[1];
			WordPtr[2] = ExtraBytesBuffer[2];
		}

		/* write the last 32 bit word to the FIFO and return with
		 * no errors
		 */

		XIo_Out32(DataBaseAddress, LastWord);
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Write data into a 64 bit packet FIFO. The packet FIFO is 64 bits wide in this
* function call such that an input buffer which is a series of bytes must be
* written into the FIFO a word at a time. If the buffer is not a multiple of
* 64 bit words, it is necessary for this function to format the remaining bytes
* into two 32 bit words to be inserted into the FIFO. This is necessary to
* avoid any accesses past the end of the buffer.
*
* @param RegBaseAddress is the base address of the FIFO registers.
*
* @param DataBaseAddress is the base address of the FIFO keyhole.
*
* @param BufferPtr points to the memory buffer that data is to be read from
*        and written into the FIFO. Since this buffer is a byte buffer, the
*        data is assumed to be endian independent. This buffer must be 32 bit
*        aligned or an alignment exception could be generated.
*
* @param ByteCount contains the number of bytes to read from the buffer and to
*        write to the FIFO.
*
* @return
*
* XST_SUCCESS is returned if the operation succeeded.  If there is not enough
* room in the FIFO to hold the specified bytes, XST_PFIFO_NO_ROOM is
* returned.
*
* @note
*
* This function assumes that if the device inserting data into the FIFO is
* a byte device, the order of the bytes in each 64 bit word is from the most
* significant byte to the least significant byte.
*
******************************************************************************/
static int Write64(u32 RegBaseAddress, u32 DataBaseAddress,
		   u8 *BufferPtr, u32 ByteCount)
{
	u32 FifoCount;
	u32 WordCount;
	u32 ExtraByteCount;
	u32 *WordBuffer = (u32 *) BufferPtr;

	/* get the count of how many words may be inserted into the FIFO */

	FifoCount =
		XIo_In32(RegBaseAddress +
			 XPF_V200A_COUNT_STATUS_REG_OFFSET) &
		XPF_V200A_COUNT_MASK;

	/* Calculate the number of 64 bit words required to insert the
	 * specified number of bytes in the FIFO and determine the number
	 * of extra bytes if the buffer length is not a multiple of 64 bit
	 * words
	 */

	WordCount = ByteCount / XPF_V200A_64BIT_FIFO_WIDTH_BYTE_COUNT;
	ExtraByteCount = ByteCount % XPF_V200A_64BIT_FIFO_WIDTH_BYTE_COUNT;

	/* take into account the extra bytes in the total word count */

	if (ExtraByteCount > 0) {
		WordCount++;
	}

	/* if there's not enough room in the FIFO to hold the specified
	 * number of bytes, then indicate an error,
	 */
	if (FifoCount < WordCount) {
		return XST_PFIFO_NO_ROOM;
	}

	/* readjust the word count to not take into account the extra bytes */

	if (ExtraByteCount > 0) {
		WordCount--;
	}

	/* Write all the bytes of the buffer which can be written as 32 bit
	 * words into the FIFO, waiting to handle the extra bytes separately
	 * The MSWord must be written first followed by the LSWord
	 */
	for (FifoCount = 0; FifoCount < WordCount; FifoCount++) {
		XIo_Out32(DataBaseAddress, WordBuffer[(FifoCount * 2)]);
		XIo_Out32(DataBaseAddress + 4, WordBuffer[(FifoCount * 2) + 1]);
	}

	/* if there are extra bytes to handle, extract them from the buffer
	 * and create two 32 bit words and write to the FIFO
	 */
	if (ExtraByteCount > 0) {

		u32 MSLastWord = 0;
		u32 LSLastWord = 0;
		u8 Index = 0;
		u8 *WordPtr;
		u8 *ExtraBytesBuffer = (u8 *) (WordBuffer + (WordCount * 2));

		/* four extra bytes in the buffer, put the bytes into the last word
		 * to be inserted into the FIFO, perform this processing inline
		 * rather than in a loop to help performance
		 */
		WordPtr = (u8 *) &MSLastWord;

		if (ExtraByteCount >= 4) {
			WordPtr[0] = ExtraBytesBuffer[Index];
			WordPtr[1] = ExtraBytesBuffer[Index + 1];
			WordPtr[2] = ExtraBytesBuffer[Index + 2];
			WordPtr[3] = ExtraBytesBuffer[Index + 3];
			ExtraByteCount = ExtraByteCount - 4;
			WordPtr = (u8 *) &LSLastWord;
			Index = 4;
		}

		/* one extra byte in the buffer, put the byte into the last word
		 * to be inserted into the FIFO, perform this processing inline
		 * rather than in a loop to help performance
		 */
		if (ExtraByteCount == 1) {
			WordPtr[0] = ExtraBytesBuffer[Index];
		}

		/* two extra bytes in the buffer, put each byte into the last word
		 * to be inserted into the FIFO
		 */
		else if (ExtraByteCount == 2) {
			WordPtr[0] = ExtraBytesBuffer[Index];
			WordPtr[1] = ExtraBytesBuffer[Index + 1];
		}

		/* three extra bytes in the buffer, put each byte into the last
		 * word to be inserted into the FIFO
		 */
		else if (ExtraByteCount == 3) {
			WordPtr[0] = ExtraBytesBuffer[Index];
			WordPtr[1] = ExtraBytesBuffer[Index + 1];
			WordPtr[2] = ExtraBytesBuffer[Index + 2];
		}

		/* write the last 64 bit word to the FIFO and return with no errors
		 * The MSWord must be written first followed by the LSWord
		 */
		XIo_Out32(DataBaseAddress, MSLastWord);
		XIo_Out32(DataBaseAddress + 4, LSLastWord);
	}

	return XST_SUCCESS;
}

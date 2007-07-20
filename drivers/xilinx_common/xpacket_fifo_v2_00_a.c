/* $Id: xpacket_fifo_v2_00_a.c,v 1.1 2006/12/13 14:23:11 imanuilov Exp $ */
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
*       (c) Copyright 2002-2003 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xpacket_fifo_v2_00_a.c
*
* Contains functions for the XPacketFifoV200a component. See
* xpacket_fifo_v2_00_a.h for more information about the component.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 2.00a ecm 12/30/02  First release
* 2.00a rmm 05/14/03  Fixed diab compiler warnings
* 2.00a rpm 10/22/03  Created and made use of Level 0 driver
* 2.00a rmm 02/24/04  Added WriteDRE function.
* 2.00a xd  10/27/04  Changed comments to support doxygen for API
*                     documentation.
* </pre>
*
*****************************************************************************/

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xio.h"
#include "xstatus.h"
#include "xpacket_fifo_v2_00_a.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************* Variable Definitions ******************************/


/************************** Function Prototypes ******************************/


/*****************************************************************************/
/**
*
* This function initializes a packet FIFO.  Initialization resets the
* FIFO such that it's empty and ready to use.
*
* @param    InstancePtr contains a pointer to the FIFO to operate on.
*
* @param    RegBaseAddress contains the base address of the registers for
*           the packet FIFO.
*
* @param    DataBaseAddress contains the base address of the data for
*           the packet FIFO.
*
* @return   Always returns XST_SUCCESS.
*
* @note     None.
*
******************************************************************************/
int XPacketFifoV200a_Initialize(XPacketFifoV200a * InstancePtr,
				u32 RegBaseAddress, u32 DataBaseAddress)
{
	/* assert to verify input argument are valid */

	XASSERT_NONVOID(InstancePtr != NULL);

	/* initialize the component variables to the specified state */

	InstancePtr->RegBaseAddress = RegBaseAddress;
	InstancePtr->DataBaseAddress = DataBaseAddress;
	InstancePtr->IsReady = XCOMPONENT_IS_READY;

	/* reset the FIFO such that it's empty and ready to use and indicate the
	 * initialization was successful, note that the is ready variable must be
	 * set prior to calling the reset function to prevent an assert
	 */
	XPF_V200A_RESET(InstancePtr);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function performs a self-test on the specified packet FIFO.  The self
* test resets the FIFO and reads a register to determine if it is the correct
* reset value.  This test is destructive in that any data in the FIFO will
* be lost.
*
* @param InstancePtr is a pointer to the packet FIFO to be operated on.
*
* @param FifoType specifies the type of FIFO, read or write, for the self test.
*        The FIFO type is specified by the values XPF_V200A_READ_FIFO_TYPE or
*        XPF_V200A_WRITE_FIFO_TYPE.
*
* @return
*
* XST_SUCCESS is returned if the selftest is successful, or
* XST_PFIFO_BAD_REG_VALUE indicating that the value read back from the
* occupancy/vacancy count register after a reset does not match the
* specified reset value.
*
* @note
*
* None.
*
******************************************************************************/
int XPacketFifoV200a_SelfTest(XPacketFifoV200a * InstancePtr, u32 FifoType)
{
	u32 Register;

	/* assert to verify valid input arguments */

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID((FifoType == XPF_V200A_READ_FIFO_TYPE) ||
			(FifoType == XPF_V200A_WRITE_FIFO_TYPE));
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* reset the FIFO and then check to make sure the occupancy/vacancy
	 * register contents are correct for a reset condition
	 */
	XPF_V200A_RESET(InstancePtr);

	Register = XIo_In32(InstancePtr->RegBaseAddress +
			    XPF_V200A_COUNT_STATUS_REG_OFFSET);

	/* check the value of the register to ensure that it's correct for the
	 * specified FIFO type since both FIFO types reset to empty, but a bit
	 * in the register changes definition based upon FIFO type
	 */

	if (FifoType == XPF_V200A_READ_FIFO_TYPE) {
		/* check the register value for a read FIFO which should be empty */

		if ((Register & ~(XPF_V200A_FIFO_WIDTH_MASK)) !=
		    XPF_V200A_EMPTY_FULL_MASK) {
			return XST_PFIFO_BAD_REG_VALUE;
		}
	}
	else {
		/* check the register value for a write FIFO which should not be full
		 * on reset
		 */
		if (((Register & ~(XPF_V200A_FIFO_WIDTH_MASK) &
		      XPF_V200A_EMPTY_FULL_MASK)) != 0) {
			return XST_PFIFO_BAD_REG_VALUE;
		}
	}

	/* check the register value for the proper FIFO width */

	Register &= ~XPF_V200A_EMPTY_FULL_MASK;

	if (((Register & XPF_V200A_FIFO_WIDTH_MASK) !=
	     XPF_V200A_FIFO_WIDTH_LEGACY_TYPE) &&
	    ((Register & XPF_V200A_FIFO_WIDTH_MASK) !=
	     XPF_V200A_FIFO_WIDTH_32BITS_TYPE) &&
	    ((Register & XPF_V200A_FIFO_WIDTH_MASK) !=
	     XPF_V200A_FIFO_WIDTH_64BITS_TYPE)) {
		return XST_PFIFO_BAD_REG_VALUE;
	}

	/* the test was successful */

	return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* Read data from a FIFO and puts it into a specified buffer. This function
* invokes the Level 0 driver function to read the FIFO.
*
* @param InstancePtr contains a pointer to the FIFO to operate on.
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
* - XST_SUCCESS if the operation was successful
*   <br><br>
* - XST_PFIFO_LACK_OF_DATA if the number of bytes specified by the byte count
*   is not present in the FIFO.
*
* @note
*
* None.
*
******************************************************************************/
int XPacketFifoV200a_Read(XPacketFifoV200a * InstancePtr,
			  u8 *BufferPtr, u32 ByteCount)
{
	/* assert to verify valid input arguments including 32 bit alignment of
	 * the buffer pointer
	 */
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BufferPtr != NULL);
	XASSERT_NONVOID(((u32) BufferPtr &
			 (XPF_V200A_32BIT_FIFO_WIDTH_BYTE_COUNT - 1)) == 0);
	XASSERT_NONVOID(ByteCount != 0);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	return XPacketFifoV200a_L0Read(InstancePtr->RegBaseAddress,
				       InstancePtr->DataBaseAddress,
				       BufferPtr, ByteCount);
}

/*****************************************************************************/
/**
*
* Write data into a packet FIFO. This function invokes the Level 0 driver
* function to read the FIFO.
*
* @param InstancePtr contains a pointer to the FIFO to operate on.
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
* - XST_SUCCESS is returned if the operation succeeded.
*   <br><br>
* - XST_PFIFO_NO_ROOM is returned if there is not enough room in the FIFO to
*   hold the specified bytes.
*
* @note
*
* None.
*
******************************************************************************/
int XPacketFifoV200a_Write(XPacketFifoV200a * InstancePtr,
			   u8 *BufferPtr, u32 ByteCount)
{
	/* assert to verify valid input arguments including 32 bit alignment of
	 * the buffer pointer
	 */
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BufferPtr != NULL);
	XASSERT_NONVOID(((u32) BufferPtr &
			 (XPF_V200A_32BIT_FIFO_WIDTH_BYTE_COUNT - 1)) == 0);
	XASSERT_NONVOID(ByteCount != 0);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);


	return XPacketFifoV200a_L0Write(InstancePtr->RegBaseAddress,
					InstancePtr->DataBaseAddress,
					BufferPtr, ByteCount);
}


/*****************************************************************************/
/**
*
* Write data into a packet FIFO configured with the Data Realignment engine
* (DRE). There are no alignment restrictions. The FIFO can be written on any
* byte boundary. The FIFO must be at least 32 bits wide.
*
* @param InstancePtr contains a pointer to the FIFO to operate on.
*
* @param BufferPtr points to the memory buffer that data is to be read from
*        and written into the FIFO. Since this buffer is a byte buffer, the
*        data is assumed to be endian independent.
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
int XPacketFifoV200a_WriteDre(XPacketFifoV200a * InstancePtr,
			      u8 *BufferPtr, u32 ByteCount)
{
	/* assert to verify valid input arguments */
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BufferPtr != NULL);
	XASSERT_NONVOID(ByteCount != 0);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	return XPacketFifoV200a_L0WriteDre(InstancePtr->RegBaseAddress,
					   InstancePtr->DataBaseAddress,
					   BufferPtr, ByteCount);
}

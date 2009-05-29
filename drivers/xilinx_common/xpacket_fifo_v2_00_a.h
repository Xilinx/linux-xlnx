/* $Id: xpacket_fifo_v2_00_a.h,v 1.1 2006/12/13 14:23:19 imanuilov Exp $ */
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
*       (c) Copyright 2002-2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xpacket_fifo_v2_00_a.h
*
* This component is a common component because it's primary purpose is to
* prevent code duplication in drivers. A driver which must handle a packet
* FIFO uses this component rather than directly manipulating a packet FIFO.
*
* A FIFO is a device which has dual port memory such that one user may be
* inserting data into the FIFO while another is consuming data from the FIFO.
* A packet FIFO is designed for use with packet protocols such as Ethernet and
* ATM.  It is typically only used with devices when DMA and/or Scatter Gather
* is used.  It differs from a nonpacket FIFO in that it does not provide any
* interrupts for thresholds of the FIFO such that it is less useful without
* DMA.
*
* @note
*
* This component has the capability to generate an interrupt when an error
* condition occurs.  It is the user's responsibility to provide the interrupt
* processing to handle the interrupt. This component provides the ability to
* determine if that interrupt is active, a deadlock condition, and the ability
* to reset the FIFO to clear the condition. In this condition, the device which
* is using the FIFO should also be reset to prevent other problems. This error
* condition could occur as a normal part of operation if the size of the FIFO
* is not setup correctly.  See the hardware IP specification for more details.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 2.00a ecm 12/30/02  First release
* 2.00a rpm 10/22/03  Created and made use of Level 0 driver
* 2.00a rmm 02/24/04  Added WriteDre function.
* 2.00a xd  10/27/04  Changed comments to support doxygen for API
*                     documentation.
* </pre>
*
*****************************************************************************/
#ifndef XPACKET_FIFO_V200A_H	/* prevent circular inclusions */
#define XPACKET_FIFO_V200A_H	/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xstatus.h"
#include "xpacket_fifo_l_v2_00_a.h"

/************************** Constant Definitions *****************************/

/* See the low-level header file for constant definitions */

/**************************** Type Definitions *******************************/

/**
 * The XPacketFifo driver instance data. The driver is required to allocate a
 * variable of this type for every packet FIFO in the device.
 */
typedef struct {
	u32 RegBaseAddress; /**< Base address of registers */
	u32 IsReady;	    /**< Device is initialized and ready */
	u32 DataBaseAddress;/**< Base address of data for FIFOs */
} XPacketFifoV200a;

/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
/**
*
* Reset the specified packet FIFO.  Resetting a FIFO will cause any data
* contained in the FIFO to be lost.
*
* @param    InstancePtr contains a pointer to the FIFO to operate on.
*
* @return   None.
*
* @note     C Signature: void XPF_V200A_RESET(XPacketFifoV200a *InstancePtr)
*
******************************************************************************/
#define XPF_V200A_RESET(InstancePtr) \
    XIo_Out32((InstancePtr)->RegBaseAddress + XPF_V200A_RESET_REG_OFFSET, XPF_V200A_RESET_FIFO_MASK);


/*****************************************************************************/
/**
*
* Get the occupancy count for a read packet FIFO and the vacancy count for a
* write packet FIFO. These counts indicate the number of 32-bit words
* contained (occupancy) in the FIFO or the number of 32-bit words available
* to write (vacancy) in the FIFO.
*
* @param    InstancePtr contains a pointer to the FIFO to operate on.
*
* @return   The occupancy or vacancy count for the specified packet FIFO.
*
* @note
*
* C Signature: u32 XPF_V200A_GET_COUNT(XPacketFifoV200a *InstancePtr)
*
******************************************************************************/
#define XPF_V200A_GET_COUNT(InstancePtr) \
    (XIo_In32((InstancePtr)->RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) & \
    XPF_V200A_COUNT_MASK)


/*****************************************************************************/
/**
*
* Determine if the specified packet FIFO is almost empty. Almost empty is
* defined for a read FIFO when there is only one data word in the FIFO.
*
* @param InstancePtr contains a pointer to the FIFO to operate on.
*
* @return
*
* TRUE if the packet FIFO is almost empty, FALSE otherwise.
*
* @note
*
* C Signature: u32 XPF_V200A_IS_ALMOST_EMPTY(XPacketFifoV200a *InstancePtr)
*
******************************************************************************/
#define XPF_V200A_IS_ALMOST_EMPTY(InstancePtr) \
    (XIo_In32((InstancePtr)->RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) & \
    XPF_V200A_ALMOST_EMPTY_FULL_MASK)


/*****************************************************************************/
/**
*
* Determine if the specified packet FIFO is almost full. Almost full is
* defined for a write FIFO when there is only one available data word in the
* FIFO.
*
* @param InstancePtr contains a pointer to the FIFO to operate on.
*
* @return
*
* TRUE if the packet FIFO is almost full, FALSE otherwise.
*
* @note
*
* C Signature: u32 XPF_V200A_IS_ALMOST_FULL(XPacketFifoV200a *InstancePtr)
*
******************************************************************************/
#define XPF_V200A_IS_ALMOST_FULL(InstancePtr) \
    (XIo_In32((InstancePtr)->RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) & \
    XPF_V200A_ALMOST_EMPTY_FULL_MASK)


/*****************************************************************************/
/**
*
* Determine if the specified packet FIFO is empty. This applies only to a
* read FIFO.
*
* @param InstancePtr contains a pointer to the FIFO to operate on.
*
* @return
*
* TRUE if the packet FIFO is empty, FALSE otherwise.
*
* @note
*
* C Signature: u32 XPF_V200A_IS_EMPTY(XPacketFifoV200a *InstancePtr)
*
******************************************************************************/
#define XPF_V200A_IS_EMPTY(InstancePtr) \
    (XIo_In32((InstancePtr)->RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) & \
    XPF_V200A_EMPTY_FULL_MASK)


/*****************************************************************************/
/**
*
* Determine if the specified packet FIFO is full. This applies only to a
* write FIFO.
*
* @param InstancePtr contains a pointer to the FIFO to operate on.
*
* @return
*
* TRUE if the packet FIFO is full, FALSE otherwise.
*
* @note
*
* C Signature: u32 XPF_V200A_IS_FULL(XPacketFifoV200a *InstancePtr)
*
******************************************************************************/
#define XPF_V200A_IS_FULL(InstancePtr) \
    (XIo_In32((InstancePtr)->RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) & \
    XPF_V200A_EMPTY_FULL_MASK)


/*****************************************************************************/
/**
*
* Determine if the specified packet FIFO is deadlocked.  This condition occurs
* when the FIFO is full and empty at the same time and is caused by a packet
* being written to the FIFO which exceeds the total data capacity of the FIFO.
* It occurs because of the mark/restore features of the packet FIFO which allow
* retransmission of a packet. The software should reset the FIFO and any devices
* using the FIFO when this condition occurs.
*
* @param InstancePtr contains a pointer to the FIFO to operate on.
*
* @return
*
* TRUE if the packet FIFO is deadlocked, FALSE otherwise.
*
* @note
*
* This component has the capability to generate an interrupt when an error
* condition occurs.  It is the user's responsibility to provide the interrupt
* processing to handle the interrupt. This function provides the ability to
* determine if a deadlock condition, and the ability to reset the FIFO to
* clear the condition.
* <br><br>
* In this condition, the device which is using the FIFO should also be reset
* to prevent other problems. This error condition could occur as a normal part
* of operation if the size of the FIFO is not setup correctly.
* <br><br>
* C Signature: u32 XPF_V200A_IS_DEADLOCKED(XPacketFifoV200a *InstancePtr)
*
******************************************************************************/
#define XPF_V200A_IS_DEADLOCKED(InstancePtr) \
    (XIo_In32((InstancePtr)->RegBaseAddress + XPF_V200A_COUNT_STATUS_REG_OFFSET) & \
    XPF_V200A_DEADLOCK_MASK)


/************************** Function Prototypes ******************************/

/**
 * Standard functions
 */
int XPacketFifoV200a_Initialize(XPacketFifoV200a * InstancePtr,
				u32 RegBaseAddress, u32 DataBaseAddress);
int XPacketFifoV200a_SelfTest(XPacketFifoV200a * InstancePtr, u32 FifoType);

/**
 * Data functions
 */
int XPacketFifoV200a_Read(XPacketFifoV200a * InstancePtr,
			  u8 *ReadBufferPtr, u32 ByteCount);
int XPacketFifoV200a_Write(XPacketFifoV200a * InstancePtr,
			   u8 *WriteBufferPtr, u32 ByteCount);
int XPacketFifoV200a_WriteDre(XPacketFifoV200a * InstancePtr,
			      u8 *WriteBufferPtr, u32 ByteCount);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

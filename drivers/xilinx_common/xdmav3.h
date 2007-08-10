/* $Id: */
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
*       (c) Copyright 2006 Xilinx Inc.
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
* @file xdmav3.h
*
* The Xilinx Simple and Scatter Gather DMA driver.  This component supports a
* distributed DMA design in which each device can have it's own dedicated DMA
* channel, as opposed to a centralized DMA design. A device which uses DMA
* typically contains two DMA channels, one for sending data and the other for
* receiving data.
*
* This component is designed to be used as a basic building block for
* designing a device driver. It provides registers accesses such that all
* DMA processing can be maintained easier, but the device driver designer
* must still understand all the details of the DMA channel.
*
* For a full description of DMA features, please see the HW spec. This driver
* supports the following features:
*   - Simple DMA
*   - Scatter-Gather DMA (SGDMA)
*   - Interrupts
*   - Programmable interrupt coalescing for SGDMA
*   - 36 Bit bus addressing
*   - Programmable transaction types
*   - APIs to manage Buffer Descriptors (BD) movement to and from the SGDMA
*     engine
*   - Virtual memory support
*
* <b>Transactions</b>
*
* To describe a DMA transaction in its simplest form, you need a source address,
* destination address, and the number of bytes to transfer. When using a DMA
* receive channel, the source address is within some piece of IP HW and doesn't
* require the user explicitly set it. Likewise with a transmit channel and the
* destination address. So this leaves a user buffer address and the number
* bytes to transfer as the primary transaction attributes. There are more
* obscure attributes such as:
*
*   - Is the user buffer a fixed address FIFO or a range of memory
*   - The size of the data bus over which the transaction occurs.
*   - Does the transfer use single beat or bursting capabilities of the
*     bus over which the transaction occurs.
*   - If the transaction occurs on a bus wider than 32 bits, what are the
*     highest order address bits.
*   - If SGDMA, does this transaction represent the end of a packet.
*
* The object used to describe a transaction is referred to as a Buffer
* Descriptor (BD). The format of a BD closely matches that of the DMA HW.
* Many fields within the BD correspond directly with the same fields within the
* HW registers. See xdmabdv3.h for a detailed description of and the API for
* manipulation of these objects.
*
* <b>Simple DMA</b>
*
* Simple DMA is a single transaction type of operation. The user uses this
* driver to setup a transaction, initiate the transaction, then either wait for
* an interrupt or poll the HW for completion of the transaction. A new
* transaction may not be initiated until the current one completes.
*
* <b>Scatter-Gather DMA</b>
*
* SGDMA is more sophisticated in that it allows the user to define a list of
* transactions in memory which the HW will process without further user
* intervention. During this time, the user is free to continue adding more work
* to keep the HW busy.
*
* Notification of completed transactions can be done either by polling the HW,
* or using interrupts that signal a transaction has completed or a series of
* transactions have been processed.
*
* SGDMA processes in units of packets. A packet is defined as a series of
* data bytes that represent a message. SGDMA allows a packet of data to be
* broken up into one or more transactions. For example, take an Ethernet IP
* packet which consists of a 14 byte header followed by a 1 or more byte
* payload. With SGDMA, the user may point a BD to the header and another BD to
* the payload, then transfer them as a single message. This strategy can make a
* TCP/IP stack more efficient by allowing it to keep packet headers and data in
* different memory regions instead of assembling packets into contiguous blocks
* of memory.
*
* <b>Interrupt Coalescing</b>
*
* SGDMA provides control over the frequency of interrupts. On a high speed link
* significant processor overhead may be used servicing interrupts. Interrupt
* coalescing provides two mechanisms that help control interrupt frequency.
*
* The packet threshold will hold off interrupting the CPU until a programmable
* number of packets have been processed by the engine. The packet waitbound
* timer is used to interrupt the CPU if after a programmable amount of time
* after processing the last packet, no new packets were processed.
*
* <b>Interrupts</b>
*
* This driver does not service interrupts. This is done typically within
* a higher level driver that uses DMA. This driver does provide an API to
* enable or disable specific interrupts.
*
* <b>SGDMA List Management</b>
*
* The HW expectes BDs to be setup as a singly linked list. As BDs are completed,
* the DMA engine will dereference BD.Next and load the next BD to process.
* This driver uses a fixed buffer ring where all BDs are linked to the next
* adjacent BD in memory. The last BD in the ring is linked to the first.
*
* Within the BD ring, the driver maintains four groups of BDs. Each group
* consists of 0 or more adjacent BDs:
*
*   - Free group: Those BDs that can be allocated by the user with
*     XDmaV3_SgBdAlloc(). These BDs are under driver control.
*
*   - Pre-work group: Those BDs that have been allocated with
*     XDmaV3_SgBdAlloc(). These BDs are under user control. The user modifies
*     these BDs in preparation for future DMA transactions.
*
*   - Work group: Those BDs that have been enqueued to HW with
*     XDmaV3_SgBdToHw(). These BDs are under HW control and may be in a
*     state of awaiting HW processing, in process, or processed by HW.
*
*   - Post-work group: Those BDs that have been processed by HW and have been
*     extracted from the work group with XDmaV3_SgBdFromHw(). These BDs are under
*     user control. The user may access these BDs to determine the result
*     of DMA transactions. When the user is finished, XDmaV3_SgBdFree() should
*     be called to place them back into the Free group.
*
* It is considered an error for the user to change BDs while they are in the
* Work group. Doing so can cause data corruption and lead to system instability.
*
* The API provides macros that allow BD list traversal. These macros should be
* used with care as they do not understand where one group ends and another
* begins.
*
* The driver does not cache or keep copies of any BD. When the user modifies
* BDs returned by XDmaV3_SgBdAlloc() or XDmaV3_SgBdFromHw(), they are modifying
* the same BD list that HW accesses.
*
* Certain pairs of list modification functions have usage restrictions. See
* the function headers for XDmaV3_SgBdAlloc() and XDmaV3_SgBdFromHw() for
* more information.
*
* <b>SGDMA List Creation</b>
*
* During initialization, the function XDmaV3_SgListCreate() is used to setup
* a user supplied memory block to contain all BDs for the DMA channel. This
* function takes as an argument the number of BDs to place in the list. To
* arrive at this number, the user is given two methods of calculating it.
*
* The first method assumes the user has a block of memory and they just
* want to fit as many BDs as possible into it. The user must calculate the
* number of BDs that will fit with XDmaV3_mSgListCntCalc(), then supply that
* number into the list creation function.
*
* The second method allows the user to just supply the number directly. The
* driver assumes the memory block is large enough to contain them all. To
* double-check, the user should invoke XDmaV3_mSgListMemCalc() to verify the
* memory block is adequate.
*
* Once the list has been created, it can be used right away to perform DMA
* transactions. However, there are optional steps that can be done to increase
* throughput and decrease user code complexity by the use of XDmaV3_SgListClone().
*
* BDs have many user accessible attributes that affect how DMA transactions are
* carried out. Many of these attributes (such as the bus width) will probably
* be constant at run-time. The cloning function can be used to copy a template
* BD to every BD in the list relieving the user of having to setup transactions
* from scratch every time a BD is submitted to HW.
*
* Ideally, the only transaction parameters that need to be set at run-time
* should be: buffer address, bytes to transfer, and whether the BD is the
* "Last" BD of a packet.
*
* <b>Adding / Removing BDs from the SGDMA Engine</b>
*
* BDs may be enqueued (see XDmaV3_SgBdToHw()) to the engine any time after
* the SGDMA list is created. If the channel is running (see XDmaV3_SgStart()),
* then newly added BDs will be processed as soon as the engine reaches them.
* If the channel is stopped (see XDmaV3_SgStop()), the newly added BDs will
* be accepted but not processed by the engine until it is restarted.
*
* Processed BDs may be removed (see XDmaV3_SgBdFromHw()) at any time
* after the SGDMA list is created provided the engine has processed any.
*
* <b>Address Translation</b>
*
* When the BD list is setup with XDmaV3_SgListCreate(), a physical and
* virtual address is supplied for the segment of memory containing the
* descriptors. The driver will handle any translations internally. Subsequent
* access of descriptors by the user is done in terms of the virtual address.
*
* <b>Alignment</b>
*
* Except for 4 byte alignment of BDs there are no other alignment restrictions
* imposed by this driver. Individual DMA channels may, based on their
* capabilities or which bus they are a master of, have more stringent alignment
* requirements. It is up to the user to match the requirements of the DMA
* channel being used.
*
* Aside from the initial creation of BD list (see XDmaV3_SgListCreate()),
* there are no other run-time checks for proper alignment. Misaligned user
* buffers or BDs may result in corrupted data.
*
* <b>Cache Coherency</b>
*
* This driver expects all user buffers attached to BDs to be in cache coherent
* memory. Buffers for transmit should be flushed from the cache before passing
* the associated BD to this driver. Buffers for receive should be invalidated
* before being accessed.
*
* If the user wishes that the BD space itself be in cached memory, then
* modification of this driver is required. The driver helps the user in
* this area by: 1) Allowing the user to specify what alignment BDs should
* use (ie. aligned along cache lines); 2) Provide unimplemented invalidate/flush
* macro placeholders in the driver source code where needed.
*
* <b>Reset After Stopping</b>
*
* This driver is designed to allow for stop-reset-start cycles of the DMA
* HW while keeping the BD list intact. When restarted after a reset, this
* driver will point the DMA engine to where it left off after stopping it.
*
* <b>Limitations</b>
*
* This driver requires exclusive use of the hardware DMACR.SGS bit. This
* applies to the actual HW register and BDs submitted through this driver to
* be processed. If a BD is encountered with this bit set, then it will be
* cleared within the driver.
*
* This driver does not have any mechanism for mutual exclusion. It is up to the
* user to provide this protection.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 3.00a rmm  03/11/06 First release
*       rmm  06/22/06 Added extern "C"
* </pre>
*
******************************************************************************/

#ifndef XDMAV3_H		/* prevent circular inclusions */
#define XDMAV3_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xdmabdv3.h"
#include "xstatus.h"

/************************** Constant Definitions *****************************/

/* Minimum alignment */
#define XDMABDV3_MINIMUM_ALIGNMENT  4


/**************************** Type Definitions *******************************/

/** This is an internal structure used to maintain the SGDMA list */
typedef struct {
	u32 PhysBaseAddr;
		       /**< Physical address of 1st BD in list */
	u32 BaseAddr;  /**< Virtual address of 1st BD in list */
	u32 HighAddr;  /**< Virtual address of last BD in the list */
	u32 Length;    /**< Total size of ring in bytes */
	u32 RunState;  /**< Flag to indicate SGDMA is started */
	u32 Separation;/**< Number of bytes between the starting address
                                of adjacent BDs */
	XDmaBdV3 *FreeHead;/**< First BD in the free group */
	XDmaBdV3 *PreHead; /**< First BD in the pre-work group */
	XDmaBdV3 *HwHead;  /**< First BD in the work group */
	XDmaBdV3 *HwTail;  /**< Last BD in the work group */
	XDmaBdV3 *PostHead;/**< First BD in the post-work group */
	XDmaBdV3 *BdaRestart;
			   /**< BDA to load when channel is started */
	unsigned HwCnt;	   /**< Number of BDs in work group */
	unsigned PreCnt;   /**< Number of BDs in pre-work group */
	unsigned FreeCnt;  /**< Number of allocatable BDs in the free group */
	unsigned PostCnt;  /**< Number of BDs in post-work group */
	unsigned AllCnt;   /**< Total Number of BDs for channel */
} XDmaV3_BdRing;

/**
 * The XDmaV3 driver instance data. An instance must be allocated for each DMA
 * channel in use. If address translation is enabled, then all addresses and
 * pointers excluding PhysBase are expressed in terms of the virtual address.
 */
typedef struct XDmaV3 {
	u32 RegBase;	   /**< Base address of channel registers */
	u32 IsReady;	   /**< Flag to indicate device is ready to use */
	XDmaV3_BdRing BdRing;  /**< BD storage for SGDMA */
} XDmaV3;


/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
/**
* Use this macro at initialization time to determine how many BDs will fit
* in a BD list within the given memory constraints.
*
* The results of this macro can be provided to XDmaV3_SgListCreate().
*
* @param Alignment specifies what byte alignment the BDs must fall on and
*        must be a power of 2 to get an accurate calculation (32, 64, 126,...)
* @param Bytes is the number of bytes to be used to store BDs.
*
* @return Number of BDs that can fit in the given memory area
*
* @note
* C-style signature:
*    u32 XDmaV3_mSgListCntCalc(u32 Alignment, u32 Bytes)
*
******************************************************************************/
#define XDmaV3_mSgListCntCalc(Alignment, Bytes)                           \
    (u32)((Bytes) / ((sizeof(XDmaBdV3) + ((Alignment)-1)) & ~((Alignment)-1)))

/*****************************************************************************/
/**
* Use this macro at initialization time to determine how many bytes of memory
* is required to contain a given number of BDs at a given alignment.
*
* @param Alignment specifies what byte alignment the BDs must fall on. This
*        parameter must be a power of 2 to get an accurate calculation (32, 64,
*        128,...)
* @param NumBd is the number of BDs to calculate memory size requirements for
*
* @return The number of bytes of memory required to create a BD list with the
*         given memory constraints.
*
* @note
* C-style signature:
*    u32 XDmaV3_mSgListMemCalc(u32 Alignment, u32 NumBd)
*
******************************************************************************/
#define XDmaV3_mSgListMemCalc(Alignment, NumBd)                           \
    (u32)((sizeof(XDmaBdV3) + ((Alignment)-1)) & ~((Alignment)-1)) * (NumBd)


/****************************************************************************/
/**
* Return the total number of BDs allocated by this channel with
* XDmaV3_SgListCreate().
*
* @param  InstancePtr is the DMA channel to operate on.
*
* @return The total number of BDs allocated for this channel.
*
* @note
* C-style signature:
*    u32 XDmaBdV3_mSgGetCnt(XDmaV3* InstancePtr)
*
*****************************************************************************/
#define XDmaV3_mSgGetCnt(InstancePtr)       ((InstancePtr)->BdRing.AllCnt)


/****************************************************************************/
/**
* Return the number of BDs allocatable with XDmaV3_SgBdAlloc() for pre-
* processing.
*
* @param  InstancePtr is the DMA channel to operate on.
*
* @return The number of BDs currently allocatable.
*
* @note
* C-style signature:
*    u32 XDmaBdV3_mSgGetFreeCnt(XDmaV3* InstancePtr)
*
*****************************************************************************/
#define XDmaV3_mSgGetFreeCnt(InstancePtr)   ((InstancePtr)->BdRing.FreeCnt)


/****************************************************************************/
/**
* Return the next BD in a list.
*
* @param  InstancePtr is the DMA channel to operate on.
* @param  BdPtr is the BD to operate on.
*
* @return The next BD in the list relative to the BdPtr parameter.
*
* @note
* C-style signature:
*    XDmaBdV3 *XDmaV3_mSgBdNext(XDmaV3* InstancePtr, XDmaBdV3 *BdPtr)
*
*****************************************************************************/
#define XDmaV3_mSgBdNext(InstancePtr, BdPtr)                            \
    (((u32)(BdPtr) >= (InstancePtr)->BdRing.HighAddr) ?             \
     (XDmaBdV3*)(InstancePtr)->BdRing.BaseAddr :                        \
     (XDmaBdV3*)((u32)(BdPtr) + (InstancePtr)->BdRing.Separation))


/****************************************************************************/
/**
* Return the previous BD in the list.
*
* @param  InstancePtr is the DMA channel to operate on.
* @param  BdPtr is the BD to operate on
*
* @return The previous BD in the list relative to the BdPtr parameter.
*
* @note
* C-style signature:
*    XDmaBdV3 *XDmaV3_mSgBdPrev(XDmaV3* InstancePtr, XDmaBdV3 *BdPtr)
*
*****************************************************************************/
#define XDmaV3_mSgBdPrev(InstancePtr, BdPtr)                            \
    (((u32)(BdPtr) <= (InstancePtr)->BdRing.BaseAddr) ?             \
     (XDmaBdV3*)(InstancePtr)->BdRing.HighAddr :                        \
     (XDmaBdV3*)((u32)(BdPtr) - (InstancePtr)->BdRing.Separation))


/****************************************************************************/
/**
* Retrieve the current contents of the DMASR register. This macro can be
* used to poll the DMA HW for completion of a transaction.
*
* @param  InstancePtr is the DMA channel to operate on.
*
* @return The current contents of the DMASR register.
*
* @note
* C-style signature:
*    u32 XDmaV3_mGetStatus(XDmaV3* InstancePtr)
*
*****************************************************************************/
#define XDmaV3_mGetStatus(InstancePtr)                                  \
    XDmaV3_mReadReg((InstancePtr)->RegBase, XDMAV3_DMASR_OFFSET)


/************************** Function Prototypes ******************************/

/*
 * Initialization and control functions in xdmav3.c
 */
int XDmaV3_Initialize(XDmaV3 * InstancePtr, u32 BaseAddress);

/*
 * Interrupt related functions in xdmav3_intr.c
 */
void XDmaV3_SetInterruptStatus(XDmaV3 * InstancePtr, u32 Mask);
u32 XDmaV3_GetInterruptStatus(XDmaV3 * InstancePtr);
void XDmaV3_SetInterruptEnable(XDmaV3 * InstancePtr, u32 Mask);
u32 XDmaV3_GetInterruptEnable(XDmaV3 * InstancePtr);

/*
 * Simple DMA related functions in xdmav3_simple.c
 */
int XDmaV3_SimpleTransfer(XDmaV3 * InstancePtr, XDmaBdV3 * Bdptr);

/*
 * Scatter gather DMA related functions in xdmav3_sg.c
 */
int XDmaV3_SgStart(XDmaV3 * InstancePtr);
void XDmaV3_SgStop(XDmaV3 * InstancePtr);
int XDmaV3_SgSetPktThreshold(XDmaV3 * InstancePtr, u16 Threshold);
int XDmaV3_SgSetPktWaitbound(XDmaV3 * InstancePtr, u16 TimerVal);
u16 XDmaV3_SgGetPktThreshold(XDmaV3 * InstancePtr);
u16 XDmaV3_SgGetPktWaitbound(XDmaV3 * InstancePtr);

int XDmaV3_SgListCreate(XDmaV3 * InstancePtr, u32 PhysAddr,
			u32 VirtAddr, u32 Alignment, unsigned BdCount);
int XDmaV3_SgListClone(XDmaV3 * InstancePtr, XDmaBdV3 * SrcBdPtr);
int XDmaV3_SgCheck(XDmaV3 * InstancePtr);
int XDmaV3_SgBdAlloc(XDmaV3 * InstancePtr, unsigned NumBd,
		     XDmaBdV3 ** BdSetPtr);
int XDmaV3_SgBdUnAlloc(XDmaV3 * InstancePtr, unsigned NumBd,
		       XDmaBdV3 * BdSetPtr);
int XDmaV3_SgBdToHw(XDmaV3 * InstancePtr, unsigned NumBd, XDmaBdV3 * BdSetPtr);
int XDmaV3_SgBdFree(XDmaV3 * InstancePtr, unsigned NumBd, XDmaBdV3 * BdSetPtr);
unsigned XDmaV3_SgBdFromHw(XDmaV3 * InstancePtr, unsigned BdLimit,
			   XDmaBdV3 ** BdSetPtr);

/*
 * Selftest functions in xdmav3_selftest.c
 */
int XDmaV3_SelfTest(XDmaV3 * InstancePtr);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

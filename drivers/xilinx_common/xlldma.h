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
*       (c) Copyright 2007 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xlldma.h
*
* The Xilinx Local-Link Scatter Gather DMA driver. This driver supports Soft
* DMA (SDMA) engines. Each SDMA engine contains two separate DMA channels (TX
* and RX).
*
* This component is designed to be used as a basic building block for
* designing a device driver. It provides registers accesses such that all
* DMA processing can be maintained easier, but the device driver designer
* must still understand all the details of the DMA channel.
*
* For a full description of DMA features, please see the hardware spec. This
* driver supports the following features:
*
*   - Scatter-Gather DMA (SGDMA)
*   - Interrupts
*   - Programmable interrupt coalescing for SGDMA
*   - Capable of using 32 bit addressing for buffer. (Hardware spec states
*     36 Bit bus addressing, which includes Msb 4-bits of DMA address
*     configurable on each channel through the Channel Control Registers)
*   - APIs to manage Buffer Descriptors (BD) movement to and from the SGDMA
*     engine
*   - Virtual memory support
*
* <b>Transactions</b>
*
* To describe a DMA transaction in its simplest form, you need source address,
* destination address, and the number of bytes to transfer. When using a DMA
* receive channel, the source address is within some piece of IP Hardware and
* doesn't require the application explicitly set it. Likewise with a transmit
* channel and the destination address. So this leaves a application buffer
* address and the number bytes to transfer as the primary transaction
* attributes. Other attributes include:
*
*   - If the transaction occurs on a bus wider than 32 bits, what are the
*     highest order address bits.
*   - Does this transaction represent the start of a packet, or end of a
*     packet.
*
* The object used to describe a transaction is referred to as a Buffer
* Descriptor (BD). The format of a BD closely matches that of the DMA hardware.
* Many fields within the BD correspond directly with the same fields within the
* hardware registers. See xlldmabd.h for a detailed description of and the API
* for manipulation of these objects.
*
* <b>Scatter-Gather DMA</b>
*
* SGDMA allows the application to define a list of transactions in memory which
* the hardware will process without further application intervention. During
* this time, the application is free to continue adding more work to keep the
* Hardware busy.
*
* Notification of completed transactions can be done either by polling the
* hardware, or using interrupts that signal a transaction has completed or a
* series of transactions have been completed.
*
* SGDMA processes whole packets. A packet is defined as a series of
* data bytes that represent a message. SGDMA allows a packet of data to be
* broken up into one or more transactions. For example, take an Ethernet IP
* packet which consists of a 14 byte header followed by a 1 or more byte
* payload. With SGDMA, the application may point a BD to the header and another
* BD to the payload, then transfer them as a single message. This strategy can
* make a TCP/IP stack more efficient by allowing it to keep packet headers and
* data in different memory regions instead of assembling packets into
* contiguous blocks of memory.
*
* <b>SGDMA Ring Management</b>
*
* The hardware expects BDs to be setup as a singly linked list. As a BD is
* completed, the DMA engine will dereference BD.Next and load the next BD to
* process. This driver uses a fixed buffer ring where all BDs are linked to the
* next BD in adjacent memory. The last BD in the ring is linked to the first.
*
* Within the ring, the driver maintains four groups of BDs. Each group consists
* of 0 or more adjacent BDs:
*
*   - Free: Those BDs that can be allocated by the application with
*     XLlDma_BdRingAlloc(). These BDs are under driver control and may not be
*     modified by the application
*
*   - Pre-process: Those BDs that have been allocated with
*     XLlDma_BdRingAlloc(). These BDs are under application control. The
*     application modifies these BDs in preparation for future DMA
*     transactions.
*
*   - Hardware: Those BDs that have been enqueued to hardware with
*     XLlDma_BdRingToHw(). These BDs are under hardware control and may be in a
*     state of awaiting hardware processing, in process, or processed by
*     hardware. It is considered an error for the application to change BDs
*     while they are in this group. Doing so can cause data corruption and lead
*     to system instability.
*
*   - Post-process: Those BDs that have been processed by hardware and have
*     been extracted from the work group with XLlDma_BdRingFromHw(). These BDs
*     are under application control. The application may access these BDs to
*     determine the result of DMA transactions. When the application is
*     finished, XLlDma_BdRingFree() should be called to place them back into
*     the Free group.
*
*
* Normally BDs are moved in the following way:
* <pre>
*
*         XLlDma_BdRingAlloc()                   XLlDma_BdRingToHw()
*   Free ------------------------> Pre-process ----------------------> Hardware
*                                                                      |
*    /|\                                                               |
*     |   XLlDma_BdRingFree()                    XLlDma_BdRingFromHw() |
*     +--------------------------- Post-process <----------------------+
*
* </pre>
*
* The only exception to the flow above is that after BDs are moved from Free
* group to Pre-process group, the application decide for whatever reason these
* BDs are not ready and could not be given to hardware. In this case these BDs
* could be moved back to Free group using XLlDma_BdRingUnAlloc() function to
* help keep the BD ring in great shape and recover the error. See comments of
* the function for details
*
* <pre>
*
*         XLlDma_BdRingUnAlloc()
*   Free <----------------------- Pre-process
*
* </pre>
*
* The API provides macros that allow BD list traversal. These macros should be
* used with care as they do not understand where one group ends and another
* begins.
*
* The driver does not cache or keep copies of any BD. When the application
* modifies BDs returned by XLlDma_BdRingAlloc() or XLlDma_BdRingFromHw(), they
* are modifying the same BD that hardware accesses.
*
* Certain pairs of list modification functions have usage restrictions. See
* the function headers for XLlDma_BdRingAlloc() and XLlDma_BdRingFromHw() for
* more information.
*
* <b>SGDMA Descriptor Ring Creation</b>
*
* During initialization, the function XLlDma_BdRingCreate() is used to setup
* a application supplied memory block to contain all BDs for the DMA channel.
* This function takes as an argument the number of BDs to place in the list. To
* arrive at this number, the application is given two methods of calculating
* it.
*
* The first method assumes the application has a block of memory and they just
* want to fit as many BDs as possible into it. The application must calculate
* the number of BDs that will fit with XLlDma_mBdRingCntCalc(), then supply
* that number into the list creation function.
*
* The second method allows the application to just supply the number directly.
* The driver assumes the memory block is large enough to contain them all. To
* double-check, the application should invoke XLlDma_mBdRingMemCalc() to verify
* the memory block size is adequate.
*
* Once the list has been created, it can be used right away to perform DMA
* transactions. However, there are optional steps that can be done to increase
* throughput and decrease application code complexity by the use of
* XLlDma_BdRingClone().
*
* BDs have several application accessible attributes that affect how DMA
* transactions are carried out. Some of these attributes will probably be
* constant at run-time. The cloning function can be used to copy a template BD
* to every BD in the ring relieving the application of having to setup
* transactions from scratch every time a BD is submitted to hardware.
*
* Ideally, the only transaction parameters that need to be set by application
* should be: buffer address, bytes to transfer, and whether the BD is the
* Start and/or End of a packet.
*
* <b>Interrupt Coalescing</b>
*
* SGDMA provides control over the frequency of interrupts. On a high speed link
* significant processor overhead may be used servicing interrupts. Interrupt
* coalescing provides two mechanisms that help control interrupt frequency:
*
* - The packet threshold counter will hold off interrupting the CPU until a
*   programmable number of packets have been processed by the engine.
* - The packet waitbound timer is used to interrupt the CPU if after a
*   programmable amount of time after processing the last packet, no new
*   packets were processed.
*
* <b>Interrupt Service </b>
*
* This driver does not service interrupts. This is done typically by a
* interrupt handler within a higher level driver/application that uses DMA.
* This driver does provide an API to enable or disable specific interrupts.
*
* This interrupt handler provided by the higher level driver/application
* !!!MUST!!! clear pending interrupts before handling the BDs processed by the
* DMA. Otherwise the following corner case could raise some issue:
*
* - A packet is transmitted(/received) and asserts a TX(/RX) interrupt, and if
*   this interrupt handler deals with the BDs finished by the DMA before clears
*   the interrupt, another packet could get transmitted(/received) and assert
*   the interrupt between when the BDs are taken care  and when the interrupt
*   clearing operation begins, and the interrupt clearing operation will clear
*   the interrupt raised by the second packet and will never process its
*   according BDs until a new interrupt occurs.
*
* Changing the sequence to "Clear interrupts before handle BDs" solves this
* issue:
*
* - If the interrupt raised by the second packet is before the interrupt
*   clearing operation, the descriptors associated with the second packet must
*   have been finished by hardware and ready for the handler to deal with,
*   and those descriptors will processed with those BDs of the first packet
*   during the handling of the interrupt asserted by the first packet.
*
* - if the interrupt of the second packet is asserted after the interrupt
*   clearing operation but its BDs are finished before the handler starts to
*   deal with BDs, the packet's buffer descriptors will be handled with
*   those of the first packet during the handling of the interrupt asserted
*   by the first packet.
*
* - Otherwise, the BDs of the second packet is not ready when the interrupt
*   handler starts to deal with the BDs of the first packet. Those BDs will
*   be handled next time the interrupt handled gets invoked as the interrupt
*   of the second packet is not cleared in current pass and thereby will
*   cause the handler to get invoked again
*
* Please note if the second case above occurs, the handler will find
* NO buffer descriptor is finished by the hardware (i.e.,
* XLlDma_BdRingFromHw() returns 0) during the handling of the interrupt
* asserted by the second packet. This is valid and the application should NOT
* consider this is a hardware error and have no need to reset the hardware.
*
* <b> Software Initialization </b>
*
* The application needs to do following steps in order for preparing DMA engine
* to be ready to process DMA transactions:
*
* - DMA Initialization using XLlDma_Initialize() function. This step
*   initializes a driver instance for the given DMA engine and resets the
*   engine.
* - BD Ring creation. A BD ring is needed per channel and can be built by
*   calling XLlDma_BdRingCreate(). A parameter passed to this function is the
*   number of BD fit in a given memory range, and XLlDma_mBdRingCntCalc() helps
*   calculate the value.
* - (Optional) BD setup using a template. Once a BD ring is created, the
*   application could populate a template BD and then invoke
*   XLlDma_BdRingClone() to set the same attributes on all BDs on the BD ring.
*   This saves the application some effort to populate all fixed attributes of
*   each BD before passing it to the hardware.
* - (RX channel only) Prepare BDs with attached data buffers and give them to
*   RX channel. First allocate BDs using XLlDma_BdRingAlloc(), then populate
*   data buffer address, data buffer size and the control word fields of each
*   allocated BD with valid values. Last call XLlDma_BdRingToHw() to give the
*   BDs to the channel.
* - Enable interrupts if interrupt mode is chosen. The application is
*   responsible for setting up the interrupt system, which includes providing
*   and connecting interrupt handlers and call back functions, before
*   the interrupts are enabled.
* - Start DMA channels: Call XLlDma_BdRingStart() to start a channel
*
* <b> How to start DMA transactions </b>
*
* RX channel is ready to start RX transactions once the initialization (see
* Initialization section above) is finished. The DMA transactions are triggered
* by the user IP (like Local Link TEMAC).
*
* Starting TX transactions needs some work. The application calls
* XLlDma_BdRingAlloc() to allocate a BD list, then populates necessary
* attributes of each allocated BD including data buffer address, data size,
* and control word, and last passes those BDs to the TX channel
* (see XLlDma_BdRingToHw()). The added BDs will be processed as soon as the
* TX channel reaches them.
*
* For both channels, If the DMA engine is currently paused (see
* XLlDma_Pause()), the newly added BDs will be accepted but not processed
* until the DMA engine is resumed (see XLlDma_Resume()).
*
* <b> Software Post-Processing on completed DMA transactions </b>
*
* Some software post-processing is needed after DMA transactions are finished.
*
* if interrupt system are set up and enabled, DMA channels notify the software
* the finishing of DMA transactions using interrupts,  Otherwise the
* application could poll the channels (see XLlDma_BdRingFromHw()).
*
* - Once BDs are finished by a channel, the application first needs to fetch
*   them from the channel (see XLlDma_BdRingFromHw()).
* - On TX side, the application now could free the data buffers attached to
*   those BDs as the data in the buffers has been transmitted.
* - On RX side, the application now could use the received data in the buffers
*   attached to those BDs
* - For both channels, those BDs need to be freed back to the Free group (see
*   XLlDma_BdRingFree()) so they are allocatable for future transactions.
* - On RX side, it is the application's responsibility for having BDs ready
*   to receive data at any time. Otherwise the RX channel will refuse to
*   accept any data once it runs out of RX BDs. As we just freed those hardware
*   completed BDs in the previous step, it is good timing to allocate them
*   back (see XLlDma_BdRingAlloc()), prepare them, and feed them to the RX
*   channel again (see XLlDma_BdRingToHw())
*
* <b> Examples </b>
*
* Two examples are provided with this driver to demonstrate the driver usage:
* One for interrupt mode and one for polling mode.
*
* <b>Address Translation</b>
*
* When the BD list is setup with XLlDma_BdRingCreate(), a physical and
* virtual address is supplied for the segment of memory containing the
* descriptors. The driver will handle any translations internally. Subsequent
* access of descriptors by the application is done in terms of their virtual
* address.
*
* Any application data buffer address attached to a BD must be physical
* address. The application is responsible for calculating the physical address
* before assigns it to the buffer address field in the BD.
*
* <b>Cache Coherency</b>
*
* This driver expects all application buffers attached to BDs to be in cache
* coherent memory. Buffers for transmit MUST be flushed from the cache before
* passing the associated BD to this driver. Buffers for receive MUST be
* invalidated before passing the associated BD to this driver.
*
* If the application wishes that the BD space itself be in cached memory, then
* XENV macros XCACHE_FLUSH_DCACHE_RANGE() and XCACHE_INVALIDATE_DCACHE_RANGE()
* must be implemented via xenv.h. Otherwise this driver and hardware will NOT
* work properly.
*
* <b>Alignment</b>
*
* For BDs:
*
* Minimum alignment is defined by the constant XLLDMA_BD_MINIMUM_ALIGNMENT.
* This is the smallest alignment allowed by both hardware and software for them
* to properly work. Other than XLLDMA_BD_MINIMUM_ALIGNMENT, multiples of the
* constant are the only valid alignments for BDs.
*
* If the descriptor ring is to be placed in cached memory, alignment also MUST
* be at least the processor's cache-line size. If this requirement is not met
* then system instability will result. This is also true if the length of a BD
* is longer than one cache-line, in which case multiple cache-lines are needed
* to accommodate each BD.
*
* Aside from the initial creation of the descriptor ring (see
* XLlDma_BdRingCreate()), there are no other run-time checks for proper
* alignment.
*
* For application data buffers:
*
* Application data buffers may reside on any alignment.
*
* <b>Reset After Stopping</b>
*
* This driver is designed to allow for stop-reset-start cycles of the DMA
* hardware while keeping the BD list intact. When restarted after a reset, this
* driver will point the DMA engine to where it left off after stopping it.
*
* <b>Limitations</b>
*
* This driver only supports Normal mode (i.e., Tail Descriptor Pointer mode).
* In this mode write of a Tail Descriptor Pointer register (which is done in
* XLlDma_BdRingStart() and XLlDma_BdRingToHw()) starts DMA transactions.
*
* Legacy mode is NOT supported by this driver.
*
* This driver does not have any mechanism for mutual exclusion. It is up to the
* application to provide this protection.
*
* <b>Hardware Defaults & Exclusive Use</b>
*
* During initialization, this driver will override the following hardware
* default settings. If desired, the application may change these settings back
* to their hardware defaults:
*
*   - Normal mode (Tail Descriptor Pointer mode) will be enabled.
*   - Interrupt coalescing timer and counter overflow errors will be disabled
*     (XLLDMA_DMACR_RX_OVERFLOW_ERR_DIS_MASK and TX_OVERFLOW_ERR_DIS_MASK will
*     be set to 1). These two items control interrupt "overflow" behavior.
*     When enabled, the hardware may signal an error if interrupts are not
*     processed fast enough even though packets were correctly processed. This
*     error is triggered when certain internal counters overflow. The driver
*     disables this feature so no such error will be reported.
*
* The driver requires exclusive use of the following hardware features. If any
* are changed by the application then the driver will not operate properly:
*
*   - XLLDMA_DMACR_TAIL_PTR_ENABLE_MASK. The driver controls this bit
*     in the DMACR register.
*   - XLLDMA_BD_STSCTRL_COMPLETED_MASK. The driver controls this bit in each BD
*   - XLLDMA_NDESC_OFFSET. The driver controls this register
*   - XLLDMA_DMACR_SW_RESET_MASK. The driver controls this bit in the DMACR
*     register
*
* <b>BUS Interface</b>
*
* The constant XPAR_XLLDMA_USE_DCR (see xlldma_hw.h) is used to inform the
* driver the type of the BUS the DMA device is on. If the DMA device is on DCR
* BUS, XPAR_XLLDMA_USE_DCR must be defined in xparameters.h or as a compiler
* option used in the Makefile BEFORE this driver is compiled; Otherwise,
* the constant must NOT be defined.
*
* <b>User-IP Specific Definition</b>
*
* This driver relies on two User-IP (like Local-Link TEMAC) specific constants
* (see xlldma_userip.h) to work properly:
*
*   - XLLDMA_USR_APPWORD_OFFSET defines a user word the User-IP always updates
*     in the RX Buffer Descriptors (BD) during <b>ALL</b> Receive transactions.
*     This driver uses XLLDMA_BD_USR4_OFFSET as the default value of this
*     constant.
*
*   - XLLDMA_USR_APPWORD_INITVALUE defines the value the DMA driver uses to
*     populate the XLLDMA_USR_APPWORD_OFFSET field in any RX BD before giving
*     the BD to the RX channel for receive transaction. It must be ensured
*     that the User-IP will always populates a different value into the
*     XLLDMA_USR_APPWORD_OFFSET field during any receive transaction. Failing
*     to do so will cause the DMA driver to work improperly. This driver uses
*     0xFFFFFFFF as the default value of this constant.
*
* If the User-IP uses different setting, the correct setting must be defined
* in the xparameters.h or as compiler options used in the Makefile BEFORE this
* driver is compiled. In either case the default definition of the constants
* in this driver will be discarded.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a xd   12/21/06 First release
* </pre>
*
******************************************************************************/

#ifndef XLLDMA_H		/* prevent circular inclusions */
#define XLLDMA_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xlldma_bd.h"
#include "xlldma_bdring.h"
#include "xlldma_userip.h"
#include "xstatus.h"

/************************** Constant Definitions *****************************/

#define XLLDMA_NO_CHANGE            0xFFFF	/* Used as API argument */
#define XLLDMA_ALL_BDS              0xFFFFFFFF	/* Used as API argument */

/**************************** Type Definitions *******************************/


/**
 * The XLlDma driver instance data. An instance must be allocated for each DMA
 * engine in use. Each DMA engine includes a TX channel and a RX channel.
 */
typedef struct XLlDma {
	u32 RegBase;		/**< Virtual base address of DMA engine */
	XLlDma_BdRing TxBdRing;	/**< BD container management for TX channel */
	XLlDma_BdRing RxBdRing;	/**< BD container management for RX channel */

} XLlDma;


/***************** Macros (Inline Functions) Definitions *********************/


/****************************************************************************/
/**
* Retrieve the TX ring object. This object can be used in the various Ring
* API functions.
*
* @param  InstancePtr is the DMA engine to operate on.
*
* @return TxBdRing object
*
* @note
* C-style signature:
*    XLlDma_BdRing XLlDma_mGetTxRing(XLlDma* InstancePtr)
*
*****************************************************************************/
#define XLlDma_mGetTxRing(InstancePtr) ((InstancePtr)->TxBdRing)


/****************************************************************************/
/**
* Retrieve the RX ring object. This object can be used in the various Ring
* API functions.
*
* @param  InstancePtr is the DMA engine to operate on.
*
* @return RxBdRing object
*
* @note
* C-style signature:
*    XLlDma_BdRing XLlDma_mGetRxRing(XLlDma* InstancePtr)
*
*****************************************************************************/
#define XLlDma_mGetRxRing(InstancePtr) ((InstancePtr)->RxBdRing)


/****************************************************************************/
/**
* Retrieve the contents of the DMA engine control register
* (XLLDMA_DMACR_OFFSET).
*
* @param  InstancePtr is the DMA engine instance to operate on.
*
* @return Current contents of the DMA engine control register.
*
* @note
* C-style signature:
*    u32 XLlDma_mGetCr(XLlDma* InstancePtr)
*
*****************************************************************************/
#define XLlDma_mGetCr(InstancePtr)                                      \
	XLlDma_mReadReg((InstancePtr)->RegBase, XLLDMA_DMACR_OFFSET)


/****************************************************************************/
/**
* Set the contents of the DMA engine control register (XLLDMA_DMACR_OFFSET).
* This control register affects both DMA channels.
*
* @param  InstancePtr is the DMA engine instance to operate on.
* @param  Data is the data to write to the DMA engine control register.
*
* @note
* C-style signature:
*    u32 XLlDma_mSetCr(XLlDma* InstancePtr, u32 Data)
*
*****************************************************************************/
#define XLlDma_mSetCr(InstancePtr, Data)                                \
	XLlDma_mWriteReg((InstancePtr)->RegBase, XLLDMA_DMACR_OFFSET, (Data))


/************************** Function Prototypes ******************************/

/*
 * Initialization and control functions in xlldma.c
 */
void XLlDma_Initialize(XLlDma * InstancePtr, u32 BaseAddress);
void XLlDma_Reset(XLlDma * InstancePtr);
void XLlDma_Pause(XLlDma * InstancePtr);
void XLlDma_Resume(XLlDma * InstancePtr);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

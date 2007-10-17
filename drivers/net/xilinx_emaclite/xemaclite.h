/* $Id: xemaclite.h,v 1.1.2.1 2007/03/13 17:26:07 akondratenko Exp $ */
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
*       (c) Copyright 2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xemaclite.h
*
* The Xilinx Ethernet Lite driver component. This component supports the Xilinx
* Lite Ethernet 10/100 MAC (EMAC Lite).
*
* The Xilinx Ethernet Lite 10/100 MAC supports the following features:
*   - Media Independent Interface (MII) for connection to external
*     10/100 Mbps PHY transceivers.
*   - Independent internal transmit and receive buffers
*   - CSMA/CD compliant operations for half-duplex modes
*   - Unicast and broadcast
*   - Automatic FCS insertion
*   - Automatic pad insertion on transmit
*   - Configurable ping/pong buffer scheme for either/both transmit and receive
*     buffer areas.
*   - Interrupt driven mode available.
*
* The Xilinx Ethernet Lite 10/100 MAC does not support the following features:
*   - multi-frame buffering
*     only 1 transmit frame is allowed into each transmit buffer
*     only 1 receive frame is allowed into each receive buffer.
*     the hardware blocks reception until buffer is emptied
*   - Pause frame (flow control) detection in full-duplex mode
*   - Programmable interframe gap
*   - Multicast and promiscuous address filtering
*   - Internal loopback
*   - Automatic source address insertion or overwrite
*
* <b>Driver Description</b>
*
* The device driver enables higher layer software (e.g., an application) to
* communicate to the EMAC Lite. The driver handles transmission and reception
* of Ethernet frames, as well as configuration of the controller. It does not
* handle protocol stack functionality such as Link Layer Control (LLC) or the
* Address Resolution Protocol (ARP). The protocol stack that makes use of the
* driver handles this functionality. This implies that the driver is simply a
* pass-through mechanism between a protocol stack and the EMAC Lite.
*
* Since the driver is a simple pass-through mechanism between a protocol stack
* and the EMAC Lite, no assembly or disassembly of Ethernet frames is done at
* the driver-level. This assumes that the protocol stack passes a correctly
* formatted Ethernet frame to the driver for transmission, and that the driver
* does not validate the contents of an incoming frame. A single device driver
* can support multiple EmacLite devices.
*
* The driver supports interrupt driven mode and the default mode of operation
* is polled mode. If interrupts are desired, XEmacLite_InterruptEnable() must
* be called.
*
* <b>Device Configuration</b>
*
* The device can be configured in various ways during the FPGA implementation
* process.  Configuration parameters are stored in the xemaclite_g.c file. A table
* is defined where each entry contains configuration information for an EmacLite
* device.  This information includes such things as the base address
* of the memory-mapped device and the number of buffers.
*
* <b>Interrupt Processing</b>
*
* After _Initialize is called, _InterruptEnable can be called to enable the interrupt
* driven functionality. If polled operation is desired, just call _Send and check the
* return code. If XST_FAILURE is returned, call _Send with the same data until
* XST_SUCCESS is returned. The same idea applies to _Recv. Call _Recv until the
* returned length is non-zero at which point the received data is in the buffer
* provided in the function call.
*
* The Transmit and Receive interrupts are enabled within the _InterruptEnable
* function and disabled in the _InterruptDisable function. The _Send and _Recv
* functions acknowledge the EMACLite generated interrupts associated with each
* function.
* It is the application's responsibility to acknowledge any associated Interrupt
* Controller interrupts if it is used in the system.
*
* <b>Memory Buffer Alignment</b>
*
* The alignment of the input/output buffers for the _Send and _Recv routine is
* not required to be 32 bits. If the buffer is not aligned on a 32-bit boundry
* there will be a performance impact while the driver aligns the data for
* transmission or upon reception.
*
* For optimum performance, the user should provide a 32-bit aligned buffer
* to the _Send and _Recv routines.
*
* <b>Asserts</b>
*
* Asserts are used within all Xilinx drivers to enforce constraints on argument
* values. Asserts can be turned off on a system-wide basis by defining, at compile
* time, the NDEBUG identifier.  By default, asserts are turned on and it is
* recommended that application developers leave asserts on during development.
*
* @note
*
* This driver requires EmacLite hardware version 1.01a and higher. It is not
* compatible with earlier versions of the EmacLite hardware. Use version 1.00a
* software driver for hardware version 1.00a/b.
*
* The RX hardware is enabled from powerup and there is no disable. It is
* possible that frames have been received prior to the initialization
* of the driver. If this situation is possible, call XEmacLite_mFlushReceive()
* to empty the receive buffers after initialization.
*
* This driver is intended to be RTOS and processor independent.  It works
* with physical addresses only.  Any needs for dynamic memory management,
* threads or thread mutual exclusion, virtual memory, or cache control must
* be satisfied by the layer above this driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.01a ecm  01/30/04 First release
* </pre>
*
*
******************************************************************************/

#ifndef XEMACLITE_H		/* prevent circular inclusions */
#define XEMACLITE_H		/* by using protection macros */

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xstatus.h"
#include "xemaclite_l.h"

/************************** Constant Definitions *****************************/
/*
 * Device information
 */
#define XEL_DEVICE_NAME     "xemaclite"
#define XEL_DEVICE_DESC     "Xilinx Ethernet Lite 10/100 MAC"

/**************************** Type Definitions *******************************/

/**
 * This typedef contains configuration information for a device.
 */
typedef struct {
	u16 DeviceId;	    /**< Unique ID  of device */
	u32 BaseAddress;    /**< Device base address */
	u32 PhysAddress;	/* < Physical address (for Linux)> */
	u8 TxPingPong;	    /**< 1 if TX Pong buffer configured,0 otherwise */
	u8 RxPingPong;	    /**< 1 if RX Pong buffer configured,0 otherwise */
} XEmacLite_Config;


/*
 * Callback when data is sent or received .
 * @param CallBackRef is a callback reference passed in by the upper layer
 *        when setting the callback functions, and passed back to the upper
 *        layer when the callback is invoked.
 */
typedef void (*XEmacLite_Handler) (void *CallBackRef);

/**
 * The XEmacLite driver instance data. The user is required to allocate a
 * variable of this type for every EmacLite device in the system. A pointer
 * to a variable of this type is then passed to the driver API functions.
 */

typedef struct {
	u32 PhysAddress;	/* Base address for device (IPIF) */
	u32 BaseAddress;	/* Base address for device (IPIF) */
	u32 IsReady;		/* Device is initialized and ready */
	u32 NextTxBufferToUse;	/* Next TX buffer to write to */
	u32 NextRxBufferToUse;	/* Next RX buffer to read from */
	XEmacLite_Config *ConfigPtr;	/* A pointer to the device configuration */

	/*
	 * Callbacks
	 */

	XEmacLite_Handler RecvHandler;
	void *RecvRef;
	XEmacLite_Handler SendHandler;
	void *SendRef;

} XEmacLite;

/***************** Macros (Inline Functions) Definitions *********************/
/****************************************************************************/
/**
*
* Return the next expected Transmit Buffer's address .
*
* @param    InstancePtr is the pointer to the instance of the driver to
*           be worked on
*
* @note
* This macro returns the address of the next transmit buffer to put data into.
* This is used to determine the destination of the next transmit data frame.
*
*****************************************************************************/
#define XEmacLite_mNextTransmitAddr(InstancePtr)                             \
        ((InstancePtr)->BaseAddress + (InstancePtr)->NextTxBufferToUse) +   \
        XEL_TXBUFF_OFFSET

/****************************************************************************/
/**
*
* Return the next expected Receive Buffer's address .
*
* @param    InstancePtr is the pointer to the instance of the driver to
*           be worked on
*
* @note
* This macro returns the address of the next receive buffer to read data from.
* This is the expected receive buffer address if the driver is in sync.
*
*****************************************************************************/
#define XEmacLite_mNextReceiveAddr(InstancePtr)                              \
        ((InstancePtr)->BaseAddress + (InstancePtr)->NextRxBufferToUse)


/************************** Variable Definitions *****************************/

/************************** Function Prototypes ******************************/

/*
 * Initialization functions in xemaclite.c
 */
int XEmacLite_CfgInitialize(XEmacLite * InstancePtr, XEmacLite_Config * CfgPtr,
			u32 VirtualAddress);
void XEmacLite_SetMacAddress(XEmacLite * InstancePtr, u8 *AddressPtr);
u32 XEmacLite_TxBufferAvailable(XEmacLite * InstancePtr);
void XEmacLite_FlushReceive(XEmacLite * InstancePtr);

int XEmacLite_Send(XEmacLite * InstancePtr, u8 *FramePtr, unsigned ByteCount);
u16 XEmacLite_Recv(XEmacLite * InstancePtr, u8 *FramePtr);

/*
 * Interrupt driven functions in xemaclite_intr.c
 */

int XEmacLite_EnableInterrupts(XEmacLite * InstancePtr);
void XEmacLite_DisableInterrupts(XEmacLite * InstancePtr);

void XEmacLite_InterruptHandler(void *InstancePtr);

void XEmacLite_SetRecvHandler(XEmacLite * InstancePtr, void *CallBackRef,
			      XEmacLite_Handler FuncPtr);
void XEmacLite_SetSendHandler(XEmacLite * InstancePtr, void *CallBackRef,
			      XEmacLite_Handler FuncPtr);

/*
 * Selftest function in xemaclite_selftest.c
 */
int XEmacLite_SelfTest(XEmacLite * InstancePtr);

#endif /* end of protection macro */

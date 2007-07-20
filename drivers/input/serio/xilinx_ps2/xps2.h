/******************************************************************************
*
*     Author: Xilinx, Inc.
*
*
*     This program is free software; you can redistribute it and/or modify it
*     under the terms of the GNU General Public License as published by the
*     Free Software Foundation; either version 2 of the License, or (at your
*     option) any later version.
*
*
*     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A
*     COURTESY TO YOU. BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS
*     ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE, APPLICATION OR STANDARD,
*     XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION IS FREE
*     FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE FOR OBTAINING
*     ANY THIRD PARTY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.
*     XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO
*     THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY
*     WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM
*     CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND
*     FITNESS FOR A PARTICULAR PURPOSE.
*
*
*     Xilinx hardware products are not intended for use in life support
*     appliances, devices, or systems. Use in such applications is
*     expressly prohibited.
*
*
*     (c) Copyright 2002-2005 Xilinx Inc.
*     All rights reserved.
*
*
*     You should have received a copy of the GNU General Public License along
*     with this program; if not, write to the Free Software Foundation, Inc.,
*     675 Mass Ave, Cambridge, MA 02139, USA.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xps2.h
*
* This driver supports the following features:
*
* - Polled mode
* - Interrupt driven mode
*
* <b>Interrupts</b>
*
* The device does not have any way to disable the receiver such that the
* receiver may contain unwanted data. The IP is reset driver is initialized,
*
* The driver defaults to no interrupts at initialization such that interrupts
* must be enabled if desired. An interrupt is generated for any of the following
* conditions.
*
* - Data in the receiver
* - Any receive status error detected
* - Data byte transmitted
* - Any transmit status error detected
*
* The application can control which interrupts are enabled using the SetOptions
* function.
*
* In order to use interrupts, it is necessary for the user to connect the
* driver interrupt handler, XPs2_InterruptHandler(), to the interrupt system of
* the application. This function does not save and restore the processor
* context such that the user must provide it. A handler must be set for the
* driver such that the handler is called when interrupt events occur. The
* handler is called from interrupt context and is designed to allow application
*  specific processing to be performed.
*
* The functions, XPs2_Send() and Ps2_Recv(), are provided in the driver to
* allow data to be sent and received. They are designed to be used in polled
* or interrupt modes.
*
* <b>Initialization & Configuration</b>
*
* The XPs2_Config structure is used by the driver to configure itself. This
* configuration structure is typically created by the tool-chain based on HW
* build properties.
*
* To support multiple runtime loading and initialization strategies employed
* by various operating systems, the driver instance can be initialized in one
* of the following ways:
*
*   - XPs2_Initialize(InstancePtr, DeviceId) - The driver looks up its own
*     configuration structure created by the tool-chain based on an ID provided
*     by the tool-chain.
*
*   - XPs2_CfgInitialize(InstancePtr, CfgPtr, EffectiveAddr) - Uses a
*     configuration structure provided by the caller. If running in a system
*     with address translation, the provided virtual memory base address
*     replaces the physical address present in the configuration structure.
*
* @note
*
* None.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a ch   06/18/02 First release
* 1.01a jvb  12/14/05 I separated dependency on the static config table and
*                     xparameters.h from the driver initialization by moving
*                     _Initialize and _LookupConfig to _sinit.c. I also added
*                     the new _CfgInitialize routine.
* </pre>
*
******************************************************************************/

#ifndef XPS2_H			/* prevent circular inclusions */
#define XPS2_H			/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files ********************************/

#include "xbasic_types.h"
#include "xstatus.h"
#include "xps2_l.h"

/************************** Constant Definitions ****************************/

/*
 * These constants specify the handler events that are passed to
 * a handler from the driver. These constants are not bit masks suuch that
 * only one will be passed at a time to the handler
 */
#define XPS2_EVENT_RECV_DATA    1
#define XPS2_EVENT_RECV_ERROR   2
#define XPS2_EVENT_RECV_OVF     3
#define XPS2_EVENT_SENT_DATA    4
#define XPS2_EVENT_SENT_NOACK   5
#define XPS2_EVENT_TIMEOUT      6

/*
 * These constants specify the errors  that may be retrieved from the driver
 * using the XPs2_GetLastErrors function. All of them are bit masks, except
 * no error, such that multiple errors may be specified.
 */
#define XPS2_ERROR_NONE            0x00
#define XPS2_ERROR_WDT_TOUT_MASK   0x01
#define XPS2_ERROR_TX_NOACK_MASK   0x02
#define XPS2_ERROR_RX_OVF_MASK     0x08
#define XPS2_ERROR_RX_ERR_MASK     0x10

/**************************** Type Definitions ******************************/

/*
 * This typedef contains configuration information for the device
 */
	typedef struct {
		u16 DeviceId;	/* Unique ID  of device */
		u32 BaseAddress;	/* Base address of device */
	} XPs2_Config;

/*
 * The following data type is used to manage the buffers that are handled
 * when sending and receiving data in the interrupt mode
 */
	typedef struct {
		u8 *NextBytePtr;
		unsigned int RequestedBytes;
		unsigned int RemainingBytes;
	} XPs2Buffer;

/*
 * This data type defines a handler which the application must define
 * when using interrupt mode.  The handler will be called from the driver in an
 * interrupt context to handle application specific processing.
 *
 * @param CallBackRef is a callback reference passed in by the upper layer
 *        when setting the handler, and is passed back to the upper layer when
 *        the handler is called.
 * @param Event contains one of the event constants indicating why the handler
 *        is being called.
 * @param EventData contains the number of bytes sent or received at the time
*         of the call.
*/
	typedef void (*XPs2_Handler) (void *CallBackRef, u32 Event,
				      unsigned int EventData);
/*
 * PS/2 statistics
 */
	typedef struct {
		u16 TransmitInterrupts;
		u16 ReceiveInterrupts;
		u16 CharactersTransmitted;
		u16 CharactersReceived;
		u16 ReceiveErrors;
		u16 ReceiveOverflowErrors;
		u16 TransmitErrors;
	} XPs2Stats;

/*
 * The PS/2 driver instance data. The user is required to allocate a
 * variable of this type for every PS/2 device in the system.
 * If the last byte of a message was received then call the application
 * handler, this code should not use an else from the previous check of
 * the number of bytes to receive because the call to receive the buffer
 * updates the bytes to receive
 * A pointer to a variable of this type is then passed to the driver API
 * functions
 */
	typedef struct {
		XPs2Stats Stats;	/* Component Statistics */
		u32 BaseAddress;	/* Base address of device (IPIF) */
		u32 IsReady;	/* Device is initialized and ready */
		u8 LastErrors;	/* the accumulated errors */

		XPs2Buffer SendBuffer;
		XPs2Buffer ReceiveBuffer;

		XPs2_Handler Handler;
		void *CallBackRef;	/* Callback reference for control handler */
	} XPs2;

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Function Prototypes *****************************/

/*
 * Initialization functions in xps2_sinit.c
 */
	int XPs2_Initialize(XPs2 * InstancePtr, u16 DeviceId);
	XPs2_Config *XPs2_LookupConfig(u16 DeviceId);

/*
 * required functions is xps2.c
 */
	int XPs2_CfgInitialize(XPs2 * InstancePtr, XPs2_Config * Config,
				   u32 EffectiveAddr);
	unsigned int XPs2_Send(XPs2 * InstancePtr, u8 * BufferPtr,
			       unsigned int NumBytes);
	unsigned int XPs2_Recv(XPs2 * InstancePtr, u8 * BufferPtr,
			       unsigned int NumBytes);

/*
 * options functions in xps2_options.c
 */
	u8 XPs2_GetLastErrors(XPs2 * InstancePtr);
	u32 XPs2_IsSending(XPs2 * InstancePtr);

/*
 * interrupt functions in xps2_intr.c
 */
	void XPs2_SetHandler(XPs2 * InstancePtr, XPs2_Handler FuncPtr,
			     void *CallBackRef);
	void XPs2_InterruptHandler(XPs2 * InstancePtr);
	void XPs2_EnableInterrupt(XPs2 * InstancePtr);
	void XPs2_DisableInterrupt(XPs2 * InstancePtr);

#ifdef __cplusplus
}
#endif
#endif				/* end of protection macro */

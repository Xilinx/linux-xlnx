/* $Id: xiic.c,v 1.1 2007/12/03 15:44:58 meinelte Exp $ */
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
*       (c) Copyright 2002-2006 Xilinx Inc.
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
* @file xiic.c
*
* Contains required functions for the XIic component. See xiic.h for more
* information on the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- --- ------- -----------------------------------------------
* 1.01a rfp  10/19/01 release
* 1.01c ecm  12/05/02 new rev
* 1.01c rmm  05/14/03 Fixed diab compiler warnings relating to asserts.
* 1.01d jhl  10/08/03 Added general purpose output feature
* 1.02a jvb  12/13/05 I changed Initialize() into CfgInitialize(), and made
*                     CfgInitialize() take a pointer to a config structure
*                     instead of a device id. I moved Initialize() into
*                     xgpio_sinit.c, and had Initialize() call CfgInitialize()
*                     after it retrieved the config structure using the device
*                     id. I removed include of xparameters.h along with any
*                     dependencies on xparameters.h and the _g.c config table.
* 1.02a mta	 03/09/06 Added a new function XIic_IsIicBusy() which returns
*					  whether IIC Bus is Busy or Free.
* 1.13a wgr  03/22/07 Converted to new coding style.
* </pre>
*
****************************************************************************/

/***************************** Include Files *******************************/

#include "xiic.h"
#include "xiic_i.h"
#include "xio.h"

/************************** Constant Definitions ***************************/


/**************************** Type Definitions *****************************/


/***************** Macros (Inline Functions) Definitions *******************/


/************************** Function Prototypes ****************************/

static void XIic_StubStatusHandler(void *CallBackRef, int ErrorCode);

static void XIic_StubHandler(void *CallBackRef, int ByteCount);

/************************** Variable Definitions **************************/


/*****************************************************************************/
/**
*
* Initializes a specific XIic instance.  The initialization entails:
*
* - Check the device has an entry in the configuration table.
* - Initialize the driver to allow access to the device registers and
*   initialize other subcomponents necessary for the operation of the device.
* - Default options to:
*     - 7-bit slave addressing
*     - Send messages as a slave device
*     - Repeated start off
*     - General call recognition disabled
* - Clear messageing and error statistics
*
* The XIic_Start() function must be called after this function before the device
* is ready to send and receive data on the IIC bus.
*
* Before XIic_Start() is called, the interrupt control must connect the ISR
* routine to the interrupt handler. This is done by the user, and not
* XIic_Start() to allow the user to use an interrupt controller of their choice.
*
* @param InstancePtr is a pointer to the XIic instance to be worked on.
* @param Config is a reference to a structure containing information about
*        a specific IIC device. This function initializes an InstancePtr object
*        for a specific device specified by the contents of Config. This
*        function can initialize multiple instance objects with the use of
*        multiple calls giving different Config information on each call.
* @param EffectiveAddr is the device base address in the virtual memory address
*        space. The caller is responsible for keeping the address mapping
*        from EffectiveAddr to the device physical base address unchanged
*        once this function is invoked. Unexpected errors may occur if the
*        address mapping changes after this function is called. If address
*        translation is not used, use Config->BaseAddress for this parameters,
*        passing the physical address instead.
*
* @return
*
* - XST_SUCCESS when successful
* - XST_DEVICE_IS_STARTED indicates the device is started (i.e. interrupts
*   enabled and messaging is possible). Must stop before re-initialization
*   is allowed.
*
* @note
*
* None.
*
****************************************************************************/
int XIic_CfgInitialize(XIic * InstancePtr, XIic_Config * Config,
		       u32 EffectiveAddr)
{
	/*
	 * Asserts test the validity of selected input arguments.
	 */
	XASSERT_NONVOID(InstancePtr != NULL);

	InstancePtr->IsReady = 0;

	/*
	 * If the device is started, disallow the initialize and return a Status
	 * indicating it is started.  This allows the user to stop the device
	 * and reinitialize, but prevents a user from inadvertently initializing
	 */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return XST_DEVICE_IS_STARTED;
	}

	/*
	 * Set default values and configuration data, including setting the
	 * callback handlers to stubs  so the system will not crash should the
	 * application not assign its own callbacks.
	 */
	InstancePtr->IsStarted = 0;
	InstancePtr->BaseAddress = EffectiveAddr;
	InstancePtr->RecvHandler = XIic_StubHandler;
	InstancePtr->RecvBufferPtr = NULL;
	InstancePtr->SendHandler = XIic_StubHandler;
	InstancePtr->SendBufferPtr = NULL;
	InstancePtr->StatusHandler = XIic_StubStatusHandler;
	InstancePtr->Has10BitAddr = Config->Has10BitAddr;
	InstancePtr->IsReady = XCOMPONENT_IS_READY;
	InstancePtr->Options = 0;
	InstancePtr->BNBOnly = FALSE;
	InstancePtr->GpOutWidth = Config->GpOutWidth;
	InstancePtr->IsDynamic = FALSE;

	/*
	 * Reset the device so it's in the reset state, this must be after the
	 * IPIF is initialized since it resets thru the IPIF and clear the stats
	 */
	XIic_Reset(InstancePtr);

	XIIC_CLEAR_STATS(InstancePtr);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function starts the IIC device and driver by enabling the proper
* interrupts such that data may be sent and received on the IIC bus.
* This function must be called before the functions to send and receive data.
*
* Before XIic_Start() is called, the interrupt control must connect the ISR
* routine to the interrupt handler. This is done by the user, and not
* XIic_Start() to allow the user to use an interrupt controller of their choice.
*
* Start enables:
*  - IIC device
*  - Interrupts:
*     - Addressed as slave to allow messages from another master
*     - Arbitration Lost to detect Tx arbitration errors
*     - Global IIC interrupt within the IPIF interface
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
*
* @return
*
* XST_SUCCESS always
*
* @note
*
* The device interrupt is connected to the interrupt controller, but no
* "messaging" interrupts are enabled. Addressed as Slave is enabled to
* reception of messages when this devices address is written to the bus.
* The correct messaging interrupts are enabled when sending or receiving
* via the IicSend() and IicRecv() functions. No action is required
* by the user to control any IIC interrupts as the driver completely
* manages all 8 interrupts. Start and Stop control the ability
* to use the device. Stopping the device completely stops all device
* interrupts from the processor.
*
****************************************************************************/
int XIic_Start(XIic * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Mask off all interrupts, each is enabled when needed.
	 */
	XIIC_WRITE_IIER(InstancePtr->BaseAddress, 0);

	/*
	 * Clear all interrupts by reading and rewriting exact value back.
	 * Only those bits set will get written as 1 (writing 1 clears intr)
	 */
	XIic_mClearIntr(InstancePtr->BaseAddress, 0xFFFFFFFF);

	/*
	 * Enable the device
	 */
	XIo_Out8(InstancePtr->BaseAddress + XIIC_CR_REG_OFFSET,
		 XIIC_CR_ENABLE_DEVICE_MASK);
	/*
	 * Set Rx FIFO Occupancy depth to throttle at first byte(after reset = 0)
	 */
	XIo_Out8(InstancePtr->BaseAddress + XIIC_RFD_REG_OFFSET, 0);

	/*
	 * Clear and enable the interrupts needed
	 */
	XIic_mClearEnableIntr(InstancePtr->BaseAddress,
			      XIIC_INTR_AAS_MASK | XIIC_INTR_ARB_LOST_MASK);

	InstancePtr->IsStarted = XCOMPONENT_IS_STARTED;
	InstancePtr->IsDynamic = FALSE;

	/* Enable all interrupts by the global enable in the IPIF */

	XIIC_GINTR_ENABLE(InstancePtr->BaseAddress);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function stops the IIC device and driver such that data is no longer
* sent or received on the IIC bus. This function stops the device by
* disabling interrupts. This function only disables interrupts within the
* device such that the caller is responsible for disconnecting the interrupt
* handler of the device from the interrupt source and disabling interrupts
* at other levels.
*
* Due to bus throttling that could hold the bus between messages when using
* repeated start option, stop will not occur when the device is actively
* sending or receiving data from the IIC bus or the bus is being throttled
* by this device, but instead return XST_IIC_BUS_BUSY.
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
*
* @return
*
* - XST_SUCCESS indicates all IIC interrupts are disabled. No messages can
*   be received or transmitted until XIic_Start() is called.
* - XST_IIC_BUS_BUSY indicates this device is currently engaged in message
*   traffic and cannot be stopped.
*
* @note
*
* None.
*
****************************************************************************/
int XIic_Stop(XIic * InstancePtr)
{
	u8 Status;
	u8 CntlReg;

	XASSERT_NONVOID(InstancePtr != NULL);

	/*
	 * Disable all interrupts globally using the IPIF
	 */
	XIIC_GINTR_DISABLE(InstancePtr->BaseAddress);

	CntlReg = XIo_In8(InstancePtr->BaseAddress + XIIC_CR_REG_OFFSET);
	Status = XIo_In8(InstancePtr->BaseAddress + XIIC_SR_REG_OFFSET);

	if ((CntlReg & XIIC_CR_MSMS_MASK) ||
	    (Status & XIIC_SR_ADDR_AS_SLAVE_MASK)) {
		/* when this device is using the bus
		 * - re-enable interrupts to finish current messaging
		 * - return bus busy
		 */
		XIIC_GINTR_ENABLE(InstancePtr->BaseAddress);

		return XST_IIC_BUS_BUSY;
	}

	InstancePtr->IsStarted = 0;

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Resets the IIC device. Reset must only be called after the driver has been
* initialized. The configuration after this reset is as follows:
*   - Repeated start is disabled
*   - General call is disabled
*
* The upper layer software is responsible for initializing and re-configuring
* (if necessary) and restarting the IIC device after the reset.
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
* @internal
*
* The reset is accomplished by setting the IPIF reset register.  This takes
* care of resetting all IPIF hardware blocks, including the IIC device.
*
****************************************************************************/
void XIic_Reset(XIic * InstancePtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	XIIC_RESET(InstancePtr->BaseAddress);
}

/*****************************************************************************/
/**
*
* This function sets the bus addresses. The addresses include the device
* address that the device responds to as a slave, or the slave address
* to communicate with on the bus.  The IIC device hardware is built to
* allow either 7 or 10 bit slave addressing only at build time rather
* than at run time. When this device is a master, slave addressing can
* be selected at run time to match addressing modes for other bus devices.
*
* Addresses are represented as hex values with no adjustment for the data
* direction bit as the software manages address bit placement.
* Example: For a 7 address written to the device of 1010 011X where X is
* the transfer direction (send/recv), the address parameter for this function
* needs to be 01010011 or 0x53 where the correct bit alllignment will be
* handled for 7 as well as 10 bit devices. This is especially important as
* the bit placement is not handled the same depending on which options are
* used such as repeated start.
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
* @param    AddressType indicates which address is being modified; the address
*           which this device responds to on the IIC bus as a slave, or the
*           slave address to communicate with when this device is a master. One
*           of the following values must be contained in this argument.
* <pre>
*   XII_ADDRESS_TO_SEND         Slave being addressed by a this master
*   XII_ADDRESS_TO_RESPOND      Address to respond to as a slave device
* </pre>
* @param    Address contains the address to be set; 7 bit or 10 bit address.
*           A ten bit address must be within the range: 0 - 1023 and a 7 bit
*           address must be within the range 0 - 127.
*
* @return
*
* XST_SUCCESS is returned if the address was successfully set, otherwise one
* of the following errors is returned.
* - XST_IIC_NO_10_BIT_ADDRESSING indicates only 7 bit addressing supported.
* - XST_INVALID_PARAM indicates an invalid parameter was specified.
*
* @note
*
* Upper bits of 10-bit address is written only when current device is built
* as a ten bit device.
*
****************************************************************************/
int XIic_SetAddress(XIic * InstancePtr, int AddressType, int Address)
{
	u8 SendAddr;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(Address < 1023);

	/* Set address to respond to for this device into address registers */

	if (AddressType == XII_ADDR_TO_RESPOND_TYPE) {
		SendAddr = (u8) ((Address & 0x007F) << 1);	/* Addr in upper 7 bits */
		XIo_Out8(InstancePtr->BaseAddress + XIIC_ADR_REG_OFFSET,
			 SendAddr);

		if (InstancePtr->Has10BitAddr == TRUE) {
			/* Write upper 3 bits of addr to DTR only when 10 bit option
			 * included in design i.e. register exists
			 */
			SendAddr = (u8) ((Address & 0x0380) >> 7);
			XIo_Out8(InstancePtr->BaseAddress + XIIC_TBA_REG_OFFSET,
				 SendAddr);
		}

		return XST_SUCCESS;
	}

	/* Store address of slave device being read from */

	if (AddressType == XII_ADDR_TO_SEND_TYPE) {
		InstancePtr->AddrOfSlave = Address;
		return XST_SUCCESS;
	}

	return XST_INVALID_PARAM;
}

/*****************************************************************************/
/**
*
* This function gets the addresses for the IIC device driver. The addresses
* include the device address that the device responds to as a slave, or the
* slave address to communicate with on the bus. The address returned has the
* same format whether 7 or 10 bits.
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
* @param    AddressType indicates which address, the address which this
*           responds to on the IIC bus as a slave, or the slave address to
*           communicate with when this device is a master. One of the following
*           values must be contained in this argument.
* <pre>
*   XII_ADDRESS_TO_SEND_TYPE         slave being addressed as a master
*   XII_ADDRESS_TO_RESPOND_TYPE      slave address to respond to as a slave
* </pre>
*  If neither of the two valid arguments are used, the function returns
*  the address of the slave device
*
* @return
*
* The address retrieved.
*
* @note
*
* None.
*
****************************************************************************/
u16 XIic_GetAddress(XIic * InstancePtr, int AddressType)
{
	u8 LowAddr;
	u16 HighAddr = 0;

	XASSERT_NONVOID(InstancePtr != NULL);

	/* return this devices address */

	if (AddressType == XII_ADDR_TO_RESPOND_TYPE) {

		LowAddr =
			XIo_In8(InstancePtr->BaseAddress + XIIC_ADR_REG_OFFSET);

		if (InstancePtr->Has10BitAddr == TRUE) {
			HighAddr = (u16) XIo_In8(InstancePtr->BaseAddress +
						 XIIC_TBA_REG_OFFSET);
		}
		return ((HighAddr << 8) & (u16) LowAddr);
	}

	/* Otherwise return address of slave device on the IIC bus */

	return InstancePtr->AddrOfSlave;
}

/*****************************************************************************/
/**
*
* This function sets the contents of the General Purpose Output register
* for the IIC device driver. Note that the number of bits in this register is
* parameterizable in the hardware such that it may not exist.  This function
* checks to ensure that it does exist to prevent bus errors, but does not
* ensure that the number of bits in the register are sufficient for the
* value being written (won't cause a bus error).
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
*
* @param    OutputValue contains the value to be written to the register.
*
* @return
*
* A value indicating success, XST_SUCCESS, or XST_NO_FEATURE if the hardware
* is configured such that this register does not contain any bits to read
* or write.
*
* @note
*
* None.
*
****************************************************************************/
int XIic_SetGpOutput(XIic * InstancePtr, u8 OutputValue)
{
	XASSERT_NONVOID(InstancePtr != NULL);

	/* If the general purpose output register is implemented by the hardware
	 * then write the specified value to it, otherwise indicate an error
	 */
	if (InstancePtr->GpOutWidth > 0) {
		XIic_mWriteReg(InstancePtr->BaseAddress, XIIC_GPO_REG_OFFSET,
			       OutputValue);
		return XST_SUCCESS;
	}
	else {
		return XST_NO_FEATURE;
	}
}


/*****************************************************************************/
/**
*
* This function gets the contents of the General Purpose Output register
* for the IIC device driver. Note that the number of bits in this register is
* parameterizable in the hardware such that it may not exist.  This function
* checks to ensure that it does exist to prevent bus errors.
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
*
* @param    OutputValuePtr contains the value which was read from the
*           register.
*
* @return
*
* A value indicating success, XST_SUCCESS, or XST_NO_FEATURE if the hardware
* is configured such that this register does not contain any bits to read
* or write.
*
* The OutputValuePtr is also an output as it contains the value read.
*
* @note
*
* None.
*
****************************************************************************/
int XIic_GetGpOutput(XIic * InstancePtr, u8 *OutputValuePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(OutputValuePtr != NULL);

	/* If the general purpose output register is implemented by the hardware
	 * then read the value from it, otherwise indicate an error
	 */
	if (InstancePtr->GpOutWidth > 0) {
		*OutputValuePtr = XIic_mReadReg(InstancePtr->BaseAddress,
						XIIC_GPO_REG_OFFSET);
		return XST_SUCCESS;
	}
	else {
		return XST_NO_FEATURE;
	}
}

/*****************************************************************************/
/**
*
* A function to determine if the device is currently addressed as a slave
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
*
* @return
*
* TRUE if the device is addressed as slave, and FALSE otherwise.
*
* @note
*
* None.
*
****************************************************************************/
u32 XIic_IsSlave(XIic * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);

	if ((XIo_In8(InstancePtr->BaseAddress + XIIC_SR_REG_OFFSET) &
	     XIIC_SR_ADDR_AS_SLAVE_MASK) == 0) {
		return FALSE;
	}
	return TRUE;
}

/*****************************************************************************/
/**
*
* Sets the receive callback function, the receive handler, which the driver
* calls when it finishes receiving data. The number of bytes used to signal
* when the receive is complete is the number of bytes set in the XIic_Recv
* function.
*
* The handler executes in an interrupt context such that it must minimize
* the amount of processing performed such as transferring data to a thread
* context.
*
* The number of bytes received is passed to the handler as an argument.
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
* @param    CallBackRef is the upper layer callback reference passed back when
*           the callback function is invoked.
* @param    FuncPtr is the pointer to the callback function.
*
* @return
*
* None.
*
* @note
*
* The handler is called within interrupt context ...
*
****************************************************************************/
void XIic_SetRecvHandler(XIic * InstancePtr, void *CallBackRef,
			 XIic_Handler FuncPtr)
{
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);

	InstancePtr->RecvHandler = FuncPtr;
	InstancePtr->RecvCallBackRef = CallBackRef;
}

/*****************************************************************************/
/**
*
* Sets the send callback function, the send handler, which the driver calls when
* it receives confirmation of sent data. The handler executes in an interrupt
* context such that it must minimize the amount of processing performed such
* as transferring data to a thread context.
*
* @param    InstancePtr the pointer to the XIic instance to be worked on.
* @param    CallBackRef the upper layer callback reference passed back when
*           the callback function is invoked.
* @param    FuncPtr the pointer to the callback function.
*
* @return
*
* None.
*
* @note
*
* The handler is called within interrupt context ...
*
****************************************************************************/
void XIic_SetSendHandler(XIic * InstancePtr, void *CallBackRef,
			 XIic_Handler FuncPtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID(FuncPtr != NULL);

	InstancePtr->SendHandler = FuncPtr;
	InstancePtr->SendCallBackRef = CallBackRef;
}

/*****************************************************************************/
/**
*
* Sets the status callback function, the status handler, which the driver calls
* when it encounters conditions which are not data related. The handler
* executes in an interrupt context such that it must minimize the amount of
* processing performed such as transferring data to a thread context. The
* status events that can be returned are described in xiic.h.
*
* @param    InstancePtr points to the XIic instance to be worked on.
* @param    CallBackRef is the upper layer callback reference passed back when
*           the callback function is invoked.
* @param    FuncPtr is the pointer to the callback function.
*
* @return
*
* None.
*
* @note
*
* The handler is called within interrupt context ...
*
****************************************************************************/
void XIic_SetStatusHandler(XIic * InstancePtr, void *CallBackRef,
			   XIic_StatusHandler FuncPtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID(FuncPtr != NULL);

	InstancePtr->StatusHandler = FuncPtr;
	InstancePtr->StatusCallBackRef = CallBackRef;
}

/*****************************************************************************
*
* This is a stub for the send and recv callbacks. The stub is here in case the
* upper layers forget to set the handlers.
*
* @param    CallBackRef is a pointer to the upper layer callback reference
* @param    ByteCount is the number of bytes sent or received
*
* @return
*
* None.
*
* @note
*
* None.
*
******************************************************************************/
static void XIic_StubHandler(void *CallBackRef, int ByteCount)
{
	XASSERT_VOID_ALWAYS();
}

/*****************************************************************************
*
* This is a stub for the asynchronous error callback. The stub is here in case
* the upper layers forget to set the handler.
*
* @param    CallBackRef is a pointer to the upper layer callback reference
* @param    ErrorCode is the Xilinx error code, indicating the cause of the error
*
* @return
*
* None.
*
* @note
*
* None.
*
******************************************************************************/
static void XIic_StubStatusHandler(void *CallBackRef, int ErrorCode)
{
	XASSERT_VOID_ALWAYS();
}

/*****************************************************************************
*
* This is a function which tells whether Bus is Busy or free.
*
* @param    InstancePtr points to the XIic instance to be worked on.
*
* @return  TRUE if Bus is Busy else FALSE
*
* @note    None.
*
******************************************************************************/
u32 XIic_IsIicBusy(XIic * InstancePtr)
{
	u8 StatusReg;

	StatusReg = XIic_mReadReg(InstancePtr->BaseAddress, XIIC_SR_REG_OFFSET);
	if (StatusReg & XIIC_SR_BUS_BUSY_MASK) {
		return TRUE;
	}
	else {
		return FALSE;
	}
}

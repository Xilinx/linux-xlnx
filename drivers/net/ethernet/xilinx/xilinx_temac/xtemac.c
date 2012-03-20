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
*       (c) Copyright 2005-2006 Xilinx Inc.
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
* @file xtemac.c
*
* The XTemac driver. Functions in this file are the minimum required functions
* for this driver. See xtemac.h for a detailed description of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rmm  06/01/05 First release
* 2.00a rmm  11/21/05 Switched to local link DMA driver, removed simple
*                     DMA code, relocated XTemac_Initialize(),
*                     XTemac_VmInitialize(), and XTemac_LookupConfig() to
*                     xtemac_init.c, added XTemac_CfgInitialize().
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include <linux/string.h>

#include "xtemac.h"
#include "xtemac_i.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/

void XTemac_StubHandler(void);	/* Default handler routine */
static void InitHw(XTemac *InstancePtr);	/* HW reset */

/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
* Initialize a specific XTemac instance/driver. The initialization entails:
* - Initialize fields of the XTemac instance structure
* - Reset HW and apply default options
* - Configure the packet FIFOs if present
* - Configure the DMA channels if present
*
* The PHY is setup independently from the TEMAC. Use the MII or whatever other
* interface may be present for setup.
*
* @param InstancePtr is a pointer to the instance to be worked on.
* @param CfgPtr is the device configuration structure containing required HW
*        build data.
* @param VirtualAddress is the base address of the device. If address
*        translation is not utilized, this parameter can be passed in using
*        CfgPtr->BaseAddress to specify the physical base address.
*
* @return
* - XST_SUCCESS if initialization was successful
* - XST_FAILURE if initialization of packet FIFOs or DMA channels failed, or
*   device operating mode cannot be determined
*
******************************************************************************/
int XTemac_CfgInitialize(XTemac *InstancePtr, XTemac_Config *CfgPtr,
			 u32 VirtualAddress)
{
	int Result;

	/* Verify arguments */
	XASSERT_NONVOID(InstancePtr != NULL);

	/* Clear instance memory and make copy of configuration */
	memset(InstancePtr, 0, sizeof(XTemac));
	memcpy(&InstancePtr->Config, CfgPtr, sizeof(XTemac_Config));

	/* Set device base address */
	InstancePtr->BaseAddress = VirtualAddress;

	/* Set callbacks to an initial stub routine */
	InstancePtr->FifoRecvHandler =
		(XTemac_FifoRecvHandler) XTemac_StubHandler;
	InstancePtr->FifoSendHandler =
		(XTemac_FifoSendHandler) XTemac_StubHandler;
	InstancePtr->ErrorHandler = (XTemac_ErrorHandler) XTemac_StubHandler;
	InstancePtr->AnegHandler = (XTemac_AnegHandler) XTemac_StubHandler;
	InstancePtr->SgRecvHandler = (XTemac_SgHandler) XTemac_StubHandler;
	InstancePtr->SgSendHandler = (XTemac_SgHandler) XTemac_StubHandler;

	/* FIFO mode */
	if (XTemac_mIsFifo(InstancePtr)) {
		/* Select best processor based transfer method to/from FIFOs */
		Result = XTemac_ConfigureFifoAccess(InstancePtr);
		if (Result != XST_SUCCESS) {
			return (XST_FAILURE);
		}
	}

	/* SGDMA mode */
	else if (XTemac_mIsSgDma(InstancePtr)) {
		Result = XDmaV3_Initialize(&InstancePtr->RecvDma,
					   InstancePtr->BaseAddress +
					   XTE_DMA_RECV_OFFSET);
		if (Result != XST_SUCCESS) {
			return (XST_FAILURE);
		}

		Result = XDmaV3_Initialize(&InstancePtr->SendDma,
					   InstancePtr->BaseAddress +
					   XTE_DMA_SEND_OFFSET);
		if (Result != XST_SUCCESS) {
			return (XST_FAILURE);
		}
	}

	/* Unknown mode */
	else {
		return (XST_FAILURE);
	}

	/* Reset the hardware and set default options */
	InstancePtr->IsReady = XCOMPONENT_IS_READY;
	XTemac_Reset(InstancePtr, XTE_NORESET_HARD);

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
* Start the Ethernet controller as follows:
*   - Enable transmitter if XTE_TRANSMIT_ENABLE_OPTION is set
*   - Enable receiver if XTE_RECEIVER_ENABLE_OPTION is set
*   - If not polled mode, then start the SG DMA send and receive channels (if
*     configured) and enable the global device interrupt
*
* If starting for the first time after calling XTemac_Initialize() or
* XTemac_Reset(), send and receive interrupts will not be generated until
* XTemac_IntrFifoEnable() or XTemac_IntrSgEnable() are called. Otherwise,
* interrupt settings made by these functions will be restored.
*
* @param InstancePtr is a pointer to the instance to be worked on.
*
* @return
* - XST_SUCCESS if the device was started successfully
* - XST_DMA_SG_NO_LIST if configured for scatter-gather DMA and a descriptor
*   list has not yet been created for the send or receive channel
*
* @note
* The driver tries to match the hardware configuration. So if the hardware
* is configured with scatter-gather DMA, the driver expects to start the
* scatter-gather channels and expects that the user has previously set up
* the buffer descriptor lists.
*
* This function makes use of internal resources that are shared between the
* Start, Stop, and Set/ClearOptions functions. So if one task might be setting
* device options while another is trying to start the device, the user is
* required to provide protection of this shared data (typically using a
* semaphore).
*
* This function must not be preempted by an interrupt that may service the
* device.
*
******************************************************************************/
int XTemac_Start(XTemac *InstancePtr)
{
	u32 Reg;
	int Result;

	/* Assert bad arguments and conditions */
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* If already started, then there is nothing to do */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_SUCCESS);
	}

	/* Start SG DMA */
	if (XTemac_mIsSgDma(InstancePtr)) {
		/* When starting the DMA channels, both transmit and receive sides
		 * need an initialized BD list.
		 */
		Result = XDmaV3_SgStart(&InstancePtr->RecvDma);
		if (Result == XST_DMA_SG_NO_LIST) {
			return (Result);
		}

		Result = XDmaV3_SgStart(&InstancePtr->SendDma);
		if (Result == XST_DMA_SG_NO_LIST) {
			return (Result);
		}
	}

	/* Enable transmitter if not already enabled */
	if (InstancePtr->Options & XTE_TRANSMITTER_ENABLE_OPTION) {
		Reg = XTemac_mGetHostReg(XTE_TXC_OFFSET);
		if (!(Reg & XTE_TXC_TXEN_MASK)) {
			XTemac_mSetHostReg(XTE_TXC_OFFSET,
					   Reg | XTE_TXC_TXEN_MASK);
		}
	}

	/* Enable receiver? */
	if (InstancePtr->Options & XTE_RECEIVER_ENABLE_OPTION) {
		Reg = XTemac_mGetHostReg(XTE_RXC1_OFFSET) | XTE_RXC1_RXEN_MASK;
		XTemac_mSetHostReg(XTE_RXC1_OFFSET, Reg);
	}

	/* Mark as started */
	InstancePtr->IsStarted = XCOMPONENT_IS_STARTED;

	/* Allow interrupts (if not in polled mode) and exit */
	if ((InstancePtr->Options & XTE_POLLED_OPTION) == 0) {
		XTemac_mSetIpifReg(XTE_DGIE_OFFSET, XTE_DGIE_ENABLE_MASK);
	}

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
* Gracefully stop the Ethernet MAC as follows:
*   - Disable all interrupts from this device
*   - Stop DMA channels (if configured)
*   - Disable the receiver
*
* Device options currently in effect are not changed.
*
* This function will disable all interrupts by clearing the global interrupt
* enable. Any interrupts settings that had been enabled through
* XTemac_IntrFifoEnable(), XTemac_IntrFifoDmaEnable(), or
* XTemac_IntrSgEnable() will be restored when XTemac_Start() is called.
*
* Since the transmitter is not disabled, frames currently in the packet FIFO
* or in process by the SGDMA engine are allowed to be transmitted. XTemac API
* functions that place new data in the packet FIFOs will not be allowed to do
* so until XTemac_Start() is called.
*
* @param InstancePtr is a pointer to the instance to be worked on.
*
* @note
* This function makes use of internal resources that are shared between the
* Start, Stop, SetOptions, and ClearOptions functions. So if one task might be
* setting device options while another is trying to start the device, the user
* is required to provide protection of this shared data (typically using a
* semaphore).
*
* Stopping the DMA channels may cause this function to block until the DMA
* operation is complete. This function will not block waiting for frame data to
* to exit the packet FIFO to the transmitter.
*
******************************************************************************/
void XTemac_Stop(XTemac *InstancePtr)
{
	volatile u32 Reg;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* If already stopped, then there is nothing to do */
	if (InstancePtr->IsStarted == 0) {
		return;
	}

	/* Disable interrupts */
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, 0);

	/* For SGDMA, use the DMA driver function to stop the channels */
	if (XTemac_mIsSgDma(InstancePtr)) {
		(void) XDmaV3_SgStop(&InstancePtr->SendDma);
		(void) XDmaV3_SgStop(&InstancePtr->RecvDma);
	}

	/* Disable the receiver */
	Reg = XTemac_mGetHostReg(XTE_RXC1_OFFSET);
	Reg &= ~XTE_RXC1_RXEN_MASK;
	XTemac_mSetHostReg(XTE_RXC1_OFFSET, Reg);

	/* Stopping the receiver in mid-packet causes a dropped packet indication
	 * from HW. Clear it.
	 */
	Reg = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
	if (Reg & XTE_IPXR_RECV_REJECT_MASK) {
		XTemac_mSetIpifReg(XTE_IPISR_OFFSET, XTE_IPXR_RECV_REJECT_MASK);
	}

	/* Mark as stopped */
	InstancePtr->IsStarted = 0;
}


/*****************************************************************************/
/**
* Perform a graceful reset of the Ethernet MAC. Resets the DMA channels, the
* FIFOs, the transmitter, and the receiver.
*
* All options are placed in their default state. Any frames in the scatter-
* gather descriptor lists will remain in the lists. The side effect of doing
* this is that after a reset and following a restart of the device, frames that
* were in the list before the reset may be transmitted or received.
*
* The upper layer software is responsible for re-configuring (if necessary)
* and restarting the MAC after the reset. Note also that driver statistics
* are not cleared on reset. It is up to the upper layer software to clear the
* statistics if needed.
*
* When a reset is required due to an internal error, the driver notifies the
* upper layer software of this need through the ErrorHandler callback and
* specific status codes.  The upper layer software is responsible for calling
* this Reset function and then re-configuring the device.
*
* Resetting the IPIF should suffice in most circumstances. As a last resort
* however, the hard TEMAC core can be reset as well using the HardCoreAction
* parameter. In systems with two TEMACs, the reset signal is shared between
* both devices resulting in BOTH being reset. This requires the user save the
* state of both TEMAC's prior to resetting the hard core on either device
* instance.

* @param InstancePtr is a pointer to the instance to be worked on.
* @param HardCoreAction describes how the hard core part of the TEMAC should
*        be managed. If XTE_RESET_HARD is passed in, then the reset signal is
*        asserted to the hard core block. This will reset both hard cores.
*        If any other value is passed in, then only the IPIF of the given
*        instance is reset.
*
******************************************************************************/
void XTemac_Reset(XTemac *InstancePtr, int HardCoreAction)
{
	u32 Data;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Stop the device and reset HW */
	XTemac_Stop(InstancePtr);
	InstancePtr->Options = XTE_DEFAULT_OPTIONS;

	/* Reset IPIF */
	XTemac_mSetIpifReg(XTE_DSR_OFFSET, XTE_DSR_RESET_MASK);
	udelay(XTE_RESET_IPIF_DELAY_US);

	/* Reset hard core if required */
	if (HardCoreAction == XTE_RESET_HARD) {
		Data = XTemac_mGetIpifReg(XTE_CR_OFFSET);
		XTemac_mSetIpifReg(XTE_CR_OFFSET, Data | XTE_CR_HRST_MASK);
		udelay(XTE_RESET_HARD_DELAY_US);
	}

	/* Setup HW */
	InitHw(InstancePtr);
}


/******************************************************************************
 * Perform one-time setup of HW. The setups performed here only need to occur
 * once after any reset.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 ******************************************************************************/
static void InitHw(XTemac *InstancePtr)
{
	u32 Reg;

	/* Disable the receiver */
	Reg = XTemac_mGetHostReg(XTE_RXC1_OFFSET);
	Reg &= ~XTE_RXC1_RXEN_MASK;
	XTemac_mSetHostReg(XTE_RXC1_OFFSET, Reg);

	/* Stopping the receiver in mid-packet causes a dropped packet indication
	 * from HW. Clear it.
	 */
	Reg = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
	if (Reg & XTE_IPXR_RECV_REJECT_MASK) {
		XTemac_mSetIpifReg(XTE_IPISR_OFFSET, XTE_IPXR_RECV_REJECT_MASK);
	}

	/* Default IPIF interrupt block enable mask */
	Reg = (XTE_DXR_CORE_MASK | XTE_DXR_DPTO_MASK | XTE_DXR_TERR_MASK);

	if (XTemac_mIsFifo(InstancePtr)) {
		Reg |= (XTE_DXR_RECV_FIFO_MASK | XTE_DXR_SEND_FIFO_MASK);
	}

	XTemac_mSetIpifReg(XTE_DIER_OFFSET, Reg);

	if (XTemac_mIsSgDma(InstancePtr)) {
		/* Setup SGDMA interupt coalescing defaults */
		(void) XTemac_IntrSgCoalSet(InstancePtr, XTE_SEND,
					    XTE_SGDMA_DFT_THRESHOLD,
					    XTE_SGDMA_DFT_WAITBOUND);
		(void) XTemac_IntrSgCoalSet(InstancePtr, XTE_RECV,
					    XTE_SGDMA_DFT_THRESHOLD,
					    XTE_SGDMA_DFT_WAITBOUND);

		/* Setup interrupt enable data for each channel */
		Reg = (XDMAV3_IPXR_PCTR_MASK |
		       XDMAV3_IPXR_PWBR_MASK | XDMAV3_IPXR_DE_MASK);

		XDmaV3_SetInterruptEnable(&InstancePtr->SendDma, Reg);
		XDmaV3_SetInterruptEnable(&InstancePtr->RecvDma, Reg);
	}

	/* Sync default options with HW but leave receiver and transmitter
	 * disabled. They get enabled with XTemac_Start() if XTE_TRANSMITTER_ENABLE-
	 * _OPTION and XTE_RECEIVER_ENABLE_OPTION are set
	 */
	XTemac_SetOptions(InstancePtr, InstancePtr->Options &
			  ~(XTE_TRANSMITTER_ENABLE_OPTION |
			    XTE_RECEIVER_ENABLE_OPTION));

	XTemac_ClearOptions(InstancePtr, ~InstancePtr->Options);

	/* Set default MDIO divisor */
	XTemac_PhySetMdioDivisor(InstancePtr, XTE_MDIO_DIV_DFT);
}

/******************************************************************************/
/**
 * This is a stub for the asynchronous callbacks. The stub is here in case the
 * upper layer forgot to set the handler(s). On initialization, all handlers are
 * set to this callback. It is considered an error for this handler to be
 * invoked.
 *
 ******************************************************************************/
void XTemac_StubHandler(void)
{
	XASSERT_VOID_ALWAYS();
}

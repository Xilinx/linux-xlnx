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
*
******************************************************************************/
/*****************************************************************************/
/**
 *
 * @file xlltemac.c
 *
 * The XLlTemac driver. Functions in this file are the minimum required functions
 * for this driver. See xlltemac.h for a detailed description of the driver.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a jvb  11/10/06 First release
 * </pre>
 ******************************************************************************/

/***************************** Include Files *********************************/

#include <linux/string.h>
#include <linux/delay.h>

#include "xlltemac.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/

static void InitHw(XLlTemac *InstancePtr);	/* HW reset */

/************************** Variable Definitions *****************************/

xdbg_stmnt(int indent_on = 0;

	)
	xdbg_stmnt(u32 _xlltemac_rir_value;

	)

/*****************************************************************************/
/**
 *
 * XLlTemac_CfgInitialize initializes a TEMAC channel along with the
 * <i>InstancePtr</i> that references it. Each TEMAC channel is treated as a
 * separate device from the point of view of this driver.
 *
 * The PHY is setup independently from the TEMAC. Use the MII or whatever other
 * interface may be present for setup.
 *
 * @param  InstancePtr references the memory instance to be associated with
 *         the TEMAC channel upon initialization.
 * @param  CfgPtr references the structure holding the hardware configuration
 *         for the TEMAC channel to initialize.
 * @param  EffectiveAddress is the processor address used to access the
 *         base address of the TEMAC channel. In systems with an MMU and virtual
 *         memory, <i>EffectiveAddress</i> is the virtual address mapped to the
 *         physical in <code>ConfigPtr->Config.BaseAddress</code>. In systems
 *         without an active MMU, <i>EffectiveAddress</i> should be set to the
 *         same value as <code>ConfigPtr->Config.BaseAddress</code>.
 *        
 * @return XLlTemac_CfgInitialize returns XST_SUCCESS.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 *
 ******************************************************************************/
     int XLlTemac_CfgInitialize(XLlTemac *InstancePtr,
				XLlTemac_Config *CfgPtr, u32 EffectiveAddress)
{
	/* Verify arguments */
	XASSERT_NONVOID(InstancePtr != NULL);

	/* Clear instance memory and make copy of configuration */
	memset(InstancePtr, 0, sizeof(XLlTemac));
	memcpy(&InstancePtr->Config, CfgPtr, sizeof(XLlTemac_Config));

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_CfgInitialize\n");
	/* Set device base address */
	InstancePtr->Config.BaseAddress = EffectiveAddress;

	/* Reset the hardware and set default options */
	InstancePtr->IsReady = XCOMPONENT_IS_READY;

	XLlTemac_Reset(InstancePtr, XTE_NORESET_HARD);

	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "Temac_CfgInitialize: returning SUCCESS\n");
	return XST_SUCCESS;
}


/*****************************************************************************/
/**
 * XLlTemac_Start starts the TEMAC channel as follows:
 *   - Enable transmitter if XTE_TRANSMIT_ENABLE_OPTION is set
 *   - Enable receiver if XTE_RECEIVER_ENABLE_OPTION is set
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return N/A
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
void XLlTemac_Start(XLlTemac *InstancePtr)
{
	u32 Reg;

	/* Assert bad arguments and conditions */
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);

	/* If already started, then there is nothing to do */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return;
	}

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_Start\n");
	/* Enable transmitter if not already enabled */
	if (InstancePtr->Options & XTE_TRANSMITTER_ENABLE_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL, "enabling transmitter\n");
		Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					       XTE_TC_OFFSET);
		if (!(Reg & XTE_TC_TX_MASK)) {
			xdbg_printf(XDBG_DEBUG_GENERAL,
				    "transmitter not enabled, enabling now\n");
			XLlTemac_WriteIndirectReg(InstancePtr->Config.
						  BaseAddress, XTE_TC_OFFSET,
						  Reg | XTE_TC_TX_MASK);
		}
		xdbg_printf(XDBG_DEBUG_GENERAL, "transmitter enabled\n");
	}

	/* Enable receiver */
	if (InstancePtr->Options & XTE_RECEIVER_ENABLE_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL, "enabling receiver\n");
		Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					       XTE_RCW1_OFFSET);
		if (!(Reg & XTE_RCW1_RX_MASK)) {
			xdbg_printf(XDBG_DEBUG_GENERAL,
				    "receiver not enabled, enabling now\n");

			XLlTemac_WriteIndirectReg(InstancePtr->Config.
						  BaseAddress, XTE_RCW1_OFFSET,
						  Reg | XTE_RCW1_RX_MASK);
		}
		xdbg_printf(XDBG_DEBUG_GENERAL, "receiver enabled\n");
	}

	/* Mark as started */
	InstancePtr->IsStarted = XCOMPONENT_IS_STARTED;
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_Start: done\n");
}

/*****************************************************************************/
/**
 * XLlTemac_Stop gracefully stops the TEMAC channel as follows:
 *   - Disable all interrupts from this device
 *   - Disable the receiver
 *
 * XLlTemac_Stop does not modify any of the current device options.
 *
 * Since the transmitter is not disabled, frames currently in internal buffers
 * or in process by a DMA engine are allowed to be transmitted.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return N/A
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 * 
 ******************************************************************************/
void XLlTemac_Stop(XLlTemac *InstancePtr)
{
	u32 Reg;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);

	/* If already stopped, then there is nothing to do */
	if (InstancePtr->IsStarted == 0) {
		return;
	}

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_Stop\n");
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_Stop: disabling interrupts\n");
	/* Disable interrupts */
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress, XTE_IE_OFFSET, 0);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_Stop: disabling receiver\n");
	/* Disable the receiver */
	Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
				       XTE_RCW1_OFFSET);
	Reg &= ~XTE_RCW1_RX_MASK;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_RCW1_OFFSET, Reg);

	/* Stopping the receiver in mid-packet causes a dropped packet indication
	 * from HW. Clear it.
	 */
	/* get the interrupt pending register */
	Reg = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress, XTE_IP_OFFSET);
	if (Reg & XTE_INT_RXRJECT_MASK) {
		/* set the interrupt status register to clear the interrupt */
		XLlTemac_WriteReg(InstancePtr->Config.BaseAddress,
				  XTE_IS_OFFSET, XTE_INT_RXRJECT_MASK);
	}

	/* Mark as stopped */
	InstancePtr->IsStarted = 0;
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_Stop: done\n");
}


/*****************************************************************************/
/**
 * XLlTemac_Reset performs a reset of the TEMAC channel, specified by
 * <i>InstancePtr</i>, or both channels if <i>HardCoreAction</i> is set to
 * XTE_RESET_HARD.
 *
 * XLlTemac_Reset also resets the TEMAC channel's options to their default values.
 *
 * The calling software is responsible for re-configuring the TEMAC channel
 * (if necessary) and restarting the MAC after the reset.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param HardCoreAction describes how XLlTemac_Reset should treat the hard core
 *        block of the TEMAC.<br><br>
 *
 *        If XTE_RESET_HARD is set to XTE_RESET_HARD, then XLlTemac_Reset asserts
 *        the reset signal to the hard core block which will reset both channels
 *        of the TEMAC. This, of course, will bork any activity that may be
 *        occuring on the other channel. So, be careful here.<br><br>
 *
 *        Otherwise, XLlTemac_Reset resets just the transmitter and receiver of
 *        this TEMAC channel.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
void XLlTemac_Reset(XLlTemac *InstancePtr, int HardCoreAction)
{
	u32 Reg;
	u32 TimeoutCount = 2;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_Reset\n");
	/* Stop the device and reset HW */
	XLlTemac_Stop(InstancePtr);
	InstancePtr->Options = XTE_DEFAULT_OPTIONS;

	/* Reset the receiver */
	xdbg_printf(XDBG_DEBUG_GENERAL, "resetting the receiver\n");
	Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
				       XTE_RCW1_OFFSET);
	Reg |= XTE_RCW1_RST_MASK;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_RCW1_OFFSET, Reg);

	/* Reset the transmitter */
	xdbg_printf(XDBG_DEBUG_GENERAL, "resetting the transmitter\n");
	Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
				       XTE_TC_OFFSET);
	Reg |= XTE_TC_RST_MASK;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_TC_OFFSET, Reg);

	xdbg_printf(XDBG_DEBUG_GENERAL, "waiting until reset is done\n");
	/* Poll until the reset is done */
	while (Reg & (XTE_RCW1_RST_MASK | XTE_TC_RST_MASK)) {
		Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					       XTE_RCW1_OFFSET);
		Reg |= XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
						XTE_TC_OFFSET);
	}

	/* Reset hard core if required */
	/* Resetting hard core will cause both channels to reset :-( */
	if (HardCoreAction == XTE_RESET_HARD) {
		xdbg_printf(XDBG_DEBUG_GENERAL, "hard reset\n");
		Reg = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				       XTE_RAF_OFFSET);
		XLlTemac_WriteReg(InstancePtr->Config.BaseAddress,
				  XTE_RAF_OFFSET, Reg | XTE_RAF_HTRST_MASK);
		while (TimeoutCount &&
		       (!(XLlTemac_ReadReg
			  (InstancePtr->Config.BaseAddress,
			   XTE_RDY_OFFSET) & XTE_RDY_HARD_ACS_RDY_MASK))) {
			udelay(XTE_RESET_HARD_DELAY_US);
			TimeoutCount--;
		}
	}

	/* Setup HW */
	InitHw(InstancePtr);
}


/******************************************************************************
 * InitHw (internal use only) performs a one-time setup of a TEMAC channel. The
 * setup performed here only need to occur once after any reset.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
static void InitHw(XLlTemac *InstancePtr)
{
	u32 Reg;

	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac InitHw\n");
	/* Disable the receiver */
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac InitHw\n");
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac InitHw: disabling receiver\n");
	Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
				       XTE_RCW1_OFFSET);
	Reg &= ~XTE_RCW1_RX_MASK;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_RCW1_OFFSET, Reg);

	/*
	 * Stopping the receiver in mid-packet causes a dropped packet
	 * indication from HW. Clear it.
	 */
	/* get the interrupt pending register */
	Reg = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress, XTE_IP_OFFSET);
	if (Reg & XTE_INT_RXRJECT_MASK) {
		/*
		 * set the interrupt status register to clear the pending
		 * interrupt
		 */
		XLlTemac_WriteReg(InstancePtr->Config.BaseAddress,
				  XTE_IS_OFFSET, XTE_INT_RXRJECT_MASK);
	}

	/* Sync default options with HW but leave receiver and transmitter
	 * disabled. They get enabled with XLlTemac_Start() if
	 * XTE_TRANSMITTER_ENABLE_OPTION and XTE_RECEIVER_ENABLE_OPTION are set
	 */
	XLlTemac_SetOptions(InstancePtr, InstancePtr->Options &
			    ~(XTE_TRANSMITTER_ENABLE_OPTION |
			      XTE_RECEIVER_ENABLE_OPTION));

	XLlTemac_ClearOptions(InstancePtr, ~InstancePtr->Options);

	/* Set default MDIO divisor */
	XLlTemac_PhySetMdioDivisor(InstancePtr, XTE_MDIO_DIV_DFT);
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac InitHw: done\n");
}

/*****************************************************************************/
/**
 * XLlTemac_SetMacAddress sets the MAC address for the TEMAC channel, specified
 * by <i>InstancePtr</i> to the MAC address specified by <i>AddressPtr</i>.
 * The TEMAC channel must be stopped before calling this function.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param AddressPtr is a reference to the 6-byte MAC address to set.
 *
 * @return On successful completion, XLlTemac_SetMacAddress returns XST_SUCCESS.
 *         Otherwise, if the TEMAC channel has not stopped,
 *         XLlTemac_SetMacAddress returns XST_DEVICE_IS_STARTED.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_SetMacAddress(XLlTemac *InstancePtr, void *AddressPtr)
{
	u32 MacAddr;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(AddressPtr != NULL);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
					 XTE_RDY_OFFSET) &
			XTE_RDY_HARD_ACS_RDY_MASK);

	/* Be sure device has been stopped */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_SetMacAddress: setting mac address to: 0x%08x%8x%8x%8x%8x%8x\n",
		    Aptr[0], Aptr[1], Aptr[2], Aptr[3], Aptr[4], Aptr[5]);
	/*
	 * Set the MAC bits [31:0] in UAW0
	 * Having Aptr be unsigned type prevents the following operations from sign extending
	 */
	MacAddr = Aptr[0];
	MacAddr |= Aptr[1] << 8;
	MacAddr |= Aptr[2] << 16;
	MacAddr |= Aptr[3] << 24;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_UAW0_OFFSET, MacAddr);

	/* There are reserved bits in UAW1 so don't affect them */
	MacAddr = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					   XTE_UAW1_OFFSET);
	MacAddr &= ~XTE_UAW1_UNICASTADDR_MASK;

	/* Set MAC bits [47:32] in UAW1 */
	MacAddr |= Aptr[4];
	MacAddr |= Aptr[5] << 8;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_UAW1_OFFSET, MacAddr);

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * XLlTemac_GetMacAddress gets the MAC address for the TEMAC channel, specified
 * by <i>InstancePtr</i> into the memory buffer specified by <i>AddressPtr</i>.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param AddressPtr references the memory buffer to store the retrieved MAC
 *        address. This memory buffer must be at least 6 bytes in length.
 *
 * @return N/A
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
void XLlTemac_GetMacAddress(XLlTemac *InstancePtr, void *AddressPtr)
{
	u32 MacAddr;
	u8 *Aptr = (u8 *) AddressPtr;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);

	/* Read MAC bits [31:0] in UAW0 */
	MacAddr = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					   XTE_UAW0_OFFSET);
	Aptr[0] = (u8) MacAddr;
	Aptr[1] = (u8) (MacAddr >> 8);
	Aptr[2] = (u8) (MacAddr >> 16);
	Aptr[3] = (u8) (MacAddr >> 24);

	/* Read MAC bits [47:32] in UAW1 */
	MacAddr = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					   XTE_UAW1_OFFSET);
	Aptr[4] = (u8) MacAddr;
	Aptr[5] = (u8) (MacAddr >> 8);
}

/*****************************************************************************/
/**
 * XLlTemac_SetOptions enables the options, <i>Options</i> for the TEMAC channel,
 * specified by <i>InstancePtr</i>. The TEMAC channel should be stopped with
 * XLlTemac_Stop() before changing options.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param Options is a bitmask of OR'd XTE_*_OPTION values for options to
 *        set. Options not specified are not affected.
 *
 * @return On successful completion, XLlTemac_SetOptions returns XST_SUCCESS.
 *         Otherwise, if the device has not been stopped, XLlTemac_SetOptions
 *         returns XST_DEVICE_IS_STARTED.
 *
 * @note
 * See xlltemac.h for a description of the available options.
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_SetOptions(XLlTemac *InstancePtr, u32 Options)
{
	u32 Reg;		/* Generic register contents */
	u32 RegRcw1;		/* Reflects original contents of RCW1 */
	u32 RegTc;		/* Reflects original contents of TC  */
	u32 RegNewRcw1;		/* Reflects new contents of RCW1 */
	u32 RegNewTc;		/* Reflects new contents of TC  */

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
					 XTE_RDY_OFFSET) &
			XTE_RDY_HARD_ACS_RDY_MASK);

	/* Be sure device has been stopped */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_SetOptions\n");
	/* Many of these options will change the RCW1 or TC registers.
	 * To reduce the amount of IO to the device, group these options here
	 * and change them all at once. 
	 */

	/* Grab current register contents */
	RegRcw1 = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					   XTE_RCW1_OFFSET);
	RegTc = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					 XTE_TC_OFFSET);
	RegNewRcw1 = RegRcw1;
	RegNewTc = RegTc;

	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "current control regs: RCW1: 0x%0x; TC: 0x%0x\n", RegRcw1,
		    RegTc);
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "Options: 0x%0x; default options: 0x%0x\n", Options,
		    XTE_DEFAULT_OPTIONS);

	/* Turn on jumbo packet support for both Rx and Tx */
	if (Options & XTE_JUMBO_OPTION) {
		RegNewTc |= XTE_TC_JUM_MASK;
		RegNewRcw1 |= XTE_RCW1_JUM_MASK;
	}

	/* Turn on VLAN packet support for both Rx and Tx */
	if (Options & XTE_VLAN_OPTION) {
		RegNewTc |= XTE_TC_VLAN_MASK;
		RegNewRcw1 |= XTE_RCW1_VLAN_MASK;
	}

	/* Turn on FCS stripping on receive packets */
	if (Options & XTE_FCS_STRIP_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "setOptions: enabling fcs stripping\n");
		RegNewRcw1 &= ~XTE_RCW1_FCS_MASK;
	}

	/* Turn on FCS insertion on transmit packets */
	if (Options & XTE_FCS_INSERT_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "setOptions: enabling fcs insertion\n");
		RegNewTc &= ~XTE_TC_FCS_MASK;
	}

	/* Turn on length/type field checking on receive packets */
	if (Options & XTE_LENTYPE_ERR_OPTION) {
		RegNewRcw1 &= ~XTE_RCW1_LT_DIS_MASK;
	}

	/* Enable transmitter */
	if (Options & XTE_TRANSMITTER_ENABLE_OPTION) {
		RegNewTc |= XTE_TC_TX_MASK;
	}

	/* Enable receiver */
	if (Options & XTE_RECEIVER_ENABLE_OPTION) {
		RegNewRcw1 |= XTE_RCW1_RX_MASK;
	}

	/* Change the TC or RCW1 registers if they need to be modified */
	if (RegTc != RegNewTc) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "setOptions: writting tc: 0x%0x\n", RegNewTc);
		XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
					  XTE_TC_OFFSET, RegNewTc);
	}

	if (RegRcw1 != RegNewRcw1) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "setOptions: writting rcw1: 0x%0x\n", RegNewRcw1);
		XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
					  XTE_RCW1_OFFSET, RegNewRcw1);
	}

	/* Rest of options twiddle bits of other registers. Handle them one at
	 * a time
	 */

	/* Turn on flow control */
	if (Options & XTE_FLOW_CONTROL_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "setOptions: endabling flow control\n");
		Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					       XTE_FCC_OFFSET);
		Reg |= XTE_FCC_FCRX_MASK;
		XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
					  XTE_FCC_OFFSET, Reg);
	}
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "setOptions: rcw1 is now (fcc): 0x%0x\n",
		    XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					     XTE_RCW1_OFFSET));

	/* Turn on promiscuous frame filtering (all frames are received ) */
	if (Options & XTE_PROMISC_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "setOptions: endabling promiscuous mode\n");
		Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					       XTE_AFM_OFFSET);
		Reg |= XTE_AFM_PM_MASK;
		XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
					  XTE_AFM_OFFSET, Reg);
	}
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "setOptions: rcw1 is now (afm): 0x%0x\n",
		    XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					     XTE_RCW1_OFFSET));

	/* Allow broadcast address filtering */
	if (Options & XTE_BROADCAST_OPTION) {
		Reg = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				       XTE_RAF_OFFSET);
		Reg &= ~XTE_RAF_BCSTREJ_MASK;
		XLlTemac_WriteReg(InstancePtr->Config.BaseAddress,
				  XTE_RAF_OFFSET, Reg);
	}
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "setOptions: rcw1 is now (raf): 0x%0x\n",
		    XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					     XTE_RCW1_OFFSET));

	/* Allow multicast address filtering */
	if (Options & XTE_MULTICAST_OPTION) {
		Reg = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				       XTE_RAF_OFFSET);
		Reg &= ~XTE_RAF_MCSTREJ_MASK;
		XLlTemac_WriteReg(InstancePtr->Config.BaseAddress,
				  XTE_RAF_OFFSET, Reg);
	}
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "setOptions: rcw1 is now (raf2): 0x%0x\n",
		    XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					     XTE_RCW1_OFFSET));

	/* The remaining options not handled here are managed elsewhere in the
	 * driver. No register modifications are needed at this time. Reflecting the
	 * option in InstancePtr->Options is good enough for now.
	 */

	/* Set options word to its new value */
	InstancePtr->Options |= Options;

	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "setOptions: rcw1 is now (end): 0x%0x\n",
		    XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					     XTE_RCW1_OFFSET));
	xdbg_printf(XDBG_DEBUG_GENERAL, "setOptions: returning SUCCESS\n");
	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * XLlTemac_ClearOptions clears the options, <i>Options</i> for the TEMAC channel,
 * specified by <i>InstancePtr</i>. The TEMAC channel should be stopped with
 * XLlTemac_Stop() before changing options.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param Options is a bitmask of OR'd XTE_*_OPTION values for options to
 *        clear. Options not specified are not affected.
 *
 * @return On successful completion, XLlTemac_ClearOptions returns XST_SUCCESS.
 *         Otherwise, if the device has not been stopped, XLlTemac_ClearOptions
 *         returns XST_DEVICE_IS_STARTED.
 *
 * @note
 * See xlltemac.h for a description of the available options.
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
int XLlTemac_ClearOptions(XLlTemac *InstancePtr, u32 Options)
{
	u32 Reg;		/* Generic */
	u32 RegRcw1;		/* Reflects original contents of RCW1 */
	u32 RegTc;		/* Reflects original contents of TC  */
	u32 RegNewRcw1;		/* Reflects new contents of RCW1 */
	u32 RegNewTc;		/* Reflects new contents of TC  */

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
					 XTE_RDY_OFFSET) &
			XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "Xtemac_ClearOptions: 0x%08x\n",
		    Options);
	/* Be sure device has been stopped */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Many of these options will change the RCW1 or TC registers.
	 * Group these options here and change them all at once. What we are
	 * trying to accomplish is to reduce the amount of IO to the device
	 */

	/* Grab current register contents */
	RegRcw1 = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					   XTE_RCW1_OFFSET);
	RegTc = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					 XTE_TC_OFFSET);
	RegNewRcw1 = RegRcw1;
	RegNewTc = RegTc;

	/* Turn off jumbo packet support for both Rx and Tx */
	if (Options & XTE_JUMBO_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: disabling jumbo\n");
		RegNewTc &= ~XTE_TC_JUM_MASK;
		RegNewRcw1 &= ~XTE_RCW1_JUM_MASK;
	}

	/* Turn off VLAN packet support for both Rx and Tx */
	if (Options & XTE_VLAN_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: disabling vlan\n");
		RegNewTc &= ~XTE_TC_VLAN_MASK;
		RegNewRcw1 &= ~XTE_RCW1_VLAN_MASK;
	}

	/* Turn off FCS stripping on receive packets */
	if (Options & XTE_FCS_STRIP_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: disabling fcs strip\n");
		RegNewRcw1 |= XTE_RCW1_FCS_MASK;
	}

	/* Turn off FCS insertion on transmit packets */
	if (Options & XTE_FCS_INSERT_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: disabling fcs insert\n");
		RegNewTc |= XTE_TC_FCS_MASK;
	}

	/* Turn off length/type field checking on receive packets */
	if (Options & XTE_LENTYPE_ERR_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: disabling lentype err\n");
		RegNewRcw1 |= XTE_RCW1_LT_DIS_MASK;
	}

	/* Disable transmitter */
	if (Options & XTE_TRANSMITTER_ENABLE_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: disabling transmitter\n");
		RegNewTc &= ~XTE_TC_TX_MASK;
	}

	/* Disable receiver */
	if (Options & XTE_RECEIVER_ENABLE_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: disabling receiver\n");
		RegNewRcw1 &= ~XTE_RCW1_RX_MASK;
	}

	/* Change the TC and RCW1 registers if they need to be
	 * modified
	 */
	if (RegTc != RegNewTc) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: setting TC: 0x%0x\n",
			    RegNewTc);
		XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
					  XTE_TC_OFFSET, RegNewTc);
	}

	if (RegRcw1 != RegNewRcw1) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: setting RCW1: 0x%0x\n",
			    RegNewRcw1);
		XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
					  XTE_RCW1_OFFSET, RegNewRcw1);
	}

	/* Rest of options twiddle bits of other registers. Handle them one at
	 * a time
	 */

	/* Turn off flow control */
	if (Options & XTE_FLOW_CONTROL_OPTION) {
		Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					       XTE_FCC_OFFSET);
		Reg &= ~XTE_FCC_FCRX_MASK;
		XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
					  XTE_FCC_OFFSET, Reg);
	}

	/* Turn off promiscuous frame filtering */
	if (Options & XTE_PROMISC_OPTION) {
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: disabling promiscuous mode\n");
		Reg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					       XTE_AFM_OFFSET);
		Reg &= ~XTE_AFM_PM_MASK;
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "Xtemac_ClearOptions: setting AFM: 0x%0x\n", Reg);
		XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
					  XTE_AFM_OFFSET, Reg);
	}

	/* Disable broadcast address filtering */
	if (Options & XTE_BROADCAST_OPTION) {
		Reg = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				       XTE_RAF_OFFSET);
		Reg |= XTE_RAF_BCSTREJ_MASK;
		XLlTemac_WriteReg(InstancePtr->Config.BaseAddress,
				  XTE_RAF_OFFSET, Reg);
	}

	/* Disable multicast address filtering */
	if (Options & XTE_MULTICAST_OPTION) {
		Reg = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				       XTE_RAF_OFFSET);
		Reg |= XTE_RAF_MCSTREJ_MASK;
		XLlTemac_WriteReg(InstancePtr->Config.BaseAddress,
				  XTE_RAF_OFFSET, Reg);
	}

	/* The remaining options not handled here are managed elsewhere in the
	 * driver. No register modifications are needed at this time. Reflecting the
	 * option in InstancePtr->Options is good enough for now.
	 */

	/* Set options word to its new value */
	InstancePtr->Options &= ~Options;

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * XLlTemac_GetOptions returns the current option settings.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return XLlTemac_GetOptions returns a bitmask of XTE_*_OPTION constants,
 *         each bit specifying an option that is currently active.
 *
 * @note
 * See xlltemac.h for a description of the available options.
 *
 ******************************************************************************/
u32 XLlTemac_GetOptions(XLlTemac *InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	return (InstancePtr->Options);
}

/*****************************************************************************/
/**
 * XLlTemac_GetOperatingSpeed gets the current operating link speed. This may be
 * the value set by XLlTemac_SetOperatingSpeed() or a hardware default.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return XLlTemac_GetOperatingSpeed returns the link speed in units of megabits
 *         per second.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
u16 XLlTemac_GetOperatingSpeed(XLlTemac *InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_NONVOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
					 XTE_RDY_OFFSET) &
			XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_GetOperatingSpeed\n");
	switch (XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					 XTE_EMMC_OFFSET) &
		XTE_EMMC_LINKSPEED_MASK) {
	case XTE_EMMC_LINKSPD_1000:
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "XLlTemac_GetOperatingSpeed: returning 1000\n");
		return (1000);

	case XTE_EMMC_LINKSPD_100:
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "XLlTemac_GetOperatingSpeed: returning 100\n");
		return (100);

	case XTE_EMMC_LINKSPD_10:
		xdbg_printf(XDBG_DEBUG_GENERAL,
			    "XLlTemac_GetOperatingSpeed: returning 10\n");
		return (10);

	default:
		return (0);
	}
}


/*****************************************************************************/
/**
 * XLlTemac_SetOperatingSpeed sets the current operating link speed. For any
 * traffic to be passed, this speed must match the current MII/GMII/SGMII/RGMII
 * link speed.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param Speed is the speed to set in units of Mbps. Valid values are 10, 100,
 *        or 1000. XLlTemac_SetOperatingSpeed ignores invalid values.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
void XLlTemac_SetOperatingSpeed(XLlTemac *InstancePtr, u16 Speed)
{
	u32 EmmcReg;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID((Speed == 10) || (Speed == 100) || (Speed == 1000));
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_SetOperatingSpeed\n");
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_SetOperatingSpeed: setting speed to: %d (0x%0x)\n",
		    Speed, Speed);
	/* Get the current contents of the EMAC config register and zero out
	 * speed bits
	 */
	EmmcReg = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
					   XTE_EMMC_OFFSET) &
		~XTE_EMMC_LINKSPEED_MASK;

	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_SetOperatingSpeed: current speed: 0x%0x\n",
		    EmmcReg);
	switch (Speed) {
	case 10:
		break;

	case 100:
		EmmcReg |= XTE_EMMC_LINKSPD_100;
		break;

	case 1000:
		EmmcReg |= XTE_EMMC_LINKSPD_1000;
		break;

	default:
		return;
	}

	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_SetOperatingSpeed: new speed: 0x%0x\n", EmmcReg);
	/* Set register and return */
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_EMMC_OFFSET, EmmcReg);
	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_SetOperatingSpeed: done\n");
}

/*****************************************************************************/
/**
 * XLlTemac_PhySetMdioDivisor sets the MDIO clock divisor in the TEMAC channel,
 * specified by <i>InstancePtr</i> to the value, <i>Divisor</i>. This function
 * must be called once after each reset prior to accessing MII PHY registers.
 *
 * From the Virtex-4 Embedded Tri-Mode Ethernet MAC User's Guide, the
 * following equation governs the MDIO clock to the PHY:
 *
 * <pre>
 *              f[HOSTCLK]
 *   f[MDC] = -----------------
 *            (1 + Divisor) * 2
 * </pre>
 *
 * where f[HOSTCLK] is the bus clock frequency in MHz, and f[MDC] is the
 * MDIO clock frequency in MHz to the PHY. Typically, f[MDC] should not
 * exceed 2.5 MHz. Some PHYs can tolerate faster speeds which means faster
 * access.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param Divisor is the divisor value to set within the range of 0 to
 *        XTE_MC_CLK_DVD_MAX.
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.
 *
 ******************************************************************************/
void XLlTemac_PhySetMdioDivisor(XLlTemac *InstancePtr, u8 Divisor)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY)
		XASSERT_VOID(Divisor <= XTE_MC_CLOCK_DIVIDE_MAX);

	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_PhySetMdioDivisor\n");
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_MC_OFFSET,
				  (u32) Divisor | XTE_MC_MDIOEN_MASK);
}

/*****************************************************************************/
/*
 * XLlTemac_PhyRead reads the specified PHY register, <i>RegiseterNum</i> on the
 * PHY specified by <i>PhyAddress</i> into <i>PhyDataPtr</i>. This Ethernet
 * driver does not require the device to be stopped before reading from the PHY.
 * It is the responsibility of the calling code to stop the device if it is
 * deemed necessary.
 *
 * Note that the TEMAC hardware provides the ability to talk to a PHY that
 * adheres to the Media Independent Interface (MII) as defined in the IEEE 802.3
 * standard.
 *
 * <b>It is important that calling code set up the MDIO clock with
 * XLlTemac_PhySetMdioDivisor() prior to accessing the PHY with this function.</b>
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param PhyAddress is the address of the PHY to be written (multiple
 *        PHYs supported).
 * @param RegisterNum is the register number, 0-31, of the specific PHY register
 *        to write.
 * @param PhyDataPtr is a reference to the location where the 16-bit result
 *        value is stored.
 *
 * @return N/A
 *
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.<br><br>
 *
 * There is the possibility that this function will not return if the hardware
 * is broken (i.e., it never sets the status bit indicating that the write is
 * done). If this is of concern, the calling code should provide a mechanism
 * suitable for recovery.
 *
 ******************************************************************************/
void XLlTemac_PhyRead(XLlTemac *InstancePtr, u32 PhyAddress,
		      u32 RegisterNum, u16 *PhyDataPtr)
{
	u32 MiiReg;
	u32 Rdy;
	u32 Ie;
	u32 Tis;

	XASSERT_VOID(InstancePtr != NULL);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);


	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_PhyRead: BaseAddress: 0x%08x\n",
		    InstancePtr->Config.BaseAddress);
	/*
	 * XLlTemac_PhyRead saves the state of the IE register so that it can
	 * clear the HardAcsCmplt bit and later restore the state of the IE
	 * register. Since XLlTemac_PhyRead will poll for the status already, the
	 * HardAcsCmplt bit is cleared in the IE register so that the
	 * application code above doesn't also receive the interrupt.
	 */
	Ie = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress, XTE_IE_OFFSET);
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress, XTE_IE_OFFSET,
			  Ie & ~XTE_INT_HARDACSCMPLT_MASK);

	/*
	 * This is a double indirect mechanism. We indirectly write the
	 * PHYAD and REGAD so we can read the PHY register back out in
	 * the LSW register.
	 *
	 * In this case, the method of reading the data is a little unusual.
	 * Normally to write to a TEMAC register, one would set the WEN bit
	 * in the CTL register so that the values of the LSW will be written.
	 *
	 * In this case, the WEN bit is not set, and the PHYAD and REGAD
	 * values in the LSW will still get sent to the PHY before actually
	 * reading the result in the LSW.
	 *
	 * What needs to be done, is the following:
	 * 1) Write lsw reg with the phyad, and the regad
	 * 2) write the ctl reg with the miimai value (BUT WEN bit set to 0!!!)
	 * 3) poll the ready bit
	 * 4) get the value out of lsw
	 */
	MiiReg = RegisterNum & XTE_MIIM_REGAD_MASK;
	MiiReg |= ((PhyAddress << XTE_MIIM_PHYAD_SHIFT) & XTE_MIIM_PHYAD_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_PhyRead: Mii Reg: 0x%0x; Value written: 0x%0x\n",
		    RegisterNum, MiiReg);
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress, XTE_LSW_OFFSET,
			  MiiReg);
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress, XTE_CTL_OFFSET,
			  XTE_MIIMAI_OFFSET);

	/*
	 * Wait here polling, until the value is ready to be read.
	 */
	do {
		Rdy = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				       XTE_RDY_OFFSET);
	} while (!(Rdy & XTE_RSE_MIIM_RR_MASK));

	/* Read data */
	*PhyDataPtr = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				       XTE_LSW_OFFSET);
	xdbg_printf(XDBG_DEBUG_GENERAL,
		    "XLlTemac_PhyRead: Value retrieved: 0x%0x\n", *PhyDataPtr);

	/*
	 * Clear MII status bits. The TIS register in the hard TEMAC doesn't
	 * use the 'write a 1 to clear' method, so we need to read the TIS
	 * register, clear the MIIM RST bit, and then write it back out.
	 */
	Tis = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
				       XTE_TIS_OFFSET);
	Tis &= ~XTE_RSE_MIIM_RR_MASK;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_TIS_OFFSET, Tis);

	/*
	 * restore the state of the IE reg
	 */
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress, XTE_IE_OFFSET, Ie);
}


/*****************************************************************************/
/*
 * XLlTemac_PhyWrite writes <i>PhyData</i> to the specified PHY register,
 * <i>RegiseterNum</i> on the PHY specified by <i>PhyAddress</i>. This Ethernet
 * driver does not require the device to be stopped before writing to the PHY.
 * It is the responsibility of the calling code to stop the device if it is
 * deemed necessary.
 *
 * Note that the TEMAC hardware provides the ability to talk to a PHY that
 * adheres to the Media Independent Interface (MII) as defined in the IEEE 802.3
 * standard.
 *
 * <b>It is important that calling code set up the MDIO clock with
 * XLlTemac_PhySetMdioDivisor() prior to accessing the PHY with this function.</b>
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 * @param PhyAddress is the address of the PHY to be written (multiple
 *        PHYs supported).
 * @param RegisterNum is the register number, 0-31, of the specific PHY register
 *        to write.
 * @param PhyData is the 16-bit value that will be written to the register.
 *
 * @return N/A
 *
 * @note
 *
 * This routine accesses the hard TEMAC registers through a shared interface
 * between both channels of the TEMAC. Becuase of this, the application/OS code
 * must provide mutual exclusive access to this routine with any of the other
 * routines in this TEMAC driverr.<br><br>
 *
 * There is the possibility that this function will not return if the hardware
 * is broken (i.e., it never sets the status bit indicating that the write is
 * done). If this is of concern, the calling code should provide a mechanism
 * suitable for recovery.
 *
 ******************************************************************************/
void XLlTemac_PhyWrite(XLlTemac *InstancePtr, u32 PhyAddress,
		       u32 RegisterNum, u16 PhyData)
{
	u32 MiiReg;
	u32 Rdy;
	u32 Ie;
	u32 Tis;

	XASSERT_VOID(InstancePtr != NULL);
	/*
	 * If the mutual exclusion is enforced properly in the calling code, we
	 * should never get into the following case.
	 */
	XASSERT_VOID(XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				      XTE_RDY_OFFSET) &
		     XTE_RDY_HARD_ACS_RDY_MASK);

	xdbg_printf(XDBG_DEBUG_GENERAL, "XLlTemac_PhyWrite\n");
	/*
	 * XLlTemac_PhyWrite saves the state of the IE register so that it can
	 * clear the HardAcsCmplt bit and later restore the state of the IE
	 * register. Since XLlTemac_PhyWrite will poll for the status already, the
	 * HardAcsCmplt bit is cleared in the IE register so that the
	 * application code above doesn't also receive the interrupt.
	 */
	Ie = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress, XTE_IE_OFFSET);
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress, XTE_IE_OFFSET,
			  Ie & ~XTE_INT_HARDACSCMPLT_MASK);

	/*
	 * This is a double indirect mechanism. We indirectly write the
	 * PhyData to the MIIMWD register, and then indirectly write PHYAD and
	 * REGAD so the value in MIIMWD will get written to the PHY.
	 */
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_MIIMWD_OFFSET, PhyData);

	MiiReg = RegisterNum & XTE_MIIM_REGAD_MASK;
	MiiReg |= ((PhyAddress << XTE_MIIM_PHYAD_SHIFT) & XTE_MIIM_PHYAD_MASK);

	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_MIIMAI_OFFSET, MiiReg);

	/*
	 * Wait here polling, until the value is ready to be read.
	 */
	do {
		Rdy = XLlTemac_ReadReg(InstancePtr->Config.BaseAddress,
				       XTE_RDY_OFFSET);
	} while (!(Rdy & XTE_RSE_MIIM_WR_MASK));

	/*
	 * Clear MII status bits. The TIS register in the hard TEMAC doesn't
	 * use the 'write a 1 to clear' method, so we need to read the TIS
	 * register, clear the MIIM WST bit, and then write it back out.
	 */
	Tis = XLlTemac_ReadIndirectReg(InstancePtr->Config.BaseAddress,
				       XTE_TIS_OFFSET);
	Tis &= XTE_RSE_MIIM_WR_MASK;
	XLlTemac_WriteIndirectReg(InstancePtr->Config.BaseAddress,
				  XTE_TIS_OFFSET, Tis);

	/*
	 * restore the state of the IE reg
	 */
	XLlTemac_WriteReg(InstancePtr->Config.BaseAddress, XTE_IE_OFFSET, Ie);
}
